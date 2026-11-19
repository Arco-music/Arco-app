#pragma once
// ARCO 2.6: the sessions saved inside ARCO (the SESSIONS library) - where they are, their names, what is in them.
#include <JuceHeader.h>
#include <vector>

namespace arco {

namespace sessions {
    juce::File dir();                                            // where they live
    juce::String suggestName(double bpm);                        // "ARC-007 · 92 BPM"
    juce::File uniqueFolder(const juce::String& name);           // dir()/name, or "name 2" when it is taken
    bool isSession(const juce::File& folder);
    bool rename(const juce::File& folder, const juce::String& newName, juce::File& result);
    bool duplicate(const juce::File& folder, juce::File& result);
    struct Info { juce::File folder; juce::String name; juce::Time saved, created; double bpm = 0; int beats = 4, den = 4, loops = 0, clips = 0, tracks = 0; };
    std::vector<Info> list();                                    // newest first
}


} // namespace arco
