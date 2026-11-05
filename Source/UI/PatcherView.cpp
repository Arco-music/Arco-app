// ARCO 2.4: PATCHER - its own screen: the canvas, the search, the help, the guide, the patches, the console.
#include "PatcherView.h"
#include "MainView.h"
#include "../App/AudioHost.h"
#include "../Engine/PatcherLib.h"

namespace arco {

using namespace px;
#ifndef ARCO_U8_HERE
#define ARCO_U8_HERE
static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
#endif
static void txt(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft, int lines = 1) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, lines, 0.9f); }
static juce::Colour sigCol() { return Theme::amber(); }

// ================================================================== geometry of a box
struct Geo { juce::Rectangle<float> r; BoxIO io; std::string cls; std::vector<Atom> args; bool ui = false; };
static float textW(const juce::String& s, float fs, int w = 1) { return juce::GlyphArrangement::getStringWidth(Theme::font(fs, w), s); }
static Geo geoOf(const PBox& b) {
    Geo g; g.io = boxIO(b); g.cls = g.io.cls;
    auto toks = tokenize(b.text.toStdString());
    for (size_t i = 1; i < toks.size(); ++i) { auto a = parseAtoms(toks[i]); if (!a.empty()) g.args.push_back(a[0]); }
    if (b.kind == "comment") {
        float w = b.w > 0 ? b.w : juce::jmin(380.0f, textW(b.text, 9.0f, 0) + 10);
        int lines = juce::jmax(1, (int) std::ceil(textW(b.text, 9.0f, 0) / juce::jmax(40.0f, w - 8)) + b.text.retainCharacters("\n").length());
        g.r = { b.x, b.y, w, 6.0f + 15.0f * (float) lines }; return g;
    }
    if (b.kind == "obj" && g.io.ui && b.sub == nullptr) { g.ui = true; g.r = { b.x, b.y, b.w > 0 ? b.w : (float) g.io.uiW, b.h > 0 ? b.h : (float) g.io.uiH }; return g; }
    int ports = juce::jmax(g.io.ins, g.io.outs);
    float w = juce::jmax(40.0f, textW(b.text, 9.5f, 1) + 20.0f, ports * 18.0f);
    if (b.kind == "msg") w += 6;
    g.r = { b.x, b.y, b.w > 0 ? juce::jmax(w, b.w) : w, 26.0f };
    return g;
}
static juce::Point<float> portPos(const Geo& g, bool out, int k) {
    int n = out ? g.io.outs : g.io.ins;
    float x = n <= 1 ? g.r.getX() + 8 : g.r.getX() + 8 + k * (g.r.getWidth() - 16) / (float) (n - 1);
    return { x, out ? g.r.getBottom() : g.r.getY() };
}
static double argD(const Geo& g, size_t k, double d) { return k < g.args.size() && g.args[k].num() ? g.args[k].d() : d; }

// ================================================================== the canvas
struct PatcherView::Canvas : public juce::Component, public juce::FileDragAndDropTarget {
    PatcherView& v;
    explicit Canvas(PatcherView& pv) : v(pv) { setWantsKeyboardFocus(true); setSize(3200, 2400); }
    // dragging
    enum Drag { None, Move, Cord, Band, Ui, Resize };
    Drag drag = None; juce::Point<float> p0, p1; int dragBox = -1, cordFrom = -1, cordOut = 0; bool movedAny = false;
    std::map<int, juce::Point<float>> startPos;
    double uiStart = 0; std::vector<float> msStart; int fnPoint = -1;
    // typing a box
    std::unique_ptr<juce::TextEditor> typer; int typingBox = -1; juce::String typingKind; juce::Point<float> typeAt; int pick = 0;
    std::vector<const ObjDef*> sugg;
    std::map<int, std::pair<int, double>> flashes;
    std::vector<PBox> clip; std::vector<PLine> clipLines;
    int hoverBox = -1; int hoverPort = -1; bool hoverOut = false;

    PatchDoc& D() { return v.cur(); }
    ObjPtr objOf(int id) { auto* r = v.rt(); return r ? r->objAt(v.keyOf(id)) : nullptr; }
    juce::String errOf(int id) { auto* r = v.rt(); return r ? r->boxErrorKey(v.keyOf(id)) : juce::String(); }
    bool isSel(int id) const { return std::find(v.sel.begin(), v.sel.end(), id) != v.sel.end(); }

