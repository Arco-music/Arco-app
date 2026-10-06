#pragma once
// SCORE - the device rack: the selected track's instrument and effects (or the selected clip's own effects) as cards.
#include "Icons.h"

namespace score {

// one effect as a card: on/off, name, a little screen (its sound or its curve), every knob
class FxCard : public juce::Component, public juce::DragAndDropTarget {
public:
    FxCard(std::shared_ptr<Effect> e, Uid ownerTrack, std::vector<std::shared_ptr<Effect>>* chain);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    int preferredWidth(int h) const;
    int knobRows(int h) const;
    void tick();
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("card:"); }
    void itemDropped(const SourceDetails& d) override;
    std::shared_ptr<Effect> fx;
    juce::Colour colour() const;
    static juce::Colour colourOf(const std::string& type);
private:
    void menu();
    Uid track; std::vector<std::shared_ptr<Effect>>* chain;
    juce::OwnedArray<juce::Component> controls;
    std::vector<ParamKnob*> knobs;
    juce::Rectangle<int> screen, onBox, menuBox;
    int toggles = 0, choices = 0;
};

// the instrument of an instrument track, small (the big editor opens in its own window)
class InstCard : public juce::Component {
public:
    InstCard(Track* t);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;
    int preferredWidth(int h) const { return h < 200 ? juce::jmax(250, knobs.size() * 56 + 16) : 250; }
    void tick() { repaint(); }
    std::function<void()> onOpen;
private:
    Track* track;
    juce::OwnedArray<ParamKnob> knobs;
    Pill open { "OPEN" };
};

class Rack : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    Rack();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void update();                        // the selection changed
    bool clipMode = false;                // show the selected clip's own effects
    Uid showTrack = 0;                    // 0 = the selected track (the mixer can show a return or the master)
    std::function<void(Uid)> onOpenInstrument;
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("fx:"); }
    void itemDropped(const SourceDetails& d) override;
    void addMenu(juce::Component* near);
private:
    void timerCallback() override;
    Track* target() const;
    std::vector<std::shared_ptr<Effect>>* chain();
    juce::Viewport view; juce::Component content;
    juce::OwnedArray<juce::Component> cards;
    Pill trackTab { "TRACK FX" }, clipTab { "CLIP FX" }, addBtn { "+ EFFECT" }, storeBtn { "STORE" }, autoBtn { "AUTO" };
    Uid seenTrack = 0, seenClip = 0; int seenStructure = -1; bool seenClipMode = false; size_t seenCount = 0;
};

} // namespace score
