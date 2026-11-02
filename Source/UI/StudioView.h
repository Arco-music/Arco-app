#pragma once
// ARCO 1.3: STUDIO - the arrangement window. Clean, but complete:
//   top: transport (play, record with count-in, loop, metronome), position, tempo, grid, zoom, ARRANGE / MIX, export, save
//   left: THE STATION (its scenes and loops - drag them onto the timeline), new lanes, files, recording the station
//   middle: the lanes (audio, instrument, returns) with their clips and automation
//   bottom: the details of what is chosen - a clip (or the piano roll of a MIDI clip), a lane's devices, the master
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"

namespace arco {
class Canvas;

class StudioView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer, private juce::Thread {
public:
    StudioView(Canvas& c);
    ~StudioView() override;
    void open();                                 // from the station (opens the song's arrangement or lays out the scenes)
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    bool keyPressed(const juce::KeyPress& k) override;
    void visibilityChanged() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    std::function<void()> onClose;
    // 2.0: inside the app shell - its top bar has the transport, the position and the tempo; here: the tools
    bool embedded = false;
    void setEmbedded(bool e) { embedded = e; topH = e ? 52.0f : 60.0f; resized(); repaint(); }
    void togglePlay();
    void toggleRecord();
    void rewind() { studio.pos = studio.song.loopOn ? studio.song.loopStart : 0; scrollSec = 0; repaint(); }
    Studio& studio;
private:
    Canvas& cv;
    void timerCallback() override;
    void run() override;                         // export / print effects / transpose
    // ---- layout
    bool showBrowser = true, showDetail = true, mixView = false;
    int displayX = 300;
    float topH = 60, rulerH = 34, headW = 232, browserW = 210, detailH = 236, laneGap = 4, autoH = 52;
    juce::Rectangle<int> browserArea() const;
    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> headArea() const;
    juce::Rectangle<int> clipArea() const;      // the timeline part of the lanes
    juce::Rectangle<int> detailArea() const;
    double pxPerSec = 40.0, scrollSec = 0; float scrollY = 0;
    double secAt(float x) const { return scrollSec + (x - clipArea().getX()) / pxPerSec; }
    float xAt(double sec) const { return (float) (clipArea().getX() + (sec - scrollSec) * pxPerSec); }
    long sampleAt(float x) const { return (long) std::max(0.0, secAt(x) * studio.song.sr); }
    float laneTop(int li) const;                 // y of a lane (with automation lanes)
    float laneHeight(int li) const;              // its clip part
    int laneAt(float y, bool* inAuto = nullptr) const;
    int grid = 2;                                // 0 off, 1 bar, 2 beat, 3 1/8, 4 1/16
    long snapTo(long samples, bool fine) const;
    // ---- what is chosen
    int selLane = 0, selClip = -1;
    std::vector<int> selClips;                   // more clips (shift-click)
    enum Detail { DClip, DLane, DMaster } detail = DLane;
    // ---- dragging
    enum Drag { None, Move, TrimL, TrimR, FadeIn, FadeOut, Seek, LoopMake, LoopMove, Vol, AutoPt, NoteMove, NoteLen, NoteVel, Browser, ScrollV, Split, Resize };
    Drag drag = None; juce::Point<float> downAt;
    long dragStart = 0, dragLen = 0, dragOff = 0; float dragFade = 0; int dragLane = 0, dragPt = -1, dragNote = -1; double dragNoteStart = 0, dragNoteLen = 0; int dragPitch = 0; float dragVel = 0;
    std::vector<std::pair<int, long>> dragGroup;  // other chosen clips: index, start
    struct BrowserItem { int scene = 0, track = -1; juce::String text; juce::Rectangle<int> r; int action = 0; };
    std::vector<BrowserItem> browserItems;
    int dragItem = -1; juce::Point<float> dragNow;
    // ---- peaks (per source)
    std::map<const StudioSource*, std::vector<float>> peaks;
    const std::vector<float>& peaksOf(const StudioSource* s);
    // ---- children
    juce::OwnedArray<juce::Component> kids;      // top bar
    juce::OwnedArray<juce::Component> detailKids;   // the detail panel (rebuilt when the choice changes)
    void buildTop();
    void rebuildDetail();
    int builtFor = -2, builtKind = -1, builtDevices = -1;
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); return c; }
    template <typename T, typename... A> T* addD(juce::Rectangle<int> r, A&&... a) { auto* c = new T(std::forward<A>(a)...); detailKids.add(c); addAndMakeVisible(c); c->setBounds(r); return c; }
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> detailLabels;
    // ---- painting
    void paintTop(juce::Graphics& g);
    void paintBrowser(juce::Graphics& g);
    void paintRuler(juce::Graphics& g);
    void paintLanes(juce::Graphics& g);
    void paintClip(juce::Graphics& g, const StudioClip& c, int ci, juce::Rectangle<float> r);
    void paintAuto(juce::Graphics& g, int li, juce::Rectangle<float> r);
    void paintDetail(juce::Graphics& g);
    void paintPianoRoll(juce::Graphics& g, juce::Rectangle<int> r);
    void paintMixer(juce::Graphics& g);
    // ---- piano roll
    juce::Rectangle<int> rollArea() const;
    int rollTopPitch = 84; float rollRowH = 9; double rollPx = 0;   // px per sample in the roll (0 = fit the clip)
    double rollX(double t, juce::Rectangle<int> r) const;
    double rollT(float x, juce::Rectangle<int> r) const;
    // ---- actions
    void split();
    void duplicate();
    void removeClip();
    void clipMenu(int ci);
    void laneMenu(int li);
    void stationAction(int a);
    void inputMenu(int li);
    void autoMenu(int li);
    void addDeviceMenu(int li);
    void doExport();
    void rename(int li);
    void newMidiClip(int li, long at);
    // ---- jobs on the thread
    enum Job { JNone, JExport, JStems, JPrint, JTranspose } job = JNone;
    int jobLane = 0, jobClip = -1; double jobSemis = 0; std::atomic<float> progress { 0 }; std::atomic<bool> jobDone { false };
    juce::File exportFile; ExportOptions exportOpt; juce::String report;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String hint;                           // a line under the transport: what you can do here
    // controls drawn by paint (lane headers, the mixer, the browser): what they do
    struct Hit { juce::Rectangle<float> r; std::function<void()> click; std::function<void(juce::Point<float> delta, const juce::MouseEvent& e)> drag; std::function<void()> dbl; };
    std::vector<Hit> hits;
    Hit activeHit; bool hitActive = false;
    struct DynLabel { juce::Rectangle<int> r; std::function<juce::String()> text; bool title = false; };
    std::vector<DynLabel> dynLabels, persistentLabels;
    struct DevBox { juce::Rectangle<int> r; int dev; bool master; };
    std::vector<DevBox> devBoxes;
};

} // namespace arco
