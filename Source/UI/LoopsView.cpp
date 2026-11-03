#include "ShellViews.h"

namespace arco {

static void say(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}
static const char* SRCNAME[LoopGrid::NSrc] = { "LIVE 1", "LIVE 2", "LIVE 3", "LIVE 4", "DRUMS", "SYNTH", "PHRASE" };
static juce::Colour rowColour(int r) { static const int O[8] = { 3, 1, 2, 4, 0, 5, 6, 7 }; return Theme::pal().acc[(size_t) O[r % 8]]; }

LoopGrid& LoopsView::lg() { return shell.host.engine.loopGrid; }

LoopsView::LoopsView(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    static const int BARS[5] = { 1, 2, 4, 8, 0 };
    auto* len = add<Segments>(juce::StringArray { "1 BAR", "2 BARS", "4 BARS", "8 BARS", "FREE" }, [this] { int b = lg().recBars.load(); for (int i = 0; i < 5; ++i) if (BARS[i] == b) return i; return 1; },
                              [this](int i) { lg().recBars.store(BARS[i]); }, [] { return Theme::red(); });
    len->setComponentID("lv:len");
    auto* q = add<Segments>(juce::StringArray { "ON THE BAR", "ON THE BEAT" }, [this] { return lg().quant.load(); }, [this](int i) { lg().quant.store(i); }, [] { return Theme::blue(); });
    q->setComponentID("lv:quant");
    auto* stop = add<Pill>("STOP ALL", [] { return Theme::btn(); }, 9.0f, 10.0f); stop->icon = 2; stop->textColour = [] { return Theme::txt(); };
    stop->onClick = [this] { stopAll(); }; stop->setComponentID("lv:stop");
    auto* br = add<Pill>("STATION LOOPS", [this] { return showBrowser ? Theme::sel() : Theme::btn(); }, 8.5f, 10.0f);
    br->textColour = [this] { return showBrowser ? Theme::textOn(Theme::sel()) : Theme::txt(); };
    br->onClick = [this] { showBrowser = !showBrowser; resized(); repaint(); }; br->setComponentID("lv:browser");
    auto* mv = add<KnobCell>("VOLUME", [this] { return lg().master.load() * 100.0; }, [this](double v) { lg().master.store((float) v / 100.0f); }, 0, 100, 80, [] { return Theme::txt(); }, nullptr);
    mv->setComponentID("lv:vol");
    startTimerHz(30);
}
LoopsView::~LoopsView() { pool.removeAllJobs(true, 4000); }

void LoopsView::resized() {
    int W = getWidth(), H = getHeight();
    bool tall = H > W;
    if (tall != wasTall) { wasTall = tall; showBrowser = !tall; }     // upright: the grid gets the width (STATION LOOPS opens on demand)
    int bw = showBrowser ? 230 : 0;
    browR = showBrowser ? juce::Rectangle<int>(W - 18 - bw, 16, bw, H - 16 - 76) : juce::Rectangle<int>();
    headR = { 18, 16, 214, H - 16 - 76 };
    gridR = { headR.getRight() + 10, 16, (showBrowser ? browR.getX() - 12 : W - 18) - (headR.getRight() + 10), H - 16 - 76 };
    auto byId = [&](const juce::String& id) -> juce::Component* { for (auto* c : owned) if (c->getComponentID() == id) return c; return nullptr; };
    int y = H - 62;
    if (auto* c = byId("lv:stop")) c->setBounds(18, y, 130, 46);
    if (tall) {
        if (auto* c = byId("lv:len")) c->setBounds(160, y + 6, 360, 36);
        if (auto* c = byId("lv:quant")) c->setBounds(530, y + 6, 210, 36);
        if (auto* c = byId("lv:browser")) c->setBounds(W - 18 - 140, y, 140, 46);
        if (auto* c = byId("lv:vol")) c->setVisible(false);
        return;
    }
    if (auto* c = byId("lv:vol")) c->setVisible(true);
    bool narrow = W < 1300;          // an iPhone lying down
    if (auto* c = byId("lv:len")) c->setBounds(narrow ? 168 : 240, y + 6, narrow ? 380 : 430, 36);
    if (auto* c = byId("lv:quant")) c->setBounds(narrow ? 562 : 690, y + 6, narrow ? 230 : 250, 36);
    if (auto* c = byId("lv:browser")) c->setBounds(W - 18 - 160, y, 160, 46);
    if (auto* c = byId("lv:vol")) c->setBounds(W - 18 - 160 - 90, y - 14, 80, 74);
}
juce::Rectangle<int> LoopsView::colHead(int col) const {
    float w = gridR.getWidth() / (float) LoopGrid::COLS;
    return juce::Rectangle<float>(gridR.getX() + col * w, (float) gridR.getY(), w, 40).reduced(4, 2).toNearestInt();
}
juce::Rectangle<int> LoopsView::rowRect(int row) const {
    float h = (gridR.getHeight() - 46) / (float) LoopGrid::ROWS;
    return juce::Rectangle<float>((float) headR.getX(), gridR.getY() + 46 + row * h, (float) headR.getWidth(), h).reduced(0, 3).toNearestInt();
}
juce::Rectangle<int> LoopsView::cellRect(int row, int col) const {
    float w = gridR.getWidth() / (float) LoopGrid::COLS, h = (gridR.getHeight() - 46) / (float) LoopGrid::ROWS;
    return juce::Rectangle<float>(gridR.getX() + col * w, gridR.getY() + 46 + row * h, w, h).reduced(4, 3).toNearestInt();
}
bool LoopsView::cellAt(juce::Point<int> p, int& row, int& col) const {
    for (int r = 0; r < LoopGrid::ROWS; ++r) for (int c = 0; c < LoopGrid::COLS; ++c) if (cellRect(r, c).contains(p)) { row = r; col = c; return true; }
    return false;
}
const std::vector<float>& LoopsView::peaksOf(const LoopClip* c) {
    auto it = peaks.find(c); if (it != peaks.end()) return it->second;
    std::vector<float> p(64, 0.0f);
    for (long i = 0; i < c->len; ++i) { auto& q = p[(size_t) (i * 64 / std::max(1L, c->len))]; q = juce::jmax(q, std::abs(c->l[(size_t) i]), std::abs(c->r[(size_t) i])); }
    float mx = 1e-6f; for (float v : p) mx = juce::jmax(mx, v); for (auto& v : p) v /= mx;
    if (peaks.size() > 200) peaks.clear();
    return peaks[c] = std::move(p);
}

void LoopsView::paint(juce::Graphics& g) {
    struct ShadeAtEnd { juce::Graphics& g; juce::Component& c; ~ShadeAtEnd() { drawSoftShades(g, c); } } shadeAtEnd { g, *this };
    Theme::backdrop(g, getLocalBounds());
    auto& L = lg();
    // column headers: start a whole column
    for (int c = 0; c < LoopGrid::COLS; ++c) {
        auto r = colHead(c).toFloat();
        bool any = false; for (int rw = 0; rw < LoopGrid::ROWS; ++rw) any |= L.cell[rw][c].load() != nullptr;
        const bool soft = Theme::softUI();
        if (soft) softKeyLite(g, r.reduced(2.0f), (r.getHeight() - 4) * 0.5f, false);
        else { g.setColour(Theme::btn()); g.fillRoundedRectangle(r, 10.0f); }
        juce::Path p; float cx = r.getX() + 20, cy = r.getCentreY(); p.addTriangle(cx - 4, cy - 6, cx - 4, cy + 6, cx + 6, cy);
        g.setColour(any ? (soft ? Theme::txt() : Theme::ink(Theme::green())) : Theme::sub()); g.fillPath(p);
        say(g, soft ? "Scene " + juce::String(c + 1) : "SCENE " + juce::String(c + 1), r.toNearestInt().withTrimmedLeft(34), soft ? 10.0f : 8.0f, any ? Theme::txt() : Theme::sub(), soft ? 1 : 2);
    }
    // rows
    for (int rw = 0; rw < LoopGrid::ROWS; ++rw) {
        auto r = rowRect(rw); auto col = rowColour(rw);
        const bool soft = Theme::softUI();
        drawPanel(g, r.toFloat(), 12.0f);
        bool mu = L.mute[rw].load() != 0, pl = L.playing[rw].load() >= 0;
        auto sr = juce::Rectangle<int>(r.getX() + 22, r.getY() + 28, 92, 22);
        auto mr = juce::Rectangle<int>(r.getRight() - 84, r.getY() + 8, 34, 22);
        auto st = juce::Rectangle<int>(r.getRight() - 44, r.getY() + 8, 34, 22);
        auto vr = juce::Rectangle<float>((float) r.getRight() - 84, (float) r.getY() + 38, 74.0f, 8.0f);
        float lvl = L.level[rw].load();
        if (soft) {                // 2.3: the row in silicone - its light, the source in a groove, M and stop as keys, the level as a slot
            softLed(g, { (float) r.getX() + 13.0f, (float) r.getY() + 15.0f }, 3.2f, col, pl);
            say(g, "Row " + juce::String(rw + 1), { r.getX() + 22, r.getY() + 6, 80, 18 }, 10.5f, Theme::txt(), 2);
            softWellLite(g, sr.toFloat(), sr.getHeight() * 0.5f);
            g.setColour(Theme::sub()); g.setFont(Theme::mono(7.5f));
            g.drawText((juce::String::fromUTF8("‹ ") + SRCNAME[juce::jlimit(0, LoopGrid::NSrc - 1, L.src[rw].load())] + juce::String::fromUTF8(" ›")).toUpperCase(), sr, juce::Justification::centred);
            softKeyLite(g, mr.toFloat(), mr.getHeight() * 0.5f, mu, Theme::red(), mu ? 0.5f : 0.0f);
            say(g, "M", mr, 9.0f, mu ? Theme::txt() : Theme::sub(), 2, juce::Justification::centred);
            softKeyLite(g, st.toFloat(), st.getHeight() * 0.5f, false);
            g.setColour(pl ? Theme::txt() : Theme::sub()); g.fillRoundedRectangle(st.toFloat().withSizeKeepingCentre(8, 8), 1.5f);
            softWellLite(g, vr, 4.0f);
            auto vf = vr.reduced(2.0f, 2.5f); vf = vf.withWidth(vf.getWidth() * L.vol[rw].load());
            g.setColour(col.withAlpha(0.25f)); g.fillRoundedRectangle(vf.expanded(0, 1.5f), 2.5f); g.setColour(col); g.fillRoundedRectangle(vf, 1.5f);
            g.setColour(col.withAlpha(0.7f)); g.fillRoundedRectangle(vr.getX() + 2, vr.getBottom() + 4, (vr.getWidth() - 4) * juce::jmin(1.0f, lvl * 1.4f), 2.0f, 1.0f);
        } else {
        g.setColour(col); g.fillRoundedRectangle((float) r.getX() + 8, (float) r.getY() + 10, 5.0f, r.getHeight() - 20.0f, 2.5f);
        say(g, "ROW " + juce::String(rw + 1), { r.getX() + 22, r.getY() + 6, 80, 18 }, 9.0f, Theme::txt(), 2);
        // the source (click: the next one)
        g.setColour(Theme::card2()); g.fillRoundedRectangle(sr.toFloat(), 6.0f);
        say(g, juce::String::fromUTF8("◂ ") + SRCNAME[juce::jlimit(0, LoopGrid::NSrc - 1, L.src[rw].load())] + juce::String::fromUTF8(" ▸"), sr, 7.6f, Theme::txt(), 2, juce::Justification::centred);
        // mute, the level, stop
        g.setColour(mu ? Theme::red() : Theme::btn()); g.fillRoundedRectangle(mr.toFloat(), 6.0f);
        say(g, "M", mr, 8.5f, mu ? Theme::textOn(Theme::red()) : Theme::sub(), 2, juce::Justification::centred);
        g.setColour(Theme::btn()); g.fillRoundedRectangle(st.toFloat(), 6.0f);
        g.setColour(pl ? Theme::txt() : Theme::sub()); g.fillRoundedRectangle(st.toFloat().withSizeKeepingCentre(9, 9), 1.5f);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(vr, 4.0f);
        g.setColour(col.withAlpha(0.8f)); g.fillRoundedRectangle(vr.withWidth(vr.getWidth() * L.vol[rw].load()), 4.0f);
        g.setColour(Theme::green().withAlpha(0.9f)); g.fillRect(vr.getX(), vr.getBottom() + 3, vr.getWidth() * juce::jmin(1.0f, lvl * 1.4f), 2.0f);
        }
        // the cells
        for (int c = 0; c < LoopGrid::COLS; ++c) {
            auto cr = cellRect(rw, c).toFloat();
            const LoopClip* clip = L.cell[rw][c].load();
            bool playing = L.playing[rw].load() == c, queued = L.queued[rw].load() == c, stopping = L.queued[rw].load() == -1 && playing;
            bool recHere = L.recRow.load() == rw && L.recCol.load() == c && (L.recState.load() == 1 || L.recState.load() == 2);
            bool selHere = rw == selRow && c == selCol;
            if (soft) {              // 2.3: a loop is a silicone key with its sound shining through; playing = pressed in and bright
                auto k = cr.reduced(3.0f); float kr = 14.0f;
                if (recHere) {
                    softKeyLite(g, k, kr, true, Theme::red(), L.recState.load() == 2 ? 0.75f : 0.35f);
                    if (L.recState.load() == 2) { g.setColour(Theme::red()); g.fillRoundedRectangle(k.reduced(10, 0).withWidth((k.getWidth() - 20) * juce::jlimit(0.0f, 1.0f, L.recProgressOut.load())).withTop(k.getBottom() - 8).withHeight(3), 1.5f); }
                    say(g, L.recState.load() == 2 ? "Rec" : juce::String::fromUTF8("Rec …"), k.toNearestInt(), 11.0f, Theme::txt(), 2, juce::Justification::centred);
                } else if (clip) {
                    softKeyLite(g, k, kr, playing, col, playing ? 0.55f : 0.0f);
                    auto& pk = peaksOf(clip);
                    auto wv = k.reduced(12, 18).withTrimmedTop(4);
                    g.setColour(col.withAlpha(playing ? 0.95f : 0.65f));
                    for (size_t q = 0; q < pk.size(); ++q) { float x = wv.getX() + q * wv.getWidth() / pk.size(), h = pk[q] * wv.getHeight() * 0.5f;
                        g.fillRoundedRectangle(x, wv.getCentreY() - h, juce::jmax(1.0f, wv.getWidth() / pk.size() - 1.2f), h * 2 + 1, 0.8f); }
                    if (playing) { float pr = L.progress[rw].load(); float px = k.getX() + 8 + (k.getWidth() - 16) * pr;
                        g.setColour(Theme::txt().withAlpha(0.25f)); g.fillRoundedRectangle(px - 2.5f, k.getY() + 6, 5.0f, k.getHeight() - 12, 2.5f);
                        g.setColour(Theme::txt()); g.fillRoundedRectangle(px - 1.0f, k.getY() + 6, 2.0f, k.getHeight() - 12, 1.0f); }
                    int nb = (int) std::round(clip->beats / juce::jmax(1.0, shell.host.model.get("beats")));
                    auto nm = names[rw][c]; if (nm.isEmpty()) nm = juce::String(nb) + (nb == 1 ? " bar" : " bars");
                    g.setColour(Theme::sub()); g.setFont(Theme::mono(7.0f)); g.drawText(nm.toUpperCase(), k.toNearestInt().reduced(12, 6).withHeight(14), juce::Justification::centredLeft);
                    if (queued || stopping) { float ph = (float) (0.5 + 0.5 * std::sin(juce::Time::getMillisecondCounterHiRes() * 0.012));
                        softLed(g, { k.getRight() - 13.0f, k.getY() + 13.0f }, 3.0f, col, ph > 0.5f); }
                } else {
                    softWellLite(g, k, kr);
                    if (cr.contains(hover.toFloat()) || (dragItem >= 0 && cr.contains(dragNow.toFloat())) || selHere) softLed(g, k.getCentre(), 4.0f, Theme::red(), true);
                }
                if (selHere) { g.setColour(Theme::txt().withAlpha(0.35f)); g.drawRoundedRectangle(k.expanded(2.5f), kr + 2.5f, 1.5f); }
                continue;
            }
            if (recHere) {
                g.setColour(Theme::red().withAlpha(L.recState.load() == 2 ? 0.55f : 0.25f)); g.fillRoundedRectangle(cr, 10.0f);
                if (L.recState.load() == 2) { g.setColour(Theme::red()); g.fillRoundedRectangle(cr.withWidth(cr.getWidth() * juce::jlimit(0.0f, 1.0f, L.recProgressOut.load())).withTop(cr.getBottom() - 5), 2.5f); }
                say(g, L.recState.load() == 2 ? "REC" : juce::String::fromUTF8("REC …"), cr.toNearestInt(), 10.0f, Theme::txt(), 2, juce::Justification::centred);
            } else if (clip) {
                g.setColour(col.withAlpha(playing ? 0.95f : 0.45f)); g.fillRoundedRectangle(cr, 10.0f);
                auto& pk = peaksOf(clip);
                auto wv = cr.reduced(8, 14);
                g.setColour(Theme::textOn(col).withAlpha(playing ? 0.75f : 0.5f));
                for (size_t k = 0; k < pk.size(); ++k) { float x = wv.getX() + k * wv.getWidth() / pk.size(), h = pk[k] * wv.getHeight() * 0.5f; g.fillRect(x, wv.getCentreY() - h, juce::jmax(1.0f, wv.getWidth() / pk.size() - 1), h * 2 + 1); }
                if (playing) { float pr = L.progress[rw].load(); g.setColour(Theme::textOn(col)); g.fillRect(cr.getX() + 4 + (cr.getWidth() - 8) * pr, cr.getY() + 4, 2.0f, cr.getHeight() - 8); }
                int nb = (int) std::round(clip->beats / juce::jmax(1.0, shell.host.model.get("beats")));
                auto nm = names[rw][c]; if (nm.isEmpty()) nm = juce::String(nb) + (nb == 1 ? " bar" : " bars");
                say(g, nm, cr.toNearestInt().reduced(8, 3).withHeight(14), 7.0f, Theme::textOn(col), 2);
                if (queued || stopping) { float ph = (float) (0.5 + 0.5 * std::sin(juce::Time::getMillisecondCounterHiRes() * 0.012));
                    g.setColour(Theme::txt().withAlpha(0.4f + 0.6f * ph)); g.drawRoundedRectangle(cr.reduced(1.5f), 10.0f, 3.0f); }
            } else {
                g.setColour(Theme::card()); g.fillRoundedRectangle(cr, 10.0f);
                g.setColour(Theme::line()); g.drawRoundedRectangle(cr.reduced(0.5f), 10.0f, 1.0f);
                if (cr.contains(hover.toFloat()) || (dragItem >= 0 && cr.contains(dragNow.toFloat())) || selHere) {
                    g.setColour(Theme::ink(Theme::red()).withAlpha(0.8f)); g.fillEllipse(cr.getCentreX() - 6, cr.getCentreY() - 6, 12, 12); }
            }
            if (selHere) { g.setColour(Theme::sel()); g.drawRoundedRectangle(cr.expanded(2.0f), 11.0f, 2.0f); }
        }
    }
    // the station's loops to drag in
    bitems.clear();
    if (showBrowser) {
        drawPanel(g, browR.toFloat(), 16.0f);
        say(g, "STATION LOOPS", { browR.getX() + 14, browR.getY() + 10, 200, 20 }, 9.5f, Theme::txt(), 2);
        say(g, "drag one onto a cell", { browR.getX() + 14, browR.getY() + 30, 200, 16 }, 7.5f, Theme::sub(), 1);
        auto& e = shell.host.engine; int y = browR.getY() + 56;
        static const char* SC[4] = { "A", "B", "C", "D" };
        for (int sc = 0; sc < 4; ++sc) {
            int mask = (int) e.snap("sv_mask" + std::to_string(sc)).v.load();
            if (mask == 0) continue;
            say(g, juce::String("SCENE ") + SC[sc], { browR.getX() + 14, y, 200, 18 }, 8.0f, Theme::sub(), 2); y += 22;
            for (int t = 0; t < 8; ++t) if ((mask >> t) & 1) {
                auto r = juce::Rectangle<int>(browR.getX() + 12, y, browR.getWidth() - 24, 30);
                if (Theme::softUI()) { softKeyLite(g, r.toFloat().reduced(1.0f), 14.0f, false); softLed(g, { (float) r.getX() + 14.0f, (float) r.getCentreY() }, 3.0f, Theme::strip(4 + t), true);
                                       say(g, "Track " + juce::String(t + 1), r.withTrimmedLeft(26), 9.5f, Theme::txt(), 1); }
                else { g.setColour(Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 8.0f);
                g.setColour(Theme::strip(4 + t)); g.fillRoundedRectangle((float) r.getX() + 6, (float) r.getY() + 7, 4.0f, 16.0f, 2.0f);
                say(g, "TRACK " + juce::String(t + 1), r.withTrimmedLeft(18), 8.5f, Theme::txt(), 2); }
                bitems.push_back({ sc, t, r }); y += 34;
                if (y > browR.getBottom() - 40) break;
            }
            y += 6;
        }
        if (bitems.empty()) { g.setColour(Theme::sub()); g.setFont(Theme::font(8.0f)); g.drawFittedText("loops recorded on the WORKSTATION show here", browR.reduced(14).withTrimmedTop(50).withHeight(60), juce::Justification::topLeft, 3); }
    }
    if (dragItem >= 0 && dragItem < (int) bitems.size()) {
        auto r = juce::Rectangle<float>((float) dragNow.x - 10, (float) dragNow.y - 14, 150, 28);
        g.setColour(Theme::sel().withMultipliedAlpha(0.92f)); g.fillRoundedRectangle(r, 8.0f);
        say(g, juce::String::charToString((juce::juce_wchar) ('A' + bitems[(size_t) dragItem].scene)) + " · TRACK " + juce::String(bitems[(size_t) dragItem].track + 1), r.toNearestInt().reduced(10, 0), 8.5f, Theme::textOn(Theme::sel()), 2);
    }
    // the bottom bar
    for (auto* c : owned) { if (c->getComponentID() == "lv:len") say(g, "RECORD", { c->getX(), c->getY() - 22, 200, 16 }, 8.0f, Theme::sub(), 2);
                            if (c->getComponentID() == "lv:quant") say(g, "START", { c->getX(), c->getY() - 22, 200, 16 }, 8.0f, Theme::sub(), 2); }
    if (getHeight() > getWidth()) return;
    juce::String hint = message.isNotEmpty() ? message : juce::String::fromUTF8("tap an empty cell to record into it  ·  tap a loop to start / stop it  ·  drop an audio file on a cell  ·  right-click: more");
    say(g, hint, { 960, getHeight() - 58, juce::jmax(100, getWidth() - 960 - 280), 40 }, 7.6f, message.isNotEmpty() ? Theme::ink(Theme::green()) : Theme::sub(), 1);
}

void LoopsView::mouseDown(const juce::MouseEvent& e) {
    auto p = e.getPosition(); auto& L = lg();
    dragItem = -1; dragFromRow = -1;
    for (size_t k = 0; k < bitems.size(); ++k) if (bitems[k].r.contains(p)) { dragItem = (int) k; dragNow = p; return; }
    for (int c = 0; c < LoopGrid::COLS; ++c) if (colHead(c).contains(p)) { L.launchColumn(c); return; }
    for (int rw = 0; rw < LoopGrid::ROWS; ++rw) {
        auto r = rowRect(rw); if (!r.contains(p)) continue;
        if (juce::Rectangle<int>(r.getX() + 22, r.getY() + 28, 92, 22).contains(p)) { int s = L.src[rw].load(); L.src[rw].store((s + (p.x < r.getX() + 68 ? LoopGrid::NSrc - 1 : 1)) % LoopGrid::NSrc); repaint(); return; }
        if (juce::Rectangle<int>(r.getRight() - 84, r.getY() + 8, 34, 22).contains(p)) { L.mute[rw].store(L.mute[rw].load() ? 0 : 1); return; }
        if (juce::Rectangle<int>(r.getRight() - 44, r.getY() + 8, 34, 22).contains(p)) { L.stopRow(rw); return; }
        if (p.y > r.getY() + 32 && p.x > r.getRight() - 90) { dragFromRow = 100 + rw; return; }      // the volume bar
        return;
    }
    int rw, c;
    if (!cellAt(p, rw, c)) return;
    selRow = rw; selCol = c;
    const LoopClip* clip = L.cell[rw][c].load();
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m;
        m.addItem(1, "Record into it", L.recState.load() == 0);
        m.addItem(2, "Clear", clip != nullptr);
        m.addItem(3, "Copy to the next empty cell", clip != nullptr);
        m.addItem(4, "Put in STUDIO (a new lane)", clip != nullptr);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(e.getScreenX(), e.getScreenY(), 1, 1)), [this, rw, c](int r) {
            auto& L2 = lg();
            if (r == 1) { if (L2.playing[rw].load() == c) L2.stopRow(rw); L2.record(rw, c); }
            if (r == 2) { if (L2.playing[rw].load() == c) { L2.stopRow(rw); } setClip(rw, c, nullptr); }
            if (r == 3) for (int k = c + 1; k < LoopGrid::COLS; ++k) if (!L2.cell[rw][k].load()) { setClip(rw, k, clips[rw][c]); names[rw][k] = names[rw][c]; break; }
            if (r == 4 && clips[rw][c]) {
                auto& st = shell.host.studio; auto cl = clips[rw][c];
                const juce::ScopedLock sl(st.lock); st.snapshot();
                int lane = st.addLane(LaneAudio, "LOOP " + juce::String(rw + 1) + "." + juce::String(c + 1));
                st.song.sources.push_back(st.makeSource("live loop " + juce::String(rw + 1) + "." + juce::String(c + 1), std::vector<float>(cl->l.begin(), cl->l.begin() + cl->len), std::vector<float>(cl->r.begin(), cl->r.begin() + cl->len)));
                StudioClip sc; sc.lane = lane; sc.source = (int) st.song.sources.size() - 1; sc.start = 0; sc.length = cl->len * 4; sc.loop = true; sc.name = "loop " + juce::String(rw + 1) + "." + juce::String(c + 1);
                st.song.clips.push_back(sc); message = juce::String::fromUTF8("✓ in STUDIO, on a new lane (four times)");
            }
            repaint(); });
        return;
    }
    if (clip == nullptr) {
        if (L.recState.load() == 2 && L.recRow.load() == rw && L.recCol.load() == c) { L.recStopAsked = true; return; }
        if (L.recState.load() != 0) { if (L.recRow.load() == rw && L.recCol.load() == c && L.recState.load() == 1) L.recState.store(0); return; }
        L.record(rw, c);
        message = {};
        return;
    }
    if (L.recState.load() == 2 && L.recRow.load() == rw && L.recCol.load() == c) { L.recStopAsked = true; return; }
    dragFromRow = rw; dragFromCol = c; dragNow = p;
}
void LoopsView::mouseDrag(const juce::MouseEvent& e) {
    setViewportIgnoreDragFlag(dragItem >= 0 || dragFromRow >= 0);   // dragging a loop or a volume: no scrolling (iPhone)
    dragNow = e.getPosition();
    if (dragFromRow >= 100) { int rw = dragFromRow - 100; auto r = rowRect(rw); lg().vol[rw].store(juce::jlimit(0.0f, 1.0f, (e.position.x - (r.getRight() - 84)) / 74.0f)); repaint(); return; }
    repaint();
}
void LoopsView::mouseUp(const juce::MouseEvent& e) {
    auto p = e.getPosition(); auto& L = lg();
    if (dragItem >= 0 && dragItem < (int) bitems.size()) {
        int rw, c; if (cellAt(p, rw, c)) takeStationLoop(bitems[(size_t) dragItem].scene, bitems[(size_t) dragItem].track, rw, c);
        dragItem = -1; repaint(); return;
    }
    if (dragFromRow >= 0 && dragFromRow < 100) {
        int rw, c;
        if (cellAt(p, rw, c) && (rw != dragFromRow || c != dragFromCol) && e.getDistanceFromDragStart() > 8) {      // dragged onto another cell: a copy
            setClip(rw, c, clips[dragFromRow][dragFromCol]); names[rw][c] = names[dragFromRow][dragFromCol];
        } else if (e.getDistanceFromDragStart() <= 8) {
            int r0 = dragFromRow, c0 = dragFromCol;
            if (L.playing[r0].load() == c0 && L.queued[r0].load() != -1) L.stopRow(r0); else L.launch(r0, c0);
        }
    }
    dragFromRow = -1; repaint();
}

