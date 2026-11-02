#pragma once
// ARCO 2.0: the app around the station. A HOME screen of big cards (drag them sideways, tap one to open it) and
// one bar on top that is always there - home, the transport, where we are, the tempo, the metronome - and does the
// right thing for the screen that is open (STUDIO's transport in STUDIO, the drum machine's in DRUM MACHINE ...).
// The station itself (WORKSTATION) is the 1.6 window without its own top row.
#include <JuceHeader.h>
#include "MainView.h"

namespace arco {

class AppShell;

// a knob with its name under it (and its value while it is turned)
class KnobCell : public juce::Component {
public:
    KnobCell(const juce::String& name, ValueFn g, SetFn s, double lo, double hi, double def, ColourFn col, std::function<juce::String(double)> fmt = nullptr);
    void resized() override;
    void paint(juce::Graphics& g) override;
    Knob knob; juce::String name; std::function<juce::String(double)> fmt; ValueFn get;
};

// a row of choices (one is on)
class Segments : public juce::Component {
public:
    Segments(juce::StringArray items, std::function<int()> get, std::function<void(int)> set, ColourFn col);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    juce::StringArray items; std::function<int()> get; std::function<void(int)> set; ColourFn col; float fsize = 9.0f;
};

// ---------------------------------------------------------------- the screens
enum class Screen { Home, Workstation, Loops, Studio, Drums, Synth, Keys, Phrase, Modular, Speakers };
const char* screenTitle(Screen s);

class TopBar : public juce::Component, private juce::Timer {
public:
    explicit TopBar(AppShell& s);
    void paint(juce::Graphics& g) override;
    void resized() override;
    AppShell& shell;
private:
    void timerCallback() override { repaint(); for (auto* c : owned) c->repaint(); }
    juce::OwnedArray<juce::Component> owned;
    Pill *home = nullptr, *rew = nullptr, *play = nullptr, *rec = nullptr, *click = nullptr, *count = nullptr, *undo = nullptr, *save = nullptr, *setup = nullptr;
    juce::Rectangle<int> posR, bpmR, sigR, titleR, meterR;
    float bpmStart = 0, dragY = 0; bool bpmDrag = false;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    float inLevel = 0, outLevel = 0;
};

class HomeScreen : public juce::Component, private juce::Timer {
public:
    explicit HomeScreen(AppShell& s);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
    bool keyPressed(const juce::KeyPress& k) override;
    struct Card { Screen screen; juce::String title, sub; int accent; };
    std::vector<Card> cards;
    float pos = 0, target = 0, vel = 0;            // which card is in the middle (fractional while moving)
    void focusOn(Screen s);
private:
    AppShell& shell;
    void timerCallback() override;
    juce::Rectangle<float> cardRect(int i) const;
    void drawArt(juce::Graphics& g, int i, juce::Rectangle<float> r, juce::Colour c);
    float downX = 0, downPos = 0, lastX = 0; double lastT = 0; bool dragged = false;
    juce::OwnedArray<juce::Component> owned;
};

class DrumView;
class PhraseView;
class LoopsView;

class AppShell : public juce::Component, private juce::Timer {
public:
    explicit AppShell(AudioHost& h);
    ~AppShell() override;
    AudioHost& host;
    Canvas canvas;
    TopBar top { *this };
    HomeScreen homeScreen { *this };
    std::unique_ptr<DrumView> drums;
    std::unique_ptr<PhraseView> phrase;
    std::unique_ptr<LoopsView> loops;
    Screen screen = Screen::Home;
    void show(Screen s);
    void setLayout(bool tall, int w, int h);
    void paint(juce::Graphics& g) override { g.fillAll(Theme::bg()); }
    int W = 1440, H = 880; bool portrait = false;
    static constexpr int TOPH = 64;
    juce::Rectangle<int> content() const { return { 0, TOPH, W, H - TOPH }; }
    // the transport of the screen that is open
    bool studioScreen() const { return screen == Screen::Studio; }
    void transportPlay();
    void transportStop();
    void transportRewind();
    void transportRecord();
    bool isPlaying();
    bool isRecording();
    juce::String positionText();
    double tempo();
    void setTempo(double bpm);
    int beatsPerBar();
    void setBeatsPerBar(int b);
    bool clickOn(); void toggleClick();
    bool countOn(); void toggleCount();
    void undo();
    StudioView* studioView();
private:
    void timerCallback() override;
};

// the window's content: the shell, or (an iPhone, or from the menu) this device as a phone for a station
class MainComponent : public juce::Component {
public:
    explicit MainComponent(AudioHost& h) : shell(h), canvas(shell.canvas), phone(h) {
        addAndMakeVisible(shell); addChildComponent(phone);
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
        // the design is 1440 wide (1000 standing up); its height follows the screen, so the whole screen is used
        int w = tall ? 1000 : 1440;
        int h = tall ? juce::jmax(1200, juce::roundToInt(1000.0f * area.getHeight() / juce::jmax(1, area.getWidth())))
                     : juce::jlimit(880, 1300, juce::roundToInt(1440.0f * area.getHeight() / juce::jmax(1, area.getWidth())));
        shell.setLayout(tall, w, h);
        float s = juce::jmin(area.getWidth() / (float) w, area.getHeight() / (float) h);
        shell.setBounds(0, 0, w, h);
        float dx = area.getX() + (area.getWidth() - w * s) * 0.5f, dy = area.getY() + (area.getHeight() - h * s) * 0.5f;
        shell.setTransform(juce::AffineTransform::scale(s).translated(dx, dy));
        phone.setBounds(getLocalBounds());
    }
    // this device as a phone for a station (iPad: from the menu; iPhone: always)
    void showMic(bool on) {
        if (on == phone.isVisible()) return;
        phone.showBack = !AudioHost::isPhone();
        phone.setVisible(on); shell.setVisible(!on);
        if (on) { phone.start(); phone.toFront(false); } else phone.stop();
    }
    AppShell shell;
    Canvas& canvas;                 // the station (WORKSTATION), inside the shell
    PhoneRemote phone;
};

} // namespace arco
