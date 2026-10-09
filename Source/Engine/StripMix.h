#pragma once
// ARCO 1.2: the mixing desk behind every strip - high-pass, three-band EQ (low shelf, bell, high shelf), compressor,
// trim - and the master polish (tilt EQ, gain, true-peak-safe limiter). AUTO MIX sets them; STUDIO shows them.
// Message buses "st_mix" (strips) and "st_mst" (master). Flat settings cost nothing (the stage is skipped).
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace arco {

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float tick(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void set(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    void flat() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    // RBJ cookbook
    void highpass(double sr, double f, double q) { double w = 2 * M_PI * std::min(f, sr * 0.45) / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
    void peak(double sr, double f, double q, double db) { double A = std::pow(10, db / 40), w = 2 * M_PI * std::min(f, sr * 0.45) / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
        set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); }
    void lowShelf(double sr, double f, double db) { double A = std::pow(10, db / 40), w = 2 * M_PI * std::min(f, sr * 0.45) / sr, c = std::cos(w), s = std::sin(w), al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa), (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa); }
    void highShelf(double sr, double f, double db) { double A = std::pow(10, db / 40), w = 2 * M_PI * std::min(f, sr * 0.45) / sr, c = std::cos(w), s = std::sin(w), al = s / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * al;
        set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa); }
};

// the settings of one strip (names: "mx_<field><strip>", e.g. "mx_hp4")
struct MixSettings {
    float hp = 0;                  // high-pass Hz (0 = off), 12 dB / octave
    float lsg = 0, lsf = 120;      // low shelf dB, Hz
    float pkg = 0, pkf = 400, pkq = 1.0f;   // bell dB, Hz, Q
    float hsg = 0, hsf = 8000;     // high shelf dB, Hz
    float p2g = 0, p2f = 2500, p2q = 1.0f;  // a second bell (1.3)
    float cth = 0, crt = 1, cat = 10, crl = 120, cmk = 0;   // compressor: threshold dB (0 = off), ratio, attack ms, release ms, make-up dB
    float trim = 0;                // dB
};
inline const std::vector<std::string>& mixFields() {
    static const std::vector<std::string> f = { "hp", "lsg", "lsf", "pkg", "pkf", "pkq", "hsg", "hsf", "cth", "crt", "cat", "crl", "cmk", "trim", "p2g", "p2f", "p2q" };
    return f;
}
inline float& mixField(MixSettings& m, int i) {
    float* p[17] = { &m.hp, &m.lsg, &m.lsf, &m.pkg, &m.pkf, &m.pkq, &m.hsg, &m.hsf, &m.cth, &m.crt, &m.cat, &m.crl, &m.cmk, &m.trim, &m.p2g, &m.p2f, &m.p2q };
    return *p[std::clamp(i, 0, 16)];
}

class StripMix {
public:
    static constexpr int NS = 12;
    std::array<std::atomic<float>, NS> grDb {};      // compressor gain reduction, for the meters
    void init(double sampleRate) { sr = sampleRate; for (auto& s : st) { s = {}; s.dirty = true; } for (auto& g : grDb) g.store(0); }
    int paramIndex(const std::string& n) const {
        if (n.rfind("mx_", 0) != 0) return -1;
        auto& F = mixFields();
        for (int f = 0; f < (int) F.size(); ++f) {
            auto& k = F[(size_t) f];
            if (n.compare(3, k.size(), k) == 0 && n.size() > 3 + k.size() && std::isdigit((unsigned char) n[3 + k.size()])) {
                int s = std::atoi(n.c_str() + 3 + k.size());
                if (s >= 0 && s < NS) return f * NS + s;
            }
        }
        return -1;
    }
    void setp(int i, double v) { int f = i / NS, s = i % NS; mixField(st[(size_t) s].m, f) = (float) v; st[(size_t) s].dirty = true; }

