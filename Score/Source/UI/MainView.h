#pragma once
// SCORE - the main window: the top bar (transport, tempo, views), the browser, the arranger / score line / mixer,
// the devices and the editor below, the keyboard, the status line.
#include "ArrangeView.h"
#include "Rack.h"
#include "Editors.h"
#include "StationView.h"
#include "MixerView.h"
#include "InstrumentView.h"
#include "Browser.h"
#include "Dialogs.h"

namespace score {

class TopBar : public juce::Component {
public:
    TopBar();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void tick();
    std::function<void(int)> onView; std::function<void()> onFileMenu, onBrowser, onKeys, onSettings, onExport;
    int view = 0;
    juce::Rectangle<int> nameArea, posArea;
    void mouseDown(const juce::MouseEvent& e) override;
private:
    IconButton toStart { Icon::ToStart, "to the start" }, play { Icon::Play, "play / stop (space)" }, stop { Icon::Stop, "stop" }, rec { Icon::Record, "record (R)" },
               loop { Icon::Loop, "loop (L)" }, metro { Icon::Metro, U("metronome (M) · right-click: count-in") }, undo { Icon::Undo, "undo" }, redo { Icon::Redo, "redo" },
               browser { Icon::Folder, "sounds, synths, effects, pieces" }, keysBtn { Icon::Keys, "the keyboard" }, settings { Icon::Gear, "settings" }, exportBtn { Icon::Export, "export" };
    Pill vArr { "ARRANGE" }, vLine { "SCORE LINE" }, vMix { "MIXER" }, tap { "TAP" }, goBtn { "GO" };
    std::unique_ptr<ValueBox> tempo, sig;
    std::unique_ptr<Meter> mL, mR;
};

// sets up what every control reaches (before any view is built)
struct UiHolder { Ui ui; explicit UiHolder(Host& h) { ui.host = &h; Ui::get() = &ui; } ~UiHolder() { if (Ui::get() == &ui) Ui::get() = nullptr; } };

class MainView : private UiHolder, public juce::Component, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget, private juce::ChangeListener, private juce::Timer {
public:
    MainView(Host& h);
    ~MainView() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& k) override;
    bool keyStateChanged(bool isDown) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { draggingSplit = false; }
    void mouseMove(const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void setView(int v);
    void openInstrument(Uid track);
    void showOverlay(juce::Component* c);
    void closeOverlay();
    void fileMenu();
    void saveAs();
    void rebuildAll();
    int view = 0;
    // for the self-test / screenshots
    ArrangeView arrange; StationView line; MixerView mixer; Rack rack; ClipEditor editor; Browser browser; TopBar top;
    std::unique_ptr<InstrumentEditor> inst;
    std::unique_ptr<juce::Component> overlay;
    int bottomTab = 0;                       // 0 devices, 1 editor
    bool showBrowser = true, showKeys = false;
    int bottomH = 270;
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    Host& host_;
    Pill tDev { "DEVICES" }, tEd { "EDITOR" };
    std::unique_ptr<juce::MidiKeyboardComponent> keys;
    juce::Rectangle<int> split, status;
    bool draggingSplit = false; int splitStart = 0;
    std::set<int> held; int octave = 4;
    juce::TooltipWindow tips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String lastStatus; double lastStatusTime = 0;
};

} // namespace score
