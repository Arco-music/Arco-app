#include "MainView.h"
#include "Panels.h"
#include "SynthPanel.h"
#include "Sheets.h"
#include "LearnOverlay.h"
#include "SpatialPanel.h"
#include "InstrumentPanels.h"
#include "StudioView.h"
#include "../App/FxPresets.h"

namespace arco {

static const char* STATE_NAMES[11] = { "EMPTY", "WAITING", "RECORDING", "PLAYING", "ARMED", "OVERDUB", "COUNT-IN", "RECORDING", "STOPPED", "STARTING", "STOPPING" };
static juce::Colour stateColour(int s) {
    switch (s) {
        case 1: case 4: case 6: case 9: return Theme::amber();
        case 2: case 7: return Theme::red();
        case 3: return Theme::green();
        case 5: return Theme::c(Theme::rgb(0.80f, 0.38f, 0.80f));
        case 8: return Theme::c(Theme::rgb(0.24f, 0.34f, 0.50f));
        case 10: return Theme::c(Theme::rgb(0.55f, 0.62f, 0.30f));
        default: return Theme::btn();
    }
}
static const std::vector<std::string> DOTS = { "eq", "drv", "trm", "phs", "swl", "cho", "dly", "rev", "fxb", "shd", "halo", "res", "grn", "frz", "spc", "orb" };
static void label(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                  juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}

// ===================================================================== one strip
// 1.6: the new design - the state inside the screen, big REC / PLAY, segmented rows, a long fader with a dB scale.
// Every control as before (the same IDs: MIDI learn, the phone remote).
static Pill* segPill(juce::OwnedArray<juce::Component>& kids, juce::Component* parent, const juce::String& t, ColourFn col, int seg, float fs = 9.0f) {
    auto* p = new Pill(t, std::move(col), fs, 9.0f); p->seg = seg; p->tint = true; kids.add(p); parent->addAndMakeVisible(p); return p;
}
class StripView : public juce::Component {
public:
    StripView(Canvas& cv, int s_) : c(cv), s(s_) {
        auto& m = c.model();
        auto add = [this](juce::Component* x) { kids.add(x); addAndMakeVisible(x); return x; };
        head = new Pill("", [] { return juce::Colours::transparentBlack; }, 9.0f, 8.0f);
        head->onClick = [this] { c.model().select(s); };
        add(head); head->setComponentID("strip" + juce::String(s) + ":select");
        wave = new WaveScreen(c.engine(), [this]() -> SharedBuffer* {
                if (s < 4) return &c.engine().buffer("st_wavel");
                int sc = (int) c.snap("lscene"); const char* L[4] = { "A", "B", "C", "D" };
                return &c.engine().buffer(std::string("st_wave") + L[juce::jlimit(0, 3, sc)]); },
            [this] { return s < 4 ? s : s - 4; }, [this] { return s >= 4 && trackState() == 2 ? Theme::red() : Theme::strip(s); });
        wave->overlay = [this](juce::Graphics& g, juce::Rectangle<float> r) { paintScreen(g, r); };
        add(wave);
        if (s >= 4) {
            int k = s - 4;
            wave->onDrop = [this, k](const juce::File& f) { c.requestImport(f, k + 1); };
            auto press = [this, k](Pill* p, std::string cmd) {
                p->onClick = [this, cmd, k] { c.model().press("st_looper", cmd + std::to_string(k + 1)); };
                p->setComponentID("trk" + juce::String(k + 1) + ":" + juce::String(cmd)); };
            rec = new Pill("REC", [] { return Theme::red(); }, 10.5f, 12.0f); rec->tint = true; rec->icon = 3;
            rec->solidFn = [this] { int st = trackState(); return st == 2 || st == 7; };
            press(rec, "rec"); add(rec);
            tog = new Pill("", [] { return Theme::green(); }, 10.5f, 12.0f); tog->tint = true; tog->icon = 9;
            tog->onFn = [this] { return trackState() == 3; };
            press(tog, "tog"); add(tog);
            clr = segPill(kids, this, "CLR", [] { return Theme::btn(); }, 1); clr->icon = 7; press(clr, "clr");
            spd = segPill(kids, this, "1x", [] { return Theme::btn(); }, 2); press(spd, "spd");
            spd->dynText = [this, k] { const char* S[4] = { "1x", "½x", "2x", "◀" }; int v = (int) c.snap("spdk"); return U(S[(v >> (2 * k)) & 3]); };
            file = segPill(kids, this, "FILE", [] { return Theme::btn(); }, 3); file->icon = 6;
            file->onClick = [this, k] { c.importInto(k + 1); }; file->setComponentID("trk" + juce::String(k + 1) + ":file");
            auto* ib = new ValueBox([&m, k] { return m.get("src" + std::to_string(k + 1)); }, [&m, k](double v) { m.set("src" + std::to_string(k + 1), v); },
                                    1, 4, 0, [] { return Theme::blue(); });
            ib->fsize = 11.0f; ib->flat = true; add(ib); ib->setComponentID("p:src" + juce::String(k + 1)); inb = ib;
        } else {
            input = segPill(kids, this, "", [] { return Theme::btn(); }, 4, 8.5f);
            input->dynText = [this] { bool syn = c.host.synthInto > 0 && c.host.synthInto == s; auto ph = c.host.phoneOnLive(s + 1);
                return (syn ? juce::String("SYNTH ") + juce::String(s + 1) : ph.isNotEmpty() ? U("PHONE: ") + ph.toUpperCase()
                            : (nr ? "INPUT " : "INTERFACE INPUT ") + juce::String(c.host.inputCh[s])) + U("  ▾"); };
            input->textColour = [this] { return c.host.synthInto > 0 && c.host.synthInto == s ? Theme::violet() : Theme::txt(); };
            input->onClick = [this] { c.openSetup(true); };
            fxb = segPill(kids, this, U("FX ▸"), [] { return Theme::btn(); }, 1);
            fxb->onClick = [this] { c.model().select(s); };
            tune = segPill(kids, this, "TUNE", [] { return Theme::fx("tun"); }, 3);
            tune->onFn = [this] { return c.model().get("tn_amt") > 0; };
            tune->onClick = [this] { auto& ct = cardTable(); for (int i = 0; i < (int) ct.size(); ++i) if (juce::String(ct[(size_t) i].key) == "tun") {
                if (!c.host.visibleCards.contains("tun")) c.host.visibleCards.add("tun"); c.showCard(i); } };
        }
        auto* pn = new Knob([&m, this] { return m.get("pan" + std::to_string(s)); }, [&m, this](double v) { m.set("pan" + std::to_string(s), v); },
                            -50, 50, 0, [] { return Theme::sub(); });
        add(pn); pn->setComponentID("p:pan" + juce::String(s)); pan = pn;
        auto tg = [&](const juce::String& t, const char* key, ColourFn on, int seg) {
            std::string id = key + std::to_string(s);
            auto* p = segPill(kids, this, t, [this, id, on] { return c.model().get(id) > 0.5 ? on() : Theme::btn(); }, seg, 9.5f);
            p->onClick = [this, id] { c.model().set(id, c.model().get(id) > 0.5 ? 0 : 1); };
            p->setComponentID("p:" + juce::String(id)); return p; };
        mute = tg("M", "mute", [] { return Theme::red(); }, 1);
        solo = tg("S", "solo", [] { return Theme::blue(); }, 2);
        cue = tg("", "cue", [] { return Theme::amber(); }, 2); cue->icon = 5;
        room = tg("ROOM", "room", [] { return Theme::green(); }, 3);
        auto* fd = new Fader([&m, this] { return m.get("vol" + std::to_string(s)); }, [&m, this](double v) { m.set("vol" + std::to_string(s), v); }, 0, 100, 71);
        fd->scale = true; fd->col = [this] { return Theme::strip(s); };
        add(fd); fd->setComponentID("p:vol" + juce::String(s)); fader = fd;
        meter = new Meter([this] { return c.engine().peak("ch" + std::to_string(s)); }, true);
        add(meter); meter2 = meter;
        if (s < 4) { inMeter = new Meter([this] { return c.engine().peak("live_in" + std::to_string(s + 1)); }); add(inMeter); }
    }
    ~StripView() override { kids.clear(true); }
    int trackState() const { if (s < 4) return 0; int k = s - 4; float pack = c.snap(k < 4 ? "statA" : "statB"); return ((int) pack >> (4 * (k % 4))) & 15; }
    void resized() override {
        const int w = getWidth(), h = getHeight();
        nr = w < 150;
        const float k = juce::jlimit(0.72f, 1.0f, (h - 300) / 200.0f);           // a short strip: the rows get a little lower, the fader keeps room
        auto H = [&](int v) { return juce::roundToInt(v * k); };
        const int P = nr ? 7 : 12, iw = w - 2 * P, x = P, g = nr ? 6 : 8;
        int y = P;
        head->setBounds(0, 0, w, P + 20);
        headR = { x, y, iw, 18 }; y += 18 + g;
        int dh = nr ? H(48) : H(64); wave->setBounds(x, y, iw, dh); y += dh + g;
        if (s >= 4) {
            int bh = nr ? H(38) : H(48), gg = nr ? 5 : 8, hw = (iw - gg) / 2;
            rec->setBounds(x, y, hw, bh); tog->setBounds(x + iw - hw, y, hw, bh); y += bh + g;
            rec->text = nr ? "" : "REC";
            int sh = nr ? H(32) : H(36);
            int a = iw / 3; clr->setBounds(x, y, a, sh); spd->setBounds(x + a, y, iw - 2 * a, sh); file->setBounds(x + iw - a, y, a, sh); y += sh + g;
            clr->text = nr ? "" : "CLR"; file->text = nr ? "" : "FILE";
            int ih = nr ? H(30) : H(32);
            inR = { x, y, iw, ih };
            inb->setBounds(x + (nr ? 22 : 28), y + 2, nr ? iw - 26 : 44, ih - 4);
            dotsBelow = !nr && iw < 210;
            dotsR = nr ? juce::Rectangle<int> { x + (iw - 47) / 2, y + ih + 5, 47, 14 }
                       : dotsBelow ? juce::Rectangle<int> { x + (iw - 96) / 2, y + ih + 4, 96, 14 } : juce::Rectangle<int> { x + iw - 102, y + (ih - 14) / 2, 96, 14 };
            y += ih + g + (nr ? 18 : dotsBelow ? 14 : 0);
        } else {
            int ih = nr ? H(32) : H(38);
            input->setBounds(x, y, iw, ih); y += ih + g;
            inMeter->setBounds(x, y + 2, nr ? iw : iw - 104, 6);
            dotsR = nr ? juce::Rectangle<int> { x + (iw - 47) / 2, y + 12, 47, 14 } : juce::Rectangle<int> { x + iw - 96, y - 2, 96, 14 };
            y += nr ? 30 : 14;
            int fh = nr ? H(32) : H(38);
            int hw = iw / 2; fxb->setBounds(x, y, hw, fh); tune->setBounds(x + hw, y, iw - hw, fh); y += fh + g;
        }
        // bottom: M S cue ROOM (two rows on a narrow strip)
        int bh = nr ? H(32) : H(42), by;
        if (nr) { by = h - P - bh - 6 - bh; int a = iw / 3; mute->setBounds(x, by, a, bh); solo->setBounds(x + a, by, iw - 2 * a, bh); cue->setBounds(x + iw - a, by, a, bh);
                  cue->seg = 3; room->seg = 4; room->setBounds(x, by + bh + 6, iw, bh); }
        else { by = h - P - bh; int a = (iw * 10) / 46;
               mute->setBounds(x, by, a, bh); solo->setBounds(x + a, by, a, bh); cue->setBounds(x + 2 * a, by, a, bh); room->setBounds(x + 3 * a, by, iw - 3 * a, bh);
               cue->seg = 2; room->seg = 3; }
        // pan | the dB value over the long fader | the meter
        int fz = y + 2, fb = by - g;
        int pw = nr ? 34 : 56, ks = nr ? 34 : 54, mw = nr ? 4 : 6;
        pan->setBounds(x + (pw - ks) / 2, fz + 4, ks, ks);
        panLabelR = { x - 4, fz + 8 + ks, pw + 8, 14 };
        int fx0 = x + pw + (nr ? 2 : 8), fx1 = x + iw - mw - 2 * (nr ? 2 : 6);
        int fwid = juce::jmin(fx1 - fx0, nr ? 52 : 110), fcx = (fx0 + fx1) / 2;
        int dbw = juce::jmin(fwid, nr ? 56 : 84), dbh = nr ? 24 : 30;
        dbR = { fcx - dbw / 2, fz, dbw, dbh };
        fader->setBounds(fcx - fwid / 2, fz + dbh + 4, fwid, juce::jmax(40, fb - fz - dbh - 4));
        meter->setBounds(x + iw - mw, fz + dbh + 10, mw, juce::jmax(30, fb - fz - dbh - 16));
    }
    juce::String name() const { return s < 4 ? "LIVE " + juce::String(s + 1) : "TRACK " + juce::String(s - 3); }
    void update() {
        if (meter) meter->update(); if (inMeter) inMeter->update();
        repaint();
        for (auto* kd : kids) kd->repaint();
    }
    void paintScreen(juce::Graphics& g, juce::Rectangle<float> r) {
        float fs = nr ? 7.0f : 8.0f;
        auto top = r.toNearestInt().reduced(8, 0).withHeight(17).translated(0, 3);
        if (s >= 4) {
            int st = trackState();
            juce::String t = st == 2 || st == 7 ? juce::String(U("● RECORDING")) : juce::String(STATE_NAMES[juce::jlimit(0, 10, st)]);
            label(g, t, top, fs, st == 0 ? Theme::sub() : stateColour(st), 2);
        } else {
            bool syn = c.host.synthInto > 0 && c.host.synthInto == s;
            label(g, syn ? "SYNTH " + juce::String(s + 1) : "INPUT " + juce::String(c.host.inputCh[s]), top, fs, Theme::sub(), 2);
            if (s == 0) {                                     // the note and the tuner
                float p = c.snap("pitch");
                static const char* N[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
                if (p > 20) { int n = (int) std::floor(p + 0.5); label(g, juce::String(N[n % 12]) + juce::String(n / 12 - 1), top, fs + 1, Theme::fx("halo"), 2, juce::Justification::centredRight); }
                float tp = c.snap("tn_pitch");
                if (tp > 0) {
                    float cents = (tp - std::floor(tp + 0.5f)) * 100.0f;
                    float cx = r.getCentreX() + juce::jlimit(-1.0f, 1.0f, cents / 50.0f) * (r.getWidth() * 0.5f - 10);
                    g.setColour(Theme::green()); g.fillRoundedRectangle(cx - 2, r.getBottom() - 6, 4, 4, 2);
                    if (!nr) label(g, juce::String(cents >= 0 ? "+" : "") + juce::String((int) std::round(cents)) + U("¢"), top.withTrimmedRight(34), fs, Theme::green(), 2, juce::Justification::centredRight);
                }
            }
        }
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        drawPanel(g, r, 16.0f);
        bool empty = s >= 4 && trackState() == 0;
        { juce::Graphics::ScopedSaveState ss(g); juce::Path clip; clip.addRoundedRectangle(r, 16.0f); g.reduceClipRegion(clip);
          auto col = Theme::strip(s).withMultipliedAlpha(empty ? 0.45f : 1.0f);
          g.setGradientFill(juce::ColourGradient(col, 0, 0, col.withAlpha(col.getFloatAlpha() * 0.33f), r.getWidth(), 0, false)); g.fillRect(0.0f, 0.0f, r.getWidth(), 3.0f); }
        auto& m = c.model();
        // the name (tap: this strip is the one the effects' SEND knobs edit)
        auto col = Theme::strip(s);
        g.setColour(col.withMultipliedAlpha(empty ? 0.5f : 1.0f)); g.fillEllipse((float) headR.getX() + 1, (float) headR.getCentreY() - 4.5f, 9, 9);
        label(g, name(), headR.withTrimmedLeft(16), nr ? 10.5f : 12.5f, Theme::txt(), 2);
        if (s < 4 && !nr) label(g, "IN " + juce::String(c.host.inputCh[s]), headR, 8.0f, Theme::sub(), 1, juce::Justification::centredRight);
        if (s >= 4) {
            g.setColour(Theme::btn()); g.fillRoundedRectangle(inR.toFloat(), juce::jmin(10.0f, inR.getHeight() * 0.32f));
            label(g, nr ? "IN" : U("IN ▸"), inR.withTrimmedLeft(nr ? 5 : 8).withWidth(26), nr ? 7.5f : 8.5f, Theme::sub(), 2);
            if (!nr && !dotsBelow) { g.setColour(Theme::line()); g.fillRect((float) dotsR.getX() - 8, (float) inR.getY(), 1.0f, (float) inR.getHeight()); }
        }
        // the effects this strip uses
        bool row = dotsR.getWidth() > 60;
        for (int d = 0; d < (int) DOTS.size(); ++d) {
            auto id = Model::stripParam(s, DOTS[(size_t) d]);
            bool on = !id.empty() && m.get(id) > 0.5;
            g.setColour(on ? Theme::fx(DOTS[(size_t) d]) : Theme::line());
            float dx = row ? (float) dotsR.getX() + d * 6.0f : (float) dotsR.getX() + (d % 8) * 6.0f, dy = row ? (float) dotsR.getCentreY() - 2.5f : (float) dotsR.getY() + (d / 8) * 7.0f;
            g.fillEllipse(dx, dy, 5, 5);
        }
        label(g, "PAN", panLabelR, 8.0f, Theme::sub(), 2, juce::Justification::centred);
        double db = Fader::volToDb(m.get("vol" + std::to_string(s))); if (std::abs(db) < 0.15) db = 0;
        g.setColour(Theme::scr()); g.fillRoundedRectangle(dbR.toFloat(), 8.0f);
        juce::String dt = db < -90 ? juce::String(U("-∞")) : (db > 0.05 ? "+" : (db < -0.05 ? U("−") : juce::String())) + juce::String(std::abs(db), 1);
        g.setColour(Theme::txt()); g.setFont(Theme::font(nr ? 10.5f : 13.0f, 2));
        auto tw = juce::GlyphArrangement::getStringWidth(Theme::font(nr ? 10.5f : 13.0f, 2), dt);
        auto dr = dbR.toFloat(); float x0 = dr.getCentreX() - (tw + 14) * 0.5f;
        g.drawText(dt, juce::Rectangle<float>(x0, dr.getY(), tw + 2, dr.getHeight()), juce::Justification::centredLeft);
        g.setColour(Theme::sub()); g.setFont(Theme::font(7.0f, 1)); g.drawText("dB", juce::Rectangle<float>(x0 + tw + 2, dr.getY() + 1, 14, dr.getHeight()), juce::Justification::centredLeft);
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (c.model().selected == s) { g.setColour(Theme::strip(s).withAlpha(0.85f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 15.0f, 2.0f); }
    }
private:
    Canvas& c; int s; bool nr = false, dotsBelow = false;
    juce::OwnedArray<juce::Component> kids;
    Pill *head = nullptr, *rec = nullptr, *tog = nullptr, *clr = nullptr, *spd = nullptr, *file = nullptr, *mute = nullptr, *solo = nullptr, *cue = nullptr, *room = nullptr,
         *input = nullptr, *fxb = nullptr, *tune = nullptr;
    WaveScreen* wave = nullptr; Meter* meter = nullptr; Meter* meter2 = nullptr; Meter* inMeter = nullptr;
    juce::Component *fader = nullptr, *pan = nullptr, *inb = nullptr;
    juce::Rectangle<int> headR, inR, dotsR, panLabelR, dbR;
};

// ===================================================================== 1.6: the MASTER strip (beside the tracks)
class MasterView : public juce::Component {
public:
    explicit MasterView(Canvas& cv) : c(cv) {
        auto& m = c.model();
        rec = new Pill("REC SHOW", [] { return Theme::red(); }, 9.0f, 12.0f); rec->tint = true; rec->icon = 3;
        rec->onFn = [] { return true; };
        rec->solidFn = [this] { return c.host.isRecording(); };
        rec->dynText = [this] { return c.host.isRecording() ? juce::String("STOP") : juce::String("REC SHOW"); };
        rec->onClick = [this] { c.doRecShow(); }; rec->setComponentID("t:recshow");
        kids.add(rec); addAndMakeVisible(rec);
        master = new Knob([&m] { return m.get("master"); }, [&m](double v) { m.set("master", v); }, 0, 100, 75, [] { return Theme::blue(); });
        master->setComponentID("p:master"); kids.add(master); addAndMakeVisible(master);
        glue = new Knob([&m] { return m.get("glue"); }, [&m](double v) { m.set("glue", v); }, 0, 100, 25, [] { return Theme::amber(); });
        glue->setComponentID("p:glue"); kids.add(glue); addAndMakeVisible(glue);
        for (auto* mt : { new Meter([this] { return c.engine().peak("glue_gr"); }, true), new Meter([this] { return c.engine().peak("live_in1"); }, true),
                          new Meter([this] { return c.engine().peak("mainL"); }, true), new Meter([this] { return c.engine().peak("mainR"); }, true) }) {
            meters.add(mt); addAndMakeVisible(mt); }
    }
    ~MasterView() override { meters.clear(true); kids.clear(true); }
    void resized() override {
        int w = getWidth(), h = getHeight(), P = 12, iw = w - 2 * P;
        rec->setBounds(P, 40, iw, 40);
        int ks = juce::jmin(iw - 10, h > 440 ? 80 : 66);
        master->setBounds((w - ks) / 2, 112, ks, ks);
        int gs = juce::jmin(iw - 30, 54), gy = 112 + ks + 26;
        glue->setBounds((w - gs) / 2, gy, gs, gs);
        meters[0]->setBounds((w + gs) / 2 + 4, gy + 6, 4, gs - 12);           // how much the glue holds back
        int my = gy + gs + 28, mh = juce::jmax(40, h - my - 46);
        meters[1]->setBounds(w / 2 - 26, my, 6, mh);
        meters[2]->setBounds(w / 2 + 14, my, 5, mh); meters[3]->setBounds(w / 2 + 21, my, 5, mh);
        meterLabelY = my + mh + 6; glueLabelY = gy + gs + 4; masterLabelY = 112 + ks + 4;
    }
    void update() { for (auto* mt : meters) mt->update(); repaint(); for (auto* k : kids) k->repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        drawPanel(g, r, 16.0f);
        { juce::Graphics::ScopedSaveState ss(g); juce::Path clip; clip.addRoundedRectangle(r, 16.0f); g.reduceClipRegion(clip);
          g.setGradientFill(juce::ColourGradient(Theme::txt().withAlpha(0.5f), 0, 0, Theme::txt().withAlpha(0.12f), r.getWidth(), 0, false)); g.fillRect(0.0f, 0.0f, r.getWidth(), 3.0f); }
        int w = getWidth();
        label(g, "MASTER", { 0, 10, w, 22 }, 12.5f, Theme::txt(), 2, juce::Justification::centred);
        double sec = c.host.recordSeconds();
        label(g, juce::String::formatted("%02d:%02d", (int) sec / 60, (int) sec % 60), { 0, 84, w, 18 }, 10.5f, Theme::sub(), 2, juce::Justification::centred);
        label(g, "MASTER", { 0, masterLabelY, w, 14 }, 8.0f, Theme::sub(), 2, juce::Justification::centred);
        label(g, "GLUE", { 0, glueLabelY, w, 14 }, 8.0f, Theme::sub(), 2, juce::Justification::centred);
        label(g, "IN", { w / 2 - 41, meterLabelY, 36, 12 }, 7.5f, Theme::sub(), 2, juce::Justification::centred);
        label(g, "OUT", { w / 2 + 5, meterLabelY, 36, 12 }, 7.5f, Theme::sub(), 2, juce::Justification::centred);
        label(g, "CPU " + juce::String((int) std::round(c.engine().cpu.load() * 100)) + " %", { 0, getHeight() - 24, w, 14 }, 7.5f, Theme::sub(), 1, juce::Justification::centred);
    }
private:
    Canvas& c; juce::OwnedArray<juce::Component> kids; juce::OwnedArray<Meter> meters;
    Pill* rec = nullptr; Knob *master = nullptr, *glue = nullptr;
    int meterLabelY = 0, glueLabelY = 0, masterLabelY = 0;
};

// ===================================================================== one effect card
// 1.6: the new design - an on/off switch, the name (tap: presets), what it does, its value; big knobs; the signal line at the bottom
class CardView : public juce::Component {
public:
    CardView(Canvas& cv, const CardDef& d) : c(cv), def(d) {
        auto& m = c.model();
        std::string key = d.key;
        std::map<std::string, std::string> scopeBus = { { "eq", "selsig" }, { "drv", "selsig" }, { "trm", "selsig" }, { "phs", "selsig" }, { "swl", "selsig" },
            { "tun", "live1" }, { "cho", "choL" }, { "dly", "dlyL" }, { "rev", "revL" }, { "fxb", "fxbL" }, { "halo", "haloL" }, { "res", "resL" },
            { "grn", "grnL" }, { "frz", "frzL" }, { "spc", "to_spc" }, { "orb", "to_orb" } };
        for (auto& e : extraFxDefs()) scopeBus[e.key] = "xr_" + std::string(e.key);
        if (scopeBus.count(key)) { scope = new MiniScope(&c.engine().ring(scopeBus[key]), [key] { return Theme::fx(key); }); kids.add(scope); addAndMakeVisible(scope); }
        for (int j = 0; j < (int) d.knobs.size(); ++j) {
            std::string id = d.knobs[(size_t) j].id;
            Knob* kn;
            if (id == "SEND") {
                kn = new Knob([this, key] { auto p = Model::stripParam(c.model().selected, key); return p.empty() ? 0.0 : c.model().get(p); },
                              [this, key](double v) { auto p = Model::stripParam(c.model().selected, key); if (!p.empty()) c.model().set(p, v); },
                              0, 100, 0, [key] { return Theme::fx(key); });
                sendKnob = kn;
            } else {
                auto* pd = m.def(id);
                kn = new Knob([&m, id] { return m.get(id); }, [&m, id](double v) { m.set(id, v); }, pd ? pd->lo : 0, pd ? pd->hi : 100, pd ? pd->def : 0,
                              [key] { return Theme::fx(key); });
            }
            kn->setComponentID(id == "SEND" ? juce::String("send:" + key) : juce::String("p:" + id));
            kids.add(kn); addAndMakeVisible(kn); knobs.push_back(kn);
            labels.push_back({ U(d.knobs[(size_t) j].label), {}, id == "SEND" || juce::String(d.knobs[(size_t) j].label) == "AMOUNT" });
        }
        auto pill = [&](const juce::String& t, ColourFn bg, std::function<void()> f, const juce::String& id) {
            auto* p = new Pill(t, std::move(bg), 8.0f, 8.0f); p->tint = true; p->onClick = std::move(f); p->setComponentID(id);
            kids.add(p); addAndMakeVisible(p); badgePill = p; };
        if (key == "frz") pill("HOLD", [&m] { return m.get("frz_on") > 0.5 ? Theme::fx("frz") : Theme::btn(); }, [&m] { m.set("frz_on", m.get("frz_on") > 0.5 ? 0 : 1); }, "p:frz_on");
        if (key == "pdl") pill("PEDAL", [&m] { return m.get("pdl_down") > 0.5 ? Theme::fx("pdl") : Theme::btn(); }, [&m] { m.set("pdl_down", m.get("pdl_down") > 0.5 ? 0 : 1); }, "p:pdl_down");
        if (key == "fbk") pill("LET GO", [&m] { return m.get("fbk_stop") > 0.5 ? Theme::fx("fbk") : Theme::btn(); },
                               [&m] { m.set("fbk_stop", 1); juce::Timer::callAfterDelay(400, [&m] { m.set("fbk_stop", 0); }); }, "p:fbk_stop");
        if (key == "drm") pill("ON", [&m] { return m.get("drm_on") > 0.5 ? Theme::green() : Theme::btn(); }, [&m] { m.set("drm_on", m.get("drm_on") > 0.5 ? 0 : 1); }, "p:drm_on");
        if (key == "shd") {
            auto nm = shadowNames();
            for (int v = 0; v < 12; ++v) {
                std::string id = "shd_v" + std::to_string(v);
                auto* p = new Pill(U(nm[(size_t) v].c_str()), [&m, id] { return m.get(id) > 0.5 ? Theme::fx("shd") : Theme::btn(); }, 7.5f, 6.0f);
                p->tint = true; p->onClick = [&m, id] { m.set(id, m.get(id) > 0.5 ? 0 : 1); };
                p->setComponentID("p:" + juce::String(id)); kids.add(p); addAndMakeVisible(p); shadowPills.push_back(p);
            }
        }
    }
    ~CardView() override { kids.clear(true); }
    int cols() const { int n = (int) knobs.size(); return n <= 4 ? n : n <= 6 ? 3 : n <= 8 ? 4 : 5; }
    int minWidth() const { return cols() * 64 + 30; }
    float weight() const { return juce::jmax(1.0f, cols() / 3.0f); }
    void update() { repaint(); for (auto* k : kids) k->repaint(); }
    // on / off for the strip the SEND knobs edit (TUNE: its amount; DRUMS: on); the level comes back when switched on again
    bool isOn() const {
        juce::String key = def.key; auto& m = c.model();
        if (key == "tun") return m.get("tn_amt") > 0;
        if (key == "drm") return m.get("drm_on") > 0.5;
        auto p = Model::stripParam(m.selected, def.key);
        return !p.empty() && m.def(p) != nullptr && m.get(p) > 0.5;
    }
    void toggle() {
        juce::String key = def.key; auto& m = c.model();
        if (key == "drm") { m.set("drm_on", m.get("drm_on") > 0.5 ? 0 : 1); return; }
        std::string p = key == "tun" ? std::string("tn_amt") : Model::stripParam(m.selected, def.key);
        if (p.empty() || m.def(p) == nullptr) { c.showMessage(U(def.name) + juce::String(" is not used on this strip")); return; }
        double v = m.get(p);
        if (v > 0.5) { lastOn[p] = v; m.set(p, 0); }
        else m.set(p, lastOn.count(p) ? lastOn[p] : 50.0);
    }
    void resized() override {
        int w = getWidth(), h = getHeight(), P = 14;
        headerR = { P, 12, w - 2 * P, 30 };
        toggleR = { P, 17, 38, 21 };
        int bw = 0;
        if (badgePill) { bw = badgePill->getComponentID() == "p:fbk_stop" ? 64 : 56; badgePill->setBounds(w - P - bw, 15, bw, 26); }
        badgeR = { w - P - (bw ? bw + 6 : 0) - 96, 15, 96, 26 };
        int y = 50;
        if (!shadowPills.empty()) {
            int pw = (w - 2 * P - 5 * 5) / 6;
            for (int v = 0; v < 12; ++v) shadowPills[(size_t) v]->setBounds(P + (v % 6) * (pw + 5), y + (v / 6) * 26, pw, 22);
            y += 54;
        }
        if (juce::String(def.key) == "drm") { drumR = { P, y, w - 2 * P, 34 }; y += 42; }
        int foot = 22, n = (int) knobs.size(), cl = juce::jmax(1, cols()), rows = (n + cl - 1) / cl;
        auto area = juce::Rectangle<int>(P, y, w - 2 * P, h - y - foot - 8);
        int ks = juce::jlimit(36, 74, juce::jmin(area.getHeight() / juce::jmax(1, rows) - 26, area.getWidth() / cl - 18));
        area = area.withSizeKeepingCentre(area.getWidth(), juce::jmin(area.getHeight(), rows * (ks + 56)));
        float cw = area.getWidth() / (float) cl, rh = area.getHeight() / (float) juce::jmax(1, rows);
        for (int j = 0; j < n; ++j) {
            int r = j / cl, cc = j % cl;
            float cx = area.getX() + cw * (cc + 0.5f), cy = area.getY() + rh * (r + 0.5f) - 9;
            knobs[(size_t) j]->setBounds((int) (cx - ks / 2), (int) (cy - ks / 2), ks, ks);
            labels[(size_t) j].r = { (int) (cx - cw / 2), (int) (cy + ks / 2 + 4), (int) cw, 14 };
        }
        if (scope) scope->setBounds(P, h - foot - 6, (w - 2 * P) / 2, foot - 4);
        footR = { P + (w - 2 * P) / 2 + 8, h - foot - 6, (w - 2 * P) / 2 - 8, foot - 4 };
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (toggleR.expanded(6).contains(e.getPosition())) { toggle(); update(); return; }
        if (!nameR.expanded(4).contains(e.getPosition())) return;
        auto& m = c.model(); juce::String key = def.key;
        juce::PopupMenu menu, del;
        auto names = fxpresets::names(key);
        menu.addSectionHeader(U(def.name) + " - presets");
        for (int i = 0; i < names.size(); ++i) { menu.addItem(100 + i, names[i]); del.addItem(300 + i, names[i]); }
        if (names.isEmpty()) menu.addItem(-1, "(none yet)", false);
        menu.addSeparator();
        menu.addItem(1, U("Save these settings as a preset …"));
        if (!names.isEmpty()) menu.addSubMenu("Delete a preset", del);
        menu.addSeparator();
        menu.addItem(2, "Use these settings for every new song", true, fxpresets::hasDefault(key));
        if (fxpresets::hasDefault(key)) menu.addItem(3, "New songs: back to the factory settings");
        menu.addItem(4, "Factory settings now");
        juce::Component::SafePointer<CardView> sp(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [sp, key, names, &m](int r) {
            if (sp == nullptr || r == 0) return;
            if (r >= 100 && r < 300) fxpresets::load(m, key, names[r - 100]);
            else if (r >= 300) fxpresets::remove(key, names[r - 300]);
            else if (r == 2) fxpresets::setDefault(m, key);
            else if (r == 3) fxpresets::clearDefault(key);
            else if (r == 4) fxpresets::factory(m, key.toStdString());
            else if (r == 1) {
                auto* w = new juce::AlertWindow("Save a preset", "A name for these " + juce::String(sp->def.name) + " settings:", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor("name", juce::String(sp->def.name).toLowerCase() + " " + juce::String(names.size() + 1));
                w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
                w->enterModalState(true, juce::ModalCallbackFunction::create([w, key, &m](int ok) {
                    auto n = w->getTextEditorContents("name").trim().substring(0, 40);
                    if (ok == 1 && n.isNotEmpty()) fxpresets::save(m, key, n); }), true);
            }
            if (sp != nullptr) sp->update();
        });
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        std::string key = def.key;
        auto col = Theme::fx(key);
        drawPanel(g, r, 16.0f);
        bool on = isOn();
        if (on) { juce::Graphics::ScopedSaveState ss(g); juce::Path clip; clip.addRoundedRectangle(r, 16.0f); g.reduceClipRegion(clip);
                  g.setGradientFill(juce::ColourGradient(col.withAlpha(Theme::lightMode() ? 0.16f : 0.11f), 0, 0, col.withAlpha(0.0f), r.getWidth() * 0.62f, r.getHeight() * 0.5f, true));
                  g.fillRect(r); }
        drawToggle(g, toggleR.toFloat(), on, col);
        auto& m = c.model();
        auto pick = [](double v, int n, const char* const* names) { return juce::String(names[juce::jlimit(0, n - 1, (int) (v / 100.0 * (n - 0.01)))]); };
        static const char* DLY[8] = { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1/1" };
        static const char* TRM[6] = { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" };
        static const char* REV[6] = { "1/8", "1/4", "1/4.", "1/2", "1/2.", "1/1" };
        static const char* FXB[6] = { "LO-FI", "RING", "FILTER", "STUTTER", "METAL", "OCTAVE" };
        static const char* PTH[3] = { "CIRCLE", "SWING", "WANDER" };
        static const char* STY[8] = { "POP", "FUNK", "HIP-HOP", "HOUSE", "BOSSA", "JAZZ", "REGGAE", "AMBIENT" };
        static const char* KIT[4] = { "ACOUSTIC", "808", "909", "LO-FI" };
        static const char* SCL[2] = { "CHROMATIC", "IN-KEY" };
        static const char* N[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        juce::String badge, foot;
        if (key == "dly") badge = pick(m.get("dly_time"), 8, DLY);
        if (key == "trm") badge = pick(m.get("trm_rate"), 6, TRM);
        if (key == "rev") badge = pick(m.get("rev_time"), 6, REV);
        if (key == "fxb") badge = pick(m.get("fxb_mode"), 6, FXB);
        if (key == "orb") {
            badge = pick(m.get("orb_path"), 3, PTH);
            static const char* DIR[8] = { "FRONT", "FRONT-R", "RIGHT", "BACK-R", "BEHIND", "BACK-L", "LEFT", "FRONT-L" };
            static const char* DIS[4] = { "close", "near", "far", "distant" };
            float pos = c.snap("orb_pos"), dm = c.snap("orb_dm");
            int di = ((int) ((pos + 0.0625f) * 8)) % 8, dd = (dm > 1.2f) + (dm > 3.5f) + (dm > 8.0f);
            foot = juce::String(DIR[juce::jlimit(0, 7, di)]) + U(" · ") + DIS[dd];
        }
        if (key == "tun") { badge = pick(m.get("tn_scale"), 2, SCL); foot = juce::String((int) std::round(c.snap("tn_corr") * 100)) + U("¢"); }
        if (key == "halo") { int k = (int) c.snap("halo_key"); badge = k >= 0 ? juce::String(N[k % 12]) + U(" · ") + N[(k + 9) % 12] + "m" : juce::String(U("KEY –"));
                             foot = juce::String((int) c.snap("halo_n")) + " voices";
                             if (sendKnob) sendKnob->enabled = !(c.model().selected >= 1 && c.model().selected <= 3); }
        if (key == "fbk") { static const char* FM[3] = { "CATCH", "LISTEN", "HOLD" }; static const char* FH[5] = { "the note", "octave", "12th", "2 octaves", "free" };
                            foot = pick(m.get("fbk_mode"), 3, FM) + U(" · ") + pick(m.get("fbk_harm"), 5, FH); }
        if (key == "pdl" && m.get("pdl_auto") > 4) { static const char* AP[4] = { "every 2 bars", "every bar", "every half bar", "every beat" };
                            foot = U("auto · ") + AP[juce::jlimit(0, 3, (int) (m.get("pdl_auto") / 100.0 * 3.99))]; }
        // the header: name ▾ (presets), what it does, its value
        auto f = Theme::font(13.0f, 2);
        juce::String nm = U(def.name);
        int nw = (int) juce::GlyphArrangement::getStringWidth(f, nm) + 4;
        nameR = { toggleR.getRight() + 12, 14, nw + 14, 26 };
        g.setColour(on ? Theme::txt() : Theme::sub()); g.setFont(f); g.drawText(nm, nameR.withWidth(nw), juce::Justification::centredLeft);
        label(g, U("▾"), { nameR.getX() + nw, 14, 12, 26 }, 9.0f, Theme::sub(), 0);
        int bx = badgePill ? badgePill->getX() - 6 : getWidth() - 14;
        if (badge.isNotEmpty()) {
            int bw2 = (int) juce::GlyphArrangement::getStringWidth(Theme::font(8.0f, 2), badge) + 22;
            auto br = juce::Rectangle<int>(bx - bw2, 15, bw2, 26); bx = br.getX() - 6;
            g.setColour(Theme::btn()); g.fillRoundedRectangle(br.toFloat(), 8.0f);
            label(g, badge, br, 8.0f, Theme::sub(), 2, juce::Justification::centred);
        }
        auto descR = juce::Rectangle<int>(nameR.getRight() + 6, 14, bx - nameR.getRight() - 10, 26);
        auto df = Theme::font(8.0f, 0);
        if (descR.getWidth() > juce::GlyphArrangement::getStringWidth(df, U(def.desc)) + 4) label(g, U(def.desc), descR, 8.0f, Theme::sub(), 0);
        if (key == "drm") {
            g.setColour(Theme::scr()); g.fillRoundedRectangle(drumR.toFloat(), 8.0f);
            label(g, pick(m.get("drm_style"), 8, STY), drumR.withWidth(drumR.getWidth() / 2), 10.0f, Theme::txt(), 2, juce::Justification::centred);
            label(g, pick(m.get("drm_kit"), 4, KIT), drumR.withTrimmedLeft(drumR.getWidth() / 2), 10.0f, Theme::txt(), 2, juce::Justification::centred);
        }
        for (auto& l : labels) label(g, l.text, l.r, 8.0f, l.send ? Theme::ink(col) : Theme::sub(), 2, juce::Justification::centred);
        if (foot.isNotEmpty()) label(g, foot, footR, 8.0f, Theme::txt(), 2, juce::Justification::centredRight);
    }
    void paintOverChildren(juce::Graphics& g) override {      // switched off: the knobs a little quieter
        if (isOn() || juce::String(def.key) == "drm") return;
        g.setColour(Theme::card().withAlpha(0.32f)); for (auto* k : knobs) g.fillEllipse(k->getBounds().toFloat());
    }
private:
    Canvas& c; const CardDef& def;
    juce::OwnedArray<juce::Component> kids;
    struct L { juce::String text; juce::Rectangle<int> r; bool send; };
    std::vector<L> labels;
    std::vector<Knob*> knobs; std::vector<Pill*> shadowPills;
    Knob* sendKnob = nullptr; Pill* badgePill = nullptr; MiniScope* scope = nullptr;
    juce::Rectangle<int> headerR, toggleR, nameR, badgeR, drumR, footR;
    std::map<std::string, double> lastOn;
};

// ===================================================================== 1.6: the effect map
// every shown effect, grouped in its family; lit = working now. Tap a family (or an effect) to open it below.
class FxMap : public juce::Component {
public:
    explicit FxMap(Canvas& cv) : c(cv) {}
    struct Hit { juce::Rectangle<int> r; int fam = -1, card = -1, act = 0; };   // act: 1 choose, 2 prev page, 3 next page
    std::vector<Hit> hits;
    void paint(juce::Graphics& g) override {
        hits.clear();
        auto b = getLocalBounds();
        drawPanel(g, b.toFloat(), 16.0f);
        auto in = b.reduced(6);
        auto& ct = cardTable();
        // right: CHOOSE EFFECTS, and the pages of the open family
        auto ch = in.removeFromRight(112); in.removeFromRight(6);
        g.setColour(Theme::btn()); g.fillRoundedRectangle(ch.toFloat(), 10.0f);
        label(g, "+", ch.withTrimmedBottom(ch.getHeight() / 2).translated(0, 4), 13.0f, Theme::blue(), 2, juce::Justification::centred);
        label(g, "CHOOSE EFFECTS", ch.withTrimmedTop(ch.getHeight() / 2 - 2), 7.5f, Theme::sub(), 2, juce::Justification::centred);
        hits.push_back({ ch, -1, -1, 1 });
        if (c.numCardPages > 1) {
            auto pg = in.removeFromRight(96); in.removeFromRight(6);
            auto l = pg.removeFromLeft(30), r = pg.removeFromRight(30);
            g.setColour(Theme::btn()); g.fillRoundedRectangle(l.toFloat(), 9.0f); g.fillRoundedRectangle(r.toFloat(), 9.0f);
            label(g, U("‹"), l, 15.0f, c.cardPage > 0 ? Theme::txt() : Theme::off(), 2, juce::Justification::centred);
            label(g, U("›"), r, 15.0f, c.cardPage < c.numCardPages - 1 ? Theme::txt() : Theme::off(), 2, juce::Justification::centred);
            label(g, juce::String(c.cardPage + 1) + " / " + juce::String(c.numCardPages), pg, 9.0f, Theme::txt(), 2, juce::Justification::centred);
            hits.push_back({ l, -1, -1, 2 }); hits.push_back({ r, -1, -1, 3 });
        }
        // the families
        juce::Array<int> fc[Canvas::NFAM]; float wt[Canvas::NFAM];
        const int gap = 6, rowsF = getHeight() > 90 ? 2 : 1, perRowF = Canvas::NFAM / rowsF;     // upright: two rows of three families
        int total = 0; for (int f = 0; f < Canvas::NFAM; ++f) { fc[f] = c.familyCards(f); total += fc[f].size(); }
        float availAll = (float) (in.getWidth() - gap * (perRowF - 1));
        bool cramped = availAll * rowsF / (float) juce::jmax(1, total + 5) < 40.0f;     // many effects: the open family gets the room for its names
        for (int f = 0; f < Canvas::NFAM; ++f) {
            float n = (float) fc[f].size();
            wt[f] = cramped && f != c.fxFamily ? juce::jmax(1.6f, n * 0.25f + 0.9f) : juce::jmax(1.6f, n) + 0.8f; }
        int rowH = (in.getHeight() - (rowsF - 1) * gap) / rowsF;
        float x = (float) in.getX();
        for (int f = 0; f < Canvas::NFAM; ++f) {
            int rowI = f / perRowF;
            if (f % perRowF == 0) x = (float) in.getX();
            float wsum = 0; for (int q = rowI * perRowF; q < (rowI + 1) * perRowF; ++q) wsum += wt[q];
            int gw = (int) (availAll * wt[f] / wsum);
            juce::Rectangle<int> gr((int) x, in.getY() + rowI * (rowH + gap), gw, rowH); x += (float) gw + gap;
            auto col = Canvas::familyColour(f); bool open = f == c.fxFamily;
            if (open) { g.setColour(col.withAlpha(Theme::lightMode() ? 0.20f : 0.14f)); g.fillRoundedRectangle(gr.toFloat(), 11.0f);
                        g.setColour(col.withAlpha(0.75f)); g.drawRoundedRectangle(gr.toFloat().reduced(0.75f), 11.0f, 1.5f); }
            int n = fc[f].size(), on = 0;
            for (int i : fc[f]) on += c.fxUsed(ct[(size_t) i].key);
            auto hd = gr.reduced(7, 0).withHeight(17).translated(0, 3);
            g.setColour(col); g.fillEllipse((float) hd.getX(), (float) hd.getCentreY() - 3.0f, 6, 6);
            label(g, Canvas::familyName(f), hd.withTrimmedLeft(10).withTrimmedRight(26), 7.5f, open ? Theme::ink(col) : Theme::sub(), 2);
            label(g, juce::String(on) + "/" + juce::String(n), hd.removeFromRight(26), 7.5f, on ? Theme::ink(col) : Theme::sub(), 2, juce::Justification::centredRight);
            hits.push_back({ gr, f, -1, 0 });
            auto chips = gr.reduced(5, 0).withTop(gr.getY() + 22).withBottom(gr.getBottom() - 5);
            if (n == 0) { label(g, "none chosen", chips, 7.0f, Theme::off(), 0, juce::Justification::centred); continue; }
            int cw = (chips.getWidth() - (n - 1) * 4) / n;
            for (int k = 0; k < n; ++k) {
                int ci = fc[f][k]; auto& d = ct[(size_t) ci];
                juce::Rectangle<int> r(chips.getX() + k * (cw + 4), chips.getY(), cw, chips.getHeight());
                bool u = c.fxUsed(d.key);
                g.setColour(u ? col.withAlpha(Theme::lightMode() ? 0.38f : 0.30f) : Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 8.0f);
                auto tr = r.reduced(3, 0);
                if (u && cw > 46) { g.setColour(col); g.fillEllipse((float) tr.getX() + 2, (float) tr.getCentreY() - 2.0f, 4, 4); tr.removeFromLeft(7); }
                if (cw >= 24) { g.setColour(u ? Theme::txt() : Theme::sub()); g.setFont(Theme::font(cw >= 34 ? 7.5f : 6.5f, 2));
                                g.drawFittedText(U(d.name), tr, juce::Justification::centred, 1, 0.5f); }
                else if (u) { g.setColour(col); g.fillEllipse(r.toFloat().withSizeKeepingCentre(5, 5)); }
                hits.push_back({ r, f, ci, 0 });
            }
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        auto pt = e.getPosition();
        for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->r.contains(pt)) {
            if (it->act == 1) c.openLook(true);
            else if (it->act == 2) { if (c.cardPage > 0) { --c.cardPage; c.layoutCards(); } }
            else if (it->act == 3) { if (c.cardPage < c.numCardPages - 1) { ++c.cardPage; c.layoutCards(); } }
            else if (it->card >= 0) c.showCard(it->card);
            else if (it->fam >= 0) c.showFamily(it->fam);
            repaint(); return;
        }
    }
private:
    Canvas& c;
};

int Canvas::familyOf(const juce::String& k) {
    static const std::map<juce::String, int> F = {
        { "eq", 0 }, { "drv", 0 }, { "swl", 0 }, { "tun", 0 }, { "wah", 0 }, { "flt", 0 }, { "oct", 0 }, { "pit", 0 }, { "crs", 0 }, { "tap", 0 }, { "pdl", 0 },
        { "trm", 1 }, { "phs", 1 }, { "cho", 1 }, { "flg", 1 }, { "vib", 1 }, { "rng", 1 },
        { "spc", 2 }, { "orb", 2 }, { "spr", 2 },
        { "dly", 3 }, { "rev", 3 }, { "fxb", 3 }, { "shd", 3 }, { "png", 3 }, { "stu", 3 },
        { "halo", 4 }, { "res", 4 }, { "grn", 4 }, { "frz", 4 }, { "fbk", 4 }, { "voc", 4 }, { "tlk", 4 },
        { "drm", 5 } };
    auto it = F.find(k); return it == F.end() ? 4 : it->second;
}
const char* Canvas::familyName(int f) { static const char* N[NFAM] = { "TONE", "MOVEMENT", "SPACE", "ECHOES", "TEXTURE", "RHYTHM" }; return N[juce::jlimit(0, NFAM - 1, f)]; }
juce::Colour Canvas::familyColour(int f) { static const char* K[NFAM] = { "eq", "trm", "spc", "dly", "halo", "drv" }; return Theme::fx(K[juce::jlimit(0, NFAM - 1, f)]); }
juce::Array<int> Canvas::familyCards(int f) const {
    juce::Array<int> v; auto& ct = cardTable();
    for (int i = 0; i < (int) ct.size(); ++i) if (host.visibleCards.contains(ct[(size_t) i].key) && familyOf(ct[(size_t) i].key) == f) v.add(i);
    return v;
}
bool Canvas::fxUsed(const juce::String& key) {
    static const std::map<juce::String, int> bit = { { "eq", 0 }, { "drv", 1 }, { "trm", 2 }, { "phs", 3 }, { "swl", 4 }, { "cho", 5 }, { "dly", 6 },
        { "rev", 7 }, { "fxb", 8 }, { "shd", 9 }, { "orb", 10 }, { "res", 12 }, { "grn", 13 }, { "frz", 14 }, { "spc", 15 } };
    if (key == "halo") return snap("halo_used") > 0.5f;
    if (key == "tun") return model().get("tn_amt") > 0;
    if (key == "drm") return model().get("drm_on") > 0.5;
    auto it = bit.find(key);
    if (it != bit.end()) return (((int) snap("fxused")) >> it->second) & 1;
    for (int s = 0; s < NSTRIP; ++s) { auto id = Model::stripParam(s, key.toStdString()); if (model().def(id) != nullptr && model().get(id) > 0.5) return true; }
    return false;
}
void Canvas::showFamily(int f, int page) { fxFamily = juce::jlimit(0, NFAM - 1, f); cardPage = page; layoutCards(); if (fxMap) fxMap->repaint(); }
void Canvas::showCard(int ci) {
    int f = familyOf(cardTable()[(size_t) ci].key);
    fxFamily = f; cardPage = 0; layoutCards();                     // learns how many fit on a page
    int idx = familyCards(f).indexOf(ci), per = juce::jmax(1, cardsPerPage);
    showFamily(f, idx < 0 ? 0 : idx / per);
}

// ===================================================================== the canvas
Canvas::Canvas(AudioHost& h) : host(h) {
    setOpaque(true);
    buildTopBar();
    buildScenes();
    for (int s = 0; s < NSTRIP; ++s) { auto* sv = strips.add(new StripView(*this, s)); addAndMakeVisible(sv); }
    masterView = std::make_unique<MasterView>(*this); addAndMakeVisible(masterView.get());
    auto& ct = cardTable();
    for (int i = 0; i < (int) ct.size(); ++i) {
        auto* cv = cards.add(new CardView(*this, ct[(size_t) i]));
        addChildComponent(cv);
    }
    fxMap = std::make_unique<FxMap>(*this); addAndMakeVisible(fxMap.get());
    setup = std::make_unique<SetupPanel>(*this);
    addChildComponent(setup.get()); setup->setBounds(0, 0, 1440, 880);
    look = std::make_unique<LookPanel>(*this);
    addChildComponent(look.get()); look->setBounds(0, 0, 1440, 880);
    spatial = std::make_unique<SpatialPanel>(*this);
    addChildComponent(spatial.get()); spatial->setBounds(0, 0, 1440, 880);
    keyboardWin = std::make_unique<KeyboardPanel>(*this);
    addChildComponent(keyboardWin.get()); keyboardWin->setBounds(0, 0, 1440, 880);
    modularWin = std::make_unique<ModularPanel>(*this);
    addChildComponent(modularWin.get()); modularWin->setBounds(0, 0, 1440, 880);
    exportSheet = std::make_unique<ExportSheet>(*this); addChildComponent(exportSheet.get()); exportSheet->setBounds(0, 0, 1440, 880);
    helpSheet = std::make_unique<HelpSheet>(*this); addChildComponent(helpSheet.get()); helpSheet->setBounds(0, 0, 1440, 880);
    aboutSheet = std::make_unique<AboutSheet>(*this); addChildComponent(aboutSheet.get()); aboutSheet->setBounds(0, 0, 1440, 880);
    importSheet = std::make_unique<ImportSheet>(*this); addChildComponent(importSheet.get()); importSheet->setBounds(0, 0, 1440, 880);
    autoMixSheet = std::make_unique<AutoMixSheet>(*this); addChildComponent(autoMixSheet.get()); autoMixSheet->setBounds(0, 0, 1440, 880);
    menu = std::make_unique<AppMenu>(*this);
    synthWin = std::make_unique<SynthPanel>(*this);
    addChildComponent(synthWin.get()); synthWin->setBounds(0, 0, 1440, 880);
    learnLayer = std::make_unique<LearnOverlay>(*this, host);
    addChildComponent(learnLayer.get()); learnLayer->setBounds(0, 0, 1440, 880);
    learnLayer->onDone = [this] { setMidiLearn(false); };
    learnLayer->activeWindow = [this]() -> juce::Component* { auto* w = topWindow(); return w != nullptr ? w : this; };
    host.onMidiControl = [this](const juce::String& id, int t, int v, bool press) { if (auto* w = learn::find(this, id)) learn::apply(w, t, v, press); };
    host.onLearned = [this] { if (learnLayer->isVisible()) learnLayer->learned(); };
    setWantsKeyboardFocus(true);
    host.onAction = [this](int a) { const char* W[4] = { "REC", U("PLAY / STOP").toRawUTF8(), "UNDO", "NEXT SCENE" };
        flash = a == 1 ? U("PLAY / STOP") : juce::String(W[a]); flashUntil = juce::Time::getMillisecondCounterHiRes() + 900; };
    layoutHeader();
    layoutStrips();
    layoutCards();
    startTimerHz(30);
}

Canvas::~Canvas() { stopTimer(); masterView = nullptr; studioWindow = nullptr; host.onAction = nullptr; host.onMidiControl = nullptr; host.onLearned = nullptr; }

void Canvas::chooseFile(const juce::String& title, const juce::String& pattern, bool save, std::function<void(juce::File)> done, bool dirs) {
   #if JUCE_IOS
    // iPad / iPhone: songs and recordings live in ARCO's folder (Files app → On My iPad → ARCO); the picker only imports audio files
    juce::ignoreUnused(save, dirs);
    chooser = std::make_unique<juce::FileChooser>(title, juce::File(), pattern);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [done](const juce::FileChooser& fc) {
        auto url = fc.getURLResult();
        if (url.isEmpty()) return;
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(url.getFileName());
        tmp.deleteFile();
        if (auto in = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress))) {
            juce::FileOutputStream os(tmp);
            if (os.openedOk()) { os.writeFromInputStream(*in, -1); os.flush(); }
        }
        if (tmp.existsAsFile()) done(tmp); });
   #else
    chooser = std::make_unique<juce::FileChooser>(title, juce::File::getSpecialLocation(juce::File::userMusicDirectory), pattern);
    int flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting) : juce::FileBrowserComponent::openMode;
    flags |= dirs ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles;
    if (dirs && !save) flags |= juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [done](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) done(f); });
   #endif
}

juce::File Canvas::songsDir() {
   #if JUCE_IOS
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Songs");
   #else
    auto d = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("ARCO Songs");
   #endif
    d.createDirectory(); return d;
}

void Canvas::doOpen() {
   #if JUCE_IOS
    juce::PopupMenu pm; juce::Array<juce::File> songs;
    for (auto& f : songsDir().findChildFiles(juce::File::findDirectories, false)) if (!f.findChildFiles(juce::File::findFiles, false, "*-settings.json").isEmpty()) songs.add(f);
    if (songs.isEmpty()) { showMessage("no songs saved yet"); return; }
    for (int i = 0; i < songs.size(); ++i) pm.addItem(i + 1, songs[i].getFileName());
    pm.showMenuAsync(juce::PopupMenu::Options(), [this, songs](int r) { if (r > 0) host.openSong(songs[r - 1]); });
   #else
    chooseFile("Open a song (its folder, or its -settings.json)", "*-settings.json", false, [this](juce::File f) { host.openSong(f); }, true);
   #endif
}

void Canvas::doRecShow() {
    if (host.isRecording()) { host.stopRecording(); return; }
   #if JUCE_IOS
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Recordings"); d.createDirectory();
    host.startRecording(d.getChildFile("show " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M") + ".wav"));
   #else
    chooseFile("Record the show to", "*.wav", true, [this](juce::File f) { host.startRecording(f.withFileExtension("wav")); });
   #endif
}

// ===================================================================== 1.6: the top bar and the scene row
// groups: 0 ARCO  1 transport  2 tempo  3 song  4 power  5 scenes  6 tracks  7 windows  8 the flash word
static const int GW[9] = { 132, 400, 530, 266, 48, 385, 250, 684, 150 };
void Canvas::layoutHeader() {
    const int topH = 60, navH = 44;
    if (!portrait) {
        grpR[0] = { 18, 8, GW[0], topH }; grpR[1] = { 156, 8, GW[1], topH }; grpR[2] = { 564, 8, GW[2], topH };
        grpR[3] = { W - 16 - 48 - 8 - GW[3], 8, GW[3], topH }; grpR[4] = { W - 16 - 48, 8, 48, topH };
        grpR[5] = { 18, 78, GW[5], navH }; grpR[6] = { 411, 78, GW[6], navH };
        grpR[7] = { W - 16 - GW[7], 78, GW[7], navH }; grpR[8] = { 669, 78, GW[8], navH };
        headerBottom = 130;
    } else {
        grpR[0] = { 18, 8, GW[0], topH }; grpR[1] = { 156, 8, GW[1], topH }; grpR[4] = { W - 16 - 48, 8, 48, topH };
        grpR[2] = { 18, 76, GW[2], topH }; grpR[3] = { 556, 76, juce::jmin(GW[3], W - 16 - 556), topH };
        grpR[5] = { 18, 144, GW[5], navH }; grpR[6] = { 411, 144, GW[6], navH }; grpR[8] = { 669, 144, juce::jmin(GW[8], W - 16 - 669), navH };
        grpR[7] = { 18, 196, W - 34, navH };
        headerBottom = 248;
    }
    for (auto& k : hk) k.c->setBounds(k.rel.translated(grpR[k.grp].getX(), grpR[k.grp].getY()));
    int n = (int) winBtns.size(), pad = 5, gap = 3;
    float bw = (grpR[7].getWidth() - 2 * pad - (n - 1) * gap) / (float) juce::jmax(1, n);
    for (int i = 0; i < n; ++i) winBtns[(size_t) i]->setBounds(grpR[7].getX() + pad + (int) (i * (bw + gap)), grpR[7].getY() + pad, (int) bw, navH - 2 * pad);
}

void Canvas::setLayout(bool tall, int w, int h) {
    if (tall == portrait && w == W && h == H) return;
    portrait = tall; W = w; H = h;
    layoutHeader();
    // the full-screen windows (SETUP, SYNTH, the sheets ...) keep their design, scaled to the width
    float s = (float) W / 1440.0f;
    auto fit = [&](juce::Component* c) { if (c == nullptr) return;
        c->setBounds(0, 0, 1440, portrait ? (int) std::ceil(H / s) : 880);              // the whole screen is dimmed behind it
        c->setTransform(portrait ? juce::AffineTransform::scale(s).translated(0.0f, 60.0f) : juce::AffineTransform()); };
    for (juce::Component* c : { (juce::Component*) setup.get(), (juce::Component*) look.get(), (juce::Component*) exportSheet.get(), (juce::Component*) helpSheet.get(),
                                (juce::Component*) aboutSheet.get(), (juce::Component*) importSheet.get(), (juce::Component*) autoMixSheet.get(), (juce::Component*) synthWin.get(), (juce::Component*) spatial.get(),
                                (juce::Component*) keyboardWin.get(), (juce::Component*) modularWin.get() }) fit(c);
    if (learnLayer) learnLayer->setBounds(0, 0, W, H);
   #if JUCE_IOS
    if (studioView != nullptr) studioView->setBounds(0, 0, W, H);          // STUDIO uses the whole screen either way
   #endif
    layoutStrips();
    layoutCards();
    repaint();
}

void Canvas::buildTopBar() {
    auto& m = model();
    // the ARCO title opens the menus (File, Edit, View, Help) - on the Mac they are also in the menu bar
    auto* mb = hadd<Pill>(0, { 0, 0, 120, 30 }, "", [] { return juce::Colours::transparentBlack; }, 8.0f, 6.0f);
    mb->onClick = [this] { showMenu(); };
    // transport
    auto* play = hadd<Pill>(1, { 6, 6, 72, 48 }, "", [this] { int t = (int) snap("transport"); return t == 2 ? Theme::amber() : Theme::green(); }, 15.0f, 12.0f);
    play->icon = 1; play->tint = true; play->onFn = [] { return true; };
    play->solidFn = [this] { int t = (int) snap("transport"); return t == 1 || t == 2; };
    play->onClick = [this] { model().press("st_looper", "playall"); }; play->setComponentID("t:play");
    auto* stop = hadd<Pill>(1, { 84, 6, 54, 48 }, "", [] { return Theme::btn(); }, 13.0f, 12.0f);
    stop->icon = 2; stop->textColour = [] { return Theme::txt(); };
    stop->onClick = [this] { model().press("st_looper", "stopall"); }; stop->setComponentID("t:stop");
    auto* undo = hadd<Pill>(1, { 144, 6, 90, 48 }, "UNDO", [this] { return snap("undoav") > 0.5f ? Theme::amber() : Theme::btn(); }, 9.0f, 12.0f);
    undo->icon = 4; undo->tint = true; undo->textColour = [this] { return snap("undoav") > 0.5f ? Theme::ink(Theme::amber()) : Theme::txt(); };
    undo->onClick = [this] { model().press("st_looper", "undo"); }; undo->setComponentID("t:undo");
    auto ro = [&](juce::Rectangle<int> r, const char* bus) {
        auto* v = hadd<ValueBox>(1, r, [this, bus] { return (double) snap(bus); }, [](double) {}, 0, 999, 0); v->readOnly = true; v->flat = true; v->fsize = 19.0f; };
    ro({ 244, 8, 50, 30 }, "barno"); ro({ 294, 8, 50, 30 }, "beatno"); ro({ 344, 8, 50, 30 }, "loopbars");
    // tempo
    auto* tap = hadd<Pill>(2, { 6, 6, 62, 48 }, "TAP", [] { return Theme::btn(); }, 9.0f, 8.0f);
    tap->seg = 1; tap->textColour = [] { return Theme::txt(); }; tap->setComponentID("t:tap");
    tap->onClick = [this] {
        static double last = 0; double now = juce::Time::getMillisecondCounterHiRes(); double d = now - last; last = now;
        if (d > 200 && d < 2000) model().set("bpm", std::round(600000.0 / d) / 10.0); };
    auto* rst = hadd<Pill>(2, { 68, 6, 62, 48 }, "RESET", [] { return Theme::btn(); }, 9.0f, 8.0f);
    rst->seg = 3;
    rst->onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Reset everything?", "All loops of all scenes are cleared.", "Reset", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this](int r) { if (r == 1) model().press("st_looper", "rstall"); })); };
    auto* bpm = hadd<ValueBox>(2, { 138, 22, 74, 32 }, [this, &m] { return snap("smask") > 0 ? std::round(snap("ebpm") * 10) / 10 : m.get("bpm"); },
                               [&m](double v) { m.set("bpm", v); }, 30, 300, 1, [] { return Theme::blue(); });
    bpm->flat = true; bpm->fsize = 19.0f;
    auto* bts = hadd<ValueBox>(2, { 216, 22, 54, 32 }, [&m] { return m.get("beats"); }, [&m](double v) { m.set("beats", v); }, 1, 16, 0, [] { return Theme::blue(); });
    bts->flat = true; bts->fsize = 19.0f;
    auto* brs = hadd<ValueBox>(2, { 272, 22, 54, 32 }, [&m] { return m.get("bars"); }, [&m](double v) { m.set("bars", v); }, 0, 32, 0, [] { return Theme::blue(); });
    brs->flat = true; brs->fsize = 19.0f;
    auto tg = [&](juce::Rectangle<int> r, const char* t, const char* id, ColourFn on, int seg) {
        auto* p = hadd<Pill>(2, r, t, [&m, id, on] { return m.get(id) > 0.5 ? on() : Theme::btn(); }, 9.0f, 8.0f);
        p->tint = true; p->seg = seg; p->onClick = [&m, id] { m.set(id, m.get(id) > 0.5 ? 0 : 1); }; p->setComponentID("p:" + juce::String(id)); };
    tg({ 334, 6, 74, 48 }, "COUNT", "countin", [] { return Theme::amber(); }, 1);
    tg({ 408, 6, 74, 48 }, "CLICK", "clickall", [] { return Theme::red(); }, 3);
    hadd<Knob>(2, { 486, 10, 40, 40 }, [&m] { return m.get("clickvol"); }, [&m](double v) { m.set("clickvol", v); }, 0, 100, 60, [] { return Theme::red(); })->setComponentID("p:clickvol");
    // song
    auto* save = hadd<Pill>(3, { 126, 6, 48, 48 }, "SAVE", [] { return Theme::blue(); }, 8.5f, 8.0f);
    save->seg = 1; save->tint = true; save->onFn = [] { return true; };
    save->onClick = [this] { doSave(false); };
    auto* as = hadd<Pill>(3, { 174, 6, 40, 48 }, U("AS…"), [] { return Theme::btn(); }, 8.5f, 8.0f);
    as->seg = 2; as->onClick = [this] { doSave(true); };
    auto* open = hadd<Pill>(3, { 214, 6, 46, 48 }, "OPEN", [] { return Theme::btn(); }, 8.5f, 8.0f);
    open->seg = 3; open->textColour = [] { return Theme::txt(); }; open->onClick = [this] { doOpen(); };
    // the sound on / off
    auto* pw = hadd<Pill>(4, { 4, 6, 40, 48 }, "", [this] { return host.devices.getCurrentAudioDevice() != nullptr ? Theme::green() : Theme::amber(); }, 13.0f, 12.0f);
    pw->icon = 8; pw->textColour = [this] { return host.devices.getCurrentAudioDevice() != nullptr ? Theme::ink(Theme::green()) : Theme::ink(Theme::amber()); };
    pw->bg = [] { return juce::Colours::transparentBlack; };
    pw->setTooltip("the sound on / off");
    pw->onClick = [this] { if (host.devices.getCurrentAudioDevice() != nullptr) host.devices.closeAudioDevice(); else host.devices.restartLastAudioDevice(); };
}

void Canvas::buildScenes() {
    // the windows: one card of buttons
    auto win = [&](const juce::String& t, ColourFn col, std::function<void()> f, const juce::String& id) {
        auto* p = new Pill(t, std::move(col), 9.0f, 10.0f); owned.add(p); addAndMakeVisible(p);
        p->tint = true; p->textColour = nullptr; p->onClick = std::move(f); if (id.isNotEmpty()) p->setComponentID(id);
        p->bg = p->bg; winBtns.push_back(p); return p; };
    auto plain = [] { return juce::Colours::transparentBlack; };
    auto txtOn = [](Pill* p) { p->textColour = [p] { auto c = p->bg(); return c.getAlpha() == 0 ? Theme::txt() : Theme::ink(c); }; };
    Pill* w;
    w = win("SYNTH", plain, [this] { openSynth(true); }, "t:synth"); txtOn(w);
    w = win("KEYS", plain, [this] { openKeyboard(true); }, "t:keys"); txtOn(w); w->setTooltip("KEYBOARD: a big keyboard, sampled instruments, plug-ins, MODULAR");
    w = win("STUDIO", plain, [this] { openStudio(); }, "t:studio"); txtOn(w);
    w = win("AUTO MIX", [this, plain] { return autoMixAvailable() ? Theme::blue() : plain(); }, [this] { openAutoMix(); }, "t:automix"); txtOn(w);
    w = win("MIDI LEARN", [this, plain] { return midiLearn ? Theme::amber() : plain(); }, [this] { setMidiLearn(!midiLearn); }, "t:midilearn"); txtOn(w);
    w = win("EFFECTS", plain, [this] { openLook(true); }, "t:effects"); txtOn(w); w->setTooltip("choose the effect cards");
    w = win("SETUP", plain, [this] { openSetup(true); }, "t:setup"); txtOn(w);
    for (auto* p : winBtns) static_cast<Pill*>(p)->onFn = [p] { return static_cast<Pill*>(p)->bg().getAlpha() > 0; };
    for (int i = 0; i < 4; ++i) {
        auto* tab = hadd<Pill>(5, { 5 + i * 95, 5, 54, 34 }, juce::String::charToString((juce::juce_wchar) ('A' + i)), [this, i] {
            int sc = (int) snap("scenes"), mask = (int) snap("smask"), n = i + 1;
            if (sc % 8 == n) return Theme::green();
            if (sc / 8 == n) return Theme::amber();
            if ((mask >> i) & 1) return Theme::blue();
            return Theme::btn(); }, 15.0f, 10.0f);
        tab->seg = 1; tab->tint = true;
        tab->textColour = [this, i, tab] { auto c = tab->bg(); int mask = (int) snap("smask"); return c != Theme::btn() ? Theme::ink(c) : (((mask >> i) & 1) ? Theme::txt() : Theme::sub()); };
        tab->onClick = [this, i] { model().press("st_looper", "scene", 8, i + 1); }; tab->setComponentID("scene" + juce::String(i + 1));
        auto* cue = hadd<Pill>(5, { 59 + i * 95, 5, 36, 34 }, "", [this, i] { return pvw == i + 1 ? Theme::amber() : Theme::btn(); }, 8.0f, 8.0f);
        cue->seg = 3; cue->tint = true; cue->icon = 5;
        cue->onClick = [this, i] { pvw = pvw == i + 1 ? 0 : i + 1; engine().set("st_looper", "pvw", pvw); };
    }
    auto* nt = hadd<ValueBox>(6, { 70, 7, 56, 30 }, [this] { return snap("ntn") < 1 ? 4.0 : (double) snap("ntn"); },
                              [this](double v) { model().press("st_looper", "ntr", 16, (int) v); }, 1, 8, 0, [] { return Theme::blue(); });
    nt->flat = true; nt->fsize = 17.0f;
}

// 1.6: top to bottom - the strips and MASTER (filling the width), the effect map, the open family's effects
int Canvas::visibleCardCount() const { int n = 0; for (auto& d : cardTable()) n += host.visibleCards.contains(d.key); return n; }

static const int MAP_H = 60, MAP_H2 = 110;
void Canvas::layoutStrips() {
    int ntn = juce::jlimit(1, 8, (int) snap("ntn")); if (snap("ntn") < 1) ntn = 4;
    if (shotTracks > 0) ntn = shotTracks;
    int nvis = 0;
    for (int s = 0; s < NSTRIP; ++s) nvis += s < 4 ? (s < host.nLive || (host.synthInto > 0 && s == host.synthInto)) : (s - 4) < ntn;
    nvis = juce::jmax(1, nvis);
    int ntracks = 0; for (int s = 4; s < NSTRIP; ++s) ntracks += (s - 4) < ntn;
    const int perRow = portrait ? (nvis <= 10 ? nvis : (nvis + 1) / 2) : 12, gap = 8;
    stripRows = (nvis + perRow - 1) / perRow;
    int top = headerBottom;
    int cardsH = portrait ? juce::jlimit(420, 640, (int) (H * 0.37f)) : juce::jlimit(250, 420, (int) (H * 0.29f));
    const int mapH = portrait ? MAP_H2 : MAP_H;
    int avail = H - top - 8 - mapH - 8 - cardsH - 8;
    stripH = juce::jlimit(340, 620, (avail - (stripRows - 1) * 8) / stripRows);
    // MASTER on the right of the (first) row
    int mw = ntracks > 4 || portrait ? 122 : 160;
    int rowW = W - 36 - mw - gap;
    int inRow0 = juce::jmin(perRow, nvis);
    int nLiveVis = 0; for (int s = 0; s < 4; ++s) nLiveVis += (s < host.nLive || (host.synthInto > 0 && s == host.synthInto));
    int sep = nLiveVis > 0 && nvis > nLiveVis && !portrait ? 10 : 0;             // a little room between the live strips and the tracks
    int sw = (rowW - (inRow0 - 1) * gap - sep) / inRow0;
    int lw = sw;
    if (!portrait && ntracks > 4 && nLiveVis > 0) { lw = juce::jmin(sw, juce::jmax(118, (int) (rowW * 0.088f))); sw = (rowW - nLiveVis * lw - (inRow0 - 1) * gap - sep) / juce::jmax(1, inRow0 - nLiveVis); }
    sw = juce::jlimit(86, 330, sw); lw = juce::jlimit(86, 330, lw);
    int idx = 0, x = 18, row = 0;
    for (int s = 0; s < NSTRIP; ++s) {
        bool vis = s < 4 ? (s < host.nLive || (host.synthInto > 0 && s == host.synthInto)) : (s - 4) < ntn;
        strips[s]->setVisible(vis);
        if (!vis) continue;
        int r2 = idx / perRow;
        if (r2 != row) { row = r2; x = 18; }
        if (s >= 4 && idx == nLiveVis && r2 == 0) x += sep;
        int wv = s < 4 ? lw : sw;
        auto rr = juce::Rectangle<int>(x, top + row * (stripH + 8), wv, stripH);
        if (strips[s]->getBounds() != rr) strips[s]->setBounds(rr);
        x += wv + gap; ++idx;
    }
    auto mr = juce::Rectangle<int>(W - 18 - mw, top, mw, stripH);
    if (masterView && masterView->getBounds() != mr) masterView->setBounds(mr);
    auto mapr = juce::Rectangle<int>(18, top + stripRows * (stripH + 8), W - 36, mapH);
    if (mapr != mapR) { mapR = mapr; if (fxMap) fxMap->setBounds(mapR); if (!inLayout) layoutCards(); }
}

// the open family's cards: as many as fit at a good size (knobs for fingers), more = pages (the arrows on the map)
void Canvas::layoutCards() {
    if (!inLayout) { inLayout = true; layoutStrips(); inLayout = false; }
    for (auto* p : pagePills) p->setVisible(false);
    auto vis = familyCards(fxFamily);
    if (vis.isEmpty()) for (int f = 0; f < NFAM; ++f) if (!familyCards(f).isEmpty()) { fxFamily = f; vis = familyCards(f); break; }
    int top = mapR.getBottom() + 8, gap = 10;
    int availW = W - 36, availH = H - top - 10;
    // rows of cards (as many as their smallest good width allows); a tall screen shows two rows; more = pages
    std::vector<std::pair<int, int>> rowsV;      // first, count
    for (int i = 0; i < vis.size();) {
        int used = 0, n = 0;
        while (i + n < vis.size()) { int mwid = cards[vis[i + n]]->minWidth(); if (n > 0 && used + gap + mwid > availW) break; used += (n ? gap : 0) + mwid; ++n; }
        rowsV.push_back({ i, n }); i += n;
    }
    if (rowsV.empty()) rowsV.push_back({ 0, 0 });
    int maxRows = availH >= 480 && rowsV.size() > 1 ? 2 : 1;
    numCardPages = ((int) rowsV.size() + maxRows - 1) / maxRows;
    cardPage = juce::jlimit(0, numCardPages - 1, cardPage);
    cardsPerPage = juce::jmax(1, rowsV[0].second * maxRows);
    for (auto* c : cards) c->setVisible(false);
    int r0 = cardPage * maxRows, nr = juce::jmin(maxRows, (int) rowsV.size() - r0);
    int rh = (availH - (nr - 1) * gap) / juce::jmax(1, nr);
    for (int ri = 0; ri < nr; ++ri) {
        auto pg = rowsV[(size_t) (r0 + ri)];
        float wsum = 0; for (int k = 0; k < pg.second; ++k) wsum += cards[vis[pg.first + k]]->weight();
        std::vector<int> ws; int total = 0;
        for (int k = 0; k < pg.second; ++k) {
            auto* cv = cards[vis[pg.first + k]];
            int w = (int) ((availW - (pg.second - 1) * gap) * cv->weight() / juce::jmax(0.01f, wsum));
            w = juce::jmin(w, cv->cols() * 150 + 60); ws.push_back(w); total += w + (k ? gap : 0);
        }
        int x = 18 + (availW - total) / 2;
        for (int k = 0; k < pg.second; ++k) {
            auto* cv = cards[vis[pg.first + k]];
            cv->setTransform({});
            cv->setBounds(x, top + ri * (rh + gap), ws[(size_t) k], rh);
            cv->setVisible(true); x += ws[(size_t) k] + gap;
        }
    }
    if (fxMap) fxMap->repaint();
    repaint();
}

void Canvas::themeChanged() {
    if (auto* top = getTopLevelComponent()) { top->repaint(); if (auto* w = dynamic_cast<juce::DocumentWindow*>(top)) w->setBackgroundColour(Theme::bg()); }
    sendLookAndFeelChange(); repaint();
}
void Canvas::openSetup(bool show) { setup->setVisible(show); if (show) setup->toFront(true); }
void Canvas::openMic(bool show) { if (auto* mc = findParentComponentOfClass<MainComponent>()) mc->showMic(show); }
void Canvas::openLook(bool show) { look->setVisible(show); if (show) look->toFront(true); }
void Canvas::openSpatial(bool show) { spatial->setVisible(show); if (show) spatial->toFront(true); }
void Canvas::openOutputs() { spatial->showTab(0); openSpatial(true); }
void Canvas::openKeyboard(bool show) { keyboardWin->setVisible(show); if (show) keyboardWin->toFront(true); }
void Canvas::keyboardSearch(const juce::String& q) { openKeyboard(true); keyboardWin->setSearch(q); }
void Canvas::openModular(bool show) { modularWin->setVisible(show); if (show) modularWin->toFront(true); }
void Canvas::synthPage(int p) { synthWin->setPage(p); if (p == 3) { synthWin->keysMode = 1; } }
void Canvas::synthKeys(int m, bool edit) { synthWin->setKeysMode(m, edit); }
void Canvas::synthPreset(int i) { synthWin->loadPreset(i); }
int Canvas::synthPresetIndex() const { return synthWin->presetIndex; }
void Canvas::openSynth(bool show) { synthWin->setVisible(show); if (show) synthWin->toFront(true); }

void Canvas::doSave(bool askName) {
    if (!askName && host.songFolder.isDirectory()) { host.saveSong(host.songFolder); return; }
   #if JUCE_IOS
    auto* w = new juce::AlertWindow("Save the song", "Its name:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", host.songFolder.isDirectory() ? host.songFolder.getFileName() : juce::String("my song"));
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        auto n = juce::File::createLegalFileName(w->getTextEditorContents("name").trim());
        if (r == 1 && n.isNotEmpty()) host.saveSong(songsDir().getChildFile(n)); }), true);
   #else
    chooseFile("Save the song: choose a name (a folder with that name is made)", "*", true,
               [this](juce::File f) { host.saveSong(f.withFileExtension("")); });
   #endif
}