void LoopsView::setClip(int row, int col, std::shared_ptr<LoopClip> c) {
    auto old = clips[row][col];
    clips[row][col] = c;
    lg().cell[row][col].store(c.get());
    if (!c) names[row][col] = {};
    if (old) trash.push_back({ juce::Time::getMillisecondCounterHiRes() + 3000.0, old });      // the audio thread may still be reading it
}

void LoopsView::timerCallback() {
    auto& L = lg();
    // a take finished: it becomes the cell's own (the take buffer is free for the next one)
    if (L.recState.load() == 3) {
        int rw = L.recRow.load(), c = L.recCol.load();
        auto cl = std::make_shared<LoopClip>();
        cl->len = L.recClip.len; cl->beats = L.recClip.beats;
        cl->l.assign(L.recClip.l.begin(), L.recClip.l.begin() + cl->len); cl->r.assign(L.recClip.r.begin(), L.recClip.r.begin() + cl->len);
        auto old = clips[rw][c]; clips[rw][c] = cl; L.cell[rw][c].store(cl.get());
        if (old) trash.push_back({ juce::Time::getMillisecondCounterHiRes() + 3000.0, old });
        names[rw][c] = SRCNAME[juce::jlimit(0, LoopGrid::NSrc - 1, L.src[rw].load())];
        L.recRow.store(-1); L.recState.store(0);
        message = juce::String::fromUTF8("✓ recorded: ") + juce::String((int) std::round(cl->beats / juce::jmax(1.0, shell.host.model.get("beats")))) + " bars";
    }
    double now = juce::Time::getMillisecondCounterHiRes();
    trash.erase(std::remove_if(trash.begin(), trash.end(), [now](auto& t) { return t.first < now; }), trash.end());
    if (isVisible()) { repaint(); for (auto* c : owned) c->repaint(); }
}