    // ---------------------------------------------------------------- drawing
    void paintCord(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, bool sig, bool selected, float alpha = 1) {
        juce::Path p; p.startNewSubPath(a);
        float dy = juce::jmax(24.0f, std::abs(b.y - a.y) * 0.45f);
        p.cubicTo(a + juce::Point<float>(0, dy), b - juce::Point<float>(0, dy), b);
        auto col = selected ? Theme::blue() : sig ? sigCol() : Theme::sub().withMultipliedAlpha(0.85f);
        if (sig) {
            g.setColour(col.withAlpha(0.95f * alpha)); g.strokePath(p, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path dash; float d[2] = { 4.0f, 5.0f }; juce::PathStrokeType(1.2f).createDashedStroke(dash, p, d, 2);
            g.setColour(juce::Colours::white.withAlpha(0.45f * alpha)); g.fillPath(dash);
        } else { g.setColour(col.withAlpha(alpha)); g.strokePath(p, juce::PathStrokeType(selected ? 2.4f : 1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    }
    juce::Path cordPath(juce::Point<float> a, juce::Point<float> b) { juce::Path p; p.startNewSubPath(a); float dy = juce::jmax(24.0f, std::abs(b.y - a.y) * 0.45f); p.cubicTo(a + juce::Point<float>(0, dy), b - juce::Point<float>(0, dy), b); return p; }
    void paint(juce::Graphics& g) override {
        g.fillAll(Theme::bg());
        if (v.editing) { g.setColour(Theme::line().withMultipliedAlpha(0.45f)); for (int y = 0; y < getHeight(); y += 20) for (int x = 0; x < getWidth(); x += 20) g.fillRect((float) x, (float) y, 1.4f, 1.4f); }
        auto& d = D();
        std::map<int, Geo> geo; for (auto& b : d.boxes) geo[b.id] = geoOf(b);
        // panels behind everything
        for (auto& b : d.boxes) if (geo[b.id].cls == "panel") { auto r = geo[b.id].r; if (Theme::softUI()) softWellLite(g, r, 16); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 10); } }
        // cords
        for (int li = 0; li < (int) d.lines.size(); ++li) { auto& l = d.lines[(size_t) li];
            auto a = geo.find(l.from), b = geo.find(l.to); if (a == geo.end() || b == geo.end()) continue;
            if (l.outlet >= a->second.io.outs || l.inlet >= b->second.io.ins) continue;
            bool sig = l.outlet < 64 && ((a->second.io.sigOut >> l.outlet) & 1ULL);
            paintCord(g, portPos(a->second, true, l.outlet), portPos(b->second, false, l.inlet), sig, li == v.selLine); }
        for (auto& b : d.boxes) if (geo[b.id].cls != "panel") paintBox(g, b, geo[b.id]);
        if (drag == Cord) { auto it = geo.find(cordFrom); if (it != geo.end()) paintCord(g, portPos(it->second, true, cordOut), p1, ((it->second.io.sigOut >> cordOut) & 1ULL) != 0, false, 0.8f); }
        if (drag == Band) { auto r = juce::Rectangle<float>(p0, p1); g.setColour(Theme::blue().withAlpha(0.08f)); g.fillRoundedRectangle(r, 6); g.setColour(Theme::blue().withAlpha(0.5f)); g.drawRoundedRectangle(r, 6, 1.2f); }
        if (d.boxes.empty() && !typer) {
            auto r = juce::Rectangle<int>(60, 60, 760, 160);
            txt(g, "An empty patch", r.removeFromTop(30), 15.0f, Theme::txt(), 2);
            txt(g, U8("Double-click anywhere (or press N) and type what you want - a box appears.\nOr search on the left: \"sine\", \"echo\", \"random notes\" - and click.\nOr open a ready-made patch (PATCHES, on the right) and change it."), r, 10.0f, Theme::sub(), 0, juce::Justification::topLeft, 4);
        }
    }
    void paintPorts(juce::Graphics& g, const Geo& ge, int id) {
        for (int o = 0; o < 2; ++o) { int n = o ? ge.io.outs : ge.io.ins;
            for (int k = 0; k < n; ++k) {
                auto p = portPos(ge, o == 1, k); bool sig = k < 64 && (((o ? ge.io.sigOut : ge.io.sigIn) >> k) & 1ULL);
                bool hot = hoverBox == id && hoverPort == k && hoverOut == (o == 1);
                auto r = juce::Rectangle<float>(p.x - 6, p.y - (o ? 3.5f : 3.0f), 12, 6.5f);
                g.setColour(hot ? Theme::blue() : sig ? sigCol() : (k == 0 && !o ? Theme::txt().withAlpha(0.75f) : Theme::sub().withAlpha(0.8f)));
                g.fillRoundedRectangle(r, 3.0f);
            } }
    }
    void paintBox(juce::Graphics& g, const PBox& b, const Geo& ge) {
        auto r = ge.r; bool s = isSel(b.id); juce::String err = errOf(b.id); bool bad = err.isNotEmpty() || (!ge.io.ok && b.kind == "obj");
        if (b.kind == "comment") {
            if (v.editing && s) { g.setColour(Theme::blue().withAlpha(0.12f)); g.fillRoundedRectangle(r.expanded(3), 6); }
            txt(g, b.text, r.toNearestInt().reduced(4, 2), 9.0f, Theme::sub(), 0, juce::Justification::topLeft, 12); return;
        }
        if (typer && typingBox == b.id) return;
        if (ge.ui) { paintUi(g, b, ge, s); paintPorts(g, ge, b.id); return; }
        float rad = 9.0f;
        auto face = b.kind == "msg" ? Theme::btn() : Theme::card();
        if (Theme::softUI()) softRaised(g, r, rad, 2.6f, face); else { g.setColour(face); g.fillRoundedRectangle(r, 6); }
        if (b.kind == "msg") {      // a message: a key you press - flashes when it sends
            auto f = flashes[b.id]; auto o = objOf(b.id); int fl = o ? o->uiFlash.load() : 0;
            if (fl != f.first) { flashes[b.id] = { fl, juce::Time::getMillisecondCounterHiRes() }; }
            double since = juce::Time::getMillisecondCounterHiRes() - flashes[b.id].second;
            if (since < 180) { g.setColour(Theme::blue().withAlpha(0.25f * (float) (1 - since / 180))); g.fillRoundedRectangle(r, rad); }
            g.setColour(Theme::blue().withAlpha(0.55f)); g.fillRoundedRectangle(r.getX() + 3, r.getY() + 6, 3, r.getHeight() - 12, 1.5f);
        }
        if (b.sub) { g.setColour(Theme::violet().withAlpha(0.6f)); g.fillRoundedRectangle(r.getX() + 3, r.getY() + 6, 3, r.getHeight() - 12, 1.5f); }
        bool sigBox = ge.io.sigIn != 0 || ge.io.sigOut != 0;
        if (sigBox && b.kind == "obj") { g.setColour(sigCol().withAlpha(0.14f)); g.fillRoundedRectangle(r.reduced(1), rad); }
        if (bad) { g.setColour(Theme::red().withAlpha(0.85f)); juce::Path p; p.addRoundedRectangle(r.reduced(0.5f), rad); juce::Path dsh; float dl[2] = { 4, 3 }; juce::PathStrokeType(1.4f).createDashedStroke(dsh, p, dl, 2); g.fillPath(dsh); }
        if (s) { g.setColour(Theme::blue()); g.drawRoundedRectangle(r.expanded(1.5f), rad + 1.5f, 1.6f); }
        auto label = b.kind == "msg" ? (([&] { auto o = objOf(b.id); return o ? o->uiText() : b.text; })()) : b.text;
        if (label.isEmpty()) label = b.text;
        txt(g, label, r.toNearestInt().withTrimmedLeft(b.kind == "msg" || b.sub ? 11 : 8).withTrimmedRight(6), 9.5f, bad ? Theme::ink(Theme::red()) : Theme::txt(), 1);
        paintPorts(g, ge, b.id);
    }
    // ---- the boxes you touch
    void paintUi(juce::Graphics& g, const PBox& b, const Geo& ge, bool s) {
        auto r = ge.r; auto o = objOf(b.id); double val = o ? o->uiValue.load() : 0; const auto& c = ge.cls;
        auto well = [&](juce::Rectangle<float> x, float rad) { if (Theme::softUI()) softWellLite(g, x, rad); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(x, rad); } };
        auto key = [&](juce::Rectangle<float> x, float rad, bool on, juce::Colour col) { if (Theme::softUI()) softKeyLite(g, x, rad, on, col, on ? 0.55f : 0.0f); else { g.setColour(on ? col : Theme::btn()); g.fillRoundedRectangle(x, rad); } };
        auto flash = [&]() { int fl = o ? o->uiFlash.load() : 0; auto& f = flashes[b.id]; if (fl != f.first) f = { fl, juce::Time::getMillisecondCounterHiRes() }; return juce::Time::getMillisecondCounterHiRes() - f.second; };
        if (c == "button") { double t = flash(); key(r, r.getHeight() * 0.5f, t < 150, Theme::amber()); g.setColour(Theme::sub().withAlpha(0.5f)); g.drawEllipse(r.reduced(8), 1.5f); }
        else if (c == "toggle") { bool on = val > 0.5; key(r, 8, on, Theme::green()); if (!on) { g.setColour(Theme::sub().withAlpha(0.45f)); g.drawRoundedRectangle(r.reduced(7), 4, 1.4f); } if (on) { g.setColour(Theme::ink(Theme::green())); juce::Path x; x.startNewSubPath(r.getX() + 8, r.getCentreY()); x.lineTo(r.getCentreX() - 1, r.getBottom() - 8); x.lineTo(r.getRight() - 7, r.getY() + 8); g.strokePath(x, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); } }
        else if (c == "number" || c == "flonum" || c == "number~") {
            well(r, 8); g.setColour(c == "number~" ? sigCol() : Theme::blue()); juce::Path tri; tri.addTriangle(r.getX() + 7, r.getY() + 8, r.getX() + 7, r.getBottom() - 8, r.getX() + 13, r.getCentreY()); g.fillPath(tri);
            juce::String t = c == "number" ? juce::String((long long) std::llround(std::trunc(val))) : juce::String(Atom::F(val).str());
            txt(g, t, r.toNearestInt().withTrimmedLeft(17).withTrimmedRight(4), 9.5f, Theme::txt(), 1); }
        else if (c == "slider" || c == "hslider" || c == "gain~") {
            double lo = c == "gain~" ? 0 : argD(ge, 0, 0), hi = c == "gain~" ? 1 : argD(ge, 1, ge.args.empty() ? 127 : 1); double t = hi == lo ? 0 : juce::jlimit(0.0, 1.0, (val - lo) / (hi - lo));
            bool horiz = c == "hslider"; well(r, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
            auto col = c == "gain~" ? sigCol() : Theme::blue();
            if (horiz) { auto f = r.reduced(4).withWidth((r.getWidth() - 8) * (float) t); g.setColour(col.withAlpha(0.35f)); g.fillRoundedRectangle(f, f.getHeight() * 0.5f); key({ r.getX() + 2 + (r.getWidth() - 26) * (float) t, r.getY() + 2, 22, r.getHeight() - 4 }, 10, false, col); }
            else { auto f = r.reduced(4); f = f.removeFromBottom(f.getHeight() * (float) t); g.setColour(col.withAlpha(0.35f)); g.fillRoundedRectangle(f, f.getWidth() * 0.5f); key({ r.getX() + 2, r.getBottom() - 24 - (r.getHeight() - 26) * (float) t, r.getWidth() - 4, 22 }, 10, false, col); }
        }
        else if (c == "dial") {
            double lo = argD(ge, 0, 0), hi = argD(ge, 1, ge.args.empty() ? 127 : 1); float t = (float) (hi == lo ? 0 : juce::jlimit(0.0, 1.0, (val - lo) / (hi - lo)));
            auto k = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()) - 10, juce::jmin(r.getWidth(), r.getHeight()) - 10);
            if (Theme::softUI()) softRaised(g, k.reduced(6), k.getWidth() * 0.5f - 6, 2.5f, Theme::btn()); else { g.setColour(Theme::btn()); g.fillEllipse(k.reduced(6)); }
            const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f; float rad = k.getWidth() * 0.5f;
            juce::Path tr; tr.addCentredArc(k.getCentreX(), k.getCentreY(), rad, rad, 0, a0, a1, true); g.setColour(Theme::line()); g.strokePath(tr, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path va; va.addCentredArc(k.getCentreX(), k.getCentreY(), rad, rad, 0, a0, a0 + (a1 - a0) * t, true); g.setColour(Theme::arc(Theme::blue())); g.strokePath(va, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            float a = a0 + (a1 - a0) * t; g.setColour(Theme::txt()); g.fillEllipse(k.getCentreX() + std::sin(a) * rad * 0.45f - 2, k.getCentreY() - std::cos(a) * rad * 0.45f - 2, 4, 4);
            txt(g, ge.args.size() >= 2 && (ge.args[0].t == 'f' || ge.args[1].t == 'f') ? juce::String(val, 2) : juce::String((int) std::round(val)), r.toNearestInt().removeFromBottom(10).translated(0, 9), 7.0f, Theme::sub(), 1, juce::Justification::centred);
        }
        else if (c == "kslider") {
            int lo = 36, n = 48; int white = 0; for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10) ++white; }
            float ww = r.getWidth() / white; int wi = 0; int note = (int) val;
            well(r, 6);
            for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; if (blk) continue;
                auto k = juce::Rectangle<float>(r.getX() + wi * ww + 1, r.getY() + 1, ww - 2, r.getHeight() - 2); g.setColour(lo + i == note ? Theme::blue() : juce::Colours::white.withAlpha(0.92f)); g.fillRoundedRectangle(k, 3); ++wi; }
            wi = 0;
            for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; bool blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; if (!blk) { ++wi; continue; }
                auto k = juce::Rectangle<float>(r.getX() + wi * ww - ww * 0.3f, r.getY() + 1, ww * 0.6f, r.getHeight() * 0.6f); g.setColour(lo + i == note ? Theme::blue() : juce::Colour(0xff2a2c33)); g.fillRoundedRectangle(k, 2.5f); }
        }
        else if (c == "multislider") {
            well(r, 8); float a[128]; int n = o ? o->uiArray(a, 128) : (int) argD(ge, 0, 8);
            if (!o) for (int i = 0; i < n; ++i) a[i] = 0;
            float bw = (r.getWidth() - 8) / juce::jmax(1, n);
            for (int i = 0; i < n; ++i) { float h = (r.getHeight() - 8) * juce::jlimit(0.0f, 1.0f, a[i]); g.setColour(Theme::blue().withAlpha(0.75f)); g.fillRoundedRectangle(r.getX() + 4 + i * bw + 1, r.getBottom() - 4 - h, bw - 2, juce::jmax(2.0f, h), 2.5f); }
        }
        else if (c == "umenu") {
            key(r, r.getHeight() * 0.5f, false, Theme::blue());
            auto items = o ? juce::StringArray::fromLines(o->uiText()) : juce::StringArray(); items.removeEmptyStrings(); int ix = (int) val;
            txt(g, (ix >= 0 && ix < items.size() ? items[ix] : juce::String("(empty)")) + U8("  ▾"), r.toNearestInt().reduced(12, 0), 9.0f, Theme::txt(), 1);
        }
        else if (c == "radiogroup") {
            int n = (int) argD(ge, 0, 4); float h = r.getHeight() / n;
            for (int i = 0; i < n; ++i) { auto k = juce::Rectangle<float>(r.getX() + 3, r.getY() + i * h + 3, r.getWidth() - 6, h - 6); key(k, k.getHeight() * 0.5f, (int) val == i, Theme::blue()); }
        }
        else if (c == "function") {
            well(r, 10); float a[128]; int n = o ? o->uiArray(a, 128) : 0; juce::Path p; auto in = r.reduced(8);
            for (int i = 0; i + 1 < n; i += 2) { float x = in.getX() + a[i] * in.getWidth(), y = in.getBottom() - a[i + 1] * in.getHeight(); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
            g.setColour(Theme::blue()); g.strokePath(p, juce::PathStrokeType(2.0f));
            for (int i = 0; i + 1 < n; i += 2) { float x = in.getX() + a[i] * in.getWidth(), y = in.getBottom() - a[i + 1] * in.getHeight(); g.setColour(Theme::txt()); g.fillEllipse(x - 3.5f, y - 3.5f, 7, 7); }
        }
        else if (c == "ezdac~" || c == "ezadc~") {
            bool on = val > 0.5; key(r, r.getHeight() * 0.5f, on, sigCol());
            auto ic = r.reduced(r.getWidth() * 0.27f); g.setColour(on ? Theme::ink(sigCol()) : Theme::sub());
            if (c == "ezdac~") { juce::Path sp; sp.startNewSubPath(ic.getX(), ic.getY() + ic.getHeight() * 0.35f); sp.lineTo(ic.getX() + ic.getWidth() * 0.3f, ic.getY() + ic.getHeight() * 0.35f);
                sp.lineTo(ic.getX() + ic.getWidth() * 0.6f, ic.getY()); sp.lineTo(ic.getX() + ic.getWidth() * 0.6f, ic.getBottom()); sp.lineTo(ic.getX() + ic.getWidth() * 0.3f, ic.getY() + ic.getHeight() * 0.65f);
                sp.lineTo(ic.getX(), ic.getY() + ic.getHeight() * 0.65f); sp.closeSubPath(); g.fillPath(sp);
                if (on) { juce::Path w; w.addCentredArc(ic.getX() + ic.getWidth() * 0.62f, ic.getCentreY(), ic.getWidth() * 0.35f, ic.getWidth() * 0.35f, 0, 0.6f, 2.5f, true); g.strokePath(w, juce::PathStrokeType(1.6f)); } }
            else { g.fillRoundedRectangle(ic.getCentreX() - 4, ic.getY(), 8, ic.getHeight() * 0.62f, 4); g.drawLine(ic.getCentreX(), ic.getY() + ic.getHeight() * 0.7f, ic.getCentreX(), ic.getBottom(), 1.6f); }
        }
        else if (c == "meter~") { well(r, 6); float lv = juce::jlimit(0.0f, 1.0f, std::sqrt((float) val)); auto f = r.reduced(3); f = f.removeFromBottom(f.getHeight() * lv);
            juce::ColourGradient gr(Theme::green(), f.getX(), r.getBottom(), Theme::red(), f.getX(), r.getY(), false); gr.addColour(0.7, Theme::amber()); g.setGradientFill(gr); g.fillRoundedRectangle(f, 3); }
        else if (c == "scope~") { well(r, 10); float a[256]; int n = o ? o->uiArray(a, 256) : 0; juce::Path p; auto in = r.reduced(6);
            g.setColour(Theme::line()); g.drawHorizontalLine((int) in.getCentreY(), in.getX(), in.getRight());
            for (int i = 0; i < n; ++i) { float x = in.getX() + i * in.getWidth() / (n - 1), y = in.getCentreY() - juce::jlimit(-1.0f, 1.0f, a[i]) * in.getHeight() * 0.48f; if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
            g.setColour(sigCol()); g.strokePath(p, juce::PathStrokeType(1.6f)); }
        else if (c == "spectroscope~") { well(r, 10); float a[1024]; int n = o ? o->uiArray(a, 1024) : 0; auto in = r.reduced(6);
            if (n == 1024) { const int B = 48; for (int k = 0; k < B; ++k) { double f = std::pow(2.0, 1 + k * 8.5 / B); double re = 0, im = 0; double w = TWO_PI * f / 1024.0 * 1.0;
                    for (int i = 0; i < 1024; i += 2) { double win = 0.5 - 0.5 * std::cos(TWO_PI * i / 1023.0); re += a[i] * win * std::cos(w * i); im += a[i] * win * std::sin(w * i); }
                    float m = (float) std::sqrt(re * re + im * im) / 128.0f; float db = juce::jlimit(0.0f, 1.0f, (20 * std::log10(m + 1e-6f) + 60) / 60);
                    float bw = in.getWidth() / B; g.setColour(sigCol().withAlpha(0.8f)); g.fillRoundedRectangle(in.getX() + k * bw + 1, in.getBottom() - db * in.getHeight(), bw - 2, db * in.getHeight(), 1.5f); } } }
        else if (c == "waveform~") { well(r, 10); auto* rt = v.rt(); BufPtr bp; if (rt && !ge.args.empty() && ge.args[0].t == 's') { const juce::SpinLock::ScopedLockType sl(rt->bufLock); auto it = rt->bufs.find(ge.args[0].s); if (it != rt->bufs.end()) bp = it->second; }
            auto in = r.reduced(6);
            if (bp && bp->frames() > 1) { long N = bp->frames(); int W = (int) in.getWidth(); g.setColour(Theme::blue().withAlpha(0.8f));
                for (int x = 0; x < W; ++x) { long a0 = N * x / W, a1 = juce::jmax(a0 + 1, N * (x + 1) / W); float mx = 0; for (long i = a0; i < a1; i += juce::jmax(1L, (a1 - a0) / 64)) mx = juce::jmax(mx, std::abs(bp->ch[0][(size_t) i])); float h = juce::jmin(1.0f, mx) * in.getHeight() * 0.5f; g.fillRect(in.getX() + x, in.getCentreY() - h, 1.0f, juce::jmax(1.0f, h * 2)); } }
            else txt(g, "drop a sound file here", in.toNearestInt(), 8.0f, Theme::sub(), 0, juce::Justification::centred); }
        if (s) { g.setColour(Theme::blue()); g.drawRoundedRectangle(r.expanded(2), 8, 1.6f); }
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (!typer) return;
        int y = typer->getBottom() + 4; int x = typer->getX();
        for (int i = 0; i < (int) sugg.size() && i < 8; ++i) {
            auto r = juce::Rectangle<float>((float) x, (float) y, 380, 32);
            if (Theme::softUI()) softRaised(g, r, 12, 2.5f, i == pick ? Theme::btn() : Theme::card()); else { g.setColour(i == pick ? Theme::sel() : Theme::card()); g.fillRoundedRectangle(r, 8); }
            if (i == pick) { g.setColour(Theme::blue().withAlpha(0.6f)); g.drawRoundedRectangle(r, 12, 1.4f); }
            txt(g, sugg[(size_t) i]->name, r.toNearestInt().withTrimmedLeft(12).withWidth(118), 9.5f, Theme::txt(), 2);
            txt(g, sugg[(size_t) i]->desc, r.toNearestInt().withTrimmedLeft(132).withTrimmedRight(8), 7.2f, Theme::sub());
            y += 35;
        }
        if (!sugg.empty()) txt(g, U8("↑ ↓ choose · Return or Tab takes it · Esc cancels"), { x, y, 380, 16 }, 7.0f, Theme::sub());
    }
    // ---------------------------------------------------------------- hits
    int boxAt(juce::Point<float> p) { auto& d = D(); for (int i = (int) d.boxes.size(); --i >= 0;) { auto ge = geoOf(d.boxes[(size_t) i]); if (ge.r.expanded(2).contains(p) && !(ge.cls == "panel" && !v.editing)) return d.boxes[(size_t) i].id; } return -1; }
    bool portAt(juce::Point<float> p, int& box, int& port, bool& out) {
        for (auto& b : D().boxes) { auto ge = geoOf(b);
            for (int o = 0; o < 2; ++o) for (int k = 0; k < (o ? ge.io.outs : ge.io.ins); ++k) if (portPos(ge, o == 1, k).getDistanceFrom(p) < 8.0f) { box = b.id; port = k; out = o == 1; return true; } }
        return false;
    }
    int lineAt(juce::Point<float> p) {
        auto& d = D();
        for (int li = 0; li < (int) d.lines.size(); ++li) { auto& l = d.lines[(size_t) li]; auto* a = d.box(l.from); auto* b = d.box(l.to); if (!a || !b) continue;
            auto ga = geoOf(*a), gb = geoOf(*b); if (l.outlet >= ga.io.outs || l.inlet >= gb.io.ins) continue;
            auto path = cordPath(portPos(ga, true, l.outlet), portPos(gb, false, l.inlet)); juce::Point<float> np; path.getNearestPoint(p, np); if (np.getDistanceFrom(p) < 5.0f) return li; }
        return -1;
    }
    // ---------------------------------------------------------------- the mouse
    void mouseMove(const juce::MouseEvent& e) override { int b, k; bool o; int ob = hoverBox, op = hoverPort; if (v.editing && portAt(e.position, b, k, o)) { hoverBox = b; hoverPort = k; hoverOut = o; } else { hoverBox = -1; hoverPort = -1; } if (ob != hoverBox || op != hoverPort) repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus(); setViewportIgnoreDragFlag(true);
        if (typer) commitTyper();
        p0 = p1 = e.position; movedAny = false; v.selLine = -1;
        if (e.mods.isPopupMenu()) { popup(e.position); return; }
        bool operate = !v.editing || e.mods.isCommandDown();
        int id = boxAt(e.position);
        if (operate && id >= 0) { uiDown(id, e); return; }
        if (!v.editing) return;
        int pb, pk; bool po;
        if (portAt(e.position, pb, pk, po) && po) { drag = Cord; cordFrom = pb; cordOut = pk; repaint(); return; }
        if (id >= 0) {
            auto* b = D().box(id); auto ge = geoOf(*b);
            if (ge.ui && e.position.x > ge.r.getRight() - 8 && e.position.y > ge.r.getBottom() - 8) { drag = Resize; dragBox = id; startPos[id] = { ge.r.getWidth(), ge.r.getHeight() }; return; }
            if (e.mods.isShiftDown()) { if (isSel(id)) v.sel.erase(std::find(v.sel.begin(), v.sel.end(), id)); else v.sel.push_back(id); }
            else if (!isSel(id)) v.sel = { id };
            if (e.mods.isAltDown()) { copySel(); pasteAt(juce::Point<float>(b->x + 16, b->y + 16)); }
            drag = Move; dragBox = id; startPos.clear(); for (int s : v.sel) if (auto* x = D().box(s)) startPos[s] = { x->x, x->y };
            v.helpFor = ge.cls; v.rightTab = 0; v.repaint(); repaint(); return;
        }
        int li = lineAt(e.position); if (li >= 0) { v.selLine = li; v.sel.clear(); repaint(); return; }
        v.sel.clear(); drag = Band; repaint(); v.repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        p1 = e.position;
        if (drag == Ui) { uiDrag(e); return; }
        if (drag == Cord) { int b, k; bool o; hoverBox = -1; if (portAt(p1, b, k, o) && !o) { hoverBox = b; hoverPort = k; hoverOut = false; } repaint(); return; }
        if (drag == Band) { v.sel.clear(); auto r = juce::Rectangle<float>(p0, p1); for (auto& b : D().boxes) if (r.intersects(geoOf(b).r)) v.sel.push_back(b.id); repaint(); return; }
        if (drag == Resize) { if (auto* b = D().box(dragBox)) { auto s = startPos[dragBox]; b->w = juce::jmax(20.0f, s.x + (p1.x - p0.x)); b->h = juce::jmax(18.0f, s.y + (p1.y - p0.y)); movedAny = true; } repaint(); return; }
        if (drag == Move) {
            auto dlt = p1 - p0; if (!movedAny && dlt.getDistanceFromOrigin() < 3) return;
            if (!movedAny) v.snapshot();
            movedAny = true;
            for (auto& [id, st] : startPos) if (auto* b = D().box(id)) { b->x = juce::jmax(0.0f, std::round((st.x + dlt.x) / 5.0f) * 5.0f); b->y = juce::jmax(0.0f, std::round((st.y + dlt.y) / 5.0f) * 5.0f); }
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (drag == Ui) { uiUp(e); drag = None; return; }
        if (drag == Cord) {
            int b, k; bool o;
            if (portAt(e.position, b, k, o) && !o && b != cordFrom) { v.snapshot(); D().connect(cordFrom, cordOut, b, k); v.changed(); }
            else if (!o) { int id = boxAt(e.position); if (id >= 0 && id != cordFrom) { auto ge = geoOf(*D().box(id)); if (ge.io.ins > 0) { int best = 0; float bd = 1e9f; for (int i = 0; i < ge.io.ins; ++i) { float d = std::abs(portPos(ge, false, i).x - e.position.x); if (d < bd) { bd = d; best = i; } } v.snapshot(); D().connect(cordFrom, cordOut, id, best); v.changed(); } } }
        }
        if ((drag == Move || drag == Resize) && movedAny) v.changed(false);
        drag = None; hoverBox = -1; repaint(); v.repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!v.editing) return;
        int id = boxAt(e.position);
        if (id < 0) { openTyper({ e.position.x, e.position.y }, "obj", -1, ""); return; }
        auto* b = D().box(id);
        if (b->sub) { v.path.push_back(id); v.sel.clear(); repaint(); v.repaint(); return; }
        if (geoOf(*b).ui && b->kind == "obj") return;
        openTyper({ b->x, b->y }, b->kind, id, b->text);
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        int id = boxAt(e.position); auto* b = id >= 0 ? D().box(id) : nullptr;
        if (b && !v.editing) { auto ge = geoOf(*b); if (ge.cls == "number" || ge.cls == "flonum" || ge.cls == "dial" || ge.cls == "slider") { auto o = objOf(id); double val = o ? o->uiValue.load() : 0; double step = ge.cls == "flonum" ? 0.05 : 1; send(id, ge.cls == "flonum" || (ge.args.size() >= 2 && ge.args[0].t == 'f') ? Msg::f(val + (w.deltaY > 0 ? step : -step)) : Msg::i(val + (w.deltaY > 0 ? 1 : -1))); return; } }
        Component::mouseWheelMove(e, w);
    }
    void send(int id, const Msg& m, int inlet = 0) { if (auto* r = v.rt()) r->postKey(v.keyOf(id), inlet, m); }
    // ---- operating the boxes you touch
    void uiDown(int id, const juce::MouseEvent& e) {
        auto* b = D().box(id); auto ge = geoOf(*b); auto o = objOf(id); const auto& c = ge.cls; dragBox = id; drag = Ui;
        uiStart = o ? o->uiValue.load() : 0;
        if (b->kind == "msg") { send(id, Msg::bang()); if (o) o->uiFlash++; drag = None; return; }
        if (b->sub) { v.path.push_back(id); v.sel.clear(); drag = None; repaint(); v.repaint(); return; }
        if (c == "button") { send(id, Msg::bang()); drag = None; return; }
        if (c == "toggle") { send(id, Msg::i(uiStart > 0.5 ? 0 : 1)); drag = None; return; }
        if (c == "ezdac~" || c == "ezadc~") { send(id, Msg::i(uiStart > 0.5 ? 0 : 1)); drag = None; return; }
        if (c == "umenu") { auto items = o ? juce::StringArray::fromLines(o->uiText()) : juce::StringArray(); items.removeEmptyStrings(); juce::PopupMenu pm; for (int i = 0; i < items.size(); ++i) pm.addItem(i + 1, items[i], true, (int) uiStart == i);
            pm.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(ge.r.toNearestInt())), [this, id](int x) { if (x > 0) send(id, Msg::i(x - 1)); }); drag = None; return; }
        if (c == "radiogroup") { int n = (int) argD(ge, 0, 4); send(id, Msg::i(juce::jlimit(0, n - 1, (int) ((e.position.y - ge.r.getY()) / (ge.r.getHeight() / n))))); drag = None; return; }
        if (c == "kslider") { int note = keyAt(ge, e.position); if (note >= 0) { send(id, Msg::i(100), 1); send(id, Msg::i(note)); fnPoint = note; } return; }
        if (c == "multislider") { msStart.clear(); float a[128]; int n = o ? o->uiArray(a, 128) : 0; msStart.assign(a, a + n); uiDrag(e); return; }
        if (c == "function") { float a[128]; int n = o ? o->uiArray(a, 128) : 0; auto in = ge.r.reduced(8); fnPoint = -1; msStart.assign(a, a + n);
            for (int i = 0; i + 1 < n; i += 2) { juce::Point<float> pt(in.getX() + a[i] * in.getWidth(), in.getBottom() - a[i + 1] * in.getHeight()); if (pt.getDistanceFrom(e.position) < 8) fnPoint = i / 2; }
            if (e.mods.isPopupMenu() || e.mods.isRightButtonDown()) { if (fnPoint > 0 && fnPoint < n / 2 - 1) { msStart.erase(msStart.begin() + fnPoint * 2, msStart.begin() + fnPoint * 2 + 2); sendPoints(id); } drag = None; return; }
            if (fnPoint < 0) { float t = juce::jlimit(0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth()), y = juce::jlimit(0.0f, 1.0f, (in.getBottom() - e.position.y) / in.getHeight());
                int at = 0; while (at * 2 < (int) msStart.size() && msStart[(size_t) at * 2] < t) ++at; msStart.insert(msStart.begin() + at * 2, { t, y }); fnPoint = at; sendPoints(id); }
            return; }
        if (c == "slider" || c == "hslider" || c == "gain~") { uiDrag(e); return; }
    }
    int keyAt(const Geo& ge, juce::Point<float> p) {
        int lo = 36, n = 48; std::vector<int> whites; for (int i = 0; i < n; ++i) { int pc = (lo + i) % 12; if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10) whites.push_back(lo + i); }
        float ww = ge.r.getWidth() / (float) whites.size(); int wi = (int) ((p.x - ge.r.getX()) / ww); if (wi < 0 || wi >= (int) whites.size()) return -1;
        int note = whites[(size_t) wi];
        if (p.y < ge.r.getY() + ge.r.getHeight() * 0.6f) { float fx = (p.x - ge.r.getX()) / ww - wi; int pc = note % 12;
            if (fx < 0.3f && pc != 0 && pc != 5) note -= 1; else if (fx > 0.7f && pc != 4 && pc != 11) note += 1; }
        return note;
    }
    void sendPoints(int id) { Msg m; m.sel = sym("points"); for (float x : msStart) m.push(Atom::F(x)); send(id, m); }
    void uiDrag(const juce::MouseEvent& e) {
        auto* b = D().box(dragBox); if (!b) return; auto ge = geoOf(*b); const auto& c = ge.cls;
        if (c == "number" || c == "flonum") { double step = c == "flonum" ? (e.mods.isShiftDown() ? 0.001 : 0.01) : (e.mods.isShiftDown() ? 0.2 : 1.0); double v2 = uiStart - (e.position.y - p0.y) * step;
            send(dragBox, c == "flonum" ? Msg::f(v2) : Msg::i(std::round(v2))); return; }
        if (c == "dial") { double lo = argD(ge, 0, 0), hi = argD(ge, 1, ge.args.empty() ? 127 : 1); double v2 = juce::jlimit(std::min(lo, hi), std::max(lo, hi), uiStart - (e.position.y - p0.y) * (hi - lo) / 200.0); send(dragBox, Msg::f(v2)); return; }
        if (c == "slider" || c == "hslider" || c == "gain~") { double lo = c == "gain~" ? 0 : argD(ge, 0, 0), hi = c == "gain~" ? 1 : argD(ge, 1, ge.args.empty() ? 127 : 1);
            double t = c == "hslider" ? (e.position.x - ge.r.getX() - 12) / (ge.r.getWidth() - 24) : (ge.r.getBottom() - 12 - e.position.y) / (ge.r.getHeight() - 24);
            send(dragBox, Msg::f(lo + juce::jlimit(0.0, 1.0, t) * (hi - lo))); return; }
        if (c == "multislider") { int n = (int) msStart.size(); if (n == 0) return; double lo = argD(ge, 1, 0), hi = argD(ge, 2, 127); if (ge.args.size() < 3) { lo = 0; hi = ge.args.size() == 2 ? argD(ge, 1, 127) : 127; }
            int k = juce::jlimit(0, n - 1, (int) ((e.position.x - ge.r.getX() - 4) / ((ge.r.getWidth() - 8) / n))); float t = juce::jlimit(0.0f, 1.0f, (ge.r.getBottom() - 4 - e.position.y) / (ge.r.getHeight() - 8));
            msStart[(size_t) k] = t; Msg m; m.sel = S::list; for (float x : msStart) m.push(Atom::F(lo + x * (hi - lo))); if (m.n == 1) m = Msg::f(m.a[0].d()); send(dragBox, m); return; }
        if (c == "function" && fnPoint >= 0) { auto in = ge.r.reduced(8); int n = (int) msStart.size() / 2; float t = juce::jlimit(0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth()), y = juce::jlimit(0.0f, 1.0f, (in.getBottom() - e.position.y) / in.getHeight());
            if (fnPoint == 0) t = 0; if (fnPoint == n - 1) t = 1; if (fnPoint > 0) t = juce::jmax(t, msStart[(size_t) (fnPoint - 1) * 2]); if (fnPoint < n - 1) t = juce::jmin(t, msStart[(size_t) (fnPoint + 1) * 2]);
            msStart[(size_t) fnPoint * 2] = t; msStart[(size_t) fnPoint * 2 + 1] = y; sendPoints(dragBox); return; }
        if (c == "kslider") { int note = keyAt(ge, e.position); if (note >= 0 && note != fnPoint) { if (fnPoint >= 0) { send(dragBox, Msg::i(0), 1); send(dragBox, Msg::i(fnPoint)); } send(dragBox, Msg::i(100), 1); send(dragBox, Msg::i(note)); fnPoint = note; } }
    }
    void uiUp(const juce::MouseEvent&) {
        auto* b = D().box(dragBox); if (!b) return; auto ge = geoOf(*b);
        if (ge.cls == "kslider" && fnPoint >= 0) { send(dragBox, Msg::i(0), 1); send(dragBox, Msg::i(fnPoint)); fnPoint = -1; }
        if (ge.cls == "function" || ge.cls == "multislider" || ge.cls == "slider" || ge.cls == "dial" || ge.cls == "number" || ge.cls == "flonum") v.changed(false);
    }
    // ---------------------------------------------------------------- typing a box
    void openTyper(juce::Point<float> at, const juce::String& kind, int box, const juce::String& text) {
        typeAt = at; typingKind = kind; typingBox = box; pick = 0;
        typer = std::make_unique<juce::TextEditor>();
        typer->setFont(Theme::font(10.5f, 1)); typer->setIndents(8, 6);
        typer->setColour(juce::TextEditor::backgroundColourId, Theme::card()); typer->setColour(juce::TextEditor::textColourId, Theme::txt());
        typer->setColour(juce::TextEditor::outlineColourId, Theme::blue().withAlpha(0.6f)); typer->setColour(juce::TextEditor::focusedOutlineColourId, Theme::blue());
        typer->setColour(juce::TextEditor::highlightColourId, Theme::blue().withAlpha(0.3f));
        typer->setTextToShowWhenEmpty(kind == "msg" ? U8("a message: 440, or \"1 100\", or \"set 3\"") : kind == "comment" ? U8("a note for whoever reads the patch") : U8("type: cycle~ 440, metro 4n, + 1, echo ..."), Theme::sub());
        typer->setBounds((int) at.x, (int) at.y, kind == "comment" ? 300 : 260, 28);
        typer->setText(text, false); typer->selectAll();
        typer->onTextChange = [this] { updateSugg(); };
        typer->onReturnKey = [this] { if (typingKind == "obj" && !sugg.empty() && pick > 0) takeSugg(); commitTyper(); };
        typer->onEscapeKey = [this] { closeTyper(); };
        addAndMakeVisible(*typer); typer->grabKeyboardFocus();
        typer->addKeyListener(&keys);
        updateSugg(); repaint();
    }
    struct Keys : juce::KeyListener { Canvas* c = nullptr; bool keyPressed(const juce::KeyPress& k, juce::Component*) override {
        if (c == nullptr || c->sugg.empty()) return false;
        if (k == juce::KeyPress::downKey) { c->pick = juce::jmin((int) c->sugg.size() - 1, juce::jmin(7, c->pick + 1)); c->repaint(); return true; }
        if (k == juce::KeyPress::upKey) { c->pick = juce::jmax(0, c->pick - 1); c->repaint(); return true; }
        if (k == juce::KeyPress::tabKey) { c->takeSugg(); return true; }
        return false; } } keys;
    void updateSugg() {
        sugg.clear(); if (!typer || typingKind != "obj") { repaint(); return; }
        auto t = typer->getText().trim(); if (t.isEmpty() || t.containsChar(' ')) { repaint(); return; }
        for (auto* d : searchDefs(t.toStdString(), 8)) sugg.push_back(d);
        pick = juce::jmin(pick, juce::jmax(0, (int) sugg.size() - 1)); repaint();
    }
    void takeSugg() { if (!typer || sugg.empty()) return; auto* d = sugg[(size_t) juce::jlimit(0, (int) sugg.size() - 1, pick)]; auto ex = juce::String(d->example);
        typer->setText(ex.startsWith(d->name) ? ex : juce::String(d->name), false); typer->moveCaretToEnd(); sugg.clear(); repaint(); }
    void closeTyper() { if (!typer) return; removeChildComponent(typer.get()); juce::MessageManager::callAsync([t = typer.release()] { delete t; }); typingBox = -1; sugg.clear(); grabKeyboardFocus(); repaint(); }
    void commitTyper() {
        if (!typer) return;
        auto t = typer->getText().trim();
        v.snapshot();
        if (typingBox >= 0) { if (auto* b = D().box(typingBox)) { if (t.isEmpty()) D().remove(typingBox); else { b->text = t; if (b->kind == "obj" && (clsOf(t) == "p" || clsOf(t) == "patcher") && !b->sub) { b->kind = "sub"; b->sub = std::make_shared<PatchDoc>(); } b->w = 0; } } }
        else if (t.isNotEmpty()) {
            auto kind = typingKind; auto cls = clsOf(t);
            if (kind == "obj" && (cls == "p" || cls == "patcher")) kind = "sub";
            int id = D().add(kind, t, std::round(typeAt.x / 5) * 5, std::round(typeAt.y / 5) * 5);
            if (kind == "sub") { auto& s = *D().box(id)->sub; int i = s.add("obj", "inlet", 40, 30); int o = s.add("obj", "outlet", 40, 140); juce::ignoreUnused(i, o); }
            v.sel = { id }; v.helpFor = juce::String(clsOf(t));
        }
        dropBadLines();
        closeTyper(); v.changed();
    }
    void dropBadLines() { auto& d = D(); d.lines.erase(std::remove_if(d.lines.begin(), d.lines.end(), [&](const PLine& l) { auto* a = d.box(l.from); auto* b = d.box(l.to); if (!a || !b) return true;
        auto ia = boxIO(*a), ib = boxIO(*b); return l.outlet >= ia.outs || l.inlet >= ib.ins; }), d.lines.end()); }
    // ---------------------------------------------------------------- copy, paste, keys
    void copySel() { clip.clear(); clipLines.clear(); for (int s : v.sel) if (auto* b = D().box(s)) clip.push_back(*b);
        for (auto& l : D().lines) { bool a = false, b = false; for (auto& x : clip) { a |= x.id == l.from; b |= x.id == l.to; } if (a && b) clipLines.push_back(l); } }
    void pasteAt(juce::Point<float> at) {
        if (clip.empty()) return; v.snapshot();
        float x0 = 1e9f, y0 = 1e9f; for (auto& b : clip) { x0 = juce::jmin(x0, b.x); y0 = juce::jmin(y0, b.y); }
        std::map<int, int> nid; v.sel.clear();
        for (auto b : clip) { int n = D().nextId++; nid[b.id] = n; b.id = n; b.x = at.x + b.x - x0; b.y = at.y + b.y - y0; if (b.sub) b.sub = std::make_shared<PatchDoc>(*b.sub); D().boxes.push_back(b); v.sel.push_back(n); }
        for (auto l : clipLines) { l.from = nid[l.from]; l.to = nid[l.to]; D().lines.push_back(l); }
        v.changed();
    }
    void removeSel() {
        if (v.selLine >= 0 && v.selLine < (int) D().lines.size()) { v.snapshot(); D().lines.erase(D().lines.begin() + v.selLine); v.selLine = -1; v.changed(); return; }
        if (v.sel.empty()) return; v.snapshot(); for (int s : v.sel) D().remove(s); v.sel.clear(); v.changed();
    }
    juce::Point<float> mouseHere() { auto p = getMouseXYRelative().toFloat(); return getLocalBounds().toFloat().contains(p) ? p : v.visibleCentre(); }
    bool keyPressed(const juce::KeyPress& k) override {
        bool cmd = k.getModifiers().isCommandDown(); int kc = k.getKeyCode();
        if (cmd && kc == 'E') { v.setEditing(!v.editing); return true; }
        if (cmd && kc == 'Z') { if (k.getModifiers().isShiftDown()) v.redo(); else v.undo(); return true; }
        if (cmd && kc == 'S') { v.saveFile(k.getModifiers().isShiftDown()); return true; }
        if (!v.editing) return false;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { removeSel(); return true; }
        if (cmd && kc == 'C') { copySel(); return true; }
        if (cmd && kc == 'X') { copySel(); removeSel(); return true; }
        if (cmd && kc == 'V') { pasteAt(mouseHere()); return true; }
        if (cmd && kc == 'D') { copySel(); float x = 1e9f, y = 1e9f; for (auto& b : clip) { x = juce::jmin(x, b.x); y = juce::jmin(y, b.y); } pasteAt({ x + 20, y + 40 }); return true; }
        if (cmd && kc == 'A') { v.sel.clear(); for (auto& b : D().boxes) v.sel.push_back(b.id); repaint(); return true; }
        if (!cmd && !v.sel.empty() && (kc == juce::KeyPress::leftKey || kc == juce::KeyPress::rightKey || kc == juce::KeyPress::upKey || kc == juce::KeyPress::downKey)) {
            float st = k.getModifiers().isShiftDown() ? 10.0f : 1.0f; v.snapshot();
            for (int s : v.sel) if (auto* b = D().box(s)) { if (kc == juce::KeyPress::leftKey) b->x -= st; if (kc == juce::KeyPress::rightKey) b->x += st; if (kc == juce::KeyPress::upKey) b->y -= st; if (kc == juce::KeyPress::downKey) b->y += st; }
            v.changed(false); return true; }
        if (cmd || k.getModifiers().isAltDown()) return false;
        auto quick = [&](const juce::String& kind, const juce::String& text) { auto p = mouseHere(); if (text.isNotEmpty()) { v.snapshot(); int id = D().add("obj", text, std::round(p.x / 5) * 5, std::round(p.y / 5) * 5); v.sel = { id }; v.changed(); } else openTyper(p, kind, -1, ""); return true; };
        switch (k.getTextCharacter()) {
            case 'n': case 'N': return quick("obj", ""); case 'm': case 'M': return quick("msg", ""); case 'c': case 'C': return quick("comment", "");
            case 'b': case 'B': return quick("obj", "button"); case 't': case 'T': return quick("obj", "toggle"); case 'i': case 'I': return quick("obj", "number"); case 'f': case 'F': return quick("obj", "flonum");
            default: break;
        }
        return false;
    }
    void popup(juce::Point<float> at) {
        int id = boxAt(at); juce::PopupMenu pm;
        if (id >= 0) { if (!isSel(id)) v.sel = { id }; auto* b = D().box(id);
            pm.addItem(1, "Help for this box"); if (b->kind != "comment") pm.addItem(2, "Change its text"); pm.addItem(3, "Duplicate"); pm.addItem(4, "Delete");
            if (b->sub) pm.addItem(5, "Open the subpatch"); }
        else { pm.addItem(10, "New box (N)"); pm.addItem(11, "New message (M)"); pm.addItem(12, "New comment (C)"); pm.addItem(13, "Button (B)"); pm.addItem(14, "Toggle (T)"); pm.addItem(15, "Number (I)"); pm.addSeparator(); pm.addItem(16, "Paste", !clip.empty()); }
        pm.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id, at](int x) {
            auto* b = id >= 0 ? D().box(id) : nullptr;
            switch (x) {
                case 1: if (b) { v.helpFor = juce::String(geoOf(*b).cls); v.rightTab = 0; v.repaint(); } break;
                case 2: if (b) openTyper({ b->x, b->y }, b->kind, id, b->text); break;
                case 3: copySel(); if (b) pasteAt({ b->x + 20, b->y + 40 }); break;
                case 4: removeSel(); break;
                case 5: if (b) { v.path.push_back(id); v.sel.clear(); repaint(); v.repaint(); } break;
                case 10: openTyper(at, "obj", -1, ""); break; case 11: openTyper(at, "msg", -1, ""); break; case 12: openTyper(at, "comment", -1, ""); break;
                case 13: case 14: case 15: { v.snapshot(); const char* T[3] = { "button", "toggle", "number" }; int n = D().add("obj", T[x - 13], at.x, at.y); v.sel = { n }; v.changed(); break; }
                case 16: pasteAt(at); break;
                default: break;
            } });
    }
    // a sound file dropped on a buffer~ / waveform~
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int x, int y) override {
        int id = boxAt({ (float) x, (float) y }); auto* b = id >= 0 ? D().box(id) : nullptr;
        if (b) { auto ge = geoOf(*b); if ((ge.cls == "buffer~" || ge.cls == "waveform~") && !ge.args.empty() && ge.args[0].t == 's') { v.readBuffer(juce::String(symStr(ge.args[0].s)), juce::File(files[0])); return; } }
        v.filesDropped(files, x, y);
    }
};