// keys and page-turner pedals (they send arrow / page keys): the hands stay on the instrument
bool Canvas::keyPressed(const juce::KeyPress& k) {
    const auto code = k.getKeyCode(); const bool cmd = k.getModifiers().isCommandDown(), shift = k.getModifiers().isShiftDown();
    // app shortcuts work everywhere
    if (code == juce::KeyPress::F1Key) { command(cmdHelp); return true; }
    if (cmd && code == 'N') { command(cmdNewSong); return true; }
    if (cmd && code == 'E') { command(cmdExport); return true; }
    if (cmd && code == 'T') { command(cmdStudio); return true; }
    if (cmd && code == 'R') { command(cmdRecordShow); return true; }
    if (cmd && shift && code == 'L') { command(cmdToggleLight); return true; }
    if (cmd && code == 'L') { command(cmdMidiLearn); return true; }
    if (cmd && shift && code == 'M') { command(cmdAutoMix); return true; }
    if (cmd && code == 'M') { command(cmdMinimise); return true; }
    if (cmd && code == ',') { command(cmdSetup); return true; }
    if (cmd && k.getModifiers().isCtrlDown() && code == 'F') { command(cmdFullScreen); return true; }
    if (anyOverlay()) return false;
    if (cmd && code == 'S') { doSave(k.getModifiers().isShiftDown()); return true; }
    if (cmd && code == 'O') { doOpen(); return true; }
    if (cmd && code == 'Z') { host.doAction(AudioHost::ActUndo); return true; }
    if (cmd) return false;
    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::downKey) { host.doAction(AudioHost::ActPlay); return true; }
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::pageDownKey || code == juce::KeyPress::returnKey) { host.doAction(AudioHost::ActRec); return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::backspaceKey) { host.doAction(AudioHost::ActUndo); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::tabKey) { host.doAction(AudioHost::ActScene); return true; }
    if (code >= '1' && code <= '4') { model().press("st_looper", "scene", 8, code - '0'); return true; }
    if (k.getTextCharacter() == 'k' || k.getTextCharacter() == 'K') { openSynth(true); return true; }
    return false;
}

