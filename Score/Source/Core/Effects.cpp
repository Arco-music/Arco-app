#include "Effects.h"
#include <map>

namespace score {

static std::atomic<uint32_t> g_uid { 1000 };
uint32_t newUid() { return g_uid.fetch_add(1); }
void bumpUid(uint32_t seen) { uint32_t cur = g_uid.load(); while (seen >= cur && !g_uid.compare_exchange_weak(cur, seen + 1)) {} }

void Effect::prepare(double s, int mb) {
    sr = s; maxBlock = std::max(16, mb);
    dryL.assign((size_t) maxBlock, 0.0f); dryR.assign((size_t) maxBlock, 0.0f);
    prepared = true; fade = enabled.load() ? 1.0f : 0.0f;
    onPrepare(); reset();
}

void Effect::run(float* L, float* R, int n, const TimeInfo& t) {
    if (!prepared) return;
    bool on = enabled.load(std::memory_order_relaxed);
    if (!on && fade <= 0.0f) return;
    for (int off = 0; off < n; off += maxBlock) {
        int m = std::min(maxBlock, n - off);
        float* l = L + off; float* r = R + off;
        std::memcpy(dryL.data(), l, sizeof(float) * (size_t) m); std::memcpy(dryR.data(), r, sizeof(float) * (size_t) m);
        process(l, r, m, t);
        float target = on ? 1.0f : 0.0f;
        if (fade != target || !on) {
            float step = 1.0f / (float) (0.01 * sr);
            for (int i = 0; i < m; ++i) {
                fade += fade < target ? std::min(step, target - fade) : -std::min(step, fade - target);
                l[i] = dryL[(size_t) i] + (l[i] - dryL[(size_t) i]) * fade; r[i] = dryR[(size_t) i] + (r[i] - dryR[(size_t) i]) * fade;
            }
            if (!on && fade <= 0.0f) reset();
        }
        for (int i = 0; i < m; ++i) if (isBad(l[i]) || isBad(r[i])) { reset(); std::memcpy(l, dryL.data(), sizeof(float) * (size_t) m);
            std::memcpy(r, dryR.data(), sizeof(float) * (size_t) m); break; }
        scope.push(l, r, m);
    }
}

namespace {

std::vector<std::string> waveNames() { return { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Smooth random" }; }

// ======================================================================== EQ
class EQ : public Effect {
public:
    EQ() : Effect("eq") {
        add("lc", "LOW CUT", 20, 2000, 20, Unit::Hz, true);
        add("lg", "LOW", -18, 18, 0, Unit::dB); add("lf", "LOW FREQ", 30, 600, 120, Unit::Hz, true);
        add("mg", "MID", -18, 18, 0, Unit::dB); add("mf", "MID FREQ", 150, 9000, 1000, Unit::Hz, true); add("mq", "MID Q", 0.3f, 8, 1, Unit::None, true);
        add("hg", "HIGH", -18, 18, 0, Unit::dB); add("hf", "HIGH FREQ", 1500, 16000, 6000, Unit::Hz, true);
        add("hc", "HIGH CUT", 1000, 20000, 20000, Unit::Hz, true);
        add("out", "OUTPUT", -18, 18, 0, Unit::dB);
    }
    Biquad b[2][7]; float last[10] = { -1 };
    void reset() override { for (auto& c : b) for (auto& q : c) q.reset(); }
    void update() {
        bool ch = false; for (int i = 0; i < 10; ++i) if (val(i) != last[i]) { ch = true; last[i] = val(i); }
        if (!ch) return;
        for (auto& c : b) {
            c[0].set(Biquad::HP, sr, val(0), 0.5412); c[1].set(Biquad::HP, sr, val(0), 1.3066);
            c[2].set(Biquad::LowShelf, sr, val(2), 0.7, val(1)); c[3].set(Biquad::Peak, sr, val(4), val(5), val(3));
            c[4].set(Biquad::HighShelf, sr, val(7), 0.7, val(6));
            c[5].set(Biquad::LP, sr, val(8), 0.5412); c[6].set(Biquad::LP, sr, val(8), 1.3066);
        }
    }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        update();
        bool lc = val(0) > 20.5f, hc = val(8) < 19900.0f, lo = std::abs(val(1)) > 0.01f, mid = std::abs(val(3)) > 0.01f, hi = std::abs(val(6)) > 0.01f;
        float g = dbToGain(val(9));
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c) {
            auto& q = b[c]; float* x = io[c];
            for (int i = 0; i < n; ++i) {
                float s = x[i];
                if (lc) s = q[1].process(q[0].process(s));
                if (lo) s = q[2].process(s);
                if (mid) s = q[3].process(s);
                if (hi) s = q[4].process(s);
                if (hc) s = q[6].process(q[5].process(s));
                x[i] = s * g;
            }
        }
    }
    bool hasCurve() const override { return true; }
    float curveDb(double f) const override {
        double m = 1.0; auto& q = b[0];
        if (val(0) > 20.5f) m *= q[0].magnitude(f, sr) * q[1].magnitude(f, sr);
        if (std::abs(val(1)) > 0.01f) m *= q[2].magnitude(f, sr);
        if (std::abs(val(3)) > 0.01f) m *= q[3].magnitude(f, sr);
        if (std::abs(val(6)) > 0.01f) m *= q[4].magnitude(f, sr);
        if (val(8) < 19900.0f) m *= q[5].magnitude(f, sr) * q[6].magnitude(f, sr);
        return (float) (20.0 * std::log10(std::max(1e-6, m))) + val(9);
    }
};

// ======================================================================== COMPRESSOR
class Comp : public Effect {
public:
    Comp() : Effect("comp") {
        add("th", "THRESHOLD", -60, 0, -18, Unit::dB); add("ra", "RATIO", 1, 20, 4, Unit::Ratio, true);
        add("at", "ATTACK", 0.1f, 100, 10, Unit::Ms, true); add("re", "RELEASE", 10, 1500, 150, Unit::Ms, true);
        add("kn", "KNEE", 0, 18, 6, Unit::dB); add("mk", "MAKE-UP", 0, 30, 0, Unit::dB);
        add("mix", "MIX", 0, 100, 100, Unit::Percent);
        addOnOff("auto", "AUTO GAIN", true);
    }
    float gr = 0, env = 0, mixS = 1;
    void reset() override { gr = 0; env = 0; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float th = val(0), ra = val(1), kn = val(4);
        float at = (float) (1.0 - std::exp(-1000.0 / (val(2) * sr))), re = (float) (1.0 - std::exp(-1000.0 / (val(3) * sr)));
        float envA = (float) (1.0 - std::exp(-1000.0 / (2.0 * sr)));
        float mk = val(5) + (params[7]->on() ? std::min(12.0f, -th * (1.0f - 1.0f / ra) * 0.5f) : 0.0f);
        float mg = dbToGain(mk); float maxGr = 0;
        for (int i = 0; i < n; ++i) {
            float x = std::max(std::abs(L[i]), std::abs(R[i]));
            env += (x - env) * (x > env ? envA : envA * 0.25f);
            float db = gainToDb(env), over = db - th, target;
            if (kn > 0 && 2 * over > -kn && 2 * over < kn) target = (1.0f / ra - 1.0f) * (over + kn * 0.5f) * (over + kn * 0.5f) / (2.0f * kn);
            else target = over > 0 ? over * (1.0f / ra - 1.0f) : 0.0f;
            gr += (target - gr) * (target < gr ? at : re);
            float g = dbToGain(gr) * mg;
            L[i] *= g; R[i] *= g; maxGr = std::min(maxGr, gr);
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(6) / 100.0f, mixS);
        meter[0].store(-maxGr);
    }
};

// ======================================================================== GATE
class Gate : public Effect {
public:
    Gate() : Effect("gate") {
        add("th", "THRESHOLD", -80, 0, -45, Unit::dB); add("at", "ATTACK", 0.1f, 50, 1, Unit::Ms, true);
        add("ho", "HOLD", 0, 500, 50, Unit::Ms); add("re", "RELEASE", 5, 2000, 150, Unit::Ms, true);
        add("rg", "RANGE", -80, 0, -80, Unit::dB);
    }
    float env = 0, g = 0; int hold = 0;
    void reset() override { env = 0; g = 0; hold = 0; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float th = dbToGain(val(0)), fl = dbToGain(val(4));
        float at = (float) (1.0 - std::exp(-1000.0 / (val(1) * sr))), re = (float) (1.0 - std::exp(-1000.0 / (val(3) * sr)));
        int ho = (int) (val(2) * 0.001 * sr);
        for (int i = 0; i < n; ++i) {
            float x = std::max(std::abs(L[i]), std::abs(R[i]));
            env = std::max(x, env * 0.9995f);
            bool open = env > th;
            if (open) hold = ho; else if (hold > 0) --hold;
            float tg = (open || hold > 0) ? 1.0f : 0.0f;
            g += (tg - g) * (tg > g ? at : re);
            float gg = fl + (1.0f - fl) * g;
            L[i] *= gg; R[i] *= gg;
        }
        meter[0].store(g);
    }
};

// ======================================================================== DRIVE
class Drive : public Effect {
public:
    Drive() : Effect("drv") {
        addChoice("ty", "TYPE", { "Tube", "Overdrive", "Fuzz", "Fold", "Rectify", "Hard clip" });
        add("dr", "DRIVE", 0, 48, 12, Unit::dB); add("tn", "TONE", 400, 18000, 6000, Unit::Hz, true);
        add("bi", "BIAS", 0, 100, 0, Unit::Percent); add("mix", "MIX", 0, 100, 100, Unit::Percent);
        add("out", "OUTPUT", -24, 12, -6, Unit::dB);
    }
    OnePole tone[2], dc[2]; float mixS = 1;
    void reset() override { for (auto& t : tone) t.reset(); for (auto& d : dc) d.reset(); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        int ty = params[0]->geti(); float g = dbToGain(val(1)), bias = val(3) / 100.0f * 0.6f, out = dbToGain(val(5));
        for (auto& t : tone) t.setHz(sr, val(2));
        for (auto& d : dc) d.setHz(sr, 12);
        float* io[2] = { L, R };
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) {
            float x = io[c][i] * g + bias, y;
            switch (ty) {
                case 0: y = x > 0 ? softClip(x) : softClip(x * 0.7f) * 1.1f; break;                  // tube: asymmetric, warm
                case 1: y = std::atan(x * 1.4f) * 0.7f; break;
                case 2: y = softClip(x * 3.0f) * 0.9f; y = y > 0 ? std::min(y, 0.6f) : std::max(y, -0.8f); break;
                case 3: { float f = x * 0.25f + 0.25f; f -= std::floor(f); y = 1.0f - std::abs(f * 4.0f - 2.0f) ; y = clampf(y, -1, 1); break; }   // wave folding
                case 4: y = softClip(std::abs(x)) * 2.0f - 0.6f; break;
                default: y = clampf(x, -0.7f, 0.7f); break;
            }
            y = dc[c].hp(y);
            io[c][i] = tone[c].lp(y) * out;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(4) / 100.0f, mixS);
    }
};

// ======================================================================== CRUSH
class Crush : public Effect {
public:
    Crush() : Effect("crush") {
        add("bits", "BITS", 1, 16, 8, Unit::None); add("rate", "RATE", 300, 48000, 11025, Unit::Hz, true);
        add("jit", "JITTER", 0, 100, 0, Unit::Percent); add("mix", "MIX", 0, 100, 100, Unit::Percent);
    }
    float ph = 0, hl = 0, hr = 0, mixS = 1; Rng rng;
    void reset() override { ph = 0; hl = hr = 0; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float step = std::pow(2.0f, 1.0f - val(0)); float inc = (float) (val(1) / sr), jit = val(2) / 100.0f;
        for (int i = 0; i < n; ++i) {
            ph += inc * (1.0f + jit * rng.bi() * 0.8f);
            if (ph >= 1.0f) { ph -= std::floor(ph); hl = std::round(L[i] / step) * step; hr = std::round(R[i] / step) * step; }
            L[i] = hl; R[i] = hr;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(3) / 100.0f, mixS);
    }
};

// ======================================================================== FILTER (with LFO and envelope)
class Filter : public Effect {
public:
    Filter() : Effect("flt") {
        addChoice("ty", "TYPE", { "Low 12", "Low 24", "High 12", "High 24", "Band", "Notch", "Ladder" }, 1);
        add("fc", "CUTOFF", 20, 20000, 2000, Unit::Hz, true); add("q", "RESONANCE", 0, 100, 20, Unit::Percent);
        add("dr", "DRIVE", 0, 24, 0, Unit::dB);
        addOnOff("sync", "SYNC", false); add("rate", "RATE", 0.02f, 20, 0.5f, Unit::Hz, true);
        addChoice("div", "DIVISION", divisionNames(), 6);
        add("lfo", "LFO", 0, 4, 0, Unit::Oct); params.back()->unit = Unit::None;
        addChoice("sh", "SHAPE", waveNames());
        add("env", "ENVELOPE", -4, 4, 0, Unit::None);
        add("mix", "MIX", 0, 100, 100, Unit::Percent);
    }
    SVF s1[2], s2[2]; Ladder ld[2]; EnvFollow ef; double ph = 0; float held = 0, prevHeld = 0, mixS = 1, fcS = 0; Rng rng;
    void reset() override { for (int c = 0; c < 2; ++c) { s1[c].reset(); s2[c].reset(); ld[c].reset(); } ef.env = 0; }
    void onPrepare() override { ef.set(sr, 5, 180); fcS = val(1); }
    void process(float* L, float* R, int n, const TimeInfo& t) override {
        int ty = params[0]->geti(); float res = val(2) / 100.0f, dg = dbToGain(val(3));
        double rate = params[4]->on() ? t.bpm / 60.0 / divisionBeats(params[6]->geti()) : val(5);
        if (params[4]->on() && t.playing) ph = std::fmod(t.beat / divisionBeats(params[6]->geti()), 1.0);
        Shape sh = (Shape) params[8]->geti();
        const int SUB = 16;
        for (int o = 0; o < n; o += SUB) {
            int m = std::min(SUB, n - o);
            float e = 0; for (int i = 0; i < m; ++i) e = ef.process(0.5f * (L[o + i] + R[o + i]));
            double before = ph; ph += rate * m / sr; if (ph >= 1.0) { ph -= std::floor(ph); prevHeld = held; held = rng.bi(); }
            (void) before;
            float lf = lfoShape(sh, (float) ph, held, prevHeld);
            float oct = val(7) * lf * 0.5f + val(9) * clampf(e * 4.0f, 0, 1);
            float target = val(1) * std::pow(2.0f, oct);
            fcS += (target - fcS) * 0.25f;
            float fc = clampf(fcS, 20, (float) (sr * 0.45));
            for (int c = 0; c < 2; ++c) { s1[c].set(sr, fc, ty == 1 || ty == 3 ? res * 0.6f : res); s2[c].set(sr, fc, 0.15f); ld[c].set(sr, fc, res * 0.95f); }
            float* io[2] = { L + o, R + o };
            for (int c = 0; c < 2; ++c) for (int i = 0; i < m; ++i) {
                float x = io[c][i] * dg; if (dg > 1.01f) x = softClip(x);
                float y;
                switch (ty) {
                    case 0: s1[c].tick(x); y = s1[c].lp; break;
                    case 1: s1[c].tick(x); s2[c].tick(s1[c].lp); y = s2[c].lp; break;
                    case 2: s1[c].tick(x); y = s1[c].hp; break;
                    case 3: s1[c].tick(x); s2[c].tick(s1[c].hp); y = s2[c].hp; break;
                    case 4: s1[c].tick(x); y = s1[c].bpn(); break;
                    case 5: s1[c].tick(x); y = s1[c].notch(); break;
                    default: y = ld[c].process(x); break;
                }
                io[c][i] = dg > 1.01f ? y / std::sqrt(dg) : y;
            }
        }
        meter[0].store(fcS);
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(10) / 100.0f, mixS);
    }
    bool hasCurve() const override { return true; }
    float curveDb(double f) const override {
        int ty = params[0]->geti(); double fc = meter[0].load() > 0 ? meter[0].load() : val(1), q = 0.5 / std::max(0.02, (2.0 - 1.96 * val(2) / 100.0) * 0.5);
        double x = f / fc, m;
        auto lp2 = [&](double xx) { return 1.0 / std::sqrt((1 - xx * xx) * (1 - xx * xx) + (xx / q) * (xx / q)); };
        auto hp2 = [&](double xx) { return xx * xx * lp2(xx); };
        switch (ty) { case 0: m = lp2(x); break; case 1: m = lp2(x) * lp2(x) / std::max(1.0, q * 0.6); break; case 2: m = hp2(x); break;
            case 3: m = hp2(x) * hp2(x) / std::max(1.0, q * 0.6); break; case 4: m = (x / q) * lp2(x); break;
            case 5: m = std::abs(1 - x * x) * lp2(x); break; default: m = lp2(x) * lp2(x) / std::max(1.0, q * 0.6); break; }
        return (float) (20.0 * std::log10(std::max(1e-6, m)));
    }
};

