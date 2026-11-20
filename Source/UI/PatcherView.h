#pragma once
// ARCO 2.4: PATCHER - its own screen. Write programs, effects and instruments by connecting boxes (like Max / MSP):
// a canvas, a search that understands what you want, help for every box, a guide, ready-made patches, a console.
#include "Shell.h"
#include "../Engine/Patcher.h"
#include "../Engine/PatcherExamples.h"

namespace arco {

class PatcherView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit PatcherView(AppShell& s);
    ~PatcherView() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    AppShell& shell;

    // what is open: your patch (saved with the song), or the program of a STUDIO device
    void editMain();
    void editDevice(std::shared_ptr<px::Runtime> rt, const juce::String& json, const juce::String& title, std::function<void(const juce::String&)> writeBack);
    void openDoc(const px::PatchDoc& d, bool asNew);      // replace the patch
    void openExample(int i);
    px::PatchDoc& root();
    px::Runtime* rt();
    px::PatchDoc& cur();                                   // the patch on screen (a subpatch when one is open)
    std::vector<int> path;                                 // the subpatches opened, from the top
    std::string keyOf(int id) const;                       // the runtime's name for a box on screen
    void changed(bool structural = true);                  // the patch was edited: compile, keep for undo
    void snapshot();
    void undo(); void redo();
    bool editing = true;
    void setEditing(bool e);
    std::vector<int> sel;                                  // chosen boxes
    int selLine = -1;
    juce::String helpFor;                                  // the class shown in the help
    int rightTab = 0;                                      // 0 help, 1 guide, 2 patches
    int lessonOpen = -1;
    juce::File file;                                       // where the patch was saved (if it was)
    void demo(const juce::String& cmd);                    // (screenshots)
    struct Canvas; struct Side; struct Right; struct Console;
    std::unique_ptr<Canvas> canvas; std::unique_ptr<Side> side; std::unique_ptr<Right> right; std::unique_ptr<Console> console;
    juce::Viewport vp;
    juce::String devTitle;
    bool deviceMode() const { return devRt != nullptr; }
    void addFromSearch(const std::string& cls, juce::Point<float> at = { -1, -1 });
    juce::Point<float> visibleCentre() const;
    void saveFile(bool as); void openMenu(); void loadFile(const juce::File& f);
    void readBuffer(const juce::String& name, const juce::File& f);
    static juce::File library();
private:
    void timerCallback() override;
    std::shared_ptr<px::Runtime> devRt; px::PatchDoc devDoc; std::function<void(const juce::String&)> devWrite;
    std::deque<px::Line> devLog;
    std::vector<juce::String> undoStack, redoStack;
    juce::OwnedArray<juce::Component> owned;
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); return c; }
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> barR;
    int lastLogGen = -1;
    friend struct Canvas; friend struct Console;
public:
    std::deque<px::Line>& log();
};

} // namespace arco
