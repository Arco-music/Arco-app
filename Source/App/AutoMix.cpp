// ARCO 1.2: AUTO MIX (see AutoMix.h)
#include "AutoMix.h"
#include "AudioHost.h"
#include "Analysis.h"
#include "../Engine/StripMix.h"

namespace arco {
namespace automix {

static const char* LL[4] = { "A", "B", "C", "D" };
static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static double db(double g) { return 20.0 * std::log10(std::max(g, 1e-9)); }
static double volToDb(double v) { return db(std::pow(v / 100.0, 2.0) * 2.0); }
static double dbToVol(double d) { return juce::jlimit(0.0, 100.0, 100.0 * std::sqrt(std::pow(10.0, d / 20.0) / 2.0)); }
static juce::String hz(double f) { return f >= 1000 ? juce::String(f / 1000.0, f >= 10000 ? 0 : 1) + " kHz" : juce::String((int) std::round(f)) + " Hz"; }
static juce::String sdb(double d) { return (d > 0 ? "+" : U8("−")) + juce::String(std::abs(d), 1) + " dB"; }

// the sends that add space (they muddy a bass)
static const std::vector<std::string>& ambientSends() {
    static const std::vector<std::string> v = { "spc", "dly", "rev", "png", "grn", "frz", "res", "cho", "flg" };
    return v;
}

enum Role { Bass, Drums, Lead, Pad, Texture };
static const char* roleName(Role r) { static const char* N[5] = { "bass", "drums / rhythm", "lead / melody", "pad / chords", "texture" }; return N[r]; }

struct Track {
    int scene = 1, t = 0, strip = 4;
    const std::vector<float>* x = nullptr; long len = 0;
    double hiFrac = 0, lufs = -70, peakDb = -100, rmsDb = -100, crest = 0, density = 0, lowFrac = 0, centroid = 1000, st50 = -70, st90 = -70;
    std::vector<double> bands;
    Role role = Lead;
    MixSettings mix; double vol = 71, pan = 0;
    juce::StringArray did;
    double procLufs = -70;
    bool muted = false;
};

// ------------------------------------------------------------------ listening to one track
static void listen(Track& k, double sr) {
    const float* x = k.x->data(); long n = k.len;
    double pk = 0, ss = 0;
    for (long i = 0; i < n; ++i) { double a = std::abs(x[i]); pk = std::max(pk, a); ss += a * a; }
    k.peakDb = db(pk); k.rmsDb = 10.0 * std::log10(ss / std::max(1L, n) + 1e-14); k.crest = k.peakDb - k.rmsDb;
    juce::AudioBuffer<float> b(1, (int) n); b.copyFrom(0, 0, x, (int) n);
    k.lufs = loud::integrated(b, sr);
    k.bands = analysis::bands(x, (int) n, sr);
    auto on = analysis::onsets(x, (int) n, sr, 1.0f, true);
    k.density = on.size() / std::max(0.5, n / sr);
    double tot = 0, low = 0, cw = 0, hi = 0;
    for (int i = 0; i < analysis::NBANDS; ++i) { double p = std::pow(10.0, k.bands[(size_t) i] / 10.0), f = analysis::bandFreq(i); tot += p; if (f < 250) low += p; if (f > 4000) hi += p; cw += p * std::log2(f); }
    k.lowFrac = tot > 0 ? low / tot : 0; k.hiFrac = tot > 0 ? hi / tot : 0; k.centroid = tot > 0 ? std::pow(2.0, cw / tot) : 1000;
    // short-term levels (400 ms)
    std::vector<double> st; int w = (int) (0.4 * sr);
    for (long a = 0; a + w <= n; a += w / 2) { double s = 0; for (int i = 0; i < w; ++i) s += (double) x[a + i] * x[a + i]; st.push_back(10.0 * std::log10(s / w + 1e-14)); }
    if (!st.empty()) { std::sort(st.begin(), st.end()); k.st50 = st[st.size() / 2]; k.st90 = st[(size_t) (st.size() * 0.9)]; }
    // what is it?
    // drums: many sharp attacks, and noise up high (hi-hats, snares) even when the kick carries the weight
    if (k.density > 2.0 && k.crest > 12 && k.hiFrac > 0.004) k.role = Drums;
    else if (k.lowFrac > 0.55 && k.centroid < 320) k.role = Bass;
    else if (k.density < 1.0 && k.crest < 13) k.role = k.centroid > 2500 ? Texture : Pad;
    else if (k.centroid > 4000 && k.density < 2.0) k.role = Texture;
    else k.role = Lead;
}

// smoothing of a band curve over about an octave (for finding resonances)
static std::vector<double> smooth(const std::vector<double>& b) {
    std::vector<double> o(b.size());
    for (int i = 0; i < (int) b.size(); ++i) { double s = 0; int c = 0; for (int j = i - 2; j <= i + 2; ++j) if (j >= 0 && j < (int) b.size()) { s += b[(size_t) j]; ++c; } o[(size_t) i] = s / c; }
    return o;
}
static double bandAvg(const std::vector<double>& b, double f0, double f1) {
    double s = 0; int c = 0; for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f >= f0 && f <= f1) { s += std::pow(10.0, b[(size_t) i] / 10.0); ++c; } }
    return c ? 10.0 * std::log10(s / c + 1e-14) : -120.0;
}