// ================================================================== the search and the shelves (left)
struct PatcherView::Side : public juce::Component {
    PatcherView& v; juce::TextEditor search; int shelf = 0, scroll = 0;
    struct Row { juce::Rectangle<int> r; std::string name; int shelf = -1; };
    std::vector<Row> rows; std::string dragging; juce::Point<int> dragAt;
    explicit Side(PatcherView& pv) : v(pv) {
        search.setFont(Theme::font(10.5f, 1)); search.setIndents(14, 8);
        search.setTextToShowWhenEmpty(U8("find: sine, echo, random, mic ..."), Theme::sub());
        search.setColour(juce::TextEditor::backgroundColourId, Theme::card2()); search.setColour(juce::TextEditor::textColourId, Theme::txt());
        search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack); search.setColour(juce::TextEditor::focusedOutlineColourId, Theme::blue().withAlpha(0.5f));
        search.onTextChange = [this] { scroll = 0; repaint(); };
        search.onReturnKey = [this] { auto h = searchDefs(search.getText().toStdString(), 1); if (!h.empty()) v.addFromSearch(h[0]->name); };
        addAndMakeVisible(search);
    }
    void resized() override { search.setBounds(12, 12, getWidth() - 24, 38); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (Theme::softUI()) softWell(g, r, 20); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 10); }
        rows.clear(); int y = 60 - scroll; auto q = search.getText().trim();
        auto row = [&](const ObjDef* d) {
            auto rr = juce::Rectangle<int>(10, y, getWidth() - 20, 46);
            if (rr.getBottom() > 0 && rr.getY() < getHeight()) {
                if (Theme::softUI()) softKeyLite(g, rr.toFloat(), 16, false); else { g.setColour(Theme::btn()); g.fillRoundedRectangle(rr.toFloat(), 8); }
                bool sig = d->name.find('~') != std::string::npos; softLed(g, { (float) rr.getX() + 14, (float) rr.getY() + 15 }, 2.8f, sig ? sigCol() : d->uiW > 0 ? Theme::green() : Theme::blue(), true);
                txt(g, d->name, rr.withTrimmedLeft(26).withHeight(26).translated(0, 2), 9.5f, Theme::txt(), 2);
                txt(g, d->desc, rr.withTrimmedLeft(14).withTrimmedRight(8).withTrimmedTop(25).withHeight(17), 6.8f, Theme::sub());
            }
            rows.push_back({ rr, d->name, -1 }); y += 51;
        };
        if (q.isNotEmpty()) {
            auto hits = searchDefs(q.toStdString(), 60);
            txt(g, hits.empty() ? juce::String("nothing for that - try other words") : juce::String((int) hits.size()) + " found", { 14, y, getWidth() - 28, 16 }, 7.5f, Theme::sub()); y += 20;
            for (auto* d : hits) row(d);
        } else {
            auto cats = categories();
            for (int c = 0; c < (int) cats.size(); ++c) {
                auto rr = juce::Rectangle<int>(10, y, getWidth() - 20, 32);
                if (rr.getBottom() > 0 && rr.getY() < getHeight()) {
                    if (Theme::softUI()) softKeyLite(g, rr.toFloat(), 16, c == shelf, Theme::blue(), c == shelf ? 0.45f : 0.0f); else { g.setColour(c == shelf ? Theme::sel() : Theme::btn()); g.fillRoundedRectangle(rr.toFloat(), 8); }
                    int n = 0; for (auto& d : catalogue()) n += d.cat == cats[(size_t) c] && d.aliasOf.empty();
                    txt(g, juce::String(cats[(size_t) c]), rr.reduced(14, 0), 9.0f, Theme::txt(), c == shelf ? 2 : 1);
                    txt(g, juce::String(n), rr.reduced(14, 0), 7.5f, Theme::sub(), 0, juce::Justification::centredRight);
                }
                rows.push_back({ rr, {}, c }); y += 37;
                if (c == shelf) { for (auto& d : catalogue()) if (d.cat == cats[(size_t) c] && d.aliasOf.empty()) row(&d); y += 6; }
            }
        }
        maxScroll = juce::jmax(0, y + scroll - getHeight() + 20);
        if (!dragging.empty()) { auto rr = juce::Rectangle<int>(dragAt.x - 50, dragAt.y - 14, 100, 28); g.setColour(Theme::blue().withAlpha(0.8f)); g.fillRoundedRectangle(rr.toFloat(), 14); txt(g, dragging, rr, 9, juce::Colours::white, 2, juce::Justification::centred); }
    }
    int maxScroll = 0;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jlimit(0, maxScroll, scroll - (int) (w.deltaY * 220)); repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        for (auto& r : rows) if (r.r.contains(e.getPosition())) {
            if (r.shelf >= 0) { shelf = shelf == r.shelf ? -1 : r.shelf; repaint(); return; }
            dragging = r.name; dragAt = e.getPosition(); return; }
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (!dragging.empty()) { dragAt = e.getPosition(); repaint(); } }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragging.empty()) return; auto name = dragging; dragging.clear(); repaint();
        auto inVp = v.vp.getLocalPoint(this, e.getPosition());
        if (v.vp.getLocalBounds().contains(inVp)) v.addFromSearch(name, (inVp + v.vp.getViewPosition()).toFloat());
        else if (e.getDistanceFromDragStart() < 6) v.addFromSearch(name);
    }
};

