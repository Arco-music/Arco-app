#pragma once
// ARCO --importcheck: audio files into the tracks. Makes many files (tempos, bar counts, 4/4 and 3/4, 44.1 / 48 kHz,
// loops cut on the bar, excerpts with a pickup and a tail, mono) and checks that the import hears the tempo, the bars
// and the first bar line - then puts them through the real looper and checks what the station plays.
#include "AudioHost.h"
#include "Analysis.h"
#include "SelfTest.h"
#include <cstdio>

namespace arco {

struct TestFile { double bpm; int bars; int bpb; double sr; int style; double pre; double tail; bool mono; };

// a groove: style 0 drums + chords + bass, 1 piano chords only (no drums), 2 bass + hats
inline juce::AudioBuffer<float> makeGroove(const TestFile& t, int seed = 1) {
    double beat = 60.0 / t.bpm * t.sr;
    int beats = t.bars * t.bpb;
    int pre = (int) std::llround(t.pre * beat), tail = (int) std::llround(t.tail * beat);
    int nS = pre + (int) std::llround(beats * beat) + tail;
    juce::AudioBuffer<float> b(t.mono ? 1 : 2, nS); b.clear();
    juce::Random rnd(seed);
    auto add = [&](int at, float v) { if (at < 0 || at >= nS) return; for (int c = 0; c < b.getNumChannels(); ++c) b.addSample(c, at, v); };
    auto tone = [&](double f, double amp, int a, int len) { for (int i = 0; i < len; ++i) { double env = std::exp(-i / (t.sr * 0.9)) * juce::jmin(1.0, i / 120.0);
        add(a + i, (float) (amp * env * (std::sin(2 * M_PI * f * i / t.sr) + 0.4 * std::sin(4 * M_PI * f * i / t.sr)))); } };
    static const double CH[4][3] = { { 220.0, 261.63, 329.63 }, { 293.66, 349.23, 440.0 }, { 220.0, 261.63, 329.63 }, { 246.94, 329.63, 415.30 } };
    static const double BASS[4] = { 110.0, 146.83, 110.0, 164.81 };
    // the pickup and the tail: a few soft notes off the grid, like a real excerpt
    int total = beats + (int) std::ceil(t.pre) + (int) std::ceil(t.tail);
    for (int k = 0; k < total; ++k) {
        int bt = k - (int) std::ceil(t.pre);                       // beat number from the first bar line
        int a = pre + (int) std::llround(bt * beat);
        bool inside = bt >= 0 && bt < beats;
        int pos = ((bt % t.bpb) + t.bpb) % t.bpb, bar = (bt >= 0 ? bt : bt - t.bpb + 1) / t.bpb;
        if (!inside) { if (a >= 0) tone(BASS[((bar % 4) + 4) % 4] * 2, 0.05, a, (int) beat); continue; }
        if (t.style == 0) {
            if (pos == 0 || (t.bpb == 4 && pos == 2))                // kick
                for (int i = 0; i < 6000; ++i) add(a + i, (float) (0.8 * std::exp(-i / 1500.0) * std::sin(2 * M_PI * (50 + 80 * std::exp(-i / 800.0)) * i / t.sr)));
            else                                                    // snare
                for (int i = 0; i < 4000; ++i) add(a + i, (float) (0.35 * std::exp(-i / 900.0) * (rnd.nextFloat() * 2 - 1)));
        }
        if (t.style != 1) {                                         // hats on the off-beats
            int hh = a + (int) std::llround(beat * 0.5);
            for (int i = 0; i < 1200; ++i) add(hh + i, (float) (0.12 * std::exp(-i / 250.0) * (rnd.nextFloat() * 2 - 1)));
        }
        if (pos == 0) {
            if (t.style != 2) for (double f : CH[bar % 4]) tone(f, t.style == 1 ? 0.12 : 0.07, a, (int) (beat * t.bpb));
            tone(BASS[bar % 4], t.style == 1 ? 0.05 : 0.14, a, (int) (beat * t.bpb));
        } else if (t.style == 1) for (double f : CH[bar % 4]) tone(f * 2, 0.05, a, (int) beat);   // chords on every beat, softer
        else if (t.style == 2) tone(BASS[bar % 4], 0.08, a, (int) (beat * 0.9));
    }
    return b;
}

inline juce::File writeTestFile(const juce::File& dir, const juce::String& name, const juce::AudioBuffer<float>& b, double sr) {
    auto f = dir.getChildFile(name); f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(new juce::FileOutputStream(f), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    return f;
}

inline int runImportCheck(AudioHost& h) {
    int fails = 0, total = 0;
    auto say = [](const char* f, auto... a) { std::printf(f, a...); std::printf("\n"); std::fflush(stdout); };
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("arco-importcheck");
    dir.createDirectory();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    const double esr = 48000.0;

    // ---------------------------------------------------------------- 1 what the analysis hears
    say("== 1. listening: tempo, bars, the first bar line");
    std::vector<TestFile> files;
    for (double bpm : { 70.0, 84.0, 90.0, 100.0, 110.0, 120.0, 128.0, 140.0, 160.0, 174.0 })
        for (int bars : { 1, 2, 4, 8 })
            for (int bpb : { 4, 3 })
                for (int style : { 0, 1, 2 }) {
                    double sr = ((int) bpm + bars + style) % 2 ? 44100.0 : 48000.0;
                    files.push_back({ bpm, bars, bpb, sr, style, 0, 0, style == 2 });            // a loop, cut on the bar
                    if (bars >= 2) files.push_back({ bpm, bars, bpb, sr, style, 1.0, 0.75, false }); // an excerpt: a pickup beat, a tail
                }
    int tempoOk = 0, barsOk = 0, downOk = 0, loops = 0, excerpts = 0, octave = 0;
    juce::StringArray bad;
    for (auto& t : files) {
        if (std::getenv("ARCO_ONLY") && juce::String(t.bpm, 0) + "-" + juce::String(t.bars) + "-" + juce::String(t.style) + "-" + juce::String((int) (t.pre * 4)) != std::getenv("ARCO_ONLY")) continue;
        auto b = makeGroove(t, (int) t.bpm * 7 + t.bars);
        auto f = writeTestFile(dir, "t.wav", b, t.sr);
        ImportPlan p; juce::String err;
        bool ok = prepareImport(f, esr, p, fm, err, t.bpb);
        bool loop = t.pre == 0 && t.tail == 0;
        double e = ok && p.fileBpm > 0 ? std::abs(p.fileBpm / t.bpm - 1.0) : 1.0;
        bool tOk = e < 0.005;
        bool oct = !tOk && ok && p.fileBpm > 0 && (std::abs(p.fileBpm / t.bpm - 2.0) < 0.01 || std::abs(p.fileBpm / t.bpm - 0.5) < 0.005);
        tempoOk += tOk; octave += oct;
        juce::String what = juce::String(t.bpm, 0) + " BPM " + juce::String(t.bars) + " bars " + juce::String(t.bpb) + "/4 style " + juce::String(t.style)
                          + (loop ? " loop" : " excerpt") + " " + juce::String((int) (t.sr / 100) / 10.0) + "k";
        // half or double the tempo is the same music (the sheet's 1/2 and x2 choose): the loop then has twice or half
        // the bars, and the bar lines still fall on the music's bar lines
        double fam = tOk ? 1.0 : (oct ? p.fileBpm / t.bpm : 0.0);
        if (loop) {
            ++loops;
            bool bOk = fam > 0 && p.tempo.wholeBars && std::abs(p.tempo.bars - t.bars * fam) < 0.01;
            barsOk += bOk;
            if (fam == 0 || !bOk) bad.add(what + "  ->  " + juce::String(p.fileBpm, 2) + " BPM, " + (p.tempo.wholeBars ? juce::String(p.tempo.bars) + " bars" : juce::String("not a loop")));
        } else {
            ++excerpts;
            double want = t.pre * 60.0 / t.bpm * esr;                                       // the first bar line, in the engine's samples
            double trueBar = t.bpb * 60.0 / t.bpm * esr;
            double dd = std::fmod(std::abs(p.tempo.downbeatOffset - want), trueBar); dd = juce::jmin(dd, trueBar - dd);
            double dErr = dd / esr * 1000.0;
            bool dOk = fam > 0 && dErr < 25.0;
            downOk += dOk;
            if (!dOk) bad.add(what + "  ->  " + juce::String(p.fileBpm, 2) + " BPM, first bar line " + juce::String(dErr, 0) + " ms off");
        }
    }
    say("  tempo right: %d of %d   (half / double: %d - the sheet's 1/2 and x2 fix those)", tempoOk, (int) files.size(), octave);
    say("  loops: the bar count right: %d of %d", barsOk, loops);
    say("  excerpts: the first bar line right (25 ms): %d of %d", downOk, excerpts);
    for (auto& s : bad) say("    miss: %s", s.toRawUTF8());
    total += 3;
    if (tempoOk + octave < (int) files.size() * 97 / 100) { ++fails; say("  FAIL  tempo"); }
    if (barsOk < loops * 95 / 100) { ++fails; say("  FAIL  bar count of loops"); }
    if (downOk < excerpts * 90 / 100) { ++fails; say("  FAIL  first bar line of excerpts"); }

    // ---------------------------------------------------------------- 2 into the real station
    say("== 2. into the tracks: the station's tempo, bars and loop length");
    h.devices.addAudioDeviceType(std::make_unique<FakeType>());
    h.devices.setCurrentAudioDeviceType("ARCO test", true);
    h.openAudio("Test Speakers", "Test Mic", 48000, 512);
    const int N = 512; float* outs[8]; std::vector<float> ob[8]; for (int i = 0; i < 8; ++i) { ob[i].assign(N, 0); outs[i] = ob[i].data(); }
    std::vector<float> ib(N, 0.0f); const float* ins[2] = { ib.data(), ib.data() };
    juce::AudioIODeviceCallbackContext ctx;
    float lastOut = 0;
    auto run = [&](double sec) { int blocks = (int) (sec * 48000 / N); lastOut = 0;
        for (int k = 0; k < blocks; ++k) { h.audioDeviceIOCallbackWithContext(ins, 2, outs, 8, N, ctx); h.model.tick(); h.pollImport();
                                           for (int i = 0; i < N; ++i) lastOut = juce::jmax(lastOut, std::abs(ob[0][(size_t) i])); } };
    auto waitImport = [&] { for (int k = 0; k < 400 && h.importing(); ++k) run(0.05); run(0.3); };
    auto sv = [&](const char* n) { return (double) h.engine.snap(n).v.load(); };
    auto check = [&](bool ok, const juce::String& w, double v) { ++total; if (!ok) ++fails; say("  %s  %-70s %.3f", ok ? "ok  " : "FAIL", w.toRawUTF8(), v); };
    struct Case { TestFile t; double startBpm; };
    std::vector<Case> cases = { { { 100, 4, 4, 44100, 0, 0, 0, false }, 120 }, { { 128, 2, 4, 48000, 0, 0, 0, false }, 90 },
                                { { 84, 4, 4, 44100, 1, 0, 0, false }, 120 }, { { 140, 8, 4, 48000, 2, 0, 0, true }, 100 },
                                { { 90, 4, 3, 48000, 0, 0, 0, false }, 120 }, { { 110, 4, 4, 44100, 0, 1.0, 0.75, false }, 120 },
                                { { 174, 4, 4, 48000, 0, 0, 0, false }, 120 } };
    for (auto& cs : cases) {
        auto& t = cs.t;
        h.newSong(); run(0.4);
        h.model.set("beats", t.bpb); h.model.set("bpm", cs.startBpm); h.model.set("countin", 0); run(0.3);
        auto f = writeTestFile(dir, "case.wav", makeGroove(t, 3), t.sr);
        ImportPlan p; juce::String err;
        prepareImport(f, h.engine.sr, p, fm, err, (int) h.model.get("beats"));
        h.importKeep(p, 1); waitImport();
        if ((int) sv("transport") != 1) h.model.press("st_looper", "playall");
        run(1.5); say("    (transport %.0f, track 1 state %d)", sv("transport"), h.trackState(1));
        double bl = sv("sv_bl"), len = sv("sv_len0"), bts = sv("sv_bts");
        double bpm = bl > 16 ? 60.0 * h.engine.sr / bl : 0, bars = bl > 16 && bts > 0 ? len / (bl * bts) : 0;
        juce::String w = juce::String(t.bpm, 0) + " BPM, " + juce::String(t.bars) + " bars, " + juce::String(t.bpb) + "/4" + (t.pre > 0 ? " excerpt" : " loop")
                       + " (station was at " + juce::String(cs.startBpm, 0) + ")";
        double fam = std::abs(bpm - t.bpm) < 0.05 ? 1.0 : (std::abs(bpm - t.bpm * 0.5) < 0.05 ? 0.5 : (std::abs(bpm - t.bpm * 2) < 0.1 ? 2.0 : 0.0));
        check(fam > 0, "track 1: the station takes the file's tempo (or half / double) - " + w, bpm);
        check(fam > 0 && std::abs(bars - t.bars * fam) < 0.001 && (int) std::round(bts) == t.bpb, "  ... exactly " + juce::String(t.bars) + " bars of " + juce::String(t.bpb), bars);
        check(lastOut > 0.02f, "  ... and it plays", lastOut);
        // a second file at another tempo into track 2: fitted to the piece (the sheet's MATCH)
        TestFile t2 { t.bpm * 0.8, 2, t.bpb, 44100, 2, 0, 0, false };
        auto f2 = writeTestFile(dir, "case2.wav", makeGroove(t2, 5), t2.sr);
        ImportPlan p2; prepareImport(f2, h.engine.sr, p2, fm, err, t.bpb);
        auto piece = h.pieceInfo(false);
        p2.matchKey = false; planImport(p2, piece);
        auto out = renderImport(p2, piece);
        h.importBuffer(out, 2, "second"); waitImport();
        run(1.0);
        double len2 = sv("sv_len0"), bl2 = sv("sv_bl");
        check(std::abs(len2 - len) < 1 && std::abs(bl2 - bl) < 0.01 && h.trackState(2) != 0,
              "  track 2: a " + juce::String(t2.bpm, 0) + " BPM file fitted in - the loop and tempo unchanged", h.trackState(2));
        h.model.press("st_looper", "stopall"); run(0.3);
    }
    // all 8 tracks take a file
    {
        h.newSong(); run(0.4); h.model.set("beats", 4); h.model.set("bpm", 120); run(0.2);
        auto f = writeTestFile(dir, "eight.wav", makeGroove({ 120, 2, 4, 48000, 0, 0, 0, false }, 9), 48000);
        ImportPlan p; juce::String err; prepareImport(f, h.engine.sr, p, fm, err, 4);
        h.importKeep(p, 1); waitImport();
        for (int tr = 2; tr <= 8; ++tr) { auto piece = h.pieceInfo(false); ImportPlan q; prepareImport(f, h.engine.sr, q, fm, err, 4); planImport(q, piece);
                                          h.importBuffer(renderImport(q, piece), tr, "t" + juce::String(tr)); waitImport(); }
        int full = 0; for (int tr = 1; tr <= 8; ++tr) full += h.trackState(tr) != 0;
        check(full == 8, "all 8 tracks hold a file", full);
    }
    say("import check: %d of %d failed", fails, total);
    dir.deleteRecursively();
    return fails;
}

} // namespace arco