// ------------------------------------------------------------------ the EQ and the compressor of one track
static void design(Track& k, const std::vector<Track*>& all, int style) {
    const double eqAmt = style == 0 ? 0.7 : (style == 2 ? 1.15 : 1.0);
    auto& b = k.bands;
    auto& m = k.mix;
    // high-pass: below the lowest real content (never into a bass)
    double mx = *std::max_element(b.begin(), b.end());
    int lowB = 0; for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i] > mx - 30) { lowB = i; break; }
    double lowF = analysis::bandFreq(lowB);
    double hpf = k.role == Bass ? 28.0 : juce::jlimit(30.0, k.role == Drums ? 45.0 : 160.0, lowF * 0.7);
    if (k.role == Lead || k.role == Pad) hpf = std::max(hpf, 40.0);
    m.hp = (float) std::round(hpf);
    k.did.add("high-pass " + hz(m.hp));
    // a very pure sound (a few notes, no noise, no body): cutting would only take away its notes
    int spread = 0; for (double v : b) if (v > mx - 24) ++spread;
    if (spread < 7) { k.did.add("pure tone - EQ left open"); return; }
    // the one bell: the biggest problem first - mud, harshness, a resonance, or presence
    auto sm = smooth(b);
    double mud = bandAvg(b, 180, 500) - bandAvg(b, 800, 2500);
    double harsh = bandAvg(b, 2500, 5000) - bandAvg(b, 800, 2000);
    int resB = -1; double resX = 0;
    for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f < 150 || f > 7000) continue; double ex = b[(size_t) i] - sm[(size_t) i]; if (ex > resX) { resX = ex; resB = i; } }
    // masking in the low mids: how many other tracks are strong where this one is
    int crowd = 0;
    for (auto* o : all) if (o != &k && !o->muted && o->scene == k.scene && bandAvg(o->bands, 200, 500) > bandAvg(b, 200, 500) - 6) ++crowd;
    if (k.role != Bass && (mud > 4.0 || (crowd >= 2 && mud > 0.0))) {
        int pkB = 0; double pv = -1e9; for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f >= 180 && f <= 500 && b[(size_t) i] > pv) { pv = b[(size_t) i]; pkB = i; } }
        m.pkf = (float) analysis::bandFreq(pkB); m.pkq = 1.2f;
        m.pkg = (float) -juce::jlimit(1.5, 4.5, (std::max(mud, 2.0) * 0.5 + crowd * 0.5) * eqAmt);
        k.did.add(sdb(m.pkg) + " at " + hz(m.pkf) + (crowd >= 2 ? " (room for the others)" : " (less boxy)"));
    } else if (harsh > 3.0 && k.role != Bass) {
        int pkB = 0; double pv = -1e9; for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f >= 2500 && f <= 5000 && b[(size_t) i] > pv) { pv = b[(size_t) i]; pkB = i; } }
        m.pkf = (float) analysis::bandFreq(pkB); m.pkq = 2.0f; m.pkg = (float) -juce::jlimit(1.5, 4.0, harsh * 0.6 * eqAmt);
        k.did.add(sdb(m.pkg) + " at " + hz(m.pkf) + " (less harsh)");
    } else if (resB >= 0 && resX > 5.0) {
        m.pkf = (float) analysis::bandFreq(resB); m.pkq = 3.0f; m.pkg = (float) -juce::jlimit(1.5, 5.0, (resX - 3.0) * 0.7 * eqAmt);
        k.did.add(sdb(m.pkg) + " at " + hz(m.pkf) + " (a ringing note)");
    } else if (k.role == Lead && bandAvg(b, 2000, 5000) < bandAvg(b, 500, 1500) - 8) {
        m.pkf = 3000; m.pkq = 0.9f; m.pkg = (float) (2.0 * eqAmt);
        k.did.add(sdb(m.pkg) + " at 3 kHz (clearer)");
    }
    // the top end
    double air = bandAvg(b, 7000, 14000) - bandAvg(b, 800, 2500);
    if (k.role == Lead && air < -20) { m.hsg = (float) (2.0 * eqAmt); m.hsf = 8000; k.did.add(sdb(m.hsg) + " air above 8 kHz"); }
    else if ((k.role == Pad || k.role == Texture) && air > -10) { m.hsg = (float) (-2.0 * eqAmt); m.hsf = 7000; k.did.add(sdb(m.hsg) + U8(" above 7 kHz (sits behind)")); }
    else if (k.role == Bass && air > -25) { m.hsg = -3.0f; m.hsf = 5000; k.did.add(U8("−3 dB above 5 kHz (no hiss)")); }
    // the low end
    if (k.role == Bass && bandAvg(b, 40, 100) < bandAvg(b, 100, 250) - 6 && bandAvg(b, 40, 100) > mx - 30) { m.lsg = 2.5f; m.lsf = 90; k.did.add("+2.5 dB below 90 Hz (fuller)"); }
    else if (k.role == Pad && bandAvg(b, 80, 200) > bandAvg(b, 300, 1000) + 3) { m.lsg = -2.0f; m.lsf = 150; k.did.add(U8("−2 dB below 150 Hz")); }
}

