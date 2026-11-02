#pragma once
// ARCO audio host: the sound card, the routing (live inputs, headphones, speakers), recording the show,
// importing files, and saving / opening a song folder.
#include <JuceHeader.h>
#include <deque>
#include <set>
#include <array>
#include "../Engine/Engine.h"
#include "../Model/Model.h"
#include "PhoneLink.h"
#include "SynthPlayer.h"
#include "Analysis.h"
#include "Studio.h"
#include "Instruments.h"
#include "DrumKits.h"

namespace arco {

class AudioHost : public juce::AudioIODeviceCallback, public juce::MidiInputCallback, private juce::MidiKeyboardState::Listener, private juce::AsyncUpdater, private juce::Timer {
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
    std::atomic<int> testSpeaker { -1 }, testSpkLeft { 0 };   // SPEAKERS & SPACE: a soft noise on one speaker
    double testPhase = 0; juce::Random rnd; float pinkS = 0;
    void setSynthInto(int v) { synthInto = v; engine.synthInto.store(v); }
    int synthInto = 0;
    // 2.0: the SYNTH (or the KEYBOARD's instrument) plays through a LIVE strip that is not an input in use:
    // LIVE 2 when only LIVE 1 is used, LIVE 3 when two are used ... (before, it always took LIVE 2 - and a second
    // instrument plugged into LIVE 2 went silent at the first note)
    int freeSynthStrip() {
        for (int v = juce::jmax(1, nLive); v <= 3; ++v) if (phoneOnLive(v + 1).isEmpty()) return v;
        return 3; }
    void synthNeedsStrip() {
        if (synthInto > 0 && (synthInto >= nLive || nLive >= 4)) return;     // it has one (that is not a used input)
        int v = freeSynthStrip(); setSynthInto(v);
        status = (nLive >= 4 ? juce::String("all four LIVE inputs are used - the SYNTH plays through LIVE 4") : "the SYNTH plays through LIVE " + juce::String(v + 1)); }

    // phones (1.2): the station finds iPhones running ARCO - each one a microphone (on a LIVE input) and a remote control
    Studio studio { *this };                 // STUDIO: the song as a timeline (plays into outputs 1-2 and the headphones)
    PhoneHub phones;                         // the station side
    PhoneServer phoneLink;                   // the phone side (this device is a phone for a station)
    std::atomic<bool> micMode { false };     // this device is a phone now: the microphone goes to the station (and, if asked, the station's sound plays here)
    std::atomic<float> listenVol { 0.8f };   // the phone's own output: the station's sound (0-1)
    std::atomic<int> listenWant { 0 };       // the phone asked for: 0 nothing, 1 the main mix, 2 the headphone mix
    void tickPhones();                       // ~10x a second: commands from the phones, their screens kept current
    void phoneCommand(PhoneConn& c, const juce::String& line);
    juce::String phoneOnLive(int live);      // the name of the phone playing as LIVE n (or empty)
    static bool isPhone();                   // an iPhone: it runs as a phone (microphone + remote), not as a station
    std::function<void()> onCardsChanged;    // the effect cards were chosen from a phone
    juce::StringArray visibleCards = defaultVisibleCards();
    juce::StringArray recentSongs;            // File > Open Recent
    void addRecent(const juce::File& f) { recentSongs.removeString(f.getFullPathName()); recentSongs.insert(0, f.getFullPathName()); while (recentSongs.size() > 8) recentSongs.remove(8); saveSetup(); }
    void newSong();   // the effect cards on the main window (SETUP > EFFECTS)

