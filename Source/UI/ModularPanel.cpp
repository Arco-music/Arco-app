// ARCO 2.3: MODULAR - the RACK and the PATCH LAB over one patch, a library on shelves, guides and ready-made patches.
#include "InstrumentPanels.h"
#include "MainView.h"
#include "../App/AudioHost.h"
#include "../Engine/Modular.h"
#include "PlaySurfaces.h"

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static void txtAt(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft, int lines = 2) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, lines, 0.9f);
}
static juce::Colour modColour(int type) {
    switch (modCategory(type)) {
        case CatSound: return Theme::violet();
        case CatDrum: return Theme::red();
        case CatFilter: return Theme::blue();
        case CatShape: return Theme::green();
        case CatFx: return Theme::fx("dly");
        case CatMove: return Theme::amber();
        case CatTime: return Theme::c(Theme::rgb(0.86f, 0.55f, 0.30f));
        case CatPitch: return Theme::fx("cho");
        case CatIO: return Theme::txt();
        case CatVisual: return Theme::blue().interpolatedWith(Theme::green(), 0.45f);
        default: return Theme::c(Theme::rgb(0.62f, 0.62f, 0.68f));
    }
}
static juce::Colour cableColour(int i) {
    switch (i % 6) { case 0: return Theme::red(); case 1: return Theme::amber(); case 2: return Theme::green(); case 3: return Theme::blue(); case 4: return Theme::violet(); default: return Theme::fx("cho"); }
}
static juce::String niceName(int type) { return Theme::keyCase(modDefs()[(size_t) type].name); }