static void compress(Track& k, int style) {
    auto& m = k.mix;
    double amt = style == 0 ? 0.6 : (style == 2 ? 1.5 : 1.0);
    struct P { double ratio, att, rel, above; };
    P p = k.role == Drums ? P { 3.0, 18, 110, 1.5 } : k.role == Bass ? P { 4.0, 8, 160, 1.0 } : k.role == Lead ? P { 2.6, 8, 130, 2.0 } : P { 1.8, 30, 300, 1.0 };
    // compress the loudest part of the track: the threshold sits between its typical and its loud level
    double thr = k.st50 + (k.st90 - k.st50) * 0.5 + p.above - (amt - 1.0) * 4.0;
    if (k.st90 - k.st50 < 3 && k.crest < 10) { k.did.add("no compression needed (already even)"); return; }
    m.cth = (float) juce::jlimit(-45.0, -3.0, thr);
    m.crt = (float) (1.0 + (p.ratio - 1.0) * amt);
    m.cat = (float) p.att; m.crl = (float) p.rel;
    k.did.add("compressor " + juce::String(m.crt, 1) + ":1");
}

// how loud a track is after its EQ and compressor (by running them)
static double processedLufs(const Track& k, double sr) {
    StripMix sm; sm.init(sr);
    auto& F = mixFields();
    MixSettings ms = k.mix;
    for (int f = 0; f < (int) F.size(); ++f) sm.setp(f * StripMix::NS + 0, mixField(ms, f));
    juce::AudioBuffer<float> b(1, (int) k.len);
    const float* src = k.x->data(); float* dst = b.getWritePointer(0);
    std::array<const float*, 12> ip {}; std::array<float*, 12> op {};
    std::vector<float> tmpIn(64), tmpOut(64);
    for (long a = 0; a < k.len; a += 64) {
        int n = (int) std::min(64L, k.len - a);
        std::fill(tmpIn.begin(), tmpIn.end(), 0.0f); std::copy(src + a, src + a + n, tmpIn.begin());
        ip[0] = tmpIn.data(); op[0] = tmpOut.data();
        sm.process(ip.data(), op.data(), 64);
        std::copy(tmpOut.begin(), tmpOut.begin() + n, dst + a);
    }
    return loud::integrated(b, sr);
}

