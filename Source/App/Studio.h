#pragma once
// ARCO 1.3: STUDIO - the arrangement. Like a DAW's arrangement view, made for what the station records:
//  - lanes: AUDIO (clips of recorded / imported sound), INSTRUMENT (MIDI clips playing a SYNTH 2 of their own),
//    and two RETURNS (A: a reverb, B: a delay) every lane can send to; a MASTER with its own devices
//  - clips: move, trim, split, duplicate, fades, gain, loop, reverse, mute; MIDI clips with notes (piano roll)
//  - devices on every lane (EQ, compressor, saturation, delay, reverb, chorus, gate, utility, limiter, filter), in a chain
//  - automation of volume, pan, the sends and every device knob (breakpoints)
//  - transport: play, record (audio from any input, MIDI from the keyboard), loop region, metronome, count-in, markers
//  - from the station: scenes laid out as a song, single loops dragged in, the station's live sound recorded into lanes
//  - export of the mix and of every lane (stems); saved next to the song
// The same code plays and exports, so what you hear is what you get.
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "../Engine/StripMix.h"
#include "../Engine/Synth2.h"
#include "StudioFx.h"
#include "Render.h"
#include "Analysis.h"

namespace arco {
class AudioHost;

struct StudioSource {                               // audio at the studio rate; r empty = mono. Shared between song copies (undo is cheap)
    juce::String name, id;
    std::vector<float> l, r;
    long size() const { return (long) l.size(); }
};
using SourcePtr = std::shared_ptr<const StudioSource>;

struct StudioNote { double start = 0, len = 0; int pitch = 60; float vel = 0.8f; };   // samples, from the clip's content start
// 2.3: TAKES - every pass of a recording kept in the clip; one plays. `delta`: where this take's sound sits against the
// one playing now (0 for the playing one), so trimming, moving and splitting keep them all in line (comping: split, choose)
struct StudioTake { int source = -1; long delta = 0; juce::String name; };
struct StudioClip {
    int lane = 0, source = -1;                      // source: an audio source; -1 = a MIDI clip (notes)
    long start = 0, length = 0;                     // on the timeline (samples)
    long offset = 0;                                // where in the content it starts
    bool loop = true;                               // the content repeats inside the clip
    float gainDb = 0, fadeIn = 0, fadeOut = 0;      // fades in samples
    bool reverse = false, muted = false;
    juce::String name; int colour = -1;
    std::vector<StudioNote> notes; long midiLen = 0; // MIDI: the notes and the length of the content (bars)
    std::vector<StudioTake> takes;                  // 2.3: empty = one take; else every take, `take` the one playing
    int take = 0;
    bool isMidi() const { return source < 0; }
    long end() const { return start + length; }
};
struct AutoPoint { double t = 0; float v = 0; };
struct StudioAuto { std::string param; std::vector<AutoPoint> pts; };   // "vol", "pan", "sendA", "sendB", "d<i>p<k>"
enum LaneKind { LaneAudio = 0, LaneInstrument = 1, LaneReturn = 2, LaneBus = 3 };
// 2.3: a send to any bus or return (beside the two fixed ones, A and B); pre: before the fader
struct StudioSend { int to = -1; float amt = 0.5f; bool pre = false; };
struct StudioLane {
    juce::String name; int colour = 4; int kind = LaneAudio;
    float vol = 0, pan = 0;                         // dB, -1..1
    bool mute = false, solo = false, arm = false, monitor = false;
    int input = 0;                                  // recording from: 0-3 = LIVE 1-4 (the station's inputs, dry), 4+ = the sound card's input n-4, -1 = the station's mix
    float sendA = 0, sendB = 0;                     // 0..1, after the fader
    // 2.3 ROUTING: where it goes (-1 the master, else a BUS or RETURN lane), more sends; a BUS can fold its lanes away
    int out = -1;
    std::vector<StudioSend> sends;
    bool folded = false;
    std::vector<StudioDevice> devices;
    std::vector<StudioAuto> autos;
    std::map<std::string, float> synth;             // INSTRUMENT: its SYNTH 2 sound (parameter -> value)
    int showAuto = -1;                              // which automation is shown under the lane (-1 none)
    // 2.3: FREEZE - the lane played from a recording of itself (its clips, synth and effects rendered once): light to
    // play, nothing lost (UNFREEZE brings it all back). Its volume, pan, sends and automation of them still work
    bool frozen = false; int freezeSrc = -1; long freezeAt = 0;
    float height = 74;
    // 1.2 songs: the simple EQ / compressor become devices when opened
    float lo = 0, mid = 0, hi = 0, midHz = 1000, comp = 0, hp = 0;
};
struct StudioMarker { long pos = 0; juce::String name; };
struct StudioSong {
    double sr = 48000, bpm = 120; int beats = 4, den = 4;       // 2.3: den - the time signature's note (display; a beat is one of these)
    std::vector<SourcePtr> sources;
    std::vector<StudioClip> clips;
    std::vector<StudioLane> lanes;
    std::vector<StudioDevice> master;               // the master's chain
    std::vector<StudioAuto> masterAutos;
    float masterDb = 0, glue = 20;
    bool loopOn = false; long loopStart = 0, loopEnd = 0;
    bool metronome = false; bool countIn = true;
    std::vector<StudioMarker> markers;
    // 2.3: PUNCH - recording keeps only what falls between these; AUTO-CROSSFADE where two clips overlap on a lane;
    // RIPPLE - deleting a clip / time pulls what comes after it to the left (0 off, 1 that lane, 2 every lane)
    bool punchOn = false; long punchStart = 0, punchEnd = 0;
    bool autoXfade = true;
    int ripple = 0;
    long length() const { long l = 0; for (auto& c : clips) l = std::max(l, c.start + c.length); return l; }
    double barLen() const { return 60.0 / bpm * beats * sr; }
    double beatLen() const { return 60.0 / bpm * sr; }
    int returnA() const { for (int i = 0; i < (int) lanes.size(); ++i) if (lanes[(size_t) i].kind == LaneReturn) return i; return -1; }
    int returnB() const { int f = 0; for (int i = 0; i < (int) lanes.size(); ++i) if (lanes[(size_t) i].kind == LaneReturn && f++ == 1) return i; return -1; }
    // 2.3 routing
    bool isTarget(int i) const { return i >= 0 && i < (int) lanes.size() && (lanes[(size_t) i].kind == LaneBus || lanes[(size_t) i].kind == LaneReturn); }
    bool feeds(int from, int to) const;             // does `from` reach `to` (by its output or a send, through buses)?
    bool canRoute(int from, int to) const { return to == -1 || (isTarget(to) && to != from && !feeds(to, from)); }
    bool hidden(int i) const;                       // inside a folded bus
};

// one parameter that can be automated: its name and range
struct AutoParam { std::string id; juce::String name; float lo, hi; };
std::vector<AutoParam> autoParams(const StudioLane& L);
float autoValue(const StudioAuto& a, double t, float fallback);

class Studio {
public:
    explicit Studio(AudioHost& h);
    AudioHost& host;
    StudioSong song;
    juce::CriticalSection lock;                     // the audio thread renders under it (try-lock); the message thread edits under it
    // the song from the station: every scene with loops, `repeats` times each, in order A-D
    bool buildFromStation(int repeats, juce::String& why);
    // one loop of the station (scene 1-4, track 0-7) as a clip at `at` on `lane` (-1: a new lane)
    bool addStationLoop(int scene, int track, long at, int lane, juce::String& why);
    // a whole scene at `at` (its loops on the lanes of their tracks)
    bool addStationScene(int scene, long at, int repeats, juce::String& why);
    void ensureReturns();                           // RETURN A (reverb) and RETURN B (delay), a master chain
    int addLane(int kind, const juce::String& name);
    void laneInserted(int at);                      // 2.3: every lane number (outputs, sends, sidechains) after it moves down
    void laneRemoved(int at);
    int addBus(const juce::String& name, const std::vector<int>& lanesIn);   // a bus (folder) right above the first of them, they go into it
    bool deleteLane(int lane);                      // with its clips
    int duplicateLane(int lane);                    // with its clips, right below it
    void refresh();                                 // the engine takes the song's lanes and clips as they are now (no stop)
    // 2.2: the effects recorded into the sound: the lane played alone through its own effects (not its volume, pan,
    // sends or the master) becomes one audio clip; its effects are then switched off (not heard twice). An instrument
    // lane gives a new audio lane under it (and is muted). UNDO brings it all back. Returns the new clip (-1: failed)
    int printLane(int lane, juce::String& why);
    // ready-made effect chains for a lane: 0 voice, 1 instrument, 2 drums, 3 warm, 4 wide echo, 5 big room
    void addChain(int lane, int chain);
    // 2.2: an audio file (already listened to: prepareImport) into the song at `at` on `lane` (-1: a new lane).
    // keep: its own speed and pitch, sample for sample (the first audio in an empty song gives the song its tempo);
    // fit: stretched to the song's tempo, same pitch. Its first bar line lands on a bar line. Returns the clip (-1: failed)
    int importAudio(const ImportPlan& plan, int lane, long at, bool fit, juce::String& why);
    // play
    std::atomic<bool> playing { false }, recording { false };
    std::atomic<long> pos { 0 };
    std::atomic<long> countInLeft { 0 };
    void play(); void stop(); void startRecording(); void stopRecording();
    // 1.4: the studio always runs at the sound card's rate. A song made (or opened) at another rate is converted:
    // its audio resampled, its times moved (before, it played too slow or too fast). Message thread.
    bool matchRate(double newSr);
    bool renderBlock(float* l, float* r, int n);                       // audio thread: adds into l / r (false: nothing to play)
    std::atomic<int> missedBlocks { 0 };                               // blocks the audio thread found the song locked (it must stay 0)
    std::vector<float> tlBuf = std::vector<float>(8192), trBuf = std::vector<float>(8192);
    void input(const float* const* in, int numIn, const float* live[4], const float* mixL, const float* mixR, int n);   // audio thread: what can be recorded
    void collectRecordings();                                           // message thread, often: the recorded audio into clips
    long recordLatency = 0;                                              // samples the recording is moved earlier (the sound card's delay)
    // ---- 2.3 editing like a full DAW (message thread; each one takes an undo step itself)
    bool chooseTake(int clip, int take);            // the take that plays (others kept)
    int explodeTakes(int clip);                     // every take on a lane of its own under it (returns the first new lane)
    bool deleteOtherTakes(int clip);
    bool freezeLane(int lane, juce::String& why);
    bool unfreezeLane(int lane);
    bool removeClips(std::vector<int> clips);       // with RIPPLE as set
    void removeTime(long a, long b);                // the time between a and b gone on every lane (clips cut, after pulled left)
    void insertTime(long at, long len);             // silence inserted (what is after moves right)
    std::vector<StudioClip> clipboard; long clipboardStart = 0;
    void copyClips(const std::vector<int>& clips);
    std::vector<int> pasteClips(long at, int lane); // the first copied clip's lane goes to `lane` (the others keep their distance)
    void quantise(int clip, double grid, float strength, bool ends);   // grid in samples
    int importMidi(const juce::File& f, int lane, long at, juce::String& why);   // returns the first new clip
    bool exportMidi(int clip, const juce::File& f);
    // MIDI from the keyboard (recorded into an armed INSTRUMENT lane, and heard on it)
    void midiNote(int note, float vel, bool on);
    // export (any thread)
    bool renderAll(juce::AudioBuffer<float>& out, std::function<bool(float)> progress, int onlyLane = -1);
    // undo
    void snapshot();
    bool undo();
    bool redo();
    // save / open next to the song
    bool save(const juce::File& folder);
    bool load(const juce::File& folder);
    // effects printed: a lane replaced by its sound through the station's effects
    bool printEffects(int lane, std::function<bool(float)> progress);
    // clip tools
    bool reverseClip(int clip);
    bool normaliseClip(int clip);
    bool transposeClip(int clip, double semitones);                    // a new source, best quality
    SourcePtr makeSource(const juce::String& name, std::vector<float> l, std::vector<float> r = {});
    std::vector<int> laneStrip;                      // which station strip each lane came from (4.. = track), -1 none
    std::vector<int> laneScene;
    // meters (message thread reads)
    struct Meter { std::atomic<float> l { 0 }, r { 0 }, gr { 0 }; };
    std::array<Meter, 64> meters;                    // per lane
    Meter masterMeter;
    juce::String lastRecordName;
private:
    struct LaneRt {
        std::vector<DeviceDsp> dev;
        std::unique_ptr<Synth2> syn;
        std::map<std::string, float> synthSet;       // what was sent to the synth
        struct V { int note = -1; long trig = 0; double age = 0; } v[Synth2::NV];
        double clock = 0;
        int nDev = -1;
    };
    struct Runtime {
        double sr = 0;
        std::vector<LaneRt> lanes;
        std::vector<DeviceDsp> master;
        std::vector<float> buf[6];                  // lane L/R, return A L/R, return B L/R
        std::vector<float> zero, synL, synR, synM, synN, synLfo, beat, bpm, pitch;
        std::vector<float> busIn, key;              // 2.3: per lane, 2 x 64: what comes into a bus / return; the lane's sound before its fader (sidechains)
        std::vector<int> order;                     // the lanes in the order they are mixed (what feeds a bus before it)
        double clickPh = 0; long clickLeft = 0; float clickHz = 0;
        std::array<Meter, 64>* meters = nullptr;   // the live engine's lane meters (null when exporting)
    };
    Runtime rt;                                      // the live one
    void prepareRuntime(Runtime& R, const StudioSong& s);   // (message thread / before a render): allocations
    static void mix(const StudioSong& s, Runtime& R, float* outL, float* outR, long start, int n, int onlyLane, const float* liveIn[64], bool metro, bool clipsOn);
    static void laneSynth(const StudioSong& s, const StudioLane& L, int li, LaneRt& lr, Runtime& R, long start, int n, float* outL, float* outR, bool clipsOn);
    std::vector<StudioSong> undoStack, redoStack;
    // recording
    struct Take { int lane = -1; long startPos = 0; std::vector<float> l, r; juce::AbstractFifo fifo { 1 << 17 }; std::vector<float> fl, fr; bool stereo = false; };
    std::vector<std::unique_ptr<Take>> takes;        // one per armed lane while recording
    juce::CriticalSection takeLock;
    struct MidiRec { int lane = -1; long startPos = 0; bool looped = false; std::vector<StudioNote> notes; std::map<int, StudioNote> open; };
    std::atomic<bool> recLooped { false }; long recLoopStart = 0, recLoopEnd = 0;      // 2.3: recording inside the loop - every pass a take
    void addTakeClip(int lane, long startPos, std::vector<float> l, std::vector<float> r, const juce::String& name);
    bool renderLaneAlone(const StudioSong& copy, int lane, long first, long last, juce::AudioBuffer<float>& out);
    MidiRec midiRec;
    std::atomic<int> liveNotes[128] {};              // keyboard notes heard on the armed instrument lane (audio thread plays them)
    std::atomic<int> liveVel[128] {};
    std::vector<float> monitorBuf[64];               // live input per lane (monitoring), filled by input()
    std::atomic<bool> monitorFresh { false };
    std::atomic<juce::uint32> keysUntil { 0 };
};

} // namespace arco
