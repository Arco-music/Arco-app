// ARCO 2.4: PATCHER - the signal objects (~): oscillators, math on sound, filters, delays, samples, effects, in / out - MSP's world.
#include "PatcherLib.h"
#include <map>

namespace arco { namespace px {

namespace {

static float cosTab(double ph) {      // cos(2 pi ph), from a table
    static std::vector<float> t = [] { std::vector<float> v(4097); for (int i = 0; i <= 4096; ++i) v[(size_t) i] = (float) std::cos(TWO_PI * i / 4096.0); return v; }();
    ph -= std::floor(ph); double x = ph * 4096.0; int i = (int) x; float fr = (float) (x - i);
    return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * fr;
}
static double blep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
// a time given as a note value ("4n") in Hz for an oscillator
static bool noteHz(const Atom& a, double& beats) { if (a.t != 's') return false; auto s = symStr(a.s); double n = std::atof(s.c_str()); if (n <= 0 || s.find('n') == std::string::npos) return false;
    beats = 4.0 / n; if (s.find('t') != std::string::npos) beats *= 2.0 / 3.0; if (s.find('d') != std::string::npos) beats *= 1.5; return true; }

// ================================================================== making sound
struct Cycle : Obj {
    double ph = 0; int buf = 0;
    explicit Cycle(const Args& a) { setIO(2, 1); sigInlet(0, (float) argF(a, 0, 0)); sigInlet(1, 0); sigOutlet(0); buf = argS(a, a.size() && a[0].t == 's' ? 0 : 1, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::set && m.n) { buf = m.a[0].s; return; } if (in == 1 && m.isNum()) { ph = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        double isr = 1.0 / c.sr; BufPtr b = buf ? c.buffer(buf) : nullptr;
        bool phaseIn = connectedIn[1].load();
        for (int i = 0; i < n; ++i) {
            double p = ph + (phaseIn ? in[1][i] : 0.0);
            if (b && b->frames() > 1) { double x = (p - std::floor(p)) * juce::jmin<long>(512, b->frames()); out[0][i] = b->interp(0, x); }
            else out[0][i] = cosTab(p);
            ph += in[0][i] * isr; ph -= std::floor(ph);
        }
    }
};
struct Phasor : Obj {
    double ph = 0; double beats = 0;
    explicit Phasor(const Args& a) { setIO(2, 1); sigInlet(0, (float) argF(a, 0, 0)); sigOutlet(0); if (!a.empty()) noteHz(a[0], beats); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1 && m.isNum()) { ph = m.f(); return; } if (in == 0 && m.n && m.a[0].t == 's' && noteHz(m.a[0], beats)) return; if (in == 0 && m.isNum()) beats = 0; Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        if (beats > 0) {      // in time with the app: the phase follows its beat
            double b = c.beat, db = c.bpm / 60.0 / c.sr;
            for (int i = 0; i < n; ++i) { double p = (b + db * i) / beats; out[0][i] = (float) (p - std::floor(p)); }
            return;
        }
        double isr = 1.0 / c.sr;
        for (int i = 0; i < n; ++i) { out[0][i] = (float) ph; ph += in[0][i] * isr; ph -= std::floor(ph); }
    }
};
struct Saw : Obj {
    double ph = 0;
    explicit Saw(const Args& a) { setIO(1, 1); sigInlet(0, (float) argF(a, 0, 0)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { double dt = std::abs(in[0][i]) / c.sr; out[0][i] = (float) (2.0 * ph - 1.0 - blep(ph, juce::jmax(1e-9, dt))); ph += dt; if (ph >= 1) ph -= 1; }
    }
};
struct Rect : Obj {
    double ph = 0;
    explicit Rect(const Args& a) { setIO(2, 1); sigInlet(0, (float) argF(a, 0, 0)); sigInlet(1, (float) argF(a, 1, 0.5)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { double dt = juce::jmax(1e-9, std::abs((double) in[0][i]) / c.sr), w = juce::jlimit(0.01, 0.99, (double) in[1][i]);
            double v = ph < w ? 1.0 : -1.0; v += blep(ph, dt); double t2 = ph - w; if (t2 < 0) t2 += 1; v -= blep(t2, dt);
            out[0][i] = (float) v; ph += dt; if (ph >= 1) ph -= 1; }
    }
};
struct Tri : Obj {
    double ph = 0;
    explicit Tri(const Args& a) { setIO(2, 1); sigInlet(0, (float) argF(a, 0, 0)); sigInlet(1, (float) argF(a, 1, 0.5)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { double w = juce::jlimit(0.01, 0.99, (double) in[1][i]); out[0][i] = (float) (ph < w ? -1 + 2 * ph / w : 1 - 2 * (ph - w) / (1 - w)); ph += in[0][i] / c.sr; ph -= std::floor(ph); }
    }
};
struct Noise : Obj {
    unsigned s; bool pink; float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    Noise(const Args&, bool p) : pink(p) { setIO(1, 1); sigOutlet(0); s = (unsigned) (juce::Random::getSystemRandom().nextInt() | 1); }
    void msg(Ctx&, int, const Msg& m) override { if (m.sel == sym("seed")) s = (unsigned) m.f() | 1u; }
    void dsp(Ctx&, const float* const*, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { s = s * 1664525u + 1013904223u; float w = (float) ((int) (s >> 8) - 8388608) / 8388608.0f;
            if (!pink) { out[0][i] = w; continue; }
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f; out[0][i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f; b6 = w * 0.115926f; }
    }
};
struct RandSig : Obj {      // [rand~ 10]: a random value that moves to the next one (linearly) 10 times a second
    double ph = 1, a = 0, b = 0; unsigned s = 99u;
    explicit RandSig(const Args& ar) { setIO(1, 1); sigInlet(0, (float) argF(ar, 0, 1)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { ph += std::abs(in[0][i]) / c.sr; if (ph >= 1) { ph -= std::floor(ph); a = b; s = s * 1664525u + 1013904223u; b = ((int) (s >> 8) - 8388608) / 8388608.0; } out[0][i] = (float) (a + (b - a) * ph); }
    }
};
struct Sig : Obj {
    explicit Sig(const Args& a) { setIO(1, 1); sigInlet(0, (float) argF(a, 0, 0)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { std::copy(in[0], in[0] + n, out[0]); }
};
struct Click : Obj { bool fire = false; explicit Click(const Args&) { setIO(1, 1); sigOutlet(0); }
    void msg(Ctx&, int, const Msg&) override { fire = true; }
    void dsp(Ctx&, const float* const*, float* const* out, int n) override { std::fill(out[0], out[0] + n, 0.0f); if (fire) { out[0][0] = 1; fire = false; } } };
struct Train : Obj {
    double ph = 0;
    explicit Train(const Args& a) { setIO(3, 2); sigInlet(0, (float) argF(a, 0, 1000)); sigInlet(1, (float) argF(a, 1, 0.5)); sigInlet(2, 0); sigOutlet(0); }
    bool rose = false;
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { double per = juce::jmax(1.0, (double) in[0][i]); double w = juce::jlimit(0.0, 1.0, (double) in[1][i]);
            double p2 = ph + in[2][i]; p2 -= std::floor(p2); out[0][i] = p2 < w ? 1.0f : 0.0f;
            double nph = ph + 1000.0 / per / c.sr; if (nph >= 1) { nph -= 1; rose = true; } ph = nph; }
        if (rose) { rose = false; c.delay(this, 0); }
    }
    void tick(Ctx& c, int) override { c.bang(this, 1); }
};
struct CountSig : Obj {
    double v = 0, lo, hi; bool on = true;
    explicit CountSig(const Args& a) { setIO(2, 1); lo = argF(a, 0, 0); hi = argF(a, 1, 0); v = lo; sigOutlet(0); }
    void msg(Ctx&, int in, const Msg& m) override { if (in == 1) { hi = m.f(); return; } if (m.isNum()) { v = m.f(); return; } if (m.sel == S::stop) on = false; else if (m.sel == S::start || m.isBang()) { on = true; v = lo; } }
    void dsp(Ctx&, const float* const*, float* const* out, int n) override { for (int i = 0; i < n; ++i) { out[0][i] = (float) v; if (on) { v += 1; if (hi > lo && v >= hi) v = lo; } } }
};
// [line~]: glides as a signal. "1 50" goes to 1 in 50 ms; "0 0 1 10 0 500" is three segments.
struct LineSig : Obj {
    double v = 0; struct Seg { double to, ms; }; Seg segs[64]; int nSeg = 0, cur = 0; double left = 0, inc = 0; bool active = false, endSoon = false;
    explicit LineSig(const Args& a) { setIO(2, 2); v = argF(a, 0, 0); sigOutlet(0); }
    double ramp = 0;
    void startSeg(Ctx& c) { if (cur >= nSeg) { active = false; return; } auto& s = segs[cur]; double samples = s.ms * c.sr / 1000.0;
        if (samples < 1) { v = s.to; ++cur; startSeg(c); return; } inc = (s.to - v) / samples; left = samples; active = true; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { ramp = m.f(); return; }
        if (m.sel == S::stop) { active = false; return; }
        Atom at[MAXA]; int n = m.atoms(at); if (n == 0) return;
        nSeg = 0; cur = 0;
        if (n == 1) { segs[nSeg++] = { at[0].d(), ramp }; ramp = 0; }
        else for (int k = 0; k + 1 < n && nSeg < 64; k += 2) segs[nSeg++] = { at[k].d(), at[k + 1].d() };
        if (n % 2 == 1 && n > 1) segs[nSeg++] = { at[n - 1].d(), 0 };
        startSeg(c); if (!active) endSoon = true;
    }
    void dsp(Ctx& c, const float* const*, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) {
            if (active) { v += inc; if (--left <= 0) { v = segs[cur].to; ++cur; startSeg(c); if (!active) endSoon = true; } }
            out[0][i] = (float) v;
        }
        if (endSoon) { endSoon = false; c.delay(this, 0); }
    }
    void tick(Ctx& c, int) override { c.bang(this, 1); }
};
struct CurveSig : LineSig {      // [curve~]: like line~ but bent (a third number per segment: -1 .. 1)
    using LineSig::LineSig;
};
// [adsr~ 10 200 0.6 500]: an envelope. A number above 0 starts it (that loud), 0 lets it go. A signal into the left: the gate.
struct Adsr : Obj {
    double a, d, s, r, env = 0, peak = 1; int stage = 0; bool gateWas = false;
    explicit Adsr(const Args& ar) { setIO(5, 1); sigInlet(0, 0); a = argF(ar, 0, 10); d = argF(ar, 1, 100); s = argF(ar, 2, 0.7); r = argF(ar, 3, 300); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override {
        switch (in) { case 1: a = m.f(); return; case 2: d = m.f(); return; case 3: s = m.f(); return; case 4: r = m.f(); return; default: break; }
        if (m.isNum()) { double v = m.f(); if (v > 0) { peak = v; stage = 1; } else stage = 4; return; }
        if (m.isBang()) { peak = 1; stage = 1; return; }
        Obj::msg(c, in, m);
    }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        bool sigGate = connectedIn[0].load();
        double ka = 1000.0 / juce::jmax(0.1, a) / c.sr, kd = 1000.0 / juce::jmax(0.1, d) / c.sr, kr = 1000.0 / juce::jmax(0.1, r) / c.sr;
        for (int i = 0; i < n; ++i) {
            if (sigGate) { bool g = in[0][i] > 0; if (g && !gateWas) { peak = in[0][i]; stage = 1; } else if (!g && gateWas) stage = 4; gateWas = g; }
            switch (stage) {
                case 1: env += ka * peak; if (env >= peak) { env = peak; stage = 2; } break;
                case 2: env -= kd * peak; if (env <= s * peak) { env = s * peak; stage = 3; } break;
                case 3: env = s * peak; break;
                case 4: env -= kr * juce::jmax(peak, 0.001); if (env <= 0) { env = 0; stage = 0; } break;
                default: env = 0;
            }
            out[0][i] = (float) env;
        }
    }
};

// ================================================================== math on signals
using F2 = float (*)(float, float);
struct SigBin : Obj {
    F2 f; bool comm;
    SigBin(const Args& a, F2 fn, float dflt) : f(fn) { setIO(2, 1); sigInlet(0, 0); sigInlet(1, (float) argF(a, 0, dflt)); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 0 && m.isNum()) { sv[0] = (float) m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) out[0][i] = f(in[0][i], in[1][i]); }
};
using F1 = float (*)(float);
struct SigUn : Obj {
    F1 f; explicit SigUn(F1 fn) : f(fn) { setIO(1, 1); sigInlet(0, 0); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { float v = f(in[0][i]); out[0][i] = std::isfinite(v) ? v : 0.0f; } }
};
struct ClipSig : Obj { explicit ClipSig(const Args& a) { setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, -1)); sigInlet(2, (float) argF(a, 1, 1)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) out[0][i] = juce::jlimit(juce::jmin(in[1][i], in[2][i]), juce::jmax(in[1][i], in[2][i]), in[0][i]); } };
struct ScaleSig : Obj {
    explicit ScaleSig(const Args& a) { setIO(6, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, -1)); sigInlet(2, (float) argF(a, 1, 1)); sigInlet(3, (float) argF(a, 2, 0)); sigInlet(4, (float) argF(a, 3, 1)); sigInlet(5, (float) argF(a, 4, 1)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { float il = in[1][i], ih = in[2][i]; float t = ih == il ? 0 : (in[0][i] - il) / (ih - il); if (in[5][i] != 1 && t > 0) t = std::pow(t, in[5][i]); out[0][i] = in[3][i] + t * (in[4][i] - in[3][i]); } }
};
struct RoundSig : Obj { explicit RoundSig(const Args& a) { setIO(2, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { float st = in[1][i] == 0 ? 1 : in[1][i]; out[0][i] = std::round(in[0][i] / st) * st; } } };
struct ExprSig : Obj {
    Expr e; explicit ExprSig(const Args& a) { std::string t; for (auto& x : a) { if (!t.empty()) t += " "; t += x.str(); } if (!e.parse(t)) err = "expr~: " + e.err; int k = juce::jmax(1, e.maxIn); setIO(k, 1); for (int i = 0; i < k; ++i) sigInlet(i); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override {
        double v[9]; int k = juce::jmin(9, nIn);
        for (int i = 0; i < n; ++i) { for (int j = 0; j < k; ++j) v[j] = in[j][i]; double r = e.eval(v, k); out[0][i] = std::isfinite(r) ? (float) r : 0.0f; } }
};

// ================================================================== filters
struct OnePole : Obj { double y = 0; bool hp;
    OnePole(const Args& a, bool h) : hp(h) { setIO(2, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, h ? 20 : 1000)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { double k = 1.0 - std::exp(-TWO_PI * juce::jlimit(0.1, c.sr * 0.45, (double) in[1][i]) / c.sr); y += k * (in[0][i] - y); y = flush((float) y); out[0][i] = (float) (hp ? in[0][i] - y : y); } } };
// a state-variable filter (stable when its knobs move fast)
struct SvfCore { double ic1 = 0, ic2 = 0;
    void run(double x, double f, double q, double sr, double& lp, double& bp, double& hp) { double g = std::tan(juce::MathConstants<double>::pi * juce::jlimit(5.0, sr * 0.45, f) / sr), k = 1.0 / juce::jmax(0.05, q);
        double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2; double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; if (std::abs(ic1) < 1e-20) ic1 = 0; if (std::abs(ic2) < 1e-20) ic2 = 0; lp = v2; bp = v1; hp = x - k * v1 - v2; } };
struct Lores : Obj { SvfCore f; explicit Lores(const Args& a) { setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1000)); sigInlet(2, (float) argF(a, 1, 0.3)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double lp, bp, hp; f.run(in[0][i], in[1][i], 0.5 + juce::jlimit(0.0, 0.999, (double) in[2][i]) * 20.0, c.sr, lp, bp, hp); out[0][i] = (float) lp; } } };
struct Reson : Obj { SvfCore f; explicit Reson(const Args& a) { setIO(4, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1)); sigInlet(2, (float) argF(a, 1, 1000)); sigInlet(3, (float) argF(a, 2, 10)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double lp, bp, hp; f.run(in[0][i], in[2][i], juce::jmax(0.1f, in[3][i]), c.sr, lp, bp, hp); out[0][i] = (float) (bp * in[1][i]); } } };
struct Svf : Obj { SvfCore f; explicit Svf(const Args& a) { setIO(3, 4); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1000)); sigInlet(2, (float) argF(a, 1, 0.5)); for (int k = 0; k < 4; ++k) sigOutlet(k); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double lp, bp, hp; f.run(in[0][i], in[1][i], 0.5 + juce::jlimit(0.0, 0.999, (double) in[2][i]) * 20.0, c.sr, lp, bp, hp);
        out[0][i] = (float) lp; out[1][i] = (float) hp; out[2][i] = (float) bp; out[3][i] = (float) (lp + hp); } } };
struct Biquad : Obj {
    double c0 = 1, c1 = 0, c2 = 0, d1 = 0, d2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    explicit Biquad(const Args& a) { setIO(6, 1); sigInlet(0); for (int k = 1; k < 6; ++k) sigInlet(k, (float) argF(a, (size_t) k - 1, k == 1 ? 1 : 0)); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 0 && m.isList() && m.n >= 5) { for (int k = 0; k < 5; ++k) sv[(size_t) k + 1] = (float) m.f(k); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double x = in[0][i]; double y = in[1][i] * x + in[2][i] * x1 + in[3][i] * x2 - in[4][i] * y1 - in[5][i] * y2;
        if (!std::isfinite(y)) y = 0; x2 = x1; x1 = x; y2 = y1; y1 = flush((float) y); out[0][i] = (float) y; } }
};
// [filter~ lowpass 1000 0.7]: one filter of any shape, in plain words (lowpass highpass bandpass notch peak lowshelf highshelf allpass)
struct FilterSig : Obj {
    int type = 0; double x1 = 0, x2 = 0, y1 = 0, y2 = 0, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; float lf = -1, lq = -1, lg = -99;
    explicit FilterSig(const Args& a) { setIO(4, 1); sigInlet(0); size_t k = 0; if (!a.empty() && a[0].t == 's') { setType(a[0].s); k = 1; }
        sigInlet(1, (float) argF(a, k, 1000)); sigInlet(2, (float) argF(a, k + 1, 0.707)); sigInlet(3, (float) argF(a, k + 2, 0)); sigOutlet(0); }
    void setType(int s) { static const char* T[8] = { "lowpass", "highpass", "bandpass", "notch", "peak", "lowshelf", "highshelf", "allpass" }; auto& w = symStr(s); for (int i = 0; i < 8; ++i) if (w == T[i] || w == std::string(T[i]).substr(0, 2)) type = i; lf = -1; }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::symbol && m.n) { setType(m.a[0].s); return; } if (m.n == 0 && !m.isBang() && m.sel != S::list) { setType(m.sel); return; } Obj::msg(c, in, m); }
    void coef(double f, double q, double gdb, double sr) {
        double w = TWO_PI * juce::jlimit(5.0, sr * 0.45, f) / sr, cs = std::cos(w), sn = std::sin(w), al = sn / (2 * juce::jmax(0.05, q)), A = std::pow(10.0, gdb / 40.0);
        double B0, B1, B2, A0, A1, A2;
        switch (type) {
            case 1: B0 = (1 + cs) / 2; B1 = -(1 + cs); B2 = B0; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case 2: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case 3: B0 = 1; B1 = -2 * cs; B2 = 1; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            case 4: B0 = 1 + al * A; B1 = -2 * cs; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cs; A2 = 1 - al / A; break;
            case 5: { double sq = 2 * std::sqrt(A) * al; B0 = A * ((A + 1) - (A - 1) * cs + sq); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - sq); A0 = (A + 1) + (A - 1) * cs + sq; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - sq; break; }
            case 6: { double sq = 2 * std::sqrt(A) * al; B0 = A * ((A + 1) + (A - 1) * cs + sq); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - sq); A0 = (A + 1) - (A - 1) * cs + sq; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - sq; break; }
            case 7: B0 = 1 - al; B1 = -2 * cs; B2 = 1 + al; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; break;
            default: B0 = (1 - cs) / 2; B1 = 1 - cs; B2 = B0; A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) {
            if ((i & 15) == 0 && (in[1][i] != lf || in[2][i] != lq || in[3][i] != lg)) { lf = in[1][i]; lq = in[2][i]; lg = in[3][i]; coef(lf, lq, lg, c.sr); }
            double x = in[0][i], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; if (!std::isfinite(y)) y = 0;
            x2 = x1; x1 = x; y2 = y1; y1 = flush((float) y); out[0][i] = (float) y; }
    }
};
struct Cross : Obj { double y = 0; explicit Cross(const Args& a) { setIO(2, 2); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1000)); sigOutlet(0); sigOutlet(1); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double k = 1.0 - std::exp(-TWO_PI * juce::jlimit(1.0, c.sr * 0.45, (double) in[1][i]) / c.sr); y += k * (in[0][i] - y); out[0][i] = (float) y; out[1][i] = (float) (in[0][i] - y); } } };
struct Slide : Obj { double y = 0; explicit Slide(const Args& a) { setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1)); sigInlet(2, (float) argF(a, 1, 1)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double x = in[0][i], k = juce::jmax(1.0f, x > y ? in[1][i] : in[2][i]); y += (x - y) / k; out[0][i] = (float) y; } } };
struct Rampsmooth : Obj { double y = 0, step = 0; int left = 0; double target = 0; explicit Rampsmooth(const Args& a) { setIO(1, 1); sigInlet(0); up = argF(a, 0, 64); down = argF(a, 1, up); sigOutlet(0); }
    double up, down;
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == sym("rampup")) { up = m.f(); return; } if (m.sel == sym("rampdown")) { down = m.f(); return; } if (m.sel == sym("ramp")) { up = down = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double x = in[0][i]; if (x != target) { target = x; double len = juce::jmax(1.0, x > y ? up : down); step = (x - y) / len; left = (int) len; }
        if (left > 0) { y += step; if (--left == 0) y = target; } else y = target; out[0][i] = (float) y; } } };
