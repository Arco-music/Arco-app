#pragma once
// SCORE - offline time-stretch and pitch-shift (WSOLA: overlapping pieces, each placed where it matches best).
#include <vector>
#include <cmath>
#include <algorithm>

namespace score {

// stretch by ratio (2 = twice as long), stereo, keeping the pitch
inline void wsolaStretch(const std::vector<float>& inL, const std::vector<float>& inR, double ratio, double sr,
                         std::vector<float>& outL, std::vector<float>& outR) {
    const int N = (int) inL.size();
    ratio = std::max(0.25, std::min(4.0, ratio));
    const int W = std::max(256, (int) (0.046 * sr));      // window (~46 ms)
    const int H = W / 2;                                   // synthesis hop
    const int S = std::max(64, W / 4);                     // search range
    const int outN = (int) std::ceil(N * ratio) + W;
    outL.assign((size_t) outN, 0.0f); outR.assign((size_t) outN, 0.0f);
    std::vector<float> norm((size_t) outN, 0.0f), win((size_t) W);
    for (int i = 0; i < W; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * (float) i / (float) W);
    auto mono = [&](int i) { return i >= 0 && i < N ? 0.5f * (inL[(size_t) i] + inR[(size_t) i]) : 0.0f; };
    int prevIn = 0;
    for (int o = 0; o + W < outN; o += H) {
        double ideal = o / ratio;
        int best = (int) ideal;
        if (o > 0) {
            // the piece that continues the previous one most smoothly: the previous piece's next H samples
            int want = prevIn + H; double bestC = -1e30;
            for (int d = -S; d <= S; d += 4) {
                int c = (int) ideal + d; if (c < 0 || c + W > N) continue;
                double s = 0; for (int k = 0; k < W / 2; k += 2) s += mono(want + k) * mono(c + k);
                if (s > bestC) { bestC = s; best = c; }
            }
            // refine
            int b0 = best; for (int d = -3; d <= 3; ++d) { int c = b0 + d; if (c < 0 || c + W > N) continue;
                double s = 0; for (int k = 0; k < W / 2; k += 2) s += mono(want + k) * mono(c + k); if (s > bestC) { bestC = s; best = c; } }
        }
        best = std::max(0, std::min(N - 1, best));
        for (int k = 0; k < W; ++k) {
            int i = best + k; if (i >= N) break;
            outL[(size_t) (o + k)] += inL[(size_t) i] * win[(size_t) k]; outR[(size_t) (o + k)] += inR[(size_t) i] * win[(size_t) k];
            norm[(size_t) (o + k)] += win[(size_t) k];
        }
        prevIn = best;
    }
    for (int i = 0; i < outN; ++i) if (norm[(size_t) i] > 1e-3f) { outL[(size_t) i] /= norm[(size_t) i]; outR[(size_t) i] /= norm[(size_t) i]; }
    int keep = (int) std::llround(N * ratio);
    outL.resize((size_t) keep); outR.resize((size_t) keep);
}

// resample by a speed factor (2 = an octave up, half as long), cubic interpolation
inline void resampleSpeed(const std::vector<float>& in, double speed, std::vector<float>& out) {
    int N = (int) in.size(); int M = (int) std::floor((N - 2) / speed);
    out.assign((size_t) std::max(0, M), 0.0f);
    auto at = [&](int i) { return in[(size_t) std::max(0, std::min(N - 1, i))]; };
    for (int k = 0; k < M; ++k) {
        double p = k * speed; int i = (int) p; float f = (float) (p - i);
        float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
        float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        out[(size_t) k] = ((c3 * f + c2) * f + c1) * f + x0;
    }
}

// pitch-shift by semitones, keeping the length
inline void pitchShift(const std::vector<float>& inL, const std::vector<float>& inR, double semis, double sr,
                       std::vector<float>& outL, std::vector<float>& outR) {
    double speed = std::pow(2.0, semis / 12.0);
    std::vector<float> sL, sR;
    wsolaStretch(inL, inR, speed, sr, sL, sR);              // longer by speed ...
    resampleSpeed(sL, speed, outL); resampleSpeed(sR, speed, outR);   // ... then played faster: same length, new pitch
    outL.resize(inL.size(), 0.0f); outR.resize(inR.size(), 0.0f);
}

} // namespace score
