#include "SynthPanel.h"
#include "MainView.h"
#include "Panels.h"
#include <cmath>

namespace arco {

static void slab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                 juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.8f);
}
static juce::Colour cOsc() { return Theme::c(Theme::rgb(0.62f, 0.48f, 1.00f)); }
static juce::Colour cOscB() { return Theme::c(Theme::rgb(0.38f, 0.64f, 1.00f)); }
static juce::Colour cFil() { return Theme::c(Theme::rgb(1.00f, 0.55f, 0.30f)); }
static juce::Colour cEnv() { return Theme::c(Theme::rgb(0.30f, 0.82f, 0.62f)); }
static juce::Colour cLfo() { return Theme::c(Theme::rgb(0.95f, 0.78f, 0.32f)); }
static juce::Colour cVoi() { return Theme::c(Theme::rgb(0.93f, 0.47f, 0.80f)); }

// ------------------------------------------------------------------ presets
struct Preset { const char* name; std::vector<std::pair<const char*, double>> v; };
static const std::vector<Preset>& factory() {
    static const std::vector<Preset> P = {
        { "Init", {} },
        { "Warm Pad", { { "a_uni", 3 }, { "a_det", 20 }, { "b_lvl", 50 }, { "b_wave", 25 }, { "b_semi", 0 }, { "b_fine", 7 }, { "f_cut", 45 }, { "f_res", 15 },
                        { "f_env", 20 }, { "e1_a", 55 }, { "e1_d", 60 }, { "e1_s", 80 }, { "e1_r", 60 }, { "e2_a", 50 }, { "e2_d", 70 }, { "e2_s", 40 }, { "e2_r", 60 },
                        { "l_rate", 20 }, { "l_cut", 15 }, { "spread", 80 }, { "s_lvl", 65 } } },
        { "Supersaw Lead", { { "a_uni", 5 }, { "a_det", 45 }, { "b_lvl", 40 }, { "b_wave", 50 }, { "b_semi", 12 }, { "b_uni", 3 }, { "b_det", 30 }, { "f_cut", 70 },
                             { "f_res", 25 }, { "f_env", 30 }, { "e1_a", 2 }, { "e1_s", 80 }, { "e1_r", 30 }, { "s_glide", 20 }, { "spread", 90 }, { "mono", 1 } } },
        { "Cello Shadow (FOLLOW)", { { "follow", 1 }, { "f_oct", -12 }, { "a_wave", 25 }, { "a_warp", 20 }, { "sub_lvl", 40 }, { "f_cut", 50 }, { "e1_a", 20 },
                                     { "e1_r", 50 }, { "s_glide", 30 }, { "l_pitch", 8 }, { "l_rate", 35 } } },
        { "Glass Keys", { { "a_wave", 0 }, { "a_warp", 60 }, { "b_wave", 25 }, { "b_lvl", 40 }, { "b_semi", 12 }, { "f_cut", 85 }, { "e1_a", 0 }, { "e1_d", 55 },
                          { "e1_s", 30 }, { "e1_r", 45 }, { "l_rate", 40 }, { "l_wave", 20 } } },
        { "Deep Bass", { { "mono", 1 }, { "a_wave", 75 }, { "a_warp", 10 }, { "sub_lvl", 70 }, { "b_lvl", 0 }, { "f_cut", 30 }, { "f_res", 30 }, { "f_drive", 40 },
                         { "f_env", 45 }, { "e2_a", 0 }, { "e2_d", 30 }, { "e2_s", 10 }, { "e1_a", 0 }, { "e1_s", 80 }, { "e1_r", 15 }, { "s_glide", 10 } } },
        { "Pluck", { { "a_uni", 2 }, { "a_det", 15 }, { "f_cut", 25 }, { "f_res", 35 }, { "f_env", 70 }, { "e2_a", 0 }, { "e2_d", 28 }, { "e2_s", 0 }, { "e2_r", 25 },
                     { "e1_a", 0 }, { "e1_d", 35 }, { "e1_s", 0 }, { "e1_r", 30 } } },
        { "Strings", { { "a_uni", 4 }, { "a_det", 30 }, { "b_wave", 50 }, { "b_lvl", 60 }, { "b_semi", 0 }, { "b_fine", -8 }, { "b_uni", 3 }, { "f_cut", 55 },
                       { "f_env", 15 }, { "e1_a", 45 }, { "e1_d", 50 }, { "e1_s", 85 }, { "e1_r", 55 }, { "l_rate", 30 }, { "l_pitch", 6 }, { "spread", 85 } } },
        { "Vocal Warp", { { "a_wave", 25 }, { "a_warp", 70 }, { "l_wave", 40 }, { "l_rate", 25 }, { "f_type", 50 }, { "f_cut", 55 }, { "f_res", 45 }, { "e1_a", 15 },
                          { "e1_s", 80 } } },
        { "Sub Drone", { { "a_wave", 0 }, { "sub_lvl", 80 }, { "noise_lvl", 8 }, { "f_cut", 35 }, { "e1_a", 70 }, { "e1_r", 75 }, { "l_rate", 10 }, { "l_cut", 25 } } },
        { "Brass", { { "a_uni", 2 }, { "a_det", 10 }, { "f_cut", 35 }, { "f_env", 60 }, { "e2_a", 15 }, { "e2_d", 40 }, { "e2_s", 50 }, { "e1_a", 8 }, { "e1_s", 85 },
                     { "e1_r", 25 } } },
        { "Bell", { { "a_wave", 0 }, { "a_warp", 85 }, { "b_wave", 0 }, { "b_semi", 19 }, { "b_lvl", 45 }, { "b_warp", 50 }, { "e1_a", 0 }, { "e1_d", 75 }, { "e1_s", 0 },
                    { "e1_r", 70 }, { "f_cut", 90 } } },
        { "Noise Wind", { { "a_lvl", 0 }, { "noise_lvl", 70 }, { "f_type", 50 }, { "f_cut", 50 }, { "f_res", 60 }, { "l_rate", 15 }, { "l_cut", 50 }, { "e1_a", 60 },
                          { "e1_r", 70 } } },
    };
    return P;
}
static bool isSynthParam(const ParamDef& d) {
    std::string id = d.id;
    return !d.dests.empty() && d.dests[0] == "st_syn" && !(id.size() >= 3 && (id[0] == 's') && (id[1] == 'n' || id[1] == 'v' || id[1] == 'g') && std::isdigit((unsigned char) id[2]));
}

