// ARCO 1.3: AUTO MIX (see AutoMix.h) - rebuilt the way a mixing engineer works:
//  1. listen: what each part IS (its pitch range, how melodic, how rhythmic, how percussive, how sustained, how wet)
//  2. decide the roles relative to each other: the bass (the lowest line), the melody (the most melodic, singing part),
//     rhythm (drums, or a repeated pitched figure), chords / pads, texture
//  3. per part: high-pass below its lowest note, two EQ bells (masking against more important parts first, then mud,
//     resonances, harshness, presence), a compressor set for a target gain reduction on its loud parts
//  4. levels by priority (measured after EQ + compression, gated loudness), panning that keeps the low end and the
//     melody centred and places parts that clash on opposite sides
//  5. one room: every part sends to SPACE by its depth (front / middle / back), less for parts that already carry their
//     own reverb; the other effects send their echoes into the same room; DELAY on the melody's rhythm
//  6. the master: tonal balance against a reference curve, glue, loudness for streaming with a true-peak ceiling -
//     rendered and measured twice so the number is the real one.
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
static juce::String noteOf(double f) { if (f <= 0) return "-"; int m = (int) std::lround(69 + 12 * std::log2(f / 440.0)); return juce::MidiMessage::getMidiNoteName(m, true, true, 4); }

// the sends that add space (they muddy a bass)
static const std::vector<std::string>& ambientSends() {
    static const std::vector<std::string> v = { "spc", "dly", "rev", "png", "grn", "frz", "res", "cho", "flg", "pdl" };
    return v;
}

enum Role { Bass, Drums, Rhythm, Lead, Counter, Pad, Texture };
static const char* roleName(Role r) { static const char* N[7] = { "bass", "drums / percussion", "rhythm", "melody", "second melody", "chords / pad", "texture" }; return N[r]; }
// who wins where two parts fight for the same frequencies (lower = more important)
static int priority(Role r, double f) {
    if (f < 220) { static const int P[7] = { 0, 1, 4, 3, 5, 6, 7 }; return P[r]; }   // the low end belongs to the bass, then the kick
    static const int P[7] = { 3, 1, 4, 0, 2, 5, 6 }; return P[r];
}

struct Track {
    int scene = 1, t = 0, strip = 4;
    const std::vector<float>* x = nullptr; long len = 0;
    // what it is
    double lufs = -70, peakDb = -100, rmsDb = -100, crest = 0, density = 0, lowFrac = 0, hiFrac = 0, centroid = 1000, st10 = -70, st50 = -70, st90 = -70;
    bool poly = false;
    double pitched = 0, f0med = 0, f0lo = 0, f0hi = 0, rangeSt = 0, regular = 1, ioiMed = 0, sustain = 0, decaySec = 0, attackSharp = 0;
    std::vector<double> bands;
    Role role = automix::Lead; juce::String why;
    // what it gets
    MixSettings mix; double vol = 71, pan = 0;
    juce::StringArray did;
    double procLufs = -70, active = -70, rawActive = -70;     // after its EQ + compressor: integrated, and how loud it is while it plays
    double gainDb = 0;                         // the fader move it gets
    bool muted = false;
};

// ------------------------------------------------------------------ listening to one track
// pitch per 20 ms (YIN at a quarter of the rate): the fundamental and how clear it is
static void pitchTrack(const float* x, long n, double sr, std::vector<double>& f0, std::vector<double>& clar, std::vector<double>& lev) {
    const int D = std::max(1, (int) std::round(sr / 12000.0)); const double fs = sr / D;
    std::vector<float> y((size_t) (n / D) + 1); { double a = 0; for (long i = 0, k = 0; i + D <= n; i += D, ++k) { double s = 0; for (int j = 0; j < D; ++j) s += x[i + j]; a += 0.6 * (s / D - a); y[(size_t) k] = (float) a; } }
    const int W = 512, maxLag = std::min(W - 2, (int) (fs / 38.0)), minLag = std::max(2, (int) (fs / 1600.0)), hop = (int) (0.02 * fs);
    std::vector<float> d((size_t) maxLag + 2), cm((size_t) maxLag + 2);
    for (long s = 0; s + W + maxLag < (long) y.size(); s += hop) {
        const float* p = y.data() + s;
        double e = 0; for (int k = 0; k < W; ++k) e += p[k] * p[k];
        lev.push_back(10.0 * std::log10(e / W + 1e-14));
        if (e / W < 1e-8) { f0.push_back(0); clar.push_back(0); continue; }
        float run = 0; int best = -1;
        for (int tau = 1; tau <= maxLag; ++tau) { float acc = 0; for (int k = 0; k < W; ++k) { float q = p[k] - p[k + tau]; acc += q * q; } d[(size_t) tau] = acc; run += acc; cm[(size_t) tau] = run > 0 ? acc * tau / run : 1.0f; }
        for (int tau = minLag; tau <= maxLag; ++tau) if (cm[(size_t) tau] < 0.2f) { while (tau + 1 <= maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0) { f0.push_back(0); clar.push_back(0); continue; }
        double t = best; if (best > minLag && best < maxLag) { double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1], den = a - 2 * b + c; if (std::abs(den) > 1e-9) t += 0.5 * (a - c) / den; }
        f0.push_back(fs / t); clar.push_back(1.0 - cm[(size_t) best]);
    }
}

static double pct(std::vector<double> v, double q) { if (v.empty()) return 0; std::sort(v.begin(), v.end()); return v[(size_t) juce::jlimit(0, (int) v.size() - 1, (int) (q * (v.size() - 1)))]; }

// how loud a part sounds WHILE IT PLAYS (K-weighted, 400 ms blocks, the louder half of the blocks it sounds in).
// The integrated loudness of a loop counts its rests: a melody with space between its phrases reads quieter than it
// sounds, a held chord louder - balancing by it pushes melodies up and long notes down. This is what the ear compares.
static double activeLoudness(const juce::AudioBuffer<float>& b, double sr) {
    int n = b.getNumSamples(); if (n < (int) (0.4 * sr)) return loud::integrated(b, sr);
    std::vector<double> sq((size_t) n, 0.0);
    for (int c = 0; c < b.getNumChannels(); ++c) { loud::Biquad sh, hp; loud::kweight(sr, sh, hp); auto* d = b.getReadPointer(c); for (int i = 0; i < n; ++i) { double y = hp.tick(sh.tick(d[i])); sq[(size_t) i] += y * y; } }
    int blk = (int) (0.4 * sr), hop = (int) (0.1 * sr);
    std::vector<double> z; double run = 0; for (int i = 0; i < blk; ++i) run += sq[(size_t) i];
    for (int st = 0; st + blk <= n; st += hop) { if (st > 0) { for (int i = st - hop; i < st; ++i) run -= sq[(size_t) i]; for (int i = st + blk - hop; i < st + blk; ++i) run += sq[(size_t) i]; } z.push_back(std::max(0.0, run / blk)); }
    double mx = 0; for (double v : z) mx = std::max(mx, v);
    if (mx <= 1e-12) return -70.0;
    std::vector<double> on; for (double v : z) if (v > mx * 0.01) on.push_back(v);          // it sounds: within 20 dB of its loudest
    std::sort(on.begin(), on.end());
    double s = 0; int c = 0; for (size_t i = on.size() / 2; i < on.size(); ++i) { s += on[i]; ++c; }
    return c ? -0.691 + 10.0 * std::log10(s / c) : -70.0;
}

