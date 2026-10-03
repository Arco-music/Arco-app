#pragma once
// SCORE - the controls: buttons, knobs (bound to parameters), faders, meters, value boxes, menus.
#include <JuceHeader.h>
#include <functional>
#include "Theme.h"
#include "../App/Host.h"

namespace score {

using ColourFn = std::function<juce::Colour()>;

// what every control needs to reach: the host, and a few things the main window offers
struct Ui {
    static Ui*& get() { static Ui* u = nullptr; return u; }
    Host* host = nullptr;
    std::function<void(Param*, Uid ownerTrack)> automate;            // open an automation lane for this knob
    std::function<void(const juce::String&)> help;                   // a short hint in the status line
    std::function<void(juce::Component*)> showOverlay;              // a big panel over everything (synth, settings)
    std::function<void()> closeOverlay;
    std::function<void()> refresh;                                   // the piece changed: rebuild what is on screen
    bool touch() const {
       #if JUCE_IOS
        return true;
       #else
        return false;
       #endif
    }
};
inline Host& host() { return *Ui::get()->host; }

// long-press = right click on a touch screen
class LongPress : private juce::Timer {
public:
    std::function<void(juce::Point<int>)> onLong;
    void lpDown(const juce::MouseEvent& e) { at = e.getPosition(); fired = false; moved = false; if (onLong) startTimer(550); }
    void lpDrag(const juce::MouseEvent& e) { if (e.getPosition().getDistanceFrom(at) > 8) { moved = true; stopTimer(); } }
    void lpUp() { stopTimer(); }
    bool fired = false, moved = false;
private:
    void timerCallback() override { stopTimer(); if (!moved && onLong) { fired = true; onLong(at); } }
    juce::Point<int> at;
};

// ---------------------------------------------------------------- a rounded button
class Pill : public juce::Component, public juce::SettableTooltipClient {
public:
    Pill(juce::String t, ColourFn bg_ = nullptr, float fs = 10.5f, float r = 7.0f) : text(std::move(t)), bg(std::move(bg_)), fsize(fs), radius(r) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    std::function<juce::String()> dynText;
    std::function<bool()> lit;              // lit = drawn in litColour
    ColourFn litColour, textColour;
    juce::String text; ColourFn bg; float fsize, radius; int weight = 2;
    bool enabledLook = true;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        bool on = lit && lit();
        auto c = on ? (litColour ? litColour() : Theme::blue()) : (bg ? bg() : Theme::btn());
        if (!enabledLook) c = c.withAlpha(0.5f);
        if (isMouseOver()) c = c.brighter(Theme::lightMode() ? 0.06f : 0.12f);
        if (isMouseButtonDown()) c = c.darker(0.15f);
        g.setColour(c); g.fillRoundedRectangle(r, radius);
        auto tc = textColour ? textColour() : ((on || bg) && !Theme::lightMode() ? juce::Colours::white : (on ? juce::Colours::white : Theme::txt()));
        if (!enabledLook) tc = tc.withAlpha(0.5f);
        g.setColour(tc);
        g.setFont(Theme::font(fsize, weight));
        g.drawFittedText(dynText ? dynText() : text, getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.7f);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override { repaint(); if (getLocalBounds().contains(e.getPosition()) && onClick && enabledLook) onClick(); }
};

// ---------------------------------------------------------------- a knob for a parameter (drag up / down; double-click = start value)
class ParamKnob : public juce::Component, public juce::SettableTooltipClient, private LongPress {
public:
    ParamKnob(Param* p, ColourFn col_, Uid ownerTrack = 0) : param(p), col(std::move(col_)), track(ownerTrack) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        onLong = [this](juce::Point<int>) { menu(); };
        if (p) setTooltip(juce::String(p->name) + "  -  drag, double-click resets, right-click for more");
    }
    Param* param; ColourFn col; Uid track; bool showName = true, showValue = true; juce::String label;
    std::function<void()> onChange;
    void paint(juce::Graphics& g) override {
        if (!param) return;
        auto b = getLocalBounds();
        int textH = (showName ? 13 : 0) + (showValue ? 12 : 0);
        auto kr = b.withTrimmedBottom(textH).toFloat().reduced(2.0f);
        float d = juce::jmin(kr.getWidth(), kr.getHeight());
        auto c = kr.getCentre(); float rad = d * 0.5f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        float t = param->norm();
        g.setColour(Theme::card2()); g.fillEllipse(c.x - rad * 0.72f, c.y - rad * 0.72f, rad * 1.44f, rad * 1.44f);
        juce::Path tr; tr.addCentredArc(c.x, c.y, rad * 0.9f, rad * 0.9f, 0, a0, a1, true);
        g.setColour(Theme::line()); g.strokePath(tr, juce::PathStrokeType(d * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float start = param->min < 0 && param->max > 0 && !param->log ? param->toNorm(0) : 0.0f;
        float ta = a0 + (a1 - a0) * juce::jmin(start, t), tb = a0 + (a1 - a0) * juce::jmax(start, t);
        auto cc = col ? col() : Theme::txt();
        if (tb - ta > 0.001f) { juce::Path va; va.addCentredArc(c.x, c.y, rad * 0.9f, rad * 0.9f, 0, ta, tb, true);
            g.setColour(cc); g.strokePath(va, juce::PathStrokeType(d * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        float an = a0 + (a1 - a0) * t - juce::MathConstants<float>::halfPi;
        g.setColour(Theme::txt());
        g.drawLine({ c.x + std::cos(an) * rad * 0.2f, c.y + std::sin(an) * rad * 0.2f, c.x + std::cos(an) * rad * 0.6f, c.y + std::sin(an) * rad * 0.6f }, d * 0.075f);
        int y = (int) kr.getBottom() + 1;
        if (showName) { g.setColour(Theme::sub()); g.setFont(Theme::font(8.0f, 1));
            g.drawFittedText(label.isNotEmpty() ? label : juce::String(param->name), b.getX(), y, b.getWidth(), 12, juce::Justification::centred, 1, 0.6f); y += 13; }
        if (showValue) { g.setColour(dragging ? cc : Theme::txt().withAlpha(0.85f)); g.setFont(Theme::font(8.0f, dragging ? 2 : 0));
            g.drawFittedText(param->text(), b.getX(), y - 1, b.getWidth(), 12, juce::Justification::centred, 1, 0.6f); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!param) return;
        if (e.mods.isPopupMenu()) { menu(); return; }
        lpDown(e); startN = param->norm(); startY = e.position.y; dragging = true; host().knobTouched(); repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!param) return; lpDrag(e);
        if (fired) return;
        float sens = e.mods.isShiftDown() ? 800.0f : 180.0f;
        set(param->fromNorm(juce::jlimit(0.0f, 1.0f, startN + (startY - e.position.y) / sens))); }
    void mouseUp(const juce::MouseEvent&) override { lpUp(); dragging = false; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (param) { host().knobTouched(); set(param->def); } }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (!param) return; host().knobTouched();
        float step = param->isDiscrete() ? (w.deltaY > 0 ? 1.0f : -1.0f) / juce::jmax(1.0f, param->max - param->min) : w.deltaY * 0.08f;
        set(param->fromNorm(juce::jlimit(0.0f, 1.0f, param->norm() + step))); }
    void set(float v) { param->set(v); repaint(); if (onChange) onChange(); host().sendChangeMessage(); }
    void menu() {
        juce::PopupMenu m;
        m.addSectionHeader(juce::String(param->name) + "  " + param->text());
        m.addItem(1, "Type a value ...");
        m.addItem(2, "Back to start (" + juce::String(param->format(param->def)) + ")");
        if (track != 0 && param->modulatable) m.addItem(3, "Automate this knob");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
            if (r == 1) typeValue();
            if (r == 2) { host().knobTouched(); set(param->def); }
            if (r == 3 && Ui::get()->automate) Ui::get()->automate(param, track);
        });
    }
    void typeValue() {
        auto* ed = new juce::TextEditor(); ed->setText(juce::String(param->get(), 2)); ed->setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmax(60, getWidth()), 22));
        ed->setFont(Theme::font(10.0f, 1)); ed->setJustification(juce::Justification::centred); ed->selectAll();
        ed->onReturnKey = [this, ed] { host().knobTouched(); set(ed->getText().getFloatValue()); juce::MessageManager::callAsync([ed] { delete ed; }); };
        ed->onFocusLost = ed->onEscapeKey = [ed] { juce::MessageManager::callAsync([ed] { delete ed; }); };
        addAndMakeVisible(ed); ed->grabKeyboardFocus();
    }
