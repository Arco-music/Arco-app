#include "InstrumentView.h"

namespace score {

static Project& P() { return *host().project; }

// ======================================================================== panel
Panel::Panel(juce::String t, ParamOwner* o, std::vector<std::string> ids, ColourFn c, Uid track, int cl, int dh, std::string onId)
    : title(std::move(t)), col(std::move(c)), cols(cl), displayH(dh) {
    for (auto& id : ids) {
        Param* p = o->find(id); if (!p) continue;
        if (p->unit == Unit::OnOff) { auto* x = new ParamToggle(p, juce::String(p->name).fromLastOccurrenceOf(" ", false, false), col); small.add(x); addAndMakeVisible(x); }
        else if (p->unit == Unit::Choice) { auto* x = new ParamChoice(p, col); x->setTooltip(juce::String(p->name)); small.add(x); addAndMakeVisible(x); }
        else { auto* k = new ParamKnob(p, col, track); knobOwn.add(k); knobs.push_back(k); addAndMakeVisible(k);
            // short names inside a panel: "OSC A POSITION" -> "POSITION"
            juce::String n(p->name); for (auto pre : { "OSC A ", "OSC B ", "OSC C ", "ENV 1 ", "ENV 2 ", "ENV 3 ", "ENV 4 ", "LFO 1 ", "LFO 2 ", "LFO 3 ", "LFO 4 ", "F1 ", "F2 ", "MACRO " })
                if (n.startsWith(pre)) n = n.substring((int) strlen(pre));
            k->label = n; }
    }
    if (!onId.empty()) if (Param* p = o->find(onId)) { onSwitch = std::make_unique<ParamToggle>(p, "ON", col); addAndMakeVisible(*onSwitch); }
}

void Panel::resized() {
    if (onSwitch) onSwitch->setBounds(getWidth() - 44, 7, 34, 17);
    auto r = content();
    display = displayH > 0 ? r.removeFromTop(displayH).toFloat() : juce::Rectangle<float>();
    if (displayH > 0) r.removeFromTop(6);
    if (!small.isEmpty()) {
        int per = juce::jmax(1, juce::jmin(small.size(), r.getWidth() / 74));
        int w = (r.getWidth() - (per - 1) * 4) / per;
        for (int i = 0; i < small.size(); ++i) small[i]->setBounds(r.getX() + (i % per) * (w + 4), r.getY() + (i / per) * 22, w, 18);
        r.removeFromTop(((small.size() + per - 1) / per) * 22 + 2);
    }
    int n = (int) knobs.size(); if (n == 0) return;
    int c = juce::jmax(1, juce::jmin(cols, n)), rows = (n + c - 1) / c;
    int cw = r.getWidth() / c, ch = juce::jmin(70, r.getHeight() / rows);
    for (int i = 0; i < n; ++i) knobs[(size_t) i]->setBounds(r.getX() + (i % c) * cw, r.getY() + (i / c) * ch, cw, ch);
}

void Panel::paint(juce::Graphics& g) {
    cardBg(g, getLocalBounds().toFloat(), 10);
    label(g, title, { 10, 6, getWidth() - 60, 18 }, 9.0f, 2, col ? col() : Theme::txt());
    if (displayH > 0) {
        g.setColour(Theme::scr()); g.fillRoundedRectangle(display, 6);
        if (drawDisplay) { g.saveState(); g.reduceClipRegion(display.toNearestInt()); drawDisplay(g, display); g.restoreState(); }
    }
}

// ======================================================================== tabs
TabPanel::TabPanel(juce::String t, std::vector<std::unique_ptr<Panel>> p, std::vector<juce::String> names, ColourFn c) : title(std::move(t)), pages(std::move(p)), col(std::move(c)) {
    for (size_t i = 0; i < pages.size(); ++i) {
        addChildComponent(*pages[i]);
        auto* b = new Pill(names[i]); b->fsize = 8.0f; b->radius = 4.0f; int k = (int) i;
        b->lit = [this, k] { return page == k; }; b->litColour = col;
        b->onClick = [this, k] { page = k; for (size_t j = 0; j < pages.size(); ++j) pages[j]->setVisible((int) j == page); repaint(); };
        tabs.add(b); addAndMakeVisible(b);
    }
    if (!pages.empty()) pages[0]->setVisible(true);
}
void TabPanel::resized() {
    int x = getWidth() - 8;
    for (int i = tabs.size() - 1; i >= 0; --i) { tabs[i]->setBounds(x - 40, 6, 38, 18); x -= 42; }
    for (auto& p : pages) p->setBounds(getLocalBounds());
}
void TabPanel::paint(juce::Graphics&) {}

// ======================================================================== mod matrix
ModMatrix::ModMatrix(Nova& n, ColourFn c, Uid t) : nova(n), col(std::move(c)), track(t) {
    for (int s = 0; s < Nova::NSLOT; ++s) {
        auto* a = new ParamChoice(n.find("ms" + std::to_string(s) + "s"), col); src.add(a); addChildComponent(a);
        auto* b = new ParamChoice(n.find("ms" + std::to_string(s) + "d"), col); dst.add(b); addChildComponent(b);
    }
}
juce::Rectangle<int> ModMatrix::amountRect(int row) const {
    int y = 32 + (row - first) * 24; int w = getWidth() - 20; int x0 = 10 + 24 + (w - 24) * 2 / 5 * 2 / 2;
    return { 10 + 24 + (w - 24) * 4 / 5 - 0 + 4, y, getWidth() - 10 - (10 + 24 + (w - 24) * 4 / 5 + 4), 20 };
}
void ModMatrix::resized() {
    int w = getWidth() - 20, colW = (w - 24) * 2 / 5;
    for (int s = 0; s < Nova::NSLOT; ++s) {
        bool vis = s >= first && s < first + visible();
        src[s]->setVisible(vis); dst[s]->setVisible(vis);
        if (!vis) continue;
        int y = 32 + (s - first) * 24;
        src[s]->setBounds(10 + 24, y, colW - 4, 20); dst[s]->setBounds(10 + 24 + colW, y, colW - 4, 20);
    }
}
void ModMatrix::paint(juce::Graphics& g) {
    cardBg(g, getLocalBounds().toFloat(), 10);
    label(g, "MODULATION", { 10, 6, 140, 18 }, 9.0f, 2, col());
    label(g, U("source  →  target  ·  amount (drag)  ·  scroll for all 16"), { 120, 6, getWidth() - 130, 18 }, 7.5f, 0, Theme::sub(), juce::Justification::centredRight);
    for (int s = first; s < juce::jmin(Nova::NSLOT, first + visible()); ++s) {
        int y = 32 + (s - first) * 24;
        float a = nova.find("ms" + std::to_string(s) + "a")->get();
        bool active = nova.find("ms" + std::to_string(s) + "s")->geti() > 0 && nova.find("ms" + std::to_string(s) + "d")->geti() > 0 && std::abs(a) > 0.01f;
        label(g, juce::String(s + 1), { 10, y, 22, 20 }, 8.0f, 2, active ? col() : Theme::sub());
        auto r = amountRect(s).toFloat();
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 4);
        float cx = r.getCentreX(), w = r.getWidth() * 0.5f * a / 100.0f;
        g.setColour(col().withAlpha(active ? 0.9f : 0.4f)); g.fillRoundedRectangle(w >= 0 ? cx : cx + w, r.getY() + 3, std::abs(w), r.getHeight() - 6, 2);
        g.setColour(Theme::line()); g.drawVerticalLine((int) cx, r.getY() + 2, r.getBottom() - 2);
        label(g, juce::String((int) std::lround(a)) + "%", r.toNearestInt().reduced(5, 0), 7.5f, 2, Theme::txt(), juce::Justification::centredRight);
    }
}
void ModMatrix::mouseDown(const juce::MouseEvent& e) {
    dragRow = -1;
    for (int s = first; s < juce::jmin(Nova::NSLOT, first + visible()); ++s) if (amountRect(s).contains(e.getPosition())) {
        dragRow = s; host().knobTouched();
        auto r = amountRect(s); float v = juce::jlimit(-100.0f, 100.0f, (e.position.x - r.getCentreX()) / (r.getWidth() * 0.5f) * 100.0f);
        nova.find("ms" + std::to_string(s) + "a")->set(v); startV = v; repaint();
    }
}
void ModMatrix::mouseDrag(const juce::MouseEvent& e) {
    if (dragRow < 0) return;
    auto r = amountRect(dragRow); float v = juce::jlimit(-100.0f, 100.0f, (e.position.x - r.getCentreX()) / (r.getWidth() * 0.5f) * 100.0f);
    nova.find("ms" + std::to_string(dragRow) + "a")->set(std::abs(v) < 3 ? 0.0f : v); repaint();
}
void ModMatrix::mouseDoubleClick(const juce::MouseEvent&) { if (dragRow >= 0) { nova.find("ms" + std::to_string(dragRow) + "a")->set(0); repaint(); } }