// ======================================================================== CHORUS
class Chorus : public Effect {
public:
    Chorus() : Effect("cho") {
        add("rate", "RATE", 0.05f, 5, 0.4f, Unit::Hz, true); add("depth", "DEPTH", 0, 100, 40, Unit::Percent);
        add("dl", "DELAY", 5, 30, 12, Unit::Ms); addChoice("vo", "VOICES", { "2", "3", "4" }, 1);
        add("wid", "WIDTH", 0, 100, 80, Unit::Percent); add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    DelayLine dl[2]; double ph = 0; float mixS = 0.5f;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (0.06 * sr)); }
    void reset() override { for (auto& d : dl) d.clear(); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        int nv = params[3]->geti() + 2; float base = (float) (val(2) * 0.001 * sr), dep = (float) (val(1) / 100.0 * 0.008 * sr), wid = val(4) / 100.0f;
        double inc = val(0) / sr;
        for (int i = 0; i < n; ++i) {
            dl[0].push(L[i]); dl[1].push(R[i]);
            float ol = 0, orr = 0;
            for (int v = 0; v < nv; ++v) {
                float pv = (float) (ph + (double) v / nv);
                float ml = std::sin(kTwoPiF * pv), mr = std::sin(kTwoPiF * (pv + 0.25f * wid));
                ol += dl[0].readCubic(base + dep * (0.5f + 0.5f * ml));
                orr += dl[1].readCubic(base + dep * (0.5f + 0.5f * mr));
            }
            ph += inc; if (ph >= 1) ph -= 1;
            float k = 1.6f / (float) nv;
            float m = 0.5f * (ol + orr) * k, s = 0.5f * (ol - orr) * k;
            L[i] = m + s * (0.5f + wid); R[i] = m - s * (0.5f + wid);
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(5) / 100.0f, mixS);
    }
};

