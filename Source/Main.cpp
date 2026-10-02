// ARCO - a live looping station for instruments: 4 scenes, 8 tracks, 4 live inputs, 18 effects, up to 32 speakers.
#include <JuceHeader.h>
#include "App/AudioHost.h"
#include "UI/MainView.h"

class ARCOApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "ARCO"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface(arco::Theme::face(0));
        host = std::make_unique<arco::AudioHost>();
        if (cmd.contains("--screenshot")) { screenshot(cmd); return; }
        window = std::make_unique<MainWindow>(*host);
    }
    void shutdown() override { window = nullptr; host = nullptr; }
    void systemRequestedQuit() override { quit(); }

    // for testing: render the interface to a PNG and quit (ARCO --screenshot out.png [light])
    void screenshot(const juce::String& cmd) {
        auto args = juce::StringArray::fromTokens(cmd, true);
        juce::File out(args[args.indexOf("--screenshot") + 1]);
        arco::Theme::lightMode() = cmd.contains("light");
        auto* mc = new arco::MainComponent(*host);
        mc->setSize(1440, 880);
        if (cmd.contains("setup")) mc->canvas.openSetup(true);
        juce::Timer::callAfterDelay(1500, [this, mc, out] {
            auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat png; juce::FileOutputStream os(out); os.setPosition(0); os.truncate(); png.writeImageToStream(img, os);
            delete mc; quit(); });
    }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(arco::AudioHost& h) : DocumentWindow("ARCO", arco::Theme::bg(), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
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
