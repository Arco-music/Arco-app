#pragma once
// ARCO 2.0: DRUM MACHINE - twelve pads of real (sampled) and classic (modelled) drums, played by a step
// sequencer that follows the station's clock (or STUDIO's, or its own), sample-accurate.
//  - kits: banks of one-shot samples, two velocity layers x two round robins per pad (made on the message thread,
//    handed over as an immutable bank - the audio thread never allocates)
//  - every pad: level, pan, tune, decay, tone (low-pass <- -> high-pass), SPACE send, mute / solo, its own kit
//  - eight patterns of up to 64 steps; each step: velocity, chance, repeats (ratchets), nudge (early / late)
//  - swing, humanise, the whole kit through DRIVE and a GLUE compressor
//  - pads played by hand (screen, MIDI) are heard at once and, with REC, written into the pattern at the nearest step
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

namespace arco {

struct DrumSample { std::vector<float> l, r; float rate = 48000.0f; };
struct DrumPad {
    std::string name;
    std::array<std::array<DrumSample, 2>, 2> s;     // [layer: soft, hard][round robin]
    int choke = 0;                                   // pads of the same group > 0 cut each other (the hi-hat)
};
struct DrumKit { std::string name; std::array<DrumPad, 12> pads; };
struct DrumBank { std::vector<std::shared_ptr<const DrumKit>> kits; };

class DrumMachine {
public:
    static constexpr int NPAD = 12, NPAT = 8, NSTEP = 64, NVOICE = 40;

    // ---- a step, packed into one word (the interface edits it, the audio thread reads it)
    struct Step { int vel = 0, chance = 100, repeats = 1, nudge = 0; };   // vel 0 = off; nudge -50..50 (% of a step)
    static uint32_t pack(Step s) {
        return (uint32_t) std::clamp(s.vel, 0, 127) | ((uint32_t) std::clamp(s.chance, 0, 100) << 8) | ((uint32_t) std::clamp(s.repeats, 1, 4) << 16)
               | ((uint32_t) (std::clamp(s.nudge, -50, 50) + 64) << 20); }
    static Step unpack(uint32_t w) { Step s; s.vel = (int) (w & 127); s.chance = (int) ((w >> 8) & 127); s.repeats = std::max(1, (int) ((w >> 16) & 7));
                                     s.nudge = (int) ((w >> 20) & 127) - 64; if (w == 0) { s.chance = 100; s.repeats = 1; s.nudge = 0; } return s; }
    std::atomic<uint32_t> steps[NPAT][NPAD][NSTEP];
    std::atomic<int> patLen[NPAT];                   // steps (1-64)
    std::atomic<int> patRes[NPAT];                   // 0 = 1/8, 1 = 1/16, 2 = 1/16 triplets, 3 = 1/32
    Step getStep(int p, int pad, int st) const { return unpack(steps[p][pad][st].load(std::memory_order_relaxed)); }
    void setStep(int p, int pad, int st, Step s) { steps[p][pad][st].store(s.vel > 0 ? pack(s) : 0u, std::memory_order_relaxed); }
    static double stepBeats(int res) { static const double R[4] = { 0.5, 0.25, 1.0 / 6.0, 0.125 }; return R[std::clamp(res, 0, 3)]; }

    // ---- the parameters
    struct PadP { float lvl = 80, pan = 0, tune = 0, dec = 100, tone = 50, mute = 0, solo = 0, kit = -1, spc = 0; };
    struct P { float on = 1, kit = 0, pat = 0, swing = 50, vol = 75, drive = 0, glue = 20, human = 0, space = 0, run = 0, rec = 0, follow = 1, chain = 0;
               std::array<PadP, NPAD> pad; } p;
    std::vector<std::string> names;
    std::vector<float*> slot;
    int paramIndex(const std::string& n) const { for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int) i; return -1; }
    void setp(int i, double v) { if (i >= 0 && i < (int) slot.size()) *slot[(size_t) i] = (float) v; }
    static std::vector<std::string> paramNames() {
        std::vector<std::string> v = { "dm_on", "dm_kit", "dm_pat", "dm_swing", "dm_vol", "dm_drive", "dm_glue", "dm_human", "dm_space", "dm_run", "dm_rec", "dm_follow", "dm_chain" };
        for (int i = 0; i < NPAD; ++i) for (auto* k : { "lvl", "pan", "tune", "dec", "tone", "mute", "solo", "kit", "spc" }) v.push_back("dm" + std::to_string(i) + "_" + k);
        return v;
    }