bool LoopsView::takeStationLoop(int scene, int track, int row, int col) {
    auto& e = shell.host.engine;
    static const char* SC[4] = { "A", "B", "C", "D" };
    long len = (long) e.snap("sv_len" + std::to_string(scene)).v.load();
    double bl = e.snap("sv_bl").v.load();
    if (len < 64 || bl < 16) { message = "that loop is empty"; return false; }
    auto& buf = e.buffer(std::string("st_loops") + SC[scene]);
    if (track >= buf.ch || len > buf.len) return false;
    auto cl = std::make_shared<LoopClip>();
    const float* src = buf.chan(track);
    cl->l.assign(src, src + len); cl->r = cl->l; cl->len = len;
    cl->beats = juce::jmax(1.0, std::round(len / bl));
    setClip(row, col, cl);
    names[row][col] = juce::String(SC[scene]) + " · TRACK " + juce::String(track + 1);
    message = juce::String::fromUTF8("✓ ") + names[row][col] + " in row " + juce::String(row + 1);
    return true;
}

bool LoopsView::importInto(const juce::File& f, int row, int col) {
    message = juce::String::fromUTF8("reading ") + f.getFileName() + juce::String::fromUTF8(" …");
    auto piece = shell.host.pieceInfo(false);
    juce::Component::SafePointer<LoopsView> sp(this);
    pool.addJob([sp, f, row, col, piece, sr = shell.host.engine.sr, &fm = shell.host.formatManager()]() mutable {
        ImportPlan plan; juce::String err;
        bool ok = prepareImport(f, sr, plan, fm, err);
        juce::AudioBuffer<float> out;
        if (ok) { planImport(plan, piece); out = renderImport(plan, piece); }
        int bars = plan.outBars;
        juce::MessageManager::callAsync([sp, ok, err, out, bars, row, col, f]() {
            if (!sp) return;
            if (!ok || out.getNumSamples() < 64) { sp->message = "could not use " + f.getFileName() + (err.isNotEmpty() ? ": " + err : juce::String()); return; }
            auto cl = std::make_shared<LoopClip>();
            cl->len = out.getNumSamples();
            cl->l.assign(out.getReadPointer(0), out.getReadPointer(0) + cl->len);
            cl->r.assign(out.getReadPointer(out.getNumChannels() > 1 ? 1 : 0), out.getReadPointer(out.getNumChannels() > 1 ? 1 : 0) + cl->len);
            double bpb = juce::jmax(1.0, sp->shell.host.model.get("beats"));
            cl->beats = (bars > 0 ? bars : juce::jmax(1.0, std::round(cl->len / (sp->shell.host.engine.sr * 60.0 / juce::jmax(20.0, sp->shell.tempo()) * bpb)))) * bpb;
            sp->setClip(row, col, cl); sp->names[row][col] = f.getFileNameWithoutExtension().toUpperCase().substring(0, 18);
            sp->message = juce::String::fromUTF8("✓ ") + f.getFileName() + ": " + juce::String((int) std::round(cl->beats / bpb)) + " bars, fitted to the tempo";
        });
    });
    return true;
}
void LoopsView::filesDropped(const juce::StringArray& files, int x, int y) {
    hover = { -1, -1 };
    int rw, c; if (!cellAt({ x, y }, rw, c)) { rw = selRow; c = selCol; }
    for (auto& f : files) { importInto(juce::File(f), rw, c); if (++c >= LoopGrid::COLS) break; }
}

