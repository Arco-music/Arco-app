#pragma once
// ARCO audio engine: the station's gen~ modules wired exactly like the Max patch (b7_engine.py).
// Audio runs in sub-blocks of 64 samples. The message thread talks to it through a lock-free queue.
#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "GenModules.h"
#include "ExtraFx.h"
#include "StripMix.h"
#include "Synth2.h"
#include "Ambisonics.h"
#include "Clock.h"
#include "DrumMachine.h"
#include "Phrase.h"
#include "LoopGrid.h"

namespace arco {

constexpr int SUB = 64;
constexpr int NSPK = 32;
constexpr int NSTRIP = 12;

struct IModule {
    virtual ~IModule() = default;
    virtual int paramIndex(const std::string&) = 0;
    virtual void setp(int, double) = 0;
    virtual void bind(const std::string&, SharedBuffer&) = 0;
    virtual double* data(const std::string&) = 0;
};
template <typename M> struct Mod : IModule {
    M m;
    int paramIndex(const std::string& n) override { return M::paramIndex(n); }
    void setp(int i, double v) override { m.setp(i, v); }
    void bind(const std::string& n, SharedBuffer& b) override { m.bind(n, b); }
    double* data(const std::string& n) override { return m.data(n); }
};

// the C++ effects (ARCO 1.2) behind the same interface
struct XMod : IModule {
    ExtraFx fx;
    int paramIndex(const std::string& n) override { return fx.paramIndex(n); }
    void setp(int i, double v) override { fx.setp(i, v); }
    void bind(const std::string&, SharedBuffer&) override {}
    double* data(const std::string&) override { return nullptr; }
};

// the mixing desk (ARCO 1.2)
struct StripMixMod : IModule {
    StripMix mx;
    int paramIndex(const std::string& n) override { return mx.paramIndex(n); }
    void setp(int i, double v) override { mx.setp(i, v); }
    void bind(const std::string&, SharedBuffer&) override {}
    double* data(const std::string&) override { return nullptr; }
};
struct MasterMixMod : IModule {
    MasterMix mx;
    int paramIndex(const std::string& n) override { return mx.paramIndex(n); }
    void setp(int i, double v) override { mx.setp(i, v); }
    void bind(const std::string&, SharedBuffer&) override {}
    double* data(const std::string&) override { return nullptr; }
};

struct AmbiMod : IModule {
    Ambisonics amb;
    int paramIndex(const std::string& n) override { return amb.paramIndex(n); }
    void setp(int i, double v) override { amb.setp(i, v); }
    void bind(const std::string&, SharedBuffer&) override {}
    double* data(const std::string&) override { return nullptr; }
};

struct Synth2Mod : IModule {
    Synth2 syn;
    int paramIndex(const std::string& n) override { return syn.paramIndex(n); }
    void setp(int i, double v) override { syn.setParam(i, v); }
    void bind(const std::string&, SharedBuffer&) override {}
    double* data(const std::string&) override { return nullptr; }
};

// 2.0: modules that live as long as the engine (their patterns / takes survive a new sample rate)
template <typename T> struct RefMod : IModule {
    T* t = nullptr;
    int paramIndex(const std::string& n) override { return t->paramIndex(n); }
    void setp(int i, double v) override { t->setp(i, v); }
    void bind(const std::string&, SharedBuffer&) override {}
    double* data(const std::string&) override { return nullptr; }
};

// a single-producer single-consumer queue of parameter changes / commands
struct Cmd { int module; int index; double value; };
class CmdQueue {
public:
    bool push(const Cmd& c) { auto w = wr.load(std::memory_order_relaxed); auto n = (w + 1) % N;
        if (n == rd.load(std::memory_order_acquire)) return false; buf[w] = c; wr.store(n, std::memory_order_release); return true; }
    bool pop(Cmd& c) { auto r = rd.load(std::memory_order_relaxed); if (r == wr.load(std::memory_order_acquire)) return false;
        c = buf[r]; rd.store((r + 1) % N, std::memory_order_release); return true; }
private:
    static constexpr size_t N = 16384;
    std::array<Cmd, N> buf; std::atomic<size_t> rd { 0 }, wr { 0 };
};

// values the interface reads (last sample of a bus, or a decaying peak)
struct Snap { std::atomic<float> v { 0 }; };

// a ring of a signal for the little screens (every 4th sample)
struct Ring { static constexpr int N = 2048; std::array<float, N> d {}; std::atomic<int> w { 0 }; int dec = 0;
    void push(const float* x, int n) { int ww = w.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { if (++dec >= 4) { dec = 0; d[(size_t) ww] = x[i]; ww = (ww + 1) % N; } }
        w.store(ww, std::memory_order_release); } };

// 1.3: an instrument outside the engine (MODULAR, a sampled instrument, a plug-in): it plays with the SYNTH (adds into it)
struct ExtInstrument { virtual ~ExtInstrument() = default; virtual void render(float* L, float* R, int n, double sampleRate, double bpm, double beat) = 0;
    virtual void setInputs(const float* const* /*live4*/, const float* /*mixL*/, const float* /*mixR*/) {} };   // 2.3: what an IN module hears

// how long a track can be: 6 minutes on the Mac, 40 s on the iPad / iPhone (memory)
inline double trackSeconds() {
   #if JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX
    return 360.0;
   #else
    return 40.0;
   #endif
}
namespace px { struct Runtime; }
class Engine {
public:
    std::atomic<Synth2*> synthNow { nullptr };      // 2.4: the synth engine now (its LFOs move the knobs on the screen)
    Engine();
    void prepare(double sampleRate);
    // device callback: in = live inputs 1-4 (may be null), out = device outputs
    void process(const float* const* in, int numIn, float* const* out, int numOut, int numSamples);

