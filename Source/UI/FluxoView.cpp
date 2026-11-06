// ARCO 2.5: FLUXO - the screen: drawers of blocks, the blocks you read like sentences, the cables, "write what you want",
// the panel that explains the chosen block, your plug-ins, the console. Underneath: the PATCHER.
#include "FluxoView.h"
#include "PatcherView.h"
#include "MainView.h"
#include "../App/AudioHost.h"
#include <set>
#include <cstring>

namespace arco {

using namespace fx;
#ifndef ARCO_U8_HERE
#define ARCO_U8_HERE
static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
#endif
static void ftxt(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft, int lines = 1) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, lines, 0.9f); }
static float tw(const juce::String& s, float fs, int w = 0) { return juce::GlyphArrangement::getStringWidth(Theme::font(fs, w), s); }
static juce::Colour acc(int i) { return Theme::pal().acc[(size_t) (i & 7)]; }
static juce::Colour kindCol(Kind k) { return k == Sound ? Theme::amber() : k == Notes ? acc(5) : k == Move ? Theme::blue() : Theme::sub(); }
static juce::Colour shelfCol(const std::string& s) {
    if (s == "Listen") return Theme::green(); if (s == "Touch") return Theme::blue(); if (s == "Time") return Theme::violet(); if (s == "Notes") return acc(5);
    if (s == "Sound") return Theme::amber(); if (s == "Samples") return acc(6); if (s == "Change the sound") return Theme::red(); if (s == "Move by itself") return Theme::blue();
    if (s == "Numbers & logic") return Theme::sub(); if (s == "Watch") return acc(7); if (s == "Out") return Theme::txt(); if (s == "Mine") return Theme::violet(); if (s == "Max boxes") return Theme::violet();
    return Theme::sub();
}
static juce::Colour namedCol(const std::string& n) { if (n == "coral") return acc(0); if (n == "green") return acc(1); if (n == "amber") return acc(2); if (n == "violet") return acc(4); if (n == "teal") return acc(5); return acc(3); }
static juce::Colour blockCol(const Block& b, const BlockDef* d) { if (b.own) return namedCol(b.own->colour); return d ? shelfCol(d->shelf) : Theme::sub(); }

// ================================================================== a block's geometry
static constexpr float BW = 300.0f;
struct Chip { juce::Rectangle<float> r; std::string id; };
struct BGeo {
    juce::Rectangle<float> r, head, widget; std::vector<Chip> chips; std::vector<std::pair<juce::Rectangle<float>, juce::String>> words;
    std::vector<juce::Point<float>> ins, outs; const BlockDef* d = nullptr;
};
static const float FS = 10.4f;
static BGeo geoOf(const Block& b) {
    BGeo g; g.d = defOf(b); float x0 = b.x + 16, maxX = b.x + BW - 14, y = b.y + 40, lh = 30, x = x0;
    if (!g.d) { g.r = { b.x, b.y, BW, 80 }; g.head = { b.x, b.y, BW, 32 }; return g; }
    auto word = [&](const juce::String& w) { float wd = tw(w, FS); if (x + wd > maxX && x > x0) { x = x0; y += lh; } g.words.push_back({ { x, y, wd + 1, 24 }, w }); x += wd; };
    auto chip = [&](const juce::String& t, const std::string& id) { float wd = juce::jmin(maxX - x0, tw(t, FS, 2) + 20); if (x + wd > maxX && x > x0) { x = x0; y += lh; } g.chips.push_back({ { x, y - 1, wd, 26 }, id }); x += wd + 4; };
    if (b.type == "box") { chip(b.text.isEmpty() ? juce::String("type a Max object") : b.text, "#text"); }
    else for (auto& p : pieces(*g.d)) {
        if (p.field) { auto* f = g.d->field(p.id); chip(f ? shown(*f, valueOf(b, *f)) : juce::String("?"), p.id); continue; }
        juce::String cur; for (auto ch : p.text) { cur << juce::String::charToString(ch); if (ch == ' ') { word(cur); cur.clear(); } } if (cur.isNotEmpty()) word(cur);
    }
    y += lh;
    if (g.d->uiH > 0) { g.widget = { x0, y + 2, BW - 30, (float) g.d->uiH }; y += (float) g.d->uiH + 12; }
    float h = juce::jmax(80.0f, y - b.y + 8);
    g.r = { b.x, b.y, BW, h }; g.head = { b.x, b.y, BW, 34 };
    auto spread = [&](int n, float yy, std::vector<juce::Point<float>>& v) { for (int i = 0; i < n; ++i) v.push_back({ n == 1 ? b.x + BW * 0.5f : b.x + 40 + i * (BW - 80) / (float) (n - 1), yy }); };
    spread((int) g.d->ins.size(), b.y, g.ins); spread((int) g.d->outs.size(), b.y + h, g.outs);
    return g;
}
static juce::Path cablePath(juce::Point<float> a, juce::Point<float> b, bool toWord) {
    juce::Path p; p.startNewSubPath(a);
    if (toWord) { float dx = juce::jmax(40.0f, std::abs(b.x - a.x) * 0.4f) * (a.x > b.x ? -1.0f : 1.0f); p.cubicTo(a.x, a.y + 60, b.x - dx, b.y, b.x, b.y); }
    else { float dy = juce::jmax(30.0f, std::abs(b.y - a.y) * 0.45f); p.cubicTo(a.x, a.y + dy, b.x, b.y - dy, b.x, b.y); }
    return p;
}

// ================================================================== the canvas
struct FluxoView::Canvas : public juce::Component, public juce::FileDragAndDropTarget {
    FluxoView& v;
    explicit Canvas(FluxoView& fv) : v(fv) { setWantsKeyboardFocus(true); setSize(4000, 3000); }
    enum Drag { None, Move, Cable, Band, Word, Widget };
    Drag drag = None; juce::Point<float> p0, p1; int dragBlock = -1, cableFrom = -1, cableOut = 0; bool moved = false;
    std::map<int, juce::Point<float>> startPos; std::string dragWord; double wordStart = 0; juce::var wordVal;
    juce::String ghostType; juce::Point<float> ghostAt;     // a block being dragged in from a drawer
    int hoverBlock = -1;
    std::map<std::string, std::pair<int, double>> flashes;
    bool isSel(int id) const { return std::find(v.sel.begin(), v.sel.end(), id) != v.sel.end(); }
    std::string keyOf(int id) { auto it = v.comp().uiKey.find(v.pathPrefix() + std::to_string(id)); return it == v.comp().uiKey.end() ? std::string() : it->second; }
    px::ObjPtr objOf(int id) { auto k = keyOf(id); auto* r = v.rt(); return (r && !k.empty()) ? r->objAt(k) : nullptr; }
    double since(const std::string& k, int flash) { auto& f = flashes[k]; if (flash != f.first) f = { flash, juce::Time::getMillisecondCounterHiRes() }; return juce::Time::getMillisecondCounterHiRes() - f.second; }
    juce::String errOf(const Block& b) {
        auto* r = v.rt(); if (!r) return {}; auto pre = v.pathPrefix() + std::to_string(b.id) + "/";
        for (auto& kv : r->errs) if (kv.second.isNotEmpty() && kv.first.rfind(pre, 0) == 0) return kv.second;
        auto it = v.comp().problems.find(b.id); return it == v.comp().problems.end() ? juce::String() : it->second;
    }