// ================================================================== help, guide, patches (right)
struct PatcherView::Right : public juce::Component {
    PatcherView& v; int scroll = 0, maxScroll = 0;
    std::vector<std::pair<juce::Rectangle<int>, std::function<void()>>> hits;
    explicit Right(PatcherView& pv) : v(pv) {}
    void key(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& t, std::function<void()> f, bool on = false, float fs = 8.8f) {
        if (Theme::softUI()) softKeyLite(g, b.toFloat(), b.getHeight() * 0.5f, on, Theme::blue(), on ? 0.45f : 0.0f); else { g.setColour(on ? Theme::sel() : Theme::btn()); g.fillRoundedRectangle(b.toFloat(), 8); }
        txt(g, t, b.reduced(10, 0), fs, Theme::txt(), 2, juce::Justification::centred); hits.push_back({ b, std::move(f) });
    }
    int para(juce::Graphics& g, const juce::String& t, int x, int y, int w, float fs, juce::Colour c) {
        juce::AttributedString as; as.append(t, Theme::font(fs, 0), c); as.setLineSpacing(3.0f); as.setWordWrap(juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout(as, (float) w); tl.draw(g, juce::Rectangle<float>((float) x, (float) y, (float) w, tl.getHeight())); return (int) tl.getHeight();
    }
    void paint(juce::Graphics& g) override {
        hits.clear();
        auto r = getLocalBounds().toFloat();
        if (Theme::softUI()) softWell(g, r, 20); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 10); }
        const char* T[3] = { "Help", "Guide", "Patches" }; int tw = (getWidth() - 32) / 3;
        for (int i = 0; i < 3; ++i) key(g, { 12 + i * (tw + 4), 12, tw, 34 }, T[i], [this, i] { v.rightTab = i; scroll = 0; repaint(); }, v.rightTab == i);
        juce::Graphics::ScopedSaveState ss(g); g.reduceClipRegion(0, 56, getWidth(), getHeight() - 56);
        int x = 18, w = getWidth() - 36, y = 62 - scroll;
        if (v.rightTab == 0) {
            auto* d = v.helpFor.isNotEmpty() ? findDef(v.helpFor.toStdString()) : nullptr;
            if (d == nullptr) {
                txt(g, "Help", { x, y, w, 24 }, 13, Theme::txt(), 2); y += 30;
                y += para(g, U8("Choose a box: what it does, what each inlet and outlet means, and an example.\n\nThe quick keys (while editing):\nN a box · M a message · C a comment\nB button · T toggle · I number · F decimal number\nCmd E edit / play · Cmd Z undo · Cmd D duplicate\nAlt-drag copies · Shift-click chooses several\nDouble-click a box to change it; double-click [p ...] to open it.\n\nCords: thin ones carry messages, thick coloured ones carry sound."), x, y, w, 8.6f, Theme::txt().withAlpha(0.85f));
            } else {
                txt(g, d->name, { x, y, w, 26 }, 15, Theme::txt(), 2); y += 30;
                txt(g, juce::String(d->cat), { x, y, w, 16 }, 7.5f, Theme::sub()); y += 22;
                y += para(g, d->desc, x, y, w, 9.0f, Theme::txt()) + 14;
                for (auto& b : v.sel) if (auto* bx = v.cur().box(b)) { auto e = v.rt() ? v.rt()->boxErrorKey(v.keyOf(b)) : juce::String(); if (e.isNotEmpty()) { y += para(g, U8("⚠ ") + e, x, y, w, 8.6f, Theme::ink(Theme::red())) + 10; } juce::ignoreUnused(bx); }
                auto list = [&](const char* title, const std::string& s) {
                    auto parts = juce::StringArray::fromTokens(juce::String(s), "|", ""); if (s.empty()) return;
                    drawCaption(g, title, { x, y, w, 14 }); y += 18;
                    for (int i = 0; i < parts.size(); ++i) { auto t = parts[i].trim(); if (t.isEmpty()) continue; txt(g, juce::String(i + 1), { x, y, 18, 18 }, 8, Theme::sub(), 2); y += para(g, t, x + 20, y + 1, w - 20, 8.4f, Theme::txt().withAlpha(0.85f)) + 6; }
                    y += 6; };
                list(d->ins.empty() ? "" : "inlets (left to right)", d->ins); list("outlets", d->outs);
                if (!d->example.empty()) { drawCaption(g, "example", { x, y, w, 14 }); y += 18; txt(g, "[" + juce::String(d->example) + "]", { x, y, w, 20 }, 9.5f, Theme::txt(), 1); y += 26;
                    auto ex = d->example; key(g, { x, y, w, 34 }, "Put one in the patch", [this, ex] { v.addFromSearch(clsOf(juce::String(ex)), { -2, -2 }); juce::ignoreUnused(ex); }); y += 42; }
                for (auto& dd : catalogue()) if (dd.aliasOf == d->name) { txt(g, "also: " + juce::String(dd.name), { x, y, w, 16 }, 8, Theme::sub()); y += 18; }
            }
        } else if (v.rightTab == 1) {
            auto& L = lessons();
            if (v.lessonOpen < 0) {
                txt(g, "Guide", { x, y, w, 24 }, 13, Theme::txt(), 2); y += 28;
                y += para(g, U8("Eleven short steps. Each one opens a patch that shows it - play with it, then change it."), x, y, w, 8.4f, Theme::sub()) + 12;
                for (int i = 0; i < (int) L.size(); ++i) { key(g, { x, y, w, 36 }, L[(size_t) i].title, [this, i] { v.lessonOpen = i; scroll = 0; repaint(); }); y += 42; }
            } else {
                auto& l = L[(size_t) v.lessonOpen];
                key(g, { x, y, 104, 30 }, U8("‹ All steps"), [this] { v.lessonOpen = -1; scroll = 0; repaint(); }); y += 42;
                txt(g, l.title, { x, y, w, 24 }, 12, Theme::txt(), 2); y += 32;
                y += para(g, l.text, x, y, w, 8.8f, Theme::txt().withAlpha(0.9f)) + 16;
                if (l.example.isNotEmpty()) { auto ex = l.example; key(g, { x, y, w, 38 }, U8("Show me (opens the patch)"), [this, ex] { auto& E = examples(); for (int i = 0; i < (int) E.size(); ++i) if (E[(size_t) i].name == ex) v.openExample(i); }, true); y += 46; }
                if (v.lessonOpen + 1 < (int) L.size()) { key(g, { x, y, w, 34 }, U8("Next step ›"), [this] { v.lessonOpen++; scroll = 0; repaint(); }); y += 42; }
            }
        } else {
            txt(g, "Patches", { x, y, w, 24 }, 13, Theme::txt(), 2); y += 30;
            key(g, { x, y, (w - 6) / 2, 32 }, "New", [this] { PatchDoc d; v.openDoc(d, true); }); key(g, { x + (w - 6) / 2 + 6, y, (w - 6) / 2, 32 }, U8("Open a file…"), [this] { v.openMenu(); }); y += 40;
            auto mine = PatcherView::library().findChildFiles(juce::File::findFiles, false, "*.arcopatch");
            if (!mine.isEmpty()) { drawCaption(g, "yours", { x, y, w, 14 }); y += 18; mine.sort();
                for (auto& f : mine) { auto ff = f; key(g, { x, y, w, 30 }, f.getFileNameWithoutExtension(), [this, ff] { v.loadFile(ff); }, v.file == f); y += 35; } y += 8; }
            juce::String shelf;
            auto& E = examples();
            for (int i = 0; i < (int) E.size(); ++i) {
                if (E[(size_t) i].shelf != shelf) { shelf = E[(size_t) i].shelf; y += 6; drawCaption(g, shelf, { x, y, w, 14 }); y += 18; }
                key(g, { x, y, w, 30 }, E[(size_t) i].name, [this, i] { v.openExample(i); }, v.root().name == E[(size_t) i].name); y += 35;
            }
        }
        maxScroll = juce::jmax(0, y + scroll - getHeight() + 20);
    }
    void mouseDown(const juce::MouseEvent& e) override { for (auto& h : hits) if (h.first.contains(e.getPosition()) && (h.first.getY() < 50 || e.getPosition().y > 56)) { auto f = h.second; juce::MessageManager::callAsync(f); return; } }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jlimit(0, maxScroll, scroll - (int) (w.deltaY * 220)); repaint(); }
};

