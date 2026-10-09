#pragma once
// ARCO 1.3: a sample player for SFZ instruments (the open format of most free sampled instruments: pianos, Rhodes,
// strings, basses, drums). It reads the SFZ (with #define / #include, <control> <global> <master> <group> <region>),
// loads the samples (WAV, FLAC, AIFF, OGG) into memory as 16-bit, and plays them: key / velocity ranges, round robins,
// loops, envelopes, release samples, the sustain pedal, note polyphony, the common "_onccN" modifiers.
#include <JuceHeader.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <climits>

namespace arco {

struct SfzSample {
    juce::String path;
    std::vector<int16_t> data;            // interleaved, ch channels
    int ch = 1; double sr = 48000; long frames = 0;
    long loopStart = -1, loopEnd = -1;    // from the file (smpl chunk), if any
};

struct SfzRegion {
    int sample = -1;
    int lokey = 0, hikey = 127, lovel = 1, hivel = 127, keycenter = 60;
    float tune = 0, transpose = 0, volume = 0, pan = 0, amplitude = 100, veltrack = 100, keytrack = 100;
    float attack = 0.001f, hold = 0, decay = 0, sustain = 100, release = 0.06f, delay = 0;
    bool releaseTrigger = false;
    int loopMode = -1;                    // -1 default, 0 no_loop, 1 one_shot, 2 loop_continuous, 3 loop_sustain
    long loopStart = -1, loopEnd = -1, offset = 0;
    float lorand = 0, hirand = 1;
    int seqLength = 1, seqPosition = 1;
    int group = 0, offBy = 0, notePolyphony = 0; float offTime = 0.006f;
    std::map<int, std::pair<int, int>> ccRange;      // locc / hicc conditions
    std::map<std::string, std::vector<std::pair<int, float>>> oncc;   // opcode -> (cc, depth)
};

class SfzInstrument {
public:
    bool load(const juce::File& sfz, juce::String& error, std::function<bool(float)> progress = nullptr);
    juce::String name;
    std::vector<SfzRegion> regions;
    std::vector<SfzSample> samples;
    std::map<int, float> ccDefaults;      // set_cc / set_hdcc (0..1)
    size_t memoryBytes() const { size_t b = 0; for (auto& s : samples) b += s.data.size() * 2; return b; }
};

class SfzPlayer {
public:
    void prepare(double sampleRate) { sr = sampleRate; for (auto& v : voices) v.active = false; }
    void setInstrument(std::shared_ptr<SfzInstrument> inst);          // message thread
    std::shared_ptr<SfzInstrument> instrument() const { return std::atomic_load(&inst); }
    // message / MIDI thread: queued, played on the audio thread
    void noteOn(int note, float vel01) { push({ 1, note, vel01 }); }
    void noteOff(int note) { push({ 2, note, 0 }); }
    void sustain(bool down) { push({ 3, 64, down ? 1.0f : 0.0f }); }
    void allOff() { push({ 4, 0, 0 }); }
    void process(float* L, float* R, int n);                           // audio thread: adds
    std::atomic<float> gain { 0.9f };
    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active; return c; }
private:
    struct Ev { int type, note; float val; };
    juce::AbstractFifo fifo { 512 }; Ev evs[512];
    juce::SpinLock pushLock;
    void push(Ev e) { const juce::SpinLock::ScopedLockType sl(pushLock); int s1, n1, s2, n2; fifo.prepareToWrite(1, s1, n1, s2, n2); if (n1) evs[s1] = e; else if (n2) evs[s2] = e; fifo.finishedWrite(n1 + n2); }
    std::shared_ptr<SfzInstrument> inst, playing;
    double sr = 48000;
    struct Voice {
        bool active = false, released = false, held = false, sustained = false;
        const SfzRegion* r = nullptr; const SfzSample* s = nullptr;
        int note = 0; float vel = 0;
        double pos = 0, step = 1; float gl = 1, gr = 1;
        int stage = 0; float env = 0, envRel = 1; double t = 0;           // 0 delay 1 attack 2 hold 3 decay 4 sustain 5 release
        float att = 0.001f, hold = 0, dec = 0, sus = 1, rel = 0.05f, delay = 0, fastRel = 0;
        long loopS = -1, loopE = -1; int loopMode = 0; long age = 0;
    };
    std::array<Voice, 96> voices;
    long clock = 0;
    float cc[140] {};
    bool pedal = false;
    float noteVel[128] {};                              // for release samples
    int seqCounter[128] {};
    juce::Random rnd;
    void start(int note, float vel, bool releasePhase);
    void release(Voice& v);
    float ccVal(int n) const { return n >= 0 && n < 140 ? cc[n] : 0.0f; }
};

// ---------------------------------------------------------------- the free instruments ARCO can fetch
struct InstrumentInfo { const char* id; const char* name; const char* what; const char* licence; const char* zipUrl; const char* sfz; int sizeMB; };
const std::vector<InstrumentInfo>& instrumentCatalogue();

} // namespace arco
