#pragma once
// ARCO 1.3: the KEYBOARD's instruments. What the keys (on screen, the computer's, a MIDI keyboard) play:
//   SYNTH 2 (ARCO's synthesizer), MODULAR (ARCO's patchable modular), a SAMPLED instrument (SFZ: free pianos, Rhodes,
//   basses, drums - downloaded with one click, or any SFZ you have), or a PLUG-IN (Audio Units / VST3 instruments you own).
// It plays into the same place as the SYNTH (PLAYS INTO: a LIVE strip), so it can be looped, recorded, mixed.
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include "../Engine/Engine.h"
#include "../Engine/Modular.h"
#include "Sampler.h"

namespace arco {

class InstrumentRack : public ExtInstrument, private juce::Timer {
public:
    enum Kind { KSynth = 0, KModular, KSampler, KPlugin };
    InstrumentRack();
    ~InstrumentRack() override;
    std::atomic<int> kind { KSynth };
    void setKind(int k);
    // ---- notes (message thread)
    void noteOn(int note, float vel);
    void noteOff(int note);
    void sustain(bool down);
    void allOff();
    bool takesNotes() const { return kind.load() != KSynth; }
    // ---- audio thread
    void render(float* L, float* R, int n, double sampleRate, double bpm, double beat) override;
    void prepare(double sampleRate);
    std::atomic<float> level { 0 };

    // ---- MODULAR
    Modular modular;

    // ---- SAMPLER
    SfzPlayer sampler;
    juce::String samplerName, samplerPath;        // what is loaded (or loading)
    std::atomic<bool> samplerLoading { false };
    std::atomic<float> samplerProgress { 0 };
    juce::String samplerError;
    void loadSfz(const juce::File& f);            // in the background
    static juce::File instrumentsFolder();        // AppData/ARCO/Instruments
    static juce::File installedSfz(const InstrumentInfo& i);   // the .sfz of a downloaded instrument (or none)
    juce::StringArray userSfz;                    // SFZ files added by hand (this computer)
    // downloads (one at a time)
    struct Download;
    std::unique_ptr<Download> download;
    juce::String downloadId; std::atomic<float> downloadProgress { 0 }; juce::String downloadStatus;
    void startDownload(const InstrumentInfo& i);
    void cancelDownload();
    // 1.5: an instrument from the search (InstrumentIndex): only its files, straight into ARCO
    struct IndexDownload;
    std::unique_ptr<IndexDownload> indexDownload;
    int indexDownloading = -1;
    void startIndexDownload(int entry);
    void removeIndexInstrument(int entry);
    void poll();                                   // downloads and loading finished? (the timer calls it; tests too)

    // ---- PLUG-INS
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String pluginName;
    juce::String loadPlugin(const juce::PluginDescription& d);   // empty = ok, otherwise why not
    void unloadPlugin();
    void scanPlugins(std::function<void(const juce::String&)> progress);
    juce::Array<juce::PluginDescription> instrumentPlugins() const;
    std::unique_ptr<juce::DocumentWindow> pluginWindow;
    void showPluginWindow();

    // ---- save / open (with the song)
    juce::var toVar();
    void fromVar(const juce::var& v);
    std::function<void()> onChange;               // the interface updates

    double sr = 48000;
private:
    juce::CriticalSection pluginLock;
    juce::AudioBuffer<float> pbuf;
    juce::MidiBuffer midiIn, midiPending; juce::SpinLock midiLock;
    double lastBeat = -1e9; int stillBlocks = 0;
    std::vector<float> tmp;
    void timerCallback() override;
    std::unique_ptr<juce::Thread> loader;
    juce::String pendingPluginState;
};

} // namespace arco
