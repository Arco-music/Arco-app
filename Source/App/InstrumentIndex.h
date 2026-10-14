#pragma once
// ARCO 1.5: the search for free instruments. A built-in index of 600+ free sampled instruments (SFZ) - the
// sfzinstruments.github.io collection (Karoryfer Samples, Versilian, DrumGizmo, Salamander, Splendid ...) and the
// Sonatina Symphonic Orchestra, one entry per instrument and articulation - with the exact files each one needs.
// DOWNLOAD fetches only those files, straight from where the library is published (GitHub), inside ARCO:
// no web page, no account. (tools/instrument_index/ makes the index.)
#include <JuceHeader.h>

namespace arco {

const unsigned char* instrumentIndexData(size_t& n);
size_t instrumentIndexRawSize();

struct InstrumentIndex {
    struct Lib { juce::String repo, branch, name, author, licence, category, site, about; juce::StringArray files; };
    struct Entry { int lib = 0; juce::String name, art, category, tags; int sizeMB = 0, samples = 0; std::vector<int> files; juce::String lower; };
    std::vector<Lib> libs;
    std::vector<Entry> entries;
    static const InstrumentIndex& get();
    // best first; an empty query gives the recommended ones
    std::vector<int> search(const juce::String& query, int maxResults = 300) const;
    std::vector<int> recommended() const;
    juce::String title(int e) const;                  // "Solo Cello · Sustain"
    juce::String source(int e) const;                 // "Sonatina Symphonic Orchestra · github.com/peastman/sso"
    juce::File folder(int e) const;                   // where its files go
    juce::File sfz(int e) const;                      // the program it plays
    bool installed(int e) const;                      // every file is here
    juce::String url(int e, int file) const;          // where a file comes from
    juce::String key(int e) const;                    // stable id (library + program)
    int find(const juce::String& key) const;          // -1 if none
};

} // namespace arco
