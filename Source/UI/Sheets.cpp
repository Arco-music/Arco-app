#include "Sheets.h"
#include "MainView.h"
#include "HelpText.h"
#include "../App/Updater.h"

namespace arco {

// ================================================================== Sheet
void Sheet::text(juce::Graphics& g, const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt, int lines) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, wt));
    g.drawFittedText(t, { box.getX() + x, box.getY() + y, w, (int) (fs * 1.6f * lines) + 4 }, juce::Justification::topLeft, lines, 0.9f);
}
void Sheet::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.25f : 0.55f));
    g.setColour(Theme::bg()); g.fillRoundedRectangle(box.toFloat(), 16.0f);
    g.setColour(Theme::line()); g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 16.0f, 1.0f);
    text(g, title, 24, 16, 600, 17.0f, Theme::txt(), 2, 1);
    if (subtitle.isNotEmpty()) text(g, subtitle, 24, 46, box.getWidth() - 160, 8.5f, Theme::sub(), 0, 2);
    paintContent(g);
}
void Sheet::choices(int x, int y, const juce::StringArray& names, int w, std::function<int()> get, std::function<void(int)> set) {
    for (int i = 0; i < names.size(); ++i) {
        auto* p = new Pill(names[i], [get, i] { return get() == i ? Theme::sel() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [this, set, i] { set(i); repaint(); };
        kids.add(p); addAndMakeVisible(p); p->setBounds(box.getX() + x + i * (w + 6), box.getY() + y, w, 26);
    }
}

// ================================================================== EXPORT
ExportSheet::ExportSheet(Canvas& c) : Sheet(c, "EXPORT", { 200, 50, 1040, 780 }), juce::Thread("ARCO export") {
    subtitle = juce::String::fromUTF8("the song rendered at full quality, faster than real time - measured, levelled for where it is going, never clipping");
    int y = 100, L = 200;
    choices(L, y, { "THIS SCENE", juce::String::fromUTF8("THE WHOLE SONG  (A → D)") }, 220, [this] { return opt.what; }, [this](int i) { opt.what = i; });
    y += 62;
    choices(L, y, { "1", "2", "4", "8", "16" }, 56, [this] { int r = opt.repeats; return r == 1 ? 0 : r == 2 ? 1 : r == 4 ? 2 : r == 8 ? 3 : 4; },
            [this](int i) { static const int R[5] = { 1, 2, 4, 8, 16 }; opt.repeats = R[i]; });
    y += 62;
    choices(L, y, { "NONE", "2 s", "4 s", "8 s" }, 70, [this] { double t = opt.tailSeconds; return t < 1 ? 0 : t < 3 ? 1 : t < 6 ? 2 : 3; },
            [this](int i) { static const double T[4] = { 0, 2, 4, 8 }; opt.tailSeconds = T[i]; });
    y += 62;
    choices(L, y, { "WAV 16-bit", "WAV 24-bit", "WAV 32-bit float", "FLAC 16-bit", "FLAC 24-bit" }, 136, [this] { return fileChoice; },
            [this](int i) { fileChoice = i; static const int B[5] = { 16, 24, 32, 16, 24 }; opt.bits = B[i]; opt.flac = i >= 3; });
    y += 62;
    choices(L, y, { "44.1 kHz", "48 kHz", "96 kHz" }, 100, [this] { return rateChoice; },
            [this](int i) { rateChoice = i; static const double S[3] = { 44100, 48000, 96000 }; opt.sampleRate = S[i]; });
    y += 62;
    choices(L, y, { "AS IS", juce::String::fromUTF8("STREAMING  −14 LUFS"), juce::String::fromUTF8("LOUD  −10 LUFS"), juce::String::fromUTF8("BROADCAST  −23 LUFS") }, 176,
            [this] { return opt.loudness; }, [this](int i) { opt.loudness = i; });
    y += 62;
    choices(L, y, { "MIX ONLY", "+ STEMS  (AS RECORDED)", "+ STEMS  (WITH EFFECTS)" }, 216, [this] { return stemChoice; },
            [this](int i) { stemChoice = i; opt.stems = i > 0; opt.stemsDry = i == 1; });
    opt.bits = 24; opt.sampleRate = 48000;
    addAndMakeVisible(go); go.setBounds(box.getRight() - 224, box.getBottom() - 70, 200, 46);
    go.onClick = [this] { start(); };
    addChildComponent(share); share.setBounds(box.getRight() - 340, box.getBottom() - 64, 104, 34);
    share.onClick = [this] {
        if (written.isEmpty()) return;
       #if JUCE_IOS
        juce::Array<juce::URL> urls; for (auto& f : written) urls.add(juce::URL(f));
        juce::ContentSharer::shareFilesScoped(urls, [](bool, const juce::String&) {});
       #else
        written[0].revealToUser();
       #endif
    };
   #if JUCE_IOS
    share.text = "SHARE";
   #endif
    startTimerHz(15);
}
ExportSheet::~ExportSheet() { stopThread(5000); }

void ExportSheet::paintContent(juce::Graphics& g) {
    const char* rows[7] = { "WHAT", "LOOPS", "ECHO TAIL", "FILE", "SAMPLE RATE", "LEVEL", "STEMS" };
    const char* help[7] = { "the scene you are on, or every scene that has loops, one after the other",
                            "how many times each scene plays",
                            "after the last loop the effects ring out",
                            "WAV for everything; FLAC is the same quality, smaller; 32-bit float for further mixing",
                            "48 kHz for video, 44.1 kHz for CD, 96 kHz for archives",
                            "streaming: Spotify, Apple Music, YouTube level. The true peak always stays below -1 dB",
                            "every track as its own file too, in a folder next to the mix (for STUDIO or any other program)" };
    for (int r = 0; r < 7; ++r) {
        text(g, rows[r], 24, 104 + r * 62, 170, 9.5f, Theme::txt(), 2, 1);
        text(g, juce::String::fromUTF8(help[r]), 200, 132 + r * 62, 800, 7.5f, Theme::sub(), 0, 1);
    }
    auto bar = juce::Rectangle<float>((float) box.getX() + 24, (float) box.getBottom() - 116, (float) box.getWidth() - 48, 8);
    if (busy || progress > 0) {
        g.setColour(Theme::card2()); g.fillRoundedRectangle(bar, 4);
        g.setColour(Theme::green()); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())), 4);
    }
    juce::String st = busy ? juce::String::fromUTF8("rendering … ") + juce::String((int) (progress * 100)) + " %" : (error.isNotEmpty() ? error : report);
    text(g, st, 24, box.getHeight() - 96, box.getWidth() - 400, 8.5f, error.isNotEmpty() ? Theme::red() : Theme::txt(), 1, 3);
    if (cv.engine().snap("smask").v.load() < 0.5f) text(g, "Record some loops first - there is nothing to export yet.", 24, box.getHeight() - 60, 600, 9.0f, Theme::amber(), 1, 1);
}