    // ---------------------------------------------------------------- drawing
    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bg());
        if (!v.root().chain || !v.path.empty()) { g.setColour(Theme::line().withMultipliedAlpha(0.5f)); for (int y = 0; y < getHeight(); y += 24) for (int x = 0; x < getWidth(); x += 24) g.fillRect((float) x, (float) y, 1.5f, 1.5f); }
        auto& d = v.doc(); std::map<int, BGeo> geo; for (auto& b : d.blocks) geo[b.id] = geoOf(b);
        double t = juce::Time::getMillisecondCounterHiRes() / 1000.0; bool on = v.shell.host.engine.fluxoGain.load() > 0;
        // cables
        for (int wi = 0; wi < (int) d.wires.size(); ++wi) { auto& w = d.wires[(size_t) wi];
            auto a = geo.find(w.a), b = geo.find(w.b); if (a == geo.end() || b == geo.end() || !a->second.d || !b->second.d) continue;
            if (w.out >= (int) a->second.outs.size()) continue;
            auto pa = a->second.outs[(size_t) w.out]; juce::Point<float> pb; bool word = !w.field.empty();
            if (word) { bool f = false; for (auto& c : b->second.chips) if (c.id == w.field) { pb = pa.x > c.r.getCentreX() + 40 ? juce::Point<float>(c.r.getRight() + 2, c.r.getCentreY()) : juce::Point<float>(c.r.getX() - 2, c.r.getCentreY()); f = true; } if (!f) continue; }
            else { if (w.in >= (int) b->second.ins.size()) continue; pb = b->second.ins[(size_t) w.in]; }
            Kind k = a->second.d->outs[(size_t) w.out].kind;
            paintCable(g, cablePath(pa, pb, word), k, wi == v.selWire, t, on);
            // a translator on the cable
            juce::String conv; for (auto& c : v.comp().conv) if (c.wire == wi && v.path.empty()) conv = c.name;
            if (conv.isNotEmpty()) { auto mid = cablePath(pa, pb, word).getPointAlongPath(cablePath(pa, pb, word).getLength() * 0.5f);
                float cw = tw(conv, 8.0f, 2) + 22; auto r = juce::Rectangle<float>(mid.x - cw / 2, mid.y - 11, cw, 22);
                if (Theme::softUI()) softRaised(g, r, 11, 2.5f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(r, 11); }
                ftxt(g, conv, r.toNearestInt(), 8.0f, Theme::ink(Theme::blue()), 2, juce::Justification::centred); }
            // the value a movement carries
            if (k == Move) { if (auto o = objOf(w.a)) { auto mid = cablePath(pa, pb, word).getPointAlongPath(cablePath(pa, pb, word).getLength() * 0.35f);
                    auto s = juce::String(o->uiValue.load(), 2); auto r = juce::Rectangle<float>(mid.x - 20, mid.y - 10, 40, 20); if (Theme::softUI()) softRaised(g, r, 10, 2.0f, Theme::card()); ftxt(g, s, r.toNearestInt(), 8.0f, Theme::ink(Theme::blue()), 2, juce::Justification::centred); } }
        }
        for (auto& b : d.blocks) paintBlock(g, b, geo[b.id], t);
        if (drag == Cable) { auto it = geo.find(cableFrom); if (it != geo.end() && cableOut < (int) it->second.outs.size()) {
            Kind k = it->second.d->outs[(size_t) cableOut].kind; paintCable(g, cablePath(it->second.outs[(size_t) cableOut], p1, false), k, false, t, true); } }
        if (drag == Band) { auto r = juce::Rectangle<float>(p0, p1); g.setColour(Theme::blue().withAlpha(0.08f)); g.fillRoundedRectangle(r, 8); g.setColour(Theme::blue().withAlpha(0.5f)); g.drawRoundedRectangle(r, 8, 1.2f); }
        if (ghostType.isNotEmpty()) { Block gb; gb.type = ghostType.toStdString(); gb.x = ghostAt.x - BW / 2; gb.y = ghostAt.y - 20; auto gg = geoOf(gb); g.setOpacity(0.55f); paintBlock(g, gb, gg, t); g.setOpacity(1.0f); }
        if (d.blocks.empty() && ghostType.isEmpty()) {
            auto r = juce::Rectangle<int>(80, 70, 720, 260);
            ftxt(g, v.path.empty() ? "An empty FLUXO" : "An empty plug-in", r.removeFromTop(40), 17.0f, Theme::txt(), 2);
            ftxt(g, U8("Three ways to start:\n\n•  write what you want in the bar above - \"echo on my voice\", \"drum machine\", \"arpejo\"\n•  open a drawer on the left and click a block (or drag it here)\n•  pick a recipe on the right and change it\n\nEvery block reads like a sentence: the words in colour are buttons. Drag them up and down, or click them."), r, 10.5f, Theme::sub(), 0, juce::Justification::topLeft, 12);
        }
    }
    void paintCable(juce::Graphics& g, const juce::Path& p, Kind k, bool sel, double t, bool on) {
        auto c = sel ? Theme::blue() : kindCol(k);
        if (k == Sound) {
            g.setColour(c.withAlpha(0.18f)); g.strokePath(p, juce::PathStrokeType(11.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(c); g.strokePath(p, juce::PathStrokeType(5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path dash; float dl[2] = { 7.0f, 11.0f }; juce::PathStrokeType(1.6f).createDashedStroke(dash, p, dl, 2);
            g.setColour(juce::Colours::white.withAlpha(on ? 0.55f : 0.25f)); g.fillPath(dash);
        } else if (k == Notes) {
            juce::Path dash; float dl[2] = { 4.0f, 8.0f }; (void) t;
            juce::PathStrokeType st(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); st.createDashedStroke(dash, p, dl, 2);
            g.setColour(c); g.fillPath(dash);
        } else { g.setColour(c.withAlpha(k == Any ? 0.7f : 1.0f)); g.strokePath(p, juce::PathStrokeType(sel ? 3.0f : 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    }
    void paintBlock(juce::Graphics& g, const Block& b, const BGeo& ge, double t) {
        auto r = ge.r; auto* d = ge.d; auto col = blockCol(b, d); bool s = isSel(b.id); auto err = d ? errOf(b) : juce::String("This block is not known.");
        if (Theme::softUI()) softRaised(g, r, 20, 5.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(r, 14); }
        if (b.bypass) { g.setColour(Theme::bg().withAlpha(0.45f)); g.fillRoundedRectangle(r, 20); }
        // the head: what it is
        g.setColour(col); g.fillEllipse(r.getX() + 16, r.getY() + 14, 10, 10);
        juce::String title = d ? juce::String(U8(d->name.c_str())).toUpperCase() : juce::String("?"); if (b.label.isNotEmpty()) title = b.label.toUpperCase();
        ftxt(g, title, { (int) r.getX() + 34, (int) r.getY() + 8, (int) BW - 120, 22 }, 8.6f, Theme::ink(col.withMultipliedSaturation(1.1f)), 2);
        if (b.bypass) ftxt(g, "OFF", { (int) r.getRight() - 70, (int) r.getY() + 8, 54, 22 }, 8.0f, Theme::sub(), 2, juce::Justification::centredRight);
        else if (b.own) ftxt(g, "PLUG-IN", { (int) r.getRight() - 90, (int) r.getY() + 8, 74, 22 }, 7.6f, Theme::sub(), 2, juce::Justification::centredRight);
        // the sentence
        for (auto& w : ge.words) ftxt(g, w.second, w.first.toNearestInt(), FS, b.bypass ? Theme::sub() : Theme::txt(), 0, juce::Justification::centredLeft);
        std::set<std::string> wired; for (auto& w : v.doc().wires) if (w.b == b.id && !w.field.empty()) wired.insert(w.field);
        for (auto& c : ge.chips) {
            bool wd = wired.count(c.id) > 0; auto cr = c.r;
            if (Theme::softUI()) softWellLite(g, cr, 11); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(cr, 9); }
            if (wd) { g.setColour(Theme::blue().withAlpha(0.7f)); g.drawRoundedRectangle(cr.reduced(0.5f), 11, 1.6f); }
            if (drag == Word && dragBlock == b.id && dragWord == c.id) { g.setColour(col.withAlpha(0.15f)); g.fillRoundedRectangle(cr, 11); }
            juce::String t2; if (c.id == "#text") t2 = b.text.isEmpty() ? juce::String("type a Max object") : b.text; else if (auto* f = d->field(c.id)) t2 = shown(*f, valueOf(b, *f));
            ftxt(g, t2, cr.toNearestInt().reduced(9, 0), FS, b.bypass ? Theme::sub() : Theme::ink(col), 2, juce::Justification::centred);
        }
        if (!ge.widget.isEmpty()) paintWidget(g, b, ge, t);
        // ways in and out
        auto port = [&](juce::Point<float> p, Kind k, bool out, const juce::String& label, int n) {
            auto pr = juce::Rectangle<float>(p.x - 11, p.y - 5, 22, 10);
            g.setColour(Theme::card()); g.fillRoundedRectangle(pr.expanded(2), 7); g.setColour(kindCol(k)); g.fillRoundedRectangle(pr, 5);
            if (n > 1 || hoverBlock == b.id) ftxt(g, label, { (int) p.x - 50, out ? (int) p.y + 7 : (int) p.y - 21, 100, 15 }, 7.2f, Theme::sub(), 0, juce::Justification::centred); };
        if (d) { for (int i = 0; i < (int) ge.ins.size(); ++i) port(ge.ins[(size_t) i], d->ins[(size_t) i].kind, false, U8(d->ins[(size_t) i].label.c_str()), (int) ge.ins.size());
                 for (int i = 0; i < (int) ge.outs.size(); ++i) port(ge.outs[(size_t) i], d->outs[(size_t) i].kind, true, U8(d->outs[(size_t) i].label.c_str()), (int) ge.outs.size()); }
        if (err.isNotEmpty()) { g.setColour(Theme::red().withAlpha(0.8f)); juce::Path p; p.addRoundedRectangle(r.reduced(1), 20); juce::Path dsh; float dl[2] = { 5, 4 }; juce::PathStrokeType(1.6f).createDashedStroke(dsh, p, dl, 2); g.fillPath(dsh);
            ftxt(g, U8("⚠ ") + err, { (int) r.getX() + 16, (int) r.getBottom() - 22, (int) BW - 32, 16 }, 7.4f, Theme::ink(Theme::red()), 0); }
        if (s) { g.setColour(Theme::blue()); g.drawRoundedRectangle(r.expanded(3), 22, 2.0f); }
    }
    void well(juce::Graphics& g, juce::Rectangle<float> r, float rad) { if (Theme::softUI()) softWellLite(g, r, rad); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(r, rad); } }
    void key(juce::Graphics& g, juce::Rectangle<float> r, float rad, bool on, juce::Colour c) { if (Theme::softUI()) softKeyLite(g, r, rad, on, c, on ? 0.6f : 0.0f); else { g.setColour(on ? c : Theme::btn()); g.fillRoundedRectangle(r, rad); } }
    std::string widgetKind(const BGeo& ge) { auto w = ge.d->ui; if (w.rfind("px:", 0) == 0) { auto c = w.substr(3);
            if (c == "button" || c == "bng" || c == "live.button") return "button"; if (c == "toggle" || c == "tgl" || c == "live.toggle") return "toggle"; if (c == "slider" || c == "hslider" || c == "live.slider" || c == "gain~" || c == "live.gain~") return "slider";
            if (c == "dial" || c == "knob" || c == "live.dial") return "knob"; if (c == "kslider") return "keys"; if (c == "number" || c == "flonum" || c == "live.numbox" || c == "number~") return "number"; if (c == "multislider") return "bars";
            if (c == "scope~") return "scope"; if (c == "spectroscope~") return "spectrum"; if (c == "meter~" || c == "levelmeter~") return "level"; return "number"; }
        return w; }
    void paintWidget(juce::Graphics& g, const Block& b, const BGeo& ge, double) {
        auto r = ge.widget; auto o = objOf(b.id); double val = o ? o->uiValue.load() : 0; auto w = widgetKind(ge); auto col = blockCol(b, ge.d); auto k = keyOf(b.id);
        if (w == "level" || w == "move") { auto bar = r.withHeight(10).withCentre(r.getCentre()); well(g, bar, 5); float lv = (float) juce::jlimit(0.0, 1.0, w == "level" ? std::sqrt(val) : val);
            if (lv > 0.002f) { g.setColour((w == "level" ? Theme::green() : Theme::blue()).withAlpha(0.9f)); g.fillRoundedRectangle(bar.reduced(2).withWidth(juce::jmax(6.0f, (bar.getWidth() - 4) * lv)), 3); } }
        else if (w == "blink") { double t2 = since(k, o ? o->uiFlash.load() : 0); auto led = juce::Rectangle<float>(r.getX(), r.getY(), 10, 10);
            g.setColour(t2 < 120 ? col : Theme::line()); g.fillEllipse(led); }
        else if (w == "button") { double t2 = since(k, o ? o->uiFlash.load() : 0); auto kr = r.withSizeKeepingCentre(r.getHeight(), r.getHeight()); key(g, kr, kr.getHeight() / 2, t2 < 150, Theme::amber()); }
        else if (w == "toggle") { bool on = val > 0.5; auto kr = r.withSizeKeepingCentre(r.getHeight() * 1.6f, r.getHeight()); key(g, kr, kr.getHeight() / 2, on, Theme::green());
            ftxt(g, on ? "ON" : "OFF", kr.toNearestInt(), 8.0f, on ? Theme::ink(Theme::green()) : Theme::sub(), 2, juce::Justification::centred); }
        else if (w == "slider") { double lo = 0, hi = 1; float tt = (float) juce::jlimit(0.0, 1.0, (val - lo) / (hi - lo)); auto tr = r.withHeight(14).withCentre(r.getCentre()); well(g, tr, 7);
            g.setColour(Theme::blue().withAlpha(0.35f)); g.fillRoundedRectangle(tr.reduced(3).withWidth((tr.getWidth() - 6) * tt), 4);
            key(g, { tr.getX() + (tr.getWidth() - 28) * tt, r.getY() + 2, 28, r.getHeight() - 4 }, 12, false, Theme::blue()); }
        else if (w == "knob") { float tt = (float) juce::jlimit(0.0, 1.0, val); auto kr = r.withSizeKeepingCentre(r.getHeight(), r.getHeight()); float rad = kr.getWidth() * 0.5f - 5;
            if (Theme::softUI()) softRaised(g, kr.reduced(10), kr.getWidth() * 0.5f - 10, 3.0f, Theme::btn()); else { g.setColour(Theme::btn()); g.fillEllipse(kr.reduced(10)); }
            const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
            juce::Path tr; tr.addCentredArc(kr.getCentreX(), kr.getCentreY(), rad, rad, 0, a0, a1, true); g.setColour(Theme::line()); g.strokePath(tr, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path va; va.addCentredArc(kr.getCentreX(), kr.getCentreY(), rad, rad, 0, a0, a0 + (a1 - a0) * tt, true); g.setColour(Theme::arc(Theme::blue())); g.strokePath(va, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            ftxt(g, juce::String(tt, 2), { (int) kr.getRight() + 8, (int) kr.getCentreY() - 9, 60, 18 }, 8.6f, Theme::sub(), 2); }
        else if (w == "xy") { auto pad = r.withSizeKeepingCentre(r.getHeight() * 1.6f, r.getHeight()); well(g, pad, 14); double x = val, y = 0.5;
            if (auto* rt = v.rt()) { auto ky = yKey(k); if (auto oy = rt->objAt(ky)) y = oy->uiValue.load(); }
            if (!o) x = 0.5; float px = pad.getX() + 10 + (pad.getWidth() - 20) * (float) juce::jlimit(0.0, 1.0, x), py = pad.getBottom() - 10 - (pad.getHeight() - 20) * (float) juce::jlimit(0.0, 1.0, y);
            g.setColour(Theme::blue().withAlpha(0.2f)); g.drawLine(pad.getX() + 8, py, pad.getRight() - 8, py, 1.0f); g.drawLine(px, pad.getY() + 8, px, pad.getBottom() - 8, 1.0f);
            key(g, { px - 11, py - 11, 22, 22 }, 11, true, Theme::blue()); }
        else if (w == "keys") { int lo = 48, n = 25; int whites = 0; for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10) ++whites; }
            float ww = r.getWidth() / whites; int wi = 0; int note = (int) val; well(g, r, 8);
            for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; if (blk) continue;
                g.setColour(lo + i == note ? Theme::blue() : juce::Colours::white.withAlpha(0.92f)); g.fillRoundedRectangle(r.getX() + wi * ww + 1.5f, r.getY() + 2, ww - 3, r.getHeight() - 4, 4); ++wi; }
            wi = 0; for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; if (!blk) { ++wi; continue; }
                g.setColour(lo + i == note ? Theme::blue() : juce::Colour(0xff2a2c33)); g.fillRoundedRectangle(r.getX() + wi * ww - ww * 0.3f, r.getY() + 2, ww * 0.6f, r.getHeight() * 0.58f, 3); } }
        else if (w == "message") { double t2 = since(k, o ? o->uiFlash.load() : 0); auto kr = r.withWidth(juce::jmin(r.getWidth(), tw(b.text.isEmpty() ? valueOf(b, *ge.d->field("text")).toString() : b.text, 10.0f, 2) + 60));
            key(g, kr, kr.getHeight() / 2, t2 < 150, Theme::blue()); ftxt(g, U8("send ▸"), kr.toNearestInt(), 9.0f, Theme::txt(), 2, juce::Justification::centred); }
        else if (w == "euclid") { float a[64]; int n = o ? o->uiArray(a, 64) : 0; if (n <= 0) return; float cw = juce::jmin(22.0f, r.getWidth() / n); int cur = (int) val;
            for (int i = 0; i < n; ++i) { auto c = juce::Rectangle<float>(r.getX() + i * cw + 2, r.getY() + 2, cw - 4, cw - 4).withSizeKeepingCentre(juce::jmin(cw - 4, 16.0f), juce::jmin(cw - 4, 16.0f));
                if (a[i] > 0.5f) { g.setColour(i == cur ? col : col.withAlpha(0.55f)); g.fillEllipse(c); } else { g.setColour(i == cur ? Theme::sub() : Theme::line()); g.drawEllipse(c, 1.5f); } } }
        else if (w == "steps" || w == "bars") { float a[128]; int n = o ? o->uiArray(a, 128) : 0; well(g, r, 10); if (n <= 0) return;
            int steps = w == "steps" ? n / 2 : n; float bw = (r.getWidth() - 8) / steps; int cur = (int) val;
            for (int i = 0; i < steps; ++i) { float vv = w == "steps" ? a[i * 2] : a[i]; bool onn = w == "steps" ? a[i * 2 + 1] > 0.5f : true;
                float h = (r.getHeight() - 10) * juce::jlimit(0.04f, 1.0f, vv); auto br = juce::Rectangle<float>(r.getX() + 4 + i * bw + 1.5f, r.getBottom() - 5 - h, bw - 3, h);
                g.setColour(!onn ? Theme::line() : i == cur ? col : col.withAlpha(0.55f)); g.fillRoundedRectangle(br, 3); } }
        else if (w == "scope") { well(g, r, 10); float a[256]; int n = o ? o->uiArray(a, 256) : 0; auto in = r.reduced(8); juce::Path p;
            for (int i = 0; i < n; ++i) { float x = in.getX() + i * in.getWidth() / juce::jmax(1, n - 1), y = in.getCentreY() - juce::jlimit(-1.0f, 1.0f, a[i]) * in.getHeight() * 0.48f; if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
            g.setColour(Theme::amber()); g.strokePath(p, juce::PathStrokeType(1.8f)); }
        else if (w == "spectrum") { well(g, r, 10); float a[1024]; int n = o ? o->uiArray(a, 1024) : 0; auto in = r.reduced(8);
            if (n == 1024) { const int B = 40; for (int kk = 0; kk < B; ++kk) { double f = std::pow(2.0, 1 + kk * 8.5 / B); double re = 0, im = 0, w2 = 6.283185307 * f / 1024.0;
                    for (int i = 0; i < 1024; i += 2) { double win = 0.5 - 0.5 * std::cos(6.283185307 * i / 1023.0); re += a[i] * win * std::cos(w2 * i); im += a[i] * win * std::sin(w2 * i); }
                    float m = (float) std::sqrt(re * re + im * im) / 128.0f; float db = juce::jlimit(0.0f, 1.0f, (20 * std::log10(m + 1e-6f) + 60) / 60); float bw = in.getWidth() / B;
                    g.setColour(Theme::amber().withAlpha(0.85f)); g.fillRoundedRectangle(in.getX() + kk * bw + 1, in.getBottom() - db * in.getHeight(), bw - 2, db * in.getHeight(), 2); } } }
        else if (w == "number") { well(g, r, 10); ftxt(g, juce::String(val, 3), r.toNearestInt().reduced(12, 0), 11.0f, Theme::txt(), 2); }
        else if (w == "wave") { well(g, r, 10); auto in = r.reduced(8); auto* rt = v.rt(); auto* f = ge.d->field("name"); px::BufPtr bp;
            if (rt && f) { int nm = px::sym(valueOf(b, *f).toString().toStdString()); const juce::SpinLock::ScopedLockType sl(rt->bufLock); auto it = rt->bufs.find(nm); if (it != rt->bufs.end()) bp = it->second; }
            bool any = false; if (bp && bp->frames() > 1) { long N = bp->frames(); int W = (int) in.getWidth(); g.setColour(col.withAlpha(0.85f));
                for (int x = 0; x < W; ++x) { long a0 = N * x / W, a1 = juce::jmax(a0 + 1, N * (x + 1) / W); float mx = 0; for (long i = a0; i < a1; i += juce::jmax(1L, (a1 - a0) / 32)) mx = juce::jmax(mx, std::abs(bp->ch[0][(size_t) i])); if (mx > 0.002f) any = true;
                    float h = juce::jmin(1.0f, mx) * in.getHeight() * 0.5f; g.fillRect(in.getX() + x, in.getCentreY() - h, 1.0f, juce::jmax(1.0f, h * 2)); } }
            if (!any) ftxt(g, "empty - record into it, or drop a sound file here", in.toNearestInt(), 8.2f, Theme::sub(), 0, juce::Justification::centred); }
    }
    std::string yKey(const std::string& xKey) { auto p = xKey.rfind('/'); if (p == std::string::npos) return {}; return xKey.substr(0, p + 1) + std::to_string(std::stoi(xKey.substr(p + 1)) + 1); }

    // ---------------------------------------------------------------- hits
    int blockAt(juce::Point<float> p) { auto& d = v.doc(); for (int i = (int) d.blocks.size(); --i >= 0;) if (geoOf(d.blocks[(size_t) i]).r.expanded(4).contains(p)) return d.blocks[(size_t) i].id; return -1; }
    bool outAt(juce::Point<float> p, int& b, int& o) { for (auto& bl : v.doc().blocks) { auto ge = geoOf(bl); for (int i = 0; i < (int) ge.outs.size(); ++i) if (ge.outs[(size_t) i].getDistanceFrom(p) < 14) { b = bl.id; o = i; return true; } } return false; }
    bool inAt(juce::Point<float> p, int& b, int& i2, std::string& word) {
        for (auto& bl : v.doc().blocks) { auto ge = geoOf(bl);
            for (int i = 0; i < (int) ge.ins.size(); ++i) if (ge.ins[(size_t) i].getDistanceFrom(p) < 18) { b = bl.id; i2 = i; word.clear(); return true; }
            for (auto& c : ge.chips) if (c.id != "#text" && c.r.expanded(4).contains(p)) { auto* f = ge.d ? ge.d->field(c.id) : nullptr; if (f && f->type != 's') { b = bl.id; i2 = 0; word = c.id; return true; } } }
        return false;
    }
    int wireAt(juce::Point<float> p) {
        auto& d = v.doc(); std::map<int, BGeo> geo; for (auto& b : d.blocks) geo[b.id] = geoOf(b);
        for (int wi = 0; wi < (int) d.wires.size(); ++wi) { auto& w = d.wires[(size_t) wi]; auto a = geo.find(w.a), b = geo.find(w.b); if (a == geo.end() || b == geo.end() || w.out >= (int) a->second.outs.size()) continue;
            juce::Point<float> pb; auto pa0 = a->second.outs[(size_t) w.out]; if (!w.field.empty()) { bool f = false; for (auto& c : b->second.chips) if (c.id == w.field) { pb = pa0.x > c.r.getCentreX() + 40 ? juce::Point<float>(c.r.getRight() + 2, c.r.getCentreY()) : juce::Point<float>(c.r.getX() - 2, c.r.getCentreY()); f = true; } if (!f) continue; } else { if (w.in >= (int) b->second.ins.size()) continue; pb = b->second.ins[(size_t) w.in]; }
            auto path = cablePath(a->second.outs[(size_t) w.out], pb, !w.field.empty()); juce::Point<float> np; path.getNearestPoint(p, np); if (np.getDistanceFrom(p) < 7) return wi; }
        return -1;
    }

    // ---------------------------------------------------------------- the mouse
    void mouseMove(const juce::MouseEvent& e) override { int h = blockAt(e.position); if (h != hoverBlock) { hoverBlock = h; repaint(); } }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus(); setViewportIgnoreDragFlag(true); v.stopPreview();
        p0 = p1 = e.position; moved = false; drag = None;
        int b, o; std::string word;
        if (outAt(e.position, b, o)) { drag = Cable; cableFrom = b; cableOut = o; return; }
        // picking a cable up from where it goes in
        if (inAt(e.position, b, o, word) && word.empty()) { auto& d = v.doc(); for (int wi = (int) d.wires.size(); --wi >= 0;) { auto& w = d.wires[(size_t) wi]; if (w.b == b && w.in == o && w.field.empty()) {
                    cableFrom = w.a; cableOut = w.out; v.edit([&] { d.wires.erase(d.wires.begin() + wi); }); drag = Cable; return; } } }
        int id = blockAt(e.position);
        if (id >= 0) {
            auto* bl = v.doc().block(id); auto ge = geoOf(*bl);
            if (e.mods.isPopupMenu()) { if (!isSel(id)) v.select(id); popup(id); return; }
            for (auto& c : ge.chips) if (c.r.contains(e.position)) {
                if (!isSel(id)) v.select(id);
                if (c.id == "#text") { editText(id, c.r, true); return; }
                auto* f = ge.d->field(c.id); if (!f) return;
                if (f->type == 's') { editText(id, c.r, false, c.id); return; }
                drag = Word; dragBlock = id; dragWord = c.id; wordVal = valueOf(*bl, *f); wordStart = toNorm(*f, wordVal); v.edit([] {}); return; }
            if (!ge.widget.isEmpty() && ge.widget.expanded(4).contains(e.position) && !e.mods.isShiftDown()) { if (!isSel(id)) v.select(id); drag = Widget; dragBlock = id; widget(e, true); return; }
            if (e.mods.isShiftDown() || e.mods.isCommandDown()) { v.select(id, true); return; }
            if (!isSel(id)) v.select(id);
            drag = Move; dragBlock = id; startPos.clear(); for (int s : v.sel) if (auto* x = v.doc().block(s)) startPos[s] = { x->x, x->y };
            if (e.getNumberOfClicks() >= 2) { v.seeInside(id); drag = None; }
            return;
        }
        int wi = wireAt(e.position); if (wi >= 0) { v.sel.clear(); v.selWire = wi; v.repaint(); repaint(); return; }
        v.sel.clear(); v.selWire = -1; drag = Band;
        if (e.getNumberOfClicks() >= 2 && v.askEd) { v.askEd->grabKeyboardFocus(); drag = None; }
        v.repaint(); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        p1 = e.position; if (e.getDistanceFromDragStart() > 3) moved = true;
        if (drag == Move) { for (auto& sp : startPos) if (auto* x = v.doc().block(sp.first)) { x->x = juce::jmax(0.0f, sp.second.x + p1.x - p0.x); x->y = juce::jmax(0.0f, sp.second.y + p1.y - p0.y); } }
        else if (drag == Word) {
            auto* bl = v.doc().block(dragBlock); if (!bl) return; auto* f = defOf(*bl)->field(dragWord); if (!f) return;
            double steps = f->type == 'c' ? juce::jmax(1.0, (double) f->opts.size()) * 18.0 : 220.0;
            auto nv = fromNorm(*f, wordStart + (p0.y - p1.y) / steps + (p1.x - p0.x) / (steps * 2));
            if (nv != wordVal) { wordVal = nv; bl->f[dragWord] = nv;
                if (v.path.empty() && !f->bake) for (auto& m : liveSet(v.comp(), *bl, *f, nv)) { auto k = m.first.substr(0, m.first.find('#')); int inl = std::stoi(m.first.substr(m.first.find('#') + 1)); if (auto* r = v.rt()) r->postKey(k, inl, m.second); } }
        }
        else if (drag == Widget) widget(e, false);
        repaint(); v.repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (drag == Move && moved) { snap(); v.rebuild(); }
        else if (drag == Word) {
            auto* bl = v.doc().block(dragBlock);
            if (!moved && bl) { auto* f = defOf(*bl)->field(dragWord); if (f && f->type == 'c') choose(dragBlock, dragWord); else if (f) { auto ge = geoOf(*bl); for (auto& c : ge.chips) if (c.id == dragWord) editText(dragBlock, c.r, false, dragWord); } }
            else v.rebuild();
        }
        else if (drag == Widget) widget(e, false, true);
        else if (drag == Cable) {
            int b, i; std::string word;
            if (inAt(e.position, b, i, word) && b != cableFrom) { v.edit([&] { v.doc().connect({ cableFrom, cableOut, b, i, word }); }); }
            else if (moved) { // dropped on nothing: offer the blocks that fit, there
                cableMenu(e.position); }
        }
        else if (drag == Band && moved) { auto r = juce::Rectangle<float>(p0, p1); v.sel.clear(); for (auto& bl : v.doc().blocks) if (r.intersects(geoOf(bl).r)) v.sel.push_back(bl.id); }
        drag = None; repaint(); v.repaint();
    }
    // FLUXO's chain: a block let go under another one joins it (and the cable is made)
    void snap() {
        if (!v.root().chain || v.sel.size() != 1) return;
        auto* m = v.doc().block(v.sel[0]); if (!m) return; auto mg = geoOf(*m); auto* md = mg.d; if (!md || md->ins.empty()) return;
        for (auto& o : v.doc().blocks) { if (o.id == m->id) continue; auto og = geoOf(o); if (!og.d || og.d->outs.empty()) continue;
            juce::Point<float> a(og.r.getCentreX(), og.r.getBottom()), b(mg.r.getCentreX(), mg.r.getY());
            if (std::abs(a.x - b.x) < 90 && b.y - a.y > -40 && b.y - a.y < 90) {
                m->x = o.x; m->y = og.r.getBottom() + 46;
                bool has = false; for (auto& w : v.doc().wires) if (w.b == m->id && w.in == 0 && w.field.empty()) has = true;
                if (!has) v.doc().connect({ o.id, 0, m->id, 0, {} });
                return; } }
    }
    void widget(const juce::MouseEvent& e, bool down, bool up = false) {
        auto* bl = v.doc().block(dragBlock); if (!bl) return; auto ge = geoOf(*bl); auto w = widgetKind(ge); auto k = keyOf(bl->id); auto* r = v.rt(); if (!r || k.empty()) return;
        auto wr = ge.widget; auto o = r->objAt(k);
        if (w == "button" || w == "message") { if (down) r->postKey(k, 0, px::Msg::bang()); }
        else if (w == "toggle") { if (down && o) r->postKey(k, 0, px::Msg::i(o->uiValue.load() > 0.5 ? 0 : 1)); }
        else if (w == "slider") { double t = juce::jlimit(0.0, 1.0, (double) ((e.position.x - wr.getX() - 14) / juce::jmax(1.0f, wr.getWidth() - 28))); r->postKey(k, 0, px::Msg::f(t)); }
        else if (w == "knob") { static double st = 0; if (down) st = o ? o->uiValue.load() : 0; double t = juce::jlimit(0.0, 1.0, st + (p0.y - e.position.y) / 160.0); r->postKey(k, 0, px::Msg::f(t)); }
        else if (w == "xy") { auto pad = wr.withSizeKeepingCentre(wr.getHeight() * 1.6f, wr.getHeight()); double x = juce::jlimit(0.0, 1.0, (double) ((e.position.x - pad.getX() - 10) / (pad.getWidth() - 20))), y = juce::jlimit(0.0, 1.0, (double) ((pad.getBottom() - 10 - e.position.y) / (pad.getHeight() - 20)));
            r->postKey(k, 0, px::Msg::f(x)); r->postKey(yKey(k), 0, px::Msg::f(y)); }
        else if (w == "keys") { static int held = -1; int lo = 48, n = 25; int whites = 0; std::vector<int> wn; for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10) { ++whites; wn.push_back(lo + i); } }
            float ww = wr.getWidth() / whites; int wi = juce::jlimit(0, whites - 1, (int) ((e.position.x - wr.getX()) / ww)); int note = wn[(size_t) wi];
            if (e.position.y < wr.getY() + wr.getHeight() * 0.58f) { float fx = (e.position.x - wr.getX()) / ww - wi; if (fx < 0.3f && wi > 0) { int c2 = note - 1; if ((c2 % 12) == 1 || (c2 % 12) == 3 || (c2 % 12) == 6 || (c2 % 12) == 8 || (c2 % 12) == 10) note = c2; } else if (fx > 0.7f) { int c2 = note + 1; if ((c2 % 12) == 1 || (c2 % 12) == 3 || (c2 % 12) == 6 || (c2 % 12) == 8 || (c2 % 12) == 10) note = c2; } }
            auto send = [&](int nn, int vel) { px::Msg m; m.sel = px::S::list; m.push(px::Atom::I(nn)); m.push(px::Atom::I(vel)); r->postKey(k, 0, m); };
            if (up) { if (held >= 0) send(held, 0); held = -1; } else if (note != held) { if (held >= 0) send(held, 0); send(note, 100); held = note; } }
        else if (w == "steps" || w == "bars") { float a[128]; int n = o ? o->uiArray(a, 128) : 0; int steps = w == "steps" ? n / 2 : n; if (steps <= 0) return; float bw = (wr.getWidth() - 8) / steps;
            int i = juce::jlimit(0, steps - 1, (int) ((e.position.x - wr.getX() - 4) / bw)); double val = juce::jlimit(0.0, 1.0, (double) ((wr.getBottom() - 5 - e.position.y) / (wr.getHeight() - 10)));
            if (up && !moved && w == "steps") { px::Msg m; m.sel = px::sym("toggle"); m.push(px::Atom::I(i)); r->postKey(k, 0, m); return; }
            if (up) return;
            if (moved || w == "bars") { px::Msg m; m.sel = px::S::set; m.push(px::Atom::I(i)); m.push(px::Atom::F(val)); r->postKey(k, w == "bars" ? 0 : 0, m); } }
    }
    void choose(int id, const std::string& word) {
        auto* bl = v.doc().block(id); if (!bl) return; auto* f = defOf(*bl)->field(word); if (!f) return;
        juce::PopupMenu m; int cur = (int) std::lround((double) valueOf(*bl, *f));
        for (int i = 0; i < (int) f->opts.size(); ++i) m.addItem(i + 1, U8(f->opts[(size_t) i].label.c_str()), true, i == cur);
        juce::Component::SafePointer<Canvas> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe, id, word](int r) { if (!safe || r <= 0) return; auto& vv = safe->v;
            vv.edit([&] { if (auto* b = vv.doc().block(id)) b->f[word] = r - 1; }); });
    }
    void editText(int id, juce::Rectangle<float> r, bool boxText, const std::string& word = {}) {
        auto* ed = new juce::TextEditor(); auto* bl = v.doc().block(id); if (!bl) { delete ed; return; }
        juce::String cur = boxText ? bl->text : ([&] { auto* f = defOf(*bl)->field(word); if (!f) return juce::String(); auto vv = valueOf(*bl, *f); return f->type == 's' ? vv.toString() : f->type == 'k' ? noteName((int) (double) vv) : juce::String((double) vv); })();
        ed->setText(cur, false); ed->setFont(Theme::font(10.5f, 1)); ed->setJustification(juce::Justification::centredLeft); ed->setBounds(r.toNearestInt().withWidth(juce::jmax((int) r.getWidth(), boxText ? 260 : 120)).expanded(2, 2));
        ed->setColour(juce::TextEditor::backgroundColourId, Theme::card()); ed->setColour(juce::TextEditor::textColourId, Theme::txt()); ed->setColour(juce::TextEditor::outlineColourId, Theme::blue());
        addAndMakeVisible(ed); ed->grabKeyboardFocus(); ed->selectAll();
        juce::Component::SafePointer<Canvas> safe(this);
        auto done = std::make_shared<bool>(false);
        auto commit = [safe, ed, id, boxText, word, done] { if (*done) return; *done = true; if (!safe) return; auto t = ed->getText().trim(); auto& vv = safe->v;
            vv.edit([&] { auto* b = vv.doc().block(id); if (!b) return; if (boxText) { if (t.isNotEmpty()) b->text = t; return; }
                auto* f = defOf(*b)->field(word); if (!f) return;
                if (f->type == 's') b->f[word] = t;
                else if (f->type == 'k') { int n = -1; static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    for (int pc = 11; pc >= 0 && n < 0; --pc) if (t.toUpperCase().startsWith(N[pc]) && (juce::String(N[pc]).length() == 2 || !t.substring(1).startsWith("#"))) n = pc + 12 * (t.substring(juce::String(N[pc]).length()).getIntValue() + 1);
                    if (n < 0) n = t.getIntValue(); b->f[word] = (double) juce::jlimit((int) f->lo, (int) f->hi, n); }
                else { double x = t.retainCharacters("-0123456789.").getDoubleValue(); b->f[word] = juce::jlimit(juce::jmin(f->lo, f->hi), juce::jmax(f->lo, f->hi), x); } });
            juce::MessageManager::callAsync([ed] { delete ed; }); };
        ed->onReturnKey = commit; ed->onFocusLost = commit; ed->onEscapeKey = [ed, done] { if (*done) return; *done = true; juce::MessageManager::callAsync([ed] { delete ed; }); };
    }
    void popup(int id) {
        juce::PopupMenu m; auto* bl = v.doc().block(id); if (!bl) return; auto* d = defOf(*bl);
        bool snd = d && !d->ins.empty() && !d->outs.empty() && d->ins[0].kind == Sound && d->outs[0].kind == Sound;
        if (snd) m.addItem(1, "Hear it without this block", true, bl->bypass);
        m.addItem(2, "Duplicate"); m.addItem(3, "Rename..."); m.addItem(4, d && (bl->own || bl->type == "patch") ? "Open it" : "See inside (the PATCHER)");
        if (bl->own) m.addItem(6, U8("Put it on a STUDIO track ▸"));
        m.addSeparator(); m.addItem(5, "Delete");
        juce::Component::SafePointer<Canvas> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe, id](int r) { if (!safe) return; auto& vv = safe->v;
            if (r == 1) vv.edit([&] { if (auto* b = vv.doc().block(id)) b->bypass = !b->bypass; });
            if (r == 2) { auto* b = vv.doc().block(id); if (b) { Block c = *b; vv.edit([&] { c.id = vv.doc().nextId++; c.x += 40; c.y += 40; vv.doc().blocks.push_back(c); vv.sel = { c.id }; }); } }
            if (r == 3) { auto* b = vv.doc().block(id); if (b) safe->rename(id); }
            if (r == 4) vv.seeInside(id);
            if (r == 5) vv.edit([&] { vv.doc().remove(id); vv.sel.clear(); });
            if (r == 6) vv.toStudio(id); });
    }
    void rename(int id) {
        auto* bl = v.doc().block(id); if (!bl) return; auto ge = geoOf(*bl);
        auto* ed = new juce::TextEditor(); ed->setText(bl->label, false); ed->setFont(Theme::font(9.0f, 2)); ed->setBounds(ge.head.toNearestInt().reduced(30, 6));
        ed->setColour(juce::TextEditor::backgroundColourId, Theme::card()); ed->setColour(juce::TextEditor::textColourId, Theme::txt());
        addAndMakeVisible(ed); ed->grabKeyboardFocus();
        juce::Component::SafePointer<Canvas> safe(this);
        auto done = std::make_shared<bool>(false);
        auto commit = [safe, ed, id, done] { if (*done) return; *done = true; if (safe) safe->v.edit([&] { if (auto* b = safe->v.doc().block(id)) b->label = ed->getText().trim(); }); juce::MessageManager::callAsync([ed] { delete ed; }); };
        ed->onReturnKey = commit; ed->onFocusLost = commit;
    }
    // a cable let go on nothing: the blocks that take it, placed there
    void cableMenu(juce::Point<float> at) {
        auto* A = v.doc().block(cableFrom); if (!A) return; auto* da = defOf(*A); if (!da || cableOut >= (int) da->outs.size()) return;
        Kind k = da->outs[(size_t) cableOut].kind; juce::PopupMenu m; std::vector<std::string> ids;
        auto addShelf = [&](const char* shelf) { juce::PopupMenu sub; for (auto& b : library()) if (b.shelf == shelf && !b.ins.empty() && (b.ins[0].kind == k || b.ins[0].kind == Any || k == Any)) { ids.push_back(b.id); sub.addItem((int) ids.size(), U8(b.name.c_str())); }
            if (sub.getNumItems() > 0) m.addSubMenu(shelf, sub); };
        m.addSectionHeader(juce::String("Plug this ") + kindName(k) + " into...");
        for (auto* s : { "Change the sound", "Notes", "Sound", "Samples", "Time", "Move by itself", "Numbers & logic", "Watch", "Out" }) addShelf(s);
        juce::Component::SafePointer<Canvas> safe(this); int from = cableFrom, out = cableOut;
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe, ids, at, from, out](int r) { if (!safe || r <= 0) return; auto& vv = safe->v;
            vv.edit([&] { int id = vv.doc().add(ids[(size_t) r - 1], at.x - BW / 2, at.y); vv.doc().connect({ from, out, id, 0, {} }); vv.sel = { id }; }); });
    }
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override {
        int id = blockAt({ (float) x, (float) y });
        if (id >= 0) if (auto* bl = v.doc().block(id)) if (auto* d = defOf(*bl)) if (auto* f = d->field("name")) { for (auto& fn : files) { juce::File ff(fn); if (ff.existsAsFile()) { v.readBuffer(valueOf(*bl, *f).toString(), ff); return; } } }
        for (auto& fn : files) { juce::File ff(fn); if (ff.hasFileExtension("wav;aif;aiff;flac;mp3;ogg;m4a")) {     // a sound: a Sample player with it
                juce::String nm = ff.getFileNameWithoutExtension().retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").substring(0, 16); if (nm.isEmpty()) nm = "sound";
                int nid = -1; v.edit([&] { nid = v.doc().add("samples.player", (float) x - BW / 2, (float) y); v.doc().block(nid)->f["name"] = nm; v.sel = { nid }; });
                v.readBuffer(nm, ff); return; } }
        v.filesDropped(files, x, y);
    }
};