    // ---- message thread
    void set(const std::string& bus, const std::string& name, double v);       // like "s st_looper" + "name v"
    int moduleId(const std::string& bus) const;
    SharedBuffer& buffer(const std::string& name);
    // swap in a new import buffer (prepared on the message thread)
    void setImport(std::shared_ptr<SharedBuffer> b);
    // routing
    std::atomic<int> inputChannel[4] { {0}, {1}, {2}, {3} };     // device input for live 1-4
    const float* phoneData[4] = { nullptr, nullptr, nullptr, nullptr };   // LIVE 1-4 from a phone (set by the host right before process)
    std::atomic<int> phonesFirst { 2 };                            // device output of headphones L (R = +1); -1 = none
    std::atomic<ExtInstrument*> ext { nullptr };                    // 1.3: the KEYBOARD's instrument (plays with the SYNTH)
    std::atomic<int> synthInto { 0 };                              // 0 off, 1 = LIVE 2, 2 = LIVE 3, 3 = LIVE 4
    std::atomic<px::Runtime*> patcher { nullptr };                 // 2.4: PATCHER - your own program, every block
    std::atomic<int> patcherInto { 0 };                            // 0 its own (straight into the mix), 1-3 = LIVE 2-4
    std::atomic<float> patcherGain { 1.0f };
    std::atomic<int> drumsInto { 0 };                              // 2.2: the DRUM MACHINE through a LIVE strip too (so a track records it)
    float dmDirect = 1.0f;                                         // its direct sound (fades out while it plays through a strip)
    std::atomic<float> grid[14][NSPK];                             // direct outs: source x speaker
    // ---- what the interface reads
    Snap& snap(const std::string& bus);
    float peak(const std::string& bus);       // peak since last call
    Ring& ring(const std::string& bus);
    std::atomic<float> cpu { 0 };
    // 2.0: the clock everything in tempo follows, the DRUM MACHINE, PHRASE
    Clock clock;
    DrumMachine drumMachine;
    Phrase phrase;
    LoopGrid loopGrid;
    std::atomic<int> selStrip { 0 };
    StripMix* stripMix = nullptr;             // for the meters (gain reduction)
    MasterMix* masterMix = nullptr;
    Ambisonics* ambi = nullptr;               // 1.3: strips placed around the audience
    double* ambGate = nullptr;                // (the channel strips: which ones leave the stereo mix)
    double sr = 48000.0;
    // show recording: called with the main mix (L, R) of every sub-block (audio thread)
    std::function<void(const float*, const float*, int)> recorder;
    // 2.0: the station's main mix (stereo, before the speakers), for STUDIO to record - the device outputs are not it
    // (with more speakers, or headphones on outputs 1-2, they carry other things: the click, the CUE)
    int pullMainMix(float* l, float* r, int n) {
        int got = 0;
        while (got < n && tapRd != tapWr) { l[got] = tapL[tapRd]; r[got] = tapR[tapRd]; tapRd = (tapRd + 1) % TAPN; ++got; }
        for (int i = got; i < n; ++i) { l[i] = 0; r[i] = 0; }
        return got; }

private:
    std::vector<std::unique_ptr<IModule>> mods;
    std::map<std::string, int> busToMod;                // message bus -> module
    std::vector<int> modOrder;
    CmdQueue q;
    std::map<std::string, SharedBuffer> bufs;
    std::shared_ptr<SharedBuffer> importBuf, importPending;
    std::atomic<bool> importSwap { false };

    // the signal buses
    std::map<std::string, std::vector<float>> bus;
    float* B(const std::string& n);
    std::map<std::string, Snap> snaps;
    std::map<std::string, std::atomic<float>> peaks;
    std::map<std::string, Ring> rings;
    std::vector<std::pair<float*, Snap*>> snapList;
    std::vector<std::pair<float*, std::atomic<float>*>> peakList;
    std::vector<std::pair<float*, Ring*>> ringList;

    struct Node { IModule* mod; std::function<void()> run; };
    std::vector<std::function<void()>> graph;
    // any buffer size: when a block is not a multiple of 64 the engine runs one 64-sample block behind (1.3 ms)
    static constexpr int MAXIO = 64;
    bool fifoMode = false;
    int accN = 0, outQ = 0;
    std::vector<float> accIn[MAXIO], accPhone[4], qOut[MAXIO];
    void processFifo(const float* const* in, int numIn, float* const* out, int numOut, int numSamples);
    float tuneMix = 0, synG[3] = { 0, 0, 0 }, dmG[3] = { 0, 0, 0 };
    float patG[3] = { 0, 0, 0 }, patDirect = 1.0f;
    std::vector<float> dmPrev;                                     // the drum machine's last block, mono (a LIVE strip takes it)
    std::vector<float> beatPrev;
    double tuneAmt = 0;
    void runSub(const float* const* in, int numIn, float* const* out, int numOut, int off, int n);
    static constexpr int TAPN = 16384;
    std::array<float, TAPN> tapL {}, tapR {}; int tapRd = 0, tapWr = 0;
};

} // namespace arco