void ExportSheet::start() {
    if (busy) return;
    if (cv.engine().snap("smask").v.load() < 0.5f) { error = "nothing recorded yet"; repaint(); return; }
    juce::String name = cv.host.songFolder.isDirectory() ? cv.host.songFolder.getFileName() : juce::String("ARCO mix");
    juce::String ext = opt.flac ? ".flac" : ".wav";
    auto begin = [this](juce::File f) {
        target = f; error = {}; report = {}; written.clear(); share.setVisible(false);
        snap = Renderer(cv.host).snapshot();
        busy = true; progress = 0.001f;
        startThread();
    };
   #if JUCE_IOS
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Exports"); d.createDirectory();
    begin(d.getChildFile(name + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M") + ext));
   #else
    chooser = std::make_unique<juce::FileChooser>("Export the mix as", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(name + ext), "*" + ext);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting | juce::FileBrowserComponent::canSelectFiles,
        [this, ext, begin](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) begin(f.withFileExtension(ext.substring(1))); });
   #endif
}

void ExportSheet::run() {
    Renderer rd(cv.host);
    juce::AudioBuffer<float> mix; double rate = 0;
    int nStems = 0;
    juce::Array<int> stemStrips;
    if (opt.stems && !opt.stemsDry) {
        int mask = 0;
        for (int s = 1; s <= 4; ++s) if (opt.what == ExportOptions::AllScenes || s == snap.sceneNow) mask |= (int) snap.sv[6 + s - 1];
        for (int t = 0; t < 8; ++t) if ((mask >> t) & 1) stemStrips.add(4 + t);
        nStems = stemStrips.size();
    }
    float share1 = 1.0f / (1 + nStems);
    bool ok = rd.renderMix(snap, opt, mix, rate, -1, [this, share1](float p) { progress = p * share1; return !threadShouldExit(); });
    if (!ok || threadShouldExit()) { error = "the export was cancelled or there was nothing to render"; return; }
    juce::String rep;
    if (!Renderer::write(mix, rate, opt, target, rep)) { error = rep; return; }
    written.add(target);
    juce::String out = juce::String::fromUTF8("✓ ") + target.getFileName() + "   " + rep;
    if (opt.stems) {
        auto dir = target.getParentDirectory().getChildFile(target.getFileNameWithoutExtension() + " stems");
        dir.createDirectory();
        ExportOptions so = opt; so.loudness = 0;                // stems keep their balance: no levelling
        int count = 0;
        if (opt.stemsDry) {
            juce::OwnedArray<juce::AudioBuffer<float>> st; juce::StringArray nm; double r2 = 0;
            for (int s = 1; s <= 4; ++s) {
                if (!(opt.what == ExportOptions::AllScenes || s == snap.sceneNow)) continue;
                st.clear(); nm.clear();
                if (!rd.dryStems(s, st, nm, r2)) continue;
                for (int i = 0; i < st.size(); ++i) {
                    juce::String r3; auto f = dir.getChildFile(juce::String::charToString((juce::juce_wchar) ('A' + s - 1)) + " - " + nm[i] + (opt.flac ? ".flac" : ".wav"));
                    if (Renderer::write(*st[i], r2, so, f, r3, false)) { written.add(f); ++count; }
                }
            }
        } else {
            for (int k = 0; k < stemStrips.size(); ++k) {
                juce::AudioBuffer<float> sb; double r2 = 0;
                float base = share1 * (1 + k);
                if (!rd.renderMix(snap, opt, sb, r2, stemStrips[k], [this, base, share1](float p) { progress = base + p * share1; return !threadShouldExit(); })) continue;
                juce::String r3; auto f = dir.getChildFile("Track " + juce::String(stemStrips[k] - 3) + (opt.flac ? ".flac" : ".wav"));
                if (Renderer::write(sb, r2, so, f, r3, false)) { written.add(f); ++count; }
            }
        }
        out << "\n+ " << count << " stems in \"" << dir.getFileName() << "\"";
    }
    progress = 1.0f;
    report = out;
}

