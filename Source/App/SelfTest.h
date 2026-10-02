#pragma once
// ARCO --selftest: plays a test tone into the real engine and checks recording, looping, scenes,
// scene memory, saving / opening a song, importing a file and the show recorder. Prints a report, returns failures.
#include "AudioHost.h"
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
        check(rcv.delayMs.load() < 30.0f, "  the buffer adds only a few ms", rcv.delayMs.load());
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