    // the SYNTH: one keyboard state for the on-screen keys, the computer keys and MIDI keyboards
    juce::MidiKeyboardState keys;
    SynthPlayer synth { engine };
    void recordSynthOn(int track);            // REC on that track, listening to the synth
    std::atomic<bool> studioKeys { false };    // STUDIO is open with an instrument lane: the keys play it (and record into it)
    InstrumentRack rack;                       // 1.3: the KEYBOARD's instrument (MODULAR, a sampled instrument, a plug-in) - or the SYNTH
    std::atomic<bool> drumKeys { false };      // 2.0: the DRUM MACHINE is open: MIDI notes play its pads (General MIDI drum notes)
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override {
        if (drumKeys.load()) { int p = DrumKits::padForNote(note); if (p < 0) p = ((note % 12) + 12) % 12; engine.drumMachine.play(p, vel); return; }
        if (studioKeys.load()) { studio.midiNote(note, vel, true); return; }
        if (latch) {                                    // HOLD: a touch turns a note on, the next touch turns it off
            if (latched.count(note)) { latched.erase(note); playOff(note); } else { latched.insert(note); playOn(note, vel); }
            return; }
        playOn(note, vel); }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override {
        if (drumKeys.load()) return;
        if (studioKeys.load()) { studio.midiNote(note, 0, false); return; }
        if (latch) return;
        playOff(note); }
    void playOn(int note, float vel) { if (rack.takesNotes()) { synthNeedsStrip(); rack.noteOn(note, vel); } else synth.noteOn(note, vel); }
    void playOff(int note) { rack.noteOff(note); synth.noteOff(note); }
    // 1.4: HOLD - notes stay on until touched again (easy chords on a screen; with the arpeggiator every touched note joins it)
    bool latch = false;
    std::set<int> latched;
    void setLatch(bool on) { if (!on) releaseLatched(); latch = on; }
    void releaseLatched() { for (int n : latched) playOff(n); latched.clear(); }
    void latchChord(const std::vector<int>& notes, float vel) {     // a chord pad with HOLD: it replaces the held chord (the same pad again: off)
        std::set<int> want(notes.begin(), notes.end());
        bool same = want == latched;
        releaseLatched();
        if (!same) for (int n : want) { latched.insert(n); playOn(n, vel); } }
    // 1.4: the 8 chord pads (notes above the C of the octave shown; empty = the chords of the key)
    std::array<std::vector<int>, 8> chordPads;
    void setSustain(bool down) { synth.setSustain(down); rack.sustain(down); }

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
    void saveDrums(const juce::File& folder);
    void loadDrums(const juce::File& folder);
    std::function<void(const juce::File&)> onSaveExtra, onOpenExtra;     // 2.0: the shell's screens save with the song (LIVE LOOPS)
    // import an audio file into a track (1-8)
    bool importFile(const juce::File& f, int track);
    // ... or audio already at the engine rate (the smart import: fitted to the piece)
    bool importBuffer(const juce::AudioBuffer<float>& b, int track, const juce::String& name);
    void pollImport();                               // ~30 x a second: hands queued imports to the looper, one by one
    bool importing() const { return !imports.empty(); }
    int trackState(int track);                       // the state of a track (1-8) of the scene playing now
    PieceInfo pieceInfo(bool listenForKey = true);   // the piece now: tempo, bar, loop length, key (from its loops)
    void listenKey(PieceInfo& p);                    // the key of the loops (any thread)
    juce::AudioFormatManager& formatManager() { return formats; }
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
    void timerCallback() override { tickPhones(); }
    juce::AudioFormatManager formats;
    struct PendingImport { std::shared_ptr<SharedBuffer> buf; int track; double sr; juce::String name; int stage = 0; double t = 0, started = 0; bool seenBusy = false; int stateBefore = 0; };
    std::deque<PendingImport> imports;
    juce::TimeSliceThread writerThread { "ARCO show recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
    juce::CriticalSection recLock;
    std::atomic<juce::int64> recSamples { 0 };
    double lastRate = 0;
    juce::AudioIODevice* rawDone = nullptr;
    std::vector<float> phoneBuf[4];
    std::vector<float> mixTapL, mixTapR;     // the station's main mix, for STUDIO
    int ccLast[16][128] = {};
    juce::File setupFile() const;
};

} // namespace arco
