#pragma once
// ARCO --selftest: plays a test tone into the real engine and checks recording, looping, scenes,
// scene memory, saving / opening a song, importing a file and the show recorder. Prints a report, returns failures.
#include "AudioHost.h"
#include "../UI/SynthPanel.h"
#include "../UI/MainView.h"
#include "../UI/LearnOverlay.h"
#include "Render.h"
#include "AutoMix.h"
#include <cstdio>
#include <cmath>

namespace arco {

// pretend sound cards, to test what ARCO does when a device does not open (as Bluetooth headphones + a microphone often do)
struct FakeDevice : public juce::AudioIODevice {
    FakeDevice(const juce::String& o, const juce::String& i, bool f) : juce::AudioIODevice(o + "|" + i, "ARCO test"), outN(o), inN(i), fail(f) {}
    juce::String outN, inN; bool fail, opened = false, playing = false; double rate = 48000; int bs = 512;
    juce::BigInteger ain, aout; juce::AudioIODeviceCallback* cb = nullptr;
    juce::StringArray getOutputChannelNames() override { juce::StringArray a; for (int k = 0; k < (outN.isEmpty() ? 0 : 8); ++k) a.add("out " + juce::String(k + 1)); return a; }
    juce::StringArray getInputChannelNames() override { juce::StringArray a; for (int k = 0; k < (inN.isEmpty() ? 0 : 2); ++k) a.add("in " + juce::String(k + 1)); return a; }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 64, 128, 256, 480, 512 }; }
    int getDefaultBufferSize() override { return 512; }
    juce::String open(const juce::BigInteger& i, const juce::BigInteger& o, double r, int b) override {
        if (fail) return "the two devices run at different sample rates";
        ain = i; ain.setRange(getInputChannelNames().size(), 64, false); aout = o; aout.setRange(getOutputChannelNames().size(), 64, false);
        rate = r > 0 ? r : 48000; bs = b > 0 ? b : 512; opened = true; return {}; }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start(juce::AudioIODeviceCallback* c) override { if (c) { c->audioDeviceAboutToStart(this); cb = c; } playing = true; }
    void stop() override { if (cb) cb->audioDeviceStopped(); cb = nullptr; playing = false; }
    bool isPlaying() override { return playing; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bs; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return aout; }
    juce::BigInteger getActiveInputChannels() const override { return ain; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};