struct DcBlock : Obj { double x1 = 0, y1 = 0; explicit DcBlock(const Args&) { setIO(1, 1); sigInlet(0); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double y = in[0][i] - x1 + 0.995 * y1; x1 = in[0][i]; y1 = flush((float) y); out[0][i] = (float) y; } } };
struct AllpassSig : Obj { std::vector<float> b; int w = 0; explicit AllpassSig(const Args& a) { setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 1, 10)); sigInlet(2, (float) argF(a, 2, 0.5)); sigOutlet(0); maxMs = argF(a, 0, 100); }
    double maxMs; void prepare(double sr) override { b.assign((size_t) (maxMs * sr / 1000.0) + 4, 0.0f); w = 0; }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { int N = (int) b.size(); if (N < 4) return;
        for (int i = 0; i < n; ++i) { double d = juce::jlimit(1.0, (double) N - 2, in[1][i] * c.sr / 1000.0); double rp = w - d; while (rp < 0) rp += N; int i0 = (int) rp; float fr = (float) (rp - i0);
            float del = b[(size_t) i0] * (1 - fr) + b[(size_t) ((i0 + 1) % N)] * fr; float g = in[2][i]; float v = in[0][i] + g * del; b[(size_t) w] = flush(v); out[0][i] = del - g * v; w = (w + 1) % N; } } };