// ---------------------------------------------------------------- a knob of a module (silicone)
struct ModKnob : public juce::Component {
    Modular& M; int id, k; const ModParamDef& d;
    float startT = 0, startY = 0;
    ModKnob(Modular& m, int id_, int k_, const ModParamDef& def) : M(m), id(id_), k(k_), d(def) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
    float value() const { for (auto& m : M.patch.mods) if (m.id == id) return m.p[(size_t) k]; return d.def; }
    float toT(float v) const { return d.log ? std::log(v / d.lo) / std::log(d.hi / d.lo) : (v - d.lo) / (d.hi - d.lo); }
    float fromT(float t) const { t = juce::jlimit(0.0f, 1.0f, t); float v = d.log ? d.lo * std::pow(d.hi / d.lo, t) : d.lo + t * (d.hi - d.lo); return stepped() ? std::round(v) : v; }
    bool stepped() const { juce::String n(d.name); return d.choices != nullptr || n == "OCT" || n == "OCTAVE" || n == "STEPS" || n == "HITS" || n == "ROTATE" || n == "VOICES" || n == "LENGTH" || n == "BITS" || n == "DOWN" || n == "TUNE" || n == "SEMI"
                                                  || n == "TYPE" || n == "MODE" || n == "RULE" || n == "BALLS" || n == "COUNT" || n == "PARTIALS" || (n == "RANGE" && d.hi <= 3) || n == "INVERT" || n == "OCTAVES" || n == "LOOP" || n == "BIPOLAR" || (n.length() == 1 && n[0] >= '1' && n[0] <= '8' && d.lo < 0); }
    juce::String text(float v) const {
        if (d.choices != nullptr) return Theme::keyCase(d.choices[juce::jlimit(0, d.nChoices - 1, (int) std::round(v))]);
        juce::String n(d.name);
        if (n == "BIPOLAR") return v > 0.5f ? "+/-" : "0..+";
        if (n == "LOOP") return v > 0.5f ? "loops" : "once";
        if (n == "CUTOFF" || n == "FREQ" || n == "MID FREQ" || n == "TONE" && d.hi > 1000) return v >= 1000 ? juce::String(v / 1000.0f, 1) + "k" : juce::String((int) v);
        if (n == "RATE" && d.log) return juce::String(v, v < 1 ? 2 : 1) + " Hz";
        if (n == "ATTACK" || n == "DECAY" || n == "RELEASE" || n == "TIME" || n == "RISE" || n == "FALL")
            return d.hi > 50 ? juce::String((int) std::round(v)) + " ms" : (v < 1 ? juce::String((int) std::round(v * 1000)) + " ms" : juce::String(v, 2) + " s");
        if (stepped()) return (v > 0 && d.lo < 0 ? "+" : "") + juce::String((int) std::round(v));
        return juce::String(v, std::abs(v) < 10 ? 1 : 0);
    }
    int typeOf() const { for (auto& m : M.patch.mods) if (m.id == id) return m.type; return 0; }
    void paint(juce::Graphics& g) override {
        float v = value(), t = juce::jlimit(0.0f, 1.0f, toT(v));
        auto c = juce::Point<float>(getWidth() * 0.5f, 17.0f); float rad = 13.0f;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        auto face = juce::Rectangle<float>(c.x - rad * 0.72f, c.y - rad * 0.72f, rad * 1.44f, rad * 1.44f);
        if (Theme::softUI()) softRaised(g, face, rad * 0.72f, 2.5f, Theme::btn()); else { g.setColour(Theme::card()); g.fillEllipse(face); }
        juce::Path tr; tr.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
        g.setColour(Theme::line().withMultipliedAlpha(0.8f)); g.strokePath(tr, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float s0 = d.lo < 0 && d.hi > 0 && !d.log ? toT(0) : 0.0f;
        juce::Path va; va.addCentredArc(c.x, c.y, rad, rad, 0, a0 + (a1 - a0) * juce::jmin(s0, t), a0 + (a1 - a0) * juce::jmax(s0, t), true);
        g.setColour(Theme::arc(modColour(typeOf()))); g.strokePath(va, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        float a = a0 + (a1 - a0) * t;
        g.setColour(Theme::txt()); g.fillEllipse(c.x + std::sin(a) * rad * 0.48f - 1.8f, c.y - std::cos(a) * rad * 0.48f - 1.8f, 3.6f, 3.6f);
        g.setColour(Theme::sub()); g.setFont(Theme::mono(6.0f)); g.drawFittedText(juce::String(d.name).toUpperCase(), { 0, 33, getWidth(), 10 }, juce::Justification::centred, 1, 0.75f);
        txtAt(g, text(v), { 0, 43, getWidth(), 10 }, 6.5f, Theme::txt(), 1, juce::Justification::centred, 1);
    }
    void mouseDown(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true); startT = toT(value()); startY = e.position.y; }
    void mouseDrag(const juce::MouseEvent& e) override {
        float sens = e.mods.isShiftDown() ? 0.0012f : (stepped() ? 0.008f : 0.005f);
        M.setParam(id, k, fromT(startT - (e.position.y - startY) * sens)); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { M.setParam(id, k, d.def); repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { M.setParam(id, k, fromT(toT(value()) + w.deltaY * 0.05f)); repaint(); }
};

// a jack: an input is a hole in the plate, an output a lit ring
static void drawJack(juce::Graphics& g, juce::Point<float> p, bool out, juce::Colour col, float level, bool used) {
    auto r = juce::Rectangle<float>(p.x - 9, p.y - 9, 18, 18);
    if (Theme::softUI()) {
        if (out) { g.setColour(col.withAlpha(0.12f + 0.55f * juce::jlimit(0.0f, 1.0f, level))); g.fillEllipse(r.expanded(4)); softRaised(g, r, 9.0f, 2.0f, Theme::btn()); }
        softWellLite(g, r.reduced(3.5f), 5.5f);
        if (used || out) { g.setColour(col.withAlpha(out ? 0.95f : 0.6f)); g.drawEllipse(r.reduced(out ? 1.5f : 2.5f), out ? 1.8f : 1.2f); }
    } else {
        g.setColour(Theme::bg()); g.fillEllipse(r.reduced(1)); g.setColour(out ? col : Theme::txt().withAlpha(0.7f)); g.drawEllipse(r.reduced(1), 2.0f);
        g.setColour(Theme::bg().darker(0.4f)); g.fillEllipse(r.reduced(5.5f));
    }
}
static void drawCable(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float alpha, bool hang) {
    float sag = hang ? 30.0f + a.getDistanceFrom(b) * 0.22f : juce::jmax(30.0f, std::abs(b.y - a.y) * 0.5f);
    juce::Path p; p.startNewSubPath(a);
    if (hang) p.cubicTo(a + juce::Point<float>(0, sag), b + juce::Point<float>(0, sag), b);
    else p.cubicTo(a + juce::Point<float>(0, sag), b - juce::Point<float>(0, sag), b);
    g.setColour(juce::Colours::black.withAlpha(0.22f * alpha)); g.strokePath(p, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation(0, 2));
    g.setColour(c.withAlpha(alpha)); g.strokePath(p, juce::PathStrokeType(3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.25f * alpha)); g.strokePath(p, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation(-0.6f, -0.8f));
    for (auto e : { a, b }) { g.setColour(c.withAlpha(juce::jmax(0.6f, alpha))); g.fillEllipse(e.x - 5, e.y - 5, 10, 10); }
}

// ================================================================== THE RACK: rails, plates, screws
struct ModularPanel::Rack : public juce::Component {
    ModularPanel& panel; Modular& M;
    struct ModView;
    juce::OwnedArray<ModView> views;
    bool dragging = false; int dragMod = 0, dragPort = 0; bool dragFromOut = true; juce::Point<float> dragPos; int dragColour = 0;
    static constexpr int H = 300, ROW = 330;
    Rack(ModularPanel& p, Modular& m) : panel(p), M(m) {}
    ModView* viewOf(int id) const;
    juce::Point<float> portPos(int id, bool out, int port) const;
    bool portAt(juce::Point<float> p, int& id, bool& out, int& port) const;
    void build();
    void paintOverChildren(juce::Graphics& g) override;
    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bg());
        for (int y = 14; y < getHeight(); y += ROW) {          // two rails a row, with their holes
            for (int rr = 0; rr < 2; ++rr) {
                auto rail = juce::Rectangle<float>(6.0f, (float) (rr == 0 ? y - 10 : y + H + 2), (float) getWidth() - 12, 9.0f);
                if (Theme::softUI()) softWellLite(g, rail, 4.5f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(rail, 4.5f); }
                g.setColour(Theme::line().withMultipliedAlpha(0.6f));
                for (float x = 20; x < getWidth() - 14; x += 26) g.fillEllipse(x, rail.getCentreY() - 1.5f, 3, 3);
            }
        }
    }
    void beginDrag(int id, bool out, int port, juce::Point<float> at) {
        if (!out) for (auto& c : M.patch.cables) if (c.toMod == id && c.toPort == port) { int fm = c.fromMod, fp = c.fromPort; dragColour = c.colour; M.disconnect(id, port); dragMod = fm; dragPort = fp; dragFromOut = true; dragging = true; dragPos = at; repaint(); return; }
        dragMod = id; dragPort = port; dragFromOut = out; dragging = true; dragPos = at; dragColour = (int) M.patch.cables.size(); repaint();
    }
    void moveDrag(juce::Point<float> at) { if (dragging) { dragPos = at; repaint(); } }
    void endDrag(juce::Point<float> at) {
        if (!dragging) return;
        dragging = false;
        int id, port; bool out;
        if (portAt(at, id, out, port) && out != dragFromOut && id != dragMod) { if (dragFromOut) M.connect(dragMod, dragPort, id, port); else M.connect(id, port, dragMod, dragPort); }
        repaint();
    }
};

struct ModularPanel::Rack::ModView : public juce::Component {
    Rack& rack; int id, type;
    juce::OwnedArray<ModKnob> knobs;
    int knobRows = 1;
    ModView(Rack& r, int id_, int type_) : rack(r), id(id_), type(type_) {
        auto& D = modDefs()[(size_t) type];
        int nIo = juce::jmax((int) D.ins.size(), (int) D.outs.size());
        int np = (int) D.params.size();
        int w = juce::jmax(D.width, 30 + nIo * 34, np > 0 ? 12 + juce::jmin(6, (np + 1) / 2) * 50 : 0); setSize(w, Rack::H);   // knobs in two rows when they can be
        int cols = juce::jmax(1, (w - 12) / 50);
        knobRows = ((int) D.params.size() + cols - 1) / cols;
        for (int k = 0; k < (int) D.params.size(); ++k) {
            auto* kn = knobs.add(new ModKnob(rack.M, id, k, D.params[(size_t) k]));
            addAndMakeVisible(kn);
            int cw = (w - 12) / cols;
            int kh = knobRows > 2 ? 48 : 56;
            int top = modIsVisual(type) && type != MScope ? 136 : 50;     // a module to watch: its screen first, the knobs under it
            kn->setBounds(6 + (k % cols) * cw + (cw - 48) / 2, top + (k / cols) * kh, 48, 56);
        }
    }
    juce::Point<float> port(bool out, int k) const {
        auto& D = modDefs()[(size_t) type]; int n = (int) (out ? D.outs.size() : D.ins.size());
        return { 10 + (getWidth() - 20) * (k + 0.5f) / (float) juce::jmax(1, n), out ? 268.0f : 218.0f };
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
        auto r = getLocalBounds().toFloat().reduced(3.0f, 2.0f);
        if (Theme::softUI()) softRaised(g, r, 12.0f, 4.0f, Theme::card()); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 8.0f); }
        // screws in the four corners
        for (auto sp : { juce::Point<float>(r.getX() + 9, r.getY() + 9), juce::Point<float>(r.getRight() - 9, r.getY() + 9), juce::Point<float>(r.getX() + 9, r.getBottom() - 9), juce::Point<float>(r.getRight() - 9, r.getBottom() - 9) }) {
            auto s = juce::Rectangle<float>(sp.x - 3.5f, sp.y - 3.5f, 7, 7);
            if (Theme::softUI()) softWellLite(g, s, 3.5f); else { g.setColour(Theme::line()); g.fillEllipse(s); }
            g.setColour(Theme::sub().withAlpha(0.6f)); g.drawLine(s.getX() + 1.5f, s.getCentreY(), s.getRight() - 1.5f, s.getCentreY(), 1.0f);
        }
        softLed(g, { r.getX() + 22, r.getY() + 22 }, 3.2f, col, true);
        txtAt(g, niceName(type), { 32, 12, getWidth() - 62, 18 }, 10.0f, Theme::txt(), 2, juce::Justification::centredLeft, 1);
        txtAt(g, D.desc, { 14, 30, getWidth() - 24, 18 }, 6.3f, Theme::sub());
        txtAt(g, U8("✕"), { getWidth() - 30, 10, 20, 20 }, 9.0f, Theme::sub(), 2, juce::Justification::centred);
        // the jacks: inputs above, outputs on their plate below
        auto plate = juce::Rectangle<float>(10, 244, getWidth() - 20.0f, 44);
        if (Theme::softUI()) softWellLite(g, plate, 14.0f); else { g.setColour(Theme::scr().withMultipliedAlpha(0.55f)); g.fillRoundedRectangle(plate, 6.0f); }
        for (int o = 0; o < 2; ++o) {
            int n = (int) (o ? D.outs.size() : D.ins.size());
            for (int k = 0; k < n; ++k) {
                auto p = port(o == 1, k);
                bool used = false; for (auto& c : rack.M.patch.cables) used |= o ? (c.fromMod == id && c.fromPort == k) : (c.toMod == id && c.toPort == k);
                drawJack(g, p, o == 1, col, o ? std::abs(rack.M.valueAt(id, k)) / 10.0f : 0.0f, used);
                g.setColour(o ? Theme::ink(col) : Theme::sub()); g.setFont(Theme::mono(5.8f));
                g.drawFittedText(juce::String(o ? D.outs[(size_t) k] : D.ins[(size_t) k]), juce::Rectangle<int>((int) p.x - 24, o ? (int) p.y - 25 : (int) p.y - 23, 48, 10), juce::Justification::centred, 1, 0.7f);
            }
        }
        if (type == MOut) { float lv = juce::jlimit(0.0f, 1.0f, rack.M.outLevel.load()); auto m = juce::Rectangle<float>(16, 160, getWidth() - 32.0f, 8);
            if (Theme::softUI()) softWellLite(g, m, 4.0f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(m, 4); }
            g.setColour(lv > 0.95f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(m.reduced(1.5f).withWidth((m.getWidth() - 3) * std::sqrt(lv)), 3); }
        if (type == MSeq) { int st = (int) rack.M.valueAt(id, 2); int cols = juce::jmax(1, (getWidth() - 12) / 50), cw = (getWidth() - 12) / cols; int kh = knobRows > 2 ? 48 : 56;
            softLed(g, { 6 + (st % cols) * cw + cw * 0.5f, 47.0f + (st / cols) * kh }, 2.6f, col, true); }
        if (modIsVisual(type) && type != MScope) if (auto* sc = rack.M.scopeOf(id)) {
            auto s = juce::Rectangle<float>(14, 52, getWidth() - 28.0f, 80);
            if (Theme::softUI()) softWellLite(g, s, 12.0f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(s, 6); }
            auto in = s.reduced(7); auto glow = col.brighter(0.2f);
            int w0 = sc->w.load();
            switch (type) {
                case MXy: { auto& def = modDefs()[(size_t) type]; juce::ignoreUnused(def); float zoom = 1.0f; for (auto& mm : rack.M.patch.mods) if (mm.id == id) zoom = mm.p[0] / 100.0f;
                    juce::Path p; for (int i = 0; i < 256; ++i) { int k = (w0 + i) & 255; float x = in.getCentreX() + juce::jlimit(-1.0f, 1.0f, sc->a[k] / 5.0f * zoom) * in.getWidth() * 0.48f, y = in.getCentreY() - juce::jlimit(-1.0f, 1.0f, sc->b[k] / 5.0f * zoom) * in.getHeight() * 0.48f;
                        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
                    g.setColour(glow.withAlpha(0.25f)); g.strokePath(p, juce::PathStrokeType(4.0f)); g.setColour(glow); g.strokePath(p, juce::PathStrokeType(1.3f)); break; }
                case MSpectrum: { const int B = 28; for (int b = 0; b < B; ++b) { double f = std::pow(2.0, 0.5 + b * 6.5 / B); double re = 0, im = 0;
                        for (int i = 0; i < 256; ++i) { int k = (w0 + i) & 255; double win = 0.5 - 0.5 * std::cos(6.2831853 * i / 255.0); re += sc->a[k] * win * std::cos(6.2831853 * f * i / 256.0); im += sc->a[k] * win * std::sin(6.2831853 * f * i / 256.0); }
                        float db = juce::jlimit(0.0f, 1.0f, (float) (20 * std::log10(std::sqrt(re * re + im * im) / 64.0 + 1e-6) + 50) / 50.0f); float bw = in.getWidth() / B;
                        juce::ColourGradient gr(glow, 0, in.getBottom(), Theme::amber(), 0, in.getY(), false); g.setGradientFill(gr); g.fillRoundedRectangle(in.getX() + b * bw + 1, in.getBottom() - db * in.getHeight(), bw - 2, juce::jmax(2.0f, db * in.getHeight()), 2.0f); } break; }
                case MLife: { float cw = in.getWidth() / 16, ch = in.getHeight() / 8; int ph = (int) sc->b[0];
                    g.setColour(Theme::amber().withAlpha(0.18f)); g.fillRoundedRectangle(in.getX() + ph * cw, in.getY(), cw, in.getHeight(), 3);
                    for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) if (sc->a[y * 16 + x] > 0.5f) { g.setColour(x == ph ? Theme::amber() : glow); g.fillRoundedRectangle(in.getX() + x * cw + 1, in.getY() + y * ch + 1, cw - 2, ch - 2, 2.5f); } break; }
                case MBounce: { int nb = (int) sc->a[4]; g.setColour(Theme::line()); g.drawLine(in.getX(), in.getBottom(), in.getRight(), in.getBottom(), 1.5f);
                    for (int k = 0; k < 4; ++k) { if (sc->a[k] < 0 || k >= nb) continue; float x = in.getX() + (k + 0.5f) * in.getWidth() / 4, y = in.getBottom() - 7 - juce::jlimit(0.0f, 1.6f, sc->a[k]) / 1.6f * (in.getHeight() - 10);
                        float fl = sc->b[k]; g.setColour(cableColour(k).withAlpha(0.25f + 0.5f * fl)); g.fillEllipse(x - 9 - fl * 4, y - 9 - fl * 4, 18 + fl * 8, 18 + fl * 8); g.setColour(cableColour(k)); g.fillEllipse(x - 6, y - 6, 12, 12); } break; }
                case MOrbit: { auto c = in.getCentre(); float R = juce::jmin(in.getWidth(), in.getHeight()) * 0.46f;
                    for (int k = 0; k < 3; ++k) { float r = R * (0.38f + 0.31f * k); g.setColour(Theme::line()); g.drawEllipse(c.x - r, c.y - r, r * 2, r * 2, 1.0f);
                        float a = sc->a[k]; float x = c.x + std::sin(a) * r, y = c.y - std::cos(a) * r, fl = sc->b[k];
                        g.setColour(cableColour(k * 2).withAlpha(0.3f * (1 + fl))); g.fillEllipse(x - 8 - fl * 4, y - 8 - fl * 4, 16 + fl * 8, 16 + fl * 8); g.setColour(cableColour(k * 2)); g.fillEllipse(x - 4.5f, y - 4.5f, 9, 9); }
                    g.setColour(Theme::amber()); g.fillEllipse(c.x - 4, c.y - 4, 8, 8); g.setColour(Theme::sub()); g.drawLine(c.x, c.y - R, c.x, c.y - R + 8, 1.4f); break; }
                case MPendulum: { auto c = juce::Point<float>(in.getCentreX(), in.getCentreY()); float R = in.getHeight() * 0.48f; juce::Path p;
                    for (int i = 0; i < 256; ++i) { int k = (w0 + i) & 255; float x = c.x + sc->a[k] * R, y = c.y - sc->b[k] * R; if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
                    g.setColour(glow.withAlpha(0.6f)); g.strokePath(p, juce::PathStrokeType(1.2f)); int k = (w0 + 255) & 255; g.setColour(Theme::amber()); g.fillEllipse(c.x + sc->a[k] * R - 4, c.y - sc->b[k] * R - 4, 8, 8);
                    g.setColour(Theme::txt()); g.fillEllipse(c.x - 2.5f, c.y - 2.5f, 5, 5); break; }
                case MTuring: { int len = juce::jlimit(2, 16, (int) sc->b[0]); float cw = in.getWidth() / 16;
                    for (int k = 0; k < 16; ++k) { auto r = juce::Rectangle<float>(in.getX() + (15 - k) * cw + 1.5f, in.getCentreY() - cw * 0.5f, cw - 3, cw);
                        bool on = sc->a[k] > 0.5f; g.setColour(k < len ? (on ? glow : Theme::line()) : Theme::line().withAlpha(0.3f)); g.fillRoundedRectangle(r, 3); }
                    g.setColour(Theme::amber()); g.fillRoundedRectangle(in.getX() + (16 - len) * cw, in.getBottom() - 4, len * cw, 3, 1.5f); break; }
                case MCellular: { float cw = in.getWidth() / 16, ch = in.getHeight() / 16;
                    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) if (sc->a[y * 16 + x] > 0.5f) { g.setColour(glow.withAlpha(1.0f - y * 0.05f)); g.fillRect(in.getX() + x * cw + 0.5f, in.getY() + y * ch + 0.5f, cw - 1, ch - 1); } break; }
                case MGrains: { for (int k = 0; k < 16; ++k) { if (sc->a[k] < 0) continue; float x = in.getRight() - sc->a[k] * in.getWidth(), y = in.getY() + ((k * 37) % 16) / 15.0f * in.getHeight(), e = sc->b[k];
                        g.setColour(cableColour(k).withAlpha(0.25f + 0.6f * e)); g.fillEllipse(x - 3 - e * 6, y - 3 - e * 6, 6 + e * 12, 6 + e * 12); }
                    g.setColour(Theme::sub()); g.drawLine(in.getRight(), in.getY(), in.getRight(), in.getBottom(), 1.0f); break; }
                case MHarmonics: { float bw = in.getWidth() / 8; for (int k = 0; k < 8; ++k) { float h = juce::jlimit(0.0f, 1.0f, sc->a[k]) * in.getHeight();
                        juce::ColourGradient gr(glow, 0, in.getBottom(), Theme::violet(), 0, in.getY(), false); g.setGradientFill(gr); g.fillRoundedRectangle(in.getX() + k * bw + 3, in.getBottom() - h, bw - 6, juce::jmax(2.0f, h), 3); } break; }
                default: break;
            }
        }
        if (type == MScope) if (auto* sc = rack.M.scopeOf(id)) {
            auto s = juce::Rectangle<float>(14, 110, getWidth() - 28.0f, 90);
            if (Theme::softUI()) softWellLite(g, s, 10.0f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(s, 6); }
            for (int ch = 0; ch < 2; ++ch) { juce::Path pth; int w0 = sc->w.load();
                for (int i = 0; i < 256; ++i) { float v = (ch ? sc->b : sc->a)[(w0 + i) & 255]; float x = s.getX() + 6 + i * (s.getWidth() - 12) / 255.0f, y = s.getCentreY() - juce::jlimit(-1.0f, 1.0f, v / 10.0f) * (s.getHeight() * 0.42f);
                    if (i == 0) pth.startNewSubPath(x, y); else pth.lineTo(x, y); }
                g.setColour((ch ? Theme::amber() : col).withAlpha(0.9f)); g.strokePath(pth, juce::PathStrokeType(1.4f)); }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.x > getWidth() - 32 && e.y < 32) { int i = id; auto* rk = &rack; juce::MessageManager::callAsync([rk, i] { rk->M.removeModule(i); rk->panel.rebuild(); }); return; }
        bool out; int k = portHit(e.position, out);
        if (k >= 0) rack.beginDrag(id, out, k, e.getEventRelativeTo(&rack).position);
    }
    void mouseDrag(const juce::MouseEvent& e) override { setViewportIgnoreDragFlag(true); rack.moveDrag(e.getEventRelativeTo(&rack).position); }
    void mouseUp(const juce::MouseEvent& e) override { rack.endDrag(e.getEventRelativeTo(&rack).position); }
};