private:
    float startN = 0, startY = 0; bool dragging = false;
};

// ---------------------------------------------------------------- a choice parameter as a box with a menu (or arrows)
class ParamChoice : public juce::Component, public juce::SettableTooltipClient {
public:
    ParamChoice(Param* p, ColourFn col_ = nullptr) : param(p), col(std::move(col_)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    Param* param; ColourFn col; std::function<void()> onChange; bool showName = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 5.0f);
        g.setColour(col ? col() : Theme::sub());
        juce::Path tri; float cx = r.getRight() - 9, cy = r.getCentreY(); tri.addTriangle(cx - 3.5f, cy - 2, cx + 3.5f, cy - 2, cx, cy + 2.5f); g.fillPath(tri);
        g.setColour(Theme::txt()); g.setFont(Theme::font(9.0f, 1));
        auto txt = param ? (showName ? juce::String(param->name) + "  " : juce::String()) + juce::String(param->text()) : juce::String();
        g.drawFittedText(txt, getLocalBounds().reduced(6, 0).withTrimmedRight(12), juce::Justification::centredLeft, 1, 0.7f);
    }
    void mouseDown(const juce::MouseEvent&) override {
        if (!param) return;
        juce::PopupMenu m;
        int n = (int) std::lround(param->max - param->min) + 1;
        for (int i = 0; i < n; ++i) m.addItem(i + 1, param->format(param->min + (float) i), true, param->geti() == (int) param->min + i);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMaximumNumColumns(n > 18 ? 2 : 1), [this](int r) {
            if (r > 0) { host().knobTouched(); param->set(param->min + (float) (r - 1)); repaint(); if (onChange) onChange(); host().sendChangeMessage(); } });
    }
};

