// ARCO 1.2: tempo, key, onsets, and the high-quality stretch. (This file is built without -ffast-math: see CMakeLists.txt.)
#include "Analysis.h"
#include "../Engine/Engine.h"
#include "../ThirdParty/signalsmith/signalsmith-stretch.h"
#include <complex>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <array>

namespace arco {
namespace analysis {

static const char* NN[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* noteName(int pc) { return NN[((pc % 12) + 12) % 12]; }
juce::String Key::name() const { return tonic < 0 ? juce::String("no clear key") : juce::String(noteName(tonic)) + (minor ? " minor" : " major"); }

// ------------------------------------------------------------------ a small radix-2 FFT (magnitudes only are needed)
struct FFT {
    int n = 0; std::vector<std::complex<float>> w; std::vector<int> rev; std::vector<std::complex<float>> buf;
    explicit FFT(int size) : n(size), w((size_t) size / 2), rev((size_t) size), buf((size_t) size) {
        for (int i = 0; i < n / 2; ++i) w[(size_t) i] = std::polar(1.0f, (float) (-2.0 * M_PI * i / n));
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
    }
    // in: n real samples (windowed); out: n/2+1 magnitudes
    void mags(const float* in, float* out) {
        for (int i = 0; i < n; ++i) buf[(size_t) rev[(size_t) i]] = { in[i], 0.0f };
        for (int len = 2; len <= n; len <<= 1) {
            int step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < len / 2; ++j) {
                    auto u = buf[(size_t) (i + j)], v = buf[(size_t) (i + j + len / 2)] * w[(size_t) (j * step)];
                    buf[(size_t) (i + j)] = u + v; buf[(size_t) (i + j + len / 2)] = u - v;
                }
        }
        for (int i = 0; i <= n / 2; ++i) out[i] = std::abs(buf[(size_t) i]);
    }
};
static std::vector<float> hann(int n) { std::vector<float> h((size_t) n); for (int i = 0; i < n; ++i) h[(size_t) i] = 0.5f - 0.5f * (float) std::cos(2 * M_PI * i / n); return h; }

// ------------------------------------------------------------------ onset strength: log spectral flux, ~100 values per second
Envelope onsetEnvelope(const float* x, int n, double sr) {
    Envelope e; e.sr = sr;
    int N = sr > 60000 ? 4096 : 2048;
    int hop = (int) std::round(sr / 100.0); e.hop = hop;
    if (n < N) return e;
    FFT fft(N); auto win = hann(N);
    std::vector<float> fr((size_t) N), mag((size_t) (N / 2 + 1)), prev((size_t) (N / 2 + 1), 0.0f), prevLin((size_t) (N / 2 + 1), 0.0f);
    int b0 = (int) (30.0 * N / sr), b1 = juce::jmin(N / 2, (int) (8000.0 * N / sr)), bLow = juce::jmax(b0 + 2, (int) (150.0 * N / sr));
    int frames = (n - N) / hop + 1;
    e.v.assign((size_t) frames, 0.0f); e.low.assign((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f) {
        const float* p = x + (size_t) f * hop;
        for (int i = 0; i < N; ++i) fr[(size_t) i] = p[i] * win[(size_t) i];
        fft.mags(fr.data(), mag.data());
        float flux = 0, lowFlux = 0;
        for (int b = b0; b <= b1; ++b) {
            float m = std::log1p(100.0f * mag[(size_t) b] / N);
            float d = m - prev[(size_t) b];
            if (d > 0) flux += d;
            if (b < bLow) { float dl = mag[(size_t) b] - prevLin[(size_t) b]; if (dl > 0) lowFlux += dl / N; prevLin[(size_t) b] = mag[(size_t) b]; }   // bass and kick: by their energy
            prev[(size_t) b] = m;
        }
        e.v[(size_t) f] = f == 0 ? 0.0f : flux; e.low[(size_t) f] = f == 0 ? 0.0f : lowFlux;
    }
    // remove the slow part (a moving average of 0.4 s) and keep what rises
    auto detrend = [](std::vector<float>& v, int w) {
        std::vector<float> o(v.size()); double s = 0; int n2 = (int) v.size();
        std::vector<double> c((size_t) n2 + 1, 0.0); for (int i = 0; i < n2; ++i) c[(size_t) i + 1] = c[(size_t) i] + v[(size_t) i];
        for (int i = 0; i < n2; ++i) { int a = juce::jmax(0, i - w), b = juce::jmin(n2, i + w + 1); s = (c[(size_t) b] - c[(size_t) a]) / (b - a); o[(size_t) i] = juce::jmax(0.0f, v[(size_t) i] - (float) s); }
        v.swap(o); };
    detrend(e.v, 20); detrend(e.low, 20);
    return e;
}

// ------------------------------------------------------------------ tempo, beat, downbeat
Tempo tempo(const float* x, int n, double sr, int bpb, double prefer) {
    Tempo t;
    auto e = onsetEnvelope(x, n, sr);
    int F = (int) e.v.size();
    if (F < 95) return t;                                    // under a second: too short to tell
    double fps = sr / e.hop;
    auto& v = e.v;
    if (*std::max_element(v.begin(), v.end()) < 0.6f) return t;   // no attacks at all: no beat
    double mean = std::accumulate(v.begin(), v.end(), 0.0) / F;
    std::vector<float> z((size_t) F); for (int i = 0; i < F; ++i) z[(size_t) i] = (float) (v[(size_t) i] - mean);
    // autocorrelation up to 4 bars of the slowest tempo
    int maxLag = juce::jmin(F - 1, (int) (fps * 60.0 / 40.0 * 4));
    std::vector<double> r((size_t) maxLag + 1, 0.0);
    for (int l = 1; l <= maxLag; ++l) { double s = 0; for (int i = 0; i + l < F; ++i) s += z[(size_t) i] * z[(size_t) (i + l)]; r[(size_t) l] = s / (F - l); }
    double r0 = 0; for (int i = 0; i < F; ++i) r0 += (double) z[(size_t) i] * z[(size_t) i]; r0 /= F;
    if (r0 <= 1e-12) return t;
    auto at = [&](double l) { if (l < 1 || l >= maxLag) return 0.0; int i = (int) l; double f = l - i; return r[(size_t) i] * (1 - f) + r[(size_t) i + 1] * f; };
    // a comb over the beat and its multiples, with a gentle preference for 70-160 BPM
    auto score = [&](double bpm) {
        double l = fps * 60.0 / bpm, s = 0;
        for (int k = 1; k <= 4; ++k) s += at(l * k) / k;
        s += 0.5 * at(l * bpb);
        double c = prefer > 0 ? prefer : 115.0;
        double wgt = std::exp(-0.5 * std::pow(std::log2(bpm / c) / 0.85, 2.0));
        return s * wgt;
    };
    double best = 0, bestBpm = 0;
    std::vector<double> sc;
    for (double b = 50; b <= 220; b += 0.25) { double s = score(b); sc.push_back(s); if (s > best) { best = s; bestBpm = b; } }
    if (best <= 0) return t;
    // refine
    for (double b = bestBpm - 0.25; b <= bestBpm + 0.25; b += 0.01) { double s = score(b); if (s > best) { best = s; bestBpm = b; } }
    double avg = std::accumulate(sc.begin(), sc.end(), 0.0) / sc.size();
    t.confidence = juce::jlimit(0.0, 1.0, (best / std::max(avg, 1e-12) - 1.0) / 4.0);
    // a loop: the file length is a whole number of bars - then the tempo is exactly that
    double secs = n / sr;
    double bestLoop = 0, loopErr = 1e9;
    for (double mult : { 1.0, 2.0, 0.5, 1.5, 2.0 / 3.0 }) {
        double cand = bestBpm * mult;
        if (cand < 50 || cand > 220) continue;
        if (mult != 1.0 && score(cand) < best * (mult == 2.0 || mult == 0.5 ? 0.6 : 0.9)) continue;
        double bars = secs * cand / 60.0 / bpb;
        for (int nb : { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 }) {
            double exactBpm = nb * bpb * 60.0 / secs;
            // a loop cut on its bar line matches the heard tempo closely (a long excerpt matches some bar count by chance:
            // the tolerance shrinks with the number of beats, where the heard tempo is more precise)
            double tol = juce::jlimit(0.004, 0.02, 0.6 / (nb * bpb));
            double err = std::abs(exactBpm / cand - 1.0) + (mult == 1.0 ? 0.0 : 0.002);
            if (err < tol && err < loopErr) { loopErr = err; bestLoop = exactBpm; t.bars = nb; }
            juce::ignoreUnused(bars);
        }
    }
    if (bestLoop > 0) { bestBpm = bestLoop; t.wholeBars = true; }
    else {
        // not a loop: fine-tune the tempo so the beat grid stays on the attacks over the whole file
        // (an error of 0.3 % drifts 30 ms over 8 bars)
        std::vector<float> both((size_t) F);
        double lm = std::accumulate(e.low.begin(), e.low.end(), 0.0) / F + 1e-9;
        for (int i = 0; i < F; ++i) both[(size_t) i] = (float) (v[(size_t) i] / (mean + 1e-9) + 1.5 * e.low[(size_t) i] / lm);
        auto fit = [&](double bpm) {
            double Pp = fps * 60.0 / bpm, bs = -1;
            for (double ph = 0; ph < Pp; ph += 0.5) {
                double sm = 0; int cnt = 0;
                for (double q = ph; q < F - 1; q += Pp) { int i = (int) q; double fr = q - i; sm += both[(size_t) i] * (1 - fr) + both[(size_t) i + 1] * fr; ++cnt; }
                if (cnt && sm / cnt > bs) bs = sm / cnt; }
            return bs; };
        double fb = bestBpm, fs = fit(bestBpm);
        for (double b = bestBpm * 0.992; b <= bestBpm * 1.008; b += bestBpm * 0.0005) { double sc2 = fit(b); if (sc2 > fs * 1.0001) { fs = sc2; fb = b; } }
        bestBpm = fb;
        if (std::abs(bestBpm - std::round(bestBpm)) < 0.25) bestBpm = std::round(bestBpm);   // most music is made at a whole BPM
    }
    t.bpm = bestBpm;
    // the beat grid: the phase where the onsets line up best
    double P = fps * 60.0 / bestBpm;
    auto gridSum = [&](double ph, double step, const std::vector<float>& env) {
        double s = 0; int cnt = 0;
        for (double p = ph; p < F - 1; p += step) { int i = (int) std::round(p); s += env[(size_t) i] + 0.5 * (i > 0 ? env[(size_t) i - 1] : 0) + 0.5 * env[(size_t) i + 1]; ++cnt; }
        return cnt ? s / cnt : 0.0; };
    double bestPh = 0, bestS = -1;
    double lowMean0 = std::accumulate(e.low.begin(), e.low.end(), 0.0) / F + 1e-9, vMean0 = mean + 1e-9;
    for (double ph = 0; ph < P; ph += 0.25) { double s = gridSum(ph, P, v) / vMean0 + 1.5 * gridSum(ph, P, e.low) / lowMean0; if (s > bestS) { bestS = s; bestPh = ph; } }
    // a loop that starts on its beat: keep the start
    bool snapped = t.wholeBars && (bestPh < 4.0 || P - bestPh < 4.0);
    if (snapped) bestPh = 0;
    const double centre = (sr > 60000 ? 4096 : 2048) * 0.5;       // a frame hears an onset best when it is in its middle
    t.beatOffset = bestPh * e.hop + (snapped ? 0.0 : centre);
    // the downbeat: the beat of the bar with the strongest low (bass, kick) onsets
    double bestD = -1; int bestB = 0;
    double s0 = 0;
    double lowMean = std::accumulate(e.low.begin(), e.low.end(), 0.0) / F;
    // the harmony: a chroma for every beat; the chords (and the bass notes) change on the bar line
    std::vector<double> nov;
    {
        int NF = sr > 60000 ? 8192 : 4096;
        double beatS = P * e.hop;
        if (beatS > NF * 0.6) {
            FFT fft(NF); auto win = hann(NF);
            std::vector<float> fr((size_t) NF), mag((size_t) (NF / 2 + 1));
            std::vector<std::array<double, 12>> chroma;
            for (double bp = bestPh * e.hop + (snapped ? 0.0 : centre); ; bp += beatS) {
                long a = (long) std::llround(bp + beatS * 0.15);
                if (a + NF > n) break;
                std::array<double, 12> c {}; double tot = 0;
                if (a >= 0) {
                    for (int i = 0; i < NF; ++i) fr[(size_t) i] = x[a + i] * win[(size_t) i];
                    fft.mags(fr.data(), mag.data());
                    for (int bn = 2; bn < NF / 2; ++bn) { double f = bn * sr / NF; if (f < 55 || f > 1800) continue;
                        if (mag[(size_t) bn] < mag[(size_t) bn - 1] || mag[(size_t) bn] < mag[(size_t) bn + 1]) continue;
                        int pc = (((int) std::lround(12.0 * std::log2(f / 440.0)) + 69) % 12 + 12) % 12;
                        c[(size_t) pc] += mag[(size_t) bn]; tot += mag[(size_t) bn]; }
                }
                if (tot > 0) for (auto& vv : c) vv = std::sqrt(vv / tot);
                chroma.push_back(c);
            }
            nov.assign(chroma.size(), 0.0);
            for (size_t k = 1; k < chroma.size(); ++k) {
                double dot = 0, na = 0, nb2 = 0;
                for (int i = 0; i < 12; ++i) { dot += chroma[k][(size_t) i] * chroma[k - 1][(size_t) i]; na += chroma[k][(size_t) i] * chroma[k][(size_t) i]; nb2 += chroma[k - 1][(size_t) i] * chroma[k - 1][(size_t) i]; }
                nov[k] = na > 0 && nb2 > 0 ? 1.0 - dot / std::sqrt(na * nb2) : 0.0;
            }
        }
    }
    double novB[16] = {}, novMean = 0; int novCnt = 0;
    for (size_t k = 1; k < nov.size(); ++k) { novB[k % (size_t) bpb] += nov[k]; novMean += nov[k]; ++novCnt; }
    if (novCnt > 0) novMean /= novCnt;
    int perB[16] = {}; for (size_t k = 1; k < nov.size(); ++k) ++perB[k % (size_t) bpb];
    // the harmony counts when it changes clearly more on one beat of the bar than on the others
    double novBest = 0, novSecond = 0;
    for (int b = 0; b < bpb; ++b) { double m = perB[b] ? novB[b] / perB[b] : 0; if (m > novBest) { novSecond = novBest; novBest = m; } else if (m > novSecond) novSecond = m; }
    bool harmonyClear = novCnt >= bpb * 2 && novBest > 0.08 && novBest > novSecond * 1.6;
    if (std::getenv("ARCO_DBG")) { std::fprintf(stderr, "nov: cnt %d best %.3f second %.3f clear %d :", novCnt, novBest, novSecond, (int) harmonyClear); for (int b = 0; b < bpb; ++b) std::fprintf(stderr, " %.3f", perB[b] ? novB[b] / perB[b] : 0.0); std::fprintf(stderr, "  ph %.1f P %.1f\n", bestPh, P); }
    // each clue relative to its own average over the beats of the bar, so none drowns the others
    double fLow[16] = {}, fAll[16] = {}, fNov[16] = {}, mLow = 0, mAll = 0, mNov = 0;
    for (int b = 0; b < bpb; ++b) {
        fLow[b] = gridSum(bestPh + b * P, P * bpb, e.low) / (lowMean + 1e-9); fAll[b] = gridSum(bestPh + b * P, P * bpb, v) / (mean + 1e-9);
        fNov[b] = perB[b] ? novB[b] / perB[b] : 0.0;
        mLow += fLow[b] / bpb; mAll += fAll[b] / bpb; mNov += fNov[b] / bpb; }
    for (int b = 0; b < bpb; ++b) {
        double s = 2.0 * fLow[b] / (mLow + 1e-9) + fAll[b] / (mAll + 1e-9);
        if (harmonyClear) s += 4.0 * fNov[b] / (mNov + 1e-9);         // the chord changes: the strongest clue for the "one"
        if (b == 0) s0 = s;
        if (s > bestD) { bestD = s; bestB = b; }
    }
    // a loop is usually cut on its bar line: keep its start unless another beat is clearly the "one"
    if (t.wholeBars && bestPh == 0 && bestD < s0 * 1.3) bestB = 0;
    t.downbeatOffset = (bestPh + bestB * P) * e.hop + (snapped ? 0.0 : centre);
    if (std::getenv("ARCO_DBG")) std::fprintf(stderr, "  bestB %d bestD %.2f s0 %.2f whole %d down %.0f\n", bestB, bestD, s0, (int) t.wholeBars, t.downbeatOffset);
    return t;
}

// ------------------------------------------------------------------ key: a chroma profile against the Krumhansl key profiles
Key key(const float* x, int n, double sr) {
    Key k;
    int N = 8192; if (sr > 60000) N = 16384;
    if (n < N) return k;
    FFT fft(N); auto win = hann(N);
    std::vector<float> fr((size_t) N), mag((size_t) (N / 2 + 1));
    double chroma[12] = {};
    int hop = N / 2, frames = juce::jmin((n - N) / hop + 1, (int) (90.0 * sr / hop));
    // which bins belong to which pitch class (65 Hz - 2.1 kHz), with weights to the two nearest semitones
    struct BW { int pc; float w; };
    std::vector<std::array<BW, 2>> map((size_t) (N / 2 + 1), { BW { 0, 0.0f }, BW { 0, 0.0f } });
    for (int b = 1; b <= N / 2; ++b) {
        double f = b * sr / N; if (f < 65 || f > 2100) continue;
        double m = 12.0 * std::log2(f / 440.0) + 69.0; int lo = (int) std::floor(m); double fr2 = m - lo;
        double ww = 1.0 / (1.0 + std::pow(f / 1000.0, 2.0));          // the high harmonics count less
        map[(size_t) b] = { BW { ((lo % 12) + 12) % 12, (float) ((1 - fr2) * ww) }, BW { (((lo + 1) % 12) + 12) % 12, (float) (fr2 * ww) } };
    }
    for (int f = 0; f < frames; ++f) {
        const float* p = x + (size_t) f * hop;
        float en = 0;
        for (int i = 0; i < N; ++i) { fr[(size_t) i] = p[i] * win[(size_t) i]; en += p[i] * p[i]; }
        if (en / N < 1e-7f) continue;
        fft.mags(fr.data(), mag.data());
        double c[12] = {}, tot = 0;
        for (int b = 1; b <= N / 2; ++b) { auto& mm = map[(size_t) b]; if (mm[0].w + mm[1].w <= 0) continue; double m2 = mag[(size_t) b];
            // only the peaks of the spectrum (the notes), not the noise between them
            if (b > 1 && b < N / 2 && (mag[(size_t) b] < mag[(size_t) b - 1] || mag[(size_t) b] < mag[(size_t) b + 1])) continue;
            c[mm[0].pc] += m2 * mm[0].w; c[mm[1].pc] += m2 * mm[1].w; }
        for (double vv : c) tot += vv;
        if (tot > 0) for (int i = 0; i < 12; ++i) chroma[i] += std::sqrt(c[i] / tot);
    }
    static const double MAJ[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double MIN[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&](const double* prof, int tonic) {
        double mx = 0, my = 0; for (int i = 0; i < 12; ++i) { mx += chroma[(i + tonic) % 12]; my += prof[i]; } mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) { double a = chroma[(i + tonic) % 12] - mx, b = prof[i] - my; sxy += a * b; sxx += a * a; syy += b * b; }
        return sxx > 0 ? sxy / std::sqrt(sxx * syy) : 0.0; };
    double best = -2, second = -2; int bt = -1; bool bm = false;
    for (int tn = 0; tn < 12; ++tn)
        for (int mi = 0; mi < 2; ++mi) {
            double c = corr(mi ? MIN : MAJ, tn);
            if (c > best) { second = best; best = c; bt = tn; bm = mi == 1; }
            else if (c > second) second = c;
        }
    if (bt < 0 || best < 0.3) return k;
    k.tonic = bt; k.minor = bm; k.confidence = juce::jlimit(0.0, 1.0, (best - 0.3) / 0.6 * 0.5 + (best - second) * 3.0);
    if (k.confidence < 0.25) k.tonic = -1;                 // not sure: better no key than a wrong one
    return k;
}

// ------------------------------------------------------------------ onsets: the peaks of the onset envelope
std::vector<Onset> onsets(const float* x, int n, double sr, float sens, bool loop) {
    std::vector<Onset> out;
    if (n < 64) return out;
    // what comes before the start: silence, or (a loop) its own end - so a note right at the start is found too
    int P = juce::jmin(n, (int) (0.1 * sr) + (sr > 60000 ? 4096 : 2048));
    std::vector<float> ext((size_t) (n + P), 0.0f);
    if (loop) std::copy(x + n - P, x + n, ext.begin());
    std::copy(x, x + n, ext.begin() + P);
    auto e = onsetEnvelope(ext.data(), n + P, sr);
    int F = (int) e.v.size(); if (F < 5) return out;
    std::vector<float> s(e.v);
    float mx = *std::max_element(s.begin(), s.end()); if (mx < 0.5f) return out;   // nothing that starts: a steady sound
    std::vector<float> sorted(s); std::nth_element(sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    float med = sorted[sorted.size() / 2];
    float thr = std::max({ mx * 0.12f / sens, 0.35f, med * 4.0f });
    const int w30 = (int) (0.03 * sr);
    auto rms = [&](long a, long b) { double s2 = 0; long c = 0; for (long i = juce::jmax(0L, a); i < juce::jmin((long) ext.size(), b); ++i) { s2 += (double) ext[(size_t) i] * ext[(size_t) i]; ++c; } return c ? s2 / c : 0.0; };
    int lastF = -100;
    for (int i = 2; i < F - 2; ++i) {
        float a = s[(size_t) i];
        if (a < thr || a < s[(size_t) i - 1] || a < s[(size_t) i + 1] || a < s[(size_t) i - 2] || a < s[(size_t) i + 2]) continue;
        // a local threshold: louder than the neighbourhood
        double loc = 0; int cnt = 0; for (int j = juce::jmax(0, i - 15); j < juce::jmin(F, i + 16); ++j) { loc += s[(size_t) j]; ++cnt; }
        if (a < 1.5 * loc / cnt) continue;
        if (i - lastF < 5) continue;                         // 50 ms apart at least
        // the onset is a little before the flux peak: half a frame back, refined by the parabola
        double y0 = s[(size_t) i - 1], y1 = a, y2 = s[(size_t) i + 1];
        double d = (y0 - 2 * y1 + y2) != 0 ? 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) : 0;
        double pos = (i + d) * e.hop + (e.sr > 60000 ? 4096 : 2048) * 0.5 - e.hop * 0.5 - P;
        // a note starts: the sound gets louder (3 dB or more) - a slowly beating chord does not
        long pe = (long) (pos + P);
        if (rms(pe, pe + w30) < 2.0 * rms(pe - w30 - (long) (0.01 * sr), pe - (long) (0.005 * sr)) + 1e-10) continue;
        lastF = i;
        if (pos < -0.025 * sr || pos >= n) continue;
        out.push_back({ juce::jmax(0.0, pos), a / mx });
    }
    return out;
}

// ------------------------------------------------------------------ 1/3-octave spectrum
double bandFreq(int b) { return 25.0 * std::pow(2.0, b / 3.0); }
std::vector<double> bands(const float* x, int n, double sr) {
    std::vector<double> out((size_t) NBANDS, -120.0);
    int N = 8192; if (n < N) N = 2048; if (n < N) return out;
    FFT fft(N); auto win = hann(N);
    std::vector<float> fr((size_t) N), mag((size_t) (N / 2 + 1));
    std::vector<double> acc((size_t) NBANDS, 0.0);
    int hop = N / 2, frames = 0;
    std::vector<int> bandOf((size_t) (N / 2 + 1), -1);
    for (int k = 1; k <= N / 2; ++k) { double f = k * sr / N; int b = (int) std::floor(3.0 * std::log2(f / 25.0) + 0.5); if (b >= 0 && b < NBANDS) bandOf[(size_t) k] = b; }
    for (int st = 0; st + N <= n; st += hop) {
        for (int i = 0; i < N; ++i) fr[(size_t) i] = x[st + i] * win[(size_t) i];
        fft.mags(fr.data(), mag.data());
        for (int k = 1; k <= N / 2; ++k) if (bandOf[(size_t) k] >= 0) { double m = mag[(size_t) k] / (N * 0.25); acc[(size_t) bandOf[(size_t) k]] += m * m; }
        ++frames;
    }
    if (frames == 0) return out;
    for (int b = 0; b < NBANDS; ++b) out[(size_t) b] = 10.0 * std::log10(acc[(size_t) b] / frames + 1e-14);
    return out;
}

double attackStart(const float* x, int n, double sr, double around) {
    int w = (int) (0.015 * sr), a = juce::jmax(0, (int) around - w), b = juce::jmin(n, (int) around + w);
    if (b - a < 16) return around;
    // a 1 ms envelope
    float k = 1.0f - std::exp(-1.0f / (float) (0.001 * sr)), env = 0, mx = 0;
    std::vector<float> e((size_t) (b - a));
    for (int i = a; i < b; ++i) { env += k * (std::abs(x[i]) - env); e[(size_t) (i - a)] = env; mx = juce::jmax(mx, env); }
    if (mx <= 1e-6f) return around;
    // the quiet floor before the attack
    float floor = mx; int pk = 0;
    for (int i = 0; i < (int) e.size(); ++i) if (e[(size_t) i] >= mx * 0.999f) { pk = i; break; }
    for (int i = 0; i < pk; ++i) floor = juce::jmin(floor, e[(size_t) i]);
    float thr = floor + (mx - floor) * 0.25f;
    for (int i = 0; i <= pk; ++i) if (e[(size_t) i] >= thr) return a + i - (0.0007 * sr);   // the envelope lags ~0.7 ms
    return around;
}

// ------------------------------------------------------------------ the stretch
juce::AudioBuffer<float> stretch(const juce::AudioBuffer<float>& in, double sr, int outLength, double semis) {
    int ch = in.getNumChannels(), n = in.getNumSamples();
    juce::AudioBuffer<float> out(ch, juce::jmax(1, outLength)); out.clear();
    if (n < 64 || outLength < 64) return out;
    if (outLength == n && std::abs(semis) < 0.001) { for (int c = 0; c < ch; ++c) out.copyFrom(c, 0, in, c, 0, n); return out; }
    signalsmith::stretch::SignalsmithStretch<float> st(1234);
    st.presetDefault(ch, (float) sr);
    st.setTransposeSemitones((float) semis, (float) (8000.0 / sr));
    // the stretcher wants some input before the start: pad with silence on both sides, then cut
    int pad = st.blockSamples() + st.intervalSamples();
    double ratio = (double) outLength / n;
    int inN = n + 2 * pad, outN = (int) std::round(inN * ratio);
    std::vector<std::vector<float>> ib((size_t) ch, std::vector<float>((size_t) inN, 0.0f)), ob((size_t) ch, std::vector<float>((size_t) outN, 0.0f));
    for (int c = 0; c < ch; ++c) std::copy(in.getReadPointer(c), in.getReadPointer(c) + n, ib[(size_t) c].begin() + pad);
    st.exact(ib, inN, ob, outN);
    int off = (int) std::round(pad * ratio);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < outLength; ++i) { int j = off + i; out.setSample(c, i, j < outN ? ob[(size_t) c][(size_t) j] : 0.0f); }
    return out;
}

juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& in, double from, double to) {
    if (std::abs(from - to) < 0.5 || in.getNumSamples() == 0) return in;
    double ratio = from / to;
    int outN = (int) std::floor(in.getNumSamples() / ratio);
    juce::AudioBuffer<float> o(in.getNumChannels(), juce::jmax(1, outN)); o.clear();
    for (int c = 0; c < in.getNumChannels(); ++c) { juce::WindowedSincInterpolator ip; ip.process(ratio, in.getReadPointer(c), o.getWritePointer(c), outN, in.getNumSamples(), 0); }
    return o;
}

} // namespace analysis

