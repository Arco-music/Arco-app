#pragma once
// ARCO 1.3: KEYBOARD (a big keyboard + the instrument it plays: SYNTH 2, MODULAR, sampled instruments to download, plug-ins)
// and MODULAR (ARCO's modular synthesizer: modules, knobs, cables).
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;

class KeyboardPanel : public juce::Component, private juce::Timer {
public:
    explicit KeyboardPanel(Canvas& c);
    ~KeyboardPanel() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    Canvas& cv;
    struct Row { juce::String title, sub, button; juce::Colour bcol; std::function<void()> click; bool active = false; float progress = -1; };
private:
    void timerCallback() override;
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::OwnedArray<juce::Component> kids;
    std::unique_ptr<juce::MidiKeyboardComponent> kb;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    std::vector<Row> sampleRows() const;
    std::vector<Row> pluginRows() const;
    juce::Rectangle<int> sampleArea { 24, 214, 650, 352 }, pluginArea { 694, 214, 642, 352 };
    int pluginScroll = 0, sampleScroll = 0;
    int octave = 3;
    juce::String scanText;
    void addSfz();
    std::unique_ptr<juce::FileChooser> chooser;
};

class ModularPanel : public juce::Component, private juce::Timer {
public:
    explicit ModularPanel(Canvas& c);
    ~ModularPanel() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    void rebuild();                     // the modules changed: lay them out again
    Canvas& cv;
    struct Rack;
private:
    void timerCallback() override;
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::OwnedArray<juce::Component> kids;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    juce::Viewport view;
    std::unique_ptr<Rack> rack;
    int patchIndex = 0;
};

} // namespace arco
