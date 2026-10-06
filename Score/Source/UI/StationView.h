#pragma once
// SCORE - the SCORE LINE: the piece as a line of stations. Perform it (GO / BACK, pedals), or shape each station:
// its sound (every knob), its glide, what happens at its end, and what you record in it.
#include "Icons.h"

namespace score {

class StationLine : public juce::Component {
public:
    StationLine();
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    int shown = 0;                      // the station whose settings are shown
    float scroll = 0;
    std::function<void()> onShow;
    static constexpr int cardW = 150, gap = 26;
    juce::Rectangle<float> cardRect(int i) const;
private:
    int dragFrom = -1, dragTo = -1; bool dragging = false;
};

class StationView : public juce::Component, private juce::Timer {
public:
    StationView();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void update();
    void addStation(bool atPlayhead);
private:
    void timerCallback() override;
    void showStation(int i);
    Station* st();
    StationLine line;
    IconButton goBtn { Icon::Next, "GO: the next station (pedal / space bar in PERFORM)" }, backBtn { Icon::Prev, "BACK" };
    Pill startBtn { "START" }, modeBtn { "" }, storeBtn { "STORE" }, autoBtn { "AUTO STORE" }, followBtn { "FOLLOW THE ARRANGER" };
    Pill addBtn { "+ STATION" }, addHereBtn { "+ AT PLAYHEAD" }, recHere { "RECORD IN THIS STATION" }, playHere { "PLAY FROM HERE" }, loopHere { "LOOP IT" },
         dupBtn { "DUPLICATE" }, delBtn { "DELETE" }, sameBtn { "SOUND LIKE THE ONE BEFORE" };
    juce::TextEditor name, note;
    std::unique_ptr<ValueBox> barBox, glideBox, autoGoBox;
    Pill curveBtn { "" }, endBtn { "" }, jumpBtn { "" };
    juce::Rectangle<int> big, settings, changes, logArea, labRect[8];
    int seenCount = -1, seenCurrent = -2, seenStructure = -1;
    juce::StringArray changeList;
    void computeChanges();
};

} // namespace score