// ================================================================== the smart import
juce::String ImportPlan::summary() const {
    juce::String s;
    s << (fileBpm > 0 ? juce::String(fileBpm, 1) + " BPM" : juce::String("no clear beat")) << juce::String::fromUTF8("  ·  ") << key.name();
    if (cut) s << juce::String::fromUTF8("  ·  its first ") << (int) trackSeconds() << juce::String::fromUTF8(" s (a track holds ") << (int) trackSeconds() << " s; use STUDIO for longer files)";
    return s;
}

bool prepareImport(const juce::File& f, double engineSr, ImportPlan& p, juce::AudioFormatManager& fm, juce::String& error, int bpb) {
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) { error = "can't read " + f.getFileName() + " (use WAV, AIFF, MP3, FLAC or M4A)"; return false; }
    p.file = f; p.fileSr = r->sampleRate;
    // a track holds up to 40 s: read only what can be used (a long file read whole filled the iPad's memory and it closed)
    long n = juce::jmin((long) r->lengthInSamples, (long) (r->sampleRate * (trackSeconds() + 2.0)));
    if (n < 32) { error = "the file is empty"; return false; }
    p.cut = r->lengthInSamples > n;
    juce::AudioBuffer<float> tmp((int) juce::jlimit(1u, 2u, r->numChannels), (int) n);
    r->read(&tmp, 0, (int) n, 0, true, tmp.getNumChannels() > 1);
    p.seconds = n / r->sampleRate;
    p.audio = analysis::resample(tmp, r->sampleRate, engineSr);
    // listen to a mono version (the first 90 s are plenty)
    int an = juce::jmin(p.audio.getNumSamples(), (int) (engineSr * 90));
    std::vector<float> mono((size_t) an);
    for (int i = 0; i < an; ++i) { float s = 0; for (int c = 0; c < p.audio.getNumChannels(); ++c) s += p.audio.getSample(c, i); mono[(size_t) i] = s / p.audio.getNumChannels(); }
    p.tempo = analysis::tempo(mono.data(), an, engineSr, juce::jlimit(2, 12, bpb));
    // a long file is heard in its first 90 s: is the whole file a whole number of bars at that tempo? (a long loop)
    if (p.audio.getNumSamples() > an && p.tempo.bpm > 0) {
        double barS = juce::jlimit(2, 12, bpb) * 60.0 / p.tempo.bpm * engineSr;
        double nb = p.audio.getNumSamples() / barS; int k = (int) std::round(nb);
        if (k >= 1 && std::abs(nb - k) * barS < engineSr * 0.03 * juce::jmax(1.0, k / 32.0)) {        // within ~30 ms over the whole file
            p.tempo.wholeBars = true; p.tempo.bars = k; p.tempo.bpm = k * juce::jlimit(2, 12, bpb) * 60.0 / (p.audio.getNumSamples() / engineSr);
            p.tempo.downbeatOffset = std::fmod(p.tempo.downbeatOffset, barS) < barS * 0.1 ? 0.0 : p.tempo.downbeatOffset;
        }
    }
    p.key = analysis::key(mono.data(), an, engineSr);
    p.fileBpm = p.tempo.bpm;
    return true;
}

