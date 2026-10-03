#pragma once
// SCORE - ARRANGE: tracks with clips on a bar grid, the loop and the stations on the ruler, automation lanes.
#include "Icons.h"

namespace score {

class ArrangeView;

struct Row { Track* t = nullptr; int y = 0, h = 0, autoH = 0; };

class TrackHeader : public juce::Component, private LongPress {
public:
    TrackHeader(ArrangeView& a, Track* t);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void menu();
    Track* track;
private:
    ArrangeView& av;
    Pill mute { "M" }, solo { "S" }, arm { "R" }, mon { "IN" }, autoBtn { "A" }, inst { "" };
    std::unique_ptr<Meter> meter;
    std::unique_ptr<juce::Component> fader;
    bool resizing = false; float startH = 0; int startY = 0;
    void rename();
};

class Lanes : public juce::Component, private LongPress {
public:
    explicit Lanes(ArrangeView& a) : av(a) { onLong = [this](juce::Point<int> p) { contextMenu(p); }; }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMagnify(const juce::MouseEvent& e, float scale) override;
    void contextMenu(juce::Point<int> p);
private:
    ArrangeView& av;
    enum Mode { None, Move, TrimL, TrimR, FadeIn, FadeOut, Band, AutoPt, Seek } mode = None;
    struct Orig { Uid id; Uid track; double start, length, offset, srcLen, fadeIn, fadeOut; std::vector<Note> notes; };
    std::vector<Orig> orig;
    juce::Point<int> down; double downBeat = 0; Uid hitClip = 0; bool changed = false; bool alt = false;
    juce::Rectangle<int> band;
    Track* autoTrack = nullptr; int autoIdx = -1;
    struct Hit { Clip* c = nullptr; Track* t = nullptr; Mode part = None; };
    Hit hitTest(juce::Point<int> p);
    void drawClip(juce::Graphics& g, Clip& c, Track& t, juce::Rectangle<int> r, bool selected);
    void drawAuto(juce::Graphics& g, const Row& row);
    friend class ArrangeView;
};

class Ruler : public juce::Component, private LongPress {
public:
    explicit Ruler(ArrangeView& a) : av(a) { onLong = [this](juce::Point<int> p) { stationMenu(p); }; }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void stationMenu(juce::Point<int> p);
private:
    ArrangeView& av;
    int mode = 0; double a0 = 0; int dragStation = -1; bool moved = false;
    int stationAt(juce::Point<int> p) const;
};

class ArrangeView : public juce::Component, public juce::FileDragAndDropTarget, public juce::DragAndDropTarget, private juce::Timer {
public:
    ArrangeView();
    ~ArrangeView() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();                                        // the tracks changed
    void updateFromHost();                                 // something changed (selection, meters)
    // geometry
    double ppb = 24.0, scrollBeat = 0.0; int scrollY = 0;
    static constexpr int headerW = 236, rulerH = 44, bottomBar = 30;
    int laneX() const { return headerW; }
    float xOf(double beat) const { return (float) ((beat - scrollBeat) * ppb); }
    double beatAt(float x) const { return scrollBeat + x / ppb; }
    double snap(double beat, bool force = false) const;
    double gridBeats() const;
    int snapMode = 2;                                      // 0 off, 1 bar, 2 beat, 3 1/2, 4 1/4 (16ths), 5 auto
    bool followPlay = true;
    std::vector<Row> rows;
    int contentHeight() const;
    Row* rowAt(int y);                                     // y in lane coordinates (0 = top of the first track, before scroll)
    void zoomAround(double factor, float x);
    void openAutomation(Param* p, Uid track);
    // drop targets
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override { dropAt = { x, y }; repaint(); }
    void fileDragMove(const juce::StringArray&, int x, int y) override { dropAt = { x, y }; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dropAt = { -1, -1 }; repaint(); }
    bool isInterestedInDragSource(const SourceDetails&) override { return true; }
    void itemDropped(const SourceDetails& d) override;
    void itemDragMove(const SourceDetails& d) override { dropAt = d.localPosition.toInt(); repaint(); }
    void itemDragExit(const SourceDetails&) override { dropAt = { -1, -1 }; repaint(); }
    void addTrackMenu();
    std::function<void()> onOpenEditor;                    // double-click on a clip
    std::function<void(Uid)> onOpenInstrument;
    juce::Point<int> dropAt { -1, -1 };
private:
    void timerCallback() override;
    void layoutHeaders();
    std::unique_ptr<juce::ScrollBar::Listener> sbl;
    Ruler ruler { *this };
    Lanes lanes { *this };
    juce::Component headerBox;
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar hbar { false }, vbar { true };
    Pill addTrack { "+ TRACK" }, snapBtn { "" }, zoomIn { "+" }, zoomOut { U("−") }, followBtn { "FOLLOW" }, fitBtn { "FIT" };
    int seenStructure = -1;
    double lastPos = -1;
    friend class Lanes; friend class Ruler; friend class TrackHeader;
};

} // namespace score
