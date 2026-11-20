#include "InstrumentPanels.h"
#include "MainView.h"
#include "../App/AudioHost.h"
#include "../Engine/Modular.h"
#include "PlaySurfaces.h"
#include "SynthPanel.h"
#include "../App/InstrumentIndex.h"

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static void lab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.85f);
}
static juce::String mb(int m) { return m >= 1000 ? juce::String(m / 1000.0, 1) + " GB" : juce::String(m) + " MB"; }

// ================================================================== KEYBOARD
// 1.4, cleaner: the instrument at the top (one page each), the way to play below. Nothing else on screen.
KeyboardPanel::KeyboardPanel(Canvas& c) : cv(c) {
    getProperties().set("softShades", true);
    auto& h = cv.host; auto& r = h.rack;
    setWantsKeyboardFocus(true);
    auto pill = [&](juce::Rectangle<int> b, const juce::String& t, ColourFn col, std::function<void()> f, float fs = 8.0f) { auto* p = add<Pill>(b, t, std::move(col), fs, 7.0f); p->onClick = std::move(f); return p; };
    pill({ 1360 - 104, 14, 88, 30 }, U8("✕ CLOSE"), [] { return Theme::btn(); }, [this] { cv.host.keys.allNotesOff(0); cv.host.rack.allOff(); setVisible(false); });
    // the instrument: four tabs
    const char* KN[4] = { "SYNTH 2", "MODULAR", "SAMPLED", "PLUG-IN" };
    for (int k = 0; k < 4; ++k) {
        auto* p = pill({ 210 + k * 156, 12, 150, 34 }, KN[k], [this, k] { return page == k ? Theme::sel() : Theme::btn(); }, [this, k] { showPage(k); }, 10.0f);
        p->dynText = [&r, k, KN] { return (r.kind.load() == k ? U8("● ") : juce::String()) + KN[k]; };
    }
    // where it plays
    const char* INTO[4] = { "OFF", "LIVE 2", "LIVE 3", "LIVE 4" };
    for (int i = 0; i < 4; ++i) pill({ 110 + i * 64, 62, 60, 26 }, INTO[i], [&h, i] { return h.synthInto == i ? Theme::violet() : Theme::btn(); }, [&h, i] { h.setSynthInto(i); });
    for (int t = 0; t < 8; ++t)
        pill({ 520 + t * 38, 62, 34, 26 }, juce::String(t + 1), [this, t] {
            int st = ((int) cv.snap(t < 4 ? "statA" : "statB") >> (4 * (t % 4))) & 15;
            bool mine = (int) cv.model().get("src" + std::to_string(t + 1)) == cv.host.synthInto + 1 && cv.host.synthInto > 0;
            if (mine && (st == 2 || st == 5 || st == 7)) return Theme::red();
            if (mine && st == 6) return Theme::amber();
            return Theme::strip(t + 4).withMultipliedBrightness(0.75f); }, [&h, t] { h.recordSynthOn(t + 1); }, 9.0f);
    // ---- page SYNTH 2
    auto* sname = pill({ 24, 150, 520, 44 }, "", [] { return Theme::card2(); }, [this] {
        juce::PopupMenu pm; auto n = SynthPanel::presetNames();
        for (int i = 0; i < n.size(); ++i) pm.addItem(i + 1, n[i], true, i == cv.synthPresetIndex());
        pm.showMenuAsync(juce::PopupMenu::Options(), [this](int x) { if (x > 0) { cv.synthPreset(x - 1); cv.host.rack.setKind(InstrumentRack::KSynth); cv.host.audition(); } }); }, 12.0f);
    sname->dynText = [this] { return U8("SOUND:  ") + SynthPanel::presetNames()[cv.synthPresetIndex()] + U8("   ▾"); };
    sname->textColour = [] { return Theme::txt(); };
    pages[0] = { sname,
        pill({ 552, 150, 44, 44 }, U8("◀"), [] { return Theme::btn(); }, [this] { int n = SynthPanel::presetNames().size(); cv.synthPreset((cv.synthPresetIndex() + n - 1) % n); cv.host.audition(); }, 11.0f),
        pill({ 600, 150, 44, 44 }, U8("▶"), [] { return Theme::btn(); }, [this] { int n = SynthPanel::presetNames().size(); cv.synthPreset((cv.synthPresetIndex() + 1) % n); cv.host.audition(); }, 11.0f),
        pill({ 660, 150, 200, 44 }, U8("OPEN THE SYNTH ▸"), [] { return Theme::violet(); }, [this] { cv.openSynth(true); }, 10.0f) };
    // ---- page MODULAR
    auto* mp = pill({ 24, 150, 520, 44 }, "", [] { return Theme::card2(); }, [this, &r] {
        juce::PopupMenu pm; auto names = Modular::patchNames();
        for (int i = 0; i < names.size(); ++i) pm.addItem(i + 1, names[i]);
        pm.showMenuAsync(juce::PopupMenu::Options(), [this, &r](int x) { if (x <= 0) return; modPatch = x - 1; r.modular.setPatch(Modular::defaultPatch(modPatch)); r.setKind(InstrumentRack::KModular); cv.host.audition(); }); }, 12.0f);
    mp->dynText = [this] { return U8("START FROM:  ") + Modular::patchNames()[modPatch] + U8("   ▾"); };
    mp->textColour = [] { return Theme::txt(); };
    pages[1] = { mp, pill({ 660, 150, 200, 44 }, U8("OPEN MODULAR ▸"), [] { return Theme::violet(); }, [this] { cv.openModular(true); }, 10.0f) };
    // ---- page SAMPLED: search for an instrument, download it inside ARCO
    search = std::make_unique<juce::TextEditor>();
    search->setTextToShowWhenEmpty(U8("Search 600+ free instruments:  cello, cello section, flute, piano, choir, harp, drums …"), Theme::sub());
    search->setFont(Theme::font(11.0f, 0)); search->setIndents(14, 10);
    search->setColour(juce::TextEditor::backgroundColourId, Theme::scr()); search->setColour(juce::TextEditor::textColourId, Theme::txt());
    search->setColour(juce::TextEditor::outlineColourId, Theme::line()); search->setColour(juce::TextEditor::focusedOutlineColourId, Theme::violet());
    search->onTextChange = [this] { chip = -1; refreshResults(); };
    search->onEscapeKey = [this] { search->clear(); refreshResults(); };
    addChildComponent(*search); search->setBounds(box.getX() + 24, box.getY() + 110, 860, 36);
    pages[2] = { search.get(), pill({ 1336 - 170, 114, 170, 28 }, U8("ADD SFZ FILE …"), [] { return Theme::btn(); }, [this] { addSfz(); }) };
    {   // shortcuts
        const char* CH[11] = { "ON THIS DEVICE", "STRINGS", "WOODWINDS", "BRASS", "PIANOS", "KEYS", "VOICES", "PERCUSSION", "DRUMS", "GUITARS & BASSES", "SYNTHS" };
        int x = 24;
        for (int i = 0; i < 11; ++i) {
            int w = i == 0 ? 132 : i == 9 ? 140 : 100;
            pages[2].push_back(pill({ x, 154, w, 24 }, CH[i], [this, i] { return chip == i ? Theme::violet() : Theme::btn(); }, [this, i] {
                static const char* Q[11] = { "", "strings", "woodwind", "brass", "piano", "keys", "vocal", "percussion", "drum", "bass guitar", "synth" };
                search->setText(i == 0 ? juce::String() : juce::String(Q[i]), juce::dontSendNotification); chip = i;
                if (i == 9) { search->setText("guitar", juce::dontSendNotification); }
                refreshResults(); }, 7.0f));
            x += w + 6;
        }
    }
    // ---- page PLUG-IN
    pages[3] = { pill({ 1336 - 170, 112, 170, 28 }, U8("FIND PLUG-INS"), [] { return Theme::blue(); }, [this, &r] {
                     scanText = U8("looking for instrument plug-ins …"); repaint();
                     juce::MessageManager::callAsync([this, &r] { r.scanPlugins([this](const juce::String& s) { scanText = s; });
                         scanText = juce::String(r.instrumentPlugins().size()) + " instrument plug-ins found"; cv.host.saveSetup(); repaint(); }); }),
                 pill({ 1336 - 350, 112, 172, 28 }, "SHOW ITS WINDOW", [&r] { return r.plugin != nullptr ? Theme::violet() : Theme::btn(); },
                      [this, &r] { if (r.plugin == nullptr) cv.showMessage("no plug-in chosen yet"); else r.showPluginWindow(); }) };
    // ---- playing
    const char* MD[3] = { "KEYS", "CHORDS", "HARMONY" };
    for (int i = 0; i < 3; ++i) pill({ 24 + i * 100, 540, 94, 28 }, MD[i], [this, i] { return playMode == i ? Theme::violet() : Theme::btn(); }, [this, i] { setPlayMode(i); });
    auto* edit = pill({ 330, 540, 130, 28 }, "SET THE CHORDS", [this] { return surface && surface->editing ? Theme::amber() : Theme::btn(); }, [this] {
        if (playMode != 1) setPlayMode(1); surface->editing = !surface->editing; if (!surface->editing) cv.host.saveSetup(); surface->repaint(); });
    edit->dynText = [this] { return surface && surface->editing ? juce::String::fromUTF8("✓ DONE") : juce::String("SET THE CHORDS"); };
    auto* hold = pill({ 470, 540, 130, 28 }, "HOLD", [&h] { return h.latch ? Theme::green() : Theme::btn(); }, [this, &h] { h.setLatch(!h.latch); repaint(); });
    hold->dynText = [&h] { return h.latch ? juce::String::fromUTF8("● HOLD") : juce::String("HOLD"); };
    hold->setTooltip("HOLD: a touch turns a note on, the next touch turns it off (easy chords; every note joins the arpeggiator)");
    pill({ 610, 540, 110, 28 }, "SUSTAIN", [this] { return cv.model().get("pdl_down") > 0.5 ? Theme::green() : Theme::btn(); }, [this, &h] {
        bool on = cv.model().get("pdl_down") < 0.5; cv.model().set("pdl_down", on ? 1 : 0); h.setSustain(on); });
    pill({ 1336 - 300, 540, 34, 28 }, U8("−"), [] { return Theme::btn(); }, [this] { octave = juce::jmax(0, octave - 1); kb->setAvailableRange(octave * 12 + 12, 127); kb->setKeyPressBaseOctave(octave + 2); }, 12.0f);
    pill({ 1336 - 186, 540, 34, 28 }, "+", [] { return Theme::btn(); }, [this] { octave = juce::jmin(5, octave + 1); kb->setAvailableRange(octave * 12 + 12, 127); kb->setKeyPressBaseOctave(octave + 2); }, 12.0f);
    pill({ 1336 - 140, 540, 140, 28 }, "ALL NOTES OFF", [] { return Theme::btn(); }, [this, &h] { h.keys.allNotesOff(0); h.releaseLatched(); h.rack.allOff(); h.synth.allOff(); });
    auto playArea = juce::Rectangle<int>(box.getX() + 24, box.getY() + 578, 1312, 232);
    kb = std::make_unique<juce::MidiKeyboardComponent>(h.keys, juce::MidiKeyboardComponent::horizontalKeyboard);
    kb->setAvailableRange(octave * 12 + 12, 127);   // (no scroll buttons: the range starts where the keys start)
    kb->setBounds(playArea);
    kb->setKeyWidth(1312.0f / 29.0f);
    kb->setVelocity(0.85f, true);
    kb->setScrollButtonsVisible(false);
    kb->setOctaveForMiddleC(4);
    kb->setKeyPressBaseOctave(octave + 2);
    kb->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::violet());
    kb->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::violet().withAlpha(0.3f));
    addAndMakeVisible(kb.get());
    surface = std::make_unique<KeyPad>(cv, [this] { return octave + 1; });
    addChildComponent(*surface); surface->setBounds(playArea);
    held = std::make_unique<HeldOverlay>(*kb, h); addAndMakeVisible(*held); held->setBounds(playArea);
    showPage(r.kind.load());
    startTimerHz(15);
}
KeyboardPanel::~KeyboardPanel() = default;