static void listen(Track& k, double sr) {
    const float* x = k.x->data(); long n = k.len;
    double pk = 0, ss = 0;
    for (long i = 0; i < n; ++i) { double a = std::abs(x[i]); pk = std::max(pk, a); ss += a * a; }
    k.peakDb = db(pk); k.rmsDb = 10.0 * std::log10(ss / std::max(1L, n) + 1e-14); k.crest = k.peakDb - k.rmsDb;
    juce::AudioBuffer<float> b(1, (int) n); b.copyFrom(0, 0, x, (int) n);
    k.lufs = loud::integrated(b, sr);
    k.rawActive = activeLoudness(b, sr);
    k.bands = analysis::bands(x, (int) n, sr);
    auto on = analysis::onsets(x, (int) n, sr, 1.0f, true);
    k.density = on.size() / std::max(0.5, n / sr);
    double tot = 0, low = 0, cw = 0, hi = 0;
    for (int i = 0; i < analysis::NBANDS; ++i) { double p = std::pow(10.0, k.bands[(size_t) i] / 10.0), f = analysis::bandFreq(i); tot += p; if (f < 250) low += p; if (f > 4000) hi += p; cw += p * std::log2(f); }
    k.lowFrac = tot > 0 ? low / tot : 0; k.hiFrac = tot > 0 ? hi / tot : 0; k.centroid = tot > 0 ? std::pow(2.0, cw / tot) : 1000;
    // short-term levels (400 ms): how even it is
    std::vector<double> st; int w = (int) (0.4 * sr);
    for (long a = 0; a + w <= n; a += w / 2) { double s = 0; for (int i = 0; i < w; ++i) s += (double) x[a + i] * x[a + i]; st.push_back(10.0 * std::log10(s / w + 1e-14)); }
    if (!st.empty()) { k.st10 = pct(st, 0.1); k.st50 = pct(st, 0.5); k.st90 = pct(st, 0.9); }
    // pitch: how much of it has a clear pitch, where it lives, how far it moves
    std::vector<double> f0, cl, lv; pitchTrack(x, n, sr, f0, cl, lv);
    double lvMax = lv.empty() ? -100 : *std::max_element(lv.begin(), lv.end());
    std::vector<double> fs, st2; int act = 0, pit = 0;
    for (size_t i = 0; i < f0.size(); ++i) {
        if (lv[i] < lvMax - 30) continue;
        ++act;
        if (cl[i] > 0.8 && f0[i] > 30) { ++pit; fs.push_back(f0[i]); st2.push_back(12 * std::log2(f0[i] / 440.0)); }
    }
    k.pitched = act ? (double) pit / act : 0;
    if (!fs.empty()) { k.f0med = pct(fs, 0.5); k.f0lo = pct(fs, 0.08); k.f0hi = pct(fs, 0.92); k.rangeSt = pct(st2, 0.92) - pct(st2, 0.08); }
    // a chord gives a "pitch" far below its notes (their common root): there is no energy down there.
    // Then it is several notes at once - its real low end is its lowest strong band.
    if (k.f0med > 0) {
        double mxB = *std::max_element(k.bands.begin(), k.bands.end());
        auto bandAt = [&](double f) { int bi = 0; double bd = 1e9; for (int i = 0; i < analysis::NBANDS; ++i) { double d = std::abs(std::log2(analysis::bandFreq(i) / f)); if (d < bd) { bd = d; bi = i; } } return k.bands[(size_t) bi]; };
        if (bandAt(k.f0med) < mxB - 15) {
            k.poly = true;
            int lb = 0; for (int i = 0; i < analysis::NBANDS; ++i) if (k.bands[(size_t) i] > mxB - 12) { lb = i; break; }
            double f = analysis::bandFreq(lb);
            k.f0lo = f; k.f0med = f * 1.5; k.f0hi = std::max(k.f0hi, f * 2); k.pitched *= 0.5;
        }
    }
    // rhythm: the time between notes, and how regular it is
    std::vector<double> ioi; for (size_t i = 1; i < on.size(); ++i) ioi.push_back((on[i].pos - on[i - 1].pos) / sr);
    if (ioi.size() >= 3) {
        k.ioiMed = pct(ioi, 0.5);
        double m = 0, v = 0; for (double d : ioi) m += d; m /= ioi.size(); for (double d : ioi) v += (d - m) * (d - m);
        k.regular = std::sqrt(v / ioi.size()) / std::max(1e-3, m);
    }
    // sustain: how much of the time it sounds (within 15 dB of its loud level); attacks: how sharp
    { int c = 0; for (double v : st) if (v > k.st90 - 15) ++c; k.sustain = st.empty() ? 0 : (double) c / st.size(); }
    { double s = 0; for (auto& o : on) s += o.strength; k.attackSharp = on.empty() ? 0 : s / on.size(); }
    // how wet: a part recorded in a room decays in two slopes - the note falls fast, then the room's tail falls slowly.
    // (a plucked note on its own falls in one slope, however long)
    {
        int h = (int) (0.01 * sr); std::vector<double> env;
        for (long a = 0; a + h <= n; a += h) { double s = 0; for (int i = 0; i < h; ++i) s += (double) x[a + i] * x[a + i]; env.push_back(10.0 * std::log10(s / h + 1e-14)); }
        std::vector<double> tails;
        for (size_t i = 0; i < on.size(); ++i) {
            long e0 = (long) (on[i].pos / h), e1 = i + 1 < on.size() ? (long) (on[i + 1].pos / h) : (long) env.size();
            if (e1 - e0 < 30) continue;
            long pkI = e0; for (long j = e0; j < std::min(e0 + 10, e1); ++j) if (env[(size_t) j] > env[(size_t) pkI]) pkI = j;
            double top = env[(size_t) pkI]; long a5 = -1, a15 = -1, a35 = -1;
            for (long j = pkI; j < e1; ++j) { double v = env[(size_t) j]; if (a5 < 0 && v < top - 5) a5 = j; if (a15 < 0 && v < top - 15) a15 = j; if (v < top - 35) { a35 = j; break; } }
            if (a5 < 0 || a15 <= a5 || a35 <= a15) continue;
            double early = (a15 - a5) * 0.01 * 6.0, late = (a35 - a15) * 0.01 * 3.0;     // both as a 60 dB decay time
            if (late > early * 1.8 && late > 0.5) tails.push_back(late); else tails.push_back(0.0);
        }
        int wet = 0; for (double t : tails) if (t > 0) ++wet;
        k.decaySec = tails.size() >= 2 && wet * 2 >= (int) tails.size() ? pct(std::vector<double>(tails), 0.75) : 0;
    }
}