// ================================================================== the console (bottom)
struct PatcherView::Console : public juce::Component {
    PatcherView& v; explicit Console(PatcherView& pv) : v(pv) {}
    std::vector<std::pair<juce::Rectangle<int>, int>> lineHits;
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (Theme::softUI()) softWellLite(g, r, 16); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 8); }
        drawCaption(g, "console", { 16, 6, 100, 16 });
        lineHits.clear();
        auto& L = v.log(); int y = getHeight() - 22;
        if (L.empty()) txt(g, U8("what [print] says, and any problem, shows here"), { 16, 26, getWidth() - 32, 18 }, 8, Theme::sub());
        for (int i = (int) L.size() - 1; i >= 0 && y > 20; --i, y -= 17) {
            auto& l = L[(size_t) i]; auto c = l.kind == 'e' ? Theme::ink(Theme::red()) : Theme::txt().withAlpha(0.85f);
            g.setColour(c); g.setFont(Theme::font(8.6f, 0)); g.drawFittedText((l.kind == 'e' ? U8("⚠ ") : juce::String()) + juce::String::fromUTF8(l.text), juce::Rectangle<int>(16, y, getWidth() - 40, 16), juce::Justification::centredLeft, 1, 0.9f);
            if (l.box > 0) lineHits.push_back({ { 16, y, getWidth() - 40, 16 }, l.box });
        }
        txt(g, "clear", { getWidth() - 70, 4, 56, 18 }, 7.5f, Theme::sub(), 2, juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.x > getWidth() - 80 && e.y < 24) { v.log().clear(); repaint(); return; }
        for (auto& h : lineHits) if (h.first.contains(e.getPosition())) { v.path.clear(); v.sel = { h.second }; if (auto* b = v.cur().box(h.second)) { v.helpFor = juce::String(clsOf(b->text)); v.rightTab = 0; } v.repaint(); return; }
    }
};