struct CombSig : Obj { std::vector<float> xb, yb; int w = 0; double maxMs; explicit CombSig(const Args& a) { setIO(5, 1); sigInlet(0); maxMs = argF(a, 0, 100); sigInlet(1, (float) argF(a, 1, 10)); sigInlet(2, (float) argF(a, 2, 1)); sigInlet(3, (float) argF(a, 3, 0)); sigInlet(4, (float) argF(a, 4, 0.5)); sigOutlet(0); }
    void prepare(double sr) override { xb.assign((size_t) (maxMs * sr / 1000.0) + 4, 0.0f); yb = xb; w = 0; }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { int N = (int) xb.size(); if (N < 4) return;
        for (int i = 0; i < n; ++i) { double d = juce::jlimit(1.0, (double) N - 2, in[1][i] * c.sr / 1000.0); double rp = w - d; while (rp < 0) rp += N; int i0 = (int) rp; float fr = (float) (rp - i0); int i1 = (i0 + 1) % N;
            float xd = xb[(size_t) i0] * (1 - fr) + xb[(size_t) i1] * fr, yd = yb[(size_t) i0] * (1 - fr) + yb[(size_t) i1] * fr;
            float y = in[2][i] * in[0][i] + in[3][i] * xd + in[4][i] * yd; xb[(size_t) w] = in[0][i]; yb[(size_t) w] = flush(y); out[0][i] = y; w = (w + 1) % N; } } };

// ================================================================== delays and time
struct DelayLine { std::vector<float> b; int w = 0;
    void size(int n) { b.assign((size_t) juce::jmax(4, n), 0.0f); w = 0; }
    void push(float x) { b[(size_t) w] = x; w = (w + 1) % (int) b.size(); }
    float read(double d) const { int N = (int) b.size(); d = juce::jlimit(1.0, (double) N - 2, d); double rp = w - d; while (rp < 0) rp += N; int i0 = (int) rp; float fr = (float) (rp - i0); return b[(size_t) i0] * (1 - fr) + b[(size_t) ((i0 + 1) % N)] * fr; } };
static const unsigned TAP_MAGIC = 0x7a9e5u;
struct Tapin : Obj { DelayLine dl; double maxMs; explicit Tapin(const Args& a) { setIO(1, 1); sigInlet(0); sigOutlet(0); maxMs = juce::jlimit(1.0, 600000.0, argF(a, 0, 1000)); }
    void prepare(double sr) override { dl.size((int) (maxMs * sr / 1000.0) + 4); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::clear) { std::fill(dl.b.begin(), dl.b.end(), 0.0f); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override {
        // the cord to [tapout~] carries a way to this tapin (not sound)
        Tapin* self = this; std::memset(out[0], 0, sizeof(float) * (size_t) n); std::memcpy(out[0], &self, sizeof self); std::memcpy(out[0] + 2, &TAP_MAGIC, sizeof TAP_MAGIC);
        base = dl.w; for (int i = 0; i < n; ++i) dl.push(in[0][i]); }
    int base = 0;
};
struct Tapout : Obj {
    std::vector<double> times;
    explicit Tapout(const Args& a) { for (auto& x : a) times.push_back(x.d()); if (times.empty()) times.push_back(0); setIO((int) times.size(), (int) times.size()); for (int k = 0; k < nIn; ++k) { sigInlet(k, (float) times[(size_t) k]); sigOutlet(k); } }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        unsigned mg = 0; std::memcpy(&mg, in[0] + 2, sizeof mg); Tapin* t = nullptr;
        if (mg == TAP_MAGIC) std::memcpy(&t, in[0], sizeof t);
        if (t == nullptr) { for (int k = 0; k < nOut; ++k) std::fill(out[k], out[k] + n, 0.0f); return; }
        int N = (int) t->dl.b.size();
        for (int k = 0; k < nOut; ++k) for (int i = 0; i < n; ++i) {
            double ms = k == 0 ? sv[0] : in[k][i];
            double d = juce::jlimit(1.0, (double) N - 3, ms * c.sr / 1000.0);
            double rp = t->base + i - d; while (rp < 0) rp += N; while (rp >= N) rp -= N;
            int i0 = (int) rp; float fr = (float) (rp - i0);
            out[k][i] = t->dl.b[(size_t) i0] * (1 - fr) + t->dl.b[(size_t) ((i0 + 1) % N)] * fr;
        }
    }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.isNum() && in < (int) times.size()) { times[(size_t) in] = m.f(); sv[(size_t) in] = (float) m.f(); return; } Obj::msg(c, in, m); }
};
struct DelaySig : Obj { DelayLine dl; double maxS; explicit DelaySig(const Args& a) { setIO(2, 1); sigInlet(0); maxS = juce::jlimit(2.0, 48000.0 * 600, argF(a, 0, 44100)); sigInlet(1, (float) argF(a, 1, 0)); sigOutlet(0); }
    void prepare(double) override { dl.size((int) maxS + 4); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { dl.push(in[0][i]); out[0][i] = in[1][i] < 1 ? in[0][i] : dl.read(in[1][i] + 1); } } };
