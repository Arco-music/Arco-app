// ARCO - a live looping station for instruments: 4 scenes, 8 tracks, 4 live inputs, 18 effects, up to 32 speakers.
#include <JuceHeader.h>
#include "App/AudioHost.h"
#include "UI/MainView.h"
#include "App/SelfTest.h"
#include "App/Updater.h"
#include "UI/Sheets.h"

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
        if (cmd.contains("setup")) mc->canvas.openSetup(true);
        if (cmd.contains("synth")) mc->canvas.openSynth(true);
        if (cmd.contains("look")) mc->canvas.openLook(true);
        if (cmd.contains("allfx")) { host->visibleCards.clear(); for (auto& d : arco::cardTable()) host->visibleCards.add(d.key); mc->canvas.layoutCards(); }
        if (cmd.contains("fewfx")) { host->visibleCards = juce::StringArray { "eq", "dly", "rev", "flg", "png", "tap" }; mc->canvas.layoutCards(); }
        if (cmd.contains("automix")) mc->canvas.command(arco::cmdAutoMix);
        if (cmd.contains("export")) mc->canvas.command(arco::cmdExport);
        if (cmd.contains("help")) mc->canvas.command(arco::cmdHelp);
        if (cmd.contains("about")) mc->canvas.command(arco::cmdAbout);
        if (cmd.contains("learn")) mc->canvas.setMidiLearn(true);
        if (cmd.contains("import=")) mc->canvas.requestImport(juce::File(cmd.fromFirstOccurrenceOf("import=", false, false).upToFirstOccurrenceOf(" ", false, false)), 2);
        juce::Timer::callAfterDelay(cmd.contains("import=") ? 4000 : 1500, [this, mc, out] {
            auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png; juce::FileOutputStream os(out); os.setPosition(0); os.truncate(); png.writeImageToStream(img, os);
            delete mc; quit(); });
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
            // an iPhone is mostly a microphone for the Mac: it opens on MIC FOR MAC (the station is one tap away)
            if (juce::SystemStats::getDeviceDescription().containsIgnoreCase("iPhone")) { mc->mic.backText = "OPEN THE STATION"; mc->showMic(true); }
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