void LoopsView::togglePlay() {
    auto& L = lg();
    if (anyPlaying()) { stopAll(); return; }
    // nothing plays: the selected cell's column (or the first column with loops)
    int col = selCol; bool any = false; for (int r = 0; r < LoopGrid::ROWS; ++r) any |= L.cell[r][col].load() != nullptr;
    if (!any) for (int c = 0; c < LoopGrid::COLS && !any; ++c) for (int r = 0; r < LoopGrid::ROWS; ++r) if (L.cell[r][c].load()) { col = c; any = true; break; }
    if (any) L.launchColumn(col);
}
void LoopsView::stopAll() { lg().stopAll(); if (lg().recState.load() == 1) lg().recState.store(0); }
void LoopsView::recordSelected() {
    auto& L = lg();
    if (L.recState.load() == 2) { L.recStopAsked = true; return; }
    if (L.recState.load() == 1) { L.recState.store(0); return; }
    int c = selCol; if (L.cell[selRow][c].load()) { for (int k = 0; k < LoopGrid::COLS; ++k) if (!L.cell[selRow][k].load()) { c = k; break; } }
    selCol = c; L.record(selRow, c);
}
bool LoopsView::anyPlaying() { for (int r = 0; r < LoopGrid::ROWS; ++r) if (lg().playing[r].load() >= 0) return true; return false; }
bool LoopsView::anyRecording() { int s = lg().recState.load(); return s == 1 || s == 2; }

