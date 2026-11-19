#pragma once
// ARCO 2.6: SESSIONS - the library of your sessions, saved inside ARCO (in Music / ARCO Songs).
// Open one, rename it, duplicate it, put it in the bin, find it by its name. "Save session" puts the song here.
#include "Shell.h"
#include "Sessions.h"

namespace arco {

class LibraryView : public juce::Component, private juce::Timer {
public:
    explicit LibraryView(AppShell& s);
    ~LibraryView() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void refresh();
    AppShell& shell;
    struct Grid;
private:
    void timerCallback() override;
    std::unique_ptr<Grid> grid; juce::Viewport vp;
    juce::TextEditor search;
    juce::OwnedArray<juce::Component> owned;
    juce::Rectangle<int> headR, searchR;
    juce::int64 lastScan = 0;
};

} // namespace arco
