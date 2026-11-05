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
static juce::String srcName(int s) { return s < 4 ? "Live " + juce::String(s + 1) : "Track " + juce::String(s - 3); }
static const char* MOT[5] = { "Still", "Circle", "Swing", "Wander", "Fly over" };

SpatialPanel::SpatialPanel(Canvas& c) : cv(c) {
    auto& m = cv.model(); auto& h = cv.host;
    auto pill = [&](juce::Rectangle<int> r, const juce::String& t, ColourFn col, std::function<void()> f, float fs = 8.0f) { auto* p = add<Pill>(r, t, std::move(col), fs, 6.0f); p->onClick = std::move(f); return p; };
    pill({ 1360 - 112, 16, 92, 34 }, U8("✕ Close"), [] { return Theme::btn(); }, [this] { cv.host.saveSetup(); setVisible(false); });
    // 2.3: SURROUND - the sounds placed all round the audience (the name of the method is not shown)
    auto* on = pill({ 1360 - 290, 16, 170, 34 }, "Surround", [&m] { return m.get("amb_on") > 0.5 ? Theme::green() : Theme::btn(); }, [&m] { m.set("amb_on", m.get("amb_on") > 0.5 ? 0 : 1); });
    on->tint = true;
    on->dynText = [&m] { return m.get("amb_on") > 0.5 ? juce::String(U8("Surround is on")) : juce::String(U8("Surround is off")); };
    on->setComponentID("p:amb_on");
    // ---- layouts
    const char* LAY[10] = { "Stereo", "Quad", "5.0", "7.0", "Ring 6", "Ring 8", "Dome 8+4", "Cube 8", "5.0.4", "Ring 16" };
    for (int i = 0; i < 10; ++i) pill({ 760 + (i % 5) * 116, 112 + (i / 5) * 42, 110, 38 }, LAY[i], [] { return Theme::btn(); }, [this, i] { preset(i); }, 9.0f);
    // ---- the speakers: how many, the chosen one
    auto* nb = add<ValueBox>({ 860, 210, 56, 30 }, [&m] { return m.get("spk_n"); }, [&m, &h](double v) { m.set("spk_n", v); h.speakerLayout = 2; h.applySpeakers(); h.applyRouting(); }, 2, 32, 0, [] { return Theme::green(); });
    nb->fsize = 12.0f;
    auto* sb = add<ValueBox>({ 1040, 210, 56, 30 }, [this] { return (double) (selSpk + 1); }, [this, &m](double v) { selSpk = juce::jlimit(0, (int) m.get("spk_n") - 1, (int) v - 1); repaint(); }, 1, 32, 0, [] { return Theme::sel(); });
    sb->fsize = 12.0f;
    auto spkKnob = [&](int x, const char* what, double lo, double hi) {
        add<Knob>({ x, 254, 40, 40 }, [this, &m, what] { return m.get(std::string(what) + std::to_string(selSpk)); },
                  [this, &m, &h, what](double v) { m.set(std::string(what) + std::to_string(selSpk), v); h.speakerLayout = 2; h.applySpeakers(); }, lo, hi, 0, [] { return Theme::green(); }); };
    spkKnob(780, "spk_az", -180, 180); spkKnob(860, "spk_el", -90, 90); spkKnob(940, "spk_dist", 0.3, 30);
    pill({ 1130, 206, 110, 38 }, U8("▶ Test it"), [] { return Theme::btn(); }, [this, &h] { h.testSpeaker.store(selSpk); h.testSpkLeft.store(48000); }, 9.0f);
    // ---- the chosen sound
    auto srcKnob = [&](int x, int y, const char* what, double lo, double hi, ColourFn col) {
        add<Knob>({ x, y, 40, 40 }, [this, &m, what] { return m.get(std::string(what) + std::to_string(selSrc)); },
                  [this, &m, what](double v) { m.set(std::string(what) + std::to_string(selSrc), v); }, lo, hi, 0, std::move(col)); };
    int y0 = 476;
    auto sc = [this] { return Theme::strip(selSrc); };
    pill({ 760, y0, 200, 36 }, "", [this, &m] { return m.get("amb" + std::to_string(selSrc)) > 0.5 ? Theme::strip(selSrc) : Theme::btn(); },
         [this, &m] { auto id = "amb" + std::to_string(selSrc); m.set(id, m.get(id) > 0.5 ? 0 : 1); if (m.get("amb_on") < 0.5) m.set("amb_on", 1); })->dynText =
        [this, &m] { return m.get("amb" + std::to_string(selSrc)) > 0.5 ? srcName(selSrc) + U8(" is placed  ·  back to stereo") : U8("Place ") + srcName(selSrc) + U8(" in the room"); };
    srcKnob(780, y0 + 50, "amb_az", -180, 180, sc); srcKnob(860, y0 + 50, "amb_el", -90, 90, sc); srcKnob(940, y0 + 50, "amb_dist", 0.3, 20, sc); srcKnob(1020, y0 + 50, "amb_w", 0, 100, sc);
    for (int i = 0; i < 5; ++i)
        pill({ 760 + i * 116, y0 + 172, 110, 36 }, MOT[i], [this, &m, i] { return (int) std::lround(m.get("amb_mot" + std::to_string(selSrc))) == i ? Theme::amber() : Theme::btn(); },
             [this, &m, i] { m.set("amb_mot" + std::to_string(selSrc), i); });
    srcKnob(780, y0 + 222, "amb_spd", 0, 100, [] { return Theme::amber(); }); srcKnob(860, y0 + 222, "amb_rng", 0, 180, [] { return Theme::amber(); });
    pill({ 940, y0 + 226, 120, 34 }, "In tempo", [this, &m] { return m.get("amb_sync" + std::to_string(selSrc)) > 0.5 ? Theme::amber() : Theme::btn(); },
         [this, &m] { auto id = "amb_sync" + std::to_string(selSrc); m.set(id, m.get(id) > 0.5 ? 0 : 1); });
    // 1.4: two pages - OUTPUTS (what goes to which output of the sound card) and SPEAKERS & SPACE (ambisonics)
    routing = std::make_unique<RoutingView>(cv);
    addChildComponent(*routing);
    routing->setBounds(box.getX() + 20, box.getY() + 64, box.getWidth() - 40, box.getHeight() - 80);
    const char* TB[2] = { "Outputs", "Speakers & surround" };
    for (int t = 0; t < 2; ++t) {
        auto* p = new Pill(TB[t], [this, t] { return tab == t ? Theme::sel() : Theme::btn(); }, 10.0f, 8.0f);
        p->onClick = [this, t] { showTab(t); };
        p->tint = true; addAndMakeVisible(p); p->setBounds(box.getX() + 24 + t * 206, box.getY() + 16, 198, 38);
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
    g.fillAll(juce::Colours::black.withAlpha(Theme::softUI() ? 0.18f : 0.45f));
    if (Theme::softUI()) softRaised(g, box.toFloat(), 28.0f, 12.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(box.toFloat(), 14.0f); }
    drawSoftShades(g, *this);
    if (tab == 0) return;                                                                 // OUTPUTS draws itself
    const bool on = m.get("amb_on") > 0.5;
    lab(g, on ? U8("every sound you place plays from its own spot in the room - the others stay in the stereo mix")
              : U8("off: every sound plays in the stereo mix, as usual - turn it on, then place the sounds"), { box.getX() + 450, box.getY() + 24, 500, 22 }, 8.0f, on ? Theme::green() : Theme::sub());
    // ---- the room from above: a round well, rings every metre, the audience in the middle
    auto c = juce::Point<float>(box.getX() + centre.x, box.getY() + centre.y);
    auto room = juce::Rectangle<float>(c.x - radius, c.y - radius, radius * 2, radius * 2);
    if (Theme::softUI()) softWell(g, room, radius); else { g.setColour(Theme::scr()); g.fillEllipse(room); }
    float rm = roomM();
    for (int k = 1; k <= (int) rm; ++k) { float r = k / rm * radius; g.setColour(Theme::line().withMultipliedAlpha(k % 5 == 0 ? 0.7f : 0.3f)); g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, 1.0f); }
    g.setColour(Theme::line().withMultipliedAlpha(0.6f)); g.drawLine(c.x, c.y - radius + 12, c.x, c.y + radius - 12, 0.7f); g.drawLine(c.x - radius + 12, c.y, c.x + radius - 12, c.y, 0.7f);
    drawCaption(g, "FRONT", { (int) c.x - 40, (int) (c.y - radius) - 22, 80, 16 }, juce::Justification::centred);
    drawCaption(g, "BACK", { (int) c.x - 40, (int) (c.y + radius) + 6, 80, 16 }, juce::Justification::centred);
    drawCaption(g, "LEFT", { (int) (c.x - radius) - 48, (int) c.y - 8, 44, 16 }, juce::Justification::centredRight);
    drawCaption(g, "RIGHT", { (int) (c.x + radius) + 4, (int) c.y - 8, 48, 16 });
    lab(g, juce::String((int) std::round(rm)) + " m", { (int) (c.x + radius) - 48, (int) c.y + 4, 40, 14 }, 7.0f, Theme::sub(), 1, juce::Justification::centredRight);
    // the audience
    if (Theme::softUI()) { softRaised(g, juce::Rectangle<float>(c.x - 16, c.y - 16, 32, 32), 16.0f, 3.0f, Theme::btn()); }
    g.setColour(Theme::txt()); g.fillEllipse(c.x - 4, c.y - 4, 8, 8);
    drawCaption(g, "YOU", { (int) c.x - 30, (int) c.y + 18, 60, 14 }, juce::Justification::centred);
    // the speakers: round keys with their number; the light under them tells their height
    int n = (int) m.get("spk_n");
    for (int k = 0; k < n; ++k) {
        float az = (float) m.get("spk_az" + std::to_string(k)), el = (float) m.get("spk_el" + std::to_string(k)), d = (float) m.get("spk_dist" + std::to_string(k));
        auto p = at(az, d * std::cos(el * juce::MathConstants<float>::pi / 180.0f));
        bool sel = k == selSpk;
        auto col = el > 5 ? Theme::blue() : (el < -5 ? Theme::violet() : Theme::green());
        auto kr = juce::Rectangle<float>(p.x - 16, p.y - 16, 32, 32);
        if (Theme::softUI()) softKeyLite(g, kr, 16.0f, sel, col, sel ? 0.8f : 0.35f); else { g.setColour(col); g.fillEllipse(kr); }
        lab(g, juce::String(k + 1), kr.toNearestInt(), 9.5f, Theme::txt(), 2, juce::Justification::centred);
        if (std::abs(el) > 5) lab(g, (el > 0 ? U8("↑ ") : U8("↓ ")) + juce::String((int) std::abs(el)) + U8("°"), { (int) p.x - 24, (int) p.y + 17, 48, 12 }, 7.0f, col, 2, juce::Justification::centred);
    }
    // the sounds, where they are now: a light in their colour, as big as they are wide
    auto* amb = cv.engine().ambi;
    for (int s = 0; s < 12; ++s) {
        bool placed = m.get("amb" + std::to_string(s)) > 0.5;
        if (!placed && s != selSrc) continue;
        float az = amb && placed ? amb->curAz[s].load() : (float) m.get("amb_az" + std::to_string(s));
        float el = amb && placed ? amb->curEl[s].load() : (float) m.get("amb_el" + std::to_string(s));
        float d = (float) m.get("amb_dist" + std::to_string(s)) * std::cos(el * juce::MathConstants<float>::pi / 180.0f);
        auto p = at(az, d);
        auto col = Theme::strip(s);
        float lv = amb ? juce::jmin(1.0f, amb->level[s] * 3.0f) : 0.0f;
        float rr = 10 + 8 * (float) m.get("amb_w" + std::to_string(s)) / 100.0f;
        g.setColour(col.withAlpha(placed ? 0.18f + 0.4f * lv : 0.10f)); g.fillEllipse(p.x - rr - 8, p.y - rr - 8, (rr + 8) * 2, (rr + 8) * 2);
        g.setColour(placed ? col : col.withAlpha(0.45f)); g.fillEllipse(p.x - rr, p.y - rr, rr * 2, rr * 2);
        if (s == selSrc) { g.setColour(Theme::txt().withAlpha(0.7f)); g.drawEllipse(p.x - rr - 3, p.y - rr - 3, (rr + 3) * 2, (rr + 3) * 2, 1.5f); }
        labOn(g, s < 4 ? "L" + juce::String(s + 1) : juce::String(s - 3), { (int) (p.x - rr), (int) (p.y - rr), (int) (rr * 2), (int) (rr * 2) }, 8.5f, placed ? col : col.withAlpha(0.45f).overlaidWith(Theme::scr()));
    }
    lab(g, U8("drag a speaker or a sound  ·  the light under a speaker: green at ear height, blue above, violet below"), { box.getX() + 40, box.getBottom() - 34, 680, 16 }, 7.5f, Theme::sub());
    // ---- the right side: three steps
    int X = box.getX() + 760, Y = box.getY();
    auto step = [&](int num, const juce::String& t, const juce::String& sub, int y) {
        auto nr = juce::Rectangle<float>((float) X, (float) y, 24, 24);
        if (Theme::softUI()) softWellLite(g, nr, 12.0f); else { g.setColour(Theme::card2()); g.fillEllipse(nr); }
        lab(g, juce::String(num), nr.toNearestInt(), 9.0f, Theme::txt(), 2, juce::Justification::centred);
        lab(g, t, { X + 34, y, 300, 24 }, 11.5f, Theme::txt(), 2);
        lab(g, sub, { X + 220, y, 360, 24 }, 7.5f, Theme::sub(), 0, juce::Justification::centredRight); };
    step(1, "Your speakers", U8("choose a layout, or drag them where they really are"), Y + 74);
    drawCaption(g, "HOW MANY", { X, Y + 210, 96, 30 });
    drawCaption(g, "SPEAKER", { X + 196, Y + 210, 80, 30 });
    const char* KN[3] = { "DIRECTION", "HEIGHT", "DISTANCE" };
    for (int i = 0; i < 3; ++i) drawCaption(g, KN[i], { X + 4 + i * 80, Y + 298, 72, 14 }, juce::Justification::centred, 6.5f);
    {   auto id = std::to_string(selSpk);
        lab(g, juce::String((int) m.get("spk_az" + id)) + U8("°  ·  ") + juce::String((int) m.get("spk_el" + id)) + U8("° up  ·  ") + juce::String(m.get("spk_dist" + id), 1) + " m  ·  output " + juce::String(selSpk + 1),
            { X + 260, Y + 262, 320, 18 }, 8.5f, Theme::txt(), 1); }
    lab(g, U8("measured from where the audience sits (0° = front, 90° = right) - nearer speakers are delayed and softer, so the sound arrives together"),
        { X + 260, Y + 282, 320, 30 }, 7.0f, Theme::sub());
    g.setColour(Theme::line().withMultipliedAlpha(0.5f)); g.fillRect(X, Y + 326, 580, 1);
    step(2, "Place each sound", U8("choose one, then place it"), Y + 340);
    srcRows.clear();
    for (int s = 0; s < 12; ++s) {
        auto r = juce::Rectangle<int>(X + (s % 6) * 97, Y + 376 + (s / 6) * 44, 91, 38);
        bool placed = m.get("amb" + std::to_string(s)) > 0.5;
        if (Theme::softUI()) { softKeyLite(g, r.toFloat(), 19.0f, s == selSrc, Theme::strip(s), s == selSrc ? 0.6f : 0.0f);
                               softLed(g, { (float) r.getX() + 15.0f, (float) r.getCentreY() }, 3.0f, Theme::strip(s), placed); }
        else { g.setColour(placed ? Theme::strip(s) : Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 6); }
        lab(g, srcName(s), r.withTrimmedLeft(22), 8.5f, Theme::txt(), 1, juce::Justification::centred);
        srcRows.push_back({ r, s });
    }
    const char* SK[4] = { "DIRECTION", "HEIGHT", "DISTANCE", "WIDTH" };
    for (int i = 0; i < 4; ++i) drawCaption(g, SK[i], { X + 4 + i * 80, Y + 476 + 94, 72, 14 }, juce::Justification::centred, 6.5f);
    { auto id = std::to_string(selSrc);
      lab(g, juce::String((int) m.get("amb_az" + id)) + U8("°  ·  ") + juce::String((int) m.get("amb_el" + id)) + U8("° up  ·  ") + juce::String(m.get("amb_dist" + id), 1) + " m", { X + 340, Y + 476 + 56, 240, 16 }, 8.5f, Theme::txt(), 1); }
    g.setColour(Theme::line().withMultipliedAlpha(0.5f)); g.fillRect(X, Y + 600, 580, 1);
    step(3, "How it moves", U8("still, or moving round you - in tempo if you like"), Y + 614);
    drawCaption(g, "SPEED", { X + 4, Y + 476 + 266, 72, 14 }, juce::Justification::centred, 6.5f);
    drawCaption(g, "RANGE", { X + 84, Y + 476 + 266, 72, 14 }, juce::Justification::centred, 6.5f);
    lab(g, U8("Circle goes round you, Swing moves left - right by RANGE, Wander drifts, Fly over passes above. In tempo: one turn every 1 - 8 bars. Headphones hear a stereo version."),
        { X + 330, Y + 476 + 222, 250, 54 }, 7.0f, Theme::sub());
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
    getProperties().set("softShades", true);
    pill({ 0, 46, 270, 36 }, U8("One output each (for a mixing desk)"), [] { return Theme::btn(); }, [this] { oneEach(); });
    pill({ 280, 46, 120, 36 }, "Clear all", [] { return Theme::btn(); }, [this] { clearAll(); });
    pill({ 410, 46, 190, 36 }, U8("The sound card (Setup) ▸"), [] { return Theme::btn(); }, [this] { cv.openSpatial(false); cv.openSetup(true); });
    startTimerHz(15);
}
int RoutingView::numOuts() const {
    if (auto* d = cv.host.devices.getCurrentAudioDevice()) return juce::jlimit(2, NSPK, d->getActiveOutputChannels().countNumberOfSetBits());
    if (auto f = juce::SystemStats::getEnvironmentVariable("ARCO_FAKE_OUTS", ""); f.isNotEmpty()) return juce::jlimit(2, NSPK, f.getIntValue());   // (screenshots)
    return 2;
}
juce::String RoutingView::sourceName(int s) const {
    if (s < 4) return "Live " + juce::String(s + 1) + (cv.host.synthInto > 0 && cv.host.synthInto == s ? U8("  ·  synth") : juce::String());
    if (s < 12) return "Track " + juce::String(s - 3);
    return s == 12 ? "Drums" : "Halo";
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
    lab(g, U8("Every sound goes to the ROOM (the main speakers, laid out in SPEAKERS & SURROUND) - and, if you want, straight to any output of the sound card: "
              "one channel each on the room's mixing desk, a monitor for a musician, a recorder. Turn ROOM off and it goes only where you send it."),
        { 0, 0, getWidth(), 40 }, 8.5f, Theme::sub());
    juce::String dev = "no sound card";
    if (auto* d = cv.host.devices.getCurrentAudioDevice()) dev = d->getName() + "  ·  " + juce::String(no) + " outputs";
    lab(g, dev, { 540, 44, getWidth() - 540, 28 }, 9.0f, Theme::txt(), 2, juce::Justification::centredRight);
    if (no <= 2) lab(g, U8("This sound card has 2 outputs. Connect an interface with more (on the iPad: USB, through the adapter) and they appear here."), { 0, 78, getWidth(), 18 }, 8.5f, Theme::amber());
    // the column heads
    drawCaption(g, "SOUND", { 22, (int) TOP - 30, (int) NAMEW, 18 });
    drawCaption(g, "ROOM", { (int) NAMEW, (int) TOP - 30, (int) ROOMW, 18 }, juce::Justification::centred);
    drawCaption(g, "OUTPUTS OF THE SOUND CARD", { (int) (NAMEW + ROOMW + 12), (int) TOP - 52, 400, 16 });
    juce::StringArray chNames; if (auto* d = cv.host.devices.getCurrentAudioDevice()) chNames = d->getOutputChannelNames();
    for (int k = 0; k < no; ++k) {
        auto c = cell(0, k); auto hr = juce::Rectangle<float>(c.getX(), TOP - 34, c.getWidth(), 30).reduced(1);
        bool isHp = pf >= 0 && (k == pf || k == pf + 1), isRoom = k < spk;
        if (Theme::softUI()) { softWellLite(g, hr, 8.0f); if (isHp || isRoom) softLed(g, { hr.getRight() - 7, hr.getY() + 7 }, 2.4f, isHp ? Theme::amber() : Theme::green(), true); }
        else { g.setColour(isHp ? Theme::amber().withAlpha(0.25f) : (isRoom ? Theme::green().withAlpha(0.2f) : Theme::card2())); g.fillRoundedRectangle(hr, 4.0f); }
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
        if (Theme::softUI()) { softKeyLite(g, rc, rc.getHeight() * 0.5f, room, Theme::green(), room ? 0.6f : 0.0f);
                               lab(g, s < 12 ? (room ? "On" : "Off") : "Always", rc.toNearestInt(), 7.5f, Theme::txt(), 1, juce::Justification::centred); }
        else { g.setColour(room ? Theme::green() : Theme::btn()); g.fillRoundedRectangle(rc, 5.0f);
               txtOn(g, s < 12 ? (room ? "ON" : "OFF") : "ALWAYS", rc.toNearestInt(), 7.5f, room ? Theme::green() : Theme::btn()); }
        for (int k = 0; k < no; ++k) {
            auto c = cell(s, k).reduced(3, 5);
            bool on = e.grid[s][k].load() > 0;
            auto sc = s < 12 ? Theme::strip(s) : Theme::blue();
            if (Theme::softUI()) { float rr2 = juce::jmin(c.getWidth(), c.getHeight()) * 0.5f;
                if (on) { softKeyLite(g, c, rr2, true, sc, 0.7f); softLed(g, c.getCentre(), 3.2f, sc, true); } else softWellLite(g, c, rr2); }
            else { g.setColour(on ? sc : Theme::scr()); g.fillRoundedRectangle(c, 4.0f);
                   if (on) { g.setColour(Theme::textOn(sc)); g.fillEllipse(c.withSizeKeepingCentre(7, 7)); } }
        }
    }
    lab(g, U8("Touch a square to send that sound to that output (drag along a row for several). ROOM speakers (green) play the main mix; HP = the headphones."),
        { 0, getHeight() - 30, getWidth(), 24 }, 8.0f, Theme::sub());
    drawSoftShades(g, *this);
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
