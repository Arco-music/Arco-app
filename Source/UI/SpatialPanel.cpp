#include "SpatialPanel.h"
#include "MainView.h"

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static void lab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.85f);
}
static void labOn(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour bg) {
    g.setColour(Theme::textOn(bg)); g.setFont(Theme::font(fs, 2)); g.drawFittedText(t, r, juce::Justification::centred, 1, 0.85f);
}
static juce::String srcName(int s) { return s < 4 ? "LIVE " + juce::String(s + 1) : "TRACK " + juce::String(s - 3); }
static const char* MOT[5] = { "STILL", "CIRCLE", "SWING", "WANDER", "FLY OVER" };

SpatialPanel::SpatialPanel(Canvas& c) : cv(c) {
    auto& m = cv.model(); auto& h = cv.host;
    auto pill = [&](juce::Rectangle<int> r, const juce::String& t, ColourFn col, std::function<void()> f, float fs = 8.0f) { auto* p = add<Pill>(r, t, std::move(col), fs, 6.0f); p->onClick = std::move(f); return p; };
    pill({ 1360 - 104, 14, 88, 26 }, U8("✕ CLOSE"), [] { return Theme::btn(); }, [this] { cv.host.saveSetup(); setVisible(false); });
    auto* on = pill({ 1360 - 236, 14, 124, 26 }, "AMBISONICS OFF", [&m] { return m.get("amb_on") > 0.5 ? Theme::green() : Theme::btn(); }, [&m] { m.set("amb_on", m.get("amb_on") > 0.5 ? 0 : 1); });
    on->dynText = [&m] { return m.get("amb_on") > 0.5 ? juce::String(U8("● AMBISONICS ON")) : juce::String("AMBISONICS OFF"); };
    on->setComponentID("p:amb_on");
    // ---- layouts
    const char* LAY[10] = { "STEREO", "QUAD", "5.0", "7.0", "RING 6", "RING 8", "DOME 8+4", "CUBE 8", "5.0.4", "RING 16" };
    for (int i = 0; i < 10; ++i) pill({ 760 + (i % 5) * 116, 92 + (i / 5) * 30, 110, 26 }, LAY[i], [] { return Theme::btn(); }, [this, i] { preset(i); });
    // ---- the speakers: how many, the chosen one
    auto* nb = add<ValueBox>({ 860, 172, 56, 26 }, [&m] { return m.get("spk_n"); }, [&m, &h](double v) { m.set("spk_n", v); h.speakerLayout = 2; h.applySpeakers(); h.applyRouting(); }, 2, 32, 0, [] { return Theme::green(); });
    nb->fsize = 12.0f;
    auto* sb = add<ValueBox>({ 1030, 172, 50, 26 }, [this] { return (double) (selSpk + 1); }, [this, &m](double v) { selSpk = juce::jlimit(0, (int) m.get("spk_n") - 1, (int) v - 1); repaint(); }, 1, 32, 0, [] { return Theme::sel(); });
    sb->fsize = 12.0f;
    auto spkKnob = [&](int x, const char* what, double lo, double hi) {
        add<Knob>({ x, 212, 40, 40 }, [this, &m, what] { return m.get(std::string(what) + std::to_string(selSpk)); },
                  [this, &m, &h, what](double v) { m.set(std::string(what) + std::to_string(selSpk), v); h.speakerLayout = 2; h.applySpeakers(); }, lo, hi, 0, [] { return Theme::green(); }); };
    spkKnob(780, "spk_az", -180, 180); spkKnob(860, "spk_el", -90, 90); spkKnob(940, "spk_dist", 0.3, 30);
    pill({ 1030, 220, 90, 26 }, "TEST", [] { return Theme::sel(); }, [this, &h] { h.testSpeaker.store(selSpk); h.testSpkLeft.store(48000); }, 8.5f);
    // ---- the chosen sound
    auto srcKnob = [&](int x, int y, const char* what, double lo, double hi, ColourFn col) {
        add<Knob>({ x, y, 40, 40 }, [this, &m, what] { return m.get(std::string(what) + std::to_string(selSrc)); },
                  [this, &m, what](double v) { m.set(std::string(what) + std::to_string(selSrc), v); }, lo, hi, 0, std::move(col)); };
    int y0 = 548;
    auto sc = [this] { return Theme::strip(selSrc); };
    pill({ 760, y0, 150, 26 }, "", [this, &m] { return m.get("amb" + std::to_string(selSrc)) > 0.5 ? Theme::strip(selSrc) : Theme::btn(); },
         [this, &m] { auto id = "amb" + std::to_string(selSrc); m.set(id, m.get(id) > 0.5 ? 0 : 1); if (m.get("amb_on") < 0.5) m.set("amb_on", 1); })->dynText =
        [this, &m] { return (m.get("amb" + std::to_string(selSrc)) > 0.5 ? U8("● ") : juce::String()) + srcName(selSrc) + (m.get("amb" + std::to_string(selSrc)) > 0.5 ? " PLACED" : " - PLACE IT"); };
    srcKnob(780, y0 + 44, "amb_az", -180, 180, sc); srcKnob(860, y0 + 44, "amb_el", -90, 90, sc); srcKnob(940, y0 + 44, "amb_dist", 0.3, 20, sc); srcKnob(1020, y0 + 44, "amb_w", 0, 100, sc);
    for (int i = 0; i < 5; ++i)
        pill({ 760 + i * 104, y0 + 124, 98, 26 }, MOT[i], [this, &m, i] { return (int) std::lround(m.get("amb_mot" + std::to_string(selSrc))) == i ? Theme::amber() : Theme::btn(); },
             [this, &m, i] { m.set("amb_mot" + std::to_string(selSrc), i); });
    srcKnob(780, y0 + 164, "amb_spd", 0, 100, [] { return Theme::amber(); }); srcKnob(860, y0 + 164, "amb_rng", 0, 180, [] { return Theme::amber(); });
    pill({ 940, y0 + 172, 120, 26 }, "IN TEMPO", [this, &m] { return m.get("amb_sync" + std::to_string(selSrc)) > 0.5 ? Theme::amber() : Theme::btn(); },
         [this, &m] { auto id = "amb_sync" + std::to_string(selSrc); m.set(id, m.get(id) > 0.5 ? 0 : 1); });
    // 1.4: two pages - OUTPUTS (what goes to which output of the sound card) and SPEAKERS & SPACE (ambisonics)
    routing = std::make_unique<RoutingView>(cv);
    addChildComponent(*routing);
    routing->setBounds(box.getX() + 20, box.getY() + 64, box.getWidth() - 40, box.getHeight() - 80);
    const char* TB[2] = { "OUTPUTS", "SPEAKERS & SPACE" };
    for (int t = 0; t < 2; ++t) {
        auto* p = new Pill(TB[t], [this, t] { return tab == t ? Theme::sel() : Theme::btn(); }, 10.0f, 8.0f);
        p->onClick = [this, t] { showTab(t); };
        addAndMakeVisible(p); p->setBounds(box.getX() + 24 + t * 196, box.getY() + 14, 188, 34);
        tabPills.push_back(p); kidsTabs.add(p);
    }
    showTab(1);
    startTimerHz(30);
}