void ExportSheet::finished() {
    busy = false;
    share.setVisible(!written.isEmpty());
    repaint();
}

// ================================================================== HELP
HelpSheet::HelpSheet(Canvas& c) : Sheet(c, "HELP", { 120, 40, 1200, 800 }) {
    subtitle = "everything ARCO does, in short";
    auto& S = helpSections();
    for (int i = 0; i < (int) S.size(); ++i) {
        auto* p = new Pill(juce::String::fromUTF8(S[(size_t) i].title), [this, i] { return current == i ? Theme::sel() : Theme::card(); }, 8.5f, 6.0f);
        p->onClick = [this, i] { show(i); };
        tabs.add(p); addAndMakeVisible(p);
    }
    view.setViewedComponent(&page, false);
    view.setScrollBarsShown(true, false);
    addAndMakeVisible(view);
    show(0);
}
void HelpSheet::resized() {
    Sheet::resized();
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setBounds(box.getX() + 24, box.getY() + 86 + i * 34, 220, 28);
    view.setBounds(box.getX() + 268, box.getY() + 86, box.getWidth() - 292, box.getHeight() - 110);
    page.setSize(view.getWidth() - 14, page.heightFor(view.getWidth() - 14));
}
void HelpSheet::show(int s) {
    auto& S = helpSections();
    current = juce::jlimit(0, (int) S.size() - 1, s);
    page.body = juce::String::fromUTF8(S[(size_t) current].body);
    page.setSize(juce::jmax(100, view.getWidth() - 14), page.heightFor(juce::jmax(100, view.getWidth() - 14)));
    view.setViewPosition(0, 0);
    repaint();
}
void HelpSheet::paintContent(juce::Graphics& g) {
    g.setColour(Theme::card()); g.fillRoundedRectangle(view.getBounds().toFloat().expanded(8), 10.0f);
}
static juce::AttributedString helpLayout(const juce::String& body) {
    juce::AttributedString a; a.setWordWrap(juce::AttributedString::byWord); a.setLineSpacing(4.0f);
    for (auto& line : juce::StringArray::fromLines(body)) {
        if (line.startsWith("# ")) a.append(line.substring(2) + "\n", Theme::font(13.0f, 2), Theme::ink(Theme::txt()));
        else if (line.startsWith("## ")) a.append("\n" + line.substring(3) + "\n", Theme::font(10.5f, 2), Theme::ink(Theme::txt()));
        else if (line.startsWith("- ")) a.append(juce::String::fromUTF8("•  ") + line.substring(2) + "\n", Theme::font(9.5f, 0), Theme::ink(Theme::txt()));
        else if (line.trim().isEmpty()) a.append("\n", Theme::font(6.0f, 0), Theme::txt());
        else a.append(line + "\n", Theme::font(9.5f, 0), Theme::ink(Theme::sub()));
    }
    return a;
}
int HelpSheet::Page::heightFor(int w) {
    juce::TextLayout tl; tl.createLayout(helpLayout(body), (float) w - 24);
    return (int) tl.getHeight() + 40;
}
void HelpSheet::Page::paint(juce::Graphics& g) {
    juce::TextLayout tl; tl.createLayout(helpLayout(body), (float) getWidth() - 24);
    tl.draw(g, getLocalBounds().toFloat().reduced(12, 14));
}

