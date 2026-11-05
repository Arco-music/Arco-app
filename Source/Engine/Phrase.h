#pragma once
// ARCO 2.0: PHRASE - play a short phrase (four notes, say) into it from a LIVE input; it cuts the phrase into its
// notes and plays them back as a sequence in tempo, which you then shape while it runs:
//   RATE (how fast the notes come, locked to the metronome), ORDER (forward, backward, ping-pong, spiral, skip, random),
//   GATE, PITCH and OCTAVES (each pass a step higher), REVERSE (some notes backwards), SWING, a FILTER that
//   opens with every note, SPREAD (left / right), SPACE - and BUILD: over 1-16 bars the notes get faster and louder,
//   the filter opens and the space grows (a crescendo), then it drops back.
// Recording starts on the next downbeat and lasts 1, 2 or 4 bars (or half a bar). Every note is a smooth grain:
// no clicks whatever the knobs do, and the pitch is resampled with a 4-point interpolator behind an anti-alias filter.
#include <atomic>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <string>

namespace arco {

class Phrase {
public:
    static constexpr int MAXSL = 16, NG = 6;
    struct P { float on = 1, src = 1, len = 1, rate = 3, order = 0, gate = 85, pitch = 0, oct = 0, rev = 0, swing = 50,
               cut = 100, res = 15, env = 30, spread = 30, space = 20, vol = 80, build = 60, blen = 4, bpb = 4, steps = 0,
               rec = 0, play = 0, clear = 0, buildGo = 0; } p;
    std::vector<std::string> names; std::vector<float*> slot;
    int paramIndex(const std::string& n) const { for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int) i; return -1; }
    void setp(int i, double v) { if (i >= 0 && i < (int) slot.size()) *slot[(size_t) i] = (float) v; }
    static std::vector<std::string> paramNames() {
        return { "phr_on", "phr_src", "phr_len", "phr_rate", "phr_order", "phr_gate", "phr_pitch", "phr_oct", "phr_rev", "phr_swing",
                 "phr_cut", "phr_res", "phr_env", "phr_spread", "phr_space", "phr_vol", "phr_build", "phr_blen", "beats", "phr_steps",
                 "phr_rec", "phr_play", "phr_clear", "phr_bgo" }; }
    Phrase() {
        names = paramNames();
        for (auto* x : { &p.on, &p.src, &p.len, &p.rate, &p.order, &p.gate, &p.pitch, &p.oct, &p.rev, &p.swing, &p.cut, &p.res, &p.env,
                         &p.spread, &p.space, &p.vol, &p.build, &p.blen, &p.bpb, &p.steps, &p.rec, &p.play, &p.clear, &p.buildGo }) slot.push_back(x);
    }
    void init(double rate) {
        sr = rate; buf.assign((size_t) (sr * 18.0), 0.0f); envF.assign(buf.size() / HOP + 2, 0.0f);
        state = Idle; nSl = 0; for (auto& g : gr) g.on = false; lastRec = p.rec; lastClear = p.clear; lastBuild = p.buildGo; lastPlay = p.play; playing = false;
        z1 = z2 = 0; z1r = z2r = 0; cutS = 1; volS = 0;
    }
    bool wantsClock() const { return playing || state == Waiting || state == Recording; }

    // ---- what the interface shows
    std::atomic<int> stateOut { 0 };                 // 0 empty, 1 waiting for the downbeat, 2 recording, 3 ready / playing
    std::atomic<int> slicesOut { 0 }, stepOut { -1 }, sliceOut { -1 };
    std::atomic<float> recProgress { 0 }, buildOut { 0 }, levelOut { 0 };
    std::array<std::atomic<float>, MAXSL + 1> sliceAt {};     // where each note starts (0..1 of the phrase), for the picture
    std::array<std::atomic<float>, 256> wave {};              // the recorded phrase, drawn
    std::atomic<bool> isPlaying { false };