ModularPanel::Rack::ModView* ModularPanel::Rack::viewOf(int id) const { for (auto* v : views) if (v->id == id) return v; return nullptr; }
juce::Point<float> ModularPanel::Rack::portPos(int id, bool out, int port) const { if (auto* v = viewOf(id)) return v->port(out, port) + v->getPosition().toFloat(); return {}; }
bool ModularPanel::Rack::portAt(juce::Point<float> p, int& id, bool& out, int& port) const {
    for (auto* v : views) if (v->getBounds().toFloat().expanded(6).contains(p)) { int k = v->portHit(p - v->getPosition().toFloat(), out); if (k >= 0) { id = v->id; port = k; return true; } }
    return false;
}
void ModularPanel::Rack::build() {
    views.clear();
    int x = 10, y = 14, W = juce::jmax(400, getParentWidth() > 0 ? getParentWidth() - 12 : 1060);
    for (auto& m : M.patch.mods) {
        auto* v = views.add(new ModView(*this, m.id, m.type));
        if (x + v->getWidth() > W - 10 && x > 10) { x = 10; y += ROW; }
        v->setTopLeftPosition(x, y); addAndMakeVisible(v);
        x += v->getWidth() + 6;
    }
    setSize(W, y + ROW);
}
void ModularPanel::Rack::paintOverChildren(juce::Graphics& g) {
    float al = panel.dimCables && !dragging ? 0.28f : 0.92f;
    for (auto& c : M.patch.cables) if (viewOf(c.fromMod) && viewOf(c.toMod)) drawCable(g, portPos(c.fromMod, true, c.fromPort), portPos(c.toMod, false, c.toPort), cableColour(c.colour), al, true);
    if (dragging) drawCable(g, portPos(dragMod, dragFromOut, dragPort), dragPos, cableColour(dragColour), 0.92f, true);
}