// ================================================================== ABOUT
AboutSheet::AboutSheet(Canvas& c) : Sheet(c, "ABOUT", { 420, 170, 600, 520 }) {}
void AboutSheet::paintContent(juce::Graphics& g) {
    auto img = juce::ImageCache::getFromMemory(BinaryData::icon_png, BinaryData::icon_pngSize);
    g.drawImage(img, juce::Rectangle<float>((float) box.getCentreX() - 80, (float) box.getY() + 70, 160, 160));
    auto c = [&](const juce::String& t, int y, float fs, juce::Colour col, int wt) {
        g.setColour(Theme::ink(col)); g.setFont(Theme::font(fs, wt));
        g.drawFittedText(t, { box.getX() + 20, box.getY() + y, box.getWidth() - 40, (int) (fs * 2.4f) }, juce::Justification::centred, 2); };
    c("ARCO", 250, 22.0f, Theme::txt(), 2);
    c("version " + juce::String(ProjectInfo::versionString) + (Updater::build() > 0 ? juce::String::fromUTF8(" · build ") + juce::String(Updater::build()) : juce::String()), 290, 9.5f, Theme::sub(), 0);
    c(juce::String::fromUTF8("a live looping station for instruments - loops, scenes, effects, a synth, a studio"), 330, 9.5f, Theme::txt(), 0);
    c(juce::String::fromUTF8("made for Paulo da Cunha Andrade"), 372, 9.0f, Theme::sub(), 1);
    c(juce::String::fromUTF8("built with JUCE · Inter typeface · Signalsmith Stretch"), 440, 8.0f, Theme::sub(), 0);
}

// ================================================================== MENUS
static juce::PopupMenu::Item item(int id, const juce::String& t, const juce::String& key = {}, bool on = true, bool ticked = false) {
    juce::PopupMenu::Item i(t); i.itemID = id; i.isEnabled = on; i.isTicked = ticked;
    if (key.isNotEmpty()) i.shortcutKeyDescription = key;
    return i;
}
#if JUCE_MAC
 #define CMDKEY juce::String::fromUTF8("⌘")
#else
 #define CMDKEY juce::String("Ctrl+")
#endif

