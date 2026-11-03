#include "SynthPanel.h"
#include "MainView.h"
#include "Panels.h"
#include "../Engine/Synth2.h"
#include "PlaySurfaces.h"
#include <cmath>
#include <set>

namespace arco {

static void slab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                 juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.8f);
}
static juce::Colour cOsc(int o) { return o == 0 ? Theme::violet() : (o == 1 ? Theme::blue() : (o == 2 ? Theme::fx("cho") : Theme::fx("phs"))); }
static juce::Colour cFil() { return Theme::amber(); }
static juce::Colour cEnv() { return Theme::green(); }
static juce::Colour cLfo() { return Theme::c(Theme::rgb(0.95f, 0.78f, 0.32f)); }
static juce::Colour cVoi() { return Theme::red(); }
static juce::Colour srcColour(int s) {
    switch (s) { case 1: case 2: case 3: return cLfo(); case 4: case 5: return cEnv(); case 6: case 7: case 8: case 9: return Theme::blue(); default: return Theme::violet(); }
}
static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }

// ------------------------------------------------------------------ presets
struct Preset { const char* name; std::vector<std::pair<const char*, double>> v; };
static const std::vector<Preset>& factory() {
    static const std::vector<Preset> P = {
        { "Init", {} },
        // ---- 1.3: ten of the most famous synthesizers, as ARCO's SYNTH 2 can play them (in their spirit, not samples)
        { "Minimoog Bass", { { "mono", 1 }, { "a_wave", 50 }, { "b_lvl", 70 }, { "b_wave", 50 }, { "b_semi", -12 }, { "b_fine", 4 }, { "c_lvl", 40 }, { "c_wave", 75 },
                             { "c_semi", 0 }, { "c_fine", -5 }, { "f_slope", 1 }, { "f_cut", 28 }, { "f_res", 35 }, { "f_drive", 45 }, { "f_env", 55 }, { "f_key", 40 },
                             { "e2_a", 0 }, { "e2_d", 35 }, { "e2_s", 25 }, { "e2_r", 20 }, { "e1_a", 0 }, { "e1_d", 40 }, { "e1_s", 85 }, { "e1_r", 15 }, { "s_glide", 15 } } },
        { "Juno-60 Pad", { { "a_wave", 50 }, { "a_uni", 3 }, { "a_det", 18 }, { "b_lvl", 45 }, { "b_wave", 100 }, { "b_semi", 0 }, { "sub_lvl", 35 },
                           { "f_slope", 1 }, { "f_cut", 48 }, { "f_res", 15 }, { "f_env", 15 }, { "e1_a", 45 }, { "e1_d", 60 }, { "e1_s", 85 }, { "e1_r", 60 },
                           { "e2_a", 40 }, { "e2_d", 60 }, { "e2_s", 50 }, { "e2_r", 50 }, { "l_rate", 25 }, { "l_wave", 20 }, { "spread", 85 } } },
        { "DX7 E-Piano", { { "a_mode", 1 }, { "a_wave", 30 }, { "a_warp", 15 }, { "b_mode", 1 }, { "b_lvl", 30 }, { "b_wave", 18 }, { "b_warp", 92 }, { "b_semi", 0 },
                           { "e1_a", 0 }, { "e1_d", 65 }, { "e1_s", 20 }, { "e1_r", 45 }, { "e2_a", 0 }, { "e2_d", 40 }, { "e2_s", 10 }, { "f_cut", 85 }, { "f_env", 0 },
                           { "m0_src", 4 }, { "m0_dst", 5 }, { "m0_amt", 40 }, { "m1_src", 6 }, { "m1_dst", 5 }, { "m1_amt", 30 }, { "m2_src", 4 }, { "m2_dst", 12 }, { "m2_amt", 30 },
                           { "vel_amt", 85 }, { "spread", 60 } } },
        { "TB-303 Acid", { { "mono", 1 }, { "a_wave", 50 }, { "f_slope", 1 }, { "f_cut", 22 }, { "f_res", 78 }, { "f_env", 65 }, { "f_drive", 35 }, { "f_key", 30 },
                           { "e2_a", 0 }, { "e2_d", 22 }, { "e2_s", 0 }, { "e2_r", 15 }, { "e1_a", 0 }, { "e1_d", 40 }, { "e1_s", 80 }, { "e1_r", 8 }, { "s_glide", 12 },
                           { "vel_amt", 50 }, { "m0_src", 6 }, { "m0_dst", 16 }, { "m0_amt", 25 } } },
        { "Prophet-5 Brass", { { "a_wave", 50 }, { "b_lvl", 70 }, { "b_wave", 50 }, { "b_semi", 0 }, { "b_fine", 7 }, { "f_slope", 1 }, { "f_cut", 30 }, { "f_res", 15 },
                               { "f_env", 55 }, { "f_key", 50 }, { "e2_a", 22 }, { "e2_d", 45 }, { "e2_s", 45 }, { "e2_r", 30 }, { "e1_a", 10 }, { "e1_d", 40 },
                               { "e1_s", 85 }, { "e1_r", 30 }, { "spread", 60 } } },
        { "OB-Xa Jump", { { "a_wave", 50 }, { "a_uni", 2 }, { "a_det", 15 }, { "b_lvl", 75 }, { "b_wave", 50 }, { "b_semi", 0 }, { "b_fine", 10 }, { "f_slope", 0 },
                          { "f_cut", 62 }, { "f_res", 10 }, { "f_env", 20 }, { "e2_a", 0 }, { "e2_d", 50 }, { "e2_s", 60 }, { "e1_a", 2 }, { "e1_d", 50 }, { "e1_s", 90 },
                          { "e1_r", 30 }, { "spread", 75 }, { "s_lvl", 65 } } },
        { "CS-80 Blade", { { "a_wave", 50 }, { "a_uni", 2 }, { "a_det", 12 }, { "b_lvl", 60 }, { "b_wave", 100 }, { "b_semi", 0 }, { "b_fine", -9 }, { "d_lvl", 30 },
                           { "d_wave", 25 }, { "d_semi", 12 }, { "f_cut", 40 }, { "f_res", 25 }, { "f_slope", 0 }, { "f_env", 25 }, { "e2_a", 45 }, { "e2_d", 70 },
                           { "e2_s", 55 }, { "e1_a", 35 }, { "e1_d", 60 }, { "e1_s", 85 }, { "e1_r", 70 }, { "l_rate", 32 }, { "l_pitch", 5 },
                           { "m0_src", 6 }, { "m0_dst", 16 }, { "m0_amt", 20 }, { "spread", 90 } } },
        { "Jupiter-8 Strings", { { "a_wave", 50 }, { "a_uni", 4 }, { "a_det", 22 }, { "b_lvl", 55 }, { "b_wave", 50 }, { "b_semi", 0 }, { "b_fine", 6 }, { "b_uni", 3 },
                                 { "d_lvl", 25 }, { "d_wave", 50 }, { "d_semi", 12 }, { "d_fine", -5 }, { "f_cut", 52 }, { "f_res", 8 }, { "f_slope", 1 }, { "f_env", 10 },
                                 { "e1_a", 50 }, { "e1_d", 60 }, { "e1_s", 90 }, { "e1_r", 60 }, { "l_rate", 30 }, { "l_pitch", 4 }, { "spread", 95 } } },
        { "MS-20 Scream", { { "mono", 1 }, { "a_wave", 50 }, { "b_lvl", 60 }, { "b_wave", 75 }, { "b_semi", 0 }, { "b_fine", 12 }, { "f_cut", 40 }, { "f_res", 85 },
                            { "f_drive", 70 }, { "f_env", 45 }, { "e2_a", 5 }, { "e2_d", 50 }, { "e2_s", 30 }, { "e1_a", 1 }, { "e1_s", 90 }, { "e1_r", 20 },
                            { "l_rate", 55 }, { "l_cut", 15 }, { "s_glide", 8 } } },
        { "Odyssey Sync", { { "mono", 1 }, { "a_mode", 3 }, { "a_warp", 40 }, { "a_wave", 85 }, { "b_lvl", 35 }, { "b_wave", 75 }, { "b_semi", -12 }, { "f_cut", 70 },
                            { "f_res", 20 }, { "f_slope", 0 }, { "e1_a", 1 }, { "e1_s", 85 }, { "e1_r", 20 }, { "s_glide", 10 },
                            { "m0_src", 5 }, { "m0_dst", 8 }, { "m0_amt", 55 }, { "e3_a", 0 }, { "e3_d", 50 }, { "e3_s", 15 } } },
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


// ------------------------------------------------------------------ a knob you can drop a modulation source on
class SynthKnob : public Knob, public juce::DragAndDropTarget {
public:
    SynthKnob(SynthPanel& p, Model& m, const std::string& id_, const ParamDef& d, ColourFn col)
        : Knob([&m, id_] { return m.get(id_); }, [&m, id_](double v) { m.set(id_, v); }, d.lo, d.hi, d.def, std::move(col)), panel(p), model(m), id(id_), dest(synthDestForKnob(id_)) {
        setComponentID("p:" + juce::String(id_));
        // 2.4: the LFOs on this knob move it on the screen as they move the sound
        liveOffset = [this]() -> double {
            if (dest <= 0) return 0.0;
            double sum = 0; auto& syn = panel.lfoSource();
            for (int m2 = 0; m2 < kModSlots; ++m2) {
                int sr = (int) model.get("m" + std::to_string(m2) + "_src");
                if ((int) model.get("m" + std::to_string(m2) + "_dst") != dest || sr < 1 || sr > 3) continue;
                sum += syn.lfoNow[sr - 1].load(std::memory_order_relaxed) * model.get("m" + std::to_string(m2) + "_amt") / 100.0 * synthModDests()[(size_t) dest].range;
            }
            return sum; }; }
    SynthPanel& panel; Model& model; std::string id; int dest; bool over = false;
    void paint(juce::Graphics& g) override {
        Knob::paint(g);
        if (dest > 0) {
            auto r = getLocalBounds().toFloat();
            float rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
            int k = 0;
            for (int m = 0; m < kModSlots; ++m) {
                if ((int) model.get("m" + std::to_string(m) + "_dst") != dest || (int) model.get("m" + std::to_string(m) + "_src") == 0) continue;
                float amt = (float) model.get("m" + std::to_string(m) + "_amt") / 100.0f;
                const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
                double v = get ? get() : def; float t = (float) ((v - lo) / juce::jmax(1e-9, hi - lo));
                float ta = a0 + (a1 - a0) * t, tb = a0 + (a1 - a0) * juce::jlimit(0.0f, 1.0f, t + amt);
                juce::Path p; p.addCentredArc(r.getCentreX(), r.getCentreY(), rad - 0.5f - k * 2.5f, rad - 0.5f - k * 2.5f, 0, juce::jmin(ta, tb), juce::jmax(ta, tb), true);
                g.setColour(srcColour((int) model.get("m" + std::to_string(m) + "_src"))); g.strokePath(p, juce::PathStrokeType(2.0f));
                ++k;
            }
        }
        if (over) { g.setColour(Theme::amber()); g.drawEllipse(getLocalBounds().toFloat().reduced(1), 2.0f); }
    }
    bool isInterestedInDragSource(const SourceDetails& d) override { return dest > 0 && d.description.toString().startsWith("mod:"); }
    void itemDragEnter(const SourceDetails&) override { over = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { over = false; repaint(); }
    void itemDropped(const SourceDetails& d) override { over = false; panel.addMod(d.description.toString().substring(4).getIntValue(), id); repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && dest > 0) {
            juce::PopupMenu pm; int n = 0;
            for (int m = 0; m < kModSlots; ++m) if ((int) model.get("m" + std::to_string(m) + "_dst") == dest && (int) model.get("m" + std::to_string(m) + "_src") > 0) {
                pm.addItem(m + 1, "remove " + juce::String(synthModSources()[(size_t) model.get("m" + std::to_string(m) + "_src")]) + juce::String::fromUTF8(" → this knob")); ++n; }
            if (n == 0) pm.addItem(-1, juce::String::fromUTF8("drag an LFO / ENV / MACRO dot here to move this knob"), false);
            pm.showMenuAsync(juce::PopupMenu::Options(), [this](int r) { if (r > 0) { model.set("m" + std::to_string(r - 1) + "_src", 0); model.set("m" + std::to_string(r - 1) + "_amt", 0); repaint(); } });
            return;
        }
        Knob::mouseDown(e);
    }
};

// a dot to drag: a modulation source
class ModChip : public juce::Component {
public:
    ModChip(SynthPanel& p, int s, juce::Colour c) : panel(p), src(s), col(c) { setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        setTooltip(juce::String::fromUTF8("drag onto any knob")); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1);
        if (Theme::softUI()) {        // 2.4: a silicone capsule to pick up; its light breathes with the LFO
            auto k = r.reduced(3.0f); softRaised(g, k, k.getHeight() * 0.5f, 3.0f, Theme::btn());
            float lv = 1.0f; if (src >= 1 && src <= 3) lv = 0.5f + 0.5f * panel.lfoSource().lfoNow[src - 1].load();
            float dr = juce::jmin(4.0f, k.getHeight() * 0.2f);
            softLed(g, { k.getX() + k.getHeight() * 0.5f + 2, k.getCentreY() }, dr, col.withMultipliedAlpha(0.35f + 0.65f * lv), true);
            g.setColour(Theme::txt()); g.setFont(Theme::font(juce::jmin(10.5f, k.getHeight() * 0.36f), 2));
            g.drawFittedText(juce::String(synthModSources()[(size_t) src]), k.toNearestInt().withTrimmedLeft((int) (k.getHeight() * 0.5f + 8)), juce::Justification::centred, 1, 0.7f);
            return;
        }
        g.setColour(col); g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(Theme::textOn(col)); g.setFont(Theme::font(7.0f, 2));
        g.drawFittedText(juce::String::fromUTF8("⟿ ") + juce::String(synthModSources()[(size_t) src]), getLocalBounds(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseDrag(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true); if (e.getDistanceFromDragStart() > 4 && !panel.isDragAndDropActive()) panel.startDragging("mod:" + juce::String(src), this); }
    void setTooltip(const juce::String&) {}
    SynthPanel& panel; int src; juce::Colour col;
};

// the modulation list
class ModList : public juce::Component {
public:
    ModList(Model& m) : model(m) {}
    Model& model;
    int rowH = 30;
    void paint(juce::Graphics& g) override {
        int y = 0; int shown = 0;
        for (int i = 0; i < kModSlots; ++i) {
            int s = (int) model.get(k(i, "src")), d = (int) model.get(k(i, "dst"));
            if (s == 0 && shown > 0 && i > lastUsed()) continue;
            if (s == 0 && i > lastUsed() + 1) continue;
            auto r = juce::Rectangle<int>(0, y, getWidth(), rowH - 4);
            g.setColour(Theme::card2()); g.fillRoundedRectangle(r.toFloat(), 7.0f);
            auto srcR = r.withWidth(96).reduced(4, 3), dstR = r.withX(r.getX() + 120).withWidth(110).reduced(4, 3), amtR = r.withX(r.getX() + 236).withWidth(r.getWidth() - 276).reduced(4, 8);
            g.setColour(s ? srcColour(s) : Theme::btn()); g.fillRoundedRectangle(srcR.toFloat(), 6.0f);
            slab(g, s ? juce::String(synthModSources()[(size_t) s]) : juce::String("+ source"), srcR, 8.0f, s ? Theme::textOn(srcColour(s)) : Theme::sub(), 2, juce::Justification::centred);
            slab(g, juce::String::fromUTF8("→"), r.withX(r.getX() + 100).withWidth(20), 9.0f, Theme::sub(), 2, juce::Justification::centred);
            g.setColour(Theme::btn()); g.fillRoundedRectangle(dstR.toFloat(), 6.0f);
            slab(g, d ? juce::String(synthModDests()[(size_t) d].name) : juce::String("+ knob"), dstR, 8.0f, Theme::txt(), 2, juce::Justification::centred);
            float a = (float) model.get(k(i, "amt")) / 100.0f;
            g.setColour(Theme::scr()); g.fillRoundedRectangle(amtR.toFloat(), 4.0f);
            float cx = amtR.getCentreX(), w = amtR.getWidth() * 0.5f * a;
            g.setColour(s ? srcColour(s) : Theme::btn()); g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmin(cx, cx + w), (float) amtR.getY(), std::abs(w), (float) amtR.getHeight()), 4.0f);
            slab(g, juce::String((int) std::round(a * 100)), r.withX(r.getRight() - 40).withWidth(22), 8.0f, Theme::txt(), 1, juce::Justification::centredRight);
            slab(g, juce::String::fromUTF8("✕"), r.withX(r.getRight() - 18).withWidth(16), 8.0f, Theme::sub(), 2, juce::Justification::centred);
            y += rowH; ++shown;
        }
        if (lastUsed() < 0) slab(g, juce::String::fromUTF8("drag a ⟿ dot (LFO, ENV, MACRO) onto any knob - or choose here"), { 0, rowH + 4, getWidth(), 30 }, 8.0f, Theme::sub(), 0, juce::Justification::centred);
    }
    int lastUsed() const { int l = -1; for (int i = 0; i < kModSlots; ++i) if ((int) model.get(k(i, "src")) > 0 || (int) model.get(k(i, "dst")) > 0) l = i; return l; }
    static std::string k(int i, const char* f) { return "m" + std::to_string(i) + "_" + f; }
    int rowAt(int y) const { int row = y / rowH; int vis = 0;
        for (int i = 0; i < kModSlots; ++i) { int s = (int) model.get(k(i, "src")); if (s == 0 && i > lastUsed() + 1) continue; if (s == 0 && vis > 0 && i > lastUsed()) continue; if (vis == row) return i; ++vis; } return -1; }
    int dragRow = -1; float dragStart = 0; double amtStart = 0;
    void mouseDown(const juce::MouseEvent& e) override {
        int i = rowAt(e.y); if (i < 0) return;
        int x = e.x;
        if (x < 100) {
            juce::PopupMenu pm; auto& S = synthModSources(); for (int s = 0; s < (int) S.size(); ++s) pm.addItem(s + 1, S[(size_t) s], true, (int) model.get(k(i, "src")) == s);
            pm.showMenuAsync(juce::PopupMenu::Options(), [this, i](int r) { if (r > 0) { model.set(k(i, "src"), r - 1); if (model.get(k(i, "amt")) == 0) model.set(k(i, "amt"), 40); repaint(); } });
        } else if (x > 116 && x < 232) {
            juce::PopupMenu pm; auto& D = synthModDests(); for (int d = 0; d < (int) D.size(); ++d) pm.addItem(d + 1, D[(size_t) d].name, true, (int) model.get(k(i, "dst")) == d);
            pm.showMenuAsync(juce::PopupMenu::Options(), [this, i](int r) { if (r > 0) { model.set(k(i, "dst"), r - 1); repaint(); } });
        } else if (x > getWidth() - 18) { model.set(k(i, "src"), 0); model.set(k(i, "dst"), 0); model.set(k(i, "amt"), 0); repaint(); }
        else { dragRow = i; dragStart = (float) e.x; amtStart = model.get(k(i, "amt")); }
    }
    void mouseDrag(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true); if (dragRow >= 0) { model.set(k(dragRow, "amt"), juce::jlimit(-100.0, 100.0, amtStart + (e.x - dragStart) / (getWidth() - 276) * 200.0)); repaint(); } }
    void mouseUp(const juce::MouseEvent&) override { dragRow = -1; }
    void mouseDoubleClick(const juce::MouseEvent& e) override { int i = rowAt(e.y); if (i >= 0 && e.x > 236 && e.x < getWidth() - 40) model.set(k(i, "amt"), 0); }
};

