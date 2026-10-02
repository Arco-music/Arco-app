#pragma once
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"

namespace arco {

class StripView;
class CardView;
class SetupPanel;

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
    void showMessage(const juce::String& s) { host.status = s; }
    int pvw = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    void chooseFile(const juce::String& title, const juce::String& pattern, bool save, std::function<void(juce::File)> done, bool dirs = false);
    void layoutStrips();
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
    juce::Array<Meter*> meters;
    bool inputSeen = false;
    juce::String songLine;
    friend class StripView;
    friend class CardView;
};

class MainComponent : public juce::Component {
public:
    explicit MainComponent(AudioHost& h) : canvas(h) { addAndMakeVisible(canvas); setSize(1440, 880); }
    void paint(juce::Graphics& g) override { g.fillAll(Theme::bg()); }
    void resized() override {
        float s = juce::jmin(getWidth() / 1440.0f, getHeight() / 880.0f);
        canvas.setBounds(0, 0, 1440, 880);
        float dx = (getWidth() - 1440 * s) * 0.5f, dy = (getHeight() - 880 * s) * 0.5f;
        canvas.setTransform(juce::AffineTransform::scale(s).translated(dx, dy));
    }
    Canvas canvas;
};

} // namespace arco