// ======================================================================== drawings
static void drawTable(juce::Graphics& g, juce::Rectangle<float> r, const Wavetable* t, float pos, juce::Colour c) {
    if (!t) { label(g, "drop a sound here: it becomes this oscillator's wavetable", r.toNearestInt().reduced(8, 0), 8.0f, 1, Theme::sub(), juce::Justification::centred); return; }
    int frames = 9;
    for (int f = frames - 1; f >= 0; --f) {
        float fp = f / (float) (frames - 1);
        auto pts = t->preview(fp, 96);
        float ox = r.getX() + 10 + fp * r.getWidth() * 0.16f, oy = r.getBottom() - 12 - fp * r.getHeight() * 0.32f;
        float w = r.getWidth() * 0.76f, h = r.getHeight() * 0.28f;
        juce::Path p; for (size_t i = 0; i < pts.size(); ++i) { float x = ox + w * i / (float) (pts.size() - 1), y = oy - h - pts[i] * h; if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
        g.setColour(c.withAlpha(0.18f)); g.strokePath(p, juce::PathStrokeType(1.0f));
    }
    auto pts = t->preview(pos, 128);
    float ox = r.getX() + 10 + pos * r.getWidth() * 0.16f, oy = r.getBottom() - 12 - pos * r.getHeight() * 0.32f, w = r.getWidth() * 0.76f, h = r.getHeight() * 0.28f;
    juce::Path p; for (size_t i = 0; i < pts.size(); ++i) { float x = ox + w * i / (float) (pts.size() - 1), y = oy - h - pts[i] * h; if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
    g.setColour(c); g.strokePath(p, juce::PathStrokeType(2.0f));
    label(g, juce::String(t->name), r.toNearestInt().reduced(6, 3).withHeight(12), 7.5f, 2, Theme::sub(), juce::Justification::topRight);
}

static float filterMag(int type, double f, double fc, double res) {
    double x = f / fc, q = 0.6 + res / 100.0 * 9.0;
    auto lp2 = [&](double xx) { return 1.0 / std::sqrt(std::pow(1 - xx * xx, 2) + std::pow(xx / q, 2)); };
    auto hp2 = [&](double xx) { return xx * xx / std::sqrt(std::pow(1 - xx * xx, 2) + std::pow(xx / q, 2)); };
    auto bp = [&](double xx) { return (xx / q) / std::sqrt(std::pow(1 - xx * xx, 2) + std::pow(xx / q, 2)); };
    double m = 1;
    switch (type) {
        case 0: m = lp2(x); break; case 1: case 6: m = lp2(x) * (1.0 / std::sqrt(1 + std::pow(x, 4))); if (type == 6) m *= 1 + res / 40.0 * (std::abs(x - 1) < 0.15 ? 1 : 0); break;
        case 2: m = hp2(x); break; case 3: m = hp2(x) * (x * x / std::sqrt(1 + std::pow(x, 4))); break;
        case 4: case 9: m = bp(x) * (type == 9 ? 1.0 + 0.5 * bp(x * 2.2) : 1.0); break;
        case 5: m = std::abs(1 - x * x) / std::sqrt(std::pow(1 - x * x, 2) + std::pow(x / q, 2)); break;
        case 7: case 8: { double ph = juce::MathConstants<double>::twoPi * f / fc; double fb = 0.3 + res / 100.0 * 0.6 * (type == 8 ? -1 : 1);
            m = 1.0 / std::sqrt(1 + fb * fb - 2 * fb * std::cos(ph)); break; }
        default: break;
    }
    return (float) juce::Decibels::gainToDecibels(m, -60.0);
}

static void drawFilter(juce::Graphics& g, juce::Rectangle<float> r, Nova& n, int k, juce::Colour c) {
    std::string p = k == 1 ? "f1" : "f2";
    bool on = n.find(p + "on")->on();
    int ty = n.find(p + "ty")->geti(); double fc = n.find(p + "c")->get(), res = n.find(p + "r")->get();
    juce::Path path;
    for (int x = 0; x <= (int) r.getWidth(); x += 2) {
        double f = 20.0 * std::pow(1000.0, x / r.getWidth());
        float db = juce::jlimit(-36.0f, 24.0f, filterMag(ty, f, fc, res));
        float y = r.getY() + r.getHeight() * 0.35f - db / 36.0f * r.getHeight() * 0.6f;
        if (x == 0) path.startNewSubPath(r.getX() + x, y); else path.lineTo(r.getX() + x, y);
    }
    juce::Path fill = path; fill.lineTo(r.getRight(), r.getBottom()); fill.lineTo(r.getX(), r.getBottom()); fill.closeSubPath();
    g.setColour(c.withAlpha(on ? 0.15f : 0.05f)); g.fillPath(fill);
    g.setColour(c.withAlpha(on ? 1.0f : 0.3f)); g.strokePath(path, juce::PathStrokeType(1.8f));
    if (!on) label(g, "OFF", r.toNearestInt(), 10.0f, 2, Theme::sub(), juce::Justification::centred);
}

static void drawEnv(juce::Graphics& g, juce::Rectangle<float> r, Nova& n, int e, juce::Colour c) {
    std::string p = "e" + std::to_string(e);
    float a = n.find(p + "a")->get(), d = n.find(p + "d")->get(), s = n.find(p + "s")->get() / 100.0f, rl = n.find(p + "r")->get(), cv = n.find(p + "c")->get() / 100.0f;
    auto tx = [](float ms) { return std::log10(1.0f + ms) / std::log10(1.0f + 20000.0f); };
    float W = r.getWidth() - 16, ta = tx(a) * W * 0.3f, td = tx(d) * W * 0.3f, ts = W * 0.12f, tr = tx(rl) * W * 0.28f;
    float x0 = r.getX() + 8, yb = r.getBottom() - 6, yt = r.getY() + 8, H = yb - yt;
    auto seg = [&](juce::Path& path, float xa, float ya, float xb, float yb2) { for (int i = 1; i <= 16; ++i) { float t = i / 16.0f; float k = cv >= 0 ? std::pow(t, 1 + cv * 2) : 1 - std::pow(1 - t, 1 - cv * 2);
        path.lineTo(xa + (xb - xa) * t, ya + (yb2 - ya) * k); } };
    juce::Path path; path.startNewSubPath(x0, yb);
    seg(path, x0, yb, x0 + ta, yt);
    seg(path, x0 + ta, yt, x0 + ta + td, yb - H * s);
    path.lineTo(x0 + ta + td + ts, yb - H * s);
    seg(path, x0 + ta + td + ts, yb - H * s, x0 + ta + td + ts + tr, yb);
    juce::Path fill = path; fill.lineTo(x0, yb); fill.closeSubPath();
    g.setColour(c.withAlpha(0.15f)); g.fillPath(fill); g.setColour(c); g.strokePath(path, juce::PathStrokeType(1.8f));
    float v = n.envView[e - 1]; g.setColour(Theme::txt()); g.fillRect(r.getRight() - 6, yb - H * v, 3.0f, H * v);
}

static void drawLfo(juce::Graphics& g, juce::Rectangle<float> r, Nova& n, int l, juce::Colour c) {
    int sh = n.find("l" + std::to_string(l) + "sh")->geti();
    juce::Path p; juce::Random rnd(7);
    float prev = 0;
    for (int i = 0; i <= 120; ++i) {
        float t = i / 120.0f * 2.0f, ph = std::fmod(t, 1.0f), v = 0;
        switch (sh) { case 0: v = std::sin(ph * 6.2832f); break; case 1: v = 1 - 4 * std::abs(ph - 0.5f) ; v = -v; v = 4 * std::abs(ph - 0.5f) - 1; break;
            case 2: v = 2 * ph - 1; break; case 3: v = 1 - 2 * ph; break; case 4: v = ph < 0.5f ? 1.0f : -1.0f; break;
            case 5: if (i % 15 == 0) prev = rnd.nextFloat() * 2 - 1; v = prev; break; default: if (i % 15 == 0) prev = rnd.nextFloat() * 2 - 1; v = prev * 0.8f; break; }
        float x = r.getX() + 6 + (r.getWidth() - 12) * i / 120.0f, y = r.getCentreY() - v * r.getHeight() * 0.38f;
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    if (sh == 6) p = p.createPathWithRoundedCorners(12.0f);
    g.setColour(c); g.strokePath(p, juce::PathStrokeType(1.8f));
    float v = n.lfoView[l - 1]; g.setColour(Theme::txt()); g.fillEllipse(r.getRight() - 14, r.getCentreY() - v * r.getHeight() * 0.38f - 4, 8, 8);
}

// ======================================================================== the window
static juce::File userPresetFolder(const std::string& type) {
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SCORE").getChildFile("Presets").getChildFile(type);
    f.createDirectory(); return f;
}

InstrumentEditor::InstrumentEditor(Track* t) : track(t) {
    setWantsKeyboardFocus(true);
    for (auto* p : { &close, &preset, &prev, &next, &save, &load }) { addAndMakeVisible(*p); p->fsize = 9.0f; p->radius = 6.0f; }
    close.onClick = [this] { if (onClose) onClose(); };
    preset.dynText = [this] { return presetName.isEmpty() ? juce::String("SOUNDS") : presetName; };
    preset.onClick = [this] { presetMenu(); };
    prev.onClick = [this] { stepPreset(-1); }; next.onClick = [this] { stepPreset(1); };
    save.onClick = [this] {
        auto* aw = new juce::AlertWindow("Save the sound", "A name for it (it appears under MY SOUNDS):", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor("n", presetName.isEmpty() ? "My sound" : presetName); aw->addButton("Save", 1); aw->addButton("Cancel", 0);
        aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int b) {
            if (b != 1) return; auto nm = juce::File::createLegalFileName(aw->getTextEditorContents("n").trim()); if (nm.isEmpty()) return;
            auto f = userPresetFolder(track->inst->type).getChildFile(nm + ".json");
            f.replaceWithText(juce::JSON::toString(paramsToVar(*track->inst)));
            presetName = nm; host().say("saved: " + nm); repaint(); }), true);
    };
    load.setVisible(t->inst->type != "nova");
    load.onClick = [this] { loadSampleInto(track->inst->type == "drm" ? selPad : -1); };
    keys = std::make_unique<juce::MidiKeyboardComponent>(host().keyboard, juce::MidiKeyboardComponent::horizontalKeyboard);
    keys->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, Theme::lightMode() ? juce::Colours::white : Theme::rgb(0.86f, 0.87f, 0.9f));
    keys->setColour(juce::MidiKeyboardComponent::blackNoteColourId, Theme::rgb(0.1f, 0.1f, 0.12f));
    keys->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::argb(t->colour));
    keys->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::argb(t->colour).withAlpha(0.3f));
    keys->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::line());
    keys->setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keys->setLowestVisibleKey(36); keys->setKeyWidth(18); keys->setWantsKeyboardFocus(false);
    addAndMakeVisible(*keys);
    host().engine.keysTarget.store(t->id);
    if (auto* n = dynamic_cast<Nova*>(t->inst.get())) buildNova(*n);
    else if (auto* s = dynamic_cast<Sampler*>(t->inst.get())) buildSampler(*s);
    else if (auto* d = dynamic_cast<Drums*>(t->inst.get())) buildDrums(*d);
    startTimerHz(30);
}
InstrumentEditor::~InstrumentEditor() { host().allNotesOff(); }

