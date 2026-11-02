#include "ShellViews.h"

namespace arco {

static void say(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}

juce::Colour DrumView::padColour(int pad) {
    static const int A[12] = { 0, 2, 2, 6, 5, 5, 5, 4, 4, 3, 3, 3 };    // kick red, snare / rim ochre, clap warm, hats teal, toms mauve, cymbals blue
    return Theme::pal().acc[(size_t) A[juce::jlimit(0, 11, pad)]];
}
DrumMachine& DrumView::dm() { return shell.host.engine.drumMachine; }
int DrumView::pat() { return juce::jlimit(0, 7, (int) shell.host.model.get("dm_pat")); }

DrumView::DrumView(AppShell& s) : shell(s) {
    setWantsKeyboardFocus(true);
    auto& m = shell.host.model;
    // kits
    auto names = DrumKits::kitNames();
    static const char* about[5] = { "a real jazz kit, recorded", "the classic boom and tick", "punchy house and techno", "long 808 bass, crisp hats", "the jazz kit on old tape" };
    for (int k = 0; k < names.size(); ++k) {
        auto* p = add<Pill>(names[k], [&m, k] { return (int) m.get("dm_kit") == k ? Theme::sel() : Theme::btn(); }, 10.0f, 10.0f);
        p->onClick = [&m, k] { m.set("dm_kit", k); }; p->setTooltip(about[k]);
        p->textColour = [&m, k] { return (int) m.get("dm_kit") == k ? Theme::textOn(Theme::sel()) : Theme::txt(); };
        kitBtns.push_back(p);
    }
    // patterns A-H
    for (int k = 0; k < 8; ++k) {
        auto* p = add<Pill>(juce::String::charToString((juce::juce_wchar) ('A' + k)), [this, k] {
            if (pat() == k) return Theme::blue();
            for (int pd = 0; pd < 12; ++pd) for (int st = 0; st < dm().patLen[k].load(); ++st) if (dm().getStep(k, pd, st).vel > 0) return Theme::card2().interpolatedWith(Theme::blue(), 0.25f);
            return Theme::btn(); }, 13.0f, 10.0f);
        p->onClick = [this, k, &m] { m.set("dm_pat", k); repaint(); };
        p->textColour = [this, k] { return pat() == k ? Theme::textOn(Theme::blue()) : Theme::txt(); };
        patBtns.push_back(p);
    }
    auto* chain = add<Pill>("CHAIN", [&m] { return m.get("dm_chain") > 0.5 ? Theme::amber() : Theme::btn(); }, 8.5f, 10.0f);
    chain->tint = true; chain->onClick = [&m] { m.set("dm_chain", m.get("dm_chain") > 0.5 ? 0 : 1); }; chain->setComponentID("dv:chain");
    chain->setTooltip("play the patterns that have notes, one after another");
    auto* copy = add<Pill>("COPY TO NEXT", [] { return Theme::btn(); }, 8.5f, 10.0f); copy->setComponentID("dv:copy");
    copy->textColour = [] { return Theme::txt(); };
    copy->onClick = [this, &m] { snapshot(); int p = pat(), q = (p + 1) % 8; auto& D = dm();
        for (int pd = 0; pd < 12; ++pd) for (int st = 0; st < DrumMachine::NSTEP; ++st) D.steps[q][pd][st].store(D.steps[p][pd][st].load());
        D.patLen[q].store(D.patLen[p].load()); D.patRes[q].store(D.patRes[p].load()); m.set("dm_pat", q); repaint(); };
    auto* clr = add<Pill>("CLEAR", [] { return Theme::btn(); }, 8.5f, 10.0f); clr->setComponentID("dv:clear");
    clr->textColour = [] { return Theme::txt(); };
    clr->onClick = [this] { snapshot(); for (int pd = 0; pd < 12; ++pd) for (int st = 0; st < DrumMachine::NSTEP; ++st) dm().steps[pat()][pd][st].store(0); repaint(); };
    lenSeg = add<Segments>(juce::StringArray { "16", "32", "48", "64" }, [this] { return juce::jlimit(0, 3, dm().patLen[pat()].load() / 16 - 1); },
                           [this](int i) { dm().patLen[pat()].store((i + 1) * 16); page = juce::jmin(page, i); repaint(); }, [] { return Theme::blue(); });
    add<Segments>(juce::StringArray { "1/8", "1/16", "1/16T", "1/32" }, [this] { return dm().patRes[pat()].load(); },
                  [this](int i) { dm().patRes[pat()].store(i); }, [] { return Theme::blue(); });
    pageSeg = add<Segments>(juce::StringArray { "1-16", "17-32", "33-48", "49-64" }, [this] { return page; }, [this](int i) { if (i * 16 < dm().patLen[pat()].load()) page = i; repaint(); },
                            [] { return Theme::sel(); });
    add<Segments>(juce::StringArray { "STEPS", "VELOCITY", "CHANCE", "REPEATS", "NUDGE" }, [this] { return mode; }, [this](int i) { mode = i; repaint(); }, [] { return Theme::sel(); });
    auto knob = [&](const juce::String& n, const std::string& id, double lo, double hi, double def, ColourFn c, std::function<juce::String(double)> f) {
        return add<KnobCell>(n, [&m, id] { return m.get(id); }, [&m, id](double v) { m.set(id, v); }, lo, hi, def, std::move(c), std::move(f)); };
    auto pct = [](double v) { return juce::String((int) std::round(v)) + " %"; };
    knob("SWING", "dm_swing", 50, 75, 50, [] { return Theme::amber(); }, pct)->setComponentID("dv:swing");
    knob("HUMANISE", "dm_human", 0, 100, 0, [] { return Theme::amber(); }, pct)->setComponentID("dv:human");
    // the selected pad
    auto pk = [&](const juce::String& n, const char* key, double lo, double hi, double def, std::function<juce::String(double)> f) {
        auto* kc = add<KnobCell>(n, [this, key] { return shell.host.model.get("dm" + std::to_string(selPad) + "_" + key); },
                                 [this, key](double v) { shell.host.model.set("dm" + std::to_string(selPad) + "_" + key, v); }, lo, hi, def, [this] { return padColour(selPad); }, std::move(f));
        padKnobs.push_back(kc); return kc; };
    pk("LEVEL", "lvl", 0, 100, 80, [](double v) { return juce::String((int) v); });
    pk("PAN", "pan", -50, 50, 0, [](double v) { return std::abs(v) < 1 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String((int) std::abs(v) * 2); });
    pk("TUNE", "tune", -12, 12, 0, [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; });
    pk("DECAY", "dec", 0, 100, 100, [](double v) { return v >= 99.5 ? juce::String("natural") : juce::String((int) v); });
    pk("TONE", "tone", 0, 100, 50, [](double v) { return std::abs(v - 50) < 3 ? juce::String("open") : (v < 50 ? "darker" : "thinner"); });
    pk("SPACE", "spc", 0, 100, 0, [](double v) { return juce::String((int) v); });
    auto* mute = add<Pill>("MUTE", [this] { return shell.host.model.get("dm" + std::to_string(selPad) + "_mute") > 0.5 ? Theme::red() : Theme::btn(); }, 8.5f, 8.0f);
    mute->tint = true; mute->onClick = [this] { auto id = "dm" + std::to_string(selPad) + "_mute"; shell.host.model.set(id, shell.host.model.get(id) > 0.5 ? 0 : 1); }; mute->setComponentID("dv:mute");
    auto* solo = add<Pill>("SOLO", [this] { return shell.host.model.get("dm" + std::to_string(selPad) + "_solo") > 0.5 ? Theme::blue() : Theme::btn(); }, 8.5f, 8.0f);
    solo->tint = true; solo->onClick = [this] { auto id = "dm" + std::to_string(selPad) + "_solo"; shell.host.model.set(id, shell.host.model.get(id) > 0.5 ? 0 : 1); }; solo->setComponentID("dv:solo");
    auto* pkit = add<Pill>("", [] { return Theme::btn(); }, 8.5f, 8.0f); pkit->setComponentID("dv:padkit");
    pkit->dynText = [this] { int k = (int) shell.host.model.get("dm" + std::to_string(selPad) + "_kit"); return k < 0 ? juce::String("SOUND: THE KIT'S") : "SOUND FROM " + DrumKits::kitNames()[k]; };
    pkit->textColour = [] { return Theme::txt(); };
    pkit->onClick = [this] { auto id = "dm" + std::to_string(selPad) + "_kit"; int k = (int) shell.host.model.get(id); shell.host.model.set(id, k >= 4 ? -1 : k + 1); };
    pkit->setTooltip("this pad can take its sound from another kit");
    // the kit bus
    knob("VOLUME", "dm_vol", 0, 100, 75, [] { return Theme::txt(); }, nullptr)->setComponentID("dv:vol");
    knob("DRIVE", "dm_drive", 0, 100, 0, [] { return Theme::red(); }, nullptr)->setComponentID("dv:drive");
    knob("GLUE", "dm_glue", 0, 100, 20, [] { return Theme::amber(); }, nullptr)->setComponentID("dv:glue");
    knob("SPACE", "dm_space", 0, 100, 0, [] { return Theme::blue(); }, nullptr)->setComponentID("dv:space");
    auto* recb = add<Pill>("REC", [&m] { return m.get("dm_rec") > 0.5 ? Theme::red() : Theme::btn(); }, 9.0f, 10.0f);
    recb->tint = true; recb->icon = 3; recb->onClick = [&m] { m.set("dm_rec", m.get("dm_rec") > 0.5 ? 0 : 1); }; recb->setComponentID("dv:rec");
    recb->setTooltip("pads played while it runs are written into the pattern");
    startTimerHz(30);
}
DrumView::~DrumView() { shell.host.drumKeys = false; }
void DrumView::visibilityChanged() { shell.host.drumKeys = isVisible(); }

void DrumView::resized() {
    int W = getWidth(), H = getHeight();
    bool tall = H > W;
    auto byId = [&](const juce::String& id) -> juce::Component* { for (auto* c : owned) if (c->getComponentID() == id) return c; return nullptr; };
    std::vector<Segments*> segs; for (auto* c : owned) if (auto* sg = dynamic_cast<Segments*>(c)) segs.push_back(sg);   // length, step, pages, edit mode
    if (!tall) {
        leftR = { 18, 16, 262, H - 32 };
        rightR = { W - 18 - 350, 16, 350, H - 32 };
        gridR = { leftR.getRight() + 16, 16, rightR.getX() - 16 - (leftR.getRight() + 16), H - 32 };
        // left: kits, patterns, length, grid, swing
        int x = leftR.getX() + 16, w = leftR.getWidth() - 32, y = leftR.getY() + 40;
        for (size_t k = 0; k < kitBtns.size(); ++k) { kitBtns[k]->setBounds(x, y, w, 38); y += 42; }
        y += 34;
        for (int k = 0; k < 8; ++k) patBtns[(size_t) k]->setBounds(x + (k % 4) * (w / 4), y + (k / 4) * 46, w / 4 - 6, 40);
        y += 96;
        if (auto* c = byId("dv:chain")) c->setBounds(x, y, 70, 32);
        if (auto* c = byId("dv:copy")) c->setBounds(x + 76, y, 104, 32);
        if (auto* c = byId("dv:clear")) c->setBounds(x + 186, y, w - 186, 32);
        y += 66;
        segs[0]->setBounds(x, y, w, 32); segs[1]->setBounds(x, y + 58, w, 32);
        y += 112;
        if (auto* c = byId("dv:swing")) c->setBounds(x, y, w / 2, 86);
        if (auto* c = byId("dv:human")) c->setBounds(x + w / 2, y, w / 2, 86);
        // right: the pads, the selected pad, the kit bus
        int rx = rightR.getX() + 16, rw = rightR.getWidth() - 32;
        padsR = { rx, rightR.getY() + 40, rw, 3 * 74 };
        padNameR = { rx, padsR.getBottom() + 14, 300, 26 };
        int py = padsR.getBottom() + 52;
        for (size_t k = 0; k < padKnobs.size(); ++k) padKnobs[k]->setBounds(rx + (int) (k % 3) * (rw / 3), py + (int) (k / 3) * 92, rw / 3, 88);
        py += 188;
        if (auto* c = byId("dv:mute")) c->setBounds(rx, py, 70, 30);
        if (auto* c = byId("dv:solo")) c->setBounds(rx + 76, py, 70, 30);
        if (auto* c = byId("dv:padkit")) c->setBounds(rx + 152, py, rw - 152, 30);
        py = rightR.getBottom() - 104;
        kitLabelR = { rx, py - 24, 120, 18 };
        const char* bus[4] = { "dv:vol", "dv:drive", "dv:glue", "dv:space" };
        for (int k = 0; k < 4; ++k) if (auto* c = byId(bus[k])) c->setBounds(rx + k * (rw / 4), py, rw / 4, 90);
    } else {
        // an iPad held upright: the kit and the patterns on top, the grid in the middle, the pads and the sound below
        leftR = { 18, 16, W - 36, 236 };
        int x = leftR.getX() + 16, w = leftR.getWidth() - 32, y = leftR.getY() + 40;
        int kw = (w - 4 * 8) / 5;
        for (size_t k = 0; k < kitBtns.size(); ++k) kitBtns[k]->setBounds(x + (int) k * (kw + 8), y, kw, 38);
        y += 76;
        int pw = 54; for (int k = 0; k < 8; ++k) patBtns[(size_t) k]->setBounds(x + k * (pw + 6), y, pw, 40);
        int xx = x + 8 * (pw + 6) + 10;
        if (auto* c = byId("dv:chain")) c->setBounds(xx, y + 4, 70, 32);
        if (auto* c = byId("dv:copy")) c->setBounds(xx + 76, y + 4, 104, 32);
        if (auto* c = byId("dv:clear")) c->setBounds(xx + 186, y + 4, 70, 32);
        y += 76;
        segs[0]->setBounds(x, y, 260, 32); segs[1]->setBounds(x + 280, y, 300, 32);
        if (auto* c = byId("dv:swing")) c->setBounds(x + 600, y - 52, 90, 92);
        if (auto* c = byId("dv:human")) c->setBounds(x + 700, y - 52, 90, 92);
        rightR = { 18, H - 16 - 380, W - 36, 380 };
        gridR = { 18, leftR.getBottom() + 14, W - 36, rightR.getY() - 14 - (leftR.getBottom() + 14) };
        int rx = rightR.getX() + 16;
        padsR = { rx, rightR.getY() + 40, 400, 3 * 74 };
        padNameR = { padsR.getRight() + 20, rightR.getY() + 10, 300, 26 };
        int kx = padsR.getRight() + 20, kwid = rightR.getRight() - 16 - kx;
        for (size_t k = 0; k < padKnobs.size(); ++k) padKnobs[k]->setBounds(kx + (int) (k % 3) * (kwid / 3), rightR.getY() + 40 + (int) (k / 3) * 92, kwid / 3, 88);
        int py = rightR.getY() + 230;
        if (auto* c = byId("dv:mute")) c->setBounds(kx, py, 70, 30);
        if (auto* c = byId("dv:solo")) c->setBounds(kx + 76, py, 70, 30);
        if (auto* c = byId("dv:padkit")) c->setBounds(kx + 152, py, kwid - 152, 30);
        py = rightR.getBottom() - 100;
        kitLabelR = { rx, py - 22, 120, 18 };
        const char* bus[4] = { "dv:vol", "dv:drive", "dv:glue", "dv:space" };
        for (int k = 0; k < 4; ++k) if (auto* c = byId(bus[k])) c->setBounds(rx + k * 100, py, 100, 90);
    }
    if (auto* c = byId("dv:rec")) c->setBounds(rightR.getRight() - 16 - 80, rightR.getY() + 8, 80, 28);
    int ew = juce::jmin(410, gridR.getWidth() / 2 - 24);
    segs[3]->setBounds(gridR.getX() + 16, gridR.getY() + 12, ew, 32);
    segs[2]->setBounds(gridR.getX() + 26 + ew, gridR.getY() + 12, gridR.getWidth() - 42 - ew, 32);
}

juce::Rectangle<int> DrumView::rowHead(int pad) const {
    int top = gridR.getY() + 60, h = (gridR.getHeight() - 60 - 16) / 12;
    return { gridR.getX() + 12, top + pad * h, 150, h - 4 };
}
juce::Rectangle<int> DrumView::cellRect(int pad, int step) const {
    auto rh = rowHead(pad);
    int x0 = rh.getRight() + 10, w = gridR.getRight() - 12 - x0;
    float cw = w / 16.0f; int i = step - page * 16;
    int gap = (i % 4 == 0 && i > 0) ? 4 : 0;
    return { x0 + (int) (i * cw) + gap, rh.getY(), (int) cw - 4 - gap, rh.getHeight() };
}
bool DrumView::cellAt(juce::Point<int> p, int& pad, int& step) const {
    for (int pd = 0; pd < 12; ++pd) for (int i = 0; i < 16; ++i) { auto r = cellRect(pd, page * 16 + i).expanded(2, 2); if (r.contains(p)) { pad = pd; step = page * 16 + i; return true; } }
    return false;
}
juce::Rectangle<int> DrumView::padRect(int pad) const {
    int cols = 4; float w = padsR.getWidth() / (float) cols, h = padsR.getHeight() / 3.0f;
    int row = 2 - pad / 4, col = pad % 4;               // kick bottom-left, like a pad controller
    return juce::Rectangle<float>(padsR.getX() + col * w, padsR.getY() + row * h, w, h).reduced(4).toNearestInt();
}

void DrumView::paint(juce::Graphics& g) {
    Theme::backdrop(g, getLocalBounds());
    drawPanel(g, leftR.toFloat(), 18); drawPanel(g, gridR.toFloat(), 18); drawPanel(g, rightR.toFloat(), 18);
    auto& D = dm(); auto& m = shell.host.model;
    int lx = leftR.getX() + 16;
    say(g, "KIT", { lx, leftR.getY() + 12, 200, 20 }, 9.5f, Theme::sub(), 2);
    int py = patBtns.empty() ? 0 : patBtns.front()->getY() - 30;
    say(g, "PATTERN", { lx, py, 200, 20 }, 9.5f, Theme::sub(), 2);
    if (D.queuedPat.load() >= 0) say(g, juce::String::fromUTF8("next: ") + juce::String::charToString((juce::juce_wchar) ('A' + D.queuedPat.load())), { lx + 90, py, 150, 20 }, 8.5f, Theme::ink(Theme::amber()), 2);
    for (auto* c : owned) if (auto* s = dynamic_cast<Segments*>(c)) {
        if (s == lenSeg) say(g, "LENGTH (steps)", { s->getX(), s->getY() - 22, 200, 18 }, 9.0f, Theme::sub(), 2);
        else if (s->items[0] == "1/8") say(g, "STEP", { s->getX(), s->getY() - 22, 200, 18 }, 9.0f, Theme::sub(), 2);
    }
    if (!kitBtns.empty()) {
        auto bank = D.bank.load();
        if (bank == nullptr) say(g, juce::String::fromUTF8("loading the kits …"), { lx, leftR.getBottom() - 30, 230, 20 }, 8.0f, Theme::ink(Theme::amber()), 1);
    }
    // grid
    int ps = D.playStep.load(), pp = D.patPlaying.load(), len = D.patLen[pat()].load();
    int kit = (int) m.get("dm_kit");
    for (int pd = 0; pd < 12; ++pd) {
        auto rh = rowHead(pd); auto col = padColour(pd);
        bool sel = pd == selPad;
        if (sel) { g.setColour(Theme::sel().withMultipliedAlpha(0.35f)); g.fillRoundedRectangle(rh.toFloat().expanded(4, 1), 8.0f); }
        bool flash = juce::Time::getMillisecondCounterHiRes() < flashUntil[pd];
        g.setColour(flash ? col : col.withAlpha(0.5f)); g.fillEllipse((float) rh.getX() + 6, rh.getCentreY() - 5.0f, 10.0f, 10.0f);
        int pk = (int) m.get("dm" + std::to_string(pd) + "_kit"); if (pk < 0) pk = kit;
        bool muted = m.get("dm" + std::to_string(pd) + "_mute") > 0.5, solo = m.get("dm" + std::to_string(pd) + "_solo") > 0.5;
        say(g, DrumKits::padName(pk, pd), rh.withTrimmedLeft(24).withTrimmedRight(30), 8.6f, muted ? Theme::sub() : Theme::txt(), 2);
        if (muted || solo) say(g, muted ? "M" : "S", rh.withTrimmedLeft(rh.getWidth() - 22), 8.5f, Theme::ink(muted ? Theme::red() : Theme::blue()), 2, juce::Justification::centred);
        for (int i = 0; i < 16; ++i) {
            int st = page * 16 + i; auto r = cellRect(pd, st).toFloat();
            bool inPat = st < len;
            auto step = D.getStep(pat(), pd, st);
            juce::Colour base = (i / 4) % 2 == 0 ? Theme::btn() : Theme::card2();
            if (!inPat) base = base.withAlpha(0.35f);
            g.setColour(base); g.fillRoundedRectangle(r, 6.0f);
            if (step.vel > 0 && inPat) {
                float v = step.vel / 127.0f;
                if (mode == 1 || mode == 2) {          // the value as a bar
                    float k = mode == 1 ? v : step.chance / 100.0f;
                    g.setColour(col.withAlpha(0.35f)); g.fillRoundedRectangle(r, 6.0f);
                    g.setColour(col); g.fillRoundedRectangle(r.withTop(r.getBottom() - r.getHeight() * k), 6.0f);
                } else {
                    g.setColour(col.withAlpha(0.45f + 0.55f * v)); g.fillRoundedRectangle(r, 6.0f);
                    if (step.vel >= 120) { g.setColour(Theme::textOn(col).withAlpha(0.8f)); g.fillEllipse(r.getCentreX() - 2.5f, r.getY() + 4, 5, 5); }   // accent
                }
                if (step.chance < 100 && mode != 2) { g.setColour(Theme::textOn(col).withAlpha(0.7f)); say(g, juce::String(step.chance) + "%", r.toNearestInt(), 6.8f, Theme::textOn(col), 2, juce::Justification::centredBottom); }
                if (step.repeats > 1) { g.setColour(Theme::textOn(col)); for (int k = 0; k < step.repeats; ++k) g.fillRect(r.getX() + 4 + k * 4.0f, r.getBottom() - 6, 2.0f, 3.0f);
                                        if (mode == 3) say(g, "x" + juce::String(step.repeats), r.toNearestInt(), 9.0f, Theme::textOn(col), 2, juce::Justification::centred); }
                if (step.nudge != 0) { float nx = r.getCentreX() + step.nudge / 100.0f * r.getWidth(); g.setColour(Theme::textOn(col)); g.fillRect(nx - 1, r.getY() + 2, 2.0f, r.getHeight() - 4); }
            }
            if (inPat && ps == st && pp == pat()) { g.setColour(Theme::txt().withAlpha(0.22f)); g.fillRoundedRectangle(r, 6.0f); }
        }
    }
    // step numbers
    for (int i = 0; i < 16; ++i) if (i % 4 == 0) { auto r = cellRect(0, page * 16 + i); say(g, juce::String(page * 16 + i + 1), { r.getX(), r.getY() - 16, 40, 14 }, 7.5f, Theme::sub(), 1); }
    // right: pads
    int rx = rightR.getX() + 16;
    say(g, "PADS", { rx, rightR.getY() + 12, 120, 20 }, 9.5f, Theme::sub(), 2);
    for (int pd = 0; pd < 12; ++pd) {
        auto r = padRect(pd).toFloat(); auto col = padColour(pd);
        bool flash = juce::Time::getMillisecondCounterHiRes() < flashUntil[pd];
        g.setColour(flash ? col : (pd == selPad ? Theme::card2().interpolatedWith(col, 0.30f) : Theme::btn())); g.fillRoundedRectangle(r, 12.0f);
        g.setColour(pd == selPad ? col : Theme::line()); g.drawRoundedRectangle(r.reduced(0.5f), 12.0f, pd == selPad ? 2.0f : 1.0f);
        int pk = (int) m.get("dm" + std::to_string(pd) + "_kit"); if (pk < 0) pk = kit;
        say(g, DrumKits::padName(pk, pd), r.toNearestInt().reduced(6, 4), 7.6f, flash ? Theme::textOn(col) : Theme::txt(), 2, juce::Justification::bottomLeft);
    }
    int pk = (int) m.get("dm" + std::to_string(selPad) + "_kit"); if (pk < 0) pk = kit;
    say(g, DrumKits::padName(pk, selPad), padNameR, 13.0f, Theme::ink(padColour(selPad)), 2);
    say(g, "THE KIT", kitLabelR, 9.5f, Theme::sub(), 2);
    // a line of help
    juce::String hint = mode == 0 ? juce::String::fromUTF8("click a step to add / remove it  ·  shift-click: an accent  ·  drag along a row to draw  ·  keys Z X C V / A S D F / Q W E R play the pads")
                      : mode == 1 ? juce::String("drag a step up / down: how hard it is played") : mode == 2 ? juce::String("drag up / down: the chance the step plays (100 % = always)")
                      : mode == 3 ? juce::String("click: 1, 2, 3 or 4 hits inside the step (rolls)") : juce::String("drag left / right: a little early or late (feel)");
    say(g, hint, { gridR.getX() + 16, gridR.getBottom() - 22, gridR.getWidth() - 32, 16 }, 7.8f, Theme::sub(), 1);
}

void DrumView::snapshot() {
    std::vector<uint32_t> s; s.reserve(12 * DrumMachine::NSTEP + 2); int p = pat();
    s.push_back((uint32_t) p); s.push_back((uint32_t) dm().patLen[p].load());
    for (int pd = 0; pd < 12; ++pd) for (int st = 0; st < DrumMachine::NSTEP; ++st) s.push_back(dm().steps[p][pd][st].load());
    undoStack.push_back(std::move(s)); if (undoStack.size() > 64) undoStack.erase(undoStack.begin());
}
void DrumView::undo() {
    if (undoStack.empty()) return;
    auto s = undoStack.back(); undoStack.pop_back();
    int p = (int) s[0]; dm().patLen[p].store((int) s[1]); size_t k = 2;
    for (int pd = 0; pd < 12; ++pd) for (int st = 0; st < DrumMachine::NSTEP; ++st) dm().steps[p][pd][st].store(s[k++]);
    shell.host.model.set("dm_pat", p); repaint();
}

void DrumView::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto p = e.getPosition();
    for (int pd = 0; pd < 12; ++pd) if (padRect(pd).contains(p)) {
        selPad = pd;
        float vel = juce::jlimit(0.25f, 1.0f, 1.0f - (p.y - padRect(pd).getY()) / (float) padRect(pd).getHeight() * 0.6f);   // higher on the pad = harder
        dm().play(pd, vel); repaint(); return; }
    for (int pd = 0; pd < 12; ++pd) if (rowHead(pd).contains(p)) { selPad = pd; dm().play(pd, 0.8f); repaint(); return; }
    int pd, st;
    if (cellAt(p, pd, st) && st < dm().patLen[pat()].load()) {
        snapshot();
        selPad = pd; dragPad = pd; dragStep = st; dragY0 = e.position.y;
        auto s = dm().getStep(pat(), pd, st);
        if (mode == 0) {
            paintVel = s.vel > 0 ? 0 : (e.mods.isShiftDown() ? 127 : 100);
            s.vel = paintVel; if (s.vel > 0) { s.chance = 100; s.repeats = 1; s.nudge = 0; }
            dm().setStep(pat(), pd, st, s);
            if (paintVel > 0) dm().play(pd, paintVel / 127.0f);
        } else if (s.vel > 0) {
            if (mode == 1) dragV0 = s.vel; else if (mode == 2) dragV0 = s.chance; else if (mode == 4) dragV0 = s.nudge;
            else if (mode == 3) { s.repeats = s.repeats % 4 + 1; dm().setStep(pat(), pd, st, s); }
        }
        repaint();
    }
}
void DrumView::mouseDrag(const juce::MouseEvent& e) {
    if (dragPad < 0) return;
    if (mode == 0) {
        int pd, st;
        if (cellAt({ e.getPosition().x, cellRect(dragPad, page * 16).getCentreY() }, pd, st) && st != dragStep && st < dm().patLen[pat()].load()) {
            auto s = dm().getStep(pat(), dragPad, st); s.vel = paintVel; s.chance = 100; s.repeats = 1; s.nudge = 0;
            dm().setStep(pat(), dragPad, st, s); dragStep = st; repaint(); }
        return;
    }
    auto s = dm().getStep(pat(), dragPad, dragStep); if (s.vel <= 0) return;
    float dy = dragY0 - e.position.y, dx = (float) e.getDistanceFromDragStartX();
    if (mode == 1) s.vel = juce::jlimit(1, 127, dragV0 + (int) (dy * 1.2f));
    if (mode == 2) s.chance = juce::jlimit(1, 100, dragV0 + (int) dy);
    if (mode == 4) s.nudge = juce::jlimit(-50, 50, dragV0 + (int) (dx * 0.8f));
    dm().setStep(pat(), dragPad, dragStep, s); repaint();
}
void DrumView::mouseUp(const juce::MouseEvent&) { dragPad = -1; }