// [echo~ 8n 0.5 0.4]: an echo - time (ms or a note value in tempo), how much comes back, how much you hear
struct Echo : Obj { DelayLine dl; Atom time; double fb, mix; double lp = 0;
    explicit Echo(const Args& a) { setIO(4, 1); sigInlet(0); time = a.empty() ? Atom::I(375) : a[0]; fb = argF(a, 1, 0.45); mix = argF(a, 2, 0.4); sigOutlet(0); }
    void prepare(double sr) override { dl.size((int) (sr * 5.0)); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { if (m.n) time = m.a[0]; return; } if (in == 2) { fb = juce::jlimit(0.0, 0.98, m.f()); return; } if (in == 3) { mix = juce::jlimit(0.0, 1.0, m.f()); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { double d = juce::jlimit(1.0, c.sr * 4.9, c.noteMs(time, 375) * c.sr / 1000.0);
        for (int i = 0; i < n; ++i) { float del = dl.read(d); lp += 0.35 * (del - lp); dl.push(flush((float) (in[0][i] + lp * fb))); out[0][i] = (float) (in[0][i] * (1 - mix) + del * mix); } } };

// ================================================================== watching sound
struct Snapshot : Obj { double ms; float last = 0; bool on = false; explicit Snapshot(const Args& a) { setIO(2, 1); sigInlet(0); ms = argF(a, 0, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { ms = m.f(); if (ms > 0) { on = true; c.delay(this, ms); } else { on = false; c.cancel(this); } return; }
        if (m.isBang()) { c.outF(this, 0, last); return; } if (m.isNum()) { if (m.f() != 0 && ms > 0) { on = true; c.delay(this, ms); } else { on = false; c.cancel(this); } return; } Obj::msg(c, in, m); }
    void dspStarted(Ctx& c) override { if (ms > 0 && !on) { on = true; c.delay(this, ms); } }
    void dsp(Ctx&, const float* const* in, float* const*, int n) override { last = in[0][n - 1]; uiValue = last; }
    void tick(Ctx& c, int) override { c.outF(this, 0, last); if (on && ms > 0) c.delay(this, ms); } };
struct Peakamp : Obj { double ms; float pk = 0; bool on = false; explicit Peakamp(const Args& a) { setIO(2, 1); sigInlet(0); ms = argF(a, 0, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { ms = m.f(); if (ms > 0) c.delay(this, ms); return; } if (m.isBang()) { c.outF(this, 0, pk); pk = 0; return; } Obj::msg(c, in, m); }
    void dspStarted(Ctx& c) override { if (ms > 0) c.delay(this, ms); }
    void dsp(Ctx&, const float* const* in, float* const*, int n) override { for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(in[0][i])); }
    void tick(Ctx& c, int) override { c.outF(this, 0, pk); pk = 0; if (ms > 0) c.delay(this, ms); } };
struct Avg : Obj { double s = 0; long long k = 0; explicit Avg(const Args&) { setIO(1, 1); sigInlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.isBang()) { c.outF(this, 0, k ? s / (double) k : 0); s = 0; k = 0; return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const*, int n) override { for (int i = 0; i < n; ++i) s += std::abs(in[0][i]); k += n; } };
struct EdgeSig : Obj { bool was = false; int up = 0, down = 0; explicit EdgeSig(const Args&) { setIO(1, 2); sigInlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override { for (int i = 0; i < n; ++i) { bool v = in[0][i] != 0; if (v && !was) ++up; if (!v && was) ++down; was = v; } if (up || down) c.delay(this, 0); }
    void tick(Ctx& c, int) override { if (down) { down = 0; c.bang(this, 1); } if (up) { up = 0; c.bang(this, 0); } } };
struct ThreshSig : Obj { bool on = false; explicit ThreshSig(const Args& a) { setIO(3, 1); sigInlet(0); lo = argF(a, 0, 0); hi = argF(a, 1, 0); sigOutlet(0); } double lo, hi;
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { lo = m.f(); return; } if (in == 2) { hi = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { if (in[0][i] >= hi) on = true; else if (in[0][i] < lo) on = false; out[0][i] = on ? 1.0f : 0.0f; } } };
struct ChangeSig : Obj { float last = 0; explicit ChangeSig(const Args&) { setIO(1, 1); sigInlet(0); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { float x = in[0][i]; out[0][i] = x > last ? 1.0f : x < last ? -1.0f : 0.0f; last = x; } } };
struct Zerox : Obj { float last = 0; explicit Zerox(const Args&) { setIO(1, 2); sigInlet(0); sigOutlet(0); sigOutlet(1); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { int k = 0; for (int i = 0; i < n; ++i) { float x = in[0][i]; bool z = (x >= 0) != (last >= 0); out[1][i] = z ? 1.0f : 0.0f; k += z; last = x; } std::fill(out[0], out[0] + n, (float) k); } };
struct Sah : Obj { float held = 0, last = 0; explicit Sah(const Args& a) { setIO(2, 1); sigInlet(0); sigInlet(1); th = (float) argF(a, 0, 0); sigOutlet(0); } float th;
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { float t = in[1][i]; if (t > th && last <= th) held = in[0][i]; last = t; out[0][i] = held; } } };

// ================================================================== routing sound
struct SelectorSig : Obj { int which; explicit SelectorSig(const Args& a) { int k = clampN(argI(a, 0, 2), 1, 32); which = (int) argI(a, 1, 0); setIO(k + 1, 1); for (int i = 1; i <= k; ++i) sigInlet(i); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 0 && m.isNum()) { which = (int) m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { if (which >= 1 && which < nIn) std::copy(in[which], in[which] + n, out[0]); else std::fill(out[0], out[0] + n, 0.0f); } };
struct GateSig : Obj { int which; explicit GateSig(const Args& a) { int k = clampN(argI(a, 0, 1), 1, 32); which = (int) argI(a, 1, 0); setIO(2, k); sigInlet(1); for (int i = 0; i < k; ++i) sigOutlet(i); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 0 && m.isNum()) { which = (int) m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int k = 0; k < nOut; ++k) { if (k + 1 == which) std::copy(in[1], in[1] + n, out[k]); else std::fill(out[k], out[k] + n, 0.0f); } } };
struct MatrixSig : Obj {
    int ni, no; std::vector<float> g, cur;
    explicit MatrixSig(const Args& a) { ni = clampN(argI(a, 0, 2), 1, 16); no = clampN(argI(a, 1, 2), 1, 16); setIO(ni, no + 1); for (int i = 0; i < ni; ++i) sigInlet(i); for (int k = 0; k < no; ++k) sigOutlet(k); g.assign((size_t) (ni * no), 0.0f); cur = g; }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::clear) { std::fill(g.begin(), g.end(), 0.0f); return; }
        if (in == 0 && m.isList() && m.n >= 2) { int a = (int) m.f(0), b = (int) m.f(1); float v = m.n > 2 ? (float) m.f(2) : 1.0f; if (a >= 0 && a < ni && b >= 0 && b < no) g[(size_t) (a * no + b)] = v; return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int k = 0; k < no; ++k) std::fill(out[k], out[k] + n, 0.0f);
        for (int a = 0; a < ni; ++a) for (int b = 0; b < no; ++b) { auto& cg = cur[(size_t) (a * no + b)]; float tg = g[(size_t) (a * no + b)]; if (cg == 0 && tg == 0) continue;
            for (int i = 0; i < n; ++i) { cg += (tg - cg) * 0.01f; out[b][i] += in[a][i] * cg; } } }
};
struct SendSig : Obj { int name; bool throwMode; SendSig(const Args& a, bool t) : throwMode(t) { name = argS(a, 0, 0); setIO(1, 0); sigInlet(0); if (!name) err = "needs a name"; }
    int sigSendName() const override { return name; }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::set && m.n) { name = m.a[0].s; return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override { auto* b = c.sigSend(name); if (throwMode) for (int i = 0; i < n; ++i) b[i] += in[0][i]; else std::copy(in[0], in[0] + n, b); } };
struct RecvSig : Obj { int name; bool catchMode; RecvSig(const Args& a, bool ct) : catchMode(ct) { name = argS(a, 0, 0); setIO(1, 1); sigOutlet(0); }
    int sigRecvName() const override { return name; }
    void msg(Ctx&, int, const Msg& m) override { if (m.sel == S::set && m.n) name = m.a[0].s; }
    void dsp(Ctx& c, const float* const*, float* const* out, int n) override { auto* b = c.sigSend(name); std::copy(b, b + n, out[0]); if (catchMode) std::fill(b, b + n, 0.0f); } };

// ================================================================== in and out
// [dac~ 1 2]: to ARCO's mix (the PATCHER's own strip, or a LIVE strip, or - in STUDIO - the track)
struct Dac : Obj {
    std::vector<int> ch; float gain = 1.0f; bool on = true;
    explicit Dac(const Args& a) { for (auto& x : a) if (x.num()) ch.push_back((int) x.d()); if (ch.empty()) ch = { 1, 2 }; setIO((int) ch.size(), 0); for (int k = 0; k < nIn; ++k) sigInlet(k); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::stop) { on = false; return; } if (m.sel == S::start || m.sel == sym("startwindow")) { on = true; return; } if (m.isNum() && in == 0 && !connectedIn[0].load()) { on = m.f() != 0; return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override { if (!on) return; for (int k = 0; k < nIn; ++k) { int o = ch[(size_t) k] - 1; if (o < 0 || o >= 8 || c.dacOut[o] == nullptr) continue; for (int i = 0; i < n; ++i) c.dacOut[o][i] += in[k][i] * gain; } }
};
// [adc~ 1 2]: what comes in - LIVE inputs 1-4; 5 6 the station's mix; in STUDIO: the track's sound
struct Adc : Obj {
    std::vector<int> ch;
    explicit Adc(const Args& a) { for (auto& x : a) if (x.num()) ch.push_back((int) x.d()); if (ch.empty()) ch = { 1, 2 }; setIO(1, (int) ch.size()); for (int k = 0; k < nOut; ++k) sigOutlet(k); }
    void msg(Ctx&, int, const Msg&) override {}
    void dsp(Ctx& c, const float* const*, float* const* out, int n) override { for (int k = 0; k < nOut; ++k) { int s = ch[(size_t) k] - 1; if (s >= 0 && s < 8 && c.adcIn[s]) std::copy(c.adcIn[s], c.adcIn[s] + n, out[k]); else std::fill(out[k], out[k] + n, 0.0f); } }
};

// ================================================================== samples (buffer~ and its friends)
struct BufferObj : Obj {
    int name; double ms; int chans;
    explicit BufferObj(const Args& a) { name = argS(a, 0, 0); ms = argF(a, 1, 1000); chans = clampN(argI(a, 2, 1), 1, 8); setIO(1, 2); if (!name) err = "a buffer~ needs a name: [buffer~ mysound]"; }
    void dspStarted(Ctx& c) override { if (!name) return; auto b = c.buffer(name, true); if (b->frames() == 0) b->resize((long) (ms * c.sr / 1000.0), chans); }
    void msg(Ctx& c, int, const Msg& m) override {
        auto b = name ? c.buffer(name, true) : nullptr; if (!b) return;
        if (m.sel == S::size || m.sel == sym("sizeinsamps")) { long fr = m.sel == S::size ? (long) (m.f() * c.sr / 1000.0) : (long) m.f(); b->resize(juce::jmax(1L, fr), b->chans() ? b->chans() : chans); uiGen++; return; }
        if (m.sel == S::clear) { for (auto& x : b->ch) std::fill(x.begin(), x.end(), 0.0f); uiGen++; return; }
        if (m.sel == sym("normalize")) { float pk = 0; for (auto& x : b->ch) for (float v : x) pk = juce::jmax(pk, std::abs(v)); double t = m.n ? m.f() : 1.0; if (pk > 0) for (auto& x : b->ch) for (auto& v : x) v = (float) (v * t / pk); uiGen++; return; }
        if (m.sel == S::read || m.sel == sym("replace") || m.sel == sym("import")) { c.action("bufread", 0, 0, 0, symStr(name) + (m.n ? std::string("|") + m.a[0].str() : std::string()), boxId); return; }
        if (m.sel == S::write) { c.action("bufwrite", 0, 0, 0, symStr(name), boxId); return; }
        if (m.isBang()) { c.outF(this, 0, b->frames() * 1000.0 / juce::jmax(1.0, b->sr)); }
    }
    int uiArray(float* out, int cap) const override { return 0 * cap + (out ? 0 : 0); }
};
struct Record : Obj {
    int name, chans; bool on = false, loop = false, append = false; double pos = 0;
    explicit Record(const Args& a) { name = argS(a, 0, 0); chans = clampN(argI(a, 1, 1), 1, 8); setIO(chans + 2, 1); for (int k = 0; k < chans; ++k) sigInlet(k); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (m.sel == sym("loop")) { loop = m.f() != 0; return; } if (m.sel == sym("append")) { append = m.f() != 0; return; } if (m.sel == S::set && m.n) { name = m.a[0].s; return; }
        if (in == 0 && (m.isNum() || m.isBang() || m.sel == S::start || m.sel == S::stop)) { bool v = m.sel == S::stop ? false : m.isNum() ? m.f() != 0 : true; if (v && !on && !append) pos = 0; on = v; uiValue = on ? 1 : 0; return; }
        Obj::msg(c, in, m);
    }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        auto b = c.buffer(name); if (!b || b->frames() == 0) { std::fill(out[0], out[0] + n, 0.0f); return; }
        long N = b->frames();
        for (int i = 0; i < n; ++i) {
            if (on) { long p = (long) pos; if (p >= N) { if (loop) { pos = 0; p = 0; } else { on = false; } }
                if (on) { for (int k = 0; k < chans && k < b->chans(); ++k) b->ch[(size_t) k][(size_t) p] = in[k][i]; pos += 1; } }
            out[0][i] = (float) (pos / (double) N);
        }
        if (on) uiGen++;
    }
};
struct Groove : Obj {
    int name, chans; double pos = 0; bool loop = false; double loopStart = 0, loopEnd = 0;
    explicit Groove(const Args& a) { name = argS(a, 0, 0); chans = clampN(argI(a, 1, 1), 1, 8); setIO(3, chans + 1); sigInlet(0, 0); sigInlet(1, 0); sigInlet(2, 0); for (int k = 0; k <= chans; ++k) sigOutlet(k); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (m.sel == sym("loop")) { loop = m.f() != 0; return; } if (m.sel == S::set && m.n) { name = m.a[0].s; return; }
        if (in == 0 && m.isNum() && !m.isList()) { auto b = c.buffer(name); pos = m.f() * (b ? b->sr : c.sr) / 1000.0; return; }
        if (in == 0 && m.sel == S::stop) { sv[0] = 0; return; }
        if (in == 0 && (m.sel == S::start || m.isBang())) { pos = 0; if (sv[0] == 0) sv[0] = 1; return; }
        if (in == 1 && m.isNum()) { loopStart = m.f(); return; } if (in == 2 && m.isNum()) { loopEnd = m.f(); return; }
        Obj::msg(c, in, m);
    }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        auto b = c.buffer(name); if (!b || b->frames() < 2) { for (int k = 0; k <= chans; ++k) std::fill(out[k], out[k] + n, 0.0f); return; }
        long N = b->frames(); double k = b->sr / c.sr;
        for (int i = 0; i < n; ++i) {
            double ls = juce::jlimit(0.0, (double) N - 1, in[1][i] * b->sr / 1000.0), le = in[2][i] > 0 ? juce::jlimit(ls + 1, (double) N, in[2][i] * b->sr / 1000.0) : (double) N;
            if (!connectedIn[1].load()) ls = juce::jlimit(0.0, (double) N - 1, loopStart * b->sr / 1000.0);
            if (!connectedIn[2].load()) le = loopEnd > 0 ? juce::jlimit(ls + 1, (double) N, loopEnd * b->sr / 1000.0) : (double) N;
            double rate = in[0][i];
            for (int ch = 0; ch < chans; ++ch) out[ch][i] = (pos >= 0 && pos < N) ? b->interp(ch, pos) : 0.0f;
            out[chans][i] = (float) ((pos - ls) / juce::jmax(1.0, le - ls));
            pos += rate * k;
            if (pos >= le) { if (loop) pos = ls + std::fmod(pos - le, juce::jmax(1.0, le - ls)); else pos = (double) N; }
            else if (pos < ls && rate < 0) { if (loop) pos = le - std::fmod(ls - pos, juce::jmax(1.0, le - ls)); else pos = -1; }
        }
    }
};
// [play~ name]: "start" plays it once, "start 500 1000" a part (ms), "loop 1" round, "stop"; or a signal of ms into it (from [line~])
struct Play : Obj {
    int name, chans; double pos = -1, st = 0, en = 0, rate = 1; bool loop = false;
    explicit Play(const Args& a) { name = argS(a, 0, 0); chans = clampN(argI(a, 1, 1), 1, 8); setIO(1, chans + 1); sigInlet(0); for (int k = 0; k < chans; ++k) sigOutlet(k); }
    void msg(Ctx& c, int in, const Msg& m) override {
        auto b = c.buffer(name);
        if (m.sel == S::start || m.isBang()) { double sr = b ? b->sr : c.sr; st = m.n > 0 ? m.f(0) * sr / 1000.0 : 0; en = m.n > 1 ? m.f(1) * sr / 1000.0 : (b ? (double) b->frames() : 0); rate = 1; if (m.n > 2 && m.f(2) > 0) rate = std::abs(en - st) / (m.f(2) * c.sr / 1000.0); pos = st; return; }
        if (m.sel == S::stop) { pos = -1; return; } if (m.sel == sym("loop")) { loop = m.f() != 0; return; } if (m.sel == S::set && m.n) { name = m.a[0].s; return; }
        Obj::msg(c, in, m);
    }
    bool ending = false;
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        auto b = c.buffer(name); if (!b || b->frames() < 2) { for (int k = 0; k < nOut; ++k) std::fill(out[k], out[k] + n, 0.0f); return; }
        bool sigPos = connectedIn[0].load();
        for (int i = 0; i < n; ++i) {
            double p;
            if (sigPos) p = in[0][i] * b->sr / 1000.0;
            else { p = pos; if (pos >= 0) { double dir = en >= st ? 1 : -1; pos += dir * rate * b->sr / c.sr; if ((dir > 0 && pos >= en) || (dir < 0 && pos <= en)) { if (loop) pos = st; else { pos = -1; ending = true; } } } }
            for (int ch = 0; ch < chans; ++ch) out[ch][i] = p >= 0 ? b->interp(ch, p) : 0.0f;
        }
        if (ending) { ending = false; c.delay(this, 0); }
    }
    void tick(Ctx& c, int) override { c.bang(this, chans); }
};
struct IndexSig : Obj { int name, chan; explicit IndexSig(const Args& a) { name = argS(a, 0, 0); chan = (int) argI(a, 1, 1) - 1; setIO(2, 1); sigInlet(0); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { auto b = c.buffer(name); for (int i = 0; i < n; ++i) out[0][i] = b ? b->get(chan, (long) in[0][i]) : 0.0f; } };
struct WaveSig : Obj { int name; explicit WaveSig(const Args& a) { name = argS(a, 0, 0); setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 1, 0)); sigInlet(2, (float) argF(a, 2, 0)); sigOutlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { auto b = c.buffer(name); if (!b || b->frames() < 2) { std::fill(out[0], out[0] + n, 0.0f); return; }
        for (int i = 0; i < n; ++i) { double s0 = in[1][i] * b->sr / 1000.0, e0 = in[2][i] > 0 ? in[2][i] * b->sr / 1000.0 : (double) b->frames() - 1; double ph = in[0][i] - std::floor(in[0][i]); out[0][i] = b->interp(0, s0 + ph * (e0 - s0)); } } };
