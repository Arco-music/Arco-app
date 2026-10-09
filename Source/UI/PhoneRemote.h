#pragma once
// ARCO on an iPhone (1.2): no station window - the phone is a microphone for the station (Mac or iPad) and a remote
// control for all of it: transport, scenes, every track, the mixer, every effect, and the SYNTH (slide pad, keys, chords).
#include <JuceHeader.h>
#include "Widgets.h"
#include "../App/AudioHost.h"

namespace arco {

// what the station told this phone
struct RemoteState {
    std::map<std::string, double> p, s;
    std::map<std::string, juce::String> info;
    std::map<std::string, double> localUntil;           // a value moved here wins over the station's for a moment
    void apply(const juce::StringArray& lines);
    double par(const std::string& id) const { auto it = p.find(id); if (it != p.end()) return it->second; auto* d = defOf(id); return d ? d->def : 0.0; }
    double snap(const std::string& id) const { auto it = s.find(id); return it == s.end() ? 0.0 : it->second; }
    juce::String inf(const std::string& k) const { auto it = info.find(k); return it == info.end() ? juce::String() : it->second; }
    static const ParamDef* defOf(const std::string& id);
};

class PhoneRemote : public juce::Component, private juce::Timer {
public:
    explicit PhoneRemote(AudioHost& h);
    ~PhoneRemote() override;
    void start();                                     // the phone side starts (microphone + link)
    void stop();
    std::function<void()> onBack;                     // iPad: back to the station
    bool showBack = false;
    bool demo = false;                                // screenshots: pretend to be connected
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    RemoteState st;
    void setPage(int p) { page = p; scroll = 0; repaint(); }
private:
    void timerCallback() override;
    AudioHost& host;
    int page = 0;                                     // 0 PLAY, 1 MIX, 2 EFFECTS, 3 SYNTH, 4 AUDIO
    float scroll = 0, scrollMax = 0;
    int fxStrip = 0;                                  // whose sends the EFFECTS page shows
    juce::String openCard;                            // the effect shown open
    int synthMode = 0;                                // 0 slide pad, 1 keys, 2 chords
    int octave = 4;
    // the controls drawn this frame: what they do when touched
    struct Ctl { juce::Rectangle<float> r; int kind; std::string id; double lo, hi; std::function<void()> tap; bool scrolls = true; };
    std::vector<Ctl> ctls;
    int active = -1; juce::Point<float> downAt; float scrollAtDown = 0; bool dragging = false, scrollingPage = false;
    double valueAtDown = 0;
    std::map<int, int> padNotes;                      // touch index -> note sounding on the station
    int padNote = -1;
    void send(const juce::String& line) { host.phoneLink.send(line); }
    void setPar(const std::string& id, double v);
    void press(const juce::String& dest, const juce::String& name, int mul = 1, int add = 0) { send("press " + dest + " " + name + " " + juce::String(mul) + " " + juce::String(add)); }
    void drawPlay(juce::Graphics& g, juce::Rectangle<float> r);
    void drawMix(juce::Graphics& g, juce::Rectangle<float> r);
    void drawFx(juce::Graphics& g, juce::Rectangle<float> r);
    void drawSynth(juce::Graphics& g, juce::Rectangle<float> r);
    void drawAudio(juce::Graphics& g, juce::Rectangle<float> r);   // 1.3: microphone, output, listening to the station
    juce::Array<std::pair<juce::String, juce::String>> inputList;  // id, name (iOS)
    double inputsAt = 0;
   #if JUCE_IOS
    std::unique_ptr<juce::UIViewComponent> routePicker;
   #endif
    juce::Rectangle<float> content;
    float u = 1.0f;                                   // the size unit (phones differ)
    void button(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c, std::function<void()> tap, float fs = 0, bool scrolls = true);
    void slider(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const std::string& id, juce::Colour c, bool scrolls = true);
    void text(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft);
    void padTouch(const juce::MouseEvent& e, bool down, bool up);
    juce::String myName;
    void rename();
    juce::String lastPalette;
};

} // namespace arco
