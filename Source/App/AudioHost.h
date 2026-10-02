#pragma once
// ARCO audio host: the sound card, the routing (live inputs, headphones, speakers), recording the show,
// importing files, and saving / opening a song folder.
#include <JuceHeader.h>
#include "../Engine/Engine.h"
#include "../Model/Model.h"

namespace arco {

class AudioHost : public juce::AudioIODeviceCallback, private juce::AsyncUpdater {
public:
    AudioHost();
    ~AudioHost() override;
    Engine engine;
    Model model { engine };
    juce::AudioDeviceManager devices;

    // routing (this computer)
    int nLive = 1;
    int inputCh[4] = { 1, 2, 3, 4 };        // 1-based interface inputs for LIVE 1-4
    int phonesPair = 0;                      // 0 = outputs 3-4, 1 = 5-6 ... 14 = 31-32, 15 = 1-2
    int speakerLayout = 0;                   // 0 ring, 1 front arc
    void applyRouting();
    void applySpeakers();
    void setSynthInto(int v) { synthInto = v; engine.synthInto.store(v); }
    int synthInto = 0;

    // status shown in the song line
    juce::String status;
    juce::File songFolder;                   // the song being worked on (SAVE writes here)

    // songs
    bool saveSong(const juce::File& folder);
    bool openSong(const juce::File& settingsOrFolder);
    // import an audio file into a track (1-8)
    bool importFile(const juce::File& f, int track);
    // recording the show
    bool startRecording(const juce::File& f);
    void stopRecording();
    bool isRecording() const { return threaded != nullptr; }
    double recordSeconds() const { return recSamples.load() / engine.sr; }

    // this computer's settings (Application Support/ARCO/setup.json)
    void saveSetup();
    void loadSetup();
    std::atomic<bool> engineRestarted { false };

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}

private:
    void handleAsyncUpdate() override;
    juce::AudioFormatManager formats;
    juce::TimeSliceThread writerThread { "ARCO show recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
    juce::CriticalSection recLock;
    std::atomic<juce::int64> recSamples { 0 };
    double lastRate = 0;
    juce::File setupFile() const;
};

} // namespace arco
