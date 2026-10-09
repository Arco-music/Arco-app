#pragma once
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"
#include "PhoneRemote.h"

namespace arco {

class StripView;
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
    void layoutCards();
    int cardPage = 0, numCardPages = 1;
    std::vector<juce::Component*> pagePills;
    void openLook(bool show);
    void openSpatial(bool show);
    void openKeyboard(bool show);
    void openModular(bool show);
    // 1.3: iPad portrait - the same station, rearranged for a tall screen (logical size W x H)
    bool portrait = false; int W = 1440, H = 880, stripRows = 1;
    void setLayout(bool tall, int w, int h);
    juce::Rectangle<int> place(juce::Rectangle<int> landscape) const;   // where a top-bar / scene-row control goes now
private:
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> headerKids;
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
    juce::Array<Meter*> meters;
    bool inputSeen = false;
    juce::String songLine;
    friend class StripView;
    friend class CardView;
    friend class SynthPanel;
};

class MainComponent : public juce::Component {
public:
    explicit MainComponent(AudioHost& h) : canvas(h), phone(h) {
        addAndMakeVisible(canvas); addChildComponent(phone);
        phone.onBack = [this] { showMic(false); };
        setSize(1440, 880);
        if (AudioHost::isPhone()) { phone.showBack = false; showMic(true); }      // an iPhone is a phone: microphone + remote
    }
    void paint(juce::Graphics& g) override { g.fillAll(Theme::bg()); }
    void resized() override {
        auto area = getLocalBounds();
       #if JUCE_IOS
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->safeAreaInsets.subtractedFrom(area);   // notch, home bar
       #endif
        // a tall screen (iPad in portrait): the station rearranges itself, 1000 wide
        bool tall = area.getHeight() > area.getWidth() * 1.05f;
        int w = tall ? 1000 : 1440, h = tall ? juce::jmax(1200, juce::roundToInt(1000.0f * area.getHeight() / juce::jmax(1, area.getWidth()))) : 880;
        canvas.setLayout(tall, w, h);
        float s = juce::jmin(area.getWidth() / (float) w, area.getHeight() / (float) h);
        canvas.setBounds(0, 0, w, h);
        float dx = area.getX() + (area.getWidth() - w * s) * 0.5f, dy = area.getY() + (area.getHeight() - h * s) * 0.5f;
        canvas.setTransform(juce::AffineTransform::scale(s).translated(dx, dy));
        phone.setBounds(getLocalBounds());
    }
    // this device as a phone for a station (iPad: from the menu; iPhone: always)
    void showMic(bool on) {
        if (on == phone.isVisible()) return;
        phone.showBack = !AudioHost::isPhone();
        phone.setVisible(on); canvas.setVisible(!on);
        if (on) { phone.start(); phone.toFront(false); } else phone.stop();
    }
    Canvas canvas;
    PhoneRemote phone;
};

} // namespace arco
