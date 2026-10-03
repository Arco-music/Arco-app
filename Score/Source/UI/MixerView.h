#pragma once
// SCORE - the mixer: a channel for every track, the two returns (reverb / delay ready) and the master.
#include "Icons.h"

namespace score {

class Strip : public juce::Component {
public:
    Strip(Track* t);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void tick();
    Track* track;
private:
    std::unique_ptr<Fader> fader; std::unique_ptr<Meter> meterL, meterR;
    std::unique_ptr<ParamKnob> pan, sa, sb;
    Pill mute { "M" }, solo { "S" }, arm { "R" };
    juce::Rectangle<int> fxArea;
};

class MixerView : public juce::Component, private juce::Timer {
public:
    MixerView();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();
private:
    void timerCallback() override;
    juce::Viewport view; juce::Component content;
    juce::OwnedArray<Strip> strips, fixed;
    int seenStructure = -1;
};

} // namespace score
