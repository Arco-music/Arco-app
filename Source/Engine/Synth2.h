#pragma once
// ARCO 1.2: SYNTH 2 (1.3: 4 oscillators). 12 voices, 4 oscillators (WAVE morph / FM / FOLD / SYNC, unison up to 5), sub, noise, a resonant
// filter (12 or 24 dB, low / band / high), 3 envelopes, 3 LFOs, a modulation matrix (12 slots: any source to any of the
// knobs below), 4 macros, glide, mono / legato, FOLLOW (a voice sings the note LIVE 1 plays), and an arpeggiator in tempo.
// The old SYNTH's parameters keep their meaning, so every preset sounds as before.
// Message bus "st_syn": voices sn<k> (note, may be fractional), sv<k> (velocity, 0 = released), sg<k> (a new number starts
// the note), "bend" (semitones), "expr" (0..1 loudness while playing), hn<note> (held notes for the arpeggiator).
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

namespace arco {

// the modulation sources and destinations (the SYNTH window shows them in this order)
inline const std::vector<std::string>& synthModSources() {
    static const std::vector<std::string> s = { "-", "LFO 1", "LFO 2", "LFO 3", "ENV 2", "ENV 3", "VELOCITY", "NOTE", "EXPRESSION", "RANDOM", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4" };
    return s;
}
struct SynthDest { const char* name; const char* knob; float range; };     // knob: the parameter it moves (for drag and drop)
inline const std::vector<SynthDest>& synthModDests() {
    static const std::vector<SynthDest> d = {
        { "-", "", 0 }, { "PITCH", "f_oct", 24 }, { "A PITCH", "a_semi", 24 }, { "B PITCH", "b_semi", 24 }, { "C PITCH", "c_semi", 24 },
        { "A WAVE", "a_wave", 100 }, { "B WAVE", "b_wave", 100 }, { "C WAVE", "c_wave", 100 },
        { "A WARP", "a_warp", 100 }, { "B WARP", "b_warp", 100 }, { "C WARP", "c_warp", 100 },
        { "A LEVEL", "a_lvl", 100 }, { "B LEVEL", "b_lvl", 100 }, { "C LEVEL", "c_lvl", 100 }, { "SUB", "sub_lvl", 100 }, { "NOISE", "noise_lvl", 100 },
        { "CUTOFF", "f_cut", 100 }, { "RESO", "f_res", 100 }, { "DRIVE", "f_drive", 100 }, { "VOLUME", "s_lvl", 100 }, { "PAN", "spread", 100 },
        { "LFO 1 RATE", "l_rate", 100 }, { "LFO 2 RATE", "l2_rate", 100 }, { "LFO 3 RATE", "l3_rate", 100 }, { "DETUNE", "a_det", 100 },
        { "A FINE", "a_fine", 100 }, { "B FINE", "b_fine", 100 }, { "C FINE", "c_fine", 100 },
        { "D PITCH", "d_semi", 24 }, { "D WAVE", "d_wave", 100 }, { "D WARP", "d_warp", 100 }, { "D LEVEL", "d_lvl", 100 }, { "D FINE", "d_fine", 100 },
    };
    return d;
}
inline int synthDestForKnob(const std::string& knob) {
    auto& d = synthModDests();
    for (int i = 1; i < (int) d.size(); ++i) if (knob == d[(size_t) i].knob) return i;
    if (knob == "b_det" || knob == "c_det" || knob == "d_det") return 24;
    return 0;
}
constexpr int kModSlots = 12;

class Synth2 {
public:
    static constexpr int NV = 12, NU = 5, NO = 4;
    static constexpr int NM = 33;   // modulation destinations
    Synth2() { build(); }
    void init(double sampleRate) {
        sr = sampleRate;
        for (auto& v : vo) v = Voice();
        std::mt19937 rng(7);
        for (auto& v : vo) for (auto& o : v.ph) for (auto& p : o) p = (float) (rng() % 10000) / 10000.0f;
        lph = { 0, 0, 0 }; lsh = { 0, 0, 0 }; lshPrev = { 0, 0, 0 }; lshNext = { 0, 0, 0 };
        arpPos = -1; arpIdx = 0; arpNote = -1; internalBeat = 0; lastBeat = -1; beatStill = 0;
    }
    int paramIndex(const std::string& n) const { auto it = idx.find(n); return it == idx.end() ? -1 : it->second; }
    // 2.4: what the LFOs do right now (-1..1) and where they are in their cycle (0..1) - the screen moves the knobs with them
    std::atomic<float> lfoNow[3] { {0}, {0}, {0} }, lfoPhase[3] { {0}, {0}, {0} };
    void setp(int i, double v) { if (i >= 0 && i < (int) slot.size()) *slot[(size_t) i] = (float) v; }

    // pitch: LIVE 1 pitch (MIDI note, <= 0 none); beat: the beat position (for SYNC and the arpeggiator); bpm: the tempo
    void process(const float* pitch, const float* beat, const float* bpmIn, float* outL, float* outR, float* outMono, float* outN, float* outLfo, int n) {
        const float isr = 1.0f / (float) sr;
        // ---- shared settings
        auto ms = [this](float x) { return (float) (x * 0.001 * sr); };
        float ta[3], td[3], tr[3], sl[3];
        const float* E[3][4] = { { &P.e1a, &P.e1d, &P.e1s, &P.e1r }, { &P.e2a, &P.e2d, &P.e2s, &P.e2r }, { &P.e3a, &P.e3d, &P.e3s, &P.e3r } };
        for (int e = 0; e < 3; ++e) {
            ta[e] = 1.0f - std::exp(-1.0f / ms(1 + std::pow(*E[e][0] / 100.0f, 3.0f) * 5000));
            td[e] = 1.0f - std::exp(-3.0f / ms(5 + std::pow(*E[e][1] / 100.0f, 3.0f) * 6000));
            tr[e] = std::exp(-4.0f / ms(5 + std::pow(*E[e][3] / 100.0f, 3.0f) * 8000));
            sl[e] = *E[e][2] / 100.0f;
        }
        float gk = 1.0f - std::exp(-1.0f / ms(1 + std::pow(P.glide / 100.0f, 2.0f) * 2000));
        float gkMin = 1.0f - std::exp(-1.0f / ms(8));
        int fty = std::clamp((int) (P.ftype / 100.0f * 2.99f), 0, 2);
        float rq = 2.0f - 1.96f * std::pow(P.fres / 100.0f, 0.7f);
        float spr = P.spread / 100.0f;
        float velAmt = P.velAmt / 100.0f;
        float bendSemis = P.bend * P.bendRange / 2.0f;
        int arpOn = P.arpOn > 0.5f;
        if (!arpOn && arpNote >= 0) { releaseArp(); }
        float pk = 0; int nact = 0;
        samplesNow += n;
        for (int i = 0; i < n; ++i) {
            // ---- the tempo clock (the loop's beat when it runs, otherwise our own at the tempo)
            float b = beat ? beat[i] : 0.0f, bpm = bpmIn && bpmIn[i] > 20.0f ? bpmIn[i] : 90.0f;
            if (b != lastBeat) { beatStill = 0; internalBeat = b; } else if (++beatStill > (int) (0.1 * sr)) internalBeat += bpm / 60.0f * isr;
            lastBeat = b;
            double clock = beatStill > (int) (0.1 * sr) ? internalBeat : b;
            if (i == 0) clockBpm.store(bpm);
            // ---- LFOs (every sample: cheap)
            float lfo[3];
            for (int l = 0; l < 3; ++l) lfo[l] = lastLfo[l] = lfoTick(l, clock, isr);
            exprSm += 0.002f * (P.expr - exprSm);
            // ---- the arpeggiator
            if (arpOn) arpTick(clock);
            // ---- FOLLOW: voice 1 sings the note of LIVE 1
            float fp = pitch ? pitch[i] : 0.0f;
            if (fp > 20) { followP = fp; followHold = (int) (0.09 * sr); } else if (followHold > 0) --followHold;
            float l = 0, r = 0;
            for (int k = 0; k < NV; ++k) {
                auto& v = vo[(size_t) k];
                float nt = v.note, vl = v.vel; int tg = v.trig;
                if (P.follow > 0.5f) { nt = followP + P.foct; vl = (followHold > 0 && k == 0) ? 100.0f : 0.0f; tg = v.lastTrig; }
                else if (P.mono > 0.5f && k > 0) vl = 0;
                bool gate = vl > 0;
                bool trig = tg != v.lastTrig; v.lastTrig = tg;
                if (trig && gate) {
                    for (int e = 0; e < 3; ++e) v.st[e] = 1;
                    v.velo = vl / 127.0f;
                    if (v.env[0] < 0.0001f || P.glide < 0.5f) v.pitch = nt;
                    v.rnd = (float) (rngDist(rng)); v.glideFast = false;
                    if (lfoRetrig()) { /* the LFOs run free: they keep the groove */ }
                } else if (!trig && std::abs(nt - v.lastNote) > 1e-4f) v.glideFast = P.glide < 0.5f;   // a pad slide or a bend: follow quickly
                v.lastNote = nt;
                if (gate && v.st[0] == 0) { for (int e = 0; e < 3; ++e) v.st[e] = 1; v.velo = vl / 127.0f; }
                if (!gate && v.gate) for (int e = 0; e < 3; ++e) v.st[e] = 4;
                v.gate = gate;
                // envelopes
                for (int e = 0; e < 3; ++e) {
                    float& x = v.env[e]; int& s = v.st[e];
                    if (s == 1) { x += (1.25f - x) * ta[e]; if (x >= 1) { x = 1; s = 2; } }
                    else if (s == 2) x += (sl[e] - x) * td[e];
                    else if (s == 4) { x *= tr[e]; if (x < 0.0001f) { x = 0; s = 0; } }
                }
                if (v.st[0] == 0) { v.clearFilters(); continue; }
                ++nact;
                v.pitch += (nt - v.pitch) * (v.glideFast ? gkMin : gk);
                // modulation (at control rate: every 16 samples)
                if ((v.ctl++ & 15) == 0) modulate(v, lfo);
                const float* M = v.mod;
                float pv = v.pitch + bendSemis + lfo[0] * std::pow(P.lpitch / 100.0f, 2.0f) + M[1];
                float wmod = lfo[0] * P.lwave / 100.0f * 50.0f + v.env[1] * P.e2wave / 100.0f * 50.0f;
                float sL = 0, sR = 0;
                static const int DP[NO] = { 2, 3, 4, 28 }, DW[NO] = { 5, 6, 7, 29 }, DR[NO] = { 8, 9, 10, 30 }, DL[NO] = { 11, 12, 13, 31 }, DF[NO] = { 25, 26, 27, 32 };
                for (int o = 0; o < NO; ++o) {
                    auto& O = P.osc[o];
                    float lv = std::clamp(O.lvl / 100.0f + M[DL[o]] / 100.0f, 0.0f, 1.0f);
                    if (lv <= 0.001f) continue;
                    int nu = std::clamp((int) O.uni, 1, NU);
                    float wave = std::clamp(O.wave + wmod + M[DW[o]], 0.0f, 100.0f), warp = std::clamp(O.warp + M[DR[o]], 0.0f, 100.0f);
                    float det = std::clamp(O.det + M[24], 0.0f, 100.0f);
                    float ug = lv / std::sqrt((float) nu);
                    float base = pv + O.semi + M[DP[o]] + (O.fine + M[DF[o]] * 0.5f) / 100.0f;
                    int mode = std::clamp((int) O.mode, 0, 3);
                    for (int u = 0; u < nu; ++u) {
                        float uo = nu > 1 ? (u / (float) (nu - 1)) * 2.0f - 1.0f : 0.0f;
                        float hz = 440.0f * std::exp2((base + uo * det / 100.0f * 0.5f - 69.0f) / 12.0f);
                        float inc = std::min(0.45f, hz * isr);
                        float smp = oscSample(v, o, u, mode, inc, wave, warp);
                        smp *= ug;
                        float pa = 0.5f + uo * 0.5f * spr;
                        sL += smp * (1 - pa) * 1.4142f; sR += smp * pa * 1.4142f;
                    }
                }
                float asub = std::clamp(P.sub / 100.0f + M[14] / 100.0f, 0.0f, 1.0f);
                if (asub > 0.001f) {
                    v.subPh += std::min(0.45f, 440.0f * std::exp2((pv + P.osc[0].semi - 12 - 69.0f) / 12.0f) * isr); if (v.subPh >= 1) v.subPh -= 1;
                    float sv = std::sin(6.2831853f * v.subPh) * asub; sL += sv; sR += sv;
                }
                float anz = std::clamp(P.noise / 100.0f + M[15] / 100.0f, 0.0f, 1.0f);
                if (anz > 0.001f) { float nz = (rngDist(rng)) * anz * 0.5f; sL += nz; sR += nz; }
                // filter: drive, then a state-variable filter (two in a row for 24 dB)
                float cutK = std::clamp(P.fcut + M[16], 0.0f, 100.0f);
                float co = 20.0f * std::pow(1000.0f, cutK / 100.0f) * std::exp2(v.env[1] * P.fenv / 100.0f * 6.0f + (pv - 60.0f) / 12.0f * P.fkey / 100.0f + lfo[0] * P.lcut / 100.0f * 3.0f);
                co = std::clamp(co, 20.0f, (float) sr * 0.45f);
                float rqv = std::clamp(rq - M[17] / 100.0f * 1.9f, 0.04f, 2.0f);
                float dg = 1.0f + std::clamp(P.fdrive + M[18], 0.0f, 100.0f) / 100.0f * 6.0f;
                float g = std::tan(3.14159265f * co * isr);
                float xl = std::tanh(sL * dg) / std::sqrt(dg), xr = std::tanh(sR * dg) / std::sqrt(dg);
                float yl, yr;
                if (P.slope > 0.5f) {       // 24 dB: a Butterworth pair, the resonance in the second
                    float q2 = std::min(0.765f, rqv);
                    yl = v.f[2].tick(v.f[0].tick(xl, g, 1.848f, fty), g, q2, fty);
                    yr = v.f[3].tick(v.f[1].tick(xr, g, 1.848f, fty), g, q2, fty);
                } else { yl = v.f[0].tick(xl, g, rqv, fty); yr = v.f[1].tick(xr, g, rqv, fty); }
                float amp = v.env[0] * ((1.0f - velAmt) + velAmt * v.velo) * std::clamp(1.0f + M[19] / 100.0f, 0.0f, 2.0f);
                float pan = std::clamp(M[20] / 100.0f, -1.0f, 1.0f);
                l += yl * amp * (pan > 0 ? 1 - pan : 1); r += yr * amp * (pan < 0 ? 1 + pan : 1);
            }
            float lv = std::pow(P.level / 100.0f, 2.0f) * 1.2f * exprSm;
            float ol = std::tanh(l * lv), orr = std::tanh(r * lv);
            outL[i] = ol; outR[i] = orr; outMono[i] = (ol + orr) * 0.5f;
            outN[i] = (float) countActive(); outLfo[i] = lfo[0];
            pk = std::max(pk, std::abs(ol));
        }
        (void) nact; (void) pk;
        for (int l = 0; l < 3; ++l) { lfoNow[l].store(lastLfo[l], std::memory_order_relaxed); lfoPhase[l].store((float) lph[(size_t) l], std::memory_order_relaxed); }
    }
    float lastLfo[3] { 0, 0, 0 };

private:
    double sr = 48000;
    struct OscP { float wave = 50, warp = 0, semi = 0, fine = 0, lvl = 80, uni = 1, det = 25, mode = 0; };
    struct Params {
        OscP osc[NO];
        float sub = 0, noise = 0, ftype = 0, fcut = 60, fres = 20, fdrive = 10, fenv = 30, fkey = 50, slope = 0;
        float e1a = 5, e1d = 40, e1s = 70, e1r = 35, e2a = 10, e2d = 45, e2s = 30, e2r = 40, e3a = 20, e3d = 50, e3s = 0, e3r = 40;
        float lrate[3] = { 30, 20, 10 }, lshape[3] = { 0, 0, 0 }, lsync[3] = { 0, 0, 0 };
        float lwave = 0, lcut = 0, lpitch = 0, e2wave = 0, glide = 0, mono = 0, spread = 50, level = 70, follow = 0, foct = 0;
        float msrc[kModSlots] {}, mdst[kModSlots] {}, mamt[kModSlots] {};
        float macro[4] { 0, 0, 0, 0 };
        float arpOn = 0, arpMode = 0, arpRate = 3, arpOct = 1, arpGate = 60, arpHold = 0;
        float velAmt = 65, bendRange = 2, bend = 0, expr = 1;
    } P;
    struct Svf { float z1 = 0, z2 = 0;
        float tick(float x, float g, float rq, int type) {
            float a1 = 1.0f / (1.0f + g * (g + rq));
            float v1 = (z1 + g * (x - z2)) * a1, v2 = z2 + g * v1;
            z1 = 2 * v1 - z1; z2 = 2 * v2 - z2;
            if (!std::isfinite(z1) || !std::isfinite(z2)) { z1 = z2 = 0; return 0; }
            return type == 0 ? v2 : (type == 1 ? v1 * rq : x - rq * v1 - v2); } };
    struct Voice {
        float note = 60, vel = 0; int trig = 0, lastTrig = 0;
        float pitch = 60, lastNote = 60, velo = 0, rnd = 0; bool gate = false, glideFast = false;
        float env[3] { 0, 0, 0 }; int st[3] { 0, 0, 0 };
        float ph[NO][NU] {}, ph2[NO][NU] {}, subPh = 0;
        Svf f[4];
        float mod[NM] {};
        unsigned ctl = 0;
        void clearFilters() { for (auto& x : f) x = Svf(); }
    };
    std::array<Voice, NV> vo;
    std::vector<float*> slot;
    std::map<std::string, int> idx;
    std::array<float, 128> held {};
    std::vector<int> heldOrder;
    float followP = 0; int followHold = 0;
    std::array<double, 3> lph {}; std::array<float, 3> lsh {}, lshPrev {}, lshNext {};
    double internalBeat = 0; float lastBeat = -1; int beatStill = 0;
    float exprSm = 1;
    std::mt19937 rng { 1234 };
    std::uniform_real_distribution<float> rngDist { -1.0f, 1.0f };
    // arpeggiator state
    double arpPos = -1; int arpIdx = 0, arpNote = -1, arpVoice = 0, arpDir = 1; double arpOffAt = -1; int arpTrig = 0;

    void reg(const std::string& n, float* p) { idx[n] = (int) slot.size(); slot.push_back(p); }
    void build() {
        const char* on[NO] = { "a", "b", "c", "d" };
        for (int o = 0; o < NO; ++o) {
            std::string p = on[o];
            reg(p + "_wave", &P.osc[o].wave); reg(p + "_warp", &P.osc[o].warp); reg(p + "_semi", &P.osc[o].semi); reg(p + "_fine", &P.osc[o].fine);
            reg(p + "_lvl", &P.osc[o].lvl); reg(p + "_uni", &P.osc[o].uni); reg(p + "_det", &P.osc[o].det); reg(p + "_mode", &P.osc[o].mode);
        }
        P.osc[1] = { 75, 0, -12, 5, 0, 1, 25, 0 }; P.osc[2] = { 25, 0, 12, 0, 0, 1, 25, 0 }; P.osc[3] = { 50, 0, 7, 0, 0, 1, 25, 0 };
        reg("sub_lvl", &P.sub); reg("noise_lvl", &P.noise); reg("f_type", &P.ftype); reg("f_cut", &P.fcut); reg("f_res", &P.fres); reg("f_drive", &P.fdrive);
        reg("f_env", &P.fenv); reg("f_key", &P.fkey); reg("f_slope", &P.slope);
        reg("e1_a", &P.e1a); reg("e1_d", &P.e1d); reg("e1_s", &P.e1s); reg("e1_r", &P.e1r);
        reg("e2_a", &P.e2a); reg("e2_d", &P.e2d); reg("e2_s", &P.e2s); reg("e2_r", &P.e2r);
        reg("e3_a", &P.e3a); reg("e3_d", &P.e3d); reg("e3_s", &P.e3s); reg("e3_r", &P.e3r);
        reg("l_rate", &P.lrate[0]); reg("l_shape", &P.lshape[0]); reg("l_sync", &P.lsync[0]);
        reg("l2_rate", &P.lrate[1]); reg("l2_shape", &P.lshape[1]); reg("l2_sync", &P.lsync[1]);
        reg("l3_rate", &P.lrate[2]); reg("l3_shape", &P.lshape[2]); reg("l3_sync", &P.lsync[2]);
        reg("l_wave", &P.lwave); reg("l_cut", &P.lcut); reg("l_pitch", &P.lpitch); reg("e2_wave", &P.e2wave);
        reg("s_glide", &P.glide); reg("mono", &P.mono); reg("spread", &P.spread); reg("s_lvl", &P.level); reg("follow", &P.follow); reg("f_oct", &P.foct);
        for (int m = 0; m < kModSlots; ++m) { reg("m" + std::to_string(m) + "_src", &P.msrc[m]); reg("m" + std::to_string(m) + "_dst", &P.mdst[m]); reg("m" + std::to_string(m) + "_amt", &P.mamt[m]); }
        for (int m = 0; m < 4; ++m) reg("mc" + std::to_string(m + 1), &P.macro[m]);
        reg("arp_on", &P.arpOn); reg("arp_mode", &P.arpMode); reg("arp_rate", &P.arpRate); reg("arp_oct", &P.arpOct); reg("arp_gate", &P.arpGate); reg("arp_hold", &P.arpHold);
        reg("vel_amt", &P.velAmt); reg("bend_rng", &P.bendRange); reg("bend", &P.bend); reg("expr", &P.expr);
        // voices
        for (int k = 0; k < NV; ++k) { reg("sn" + std::to_string(k), &vo[(size_t) k].note); reg("sv" + std::to_string(k), &vo[(size_t) k].vel); }
        // (sg and hn need an int / a held list: special slots)
        for (int k = 0; k < NV; ++k) { idx["sg" + std::to_string(k)] = (int) slot.size(); slot.push_back(&trigF[(size_t) k]); }
        for (int nn = 0; nn < 128; ++nn) { idx["hn" + std::to_string(nn)] = (int) slot.size(); slot.push_back(&held[(size_t) nn]); }
        firstSg = idx["sg0"]; firstHn = idx["hn0"];
    }
public:
    // a trigger / held-note change comes through setp as a float: turn it into what the voices use
    void setParam(int i, double v) {
        if (i >= firstSg && i < firstSg + NV) { vo[(size_t) (i - firstSg)].trig = (int) std::lround(v); return; }
        if (i >= firstHn && i < firstHn + 128) {
            int nn = i - firstHn;
            if (v > 0) {
                // HOLD: a new chord replaces the old - but notes tapped one after another (a screen: C, then E, then G)
                // join the chord; only after a pause (a second with no new note) does the next note start a new one
                if (P.arpHold > 0.5f && downCount == 0 && samplesNow - lastDownAt > (long) sr) { for (int x : heldOrder) held[(size_t) x] = 0; heldOrder.clear(); }
                lastDownAt = samplesNow;
                ++downCount;
                if (held[(size_t) nn] <= 0) heldOrder.push_back(nn);
                held[(size_t) nn] = (float) v;
            } else {
                downCount = std::max(0, downCount - 1);
                if (P.arpHold < 0.5f) { held[(size_t) nn] = 0; heldOrder.erase(std::remove(heldOrder.begin(), heldOrder.end(), nn), heldOrder.end()); }
            }
            return;
        }
        setp(i, v);
    }
    int countActive() const { int c = 0; for (auto& v : vo) c += v.st[0] != 0; return c; }
    std::atomic<int> arpSteps { 0 }, arpLast { -1 };            // (tests: how many notes the arpeggiator played, the last one)
    std::atomic<float> clockBpm { 0 };                           // (tests: the tempo the synth hears)
private:
    std::array<float, NV> trigF {};
    int firstSg = 0, firstHn = 0;
    int downCount = 0;
    long samplesNow = 0, lastDownAt = -100000000;
    bool lfoRetrig() const { return false; }

    float lfoTick(int l, double clock, float isr) {
        float rate = std::clamp(P.lrate[l] + lfoRateMod[l], 0.0f, 100.0f);
        double ph;
        if (P.lsync[l] > 0.5f) {
            static const float BPC[7] = { 16, 8, 4, 2, 1, 0.5f, 0.25f };
            float bpc = BPC[std::clamp((int) (rate / 100.0f * 6.99f), 0, 6)];
            ph = clock / bpc; ph -= std::floor(ph);
        } else {
            float hz = 0.05f * std::pow(400.0f, rate / 100.0f);
            ph = lph[(size_t) l] + hz * isr; ph -= std::floor(ph);
        }
        int shapes = l == 0 ? 5 : 6;
        int s = std::clamp((int) (P.lshape[l] / 100.0f * (shapes - 0.01f)), 0, shapes - 1);
        float out;
        bool wrapped = ph < lph[(size_t) l];
        if (wrapped) { lshPrev[(size_t) l] = lshNext[(size_t) l]; lshNext[(size_t) l] = rngDist(rng); lsh[(size_t) l] = lshNext[(size_t) l]; }
        switch (s) {
            case 0: out = std::sin(6.2831853f * (float) ph); break;
            case 1: out = 1.0f - 4.0f * std::abs((float) ph - 0.5f); break;
            case 2: out = 2.0f * (float) ph - 1.0f; break;
            case 3: out = ph < 0.5 ? 1.0f : -1.0f; break;
            case 4: out = lsh[(size_t) l]; break;
            default: { float t = (float) ph; t = t * t * (3 - 2 * t); out = lshPrev[(size_t) l] + (lshNext[(size_t) l] - lshPrev[(size_t) l]) * t; }
        }
        lph[(size_t) l] = ph;
        return out;
    }
    float lfoRateMod[3] { 0, 0, 0 };

    void modulate(Voice& v, const float* lfo) {
        std::fill(std::begin(v.mod), std::end(v.mod), 0.0f);
        float g[3] = { 0, 0, 0 };
        for (int m = 0; m < kModSlots; ++m) {
            int s = (int) P.msrc[m], d = (int) P.mdst[m];
            if (s <= 0 || d <= 0 || d >= NM || std::abs(P.mamt[m]) < 0.01f) continue;
            float x = 0; bool global = true;
            switch (s) {
                case 1: x = lfo[0]; break; case 2: x = lfo[1]; break; case 3: x = lfo[2]; break;
                case 4: x = v.env[1]; global = false; break; case 5: x = v.env[2]; global = false; break;
                case 6: x = v.velo; global = false; break; case 7: x = (v.pitch - 60.0f) / 24.0f; global = false; break;
                case 8: x = exprSm; break; case 9: x = v.rnd; global = false; break;
                default: x = P.macro[std::clamp(s - 10, 0, 3)] / 100.0f;
            }
            float amt = P.mamt[m] / 100.0f * synthModDests()[(size_t) d].range;
            v.mod[d] += x * amt;
            if (global && d >= 21 && d <= 23) g[d - 21] += x * amt;
        }
        if (&v == &vo[0] || countActive() <= 1) for (int l = 0; l < 3; ++l) lfoRateMod[l] = g[l];
    }

    inline float blep(float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    float oscSample(Voice& v, int o, int u, int mode, float inc, float wave, float warp) {
        float& p = v.ph[o][u];
        float pPrev = p;
        p += inc; if (p >= 1.0f) p -= 1.0f;
        switch (mode) {
            case 1: {   // FM: a sine bent by a second sine (WARP = ratio, WAVE = depth)
                static const float R[8] = { 0.5f, 1, 1.5f, 2, 3, 4, 5, 7 };
                float ratio = R[std::clamp((int) (warp / 100.0f * 7.99f), 0, 7)];
                float& pm = v.ph2[o][u]; pm += inc * ratio; pm -= std::floor(pm);
                float index = wave / 100.0f * 6.0f;
                return std::sin(6.2831853f * p + index * std::sin(6.2831853f * pm));
            }
            case 2: {   // FOLD: a sine folded back on itself (WAVE = how much, WARP = lean)
                float x = std::sin(6.2831853f * p) * (1.0f + wave / 100.0f * 7.0f) + warp / 100.0f * 1.5f;
                return std::sin(x);
            }
            case 3: {   // SYNC: a saw restarted by the note (WARP = how high it sweeps)
                float ratio = 1.0f + warp / 100.0f * 7.0f;
                float& ps = v.ph2[o][u];
                bool reset = p < pPrev;
                if (reset) ps = p * ratio; else { ps += inc * ratio; ps -= std::floor(ps); }
                float s = 2.0f * ps - 1.0f - blep(ps, std::min(0.45f, inc * ratio));
                float bright = wave / 100.0f;
                return s * (0.5f + 0.5f * bright) + std::sin(6.2831853f * p) * (0.5f - 0.5f * bright);
            }
            default: {  // WAVE: sine - triangle - saw - square - pulse, with WARP (phase distortion)
                float wpos = wave / 25.0f; int wi = std::min(3, (int) wpos); float wf = wpos - wi;
                float q = warp > 0.5f ? std::pow(p, 1.0f + warp / 100.0f * 4.0f) : p;
                bool bl = warp <= 0.5f;
                float w[5];
                w[0] = std::sin(6.2831853f * q);
                w[1] = 1.0f - 4.0f * std::abs(q - 0.5f);
                float b1 = blep(p, inc);
                w[2] = 2.0f * q - 1.0f - (bl ? b1 : 0.0f);
                float p2 = p + 0.5f; if (p2 >= 1) p2 -= 1;
                w[3] = (q < 0.5f ? 1.0f : -1.0f) + (bl ? b1 - blep(p2, inc) : 0.0f);
                float p3 = p + 0.88f; if (p3 >= 1) p3 -= 1;
                w[4] = ((q < 0.12f ? 1.0f : -1.0f) + (bl ? b1 - blep(p3, inc) : 0.0f)) * 0.8f + 0.6f;
                return w[wi] + (w[wi + 1] - w[wi]) * wf;
            }
        }
    }

    // ---- the arpeggiator: the held notes, in tempo, on voice 0..NV round robin
    void releaseArp() { if (arpNote >= 0) { if (chordVoices > 0) { for (int k = 0; k < std::min(chordVoices, NV); ++k) vo[(size_t) k].vel = 0; chordVoices = 0; } else vo[(size_t) arpVoice].vel = 0; arpNote = -1; } }
    void arpTick(double clock) {
        static const double DIV[7] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 2.0 };   // beats per step
        double step = DIV[std::clamp((int) P.arpRate, 0, 6)];
        double pos = std::floor(clock / step);
        double frac = clock / step - pos;
        if (arpNote >= 0 && frac >= P.arpGate / 100.0f && P.arpGate < 99.5f) releaseArp();
        if (pos == arpPos) return;
        arpPos = pos;
        // the notes to walk through
        std::vector<int> notes;
        for (int nn : heldOrder) if (held[(size_t) nn] > 0) notes.push_back(nn);
        if (notes.empty()) { releaseArp(); return; }
        int mode = std::clamp((int) P.arpMode, 0, 5);
        if (mode != 4) std::sort(notes.begin(), notes.end());
        int oct = std::clamp((int) P.arpOct, 1, 4);
        std::vector<int> seq;
        for (int o = 0; o < oct; ++o) for (int nn : notes) seq.push_back(nn + 12 * o);
        releaseArp();
        if (mode == 5) {            // chord: all of them on every step
            int k = 0;
            for (int nn : notes) { auto& v = vo[(size_t) (k % NV)]; v.note = (float) nn; v.vel = std::max(1.0f, held[(size_t) nn]); v.trig++; ++k; }
            arpNote = notes[0]; arpVoice = 0;   // (released together: voice 0 marks it)
            chordVoices = (int) notes.size();
            return;
        }
        if (chordVoices > 0) { for (int k = 0; k < std::min(chordVoices, NV); ++k) vo[(size_t) k].vel = 0; chordVoices = 0; }
        int n = (int) seq.size();
        int i;
        if (mode == 0 || mode == 4) i = arpIdx % n;
        else if (mode == 1) i = n - 1 - (arpIdx % n);
        else if (mode == 2) { int period = std::max(1, 2 * n - 2); int t = arpIdx % period; i = t < n ? t : period - t; }
        else i = (int) (std::abs(rngDist(rng)) * n) % n;
        ++arpIdx;
        int nn = seq[(size_t) std::clamp(i, 0, n - 1)];
        arpVoice = (arpVoice + 1) % NV;
        auto& v = vo[(size_t) arpVoice];
        int base = ((nn - notes[0]) % 12 + 12) % 12; (void) base; float vv = held[(size_t) std::clamp(nn, 0, 127)]; if (vv <= 0) vv = held[(size_t) std::clamp(notes[0], 0, 127)];
        v.note = (float) nn; v.vel = std::max(1.0f, vv > 0 ? vv : 100.0f); v.trig++;
        arpNote = nn; arpSteps.fetch_add(1); arpLast.store(nn);
    }
    int chordVoices = 0;
};

} // namespace arco