    DrumMachine() {
        names = paramNames();
        float* g[] = { &p.on, &p.kit, &p.pat, &p.swing, &p.vol, &p.drive, &p.glue, &p.human, &p.space, &p.run, &p.rec, &p.follow, &p.chain };
        for (auto* x : g) slot.push_back(x);
        for (int i = 0; i < NPAD; ++i) { auto& q = p.pad[(size_t) i]; for (auto* x : { &q.lvl, &q.pan, &q.tune, &q.dec, &q.tone, &q.mute, &q.solo, &q.kit, &q.spc }) slot.push_back(x); }
        for (auto& a : steps) for (auto& b : a) for (auto& c : b) c.store(0);
        for (int i = 0; i < NPAT; ++i) { patLen[i].store(16); patRes[i].store(1); }
        for (auto& h : hit) h.store(0);
    }
    void init(double sampleRate) { sr = sampleRate; for (auto& v : vo) v.active = false; resync = true; glueEnv = 0; inBeat = 0; }

    // the samples (message thread; the bank stays alive for as long as the program runs)
    std::atomic<const DrumBank*> bank { nullptr };

    // ---- played by hand (any thread -> audio thread)
    struct Hit { int pad; float vel; };
    void play(int pad, float vel) { auto w = hw.load(); auto n = (w + 1) % HQ; if (n == hr.load()) return; hq[w] = { pad, vel }; hw.store(n); }

    // ---- what the interface shows
    std::atomic<int> playStep { -1 };                // the step under the playhead (-1 stopped)
    std::atomic<int> hit[NPAD];                      // counts up at every hit of a pad (the pads flash)
    std::atomic<int> patPlaying { 0 };
    std::atomic<int> queuedPat { -1 };               // a new pattern starts at the end of the current one

