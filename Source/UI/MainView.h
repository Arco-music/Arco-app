#pragma once
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"
#include "PhoneRemote.h"

namespace arco {

class StripView;
class MasterView;
class CardView;
class SetupPanel;
class SynthPanel;
class LookPanel;
class ExportSheet;
class HelpSheet;
class AboutSheet;
class ImportSheet;
class AutoMixSheet;
class StudioView;
class SpatialPanel;
class KeyboardPanel;
class ModularPanel;
class AppMenu;
class LearnOverlay;

// The instrument: drawn in a 1440 x 880 design space (the PDF layout) and scaled to the window.
class Canvas : public juce::Component, private juce::Timer {
public:
    explicit Canvas(AudioHost& h);
    ~Canvas() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    AudioHost& host;
    Model& model() { return host.model; }
    Engine& engine() { return host.engine; }
    float snap(const char* n) { return engine().snap(n).v.load(); }
    void openSetup(bool show);
    void themeChanged();                       // a new palette: everything repaints in it
    void openSynth(bool show);
    void synthPage(int p);
    void synthKeys(int m, bool edit);
    void synthPreset(int i); int synthPresetIndex() const;
    void openMic(bool show);
    bool keyPressed(const juce::KeyPress& k) override;
    void doSave(bool askName);
    void doOpen();
    void doRecShow();
    static juce::File songsDir();
    juce::String flash; double flashUntil = 0;     // a short word after a pedal / key (REC, PLAY ...)
    void showMessage(const juce::String& s) { host.status = s; }
    int pvw = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseFile(const juce::String& title, const juce::String& pattern, bool save, std::function<void(juce::File)> done, bool dirs = false);
    void layoutStrips();
    juce::String focusFx() const;
    void layoutCards();
    int cardPage = 0, numCardPages = 1; bool inLayout = false;
    std::vector<juce::Component*> pagePills;
    // 1.6: the effects in six families - the effect map shows them all, one family is open below it
    static constexpr int NFAM = 6;
    static int familyOf(const juce::String& key);
    static const char* familyName(int f);
    static juce::Colour familyColour(int f);
    int fxFamily = 0;
    juce::Array<int> familyCards(int f) const;      // the shown cards (indexes into cardTable) of one family
    bool fxUsed(const juce::String& key);           // is this effect working now (lit on the map)
    void showFamily(int f, int page = 0);
    void showCard(int cardIndex);
    juce::Rectangle<int> mapR, fxPanelR; int fxCard = -1; int cardsPerPage = 4; int shotTracks = 0;   // screenshots: how many tracks to show
    std::unique_ptr<juce::Component> fxMap;
    void openLook(bool show);
    void openSpatial(bool show);
    void openOutputs();                  // the SPEAKERS & SPACE window on its OUTPUTS page
    void openKeyboard(bool show);
    void keyboardSearch(const juce::String& q);
    void openModular(bool show);
    // 1.3: iPad portrait - the same station, rearranged for a tall screen (logical size W x H)
    bool portrait = false; int W = 1440, H = 880, stripRows = 1, stripH = 298;
    int visibleCardCount() const;
    void setLayout(bool tall, int w, int h);
    // 1.6: the top bar and the scene row in groups (cards); each control sits in its group, the groups move (landscape / upright)
    void layoutHeader();
    juce::Rectangle<int> grpR[9]; int headerBottom = 130;
    std::unique_ptr<MasterView> masterView;
    // 2.0: inside the app shell - its top bar has the transport, the tempo, the song; HOME has the other windows
    bool inShell = false;
    void showStation(bool on);          // 2.0: in the shell, a window (SYNTH ...) is a screen of its own: nothing of the station behind it
    bool stationShown = true;
    bool synthOpen() const; bool keyboardOpen() const; bool modularOpen() const; bool spatialOpen() const;
    std::function<void()> onOpenStudio;
    std::function<void(int lane, int device)> onEditPatcher;        // 2.4: a STUDIO device's PATCHER program, opened in its screen
private:
    struct HK { juce::Component* c; int grp; juce::Rectangle<int> rel; };
    std::vector<HK> hk; std::vector<juce::Component*> winBtns;
    template <typename T, typename... A> T* hadd(int grp, juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); hk.push_back({ c, grp, r }); return c; }
    void paintTop(juce::Graphics& g);
    void paintScenes(juce::Graphics& g);
    void timerCallback() override;
    void buildTopBar();
    void buildScenes();
    template <typename T, typename... A> T* add(juce::Rectangle<int> r, A&&... a) {
        auto* c = new T(std::forward<A>(a)...); owned.add(c); addAndMakeVisible(c); c->setBounds(r); return c; }
    juce::OwnedArray<juce::Component> owned;
    juce::OwnedArray<StripView> strips;
    juce::OwnedArray<CardView> cards;
    std::unique_ptr<SetupPanel> setup;
    std::unique_ptr<SynthPanel> synthWin;
    std::unique_ptr<LookPanel> look;
    std::unique_ptr<SpatialPanel> spatial;
    std::unique_ptr<KeyboardPanel> keyboardWin;
    std::unique_ptr<ModularPanel> modularWin;
public:
    std::unique_ptr<ExportSheet> exportSheet;
    std::unique_ptr<HelpSheet> helpSheet;
    std::unique_ptr<AboutSheet> aboutSheet;
    std::unique_ptr<ImportSheet> importSheet;
    std::unique_ptr<AutoMixSheet> autoMixSheet;
    std::unique_ptr<juce::Component> studioWindow;   // Mac: its own window; iPad: over the station
    StudioView* studioView = nullptr;
    void requestImport(const juce::File& f, int track);   // the smart import window
    std::unique_ptr<AppMenu> menu;
    std::unique_ptr<LearnOverlay> learnLayer;
    juce::Component* topWindow() const;         // the window open over the station, or nullptr
    void command(int id);                      // every menu item and shortcut (Commands.cpp)
    void showMenu();                           // the menus from the ARCO title (iPad, or anywhere)
    bool midiLearnActive() const { return midiLearn; }
    bool autoMixAvailable() const;
    bool anyOverlay() const;
    void importInto(int track);
    bool midiLearn = false;
    int menuTick = 0;
    juce::Component* overlayExtra() const;
    void openStudio();
    void setMidiLearn(bool on);
    void openAutoMix();
    void autoMixCompare();
private:
    bool inputSeen = false;
    juce::String songLine;
    friend class StripView;
    friend class MasterView;
    friend class CardView;
    friend class SynthPanel;
};

} // namespace arco
