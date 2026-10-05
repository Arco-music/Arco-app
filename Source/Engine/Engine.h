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

class Engine {
public:
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
    std::atomic<int> phoneLive { 0 };                              // 1-4: that live input plays the phone microphone
    const float* phoneData = nullptr;                              // set by the host right before process (audio thread)
    std::atomic<int> phonesFirst { 2 };                            // device output of headphones L (R = +1); -1 = none
    std::atomic<int> synthInto { 0 };                              // 0 off, 1 = LIVE 2, 2 = LIVE 3, 3 = LIVE 4
    std::atomic<float> grid[14][NSPK];                             // direct outs: source x speaker
    // ---- what the interface reads
    Snap& snap(const std::string& bus);
    float peak(const std::string& bus);       // peak since last call
    Ring& ring(const std::string& bus);
    std::atomic<float> cpu { 0 };
    std::atomic<int> selStrip { 0 };
    double sr = 48000.0;
    // show recording: called with the main mix (L, R) of every sub-block (audio thread)
    std::function<void(const float*, const float*, int)> recorder;

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
    std::vector<float> accIn[MAXIO], accPhone, qOut[MAXIO];
    void processFifo(const float* const* in, int numIn, float* const* out, int numOut, int numSamples);
    float tuneMix = 0, synG[3] = { 0, 0, 0 };
    std::vector<float> beatPrev;
    double tuneAmt = 0;
    void runSub(const float* const* in, int numIn, float* const* out, int numOut, int off, int n);
};

} // namespace arco