void KeyboardPanel::showPage(int p) {
    page = juce::jlimit(0, 3, p);
    for (int k = 0; k < 4; ++k) for (auto* c : pages[k]) c->setVisible(k == page);
    if (page == 2) refreshResults();
    auto& r = cv.host.rack;
    if (page == 0) r.setKind(InstrumentRack::KSynth);
    else if (page == 1) r.setKind(InstrumentRack::KModular);
    else if (page == 2 && r.sampler.instrument() != nullptr && !r.samplerLoading.load()) r.setKind(InstrumentRack::KSampler);
    else if (page == 3 && r.plugin != nullptr) r.setKind(InstrumentRack::KPlugin);
    repaint();
}
void KeyboardPanel::setPlayMode(int m) {
    playMode = juce::jlimit(0, 2, m);
    kb->setVisible(playMode == 0); held->setVisible(playMode == 0);
    surface->setVisible(playMode > 0); surface->mode = playMode == 1 ? 2 : 3; surface->editing = false;
    surface->repaint(); repaint();
}

void KeyboardPanel::visibilityChanged() {
    if (!isVisible()) return;
    showPage(cv.host.rack.kind.load());
    toFront(true); kb->grabKeyboardFocus();
}
bool KeyboardPanel::keyPressed(const juce::KeyPress& k) { if (k == juce::KeyPress::escapeKey) { setVisible(false); return true; } return false; }
void KeyboardPanel::timerCallback() {
    if (!isShowing()) return;
    // back from installing a platform's instrument: look for new plug-ins by itself, and open it
    if (platformPending.isNotEmpty()) {
        bool fg = juce::Process::isForegroundProcess();
        if (fg && !wasForeground) {
            auto& r = cv.host.rack; juce::String want = platformPending;
            cv.showMessage(U8("looking for ") + want + U8(" …"));
            juce::MessageManager::callAsync([this, &r, want] {
                r.scanPlugins([](const juce::String&) {}); cv.host.saveSetup();
                for (auto& d : r.instrumentPlugins())
                    if (d.name.containsIgnoreCase(want)) { platformPending.clear(); showPage(3); cv.showMessage(U8("✓ ") + d.name + U8(" found - press PLAY")); return; }
                cv.showMessage(want + U8(" is not installed yet - when it is, come back to ARCO (or press FIND PLUG-INS)")); });
        }
        wasForeground = fg;
    }
    if (cv.host.rack.indexDownloading != lastDownloading) { lastDownloading = cv.host.rack.indexDownloading; if (page == 2) refreshResults(true); }
    repaint(); if (held->isVisible()) held->repaint();
}

