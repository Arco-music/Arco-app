#include "Modular.h"

namespace arco {

namespace {
constexpr float kC4 = 261.6256f;
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float polyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}
inline double divBeats(int d) { static const double v[6] = { 4, 2, 1, 0.5, 0.25, 0.125 }; return v[juce::jlimit(0, 5, d)]; }
inline double lfoBeats(int d) { static const double v[8] = { 0, 16, 8, 4, 2, 1, 0.5, 0.25 }; return v[juce::jlimit(0, 7, d)]; }
const int scaleMasks[6] = {
    0xFFF,                                                   // chromatic
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),
};
}

int modCategory(int t) {
    switch (t) {
        case MVco: case MNoise: case MSupersaw: case MFmOp: case MWave: case MString: return CatSound;
        case MKick: case MSnare: case MHat: return CatDrum;
        case MVcf: case MSvf: case MComb: case MFormant: case MEq3: return CatFilter;
        case MVca: case MFold: case MDrive: case MCrush: case MRectify: case MComp: case MMix: case MMix8: case MStMix: case MPan: return CatShape;
        case MDelay: case MReverb: case MChorus: case MPhaser: return CatFx;
        case MLfo: case MEnv: case MAr: case MRandom: case MChaos: case MFollow: case MSah: case MSlew: return CatMove;
        case MClock: case MSeq: case MDivider: case MEuclid: case MBernoulli: case MCounter: return CatTime;
        case MKeys: case MArp: case MQuant: case MChord: case MTranspose: return CatPitch;
        case MAtt: case MRing: case MInvert: case MXfade: case MSwitch: case MCompare: case MMinMax: case MConst: case MLogic: return CatMath;
        case MOut: case MIn: return CatIO;
        case MScope: case MXy: case MSpectrum: case MLife: case MBounce: case MOrbit: case MPendulum: case MTuring: case MCellular: return CatVisual;
        case MGrains: case MFreqShift: case MTape: case MLooper: case MPingPong: return CatFx;
        case MQuadLfo: case MFunction: return CatMove;
        case MBurst: return CatTime;
        case MResonator: return CatFilter;
        case MHarmonics: return CatSound;
        default: return CatMath;
    }
}

void Modular::rebuild() {
    std::vector<Rt> old; old.swap(rt);
    for (auto& sc : scopes) sc.id = 0;
    rt.reserve(patch.mods.size());
    for (auto& m : patch.mods) {
        Rt r;
        for (auto& o : old) if (o.id == m.id && o.type == m.type) { r = std::move(o); break; }
        r.id = m.id; r.type = m.type; r.p = m.p;
        for (int k = 0; k < 8; ++k) { r.inMod[k] = -1; r.inPort[k] = 0; }
        int want = 0;
        switch (m.type) {
            case MDelay: want = (int) (sr * 2.1) + 4; break;
            case MString: want = (int) (sr / 15.0) + 4; break;
            case MComb: want = (int) (sr / 18.0) + 4; break;
            case MReverb: want = (int) (sr * 0.36) + 64; break;
            case MChorus: want = (int) (sr * 0.06) + 4; break;
            case MLife: want = 256; break;
            case MCellular: want = 256; break;
            case MGrains: want = (int) (sr * 2.5) + 16 * 6; break;
            case MTape: want = (int) (sr * 0.1) + 4; break;
            case MLooper: want = (int) (sr * 10.0) + 4; break;
            case MPingPong: want = 2 * ((int) (sr * 2.1) + 4); break;
            default: break;
        }
        if (want > 0 && r.bw != want) { r.buf.assign((size_t) want, 0.0f); r.bw = want; }
        rt.push_back(std::move(r));
    }
    { int k = 0; for (auto& r : rt) { r.scope = -1; if (modIsVisual(r.type) && k < 16) { scopes[k].id = r.id; r.scope = k++; } } }
    auto indexOf = [this](int id) { for (size_t i = 0; i < rt.size(); ++i) if (rt[i].id == id) return (int) i; return -1; };
    for (auto& c : patch.cables) {
        int f = indexOf(c.fromMod), t = indexOf(c.toMod);
        if (f < 0 || t < 0 || c.toPort < 0 || c.toPort > 7 || c.fromPort < 0 || c.fromPort > 7) continue;
        rt[(size_t) t].inMod[c.toPort] = f; rt[(size_t) t].inPort[c.toPort] = c.fromPort;
    }
}

void Modular::process(float* L, float* R, int n, double bpm, double beat) {
    if (! running.load()) return;
    const juce::ScopedTryLock sl(lock);
    if (! sl.isLocked()) return;
    if (rt.empty()) return;
    if (bpm <= 1) bpm = 120;
    bpmNow = bpm;
    const bool own = beat < 0;
    double b0 = own ? ownBeat : beat;
    const double beatPerSample = bpm / 60.0 / sr;
    // the keyboard's state for this block
    int note; float vel; bool gate; bool retrig;
    { const juce::SpinLock::ScopedLockType nl(noteLock); note = curNote; vel = curVel; gate = gateOn; retrig = trigCount != lastTrigCount; lastTrigCount = trigCount;
      arpCount = juce::jmin(16, held.size()); for (int k = 0; k < arpCount; ++k) arpNotes[k] = held[k]; std::sort(arpNotes, arpNotes + arpCount); }
    blockN = n; blockI = 0;
    const float isr = (float) (1.0 / sr);
    float peak = 0;
    for (int i = 0; i < n; ++i) {
        const double bt = b0 + i * beatPerSample;
        float outL = 0, outR = 0;
        for (auto& m : rt) {
            auto P = [&m](int k) { return m.p[(size_t) k]; };
            switch (m.type) {
            case MKeys: {
                float target = (note - 60) / 12.0f + std::round(P(0));
                float g = P(1);
                if (g <= 0.01f) keysPitch = target;
                else { float c = 1.0f - std::exp(-1.0f / (g * 0.01f * (float) sr)); keysPitch += (target - keysPitch) * c; }
                m.out[0] = keysPitch;
                m.out[1] = (gate && ! (retrig && i == 0)) ? 10.0f : 0.0f;
                m.out[2] = vel * 10.0f;
                break;
            }
            case MVco: {
                float v = std::round(P(0)) + P(1) / 12.0f + P(2) / 1200.0f + in(m, 0) + in(m, 1) * 0.2f * P(3) / 100.0f * 4.0f;
                double f = kC4 * std::pow(2.0, (double) clampf(v, -10, 7));
                double dt = juce::jmin(0.45, f / sr);
                float sync = in(m, 3); bool trig = sync > 1.0f;
                if (connected(m, 3) && trig && ! m.lastTrig) m.ph = 0;
                m.lastTrig = trig;
                float pw = clampf(P(4) / 100.0f + in(m, 2) * 0.09f, 0.03f, 0.97f);
                double t = m.ph;
                float sine = (float) std::sin(t * juce::MathConstants<double>::twoPi);
                float tri = (float) (t < 0.5 ? 4 * t - 1 : 3 - 4 * t);
                float saw = (float) (2 * t - 1) - polyBlep(t, dt);
                double t2 = t + (1.0 - pw); if (t2 >= 1) t2 -= 1;
                float sq = (t < pw ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(t2, dt);
                m.out[0] = sine * 5; m.out[1] = tri * 5; m.out[2] = saw * 5; m.out[3] = sq * 5;
                m.ph += dt; if (m.ph >= 1) m.ph -= 1;
                break;
            }
            case MLfo: {
                int sync = (int) std::round(P(1));
                bool rs = in(m, 1) > 1.0f;
                if (connected(m, 1) && rs && ! m.lastTrig) m.ph = 0;
                m.lastTrig = rs;
                double prev = m.ph;
                if (sync > 0) { double len = lfoBeats(sync); m.ph = std::fmod(bt / len, 1.0); if (m.ph < 0) m.ph += 1; }
                else { double rate = P(0) * std::pow(2.0, (double) clampf(in(m, 0), -8, 8)); m.ph += rate / sr; if (m.ph >= 1) m.ph -= std::floor(m.ph); }
                if (m.ph < prev) m.held = m.rnd.nextFloat() * 2 - 1;
                double t = m.ph;
                float s = (float) std::sin(t * juce::MathConstants<double>::twoPi), tr = (float) (t < 0.5 ? 4 * t - 1 : 3 - 4 * t);
                float sw = (float) (2 * t - 1), sq = t < 0.5 ? 1.0f : -1.0f;
                float vals[5] = { s, tr, sw, sq, m.held };
                bool bi = P(2) >= 0.5f;
                for (int k = 0; k < 5; ++k) m.out[k] = bi ? vals[k] * 5 : (vals[k] + 1) * 5;
                break;
            }
            case MEnv: {
                bool g = in(m, 0) > 1.0f, rt2 = in(m, 1) > 1.0f;
                if ((g && ! m.lastGate) || (rt2 && ! m.lastTrig && g)) m.stage = 1;
                if (! g && m.lastGate) m.stage = 4;
                m.lastGate = g; m.lastTrig = rt2;
                float sus = P(2) / 100.0f;
                switch (m.stage) {
                case 1: { float a = juce::jmax(0.0005f, P(0)); m.env += isr / a * 1.2f * (1.25f - m.env); if (m.env >= 1) { m.env = 1; m.stage = 2; } break; }
                case 2: { float d = juce::jmax(0.002f, P(1)); m.env = sus + (m.env - sus) * std::exp(-isr * 4.6f / d); if (std::abs(m.env - sus) < 1e-4f) m.stage = 3; break; }
                case 3: m.env = sus; break;
                case 4: { float r = juce::jmax(0.002f, P(3)); m.env *= std::exp(-isr * 4.6f / r); if (m.env < 1e-5f) { m.env = 0; m.stage = 0; } break; }
                default: m.env = 0; break;
                }
                m.out[0] = m.env * 10; m.out[1] = -m.env * 10;
                break;
            }
            case MVcf: {
                float cut = P(0) * std::pow(2.0f, clampf(in(m, 1) * P(2) / 100.0f, -10, 10));
                cut = clampf(cut, 16.0f, (float) sr * 0.45f);
                float res = clampf(P(1) / 100.0f + in(m, 2) * 0.1f, 0.0f, 1.0f);
                float drive = 1.0f + P(3) / 25.0f;
                float x = std::tanh(in(m, 0) * 0.2f * drive);
                // 4-pole ladder (one-pole cascade, tanh feedback)
                float g = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * cut / (float) sr);
                float k = res * 3.9f;
                float u = std::tanh(x - k * m.z[3]);
                m.z[0] += g * (u - m.z[0]); m.z[1] += g * (m.z[0] - m.z[1]); m.z[2] += g * (m.z[1] - m.z[2]); m.z[3] += g * (m.z[2] - m.z[3]);
                float lp = m.z[3] * (1.0f + k * 0.5f);
                // state variable for BP / HP
                float G = std::tan(juce::MathConstants<float>::pi * cut / (float) sr), Kq = 2.0f - 1.9f * res;
                float hp = (x - (Kq + G) * m.z[4] - m.z[5]) / (1 + Kq * G + G * G);
                float bp = G * hp + m.z[4]; m.z[4] = G * hp + bp;
                float lp2 = G * bp + m.z[5]; m.z[5] = G * bp + lp2;
                for (int j = 0; j < 6; ++j) if (! std::isfinite(m.z[j])) m.z[j] = 0;
                m.out[0] = clampf(lp, -2, 2) * 5 / drive * 1.4f; m.out[1] = clampf(bp, -2, 2) * 5; m.out[2] = clampf(hp, -2, 2) * 5;
                break;
            }
            case MVca: {
                float a = P(0) / 100.0f + (connected(m, 1) ? juce::jmax(0.0f, in(m, 1)) / 10.0f * P(1) / 100.0f : 0.0f);
                m.out[0] = in(m, 0) * clampf(a, 0, 1.5f);
                break;
            }
            case MMix: m.out[0] = in(m, 0) * P(0) / 100.0f + in(m, 1) * P(1) / 100.0f + in(m, 2) * P(2) / 100.0f + in(m, 3) * P(3) / 100.0f; break;
            case MNoise: {
                float w = m.rnd.nextFloat() * 2 - 1;
                m.pink[0] = 0.99765f * m.pink[0] + w * 0.0990460f; m.pink[1] = 0.96300f * m.pink[1] + w * 0.2965164f; m.pink[2] = 0.57000f * m.pink[2] + w * 1.0526913f;
                m.out[0] = w * 5; m.out[1] = clampf((m.pink[0] + m.pink[1] + m.pink[2] + w * 0.1848f) * 0.25f, -1, 1) * 5;
                break;
            }
            case MSah: {
                bool t = in(m, 1) > 1.0f;
                if (t && ! m.lastTrig) m.held = connected(m, 0) ? in(m, 0) : (m.rnd.nextFloat() * 2 - 1) * 5;
                m.lastTrig = t; m.out[0] = m.held;
                break;
            }
            case MSeq: {
                int steps = juce::jlimit(1, 8, (int) std::round(P(8)));
                bool rs = in(m, 1) > 1.0f;
                if (rs && ! m.lastGate) { m.step = 0; m.clk = 0; }
                m.lastGate = rs;
                float gateFrac = P(9) / 100.0f; float frac;
                if (connected(m, 0)) {
                    bool c = in(m, 0) > 1.0f;
                    m.clk += 1;
                    if (c && ! m.lastTrig) { m.slew = (float) m.clk; m.clk = 0; m.step = (m.step + 1) % steps; }
                    m.lastTrig = c;
                    frac = m.slew > 0 ? (float) (m.clk / m.slew) : 0.0f;
                } else {
                    double len = divBeats((int) std::round(P(10)));
                    double pos = bt / len; long idx = (long) std::floor(pos);
                    m.step = (int) (((idx % steps) + steps) % steps);
                    frac = (float) (pos - std::floor(pos));
                }
                m.step %= steps;
                m.out[0] = std::round(P(m.step)) / 12.0f;
                m.out[1] = frac < gateFrac ? 10.0f : 0.0f;
                m.out[2] = (float) m.step;
                break;
            }
            case MClock: {
                double len = divBeats((int) std::round(P(0)));
                double per[4] = { len, len / 2, len / 4, len * 2 };
                for (int k = 0; k < 4; ++k) { double f = std::fmod(bt / per[k], 1.0); if (f < 0) f += 1; m.out[k] = f < 0.5 ? 10.0f : 0.0f; }
                break;
            }
            case MDelay: {
                if (m.bw < 8) { m.out[0] = in(m, 0); break; }
                float target = clampf(P(0) * std::pow(2.0f, clampf(in(m, 1) * 0.1f, -3, 3)), 0.001f, 2.0f);
                m.slew += (target - m.slew) * 0.0005f; if (m.slew <= 0) m.slew = target;
                double d = m.slew * sr; double rp = m.step - d; while (rp < 0) rp += m.bw;
                int i0 = (int) rp; float fr = (float) (rp - i0); int i1 = (i0 + 1) % m.bw;
                float wet = m.buf[(size_t) i0] * (1 - fr) + m.buf[(size_t) i1] * fr;
                float x = in(m, 0);
                float fbv = x + wet * P(1) / 100.0f;
                m.z[0] += 0.35f * (fbv - m.z[0]);                // darker repeats
                m.buf[(size_t) m.step] = 5.0f * std::tanh(m.z[0] * 0.2f);
                m.step = (m.step + 1) % m.bw;
                float mix = P(2) / 100.0f;
                m.out[0] = x * (1 - mix) + wet * mix;
                break;
            }
            case MOut: {
                float lv = P(0) / 100.0f * 0.2f;
                float l = in(m, 0), r = connected(m, 1) ? in(m, 1) : l;
                if (! connected(m, 0)) l = r;
                outL += l * lv; outR += r * lv;
                break;
            }
            case MAtt:
                m.out[0] = in(m, 0) * P(0) / 100.0f + P(1);
                m.out[1] = in(m, 1) * P(2) / 100.0f + P(3);
                break;
            case MQuant: {
                int mask = scaleMasks[juce::jlimit(0, 5, (int) std::round(P(0)))]; int key = juce::jlimit(0, 11, (int) std::round(P(1)));
                float semi = in(m, 0) * 12.0f; int base = (int) std::floor(semi);
                int best = base; float bd = 1e9f;
                for (int s = base - 6; s <= base + 6; ++s) { int pc = ((s - key) % 12 + 12) % 12; if (mask & (1 << pc)) { float d = std::abs(semi - s); if (d < bd) { bd = d; best = s; } } }
                m.out[0] = best / 12.0f;
                break;
            }
            case MSlew: {
                float x = in(m, 0); float t = x > m.held ? P(0) : P(1);
                if (t <= 0.0005f) m.held = x; else m.held += (x - m.held) * (1.0f - std::exp(-isr / t));
                m.out[0] = m.held;
                break;
            }
            case MFold: {
                float amt = 1.0f + (P(0) / 100.0f + juce::jmax(0.0f, in(m, 1)) * 0.1f) * 6.0f;
                float x = in(m, 0) * 0.2f * amt + P(1) / 100.0f;
                // triangle folding into -1..1
                float y = x + 1.0f; y = y - 4.0f * std::floor(y / 4.0f); y = y < 2 ? y - 1 : 3 - y;
                m.out[0] = y * 5;
                break;
            }
            default: stepMore(m, i, bt, isr, outL, outR); break;
            }
        }
        outL = clampf(outL, -1.5f, 1.5f); outR = clampf(outR, -1.5f, 1.5f);
        // soft limit so a wild patch never blows anything
        outL = std::tanh(outL * 0.9f) / 0.9f * 0.98f; outR = std::tanh(outR * 0.9f) / 0.9f * 0.98f;
        if (L) L[i] += outL; if (R) R[i] += outR;
        peak = juce::jmax(peak, std::abs(outL), std::abs(outR));
    }
    if (own) ownBeat = b0 + n * beatPerSample;
    outLevel.store(juce::jmax(peak, outLevel.load() * 0.9f));
}


