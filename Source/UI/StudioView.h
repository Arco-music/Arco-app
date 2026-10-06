#pragma once
// STUDIO: the song as a timeline - lanes, clips, a mixer, playback, export (see App/Studio.h)
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"

namespace arco {
class Canvas;

class StudioView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer, private juce::Thread {
public:
    StudioView(Canvas& c);
    ~StudioView() override;
    void open();                                 // from the station (rebuilds when there is nothing yet)
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    std::function<void()> onClose;
    Studio& studio;
private:
    Canvas& cv;
    void timerCallback() override;
    void run() override;                         // export / print effects
    juce::OwnedArray<juce::Component> kids;
    juce::OwnedArray<Knob> laneKnobs, clipKnobs, masterKnobs;
    int selLane = 0, selClip = -1;
    double pxPerSec = 40.0, scrollSec = 0;
    float laneH = 74, headW = 230, topH = 60, rulerH = 26, inspH = 150;
    juce::Rectangle<int> timeline() const;
    double secAt(float x) const { return scrollSec + (x - timeline().getX()) / pxPerSec; }
    float xAt(double sec) const { return (float) (timeline().getX() + (sec - scrollSec) * pxPerSec); }
    int laneAt(float y) const { return (int) std::floor((y - timeline().getY()) / laneH); }
    long snapTo(long samples, bool fine) const;
    // dragging
    enum Drag { None, Move, TrimL, TrimR, FadeIn, FadeOut, Seek, Gain };
    Drag drag = None; long dragStart = 0, dragLen = 0, dragOff = 0; float dragFade = 0, dragGain = 0; int dragLane = 0; juce::Point<float> downAt; bool snapped = false;
    std::vector<std::vector<float>> peaks;       // per source, max of every 256 samples
    void buildPeaks();
    void split();
    void duplicate();
    void removeClip();
    void doExport();
    void layoutInspector();
    // a job on the thread
    enum Job { JNone, JExport, JPrint } job = JNone;
    int jobLane = 0; std::atomic<float> progress { 0 }; std::atomic<bool> jobDone { false };
    juce::File exportFile; ExportOptions exportOpt; juce::String report;
    std::unique_ptr<juce::FileChooser> chooser;
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); return c; }
};

} // namespace arco