void InstrumentEditor::buildNova(Nova& n) {
    Uid tid = track->id;
    auto col = [this] { return Theme::argb(track->colour); };
    const char* O[3] = { "o1", "o2", "o3" }; const char* ON[3] = { "OSC A", "OSC B", "OSC C" };
    juce::Colour oc[3] = { Theme::rgb(0.45f, 0.62f, 1.0f), Theme::rgb(0.70f, 0.52f, 1.0f), Theme::rgb(0.35f, 0.82f, 0.80f) };
    for (int k = 0; k < 3; ++k) {
        std::string p = O[k]; auto c = oc[k];
        auto* pn = new Panel(ON[k], &n, { p + "tab", p + "warp", p + "flt", p + "pos", p + "wamt", p + "lvl", p + "pan", p + "oct", p + "semi", p + "fine", p + "uni", p + "det", p + "spr", p + "ph" },
                             [c] { return Theme::c(c); }, tid, 7, 74, p + "on");
        Nova* np = &n;
        pn->drawDisplay = [np, k, p, c](juce::Graphics& g, juce::Rectangle<float> r) {
            int ti = np->find(p + "tab")->geti(); auto& ft = factoryTables();
            const Wavetable* t = ti < (int) ft.size() ? ft[(size_t) ti].get() : np->userTable[k].get();
            drawTable(g, r, t, np->find(p + "pos")->get() / 100.0f, np->find(p + "on")->on() ? Theme::c(c) : Theme::off()); };
        pn->displayMouse = [np, p](const juce::MouseEvent& e, juce::Rectangle<float>) {     // drag on the picture: the position
            static float st = 0; if (e.mouseWasDraggedSinceMouseDown()) np->find(p + "pos")->set(juce::jlimit(0.0f, 100.0f, st - e.getDistanceFromDragStartY() * 0.5f)); else { st = np->find(p + "pos")->get(); host().knobTouched(); } };
        parts.add(pn);
    }
    parts.add(new Panel("SUB + NOISE", &n, { "suboct", "subsh", "nflt", "sub", "noise", "ncol" }, [] { return Theme::c(Theme::rgb(0.6f, 0.65f, 0.75f)); }, tid, 3));
    for (int k = 1; k <= 2; ++k) {
        std::string p = k == 1 ? "f1" : "f2"; auto c = k == 1 ? Theme::rgb(1.0f, 0.58f, 0.32f) : Theme::rgb(1.0f, 0.42f, 0.5f);
        auto* pn = new Panel(k == 1 ? "FILTER 1" : "FILTER 2", &n, { p + "ty", p + "c", p + "r", p + "d", p + "k" }, [c] { return Theme::c(c); }, tid, 4, 56, p + "on");
        Nova* np = &n; pn->drawDisplay = [np, k, c](juce::Graphics& g, juce::Rectangle<float> r) { drawFilter(g, r, *np, k, Theme::c(c)); };
        parts.add(pn);
    }
    {   std::vector<std::unique_ptr<Panel>> pages; std::vector<juce::String> names;
        auto c = Theme::rgb(0.35f, 0.82f, 0.52f);
        for (int e = 1; e <= 4; ++e) { std::string p = "e" + std::to_string(e);
            auto pn = std::make_unique<Panel>(e == 1 ? U("ENV 1 · VOLUME") : U("ENVELOPE ") + juce::String(e), &n, std::vector<std::string> { p + "a", p + "d", p + "s", p + "r", p + "c" }, [c] { return Theme::c(c); }, tid, 5, 56);
            Nova* np = &n; pn->drawDisplay = [np, e, c](juce::Graphics& g, juce::Rectangle<float> r) { drawEnv(g, r, *np, e, Theme::c(c)); };
            pages.push_back(std::move(pn)); names.push_back(juce::String(e)); }
        parts.add(new TabPanel("ENV", std::move(pages), names, [c] { return Theme::c(c); }));
    }
    {   std::vector<std::unique_ptr<Panel>> pages; std::vector<juce::String> names;
        auto c = Theme::rgb(0.95f, 0.78f, 0.3f);
        for (int l = 1; l <= 4; ++l) { std::string p = "l" + std::to_string(l);
            auto pn = std::make_unique<Panel>("LFO", &n, std::vector<std::string> { p + "sh", p + "div", p + "mode", p + "sync", p + "rate" }, [c] { return Theme::c(c); }, tid, 2, 56);
            Nova* np = &n; pn->drawDisplay = [np, l, c](juce::Graphics& g, juce::Rectangle<float> r) { drawLfo(g, r, *np, l, Theme::c(c)); };
            pages.push_back(std::move(pn)); names.push_back(juce::String(l)); }
        parts.add(new TabPanel("LFO", std::move(pages), names, [c] { return Theme::c(c); }));
    }
    parts.add(new ModMatrix(n, [] { return Theme::violet(); }, tid));
    parts.add(new Panel("MACROS", &n, { "m1", "m2", "m3", "m4" }, [] { return Theme::gold(); }, tid, 2));
    parts.add(new Panel("VOICE", &n, { "vmode", "froute", "poly", "glide", "vel", "bendr", "lvl", "pan" }, col, tid, 3));
    parts.add(new Panel("ARPEGGIATOR", &n, { "arp", "arpl", "arpm", "arpr", "arpo", "arpg", "arps" }, [] { return Theme::cyan(); }, tid, 2));
    // the synth's own effects, as cards
    {   auto* fxp = new Panel("SYNTH FX", &n, { "fxdrv", "fxcho", "fxdly", "fxrev" }, [] { return Theme::c(Theme::rgb(0.9f, 0.45f, 0.6f)); }, tid, 1);
        std::vector<std::string> show[4] = { { "dr", "tone", "out" }, { "rate", "depth", "mix" }, { "time", "fb", "mix" }, { "size", "decay", "mix" } };
        auto fx = n.internalEffects();
        for (int i = 0; i < 4; ++i) for (auto& id : show[i]) if (auto* q = fx[(size_t) i]->find(id)) { auto* k = new ParamKnob(q, [] { return Theme::c(Theme::rgb(0.9f, 0.45f, 0.6f)); }, tid);
            k->label = juce::String(q->name); k->setComponentID(juce::String(i)); fxp->extra.add(k); fxp->addAndMakeVisible(k); }
        parts.add(fxp);
    }
    layout = [this] {
        auto r = getLocalBounds().reduced(14, 0).withTrimmedTop(48).withTrimmedBottom(84);
        int h1 = juce::jmax(200, r.getHeight() * 38 / 100), h2 = juce::jmax(160, r.getHeight() * 30 / 100);
        auto row1 = r.removeFromTop(h1); r.removeFromTop(8);
        auto row2 = r.removeFromTop(h2); r.removeFromTop(8);
        auto row3 = r;
        int sw = 150, ow = (row1.getWidth() - sw - 24) / 3;
        for (int k = 0; k < 3; ++k) { parts[k]->setBounds(row1.removeFromLeft(ow)); row1.removeFromLeft(8); }
        parts[3]->setBounds(row1);
        int w2 = (row2.getWidth() - 24) / 4;
        for (int k = 4; k < 8; ++k) { parts[k]->setBounds(row2.removeFromLeft(w2)); row2.removeFromLeft(8); }
        parts[8]->setBounds(row3.removeFromLeft(row3.getWidth() * 29 / 100)); row3.removeFromLeft(8);
        auto right = row3;
        int a = right.getWidth();
        parts[9]->setBounds(right.removeFromLeft(a * 15 / 100)); right.removeFromLeft(8);
        parts[10]->setBounds(right.removeFromLeft(a * 25 / 100)); right.removeFromLeft(8);
        parts[11]->setBounds(right.removeFromLeft(a * 19 / 100)); right.removeFromLeft(8);
        parts[12]->setBounds(right);
        // synth fx: toggles stacked left, three knobs per effect to the right
        if (auto* fxp = dynamic_cast<Panel*>(parts[12])) {
            auto c = fxp->content(); int rowH = c.getHeight() / 4;
            for (auto* k : fxp->extra) { int i = k->getComponentID().getIntValue(); int j = 0; for (auto* q : fxp->extra) { if (q == k) break; if (q->getComponentID() == k->getComponentID()) ++j; }
                k->setBounds(c.getX() + 78 + j * ((c.getWidth() - 78) / 3), c.getY() + i * rowH, (c.getWidth() - 78) / 3, rowH); }
            int ti = 0; for (auto* ch : fxp->getChildren()) if (auto* tg = dynamic_cast<ParamToggle*>(ch)) { tg->setBounds(c.getX(), c.getY() + ti * rowH + rowH / 2 - 10, 70, 20); ++ti; }
        }
    };
    for (auto* p : parts) addAndMakeVisible(p);
    // the synth fx toggles are laid out as a column in that panel
    if (auto* fxp = dynamic_cast<Panel*>(parts[12])) fxp->cols = 1;
}

