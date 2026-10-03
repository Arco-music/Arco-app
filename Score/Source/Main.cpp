// SCORE - compose, record, edit and perform: an arranger and a score line of stations, for Mac and iPad.
#include <JuceHeader.h>
#include "App/Host.h"
#include "App/Demo.h"
#include "App/SelfTest.h"
#include "UI/MainView.h"

class ScoreApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "SCORE"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface(score::Theme::face(0));
        bool test = cmd.contains("--screenshot") || cmd.contains("--selftest");
        host = std::make_unique<score::Host>(!test);
        if (cmd.contains("--selftest")) {
            int f = score::runSelfTest(*host);
           #if JUCE_IOS
            std::fflush(stdout); std::exit(f == 0 ? 0 : 1);
           #endif
            setApplicationReturnValue(f == 0 ? 0 : 1); quit(); return;
        }
        if (cmd.contains("--screenshot")) { screenshot(cmd); return; }
        bool ui = cmd.contains("--uitest");
        auto step = [ui](const char* s) { if (ui) { std::printf("step: %s\n", s); std::fflush(stdout); } };
        step("host ready");
        if (!host->recent.isEmpty() && juce::File(host->recent[0]).getChildFile("project.json").existsAsFile()) host->openProject(juce::File(host->recent[0]));
        else if (firstRun()) { score::makeDemo(*host); host->saveProjectAs("Demo"); }
        step("piece ready");
        window = std::make_unique<MainWindow>(*host);
        step("window open");
        if (ui) { uiTest(); return; }
        if (cmd.isNotEmpty()) anotherInstanceStarted(cmd);
    }
    void shutdown() override { window = nullptr; host = nullptr; }

    // SCORE --uitest: the normal start (sound card, demo, window), then every view and window once, then quit
    void uiTest() {
        auto* mc = dynamic_cast<score::MainView*>(window->getContentComponent());
        auto say = [](const char* s) { std::printf("step: %s\n", s); std::fflush(stdout); };
        struct Step { int ms; std::function<void()> f; };
        auto steps = std::make_shared<std::vector<Step>>();
        auto& p = *host->project;
        steps->push_back({ 1500, [=] { say("play"); host->play(); } });
        steps->push_back({ 1500, [=] { say("score line"); mc->setView(1); } });
        steps->push_back({ 800, [=] { say("go"); host->stations.perform = true; host->stations.go(); } });
        steps->push_back({ 800, [=] { say("mixer"); mc->setView(2); } });
        steps->push_back({ 800, [=, &p] { say("nova"); mc->setView(0); for (auto& t : p.tracks) if (t->inst && t->inst->type == "nova") { mc->openInstrument(t->id); break; } } });
        steps->push_back({ 800, [=, &p] { say("drums"); for (auto& t : p.tracks) if (t->inst && t->inst->type == "drm") { mc->openInstrument(t->id); break; } } });
        steps->push_back({ 800, [=] { say("settings"); mc->showOverlay(new score::SettingsPanel()); } });
        steps->push_back({ 800, [=] { say("export"); mc->closeOverlay(); mc->showOverlay(new score::ExportPanel()); } });
        steps->push_back({ 800, [=] { say("stop"); mc->closeOverlay(); host->stop(); } });
        steps->push_back({ 500, [] { std::printf("UITEST OK\n"); std::fflush(stdout);
                                      #if JUCE_IOS
                                       std::exit(0);
                                      #endif
                                       juce::JUCEApplication::quit(); } });
        auto run = std::make_shared<std::function<void(size_t)>>();
        *run = [steps, run](size_t i) { if (i >= steps->size()) return; juce::Timer::callAfterDelay((*steps)[i].ms, [steps, run, i] { (*steps)[i].f(); (*run)(i + 1); }); };
        (*run)(0);
    }

    static bool firstRun() {
        auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SCORE").getChildFile("welcomed");
        if (f.exists()) return false;
        f.getParentDirectory().createDirectory(); f.create(); return true;
    }

    // a file opened with SCORE (a piece, a zip, sound files)
    void anotherInstanceStarted(const juce::String& cmd) override {
        if (!host || !window) return;
        for (auto& a : juce::StringArray::fromTokens(cmd, " ", "\"")) {
            juce::File f(a.unquoted()); if (!f.exists()) continue;
            if (f.isDirectory() || f.hasFileExtension("zip;json")) { if (host->dirty) host->saveProject(); host->openProject(f); return; }
            host->importClip(f, 0, host->engine.position());
        }
    }

    void systemRequestedQuit() override {
        if (!window || !host || !host->dirty || asking) { quit(); return; }
        if (host->project->folder.getChildFile("project.json").existsAsFile()) { host->saveProject(); quit(); return; }   // a named piece: saved quietly
        bool empty = true; for (auto& t : host->project->tracks) if (!t->clips.empty()) empty = false;
        if (empty) { quit(); return; }
        asking = true;
        auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Keep this piece?")
                        .withMessage("\"" + host->project->name + "\" has not been saved yet.").withButton("Save").withButton("Don't save").withButton("Cancel");
        juce::AlertWindow::showAsync(opts, [this](int r) {
            asking = false;
            if (r == 2) { quit(); return; }
            if (r == 1) { host->saveProject(); quit(); }
        });
    }
    bool asking = false;

    // SCORE --screenshot out.png [arrange|line|mixer|nova|sampler|drums|editor|audio|settings|export] [light]
    void screenshot(const juce::String& cmd) {
        auto args = juce::StringArray::fromTokens(cmd, true);
        juce::File out(args[args.indexOf("--screenshot") + 1]);
        score::Theme::lightMode() = cmd.contains("light");
        host->theme = score::Theme::lightMode() ? 1 : 0;
        score::makeDemo(*host);
        auto* mc = new score::MainView(*host);
        mc->setSize(1440, 900);
        auto& p = *host->project;
        host->engine.locate(26);
        if (cmd.contains("line")) { mc->setView(1); host->stations.select(1); }
        if (cmd.contains("mixer")) mc->setView(2);
        if (cmd.contains("nova")) mc->openInstrument(p.tracks[2]->id);
        if (cmd.contains("drums")) { host->selectClip(p.tracks[0]->clips[0]->id); mc->openInstrument(p.tracks[0]->id); }
        if (cmd.contains("sampler")) { auto id = host->clipToSampler(p.tracks[3]->clips[0]->id); mc->openInstrument(id); }
        if (cmd.contains("editor")) { host->selectClip(p.tracks[2]->clips[0]->id); mc->bottomTab = 1; mc->editor.update(); }
        if (cmd.contains("audio")) { host->selectClip(p.tracks[3]->clips[0]->id); mc->bottomTab = 1; mc->editor.update(); }
        if (cmd.contains("devices")) host->selectTrack(p.tracks[2]->id);
        if (cmd.contains("settings")) mc->showOverlay(new score::SettingsPanel());
        if (cmd.contains("export")) mc->showOverlay(new score::ExportPanel());
        mc->resized();
        juce::Timer::callAfterDelay(1500, [this, mc, out] {
            mc->resized();
            auto img = mc->createComponentSnapshot(mc->getLocalBounds(), true, 1.5f);
            juce::PNGImageFormat png; juce::FileOutputStream os(out); os.setPosition(0); os.truncate(); png.writeImageToStream(img, os);
            delete mc; quit(); });
    }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(score::Host& h) : DocumentWindow("SCORE", score::Theme::bg(), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            juce::Desktop::getInstance().setGlobalScaleFactor(h.uiScale);
            auto* mc = new score::MainView(h);
           #if JUCE_IOS || JUCE_ANDROID
            setContentOwned(mc, false); setFullScreen(true); setVisible(true); return;
           #endif
            setContentOwned(mc, false);
            setResizable(true, true);
            setResizeLimits(1100, 680, 5000, 3000);
            auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            centreWithSize(juce::jmin(1520, area.getWidth() - 40), juce::jmin(940, area.getHeight() - 40));
            setVisible(true);
            mc->grabKeyboardFocus();
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
private:
    std::unique_ptr<score::Host> host;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(ScoreApp)
