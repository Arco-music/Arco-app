#pragma once
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;

// SETUP: the sound card, live inputs, headphones, speakers, recording timing, practice, tuner.
class SetupPanel : public juce::Component {
public:
    explicit SetupPanel(Canvas& c);
    ~SetupPanel() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
private:
    Canvas& cv;
    juce::Rectangle<int> box { 150, 70, 1140, 740 };
    juce::OwnedArray<juce::Component> kids;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> devSel;
    std::unique_ptr<juce::LookAndFeel_V4> laf;
    juce::ComboBox phones;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    void styleLaf();
};

} // namespace arco