void InstrumentEditor::buildSampler(Sampler& s) {
    Uid tid = track->id; auto col = [this] { return Theme::argb(track->colour); };
    auto* wave = new Panel("SOUND", &s, {}, col, tid, 1, 10);
    Sampler* sp = &s;
    wave->drawDisplay = [sp, this](juce::Graphics& g, juce::Rectangle<float> r) {
        auto smp = sp->sample(); auto c = Theme::argb(track->colour);
        if (!smp || smp->length() == 0) { label(g, U("drop a sound file here, or LOAD SOUND · or in the arranger: right-click a clip → Play it in a SAMPLER"), r.toNearestInt(), 10.0f, 1, Theme::sub(), juce::Justification::centred); return; }
        int W = (int) r.getWidth(); float st = sp->find("start")->get() / 100.0f, en = sp->find("end")->get() / 100.0f;
        bool loop = sp->find("loop")->on(); float ls = sp->find("ls")->get() / 100.0f, le = sp->find("le")->get() / 100.0f;
        for (int x = 0; x < W; ++x) { int a = (int) ((int64_t) smp->length() * x / W), b = (int) ((int64_t) smp->length() * (x + 1) / W); float pk = 0;
            for (int i = a; i < b; i += 2) pk = std::max(pk, std::abs(smp->L[(size_t) i])); float hh = pk * r.getHeight() * 0.46f; float t = x / (float) W;
            g.setColour(t >= st && t <= en ? c : Theme::off()); g.fillRect(r.getX() + x, r.getCentreY() - hh, 1.0f, hh * 2 + 0.5f); }
        g.setColour(Theme::gold()); g.fillRect(r.getX() + st * r.getWidth() - 1, r.getY(), 2.0f, r.getHeight()); g.fillRect(r.getX() + en * r.getWidth() - 1, r.getY(), 2.0f, r.getHeight());
        label(g, "START", { (int) (r.getX() + st * r.getWidth()) + 4, (int) r.getY() + 4, 50, 12 }, 7.5f, 2, Theme::gold());
        if (loop) { g.setColour(Theme::cyan().withAlpha(0.18f)); g.fillRect(r.getX() + ls * r.getWidth(), r.getY(), (le - ls) * r.getWidth(), r.getHeight());
            label(g, "LOOP", { (int) (r.getX() + ls * r.getWidth()) + 4, (int) r.getBottom() - 16, 50, 12 }, 7.5f, 2, Theme::cyan()); }
        if (sp->find("mode")->geti() == 1) { int ns = sp->find("nsl")->geti(); bool tr = sp->find("slm")->geti() == 1 && !smp->slices.empty();
            g.setColour(Theme::txt().withAlpha(0.5f));
            if (tr) for (int k = 0; k < juce::jmin(ns, (int) smp->slices.size()); ++k) g.fillRect(r.getX() + smp->slices[(size_t) k] / (float) smp->length() * r.getWidth(), r.getY(), 1.0f, r.getHeight());
            else for (int k = 1; k < ns; ++k) g.fillRect(r.getX() + (st + (en - st) * k / ns) * r.getWidth(), r.getY(), 1.0f, r.getHeight()); }
        for (auto& pp : sp->playPos) { float v = pp.load(); if (v >= 0) { g.setColour(Theme::txt()); g.fillRect(r.getX() + v * r.getWidth(), r.getY(), 1.5f, r.getHeight()); } }
        label(g, juce::String(smp->name) + U("  ·  ") + juce::String(smp->length() / smp->sr, 2) + " s", r.toNearestInt().reduced(6, 4).withHeight(12), 8.0f, 1, Theme::sub(), juce::Justification::topRight);
    };
    wave->displayMouse = [sp](const juce::MouseEvent& e, juce::Rectangle<float> r) {     // drag near START / END to move them
        float t = juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()) * 100.0f;
        static int which = 0;
        if (!e.mouseWasDraggedSinceMouseDown()) { host().knobTouched(); float st = sp->find("start")->get(), en = sp->find("end")->get(); which = std::abs(t - st) < std::abs(t - en) ? 0 : 1; }
        if (which == 0) sp->find("start")->set(std::min(t, sp->find("end")->get() - 0.5f)); else sp->find("end")->set(std::max(t, sp->find("start")->get() + 0.5f)); };
    parts.add(wave);
    parts.add(new Panel("PLAY", &s, { "mode", "slm", "rev", "root", "tune", "fine", "nsl" }, col, tid, 4));
    parts.add(new Panel("LOOP", &s, { "loop", "ls", "le", "xf" }, [] { return Theme::cyan(); }, tid, 3));
    parts.add(new Panel("AMP", &s, { "a", "d", "s", "r", "vel" }, [] { return Theme::green(); }, tid, 3));
    parts.add(new Panel("FILTER", &s, { "fty", "fc", "fr", "fenv" }, [] { return Theme::c(Theme::rgb(1.0f, 0.58f, 0.32f)); }, tid, 3));
    parts.add(new Panel("OUTPUT", &s, { "poly", "lvl", "pan", "start", "end" }, col, tid, 3));
    parts.add(new Panel("ARPEGGIATOR", &s, { "arp", "arpl", "arpm", "arpr", "arpo", "arpg", "arps" }, [] { return Theme::cyan(); }, tid, 2));
    layout = [this] {
        auto r = getLocalBounds().reduced(14, 0).withTrimmedTop(48).withTrimmedBottom(84);
        auto top = r.removeFromTop(r.getHeight() * 52 / 100);
        if (auto* w = dynamic_cast<Panel*>(parts[0])) w->displayH = top.getHeight() - 40;
        parts[0]->setBounds(top); r.removeFromTop(8);
        int w = (r.getWidth() - 40) / 6;
        for (int k = 1; k < 7; ++k) { parts[k]->setBounds(r.removeFromLeft(k == 6 ? r.getWidth() : w)); r.removeFromLeft(8); }
    };
    for (auto* p : parts) addAndMakeVisible(p);
}