// ======================================================================== FLANGER
class Flanger : public Effect {
public:
    Flanger() : Effect("fla") {
        add("rate", "RATE", 0.02f, 5, 0.2f, Unit::Hz, true); add("depth", "DEPTH", 0, 100, 70, Unit::Percent);
        add("man", "DELAY", 0.2f, 10, 1.5f, Unit::Ms, true); add("fb", "FEEDBACK", -95, 95, 50, Unit::Percent);
        add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    DelayLine dl[2]; double ph = 0; float fbv[2] = { 0, 0 }, mixS = 0.5f;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (0.03 * sr)); }
    void reset() override { for (auto& d : dl) d.clear(); fbv[0] = fbv[1] = 0; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float man = (float) (val(2) * 0.001 * sr), dep = val(1) / 100.0f, fb = val(3) / 100.0f; double inc = val(0) / sr;
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) {
                float m = 0.5f + 0.5f * std::sin(kTwoPiF * (float) (ph + c * 0.25));
                float d = 1.0f + man * (0.05f + 0.95f * (1.0f - dep + dep * m)) * 1.0f;
                dl[c].push(io[c][i] + softClip(fbv[c] * fb));
                float y = dl[c].readCubic(d); fbv[c] = y;
                io[c][i] = 0.5f * (io[c][i] + y);
            }
            ph += inc; if (ph >= 1) ph -= 1;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(4) / 100.0f, mixS);
    }
};

// ======================================================================== PHASER
class Phaser : public Effect {
public:
    Phaser() : Effect("phs") {
        add("rate", "RATE", 0.02f, 8, 0.3f, Unit::Hz, true); add("depth", "DEPTH", 0, 100, 80, Unit::Percent);
        add("fc", "CENTRE", 100, 5000, 800, Unit::Hz, true); add("fb", "FEEDBACK", -90, 90, 40, Unit::Percent);
        addChoice("st", "STAGES", { "4", "6", "8", "12" }, 1); add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    Allpass1 ap[2][12]; double ph = 0; float fbv[2] = { 0, 0 }, mixS = 0.5f;
    void reset() override { for (auto& c : ap) for (auto& a : c) a.reset(); fbv[0] = fbv[1] = 0; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        static const int ST[4] = { 4, 6, 8, 12 }; int ns = ST[params[4]->geti()];
        float fb = val(3) / 100.0f, dep = val(1) / 100.0f; double inc = val(0) / sr;
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) {
                float m = std::sin(kTwoPiF * (float) (ph + c * 0.2));
                float f = val(2) * std::pow(2.0f, m * 2.0f * dep);
                float w = (float) std::tan(kPi * std::min((double) f, sr * 0.45) / sr), a = (1 - w) / (1 + w);
                float x = io[c][i] + fbv[c] * fb;
                for (int s = 0; s < ns; ++s) x = ap[c][s].process(x, a);
                fbv[c] = softClip(x);
                io[c][i] = 0.5f * (io[c][i] + x);
            }
            ph += inc; if (ph >= 1) ph -= 1;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(5) / 100.0f, mixS);
    }
};

// ======================================================================== TREMOLO / AUTO-PAN
class Tremolo : public Effect {
public:
    Tremolo() : Effect("trm") {
        addOnOff("sync", "SYNC", true); add("rate", "RATE", 0.1f, 20, 4, Unit::Hz, true); addChoice("div", "DIVISION", divisionNames(), 9);
        add("depth", "DEPTH", 0, 100, 70, Unit::Percent); addChoice("sh", "SHAPE", { "Sine", "Triangle", "Square", "Saw" });
        add("pan", "PAN", 0, 100, 0, Unit::Percent);
    }
    double ph = 0; float sq = 0;
    void process(float* L, float* R, int n, const TimeInfo& t) override {
        double beats = divisionBeats(params[2]->geti());
        double rate = params[0]->on() ? t.bpm / 60.0 / beats : val(1);
        if (params[0]->on() && t.playing) ph = std::fmod(t.beat / beats, 1.0);
        float dep = val(3) / 100.0f, pan = val(5) / 100.0f; int sh = params[4]->geti();
        for (int i = 0; i < n; ++i) {
            float p = (float) ph, w;
            switch (sh) { case 1: w = p < 0.5f ? p * 4 - 1 : 3 - p * 4; break; case 2: w = p < 0.5f ? 1.0f : -1.0f; break; case 3: w = 1 - 2 * p; break; default: w = std::sin(kTwoPiF * p); }
            if (sh == 2) { sq += (w - sq) * 0.01f; w = sq; }
            float trem = 1.0f - dep * (0.5f - 0.5f * w);
            float pa = (w * pan * dep + 1.0f) * 0.25f * kPiF;
            float gl = trem * (1 - pan) + pan * std::cos(pa) * 1.4142f, gr = trem * (1 - pan) + pan * std::sin(pa) * 1.4142f;
            L[i] *= gl; R[i] *= gr;
            ph += rate / sr; if (ph >= 1) ph -= 1;
        }
    }
};

