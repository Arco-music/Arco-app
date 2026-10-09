#pragma once
// ARCO 1.3: AMBISONICS - put any strip anywhere around the audience, still or moving, on any speaker layout.
//  - encoding: 3rd order (16 channels, ACN / SN3D) per strip, with width (blur by order) and distance (level, air, delay)
//  - decoding: AllRAD - 240 virtual speakers spread evenly over the sphere, each one panned with VBAP onto the real
//    speakers (their convex hull; imaginary speakers above / below fill what the layout lacks), max-rE weighting,
//    energy normalised. Works for stereo, quad, 5.0, 7.0, rings, domes, cubes, anything up to 32 speakers.
//  - the speakers' own distances are compensated (delay + level), so the sound arrives together from all of them
//  - motion: still, circle, swing, wander, fly-over - free or locked to the tempo
//  - the headphones get a stereo version of the same scene
// Message bus "st_amb". Speaker k: spk_az<k>, spk_el<k>, spk_dist<k> (m); strip s: amb<s> (on), amb_az<s>, amb_el<s>,
// amb_dist<s> (m), amb_w<s> (width %), amb_mot<s> (0-4), amb_spd<s> (%), amb_rng<s> (deg), amb_sync<s>; amb_on, spk_n.
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace arco {

class Ambisonics {
public:
    static constexpr int CH = 16, NS = 12, MAXSPK = 32, NV = 240, SUBMAX = 64;
    Ambisonics() {
        for (int k = 0; k < MAXSPK; ++k) { spAz[k] = k < 2 ? (k == 0 ? -30.0f : 30.0f) : -180.0f + k * 360.0f / MAXSPK; spEl[k] = 0; spDist[k] = 2.0f; }
        for (int s = 0; s < NS; ++s) { srcAz[s] = base[s] = -90.0f + s * 15.0f; srcEl[s] = 0; srcDist[s] = 2.0f; curAz[s].store(srcAz[s]); curEl[s].store(0.0f); }
        // the virtual speakers: a Fibonacci sphere
        for (int v = 0; v < NV; ++v) {
            double z = 1.0 - 2.0 * (v + 0.5) / NV, r = std::sqrt(1.0 - z * z), ph = v * 2.399963229728653;
            vx[v] = (float) (r * std::cos(ph)); vy[v] = (float) (r * std::sin(ph)); vz[v] = (float) z;
        }
    }
    // ---- parameters
    int paramIndex(const std::string& n) const {
        auto num = [&](const char* pre) -> int { size_t l = std::strlen(pre); if (n.compare(0, l, pre) != 0 || n.size() <= l || !std::isdigit((unsigned char) n[l])) return -1; return std::atoi(n.c_str() + l); };
        if (n == "amb_on") return 0;
        if (n == "spk_n") return 1;
        int k;
        if ((k = num("spk_az")) >= 0 && k < MAXSPK) return 100 + k;
        if ((k = num("spk_el")) >= 0 && k < MAXSPK) return 200 + k;
        if ((k = num("spk_dist")) >= 0 && k < MAXSPK) return 300 + k;
        if ((k = num("amb_az")) >= 0 && k < NS) return 400 + k;
        if ((k = num("amb_el")) >= 0 && k < NS) return 500 + k;
        if ((k = num("amb_dist")) >= 0 && k < NS) return 600 + k;
        if ((k = num("amb_w")) >= 0 && k < NS) return 700 + k;
        if ((k = num("amb_mot")) >= 0 && k < NS) return 800 + k;
        if ((k = num("amb_spd")) >= 0 && k < NS) return 900 + k;
        if ((k = num("amb_rng")) >= 0 && k < NS) return 1000 + k;
        if ((k = num("amb_sync")) >= 0 && k < NS) return 1100 + k;
        if ((k = num("amb")) >= 0 && k < NS) return 1200 + k;
        return -1;
    }
    void setp(int i, double v) {
        float f = (float) v;
        if (i == 0) on = v > 0.5;
        else if (i == 1) { nSpk = std::clamp((int) v, 2, MAXSPK); dirty = 8; }
        else if (i >= 100 && i < 200) { spAz[i - 100] = f; dirty = 8; }
        else if (i >= 200 && i < 300) { spEl[i - 200] = f; dirty = 8; }
        else if (i >= 300 && i < 400) { spDist[i - 300] = std::max(0.3f, f); dirty = 8; }
        else if (i >= 400 && i < 500) { srcAz[i - 400] = f; base[i - 400] = f; }
        else if (i >= 500 && i < 600) srcEl[i - 500] = f;
        else if (i >= 600 && i < 700) srcDist[i - 600] = std::max(0.3f, f);
        else if (i >= 700 && i < 800) srcW[i - 700] = f;
        else if (i >= 800 && i < 900) srcMot[i - 800] = (int) std::lround(v);
        else if (i >= 900 && i < 1000) srcSpd[i - 900] = f;
        else if (i >= 1000 && i < 1100) srcRng[i - 1000] = f;
        else if (i >= 1100 && i < 1200) srcSync[i - 1100] = v > 0.5;
        else if (i >= 1200 && i < 1300) srcOn[i - 1200] = v > 0.5;
    }
    void prepare(double sampleRate) {
        sr = sampleRate;
        int maxDelay = (int) (sr * 0.12) + 4;
        for (auto& d : dl) { d.assign((size_t) maxDelay, 0.0f); }
        dw = 0; dirty = 1;
        for (auto& s : lp) s = 0;
    }
    bool active() const { return on; }
    bool sourceOn(int s) const { return on && srcOn[s]; }
    // ch: the 12 strip signals; out: speakers (adds); phL / phR: the headphones (adds); bpm: the tempo
    void process(const float* const* ch, float* const* out, int numOut, float* phL, float* phR, int n, double bpm) {
        if (!on) return;
        if (dirty > 0 && --dirty == 0) design();
        int nOut = std::min(nSpk, numOut);
        float B[CH][SUBMAX];
        for (auto& b : B) std::fill(b, b + n, 0.0f);
        float stereo[2][SUBMAX]; std::fill(stereo[0], stereo[0] + n, 0.0f); std::fill(stereo[1], stereo[1] + n, 0.0f);
        double dt = n / sr;
        for (int s = 0; s < NS; ++s) {
            if (!srcOn[s] || ch[s] == nullptr) { level[s] = 0; continue; }
            // motion
            double spd = srcSpd[s] / 100.0;
            double rate = srcSync[s] ? (bpm > 20 ? bpm / 60.0 : 1.5) / (4.0 * (1 + std::round((1.0 - spd) * 7.0))) : 0.02 + spd * spd * 1.5;   // turns (cycles) per second
            phase[s] += rate * dt; phase[s] -= std::floor(phase[s]);
            float az = base[s], el = srcEl[s];
            switch (srcMot[s]) {
                case 1: az = base[s] + (float) (phase[s] * 360.0); break;                                       // circle
                case 2: az = base[s] + srcRng[s] * (float) std::sin(phase[s] * 6.2831853); break;               // swing
                case 3: { wanderT[s] += dt * (0.3 + spd * 2.0); float a = (float) (std::sin(wanderT[s] * 1.3) + std::sin(wanderT[s] * 0.71 + 1.0) * 0.6);
                          az = base[s] + srcRng[s] * a * 0.65f; el = srcEl[s] + 15.0f * (float) std::sin(wanderT[s] * 0.53); break; }                       // wander
                case 4: { float p = (float) phase[s]; az = base[s] + (p < 0.5f ? 0.0f : 180.0f); el = 180.0f * (p < 0.5f ? p * 2 : (p - 0.5f) * 2); if (el > 90) { el = 180 - el; } break; } // fly over
                default: break;
            }
            curAz[s].store(wrap(az)); curEl[s].store(std::clamp(el, -90.0f, 90.0f));
            float Y[CH]; sh(az, std::clamp(el, -90.0f, 90.0f), Y);
            // width: the higher orders fade (a wider, softer image)
            float w = std::clamp(srcW[s] / 100.0f, 0.0f, 1.0f);
            const float ow[4] = { 1.0f, 1.0f - w * 0.45f, 1.0f - w * 0.75f, 1.0f - w * 0.95f };
            for (int c = 0; c < CH; ++c) Y[c] *= ow[ordOf(c)];
            // distance: level (1/r beyond 1 m), air (high frequencies fade)
            float d = srcDist[s];
            float g = 1.0f / std::max(1.0f, d * 0.7f);
            float a = 1.0f - std::exp(-2.0f * 3.14159265f * std::min(20000.0f, 22000.0f / (1.0f + d * 0.25f)) / (float) sr);
            float pk = 0;
            if (nSpk == 2) {
                // two speakers: plain constant-power panning is sharper than ambisonics can be (front -30..+30, the back folds to the front, softer)
                float azw = wrap(az), back = 1.0f;
                if (std::abs(azw) > 90.0f) { azw = (azw > 0 ? 180.0f : -180.0f) - azw; back = 0.7f; }
                float pp = std::clamp((azw / std::max(1.0f, std::abs(spAz[1] - spAz[0]) * 0.5f)) * (1.0f - w * 0.5f), -1.0f, 1.0f);
                float gl = std::cos((pp + 1) * 0.785398f) * back, gr = std::sin((pp + 1) * 0.785398f) * back;
                if (spAz[0] > spAz[1]) std::swap(gl, gr);
                for (int i = 0; i < n; ++i) { lp[s] += a * (ch[s][i] - lp[s]); float x = lp[s] * g; pk = std::max(pk, std::abs(x)); stereo[0][i] += x * gl; stereo[1][i] += x * gr; for (int c = 0; c < CH; ++c) B[c][i] += x * Y[c]; }
                level[s] = pk;
                continue;
            }
            for (int i = 0; i < n; ++i) {
                lp[s] += a * (ch[s][i] - lp[s]);
                float x = lp[s] * g;
                pk = std::max(pk, std::abs(x));
                for (int c = 0; c < CH; ++c) B[c][i] += x * Y[c];
            }
            level[s] = pk;
        }
        // decode, with the speakers' distances made equal (delay + level)
        int N = (int) dl[0].size();
        for (int k = 0; k < nOut; ++k) {
            if (out[k] == nullptr) continue;
            const float* D = dec[k];
            auto& line = dl[k];
            int w0 = dw;
            for (int i = 0; i < n; ++i) {
                float y = 0;
                if (nSpk == 2) y = stereo[k & 1][i]; else for (int c = 0; c < CH; ++c) y += D[c] * B[c][i];
                line[(size_t) ((w0 + i) % N)] = y * spkGain[k];
                int rp = (w0 + i - spkDelay[k]) % N; if (rp < 0) rp += N;
                out[k][i] += line[(size_t) rp];
            }
        }
        dw = (dw + n) % N;
        // the headphones: two virtual cardioids at +-90 (from the first order), a little of the room
        if (phL && phR) for (int i = 0; i < n; ++i) { float w = B[0][i], y = B[1][i], x = B[3][i]; phL[i] += 0.5f * (w + y) + 0.12f * x; phR[i] += 0.5f * (w - y) + 0.12f * x; }
    }
    std::atomic<float> curAz[NS], curEl[NS];         // where each source is now (for the window)
    float level[NS] {};
    static float wrap(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }
    // the decoder (for tests)
    float decoderGain(int k, float az, float el) const {
        if (nSpk == 2) { float azw = wrap(az), back = 1.0f; if (std::abs(azw) > 90.0f) { azw = (azw > 0 ? 180.0f : -180.0f) - azw; back = 0.7f; }
            float pp = std::clamp(azw / std::max(1.0f, std::abs(spAz[1] - spAz[0]) * 0.5f), -1.0f, 1.0f); float gl = std::cos((pp + 1) * 0.785398f) * back, gr = std::sin((pp + 1) * 0.785398f) * back;
            if (spAz[0] > spAz[1]) std::swap(gl, gr); juce_unused_el(el); return k == 0 ? gl : gr; }
        float Y[CH]; sh(az, el, Y); float y = 0; for (int c = 0; c < CH; ++c) y += dec[k][c] * Y[c]; return y; }
    static void juce_unused_el(float) {}
    int speakers() const { return nSpk; }
    void designNow() { design(); dirty = 0; }
private:
    double sr = 48000;
    bool on = false; int nSpk = 2; int dirty = 1;
    float spAz[MAXSPK], spEl[MAXSPK], spDist[MAXSPK];
    float srcAz[NS], srcEl[NS], srcDist[NS], srcW[NS] {}, srcSpd[NS] {}, srcRng[NS] {}, base[NS] {};
    int srcMot[NS] {}; bool srcSync[NS] {}, srcOn[NS] {};
    double phase[NS] {}, wanderT[NS] {};
    float lp[NS] {};
    float vx[NV], vy[NV], vz[NV];
    float dec[MAXSPK][CH] {};
    int spkDelay[MAXSPK] {}; float spkGain[MAXSPK] {};
    std::vector<float> dl[MAXSPK]; int dw = 0;
    static int ordOf(int acn) { return acn == 0 ? 0 : (acn < 4 ? 1 : (acn < 9 ? 2 : 3)); }
    // real spherical harmonics, ACN order, SN3D; azimuth in degrees (0 front, + right), elevation up
    static void sh(float azDeg, float elDeg, float* Y) {
        double phi = -azDeg * 3.14159265358979 / 180.0, th = elDeg * 3.14159265358979 / 180.0;
        double x = std::cos(th) * std::cos(phi), y = std::cos(th) * std::sin(phi), z = std::sin(th);
        shXYZ(x, y, z, Y);
    }
    static void shXYZ(double x, double y, double z, float* Y) {
        const double s3 = std::sqrt(3.0), s15 = std::sqrt(15.0), s58 = std::sqrt(5.0 / 8.0), s38 = std::sqrt(3.0 / 8.0);
        Y[0] = 1.0f;
        Y[1] = (float) y; Y[2] = (float) z; Y[3] = (float) x;
        Y[4] = (float) (s3 * x * y); Y[5] = (float) (s3 * y * z); Y[6] = (float) (0.5 * (3 * z * z - 1)); Y[7] = (float) (s3 * x * z); Y[8] = (float) (s3 / 2 * (x * x - y * y));
        Y[9] = (float) (s58 * y * (3 * x * x - y * y)); Y[10] = (float) (s15 * x * y * z); Y[11] = (float) (s38 * y * (5 * z * z - 1)); Y[12] = (float) (0.5 * z * (5 * z * z - 3));
        Y[13] = (float) (s38 * x * (5 * z * z - 1)); Y[14] = (float) (s15 / 2 * z * (x * x - y * y)); Y[15] = (float) (s58 * x * (x * x - 3 * y * y));
    }
    // AllRAD
    void design() {
        const int N = nSpk;
        // the real speakers (+ imaginary ones above / below when the layout has none there)
        double P[MAXSPK + 2][3]; int nP = N;
        double minEl = 90, maxEl = -90;
        for (int k = 0; k < N; ++k) {
            double phi = -spAz[k] * M_PI / 180.0, th = std::clamp((double) spEl[k], -89.0, 89.0) * M_PI / 180.0;
            P[k][0] = std::cos(th) * std::cos(phi); P[k][1] = std::cos(th) * std::sin(phi); P[k][2] = std::sin(th);
            minEl = std::min(minEl, (double) spEl[k]); maxEl = std::max(maxEl, (double) spEl[k]);
        }
        int top = -1, bot = -1;
        if (maxEl < 45) { top = nP; P[nP][0] = 0; P[nP][1] = 0; P[nP][2] = 1; ++nP; }
        if (minEl > -45) { bot = nP; P[nP][0] = 0; P[nP][1] = 0; P[nP][2] = -1; ++nP; }
        // the convex hull: every triangle with all the other points on one side
        struct Tri { int a, b, c; double inv[3][3]; };
        std::vector<Tri> tris;
        for (int a = 0; a < nP; ++a) for (int b = a + 1; b < nP; ++b) for (int c = b + 1; c < nP; ++c) {
            double u[3] = { P[b][0] - P[a][0], P[b][1] - P[a][1], P[b][2] - P[a][2] }, v[3] = { P[c][0] - P[a][0], P[c][1] - P[a][1], P[c][2] - P[a][2] };
            double nn[3] = { u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
            double len = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]); if (len < 1e-6) continue;
            int pos = 0, neg = 0;
            for (int q = 0; q < nP; ++q) { if (q == a || q == b || q == c) continue; double d = (P[q][0] - P[a][0]) * nn[0] + (P[q][1] - P[a][1]) * nn[1] + (P[q][2] - P[a][2]) * nn[2];
                if (d > 1e-9 * len) ++pos; else if (d < -1e-9 * len) ++neg; }
            if (pos > 0 && neg > 0) continue;
            // the inverse of the 3x3 matrix of the three speaker directions (VBAP)
            double m[3][3] = { { P[a][0], P[a][1], P[a][2] }, { P[b][0], P[b][1], P[b][2] }, { P[c][0], P[c][1], P[c][2] } };
            double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            if (std::abs(det) < 1e-9) continue;
            Tri t { a, b, c, {} };
            t.inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det; t.inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det; t.inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
            t.inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det; t.inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det; t.inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
            t.inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det; t.inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det; t.inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
            tris.push_back(t);
        }
        // the imaginary speakers give their sound to the real ones next to them
        std::vector<int> nbTop, nbBot;
        for (auto& t : tris) for (int q : { t.a, t.b, t.c }) {
            bool hasT = t.a == top || t.b == top || t.c == top, hasB = t.a == bot || t.b == bot || t.c == bot;
            if (q < N && hasT && std::find(nbTop.begin(), nbTop.end(), q) == nbTop.end()) nbTop.push_back(q);
            if (q < N && hasB && std::find(nbBot.begin(), nbBot.end(), q) == nbBot.end()) nbBot.push_back(q);
        }
        // each virtual speaker -> real gains (VBAP), then the decoder: D = G * Y^T * weights / V
        static const float rE[4] = { 1.0f, 0.861f, 0.612f, 0.305f };
        double D[MAXSPK][CH] = {};
        for (int v = 0; v < NV; ++v) {
            double p[3] = { vx[v], vy[v], vz[v] };
            double g[MAXSPK + 2] = {};
            double best = -1e9; const Tri* bt = nullptr; double bg[3] = {};
            for (auto& t : tris) {
                double gg[3]; for (int r = 0; r < 3; ++r) gg[r] = p[0] * t.inv[0][r] + p[1] * t.inv[1][r] + p[2] * t.inv[2][r];
                double mn = std::min({ gg[0], gg[1], gg[2] });
                if (mn > best) { best = mn; bt = &t; bg[0] = gg[0]; bg[1] = gg[1]; bg[2] = gg[2]; }
            }
            if (bt == nullptr) continue;
            for (auto& x : bg) x = std::max(0.0, x);
            double e = std::sqrt(bg[0] * bg[0] + bg[1] * bg[1] + bg[2] * bg[2]); if (e < 1e-9) continue;
            int idx[3] = { bt->a, bt->b, bt->c };
            for (int r = 0; r < 3; ++r) g[idx[r]] += bg[r] / e;
            if (top >= 0 && g[top] > 0 && !nbTop.empty()) { for (int q : nbTop) g[q] += g[top] / std::sqrt((double) nbTop.size()); g[top] = 0; }
            if (bot >= 0 && g[bot] > 0 && !nbBot.empty()) { for (int q : nbBot) g[q] += g[bot] / std::sqrt((double) nbBot.size()); g[bot] = 0; }
            float Y[CH]; shXYZ(p[0], p[1], p[2], Y);
            for (int k = 0; k < N; ++k) if (g[k] != 0) for (int c = 0; c < CH; ++c) D[k][c] += g[k] * Y[c] * (2 * ordOf(c) + 1) * rE[ordOf(c)] / NV;
        }
        // energy: a source anywhere gives (on average) the same power as one speaker at full level
        double sumE = 0; int cnt = 0;
        for (int v = 0; v < NV; v += 3) { float Y[CH]; shXYZ(vx[v], vy[v], vz[v], Y); double e = 0; for (int k = 0; k < N; ++k) { double y = 0; for (int c = 0; c < CH; ++c) y += D[k][c] * Y[c]; e += y * y; } sumE += e; ++cnt; }
        double norm = cnt > 0 && sumE > 1e-12 ? 1.0 / std::sqrt(sumE / cnt) : 1.0;
        for (int k = 0; k < MAXSPK; ++k) for (int c = 0; c < CH; ++c) dec[k][c] = k < N ? (float) (D[k][c] * norm) : 0.0f;
        // the speakers' distances: the farthest plays first, the nearer ones wait and play softer
        float far = 0.3f; for (int k = 0; k < N; ++k) far = std::max(far, spDist[k]);
        for (int k = 0; k < N; ++k) {
            spkDelay[k] = std::clamp((int) std::lround((far - spDist[k]) / 343.0 * sr), 0, (int) dl[0].size() - 2);
            spkGain[k] = std::clamp(spDist[k] / far, 0.1f, 1.0f);
        }
    }
};

} // namespace arco