void InstrumentEditor::buildDrums(Drums& d) {
    Uid tid = track->id; auto col = [this] { return Theme::argb(track->colour); };
    // the pads
    struct Pads : public juce::Component, public juce::FileDragAndDropTarget {
        Pads(InstrumentEditor& e, Drums& dr) : ed(e), d(dr) {}
        InstrumentEditor& ed; Drums& d;
        juce::Rectangle<float> pad(int k) const { int c = k % 4, r = 3 - k / 4; float w = (getWidth() - 30) / 4.0f, h = (getHeight() - 40) / 4.0f;
            return { 10 + c * (w + 3.3f), 30 + r * (h + 3.3f), w, h }; }
        void paint(juce::Graphics& g) override {
            cardBg(g, getLocalBounds().toFloat(), 10);
            label(g, "PADS", { 10, 6, 100, 18 }, 9.0f, 2, Theme::argb(ed.track->colour));
            label(g, U("click plays · drop a sound on a pad · notes C2–D#3"), { 60, 6, getWidth() - 70, 18 }, 7.5f, 0, Theme::sub(), juce::Justification::centredRight);
            auto c = Theme::argb(ed.track->colour);
            for (int k = 0; k < 16; ++k) { auto r = pad(k); float h = d.hit[(size_t) k].load(); bool s = ed.selectedPad() == k;
                g.setColour(Theme::card2().interpolatedWith(c, 0.15f + 0.7f * h)); g.fillRoundedRectangle(r, 8);
                if (s) { g.setColour(c); g.drawRoundedRectangle(r.reduced(1), 8, 2); }
                int v = d.find("p" + std::to_string(k) + "v")->geti(); auto sm = d.padSample(k);
                label(g, v == 16 && sm ? juce::String(sm->name) : juce::String(Drums::padName(k, v)), r.toNearestInt().reduced(8, 6).withHeight(14), 8.5f, 2, Theme::txt());
                label(g, juce::MidiMessage::getMidiNoteName(36 + k, true, true, 4), r.toNearestInt().reduced(8, 6), 7.0f, 1, Theme::sub(), juce::Justification::bottomRight); }
        }
        void mouseDown(const juce::MouseEvent& e) override { for (int k = 0; k < 16; ++k) if (pad(k).contains(e.position)) { ed.selectPad(k); host().engine.keysTarget.store(ed.track->id); host().noteOn(36 + k, 0.4f + 0.6f * juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - pad(k).getY()) / pad(k).getHeight())); held = 36 + k; } }
        void mouseUp(const juce::MouseEvent&) override { if (held >= 0) host().noteOff(held); held = -1; }
        bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
        void filesDropped(const juce::StringArray& f, int x, int y) override { for (int k = 0; k < 16; ++k) if (pad(k).contains((float) x, (float) y)) { ed.selectPad(k); ed.filesDropped(f, -1, -1); } }
        int held = -1;
    };
    parts.add(new Pads(*this, d));
    // the step sequencer: writes notes into the selected clip of this track (a one-bar clip is made when there is none)
    struct Steps : public juce::Component {
        Steps(InstrumentEditor& e) : ed(e) {}
        InstrumentEditor& ed; int steps = 16; int paintMode = -1;
        Clip* clip() const { Track* t = nullptr; Clip* c = host().selectedClip(&t); return c && c->midi && t == ed.track ? c : nullptr; }
        juce::Rectangle<float> cell(int row, int s) const { float x0 = 92, w = (getWidth() - x0 - 10) / (float) steps, h = (getHeight() - 36) / 16.0f;
            return { x0 + s * w, 30 + (15 - row) * h, w - 2, h - 2 }; }
        double stepLen() const { return P().beatsPerBar / (double) steps; }
        bool has(Clip* c, int row, int s) const { if (!c) return false; double a = s * stepLen(); for (auto& n : c->notes) if (n.pitch == 36 + row && std::abs(n.start - a) < stepLen() * 0.5) return true; return false; }
        void paint(juce::Graphics& g) override {
            cardBg(g, getLocalBounds().toFloat(), 10);
            auto c = Theme::argb(ed.track->colour); Clip* cl = clip();
            label(g, "STEPS", { 10, 6, 80, 18 }, 9.0f, 2, c);
            label(g, cl ? U("editing “") + cl->name + U("” (first bar) · click or drag to set steps") : U("click a step: a one-bar clip is made at the playhead"), { 80, 6, getWidth() - 90, 18 }, 7.5f, 0, Theme::sub(), juce::Justification::centredRight);
            auto* dr = dynamic_cast<Drums*>(ed.track->inst.get());
            double pos = host().engine.position(); int now = cl && pos >= cl->start && pos < cl->end() ? (int) (std::fmod(pos - cl->start, (double) P().beatsPerBar) / stepLen()) : -1;
            for (int row = 0; row < 16; ++row) {
                auto lr = cell(row, 0).withX(10).withWidth(78);
                int v = dr ? dr->find("p" + std::to_string(row) + "v")->geti() : 0;
                label(g, Drums::padName(row, v), lr.toNearestInt(), 7.5f, row == ed.selectedPad() ? 2 : 1, row == ed.selectedPad() ? c : Theme::sub());
                for (int s = 0; s < steps; ++s) { auto r = cell(row, s); bool on = has(cl, row, s);
                    g.setColour(on ? c : (s / 4 % 2 ? Theme::card2() : Theme::card2().brighter(0.06f))); if (s == now) g.setColour(on ? c.brighter(0.4f) : Theme::line()); g.fillRoundedRectangle(r, 2.5f); }
            }
        }
        void toggle(int row, int s, bool on) {
            Clip* cl = clip();
            if (!cl) {
                Uid made = 0; Track* t = ed.track; double b = std::floor(host().engine.position() / P().beatsPerBar) * P().beatsPerBar;
                host().edit("new drum clip", [&] { auto nc = P().newMidiClip(b, P().beatsPerBar); nc->name = "Beat"; t->clips.push_back(nc); made = nc->id; });
                host().selectClip(made); cl = clip(); if (!cl) return;
            }
            double a = s * stepLen();
            auto& N = cl->notes;
            N.erase(std::remove_if(N.begin(), N.end(), [&](const Note& n) { return n.pitch == 36 + row && std::abs(n.start - a) < stepLen() * 0.5; }), N.end());
            if (on) { Note n; n.start = a; n.len = stepLen() * 0.9; n.pitch = 36 + row; n.vel = s % 4 == 0 ? 0.95f : 0.75f; N.push_back(n); }
            host().rebuild(); repaint();
        }
        void mouseDown(const juce::MouseEvent& e) override { host().beginGesture(); for (int row = 0; row < 16; ++row) for (int s = 0; s < steps; ++s) if (cell(row, s).contains(e.position)) {
            paintMode = has(clip(), row, s) ? 0 : 1; toggle(row, s, paintMode == 1); ed.selectPad(row); if (paintMode) { host().noteOn(36 + row, 0.8f); int nn = 36 + row; juce::Timer::callAfterDelay(120, [nn] { host().noteOff(nn); }); } } }
        void mouseDrag(const juce::MouseEvent& e) override { if (paintMode < 0) return; for (int row = 0; row < 16; ++row) for (int s = 0; s < steps; ++s) if (cell(row, s).contains(e.position) && has(clip(), row, s) != (paintMode == 1)) toggle(row, s, paintMode == 1); }
        void mouseUp(const juce::MouseEvent&) override { if (paintMode >= 0) host().endGesture("steps", true); paintMode = -1; }
    };
    parts.add(new Steps(*this));
    parts.add(new Panel("KIT", &d, { "kit", "lvl" }, col, tid, 1));
    // the selected pad's knobs (rebuilt when another pad is chosen)
    parts.add(new Panel("PAD 1", &d, { "p0v", "p0t", "p0d", "p0c", "p0l", "p0p" }, col, tid, 5));
    layout = [this] {
        auto r = getLocalBounds().reduced(14, 0).withTrimmedTop(48).withTrimmedBottom(84);
        auto left = r.removeFromLeft(juce::jmin(420, r.getWidth() * 38 / 100)); r.removeFromLeft(8);
        auto pads = left.removeFromTop(left.getHeight() * 62 / 100); left.removeFromTop(8);
        parts[0]->setBounds(pads);
        parts[2]->setBounds(left.removeFromLeft(110)); left.removeFromLeft(8); parts[3]->setBounds(left);
        parts[1]->setBounds(r);
    };
    for (auto* p : parts) addAndMakeVisible(p);
    // a kit change in the KIT box loads the kit's voices
    if (auto* kp = dynamic_cast<Panel*>(parts[2])) for (auto* c : kp->getChildren()) if (auto* pc = dynamic_cast<ParamChoice*>(c))
        pc->onChange = [this] { if (auto* dd = dynamic_cast<Drums*>(track->inst.get())) { applyDrumKit(*dd, dd->find("kit")->geti()); repaint(); for (auto* p : parts) p->repaint(); } };
}