// ======================================================================== DELAY (tape-like time changes)
class Delay : public Effect {
public:
    Delay() : Effect("dly") {
        addOnOff("sync", "SYNC", true); add("time", "TIME", 1, 2000, 375, Unit::Ms, true); addChoice("div", "DIVISION", divisionNames(), 10);
        add("fb", "FEEDBACK", 0, 110, 40, Unit::Percent); addChoice("mode", "MODE", { "Stereo", "Ping-pong", "Mono" }, 1);
        add("tone", "TONE", 500, 20000, 6000, Unit::Hz, true); add("lc", "LOW CUT", 20, 2000, 120, Unit::Hz, true);
        add("wob", "WOBBLE", 0, 100, 10, Unit::Percent); add("mix", "MIX", 0, 100, 30, Unit::Percent);
    }
    DelayLine dl[2]; OnePole lp[2], hp[2]; float fbv[2] = { 0, 0 }, timeS = 0, mixS = 0.3f; double wph = 0;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (4.2 * sr)); timeS = target(TimeInfo()); }
    void reset() override { for (auto& d : dl) d.clear(); fbv[0] = fbv[1] = 0; for (auto& f : lp) f.reset(); for (auto& f : hp) f.reset(); }
    double tailSeconds() const override { return 4.0 + val(3) / 20.0; }
    float target(const TimeInfo& t) const {
        double ms = params[0]->on() ? divisionBeats(params[2]->geti()) * 60000.0 / std::max(20.0, t.bpm) : val(1);
        return (float) std::min(4.0 * sr, ms * 0.001 * sr); }
    void process(float* L, float* R, int n, const TimeInfo& t) override {
        float tg = target(t), fb = val(3) / 100.0f; int mode = params[4]->geti(); float wob = val(7) / 100.0f;
        for (auto& f : lp) f.setHz(sr, val(5)); for (auto& f : hp) f.setHz(sr, val(6));
        for (int i = 0; i < n; ++i) {
            timeS += (tg - timeS) * 0.00025f;                       // a slow glide: changing TIME bends the pitch like tape
            wph += 0.7 / sr; if (wph >= 1) wph -= 1;
            float d = timeS * (1.0f + wob * 0.004f * std::sin(kTwoPiF * (float) wph));
            float yl = dl[0].readCubic(d), yr = dl[1].readCubic(d);
            yl = hp[0].hp(lp[0].lp(yl)); yr = hp[1].hp(lp[1].lp(yr));
            float inL = L[i], inR = R[i];
            if (mode == 1) { dl[0].push(0.5f * (inL + inR) + softClip(yr * fb)); dl[1].push(softClip(yl * fb)); }
            else if (mode == 2) { float m = 0.5f * (inL + inR) + softClip(0.5f * (yl + yr) * fb); dl[0].push(m); dl[1].push(m); }
            else { dl[0].push(inL + softClip(yl * fb)); dl[1].push(inR + softClip(yr * fb)); }
            L[i] = yl; R[i] = yr;
        }
        float w = val(8) / 100.0f;
        for (int i = 0; i < n; ++i) { mixS += (w - mixS) * 0.002f;
            float dg = std::min(1.0f, 2.0f - 2.0f * mixS), wg = std::min(1.0f, 2.0f * mixS);
            L[i] = dryL[(size_t) i] * dg + L[i] * wg; R[i] = dryR[(size_t) i] * dg + R[i] * wg; }
    }
};

// ======================================================================== REVERB (8-line feedback delay network, shimmer, freeze)
class Reverb : public Effect {
public:
    Reverb() : Effect("rev") {
        add("size", "SIZE", 0, 100, 60, Unit::Percent); add("decay", "DECAY", 0.2f, 30, 3, Unit::Sec, true);
        add("damp", "DAMPING", 1000, 20000, 7000, Unit::Hz, true); add("pre", "PRE-DELAY", 0, 250, 15, Unit::Ms);
        add("shim", "SHIMMER", 0, 100, 0, Unit::Percent); add("wid", "WIDTH", 0, 100, 100, Unit::Percent);
        addOnOff("hold", "HOLD", false); add("mix", "MIX", 0, 100, 30, Unit::Percent);
    }
    static constexpr int N = 8;
    DelayLine fd[N], pre, shd; OnePole lp[N]; SchroederAP ap[4]; float st[N] = {};
    double mph = 0, shp = 0; float shy = 0, holdS = 0, mixS = 0.3f;
    void onPrepare() override {
        for (auto& d : fd) d.setMax((int) (0.2 * sr)); pre.setMax((int) (0.3 * sr)); shd.setMax((int) (0.2 * sr));
        static const float APMS[4] = { 4.771f, 3.595f, 12.73f, 9.307f };
        for (int i = 0; i < 4; ++i) ap[i].init((int) (APMS[i] * 0.001 * sr), 0.62f);
    }
    void reset() override { for (auto& d : fd) d.clear(); pre.clear(); shd.clear(); for (auto& a : ap) a.clear(); for (auto& f : lp) f.reset(); std::fill(st, st + N, 0.0f); shy = 0; }
    double tailSeconds() const override { return params[6]->on() ? 600.0 : val(1) * 1.2 + 0.3; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        static const float MS[N] = { 23.1f, 29.3f, 35.7f, 41.9f, 47.3f, 53.9f, 61.7f, 71.3f };
        float sz = 0.45f + 1.4f * val(0) / 100.0f, rt = val(1), shim = val(4) / 100.0f, wid = val(5) / 100.0f;
        bool hold = params[6]->on(); int pd = (int) (val(3) * 0.001 * sr);
        for (auto& f : lp) f.setHz(sr, val(2));
        float len[N], g[N];
        for (int k = 0; k < N; ++k) { len[k] = (float) (MS[k] * 0.001 * sr * sz); g[k] = (float) std::pow(10.0, -3.0 * len[k] / (rt * sr)); }
        const float shw = (float) (0.08 * sr);
        for (int i = 0; i < n; ++i) {
            holdS += ((hold ? 1.0f : 0.0f) - holdS) * 0.0008f;
            pre.push(0.5f * (L[i] + R[i]));
            float x = (pd > 0 ? pre.tap(pd) : 0.5f * (L[i] + R[i]));
            // shimmer: an octave above the reverb, fed back in
            shp += -1.0 / shw; if (shp < 0) shp += 1.0;
            float q = (float) shp, wv = std::sin(kPiF * q); wv *= wv;
            float sh = shd.read(q * shw + 2) * wv + shd.read(std::fmod(q + 0.5f, 1.0f) * shw + 2) * (1 - wv);
            x = (x + softClip(shy * 1.5f) * shim * 0.35f) * (1.0f - holdS);
            for (auto& a : ap) x = a.process(x);
            mph += 0.23 / sr; if (mph >= 1) mph -= 1;
            float r[N], sum = 0;
            for (int k = 0; k < N; ++k) {
                r[k] = fd[k].read(len[k] + 6.0f * std::sin(kTwoPiF * (float) (mph + k * 0.125)) + 8.0f);
                float damped = lp[k].lp(r[k]);
                damped = damped + (r[k] - damped) * holdS;
                st[k] = damped * (g[k] + (1.0f - g[k]) * holdS);
                sum += st[k];
            }
            float hs = sum * (2.0f / N);
            for (int k = 0; k < N; ++k) { float o = st[k] - hs + ((k & 1) ? -x : x) * 0.5f; fd[k].push(softClip(o * 0.4f) * 2.5f); }
            float wl = (r[0] - r[2] + r[4] - r[6]) * 0.35f, wr = (r[1] - r[3] + r[5] - r[7]) * 0.35f;
            shd.push(0.5f * (wl + wr)); shy = sh;
            float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * wid;
            L[i] = m + s; R[i] = m - s;
        }
        float w = val(7) / 100.0f;
        for (int i = 0; i < n; ++i) { mixS += (w - mixS) * 0.002f;
            float dg = std::min(1.0f, 2.0f - 2.0f * mixS), wg = std::min(1.0f, 2.0f * mixS);
            L[i] = dryL[(size_t) i] * dg + L[i] * wg; R[i] = dryR[(size_t) i] * dg + R[i] * wg; }
    }
};

