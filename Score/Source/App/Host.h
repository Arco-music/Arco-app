#pragma once
// SCORE - the app around the engine: the sound card, keyboards and pedals, the piece on disk, recording,
// undo, the station player, exporting and the clip tools.
#include <JuceHeader.h>
#include <set>
#include "../Core/Engine.h"

namespace score {

class Host;

// ---------------------------------------------------------------- the score line in performance: GO, BACK, glides
class StationPlayer : private juce::Timer {
public:
    explicit StationPlayer(Host& h) : host(h) {}
    int current = -1;                       // index into project->stations
    bool perform = false;                   // PERFORM (GO plays the transitions) or COMPOSE (clicks jump, nothing plays)
    bool follow = true;                     // while the arranger plays, arriving at a station brings its sound
    void go();                              // the next station
    void back();
    void goTo(int index, bool fromPedal = true);   // PERFORM: with the station's glide and jump; COMPOSE: at once
    void select(int index);                 // COMPOSE: jump to it, its sound at once, nothing plays
    void store();                           // the sound now -> the current station
    void storeQuiet();                      // the same, without an undo step (AUTO STORE after a knob settles)
    bool autoStore = true;                  // knobs you turn belong to the station you are in
    void startPerformance();                // START: from the first station
    void stopPerformance();
    void stopTransportGuards();             // the region loop / wait end
    bool gliding() const { return glideT < 1.0; }
    double glideProgress() const { return glideT; }
    double secondsInStation() const { return (juce::Time::getMillisecondCounterHiRes() - arrived) / 1000.0; }
    double pieceSeconds() const { return started > 0 ? (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0 : 0.0; }
    juce::StringArray log;                  // "1  Breath  0:00.0"
    std::function<void()> onChange;
    void arrive(int index, bool glide, bool viaTimeline);
    void tick();                            // also called by the self-test
private:
    void timerCallback() override { tick(); }
    Host& host;
    std::map<std::string, float> from, to; std::map<Uid, std::pair<bool, bool>> fxOn;   // fx on/off: (from, to)
    double glideT = 1.0, glideDur = 0, glideStart = 0; int glideCurve = 1;
    double arrived = 0, started = 0, autoGoAt = 0;
    int lastSeen = -2; bool ownLoop = false, pendingJump = false;
    friend class Host;
};

// ---------------------------------------------------------------- recording into files
class RecWriter : public juce::Thread {
public:
    RecWriter() : juce::Thread("SCORE recorder") {}
    struct Item { RecSlotPtr slot; std::unique_ptr<juce::AudioFormatWriter> writer; juce::File file; };
    std::vector<Item> items; std::mutex lock;
    void run() override;
    void drain(Item& it);
};

class Host : public juce::AudioIODeviceCallback, public juce::MidiInputCallback, public juce::ChangeBroadcaster, private juce::Timer {
public:
    explicit Host(bool openDevice = true);
    ~Host() override;
    bool live = true;
    Engine engine;
    std::shared_ptr<Project> project;
    std::vector<std::shared_ptr<Project>> previous;   // pieces that were open before (the screen may still point into them for a moment)
    StationPlayer stations { *this };
    juce::AudioDeviceManager devices;
    juce::AudioFormatManager formats;

    // ---- editing (everything that changes the piece goes through here: undo, the graph, the screen)
    void edit(const juce::String& what, std::function<void()> change, bool rebuild = true);
    void knobTouched();                    // a knob was turned (undo after it settles)
    void beginGesture();                   // a drag starts (moving clips, notes, points): one undo step for the whole drag
    void endGesture(const juce::String& what, bool changed = true);
    void bounceTrack(Uid track);           // the track's sound (with its effects) becomes an audio clip on a new track
    void rebuild();                        // the graph for the audio thread
    void undo(); void redo();
    bool canUndo() const { return !undos.empty(); } bool canRedo() const { return !redos.empty(); }
    juce::String undoName() const { return undos.empty() ? juce::String() : undos.back().first; }
    int structure = 0;                     // changes when tracks / clips are added or removed (the screen rebuilds)
    bool dirty = false;
    juce::String status; double statusTime = 0;
    void say(const juce::String& s) { status = s; statusTime = juce::Time::getMillisecondCounterHiRes(); sendChangeMessage(); }

    // ---- selection (shared by the views)
    Uid selTrack = 0, selClip = 0;
    std::set<Uid> selClips;
    Track* selectedTrack() const { return project->track(selTrack); }
    Clip* selectedClip(Track** owner = nullptr) const { return project->clip(selClip, owner); }
    void selectTrack(Uid t);
    void selectClip(Uid c, bool add = false);

