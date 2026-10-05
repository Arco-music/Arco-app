#pragma once
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"
#include "MicView.h"

namespace arco {

class StripView;
class CardView;
class SetupPanel;
class SynthPanel;
class LookPanel;

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
private:
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
    juce::Array<Meter*> meters;
    bool inputSeen = false;
    juce::String songLine;
    friend class StripView;
    friend class CardView;
    friend class SynthPanel;
};

class MainComponent : public juce::Component {
public:
    explicit MainComponent(AudioHost& h) : canvas(h), mic(h) {
        addAndMakeVisible(canvas); addChildComponent(mic);
        mic.onBack = [this] { showMic(false); };
        setSize(1440, 880);
    }
    void paint(juce::Graphics& g) override { g.fillAll(Theme::bg()); }
    void resized() override {
        auto area = getLocalBounds();
       #if JUCE_IOS
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->safeAreaInsets.subtractedFrom(area);   // notch, home bar
       #endif
        float s = juce::jmin(area.getWidth() / 1440.0f, area.getHeight() / 880.0f);
        canvas.setBounds(0, 0, 1440, 880);
        float dx = area.getX() + (area.getWidth() - 1440 * s) * 0.5f, dy = area.getY() + (area.getHeight() - 880 * s) * 0.5f;
        canvas.setTransform(juce::AffineTransform::scale(s).translated(dx, dy));
        mic.setBounds(area);
    }
    void showMic(bool on) { mic.setVisible(on); if (!on) mic.setSending(false); else mic.toFront(false); }
    Canvas canvas;
    MicView mic;
};

} // namespace arco
