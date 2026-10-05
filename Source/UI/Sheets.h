#pragma once
// ARCO 1.2: the windows that open over the station - EXPORT, HELP, ABOUT - and the app menu (File / Edit / View / Help).
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/Render.h"

namespace arco {
class Canvas;

// a calm framed window over the station: title, close, click outside closes
class Sheet : public juce::Component {
public:
    Sheet(Canvas& c, juce::String t, juce::Rectangle<int> b) : cv(c), title(std::move(t)), box(b) {
        close.onClick = [this] { dismiss(); }; addAndMakeVisible(close); }
    void paint(juce::Graphics& g) override;
    void resized() override { close.setBounds(box.getRight() - 104, box.getY() + 16, 88, 26); }
    void mouseDown(const juce::MouseEvent& e) override { if (!box.contains(e.getPosition())) dismiss(); }
    virtual void dismiss() { setVisible(false); }
    virtual void paintContent(juce::Graphics&) {}
protected:
    Canvas& cv;
    juce::String title, subtitle;
    juce::Rectangle<int> box;
    Pill close { juce::String::fromUTF8("✕ CLOSE"), [] { return Theme::btn(); }, 9.0f, 7.0f };
    juce::OwnedArray<juce::Component> kids;
    void text(juce::Graphics& g, const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt = 0, int lines = 2);
    // a row of choices (one is on)
    void choices(int x, int y, const juce::StringArray& names, int w, std::function<int()> get, std::function<void(int)> set);
};

class ExportSheet : public Sheet, private juce::Thread, private juce::Timer {
public:
    explicit ExportSheet(Canvas& c);
    ~ExportSheet() override;
    void paintContent(juce::Graphics& g) override;
    void dismiss() override { if (!isThreadRunning()) setVisible(false); }
    void start();
private:
    void run() override;
    void timerCallback() override { repaint(); if (!isThreadRunning() && busy) finished(); }
    void finished();
    ExportOptions opt;
    int fileChoice = 1, rateChoice = 1, stemChoice = 0;
    std::atomic<float> progress { 0 };
    std::atomic<bool> busy { false };
    juce::File target;
    juce::String report, error;
    RenderSnapshot snap;
    std::unique_ptr<juce::FileChooser> chooser;
    Pill go { "EXPORT", [] { return Theme::sel(); }, 10.0f, 8.0f };
    Pill share { "SHOW", [] { return Theme::btn(); }, 8.5f, 7.0f };
    juce::Array<juce::File> written;
};

class HelpSheet : public Sheet {
public:
    explicit HelpSheet(Canvas& c);
    void show(int section);
    void paintContent(juce::Graphics& g) override;
    void resized() override;
private:
    struct Page : juce::Component { juce::String body; void paint(juce::Graphics& g) override; int heightFor(int w); };
    int current = 0;
    juce::Viewport view;
    Page page;
    juce::OwnedArray<Pill> tabs;
};

class AboutSheet : public Sheet {
public:
    explicit AboutSheet(Canvas& c);
    void paintContent(juce::Graphics& g) override;
};

// the menus: the Mac menu bar, and the same menus from the ARCO title on iPad
class AppMenu : public juce::MenuBarModel {
public:
    explicit AppMenu(Canvas& c) : cv(c) {}
    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "View", "Help" }; }
    juce::PopupMenu getMenuForIndex(int index, const juce::String& name) override;
    void menuItemSelected(int id, int) override;
    juce::PopupMenu everything();            // for the iPad: one menu with the four as submenus
    static juce::PopupMenu appleExtras();    // About / Check for updates / Settings in the ARCO menu on the Mac
private:
    Canvas& cv;
};

enum Command {
    cmdNewSong = 1, cmdOpen, cmdSave, cmdSaveAs, cmdExport, cmdRecordShow, cmdStudio, cmdImport1, // cmdImport1..+7
    cmdUndo = 20, cmdResetAll, cmdMidiLearn, cmdMidiClear, cmdAutoMix, cmdAB,
    cmdToggleLight = 40, cmdLook, cmdSynth, cmdSetup, cmdFullScreen, cmdMinimise, cmdMicView,
    cmdHelp = 60, cmdShortcuts, cmdWhatsNew, cmdUpdates, cmdAbout,
    cmdPalette = 100,   // + index
    cmdRecent = 200     // + index
};

} // namespace arco