    // ---- the piece on disk:  <folder>/<name>.score/  project.json + Audio/
    static juce::File songsFolder();
    void newProject(bool withDemoTracks = true);
    bool openProject(const juce::File& folderOrJson);
    bool saveProject();
    bool saveProjectAs(const juce::String& name);
    bool exportProjectBundle(const juce::File& zip);      // one file to send to someone (zip of the folder)
    juce::File audioFolder() const { return project->folder.getChildFile("Audio"); }
    juce::StringArray recent;
    void autosave();

    // ---- sounds
    SourcePtr loadSource(const juce::String& file);        // relative to the project folder, or absolute; cached
    SourcePtr importAudio(const juce::File& f, juce::String* error = nullptr);   // copies the file into the project
    SourcePtr writeSource(const juce::String& name, const std::vector<float>& L, const std::vector<float>& R);   // a new file in Audio/
    static std::shared_ptr<SampleData> toSample(SourcePtr s);
    bool canRead(const juce::File& f);
    // listening to a file in the browser before using it
    void preview(const juce::File& f); void stopPreview();
    juce::File previewing() const { return previewFile; }
    float previewProgress() const;
    SourcePtr peekSource(const juce::File& f);              // read (cached) without importing: for the browser's waveform
    juce::String importClip(const juce::File& f, Uid track, double beat);   // a file dropped on the arranger (empty = ok)

    // ---- transport
    void play(); void stop(); void togglePlay(); void record(); void locate(double beat);
    void toggleLoop(); void setLoopRange(double a, double b);
    bool recording() const { return engine.recording.load(); }
    double recordingSeconds() const;
    void tapTempo();
    void setTempo(double bpm);

    // ---- live notes (screen keyboard, computer keys, MIDI keyboards) go to the selected instrument track
    void noteOn(int note, float vel); void noteOff(int note); void allNotesOff();
    juce::MidiKeyboardState keyboard;      // the screen keyboard shows what plays
    int keyboardOctave = 4;

    // ---- pedals / keys
    enum Action { ActGo, ActBack, ActPlay, ActRec, ActStore, ActUndo, NumActions };
    struct Pedal { int type = 0, num = 0, ch = 0; };      // 0 none, 1 note, 2 controller
    Pedal pedal[NumActions];
    std::atomic<int> learning { -1 };
    void doAction(int a);
    static juce::String actionName(int a);
    static juce::String pedalText(const Pedal& p);
    std::function<void(int)> onAction;

    // ---- the sound card (like ARCO: open what was chosen, else the next best thing, and say what happened)
    juce::String openAudio(const juce::String& outName, const juce::String& inName, double rate, int buffer);
    int numInputs() const; int numOutputs() const;
    juce::StringArray inputNames() const; juce::StringArray outputNames() const;
    juce::String audioMessage;
    void saveSetup(); void loadSetup(bool openDevice);
    int theme = 0;                         // 0 dark 1 light
    float uiScale = 1.0f;

    // ---- export
    struct ExportOptions { bool stems = false; int range = 0; /*0 piece 1 loop 2 station 3 from-to*/ int stationIdx = 0; int bits = 24; double tail = 2.0; bool normalize = false;
                           double from = 0, to = 0; };
    void exportAudio(const ExportOptions& o, const juce::File& file, std::function<void(bool, juce::String)> done);
    std::atomic<float> exportProgress { -1.0f };
    Uid exportStems = 0;                   // (a bounce renders only this track)

    // ---- clip tools (offline)
    juce::String processClip(Uid clip, const juce::String& op, double amount = 0);   // "normalize" "reverse" "pitch" "stretch" "bake" "revert" "silence" "split"
    void splitClip(Uid clip, double beat);
    void duplicateClips();
    void deleteSelection();
    Uid clipToSampler(Uid clip);           // a new SAMPLER track playing this sound
    void clipToWavetable(Uid clip, Uid track, int osc);

    // ---- AudioIODeviceCallback / MIDI
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
    void changeRate(double r);             // a new sample rate: everything prepared again, sounds converted again
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override;
    void scanMidi();
    std::atomic<int> testTone { 0 };

private:
    void timerCallback() override;
    std::vector<std::pair<juce::String, juce::String>> undos, redos;    // (what, the piece before)
    juce::String pendingKnob; double knobAt = 0; juce::String gestureBefore;
    std::map<juce::String, SourcePtr> pool;
    std::shared_ptr<const AudioSource> previewSrc; std::atomic<int64_t> previewPos { 0 }; juce::File previewFile;
    RecWriter writer;
    std::vector<RecSlotPtr> recSlotsNow;
    double recStartedAt = 0;
    void finishRecording();
    juce::File setupFile() const;
    double lastRate = 0;
    std::vector<double> taps;
    int ccLast[16][128] = {};
    double lastAutosave = 0;
    std::unique_ptr<juce::Thread> exportThread;
    std::unique_ptr<juce::MidiKeyboardState::Listener> keysListener;
    friend class StationPlayer;
};

} // namespace score
