#pragma once
// ARCO 1.2: EXPORT. A private copy of the engine plays the song offline (faster than real time) and the result is
// measured (integrated loudness, ITU BS.1770 / EBU R128), optionally normalised, limited to a true-peak ceiling,
// dithered for 16-bit and written as WAV or FLAC. Stems: every track on its own, dry or through the mix.
#include <JuceHeader.h>
#include <functional>
#include <deque>
#include "../Engine/Engine.h"
#include "../Model/Model.h"

namespace arco {

class AudioHost;

struct ExportOptions {
    enum What { CurrentScene = 0, AllScenes = 1 };
    int what = CurrentScene;
    int repeats = 4;                 // loops of each scene
    double tailSeconds = 4.0;        // echoes and reverbs ring out
    double sampleRate = 48000.0;     // 44100, 48000, 96000
    int bits = 24;                   // 16, 24, 32 (float)
    bool flac = false;
    int loudness = 1;                // 0 off (peak -1 dBTP only), 1 streaming -14 LUFS, 2 louder -10 LUFS, 3 quiet/broadcast -23 LUFS
    bool stems = false;              // one file per track
    bool stemsDry = true;            // stems as recorded (true) or each track alone through the effects (false)
};

namespace loud {
    // K-weighting (BS.1770): high shelf then high pass, for any sample rate
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double tick(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    inline void kweight(double sr, Biquad& shelf, Biquad& hp) {
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan(juce::MathConstants<double>::pi * f0 / sr), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0; shelf.b1 = 2.0 * (K * K - Vh) / a0; shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0; shelf.a2 = (1.0 - K / Q + K * K) / a0;
        f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan(juce::MathConstants<double>::pi * f0 / sr);
        a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0 / a0; hp.b1 = -2.0 / a0; hp.b2 = 1.0 / a0; hp.a1 = 2.0 * (K * K - 1.0) / a0; hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
    // integrated loudness in LUFS (-70 if silent)
    inline double integrated(const juce::AudioBuffer<float>& b, double sr) {
        int nch = b.getNumChannels(), n = b.getNumSamples();
        if (n < (int) (0.4 * sr)) return -70.0;
        std::vector<double> sq((size_t) n, 0.0);
        for (int c = 0; c < nch; ++c) {
            Biquad s, h; kweight(sr, s, h);
            auto* d = b.getReadPointer(c);
            for (int i = 0; i < n; ++i) { double y = h.tick(s.tick(d[i])); sq[(size_t) i] += y * y; }
        }
        int blk = (int) (0.4 * sr), hop = (int) (0.1 * sr);
        std::vector<double> z;
        double run = 0; for (int i = 0; i < blk; ++i) run += sq[(size_t) i];
        for (int st = 0; st + blk <= n; st += hop) {
            if (st > 0) { for (int i = st - hop; i < st; ++i) run -= sq[(size_t) i]; for (int i = st + blk - hop; i < st + blk; ++i) run += sq[(size_t) i]; }
            z.push_back(run / blk);
        }
        auto lufs = [](double ms) { return -0.691 + 10.0 * std::log10(std::max(ms, 1e-12)); };
        double sum = 0; int cnt = 0;
        for (double v : z) if (lufs(v) > -70.0) { sum += v; ++cnt; }
        if (cnt == 0) return -70.0;
        double rel = lufs(sum / cnt) - 10.0; sum = 0; cnt = 0;
        for (double v : z) if (lufs(v) > -70.0 && lufs(v) > rel) { sum += v; ++cnt; }
        return cnt ? lufs(sum / cnt) : -70.0;
    }
    // 4x oversampled peak (true peak, dBTP)
    inline float truePeak(const float* d, int n, int i) {
        // windowed-sinc interpolation of the 3 points between i and i+1
        static float tbl[3][16]; static bool init = false;
        if (!init) { for (int ph = 0; ph < 3; ++ph) for (int k = 0; k < 16; ++k) { double t = (k - 7) - (ph + 1) / 4.0;
                    double sinc = std::abs(t) < 1e-9 ? 1.0 : std::sin(juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                    double w = 0.5 + 0.5 * std::cos(juce::MathConstants<double>::pi * t / 8.0); tbl[ph][k] = (float) (sinc * w); } init = true; }
        float mx = std::abs(d[i]);
        for (int ph = 0; ph < 3; ++ph) { float acc = 0; for (int k = 0; k < 16; ++k) { int j = i + k - 7; if (j >= 0 && j < n) acc += d[j] * tbl[ph][k]; } mx = std::max(mx, std::abs(acc)); }
        return mx;
    }
    // brick-wall true-peak limiter with 2 ms look-ahead and a smooth release
    inline void limit(juce::AudioBuffer<float>& b, double sr, float ceilingDb) {
        int nch = b.getNumChannels(), n = b.getNumSamples();
        float ceil = juce::Decibels::decibelsToGain(ceilingDb);
        std::vector<float> need((size_t) n, 1.0f);
        for (int i = 0; i < n; ++i) { float pk = 0; for (int c = 0; c < nch; ++c) pk = std::max(pk, truePeak(b.getReadPointer(c), n, i)); if (pk > ceil) need[(size_t) i] = ceil / pk; }
        int la = (int) (0.002 * sr);
        std::vector<float> g((size_t) n, 1.0f);
        // look-ahead: the minimum gain over the next la samples, then smoothed (fast attack, 80 ms release)
        std::deque<int> q;
        for (int i = n - 1; i >= 0; --i) {
            while (!q.empty() && need[(size_t) q.back()] >= need[(size_t) i]) q.pop_back();
            q.push_back(i); while (q.front() > i + la) q.pop_front();
            g[(size_t) i] = need[(size_t) q.front()];
        }
        float rel = (float) (1.0 - std::exp(-1.0 / (0.08 * sr))), att = (float) (1.0 - std::exp(-1.0 / (0.0005 * sr))), cur = 1.0f;
        for (int i = 0; i < n; ++i) { float t = g[(size_t) i]; cur += (t < cur ? att : rel) * (t - cur); cur = std::min(cur, t); g[(size_t) i] = cur; }
        for (int c = 0; c < nch; ++c) { auto* d = b.getWritePointer(c); for (int i = 0; i < n; ++i) d[i] *= g[(size_t) i]; }
    }
}

// everything a render needs, taken on the message thread (the render itself can run on another thread)
struct RenderSnapshot { juce::var song, setup; float sv[15] {}; int smask = 0, sceneNow = 1; double sr = 48000; };

class Renderer {
public:
    explicit Renderer(AudioHost& h) : host(h) {}
    RenderSnapshot snapshot();
    // render the mix (or one strip alone: solo >= 0) at the engine rate; progress(0..1) returns false to cancel
    bool renderMix(const ExportOptions& o, juce::AudioBuffer<float>& out, double& rate, int solo, std::function<bool(float)> progress);
    bool renderMix(const RenderSnapshot& snap, const ExportOptions& o, juce::AudioBuffer<float>& out, double& rate, int solo, std::function<bool(float)> progress);
    // dry stems straight from the loop memory
    bool dryStems(int scene, juce::OwnedArray<juce::AudioBuffer<float>>& out, juce::StringArray& names, double& rate);
    // finish (resample, loudness, limit) and write
    static bool write(juce::AudioBuffer<float> buf, double rate, const ExportOptions& o, const juce::File& f, juce::String& report, bool normalise = true);
    static double resampleTo(juce::AudioBuffer<float>& b, double from, double to);
private:
    AudioHost& host;
};

} // namespace arco