struct Peek : Obj { int name, chan; explicit Peek(const Args& a) { name = argS(a, 0, 0); chan = (int) argI(a, 1, 1) - 1; setIO(3, 1); }
    double val = 0; bool haveVal = false;
    void msg(Ctx& c, int in, const Msg& m) override { auto b = c.buffer(name); if (in == 1) { val = m.f(); haveVal = true; return; } if (in == 2) { chan = (int) m.f() - 1; return; }
        if (!b) return; long i = (long) m.f(0);
        if (m.isList() && m.n >= 2) { if (i >= 0 && i < b->frames()) b->ch[(size_t) juce::jlimit(0, b->chans() - 1, chan)][(size_t) i] = (float) m.f(1); return; }
        if (haveVal) { haveVal = false; if (i >= 0 && i < b->frames()) b->ch[(size_t) juce::jlimit(0, b->chans() - 1, chan)][(size_t) i] = (float) val; return; }
        c.outF(this, 0, b->get(chan, i)); } };
struct Poke : Obj { int name, chan; explicit Poke(const Args& a) { name = argS(a, 0, 0); chan = (int) argI(a, 1, 1) - 1; setIO(3, 0); sigInlet(0); sigInlet(1); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override { auto b = c.buffer(name); if (!b) return; for (int i = 0; i < n; ++i) { long k = (long) in[1][i]; if (k >= 0 && k < b->frames()) b->ch[(size_t) juce::jlimit(0, b->chans() - 1, chan)][(size_t) k] = in[0][i]; } } };
struct Info : Obj { int name; explicit Info(const Args& a) { name = argS(a, 0, 0); setIO(1, 4); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.sel == S::set && m.n) { name = m.a[0].s; return; } auto b = c.buffer(name); if (!b) return;
        c.outF(this, 3, b->sr); c.outI(this, 2, b->chans()); c.outI(this, 1, (double) b->frames()); c.outF(this, 0, b->frames() * 1000.0 / juce::jmax(1.0, b->sr)); } };