    // in[s] -> out[s] (may be the same buffer)
    void process(const float* const* in, float* const* out, int n) {
        for (int s = 0; s < NS; ++s) {
            auto& S = st[(size_t) s];
            if (in[s] == nullptr || out[s] == nullptr) continue;
            if (S.dirty) design(S);
            if (!S.active) { if (in[s] != out[s]) std::memcpy(out[s], in[s], sizeof(float) * (size_t) n); grDb[(size_t) s].store(0); S.gSm = 1; continue; }
            const float* x = in[s]; float* y = out[s];
            float minG = 1.0f;
            for (int i = 0; i < n; ++i) {
                float v = x[i];
                if (S.useHp) v = S.hp2.tick(S.hp1.tick(v));
                if (S.useLs) v = S.ls.tick(v);
                if (S.usePk) v = S.pk.tick(v);
                if (S.useP2) v = S.p2.tick(v);
                if (S.useHs) v = S.hs.tick(v);
                float g = S.trimG;
                if (S.useComp) {
                    // level in dB (peak detector with attack / release), soft knee
                    float a = std::abs(v);
                    float coef = a > S.env ? S.att : S.rel;
                    S.env += coef * (a - S.env);
                    float lev = 20.0f * std::log10(std::max(S.env, 1e-6f));
                    float over = lev - S.m.cth, knee = 6.0f, red = 0;
                    if (over > knee * 0.5f) red = over * (1.0f - 1.0f / S.m.crt);
                    else if (over > -knee * 0.5f) { float t = over + knee * 0.5f; red = (1.0f - 1.0f / S.m.crt) * t * t / (2 * knee); }
                    float cg = std::pow(10.0f, (-red + S.m.cmk) / 20.0f);
                    S.gSm += 0.02f * (cg - S.gSm);             // a tiny smoothing against zipper noise
                    g *= S.gSm;
                    minG = std::min(minG, std::pow(10.0f, -red / 20.0f));
                }
                y[i] = v * g;
            }
            grDb[(size_t) s].store(S.useComp ? -20.0f * std::log10(std::max(minG, 1e-6f)) : 0.0f, std::memory_order_relaxed);
        }
    }
private:
    struct Strip {
        MixSettings m; bool dirty = true, active = false, useHp = false, useLs = false, usePk = false, useP2 = false, useHs = false, useComp = false;
        Biquad hp1, hp2, ls, pk, p2, hs; float env = 0, att = 0, rel = 0, trimG = 1, gSm = 1;
    };
    std::array<Strip, NS> st;
    double sr = 48000;
    void design(Strip& S) {
        S.dirty = false;
        auto& m = S.m;
        S.useHp = m.hp >= 15.0f; if (S.useHp) { S.hp1.highpass(sr, m.hp, 0.5412); S.hp2.highpass(sr, m.hp, 1.3066); }   // 24 dB / oct Butterworth
        S.useLs = std::abs(m.lsg) > 0.05f; if (S.useLs) S.ls.lowShelf(sr, std::max(20.0f, m.lsf), m.lsg);
        S.usePk = std::abs(m.pkg) > 0.05f; if (S.usePk) S.pk.peak(sr, std::max(20.0f, m.pkf), std::clamp(m.pkq, 0.2f, 8.0f), m.pkg);
        S.useP2 = std::abs(m.p2g) > 0.05f; if (S.useP2) S.p2.peak(sr, std::max(20.0f, m.p2f), std::clamp(m.p2q, 0.2f, 8.0f), m.p2g);
        S.useHs = std::abs(m.hsg) > 0.05f; if (S.useHs) S.hs.highShelf(sr, std::max(500.0f, m.hsf), m.hsg);
        S.useComp = m.cth < -0.5f && m.crt > 1.01f;
        S.att = 1.0f - std::exp(-1.0f / (float) (std::max(0.1f, m.cat) * 0.001 * sr));
        S.rel = 1.0f - std::exp(-1.0f / (float) (std::max(5.0f, m.crl) * 0.001 * sr));
        S.trimG = std::pow(10.0f, m.trim / 20.0f);
        S.active = S.useHp || S.useLs || S.usePk || S.useP2 || S.useHs || S.useComp || std::abs(m.trim) > 0.01f;
        if (!S.useComp) S.gSm = 1;
    }
};