// ------------------------------------------------------------------ 2.3 the newer modules
namespace {
inline double hzOf(float v) { return kC4 * std::pow(2.0, (double) clampf(v, -10, 7)); }
inline float trigUp(float v, bool& last) { bool t = v > 1.0f; bool up = t && !last; last = t; return up ? 1.0f : 0.0f; }
inline float svfTick(float x, float g, float k, float& s1, float& s2, float& bp, float& hp) {
    hp = (x - (k + g) * s1 - s2) / (1 + k * g + g * g);
    bp = g * hp + s1; s1 = g * hp + bp;
    float lp = g * bp + s2; s2 = g * bp + lp;
    return lp;
}
}

void Modular::stepMore(Rt& m, int i, double bt, float isr, float& outL, float& outR) {
    auto P = [&m](int k) { return m.p[(size_t) k]; };
    const float fs = (float) sr;
    switch (m.type) {
    case MSupersaw: {
        int nv = juce::jlimit(1, 7, (int) std::round(P(3)));
        double f = hzOf(std::round(P(0)) + in(m, 0));
        static const float det[7] = { 0.0f, -0.11f, 0.11f, -0.06f, 0.06f, -0.02f, 0.02f };
        float l = 0, r = 0, side = P(2) / 100.0f;
        for (int v = 0; v < nv; ++v) {
            double dt = juce::jmin(0.45, f * (1.0 + det[v] * P(1) / 100.0 * 0.5) / sr);
            double& ph = m.d[v]; ph += dt; if (ph >= 1) ph -= 1;
            float saw = (float) (2 * ph - 1) - polyBlep(ph, dt);
            float g = v == 0 ? 1.0f : side;
            float pan = v == 0 ? 0.0f : (v % 2 ? -0.8f : 0.8f);
            l += saw * g * (1 - pan) * 0.5f; r += saw * g * (1 + pan) * 0.5f;
        }
        float norm = 2.0f / (1.0f + side * (nv - 1) * 0.6f);
        m.out[0] = l * norm * 5; m.out[1] = r * norm * 5;
        break;
    }
    case MFmOp: {
        double f = hzOf(std::round(P(0)) + in(m, 0));
        double fm = f * P(1);
        m.ph2 += fm / sr; if (m.ph2 >= 1) m.ph2 -= std::floor(m.ph2);
        float idx = P(2) + (connected(m, 1) ? in(m, 1) * 0.5f : 0.0f);
        float modv = (float) std::sin((m.ph2 + m.z[0] * P(3) / 100.0f * 0.3) * juce::MathConstants<double>::twoPi);
        m.z[0] = modv;
        m.ph += f / sr; if (m.ph >= 1) m.ph -= 1;
        m.out[0] = 5.0f * (float) std::sin((m.ph + modv * idx * 0.159) * juce::MathConstants<double>::twoPi);
        break;
    }
    case MWave: {
        double f = hzOf(std::round(P(0)) + P(1) / 12.0f + in(m, 0));
        double dt = juce::jmin(0.45, f / sr);
        double t = m.ph; m.ph += dt; if (m.ph >= 1) m.ph -= 1;
        float sine = (float) std::sin(t * juce::MathConstants<double>::twoPi), tri = (float) (t < 0.5 ? 4 * t - 1 : 3 - 4 * t);
        float saw = (float) (2 * t - 1) - polyBlep(t, dt);
        double t2 = t + 0.5; if (t2 >= 1) t2 -= 1;
        float sq = (t < 0.5 ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(t2, dt);
        float mo = clampf(P(2) + in(m, 1) * 0.3f * P(3) / 100.0f, 0, 3);
        int a = (int) std::floor(mo); float fr = mo - a; float w[4] = { sine, tri, saw, sq };
        m.out[0] = 5.0f * (w[juce::jmin(3, a)] * (1 - fr) + w[juce::jmin(3, a + 1)] * fr);
        break;
    }
    case MString: {
        if (m.bw < 8) break;
        double f = juce::jmax(20.0, hzOf(std::round(P(0)) + in(m, 1)));
        int len = juce::jlimit(2, m.bw - 1, (int) std::round(sr / f));
        if (trigUp(in(m, 0), m.lastTrig) > 0) for (int k = 0; k < len; ++k) m.buf[(size_t) k] = (m.rnd.nextFloat() * 2 - 1);
        int rp = m.step % len, np = (rp + 1) % len;
        float y = m.buf[(size_t) rp];
        float bright = 0.2f + 0.8f * P(2) / 100.0f, decay = 0.990f + 0.0099f * P(1) / 100.0f;
        float nv = (y * bright + m.buf[(size_t) np] * (1 - bright) * 0.5f + y * (1 - bright) * 0.5f) * decay;
        m.buf[(size_t) rp] = nv; m.step = np;
        m.out[0] = y * 5;
        break;
    }
    case MKick: {
        if (trigUp(in(m, 0), m.lastTrig) > 0) { m.env = 1; m.held = 1; }
        float dec = juce::jmax(0.02f, P(1));
        m.env *= std::exp(-isr * 4.6f / dec); m.held *= std::exp(-isr * 4.6f / 0.04f);
        float f0 = P(0) * std::pow(2.0f, clampf(in(m, 1), -2, 2));
        float f = f0 * (1.0f + m.held * 4.0f * P(2) / 100.0f);
        m.ph += f / sr; if (m.ph >= 1) m.ph -= 1;
        float x = (float) std::sin(m.ph * juce::MathConstants<double>::twoPi) * m.env;
        float dr = 1.0f + P(3) / 100.0f * 6.0f;
        m.out[0] = 5.0f * std::tanh(x * dr) / std::tanh(dr);
        break;
    }
    case MSnare: {
        if (trigUp(in(m, 0), m.lastTrig) > 0) { m.env = 1; m.held = 1; m.ph = 0; }
        float dec = juce::jmax(0.02f, P(2));
        m.env *= std::exp(-isr * 4.6f / dec); m.held *= std::exp(-isr * 4.6f / (dec * 0.6f));
        m.ph += P(0) / sr; if (m.ph >= 1) m.ph -= 1;
        float tone = (float) std::sin(m.ph * juce::MathConstants<double>::twoPi) * m.held;
        float nz = m.rnd.nextFloat() * 2 - 1; m.z[0] += 0.6f * (nz - m.z[0]); float hpn = nz - m.z[0];
        float sn = P(1) / 100.0f;
        m.out[0] = 5.0f * clampf(tone * (1 - sn * 0.6f) + hpn * m.env * sn * 1.4f, -1.5f, 1.5f);
        break;
    }
    case MHat: {
        if (trigUp(in(m, 0), m.lastTrig) > 0) m.env = 1;
        m.env *= std::exp(-isr * 4.6f / juce::jmax(0.005f, P(0)));
        // six square partials (the metal) and noise, high-passed
        static const float ratio[6] = { 2.0f, 3.0f, 4.16f, 5.43f, 6.79f, 8.21f };
        float met = 0; for (int k = 0; k < 6; ++k) { m.d[k] += 300.0 * ratio[k] / sr; if (m.d[k] >= 1) m.d[k] -= 1; met += m.d[k] < 0.5 ? 1.0f : -1.0f; }
        float nz = m.rnd.nextFloat() * 2 - 1, mt = P(2) / 100.0f;
        float x = met / 6.0f * mt + nz * (1 - mt);
        float g = std::tan(juce::MathConstants<float>::pi * clampf(P(1), 500, fs * 0.45f) / fs), bp, hp;
        svfTick(x, g, 1.2f, m.z[0], m.z[1], bp, hp);
        m.out[0] = 5.0f * clampf(hp * m.env * 1.6f, -1.5f, 1.5f);
        break;
    }
    case MSvf: {
        float cut = clampf(P(0) * std::pow(2.0f, clampf(in(m, 1) * P(3) / 100.0f, -10, 10)), 16.0f, fs * 0.45f);
        float res = clampf(P(1) / 100.0f + in(m, 2) * 0.1f, 0, 0.98f);
        float g = std::tan(juce::MathConstants<float>::pi * cut / fs), k = 2.0f - 2.0f * res, bp, hp;
        float x = in(m, 0) * 0.2f;
        float lp = svfTick(x, g, k, m.z[0], m.z[1], bp, hp);
        float notch = lp + hp;
        float ty = clampf(P(2), 0, 3); int a = (int) std::floor(ty); float fr = ty - a; float w[4] = { lp, bp, hp, notch };
        for (int j = 0; j < 2; ++j) if (!std::isfinite(m.z[j])) m.z[j] = 0;
        m.out[0] = 5.0f * clampf(w[juce::jmin(3, a)] * (1 - fr) + w[juce::jmin(3, a + 1)] * fr, -2, 2);
        m.out[1] = 5.0f * clampf(lp, -2, 2); m.out[2] = 5.0f * clampf(bp, -2, 2); m.out[3] = 5.0f * clampf(hp, -2, 2);
        break;
    }
    case MComb: {
        if (m.bw < 8) break;
        float f = clampf(P(0) * std::pow(2.0f, clampf(in(m, 1), -4, 4)), 18.0f, 8000.0f);
        double dl = juce::jlimit(1.0, (double) m.bw - 2, sr / f);
        double rp = m.step - dl; while (rp < 0) rp += m.bw;
        int i0 = (int) rp; float fr = (float) (rp - i0); float y = m.buf[(size_t) i0] * (1 - fr) + m.buf[(size_t) ((i0 + 1) % m.bw)] * fr;
        float x = in(m, 0);
        m.buf[(size_t) m.step] = clampf(x + y * P(1) / 100.0f, -20, 20); m.step = (m.step + 1) % m.bw;
        float mix = P(2) / 100.0f; m.out[0] = x * (1 - mix) + y * mix;
        break;
    }
    case MFormant: {
        static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };   // a e i o u
        float v = clampf(P(0) + in(m, 1) * 0.4f, 0, 4); int a = (int) std::floor(v); float fr = v - a; int b = juce::jmin(4, a + 1);
        float x = in(m, 0) * 0.2f, y = 0, k = 2.0f - 1.9f * P(1) / 100.0f;
        for (int j = 0; j < 3; ++j) {
            float f = F[a][j] * (1 - fr) + F[b][j] * fr;
            float g = std::tan(juce::MathConstants<float>::pi * clampf(f, 50, fs * 0.45f) / fs), bp, hp;
            svfTick(x, g, k, m.z[j * 2], m.z[j * 2 + 1], bp, hp); y += bp * (j == 0 ? 1.0f : j == 1 ? 0.7f : 0.4f);
        }
        float mix = P(2) / 100.0f; m.out[0] = 5.0f * clampf(x * (1 - mix) + y * mix * 1.5f, -2, 2);
        break;
    }
    case MEq3: {
        float x = in(m, 0) * 0.2f;
        // a low / high split (one-pole) and a band at MID FREQ
        float gl = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 250.0f / fs), gh = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 4000.0f / fs);
        m.z[0] += gl * (x - m.z[0]); float low = m.z[0];
        m.z[1] += gh * (x - m.z[1]); float high = x - m.z[1];
        float g = std::tan(juce::MathConstants<float>::pi * clampf(P(2), 50, fs * 0.45f) / fs), bp, hp;
        svfTick(x, g, 1.4f, m.z[2], m.z[3], bp, hp);
        auto db = [](float d) { return std::pow(10.0f, d / 20.0f) - 1.0f; };
        m.out[0] = 5.0f * clampf(x + low * db(P(0)) + bp * db(P(1)) * 1.4f + high * db(P(3)), -3, 3);
        break;
    }
    case MDrive: {
        float x = in(m, 0) * 0.2f;
        float d = 1.0f + (P(0) / 100.0f + juce::jmax(0.0f, in(m, 1)) * 0.1f) * 20.0f;
        int mode = juce::jlimit(0, 3, (int) std::round(P(1)));
        float y = x * d;
        if (mode == 0) y = std::tanh(y);
        else if (mode == 1) y = clampf(y, -1, 1);
        else if (mode == 2) { float t = y + 1.0f; t = t - 4.0f * std::floor(t / 4.0f); y = t < 2 ? t - 1 : 3 - t; }
        else y = y > 0 ? std::tanh(y) : std::tanh(y * 0.5f) * 0.8f;           // tube: uneven
        float tc = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * (500.0f + 15000.0f * P(2) / 100.0f) / fs);
        m.z[0] += tc * (y - m.z[0]);
        m.out[0] = 5.0f * m.z[0] * P(3) / 100.0f * 1.4f;
        break;
    }
    case MCrush: {
        int down = juce::jlimit(1, 64, (int) std::round(P(1)));
        if (++m.step >= down) { m.step = 0; float q = std::pow(2.0f, std::round(P(0)) - 1); m.held = std::round(in(m, 0) * 0.2f * q) / q * 5.0f; }
        float mix = P(2) / 100.0f; m.out[0] = in(m, 0) * (1 - mix) + m.held * mix;
        break;
    }
    case MRectify: m.out[0] = std::abs(in(m, 0)); m.out[1] = juce::jmax(0.0f, in(m, 0)); break;
    case MComp: {
        float x = in(m, 0), key = connected(m, 1) ? in(m, 1) : x;
        float a = std::abs(key) * 0.2f;
        float at = 1.0f - std::exp(-1.0f / (P(2) * 0.001f * fs)), rl = 1.0f - std::exp(-1.0f / (P(3) * 0.001f * fs));
        m.env += (a > m.env ? at : rl) * (a - m.env);
        float over = 20.0f * std::log10(juce::jmax(1e-6f, m.env)) - P(0);
        float red = over > 0 ? over * (1 - 1 / juce::jmax(1.0f, P(1))) : 0.0f;
        float g = std::pow(10.0f, (P(4) - red) / 20.0f);
        m.out[0] = x * g; m.out[1] = red / 2.0f;
        break;
    }
    case MReverb: {
        if (m.bw < 64) break;
        // four combs and two all-passes a side (Schroeder), in one buffer
        static const int CL[4] = { 1116, 1188, 1277, 1356 }, AL[2] = { 556, 441 };
        const float k = (float) (sr / 44100.0) * (0.5f + 0.5f * P(0) / 100.0f);
        float fb = 0.70f + 0.27f * P(0) / 100.0f, damp = 0.05f + 0.6f * P(1) / 100.0f;
        float xin = (in(m, 0) + (connected(m, 1) ? in(m, 1) : in(m, 0))) * 0.5f * 0.2f * 0.25f;
        float outs[2] {};
        int base = 0;
        for (int side = 0; side < 2; ++side) {
            float acc = 0;
            for (int c = 0; c < 4; ++c) {
                int len = juce::jlimit(8, 8000, (int) (CL[c] * k) + side * 23);
                int seg = base; base += 8100;
                if (base > m.bw) break;
                int& pos = m.pos[side * 6 + c];
                if (pos < 0 || pos >= len) pos = 0;
                float y = m.buf[(size_t) (seg + pos)];
                float& lp = m.z[side * 4 + c];
                lp = y * (1 - damp) + lp * damp;
                m.buf[(size_t) (seg + pos)] = xin + lp * fb;
                pos = (pos + 1) % len; acc += y;
            }
            for (int a = 0; a < 2; ++a) {
                int len = juce::jlimit(8, 1000, (int) (AL[a] * (float) (sr / 44100.0)) + side * 23);
                int seg = base; base += 1100;
                if (base > m.bw) break;
                int& pos = m.pos[side * 6 + 4 + a];
                if (pos < 0 || pos >= len) pos = 0;
                float b = m.buf[(size_t) (seg + pos)];
                float y = -acc + b; m.buf[(size_t) (seg + pos)] = acc + b * 0.5f; acc = y;
                pos = (pos + 1) % len;
            }
            outs[side] = acc;
        }
        float mix = P(2) / 100.0f;
        float dl = in(m, 0), dr = connected(m, 1) ? in(m, 1) : in(m, 0);
        m.out[0] = dl * (1 - mix) + outs[0] * 5.0f * 3.0f * mix; m.out[1] = dr * (1 - mix) + outs[1] * 5.0f * 3.0f * mix;
        break;
    }
    case MChorus: {
        if (m.bw < 8) break;
        float x = in(m, 0);
        m.buf[(size_t) m.step] = x;
        m.ph += P(0) / sr; if (m.ph >= 1) m.ph -= 1;
        float o[2];
        for (int s2 = 0; s2 < 2; ++s2) {
            double lfo = std::sin((m.ph + s2 * 0.25) * juce::MathConstants<double>::twoPi);
            double dl = (0.007 + 0.006 * P(1) / 100.0 * (1 + lfo)) * sr;
            double rp = m.step - dl; while (rp < 0) rp += m.bw;
            int i0 = (int) rp; float fr = (float) (rp - i0);
            o[s2] = m.buf[(size_t) i0] * (1 - fr) + m.buf[(size_t) ((i0 + 1) % m.bw)] * fr;
        }
        m.step = (m.step + 1) % m.bw;
        float mix = P(2) / 100.0f;
        m.out[0] = x * (1 - mix * 0.5f) + o[0] * mix; m.out[1] = x * (1 - mix * 0.5f) + o[1] * mix;
        break;
    }
    case MPhaser: {
        m.ph += P(0) * std::pow(2.0f, clampf(in(m, 1), -4, 4)) / sr; if (m.ph >= 1) m.ph -= 1;
        float sw = 0.5f + 0.5f * (float) std::sin(m.ph * juce::MathConstants<double>::twoPi) * P(1) / 100.0f;
        float f = 200.0f * std::pow(20.0f, sw);
        float c = (std::tan(juce::MathConstants<float>::pi * clampf(f, 20, fs * 0.45f) / fs) - 1) / (std::tan(juce::MathConstants<float>::pi * clampf(f, 20, fs * 0.45f) / fs) + 1);
        float x = in(m, 0) * 0.2f + m.z[8] * P(2) / 100.0f, y = x;
        for (int k = 0; k < 4; ++k) { float o = c * y + m.z[k]; m.z[k] = y - c * o; y = o; }
        m.z[8] = clampf(y, -2, 2);
        float mix = P(3) / 100.0f; m.out[0] = 5.0f * (in(m, 0) * 0.2f * (1 - mix * 0.5f) + y * mix);
        break;
    }
    case MPan: {
        float pos = clampf(P(0) / 100.0f + in(m, 1) * 0.1f * P(1) / 100.0f * 2.0f, -1, 1);
        float a = (pos + 1) * 0.25f * juce::MathConstants<float>::pi;
        m.out[0] = in(m, 0) * std::cos(a) * 1.414f; m.out[1] = in(m, 0) * std::sin(a) * 1.414f;
        break;
    }
    case MStMix: {
        float l = 0, r = 0;
        for (int k = 0; k < 4; ++k) { float x = in(m, k) * P(k) / 100.0f, a = (clampf(P(4 + k) / 100.0f, -1, 1) + 1) * 0.25f * juce::MathConstants<float>::pi; l += x * std::cos(a) * 1.414f; r += x * std::sin(a) * 1.414f; }
        m.out[0] = l; m.out[1] = r;
        break;
    }
    case MAr: {
        bool g = in(m, 0) > 1.0f, loop = P(2) >= 0.5f;
        if ((g && !m.lastGate) || (loop && m.stage == 0)) m.stage = 1;
        m.lastGate = g;
        float eoc = 0;
        if (m.stage == 1) { m.env += isr / juce::jmax(0.0005f, P(0)); if (m.env >= 1) { m.env = 1; m.stage = 2; } }
        else if (m.stage == 2) { if (!g || loop) { m.env -= isr / juce::jmax(0.002f, P(1)); if (m.env <= 0) { m.env = 0; m.stage = 0; eoc = 10; } } }
        m.out[0] = m.env * 10; m.out[1] = eoc > 0 ? 10.0f : (m.out[1] > 0 && m.held < 0.005f ? 10.0f : 0.0f);
        m.held = eoc > 0 ? 0.0f : m.held + isr;
        break;
    }
    case MRandom: {
        bool tick;
        if (connected(m, 0)) tick = trigUp(in(m, 0), m.lastTrig) > 0;
        else { m.ph += P(0) / sr; tick = m.ph >= 1; if (tick) m.ph -= std::floor(m.ph); }
        if (tick) { m.held = (m.rnd.nextFloat() * 2 - 1) * P(1); m.stage = m.rnd.nextBool() ? 1 : 0; m.slew = 0; }
        m.slew += isr;
        float t = 0.0005f + P(2) / 100.0f * 0.5f; m.env += (m.held - m.env) * (1.0f - std::exp(-isr / t));
        m.out[0] = m.held; m.out[1] = m.env; m.out[2] = (m.stage && m.slew < 0.01f) ? 10.0f : 0.0f;
        break;
    }
    case MChaos: {
        // a Lorenz attractor, slowly
        double sigma = 10, rho = 20 + 18 * P(1) / 100.0, beta = 8.0 / 3.0;
        double dt = P(0) * std::pow(2.0, clampf(in(m, 0), -4, 4)) * 0.002 * (48000.0 / sr);
        if (m.d[0] == 0 && m.d[1] == 0 && m.d[2] == 0) { m.d[0] = 0.1; m.d[1] = 0; m.d[2] = 0; }
        double x = m.d[0], y = m.d[1], z = m.d[2];
        m.d[0] = x + sigma * (y - x) * dt; m.d[1] = y + (x * (rho - z) - y) * dt; m.d[2] = z + (x * y - beta * z) * dt;
        for (int k = 0; k < 3; ++k) if (!std::isfinite(m.d[k]) || std::abs(m.d[k]) > 1e3) { m.d[0] = 0.1; m.d[1] = m.d[2] = 0; }
        m.out[0] = (float) clampf((float) m.d[0] / 4.0f, -5, 5); m.out[1] = (float) clampf((float) m.d[1] / 5.0f, -5, 5); m.out[2] = (float) clampf((float) (m.d[2] - 25) / 5.0f, -5, 5);
        break;
    }
    case MFollow: {
        float a = std::abs(in(m, 0)) * 0.2f * P(2) / 100.0f;
        float at = 1.0f - std::exp(-1.0f / (P(0) * 0.001f * fs)), rl = 1.0f - std::exp(-1.0f / (P(1) * 0.001f * fs));
        m.env += (a > m.env ? at : rl) * (a - m.env);
        m.out[0] = clampf(m.env * 10, 0, 10); m.out[1] = m.env > 0.1f ? 10.0f : 0.0f;
        break;
    }
    case MDivider: {
        if (trigUp(in(m, 1), m.lastGate) > 0) m.step = 0;
        bool c = in(m, 0) > 1.0f;
        if (c && !m.lastTrig) ++m.step;
        m.lastTrig = c;
        const int D[4] = { 2, 3, 4, 8 };
        for (int k = 0; k < 4; ++k) m.out[k] = (c && (m.step - 1) % D[k] == 0) ? 10.0f : 0.0f;   // every D-th clock passes
        break;
    }
    case MEuclid: {
        int steps = juce::jlimit(1, 32, (int) std::round(P(0))), hits = juce::jlimit(0, steps, (int) std::round(P(1))), rot = (int) std::round(P(2));
        bool fire = false, on;
        if (connected(m, 0)) { if (trigUp(in(m, 1), m.lastGate) > 0) m.step = 0; bool c = in(m, 0) > 1.0f; if (c && !m.lastTrig) { m.step = (m.step + 1) % steps; fire = true; } m.lastTrig = c; on = c; }
        else { double len = divBeats((int) std::round(P(3))), pos = bt / len; int idx = (int) (((long) std::floor(pos) % steps + steps) % steps); fire = idx != m.step; m.step = idx; on = pos - std::floor(pos) < 0.5; }
        int k = ((m.step + rot) % steps + steps) % steps;
        bool hit = hits > 0 && ((k * hits) % steps) < hits;
        (void) fire;
        m.out[0] = hit && on ? 10.0f : 0.0f; m.out[1] = hit && on && k == 0 ? 10.0f : 0.0f;
        break;
    }
    case MBernoulli: {
        bool t = in(m, 0) > 1.0f;
        if (t && !m.lastTrig) m.stage = m.rnd.nextFloat() * 100.0f < P(0) ? 1 : 2;
        m.lastTrig = t;
        m.out[0] = t && m.stage == 1 ? 10.0f : 0.0f; m.out[1] = t && m.stage == 2 ? 10.0f : 0.0f;
        break;
    }
    case MArp: {
        int nh = arpCount;
        if (nh == 0) { m.out[1] = 0; break; }
        int oct = juce::jlimit(1, 4, (int) std::round(P(1))), mode = juce::jlimit(0, 3, (int) std::round(P(0)));
        int total = nh * oct;
        bool adv; float frac;
        if (connected(m, 0)) { bool c = in(m, 0) > 1.0f; adv = c && !m.lastTrig; m.lastTrig = c; frac = c ? 0.0f : 1.0f; }
        else { double len = divBeats((int) std::round(P(2))), pos = bt / len; long idx = (long) std::floor(pos); adv = idx != (long) m.d[0]; m.d[0] = (double) idx; frac = (float) (pos - idx); }
        if (adv) {
            if (mode == 0) m.step = (m.step + 1) % total;
            else if (mode == 1) m.step = (m.step - 1 + total) % total;
            else if (mode == 2) { int span = juce::jmax(1, total * 2 - 2); m.stage = (m.stage + 1) % span; m.step = m.stage < total ? m.stage : span - m.stage; }
            else m.step = m.rnd.nextInt(total);
        }
        int s = juce::jlimit(0, total - 1, m.step);
        int note = arpNotes[s % nh] + 12 * (s / nh);
        m.out[0] = (note - 60) / 12.0f; m.out[1] = frac < P(3) / 100.0f ? 10.0f : 0.0f;
        break;
    }
    case MCounter: {
        int len = juce::jlimit(1, 32, (int) std::round(P(0)));
        if (trigUp(in(m, 1), m.lastGate) > 0) m.step = 0;
        bool c = in(m, 0) > 1.0f; bool up = c && !m.lastTrig; m.lastTrig = c;
        if (up) m.step = (m.step + 1) % len;
        m.out[0] = (float) m.step / juce::jmax(1, len - 1) * 10.0f; m.out[1] = c && m.step == 0 ? 10.0f : 0.0f;
        break;
    }
    case MChord: {
        static const int CH[8][4] = { { 0, 4, 7, 12 }, { 0, 3, 7, 12 }, { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 2, 7, 12 }, { 0, 5, 7, 12 }, { 0, 3, 6, 9 } };
        int t = juce::jlimit(0, 7, (int) std::round(P(0))), inv = juce::jlimit(0, 3, (int) std::round(P(1)));
        for (int k = 0; k < 4; ++k) { int iv = CH[t][k] + (k < inv ? 12 : 0); m.out[k] = in(m, 0) + iv / 12.0f; }
        break;
    }
    case MTranspose: m.out[0] = in(m, 0) + std::round(P(0)) / 12.0f + std::round(P(1)); break;
    case MRing: m.out[0] = in(m, 0) * in(m, 1) / 5.0f; m.out[1] = in(m, 0) + in(m, 1); break;
    case MInvert: m.out[0] = -in(m, 0); m.out[1] = 10.0f - in(m, 0); break;
    case MXfade: { float mx = clampf(P(0) / 100.0f + in(m, 2) * 0.1f, 0, 1); m.out[0] = in(m, 0) * (1 - mx) + in(m, 1) * mx; break; }
    case MSwitch: m.out[0] = in(m, 2) > 1.0f ? in(m, 1) : in(m, 0); break;
    case MCompare: { bool hi = connected(m, 1) ? in(m, 0) > in(m, 1) : in(m, 0) > P(0); m.out[0] = hi ? 10.0f : 0.0f; m.out[1] = hi ? 0.0f : 10.0f; break; }
    case MMinMax: m.out[0] = juce::jmin(in(m, 0), in(m, 1)); m.out[1] = juce::jmax(in(m, 0), in(m, 1)); break;
    case MConst: m.out[0] = P(0); m.out[1] = P(1); m.out[2] = P(2); break;
    case MLogic: { bool a = in(m, 0) > 1.0f, b = in(m, 1) > 1.0f; m.out[0] = a && b ? 10.0f : 0.0f; m.out[1] = a || b ? 10.0f : 0.0f; m.out[2] = a != b ? 10.0f : 0.0f; m.out[3] = a ? 0.0f : 10.0f; break; }
    case MIn: {
        int src = juce::jlimit(0, 5, (int) std::round(P(0))); float g = P(1) / 100.0f * 5.0f;
        const float* l = src == 0 ? thruL : src <= 4 ? live[src - 1] : mixL;
        const float* r = src == 0 ? (thruR ? thruR : thruL) : src <= 4 ? live[src - 1] : (mixR ? mixR : mixL);
        int j = blockI + i;
        m.out[0] = l && j < blockN ? l[j] * g : 0.0f; m.out[1] = r && j < blockN ? r[j] * g : 0.0f;
        break;
    }
    case MScope: {
        m.out[0] = in(m, 0);
        if (m.scope >= 0 && m.scope < 16) {
            auto& sc = scopes[m.scope];
            int every = juce::jmax(1, (int) (P(0) * 0.001 * sr / 256.0 * 4.0));
            if (++m.step >= every) { m.step = 0; int w = sc.w.load(); sc.a[w] = in(m, 0); sc.b[w] = in(m, 1); sc.w.store((w + 1) & 255); }
        }
        break;
    }
    case MMix8: { float s2 = 0; for (int k = 0; k < 8; ++k) s2 += in(m, k) * P(k) / 100.0f; m.out[0] = s2; break; }
    // ================================================================ 2.4: to watch (and that play by themselves)
    case MXy: {
        m.out[0] = in(m, 0);
        if (m.scope >= 0 && ++m.step >= juce::jmax(1, (int) (1 + P(1) * P(1) / 25.0))) { m.step = 0; auto& sc = scopes[m.scope]; int w = sc.w.load(); sc.a[w] = in(m, 0); sc.b[w] = in(m, 1); sc.w.store((w + 1) & 255); }
        break;
    }
    case MSpectrum: {
        m.out[0] = in(m, 0);
        if (m.scope >= 0 && ++m.step >= 2) { m.step = 0; auto& sc = scopes[m.scope]; int w = sc.w.load(); sc.a[w] = in(m, 0) * P(0) / 100.0f; sc.w.store((w + 1) & 255); }
        break;
    }
    case MLife: {
        if (m.buf.size() < 256) break;
        float* g = m.buf.data();
        auto seed = [&] { for (int k = 0; k < 128; ++k) g[k] = m.rnd.nextFloat() * 100.0f < P(1) ? 1.0f : 0.0f; };
        if (m.pos[5] == 0 || trigUp(in(m, 1), m.lastGate) > 0) { seed(); m.pos[5] = 1; }
        bool tick;
        if (connected(m, 0)) tick = trigUp(in(m, 0), m.lastTrig) > 0;
        else { double len = divBeats((int) std::round(P(0))); long k = (long) std::floor(bt / len); tick = k != (long) m.clk; m.clk = (double) k; }
        if (tick) {
            m.step = (m.step + 1) % 16;
            if (m.step == 0) {          // the next generation
                int alive = 0, changed = 0;
                for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) {
                    int nb = 0; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) if (dx || dy) nb += g[((y + dy + 8) % 8) * 16 + (x + dx + 16) % 16] > 0.5f;
                    bool on = g[y * 16 + x] > 0.5f; bool nx = on ? (nb == 2 || nb == 3) : nb == 3;
                    g[128 + y * 16 + x] = nx ? 1.0f : 0.0f; alive += nx; changed += nx != on; }
                for (int k = 0; k < 128; ++k) g[k] = g[128 + k];
                if (alive == 0 || changed == 0) seed();
                m.pos[2] = (int) (sr * 0.01);
            }
            int low = -1, cnt = 0; for (int y = 0; y < 8; ++y) if (g[y * 16 + m.step] > 0.5f) { ++cnt; low = y; }
            if (cnt > 0) {
                m.pos[0] = (int) (sr * 0.06);
                int mask = scaleMasks[juce::jlimit(0, 5, (int) std::round(P(2)))]; int notes[12], nn = 0; for (int k = 0; k < 12; ++k) if (mask & (1 << k)) notes[nn++] = k;
                int deg = (7 - low) * juce::jmax(1, (int) std::round(P(3))) * nn / 8; m.held = (float) ((deg / nn) * 12 + notes[deg % nn]) / 12.0f;
            }
            m.slew = cnt / 8.0f * 10.0f;
        }
        m.out[0] = m.pos[0] > 0 ? 10.0f : 0.0f; if (m.pos[0] > 0) --m.pos[0];
        m.out[1] = m.held; m.out[2] = m.slew;
        m.out[3] = m.pos[2] > 0 ? 10.0f : 0.0f; if (m.pos[2] > 0) --m.pos[2];
        if (m.scope >= 0 && (++m.pos[6] & 255) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 128; ++k) sc.a[k] = g[k]; sc.b[0] = (float) m.step; sc.w.store(1); }
        break;
    }
    case MBounce: {
        int nb = juce::jlimit(1, 4, (int) std::round(P(2)));
        if (m.pos[8] == 0) { for (int k = 0; k < 4; ++k) { m.d[k] = 0.45 + 0.17 * k; m.z[k] = 0; } m.pos[8] = 1; }
        bool kick = trigUp(in(m, 0), m.lastTrig) > 0;
        double gr = 4.0 + P(0) / 100.0 * 36.0, dt = 1.0 / sr;
        for (int k = 0; k < 4; ++k) {
            if (kick && k < nb) m.z[k] = (float) (1.5 + 0.4 * k + m.rnd.nextFloat());
            if (k >= nb) { m.out[k] = 0; continue; }
            m.z[k] -= (float) (gr * dt); m.d[k] += m.z[k] * dt;
            if (m.d[k] <= 0 && m.z[k] < 0) {
                m.d[k] = 0; m.z[k] = -m.z[k] * P(1) / 100.0f; m.pos[k] = (int) (sr * 0.008);
                if (m.z[k] < 0.25f) m.z[k] = (float) (1.6 + 0.5 * m.rnd.nextFloat() + 0.25 * k);     // it stops: thrown up again
            }
            m.out[k] = m.pos[k] > 0 ? 10.0f : 0.0f; if (m.pos[k] > 0) --m.pos[k];
        }
        m.out[4] = (float) juce::jlimit(0.0, 10.0, m.d[0] * 6.0);
        if (m.scope >= 0 && (++m.step & 127) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 4; ++k) { sc.a[k] = k < nb ? (float) m.d[k] : -1.0f; sc.b[k] = m.pos[k] > 0 ? 1.0f : sc.b[k] * 0.85f; } sc.a[4] = (float) nb; sc.w.store(1); }
        break;
    }
    case MOrbit: {
        double rate = P(0) * std::pow(2.0, connected(m, 0) ? in(m, 0) : 0.0), ratio = P(1);
        double w[3] = { 1.0, ratio, ratio * ratio };
        for (int k = 0; k < 3; ++k) {
            m.d[k] += juce::MathConstants<double>::twoPi * rate / w[k] / sr;
            if (m.d[k] >= juce::MathConstants<double>::twoPi) { m.d[k] -= juce::MathConstants<double>::twoPi; m.pos[k] = (int) (sr * 0.008); }
            m.out[2 + k] = m.pos[k] > 0 ? 10.0f : 0.0f; if (m.pos[k] > 0) --m.pos[k];
        }
        m.out[0] = (float) (std::sin(m.d[0]) * P(2)); m.out[1] = (float) (-std::cos(m.d[0]) * P(2));
        if (m.scope >= 0 && (++m.step & 127) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 3; ++k) { sc.a[k] = (float) m.d[k]; sc.b[k] = m.pos[k] > 0 ? 1.0f : sc.b[k] * 0.85f; } sc.w.store(1); }
        break;
    }
    case MPendulum: {
        if (m.pos[8] == 0) { m.d[0] = 2.2; m.d[1] = 2.6; m.d[2] = 0; m.d[3] = 0; m.pos[8] = 1; }
        if (trigUp(in(m, 0), m.lastTrig) > 0) { m.d[2] += 4.0; m.d[3] -= 3.0; }
        if (++m.pos[7] >= 16) {
            m.pos[7] = 0;
            double l1 = 1.0, l2 = P(1) / 100.0, g = 9.81, dt = 16.0 / sr * P(0);
            for (int it = 0; it < 4; ++it) {
                double t1 = m.d[0], t2 = m.d[1], w1 = m.d[2], w2 = m.d[3], dd = t1 - t2, den = 3 - std::cos(2 * dd);
                double a1 = (-g * 3 * std::sin(t1) - g * std::sin(t1 - 2 * t2) - 2 * std::sin(dd) * (w2 * w2 * l2 + w1 * w1 * l1 * std::cos(dd))) / (l1 * den);
                double a2 = (2 * std::sin(dd) * (w1 * w1 * l1 * 2 + g * 2 * std::cos(t1) + w2 * w2 * l2 * std::cos(dd))) / (l2 * den);
                double h = dt / 4; m.d[2] = juce::jlimit(-40.0, 40.0, (w1 + a1 * h) * 0.9999995); m.d[3] = juce::jlimit(-40.0, 40.0, (w2 + a2 * h) * 0.9999995);
                m.d[0] += m.d[2] * h; m.d[1] += m.d[3] * h;
            }
            double l1b = 1.0, l2b = P(1) / 100.0;
            m.z[0] = (float) ((std::sin(m.d[0]) * l1b + std::sin(m.d[1]) * l2b) / (l1b + l2b)); m.z[1] = (float) ((-std::cos(m.d[0]) * l1b - std::cos(m.d[1]) * l2b) / (l1b + l2b));
            m.z[2] = (float) (std::sin(m.d[0]) / (1 + l2b) ); m.z[3] = (float) (-std::cos(m.d[0]) / (1 + l2b));
            if (m.scope >= 0 && (++m.step & 15) == 0) { auto& sc = scopes[m.scope]; int w = sc.w.load(); sc.a[w] = m.z[0]; sc.b[w] = m.z[1]; sc.w.store((w + 1) & 255); sc.a[(w + 128) & 255] = sc.a[(w + 128) & 255]; }
        }
        m.out[0] = m.z[0] * P(2); m.out[1] = m.z[1] * P(2); m.out[2] = (float) juce::jlimit(0.0, 10.0, std::abs(m.d[3]) * 0.5);
        break;
    }
    case MTuring: {
        int len = juce::jlimit(2, 16, (int) std::round(P(1)));
        bool tick;
        if (connected(m, 0)) tick = trigUp(in(m, 0), m.lastTrig) > 0;
        else { double l = divBeats((int) std::round(P(3))); long k = (long) std::floor(bt / l); tick = k != (long) m.clk; m.clk = (double) k; }
        if (m.pos[8] == 0) { m.pos[0] = m.rnd.nextInt(65536); m.pos[8] = 1; }
        if (tick) {
            unsigned reg = (unsigned) m.pos[0]; unsigned bit = (reg >> (len - 1)) & 1u;
            if (m.rnd.nextFloat() * 100.0f < P(0)) bit ^= 1u;
            reg = ((reg << 1) | bit) & 0xFFFFu; m.pos[0] = (int) reg;
            m.held = (float) (reg & 0xFFu) / 255.0f * juce::jmax(0.0f, P(2));
            if (bit) m.pos[1] = (int) (sr * 0.05);
            m.slew = bit ? 10.0f : 0.0f;
        }
        m.out[0] = m.held; m.out[1] = m.pos[1] > 0 ? 10.0f : 0.0f; if (m.pos[1] > 0) --m.pos[1]; m.out[2] = m.slew;
        if (m.scope >= 0 && (++m.step & 255) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 16; ++k) sc.a[k] = (float) ((m.pos[0] >> k) & 1); sc.b[0] = (float) len; sc.w.store(1); }
        break;
    }
    case MCellular: {
        if (m.buf.size() < 256) break;
        bool tick;
        if (connected(m, 0)) tick = trigUp(in(m, 0), m.lastTrig) > 0;
        else { double l = divBeats((int) std::round(P(1))); long k = (long) std::floor(bt / l); tick = k != (long) m.clk; m.clk = (double) k; }
        if (m.pos[8] == 0 || m.pos[0] == 0) { m.pos[0] = 1 << 8; m.pos[8] = 1; }
        if (tick) {
            int rule = juce::jlimit(0, 255, (int) std::round(P(0))); unsigned c = (unsigned) m.pos[0], nx = 0;
            for (int k = 0; k < 16; ++k) { int l = (c >> ((k + 1) & 15)) & 1, me = (c >> k) & 1, r = (c >> ((k + 15) & 15)) & 1; if ((rule >> (l * 4 + me * 2 + r)) & 1) nx |= 1u << k; }
            m.pos[0] = (int) nx;
            for (int y = 15; y > 0; --y) for (int x = 0; x < 16; ++x) m.buf[(size_t) (y * 16 + x)] = m.buf[(size_t) ((y - 1) * 16 + x)];
            int cnt = 0; for (int x = 0; x < 16; ++x) { m.buf[(size_t) x] = (float) ((nx >> x) & 1); cnt += (nx >> x) & 1; }
            const int cells[4] = { 2, 6, 10, 14 }; for (int k = 0; k < 4; ++k) if ((nx >> cells[k]) & 1) m.pos[1 + k] = (int) (sr * 0.03);
            m.slew = cnt / 16.0f * 10.0f;
        }
        for (int k = 0; k < 4; ++k) { m.out[k] = m.pos[1 + k] > 0 ? 10.0f : 0.0f; if (m.pos[1 + k] > 0) --m.pos[1 + k]; }
        m.out[4] = m.slew;
        if (m.scope >= 0 && (++m.step & 255) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 256; ++k) sc.a[k] = m.buf[(size_t) k]; sc.w.store(1); }
        break;
    }
    // ================================================================ 2.4: more sound
    case MGrains: {
        int ring = (int) m.buf.size() - 16 * 6; if (ring < 1024) break;
        float* G = m.buf.data() + ring;          // per grain: start, age, len, rate, pan, on
        bool freeze = connected(m, 1) && in(m, 1) > 1.0f;
        if (!freeze) { m.buf[(size_t) m.pos[0]] = in(m, 0) / 5.0f; m.pos[0] = (m.pos[0] + 1) % ring; }
        m.ph += P(1) / sr;
        if (m.ph >= 1) { m.ph -= 1;
            for (int k = 0; k < 16; ++k) if (G[k * 6 + 5] < 0.5f) {
                float len = P(0) * fs / 1000.0f, rate = std::pow(2.0f, P(2) / 12.0f);
                float back = len * juce::jmax(1.0f, rate) + m.rnd.nextFloat() * P(3) / 100.0f * fs * 1.2f + 64;
                float st = (float) m.pos[0] - back; while (st < 0) st += (float) ring;
                G[k * 6] = st; G[k * 6 + 1] = 0; G[k * 6 + 2] = len; G[k * 6 + 3] = rate; G[k * 6 + 4] = m.rnd.nextFloat() * 2 - 1; G[k * 6 + 5] = 1; break; }
        }
        float l = 0, r = 0;
        for (int k = 0; k < 16; ++k) { float* g = G + k * 6; if (g[5] < 0.5f) continue;
            float t = g[1] / g[2]; if (t >= 1) { g[5] = 0; continue; }
            float env = 0.5f - 0.5f * std::cos(t * 6.2831853f);
            float pos = g[0] + g[1] * g[3]; while (pos >= ring) pos -= (float) ring; int i0 = (int) pos; float fr = pos - (float) i0;
            float v = (m.buf[(size_t) i0] * (1 - fr) + m.buf[(size_t) ((i0 + 1) % ring)] * fr) * env;
            l += v * (1 - g[4]) * 0.5f; r += v * (1 + g[4]) * 0.5f; g[1] += 1; }
        float norm = 1.0f / std::sqrt(juce::jmax(1.0f, P(1) * P(0) / 1000.0f));
        m.out[0] = l * 5 * norm * 1.5f; m.out[1] = r * 5 * norm * 1.5f;
        if (m.scope >= 0 && (++m.step & 255) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 16; ++k) { float* g = G + k * 6; sc.a[k] = g[5] > 0.5f ? std::fmod((float) m.pos[0] - g[0] + (float) ring, (float) ring) / (float) ring : -1.0f; sc.b[k] = g[5] > 0.5f ? 0.5f - 0.5f * std::cos(g[1] / g[2] * 6.2831853f) : 0.0f; } sc.w.store(1); }
        break;
    }
    case MFreqShift: {
        static const float A1[4] = { 0.6923878f, 0.9360654f, 0.9882295f, 0.9987488f }, A2[4] = { 0.4021921f, 0.8561711f, 0.9722910f, 0.9952885f };
        float x = in(m, 0), re = x, im = x;
        for (int k = 0; k < 4; ++k) { float a = A1[k] * A1[k]; float y = a * (re - m.z[k * 2 + 1]) + m.z[k * 2]; m.z[k * 2] = re; m.z[k * 2 + 1] = y; re = y;
            float b = A2[k] * A2[k]; float y2 = b * (im - m.z[8 + k * 2 + 1]) + m.z[8 + k * 2]; m.z[8 + k * 2] = im; m.z[8 + k * 2 + 1] = y2; im = y2; }
        float imD = (float) m.d[0]; m.d[0] = im;
        double shift = P(0) + (connected(m, 1) ? in(m, 1) * P(1) / 100.0 * 50.0 : 0.0);
        m.ph += shift / sr; m.ph -= std::floor(m.ph);
        float cs = (float) std::cos(m.ph * juce::MathConstants<double>::twoPi), sn = (float) std::sin(m.ph * juce::MathConstants<double>::twoPi);
        m.out[0] = re * cs - imD * sn; m.out[1] = re * cs + imD * sn;
        break;
    }
    case MTape: {
        int N = (int) m.buf.size(); if (N < 16) break;
        m.buf[(size_t) m.pos[0]] = in(m, 0); m.pos[0] = (m.pos[0] + 1) % N;
        m.ph += 0.45 / sr; m.ph -= std::floor(m.ph); m.ph2 += 6.5 / sr; m.ph2 -= std::floor(m.ph2);
        double dly = (0.012 + P(0) / 100.0 * 0.004 * std::sin(m.ph * juce::MathConstants<double>::twoPi) + P(1) / 100.0 * 0.0004 * std::sin(m.ph2 * juce::MathConstants<double>::twoPi)) * sr;
        double rp = m.pos[0] - dly; while (rp < 0) rp += N; int i0 = (int) rp; float fr = (float) (rp - i0);
        float v = m.buf[(size_t) i0] * (1 - fr) + m.buf[(size_t) ((i0 + 1) % N)] * fr;
        float drive = 1.0f + P(2) / 25.0f; v = std::tanh(v / 5.0f * drive) / std::tanh(drive) * 5.0f;
        float k = 0.05f + P(3) / 100.0f * 0.9f; m.z[0] += k * (v - m.z[0]);
        m.out[0] = m.z[0];
        break;
    }
    case MLooper: {
        int N = (int) m.buf.size(); if (N < 16) break;
        bool rec = in(m, 1) > 1.0f;
        if (trigUp(in(m, 2), m.lastTrig) > 0) { m.pos[0] = 0; m.pos[1] = 0; m.stage = 0; }
        int& len = m.pos[0]; int& idx = m.pos[1];
        if (rec && !m.lastGate && len == 0) { m.stage = 1; idx = 0; }
        if (!rec && m.lastGate && m.stage == 1) { len = juce::jmax(64, idx); idx = 0; m.stage = 2; }
        m.lastGate = rec;
        float x = in(m, 0), out = 0;
        if (m.stage == 1) { if (idx < N) m.buf[(size_t) idx++] = x; else { len = N; idx = 0; m.stage = 2; } }
        else if (len > 0) { out = m.buf[(size_t) idx]; if (rec) m.buf[(size_t) idx] = out * P(1) / 100.0f + x; idx = (idx + 1) % len; }
        m.out[0] = out * P(0) / 100.0f;
        break;
    }
    case MPingPong: {
        int half = (int) m.buf.size() / 2; if (half < 16) break;
        double sec = divBeats((int) std::round(P(0))) * 60.0 / bpmNow; if (connected(m, 1)) sec *= std::pow(2.0, in(m, 1) / 5.0);
        double d = juce::jlimit(1.0, (double) half - 2, sec * sr);
        auto rd = [&](int base) { double rp = m.pos[0] - d; while (rp < 0) rp += half; int i0 = (int) rp; float fr = (float) (rp - i0); return m.buf[(size_t) (base + i0)] * (1 - fr) + m.buf[(size_t) (base + (i0 + 1) % half)] * fr; };
        float lo = rd(0), ro = rd(half), fb = P(1) / 100.0f, x = in(m, 0);
        m.buf[(size_t) m.pos[0]] = x + ro * fb; m.buf[(size_t) (half + m.pos[0])] = lo * fb;
        m.pos[0] = (m.pos[0] + 1) % half;
        float mix = P(2) / 100.0f; m.out[0] = x * (1 - mix) + lo * mix; m.out[1] = x * (1 - mix) + ro * mix;
        break;
    }
    case MQuadLfo: {
        double rate = P(0) * std::pow(2.0, connected(m, 0) ? in(m, 0) : 0.0);
        for (int k = 0; k < 4; ++k) { m.d[k] += rate / std::pow((double) P(1), k) / sr; m.d[k] -= std::floor(m.d[k]);
            double ph = m.d[k]; m.out[k] = (float) (P(2) < 0.5f ? std::sin(ph * juce::MathConstants<double>::twoPi) * 5.0 : (ph < 0.5 ? -5 + 20 * ph : 15 - 20 * ph)); }
        break;
    }
    case MFunction: {
        if (trigUp(in(m, 0), m.lastTrig) > 0) m.stage = 1;
        if (m.stage == 0 && P(3) > 0.5f) m.stage = 1;
        float curve = std::pow(2.0f, P(2) / 50.0f);
        if (m.stage == 1) { m.ph += 1.0 / (juce::jmax(0.001f, P(0)) * sr); if (m.ph >= 1) { m.ph = 1; m.stage = 2; } }
        else if (m.stage == 2) { m.ph -= 1.0 / (juce::jmax(0.001f, P(1)) * sr); if (m.ph <= 0) { m.ph = 0; m.stage = P(3) > 0.5f ? 1 : 0; m.pos[0] = (int) (sr * 0.008); } }
        m.out[0] = (float) std::pow(m.ph, (double) curve) * 10.0f;
        m.out[1] = m.pos[0] > 0 ? 10.0f : 0.0f; if (m.pos[0] > 0) --m.pos[0];
        break;
    }
    case MBurst: {
        if (trigUp(in(m, 0), m.lastTrig) > 0) { m.pos[0] = juce::jlimit(1, 16, (int) std::round(P(0))); m.ph = 0; }
        float out = 0;
        if (m.pos[0] > 0) { out = m.ph < 0.5 ? 10.0f : 0.0f; m.ph += P(1) / sr; if (m.ph >= 1) { m.ph -= 1; --m.pos[0]; } }
        m.out[0] = out;
        break;
    }
    case MResonator: {
        double f0 = hzOf(std::round(P(0)) + in(m, 1)) * 0.5;
        static const float harm[4] = { 1, 2, 3, 4 }, bell[4] = { 1, 2.756f, 5.404f, 8.933f };
        float sh = P(1) / 100.0f, q = 5.0f + P(2) / 100.0f * 300.0f, x = in(m, 0) / 5.0f, sum = 0;
        for (int k = 0; k < 4; ++k) {
            double f = juce::jmin(sr * 0.45, f0 * (harm[k] * (1 - sh) + bell[k] * sh));
            double g = std::tan(juce::MathConstants<double>::pi * f / sr), kk = 1.0 / q, a1 = 1 / (1 + g * (g + kk)), a2 = g * a1, a3 = g * a2;
            float& ic1 = m.z[k * 2]; float& ic2 = m.z[k * 2 + 1];
            double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = (float) (2 * v1 - ic1); ic2 = (float) (2 * v2 - ic2);
            if (std::abs(ic1) < 1e-20f) ic1 = 0; if (std::abs(ic2) < 1e-20f) ic2 = 0;
            float amp = std::pow(0.4f + P(3) / 100.0f * 0.6f, (float) k);
            sum += (float) v1 * amp;
        }
        m.out[0] = clampf(sum * 5.0f * 1.5f, -12, 12);
        break;
    }
    case MHarmonics: {
        double f = hzOf(std::round(P(0)) + in(m, 0));
        float tilt = (P(1) + (connected(m, 1) ? in(m, 1) * 10 : 0)) / 100.0f * 1.5f, oe = P(2) / 100.0f; int np = juce::jlimit(1, 8, (int) std::round(P(3)));
        float amps[8], tot = 0;
        for (int k = 0; k < 8; ++k) { int n = k + 1; float a = k < np ? std::pow((float) n, tilt) : 0.0f; if (n % 2 == 1 && n > 1) a *= 1 - juce::jmax(0.0f, oe); if (n % 2 == 0) a *= 1 - juce::jmax(0.0f, -oe);
            if (f * n > sr * 0.45) a = 0; amps[k] = a; tot += a; }
        float s2 = 0;
        for (int k = 0; k < 8; ++k) { m.d[k] += f * (k + 1) / sr; m.d[k] -= std::floor(m.d[k]); if (amps[k] > 0) s2 += amps[k] * (float) std::sin(m.d[k] * juce::MathConstants<double>::twoPi); }
        m.out[0] = tot > 0 ? s2 / tot * 5.0f : 0.0f;
        if (m.scope >= 0 && (++m.step & 511) == 0) { auto& sc = scopes[m.scope]; for (int k = 0; k < 8; ++k) sc.a[k] = tot > 0 ? amps[k] / juce::jmax(0.0001f, *std::max_element(amps, amps + 8)) : 0; sc.w.store(1); }
        break;
    }
    default: break;
    }
    (void) outL; (void) outR;
}