// ================================================================== the drawers (left)
struct FluxoView::Drawer : public juce::Component {
    FluxoView& v; std::set<std::string> open { "Listen", "Sound", "Change the sound", "Mine" };
    struct Row { juce::Rectangle<int> r; std::string shelf; const BlockDef* b = nullptr; int mine = -1; };
    std::vector<Row> rows; int downRow = -1; juce::Point<int> downAt; bool dragging = false;
    explicit Drawer(FluxoView& fv) : v(fv) {}
    int layoutRows(int w) {
        rows.clear(); int y = 8;
        auto shelfRows = [&](const std::string& s) { rows.push_back({ { 6, y, w - 12, 36 }, s, nullptr, -1 }); y += 38;
            if (!open.count(s)) return;
            if (s == "Mine") { for (int i = 0; i < (int) v.mine.size(); ++i) { rows.push_back({ { 10, y, w - 20, 44 }, s, nullptr, i }); y += 46; } if (v.mine.empty()) { rows.push_back({ { 10, y, w - 20, 44 }, s, nullptr, -2 }); y += 46; } return; }
            for (auto& b : library()) if (b.shelf == s) { rows.push_back({ { 10, y, w - 20, 44 }, s, &b, -1 }); y += 46; } };
        shelfRows("Mine"); for (auto& s : shelves()) shelfRows(s);
        return y + 20;
    }
    void paint(juce::Graphics& g) override {
        for (auto& r : rows) {
            if (r.b == nullptr && r.mine == -1) {      // a drawer
                auto c = shelfCol(r.shelf); auto rr = r.r.toFloat();
                if (Theme::softUI()) softRaised(g, rr, 14, open.count(r.shelf) ? 1.5f : 3.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(rr, 10); }
                g.setColour(c); g.fillEllipse(rr.getX() + 12, rr.getCentreY() - 5, 10, 10);
                ftxt(g, U8(r.shelf.c_str()), r.r.withTrimmedLeft(30).withTrimmedRight(28), 9.6f, Theme::txt(), 2);
                ftxt(g, open.count(r.shelf) ? U8("▾") : U8("▸"), r.r.withTrimmedRight(12), 9.0f, Theme::sub(), 2, juce::Justification::centredRight);
                continue; }
            auto rr = r.r.toFloat(); if (Theme::softUI()) softRaised(g, rr.reduced(2, 1), 12, 2.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(rr, 8); }
            if (r.mine == -2) { ftxt(g, "Your plug-ins come here", r.r.reduced(14, 0), 8.6f, Theme::sub(), 0); continue; }
            juce::String name = r.b ? U8(r.b->name.c_str()) : U8(v.mine[(size_t) r.mine]->name.c_str());
            juce::String line = "your plug-in";
            if (r.b) { line.clear(); for (auto& pc : pieces(*r.b)) { if (!pc.field) { line << pc.text; continue; } auto* f = r.b->field(pc.id); if (f) line << shown(*f, f->type == 's' ? juce::var(juce::String(f->defText)) : juce::var(f->def)); } }
            ftxt(g, name, { r.r.getX() + 14, r.r.getY() + 5, r.r.getWidth() - 22, 18 }, 9.2f, Theme::txt(), 2);
            g.setColour(Theme::sub()); g.setFont(Theme::font(7.6f, 0)); g.drawText(line, juce::Rectangle<int>(r.r.getX() + 14, r.r.getY() + 23, r.r.getWidth() - 22, 15), juce::Justification::centredLeft, true);
        }
    }
    int rowAt(juce::Point<int> p) { for (int i = 0; i < (int) rows.size(); ++i) if (rows[(size_t) i].r.contains(p)) return i; return -1; }
    void mouseDown(const juce::MouseEvent& e) override { downRow = rowAt(e.getPosition()); downAt = e.getPosition(); dragging = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (downRow < 0) return; auto& r = rows[(size_t) downRow]; if (!r.b && r.mine < 0) return;
        if (e.getDistanceFromDragStart() > 6) dragging = true; if (!dragging) return;
        auto p = v.canvas->getLocalPoint(this, e.position);
        v.canvas->ghostType = r.b ? juce::String(r.b->id) : juce::String("plugin"); v.canvas->ghostAt = p; v.canvas->repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        int i = rowAt(e.getPosition());
        if (dragging && downRow >= 0) { v.canvas->ghostType.clear(); auto p = v.canvas->getLocalPoint(this, e.position);
            if (v.vp.getScreenBounds().contains(e.getScreenPosition())) place(rows[(size_t) downRow], p); v.canvas->repaint(); dragging = false; downRow = -1; return; }
        if (i < 0 || i != downRow) return;
        auto& r = rows[(size_t) i];
        if (!r.b && r.mine == -1) { if (open.count(r.shelf)) open.erase(r.shelf); else open.insert(r.shelf); v.resized(); repaint(); return; }
        if (r.b || r.mine >= 0) place(r, { -1, -1 });
    }
    void place(const Row& r, juce::Point<float> at) {
        if (r.mine >= 0) { auto def = v.mine[(size_t) r.mine]; int id = -1; v.edit([&] { id = v.doc().add("plugin", 0, 0); v.doc().block(id)->own = pluginFromVar(pluginToVar(*def)); });
            if (auto* b = v.doc().block(id)) { auto p = at.x < 0 ? v.canvas->getLocalPoint(&v.vp, v.vp.getLocalBounds().getCentre().toFloat()) : at; b->x = p.x - BW / 2; b->y = p.y - 20; v.rebuild(); } return; }
        if (r.b->id == "max.box") { v.addBlock("box", at, "metro 8n"); return; }
        if (r.b->id == "max.patch") { px::PatchDoc pd; int i1 = pd.add("obj", "inlet~", 20, 20), o1 = pd.add("obj", "outlet~", 20, 140), m = pd.add("obj", "*~ 1", 20, 80); pd.connect(i1, 0, m, 0); pd.connect(m, 0, o1, 0);
            v.addBlock("patch", at, juce::JSON::toString(pd.toVar(), true)); return; }
        v.addBlock(r.b->id, at);
    }
};

