#pragma once
// ARCO --probe: the live inputs and the clocks, through a pretend sound card (the real callback, the real routing).
// Prints what reaches the strips and the outputs - for finding why a LIVE input is silent.
#include "AudioHost.h"
#include "SelfTest.h"
#include <cstdio>

namespace arco {

inline int runProbe(AudioHost& h) {
    int fails = 0;
    auto say = [](const char* f, auto... a) { std::printf(f, a...); std::printf("\n"); std::fflush(stdout); };
    auto check = [&](bool ok, const juce::String& what, double v = 0) {
        if (!ok) ++fails; say("  %s  %-64s %.4f", ok ? "ok  " : "FAIL", what.toRawUTF8(), v); };
    h.devices.addAudioDeviceType(std::make_unique<FakeType>());
    h.devices.setCurrentAudioDeviceType("ARCO test", true);
    h.nLive = 2; h.inputCh[0] = 1; h.inputCh[1] = 2;
    auto msg = h.openAudio("Test Speakers", "Test Mic", 48000, 512);
    auto* dev = dynamic_cast<FakeDevice*>(h.devices.getCurrentAudioDevice());
    say("device: %s  %s  ins %s outs %s", dev ? dev->getName().toRawUTF8() : "none", msg.toRawUTF8(),
        dev ? dev->getActiveInputChannels().toString(2).toRawUTF8() : "-", dev ? dev->getActiveOutputChannels().toString(2).toRawUTF8() : "-");
    say("routing: live1 -> ch %d, live2 -> ch %d, phones at %d", h.engine.inputChannel[0].load(), h.engine.inputChannel[1].load(), h.engine.phonesFirst.load());
    const int N = 512, NI = 2, NO = 8;
    std::vector<float> ib[NI], ob[NO];
    for (auto& v : ib) v.assign(N, 0); for (auto& v : ob) v.assign(N, 0);
    const float* ins[NI]; float* outs[NO];
    for (int i = 0; i < NI; ++i) ins[i] = ib[i].data(); for (int i = 0; i < NO; ++i) outs[i] = ob[i].data();
    double ph[NI] = {};
    juce::AudioIODeviceCallbackContext ctx;
    auto run = [&](double sec, float a1, float a2, float& o12, float& phones) {
        o12 = 0; phones = 0; int blocks = (int) (sec * 48000 / N);
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < N; ++i) { ib[0][(size_t) i] = a1 * (float) std::sin(ph[0]); ph[0] += 2 * M_PI * 220 / 48000.0;
                                          ib[1][(size_t) i] = a2 * (float) std::sin(ph[1]); ph[1] += 2 * M_PI * 330 / 48000.0; }
            h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx);
            h.model.tick();
            for (int i = 0; i < N; ++i) { o12 = juce::jmax(o12, std::abs(ob[0][(size_t) i]), std::abs(ob[1][(size_t) i]));
                                          phones = juce::jmax(phones, std::abs(ob[2][(size_t) i]), std::abs(ob[3][(size_t) i])); }
        }
    };
    float o = 0, p = 0;
    run(0.5, 0, 0, o, p);
    h.engine.peak("ch0"); h.engine.peak("ch1");
    run(1.0, 0.3f, 0, o, p);
    float c0 = h.engine.peak("ch0"), in0 = h.engine.peak("live_in1");
    check(in0 > 0.1f, "LIVE 1: the input arrives", in0);
    check(c0 > 0.05f, "LIVE 1: its strip sounds", c0);
    check(o > 0.02f, "LIVE 1: heard on outputs 1-2", o);
    run(1.0, 0, 0.3f, o, p);
    float c1 = h.engine.peak("ch1"), in1 = h.engine.peak("live_in2");
    check(in1 > 0.1f, "LIVE 2: the input arrives", in1);
    check(c1 > 0.05f, "LIVE 2: its strip sounds", c1);
    // a note on the SYNTH must not take LIVE 2 away while LIVE 2 is a used input
    h.synth.noteOn(60, 0.8f); run(0.3, 0, 0, o, p); h.synth.noteOff(60); run(1.0, 0, 0, o, p);
    h.engine.peak("live_in2"); h.engine.peak("ch1");
    run(1.0, 0, 0.3f, o, p);
    float in1b = h.engine.peak("live_in2"), c1b = h.engine.peak("ch1");
    say("  (the synth plays into LIVE %d)", h.synthInto + 1);
    check(c1b > 0.05f && in1b > 0.1f, "LIVE 2 still heard after a SYNTH note", c1b);
    // studio: an armed lane on LIVE 2 hears its input
    {
        auto& S = h.studio; S.song = StudioSong(); S.song.sr = 48000; S.song.countIn = false; S.ensureReturns();
        int li = S.addLane(LaneAudio, "MIC"); S.song.lanes[(size_t) li].input = 1; S.song.lanes[(size_t) li].arm = true; S.song.lanes[(size_t) li].monitor = true;
        S.collectRecordings();
        S.play(); S.startRecording();
        for (int k = 0; k < 60; ++k) { run(0.05, 0, 0.3f, o, p); S.collectRecordings(); }
        S.stopRecording(); S.stop();
        long len = 0; float pk = 0;
        for (auto& c : S.song.clips) if (c.lane == li && c.source >= 0) { auto& src = *S.song.sources[(size_t) c.source]; len = src.size(); for (float x : src.l) pk = juce::jmax(pk, std::abs(x)); }
        say("  studio take: %ld samples", len);
        check(len > 48000 && pk > 0.1f, "STUDIO: a lane recording LIVE 2 gets the sound", pk);
        // the station's mix into a lane (stereo, the real mix - not the speaker outputs)
        int mi = S.addLane(LaneAudio, "MIX"); S.song.lanes[(size_t) mi].input = -1; S.song.lanes[(size_t) mi].arm = true;
        for (auto& L : S.song.lanes) if (&L != &S.song.lanes[(size_t) mi]) L.arm = false;
        S.pos = 0; S.play(); S.startRecording();
        for (int k = 0; k < 60; ++k) { run(0.05, 0.3f, 0, o, p); S.collectRecordings(); }
        S.stopRecording(); S.stop();
        float mpk = 0; for (auto& c : S.song.clips) if (c.lane == mi && c.source >= 0) for (float x : S.song.sources[(size_t) c.source]->l) mpk = juce::jmax(mpk, std::abs(x));
        check(mpk > 0.05f, "STUDIO: recording the station's mix", mpk);
        // and it plays back on outputs 1-2
        S.pos = 0; S.play(); run(1.0, 0, 0, o, p); S.stop();
        check(o > 0.05f, "STUDIO: the takes play back on outputs 1-2", o);
        juce::AudioBuffer<float> ex; bool exok = S.renderAll(ex, nullptr);
        check(exok && ex.getNumSamples() > 48000 && ex.getMagnitude(0, ex.getNumSamples()) > 0.05f, "STUDIO: export renders the song", ex.getNumSamples());
    }
    // studio: a station loop dragged in keeps its exact length (its speed): 2 bars at 120 bpm stay 96000 samples,
    // and the song takes the loop's tempo even after a lane was recorded first
    {
        auto& S = h.studio; auto& e = h.engine;
        auto& b = e.buffer("st_loopsA"); bool fits = b.len >= 96000 && b.ch >= 1;
        if (fits) {
            for (long i = 0; i < 96000; ++i) b.chan(0)[i] = 0.3f * (float) std::sin(i * 0.05);
            float l0 = e.snap("sv_len0").v.load(), m0 = e.snap("sv_mask0").v.load(), bl0 = e.snap("sv_bl").v.load();
            e.snap("sv_len0").v.store(96000.0f); e.snap("sv_mask0").v.store(1.0f); e.snap("sv_bl").v.store(24000.0f);
            S.song.sr = 44100;          // a song left at another rate (the iPad's sound card can be 44.1 or 48 kHz)
            S.song.bpm = 97; juce::String why; bool ok = S.addStationLoop(1, 0, 0, -1, why);
            check(std::abs(S.song.sr - e.sr) < 0.5, "STUDIO: the song is at the sound card's rate when a station loop comes in (no slow-down)", (float) S.song.sr);
            e.snap("sv_len0").v.store(l0); e.snap("sv_mask0").v.store(m0); e.snap("sv_bl").v.store(bl0);
            long cl = 0, sl = 0; double diff = 1;
            if (ok && !S.song.clips.empty()) { auto& c = S.song.clips.back(); cl = c.length; auto& src = *S.song.sources[(size_t) c.source]; sl = (long) src.size();
                diff = 0; for (long i = 0; i < sl; i += 997) diff = juce::jmax(diff, (double) std::abs(src.l[(size_t) i] - b.chan(0)[i])); }
            say("  loop in the studio: clip %ld, source %ld samples, song %.1f bpm", cl, sl, S.song.bpm);
            check(ok && cl == 96000 && sl == 96000 && diff < 1e-6, "STUDIO: a dragged loop keeps its exact length and samples (speed unchanged)", (float) cl);
            double blLive = e.snap("sv_bl").v.load();   // the engine rewrites the station's clock every block
            bool tempoOk = std::abs(S.song.bpm - 120.0) < 0.01 || (blLive > 16 && std::abs(S.song.bpm - 60.0 * e.sr / blLive) < 0.01) || (blLive <= 16 && S.song.bpm == 97);
            check(tempoOk, "STUDIO: the song takes the station's tempo", (float) S.song.bpm);
            // recording into a NEW lane: add one, arm it, record
            int ni = S.addLane(LaneAudio, "NEW"); for (auto& L : S.song.lanes) L.arm = false;
            S.song.lanes[(size_t) ni].input = 1; S.song.lanes[(size_t) ni].arm = true;
            S.pos = 0; S.play(); S.startRecording();
            for (int k = 0; k < 30; ++k) { run(0.05, 0, 0.3f, o, p); S.collectRecordings(); }
            S.stopRecording(); S.stop();
            float npk = 0; for (auto& c : S.song.clips) if (c.lane == ni && c.source >= 0) for (float x : S.song.sources[(size_t) c.source]->l) npk = juce::jmax(npk, std::abs(x));
            check(npk > 0.1f, "STUDIO: recording into a new track", npk);
            // and the loop still has its length after recording beside it
            long cl2 = 0; for (auto& c : S.song.clips) if (c.lane != ni && c.loop) cl2 = c.length;
            check(cl2 == 96000, "STUDIO: the loop is untouched by the new take", (float) cl2);
        } else say("  (no station loop buffer to test with)");
    }
    // the clocks: the station's tempo while stopped and while playing
    h.model.set("bpm", 100); run(0.5, 0, 0, o, p);
    say("  stopped: transport %.0f  ebpm %.2f  beat %.3f", h.engine.snap("transport").v.load(), h.engine.snap("ebpm").v.load(), h.engine.snap("beat").v.load());
    h.model.press("st_looper", "playall"); run(1.2, 0, 0, o, p);
    say("  playing: transport %.0f  ebpm %.2f  beat %.3f", h.engine.snap("transport").v.load(), h.engine.snap("ebpm").v.load(), h.engine.snap("beat").v.load());
    h.model.press("st_looper", "stopall"); run(0.5, 0, 0, o, p);
    // ---- the DRUM MACHINE: four kicks on the beats, by its own PLAY, in time with the clock
    {
        for (int k = 0; k < 200 && h.engine.drumMachine.bank.load() == nullptr; ++k) juce::Thread::sleep(20);
        auto* bk = h.engine.drumMachine.bank.load();
        say("  kits: %d", bk ? (int) bk->kits.size() : 0);
        if (bk) for (auto& k : bk->kits) { int full = 0; size_t mx = 0; for (auto& pd : k->pads) { full += !pd.s[1][0].l.empty(); mx = std::max(mx, pd.s[1][0].l.size()); }
                                           say("    %-6s %d pads, longest %.2f s", k->name.c_str(), full, mx / 48000.0); }
        auto& D = h.engine.drumMachine;
        for (int st : { 0, 4, 8, 12 }) D.setStep(0, 0, st, { 110, 100, 1, 0 });
        // (kicks only: the onsets are easy to find)
        h.model.set("dm_kit", 0); h.model.set("bpm", 120); h.model.set("dm_run", 1);
        std::vector<float> rec; float pk = 0;
        for (int b = 0; b < (int) (2.0 * 48000 / N); ++b) {
            for (auto& v : ib) std::fill(v.begin(), v.end(), 0.0f);
            h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick();
            for (int i = 0; i < N; ++i) { rec.push_back(ob[0][(size_t) i]); pk = juce::jmax(pk, std::abs(ob[0][(size_t) i])); }
        }
        check(pk > 0.05f, "DRUM MACHINE: plays (JAZZ kit)", pk);
        // the kicks: onsets of the low end, every half second at 120 BPM
        std::vector<int> on; float lp = 0, prevE = 0; int hold = 0;
        for (size_t i = 0; i < rec.size(); ++i) { lp += 0.01f * (rec[i] - lp); float e = std::abs(lp); if (hold > 0) --hold; else if (e > 0.02f && prevE <= 0.02f) { on.push_back((int) i); hold = 19200; } prevE = e; }
        juce::String gaps; for (size_t i = 1; i < on.size(); ++i) gaps << juce::String((on[i] - on[i - 1]) / 48.0, 1) << " ";
        say("  kick gaps (ms): %s", gaps.toRawUTF8());
        bool steady = on.size() >= 3; for (size_t i = 1; i < on.size(); ++i) steady &= std::abs((on[i] - on[i - 1]) - 24000) < 96;
        check(steady, "DRUM MACHINE: the kicks on the beat (500 ms apart, +-2 ms)", (double) on.size());
        for (int kit = 1; kit <= 4; ++kit) { h.model.set("dm_kit", kit); float kp = 0;
            for (int b = 0; b < (int) (1.0 * 48000 / N); ++b) { h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick(); for (int i = 0; i < N; ++i) kp = juce::jmax(kp, std::abs(ob[0][(size_t) i])); }
            check(kp > 0.05f && kp < 2.0f, "  kit " + DrumKits::kitNames()[kit] + " plays, clean", kp); }
        h.model.set("dm_run", 0); h.model.set("dm_kit", 0); run(0.5, 0, 0, o, p);
    }
    // ---- PHRASE: four notes into LIVE 1, recorded from the downbeat, cut into four, played back in tempo
    {
        h.model.set("beats", 4); h.model.set("phr_len", 1); h.model.set("phr_src", 1);
        h.model.press("st_phr", "phr_rec");
        const double F[4] = { 220, 277.2, 329.6, 440 };
        double t0 = -1; bool started = false; double phs = 0;
        for (int b = 0; b < (int) (5.0 * 48000 / N); ++b) {
            int stt = h.engine.phrase.stateOut.load();
            if (stt == 2 && !started) { started = true; t0 = 0; }
            for (int i = 0; i < N; ++i) {
                float x = 0;
                if (started) { double tt = t0; int note = (int) (tt / 0.5); double inNote = tt - note * 0.5;
                    if (note < 4) { phs += 2 * M_PI * F[note] / 48000.0; x = 0.4f * (float) std::sin(phs) * (float) std::exp(-inNote * 5.0) * (float) std::min(1.0, inNote / 0.003); }
                    t0 += 1.0 / 48000.0; }
                ib[0][(size_t) i] = x; ib[1][(size_t) i] = 0;
            }
            h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick();
        }
        int sl = h.engine.phrase.slicesOut.load();
        say("  phrase: state %d, %d notes found", h.engine.phrase.stateOut.load(), sl);
        check(sl == 4, "PHRASE: the four notes found", sl);
        float pp = 0; for (auto& v : ib) std::fill(v.begin(), v.end(), 0.0f);
        h.engine.peak("mainL");
        for (int b = 0; b < (int) (1.0 * 48000 / N); ++b) { h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick(); }
        pp = h.engine.peak("mainL");
        check(pp > 0.02f && h.engine.phrase.isPlaying.load(), "PHRASE: plays it back in tempo", pp);
        h.model.set("phr_rate", 5); h.model.set("phr_order", 2); h.model.set("phr_pitch", 7); h.model.press("st_phr", "phr_bgo");
        bool bad = false; float mx = 0;
        for (int b = 0; b < (int) (3.0 * 48000 / N); ++b) { h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick();
            for (int i = 0; i < N; ++i) { float x = ob[0][(size_t) i]; bad |= !std::isfinite(x); mx = juce::jmax(mx, std::abs(x)); } }
        check(!bad && mx < 1.5f, "PHRASE: 1/32, ping-pong, +7, BUILD - clean", mx);
        h.model.press("st_phr", "phr_play");
    }
    // ---- LIVE LOOPS: a bar recorded from LIVE 1 into a cell, on the bar line, then it loops
    {
        auto& G = h.engine.loopGrid; h.model.set("bpm", 120); h.model.set("beats", 4);
        G.src[0].store(0); G.recBars.store(1); G.record(0, 0);
        for (int b = 0; b < (int) (5.0 * 48000 / N) && G.recState.load() != 3; ++b) {
            for (int i = 0; i < N; ++i) { ib[0][(size_t) i] = 0.3f * (float) std::sin(ph[0]); ph[0] += 2 * M_PI * 220 / 48000.0; ib[1][(size_t) i] = 0; }
            h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick(); }
        say("  loop take: state %d, %ld samples (%.2f beats)", G.recState.load(), G.recClip.len, G.recClip.beats);
        check(G.recState.load() == 3 && std::abs(G.recClip.len - 96000) < 600, "LIVE LOOPS: one bar recorded, from the bar line", (double) G.recClip.len);
        float lp = 0; for (auto& v : ib) std::fill(v.begin(), v.end(), 0.0f);
        for (int b = 0; b < (int) (1.0 * 48000 / N); ++b) { h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick(); for (int i = 0; i < N; ++i) lp = juce::jmax(lp, std::abs(ob[0][(size_t) i])); }
        check(G.playing[0].load() == 0 && lp > 0.05f, "LIVE LOOPS: it plays at once, looping", lp);
        G.stopAll(); G.recState.store(0);
    }
    // ---- SYNTH: LFO 1 dropped on the cutoff moves it (the knob on the screen follows the same value)
    {
        auto* syn = h.engine.synthNow.load();
        h.model.set("l_rate", 55); h.model.set("l_sync", 0); h.model.set("l_shape", 0);
        h.model.set("m0_src", 1); h.model.set("m0_dst", 16); h.model.set("m0_amt", 60);
        float lo = 9, hi = -9;
        for (int b = 0; b < (int) (2.0 * 48000 / N); ++b) { h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick();
            if (syn) { float v = syn->lfoNow[0].load(); lo = juce::jmin(lo, v); hi = juce::jmax(hi, v); } }
        check(syn != nullptr && hi - lo > 1.2f, "SYNTH: LFO 1 swings (the knobs it is on move with it)", hi - lo);
        h.model.set("m0_src", 0); h.model.set("m0_dst", 0); h.model.set("m0_amt", 0);
    }
    // ---- 2.2: the DRUM MACHINE recorded on a track (through a LIVE strip), then LIVE LOOPS records that track and the SYNTH
    {
        auto blocks = [&](double sec, std::function<void()> each = nullptr) { for (int b = 0; b < (int) (sec * 48000 / N); ++b) {
            for (auto& v : ib) std::fill(v.begin(), v.end(), 0.0f);
            if (each) each();
            h.audioDeviceIOCallbackWithContext(ins, NI, outs, NO, N, ctx); h.model.tick(); } };
        h.model.press("st_looper", "stopall"); h.model.set("dm_run", 0); blocks(0.3);
        h.model.press("st_looper", "rstall"); blocks(0.3);
        h.model.set("countin", 0); h.model.set("bars", 1); h.model.set("bpm", 120); h.model.set("beats", 4);
        auto& D = h.engine.drumMachine;
        for (int st = 0; st < 16; ++st) D.setStep(0, 0, st, { 0, 0, 0, 0 });
        for (int st : { 0, 4, 8, 12 }) D.setStep(0, 0, st, { 120, 100, 1, 0 });
        int before = h.trackState(3);
        h.recordDrumsOn(3);
        int stt = 0; for (int k = 0; k < 120 && (stt = h.trackState(3)) != 3 && stt != 9; ++k) blocks(0.05);
        blocks(0.3);
        stt = h.trackState(3);
        auto& lb = h.engine.buffer("st_loopsA"); long len = (long) h.engine.snap("sv_len0").v.load();
        float pk = 0; if (len > 0 && lb.len >= len) for (long i = 0; i < len; ++i) pk = juce::jmax(pk, std::abs(lb.chan(2)[i]));
        say("  drums on track 3: state %d -> %d, %.2f s, peak %.3f, through LIVE %d", before, stt, len / 48000.0, pk, h.drumsInto + 1);
        check((stt == 3 || stt == 9) && pk > 0.1f && std::abs(len - 96000) < 600, "DRUM MACHINE: recorded on track 3 (one bar, its kicks)", pk);
        // the direct drums go quiet while they play through the strip (not heard twice)
        check(h.engine.dmDirect < 0.05f, "  ... heard through its strip only (not twice)", h.engine.dmDirect);
        // LIVE LOOPS: a cell from TRACK 3
        auto& G = h.engine.loopGrid; G.stopAll(); G.recState.store(0);
        if ((int) h.engine.snap("transport").v.load() != 1) h.model.press("st_looper", "playall");
        h.model.set("dm_run", 0); h.setDrumsInto(0); blocks(0.3);
        G.src[1].store(LoopGrid::SrcTrack1 + 2); G.recBars.store(1); G.record(1, 0);
        for (int k = 0; k < 120 && G.recState.load() != 3; ++k) blocks(0.05);
        float cp = 0; for (long i = 0; i < G.recClip.len; ++i) cp = juce::jmax(cp, std::abs(G.recClip.l[(size_t) i]));
        check(G.recState.load() == 3 && cp > 0.05f, "LIVE LOOPS: a cell recorded from TRACK 3 (what the track plays)", cp);
        G.stopAll(); G.recState.store(0);
        // LIVE LOOPS: a cell from the SYNTH, played while it records
        G.src[2].store(LoopGrid::SrcSynth); G.recBars.store(1); G.record(2, 0);
        bool on = false;
        for (int k = 0; k < 120 && G.recState.load() != 3; ++k) blocks(0.05, [&] { if (!on && G.recState.load() >= 1) { h.synth.noteOn(57, 0.9f); on = true; } });
        h.synth.noteOff(57);
        float sp2 = 0; for (long i = 0; i < G.recClip.len; ++i) sp2 = juce::jmax(sp2, std::abs(G.recClip.l[(size_t) i]));
        check(G.recState.load() == 3 && sp2 > 0.02f, "LIVE LOOPS: a cell recorded from the SYNTH", sp2);
        G.stopAll(); G.recState.store(0);
        h.model.press("st_looper", "stopall"); blocks(0.2);
        h.model.set("bars", 0);
    }
    say("probe: %d failed", fails);
    return fails;
}

} // namespace arco