// ------------------------------------------------------------------ save / open
juce::var Modular::toVar(const ModPatch& p) {
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> ms;
    for (auto& m : p.mods) {
        auto* mo = new juce::DynamicObject(); mo->setProperty("type", modDefs()[(size_t) m.type].name); mo->setProperty("id", m.id);
        if (m.x >= 0) { mo->setProperty("x", m.x); mo->setProperty("y", m.y); }
        juce::Array<juce::var> ps; for (size_t k = 0; k < modDefs()[(size_t) m.type].params.size(); ++k) ps.add(m.p[k]);
        mo->setProperty("p", ps); ms.add(juce::var(mo));
    }
    juce::Array<juce::var> cs;
    for (auto& c : p.cables) { juce::Array<juce::var> a { c.fromMod, c.fromPort, c.toMod, c.toPort, c.colour }; cs.add(a); }
    o->setProperty("mods", ms); o->setProperty("cables", cs); o->setProperty("next", p.nextId);
    return juce::var(o);
}

ModPatch Modular::fromVar(const juce::var& v) {
    ModPatch p;
    if (auto* ms = v["mods"].getArray())
        for (auto& mv : *ms) {
            juce::String tn = mv["type"].toString(); int type = -1;
            for (int t = 0; t < MTypes; ++t) if (tn == modDefs()[(size_t) t].name) type = t;
            if (type < 0) continue;
            ModModule m; m.type = type; m.id = (int) mv["id"]; m.x = (float) mv.getProperty("x", -1.0); m.y = (float) mv.getProperty("y", -1.0);
            auto& D = modDefs()[(size_t) type];
            for (size_t k = 0; k < D.params.size(); ++k) m.p[k] = D.params[k].def;
            if (auto* ps = mv["p"].getArray()) for (int k = 0; k < juce::jmin(ps->size(), (int) D.params.size()); ++k) m.p[(size_t) k] = juce::jlimit(D.params[(size_t) k].lo, D.params[(size_t) k].hi, (float) (double) (*ps)[k]);
            p.mods.push_back(m); p.nextId = juce::jmax(p.nextId, m.id + 1);
        }
    if (auto* cs = v["cables"].getArray())
        for (auto& c : *cs) if (c.isArray() && c.size() >= 4) p.cables.push_back({ (int) c[0], (int) c[1], (int) c[2], (int) c[3], c.size() > 4 ? (int) c[4] : 0 });
    p.nextId = juce::jmax(p.nextId, (int) v.getProperty("next", 1));
    return p;
}

