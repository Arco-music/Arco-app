#pragma once
// ARCO 2.0: LIVE LOOPS - a grid of cells, eight rows by eight columns. Each cell holds a loop (recorded straight into
// it, an audio file dropped on it, a loop taken from the station). One cell per row plays at a time; cells and whole
// columns start and stop on the next bar line (or beat), so everything stays in time - with the station, STUDIO,
// the drum machine: the same clock. Loops play at the tempo of the moment (they follow tempo changes).
// Recording: tap an empty cell - it starts on the next bar line and lasts 1, 2, 4 or 8 bars (or until tapped again,
// then to the end of that bar), and plays at once when it is done.
#include <atomic>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

namespace arco {

struct LoopClip { std::vector<float> l, r; long len = 0; double beats = 4; };

class LoopGrid {
public:
    static constexpr int ROWS = 8, COLS = 8;
    // 2.2: the station's tracks too (as you hear them: their strip, effects and volume) - after the old ones, so saved
    // songs keep their sources
    enum Src { SrcLive1 = 0, SrcLive2, SrcLive3, SrcLive4, SrcDrums, SrcSynth, SrcPhrase, SrcTrack1, NSrc = SrcTrack1 + 8 };
    std::atomic<const LoopClip*> cell[ROWS][COLS];
    // per row
    std::atomic<int> playing[ROWS];                  // the column playing (-1 none)
    std::atomic<int> queued[ROWS];                   // -2 nothing, -1 stop at the next line, >= 0 that column starts there
    std::atomic<float> vol[ROWS], level[ROWS];
    std::atomic<int> src[ROWS], mute[ROWS];
    std::atomic<float> progress[ROWS];               // where the playing loop is (0..1)
    // recording (one cell at a time)
    std::atomic<int> recRow { -1 }, recCol { -1 }, recState { 0 };   // 0 none, 1 waiting for the line, 2 recording, 3 done (the message thread takes it)
    std::atomic<int> recBars { 2 };                   // 0 = free: until the cell is tapped again
    std::atomic<bool> recStopAsked { false };
    std::atomic<int> quant { 0 };                     // 0 bar, 1 beat
    std::atomic<float> master { 0.8f };
    std::atomic<float> recProgressOut { 0 };
    LoopClip recClip;                                 // the take being recorded (preallocated)
    std::atomic<double> recBeatsOut { 0 };

    LoopGrid() {
        for (auto& r : cell) for (auto& c : r) c.store(nullptr);
        for (int i = 0; i < ROWS; ++i) { playing[i].store(-1); queued[i].store(-2); vol[i].store(0.8f); level[i].store(0); src[i].store(i < 4 ? i : SrcDrums); mute[i].store(0); progress[i].store(0); }
    }
    void init(double rate) { sr = rate; recClip.l.assign((size_t) (rate * 64.0), 0.0f); recClip.r.assign(recClip.l.size(), 0.0f); recClip.len = 0; for (auto& p : ph) p = 0; }

    // ---- message thread
    void launch(int row, int col) { queued[row].store(col); }
    void stopRow(int row) { queued[row].store(-1); }
    void launchColumn(int col) { for (int r = 0; r < ROWS; ++r) if (cell[r][col].load()) queued[r].store(col); else if (playing[r].load() >= 0) queued[r].store(-1); }
    void stopAll() { for (int r = 0; r < ROWS; ++r) if (playing[r].load() >= 0 || queued[r].load() >= 0) queued[r].store(-1); }
    bool record(int row, int col) { if (recState.load() != 0) return false; recRow.store(row); recCol.store(col); recStopAsked = false; recState.store(1); return true; }

