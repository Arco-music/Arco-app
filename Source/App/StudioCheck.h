#pragma once
// ARCO --studiocheck: the STUDIO, tested as a player would use it - files in (as they are / fitted), the sound
// unchanged (bit for bit where it can be), tempo and bar lines, new tracks, recording, delete / duplicate, undo,
// play while editing, another sample rate, save and open. Many files, many times.
#include "AudioHost.h"
#include "Analysis.h"
#include "ImportCheck.h"
#include "../UI/Shell.h"
#include "../UI/StudioView.h"
#include <cstdio>

namespace arco {

inline int runStudioCheck(AudioHost& h) {
    int fails = 0, total = 0;
    auto say = [](const char* f, auto... a) { std::printf(f, a...); std::printf("\n"); std::fflush(stdout); };
    auto check = [&](bool ok, const juce::String& w, double v = 0) { ++total; if (!ok) ++fails; say("  %s  %-78s %.4f", ok ? "ok  " : "FAIL", w.toRawUTF8(), v); };
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("arco-studiocheck"); dir.createDirectory();
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    h.devices.addAudioDeviceType(std::make_unique<FakeType>());
    h.devices.setCurrentAudioDeviceType("ARCO test", true);
    h.nLive = 2; h.inputCh[0] = 1; h.inputCh[1] = 2;
    h.openAudio("Test Speakers", "Test Mic", 48000, 512);
    const double sr = h.engine.sr;
    const int N = 512; std::vector<float> ob[8]; float* outs[8]; for (int i = 0; i < 8; ++i) { ob[i].assign(N, 0); outs[i] = ob[i].data(); }
    std::vector<float> ib0(N, 0.0f), ib1(N, 0.0f); const float* ins[2] = { ib0.data(), ib1.data() };
    juce::AudioIODeviceCallbackContext ctx;
    std::vector<float> capL, capR; bool capture = false; double inPh = 0; float inAmp = 0;
    auto run = [&](double sec) { int blocks = (int) std::ceil(sec * sr / N);
        for (int k = 0; k < blocks; ++k) {
            for (int i = 0; i < N; ++i) { ib1[(size_t) i] = inAmp * (float) std::sin(inPh); inPh += 2 * M_PI * 330.0 / sr; }
            h.audioDeviceIOCallbackWithContext(ins, 2, outs, 8, N, ctx); h.model.tick(); h.pollImport(); h.studio.collectRecordings();
            if (capture) { capL.insert(capL.end(), ob[0].begin(), ob[0].end()); capR.insert(capR.end(), ob[1].begin(), ob[1].end()); } } };
    auto& S = h.studio;
    auto fresh = [&] { S.stop(); { const juce::ScopedLock sl(S.lock); S.song = StudioSong(); S.song.sr = sr; S.song.countIn = false; S.song.metronome = false; S.laneStrip.clear(); S.laneScene.clear(); }
                       S.ensureReturns(); S.refresh(); S.pos = 0; };
    auto plan = [&](const juce::File& f) { ImportPlan p; juce::String err; prepareImport(f, sr, p, fm, err, 4, 1200.0); return p; };
    auto render = [&](juce::AudioBuffer<float>& out) { return S.renderAll(out, nullptr); };
    // the difference between two signals, in dB below the signal (higher = closer)
    auto snr = [](const float* a, const float* b, int n) { double s = 0, d = 0; for (int i = 0; i < n; ++i) { s += (double) a[i] * a[i]; d += ((double) a[i] - b[i]) * ((double) a[i] - b[i]); }
                                                             return d <= 1e-30 ? 200.0 : 10.0 * std::log10(s / d); };
    auto sine = [&](double hz, double secs, double fsr, float amp, int ch) { juce::AudioBuffer<float> b(ch, (int) (secs * fsr)); for (int i = 0; i < b.getNumSamples(); ++i) for (int c = 0; c < ch; ++c) b.setSample(c, i, amp * (float) std::sin(2 * M_PI * hz * (c ? 1.01 : 1.0) * i / fsr)); return b; };
    auto pitchOf = [](const float* x, int n, double fsr) { int zc = 0; for (int i = 1; i < n; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++zc; return zc / 2.0 / (n / fsr); };

    // ---------------------------------------------------------------- 1 the sound comes out as it went in
    say("== 1. a file in the STUDIO sounds exactly as the file");
    for (int rep = 0; rep < 3; ++rep) for (double fsr : { 48000.0, 44100.0, 96000.0 }) for (int ch : { 1, 2 }) {
        fresh();
        TestFile tf { 100.0 + rep * 7, 4, 4, fsr, rep % 3, 0, 0, ch == 1 };
        auto b = makeGroove(tf, 11 + rep);
        b.applyGain(0.9f / juce::jmax(1e-6f, b.getMagnitude(0, b.getNumSamples())));        // loud: peaks at -0.9 dB
        auto f = writeTestFile(dir, "in.wav", b, fsr);
        auto p = plan(f); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why);
        juce::AudioBuffer<float> out; bool ok = ci >= 0 && render(out);
        // the reference: the file at the sound card's rate (the same converter); the first 2 ms and last 2 ms fade
        auto ref = fsr == sr ? b : analysis::resample(b, fsr, sr);
        int st = ci >= 0 ? (int) S.song.clips[(size_t) ci].start : 0, n = juce::jmin(ref.getNumSamples(), ok ? out.getNumSamples() - st : 0) - 400;
        double q = n > 1000 ? snr(ref.getReadPointer(0) + 200, out.getReadPointer(0) + st + 200, n - 200) : 0;
        double qr = n > 1000 ? snr(ref.getReadPointer(ref.getNumChannels() > 1 ? 1 : 0) + 200, out.getReadPointer(1) + st + 200, n - 200) : 0;
        check(ok && q > (fsr == sr ? 90 : 70) && qr > (fsr == sr ? 90 : 70), juce::String((int) (fsr / 100) / 10.0) + " kHz " + (ch == 1 ? "mono" : "stereo")
              + " groove at -0.9 dBFS: out = in (dB of difference)", juce::jmin(q, qr));
    }
    {   // a pure tone: no distortion, the right pitch, the right level - from a 44.1 kHz file
        fresh();
        auto b = sine(1000.0, 3.0, 44100, 0.95f, 2);
        auto p = plan(writeTestFile(dir, "tone.wav", b, 44100)); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why);
        juce::AudioBuffer<float> out; render(out);
        int st = ci >= 0 ? (int) S.song.clips[(size_t) ci].start : 0;
        int n = juce::jmin(out.getNumSamples() - st - 2000, (int) (2.5 * sr));
        double hz = n > 0 ? pitchOf(out.getReadPointer(0) + st + 1000, n, sr) : 0;
        float pk = n > 0 ? out.getMagnitude(0, st + 1000, n) : 0;
        // distortion: the tone removed (fit a sine), what is left
        double sc = 0, cc = 0, e = 0, rest = 0;
        for (int i = 0; i < n; ++i) { double t = (double) i / sr * 2 * M_PI * 1000.0; double v = out.getSample(0, st + 1000 + i); sc += v * std::sin(t); cc += v * std::cos(t); }
        double A = 2 * sc / juce::jmax(1, n), B = 2 * cc / juce::jmax(1, n);
        for (int i = 0; i < n; ++i) { double t = (double) i / sr * 2 * M_PI * 1000.0; double v = out.getSample(0, st + 1000 + i), fitv = A * std::sin(t) + B * std::cos(t); e += fitv * fitv; rest += (v - fitv) * (v - fitv); }
        double thd = rest > 0 ? 10 * std::log10(e / rest) : 200;
        check(std::abs(hz - 1000.0) < 1.0, "a 1 kHz tone from a 44.1 kHz file plays at 1 kHz (not slower, not faster)", hz);
        check(std::abs(pk - 0.95f) < 0.02f, "  at its own level (-0.45 dBFS, the master's limiter only holds overs)", pk);
        check(thd > 70, "  no distortion (dB below the tone)", thd);
    }
    {   // the live path (what you hear while playing) = the export
        fresh();
        auto b = makeGroove({ 120, 2, 4, 48000, 0, 0, 0, false }, 21); b.applyGain(0.8f / b.getMagnitude(0, b.getNumSamples()));
        auto p = plan(writeTestFile(dir, "live.wav", b, 48000)); juce::String why;
        S.importAudio(p, -1, 0, false, why);
        juce::AudioBuffer<float> out; render(out);
        capL.clear(); capR.clear(); S.pos = 0; S.play(); capture = true; run(3.0); capture = false; S.stop();
        // find the start in the capture (the sound card adds a block or two)
        int best = 0; double bs = -1;
        for (int off = 0; off < 4096; ++off) { double s = 0; for (int i = 0; i < 4000; i += 3) s += capL[(size_t) (off + i + 2000)] * out.getSample(0, i + 2000); if (s > bs) { bs = s; best = off; } }
        int n = juce::jmin((int) capL.size() - best - 100, out.getNumSamples()) - 4000;
        double q = n > 1000 ? snr(out.getReadPointer(0) + 2000, capL.data() + best + 2000, n) : 0;
        check(q > 60, "what plays live is what the export renders (dB of difference)", q);
    }