void InstrumentEditor::selectPad(int p) {
    selPad = juce::jlimit(0, 15, p);
    auto* d = dynamic_cast<Drums*>(track->inst.get()); if (!d || parts.size() < 4) return;
    std::string k = "p" + std::to_string(selPad);
    auto b = parts[3]->getBounds();
    auto* np = new Panel("PAD " + juce::String(selPad + 1), d, { k + "v", k + "t", k + "d", k + "c", k + "l", k + "p" }, [this] { return Theme::argb(track->colour); }, track->id, 5);
    parts.set(3, np, true); addAndMakeVisible(np); np->setBounds(b);
    for (auto* x : parts) x->repaint();
}

void InstrumentEditor::resized() {
    close.setBounds(getWidth() - 90, 10, 76, 26);
    save.setBounds(close.getX() - 70, 10, 62, 26);
    next.setBounds(save.getX() - 34, 10, 28, 26);
    preset.setBounds(next.getX() - 214, 10, 210, 26);
    prev.setBounds(preset.getX() - 32, 10, 28, 26);
    load.setBounds(prev.getX() - 120, 10, 110, 26);
    keys->setBounds(getLocalBounds().reduced(14, 0).removeFromBottom(76).withTrimmedBottom(8));
    if (layout) layout();
}

void InstrumentEditor::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    auto c = Theme::argb(track->colour);
    g.setColour(c); g.fillRoundedRectangle(14, 14, 5, 18, 2.5f);
    auto& in = *track->inst;
    label(g, in.type == "nova" ? "NOVA" : in.type == "smp" ? "SAMPLER" : "DRUMS", { 26, 10, 120, 26 }, 15.0f, 2, Theme::txt());
    label(g, track->name + (in.type == "nova" ? U("  ·  3 wavetable oscillators, 2 filters, 4 envelopes, 4 LFOs, 16 modulations") : juce::String()),
          { 140, 10, juce::jmax(10, load.getX() - 380), 26 }, 9.0f, 1);
    auto& st = P().stations; int cur = host().stations.current;
    if (cur >= 0 && cur < (int) st.size()) label(g, U("kept in station ") + juce::String(cur + 1) + (host().stations.autoStore ? juce::String() : U(" when you STORE")),
                                                 { load.getX() - 230, 10, 220, 26 }, 8.5f, 2, Theme::gold(), juce::Justification::centredRight);
}

