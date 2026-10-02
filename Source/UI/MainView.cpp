#include "MainView.h"
#include "Panels.h"
#include "SynthPanel.h"

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
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}

// ===================================================================== one strip
class StripView : public juce::Component {
public:
    StripView(Canvas& cv, int s_) : c(cv), s(s_) {
        auto& m = c.model();
        auto add = [this](juce::Component* x, juce::Rectangle<int> r) { kids.add(x); addAndMakeVisible(x); x->setBounds(r); return x; };
        // header: select
        head = new Pill(name(), [this] { return c.model().selected == s ? Theme::sel() : Theme::card2(); }, 9.5f, 8.0f);
        head->textColour = [] { return Theme::txt(); };
        head->onClick = [this] { c.model().select(s); };
        add(head, { 4, 4, 104, 22 });
        wave = new WaveScreen(c.engine(), [this]() -> SharedBuffer* {
                if (s < 4) return &c.engine().buffer("st_wavel");
                int sc = (int) c.snap("lscene"); const char* L[4] = { "A", "B", "C", "D" };
                return &c.engine().buffer(std::string("st_wave") + L[juce::jlimit(0, 3, sc)]); },
            [this] { return s < 4 ? s : s - 4; }, [this] { return Theme::strip(s); });
        add(wave, { 6, 30, 100, 42 });
        if (s >= 4) {
            int k = s - 4;
            wave->onDrop = [this, k](const juce::File& f) { c.host.importFile(f, k + 1); };
            auto* file = new Pill("FILE", [] { return Theme::btn(); }, 6.5f, 6.0f);
            file->onClick = [this, k] { c.chooseFile("Import an audio file into TRACK " + juce::String(k + 1), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.m4a", false,
                                                  [this, k](juce::File f) { c.host.importFile(f, k + 1); }); };
            add(file, { 76, 76, 30, 18 });
            auto btn = [&](juce::String t, ColourFn col, float fs, juce::Rectangle<int> r, std::string cmd) {
                auto* p = new Pill(t, std::move(col), fs, 7.0f);
                p->onClick = [this, cmd, k] { c.model().press("st_looper", cmd + std::to_string(k + 1)); };
                add(p, r); return p; };
            btn("REC", [] { return Theme::red(); }, 9.5f, { 6, 98, 49, 24 }, "rec");
            btn("CLR", [] { return Theme::btn(); }, 8.5f, { 57, 98, 49, 24 }, "clr");
            btn(U("▶ ■"), [] { return Theme::btn(); }, 9.0f, { 6, 126, 49, 22 }, "tog");
            auto* sp = btn("1x", [] { return Theme::btn(); }, 8.5f, { 57, 126, 49, 22 }, "spd");
            sp->dynText = [this, k] { const char* S[4] = { "1x", "½x", "2x", "◀" }; int v = (int) c.snap("spdk"); return U(S[(v >> (2 * k)) & 3]); };
            auto* inb = new ValueBox([&m, k] { return m.get("src" + std::to_string(k + 1)); }, [&m, k](double v) { m.set("src" + std::to_string(k + 1), v); },
                                     1, 4, 0, [] { return Theme::strip(0); });
            inb->fsize = 10.0f; add(inb, { 22, 150, 30, 18 });
        } else if (s == 0) {
            // the note and the tuner are painted
        }
        // pan, CUE, ROOM, M, S, fader, meter
        auto* pan = new Knob([&m, this] { return m.get("pan" + std::to_string(s)); }, [&m, this](double v) { m.set("pan" + std::to_string(s), v); },
                             -50, 50, 0, [] { return Theme::sub(); });
        add(pan, { 8, 175, 30, 30 });
        auto tog = [&](const char* t, const char* key, ColourFn on, juce::Rectangle<int> r) {
            std::string id = key + std::to_string(s);
            auto* p = new Pill(t, [this, id, on] { return c.model().get(id) > 0.5 ? on() : Theme::btn(); }, 8.0f, 6.0f);
            p->onClick = [this, id] { c.model().set(id, c.model().get(id) > 0.5 ? 0 : 1); };
            add(p, r); };
        tog("CUE", "cue", [] { return Theme::amber(); }, { 6, 220, 50, 20 });
        tog("ROOM", "room", [] { return Theme::green(); }, { 6, 244, 50, 20 });
        tog("M", "mute", [] { return Theme::red(); }, { 6, 268, 24, 20 });
        tog("S", "solo", [] { return Theme::blue(); }, { 32, 268, 24, 20 });
        auto* fd = new Fader([&m, this] { return m.get("vol" + std::to_string(s)); }, [&m, this](double v) { m.set("vol" + std::to_string(s), v); }, 0, 100, 71);
        add(fd, { 66, 177, 18, 112 });
        meter = new Meter([this] { return c.engine().peak("ch" + std::to_string(s)); }, true);
        add(meter, { 88, 177, 6, 112 });
        if (s < 4) { inMeter = new Meter([this] { return c.engine().peak("live_in" + std::to_string(s + 1)); }); add(inMeter, { 6, s == 0 ? 116 : 80, 100, 6 }); }
    }
    ~StripView() override { kids.clear(true); }
    juce::String name() const { return s < 4 ? "LIVE " + juce::String(s + 1) : "TRACK " + juce::String(s - 3); }
    void update() {
        if (meter) meter->update(); if (inMeter) inMeter->update();
        repaint();
        for (auto* k : kids) k->repaint();
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(Theme::card()); g.fillRoundedRectangle(r, 12.0f);
        g.setColour(Theme::strip(s)); g.fillEllipse(10, 11, 8, 8);
        auto& m = c.model();
        if (s >= 4) {
            int k = s - 4;
            float pack = c.snap(k < 4 ? "statA" : "statB");
            int st = ((int) pack >> (4 * (k % 4))) & 15;
            g.setColour(stateColour(st)); g.fillRoundedRectangle(6, 76, 68, 18, 6.0f);
            label(g, STATE_NAMES[juce::jlimit(0, 10, st)], { 6, 77, 68, 16 }, 7.5f, juce::Colours::white, 2, juce::Justification::centred);
            label(g, "IN", { 6, 151, 18, 16 }, 8.0f, Theme::sub(), 2);
        } else {
            if (s == 0) {
                float p = c.snap("pitch");
                static const char* N[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
                juce::String note = "-", oct;
                if (p > 20) { int n = (int) std::floor(p + 0.5); note = N[n % 12]; oct = juce::String(n / 12 - 1); }
                label(g, note, { 6, 76, 34, 22 }, 14.0f, Theme::fx("halo"), 2);
                label(g, oct, { 34, 80, 20, 18 }, 9.0f, Theme::fx("halo"), 2);
                float tp = c.snap("tn_pitch");
                if (tp > 0) {
                    float cents = (tp - std::floor(tp + 0.5f)) * 100.0f;
                    label(g, juce::String(cents >= 0 ? "+" : "") + juce::String((int) std::round(cents)) + U("¢"), { 62, 80, 44, 16 }, 8.5f, Theme::green(), 2,
                          juce::Justification::centredRight);
                    g.setColour(Theme::card2()); g.fillRoundedRectangle(6, 102, 100, 7, 3.5f);
                    float x = 6 + 50 + juce::jlimit(-50.0f, 50.0f, cents);
                    g.setColour(Theme::green()); g.fillRoundedRectangle(x - 3, 101, 6, 9, 3.0f);
                }
            }
            int y = s == 0 ? 125 : 89;
            bool syn = c.host.synthInto > 0 && c.host.synthInto == s;     // LIVE 2-4: synth plays here
            label(g, syn ? juce::String(U("♪ from the SYNTH")) : "interface input " + juce::String(c.host.inputCh[s]), { 6, y, 100, 14 }, 7.5f,
                  syn ? Theme::violet() : Theme::sub());
        }
        // the effects this strip uses
        for (int d = 0; d < (int) DOTS.size(); ++d) {
            auto id = Model::stripParam(s, DOTS[(size_t) d]);
            bool on = !id.empty() && m.get(id) > 0.5;
            g.setColour(on ? Theme::fx(DOTS[(size_t) d]) : Theme::card2());
            g.fillRoundedRectangle(58.0f + (d % 8) * 6, 151.0f + (d / 8) * 7, 5, 5, 2);
        }
        label(g, "PAN", { 6, 203, 34, 12 }, 7.0f, Theme::sub(), 0, juce::Justification::centred);
    }
private:
    Canvas& c; int s;
    juce::OwnedArray<juce::Component> kids;
    Pill* head = nullptr; WaveScreen* wave = nullptr; Meter* meter = nullptr; Meter* inMeter = nullptr;
};

// ===================================================================== one effect card
class CardView : public juce::Component {
public:
    CardView(Canvas& cv, const CardDef& d) : c(cv), def(d) {
        auto& m = c.model();
        std::string key = d.key;
        std::map<std::string, std::string> scopeBus = { { "eq", "selsig" }, { "drv", "selsig" }, { "trm", "selsig" }, { "phs", "selsig" }, { "swl", "selsig" },
            { "tun", "live1" }, { "cho", "choL" }, { "dly", "dlyL" }, { "rev", "revL" }, { "fxb", "fxbL" }, { "halo", "haloL" }, { "res", "resL" },
            { "grn", "grnL" }, { "frz", "frzL" }, { "spc", "to_spc" }, { "orb", "to_orb" } };
        if (scopeBus.count(key)) { auto* sc = new MiniScope(&c.engine().ring(scopeBus[key]), [key] { return Theme::fx(key); }); kids.add(sc); addAndMakeVisible(sc); sc->setBounds(8, 27, 135, 32); }
        int ncol = d.cols; float cw = 135.0f / ncol; int ds = ncol == 3 ? 32 : 28;
        for (int j = 0; j < (int) d.knobs.size(); ++j) {
            int r = j / ncol, cc = j % ncol;
            int kx = (int) (8 + cc * cw + (cw - ds) / 2), ky = 66 + r * 48;
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
            kids.add(kn); addAndMakeVisible(kn); kn->setBounds(kx, ky, ds, ds);
            labels.push_back({ U(d.knobs[(size_t) j].label), juce::Rectangle<int>((int) (8 + cc * cw - 4), ky + ds + 1, (int) cw + 8, 13), id == "SEND" });
        }
        if (key == "frz") {
            auto* p = new Pill("HOLD", [&m] { return m.get("frz_on") > 0.5 ? Theme::fx("frz") : Theme::btn(); }, 7.5f, 6.0f);
            p->onClick = [&m] { m.set("frz_on", m.get("frz_on") > 0.5 ? 0 : 1); };
            kids.add(p); addAndMakeVisible(p); p->setBounds(100, 6, 44, 17);
        }
        if (key == "drm") {
            auto* p = new Pill("ON", [&m] { return m.get("drm_on") > 0.5 ? Theme::green() : Theme::btn(); }, 7.5f, 6.0f);
            p->onClick = [&m] { m.set("drm_on", m.get("drm_on") > 0.5 ? 0 : 1); };
            kids.add(p); addAndMakeVisible(p); p->setBounds(108, 6, 36, 17);
        }
        if (key == "shd") {
            auto nm = shadowNames();
            for (int v = 0; v < 12; ++v) {
                std::string id = "shd_v" + std::to_string(v);
                auto* p = new Pill(U(nm[(size_t) v].c_str()), [&m, id] { return m.get(id) > 0.5 ? Theme::c(Theme::rgb(0.43f, 0.51f, 0.92f)) : Theme::btn(); }, 6.5f, 4.0f);
                p->onClick = [&m, id] { m.set(id, m.get(id) > 0.5 ? 0 : 1); };
                kids.add(p); addAndMakeVisible(p); p->setBounds(8 + (int) ((v % 6) * 22.5f), 27 + (v / 6) * 17, 21, 15);
            }
        }
    }
    ~CardView() override { kids.clear(true); }
    void update() { repaint(); for (auto* k : kids) k->repaint(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        std::string key = def.key;
        g.setColour(Theme::card()); g.fillRoundedRectangle(r, 12.0f);
        int fx = (int) c.snap("fxused");
        static const std::map<std::string, int> bit = { { "eq", 0 }, { "drv", 1 }, { "trm", 2 }, { "phs", 3 }, { "swl", 4 }, { "cho", 5 }, { "dly", 6 },
            { "rev", 7 }, { "fxb", 8 }, { "shd", 9 }, { "orb", 10 }, { "res", 12 }, { "grn", 13 }, { "frz", 14 }, { "spc", 15 } };
        bool used = bit.count(key) ? ((fx >> bit.at(key)) & 1) : false;
        if (key == "halo") used = c.snap("halo_used") > 0.5f;
        if (key == "tun") used = c.model().get("tn_amt") > 0;
        if (key == "drm") used = c.model().get("drm_on") > 0.5;
        g.setColour(used ? Theme::fx(key) : Theme::card2()); g.fillEllipse(10, 11, 8, 8);
        label(g, U(def.name), { 22, 5, 100, 18 }, 10.5f, Theme::txt(), 2);
        label(g, U(def.desc), { 10, 214, 131, 14 }, 7.0f, Theme::sub());
        for (auto& l : labels) label(g, l.text, l.r, 7.0f, l.send ? Theme::fx(key) : Theme::sub(), l.send ? 2 : 0, juce::Justification::centred);
        auto& m = c.model();
        auto right = [&](const juce::String& t, juce::Rectangle<int> rr) { label(g, t, rr, 7.5f, Theme::txt(), 2, juce::Justification::centredRight); };
        auto pick = [](double v, int n, const char* const* names) { return juce::String(names[juce::jlimit(0, n - 1, (int) (v / 100.0 * (n - 0.01)))]); };
        static const char* DLY[8] = { "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2", "1/1" };
        static const char* TRM[6] = { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" };
        static const char* REV[6] = { "1/8", "1/4", "1/4.", "1/2", "1/2.", "1/1" };
        static const char* FXB[6] = { "LO-FI", "RING", "FILTER", "STUTTER", "METAL", "OCTAVE" };
        static const char* PTH[3] = { "CIRCLE", "SWING", "WANDER" };
        static const char* STY[8] = { "POP", "FUNK", "HIP-HOP", "HOUSE", "BOSSA", "JAZZ", "REGGAE", "AMBIENT" };
        static const char* KIT[4] = { "ACOUSTIC", "808", "909", "LO-FI" };
        static const char* SCL[2] = { "CHROMATIC", "IN-KEY" };
        if (key == "dly") right(pick(m.get("dly_time"), 8, DLY), { 96, 6, 48, 16 });
        if (key == "trm") right(pick(m.get("trm_rate"), 6, TRM), { 96, 6, 48, 16 });
        if (key == "rev") right(pick(m.get("rev_time"), 6, REV), { 96, 6, 48, 16 });
        if (key == "fxb") right(pick(m.get("fxb_mode"), 6, FXB), { 84, 6, 60, 16 });
        if (key == "orb") {
            right(pick(m.get("orb_path"), 3, PTH), { 84, 6, 60, 16 });
            static const char* DIR[8] = { "FRONT", "FRONT-R", "RIGHT", "BACK-R", "BEHIND", "BACK-L", "LEFT", "FRONT-L" };
            static const char* DIS[4] = { "close", "near", "far", "distant" };
            float pos = c.snap("orb_pos"), dm = c.snap("orb_dm");
            int di = ((int) ((pos + 0.0625f) * 8)) % 8, dd = (dm > 1.2f) + (dm > 3.5f) + (dm > 8.0f);
            right(juce::String(DIR[juce::jlimit(0, 7, di)]) + U(" · ") + DIS[dd], { 64, 214, 80, 14 });
        }
        if (key == "tun") {
            right(pick(m.get("tn_scale"), 2, SCL), { 84, 6, 60, 16 });
            right(juce::String((int) std::round(c.snap("tn_corr") * 100)) + U("¢"), { 100, 214, 42, 14 });
        }
        if (key == "drm") {
            g.setColour(Theme::scr()); g.fillRoundedRectangle(8, 27, 135, 32, 5.0f);
            label(g, pick(m.get("drm_style"), 8, STY), { 10, 35, 64, 16 }, 9.0f, Theme::txt(), 2, juce::Justification::centred);
            label(g, pick(m.get("drm_kit"), 4, KIT), { 76, 35, 64, 16 }, 9.0f, Theme::txt(), 2, juce::Justification::centred);
        }
        if (key == "halo") {
            static const char* N[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
            int k = (int) c.snap("halo_key");
            right(k >= 0 ? juce::String("KEY ") + N[k % 12] + U(" · ") + N[(k + 9) % 12] + "m" : juce::String(U("KEY –")), { 64, 6, 80, 16 });
            label(g, juce::String((int) c.snap("halo_n")) + " voices", { 90, 214, 52, 14 }, 7.5f, Theme::sub(), 0, juce::Justification::centredRight);
            if (sendKnob) sendKnob->enabled = !(c.model().selected >= 1 && c.model().selected <= 3);
        }
    }
private:
    Canvas& c; const CardDef& def;
    juce::OwnedArray<juce::Component> kids;
    struct L { juce::String text; juce::Rectangle<int> r; bool send; };
    std::vector<L> labels;
    Knob* sendKnob = nullptr;
};

// ===================================================================== the canvas
Canvas::Canvas(AudioHost& h) : host(h) {
    setOpaque(true);
    buildTopBar();
    buildScenes();
    for (int s = 0; s < NSTRIP; ++s) { auto* sv = strips.add(new StripView(*this, s)); addAndMakeVisible(sv); }
    auto& ct = cardTable();
    for (int i = 0; i < (int) ct.size(); ++i) {
        auto* cv = cards.add(new CardView(*this, ct[(size_t) i]));
        addAndMakeVisible(cv); cv->setBounds(16 + (i % 9) * 157, 406 + (i / 9) * 238, 151, 232);
    }
    setup = std::make_unique<SetupPanel>(*this);
    addChildComponent(setup.get()); setup->setBounds(0, 0, 1440, 880);
    synthWin = std::make_unique<SynthPanel>(*this);
    addChildComponent(synthWin.get()); synthWin->setBounds(0, 0, 1440, 880);
    setWantsKeyboardFocus(true);
    host.onAction = [this](int a) { const char* W[4] = { "REC", U("PLAY / STOP").toRawUTF8(), "UNDO", "NEXT SCENE" };
        flash = a == 1 ? U("PLAY / STOP") : juce::String(W[a]); flashUntil = juce::Time::getMillisecondCounterHiRes() + 900; };
    layoutStrips();
    startTimerHz(30);
}
Canvas::~Canvas() { stopTimer(); host.onAction = nullptr; }

void Canvas::chooseFile(const juce::String& title, const juce::String& pattern, bool save, std::function<void(juce::File)> done, bool dirs) {
    chooser = std::make_unique<juce::FileChooser>(title, juce::File::getSpecialLocation(juce::File::userMusicDirectory), pattern);
    int flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting) : juce::FileBrowserComponent::openMode;
    flags |= dirs ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles;
    if (dirs && !save) flags |= juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync(flags, [done](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) done(f); });
}

void Canvas::buildTopBar() {
    auto& m = model();
    // transport
    auto* play = add<Pill>({ 204, 15, 46, 38 }, U("▶"), [this] { int t = (int) snap("transport"); return t == 1 ? Theme::green() : (t == 2 ? Theme::amber() : Theme::btn()); }, 15.0f, 9.0f);
    play->onClick = [this] { model().press("st_looper", "playall"); };
    auto* stop = add<Pill>({ 254, 15, 46, 38 }, U("■"), [] { return Theme::btn(); }, 13.0f, 9.0f);
    stop->onClick = [this] { model().press("st_looper", "stopall"); };
    auto* undo = add<Pill>({ 304, 15, 46, 38 }, "UNDO", [this] { return snap("undoav") > 0.5f ? Theme::amber() : Theme::btn(); }, 8.5f, 9.0f);
    undo->onClick = [this] { model().press("st_looper", "undo"); };
    auto* tap = add<Pill>({ 354, 15, 36, 18 }, "TAP", [] { return Theme::btn(); }, 7.0f, 6.0f);
    tap->onClick = [this] {
        static double last = 0; double now = juce::Time::getMillisecondCounterHiRes(); double d = now - last; last = now;
        if (d > 200 && d < 2000) model().set("bpm", std::round(600000.0 / d) / 10.0); };
    auto* rst = add<Pill>({ 354, 35, 36, 18 }, "RESET", [] { return Theme::c(Theme::rgb(0.17f, 0.18f, 0.215f)); }, 6.0f, 6.0f);
    rst->onClick = [this] {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Reset everything?", "All loops of all scenes are cleared.", "Reset", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this](int r) { if (r == 1) model().press("st_looper", "rstall"); })); };
    auto* bpm = add<ValueBox>({ 398, 28, 54, 24 }, [this, &m] { return snap("smask") > 0 ? std::round(snap("ebpm") * 10) / 10 : m.get("bpm"); },
                              [&m](double v) { m.set("bpm", v); }, 30, 300, 1, [] { return Theme::red(); });
    (void) bpm;
    add<ValueBox>({ 458, 28, 36, 24 }, [&m] { return m.get("beats"); }, [&m](double v) { m.set("beats", v); }, 1, 16, 0, [] { return Theme::sub(); });
    add<ValueBox>({ 498, 28, 36, 24 }, [&m] { return m.get("bars"); }, [&m](double v) { m.set("bars", v); }, 0, 32, 0, [] { return Theme::sub(); });
    auto tg = [&](juce::Rectangle<int> r, const char* id, ColourFn on) {
        auto* p = add<Pill>(r, "", [&m, id, on] { return m.get(id) > 0.5 ? on() : Theme::card2(); }, 8.0f, 5.0f);
        p->onClick = [&m, id] { m.set(id, m.get(id) > 0.5 ? 0 : 1); }; };
    tg({ 544, 30, 20, 20 }, "countin", [] { return Theme::amber(); });
    tg({ 584, 30, 20, 20 }, "clickall", [] { return Theme::red(); });
    add<Knob>({ 616, 25, 28, 28 }, [&m] { return m.get("clickvol"); }, [&m](double v) { m.set("clickvol", v); }, 0, 100, 60, [] { return Theme::red(); });
    auto ro = [&](juce::Rectangle<int> r, const char* bus) {
        auto* v = add<ValueBox>(r, [this, bus] { return (double) snap(bus); }, [](double) {}, 0, 999, 0); v->readOnly = true; v->flat = true; v->fsize = 13.0f; };
    ro({ 654, 27, 34, 24 }, "barno"); ro({ 690, 27, 34, 24 }, "beatno"); ro({ 726, 27, 36, 24 }, "loopbars");
    // song
    auto* save = add<Pill>({ 978, 13, 32, 20 }, "SAVE", [] { return Theme::c(Theme::rgb(0.20f, 0.36f, 0.60f)); }, 7.5f, 6.0f);
    auto saveAs = [this] { doSave(true); };
    save->onClick = [this] { doSave(false); };
    auto* as = add<Pill>({ 1012, 13, 18, 20 }, U("AS…"), [] { return Theme::c(Theme::rgb(0.20f, 0.36f, 0.60f)); }, 6.0f, 6.0f);
    as->onClick = saveAs;
    auto* open = add<Pill>({ 978, 36, 52, 20 }, "OPEN", [] { return Theme::btn(); }, 8.5f, 6.0f);
    open->onClick = [this] { chooseFile("Open a song (its folder, or its -settings.json)", "*-settings.json", false, [this](juce::File f) { host.openSong(f); }, true); };
    // record the show
    auto* rec = add<Pill>({ 1052, 14, 58, 26 }, U("● REC"), [this] { return host.isRecording() ? Theme::red() : Theme::off(); }, 9.5f, 8.0f);
    rec->dynText = [this] { return host.isRecording() ? juce::String(U("■ STOP")) : juce::String(U("● REC")); };
    rec->onClick = [this] {
        if (host.isRecording()) host.stopRecording();
        else chooseFile("Record the show to", "*.wav", true, [this](juce::File f) { host.startRecording(f.withFileExtension("wav")); }); };
    // master + glue
    add<Knob>({ 1132, 25, 30, 30 }, [&m] { return m.get("master"); }, [&m](double v) { m.set("master", v); }, 0, 100, 75, [] { return Theme::txt(); });
    add<Knob>({ 1172, 25, 30, 30 }, [&m] { return m.get("glue"); }, [&m](double v) { m.set("glue", v); }, 0, 100, 25, [] { return Theme::amber(); });
    meters.add(add<Meter>({ 1212, 18, 7, 36 }, [this] { return engine().peak("glue_gr"); }, true));
    meters.add(add<Meter>({ 1270, 16, 42, 6 }, [this] { return engine().peak("live_in1"); }));
    meters.add(add<Meter>({ 1270, 32, 42, 5 }, [this] { return engine().peak("mainL"); }));
    meters.add(add<Meter>({ 1270, 40, 42, 5 }, [this] { return engine().peak("mainR"); }));
    // windows
    auto* syn = add<Pill>({ 1326, 9, 50, 22 }, "SYNTH", [] { return Theme::c(Theme::rgb(0.40f, 0.30f, 0.70f)); }, 8.5f, 7.0f);
    syn->onClick = [this] { openSynth(true); };
    auto* room = add<Pill>({ 1380, 9, 48, 22 }, "ROOM", [] { return Theme::off(); }, 8.5f, 7.0f);
    room->onClick = [this] { openSetup(true); };
    auto* st = add<Pill>({ 1326, 35, 42, 20 }, "SETUP", [] { return Theme::off(); }, 7.5f, 7.0f);
    st->onClick = [this] { openSetup(true); };
    auto* lt = add<Pill>({ 1371, 35, 30, 20 }, "LIGHT", [] { return Theme::off(); }, 6.0f, 7.0f);
    lt->dynText = [] { return Theme::lightMode() ? juce::String("DARK") : juce::String("LIGHT"); };
    lt->onClick = [this] { Theme::lightMode() = !Theme::lightMode(); host.saveSetup(); repaint(); };
    auto* pw = add<Pill>({ 1404, 34, 24, 22 }, U("⏻"), [this] { return host.devices.getCurrentAudioDevice() != nullptr ? Theme::green() : Theme::off(); }, 10.0f, 6.0f);
    pw->onClick = [this] { if (host.devices.getCurrentAudioDevice() != nullptr) host.devices.closeAudioDevice(); else host.devices.restartLastAudioDevice(); };
}

void Canvas::buildScenes() {
    for (int i = 0; i < 4; ++i) {
        auto* tab = add<Pill>({ 64 + i * 98, 66, 62, 28 }, juce::String::charToString((juce::juce_wchar) ('A' + i)), [this, i] {
            int sc = (int) snap("scenes"), mask = (int) snap("smask"), n = i + 1;
            if (sc % 8 == n) return Theme::green();
            if (sc / 8 == n) return Theme::amber();
            if ((mask >> i) & 1) return Theme::c(Theme::rgb(0.24f, 0.38f, 0.62f));
            return Theme::btn(); }, 13.0f, 8.0f);
        tab->onClick = [this, i] { model().press("st_looper", "scene", 8, i + 1); };
        auto* cue = add<Pill>({ 128 + i * 98, 70, 30, 20 }, "CUE", [this, i] { return pvw == i + 1 ? Theme::amber() : Theme::btn(); }, 7.0f, 6.0f);
        cue->onClick = [this, i] { pvw = pvw == i + 1 ? 0 : i + 1; engine().set("st_looper", "pvw", pvw); };
    }
    add<ValueBox>({ 512, 68, 34, 24 }, [this] { return snap("ntn") < 1 ? 4.0 : (double) snap("ntn"); },
                  [this](double v) { model().press("st_looper", "ntr", 16, (int) v); }, 1, 8, 0, [] { return Theme::green(); });
}

void Canvas::layoutStrips() {
    int ntn = juce::jlimit(1, 8, (int) snap("ntn")); if (snap("ntn") < 1) ntn = 4;
    int idx = 0;
    for (int s = 0; s < NSTRIP; ++s) {
        bool vis = s < 4 ? (s < host.nLive || (host.synthInto > 0 && s == host.synthInto)) : (s - 4) < ntn;
        strips[s]->setVisible(vis);
        if (vis) { strips[s]->setBounds(16 + idx * 118, 100, 112, 298); ++idx; }
    }
}

void Canvas::openSetup(bool show) { setup->setVisible(show); if (show) setup->toFront(true); }
void Canvas::openSynth(bool show) { synthWin->setVisible(show); if (show) synthWin->toFront(true); }

void Canvas::doSave(bool askName) {
    if (!askName && host.songFolder.isDirectory()) { host.saveSong(host.songFolder); return; }
    chooseFile("Save the song: choose a name (a folder with that name is made)", "*", true,
               [this](juce::File f) { host.saveSong(f.withFileExtension("")); });
}

// keys and page-turner pedals (they send arrow / page keys): the hands stay on the instrument
bool Canvas::keyPressed(const juce::KeyPress& k) {
    if (setup->isVisible() || synthWin->isVisible()) return false;
    const auto code = k.getKeyCode(); const bool cmd = k.getModifiers().isCommandDown();
    if (cmd && code == 'S') { doSave(k.getModifiers().isShiftDown()); return true; }
    if (cmd && code == 'O') { chooseFile("Open a song (its folder, or its -settings.json)", "*-settings.json", false, [this](juce::File f) { host.openSong(f); }, true); return true; }
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
    layoutStrips();
    for (auto* m : meters) m->update();
    for (auto* s : strips) if (s->isVisible()) s->update();
    for (auto* c : cards) c->update();
    if (!inputSeen && engine().peak("live_in1") > 0.003f) inputSeen = true;
    if (snap("imp_busy") < 0.5f && host.status.startsWith("copying")) host.status = U("file imported ✓");
    for (auto* o : owned) o->repaint();
    if (isShowing() && !setup->isVisible() && !synthWin->isVisible() && juce::Component::getCurrentlyFocusedComponent() == nullptr) grabKeyboardFocus();
    repaint(0, 0, 1440, 100);
}

void Canvas::resized() {}

void Canvas::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    auto card = [&](int x, int y, int w, int h) { g.setColour(Theme::card()); g.fillRoundedRectangle((float) x, (float) y, (float) w, (float) h, 12.0f); };
    label(g, "ARCO", { 16, 6, 176, 26 }, 18.0f, Theme::txt(), 2);
    bool running = engine().cpu.load() > 0.0001f && host.devices.getCurrentAudioDevice() != nullptr;
    label(g, running ? U("engine running ✓") : U("audio is off - press ⏻ or open SETUP"), { 16, 33, 176, 14 }, 8.0f, running ? Theme::green() : Theme::amber());
    label(g, inputSeen ? U("input 1 → looper ✓") : "input 1: waiting for sound", { 16, 46, 176, 13 }, 7.5f, inputSeen ? Theme::green() : Theme::sub());
    card(196, 8, 572, 52); card(776, 8, 262, 52); card(1046, 8, 70, 52); card(1124, 8, 112, 52); card(1244, 8, 74, 52);
    auto sl = [&](const char* t, int x, int y, int w) { label(g, t, { x, y, w, 14 }, 7.5f, Theme::sub()); };
    sl("BPM", 398, 11, 50); sl("BEATS", 456, 11, 44); sl("BARS", 498, 11, 40); sl("COUNT", 536, 11, 40); sl("CLICK", 577, 11, 40); sl("VOL", 618, 11, 30);
    sl("BAR", 654, 11, 40); sl("BEAT", 690, 11, 40); sl("LOOP", 726, 11, 40); sl("SONG", 784, 11, 60);
    sl("MASTER", 1126, 11, 44); sl("GLUE", 1168, 11, 40); sl("IN", 1250, 11, 30); sl("OUT", 1250, 27, 30);
    float prac = (float) model().get("prac");
    if (prac < 99.5f) label(g, "PRACTICE " + juce::String((int) prac) + " %", { 654, 49, 110, 11 }, 7.0f, Theme::amber(), 2);
    // the song box
    g.setColour(Theme::card2()); g.fillRoundedRectangle(784, 29, 186, 22, 5.0f);
    label(g, host.songFolder.isDirectory() ? host.songFolder.getFileName() : juce::String("new song"), { 792, 29, 176, 22 }, 9.5f, Theme::txt(), 1);
    label(g, host.status, { 784, 49, 250, 11 }, 7.0f, Theme::sub());
    double sec = host.recordSeconds();
    label(g, juce::String::formatted("%02d:%02d", (int) sec / 60, (int) sec % 60), { 1052, 42, 58, 14 }, 7.5f, Theme::sub(), 0, juce::Justification::centred);
    label(g, "CPU " + juce::String((int) std::round(engine().cpu.load() * 100)) + "%", { 1250, 47, 64, 12 }, 6.5f, Theme::sub());
    // scenes row
    label(g, "SCENE", { 16, 72, 50, 16 }, 8.0f, Theme::sub(), 2);
    if (juce::Time::getMillisecondCounterHiRes() < flashUntil) {
        g.setColour(Theme::amber()); g.fillRoundedRectangle(1290, 68, 134, 24, 6.0f);
        label(g, flash, { 1290, 68, 134, 24 }, 9.5f, juce::Colours::black, 2, juce::Justification::centred);
    }
    label(g, "TRACKS", { 462, 72, 50, 16 }, 8.0f, Theme::sub(), 2);
    int s = model().selected;
    label(g, juce::String(U("SEND ▸ ")) + (s < 4 ? "LIVE " + juce::String(s + 1) : "TRACK " + juce::String(s - 3)), { 566, 72, 260, 16 }, 9.0f, Theme::txt(), 2);
    label(g, U("← click a strip's name: the SEND knobs are for that strip   ·   SPACE play/stop  → rec  ← undo  ↑ scene  K synth"), { 836, 73, 450, 16 }, 7.5f, Theme::sub());
}

} // namespace arco