juce::File SynthPanel::presetFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("synth-presets.json");
}
juce::var SynthPanel::userPresets() { auto v = juce::JSON::parse(presetFile()); return v.isArray() ? v : juce::var(juce::Array<juce::var>()); }

juce::StringArray SynthPanel::presetNames() {
    juce::StringArray n; for (auto& p : factory()) n.add(p.name);
    auto v = juce::JSON::parse(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("synth-presets.json"));
    if (auto* a = v.getArray()) for (auto& p : *a) n.add(U("★ ") + p["name"].toString());
    return n;
}

int numFactorySynthPresets() { return (int) factory().size(); }
void applyFactorySynthPreset(Model& m, int index) {
    for (auto& d : paramTable()) if (isSynthParam(d)) m.set(d.id, d.def);
    if (index >= 0 && index < (int) factory().size()) for (auto& kv : factory()[(size_t) index].v) m.set(kv.first, kv.second);
}

void SynthPanel::loadPreset(int index) {
    auto& m = cv.model();
    auto names = presetNames();
    if (index < 0 || index >= names.size()) return;
    presetIndex = index;
    applyFactorySynthPreset(m, index < (int) factory().size() ? index : 0);
    if (index >= (int) factory().size())
      if (auto* a = userPresets().getArray()) {
        auto p = (*a)[index - (int) factory().size()];
        if (auto* o = p["values"].getDynamicObject()) for (auto& kv : o->getProperties()) m.set(kv.name.toString().toStdString(), (double) kv.value);
    }
    cv.host.synth.allOff();
    repaint();
}