    // clock: beat position and tempo per sample, `running`: the clock moves (station / STUDIO / its own)
    // out: adds into L / R, and into spc (SPACE's input)
    void process(const float* beat, const float* bpmIn, const float* runIn, float* L, float* R, float* spc, int n) {
        const DrumBank* bk = bank.load(std::memory_order_acquire);
        // the pads played by hand
        for (auto r = hr.load(); r != hw.load(); r = (r + 1) % HQ) { auto h = hq[r]; hr.store((r + 1) % HQ); handHit(bk, h.pad, h.vel, beat ? beat[0] : 0.0, runIn && runIn[0] > 0.5f); }
        bool anySolo = false; for (auto& q : p.pad) anySolo |= q.solo > 0.5f;
        int pat = std::clamp((int) p.pat, 0, NPAT - 1);
        if (pat != lastParamPat) {               // a new pattern chosen: at once when stopped, else at the end of this one
            lastParamPat = pat;
            if (!wasRunning) { curPat = pat; queuedPat.store(-1); patPlaying.store(pat); } else queuedPat.store(pat);
        }
        for (int i = 0; i < n; ++i) {
            bool running = p.on > 0.5f && runIn && runIn[i] > 0.5f;
            double b = beat ? beat[i] : 0.0;
            if (running && bk) {
                if (!wasRunning || resync || b < lastBeat - 1e-6 || b > lastBeat + 1.0) { schedule(b, true); resync = false; }
                while (b >= nextDue - 1e-9) { fireStep(); advance(); }
                for (int e = 0; e < nPend; ) { if (b >= pend[e].due - 1e-9) { trigger(bk, pend[e].pad, pend[e].vel, anySolo); pend[e] = pend[--nPend]; } else ++e; }
            } else if (wasRunning) { playStep.store(-1); nPend = 0; }
            wasRunning = running; lastBeat = b; curBpm = bpmIn ? bpmIn[i] : 120.0f;
            // the voices
            float l = 0, r = 0, s = 0;
            for (auto& v : vo) if (v.active) renderVoice(v, l, r, s);
            outL[i] = l; outR[i] = r; outS[i] = s;
        }
        // the kit bus: drive, glue, level
        float vol = std::pow(p.vol / 100.0f, 2.0f) * 2.0f;
        float drv = p.drive / 100.0f, dg = 1.0f + drv * 6.0f, dc = 1.0f / std::tanh(dg);
        float th = std::pow(10.0f, (-6.0f - p.glue * 0.18f) / 20.0f), ratio = 1.0f + p.glue / 100.0f * 5.0f;
        float atk = 1.0f - std::exp(-1.0f / (0.004f * (float) sr)), rel = 1.0f - std::exp(-1.0f / (0.12f * (float) sr));
        float spAmt = p.space / 100.0f;
        for (int i = 0; i < n; ++i) {
            float l = outL[i], r = outR[i];
            if (drv > 0.001f) { l = std::tanh(l * dg) * dc * (1.0f - 0.25f * drv); r = std::tanh(r * dg) * dc * (1.0f - 0.25f * drv); }
            float lv = std::max(std::abs(l), std::abs(r));
            glueEnv += (lv > glueEnv ? atk : rel) * (lv - glueEnv);
            float gr = 1.0f;
            if (p.glue > 0.5f && glueEnv > th) gr = std::pow(glueEnv / th, 1.0f / ratio - 1.0f);
            float mk = p.glue > 0.5f ? 1.0f + p.glue / 100.0f * 0.6f : 1.0f;
            l *= gr * mk * vol; r *= gr * mk * vol;
            l = softLimit(l); r = softLimit(r);
            L[i] += l; R[i] += r;
            if (spc) spc[i] += (outS[i] * vol + (l + r) * 0.5f * spAmt) * 0.7f;
        }
    }
    double sr = 48000.0;

private:
    static float softLimit(float x) { return std::abs(x) < 0.9f ? x : std::copysign(0.9f + 0.1f * std::tanh((std::abs(x) - 0.9f) * 10.0f), x); }
    struct Voice {
        bool active = false; int pad = 0; const DrumSample* s = nullptr; double pos = 0, inc = 1;
        float gL = 0, gR = 0, env = 1, decK = 1, hold = 0, fade = 1, fadeK = 0, spc = 0;
        int tone = 0; float g = 0, k = 1, z1[2] = {}, z2[2] = {};   // a state-variable filter (TPT), 0 none, 1 LP, 2 HP
        int age = 0;
    };
    std::array<Voice, NVOICE> vo;
    float outL[4096] {}, outR[4096] {}, outS[4096] {};
    static constexpr int HQ = 256; Hit hq[HQ]; std::atomic<int> hr { 0 }, hw { 0 };
    struct Pend { int pad; float vel; double due; };
    Pend pend[128]; int nPend = 0;
    int curPat = 0, lastParamPat = 0, nextStep = 0, age = 0; double nextDue = 1e18, cycleStart = 0, lastBeat = 0, inBeat = 0; bool wasRunning = false, resync = true;
    float curBpm = 120, glueEnv = 0;
    std::mt19937 rng { 2024 };
    std::uniform_real_distribution<float> uni { 0.0f, 1.0f };
    int rr[NPAD] {};

