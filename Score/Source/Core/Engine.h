#pragma once
// SCORE - the audio engine. The message thread builds a "graph" (what to play) from the Project and hands it over
// in one atomic step; the audio thread only reads it. Transport commands and live notes travel through lock-free queues.
#include <JuceHeader.h>
#include "Project.h"

namespace score {

constexpr int MAXB = 256;                 // the engine works in blocks of at most this many samples

// a single-producer single-consumer queue
template <typename T, int N> class Spsc {
public:
    bool push(const T& v) { int w = wr.load(std::memory_order_relaxed), n = (w + 1) % N; if (n == rd.load(std::memory_order_acquire)) return false;
        buf[(size_t) w] = v; wr.store(n, std::memory_order_release); return true; }
    bool pop(T& v) { int r = rd.load(std::memory_order_relaxed); if (r == wr.load(std::memory_order_acquire)) return false;
        v = buf[(size_t) r]; rd.store((r + 1) % N, std::memory_order_release); return true; }
private:
    std::array<T, N> buf {}; std::atomic<int> rd { 0 }, wr { 0 };
};

// what the audio thread keeps for each track between blocks
struct TrackRT {
    Uid id = 0;
    std::vector<float> L, R, inL, inR, lastL, lastR, tmpL, tmpR;
    std::atomic<float> peakL { 0 }, peakR { 0 };       // decaying peaks for the meters
    std::atomic<float> grMeter { 0 };
    bool clipOn[128] = {};                              // notes sounding from clips (so they can be stopped / chased)
    std::vector<NoteEv> ev;                              // this block's note events
    Spsc<NoteEv, 512> live;                              // notes from keyboards / the screen
    PitchTracker follow; int followNote = -1, followCand = -1, followCount = 0;
    std::atomic<float> inPeak { 0 };
    void prepare(double sr) {
        for (auto* v : { &L, &R, &inL, &inR, &lastL, &lastR, &tmpL, &tmpR }) v->assign(MAXB, 0.0f);
        follow.prepare(sr); ev.reserve(512);
    }
};
using TrackRTPtr = std::shared_ptr<TrackRT>;

struct GNote { int64_t s, e; int pitch; float vel; };
struct GClip {
    ClipPtr clip; SourcePtr src; ClipRT* rt = nullptr;
    int64_t s = 0, e = 0;              // timeline samples
    int64_t off = 0, srcN = 0;         // source offset / length (samples)
    float gain = 1; int64_t fin = 0, fout = 0; bool rev = false, midi = false, muted = false;
    std::vector<Effect*> fx; int64_t tail = 0;
    std::vector<GNote> notes;          // sorted by start
};
struct GAuto { Param* p = nullptr; std::vector<std::pair<int64_t, float>> pts; };
struct GTrack {
    TrackPtr track; TrackRTPtr rt; Track* t = nullptr;
    Instrument* inst = nullptr; std::vector<Effect*> fx; Mixer* mix = nullptr;
    std::vector<GClip> clips; std::vector<GAuto> autos;
    bool solo = false, arm = false; int monitor = 1; InputType inType = InputType::None; int inCh = 0; int inTrack = -1; int recMode = 0;
    bool follow = false; int followOct = 0; TrackKind kind = TrackKind::Audio;
};
struct Graph {
    std::vector<GTrack> tracks; GTrack retA, retB, master;
    double sr = 48000, bpm = 120, spb = 24000; int bpb = 4; bool anySolo = false;
    bool metro = false; float metroGain = 0.25f;
    std::shared_ptr<Project> keep;     // (not used by the audio thread)
};
using GraphPtr = std::shared_ptr<const Graph>;

// recording: the audio thread writes into these rings, the app's writer thread empties them into files
struct RecSlot {
    Uid track = 0; bool midi = false;
    juce::AbstractFifo fifo { 1 << 18 };
    std::vector<float> data = std::vector<float>((size_t) (1 << 18) * 2);
    std::atomic<int64_t> written { 0 };
    std::vector<float> peaks; std::mutex peakLock;     // the waveform while it records
    // midi
    Spsc<std::pair<double, NoteEv>, 4096> notes;       // (beat, event)
    void push(const float* l, const float* r, int n) {
        int s1, n1, s2, n2; fifo.prepareToWrite(n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { data[(size_t) (s1 + i) * 2] = l[i]; data[(size_t) (s1 + i) * 2 + 1] = r[i]; }
        for (int i = 0; i < n2; ++i) { data[(size_t) (s2 + i) * 2] = l[n1 + i]; data[(size_t) (s2 + i) * 2 + 1] = r[n1 + i]; }
        fifo.finishedWrite(n1 + n2); written += n1 + n2;
    }
};
using RecSlotPtr = std::shared_ptr<RecSlot>;

class Engine {
public:
    Engine();
    void prepare(double sampleRate);
    double sr = 48000.0;
    void process(const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    // ---- message thread
    void setGraph(GraphPtr g);
    GraphPtr graph() const { return std::atomic_load(&cur); }
    TrackRTPtr runtime(Uid track);                       // persistent per track
    void forget(Uid track);

    // transport
    void play();
    void stop();
    void locate(double beat);                            // move the playhead
    void jumpAt(double atBeat, double toBeat);           // jump when the playhead reaches atBeat (stations: GO on the next bar)
    void stopAt(double beat);                            // stop when the playhead reaches it (stations: WAIT); <0 = never
    void setLoop(bool on, double a, double b) { loopOn.store(on); loopA.store(a); loopB.store(b); }
    std::atomic<bool> loopOn { false }; std::atomic<double> loopA { 0 }, loopB { 16 };
    bool isPlaying() const { return playing.load(); }
    double position() const { return posBeat.load(); }
    std::atomic<bool> playing { false };
    std::atomic<double> posBeat { 0 };

    // recording
    void startRecording(std::vector<RecSlotPtr> slots, double fromBeat, double countInBeats);
    void stopRecording();
    std::atomic<bool> recording { false }, recRolling { false };
    std::atomic<double> recStartBeat { 0 };
    std::vector<RecSlotPtr> recSlots() const { std::lock_guard<std::mutex> l(recLock); return slots; }

    // live notes into a track (keyboards, the screen keyboard)
    void liveNote(Uid track, const NoteEv& e);
    std::atomic<Uid> keysTarget { 0 };                   // where the keyboards play (the selected instrument track)

    // routing
    std::atomic<int> outFirst { 0 };                     // main out: device outputs outFirst, outFirst+1
    std::atomic<int> clickFirst { -1 };                  // metronome on separate outputs (-1 = with the main mix)
    // meters
    std::atomic<float> masterL { 0 }, masterR { 0 }, cpu { 0 };
    std::atomic<float> inPeak[8];
    // offline (export): the audio callback is muted while the app renders on another thread
    std::atomic<bool> offline { false };
    std::atomic<int> callbacks { 0 };
    void renderBlock(float* L, float* R, int n, Uid onlyTrack = 0);      // one block of the mix (used by the export)
    void resetAll();                                     // effects / instruments to silence (before and after an export)
    std::function<void(const float*, const float*, int)> masterTap;      // (unused for now)

private:
    void runBlock(const float* const* in, int numIn, float* const* out, int numOut, int off, int n, const Graph& g, Uid only);
    void doTrack(const GTrack& gt, const Graph& g, int64_t p0, int n, bool roll, const float* const* in, int numIn);
    void clipAudio(const GTrack& gt, GClip& c, float* L, float* R, int64_t p0, int n, const TimeInfo& ti);
    void clipNotes(const GTrack& gt, TrackRT& rt, int64_t p0, int n, bool roll);
    void applyAutos(const GTrack& gt, int64_t p);
    void metronome(const Graph& g, float* L, float* R, int64_t p0, int n);
    void followPitch(const GTrack& gt, TrackRT& rt, int n);

    GraphPtr cur;
    std::map<Uid, TrackRTPtr> rts; std::mutex rtLock;
    enum CmdType { CPlay, CStop, CLocate, CJump, CStopAt, CRec, CRecStop };
    struct Cmd { CmdType t; double a = 0, b = 0; };
    Spsc<Cmd, 256> cmds;
    struct LiveEv { Uid track; NoteEv e; };
    Spsc<LiveEv, 1024> liveQ;
    int64_t pos = 0; bool rolling = false;
    double jumpAtB = -1, jumpToB = 0, stopAtB = -1;
    std::vector<RecSlotPtr> slots; mutable std::mutex recLock;
    std::vector<RecSlot*> activeSlots; int64_t recFrom = 0; bool recWanted = false;
    std::vector<float> mL, mR, aL, aR, bL, bR, xL, xR;
    double clickPh = 0; int64_t clickT = -1; bool clickAccent = false;
    std::vector<GClip>* scratch = nullptr;
    float lastOut[2] = { 0, 0 };
};

GraphPtr buildGraph(const Project& p, Engine& e);

} // namespace score