// ======================================================================== HALO - three voices that follow the pitch you play
class Halo : public Effect {
public:
    Halo() : Effect("halo") {
        add("v1", "VOICE 1", -24, 24, 7, Unit::Semi); add("v2", "VOICE 2", -24, 24, 12, Unit::Semi); add("v3", "VOICE 3", -24, 24, -12, Unit::Semi);
        add("spr", "SPREAD", 0, 100, 30, Unit::Percent); add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    DelayLine dl; PitchTracker pt; double p[3] = { 0, 0, 0 }; float sm[3] = { 0, 0, 0 }, gv[3] = { 0, 0, 0 }, winS = 3000, mixS = 0.5f;
    std::vector<float> mono;
    void onPrepare() override { dl.setMax((int) (0.5 * sr)); pt.prepare(sr); winS = (float) (0.07 * sr); mono.assign((size_t) maxBlock, 0.0f); }
    void reset() override { dl.clear(); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (L[i] + R[i]);
        pt.push(mono.data(), n);
        float per = pt.period;
        float wt = per > 8 ? 2.0f * std::max(1.0f, std::round((float) (0.035 * sr) / per)) * per : (float) (0.07 * sr);
        float spr = val(3) / 100.0f; float det = spr * 14.0f;
        float a = (1 - spr * 0.8f) * 0.785398f, b = (1 + spr * 0.8f) * 0.785398f;
        for (int i = 0; i < n; ++i) {
            dl.push(mono[(size_t) i]);
            winS += (wt - winS) * 0.0005f;
            float y[3];
            for (int v = 0; v < 3; ++v) {
                float t = val(v); sm[v] += (t - sm[v]) * 0.0006f; gv[v] += ((std::abs(t) > 0.5f ? 1.0f : 0.0f) - gv[v]) * 0.0006f;
                float cents = v == 0 ? -det : (v == 1 ? det : det * 0.5f);
                double r = std::pow(2.0, (sm[v] + cents * 0.01) / 12.0);
                p[v] += (1.0 - r) / winS; p[v] -= std::floor(p[v]);
                float q = (float) p[v], w = std::sin(kPiF * q); w *= w;
                float q2 = q + 0.5f; if (q2 >= 1) q2 -= 1;
                y[v] = (dl.readCubic(q * winS + 2) * w + dl.readCubic(q2 * winS + 2) * (1 - w)) * gv[v];
            }
            float wl = (y[0] * std::cos(a) + y[1] * std::cos(b)) * 1.4142f + y[2];
            float wr = (y[0] * std::sin(a) + y[1] * std::sin(b)) * 1.4142f + y[2];
            L[i] = wl * 0.55f; R[i] = wr * 0.55f;
        }
        meter[0].store(pt.hz);
        float w = val(4) / 100.0f;
        for (int i = 0; i < n; ++i) { mixS += (w - mixS) * 0.002f;
            float dg = std::min(1.0f, 2.0f - 2.0f * mixS), wg = std::min(1.0f, 2.0f * mixS);
            L[i] = dryL[(size_t) i] * dg + L[i] * wg; R[i] = dryR[(size_t) i] * dg + R[i] * wg; }
    }
};

// ======================================================================== PITCH (shift by semitones, two overlapping grains)
class Pitch : public Effect {
public:
    Pitch() : Effect("pit") {
        add("st", "PITCH", -24, 24, -12, Unit::Semi); add("fine", "FINE", -100, 100, 0, Unit::Cents);
        add("win", "WINDOW", 20, 150, 60, Unit::Ms, true); add("mix", "MIX", 0, 100, 100, Unit::Percent);
    }
    DelayLine dl[2]; double p = 0; float mixS = 1;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (0.4 * sr)); }
    void reset() override { for (auto& d : dl) d.clear(); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float win = (float) (val(2) * 0.001 * sr); double r = std::pow(2.0, (val(0) + val(1) * 0.01) / 12.0);
        for (int i = 0; i < n; ++i) {
            dl[0].push(L[i]); dl[1].push(R[i]);
            p += (1.0 - r) / win; p -= std::floor(p);
            float q = (float) p, w = std::sin(kPiF * q); w *= w; float q2 = q + 0.5f; if (q2 >= 1) q2 -= 1;
            L[i] = dl[0].readCubic(q * win + 2) * w + dl[0].readCubic(q2 * win + 2) * (1 - w);
            R[i] = dl[1].readCubic(q * win + 2) * w + dl[1].readCubic(q2 * win + 2) * (1 - w);
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(3) / 100.0f, mixS);
    }
};

// ======================================================================== FREEZE (hold a moment of sound forever)
class Freeze : public Effect {
public:
    Freeze() : Effect("frz") {
        addOnOff("hold", "HOLD", false); add("tex", "TEXTURE", 0, 100, 40, Unit::Percent);
        add("len", "LENGTH", 100, 2000, 600, Unit::Ms, true); add("lvl", "LEVEL", -48, 6, 0, Unit::dB);
        add("dry", "DRY", -60, 0, 0, Unit::dB);
    }
    std::vector<float> buf[2]; int cap = 0, capLen = 0; bool capturing = false, have = false, lastHold = false;
    struct G { bool on = false; float pos = 0, age = 0, dur = 1, rate = 1, pan = 0.5f; };
    G g[6]; float tm = 0, outG = 0; Rng rng;
    void onPrepare() override { for (auto& b : buf) b.assign((size_t) (2.1 * sr), 0.0f); }
    void reset() override { capturing = false; for (auto& x : g) x.on = false; }
    double tailSeconds() const override { return params[0]->on() ? 600.0 : 0.5; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        bool hold = params[0]->on();
        if (hold && !lastHold) { capturing = true; cap = 0; capLen = (int) std::min((double) buf[0].size() - 4, val(2) * 0.001 * sr); }
        lastHold = hold;
        float tex = val(1) / 100.0f; float gd = (float) ((0.08 + 0.4 * tex) * sr);
        float lvl = dbToGain(val(3)), dry = dbToGain(val(4));
        for (int i = 0; i < n; ++i) {
            if (capturing) { buf[0][(size_t) cap] = L[i]; buf[1][(size_t) cap] = R[i]; if (++cap >= capLen) { capturing = false; have = true; } }
            float yl = 0, yr = 0;
            if (have) {
                tm += 4.0f / gd;
                if (tm >= 1.0f) { tm -= 1.0f;
                    for (auto& x : g) if (!x.on) { x.on = true; x.age = 0; x.dur = gd; x.rate = std::pow(2.0f, rng.bi() * tex * 0.004f);
                        x.pos = rng.uni() * std::max(1.0f, (float) capLen - gd * 1.02f - 4.0f); x.pan = 0.5f + 0.4f * rng.bi(); break; } }
                for (auto& x : g) if (x.on) {
                    float ps = x.pos + x.age * x.rate; int i0 = (int) ps; float fr = ps - (float) i0;
                    if (i0 + 1 >= capLen) { x.on = false; continue; }
                    float w = std::sin(kPiF * x.age / x.dur); w *= w;
                    float vl = buf[0][(size_t) i0] + (buf[0][(size_t) i0 + 1] - buf[0][(size_t) i0]) * fr;
                    float vr = buf[1][(size_t) i0] + (buf[1][(size_t) i0 + 1] - buf[1][(size_t) i0]) * fr;
                    yl += vl * w * (1.4f - x.pan); yr += vr * w * (0.4f + x.pan);
                    if (++x.age >= x.dur) x.on = false;
                }
            }
            outG += ((hold && have ? 1.0f : 0.0f) - outG) * 0.0005f;
            L[i] = L[i] * dry + yl * 0.5f * lvl * outG; R[i] = R[i] * dry + yr * 0.5f * lvl * outG;
        }
        if (!hold && outG < 0.001f) have = false;
        meter[0].store(hold ? (capturing ? 1.0f : 2.0f) : 0.0f);
    }
};