void InstrumentEditor::timerCallback() {
    if (!isShowing()) return;
    for (auto* p : parts) { if (auto* pn = dynamic_cast<Panel*>(p)) { if (pn->displayH > 0) pn->repaint(pn->display.toNearestInt()); } else p->repaint(); }
    static int k = 0; if (++k % 6 == 0) for (auto* p : parts) for (auto* c : p->getChildren()) c->repaint();
}

void InstrumentEditor::presetMenu() {
    juce::PopupMenu m; auto type = track->inst->type;
    std::vector<juce::File> user;
    for (auto& f : userPresetFolder(type).findChildFiles(juce::File::findFiles, false, "*.json")) user.push_back(f);
    std::sort(user.begin(), user.end(), [](auto& a, auto& b) { return a.getFileName() < b.getFileName(); });
    if (type == "nova") {
        std::map<std::string, juce::PopupMenu> groups; std::vector<std::string> order;
        auto& pr = novaPresets();
        for (int i = 0; i < (int) pr.size(); ++i) { if (!groups.count(pr[(size_t) i].group)) order.push_back(pr[(size_t) i].group); groups[pr[(size_t) i].group].addItem(i + 1, pr[(size_t) i].name, true, presetName == juce::String(pr[(size_t) i].name)); }
        for (auto& gname : order) m.addSubMenu(gname, groups[gname]);
    } else if (type == "drm") { auto kits = drumKitNames(); for (int i = 0; i < (int) kits.size(); ++i) m.addItem(500 + i, "Kit: " + juce::String(kits[(size_t) i])); }
    juce::PopupMenu mine; for (int i = 0; i < (int) user.size(); ++i) mine.addItem(1000 + i, user[(size_t) i].getFileNameWithoutExtension());
    if (user.empty()) mine.addItem(-1, "(SAVE keeps your sounds here)", false);
    m.addSeparator(); m.addSubMenu("MY SOUNDS", mine);
    m.addItem(900, "Reveal my sounds folder", true);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&preset), [this, user](int r) {
        if (r <= 0) return;
        host().knobTouched();
        if (r >= 1000) { auto f = user[(size_t) r - 1000]; paramsFromVar(*track->inst, juce::JSON::parse(f)); presetName = f.getFileNameWithoutExtension(); }
        else if (r >= 500 && r < 900) { if (auto* d = dynamic_cast<Drums*>(track->inst.get())) { applyDrumKit(*d, r - 500); presetName = "Kit " + juce::String(drumKitNames()[(size_t) r - 500]); } }
        else if (r == 900) { userPresetFolder(track->inst->type).revealToUser(); return; }
        else if (auto* n = dynamic_cast<Nova*>(track->inst.get())) { auto& pr = novaPresets()[(size_t) r - 1]; applyNovaPreset(*n, pr); presetName = pr.name; }
        host().sendChangeMessage(); repaint(); for (auto* p : parts) p->repaint();
    });
}