// the master polish: tilt EQ, gain into a look-ahead limiter (ceiling -1 dB), on mainL / mainR in place
class MasterMix {
public:
    std::atomic<float> grDb { 0 };
    void init(double sampleRate) {
        sr = sampleRate; la = std::max(8, (int) (0.0015 * sr));
        for (auto& d : dl) d.assign((size_t) la + 1, 0.0f);
        needQ.assign((size_t) la, 1.0f); rQ.assign((size_t) la, 1.0f); rSum = (double) la; wpos = 0; qpos = 0; rState = 1;
        dirty = true; gain = 1;
    }
    int paramIndex(const std::string& n) const {
        static const char* N[5] = { "ms_lsg", "ms_hsg", "ms_gain", "ms_lim", "ms_ceil" };
        for (int i = 0; i < 5; ++i) if (n == N[i]) return i;
        return -1;
    }
    void setp(int i, double v) { switch (i) { case 0: lsg = (float) v; break; case 1: hsg = (float) v; break; case 2: gdb = (float) v; break; case 3: lim = v > 0.5; break; case 4: ceil = (float) v; break; default: break; } dirty = true; }
    void process(float* L, float* R, int n) {
        if (dirty) design();
        if (!active) { grDb.store(0); return; }
        const float c = std::pow(10.0f, ceil / 20.0f);
        const float rel = 1.0f - std::exp(-1.0f / (float) (0.06 * sr));
        float minG = 1;
        for (int i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            if (useLs) { l = lsL.tick(l); r = lsR.tick(r); }
            if (useHs) { l = hsL.tick(l); r = hsR.tick(r); }
            gain += 0.0005f * (gTarget - gain);
            l *= gain; r *= gain;
            if (lim) {
                // look-ahead limiter: the minimum gain needed within the look-ahead, released slowly, then averaged over
                // the look-ahead (so the gain is already down when the peak comes out)
                float pk = std::max(std::abs(l), std::abs(r)) * 1.06f;           // a margin for the peaks between samples
                needQ[(size_t) qpos] = pk > c ? c / pk : 1.0f;
                float wmin = 1.0f; for (float v : needQ) wmin = std::min(wmin, v);
                rState = wmin < rState ? wmin : rState + rel * (wmin - rState);
                float g = (float) (rSum / la);                                     // the average of the last la values
                rSum += rState - rQ[(size_t) qpos]; rQ[(size_t) qpos] = rState;
                qpos = (qpos + 1) % la;
                dl[0][(size_t) wpos] = l; dl[1][(size_t) wpos] = r;
                int rp = (wpos + 1) % (la + 1);
                l = dl[0][(size_t) rp] * g; r = dl[1][(size_t) rp] * g;
                l = std::clamp(l, -c, c); r = std::clamp(r, -c, c);           // never above the ceiling
                wpos = rp;
                minG = std::min(minG, g);
            }
            L[i] = l; R[i] = r;
        }
        grDb.store(-20.0f * std::log10(std::max(minG, 1e-6f)), std::memory_order_relaxed);
    }
private:
    double sr = 48000; bool dirty = true, active = false, useLs = false, useHs = false, lim = false;
    float lsg = 0, hsg = 0, gdb = 0, ceil = -1.0f, gTarget = 1, gain = 1, rState = 1;
    Biquad lsL, lsR, hsL, hsR;
    int la = 64, wpos = 0, qpos = 0; double rSum = 64; std::array<std::vector<float>, 2> dl; std::vector<float> needQ, rQ;
    void design() {
        dirty = false;
        useLs = std::abs(lsg) > 0.05f; if (useLs) { lsL.lowShelf(sr, 110, lsg); lsR.lowShelf(sr, 110, lsg); }
        useHs = std::abs(hsg) > 0.05f; if (useHs) { hsL.highShelf(sr, 9000, hsg); hsR.highShelf(sr, 9000, hsg); }
        gTarget = std::pow(10.0f, gdb / 20.0f);
        active = useLs || useHs || lim || std::abs(gdb) > 0.01f || std::abs(gain - 1) > 0.001f;
    }
};

} // namespace arco