// a search over the modules: names first, then words in what they do
static std::vector<int> modMatches(const juce::String& q) {
        std::vector<int> r; auto s = q.trim().toLowerCase(); if (s.isEmpty()) return r;
        for (int t = 0; t < MTypes; ++t) if (juce::String(modDefs()[(size_t) t].name).toLowerCase().startsWith(s)) r.push_back(t);
        for (int t = 0; t < MTypes; ++t) { auto n = juce::String(modDefs()[(size_t) t].name).toLowerCase(), d = juce::String(modDefs()[(size_t) t].desc).toLowerCase();
            if (!n.startsWith(s) && (n.contains(s) || d.contains(s))) r.push_back(t); }
        return r;
    }

// ================================================================== THE LIBRARY: shelves of modules, a search
struct ModularPanel::Library : public juce::Component {
    ModularPanel& panel;
    juce::TextEditor search;
    int cat = CatSound, scroll = 0;
    struct Row { juce::Rectangle<int> r; int type; };
    std::vector<Row> rows; std::vector<std::pair<juce::Rectangle<int>, int>> cats;
    explicit Library(ModularPanel& p) : panel(p) {
        search.setFont(Theme::font(10.0f, 0)); search.setTextToShowWhenEmpty(U8("find a module …"), Theme::sub());
        search.setColour(juce::TextEditor::backgroundColourId, Theme::card2()); search.setColour(juce::TextEditor::textColourId, Theme::txt());
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack); search.setColour(juce::TextEditor::focusedOutlineColourId, Theme::blue().withAlpha(0.5f));
        search.setIndents(14, 7);
        search.onTextChange = [this] { scroll = 0; repaint(); };
        addAndMakeVisible(search);
    }
    void resized() override { search.setBounds(10, 10, getWidth() - 20, 34); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (Theme::softUI()) softWell(g, r, 18.0f); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 10); }
        cats.clear(); rows.clear();
        const bool searching = search.getText().trim().isNotEmpty();
        int y = 54;
        if (!searching) {      // the shelves: two columns of keys
            for (int c = 0; c < CatCount; ++c) {
                auto kr = juce::Rectangle<int>(10 + (c % 2) * ((getWidth() - 26) / 2 + 6), y + (c / 2) * 34, (getWidth() - 26) / 2, 28);
                if (Theme::softUI()) softKeyLite(g, kr.toFloat(), 14.0f, c == cat, modColour(c == CatSound ? MVco : c == CatDrum ? MKick : c == CatFilter ? MVcf : c == CatShape ? MVca : c == CatFx ? MDelay : c == CatMove ? MLfo : c == CatTime ? MClock : c == CatPitch ? MQuant : c == CatIO ? MOut : c == CatVisual ? MLife : MAtt), c == cat ? 0.5f : 0.0f);
                else { g.setColour(c == cat ? Theme::sel() : Theme::btn()); g.fillRoundedRectangle(kr.toFloat(), 8); }
                txtAt(g, modCatName(c), kr, 7.8f, Theme::txt(), c == cat ? 2 : 1, juce::Justification::centred, 1);
                cats.push_back({ kr, c });
            }
            y += ((CatCount + 1) / 2) * 34 + 8;
        }
        std::vector<int> list;
        if (searching) list = modMatches(search.getText());
        else for (int t = 0; t < MTypes; ++t) if (modCategory(t) == cat) list.push_back(t);
        int per = (getHeight() - y - 8) / 50; scroll = juce::jlimit(0, juce::jmax(0, (int) list.size() - per), scroll);
        for (int i = scroll; i < (int) list.size() && (i - scroll) < per; ++i) {
            int t = list[(size_t) i];
            auto rr = juce::Rectangle<int>(10, y, getWidth() - 20, 44);
            if (Theme::softUI()) softKeyLite(g, rr.toFloat(), 16.0f, false); else { g.setColour(Theme::btn()); g.fillRoundedRectangle(rr.toFloat(), 8); }
            softLed(g, { (float) rr.getX() + 14, (float) rr.getY() + 15 }, 2.8f, modColour(t), true);
            txtAt(g, niceName(t), rr.withTrimmedLeft(26).withHeight(26).translated(0, 2), 8.8f, Theme::txt(), 2, juce::Justification::centredLeft, 1);
            txtAt(g, modDefs()[(size_t) t].desc, rr.withTrimmedLeft(14).withTrimmedRight(8).withTrimmedTop(24), 6.3f, Theme::sub(), 0, juce::Justification::centredLeft, 1);
            rows.push_back({ rr, t });
            y += 50;
        }
        if ((int) list.size() > per) txtAt(g, juce::String(scroll + 1) + U8("–") + juce::String(juce::jmin((int) list.size(), scroll + per)) + " of " + juce::String((int) list.size()) + U8("  ·  scroll for more"), { 10, getHeight() - 20, getWidth() - 20, 14 }, 6.5f, Theme::sub(), 0, juce::Justification::centred, 1);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (auto& c : cats) if (c.first.contains(e.getPosition())) { cat = c.second; scroll = 0; repaint(); return; }
        for (auto& r : rows) if (r.r.contains(e.getPosition())) { panel.addModule(r.type); return; }
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll += w.deltaY > 0 ? -1 : 1; repaint(); }
};

