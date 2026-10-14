#pragma once
#include <JuceHeader.h>
#include <functional>
#include "Theme.h"
#include "../Engine/Engine.h"

namespace arco {

using ColourFn = std::function<juce::Colour()>;
using ValueFn = std::function<double()>;
using SetFn = std::function<void(double)>;

// ---------------------------------------------------------------- a rounded button
class Pill : public juce::Component, public juce::SettableTooltipClient {
public:
    Pill(juce::String t, ColourFn bg_, float fs = 10.0f, float r = 7.0f) : text(std::move(t)), bg(std::move(bg_)), fsize(fs), radius(r) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    std::function<juce::String()> dynText;
    ColourFn textColour;
    juce::String text; ColourFn bg; float fsize, radius; int weight = 2;
    // 1.6: tint = an "on" colour shows as a soft wash with coloured text (solid only when solidFn says so);
    // seg = a part of a segmented control (1 left, 2 middle, 3 right, 4 alone): shared background, thin dividers
    bool tint = false; int seg = 0; std::function<bool()> solidFn; int icon = 0;   // icon: see drawIcon
    std::function<bool()> onFn;      // with tint: is it "on" (else: on = its colour differs from btn())
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        auto c = bg ? bg() : Theme::btn();
        bool on = onFn ? onFn() : (c != Theme::btn() && c != Theme::card2() && c.getAlpha() > 0);
        juce::Colour fill = c, ink;
        if (tint && !(solidFn && solidFn())) {
            fill = on ? Theme::btn().interpolatedWith(c, Theme::lightMode() ? 0.28f : 0.20f) : (c.getAlpha() == 0 ? c : Theme::btn());
            ink = on ? Theme::ink(c) : Theme::sub();
        } else ink = Theme::textOn(c);
        if (seg != 0 && !on && !(solidFn && solidFn())) fill = Theme::btn();
        if (isMouseOver()) fill = fill.brighter(Theme::lightMode() ? 0.05f : 0.10f);
        if (isMouseButtonDown()) fill = fill.darker(0.12f);
        float rr = seg == 0 ? radius : juce::jmin(10.0f, r.getHeight() * 0.32f);
        if (seg == 0 || seg == 4) { g.setColour(fill); g.fillRoundedRectangle(r, rr); }
        else {
            juce::Path p; bool L = seg == 1, R = seg == 3;
            p.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), rr, rr, L, R, L, R);
            g.setColour(fill); g.fillPath(p);
            if (seg != 1) { g.setColour(Theme::line()); g.fillRect(r.getX(), r.getY() + 0.0f, 1.0f, r.getHeight()); }
        }
        auto tc = textColour ? textColour() : ink;
        auto txt = dynText ? dynText() : text;
        auto box = getLocalBounds().reduced(3, 0);
        if (icon != 0) {
            float is = juce::jmin((float) getHeight() * 0.42f, 14.0f);
            float tw = txt.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth(Theme::font(fsize, weight), txt) + 5.0f;
            float x0 = r.getCentreX() - (is + tw) * 0.5f;
            drawIcon(g, icon, juce::Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), tc);
            box = box.withTrimmedLeft((int) (x0 + is + 5 - box.getX())).withWidth((int) tw + 4);
            if (txt.isEmpty()) return;
            g.setColour(tc); g.setFont(Theme::font(fsize, weight)); g.drawText(txt, box, juce::Justification::centredLeft); return;
        }
        g.setColour(tc);
        g.setFont(Theme::font(fsize, weight));
        g.drawFittedText(txt, box, juce::Justification::centred, 1, 0.7f);
    }
    // 1 play  2 stop  3 rec dot  4 undo  5 headphones  6 folder  7 clear (x)  8 power  9 play/stop
    static void drawIcon(juce::Graphics& g, int k, juce::Rectangle<float> b, juce::Colour c) {
        g.setColour(c); float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
        juce::PathStrokeType st(juce::jmax(1.4f, w * 0.13f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (k == 1) { juce::Path p; p.addTriangle(x + w * 0.2f, y + h * 0.1f, x + w * 0.2f, y + h * 0.9f, x + w * 0.92f, y + h * 0.5f); g.fillPath(p); }
        else if (k == 2) g.fillRoundedRectangle(b.reduced(w * 0.14f), w * 0.12f);
        else if (k == 3) g.fillEllipse(b.reduced(w * 0.18f));
        else if (k == 4) { juce::Path p; p.addCentredArc(x + w * 0.52f, y + h * 0.56f, w * 0.36f, h * 0.32f, 0, -juce::MathConstants<float>::halfPi * 1.0f, juce::MathConstants<float>::pi * 0.75f, true);
                           g.strokePath(p, st); juce::Path a; a.addTriangle(x + w * 0.02f, y + h * 0.24f, x + w * 0.36f, y + h * 0.02f, x + w * 0.38f, y + h * 0.46f); g.fillPath(a); }
        else if (k == 5) { juce::Path p; p.addCentredArc(x + w * 0.5f, y + h * 0.56f, w * 0.38f, h * 0.42f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                           g.strokePath(p, st); g.fillRoundedRectangle(x + w * 0.06f, y + h * 0.55f, w * 0.22f, h * 0.38f, w * 0.06f); g.fillRoundedRectangle(x + w * 0.72f, y + h * 0.55f, w * 0.22f, h * 0.38f, w * 0.06f); }
        else if (k == 6) { juce::Path p; p.startNewSubPath(x + w * 0.06f, y + h * 0.22f); p.lineTo(x + w * 0.4f, y + h * 0.22f); p.lineTo(x + w * 0.5f, y + h * 0.34f); p.lineTo(x + w * 0.94f, y + h * 0.34f);
                           p.lineTo(x + w * 0.94f, y + h * 0.84f); p.lineTo(x + w * 0.06f, y + h * 0.84f); p.closeSubPath(); g.strokePath(p, st); }
        else if (k == 7) { g.drawLine(x + w * 0.2f, y + h * 0.2f, x + w * 0.8f, y + h * 0.8f, st.getStrokeThickness()); g.drawLine(x + w * 0.8f, y + h * 0.2f, x + w * 0.2f, y + h * 0.8f, st.getStrokeThickness()); }
        else if (k == 8) { juce::Path p; p.addCentredArc(x + w * 0.5f, y + h * 0.55f, w * 0.38f, h * 0.38f, 0, juce::MathConstants<float>::pi * 0.2f, juce::MathConstants<float>::pi * 1.8f, true);
                           g.strokePath(p, st); g.drawLine(x + w * 0.5f, y + h * 0.05f, x + w * 0.5f, y + h * 0.5f, st.getStrokeThickness()); }
        else if (k == 9) { juce::Path p; p.addTriangle(x, y + h * 0.18f, x, y + h * 0.82f, x + w * 0.5f, y + h * 0.5f); g.fillPath(p); g.fillRoundedRectangle(x + w * 0.6f, y + h * 0.26f, w * 0.4f, h * 0.48f, 1.5f); }
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override { repaint(); if (getLocalBounds().contains(e.getPosition()) && onClick) onClick(); }
};

// 1.6: a panel of the new design (soft shade, hairline)
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius = 16.0f) {
    if (Theme::lightMode()) { g.setColour(juce::Colour::fromRGBA(60, 50, 30, 16)); g.fillRoundedRectangle(r.translated(0, 2.5f).expanded(1.0f), radius); }
    else { g.setColour(juce::Colours::black.withAlpha(0.32f)); g.fillRoundedRectangle(r.translated(0, 4.0f).expanded(1.0f), radius); }
    g.setGradientFill(juce::ColourGradient(Theme::card().brighter(Theme::lightMode() ? 0.01f : 0.025f), 0, r.getY(), Theme::card(), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(Theme::lightMode() ? juce::Colour::fromRGBA(60, 50, 30, 18) : juce::Colours::white.withAlpha(0.035f)); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}
inline void drawToggle(juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour col) {
    g.setColour(on ? col : Theme::line()); g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    float d = r.getHeight() - 4; float x = on ? r.getRight() - 2 - d : r.getX() + 2;
    g.setColour(juce::Colours::black.withAlpha(0.25f)); g.fillEllipse(x, r.getY() + 2.6f, d, d);
    g.setColour(on ? juce::Colours::white : Theme::knobHi()); g.fillEllipse(x, r.getY() + 2, d, d);
}

// ---------------------------------------------------------------- an arc knob (drag up / down, double-click = back to start)
class Knob : public juce::Component, public juce::SettableTooltipClient {
public:
    Knob(ValueFn get_, SetFn set_, double lo_, double hi_, double def_, ColourFn col_) :
        get(std::move(get_)), set(std::move(set_)), lo(lo_), hi(hi_), def(def_), col(std::move(col_)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    ValueFn get; SetFn set; double lo, hi, def; ColourFn col;
    bool enabled = true;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        float d = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.getCentre(); float rad = d * 0.5f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        double v = get ? get() : def;
        float t = (float) ((v - lo) / juce::jmax(1e-9, hi - lo));
        // 1.6: the track ring, the value arc, a shaded face and a dot (as the new design)
        float sw = juce::jmax(2.2f, d * 0.065f), ring = rad - sw * 0.5f;
        juce::Path tr; tr.addCentredArc(c.x, c.y, ring, ring, 0, a0, a1, true);
        g.setColour(Theme::line()); g.strokePath(tr, juce::PathStrokeType(sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float start = lo < 0 && hi > 0 ? (float) ((0 - lo) / (hi - lo)) : 0.0f;
        float ta = a0 + (a1 - a0) * juce::jmin(start, t), tb = a0 + (a1 - a0) * juce::jmax(start, t);
        auto cc = enabled ? Theme::arc(col ? col() : Theme::txt()) : Theme::line();
        if (tb - ta > 0.001f) {
            juce::Path va; va.addCentredArc(c.x, c.y, ring, ring, 0, ta, tb, true);
            g.setColour(cc); g.strokePath(va, juce::PathStrokeType(sw * 1.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        float fr = ring - sw * 0.5f - juce::jmax(2.0f, d * 0.07f);
        auto face = juce::Rectangle<float>(c.x - fr, c.y - fr, fr * 2, fr * 2);
        if (Theme::lightMode()) { g.setColour(juce::Colours::black.withAlpha(0.10f)); g.fillEllipse(face.translated(0, 1.5f)); }
        else { g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillEllipse(face.translated(0, 2.0f)); }
        g.setGradientFill(juce::ColourGradient(Theme::knobHi(), face.getX() + fr * 0.8f, face.getY() + fr * 0.64f, Theme::knobLo(), face.getRight(), face.getBottom(), true));
        g.fillEllipse(face);
        g.setColour(Theme::knobEdge()); g.drawEllipse(face, 1.0f);
        float an = a0 + (a1 - a0) * t - juce::MathConstants<float>::halfPi;
        float pr = fr - juce::jmax(3.5f, fr * 0.28f), dr = juce::jmax(1.6f, d / 26.0f);
        g.setColour(enabled ? Theme::knobDot() : Theme::sub());
        g.fillEllipse(c.x + std::cos(an) * pr - dr, c.y + std::sin(an) * pr - dr, dr * 2, dr * 2);
        if (dragging) {
            g.setColour(Theme::txt()); g.setFont(Theme::font(8.0f, 1));
            g.drawText(juce::String(v, std::abs(hi - lo) <= 2 ? 2 : 0), getLocalBounds().withY(getHeight() / 2 - 6).withHeight(12), juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { start = get ? get() : def; dragging = true; startY = e.position.y; repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!enabled) return;
        double range = hi - lo, sens = e.mods.isShiftDown() ? 600.0 : 160.0;
        double v = juce::jlimit(lo, hi, start + (startY - e.position.y) / sens * range);
        if (set) set(v); repaint(); }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (set && enabled) set(def); repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (set && enabled) set(juce::jlimit(lo, hi, (get ? get() : def) + w.deltaY * (hi - lo) * 0.1)); repaint(); }
private:
    double start = 0; float startY = 0; bool dragging = false;
};

// ---------------------------------------------------------------- a vertical fader
class Fader : public juce::Component {
public:
    Fader(ValueFn g, SetFn s, double l, double h, double d) : get(std::move(g)), set(std::move(s)), lo(l), hi(h), def(d) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    ValueFn get; SetFn set; double lo, hi, def;
    // 1.6: the strip fader - a long thin track with a dB scale, a big handle in the strip's colour
    ColourFn col; bool scale = false;
    static double volToDb(double v) { double g = std::pow(juce::jmax(0.0, v) / 100.0, 2.0) * 2.0; return g <= 1e-6 ? -120.0 : 20.0 * std::log10(g); }
    static double dbToVol(double d) { return 100.0 * std::sqrt(std::pow(10.0, d / 20.0) / 2.0); }
    void paint(juce::Graphics& gr) override {
        if (scale) { paintScale(gr); return; }
        auto r = getLocalBounds().toFloat();
        gr.setColour(Theme::card2()); gr.fillRoundedRectangle(r, r.getWidth() * 0.5f);
        float t = (float) ((get() - lo) / (hi - lo));
        auto fill = r.withTop(r.getBottom() - r.getHeight() * t);
        gr.setColour(Theme::txt().withAlpha(0.18f)); gr.fillRoundedRectangle(fill, r.getWidth() * 0.5f);
        float y = r.getBottom() - r.getHeight() * t;
        auto cap = juce::Rectangle<float>(r.getX() - 1, juce::jlimit(r.getY(), r.getBottom() - 8.0f, y - 4.0f), r.getWidth() + 2, 8.0f);
        gr.setColour(Theme::txt()); gr.fillRoundedRectangle(cap, 3.0f);
    }
    void mouseDown(const juce::MouseEvent& e) override { startV = get(); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        double v = juce::jlimit(lo, hi, startV + (startY - e.position.y) / (getHeight() * (e.mods.isShiftDown() ? 4.0 : 1.0)) * (hi - lo));
        set(v); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { set(def); repaint(); }
    float capH() const { return juce::jlimit(22.0f, 34.0f, getWidth() * 0.5f); }
    void paintScale(juce::Graphics& gr) {
        auto b = getLocalBounds().toFloat();
        bool labels = b.getWidth() >= 58;
        float lx = labels ? 18.0f : 0.0f;
        float cx = lx + (b.getWidth() - lx) * 0.5f, ch = capH(), top = ch * 0.5f, bot = b.getHeight() - ch * 0.5f, len = bot - top;
        float cw = juce::jmin(60.0f, b.getWidth() - lx);
        auto yOf = [&](double v) { return bot - len * (float) ((v - lo) / (hi - lo)); };
        // the scale: +6 0 -6 -12 -24 -48 -inf
        const double marks[7] = { 6, 0, -6, -12, -24, -48, -200 };
        const char* names[7] = { "+6", "0", "-6", "-12", "-24", "-48", "" };
        for (int i = 0; i < 7; ++i) {
            float y = i == 6 ? bot : yOf(juce::jlimit(lo, hi, dbToVol(marks[i])));
            gr.setColour(Theme::line().withAlpha(i == 1 ? 1.0f : 0.75f));
            gr.fillRect(cx - cw * 0.5f - 3, y - 0.5f, cw + 6, i == 1 ? 1.5f : 1.0f);
            if (labels && i < 6) { gr.setColour(Theme::sub()); gr.setFont(Theme::font(7.0f, i == 1 ? 2 : 0));
                gr.drawText(names[i], juce::Rectangle<float>(0, y - 6, lx - 2, 12), juce::Justification::centredRight); }
        }
        auto c = col ? col() : Theme::txt();
        auto track = juce::Rectangle<float>(cx - 3.5f, top, 7.0f, len);
        gr.setColour(Theme::card2()); gr.fillRoundedRectangle(track, 3.5f);
        float y = yOf(juce::jlimit(lo, hi, get()));
        gr.setColour(c.withAlpha(0.85f)); gr.fillRoundedRectangle(track.withTop(y), 3.5f);
        // the handle
        auto cap = juce::Rectangle<float>(cx - cw * 0.5f, y - ch * 0.5f, cw, ch);
        gr.setColour(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.16f : 0.45f)); gr.fillRoundedRectangle(cap.translated(0, 2.5f).expanded(0.5f), 9.0f);
        auto top1 = Theme::lightMode() ? juce::Colour(0xffefeee9) : juce::Colour(0xffe6e8ec), bot1 = Theme::lightMode() ? juce::Colour(0xffd6d4cd) : juce::Colour(0xffb9bcc4);
        gr.setGradientFill(juce::ColourGradient(top1, cap.getX(), cap.getY(), bot1, cap.getX(), cap.getBottom(), false));
        gr.fillRoundedRectangle(cap, 9.0f);
        gr.setColour(juce::Colour(0xff8a8e97).withAlpha(0.7f)); gr.drawRoundedRectangle(cap.reduced(0.5f), 9.0f, 1.0f);
        gr.setColour(juce::Colour(0xff6b6f78)); gr.fillRect(cap.getCentreX() - cw * 0.28f, cap.getCentreY() - ch * 0.2f, cw * 0.56f, 1.3f);
        gr.fillRect(cap.getCentreX() - cw * 0.28f, cap.getCentreY() + ch * 0.2f - 1.3f, cw * 0.56f, 1.3f);
        gr.setColour(c); gr.fillRect(cap.getCentreX() - cw * 0.34f, cap.getCentreY() - 1.0f, cw * 0.68f, 2.0f);
    }
private:
    double startV = 0; float startY = 0;
};

// ---------------------------------------------------------------- a number (drag up / down; double-click to type)
class ValueBox : public juce::Component {
public:
    ValueBox(ValueFn g, SetFn s, double l, double h, int decimals = 0, ColourFn tri = nullptr) :
        get(std::move(g)), set(std::move(s)), lo(l), hi(h), dec(decimals), triCol(std::move(tri)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    ValueFn get; SetFn set; double lo, hi; int dec; ColourFn triCol; float fsize = 12.5f; bool readOnly = false; bool flat = false;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (!flat) { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 5.0f); }
        if (!readOnly && triCol) {
            juce::Path p; float h = r.getHeight() * 0.3f; float x = 5, y = r.getCentreY();
            p.addTriangle(x, y - h * 0.5f, x, y + h * 0.5f, x + h * 0.8f, y);
            g.setColour(triCol()); g.fillPath(p);
        }
        g.setColour(Theme::txt()); g.setFont(Theme::font(fsize, 1));
        g.drawText(juce::String(get(), dec), getLocalBounds().reduced(4, 0).withTrimmedLeft(readOnly ? 0 : 6),
                   readOnly ? juce::Justification::centred : juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override { startV = get(); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (readOnly) return;
        double step = dec > 0 ? 0.1 : 1.0;
        double v = juce::jlimit(lo, hi, startV + std::round((startY - e.position.y) / 4.0) * step * (e.mods.isShiftDown() ? 0.1 : 1.0));
        set(v); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (readOnly) return;
        auto* ed = new juce::TextEditor(); ed->setText(juce::String(get(), dec)); ed->setBounds(getLocalBounds());
        ed->setFont(Theme::font(fsize, 1)); ed->setJustification(juce::Justification::centred); ed->selectAll();
        ed->onReturnKey = [this, ed] { set(juce::jlimit(lo, hi, ed->getText().getDoubleValue())); juce::MessageManager::callAsync([ed] { delete ed; }); repaint(); };
        ed->onFocusLost = ed->onEscapeKey = [ed] { juce::MessageManager::callAsync([ed] { delete ed; }); };
        addAndMakeVisible(ed); ed->grabKeyboardFocus();
    }
private:
    double startV = 0; float startY = 0;
};

// ---------------------------------------------------------------- a level meter (green -> amber -> red), with decay
class Meter : public juce::Component {
public:
    explicit Meter(std::function<float()> peakFn, bool vertical = false) : peak(std::move(peakFn)), vert(vertical) { setInterceptsMouseClicks(false, false); }
    void update() { float p = peak(); level = juce::jmax(p, level * 0.86f); if (p > 0.98f) clipHold = 40; else if (clipHold > 0) --clipHold; repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
        float db = juce::Decibels::gainToDecibels(level, -60.0f);
        float t = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto col = db > -3 ? Theme::red() : (db > -12 ? Theme::amber() : Theme::green());
        g.setColour(clipHold > 0 ? Theme::red() : col);
        auto f = vert ? r.withTop(r.getBottom() - r.getHeight() * t) : r.withWidth(r.getWidth() * t);
        if (t > 0.01f) g.fillRoundedRectangle(f, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
    }
private:
    std::function<float()> peak; bool vert; float level = 0; int clipHold = 0;
};

// ---------------------------------------------------------------- the wave screen of a strip (reads the SCOPE buffer)
class WaveScreen : public juce::Component, public juce::FileDragAndDropTarget {
public:
    WaveScreen(Engine& e, std::function<SharedBuffer*()> buf_, std::function<int()> ch_, ColourFn col_) :
        engine(e), buf(std::move(buf_)), ch(std::move(ch_)), col(std::move(col_)) {}
    std::function<void(const juce::File&)> onDrop;
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> overlay;     // 1.6: the state written inside the screen
    void paint(juce::Graphics& g) override {
        auto full = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(full, 9.0f);
        auto r = overlay ? full.withTrimmedTop(15.0f).reduced(7.0f, 4.0f) : full;
        auto* b = buf(); int c = ch();
        if (b != nullptr && c >= 0 && c < b->ch && b->len > 4) {
            int npt = (int) (b->len / 2);
            juce::Path p; float cy = r.getCentreY(), h = r.getHeight() * 0.46f;
            const float* d = b->chan(c);
            p.startNewSubPath(r.getX(), cy);
            for (int i = 0; i < npt; ++i) p.lineTo(r.getX() + r.getWidth() * i / (float) (npt - 1), cy - juce::jmin(1.0f, std::abs(d[2 * i])) * h);
            for (int i = npt - 1; i >= 0; --i) p.lineTo(r.getX() + r.getWidth() * i / (float) (npt - 1), cy + juce::jmin(1.0f, std::abs(d[2 * i])) * h);
            p.closeSubPath();
            g.setColour(Theme::arc(col())); g.fillPath(p);
        }
        if (overlay) overlay(g, full);
        if (dragOver) { g.setColour(Theme::amber()); g.drawRoundedRectangle(full.reduced(1), 9.0f, 2.0f);
            g.setFont(Theme::font(8.5f, 2)); g.drawText("drop to import", getLocalBounds(), juce::Justification::centred); }
    }
    bool isInterestedInFileDrag(const juce::StringArray&) override { return onDrop != nullptr; }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override { dragOver = false; repaint(); if (onDrop && files.size() > 0) onDrop(juce::File(files[0])); }
private:
    Engine& engine; std::function<SharedBuffer*()> buf; std::function<int()> ch; ColourFn col; bool dragOver = false;
};

// ---------------------------------------------------------------- the little screen of an effect card
class MiniScope : public juce::Component {
public:
    MiniScope(Ring* r, ColourFn c) : ring(r), col(std::move(c)) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 5.0f);
        if (ring == nullptr) return;
        int w = ring->w.load(std::memory_order_acquire);
        const int n = 512;
        juce::Path p;
        float cy = r.getCentreY(), h = r.getHeight() * 0.8f;
        for (int i = 0; i < n; ++i) {
            float v = ring->d[(size_t) ((w - n + i + Ring::N) % Ring::N)];
            float x = r.getX() + 3 + (r.getWidth() - 6) * i / (float) (n - 1), y = cy - juce::jlimit(-0.5f, 0.5f, v) * h;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(Theme::arc(col())); g.strokePath(p, juce::PathStrokeType(1.2f));
    }
private:
    Ring* ring; ColourFn col;
};

// ---------------------------------------------------------------- MIDI learn: every knob, fader, number and button
namespace learn {
    inline bool isLearnable(juce::Component* c) {
        return dynamic_cast<Knob*>(c) || dynamic_cast<Fader*>(c) || dynamic_cast<ValueBox*>(c) || dynamic_cast<Pill*>(c); }
    inline juce::Component* widgetAt(juce::Component* c) { while (c != nullptr && !isLearnable(c)) c = c->getParentComponent(); return c; }
    // a stable name: the component ID when it has one ("p:vol4", "trk2:rec" ...), otherwise its place in the window
    inline juce::String idOf(juce::Component* w, juce::Component* root) {
        if (w->getComponentID().isNotEmpty()) return w->getComponentID();
        juce::StringArray path;
        for (auto* c = w; c != nullptr && c != root; c = c->getParentComponent()) {
            auto* p = c->getParentComponent(); if (p == nullptr) break;
            path.insert(0, juce::String(p->getIndexOfChildComponent(c)));
        }
        return "path:" + path.joinIntoString("/");
    }
    inline juce::Component* find(juce::Component* root, const juce::String& id) {
        if (id.startsWith("path:")) {
            auto* c = root;
            for (auto& t : juce::StringArray::fromTokens(id.substring(5), "/", "")) {
                int i = t.getIntValue(); if (c == nullptr || i < 0 || i >= c->getNumChildComponents()) return nullptr; c = c->getChildComponent(i); }
            return isLearnable(c) ? c : nullptr;
        }
        std::function<juce::Component*(juce::Component*)> rec = [&](juce::Component* c) -> juce::Component* {
            if (c->getComponentID() == id) return c;
            for (auto* k : c->getChildren()) if (auto* r = rec(k)) return r;
            return nullptr; };
        return rec(root);
    }
    // a MIDI value 0..127 (controllers) or a press (keys, pads, footswitches)
    inline void apply(juce::Component* w, int type, int value, bool press) {
        double t = value / 127.0;
        if (auto* k = dynamic_cast<Knob*>(w)) {
            if (type == 2) k->set(k->lo + t * (k->hi - k->lo));
            else if (press) { double v = k->get ? k->get() : k->lo; k->set(v > (k->lo + k->hi) * 0.5 ? k->lo : k->hi); }
        } else if (auto* f = dynamic_cast<Fader*>(w)) {
            if (type == 2) f->set(f->lo + t * (f->hi - f->lo));
        } else if (auto* b = dynamic_cast<ValueBox*>(w)) {
            if (type == 2 && !b->readOnly) b->set(std::round(b->lo + t * (b->hi - b->lo)));
        } else if (auto* p = dynamic_cast<Pill*>(w)) {
            if (press && p->onClick) p->onClick();
        }
        w->repaint();
    }
}

} // namespace arco
