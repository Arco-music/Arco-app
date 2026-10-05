#pragma once
// MIDI LEARN: a clear layer over the station. Every control that can be linked gets a frame; click one, then move a knob or
// press a key / pad / footswitch on any MIDI device. Linked controls show what they are linked to; click one again to unlink.
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"

namespace arco {

class LearnOverlay : public juce::Component, private juce::Timer {
public:
    LearnOverlay(juce::Component& root_, AudioHost& h) : root(root_), host(h) {
        setWantsKeyboardFocus(true);
        done.onClick = [this] { if (onDone) onDone(); };
        clear.onClick = [this] {
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Forget every MIDI link?", "All controls are unlinked.", "Forget all", "Cancel", nullptr,
                juce::ModalCallbackFunction::create([this](int r) { if (r == 1) { host.clearMidiMap(); waiting = {}; repaint(); } })); };
        addAndMakeVisible(done); addAndMakeVisible(clear);
    }
    std::function<void()> onDone;
    std::function<juce::Component*()> activeWindow;     // the window on top (SYNTH, SETUP ...) or the station

    void start() { waiting = {}; setVisible(true); toFront(true); grabKeyboardFocus(); startTimerHz(15); collect(); repaint(); }
    void stop() { stopTimer(); { const juce::ScopedLock sl(host.midiLock); host.learnTarget = {}; } waiting = {}; setVisible(false); }
    void learned() { waiting = {}; collect(); repaint(); }

    void resized() override { done.setBounds(getWidth() / 2 + 300, 12, 110, 34); clear.setBounds(getWidth() / 2 + 190, 16, 100, 26); }

    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bg().withAlpha(0.18f));
        auto accent = Theme::amber();
        for (auto& w : widgets) {
            if (w.c == nullptr || !w.c->isShowing()) continue;
            auto r = getLocalArea(w.c, w.c->getLocalBounds()).toFloat().expanded(2.0f);
            bool isWaiting = waiting.isNotEmpty() && w.id == waiting;
            auto link = host.linkText(w.id);
            if (isWaiting) { g.setColour(accent.withAlpha(0.35f)); g.fillRoundedRectangle(r, 6.0f); }
            g.setColour(isWaiting ? accent : (link.isNotEmpty() ? Theme::green() : Theme::ink(Theme::blue()).withAlpha(0.75f)));
            g.drawRoundedRectangle(r, 6.0f, isWaiting ? 2.4f : 1.4f);
            if (link.isNotEmpty()) {
                g.setFont(Theme::font(7.0f, 2));
                int tw = juce::jmax(28, (int) Theme::font(7.0f, 2).getStringWidthFloat(link) + 8);
                juce::Rectangle<float> tag(r.getCentreX() - tw * 0.5f, r.getBottom() - 5.0f, (float) tw, 12.0f);
                g.setColour(Theme::green()); g.fillRoundedRectangle(tag, 4.0f);
                g.setColour(Theme::textOn(Theme::green())); g.drawText(link, tag, juce::Justification::centred);
            }
        }
        // the banner
        auto b = juce::Rectangle<float>(getWidth() / 2.0f - 430, 6, 860, 46);
        g.setColour(Theme::card().withAlpha(0.97f)); g.fillRoundedRectangle(b, 12.0f);
        g.setColour(accent); g.drawRoundedRectangle(b, 12.0f, 1.5f);
        g.setColour(Theme::ink(accent)); g.setFont(Theme::font(13.0f, 2));
        g.drawText("MIDI LEARN", b.withTrimmedLeft(18).withWidth(140), juce::Justification::centredLeft);
        g.setColour(Theme::txt()); g.setFont(Theme::font(9.0f, 1));
        juce::String msg = waiting.isNotEmpty() ? juce::String::fromUTF8("now move a knob or press a key / pad / pedal on your MIDI device …  (click elsewhere to cancel)")
                                                : juce::String::fromUTF8("click any knob, fader or button, then move a MIDI control.  Green = linked (click it to unlink).");
        g.drawFittedText(msg, b.withTrimmedLeft(150).withTrimmedRight(250).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        auto p = e.getPosition();
        if (p.y < 56 && std::abs(p.x - getWidth() / 2) < 430) return;
        for (int i = widgets.size(); --i >= 0;) {
            auto& w = widgets.getReference(i);
            if (w.c == nullptr || !w.c->isShowing()) continue;
            if (getLocalArea(w.c, w.c->getLocalBounds()).expanded(2).contains(p)) {
                if (waiting.isEmpty() && host.linkText(w.id).isNotEmpty()) { host.unlink(w.id); repaint(); return; }
                waiting = w.id;
                { const juce::ScopedLock sl(host.midiLock); host.learnTarget = waiting; }
                repaint(); return;
            }
        }
        waiting = {};
        { const juce::ScopedLock sl(host.midiLock); host.learnTarget = {}; }
        repaint();
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::escapeKey) { if (waiting.isNotEmpty()) { waiting = {}; const juce::ScopedLock sl(host.midiLock); host.learnTarget = {}; repaint(); } else if (onDone) onDone(); return true; }
        return false;
    }

private:
    struct W { juce::Component::SafePointer<juce::Component> c; juce::String id; };
    juce::Array<W> widgets;
    juce::Component& root;
    AudioHost& host;
    juce::String waiting;
    Pill done { "DONE", [] { return Theme::amber(); }, 11.0f, 9.0f };
    Pill clear { "FORGET ALL", [] { return Theme::btn(); }, 7.5f, 7.0f };

    void collect() {
        widgets.clear();
        auto* top = activeWindow ? activeWindow() : &root;
        std::function<void(juce::Component*)> rec = [&](juce::Component* c) {
            if (c == this || !c->isVisible()) return;
            if (c != top && c->getParentComponent() == &root && top == &root && !isStationPart(c)) return;
            if (learn::isLearnable(c) && c != top) { widgets.add({ c, learn::idOf(c, &root) }); return; }
            for (auto* k : c->getChildren()) rec(k);
        };
        rec(top);
    }
    bool isStationPart(juce::Component* c) const { return dynamic_cast<LearnOverlay*>(c) == nullptr; }
    void timerCallback() override { collect(); repaint(); }
};

} // namespace arco