void InstrumentEditor::stepPreset(int d) {
    auto* n = dynamic_cast<Nova*>(track->inst.get()); if (!n) { presetMenu(); return; }
    auto& pr = novaPresets(); int i = 0;
    for (int k = 0; k < (int) pr.size(); ++k) if (presetName == juce::String(pr[(size_t) k].name)) i = k;
    i = (i + d + (int) pr.size()) % (int) pr.size();
    host().knobTouched(); applyNovaPreset(*n, pr[(size_t) i]); presetName = pr[(size_t) i].name;
    host().say(juce::String(pr[(size_t) i].group) + U(" · ") + presetName);
    repaint(); for (auto* p : parts) p->repaint();
}

void InstrumentEditor::setSampleFile(const juce::File& f, int pad) {
    juce::String err; auto s = host().importAudio(f, &err);
    if (!s) { host().say(err.isEmpty() ? "that file can't be read" : err); return; }
    auto sd = Host::toSample(s);
    host().knobTouched();
    if (auto* sm = dynamic_cast<Sampler*>(track->inst.get())) { sm->setSample(sd); presetName = s->name; }
    else if (auto* d = dynamic_cast<Drums*>(track->inst.get())) { int p = pad < 0 ? selPad : pad; d->setPadSample(p, sd); d->find("p" + std::to_string(p) + "v")->set(16); selectPad(p); }
    else if (auto* n = dynamic_cast<Nova*>(track->inst.get())) {
        // a sound dropped on NOVA: the oscillator under the mouse (or A) plays it as a wavetable
        int osc = juce::jlimit(0, 2, pad);
        std::vector<float> L(s->buf.getReadPointer(0), s->buf.getReadPointer(0) + s->length());
        n->userTable[osc] = wavetableFromAudio(L.data(), (int) L.size(), s->sr, s->name.toStdString()); n->userTablePath[osc] = s->file.toStdString();
        n->find("o" + std::to_string(osc + 1) + "tab")->set((float) factoryTables().size()); n->find("o" + std::to_string(osc + 1) + "on")->set(1);
        host().say(U("OSC ") + juce::String::charToString((juce::juce_wchar) ('A' + osc)) + U(" plays “") + s->name + U("” as a wavetable · move POSITION to travel through it"));
    }
    host().dirty = true; host().sendChangeMessage(); repaint();
}

void InstrumentEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    if (files.isEmpty()) return;
    int pad = -1;
    if (track->inst->type == "nova") { pad = 0; for (int k = 0; k < 3 && k < parts.size(); ++k) if (parts[k]->getBounds().contains(x, y)) pad = k; }
    else if (track->inst->type == "drm") pad = selPad;
    setSampleFile(juce::File(files[0]), pad);
}

void InstrumentEditor::loadSampleInto(int pad) {
    chooser = std::make_unique<juce::FileChooser>("Choose a sound", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, pad](const juce::FileChooser& fc) {
        auto r = fc.getResult(); if (r.existsAsFile()) setSampleFile(r, pad); });
}

} // namespace score