// saved inside the song folder: loops/<row>-<col>.wav and loops/grid.json
bool LoopsView::save(const juce::File& folder) {
    auto dir = folder.getChildFile("live loops");
    dir.deleteRecursively();
    bool any = false; for (auto& r : clips) for (auto& c : r) any |= c != nullptr;
    if (!any) return true;
    dir.createDirectory();
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> cells, rows;
    juce::WavAudioFormat wav;
    for (int r = 0; r < LoopGrid::ROWS; ++r) {
        auto* ro = new juce::DynamicObject(); ro->setProperty("src", lg().src[r].load()); ro->setProperty("vol", lg().vol[r].load()); ro->setProperty("mute", lg().mute[r].load()); rows.add(juce::var(ro));
        for (int c = 0; c < LoopGrid::COLS; ++c) if (auto cl = clips[r][c]) {
            auto f = dir.getChildFile(juce::String(r + 1) + "-" + juce::String(c + 1) + ".wav");
            if (auto os = f.createOutputStream()) {
                std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.release(), shell.host.engine.sr, 2, 24, {}, 0));
                if (w) { const float* ch[2] = { cl->l.data(), cl->r.data() }; w->writeFromFloatArrays(ch, 2, (int) cl->len); }
            }
            auto* o = new juce::DynamicObject(); o->setProperty("row", r); o->setProperty("col", c); o->setProperty("beats", cl->beats); o->setProperty("name", names[r][c]); o->setProperty("file", f.getFileName());
            cells.add(juce::var(o));
        }
    }
    root->setProperty("cells", cells); root->setProperty("rows", rows); root->setProperty("recBars", lg().recBars.load()); root->setProperty("quant", lg().quant.load());
    return dir.getChildFile("grid.json").replaceWithText(juce::JSON::toString(juce::var(root)));
}
bool LoopsView::load(const juce::File& folder) {
    stopAll();
    for (int r = 0; r < LoopGrid::ROWS; ++r) for (int c = 0; c < LoopGrid::COLS; ++c) { lg().playing[r].store(-1); setClip(r, c, nullptr); }
    auto dir = folder.getChildFile("live loops");
    auto v = juce::JSON::parse(dir.getChildFile("grid.json"));
    if (!v.isObject()) return false;
    if (auto* rows = v["rows"].getArray()) for (int r = 0; r < juce::jmin(LoopGrid::ROWS, rows->size()); ++r) {
        lg().src[r].store((int) (*rows)[r]["src"]); lg().vol[r].store((float) (double) (*rows)[r]["vol"]); lg().mute[r].store((int) (*rows)[r]["mute"]); }
    lg().recBars.store((int) v.getProperty("recBars", 2)); lg().quant.store((int) v.getProperty("quant", 0));
    if (auto* cells = v["cells"].getArray()) for (auto& o : *cells) {
        int r = (int) o["row"], c = (int) o["col"]; if (r < 0 || r >= LoopGrid::ROWS || c < 0 || c >= LoopGrid::COLS) continue;
        std::unique_ptr<juce::AudioFormatReader> rd(shell.host.formatManager().createReaderFor(dir.getChildFile(o["file"].toString())));
        if (!rd) continue;
        juce::AudioBuffer<float> b(2, (int) rd->lengthInSamples); rd->read(&b, 0, (int) rd->lengthInSamples, 0, true, true);
        auto cl = std::make_shared<LoopClip>(); cl->len = b.getNumSamples();
        cl->l.assign(b.getReadPointer(0), b.getReadPointer(0) + cl->len); cl->r.assign(b.getReadPointer(1), b.getReadPointer(1) + cl->len);
        cl->beats = juce::jmax(1.0, (double) o["beats"]);
        setClip(r, c, cl); names[r][c] = o["name"].toString();
    }
    return true;
}

} // namespace arco