juce::PopupMenu AppMenu::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu m;
    auto& h = cv.host;
    bool loops = cv.engine().snap("smask").v.load() > 0.5f;
    if (index == 0) {
        m.addItem(item(cmdNewSong, "New Song", CMDKEY + "N"));
        m.addItem(item(cmdOpen, juce::String::fromUTF8("Open Song…"), CMDKEY + "O"));
        juce::PopupMenu rec;
        for (int i = 0; i < h.recentSongs.size(); ++i) rec.addItem(item(cmdRecent + i, juce::File(h.recentSongs[i]).getFileName()));
        m.addSubMenu("Open Recent", rec, h.recentSongs.size() > 0);
        m.addSeparator();
        m.addItem(item(cmdSave, "Save", CMDKEY + "S"));
        m.addItem(item(cmdSaveAs, juce::String::fromUTF8("Save As…"), juce::String::fromUTF8("⇧") + CMDKEY + "S"));
        m.addSeparator();
        juce::PopupMenu imp; for (int t = 0; t < 8; ++t) imp.addItem(item(cmdImport1 + t, "into Track " + juce::String(t + 1)));
        m.addSubMenu("Import Audio File", imp);
        m.addItem(item(cmdExport, juce::String::fromUTF8("Export Mix & Stems…"), CMDKEY + "E", loops));
        m.addItem(item(cmdRecordShow, h.isRecording() ? "Stop Recording the Show" : "Record the Show", CMDKEY + "R"));
        m.addSeparator();
        m.addItem(item(cmdStudio, juce::String::fromUTF8("Open in STUDIO…"), CMDKEY + "T", loops));
    } else if (index == 1) {
        m.addItem(item(cmdUndo, "Undo Last Recording", CMDKEY + "Z", cv.snap("undoav") > 0.5f));
        m.addItem(item(cmdResetAll, juce::String::fromUTF8("Clear All Loops…"), {}, loops));
        m.addSeparator();
        m.addItem(item(cmdAutoMix, juce::String::fromUTF8("AUTO MIX…"), CMDKEY + "M", loops));
        m.addItem(item(cmdAB, "Compare Before / After AUTO MIX", {}, cv.autoMixAvailable()));
        m.addSeparator();
        m.addItem(item(cmdMidiLearn, cv.midiLearnActive() ? "Stop MIDI Learn" : juce::String::fromUTF8("MIDI Learn…"), CMDKEY + "L", true, cv.midiLearnActive()));
        m.addItem(item(cmdMidiClear, "Forget All MIDI Connections"));
    } else if (index == 2) {
        auto& P = Theme::palettes();
        juce::PopupMenu pal;
        for (int i = 0; i < (int) P.size(); ++i) pal.addItem(item(cmdPalette + i, juce::String(P[(size_t) i].name) + (P[(size_t) i].light ? " (light)" : " (dark)"), {}, true, i == Theme::paletteIndex()));
        m.addSubMenu("Colours", pal);
        m.addItem(item(cmdToggleLight, Theme::lightMode() ? "Dark Mode" : "Light Mode", juce::String::fromUTF8("⇧") + CMDKEY + "L"));
        m.addItem(item(cmdLook, juce::String::fromUTF8("Choose Effects…")));
        m.addSeparator();
        m.addItem(item(cmdSynth, "SYNTH", "K"));
        m.addItem(item(cmdMicView, "MIC FOR MAC (this device as a microphone)"));
        m.addItem(item(cmdSetup, juce::String::fromUTF8("Settings…"), CMDKEY + ","));
       #if ! JUCE_IOS
        m.addSeparator();
        m.addItem(item(cmdFullScreen, "Full Screen", juce::String::fromUTF8("⌃") + CMDKEY + "F"));
        m.addItem(item(cmdMinimise, "Minimise", CMDKEY + "M"));
       #endif
    } else {
        m.addItem(item(cmdHelp, "ARCO Help", "F1"));
        m.addItem(item(cmdShortcuts, "Keys, Pedals & MIDI"));
        m.addItem(item(cmdWhatsNew, "What's New in 1.2"));
        m.addSeparator();
       #if ! JUCE_IOS
        m.addItem(item(cmdUpdates, juce::String::fromUTF8("Check for Updates…")));
       #endif
        m.addItem(item(cmdAbout, "About ARCO"));
    }
    return m;
}

juce::PopupMenu AppMenu::everything() {
    juce::PopupMenu m; auto names = getMenuBarNames();
    for (int i = 0; i < names.size(); ++i) m.addSubMenu(names[i], getMenuForIndex(i, names[i]));
    return m;
}

juce::PopupMenu AppMenu::appleExtras() {
    juce::PopupMenu m;
    m.addItem(item(cmdAbout, "About ARCO"));
    m.addItem(item(cmdUpdates, juce::String::fromUTF8("Check for Updates…")));
    m.addSeparator();
    m.addItem(item(cmdSetup, juce::String::fromUTF8("Settings…"), CMDKEY + ","));
    return m;
}

void AppMenu::menuItemSelected(int id, int) { cv.command(id); }

} // namespace arco