struct FakeType : public juce::AudioIODeviceType {
    FakeType() : juce::AudioIODeviceType("ARCO test") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool in) const override {
        return in ? juce::StringArray { "Test Mic", "Headset Mic" } : juce::StringArray { "Test Speakers", "BT Headphones", "Broken Speakers" }; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice* d, bool in) const override {
        auto* f = dynamic_cast<FakeDevice*>(d); return f ? getDeviceNames(in).indexOf(in ? f->inN : f->outN) : -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& o, const juce::String& i) override {
        return new FakeDevice(o, i, o == "Broken Speakers" || (o == "BT Headphones" && i == "Test Mic")); }
};

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
        for (double t = 0; t < maxSec; t += 0.05) { if (cond()) return true; run(0.05, amp); h.pollImport(); }
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
        waitFor([&] { return !h.importing() && trk(1) == 3; }, 8.0);
        check(trk(1) == 3, "  track 2 plays the file", trk(1));
    }
    // SMART IMPORT: hear the tempo and key of a file, fit it to the piece
    {
        const double fsr = 44100.0, bpm0 = 100.0; const int bars0 = 4;
        int nS = (int) std::llround(bars0 * 4 * 60.0 / bpm0 * fsr);
        juce::AudioBuffer<float> b(2, nS); b.clear();
        juce::Random rnd(7);
        double beat = 60.0 / bpm0 * fsr;
        auto addTone = [&](double f, double amp, int a, int len) { for (int i = 0; i < len && a + i < nS; ++i) { double env = std::exp(-i / (fsr * 1.2)) * juce::jmin(1.0, i / 200.0);
                float v = (float) (amp * env * (std::sin(2 * M_PI * f * i / fsr) + 0.5 * std::sin(4 * M_PI * f * i / fsr) + 0.25 * std::sin(6 * M_PI * f * i / fsr))); b.addSample(0, a + i, v); b.addSample(1, a + i, v); } };
        for (int bt = 0; bt < bars0 * 4; ++bt) {
            int a = (int) std::llround(bt * beat);
            if (bt % 2 == 0) for (int i = 0; i < 6000 && a + i < nS; ++i) { float v = (float) (0.8 * std::exp(-i / 1500.0) * std::sin(2 * M_PI * (50 + 80 * std::exp(-i / 800.0)) * i / fsr)); b.addSample(0, a + i, v); b.addSample(1, a + i, v); }
            else for (int i = 0; i < 4000 && a + i < nS; ++i) { float v = (float) (0.35 * std::exp(-i / 900.0) * (rnd.nextFloat() * 2 - 1)); b.addSample(0, a + i, v); b.addSample(1, a + i, v); }
            int hh = (int) std::llround((bt + 0.5) * beat);
            for (int i = 0; i < 1200 && hh + i < nS; ++i) { float v = (float) (0.12 * std::exp(-i / 250.0) * (rnd.nextFloat() * 2 - 1)); b.addSample(0, hh + i, v); b.addSample(1, hh + i, v); }
            if (bt % 4 == 0) {   // A minor, then F major, C major, G major? keep it A minor: Am - Dm - Am - E
                static const double CH[4][3] = { { 220.0, 261.63, 329.63 }, { 293.66, 349.23, 440.0 }, { 220.0, 261.63, 329.63 }, { 246.94, 329.63, 415.30 } };
                static const double BASS[4] = { 110.0, 146.83, 110.0, 164.81 };
                int bar = bt / 4;
                for (double f : CH[bar % 4]) addTone(f, 0.08, a, (int) (beat * 4));
                addTone(BASS[bar % 4], 0.12, a, (int) (beat * 4));
            }
        }
        auto f = dir.getChildFile("selftest groove.wav"); f.deleteFile();
        { juce::WavAudioFormat fmt; std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(new juce::FileOutputStream(f), fsr, 2, 24, {}, 0)); if (w) w->writeFromAudioSampleBuffer(b, 0, nS); }
        ImportPlan plan; juce::String err;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        bool ok = prepareImport(f, e.sr, plan, fm, err);
        check(ok && std::abs(plan.fileBpm - bpm0) < 0.6, "SMART IMPORT: hears the tempo of a groove (BPM)", plan.fileBpm);
        check(plan.tempo.wholeBars && plan.tempo.bars == bars0, "  knows it is a 4-bar loop", plan.tempo.bars);
        check(plan.key.majorEquivalent() == 0, ("  hears the key: " + plan.key.name()).toRawUTF8());
        PieceInfo pc; pc.sr = e.sr; pc.bpm = 120; pc.beats = 4; pc.beatLen = 60.0 * e.sr / 120.0; pc.loopLen = 0; pc.hasLoops = true;
        pc.key.tonic = 2; pc.key.minor = false;                  // the piece is in D major: the file moves up 2 semitones
        planImport(plan, pc);
        check(plan.semitones == 2 && plan.outBars == 4, "  plans: +2 semitones, 4 bars", plan.semitones * 10 + plan.outBars);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = renderImport(plan, pc);
        double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        long expectN = (long) std::llround(4 * 4 * pc.beatLen);
        check(out.getNumSamples() == expectN, "  fitted: exactly 4 bars at 120 BPM (seconds)", out.getNumSamples() / e.sr);
        std::vector<float> mono((size_t) out.getNumSamples()); for (int i = 0; i < out.getNumSamples(); ++i) mono[(size_t) i] = 0.5f * (out.getSample(0, i) + out.getSample(1, i));
        auto t2 = analysis::tempo(mono.data(), (int) mono.size(), e.sr);
        auto k2 = analysis::key(mono.data(), (int) mono.size(), e.sr);
        check(std::abs(t2.bpm - 120.0) < 0.8, "  the result plays at 120 BPM", t2.bpm);
        { auto f2 = dir.getChildFile("selftest groove fitted.wav"); f2.deleteFile(); juce::WavAudioFormat fmt; std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(new juce::FileOutputStream(f2), e.sr, 2, 24, {}, 0)); if (w) w->writeFromAudioSampleBuffer(out, 0, out.getNumSamples()); }
        check(k2.majorEquivalent() == 2, ("  ... in the new key: " + k2.name()).toRawUTF8());
        check(out.getMagnitude(0, out.getNumSamples()) < 1.5f && ms < 15000, "  clean, and quick (ms)", ms);
        // in a scene with a 8-bar loop the 4 bars repeat twice
        pc.loopLen = 2 * expectN;
        auto out2 = renderImport(plan, pc);
        float d = 0; for (int i = 0; i < (int) expectN; i += 7) d = juce::jmax(d, std::abs(out2.getSample(0, i) - out2.getSample(0, i + (int) expectN)));
        check(out2.getNumSamples() == 2 * expectN && d < 1e-6f, "  in an 8-bar loop it repeats twice, seamless", out2.getNumSamples() / e.sr);
        // and the looper takes it
        check(h.importBuffer(out2.getNumSamples() > 0 ? out : out2, 3, "groove"), "  into track 3");
        check(waitFor([&] { return trk(2) == 3; }, 6.0), "  track 3 plays it", trk(2));
    }
    // AUTO MIX: timing, then the whole mix with A / B
    {
        const double bl = e.sr * 0.5; long L = (long) (bl * 8);
        std::vector<float> x((size_t) L, 0.0f);
        const double offs[8] = { 0.012, -0.009, 0.015, 0.0, 0.011, -0.014, 0.008, 0.010 };
        for (int k = 0; k < 8; ++k) { long a = (long) (k * bl + offs[k] * e.sr); for (int i = 0; i < 3000 && a + i < L; ++i) x[(size_t) (a + i)] = (float) (0.7 * std::exp(-i / 500.0) * std::sin(i * 0.2)); }
        int moved = 0; double avg = 0;
        auto y = automix::tighten(x, L, e.sr, bl, 1.0f, moved, avg);
        auto on = analysis::onsets(y.data(), (int) L, e.sr);
        double worst = 0; for (auto& o : on) { double p = analysis::attackStart(y.data(), (int) L, e.sr, o.pos); double g = std::round(p / bl) * bl; worst = std::max(worst, std::abs(p - g)); }
        if (worst >= 0.003 * e.sr) for (auto& o : on) std::printf("        onset %.4f s (attack %.4f) strength %.2f\n", o.pos / e.sr, analysis::attackStart(y.data(), (int) L, e.sr, o.pos) / e.sr, o.strength);
        check(moved >= 6 && worst < 0.003 * e.sr, "AUTO MIX timing: notes land on the beat (worst ms)", worst / e.sr * 1000.0);
        auto y2 = automix::tighten(x, L, e.sr, bl, 0.5f, moved, avg);
        check(moved >= 6 && avg > 4 && avg < 7, "  'gently': half the way (average ms moved)", avg);
        // the whole mix of scene A (tracks 1, 2, 3 are recorded)
        double volBefore = m.get("vol6"), panBefore = m.get("pan6");
        AutoMixOptions ao; ao.timingAmount = 0.5f;
        auto job = automix::prepare(h, ao);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto res = automix::run(job, nullptr);
        double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check(res.ok && !res.changes.empty(), ("AUTO MIX: " + res.headline).toRawUTF8(), ms);
        for (auto& l : res.report) std::printf("        %s (%s): %s\n", l.who.toRawUTF8(), l.role.toRawUTF8(), l.what.toRawUTF8());
        automix::apply(h, res, true);
        run(0.2, 0);
        check(m.get("ms_lim") > 0.5 && m.get("mx_hp6") > 10, "  applied: master limiter on, high-pass on track 3");
        float pk = run(3.0, 0);
        check(!nan && pk > 0.05f && pk < 0.95f, "  the mixed loops sound, never above -1 dB (peak)", pk);
        automix::apply(h, res, false);
        check(std::abs(m.get("vol6") - volBefore) < 1e-6 && std::abs(m.get("pan6") - panBefore) < 1e-6 && m.get("ms_lim") < 0.5, "  A/B: back to before, exactly");
        automix::apply(h, res, true);
        automix::apply(h, res, false);
    }
    // AUTO MIX understands a band: drums, bass, chords and a melody in scene C
    {
        m.press("st_looper", "scene", 8, 3);
        waitFor([&] { return scene() == 3; }, 6.0);
        const double bl = e.snap("sv_bl").v.load(); const int bts = juce::jmax(1, (int) e.snap("sv_bts").v.load());
        const long L = (long) std::llround(bl * bts * 2);           // 2 bars
        const double sr = e.sr; juce::Random rnd(3);
        std::array<juce::AudioBuffer<float>, 4> st; for (auto& b : st) { b.setSize(1, (int) L); b.clear(); }
        auto add = [&](int k, long a, std::function<float(int)> f, int len) { for (int i = 0; i < len && a + i < L; ++i) st[(size_t) k].addSample(0, (int) (a + i), f(i)); };
        int nb = bts * 2;
        for (int bt = 0; bt < nb; ++bt) {
            long a = (long) std::llround(bt * bl);
            if (bt % 2 == 0) add(0, a, [&](int i) { return (float) (0.8 * std::exp(-i / (0.035 * sr)) * std::sin(2 * M_PI * (50 + 90 * std::exp(-i / (0.012 * sr))) * i / sr)); }, (int) (0.25 * sr));
            else add(0, a, [&](int i) { return (float) (0.4 * std::exp(-i / (0.02 * sr)) * (rnd.nextFloat() * 2 - 1)); }, (int) (0.2 * sr));
            for (int h8 = 0; h8 < 2; ++h8) add(0, a + (long) (h8 * bl / 2), [&](int i) { return (float) (0.12 * std::exp(-i / (0.005 * sr)) * (rnd.nextFloat() * 2 - 1)); }, (int) (0.05 * sr));
            // bass: eighth notes, a saw low-passed by its harmonics
            static const double BN[4] = { 55.0, 55.0, 65.41, 49.0 };
            double f = BN[(bt / bts) % 4 + (bt % 2)];
            add(1, a, [&](int i) { double s2 = 0; for (int hN = 1; hN <= 6; ++hN) s2 += std::sin(2 * M_PI * f * hN * i / sr) / (hN * hN); return (float) (0.5 * s2 * std::min(1.0, i / 200.0) * std::exp(-i / (0.6 * sr))); }, (int) (bl * 0.95));
            // melody: plucked notes on every beat, a little late or early
            static const double MN[8] = { 440.0, 523.25, 587.33, 659.25, 587.33, 523.25, 440.0, 392.0 };
            double mf = MN[bt % 8]; long ma = a + (long) ((rnd.nextFloat() - 0.5) * 0.024 * sr);
            add(3, juce::jmax(0L, ma), [&](int i) { double s2 = 0; for (int hN = 1; hN <= 8; ++hN) s2 += std::sin(2 * M_PI * mf * hN * i / sr) * std::pow(0.6, hN); return (float) (0.35 * s2 * std::min(1.0, i / 100.0) * std::exp(-i / (0.25 * sr))); }, (int) (bl * 0.9));
        }
        // chords: long, soft, wide
        for (double f : { 220.0, 277.18, 329.63, 440.0 }) add(2, 0, [&](int i) { double env = std::min(1.0, i / (0.3 * sr)) * std::min(1.0, (L - i) / (0.3 * sr));
                return (float) (0.08 * env * (std::sin(2 * M_PI * f * i / sr) + 0.3 * std::sin(4 * M_PI * f * i / sr * 1.003) + 0.2 * (rnd.nextFloat() * 2 - 1) * 0.1)); }, (int) L);
        for (int k = 0; k < 4; ++k) {
            h.importBuffer(st[(size_t) k], k + 1, "stem");
                waitFor([&] { return !h.importing(); }, 12.0);
        }
        bool all4 = trk(0) >= 3 && trk(1) >= 3 && trk(2) >= 3 && trk(3) >= 3;
        if (!all4) std::printf("        track states %d %d %d %d, busy %.0f\n", trk(0), trk(1), trk(2), trk(3), e.snap("imp_busy").v.load());
        AutoMixOptions ao; auto job = automix::prepare(h, ao); auto res = automix::run(job, nullptr);
        juce::String roles; for (auto& l : res.report) if (l.who.startsWith("TRACK")) roles << l.role << " | ";
        for (auto& l : res.report) std::printf("        %s (%s): %s\n", l.who.toRawUTF8(), l.role.toRawUTF8(), l.what.toRawUTF8());
        bool rolesOk = res.report.size() >= 4 && res.report[0].role.startsWith("drums") && res.report[1].role == "bass" && res.report[2].role.startsWith("pad") && res.report[3].role.startsWith("lead");
        check(all4 && rolesOk, ("AUTO MIX hears: " + roles).toRawUTF8());
        automix::apply(h, res, true);
        check(std::abs(m.get("pan5")) < 1 && std::abs(m.get("pan7")) < 1 && std::abs(m.get("pan6")) > 10 && m.get("spc5") < 6, "  bass and melody in the centre, chords to the side, no SPACE on the bass");
        float pk = run(2.0, 0);
        check(!nan && pk > 0.1f && pk < 0.95f, "  the band sounds, below -1 dB", pk);
        automix::apply(h, res, false);
        m.press("st_looper", "scene", 8, 1);
        waitFor([&] { return scene() == 1; }, 6.0);
    }
    // EXPORT: render the scene offline, normalise, limit, write, read back
    {
        Renderer rd(h);
        ExportOptions o; o.repeats = 2; o.tailSeconds = 2.0; o.loudness = 1; o.bits = 24; o.sampleRate = 48000;
        juce::AudioBuffer<float> mix; double rate = 0;
        bool okR = rd.renderMix(o, mix, rate, -1, nullptr);
        double expect = e.snap("sv_len0").v.load() * 2 + 2.0 * e.sr;
        check(okR && std::abs(mix.getNumSamples() - expect) < e.sr * 0.6, "EXPORT: the mix renders offline (seconds)", mix.getNumSamples() / e.sr);
        check(mix.getMagnitude(0, mix.getNumSamples()) > 0.01f, "  it has the loops in it");
        auto wav = dir.getChildFile("export test.wav"); juce::String rep;
        bool w1 = Renderer::write(mix, rate, o, wav, rep);
        check(w1, ("  WAV 24-bit written: " + rep).toRawUTF8());
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rr(fm.createReaderFor(wav));
        juce::AudioBuffer<float> back(2, rr ? (int) rr->lengthInSamples : 0);
        if (rr) rr->read(&back, 0, back.getNumSamples(), 0, true, true);
        double lu = loud::integrated(back, 48000.0);
        float tp = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < back.getNumSamples(); ++i) tp = std::max(tp, loud::truePeak(back.getReadPointer(c), back.getNumSamples(), i));
        check(rr != nullptr && rr->bitsPerSample == 24 && std::abs(lu + 14.0) < 1.5, "  read back: about -14 LUFS (streaming level)", lu);
        check(juce::Decibels::gainToDecibels(tp) <= -0.85f, "  true peak at or below -1 dBTP", juce::Decibels::gainToDecibels(tp));
        ExportOptions f2 = o; f2.flac = true; f2.bits = 16; f2.sampleRate = 44100;
        auto fl = dir.getChildFile("export test.flac");
        bool w2 = Renderer::write(mix, rate, f2, fl, rep);
        check(w2, ("  FLAC 16-bit 44.1 kHz written: " + rep).toRawUTF8());
        std::unique_ptr<juce::AudioFormatReader> fr(fm.createReaderFor(fl));
        check(fr != nullptr && (int) fr->sampleRate == 44100 && std::abs(fr->lengthInSamples / 44100.0 - mix.getNumSamples() / rate) < 0.1, "  FLAC reads back at 44.1 kHz, same length");
        juce::OwnedArray<juce::AudioBuffer<float>> st; juce::StringArray nm; double sr2;
        check(rd.dryStems(1, st, nm, sr2) && st.size() >= 2, "  dry stems: one per recorded track", st.size());
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
    // the ten new effects: each one on its own, fed by LIVE 1
    {
        for (auto k : { "halo0", "shd0", "dly0", "spc0", "rev0", "cho0", "grn0", "res0", "frz0", "fxb0", "orb0" }) m.set(k, 0);
        m.set("drm_on", 0); h.setSynthInto(0);
        m.press("st_looper", "rstall"); run(0.5, 0);        // no loops playing
        auto ringPeak = [&](const std::string& k) { auto& r = e.ring("xr_" + k); float mx = 0; for (auto v : r.d) { if (!std::isfinite(v)) return 99.0f; mx = std::max(mx, std::abs(v)); } return mx; };
        run(11.0, 0);                                       // tails from the earlier tests ring out
        float silent = 0; for (auto& d : extraFxDefs()) silent = std::max(silent, ringPeak(d.key));
        check(silent < 0.001f, "  (control: no sends = the effects are silent)", silent);
        bool ok = true; juce::String bad;
        for (auto& d : extraFxDefs()) {
            std::string sid = std::string(d.key) + "0";
            if (std::string(d.key) == "stu") m.set("stu_chance", 100);
            if (std::string(d.key) == "oct") m.set("oct_down", 80);
            m.set(sid, 85); nan = false;
            float pk = 0; for (int r = 0; r < 4; ++r) { run(0.5, 0.3f); pk = std::max(pk, ringPeak(d.key)); }
            m.set(sid, 0); run(0.2, 0);
            bool good = !nan && pk > 0.01f && pk < 3.0f;
            if (std::getenv("ARCO_DEBUG")) std::printf("    %s peak %.3f\n", d.key, pk);
            if (!good) { ok = false; bad << d.key << "(" << juce::String(pk, 3) << ") "; }
        }
        check(ok, ("NEW EFFECTS: all 10 sound, clean, not too loud " + bad).toRawUTF8());
    }
    // odd buffer sizes (Bluetooth, some interfaces): same sound and same timing as with 512, only 64 samples later
    {
        auto render = [&](int bs, std::vector<float>& outv, float& lph) {
            Engine e2; e2.prepare(48000); e2.inputChannel[0].store(0);
            std::vector<float> a((size_t) bs), q[34]; for (auto& v : q) v.assign((size_t) bs, 0);
            float* oo[34]; for (int i = 0; i < 34; ++i) oo[i] = q[i].data();
            const float* ii[1] = { a.data() };
            long total = 0; outv.clear();
            while (total < 48000 * 2) {
                for (int i = 0; i < bs; ++i) a[(size_t) i] = 0.3f * (float) std::sin(2 * M_PI * 300.0 * (double) (total + i) / 48000);
                e2.process(ii, 1, oo, 34, bs);
                outv.insert(outv.end(), q[0].begin(), q[0].end());
                total += bs;
            }
            lph = e2.snap("lphase").v.load();
        };
        std::vector<float> ref; float lref; render(512, ref, lref);
        for (int bs : { 480, 37, 1000 }) {
            std::vector<float> o2; float l2; render(bs, o2, l2);
            double maxd = 0, pkk = 0;
            for (size_t i = 4800; i + 64 < std::min(ref.size(), o2.size()) && i < 90000; ++i) { maxd = std::max(maxd, (double) std::abs(ref[i] - o2[i + 64])); pkk = std::max(pkk, (double) std::abs(ref[i])); }
            juce::String what = "buffer " + juce::String(bs) + ": same sound and timing as 512";
            check(pkk > 0.05 && maxd < 1e-3, what.toRawUTF8(), maxd);
        }
    }
    // phone microphone: a sender (as on the iPhone, 44.1 kHz) and the receiver (as on the Mac, 48 kHz) over the network
    {
        MicSender snd; MicReceiver rcv;
        snd.start(); juce::Thread::sleep(200);
        rcv.connect(MicReceiver::WiFi, "127.0.0.1");
        const double sr = 44100.0, outSr = 48000.0; const int bn = 256, on = 256;
        std::vector<float> x(bn), y(on), all;
        double ph2 = 0; int blocksIn = 0;
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        // real time: the phone sends a 441 Hz tone; the Mac pulls in its own blocks
        double inClock = 0, outClock = 0; int dAt2 = -1;
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 4000) {
            double now = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            while (inClock < now) { for (auto& v : x) { v = 0.5f * (float) std::sin(ph2); ph2 += 2 * M_PI * 441.0 / sr; } snd.push(x.data(), bn, sr); inClock += bn / sr; ++blocksIn; }
            while (outClock < now) { int d0 = rcv.dropouts.load(); rcv.pull(y.data(), on, outSr); if (outClock > 2.0 && dAt2 < 0) dAt2 = rcv.dropouts.load(); if (outClock > 2.0) all.insert(all.end(), y.begin(), y.end()); outClock += on / outSr;
                if (std::getenv("ARCO_DEBUG") && rcv.dropouts.load() != d0) std::printf("    dropout at %.3f s (in %.3f) buffer %.1f ms\n", outClock, inClock, rcv.delayMs.load()); }
            juce::Thread::sleep(1);
        }
        check(rcv.connected(), "PHONE MIC: the Mac finds the phone (Wi-Fi)");
        int zc = 0; for (size_t i = 1; i < all.size(); ++i) if ((all[i - 1] < 0) != (all[i] < 0)) ++zc;
        double hz = zc / 2.0 / (all.size() / outSr);
        float pkr = 0; for (auto v : all) pkr = std::max(pkr, std::abs(v));
        check(std::abs(hz - 441.0) < 4.0 && pkr > 0.4f, "  the tone arrives at the right pitch (Hz)", hz);
        check(rcv.delayMs.load() < 45.0f, "  the buffer adds only a few ms", rcv.delayMs.load());
        check(rcv.dropouts.load() - dAt2 == 0, "  no dropouts once settled (2 s of sound)", rcv.dropouts.load() - dAt2);
        // through the engine: phone on LIVE 2
        rcv.disconnect(); snd.stop();
    }
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
    // the SYNTH: notes make sound, it plays into LIVE 2, a track records it, every preset is clean
    {
        for (auto k : { "halo0", "shd0", "dly0", "spc0", "rev0", "cho0", "grn0", "res0", "frz0", "fxb0", "orb0" }) m.set(k, 0);
        m.set("drm_on", 0); h.setSynthInto(0); h.model.set("spk_n", 2); h.applySpeakers();
        m.press("st_looper", "rstall"); run(0.5, 0);
        applyFactorySynthPreset(m, 0);
        h.keys.noteOn(1, 57, 0.9f);
        check(h.synthInto == 1, "SYNTH: the first note sends it into LIVE 2");
        float sp = run(0.8, 0);
        check(sp > 0.02f, "  a note sounds (main out)", sp);
        check(e.snap("syn_n").v.load() >= 1, "  one voice sounding", e.snap("syn_n").v.load());
        h.keys.noteOn(1, 61, 0.9f); h.keys.noteOn(1, 64, 0.9f); run(0.3, 0);
        check(e.snap("syn_n").v.load() >= 3, "  a chord: 3 voices", e.snap("syn_n").v.load());
        h.keys.noteOff(1, 57, 0); h.keys.noteOff(1, 61, 0); h.keys.noteOff(1, 64, 0);
        run(3.0, 0);
        check(e.snap("syn_n").v.load() < 0.5f, "  released: silence again", e.snap("syn_n").v.load());
        // record it on TRACK 3
        h.recordSynthOn(3);
        h.keys.noteOn(1, 52, 0.8f);
        check(waitFor([&] { return trk(2) == 7; }, 5.0), "  RECORD ON TRACK 3: recording the synth", trk(2));
        run(2.0, 0);
        h.keys.noteOff(1, 52, 0);
        m.press("st_looper", "rec3");
        check(waitFor([&] { return trk(2) == 3; }, 5.0), "  track 3 plays it back", trk(2));
        run(3.0, 0);
        check(run(3.0, 0) > 0.01f, "  and you hear the recorded synth (no key held)");
        m.press("st_looper", "rstall"); run(0.3, 0);
        // mono + glide: one voice
        m.set("mono", 1); m.set("s_glide", 40);
        h.keys.noteOn(1, 60, 0.8f); h.keys.noteOn(1, 67, 0.8f); run(0.4, 0);
        check(e.snap("syn_n").v.load() < 1.5f && run(0.3, 0) > 0.01f, "  MONO: one voice, gliding");
        h.keys.allNotesOff(0); h.synth.allOff(); run(2.0, 0);
        // every factory sound, a chord each: sounds, no NaN, not too loud
        bool allOk = true; float worst = 0;
        for (int p = 0; p < numFactorySynthPresets(); ++p) {
            applyFactorySynthPreset(m, p);
            bool fol = m.get("follow") > 0.5;
            if (!fol) for (int nn : { 48, 55, 60, 64 }) h.keys.noteOn(1, nn, 0.8f);
            float pk2 = run(1.5, fol ? 0.3f : 0.0f);
            h.keys.allNotesOff(0); h.synth.allOff(); run(1.5, 0);
            if (!(pk2 > 0.01f && pk2 < 3.5f) || nan) { allOk = false; std::printf("    sound %d: peak %.3f\n", p, pk2); }
            worst = std::max(worst, pk2);
        }
        check(allOk, "  every factory sound plays cleanly (loudest peak)", worst);
        applyFactorySynthPreset(m, 0); h.setSynthInto(0);
    }
    // MIDI LEARN: a knob and a button linked to MIDI controls
    {
        auto mc = std::make_unique<MainComponent>(h);
        auto& cv = mc->canvas;
        cv.setMidiLearn(true);
        bool hasFrames = cv.learnLayer != nullptr && cv.learnLayer->isVisible();
        { const juce::ScopedLock sl(h.midiLock); h.learnTarget = "p:master"; }
        h.handleIncomingMidiMessage(nullptr, juce::MidiMessage::controllerEvent(1, 21, 90));
        bool linked = h.linkText("p:master").isNotEmpty();
        { const juce::ScopedLock sl(h.midiLock); h.learnTarget = "t:stop"; }
        h.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 36, (juce::uint8) 100));
        cv.setMidiLearn(false);
        check(hasFrames && linked && h.linkText("t:stop").isNotEmpty(), "MIDI LEARN: a knob (CC 21) and a button (note 36) linked");
        if (h.onMidiControl) h.onMidiControl("p:master", 2, 127, true);
        double hi = m.get("master");
        if (h.onMidiControl) h.onMidiControl("p:master", 2, 0, false);
        check(hi > 99.0 && m.get("master") < 1.0, "  the MIDI knob moves MASTER all the way", hi);
        auto* path = learn::find(&cv, learn::idOf(learn::find(&cv, "p:glue"), &cv));
        check(path != nullptr && learn::find(&cv, "send:dly") != nullptr && learn::find(&cv, "trk1:rec") != nullptr, "  every control has a stable name");
        m.set("master", 75); h.clearMidiMap();
        check(h.linkText("p:master").isEmpty(), "  FORGET ALL unlinks everything");
    }
    // the sound card settings: what happens when a device does not open
    {
        h.devices.addAudioDeviceType(std::make_unique<FakeType>());
        h.devices.setCurrentAudioDeviceType("ARCO test", true);
        auto m1 = h.openAudio("Test Speakers", "Test Mic", 0, 0);
        auto* d = h.devices.getCurrentAudioDevice();
        check(d != nullptr && m1.isEmpty(), "AUDIO: speakers + microphone open");
        check(d != nullptr && d->getActiveInputChannels().countNumberOfSetBits() == 2 && d->getActiveOutputChannels().countNumberOfSetBits() == 8,
              "  every input and output of the card is open");
        h.nLive = 2; h.inputCh[0] = 2; h.inputCh[1] = 1; h.applyRouting();
        check(h.engine.inputChannel[0].load() == 1 && h.engine.inputChannel[1].load() == 0 && h.devices.getCurrentAudioDevice() == d,
              "  choosing LIVE inputs does not reopen the card");
        h.phonesPair = 0; h.applyRouting();
        check(h.engine.phonesFirst.load() == 2 && !h.phonesShared, "  headphones on outputs 3-4");
        auto m2 = h.openAudio("BT Headphones", "Test Mic", 0, 0);
        d = h.devices.getCurrentAudioDevice();
        check(d != nullptr && h.devices.getAudioDeviceSetup().outputDeviceName == "BT Headphones" && m2.contains("does not work together"),
              "  headphones + mic that clash: headphones still play, it says why");
        auto m3 = h.openAudio("Broken Speakers", "", 0, 0);
        d = h.devices.getCurrentAudioDevice();
        check(d != nullptr && h.devices.getAudioDeviceSetup().outputDeviceName == "BT Headphones" && m3.contains("kept the previous"),
              "  a device that will not open: keeps the one that worked (never 'none')");
        auto m4 = h.openAudio("Test Speakers", "Test Mic", 44100, 480);
        d = h.devices.getCurrentAudioDevice();
        check(d != nullptr && d->getCurrentSampleRate() == 44100 && d->getCurrentBufferSizeSamples() == 480, "  sample rate and buffer change");
        h.devices.closeAudioDevice();
        h.nLive = 1; h.inputCh[0] = 1; h.inputCh[1] = 2;
    }
    h.model.loadSetupState(savedSetup); h.speakerLayout = savedLayout; h.setSynthInto(savedSynth); h.applySpeakers();
    std::printf("%d checks, %d failed\n", n, fails);
    return fails;
}

} // namespace arco
