#pragma once
// SCORE - small DSP building blocks used by the effects and instruments.
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace score {

constexpr double kPi = 3.14159265358979323846;
constexpr float kPiF = 3.14159265358979f;
constexpr float kTwoPiF = 6.28318530717959f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float dbToGain(float db) { return db <= -96.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return g <= 1e-6f ? -120.0f : 20.0f * std::log10(g); }
inline float noteToHz(float n) { return 440.0f * std::pow(2.0f, (n - 69.0f) / 12.0f); }
inline float hzToNote(float f) { return 69.0f + 12.0f * std::log2(std::max(1e-3f, f) / 440.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// a soft clipper that is exact enough and cheap (rational tanh)
inline float softClip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline bool isBad(float x) { return !(std::isfinite(x)) || std::abs(x) > 1.0e4f; }

// ---------------------------------------------------------------- random
struct Rng {
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (next() >> 8) * (1.0f / 16777216.0f); }            // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                                // -1..1
};

// ---------------------------------------------------------------- smoothing
struct Smooth {
    float cur = 0, target = 0, coef = 0.001f;
    void setTime(double sr, double ms) { coef = (float) (1.0 - std::exp(-1000.0 / (std::max(0.05, ms) * sr))); }
    void reset(float v) { cur = target = v; }
    float next() { cur += (target - cur) * coef; return cur; }
    // advance n samples at once (for block-rate use)
    float skip(int n) { float k = 1.0f - std::pow(1.0f - coef, (float) n); cur += (target - cur) * k; return cur; }
};

// ---------------------------------------------------------------- one-pole filters
struct OnePole {
    float a = 0, z = 0;
    void setHz(double sr, double hz) { a = (float) std::exp(-2.0 * kPi * std::min(hz, sr * 0.49) / sr); }
    float lp(float x) { z = x + (z - x) * a; return z; }
    float hp(float x) { return x - lp(x); }
    void reset() { z = 0; }
};

// ---------------------------------------------------------------- biquad (RBJ cookbook)
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, BP, Notch, Peak, LowShelf, HighShelf, AllPass };
    void set(Type t, double sr, double f, double q, double gainDb = 0) {
        f = std::max(5.0, std::min(f, sr * 0.49));
        q = std::max(0.05, q);
        double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * kPi * f / sr, cs = std::cos(w), sn = std::sin(w), al = sn / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t) {
            case LP: B0 = (1 - cs) / 2; B1 = 1 - cs; B2 = (1 - cs) / 2; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case HP: B0 = (1 + cs) / 2; B1 = -(1 + cs); B2 = (1 + cs) / 2; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case BP: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case Notch: B0 = 1; B1 = -2 * cs; B2 = 1; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case AllPass: B0 = 1 - al; B1 = -2 * cs; B2 = 1 + al; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case Peak: B0 = 1 + al * A; B1 = -2 * cs; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cs; A2 = 1 - al / A; break;
            case LowShelf: { double s2 = 2 * std::sqrt(A) * al;
                B0 = A * ((A + 1) - (A - 1) * cs + s2); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - s2);
                A0 = (A + 1) + (A - 1) * cs + s2; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - s2; break; }
            case HighShelf: { double s2 = 2 * std::sqrt(A) * al;
                B0 = A * ((A + 1) + (A - 1) * cs + s2); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - s2);
                A0 = (A + 1) - (A - 1) * cs + s2; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - s2; break; }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    float process(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    // magnitude at f (for drawing EQ curves)
    double magnitude(double f, double sr) const {
        double w = 2.0 * kPi * f / sr; double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt((nr * nr + ni * ni) / std::max(1e-20, dr * dr + di * di));
    }
};