void SpatialPanel::showTab(int t) {
    tab = juce::jlimit(0, 1, t);
    for (int i = 1; i < kids.size(); ++i) kids[i]->setVisible(tab == 1);       // (0: CLOSE)
    routing->setVisible(tab == 0);
    repaint();
}

void SpatialPanel::visibilityChanged() { if (isVisible()) toFront(true); }

float SpatialPanel::roomM() const {
    auto& m = cv.model(); float far = 3.0f;
    for (int k = 0; k < (int) m.get("spk_n"); ++k) far = juce::jmax(far, (float) m.get("spk_dist" + std::to_string(k)));
    return far * 1.25f;
}
juce::Point<float> SpatialPanel::at(float az, float dist) const {
    float r = juce::jmin(1.0f, dist / roomM()) * radius, a = az * juce::MathConstants<float>::pi / 180.0f;
    return { box.getX() + centre.x + r * std::sin(a), box.getY() + centre.y - r * std::cos(a) };
}
void SpatialPanel::fromPoint(juce::Point<float> p, float& az, float& dist) const {
    float dx = p.x - (box.getX() + centre.x), dy = (box.getY() + centre.y) - p.y;
    az = std::atan2(dx, dy) * 180.0f / juce::MathConstants<float>::pi;
    dist = juce::jlimit(0.3f, roomM(), std::sqrt(dx * dx + dy * dy) / radius * roomM());
}