// ================================================================== the panel on the right: the chosen block, or how it works
struct FluxoView::Side : public juce::Component {
    FluxoView& v; std::vector<std::pair<juce::Rectangle<int>, int>> recipeHits;
    std::vector<Pill*> pills;
    explicit Side(FluxoView& fv) : v(fv) {
        getProperties().set("softShades", true);
        auto mk = [&](const juce::String& t, std::function<void()> f, const char* id) { auto* p = new Pill(t, [] { return Theme::btn(); }, 9.0f, 8.0f); p->onClick = std::move(f); p->setComponentID(juce::String("fluxo:") + id); addChildComponent(p); pills.push_back(p); return p; };
        auto* byp = mk("Hear it without", [this] { if (v.sel.size() == 1) v.edit([&] { if (auto* b = v.doc().block(v.sel[0])) b->bypass = !b->bypass; }); }, "bypass");
        byp->tint = true; byp->bg = [this] { auto* b = v.sel.size() == 1 ? v.doc().block(v.sel[0]) : nullptr; return b && b->bypass ? Theme::amber() : Theme::btn(); };
        mk("Duplicate", [this] { if (v.sel.size() != 1) return; auto* b = v.doc().block(v.sel[0]); if (!b) return; Block c = *b; v.edit([&] { c.id = v.doc().nextId++; c.x += 40; c.y += 40; v.doc().blocks.push_back(c); v.sel = { c.id }; }); }, "dup");
        mk("Delete", [this] { auto s = v.sel; v.edit([&] { for (int id : s) v.doc().remove(id); v.sel.clear(); }); }, "del");
        mk(U8("See inside ▸"), [this] { if (v.sel.size() == 1) v.seeInside(v.sel[0]); }, "inside");
        mk(U8("On a STUDIO track ▸"), [this] { if (v.sel.size() == 1) v.toStudio(v.sel[0]); }, "studio");
        mk("Make a plug-in", [this] { v.openPluginMaker(); }, "plug");
    }
    Pill* pill(const char* id) { for (auto* p : pills) if (p->getComponentID() == juce::String("fluxo:") + id) return p; return nullptr; }
    void update() {
        const Block* b = v.sel.size() == 1 ? v.doc().block(v.sel[0]) : nullptr; auto* d = b ? defOf(*b) : nullptr;
        bool snd = d && !d->ins.empty() && !d->outs.empty() && d->ins[0].kind == Sound && d->outs[0].kind == Sound;
        int y = getHeight() - 16, w = getWidth() - 32;
        auto put = [&](const char* id, bool vis) { auto* p = pill(id); if (!p) return; p->setVisible(vis); if (vis) { y -= 44; p->setBounds(16, y, w, 38); } };
        put("plug", v.sel.size() >= 1); put("studio", b && b->own); put("del", !v.sel.empty()); put("dup", b != nullptr); put("inside", b != nullptr); put("bypass", snd);
    }
    void resized() override { update(); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().reduced(18, 16); recipeHits.clear();
        if (Theme::softUI()) softRaised(g, getLocalBounds().toFloat().reduced(4), 22, 4.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(4), 14); }
        const Block* b = v.sel.size() == 1 ? v.doc().block(v.sel[0]) : nullptr; auto* d = b ? defOf(*b) : nullptr;
        if (v.sel.size() > 1) { ftxt(g, juce::String(v.sel.size()) + " BLOCKS", r.removeFromTop(24), 8.6f, Theme::sub(), 2);
            ftxt(g, "Together they can become one block of your own: a plug-in, with the words you choose on its front. You can use it again in any patch, and on a STUDIO track.", r.removeFromTop(120), 9.6f, Theme::txt(), 0, juce::Justification::topLeft, 8); return; }
        if (d) {
            auto col = blockCol(*b, d);
            ftxt(g, juce::String(U8(d->shelf.c_str())).toUpperCase(), r.removeFromTop(20), 7.8f, Theme::ink(col), 2);
            ftxt(g, b->label.isNotEmpty() ? b->label : U8(d->name.c_str()), r.removeFromTop(34), 15.0f, Theme::txt(), 2);
            auto help = U8(d->help.c_str()); int hl = juce::jmax(2, (int) (tw(help, 9.6f) / juce::jmax(80, r.getWidth())) + 2);
            ftxt(g, help, r.removeFromTop(hl * 17), 9.6f, Theme::txt(), 0, juce::Justification::topLeft, hl); r.removeFromTop(10);
            if (!d->tryThis.empty()) { auto tt = U8(d->tryThis.c_str()); auto tr = r.removeFromTop(64);
                if (Theme::softUI()) softWellLite(g, tr.toFloat(), 14); ftxt(g, "TRY", tr.removeFromTop(22).reduced(12, 0).withTrimmedTop(6), 7.4f, Theme::ink(Theme::blue()), 2); ftxt(g, tt, tr.reduced(12, 2), 8.8f, Theme::txt(), 0, juce::Justification::topLeft, 3); r.removeFromTop(10); }
            auto list = [&](const char* head, const std::vector<PortDef>& ps) { if (ps.empty()) return; ftxt(g, head, r.removeFromTop(20), 7.6f, Theme::sub(), 2);
                for (auto& p : ps) { auto row = r.removeFromTop(20); g.setColour(kindCol(p.kind)); g.fillRoundedRectangle((float) row.getX(), (float) row.getCentreY() - 4, 16, 8, 4);
                    ftxt(g, U8(p.label.c_str()) + "  (" + kindName(p.kind) + ")", row.withTrimmedLeft(24), 8.6f, Theme::txt(), 0); } r.removeFromTop(6); };
            list("COMES IN", d->ins); list("GOES OUT", d->outs);
            std::set<std::string> wired; for (auto& w : v.doc().wires) if (w.b == b->id && !w.field.empty()) wired.insert(w.field);
            if (!wired.empty()) { ftxt(g, "MOVED BY A CABLE", r.removeFromTop(20), 7.6f, Theme::sub(), 2); for (auto& w2 : wired) ftxt(g, U8("•  ") + juce::String(w2), r.removeFromTop(18), 8.6f, Theme::ink(Theme::blue()), 2); }
            return;
        }
        ftxt(g, "HOW FLUXO WORKS", r.removeFromTop(22), 7.8f, Theme::sub(), 2);
        const char* steps[] = { "Blocks read like sentences. The coloured words are buttons: drag them up and down, or click.",
                                "Connect a block's bottom to another block's top. Three cables: SOUND (gold), NOTES (dots), MOVEMENT (thin).",
                                "Plug a movement into a word and the word moves by itself.",
                                "Plug the wrong kind in: FLUXO puts the right translator on the cable.",
                                "Choose some blocks and make your own plug-in." };
        for (int i = 0; i < 5; ++i) { auto row = r.removeFromTop(54); g.setColour(Theme::blue().withAlpha(0.15f)); g.fillEllipse((float) row.getX(), (float) row.getY() + 2, 22, 22);
            ftxt(g, juce::String(i + 1), { row.getX(), row.getY() + 2, 22, 22 }, 8.6f, Theme::ink(Theme::blue()), 2, juce::Justification::centred);
            ftxt(g, U8(steps[i]), row.withTrimmedLeft(32), 8.6f, Theme::txt(), 0, juce::Justification::topLeft, 3); }
        r.removeFromTop(8); ftxt(g, "START FROM A RECIPE", r.removeFromTop(22), 7.8f, Theme::sub(), 2);
        auto& R = recipes();
        for (int i = 0; i < (int) R.size() && r.getHeight() > 44; ++i) { auto row = r.removeFromTop(42);
            if (Theme::softUI()) softRaised(g, row.toFloat().reduced(1, 2), 12, 2.0f, Theme::btn()); else { g.setColour(Theme::btn()); g.fillRoundedRectangle(row.toFloat().reduced(1, 2), 8); }
            ftxt(g, R[(size_t) i].name, row.reduced(12, 0), 9.0f, Theme::txt(), 2); recipeHits.push_back({ row, i }); }
    }
    void mouseUp(const juce::MouseEvent& e) override { for (auto& h : recipeHits) if (h.first.contains(e.getPosition())) { v.addRecipe(h.second, false); return; } }
};

