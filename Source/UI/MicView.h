#pragma once
// MIC FOR MAC: this iPhone / iPad becomes a high-quality microphone for ARCO on the Mac (cable or Wi-Fi).
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"

namespace arco {

class MicView : public juce::Component, private juce::Timer {
public:
    explicit MicView(AudioHost& h) : host(h) {
        addAndMakeVisible(go); addAndMakeVisible(back);
        go.onClick = [this] { setSending(!host.micMode.load()); };
        go.dynText = [this] { return host.micMode.load() ? juce::String("STOP") : juce::String("START"); };
        back.onClick = [this] { setSending(false); if (onBack) onBack(); };
        startTimerHz(20);
    }
    ~MicView() override { setSending(false); }
    std::function<void()> onBack;
    juce::String backText = "BACK TO THE STATION";

    void setSending(bool on) {
        if (on == host.micMode.load()) return;
        if (on) { host.synth.allOff(); host.micOut.start(); host.micMode.store(true); }
        else { host.micMode.store(false); host.micOut.stop(); }
        juce::Desktop::getInstance().setScreenSaverEnabled(!on);     // keep the screen awake while sending
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bg());
        auto r = getLocalBounds().reduced(juce::jmax(20, getWidth() / 14), 24);
        auto line = [&](const juce::String& t, int h, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centred) {
            g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r.removeFromTop(h), j, 3, 0.8f); };
        float k = juce::jlimit(0.8f, 1.6f, getWidth() / 500.0f);
        line("ARCO", (int) (34 * k), 22 * k, Theme::txt(), 2);
        line("MIC FOR MAC", (int) (24 * k), 12 * k, Theme::violet(), 2);
        r.removeFromTop((int) (10 * k));
        bool on = host.micMode.load(), conn = host.micOut.connected();
        juce::String st = !on ? U("press START, then connect from ARCO on the Mac")
                              : (conn ? U("✓ sending to the Mac") : U("waiting for the Mac …"));
        line(st, (int) (26 * k), 10.5f * k, on ? (conn ? Theme::green() : Theme::amber()) : Theme::sub(), 1);
        // level
        auto lv = r.removeFromTop((int) (16 * k)).reduced(getWidth() / 8, 2).toFloat();
        float v = juce::jmin(1.0f, host.micOut.level.load() * 2.0f);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(lv, 5.0f);
        g.setColour(v > 0.9f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(lv.withWidth(lv.getWidth() * v), 5.0f);
        r.removeFromTop((int) (14 * k));
        goArea = r.removeFromTop((int) (110 * k)).withSizeKeepingCentre((int) (110 * k), (int) (110 * k));
        r.removeFromTop((int) (16 * k));
        line(U("WITH THE CABLE  (best sound, smallest delay)"), (int) (18 * k), 9 * k, Theme::txt(), 2);
        line(U("plug this device into the Mac, unlock it, tap Trust  →  on the Mac: SETUP → CABLE (USB)"), (int) (30 * k), 8 * k, Theme::sub());
        line(U("OR OVER WI-FI  (same network)"), (int) (18 * k), 9 * k, Theme::txt(), 2);
        auto ips = MicSender::localAddresses();
        line(ips.isEmpty() ? juce::String("no Wi-Fi") : U("on the Mac: SETUP → WI-FI, type  ") + ips.joinIntoString("  or  "), (int) (30 * k), 8 * k, Theme::sub());
        line(U("The microphone is used raw at full quality - no voice filters. Keep this screen open while you play."), (int) (30 * k), 7.5f * k, Theme::sub());
        backArea = r.removeFromBottom((int) (34 * k)).withSizeKeepingCentre((int) (220 * k), (int) (30 * k));
        go.setBounds(goArea); back.setBounds(backArea); back.text = backText;
        go.fsize = 15 * k; back.fsize = 8 * k; go.radius = goArea.getWidth() * 0.5f;
    }
    void resized() override { repaint(); }
private:
    void timerCallback() override { repaint(); }
    AudioHost& host;
    juce::Rectangle<int> goArea, backArea;
    Pill go { "START", [this] { return host.micMode.load() ? Theme::red() : Theme::green(); }, 15.0f, 55.0f };
    Pill back { "BACK TO THE STATION", [] { return Theme::btn(); }, 8.0f, 7.0f };
};

} // namespace arco