// ================================================================== effects in plain words (ARCO)
struct Pan : Obj { explicit Pan(const Args& a) { setIO(2, 2); sigInlet(0); sigInlet(1, (float) argF(a, 0, 0.5)); sigOutlet(0); sigOutlet(1); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { float p = juce::jlimit(0.0f, 1.0f, in[1][i]); float a = p * 1.5707963f; out[0][i] = in[0][i] * std::cos(a); out[1][i] = in[0][i] * std::sin(a); } } };
struct Reverb : Obj {     // a Freeverb-style room: 8 combs and 4 allpasses per side
    struct C { std::vector<float> b; int i = 0; float f = 0; } cL[8], cR[8]; struct A { std::vector<float> b; int i = 0; } aL[4], aR[4];
    double size, damp, mix;
    explicit Reverb(const Args& a) { setIO(5, 2); sigInlet(0); sigInlet(1); size = argF(a, 0, 0.7); damp = argF(a, 1, 0.4); mix = argF(a, 2, 0.3); sigOutlet(0); sigOutlet(1); }
    void prepare(double sr) override { static const int CT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, AT[4] = { 556, 441, 341, 225 }; double k = sr / 44100.0;
        for (int j = 0; j < 8; ++j) { cL[j].b.assign((size_t) (CT[j] * k) + 1, 0.0f); cR[j].b.assign((size_t) ((CT[j] + 23) * k) + 1, 0.0f); }
        for (int j = 0; j < 4; ++j) { aL[j].b.assign((size_t) (AT[j] * k) + 1, 0.0f); aR[j].b.assign((size_t) ((AT[j] + 23) * k) + 1, 0.0f); } }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 2) { size = juce::jlimit(0.0, 1.0, m.f()); return; } if (in == 3) { damp = juce::jlimit(0.0, 1.0, m.f()); return; } if (in == 4) { mix = juce::jlimit(0.0, 1.0, m.f()); return; }
        if (m.sel == S::clear) { for (auto* g : { cL, cR }) for (int j = 0; j < 8; ++j) std::fill(g[j].b.begin(), g[j].b.end(), 0.0f); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override {
        if (cL[0].b.empty()) prepare(48000);
        float fb = (float) (0.7 + 0.28 * size), d = (float) (damp * 0.4); bool st = connectedIn[1].load();
        for (int i = 0; i < n; ++i) {
            float x = (in[0][i] + (st ? in[1][i] : in[0][i])) * 0.015f, l = 0, r = 0;
            for (int j = 0; j < 8; ++j) { auto& L = cL[j]; float o = L.b[(size_t) L.i]; L.f = o * (1 - d) + L.f * d; L.b[(size_t) L.i] = flush(x + L.f * fb); L.i = (L.i + 1) % (int) L.b.size(); l += o;
                auto& R = cR[j]; float o2 = R.b[(size_t) R.i]; R.f = o2 * (1 - d) + R.f * d; R.b[(size_t) R.i] = flush(x + R.f * fb); R.i = (R.i + 1) % (int) R.b.size(); r += o2; }
            for (int j = 0; j < 4; ++j) { auto& A1 = aL[j]; float b = A1.b[(size_t) A1.i]; A1.b[(size_t) A1.i] = flush(l + b * 0.5f); l = b - l; A1.i = (A1.i + 1) % (int) A1.b.size();
                auto& A2 = aR[j]; float b2 = A2.b[(size_t) A2.i]; A2.b[(size_t) A2.i] = flush(r + b2 * 0.5f); r = b2 - r; A2.i = (A2.i + 1) % (int) A2.b.size(); }
            float dl = in[0][i], dr = st ? in[1][i] : in[0][i];
            out[0][i] = (float) (dl * (1 - mix) + l * mix * 3.0); out[1][i] = (float) (dr * (1 - mix) + r * mix * 3.0);
        }
    }
};
struct Chorus : Obj { DelayLine dl, dr; double ph = 0, rate, depth, mix;
    explicit Chorus(const Args& a) { setIO(4, 2); sigInlet(0); rate = argF(a, 0, 0.6); depth = argF(a, 1, 0.5); mix = argF(a, 2, 0.5); sigOutlet(0); sigOutlet(1); }
    void prepare(double sr) override { dl.size((int) (sr * 0.06)); dr.size((int) (sr * 0.06)); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { rate = m.f(); return; } if (in == 2) { depth = m.f(); return; } if (in == 3) { mix = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { if (dl.b.size() < 8) prepare(c.sr);
        for (int i = 0; i < n; ++i) { dl.push(in[0][i]); dr.push(in[0][i]); double base = 0.012 * c.sr, dep = juce::jlimit(0.0, 1.0, depth) * 0.008 * c.sr;
            float a = dl.read(base + dep * (0.5 + 0.5 * std::sin(TWO_PI * ph))), b = dr.read(base + dep * (0.5 + 0.5 * std::cos(TWO_PI * ph)));
            ph += rate / c.sr; ph -= std::floor(ph); out[0][i] = (float) (in[0][i] * (1 - mix * 0.5) + a * mix); out[1][i] = (float) (in[0][i] * (1 - mix * 0.5) + b * mix); } } };
struct Flanger : Obj { DelayLine dl; double ph = 0, rate, depth, fb, last = 0;
    explicit Flanger(const Args& a) { setIO(4, 1); sigInlet(0); rate = argF(a, 0, 0.25); depth = argF(a, 1, 0.7); fb = argF(a, 2, 0.5); sigOutlet(0); }
    void prepare(double sr) override { dl.size((int) (sr * 0.02)); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { rate = m.f(); return; } if (in == 2) { depth = m.f(); return; } if (in == 3) { fb = juce::jlimit(-0.95, 0.95, m.f()); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { if (dl.b.size() < 8) prepare(c.sr);
        for (int i = 0; i < n; ++i) { dl.push(flush((float) (in[0][i] + last * fb))); double d = (0.0005 + 0.006 * juce::jlimit(0.0, 1.0, depth) * (0.5 + 0.5 * std::sin(TWO_PI * ph))) * c.sr; last = dl.read(d);
            ph += rate / c.sr; ph -= std::floor(ph); out[0][i] = (float) ((in[0][i] + last) * 0.7); } } };
struct Phaser : Obj { double z[6] = {}, ph = 0, rate, depth, fb, last = 0;
    explicit Phaser(const Args& a) { setIO(4, 1); sigInlet(0); rate = argF(a, 0, 0.3); depth = argF(a, 1, 0.8); fb = argF(a, 2, 0.4); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { rate = m.f(); return; } if (in == 2) { depth = m.f(); return; } if (in == 3) { fb = juce::jlimit(-0.95, 0.95, m.f()); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        for (int i = 0; i < n; ++i) { double f = 300 + 3000 * juce::jlimit(0.0, 1.0, depth) * (0.5 + 0.5 * std::sin(TWO_PI * ph)); double t = std::tan(juce::MathConstants<double>::pi * f / c.sr), a = (t - 1) / (t + 1);
            double x = in[0][i] + last * fb; for (auto& s : z) { double y = a * x + s; s = x - a * y; if (std::abs(s) < 1e-20) s = 0; x = y; } last = x;
            ph += rate / c.sr; ph -= std::floor(ph); out[0][i] = (float) ((in[0][i] + x) * 0.6); } } };
struct Compressor : Obj { double env = 0, thr, ratio, att, rel, gainDb = 0; float gr = 0;
    explicit Compressor(const Args& a) { setIO(6, 2); sigInlet(0); sigInlet(1); thr = argF(a, 0, -18); ratio = argF(a, 1, 4); att = argF(a, 2, 10); rel = argF(a, 3, 150); sigOutlet(0); sigOutlet(1); }
    void msg(Ctx& c, int in, const Msg& m) override { switch (in) { case 2: thr = m.f(); return; case 3: ratio = juce::jmax(1.0, m.f()); return; case 4: att = m.f(); return; case 5: rel = m.f(); return; default: break; }
        if (m.sel == sym("gain")) { gainDb = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { double ka = std::exp(-1000.0 / (juce::jmax(0.1, att) * c.sr)), kr = std::exp(-1000.0 / (juce::jmax(1.0, rel) * c.sr)); bool st = connectedIn[1].load(); double mk = std::pow(10.0, gainDb / 20.0);
        for (int i = 0; i < n; ++i) { double x = juce::jmax(std::abs(in[0][i]), st ? std::abs(in[1][i]) : 0.0f); env = x > env ? ka * env + (1 - ka) * x : kr * env + (1 - kr) * x;
            double db = 20 * std::log10(juce::jmax(1e-6, env)), over = db - thr, g = over > 0 ? -over * (1 - 1 / ratio) : 0; gr = (float) g; double k = std::pow(10.0, g / 20.0) * mk;
            out[0][i] = (float) (in[0][i] * k); out[1][i] = (float) ((st ? in[1][i] : in[0][i]) * k); } uiValue = gr; } };
struct Limiter : Obj { double env = 0, ceil; DelayLine d0, d1;
    explicit Limiter(const Args& a) { setIO(3, 2); sigInlet(0); sigInlet(1); ceil = argF(a, 0, -1); sigOutlet(0); sigOutlet(1); }
    void prepare(double sr) override { d0.size((int) (sr * 0.003) + 4); d1.size((int) (sr * 0.003) + 4); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 2) { ceil = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { if (d0.b.size() < 8) prepare(c.sr); double lim = std::pow(10.0, ceil / 20.0), kr = std::exp(-1000.0 / (80.0 * c.sr)); bool st = connectedIn[1].load(); double la = c.sr * 0.002;
        for (int i = 0; i < n; ++i) { float l = in[0][i], r = st ? in[1][i] : in[0][i]; double pk = juce::jmax(std::abs(l), std::abs(r)); double need = pk > lim ? lim / pk : 1.0; env = need < env ? need : kr * env + (1 - kr) * need; if (env > 1) env = 1;
            d0.push(l); d1.push(r); out[0][i] = (float) juce::jlimit(-lim, lim, d0.read(la) * env); out[1][i] = (float) juce::jlimit(-lim, lim, d1.read(la) * env); } } };
struct Overdrive : Obj { explicit Overdrive(const Args& a) { setIO(2, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 2)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { float d = juce::jmax(1.0f, in[1][i]); out[0][i] = std::tanh(in[0][i] * d) / std::tanh(d); } } };
struct Degrade : Obj { double ph = 0; float held = 0; explicit Degrade(const Args& a) { setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1)); sigInlet(2, (float) argF(a, 1, 24)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { ph += juce::jlimit(0.001f, 1.0f, in[1][i]); if (ph >= 1) { ph -= std::floor(ph); float q = std::pow(2.0f, juce::jlimit(1.0f, 24.0f, in[2][i]) - 1); held = std::round(in[0][i] * q) / q; } out[0][i] = held; } } };
struct Downsamp : Obj { long long k = 0; float held = 0; explicit Downsamp(const Args& a) { setIO(2, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 1)); sigOutlet(0); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { if (k <= 0) { held = in[0][i]; k = (long long) juce::jmax(1.0f, in[1][i]); } --k; out[0][i] = held; } } };
// [pitchshift~ 7]: higher or lower without changing the time (two delay heads crossing)
struct PitchShift : Obj { DelayLine dl; double ph = 0; explicit PitchShift(const Args& a) { setIO(2, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 0)); sigOutlet(0); }
    void prepare(double sr) override { dl.size((int) (sr * 0.2)); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { if (dl.b.size() < 8) prepare(c.sr); double win = 0.05 * c.sr;
        for (int i = 0; i < n; ++i) { dl.push(in[0][i]); double r = std::pow(2.0, juce::jlimit(-24.0f, 24.0f, in[1][i]) / 12.0); ph += (1 - r) / win; ph -= std::floor(ph);
            double p2 = ph + 0.5; p2 -= std::floor(p2); float a = dl.read(1 + ph * win), b = dl.read(1 + p2 * win);
            float ga = (float) std::sin(juce::MathConstants<double>::pi * ph), gb = (float) std::sin(juce::MathConstants<double>::pi * p2); out[0][i] = a * ga + b * gb; } } };
// [freqshift~ 100]: moves every partial by the same Hz (metallic, strange) - an analytic signal from two allpass chains
struct FreqShift : Obj {
    double s1[4][2] = {}, s2[4][2] = {}, ph = 0, last2 = 0;
    explicit FreqShift(const Args& a) { setIO(2, 2); sigInlet(0); sigInlet(1, (float) argF(a, 0, 0)); sigOutlet(0); sigOutlet(1); }
    static double ap(double x, double a, double* s) { double y = a * (x - s[1]) + s[0]; s[0] = x; s[1] = y; return y; }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override {
        static const double A1[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 }, A2[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
        for (int i = 0; i < n; ++i) { double x = in[0][i], re = x, im = x; for (int k = 0; k < 4; ++k) { re = ap(re, A1[k] * A1[k], s1[k]); im = ap(im, A2[k] * A2[k], s2[k]); }
            double imD = last2; last2 = im; double cs = std::cos(TWO_PI * ph), sn = std::sin(TWO_PI * ph); ph += in[1][i] / c.sr; ph -= std::floor(ph);
            out[0][i] = (float) (re * cs - imD * sn); out[1][i] = (float) (re * cs + imD * sn); } }
};
// [gain~] in words: [volume~ -6] - a level in dB, smooth
struct Volume : Obj { double g = 1, target = 1; explicit Volume(const Args& a) { setIO(2, 1); sigInlet(0); target = std::pow(10.0, argF(a, 0, 0) / 20.0); g = target; sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1 && m.isNum()) { target = m.f() <= -90 ? 0 : std::pow(10.0, m.f() / 20.0); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { g += (target - g) * 0.002; out[0][i] = (float) (in[0][i] * g); } } };

float fAdd(float a, float b) { return a + b; } float fSub(float a, float b) { return a - b; } float fMul(float a, float b) { return a * b; }
float fDiv(float a, float b) { return b == 0 ? 0 : a / b; } float fMod(float a, float b) { return b == 0 ? 0 : std::fmod(a, b); }
float fRSub(float a, float b) { return b - a; } float fRDiv(float a, float b) { return a == 0 ? 0 : b / a; }
float fPow(float a, float b) { float r = std::pow(a, b); return std::isfinite(r) ? r : 0.0f; } float fMin(float a, float b) { return std::min(a, b); } float fMax(float a, float b) { return std::max(a, b); }
float fGt(float a, float b) { return a > b ? 1.0f : 0.0f; } float fLt(float a, float b) { return a < b ? 1.0f : 0.0f; } float fGe(float a, float b) { return a >= b ? 1.0f : 0.0f; }
float fLe(float a, float b) { return a <= b ? 1.0f : 0.0f; } float fEq(float a, float b) { return a == b ? 1.0f : 0.0f; } float fNe(float a, float b) { return a != b ? 1.0f : 0.0f; }
float fAbs(float x) { return std::abs(x); } float fSqrt(float x) { return x > 0 ? std::sqrt(x) : 0.0f; } float fCos(float x) { return cosTab(x); } float fSin(float x) { return cosTab(x - 0.25); }
float fTanh(float x) { return std::tanh(x); } float fMtof(float x) { return (float) mtof(x); } float fFtom(float x) { return (float) ftom(x); } float fDbtoa(float x) { return std::pow(10.0f, x / 20.0f); }
float fAtodb(float x) { return x > 0 ? 20.0f * std::log10(x) : -999.0f; } float fTrunc(float x) { return std::trunc(x); } float fWrap(float x) { return x - std::floor(x); }
float fExp(float x) { return std::exp(juce::jmin(x, 80.0f)); } float fLog(float x) { return x > 0 ? std::log(x) : -100.0f; } float fNeg(float x) { return -x; }
float fSign(float x) { return x > 0 ? 1.0f : x < 0 ? -1.0f : 0.0f; } float fFold(float x) { x = std::fmod(x + 1.0f, 4.0f); if (x < 0) x += 4; return x < 2 ? x - 1 : 3 - x; }

} // namespace

void registerSignal() {
    const char* MK = "Sound: make"; const char* MA = "Sound: math"; const char* FI = "Sound: filters"; const char* FX = "Sound: effects"; const char* DL = "Sound: delay & time";
    const char* SA = "Sound: samples"; const char* WA = "Sound: watch & measure"; const char* IO = "Sound: in & out";
    def("cycle~", MK, "A sine wave - the purest tone. The number is how high (in Hz: 440 is the A). It can also read a [buffer~] as its wave.", "frequency (Hz) | phase 0-1", "sound", "cycle~ 440", "sine tone oscillator wave beep frequency", mk<Cycle>());
    def("phasor~", MK, "A ramp from 0 to 1, again and again - to drive other things. [phasor~ 1n]: once a bar, in time with ARCO.", "frequency (or 4n, 1n ...) | phase", "ramp 0-1", "phasor~ 2", "ramp sawtooth lfo clock sync", mk<Phasor>());
    def("saw~", MK, "A sawtooth wave: bright and buzzy (clean, without aliasing).", "frequency", "sound", "saw~ 110", "sawtooth oscillator bright synth", mk<Saw>());
    def("rect~", MK, "A square / pulse wave: hollow. The right inlet changes its width (0.5 = square).", "frequency | width 0-1", "sound", "rect~ 220", "square pulse oscillator", mk<Rect>());
    def("tri~", MK, "A triangle wave: soft, between a sine and a square.", "frequency | shape 0-1", "sound", "tri~ 220", "triangle oscillator", mk<Tri>());
    def("noise~", MK, "White noise: every frequency at once (a hiss).", "", "sound", "noise~", "white noise hiss random", [](const Args& a) { return std::make_shared<Noise>(a, false); });
    def("pink~", MK, "Pink noise: softer noise, like rain or the sea.", "", "sound", "pink~", "pink noise rain sea", [](const Args& a) { return std::make_shared<Noise>(a, true); });
    def("rand~", MK, "A random value gliding to the next one, so many times a second - slow random movement.", "how often (Hz)", "signal", "rand~ 4", "random lfo movement", mk<RandSig>());
    def("sig~", MK, "Turns a number into a signal (a steady value).", "number", "signal", "sig~ 1", "number to signal constant", mk<Sig>());
    def("click~", MK, "One sample of 1 when it gets a bang - a click, to excite filters.", "bang", "sound", "click~", "impulse click", mk<Click>());
    def("train~", MK, "A pulse train: on, off, on, off (the period in ms).", "period ms | width | phase", "signal | bang each pulse", "train~ 500", "pulse clock gate", mk<Train>());
    def("count~", MK, "Counts samples (0, 1, 2 ...) - to read buffers.", "start / stop / a number | top", "count", "count~", "sample counter", mk<CountSig>());
    def("line~", MK, "Glides as a signal - the way to fade and to make envelopes. \"1 50\" goes to 1 in 50 ms; \"1 10 0 300\" up then down.",
        "target (and time) | time", "signal | bang at the end", "line~", "ramp fade envelope glide smooth", mk<LineSig>());
    def("curve~", MK, "Like [line~] (the same messages).", "target time ...", "signal | bang", "curve~", "ramp envelope", mk<CurveSig>());
    def("adsr~", MK, "An envelope: attack, decay, sustain, release ([adsr~ 10 200 0.6 500]). A number above 0 starts a note that loud, 0 ends it.", "1 / velocity: on, 0: off | attack ms | decay ms | sustain 0-1 | release ms", "envelope", "adsr~ 10 200 0.6 500", "envelope note volume shape attack release", mk<Adsr>());
    struct B { const char* n; F2 f; float d; const char* desc; const char* w; };
    static const B bins[] = { { "+~", fAdd, 0, "Adds two sounds (mixes them), or a number to a sound.", "add mix plus" }, { "-~", fSub, 0, "Subtracts.", "minus" },
        { "*~", fMul, 0, "Multiplies: a sound by a number is its volume; two sounds make ring modulation.", "multiply volume gain ring amplitude" },
        { "/~", fDiv, 1, "Divides.", "divide" }, { "%~", fMod, 1, "Modulo for signals.", "modulo wrap" }, { "!-~", fRSub, 0, "Subtracts the other way.", "minus" }, { "!/~", fRDiv, 1, "Divides the other way.", "divide" },
        { "pow~", fPow, 1, "To the power of.", "power" }, { "minimum~", fMin, 0, "The smaller of two signals.", "min" }, { "maximum~", fMax, 0, "The bigger of two signals.", "max" },
        { ">~", fGt, 0, "1 where left is bigger.", "compare" }, { "<~", fLt, 0, "1 where left is smaller.", "compare" }, { ">=~", fGe, 0, "1 where left is bigger or equal.", "compare" },
        { "<=~", fLe, 0, "1 where left is smaller or equal.", "compare" }, { "==~", fEq, 0, "1 where equal.", "compare" }, { "!=~", fNe, 0, "1 where different.", "compare" } };
    for (auto& b : bins) { F2 f = b.f; float d = b.d; def(b.n, MA, b.desc, "sound | sound or number", "sound", (std::string(b.n) + " 0.5").c_str(), b.w, [f, d](const Args& a) { return std::make_shared<SigBin>(a, f, d); }); }
    struct U { const char* n; F1 f; const char* desc; const char* w; };
    static const U uns[] = { { "abs~", fAbs, "Makes a signal positive.", "absolute rectify" }, { "sqrt~", fSqrt, "Square root.", "root" },
        { "cos~", fCos, "The cosine of (2 pi x) - with [phasor~], a sine wave.", "cosine wave" }, { "sin~", fSin, "The sine of (2 pi x).", "sine" },
        { "tanh~", fTanh, "Soft saturation: loud parts get rounded (warm distortion).", "saturate distortion warm soft clip" },
        { "mtof~", fMtof, "MIDI note numbers into Hz, as a signal.", "note frequency" }, { "ftom~", fFtom, "Hz into MIDI notes.", "frequency note" },
        { "dbtoa~", fDbtoa, "dB into a level.", "decibel" }, { "atodb~", fAtodb, "A level into dB.", "decibel" }, { "trunc~", fTrunc, "Cuts the decimals off.", "integer" },
        { "wrap~", fWrap, "Keeps only the part after the point (0-1, round again).", "wrap fraction" }, { "exp~", fExp, "e to the power of the signal.", "exponential" },
        { "log~", fLog, "The natural log.", "logarithm" }, { "neg~", fNeg, "Turns the signal upside down (x -1).", "invert phase" }, { "sign~", fSign, "1, 0 or -1.", "sign" },
        { "fold~", fFold, "Folds the signal back between -1 and 1 (wavefolding: rich and strange).", "wavefold distortion" } };
    for (auto& u : uns) { F1 f = u.f; def(u.n, MA, u.desc, "sound", "sound", u.n, u.w, [f](const Args&) { return std::make_shared<SigUn>(f); }); }
    def("clip~", MA, "Keeps a signal between a low and a high (hard distortion when it cuts).", "sound | low | high", "sound", "clip~ -0.5 0.5", "limit clip distortion", mk<ClipSig>());
    def("scale~", MA, "Maps a signal from one range to another (-1..1 into 200..2000).", "signal | in low | in high | out low | out high | curve", "signal", "scale~ -1 1 200 2000", "map range lfo", mk<ScaleSig>());
    def("round~", MA, "Rounds a signal to steps.", "signal | step", "signal", "round~ 0.25", "quantize steps", mk<RoundSig>());
    def("expr~", MA, "Math on signals, as you write it: [expr~ $v1 * $v2 + 0.5]. Every inlet is a signal ($v1 ...) or a number.", "$v1 | $v2 ...", "signal", "expr~ $v1 * 0.5", "formula math signal", mk<ExprSig>());
    def("onepole~", FI, "A gentle lowpass (6 dB): takes the top off.", "sound | cutoff Hz", "sound", "onepole~ 2000", "lowpass soft darker smooth", [](const Args& a) { return std::make_shared<OnePole>(a, false); });
    alias("lop~", "onepole~");
    def("hip~", FI, "A gentle highpass: takes the low end off.", "sound | cutoff Hz", "sound", "hip~ 100", "highpass thinner bass cut", [](const Args& a) { return std::make_shared<OnePole>(a, true); });
    def("lores~", FI, "A resonant lowpass (the synth filter): cutoff and resonance 0-1.", "sound | cutoff Hz | resonance 0-1", "sound", "lores~ 800 0.5", "lowpass synth filter resonance", mk<Lores>());
    def("reson~", FI, "A resonant bandpass: rings at one frequency.", "sound | gain | frequency | Q", "sound", "reson~ 1 1000 20", "bandpass ring resonant", mk<Reson>());
    def("svf~", FI, "A state-variable filter: lowpass, highpass, bandpass and notch at once.", "sound | frequency | resonance 0-1", "lowpass | highpass | bandpass | notch", "svf~ 1000 0.5", "filter all multimode", mk<Svf>());
    def("biquad~", FI, "A two-pole filter from its five numbers (a0 a1 a2 b1 b2) - for experts; [filter~] is the easy one.", "sound | a0 | a1 | a2 | b1 | b2", "sound", "biquad~ 1 0 0 0 0", "filter coefficients", mk<Biquad>());
    def("filter~", FI, "One filter, in words: [filter~ lowpass 1000 0.7]. Types: lowpass highpass bandpass notch peak lowshelf highshelf allpass. Inlets: frequency, Q, gain dB.", "sound (or a type) | frequency | Q | gain dB", "sound", "filter~ lowpass 1000 0.7", "filter eq lowpass highpass bandpass notch shelf", mk<FilterSig>());
    def("cross~", FI, "Splits a sound into lows and highs at a frequency.", "sound | frequency", "lows | highs", "cross~ 500", "crossover split bands", mk<Cross>());
    def("slide~", FI, "Smooths a signal (up and down separately, in samples).", "signal | up | down", "signal", "slide~ 100 1000", "smooth lag follower", mk<Slide>());
    def("rampsmooth~", FI, "Smooths with straight ramps of so many samples.", "signal", "signal", "rampsmooth~ 64", "smooth", mk<Rampsmooth>());
    def("dcblock~", FI, "Removes a steady offset (DC) from a sound.", "sound", "sound", "dcblock~", "dc offset", mk<DcBlock>());
    def("allpass~", FI, "An allpass filter: smears the phase (for reverbs and phasers).", "sound | delay ms | gain", "sound", "allpass~ 100 10 0.5", "diffuse phase", mk<AllpassSig>());
    def("comb~", FI, "A comb filter: a short echo with feedback - metallic, or a plucked string.", "sound | delay ms | dry | feed-forward | feedback", "sound", "comb~ 100 5 1 0 0.8", "comb metallic string karplus", mk<CombSig>());
    def("tapin~", DL, "The input of a delay line ([tapin~ 2000]: up to 2 s). Connect it to [tapout~].", "sound", "to tapout~", "tapin~ 2000", "delay echo memory", mk<Tapin>());
    def("tapout~", DL, "Reads the delay line of [tapin~] at a time ([tapout~ 500]).", "from tapin~ | times", "sound", "tapout~ 500", "delay echo", mk<Tapout>());
    def("delay~", DL, "Delays a sound by a number of samples.", "sound | delay (samples)", "sound", "delay~ 44100 22050", "delay samples", mk<DelaySig>());
    def("echo~", DL, "An echo in one box: time (ms, or 8n in tempo), how much comes back, how much you hear.", "sound | time | feedback 0-1 | mix 0-1", "sound", "echo~ 8n 0.5 0.4", "echo delay repeat feedback", mk<Echo>());
    def("snapshot~", WA, "Takes the value of a signal as a number (a bang, or every so many ms).", "signal, bang | interval ms", "number", "snapshot~ 100", "measure sample value number", mk<Snapshot>());
    def("peakamp~", WA, "The loudest moment since the last time (bang, or every so many ms).", "sound, bang | interval ms", "level", "peakamp~ 100", "level loud peak meter", mk<Peakamp>());
    def("avg~", WA, "The average loudness since the last bang.", "sound, bang", "level", "avg~", "average level", mk<Avg>());
    def("edge~", WA, "Bangs when a signal goes from 0 to something (left) or back to 0 (right).", "signal", "bang up | bang down", "edge~", "trigger detect", mk<EdgeSig>());
    def("thresh~", WA, "1 while a signal is above the high level, until it falls under the low one.", "signal | low | high", "signal", "thresh~ 0.1 0.2", "gate detect threshold", mk<ThreshSig>());
    def("change~", WA, "1 when a signal goes up, -1 when it goes down.", "signal", "signal", "change~", "direction", mk<ChangeSig>());
    def("zerox~", WA, "Counts zero crossings (noisy sounds cross a lot).", "sound", "count | clicks", "zerox~", "zero crossing noisy", mk<Zerox>());
    def("sah~", WA, "Sample and hold: keeps the left signal when the right one rises above a level.", "signal | trigger", "signal", "sah~ 0.5", "sample hold stepped random", mk<Sah>());
    def("selector~", IO, "Chooses one of several sounds (left: which, 0 none).", "which | sounds", "sound", "selector~ 2", "choose switch input", mk<SelectorSig>());
    def("gate~", IO, "Sends a sound to one of several outlets (left: which, 0 none).", "which | sound", "outlets", "gate~ 2", "route switch output", mk<GateSig>());
    def("matrix~", IO, "A mixer of many ins to many outs: \"in out gain\" connects them.", "sounds, in out gain", "sounds", "matrix~ 2 2", "mixer route", mk<MatrixSig>());
    def("send~", IO, "Sends a sound without a cord to [receive~ name].", "sound", "", "send~ bus", "wireless bus", [](const Args& a) { return std::make_shared<SendSig>(a, false); });
    def("receive~", IO, "Receives the sound of [send~ name].", "set name", "sound", "receive~ bus", "wireless bus", [](const Args& a) { return std::make_shared<RecvSig>(a, false); });
    def("throw~", IO, "Many [throw~ name] add their sound into one [catch~ name].", "sound", "", "throw~ mix", "bus sum", [](const Args& a) { return std::make_shared<SendSig>(a, true); });
    def("catch~", IO, "Hears the sum of every [throw~ name].", "", "sound", "catch~ mix", "bus sum", [](const Args& a) { return std::make_shared<RecvSig>(a, true); });
    def("dac~", IO, "The way out: plug sound in here to hear it ([dac~ 1 2]: left and right). It goes into ARCO's mix (choose where at the top).", "left | right", "", "dac~ 1 2", "output speakers hear sound out play", mk<Dac>());
    alias("out~", "dac~");
    def("adc~", IO, "The way in: [adc~ 1] is LIVE input 1 (a microphone, a guitar), 1-4 the four inputs, 5 6 the station's mix. As an effect in STUDIO: the track's sound.", "", "per input", "adc~ 1 2", "input microphone mic guitar in live record", mk<Adc>());
    alias("in~", "adc~");
    def("buffer~", SA, "Keeps a sound in memory, by name ([buffer~ loop 4000]: 4 s). Drop a sound file on it, or send \"read\". size, clear, normalize.", "read, size ms, clear, normalize, bang", "length ms | ", "buffer~ mysound 2000", "sample memory audio file sound store", mk<BufferObj>());
    def("record~", SA, "Records a sound into a [buffer~]: 1 starts, 0 stops. \"loop 1\" records round and round.", "sound, 1 / 0 | ", "where it is 0-1", "record~ mysound", "record sample capture", mk<Record>());
    def("play~", SA, "Plays a [buffer~]: \"start\" from the beginning, \"start 500 1000\" a part, \"loop 1\", \"stop\". Or give it the position (ms) as a signal.", "start, stop, loop | ", "sound | bang at the end", "play~ mysound", "play sample playback trigger", mk<Play>());
    def("groove~", SA, "Plays a [buffer~] at any speed (1 normal, 2 twice, -1 backwards); \"loop 1\" loops between the start and end (ms).", "speed | loop start ms | loop end ms", "sound | where it is 0-1", "groove~ mysound", "play loop speed pitch varispeed sample", mk<Groove>());
    def("index~", SA, "Reads a [buffer~] at a sample number.", "sample number", "sound", "index~ mysound", "read sample", mk<IndexSig>());
    def("wave~", SA, "Reads a [buffer~] as a wave from a phase 0-1 (wavetables).", "phase | start ms | end ms", "sound", "wave~ mysound", "wavetable oscillator", mk<WaveSig>());
    def("peek~", SA, "Reads (or writes, \"index value\") one sample of a [buffer~] as a number.", "index | value | channel", "value", "peek~ mysound", "read write sample number", mk<Peek>());
    def("poke~", SA, "Writes a signal into a [buffer~] at a signal position.", "value | index", "", "poke~ mysound", "write sample", mk<Poke>());
    def("info~", SA, "How long a [buffer~] is (ms, samples, channels, sample rate).", "bang", "ms | samples | channels | rate", "info~ mysound", "length duration", mk<Info>());
    def("pan~", FX, "Places a mono sound between left (0) and right (1).", "sound | position 0-1", "left | right", "pan~ 0.5", "stereo pan position left right", mk<Pan>());
    def("reverb~", FX, "A room: size, damping, mix (0-1).", "left | right | size | damping | mix", "left | right", "reverb~ 0.8 0.4 0.3", "reverb room hall space ambience", mk<Reverb>());
    def("chorus~", FX, "Thickens a sound into a wide, moving stereo (rate Hz, depth, mix).", "sound | rate | depth | mix", "left | right", "chorus~ 0.6 0.5 0.5", "chorus wide thick", mk<Chorus>());
    def("flanger~", FX, "A flanger: a jet sweep (rate, depth, feedback).", "sound | rate | depth | feedback", "sound", "flanger~ 0.25 0.7 0.5", "flanger jet sweep", mk<Flanger>());
    def("phaser~", FX, "A phaser: a swirling sweep (rate, depth, feedback).", "sound | rate | depth | feedback", "sound", "phaser~ 0.3 0.8 0.4", "phaser swirl sweep", mk<Phaser>());
    def("compressor~", FX, "Evens out the loudness: threshold dB, ratio, attack ms, release ms (\"gain 6\" adds make-up gain).", "left | right | threshold | ratio | attack | release", "left | right", "compressor~ -18 4 10 150", "compress dynamics punch level", mk<Compressor>());
    alias("omx.comp~", "compressor~");
    def("limiter~", FX, "Never louder than a ceiling (dB) - protects your ears and speakers.", "left | right | ceiling dB", "left | right", "limiter~ -1", "limit protect maximum", mk<Limiter>());
    alias("omx.peaklim~", "limiter~");
    def("overdrive~", FX, "Warm distortion: the more drive, the dirtier.", "sound | drive", "sound", "overdrive~ 4", "distortion drive dirty guitar fuzz", mk<Overdrive>());
    def("degrade~", FX, "Lo-fi: fewer samples (0-1) and fewer bits (1-24).", "sound | rate 0-1 | bits", "sound", "degrade~ 0.25 8", "bitcrush lofi crush retro", mk<Degrade>());
    def("downsamp~", FX, "Holds each sample for N samples (crunchy).", "sound | N", "sound", "downsamp~ 4", "lofi sample hold", mk<Downsamp>());
    def("pitchshift~", FX, "Higher or lower by semitones without changing the speed.", "sound | semitones", "sound", "pitchshift~ 7", "pitch shift harmony transpose higher lower", mk<PitchShift>());
    def("freqshift~", FX, "Moves every frequency by the same Hz: metallic, bell-like, strange.", "sound | shift Hz", "up | down", "freqshift~ 50", "frequency shift metallic", mk<FreqShift>());
    def("volume~", FX, "A volume in dB, smooth (-90 is silence).", "sound | dB", "sound", "volume~ -6", "gain level loud quiet", mk<Volume>());
}

}} // namespace arco::px