// the roles, decided together for one scene
static void decideRoles(std::vector<Track*>& ts, double beatSec) {
    if (ts.empty()) return;
    std::vector<Track*> rest;
    for (auto* k : ts) {
        // percussion: sharp, short hits; either no steady pitch, or a pitch that sweeps (a kick) with noise on top (snare, hats)
        bool shortHits = k->sustain < 0.72 && k->crest > 11 && k->density > 1.2;
        bool perc = (k->pitched < 0.35 && k->density > 1.2 && (k->crest > 10 || k->attackSharp > 0.5 || k->hiFrac > 0.02))
                 || (shortHits && k->hiFrac > 0.004 && (k->pitched < 0.6 || k->rangeSt > 10));
        if (perc) { k->role = automix::Drums; k->why = "sharp attacks without a steady pitch"; } else rest.push_back(k);
    }
    // the bass: the lowest pitched line
    Track* bass = nullptr;
    for (auto* k : rest) if (k->pitched > 0.25 && k->f0med > 0 && !k->poly && (bass == nullptr || k->f0med < bass->f0med)) bass = k;
    if (bass != nullptr) {
        double next = 1e9; for (auto* k : rest) if (k != bass && k->f0med > 0) next = std::min(next, k->f0med);
        bool low = bass->f0med < 165 || (bass->f0med < 230 && next > bass->f0med * 1.8) || (bass->lowFrac > 0.55 && bass->f0med < 260);
        if (low && rest.size() + (ts.size() - rest.size()) > 1) { bass->role = automix::Bass; bass->why = "the lowest line (" + noteOf(bass->f0lo) + U8("–") + noteOf(bass->f0hi) + ")"; }
        else bass = nullptr;
    }
    std::vector<Track*> mel;
    for (auto* k : rest) {
        if (k == bass) continue;
        bool sustained = k->density < 0.9 || (k->sustain > 0.8 && k->density < 1.6);
        if (k->pitched < 0.3 && sustained) { k->role = k->centroid > 2500 || k->hiFrac > 0.08 ? Texture : Pad; k->why = k->role == Pad ? "long chords (several notes at once)" : "a sound surface, no clear notes"; continue; }
        if (sustained && k->rangeSt < 7) { k->role = automix::Pad; k->why = "long held notes"; continue; }
        bool repeated = k->density > 2.0 && k->regular < 0.45 && k->rangeSt < 6;
        if (repeated) { k->role = automix::Rhythm; k->why = "a repeated figure in time (" + juce::String(k->ioiMed > 0 ? beatSec / k->ioiMed : 0, 1) + " notes a beat)"; continue; }
        if (k->pitched < 0.3) { k->role = automix::Texture; k->why = "no clear pitch"; continue; }
        mel.push_back(k);
    }
    // the melody: the most singing part - clear pitch, moving, in the middle / top, present
    auto sal = [](Track* k) { return k->pitched * 2.0 + std::min(k->rangeSt, 14.0) / 7.0 + (k->f0med > 200 ? 0.5 : 0.0) + (k->lufs + 30) / 20.0 + (k->density > 0.6 && k->density < 7 ? 0.5 : 0.0); };
    std::sort(mel.begin(), mel.end(), [&](Track* a, Track* b) { return sal(a) > sal(b); });
    for (size_t i = 0; i < mel.size(); ++i) {
        mel[i]->role = i == 0 ? Lead : Counter;
        mel[i]->why = i == 0 ? "the most singing line (" + noteOf(mel[i]->f0lo) + U8("–") + noteOf(mel[i]->f0hi) + ")" : "a second line under the melody";
    }
    // every piece needs a focus: with no melody, the most moving pitched part leads
    if (mel.empty() && ts.size() > 1) {
        Track* best = nullptr; for (auto* k : ts) if ((k->role == Rhythm || k->role == Pad) && k->pitched > 0.3 && (best == nullptr || k->rangeSt > best->rangeSt)) best = k;
        if (best != nullptr && best->rangeSt >= 3) { best->role = automix::Lead; best->why = "leads (the most moving part)"; }
    }
    if (ts.size() == 1) { ts[0]->role = ts[0]->role == Drums ? Drums : Lead; ts[0]->why = "on its own"; }
    if (std::getenv("ARCO_DEBUG"))
        for (auto* k : ts) std::printf("        [track %d] %s: pitched %.2f f0 %.0f (%.0f-%.0f) range %.1f st, %.2f notes/s regular %.2f sustain %.2f crest %.1f hi %.3f low %.2f centroid %.0f wet %.2f s\n",
            k->t + 1, roleName(k->role), k->pitched, k->f0med, k->f0lo, k->f0hi, k->rangeSt, k->density, k->regular, k->sustain, k->crest, k->hiFrac, k->lowFrac, k->centroid, k->decaySec);
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

// ------------------------------------------------------------------ the EQ of one track
static void design(Track& k, const std::vector<Track*>& scene, int style, bool hasBass) {
    const double eqAmt = style == 0 ? 0.7 : (style == 2 ? 1.15 : 1.0);
    auto& b = k.bands; auto& m = k.mix;
    double mx = *std::max_element(b.begin(), b.end());
    // high-pass: below the lowest note it plays (never into it); with a bass in the piece the others leave the lows to it
    double lowNote = k.f0lo > 0 && k.pitched > 0.25 ? k.f0lo : 0;
    int lowB = 0; for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i] > mx - 30) { lowB = i; break; }
    double lowContent = analysis::bandFreq(lowB);
    double hpf;
    switch (k.role) {
        case Bass:  hpf = juce::jlimit(22.0, 40.0, (lowNote > 0 ? lowNote : lowContent) * 0.6); break;
        case Drums: hpf = k.lowFrac > 0.25 ? 28.0 : juce::jlimit(40.0, 160.0, lowContent * 0.8); break;
        default: {
            double base = lowNote > 0 ? lowNote * 0.72 : lowContent * 0.75;
            double floorHz = hasBass ? (k.role == Lead || k.role == Counter ? 70.0 : 100.0) : 35.0;
            hpf = juce::jlimit(30.0, 300.0, std::max(floorHz, base));
            if (lowNote > 0) hpf = std::min(hpf, lowNote * 0.8);          // never above its lowest note
        }
    }
    m.hp = (float) std::round(hpf);
    k.did.add("high-pass " + hz(m.hp) + (lowNote > 0 && k.role != Drums ? " (below its lowest note " + noteOf(lowNote) + ")" : juce::String()));
    int spread = 0; for (double v : b) if (v > mx - 24) ++spread;
    bool pure = spread < 6;
    // candidate bells: { freq, gain, q, score, reason }
    struct Bell { double f, g, q, score; juce::String why; };
    std::vector<Bell> cand;
    auto sm = smooth(b);
    // 1. masking: where a more important part is strong and so is this one, this one makes room
    for (int i = 0; i < (int) b.size(); ++i) {
        double f = analysis::bandFreq(i); if (f < 90 || f > 9000) continue;
        if (b[(size_t) i] < mx - 9) continue;
        for (auto* o : scene) {
            if (o == &k || o->muted) continue;
            if (priority(o->role, f) >= priority(k.role, f)) continue;
            double om = *std::max_element(o->bands.begin(), o->bands.end());
            double ov = o->bands[(size_t) i];
            if (ov < om - 8) continue;
            double over = 10.0 - std::abs((b[(size_t) i] - mx) - (ov - om));       // both near their loudest here
            if (over <= 0) continue;
            double g = -juce::jlimit(1.5, k.role == Pad || k.role == Texture ? 3.0 : 4.0, (1.5 + over * 0.25) * eqAmt);
            cand.push_back({ f, g, 1.4, 3.0 + over * 0.3, U8("room for the ") + roleName(o->role) + " at " + hz(f) });
        }
    }
    if (!pure) {
        // 2. mud
        double mud = bandAvg(b, 180, 500) - bandAvg(b, 800, 2500);
        bool broad = spread >= 12 || k.pitched < 0.5;
        if (k.role != Bass && mud > 3.0 && broad) {
            int pkB = 0; double pv = -1e9; for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f >= 180 && f <= 500 && b[(size_t) i] > pv) { pv = b[(size_t) i]; pkB = i; } }
            cand.push_back({ analysis::bandFreq(pkB), -juce::jlimit(1.5, 4.5, mud * 0.5 * eqAmt), 1.2, 2.0 + mud * 0.3, "less boxy" });
        }
        if (k.role == Bass && bandAvg(b, 200, 400) - bandAvg(b, 60, 150) > -3) cand.push_back({ 300, -2.5 * eqAmt, 1.2, 2.5, "a tighter low end" });
        // 3. a ringing resonance
        int resB = -1; double resX = 0;
        for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f < 150 || f > 8000) continue; double ex = b[(size_t) i] - sm[(size_t) i]; if (ex > resX) { resX = ex; resB = i; } }
        if (resB >= 0 && resX > 5.0 && k.pitched < 0.4) cand.push_back({ analysis::bandFreq(resB), -juce::jlimit(1.5, 5.0, (resX - 3.0) * 0.7 * eqAmt), 3.5, 2.5 + resX * 0.2, "a ringing note" });
        // 4. harshness
        double harsh = bandAvg(b, 2500, 5000) - bandAvg(b, 800, 2000);
        if (harsh > 3.0 && k.role != Bass) {
            int pkB = 0; double pv = -1e9; for (int i = 0; i < (int) b.size(); ++i) { double f = analysis::bandFreq(i); if (f >= 2500 && f <= 5000 && b[(size_t) i] > pv) { pv = b[(size_t) i]; pkB = i; } }
            cand.push_back({ analysis::bandFreq(pkB), -juce::jlimit(1.5, 4.0, harsh * 0.6 * eqAmt), 2.0, 2.0 + harsh * 0.3, "less harsh" });
        }
        // 5. presence: the melody speaks; the bass is heard on small speakers (its overtones)
        if (k.role == Lead && bandAvg(b, 2000, 5000) < bandAvg(b, 500, 1500) - 6) cand.push_back({ 3000, 2.0 * eqAmt, 0.9, 1.5, "clearer, in front" });
        if (k.role == Bass && bandAvg(b, 700, 1500) < bandAvg(b, 60, 150) - 25) cand.push_back({ 900, 2.0 * eqAmt, 1.0, 1.2, "heard on small speakers" });
    }
    // the two best bells, at least 2/3 of an octave apart
    std::sort(cand.begin(), cand.end(), [](const Bell& a, const Bell& c) { return a.score > c.score; });
    std::vector<Bell> pick;
    for (auto& c : cand) {
        bool clash = false; for (auto& p : pick) if (std::abs(std::log2(c.f / p.f)) < 0.67) clash = true;
        if (!clash) pick.push_back(c);
        if (pick.size() == 2) break;
    }
    if (pick.size() > 0) { m.pkf = (float) pick[0].f; m.pkg = (float) pick[0].g; m.pkq = (float) pick[0].q; k.did.add(sdb(m.pkg) + " at " + hz(m.pkf) + " (" + pick[0].why + ")"); }
    if (pick.size() > 1) { m.p2f = (float) pick[1].f; m.p2g = (float) pick[1].g; m.p2q = (float) pick[1].q; k.did.add(sdb(m.p2g) + " at " + hz(m.p2f) + " (" + pick[1].why + ")"); }
    if (pure && pick.empty()) k.did.add("a pure sound - EQ left open");
    // shelves: depth (back parts a little darker), air on the melody, no hiss on the bass, weight on a thin bass
    double air = bandAvg(b, 7000, 14000) - bandAvg(b, 800, 2500);
    if (k.role == Lead && air < -22) { m.hsg = (float) (2.0 * eqAmt); m.hsf = 9000; k.did.add(sdb(m.hsg) + " air above 9 kHz"); }
    else if ((k.role == Pad || k.role == Texture) && air > -14) { m.hsg = (float) (-2.0 * eqAmt); m.hsf = 7000; k.did.add(sdb(m.hsg) + U8(" above 7 kHz (sits behind)")); }
    else if (k.role == Rhythm && air > -10) { m.hsg = (float) (-1.5 * eqAmt); m.hsf = 8000; k.did.add(sdb(m.hsg) + U8(" above 8 kHz")); }
    else if (k.role == Bass && air > -28) { m.hsg = -3.0f; m.hsf = 5000; k.did.add(U8("−3 dB above 5 kHz (no hiss)")); }
    if (k.role == Bass && bandAvg(b, 40, 100) < bandAvg(b, 100, 250) - 6 && bandAvg(b, 40, 100) > mx - 30) { m.lsg = 2.5f; m.lsf = 90; k.did.add("+2.5 dB below 90 Hz (fuller)"); }
}

