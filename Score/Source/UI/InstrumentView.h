#pragma once
// SCORE - the instrument windows: NOVA (the wavetable synth), SAMPLER and DRUMS (with a step sequencer).
#include "Icons.h"

namespace score {

// a titled panel of controls built from parameter ids (knobs, choices, switches), with an optional little screen
class Panel : public juce::Component {
public:
    Panel(juce::String title, ParamOwner* o, std::vector<std::string> ids, ColourFn col, Uid track, int cols, int displayH = 0, std::string onId = {});
    void paint(juce::Graphics& g) override;
    void resized() override;
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> drawDisplay;
    std::function<void(const juce::MouseEvent&, juce::Rectangle<float>)> displayMouse;
    void mouseDown(const juce::MouseEvent& e) override { if (displayMouse && display.contains(e.position)) displayMouse(e, display); }
    void mouseDrag(const juce::MouseEvent& e) override { if (displayMouse && display.contains(e.mouseDownPosition)) displayMouse(e, display); }
    juce::String title; ColourFn col; int cols, displayH;
    juce::Rectangle<float> display;
    juce::OwnedArray<juce::Component> extra;     // custom things placed by the owner
    juce::Rectangle<int> content() const { return getLocalBounds().reduced(10, 0).withTrimmedTop(30).withTrimmedBottom(8); }
private:
    juce::OwnedArray<juce::Component> small; std::vector<ParamKnob*> knobs; juce::OwnedArray<ParamKnob> knobOwn;
    std::unique_ptr<ParamToggle> onSwitch;
};

// 4 panels behind tabs (ENV 1-4, LFO 1-4)
class TabPanel : public juce::Component {
public:
    TabPanel(juce::String title, std::vector<std::unique_ptr<Panel>> pages, std::vector<juce::String> names, ColourFn col);
    void paint(juce::Graphics& g) override;
    void resized() override;
    int page = 0;
private:
    juce::String title; std::vector<std::unique_ptr<Panel>> pages; juce::OwnedArray<Pill> tabs; ColourFn col;
};

class ModMatrix : public juce::Component {
public:
    ModMatrix(Nova& n, ColourFn col, Uid track);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { first = juce::jlimit(0, Nova::NSLOT - visible(), first - (w.deltaY > 0 ? 1 : -1)); resized(); repaint(); }
private:
    int visible() const { return juce::jmax(1, (getHeight() - 34) / 24); }
    Nova& nova; ColourFn col; Uid track; int first = 0; int dragRow = -1; float startV = 0;
    juce::OwnedArray<ParamChoice> src, dst;
    juce::Rectangle<int> amountRect(int row) const;
};

class InstrumentEditor : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit InstrumentEditor(Track* t);
    ~InstrumentEditor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    std::function<void()> onClose;
    Track* track;
private:
    void timerCallback() override;
    void buildNova(Nova& n); void buildSampler(Sampler& s); void buildDrums(Drums& d);
    void presetMenu(); void stepPreset(int d);
    void loadSampleInto(int pad);                     // -1 = the sampler
    void setSampleFile(const juce::File& f, int pad);
    juce::OwnedArray<juce::Component> parts;
    std::vector<juce::Rectangle<int>> boxes;          // where parts go (filled in resized)
    std::function<void()> layout;
    Pill close { "CLOSE" }, preset { "" }, prev { "<" }, next { ">" }, save { "SAVE" }, load { "LOAD SOUND" };
    std::unique_ptr<juce::MidiKeyboardComponent> keys;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String presetName;
    int selPad = 0;
public:
    int selectedPad() const { return selPad; }
    void selectPad(int p);
};

} // namespace score
