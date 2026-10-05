#pragma once
// ARCO 1.2: ten send effects written in C++ (FLANGER, PING-PONG, AUTO-WAH, OCTAVER, TAPE, FILTER, RING MOD,
// STUTTER, ROTARY, CRUSH). Each strip sends to them with its own SEND; the returns are added to the dry mix
// before the master (so GLUE / the limiter / SPACE treat them like everything else).
// An effect that nobody sends to (and that has rung out) costs nothing.
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <algorithm>
#include <JuceHeader.h>
#include "../Model/ParamTable.h"

namespace arco {

struct XDelay {                       // a fractional delay line
    std::vector<float> d; int w = 0;
    void init(int n) { d.assign((size_t) std::max(4, n), 0.0f); w = 0; }
    void push(float x) { d[(size_t) w] = x; w = (w + 1) % (int) d.size(); }
    float read(double delay) const {  // delay in samples (>= 1), Hermite-free linear is enough for modulation
        int n = (int) d.size(); delay = std::clamp(delay, 1.0, (double) n - 3);
        double rp = w - delay; while (rp < 0) rp += n;
        int i0 = (int) rp; float f = (float) (rp - i0); int i1 = (i0 + 1) % n;
        return d[(size_t) i0] + (d[(size_t) i1] - d[(size_t) i0]) * f; }
};
struct XSvf {                         // TPT state-variable filter
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    // returns low, band, high
    void tick(float x, float g, float k, float& lp, float& bp, float& hp) {
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = x - k * v1 - v2;
        if (!std::isfinite(ic1) || !std::isfinite(ic2)) reset(); }
};
struct XOnePole { float z = 0; float lp(float x, float a) { z += a * (x - z); return z; } };

class ExtraFx {
public:
    static constexpr int NFX = 10;
    explicit ExtraFx() {
        int i = 0;
        for (auto& d : extraParams()) { idx[d.id] = i++; vals.push_back(d.def); }
        for (int e = 0; e < NFX; ++e) for (int s = 0; s < 12; ++s) sendIdx[e][s] = idx[std::string(keys[e]) + std::to_string(s)];
    }
    int paramIndex(const std::string& n) const { auto it = idx.find(n); return it == idx.end() ? -1 : it->second; }
    void setp(int i, double v) { if (i >= 0 && i < (int) vals.size()) vals[(size_t) i] = v; }

    void init(double sampleRate) {
        sr = sampleRate;
        for (auto& f : fl) f.init((int) (0.03 * sr));
        for (auto& p : pp) p.init((int) (8.5 * sr));
        for (auto& o : oc) o.init((int) (0.25 * sr));
        for (auto& t : tp) t.init((int) (0.05 * sr));
        for (auto& r : rt) r.init((int) (0.01 * sr));
        stuBuf.assign((size_t) (4.5 * sr), 0.0f); stuW = 0;
        for (auto& s : sv) s.reset();
        std::fill(std::begin(tail), std::end(tail), 0);
        std::fill(std::begin(g), std::end(g), 0.0f);
    }

