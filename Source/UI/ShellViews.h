#pragma once
// ARCO 2.0: the new screens of the shell - DRUM MACHINE, PHRASE, LIVE LOOPS.
#include "Shell.h"

namespace arco {

// ---------------------------------------------------------------- DRUM MACHINE
class DrumView : public juce::Component, private juce::Timer {
public:
    explicit DrumView(AppShell& s);
    ~DrumView() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool keyStateChanged(bool) override { return false; }
    void visibilityChanged() override;
    void undo();
    AppShell& shell;
    int selPad = 0, page = 0, mode = 0;           // mode: 0 steps 1 velocity 2 chance 3 repeats 4 nudge
    static juce::Colour padColour(int pad);
private:
    void timerCallback() override;
    DrumMachine& dm();
    int pat();
    juce::Rectangle<int> leftR, gridR, rightR, padsR, padNameR, kitLabelR;
    juce::Rectangle<int> cellRect(int pad, int step) const;
    juce::Rectangle<int> rowHead(int pad) const;
    bool cellAt(juce::Point<int> p, int& pad, int& step) const;
    juce::Rectangle<int> padRect(int pad) const;
    juce::OwnedArray<juce::Component> owned;
    std::vector<juce::Component*> padKnobs;
    Segments* lenSeg = nullptr; Segments* pageSeg = nullptr;
    std::vector<Pill*> patBtns, kitBtns;
    // a drag across the grid
    int dragPad = -1, dragStep = -1, paintVel = -1; float dragY0 = 0; int dragV0 = 0;
    std::vector<std::vector<uint32_t>> undoStack;
    void snapshot();
    int lastHit[12] {}; double flashUntil[12] {};
    int lastPlayStep = -2;
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); return c; }
};

// ---------------------------------------------------------------- PHRASE
class PhraseView : public juce::Component, private juce::Timer {
public:
    explicit PhraseView(AppShell& s);
    void paint(juce::Graphics& g) override;
    void resized() override;
    AppShell& shell;
private:
    void timerCallback() override { if (isVisible()) { repaint(); for (auto* c : owned) c->repaint(); } }
    juce::OwnedArray<juce::Component> owned;
    juce::Rectangle<int> waveR, buildR;
    Pill *rec = nullptr, *play = nullptr, *clear = nullptr, *build = nullptr;
    std::vector<KnobCell*> knobs;
    Segments *src = nullptr, *len = nullptr, *rate = nullptr, *order = nullptr;
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); return c; }
};

// ---------------------------------------------------------------- LIVE LOOPS
class LoopsView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit LoopsView(AppShell& s);
    ~LoopsView() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override { hover = { x, y }; repaint(); }
    void fileDragExit(const juce::StringArray&) override { hover = { -1, -1 }; repaint(); }
    AppShell& shell;
    void togglePlay();
    void stopAll();
    void recordSelected();
    bool anyPlaying();
    bool anyRecording();
    int selRow = 0, selCol = 0;
    // the clips (the engine reads them; replaced ones wait a moment before they go)
    std::shared_ptr<LoopClip> clips[LoopGrid::ROWS][LoopGrid::COLS];
    void setClip(int row, int col, std::shared_ptr<LoopClip> c);
    juce::String names[LoopGrid::ROWS][LoopGrid::COLS];
    bool importInto(const juce::File& f, int row, int col);
    bool takeStationLoop(int scene, int track, int row, int col);
    // saving with the song
    bool save(const juce::File& folder);
    bool load(const juce::File& folder);
private:
    void timerCallback() override;
    LoopGrid& lg();
    juce::Rectangle<int> gridR, headR, browR;
    juce::Rectangle<int> cellRect(int row, int col) const;
    juce::Rectangle<int> rowRect(int row) const;
    juce::Rectangle<int> colHead(int col) const;
    bool cellAt(juce::Point<int> p, int& row, int& col) const;
    juce::OwnedArray<juce::Component> owned;
    std::vector<std::pair<double, std::shared_ptr<LoopClip>>> trash;
    std::map<const LoopClip*, std::vector<float>> peaks;
    const std::vector<float>& peaksOf(const LoopClip* c);
    juce::Point<int> hover { -1, -1 };
    bool showBrowser = true, wasTall = false;
    struct BItem { int scene, track; juce::Rectangle<int> r; };
    std::vector<BItem> bitems;
    int dragItem = -1; juce::Point<int> dragNow;
    int dragFromRow = -1, dragFromCol = -1;
    juce::String message;
    juce::ThreadPool pool { 1 };
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); return c; }
};

} // namespace arco
