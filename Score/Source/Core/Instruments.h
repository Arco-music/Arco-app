#pragma once
// SCORE - the instruments: NOVA (wavetable synth), SAMPLER, DRUMS. All share an arpeggiator.
#include <memory>
#include <string>
#include <vector>
#include <array>
#include "Effects.h"
#include "Wavetable.h"

namespace score {

// a note event inside one audio block
struct NoteEv {
    enum Type : uint8_t { On, Off, Bend, CC, AllOff, Sustain };
    int offset = 0; Type type = On; int a = 0; float b = 0;     // note / cc number, velocity / value
};

// audio for the sampler and drum pads (prepared by the app: already at the engine's sample rate)
struct SampleData {
    std::vector<float> L, R; double sr = 48000.0; std::string path, name;
    std::vector<int> slices;                 // onset positions found in the sound (for SLICES)
    int length() const { return (int) L.size(); }
};
std::vector<int> findOnsets(const SampleData& s, int maxSlices);

class Instrument : public ParamOwner {
public:
    explicit Instrument(std::string t) : type(std::move(t)) {}
    std::string type;
    double sr = 48000.0; int maxBlock = 512;
    virtual void prepare(double s, int mb) { sr = s; maxBlock = mb; prepared = true; }
    bool isPrepared(double s) const { return prepared && sr == s; }
    bool prepared = false;
    // renders n samples (added into L/R), handling the events at their sample offsets and the arpeggiator
    void render(float* L, float* R, int n, const NoteEv* ev, int nev, const TimeInfo& t);
    virtual void noteOn(int note, float vel) = 0;
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff(bool hard) = 0;
    virtual void pitchBend(float /*semitones (-1..1 of the range)*/) {}
    virtual void controller(int /*cc*/, float /*0..1*/) {}
    virtual int activeVoices() const { return 0; }
    virtual std::vector<Effect*> internalEffects() { return {}; }
    ScopeRing scope;
    std::atomic<int> lastNote { -1 };
    std::atomic<int> arpSteps { 0 };        // counts the arpeggiator's notes (for the screen)
    // shown on the keyboard: which notes sound now
    std::array<std::atomic<uint8_t>, 128> held {};
protected:
    virtual void renderSub(float* L, float* R, int n, const TimeInfo& t) = 0;
    void addArpParams();
    Param* arpOn = nullptr; Param* arpMode = nullptr; Param* arpRate = nullptr; Param* arpOct = nullptr; Param* arpGate = nullptr;
    Param* arpSwing = nullptr; Param* arpLatch = nullptr;
    bool sustain = false; std::vector<int> sustained;
private:
    void handle(const NoteEv& e);
    // arpeggiator state
    std::vector<std::pair<int, float>> arpHeld, arpLatched;
    std::vector<int> arpSounding;
    int arpStep = 0; double arpOffAt = -1, arpFree = 0; long lastStep = -1; bool arpWas = false;
    Rng arpRng { 99 };
    void arpTrigger();
    void arpRelease();
};

// ---------------------------------------------------------------- NOVA: 3 wavetable oscillators, sub, noise, 2 filters,
// 4 envelopes, 4 LFOs, a 16-slot modulation matrix, 4 macros, unison, glide, built-in effects
class Nova : public Instrument {
public:
    Nova();
    static constexpr int MAXV = 16, MAXU = 8, NSLOT = 16;
    void prepare(double s, int mb) override;
    void noteOn(int note, float vel) override;
    void noteOff(int note) override;
    void allNotesOff(bool hard) override;
    void pitchBend(float v) override { bend = v; }
    void controller(int cc, float v) override { if (cc == 1) modWheel = v; else if (cc == 128) aftertouch = v; }
    int activeVoices() const override;
    std::vector<Effect*> internalEffects() override { return { fxDrive.get(), fxChorus.get(), fxDelay.get(), fxReverb.get() }; }
    // a user wavetable for oscillator k (made from a dropped sound); shared, swapped by the app
    std::shared_ptr<Wavetable> userTable[3];
    std::string userTablePath[3];
    static std::vector<std::string> sourceNames();
    static std::vector<std::string> destNames();
    float lfoView[4] = { 0, 0, 0, 0 }, envView[4] = { 0, 0, 0, 0 };   // for the screen
    std::unique_ptr<Effect> fxDrive, fxChorus, fxDelay, fxReverb;
protected:
    void renderSub(float* L, float* R, int n, const TimeInfo& t) override;
private:
    struct Env { int stage = 0; float v = 0, rel0 = 0, prog = 0, start = 0; };      // 0 idle 1 att 2 dec 3 sus 4 rel
    struct Voice {
        bool on = false, releasing = false; int note = 0; float vel = 0, rnd = 0; long age = 0;
        double ph[3][MAXU] = {}; float pitch = 0, glideFrom = 0; float glideT = 1;
        Env env[4]; double lfoPh[4] = {}; float lfoHeld[4] = {}, lfoPrev[4] = {};
        SVF f1[2], f2[2], f1b[2], f2b[2], fa1[2], fb1[2], fa2[2], fb2[2]; Ladder l1[2], l2[2]; std::vector<float> comb[2][2]; int combW = 0;
        float modS[64] = {};        // smoothed modulation per destination
        float lastOsc = 0, pink = 0; double subPh = 0; bool firstBlock = true;
        float fmPrev[3] = { 0, 0, 0 };
    };
    std::array<Voice, MAXV> voices;
    long ageCounter = 0; float bend = 0, modWheel = 0, aftertouch = 0;
    double globalLfo[4] = { 0, 0, 0, 0 }; float lastGl[4] = { 0, 0, 0, 0 }, sharedHeld[4] = { 0, 0, 0, 0 };
    int lastPlayed = -1; bool fxTail = false; Rng glRng { 4242 };
    std::vector<int> monoStack;
    std::vector<float> vbL, vbR;
    void startVoice(Voice& v, int note, float vel, bool legato);
    float envTick(Env& e, int idx, float dt);
    float runFilter(Voice& v, int f, int c, int type, float x, float drive, int combLen, float res);
    void setFormant(SVF& a, SVF& b, float cutoff, float res);
    void postFx(float* L, float* R, int n, const TimeInfo& t);
public:
    enum Id { O1On, O1Tab, O1Pos, O1Oct, O1Semi, O1Fine, O1Lvl, O1Pan, O1Uni, O1Det, O1Spr, O1Warp, O1WAmt, O1Flt, O1Phase,
              O2On, O2Tab, O2Pos, O2Oct, O2Semi, O2Fine, O2Lvl, O2Pan, O2Uni, O2Det, O2Spr, O2Warp, O2WAmt, O2Flt, O2Phase,
              O3On, O3Tab, O3Pos, O3Oct, O3Semi, O3Fine, O3Lvl, O3Pan, O3Uni, O3Det, O3Spr, O3Warp, O3WAmt, O3Flt, O3Phase,
              SubLvl, SubOct, SubShape, NoiseLvl, NoiseColor, NoiseFlt,
              F1On, F1Type, F1Cut, F1Res, F1Drv, F1Key, F2On, F2Type, F2Cut, F2Res, F2Drv, F2Key, FRoute,
              E1A, E1D, E1S, E1R, E1C, E2A, E2D, E2S, E2R, E2C, E3A, E3D, E3S, E3R, E3C, E4A, E4D, E4S, E4R, E4C,
              L1Shape, L1Rate, L1Sync, L1Div, L1Mode, L2Shape, L2Rate, L2Sync, L2Div, L2Mode,
              L3Shape, L3Rate, L3Sync, L3Div, L3Mode, L4Shape, L4Rate, L4Sync, L4Div, L4Mode,
              M1, M2, M3, M4,
              Poly, VMode, Glide, VelSens, BendRange, Level, Pan,
              FxDrvOn, FxChoOn, FxDlyOn, FxRevOn,
              MS0 };       // then 16 slots x (src, dst, amount)
};

// ---------------------------------------------------------------- SAMPLER
class Sampler : public Instrument {
public:
    Sampler();
    void prepare(double s, int mb) override;
    void noteOn(int note, float vel) override;
    void noteOff(int note) override;
    void allNotesOff(bool hard) override;
    void pitchBend(float v) override { bend = v; }
    int activeVoices() const override;
    void setSample(std::shared_ptr<SampleData> s);      // message thread; swapped safely at the next block
    std::shared_ptr<SampleData> sample() const { return swap.load() ? std::atomic_load(&pending) : std::atomic_load(&cur); }
    std::atomic<float> playPos[4] { {-1}, {-1}, {-1}, {-1} };     // where voices play, 0..1 (for the screen)
protected:
    void renderSub(float* L, float* R, int n, const TimeInfo& t) override;
private:
    std::shared_ptr<SampleData> cur, pending; std::atomic<bool> swap { false };
    float p(const char* id) const { return find(id)->get(); }
    int pi(const char* id) const { return find(id)->geti(); }
    struct V { bool on = false, rel = false; int note = 0; float vel = 0; double pos = 0, inc = 0; int s0 = 0, s1 = 0; float env = 0; int stage = 0; long age = 0; bool loop = false; SVF f[2]; };
    std::array<V, 24> v; long age = 0; float bend = 0;
};

// ---------------------------------------------------------------- DRUMS: 16 pads (synthesised voices or your samples), notes 36-51
class Drums : public Instrument {
public:
    Drums();
    static constexpr int PADS = 16;
    void prepare(double s, int mb) override;
    void noteOn(int note, float vel) override;
    void noteOff(int) override {}
    void allNotesOff(bool hard) override;
    int activeVoices() const override;
    void setPadSample(int pad, std::shared_ptr<SampleData> s);
    std::shared_ptr<SampleData> padSample(int pad) const { return std::atomic_load(&samples[(size_t) pad]); }
    static std::vector<std::string> voiceNames();
    static std::string padName(int pad, int voice);
    std::atomic<float> hit[PADS];          // flashes the pad on the screen
protected:
    void renderSub(float* L, float* R, int n, const TimeInfo& t) override;
private:
    std::array<std::shared_ptr<SampleData>, PADS> samples;
    struct Hit { bool on = false; int pad = 0; float vel = 1; double t = 0; double ph[6] = {}; float env = 0; Rng rng; SVF f, f2; float z = 0; double spos = 0; };
    std::array<Hit, 32> hits; int nextHit = 0;
    float renderVoice(Hit& h, int voice, float tune, float decay, float tone, int kit);
    Param* master = nullptr;
};

std::unique_ptr<Instrument> makeInstrument(const std::string& type);       // "nova", "smp", "drm"
struct InstrumentInfo { const char* type; const char* name; const char* about; };
const std::vector<InstrumentInfo>& instrumentCatalog();

// NOVA factory sounds
struct NovaPreset { std::string name, group; std::vector<std::pair<std::string, float>> values; };
const std::vector<NovaPreset>& novaPresets();
void applyNovaPreset(Nova& n, const NovaPreset& p);
// DRUM kits
std::vector<std::string> drumKitNames();
void applyDrumKit(Drums& d, int kit);

} // namespace score
