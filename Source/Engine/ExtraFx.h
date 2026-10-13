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
    static constexpr int NFX = 16;
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
        initPedal(); initFeedback(); initNew();
        for (auto& s : sv) s.reset();
        std::fill(std::begin(tail), std::end(tail), 0);
        std::fill(std::begin(g), std::end(g), 0.0f);
    }

    // ch: the 12 strip signals; outL/outR: added to; spc: SPACE send, added to; bpm: the tempo; pitch: LIVE 1 note (MIDI) or <= 0
    void process(const float* const* ch, float* outL, float* outR, float* spc, int n, const float* bpm, const float* pitch, float* ringL[NFX], const float* synth = nullptr) {
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
                case 10: pedal(n, L, R, bpm); break;
                case 11: feedback(n, L, R); break;
                case 12: vocoder(n, L, R, pitch, synth); break;
                case 13: pitchShift(n, L, R); break;
                case 14: spring(n, L, R); break;
                case 15: talk(n, L, R, bpm); break;
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
    static constexpr const char* keys[NFX] = { "flg", "png", "wah", "oct", "tap", "flt", "rng", "stu", "vib", "crs", "pdl", "fbk", "voc", "pit", "spr", "tlk" };
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

    // ---- PEDAL: the piano's sustain pedal. Pedal down: the dampers lift and 60 strings (C1-B5) ring with what you play -
    // your notes go on, and the other strings answer them softly. Pedal up: the dampers fall (a short, soft stop).
    // AUTO PEDAL re-pedals by itself in tempo (lift - press), so harmonies do not smear.
    static constexpr int NSTR = 60;
    float panL[NSTR] {}, panR[NSTR] {};
    float strC[NSTR] {}, strS[NSTR] {}, strRe[NSTR] {}, strIm[NSTR] {}, strR[NSTR] {}, strG[NSTR] {};
    float pdlDamp = 0;                       // 0 dampers on the strings .. 1 lifted (smoothed)
    double pdlClock = 0; XOnePole pdlLo, pdlHi;
    XDelay body[4]; float bodyLp[4] {};
    void initPedal() {
        for (int k = 0; k < NSTR; ++k) {
            double f = 440.0 * std::pow(2.0, (24 + k - 69) / 12.0);
            double w = 2 * 3.14159265358979 * f / sr;
            strC[k] = (float) std::cos(w); strS[k] = (float) std::sin(w); strRe[k] = strIm[k] = 0; strR[k] = 0.9f;
            strG[k] = 1.0f;
            float pan = -0.55f + 1.1f * (float) k / (NSTR - 1), a = (pan + 1.0f) * 0.785398f;
            panL[k] = std::cos(a) * 1.41421f; panR[k] = std::sin(a) * 1.41421f;
        }
        static const double BL[4] = { 0.0297, 0.0371, 0.0411, 0.0437 };
        for (int j = 0; j < 4; ++j) { body[j].init((int) (BL[j] * sr) + 4); bodyLp[j] = 0; }
        pdlDamp = 0; pdlClock = 0;
    }
    void pedal(int n, float* L, float* R, const float* bpm) {
        float ring = p(10, "ring") / 100.0f, bright = p(10, "bright") / 100.0f, bodyAmt = p(10, "body") / 100.0f, autoP = p(10, "auto") / 100.0f;
        bool down = p(10, "down") > 0.5f;
        // the strings' ring: 2 .. 24 s (low strings longer), dampers: 0.12 s
        double t60 = 2.0 + ring * ring * 22.0;
        for (int i = 0; i < n; ++i) {
            bool lifted = down;
            if (autoP > 0.04f) {                                      // AUTO: held down, lifted for ~90 ms at each change (2 bars / bar / half bar / beat)
                static const double EVERY[4] = { 8.0, 4.0, 2.0, 1.0 };
                double beats = EVERY[std::clamp((int) (autoP * 3.99f), 0, 3)];
                double periodSec = beats * 60.0 / tempo(bpm, i);
                pdlClock += 1.0 / (periodSec * sr); if (pdlClock >= 1.0) pdlClock -= 1.0;
                lifted = pdlClock * periodSec > 0.09;
            }
            float tgt = lifted ? 1.0f : 0.0f;
            pdlDamp += (tgt - pdlDamp) * (tgt > pdlDamp ? 0.0025f : 0.0012f);   // dampers lift in ~8 ms, fall in ~20 ms (no click)
            if ((i & 15) == 0) {                                      // the decay of every string follows the dampers
                for (int k = 0; k < NSTR; ++k) {
                    double f = 32.7 * std::pow(2.0, k / 12.0);
                    double tUp = t60 * std::pow(65.0 / f, 0.33);       // low strings ring longer
                    double t = 0.12 + (tUp - 0.12) * pdlDamp;
                    strR[k] = (float) std::exp(-6.907755 / (t * sr));
                    strG[k] = (1.0f - strR[k]) * (1.0f + bright * (float) k / NSTR * 1.5f);
                }
            }
            // what excites the strings: your sound, brighter or darker
            float lo = pdlLo.lp(in[i], 0.05f), hi = in[i] - lo;
            float x = lo + hi * (0.35f + bright * 1.2f);
            float l = 0, r = 0;
            for (int k = 0; k < NSTR; ++k) {
                float re = strRe[k], im = strIm[k], rr = strR[k];
                float nre = rr * (re * strC[k] - im * strS[k]) + strG[k] * x;
                float nim = rr * (re * strS[k] + im * strC[k]);
                strRe[k] = nre; strIm[k] = nim;
                l += nre * panL[k]; r += nre * panR[k];               // as the pianist hears it: low strings left, high right
            }
            // the soundboard: a short warm room around the strings (longer with the pedal down)
            float fbg = 0.55f + 0.33f * pdlDamp * (0.4f + 0.6f * ring);
            float o[4]; for (int j = 0; j < 4; ++j) { o[j] = body[j].read(body[j].d.size() - 3); bodyLp[j] += 0.35f * (o[j] - bodyLp[j]); o[j] = bodyLp[j]; }
            float h0 = o[0] + o[1] + o[2] + o[3];
            for (int j = 0; j < 4; ++j) body[j].push((0.5f * h0 - o[j]) * fbg + (l + r) * 0.25f * bodyAmt);
            l = l * 2.2f + (o[0] + o[2]) * bodyAmt * 0.9f; r = r * 2.2f + (o[1] + o[3]) * bodyAmt * 0.9f;
            if (!std::isfinite(l) || !std::isfinite(r)) { initPedal(); l = r = 0; }
            L[i] = std::tanh(l); R[i] = std::tanh(r);                 // never more than full scale
        }
    }

    // ---- FEEDBACK: like a guitar held to the amp - the note you play feeds itself and grows, at the pitch you played
    // (or its octave, its 12th ...). CATCH: each new note swells and is let go before it gets loud. LISTEN: it waits,
    // hears whatever sounds and slowly brings it up. HOLD: it grows to LEVEL and stays there. Always under control:
    // the loop gain is steered every sample towards LEVEL, the loop saturates softly and a limiter sits at the end.
    XDelay fbLine; XSvf fbBp, fbTone; float fbG = 0.9f, fbEnv = 0, inEnvF = 0, inEnvS = 0, fbD = 400, fbDT = 400, dcX = 0, dcY = 0, lim = 1;
    int fbState = 0;                         // 0 idle, 1 growing, 2 letting go
    double fbHz = 220, sinceOnset = 0, stableFor = 0; bool waitOnset = false, prevStop = false;
    // a small pitch follower (YIN) on the sound sent to it, at a quarter of the rate
    std::vector<float> pbuf, dfn, cm; int pw = 0, pdec = 0, pnew = 0; float pacc = 0; double pitchHz = 0, pitchConf = 0, pendingHz = 0;
    void initFeedback() {
        fbLine.init((int) (0.06 * sr)); fbBp.reset(); fbTone.reset(); fbG = 0.9f; fbEnv = inEnvF = inEnvS = 0; fbState = 0;
        fbD = fbDT = (float) (sr / 220.0); pbuf.assign(1024, 0.0f); dfn.assign(520, 0.0f); cm.assign(520, 1.0f); pw = 0; pdec = 0; pnew = 0; pacc = 0; pitchHz = 0; pitchConf = 0; lim = 1;
    }
    void trackPitch() {                      // YIN on the last 1024 samples (at sr / 4)
        const int W = 512, N = 1024; const double fs = sr / 4.0;
        int maxLag = std::min(W - 2, (int) (fs / 40.0)), minLag = std::max(2, (int) (fs / 1500.0));
        std::fill(dfn.begin(), dfn.end(), 0.0f); std::fill(cm.begin(), cm.end(), 1.0f);
        auto at = [&](int k) { return pbuf[(size_t) ((pw - N + k + N * 4) % N)]; };
        float energy = 0; for (int k = 0; k < N; ++k) energy += at(k) * at(k);
        if (energy / N < 1e-7f) { pitchConf = 0; return; }
        for (int tau = 1; tau <= maxLag; ++tau) { float d = 0; for (int k = 0; k < W; ++k) { float e = at(k) - at(k + tau); d += e * e; } dfn[(size_t) tau] = d; }
        float run = 0; int best = -1;
        for (int tau = 1; tau <= maxLag; ++tau) { run += dfn[(size_t) tau]; cm[(size_t) tau] = run > 0 ? dfn[(size_t) tau] * tau / run : 1.0f; }
        for (int tau = minLag; tau <= maxLag; ++tau) if (cm[(size_t) tau] < 0.15f) { while (tau + 1 <= maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0) { pitchConf = 0; return; }
        double t = best;
        if (best > 1 && best < maxLag) { float a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1]; double den = a - 2 * b + c; if (std::abs(den) > 1e-9) t += 0.5 * (a - c) / den; }
        pitchHz = fs / t; pitchConf = 1.0 - cm[(size_t) best];
    }
    void feedback(int n, float* L, float* R) {
        float mode = p(11, "mode") / 100.0f, grow = p(11, "grow") / 100.0f, level = p(11, "level") / 100.0f, harmK = p(11, "harm") / 100.0f, tone = p(11, "tone") / 100.0f;
        bool stop = p(11, "stop") > 0.5f;
        int md = mode < 0.34f ? 0 : (mode < 0.67f ? 1 : 2);           // CATCH, LISTEN, HOLD
        static const double HARM[5] = { 1.0, 2.0, 3.0, 4.0, 0.0 };
        double harm = HARM[std::clamp((int) (harmK * 4.99f), 0, 4)];  // 0 = FREE: it finds its own pitch
        float target = std::pow(10.0f, (-30.0f + level * 24.0f) / 20.0f);   // -30 .. -6 dBFS
        float growDb = 4.0f + grow * grow * 56.0f;                    // 4 .. 60 dB per second
        float toneHz = 600.0f * std::pow(2.0f, tone * 5.0f);
        float gT = std::tan(3.14159265f * std::min(toneHz, (float) sr * 0.45f) / (float) sr);
        if (stop && !prevStop) { fbState = 2; waitOnset = true; }
        prevStop = stop;
        for (int i = 0; i < n; ++i) {
            float x = in[i];
            // the pitch follower
            pacc += x; if (++pdec >= 4) { pbuf[(size_t) pw] = pacc * 0.25f; pw = (pw + 1) % 1024; pacc = 0; pdec = 0; if (++pnew >= 192) { pnew = 0; trackPitch(); } }
            // onsets: the sound sent jumps up
            float ax = std::abs(x);
            inEnvF += (ax > inEnvF ? 0.02f : 0.0015f) * (ax - inEnvF);
            inEnvS += 0.00008f * (ax - inEnvS);
            sinceOnset += 1.0 / sr;
            bool onset = inEnvF > 0.003f && inEnvF > inEnvS * 2.5f && sinceOnset > 0.15;
            if (onset) { sinceOnset = 0; waitOnset = false; fbState = 1; }
            // the pitch it feeds back at: the note heard (stable for a moment), times the harmonic
            if ((i & 31) == 0 && pitchConf > 0.8 && pitchHz > 40 && pitchHz < 1500) {
                if (std::abs(std::log2(pitchHz / std::max(1.0, pendingHz))) < 0.03) stableFor += 32.0 / sr; else { pendingHz = pitchHz; stableFor = 0; }
                if (stableFor > (md == 1 ? 0.25 : 0.04)) fbHz = harm > 0 ? std::min(3000.0, pendingHz * harm) : fbHz;
            }
            if (harm == 0 && (i & 31) == 0 && fbEnv > target * 0.3f && pitchConf > 0.85) fbHz = std::clamp(pitchHz, 60.0, 3000.0);   // FREE: it follows itself
            double per = sr / fbHz; int mult = std::max(1, (int) std::ceil(0.0025 * sr / per));
            fbDT = (float) std::min((double) fbLine.d.size() - 8, per * mult);
            fbD += (fbDT - fbD) * 0.0008f;                              // glides to a new note (like real feedback)
            // how much the loop grows or shrinks per second, towards LEVEL
            float e = std::max(fbEnv, 1e-6f), dbps;
            float overDb = 20.0f * std::log10(e / target);
            if (md != 0 && fbState == 0 && !waitOnset) fbState = 1;                                  // LISTEN / HOLD: always listening
            if (fbState == 2) { dbps = -40.0f; if (e < (md == 0 ? target * 0.01f : 1e-4f)) fbState = 0; }  // letting go
            else if (fbState == 0) dbps = -18.0f;                                                      // resting
            else if (md == 0) { dbps = growDb; if (e >= target) fbState = 2; }                      // CATCH: grow, then let go before it is loud
            else if (md == 1) dbps = std::clamp(-overDb * 3.0f, -30.0f, growDb * 0.35f);            // LISTEN: slowly up to LEVEL, and stay
            else dbps = std::clamp(-overDb * 6.0f, -40.0f, growDb);                                   // HOLD: up to LEVEL, and stay
            if (e > target * 1.6f) dbps = std::min(dbps, -60.0f);                                     // never above LEVEL
            float passes = (float) (sr / fbD);
            float gWant = std::pow(10.0f, dbps / 20.0f / passes);
            fbG += (gWant - fbG) * 0.01f;
            // the loop: delay (the period), a band-pass at the pitch, the tone, a soft saturation
            float d = fbLine.read(fbD);
            float hzBp = (float) std::min(sr * 0.45, sr / fbD * mult);
            float gb = std::tan(3.14159265f * hzBp / (float) sr);
            float lp, bp, hp; fbBp.tick(d, gb, 0.35f, lp, bp, hp); bp *= 0.35f;      // unity gain at the pitch
            float lp2, bp2, hp2; fbTone.tick(bp, gT, 1.414f, lp2, bp2, hp2);
            float ins = x * (md == 1 ? 0.6f : 1.0f) * (fbState == 2 && md == 0 ? 0.1f : 1.0f);
            float loop = std::tanh((lp2 * fbG + ins * 0.25f) * 1.2f) / 1.2f;
            fbLine.push(loop);
            float ae = std::abs(loop);
            fbEnv += (ae > fbEnv ? 0.004f : 0.0004f) * (ae - fbEnv);
            // the output: DC removed, a gentle limiter (-6 dBFS)
            float y = loop; dcY = y - dcX + 0.9995f * dcY; dcX = y; y = dcY;
            float peak = std::abs(y) * lim;
            if (peak > 0.5f) lim = 0.5f / std::abs(y); else lim += (1.0f - lim) * 0.0002f;
            y *= lim;
            if (!std::isfinite(y)) { initFeedback(); y = 0; }
            L[i] = y * 1.4f; R[i] = y * 1.4f;
        }
    }
    // ================================================================ 1.4
    void initNew() {
        for (auto& b : vcM) b.reset(); for (auto& b : vcC) b.reset();
        std::fill(std::begin(vcEnv), std::end(vcEnv), 0.0f);
        for (int k = 0; k < NVB; ++k) { double f = 90.0 * std::pow(8500.0 / 90.0, k / (double) (NVB - 1)); vcG[k] = (float) std::tan(3.14159265 * f / sr); }
        std::fill(std::begin(vcPh), std::end(vcPh), 0.0);
        psLine.init((int) (0.2 * sr)); psPh = 0;
        for (auto& a : spAp) a.init((int) (0.004 * sr) + 8);
        spLine.init((int) (0.12 * sr)); spLp.z = 0; spHp.z = 0; spState = 0;
        for (auto& f : tkF) f.reset(); tkEnv = 0; tkPos = 0;
    }
    // ---- VOCODER: your sound shapes another one - the SYNTH, a chord that follows the note you play, or a robot voice
    static constexpr int NVB = 20;
    XSvf vcM[NVB], vcC[NVB]; float vcG[NVB] {}, vcEnv[NVB] {}; double vcPh[3] {}; juce::Random vcRnd;
    void vocoder(int n, float* L, float* R, const float* pitch, const float* synth) {
        float src = p(12, "src") / 100.0f, react = p(12, "react") / 100.0f, bright = p(12, "bright") / 100.0f, note = p(12, "note") / 100.0f;
        int mode = src < 0.34f ? 0 : (src < 0.67f ? 1 : 2);              // SYNTH, CHORD (follows you), ROBOT (a fixed note)
        float att = 0.02f + react * 0.2f, rel = 0.0015f + react * 0.02f;
        const float q = 0.12f;                                            // narrow bands
        for (int i = 0; i < n; ++i) {
            float carrier = 0;
            if (mode == 0) carrier = synth ? synth[i] * 2.5f : 0.0f;
            else {
                double base = mode == 1 ? (pitch && pitch[i] > 20 ? pitch[i] : 48.0) : 36.0 + note * 36.0;
                const double iv[3] = { 0, mode == 1 ? 4.0 : 0.0, mode == 1 ? 7.0 : 12.0 };
                for (int v = 0; v < (mode == 1 ? 3 : 2); ++v) {
                    double hz = 440.0 * std::pow(2.0, (base + iv[v] - 69) / 12.0);
                    vcPh[v] += hz / sr; if (vcPh[v] >= 1) vcPh[v] -= 1;
                    carrier += (float) (2 * vcPh[v] - 1) * 0.5f;
                }
            }
            carrier += (vcRnd.nextFloat() * 2 - 1) * bright * 0.3f;      // breath / consonants
            float x = in[i], y = 0;
            for (int k = 0; k < NVB; ++k) {
                float lp, bp, hp;
                vcM[k].tick(x, vcG[k], q, lp, bp, hp);
                float a = std::abs(bp); float& e = vcEnv[k]; e += (a > e ? att : rel) * (a - e);
                vcC[k].tick(carrier, vcG[k], q, lp, bp, hp);
                y += bp * e * (1.0f + bright * k / (float) NVB);
            }
            y = std::tanh(y * 6.0f) * 0.42f;
            L[i] = y; R[i] = y;
        }
    }
    // ---- PITCH: the sound moved up or down (up to an octave, by semitones), fine-tuned, a short delay for a "double"
    XDelay psLine; double psPh = 0; XOnePole psLp;
    void pitchShift(int n, float* L, float* R) {
        float semis = std::round(p(13, "semi") / 100.0f * 24.0f - 12.0f), fine = (p(13, "fine") / 100.0f - 0.5f), smooth = p(13, "smooth") / 100.0f;
        double ratio = std::pow(2.0, (semis + fine) / 12.0), win = (0.02 + smooth * 0.08) * sr;
        for (int i = 0; i < n; ++i) {
            psLine.push(in[i]);
            psPh += (1.0 - ratio) / win; psPh -= std::floor(psPh);
            double p2 = psPh + 0.5; p2 -= std::floor(p2);
            float a = psLine.read(1 + psPh * win), b = psLine.read(1 + p2 * win);
            float wa = (float) std::sin(3.14159265 * psPh), wb = (float) std::sin(3.14159265 * p2);
            float y = a * wa * wa + b * wb * wb;
            L[i] = y; R[i] = y;
        }
    }
    // ---- SPRING: a guitar amp's spring reverb - the "drip", the boing, the twang
    XDelay spAp[8]; XDelay spLine; XOnePole spLp, spHp; float spState = 0;
    void spring(int n, float* L, float* R) {
        float decay = p(14, "decay") / 100.0f, tone = p(14, "tone") / 100.0f, drip = p(14, "drip") / 100.0f, tension = p(14, "tension") / 100.0f;
        float fb = 0.55f + decay * 0.4f, apg = 0.45f + drip * 0.35f;
        double dl = (0.022 + (1.0 - tension) * 0.06) * sr;
        float lpA = 0.08f + tone * 0.6f;
        for (int i = 0; i < n; ++i) {
            float x = spLp.lp(in[i] + spState * fb, lpA);
            for (int k = 0; k < 8; ++k) {                                   // the springs' dispersion: allpasses (chirps)
                float dly = spAp[k].read(18 + k * 3 + (int) (drip * 20));
                float v = x + apg * dly; spAp[k].push(v); x = dly - apg * v;
            }
            spLine.push(x);
            float y = spLine.read(dl);
            float wobble = spLine.read(dl * 0.71);
            spState = std::tanh(y);
            float hp = y - spHp.lp(y, 0.01f);
            L[i] = hp * 1.5f; R[i] = (0.6f * hp + 0.4f * (wobble - spHp.z)) * 1.5f;
        }
    }
    // ---- TALK: the sound speaks vowels (A E I O U) - by hand, moving in tempo, or opening with how hard you play
    XSvf tkF[3]; float tkEnv = 0; double tkPos = 0;
    void talk(int n, float* L, float* R, const float* bpm) {
        static const float FM[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 300, 2200, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };   // A E I O U
        float vowel = p(15, "vowel") / 100.0f, move = p(15, "move") / 100.0f, follow = p(15, "follow") / 100.0f, reso = p(15, "reso") / 100.0f;
        float k = 0.5f - reso * 0.42f;
        for (int i = 0; i < n; ++i) {
            float a = std::abs(in[i]); tkEnv += (a > tkEnv ? 0.01f : 0.0005f) * (a - tkEnv);
            if (move > 0.02f) { static const double B[5] = { 4.0, 2.0, 1.0, 0.5, 0.25 }; double beats = B[std::clamp((int) (move * 4.99f), 0, 4)];
                tkPos += tempo(bpm, i) / 60.0 / beats / sr; tkPos -= std::floor(tkPos); }
            float pos = vowel + (move > 0.02f ? (float) (0.5 - 0.5 * std::cos(6.2831853 * tkPos)) : 0.0f) + follow * std::min(1.0f, tkEnv * 6.0f);
            pos = std::fmod(pos, 1.0f) * 4.0f; int v0 = std::clamp((int) pos, 0, 3); float fr = pos - v0;
            float y = 0;
            for (int f = 0; f < 3; ++f) {
                float hz = FM[v0][f] + (FM[v0 + 1][f] - FM[v0][f]) * fr;
                float g = std::tan(3.14159265f * std::min(hz, (float) sr * 0.45f) / (float) sr);
                float lp, bp, hp; tkF[f].tick(in[i], g, k, lp, bp, hp);
                y += bp * (f == 0 ? 1.0f : (f == 1 ? 0.7f : 0.4f));
            }
            y *= 1.0f;
            L[i] = y; R[i] = y;
        }
    }
};

} // namespace arco
