#pragma once
// SCORE - the browser: your sound files (Downloads, Music, any folder), the synth sounds, the effects and your pieces.
// Click listens, drag puts it where you want, double-click puts it on the selected track at the playhead.
#include "Icons.h"

namespace score {

struct BItem { juce::String text, sub; int kind = 0; juce::String payload; int group = 0; };   // kind: 0 folder 1 sound 2 preset 3 effect 4 piece 5 header 6 place 7 instrument

class BrowserList : public juce::Component {
public:
    std::vector<BItem> items;
    std::function<void(const BItem&)> onClick, onDouble;
    std::function<juce::String(const BItem&)> dragDesc;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override { int h = indexAt(e.y); if (h != hover) { hover = h; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
    int rowH(const BItem& it) const { return it.kind == 5 ? 24 : (it.sub.isNotEmpty() ? 34 : 26); }
    int total() const { int h = 0; for (auto& i : items) h += rowH(i); return h + 10; }
    int indexAt(int y) const { int h = 0; for (int i = 0; i < (int) items.size(); ++i) { int r = rowH(items[(size_t) i]); if (y >= h && y < h + r) return i; h += r; } return -1; }
    int selected = -1, hover = -1;
    ColourFn accent;
};

class Browser : public juce::Component, private juce::Timer {
public:
    Browser();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void show(int tab);
    void openFolder(const juce::File& f);
    void importFiles();                     // the system's file picker (on iPad: the Files app, iCloud, USB sticks)
    int tab = 0;
private:
    void timerCallback() override;
    void fill();
    juce::File folder; juce::Array<juce::File> places;
    std::vector<juce::File> extraFolders;
    void loadFolders(); void saveFolders();
    Pill tSounds { "SOUNDS" }, tInst { "SYNTHS" }, tFx { "EFFECTS" }, tPieces { "PIECES" }, importBtn { "IMPORT FILES" }, upBtn { U("‹") }, addFolder { "+ FOLDER" };
    juce::TextEditor search;
    juce::Viewport view; BrowserList list;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastSearch;
};

} // namespace score