juce::Rectangle<int> KeyboardPanel::listArea() const { return page == 2 ? juce::Rectangle<int>(24, 186, 1312, 302) : juce::Rectangle<int>(24, 150, 1312, 340); }

void KeyboardPanel::setSearch(const juce::String& q) { showPage(2); search->setText(q, true); }

void KeyboardPanel::refreshResults(bool keepScroll) {
    auto& X = InstrumentIndex::get();
    here.assign(X.entries.size(), 0);
    for (int e = 0; e < (int) X.entries.size(); ++e) here[(size_t) e] = X.installed(e) ? 1 : 0;
    auto q = search ? search->getText() : juce::String();
    if (chip == 9) { auto a = X.search("guitar"), b = X.search("bass"); results = a; for (int e : b) if (std::find(results.begin(), results.end(), e) == results.end()) results.push_back(e); }
    else results = chip == 0 ? std::vector<int>() : X.search(q);
    if (!keepScroll) sampleScroll = 0;
    repaint();
}

// the free instruments that only work in their makers' own player: listed with what to do, the rest is automatic
void KeyboardPanel::platformRows(std::vector<Row>& rows) const {
    struct P { const char* name; const char* what; const char* site; const char* url; const char* plugin; };
    static const P ps[] = {
        { "Spitfire LABS", "free instruments by Spitfire Audio - strings, pianos, choirs, textures. Free account; their app installs them; ARCO plays them as a PLUG-IN", "labs.spitfireaudio.com", "https://labs.spitfireaudio.com", "LABS" },
        { "Pianobook", "thousands of free instruments sampled by musicians, played in the free Decent Sampler plug-in. Free account", "pianobook.co.uk", "https://www.pianobook.co.uk", "DecentSampler" },
        { "Komplete Start (Native Instruments)", "free instruments in the Kontakt Player. Free account; Native Access installs them; ARCO plays them as a PLUG-IN", "native-instruments.com", "https://www.native-instruments.com/en/products/komplete/bundles/komplete-start/", "Kontakt" } };
    Row h; h.header = true; h.title = U8("ALSO FREE, ON OTHER PLATFORMS  -  their own account and player: GET IT, install, come back - ARCO finds it by itself"); rows.push_back(h);
    auto plugs = cv.host.rack.instrumentPlugins();
    for (auto& p : ps) {
        Row r; r.title = p.name; r.sub = juce::String(p.what) + U8("  ·  ") + p.site;
        bool have = false; for (auto& d : plugs) if (d.name.containsIgnoreCase(p.plugin)) have = true;
        if (have) { r.button = U8("PLAY ▸ PLUG-IN"); r.bcol = Theme::green(); r.click = [this] { const_cast<KeyboardPanel*>(this)->showPage(3); }; }
        else {
            r.button = platformPending == p.plugin ? U8("WAITING FOR IT …") : U8("GET IT ▸"); r.bcol = platformPending == p.plugin ? Theme::amber() : Theme::violet();
            juce::String url = p.url, plug = p.plugin, name = p.name;
            r.click = [this, url, plug, name] { auto* self = const_cast<KeyboardPanel*>(this);
                self->platformPending = plug; self->pluginsBefore = cv.host.rack.instrumentPlugins().size(); self->wasForeground = false;
                juce::URL(url).launchInDefaultBrowser();
                cv.showMessage(name + U8(": install it with their app, then come back - ARCO looks for it by itself")); };
        }
        rows.push_back(r);
    }
}