    // ---- audio thread. in: the sources (live 1-4 mono, drums L/R, synth L/R, phrase L/R)
    void process(const float* const* in, const float* beat, const float* bpm, float bpb, float* L, float* R, int n) {
        int q = quant.load();
        float mv = master.load();
        for (int i = 0; i < n; ++i) {
            double b = beat ? beat[i] : 0, t = bpm && bpm[i] > 20 ? bpm[i] : 120;
            // a continuous beat (the station's wraps at its loop end; this one goes on)
            double d = b - lastB; lastB = b;
            if (!(d >= 0 && d < 0.05)) d = t / 60.0 / sr;
            gb += d;
            double unit = q == 1 ? 1.0 : std::max(1.0, (double) bpb);
            bool line = std::floor(gb / unit) != std::floor((gb - d) / unit);
            if (line) {
                for (int r = 0; r < ROWS; ++r) {
                    int qq = queued[r].load(std::memory_order_relaxed);
                    if (qq == -2) continue;
                    queued[r].store(-2, std::memory_order_relaxed);
                    if (qq == -1) { fadeRow(r); playing[r].store(-1); }
                    else if (cell[r][qq].load()) { if (playing[r].load() >= 0) fadeRow(r); playing[r].store(qq); start[r] = std::floor(gb / unit + 0.5) * unit; }
                }
                // recording: starts on a bar line, ends on one
                double bar = std::max(1.0, (double) bpb);
                if (recState.load() == 1 && std::floor(gb / bar) != std::floor((gb - d) / bar)) {
                    recState.store(2); recStart = std::floor(gb / bar + 0.5) * bar; recClip.len = 0;
                    int rr = recRow.load(); if (rr >= 0 && playing[rr].load() >= 0) { fadeRow(rr); playing[rr].store(-1); }
                }
            }
            if (recState.load() == 2) {
                int rr = std::clamp(recRow.load(), 0, ROWS - 1), s = std::clamp(src[rr].load(), 0, NSrc - 1);
                float xl = 0, xr = 0;
                if (s < 4) { xl = xr = in[s] ? in[s][i] : 0.0f; }
                else if (s >= SrcTrack1) { int k = 10 + (s - SrcTrack1); xl = xr = in[k] ? in[k][i] : 0.0f; }   // a track (its strip, mono)
                else { int k = 4 + (s - 4) * 2; xl = in[k] ? in[k][i] : 0.0f; xr = in[k + 1] ? in[k + 1][i] : xl; }
                if (recClip.len < (long) recClip.l.size()) { recClip.l[(size_t) recClip.len] = xl; recClip.r[(size_t) recClip.len] = xr; ++recClip.len; }
                double bar = std::max(1.0, (double) bpb), el = gb - recStart;
                int bars = recBars.load();
                double want = bars > 0 ? bars * bar : (recStopAsked.load() ? std::ceil((el + 1e-6) / bar) * bar : 1e9);
                recProgressOut.store(bars > 0 ? (float) (el / want) : (float) std::fmod(el / bar, 1.0));
                if (el >= want - 1e-9 || recClip.len >= (long) recClip.l.size()) {
                    recClip.beats = std::max(bar, std::round(el / bar) * bar); recBeatsOut.store(recClip.beats);
                    // short fades at the ends: a seamless loop
                    long f = std::min<long>(recClip.len / 4, (long) (0.003 * sr));
                    for (long k = 0; k < f; ++k) { float g = (float) k / (float) f; recClip.l[(size_t) k] *= g; recClip.r[(size_t) k] *= g;
                                                   recClip.l[(size_t) (recClip.len - 1 - k)] *= g; recClip.r[(size_t) (recClip.len - 1 - k)] *= g; }
                    int c = recCol.load();
                    cell[rr][c].store(&recClip);
                    playing[rr].store(c); start[rr] = recStart + recClip.beats;          // it plays on, from its start
                    recState.store(3);
                }
            }
            // play
            float l = 0, r = 0;
            for (int rw = 0; rw < ROWS; ++rw) {
                float g = mute[rw].load(std::memory_order_relaxed) ? 0.0f : vol[rw].load(std::memory_order_relaxed);
                g = g * g * 1.6f;
                gS[rw] += 0.003f * (g - gS[rw]);
                int c = playing[rw].load(std::memory_order_relaxed);
                float xl = 0, xr = 0;
                if (c >= 0) {
                    const LoopClip* cl = cell[rw][c].load(std::memory_order_acquire);
                    if (cl && cl->len > 4 && gb >= start[rw] - 1e-9) {
                        double pos = std::fmod(gb - start[rw], cl->beats) / cl->beats;
                        if (pos < 0) pos += 1.0;
                        if (i == n - 1) progress[rw].store((float) pos);
                        double x = pos * (double) cl->len;
                        xl = read(cl->l, cl->len, x); xr = read(cl->r, cl->len, x);
                        lastL[rw] = xl; lastR[rw] = xr;
                    }
                }
                if (fade[rw] > 0) { fade[rw] -= 1.0f / (0.006f * (float) sr); float k = std::max(0.0f, fade[rw]); xl += fL[rw] * k; xr += fR[rw] * k; }
                xl *= gS[rw]; xr *= gS[rw];
                lv[rw] = std::max(lv[rw] * 0.9997f, std::abs(xl));
                l += xl; r += xr;
            }
            L[i] += l * mv; R[i] += r * mv;
        }
        for (int rw = 0; rw < ROWS; ++rw) level[rw].store(lv[rw]);
        recBeatNow = gb;
    }
    double beatNow() const { return recBeatNow; }
    double sr = 48000;

private:
    double gb = 0, lastB = 0, recStart = 0, recBeatNow = 0;
    std::array<double, ROWS> start {}, ph {};
    std::array<float, ROWS> gS {}, lv {}, fade {}, fL {}, fR {}, lastL {}, lastR {};
    void fadeRow(int r) { fade[r] = 1.0f; fL[r] = lastL[r]; fR[r] = lastR[r]; }        // the last sample dies away in 6 ms: no click on a switch
    static float read(const std::vector<float>& v, long len, double x) {
        long i0 = (long) x; float f = (float) (x - (double) i0);
        auto at = [&](long k) { k %= len; if (k < 0) k += len; return v[(size_t) k]; };
        float xm = at(i0 - 1), x0 = at(i0), x1 = at(i0 + 1), x2 = at(i0 + 2);
        float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

} // namespace arco