// ------------------------------------------------------------------ the compressor: a target gain reduction on the loud parts
static void compress(Track& k, int style, double sr) {
    auto& m = k.mix;
    double amt = style == 0 ? 0.6 : (style == 2 ? 1.4 : 1.0);
    struct P { double ratio, att, rel, gr; };
    P p;
    switch (k.role) {
        case Drums: p = { 4.0, 25, 110, 3.0 }; break;          // slow attack: the hits stay sharp
        case Bass: p = { 3.5, 15, 160, 4.0 }; break;           // an even bass
        case Lead: p = { 2.5, 12, 140, 3.0 }; break;           // a singing line keeps its phrasing
        case Counter: p = { 2.2, 14, 160, 2.5 }; break;
        case Rhythm: p = { 2.5, 15, 120, 2.5 }; break;
        default: p = { 1.6, 35, 300, 1.5 }; break;
    }
    if (k.st90 - k.st10 < 4 && k.crest < 9) { k.did.add("no compression needed (already even)"); return; }
    // the detector of the strip's compressor, run over the track: its 90th percentile is the "loud part"
    std::vector<double> lev;
    { float env = 0; float att = 1.0f - std::exp(-1.0f / (float) (p.att * 0.001 * sr)), rel = 1.0f - std::exp(-1.0f / (float) (p.rel * 0.001 * sr));
      const float* x = k.x->data(); for (long i = 0; i < k.len; ++i) { float a = std::abs(x[i]); env += (a > env ? att : rel) * (a - env); if ((i & 255) == 0 && env > 1e-5f) lev.push_back(db(env)); } }
    double loudPart = pct(lev, 0.9);
    double gr = p.gr * amt;
    double thr = loudPart - gr / (1.0 - 1.0 / p.ratio);
    m.cth = (float) juce::jlimit(-50.0, -3.0, thr);
    m.crt = (float) p.ratio; m.cat = (float) p.att; m.crl = (float) p.rel;
    k.did.add("compressor " + juce::String(m.crt, 1) + ":1, about " + juce::String(gr, 1) + " dB on the loud notes");
}

