#pragma once
// ARCO --studiocheck: the STUDIO, tested as a player would use it - files in (as they are / fitted), the sound
// unchanged (bit for bit where it can be), tempo and bar lines, new tracks, recording, delete / duplicate, undo,
// play while editing, another sample rate, save and open. Many files, many times.
#include "AudioHost.h"
#include "Analysis.h"
#include "ImportCheck.h"
#include "../Engine/Modular.h"
#include "../UI/Shell.h"
#include "../UI/StudioView.h"
#include <cstdio>
#include <thread>

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
        if (auto* kd = std::getenv("ARCO_KEEP")) f.copyFileTo(juce::File(kd).getChildFile("keep_" + juce::String((int) bpm) + "_" + juce::String(bars) + "_" + juce::String((int) pre) + ".wav"));
        auto p = plan(f); juce::String why;
        if (std::getenv("ARCO_REPEAT2")) { std::vector<float> mono((size_t) p.audio.getNumSamples()); for (int i = 0; i < p.audio.getNumSamples(); ++i) mono[(size_t) i] = p.audio.getSample(0, i);
            juce::String r; for (int k = 0; k < 6; ++k) { auto t = analysis::tempo(mono.data(), (int) mono.size(), sr, 4); r << juce::String(t.downbeatOffset, 0) << "/" << (int) t.wholeBars << " "; } say("    T %.0f-%d-%.0f: %s", bpm, bars, pre, r.toRawUTF8()); }
        if (std::getenv("ARCO_REPEAT")) { juce::String r; for (int k = 0; k < 6; ++k) { auto q = plan(f); double cs = 0; for (int c = 0; c < q.audio.getNumChannels(); ++c) for (int i = 0; i < q.audio.getNumSamples(); i += 7) cs += q.audio.getSample(c, i) * (1 + (i % 13));
            r << juce::String(q.tempo.downbeatOffset, 0) << "/" << juce::String(cs, 6) << "/" << q.audio.getNumSamples() << " "; } say("    %.0f-%d-%.0f: %s", bpm, bars, pre, r.toRawUTF8()); }
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
    // ---------------------------------------------------------------- 7 effects: one tap on, one tap recorded into the sound
    say("== 7. effects: put on, then recorded into the sound (PRINT FX)");
    {
        fresh();
        auto b = makeGroove({ 100, 2, 4, 48000, 1, 0, 0, false }, 41); b.applyGain(0.5f / b.getMagnitude(0, b.getNumSamples()));
        auto p = plan(writeTestFile(dir, "fx.wav", b, 48000)); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why); int lane = S.song.clips[(size_t) ci].lane;
        S.song.lanes[(size_t) lane].vol = -3.0f; S.song.lanes[(size_t) lane].pan = 0.3f;     // a volume and a pan: kept as they are, not printed
        S.addChain(lane, 3);                                         // warm: saturation, EQ
        { auto& d = S.song.lanes[(size_t) lane].devices; if (!d.empty()) d[0].p[0] = 60; }   // a lot of drive: clearly heard
        juce::AudioBuffer<float> before; render(before);
        juce::AudioBuffer<float> dry; { auto saved = S.song.lanes[(size_t) lane].devices; for (auto& dv : S.song.lanes[(size_t) lane].devices) dv.on = false; S.refresh(); render(dry); S.song.lanes[(size_t) lane].devices = saved; S.refresh(); }
        int nc = S.printLane(lane, why);
        juce::AudioBuffer<float> after; render(after);
        int n = juce::jmin(before.getNumSamples(), after.getNumSamples()) - 400;
        double same = snr(before.getReadPointer(0) + 200, after.getReadPointer(0) + 200, n), sameR = snr(before.getReadPointer(1) + 200, after.getReadPointer(1) + 200, n);
        double differs = snr(dry.getReadPointer(0) + 200, before.getReadPointer(0) + 200, n);
        bool off = true; for (auto& dv : S.song.lanes[(size_t) lane].devices) off &= !dv.on;
        int onLane = 0; for (auto& c : S.song.clips) onLane += c.lane == lane;
        check(nc >= 0 && same > 60 && sameR > 60, "PRINT FX: the track sounds the same, now with the effects in its sound (dB of difference)", juce::jmin(same, sameR));
        check(differs < 30, "  (the effects were really heard: the dry sound is different, dB)", differs);
        check(off && onLane == 1 && std::abs(S.song.lanes[(size_t) lane].vol + 3) < 0.01f, "  its effects are off now (not twice), one clip, its volume kept", onLane);
        S.undo();
        bool back = false; for (auto& dv : S.song.lanes[(size_t) lane].devices) back |= dv.on;
        check(back && S.song.clips[(size_t) ci].source >= 0 && (int) S.song.clips.size() == ci + 1, "  UNDO: the effects and the dry clip are back", back);
        // a reverb: its tail is kept (the clip grows)
        S.addChain(lane, 5);
        long len0 = S.song.clips[(size_t) ci].length;
        int nc2 = S.printLane(lane, why);
        check(nc2 >= 0 && S.song.clips[(size_t) nc2].length > len0 + (long) (0.3 * sr), "  a reverb printed: its tail is kept (seconds longer)", nc2 >= 0 ? (S.song.clips[(size_t) nc2].length - len0) / sr : 0);
        // an instrument lane: printed to a new audio lane, the instrument muted
        int il = S.addLane(LaneInstrument, "SYNTH 1");
        StudioClip mc; mc.lane = il; mc.source = -1; mc.start = 0; mc.length = (long) (sr * 2); mc.midiLen = mc.length; mc.notes.push_back({ 0.0, sr * 1.5, 60, 0.9f });
        S.song.clips.push_back(mc); S.addChain(il, 5); S.refresh();
        int nl = (int) S.song.lanes.size();
        int nc3 = S.printLane(il, why);
        float pk = 0; if (nc3 >= 0) for (float v : S.song.sources[(size_t) S.song.clips[(size_t) nc3].source]->l) pk = juce::jmax(pk, std::abs(v));
        check(nc3 >= 0 && (int) S.song.lanes.size() == nl + 1 && S.song.lanes[(size_t) il].mute && S.song.clips[(size_t) nc3].lane == il + 1 && pk > 0.01f,
              "  an instrument printed: its sound on a new audio track under it, the instrument muted", pk);
    }
    // ---------------------------------------------------------------- 6 playing while the screen draws (the real app)
    say("== 6. playing in real time while the STUDIO screen draws itself");
    {
        fresh();
        auto b = makeGroove({ 110, 4, 4, 44100, 0, 0, 0, false }, 77); b.applyGain(0.8f / b.getMagnitude(0, b.getNumSamples()));
        auto p = plan(writeTestFile(dir, "rt.wav", b, 44100)); juce::String why;
        S.importAudio(p, -1, 0, false, why);
        auto p2 = plan(writeTestFile(dir, "rt2.wav", makeGroove({ 110, 2, 4, 48000, 2, 0, 0, true }, 8), 48000));
        S.importAudio(p2, -1, 0, false, why);
        juce::AudioBuffer<float> ref; render(ref);
        auto* mc = new MainComponent(h); mc->setVisible(true); mc->setSize(1440, 900);
        mc->shell.show(Screen::Studio);
        auto* sv = mc->shell.studioView();
        S.missedBlocks = 0; capL.clear(); capR.clear();
        S.pos = 0; S.play();
        std::atomic<bool> stopA { false };
        std::vector<float> cl, cr;
        std::thread audio([&] {
            std::vector<float> o[8]; float* op[8]; for (int i = 0; i < 8; ++i) { o[i].assign(N, 0); op[i] = o[i].data(); }
            std::vector<float> z(N, 0.0f); const float* ip[2] = { z.data(), z.data() };
            auto t0 = juce::Time::getMillisecondCounterHiRes(); long done = 0;
            while (!stopA.load()) {
                h.audioDeviceIOCallbackWithContext(ip, 2, op, 8, N, ctx);
                cl.insert(cl.end(), o[0].begin(), o[0].end()); cr.insert(cr.end(), o[1].begin(), o[1].end());
                done += N;
                double due = t0 + done / sr * 1000.0;                 // at the pace of a sound card
                while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep(1);
            } });
        auto tEnd = juce::Time::getMillisecondCounterHiRes() + 4000; int frames = 0;
        while (juce::Time::getMillisecondCounterHiRes() < tEnd) {
            if (sv) { juce::Image im(juce::Image::ARGB, sv->getWidth(), sv->getHeight(), true); juce::Graphics g(im); sv->paintEntireComponent(g, false); ++frames; }
            h.model.tick(); S.collectRecordings();
        }
        stopA = true; audio.join(); S.stop();
        int best = 0; double bs = -1;
        for (int off = 0; off < 6000 && off + 9000 < (int) cl.size(); ++off) { double sm = 0; for (int i = 0; i < 6000; i += 3) sm += cl[(size_t) (off + i + 3000)] * ref.getSample(0, i + 3000); if (sm > bs) { bs = sm; best = off; } }
        int n = juce::jmin((int) cl.size() - best - 100, ref.getNumSamples()) - 6000;
        double q = n > 1000 ? snr(ref.getReadPointer(0) + 3000, cl.data() + best + 3000, n) : 0;
        say("    (%d screens drawn in 4 s, %d blocks missed, %.1f s played)", frames, S.missedBlocks.load(), cl.size() / sr);
        check(S.missedBlocks.load() == 0, "while the screen draws, not one block of sound is lost", S.missedBlocks.load());
        check(q > 60, "  what you hear is the song, exactly (dB of difference)", q);
        delete mc;
    }
    // ---------------------------------------------------------------- 8 a full DAW: takes, comping, punch, crossfades, freeze, ripple, time, clipboard, MIDI
    say("== 8. takes, comping, punch, crossfades, freeze, ripple, time, copy / paste, MIDI files");
    auto peakOf = [](const StudioSource& src, long a, long b) { float pk = 0; for (long i = juce::jmax(0L, a); i < juce::jmin(b, src.size()); ++i) pk = juce::jmax(pk, std::abs(src.l[(size_t) i])); return pk; };
    auto peakOut = [](const juce::AudioBuffer<float>& b, long a, long e) { float pk = 0; for (long i = juce::jmax(0L, a); i < juce::jmin(e, (long) b.getNumSamples()); ++i) pk = juce::jmax(pk, std::abs(b.getSample(0, (int) i))); return pk; };
    {   // loop recording: every pass a take; the last whole one plays; another chosen
        fresh();
        int li = S.addLane(LaneAudio, "LOOPREC"); S.song.lanes[(size_t) li].input = 1; S.song.lanes[(size_t) li].arm = true; S.song.lanes[(size_t) li].vol = 0;
        S.song.bpm = 120; S.song.loopOn = true; S.song.loopStart = 0; S.song.loopEnd = (long) S.song.barLen();
        S.collectRecordings(); S.pos = 0; S.play(); S.startRecording();
        double bar = S.song.barLen() / sr;
        inAmp = 0.2f; run(bar); inAmp = 0.4f; run(bar); inAmp = 0.6f; run(bar * 0.5);
        inAmp = 0; S.stopRecording(); S.stop();
        const StudioClip* c = nullptr; for (auto& x : S.song.clips) if (x.lane == li) c = &x;
        bool ok = c != nullptr && c->takes.size() == 3 && c->start == 0 && c->length == S.song.loopEnd;
        float a0 = ok ? peakOf(*S.song.sources[(size_t) c->takes[0].source], 2000, (long) (sr * bar) - 2000) : 0;
        float a1 = ok ? peakOf(*S.song.sources[(size_t) c->takes[1].source], 2000, (long) (sr * bar) - 2000) : 0;
        float a2 = ok ? peakOf(*S.song.sources[(size_t) c->takes[2].source], 2000, (long) (sr * bar * 0.4)) : 0;
        check(ok && std::abs(a0 - 0.2f) < 0.03f && std::abs(a1 - 0.4f) < 0.03f && std::abs(a2 - 0.6f) < 0.03f, "loop recording: 3 passes = 3 takes of one clip, each its own sound", c ? (double) c->takes.size() : 0.0);
        check(ok && c->take == 1, "  the last whole pass plays (take 2)", ok ? c->take + 1 : 0);
        int ci = (int) (c - S.song.clips.data());
        S.chooseTake(ci, 2);
        juce::AudioBuffer<float> out; render(out);
        float pk = peakOut(out, (long) (0.1 * sr), (long) (0.4 * bar * sr));
        check(std::abs(pk - 0.6f) < 0.04f, "  take 3 chosen: it is what plays", pk);
        S.undo(); render(out); pk = peakOut(out, (long) (0.1 * sr), (long) (0.4 * bar * sr));
        check(std::abs(pk - 0.4f) < 0.04f, "  UNDO: take 2 again", pk);
    }
    {   // recording over a clip: the old one becomes a take; comping: split, a take for each piece
        fresh();
        auto b = sine(220.0, 3.0, 48000, 0.5f, 1);
        auto p = plan(writeTestFile(dir, "old.wav", b, 48000)); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why); int li = S.song.clips[(size_t) ci].lane;
        juce::AudioBuffer<float> before; render(before);
        S.song.lanes[(size_t) li].input = 1; S.song.lanes[(size_t) li].arm = true; S.song.loopOn = false;
        S.collectRecordings(); S.pos = (long) (0.5 * sr); S.play(); S.startRecording(); inAmp = 0.3f; run(2.0); inAmp = 0; S.stopRecording(); S.stop();
        int nc = -1; for (int i = 0; i < (int) S.song.clips.size(); ++i) if (S.song.clips[(size_t) i].takes.size() == 2) nc = i;
        check(nc >= 0 && S.song.clips[(size_t) nc].take == 1, "recording over a clip: the old sound is kept as take 1 of the new clip", nc >= 0 ? (double) S.song.clips[(size_t) nc].takes.size() : 0.0);
        if (nc >= 0) {
            S.chooseTake(nc, 0);
            juce::AudioBuffer<float> out; render(out);
            long a = (long) (0.6 * sr), n = (long) (1.8 * sr);
            double q = snr(before.getReadPointer(0) + a, out.getReadPointer(0) + a, (int) n);
            check(q > 60, "  take 1 chosen: the old sound plays exactly as before (dB of difference)", q);
            // comp: split the clip in two, the second half takes the new take
            { const juce::ScopedLock sl(S.lock); auto& c = S.song.clips[(size_t) nc]; StudioClip r = c; long half = c.length / 2; c.length = half; r.start += half; r.offset += half; r.length -= half; S.song.clips.push_back(r); }
            S.chooseTake((int) S.song.clips.size() - 1, 1);
            render(out);
            float firstPk = peakOut(out, (long) (0.6 * sr), (long) (1.4 * sr)), secondPk = peakOut(out, (long) (1.6 * sr), (long) (2.4 * sr));
            check(std::abs(firstPk - 0.5f) < 0.04f && std::abs(secondPk - 0.3f) < 0.04f, "  comping: split, the first half the old take, the second the new", secondPk);
        }
    }
    {   // punch: only what falls between the punch points
        fresh();
        int li = S.addLane(LaneAudio, "PUNCH"); S.song.lanes[(size_t) li].input = 1; S.song.lanes[(size_t) li].arm = true;
        S.song.punchOn = true; S.song.punchStart = (long) (1.0 * sr); S.song.punchEnd = (long) (1.5 * sr);
        S.collectRecordings(); S.pos = 0; S.play(); S.startRecording(); inAmp = 0.3f; run(2.2); inAmp = 0; S.stopRecording(); S.stop();
        const StudioClip* c = nullptr; for (auto& x : S.song.clips) if (x.lane == li) c = &x;
        check(c != nullptr && std::abs(c->start - (long) sr) < 2 && std::abs(c->length - (long) (0.5 * sr)) < 2, "PUNCH: the take starts and ends on the punch points", c ? c->length / sr : 0.0);
    }
    {   // crossfades
        fresh();
        auto b = sine(440.0, 3.0, 48000, 0.5f, 1);
        auto p = plan(writeTestFile(dir, "xf.wav", b, 48000)); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why);
        { const juce::ScopedLock sl(S.lock); auto& a = S.song.clips[(size_t) ci]; a.length = (long) sr; a.fadeOut = 0; StudioClip c2 = a; c2.start = (long) (0.8 * sr); c2.offset = (long) (0.8 * sr); c2.length = (long) sr; c2.fadeIn = 0; S.song.clips.push_back(c2); }
        juce::AudioBuffer<float> out; render(out);
        float pk = peakOut(out, (long) (0.8 * sr), (long) sr);
        double jump = 0; for (long i = (long) (0.79 * sr); i < (long) (1.01 * sr); ++i) jump = juce::jmax(jump, (double) std::abs(out.getSample(0, (int) i) - out.getSample(0, (int) i - 1)));
        S.song.autoXfade = false; juce::AudioBuffer<float> raw; render(raw); float pkRaw = peakOut(raw, (long) (0.8 * sr), (long) sr); S.song.autoXfade = true;
        check(std::abs(pk - 0.5f) < 0.01f && pkRaw > 0.95f, "AUTO-CROSSFADE: the same sound overlapped - seamless, its own level (not doubled)", pk);
        { const juce::ScopedLock sl(S.lock); S.song.clips.back().offset = (long) (0.8 * sr) + 37; }       // a different place in the sound: equal power
        juce::AudioBuffer<float> o2; render(o2); float pk2 = peakOut(o2, (long) (0.8 * sr), (long) sr);
        check(pk2 < 0.72f && pk2 > 0.45f, "  two different sounds: an equal-power crossfade", pk2);
        { const juce::ScopedLock sl(S.lock); S.song.clips.back().offset = (long) (0.8 * sr); }
        check(jump < 0.05, "  no click anywhere across it (largest step)", jump);
    }
    {   // freeze: the same sound, from a recording of itself; unfreeze: as before
        fresh();
        auto b = makeGroove({ 100, 2, 4, 48000, 1, 0, 0, false }, 5); b.applyGain(0.5f / b.getMagnitude(0, b.getNumSamples()));
        auto p = plan(writeTestFile(dir, "fz.wav", b, 48000)); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why); int lane = S.song.clips[(size_t) ci].lane;
        S.addChain(lane, 3); S.song.lanes[(size_t) lane].vol = -2; S.song.lanes[(size_t) lane].sendA = 0.2f;
        int il = S.addLane(LaneInstrument, "SYN"); StudioClip mc; mc.lane = il; mc.source = -1; mc.start = 0; mc.length = (long) (sr * 2); mc.midiLen = mc.length; mc.notes.push_back({ 0.0, sr * 1.2, 64, 0.8f }); S.song.clips.push_back(mc); S.refresh();
        juce::AudioBuffer<float> before; render(before);
        bool f1 = S.freezeLane(lane, why), f2 = S.freezeLane(il, why);
        juce::AudioBuffer<float> after; render(after);
        int n = juce::jmin(before.getNumSamples(), after.getNumSamples()) - 400;
        double q = snr(before.getReadPointer(0) + 200, after.getReadPointer(0) + 200, n);
        check(f1 && f2 && S.song.lanes[(size_t) lane].frozen && q > 50, "FREEZE (audio with effects + a synth): it sounds the same (dB of difference)", q);
        S.unfreezeLane(lane); S.unfreezeLane(il);
        juce::AudioBuffer<float> back; render(back);
        double q2 = snr(before.getReadPointer(0) + 200, back.getReadPointer(0) + 200, n);
        check(!S.song.lanes[(size_t) lane].frozen && q2 > 50, "  UNFREEZE: its clips, synth and effects play again, the same", q2);
    }
    {   // ripple, time, copy / paste
        fresh();
        auto b = sine(330.0, 1.0, 48000, 0.4f, 1);
        auto p = plan(writeTestFile(dir, "rp.wav", b, 48000)); juce::String why;
        int c0 = S.importAudio(p, -1, 0, false, why); int lane = S.song.clips[(size_t) c0].lane;
        S.importAudio(p, lane, (long) sr, false, why); S.importAudio(p, lane, (long) (2 * sr), false, why);
        S.song.ripple = 0; S.removeClips({ 1 });
        bool stay = S.song.clips.size() == 2 && S.song.clips[1].start == (long) (2 * sr);
        S.undo(); S.song.ripple = 1; S.removeClips({ 1 });
        bool moved = S.song.clips.size() == 2 && S.song.clips[1].start == (long) sr;
        check(stay && moved, "RIPPLE: off - nothing moves; on - what is after the deleted clip moves left", moved);
        S.undo(); S.song.ripple = 0;
        S.removeTime((long) (0.5 * sr), (long) (1.5 * sr));
        long ends = 0; for (auto& c : S.song.clips) ends = juce::jmax(ends, c.end());
        check(ends == (long) (2 * sr) && S.song.clips.size() == 3, "  delete a stretch of time: the song is that much shorter, clips cut", ends / sr);
        S.insertTime((long) (0.5 * sr), (long) sr);
        ends = 0; for (auto& c : S.song.clips) ends = juce::jmax(ends, c.end());
        check(ends == (long) (3 * sr), "  insert silence: the song is longer again", ends / sr);
        S.copyClips({ 0 }); auto got = S.pasteClips((long) (5 * sr), lane);
        check(got.size() == 1 && S.song.clips.back().start == (long) (5 * sr) && S.song.clips.back().source == S.song.clips[0].source, "  copy / paste: the clip again, at the playhead", (double) got.size());
    }
    {   // MIDI: quantise, a file out and back in
        fresh();
        S.song.bpm = 120;
        int il = S.addLane(LaneInstrument, "KEYS");
        StudioClip mc; mc.lane = il; mc.source = -1; mc.start = 0; mc.midiLen = mc.length = (long) S.song.barLen(); mc.name = "riff";
        double bt = S.song.beatLen();
        mc.notes = { { bt * 0.03, bt * 0.5, 60, 0.8f }, { bt * 1.1, bt * 0.4, 64, 0.6f }, { bt * 1.96, bt * 0.9, 67, 0.7f } };
        S.song.clips.push_back(mc); int ci = (int) S.song.clips.size() - 1;
        S.quantise(ci, bt, 1.0f, false);
        bool qOk = true; for (auto& n : S.song.clips[(size_t) ci].notes) qOk &= std::abs(std::fmod(n.start, bt)) < 1e-6 || std::abs(std::fmod(n.start, bt) - bt) < 1e-6;
        check(qOk, "QUANTISE: every note on the beat", qOk);
        auto mf = dir.getChildFile("riff.mid");
        bool wr = S.exportMidi(ci, mf);
        juce::String why; int back = S.importMidi(mf, -1, (long) S.song.barLen(), why);
        bool same = back >= 0 && S.song.clips[(size_t) back].notes.size() == 3;
        if (same) for (int i = 0; i < 3; ++i) { auto& a = S.song.clips[(size_t) ci].notes[(size_t) i]; auto& b2 = S.song.clips[(size_t) back].notes[(size_t) i];
            same &= a.pitch == b2.pitch && std::abs(a.start - b2.start) < 30 && std::abs(a.len - b2.len) < 30 && std::abs(a.vel - b2.vel) < 0.02f; }
        check(wr && same, "a MIDI file: saved and opened again, the same notes", back >= 0 ? (double) S.song.clips[(size_t) back].notes.size() : 0.0);
    }
    {   // all of it saved and opened
        fresh();
        auto b = sine(330.0, 1.0, 48000, 0.4f, 1);
        auto p = plan(writeTestFile(dir, "sv.wav", b, 48000)); juce::String why;
        int ci = S.importAudio(p, -1, 0, false, why); int lane = S.song.clips[(size_t) ci].lane;
        { const juce::ScopedLock sl(S.lock); auto& c = S.song.clips[(size_t) ci]; c.takes = { { c.source, 0, "a" }, { c.source, 100, "b" } }; c.take = 0; }
        S.chooseTake(ci, 1); S.freezeLane(lane, why);
        S.song.punchOn = true; S.song.punchStart = 1000; S.song.punchEnd = 50000; S.song.ripple = 2; S.song.autoXfade = false;
        auto folder = dir.getChildFile("song23"); folder.createDirectory();
        bool sv = S.save(folder); fresh(); bool ld = S.load(folder);
        bool same = ld && S.song.clips.size() == 1 && S.song.clips[0].takes.size() == 2 && S.song.clips[0].take == 1 && S.song.lanes[(size_t) lane].frozen
                    && S.song.punchOn && S.song.punchEnd == 50000 && S.song.ripple == 2 && !S.song.autoXfade;
        check(sv && same, "save and open: takes, the take playing, freeze, punch, ripple, crossfades", same);
    }
    // ---------------------------------------------------------------- 9 routing: buses (groups), sends anywhere, sidechain
    say("== 9. routing: buses (groups / folders), sends to any bus, sidechain");
    {
        auto rms = [](const juce::AudioBuffer<float>& b, long a, long e) { double s2 = 0; long n = 0; for (long i = a; i < juce::jmin(e, (long) b.getNumSamples()); ++i) { s2 += (double) b.getSample(0, (int) i) * b.getSample(0, (int) i); ++n; } return n ? std::sqrt(s2 / n) : 0.0; };
        fresh();
        juce::String why;
        int c1 = S.importAudio(plan(writeTestFile(dir, "r1.wav", sine(300.0, 2.0, 48000, 0.3f, 1), 48000)), -1, 0, false, why);
        int c2 = S.importAudio(plan(writeTestFile(dir, "r2.wav", sine(500.0, 2.0, 48000, 0.3f, 1), 48000)), -1, 0, false, why);
        int l1 = S.song.clips[(size_t) c1].lane, l2 = S.song.clips[(size_t) c2].lane;
        juce::AudioBuffer<float> before; render(before);
        int bus = S.addBus("DRUMS", { l1, l2 }); l1 = S.song.clips[(size_t) c1].lane; l2 = S.song.clips[(size_t) c2].lane;
        juce::AudioBuffer<float> through; render(through);
        long a = (long) (0.2 * sr), e = (long) (1.8 * sr);
        double q = snr(before.getReadPointer(0) + a, through.getReadPointer(0) + a, (int) (e - a));
        check(bus >= 0 && S.song.lanes[(size_t) l1].out == bus && S.song.lanes[(size_t) l2].out == bus && q > 90, "a bus: two tracks go into it, the sound unchanged (dB of difference)", q);
        S.song.lanes[(size_t) bus].vol = -6.0206f; juce::AudioBuffer<float> half; render(half);
        check(std::abs(rms(half, a, e) / rms(before, a, e) - 0.5) < 0.01, "  its fader acts on both (-6 dB = half)", rms(half, a, e) / rms(before, a, e));
        S.song.lanes[(size_t) bus].vol = 0; S.song.lanes[(size_t) bus].mute = true; juce::AudioBuffer<float> muted; render(muted);
        check(rms(muted, a, e) < 1e-6, "  MUTE on the bus: both silent", rms(muted, a, e));
        S.song.lanes[(size_t) bus].mute = false;
        S.song.lanes[(size_t) bus].solo = true; juce::AudioBuffer<float> solo; render(solo); S.song.lanes[(size_t) bus].solo = false;
        double qs = snr(before.getReadPointer(0) + a, solo.getReadPointer(0) + a, (int) (e - a));
        check(qs > 90, "  SOLO on the bus: its tracks play", qs);
        S.song.lanes[(size_t) bus].folded = true;
        check(S.song.hidden(l1) && S.song.hidden(l2) && !S.song.hidden(bus), "  folded: its tracks tucked under it", 1);
        S.song.lanes[(size_t) bus].folded = false;
        // a bus inside a bus, never in a circle
        int bus2 = S.addBus("ALL", { bus }); l1 = S.song.clips[(size_t) c1].lane; bus = S.song.lanes[(size_t) l1].out;
        check(S.song.lanes[(size_t) bus].out == bus2 && !S.song.canRoute(bus2, bus) && S.song.canRoute(bus, -1), "  a bus in a bus; a bus can never feed itself (no circle)", bus2);
        juce::AudioBuffer<float> nested; render(nested);
        double qn = snr(before.getReadPointer(0) + a, nested.getReadPointer(0) + a, (int) (e - a));
        check(qn > 90, "  through both: still the same sound", qn);
        // a stem of the bus: its tracks
        juce::AudioBuffer<float> stem; S.renderAll(stem, nullptr, bus);
        double qst = snr(before.getReadPointer(0) + a, stem.getReadPointer(0) + a, (int) (e - a));
        check(qst > 90, "  the bus exported on its own (stem): its tracks", qst);
        // delete the outer bus: what went into it goes to the master, nothing lost
        S.deleteLane(bus2); bus = S.song.lanes[(size_t) S.song.clips[(size_t) c1].lane].out;
        juce::AudioBuffer<float> del; render(del);
        double qd = snr(before.getReadPointer(0) + a, del.getReadPointer(0) + a, (int) (e - a));
        check(bus >= 0 && S.song.lanes[(size_t) bus].out == -1 && qd > 90, "  a bus deleted: what went into it goes to the master", qd);
        // a send before the fader: heard on the bus even with the track's fader down
        fresh();
        int c3 = S.importAudio(plan(writeTestFile(dir, "r3.wav", sine(300.0, 2.0, 48000, 0.3f, 1), 48000)), -1, 0, false, why);
        int l3 = S.song.clips[(size_t) c3].lane;
        juce::AudioBuffer<float> ref; render(ref);
        int b3 = S.addBus("FX BUS", {}); l3 = S.song.clips[(size_t) c3].lane;
        S.song.lanes[(size_t) l3].vol = -60; S.song.lanes[(size_t) l3].sends.push_back({ b3, 1.0f, true });
        juce::AudioBuffer<float> pre; render(pre);
        double rp = rms(pre, a, e) / rms(ref, a, e);
        S.song.lanes[(size_t) l3].sends.back().pre = false; juce::AudioBuffer<float> post; render(post);
        double rq = rms(post, a, e) / rms(ref, a, e);
        check(std::abs(rp - 1.0) < 0.01 && rq < 0.01, "a send before the fader: the bus hears it with the fader down (after: it doesn't)", rp);
        // sidechain: a pad ducks when the kick plays
        fresh();
        int cp = S.importAudio(plan(writeTestFile(dir, "pad.wav", sine(200.0, 2.0, 48000, 0.3f, 1), 48000)), -1, 0, false, why);
        juce::AudioBuffer<float> kick(1, (int) (2 * 48000)); kick.clear(); for (int i = (int) (1.0 * 48000); i < (int) (1.4 * 48000); ++i) kick.setSample(0, i, 0.8f * (float) std::sin(2 * M_PI * 60 * i / 48000.0));
        int ck = S.importAudio(plan(writeTestFile(dir, "kick.wav", kick, 48000)), -1, 0, false, why);
        int lp = S.song.clips[(size_t) cp].lane, lk = S.song.clips[(size_t) ck].lane;
        S.song.lanes[(size_t) lk].mute = true;                   // the kick is not heard: it only drives the compressor
        auto comp = StudioDevice::make(DevComp); comp.p[0] = -30; comp.p[1] = 20; comp.p[2] = 1; comp.p[5] = 0; comp.side = lk;
        S.song.lanes[(size_t) lp].devices.push_back(comp); S.refresh();
        juce::AudioBuffer<float> sc; render(sc);
        double quiet = rms(sc, (long) (0.3 * sr), (long) (0.9 * sr)), ducked = rms(sc, (long) (1.1 * sr), (long) (1.35 * sr));
        check(quiet > 0.2 && ducked < quiet * 0.3, "SIDECHAIN: the pad ducks only while the kick plays (level while ducked / before)", ducked / quiet);
        // saved and opened
        int bsv = S.addBus("SAVE", { lp }); S.song.lanes[(size_t) bsv].folded = true;
        lp = S.song.clips[(size_t) cp].lane; S.song.lanes[(size_t) lp].sends.push_back({ bsv, 0.25f, true });
        auto folder = dir.getChildFile("song-route"); folder.createDirectory();
        int side = S.song.lanes[(size_t) lp].devices.back().side;
        bool sv = S.save(folder); fresh(); bool ld = S.load(folder);
        bool same = ld && S.song.lanes[(size_t) lp].out == bsv && S.song.lanes[(size_t) bsv].folded && S.song.lanes[(size_t) lp].sends.size() == 1 && S.song.lanes[(size_t) lp].sends[0].pre
                    && S.song.lanes[(size_t) lp].devices.back().side == side;
        check(sv && same, "  save and open: buses, folds, sends, sidechains", same);
    }
    // ---------------------------------------------------------------- 10 the station's effects on studio lanes
    say("== 10. every effect of the loop station, on a studio track");
    {
        fresh();
        auto b = makeGroove({ 110, 2, 4, 48000, 1, 0, 0, false }, 21); b.applyGain(0.5f / b.getMagnitude(0, b.getNumSamples()));
        juce::String why;
        int ci = S.importAudio(plan(writeTestFile(dir, "stfx.wav", b, 48000)), -1, 0, false, why); int lane = S.song.clips[(size_t) ci].lane;
        juce::AudioBuffer<float> dry; render(dry);
        int nOk = 0, nAll = stationFxCount(); juce::StringArray bad; double worstMs = 0;
        for (int f = 0; f < nAll; ++f) {
            { const juce::ScopedLock sl(S.lock); S.song.lanes[(size_t) lane].devices.clear(); S.song.lanes[(size_t) lane].devices.push_back(StudioDevice::makeStation(f)); }
            S.refresh();
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            juce::AudioBuffer<float> out; render(out);
            double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            worstMs = juce::jmax(worstMs, ms / (out.getNumSamples() / sr));
            bool finite = true; float pk = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) { float v = out.getSample(c, i); finite &= std::isfinite(v); pk = juce::jmax(pk, std::abs(v)); }
            int n = juce::jmin(dry.getNumSamples(), out.getNumSamples()) - 4000;
            double diff = snr(dry.getReadPointer(0) + 2000, out.getReadPointer(0) + 2000, n);
            bool heard = diff < 40 || juce::String(stationFxInfo(f).name) == "EQ";   // it changed the sound (the EQ starts flat)
            bool ok = finite && pk < 4.0f && pk > 0.05f && heard;
            if (ok) ++nOk; else bad.add(juce::String(stationFxInfo(f).name) + (finite ? "" : " NaN") + " peak " + juce::String(pk, 2) + " diff " + juce::String(diff, 1));
        }
        for (auto& x : bad) say("    %s", x.toRawUTF8());
        check(nOk == nAll && nAll >= 25, "all " + juce::String(nAll) + " station effects on a track: heard, clean, never blowing up", nOk);
        check(worstMs < 600, "  the heaviest one: ms per second of sound, its start included (real time: under 1000)", worstMs);
        // an insert at 0%: the sound as it was (the strip's own level taken out)
        int ins = -1; for (int f = 0; f < nAll; ++f) if (stationFxGroup(f) == 0 && juce::String(stationFxInfo(f).name) == "DRIVE") ins = f;
        if (ins >= 0) {
            { const juce::ScopedLock sl(S.lock); auto d = StudioDevice::makeStation(ins); d.p[0] = 0; S.song.lanes[(size_t) lane].devices = { d }; }
            S.refresh(); juce::AudioBuffer<float> out; render(out);
            double q = snr(dry.getReadPointer(0) + 4000, out.getReadPointer(0) + 4000, dry.getNumSamples() - 8000);
            check(q > 25, "  the station's DRIVE at 0%: the sound as it was (dB of difference)", q);
        }
        // saved and opened by name
        { const juce::ScopedLock sl(S.lock); S.song.lanes[(size_t) lane].devices = { StudioDevice::makeStation(nAll - 1) }; S.song.lanes[(size_t) lane].devices[0].p[1] = 33; }
        auto folder = dir.getChildFile("song-stfx"); folder.createDirectory();
        bool sv = S.save(folder); fresh(); bool ld = S.load(folder);
        auto& dv = S.song.lanes[(size_t) lane].devices;
        check(sv && ld && dv.size() == 1 && dv[0].type == DevStation && dv[0].fx == nAll - 1 && std::abs(dv[0].p[1] - 33) < 0.01f, "  save and open: the station effect and its knobs", dv.empty() ? 0.0 : (double) dv[0].fx);
    }
    // ---------------------------------------------------------------- 11 plug-ins (VST3 / AU): found, opened, heard, their window, saved
    say("== 11. plug-ins: found on the computer, on a track, an instrument, their own window, saved with the song");
    {
        fresh();
        S.syncPlugins = true;
        h.rack.scanPlugins(nullptr);
        auto fx = S.pluginList(false), inst = S.pluginList(true);
        juce::PluginDescription gain, synth; bool gOk = false, sOk = false;
        for (auto& d : fx) if (d.name.contains("Arco Test Gain")) { gain = d; gOk = true; }
        for (auto& d : inst) if (d.name.contains("Arco Test Synth")) { synth = d; sOk = true; }
        check(gOk && sOk, "found: an effect plug-in and an instrument plug-in (" + juce::String(fx.size()) + " effects, " + juce::String(inst.size()) + " instruments)", fx.size() + inst.size());
        if (gOk && sOk) {
            juce::String why;
            int ci = S.importAudio(plan(writeTestFile(dir, "pl.wav", sine(330.0, 2.0, 48000, 0.4f, 1), 48000)), -1, 0, false, why); int lane = S.song.clips[(size_t) ci].lane;
            juce::AudioBuffer<float> dry; render(dry);
            juce::String err = "?"; S.addPluginEffect(lane, gain, [&](juce::String e) { err = e; });
            auto& dv = S.song.lanes[(size_t) lane].devices;
            bool on = err.isEmpty() && !dv.empty() && dv.back().type == DevPlugin && dv.back().slot && dv.back().slot->inst;
            if (on) { auto& ps0 = dv.back().slot->inst->getParameters(); if (!ps0.isEmpty()) ps0[0]->setValueNotifyingHost(1.0f); }   // (the demo starts at half)
            juce::AudioBuffer<float> wet; render(wet);
            double q = snr(dry.getReadPointer(0) + 2000, wet.getReadPointer(0) + 2000, dry.getNumSamples() - 4000);
            check(on && q > 60, "an effect plug-in on a track: it plays (the gain plug-in at 1: the sound as it was, dB)", q);
            // its parameter changed: heard
            if (on) { auto& ps = dv.back().slot->inst->getParameters(); if (!ps.isEmpty()) ps[0]->setValueNotifyingHost(0.25f); }
            juce::AudioBuffer<float> soft; render(soft);
            float r0 = dry.getRMSLevel(0, 2000, dry.getNumSamples() - 4000), r1 = soft.getRMSLevel(0, 2000, soft.getNumSamples() - 4000);
            check(on && r1 < r0 * 0.5f && r1 > r0 * 0.1f, "  its knob turned down: the track is quieter (ratio)", r1 / juce::jmax(1e-6f, r0));
            // its own window
            if (on) dv.back().slot->show();
            bool win = on && dv.back().slot->win && dv.back().slot->win->isVisible() && dv.back().slot->win->getContentComponent() != nullptr;
            check(win, "  its own window opens (the plug-in's own face)", win);
            if (on && dv.back().slot->win) dv.back().slot->win->setVisible(false);
            // an instrument plug-in on an instrument lane, playing a MIDI clip
            int il = S.addLane(LaneInstrument, "PLUGSYN");
            juce::String e2 = "?"; S.setPluginInstrument(il, synth, [&](juce::String e) { e2 = e; });
            StudioClip mc; mc.lane = il; mc.source = -1; mc.start = 0; mc.length = (long) (sr * 1.5); mc.midiLen = mc.length; mc.notes.push_back({ 0.0, sr * 1.0, 69, 0.9f }); S.song.clips.push_back(mc);
            S.song.lanes[(size_t) lane].mute = true; S.refresh();
            juce::AudioBuffer<float> syn; render(syn);
            float pk = syn.getMagnitude(0, 2000, (int) (0.9 * sr)), after = syn.getMagnitude(0, (int) (1.6 * sr), (int) (0.3 * sr));
            double hz = pitchOf(syn.getReadPointer(0) + 4000, (int) (0.5 * sr), sr);
            check(e2.isEmpty() && S.song.lanes[(size_t) il].inst && pk > 0.02f && std::abs(hz - 440) < 6, "an instrument plug-in plays the lane's MIDI clip (A = Hz)", hz);
            check(after < pk * 0.5f, "  the note ends where the clip says", after);
            S.song.lanes[(size_t) lane].mute = false;
            // saved with the song, opened again with its setting
            auto folder = dir.getChildFile("song-plug"); folder.createDirectory();
            bool sv = S.save(folder);
            fresh(); bool ld = S.load(folder);
            auto& dv2 = S.song.lanes[(size_t) lane].devices;
            bool back = ld && !dv2.empty() && dv2.back().type == DevPlugin && dv2.back().slot && S.song.lanes[(size_t) il].inst;
            float pv = back && !dv2.back().slot->inst->getParameters().isEmpty() ? dv2.back().slot->inst->getParameters()[0]->getValue() : -1.0f;
            check(sv && back && std::abs(pv - 0.25f) < 0.01f, "  save and open: both plug-ins back, with their settings (gain knob)", pv);
        }
        S.syncPlugins = false;
    }
    // ---------------------------------------------------------------- 12 the modular's modules, every one
    say("== 12. MODULAR: every module makes its sound (or its voltage), clean, never blowing up");
    {
        int okN = 0; juce::StringArray bad;
        for (int t = 0; t < MTypes; ++t) {
            if (t == MOut || t == MKeys) continue;
            Modular mo; mo.prepare(sr);
            ModPatch p; int nid = 1;
            auto addM = [&](int type) { ModModule m; m.type = type; m.id = nid++; auto& D = modDefs()[(size_t) type]; for (size_t i = 0; i < D.params.size(); ++i) m.p[i] = D.params[i].def; p.mods.push_back(m); return m.id; };
            int keys = addM(MKeys), clk = addM(MClock), vco = addM(MVco), mid = addM(t), out = addM(MOut);
            auto& D = modDefs()[(size_t) t];
            for (int k = 0; k < (int) D.ins.size(); ++k) {
                juce::String nm = D.ins[(size_t) k];
                if (nm.contains("GATE") || nm.contains("TRIG") || nm.contains("CLOCK")) p.cables.push_back({ clk, 1, mid, k, 0 });
                else if (nm.contains("RESET")) continue;
                else if (nm.contains("PITCH")) p.cables.push_back({ keys, 0, mid, k, 0 });
                else p.cables.push_back({ vco, 2, mid, k, 0 });
            }
            if (!D.outs.empty()) { p.cables.push_back({ mid, 0, out, 0, 0 }); if (D.outs.size() > 1 && (juce::String(D.outs[1]) == "R")) p.cables.push_back({ mid, 1, out, 1, 0 }); }
            p.nextId = nid;
            mo.setPatch(p);
            std::vector<float> tl(sr * 2 / 512 * 512 + 512, 0.0f), tr2(tl.size(), 0.0f), thru(512, 0.0f);
            for (int i = 0; i < 512; ++i) thru[(size_t) i] = 0.5f * (float) std::sin(i * 0.05);
            mo.thruL = thru.data(); mo.thruR = thru.data();
            mo.noteOn(60, 0.8f); mo.noteOn(64, 0.8f);
            double sum = 0; bool finite = true; float pk = 0;
            for (size_t b = 0; b + 512 <= tl.size(); b += 512) {
                mo.process(tl.data() + b, tr2.data() + b, 512, 120.0, (double) b / sr * 2.0);
                for (int i = 0; i < 512; ++i) { float v = tl[b + i]; finite &= std::isfinite(v); pk = juce::jmax(pk, std::abs(v)); sum += (double) v * v; }
            }
            double rms = std::sqrt(sum / tl.size());
            int cat = modCategory(t);
            bool sounds = cat == CatSound || cat == CatDrum || cat == CatFilter || cat == CatFx || t == MDrive || t == MCrush || t == MComp || t == MVca || t == MIn;
            bool ok = finite && pk <= 1.5f && (!sounds || rms > 1e-4 || t == MVca);
            if (ok) ++okN; else bad.add(juce::String(D.name) + (finite ? "" : " NaN") + " peak " + juce::String(pk, 3) + " rms " + juce::String(rms, 5));
        }
        for (auto& b : bad) say("    %s", b.toRawUTF8());
        check(bad.isEmpty() && okN >= 55, juce::String(okN) + " modules: each one heard / moving, finite, within the limits", okN);
    }
    {   // a patch as an effect in STUDIO: IN (THROUGH) -> VCF -> OUT on a lane, the sound goes through it (and is saved)
        ModPatch p; int nid = 1;
        auto addM = [&](int type) { ModModule m; m.type = type; m.id = nid++; auto& D = modDefs()[(size_t) type]; for (size_t i = 0; i < D.params.size(); ++i) m.p[i] = D.params[i].def; p.mods.push_back(m); return m.id; };
        int in = addM(MIn), out = addM(MOut); p.cables.push_back({ in, 0, out, 0, 0 }); p.cables.push_back({ in, 1, out, 1, 0 }); p.nextId = nid;
        StudioDevice d; d.type = DevPatch; d.patchJson = juce::JSON::toString(Modular::toVar(p), true); d.plugName = "Patch";
        DeviceDsp dsp; dsp.prepareFor(d, sr);
        std::vector<float> L(512), R(512); double si = 0, so = 0;
        for (int b = 0; b < 40; ++b) { for (int i = 0; i < 512; ++i) { L[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin((b * 512 + i) * 0.03); si += L[(size_t) i] * L[(size_t) i]; }
            dsp.process(d, L.data(), R.data(), 512, 120.0); for (float v : L) so += v * v; }
        check(so > si * 0.05 && std::isfinite(so), "a modular patch as a STUDIO effect: the lane's sound goes through it", std::sqrt(so / si));
        auto d2 = studioDevFromVar(studioDevToVar(d));
        check(d2.type == DevPatch && d2.patchJson == d.patchJson, "  ... and it is saved with the song");
    }
    say("studio check: %d of %d failed", fails, total);
    fresh();
    dir.deleteRecursively();
    return fails;
}

} // namespace arco