    double stepLen() const { return stepBeats(patRes[curPat].load(std::memory_order_relaxed)); }
    int len() const { return std::clamp(patLen[curPat].load(std::memory_order_relaxed), 1, NSTEP); }
    // when step k of the cycle starting at `start` is due (swing on the off-beats, the step's nudge)
    double dueOf(double start, int k) const {
        double L = stepLen(); double t = start + k * L;
        if (k % 2 == 1) t += (std::clamp(p.swing, 50.0f, 75.0f) - 50.0f) / 50.0f * L;
        return t;
    }
    // find the next step from beat b (on a start: the step on b itself plays)
    void schedule(double b, bool inclusive) {
        double L = stepLen(), cyc = L * len();
        cycleStart = std::floor((b + 1e-9) / cyc) * cyc;
        nextStep = 0;
        while (nextStep < len() && dueOf(cycleStart, nextStep) < b - (inclusive ? 0.02 * L : 0.0)) ++nextStep;
        if (nextStep >= len()) { cycleStart += cyc; nextStep = 0; }
        nextDue = dueOf(cycleStart, nextStep);
        nPend = 0;
    }
    void advance() {
        ++nextStep;
        if (nextStep >= len()) {
            nextStep = 0; cycleStart += stepLen() * len();
            int q = queuedPat.exchange(-1);
            if (q >= 0) curPat = q;
            else if (p.chain > 0.5f) {           // CHAIN: the patterns that have notes, one after another
                for (int k = 1; k <= NPAT; ++k) { int c = (curPat + k) % NPAT; if (hasNotes(c)) { curPat = c; break; } }
            }
            patPlaying.store(curPat);
        }
        nextDue = dueOf(cycleStart, nextStep);
    }
    bool hasNotes(int pt) const { for (int pd = 0; pd < NPAD; ++pd) for (int s = 0; s < patLen[pt].load(); ++s) if (steps[pt][pd][s].load(std::memory_order_relaxed) & 127) return true; return false; }
    void fireStep() {
        playStep.store(nextStep); patPlaying.store(curPat);
        double L = stepLen(), due = dueOf(cycleStart, nextStep);
        float hum = p.human / 100.0f;
        for (int pd = 0; pd < NPAD; ++pd) {
            auto st = getStep(curPat, pd, nextStep);
            if (st.vel <= 0) continue;
            if (st.chance < 100 && uni(rng) * 100.0f >= (float) st.chance) continue;
            float vel = st.vel / 127.0f;
            if (hum > 0) vel = std::clamp(vel * (1.0f + (uni(rng) - 0.5f) * 0.35f * hum), 0.05f, 1.0f);
            double t0 = due + st.nudge / 100.0 * L;
            if (hum > 0) t0 += (uni(rng) - 0.5) * 0.06 * L * hum;
            for (int k = 0; k < st.repeats; ++k) {
                double t = t0 + k * L / st.repeats; float vv = k == 0 ? vel : vel * (0.78f + 0.08f * k);
                if (nPend < 128) pend[nPend++] = { pd, vv, t };      // (due now: played right below, this same sample)
            }
        }
    }
    void handHit(const DrumBank* bk, int pad, float vel, double b, bool running) {
        if (pad < 0 || pad >= NPAD) return;
        trigger(bk, pad, vel, false, true);
        if (!running || p.rec < 0.5f) return;
        // REC: written at the nearest step of the pattern playing now
        double L = stepLen(); int ln = len();
        double rel = b - cycleStart; int k = (int) std::lround(rel / L);
        k = ((k % ln) + ln) % ln;
        Step s; s.vel = std::clamp((int) std::lround(vel * 127.0f), 1, 127);
        setStep(curPat, pad, k, s);
    }
    void trigger(const DrumBank* bk, int pad, float vel, bool anySolo, bool byHand = false) {
        if (!bk || bk->kits.empty()) return;
        auto& pp = p.pad[(size_t) pad];
        if (!byHand && (pp.mute > 0.5f || (anySolo && pp.solo < 0.5f))) return;
        int ki = (int) (pp.kit >= 0 ? pp.kit : p.kit);
        ki = std::clamp(ki, 0, (int) bk->kits.size() - 1);
        auto& kit = *bk->kits[(size_t) ki];
        auto& P = kit.pads[(size_t) pad];
        hit[pad].fetch_add(1);
        // the choke group: the closed hi-hat stops the open one
        if (P.choke > 0) for (auto& v : vo) if (v.active && v.pad != pad && kit.pads[(size_t) v.pad].choke == P.choke) { v.fadeK = 1.0f / (0.012f * (float) sr); }
        int layer = vel > 0.62f ? 1 : 0;
        int r = rr[pad] = (rr[pad] + 1) % 2;
        const DrumSample* s = &P.s[(size_t) layer][(size_t) r];
        if (s->l.empty()) s = &P.s[(size_t) layer][0];
        if (s->l.empty()) s = &P.s[0][0];
        if (s->l.empty()) return;
        // the layer gives the timbre; the velocity inside it the level (soft hits of the hard layer are not too loud)
        float g = layer == 1 ? 0.55f + 0.45f * (vel - 0.62f) / 0.38f : 0.45f + 0.55f * vel / 0.62f;
        g *= std::pow(pp.lvl / 100.0f, 2.0f) * 1.6f;
        // a free voice (or the oldest of this pad, or the oldest at all)
        Voice* v = nullptr; int perPad = 0; Voice* oldestPad = nullptr; Voice* oldest = nullptr;
        for (auto& x : vo) {
            if (!x.active) { if (!v) v = &x; continue; }
            if (x.pad == pad) { ++perPad; if (!oldestPad || x.age < oldestPad->age) oldestPad = &x; }
            if (!oldest || x.age < oldest->age) oldest = &x;
        }
        if (perPad >= 4 && oldestPad) oldestPad->fadeK = 1.0f / (0.004f * (float) sr);
        if (!v) { v = oldest; }
        Voice nv; nv.active = true; nv.pad = pad; nv.s = s; nv.pos = 0; nv.age = ++age;
        nv.inc = (double) s->rate / sr * std::pow(2.0, pp.tune / 12.0);
        float pan = std::clamp(pp.pan / 50.0f, -1.0f, 1.0f), a = (pan + 1.0f) * 0.25f * 3.14159265f;
        nv.gL = g * std::cos(a) * 1.4142f; nv.gR = g * std::sin(a) * 1.4142f;
        nv.spc = pp.spc / 100.0f;
        nv.env = 1; nv.fade = 1; nv.fadeK = 0; nv.hold = (float) (0.006 * sr);
        nv.decK = pp.dec >= 99.5f ? 1.0f : std::exp(-1.0f / ((0.025f * std::pow(90.0f, pp.dec / 100.0f)) * (float) sr));
        if (pp.tone < 47 || pp.tone > 53) {
            double fc = pp.tone < 50 ? 18000.0 * std::pow(250.0 / 18000.0, (50.0 - pp.tone) / 50.0) : 20.0 * std::pow(2500.0 / 20.0, (pp.tone - 50.0) / 50.0);
            nv.tone = pp.tone < 50 ? 1 : 2; nv.g = (float) std::tan(3.14159265 * std::min(fc, sr * 0.45) / sr); nv.k = 1.2f;
        }
        if (v->active) {             // stealing: a very short fade would be better, but the new hit matters more - keep it click-free with a 1 ms ramp
            nv.fade = 0.0f; nv.fadeK = -1.0f / (0.001f * (float) sr);
        }
        *v = nv;
    }
    void renderVoice(Voice& v, float& l, float& r, float& s) {
        auto& S = *v.s;
        long n = (long) S.l.size();
        long i0 = (long) v.pos;
        if (i0 >= n - 1) { v.active = false; return; }
        float f = (float) (v.pos - (double) i0);
        auto cub = [&](const std::vector<float>& x) {     // 4-point Hermite
            float xm = i0 > 0 ? x[(size_t) i0 - 1] : x[(size_t) i0], x0 = x[(size_t) i0], x1 = x[(size_t) i0 + 1], x2 = i0 + 2 < n ? x[(size_t) i0 + 2] : x1;
            float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0; };
        float a = cub(S.l), b = S.r.empty() ? a : cub(S.r);
        v.pos += v.inc;
        if (v.hold > 0) v.hold -= 1.0f; else v.env *= v.decK;
        if (v.fadeK > 0) { v.fade -= v.fadeK; if (v.fade <= 0) { v.active = false; return; } }
        else if (v.fadeK < 0) { v.fade -= v.fadeK; if (v.fade >= 1) { v.fade = 1; v.fadeK = 0; } }
        if (v.env < 0.0003f) { v.active = false; return; }
        if (v.tone != 0) {
            float in[2] = { a, b };
            for (int c = 0; c < 2; ++c) {
                float hp = (in[c] - (v.k + v.g) * v.z1[c] - v.z2[c]) / (1.0f + v.g * (v.g + v.k));
                float bp = v.g * hp + v.z1[c]; v.z1[c] = v.g * hp + bp;
                float lp = v.g * bp + v.z2[c]; v.z2[c] = v.g * bp + lp;
                in[c] = v.tone == 1 ? lp : hp;
            }
            a = in[0]; b = in[1];
        }
        float e = v.env * v.fade;
        l += a * v.gL * e; r += b * v.gR * e; s += (a + b) * 0.5f * v.spc * e * (v.gL + v.gR) * 0.5f;
    }
};

} // namespace arco
