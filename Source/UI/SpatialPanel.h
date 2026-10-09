#pragma once
// ARCO 1.3: SPEAKERS & SPACE - where the speakers are, and where each sound is (still or moving): AMBISONICS.
// Left: the room from above - drag the speakers (custom layouts) and the sounds (direction, distance).
// Right: layouts (stereo, quad, 5.0, 7.0, rings, dome, cube, 5.0.4), the chosen speaker, the sounds and their motion.
#include <JuceHeader.h>
#include "Widgets.h"

namespace arco {
class Canvas;

class SpatialPanel : public juce::Component, private juce::Timer {
public:
    explicit SpatialPanel(Canvas& c);
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
};

} // namespace arco
