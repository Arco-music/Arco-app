#include "InstrumentPanels.h"
#include "MainView.h"
#include "../App/AudioHost.h"
#include "../Engine/Modular.h"

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static void lab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.85f);
}
static juce::String mb(int m) { return m >= 1000 ? juce::String(m / 1000.0, 1) + " GB" : juce::String(m) + " MB"; }

// ================================================================== KEYBOARD
KeyboardPanel::KeyboardPanel(Canvas& c) : cv(c) {
    auto& h = cv.host; auto& r = h.rack;
    setWantsKeyboardFocus(true);
    auto pill = [&](juce::Rectangle<int> b, const juce::String& t, ColourFn col, std::function<void()> f, float fs = 8.0f) { auto* p = add<Pill>(b, t, std::move(col), fs, 6.0f); p->onClick = std::move(f); return p; };
    pill({ 1360 - 104, 14, 88, 26 }, U8("✕ CLOSE"), [] { return Theme::btn(); }, [this] { cv.host.keys.allNotesOff(0); cv.host.rack.allOff(); setVisible(false); });
    const char* INTO[4] = { "OFF", "LIVE 2", "LIVE 3", "LIVE 4" };
    for (int i = 0; i < 4; ++i) pill({ 110 + i * 62, 52, 58, 24 }, INTO[i], [&h, i] { return h.synthInto == i ? Theme::violet() : Theme::btn(); }, [&h, i] { h.setSynthInto(i); });
    for (int t = 0; t < 8; ++t)
        pill({ 520 + t * 36, 52, 32, 24 }, juce::String(t + 1), [this, t] {
            int st = ((int) cv.snap(t < 4 ? "statA" : "statB") >> (4 * (t % 4))) & 15;
            bool mine = (int) cv.model().get("src" + std::to_string(t + 1)) == cv.host.synthInto + 1 && cv.host.synthInto > 0;
            if (mine && (st == 2 || st == 5 || st == 7)) return Theme::red();
            if (mine && st == 6) return Theme::amber();
            return Theme::strip(t + 4).withMultipliedBrightness(0.75f); }, [&h, t] { h.recordSynthOn(t + 1); }, 9.0f);
    // the four kinds of instrument
    const char* KN[4] = { "SYNTH 2", "MODULAR", "SAMPLED", "PLUG-IN" };
    for (int k = 0; k < 4; ++k) {
        auto* p = pill({ 24 + k * 166, 96, 158, 64 }, KN[k], [&r, k] { return r.kind.load() == k ? Theme::violet() : Theme::card2(); }, [this, &r, k] {
            if (k == InstrumentRack::KSampler && r.sampler.instrument() == nullptr) { cv.showMessage("choose a sampled instrument below (download one, or ADD SFZ FILE)"); return; }
            if (k == InstrumentRack::KPlugin && r.plugin == nullptr) { cv.showMessage("choose a plug-in on the right (FIND PLUG-INS looks for them)"); return; }
            r.setKind(k); repaint(); }, 11.0f);
        p->dynText = [&r, k, KN] {
            juce::String what = k == 0 ? juce::String("ARCO's synthesizer") : k == 1 ? juce::String("patch it yourself")
                              : k == 2 ? (r.samplerName.isNotEmpty() ? r.samplerName : juce::String("pianos, Rhodes, bass, drums"))
                                       : (r.pluginName.isNotEmpty() ? r.pluginName : juce::String("AU / VST3 instruments"));
            return (r.kind.load() == k ? U8("● ") : juce::String()) + KN[k] + "\n" + what; };
    }
    pill({ 694, 96, 150, 30 }, U8("OPEN SYNTH ▸"), [] { return Theme::btn(); }, [this] { cv.openSynth(true); });
    pill({ 694, 130, 150, 30 }, U8("OPEN MODULAR ▸"), [] { return Theme::btn(); }, [this] { cv.openModular(true); });
    pill({ 854, 96, 160, 30 }, "SHOW PLUG-IN WINDOW", [&r] { return r.plugin != nullptr ? Theme::blue() : Theme::btn(); }, [this, &r] {
        if (r.plugin == nullptr) cv.showMessage("no plug-in chosen yet"); else r.showPluginWindow(); });
    pill({ 854, 130, 160, 30 }, "ALL NOTES OFF", [] { return Theme::btn(); }, [this, &h] { h.keys.allNotesOff(0); h.rack.allOff(); h.synth.allOff(); });
    // the lists
    pill({ 520, 184, 154, 24 }, U8("ADD SFZ FILE …"), [] { return Theme::btn(); }, [this] { addSfz(); });
    pill({ 1172, 184, 164, 24 }, U8("FIND PLUG-INS"), [] { return Theme::blue(); }, [this, &r] {
        scanText = U8("looking for instrument plug-ins …"); repaint();
        juce::MessageManager::callAsync([this, &r] {
            r.scanPlugins([this](const juce::String& s) { scanText = s; });
            scanText = juce::String(r.instrumentPlugins().size()) + " instrument plug-ins found";
            cv.host.saveSetup(); repaint(); }); });
    // the keyboard
    pill({ 24, 588, 30, 26 }, U8("−"), [] { return Theme::btn(); }, [this] { octave = juce::jmax(0, octave - 1); kb->setLowestVisibleKey(octave * 12 + 12); kb->setKeyPressBaseOctave(octave + 2); }, 12.0f);
    pill({ 58, 588, 30, 26 }, "+", [] { return Theme::btn(); }, [this] { octave = juce::jmin(5, octave + 1); kb->setLowestVisibleKey(octave * 12 + 12); kb->setKeyPressBaseOctave(octave + 2); }, 12.0f);
    auto* sus = pill({ 100, 588, 110, 26 }, "SUSTAIN", [this] { return cv.model().get("pdl_down") > 0.5 ? Theme::green() : Theme::btn(); }, [this, &h] {
        bool on = cv.model().get("pdl_down") < 0.5; cv.model().set("pdl_down", on ? 1 : 0); h.setSustain(on); });
    sus->setTooltip("the sustain pedal (a MIDI pedal works too)");
    kb = std::make_unique<juce::MidiKeyboardComponent>(h.keys, juce::MidiKeyboardComponent::horizontalKeyboard);
    kb->setAvailableRange(12, 120);
    kb->setBounds(box.getX() + 24, box.getY() + 624, 1312, 182);
    kb->setKeyWidth(1312.0f / 36.0f);
    kb->setLowestVisibleKey(octave * 12 + 12);
    kb->setVelocity(0.85f, true);
    kb->setScrollButtonsVisible(false);
    kb->setOctaveForMiddleC(4);
    kb->setKeyPressBaseOctave(octave + 2);
    kb->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::violet());
    kb->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::violet().withAlpha(0.3f));
    addAndMakeVisible(kb.get());
    startTimerHz(15);
}
KeyboardPanel::~KeyboardPanel() = default;