juce::StringArray Modular::patchNames() { return { "Basic subtractive", "Generative sequence", "FM bell", "Noise drums", "Empty rack",
    "Supersaw pad", "Acid line", "Euclid drums", "Harp of chance", "Robot voice (an effect on LIVE 1)", "Ducking echo (an effect)", "Strange melodies", "Bells in tempo", "Your first sound",
    "Game of Life melodies", "Bouncing balls", "Orbits (drums from planets)", "Pendulum drone", "Turing machine", "Grain cloud (an effect on LIVE 1)", "Cellular rhythm" }; }

ModPatch Modular::defaultPatch(int which) {
    ModPatch p;
    auto add = [&p](int type, std::initializer_list<std::pair<int, float>> set = {}) {
        ModModule m; m.type = type; m.id = p.nextId++;
        auto& D = modDefs()[(size_t) type]; for (size_t i = 0; i < D.params.size(); ++i) m.p[i] = D.params[i].def;
        for (auto& s : set) m.p[(size_t) s.first] = s.second;
        p.mods.push_back(m); return m.id;
    };
    int col = 0;
    auto cable = [&p, &col](int f, int fp, int t, int tp) { p.cables.push_back({ f, fp, t, tp, (col++) % 6 }); };
    switch (which) {
    case 0: {   // two detuned saws, ladder filter, envelope, a slow LFO on the pulse width
        int keys = add(MKeys, { { 1, 4 } });
        int v1 = add(MVco), v2 = add(MVco, { { 2, 9 } });
        int lfo = add(MLfo, { { 0, 0.4f } });
        int mix = add(MMix, { { 0, 60 }, { 1, 60 } });
        int env = add(MEnv, { { 0, 0.005f }, { 1, 0.5f }, { 2, 45 }, { 3, 0.35f } });
        int vcf = add(MVcf, { { 0, 600 }, { 1, 35 }, { 2, 55 } });
        int vca = add(MVca); int out = add(MOut);
        cable(keys, 0, v1, 0); cable(keys, 0, v2, 0); cable(lfo, 0, v2, 2);
        cable(v1, 2, mix, 0); cable(v2, 3, mix, 1); cable(mix, 0, vcf, 0);
        cable(keys, 1, env, 0); cable(env, 0, vcf, 1); cable(vcf, 0, vca, 0); cable(env, 0, vca, 1); cable(vca, 0, out, 0);
        break;
    }
    case 1: {   // the clock drives a sequence through a quantizer; random notes sometimes; filter breathes; echoes in tempo
        int clk = add(MClock, { { 0, 3 } });
        int seq = add(MSeq, { { 10, 3 }, { 9, 40 } });
        int sah = add(MSah); int att = add(MAtt, { { 0, 30 }, { 2, 100 } });
        int mix = add(MMix, { { 0, 100 }, { 1, 100 } });
        int q = add(MQuant, { { 0, 3 } });
        int vco = add(MVco, { { 0, -1 } });
        int lfo = add(MLfo, { { 1, 2 } });
        int env = add(MEnv, { { 0, 0.002f }, { 1, 0.25f }, { 2, 0 }, { 3, 0.2f } });
        int vcf = add(MVcf, { { 0, 500 }, { 1, 55 }, { 2, 70 } });
        int vca = add(MVca); int dl = add(MDelay, { { 0, 0.375f }, { 1, 55 }, { 2, 40 } }); int out = add(MOut);
        cable(clk, 0, sah, 1); cable(seq, 0, mix, 0); cable(sah, 0, att, 0); cable(att, 0, mix, 1); cable(mix, 0, q, 0); cable(q, 0, vco, 0);
        cable(seq, 1, env, 0); cable(vco, 2, vcf, 0); cable(env, 0, vcf, 1); cable(lfo, 1, vcf, 2);
        cable(vcf, 0, vca, 0); cable(env, 0, vca, 1); cable(vca, 0, dl, 0); cable(dl, 0, out, 0);
        break;
    }
    case 2: {   // FM: a modulator at 3.5x through a fast envelope into a sine carrier
        int keys = add(MKeys);
        int mod = add(MVco, { { 0, 1 }, { 1, 9.69f } });
        int envM = add(MEnv, { { 0, 0.001f }, { 1, 0.9f }, { 2, 0 }, { 3, 0.6f } });
        int vcaM = add(MVca, { { 0, 5 } });
        int car = add(MVco, { { 0, 1 }, { 3, 70 } });
        int env = add(MEnv, { { 0, 0.002f }, { 1, 3.5f }, { 2, 0 }, { 3, 2.5f } });
        int vca = add(MVca); int dl = add(MDelay, { { 0, 0.5f }, { 1, 35 }, { 2, 25 } }); int out = add(MOut);
        cable(keys, 0, mod, 0); cable(keys, 0, car, 0); cable(keys, 1, envM, 0); cable(keys, 1, env, 0);
        cable(mod, 0, vcaM, 0); cable(envM, 0, vcaM, 1); cable(vcaM, 0, car, 1);
        cable(car, 0, vca, 0); cable(env, 0, vca, 1); cable(vca, 0, dl, 0); cable(dl, 0, out, 0);
        break;
    }
    case 3: {   // a kick (sine with a falling pitch) and a hat (filtered noise)
        int clk = add(MClock, { { 0, 2 } });
        int envK = add(MEnv, { { 0, 0.001f }, { 1, 0.25f }, { 2, 0 }, { 3, 0.2f } });
        int envP = add(MEnv, { { 0, 0.001f }, { 1, 0.05f }, { 2, 0 }, { 3, 0.05f } });
        int att = add(MAtt, { { 0, 25 }, { 1, -2.6f } });
        int kick = add(MVco);
        int vcaK = add(MVca);
        int noise = add(MNoise);
        int envH = add(MEnv, { { 0, 0.001f }, { 1, 0.06f }, { 2, 0 }, { 3, 0.05f } });
        int vcf = add(MVcf, { { 0, 8000 }, { 1, 30 }, { 2, 0 } });
        int vcaH = add(MVca);
        int mix = add(MMix, { { 0, 100 }, { 1, 45 } }); int out = add(MOut);
        cable(clk, 0, envK, 0); cable(clk, 0, envP, 0); cable(envP, 0, att, 0); cable(att, 0, kick, 0);
        cable(kick, 0, vcaK, 0); cable(envK, 0, vcaK, 1);
        cable(clk, 1, envH, 0); cable(noise, 0, vcf, 0); cable(vcf, 2, vcaH, 0); cable(envH, 0, vcaH, 1);
        cable(vcaK, 0, mix, 0); cable(vcaH, 0, mix, 1); cable(mix, 0, out, 0);
        break;
    }
    case 5: {   // keys -> a wide supersaw -> a breathing filter -> chorus -> reverb
        int keys = add(MKeys, { { 1, 6 } }); int ss = add(MSupersaw, { { 1, 40 } });
        int ar = add(MAr, { { 0, 0.6f }, { 1, 1.8f } }); int lfo = add(MLfo, { { 0, 0.12f } });
        int f = add(MSvf, { { 0, 900 }, { 1, 30 }, { 3, 40 } }); int cho = add(MChorus, { { 2, 40 } }); int vca = add(MVca);
        int rv = add(MReverb, { { 0, 80 }, { 2, 45 } }); int out = add(MOut);
        cable(keys, 0, ss, 0); cable(keys, 1, ar, 0); cable(ss, 0, f, 0); cable(lfo, 0, f, 1); cable(f, 0, vca, 0); cable(ar, 0, vca, 1);
        cable(vca, 0, cho, 0); cable(cho, 0, rv, 0); cable(cho, 1, rv, 1); cable(rv, 0, out, 0); cable(rv, 1, out, 1);
        break;
    }
    case 6: {   // a 16th sequence, a squelchy filter opened by an envelope, drive, an echo in tempo
        int clk = add(MClock, { { 0, 4 } }); int seq = add(MSeq, { { 10, 4 }, { 9, 55 }, { 0, 0 }, { 1, 12 }, { 2, 0 }, { 3, 3 }, { 4, 0 }, { 5, 15 }, { 6, 0 }, { 7, 10 } });
        int vco = add(MVco, { { 0, -2 } }); int env = add(MEnv, { { 0, 0.002f }, { 1, 0.18f }, { 2, 10 }, { 3, 0.1f } });
        int f = add(MSvf, { { 0, 400 }, { 1, 82 }, { 3, 85 } }); int dr = add(MDrive, { { 0, 45 }, { 1, 3 } }); int vca = add(MVca);
        int dl = add(MDelay, { { 0, 0.375f }, { 1, 40 }, { 2, 25 } }); int out = add(MOut);
        (void) clk; cable(seq, 0, vco, 0); cable(seq, 1, env, 0); cable(vco, 2, f, 0); cable(env, 0, f, 1); cable(f, 0, dr, 0);
        cable(dr, 0, vca, 0); cable(env, 0, vca, 1); cable(vca, 0, dl, 0); cable(dl, 0, out, 0);
        break;
    }
    case 7: {   // three even rhythms: kick, snare, hat - placed in the stereo, a little room
        int clk = add(MClock, { { 0, 4 } });
        int e1 = add(MEuclid, { { 0, 16 }, { 1, 4 } }), e2 = add(MEuclid, { { 0, 16 }, { 1, 2 }, { 2, 4 } }), e3 = add(MEuclid, { { 0, 16 }, { 1, 11 } });
        int k = add(MKick), sn = add(MSnare), hh = add(MHat, { { 0, 0.04f } });
        int mx = add(MStMix, { { 0, 100 }, { 1, 70 }, { 2, 45 }, { 4, 0 }, { 5, 10 }, { 6, 35 } }); int rv = add(MReverb, { { 0, 35 }, { 2, 15 } }); int out = add(MOut);
        cable(clk, 0, e1, 0); cable(clk, 0, e2, 0); cable(clk, 0, e3, 0);
        cable(e1, 0, k, 0); cable(e2, 0, sn, 0); cable(e3, 0, hh, 0);
        cable(k, 0, mx, 0); cable(sn, 0, mx, 1); cable(hh, 0, mx, 2); cable(mx, 0, rv, 0); cable(mx, 1, rv, 1); cable(rv, 0, out, 0); cable(rv, 1, out, 1);
        break;
    }
    case 8: {   // hold some keys: an arpeggio plucks a string, sometimes skipping, into a big room
        int arp = add(MArp, { { 0, 2 }, { 1, 2 }, { 2, 4 } }); int ch = add(MBernoulli, { { 0, 80 } });
        int st = add(MString, { { 1, 80 }, { 2, 55 } }); int rv = add(MReverb, { { 0, 85 }, { 2, 40 } }); int out = add(MOut);
        cable(arp, 1, ch, 0); cable(ch, 0, st, 0); cable(arp, 0, st, 1); cable(st, 0, rv, 0); cable(rv, 0, out, 0); cable(rv, 1, out, 1);
        break;
    }
    case 9: {   // a voice (LIVE 1) says the vowels as an LFO moves - a robot
        int in = add(MIn, { { 0, 1 } }); int lfo = add(MLfo, { { 1, 4 } }); int vw = add(MFormant, { { 1, 70 } }); int out = add(MOut);
        cable(in, 0, vw, 0); cable(lfo, 1, vw, 1); cable(vw, 0, out, 0);
        break;
    }
    case 10: {  // an echo that waits: it ducks while the sound plays, blooms when it stops
        int in = add(MIn); int dl = add(MDelay, { { 0, 0.5f }, { 1, 60 }, { 2, 100 } }); int cp = add(MComp, { { 0, -30 }, { 1, 10 } });
        int mx = add(MMix, { { 0, 100 }, { 1, 90 } }); int out = add(MOut);
        cable(in, 0, dl, 0); cable(dl, 0, cp, 0); cable(in, 0, cp, 1); cable(in, 0, mx, 0); cable(cp, 0, mx, 1); cable(mx, 0, out, 0);
        break;
    }
    case 11: {  // a strange attractor through a scale: a melody that never repeats
        int ch = add(MChaos, { { 0, 0.3f } }); int at = add(MAtt, { { 0, 25 } }); int q = add(MQuant, { { 0, 3 } });
        int rnd = add(MRandom, { { 0, 3 } }); int ar = add(MAr, { { 0, 0.005f }, { 1, 0.4f } });
        int wv = add(MWave, { { 2, 1.5f } }); int f = add(MSvf, { { 0, 2500 }, { 1, 20 } }); int vca = add(MVca); int rv = add(MReverb, { { 2, 35 } }); int out = add(MOut);
        cable(ch, 0, at, 0); cable(at, 0, q, 0); cable(q, 0, wv, 0); cable(ch, 1, wv, 1); cable(rnd, 2, ar, 0);
        cable(wv, 0, f, 0); cable(f, 0, vca, 0); cable(ar, 0, vca, 1); cable(vca, 0, rv, 0); cable(rv, 0, out, 0); cable(rv, 1, out, 1);
        break;
    }
    case 12: {  // bells every beat, a chord of them every bar
        int clk = add(MClock, { { 0, 2 } }); int dv = add(MDivider); int ar = add(MAr, { { 0, 0.001f }, { 1, 1.6f } });
        int seq = add(MSeq, { { 10, 2 }, { 0, 0 }, { 1, 7 }, { 2, 12 }, { 3, 4 }, { 4, 9 }, { 5, 7 }, { 6, 16 }, { 7, 11 } });
        int fm = add(MFmOp, { { 0, 1 }, { 1, 3.5f }, { 2, 3 } }); int vca = add(MVca); int rv = add(MReverb, { { 0, 70 }, { 2, 40 } }); int out = add(MOut);
        cable(clk, 0, dv, 0); cable(clk, 0, ar, 0); cable(seq, 0, fm, 0); cable(ar, 0, fm, 1); cable(fm, 0, vca, 0); cable(ar, 0, vca, 1);
        cable(vca, 0, rv, 0); cable(rv, 0, out, 0); cable(rv, 1, out, 1);
        break;
    }
    case 13: {  // the smallest instrument: the keys play an oscillator, an envelope shapes it
        int keys = add(MKeys); int vco = add(MVco); int env = add(MEnv); int vca = add(MVca); int out = add(MOut);
        cable(keys, 0, vco, 0); cable(keys, 1, env, 0); cable(vco, 2, vca, 0); cable(env, 0, vca, 1); cable(vca, 0, out, 0);
        break;
    }
    case 14: {  // the Game of Life plays a synth: the playhead's column is the note, the population opens the filter
        int life = add(MLife); int vco = add(MVco, { { 0, -1 } }); int env = add(MEnv, { { 0, 0.005f }, { 1, 0.25f }, { 2, 20 }, { 3, 0.3f } });
        int vcf = add(MVcf, { { 0, 600 }, { 1, 35 } }); int vca = add(MVca); int rv = add(MReverb, { { 2, 40 } }); int out = add(MOut);
        cable(life, 1, vco, 0); cable(life, 0, env, 0); cable(life, 2, vcf, 1); cable(vco, 2, vcf, 0); cable(vcf, 0, vca, 0); cable(env, 0, vca, 1);
        cable(vca, 0, rv, 0); cable(rv, 0, out, 0); cable(rv, 1, out, 1); break;
    }
    case 15: {  // balls bounce on three strings
        int b = add(MBounce); int s1 = add(MString, { { 0, 0 } }); int s2 = add(MString, { { 0, 1 } }); int s3 = add(MString, { { 0, -1 } });
        int mx = add(MStMix); int rv = add(MReverb, { { 0, 75 }, { 2, 45 } }); int out = add(MOut);
        cable(b, 0, s1, 0); cable(b, 1, s2, 0); cable(b, 2, s3, 0); cable(s1, 0, mx, 0); cable(s2, 0, mx, 1); cable(s3, 0, mx, 2);
        cable(mx, 0, rv, 0); cable(mx, 1, rv, 1); cable(rv, 0, out, 0); cable(rv, 1, out, 1); break;
    }
    case 16: {  // three planets play a kick, a snare and a hat; the first one's place on an XY screen
        int o = add(MOrbit, { { 0, 0.5f }, { 1, 1.5f } }); int k = add(MKick); int sn = add(MSnare); int h = add(MHat); int mx = add(MStMix); int xy = add(MXy, { { 1, 60 } }); int out = add(MOut);
        cable(o, 2, k, 0); cable(o, 3, sn, 0); cable(o, 4, h, 0); cable(k, 0, mx, 0); cable(sn, 0, mx, 1); cable(h, 0, mx, 2); cable(o, 0, xy, 0); cable(o, 1, xy, 1);
        cable(mx, 0, out, 0); cable(mx, 1, out, 1); break;
    }
    case 17: {  // a double pendulum bends a drone
        int pd = add(MPendulum, { { 0, 0.6f }, { 2, 2 } }); int att = add(MAtt, { { 0, 15 }, { 1, -1 }, { 2, 100 } }); int vco = add(MVco, { { 0, -1 } }); int vco2 = add(MVco, { { 0, -1 }, { 2, 8 } });
        int vcf = add(MVcf, { { 0, 900 }, { 1, 45 }, { 2, 80 } }); int xy = add(MXy, { { 0, 200 }, { 1, 50 } }); int rv = add(MReverb, { { 0, 80 }, { 2, 40 } }); int out = add(MOut, { { 0, 55 } });
        cable(pd, 0, att, 0); cable(pd, 1, att, 1); cable(att, 0, vco, 0); cable(att, 0, vco2, 0); cable(att, 1, vcf, 1); cable(vco, 2, vcf, 0); cable(vco2, 2, vcf, 0);
        cable(pd, 0, xy, 0); cable(pd, 1, xy, 1); cable(vcf, 0, rv, 0); cable(rv, 0, out, 0); cable(rv, 1, out, 1); break;
    }
    case 18: {  // a Turing machine plays an organ of harmonics, in a scale
        int tm = add(MTuring); int q = add(MQuant, { { 0, 3 } }); int hm = add(MHarmonics, { { 0, -1 } }); int env = add(MEnv, { { 0, 0.003f }, { 1, 0.2f }, { 2, 30 }, { 3, 0.25f } });
        int vca = add(MVca); int dl = add(MDelay, { { 1, 40 }, { 2, 30 } }); int out = add(MOut);
        cable(tm, 0, q, 0); cable(q, 0, hm, 0); cable(tm, 1, env, 0); cable(hm, 0, vca, 0); cable(env, 0, vca, 1); cable(vca, 0, dl, 0); cable(dl, 0, out, 0); cable(dl, 0, out, 1); break;
    }
    case 19: {  // LIVE input 1 into a cloud of grains
        int in = add(MIn, { { 0, 1 } }); int gr = add(MGrains); int rv = add(MReverb, { { 0, 85 }, { 2, 50 } }); int sp = add(MSpectrum); int out = add(MOut);
        cable(in, 0, gr, 0); cable(gr, 0, rv, 0); cable(gr, 1, rv, 1); cable(rv, 0, sp, 0); cable(rv, 0, out, 0); cable(rv, 1, out, 1); break;
    }
    case 20: {  // a cellular automaton plays drums; a burst of hats on the fourth cell
        int ca = add(MCellular, { { 0, 30 } }); int k = add(MKick); int sn = add(MSnare); int h = add(MHat); int bu = add(MBurst, { { 0, 3 } }); int mx = add(MStMix); int out = add(MOut);
        cable(ca, 0, k, 0); cable(ca, 1, sn, 0); cable(ca, 2, h, 0); cable(ca, 3, bu, 0); cable(bu, 0, h, 0); cable(k, 0, mx, 0); cable(sn, 0, mx, 1); cable(h, 0, mx, 2);
        cable(mx, 0, out, 0); cable(mx, 1, out, 1); break;
    }
    default: add(MKeys); add(MOut); break;
    }
    return p;
}

} // namespace arco