// ------------------------------------------------------------------ the other keyboards: slide pad, chords, scale grid
// ------------------------------------------------------------------ building the window
void SynthPanel::knob(int pg, int x, int y, const std::string& id, const juce::String& label, ColourFn col, int size) {
    auto& m = cv.model();
    auto* d = m.def(id);
    if (d == nullptr) return;
    auto* k = new SynthKnob(*this, m, id, *d, std::move(col));
    kids.add(k); addAndMakeVisible(k); k->setBounds(box.getX() + x, box.getY() + y, size, size); pages[pg].push_back(k);
    labels.push_back({ label, { box.getX() + x - 16, box.getY() + y + size + 1, size + 32, 13 }, pg });
}
void SynthPanel::chip(int pg, int x, int y, int source, juce::Colour col) {
    auto* c = new ModChip(*this, source, col);
    kids.add(c); addAndMakeVisible(c); c->setBounds(box.getX() + x, box.getY() + y, 78, 18); if (pg >= 0) pages[pg].push_back(c);
}
void SynthPanel::addMod(int source, const std::string& knobId) {
    auto& m = cv.model();
    int d = synthDestForKnob(knobId); if (d <= 0) return;
    for (int i = 0; i < kModSlots; ++i)   // the same pair again: make it stronger
        if ((int) m.get("m" + std::to_string(i) + "_src") == source && (int) m.get("m" + std::to_string(i) + "_dst") == d) { m.set("m" + std::to_string(i) + "_amt", juce::jlimit(-100.0, 100.0, m.get("m" + std::to_string(i) + "_amt") + 20)); repaint(); return; }
    for (int i = 0; i < kModSlots; ++i)
        if ((int) m.get("m" + std::to_string(i) + "_src") == 0) {
            m.set("m" + std::to_string(i) + "_src", source); m.set("m" + std::to_string(i) + "_dst", d); m.set("m" + std::to_string(i) + "_amt", 40);
            cv.showMessage(juce::String(synthModSources()[(size_t) source]) + juce::String::fromUTF8(" → ") + synthModDests()[(size_t) d].name + juce::String::fromUTF8(" (drag in the list to change how much)"));
            repaint(); return;
        }
    cv.showMessage("all 12 modulation slots are in use - remove one first");
}
void SynthPanel::setPage(int p) {
    page = p;
    for (int i = 0; i < 3; ++i) for (auto* c : pages[i]) c->setVisible(i == page);
    repaint();
}