    // ch: the 12 strip signals; outL/outR: added to; spc: SPACE send, added to; bpm: the tempo; pitch: LIVE 1 note (MIDI) or <= 0
    void process(const float* const* ch, float* outL, float* outR, float* spc, int n, const float* bpm, const float* pitch, float* ringL[NFX]) {
        for (int e = 0; e < NFX; ++e) {
            // gather the sends (smoothed, squared response like the other sends)
            bool any = false; float tgt[12];
            for (int s = 0; s < 12; ++s) { float v = (float) vals[(size_t) sendIdx[e][s]] / 100.0f; tgt[s] = v * v; any |= tgt[s] > 0.0f; }
            if (!any && tail[e] <= 0) { if (ringL[e]) std::fill(ringL[e], ringL[e] + n, 0.0f); continue; }
            tail[e] = any ? (int) (10.0 * sr) : tail[e] - n;            // effects with echoes ring out for up to 10 s
            for (int i = 0; i < n; ++i) {
                float x = 0;
                for (int s = 0; s < 12; ++s) { float& gg = g[e * 12 + s]; gg += 0.002f * (tgt[s] - gg); if (gg > 1e-6f) x += ch[s][i] * gg; }
                in[i] = x;
            }
            float L[SUBMAX], R[SUBMAX];
            switch (e) {
                case 0: flanger(n, L, R, bpm); break;
                case 1: pingpong(n, L, R, bpm); break;
                case 2: wah(n, L, R, bpm); break;
                case 3: octaver(n, L, R); break;
                case 4: tape(n, L, R); break;
                case 5: filter(n, L, R, bpm); break;
                case 6: ringmod(n, L, R, pitch); break;
                case 7: stutter(n, L, R, bpm); break;
                case 8: rotary(n, L, R); break;
                case 9: crush(n, L, R); break;
            }
            float ret = p(e, "ret") / 100.0f; ret = ret * ret * 1.4f;
            float sp = has(e, "spc") ? p(e, "spc") / 100.0f : 0.0f;
            for (int i = 0; i < n; ++i) {
                float l = L[i] * ret, r = R[i] * ret;
                if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0; }
                outL[i] += l; outR[i] += r;
                if (sp > 0) spc[i] += 0.5f * (l + r) * sp;
            }
            if (ringL[e]) for (int i = 0; i < n; ++i) ringL[e][i] = L[i] * ret;
        }
    }
    static constexpr int SUBMAX = 64;
    std::vector<double> vals;

private:
    double sr = 48000;
    std::map<std::string, int> idx;
    float g[NFX * 12] {};
    int tail[NFX] {};
    float in[SUBMAX] {};
    static constexpr const char* keys[NFX] = { "flg", "png", "wah", "oct", "tap", "flt", "rng", "stu", "vib", "crs" };
    int sendIdx[NFX][12] {};
    float p(int e, const char* k) const {
        auto it = idx.find(std::string(keys[e]) + "_" + k);
        return it == idx.end() ? 0.0f : (float) vals[(size_t) it->second]; }
    bool has(int e, const char* k) const { return idx.count(std::string(keys[e]) + "_" + k) > 0; }
    static double beatsOf(float v) {                                 // the tempo divisions (as in DELAY)
        static const double B[8] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
        return B[std::clamp((int) (v / 100.0f * 7.99f), 0, 7)]; }
    static float tempo(const float* bpm, int i) { float b = bpm ? bpm[i] : 90.0f; return b > 20.0f && b < 400.0f ? b : 90.0f; }
    float phase[NFX][2] {};

