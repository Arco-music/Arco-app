#pragma once
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;
struct ArcoLaf;
class Model;
int numFactorySynthPresets();
void applyFactorySynthPreset(Model& m, int index);

// SYNTH: the second screen - two wavetable oscillators, sub, noise, filter, two envelopes, LFO, voice;
// played by the on-screen keyboard, the computer keys or a MIDI keyboard; recorded on any track.
class SynthPanel : public juce::Component, private juce::Timer {
public:
    explicit SynthPanel(Canvas& c);
    ~SynthPanel() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool keyStateChanged(bool isDown) override;   // the computer keys: a note stops when its key is let go
    void loadPreset(int index);
    static juce::StringArray presetNames();
    int presetIndex = 0;
private:
    void timerCallback() override;
    Canvas& cv;
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::OwnedArray<juce::Component> kids;
    std::unique_ptr<juce::MidiKeyboardComponent> kb;
    std::vector<float> scope;
    int octave = 4;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    void knob(int x, int y, const char* id, const char* label, ColourFn col);
    struct Lab { juce::String t; juce::Rectangle<int> r; };
    std::vector<Lab> labels;
    void savePreset();
    juce::var userPresets();
    juce::File presetFile() const;
};

} // namespace arco
