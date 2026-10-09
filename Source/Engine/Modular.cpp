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

void Modular::rebuild() {
    std::vector<Rt> old; old.swap(rt);
    rt.reserve(patch.mods.size());
    for (auto& m : patch.mods) {
        Rt r;
        for (auto& o : old) if (o.id == m.id && o.type == m.type) { r = std::move(o); break; }
        r.id = m.id; r.type = m.type; r.p = m.p;
        for (int k = 0; k < 4; ++k) { r.inMod[k] = -1; r.inPort[k] = 0; }
        if (m.type == MDelay) { int want = (int) (sr * 2.1) + 4; if (r.bw != want) { r.buf.assign((size_t) want, 0.0f); r.bw = want; } }
        rt.push_back(std::move(r));
    }
    auto indexOf = [this](int id) { for (size_t i = 0; i < rt.size(); ++i) if (rt[i].id == id) return (int) i; return -1; };
    for (auto& c : patch.cables) {
        int f = indexOf(c.fromMod), t = indexOf(c.toMod);
        if (f < 0 || t < 0 || c.toPort < 0 || c.toPort > 3 || c.fromPort < 0 || c.fromPort > 4) continue;
        rt[(size_t) t].inMod[c.toPort] = f; rt[(size_t) t].inPort[c.toPort] = c.fromPort;
    }
}

void Modular::process(float* L, float* R, int n, double bpm, double beat) {
    if (! running.load()) return;
    const juce::ScopedTryLock sl(lock);
    if (! sl.isLocked()) return;
    if (rt.empty()) return;
    if (bpm <= 1) bpm = 120;
    const bool own = beat < 0;
    double b0 = own ? ownBeat : beat;
    const double beatPerSample = bpm / 60.0 / sr;
    // the keyboard's state for this block
    int note; float vel; bool gate; bool retrig;
    { const juce::SpinLock::ScopedLockType nl(noteLock); note = curNote; vel = curVel; gate = gateOn; retrig = trigCount != lastTrigCount; lastTrigCount = trigCount; }
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
            default: break;
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

// ------------------------------------------------------------------ save / open
juce::var Modular::toVar(const ModPatch& p) {
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> ms;
    for (auto& m : p.mods) {
        auto* mo = new juce::DynamicObject(); mo->setProperty("type", modDefs()[(size_t) m.type].name); mo->setProperty("id", m.id);
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
            ModModule m; m.type = type; m.id = (int) mv["id"];
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

juce::StringArray Modular::patchNames() { return { "Basic subtractive", "Generative sequence", "FM bell", "Noise drums", "Empty rack" }; }

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
    default: add(MKeys); add(MOut); break;
    }
    return p;
}

} // namespace arco