SynthPanel::SynthPanel(Canvas& c) : cv(c) {
    getProperties().set("softShades", true);
    auto& h = cv.host; auto& m = cv.model();
    setWantsKeyboardFocus(true);
    auto* close = add<Pill>(-1, { 1360 - 104, 14, 88, 26 }, U8("✕ CLOSE"), [] { return Theme::btn(); }, 9.0f, 7.0f);
    auto* kbB = add<Pill>(-1, { 1360 - 330, 14, 106, 26 }, U8("KEYBOARD ▸"), [] { return Theme::btn(); }, 8.0f, 7.0f);
    kbB->onClick = [this] { cv.openKeyboard(true); };
    auto* mdB = add<Pill>(-1, { 1360 - 216, 14, 104, 26 }, U8("MODULAR ▸"), [] { return Theme::btn(); }, 8.0f, 7.0f);
    mdB->onClick = [this] { cv.openModular(true); };
    close->onClick = [this] { cv.host.synth.allOff(); setVisible(false); };
    const char* INTO[4] = { "OFF", "LIVE 2", "LIVE 3", "LIVE 4" };
    for (int i = 0; i < 4; ++i) {
        auto* p = add<Pill>(-1, { 110 + i * 62, 52, 58, 24 }, INTO[i], [&h, i] { return h.synthInto == i ? Theme::violet() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [&h, i] { h.setSynthInto(i); };
    }
    for (int t = 0; t < 8; ++t) {
        auto* p = add<Pill>(-1, { 520 + t * 36, 52, 32, 24 }, juce::String(t + 1), [this, t] {
            int st = ((int) cv.snap(t < 4 ? "statA" : "statB") >> (4 * (t % 4))) & 15;
            bool mine = (int) cv.model().get("src" + std::to_string(t + 1)) == cv.host.synthInto + 1 && cv.host.synthInto > 0;
            if (mine && (st == 2 || st == 5 || st == 7)) return Theme::red();
            if (mine && st == 6) return Theme::amber();
            return Theme::strip(t + 4).withMultipliedBrightness(0.75f); }, 9.0f, 6.0f);
        p->onClick = [&h, t] { h.recordSynthOn(t + 1); };
    }
    auto* prev = add<Pill>(-1, { 860, 52, 24, 24 }, U8("◀"), [] { return Theme::btn(); }, 8.0f, 6.0f);
    prev->onClick = [this] { int n = presetNames().size(); loadPreset((presetIndex + n - 1) % n); };
    auto* name = add<Pill>(-1, { 888, 52, 200, 24 }, "", [] { return Theme::card2(); }, 9.0f, 6.0f);
    name->dynText = [this] { return presetNames()[presetIndex]; };
    name->textColour = [] { return Theme::txt(); };
    name->onClick = [this, name] {
        juce::PopupMenu pm; auto n = presetNames();
        for (int i = 0; i < n.size(); ++i) pm.addItem(i + 1, n[i], true, i == presetIndex);
        pm.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(name), [this](int r) { if (r > 0) loadPreset(r - 1); }); };
    auto* next = add<Pill>(-1, { 1092, 52, 24, 24 }, U8("▶"), [] { return Theme::btn(); }, 8.0f, 6.0f);
    next->onClick = [this] { int n = presetNames().size(); loadPreset((presetIndex + 1) % n); };
    auto* sv = add<Pill>(-1, { 1122, 52, 56, 24 }, "SAVE", [] { return Theme::blue(); }, 8.0f, 6.0f);
    sv->onClick = [this] { savePreset(); };
    auto* off = add<Pill>(-1, { 1186, 52, 150, 24 }, "ALL NOTES OFF", [] { return Theme::btn(); }, 8.0f, 6.0f);
    off->onClick = [this] { cv.host.keys.allNotesOff(0); cv.host.synth.allOff(); cv.host.synth.padOff(); };
    // the pages
    const char* PG[3] = { "SOUND", "MOVEMENT", "PLAY" };
    for (int i = 0; i < 3; ++i) { auto* p = add<Pill>(-1, { 24 + i * 128, 90, 120, 28 }, PG[i], [this, i] { return page == i ? Theme::sel() : Theme::card(); }, 9.5f, 7.0f); p->onClick = [this, i] { setPage(i); }; }

    // ======== SOUND: four oscillators
    for (int o = 0; o < 4; ++o) {
        int x0 = 24 + o * 332;
        std::string P = juce::String::charToString((juce::juce_wchar) ('a' + o)).toStdString() + "_";
        const char* MD[4] = { "WAVE", "FM", "FOLD", "SYNC" };
        for (int md = 0; md < 4; ++md) {
            auto* p = add<Pill>(0, { x0 + 92 + md * 56, 136, 52, 20 }, MD[md], [&m, P, md, o] { return (int) m.get(P + "mode") == md ? cOsc(o) : Theme::btn(); }, 7.5f, 5.0f);
            p->onClick = [&m, P, md] { m.set(P + "mode", md); };
        }
        const char* names[7] = { "WAVE", "WARP", "SEMI", "FINE", "LEVEL", "UNISON", "DETUNE" };
        const char* keys[7] = { "wave", "warp", "semi", "fine", "lvl", "uni", "det" };
        for (int k = 0; k < 7; ++k) knob(0, x0 + 8 + k * 45, 266, P + keys[k], names[k], [o] { return cOsc(o); }, 34);
    }
    // sub, noise, filter, voice
    knob(0, 44, 418, "sub_lvl", "SUB", [] { return Theme::txt(); }); knob(0, 104, 418, "noise_lvl", "NOISE", [] { return Theme::sub(); });
    knob(0, 164, 418, "e2_wave", U8("ENV 2 → WAVE"), cEnv);
    const char* FT[3] = { "LOW", "BAND", "HIGH" };
    for (int i = 0; i < 3; ++i) {
        auto* p = add<Pill>(0, { 400 + i * 56, 368, 52, 20 }, FT[i], [&m, i] { int t = (int) std::floor(m.get("f_type") / 100 * 2.99); return t == i ? cFil() : Theme::btn(); }, 7.5f, 5.0f);
        p->onClick = [&m, i] { m.set("f_type", i * 50.0); };
    }
    for (int i = 0; i < 2; ++i) {
        auto* p = add<Pill>(0, { 580 + i * 56, 368, 52, 20 }, i == 0 ? "12 dB" : "24 dB", [&m, i] { return (int) m.get("f_slope") == i ? cFil() : Theme::btn(); }, 7.5f, 5.0f);
        p->onClick = [&m, i] { m.set("f_slope", i); };
    }
    knob(0, 270, 512, "f_cut", "CUTOFF", cFil); knob(0, 330, 512, "f_res", "RESO", cFil); knob(0, 390, 512, "f_drive", "DRIVE", cFil);
    knob(0, 450, 512, "f_env", "ENV 2", cFil); knob(0, 510, 512, "f_key", "KEY", cFil); knob(0, 570, 512, "vel_amt", "VELOCITY", cFil);
    knob(0, 740, 432, "s_glide", "GLIDE", cVoi); knob(0, 800, 432, "spread", "WIDTH", cVoi); knob(0, 860, 432, "s_lvl", "LEVEL", cVoi); knob(0, 920, 432, "bend_rng", "BEND", cVoi);
    // 2.4: LFO 1 and 2 right here: pick up the capsule and drop it on any knob - that knob then moves with it
    for (int l = 0; l < 2; ++l) {
        int y0 = 392 + l * 96; const char* LP2[2] = { "l_", "l2_" };
        auto* c = new ModChip(*this, 1 + l, cLfo()); kids.add(c); addAndMakeVisible(c); c->setBounds(box.getX() + 1044, box.getY() + y0, 100, 34); pages[0].push_back(c);
        knob(0, 1156, y0, std::string(LP2[l]) + "rate", "RATE", cLfo); knob(0, 1208, y0, std::string(LP2[l]) + "shape", "SHAPE", cLfo);
        std::string sy = std::string(LP2[l]) + "sync";
        auto* p = add<Pill>(0, { 1262, y0 + 6, 62, 28 }, "SYNC", [&m, sy] { return m.get(sy) > 0.5 ? cLfo() : Theme::btn(); }, 8.0f, 6.0f);
        p->tint = true; p->onFn = [&m, sy] { return m.get(sy) > 0.5; };
        p->onClick = [&m, sy] { m.set(sy, m.get(sy) > 0.5 ? 0 : 1); };
    }
    auto* mo = add<Pill>(0, { 740, 512, 70, 24 }, "MONO", [&m] { return m.get("mono") > 0.5 ? cVoi() : Theme::btn(); }, 8.0f, 6.0f);
    mo->onClick = [this, &m] { m.set("mono", m.get("mono") > 0.5 ? 0 : 1); cv.host.synth.allOff(); };
    auto* fo = add<Pill>(0, { 820, 512, 82, 24 }, "FOLLOW", [&m] { return m.get("follow") > 0.5 ? Theme::green() : Theme::btn(); }, 8.0f, 6.0f);
    fo->onClick = [this, &m] { m.set("follow", m.get("follow") > 0.5 ? 0 : 1); if (m.get("follow") > 0.5) cv.host.synthNeedsStrip(); };
    auto* fov = add<ValueBox>(0, { 908, 512, 60, 24 }, [&m] { return m.get("f_oct"); }, [&m](double v) { m.set("f_oct", std::round(v)); }, -24, 24, 0, [] { return Theme::green(); });
    fov->fsize = 11.0f;

    // ======== MOVEMENT: envelopes, LFOs, the list, macros
    const char* AD[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
    const char* EP[3] = { "e1_", "e2_", "e3_" }; const char* EK[4] = { "a", "d", "s", "r" };
    for (int e = 0; e < 3; ++e) for (int k = 0; k < 4; ++k) knob(1, 30 + e * 306 + k * 70, 262, std::string(EP[e]) + EK[k], AD[k], cEnv);
    chip(1, 336 + 200, 138, 4, cEnv()); chip(1, 642 + 200, 138, 5, cEnv());
    const char* LP[3] = { "l_", "l2_", "l3_" };
    for (int l = 0; l < 3; ++l) {
        knob(1, 30 + l * 306, 504, std::string(LP[l]) + "rate", "RATE", cLfo); knob(1, 96 + l * 306, 504, std::string(LP[l]) + "shape", "SHAPE", cLfo);
        std::string sy = std::string(LP[l]) + "sync";
        auto* p = add<Pill>(1, { 162 + l * 306, 512, 52, 22 }, "SYNC", [&m, sy] { return m.get(sy) > 0.5 ? cLfo() : Theme::btn(); }, 7.5f, 5.0f);
        p->onClick = [&m, sy] { m.set(sy, m.get(sy) > 0.5 ? 0 : 1); };
        chip(1, 30 + l * 306 + 200, 380, 1 + l, cLfo());
    }
    knob(1, 226, 504, "l_pitch", U8("→ PITCH"), cLfo); knob(1, 226 + 0, 432, "l_cut", U8("→ CUTOFF"), cLfo, 34); knob(1, 168, 432, "l_wave", U8("→ WAVE"), cLfo, 34);
    auto* list = new ModList(m); kids.add(list); addAndMakeVisible(list); list->setBounds(box.getX() + 944, box.getY() + 166, 392, 300); pages[1].push_back(list);
    for (int i = 0; i < 4; ++i) { knob(1, 960 + i * 92, 504, "mc" + std::to_string(i + 1), "MACRO " + juce::String(i + 1), [] { return Theme::violet(); }); chip(1, 940 + i * 92, 480, 10 + i, Theme::violet()); }

    // ======== PLAY: the arpeggiator
    auto* ao = add<Pill>(2, { 44, 170, 120, 34 }, "ARP ON", [&m] { return m.get("arp_on") > 0.5 ? Theme::green() : Theme::btn(); }, 10.0f, 8.0f);
    ao->onClick = [this, &m] { cv.host.keys.allNotesOff(0); cv.host.synth.allOff(); m.set("arp_on", m.get("arp_on") > 0.5 ? 0 : 1); };
    auto* ah = add<Pill>(2, { 172, 170, 90, 34 }, "HOLD", [&m] { return m.get("arp_hold") > 0.5 ? Theme::amber() : Theme::btn(); }, 9.0f, 8.0f);
    ah->onClick = [&m] { m.set("arp_hold", m.get("arp_hold") > 0.5 ? 0 : 1); };
    // 2.0: what the arpeggiator follows - the metronome's beat (the station), STUDIO's when it plays, or the tempo alone
    auto* sy = add<Pill>(2, { 270, 170, 330, 34 }, "", [this] { double b = cv.host.engine.clock.beatNow.load(); float f = (float) (b - std::floor(b));
                                                               return f < 0.12f ? Theme::violet() : Theme::btn(); }, 8.5f, 8.0f);
    sy->tint = true; sy->onFn = [] { return true; };
    sy->dynText = [this] { int src = cv.host.engine.clock.source.load(); double bpm = src == 2 ? cv.host.studio.song.bpm : (double) cv.host.engine.snap("ebpm").v.load();
        if (bpm < 20) bpm = cv.host.model.get("bpm");
        return juce::String::fromUTF8("IN TEMPO · ") + juce::String(bpm, 1) + " BPM · " + (src == 2 ? "STUDIO" : src == 1 ? "THE STATION PLAYING" : "THE METRONOME"); };
    sy->setTooltip("the arpeggiator steps on the metronome's beat: the station's when it plays, STUDIO's when STUDIO plays");
    const char* AM[6] = { "UP", "DOWN", U8("UP-DOWN").toRawUTF8(), "RANDOM", "AS PLAYED", "CHORD" };
    juce::String amn[6] = { "UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED", "CHORD" }; juce::ignoreUnused(AM);
    for (int i = 0; i < 6; ++i) { auto* p = add<Pill>(2, { 44 + i * 100, 246, 94, 28 }, amn[i], [&m, i] { return (int) m.get("arp_mode") == i ? Theme::violet() : Theme::btn(); }, 8.0f, 6.0f); p->onClick = [&m, i] { m.set("arp_mode", i); }; }
    juce::String rn[7] = { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32", "1/2" };
    for (int i = 0; i < 7; ++i) { auto* p = add<Pill>(2, { 44 + i * 86, 318, 80, 28 }, rn[i], [&m, i] { return (int) m.get("arp_rate") == i ? Theme::violet() : Theme::btn(); }, 8.0f, 6.0f); p->onClick = [&m, i] { m.set("arp_rate", i); }; }
    for (int i = 1; i <= 4; ++i) { auto* p = add<Pill>(2, { 44 + (i - 1) * 70, 390, 64, 28 }, juce::String(i) + (i == 1 ? " oct" : " oct"), [&m, i] { return (int) m.get("arp_oct") == i ? Theme::violet() : Theme::btn(); }, 8.0f, 6.0f); p->onClick = [&m, i] { m.set("arp_oct", i); }; }
    knob(2, 360, 384, "arp_gate", "GATE", [] { return Theme::violet(); });

    // ======== the keyboards
    kb = std::make_unique<juce::MidiKeyboardComponent>(h.keys, juce::MidiKeyboardComponent::horizontalKeyboard);
    addAndMakeVisible(*kb);
    kb->setBounds(box.getX() + 24, box.getY() + 640, 1080, 160);
    kb->setAvailableRange(24, 108); kb->setOctaveForMiddleC(4); kb->setLowestVisibleKey(36);
    kb->setKeyWidth(1080.0f / 36.0f); kb->setKeyPressBaseOctave(octave); kb->setScrollButtonsVisible(false); kb->setWantsKeyboardFocus(false);
    pad = std::make_unique<KeyPad>(cv, [this] { return octave; }); addChildComponent(*pad); pad->setBounds(kb->getBounds());
    held = std::make_unique<HeldOverlay>(*kb, h); addAndMakeVisible(*held); held->setBounds(kb->getBounds());
    const char* KM[4] = { "KEYS", "SLIDE PAD", "CHORDS", "HARMONY" };
    for (int i = 0; i < 4; ++i) {
        auto* p = add<Pill>(-1, { 100 + i * 96, 606, 90, 24 }, KM[i], [this, i] { return keysMode == i ? Theme::violet() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [this, i] { keysMode = i; kb->setVisible(i == 0); held->setVisible(i == 0); pad->setVisible(i > 0); auto* kp = static_cast<KeyPad*>(pad.get()); kp->mode = i; kp->editing = false;
                                 cv.host.synth.allOff(); cv.host.synth.padOff(); repaint(); };
    }
    auto* hold = add<Pill>(-1, { 492, 606, 110, 24 }, "HOLD", [&h] { return h.latch ? Theme::green() : Theme::btn(); }, 8.0f, 6.0f);
    hold->dynText = [&h] { return h.latch ? juce::String::fromUTF8("● HOLD (notes stay)") : juce::String("HOLD"); };
    hold->setTooltip("HOLD: a touch turns a note on, the next touch turns it off - easy chords, and every note joins the arpeggiator");
    hold->onClick = [this, &h] { h.setLatch(!h.latch); repaint(); };
    auto* edit = add<Pill>(-1, { 608, 606, 120, 24 }, "SET THE CHORDS", [this] { return static_cast<KeyPad*>(pad.get())->editing ? Theme::amber() : Theme::btn(); }, 8.0f, 6.0f);
    edit->dynText = [this] { return static_cast<KeyPad*>(pad.get())->editing ? juce::String::fromUTF8("✓ DONE") : juce::String("SET THE CHORDS"); };
    edit->onClick = [this] { auto* kp = static_cast<KeyPad*>(pad.get()); if (keysMode != 2) { keysMode = 2; kb->setVisible(false); held->setVisible(false); pad->setVisible(true); kp->mode = 2; kp->editing = true; }
                             else kp->editing = !kp->editing; if (!kp->editing) cv.host.saveSetup(); kp->repaint(); repaint(); };
    auto* om = add<Pill>(-1, { 24, 606, 30, 24 }, U8("−"), [] { return Theme::btn(); }, 11.0f, 6.0f);
    om->onClick = [this] { octave = juce::jmax(1, octave - 1); kb->setKeyPressBaseOctave(octave); repaint(); };
    auto* op = add<Pill>(-1, { 58, 606, 30, 24 }, "+", [] { return Theme::btn(); }, 11.0f, 6.0f);
    op->onClick = [this] { octave = juce::jmin(7, octave + 1); kb->setKeyPressBaseOctave(octave); repaint(); };
    setPage(0);
    startTimerHz(30);
}

SynthPanel::~SynthPanel() { stopTimer(); }
Synth2& SynthPanel::lfoSource() { static Synth2 idle; auto* s = cv.host.engine.synthNow.load(); return s != nullptr ? *s : idle; }
void SynthPanel::setKeysMode(int m, bool edit) {
    keysMode = juce::jlimit(0, 3, m); kb->setVisible(keysMode == 0); held->setVisible(keysMode == 0); pad->setVisible(keysMode > 0);
    auto* kp = static_cast<KeyPad*>(pad.get()); kp->mode = keysMode; kp->editing = edit && keysMode == 2; repaint();
}

void SynthPanel::visibilityChanged() {
    if (isVisible()) {
        auto bg = Theme::card();
        kb->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, Theme::lightMode() ? juce::Colour(0xfff6f3ea) : juce::Colour(0xffe8e4dc));
        kb->setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff201820));
        kb->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, bg.darker(0.3f));
        kb->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::violet().withAlpha(0.8f));
        kb->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::violet().withAlpha(0.25f));
        kb->setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        kb->setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, bg);
        juce::Timer::callAfterDelay(50, [sp = juce::Component::SafePointer<SynthPanel>(this)] { if (sp) sp->grabKeyboardFocus(); });
        cv.host.rack.setKind(InstrumentRack::KSynth);     // the SYNTH window: the keys play the SYNTH
    } else { cv.host.synth.allOff(); cv.host.synth.padOff(); }
}

