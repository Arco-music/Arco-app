#pragma once
// ARCO 1.3: SPEAKERS & SPACE - where the speakers are, and where each sound is (still or moving): AMBISONICS.
// Left: the room from above - drag the speakers (custom layouts) and the sounds (direction, distance).
// Right: layouts (stereo, quad, 5.0, 7.0, rings, dome, cube, 5.0.4), the chosen speaker, the sounds and their motion.
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;

// 1.4: OUTPUTS - every sound (LIVE 1-4, the tracks, the drums, HALO; the SYNTH plays through its LIVE strip) to one or more
// outputs of the sound card, e.g. one channel each on the room's mixing desk. A grid: a row per sound, a column per output.
class RoutingView : public juce::Component, private juce::Timer {
public:
    explicit RoutingView(Canvas& c);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
private:
    Canvas& cv;
    void timerCallback() override { if (isShowing()) repaint(); }
    int numOuts() const;
    juce::Rectangle<float> cell(int src, int out) const;
    juce::Rectangle<float> roomCell(int src) const;
    bool hit(juce::Point<float> p, int& src, int& out) const;     // out -1 = ROOM
    int paintValue = -1;                                          // a drag paints on or off
    juce::OwnedArray<juce::Component> kids;
    juce::String sourceName(int s) const;
    void oneEach(); void clearAll();
};

class SpatialPanel : public juce::Component, private juce::Timer {
public:
    explicit SpatialPanel(Canvas& c);
    void showTab(int t);                                           // 0 OUTPUTS, 1 SPEAKERS & SPACE
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void visibilityChanged() override;
private:
    Canvas& cv;
    void timerCallback() override { if (isShowing()) repaint(); }
    juce::Rectangle<int> box { 40, 28, 1360, 826 };
    juce::Point<float> centre { 390, 455 }; float radius = 325;
    float roomM() const;                                    // metres to the edge of the circle
    juce::Point<float> at(float az, float dist) const;
    void fromPoint(juce::Point<float> p, float& az, float& dist) const;
    juce::OwnedArray<juce::Component> kids;
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) { auto* c = new T(std::forward<A>(a)...); kids.add(c); addAndMakeVisible(c); c->setBounds(r.translated(box.getX(), box.getY())); return c; }
    int selSpk = 0, selSrc = 4;
    int dragSpk = -1, dragSrc = -1;
    void preset(int which);
    std::vector<std::pair<juce::Rectangle<int>, int>> srcRows;
    int tab = 1;
    std::unique_ptr<RoutingView> routing;
    std::vector<juce::Component*> tabPills;
    juce::OwnedArray<juce::Component> kidsTabs;
};

} // namespace arco
