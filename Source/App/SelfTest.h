#pragma once
// ARCO --selftest: plays a test tone into the real engine and checks recording, looping, scenes,
// scene memory, saving / opening a song, importing a file and the show recorder. Prints a report, returns failures.
#include "AudioHost.h"
#include <cstdio>
#include <cmath>

namespace arco {

inline int runSelfTest(AudioHost& h, const juce::File& dir) {
    auto savedSetup = h.model.setupState();     // put this computer's settings back afterwards
    int savedLayout = h.speakerLayout, savedSynth = h.synthInto;
    h.model.set("spk_n", 2); h.speakerLayout = 0; h.applySpeakers();
    h.engine.inputChannel[0].store(0);          // the test tone is "interface input 1"
    for (int j = 1; j < 4; ++j) h.engine.inputChannel[j].store(-1);
    int fails = 0, n = 0;
    auto check = [&](bool ok, const char* what, double v = 0) {
        ++n; if (!ok) ++fails;
        std::printf("  %s  %-58s %s\n", ok ? "ok  " : "FAIL", what, v != 0 ? juce::String(v, 3).toRawUTF8() : "");
        std::fflush(stdout);
    };
    Engine& e = h.engine; Model& m = h.model;
    const int N = 512; std::vector<float> in1(N), o[34]; for (auto& v : o) v.assign(N, 0);
    float* outs[34]; for (int i = 0; i < 34; ++i) outs[i] = o[i].data();
    const float* ins[4] = { in1.data(), nullptr, nullptr, nullptr };
    double ph = 0; bool nan = false;
    auto run = [&](double sec, float amp) {
        float pk = 0; int blocks = (int) (sec * e.sr / N);
        for (int b = 0; b < blocks; ++b) {
            for (int i = 0; i < N; ++i) { in1[i] = amp * (float) std::sin(ph); ph += 2 * M_PI * 220.0 / e.sr; }
            e.process(ins, 4, outs, 34, N);
            for (int i = 0; i < N; ++i) { float x = std::max(std::abs(o[0][i]), std::abs(o[1][i])); if (std::isnan(x)) nan = true; pk = std::max(pk, std::abs(x)); }
            m.tick();
        }
        return pk;
    };
    auto trk = [&](int t) { return ((int) e.snap(t < 4 ? "statA" : "statB").v.load() >> (4 * (t % 4))) & 15; };
    auto scene = [&] { return ((int) e.snap("scenes").v.load()) % 8; };
    auto waitFor = [&](std::function<bool()> cond, double maxSec, float amp = 0) {
        for (double t = 0; t < maxSec; t += 0.05) { if (cond()) return true; run(0.05, amp); }
        return cond();
    };

    std::printf("ARCO self-test (%d Hz)\n", (int) e.sr);
    run(0.5, 0);
    check(scene() == 1, "starts on scene A");
    // 1 record a loop on track 1
    m.press("st_looper", "rec1");
    run(0.2, 0.3f);
    check(trk(0) == 6 || trk(0) == 7, "REC: count-in, then track 1 records", trk(0));
    check(waitFor([&] { return trk(0) == 7; }, 4.0, 0.3f), "  recording the first loop");
    run(3.0, 0.3f);
    m.press("st_looper", "rec1");
    check(waitFor([&] { return trk(0) == 3; }, 4.0, 0.3f), "REC again: track 1 plays the loop", trk(0));
    check(e.snap("loopbars").v.load() >= 1, "  loop length in bars", e.snap("loopbars").v.load());
    float pk = run(2.0, 0.0f);
    check(pk > 0.01f, "the loop sounds with no input (main out peak)", pk);
    // 2 stop / play
    m.press("st_looper", "stopall"); run(0.6, 0);
    float quiet = run(0.5, 0);
    check(quiet < 0.02f, "STOP silences the loops", quiet);
    m.press("st_looper", "playall");
    { float best = 0; for (int i = 0; i < 40 && best < 0.01f; ++i) best = run(0.1, 0);
      check(best > 0.01f, "PLAY brings them back", e.snap("transport").v.load() + 10 * trk(0)); }
    // 3 scenes and scene memory
    m.set("vol1", 71);
    m.press("st_looper", "scene", 8, 2);
    check(waitFor([&] { return scene() == 2; }, 4.0), "scene B selected (on the bar line)", scene());
    run(0.2, 0);
    check(trk(0) == 0, "scene B has its own (empty) tracks", trk(0));
    m.set("vol1", 20); m.set("dly0", 55);
    m.press("st_looper", "scene", 8, 1);
    check(waitFor([&] { return scene() == 1; }, 4.0), "back to scene A");
    run(0.2, 0);
    check(std::abs(m.get("vol1") - 71) < 0.01 && std::abs(m.get("dly0") - 0) < 0.01, "scene A remembers its fader and sends", m.get("vol1"));
    check(trk(0) == 3, "scene A's loop is still there", trk(0));
    m.press("st_looper", "scene", 8, 2); waitFor([&] { return scene() == 2; }, 4.0); run(0.2, 0);
    check(std::abs(m.get("vol1") - 20) < 0.01 && std::abs(m.get("dly0") - 55) < 0.01, "scene B remembers its own", m.get("vol1"));
    m.press("st_looper", "scene", 8, 1); waitFor([&] { return scene() == 1; }, 4.0); run(0.2, 0);
    // 4 tracks per scene
    m.press("st_looper", "ntr", 16, 6); run(0.3, 0);
    check((int) e.snap("ntn").v.load() == 6, "6 tracks on this scene", e.snap("ntn").v.load());
    // 5 save, reset, open
    auto song = dir.getChildFile("selftest song");
    song.deleteRecursively();
    check(h.saveSong(song), "SAVE writes the song folder");
    check(song.getChildFile("selftest song-settings.json").existsAsFile() && song.getChildFile("selftest song-A.wav").existsAsFile(),
          "  settings + scene A audio are in it");
    m.press("st_looper", "rstall"); run(0.5, 0);
    check(trk(0) == 0, "RESET empties the station", trk(0));
    m.set("vol1", 50);
    check(h.openSong(song), "OPEN reads the song back");
    check(waitFor([&] { return trk(0) == 3; }, 4.0), "  track 1 plays again", trk(0));
    check(std::abs(m.get("vol1") - 71) < 0.01, "  with its settings", m.get("vol1"));
    check(e.snap("transport").v.load() < 0.5f, "  opens stopped, ready to play");
    m.press("st_looper", "playall");
    { float best = 0; for (int i = 0; i < 40 && best < 0.01f; ++i) best = run(0.1, 0);
      check(best > 0.01f, "  PLAY: it sounds", e.snap("transport").v.load() + 10 * trk(0) + 100 * e.snap("sv_len0").v.load()); }
    // 6 import a file into track 2
    {
        auto wav = dir.getChildFile("selftest tone.wav");
        juce::AudioBuffer<float> b(2, (int) e.sr * 2);
        for (int i = 0; i < b.getNumSamples(); ++i) { float x = 0.4f * (float) std::sin(2 * M_PI * 330.0 * i / e.sr); b.setSample(0, i, x); b.setSample(1, i, x); }
        wav.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(new juce::FileOutputStream(wav), e.sr, 2, 24, {}, 0));
        if (w) w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        w.reset();
        check(h.importFile(wav, 2), "IMPORT reads a WAV file");
        run(0.1, 0);
        m.press("st_looper", "imp_go");           // (the interface does this 60 ms later)
        waitFor([&] { return trk(1) == 3; }, 6.0);
        check(trk(1) == 3, "  track 2 plays the file", trk(1));
    }
    // 7 the show recorder
    {
        auto rec = dir.getChildFile("selftest show.wav"); rec.deleteFile();
        check(h.startRecording(rec), "REC SHOW starts a file");
        run(2.0, 0);
        h.stopRecording();
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(rec));
        double secs = r ? r->lengthInSamples / r->sampleRate : 0;
        check(r != nullptr && secs > 1.5, "  the show file has the sound (seconds)", secs);
    }
    // 8 every effect at once, the synth, 8 speakers: no NaN, no runaway
    for (auto k : { "halo0", "shd0", "dly0", "spc0", "rev0", "cho0", "grn0", "res0", "frz0", "fxb0", "orb0" }) m.set(k, 60);
    m.set("drm_on", 1); m.set("spk_n", 8);
    for (int k = 0; k < 3; ++k) { e.set("st_syn", "sn" + std::to_string(k), 52 + k * 3); e.set("st_syn", "sv" + std::to_string(k), 100); e.set("st_syn", "sg" + std::to_string(k), 1); }
    h.setSynthInto(2);
    float big = run(10.0, 0.5f);
    check(!nan && big < 4.0f, "all effects + synth + drums for 10 s: clean", big);
    // ring numbering
    {
        for (auto k : { "halo0", "shd0", "dly0", "spc0", "rev0", "cho0", "grn0", "res0", "frz0", "fxb0", "orb0" }) m.set(k, 0);
        m.set("drm_on", 0); h.setSynthInto(0);
        h.model.set("spk_n", 8); h.speakerLayout = 0; h.applySpeakers();
        run(1.0, 0.3f); for (int k = 1; k <= 8; ++k) e.peak("spk" + std::to_string(k));
        run(1.0, 0.3f);
        float p[9]; for (int k = 1; k <= 8; ++k) p[k] = e.peak("spk" + std::to_string(k));
        check(p[1] > 2 * p[5] && p[2] > 2 * p[5] && p[1] > 2 * p[4], "8-speaker ring: 1-2 in front (main mix), 4-5 behind", p[1]);
    }
    h.model.loadSetupState(savedSetup); h.speakerLayout = savedLayout; h.setSynthInto(savedSynth); h.applySpeakers();
    std::printf("%d checks, %d failed\n", n, fails);
    return fails;
}

} // namespace arco
