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
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        auto c = bg ? bg() : Theme::btn();
        if (isMouseOver()) c = c.brighter(Theme::lightMode() ? 0.08f : 0.12f);
        if (isMouseButtonDown()) c = c.darker(0.15f);
        g.setColour(c); g.fillRoundedRectangle(r, radius);
        g.setColour(textColour ? textColour() : juce::Colours::white);
        g.setFont(Theme::font(fsize, weight));
        g.drawFittedText(dynText ? dynText() : text, getLocalBounds().reduced(2, 0), juce::Justification::centred, 1, 0.7f);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override { repaint(); if (getLocalBounds().contains(e.getPosition()) && onClick) onClick(); }
};

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
        // face
        g.setColour(Theme::card2()); g.fillEllipse(c.x - rad * 0.74f, c.y - rad * 0.74f, rad * 1.48f, rad * 1.48f);
        // track + value arc
        juce::Path tr; tr.addCentredArc(c.x, c.y, rad * 0.9f, rad * 0.9f, 0, a0, a1, true);
        g.setColour(Theme::line()); g.strokePath(tr, juce::PathStrokeType(d * 0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float start = lo < 0 && hi > 0 ? (float) ((0 - lo) / (hi - lo)) : 0.0f;
        float ta = a0 + (a1 - a0) * juce::jmin(start, t), tb = a0 + (a1 - a0) * juce::jmax(start, t);
        auto cc = enabled ? (col ? col() : Theme::txt()) : Theme::line();
        if (tb - ta > 0.001f) {
            juce::Path va; va.addCentredArc(c.x, c.y, rad * 0.9f, rad * 0.9f, 0, ta, tb, true);
            g.setColour(cc); g.strokePath(va, juce::PathStrokeType(d * 0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        float an = a0 + (a1 - a0) * t - juce::MathConstants<float>::halfPi;
        g.setColour(enabled ? Theme::txt() : Theme::sub());
        juce::Point<float> p1(c.x + std::cos(an) * rad * 0.22f, c.y + std::sin(an) * rad * 0.22f), p2(c.x + std::cos(an) * rad * 0.62f, c.y + std::sin(an) * rad * 0.62f);
        g.drawLine({ p1, p2 }, d * 0.07f);
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
    void paint(juce::Graphics& gr) override {
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
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 5.0f);
        auto* b = buf(); int c = ch();
        if (b != nullptr && c >= 0 && c < b->ch && b->len > 4) {
            int npt = (int) (b->len / 2);
            juce::Path p; float cy = r.getCentreY(), h = r.getHeight() * 0.46f;
            const float* d = b->chan(c);
            p.startNewSubPath(r.getX(), cy);
            for (int i = 0; i < npt; ++i) p.lineTo(r.getX() + r.getWidth() * i / (float) (npt - 1), cy - juce::jmin(1.0f, std::abs(d[2 * i])) * h);
            for (int i = npt - 1; i >= 0; --i) p.lineTo(r.getX() + r.getWidth() * i / (float) (npt - 1), cy + juce::jmin(1.0f, std::abs(d[2 * i])) * h);
            p.closeSubPath();
            g.setColour(col()); g.fillPath(p);
        }
        if (dragOver) { g.setColour(Theme::amber()); g.drawRoundedRectangle(r.reduced(1), 5.0f, 2.0f);
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
        g.setColour(col()); g.strokePath(p, juce::PathStrokeType(1.0f));
    }
private:
    Ring* ring; ColourFn col;
};

} // namespace arco