// ================================================================== "write what you want": what was found
struct FluxoView::Ask : public juce::Component {
    FluxoView& v; std::vector<Found> found; struct Hit { juce::Rectangle<int> r; int i; int what; }; std::vector<Hit> hits; int pick = 0;
    explicit Ask(FluxoView& fv) : v(fv) { getProperties().set("softShades", true); }
    void set(const juce::String& q) { found = ask(q, 9); pick = 0; int h = 16; for (auto& f : found) h += f.recipe >= 0 ? 74 : 46; setSize(getWidth(), juce::jmin(600, h + 58)); setVisible(!found.empty()); repaint(); }
    void paint(juce::Graphics& g) override {
        hits.clear(); auto all = getLocalBounds().toFloat().reduced(16);
        if (Theme::softUI()) softRaised(g, all, 22, 6.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(all, 14); }
        auto r = getLocalBounds().reduced(30, 28);
        for (int i = 0; i < (int) found.size(); ++i) { auto& f = found[(size_t) i];
            if (f.recipe >= 0) { auto& rc = recipes()[(size_t) f.recipe]; auto row = r.removeFromTop(70);
                if (i == pick) { g.setColour(Theme::blue().withAlpha(0.08f)); g.fillRoundedRectangle(row.toFloat(), 12); }
                ftxt(g, "RECIPE", { row.getX() + 12, row.getY() + 6, 80, 16 }, 7.0f, Theme::ink(Theme::blue()), 2);
                ftxt(g, rc.name, { row.getX() + 12, row.getY() + 22, row.getWidth() - 220, 20 }, 10.6f, Theme::txt(), 2);
                ftxt(g, rc.says, { row.getX() + 12, row.getY() + 44, row.getWidth() - 220, 18 }, 8.4f, Theme::sub(), 0);
                auto lb = juce::Rectangle<int>(row.getRight() - 196, row.getY() + 17, 90, 36), ab = juce::Rectangle<int>(row.getRight() - 100, row.getY() + 17, 92, 36);
                bool hearing = v.previewing == f.recipe;
                if (Theme::softUI()) { softKeyLite(g, lb.toFloat(), 18, hearing, Theme::green(), hearing ? 0.5f : 0); softKeyLite(g, ab.toFloat(), 18, false); }
                ftxt(g, hearing ? U8("■ Stop") : U8("▶ Listen"), lb, 8.8f, hearing ? Theme::ink(Theme::green()) : Theme::txt(), 2, juce::Justification::centred);
                ftxt(g, "Add it", ab, 8.8f, Theme::ink(Theme::blue()), 2, juce::Justification::centred);
                hits.push_back({ lb, i, 1 }); hits.push_back({ ab, i, 2 }); hits.push_back({ row, i, 2 });
            } else { auto row = r.removeFromTop(42); if (i == pick) { g.setColour(Theme::blue().withAlpha(0.08f)); g.fillRoundedRectangle(row.toFloat(), 10); }
                if (f.block) { g.setColour(shelfCol(f.block->shelf)); g.fillEllipse((float) row.getX() + 12, (float) row.getCentreY() - 5, 10, 10);
                    ftxt(g, U8(f.block->name.c_str()), { row.getX() + 30, row.getY() + 3, 200, 20 }, 9.6f, Theme::txt(), 2);
                    juce::String ln; for (auto& pc : pieces(*f.block)) { if (!pc.field) { ln << pc.text; continue; } if (auto* fd = f.block->field(pc.id)) ln << shown(*fd, fd->type == 's' ? juce::var(juce::String(fd->defText)) : juce::var(fd->def)); }
                    ftxt(g, ln, { row.getX() + 30, row.getY() + 21, row.getWidth() - 40, 17 }, 7.8f, Theme::sub(), 0); }
                else { g.setColour(Theme::violet()); g.fillEllipse((float) row.getX() + 12, (float) row.getCentreY() - 5, 10, 10);
                    ftxt(g, "Max box: " + juce::String(f.pxClass), { row.getX() + 30, row.getY() + 3, 300, 20 }, 9.6f, Theme::txt(), 2);
                    if (auto* od = px::findDef(f.pxClass)) ftxt(g, U8(od->desc.c_str()), { row.getX() + 30, row.getY() + 21, row.getWidth() - 40, 17 }, 7.8f, Theme::sub(), 0); }
                hits.push_back({ row, i, 2 }); }
            r.removeFromTop(4); }
    }
    void take(int i, int what) {
        if (i < 0 || i >= (int) found.size()) return; auto f = found[(size_t) i];
        if (f.recipe >= 0) { if (what == 1) { if (v.previewing == f.recipe) v.stopPreview(); else v.addRecipe(f.recipe, true); repaint(); return; } v.addRecipe(f.recipe, false); }
        else if (f.block) v.addBlock(f.block->id); else v.addBlock("box", { -1, -1 }, juce::String(f.pxClass));
        setVisible(false); if (v.askEd) v.askEd->setText({}, false);
    }
    void mouseUp(const juce::MouseEvent& e) override { for (auto& h : hits) if (h.r.contains(e.getPosition())) { take(h.i, h.what); return; } }
};