std::vector<KeyboardPanel::Row> KeyboardPanel::sampleRows() const {
    auto& r = cv.host.rack;
    auto& X = InstrumentIndex::get();
    std::vector<Row> rows;
    bool samplerOn = r.kind.load() == InstrumentRack::KSampler;
    auto header = [&](const juce::String& t) { Row h; h.header = true; h.title = t; rows.push_back(h); };
    auto indexRow = [&](int e) {
        auto& en = X.entries[(size_t) e]; auto& L = X.libs[(size_t) en.lib];
        Row row; row.title = X.title(e);
        row.sub = X.source(e) + U8("  ·  ") + en.category + U8("  ·  ") + mb(en.sizeMB) + U8("  ·  ") + L.licence;
        bool inst = e < (int) here.size() && here[(size_t) e];
        auto f = X.sfz(e);
        if (r.indexDownloading == e) { row.button = "STOP"; row.bcol = Theme::red(); row.progress = r.downloadProgress.load(); row.sub = r.downloadStatus; row.click = [&r] { r.cancelDownload(); }; }
        else if (inst && r.samplerLoading.load() && r.samplerPath == f.getFullPathName()) { row.button = "LOADING " + juce::String((int) (r.samplerProgress.load() * 100)) + " %"; row.bcol = Theme::amber(); row.progress = r.samplerProgress.load(); }
        else if (inst) {
            row.active = samplerOn && r.samplerPath == f.getFullPathName() && !r.samplerLoading.load();
            row.button = row.active ? U8("Open its window ▸") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green();
            row.click = [&r, f, act = row.active] { if (act) return; if (r.samplerPath == f.getFullPathName() && r.sampler.instrument() != nullptr) r.setKind(InstrumentRack::KSampler); else r.loadSfz(f); };
        } else {
            row.button = U8("DOWNLOAD  ") + mb(en.sizeMB); row.bcol = Theme::blue();
            row.click = [this, &r, e] { if (r.indexDownload || r.download) { cv.showMessage("one download at a time - wait for the other one (or STOP it)"); return; } r.startIndexDownload(e); };
        }
        rows.push_back(row);
    };
    if (chip != 0 && search != nullptr && search->getText().trim().isNotEmpty() ) {
        for (int e : results) indexRow(e);
        platformRows(rows);
        return rows;
    }
    if (chip > 0) { for (int e : results) indexRow(e); platformRows(rows); return rows; }
    // nothing typed: what is on this device, then a few of the best
    bool any = false;
    for (int e = 0; e < (int) here.size(); ++e) if (here[(size_t) e]) { if (!any) header("ON THIS DEVICE"); any = true; indexRow(e); }
    for (auto& i : instrumentCatalogue()) {
        if (!InstrumentRack::installedSfz(i).existsAsFile()) continue;
        if (!any) header("ON THIS DEVICE"); any = true;
        Row row; row.title = i.name; row.sub = juce::String(i.what) + U8("  ·  ") + i.licence + U8("  ·  ") + mb(i.sizeMB);
        auto f = InstrumentRack::installedSfz(i);
        bool here = f.existsAsFile();
        if (r.downloadId == i.id) { row.button = "STOP"; row.bcol = Theme::red(); row.progress = r.downloadProgress.load(); row.sub = r.downloadStatus;
            row.click = [&r] { r.cancelDownload(); }; }
        else if (here && r.samplerLoading.load() && r.samplerPath == f.getFullPathName()) { row.button = "LOADING " + juce::String((int) (r.samplerProgress.load() * 100)) + " %"; row.bcol = Theme::amber(); row.progress = r.samplerProgress.load(); }
        else if (here) {
            row.active = samplerOn && r.samplerPath == f.getFullPathName() && !r.samplerLoading.load();
            row.button = row.active ? U8("Open its window ▸") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green();
            row.click = [&r, f, act = row.active] { if (act) return; if (r.samplerPath == f.getFullPathName() && r.sampler.instrument() != nullptr) r.setKind(InstrumentRack::KSampler); else r.loadSfz(f); };
        } else { row.button = "DOWNLOAD"; row.bcol = Theme::blue(); auto info = i; row.click = [&r, info] { r.startDownload(info); }; }
        rows.push_back(row);
    }
    for (auto& p : r.userSfz) {
        if (!any) header("ON THIS DEVICE"); any = true;
        juce::File f(p); Row row; row.title = f.getFileNameWithoutExtension(); row.sub = f.getParentDirectory().getFullPathName();
        row.active = samplerOn && r.samplerPath == p && !r.samplerLoading.load();
        if (r.samplerLoading.load() && r.samplerPath == p) { row.button = "LOADING " + juce::String((int) (r.samplerProgress.load() * 100)) + " %"; row.bcol = Theme::amber(); }
        else { row.button = row.active ? U8("● PLAYING") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green(); row.click = [&r, f] { r.loadSfz(f); }; }
        rows.push_back(row);
    }
    if (chip == 0) return rows;
    header(any ? "SOME OF THE BEST - or search above" : "SOME OF THE BEST - or search above for any instrument");
    for (int e : X.recommended()) indexRow(e);
    platformRows(rows);
    return rows;
}

