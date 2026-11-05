#pragma once
// ARCO 1.3: STUDIO's devices - the effects that sit on a lane (and on the returns and the master), in a chain.
// Each device has up to 12 parameters (real units), all automatable. Stereo in, stereo out, any block size.
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>
#include "../Engine/StripMix.h"
#include "PluginSlot.h"

namespace arco {

enum DevType { DevEQ = 0, DevComp, DevSat, DevDelay, DevReverb, DevChorus, DevGate, DevUtility, DevLimiter, DevFilter, DevCount };

struct DevParam { const char* name; float lo, hi, def; const char* unit; bool log = false; const char* const* choices = nullptr; int nChoices = 0; };
struct DevInfo { const char* name; const char* desc; std::vector<DevParam> p; };

inline const std::vector<DevInfo>& devInfo() {
    static const char* DIV[8] = { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1/1" };
    static const char* FT[3] = { "LOW", "BAND", "HIGH" };
    static const std::vector<DevInfo> d = {
        { "EQ", "high-pass, low shelf, 3 bells, high shelf, low-pass", {
            { "HIGH-PASS", 0, 400, 0, "Hz" }, { "LOW", -15, 15, 0, "dB" }, { "LOW FREQ", 30, 500, 110, "Hz", true },
            { "BELL 1", -15, 15, 0, "dB" }, { "B1 FREQ", 40, 16000, 300, "Hz", true }, { "B1 Q", 0.2f, 8, 1, "" },
            { "BELL 2", -15, 15, 0, "dB" }, { "B2 FREQ", 40, 16000, 1500, "Hz", true }, { "B2 Q", 0.2f, 8, 1, "" },
            { "HIGH", -15, 15, 0, "dB" }, { "HIGH FREQ", 1500, 18000, 8000, "Hz", true }, { "LOW-PASS", 1000, 22000, 22000, "Hz", true } } },
        { "COMPRESSOR", "evens the level", {
            { "THRESHOLD", -50, 0, -18, "dB" }, { "RATIO", 1, 20, 3, ":1", true }, { "ATTACK", 0.1f, 100, 10, "ms", true }, { "RELEASE", 10, 1000, 120, "ms", true },
            { "MAKE-UP", 0, 24, 0, "dB" }, { "KNEE", 0, 18, 6, "dB" }, { "MIX", 0, 100, 100, "%" } } },
        { "SATURATION", "warmth to drive", { { "DRIVE", 0, 100, 25, "%" }, { "TONE", 0, 100, 50, "%" }, { "MIX", 0, 100, 100, "%" }, { "OUTPUT", -24, 6, 0, "dB" } } },
        { "DELAY", "echoes in tempo", { { "TIME", 0, 7, 3, "", false, DIV, 8 }, { "FEEDBACK", 0, 95, 35, "%" }, { "TONE", 0, 100, 55, "%" }, { "PING-PONG", 0, 100, 0, "%" }, { "MIX", 0, 100, 100, "%" } } },
        { "REVERB", "a room, a hall", { { "SIZE", 0, 100, 55, "%" }, { "DECAY", 0.2f, 20, 2.2f, "s", true }, { "DAMPING", 0, 100, 45, "%" }, { "PRE-DELAY", 0, 120, 15, "ms" },
                                        { "WIDTH", 0, 100, 100, "%" }, { "MIX", 0, 100, 100, "%" } } },
        { "CHORUS", "width and movement", { { "RATE", 0.05f, 5, 0.6f, "Hz", true }, { "DEPTH", 0, 100, 40, "%" }, { "MIX", 0, 100, 50, "%" } } },
        { "GATE", "silence between notes", { { "THRESHOLD", -80, 0, -45, "dB" }, { "ATTACK", 0.1f, 50, 1, "ms", true }, { "HOLD", 0, 500, 40, "ms" }, { "RELEASE", 5, 1000, 120, "ms", true }, { "RANGE", -80, 0, -60, "dB" } } },
        { "UTILITY", "gain, width, mono", { { "GAIN", -40, 24, 0, "dB" }, { "WIDTH", 0, 200, 100, "%" }, { "MONO BASS", 0, 300, 0, "Hz" }, { "PHASE L", 0, 1, 0, "" } } },
        { "LIMITER", "a ceiling nothing passes", { { "GAIN", 0, 24, 0, "dB" }, { "CEILING", -12, 0, -1, "dB" }, { "RELEASE", 10, 1000, 100, "ms", true } } },
        { "FILTER", "resonant filter", { { "TYPE", 0, 2, 0, "", false, FT, 3 }, { "CUTOFF", 20, 20000, 2000, "Hz", true }, { "RESO", 0, 100, 20, "%" }, { "DRIVE", 0, 100, 0, "%" } } },
    };
    return d;
}

struct StudioDevice {
    int type = DevEQ;
    bool on = true;
    std::array<float, 12> p {};
    int side = -1;                          // 2.3: SIDECHAIN - the lane whose sound drives this compressor / gate (-1: its own)
    int fx = -1;                            // 2.3: DevStation - which of the station's effects
    // 2.3: DevPlugin - an AU / VST3: what it is (to open it again), its settings when saved, the running one
    juce::String plugDesc, plugState, plugName, uid;
    PluginSlotPtr slot;
    static StudioDevice make(int type) { StudioDevice d; d.type = type; auto& I = devInfo()[(size_t) type]; for (size_t i = 0; i < I.p.size(); ++i) d.p[i] = I.p[i].def; return d; }
    static StudioDevice makeStation(int which);
};

// 2.3: the station's own effects as studio devices (StationFx.h, run by Studio.cpp)
constexpr int DevStation = 100;
constexpr int DevPlugin = 101;
struct StationRunBase {
    virtual ~StationRunBase() = default;
    int fx = -1;
    virtual void process(const StudioDevice& d, float* L, float* R, int n, double bpm, double beat) = 0;
};
std::unique_ptr<StationRunBase> makeStationRun(int which, double sampleRate);
int stationFxCount();
const DevInfo& stationFxInfo(int which);
int stationFxGroup(int which);              // 0 inserts, 1 send effects, 2 the newer effects
inline const DevInfo& devInfoOf(const StudioDevice& d) {
    static const DevInfo plug { "PLUG-IN", "its own window: OPEN", {} };
    if (d.type == DevPlugin) return plug;
    return d.type == DevStation ? stationFxInfo(d.fx) : devInfo()[(size_t) std::max(0, std::min((int) DevCount - 1, d.type))]; }
inline StudioDevice StudioDevice::makeStation(int which) { StudioDevice d; d.type = DevStation; d.fx = which; auto& I = stationFxInfo(which); for (size_t i = 0; i < I.p.size() && i < 12; ++i) d.p[i] = I.p[i].def; return d; }

// ---------------------------------------------------------------- the running device (audio thread)
class DeviceDsp {
public:
    void prepare(double sampleRate) { sr = sampleRate; reset(); }
    // 2.3: for this device (a station effect gets its own copy of the station's DSP)
    void prepareFor(const StudioDevice& d, double sampleRate) { prepare(sampleRate); st = d.type == DevStation ? makeStationRun(d.fx, sampleRate) : nullptr; key = keyOf(d); }
    static int keyOf(const StudioDevice& d) { return d.type * 1000 + (d.type == DevStation ? d.fx : 0); }
    int key = -1;
    double beatPos = 0;                     // where the song is (beats), for the station's effects in tempo
    std::unique_ptr<StationRunBase> st;
    void reset() {
        for (auto& c : eq) for (auto& b : c) b.reset();
        env = 0; gSm = 1; gateG = 0; holdLeft = 0;
        dl[0].assign((size_t) (sr * 4.2), 0.0f); dl[1].assign((size_t) (sr * 4.2), 0.0f); dw = 0; dlp[0] = dlp[1] = 0;
        for (int i = 0; i < 8; ++i) { rv[i].assign((size_t) (sr * 0.2) + 8, 0.0f); rvw[i] = 0; rvlp[i] = 0; }
        pre.assign((size_t) (sr * 0.13) + 8, 0.0f); prew = 0;
        ch[0].assign((size_t) (sr * 0.05), 0.0f); ch[1].assign((size_t) (sr * 0.05), 0.0f); chw = 0; chPh = 0;
        limG = 1; svf[0] = svf[1] = {}; satLp[0] = satLp[1] = 0; mbLp[0] = mbLp[1] = 0;
        lastKey = {}; designed = false;
    }
    // L, R in place; bpm for the delay
    void process(const StudioDevice& d, float* L, float* R, int n, double bpm) {
        if (!d.on) return;
        switch (d.type) {
            case DevEQ: eqProc(d, L, R, n); break;
            case DevComp: compProc(d, L, R, n); break;
            case DevSat: satProc(d, L, R, n); break;
            case DevDelay: delayProc(d, L, R, n, bpm); break;
            case DevReverb: reverbProc(d, L, R, n); break;
            case DevChorus: chorusProc(d, L, R, n); break;
            case DevGate: gateProc(d, L, R, n); break;
            case DevUtility: utilProc(d, L, R, n); break;
            case DevLimiter: limProc(d, L, R, n); break;
            case DevFilter: filterProc(d, L, R, n); break;
            case DevStation: if (st && st->fx == d.fx) st->process(d, L, R, n, bpm, beatPos); break;
            case DevPlugin: if (d.slot) d.slot->process(L, R, n); break;
            default: break;
        }
        for (int i = 0; i < n; ++i) if (!std::isfinite(L[i]) || !std::isfinite(R[i])) { reset(); std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); return; }
    }
    float grDb = 0;                         // for the meters (compressor, limiter, gate)
    const float* keyL = nullptr; const float* keyR = nullptr;   // 2.3: the sidechain's sound for this block (null: its own)
private:
    double sr = 48000;
    // EQ
    Biquad eq[2][8]; std::array<float, 12> lastKey {}; bool designed = false; bool use[8] {};
    void eqProc(const StudioDevice& d, float* L, float* R, int n) {
        if (!designed || d.p != lastKey) {
            lastKey = d.p; designed = true;
            for (int c = 0; c < 2; ++c) {
                use[0] = use[1] = d.p[0] >= 15; if (use[0]) { eq[c][0].highpass(sr, d.p[0], 0.5412); eq[c][1].highpass(sr, d.p[0], 1.3066); }
                use[2] = std::abs(d.p[1]) > 0.05f; if (use[2]) eq[c][2].lowShelf(sr, d.p[2], d.p[1]);
                use[3] = std::abs(d.p[3]) > 0.05f; if (use[3]) eq[c][3].peak(sr, d.p[4], d.p[5], d.p[3]);
                use[4] = std::abs(d.p[6]) > 0.05f; if (use[4]) eq[c][4].peak(sr, d.p[7], d.p[8], d.p[6]);
                use[5] = std::abs(d.p[9]) > 0.05f; if (use[5]) eq[c][5].highShelf(sr, d.p[10], d.p[9]);
                use[6] = d.p[11] < std::min(20000.0, sr * 0.45); if (use[6]) lowpass(eq[c][6], d.p[11]);
            }
        }
        float* X[2] = { L, R };
        for (int c = 0; c < 2; ++c) for (int f = 0; f < 7; ++f) if (use[f]) { auto& b = eq[c][f]; for (int i = 0; i < n; ++i) X[c][i] = b.tick(X[c][i]); }
    }
    void lowpass(Biquad& b, double f) { double w = 2 * M_PI * std::min(f, sr * 0.45) / sr, c = std::cos(w), al = std::sin(w) / (2 * 0.7071); b.set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    // COMPRESSOR (stereo-linked, soft knee, parallel mix)
    float env = 0, gSm = 1;
    void compProc(const StudioDevice& d, float* L, float* R, int n) {
        float thr = d.p[0], ratio = std::max(1.0f, d.p[1]), knee = d.p[5], mix = d.p[6] / 100.0f, mk = std::pow(10.0f, d.p[4] / 20.0f);
        float att = 1.0f - std::exp(-1.0f / (float) (std::max(0.1f, d.p[2]) * 0.001 * sr)), rel = 1.0f - std::exp(-1.0f / (float) (std::max(5.0f, d.p[3]) * 0.001 * sr));
        float maxRed = 0;
        for (int i = 0; i < n; ++i) {
            float a = keyL ? std::max(std::abs(keyL[i]), std::abs(keyR[i])) : std::max(std::abs(L[i]), std::abs(R[i]));
            env += (a > env ? att : rel) * (a - env);
            float over = 20.0f * std::log10(std::max(env, 1e-6f)) - thr, red = 0;
            if (over > knee * 0.5f) red = over * (1 - 1 / ratio);
            else if (knee > 0 && over > -knee * 0.5f) { float t = over + knee * 0.5f; red = (1 - 1 / ratio) * t * t / (2 * knee); }
            float g = std::pow(10.0f, -red / 20.0f);
            gSm += 0.05f * (g - gSm);
            maxRed = std::max(maxRed, red);
            float wl = L[i] * gSm * mk, wr = R[i] * gSm * mk;
            L[i] = L[i] * (1 - mix) + wl * mix; R[i] = R[i] * (1 - mix) + wr * mix;
        }
        grDb = maxRed;
    }
    // SATURATION
    float satLp[2] {};
    void satProc(const StudioDevice& d, float* L, float* R, int n) {
        float drive = 1.0f + d.p[0] / 100.0f * 12.0f, tone = d.p[1] / 100.0f, mix = d.p[2] / 100.0f, out = std::pow(10.0f, d.p[3] / 20.0f);
        float a = 0.05f + tone * tone * 0.93f, norm = 1.0f / std::tanh(drive);
        float* X[2] = { L, R };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) {
            float x = X[c][i];
            float y = std::tanh(x * drive + 0.1f * x * x * drive) * norm * 0.8f;   // a touch of even harmonics
            satLp[c] += a * (y - satLp[c]);
            X[c][i] = (x * (1 - mix) + satLp[c] * mix) * out;
        }
    }
    // DELAY (tempo divisions, ping-pong)
    std::vector<float> dl[2]; int dw = 0; float dlp[2] {};
    void delayProc(const StudioDevice& d, float* L, float* R, int n, double bpm) {
        static const double B[8] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
        double beats = B[std::clamp((int) std::lround(d.p[0]), 0, 7)];
        int N = (int) dl[0].size();
        double ds = std::clamp(60.0 / std::max(20.0, bpm) * beats * sr, 16.0, (double) N - 4);
        float fb = d.p[1] / 100.0f, pp = d.p[3] / 100.0f, mix = d.p[4] / 100.0f, a = 0.05f + std::pow(d.p[2] / 100.0f, 2.0f) * 0.9f;
        for (int i = 0; i < n; ++i) {
            int rp = (int) (dw - ds); while (rp < 0) rp += N;
            float yl = dl[0][(size_t) rp], yr = dl[1][(size_t) rp];
            dlp[0] += a * (yl - dlp[0]); dlp[1] += a * (yr - dlp[1]);
            float inL = L[i], inR = R[i], mono = 0.5f * (inL + inR);
            // ping-pong: the input enters on the left, each echo crosses over
            float wl = (inL * (1 - pp) + mono * pp) + (dlp[0] * (1 - pp) + dlp[1] * pp) * fb;
            float wr = (inR * (1 - pp)) + (dlp[1] * (1 - pp) + dlp[0] * pp) * fb;
            dl[0][(size_t) dw] = std::tanh(wl); dl[1][(size_t) dw] = std::tanh(wr);
            dw = (dw + 1) % N;
            L[i] = inL * (1 - mix) + yl * mix; R[i] = inR * (1 - mix) + yr * mix;
        }
    }
    // REVERB: 8-line feedback delay network, pre-delay, damping, width
    std::vector<float> rv[8]; int rvw[8] {}; float rvlp[8] {}; std::vector<float> pre; int prew = 0;
    void reverbProc(const StudioDevice& d, float* L, float* R, int n) {
        static const double LEN[8] = { 0.0297, 0.0371, 0.0411, 0.0437, 0.0503, 0.0571, 0.0613, 0.0683 };
        float size = 0.35f + d.p[0] / 100.0f * 1.6f, decay = std::max(0.2f, d.p[1]), damp = d.p[2] / 100.0f, width = d.p[4] / 100.0f, mix = d.p[5] / 100.0f;
        int pd = std::clamp((int) (d.p[3] * 0.001 * sr), 0, (int) pre.size() - 2);
        float g[8]; int len[8];
        for (int k = 0; k < 8; ++k) { len[k] = std::clamp((int) (LEN[k] * size * sr), 8, (int) rv[k].size() - 2); g[k] = std::pow(10.0f, -3.0f * (float) len[k] / (float) sr / decay); }
        float a = 1.0f - damp * 0.85f;
        for (int i = 0; i < n; ++i) {
            float in = 0.5f * (L[i] + R[i]);
            pre[(size_t) prew] = in; int rp = prew - pd; if (rp < 0) rp += (int) pre.size(); float x = pre[(size_t) rp]; prew = (prew + 1) % (int) pre.size();
            float o[8];
            for (int k = 0; k < 8; ++k) { int p = rvw[k] - len[k]; if (p < 0) p += (int) rv[k].size(); o[k] = rv[k][(size_t) p]; rvlp[k] += a * (o[k] - rvlp[k]); o[k] = rvlp[k] * g[k]; }
            // Hadamard-like mix (orthogonal): keeps the energy
            float s = 0; for (int k = 0; k < 8; ++k) s += o[k]; s *= 0.25f;
            for (int k = 0; k < 8; ++k) { rv[k][(size_t) rvw[k]] = (s - o[k]) + x * 0.35f; rvw[k] = (rvw[k] + 1) % (int) rv[k].size(); }
            float wl = (o[0] + o[2] + o[4] + o[6]) * 0.5f, wr = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
            float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
            L[i] = L[i] * (1 - mix) + (m + sd) * mix; R[i] = R[i] * (1 - mix) + (m - sd) * mix;
        }
    }
    // CHORUS
    std::vector<float> ch[2]; int chw = 0; double chPh = 0;
    void chorusProc(const StudioDevice& d, float* L, float* R, int n) {
        float rate = d.p[0], depth = d.p[1] / 100.0f, mix = d.p[2] / 100.0f; int N = (int) ch[0].size();
        for (int i = 0; i < n; ++i) {
            chPh += rate / sr; if (chPh >= 1) chPh -= 1;
            ch[0][(size_t) chw] = L[i]; ch[1][(size_t) chw] = R[i];
            float out[2];
            for (int c = 0; c < 2; ++c) {
                double ms = 7.0 + 5.0 * depth * std::sin(2 * M_PI * (chPh + c * 0.25));
                double rp = chw - ms * 0.001 * sr; while (rp < 0) rp += N;
                int i0 = (int) rp; float f = (float) (rp - i0); int i1 = (i0 + 1) % N;
                out[c] = ch[c][(size_t) i0] + (ch[c][(size_t) i1] - ch[c][(size_t) i0]) * f;
            }
            chw = (chw + 1) % N;
            L[i] = L[i] * (1 - mix * 0.5f) + out[0] * mix; R[i] = R[i] * (1 - mix * 0.5f) + out[1] * mix;
        }
    }
    // GATE
    float gateG = 0; int holdLeft = 0;
    void gateProc(const StudioDevice& d, float* L, float* R, int n) {
        float thr = std::pow(10.0f, d.p[0] / 20.0f), range = std::pow(10.0f, d.p[4] / 20.0f);
        float att = 1.0f - std::exp(-1.0f / (float) (std::max(0.1f, d.p[1]) * 0.001 * sr)), rel = 1.0f - std::exp(-1.0f / (float) (std::max(5.0f, d.p[3]) * 0.001 * sr));
        int hold = (int) (d.p[2] * 0.001 * sr);
        for (int i = 0; i < n; ++i) {
            float a = keyL ? std::max(std::abs(keyL[i]), std::abs(keyR[i])) : std::max(std::abs(L[i]), std::abs(R[i]));
            env += (a > env ? 0.2f : 0.002f) * (a - env);
            bool open = env > thr; if (open) holdLeft = hold; else if (holdLeft > 0) { --holdLeft; open = true; }
            float tgt = open ? 1.0f : range;
            gateG += (tgt > gateG ? att : rel) * (tgt - gateG);
            L[i] *= gateG; R[i] *= gateG;
        }
        grDb = -20.0f * std::log10(std::max(gateG, 1e-5f));
    }
    // UTILITY
    float mbLp[2] {};
    void utilProc(const StudioDevice& d, float* L, float* R, int n) {
        float g = std::pow(10.0f, d.p[0] / 20.0f), w = d.p[1] / 100.0f, mb = d.p[2]; bool inv = d.p[3] > 0.5f;
        float a = mb > 10 ? 1.0f - std::exp(-2.0f * (float) M_PI * mb / (float) sr) : 0.0f;
        for (int i = 0; i < n; ++i) {
            float l = L[i] * (inv ? -1.0f : 1.0f), r = R[i];
            float m = 0.5f * (l + r), s = 0.5f * (l - r);
            if (a > 0) { mbLp[0] += a * (s - mbLp[0]); s -= mbLp[0]; }      // no stereo below the frequency (a centred bass)
            s *= w;
            L[i] = (m + s) * g; R[i] = (m - s) * g;
        }
    }
    // LIMITER (instant attack on a 1 ms smoothed peak; nothing above the ceiling)
    float limG = 1;
    void limProc(const StudioDevice& d, float* L, float* R, int n) {
        float pre = std::pow(10.0f, d.p[0] / 20.0f), ceil = std::pow(10.0f, d.p[1] / 20.0f);
        float rel = 1.0f - std::exp(-1.0f / (float) (std::max(10.0f, d.p[2]) * 0.001 * sr));
        float minG = 1;
        for (int i = 0; i < n; ++i) {
            float l = L[i] * pre, r = R[i] * pre;
            float pk = std::max(std::abs(l), std::abs(r));
            float need = pk > ceil ? ceil / pk : 1.0f;
            if (need < limG) limG = need; else limG += rel * (1.0f - limG);
            minG = std::min(minG, limG);
            L[i] = juce::jlimit(-ceil, ceil, l * limG); R[i] = juce::jlimit(-ceil, ceil, r * limG);
        }
        grDb = -20.0f * std::log10(std::max(minG, 1e-5f));
    }
    // FILTER (TPT state variable)
    struct Svf { float a = 0, b = 0; } svf[2];
    void filterProc(const StudioDevice& d, float* L, float* R, int n) {
        int type = std::clamp((int) std::lround(d.p[0]), 0, 2);
        float g = std::tan((float) M_PI * std::min(d.p[1], (float) (sr * 0.45)) / (float) sr), k = 2.0f - 1.95f * d.p[2] / 100.0f, drv = 1.0f + d.p[3] / 100.0f * 6.0f;
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float* X[2] = { L, R };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) {
            float x = drv > 1.01f ? std::tanh(X[c][i] * drv) / std::tanh(drv) : X[c][i];
            auto& s = svf[c];
            float v3 = x - s.b, v1 = a1 * s.a + a2 * v3, v2 = s.b + a2 * s.a + a3 * v3;
            s.a = 2 * v1 - s.a; s.b = 2 * v2 - s.b;
            X[c][i] = type == 0 ? v2 : (type == 1 ? v1 * k : x - k * v1 - v2);
        }
    }
};

} // namespace arco
