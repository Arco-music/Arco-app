#pragma once
// ARCO 2.5: FLUXO - its own screen. Blocks you read like sentences (the words in brackets are buttons), three kinds of
// cable (sound, notes, movement), drawers of blocks, "write what you want", your own plug-ins, and the PATCHER underneath.
#include "Shell.h"
#include "../Engine/Fluxo.h"

namespace arco {

class FluxoView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit FluxoView(AppShell& s);
    ~FluxoView() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    AppShell& shell;

    // what is on screen: the patch, or the inside of a plug-in (path: the plug-in blocks opened from the top)
    fx::FluxDoc& root();
    fx::FluxDoc& doc();
    std::vector<int> path;
    std::string pathPrefix() const;                    // "7/" inside plug-in 7
    px::Runtime* rt();
    const fx::Compiled& comp();
    void edit(const std::function<void()>& f);         // keep for undo, change, rebuild
    void rebuild();
    void undo(); void redo();
    std::vector<int> sel; int selWire = -1;
    void select(int id, bool add = false);
    int addBlock(const std::string& type, juce::Point<float> at = { -1, -1 }, const juce::String& text = {});
    void addRecipe(int i, bool preview);
    void stopPreview();
    int previewing = -1;
    void openPluginMaker();
    void makePlugin(const juce::String& name, const std::vector<std::pair<int, std::string>>& front, const juce::String& colour);
    void seeInside(int block);
    void toStudio(int block);
    void readBuffer(const juce::String& name, const juce::File& f);
    void info(const juce::String& s, char kind = 'i');
    juce::File libraryDir() const;                     // ~/Documents/ARCO/Fluxo
    std::vector<std::shared_ptr<fx::BlockDef>> mine;   // your plug-ins, saved as files
    void loadMine();
    void saveFile(); void openFile();
    void demo(const juce::String& cmd);                // (screenshots)
    struct Canvas; struct Drawer; struct Side; struct Ask; struct Console; struct Maker;
    std::unique_ptr<Canvas> canvas; std::unique_ptr<Drawer> drawer; std::unique_ptr<Side> side; std::unique_ptr<Ask> askBox; std::unique_ptr<Console> console; std::unique_ptr<Maker> maker;
    juce::Viewport vp, drawerVp;
    std::unique_ptr<juce::TextEditor> askEd;
    juce::Rectangle<int> barR, askR;
private:
    void timerCallback() override;
    std::vector<juce::String> undoStack, redoStack;
    juce::OwnedArray<juce::Component> owned;
    template <typename T, typename... A> T* add(A&&... a) { auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); return c; }
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String beforePreview;
    int lastLogGen = -1;
};

} // namespace arco