// ---------------------------------------------------------------- an on/off parameter as a small lit button
class ParamToggle : public juce::Component, public juce::SettableTooltipClient {
public:
    ParamToggle(Param* p, juce::String t, ColourFn col_) : param(p), text(std::move(t)), col(std::move(col_)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    Param* param; juce::String text; ColourFn col; std::function<void()> onChange;
    void paint(juce::Graphics& g) override {
        bool on = param && param->on();
        g.setColour(on ? (col ? col() : Theme::blue()) : Theme::btn()); g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
        g.setColour(on ? juce::Colours::white : Theme::sub()); g.setFont(Theme::font(8.5f, 2));
        g.drawFittedText(text, getLocalBounds().reduced(2, 0), juce::Justification::centred, 1, 0.7f);
    }
    void mouseDown(const juce::MouseEvent&) override { if (!param) return; host().knobTouched(); param->set(param->on() ? 0.0f : 1.0f); repaint(); if (onChange) onChange(); host().sendChangeMessage(); }
};

// ---------------------------------------------------------------- a vertical fader (dB)
class Fader : public juce::Component {
public:
    explicit Fader(Param* p, ColourFn c = nullptr) : param(p), col(std::move(c)) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    Param* param; ColourFn col;
    float posOf(float db) const { return juce::jlimit(0.0f, 1.0f, db <= -60 ? 0.0f : std::pow((db + 60.0f) / 66.0f, 2.2f)); }
    float dbOf(float t) const { return t <= 0.0001f ? -60.0f : std::pow(t, 1.0f / 2.2f) * 66.0f - 60.0f; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); float w = 6;
        auto track = r.withSizeKeepingCentre(w, r.getHeight() - 10);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(track, 3);
        float t = posOf(param->get()), y = track.getBottom() - track.getHeight() * t;
        g.setColour((col ? col() : Theme::txt()).withAlpha(0.55f)); g.fillRoundedRectangle(track.withTop(y), 3);
        float z = track.getBottom() - track.getHeight() * posOf(0); g.setColour(Theme::sub().withAlpha(0.6f)); g.drawLine(r.getX() + 2, z, r.getX() + 7, z, 1);
        auto cap = juce::Rectangle<float>(r.getCentreX() - 11, y - 5, 22, 10);
        g.setColour(Theme::txt()); g.fillRoundedRectangle(cap, 3);
        g.setColour(Theme::bg()); g.drawLine(cap.getX() + 4, cap.getCentreY(), cap.getRight() - 4, cap.getCentreY(), 1.2f);
    }
    void mouseDown(const juce::MouseEvent& e) override { host().knobTouched(); startT = posOf(param->get()); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        float t = juce::jlimit(0.0f, 1.0f, startT + (startY - e.position.y) / ((getHeight() - 10) * (e.mods.isShiftDown() ? 4.0f : 1.0f)));
        param->set(dbOf(t)); repaint(); host().sendChangeMessage(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { host().knobTouched(); param->set(0); repaint(); }
private:
    float startT = 0, startY = 0;
};

// ---------------------------------------------------------------- a level meter (green -> amber -> red)
class Meter : public juce::Component {
public:
    explicit Meter(std::function<float()> peakFn, bool vertical = true) : peak(std::move(peakFn)), vert(vertical) { setInterceptsMouseClicks(false, false); }
    std::function<float()> peak;
    void update() { float p = peak ? peak() : 0.0f; float nl = juce::jmax(p, level * 0.88f); if (p > 0.99f) clip = 45; else if (clip > 0) --clip;
        if (std::abs(nl - level) > 0.002f || clip) { level = nl; repaint(); } else level = nl; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 2.0f);
        float db = juce::Decibels::gainToDecibels(level, -60.0f), t = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto col = db > -3 ? Theme::red() : (db > -12 ? Theme::amber() : Theme::green());
        g.setColour(clip ? Theme::red() : col);
        if (t > 0.005f) g.fillRoundedRectangle(vert ? r.withTop(r.getBottom() - r.getHeight() * t) : r.withWidth(r.getWidth() * t), 2.0f);
    }
private:
    bool vert; float level = 0; int clip = 0;
};

// ---------------------------------------------------------------- a number you drag (double-click to type)
class ValueBox : public juce::Component {
public:
    ValueBox(std::function<double()> g, std::function<void(double)> s, double l, double h, int decimals = 0, double step = 1.0)
        : get(std::move(g)), set(std::move(s)), lo(l), hi(h), dec(decimals), stepSize(step) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    std::function<double()> get; std::function<void(double)> set; double lo, hi; int dec; double stepSize;
    std::function<juce::String(double)> format; float fsize = 13.0f; ColourFn triCol; bool flat = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (!flat) { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 5.0f); }
        if (triCol) { juce::Path p; float h = r.getHeight() * 0.28f, x = 6, y = r.getCentreY(); p.addTriangle(x, y - h * 0.5f, x, y + h * 0.5f, x + h * 0.8f, y); g.setColour(triCol()); g.fillPath(p); }
        g.setColour(Theme::txt()); g.setFont(Theme::font(fsize, 2));
        g.drawFittedText(format ? format(get()) : juce::String(get(), dec), getLocalBounds().reduced(4, 0).withTrimmedLeft(triCol ? 8 : 0), juce::Justification::centred, 1, 0.6f);
    }
    void mouseDown(const juce::MouseEvent& e) override { startV = get(); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        double v = juce::jlimit(lo, hi, startV + std::round((startY - e.position.y) / 5.0) * stepSize * (e.mods.isShiftDown() ? 0.1 : 1.0));
        set(v); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override {
        auto* ed = new juce::TextEditor(); ed->setText(juce::String(get(), dec)); ed->setBounds(getLocalBounds());
        ed->setFont(Theme::font(fsize, 1)); ed->setJustification(juce::Justification::centred); ed->selectAll();
        ed->onReturnKey = [this, ed] { set(juce::jlimit(lo, hi, ed->getText().getDoubleValue())); juce::MessageManager::callAsync([ed] { delete ed; }); repaint(); };
        ed->onFocusLost = ed->onEscapeKey = [ed] { juce::MessageManager::callAsync([ed] { delete ed; }); };
        addAndMakeVisible(ed); ed->grabKeyboardFocus();
    }
private:
    double startV = 0; float startY = 0;
};

// ---------------------------------------------------------------- a small label drawn by hand
inline void label(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float size = 9.0f, int weight = 1, juce::Colour c = juce::Colour(),
                  juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(c.isTransparent() ? Theme::sub() : c); g.setFont(Theme::font(size, weight)); g.drawFittedText(t, r, j, 1, 0.7f);
}
inline void cardBg(juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f) { g.setColour(Theme::card()); g.fillRoundedRectangle(r, radius); }

// a text field in the SCORE look
inline void styleEditor(juce::TextEditor& e, float size = 11.0f) {
    e.setFont(Theme::font(size, 1));
    e.setColour(juce::TextEditor::backgroundColourId, Theme::card2()); e.setColour(juce::TextEditor::textColourId, Theme::txt());
    e.setColour(juce::TextEditor::outlineColourId, Theme::line()); e.setColour(juce::TextEditor::focusedOutlineColourId, Theme::blue());
    e.setColour(juce::TextEditor::highlightColourId, Theme::blue().withAlpha(0.4f)); e.setColour(juce::CaretComponent::caretColourId, Theme::txt());
}

} // namespace score