void SynthPanel::savePreset() {
    auto* w = new juce::AlertWindow("Save this sound", "A name for it:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", "My sound");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r != 1) return;
        auto name = w->getTextEditorContents("name").trim(); if (name.isEmpty()) name = "My sound";
        auto* vals = new juce::DynamicObject();
        for (auto& d : paramTable()) if (isSynthParam(d)) vals->setProperty(juce::Identifier(d.id), cv.model().get(d.id));
        auto* o = new juce::DynamicObject(); o->setProperty("name", name); o->setProperty("values", juce::var(vals));
        auto list = userPresets(); list.append(juce::var(o));
        presetFile().getParentDirectory().createDirectory();
        presetFile().replaceWithText(juce::JSON::toString(list));
        presetIndex = presetNames().size() - 1;
        cv.showMessage(U("sound saved: ") + name);
        repaint();
    }), true);
}

// ------------------------------------------------------------------ building the window
void SynthPanel::knob(int x, int y, const char* id, const char* label, ColourFn col) {
    auto& m = cv.model();
    auto* d = m.def(id);
    if (d == nullptr) return;
    std::string s = id;
    add<Knob>({ x, y, 40, 40 }, [&m, s] { return m.get(s); }, [&m, s](double v) { m.set(s, v); }, d->lo, d->hi, d->def, std::move(col));
    labels.push_back({ U(label), { box.getX() + x - 14, box.getY() + y + 41, 68, 13 } });
}

