#pragma once
// ARCO audio host: the sound card, the routing (live inputs, headphones, speakers), recording the show,
// importing files, and saving / opening a song folder.
#include <JuceHeader.h>
#include "../Engine/Engine.h"
#include "../Model/Model.h"
#include "PhoneLink.h"
#include "SynthPlayer.h"

namespace arco {

class AudioHost : public juce::AudioIODeviceCallback, public juce::MidiInputCallback, private juce::MidiKeyboardState::Listener, private juce::AsyncUpdater {
public:
    explicit AudioHost(bool openDevice = true);   // false: tests / screenshots (no sound card, settings not saved)
    bool live = true;
    bool phonesShared = false;               // only 2 outputs: the headphone mix (click, CUE) comes out of 1-2 too
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
    // the sound card: open these devices; if they do not work, try the next best thing and say what happened
    juce::String openAudio(const juce::String& outName, const juce::String& inName, double rate, int buffer);
    void openAllChannels();
    juce::String audioMessage;
    std::atomic<int> testTone { 0 };          // samples of test tone left
    double testPhase = 0;
    void setSynthInto(int v) { synthInto = v; engine.synthInto.store(v); }
    int synthInto = 0;

    // the phone microphone
    MicReceiver phone;                       // Mac: an iPhone / iPad sends its microphone (cable or Wi-Fi)
    MicSender micOut;                        // iPhone / iPad: MIC FOR MAC mode sends this device's microphone
    std::atomic<bool> micMode { false };
    int phoneLive = 0;                       // 0 off, 1-4 = which live input plays it
    juce::String phoneWifi;                  // last Wi-Fi address typed
    juce::StringArray visibleCards = defaultVisibleCards();
    juce::StringArray recentSongs;            // File > Open Recent
    void addRecent(const juce::File& f) { recentSongs.removeString(f.getFullPathName()); recentSongs.insert(0, f.getFullPathName()); while (recentSongs.size() > 8) recentSongs.remove(8); saveSetup(); }
    void newSong();   // the effect cards on the main window (SETUP > EFFECTS)
    void setPhoneLive(int v) { phoneLive = v; engine.phoneLive.store(v); }

    // the SYNTH: one keyboard state for the on-screen keys, the computer keys and MIDI keyboards
    juce::MidiKeyboardState keys;
    SynthPlayer synth { engine };
    void recordSynthOn(int track);            // REC on that track, listening to the synth
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override { synth.noteOn(note, vel); }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override { synth.noteOff(note); }

    // pedals / keys: REC, PLAY/STOP, UNDO, NEXT SCENE
    enum Action { ActRec = 0, ActPlay, ActUndo, ActScene, NumActions };
    struct Pedal { int type = 0, num = 0, ch = 0; };   // type 0 none, 1 note, 2 controller
    Pedal pedal[NumActions];
    std::atomic<int> learning { -1 };
    void doAction(int a);
    static juce::String pedalText(const Pedal& p);
    std::function<void(int)> onAction;       // the interface flashes what happened
    // MIDI learn: any control of the interface, by its id
    struct MidiLink { juce::String id; int type = 0, num = 0, ch = 0; };
    std::vector<MidiLink> midiLinks;
    juce::CriticalSection midiLock;
    juce::String learnTarget;                // the control waiting for a MIDI message (message thread writes, MIDI thread reads under the lock)
    std::function<void(const juce::String& id, int type, int value, bool press)> onMidiControl;
    std::function<void()> onLearned;
    void clearMidiMap() { const juce::ScopedLock sl(midiLock); midiLinks.clear(); learnTarget = {}; saveSetup(); }
    void unlink(const juce::String& id) { const juce::ScopedLock sl(midiLock); midiLinks.erase(std::remove_if(midiLinks.begin(), midiLinks.end(), [&](auto& l) { return l.id == id; }), midiLinks.end()); saveSetup(); }
    juce::String linkText(const juce::String& id) { const juce::ScopedLock sl(midiLock); for (auto& l : midiLinks) if (l.id == id) return pedalText({ l.type, l.num, l.ch }); return {}; }
    void scanMidi();
    bool isPedalCC(int cc, int ch) const;

    // status shown in the song line
    juce::String status;
    juce::File songFolder;                   // the song being worked on (SAVE writes here)

    // songs
    bool saveSong(const juce::File& folder);
    juce::String loopsSignature();           // changes whenever the loops change
    juce::String savedSignature;
    int sigSettle = 0;             // ... as they were at the last SAVE / OPEN
    bool unsavedLoops() { return engine.snap("smask").v.load() > 0.5f && loopsSignature() != savedSignature; }
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
    void loadSetup(bool openDevice = true);
    std::atomic<bool> engineRestarted { false };

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override;

private:
    void handleAsyncUpdate() override;
    juce::AudioFormatManager formats;
    juce::TimeSliceThread writerThread { "ARCO show recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
    juce::CriticalSection recLock;
    std::atomic<juce::int64> recSamples { 0 };
    double lastRate = 0;
    juce::AudioIODevice* rawDone = nullptr;
    std::vector<float> phoneBuf;
    int ccLast[16][128] = {};
    juce::File setupFile() const;
};

} // namespace arco