void planImport(ImportPlan& p, const PieceInfo& piece) {
    p.semitones = 0;
    if (p.matchKey && p.key.tonic >= 0 && piece.key.tonic >= 0) {
        int d = ((piece.key.majorEquivalent() - p.key.majorEquivalent()) % 12 + 12) % 12;
        p.semitones = d > 6 ? d - 12 : d;                     // the smallest move: -5 .. +6
        if (p.semitones == 6) p.semitones = -6 + 0;          // a tritone either way: down sounds more natural
    }
    double barLen = piece.beatLen * piece.beats;
    if (barLen <= 0) { p.outBars = 0; return; }
    double start = p.matchTempo && p.fileBpm > 0 && !p.tempo.wholeBars ? p.tempo.downbeatOffset : 0.0;
    double len = p.audio.getNumSamples() - start;
    if (p.matchTempo && p.fileBpm > 0) len *= p.fileBpm / piece.bpm;
    p.outBars = juce::jmax(1, (int) std::round(len / barLen));
}
juce::String ImportPlan::fitText(const PieceInfo& piece) const {
    double barLen = piece.beatLen * piece.beats;
    if (piece.loopLen <= 0 || barLen <= 0) return juce::String(outBars) + (outBars == 1 ? " bar" : " bars");
    int Lb = juce::jmax(1, (int) std::round(piece.loopLen / barLen));
    if (outBars >= Lb) return outBars == Lb ? juce::String(Lb) + " bars, the length of the loop" : "the first " + juce::String(Lb) + " bars (the loop is " + juce::String(Lb) + " bars)";
    int nb = outBars; while (Lb % nb != 0) ++nb;
    return juce::String(outBars) + (outBars == 1 ? " bar" : " bars") + ", repeated " + juce::String(Lb / nb) + juce::String::fromUTF8("× to fill the ") + juce::String(Lb) + "-bar loop";
}