// ------------------------------------------------------------------ timing: slices moved towards the grid
std::vector<float> tighten(const std::vector<float>& xin, long L, double sr, double beatLen, float amount, int& moved, double& avgMs) {
    moved = 0; avgMs = 0;
    std::vector<float> x(xin.begin(), xin.begin() + L);
    if (L < (long) (sr * 0.5) || amount <= 0.001f || beatLen <= 16) return x;
    auto ons = analysis::onsets(x.data(), (int) L, sr, 1.0f, true);
    if (ons.size() < 2) return x;
    // the exact attacks
    struct A { double o, s; };
    std::vector<A> at;
    for (auto& o : ons) if (o.strength >= 0.18f) at.push_back({ analysis::attackStart(x.data(), (int) L, sr, o.pos), o.strength });
    if (at.size() < 2) return x;
    // the grid: the coarsest one the notes really follow (1/8, 1/8 triplets, 1/16, 1/16 triplets)
    const double grids[4] = { beatLen / 2, beatLen / 3, beatLen / 4, beatLen / 6 };
    double step = grids[2];
    for (double g : grids) {
        int near = 0; for (auto& a : at) { double d = std::fmod(a.o, g); d = std::min(d, g - d); if (d < g * 0.22) ++near; }
        if (near >= (int) (at.size() * 0.8)) { step = g; break; }
    }
    // swing: are the in-between notes consistently late? then that is the groove, keep it
    double swing = 0; { double s = 0; int c = 0;
        for (auto& a : at) { double q = a.o / step; long qi = (long) std::floor(q + 0.5); if (qi % 2 != 0) { s += (a.o - qi * step); ++c; } }
        if (c >= 3) { double mean = s / c; if (mean > step * 0.06 && mean < step * 0.4) swing = mean; } }
    auto gridOf = [&](double pos) {
        long qi = (long) std::floor(pos / step + 0.5);
        double best = 1e18, g0 = 0;
        for (long q = qi - 1; q <= qi + 1; ++q) { double g = q * step + ((q % 2 != 0) ? swing : 0.0); if (std::abs(g - pos) < best) { best = std::abs(g - pos); g0 = g; } }
        return g0; };
    // the anchors: (original, target)
    struct K { double o, t; };
    std::vector<K> ks;
    double maxMove = std::min(step * 0.35, 0.07 * sr), minMove = 0.0015 * sr, sum = 0;
    for (auto& a : at) {
        double g = gridOf(a.o), d = g - a.o;
        if (std::abs(d) > maxMove || std::abs(d) < minMove) { ks.push_back({ a.o, a.o }); continue; }
        double t = a.o + d * amount;
        ks.push_back({ a.o, t }); ++moved; sum += std::abs(t - a.o);
    }
    if (moved == 0) return x;
    avgMs = sum / moved / sr * 1000.0;
    std::sort(ks.begin(), ks.end(), [](const K& a, const K& b) { return a.o < b.o; });
    // keep the order (two anchors never cross)
    for (size_t i = 1; i < ks.size(); ++i) if (ks[i].t <= ks[i - 1].t + 0.004 * sr) ks[i].t = ks[i].o, ks[i - 1].t = std::min(ks[i - 1].t, ks[i - 1].o);
    // build the output: each slice (from just before its attack to just before the next) keeps its attack exactly and
    // its tail is stretched (best quality) to fill the new distance - nothing doubled, no gaps
    const long pre = (long) (0.002 * sr), xf = (long) (0.002 * sr);
    std::vector<float> out((size_t) L, 0.0f), wsum((size_t) L, 0.0f);
    auto src = [&](long p) { p %= L; if (p < 0) p += L; return x[(size_t) p]; };
    int K2 = (int) ks.size();
    for (int k = 0; k < K2; ++k) {
        double oN = k + 1 < K2 ? ks[(size_t) k + 1].o : ks[0].o + L, tN = k + 1 < K2 ? ks[(size_t) k + 1].t : ks[0].t + L;
        long s0 = (long) std::llround(ks[(size_t) k].o) - pre, s1 = (long) std::llround(oN) - pre;
        long d0 = (long) std::llround(ks[(size_t) k].t) - pre, d1 = (long) std::llround(tN) - pre;
        long inLen = s1 - s0, outLen = d1 - d0;
        if (inLen <= 0 || outLen <= 0) continue;
        std::vector<float> seg((size_t) (outLen + xf));
        if (std::abs(outLen - inLen) <= 1) for (long i = 0; i < outLen + xf; ++i) seg[(size_t) i] = src(s0 + i);
        else {
            long H = std::min((long) (0.03 * sr), inLen / 3);
            for (long i = 0; i < H; ++i) seg[(size_t) i] = src(s0 + i);
            long inT = inLen - H + xf, outT = outLen - H + xf;
            juce::AudioBuffer<float> tb(1, (int) inT);
            for (long i = 0; i < inT; ++i) tb.setSample(0, (int) i, src(s0 + H + i));
            auto st = analysis::stretch(tb, sr, (int) outT, 0.0);
            long cf = std::min(64L, outT / 4);
            for (long i = 0; i < outT; ++i) {
                float v = i < st.getNumSamples() ? st.getSample(0, (int) i) : 0.0f;
                if (i < cf) { float w = (float) i / (float) cf; v = src(s0 + H + i) * (1 - w) + v * w; }
                seg[(size_t) (H + i)] = v;
            }
        }
        for (long i = 0; i < outLen + xf; ++i) {
            double w = 1.0;
            if (i < xf) { w = std::sin((double) i / xf * juce::MathConstants<double>::halfPi); w *= w; }
            else if (i >= outLen) { w = std::cos((double) (i - outLen) / xf * juce::MathConstants<double>::halfPi); w *= w; }
            long q = ((d0 + i) % L + L) % L;
            out[(size_t) q] += (float) (w * seg[(size_t) i]); wsum[(size_t) q] += (float) w;
        }
    }
    for (long i = 0; i < L; ++i) out[(size_t) i] = wsum[(size_t) i] > 1e-4f ? out[(size_t) i] / wsum[(size_t) i] : x[(size_t) i];
    return out;
}

// ------------------------------------------------------------------ preparing (message thread)
AutoMixJob prepare(AudioHost& h, const AutoMixOptions& o) {
    AutoMixJob j; j.opt = o; j.host = &h;
    auto& e = h.engine; j.sr = e.sr;
    auto sv = [&](const std::string& n) { return (double) e.snap(n).v.load(); };
    j.beatLen = sv("sv_bl"); j.beats = juce::jmax(1, (int) std::round(sv("sv_bts")));
    int smask = (int) sv("smask");
    for (int s = 1; s <= 4; ++s) {
        bool want = o.allScenes ? ((smask >> (s - 1)) & 1) : (s == h.model.sceneNow && ((smask >> (s - 1)) & 1));
        if (!want) continue;
        AutoMixJob::Scene sc; sc.index = s;
        sc.len = (long) sv("sv_len" + std::to_string(s - 1)); sc.mask = (int) sv("sv_mask" + std::to_string(s - 1)); sc.speeds = (int) sv("sv_spd" + std::to_string(s - 1));
        auto& b = e.buffer(std::string("st_loops") + LL[s - 1]);
        sc.len = std::min(sc.len, b.len);
        if (sc.len < 1024) continue;
        for (int t = 0; t < 8; ++t) if ((sc.mask >> t) & 1) sc.audio[(size_t) t].assign(b.chan(t), b.chan(t) + sc.len);
        j.scenes.push_back(std::move(sc));
    }
    for (auto& d : paramTable()) {
        if (d.scope == 1) j.songVals[d.id] = h.model.get(d.id);
        else if (d.scope == 0) for (int s = 1; s <= 4; ++s) j.sceneVals[(size_t) s - 1][d.id] = h.model.getInScene(s, d.id);
    }
    j.visible = h.visibleCards;
    for (int t = 0; t < 8; ++t) j.src[t] = (int) h.model.get("src" + std::to_string(t + 1));
    j.nLive = h.nLive;
    j.snap = Renderer(h).snapshot();
    return j;
}