// ================================================================== THE GUIDE: lessons that build themselves, ready-made patches
struct ModularPanel::Guide : public juce::Component {
    ModularPanel& panel;
    int page = -1;            // -1: the list
    struct Lesson { const char* title; const char* steps; int example; };
    static const std::vector<Lesson>& lessons() {
        static const std::vector<Lesson> L = {
            { "1  Your first sound", "Everything is a module with sockets: OUTPUTS (lit rings, below) send, INPUTS (holes, above) listen.\n\n1. Take KEYS (Notes) and VCO (Sound).\n2. Drag from KEYS PITCH to VCO PITCH: the keys now choose its note.\n3. Take an ENV (Movement) and a VCA (Shape): KEYS GATE to ENV GATE, ENV to VCA CV.\n4. VCO SAW to VCA IN, VCA OUT to OUT L. Play the keys below.", 13 },
            { "2  Shape it: a filter", "A filter takes the edge off. Put a VCF (Filters) between the VCO and the VCA.\n\nSend the ENV into the filter's CUTOFF too: each note opens it and closes it again - the classic synth sound. RESO makes it sing.", 0 },
            { "3  Make it move", "Movement modules make slow voltages: an LFO goes round, RANDOM jumps, CHAOS never repeats.\n\nPlug an LFO into the filter's CUTOFF and listen to it breathe. Turn its TEMPO knob: it follows the loops.", 5 },
            { "4  Rhythm by itself", "CLOCK ticks in the tempo of the loops. EUCLID spreads hits evenly over steps (4 of 16: a kick on every beat).\n\nThree EUCLIDs into KICK, SNARE and HAT make a drum machine. Change HITS and ROTATE - new grooves.", 7 },
            { "5  Notes by themselves", "SEQ plays eight notes in turn. QUANTIZER keeps any voltage inside a scale, so even random ones sound right.\n\nOpen this one, then change the sequence's knobs while it plays.", 1 },
            { "6  An effect on a live sound", "IN brings a sound in: LIVE 1 - 4, the station's mix, or (as an effect in STUDIO) the sound passing through.\n\nThis robot voice: IN (LIVE 1) into VOWEL, an LFO moving it. Sing into LIVE 1.", 9 },
            { "7  Big and wide", "Stereo modules have L and R. SUPERSAW is seven saws spread wide; CHORUS and REVERB keep it wide - plug both L and R to OUT.", 5 },
            { "8  Make it yours", "Tips:\n- the library on the left: a shelf, or type what you want in its search\n- pull a cable off an input to unplug it; double-click a knob to reset it\n- to write your own effects and programs, open PATCHER from HOME\n- every patch can be an instrument (the keys), play into a LIVE strip, or be an effect in STUDIO", -1 },
        };
        return L;
    }
    explicit Guide(ModularPanel& p) : panel(p) {}
    std::vector<std::pair<juce::Rectangle<int>, std::function<void()>>> hits;
    void paint(juce::Graphics& g) override {
        hits.clear();
        auto r = getLocalBounds().toFloat();
        if (Theme::softUI()) softRaised(g, r.reduced(4), 20.0f, 6.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(r, 10); }
        auto key = [&](juce::Rectangle<int> b, const juce::String& t, std::function<void()> f, bool accent = false) {
            if (Theme::softUI()) softKeyLite(g, b.toFloat(), b.getHeight() * 0.5f, false, Theme::blue(), accent ? 0.35f : 0.0f); else { g.setColour(Theme::btn()); g.fillRoundedRectangle(b.toFloat(), 8); }
            txtAt(g, t, b, 8.5f, Theme::txt(), 2, juce::Justification::centred, 1); hits.push_back({ b, std::move(f) }); };
        int x = 20, w = getWidth() - 40, y = 20;
        if (page == -1) {
            txtAt(g, "Guide", { x, y, w, 24 }, 13.0f, Theme::txt(), 2); y += 28;
            txtAt(g, U8("eight short lessons - each one can build its patch for you"), { x, y, w, 16 }, 7.5f, Theme::sub()); y += 26;
            for (int i = 0; i < (int) lessons().size(); ++i) { key({ x, y, w, 34 }, lessons()[(size_t) i].title, [this, i] { page = i; repaint(); }); y += 40; }
            y += 8;
            key({ x, y, w, 38 }, juce::String(U8("Ready-made patches (")) + juce::String(Modular::patchNames().size()) + U8(") ›"), [this] { page = -2; repaint(); }, true);
        } else if (page == -2) {
            key({ x, y, 90, 30 }, U8("‹ Lessons"), [this] { page = -1; repaint(); }); y += 42;
            txtAt(g, "Ready-made patches", { x, y, w, 22 }, 12.0f, Theme::txt(), 2); y += 24;
            txtAt(g, U8("open one, or + add it beside yours - then change anything"), { x, y, w, 16 }, 7.5f, Theme::sub()); y += 22;
            auto names = Modular::patchNames();
            for (int i = 0; i < names.size() && y < getHeight() - 36; ++i) {
                key({ x, y, w - 72, 24 }, names[i], [this, i] { panel.loadExample(i, false); });
                key({ x + w - 66, y, 66, 24 }, "+ Add", [this, i] { panel.loadExample(i, true); });
                y += 26;
            }
        } else {
            auto& L = lessons()[(size_t) page];
            key({ x, y, 90, 30 }, U8("‹ Lessons"), [this] { page = -1; repaint(); }); y += 44;
            txtAt(g, juce::String::fromUTF8(L.title), { x, y, w, 24 }, 12.0f, Theme::txt(), 2, juce::Justification::centredLeft, 1); y += 34;
            g.setColour(Theme::txt().withAlpha(0.85f)); g.setFont(Theme::font(8.6f, 0));
            juce::AttributedString as; as.append(juce::String::fromUTF8(L.steps), Theme::font(8.6f, 0), Theme::txt().withAlpha(0.88f)); as.setLineSpacing(4.0f); as.setWordWrap(juce::AttributedString::byWord);
            juce::TextLayout tl; tl.createLayout(as, (float) w); tl.draw(g, juce::Rectangle<float>((float) x, (float) y, (float) w, tl.getHeight()));
            y += (int) tl.getHeight() + 20;
            if (L.example >= 0) {
                key({ x, y, w, 36 }, U8("Show me - build it"), [this, ex = L.example] { panel.loadExample(ex, false); }, true); y += 44;
                key({ x, y, w, 36 }, U8("Add it to my patch"), [this, ex = L.example] { panel.loadExample(ex, true); }); y += 44;
            }
            if (page + 1 < (int) lessons().size()) key({ x, y, w, 34 }, U8("Next lesson ›"), [this] { page += 1; repaint(); });
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { for (auto& h : hits) if (h.first.contains(e.getPosition())) { auto f = h.second; juce::MessageManager::callAsync(f); return; } }
};

static juce::String modShape(const ModPatch& p) { juce::String k; for (auto& m : p.mods) k << m.id << ':' << m.type << ','; k << '|' << (int) p.cables.size(); return k; }
// ================================================================== the window
ModularPanel::ModularPanel(Canvas& c) : cv(c) {
    getProperties().set("softShades", true);
    setWantsKeyboardFocus(true);
    auto& r = cv.host.rack; auto& h = cv.host;
    auto pill = [&](juce::Rectangle<int> b, const juce::String& t, ColourFn col, std::function<void()> f, float fs = 8.5f) { auto* p = add<Pill>(b, t, std::move(col), fs, 8.0f); p->onClick = std::move(f); return p; };
    pill({ 1360 - 112, 16, 92, 36 }, U8("✕ Close"), [] { return Theme::btn(); }, [this] { setVisible(false); });
    // the two ways in
    auto* use = pill({ 1360 - 336, 16, 214, 36 }, "", [&r] { return r.kind.load() == InstrumentRack::KModular ? Theme::violet() : Theme::btn(); }, [&r, &h] {
        r.setKind(r.kind.load() == InstrumentRack::KModular ? InstrumentRack::KSynth : InstrumentRack::KModular);
        if (r.kind.load() == InstrumentRack::KModular) h.synthNeedsStrip(); });
    use->tint = true;
    use->dynText = [&r] { return r.kind.load() == InstrumentRack::KModular ? U8("The keys play modular") : juce::String("Play modular with the keys"); };
    const char* INTO[4] = { "Off", "Live 2", "Live 3", "Live 4" };
    for (int i = 0; i < 4; ++i) { auto* p = pill({ 640 + i * 76, 16, 70, 36 }, INTO[i], [&h, i] { return h.synthInto == i ? Theme::violet() : Theme::btn(); }, [&h, i] { h.setSynthInto(i); }); p->tint = true; }
    // the second row: the patch, the tools
    auto* patch = pill({ 264, 64, 250, 34 }, "", [] { return Theme::btn(); }, [this, &r] {
        juce::PopupMenu pm; auto names = Modular::patchNames();
        for (int i = 0; i < names.size(); ++i) pm.addItem(i + 1, names[i]);
        pm.showMenuAsync(juce::PopupMenu::Options(), [this, &r](int x) { if (x <= 0) return; loadExample(x - 1, false); juce::ignoreUnused(r); }); });
    patch->dynText = [this] { auto nm = Modular::patchNames(); auto sh = modShape(cv.host.rack.modular.patch);
        for (int i = 0; i < nm.size(); ++i) if (modShape(Modular::defaultPatch((i + patchIndex) % nm.size())) == sh) return U8("Patch: ") + nm[(i + patchIndex) % nm.size()] + U8("  ▾");
        return U8("Patch: your own  ▾"); };
    pill({ 522, 64, 120, 34 }, "Unplug all", [] { return Theme::btn(); }, [this, &r] { ModPatch p = r.modular.patch; p.cables.clear(); r.modular.setPatch(p); rebuild(); });
    auto* dim = pill({ 650, 64, 170, 34 }, "", [this] { return dimCables ? Theme::sel() : Theme::btn(); }, [this] { dimCables = !dimCables; repaint(); if (rack) rack->repaint(); });
    dim->dynText = [this] { return dimCables ? U8("Cables: see-through") : U8("Cables: solid"); };
    auto* gd = pill({ 828, 64, 120, 34 }, "Guide", [this] { return guide && guide->isVisible() ? Theme::blue() : Theme::btn(); }, [this] { guide->setVisible(!guide->isVisible()); repaint(); });
    gd->tint = true;
    pill({ 956, 64, 220, 34 }, U8("Use as an effect in Studio ▸"), [] { return Theme::btn(); }, [this] {
        auto& S = cv.host.studio;
        StudioDevice d; d.type = DevPatch; d.patchJson = juce::JSON::toString(Modular::toVar(cv.host.rack.modular.patch), true); d.plugName = "Patch";
        bool hasIn = false; for (auto& m : cv.host.rack.modular.patch.mods) hasIn |= m.type == MIn && std::lround(m.p[0]) == 0;
        int li = -1; for (int i = 0; i < (int) S.song.lanes.size(); ++i) if (S.song.lanes[(size_t) i].kind == LaneAudio) { li = i; break; }
        if (li < 0) { message = "make an audio lane in STUDIO first"; repaint(); return; }
        S.snapshot(); { const juce::ScopedLock sl(S.lock); S.song.lanes[(size_t) li].devices.push_back(d); } S.refresh();
        message = U8("✓ on ") + S.song.lanes[(size_t) li].name + U8(" in STUDIO") + (hasIn ? juce::String() : U8(" - add an IN module (FROM: THROUGH) so the track's sound goes into it"));
        repaint(); });
    pill({ 1184, 64, 152, 34 }, U8("Open keyboard ▸"), [] { return Theme::btn(); }, [this] { cv.openKeyboard(true); });
    // the library, the rack, the lab, the guide
    library = std::make_unique<Library>(*this);
    library->setBounds(box.getX() + 16, box.getY() + 108, 236, 502);
    addAndMakeVisible(*library);
    rack = std::make_unique<Rack>(*this, r.modular);
    rackView.setViewedComponent(rack.get(), false); rackView.setScrollBarsShown(true, false);
    rackView.setBounds(box.getX() + 262, box.getY() + 108, 1082, 502);
    addAndMakeVisible(rackView);
    guide = std::make_unique<Guide>(*this);
    guide->setBounds(box.getX() + 1344 - 330, box.getY() + 108, 330, 502);
    addChildComponent(*guide);
    // ---- play it here: the keys (or HARMONY), HOLD, the octave
    const char* MD[2] = { "Keys", "Harmony" };
    for (int i = 0; i < 2; ++i) { auto* p = pill({ 16 + i * 100, 620, 94, 30 }, MD[i], [this, i] { return playMode == i ? Theme::violet() : Theme::btn(); }, [this, i] {
        playMode = i; kb->setVisible(i == 0); held->setVisible(i == 0); surface->setVisible(i == 1); surface->mode = 3; }); p->tint = true; }
    auto* hold = pill({ 222, 620, 110, 30 }, "Hold", [&h] { return h.latch ? Theme::green() : Theme::btn(); }, [&h] { h.setLatch(!h.latch); });
    hold->tint = true;
    pill({ 340, 620, 34, 30 }, U8("−"), [] { return Theme::btn(); }, [this] { octave = juce::jmax(0, octave - 1); kb->setAvailableRange(octave * 12 + 12, 127); }, 12.0f);
    pill({ 378, 620, 34, 30 }, "+", [] { return Theme::btn(); }, [this] { octave = juce::jmin(5, octave + 1); kb->setAvailableRange(octave * 12 + 12, 127); }, 12.0f);
    auto playArea = juce::Rectangle<int>(box.getX() + 16, box.getY() + 658, 1328, 152);
    kb = std::make_unique<juce::MidiKeyboardComponent>(h.keys, juce::MidiKeyboardComponent::horizontalKeyboard);
    kb->setAvailableRange(octave * 12 + 12, 127); kb->setBounds(playArea); kb->setKeyWidth(1328.0f / 29.0f);
    kb->setScrollButtonsVisible(false); kb->setOctaveForMiddleC(4); kb->setVelocity(0.85f, true);
    kb->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::violet());
    addAndMakeVisible(*kb);
    surface = std::make_unique<KeyPad>(cv, [this] { return octave + 1; }); addChildComponent(*surface); surface->setBounds(playArea); surface->mode = 3;
    held = std::make_unique<HeldOverlay>(*kb, h); addAndMakeVisible(*held); held->setBounds(playArea);
    rebuild();
    startTimerHz(30);
}
ModularPanel::~ModularPanel() { rackView.setViewedComponent(nullptr, false); }

void ModularPanel::demoState(const juce::String& cmd) {
    if (cmd.contains("mlesson=")) { guide->page = cmd.fromFirstOccurrenceOf("mlesson=", false, false).getIntValue(); guide->repaint(); }
    if (cmd.contains("mexamples")) { guide->page = -2; guide->repaint(); }
}
void ModularPanel::showGuide(bool on) { guide->setVisible(on); rebuild(); }
void ModularPanel::addModule(int type) {
    auto& M = cv.host.rack.modular;
    M.addModule(type);
    rebuild();
    rackView.setViewPosition(0, juce::jmax(0, rack->getHeight() - rackView.getHeight()));
    if (cv.host.rack.kind.load() != InstrumentRack::KModular) cv.host.rack.setKind(InstrumentRack::KModular);
    message = U8("✓ ") + niceName(type) + U8(" added - drag from an output (lit ring) to an input (hole)");
    repaint();
}
void ModularPanel::loadExample(int which, bool insert) {
    auto& M = cv.host.rack.modular;
    auto ex = Modular::defaultPatch(which);
    if (insert) {           // added beside what is there: new numbers, below the rest
        ModPatch p = M.patch; std::map<int, int> nid; float yMax = 0;
        for (auto& m : p.mods) yMax = juce::jmax(yMax, m.y);
        int col = 0;
        for (auto m : ex.mods) { int n = p.nextId++; nid[m.id] = n; m.id = n; m.x = 60.0f + (col % 6) * 170.0f; m.y = yMax + 120.0f + (col / 6) * 110.0f + (col % 2) * 30.0f; ++col; p.mods.push_back(m); }
        for (auto c : ex.cables) { c.fromMod = nid[c.fromMod]; c.toMod = nid[c.toMod]; p.cables.push_back(c); }
        M.setPatch(p);
        message = U8("✓ added: ") + Modular::patchNames()[which];
    } else { patchIndex = which; M.setPatch(ex); message = U8("✓ ") + Modular::patchNames()[which]; }
    if (cv.host.rack.kind.load() != InstrumentRack::KModular) cv.host.rack.setKind(InstrumentRack::KModular);
    cv.host.synthNeedsStrip();
    rebuild();
}
void ModularPanel::rebuild() {
    lastShape = modShape(cv.host.rack.modular.patch);
    rack->setSize(rackView.getMaximumVisibleWidth() > 0 ? rackView.getMaximumVisibleWidth() : 1070, 10); rack->build();
    rackView.setBounds(box.getX() + 262, box.getY() + 108, guide->isVisible() ? 1082 - 336 : 1082, 502);
    repaint();
}
void ModularPanel::visibilityChanged() {
    if (!isVisible()) return;
    toFront(true); rebuild();
    if (cv.host.rack.kind.load() != InstrumentRack::KModular) cv.host.rack.setKind(InstrumentRack::KModular);    // this window plays MODULAR
}
bool ModularPanel::keyPressed(const juce::KeyPress&) { return false; }
static juce::String shapeOf2(const ModPatch& p) { juce::String k; for (auto& m : p.mods) k << m.id << ':' << m.type << ','; k << '|' << (int) p.cables.size(); return k; }
void ModularPanel::timerCallback() {
    if (!isVisible()) return;
    {   // the patch changed under us (opened, loaded, chosen elsewhere): the rack follows
        auto sh = modShape(cv.host.rack.modular.patch);
        if (sh != lastShape) { lastShape = sh; rebuild(); }
    }
    rack->repaint();
}

void ModularPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(Theme::softUI() ? 0.18f : 0.45f));
    if (Theme::softUI()) softRaised(g, box.toFloat(), 28.0f, 12.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(box.toFloat(), 14.0f); }
    txtAt(g, "Modular", { box.getX() + 26, box.getY() + 16, 140, 36 }, 16.0f, Theme::txt(), 2);
    drawCaption(g, "PLAYS INTO", { box.getX() + 560, box.getY() + 16, 76, 36 });
    if (Theme::softUI()) {          // the rack area and the lab sit in one groove
        auto area = rackView.getBounds().toFloat().expanded(4);
        softWellLite(g, area, 18.0f);
    }
    juce::String hint = message.isNotEmpty() ? message
        : U8("drag from an output (lit ring) to an input (hole) · pull a cable off an input to unplug · double-click a knob to reset · 1 V per octave");
    txtAt(g, hint, { box.getX() + 26, box.getY() + 64, 230, 34 }, 6.8f, message.isNotEmpty() ? Theme::green() : Theme::sub());
    txtAt(g, "C" + juce::String(octave), { box.getX() + 418, box.getY() + 620, 40, 30 }, 9.5f, Theme::txt(), 2);
    txtAt(g, U8("the keys play the patch (its KEYS module: pitch, gate, velocity; ARP: the keys you hold)  ·  a MIDI keyboard too"), { box.getX() + 466, box.getY() + 620, 760, 30 }, 7.5f, Theme::sub());
    drawSoftShades(g, *this);
}

} // namespace arco
