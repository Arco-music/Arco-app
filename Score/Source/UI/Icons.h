#pragma once
// SCORE - icons drawn as paths (crisp at every size, in both themes)
#include "Widgets.h"

namespace score {

enum class Icon { Play, Pause, Stop, Record, Loop, Metro, ToStart, Undo, Redo, Menu, Gear, Folder, Keys, Plus, Close, Headphones,
                  Wave, Mixer, Line, Arrange, Next, Prev, Scissors, Trash, Copy, Export, Pencil, Pointer, Mic, Magnet, Eye, Synth, Piano, Down };

inline juce::Path iconPath(Icon i, juce::Rectangle<float> r) {
    juce::Path p; float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight(); auto c = r.getCentre();
    auto line = [&](float x1, float y1, float x2, float y2, float t) { juce::Path l; l.startNewSubPath(x + x1 * w, y + y1 * h); l.lineTo(x + x2 * w, y + y2 * h);
        juce::PathStrokeType(t * w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(l, l); p.addPath(l); };
    switch (i) {
        case Icon::Play: p.addTriangle(x + w * 0.22f, y + h * 0.12f, x + w * 0.22f, y + h * 0.88f, x + w * 0.9f, c.y); break;
        case Icon::Pause: p.addRoundedRectangle(x + w * 0.2f, y + h * 0.15f, w * 0.2f, h * 0.7f, 2); p.addRoundedRectangle(x + w * 0.6f, y + h * 0.15f, w * 0.2f, h * 0.7f, 2); break;
        case Icon::Stop: p.addRoundedRectangle(x + w * 0.2f, y + h * 0.2f, w * 0.6f, h * 0.6f, 2.5f); break;
        case Icon::Record: p.addEllipse(x + w * 0.18f, y + h * 0.18f, w * 0.64f, h * 0.64f); break;
        case Icon::ToStart: p.addRectangle(x + w * 0.15f, y + h * 0.18f, w * 0.12f, h * 0.64f); p.addTriangle(x + w * 0.85f, y + h * 0.18f, x + w * 0.85f, y + h * 0.82f, x + w * 0.3f, c.y); break;
        case Icon::Loop: { juce::Path l; l.addRoundedRectangle(x + w * 0.12f, y + h * 0.28f, w * 0.76f, h * 0.44f, h * 0.2f);
            juce::PathStrokeType(w * 0.1f).createStrokedPath(l, l); p.addPath(l); p.addTriangle(x + w * 0.52f, y + h * 0.14f, x + w * 0.52f, y + h * 0.42f, x + w * 0.7f, y + h * 0.28f); break; }
        case Icon::Metro: p.addTriangle(x + w * 0.3f, y + h * 0.88f, x + w * 0.7f, y + h * 0.88f, c.x, y + h * 0.12f); { juce::Path l; l.startNewSubPath(c.x, y + h * 0.7f); l.lineTo(x + w * 0.82f, y + h * 0.22f);
            juce::PathStrokeType(w * 0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(l, l); p.addPath(l); } break;
        case Icon::Undo: case Icon::Redo: { juce::Path a; bool u = i == Icon::Undo; float sx = u ? 1 : -1;
            a.addCentredArc(c.x, c.y + h * 0.08f, w * 0.3f, h * 0.3f, 0, u ? -1.9f : 1.9f, u ? 1.6f : -1.6f, true);
            juce::PathStrokeType(w * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(a, a); p.addPath(a);
            float ax = c.x - sx * w * 0.32f, ay = y + h * 0.3f; p.addTriangle(ax - sx * w * 0.14f, ay, ax + sx * w * 0.1f, ay - h * 0.18f, ax + sx * w * 0.1f, ay + h * 0.18f); break; }
        case Icon::Menu: for (int k = 0; k < 3; ++k) p.addRoundedRectangle(x + w * 0.18f, y + h * (0.24f + 0.22f * k), w * 0.64f, h * 0.09f, h * 0.04f); break;
        case Icon::Gear: { p.addEllipse(c.x - w * 0.26f, c.y - h * 0.26f, w * 0.52f, h * 0.52f);
            for (int k = 0; k < 8; ++k) { float a = k * juce::MathConstants<float>::pi / 4; juce::Path t; t.addRoundedRectangle(-w * 0.07f, -h * 0.42f, w * 0.14f, h * 0.2f, 1.5f);
                t.applyTransform(juce::AffineTransform::rotation(a).translated(c)); p.addPath(t); }
            juce::Path hole; hole.addEllipse(c.x - w * 0.1f, c.y - h * 0.1f, w * 0.2f, h * 0.2f); p.addPath(hole); p.setUsingNonZeroWinding(false); break; }
        case Icon::Folder: p.addRoundedRectangle(x + w * 0.1f, y + h * 0.3f, w * 0.8f, h * 0.55f, 2); p.addRoundedRectangle(x + w * 0.1f, y + h * 0.2f, w * 0.35f, h * 0.2f, 2); break;
        case Icon::Keys: { p.addRoundedRectangle(x + w * 0.08f, y + h * 0.2f, w * 0.84f, h * 0.6f, 2); juce::Path k;
            for (int j = 1; j < 4; ++j) k.addRectangle(x + w * (0.08f + 0.21f * j) - w * 0.012f, y + h * 0.2f, w * 0.024f, h * 0.6f);
            for (int j : { 0, 1, 2 }) k.addRectangle(x + w * (0.22f + 0.21f * j), y + h * 0.2f, w * 0.1f, h * 0.34f);
            p.addPath(k); p.setUsingNonZeroWinding(false); break; }
        case Icon::Plus: line(0.5f, 0.2f, 0.5f, 0.8f, 0.12f); line(0.2f, 0.5f, 0.8f, 0.5f, 0.12f); break;
        case Icon::Close: line(0.25f, 0.25f, 0.75f, 0.75f, 0.12f); line(0.75f, 0.25f, 0.25f, 0.75f, 0.12f); break;
        case Icon::Headphones: { juce::Path a; a.addCentredArc(c.x, c.y + h * 0.05f, w * 0.32f, h * 0.32f, 0, -1.6f, 1.6f, true);
            juce::PathStrokeType(w * 0.09f).createStrokedPath(a, a); p.addPath(a); p.addRoundedRectangle(x + w * 0.12f, c.y, w * 0.18f, h * 0.32f, 2); p.addRoundedRectangle(x + w * 0.7f, c.y, w * 0.18f, h * 0.32f, 2); break; }
        case Icon::Wave: for (int k = 0; k < 7; ++k) { float hh = std::sin((k + 1) * 0.9f) * 0.35f + 0.42f; p.addRoundedRectangle(x + w * (0.1f + k * 0.12f), c.y - h * hh * 0.5f, w * 0.07f, h * hh, 1.5f); } break;
        case Icon::Mixer: for (int k = 0; k < 3; ++k) { float xx = x + w * (0.22f + k * 0.28f); p.addRectangle(xx - w * 0.025f, y + h * 0.15f, w * 0.05f, h * 0.7f);
            float yy = y + h * (k == 1 ? 0.35f : 0.6f); p.addRoundedRectangle(xx - w * 0.1f, yy - h * 0.06f, w * 0.2f, h * 0.12f, 1.5f); } break;
        case Icon::Line: line(0.08f, 0.5f, 0.92f, 0.5f, 0.07f); for (int k = 0; k < 3; ++k) p.addEllipse(x + w * (0.12f + k * 0.3f), c.y - h * 0.12f, w * 0.18f, h * 0.24f); break;
        case Icon::Arrange: for (int k = 0; k < 3; ++k) p.addRoundedRectangle(x + w * (k == 1 ? 0.3f : 0.1f), y + h * (0.2f + k * 0.22f), w * (k == 2 ? 0.45f : 0.55f), h * 0.15f, 2); break;
        case Icon::Next: p.addTriangle(x + w * 0.2f, y + h * 0.15f, x + w * 0.2f, y + h * 0.85f, x + w * 0.7f, c.y); p.addRectangle(x + w * 0.7f, y + h * 0.15f, w * 0.12f, h * 0.7f); break;
        case Icon::Prev: p.addTriangle(x + w * 0.8f, y + h * 0.15f, x + w * 0.8f, y + h * 0.85f, x + w * 0.3f, c.y); p.addRectangle(x + w * 0.18f, y + h * 0.15f, w * 0.12f, h * 0.7f); break;
        case Icon::Scissors: p.addEllipse(x + w * 0.1f, y + h * 0.6f, w * 0.26f, h * 0.26f); p.addEllipse(x + w * 0.64f, y + h * 0.6f, w * 0.26f, h * 0.26f); line(0.3f, 0.65f, 0.75f, 0.1f, 0.08f); line(0.7f, 0.65f, 0.25f, 0.1f, 0.08f); break;
        case Icon::Trash: p.addRoundedRectangle(x + w * 0.25f, y + h * 0.3f, w * 0.5f, h * 0.58f, 2); p.addRoundedRectangle(x + w * 0.15f, y + h * 0.16f, w * 0.7f, h * 0.09f, 1.5f); break;
        case Icon::Copy: p.addRoundedRectangle(x + w * 0.3f, y + h * 0.3f, w * 0.52f, h * 0.55f, 2); { juce::Path o; o.addRoundedRectangle(x + w * 0.15f, y + h * 0.15f, w * 0.52f, h * 0.55f, 2);
            juce::PathStrokeType(w * 0.07f).createStrokedPath(o, o); p.addPath(o); } break;
        case Icon::Export: line(0.5f, 0.12f, 0.5f, 0.62f, 0.1f); p.addTriangle(x + w * 0.3f, y + h * 0.32f, x + w * 0.7f, y + h * 0.32f, c.x, y + h * 0.1f);
            { juce::Path o; o.startNewSubPath(x + w * 0.18f, y + h * 0.5f); o.lineTo(x + w * 0.18f, y + h * 0.86f); o.lineTo(x + w * 0.82f, y + h * 0.86f); o.lineTo(x + w * 0.82f, y + h * 0.5f);
              juce::PathStrokeType(w * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(o, o); p.addPath(o); } break;
        case Icon::Pencil: line(0.2f, 0.8f, 0.78f, 0.22f, 0.16f); break;
        case Icon::Pointer: p.addTriangle(x + w * 0.25f, y + h * 0.1f, x + w * 0.25f, y + h * 0.8f, x + w * 0.75f, y + h * 0.55f); break;
        case Icon::Mic: p.addRoundedRectangle(x + w * 0.36f, y + h * 0.1f, w * 0.28f, h * 0.48f, w * 0.14f); { juce::Path a; a.addCentredArc(c.x, y + h * 0.45f, w * 0.26f, h * 0.24f, 0, 1.6f, 4.7f, true);
            juce::PathStrokeType(w * 0.07f).createStrokedPath(a, a); p.addPath(a); } line(0.5f, 0.7f, 0.5f, 0.88f, 0.07f); break;
        case Icon::Magnet: { juce::Path a; a.addCentredArc(c.x, y + h * 0.5f, w * 0.28f, h * 0.28f, 0, 1.57f, 4.71f, true); a.lineTo(x + w * 0.22f, y + h * 0.2f);
            a.startNewSubPath(x + w * 0.78f, y + h * 0.5f); a.lineTo(x + w * 0.78f, y + h * 0.2f);
            juce::PathStrokeType(w * 0.14f).createStrokedPath(a, a); p.addPath(a); } break;
        case Icon::Eye: p.addEllipse(x + w * 0.1f, y + h * 0.3f, w * 0.8f, h * 0.4f); { juce::Path e; e.addEllipse(c.x - w * 0.12f, c.y - h * 0.12f, w * 0.24f, h * 0.24f); p.addPath(e); p.setUsingNonZeroWinding(false); } break;
        case Icon::Synth: for (int k = 0; k < 24; ++k) { float t = k / 23.0f; float v = std::sin(t * 6.28f * 1.5f) * 0.3f; p.addEllipse(x + w * (0.1f + 0.8f * t) - 1, c.y - h * v - 1, 2.6f, 2.6f); } break;
        case Icon::Piano: p.addRoundedRectangle(x + w * 0.1f, y + h * 0.15f, w * 0.8f, h * 0.7f, 2); break;
        case Icon::Down: p.addTriangle(x + w * 0.2f, y + h * 0.35f, x + w * 0.8f, y + h * 0.35f, c.x, y + h * 0.72f); break;
    }
    return p;
}

class IconButton : public juce::Component, public juce::SettableTooltipClient {
public:
    IconButton(Icon i, juce::String tip = {}, ColourFn bg_ = nullptr) : icon(i), bg(std::move(bg_)) { setTooltip(tip); setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    Icon icon; ColourFn bg, iconColour, litColour; std::function<bool()> lit; std::function<void()> onClick; std::function<Icon()> dynIcon;
    juce::String caption; float pad = 0.26f;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        bool on = lit && lit();
        auto c = on ? (litColour ? litColour() : Theme::blue()) : (bg ? bg() : Theme::btn());
        if (isMouseOver()) c = c.brighter(Theme::lightMode() ? 0.05f : 0.12f);
        if (isMouseButtonDown()) c = c.darker(0.15f);
        g.setColour(c); g.fillRoundedRectangle(r, 7.0f);
        auto ic = r;
        if (caption.isNotEmpty()) ic = r.removeFromLeft(r.getHeight());
        float s = juce::jmin(ic.getWidth(), ic.getHeight()) * (1 - 2 * pad);
        auto ir = ic.withSizeKeepingCentre(s, s);
        g.setColour(iconColour ? iconColour() : (on ? juce::Colours::white : Theme::txt()));
        g.fillPath(iconPath(dynIcon ? dynIcon() : icon, ir));
        if (caption.isNotEmpty()) { g.setFont(Theme::font(9.5f, 2)); g.drawFittedText(caption, r.toNearestInt().withTrimmedRight(6), juce::Justification::centredLeft, 1, 0.7f); }
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override { repaint(); if (getLocalBounds().contains(e.getPosition()) && onClick) onClick(); }
};

} // namespace score