void Canvas::timerCallback() {
    model().tick();
    host.pollImport();
    if (!isVisible()) return;                                        // the phone view is showing
    if (++menuTick % 30 == 0 && menu) menu->menuItemsChanged();      // the Mac menu bar follows (recent songs, ticks)
    if (host.sigSettle > 0 && --host.sigSettle == 0) host.savedSignature = host.loopsSignature();
    layoutStrips();
    for (auto* s : strips) if (s->isVisible()) s->update();
    if (masterView) masterView->update();
    for (auto* c : cards) if (c->isVisible()) c->update();
    if (fxMap && menuTick % 6 == 0) fxMap->repaint();
    if (!inputSeen && engine().peak("live_in1") > 0.003f) inputSeen = true;
    host.pollImport();
    for (auto* o : owned) o->repaint();
    if (isShowing() && !setup->isVisible() && !synthWin->isVisible() && !look->isVisible() && juce::Component::getCurrentlyFocusedComponent() == nullptr) grabKeyboardFocus();
    repaint(0, 0, W, headerBottom);
}

void Canvas::resized() {}

void Canvas::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    // a soft light from the top (the new design)
    g.setGradientFill(juce::ColourGradient(Theme::bg().brighter(Theme::lightMode() ? 0.025f : 0.10f), W * 0.5f, 0.0f, Theme::bg(), W * 0.5f, H * 0.75f, true));
    g.fillRect(getLocalBounds());
    paintTop(g);
    paintScenes(g);
}
void Canvas::paintTop(juce::Graphics& g) {
    for (int k : { 1, 2, 3, 4 }) drawPanel(g, grpR[k].toFloat(), 16.0f);
    auto at = [&](int grp, int x, int y, int w, int h) { return juce::Rectangle<int>(grpR[grp].getX() + x, grpR[grp].getY() + y, w, h); };
    // ARCO, is the sound on, is the input heard
    label(g, "ARCO", at(0, 0, 2, 90, 28), 21.0f, Theme::txt(), 2);
    label(g, U("▾"), at(0, 74, 6, 20, 22), 9.0f, Theme::sub(), 0);
    bool running = engine().cpu.load() > 0.0001f && host.devices.getCurrentAudioDevice() != nullptr;
    auto stc = running ? Theme::green() : Theme::amber();
    g.setColour(stc); g.fillEllipse(at(0, 0, 36, 7, 7).toFloat());
    label(g, running ? U("engine running") : U("audio off · press ⏻"), at(0, 11, 31, 125, 16), 8.0f, Theme::ink(stc), 1);
    if (model().get("prac") >= 99.5) label(g, inputSeen ? U("input 1 → looper ✓") : "input 1: waiting", at(0, 0, 45, 132, 14), 7.5f, inputSeen ? Theme::ink(Theme::green()) : Theme::sub());
    // the position screen
    auto scr = at(1, 240, 6, 154, 48);
    g.setColour(Theme::scr()); g.fillRoundedRectangle(scr.toFloat(), 12.0f);
    const char* L[3] = { "BAR", "BEAT", "LOOP" };
    for (int i = 0; i < 3; ++i) label(g, L[i], at(1, 244 + i * 50, 38, 50, 12), 6.8f, Theme::sub(), 2, juce::Justification::centred);
    float prac = (float) model().get("prac");
    if (prac < 99.5f) label(g, "PRACTICE " + juce::String((int) prac) + " %", at(0, 0, 45, 132, 14), 7.5f, Theme::amber(), 2);
    // tempo labels
    label(g, "BPM", at(2, 138, 6, 74, 14), 7.0f, Theme::sub(), 2, juce::Justification::centred);
    label(g, "BEATS", at(2, 216, 6, 54, 14), 7.0f, Theme::sub(), 2, juce::Justification::centred);
    label(g, "BARS", at(2, 272, 6, 54, 14), 7.0f, Theme::sub(), 2, juce::Justification::centred);
    // the song
    label(g, "SONG", at(3, 14, 8, 100, 12), 7.0f, Theme::sub(), 2);
    label(g, host.songFolder.isDirectory() ? host.songFolder.getFileName() : juce::String("new song"), at(3, 14, 20, 108, 20), 12.0f, Theme::txt(), 2);
    label(g, host.status, at(3, 14, 40, 108, 13), 7.0f, Theme::sub());
}
void Canvas::paintScenes(juce::Graphics& g) {
    for (int k : { 5, 6, 7 }) drawPanel(g, grpR[k].toFloat(), 14.0f);
    auto at = [&](int grp, int x, int y, int w, int h) { return juce::Rectangle<int>(grpR[grp].getX() + x, grpR[grp].getY() + y, w, h); };
    label(g, "TRACKS", at(6, 14, 0, 56, 44), 7.5f, Theme::sub(), 2);
    g.setColour(Theme::line()); g.fillRect(at(6, 132, 11, 1, 22));
    int s = model().selected;
    label(g, U("SEND ▸"), at(6, 142, 0, 46, 44), 7.5f, Theme::sub(), 2);
    label(g, s < 4 ? "LIVE " + juce::String(s + 1) : "TRACK " + juce::String(s - 3), at(6, 188, 0, 60, 44), 10.5f, Theme::txt(), 2);
    if (juce::Time::getMillisecondCounterHiRes() < flashUntil) {
        auto r = grpR[8].reduced(0, 4).toFloat();
        g.setColour(Theme::amber()); g.fillRoundedRectangle(r, 10.0f);
        label(g, flash, r.toNearestInt(), 10.0f, Theme::textOn(Theme::amber()), 2, juce::Justification::centred);
    }
}

} // namespace arco