void SpatialPanel::paint(juce::Graphics& g) {
    auto& m = cv.model();
    g.fillAll(juce::Colours::black.withAlpha(0.45f));
    g.setColour(Theme::card()); g.fillRoundedRectangle(box.toFloat(), 14.0f);
    if (tab == 0) return;                                                                 // OUTPUTS draws itself
    lab(g, U8("ambisonics (3rd order, AllRAD): any layout up to 32 speakers - place each sound, still or moving"), { box.getX() + 420, box.getY() + 22, 640, 16 }, 8.0f, Theme::sub());
    // ---- the room from above
    auto c = juce::Point<float>(box.getX() + centre.x, box.getY() + centre.y);
    g.setColour(Theme::scr()); g.fillEllipse(c.x - radius, c.y - radius, radius * 2, radius * 2);
    float rm = roomM();
    for (int k = 1; k <= (int) rm; ++k) { float r = k / rm * radius; g.setColour(Theme::line().withMultipliedAlpha(k % 5 == 0 ? 0.8f : 0.35f)); g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, 1.0f); }
    g.setColour(Theme::line()); g.drawLine(c.x, c.y - radius, c.x, c.y + radius, 0.6f); g.drawLine(c.x - radius, c.y, c.x + radius, c.y, 0.6f);
    lab(g, "FRONT", { (int) c.x - 40, (int) (c.y - radius) - 20, 80, 16 }, 8.0f, Theme::sub(), 2, juce::Justification::centred);
    lab(g, "BACK", { (int) c.x - 40, (int) (c.y + radius) + 4, 80, 16 }, 8.0f, Theme::sub(), 2, juce::Justification::centred);
    lab(g, juce::String((int) std::round(rm)) + " m", { (int) (c.x + radius) - 40, (int) c.y + 2, 40, 14 }, 7.0f, Theme::sub(), 1, juce::Justification::centredRight);
    // the listener
    g.setColour(Theme::txt()); g.fillEllipse(c.x - 5, c.y - 5, 10, 10);
    // speakers
    int n = (int) m.get("spk_n");
    for (int k = 0; k < n; ++k) {
        float az = (float) m.get("spk_az" + std::to_string(k)), el = (float) m.get("spk_el" + std::to_string(k)), d = (float) m.get("spk_dist" + std::to_string(k));
        auto p = at(az, d * std::cos(el * juce::MathConstants<float>::pi / 180.0f));
        bool sel = k == selSpk;
        auto col = el > 5 ? Theme::blue() : (el < -5 ? Theme::violet() : Theme::green());
        g.setColour(col); g.fillRoundedRectangle(p.x - 13, p.y - 13, 26, 26, 5);
        if (sel) { g.setColour(Theme::txt()); g.drawRoundedRectangle(p.x - 15, p.y - 15, 30, 30, 6, 2); }
        labOn(g, juce::String(k + 1), { (int) p.x - 13, (int) p.y - 13, 26, 26 }, 9.0f, col);
        if (std::abs(el) > 5) lab(g, (el > 0 ? U8("↑") : U8("↓")) + juce::String((int) std::abs(el)) + U8("°"), { (int) p.x - 20, (int) p.y + 14, 40, 12 }, 7.0f, col, 2, juce::Justification::centred);
    }
    // the sounds (where they are now)
    auto* amb = cv.engine().ambi;
    for (int s = 0; s < 12; ++s) {
        bool on = m.get("amb" + std::to_string(s)) > 0.5;
        if (!on && s != selSrc) continue;
        float az = amb && on ? amb->curAz[s].load() : (float) m.get("amb_az" + std::to_string(s));
        float el = amb && on ? amb->curEl[s].load() : (float) m.get("amb_el" + std::to_string(s));
        float d = (float) m.get("amb_dist" + std::to_string(s)) * std::cos(el * juce::MathConstants<float>::pi / 180.0f);
        auto p = at(az, d);
        auto col = Theme::strip(s);
        float lv = amb ? juce::jmin(1.0f, amb->level[s] * 3.0f) : 0.0f;
        float rr = 11 + 8 * (float) m.get("amb_w" + std::to_string(s)) / 100.0f;
        g.setColour(col.withAlpha(on ? 0.25f + 0.5f * lv : 0.15f)); g.fillEllipse(p.x - rr - 6, p.y - rr - 6, (rr + 6) * 2, (rr + 6) * 2);
        g.setColour(on ? col : col.withAlpha(0.4f)); g.fillEllipse(p.x - rr, p.y - rr, rr * 2, rr * 2);
        if (s == selSrc) { g.setColour(Theme::txt()); g.drawEllipse(p.x - rr - 2, p.y - rr - 2, (rr + 2) * 2, (rr + 2) * 2, 2); }
        labOn(g, s < 4 ? "L" + juce::String(s + 1) : juce::String(s - 3), { (int) (p.x - rr), (int) (p.y - rr), (int) (rr * 2), (int) (rr * 2) }, 8.5f, on ? col : col.withAlpha(0.4f).overlaidWith(Theme::scr()));
        if (std::abs(el) > 5) lab(g, (el > 0 ? U8("↑") : U8("↓")) + juce::String((int) std::abs(el)) + U8("°"), { (int) p.x - 20, (int) (p.y + rr + 2), 40, 12 }, 7.0f, col, 2, juce::Justification::centred);
    }
    lab(g, U8("drag a speaker or a sound  ·  green: at ear height, blue: above, violet: below"), { box.getX() + 40, box.getBottom() - 30, 700, 16 }, 7.5f, Theme::sub());
    // ---- the right side
    int X = box.getX() + 760, Y = box.getY();
    lab(g, "LAYOUT", { X, Y + 70, 200, 18 }, 10.0f, Theme::txt(), 2);
    lab(g, "SPEAKERS", { X, Y + 172, 100, 26 }, 9.0f, Theme::txt(), 2);
    lab(g, "SPEAKER", { X + 180, Y + 172, 80, 26 }, 9.0f, Theme::txt(), 2);
    const char* KN[3] = { "DIRECTION", "HEIGHT", "DISTANCE m" };
    for (int i = 0; i < 3; ++i) lab(g, KN[i], { X + 8 + i * 80, Y + 254, 64, 14 }, 7.0f, Theme::sub(), 1, juce::Justification::centred);
    {   auto id = std::to_string(selSpk);
        lab(g, juce::String((int) m.get("spk_az" + id)) + U8("°  ·  ") + juce::String((int) m.get("spk_el" + id)) + U8("°  ·  ") + juce::String(m.get("spk_dist" + id), 1) + " m  ·  output " + juce::String(selSpk + 1),
            { X + 270, Y + 254, 340, 14 }, 8.0f, Theme::txt(), 1); }
    lab(g, U8("Measure each speaker from where the audience sits: direction (0° = front, 90° = right), height, distance. Nearer speakers are delayed and softer automatically, so the sound arrives together."),
        { X, Y + 278, 580, 30 }, 7.5f, Theme::sub());
    // the sounds
    lab(g, "SOUNDS", { X, Y + 318, 200, 18 }, 10.0f, Theme::txt(), 2);
    srcRows.clear();
    for (int s = 0; s < 12; ++s) {
        auto r = juce::Rectangle<int>(X + (s % 6) * 98, Y + 344 + (s / 6) * 34, 92, 28);
        bool on = m.get("amb" + std::to_string(s)) > 0.5;
        g.setColour(on ? Theme::strip(s) : Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 6);
        if (s == selSrc) { g.setColour(Theme::txt()); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6, 2); }
        labOn(g, srcName(s), r, 8.0f, on ? Theme::strip(s) : Theme::btn());
        srcRows.push_back({ r, s });
    }
    lab(g, U8("click a sound to choose it, again to place it in the space (it leaves the stereo mix; its effects stay)"), { X, Y + 416, 580, 14 }, 7.5f, Theme::sub());
    const char* SK[4] = { "DIRECTION", "HEIGHT", "DISTANCE m", "WIDTH" };
    for (int i = 0; i < 4; ++i) lab(g, SK[i], { X + 8 + i * 80, Y + 548 + 86, 64, 14 }, 7.0f, Theme::sub(), 1, juce::Justification::centred);
    { auto id = std::to_string(selSrc);
      lab(g, juce::String((int) m.get("amb_az" + id)) + U8("°  ·  ") + juce::String((int) m.get("amb_el" + id)) + U8("°  ·  ") + juce::String(m.get("amb_dist" + id), 1) + " m", { X + 340, Y + 548 + 54, 200, 16 }, 8.5f, Theme::txt(), 1); }
    lab(g, "MOTION", { X, Y + 548 + 104, 200, 16 }, 9.0f, Theme::txt(), 2);
    lab(g, "SPEED", { X + 8, Y + 548 + 206, 64, 14 }, 7.0f, Theme::sub(), 1, juce::Justification::centred);
    lab(g, "RANGE", { X + 88, Y + 548 + 206, 64, 14 }, 7.0f, Theme::sub(), 1, juce::Justification::centred);
    lab(g, U8("CIRCLE goes round, SWING moves left - right by RANGE, WANDER drifts, FLY OVER passes above you. IN TEMPO: one turn every 1 - 8 bars. Headphones hear a stereo version."),
        { X + 310, Y + 548 + 164, 290, 44 }, 7.5f, Theme::sub());
    if (m.get("amb_on") < 0.5) lab(g, U8("AMBISONICS is off: every sound plays in the stereo mix as usual"), { X, Y + 46, 580, 16 }, 8.0f, Theme::amber(), 2);
}