// how loud a track is after its EQ and compressor (by running them)
static double processedLufs(const Track& k, double sr, double* active = nullptr) {
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
    if (active) *active = activeLoudness(b, sr);
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
    const double sr = j.sr, beatSec = j.beatLen / sr, barSec = beatSec * j.beats;
    auto& O = j.opt;
    auto cur = [&](int sc, const std::string& id) -> double {
        auto& m = sc == 0 ? j.songVals : j.sceneVals[(size_t) sc - 1]; auto it = m.find(id); return it == m.end() ? 0.0 : it->second; };
    std::map<std::pair<int, std::string>, double> planned;
    auto plan = [&](int sc, const std::string& id, double v) { planned[{ sc, id }] = v; };
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
            if (progress && !progress(0.05f + 0.4f * (float) ++done / std::max(1, total))) return r;
        }
    if (tracks.empty()) { r.error = "The loops are silent."; return r; }
    std::vector<Track*> all; for (auto& k : tracks) all.push_back(k.get());

    // 2. the roles, scene by scene; then each part's EQ and compressor
    for (auto& S : j.scenes) {
        std::vector<Track*> ts; for (auto* k : all) if (k->scene == S.index && !k->muted) ts.push_back(k);
        decideRoles(ts, beatSec);
        bool hasBass = false; for (auto* k : ts) if (k->role == Bass) hasBass = true;
        for (auto* k : ts) {
            if (O.eq) design(*k, ts, O.style, hasBass);
            if (O.dynamics) compress(*k, O.style, sr);
            if (O.eq || O.dynamics) k->procLufs = processedLufs(*k, sr, &k->active);
            else { k->procLufs = k->lufs; juce::AudioBuffer<float> b(1, (int) k->len); b.copyFrom(0, 0, k->x->data(), (int) k->len); k->active = activeLoudness(b, sr); }
        }
    }
    if (progress && !progress(0.55f)) return r;

    // 3. per scene: levels, panning, the room, timing
    double densest = 0; Track* mainLead = nullptr;
    for (auto* k : all) if (!k->muted && k->role == Lead && (mainLead == nullptr || k->procLufs > mainLead->procLufs)) mainLead = k;
    for (auto* k : all) if (!k->muted) densest = std::max(densest, k->density);
    for (auto& S : j.scenes) {
        std::vector<Track*> ts; for (auto* k : all) if (k->scene == S.index && !k->muted) ts.push_back(k);
        if (ts.empty()) continue;
        // levels: how loud each part sounds while it plays, measured after its EQ and compressor, balanced like an
        // engineer balances an ensemble - every voice heard, the focus a little in front (never by 8-10 dB as before).
        // Then: a part covered by louder parts in its own register comes up a little; your wishes on top.
        if (O.levels) {
            auto target = [&](Role ro) {
                switch (O.focus) {
                    case 1:  { static const double T[7] = { -2.0, -2.0, -3.5, 0.0, -2.0, -4.5, -6.0 }; return T[ro]; }   // melody in front
                    case 2:  { static const double T[7] = { -0.5, 0.0, -2.0, -1.0, -2.5, -4.0, -6.0 }; return T[ro]; }    // groove
                    default: { static const double T[7] = { -1.0, -1.0, -1.5, 0.0, -0.5, -2.0, -3.5 }; return T[ro]; }    // together
                } };
            std::map<Track*, double> tgt;
            for (auto* k : ts) tgt[k] = (ts.size() == 1 ? 0.0 : target(k->role)) + O.roleDb[(size_t) k->role] + O.trackDb[(size_t) k->t];
            // first pass: every part to its target (relative to a common reference)
            for (auto* k : ts) k->gainDb = tgt[k] - k->active;
            // masking: of its main band, how much is covered by louder parts? up to +1.5 dB to be heard
            for (auto* k : ts) {
                double mx = *std::max_element(k->bands.begin(), k->bands.end());
                int main = 0, covered = 0;
                for (int i = 0; i < analysis::NBANDS; ++i) {
                    double f = analysis::bandFreq(i); if (f < 150 || f > 6000 || k->bands[(size_t) i] < mx - 6) continue;
                    ++main; double mine = k->bands[(size_t) i] + k->gainDb;
                    for (auto* o : ts) if (o != k && o->bands[(size_t) i] + o->gainDb > mine + 3) { ++covered; break; }
                }
                if (main > 0 && covered > 0) { double up = 1.5 * covered / main; k->gainDb += up; if (up >= 0.5) k->why << (k->why.isEmpty() ? "" : U8(" · ")) << "partly covered by louder parts: +" << juce::String(up, 1) << " dB"; }
            }
            // the reference: the highest fader at 0 dB; if that would pull a part below -24 dB (one recorded far louder
            // than the others), the faders go up instead, up to +6 dB - the balance itself never changes
            double top = -1e9, bot = 1e9; for (auto* k : ts) { top = std::max(top, k->gainDb); bot = std::min(bot, k->gainDb); }
            double ref = top - juce::jlimit(0.0, 6.0, (top - bot) - 24.0);
            for (auto* k : ts) k->gainDb = juce::jlimit(-30.0, 6.0, k->gainDb - ref);
            for (auto* k : ts) {
                k->vol = dbToVol(juce::jlimit(-30.0, 6.0, k->gainDb));
                double was = cur(S.index, "vol" + std::to_string(k->strip));
                if (std::abs(k->vol - was) >= 0.5) k->did.insert(0, "level " + sdb(volToDb(k->vol) - volToDb(was)));
                plan(S.index, "vol" + std::to_string(k->strip), k->vol);
            }
            {   // the balance, before and after (the loudest = 0)
                double b0 = -1e9, a0 = -1e9;
                for (auto* k : ts) { b0 = std::max(b0, k->rawActive + volToDb(cur(S.index, "vol" + std::to_string(k->strip)))); a0 = std::max(a0, k->active + k->gainDb); }
                for (auto* k : ts) r.levels.push_back({ S.index, k->t, juce::String(roleName(k->role)), k->rawActive + volToDb(cur(S.index, "vol" + std::to_string(k->strip))) - b0, k->active + k->gainDb - a0 });
            }
            if (std::getenv("ARCO_DEBUG"))
                for (auto* k : ts) std::printf("        [level] track %d %-14s active %.1f -> %.1f LU (target %+.1f, fader %+.1f dB)\n", k->t + 1, roleName(k->role), k->active, k->active + k->gainDb, tgt[k], k->gainDb);
        }
        // panning: bass, kick and melody in the centre; the others spread so both sides weigh the same, and the parts
        // that share the most frequencies go to opposite sides
        if (O.pan) {
            std::vector<Track*> side;
            for (auto* k : ts) {
                bool centre = k->role == Bass || k->role == Lead || (k->role == Drums && k->lowFrac > 0.3);
                if (centre) k->pan = 0; else side.push_back(k);
            }
            auto prio = [](Track* k) { static const int P[7] = { 0, 1, 3, 0, 2, 4, 5 }; return P[k->role]; };
            std::sort(side.begin(), side.end(), [&](Track* a, Track* b) { return prio(a) != prio(b) ? prio(a) < prio(b) : a->procLufs > b->procLufs; });
            auto overlap = [](Track* a, Track* b) { double s = 0; for (int i = 0; i < analysis::NBANDS; ++i) { double f = analysis::bandFreq(i); if (f < 150 || f > 8000) continue;
                                                       double x = a->bands[(size_t) i], y = b->bands[(size_t) i]; s += std::max(0.0, 12.0 - std::abs(x - y)) * std::pow(10.0, std::min(x, y) / 20.0); } return s; };
            double weightL = 0, weightR = 0;
            std::vector<Track*> placed;
            for (auto* k : side) {
                double w = k->role == Pad ? 38 : k->role == Texture ? 45 : k->role == Rhythm ? 32 : k->role == Counter ? 25 : 20;
                if (side.size() == 1) w *= 0.5;                       // alone on a side: not too far (the mix stays balanced)
                w = std::min(50.0, w * (O.width < 0 ? 0.6 : O.width > 0 ? 1.35 : 1.0));
                // the side away from the part it clashes with most; otherwise the lighter side
                Track* clash = nullptr; double cmax = 0; for (auto* o : placed) { double ov = overlap(k, o); if (ov > cmax) { cmax = ov; clash = o; } }
                int dir = clash != nullptr && std::abs(clash->pan) > 1 ? (clash->pan > 0 ? -1 : 1) : (weightL <= weightR ? -1 : 1);
                k->pan = dir * w;
                double wt = std::pow(10.0, (k->procLufs + volToDb(k->vol)) / 20.0);
                (dir < 0 ? weightL : weightR) += wt;
                placed.push_back(k);
            }
            for (auto* k : ts) {
                plan(S.index, "pan" + std::to_string(k->strip), k->pan);
                k->did.add(std::abs(k->pan) < 1 ? juce::String("centre") : (k->pan < 0 ? "left " : "right ") + juce::String((int) std::abs(k->pan * 2)) + " %");
            }
        }
        // the mixing desk settings
        for (auto* k : ts) {
            auto& F = mixFields();
            for (int f = 0; f < (int) F.size(); ++f) {
                float v = mixField(k->mix, f);
                if (O.eq || O.dynamics || std::abs(cur(S.index, "mx_" + F[(size_t) f] + std::to_string(k->strip)) - v) > 1e-4)
                    plan(S.index, "mx_" + F[(size_t) f] + std::to_string(k->strip), v);
            }
        }
        // ONE ROOM: every part in the same SPACE, at its own depth; parts that already carry their own room get less
        if (O.effects) {
            double drySec = 1e9; for (auto* k : ts) if (k->decaySec > 0) drySec = std::min(drySec, k->decaySec);
            for (auto* k : ts) {
                // no space under the bass, little on drums
                for (auto& key : ambientSends()) {
                    if (key == "spc") continue;
                    auto p = Model::stripParam(k->strip, key); if (p.empty()) continue;
                    double v = cur(S.index, p), nv = v;
                    if (k->role == Bass) nv = std::min(v, 3.0);
                    else if (k->role == Drums && (key == "dly" || key == "png" || key == "rev")) nv = std::min(v, 15.0);
                    if (nv != v) { plan(S.index, p, nv); k->did.add(juce::String(key == "dly" ? "DELAY" : juce::String(key).toUpperCase()) + " send " + juce::String((int) v) + U8(" → ") + juce::String((int) nv)); }
                }
                if (!inPlay(S.index, "spc") && !O.allScenes && j.visible.size() > 0 && !j.visible.contains("spc")) {}
                auto p = Model::stripParam(k->strip, "spc"); if (p.empty()) continue;
                double depth;
                switch (k->role) { case Bass: depth = 0; break; case Drums: depth = 8; break; case Lead: depth = 15; break; case Counter: depth = 20; break;
                                   case Rhythm: depth = 18; break; case Pad: depth = 30; break; default: depth = 34; }
                depth *= O.style == 0 ? 1.2 : O.style == 2 ? 0.75 : 1.0;
                depth *= O.space == 0 ? 0.45 : O.space == 2 ? 1.6 : 1.0;
                juce::String note;
                // its own reverb (recorded in a room): less of ours, so all of them sound like one room
                if (k->decaySec > 0 && drySec < 1e8 && k->decaySec > drySec * 1.6 && k->decaySec > 0.8) {
                    double f = juce::jlimit(0.3, 1.0, drySec * 1.6 / k->decaySec);
                    depth *= f; note = " - it already has its own room (" + juce::String(k->decaySec, 1) + " s), so less";
                }
                double was = cur(S.index, p);
                if (std::abs(was - depth) >= 1) { plan(S.index, p, depth); k->did.add("SPACE " + juce::String((int) std::round(depth)) + note + (depth > 0 ? " (one room for all)" : " (a clean low end)")); }
            }
            // the other effects send their echoes into the same room
            static const std::vector<std::pair<std::string, std::string>> toRoom = { { "dly", "dly_sp" }, { "rev", "rev_sp" }, { "cho", "cho_sp" }, { "fxb", "fxb_sp" }, { "shd", "shd_sp" },
                { "halo", "halo_sp" }, { "res", "res_sp" }, { "grn", "grn_sp" }, { "frz", "frz_sp" }, { "orb", "orb_sp" } };
            for (auto& tr : toRoom) {
                if (!usedFx(S.index, tr.first)) continue;
                double v = cur(S.index, tr.second), nv = juce::jlimit(25.0, 60.0, v);
                if (std::abs(nv - v) >= 1) { plan(S.index, tr.second, nv); r.report.push_back({ juce::String(tr.first).toUpperCase(), "into the room", U8("→ SPACE ") + juce::String((int) v) + U8(" → ") + juce::String((int) nv) + " (its echoes in the same room)", -1, S.index }); }
            }
            // DELAY on the melody's rhythm
            Track* lead = nullptr; for (auto* k : ts) if (k->role == Lead) lead = k;
            if (lead != nullptr && (inPlay(S.index, "dly") || O.style != 0)) {
                double ioiB = lead->ioiMed > 0 ? lead->ioiMed / beatSec : 1.0;
                int idx; juce::String nm; double fbWant;
                if (ioiB >= 0.9) { idx = 3; nm = "dotted 1/8"; }          // long notes: the classic dotted eighth answers between them
                else if (ioiB >= 0.45) { idx = 4; nm = "1/4"; }            // eighth-note lines: quarter-note echoes
                else { idx = 4; nm = "1/4"; }                              // busy lines: quarters, few repeats
                double delaySec = beatSec * (idx == 3 ? 0.75 : 1.0);
                double repeats = std::max(1.0, (ioiB >= 0.45 ? barSec : barSec * 0.5) / delaySec);
                fbWant = juce::jlimit(18.0, 58.0, 100.0 * std::pow(10.0, -30.0 / 20.0 / repeats));
                plan(S.index == j.scenes[0].index ? 0 : 0, "dly_time", (idx + 0.5) / 8.0 * 100.0);
                plan(0, "dly_fb", fbWant); plan(0, "dly_tone", 42);
                plan(S.index, "dly_sp", 35);
                double send = (lead->density > 4 ? 10 : (lead->density > 2 ? 16 : 22)) * (O.space == 0 ? 0.6 : O.space == 2 ? 1.2 : 1.0);
                auto p = Model::stripParam(lead->strip, "dly");
                if (!p.empty()) { plan(S.index, p, send); lead->did.add("DELAY " + nm + " (" + juce::String((int) send) + "), echoes fade within " + (ioiB >= 0.45 ? "a bar" : "half a bar") + ", darker, into the room"); }
                for (auto* k : ts) if (k != lead && k->role != Counter) { auto q = Model::stripParam(k->strip, "dly"); if (!q.empty() && cur(S.index, q) > 12) { plan(S.index, q, 8); k->did.add(U8("DELAY send → 8 (the echoes belong to the melody)")); } }
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
        if (progress && !progress(0.55f + 0.2f * (float) (&S - &j.scenes[0] + 1) / (float) j.scenes.size())) return r;
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

    // 5. effects in tempo, and the room's size
    juce::StringArray fxLines;
    if (O.effects) {
        auto snapRate = [&](const char* id, const char* name, std::function<double(double)> toHz, std::function<double(double)> toKnob, bool used) {
            if (!used) return;
            double v = cur(0, id), f = toHz(v);
            if (f <= 0) return;
            double best = 0, bestErr = 1e9; juce::String bestName;
            const double mult[6] = { 0.5, 1, 2, 4, 8, 16 }; const char* nm[6] = { "every half beat", "every beat", "every 2 beats", "once a bar", "every 2 bars", "every 4 bars" };
            for (int i = 0; i < 6; ++i) { double fh = 1.0 / (beatSec * mult[i] * (i >= 3 ? j.beats / 4.0 : 1.0)); double e = std::abs(std::log(f / fh)); if (e < bestErr) { bestErr = e; best = fh; bestName = nm[i]; } }
            if (bestErr < std::log(1.25) && bestErr > std::log(1.005)) {
                plan(0, id, juce::jlimit(0.0, 100.0, toKnob(best)));
                fxLines.add(juce::String(name) + " now moves " + bestName + " (was " + juce::String(f, 2) + " Hz)");
            }
        };
        auto usedAny = [&](const std::string& key) { for (auto& S : j.scenes) if (usedFx(S.index, key)) return true; return false; };
        snapRate("phs_rate", "PHASER", [](double v) { return 0.03 * std::pow(100.0, v / 100.0); }, [](double f) { return 100.0 * std::log(f / 0.03) / std::log(100.0); }, usedAny("phs"));
        snapRate("cho_rate", "CHORUS", [](double v) { return 0.05 * std::pow(60.0, v / 100.0); }, [](double f) { return 100.0 * std::log(f / 0.05) / std::log(60.0); }, usedAny("cho"));
        if (usedAny("flg") && cur(0, "flg_rate") < 50) {
            double v = cur(0, "flg_rate"), f = 0.03 + v / 100.0 * 2.4, bar = 1.0 / barSec;
            const double fh[3] = { bar / 2, bar, bar * 2 }; const double kn[3] = { 58, 75, 92 }; const char* nm[3] = { "every 2 bars", "once a bar", "twice a bar" };
            for (int i = 0; i < 3; ++i) if (std::abs(std::log(f / fh[i])) < std::log(1.3)) { plan(0, "flg_rate", kn[i]); fxLines.add(juce::String("FLANGER now sweeps ") + nm[i] + ", locked to the tempo"); break; }
        }
        // the room: its decay breathes with the music - shorter for busy music, about a bar for slow, open music
        bool spcUsed = false; for (auto& kv : planned) if (kv.first.second.rfind("spc", 0) == 0 && kv.second > 0.5) spcUsed = true;
        if (spcUsed || usedAny("spc")) {
            double want = densest > 5 ? beatSec * 2.0 : (densest > 2.5 ? beatSec * 3.0 : barSec);
            want *= O.space == 0 ? 0.65 : O.space == 2 ? 1.5 : 1.0;
            want = juce::jlimit(0.9, O.space == 2 ? 4.8 : 3.6, want);
            double knob = 100.0 * std::sqrt(std::max(0.0, (want - 0.4) / 24.0));
            plan(0, "spc_decay", knob);
            double tone = densest > 4 ? 40 : 50;                        // a darker room for busy music: clearer
            plan(0, "spc_tone", tone);
            plan(0, "spc_shim", std::min(cur(0, "spc_shim"), 15.0));    // a natural room (shimmer stays an effect)
            fxLines.add("SPACE: one room, decay " + juce::String(want, 1) + " s (" + (want >= barSec * 0.9 ? "about a bar" : juce::String(want / beatSec, 1) + " beats") + "), " + (tone < 45 ? "darker so it stays clear" : "natural tone"));
        }
    }
    if (!fxLines.isEmpty()) r.report.push_back({ "EFFECTS", "in tempo, one room", fxLines.joinIntoString("  ·  "), -1, 0 });

    // 6. the master: tonal balance, glue, loudness - rendered and measured
    if (O.master) {
        double glue = O.style == 0 ? 15 : O.style == 2 ? 40 : 25;
        plan(0, "glue", glue);
        double target = (O.style == 0 ? -16.0 : O.style == 2 ? -11.5 : -14.0) + O.loud * 2.0;
        auto render = [&](double gain, double lsg, double hsg, bool lim, double from, double to, juce::AudioBuffer<float>& mix, double& rate) {
            RenderSnapshot snap = j.snap;
            juce::var song = juce::JSON::parse(juce::JSON::toString(snap.song));
            if (auto* so = song["song"].getDynamicObject()) for (auto& kv : planned) if (kv.first.first == 0) so->setProperty(juce::Identifier(kv.first.second), kv.second);
            if (auto* arr = song["scenes"].getArray())
                for (auto& kv : planned) if (kv.first.first >= 1 && kv.first.first <= arr->size())
                    if (auto* o = (*arr)[kv.first.first - 1].getDynamicObject()) o->setProperty(juce::Identifier(kv.first.second), kv.second);
            if (auto* so = song["song"].getDynamicObject()) { so->setProperty("ms_gain", gain); so->setProperty("ms_lim", lim ? 1 : 0); so->setProperty("ms_lsg", lsg); so->setProperty("ms_hsg", hsg); so->setProperty("ms_ceil", -1.0); }
            snap.song = song;
            int sc = j.scenes[0].index; for (auto& S : j.scenes) if (S.index == snap.sceneNow) sc = S.index;
            snap.sceneNow = sc;
            ExportOptions eo; eo.what = ExportOptions::CurrentScene; eo.repeats = 2; eo.tailSeconds = 0;
            rate = sr;
            return Renderer(*j.host).renderMix(snap, eo, mix, rate, -1, [&](float p) { return !progress || progress((float) (from + (to - from) * p)); });
        };
        juce::AudioBuffer<float> mix; double rate = sr;
        if (render(0.0, 0.0, 0.0, false, 0.78, 0.86, mix, rate) && mix.getNumSamples() > 0) {
            double lu = loud::integrated(mix, rate);
            r.lufsBefore = lu;
            // the overall colour against a reference: well-balanced mixes fall about 4.5 dB per octave from the low mids up
            std::vector<float> mono((size_t) mix.getNumSamples());
            for (int i = 0; i < mix.getNumSamples(); ++i) mono[(size_t) i] = 0.5f * (mix.getSample(0, i) + mix.getSample(1, i));
            auto bb = analysis::bands(mono.data(), (int) mono.size(), rate);
            auto rel = [&](double f0, double f1) { double ref = bandAvg(bb, 700, 1400); double fc = std::sqrt(f0 * f1); return bandAvg(bb, f0, f1) - ref + 4.5 * std::log2(fc / 1000.0); };
            double lowDev = rel(50, 160), highDev = rel(5000, 12000);
            double lsg = juce::jlimit(-3.0, 3.0, -lowDev * 0.4), hsg = juce::jlimit(-3.0, 3.0, -highDev * 0.4);
            if (O.tone < 0) { lsg += 1.0; hsg -= 1.5; } else if (O.tone > 0) { lsg -= 0.5; hsg += 1.5; }
            lsg = juce::jlimit(-3.5, 3.5, lsg); hsg = juce::jlimit(-3.5, 3.5, hsg);
            if (std::abs(lsg) < 0.5) lsg = 0; if (std::abs(hsg) < 0.5) hsg = 0;
            // how hard the limiter may work: a mastering engineer stops before the music loses its breath.
            // The loud moments (the top of 10 ms peaks, not single clicks) may be held down by at most 2 / 3.5 / 5.5 dB.
            double loudPk = -100;
            { std::vector<double> pk; int w = (int) (0.01 * rate);
              for (int a = 0; a + w <= mix.getNumSamples(); a += w) { float m2 = 0; for (int c = 0; c < mix.getNumChannels(); ++c) m2 = std::max(m2, mix.getMagnitude(c, a, w)); if (m2 > 1e-6f) pk.push_back(db(m2)); }
              loudPk = pct(pk, 0.995); }
            double allowGR = O.style == 0 ? 2.0 : O.style == 2 ? 5.5 : 3.5;
            double maxGain = -1.0 + allowGR - loudPk;
            bool held = false;
            if (target - lu > maxGain) { target = lu + maxGain; held = true; }
            double gain = juce::jlimit(-12.0, 18.0, target - lu);
            // measure again through the limiter and correct (the limiter takes a little loudness)
            double measured = lu + gain;
            for (int pass = 0; pass < 2; ++pass) {
                juce::AudioBuffer<float> m2; double r2 = sr;
                if (!render(gain, lsg, hsg, true, 0.86 + pass * 0.06, 0.92 + pass * 0.06, m2, r2) || m2.getNumSamples() == 0) break;
                measured = loud::integrated(m2, r2);
                if (std::abs(measured - target) < 0.3) break;
                gain = juce::jlimit(-12.0, 18.0, gain + (target - measured));
            }
            plan(0, "ms_gain", gain); plan(0, "ms_lim", 1); plan(0, "ms_ceil", -1.0); plan(0, "ms_lsg", lsg); plan(0, "ms_hsg", hsg);
            r.lufsAfter = measured;
            juce::String w = juce::String(lu, 1) + U8(" → ") + juce::String(measured, 1) + " LUFS measured (" + (held ? juce::String("as loud as it can be without squashing it") : juce::String(O.style == 0 ? "natural" : O.style == 2 ? "loud" : "streaming") + " level") + "), peaks under "
                             + U8("−1 dB") + ", glue " + juce::String((int) glue);
            if (lsg != 0) w << ", lows " << sdb(lsg);
            if (hsg != 0) w << ", highs " << sdb(hsg);
            w << " (tonal balance)";
            r.report.push_back({ "MASTER", "the whole mix", w, -1, 0 });
        }
    }

    // 7. the report: the tracks first (what each one is and why), then the live inputs, the effects, the master
    {
        std::vector<AutoMixResult::Line> lines;
        for (auto* k : all) {
            juce::String who = "TRACK " + juce::String(k->t + 1) + (j.scenes.size() > 1 ? U8(" · scene ") + LL[k->scene - 1] : juce::String());
            if (k->muted) lines.push_back({ who, "muted", "left as it is", k->strip, k->scene });
            else lines.push_back({ who, juce::String(roleName(k->role)), (k->why.isNotEmpty() ? k->why + U8("  ·  ") : juce::String()) + k->did.joinIntoString(U8("  ·  ")), k->strip, k->scene });
        }
        r.report.insert(r.report.begin(), lines.begin(), lines.end());
    }
    for (auto& kv : planned) {
        double before = cur(kv.first.first, kv.first.second);
        if (std::abs(before - kv.second) > 1e-6) r.changes.push_back({ kv.first.first, kv.first.second, before, kv.second });
    }
    int nt = 0; for (auto* k : all) if (!k->muted) ++nt;
    juce::StringArray roles; for (auto* k : all) if (!k->muted) roles.addIfNotAlreadyThere(roleName(k->role));
    r.headline = juce::String(nt) + (nt == 1 ? " track" : " tracks") + " mixed (" + roles.joinIntoString(", ") + ")" + (r.audio.empty() ? juce::String() : ", " + juce::String((int) r.audio.size()) + " tightened")
                 + (O.master && r.lufsBefore > -69 ? U8(" · master ") + juce::String(r.lufsAfter, 1) + " LUFS" : juce::String());
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

// ------------------------------------------------------------------ what you want, in your words
// Simple and predictable: the sentence is split into parts ("warm and spacious, track 2 louder"); each part is either
// about one track / one kind of part (with louder / quieter), or about the whole mix. English, Portuguese, German.
void parseWish(const juce::String& wishIn, AutoMixOptions& o, juce::StringArray& understood) {
    auto U = [](const char* t) { return juce::String::fromUTF8(t); };
    juce::String w = wishIn.toLowerCase();
    for (auto pr : { std::pair<const char*, const char*> { "ã", "a" }, { "á", "a" }, { "â", "a" }, { "à", "a" }, { "é", "e" }, { "ê", "e" }, { "í", "i" }, { "ó", "o" }, { "ô", "o" }, { "õ", "o" }, { "ú", "u" }, { "ç", "c" }, { "ä", "a" }, { "ö", "o" }, { "ü", "u" }, { "ß", "ss" } })
        w = w.replace(U(pr.first), pr.second);
    for (auto sep : { " and ", " but ", " e ", " und ", " aber ", " mas ", ";", ".", "!", "\n" }) w = w.replace(sep, ",");
    auto parts = juce::StringArray::fromTokens(w, ",", "");
    auto has = [](const juce::String& c, std::initializer_list<const char*> ws) { for (auto* x : ws) if ((" " + c + " ").contains(juce::String(" ") + x) ) return true; return false; };
    static const char* roleWord[7] = { "BASS", "DRUMS", "RHYTHM", "MELODY", "SECOND MELODY", "CHORDS / PADS", "TEXTURE" };
    for (auto c : parts) {
        c = c.trim(); if (c.isEmpty()) continue;
        bool up = has(c, { "louder", "more ", "up", "front", "forward", "boost", "stronger", "bigger", "lauter", "mehr", "vorne", "mais", "alto", "frente", "+" });
        bool down = has(c, { "quieter", "softer", "less", "down", "back", "behind", "lower", "smaller", "weaker", "leiser", "weniger", "hinten", "menos", "mais baixo", "muito baixo", "atras", "tras", "-" });
        if (has(c, { "too loud", "zu laut", "muito alto", "demasiado alto" })) { up = false; down = true; }
        if (has(c, { "too quiet", "too soft", "zu leise", "muito baixo" })) { up = true; down = false; }
        double amt = has(c, { "a bit", "slightly", "a little", "little", "etwas", "ein bisschen", "um pouco", "pouco" }) ? 1.5 : has(c, { "much", "a lot", "very", "way", "viel", "sehr", "muito" }) ? 4.5 : 3.0;
        // a track?
        int trackN = -1;
        { auto tk = juce::StringArray::fromTokens(c, " ", "");
          for (int i = 0; i < tk.size(); ++i) {
              auto t = tk[i];
              bool word = t.startsWith("track") || t.startsWith("pista") || t.startsWith("spur") || t.startsWith("loop") || t.startsWith("faixa") || t.startsWith("t");
              int n = t.retainCharacters("0123456789").getIntValue();
              if (word && n >= 1 && n <= 8) { trackN = n; break; }
              if (n >= 1 && n <= 8 && t.containsOnly("0123456789") && i > 0 && (tk[i - 1].startsWith("track") || tk[i - 1].startsWith("pista") || tk[i - 1].startsWith("spur") || tk[i - 1].startsWith("faixa") || tk[i - 1].startsWith("loop"))) { trackN = n; break; }
          } }
        int role = -1;
        if (has(c, { "second melody", "counter", "segunda", "gegenstimme" })) role = automix::Counter;
        else if (has(c, { "bass", "contrabaixo" }) || (has(c, { "baixo" }) && !has(c, { "mais baixo", "muito baixo" }))) role = automix::Bass;
        else if (has(c, { "drum", "percussion", "beat", "bateria", "percuss", "schlagzeug", "trommel" })) role = automix::Drums;
        else if (has(c, { "melody", "melodie", "melodia", "lead", "solo", "tune" })) role = automix::Lead;
        else if (has(c, { "chord", "pad", "harmony", "acorde", "akkord", "harmonie", "harmonia" })) role = automix::Pad;
        else if (has(c, { "texture", "textur", "textura", "noise" })) role = automix::Texture;
        else if (has(c, { "rhythm", "ritmo", "rhythmus" }) && (up || down)) role = automix::Rhythm;
        if ((trackN > 0 || role >= 0) && (up || down)) {
            double d = (up ? amt : -amt);
            if (trackN > 0) { o.trackDb[(size_t) trackN - 1] += (float) d; understood.add("TRACK " + juce::String(trackN) + " " + (d > 0 ? "+" : U("−")) + juce::String(std::abs(d), 1) + " dB"); }
            else { o.roleDb[(size_t) role] += (float) d; understood.add(juce::String(roleWord[role]) + " " + (d > 0 ? "+" : U("−")) + juce::String(std::abs(d), 1) + " dB"); }
            continue;
        }
        if (role == automix::Lead && has(c, { "front", "solo", "vorne", "frente" })) { o.focus = 1; understood.add("MELODY IN FRONT"); continue; }
        // an instrument by name: ARCO does not know which track it is - ask
        if ((up || down) && has(c, { "cello", "violin", "viola", "flute", "voice", "vocal", "piano", "guitar", "sax", "trumpet", "clarinet", "oboe", "geige", "flote", "stimme", "klavier", "violino", "flauta", "voz", "violoncelo", "guitarra", "cello" })) {
            understood.add("? \"" + c + U("\" - which track is it? e.g. 'track 2 louder'")); continue; }
        bool any = false;
        auto say = [&](const juce::String& t) { understood.addIfNotAlreadyThere(t); any = true; };
        if (has(c, { "together", "equal", "balanced", "ensemble", "blend", "every voice", "all voices", "juntos", "igual", "equilibr", "zusammen", "ausgewogen", "gleich" })) { o.focus = 0; say("EVERY VOICE TOGETHER"); }
        if (has(c, { "melody in front", "solo", "lead in front", "melodia", "melodie vorne" })) { o.focus = 1; say("MELODY IN FRONT"); }
        if (has(c, { "groove", "dance", "danc", "tanz" })) { o.focus = 2; say("GROOVE"); }
        if (has(c, { "natural", "acoustic", "classical", "classic", "organic", "naturlich", "akustisch", "klassisch", "acustic", "classico" })) { o.style = 0; say("NATURAL"); }
        if (has(c, { "punch", "powerful", "energ", "pop", "aggressive", "kraftig", "forte" })) { o.style = 2; say("PUNCHY"); }
        bool rev = has(c, { "reverb", "space", "room", "hall", "echo", "hall", "raum", "espaco", "sala", "eco" });
        if (has(c, { "dry", "intimate", "close", "trocken", "seco", "intim" }) || (rev && down) || has(c, { "no reverb", "kein hall", "sem reverb" })) { o.space = 0; say("DRY, CLOSE"); }
        else if (has(c, { "spacious", "big room", "church", "cathedral", "ambient", "wet", "airy", "kirche", "igreja", "weit" }) || (rev && up)) { o.space = 2; say("MORE SPACE (HALL)"); }
        else if (rev) { o.space = 1; say("ONE ROOM"); }
        if (has(c, { "warm", "dark", "mellow", "smooth", "soft", "round", "quente", "suave", "weich", "dunkel" })) { o.tone = -1; say("WARMER"); }
        if (has(c, { "bright", "clear", "crisp", "air", "brilliant", "sparkl", "hell", "klar", "brilh", "claro", "limpo" })) { o.tone = 1; say("BRIGHTER"); }
        if (has(c, { "wide", "wider", "broad", "stereo", "breit", "largo", "amplo" })) { o.width = 1; say("WIDER"); }
        if (has(c, { "narrow", "mono", "centred", "centered", "schmal", "estreito" })) { o.width = -1; say("NARROWER"); }
        if (has(c, { "tight", "on the beat", "in time", "precise", "exact", "genau", "preciso" })) { o.timing = true; o.timingAmount = 0.6f; say("TIGHTER TIMING"); }
        if (has(c, { "loose", "human", "leave the timing", "no timing", "don't move", "dont move", "frei", "humano" })) { o.timing = false; say("TIMING AS PLAYED"); }
        if (!rev && (has(c, { "louder", "loud", "lauter", "mais alto", "volume up" }) && !has(c, { "too loud" }))) { o.loud = 1; say("LOUDER MASTER"); }
        if (!rev && has(c, { "quieter", "quiet", "softer master", "leiser", "mais baixo", "dynamic", "dynamik", "dinamic", "breath" })) { o.loud = -1; o.style = juce::jmin(o.style, 1); say("MORE DYNAMIC (QUIETER)"); }
        if (!any) {
            // an instrument we cannot place
            if (has(c, { "cello", "violin", "viola", "flute", "voice", "vocal", "piano", "guitar", "sax", "trumpet", "geige", "flote", "stimme", "violino", "flauta", "voz", "violoncelo", "guitarra" }))
                understood.add("? \"" + c + U("\" - which track is it? e.g. 'track 2 louder'"));
            else understood.add("? \"" + c + "\"");
        }
    }
}

} // namespace arco