juce::AudioBuffer<float> renderImport(const ImportPlan& p, const PieceInfo& piece, std::function<bool(float)> progress) {
    const double sr = piece.sr;
    bool tempoOn = p.matchTempo && p.fileBpm > 0;
    double barLen = piece.beatLen * piece.beats;
    bool isLoop = p.tempo.wholeBars && tempoOn;
    int full = p.audio.getNumSamples();
    int start = tempoOn ? juce::jlimit(0, juce::jmax(0, full - 64), (int) std::round(p.tempo.downbeatOffset)) : 0;
    int ch = p.audio.getNumChannels();
    int n = isLoop ? full : full - start;
    if (n < 64) return {};
    juce::AudioBuffer<float> src(ch, n);
    if (isLoop) for (int c = 0; c < ch; ++c) { src.copyFrom(c, 0, p.audio, c, start, full - start); if (start > 0) src.copyFrom(c, full - start, p.audio, c, 0, start); }   // a loop turns around: its bar line comes first
    else for (int c = 0; c < ch; ++c) src.copyFrom(c, 0, p.audio, c, start, n);
    // the length it becomes: whole bars of the piece
    int bars = juce::jmax(1, p.outBars);
    long target = (long) std::llround(bars * barLen);
    double ratio = tempoOn ? p.fileBpm / piece.bpm : 1.0;
    if (progress) progress(0.1f);
    juce::AudioBuffer<float> out;
    if (isLoop) {
        // a loop: stretch it as a circle (the end leads into the start), so it repeats without a seam
        int wrap = juce::jmin(n / 2, (int) (sr * 0.5));
        juce::AudioBuffer<float> circ(ch, n + 2 * wrap);
        for (int c = 0; c < ch; ++c) { circ.copyFrom(c, 0, src, c, n - wrap, wrap); circ.copyFrom(c, wrap, src, c, 0, n); circ.copyFrom(c, wrap + n, src, c, 0, wrap); }
        int outLen = (int) std::llround(target);
        double r2 = (double) outLen / n;
        out.setSize(ch, outLen);
        if (std::abs(r2 - 1.0) < 1e-4 && p.semitones == 0) {      // its own tempo, its own key: the samples as they are
            out.clear(); for (int c = 0; c < ch; ++c) out.copyFrom(c, 0, src, c, 0, juce::jmin(outLen, n));
        } else {
            auto st = analysis::stretch(circ, sr, (int) std::llround((n + 2 * wrap) * r2), p.semitones);
            int off = (int) std::llround(wrap * r2);
            for (int c = 0; c < ch; ++c) out.copyFrom(c, 0, st, c, off, juce::jmin(outLen, st.getNumSamples() - off));
        }
    } else {
        int outLen = (int) std::llround(n * ratio);
        auto st = std::abs(ratio - 1.0) < 1e-6 && p.semitones == 0 ? src : analysis::stretch(src, sr, outLen, p.semitones);
        out.setSize(ch, (int) target); out.clear();
        for (int c = 0; c < ch; ++c) out.copyFrom(c, 0, st, c, 0, (int) juce::jmin((long) st.getNumSamples(), target));
        // a gentle fade where it was cut
        if (st.getNumSamples() > target) { int fl = juce::jmin((int) target / 4, (int) (sr * 0.03)); out.applyGainRamp((int) target - fl, fl, 1.0f, 0.0f); }
    }
    if (progress) progress(0.9f);
    // in a scene that already has a loop: fill the loop exactly (repeat the file when it fits a whole number of times)
    if (piece.loopLen > 0 && barLen > 0) {
        long L = piece.loopLen;
        int Lb = juce::jmax(1, (int) std::round(L / barLen));
        int nb = bars;
        if (nb < Lb) { while (Lb % nb != 0) ++nb; }               // pad up to a bar count that divides the loop
        long unit = nb >= Lb ? L : (long) std::llround(nb * barLen);
        juce::AudioBuffer<float> t(ch, (int) L); t.clear();
        for (long pos = 0; pos < L; pos += unit)
            for (int c = 0; c < ch; ++c) t.copyFrom(c, (int) pos, out, c, 0, (int) std::min({ (long) out.getNumSamples(), unit, L - pos }));
        if (out.getNumSamples() > L) { int fl = (int) juce::jmin(L / 4, (long) (sr * 0.03)); t.applyGainRamp((int) L - fl, fl, 1.0f, 0.0f); }
        out = std::move(t);
    }
    if (progress) progress(1.0f);
    return out;
}

} // namespace arco