void SpatialPanel::mouseDown(const juce::MouseEvent& e) {
    setViewportIgnoreDragFlag(true);
    if (tab == 0) return;
    auto p = e.position;
    if (!box.contains(p.toInt())) { cv.host.saveSetup(); setVisible(false); return; }
    auto& m = cv.model();
    for (auto& r : srcRows) if (r.first.contains(p.toInt())) {
        if (selSrc == r.second) { auto id = "amb" + std::to_string(r.second); m.set(id, m.get(id) > 0.5 ? 0 : 1); if (m.get(id) > 0.5 && m.get("amb_on") < 0.5) m.set("amb_on", 1); }
        selSrc = r.second; repaint(); return;
    }
    dragSpk = dragSrc = -1;
    float best = 22.0f;
    for (int s = 0; s < 12; ++s) {
        bool on = m.get("amb" + std::to_string(s)) > 0.5; if (!on && s != selSrc) continue;
        float el = (float) m.get("amb_el" + std::to_string(s));
        auto q = at((float) m.get("amb_az" + std::to_string(s)), (float) m.get("amb_dist" + std::to_string(s)) * std::cos(el * juce::MathConstants<float>::pi / 180.0f));
        if (q.getDistanceFrom(p) < best) { best = q.getDistanceFrom(p); dragSrc = s; }
    }
    if (dragSrc < 0)
        for (int k = 0; k < (int) m.get("spk_n"); ++k) {
            float el = (float) m.get("spk_el" + std::to_string(k));
            auto q = at((float) m.get("spk_az" + std::to_string(k)), (float) m.get("spk_dist" + std::to_string(k)) * std::cos(el * juce::MathConstants<float>::pi / 180.0f));
            if (q.getDistanceFrom(p) < best) { best = q.getDistanceFrom(p); dragSpk = k; }
        }
    if (dragSrc >= 0) selSrc = dragSrc;
    if (dragSpk >= 0) selSpk = dragSpk;
    repaint();
}
void SpatialPanel::mouseDrag(const juce::MouseEvent& e) {
    if (tab == 0) return;
    auto& m = cv.model(); float az, d; fromPoint(e.position, az, d);
    az = std::round(az);
    if (dragSrc >= 0) { auto id = std::to_string(dragSrc); float el = (float) m.get("amb_el" + id); m.set("amb_az" + id, az); m.set("amb_dist" + id, std::round(d / std::max(0.2f, std::cos(el * juce::MathConstants<float>::pi / 180.0f)) * 10) / 10); }
    if (dragSpk >= 0) { auto id = std::to_string(dragSpk); float el = (float) m.get("spk_el" + id); m.set("spk_az" + id, az); m.set("spk_dist" + id, std::round(d / std::max(0.2f, std::cos(el * juce::MathConstants<float>::pi / 180.0f)) * 10) / 10);
        cv.host.speakerLayout = 2; cv.host.applySpeakers(); }
    repaint();
}
void SpatialPanel::mouseUp(const juce::MouseEvent&) { if (dragSpk >= 0) cv.host.saveSetup(); dragSpk = dragSrc = -1; }