SynthPanel::SynthPanel(Canvas& c) : cv(c) {
    auto& h = cv.host; auto& m = cv.model();
    setWantsKeyboardFocus(true);
    auto* close = add<Pill>({ 1360 - 104, 14, 88, 26 }, U("✕ CLOSE"), [] { return Theme::btn(); }, 9.0f, 7.0f);
    close->onClick = [this] { cv.host.synth.allOff(); setVisible(false); };
    // where it plays, and recording it
    const char* INTO[4] = { "OFF", "LIVE 2", "LIVE 3", "LIVE 4" };
    for (int i = 0; i < 4; ++i) {
        auto* p = add<Pill>({ 110 + i * 62, 60, 58, 24 }, INTO[i], [&h, i] { return h.synthInto == i ? Theme::violet() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [&h, i] { h.setSynthInto(i); };
    }
    for (int t = 0; t < 8; ++t) {
        auto* p = add<Pill>({ 520 + t * 36, 60, 32, 24 }, juce::String(t + 1), [this, t] {
            int st = ((int) cv.snap(t < 4 ? "statA" : "statB") >> (4 * (t % 4))) & 15;
            bool mine = (int) cv.model().get("src" + std::to_string(t + 1)) == cv.host.synthInto + 1 && cv.host.synthInto > 0;
            if (mine && (st == 2 || st == 5 || st == 7)) return Theme::red();
            if (mine && st == 6) return Theme::amber();
            return Theme::strip(t + 4).withMultipliedBrightness(0.75f); }, 9.0f, 6.0f);
        p->onClick = [&h, t] { h.recordSynthOn(t + 1); };
    }
    // presets
    auto* prev = add<Pill>({ 860, 60, 24, 24 }, U("◀"), [] { return Theme::btn(); }, 8.0f, 6.0f);
    prev->onClick = [this] { int n = presetNames().size(); loadPreset((presetIndex + n - 1) % n); };
    auto* name = add<Pill>({ 888, 60, 200, 24 }, "", [] { return Theme::card2(); }, 9.0f, 6.0f);
    name->dynText = [this] { return presetNames()[presetIndex]; };
    name->textColour = [] { return Theme::txt(); };
    name->onClick = [this, name] {
        juce::PopupMenu pm; auto n = presetNames();
        for (int i = 0; i < n.size(); ++i) pm.addItem(i + 1, n[i], true, i == presetIndex);
        pm.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(name), [this](int r) { if (r > 0) loadPreset(r - 1); }); };
    auto* next = add<Pill>({ 1092, 60, 24, 24 }, U("▶"), [] { return Theme::btn(); }, 8.0f, 6.0f);
    next->onClick = [this] { int n = presetNames().size(); loadPreset((presetIndex + 1) % n); };
    auto* sv = add<Pill>({ 1122, 60, 56, 24 }, "SAVE", [] { return Theme::c(Theme::rgb(0.20f, 0.36f, 0.60f)); }, 8.0f, 6.0f);
    sv->onClick = [this] { savePreset(); };
    auto* pan = add<Pill>({ 1186, 60, 150, 24 }, "ALL NOTES OFF", [] { return Theme::btn(); }, 8.0f, 6.0f);
    pan->onClick = [this] { cv.host.keys.allNotesOff(0); cv.host.synth.allOff(); };

    // OSC A / OSC B
    for (int o = 0; o < 2; ++o) {
        int x0 = 24 + o * 412;
        ColourFn col = o == 0 ? ColourFn(cOsc) : ColourFn(cOscB);
        const char* P = o == 0 ? "a_" : "b_";
        static std::string ids[2][7];
        const char* names[7] = { "WAVE", "WARP", "SEMI", "FINE", "LEVEL", "UNISON", "DETUNE" };
        const char* keys[7] = { "wave", "warp", "semi", "fine", "lvl", "uni", "det" };
        for (int k = 0; k < 7; ++k) { ids[o][k] = std::string(P) + keys[k]; knob(x0 + 16 + k * 54, 254, ids[o][k].c_str(), names[k], col); }
    }
    knob(864, 156, "sub_lvl", "SUB", [] { return Theme::txt(); });
    knob(924, 156, "noise_lvl", "NOISE", [] { return Theme::sub(); });
    knob(894, 250, "e2_wave", U("ENV 2 → WAVE").toRawUTF8(), cEnv);
    // FILTER
    const char* FT[3] = { "LOW", "BAND", "HIGH" };
    for (int i = 0; i < 3; ++i) {
        auto* p = add<Pill>({ 1160 + i * 56, 110, 52, 20 }, FT[i], [&m, i] { int t = (int) std::floor(m.get("f_type") / 100 * 2.99); return t == i ? cFil() : Theme::btn(); }, 7.5f, 5.0f);
        p->onClick = [&m, i] { m.set("f_type", i * 50.0); };
    }
    knob(1030, 254, "f_cut", "CUTOFF", cFil); knob(1090, 254, "f_res", "RESO", cFil); knob(1150, 254, "f_drive", "DRIVE", cFil);
    knob(1210, 254, "f_env", "ENV 2", cFil); knob(1270, 254, "f_key", "KEY", cFil);
    // ENV 1 / ENV 2
    const char* AD[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
    static std::string e1[4] = { "e1_a", "e1_d", "e1_s", "e1_r" }, e2[4] = { "e2_a", "e2_d", "e2_s", "e2_r" };
    for (int k = 0; k < 4; ++k) knob(38 + k * 68, 486, e1[k].c_str(), AD[k], cEnv);
    for (int k = 0; k < 4; ++k) knob(352 + k * 68, 486, e2[k].c_str(), AD[k], cEnv);
    // LFO
    knob(668, 486, "l_rate", "RATE", cLfo); knob(726, 486, "l_shape", "SHAPE", cLfo);
    knob(784, 486, "l_wave", U("→ WAVE").toRawUTF8(), cLfo); knob(842, 486, "l_cut", U("→ CUTOFF").toRawUTF8(), cLfo); knob(900, 486, "l_pitch", U("→ PITCH").toRawUTF8(), cLfo);
    auto* sy = add<Pill>({ 900, 368, 52, 20 }, "SYNC", [&m] { return m.get("l_sync") > 0.5 ? cLfo() : Theme::btn(); }, 7.5f, 5.0f);
    sy->onClick = [&m] { m.set("l_sync", m.get("l_sync") > 0.5 ? 0 : 1); };
    // VOICE
    knob(1000, 486, "s_glide", "GLIDE", cVoi); knob(1058, 486, "spread", "SPREAD", cVoi); knob(1116, 486, "s_lvl", "LEVEL", cVoi);
    auto* mo = add<Pill>({ 1180, 400, 64, 24 }, "MONO", [&m] { return m.get("mono") > 0.5 ? cVoi() : Theme::btn(); }, 8.0f, 6.0f);
    mo->onClick = [this, &m] { m.set("mono", m.get("mono") > 0.5 ? 0 : 1); cv.host.synth.allOff(); };
    auto* fo = add<Pill>({ 1250, 400, 82, 24 }, "FOLLOW", [&m] { return m.get("follow") > 0.5 ? Theme::green() : Theme::btn(); }, 8.0f, 6.0f);
    fo->onClick = [this, &m] { m.set("follow", m.get("follow") > 0.5 ? 0 : 1); if (m.get("follow") > 0.5 && cv.host.synthInto == 0) cv.host.setSynthInto(1); };
    auto* fov = add<ValueBox>({ 1250, 470, 82, 24 }, [&m] { return m.get("f_oct"); }, [&m](double v) { m.set("f_oct", std::round(v)); }, -24, 24, 0, [] { return Theme::green(); });
    fov->fsize = 11.0f;
    // keyboard
    kb = std::make_unique<juce::MidiKeyboardComponent>(h.keys, juce::MidiKeyboardComponent::horizontalKeyboard);
    addAndMakeVisible(*kb);
    kb->setBounds(box.getX() + 24, box.getY() + 640, 1080, 160);
    kb->setAvailableRange(24, 108);
    kb->setOctaveForMiddleC(4);
    kb->setLowestVisibleKey(36);
    kb->setKeyWidth(1080.0f / 36.0f);
    kb->setKeyPressBaseOctave(octave);
    kb->setScrollButtonsVisible(false);
    kb->setWantsKeyboardFocus(false);
    auto* om = add<Pill>({ 24, 606, 30, 24 }, U("−"), [] { return Theme::btn(); }, 11.0f, 6.0f);
    om->onClick = [this] { octave = juce::jmax(1, octave - 1); kb->setKeyPressBaseOctave(octave); repaint(); };
    auto* op = add<Pill>({ 58, 606, 30, 24 }, "+", [] { return Theme::btn(); }, 11.0f, 6.0f);
    op->onClick = [this] { octave = juce::jmin(7, octave + 1); kb->setKeyPressBaseOctave(octave); repaint(); };
    startTimerHz(30);
}

SynthPanel::~SynthPanel() { stopTimer(); }

void SynthPanel::visibilityChanged() {
    if (isVisible()) {
        auto bg = Theme::card();
        kb->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, Theme::lightMode() ? juce::Colour(0xfff4f4f6) : juce::Colour(0xffe8e8ee));
        kb->setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff1c1d24));
        kb->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, bg.darker(0.3f));
        kb->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, cOsc().withAlpha(0.8f));
        kb->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cOsc().withAlpha(0.25f));
        kb->setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        kb->setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, bg);
        juce::Timer::callAfterDelay(50, [sp = juce::Component::SafePointer<SynthPanel>(this)] { if (sp) sp->grabKeyboardFocus(); });
    } else cv.host.synth.allOff();
}