void SynthPanel::mouseDown(const juce::MouseEvent& e) {
    if (!box.contains(e.getPosition())) { cv.host.synth.allOff(); setVisible(false); return; }
    grabKeyboardFocus();
}

bool SynthPanel::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) { cv.host.synth.allOff(); setVisible(false); return true; }
    auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    if (c == 'z') { octave = juce::jmax(1, octave - 1); kb->setKeyPressBaseOctave(octave); repaint(); return true; }
    if (c == 'x') { octave = juce::jmin(7, octave + 1); kb->setKeyPressBaseOctave(octave); repaint(); return true; }
    return kb->keyPressed(k);
}
bool SynthPanel::keyStateChanged(bool isDown) { return kb->keyStateChanged(isDown); }

void SynthPanel::timerCallback() {
    if (!isVisible()) return;
    repaint(box.getX() + 24, box.getY() + 120, 1312, 480);
    repaint(box.getX() + 1116, box.getY() + 606, 220, 200);
    for (auto* k : kids) if (k->isVisible()) k->repaint();
}

// ------------------------------------------------------------------ drawing
static float oscWave(float p, float wave, float warp, int mode) {
    const float tp = juce::MathConstants<float>::twoPi;
    if (mode == 1) { static const float R[8] = { 0.5f, 1, 1.5f, 2, 3, 4, 5, 7 }; float r = R[juce::jlimit(0, 7, (int) (warp / 100.0f * 7.99f))]; return std::sin(tp * p + wave / 100.0f * 6.0f * std::sin(tp * p * r)); }
    if (mode == 2) return std::sin(std::sin(tp * p) * (1.0f + wave / 100.0f * 7.0f) + warp / 100.0f * 1.5f);
    if (mode == 3) { float ratio = 1.0f + warp / 100.0f * 7.0f; float ps = p * ratio; ps -= std::floor(ps); float b = wave / 100.0f; return (2 * ps - 1) * (0.5f + 0.5f * b) + std::sin(tp * p) * (0.5f - 0.5f * b); }
    float q = warp > 0.5f ? std::pow(p, 1 + warp / 100 * 4) : p;
    float w[5] = { std::sin(tp * q), 1 - 4 * std::abs(q - 0.5f), 2 * q - 1, q < 0.5f ? 1.0f : -1.0f, (q < 0.12f ? 1.0f : -1.0f) * 0.8f + 0.6f };
    float wp = juce::jlimit(0.0f, 100.0f, wave) / 25.0f; int wi = juce::jmin(3, (int) wp); float wf = wp - wi;
    return w[wi] + (w[wi + 1] - w[wi]) * wf;
}