void SpatialPanel::preset(int which) {
    auto& m = cv.model(); auto& h = cv.host;
    std::vector<std::pair<float, float>> L;
    switch (which) {
        case 0: L = { { -30, 0 }, { 30, 0 } }; break;
        case 1: L = { { -45, 0 }, { 45, 0 }, { 135, 0 }, { -135, 0 } }; break;
        case 2: L = { { -30, 0 }, { 30, 0 }, { 0, 0 }, { -110, 0 }, { 110, 0 } }; break;             // L R C Ls Rs (the usual order without LFE)
        case 3: L = { { -30, 0 }, { 30, 0 }, { 0, 0 }, { -90, 0 }, { 90, 0 }, { -150, 0 }, { 150, 0 } }; break;
        case 4: for (int k = 0; k < 6; ++k) L.push_back({ -150.0f + k * 60.0f, 0.0f }); break;
        case 5: for (int k = 0; k < 8; ++k) L.push_back({ -157.5f + k * 45.0f, 0.0f }); break;
        case 6: for (int k = 0; k < 8; ++k) L.push_back({ -157.5f + k * 45.0f, 0.0f }); for (int k = 0; k < 4; ++k) L.push_back({ -135.0f + k * 90.0f, 45.0f }); break;
        case 7: for (int k = 0; k < 4; ++k) L.push_back({ -135.0f + k * 90.0f, -35.0f }); for (int k = 0; k < 4; ++k) L.push_back({ -135.0f + k * 90.0f, 35.0f }); break;
        case 8: L = { { -30, 0 }, { 30, 0 }, { 0, 0 }, { -110, 0 }, { 110, 0 }, { -45, 45 }, { 45, 45 }, { -135, 45 }, { 135, 45 } }; break;
        default: for (int k = 0; k < 16; ++k) L.push_back({ -168.75f + k * 22.5f, 0.0f }); break;
    }
    // sorted for a ring: speaker 1 front-left, then clockwise (rings only - the cinema layouts keep their usual order)
    m.set("spk_n", (double) L.size());
    for (size_t k = 0; k < L.size(); ++k) { auto id = std::to_string(k); m.set("spk_az" + id, L[k].first); m.set("spk_el" + id, L[k].second); m.set("spk_dist" + id, 2.0); }
    h.speakerLayout = 2; h.applySpeakers(); h.applyRouting(); h.saveSetup();
    selSpk = 0; repaint();
}

} // namespace arco