// ================================================================== the screen
juce::File PatcherView::library() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO").getChildFile("Patches"); d.createDirectory(); return d; }
PatcherView::PatcherView(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    canvas = std::make_unique<Canvas>(*this);
    vp.setViewedComponent(canvas.get(), false); vp.setScrollBarsShown(true, true); vp.setScrollBarThickness(8);
    addAndMakeVisible(vp);
    side = std::make_unique<Side>(*this); addAndMakeVisible(*side);
    right = std::make_unique<Right>(*this); addAndMakeVisible(*right);
    console = std::make_unique<Console>(*this); addAndMakeVisible(*console);
    auto& h = shell.host;
    auto pill = [&](const juce::String& t, ColourFn c, std::function<void()> f, const char* id) { auto* p = add<Pill>(t, std::move(c), 9.0f, 8.0f); p->onClick = std::move(f); p->setComponentID(juce::String("patcher:") + id); return p; };
    auto* ed = pill(U8("✎ Edit"), [this] { return editing ? Theme::blue() : Theme::btn(); }, [this] { setEditing(true); }, "edit"); ed->tint = true; ed->seg = 1;
    auto* pl = pill(U8("▶ Play"), [this] { return !editing ? Theme::green() : Theme::btn(); }, [this] { setEditing(false); }, "play"); pl->tint = true; pl->seg = 3;
    pill("Undo", [] { return Theme::btn(); }, [this] { undo(); }, "undo");
    pill("Save", [] { return Theme::btn(); }, [this] { saveFile(false); }, "save");
    pill(U8("Open…"), [] { return Theme::btn(); }, [this] { openMenu(); }, "open");
    const char* INTO[4] = { "Own", "Live 2", "Live 3", "Live 4" };
    for (int i = 0; i < 4; ++i) { auto* p = pill(INTO[i], [&h, i] { return h.patcherInto == i ? Theme::violet() : Theme::btn(); }, [&h, i] { h.setPatcherInto(i); }, (juce::String("into") + juce::String(i)).toRawUTF8()); p->tint = true; p->seg = i == 0 ? 1 : i == 3 ? 3 : 2; }
    auto* snd = pill("", [&h] { return h.engine.patcherGain.load() > 0 ? Theme::green() : Theme::btn(); }, [&h] { h.engine.patcherGain.store(h.engine.patcherGain.load() > 0 ? 0.0f : 1.0f); }, "sound");
    snd->tint = true; snd->dynText = [&h] { return h.engine.patcherGain.load() > 0 ? juce::String("Sound on") : juce::String("Sound off"); };
    auto* st = pill(U8("Use in Studio ▸"), [] { return Theme::btn(); }, [this] {
        auto& S = shell.host.studio; int li = -1; for (int i = 0; i < (int) S.song.lanes.size(); ++i) if (S.song.lanes[(size_t) i].kind == LaneAudio) { li = i; break; }
        if (li < 0) { shell.host.patchLog.push_back(px::Line { "make an audio track in STUDIO first, then come back", 'i', 0 }); ++shell.host.patchLogGen; return; }
        shell.host.patcherKeep(); StudioDevice d; d.type = DevPatcher; d.plugName = root().name; d.patchJson = juce::JSON::toString(root().toVar(), true);
        d.prt = std::make_shared<px::Runtime>(); d.prt->prepare(S.song.sr > 0 ? S.song.sr : 48000.0); d.prt->setDoc(root(), true);
        S.snapshot(); { const juce::ScopedLock sl(S.lock); S.song.lanes[(size_t) li].devices.push_back(d); } S.refresh();
        juce::String msg = juce::String("on the track \"") + S.song.lanes[(size_t) li].name + "\" in STUDIO - [adc~ 1 2] is the track's sound, [dac~ 1 2] what goes on";
        px::Line l {}; std::strncpy(l.text, msg.toRawUTF8(), sizeof l.text - 1); l.kind = 'i'; shell.host.patchLog.push_back(l); ++shell.host.patchLogGen; }, "studio");
    auto* back = pill(U8("‹ Back to my patch"), [] { return Theme::btn(); }, [this] { editMain(); }, "back"); back->setVisible(false);
    h.onPatcherAction = [this](const px::Action& a) {
        juce::String w(a.what);
        if (w == "tempo") shell.setTempo(a.a);
        else if (w == "play") { if ((a.a > 0) != shell.isPlaying()) shell.transportPlay(); }
        else if (w == "bufread") { auto parts = juce::StringArray::fromTokens(juce::String::fromUTF8(a.text), "|", ""); auto name = parts[0];
            if (parts.size() > 1 && juce::File::isAbsolutePath(parts[1]) && juce::File(parts[1]).existsAsFile()) { readBuffer(name, juce::File(parts[1])); return; }
            chooser = std::make_unique<juce::FileChooser>("A sound for " + name, juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, name](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) readBuffer(name, f); }); }
    };
    editMain();
    startTimerHz(30);
}
PatcherView::~PatcherView() { shell.host.onPatcherAction = nullptr; vp.setViewedComponent(nullptr, false); }
std::deque<px::Line>& PatcherView::log() { return deviceMode() ? devLog : shell.host.patchLog; }
px::PatchDoc& PatcherView::root() { return deviceMode() ? devDoc : shell.host.patchDoc; }
px::Runtime* PatcherView::rt() { return deviceMode() ? devRt.get() : shell.host.patcher.get(); }
px::PatchDoc& PatcherView::cur() {
    PatchDoc* d = &root();
    for (size_t i = 0; i < path.size(); ++i) { auto* b = d->box(path[i]); if (b == nullptr || !b->sub) { path.resize(i); break; } d = b->sub.get(); }
    return *d;
}
std::string PatcherView::keyOf(int id) const { std::string k; for (int p : path) k += std::to_string(p) + "/"; return k + std::to_string(id); }
void PatcherView::editMain() { devRt = nullptr; devWrite = nullptr; path.clear(); sel.clear(); for (auto* c : owned) if (c->getComponentID() == "patcher:back") c->setVisible(false); resized(); repaint(); }
void PatcherView::editDevice(std::shared_ptr<px::Runtime> r, const juce::String& json, const juce::String& title, std::function<void(const juce::String&)> wb) {
    devRt = std::move(r); devDoc = PatchDoc::fromVar(juce::JSON::parse(json)); devTitle = title; devWrite = std::move(wb); path.clear(); sel.clear(); devLog.clear();
    devRt->onLine = [this](const px::Line& l) { devLog.push_back(l); while (devLog.size() > 300) devLog.pop_front(); };
    for (auto* c : owned) if (c->getComponentID() == "patcher:back") c->setVisible(true);
    resized(); repaint();
}
void PatcherView::snapshot() { undoStack.push_back(juce::JSON::toString(root().toVar(), true)); if (undoStack.size() > 80) undoStack.erase(undoStack.begin()); redoStack.clear(); }
void PatcherView::undo() { if (undoStack.empty()) return; redoStack.push_back(juce::JSON::toString(root().toVar(), true)); root() = PatchDoc::fromVar(juce::JSON::parse(undoStack.back())); undoStack.pop_back(); sel.clear(); changed(); }
void PatcherView::redo() { if (redoStack.empty()) return; undoStack.push_back(juce::JSON::toString(root().toVar(), true)); root() = PatchDoc::fromVar(juce::JSON::parse(redoStack.back())); redoStack.pop_back(); sel.clear(); changed(); }
void PatcherView::changed(bool structural) {
    if (structural || true) { if (deviceMode()) { devRt->setDoc(devDoc, false); if (devWrite) devWrite(juce::JSON::toString(devDoc.toVar(), true)); } else shell.host.patcherCompile(false); }
    canvas->repaint(); right->repaint(); repaint();
}
void PatcherView::setEditing(bool e) { editing = e; if (!e) { canvas->closeTyper(); sel.clear(); } canvas->repaint(); repaint(); canvas->grabKeyboardFocus(); }
void PatcherView::openDoc(const px::PatchDoc& d, bool asNew) {
    snapshot(); root() = d; path.clear(); sel.clear(); if (asNew) file = juce::File();
    if (deviceMode()) { devRt->setDoc(devDoc, true); if (devWrite) devWrite(juce::JSON::toString(devDoc.toVar(), true)); } else shell.host.patcherCompile(true);
    setEditing(d.boxes.empty()); vp.setViewPosition(0, 0); repaint(); canvas->repaint(); right->repaint();
}
void PatcherView::openExample(int i) { auto& E = examples(); if (i < 0 || i >= (int) E.size()) return; openDoc(E[(size_t) i].doc, true); setEditing(false);
    px::Line l {}; std::strncpy(l.text, (E[(size_t) i].name + ": " + E[(size_t) i].notes).toRawUTF8(), sizeof l.text - 1); l.kind = 'i'; log().push_back(l); }
