#pragma once
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;
struct ArcoLaf;
class Model;
int numFactorySynthPresets();
void applyFactorySynthPreset(Model& m, int index);

// SYNTH 2: three pages - SOUND (3 oscillators, sub, noise, filter, voice), MOVEMENT (3 envelopes, 3 LFOs, the modulation
// list, 4 macros) and PLAY (the arpeggiator). Drag a source (the dots on LFOs, envelopes, macros) onto any knob to move it.
// Below: four ways to play - piano keys, the slide pad, chord pads, a scale grid - plus the computer keys and MIDI.
class SynthPanel : public juce::Component, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit SynthPanel(Canvas& c);
    ~SynthPanel() override;
    void paint(juce::Graphics& g) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool keyStateChanged(bool isDown) override;
    void loadPreset(int index);
    static juce::StringArray presetNames();
    int presetIndex = 0;
    int page = 0;                       // 0 SOUND, 1 MOVEMENT, 2 PLAY
    int keysMode = 0;                   // 0 piano, 1 slide pad, 2 chords, 3 scale grid
    void setPage(int p);
    void addMod(int source, const std::string& knobId);     // a new slot: source -> the knob's destination
    Canvas& cv;
    int octave = 5;
private:
    void timerCallback() override;
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::OwnedArray<juce::Component> kids;
    std::vector<juce::Component*> pages[3];
    std::unique_ptr<juce::MidiKeyboardComponent> kb;
    std::unique_ptr<juce::Component> pad;
    template <typename T, typename... A> T* add(int pg, juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY()));
        if (pg >= 0) pages[pg].push_back(c); return c; }
    void knob(int pg, int x, int y, const std::string& id, const juce::String& label, ColourFn col, int size = 40);
    void chip(int pg, int x, int y, int source, juce::Colour col);
    struct Lab { juce::String t; juce::Rectangle<int> r; int page; };
    std::vector<Lab> labels;
    void savePreset();
    juce::var userPresets();
    juce::File presetFile() const;
};

} // namespace arco