std::vector<KeyboardPanel::Row> KeyboardPanel::pluginRows() const {
    auto& r = cv.host.rack;
    std::vector<Row> rows;
    for (auto& d : r.instrumentPlugins()) {
        Row row; row.title = d.name; row.sub = d.manufacturerName + U8("  ·  ") + d.pluginFormatName;
        row.active = r.plugin != nullptr && r.kind.load() == InstrumentRack::KPlugin && r.plugin->getPluginDescription().isDuplicateOf(d);
        row.button = row.active ? U8("Open its window ▸") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green();
        row.click = [&r, d, act = row.active] { if (!act) r.loadPlugin(d); else r.showPluginWindow(); };
        rows.push_back(row);
    }
    return rows;
}

void KeyboardPanel::addSfz() {
   #if JUCE_IOS
    // an SFZ needs its sample folder next to it: on iPad they live in ARCO's folder (Files app → On My iPad → ARCO)
    auto& r = cv.host.rack; int n = 0;
    for (auto& f : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).findChildFiles(juce::File::findFiles, true, "*.sfz"))
        if (!r.userSfz.contains(f.getFullPathName())) { r.userSfz.add(f.getFullPathName()); ++n; }
    cv.host.saveSetup();
    cv.showMessage(n > 0 ? juce::String(n) + " SFZ instruments added" : juce::String(U8("copy an SFZ instrument's folder into Files → On My iPad → ARCO, then press ADD SFZ again")));
    repaint();
    return;
   #endif
    chooser = std::make_unique<juce::FileChooser>("An SFZ instrument", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.sfz");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (!f.existsAsFile()) return;
        auto& r = cv.host.rack; r.userSfz.addIfNotAlreadyThere(f.getFullPathName()); cv.host.saveSetup(); r.loadSfz(f); repaint(); });
}

