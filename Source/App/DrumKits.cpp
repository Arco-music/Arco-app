#include "DrumKits.h"
#include <random>

namespace arco {

static const char* PADKEY[12] = { "kick", "snare", "rim", "xstick", "hhc", "hhp", "hho", "ltom", "htom", "ride", "bell", "crash" };
const char* DrumKits::padKey(int pad) { return PADKEY[juce::jlimit(0, 11, pad)]; }
juce::StringArray DrumKits::kitNames() { return { "JAZZ", "808", "909", "TRAP", "LO-FI" }; }
juce::String DrumKits::padName(int kit, int pad) {
    static const char* J[12] = { "KICK", "SNARE", "RIMSHOT", "CROSS STICK", "CLOSED HAT", "PEDAL HAT", "OPEN HAT", "LOW TOM", "HIGH TOM", "RIDE", "RIDE BELL", "CRASH" };
    static const char* E[12] = { "KICK", "SNARE", "RIM", "CLAP", "CLOSED HAT", "PEDAL HAT", "OPEN HAT", "LOW TOM", "HIGH TOM", "CYMBAL", "COWBELL", "CRASH" };
    static const char* T[12] = { "808 KICK", "SNARE", "RIM", "CLAP", "CLOSED HAT", "HAT TICK", "OPEN HAT", "LOW TOM", "HIGH TOM", "RIDE", "COWBELL", "CRASH" };
    pad = juce::jlimit(0, 11, pad);
    if (kit == 0 || kit == 4) return J[pad];
    if (kit == 3) return T[pad];
    return E[pad];
}
int DrumKits::midiNote(int pad) { static const int N[12] = { 36, 38, 40, 39, 42, 44, 46, 45, 48, 51, 53, 49 }; return N[juce::jlimit(0, 11, pad)]; }
int DrumKits::padForNote(int note) {
    for (int p = 0; p < 12; ++p) if (midiNote(p) == note) return p;
    switch (note) { case 35: return 0; case 37: return 3; case 41: case 43: return 7; case 47: case 50: return 8; case 57: return 11; case 59: return 9; case 56: return 10; default: return -1; }
}

// ---------------------------------------------------------------- modelling the classic machines
namespace {
constexpr double SR = 48000.0, TAU = 6.283185307179586;

struct Noise { std::mt19937 g; std::uniform_real_distribution<float> d { -1.0f, 1.0f }; explicit Noise(unsigned s) : g(s) {} float operator()() { return d(g); } };
// a biquad (RBJ), for rendering
struct BQ {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    static BQ make(int type, double f, double q) {     // 0 LP 1 HP 2 BP
        BQ z; double w = TAU * std::min(f, SR * 0.45) / SR, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        if (type == 0) { z.b0 = (1 - c) / 2; z.b1 = 1 - c; z.b2 = (1 - c) / 2; }
        else if (type == 1) { z.b0 = (1 + c) / 2; z.b1 = -(1 + c); z.b2 = (1 + c) / 2; }
        else { z.b0 = al; z.b1 = 0; z.b2 = -al; }
        z.a1 = -2 * c; z.a2 = 1 - al; z.b0 /= a0; z.b1 /= a0; z.b2 /= a0; z.a1 /= a0; z.a2 /= a0; return z; }
    float operator()(float x) { double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return (float) y; }
};
// the 808's six square-wave oscillators (its hi-hats and cymbal)
struct Metal {
    double ph[6] {}; double f[6];
    explicit Metal(double scale = 1.0) { const double F[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }; for (int i = 0; i < 6; ++i) { f[i] = F[i] * scale; ph[i] = i * 0.137; } }
    float operator()() { float s = 0; for (int i = 0; i < 6; ++i) { ph[i] += f[i] / SR; ph[i] -= std::floor(ph[i]); s += ph[i] < 0.5 ? 1.0f : -1.0f; } return s / 6.0f; }
};
float env(double t, double atk, double dec) { return (float) ((t < atk ? t / atk : 1.0) * std::exp(-(t - std::min(t, atk)) / dec)); }

DrumSample finish(std::vector<float> l, std::vector<float> r, float peakTo) {
    float pk = 1e-9f; for (float x : l) pk = std::max(pk, std::abs(x)); for (float x : r) pk = std::max(pk, std::abs(x));
    float g = peakTo / pk; for (auto& x : l) x *= g; for (auto& x : r) x *= g;
    // trim the silent tail, fade the last 20 ms
    size_t n = l.size(); while (n > 1000 && std::abs(l[n - 1]) < 1e-4f && (r.empty() || std::abs(r[n - 1]) < 1e-4f)) --n;
    l.resize(n); if (!r.empty()) r.resize(n);
    size_t fd = std::min<size_t>(n / 4, (size_t) (0.02 * SR));
    for (size_t i = 0; i < fd; ++i) { float k = (float) i / (float) fd; l[n - 1 - i] *= k; if (!r.empty()) r[n - 1 - i] *= k; }
    DrumSample s; s.l = std::move(l); s.r = std::move(r); s.rate = (float) SR; return s;
}

// one hit of a modelled kit: pad, how hard (0 soft / 1 hard), round robin (small differences), machine (1 808, 2 909, 3 TRAP)
DrumSample model(int machine, int pad, int layer, int rr) {
    Noise nz((unsigned) (machine * 1000 + pad * 31 + layer * 7 + rr * 3 + 1)), nz2((unsigned) (machine * 977 + pad * 13 + rr + 99));
    double v = layer == 1 ? 1.0 : 0.62, var = 1.0 + (rr == 0 ? -0.004 : 0.004);
    auto N = [](double sec) { return (size_t) (sec * SR); };
    std::vector<float> L, R;
    bool stereo = false;
    auto mono = [&](double sec, auto f) { L.resize(N(sec)); for (size_t i = 0; i < L.size(); ++i) L[i] = f(i / SR); };
    const bool m808 = machine == 1, m909 = machine == 2, trap = machine == 3;
    switch (pad) {
        case 0: {   // KICK
            double ph = 0; BQ click = BQ::make(1, 1800, 0.7);
            double f0 = m808 ? 48 : (trap ? 41 : 52), fs = m808 ? 95 : (trap ? 72 : 190), tk = m808 ? 0.035 : (trap ? 0.05 : 0.016);
            double dec = (m808 ? 0.42 : (trap ? 0.95 : 0.28)) * (layer ? 1.0 : 0.88), drive = m808 ? 1.3 : (trap ? 2.4 : 1.8);
            mono(dec * 4.5, [&](double t) {
                double f = (f0 + fs * std::exp(-t / tk)) * var; ph += TAU * f / SR;
                float body = (float) std::sin(ph) * env(t, 0.0012, dec);
                float cl = click(nz()) * env(t, 0.0003, m909 ? 0.004 : 0.0025) * (float) (m909 ? 0.9 : 0.45) * (float) v;
                return std::tanh((body + cl) * (float) drive * (float) (0.7 + 0.3 * v)) ; });
            break; }
        case 1: {   // SNARE
            double p1 = 0, p2 = 0; BQ hp = BQ::make(1, m808 ? 1200 : 2200, 0.7), lp = BQ::make(0, m808 ? 9000 : 12000, 0.7), bp = BQ::make(2, 5000, 0.6);
            double f1 = m808 ? 185 : 192, f2 = m808 ? 330 : 345, nd = (m808 ? 0.15 : (trap ? 0.11 : 0.19)) * (layer ? 1.0 : 0.85);
            mono(0.7, [&](double t) {
                p1 += TAU * f1 * var * (1 + 0.25 * std::exp(-t / 0.01)) / SR; p2 += TAU * f2 * var * (1 + 0.2 * std::exp(-t / 0.008)) / SR;
                float tone = (float) (std::sin(p1) * env(t, 0.0008, 0.07) * 0.7 + std::sin(p2) * env(t, 0.0008, 0.045) * 0.45);
                float n = nz(); float sn = lp(hp(n)) * env(t, 0.0005, nd) * (float) (0.55 + 0.45 * v);
                if (m909 || trap) sn += bp(n) * env(t, 0.0005, nd * 0.6) * 0.5f;
                return std::tanh((tone * (float) (m808 ? 1.0 : 0.85) + sn * (float) (trap ? 1.25 : 1.0)) * 1.4f); });
            break; }
        case 2: {   // RIM
            double p1 = 0, p2 = 0; BQ hp = BQ::make(1, 500, 0.7);
            mono(0.12, [&](double t) {
                p1 += TAU * 1700 * var / SR; p2 += TAU * 455 * var / SR;
                float sq = (std::sin(p1) > 0 ? 1.0f : -1.0f) * 0.35f + (std::sin(p2) > 0 ? 1.0f : -1.0f) * 0.5f;
                return hp(sq) * env(t, 0.0002, m808 ? 0.011 : 0.016) + nz() * env(t, 0.0001, 0.002) * 0.4f; });
            break; }
        case 3: {   // CLAP
            BQ bp = BQ::make(2, m808 ? 1150 : 1350, m808 ? 1.4 : 1.1), bp2 = BQ::make(2, 2400, 0.9);
            double gap = (m808 ? 0.0105 : 0.0085) * var, tail = (m909 ? 0.16 : 0.13) * (layer ? 1 : 0.9);
            stereo = true; L.resize(N(0.7)); R.resize(L.size());
            for (size_t i = 0; i < L.size(); ++i) {
                double t = i / SR; float e = 0;
                for (int k = 0; k < 3; ++k) { double tt = t - k * gap; if (tt >= 0) e += (float) std::exp(-tt / 0.0045) * (tt < 0.0004 ? (float) (tt / 0.0004) : 1.0f); }
                double tt = t - 3 * gap; if (tt >= 0) e += (float) std::exp(-tt / tail) * 0.75f;
                float n1 = nz(), n2 = nz2();
                L[i] = (bp(n1) * 1.0f + bp2(n1) * 0.35f) * e; R[i] = (bp(n1 * 0.7f + n2 * 0.3f) * 1.0f + bp2(n2) * 0.35f) * e;
            }
            break; }
        case 4: case 5: case 6: {   // HATS
            Metal mt(m909 ? 1.18 : 1.0); BQ bp = BQ::make(2, m909 ? 9500 : 7800, 1.2), hp = BQ::make(1, m909 ? 7000 : 6200, 0.7), hp2 = BQ::make(1, 8000, 0.7);
            double dec = pad == 4 ? (trap ? 0.028 : (m909 ? 0.05 : 0.042)) : (pad == 5 ? 0.06 : (trap ? 0.22 : (m909 ? 0.42 : 0.36)));
            if (pad == 6) dec *= layer ? 1.0 : 0.85;
            float mix = m808 ? 0.85f : 0.45f;     // 808: the metal; 909: more noise
            mono(dec * 6 + 0.02, [&](double t) {
                float s = bp(mt()) * mix + hp2(nz()) * (1.0f - mix) * 0.8f;
                float e = env(t, 0.0004, dec); if (pad == 5) e *= 0.75f;
                return hp(s) * e * 3.0f; });
            break; }
        case 7: case 8: {   // TOMS
            double ph = 0; bool hi = pad == 8; BQ lp = BQ::make(0, 600, 0.7);
            double f0 = (hi ? 150 : 92) * (m909 ? 1.15 : 1.0) * var, dec = (hi ? 0.28 : 0.38) * (layer ? 1 : 0.9);
            mono(dec * 5, [&](double t) {
                double f = f0 * (1 + 0.32 * std::exp(-t / 0.05)); ph += TAU * f / SR;
                return std::tanh(((float) std::sin(ph) * env(t, 0.001, dec) + lp(nz()) * env(t, 0.0005, 0.02) * (m909 ? 0.6f : 0.25f)) * 1.3f); });
            break; }
        case 9: case 11: {   // RIDE / CYMBAL, CRASH
            bool crash = pad == 11; Metal mt(crash ? 1.6 : 1.42), mt2(crash ? 2.3 : 1.95);
            BQ bpA = BQ::make(2, crash ? 6500 : 5200, 0.9), bpB = BQ::make(2, crash ? 6500 : 5200, 0.9), hpA = BQ::make(1, crash ? 3800 : 4200, 0.7), hpB = BQ::make(1, crash ? 3800 : 4200, 0.7);
            double dec = crash ? (m909 ? 1.9 : 1.6) : (m808 ? 1.2 : 2.2);
            stereo = true; L.resize(N(dec * 4)); R.resize(L.size());
            for (size_t i = 0; i < L.size(); ++i) {
                double t = i / SR; float e = env(t, crash ? 0.003 : 0.0008, dec), tr = env(t, 0.0003, crash ? 0.06 : 0.02);
                float m = mt() * 0.6f + mt2() * 0.4f; float n1 = nz(), n2 = nz2();
                float nmix = m808 ? 0.35f : 0.7f;
                L[i] = hpA(bpA(m) * (1 - nmix) * 2.0f + n1 * nmix) * (e + tr * 0.6f);
                R[i] = hpB(bpB(m) * (1 - nmix) * 2.0f + n2 * nmix) * (e + tr * 0.6f);
            }
            break; }
        case 10: {   // COWBELL / BELL
            double p1 = 0, p2 = 0; BQ bp = BQ::make(2, 2600, 1.3);
            mono(0.9, [&](double t) {
                p1 += TAU * 540 * var / SR; p2 += TAU * 800 * var / SR;
                float sq = ((std::fmod(p1, TAU) < TAU / 2) ? 1.0f : -1.0f) + ((std::fmod(p2, TAU) < TAU / 2) ? 1.0f : -1.0f) * 0.85f;
                float e = (float) (0.65 * std::exp(-t / 0.012) + 0.35 * std::exp(-t / 0.16)) * (t < 0.0005 ? (float) (t / 0.0005) : 1.0f);
                return bp(sq) * e; });
            break; }
        default: break;
    }
    (void) stereo;
    return finish(std::move(L), std::move(R), (float) (0.85 * (layer ? 1.0 : 0.78)));
}

// the jazz kit through an old sampler and a tape: 12 bits at 26 kHz, rounded off, saturated
DrumSample lofi(const DrumSample& in, int pad) {
    DrumSample o; o.rate = in.rate;
    BQ lpA = BQ::make(0, 6500, 0.6), lpB = BQ::make(0, 6500, 0.6), hpA = BQ::make(1, pad == 0 ? 35 : 90, 0.7), hpB = BQ::make(1, pad == 0 ? 35 : 90, 0.7);
    const float step = 48000.0f / 26040.0f, q = 2048.0f;
    auto run = [&](const std::vector<float>& x, BQ& lp, BQ& hp) {
        std::vector<float> y(x.size()); float held = 0, acc = step;
        for (size_t i = 0; i < x.size(); ++i) {
            acc += 1.0f; if (acc >= step) { acc -= step; held = std::round(x[i] * q) / q; }
            y[i] = std::tanh(hp(lp(held)) * 1.6f) * 0.72f;
        }
        return y; };
    o.l = run(in.l, lpA, hpA); if (!in.r.empty()) o.r = run(in.r, lpB, hpB);
    return o;
}
} // namespace

const DrumBank* DrumKits::bank() {
    static const DrumBank* b = [] {
        auto* bk = new DrumBank();
        juce::FlacAudioFormat flac;
        // JAZZ: the samples built in
        auto jazz = std::make_shared<DrumKit>(); jazz->name = "JAZZ";
        for (int p = 0; p < 12; ++p) {
            auto& pad = jazz->pads[(size_t) p]; pad.name = padName(0, p).toStdString();
            pad.choke = (p >= 4 && p <= 6) ? 1 : 0;
            for (int l = 0; l < 2; ++l) for (int r = 0; r < 2; ++r) {
                juce::String nm = juce::String(PADKEY[p]) + "_" + juce::String(l) + "_" + juce::String(r) + "_flac";
                int sz = 0; const char* d = BinaryData::getNamedResource(nm.toRawUTF8(), sz);
                if (d == nullptr) continue;
                std::unique_ptr<juce::AudioFormatReader> rd(flac.createReaderFor(new juce::MemoryInputStream(d, (size_t) sz, false), true));
                if (!rd) continue;
                juce::AudioBuffer<float> buf((int) rd->numChannels, (int) rd->lengthInSamples);
                rd->read(&buf, 0, (int) rd->lengthInSamples, 0, true, true);
                auto& s = pad.s[(size_t) l][(size_t) r]; s.rate = (float) rd->sampleRate;
                s.l.assign(buf.getReadPointer(0), buf.getReadPointer(0) + buf.getNumSamples());
                if (buf.getNumChannels() > 1) s.r.assign(buf.getReadPointer(1), buf.getReadPointer(1) + buf.getNumSamples());
            }
        }
        bk->kits.push_back(jazz);
        for (int m = 1; m <= 3; ++m) {
            auto k = std::make_shared<DrumKit>(); k->name = kitNames()[m].toStdString();
            for (int p = 0; p < 12; ++p) {
                auto& pad = k->pads[(size_t) p]; pad.name = padName(m, p).toStdString(); pad.choke = (p >= 4 && p <= 6) ? 1 : 0;
                for (int l = 0; l < 2; ++l) for (int r = 0; r < 2; ++r) pad.s[(size_t) l][(size_t) r] = model(m, p, l, r);
            }
            bk->kits.push_back(k);
        }
        auto lf = std::make_shared<DrumKit>(); lf->name = "LO-FI";
        for (int p = 0; p < 12; ++p) {
            auto& pad = lf->pads[(size_t) p]; pad.name = padName(4, p).toStdString(); pad.choke = jazz->pads[(size_t) p].choke;
            for (int l = 0; l < 2; ++l) for (int r = 0; r < 2; ++r) pad.s[(size_t) l][(size_t) r] = lofi(jazz->pads[(size_t) p].s[(size_t) l][(size_t) r], p);
        }
        bk->kits.push_back(lf);
        return bk;
    }();
    return b;
}

} // namespace arco