    // ---------------------------------------------------------------- 2 tempo: heard, kept, or fitted
    say("== 2. the tempo of a file: the song takes it, or the file is fitted to the song");
    int tOk = 0, tN = 0, aOk = 0, fOk = 0, fN = 0;
    for (double bpm : { 72.0, 90.0, 100.0, 120.0, 128.0, 140.0 }) for (int bars : { 2, 4, 8 }) for (double pre : { 0.0, 1.0 }) {
        fresh();
        TestFile tf { bpm, bars, 4, (bars + (int) bpm) % 2 ? 44100.0 : 48000.0, (int) bpm % 3, pre, pre > 0 ? 0.5 : 0.0, false };
        auto f = writeTestFile(dir, "t.wav", makeGroove(tf, (int) bpm + bars), tf.sr);
        auto p = plan(f); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why);
        ++tN;
        double fam = std::abs(S.song.bpm / bpm - 1) < 0.003 ? 1 : (std::abs(S.song.bpm / bpm - 0.5) < 0.002 || std::abs(S.song.bpm / bpm - 2) < 0.006 ? 2 : 0);
        tOk += fam > 0;
        // the file's first bar line on a bar line of the song
        if (ci >= 0) { auto& c = S.song.clips[(size_t) ci]; double bar = 60.0 / juce::jmax(bpm, S.song.bpm) * 4 * sr; double fileDown = pre * 60.0 / bpm * sr;   // at double tempo, its bars are half as long
                       double ph = std::fmod(c.start + fileDown, bar); ph = juce::jmin(ph, bar - ph); aOk += fam > 0 && ph < 0.02 * sr;
                       if (fam == 0 || ph >= 0.02 * sr) say("    miss: %.0f BPM %d bars pickup %.0f -> song %.2f BPM, bar line %.0f ms off", bpm, bars, pre, S.song.bpm, ph / sr * 1000); }
        // a second file at another tempo, fitted: its length follows the song's tempo, its pitch is the same
        if (fam > 0 && bars == 4) {
            ++fN;
            double b2 = bpm * 0.85;
            auto f2 = writeTestFile(dir, "t2.wav", makeGroove({ b2, 2, 4, 44100, 2, 0, 0, false }, 3), 44100);
            auto p2 = plan(f2);
            int c2 = S.importAudio(p2, -1, 0, true, why);
            if (c2 >= 0 && p2.fileBpm > 0) {
                double want = p2.audio.getNumSamples() * p2.fileBpm / S.song.bpm;
                bool lenOk = std::abs(S.song.clips[(size_t) c2].length - want) < 2;
                juce::AudioBuffer<float> out; S.renderAll(out, nullptr, S.song.clips[(size_t) c2].lane);
                std::vector<float> mono((size_t) out.getNumSamples()); for (int i = 0; i < out.getNumSamples(); ++i) mono[(size_t) i] = out.getSample(0, i);
                auto t2 = analysis::tempo(mono.data(), (int) mono.size(), sr, 4, S.song.bpm);
                bool tempoOk = std::abs(t2.bpm / S.song.bpm - 1) < 0.01 || std::abs(t2.bpm / S.song.bpm - 0.5) < 0.01 || std::abs(t2.bpm / S.song.bpm - 2) < 0.02;
                fOk += lenOk && tempoOk;
                if (!(lenOk && tempoOk)) say("    fit miss: %.1f -> %.1f BPM, length %ld want %.0f, heard %.2f", p2.fileBpm, S.song.bpm, S.song.clips[(size_t) c2].length, want, t2.bpm);
            }
        }
    }
    check(tOk == tN, "the first file gives the song its tempo (" + juce::String(tOk) + " of " + juce::String(tN) + ")", tOk);
    check(aOk == tN, "  its first bar line falls on the song's bar line (" + juce::String(aOk) + " of " + juce::String(tN) + ")", aOk);
    check(fOk == fN, "  a second file fitted: the song's tempo, its length follows (" + juce::String(fOk) + " of " + juce::String(fN) + ")", fOk);
    {   // as it is: not touched (sample for sample), the song keeps its tempo
        fresh();
        auto p = plan(writeTestFile(dir, "a.wav", makeGroove({ 100, 4, 4, 48000, 0, 0, 0, false }, 1), 48000)); juce::String why;
        S.importAudio(p, -1, 0, false, why);
        auto b = makeGroove({ 84, 2, 4, 48000, 1, 0, 0, false }, 2);
        auto p2 = plan(writeTestFile(dir, "b.wav", b, 48000));
        int c2 = S.importAudio(p2, -1, 0, false, why);
        auto& src = *S.song.sources[(size_t) S.song.clips[(size_t) c2].source];
        double d = 0; for (int i = 0; i < b.getNumSamples(); i += 5) d = juce::jmax(d, (double) std::abs(src.l[(size_t) i] - b.getSample(0, i)));
        check(std::abs(S.song.bpm - 100) < 0.01 && (long) src.size() == b.getNumSamples() && d < 1e-6, "a second file as it is: sample for sample, the song stays at 100 BPM", d);
    }
    {   // a pitch stays the pitch when fitted
        fresh();
        auto p = plan(writeTestFile(dir, "a.wav", makeGroove({ 120, 4, 4, 48000, 0, 0, 0, false }, 1), 48000)); juce::String why;
        S.importAudio(p, -1, 0, false, why);
        auto b = makeGroove({ 100, 4, 4, 48000, 2, 0, 0, false }, 5);
        for (int i = 0; i < b.getNumSamples(); ++i) for (int c = 0; c < b.getNumChannels(); ++c) b.setSample(c, i, b.getSample(c, i) * 0.3f + 0.4f * (float) std::sin(2 * M_PI * 440.0 * i / 48000.0));
        auto p2 = plan(writeTestFile(dir, "c.wav", b, 48000));
        int c2 = S.importAudio(p2, -1, 0, true, why);
        juce::AudioBuffer<float> out; S.renderAll(out, nullptr, S.song.clips[(size_t) c2].lane);
        auto k = analysis::key(out.getReadPointer(0), out.getNumSamples(), sr);
        // the strongest pitch: a narrow look round 440
        double best = 0, bhz = 0; int n = juce::jmin(out.getNumSamples(), (int) (sr * 4));
        for (double hz = 400; hz <= 480; hz += 0.5) { double s = 0, c = 0; for (int i = 0; i < n; i += 2) { double t = 2 * M_PI * hz * i / sr; s += out.getSample(0, i) * std::sin(t); c += out.getSample(0, i) * std::cos(t); } double m = s * s + c * c; if (m > best) { best = m; bhz = hz; } }
        juce::ignoreUnused(k);
        check(std::abs(bhz - 440) <= 1.0, "a fitted file (100 -> 120 BPM) keeps its pitch (Hz)", bhz);
    }

    // ---------------------------------------------------------------- 3 tracks: add, record, delete, duplicate, undo - while playing
    say("== 3. new tracks: clean, heard at once, while the song plays");
    for (int round = 0; round < 25; ++round) {
        fresh();
        auto p = plan(writeTestFile(dir, "a.wav", makeGroove({ 110, 2, 4, 48000, 0, 0, 0, false }, round), 48000)); juce::String why;
        S.importAudio(p, -1, 0, false, why);
        S.song.loopOn = true; S.song.loopStart = 0; S.song.loopEnd = (long) S.song.barLen() * 2;
        S.pos = 0; S.play(); run(0.3);
        bool okAll = true;
        long posBefore = S.pos.load();
        int nl = (int) S.song.lanes.size();
        int a = S.addLane(round % 2 ? LaneInstrument : LaneAudio, "NEW");
        run(0.1);
        okAll &= S.playing.load() && S.pos.load() > posBefore;                              // adding a track does not stop the song
        okAll &= (int) S.song.lanes.size() == nl + 1 && S.laneStrip.size() == S.song.lanes.size() && S.laneScene.size() == S.song.lanes.size();
        okAll &= S.song.lanes[(size_t) a].kind != LaneReturn && S.song.lanes.back().kind == LaneReturn;   // before the returns
        for (auto& c : S.song.clips) okAll &= c.lane >= 0 && c.lane < (int) S.song.lanes.size() && S.song.lanes[(size_t) c.lane].kind == LaneAudio;
        // a file dropped on the new track goes there
        auto p2 = plan(writeTestFile(dir, "b.wav", makeGroove({ 110, 1, 4, 48000, 2, 0, 0, false }, 7), 48000));
        int c2 = S.importAudio(p2, a, (long) S.song.barLen(), false, why);
        bool onIt = c2 >= 0 && (S.song.lanes[(size_t) a].kind == LaneAudio ? S.song.clips[(size_t) c2].lane == a : S.song.clips[(size_t) c2].lane != a);
        okAll &= onIt;
        // duplicate it (with its clips), delete the original, undo, redo
        int clips0 = (int) S.song.clips.size();
        S.snapshot(); int d = S.duplicateLane(S.song.clips[(size_t) c2].lane); run(0.05);
        okAll &= d >= 0 && (int) S.song.clips.size() == clips0 + 1;
        S.snapshot(); S.deleteLane(d); run(0.05);
        okAll &= (int) S.song.clips.size() == clips0;
        for (auto& c : S.song.clips) okAll &= c.lane >= 0 && c.lane < (int) S.song.lanes.size();
        S.undo(); okAll &= (int) S.song.clips.size() == clips0 + 1; S.redo(); okAll &= (int) S.song.clips.size() == clips0;
        S.refresh(); run(0.1);
        okAll &= S.playing.load();
        S.stop();
        if (!okAll || round == 0) check(okAll, "round " + juce::String(round + 1) + ": add / drop on it / duplicate / delete / undo / redo, still playing", round);
        if (!okAll) break;
        if (round == 24) check(true, "25 rounds, every one clean", 25);
    }
    {   // record into a NEW track with nothing armed (press record): it gets the sound
        fresh();
        int li = S.addLane(LaneAudio, "TAKE 1"); S.song.lanes[(size_t) li].input = 1; S.song.lanes[(size_t) li].arm = true;
        S.collectRecordings(); S.pos = 0; S.play(); S.startRecording(); inAmp = 0.3f; run(1.5); inAmp = 0; S.stopRecording(); S.stop();
        float pk = 0; long len = 0; for (auto& c : S.song.clips) if (c.lane == li && c.source >= 0) { auto& src = *S.song.sources[(size_t) c.source]; len = (long) src.size(); for (float x : src.l) pk = juce::jmax(pk, std::abs(x)); }
        check(pk > 0.25f && pk < 0.35f && len > sr, "record into a new track: its input, at its level", pk);
    }
    // ---------------------------------------------------------------- 4 another sample rate, save / open
    say("== 4. another sound card rate, save and open");
    {
        fresh();
        auto b = sine(500.0, 2.0, 48000, 0.5f, 1);
        auto p = plan(writeTestFile(dir, "s.wav", b, 48000)); juce::String why;
        S.importAudio(p, -1, 0, false, why);
        S.matchRate(44100.0);                                  // a song opened on a 44.1 kHz sound card
        S.matchRate(sr);                                       // back
        juce::AudioBuffer<float> out; render(out);
        double hz = pitchOf(out.getReadPointer(0) + 2000, (int) (1.5 * sr), sr);
        check(std::abs(hz - 500) < 0.6 && std::abs(S.song.sr - sr) < 0.5, "a song converted to 44.1 kHz and back still plays at its pitch", hz);
        auto folder = dir.getChildFile("song"); folder.createDirectory();
        double bpm = S.song.bpm; int nclips = (int) S.song.clips.size(), nl = (int) S.song.lanes.size();
        bool sv = S.save(folder);
        fresh();
        bool ld = S.load(folder);
        check(sv && ld && (int) S.song.clips.size() == nclips && (int) S.song.lanes.size() == nl && std::abs(S.song.bpm - bpm) < 0.01, "save and open: the same lanes, clips and tempo", (double) S.song.clips.size());
    }
    // ---------------------------------------------------------------- 5 every button, pressed
    say("== 5. every button of the STUDIO, pressed one by one");
    {
        fresh();
        for (auto* nm : { "a.wav", "b.wav" }) { auto p = plan(writeTestFile(dir, nm, makeGroove({ 104, 2, 4, 48000, 0, 0, 0, false }, 3), 48000)); juce::String why; S.importAudio(p, -1, 0, false, why); }
        S.addLane(LaneInstrument, "SYNTH 1");
        auto* mc = new MainComponent(h); mc->setVisible(true); mc->setSize(1440, 900);
        mc->shell.show(Screen::Studio);

        int pressedAll = 0; juce::StringArray bad;
        for (int round = 0; round < 3; ++round) {
            if (auto* sv = mc->shell.studioView()) pressedAll += sv->pressEverything(bad);
            S.stop();
        }
        for (auto& b : bad) say("    %s", b.toRawUTF8());
        check(bad.isEmpty() && pressedAll > 60, "every button pressed (3 times over): nothing broke, the song stays whole", pressedAll);
        // and a song still plays after all that
        S.pos = 0; S.play(); run(0.5); bool playsOk = S.playing.load(); S.stop();
        check(playsOk, "  ... and it still plays", 1);
        delete mc;
    }
    say("studio check: %d of %d failed", fails, total);
    fresh();
    dir.deleteRecursively();
    return fails;
}

} // namespace arco