juce::Point<float> PatcherView::visibleCentre() const { auto p = vp.getViewPosition(); return { p.x + vp.getViewWidth() * 0.4f, p.y + vp.getViewHeight() * 0.35f }; }
void PatcherView::addFromSearch(const std::string& cls, juce::Point<float> at) {
    auto* d = findDef(cls); if (!d) return;
    if (at.x < 0) { at = visibleCentre(); at.x += (float) (cur().boxes.size() % 6) * 24; at.y += (float) (cur().boxes.size() % 5) * 30; }
    snapshot();
    juce::String text = juce::String(d->example).startsWith(juce::String(d->name)) ? juce::String(d->example) : juce::String(d->name);
    int id = cur().add(d->name == "p" ? "sub" : "obj", text, std::round(at.x / 5) * 5, std::round(at.y / 5) * 5);
    if (d->name == "p") { auto& s = *cur().box(id)->sub; s.add("obj", "inlet", 40, 30); s.add("obj", "outlet", 40, 140); }
    sel = { id }; helpFor = juce::String(d->name); rightTab = 0; setEditing(true); changed();
}
void PatcherView::saveFile(bool as) {
    shell.host.patcherKeep();
    if (!as && file != juce::File()) { file.replaceWithText(juce::JSON::toString(root().toVar())); px::Line l {}; std::strncpy(l.text, ("saved: " + file.getFileName()).toRawUTF8(), sizeof l.text - 1); l.kind = 'i'; log().push_back(l); repaint(); return; }
    chooser = std::make_unique<juce::FileChooser>("Save the patch", library().getChildFile(root().name + ".arcopatch"), "*.arcopatch");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (f == juce::File()) return; f = f.withFileExtension("arcopatch"); root().name = f.getFileNameWithoutExtension(); file = f;
        f.replaceWithText(juce::JSON::toString(root().toVar())); right->repaint(); repaint(); });
}
void PatcherView::loadFile(const juce::File& f) {
    auto v = juce::JSON::parse(f); if (!v.isObject()) { px::Line l {}; std::strncpy(l.text, "that file is not a patch", sizeof l.text - 1); l.kind = 'e'; log().push_back(l); return; }
    auto d = PatchDoc::fromVar(v); if (f.hasFileExtension("maxpat") || d.name == "From Max") d.name = f.getFileNameWithoutExtension();
    openDoc(d, true); file = f.hasFileExtension("arcopatch") ? f : juce::File();
}
void PatcherView::openMenu() {
    juce::PopupMenu pm; pm.addItem(1, U8("A patch file… (ARCO .arcopatch or Max .maxpat)")); pm.addItem(2, "Save as a new file"); pm.addItem(3, U8("Export as a Max patch (.maxpat)…")); pm.addSeparator(); pm.addItem(4, "New, empty patch");
    pm.showMenuAsync(juce::PopupMenu::Options(), [this](int x) {
        if (x == 1) { chooser = std::make_unique<juce::FileChooser>("Open a patch", library(), "*.arcopatch;*.maxpat;*.json");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) loadFile(f); }); }
        if (x == 2) saveFile(true);
        if (x == 3) { chooser = std::make_unique<juce::FileChooser>("Export for Max", library().getChildFile(root().name + ".maxpat"), "*.maxpat");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode, [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) f.withFileExtension("maxpat").replaceWithText(juce::JSON::toString(root().toMaxpat())); }); }
        if (x == 4) { PatchDoc d; openDoc(d, true); }
    });
}
void PatcherView::readBuffer(const juce::String& name, const juce::File& f) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) { px::Line l {}; std::strncpy(l.text, ("could not read " + f.getFileName()).toRawUTF8(), sizeof l.text - 1); l.kind = 'e'; log().push_back(l); return; }
    auto b = std::make_shared<Buf>(); b->name = sym(name.toStdString()); b->sr = r->sampleRate;
    long n = (long) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 600)); int ch = (int) juce::jlimit(1u, 8u, r->numChannels);
    juce::AudioBuffer<float> tmp(ch, (int) n); r->read(&tmp, 0, (int) n, 0, true, true);
    b->ch.resize((size_t) ch); for (int c = 0; c < ch; ++c) b->ch[(size_t) c].assign(tmp.getReadPointer(c), tmp.getReadPointer(c) + n);
    if (auto* R = rt()) { const juce::SpinLock::ScopedLockType sl(R->bufLock); R->bufs[b->name] = b; }
    px::Line l {}; std::strncpy(l.text, ("[buffer~ " + name + "]: " + f.getFileName() + ", " + juce::String(n / r->sampleRate, 1) + " s").toRawUTF8(), sizeof l.text - 1); l.kind = 'i'; log().push_back(l);
    canvas->repaint();
}
void PatcherView::filesDropped(const juce::StringArray& files, int, int) {
    for (auto& f : files) { juce::File ff(f); if (ff.hasFileExtension("arcopatch;maxpat;json")) { loadFile(ff); return; } }
}
void PatcherView::visibilityChanged() {
    shell.host.patcherKeys.store(isVisible());
    if (isVisible()) canvas->grabKeyboardFocus();
}
bool PatcherView::keyPressed(const juce::KeyPress& k) { return canvas->keyPressed(k); }
void PatcherView::timerCallback() {
    if (!isShowing()) return;
    if (deviceMode()) devRt->poll();
    canvas->repaint();
    int lg = shell.host.patchLogGen + (int) devLog.size(); if (lg != lastLogGen) { lastLogGen = lg; console->repaint(); }
}
void PatcherView::resized() {
    int W = getWidth(), H = getHeight();
    barR = { 16, 10, W - 32, 46 };
    int x = barR.getX() + 150;
    int y0 = barR.getY() + 4;
    auto place = [&](const char* id, int w) { for (auto* c : owned) if (c->getComponentID() == juce::String("patcher:") + id) { if (!c->isVisible() && juce::String(id) == "back") return; c->setBounds(x, y0, w, 38); x += w + 8; } };
    place("edit", 92); x -= 8; place("play", 92); x += 12; place("undo", 78); place("save", 78); place("open", 86);
    bool backOn = false; for (auto* c : owned) if (c->getComponentID() == "patcher:back") backOn = c->isVisible();
    int rightW = 100 + 4 * 66 + 20 + 116 + 158 + (backOn ? 178 : 0);
    bool twoRows = x + rightW > W - 16;                     // 2.4: a narrow window puts where it plays on a second row
    if (twoRows) { barR.setHeight(92); x = barR.getX() + 100; y0 = barR.getY() + 50; } else x += 100;
    for (int i = 0; i < 4; ++i) { place((juce::String("into") + juce::String(i)).toRawUTF8(), 74); x -= 8; } x += 20;
    place("sound", 108); place("studio", 150); place("back", 170);
    int top = barR.getBottom() + 10, conH = 118;
    side->setBounds(16, top, 268, H - top - 16);
    right->setBounds(W - 16 - 300, top, 300, H - top - 16);
    vp.setBounds(side->getRight() + 12, top, right->getX() - 12 - side->getRight() - 12, H - top - conH - 28);
    console->setBounds(vp.getX(), vp.getBottom() + 12, vp.getWidth(), conH);
}
void PatcherView::paint(juce::Graphics& g) {
    txt(g, "Patcher", { barR.getX() + 6, barR.getY(), 140, 46 }, 16, Theme::txt(), 2);
    auto name = deviceMode() ? U8("in STUDIO: ") + devTitle : root().name;
    juce::String crumbs = name; { PatchDoc* d = &root(); for (int p : path) { auto* b = d->box(p); if (!b || !b->sub) break; crumbs << U8("  ›  ") << b->text; d = b->sub.get(); } }
    drawCaption(g, "plays into", { 0, 0, 0, 0 });
    int ix = 0; for (auto* c : owned) if (c->getComponentID() == "patcher:into0") ix = c->getX();
    int iy = barR.getY() + 4; for (auto* c : owned) if (c->getComponentID() == "patcher:into0") iy = c->getY();
    drawCaption(g, "plays into", { ix - 90, iy, 84, 38 }, juce::Justification::centredRight);
    // the path of subpatches over the canvas
    auto cr = juce::Rectangle<int>(vp.getX() + 12, vp.getY() - 0, vp.getWidth() - 24, 0); juce::ignoreUnused(cr);
    if (Theme::softUI()) softWellLite(g, vp.getBounds().toFloat().expanded(3), 18);
    drawSoftShades(g, *this);
    juce::ignoreUnused(crumbs);
}
void PatcherView::demo(const juce::String& cmd) {
    if (cmd.contains("pex=")) openExample(cmd.fromFirstOccurrenceOf("pex=", false, false).getIntValue());
    if (cmd.contains("pedit")) setEditing(true);
    if (cmd.contains("ptab=")) rightTab = cmd.fromFirstOccurrenceOf("ptab=", false, false).getIntValue();
    if (cmd.contains("plesson=")) { rightTab = 1; lessonOpen = cmd.fromFirstOccurrenceOf("plesson=", false, false).getIntValue(); }
    if (cmd.contains("psel=")) { int k = cmd.fromFirstOccurrenceOf("psel=", false, false).getIntValue(); if (k >= 0 && k < (int) cur().boxes.size()) { sel = { cur().boxes[(size_t) k].id }; helpFor = juce::String(clsOf(cur().boxes[(size_t) k].text)); rightTab = 0; } }
    if (cmd.contains("ptype=")) { setEditing(true); canvas->openTyper({ 420, 300 }, "obj", -1, cmd.fromFirstOccurrenceOf("ptype=", false, false).upToFirstOccurrenceOf(" ", false, false)); }
    if (cmd.contains("pfind=")) side->search.setText(cmd.fromFirstOccurrenceOf("pfind=", false, false).upToFirstOccurrenceOf(" ", false, false));
    repaint(); right->repaint(); canvas->repaint();
}

} // namespace arco