bool DrumView::keyPressed(const juce::KeyPress& k) {
    // the computer keys as pads: Z X C V (bottom row), A S D F, Q W E R
    static const char* keys = "zxcvasdfqwer";
    auto ch = (char) juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    for (int i = 0; i < 12; ++i) if (ch == keys[i]) { dm().play(i, k.getModifiers().isShiftDown() ? 1.0f : 0.8f); selPad = i; return true; }
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z') { undo(); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { shell.transportPlay(); return true; }
    return false;
}

void DrumView::timerCallback() {
    if (!isVisible()) return;
    double now = juce::Time::getMillisecondCounterHiRes();
    bool any = false;
    for (int pd = 0; pd < 12; ++pd) { int h = dm().hit[pd].load(); if (h != lastHit[pd]) { lastHit[pd] = h; flashUntil[pd] = now + 110; any = true; } }
    int ps = dm().playStep.load();
    // the pages follow the playhead when the pattern is longer than 16 and nothing is being edited
    if (ps >= 0 && dragPad < 0 && dm().patLen[pat()].load() > 16 && !isMouseOverOrDragging(true) && ps / 16 != page && dm().patPlaying.load() == pat()) page = ps / 16;
    if (any || ps != lastPlayStep || true) { lastPlayStep = ps; repaint(); }
    for (auto* c : owned) c->repaint();
}

} // namespace arco
