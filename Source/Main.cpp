// ARCO - a live looping station for instruments: 4 scenes, 8 tracks, 4 live inputs, 18 effects, up to 32 speakers.
#include <JuceHeader.h>
#include "App/AudioHost.h"
#include "UI/MainView.h"
#include "App/SelfTest.h"
#include "App/Updater.h"
#include "UI/Sheets.h"
#include "UI/StudioView.h"

class ARCOApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "ARCO"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface(arco::Theme::face(0));
        bool test = cmd.contains("--screenshot") || cmd.contains("--selftest") || cmd.contains("--update-check") || cmd.contains("--analyse");
        if (cmd.contains("--analyse")) {               // for testing: what ARCO hears in a file
            auto args = juce::StringArray::fromTokens(cmd, true);
            juce::File f(args[args.indexOf("--analyse") + 1].unquoted());
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            arco::ImportPlan p; juce::String err;
            if (arco::prepareImport(f, 48000, p, fm, err))
                std::printf("%s: %.2f BPM (conf %.2f, loop %d, bars %d, beat %.3f s, bar line %.3f s), %s (conf %.2f)\n", f.getFileName().toRawUTF8(), p.fileBpm, p.tempo.confidence,
                            (int) p.tempo.wholeBars, p.tempo.bars, p.tempo.beatOffset / 48000, p.tempo.downbeatOffset / 48000, p.key.name().toRawUTF8(), p.key.confidence);
            else std::printf("%s\n", err.toRawUTF8());
            if (p.audio.getNumSamples() > 0) {
                auto e = arco::analysis::onsetEnvelope(p.audio.getReadPointer(0), p.audio.getNumSamples(), 48000);
                float mx = 0; double sum = 0; for (float v : e.v) { mx = juce::jmax(mx, v); sum += v; }
                auto on = arco::analysis::onsets(p.audio.getReadPointer(0), p.audio.getNumSamples(), 48000);
                std::printf("  onset envelope max %.4f mean %.5f, %d onsets\n", mx, sum / juce::jmax<size_t>(1, e.v.size()), (int) on.size());
            }
            std::fflush(stdout); quit(); return;
        }
        host = std::make_unique<arco::AudioHost>(!test);
        if (cmd.contains("--screenshot")) { screenshot(cmd); return; }
        if (cmd.contains("--update-check")) {          // for testing the UPDATE button
            updater = std::make_unique<arco::Updater>();
            updater->onChecked = [this](const juce::String& m, int b, const juce::String& n) {
                std::printf("update check: %s, latest build %d (this is %d)\n%s\n", m.toRawUTF8(), b, arco::Updater::build(), n.substring(0, 200).toRawUTF8());
                std::fflush(stdout); quit(); };
            if (cmd.contains("--install")) { updater->onProgress = [](const juce::String& m) { std::printf("  %s\n", m.toRawUTF8()); std::fflush(stdout); }; updater->install(); } else updater->check(); return;
        }
        if (cmd.contains("--selftest")) {
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ARCO selftest");
            dir.createDirectory();
            int f = arco::runSelfTest(*host, dir);
           #if JUCE_IOS
            std::fflush(stdout); std::exit(f == 0 ? 0 : 1);
           #endif
            setApplicationReturnValue(f == 0 ? 0 : 1);
            quit(); return;
        }
        window = std::make_unique<MainWindow>(*host);
       #if JUCE_MAC
        if (auto* mc = dynamic_cast<arco::MainComponent*>(window->getContentComponent())) {
            static juce::PopupMenu extras = arco::AppMenu::appleExtras();
            juce::MenuBarModel::setMacMainMenu(mc->canvas.menu.get(), &extras);
        }
       #endif
    }
    void shutdown() override {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
       #endif
        window = nullptr; host = nullptr; }
    void systemRequestedQuit() override {
        // never lose loops by accident
        if (window == nullptr || host == nullptr || !host->unsavedLoops() || asking) { quit(); return; }
        asking = true;
        auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Save the song before quitting?")
                        .withMessage("Your loops are not saved yet.").withButton("Save").withButton("Don't save").withButton("Cancel");
        juce::AlertWindow::showAsync(opts, [this](int r) {
            asking = false;
            if (r == 2) { quit(); return; }                     // don't save
            if (r != 1) return;                                 // cancel
            auto* mc = dynamic_cast<arco::MainComponent*>(window->getContentComponent());
            if (host->songFolder.isDirectory()) { host->saveSong(host->songFolder); quit(); return; }
            if (mc) mc->canvas.chooseFile("Save the song: choose a name (a folder with that name is made)", "*", true,
                                          [this](juce::File f) { if (host->saveSong(f.withFileExtension(""))) quit(); });
        });
    }
    bool asking = false;

    // for testing: render the interface to a PNG and quit (ARCO --screenshot out.png [light])
    void screenshot(const juce::String& cmd) {
        auto args = juce::StringArray::fromTokens(cmd, true);
        juce::File out(args[args.indexOf("--screenshot") + 1]);
        arco::Theme::setPalette(cmd.contains("light") ? 2 : 0);
        if (cmd.contains("palette=")) arco::Theme::setPalette(cmd.fromFirstOccurrenceOf("palette=", false, false).getIntValue());
        auto* mc = new arco::MainComponent(*host);
        mc->setSize(1440, 880);
        if (cmd.contains("portrait")) mc->setSize(820, 1180);               // an iPad held upright
        if (cmd.contains("size=")) { auto sz = cmd.fromFirstOccurrenceOf("size=", false, false).upToFirstOccurrenceOf(" ", false, false);
            mc->setSize(sz.upToFirstOccurrenceOf("x", false, false).getIntValue(), sz.fromFirstOccurrenceOf("x", false, false).getIntValue()); }
        if (cmd.contains("setup")) mc->canvas.openSetup(true);
        if (cmd.contains("synth")) { mc->canvas.openSynth(true); if (cmd.contains("spage=")) mc->canvas.synthPage(cmd.fromFirstOccurrenceOf("spage=", false, false).getIntValue());
            if (cmd.contains("skeys=")) mc->canvas.synthKeys(cmd.fromFirstOccurrenceOf("skeys=", false, false).getIntValue(), cmd.contains("sedit")); }
        if (cmd.contains("look")) mc->canvas.openLook(true);
        if (cmd.contains("kind=")) host->rack.kind.store(cmd.fromFirstOccurrenceOf("kind=", false, false).getIntValue());
        if (cmd.contains("keyboard")) mc->canvas.openKeyboard(true);
        if (cmd.contains("ksearch=")) mc->canvas.keyboardSearch(cmd.fromFirstOccurrenceOf("ksearch=", false, false).upToFirstOccurrenceOf(" ", false, false).replace("+", " "));
        if (cmd.contains("outputs")) { host->engine.grid[0][2].store(1); host->engine.grid[4][3].store(1); host->engine.grid[5][4].store(1); mc->canvas.openOutputs(); }
        if (cmd.contains("modular")) { if (cmd.contains("patch=")) host->rack.modular.setPatch(arco::Modular::defaultPatch(cmd.fromFirstOccurrenceOf("patch=", false, false).getIntValue())); mc->canvas.openModular(true); }
        if (cmd.contains("spatial")) { auto& m = host->model; m.set("amb_on", 1); for (int s : { 4, 5, 7 }) m.set("amb" + std::to_string(s), 1); m.set("amb_mot5", 1); mc->canvas.openSpatial(true); }
        if (cmd.contains("allfx")) { host->visibleCards.clear(); for (auto& d : arco::cardTable()) host->visibleCards.add(d.key); mc->canvas.layoutCards(); }
        if (cmd.contains("newfx")) { host->visibleCards = juce::StringArray { "pdl", "fbk", "eq", "dly", "rev", "spc" }; mc->canvas.layoutCards(); }
        if (cmd.contains("fx14")) { host->visibleCards = juce::StringArray { "voc", "pit", "spr", "tlk", "dly", "spc" }; mc->canvas.layoutCards(); }
        if (cmd.contains("fewfx")) { host->visibleCards = juce::StringArray { "eq", "dly", "rev", "flg", "png", "tap" }; mc->canvas.layoutCards(); }
        if (cmd.contains("phone")) {
            mc->setSize(cmd.contains("wide") ? 844 : 390, cmd.contains("wide") ? 390 : 844); mc->showMic(true); mc->phone.demo = true;
            mc->phone.st.apply(juce::StringArray::fromLines("s ntn 4\ns nlive 2\ns statA 12851\ns transport 1\ns scenes 1\ns smask 3\ns ebpm 96\ns barno 3\ns beatno 2\ns live 1\n"
                "i song Morning Etude\ni cards eq,dly,rev,spc,flg\ni phone1 Paulo's iPhone\np vol4 64\np pan5 -20\np spc4 30\ns lv4 0.5\ns lv0 0.7"));
            int pg = cmd.fromFirstOccurrenceOf("page=", false, false).getIntValue(); mc->phone.setPage(pg);
        }
        if (cmd.contains("studiodemo")) {
            auto& S = host->studio.song; S = arco::StudioSong(); S.sr = 48000; S.bpm = 96;
            const char* nm[4] = { "TRACK 1", "TRACK 2", "TRACK 3", "TRACK 4" };
            for (int t = 0; t < 4; ++t) {
                arco::StudioLane L; L.name = nm[t]; L.colour = 4 + t; L.vol = -3.0f * t; L.pan = t == 2 ? -0.4f : 0.0f; S.lanes.push_back(L);
                std::vector<float> l((size_t) (48000 * 10));
                for (size_t i = 0; i < l.size(); ++i) { double tt = i / 48000.0; double env = std::exp(-std::fmod(tt, 0.625 * (t + 1)) * 3.0); l[i] = (float) (0.6 * env * std::sin(2 * M_PI * (110.0 * (t + 1)) * tt)); }
                S.sources.push_back(host->studio.makeSource(juce::String("scene A - track ") + juce::String(t + 1), std::move(l)));
                arco::StudioClip c; c.lane = t; c.source = t; c.start = (long) (t == 3 ? 48000 * 10 : 0); c.length = 48000 * (t == 3 ? 20 : 40); c.fadeIn = 2000; c.fadeOut = t == 0 ? 96000 : 200; S.clips.push_back(c);
            }
            host->studio.laneStrip = { 4, 5, 6, 7 }; host->studio.laneScene = { 1, 1, 1, 1 };
            { auto eq = arco::StudioDevice::make(arco::DevEQ); eq.p[0] = 80; eq.p[3] = -3; eq.p[6] = 2; S.lanes[0].devices.push_back(eq); S.lanes[0].devices.push_back(arco::StudioDevice::make(arco::DevComp));
              S.lanes[0].sendA = 0.25f; S.lanes[1].autos.push_back({ "vol", { { 0, -12.0f }, { 48000 * 6.0, 0.0f }, { 48000 * 14.0, -6.0f } } }); S.lanes[1].showAuto = 0; }
            if (cmd.contains("studioempty")) { S.clips.clear(); for (auto& L : S.lanes) { L.autos.clear(); L.showAuto = -1; } }
            auto* sv = new arco::StudioView(mc->canvas); sv->setSize(1360, 820); host->studio.pos = 48000 * 12;
            sv->open();
            if (cmd.contains("studiomix")) sv->keyPressed(juce::KeyPress(juce::KeyPress::tabKey));
            juce::Timer::callAfterDelay(1200, [this, sv, out] { auto img = sv->createComponentSnapshot(sv->getLocalBounds(), true, 1.5f);
                juce::PNGImageFormat png; juce::FileOutputStream os(out); os.setPosition(0); os.truncate(); png.writeImageToStream(img, os); delete sv; quit(); });
            return;
        }
        if (cmd.contains("automix")) { mc->canvas.command(arco::cmdAutoMix);
            if (cmd.contains("amwish") && mc->canvas.autoMixSheet) mc->canvas.autoMixSheet->setWish(juce::String::fromUTF8("warm and spacious, track 2 louder, the cello a bit quieter")); }
        if (cmd.contains("export")) mc->canvas.command(arco::cmdExport);
        if (cmd.contains("help")) mc->canvas.command(arco::cmdHelp);
        if (cmd.contains("about")) mc->canvas.command(arco::cmdAbout);
        if (cmd.contains("learn")) mc->canvas.setMidiLearn(true);
        if (cmd.contains("import=")) mc->canvas.requestImport(juce::File(cmd.fromFirstOccurrenceOf("import=", false, false).upToFirstOccurrenceOf(" ", false, false)), 2);
        if (cmd.contains("amdemo")) { amDemo(mc, out); return; }
        juce::Timer::callAfterDelay(cmd.contains("import=") ? 4000 : 1500, [this, mc, out] {
            auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png; juce::FileOutputStream os(out); os.setPosition(0); os.truncate(); png.writeImageToStream(img, os);
            delete mc; quit(); });
    }

    // for the guide: four string parts recorded at very different levels, then AUTO MIX (the engine is run by hand)
    struct AmDemo : juce::Timer {
        arco::AudioHost& h; arco::MainComponent* mc; juce::File out; std::function<void()> done;
        int step = 0, waitTicks = 0; std::array<juce::AudioBuffer<float>, 4> st;
        std::vector<float> o[34]; float* outs[34]; std::vector<float> in1;
        AmDemo(arco::AudioHost& hh, arco::MainComponent* m, juce::File f) : h(hh), mc(m), out(f) {
            for (auto& v : o) v.assign(512, 0.0f); for (int i = 0; i < 34; ++i) outs[i] = o[i].data(); in1.assign(512, 0.0f);
            const double sr = h.engine.sr, bl = sr * 0.5; const long L = (long) (bl * 16);
            for (auto& b : st) { b.setSize(1, (int) L); b.clear(); }
            auto bow = [&](int k, long a, long len, double f, double amp, double att) { double ph = 0;
                for (long i = 0; i < len && a + i < L; ++i) { double t = i / sr, env = std::min(1.0, t / att) * std::min(1.0, (len - i) / (0.08 * sr));
                    ph += 2 * M_PI * f * (1.0 + 0.004 * std::sin(2 * M_PI * 5.5 * t)) / sr; double v = 0; for (int hN = 1; hN <= 10; ++hN) v += std::sin(ph * hN) / hN;
                    st[(size_t) k].addSample(0, (int) (a + i), (float) (amp * env * v * 0.5)); } };
            long bar = (long) (bl * 4);
            const double BASS[4] = { 65.41, 98.0, 110.0, 87.31 }; for (int b = 0; b < 4; ++b) bow(0, b * bar, bar - 200, BASS[b], 0.9, 0.12);
            const double HAR[8] = { 164.81, 196.0, 220.0, 246.94, 220.0, 196.0, 174.61, 196.0 }; for (int n = 0; n < 8; ++n) bow(1, n * bar / 2, bar / 2 - 200, HAR[n], 0.13, 0.25);
            const double MEL[6] = { 440.0, 493.88, 523.25, 587.33, 659.25, 587.33 }; for (int ph = 0; ph < 2; ++ph) for (int n = 0; n < 6; ++n) bow(2, ph * 2 * bar + (long) (n * bl * 0.5), (long) (bl * 0.48), MEL[n], 0.45, 0.03);
            const double PZ[4] = { 146.83, 220.0, 196.0, 220.0 };
            for (int bt = 0; bt < 16; ++bt) { long a = (long) (bt * bl); double f = PZ[bt % 4]; for (long i = 0; i < (long) (0.35 * sr) && a + i < L; ++i) { double v = 0; for (int hN = 1; hN <= 6; ++hN) v += std::sin(2 * M_PI * f * hN * i / sr) * std::pow(0.5, hN); st[3].addSample(0, (int) (a + i), (float) (0.5 * v * std::exp(-i / (0.09 * sr)))); } }
            startTimer(5);
        }
        void timerCallback() override {
            const float* ins[4] = { in1.data(), nullptr, nullptr, nullptr };
            for (int k = 0; k < 8; ++k) { h.engine.process(ins, 4, outs, 34, 512); h.model.tick(); }
            if (waitTicks > 0) { --waitTicks; return; }
            if (step < 4) { if (!h.importing()) { h.importBuffer(st[(size_t) step], step + 1, "demo"); ++step; waitTicks = 40; } return; }
            if (step == 4) { if (h.importing()) return; mc->canvas.command(arco::cmdAutoMix); if (mc->canvas.autoMixSheet) { mc->canvas.autoMixSheet->setWish("warm, one room"); mc->canvas.autoMixSheet->mixNow(); } ++step; waitTicks = 60; return; }
            if (step == 5) { if (mc->canvas.autoMixSheet && mc->canvas.autoMixSheet->hasResult()) { ++step; waitTicks = 20; } return; }
            stopTimer(); if (done) done();
        }
    };
    std::unique_ptr<AmDemo> amd;
    void amDemo(arco::MainComponent* mc, juce::File out) {
        amd = std::make_unique<AmDemo>(*host, mc, out);
        amd->done = [this, mc, out] {
            auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png; juce::FileOutputStream os(out); os.setPosition(0); os.truncate(); png.writeImageToStream(img, os);
            juce::MessageManager::callAsync([this, mc] { amd.reset(); delete mc; quit(); }); };
    }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(arco::AudioHost& h) : DocumentWindow("ARCO", arco::Theme::bg(), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
           #if JUCE_IOS || JUCE_ANDROID
            auto* mc = new arco::MainComponent(h);
            setContentOwned(mc, false);
            setFullScreen(true);
            setVisible(true);
            juce::ignoreUnused(mc);               // (an iPhone opens as a phone: MainComponent does it)
            return;
           #endif
            setContentOwned(new arco::MainComponent(h), true);
            setResizable(true, true);
            setResizeLimits(960, 587, 4000, 2445);
            getConstrainer()->setFixedAspectRatio(1440.0 / 880.0);
            centreWithSize(1440, 880);
            setVisible(true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
private:
    std::unique_ptr<arco::AudioHost> host;
    std::unique_ptr<arco::Updater> updater;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(ARCOApplication)