// ======================================================================== GRAIN (a cloud of small pieces of the last seconds)
class Grain : public Effect {
public:
    Grain() : Effect("grn") {
        add("size", "SIZE", 10, 500, 80, Unit::Ms, true); add("dens", "DENSITY", 1, 80, 12, Unit::None, true);
        add("scat", "SCATTER", 0, 2000, 300, Unit::Ms); add("pitch", "PITCH", -24, 24, 0, Unit::Semi);
        add("rnd", "RANDOM PITCH", 0, 12, 0, Unit::None); add("rev", "REVERSE", 0, 100, 0, Unit::Percent);
        add("spr", "SPREAD", 0, 100, 60, Unit::Percent); add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    DelayLine dl[2]; struct Gr { bool on = false; float d0 = 0, age = 0, dur = 1, rate = 1, pan = 0.5f; bool rev = false; };
    Gr g[24]; float tm = 0, mixS = 0.5f; Rng rng;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (3.0 * sr)); }
    void reset() override { for (auto& d : dl) d.clear(); for (auto& x : g) x.on = false; }
    double tailSeconds() const override { return 2.5; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float dur = (float) (val(0) * 0.001 * sr), dens = val(1);
        for (int i = 0; i < n; ++i) {
            dl[0].push(L[i]); dl[1].push(R[i]);
            tm += dens / (float) sr;
            if (tm >= 1) { tm -= 1;
                for (auto& x : g) if (!x.on) {
                    x.on = true; x.age = 0; x.dur = dur;
                    float st = val(3) + rng.bi() * val(4);
                    x.rate = std::pow(2.0f, st / 12.0f); x.rev = rng.uni() * 100 < val(5);
                    float lead = x.rev ? 0.0f : std::max(0.0f, (x.rate - 1.0f) * dur);
                    x.d0 = 64 + lead + rng.uni() * (float) (val(2) * 0.001 * sr);
                    x.d0 = std::min(x.d0, (float) (2.9 * sr) - dur * (1.0f + x.rate));
                    x.pan = 0.5f + rng.bi() * 0.5f * val(6) / 100.0f; break; }
            }
            float yl = 0, yr = 0; int active = 0;
            for (auto& x : g) if (x.on) {
                ++active;
                float d = x.rev ? x.d0 + x.age * (1.0f + x.rate) : x.d0 + x.age * (1.0f - x.rate);
                d = std::max(1.0f, d);
                float w = std::sin(kPiF * x.age / x.dur); w *= w;
                float a = std::cos(x.pan * 1.5708f), b = std::sin(x.pan * 1.5708f);
                yl += dl[0].readCubic(d) * w * a * 1.4142f; yr += dl[1].readCubic(d) * w * b * 1.4142f;
                if (++x.age >= x.dur) x.on = false;
            }
            float nrm = 1.0f / std::sqrt(clampf(dens * dur / (float) sr, 1, 24));
            L[i] = yl * nrm; R[i] = yr * nrm;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(7) / 100.0f, mixS);
    }
};

// ======================================================================== REVERSE (what you play comes back backwards)
class Reverse : public Effect {
public:
    Reverse() : Effect("rvs") {
        addOnOff("sync", "SYNC", true); add("len", "LENGTH", 50, 2000, 500, Unit::Ms, true); addChoice("div", "DIVISION", divisionNames(), 6);
        add("fb", "REPEAT", 0, 90, 20, Unit::Percent); add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    DelayLine dl[2]; double ph = 0; float fbv[2] = { 0, 0 }, mixS = 0.5f;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (8.5 * sr)); }
    void reset() override { for (auto& d : dl) d.clear(); fbv[0] = fbv[1] = 0; }
    double tailSeconds() const override { return 4.0; }
    void process(float* L, float* R, int n, const TimeInfo& t) override {
        double ms = params[0]->on() ? divisionBeats(params[2]->geti()) * 60000.0 / std::max(20.0, t.bpm) : val(1);
        float rs = (float) std::min(4.0 * sr, ms * 0.001 * sr); float fb = val(3) / 100.0f;
        for (int i = 0; i < n; ++i) {
            ph += 1.0 / rs; if (ph >= 1) ph -= 1;
            float q = (float) ph, q2 = q + 0.5f; if (q2 >= 1) q2 -= 1; float w = std::sin(kPiF * q); w *= w;
            float yl = dl[0].read(2 * rs * q + 1) * w + dl[0].read(2 * rs * q2 + 1) * (1 - w);
            float yr = dl[1].read(2 * rs * q + 1) * w + dl[1].read(2 * rs * q2 + 1) * (1 - w);
            dl[0].push(L[i] + fbv[0] * fb); dl[1].push(R[i] + fbv[1] * fb); fbv[0] = yl; fbv[1] = yr;
            L[i] = yl; R[i] = yr;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(4) / 100.0f, mixS);
    }
};

// ======================================================================== RING
class Ring : public Effect {
public:
    Ring() : Effect("ring") {
        add("f", "FREQUENCY", 1, 5000, 220, Unit::Hz, true); add("drift", "DRIFT", 0, 100, 0, Unit::Percent);
        addChoice("sh", "SHAPE", { "Sine", "Square", "Triangle" }); add("mix", "MIX", 0, 100, 50, Unit::Percent);
    }
    double ph = 0, dph = 0; float mixS = 0.5f;
    void process(float* L, float* R, int n, const TimeInfo&) override {
        int sh = params[2]->geti(); float dr = val(1) / 100.0f;
        for (int i = 0; i < n; ++i) {
            dph += 0.13 / sr; if (dph >= 1) dph -= 1;
            double f = val(0) * std::pow(2.0, dr * std::sin(kTwoPiF * (float) dph));
            ph += f / sr; if (ph >= 1) ph -= std::floor(ph);
            float p = (float) ph, m = sh == 1 ? (p < 0.5f ? 1.0f : -1.0f) : (sh == 2 ? (p < 0.5f ? p * 4 - 1 : 3 - p * 4) : std::sin(kTwoPiF * p));
            L[i] *= m; R[i] *= m;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(3) / 100.0f, mixS);
    }
};

// ======================================================================== WIDTH (stereo image, mono bass)
class Width : public Effect {
public:
    Width() : Effect("wid") {
        add("w", "WIDTH", 0, 200, 100, Unit::Percent); add("mb", "MONO BASS", 20, 500, 20, Unit::Hz, true);
        add("bal", "BALANCE", -50, 50, 0, Unit::Pan);
    }
    OnePole lp[2];
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float w = val(0) / 100.0f; bool mb = val(1) > 21; for (auto& f : lp) f.setHz(sr, val(1));
        float b = val(2) / 50.0f, gl = std::min(1.0f, 1.0f - b), gr = std::min(1.0f, 1.0f + b);
        for (int i = 0; i < n; ++i) {
            float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            if (mb) { float sl = lp[0].lp(s); s -= sl; }
            s *= w; L[i] = (m + s) * gl; R[i] = (m - s) * gr;
        }
    }
};

// ======================================================================== LIMITER (look-ahead, never above the ceiling)
class Limiter : public Effect {
public:
    Limiter() : Effect("lim") {
        add("in", "INPUT", 0, 24, 0, Unit::dB); add("ceil", "CEILING", -12, 0, -0.3f, Unit::dB); add("rel", "RELEASE", 10, 800, 120, Unit::Ms, true);
    }
    DelayLine dl[2], pk; float g = 1; int la = 64;
    std::vector<float> win; int wpos = 0;
    void onPrepare() override { la = std::max(16, (int) (0.0015 * sr)); for (auto& d : dl) d.setMax(la + 8); win.assign((size_t) la, 0.0f); wpos = 0; }
    void reset() override { for (auto& d : dl) d.clear(); g = 1; std::fill(win.begin(), win.end(), 0.0f); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float ig = dbToGain(val(0)), ce = dbToGain(val(1)); float re = (float) (1.0 - std::exp(-1000.0 / (val(2) * sr))); float minG = 1;
        for (int i = 0; i < n; ++i) {
            float l = L[i] * ig, r = R[i] * ig;
            win[(size_t) wpos] = std::max(std::abs(l), std::abs(r)); wpos = (wpos + 1) % la;
            float pkv = 0; for (float v : win) pkv = std::max(pkv, v);       // the loudest sample in the next 1.5 ms
            float tg = pkv > ce ? ce / pkv : 1.0f;
            g = tg < g ? g + (tg - g) * 0.35f : g + (tg - g) * re;
            dl[0].push(l); dl[1].push(r);
            float ol = dl[0].tap(la - 1) * g, orr = dl[1].tap(la - 1) * g;
            L[i] = clampf(ol, -ce, ce); R[i] = clampf(orr, -ce, ce); minG = std::min(minG, g);
        }
        meter[0].store(-gainToDb(minG));
    }
};