namespace arco {

// ===================================================================== OUTPUTS
static const int NSRC = 14;
RoutingView::RoutingView(Canvas& c) : cv(c) {
    auto pill = [&](juce::Rectangle<int> r, const juce::String& t, ColourFn col, std::function<void()> f) {
        auto* p = new Pill(t, std::move(col), 8.0f, 6.0f); p->onClick = std::move(f); kids.add(p); addAndMakeVisible(p); p->setBounds(r); return p; };
    pill({ 0, 44, 250, 28 }, U8("ONE OUTPUT EACH (for a mixing desk)"), [] { return Theme::blue(); }, [this] { oneEach(); });
    pill({ 258, 44, 110, 28 }, "CLEAR ALL", [] { return Theme::btn(); }, [this] { clearAll(); });
    pill({ 376, 44, 150, 28 }, U8("SOUND CARD (SETUP) ▸"), [] { return Theme::btn(); }, [this] { cv.openSpatial(false); cv.openSetup(true); });
    startTimerHz(15);
}
int RoutingView::numOuts() const {
    if (auto* d = cv.host.devices.getCurrentAudioDevice()) return juce::jlimit(2, NSPK, d->getActiveOutputChannels().countNumberOfSetBits());
    if (auto f = juce::SystemStats::getEnvironmentVariable("ARCO_FAKE_OUTS", ""); f.isNotEmpty()) return juce::jlimit(2, NSPK, f.getIntValue());   // (screenshots)
    return 2;
}
juce::String RoutingView::sourceName(int s) const {
    if (s < 4) return "LIVE " + juce::String(s + 1) + (cv.host.synthInto > 0 && cv.host.synthInto == s ? U8("  ·  SYNTH") : juce::String());
    if (s < 12) return "TRACK " + juce::String(s - 3);
    return s == 12 ? "DRUMS" : "HALO";
}
static const float NAMEW = 200, ROOMW = 78, TOP = 150;
static void txtOn(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour bg) {
    g.setColour(Theme::textOn(bg)); g.setFont(Theme::font(fs, 2)); g.drawFittedText(t, r, juce::Justification::centred, 1, 0.8f);
}
juce::Rectangle<float> RoutingView::roomCell(int src) const {
    float rh = juce::jmin(36.0f, (getHeight() - TOP - 40) / (float) NSRC);
    return { NAMEW, TOP + src * rh, ROOMW, rh };
}
juce::Rectangle<float> RoutingView::cell(int src, int out) const {
    float rh = juce::jmin(36.0f, (getHeight() - TOP - 40) / (float) NSRC);
    float cw = juce::jmin(48.0f, (getWidth() - NAMEW - ROOMW - 12) / (float) numOuts());
    return { NAMEW + ROOMW + 12 + out * cw, TOP + src * rh, cw, rh };
}
bool RoutingView::hit(juce::Point<float> p, int& src, int& out) const {
    for (int s = 0; s < NSRC; ++s) {
        if (roomCell(s).contains(p)) { src = s; out = -1; return true; }
        for (int k = 0; k < numOuts(); ++k) if (cell(s, k).contains(p)) { src = s; out = k; return true; }
    }
    return false;
}
void RoutingView::paint(juce::Graphics& g) {
    auto& e = cv.engine(); auto& m = cv.model();
    int no = numOuts(), spk = (int) m.get("spk_n"), pf = e.phonesFirst.load();
    lab(g, U8("Every sound goes to the ROOM (the main speakers, laid out in SPEAKERS & SPACE) - and, if you want, straight to any output of the sound card: "
              "one channel each on the room's mixing desk, a monitor for a musician, a recorder. Turn ROOM off and it goes only where you send it."),
        { 0, 0, getWidth(), 40 }, 8.5f, Theme::sub());
    juce::String dev = "no sound card";
    if (auto* d = cv.host.devices.getCurrentAudioDevice()) dev = d->getName() + "  ·  " + juce::String(no) + " outputs";
    lab(g, dev, { 540, 44, getWidth() - 540, 28 }, 9.0f, Theme::txt(), 2, juce::Justification::centredRight);
    if (no <= 2) lab(g, U8("This sound card has 2 outputs. Connect an interface with more (on the iPad: USB, through the adapter) and they appear here."), { 0, 78, getWidth(), 18 }, 8.5f, Theme::amber());
    // the column heads
    lab(g, "SOUND", { 0, (int) TOP - 34, (int) NAMEW, 18 }, 8.0f, Theme::sub(), 2);
    lab(g, "ROOM", { (int) NAMEW, (int) TOP - 34, (int) ROOMW, 18 }, 8.0f, Theme::sub(), 2, juce::Justification::centred);
    lab(g, "OUTPUTS OF THE SOUND CARD", { (int) (NAMEW + ROOMW + 12), (int) TOP - 52, 400, 16 }, 8.0f, Theme::sub(), 2);
    juce::StringArray chNames; if (auto* d = cv.host.devices.getCurrentAudioDevice()) chNames = d->getOutputChannelNames();
    for (int k = 0; k < no; ++k) {
        auto c = cell(0, k); auto hr = juce::Rectangle<float>(c.getX(), TOP - 34, c.getWidth(), 30).reduced(1);
        bool isHp = pf >= 0 && (k == pf || k == pf + 1), isRoom = k < spk;
        g.setColour(isHp ? Theme::amber().withAlpha(0.25f) : (isRoom ? Theme::green().withAlpha(0.2f) : Theme::card2())); g.fillRoundedRectangle(hr, 4.0f);
        lab(g, juce::String(k + 1), hr.toNearestInt().withTrimmedBottom(12), 9.5f, Theme::txt(), 2, juce::Justification::centred);
        lab(g, isHp ? "HP" : (isRoom ? "ROOM" : ""), hr.toNearestInt().removeFromBottom(12), 6.0f, Theme::sub(), 2, juce::Justification::centred);
    }
    int ntn = juce::jlimit(1, 8, (int) cv.snap("ntn")); if (cv.snap("ntn") < 1) ntn = 4;
    for (int s = 0; s < NSRC; ++s) {
        bool used = s < 4 ? (s < cv.host.nLive || (cv.host.synthInto > 0 && s == cv.host.synthInto)) : (s < 12 ? (s - 4) < ntn : true);
        auto rr = roomCell(s); auto row = juce::Rectangle<float>(0, rr.getY(), (float) getWidth(), rr.getHeight());
        if (s % 2 == 0) { g.setColour(Theme::card2S().withAlpha(0.35f)); g.fillRect(row); }
        g.setColour(s < 12 ? Theme::strip(s) : Theme::fx(s == 12 ? "drm" : "halo")); g.fillEllipse(6, rr.getCentreY() - 4, 8, 8);
        lab(g, sourceName(s), { 22, (int) rr.getY(), (int) NAMEW - 24, (int) rr.getHeight() }, 9.0f, used ? Theme::txt() : Theme::sub(), used ? 2 : 0);
        // ROOM
        bool room = s < 12 ? m.get("room" + std::to_string(s)) > 0.5 : true;
        auto rc = rr.reduced(8, 5);
        g.setColour(room ? Theme::green() : Theme::btn()); g.fillRoundedRectangle(rc, 5.0f);
        txtOn(g, s < 12 ? (room ? "ON" : "OFF") : "ALWAYS", rc.toNearestInt(), 7.5f, room ? Theme::green() : Theme::btn());
        for (int k = 0; k < no; ++k) {
            auto c = cell(s, k).reduced(3, 5);
            bool on = e.grid[s][k].load() > 0;
            g.setColour(on ? (s < 12 ? Theme::strip(s) : Theme::blue()) : Theme::scr()); g.fillRoundedRectangle(c, 4.0f);
            if (on) { g.setColour(Theme::textOn(s < 12 ? Theme::strip(s) : Theme::blue())); g.fillEllipse(c.withSizeKeepingCentre(7, 7)); }
        }
    }
    lab(g, U8("Touch a square to send that sound to that output (drag along a row for several). ROOM speakers (green) play the main mix; HP = the headphones."),
        { 0, getHeight() - 30, getWidth(), 24 }, 8.0f, Theme::sub());
}
void RoutingView::mouseDown(const juce::MouseEvent& e) {
    setViewportIgnoreDragFlag(true);
    int s, k; if (!hit(e.position, s, k)) return;
    auto& m = cv.model();
    if (k < 0) { if (s < 12) { auto id = "room" + std::to_string(s); m.set(id, m.get(id) > 0.5 ? 0 : 1); } paintValue = -1; repaint(); return; }
    bool on = cv.engine().grid[s][k].load() > 0;
    paintValue = on ? 0 : 1;
    cv.engine().grid[s][k].store((float) paintValue);
    repaint();
}
void RoutingView::mouseDrag(const juce::MouseEvent& e) {
    if (paintValue < 0) return;
    int s, k; if (!hit(e.position, s, k) || k < 0) return;
    cv.engine().grid[s][k].store((float) paintValue); repaint();
}
void RoutingView::mouseUp(const juce::MouseEvent&) { paintValue = -1; cv.host.saveSetup(); }
void RoutingView::clearAll() { for (int s = 0; s < NSRC; ++s) for (int k = 0; k < NSPK; ++k) cv.engine().grid[s][k].store(0.0f); cv.host.saveSetup(); repaint(); }
void RoutingView::oneEach() {
    int no = numOuts(); auto& m = cv.model();
    int first = juce::jmax(2, (int) m.get("spk_n")), pf = cv.engine().phonesFirst.load();
    clearAll();
    int ntn = juce::jlimit(1, 8, (int) cv.snap("ntn")); if (cv.snap("ntn") < 1) ntn = 4;
    int k = first, done = 0;
    for (int s = 0; s < NSRC; ++s) {
        bool used = s < 4 ? (s < cv.host.nLive || (cv.host.synthInto > 0 && s == cv.host.synthInto)) : (s < 12 ? (s - 4) < ntn : false);
        if (!used) continue;
        while (k < no && pf >= 0 && (k == pf || k == pf + 1)) ++k;
        if (k >= no) break;
        cv.engine().grid[s][k].store(1.0f); ++k; ++done;
    }
    cv.host.saveSetup();
    cv.showMessage(done > 0 ? juce::String(done) + U8(" sounds - one output each, from output ") + juce::String(first + 1)
                            : U8("no free outputs: connect a sound card with more outputs"));
    repaint();
}

} // namespace arco
