// ARCO - a live looping station for instruments: 4 scenes, 8 tracks, 4 live inputs, 18 effects, up to 32 speakers.
#include <JuceHeader.h>
#include "App/AudioHost.h"
#include "UI/MainView.h"
#include "App/SelfTest.h"

class ARCOApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "ARCO"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface(arco::Theme::face(0));
        bool test = cmd.contains("--screenshot") || cmd.contains("--selftest");
        host = std::make_unique<arco::AudioHost>(!test);
        if (cmd.contains("--screenshot")) { screenshot(cmd); return; }
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
    }
    void shutdown() override { window = nullptr; host = nullptr; }
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
        arco::Theme::lightMode() = cmd.contains("light");
        auto* mc = new arco::MainComponent(*host);
        mc->setSize(1440, 880);
        if (cmd.contains("setup")) mc->canvas.openSetup(true);
        if (cmd.contains("synth")) mc->canvas.openSynth(true);
        juce::Timer::callAfterDelay(1500, [this, mc, out] {
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
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(ARCOApplication)
