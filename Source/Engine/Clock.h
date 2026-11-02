#pragma once
// ARCO 2.0: one clock for everything that plays in tempo (the arpeggiator, the DRUM MACHINE, PHRASE).
// Per sample it says where we are (in beats), at what tempo, and whether the music is running:
//   STUDIO playing  -> STUDIO's position and tempo
//   the station playing -> the looper's beat (the metronome's beat)
//   otherwise -> a free clock at the station's tempo, which never stops (an arpeggio still runs in tempo),
//                restarted on the downbeat when something asks for it (the DRUM MACHINE's own PLAY)
#include <atomic>
#include <cmath>

namespace arco {

struct Clock {
    double sr = 48000;
    float bpm = 90;                                  // the station's tempo (when the looper is not running)
    // STUDIO (set by the host at every callback)
    std::atomic<bool> studioRun { false };
    std::atomic<double> studioBeat { 0 }, studioBpm { 120 };
    std::atomic<int> studioSeq { 0 };
    // asks for a start on the downbeat (message thread)
    std::atomic<bool> restart { false };
    std::atomic<int> source { 0 };                    // what drives it now: 0 free, 1 station, 2 STUDIO (for the interface)
    std::atomic<float> beatNow { 0 };

    void init(double rate) { sr = rate; free = 0; lastSeq = -1; }
    // transport / beat / ebpm: the looper's outputs for this block; run: something asked the free clock to run (the DRUM MACHINE's PLAY)
    void process(const float* transport, const float* beat, const float* ebpm, float ownRun, float* outBeat, float* outBpm, float* outRun, int n) {
        bool st = studioRun.load(std::memory_order_relaxed);
        int seq = studioSeq.load(std::memory_order_relaxed);
        if (st && seq != lastSeq) { lastSeq = seq; sBeat = studioBeat.load(); }
        double sBpm = studioBpm.load();
        if (restart.exchange(false)) free = 0;
        bool own = ownRun > 0.5f;
        if (own && !ownWas && !st && !(transport && transport[0] > 0.5f)) free = 0;   // PLAY on the drum machine alone: from the downbeat
        ownWas = own;
        int src = 0;
        for (int i = 0; i < n; ++i) {
            bool stationRun = transport && transport[i] > 0.5f && transport[i] < 1.5f;
            double b, t;
            if (st) { b = sBeat; t = sBpm; sBeat += sBpm / 60.0 / sr; src = 2; free = b; }
            else if (stationRun) { b = beat[i]; t = ebpm && ebpm[i] > 20.0f ? ebpm[i] : bpm; free = b; src = 1; }
            else { t = ebpm && ebpm[i] > 20.0f ? ebpm[i] : bpm; free += t / 60.0 / sr; b = free; }
            outBeat[i] = (float) b; outBpm[i] = (float) t; outRun[i] = (st || stationRun || own) ? 1.0f : 0.0f;
        }
        source.store(src); beatNow.store(outBeat[n - 1]);
    }
private:
    double free = 0, sBeat = 0; int lastSeq = -1; bool ownWas = false;
};

} // namespace arco