    // ---- FLANGER
    XDelay fl[2];
    float flfb[2] {};
    void flanger(int n, float* L, float* R, const float* bpm) {
        float rate = p(0, "rate") / 100.0f, depth = p(0, "depth") / 100.0f, fb = p(0, "fb") / 100.0f * 0.92f, man = p(0, "man") / 100.0f;
        double hz = rate < 0.5f ? 0.03 + rate * 2 * 1.2 : 0.0;        // left half: free (slow), right half: in tempo
        for (int i = 0; i < n; ++i) {
            if (rate >= 0.5f) hz = tempo(bpm, i) / 60.0 / (rate < 0.66f ? 8 : (rate < 0.83f ? 4 : 2));   // 2, 1, 1/2 bar
            for (int c = 0; c < 2; ++c) {
                float& ph = phase[0][c]; if (c == 0) { ph += (float) (hz / sr); if (ph >= 1) ph -= 1; }
                float pc = c == 0 ? ph : std::fmod(ph + 0.25f, 1.0f);
                float tri = 1 - 2 * std::abs(pc - 0.5f) * 2; tri = 0.5f + 0.5f * tri;
                double dms = 0.25 + man * 4.0 + depth * 5.0 * tri;
                float y = fl[c].read(dms * 0.001 * sr);
                fl[c].push(in[i] + y * fb);
                (c ? R : L)[i] = 0.5f * (in[i] + y);
            }
        }
    }
    // ---- PING-PONG
    XDelay pp[2]; XOnePole ppt[2];
    void pingpong(int n, float* L, float* R, const float* bpm) {
        float fb = p(1, "fb") / 100.0f * 0.9f, tone = p(1, "tone") / 100.0f, width = p(1, "width") / 100.0f;
        float a = 0.05f + tone * tone * 0.9f;
        const double bts = beatsOf(p(1, "time"));
        for (int i = 0; i < n; ++i) {
            double d = 60.0 / tempo(bpm, i) * bts * sr;
            float yl = pp[0].read(d), yr = pp[1].read(d);
            pp[0].push(ppt[0].lp(in[i] + yr * fb, a));
            pp[1].push(ppt[1].lp(yl * fb + 0.0f, a) + 0.0f);
            float m = 0.5f * (yl + yr);
            L[i] = m + (yl - m) * width; R[i] = m + (yr - m) * width;
        }
    }
    // ---- AUTO-WAH
    XSvf sv[4]; float env = 0;
    void wah(int n, float* L, float* R, const float* bpm) {
        float sens = p(2, "sens") / 100.0f, range = p(2, "range") / 100.0f, reso = p(2, "reso") / 100.0f, lfo = p(2, "mode") / 100.0f;
        float att = 1 - std::exp(-1.0f / (0.004f * (float) sr)), rel = 1 - std::exp(-1.0f / (0.15f * (float) sr));
        float k = 2.0f - 1.9f * reso;
        for (int i = 0; i < n; ++i) {
            float a = std::abs(in[i]) * (1 + sens * 30.0f);
            env += (a > env ? att : rel) * (a - env);
            float ph = (phase[2][0] += (float) (tempo(bpm, i) / 60.0 / sr)); if (phase[2][0] >= 1) phase[2][0] -= 1;
            float lf = 0.5f - 0.5f * std::cos(6.2831853f * ph);
            float ctl = std::clamp(env, 0.0f, 1.0f) * (1 - lfo) + lf * lfo;
            float hz = 280.0f * std::pow(2.0f, ctl * (1.0f + range * 3.6f));
            float gg = std::tan(3.14159265f * std::min(hz, (float) sr * 0.45f) / (float) sr);
            float lp, bp, hp; sv[0].tick(in[i], gg, k, lp, bp, hp);
            float y = (bp * (1.0f + reso) + lp * 0.15f) * 4.0f;
            L[i] = y; R[i] = y;
        }
    }
    // ---- OCTAVER: three pitch shifters (two crossfaded taps each)
    XDelay oc[1]; double ocPh[3] {};
    float shift(int which, double ratio, double win) {
        double& ph = ocPh[which];
        ph += (1.0 - ratio) / win; ph -= std::floor(ph);
        double p2 = ph + 0.5; p2 -= std::floor(p2);
        float a = oc[0].read(1 + ph * win), b = oc[0].read(1 + p2 * win);
        float wa = (float) std::sin(3.14159265 * ph), wb = (float) std::sin(3.14159265 * p2);
        return a * wa * wa + b * wb * wb;
    }
    XOnePole octLp[2];
    void octaver(int n, float* L, float* R) {
        float dn = p(3, "down") / 100.0f, up = p(3, "up") / 100.0f, fi = p(3, "fifth") / 100.0f, tone = p(3, "tone") / 100.0f;
        double win = 0.045 * sr;
        float a = 0.03f + tone * tone * 0.9f;
        for (int i = 0; i < n; ++i) {
            oc[0].push(in[i]);
            float y = 0;
            if (dn > 0) y += dn * shift(0, 0.5, win * 1.6);
            if (up > 0) y += up * shift(1, 2.0, win);
            if (fi > 0) y += fi * shift(2, 1.4983, win);
            y = octLp[0].lp(y, a);
            L[i] = y; R[i] = y;
        }
    }
    // ---- TAPE
    XDelay tp[2]; XOnePole tpt[2], bump[2];
    void tape(int n, float* L, float* R) {
        float drive = p(4, "drive") / 100.0f, wow = p(4, "wow") / 100.0f, flut = p(4, "flut") / 100.0f, tone = p(4, "tone") / 100.0f;
        float a = 0.08f + tone * 0.85f, dg = 1 + drive * 6;
        for (int i = 0; i < n; ++i) {
            phase[4][0] += (float) (0.55 / sr); if (phase[4][0] >= 1) phase[4][0] -= 1;
            phase[4][1] += (float) (6.8 / sr); if (phase[4][1] >= 1) phase[4][1] -= 1;
            float x = std::tanh(in[i] * dg) / std::tanh(dg) ;
            for (int c = 0; c < 2; ++c) {
                double mod = 0.0015 * wow * std::sin(6.2831853 * (phase[4][0] + c * 0.13)) + 0.00018 * flut * std::sin(6.2831853 * (phase[4][1] + c * 0.31));
                float y = tp[c].read((0.006 + mod) * sr);
                tp[c].push(x);
                float lo = bump[c].lp(y, 0.012f);
                y = tpt[c].lp(y + lo * 0.25f, a) * 0.6f;
                (c ? R : L)[i] = y;
            }
        }
    }
    // ---- FILTER
    void filter(int n, float* L, float* R, const float* bpm) {
        int type = std::clamp((int) (p(5, "type") / 100.0f * 2.99f), 0, 2);
        float cut = p(5, "cut") / 100.0f, res = p(5, "res") / 100.0f, depth = p(5, "depth") / 100.0f;
        float k = 2.0f - 1.96f * std::pow(res, 0.8f);
        const double beats = beatsOf(p(5, "rate")) * 4.0;            // the sweep takes 4x the division (1 bar at 1/4)
        for (int i = 0; i < n; ++i) {
            phase[5][0] += (float) (tempo(bpm, i) / 60.0 / beats / sr); if (phase[5][0] >= 1) phase[5][0] -= 1;
            for (int c = 0; c < 2; ++c) {
                float lf = 0.5f - 0.5f * std::cos(6.2831853f * (phase[5][0] + c * 0.05f));
                float oct = (cut - 0.5f) * 8.0f + (lf - 0.5f) * depth * 6.0f;
                float hz = std::clamp(900.0f * std::pow(2.0f, oct), 30.0f, (float) sr * 0.45f);
                float gg = std::tan(3.14159265f * hz / (float) sr);
                float lp, bp, hp; sv[1 + c].tick(in[i], gg, k, lp, bp, hp);
                (c ? R : L)[i] = type == 0 ? lp : (type == 1 ? bp * 1.5f : hp);
            }
        }
    }
    // ---- RING MOD
    XOnePole rlp; double rph = 0; float rhz = 200;
    void ringmod(int n, float* L, float* R, const float* pitch) {
        float fq = p(6, "freq") / 100.0f, follow = p(6, "follow") / 100.0f, shape = p(6, "shape") / 100.0f, tone = p(6, "tone") / 100.0f;
        float fixedHz = 20.0f * std::pow(100.0f, fq);
        for (int i = 0; i < n; ++i) {
            float tgt = fixedHz;
            if (follow > 0.5f && pitch && pitch[i] > 20) {
                static const float RAT[5] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
                tgt = 440.0f * std::pow(2.0f, (pitch[i] - 69) / 12.0f) * RAT[std::clamp((int) (fq * 4.99f), 0, 4)];
            }
            rhz += 0.001f * (tgt - rhz);
            rph += rhz / sr; rph -= std::floor(rph);
            float s = (float) std::sin(6.2831853 * rph), sq = s >= 0 ? 1.0f : -1.0f;
            float car = s + (sq - s) * shape * 0.8f;
            float y = rlp.lp(in[i] * car, 0.05f + tone * tone * 0.9f);
            L[i] = y; R[i] = y;
        }
    }
    // ---- STUTTER: on the grid, sometimes repeat the last slice a few times
    std::vector<float> stuBuf; int stuW = 0;
    double stuClock = 0; int stuLeft = 0; double stuPos = 0, stuStart = 0, stuLen = 0, stuRate = 1; juce::Random rnd { 7 };
    void stutter(int n, float* L, float* R, const float* bpm) {
        static const double G[5] = { 0.125, 0.25, 0.5, 1.0, 2.0 };       // 1/32 .. 1/2
        double grid = G[std::clamp((int) (p(7, "div") / 100.0f * 4.99f), 0, 4)];
        float chance = p(7, "chance") / 100.0f, reps = p(7, "len") / 100.0f, drop = p(7, "pitch") / 100.0f;
        int N = (int) stuBuf.size();
        for (int i = 0; i < n; ++i) {
            stuBuf[(size_t) stuW] = in[i]; stuW = (stuW + 1) % N;
            double beatSamples = 60.0 / tempo(bpm, i) * sr;
            stuClock += 1.0 / (beatSamples * grid);
            if (stuClock >= 1.0) {                                  // a grid line
                stuClock -= std::floor(stuClock);
                if (stuLeft > 0) { --stuLeft; stuPos = 0; stuRate *= (1.0 - drop * 0.18); }
                else if (rnd.nextFloat() < chance) {
                    stuLen = std::min(beatSamples * grid, (double) N - 10);
                    stuStart = stuW - stuLen; while (stuStart < 0) stuStart += N;
                    stuLeft = 1 + (int) (reps * 7); stuPos = 0; stuRate = 1.0;
                }
            }
            float y = 0;
            if (stuLeft > 0 && stuLen > 0) {
                double rp = stuStart + stuPos; while (rp >= N) rp -= N;
                int i0 = (int) rp; float f = (float) (rp - i0);
                y = stuBuf[(size_t) i0] + (stuBuf[(size_t) ((i0 + 1) % N)] - stuBuf[(size_t) i0]) * f;
                double env = std::min({ 1.0, stuPos / 64.0, (stuLen - stuPos) / 64.0 });
                y *= (float) std::max(0.0, env);
                stuPos += stuRate; if (stuPos >= stuLen) stuPos = stuLen - 1;
            }
            L[i] = y; R[i] = y;
        }
    }
    // ---- ROTARY
    XDelay rt[2]; XOnePole xo; float spH = 0.8f, spD = 0.7f; double angH = 0, angD = 0;
    void rotary(int n, float* L, float* R) {
        float fast = p(8, "speed") / 100.0f, drive = p(8, "drive") / 100.0f, bal = p(8, "horn") / 100.0f, width = p(8, "width") / 100.0f;
        float tH = fast > 0.5f ? 6.8f : 0.8f, tD = fast > 0.5f ? 5.9f : 0.67f;
        float dg = 1 + drive * 5;
        for (int i = 0; i < n; ++i) {
            spH += (tH - spH) * (float) (1.6 / sr); spD += (tD - spD) * (float) (0.45 / sr);   // the horn speeds up faster than the drum
            angH += spH / sr; angD += spD / sr; angH -= std::floor(angH); angD -= std::floor(angD);
            float x = std::tanh(in[i] * dg) / std::tanh(dg);
            float lo = xo.lp(x, 0.07f), hi = x - lo;
            float sh = (float) std::sin(6.2831853 * angH), ch = (float) std::cos(6.2831853 * angH);
            float sd = (float) std::sin(6.2831853 * angD);
            rt[0].push(hi);
            float hd = rt[0].read((0.0012 + 0.0006 * sh) * sr) * (0.75f + 0.25f * ch);
            float dd = lo * (0.8f + 0.2f * sd);
            float hL = hd * (0.5f + 0.5f * sh * width), hR = hd * (0.5f - 0.5f * sh * width);
            float dL = dd * (0.5f + 0.3f * sd * width), dR = dd * (0.5f - 0.3f * sd * width);
            L[i] = (hL * bal + dL * (1 - bal)) * 1.6f; R[i] = (hR * bal + dR * (1 - bal)) * 1.6f;
        }
    }
    // ---- CRUSH
    float hold = 0; double hcnt = 0; XOnePole clp;
    void crush(int n, float* L, float* R) {
        float bits = 16.0f - p(9, "bits") / 100.0f * 13.0f, ds = 1.0f + p(9, "rate") / 100.0f * 39.0f, tone = p(9, "tone") / 100.0f;
        float q = std::pow(2.0f, bits - 1);
        for (int i = 0; i < n; ++i) {
            hcnt += 1.0; if (hcnt >= ds) { hcnt -= ds; hold = std::round(std::clamp(in[i], -1.0f, 1.0f) * q) / q; }
            float y = clp.lp(hold, 0.05f + tone * tone * 0.9f);
            L[i] = y; R[i] = y;
        }
    }
};

} // namespace arco