void KeyboardPanel::visibilityChanged() {
    if (!isVisible()) return;
    toFront(true); kb->setLowestVisibleKey(octave * 12 + 12); kb->grabKeyboardFocus();
}
bool KeyboardPanel::keyPressed(const juce::KeyPress& k) { if (k == juce::KeyPress::escapeKey) { setVisible(false); return true; } return false; }
void KeyboardPanel::timerCallback() { if (isShowing()) repaint(); }

std::vector<KeyboardPanel::Row> KeyboardPanel::sampleRows() const {
    auto& r = cv.host.rack;
    std::vector<Row> rows;
    bool samplerOn = r.kind.load() == InstrumentRack::KSampler;
    for (auto& i : instrumentCatalogue()) {
        Row row; row.title = i.name; row.sub = juce::String(i.what) + U8("  ·  ") + i.licence + U8("  ·  ") + mb(i.sizeMB);
        auto f = InstrumentRack::installedSfz(i);
        bool here = f.existsAsFile();
        if (r.downloadId == i.id) { row.button = "STOP"; row.bcol = Theme::red(); row.progress = r.downloadProgress.load(); row.sub = r.downloadStatus;
            row.click = [&r] { r.cancelDownload(); }; }
        else if (here && r.samplerLoading.load() && r.samplerPath == f.getFullPathName()) { row.button = "LOADING " + juce::String((int) (r.samplerProgress.load() * 100)) + " %"; row.bcol = Theme::amber(); row.progress = r.samplerProgress.load(); }
        else if (here) {
            row.active = samplerOn && r.samplerPath == f.getFullPathName() && !r.samplerLoading.load();
            row.button = row.active ? U8("● PLAYING") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green();
            row.click = [&r, f, act = row.active] { if (act) return; if (r.samplerPath == f.getFullPathName() && r.sampler.instrument() != nullptr) r.setKind(InstrumentRack::KSampler); else r.loadSfz(f); };
        } else { row.button = "DOWNLOAD"; row.bcol = Theme::blue(); auto info = i; row.click = [&r, info] { r.startDownload(info); }; }
        rows.push_back(row);
    }
    for (auto& p : r.userSfz) {
        juce::File f(p); Row row; row.title = f.getFileNameWithoutExtension(); row.sub = f.getParentDirectory().getFullPathName();
        row.active = samplerOn && r.samplerPath == p && !r.samplerLoading.load();
        if (r.samplerLoading.load() && r.samplerPath == p) { row.button = "LOADING " + juce::String((int) (r.samplerProgress.load() * 100)) + " %"; row.bcol = Theme::amber(); }
        else { row.button = row.active ? U8("● PLAYING") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green(); row.click = [&r, f] { r.loadSfz(f); }; }
        rows.push_back(row);
    }
    return rows;
}