static void paintRows(juce::Graphics& g, const std::vector<KeyboardPanel::Row>& rows, juce::Rectangle<int> area, int scroll, const juce::String& empty) {
    g.setColour(Theme::scr()); g.fillRoundedRectangle(area.toFloat(), 8.0f);
    if (rows.empty()) { lab(g, empty, area.reduced(16), 9.0f, Theme::sub(), 0, juce::Justification::centred); return; }
    g.saveState(); g.reduceClipRegion(area);
    for (int i = 0; i < (int) rows.size(); ++i) {
        auto& row = rows[(size_t) i];
        juce::Rectangle<int> r(area.getX() + 6, area.getY() + 6 + (i - scroll) * 48, area.getWidth() - 12, 44);
        if (r.getBottom() > area.getBottom() + 40 || r.getY() < area.getY() - 40) continue;
        if (row.header) { lab(g, row.title, { r.getX() + 6, r.getY() + 16, r.getWidth() - 12, 24 }, 8.5f, Theme::sub(), 2); continue; }
        g.setColour(row.active ? Theme::violet().withAlpha(0.18f) : Theme::card2()); g.fillRoundedRectangle(r.toFloat(), 6.0f);
        lab(g, row.title, { r.getX() + 12, r.getY() + 4, r.getWidth() - 180, 18 }, 10.0f, Theme::txt(), 2);
        lab(g, row.sub, { r.getX() + 12, r.getY() + 22, r.getWidth() - 180, 16 }, 7.5f, Theme::sub());
        if (row.button.isNotEmpty()) {
            juce::Rectangle<int> b(r.getRight() - 152, r.getY() + 9, 144, 26);
            g.setColour(row.bcol); g.fillRoundedRectangle(b.toFloat(), 6.0f);
            if (row.progress >= 0) { g.setColour(juce::Colours::white.withAlpha(0.25f)); g.fillRoundedRectangle(b.withWidth((int) (b.getWidth() * juce::jlimit(0.0f, 1.0f, row.progress))).toFloat(), 6.0f); }
            g.setColour(Theme::textOn(row.bcol)); g.setFont(Theme::font(8.0f, 2)); g.drawFittedText(row.button, b, juce::Justification::centred, 1, 0.8f);
        }
    }
    g.restoreState();
}