// ------------------------------------------------------------------ the mix (any thread)
AutoMixResult run(AutoMixJob& j, std::function<bool(float)> progress) {
    AutoMixResult r;
    if (j.scenes.empty()) { r.error = "Record some loops first - AUTO MIX listens to them."; return r; }
    const double sr = j.sr;
    auto& O = j.opt;
    auto cur = [&](int sc, const std::string& id) -> double {
        auto& m = sc == 0 ? j.songVals : j.sceneVals[(size_t) sc - 1]; auto it = m.find(id); return it == m.end() ? 0.0 : it->second; };
    std::map<std::pair<int, std::string>, double> planned;
    auto plan = [&](int sc, const std::string& id, double v) { planned[{ sc, id }] = v; };
    auto now = [&](int sc, const std::string& id) { auto it = planned.find({ sc, id }); return it != planned.end() ? it->second : cur(sc, id); };
    // which effects are in play: chosen on the main window, or used by some strip
    auto usedFx = [&](int sc, const std::string& key) {
        for (int s = 0; s < 12; ++s) { auto p = Model::stripParam(s, key); if (!p.empty() && cur(sc, p) > 0.5) return true; }
        return false; };
    auto inPlay = [&](int sc, const std::string& key) { return j.visible.contains(key) || usedFx(sc, key); };

    // 1. listen
    std::vector<std::unique_ptr<Track>> tracks;
    int total = 0; for (auto& s : j.scenes) for (int t = 0; t < 8; ++t) if (!s.audio[(size_t) t].empty()) ++total;
    int done = 0;
    for (auto& s : j.scenes)
        for (int t = 0; t < 8; ++t) {
            if (s.audio[(size_t) t].empty()) continue;
            auto k = std::make_unique<Track>(); k->scene = s.index; k->t = t; k->strip = 4 + t; k->x = &s.audio[(size_t) t]; k->len = s.len;
            k->muted = cur(s.index, "mute" + std::to_string(k->strip)) > 0.5;
            listen(*k, sr);
            if (k->peakDb > -60) tracks.push_back(std::move(k));
            if (progress && !progress(0.05f + 0.45f * (float) ++done / std::max(1, total))) return r;
        }
    if (tracks.empty()) { r.error = "The loops are silent."; return r; }
    std::vector<Track*> all; for (auto& k : tracks) all.push_back(k.get());
    r.lufsBefore = -70;

    // 2. per track: EQ, compressor
    for (auto* k : all) {
        if (k->muted) continue;
        if (O.eq) design(*k, all, O.style);
        if (O.dynamics) compress(*k, O.style);
        k->procLufs = O.eq || O.dynamics ? processedLufs(*k, sr) : k->lufs;
    }
    if (progress && !progress(0.6f)) return r;

    // 3. per scene: levels, panning, sends, timing
    for (auto& S : j.scenes) {
        std::vector<Track*> ts; for (auto* k : all) if (k->scene == S.index && !k->muted) ts.push_back(k);
        if (ts.empty()) continue;
        // levels: a hierarchy by role, measured after EQ and compression
        if (O.levels) {
            auto target = [&](Role ro) { switch (ro) { case Lead: return 0.0; case Drums: return -1.5; case Bass: return -2.5; case Pad: return -7.0; default: return -9.0; } };
            // the most important melody leads; a second lead sits a little lower
            bool firstLead = true;
            std::sort(ts.begin(), ts.end(), [](Track* a, Track* b) { return a->procLufs > b->procLufs; });
            std::map<Track*, double> tgt;
            for (auto* k : ts) { double tg = target(k->role); if (k->role == Lead) { if (!firstLead) tg -= 2.5; firstLead = false; } if (ts.size() == 1) tg = 0; tgt[k] = tg; }
            double A = 1e9; for (auto* k : ts) A = std::min(A, k->procLufs - tgt[k]);
            for (auto* k : ts) {
                double g = (tgt[k] + A) - k->procLufs;           // dB, the loudest planned part at 0 dB
                g = juce::jlimit(-30.0, 6.0, g);
                k->vol = dbToVol(g);
                double was = cur(S.index, "vol" + std::to_string(k->strip));
                if (std::abs(k->vol - was) >= 0.5) k->did.insert(0, "level " + sdb(volToDb(k->vol) - volToDb(was)));
                plan(S.index, "vol" + std::to_string(k->strip), k->vol);
            }
        }
        // panning: the low end and the lead in the centre, the rest spread so both sides weigh the same
        if (O.pan) {
            std::vector<Track*> side;
            bool leadCentred = false;
            for (auto* k : ts) {
                bool centre = k->role == Bass || (k->role == Drums && k->lowFrac > 0.35) || (k->role == Lead && !leadCentred);
                if (k->role == Lead && !leadCentred) leadCentred = true;
                if (centre) k->pan = 0; else side.push_back(k);
            }
            std::sort(side.begin(), side.end(), [](Track* a, Track* b) { return a->procLufs > b->procLufs; });
            for (size_t i = 0; i < side.size(); ++i) {
                auto* k = side[i];
                double w = k->role == Pad ? 35 : k->role == Texture ? 40 : k->role == Drums ? 25 : 22;
                if (side.size() % 2 == 1 && i == side.size() - 1) w *= 0.5;      // an odd one out: closer to the middle
                k->pan = (i % 2 == 0 ? -1 : 1) * w;
            }
            for (auto* k : ts) {
                plan(S.index, "pan" + std::to_string(k->strip), k->pan);
                k->did.add(std::abs(k->pan) < 1 ? juce::String("centre") : (k->pan < 0 ? "left " : "right ") + juce::String((int) std::abs(k->pan * 2)) + " %");
            }
        }
        // the mixing desk settings
        for (auto* k : ts) {
            auto& F = mixFields(); MixSettings def;
            for (int f = 0; f < (int) F.size(); ++f) {
                float v = mixField(k->mix, f);
                if (O.eq || O.dynamics || std::abs(cur(S.index, "mx_" + F[(size_t) f] + std::to_string(k->strip)) - v) > 1e-4)
                    plan(S.index, "mx_" + F[(size_t) f] + std::to_string(k->strip), v);
                juce::ignoreUnused(def);
            }
        }
        // effects: each part gets the space it can carry
        if (O.effects) {
            for (auto* k : ts) {
                for (auto& key : ambientSends()) {
                    auto p = Model::stripParam(k->strip, key); if (p.empty()) continue;
                    double v = cur(S.index, p), nv = v;
                    if (k->role == Bass) nv = std::min(v, 5.0);
                    else if (k->role == Drums && (key == "spc" || key == "dly" || key == "png" || key == "rev")) nv = std::min(v, 20.0);
                    if (nv != v) { plan(S.index, p, nv); k->did.add(juce::String(key == "spc" ? "SPACE" : key == "dly" ? "DELAY" : juce::String(key).toUpperCase()) + " send " + juce::String((int) v) + juce::String::fromUTF8(" → ") + juce::String((int) nv) + (k->role == Bass ? " (a clean low end)" : "")); }
                }
                // a little shared SPACE glues the parts together (when SPACE is on the main window)
                if (inPlay(S.index, "spc") && k->role != Bass) {
                    auto p = Model::stripParam(k->strip, "spc");
                    double v = cur(S.index, p);
                    if (v < 0.5) {
                        double add = (k->role == Pad || k->role == Texture ? 25 : k->role == Lead ? 15 : 8) * (O.style == 0 ? 1.2 : O.style == 2 ? 0.7 : 1.0);
                        plan(S.index, p, add); k->did.add("SPACE " + juce::String((int) add) + " (together in one room)");
                    }
                }
            }
        }
        // timing
        if (O.timing && O.timingAmount > 0.001f) {
            for (auto* k : ts) {
                int spd = (S.speeds >> (2 * k->t)) & 3;
                if (spd != 0) { k->did.add("timing: left alone (plays at another speed)"); continue; }
                if (k->role == Pad || k->role == Texture) continue;     // long notes: nothing to tighten
                int moved = 0; double avg = 0;
                auto nx = tighten(*k->x, S.len, sr, j.beatLen, O.timingAmount, moved, avg);
                if (moved > 0) {
                    r.audio.push_back({ S.index, k->t, *k->x, std::move(nx) });
                    k->did.add("timing: " + juce::String(moved) + (moved == 1 ? " note" : " notes") + " closer to the beat (" + juce::String(avg, 1) + " ms on average)");
                }
            }
        }
        if (progress && !progress(0.6f + 0.2f * (float) (&S - &j.scenes[0] + 1) / (float) j.scenes.size())) return r;
    }

    // 4. the live inputs: like the tracks they recorded
    for (auto& S : j.scenes)
        for (int L = 0; L < std::max(1, j.nLive); ++L) {
            std::vector<Track*> rec; for (auto* k : all) if (k->scene == S.index && j.src[k->t] == L + 1 && !k->muted) rec.push_back(k);
            if (rec.empty() || !(O.eq || O.dynamics)) continue;
            MixSettings m;
            double hp = 400; for (auto* k : rec) hp = std::min(hp, (double) (k->mix.hp > 0 ? k->mix.hp : 30.0f));
            m.hp = (float) std::max(25.0, hp * 0.8);              // a little lower: a live player goes lower sometimes
            if (O.dynamics) { m.cth = -20; m.crt = O.style == 2 ? 2.5f : 2.0f; m.cat = 10; m.crl = 150; }
            auto& F = mixFields();
            for (int f = 0; f < (int) F.size(); ++f) plan(S.index, "mx_" + F[(size_t) f] + std::to_string(L), mixField(m, f));
            r.report.push_back({ "LIVE " + juce::String(L + 1), "your live sound", "high-pass " + hz(m.hp) + (O.dynamics ? ", gentle compressor 2:1" : "") + U8(" · its fader stays yours"), L, S.index });
        }

    // 5. effects in tempo
    juce::StringArray fxLines;
    if (O.effects) {
        double beatSec = j.beatLen / sr;
        auto snapRate = [&](const char* id, const char* name, std::function<double(double)> toHz, std::function<double(double)> toKnob, bool used) {
            if (!used) return;
            double v = cur(0, id) , f = toHz(v);
            if (f <= 0) return;
            double best = 0, bestErr = 1e9; juce::String bestName;
            const double mult[6] = { 0.5, 1, 2, 4, 8, 16 }; const char* nm[6] = { "every half beat", "every beat", "every 2 beats", "once a bar", "every 2 bars", "every 4 bars" };
            for (int i = 0; i < 6; ++i) { double fh = 1.0 / (beatSec * mult[i] * (i >= 3 ? j.beats / 4.0 : 1.0)); double e = std::abs(std::log(f / fh)); if (e < bestErr) { bestErr = e; best = fh; bestName = nm[i]; } }
            if (bestErr < std::log(1.18) && bestErr > std::log(1.005)) {
                double nk = juce::jlimit(0.0, 100.0, toKnob(best));
                plan(0, id, nk);
                fxLines.add(juce::String(name) + " now moves " + bestName + " (was " + juce::String(f, 2) + " Hz)");
            }
        };
        bool any = false;
        auto usedAny = [&](const std::string& key) { for (auto& S : j.scenes) if (usedFx(S.index, key)) return true; return false; };
        snapRate("phs_rate", "PHASER", [](double v) { return 0.03 * std::pow(100.0, v / 100.0); }, [](double f) { return 100.0 * std::log(f / 0.03) / std::log(100.0); }, usedAny("phs"));
        snapRate("cho_rate", "CHORUS", [](double v) { return 0.05 * std::pow(60.0, v / 100.0); }, [](double f) { return 100.0 * std::log(f / 0.05) / std::log(60.0); }, usedAny("cho"));
        if (usedAny("flg") && cur(0, "flg_rate") < 50) {
            double v = cur(0, "flg_rate"), f = 0.03 + v / 100.0 * 2.4, bar = 1.0 / (beatSec * j.beats);
            const double fh[3] = { bar / 2, bar, bar * 2 }; const double kn[3] = { 58, 75, 92 }; const char* nm[3] = { "every 2 bars", "once a bar", "twice a bar" };
            for (int i = 0; i < 3; ++i) if (std::abs(std::log(f / fh[i])) < std::log(1.25)) { plan(0, "flg_rate", kn[i]); fxLines.add(juce::String("FLANGER now sweeps ") + nm[i] + ", locked to the tempo"); break; }
        }
        // the delay: clear echoes
        if (usedAny("dly") && cur(0, "dly_fb") > 72) { plan(0, "dly_fb", 62); fxLines.add(U8("DELAY feedback 62 (clearer repeats - it already follows the tempo)")); }
        else if (usedAny("dly")) fxLines.add("DELAY and REVERSE already follow the tempo");
        // SPACE: a decay that breathes with the bar
        bool spcUsed = false; for (auto& kv : planned) if (kv.first.second.rfind("spc", 0) == 0 && kv.second > 0.5) spcUsed = true;
        if (spcUsed || usedAny("spc")) {
            double bar = beatSec * j.beats, want = juce::jlimit(1.2, 3.5, bar * 0.9);
            double knob = 100.0 * std::sqrt(std::max(0.0, (want - 0.4) / 24.0));
            if (std::abs(knob - cur(0, "spc_decay")) > 2) { plan(0, "spc_decay", knob); fxLines.add("SPACE decay " + juce::String(want, 1) + " s (about a bar)"); }
            any = true;
        }
        juce::ignoreUnused(any);
    }
    if (!fxLines.isEmpty()) r.report.push_back({ "EFFECTS", "in tempo", fxLines.joinIntoString("  ·  "), -1, 0 });

    // 6. the master: measure the new mix, then level it for streaming with a safe ceiling
    if (O.master) {
        double glue = O.style == 0 ? 20 : O.style == 2 ? 45 : 30;
        plan(0, "glue", glue);
        // render the scene with every planned change
        RenderSnapshot snap = j.snap;
        juce::var song = juce::JSON::parse(juce::JSON::toString(snap.song));     // a deep copy
        if (auto* so = song["song"].getDynamicObject()) for (auto& kv : planned) if (kv.first.first == 0) so->setProperty(juce::Identifier(kv.first.second), kv.second);
        if (auto* arr = song["scenes"].getArray())
            for (auto& kv : planned) if (kv.first.first >= 1 && kv.first.first <= arr->size())
                if (auto* o = (*arr)[kv.first.first - 1].getDynamicObject()) o->setProperty(juce::Identifier(kv.first.second), kv.second);
        if (auto* so = song["song"].getDynamicObject()) { so->setProperty("ms_gain", 0.0); so->setProperty("ms_lim", 0); so->setProperty("ms_lsg", 0.0); so->setProperty("ms_hsg", 0.0); }
        snap.song = song;
        int sc = j.scenes[0].index; for (auto& S : j.scenes) if (S.index == snap.sceneNow) sc = S.index;
        snap.sceneNow = sc;
        ExportOptions eo; eo.what = ExportOptions::CurrentScene; eo.repeats = 2; eo.tailSeconds = 0;
        juce::AudioBuffer<float> mix; double rate = sr;
        bool rok = Renderer(*j.host).renderMix(snap, eo, mix, rate, -1, [&](float p) { return !progress || progress(0.8f + 0.18f * p); });
        if (rok && mix.getNumSamples() > 0) {
            double lu = loud::integrated(mix, rate);
            r.lufsBefore = lu;
            double target = O.style == 0 ? -16.0 : O.style == 2 ? -11.0 : -14.0;
            double gain = juce::jlimit(-12.0, 18.0, target - lu);
            plan(0, "ms_gain", gain); plan(0, "ms_lim", 1); plan(0, "ms_ceil", -1.0);
            // the overall colour, compared with what well-balanced mixes look like
            std::vector<float> mono((size_t) mix.getNumSamples());
            for (int i = 0; i < mix.getNumSamples(); ++i) mono[(size_t) i] = 0.5f * (mix.getSample(0, i) + mix.getSample(1, i));
            auto bb = analysis::bands(mono.data(), (int) mono.size(), rate);
            double mid = bandAvg(bb, 500, 2000), lowRel = bandAvg(bb, 50, 160) - mid, highRel = bandAvg(bb, 6000, 12000) - mid;
            double lsg = lowRel > -2 ? juce::jlimit(-3.0, 3.0, (8.0 - lowRel) * 0.35) : 0.0;
            double hsg = juce::jlimit(-3.0, 3.0, (-14.0 - highRel) * 0.35);
            if (std::abs(lsg) < 0.5) lsg = 0; if (std::abs(hsg) < 0.5) hsg = 0;
            plan(0, "ms_lsg", lsg); plan(0, "ms_hsg", hsg);
            r.lufsAfter = target;
            juce::String w = juce::String(lu, 1) + juce::String::fromUTF8(" → ") + juce::String(target, 0) + " LUFS (" + (O.style == 0 ? "natural" : O.style == 2 ? "loud" : "streaming") + " level), peaks never above "
                             + U8("−1 dB") + ", glue " + juce::String((int) glue);
            if (lsg != 0) w << ", lows " << sdb(lsg);
            if (hsg != 0) w << ", air " << sdb(hsg);
            r.report.push_back({ "MASTER", "the whole mix", w, -1, 0 });
        }
    }

    // 7. the report: the tracks first, then the live inputs, the effects, the master
    {
        std::vector<AutoMixResult::Line> lines;
        for (auto* k : all) {
            juce::String who = "TRACK " + juce::String(k->t + 1) + (j.scenes.size() > 1 ? juce::String::fromUTF8(" · scene ") + LL[k->scene - 1] : juce::String());
            if (k->muted) lines.push_back({ who, "muted", "left as it is", k->strip, k->scene });
            else lines.push_back({ who, roleName(k->role), k->did.joinIntoString(juce::String::fromUTF8("  ·  ")), k->strip, k->scene });
        }
        r.report.insert(r.report.begin(), lines.begin(), lines.end());
    }
    for (auto& kv : planned) {
        double before = cur(kv.first.first, kv.first.second);
        if (std::abs(before - kv.second) > 1e-6) r.changes.push_back({ kv.first.first, kv.first.second, before, kv.second });
    }
    int nt = 0; for (auto* k : all) if (!k->muted) ++nt;
    r.headline = juce::String(nt) + (nt == 1 ? " track" : " tracks") + " mixed" + (r.audio.empty() ? juce::String() : ", " + juce::String((int) r.audio.size()) + " tightened")
                 + (O.master && r.lufsBefore > -69 ? juce::String::fromUTF8(" · master ") + juce::String(r.lufsAfter, 0) + " LUFS" : juce::String());
    r.ok = true;
    if (progress) progress(1.0f);
    return r;
}

// ------------------------------------------------------------------ A / B (message thread)
void apply(AudioHost& h, const AutoMixResult& r, bool after) {
    for (auto& c : r.changes) {
        double v = after ? c.after : c.before;
        if (c.scene == 0) h.model.set(c.id, v); else h.model.setInScene(c.scene, c.id, v);
    }
    for (auto& a : r.audio) {
        auto& b = h.engine.buffer(std::string("st_loops") + LL[a.scene - 1]);
        auto& src = after ? a.after : a.before;
        long n = std::min((long) src.size(), b.len);
        std::copy(src.begin(), src.begin() + n, b.chan(a.track));
    }
}

} // namespace automix
} // namespace arco