std::vector<KeyboardPanel::Row> KeyboardPanel::pluginRows() const {
    auto& r = cv.host.rack;
    std::vector<Row> rows;
    for (auto& d : r.instrumentPlugins()) {
        Row row; row.title = d.name; row.sub = d.manufacturerName + U8("  ·  ") + d.pluginFormatName;
        row.active = r.plugin != nullptr && r.kind.load() == InstrumentRack::KPlugin && r.plugin->getPluginDescription().isDuplicateOf(d);
        row.button = row.active ? U8("● PLAYING") : "PLAY"; row.bcol = row.active ? Theme::violet() : Theme::green();
        row.click = [&r, d, act = row.active] { if (!act) r.loadPlugin(d); else r.showPluginWindow(); };
        rows.push_back(row);
    }
    return rows;
}

void KeyboardPanel::addSfz() {
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
        g.setColour(row.active ? Theme::violet().withAlpha(0.18f) : Theme::card2()); g.fillRoundedRectangle(r.toFloat(), 6.0f);
        lab(g, row.title, { r.getX() + 12, r.getY() + 4, r.getWidth() - 160, 18 }, 10.0f, Theme::txt(), 2);
        lab(g, row.sub, { r.getX() + 12, r.getY() + 22, r.getWidth() - 160, 16 }, 7.5f, Theme::sub());
        if (row.button.isNotEmpty()) {
            juce::Rectangle<int> b(r.getRight() - 132, r.getY() + 9, 124, 26);
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
    lab(g, "KEYBOARD", { 24, 10, 200, 28 }, 17.0f, Theme::txt(), 2);
    lab(g, U8("play an instrument with these keys, the computer's keys or a MIDI keyboard · it plays into a LIVE strip, so it can be looped and recorded"), { 170, 16, 1060, 16 }, 8.5f, Theme::sub());
    lab(g, "PLAYS INTO", { 24, 55, 90, 18 }, 8.5f, Theme::txt(), 2);
    lab(g, "RECORD ON TRACK", { 380, 55, 140, 18 }, 8.5f, Theme::txt(), 2);
    // the level of what plays
    { float lv = juce::jlimit(0.0f, 1.0f, r.level.load()); juce::Rectangle<float> m(1030, 100, 300, 8); g.setColour(Theme::scr()); g.fillRoundedRectangle(m, 4.0f);
      g.setColour(lv > 0.95f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(m.withWidth(m.getWidth() * std::sqrt(lv)), 4.0f);
      lab(g, cv.host.synthInto == 0 ? juce::String("PLAYS INTO is OFF: the first note sends it to LIVE 2") : "plays into LIVE " + juce::String(cv.host.synthInto + 1), { 1030, 112, 300, 16 }, 8.0f, Theme::sub());
      auto midi = juce::MidiInput::getAvailableDevices(); juce::StringArray names; for (auto& d : midi) names.add(d.name);
      lab(g, names.isEmpty() ? juce::String("MIDI: no keyboard connected (plug one in: it plays at once)") : "MIDI: " + names.joinIntoString(", "), { 1030, 132, 300, 28 }, 8.0f, Theme::sub()); }
    lab(g, U8("SAMPLED INSTRUMENTS  ·  free, recorded from real instruments: downloaded once, then always here"), { 24, 186, 490, 20 }, 8.5f, Theme::txt(), 2);
    lab(g, U8("PLUG-INS  ·  the instrument plug-ins on this device"), { 694, 186, 300, 20 }, 8.5f, Theme::txt(), 2);
    auto sr = sampleRows(), pr = pluginRows();
    paintRows(g, sr, sampleArea, sampleScroll, "");
   #if JUCE_IOS
    juce::String noPlug = "no instrument plug-ins yet - FIND PLUG-INS looks for the AUv3 instruments installed on this iPad";
   #else
    juce::String noPlug = "no instrument plug-ins yet - FIND PLUG-INS looks for the Audio Unit and VST3 instruments on this computer";
   #endif
    paintRows(g, pr, pluginArea, pluginScroll, noPlug);
    juce::String status = r.samplerError.isNotEmpty() ? r.samplerError : r.downloadStatus;
    if (auto s = r.sampler.instrument(); s != nullptr && r.samplerError.isEmpty() && r.downloadId.isEmpty())
        status = r.samplerName + U8(" ready  ·  ") + juce::String((int) s->regions.size()) + " samples zones  ·  " + juce::String(s->memoryBytes() / 1000000) + " MB in memory";
    lab(g, status, { 24, 568, 650, 16 }, 8.0f, Theme::sub());
    lab(g, scanText.isNotEmpty() ? scanText : (r.pluginName.contains("could not") ? r.pluginName : juce::String()), { 694, 568, 642, 16 }, 8.0f, Theme::sub());
    lab(g, juce::String("from C") + juce::String(octave) + U8("  ·  computer keys: A W S E D F T G Y H U J K  ·  wheel or drag on a list to scroll"), { 222, 592, 700, 18 }, 8.0f, Theme::sub());
    g.restoreState();
}

void KeyboardPanel::mouseDown(const juce::MouseEvent& e) {
    auto p = e.getPosition() - box.getPosition();
    auto hit = [&](const std::vector<Row>& rows, juce::Rectangle<int> area, int scroll) {
        if (!area.contains(p)) return false;
        int i = (p.y - area.getY() - 6) / 48 + scroll;
        if (i >= 0 && i < (int) rows.size() && rows[(size_t) i].click) {
            bool onButton = p.x > area.getRight() - 140;
            if (onButton || rows[(size_t) i].button == "PLAY") rows[(size_t) i].click();
        }
        repaint(); return true; };
    if (hit(sampleRows(), sampleArea, sampleScroll)) return;
    hit(pluginRows(), pluginArea, pluginScroll);
}

void KeyboardPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    auto p = e.getPosition() - box.getPosition(); int d = w.deltaY < 0 ? 1 : -1;
    if (sampleArea.contains(p)) sampleScroll = juce::jlimit(0, juce::jmax(0, (int) sampleRows().size() - 6), sampleScroll + d);
    if (pluginArea.contains(p)) pluginScroll = juce::jlimit(0, juce::jmax(0, (int) pluginRows().size() - 6), pluginScroll + d);
    repaint();
}

// ================================================================== MODULAR
static juce::Colour modColour(int type) {
    switch (type) {
    case MKeys: case MClock: case MSeq: return Theme::amber();
    case MVco: case MNoise: return Theme::violet();
    case MVcf: case MFold: return Theme::blue();
    case MVca: case MMix: case MOut: case MAtt: return Theme::green();
    case MDelay: return Theme::fx("dly");
    default: return Theme::c(Theme::rgb(0.95f, 0.78f, 0.32f));
    }
}
static juce::Colour cableColour(int i) {
    switch (i % 6) { case 0: return Theme::red(); case 1: return Theme::amber(); case 2: return Theme::green(); case 3: return Theme::blue(); case 4: return Theme::violet(); default: return Theme::fx("cho"); }
}

struct ModularPanel::Rack : public juce::Component {
    ModularPanel& panel; Modular& M;
    struct ModView;
    juce::OwnedArray<ModView> views;
    // a cable being drawn
    bool dragging = false; int dragMod = 0, dragPort = 0; bool dragFromOut = true; juce::Point<float> dragPos; int dragColour = 0;
    Rack(ModularPanel& p, Modular& m) : panel(p), M(m) {}
    ModView* viewOf(int id) const;
    juce::Point<float> portPos(int id, bool out, int port) const;
    bool portAt(juce::Point<float> p, int& id, bool& out, int& port) const;
    void build();
    void paintOverChildren(juce::Graphics& g) override;
    void paint(juce::Graphics& g) override { g.fillAll(Theme::scr()); }
    void beginDrag(int id, bool out, int port, juce::Point<float> at) {
        if (!out) {     // an input with a cable: pick that cable up (its plug follows the mouse)
            for (auto& c : M.patch.cables) if (c.toMod == id && c.toPort == port) { int fm = c.fromMod, fp = c.fromPort; dragColour = c.colour; M.disconnect(id, port); dragMod = fm; dragPort = fp; dragFromOut = true; dragging = true; dragPos = at; repaint(); return; }
        }
        dragMod = id; dragPort = port; dragFromOut = out; dragging = true; dragPos = at; dragColour = (int) M.patch.cables.size();
        repaint();
    }
    void moveDrag(juce::Point<float> at) { if (dragging) { dragPos = at; repaint(); } }
    void endDrag(juce::Point<float> at) {
        if (!dragging) return;
        dragging = false;
        int id, port; bool out;
        if (portAt(at, id, out, port) && out != dragFromOut && id != dragMod) {
            if (dragFromOut) M.connect(dragMod, dragPort, id, port); else M.connect(id, port, dragMod, dragPort);
        }
        repaint();
    }
};

// a knob of a module
struct ModKnob : public juce::Component {
    Modular& M; int id, k; const ModParamDef& d;
    float startT = 0, startY = 0;
    ModKnob(Modular& m, int id_, int k_, const ModParamDef& def) : M(m), id(id_), k(k_), d(def) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    float value() const { for (auto& m : M.patch.mods) if (m.id == id) return m.p[(size_t) k]; return d.def; }
    float toT(float v) const { return d.log ? std::log(v / d.lo) / std::log(d.hi / d.lo) : (v - d.lo) / (d.hi - d.lo); }
    float fromT(float t) const { t = juce::jlimit(0.0f, 1.0f, t); float v = d.log ? d.lo * std::pow(d.hi / d.lo, t) : d.lo + t * (d.hi - d.lo); return stepped() ? std::round(v) : v; }
    bool stepped() const { juce::String n(d.name); return d.choices != nullptr || n == "OCT" || n == "OCTAVE" || n == "STEPS" || n == "TUNE" || (n.length() == 1 && n[0] >= '1' && n[0] <= '8' && d.lo < 0); }
    juce::String text(float v) const {
        if (d.choices != nullptr) return d.choices[juce::jlimit(0, d.nChoices - 1, (int) std::round(v))];
        juce::String n(d.name);
        if (n == "BIPOLAR") return v > 0.5f ? "+/-" : "0..+";
        if (n == "CUTOFF") return v >= 1000 ? juce::String(v / 1000.0f, 1) + "k" : juce::String((int) v);
        if (n == "RATE" && d.log) return juce::String(v, v < 1 ? 2 : 1) + " Hz";
        if (n == "ATTACK" || n == "DECAY" || n == "RELEASE" || n == "TIME" || n == "RISE" || n == "FALL") return v < 1 ? juce::String((int) std::round(v * 1000)) + " ms" : juce::String(v, 2) + " s";
        if (stepped()) return (v > 0 && d.lo < 0 ? "+" : "") + juce::String((int) std::round(v));
        return juce::String(v, std::abs(v) < 10 ? 1 : 0);
    }
    void paint(juce::Graphics& g) override {
        float v = value(), t = juce::jlimit(0.0f, 1.0f, toT(v));
        auto c = juce::Point<float>(getWidth() * 0.5f, 15.0f); float rad = 12.0f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        g.setColour(Theme::card()); g.fillEllipse(c.x - rad * 0.75f, c.y - rad * 0.75f, rad * 1.5f, rad * 1.5f);
        juce::Path tr; tr.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
        g.setColour(Theme::line()); g.strokePath(tr, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float s0 = d.lo < 0 && d.hi > 0 && !d.log ? toT(0) : 0.0f;
        juce::Path va; va.addCentredArc(c.x, c.y, rad, rad, 0, a0 + (a1 - a0) * juce::jmin(s0, t), a0 + (a1 - a0) * juce::jmax(s0, t), true);
        auto col = modColour(typeOf());
        g.setColour(Theme::arc(col)); g.strokePath(va, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float a = a0 + (a1 - a0) * t;
        g.setColour(Theme::txt()); g.drawLine(c.x, c.y, c.x + std::sin(a) * rad * 0.6f, c.y - std::cos(a) * rad * 0.6f, 2.0f);
        lab(g, d.name, { 0, 30, getWidth(), 10 }, 6.5f, Theme::sub(), 2, juce::Justification::centred);
        lab(g, text(v), { 0, 40, getWidth(), 10 }, 6.5f, Theme::txt(), 0, juce::Justification::centred);
    }
    int typeOf() const { for (auto& m : M.patch.mods) if (m.id == id) return m.type; return 0; }
    void mouseDown(const juce::MouseEvent& e) override { startT = toT(value()); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        float sens = e.mods.isShiftDown() ? 0.0012f : (stepped() ? 0.008f : 0.005f);
        M.setParam(id, k, fromT(startT - (e.position.y - startY) * sens)); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { M.setParam(id, k, d.def); repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { M.setParam(id, k, fromT(toT(value()) + w.deltaY * 0.05f)); repaint(); }
};

struct ModularPanel::Rack::ModView : public juce::Component {
    Rack& rack; int id, type;
    juce::OwnedArray<ModKnob> knobs;
    static constexpr int H = 300;
    ModView(Rack& r, int id_, int type_) : rack(r), id(id_), type(type_) {
        auto& D = modDefs()[(size_t) type];
        int w = D.width; setSize(w, H);
        int cols = juce::jmax(1, (w - 8) / 48);
        for (int k = 0; k < (int) D.params.size(); ++k) {
            auto* kn = knobs.add(new ModKnob(rack.M, id, k, D.params[(size_t) k]));
            addAndMakeVisible(kn);
            int cw = (w - 8) / cols;
            kn->setBounds(4 + (k % cols) * cw + (cw - 46) / 2, 44 + (k / cols) * 52, 46, 52);
        }
    }
    juce::Point<float> port(bool out, int k) const {
        auto& D = modDefs()[(size_t) type]; int n = (int) (out ? D.outs.size() : D.ins.size());
        return { getWidth() * (k + 0.5f) / (float) juce::jmax(1, n), out ? 262.0f : 214.0f };
    }
    int portHit(juce::Point<float> p, bool& out) const {
        auto& D = modDefs()[(size_t) type];
        for (int o = 0; o < 2; ++o) { int n = (int) (o ? D.outs.size() : D.ins.size());
            for (int k = 0; k < n; ++k) if (port(o == 1, k).getDistanceFrom(p) < 13.0f) { out = o == 1; return k; } }
        return -1;
    }
    void paint(juce::Graphics& g) override {
        auto& D = modDefs()[(size_t) type];
        auto col = modColour(type);
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 8.0f);
        g.setColour(col); g.fillRoundedRectangle(r.withHeight(5.0f), 2.5f);
        lab(g, D.name, { 10, 8, getWidth() - 34, 16 }, 10.0f, Theme::txt(), 2);
        lab(g, D.desc, { 10, 24, getWidth() - 16, 14 }, 6.5f, Theme::sub());
        lab(g, U8("✕"), { getWidth() - 24, 6, 18, 18 }, 9.0f, Theme::sub(), 2, juce::Justification::centred);
        // ports
        g.setColour(Theme::line()); g.drawHorizontalLine(196, 8.0f, getWidth() - 8.0f);
        lab(g, "IN", { 8, 198, 30, 10 }, 6.0f, Theme::sub(), 2);
        lab(g, "OUT", { 8, 247, 30, 10 }, 6.0f, Theme::sub(), 2);
        for (int o = 0; o < 2; ++o) {
            int n = (int) (o ? D.outs.size() : D.ins.size());
            for (int k = 0; k < n; ++k) {
                auto p = port(o == 1, k);
                if (o == 1) {
                    float v = std::abs(rack.M.valueAt(id, k)) / 10.0f;
                    g.setColour(col.withAlpha(0.15f + 0.85f * juce::jlimit(0.0f, 1.0f, v))); g.fillEllipse(p.x - 11, p.y - 11, 22, 22);
                }
                g.setColour(Theme::bg()); g.fillEllipse(p.x - 8, p.y - 8, 16, 16);
                g.setColour(o == 1 ? col : Theme::txt().withAlpha(0.7f)); g.drawEllipse(p.x - 8, p.y - 8, 16, 16, 2.0f);
                g.setColour(Theme::bg().darker(0.4f)); g.fillEllipse(p.x - 3.5f, p.y - 3.5f, 7, 7);
                lab(g, o ? D.outs[(size_t) k] : D.ins[(size_t) k], { (int) p.x - 30, (int) p.y + 10, 60, 11 }, 6.0f, Theme::sub(), 2, juce::Justification::centred);
            }
        }
        if (type == MOut) { float lv = juce::jlimit(0.0f, 1.0f, rack.M.outLevel.load()); g.setColour(Theme::scr()); g.fillRoundedRectangle(16, 150, getWidth() - 32.0f, 8, 4);
            g.setColour(lv > 0.95f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(16, 150, (getWidth() - 32.0f) * std::sqrt(lv), 8, 4); }
        if (type == MSeq) { int st = (int) rack.M.valueAt(id, 2); int cols = juce::jmax(1, (getWidth() - 8) / 48), cw = (getWidth() - 8) / cols;
            g.setColour(col); g.fillEllipse(4 + (st % cols) * cw + cw * 0.5f - 3, 44 + (st / cols) * 52 - 4.0f, 6, 6); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.x > getWidth() - 26 && e.y < 26) { int i = id; auto* rk = &rack; juce::MessageManager::callAsync([rk, i] { rk->M.removeModule(i); rk->panel.rebuild(); }); return; }
        bool out; int k = portHit(e.position, out);
        if (k >= 0) rack.beginDrag(id, out, k, e.getEventRelativeTo(&rack).position);
    }
    void mouseDrag(const juce::MouseEvent& e) override { rack.moveDrag(e.getEventRelativeTo(&rack).position); }
    void mouseUp(const juce::MouseEvent& e) override { rack.endDrag(e.getEventRelativeTo(&rack).position); }
};

ModularPanel::Rack::ModView* ModularPanel::Rack::viewOf(int id) const { for (auto* v : views) if (v->id == id) return v; return nullptr; }
juce::Point<float> ModularPanel::Rack::portPos(int id, bool out, int port) const {
    if (auto* v = viewOf(id)) return v->port(out, port) + v->getPosition().toFloat(); return {}; }
bool ModularPanel::Rack::portAt(juce::Point<float> p, int& id, bool& out, int& port) const {
    for (auto* v : views) if (v->getBounds().toFloat().expanded(6).contains(p)) { int k = v->portHit(p - v->getPosition().toFloat(), out); if (k >= 0) { id = v->id; port = k; return true; } }
    return false;
}
void ModularPanel::Rack::build() {
    views.clear();
    std::vector<std::pair<int, int>> mods;
    { for (auto& m : M.patch.mods) mods.push_back({ m.id, m.type }); }
    int x = 8, y = 8, W = juce::jmax(400, getParentWidth() > 0 ? getParentWidth() - 12 : 1300);
    for (auto& m : mods) {
        auto* v = views.add(new ModView(*this, m.first, m.second));
        if (x + v->getWidth() > W - 8 && x > 8) { x = 8; y += ModView::H + 10; }
        v->setTopLeftPosition(x, y); addAndMakeVisible(v);
        x += v->getWidth() + 8;
    }
    setSize(W, y + ModView::H + 12);
}
void ModularPanel::Rack::paintOverChildren(juce::Graphics& g) {
    auto cable = [&](juce::Point<float> a, juce::Point<float> b, juce::Colour c) {
        float sag = 30.0f + a.getDistanceFrom(b) * 0.22f;
        juce::Path p; p.startNewSubPath(a); p.cubicTo(a + juce::Point<float>(0, sag), b + juce::Point<float>(0, sag), b);
        g.setColour(juce::Colours::black.withAlpha(0.35f)); g.strokePath(p, juce::PathStrokeType(5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(c.withAlpha(0.9f)); g.strokePath(p, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (auto e : { a, b }) { g.setColour(c); g.fillEllipse(e.x - 5, e.y - 5, 10, 10); }
    };
    const auto& cs = M.patch.cables;   // (the message thread is the only one that changes the patch)
    for (auto& c : cs) if (viewOf(c.fromMod) && viewOf(c.toMod)) cable(portPos(c.fromMod, true, c.fromPort), portPos(c.toMod, false, c.toPort), cableColour(c.colour));
    if (dragging) cable(portPos(dragMod, dragFromOut, dragPort), dragPos, cableColour(dragColour));
}

ModularPanel::ModularPanel(Canvas& c) : cv(c) {
    auto& r = cv.host.rack; auto& h = cv.host;
    auto pill = [&](juce::Rectangle<int> b, const juce::String& t, ColourFn col, std::function<void()> f, float fs = 8.0f) { auto* p = add<Pill>(b, t, std::move(col), fs, 6.0f); p->onClick = std::move(f); return p; };
    pill({ 1360 - 104, 14, 88, 26 }, U8("✕ CLOSE"), [] { return Theme::btn(); }, [this] { setVisible(false); });
    auto* use = pill({ 1360 - 300, 14, 186, 26 }, "", [&r] { return r.kind.load() == InstrumentRack::KModular ? Theme::violet() : Theme::btn(); }, [this, &r, &h] {
        r.setKind(r.kind.load() == InstrumentRack::KModular ? InstrumentRack::KSynth : InstrumentRack::KModular);
        if (r.kind.load() == InstrumentRack::KModular && h.synthInto == 0) h.setSynthInto(1); });
    use->dynText = [&r] { return r.kind.load() == InstrumentRack::KModular ? U8("● THE KEYS PLAY MODULAR") : juce::String("PLAY MODULAR"); };
    auto* patch = pill({ 24, 52, 230, 24 }, "", [] { return Theme::card2(); }, [this, &r] {
        juce::PopupMenu pm; auto names = Modular::patchNames();
        for (int i = 0; i < names.size(); ++i) pm.addItem(i + 1, names[i]);
        pm.showMenuAsync(juce::PopupMenu::Options(), [this, &r](int x) { if (x <= 0) return; patchIndex = x - 1; r.modular.setPatch(Modular::defaultPatch(patchIndex)); rebuild();
            if (r.kind.load() != InstrumentRack::KModular) r.setKind(InstrumentRack::KModular); if (cv.host.synthInto == 0) cv.host.setSynthInto(1); }); });
    patch->dynText = [this] { return U8("PATCH: ") + Modular::patchNames()[patchIndex] + U8("  ▾"); };
    patch->textColour = [] { return Theme::txt(); };
    pill({ 262, 52, 140, 24 }, U8("+ MODULE  ▾"), [] { return Theme::blue(); }, [this, &r] {
        juce::PopupMenu pm;
        for (int t = 0; t < MTypes; ++t) pm.addItem(t + 1, juce::String(modDefs()[(size_t) t].name) + U8("  —  ") + modDefs()[(size_t) t].desc);
        pm.showMenuAsync(juce::PopupMenu::Options(), [this, &r](int x) { if (x <= 0) return; r.modular.addModule(x - 1); rebuild();
            view.setViewPosition(0, juce::jmax(0, rack->getHeight() - view.getHeight())); }); });
    pill({ 410, 52, 120, 24 }, "UNPLUG ALL", [] { return Theme::btn(); }, [this, &r] { ModPatch p = r.modular.patch; p.cables.clear(); r.modular.setPatch(p); rebuild(); });
    const char* INTO[4] = { "OFF", "LIVE 2", "LIVE 3", "LIVE 4" };
    for (int i = 0; i < 4; ++i) pill({ 720 + i * 62, 52, 58, 24 }, INTO[i], [&h, i] { return h.synthInto == i ? Theme::violet() : Theme::btn(); }, [&h, i] { h.setSynthInto(i); });
    pill({ 1000, 52, 150, 24 }, U8("OPEN KEYBOARD ▸"), [] { return Theme::btn(); }, [this] { cv.openKeyboard(true); });
    rack = std::make_unique<Rack>(*this, r.modular);
    view.setViewedComponent(rack.get(), false);
    view.setScrollBarsShown(true, false);
    view.setBounds(box.getX() + 16, box.getY() + 88, 1328, 726);
    addAndMakeVisible(view);
    rebuild();
    startTimerHz(30);
}
ModularPanel::~ModularPanel() { view.setViewedComponent(nullptr, false); }

void ModularPanel::rebuild() { rack->setSize(view.getMaximumVisibleWidth() > 0 ? view.getMaximumVisibleWidth() : 1316, 10); rack->build(); repaint(); }
void ModularPanel::visibilityChanged() { if (isVisible()) { toFront(true); rebuild(); } }
void ModularPanel::timerCallback() { if (isShowing()) rack->repaint(); }

void ModularPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.45f));
    g.setColour(Theme::card()); g.fillRoundedRectangle(box.toFloat(), 14.0f);
    lab(g, "MODULAR", { box.getX() + 24, box.getY() + 10, 200, 28 }, 17.0f, Theme::txt(), 2);
    lab(g, U8("ARCO's modular synthesizer: drag from an output ● to an input ○ to patch · pull a cable off an input to unplug · double-click a knob to reset · 1 V per octave"),
        { box.getX() + 160, box.getY() + 16, 880, 16 }, 8.0f, Theme::sub());
    lab(g, "PLAYS INTO", { box.getX() + 630, box.getY() + 55, 90, 18 }, 8.5f, Theme::txt(), 2);
}

} // namespace arco