// ---------------------------------------------------------------- state-variable filter (topology preserving, stable under modulation)
struct SVF {
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;
    void set(double sr, double f, double res /*0..1*/) {
        f = std::max(10.0, std::min(f, sr * 0.48));
        g = (float) std::tan(kPi * f / sr);
        k = (float) (2.0 - 1.96 * std::min(1.0, std::max(0.0, res)));
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void tick(float x) {
        float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = x - k * v1 - v2;
        if (isBad(ic1) || isBad(ic2)) { ic1 = ic2 = 0; }
    }
    float notch() const { return lp + hp; }
    float peak() const { return lp - hp; }
    float bpn() const { return bp * k; }        // normalised band-pass
    void reset() { ic1 = ic2 = lp = bp = hp = 0; }
};

// ---------------------------------------------------------------- 4-pole ladder (Moog-like, with tanh in the loop)
struct Ladder {
    float s[4] = { 0, 0, 0, 0 }, g = 0, res = 0, comp = 0.5f;
    void set(double sr, double f, double r) {
        f = std::max(10.0, std::min(f, sr * 0.45));
        double wc = 2.0 * kPi * f / sr; g = (float) (0.9892 * wc - 0.4342 * wc * wc + 0.1381 * wc * wc * wc - 0.0202 * wc * wc * wc * wc);
        res = (float) (std::min(1.0, std::max(0.0, r)) * 4.0 * (1.0029 + 0.0526 * wc - 0.926 * wc * wc + 0.0218 * wc * wc * wc));
    }
    float process(float x) {
        float in = softClip(x - res * (s[3] - comp * x));
        s[0] += g * (in - s[0]); s[1] += g * (s[0] - s[1]); s[2] += g * (s[1] - s[2]); s[3] += g * (s[2] - s[3]);
        if (isBad(s[3])) reset();
        return s[3] * (1.0f + res * 0.25f);
    }
    void reset() { s[0] = s[1] = s[2] = s[3] = 0; }
};

// ---------------------------------------------------------------- delay line with fractional read
struct DelayLine {
    std::vector<float> b; int mask = 0, w = 0;
    void setMax(int samples) { int n = 1; while (n < samples + 4) n <<= 1; b.assign((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void push(float x) { b[(size_t) w] = x; w = (w + 1) & mask; }
    float tap(int d) const { return b[(size_t) ((w - 1 - d) & mask)]; }
    // d >= 0 samples ago (0 = the sample pushed last), linear interpolation
    float read(float d) const {
        d = std::max(0.0f, std::min(d, (float) (mask - 2)));
        int i = (int) d; float f = d - (float) i;
        float a = b[(size_t) ((w - 1 - i) & mask)], c = b[(size_t) ((w - 2 - i) & mask)];
        return a + (c - a) * f;
    }
    // cubic (Hermite) interpolation, for pitch / modulated lines
    float readCubic(float d) const {
        d = std::max(1.0f, std::min(d, (float) (mask - 4)));
        int i = (int) d; float f = d - (float) i;
        float xm1 = b[(size_t) ((w - i) & mask)], x0 = b[(size_t) ((w - 1 - i) & mask)], x1 = b[(size_t) ((w - 2 - i) & mask)], x2 = b[(size_t) ((w - 3 - i) & mask)];
        float c0 = x0, c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    void clear() { std::fill(b.begin(), b.end(), 0.0f); }
    int size() const { return mask + 1; }
};

// ---------------------------------------------------------------- allpass (for diffusion / phaser)
struct Allpass1 {      // first-order allpass, coefficient a
    float z = 0;
    float process(float x, float a) { float y = -a * x + z; z = x + a * y; return y; }
    void reset() { z = 0; }
};
struct SchroederAP {   // delay-based allpass
    DelayLine d; int len = 1; float g = 0.6f;
    void init(int n, float gg) { len = n; g = gg; d.setMax(n + 2); }
    float process(float x) { float v = d.tap(len - 1); float w = x + g * v; d.push(w); return v - g * w; }
    void clear() { d.clear(); }
};

// ---------------------------------------------------------------- LFO shapes
enum class Shape { Sine, Triangle, SawUp, SawDown, Square, SampleHold, Smooth };
inline float lfoShape(Shape s, float ph /*0..1*/, float held, float prevHeld) {
    switch (s) {
        case Shape::Sine: return std::sin(kTwoPiF * ph);
        case Shape::Triangle: return ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f;
        case Shape::SawUp: return ph * 2.0f - 1.0f;
        case Shape::SawDown: return 1.0f - ph * 2.0f;
        case Shape::Square: return ph < 0.5f ? 1.0f : -1.0f;
        case Shape::SampleHold: return held;
        case Shape::Smooth: { float t = ph * ph * (3.0f - 2.0f * ph); return prevHeld + (held - prevHeld) * t; }
    }
    return 0.0f;
}

// ---------------------------------------------------------------- envelope follower
struct EnvFollow {
    float env = 0, att = 0.01f, rel = 0.001f;
    void set(double sr, double attMs, double relMs) {
        att = (float) (1.0 - std::exp(-1000.0 / (std::max(0.01, attMs) * sr)));
        rel = (float) (1.0 - std::exp(-1000.0 / (std::max(0.01, relMs) * sr))); }
    float process(float x) { float a = std::abs(x); env += (a - env) * (a > env ? att : rel); return env; }
};

// ---------------------------------------------------------------- tempo divisions (for synced rates)
struct Division { const char* name; double beats; };
inline const std::vector<Division>& divisions() {
    static const std::vector<Division> d = {
        { "4 bars", 16.0 }, { "2 bars", 8.0 }, { "1 bar", 4.0 }, { "1/2", 2.0 }, { "1/2.", 3.0 }, { "1/2T", 4.0 / 3.0 },
        { "1/4", 1.0 }, { "1/4.", 1.5 }, { "1/4T", 2.0 / 3.0 }, { "1/8", 0.5 }, { "1/8.", 0.75 }, { "1/8T", 1.0 / 3.0 },
        { "1/16", 0.25 }, { "1/16.", 0.375 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/64", 0.0625 } };
    return d;
}
inline std::vector<std::string> divisionNames() { std::vector<std::string> v; for (auto& d : divisions()) v.push_back(d.name); return v; }
inline double divisionBeats(int i) { auto& d = divisions(); return d[(size_t) std::max(0, std::min((int) d.size() - 1, i))].beats; }

// ---------------------------------------------------------------- what the effects know about time
struct TimeInfo {
    double sr = 48000.0;
    double bpm = 120.0;
    double beat = 0.0;          // position in beats (quarter notes) at the start of the block
    bool playing = false;
    int beatsPerBar = 4;
};

// ---------------------------------------------------------------- pitch detection (YIN on a 4x down-sampled signal)
// Used by HALO (harmoniser), the tuner and FOLLOW (an instrument that plays the notes you sing / bow).
struct PitchTracker {
    double sr = 48000.0; int dec = 4;
    std::vector<float> buf, d; int w = 0, hop = 0, count = 0;
    float acc = 0; int accN = 0;
    float period = 0;       // in input samples; 0 = no pitch
    float hz = 0, level = 0, clarity = 0;
    int lost = 0;
    OnePole lpA, lpB;
    void prepare(double s) {
        sr = s; dec = sr > 60000 ? 8 : 4; buf.assign(2048, 0.0f); d.assign(400, 0.0f); w = 0; hop = 0; count = 0; acc = 0; accN = 0;
        period = 0; hz = 0; level = 0; lost = 0; lpA.setHz(sr, 1500); lpB.setHz(sr, 1500); }
    void push(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            float v = lpB.lp(lpA.lp(x[i]));
            level = std::max(std::abs(x[i]), level * 0.9997f);
            acc += v; if (++accN < dec) continue;
            buf[(size_t) w] = acc / (float) dec; buf[(size_t) (w + 1024)] = buf[(size_t) w];
            w = (w + 1) & 1023; acc = 0; accN = 0;
            if (++hop >= 128) { hop = 0; analyse(); }
        }
    }
    void analyse() {
        const int W = 300; double fsd = sr / dec;
        int tmin = std::max(4, (int) (fsd / 1600.0)), tmax = std::min(380, (int) (fsd / 50.0));
        int base = w + 1023;      // the newest sample, read without wrapping (buf is written twice)
        double run = 0; int best = 0;
        if (level < 0.003f) { found(0); return; }
        for (int t = 1; t <= tmax; ++t) {
            double s = 0;
            for (int j = 0; j < W; ++j) { float a = buf[(size_t) ((base - j) & 2047)], b = buf[(size_t) ((base - j - t) & 2047)]; s += (a - b) * (a - b); }
            run += s; d[(size_t) t] = (float) (run > 1e-12 ? s * t / run : 1.0);
        }
        for (int t = tmin; t < tmax; ++t)
            if (d[(size_t) t] < 0.15f && d[(size_t) t] <= d[(size_t) t - 1] && d[(size_t) t] <= d[(size_t) t + 1]) { best = t; break; }
        if (best == 0) { float mv = 1; for (int t = tmin; t < tmax; ++t) if (d[(size_t) t] < mv) { mv = d[(size_t) t]; best = t; } if (mv > 0.3f) best = 0; }
        if (best > 0) {
            float a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1], den = a - 2 * b + c;
            float off = std::abs(den) > 1e-9f ? clampf(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
            clarity = 1.0f - b;
            found((best + off) * dec);
        } else found(0);
    }
    void found(float p) {
        if (p > 0) { period = p; hz = (float) (sr / p); lost = 0; }
        else if (++lost > 6) { period = 0; hz = 0; clarity = 0; }
    }
};

} // namespace score