void SynthPanel::mouseDown(const juce::MouseEvent& e) {
    if (!box.contains(e.getPosition())) { cv.host.synth.allOff(); setVisible(false); return; }
    grabKeyboardFocus();
}

bool SynthPanel::keyPressed(const juce::KeyPress& k) {
    // the computer keys play (A W S E D F T G Y H U J K O L P), Z / X = octave down / up, Esc closes
    if (k == juce::KeyPress::escapeKey) { cv.host.synth.allOff(); setVisible(false); return true; }
    auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    if (c == 'z') { octave = juce::jmax(1, octave - 1); kb->setKeyPressBaseOctave(octave); repaint(); return true; }
    if (c == 'x') { octave = juce::jmin(7, octave + 1); kb->setKeyPressBaseOctave(octave); repaint(); return true; }
    return kb->keyPressed(k);
}

bool SynthPanel::keyStateChanged(bool isDown) { return kb->keyStateChanged(isDown); }

void SynthPanel::timerCallback() {
    if (!isVisible()) return;
    repaint(box.getX() + 24, box.getY() + 100, 1312, 500);
    repaint(box.getX() + 1116, box.getY() + 606, 220, 200);
    for (auto* k : kids) k->repaint();
}

// ------------------------------------------------------------------ drawing
static float oscWave(float p, float wave, float warp) {     // the same shapes the synth plays (without the anti-alias)
    float q = warp > 0.5f ? std::pow(p, 1 + warp / 100 * 4) : p;
    float w[5] = { std::sin(juce::MathConstants<float>::twoPi * q), 1 - 4 * std::abs(q - 0.5f), 2 * q - 1, q < 0.5f ? 1.0f : -1.0f, (q < 0.12f ? 1.0f : -1.0f) * 0.8f + 0.6f };
    float wp = juce::jlimit(0.0f, 100.0f, wave) / 25.0f; int wi = juce::jmin(3, (int) wp); float wf = wp - wi;
    return w[wi] + (w[wi + 1] - w[wi]) * wf;
}