// ======================================================================== UTILITY
class Utility : public Effect {
public:
    Utility() : Effect("util") {
        add("g", "GAIN", -48, 24, 0, Unit::dB); add("pan", "PAN", -50, 50, 0, Unit::Pan);
        addOnOff("il", "INVERT L"); addOnOff("ir", "INVERT R"); addOnOff("mono", "MONO"); addOnOff("swap", "SWAP L/R");
    }
    Smooth gs;
    void onPrepare() override { gs.setTime(sr, 20); gs.reset(dbToGain(val(0))); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        gs.target = dbToGain(val(0)); float p = (val(1) / 50.0f + 1.0f) * 0.25f * kPiF;
        float pl = std::cos(p) * 1.4142f, pr = std::sin(p) * 1.4142f;
        bool il = params[2]->on(), ir = params[3]->on(), mo = params[4]->on(), sw = params[5]->on();
        for (int i = 0; i < n; ++i) {
            float g = gs.next(), l = L[i], r = R[i];
            if (sw) std::swap(l, r);
            if (il) l = -l; if (ir) r = -r;
            if (mo) { float m = 0.5f * (l + r); l = r = m; }
            L[i] = l * g * std::min(1.0f, pl); R[i] = r * g * std::min(1.0f, pr);
        }
    }
};

// ======================================================================== TAPE (saturation, wow, flutter, age)
class Tape : public Effect {
public:
    Tape() : Effect("tape") {
        add("sat", "SATURATION", 0, 100, 30, Unit::Percent); add("wow", "WOW", 0, 100, 20, Unit::Percent);
        add("flut", "FLUTTER", 0, 100, 15, Unit::Percent); add("age", "AGE", 0, 100, 20, Unit::Percent);
        add("hiss", "HISS", 0, 100, 0, Unit::Percent); add("mix", "MIX", 0, 100, 100, Unit::Percent);
    }
    DelayLine dl[2]; double w1 = 0, w2 = 0; OnePole lp[2], hp[2]; Rng rng; float mixS = 1, drift = 0;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (0.06 * sr)); }
    void reset() override { for (auto& d : dl) d.clear(); }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        float sat = val(0) / 100.0f, wow = val(1) / 100.0f, fl = val(2) / 100.0f, age = val(3) / 100.0f, hiss = val(4) / 100.0f;
        for (auto& f : lp) f.setHz(sr, 18000.0 * std::pow(0.15, age)); for (auto& f : hp) f.setHz(sr, 20 + 200 * age);
        float drive = 1.0f + sat * 6.0f; float base = (float) (0.012 * sr);
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i) {
            w1 += (0.5 + 0.3 * drift) / sr; if (w1 >= 1) { w1 -= 1; drift = rng.bi(); } w2 += 9.5 / sr; if (w2 >= 1) w2 -= 1;
            float mod = wow * 0.0035f * (float) sr * std::sin(kTwoPiF * (float) w1) + fl * 0.0003f * (float) sr * std::sin(kTwoPiF * (float) w2);
            for (int c = 0; c < 2; ++c) {
                float x = softClip(io[c][i] * drive) / std::sqrt(drive);
                dl[c].push(x);
                float y = dl[c].readCubic(base + mod);
                y = hp[c].hp(lp[c].lp(y)) + hiss * 0.01f * rng.bi();
                io[c][i] = y;
            }
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(5) / 100.0f, mixS);
    }
};

// ======================================================================== STUTTER (repeats slices in time)
class Stutter : public Effect {
public:
    Stutter() : Effect("stut") {
        addChoice("div", "SLICE", divisionNames(), 12); add("ch", "CHANCE", 0, 100, 40, Unit::Percent);
        add("gate", "GATE", 10, 100, 100, Unit::Percent); add("mix", "MIX", 0, 100, 100, Unit::Percent);
    }
    std::vector<float> buf[2]; int bw = 0, bl = 0; long cnt = 0, slice = 1; bool rep = false; int repN = 0; Rng rng; double lastSlice = -1; float mixS = 1;
    void onPrepare() override { for (auto& b : buf) b.assign((size_t) (4.1 * sr), 0.0f); }
    void reset() override { rep = false; cnt = 0; }
    void process(float* L, float* R, int n, const TimeInfo& t) override {
        double beats = divisionBeats(params[0]->geti());
        slice = std::max(64L, std::min((long) buf[0].size() - 2, (long) (beats * 60.0 / std::max(20.0, t.bpm) * sr)));
        float gate = val(2) / 100.0f;
        for (int i = 0; i < n; ++i) {
            double b = t.beat + (double) i / sr * t.bpm / 60.0;
            double sl = std::floor(b / beats);
            bool start = t.playing ? sl != lastSlice : cnt >= slice;
            if (start) { lastSlice = sl; cnt = 0;
                if (rep && repN < 7 && rng.uni() * 100 < val(1)) { ++repN; }
                else if (rng.uni() * 100 < val(1) * 0.6f) { rep = true; repN = 0; bl = 0; }
                else { rep = false; } }
            if (!rep) { buf[0][(size_t) (cnt % slice)] = L[i]; buf[1][(size_t) (cnt % slice)] = R[i]; }
            else {
                long k = cnt % slice; float env = (float) k < gate * slice ? 1.0f : 0.0f;
                float fadeIn = std::min(1.0f, k / 64.0f), fadeOut = std::min(1.0f, (gate * slice - k) / 64.0f);
                env *= std::min(fadeIn, std::max(0.0f, fadeOut));
                L[i] = buf[0][(size_t) k] * env; R[i] = buf[1][(size_t) k] * env;
            }
            ++cnt;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(3) / 100.0f, mixS);
        meter[0].store(rep ? 1.0f : 0.0f);
    }
};

// ======================================================================== PUMP (rhythmic ducking, in tempo)
class Pump : public Effect {
public:
    Pump() : Effect("pump") {
        addChoice("div", "RATE", divisionNames(), 6); add("depth", "DEPTH", 0, 100, 70, Unit::Percent);
        add("curve", "CURVE", 0, 100, 50, Unit::Percent); add("off", "OFFSET", 0, 100, 0, Unit::Percent);
    }
    double ph = 0;
    void process(float* L, float* R, int n, const TimeInfo& t) override {
        double beats = divisionBeats(params[0]->geti());
        if (t.playing) ph = std::fmod(t.beat / beats + val(3) / 100.0, 1.0);
        double inc = t.bpm / 60.0 / beats / sr; float dep = val(1) / 100.0f, cu = 0.5f + val(2) / 100.0f * 4.0f;
        for (int i = 0; i < n; ++i) {
            float p = (float) ph, len = 0.25f + 0.65f * val(2) / 100.0f;      // a quick dip on the beat, then the sound swells back
            float e = p < len ? std::pow(1.0f - p / len, cu) : 0.0f;
            e *= std::min(1.0f, p * 300.0f);
            float g = 1.0f - dep * e;
            L[i] *= g; R[i] *= g;
            ph += inc; if (ph >= 1) ph -= 1;
        }
    }
};