// ================================================================== the console (bottom)
struct FluxoView::Console : public juce::Component {
    FluxoView& v; explicit Console(FluxoView& fv) : v(fv) {}
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(4); if (Theme::softUI()) softWellLite(g, r, 16); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 10); }
        auto& L = v.shell.host.fluxLog; int n = (int) L.size(); int rows = juce::jmax(1, (getHeight() - 16) / 17); int y = 10;
        if (n == 0) ftxt(g, "The console: what \"Write it down\" blocks write, and what FLUXO tells you.", { 20, 8, getWidth() - 40, 18 }, 8.4f, Theme::sub(), 0);
        for (int i = juce::jmax(0, n - rows); i < n; ++i) { auto& l = L[(size_t) i];
            ftxt(g, (l.kind == 'e' ? U8("⚠ ") : juce::String()) + juce::String::fromUTF8(l.text), { 20, y, getWidth() - 40, 16 }, 8.4f, l.kind == 'e' ? Theme::ink(Theme::red()) : Theme::txt().withAlpha(0.85f), 0); y += 17; }
    }
};

// ================================================================== making a plug-in
struct FluxoView::Maker : public juce::Component {
    FluxoView& v; juce::TextEditor name; std::vector<std::pair<std::pair<int, std::string>, bool>> words; int colour = 3;
    std::vector<std::pair<juce::Rectangle<int>, int>> hits; juce::Rectangle<int> makeR, cancelR;
    explicit Maker(FluxoView& fv) : v(fv) { getProperties().set("softShades", true); addAndMakeVisible(name); name.setFont(Theme::font(11.0f, 2)); name.setText("My plug-in", false);
        name.setColour(juce::TextEditor::backgroundColourId, Theme::card2()); name.setColour(juce::TextEditor::textColourId, Theme::txt()); name.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack); }
    void start() { words.clear(); for (int id : v.sel) if (auto* b = v.doc().block(id)) if (auto* d = defOf(*b)) for (auto& f : d->fields) if (f.front && f.type != 's') words.push_back({ { id, f.id }, words.size() < 4 }); setVisible(true); toFront(true); name.grabKeyboardFocus(); name.selectAll(); repaint(); }
    void resized() override { name.setBounds(52, 104, getWidth() - 104, 40); }
    void paint(juce::Graphics& g) override {
        hits.clear(); auto all = getLocalBounds().toFloat().reduced(24);
        if (Theme::softUI()) softRaised(g, all, 26, 8.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(all, 16); }
        ftxt(g, "MAKE A PLUG-IN", { 52, 42, 400, 22 }, 8.6f, Theme::sub(), 2);
        ftxt(g, juce::String(v.sel.size()) + " blocks become one. Its name:", { 52, 66, getWidth() - 104, 30 }, 10.4f, Theme::txt(), 2);
        if (Theme::softUI()) softWellLite(g, name.getBounds().toFloat().expanded(4), 16);
        int y = 160; ftxt(g, "THE WORDS ON ITS FRONT", { 52, y, 400, 20 }, 7.8f, Theme::sub(), 2); y += 24;
        for (int i = 0; i < (int) words.size() && y < getHeight() - 150; ++i) { auto& w = words[(size_t) i]; auto row = juce::Rectangle<int>(52, y, getWidth() - 104, 30);
            auto* b = v.doc().block(w.first.first); auto* d = b ? defOf(*b) : nullptr; juce::String nm = d ? U8(d->name.c_str()) : juce::String("?");
            auto box = juce::Rectangle<float>((float) row.getX(), (float) row.getY() + 4, 22, 22); if (Theme::softUI()) softKeyLite(g, box, 7, w.second, Theme::blue(), w.second ? 0.6f : 0);
            if (w.second) { g.setColour(Theme::ink(Theme::blue())); juce::Path t; t.startNewSubPath(box.getX() + 5, box.getCentreY()); t.lineTo(box.getCentreX() - 1, box.getBottom() - 6); t.lineTo(box.getRight() - 5, box.getY() + 6); g.strokePath(t, juce::PathStrokeType(2.2f)); }
            ftxt(g, nm + U8(" · ") + juce::String(w.first.second), row.withTrimmedLeft(34), 9.4f, Theme::txt(), 1); hits.push_back({ row, i }); y += 32; }
        y = getHeight() - 172; ftxt(g, "ITS COLOUR", { 52, y, 200, 20 }, 7.8f, Theme::sub(), 2);
        for (int i = 0; i < 6; ++i) { int ci[] = { 3, 0, 1, 2, 4, 5 }; auto c = juce::Rectangle<float>(52.0f + i * 44, (float) y + 26, 30, 30); g.setColour(acc(ci[i])); g.fillEllipse(c);
            if (colour == ci[i]) { g.setColour(acc(ci[i])); g.drawEllipse(c.expanded(5), 2.0f); } hits.push_back({ c.toNearestInt().expanded(4), 100 + ci[i] }); }
        makeR = { getWidth() - 52 - 160, getHeight() - 96, 160, 44 }; cancelR = { makeR.getX() - 130, makeR.getY(), 120, 44 };
        if (Theme::softUI()) { softKeyLite(g, makeR.toFloat(), 22, false); softKeyLite(g, cancelR.toFloat(), 22, false); }
        ftxt(g, "Make it", makeR, 10.0f, Theme::ink(Theme::blue()), 2, juce::Justification::centred); ftxt(g, "Cancel", cancelR, 10.0f, Theme::txt(), 2, juce::Justification::centred);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        auto p = e.getPosition();
        if (makeR.contains(p)) { std::vector<std::pair<int, std::string>> fr; for (auto& w : words) if (w.second) fr.push_back(w.first);
            const char* cn[] = { "coral", "green", "amber", "blue", "violet", "teal" }; v.makePlugin(name.getText().trim().isEmpty() ? juce::String("My plug-in") : name.getText().trim(), fr, cn[juce::jlimit(0, 5, colour)]); setVisible(false); return; }
        if (cancelR.contains(p)) { setVisible(false); return; }
        for (auto& h : hits) if (h.first.contains(p)) { if (h.second >= 100) colour = h.second - 100; else words[(size_t) h.second].second = !words[(size_t) h.second].second; repaint(); return; }
    }
};