void SynthPanel::paint(juce::Graphics& g) {
    auto& m = cv.model();
    g.fillAll(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.25f : 0.6f));
    g.setColour(Theme::bg()); g.fillRoundedRectangle(box.toFloat(), 16.0f);
    int X = box.getX(), Y = box.getY();
    auto L = [&](const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt = 0, juce::Justification j = juce::Justification::centredLeft) {
        slab(g, t, { X + x, Y + y, w, (int) (fs * 2.2f) }, fs, c, wt, j); };
    auto card = [&](int x, int y, int w, int h, const juce::String& title, juce::Colour c) {
        g.setColour(Theme::card()); g.fillRoundedRectangle((float) (X + x), (float) (Y + y), (float) w, (float) h, 12.0f);
        g.setColour(c); g.fillEllipse((float) (X + x + 14), (float) (Y + y + 15), 7.0f, 7.0f);
        L(title, x + 28, y + 9, w - 40, 10.5f, Theme::txt(), 2); };
    auto screen = [&](int x, int y, int w, int h) { g.setColour(Theme::scr()); g.fillRoundedRectangle((float) (X + x), (float) (Y + y), (float) w, (float) h, 6.0f);
        return juce::Rectangle<float>((float) (X + x + 6), (float) (Y + y + 6), (float) (w - 12), (float) (h - 12)); };
    L("SYNTH", 24, 12, 200, 17.0f, Theme::txt(), 2);
    L(U("play it with the keys below, the computer keys or a MIDI keyboard - or let the cello play it (FOLLOW) · it sounds through the LIVE strip it plays into, so every track can record it"),
      130, 18, 1080, 8.5f, Theme::sub());
    L("PLAYS INTO", 24, 63, 90, 8.5f, Theme::txt(), 2);
    L("RECORD ON TRACK", 380, 63, 140, 8.5f, Theme::txt(), 2);
    L("SOUND", 812, 63, 50, 8.5f, Theme::txt(), 2);

    // OSC A / B with their wave
    for (int o = 0; o < 2; ++o) {
        int x0 = 24 + o * 412;
        juce::Colour col = o == 0 ? cOsc() : cOscB();
        const char* P = o == 0 ? "a_" : "b_";
        card(x0, 100, 400, 226, o == 0 ? "OSC A" : "OSC B", col);
        float lv = (float) m.get(std::string(P) + "lvl");
        L(juce::String((int) m.get(std::string(P) + "uni")) + (m.get(std::string(P) + "uni") > 1 ? " voices" : " voice") + U("  ·  ")
          + juce::String((int) m.get(std::string(P) + "semi")) + " st", x0 + 250, 108, 136, 8.0f, Theme::sub(), 1, juce::Justification::centredRight);
        auto r = screen(x0 + 14, 132, 372, 104);
        juce::Path p; float wv = (float) m.get(std::string(P) + "wave"), wp = (float) m.get(std::string(P) + "warp");
        for (int i = 0; i <= 200; ++i) {
            float ph = i / 200.0f * 2.0f; float v = oscWave(ph - std::floor(ph), wv, wp);
            float x = r.getX() + r.getWidth() * i / 200.0f, y = r.getCentreY() - v * r.getHeight() * 0.42f * (0.25f + 0.75f * lv / 100.0f);
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(col.withAlpha(lv > 0.5f ? 1.0f : 0.3f)); g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    card(848, 100, 150, 226, U("SUB · NOISE"), Theme::txt());
    // FILTER with its curve
    card(1010, 100, 326, 226, "FILTER", cFil());
    {
        auto r = screen(1024, 136, 298, 100);
        int ft = (int) std::floor(m.get("f_type") / 100 * 2.99);
        float cut = (float) m.get("f_cut") / 100.0f, res = (float) m.get("f_res") / 100.0f;
        juce::Path p;
        for (int i = 0; i <= 150; ++i) {
            float x = i / 150.0f, d = (x - cut) * 8.0f, mag;
            float peak = res * 1.6f * std::exp(-d * d * 3.0f);
            if (ft == 0) mag = (x < cut ? 1.0f : std::exp(-(x - cut) * 9.0f)) + peak;
            else if (ft == 2) mag = (x > cut ? 1.0f : std::exp(-(cut - x) * 9.0f)) + peak;
            else mag = std::exp(-d * d * 0.9f) * (0.8f + res) + peak * 0.5f;
            float y = r.getBottom() - juce::jlimit(0.0f, 1.0f, mag / 2.0f) * r.getHeight();
            if (i == 0) p.startNewSubPath(r.getX(), y); else p.lineTo(r.getX() + x * r.getWidth(), y);
        }
        g.setColour(cFil()); g.strokePath(p, juce::PathStrokeType(2.0f));
    }
    // ENV 1 / ENV 2 shapes
    auto env = [&](int x, int y, int w, int h, const char* pre, juce::Colour c) {
        auto r = screen(x, y, w, h);
        float a = (float) m.get(std::string(pre) + "a") / 100.0f, d = (float) m.get(std::string(pre) + "d") / 100.0f,
              s = (float) m.get(std::string(pre) + "s") / 100.0f, rl = (float) m.get(std::string(pre) + "r") / 100.0f;
        float tot = a + d + 0.5f + rl + 0.001f, W = r.getWidth();
        float x1 = r.getX() + W * a / tot, x2 = x1 + W * d / tot, x3 = x2 + W * 0.5f / tot, x4 = r.getRight();
        juce::Path p; p.startNewSubPath(r.getX(), r.getBottom()); p.lineTo(x1, r.getY()); p.quadraticTo(x1 + (x2 - x1) * 0.2f, r.getY() + (r.getBottom() - r.getY()) * (1 - s), x2, r.getBottom() - s * r.getHeight());
        p.lineTo(x3, r.getBottom() - s * r.getHeight()); p.quadraticTo(x3 + (x4 - x3) * 0.2f, r.getBottom(), x4, r.getBottom());
        g.setColour(c.withAlpha(0.18f)); juce::Path f = p; f.lineTo(r.getX(), r.getBottom()); f.closeSubPath(); g.fillPath(f);
        g.setColour(c); g.strokePath(p, juce::PathStrokeType(2.0f)); };
    card(24, 340, 300, 246, U("ENV 1 · LOUDNESS"), cEnv()); env(38, 376, 272, 90, "e1_", cEnv());
    card(336, 340, 300, 246, U("ENV 2 · FILTER / WAVE"), cEnv()); env(350, 376, 272, 90, "e2_", cEnv());
    // LFO
    card(648, 340, 316, 246, "LFO", cLfo());
    {
        auto r = screen(662, 396, 288, 70);
        float sh = (float) m.get("l_shape");
        juce::Path p; juce::Random rnd(7);
        float rv = 0;
        for (int i = 0; i <= 200; ++i) {
            float ph = i / 200.0f * 2.0f; ph -= std::floor(ph);
            float v;
            if (sh >= 87.5f) { if (i % 25 == 0) rv = rnd.nextFloat() * 2 - 1; v = rv; }
            else v = oscWave(ph, juce::jmin(75.0f, sh), 0);
            float x = r.getX() + r.getWidth() * i / 200.0f, y = r.getCentreY() - v * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(cLfo()); g.strokePath(p, juce::PathStrokeType(2.0f));
        float lv = cv.snap("syn_lfo");
        g.setColour(Theme::txt()); g.fillEllipse(r.getRight() - 10, r.getCentreY() - lv * r.getHeight() * 0.42f - 4, 8, 8);
        L(m.get("l_sync") > 0.5 ? "in tempo" : "free", 662, 370, 120, 8.0f, Theme::sub(), 1);
    }
    // VOICE
    card(976, 340, 360, 246, "VOICE", cVoi());
    L(m.get("mono") > 0.5 ? U("one note at a time (lead / bass)") : U("8 notes at once"), 990, 372, 180, 8.0f, Theme::sub(), 1);
    L(U("FOLLOW: one voice sings what LIVE 1 plays"), 1180, 430, 156, 7.5f, Theme::sub());
    L("FOLLOW OCTAVE", 1180, 455, 156, 7.5f, Theme::sub());
    L(juce::String(cv.host.synth.sounding()) + " / 8 notes sounding", 990, 400, 180, 8.0f, Theme::txt(), 1);
    // knob labels
    for (auto& lb : labels) slab(g, lb.t, lb.r, 7.5f, Theme::sub(), 1, juce::Justification::centred);
    // keyboard info + output
    L("KEYS FROM C" + juce::String(octave - 1), 96, 609, 96, 8.5f, Theme::txt(), 2);
    L(U("  computer keys:  A W S E D F T G Y H U J K O L P  =  notes   ·   Z / X  =  octave   ·   MIDI keyboard: just plug it in   ·   Esc closes"),
      190, 609, 900, 8.0f, Theme::sub());
    {
        g.setColour(Theme::card()); g.fillRoundedRectangle((float) (X + 1116), (float) (Y + 606), 220.0f, 194.0f, 12.0f);
        L("OUTPUT", 1130, 614, 100, 9.0f, Theme::txt(), 2);
        auto r = screen(1128, 640, 196, 120);
        auto& ring = cv.engine().ring("synL");
        int w = ring.w.load(std::memory_order_acquire); const int n = 400;
        juce::Path p;
        for (int i = 0; i < n; ++i) {
            float v = ring.d[(size_t) ((w - n + i + Ring::N) % Ring::N)];
            float x = r.getX() + r.getWidth() * i / (n - 1.0f), y = r.getCentreY() - juce::jlimit(-1.0f, 1.0f, v * 1.5f) * r.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(cOsc()); g.strokePath(p, juce::PathStrokeType(1.4f));
        juce::String where = cv.host.synthInto > 0 ? U("→ LIVE ") + juce::String(cv.host.synthInto + 1) : juce::String("not playing anywhere (OFF)");
        L(where, 1130, 768, 196, 8.5f, cv.host.synthInto > 0 ? Theme::green() : Theme::amber(), 1);
    }
}

} // namespace arco