// ======================================================================== RESONATOR (tuned strings that ring with the sound)
class Resonator : public Effect {
public:
    Resonator() : Effect("res") {
        add("note", "NOTE", 24, 84, 48, Unit::Note); addChoice("chord", "CHORD", { "Unison", "Octaves", "Fifths", "Major", "Minor", "Sus4", "Maj7", "Min7", "Cluster" }, 4);
        add("dec", "DECAY", 0.05f, 8, 1.5f, Unit::Sec, true); add("bri", "BRIGHT", 0, 100, 50, Unit::Percent);
        add("mix", "MIX", 0, 100, 40, Unit::Percent);
    }
    DelayLine dl[4]; OnePole lp[4]; float mixS = 0.4f;
    void onPrepare() override { for (auto& d : dl) d.setMax((int) (sr / 25.0)); }
    void reset() override { for (auto& d : dl) d.clear(); for (auto& f : lp) f.reset(); }
    double tailSeconds() const override { return val(2) * 1.2; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        static const int CH[9][4] = { { 0, 0, 0, 0 }, { 0, 12, 24, -12 }, { 0, 7, 12, 19 }, { 0, 4, 7, 12 }, { 0, 3, 7, 12 }, { 0, 5, 7, 12 },
                                      { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 2, 3, 7 } };
        int c = params[1]->geti(); float len[4], g[4];
        for (int k = 0; k < 4; ++k) { float f = noteToHz(val(0) + CH[c][k] + (k == 0 && c == 0 ? 0 : 0)); len[k] = (float) (sr / f);
            g[k] = (float) std::pow(10.0, -3.0 / (val(2) * f)); lp[k].setHz(sr, 800 + 15000 * val(3) / 100.0f); }
        for (int i = 0; i < n; ++i) {
            float x = 0.5f * (L[i] + R[i]) * 0.3f, yl = 0, yr = 0;
            for (int k = 0; k < 4; ++k) { float y = dl[k].read(len[k] - 1); dl[k].push(x + lp[k].lp(y) * g[k]); if (k & 1) yr += y; else yl += y; }
            L[i] = yl; R[i] = yr;
        }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(4) / 100.0f, mixS);
    }
};

// ======================================================================== AUTO-WAH (an envelope filter that talks)
class Wah : public Effect {
public:
    Wah() : Effect("wah") {
        add("sens", "SENSITIVITY", 0, 100, 60, Unit::Percent); add("lo", "FROM", 100, 1500, 300, Unit::Hz, true);
        add("hi", "TO", 600, 6000, 2500, Unit::Hz, true); add("q", "RESONANCE", 0, 100, 65, Unit::Percent);
        add("spd", "SPEED", 1, 300, 60, Unit::Ms, true); add("mix", "MIX", 0, 100, 100, Unit::Percent);
    }
    SVF f[2]; EnvFollow ef; float mixS = 1;
    void reset() override { f[0].reset(); f[1].reset(); ef.env = 0; }
    void process(float* L, float* R, int n, const TimeInfo&) override {
        ef.set(sr, 3, val(4)); float sens = val(0) / 100.0f * 8.0f;
        for (int o = 0; o < n; o += 16) { int m = std::min(16, n - o); float e = 0;
            for (int i = 0; i < m; ++i) e = ef.process(0.5f * (L[o + i] + R[o + i]));
            float t = clampf(e * sens, 0, 1); float fc = val(1) * std::pow(val(2) / val(1), t);
            for (auto& s : f) s.set(sr, fc, val(3) / 100.0f * 0.92f);
            for (int i = 0; i < m; ++i) { f[0].tick(L[o + i]); f[1].tick(R[o + i]); L[o + i] = f[0].bp * 1.6f; R[o + i] = f[1].bp * 1.6f; } }
        mixDryWet(L, R, dryL.data(), dryR.data(), n, val(5) / 100.0f, mixS);
    }
};

} // namespace

// ======================================================================== the catalogue
const std::vector<EffectInfo>& effectCatalog() {
    static const std::vector<EffectInfo> c = {
        { "eq", "EQ", "TONE", "low cut · shelves · bell · high cut", 0.35f, 0.80f, 0.86f, [] { return std::make_unique<EQ>(); } },
        { "flt", "FILTER", "TONE", "sweeps with an LFO or your playing", 0.70f, 0.80f, 0.30f, [] { return std::make_unique<Filter>(); } },
        { "wah", "AUTO-WAH", "TONE", "a filter that follows your dynamics", 0.95f, 0.62f, 0.35f, [] { return std::make_unique<Wah>(); } },
        { "comp", "COMPRESSOR", "DYNAMICS", "evens out the level", 0.96f, 0.78f, 0.32f, [] { return std::make_unique<Comp>(); } },
        { "gate", "GATE", "DYNAMICS", "silence between notes", 0.80f, 0.70f, 0.40f, [] { return std::make_unique<Gate>(); } },
        { "lim", "LIMITER", "DYNAMICS", "never louder than the ceiling", 0.92f, 0.45f, 0.40f, [] { return std::make_unique<Limiter>(); } },
        { "pump", "PUMP", "DYNAMICS", "ducks in tempo, like side-chain", 0.98f, 0.55f, 0.55f, [] { return std::make_unique<Pump>(); } },
        { "drv", "DRIVE", "COLOUR", "tube · overdrive · fuzz · fold", 1.00f, 0.50f, 0.28f, [] { return std::make_unique<Drive>(); } },
        { "tape", "TAPE", "COLOUR", "saturation, wow, flutter, age", 0.85f, 0.62f, 0.45f, [] { return std::make_unique<Tape>(); } },
        { "crush", "CRUSH", "COLOUR", "fewer bits, lower sample rate", 1.00f, 0.36f, 0.45f, [] { return std::make_unique<Crush>(); } },
        { "ring", "RING", "COLOUR", "bells and metal", 0.90f, 0.82f, 0.32f, [] { return std::make_unique<Ring>(); } },
        { "cho", "CHORUS", "MOTION", "three moving voices", 0.48f, 0.86f, 0.56f, [] { return std::make_unique<Chorus>(); } },
        { "fla", "FLANGER", "MOTION", "a jet sweeping through", 0.40f, 0.75f, 0.70f, [] { return std::make_unique<Flanger>(); } },
        { "phs", "PHASER", "MOTION", "sweeping notches", 0.93f, 0.42f, 0.78f, [] { return std::make_unique<Phaser>(); } },
        { "trm", "TREMOLO", "MOTION", "volume or auto-pan, in tempo", 0.96f, 0.70f, 0.40f, [] { return std::make_unique<Tremolo>(); } },
        { "dly", "DELAY", "SPACE", "tape echo in tempo", 0.32f, 0.62f, 1.00f, [] { return std::make_unique<Delay>(); } },
        { "rev", "REVERB", "SPACE", "big room with shimmer and hold", 0.45f, 0.48f, 1.00f, [] { return std::make_unique<Reverb>(); } },
        { "rvs", "REVERSE", "SPACE", "backwards echoes", 0.55f, 0.60f, 0.95f, [] { return std::make_unique<Reverse>(); } },
        { "res", "RESONATOR", "SPACE", "tuned strings ring along", 0.90f, 0.70f, 0.50f, [] { return std::make_unique<Resonator>(); } },
        { "halo", "HALO", "PITCH", "three voices at your intervals", 0.72f, 0.57f, 1.00f, [] { return std::make_unique<Halo>(); } },
        { "pit", "PITCH", "PITCH", "shift up or down", 0.86f, 0.48f, 0.95f, [] { return std::make_unique<Pitch>(); } },
        { "frz", "FREEZE", "TEXTURE", "hold a moment forever", 0.62f, 0.90f, 1.00f, [] { return std::make_unique<Freeze>(); } },
        { "grn", "GRAIN", "TEXTURE", "a cloud of tiny pieces", 0.85f, 0.90f, 0.45f, [] { return std::make_unique<Grain>(); } },
        { "stut", "STUTTER", "TEXTURE", "repeats slices in tempo", 1.00f, 0.45f, 0.65f, [] { return std::make_unique<Stutter>(); } },
        { "wid", "WIDTH", "UTILITY", "stereo width, mono bass", 0.55f, 0.75f, 0.95f, [] { return std::make_unique<Width>(); } },
        { "util", "UTILITY", "UTILITY", "gain, pan, phase, mono", 0.70f, 0.72f, 0.78f, [] { return std::make_unique<Utility>(); } },
    };
    return c;
}

const EffectInfo* effectInfo(const std::string& type) {
    for (auto& e : effectCatalog()) if (type == e.type) return &e;
    return nullptr;
}

std::unique_ptr<Effect> makeEffect(const std::string& type) {
    auto* i = effectInfo(type);
    if (i == nullptr) return nullptr;
    auto e = i->make(); e->uid = newUid(); return e;
}

} // namespace score
