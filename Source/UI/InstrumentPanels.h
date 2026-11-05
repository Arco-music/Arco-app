#pragma once
// ARCO 1.3: KEYBOARD (a big keyboard + the instrument it plays: SYNTH 2, MODULAR, sampled instruments to download, plug-ins)
// and MODULAR (ARCO's modular synthesizer: modules, knobs, cables).
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;

class KeyPad;
class KeyboardPanel : public juce::Component, private juce::Timer {
public:
    explicit KeyboardPanel(Canvas& c);
    ~KeyboardPanel() override;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    Canvas& cv;
    struct Row { juce::String title, sub, button; juce::Colour bcol; std::function<void()> click; bool active = false, header = false; float progress = -1; };
    void setSearch(const juce::String& q);           // the instrument search (SAMPLED)
private:
    void timerCallback() override;
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::OwnedArray<juce::Component> kids;
    std::unique_ptr<juce::MidiKeyboardComponent> kb;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    std::vector<Row> sampleRows() const;
    std::vector<Row> pluginRows() const;
    int pluginScroll = 0, sampleScroll = 0;
    int octave = 3;
    int page = 0, playMode = 0, modPatch = 0;
    std::vector<juce::Component*> pages[4];
    void showPage(int p);
    void setPlayMode(int m);
    std::unique_ptr<KeyPad> surface;
    std::unique_ptr<juce::Component> held;
    juce::String scanText;
    void addSfz();
    std::unique_ptr<juce::FileChooser> chooser;
    // 1.5: the search for instruments
    std::unique_ptr<juce::TextEditor> search;
    int chip = -1;
    std::vector<int> results;
    std::vector<char> here;                           // installed, per index entry (checked when the list changes)
    void refreshResults(bool keepScroll = false);
    int lastDownloading = -1;
    // other platforms (their own free account and player): GET IT opens their page; back in ARCO the plug-in is found by itself
    juce::String platformPending; int pluginsBefore = 0; bool wasForeground = true;
    void platformRows(std::vector<Row>& rows) const;
    juce::Rectangle<int> listArea() const;
    bool dragging = false; int dragY0 = 0, dragScroll0 = 0;
};

// 2.3: MODULAR - two ways into the same patch: the RACK (modules screwed into rails, cables) and the PATCH LAB (objects on a
// free canvas, typed by name, like Max - with guides and ready-made patches). A library of ~60 modules on shelves.
class ModularPanel : public juce::Component, private juce::Timer {
public:
    explicit ModularPanel(Canvas& c);
    ~ModularPanel() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    void rebuild();                     // the modules changed: lay them out again
    Canvas& cv;
    struct Rack; struct Lab; struct Library; struct Guide;
    int view = 0;                       // 0 the rack, 1 the patch lab
    void showView(int v); void showGuide(bool on); void demoState(const juce::String& cmd);   // (screenshots)
    void addModule(int type);           // from the library: into the rack, or the lab's middle
    void loadExample(int which, bool insert);
    juce::String message;
private:
    void timerCallback() override;
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::OwnedArray<juce::Component> kids;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    juce::Viewport rackView, labView;
    std::unique_ptr<Rack> rack;
    std::unique_ptr<Lab> lab;
    std::unique_ptr<Library> library;
    std::unique_ptr<Guide> guide;
    int patchIndex = 0; juce::String lastShape;
    std::unique_ptr<juce::MidiKeyboardComponent> kb;     // 1.4: play it right here
    std::unique_ptr<KeyPad> surface;
    std::unique_ptr<juce::Component> held;
    int playMode = 0, octave = 3;
public:
    bool dimCables = false;
};

} // namespace arco