void KeyboardPanel::paint(juce::Graphics& g) {
    auto& r = cv.host.rack;
    g.fillAll(juce::Colours::black.withAlpha(0.45f));
    g.setColour(Theme::card()); g.fillRoundedRectangle(box.toFloat(), 14.0f);
    g.saveState(); g.setOrigin(box.getPosition());
    lab(g, "KEYBOARD", { 24, 14, 180, 30 }, 17.0f, Theme::txt(), 2);
    lab(g, "PLAYS INTO", { 24, 66, 86, 18 }, 8.5f, Theme::txt(), 2);
    lab(g, "RECORD ON TRACK", { 378, 66, 140, 18 }, 8.5f, Theme::txt(), 2);
    { float lv = juce::jlimit(0.0f, 1.0f, r.level.load()); juce::Rectangle<float> m(1036, 72, 300, 8); g.setColour(Theme::scr()); g.fillRoundedRectangle(m, 4.0f);
      g.setColour(lv > 0.95f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(m.withWidth(m.getWidth() * std::sqrt(lv)), 4.0f); }
    g.setColour(Theme::line()); g.drawHorizontalLine(100, 24.0f, 1336.0f);
    // the page of the instrument
    auto list = juce::Rectangle<int>(24, 150, 1312, 340);
    if (page == 0) {
        lab(g, U8("ARCO's synthesizer: four oscillators, a filter, envelopes, LFOs, an arpeggiator. Choose a sound here; shape it in the SYNTH window."), { 24, 112, 1000, 28 }, 9.0f, Theme::sub());
        lab(g, U8("Ten famous synthesizers to start from: Minimoog, Juno-60, DX7, TB-303, Prophet-5, OB-Xa, CS-80, Jupiter-8, MS-20, Odyssey."), { 24, 206, 1000, 20 }, 8.5f, Theme::sub());
    } else if (page == 1) {
        lab(g, U8("ARCO's modular synthesizer: modules and cables. Choose where to start here; patch it in the MODULAR window."), { 24, 112, 1000, 28 }, 9.0f, Theme::sub());
    } else if (page == 2) {
        auto rows = sampleRows();
        juce::String none = search && search->getText().isNotEmpty() ? U8("nothing found for \"") + search->getText() + U8("\" - try: cello, violin, flute, clarinet, piano, organ, choir, harp, marimba, drums, guitar, bass")
                                                                    : U8("nothing downloaded yet - search above, or press a shortcut");
        paintRows(g, rows, listArea(), sampleScroll, none);
        lab(g, juce::String((int) InstrumentIndex::get().entries.size()) + U8(" free instruments  ·  downloaded inside ARCO - no web page, no account"), { 900, 116, 260, 28 }, 7.0f, Theme::sub());
        juce::String status = r.samplerError.isNotEmpty() ? r.samplerError : r.downloadStatus;
        if (auto si = r.sampler.instrument(); si != nullptr && r.samplerError.isEmpty() && r.downloadId.isEmpty())
            status = r.samplerName + U8(" ready  ·  ") + juce::String(si->memoryBytes() / 1000000) + " MB in memory";
        lab(g, status, { 24, 494, 1312, 16 }, 8.0f, Theme::sub());
    } else {
       #if JUCE_IOS
        juce::String noPlug = "no instrument plug-ins yet - FIND PLUG-INS looks for the AUv3 instruments installed on this iPad";
       #else
        juce::String noPlug = "no instrument plug-ins yet - FIND PLUG-INS looks for the Audio Unit and VST3 instruments on this computer";
       #endif
        lab(g, U8("The instrument plug-ins you have. PLAY one; SHOW ITS WINDOW opens its own controls."), { 24, 112, 900, 28 }, 9.0f, Theme::sub());
        paintRows(g, pluginRows(), list, pluginScroll, noPlug);
        lab(g, scanText.isNotEmpty() ? scanText : (r.pluginName.contains("could not") ? r.pluginName : juce::String()), { 24, 494, 1312, 16 }, 8.0f, Theme::sub());
    }
    g.setColour(Theme::line()); g.drawHorizontalLine(522, 24.0f, 1336.0f);
    lab(g, "C" + juce::String(octave), { 1336 - 262, 540, 74, 28 }, 10.0f, Theme::txt(), 2, juce::Justification::centred);
    auto midi = juce::MidiInput::getAvailableDevices();
    lab(g, midi.isEmpty() ? juce::String("plug in a MIDI keyboard: it plays at once") : "MIDI: " + midi[0].name + (midi.size() > 1 ? " +" + juce::String(midi.size() - 1) : juce::String()),
        { 730, 540, 300, 28 }, 8.0f, Theme::sub());
    g.restoreState();
    drawSoftShades(g, *this);            // 2.3: the keys' shade drawn here (a key drawing its own was cut square)
}

void KeyboardPanel::mouseDown(const juce::MouseEvent& e) {
    auto p = e.getPosition() - box.getPosition();
    setViewportIgnoreDragFlag(page >= 2 && listArea().contains(p));     // its own list scrolls; elsewhere the screen does (iPhone)
    dragging = false; dragY0 = p.y; dragScroll0 = page == 2 ? sampleScroll : pluginScroll;
    juce::ignoreUnused(p);
}
void KeyboardPanel::mouseDrag(const juce::MouseEvent& e) {
    auto p = e.getPosition() - box.getPosition();
    if (page < 2 || !listArea().contains(e.getMouseDownPosition() - box.getPosition())) return;
    if (std::abs(p.y - dragY0) > 8) dragging = true;
    if (!dragging) return;
    int n = (int) (page == 2 ? sampleRows().size() : pluginRows().size());
    int s = juce::jlimit(0, juce::jmax(0, n - 5), dragScroll0 - (int) std::round((p.y - dragY0) / 48.0));
    (page == 2 ? sampleScroll : pluginScroll) = s; repaint();
}
void KeyboardPanel::mouseUp(const juce::MouseEvent& e) {
    auto p = e.getPosition() - box.getPosition();
    auto list = listArea();
    if (dragging || !list.contains(p) || page < 2) { dragging = false; return; }
    auto rows = page == 2 ? sampleRows() : pluginRows(); int scroll = page == 2 ? sampleScroll : pluginScroll;
    int i = (p.y - list.getY() - 6) / 48 + scroll;
    // only the button (or the row of an instrument that is here) acts
    juce::Rectangle<int> btn(list.getRight() - 6 - 152, 0, 152, 10000);
    if (i >= 0 && i < (int) rows.size() && rows[(size_t) i].click && (btn.contains(p.x, 1) || rows[(size_t) i].button == "PLAY")) rows[(size_t) i].click();
    repaint();
}

void KeyboardPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    int d = w.deltaY < 0 ? 1 : -1; juce::ignoreUnused(e);
    if (page == 2) sampleScroll = juce::jlimit(0, juce::jmax(0, (int) sampleRows().size() - 5), sampleScroll + d);
    if (page == 3) pluginScroll = juce::jlimit(0, juce::jmax(0, (int) pluginRows().size() - 6), pluginScroll + d);
    repaint();
}

// (MODULAR: ModularPanel.cpp)

} // namespace arco