void SynthPanel::paint(juce::Graphics& g) {
    struct ShadeAtEnd { juce::Graphics& g; juce::Component& c; ~ShadeAtEnd() { drawSoftShades(g, c); } } shadeAtEnd { g, *this };
    auto& m = cv.model();
    if (Theme::softUI()) { g.fillAll(Theme::bg()); softRaised(g, box.toFloat(), 30.0f, 10.0f, Theme::bg()); }
    else { g.fillAll(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.25f : 0.6f));
    g.setColour(Theme::bg()); g.fillRoundedRectangle(box.toFloat(), 16.0f); }
    int X = box.getX(), Y = box.getY();
    auto L = [&](const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt = 0, juce::Justification j = juce::Justification::centredLeft) {
        slab(g, t, { X + x, Y + y, w, (int) (fs * 2.2f) }, fs, c, wt, j); };
    auto card = [&](int x, int y, int w, int h, const juce::String& title, juce::Colour c) {
        if (Theme::softUI()) softWellLite(g, juce::Rectangle<float>((float) (X + x), (float) (Y + y), (float) w, (float) h), 20.0f);
        else { g.setColour(Theme::card()); g.fillRoundedRectangle((float) (X + x), (float) (Y + y), (float) w, (float) h, 12.0f); }
        g.setColour(c); g.fillEllipse((float) (X + x + 14), (float) (Y + y + 15), 7.0f, 7.0f);
        L(title, x + 28, y + 9, w - 40, 10.5f, Theme::txt(), 2); };
    auto screen = [&](int x, int y, int w, int h) { if (Theme::softUI()) softWellLite(g, juce::Rectangle<float>((float) (X + x), (float) (Y + y), (float) w, (float) h), 14.0f);
        else { g.setColour(Theme::scr()); g.fillRoundedRectangle((float) (X + x), (float) (Y + y), (float) w, (float) h, 6.0f); }
        return juce::Rectangle<float>((float) (X + x + 6), (float) (Y + y + 6), (float) (w - 12), (float) (h - 12)); };
    auto curve = [&](juce::Rectangle<float> r, std::function<float(float)> f, juce::Colour c, float lv = 1.0f) {
        juce::Path p; for (int i = 0; i <= 200; ++i) { float ph = i / 200.0f * 2.0f; float v = f(ph - std::floor(ph));
            float x = r.getX() + r.getWidth() * i / 200.0f, y = r.getCentreY() - juce::jlimit(-1.2f, 1.2f, v) * r.getHeight() * 0.42f * lv;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
        g.setColour(c); g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); };
    L("SYNTH 2", 24, 10, 200, 17.0f, Theme::txt(), 2);
    L(U8("four oscillators, a mod matrix, an arpeggiator - drag a ⟿ dot onto any knob to move it · it plays through a LIVE strip, so every track can record it"), 150, 16, 860, 8.5f, Theme::sub());
    L("PLAYS INTO", 24, 55, 90, 8.5f, Theme::txt(), 2);
    L("RECORD ON TRACK", 380, 55, 140, 8.5f, Theme::txt(), 2);
    L("SOUND", 812, 55, 50, 8.5f, Theme::txt(), 2);
    if (page == 0) {
        for (int o = 0; o < 4; ++o) {
            int x0 = 24 + o * 332;
            std::string P = juce::String::charToString((juce::juce_wchar) ('a' + o)).toStdString() + "_";
            card(x0, 126, 320, 214, juce::String("OSC ") + juce::String::charToString((juce::juce_wchar) ('A' + o)), cOsc(o));
            float lv = (float) m.get(P + "lvl");
            auto r = screen(x0 + 12, 164, 296, 86);
            float wv = (float) m.get(P + "wave"), wp = (float) m.get(P + "warp"); int md = (int) m.get(P + "mode");
            curve(r, [wv, wp, md](float ph) { return oscWave(ph, wv, wp, md); }, cOsc(o).withAlpha(lv > 0.5f ? 1.0f : 0.3f), 0.25f + 0.75f * lv / 100.0f);
            L(juce::String((int) m.get(P + "uni")) + (m.get(P + "uni") > 1 ? " voices" : " voice") + U8("  ·  ") + juce::String((int) m.get(P + "semi")) + " st", x0 + 152, 222, 150, 8.0f, Theme::sub(), 1, juce::Justification::centredRight);
        }
        card(24, 354, 220, 232, U8("SUB · NOISE"), Theme::txt());
        card(256, 354, 460, 232, "FILTER", cFil());
        {
            auto r = screen(270, 396, 432, 96);
            int ft = (int) std::floor(m.get("f_type") / 100 * 2.99); bool s24 = m.get("f_slope") > 0.5;
            float cut = (float) m.get("f_cut") / 100.0f, res = (float) m.get("f_res") / 100.0f, k = s24 ? 16.0f : 9.0f;
            juce::Path p;
            for (int i = 0; i <= 150; ++i) {
                float x = i / 150.0f, d = (x - cut) * 8.0f, mag;
                float peak = res * 1.6f * std::exp(-d * d * 3.0f);
                if (ft == 0) mag = (x < cut ? 1.0f : std::exp(-(x - cut) * k)) + peak;
                else if (ft == 2) mag = (x > cut ? 1.0f : std::exp(-(cut - x) * k)) + peak;
                else mag = std::exp(-d * d * (s24 ? 1.6f : 0.9f)) * (0.8f + res) + peak * 0.5f;
                float y = r.getBottom() - juce::jlimit(0.0f, 1.0f, mag / 2.0f) * r.getHeight();
                if (i == 0) p.startNewSubPath(r.getX(), y); else p.lineTo(r.getX() + x * r.getWidth(), y);
            }
            g.setColour(cFil()); g.strokePath(p, juce::PathStrokeType(2.0f));
        }
        card(728, 354, 292, 232, "VOICE", cVoi());
        L((m.get("mono") > 0.5 ? U8("one note at a time") : U8("12 notes at once")) + U8("  ·  ") + juce::String(cv.host.synth.sounding()) + " sounding", 740, 390, 270, 8.0f, Theme::sub(), 1);
        L(U8("FOLLOW: a voice sings what LIVE 1 plays"), 740, 548, 270, 7.5f, Theme::sub());
        card(1032, 354, 304, 232, "LFO", cLfo());
        L(U8("drag onto any knob"), 1150, 363, 176, 7.5f, Theme::sub(), 1, juce::Justification::centredRight);
        for (int l = 0; l < 2; ++l) {
            int y0 = 392 + l * 96; const char* LP2[2] = { "l_", "l2_" }; std::string P2 = LP2[l];
            auto r = screen(1046, y0 + 42, 96, 40);
            int shapes = l == 0 ? 5 : 6, sh = juce::jlimit(0, shapes - 1, (int) (m.get(P2 + "shape") / 100.0 * (shapes - 0.01)));
            auto wf = [sh](float ph) { switch (sh) { case 0: return std::sin(6.2831853f * ph); case 1: return 1.0f - 4.0f * std::abs(ph - 0.5f); case 2: return 2.0f * ph - 1.0f;
                                                     case 3: return ph < 0.5f ? 1.0f : -1.0f; case 4: { int q = (int) (ph * 6); static const float H[6] = { 0.6f, -0.4f, 0.9f, -0.8f, 0.2f, -0.1f }; return H[q % 6]; }
                                                     default: { static const float H[7] = { 0.6f, -0.4f, 0.9f, -0.8f, 0.2f, -0.1f, 0.6f }; float x = ph * 6; int q = (int) x; float t2 = x - q; t2 = t2 * t2 * (3 - 2 * t2); return H[q] + (H[q + 1] - H[q]) * t2; } } };
            curve(r, wf, cLfo(), 0.9f);
            auto& syn = lfoSource(); float ph = syn.lfoPhase[l].load(), val = syn.lfoNow[l].load();
            float px = r.getX() + ph * r.getWidth(), py = r.getCentreY() - val * r.getHeight() * 0.42f;
            softLed(g, { px, py }, 3.2f, cLfo(), true);
            bool sync = m.get(P2 + "sync") > 0.5; float rate = (float) m.get(P2 + "rate");
            juce::String rt;
            if (sync) { static const char* N[7] = { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" }; rt = N[juce::jlimit(0, 6, (int) (rate / 100.0f * 6.99f))]; }
            else { float hz = 0.05f * std::pow(400.0f, rate / 100.0f); rt = juce::String(hz, hz < 1 ? 2 : 1) + " Hz"; }
            L(rt, 1252, y0 + 44, 82, 9.0f, Theme::txt(), 1, juce::Justification::centred);
        }
    } else if (page == 1) {
        auto env = [&](int x, int y, int w, int h, const char* pre, juce::Colour c) {
            auto r = screen(x, y, w, h);
            float a = (float) m.get(std::string(pre) + "a") / 100.0f, d = (float) m.get(std::string(pre) + "d") / 100.0f, s = (float) m.get(std::string(pre) + "s") / 100.0f, rl = (float) m.get(std::string(pre) + "r") / 100.0f;
            float tot = a + d + 0.5f + rl + 0.001f, W = r.getWidth();
            float x1 = r.getX() + W * a / tot, x2 = x1 + W * d / tot, x3 = x2 + W * 0.5f / tot, x4 = r.getRight();
            juce::Path p; p.startNewSubPath(r.getX(), r.getBottom()); p.lineTo(x1, r.getY()); p.quadraticTo(x1 + (x2 - x1) * 0.2f, r.getY() + r.getHeight() * (1 - s), x2, r.getBottom() - s * r.getHeight());
            p.lineTo(x3, r.getBottom() - s * r.getHeight()); p.quadraticTo(x3 + (x4 - x3) * 0.2f, r.getBottom(), x4, r.getBottom());
            g.setColour(c.withAlpha(0.18f)); juce::Path f = p; f.lineTo(r.getX(), r.getBottom()); f.closeSubPath(); g.fillPath(f);
            g.setColour(c); g.strokePath(p, juce::PathStrokeType(2.0f)); };
        const char* ET[3] = { "ENV 1 · LOUDNESS", "ENV 2 · FILTER / WAVE", "ENV 3 · ANYTHING" };
        const char* EP[3] = { "e1_", "e2_", "e3_" };
        for (int e = 0; e < 3; ++e) { card(24 + e * 306, 126, 294, 214, U8(ET[e]), cEnv()); env(38 + e * 306, 164, 266, 84, EP[e], cEnv()); }
        const char* LP[3] = { "l_", "l2_", "l3_" };
        for (int l = 0; l < 3; ++l) {
            card(24 + l * 306, 368, 294, 218, "LFO " + juce::String(l + 1), cLfo());
            auto r = screen(38 + l * 306, 404, l == 0 ? 118 : 266, 60);
            float sh = (float) m.get(std::string(LP[l]) + "shape"); int shapes = l == 0 ? 5 : 6; int s = juce::jlimit(0, shapes - 1, (int) (sh / 100.0f * (shapes - 0.01f)));
            juce::Random rnd(7 + l); float rv = 0, rv2 = 0;
            curve(r, [&](float ph) { switch (s) { case 0: return std::sin(6.2831853f * ph); case 1: return 1 - 4 * std::abs(ph - 0.5f); case 2: return 2 * ph - 1; case 3: return ph < 0.5f ? 1.0f : -1.0f;
                    case 4: if (ph < 0.01f) rv = rnd.nextFloat() * 2 - 1; return rv; default: if (ph < 0.01f) { rv = rv2; rv2 = rnd.nextFloat() * 2 - 1; } return rv + (rv2 - rv) * ph * ph * (3 - 2 * ph); } }, cLfo());
            L(m.get(std::string(LP[l]) + "sync") > 0.5 ? "in tempo" : "free", 38 + l * 306, 476, 120, 8.0f, Theme::sub(), 1);
        }
        card(930, 126, 406, 460, "MODULATION", Theme::violet());
        L(U8("source → knob, how much (drag)  ·  right-click a knob to remove"), 944, 150, 380, 7.5f, Theme::sub());
        L("MACROS", 944, 456, 100, 9.0f, Theme::txt(), 2);
    } else {
        card(24, 126, 620, 460, "ARPEGGIATOR", Theme::violet());
        L(U8("hold a chord - it plays the notes one after the other, on the metronome"), 200, 135, 420, 8.0f, Theme::sub());
        L("PATTERN", 44, 226, 200, 8.5f, Theme::txt(), 2); L("SPEED", 44, 298, 200, 8.5f, Theme::txt(), 2); L("RANGE", 44, 370, 200, 8.5f, Theme::txt(), 2);
        L(U8("HOLD: the notes keep playing after you let go - a new chord replaces them"), 44, 460, 560, 8.0f, Theme::sub());
        card(656, 126, 680, 460, "WAYS TO PLAY", Theme::blue());
        juce::StringArray tips {
            U8("KEYS  ·  the piano below, the computer keys (A W S E D F T G Y H U J K O L P · Z / X = octave), or a MIDI keyboard"),
            U8("SLIDE PAD  ·  left-right is the pitch (two octaves, gliding), up-down how loud - like a violin string with no frets"),
            U8("CHORDS  ·  eight chords of the piece's key (HALO hears the key) - one finger, a whole chord"),
            U8("SCALE GRID  ·  only the notes of the key, in rows a fourth apart: nothing sounds wrong"),
            U8("iPHONE  ·  the phone's SYNTH page plays all of these too, from anywhere in the room"),
            U8("FOLLOW  ·  (SOUND page) a voice sings exactly what your cello plays, an octave down or up") };
        for (int i = 0; i < tips.size(); ++i) L(tips[i], 676, 170 + i * 62, 640, 9.0f, Theme::txt(), 0);
    }
    for (auto& lb : labels) if (lb.page == page) slab(g, lb.t, lb.r, 7.5f, Theme::sub(), 1, juce::Justification::centred);
    L("C" + juce::String(octave - 1), 740, 609, 34, 8.5f, Theme::txt(), 2);
    L(keysMode == 0 ? U8("computer keys A W S E D …  ·  Z / X octave") : keysMode == 3 ? U8("neighbours sound good together: three touching circles = a chord")
                    : U8("in the key of the piece (what HALO hears; C until then)"), 776, 609, 330, 7.5f, Theme::sub());
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
        g.setColour(Theme::violet()); g.strokePath(p, juce::PathStrokeType(1.4f));
        juce::String where = cv.host.synthInto > 0 ? U8("→ LIVE ") + juce::String(cv.host.synthInto + 1) : juce::String("not playing anywhere (OFF)");
        L(where, 1130, 768, 196, 8.5f, cv.host.synthInto > 0 ? Theme::green() : Theme::amber(), 1);
    }
}

} // namespace arco