    void process(const float* const* live, const float* beat, const float* bpm, const float* run, float* L, float* R, float* spc, int n) {
        // buttons (their values change at every press)
        if (p.rec != lastRec) { lastRec = p.rec; armRecord(); }
        if (p.clear != lastClear) { lastClear = p.clear; state = Idle; nSl = 0; playing = false; for (auto& g : gr) g.on = false; }
        if (p.play != lastPlay) { lastPlay = p.play; if (nSl > 0) { playing = !playing; if (playing) restartAt = true; } }
        if (p.buildGo != lastBuild) { lastBuild = p.buildGo; building = !building || buildPos >= 1.0; buildPos = 0; }
        int src = std::clamp((int) p.src, 1, 4) - 1;
        const float* in = live ? live[src] : nullptr;
        float lvl = 0;
        for (int i = 0; i < n; ++i) {
            double b = beat ? beat[i] : 0.0, t = bpm && bpm[i] > 20 ? bpm[i] : 120.0;
            double bpb = std::clamp((double) p.bpb, 1.0, 16.0);
            // ---- recording, from the downbeat
            if (state == Waiting && std::floor(b / bpb) != std::floor(lastB / bpb)) { state = Recording; recN = 0; recBeats = recLength(bpb); recStart = std::floor(b / bpb) * bpb; }
            if (state == Recording) {
                float x = in ? in[i] : 0.0f;
                if (recN < (long) buf.size()) { buf[(size_t) recN] = x; envAcc = std::max(envAcc, std::abs(x)); if (++envCnt >= HOP) { envF[(size_t) (recN / HOP)] = envAcc; envAcc = 0; envCnt = 0; } ++recN; }
                double done = (b - recStart) / recBeats;
                recProgress.store((float) std::clamp(done, 0.0, 1.0));
                if (done >= 1.0 || recN >= (long) buf.size()) { finishRecording(); playing = true; restartAt = true; }
            }
            lastB = b;
            // ---- the sequence
            float l = 0, r = 0, s = 0;
            if (playing && nSl > 0 && p.on > 0.5f) {
                // BUILD: 0 -> 1 over its bars, then it drops back
                if (building) { buildPos += t / 60.0 / sr / (std::clamp((double) p.blen, 1.0, 16.0) * bpb); if (buildPos >= 1.0) { building = false; buildPos = 0; } }
                float bAmt = building ? (float) buildPos * p.build / 100.0f : 0.0f;
                buildOut.store(building ? (float) buildPos : 0.0f);
                int rateIdx = std::clamp((int) p.rate + (bAmt > 0.66f ? 2 : (bAmt > 0.33f ? 1 : 0)), 0, 6);
                double stepL = RATE[rateIdx];
                if (restartAt) { idx = -1; pass = 0; restartAt = false; lastK = -1e12; }
                double sw = (std::clamp(p.swing, 50.0f, 75.0f) - 50.0f) / 50.0f * stepL;      // the off-beats later
                double k = std::floor(b / stepL);
                double due = k * stepL + (((long long) k) % 2 != 0 ? sw : 0.0);
                if (k != lastK && b >= due) { lastK = k; stepNow(b, stepL, t, bAmt); }
                // the grains
                for (auto& g : gr) if (g.on) grain(g, l, r);
                // the filter: one TPT state-variable low-pass per side, its cutoff smoothed (no zipper noise)
                float target = std::clamp((p.cut / 100.0f) + bAmt * 0.35f + envNow * p.env / 100.0f, 0.02f, 1.0f);
                cutS += 0.0015f * (target - cutS); envNow *= envK;
                float fc = 40.0f * std::pow(480.0f, cutS), g = (float) std::tan(3.14159265 * std::min((double) fc, sr * 0.45) / sr);
                float kq = 2.0f - 1.9f * p.res / 100.0f;
                l = svf(l, g, kq, z1, z2); r = svf(r, g, kq, z1r, z2r);
                float vt = std::pow(p.vol / 100.0f, 2.0f) * 1.6f * (bAmt > 0 ? std::pow(10.0f, (-9.0f + 9.0f * (float) buildPos) * p.build / 100.0f / 20.0f) : 1.0f);
                volS += 0.002f * (vt - volS);
                l *= volS; r *= volS;
                s = (l + r) * 0.5f * std::min(1.0f, p.space / 100.0f + bAmt * 0.5f);
            } else { for (auto& g : gr) if (g.on) { g.fadeK = 1.0f / (0.01f * (float) sr); grain(g, l, r); } l *= volS; r *= volS; stepOut.store(-1); }
            L[i] = l; R[i] = r; if (spc) spc[i] += s;
            lvl = std::max(lvl, std::max(std::abs(l), std::abs(r)));
        }
        levelOut.store(lvl);
        stateOut.store(state == Idle ? 0 : (state == Waiting ? 1 : (state == Recording ? 2 : 3)));
        isPlaying.store(playing);
    }
    double sr = 48000;

private:
    static constexpr int HOP = 256;
    static constexpr double RATE[7] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0 };   // 1/4 1/8 1/8T 1/16 1/16T 1/32 1/32T
    enum State { Idle, Waiting, Recording, Ready } state = Idle;
    std::vector<float> buf, envF;
    long recN = 0; double recStart = 0, recBeats = 4, lastB = 0; float envAcc = 0; int envCnt = 0;
    long sl0[MAXSL + 1] {}; int nSl = 0;
    struct Grain { bool on = false; long a = 0, b = 0; double pos = 0, inc = 1; bool rev = false; float gL = 0, gR = 0, env = 0, atkK = 0, fade = 1, fadeK = 0; long left = 0; float z = 0; float aa = 1; };
    std::array<Grain, NG> gr;
    bool playing = false, restartAt = false, building = false; double buildPos = 0, lastK = -1;
    int idx = -1, pass = 0, dir = 1, gNext = 0;
    float lastRec = 0, lastClear = 0, lastBuild = 0, lastPlay = 0;
    float z1 = 0, z2 = 0, z1r = 0, z2r = 0, cutS = 1, volS = 0, envNow = 0, envK = 0.9997f;
    std::mt19937 rng { 77 }; std::uniform_real_distribution<float> uni { 0, 1 };

    double recLength(double bpb) const { static const double B[4] = { 0.5, 1, 2, 4 }; return B[std::clamp((int) p.len, 0, 3)] * bpb; }
    void armRecord() { state = Waiting; recProgress.store(0); playing = false; for (auto& g : gr) g.fadeK = 1.0f / (0.01f * (float) sr); }
    static float svf(float x, float g, float k, float& a, float& b) {
        float hp = (x - (k + g) * a - b) / (1.0f + g * (g + k)); float bp = g * hp + a; a = g * hp + bp; float lp = g * bp + b; b = g * bp + lp; return lp; }

    // the notes: where the sound starts again (onsets of the envelope, at least 1/16 of a beat apart)
    void finishRecording() {
        state = Ready;
        long N = recN; int F = (int) (N / HOP);
        float mx = 1e-6f; for (int f = 0; f < F; ++f) mx = std::max(mx, envF[(size_t) f]);
        // the picture
        for (int w = 0; w < 256; ++w) { long a = w * N / 256, b2 = (w + 1) * N / 256; float m = 0; for (long i = a; i < b2; ++i) m = std::max(m, std::abs(buf[(size_t) i])); wave[(size_t) w].store(m / mx); }
        nSl = 0;
        double minGap = std::max(0.06 * sr, (double) N / 24.0);
        float prev = 0; long lastOn = -(long) minGap;
        for (int f = 1; f < F; ++f) {
            float e = envF[(size_t) f] / mx, e0 = envF[(size_t) f - 1] / mx;
            bool onset = e > 0.12f && e > e0 * 1.6f + 0.02f && e > prev;
            long at = (long) f * HOP;
            if (onset && at - lastOn >= (long) minGap && nSl < MAXSL) { sl0[nSl++] = std::max(0L, at - HOP - (long) (0.004 * sr)); lastOn = at; }
            prev = e0 * 0.5f + prev * 0.5f;
        }
        if (nSl == 0) { for (int k = 0; k < 4; ++k) sl0[k] = k * N / 4; nSl = 4; }       // nothing clear: four equal notes
        sl0[nSl] = N;
        // fine start of every note: back to the last zero crossing
        for (int k = 0; k < nSl; ++k) { long a = sl0[k]; for (int j = 0; j < 96 && a > 1; ++j, --a) if ((buf[(size_t) a] >= 0) != (buf[(size_t) a - 1] >= 0)) break; sl0[k] = a; }
        for (int k = 0; k <= MAXSL; ++k) sliceAt[(size_t) k].store(k <= nSl ? (float) sl0[k] / (float) std::max(1L, N) : -1.0f);
        slicesOut.store(nSl);
    }
    int nextIndex(int count) {
        int mode = std::clamp((int) p.order, 0, 5);
        if (mode == 3) return (int) (uni(rng) * count) % count;
        ++idx;
        if (idx >= stepsInCycle(count)) { idx = 0; ++pass; }
        int i = idx;
        switch (mode) {
            case 1: return count - 1 - (i % count);
            case 2: { int per = std::max(1, 2 * count - 2); int t = i % per; return t < count ? t : per - t; }
            case 4: { int a = i / 2, b2 = i % 2; return (a + b2) % count; }                 // spiral: 1 2 2 3 3 4 ...
            case 5: { int h = (count + 1) / 2; return i % count < h ? (2 * (i % count)) % count : (2 * (i % count - h) + 1) % count; }   // skip: 1 3 2 4
            default: return i % count;
        }
    }
    int stepsInCycle(int count) const { int s = (int) p.steps; if (s <= 0) s = (int) p.order == 2 ? std::max(1, 2 * count - 2) : (int) p.order == 4 ? 2 * count : count; return std::max(1, s); }
    void stepNow(double b, double stepL, double bpm, float bAmt) {
        int k = nextIndex(nSl);
        stepOut.store(idx); sliceOut.store(k);
        int octs = std::clamp((int) p.oct, 0, 3);
        double semis = p.pitch + (octs > 0 ? 12.0 * (pass % (octs + 1)) : 0.0) + bAmt * 0.0;
        // a free grain (or the oldest fades out quickly)
        Grain& g = gr[(size_t) gNext]; gNext = (gNext + 1) % NG;
        for (auto& o : gr) if (o.on) o.fadeK = std::max(o.fadeK, 1.0f / (0.03f * (float) sr));      // the previous notes give way (30 ms)
        Grain ng; ng.on = true; ng.a = sl0[k]; ng.b = sl0[k + 1];
        ng.rev = uni(rng) * 100.0f < p.rev;
        ng.inc = std::pow(2.0, semis / 12.0); ng.pos = ng.rev ? (double) (ng.b - 2) : (double) ng.a;
        double stepSamples = stepL * 60.0 / bpm * sr;
        ng.left = (long) (stepSamples * std::clamp(p.gate, 5.0f, 100.0f) / 100.0f * (ng.rev ? 1.0 : 1.0));
        ng.env = 0; ng.atkK = 1.0f / (float) (0.003 * sr); ng.fade = 1; ng.fadeK = 0;
        float spread = p.spread / 100.0f, pan = (idx % 2 == 0 ? -1.0f : 1.0f) * spread * (0.6f + 0.4f * uni(rng));
        float a = (pan + 1.0f) * 0.25f * 3.14159265f;
        ng.gL = std::cos(a) * 1.4142f; ng.gR = std::sin(a) * 1.4142f;
        ng.aa = ng.inc > 1.05 ? (float) std::exp(-2.0 * 3.14159265 * (17000.0 / ng.inc) / sr) : 0.0f;   // up: a gentle anti-alias low-pass
        g = ng;
        envNow = 1.0f; envK = (float) std::exp(-1.0 / (stepSamples * 0.6));
    }
    void grain(Grain& g, float& l, float& r) {
        long n = g.b - g.a; if (n < 8) { g.on = false; return; }
        long i0 = (long) g.pos; float f = (float) (g.pos - (double) i0);
        if (i0 < g.a || i0 >= g.b - 2) { g.fadeK = std::max(g.fadeK, 1.0f / (0.004f * (float) sr)); i0 = std::clamp(i0, g.a + 1, g.b - 3); f = 0; }
        else {}
        float xm = buf[(size_t) std::max(g.a, i0 - 1)], x0 = buf[(size_t) i0], x1 = buf[(size_t) i0 + 1], x2 = buf[(size_t) std::min(g.b - 1, i0 + 2)];
        float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        float y = ((c3 * f + c2) * f + c1) * f + x0;
        if (g.aa > 0) { g.z = y + g.aa * (g.z - y); y = g.z; }
        g.pos += g.rev ? -g.inc : g.inc;
        // envelope: 3 ms in, the gate, 8 ms out (and a quicker fade when another note takes over)
        if (g.env < 1.0f) g.env = std::min(1.0f, g.env + g.atkK);
        if (--g.left <= 0 && g.fadeK <= 0) g.fadeK = 1.0f / (0.008f * (float) sr);
        if (g.fadeK > 0) { g.fade -= g.fadeK; if (g.fade <= 0) { g.on = false; return; } }
        float e = g.env * g.fade * g.fade;
        l += y * g.gL * e; r += y * g.gR * e;
    }
};

} // namespace arco
