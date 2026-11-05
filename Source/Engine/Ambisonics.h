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
        for (int s = 0; s < NS; ++s) { srcAz[s] = base[s] = -90.0f + s * 15.0f; srcEl[s] = 0; srcDist[s] = 2.0f; curAz[s].store(srcAz[s]); curEl[s].store(0.0f); curDist[s].store(2.0f); dopD[s] = -1; }
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
        static const char* G[] = { "room_shape", "room_w", "room_d", "room_h", "room_abs", "room_rev", "lis_x", "lis_y", "dist_law", "doppler", "air", "scene_rot", "binaural", "sub_spk", "sub_xo" };
        for (int g = 0; g < 15; ++g) if (n == G[g]) return 2 + g;
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
        if (i >= 2 && i < 17) { setRoom(i - 2, f); return; }
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
        int sd = (int) (sr * 0.3) + 8;                       // each sound's own line: its echoes off the walls, and the Doppler
        for (auto& d : srcLine) d.assign((size_t) sd, 0.0f);
        srcW0 = 0;
        static const int FD[8] = { 1031, 1327, 1523, 1801, 2053, 2357, 2633, 2971 };
        for (int k = 0; k < 8; ++k) { fdn[k].assign((size_t) (FD[k] * sr / 44100.0 * 3.0) + 8, 0.0f); fdnW[k] = 0; fdnLp[k] = 0; fdnBase[k] = FD[k] * sr / 44100.0; }
        for (auto& b : binLine) b.assign(64, 0.0f); binW = 0; for (auto& x : binLp) x = 0;
        for (auto& x : subLp) x = 0; for (auto& x : subLp2) x = 0;
        for (auto& x : dopD) x = -1;
        roomChanged();
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
                case 5: { float p = (float) phase[s]; az = base[s] + p * 720.0f; el = srcEl[s] + 55.0f * (float) std::sin(p * 3.14159265f); break; }                                // spiral up and down
                case 6: { double a = phase[s] * 6.2831853; az = base[s] + srcRng[s] * (float) std::sin(a); el = srcEl[s] + srcRng[s] * 0.35f * (float) std::sin(2 * a); break; }  // a figure of eight
                case 7: { int step = (int) std::floor(phase[s] * 4.0); if (step != jumpStep[s]) { jumpStep[s] = step; rnd = rnd * 1664525u + 1013904223u; jumpAz[s] = base[s] + srcRng[s] * (((rnd >> 8) & 0xFFFF) / 32768.0f - 1.0f) * 2.0f;
                              rnd = rnd * 1664525u + 1013904223u; jumpEl[s] = srcEl[s] + 25.0f * (((rnd >> 8) & 0xFFFF) / 65535.0f); } az = jumpAz[s]; el = jumpEl[s]; break; }  // jumps
                default: break;
            }
            float dist = srcDist[s];
            if (srcMot[s] == 8 || srcMot[s] == 9) {      // along the walls / bouncing inside the room: positions in the room, then seen from the listener
                float hw = roomShape == 0 ? srcDist[s] : roomW * 0.5f - 0.4f, hd = roomShape == 0 ? srcDist[s] : roomD * 0.5f - 0.4f;
                double v = (0.3 + spd * 4.0) * dt;
                if (srcMot[s] == 8) {
                    double per = 4.0 * (hw + hd); wallPos[s] = std::fmod(wallPos[s] + v, per); double q = wallPos[s];
                    if (roomShape == 3 || roomShape == 0) { double a = q / per * 6.2831853; mx[s] = (float) (std::sin(a) * hw); my[s] = (float) (std::cos(a) * hd); }
                    else if (q < 2 * hw) { mx[s] = (float) (-hw + q); my[s] = hd; } else if ((q -= 2 * hw) < 2 * hd) { mx[s] = hw; my[s] = (float) (hd - q); }
                    else if ((q -= 2 * hd) < 2 * hw) { mx[s] = (float) (hw - q); my[s] = -hd; } else { q -= 2 * hw; mx[s] = -hw; my[s] = (float) (-hd + q); }
                } else {
                    if (mvx[s] == 0 && mvy[s] == 0) { mvx[s] = 0.8f; mvy[s] = 0.55f; mx[s] = 0; my[s] = hd * 0.5f; }
                    mx[s] += (float) (mvx[s] * v); my[s] += (float) (mvy[s] * v);
                    if (mx[s] > hw) { mx[s] = hw; mvx[s] = -std::abs(mvx[s]); } if (mx[s] < -hw) { mx[s] = -hw; mvx[s] = std::abs(mvx[s]); }
                    if (my[s] > hd) { my[s] = hd; mvy[s] = -std::abs(mvy[s]); } if (my[s] < -hd) { my[s] = -hd; mvy[s] = std::abs(mvy[s]); }
                    if (roomShape == 3 || roomShape == 0) { float r = std::sqrt(mx[s] * mx[s] + my[s] * my[s]); if (r > hw) { mx[s] *= hw / r; my[s] *= hw / r; float t = mvx[s]; mvx[s] = -mvy[s]; mvy[s] = t; } }
                }
                float rx = mx[s] - lisX, ry = my[s] - lisY; dist = std::max(0.3f, std::sqrt(rx * rx + ry * ry)); az = std::atan2(rx, ry) * 57.29578f; el = srcEl[s];
            }
            az += sceneRot;
            curAz[s].store(wrap(az)); curEl[s].store(std::clamp(el, -90.0f, 90.0f)); curDist[s].store(dist);
            float Y[CH]; sh(az, std::clamp(el, -90.0f, 90.0f), Y);
            // width: the higher orders fade (a wider, softer image)
            float w = std::clamp(srcW[s] / 100.0f, 0.0f, 1.0f);
            const float ow[4] = { 1.0f, 1.0f - w * 0.45f, 1.0f - w * 0.75f, 1.0f - w * 0.95f };
            for (int c = 0; c < CH; ++c) Y[c] *= ow[ordOf(c)];
            // distance: level (1/r beyond 1 m), air (high frequencies fade)
            float d = dist;
            float g = distLaw == 2 ? 1.0f : distLaw == 1 ? 1.0f / std::max(1.0f, std::sqrt(d * 0.7f)) : 1.0f / std::max(1.0f, d * 0.7f);
            float a = 1.0f - std::exp(-2.0f * 3.14159265f * std::min(20000.0f, 22000.0f / (1.0f + d * 0.25f * airAmt)) / (float) sr);
            float pk = 0;
            if (nSpk == 2) {
                // two speakers: plain constant-power panning is sharper than ambisonics can be (front -30..+30, the back folds to the front, softer)
                float azw = wrap(az), back = 1.0f;
                if (std::abs(azw) > 90.0f) { azw = (azw > 0 ? 180.0f : -180.0f) - azw; back = 0.7f; }
                float pp = std::clamp((azw / std::max(1.0f, std::abs(spAz[1] - spAz[0]) * 0.5f)) * (1.0f - w * 0.5f), -1.0f, 1.0f);
                float gl = std::cos((pp + 1) * 0.785398f) * back, gr = std::sin((pp + 1) * 0.785398f) * back;
                if (spAz[0] > spAz[1]) std::swap(gl, gr);
                auto& L2 = srcLine[s]; int LN2 = (int) L2.size();
                for (int i = 0; i < n; ++i) { lp[s] += a * (ch[s][i] - lp[s]); if (LN2 > 8) L2[(size_t) ((srcW0 + i) % LN2)] = lp[s]; float x = lp[s] * g; pk = std::max(pk, std::abs(x)); stereo[0][i] += x * gl; stereo[1][i] += x * gr; for (int c = 0; c < CH; ++c) B[c][i] += x * Y[c]; revIn[i] += lp[s] * wet(d); }
                level[s] = pk;
                if (roomShape != 0 && LN2 > 8) reflections(s, L2, az, el, d, B, stereo, n);
                continue;
            }
            auto& L = srcLine[s]; int LN = (int) L.size();
            float wantD = doppler && LN > 8 ? std::min((float) LN - 4, d / 343.0f * (float) sr) : 0.0f;
            if (dopD[s] < 0) dopD[s] = wantD;
            for (int i = 0; i < n; ++i) {
                lp[s] += a * (ch[s][i] - lp[s]);
                if (LN > 8) L[(size_t) ((srcW0 + i) % LN)] = lp[s];
                float x;
                if (doppler && LN > 8) { dopD[s] += (wantD - dopD[s]) * 0.0005f; x = readLine(L, srcW0 + i, dopD[s]) * g; }
                else x = lp[s] * g;
                pk = std::max(pk, std::abs(x));
                for (int c = 0; c < CH; ++c) B[c][i] += x * Y[c];
                revIn[i] += lp[s] * wet(d);
            }
            level[s] = pk;
            if (roomShape != 0 && LN > 8) reflections(s, L, az, el, d, B, stereo, n);
        }
        srcW0 = srcLine[0].empty() ? 0 : (srcW0 + n) % (int) srcLine[0].size();
        // the room's tail: eight lines feeding each other (as long as the room is big and its walls hard), heard from all round
        if (roomShape != 0 && roomRev > 0.001f && !fdn[0].empty()) {
            static const float dirs[8][3] = { { 1, 0, .3f }, { .7f, .7f, -.2f }, { 0, 1, .3f }, { -.7f, .7f, -.2f }, { -1, 0, .3f }, { -.7f, -.7f, -.2f }, { 0, -1, .3f }, { .7f, -.7f, -.2f } };
            for (int i = 0; i < n; ++i) {
                float o[8];
                for (int k = 0; k < 8; ++k) { auto& F = fdn[k]; int FN = (int) F.size(); int rp = fdnW[k] - (int) (fdnBase[k] * roomScale); while (rp < 0) rp += FN; o[k] = F[(size_t) rp]; }
                float sum = 0; for (float x : o) sum += x; sum *= 0.25f;       // a Householder mix
                for (int k = 0; k < 8; ++k) {
                    float x = (o[k] - sum) * fdnG[k]; fdnLp[k] += fdnDamp * (x - fdnLp[k]);
                    auto& F = fdn[k]; F[(size_t) fdnW[k]] = flushD(fdnLp[k] + revIn[i] * 0.35f * (k & 1 ? 1.0f : -1.0f)); fdnW[k] = (fdnW[k] + 1) % (int) F.size();
                    float v = o[k] * roomRev * 0.6f; B[0][i] += v; B[1][i] += v * dirs[k][1]; B[2][i] += v * dirs[k][2]; B[3][i] += v * dirs[k][0];
                    stereo[k & 1][i] += v * 0.7f;
                }
            }
        }
        std::fill(revIn, revIn + n, 0.0f);
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
                if (subSpk >= 0 && subSpk < nOut) {          // the low notes go to the sub, the rest to the speakers
                    if (k == subSpk) y = 0;
                    else { subLp[k] += subK * (y - subLp[k]); subLp2[k] += subK * (subLp[k] - subLp2[k]); subBus[i] += subLp2[k]; y -= subLp2[k]; }
                }
                line[(size_t) ((w0 + i) % N)] = y * spkGain[k];
                int rp = (w0 + i - spkDelay[k]) % N; if (rp < 0) rp += N;
                out[k][i] += line[(size_t) rp];
            }
        }
        if (subSpk >= 0 && subSpk < nOut && out[subSpk]) for (int i = 0; i < n; ++i) out[subSpk][i] += subBus[i] * 0.7f;
        std::fill(subBus, subBus + n, 0.0f);
        dw = (dw + n) % N;
        // the headphones
        if (phL && phR) {
            if (!binaural) for (int i = 0; i < n; ++i) { float w = B[0][i], y = B[1][i], x = B[3][i]; phL[i] += 0.5f * (w + y) + 0.12f * x; phR[i] += 0.5f * (w - y) + 0.12f * x; }
            else {      // as if you sat in the room: eight virtual speakers round your head, each heard with its time and shadow between the ears
                for (int i = 0; i < n; ++i) {
                    float l = 0, r = 0;
                    for (int v = 0; v < 8; ++v) {
                        float y = 0; for (int c = 0; c < 9; ++c) y += binDec[v][c] * B[c][i];
                        auto& bl = binLine[v]; bl[(size_t) binW] = y;
                        auto tap = [&](int d) { return bl[(size_t) ((binW - d + 64) & 63)]; };
                        float nearV = tap(0), farV = tap(binItd[v]);
                        binLp[v] += binShade[v] * (farV - binLp[v]);
                        float farOut = binLp[v] * binFarGain[v];
                        if (binSide[v] > 0) { r += nearV; l += farOut; } else if (binSide[v] < 0) { l += nearV; r += farOut; } else { l += nearV * 0.85f; r += nearV * 0.85f; }
                    }
                    binW = (binW + 1) & 63;
                    phL[i] += l * 0.55f; phR[i] += r * 0.55f;
                }
            }
        }
    }
    std::atomic<float> curAz[NS], curEl[NS], curDist[NS];   // where each source is now (for the window)
    // 2.4: the room (what the window shows)
    int roomShape = 0; float roomW = 8, roomD = 10, roomH = 4, roomAbs = 0.35f, roomRev = 0.3f, lisX = 0, lisY = 0;
    int distLaw = 0; bool doppler = false; float airAmt = 1.0f, sceneRot = 0; bool binaural = false; int subSpk = -1; float subXo = 90;
    float rt60() const { float V = roomW * roomD * roomH, S = 2 * (roomW * roomD + roomW * roomH + roomD * roomH), al = 0.03f + roomAbs * 0.6f; return std::clamp(0.161f * V / (S * al), 0.15f, 9.0f); }
    float level[NS] {};
    static float wrap(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }
    // the walls: an echo of the sound from each one (its mirror image), later and softer, from where the mirror is
    void reflections(int s, const std::vector<float>& L, float az, float el, float d, float (*B)[SUBMAX], float (*stereo)[SUBMAX], int n) {
        (void) s; int LN = (int) L.size();
        float sx = lisX + d * std::sin(az / 57.29578f) * std::cos(el / 57.29578f), sy = lisY + d * std::cos(az / 57.29578f) * std::cos(el / 57.29578f), sz = 1.2f + d * std::sin(el / 57.29578f);
        float hw = roomW * 0.5f, hd = roomD * 0.5f; sx = std::clamp(sx, -hw + 0.1f, hw - 0.1f); sy = std::clamp(sy, -hd + 0.1f, hd - 0.1f); sz = std::clamp(sz, 0.1f, roomH - 0.1f);
        float img[6][3] = { { 2 * hw - sx, sy, sz }, { -2 * hw - sx, sy, sz }, { sx, 2 * hd - sy, sz }, { sx, -2 * hd - sy, sz }, { sx, sy, -sz }, { sx, sy, 2 * roomH - sz } };
        float refl = (1.0f - roomAbs) * 0.8f;
        for (auto& im : img) {
            float rx = im[0] - lisX, ry = im[1] - lisY, rz = im[2] - 1.2f, rd = std::sqrt(rx * rx + ry * ry + rz * rz);
            float dly = std::min((float) LN - 4, rd / 343.0f * (float) sr); float gg = refl / std::max(1.0f, rd * 0.7f);
            if (nSpk == 2) { float pan = std::clamp(rx / std::max(0.5f, rd), -1.0f, 1.0f); float gl = std::cos((pan + 1) * 0.785398f), gr = std::sin((pan + 1) * 0.785398f);
                for (int i = 0; i < n; ++i) { float v = readLine(L, srcW0 + i, dly) * gg; stereo[0][i] += v * gl; stereo[1][i] += v * gr; } continue; }
            float Ye[4]; Ye[0] = 1; Ye[1] = -rx / rd; Ye[2] = rz / rd; Ye[3] = ry / rd;      // (first order: the y axis is to the left)
            for (int i = 0; i < n; ++i) { float v = readLine(L, srcW0 + i, dly) * gg; B[0][i] += v * Ye[0]; B[1][i] += v * Ye[1]; B[2][i] += v * Ye[2]; B[3][i] += v * Ye[3]; }
        }
    }
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
    int jumpStep[NS] {}; float jumpAz[NS] {}, jumpEl[NS] {}; unsigned rnd = 12345u;
    double wallPos[NS] {}; float mx[NS] {}, my[NS] {}, mvx[NS] {}, mvy[NS] {};
    std::vector<float> srcLine[NS]; int srcW0 = 0; float dopD[NS];
    std::vector<float> fdn[8]; int fdnW[8] {}; float fdnLp[8] {}, fdnG[8] {}; double fdnBase[8] {}; float fdnDamp = 0.5f, roomScale = 1.0f;
    float revIn[SUBMAX] {}, subBus[SUBMAX] {};
    float subLp[MAXSPK] {}, subLp2[MAXSPK] {}, subK = 0.01f;
    std::vector<float> binLine[8]; int binW = 0; float binLp[8] {}, binDec[8][9] {}, binShade[8] {}, binFarGain[8] {}; int binItd[8] {}, binSide[8] {};
    static float flushD(float v) { return std::abs(v) < 1e-20f ? 0.0f : v; }
    static float readLine(const std::vector<float>& L, int w, float d) { int N = (int) L.size(); float rp = (float) w - d; while (rp < 0) rp += (float) N; int i0 = (int) rp; float fr = rp - (float) i0; i0 %= N; return L[(size_t) i0] * (1 - fr) + L[(size_t) ((i0 + 1) % N)] * fr; }
    float wet(float d) const { if (roomShape == 0) return 0.0f; return roomRev * (0.25f + 0.75f * std::min(1.0f, d / 8.0f)) * 0.5f; }
    void setRoom(int k, float f) {
        switch (k) {
            case 0: roomShape = std::clamp((int) std::lround(f), 0, 4); break;
            case 1: roomW = std::clamp(f, 2.0f, 80.0f); break; case 2: roomD = std::clamp(f, 2.0f, 80.0f); break; case 3: roomH = std::clamp(f, 2.0f, 30.0f); break;
            case 4: roomAbs = std::clamp(f / 100.0f, 0.0f, 1.0f); break; case 5: roomRev = std::clamp(f / 100.0f, 0.0f, 1.0f); break;
            case 6: lisX = f; break; case 7: lisY = f; break; case 8: distLaw = std::clamp((int) std::lround(f), 0, 2); break; case 9: doppler = f > 0.5f; break;
            case 10: airAmt = std::clamp(f / 50.0f, 0.0f, 4.0f); break; case 11: sceneRot = f; break; case 12: binaural = f > 0.5f; break;
            case 13: subSpk = (int) std::lround(f) - 1; break; case 14: subXo = std::clamp(f, 30.0f, 250.0f); break;
        }
        roomChanged();
    }
    void roomChanged() {
        if (roomShape == 3) roomD = roomW;          // round: as wide as it is deep
        float mfp = 4.0f * roomW * roomD * roomH / (2 * (roomW * roomD + roomW * roomH + roomD * roomH));   // the mean free path (m)
        roomScale = std::clamp(mfp / 3.0f, 0.25f, 3.0f);
        float T = rt60() * (roomShape == 4 ? 1.6f : 1.0f);
        for (int k = 0; k < 8; ++k) { double len = fdnBase[k] * roomScale; fdnG[k] = (float) std::pow(10.0, -3.0 * len / (T * sr)); }
        fdnDamp = 0.25f + (1.0f - roomAbs) * 0.65f;
        subK = 1.0f - std::exp(-2.0f * 3.14159265f * subXo / (float) sr);
        // the virtual speakers for the headphones: decoder rows (first and second order), the time between the ears, the head's shadow
        for (int v = 0; v < 8; ++v) {
            float az = -180.0f + 22.5f + v * 45.0f; float Y[CH]; sh(az, 0, Y);
            for (int c = 0; c < 9; ++c) binDec[v][c] = Y[c] * (2 * ordOf(c) + 1) / 8.0f * (ordOf(c) == 2 ? 0.6f : ordOf(c) == 1 ? 0.85f : 1.0f) * 0.5f;
            float th = std::abs(az) > 90 ? 180.0f - std::abs(az) : std::abs(az); float lat = std::sin(th / 57.29578f);
            binItd[v] = std::clamp((int) std::lround(0.0875 / 343.0 * (th / 57.29578 + lat) * sr), 0, 60);
            binSide[v] = std::abs(az) < 1 || std::abs(std::abs(az) - 180) < 1 ? 0 : (az > 0 ? 1 : -1);
            binShade[v] = 1.0f - std::exp(-2.0f * 3.14159265f * (6000.0f - 4500.0f * lat) / (float) sr);
            binFarGain[v] = 1.0f - 0.45f * lat;
        }
    }
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