// ================================================================== the screen
juce::File FluxoView::libraryDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO").getChildFile("Fluxo"); d.createDirectory(); return d; }
FluxoView::FluxoView(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    canvas = std::make_unique<Canvas>(*this); vp.setViewedComponent(canvas.get(), false); vp.setScrollBarsShown(true, true); vp.setScrollBarThickness(8); addAndMakeVisible(vp);
    drawer = std::make_unique<Drawer>(*this); drawerVp.setViewedComponent(drawer.get(), false); drawerVp.setScrollBarsShown(true, false); drawerVp.setScrollBarThickness(6); addAndMakeVisible(drawerVp);
    side = std::make_unique<Side>(*this); addAndMakeVisible(*side);
    console = std::make_unique<Console>(*this); addAndMakeVisible(*console);
    askBox = std::make_unique<Ask>(*this); addChildComponent(*askBox);
    maker = std::make_unique<Maker>(*this); addChildComponent(*maker);
    askEd = std::make_unique<juce::TextEditor>(); addAndMakeVisible(*askEd);
    askEd->setFont(Theme::font(11.0f, 0)); askEd->setTextToShowWhenEmpty(U8("Write what you want...  \"echo on my voice\", \"drum machine\", \"arpejo em lá menor\""), Theme::sub());
    askEd->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack); askEd->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    askEd->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack); askEd->setColour(juce::TextEditor::textColourId, Theme::txt()); askEd->setIndents(4, 9);
    askEd->setComponentID("fluxo:ask");
    askEd->onTextChange = [this] { askBox->setBounds(askR.getX() - 12, askR.getBottom() - 8, juce::jmax(560, askR.getWidth()), askBox->getHeight()); askBox->set(askEd->getText()); askBox->setBounds(askR.getX() - 12, askR.getBottom() - 8, juce::jmax(560, askR.getWidth()), askBox->getHeight()); askBox->toFront(false); };
    askEd->onReturnKey = [this] { if (askBox->isVisible()) askBox->take(askBox->pick, 2); };
    askEd->onEscapeKey = [this] { askBox->setVisible(false); stopPreview(); };
    auto& h = shell.host;
    auto pill = [&](const juce::String& t, ColourFn c, std::function<void()> f, const char* id) { auto* p = add<Pill>(t, std::move(c), 9.0f, 8.0f); p->onClick = std::move(f); p->setComponentID(juce::String("fluxo:") + id); return p; };
    auto* ch = pill("Chain", [this] { return root().chain ? Theme::blue() : Theme::btn(); }, [this] { root().chain = true; canvas->repaint(); repaint(); }, "chain"); ch->tint = true; ch->seg = 1;
    auto* fr = pill("Free", [this] { return !root().chain ? Theme::blue() : Theme::btn(); }, [this] { root().chain = false; canvas->repaint(); repaint(); }, "free"); fr->tint = true; fr->seg = 3;
    const char* INTO[4] = { "Own", "Live 2", "Live 3", "Live 4" };
    for (int i = 0; i < 4; ++i) { auto* p = pill(INTO[i], [&h, i] { return h.fluxoInto == i ? Theme::violet() : Theme::btn(); }, [&h, i] { h.setFluxoInto(i); }, (juce::String("into") + juce::String(i)).toRawUTF8()); p->tint = true; p->seg = i == 0 ? 1 : i == 3 ? 3 : 2; }
    auto* snd = pill("", [&h] { return h.engine.fluxoGain.load() > 0 ? Theme::green() : Theme::btn(); }, [&h] { h.engine.fluxoGain.store(h.engine.fluxoGain.load() > 0 ? 0.0f : 1.0f); }, "sound");
    snd->tint = true; snd->dynText = [&h] { return h.engine.fluxoGain.load() > 0 ? juce::String("Sound on") : juce::String("Sound off"); };
    pill("Undo", [] { return Theme::btn(); }, [this] { undo(); }, "undo");
    pill(U8("File ▾"), [] { return Theme::btn(); }, [this] {
        juce::PopupMenu m; m.addItem(1, "New (empty)"); m.addItem(2, U8("Open…")); m.addItem(3, U8("Save as…")); m.addSeparator(); m.addItem(4, U8("Open a copy in the PATCHER (Max) ▸")); m.addItem(5, "Clear the console");
        juce::Component::SafePointer<FluxoView> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe](int r) { if (!safe) return; auto& v = *safe;
            if (r == 1) v.edit([&] { v.root() = FluxDoc(); v.path.clear(); v.sel.clear(); });
            if (r == 2) v.openFile(); if (r == 3) v.saveFile();
            if (r == 4) { v.shell.host.fluxoKeep(); if (v.shell.patcher) { v.shell.show(Screen::Patcher); v.shell.patcher->openDoc(v.comp().doc, true); } }
            if (r == 5) { v.shell.host.fluxLog.clear(); v.console->repaint(); } }); }, "file");
    auto* back = pill(U8("‹ Back"), [] { return Theme::btn(); }, [this] { if (!path.empty()) { path.pop_back(); sel.clear(); rebuild(); resized(); } }, "back"); back->setVisible(false);
    loadMine();
    startTimerHz(30);
}
FluxoView::~FluxoView() { vp.setViewedComponent(nullptr, false); drawerVp.setViewedComponent(nullptr, false); }
FluxDoc& FluxoView::root() { return shell.host.fluxDoc; }
FluxDoc& FluxoView::doc() {
    FluxDoc* d = &root();
    for (size_t i = 0; i < path.size(); ++i) { auto* b = d->block(path[i]); if (!b || !b->own || !b->own->inner) { path.resize(i); break; } d = b->own->inner.get(); }
    return *d;
}
std::string FluxoView::pathPrefix() const { std::string s; for (int p : path) s += std::to_string(p) + "/"; return s; }
px::Runtime* FluxoView::rt() { return shell.host.fluxo.get(); }
const Compiled& FluxoView::comp() { return shell.host.fluxComp; }
void FluxoView::rebuild() {
    // a plug-in that was changed inside: its ways in and out, its words, again
    FluxDoc* d = &root(); std::vector<Block*> chain;
    for (int p : path) { auto* b = d->block(p); if (!b || !b->own) break; chain.push_back(b); d = b->own->inner.get(); }
    for (int i = (int) chain.size(); --i >= 0;) { auto* b = chain[(size_t) i]; auto keep = b->own->inner; auto fresh = pluginFromVar(pluginToVar(*b->own)); fresh->inner = keep; *b->own = *fresh; }
    shell.host.fluxoCompile(false);
    canvas->repaint(); side->update(); side->repaint(); repaint();
}
void FluxoView::edit(const std::function<void()>& f) {
    undoStack.push_back(juce::JSON::toString(root().toVar(), true)); if (undoStack.size() > 100) undoStack.erase(undoStack.begin()); redoStack.clear();
    f(); rebuild();
}
void FluxoView::undo() { if (undoStack.empty()) return; redoStack.push_back(juce::JSON::toString(root().toVar(), true)); root() = FluxDoc::fromVar(juce::JSON::parse(undoStack.back())); undoStack.pop_back(); sel.clear(); rebuild(); }
void FluxoView::redo() { if (redoStack.empty()) return; undoStack.push_back(juce::JSON::toString(root().toVar(), true)); root() = FluxDoc::fromVar(juce::JSON::parse(redoStack.back())); redoStack.pop_back(); sel.clear(); rebuild(); }
void FluxoView::select(int id, bool addTo) { selWire = -1; if (!addTo) sel = { id }; else { auto it = std::find(sel.begin(), sel.end(), id); if (it == sel.end()) sel.push_back(id); else sel.erase(it); } side->update(); side->repaint(); canvas->repaint(); }
int FluxoView::addBlock(const std::string& type, juce::Point<float> at, const juce::String& text) {
    int id = -1; auto& d = doc();
    edit([&] {
        float x, y;
        if (at.x >= 0) { x = at.x - BW / 2; y = at.y - 20; }
        else if (d.chain && sel.size() == 1 && d.block(sel[0])) {     // in the chain: under the chosen block (the ones below move down)
            auto* s = d.block(sel[0]); auto sg = geoOf(*s); x = s->x; y = sg.r.getBottom() + 46;
            Block probe; probe.type = type; probe.text = text; float hgt = geoOf(probe).r.getHeight() + 46;
            for (auto& b : d.blocks) if (b.id != s->id && std::abs(b.x - s->x) < 160 && b.y >= y - 10) b.y += hgt;
        } else {                                                        // a new column on the right
            float right = 40; for (auto& b : d.blocks) right = juce::jmax(right, b.x + BW + 80); x = d.blocks.empty() ? 80.0f : right; y = 60; }
        id = d.add(type, juce::jmax(0.0f, x), juce::jmax(0.0f, y)); auto* b = d.block(id); b->text = text;
        if (d.chain && sel.size() == 1) if (auto* s = d.block(sel[0])) { auto* ds = defOf(*s); auto* db = defOf(*b);
            if (ds && db && !ds->outs.empty() && !db->ins.empty() && at.x < 0) {
                // insert: what went out of the chosen block now goes out of the new one
                if (!db->outs.empty()) for (auto& w : d.wires) if (w.a == s->id && w.out == 0 && w.field.empty()) { w.a = id; }
                d.connect({ s->id, 0, id, 0, {} }); } }
        sel = { id };
    });
    if (auto* b = doc().block(id)) { auto r = geoOf(*b).r.toNearestInt(); auto vis = vp.getViewArea(); if (!vis.contains(r)) vp.setViewPosition(juce::jmax(0, r.getX() - 200), juce::jmax(0, r.getY() - 120)); }
    return id;
}
void FluxoView::addRecipe(int i, bool preview) {
    auto& R = recipes(); if (i < 0 || i >= (int) R.size()) return;
    auto rd = recipeDoc(R[(size_t) i]);
    if (preview) { stopPreview(); beforePreview = juce::JSON::toString(root().toVar(), true); previewing = i;
        auto c = compile(rd); shell.host.fluxo->setDoc(c.doc, true); askBox->repaint(); return; }
    stopPreview();
    auto& d = doc(); float right = 60; for (auto& b : d.blocks) right = juce::jmax(right, b.x + BW + 100);
    std::map<int, int> ids;
    edit([&] { for (auto& b : rd.blocks) { Block c = b; c.id = d.nextId++; c.x = (d.blocks.empty() ? 80.0f : right) + b.x; c.y = 60 + b.y; ids[b.id] = c.id; d.blocks.push_back(c); }
        for (auto w : rd.wires) { w.a = ids[w.a]; w.b = ids[w.b]; d.wires.push_back(w); }
        // the recipe's columns, stacked by the real height of each block
        std::vector<Block*> nb; for (auto& kv : ids) if (auto* b = d.block(kv.second)) nb.push_back(b);
        std::sort(nb.begin(), nb.end(), [](Block* a, Block* b) { return a->y < b->y; });
        std::map<int, float> colBottom;
        for (auto* b : nb) { int col = (int) std::round(b->x / 60.0f); auto it = colBottom.find(col); if (it != colBottom.end() && b->y < it->second + 56) b->y = it->second + 56; colBottom[col] = geoOf(*b).r.getBottom(); }
        sel.clear(); for (auto& kv : ids) sel.push_back(kv.second); if (d.notes.isEmpty()) d.notes = R[(size_t) i].says; });
    info(U8("Added: ") + R[(size_t) i].name + U8(" - change its words, plug things in."));
    if (!d.blocks.empty()) { int minX = 1 << 30, minY = 1 << 30; for (int id : sel) if (auto* b = d.block(id)) { minX = juce::jmin(minX, (int) b->x); minY = juce::jmin(minY, (int) b->y); } vp.setViewPosition(juce::jmax(0, minX - 60), juce::jmax(0, minY - 40)); }
}
void FluxoView::stopPreview() { if (previewing < 0) return; previewing = -1; shell.host.fluxoCompile(true); if (askBox) askBox->repaint(); }
void FluxoView::openPluginMaker() { if (sel.empty()) { info("Choose the blocks first (drag a box around them, or shift-click)."); return; } maker->setBounds(getLocalBounds().withSizeKeepingCentre(560, juce::jmin(getHeight() - 20, 680))); maker->start(); }
void FluxoView::makePlugin(const juce::String& name, const std::vector<std::pair<int, std::string>>& front, const juce::String& colour) {
    auto& d = doc(); std::vector<Wire> outer; auto p = fx::makePlugin(d, sel, name, front, colour, &outer);
    float x = 1e9f, y = 1e9f; for (int id : sel) if (auto* b = d.block(id)) { x = juce::jmin(x, b->x); y = juce::jmin(y, b->y); }
    edit([&] { for (int id : sel) d.remove(id); int pid = d.add("plugin", x, y); d.block(pid)->own = p;
        for (auto w : outer) { if (w.a == -1) w.a = pid; if (w.b == -1) w.b = pid; d.connect(w); } sel = { pid }; });
    auto f = libraryDir().getChildFile("Blocks"); f.createDirectory(); auto file = f.getNonexistentChildFile(juce::File::createLegalFileName(name), ".arcoblock");
    file.replaceWithText(juce::JSON::toString(pluginToVar(*p), false)); loadMine(); resized();
    info("\"" + name + U8("\" is a plug-in now: it is in the drawer \"Mine\" for every patch. Double-click it to see inside."));
}
void FluxoView::seeInside(int id) {
    auto* b = doc().block(id); if (!b) return;
    if (b->own && b->own->inner) { path.push_back(id); sel.clear(); resized(); canvas->repaint(); repaint(); return; }
    if (!shell.patcher) return;
    if (b->type == "patch") {         // a classic patch: changed in the PATCHER, written back here
        auto dummy = std::make_shared<px::Runtime>(); dummy->prepare(48000.0); std::vector<int> p = path;
        juce::Component::SafePointer<FluxoView> safe(this);
        shell.show(Screen::Patcher);
        shell.patcher->editDevice(dummy, b->text, "FLUXO - classic patch", [safe, p, id](const juce::String& json) { if (!safe) return; auto save = safe->path; safe->path = p;
            if (auto* bb = safe->doc().block(id)) { bb->text = json; } safe->path = save; safe->rebuild(); });
        return; }
    shell.host.fluxoKeep();
    for (auto& bx : comp().doc.boxes) if (path.empty() && bx.id == id && bx.sub) { shell.show(Screen::Patcher); shell.patcher->openDoc(*bx.sub, true);
        info("A copy of the block's patch is open in the PATCHER: every block is made of Max boxes."); return; }
}
void FluxoView::toStudio(int id) {
    auto* b = doc().block(id); if (!b || !b->own || !b->own->inner) return;
    auto& S = shell.host.studio; int li = -1; for (int i = 0; i < (int) S.song.lanes.size(); ++i) if (S.song.lanes[(size_t) i].kind == LaneAudio) { li = i; break; }
    if (li < 0) { info("Make an audio track in STUDIO first, then come back."); return; }
    auto inner = *b->own->inner; for (auto& fr : b->own->front) { auto key = std::to_string(fr.first) + "." + fr.second; auto it = b->f.find(key); if (it != b->f.end()) if (auto* ib = inner.block(fr.first)) ib->f[fr.second] = it->second; }
    auto c = compile(inner, true);
    StudioDevice d; d.type = DevPatcher; d.plugName = U8(b->own->name.c_str()); d.patchJson = juce::JSON::toString(c.doc.toVar(), true);
    d.prt = std::make_shared<px::Runtime>(); d.prt->prepare(S.song.sr > 0 ? S.song.sr : 48000.0); d.prt->setDoc(c.doc, true);
    S.snapshot(); { const juce::ScopedLock sl(S.lock); S.song.lanes[(size_t) li].devices.push_back(d); } S.refresh();
    info("\"" + d.plugName + "\" is on the track \"" + S.song.lanes[(size_t) li].name + "\" in STUDIO.");
}
void FluxoView::readBuffer(const juce::String& name, const juce::File& f) {
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) { info("could not read " + f.getFileName(), 'e'); return; }
    auto b = std::make_shared<px::Buf>(); b->name = px::sym(name.toStdString()); b->sr = r->sampleRate;
    long n = (long) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 600)); int ch = (int) juce::jlimit(1u, 8u, r->numChannels);
    juce::AudioBuffer<float> tmp(ch, (int) n); r->read(&tmp, 0, (int) n, 0, true, true);
    b->ch.resize((size_t) ch); for (int c = 0; c < ch; ++c) b->ch[(size_t) c].assign(tmp.getReadPointer(c), tmp.getReadPointer(c) + n);
    if (auto* R = rt()) { const juce::SpinLock::ScopedLockType sl(R->bufLock); R->bufs[b->name] = b; }
    info(name + ": " + f.getFileName() + ", " + juce::String(n / r->sampleRate, 1) + " s"); canvas->repaint();
}
void FluxoView::info(const juce::String& s, char kind) { px::Line l {}; std::strncpy(l.text, s.toRawUTF8(), sizeof l.text - 1); l.kind = kind; shell.host.fluxLog.push_back(l); ++shell.host.fluxLogGen; if (console) console->repaint(); }
void FluxoView::loadMine() {
    mine.clear(); auto dir = libraryDir().getChildFile("Blocks");
    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.arcoblock")) { auto v = juce::JSON::parse(f.loadFileAsString()); if (v.isObject()) mine.push_back(pluginFromVar(v)); }
}
void FluxoView::saveFile() {
    chooser = std::make_unique<juce::FileChooser>("Save the FLUXO patch", libraryDir().getChildFile(root().name + ".arcoflux"), "*.arcoflux");
    juce::Component::SafePointer<FluxoView> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) { if (!safe) return; auto f = fc.getResult(); if (f == juce::File()) return;
        safe->shell.host.fluxoKeep(); f.withFileExtension("arcoflux").replaceWithText(juce::JSON::toString(safe->root().toVar(), false)); safe->info("saved " + f.getFileName()); });
}
void FluxoView::openFile() {
    chooser = std::make_unique<juce::FileChooser>("Open a FLUXO patch", libraryDir(), "*.arcoflux;*.arcoblock");
    juce::Component::SafePointer<FluxoView> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) { if (!safe) return; auto f = fc.getResult(); if (!f.existsAsFile()) return;
        auto v = juce::JSON::parse(f.loadFileAsString());
        if (f.hasFileExtension("arcoblock")) { safe->edit([&] { int id = safe->doc().add("plugin", 80, 80); safe->doc().block(id)->own = pluginFromVar(v); }); return; }
        safe->edit([&] { safe->root() = FluxDoc::fromVar(v); safe->path.clear(); safe->sel.clear(); }); });
}
void FluxoView::filesDropped(const juce::StringArray& files, int, int) {
    for (auto& fn : files) { juce::File f(fn); if (f.hasFileExtension("arcoflux")) { auto v = juce::JSON::parse(f.loadFileAsString()); edit([&] { root() = FluxDoc::fromVar(v); path.clear(); sel.clear(); }); return; } }
}
void FluxoView::visibilityChanged() { shell.host.fluxoKeys.store(isShowing()); if (isShowing()) { shell.host.patcherKeys.store(false); canvas->grabKeyboardFocus(); } }
bool FluxoView::keyPressed(const juce::KeyPress& k) {
    bool cmd = k.getModifiers().isCommandDown();
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
        if (selWire >= 0) { int w = selWire; edit([&] { if (w < (int) doc().wires.size()) doc().wires.erase(doc().wires.begin() + w); }); selWire = -1; return true; }
        if (!sel.empty()) { auto s = sel; edit([&] { for (int id : s) doc().remove(id); sel.clear(); }); return true; } }
    if (cmd && k.getKeyCode() == 'Z') { if (k.getModifiers().isShiftDown()) redo(); else undo(); return true; }
    if (cmd && k.getKeyCode() == 'D' && sel.size() == 1) { auto* b = doc().block(sel[0]); if (b) { Block c = *b; edit([&] { c.id = doc().nextId++; c.x += 40; c.y += 40; doc().blocks.push_back(c); sel = { c.id }; }); } return true; }
    if (cmd && k.getKeyCode() == 'A') { sel.clear(); for (auto& b : doc().blocks) sel.push_back(b.id); canvas->repaint(); side->update(); return true; }
    if (cmd && k.getKeyCode() == 'F') { askEd->grabKeyboardFocus(); return true; }
    if (k == juce::KeyPress::escapeKey) { if (!path.empty()) { path.pop_back(); sel.clear(); rebuild(); resized(); return true; } sel.clear(); selWire = -1; askBox->setVisible(false); canvas->repaint(); side->update(); return true; }
    return false;
}
void FluxoView::timerCallback() {
    if (!isShowing()) return;
    canvas->repaint();
    if (shell.host.fluxLogGen != lastLogGen) { lastLogGen = shell.host.fluxLogGen; console->repaint(); }
    if (auto* b = [&]() -> Pill* { for (auto* c : owned) if (c->getComponentID() == "fluxo:back") return dynamic_cast<Pill*>(c); return nullptr; }()) if (b->isVisible() != !path.empty()) { b->setVisible(!path.empty()); resized(); }
}
void FluxoView::paint(juce::Graphics& g) {
    Theme::backdrop(g, getLocalBounds());
    juce::String title = "FLUXO"; { FluxDoc* d = &root(); for (int p : path) { auto* b = d->block(p); if (!b || !b->own) break; title << U8("  ›  ") << U8(b->own->name.c_str()); d = b->own->inner.get(); } }
    ftxt(g, title, { barR.getX(), barR.getY() + 2, 260, 26 }, 13.0f, Theme::txt(), 2);
    ftxt(g, path.empty() ? juce::String("make anything from blocks") : juce::String("inside your plug-in"), { barR.getX(), barR.getY() + 28, 260, 16 }, 8.0f, Theme::sub(), 0);
    if (Theme::softUI()) softWellLite(g, askR.toFloat(), (float) askR.getHeight() * 0.5f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(askR.toFloat(), 12); }
    g.setColour(Theme::blue()); juce::Path sp; auto c = juce::Point<float>((float) askR.getX() + 22, (float) askR.getCentreY());
    sp.startNewSubPath(c.x, c.y - 8); sp.lineTo(c.x + 2.2f, c.y - 2.2f); sp.lineTo(c.x + 8, c.y); sp.lineTo(c.x + 2.2f, c.y + 2.2f); sp.lineTo(c.x, c.y + 8); sp.lineTo(c.x - 2.2f, c.y + 2.2f); sp.lineTo(c.x - 8, c.y); sp.lineTo(c.x - 2.2f, c.y - 2.2f); sp.closeSubPath(); g.fillPath(sp);
    int ix = 0, iy = 0; for (auto* p : owned) if (p->getComponentID() == "fluxo:into0") { ix = p->getX(); iy = p->getY(); }
    ftxt(g, "plays", { ix - 50, iy, 46, 38 }, 8.0f, Theme::sub(), 2, juce::Justification::centredRight);
}
void FluxoView::resized() {
    int W = getWidth(), H = getHeight();
    bool two = W < 1560;
    barR = { 20, 10, W - 40, two ? 98 : 50 };
    auto comp = [&](const char* id) -> juce::Component* { for (auto* c : owned) if (c->getComponentID() == juce::String("fluxo:") + id) return c; return nullptr; };
    int y0 = barR.getY() + 6, x = barR.getRight();
    for (auto* id : { "file", "undo", "sound" }) { int w = juce::String(id) == "file" ? 84 : juce::String(id) == "undo" ? 76 : 104; x -= w; comp(id)->setBounds(x, y0, w, 38); x -= 8; }
    int rowY = y0, lx;
    if (two) { askR = { barR.getX() + 270, y0, juce::jmax(220, x - barR.getX() - 280), 38 }; rowY = y0 + 48; lx = barR.getX(); }
    else lx = x - (90 + 8 + 76 + 70 + 8 + 50 + 4 * 70 + 8);
    auto put = [&](const char* id, int w) { auto* c = comp(id); if (!c || (!c->isVisible() && juce::String(id) == "back")) return; c->setBounds(lx, rowY, w, 38); lx += w; };
    put("back", 90); if (!path.empty()) lx += 8;
    put("chain", 76); put("free", 70); lx += 58;
    for (int i = 0; i < 4; ++i) put((juce::String("into") + juce::String(i)).toRawUTF8(), 70);
    if (!two) askR = { barR.getX() + 270, y0, juce::jmax(220, x - (90 + 8 + 76 + 70 + 8 + 50 + 4 * 70 + 8) - barR.getX() - 290), 38 };
    askEd->setBounds(askR.withTrimmedLeft(38).withTrimmedRight(14));
    int top = barR.getBottom() + 10, conH = 92, dw = W < 1100 ? 210 : 250, sw = W < 1100 ? 250 : 300;
    drawerVp.setBounds(12, top, dw, H - top - 12); drawer->setSize(dw - 8, drawer->layoutRows(dw - 8));
    side->setBounds(W - sw - 12, top, sw, H - top - conH - 20);
    console->setBounds(dw + 22, H - conH - 12, W - dw - 22 - 12, conH);
    vp.setBounds(dw + 22, top, W - dw - sw - 56, H - top - conH - 26);
    if (askBox->isVisible()) askBox->setBounds(askR.getX() - 12, askR.getBottom() - 8, juce::jmax(560, askR.getWidth()), askBox->getHeight());
    if (maker->isVisible()) maker->setBounds(getLocalBounds().withSizeKeepingCentre(560, juce::jmin(H - 20, 680)));
    side->update();
}
void FluxoView::demo(const juce::String& cmd) {
    auto arg = [&](const char* k) { auto i = cmd.indexOf(k); return i < 0 ? juce::String() : cmd.substring(i + (int) strlen(k)).upToFirstOccurrenceOf(" ", false, false); };
    if (arg("fex=").isNotEmpty()) { root() = FluxDoc(); addRecipe(arg("fex=").getIntValue(), false); sel.clear(); vp.setViewPosition(0, 0); }
    if (arg("fsel=").isNotEmpty()) { int k = arg("fsel=").getIntValue(); if (k >= 0 && k < (int) doc().blocks.size()) sel = { doc().blocks[(size_t) k].id }; }
    if (cmd.contains("fplug")) { sel.clear(); for (size_t i = 1; i + 1 < doc().blocks.size(); ++i) sel.push_back(doc().blocks[i].id); openPluginMaker(); }
    if (cmd.contains("finside")) { sel.clear(); for (size_t i = 1; i + 1 < doc().blocks.size(); ++i) sel.push_back(doc().blocks[i].id); makePlugin("Space Echo", { { doc().blocks[1].id, "back" } }, "violet"); }
    if (arg("fask=").isNotEmpty()) { askEd->setText(arg("fask=").replace("_", " "), true); }
    if (cmd.contains("fempty")) { root() = FluxDoc(); sel.clear(); rebuild(); }
    side->update(); resized(); repaint();
}

} // namespace arco
