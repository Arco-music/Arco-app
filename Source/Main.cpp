// ARCO - a live looping station for instruments: 4 scenes, 8 tracks, 4 live inputs, 18 effects, up to 32 speakers.
#include <JuceHeader.h>
#include "App/AudioHost.h"
#include "UI/MainView.h"
#include "UI/Shell.h"
#include "UI/ShellViews.h"
#include "App/SelfTest.h"
#include "App/Probe.h"
#include "App/ImportCheck.h"
#include "App/StudioCheck.h"
#include "UI/Panels.h"
#include "UI/InstrumentPanels.h"
#include "App/PatcherCheck.h"
#include "UI/PatcherView.h"
#include "App/Updater.h"
#include "UI/Sheets.h"
namespace arco { size_t audioHostSizeHere(); size_t studioSizeHere(); size_t engineSizeHere(); }
#include "UI/StudioView.h"

class ARCOApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "ARCO"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& cmd) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface(arco::Theme::face(0));
        if (cmd.contains("--sizes")) { std::printf("AudioHost: here %zu, there %zu   Studio: here %zu, there %zu   Engine: here %zu, there %zu\n", sizeof(arco::AudioHost), arco::audioHostSizeHere(),
                                                   sizeof(arco::Studio), arco::studioSizeHere(), sizeof(arco::Engine), arco::engineSizeHere()); quit(); return; }
        bool test = cmd.contains("--screenshot") || cmd.contains("--selftest") || cmd.contains("--update-check") || cmd.contains("--analyse") || cmd.contains("--probe") || cmd.contains("--layoutcheck") || cmd.contains("--importcheck") || cmd.contains("--studiocheck") || cmd.contains("--clickcheck") || cmd.contains("--patchercheck");
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
        if (cmd.contains("--layoutcheck")) { layoutCheck(); return; }
        if (cmd.contains("--clickcheck")) { clickCheck(); return; }
        if (cmd.contains("--update-check")) {          // for testing the UPDATE button
            updater = std::make_unique<arco::Updater>();
            updater->onChecked = [this](const juce::String& m, int b, const juce::String& n) {
                std::printf("update check: %s, latest build %d (this is %d)\n%s\n", m.toRawUTF8(), b, arco::Updater::build(), n.substring(0, 200).toRawUTF8());
                std::fflush(stdout); quit(); };
            if (cmd.contains("--install")) { updater->onProgress = [](const juce::String& m) { std::printf("  %s\n", m.toRawUTF8()); std::fflush(stdout); }; updater->install(); } else updater->check(); return;
        }
        if (cmd.contains("--patchercheck")) { int f = arco::runPatcherCheck(); setApplicationReturnValue(f == 0 ? 0 : 1); quit(); return; }
        if (cmd.contains("--studiocheck")) { int f = arco::runStudioCheck(*host); setApplicationReturnValue(f == 0 ? 0 : 1); quit(); return; }
        if (cmd.contains("--importcheck")) { int f = arco::runImportCheck(*host); setApplicationReturnValue(f == 0 ? 0 : 1); quit(); return; }
        if (cmd.contains("--probe")) { int f = arco::runProbe(*host); setApplicationReturnValue(f == 0 ? 0 : 1); quit(); return; }
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


    // 2.3: for testing - every screen at several sizes: do any two controls overlap, or does one stick out of its panel?
    struct LCfg { arco::Screen sc; int w, h, tracks, pal; juce::String extra; };
    std::vector<LCfg> lcfg; size_t lci = 0; int lcProblems = 0; arco::MainComponent* lcmc = nullptr;
    static bool lcControl(juce::Component* c) {
        return dynamic_cast<arco::Pill*>(c) || dynamic_cast<arco::Knob*>(c) || dynamic_cast<arco::Fader*>(c) || dynamic_cast<arco::ValueBox*>(c) || dynamic_cast<arco::Segments*>(c);
    }
    static juce::String lcName(juce::Component* c) {
        juce::String n = c->getComponentID();
        if (auto* p = dynamic_cast<arco::Pill*>(c)) n << " '" << (p->dynText ? p->dynText() : p->text) << "'";
        if (auto* sg = dynamic_cast<arco::Segments*>(c)) n << " [" << sg->items.joinIntoString("|") << "]";
        juce::String cls = typeid(*c).name(); return cls.fromLastOccurrenceOf("arco", false, false).retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") + ":" + n;
    }
    void lcCollect(juce::Component* c, std::vector<juce::Component*>& out) {
        if (!c->isVisible()) return;
        if (lcControl(c)) { out.push_back(c); return; }
        for (auto* ch : c->getChildren()) lcCollect(ch, out);
    }
    // ARCO --clickcheck: every screen, every key on it pressed one by one (the app runs between presses), menus and
    // dialogs closed - nothing may throw or crash, and the screen must still be the app's afterwards
    std::vector<arco::Screen> ccScreens; size_t ccS = 0; std::vector<juce::Component::SafePointer<juce::Component>> ccKeys; size_t ccI = 0;
    int ccPressed = 0, ccBad = 0; arco::MainComponent* ccmc = nullptr;
    void clickCheck() {
        using arco::Screen;
        ccScreens = { Screen::Home, Screen::Workstation, Screen::Loops, Screen::Drums, Screen::Synth, Screen::Keys, Screen::Phrase, Screen::Studio, Screen::Workstation };
        arco::Theme::setPalette(6);
        ccmc = new arco::MainComponent(*host); ccmc->setVisible(true); ccmc->setSize(1440, 900);
        ccNextScreen();
    }
    void ccCollect(juce::Component* c) {
        if (!c->isVisible()) return;
        if (dynamic_cast<arco::Pill*>(c) || dynamic_cast<arco::Segments*>(c)) { ccKeys.push_back(c); return; }
        for (auto* ch : c->getChildren()) ccCollect(ch);
    }
    void ccNextScreen() {
        if (ccS >= ccScreens.size()) {
            printf("click check: %d keys pressed, %d problems\n", ccPressed, ccBad); fflush(stdout);
            delete ccmc; ccmc = nullptr; setApplicationReturnValue(ccBad ? 1 : 0); quit(); return; }
        auto sc = ccScreens[ccS++];
        ccmc->shell.show(sc);
        juce::Timer::callAfterDelay(250, [this, sc] {
            ccKeys.clear(); ccCollect(ccmc); ccI = 0;
            printf("  %s: %d keys\n", arco::screenTitle(sc), (int) ccKeys.size()); fflush(stdout);
            ccStep(sc); });
    }
    void ccStep(arco::Screen sc) {
        auto settle = [] { juce::PopupMenu::dismissAllActiveMenus(); if (auto* m = juce::ModalComponentManager::getInstance()) m->cancelAllModalComponents(); };
        while (ccI < ccKeys.size()) {
            auto* c = ccKeys[ccI++].getComponent();
            if (c == nullptr) continue;
            bool vis = true; for (auto* q = c; q != nullptr && q != ccmc; q = q->getParentComponent()) vis &= q->isVisible();
            if (!vis) continue;
            juce::String label = lcName(c);
            // not the ones that open the computer's file windows, write files or start the show recording
            bool skip = false;
            for (auto* w : { "Save", "SAVE", "Open", "OPEN", "Import", "IMPORT", "File", "FILE", "Export", "EXPORT", "Rec show", "MIDI learn", "CHOOSE", "Choose", "LOAD", "Load", "PLUG-IN", "SHARE", "Share" })
                if (label.contains(w)) skip = true;
            if (skip) continue;
            try {
                if (auto* p = dynamic_cast<arco::Pill*>(c)) { if (p->onClick) p->onClick(); }
                else if (auto* sg = dynamic_cast<arco::Segments*>(c)) { int was = sg->get ? sg->get() : 0; for (int k = 0; k < sg->items.size(); ++k) if (sg->set) sg->set(k); if (sg->set) sg->set(was); }
            } catch (...) { ++ccBad; printf("    THREW: %s\n", label.toRawUTF8()); }
            ++ccPressed;
            settle();
            // back to the screen being tested (a key may have opened another one)
            if (ccmc->shell.screen != sc) ccmc->shell.show(sc);
            juce::Timer::callAfterDelay(12, [this, sc] { ccStep(sc); });
            return;
        }
        host->model.press("st_looper", "stopall"); host->model.set("dm_run", 0); host->studio.stop();
        ccNextScreen();
    }
    void layoutCheck() {
        using arco::Screen;
        for (int pal : { 6, 7 }) {
            bool phoneOnly = juce::JUCEApplication::getCommandLineParameters().contains("pocket");
            std::vector<std::pair<int,int>> sizes = { { 1440, 880 }, { 1180, 820 }, { 1366, 1024 }, { 820, 1180 }, { 1024, 1366 } };
            if (phoneOnly) { arco::MainComponent::phoneSized = true; sizes = { { 750, 390 }, { 844, 390 }, { 667, 375 }, { 812, 375 } }; }
            for (auto wh : sizes) {
                for (int t : { 1, 4, 8 }) lcfg.push_back({ Screen::Workstation, wh.first, wh.second, t, pal, {} });
                for (auto sc : { Screen::Home, Screen::Loops, Screen::Studio, Screen::Drums, Screen::Synth, Screen::Phrase, Screen::Keys })
                    lcfg.push_back({ sc, wh.first, wh.second, 4, pal, {} });
            }
        }
        lcNext();
    }
    void lcNext() {
        if (lcmc) { delete lcmc; lcmc = nullptr; }
        if (lci >= lcfg.size()) { printf("layout check: %d problems\n", lcProblems); fflush(stdout); setApplicationReturnValue(lcProblems ? 1 : 0); quit(); return; }
        auto cf = lcfg[lci++];
        arco::Theme::setPalette(cf.pal);
        lcmc = new arco::MainComponent(*host);
        lcmc->setVisible(true);
        lcmc->setSize(cf.w, cf.h);
        lcmc->canvas.shotTracks = cf.tracks;
        lcmc->shell.show(cf.sc);
        if (cf.sc == arco::Screen::Synth) lcmc->canvas.openSynth(true);
        juce::Timer::callAfterDelay(250, [this, cf] {
            std::vector<juce::Component*> v; lcCollect(lcmc, v);
            if (lci <= 3 || lci % 20 == 0) { printf("  [%d controls] %s %dx%d\n", (int) v.size(), arco::screenTitle(cf.sc), cf.w, cf.h); }
            juce::String tag = juce::String(arco::screenTitle(cf.sc)) + " " + juce::String(cf.w) + "x" + juce::String(cf.h) + " tracks=" + juce::String(cf.tracks) + " pal=" + juce::String(cf.pal);
            auto rectOf = [this](juce::Component* c) { return lcmc->getLocalArea(c->getParentComponent(), c->getBounds()); };
            juce::StringArray seen;
            for (size_t i = 0; i < v.size(); ++i) {
                auto a = rectOf(v[i]);
                if (a.getWidth() < 4 || a.getHeight() < 4) continue;
                // sticking out of its parent panel (or off the screen)
                auto* par = v[i]->getParentComponent();
                if (par != nullptr && par != lcmc && !par->getLocalBounds().contains(v[i]->getBounds().reduced(1))) {
                    auto msg = tag + "  OUTSIDE  " + lcName(v[i]) + " " + v[i]->getBounds().toString() + " in " + par->getLocalBounds().toString();
                    if (!seen.contains(msg)) { seen.add(msg); printf("%s\n", msg.toRawUTF8()); ++lcProblems; } }
                {   // something else on top catches the touch
                    auto cp = a.getCentre();
                    if (lcmc->getLocalBounds().contains(cp)) {
                        auto* hit = lcmc->getComponentAt(cp);
                        bool mine = hit == v[i] || (hit && v[i]->isParentOf(hit));
                        if (!mine) { auto msg = tag + "  COVERED  " + lcName(v[i]) + " by " + (hit ? juce::String(typeid(*hit).name()) + " " + hit->getComponentID() : juce::String("nothing"));
                                     if (!seen.contains(msg)) { seen.add(msg); printf("%s\n", msg.toRawUTF8()); ++lcProblems; } }
                    }
                }
                if (auto* pl = dynamic_cast<arco::Pill*>(v[i]); pl != nullptr && arco::Theme::softUI()) {
                    // 2.3: the words of a key inside its key; a key's shade not cut square by a panel that does not draw it
                    auto t = pl->dynText ? pl->dynText() : pl->text; if (!pl->mono) t = arco::Theme::keyCase(t);
                    auto r = pl->getLocalBounds().toFloat();
                    auto face = pl->shadedByParent() ? r : r.reduced(juce::jmin(5.0f, r.getHeight() * 0.12f));
                    float need = juce::GlyphArrangement::getStringWidth(pl->mono ? arco::Theme::mono(pl->fsize) : arco::Theme::font(pl->fsize, pl->weight), t) + (pl->icon ? face.getHeight() * 0.5f : 0.0f);
                    float room = face.getWidth() - face.getHeight() * 0.45f;           // the round ends
                    if (t.isNotEmpty() && pl->bg && pl->bg().getAlpha() > 0 && need > room) {
                        auto msg = tag + "  TEXTOUT  " + lcName(v[i]) + " needs " + juce::String((int) need) + " has " + juce::String((int) room);
                        if (!seen.contains(msg)) { seen.add(msg); printf("%s\n", msg.toRawUTF8()); ++lcProblems; } }
                    if (!pl->shadedByParent() && pl->raisedNow()) {
                        auto msg = tag + "  SHADECUT  " + lcName(v[i]) + " in " + juce::String(typeid(*pl->getParentComponent()).name());
                        if (!seen.contains(msg)) { seen.add(msg); printf("%s\n", msg.toRawUTF8()); ++lcProblems; } }
                }
                if (v[i]->findParentComponentOfClass<juce::Viewport>() == nullptr && !lcmc->getLocalBounds().contains(a.reduced(1))) {
                    auto msg = tag + "  OFFSCREEN  " + lcName(v[i]) + " " + a.toString();
                    if (!seen.contains(msg)) { seen.add(msg); printf("%s\n", msg.toRawUTF8()); ++lcProblems; } }
                for (size_t j = i + 1; j < v.size(); ++j) {
                    auto b = rectOf(v[j]); if (b.getWidth() < 4 || b.getHeight() < 4) continue;
                    auto x = a.getIntersection(b);
                    if (x.getWidth() > 2 && x.getHeight() > 2) {
                        // a control lying inside a bigger one on purpose (the head pill of a strip) is not an overlap
                        auto* pa = dynamic_cast<arco::Pill*>(v[i]); auto* pb = dynamic_cast<arco::Pill*>(v[j]);
                        bool headA = pa && pa->getComponentID().endsWith(":select"), headB = pb && pb->getComponentID().endsWith(":select");
                        if (headA || headB) continue;
                        auto msg = tag + "  OVERLAP  " + lcName(v[i]) + " " + a.toString() + "  x  " + lcName(v[j]) + " " + b.toString();
                        if (!seen.contains(msg)) { seen.add(msg); printf("%s\n", msg.toRawUTF8()); ++lcProblems; }
                    }
                }
            }
            fflush(stdout);
            lcNext();
        });
    }

    // for testing: render the interface to a PNG and quit (ARCO --screenshot out.png [light])
    void screenshot(const juce::String& cmd) {
        auto args = juce::StringArray::fromTokens(cmd, true);
        juce::File out(args[args.indexOf("--screenshot") + 1]);
        arco::Theme::setPalette(cmd.contains("light") ? 7 : 6);
        if (cmd.contains("palette=")) arco::Theme::setPalette(cmd.fromFirstOccurrenceOf("palette=", false, false).getIntValue());
        if (cmd.contains("pocket")) arco::MainComponent::phoneSized = true;
        auto* mc = new arco::MainComponent(*host);
        mc->setSize(1440, 880);
        if (cmd.contains("portrait")) mc->setSize(820, 1180);               // an iPad held upright
        if (cmd.contains("size=")) { auto sz = cmd.fromFirstOccurrenceOf("size=", false, false).upToFirstOccurrenceOf(" ", false, false);
            mc->setSize(sz.upToFirstOccurrenceOf("x", false, false).getIntValue(), sz.fromFirstOccurrenceOf("x", false, false).getIntValue()); }
        {   // 2.0: which screen of the shell (screen=home|workstation|loops|studio|drums|synth|keys|phrase|modular|speakers)
            using arco::Screen; Screen sc = Screen::Home;
            for (auto w : { "setup", "look", "keyboard", "ksearch=", "outputs", "modular", "spatial", "automix", "export", "help", "about", "learn", "import=", "amdemo", "fam=", "allfx", "newfx", "fx14", "fewfx", "tracks=", "lives=" })
                if (cmd.contains(w)) sc = Screen::Workstation;
            if (cmd.contains("synth")) sc = Screen::Synth;
            auto nm = cmd.fromFirstOccurrenceOf("screen=", false, false).upToFirstOccurrenceOf(" ", false, false);
            const std::pair<const char*, Screen> S[] = { { "home", Screen::Home }, { "workstation", Screen::Workstation }, { "loops", Screen::Loops }, { "studio", Screen::Studio },
                { "drums", Screen::Drums }, { "synth", Screen::Synth }, { "keys", Screen::Keys }, { "phrase", Screen::Phrase }, { "modular", Screen::Modular }, { "speakers", Screen::Speakers }, { "patcher", Screen::Patcher } };
            for (auto& p : S) if (nm == p.first) sc = p.second;
            mc->shell.show(sc);
            if (sc == Screen::Patcher) mc->shell.patcher->demo(cmd);
            if (cmd.contains("demo2")) demo2(mc, sc);
            if (cmd.contains("fxbrowse=")) if (auto* sv = mc->shell.studioView()) sv->showEffects(cmd.fromFirstOccurrenceOf("fxbrowse=", false, false).getIntValue());
            if (cmd.contains("meter")) { mc->shell.setMeter(7, 8); mc->shell.chooseMeter(&mc->shell, { 960, 10, 160, 60 }); }
        }
        if (cmd.contains("bench")) {
            mc->setVisible(true);
            auto frame = [&] { juce::Image im(juce::Image::ARGB, mc->getWidth(), mc->getHeight(), true); juce::Graphics g(im); mc->paintEntireComponent(g, false); };
            auto t0 = juce::Time::getMillisecondCounterHiRes(); frame(); auto t1 = juce::Time::getMillisecondCounterHiRes();
            for (int k = 0; k < 10; ++k) frame();
            std::printf("bench: first frame %.1f ms, then %.1f ms per full frame\n", t1 - t0, (juce::Time::getMillisecondCounterHiRes() - t1) / 10.0); std::fflush(stdout);
        }
        if (cmd.contains("setup")) { mc->canvas.openSetup(true);
            if (cmd.contains("ssec=")) for (auto* ch : mc->canvas.getChildren()) if (auto* sp = dynamic_cast<arco::SetupPanel*>(ch)) sp->showSection(cmd.fromFirstOccurrenceOf("ssec=", false, false).getIntValue()); }
        if (cmd.contains("synth")) { mc->canvas.openSynth(true); if (cmd.contains("spage=")) mc->canvas.synthPage(cmd.fromFirstOccurrenceOf("spage=", false, false).getIntValue());
            if (cmd.contains("skeys=")) mc->canvas.synthKeys(cmd.fromFirstOccurrenceOf("skeys=", false, false).getIntValue(), cmd.contains("sedit")); }
        if (cmd.contains("look")) mc->canvas.openLook(true);
        if (cmd.contains("kind=")) host->rack.kind.store(cmd.fromFirstOccurrenceOf("kind=", false, false).getIntValue());
        if (cmd.contains("keyboard")) mc->canvas.openKeyboard(true);
        if (cmd.contains("ksearch=")) mc->canvas.keyboardSearch(cmd.fromFirstOccurrenceOf("ksearch=", false, false).upToFirstOccurrenceOf(" ", false, false).replace("+", " "));
        if (cmd.contains("outputs")) { host->engine.grid[0][2].store(1); host->engine.grid[4][3].store(1); host->engine.grid[5][4].store(1); mc->canvas.openOutputs(); }
        if (cmd.contains("modular")) { if (cmd.contains("patch=")) host->rack.modular.setPatch(arco::Modular::defaultPatch(cmd.fromFirstOccurrenceOf("patch=", false, false).getIntValue())); mc->canvas.openModular(true);
            if (cmd.contains("mrun")) { std::vector<float> l(256), r(256); for (int k = 0; k < 48000 * 6 / 256; ++k) { std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f); host->rack.modular.process(l.data(), r.data(), 256, 120, k * 256 / 48000.0 * 2.0); } }
            for (auto* ch : mc->canvas.getChildren()) if (auto* mp = dynamic_cast<arco::ModularPanel*>(ch)) {
                if (cmd.contains("mguide")) mp->showGuide(true);
                mp->demoState(cmd); } }
        if (cmd.contains("spatial")) { auto& m = host->model; m.set("amb_on", 1); for (int s : { 4, 5, 7 }) m.set("amb" + std::to_string(s), 1); m.set("amb_mot5", 1); mc->canvas.openSpatial(true); }
        if (cmd.contains("lives=")) host->nLive = cmd.fromFirstOccurrenceOf("lives=", false, false).getIntValue();
        if (cmd.contains("tracks=")) mc->canvas.shotTracks = cmd.fromFirstOccurrenceOf("tracks=", false, false).getIntValue();
        if (cmd.contains("fam=")) mc->canvas.showFamily(cmd.fromFirstOccurrenceOf("fam=", false, false).getIntValue());
        if (cmd.contains("card=")) { auto k = cmd.fromFirstOccurrenceOf("card=", false, false).upToFirstOccurrenceOf(" ", false, false); auto& ct = arco::cardTable();
            if (!host->visibleCards.contains(k)) host->visibleCards.add(k);
            for (int i = 0; i < (int) ct.size(); ++i) if (k == ct[(size_t) i].key) mc->canvas.showCard(i);
            if (k == "beq") { auto& m = host->model; int st = m.selected; auto sid = [st](const char* f) { return std::string("mx_") + f + std::to_string(st); };
                m.set(sid("hp"), 60); m.set(sid("lsg"), -3); m.set(sid("lsf"), 180); m.set(sid("pkg"), 4.5); m.set(sid("pkf"), 900); m.set(sid("pkq"), 1.4);
                m.set(sid("p2g"), -5); m.set(sid("p2f"), 3200); m.set(sid("p2q"), 3); m.set(sid("hsg"), 3); m.set(sid("hsf"), 9000); } }
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

    // 2.0 screenshots: something on every new screen (a groove, a recorded phrase, loops in the grid, a song in STUDIO)
    void demo2(arco::MainComponent* mc, arco::Screen sc) {
        auto& h = *host; auto& e = h.engine; auto& m = h.model;
        for (int k = 0; k < 300 && e.drumMachine.bank.load() == nullptr; ++k) juce::Thread::sleep(20);
        const int N = 512; std::vector<float> in1(N, 0.0f), o[34]; for (auto& v : o) v.assign(N, 0.0f);
        float* outs[34]; for (int i = 0; i < 34; ++i) outs[i] = o[i].data();
        const float* ins[4] = { in1.data(), nullptr, nullptr, nullptr };
        e.inputChannel[0].store(0);
        auto runFor = [&](double sec, std::function<float(double)> sig) { double t = 0;
            for (int b = 0; b < (int) (sec * e.sr / N); ++b) { for (int i = 0; i < N; ++i) { in1[(size_t) i] = sig ? sig(t) : 0.0f; t += 1.0 / e.sr; } e.process(ins, 4, outs, 34, N); m.tick(); } };
        m.set("bpm", 96);
        // a groove on the drum machine
        auto& D = e.drumMachine; using St = arco::DrumMachine::Step;
        for (int s : { 0, 6, 8, 11 }) D.setStep(0, 0, s, St { s == 0 ? 120 : 100, 100, 1, 0 });
        for (int s : { 4, 12 }) D.setStep(0, 1, s, St { 118, 100, 1, 0 });
        D.setStep(0, 1, 15, St { 60, 60, 2, 0 });
        for (int s = 0; s < 16; s += 2) D.setStep(0, 4, s, St { s % 4 == 0 ? 100 : 72, s == 14 ? 70 : 100, 1, 0 });
        D.setStep(0, 6, 14, St { 90, 100, 1, 0 }); D.setStep(0, 9, 0, St { 80, 100, 1, 0 }); D.setStep(0, 3, 12, St { 70, 100, 1, 0 });
        for (int s : { 0, 3, 8, 10 }) D.setStep(1, 0, s, St { 110, 100, 1, 0 });
        for (int s : { 4, 12 }) D.setStep(1, 1, s, St { 115, 100, 1, 0 });
        m.set("dm_swing", 56); m.set("dm_kit", 0);
        if (sc == arco::Screen::Drums) { m.set("dm_run", 1); runFor(1.37, nullptr); }
        if (sc == arco::Screen::Phrase) {        // four notes played into LIVE 1
            m.set("phr_rate", 3); m.set("phr_order", 2); m.set("phr_oct", 1); m.set("phr_src", 1);
            m.press("st_phr", "phr_rec");
            const double F[4] = { 196, 246.9, 293.7, 392 }; double t0 = -1, ph = 0;
            runFor(6.0, [&](double) { int st = e.phrase.stateOut.load(); if (st == 2 && t0 < 0) t0 = 0; if (t0 < 0) return 0.0f;
                double tt = t0; t0 += 1.0 / e.sr; int note = (int) (tt / 0.625); if (note > 3) return 0.0f; double inN = tt - note * 0.625;
                ph += 2 * M_PI * F[note] / e.sr; return (float) (0.45 * std::exp(-inN * 3.0) * std::min(1.0, inN / 0.004) * (std::sin(ph) + 0.3 * std::sin(2 * ph) + 0.1 * std::sin(3 * ph))); });
            m.press("st_phr", "phr_bgo"); runFor(1.1, nullptr);
        }
        if (sc == arco::Screen::Loops) {         // loops in the grid, some playing
            auto mk = [&](int row, int col, double f, double bars, int kind) {
                auto c = std::make_shared<arco::LoopClip>(); double bl = 60.0 / 96.0 * 4 * e.sr; c->len = (long) (bl * bars); c->beats = bars * 4;
                c->l.resize((size_t) c->len); c->r.resize((size_t) c->len); double ph = 0;
                for (long i = 0; i < c->len; ++i) { double t = i / e.sr, beat = std::fmod(t * 96.0 / 60.0, 1.0);
                    double env = kind == 0 ? std::exp(-beat * 6.0) : (kind == 1 ? 0.6 + 0.4 * std::sin(t * 0.7) : std::exp(-std::fmod(t * 96.0 / 30.0, 1.0) * 3.0));
                    ph += 2 * M_PI * f * (1 + 0.002 * std::sin(t * 5)) / e.sr; float v = (float) (0.5 * env * std::sin(ph)); c->l[(size_t) i] = v; c->r[(size_t) i] = v; }
                mc->shell.loops->setClip(row, col, c); };
            mk(0, 0, 110, 2, 0); mk(0, 1, 98, 2, 0); mk(0, 2, 123, 4, 0); mk(1, 0, 220, 2, 1); mk(1, 1, 247, 2, 1); mk(1, 3, 196, 4, 1);
            mk(2, 1, 440, 1, 2); mk(2, 2, 523, 2, 2); mk(4, 0, 60, 1, 0); mk(4, 1, 60, 1, 0); mk(4, 2, 65, 2, 0); mk(5, 2, 330, 4, 1);
            mc->shell.loops->names[0][0] = "BASS A"; mc->shell.loops->names[1][0] = "CELLO PAD"; mc->shell.loops->names[4][0] = "DRUMS";
            auto& L = e.loopGrid; L.launch(0, 1); L.launch(1, 1); L.launch(2, 1); L.launch(4, 1); runFor(2.6, nullptr); L.launch(0, 2);
            L.record(3, 2); runFor(2.6, nullptr);
            mc->shell.loops->selRow = 3; mc->shell.loops->selCol = 2;
        }
        if (sc == arco::Screen::Studio) {
            auto& S = h.studio; const juce::ScopedLock sl(S.lock); S.song = arco::StudioSong(); S.song.sr = e.sr; S.song.bpm = 96;
            const char* nm[4] = { "CELLO", "PIANO", "DRUMS", "VOICE" };
            for (int t = 0; t < 4; ++t) {
                arco::StudioLane L; L.name = nm[t]; L.colour = 4 + t; L.vol = -2.0f * t; S.song.lanes.push_back(L);
                std::vector<float> l((size_t) (e.sr * 10));
                for (size_t i = 0; i < l.size(); ++i) { double tt = i / e.sr; double env = std::exp(-std::fmod(tt, 0.625 * (t + 1)) * 3.0); l[i] = (float) (0.6 * env * std::sin(2 * M_PI * (110.0 * (t + 1)) * tt)); }
                S.song.sources.push_back(S.makeSource(nm[t], std::move(l)));
                arco::StudioClip c; c.lane = t; c.source = t; c.start = (long) (t == 3 ? e.sr * 10 : 0); c.length = (long) (e.sr * (t == 3 ? 20 : 40)); c.fadeIn = 2000; S.song.clips.push_back(c);
            }
            S.ensureReturns(); S.pos = (long) (e.sr * 12);
        }
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
