#pragma once
// SCORE - the clip editors: the piano roll (notes, drums) and the audio editor (a recording or an imported sound).
#include "Icons.h"

namespace score {

// scales for the piano roll (highlight, snap the drawn notes, chords that stay in the key)
struct Scales {
    static const std::vector<std::pair<juce::String, std::vector<int>>>& all();
    static bool inScale(int pitch, int key, int scale);
    static int snapToScale(int pitch, int key, int scale);
    static juce::String keyName(int k);
};

class PianoRoll : public juce::Component, private LongPress {
public:
    PianoRoll();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMagnify(const juce::MouseEvent& e, float s) override;
    bool keyPressed(const juce::KeyPress& k) override;
    void setClip(Uid id);
    void zoomToFit();
    Uid clipId = 0;
    // view
    double ppb = 60; double scrollBeat = 0; float rowH = 12; float scrollPitch = 84;     // the top row's pitch
    double grid = 0.25; bool triplet = false; bool drawMode = true; int chord = 0; bool scaleSnap = false; bool fold = false;
    double noteLen = 0.25;               // what a click draws (the last length you made)
    std::set<int> sel;                   // selected notes (index)
    void selectAll() { if (auto* c = clip()) { sel.clear(); for (int i = 0; i < (int) c->notes.size(); ++i) sel.insert(i); repaint(); } }
    void deleteSelected();
    void op(const juce::String& what);  // toolbar actions on the selection (or all)
    std::function<void()> onChanged;
    static constexpr int keysW = 58, rulerH = 20, velH = 64;
private:
    Clip* clip() const;
    Track* owner() const;
    bool drums() const;
    std::vector<int> rowsShown() const;  // pitches from top to bottom
    int pitchAtY(float y) const;
    float yOfPitch(int p) const;
    float xOf(double b) const { return keysW + (float) ((b - scrollBeat) * ppb); }
    double beatAt(float x) const { return scrollBeat + (x - keysW) / ppb; }
    double snapB(double b, bool off) const { double gg = triplet ? grid * 2.0 / 3.0 : grid; return off || gg <= 0 ? b : std::round(b / gg) * gg; }
    juce::Rectangle<float> noteRect(const Note& n) const;
    int hitNote(juce::Point<float> p, int& part) const;   // part 0 body 1 right edge 2 left edge
    juce::Rectangle<int> velArea() const { return { keysW, getHeight() - velH, getWidth() - keysW, velH }; }
    enum Mode { None, Move, ResizeR, ResizeL, Band, Vel, Keys, LoopEnd } mode = None;
    std::vector<Note> orig; juce::Point<float> down; double downBeat = 0; int downPitch = 0; int hit = -1; bool changed = false;
    juce::Rectangle<float> band; int lastKey = -1;
    std::vector<int> foldRows;
};

class AudioEditor : public juce::Component {
public:
    AudioEditor();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void setClip(Uid id) { clipId = id; repaint(); }
    Uid clipId = 0;
private:
    Clip* clip() const;
    juce::Rectangle<int> wave() const { return getLocalBounds().reduced(14, 0).withTrimmedTop(34).withTrimmedBottom(10); }
    float xOfSec(double s) const;        // seconds in the source -> x
    double secAt(float x) const;
    int mode = 0; double o0 = 0, l0 = 0, f0 = 0; float g0 = 0; bool changed = false;
};

class ClipEditor : public juce::Component, private juce::Timer {
public:
    ClipEditor();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void update();
    PianoRoll roll; AudioEditor audio;
    std::function<void()> onClipFx;      // show the clip's own effects
private:
    void timerCallback() override;
    Uid seen = 0; int seenStructure = -1;
    juce::OwnedArray<juce::Component> tools;
    void buildTools();
    bool midi = false;
};

} // namespace score
