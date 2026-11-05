// ARCO 2.4: PATCHER - the control objects (numbers, logic, lists, time, MIDI, memory, sending) - Max's message world.
#include "PatcherLib.h"
#include <set>
#include <map>
#include <regex>

namespace arco { namespace px {

namespace {

// ================================================================== Start here / structure
struct Message : Obj {
    std::vector<Atom> content;
    explicit Message(const Args& a) { setIO(1, 1); if (!a.empty()) setText(a[0].str()); }
    void setText(const std::string& t) { content.clear(); for (auto& tok : tokenize(t)) { bool d = false; for (char c : tok) d |= c == '.'; auto v = parseAtoms(tok); if (tok == "," || tok == ";") content.push_back(Atom::Sy(tok)); else if (!v.empty()) content.push_back(v[0]); } uiGen++; }
    juce::String text;
    juce::String uiText() const override {
        std::string r; for (auto& a : content) { auto s = a.str(); if (s == "," || s == ";") { r += s; continue; } if (!r.empty() && r.back() != ';') r += " "; else if (!r.empty()) r += " "; r += s; }
        return juce::String(r);
    }
    void fire(Ctx& c, const Atom* in, int nIn) {
        uiFlash++;
        Atom seg[MAXA]; int n = 0; bool toSend = false;
        auto flushSeg = [&] {
            if (toSend) { if (n >= 1 && seg[0].t == 's') { Msg m = Msg::fromAtoms(seg + 1, n - 1); c.send(seg[0].s, m); } }
            else if (n > 0) c.out(this, 0, Msg::fromAtoms(seg, n));
            n = 0; };
        for (auto& a : content) {
            if (a.t == 's') {
                auto& s = symStr(a.s);
                if (s == ",") { flushSeg(); continue; }
                if (s == ";") { flushSeg(); toSend = true; continue; }
                if (s.size() == 2 && s[0] == '$' && s[1] >= '1' && s[1] <= '9') { int k = s[1] - '1'; if (n < MAXA) seg[n++] = k < nIn ? in[k] : Atom::I(0); continue; }
            }
            if (n < MAXA) seg[n++] = a;
        }
        flushSeg();
    }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == S::set) { content.assign(m.a, m.a + m.n); uiGen++; return; }
        if (m.sel == sym("append")) { for (int i = 0; i < m.n; ++i) content.push_back(m.a[i]); uiGen++; return; }
        if (m.sel == sym("prepend")) { content.insert(content.begin(), m.a, m.a + m.n); uiGen++; return; }
        Atom in[MAXA]; int n = m.isBang() ? 0 : m.atoms(in);
        fire(c, in, n);
    }
};
struct Print : Obj {
    std::string name = "print";
    explicit Print(const Args& a) { setIO(1, 0); if (!a.empty()) name = a[0].str(); }
    void msg(Ctx& c, int, const Msg& m) override { c.print(boxId, name + ": " + m.text()); }
};
struct Loadbang : Obj {
    explicit Loadbang(const Args&) { setIO(1, 1); }
    void loadbang(Ctx& c) override { c.bang(this, 0); }
    void msg(Ctx& c, int, const Msg&) override { c.bang(this, 0); }
};
struct Loadmess : Obj {
    Args args; explicit Loadmess(const Args& a) : args(a) { setIO(1, 1); }
    void loadbang(Ctx& c) override { c.out(this, 0, Msg::fromAtoms(args.data(), (int) args.size())); }
    void msg(Ctx& c, int, const Msg&) override { loadbang(c); }
};
struct Nothing : Obj { Nothing(int i, int o) { setIO(i, o); } void msg(Ctx&, int, const Msg&) override {} };
struct InletObj : Obj { explicit InletObj(const Args&) { setIO(0, 1); } };
struct OutletObj : Obj { explicit OutletObj(const Args&) { setIO(1, 0); } void msg(Ctx&, int, const Msg&) override {} };

// ================================================================== numbers
enum Op { OAdd, OSub, OMul, ODiv, OMod, ORSub, ORDiv, ORMod, OPow, OMin, OMax, OEq, ONe, OLt, OGt, OLe, OGe, OAnd, OOr, OBAnd, OBOr, OShl, OShr, OXor, OAtan2 };
struct BinOp : Obj {
    Op op; double l = 0, r = 0; bool fl = false;
    BinOp(const Args& a, Op o) : op(o) { setIO(2, 1); r = argF(a, 0, (o == OMul || o == ODiv || o == ORDiv || o == OPow) ? (o == OPow ? 1 : (o == OMul ? 0 : 1)) : 0); if (o == OMul) r = argF(a, 0, 0);
        fl = argIsFloat(a, 0) || op == OPow || op == OAtan2; if (op == OPow && !a.empty() && !argIsFloat(a, 0)) fl = false; }
    double calc() const {
        double L = fl ? l : std::trunc(l), R = fl ? r : std::trunc(r);
        switch (op) {
            case OAdd: return L + R; case OSub: return L - R; case OMul: return L * R;
            case ODiv: return R == 0 ? 0 : (fl ? L / R : std::trunc(L / R)); case ORDiv: return L == 0 ? 0 : (fl ? R / L : std::trunc(R / L));
            case OMod: if (R == 0) return 0; return fl ? std::fmod(L, R) : (double) ((long long) L % (long long) R);
            case ORMod: if (L == 0) return 0; return fl ? std::fmod(R, L) : (double) ((long long) R % (long long) L);
            case ORSub: return R - L; case OPow: return std::pow(L, R); case OMin: return std::min(L, R); case OMax: return std::max(L, R);
            case OEq: return L == R; case ONe: return L != R; case OLt: return L < R; case OGt: return L > R; case OLe: return L <= R; case OGe: return L >= R;
            case OAnd: return (L != 0 && R != 0); case OOr: return (L != 0 || R != 0);
            case OBAnd: return (double) ((long long) L & (long long) R); case OBOr: return (double) ((long long) L | (long long) R); case OXor: return (double) ((long long) L ^ (long long) R);
            case OShl: return (double) ((long long) L << ((long long) R & 63)); case OShr: return (double) ((long long) L >> ((long long) R & 63));
            case OAtan2: return std::atan2(L, R);
        }
        return 0;
    }
    bool intOut() const { return !fl || op == OEq || op == ONe || op == OLt || op == OGt || op == OLe || op == OGe || op == OAnd || op == OOr || op == OBAnd || op == OBOr || op == OShl || op == OShr || op == OXor; }
    void emit(Ctx& c) { double v = calc(); if (intOut()) c.outI(this, 0, v); else c.outF(this, 0, v); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.isNum() || m.isList()) r = m.f(); return; }
        if (m.isBang()) { emit(c); return; }
        if (m.sel == S::set) { l = m.f(); return; }
        if (m.isList()) { l = m.f(0); if (m.n > 1) r = m.f(1); emit(c); return; }
        if (m.isNum()) { l = m.f(); emit(c); return; }
        c.error(this, "takes numbers");
    }
};
struct Unary : Obj {
    std::function<double(double)> f; bool intOut; double last = 0;
    Unary(std::function<double(double)> fn, bool io) : f(std::move(fn)), intOut(io) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.isBang()) { intOut ? c.outI(this, 0, last) : c.outF(this, 0, last); return; }
        if (m.isList()) { Atom o[MAXA]; for (int i = 0; i < m.n; ++i) o[i] = intOut ? Atom::I(f(m.a[i].d())) : Atom::F(f(m.a[i].d())); c.out(this, 0, Msg::fromAtoms(o, m.n)); return; }
        if (!m.isNum()) { c.error(this, "takes numbers"); return; }
        last = f(m.f()); if (intOut) c.outI(this, 0, last); else c.outF(this, 0, last);
    }
};
struct IntBox : Obj {        // [int] / [float]: remember a number, say it again on a bang
    bool isFloat; double v;
    IntBox(const Args& a, bool f) : isFloat(f) { setIO(2, 1); v = argF(a, 0, 0); }
    void say(Ctx& c) { if (isFloat) c.outF(this, 0, v); else c.outI(this, 0, v); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.isNum() || m.isList()) v = m.f(); return; }
        if (m.sel == S::set) { v = m.f(); return; }
        if (m.isNum() || m.isList()) v = m.f();
        say(c);
    }
    juce::var keep() const override { return v; }
    void restore(const juce::var& s) override { v = (double) s; }
};
struct Scale : Obj {
    double il = 0, ih = 127, ol = 0, oh = 1, ex = 1; bool fl = true;
    explicit Scale(const Args& a) { setIO(6, 1); il = argF(a, 0, 0); ih = argF(a, 1, 127); ol = argF(a, 2, 0); oh = argF(a, 3, 1); ex = argF(a, 4, 1);
        fl = a.size() < 4 || argIsFloat(a, 0) || argIsFloat(a, 1) || argIsFloat(a, 2) || argIsFloat(a, 3); }
    double map(double x) const { if (ih == il) return ol; double t = (x - il) / (ih - il); if (ex != 1 && t > 0) t = std::pow(t, ex); return ol + t * (oh - ol); }
    void msg(Ctx& c, int in, const Msg& m) override {
        double v = m.f();
        switch (in) { case 1: il = v; return; case 2: ih = v; return; case 3: ol = v; return; case 4: oh = v; return; case 5: ex = v; return; default: break; }
        if (m.isList()) { Atom o[MAXA]; for (int i = 0; i < m.n; ++i) o[i] = fl ? Atom::F(map(m.a[i].d())) : Atom::I(map(m.a[i].d())); c.out(this, 0, Msg::fromAtoms(o, m.n)); return; }
        if (!m.isNum()) return;
        if (fl) c.outF(this, 0, map(v)); else c.outI(this, 0, map(v));
    }
};
struct Clip : Obj {
    double lo, hi; bool fl;
    explicit Clip(const Args& a) { setIO(3, 1); lo = argF(a, 0, 0); hi = argF(a, 1, 127); fl = argIsFloat(a, 0) || argIsFloat(a, 1); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { lo = m.f(); return; } if (in == 2) { hi = m.f(); return; }
        if (m.isList()) { Atom o[MAXA]; for (int i = 0; i < m.n; ++i) { double v = juce::jlimit(std::min(lo, hi), std::max(lo, hi), m.a[i].d()); o[i] = m.a[i].t == 'f' || fl ? Atom::F(v) : Atom::I(v); } c.out(this, 0, Msg::fromAtoms(o, m.n)); return; }
        double v = juce::jlimit(std::min(lo, hi), std::max(lo, hi), m.f());
        if (fl || (m.n > 0 && m.a[0].t == 'f')) c.outF(this, 0, v); else c.outI(this, 0, v);
    }
};
struct Random : Obj {
    long long range; bool fl;
    explicit Random(const Args& a) { setIO(2, 1); range = juce::jmax(1LL, argI(a, 0, 2)); fl = argIsFloat(a, 0); if (fl) range = 0; frange = argF(a, 0, 1); }
    double frange = 1;
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (fl) frange = m.f(); else range = juce::jmax(1LL, (long long) m.f()); return; }
        if (m.sel == sym("seed")) { c.seed = (unsigned) m.f(); return; }
        if (fl) c.outF(this, 0, (c.rand() / 16777216.0) * frange); else c.outI(this, 0, (double) (c.rand() % (unsigned) range));
    }
};
struct Drunk : Obj {
    long long range, step, v;
    explicit Drunk(const Args& a) { setIO(3, 1); range = juce::jmax(1LL, argI(a, 0, 128)); step = juce::jmax(1LL, argI(a, 1, 2)); v = range / 2; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { range = juce::jmax(1LL, (long long) m.f()); return; } if (in == 2) { step = juce::jmax(1LL, (long long) std::abs(m.f())); return; }
        if (m.isNum()) { v = juce::jlimit(0LL, range - 1, (long long) m.f()); c.outI(this, 0, (double) v); return; }
        if (m.sel == S::set) { v = (long long) m.f(); return; }
        long long d = (long long) (c.rand() % (unsigned) (step * 2 + 1)) - step;
        v = juce::jlimit(0LL, range - 1, v + d); c.outI(this, 0, (double) v);
    }
};
struct Urn : Obj {
    int n; std::vector<int> left;
    explicit Urn(const Args& a) { setIO(2, 2); n = clampN(argI(a, 0, 8), 1, 4096); refill(); }
    void refill() { left.clear(); for (int i = 0; i < n; ++i) left.push_back(i); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { n = clampN((long long) m.f(), 1, 4096); refill(); return; }
        if (m.sel == S::clear || m.sel == S::reset) { refill(); return; }
        if (left.empty()) { c.bang(this, 1); return; }
        int k = (int) (c.rand() % (unsigned) left.size()); int v = left[(size_t) k]; left.erase(left.begin() + k); c.outI(this, 0, v);
    }
};
struct Decide : Obj { explicit Decide(const Args&) { setIO(2, 1); } void msg(Ctx& c, int in, const Msg&) override { if (in == 0) c.outI(this, 0, (c.rand() >> 3) & 1); } };
struct Counter : Obj {
    long long lo = 0, hi = 0x7fffffff, v = 0, carry = 0; int dir = 0; bool goingUp = true; bool first = true;
    explicit Counter(const Args& a) { setIO(4, 4);
        if (a.size() == 1) hi = argI(a, 0, hi); else if (a.size() == 2) { lo = argI(a, 0, 0); hi = argI(a, 1, hi); } else if (a.size() >= 3) { dir = (int) argI(a, 0, 0); lo = argI(a, 1, 0); hi = argI(a, 2, hi); }
        v = lo; }
    void step(Ctx& c) {
        long long out = v;
        if (!first) {
            bool up = dir == 0 || (dir == 2 && goingUp);
            out = v + (up ? 1 : -1);
            if (out > hi) { if (dir == 2) { goingUp = false; out = hi - 1; } else { out = lo; ++carry; c.outI(this, 3, (double) carry); c.outI(this, 2, 1); } }
            else if (out < lo) { if (dir == 2) { goingUp = true; out = lo + 1; } else { out = hi; c.bang(this, 1); } }
            out = juce::jlimit(lo, hi, out);
        }
        first = false; v = out; c.outI(this, 0, (double) v);
    }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { dir = juce::jlimit(0, 2, (int) m.f()); return; }
        if (in == 2) { v = (long long) m.f(); first = true; return; }
        if (in == 3) { hi = (long long) m.f(); return; }
        if (m.sel == S::reset || m.sel == S::clear) { v = lo; first = true; carry = 0; return; }
        if (m.sel == S::set) { v = (long long) m.f(); first = true; return; }
        if (m.sel == sym("jam")) { v = (long long) m.f(); first = false; c.outI(this, 0, (double) v); return; }
        if (m.sel == sym("min")) { lo = (long long) m.f(); return; } if (m.sel == sym("max")) { hi = (long long) m.f(); return; }
        if (m.sel == sym("up")) { dir = 0; return; } if (m.sel == sym("down")) { dir = 1; return; } if (m.sel == sym("updown")) { dir = 2; return; }
        if (m.isNum()) { v = (long long) m.f(); first = true; step(c); return; }
        step(c);
    }
    juce::var keep() const override { return (int) v; }
};
struct Accum : Obj {
    double v; bool fl;
    explicit Accum(const Args& a) { setIO(3, 1); v = argF(a, 0, 0); fl = argIsFloat(a, 0); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { v += m.f(); return; } if (in == 2) { v *= m.f(); return; }
        if (m.sel == S::set) { v = m.f(); return; }
        if (m.isNum()) v = m.f();
        if (fl) c.outF(this, 0, v); else c.outI(this, 0, v);
    }
};
struct Change : Obj {
    double last; explicit Change(const Args& a) { setIO(1, 3); last = argF(a, 0, -1e300); }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == S::set) { last = m.f(); return; }
        if (!m.isNum()) return; double v = m.f();
        if (v == last) return;
        bool wasZero = last == 0; last = v;
        if (v == 0) c.outI(this, 2, 1); else if (wasZero) c.outI(this, 1, 1);
        if (m.a[0].t == 'f') c.outF(this, 0, v); else c.outI(this, 0, v);
    }
};
struct Past : Obj { double th; bool above = false; explicit Past(const Args& a) { setIO(2, 1); th = argF(a, 0, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { th = m.f(); return; } if (m.sel == S::clear) { above = false; return; }
        if (!m.isNum()) return; double v = m.f(); if (v > th && !above) { above = true; c.bang(this, 0); } else if (v < th) above = false; } };
struct Peak : Obj { double v = -1e300; bool trough; Peak(const Args&, bool t) : trough(t) { setIO(2, 3); if (t) v = 1e300; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { v = m.f(); return; } if (m.isBang()) { c.outF(this, 0, v); return; } if (!m.isNum()) return;
        double x = m.f(); bool isNew = trough ? x < v : x > v; if (isNew) { v = x; c.outI(this, 2, 0); c.outI(this, 1, 1); } else { c.outI(this, 2, 1); c.outI(this, 1, 0); }
        if (isNew) { if (m.a[0].t == 'f') c.outF(this, 0, v); else c.outI(this, 0, v); } } };
struct MinMax : Obj { bool isMax; double r; MinMax(const Args& a, bool mx) : isMax(mx) { setIO(2, 2); r = argF(a, 0, mx ? -1e300 : 1e300); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { r = m.f(); return; }
        if (m.isList()) { double b = m.a[0].d(); int at = 0; for (int i = 1; i < m.n; ++i) if (isMax ? m.a[i].d() > b : m.a[i].d() < b) { b = m.a[i].d(); at = i; } c.outI(this, 1, at); c.outF(this, 0, b); return; }
        if (m.isNum()) { double v = isMax ? std::max(m.f(), r) : std::min(m.f(), r); c.outF(this, 0, v); } } };
struct Mean : Obj { double sum = 0; long long n = 0; explicit Mean(const Args&) { setIO(1, 2); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.sel == S::clear) { sum = 0; n = 0; return; }
        if (m.isList()) { double s = 0; for (int i = 0; i < m.n; ++i) s += m.a[i].d(); c.outI(this, 1, m.n); c.outF(this, 0, m.n ? s / m.n : 0); return; }
        if (!m.isNum()) return; sum += m.f(); ++n; c.outI(this, 1, (double) n); c.outF(this, 0, sum / (double) n); } };
struct Histo : Obj { std::vector<int> h; explicit Histo(const Args& a) { setIO(2, 2); h.assign((size_t) clampN(argI(a, 0, 128), 1, 4096), 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::clear) { std::fill(h.begin(), h.end(), 0); return; }
        int k = (int) m.f(); if (k < 0 || k >= (int) h.size()) return; if (in == 0) h[(size_t) k]++;
        c.outI(this, 1, h[(size_t) k]); c.outI(this, 0, k); } };

// ================================================================== logic and choosing
struct Select : Obj {
    std::vector<Atom> keys;
    explicit Select(const Args& a) : keys(a) { if (keys.empty()) keys.push_back(Atom::I(0)); setIO(keys.size() == 1 ? 2 : 1, (int) keys.size() + 1); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { keys[0] = m.n ? m.a[0] : Atom::I(0); return; }
        Atom x; if (m.isNum()) x = m.a[0]; else if (m.sel == S::symbol && m.n) x = m.a[0]; else if (m.n == 0 && m.sel != S::bang && m.sel != S::list) x = Atom::Sy(m.sel); else { c.out(this, (int) keys.size(), m); return; }
        for (size_t k = 0; k < keys.size(); ++k) { bool eq = keys[k].t == 's' ? (x.t == 's' && x.s == keys[k].s) : (x.num() && (long long) x.d() == (long long) keys[k].d() && (keys[k].t == 'i' ? true : x.d() == keys[k].d()));
            if (eq) { c.bang(this, (int) k); return; } }
        c.out(this, (int) keys.size(), m);
    }
};
struct Route : Obj {
    std::vector<Atom> keys; bool pass;
    Route(const Args& a, bool p) : keys(a), pass(p) { if (keys.empty()) keys.push_back(Atom::I(0)); setIO(keys.size() == 1 ? 2 : 1, (int) keys.size() + 1); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { keys[0] = m.n ? m.a[0] : Atom::I(0); return; }
        Atom at[MAXA]; int n = m.atoms(at);
        if (m.isBang()) { at[0] = Atom::Sy(S::bang); n = 1; }
        if (n == 0) { c.out(this, (int) keys.size(), m); return; }
        for (size_t k = 0; k < keys.size(); ++k) {
            bool eq = keys[k].t == 's' ? (at[0].t == 's' && at[0].s == keys[k].s) : (at[0].num() && at[0].d() == keys[k].d());
            if (keys[k].t == 's' && (keys[k].s == S::intS || keys[k].s == S::floatS) && m.isNum()) eq = true;
            if (keys[k].t == 's' && keys[k].s == S::list && m.isList()) eq = true;
            if (keys[k].t == 's' && keys[k].s == S::symbol && m.sel == S::symbol) eq = true;
            if (!eq) continue;
            if (pass) c.out(this, (int) k, m);
            else { bool typed = keys[k].t == 's' && (keys[k].s == S::intS || keys[k].s == S::floatS || keys[k].s == S::list || keys[k].s == S::symbol);
                if (typed) c.out(this, (int) k, m); else c.out(this, (int) k, Msg::fromAtoms(at + 1, n - 1)); }
            return;
        }
        c.out(this, (int) keys.size(), m);
    }
};
struct Gate : Obj {
    int n, open;
    explicit Gate(const Args& a) { n = clampN(argI(a, 0, 1), 1, 64); open = (int) argI(a, 1, 0); setIO(2, n); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 0) { if (m.isNum()) open = juce::jlimit(0, n, (int) m.f()); else if (m.isBang()) open = open ? 0 : 1; return; }
        if (open >= 1 && open <= n) c.out(this, open - 1, m);
    }
};
struct Switch : Obj {
    int n, open;
    explicit Switch(const Args& a) { n = clampN(argI(a, 0, 2), 1, 64); open = (int) argI(a, 1, 0); setIO(n + 1, 1); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 0) { if (m.isNum()) open = juce::jlimit(0, n, (int) m.f()); else if (m.isBang() && open >= 1) c.bang(this, 0); return; }
        if (in == open) c.out(this, 0, m);
    }
};
struct Onebang : Obj { bool ready; explicit Onebang(const Args& a) { setIO(2, 2); ready = argI(a, 0, 0) != 0; }
    void msg(Ctx& c, int in, const Msg&) override { if (in == 1) { ready = true; return; } if (ready) { ready = false; c.bang(this, 0); } else c.bang(this, 1); } };
struct Togedge : Obj { bool on = false; explicit Togedge(const Args&) { setIO(1, 2); }
    void msg(Ctx& c, int, const Msg& m) override { bool v = m.isBang() ? !on : m.f() != 0; if (v == on) return; on = v; c.bang(this, v ? 0 : 1); } };
struct Split : Obj { double lo, hi; bool fl; explicit Split(const Args& a) { setIO(3, 2); lo = argF(a, 0, 0); hi = argF(a, 1, 0); fl = argIsFloat(a, 0) || argIsFloat(a, 1); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { lo = m.f(); return; } if (in == 2) { hi = m.f(); return; } if (!m.isNum()) return;
        double v = m.f(); bool inR = v >= lo && v <= hi; if (m.a[0].t == 'f' || fl) c.outF(this, inR ? 0 : 1, v); else c.outI(this, inR ? 0 : 1, v); } };
struct Speedlim : Obj {
    double ms; bool waiting = false, pending = false; Msg held;
    explicit Speedlim(const Args& a) { setIO(2, 1); ms = argF(a, 0, 100); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { ms = m.f(); return; }
        if (!waiting) { waiting = true; c.out(this, 0, m); c.delay(this, ms); } else { held = m; pending = true; } }
    void tick(Ctx& c, int) override { if (pending) { pending = false; c.out(this, 0, held); c.delay(this, ms); } else waiting = false; }
};
struct Qlim : Speedlim { using Speedlim::Speedlim; };
struct Uzi : Obj {
    int n; bool stopped = false;
    explicit Uzi(const Args& a) { setIO(2, 3); n = clampN(argI(a, 0, 1), 0, 100000); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { n = clampN((long long) m.f(), 0, 100000); return; }
        if (m.sel == S::stop || m.sel == sym("break")) { stopped = true; return; }
        int times = m.isNum() ? clampN((long long) m.f(), 0, 100000) : n;
        stopped = false;
        for (int i = 0; i < times && !stopped; ++i) { c.outI(this, 2, i + 1); c.bang(this, 0); }
        c.bang(this, 1);
    }
};
struct CycleObj : Obj { int n, k = 0; explicit CycleObj(const Args& a) { n = clampN(argI(a, 0, 2), 1, 64); setIO(2, n); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { k = 0; return; } if (m.sel == S::set) { k = juce::jlimit(0, n - 1, (int) m.f()); return; }
        if (m.isList()) { for (int i = 0; i < m.n; ++i) { c.out(this, k, Msg::num(m.a[i])); k = (k + 1) % n; } return; }
        c.out(this, k, m); k = (k + 1) % n; } };
struct Spray : Obj { int n, off; explicit Spray(const Args& a) { n = clampN(argI(a, 0, 2), 1, 64); off = (int) argI(a, 1, 0); setIO(1, n); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.n < 2) return; int k = (int) m.f(0) - off; if (k < 0 || k >= n) return;
        if (m.n == 2) c.out(this, k, Msg::num(m.a[1])); else c.out(this, k, Msg::fromAtoms(m.a + 1, m.n - 1)); } };
struct Funnel : Obj { int off; explicit Funnel(const Args& a) { setIO(clampN(argI(a, 0, 2), 1, 64), 1); off = (int) argI(a, 1, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { Atom o[MAXA]; o[0] = Atom::I(in + off); int n = 1 + m.atoms(o + 1, MAXA - 1); c.out(this, 0, Msg::fromAtoms(o, n)); } };
struct Buddy : Obj { int n; std::vector<Msg> held; std::vector<bool> got; explicit Buddy(const Args& a) { n = clampN(argI(a, 0, 2), 1, 16); setIO(n, n); held.resize((size_t) n); got.assign((size_t) n, false); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.sel == S::clear) { std::fill(got.begin(), got.end(), false); return; }
        held[(size_t) in] = m; got[(size_t) in] = true; for (bool g : got) if (!g) return;
        std::fill(got.begin(), got.end(), false); for (int k = n - 1; k >= 0; --k) c.out(this, k, held[(size_t) k]); } };
struct Swap : Obj { double r; bool fl; explicit Swap(const Args& a) { setIO(2, 2); r = argF(a, 0, 0); fl = argIsFloat(a, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { r = m.f(); return; } double l = m.isList() ? m.f(0) : m.f(); if (m.isList() && m.n > 1) r = m.f(1);
        if (fl) c.outF(this, 1, l); else c.outI(this, 1, l); if (fl) c.outF(this, 0, r); else c.outI(this, 0, r); } };
struct Bucket : Obj { std::vector<double> v; explicit Bucket(const Args& a) { int n = clampN(argI(a, 0, 1), 1, 64); setIO(1, n); v.assign((size_t) n, 0.0); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.sel == S::set) { std::fill(v.begin(), v.end(), m.f()); return; }
        if (m.isNum()) { for (int k = (int) v.size() - 1; k > 0; --k) v[(size_t) k] = v[(size_t) k - 1]; v[0] = m.f(); }
        for (int k = (int) v.size() - 1; k >= 0; --k) c.outF(this, k, v[(size_t) k]); } };
struct Bang : Obj { explicit Bang(const Args& a) { setIO(1, clampN(argI(a, 0, 2), 1, 64)); }
    void msg(Ctx& c, int, const Msg&) override { for (int k = nOut - 1; k >= 0; --k) c.bang(this, k); } };
struct Trigger : Obj {
    std::vector<Atom> types;
    explicit Trigger(const Args& a) : types(a) { if (types.empty()) { types.push_back(Atom::Sy("i")); types.push_back(Atom::Sy("i")); } setIO(1, (int) types.size()); }
    void msg(Ctx& c, int, const Msg& m) override {
        for (int k = (int) types.size() - 1; k >= 0; --k) {
            auto& t = types[(size_t) k];
            if (t.num()) { c.out(this, k, Msg::num(t)); continue; }
            auto& s = symStr(t.s);
            if (s == "b" || s == "bang") c.bang(this, k);
            else if (s == "i" || s == "int") c.outI(this, k, m.isBang() ? 0 : m.f());
            else if (s == "f" || s == "float") c.outF(this, k, m.isBang() ? 0 : m.f());
            else if (s == "l" || s == "list" || s == "a" || s == "anything") c.out(this, k, m);
            else if (s == "s" || s == "sym" || s == "symbol") { if (m.sel == S::symbol) c.out(this, k, m); else if (m.n == 0 && !m.isBang()) c.out(this, k, Msg::symbolM(m.sel)); else c.out(this, k, Msg::symbolM(m.n ? sym(m.a[0].str()) : S::bang)); }
            else c.out(this, k, Msg::word(t.s));
        }
    }
};

// ================================================================== lists and words
struct Pack : Obj {
    std::vector<Atom> v; bool pak;
    Pack(const Args& a, bool p) : v(a), pak(p) { if (v.empty()) { v.push_back(Atom::I(0)); v.push_back(Atom::I(0)); } for (auto& x : v) if (x.t == 's') { auto& s = symStr(x.s); if (s == "f") x = Atom::F(0); else if (s == "i") x = Atom::I(0); else if (s == "s" || s == "sym") x = Atom::Sy(""); } setIO((int) v.size(), 1); }
    void put(int k, const Atom& a) { if (k < 0 || k >= (int) v.size()) return; if (v[(size_t) k].t == 'i' && a.num()) v[(size_t) k] = Atom::I(a.d()); else if (v[(size_t) k].t == 'f' && a.num()) v[(size_t) k] = Atom::F(a.d()); else v[(size_t) k] = a; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (m.sel == S::set) { if (m.n) put(in, m.a[0]); return; }
        if (in == 0 && m.isList()) { for (int k = 0; k < m.n && k < (int) v.size(); ++k) put(k, m.a[k]); c.out(this, 0, Msg::fromAtoms(v.data(), (int) v.size())); return; }
        if (!m.isBang()) { if (m.n) put(in, m.a[0]); else put(in, Atom::Sy(m.sel)); }
        if (in == 0 || pak) c.out(this, 0, Msg::fromAtoms(v.data(), (int) v.size()));
    }
};
struct Unpack : Obj {
    std::vector<Atom> types;
    explicit Unpack(const Args& a) : types(a) { if (types.empty()) { types.push_back(Atom::I(0)); types.push_back(Atom::I(0)); } setIO(1, (int) types.size()); }
    void msg(Ctx& c, int, const Msg& m) override {
        Atom at[MAXA]; int n = m.atoms(at);
        for (int k = juce::jmin(n, (int) types.size()) - 1; k >= 0; --k) {
            auto& t = types[(size_t) k]; bool wantF = t.t == 'f' || (t.t == 's' && symStr(t.s) == "f");
            bool wantI = t.t == 'i' || (t.t == 's' && symStr(t.s) == "i");
            if (at[k].num() && wantF) c.outF(this, k, at[k].d()); else if (at[k].num() && wantI) c.outI(this, k, at[k].d()); else c.out(this, k, Msg::num(at[k]));
        }
    }
};
struct Join : Obj {
    int n; std::vector<std::vector<Atom>> parts;
    explicit Join(const Args& a) { n = clampN(argI(a, 0, 2), 1, 32); setIO(n, 1); parts.resize((size_t) n); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (!m.isBang()) { Atom at[MAXA]; int k = m.atoms(at); parts[(size_t) in].assign(at, at + k); }
        if (in != 0) return;
        std::vector<Atom> all; for (auto& p : parts) all.insert(all.end(), p.begin(), p.end());
        c.out(this, 0, Msg::fromAtoms(all.data(), juce::jmin(MAXA, (int) all.size())));
    }
};
struct Iter : Obj { explicit Iter(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { Atom at[MAXA]; int n = m.atoms(at); for (int i = 0; i < n; ++i) c.out(this, 0, Msg::num(at[i])); } };
struct Prepend : Obj {
    std::vector<Atom> pre; bool app;
    Prepend(const Args& a, bool ap) : pre(a), app(ap) { setIO(2, 1); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1 || m.sel == S::set) { Atom at[MAXA]; int n = m.atoms(at); pre.assign(at, at + n); return; }
        Atom at[MAXA]; int n = 0; Atom body[MAXA]; int bn = m.isBang() ? 0 : m.atoms(body);
        if (m.isBang()) { body[0] = Atom::Sy(S::bang); bn = 1; }
        if (!app) for (auto& x : pre) if (n < MAXA) at[n++] = x;
        for (int i = 0; i < bn && n < MAXA; ++i) at[n++] = body[i];
        if (app) for (auto& x : pre) if (n < MAXA) at[n++] = x;
        c.out(this, 0, Msg::fromAtoms(at, n));
    }
};
struct Zl : Obj {
    std::string mode; std::vector<Atom> right; int arg = 0; std::vector<Atom> acc; int idx = 0;
    explicit Zl(const Args& a) {
        mode = a.empty() ? "group" : a[0].str(); if (a.size() > 1 && a[1].num()) arg = (int) a[1].d();
        for (size_t i = 1; i < a.size(); ++i) right.push_back(a[i]);
        int outs = 2;
        setIO(2, outs);
        static const std::set<std::string> known = { "group", "iter", "len", "nth", "rev", "rot", "sort", "slice", "join", "sub", "sum", "median", "mean", "reg", "lookup", "union", "sect",
            "filter", "delace", "lace", "stream", "change", "scramble", "ecils", "thin", "unique", "indexmap", "queue", "stack", "mth", "max", "min", "compare", "swap", "ecils", "first", "last", "head", "tail" };
        if (!known.count(mode)) err = "zl doesn't know the mode \"" + juce::String(mode) + "\"";
        if (mode == "group" || mode == "stream" || mode == "iter") { if (arg <= 0) arg = mode == "iter" ? 1 : 2; }
    }
    void out(Ctx& c, int k, const std::vector<Atom>& v) { c.out(this, k, Msg::fromAtoms(v.data(), juce::jmin(MAXA, (int) v.size()))); }
    void msg(Ctx& c, int in, const Msg& m) override {
        Atom at[MAXA]; int n = m.isBang() ? 0 : m.atoms(at); std::vector<Atom> L(at, at + n);
        if (in == 1) { right = L; if (!L.empty() && L[0].num()) arg = (int) L[0].d(); return; }
        if (m.sel == S::clear) { acc.clear(); return; }
        auto& M = mode;
        if (M == "group") { if (m.isBang()) { out(c, 0, acc); acc.clear(); return; } for (auto& x : L) { acc.push_back(x); if ((int) acc.size() >= arg) { out(c, 0, acc); acc.clear(); } } return; }
        if (M == "stream") { for (auto& x : L) { acc.push_back(x); if ((int) acc.size() > arg) acc.erase(acc.begin()); if ((int) acc.size() == arg) out(c, 0, acc); } return; }
        if (M == "iter") { for (size_t i = 0; i < L.size(); i += (size_t) juce::jmax(1, arg)) out(c, 0, std::vector<Atom>(L.begin() + (long) i, L.begin() + (long) std::min(L.size(), i + (size_t) juce::jmax(1, arg)))); return; }
        if (M == "len") { c.outI(this, 0, (double) L.size()); return; }
        if (M == "nth" || M == "mth") { int k = arg - (M == "nth" ? 1 : 0); if (k >= 0 && k < (int) L.size()) c.out(this, 0, Msg::num(L[(size_t) k])); else c.out(this, 1, m); return; }
        if (M == "rev") { std::reverse(L.begin(), L.end()); out(c, 0, L); return; }
        if (M == "rot") { if (!L.empty()) { int r = ((arg % (int) L.size()) + (int) L.size()) % (int) L.size(); std::rotate(L.begin(), L.begin() + (long) ((int) L.size() - r) % (long) L.size(), L.end()); } out(c, 0, L); return; }
        if (M == "sort") { std::stable_sort(L.begin(), L.end(), [&](const Atom& a, const Atom& b) { if (a.num() && b.num()) return arg < 0 ? a.d() > b.d() : a.d() < b.d(); return a.str() < b.str(); }); out(c, 0, L); return; }
        if (M == "slice" || M == "head") { int k = juce::jlimit(0, (int) L.size(), arg); out(c, 1, std::vector<Atom>(L.begin() + k, L.end())); out(c, 0, std::vector<Atom>(L.begin(), L.begin() + k)); return; }
        if (M == "ecils" || M == "tail") { int k = juce::jlimit(0, (int) L.size(), (int) L.size() - arg); out(c, 1, std::vector<Atom>(L.begin() + k, L.end())); out(c, 0, std::vector<Atom>(L.begin(), L.begin() + k)); return; }
        if (M == "join") { auto r = L; r.insert(r.end(), right.begin(), right.end()); out(c, 0, r); return; }
        if (M == "sub") { for (size_t i = 0; i + right.size() <= L.size() && !right.empty(); ++i) { bool ok = true; for (size_t j = 0; j < right.size(); ++j) ok &= L[i + j] == right[j]; if (ok) { c.outI(this, 0, (double) i + 1); return; } } c.outI(this, 0, 0); return; }
        if (M == "sum") { double s = 0; bool fl = false; for (auto& x : L) { s += x.d(); fl |= x.t == 'f'; } if (fl) c.outF(this, 0, s); else c.outI(this, 0, s); return; }
        if (M == "mean") { double s = 0; for (auto& x : L) s += x.d(); c.outF(this, 0, L.empty() ? 0 : s / (double) L.size()); return; }
        if (M == "median") { std::vector<double> v; for (auto& x : L) v.push_back(x.d()); if (v.empty()) return; std::sort(v.begin(), v.end()); c.outF(this, 0, v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) * 0.5); return; }
        if (M == "max" || M == "min") { if (L.empty()) return; double b = L[0].d(); for (auto& x : L) b = M == "max" ? std::max(b, x.d()) : std::min(b, x.d()); c.outF(this, 0, b); return; }
        if (M == "reg") { if (m.isBang()) { out(c, 0, right); return; } right = L; return; }
        if (M == "lookup") { for (auto& x : L) { int k = (int) x.d(); if (k >= 0 && k < (int) right.size()) c.out(this, 0, Msg::num(right[(size_t) k])); } return; }
        if (M == "union") { auto r = L; for (auto& x : right) if (std::find(r.begin(), r.end(), x) == r.end()) r.push_back(x); out(c, 0, r); return; }
        if (M == "sect") { std::vector<Atom> r; for (auto& x : L) if (std::find(right.begin(), right.end(), x) != right.end()) r.push_back(x); out(c, 0, r); return; }
        if (M == "filter") { std::vector<Atom> r; for (auto& x : L) if (std::find(right.begin(), right.end(), x) == right.end()) r.push_back(x); out(c, 0, r); return; }
        if (M == "delace") { std::vector<Atom> a, b; for (size_t i = 0; i < L.size(); ++i) (i % 2 ? b : a).push_back(L[i]); out(c, 1, b); out(c, 0, a); return; }
        if (M == "lace") { std::vector<Atom> r; for (size_t i = 0; i < std::max(L.size(), right.size()); ++i) { if (i < L.size()) r.push_back(L[i]); if (i < right.size()) r.push_back(right[i]); } out(c, 0, r); return; }
        if (M == "change") { if (L == acc) return; acc = L; out(c, 0, L); return; }
        if (M == "scramble") { for (int i = (int) L.size() - 1; i > 0; --i) std::swap(L[(size_t) i], L[(size_t) (c.rand() % (unsigned) (i + 1))]); out(c, 0, L); return; }
        if (M == "thin" || M == "unique") { std::vector<Atom> r; for (auto& x : L) if (std::find(r.begin(), r.end(), x) == r.end()) r.push_back(x); out(c, 0, r); return; }
        if (M == "indexmap") { std::vector<Atom> r; for (auto& x : right) { int k = (int) x.d(); if (k >= 0 && k < (int) L.size()) r.push_back(L[(size_t) k]); } out(c, 0, r); return; }
        if (M == "queue") { if (m.isBang()) { if (!acc.empty()) { auto x = acc.front(); acc.erase(acc.begin()); c.out(this, 0, Msg::num(x)); } else c.bang(this, 1); return; } for (auto& x : L) acc.push_back(x); return; }
        if (M == "stack") { if (m.isBang()) { if (!acc.empty()) { auto x = acc.back(); acc.pop_back(); c.out(this, 0, Msg::num(x)); } else c.bang(this, 1); return; } for (auto& x : L) acc.push_back(x); return; }
        if (M == "compare") { c.outI(this, 0, L == right ? 1 : 0); return; }
        if (M == "swap") { if (L.size() >= 2) std::swap(L[0], L[1]); out(c, 0, L); return; }
        if (M == "first") { if (!L.empty()) c.out(this, 0, Msg::num(L.front())); return; }
        if (M == "last") { if (!L.empty()) c.out(this, 0, Msg::num(L.back())); return; }
    }
};
struct Sprintf : Obj {
    std::string fmt; int nArgs = 0; std::vector<Atom> vals; bool symout = false;
    explicit Sprintf(const Args& a) {
        size_t s0 = 0; if (!a.empty() && a[0].str() == "symout") { symout = true; s0 = 1; }
        for (size_t i = s0; i < a.size(); ++i) { if (i > s0) fmt += " "; fmt += a[i].str(); }
        for (size_t i = 0; i + 1 < fmt.size(); ++i) if (fmt[i] == '%') { if (fmt[i + 1] == '%') { ++i; continue; } ++nArgs; }
        vals.assign((size_t) juce::jmax(1, nArgs), Atom::I(0)); setIO(juce::jmax(1, nArgs), 1);
    }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (m.isList() && in == 0) { for (int k = 0; k < m.n && k < (int) vals.size(); ++k) vals[(size_t) k] = m.a[k]; }
        else if (!m.isBang()) vals[(size_t) in] = m.n ? m.a[0] : Atom::Sy(m.sel);
        if (in != 0) return;
        std::string r; int k = 0;
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%' || i + 1 >= fmt.size()) { r += fmt[i]; continue; }
            if (fmt[i + 1] == '%') { r += '%'; ++i; continue; }
            size_t j = i + 1; while (j < fmt.size() && std::strchr("0123456789.-+ #", fmt[j])) ++j;
            if (j >= fmt.size()) break;
            std::string spec = fmt.substr(i, j - i + 1); char t = fmt[j]; char buf[256];
            auto& v = vals[(size_t) juce::jmin(k, (int) vals.size() - 1)]; ++k;
            if (t == 'd' || t == 'i' || t == 'x' || t == 'X' || t == 'c' || t == 'o' || t == 'u') { spec.back() = t; spec.insert(spec.size() - 1, "ll"); std::snprintf(buf, sizeof buf, spec.c_str(), (long long) v.d()); }
            else if (t == 'f' || t == 'g' || t == 'e') std::snprintf(buf, sizeof buf, spec.c_str(), v.d());
            else std::snprintf(buf, sizeof buf, "%s", v.str().c_str());
            r += buf; i = j;
        }
        auto at = parseAtoms(r);
        if (symout || at.size() != 1) c.out(this, 0, Msg::symbolM(sym(r))); else c.out(this, 0, Msg::fromAtoms(at.data(), (int) at.size()));
    }
};
struct FromSymbol : Obj { explicit FromSymbol(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { std::string s = m.sel == S::symbol && m.n ? m.a[0].str() : m.text(); auto at = parseAtoms(s); c.out(this, 0, Msg::fromAtoms(at.data(), juce::jmin(MAXA, (int) at.size()))); } };
struct ToSymbol : Obj { explicit ToSymbol(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { c.out(this, 0, Msg::symbolM(sym(m.text()))); } };
struct Atoi : Obj { explicit Atoi(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { std::string s = m.sel == S::symbol && m.n ? m.a[0].str() : m.text(); Atom o[MAXA]; int n = 0; for (unsigned char ch : s) if (n < MAXA) o[n++] = Atom::I(ch); c.out(this, 0, Msg::fromAtoms(o, n)); } };
struct Itoa : Obj { explicit Itoa(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { std::string s; for (int i = 0; i < m.n; ++i) s += (char) m.a[i].i(); c.out(this, 0, Msg::symbolM(sym(s))); } };
struct Regexp : Obj {
    std::regex re; bool ok = true; std::string sub; bool hasSub = false;
    explicit Regexp(const Args& a) { setIO(2, 4); std::string p; size_t i = 0; for (; i < a.size(); ++i) { auto s = a[i].str(); if (s == "@substitute" && i + 1 < a.size()) { sub = a[i + 1].str(); hasSub = true; ++i; continue; } if (!p.empty()) p += " "; p += s; }
        try { re = std::regex(p); } catch (...) { ok = false; err = "this pattern is not a regular expression"; } }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (!ok || in == 1) return;
        std::string s = m.sel == S::symbol && m.n ? m.a[0].str() : m.text();
        try {
            if (hasSub) { c.out(this, 0, Msg::symbolM(sym(std::regex_replace(s, re, sub)))); return; }
            std::smatch sm;
            if (std::regex_search(s, sm, re)) { Atom o[MAXA]; int n = 0; for (size_t g = 1; g < sm.size() && n < MAXA; ++g) { auto v = parseAtoms(sm[g].str()); o[n++] = v.empty() ? Atom::Sy(sm[g].str()) : v[0]; }
                if (n) c.out(this, 1, Msg::fromAtoms(o, n)); c.out(this, 0, Msg::symbolM(sym(sm[0].str()))); }
            else c.out(this, 3, m);
        } catch (...) {}
    }
};
struct Substitute : Obj { Atom from, to; explicit Substitute(const Args& a) { setIO(3, 2); from = a.size() > 0 ? a[0] : Atom::I(0); to = a.size() > 1 ? a[1] : Atom::I(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { if (m.n) from = m.a[0]; return; } if (in == 2) { if (m.n) to = m.a[0]; return; }
        Atom at[MAXA]; int n = m.atoms(at); bool any = false; for (int i = 0; i < n; ++i) if (at[i] == from) { at[i] = to; any = true; }
        if (any) c.out(this, 0, Msg::fromAtoms(at, n)); else c.out(this, 1, m); } };
struct Thresh : Obj { double ms; std::vector<Atom> acc; explicit Thresh(const Args& a) { setIO(2, 1); ms = argF(a, 0, 10); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { ms = m.f(); return; } Atom at[MAXA]; int n = m.atoms(at); for (int i = 0; i < n; ++i) if (acc.size() < MAXA) acc.push_back(at[i]); c.delay(this, ms); }
    void tick(Ctx& c, int) override { c.out(this, 0, Msg::fromAtoms(acc.data(), (int) acc.size())); acc.clear(); } };
struct ExprObj : Obj {
    Expr e;
    std::vector<double> in;
    explicit ExprObj(const Args& a) { std::string t; for (auto& x : a) { if (!t.empty()) t += " "; t += x.str(); }
        if (!e.parse(t)) err = "expr: " + e.err; setIO(juce::jmax(1, e.maxIn), 1); in.assign((size_t) juce::jmax(1, e.maxIn), 0.0); }
    void msg(Ctx& c, int k, const Msg& m) override {
        if (m.isList()) { for (int i = 0; i < m.n && i < (int) in.size(); ++i) in[(size_t) i] = m.a[i].d(); }
        else if (m.isNum()) in[(size_t) k] = m.f();
        else if (!m.isBang()) return;
        if (k != 0) return;
        double v = e.eval(in.data(), (int) in.size());
        if (e.intMode) c.outI(this, 0, v); else c.outF(this, 0, v);
    }
};
struct VExpr : Obj {      // [vexpr $f1 * 2]: the expression on every item of the lists
    Expr e; std::vector<std::vector<double>> in;
    explicit VExpr(const Args& a) { std::string t; for (auto& x : a) { if (!t.empty()) t += " "; t += x.str(); } if (!e.parse(t)) err = "vexpr: " + e.err; setIO(juce::jmax(1, e.maxIn), 1); in.resize((size_t) juce::jmax(1, e.maxIn)); }
    void msg(Ctx& c, int k, const Msg& m) override {
        Atom at[MAXA]; int n = m.atoms(at); if (!m.isBang()) { in[(size_t) k].clear(); for (int i = 0; i < n; ++i) in[(size_t) k].push_back(at[i].d()); }
        if (k != 0) return;
        size_t len = in[0].size(); Atom o[MAXA]; int on = 0;
        for (size_t i = 0; i < len && on < MAXA; ++i) { double v[9] = {}; for (size_t j = 0; j < in.size() && j < 9; ++j) v[j] = in[j].empty() ? 0 : in[j][std::min(i, in[j].size() - 1)];
            double r = e.eval(v, (int) in.size()); o[on++] = e.intMode ? Atom::I(r) : Atom::F(r); }
        c.out(this, 0, Msg::fromAtoms(o, on));
    }
};
// [if $i1 > 3 then 1 else 0] / [if $f1 then out2 $f1]
struct IfObj : Obj {
    Expr cond, a, b; bool hasElse = false, aOut2 = false, bOut2 = false, aSet = false; std::vector<double> in;
    std::vector<Atom> aWords, bWords; bool aIsExpr = true, bIsExpr = true;
    explicit IfObj(const Args& args) {
        std::string t; for (auto& x : args) { if (!t.empty()) t += " "; t += x.str(); }
        auto th = t.find(" then "); if (th == std::string::npos) { err = "if needs: if (test) then (what) [else (what)]"; setIO(1, 1); return; }
        std::string c = t.substr(0, th), rest = t.substr(th + 6), x = rest, y;
        auto el = rest.find(" else "); if (el != std::string::npos) { x = rest.substr(0, el); y = rest.substr(el + 6); hasElse = true; }
        auto prep = [&](std::string& s, bool& out2, Expr& e, bool& isExpr, std::vector<Atom>& words) {
            while (!s.empty() && s[0] == ' ') s.erase(0, 1);
            if (s.rfind("out2 ", 0) == 0) { out2 = true; s = s.substr(5); }
            if (!e.parse(s)) { isExpr = false; words = parseAtoms(s); } };
        cond.parse(c); if (cond.err.isNotEmpty()) err = "if: " + cond.err;
        prep(x, aOut2, a, aIsExpr, aWords); if (hasElse) prep(y, bOut2, b, bIsExpr, bWords);
        int mx = juce::jmax(cond.maxIn, a.maxIn, hasElse ? b.maxIn : 1);
        setIO(mx, (aOut2 || bOut2) ? 2 : 1); in.assign((size_t) mx, 0.0);
    }
    void msg(Ctx& c, int k, const Msg& m) override {
        if (m.isList()) for (int i = 0; i < m.n && i < (int) in.size(); ++i) in[(size_t) i] = m.a[i].d(); else if (m.isNum()) in[(size_t) k] = m.f();
        if (k != 0 || in.empty()) return;
        bool t = cond.eval(in.data(), (int) in.size()) != 0;
        if (!t && !hasElse) return;
        Expr& e = t ? a : b; bool isE = t ? aIsExpr : bIsExpr; int o = (t ? aOut2 : bOut2) ? 1 : 0;
        if (isE) { double v = e.eval(in.data(), (int) in.size()); if (e.intMode) c.outI(this, o, v); else c.outF(this, o, v); }
        else { auto& w = t ? aWords : bWords; c.out(this, o, Msg::fromAtoms(w.data(), (int) w.size())); }
    }
};

// ================================================================== time
struct Metro : Obj {
    Atom interval; bool on = false;
    explicit Metro(const Args& a) { setIO(2, 1); interval = a.empty() ? Atom::I(500) : a[0]; for (size_t i = 0; i + 1 < a.size(); ++i) if (a[i].str() == "@interval") interval = a[i + 1]; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.n) interval = m.a[0]; return; }
        if (m.sel == S::stop || (m.isNum() && m.f() == 0)) { on = false; c.cancel(this); uiValue = 0; return; }
        if (m.sel == sym("interval")) { if (m.n) interval = m.a[0]; return; }
        on = true; uiValue = 1; c.bang(this, 0); c.delay(this, juce::jmax(1.0, c.noteMs(interval, 500)));
    }
    void tick(Ctx& c, int) override { if (!on) return; c.delay(this, juce::jmax(1.0, c.noteMs(interval, 500))); uiFlash++; c.bang(this, 0); }
};
struct Delay : Obj {
    Atom time;
    explicit Delay(const Args& a) { setIO(2, 1); time = a.empty() ? Atom::I(0) : a[0]; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.n) time = m.a[0]; return; }
        if (m.sel == S::stop) { c.cancel(this); return; }
        if (m.isNum()) time = m.a[0];
        if (m.sel == sym("delay") && m.n) time = m.a[0];
        c.delay(this, c.noteMs(time, 0));
    }
    void tick(Ctx& c, int) override { c.bang(this, 0); }
};
struct Pipe : Obj {
    double ms; std::vector<Atom> held; bool fl;
    struct P { Msg m; bool used = false; }; std::vector<P> q;
    explicit Pipe(const Args& a) : held(a) { if (held.empty()) held.push_back(Atom::I(0)); ms = argF(a, a.size() > 1 ? a.size() - 1 : 0, 0); if (a.size() <= 1) { held = { Atom::I(0) }; ms = argF(a, 0, 0); } else held.pop_back();
        fl = !held.empty() && held[0].t == 'f'; setIO((int) held.size() + 1, (int) held.size()); q.resize(64); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == (int) held.size()) { ms = m.f(); return; }
        if (m.sel == S::stop || m.sel == S::clear) { for (int t = 0; t < 64; ++t) { c.cancel(this, t); q[(size_t) t].used = false; } return; }
        if (m.sel == sym("flush")) { for (int t = 0; t < 64; ++t) if (q[(size_t) t].used) { c.cancel(this, t); tick(c, t); } return; }
        if (in > 0) { if (m.n) held[(size_t) in] = m.a[0]; return; }
        Atom at[MAXA]; int n = m.atoms(at);
        if (m.isList() && n > (int) held.size()) ms = at[held.size()].d();
        for (int k = 0; k < n && k < (int) held.size(); ++k) held[(size_t) k] = at[k];
        for (int t = 0; t < 64; ++t) if (!q[(size_t) t].used) { q[(size_t) t].used = true; q[(size_t) t].m = Msg::fromAtoms(held.data(), (int) held.size()); c.delay(this, ms, t); return; }
    }
    void tick(Ctx& c, int t) override {
        if (t < 0 || t >= 64 || !q[(size_t) t].used) return; q[(size_t) t].used = false;
        auto& m = q[(size_t) t].m; Atom at[MAXA]; int n = m.atoms(at);
        for (int k = juce::jmin(n, nOut) - 1; k >= 0; --k) c.out(this, k, Msg::num(at[k]));
    }
};
struct Deferlow : Obj {
    std::vector<Msg> q; int head = 0, count = 0;
    explicit Deferlow(const Args&) { setIO(1, 1); q.resize(32); }
    void msg(Ctx& c, int, const Msg& m) override { if (count >= 32) return; q[(size_t) ((head + count) % 32)] = m; ++count; c.delay(this, 0, 100 + (head + count) % 32); }
    void tick(Ctx& c, int) override { if (count == 0) return; Msg m = q[(size_t) head]; head = (head + 1) % 32; --count; c.out(this, 0, m); }
};
// [line]: glides to a number in a time, saying every step ([line 0. 20]: a step each 20 ms)
struct LineObj : Obj {
    double v = 0, target = 0, t0 = 0, t1 = 0, v0 = 0, grain = 20, ramp = 0; bool fl;
    explicit LineObj(const Args& a) { setIO(3, 2); v = argF(a, 0, 0); grain = juce::jmax(1.0, argF(a, 1, 20)); fl = argIsFloat(a, 0); }
    void say(Ctx& c) { if (fl) c.outF(this, 0, v); else c.outI(this, 0, v); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { ramp = m.f(); return; } if (in == 2) { grain = juce::jmax(1.0, m.f()); return; }
        if (m.sel == S::stop) { c.cancel(this); return; }
        if (m.sel == S::set) { c.cancel(this); v = m.f(); return; }
        double to = m.f(0), time = m.isList() && m.n > 1 ? m.f(1) : ramp; if (m.isList() && m.n > 2) grain = juce::jmax(1.0, m.f(2));
        ramp = 0;
        if (time <= 0) { c.cancel(this); v = to; say(c); c.bang(this, 1); return; }
        v0 = v; target = to; t0 = c.nowMs; t1 = c.nowMs + time; c.delay(this, 0);
    }
    void tick(Ctx& c, int) override {
        double t = c.nowMs;
        if (t >= t1) { v = target; say(c); c.bang(this, 1); return; }
        v = v0 + (target - v0) * (t - t0) / (t1 - t0); say(c);
        c.delay(this, juce::jmin(grain, t1 - t));
    }
};
struct Timer : Obj { double t0 = 0; explicit Timer(const Args&) { setIO(2, 1); }
    void msg(Ctx& c, int in, const Msg&) override { if (in == 0) t0 = c.nowMs; else c.outF(this, 0, c.nowMs - t0); } };
struct Clocker : Obj { double ms, t0 = 0; bool on = false; explicit Clocker(const Args& a) { setIO(2, 1); ms = argF(a, 0, 100); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { ms = m.f(); return; } if (m.sel == S::stop || (m.isNum() && m.f() == 0)) { on = false; c.cancel(this); return; } on = true; t0 = c.nowMs; c.outF(this, 0, 0); c.delay(this, ms); }
    void tick(Ctx& c, int) override { if (!on) return; c.outF(this, 0, c.nowMs - t0); c.delay(this, ms); } };
struct Cpuclock : Obj { explicit Cpuclock(const Args&) { setIO(1, 1); } void msg(Ctx& c, int, const Msg&) override { c.outF(this, 0, juce::Time::getMillisecondCounterHiRes()); } };
struct DateObj : Obj { explicit DateObj(const Args&) { setIO(1, 3); }
    void msg(Ctx& c, int, const Msg& m) override { auto t = juce::Time::getCurrentTime();
        Atom tm[3] = { Atom::I(t.getHours()), Atom::I(t.getMinutes()), Atom::I(t.getSeconds()) }; Atom dt[3] = { Atom::I(t.getMonth() + 1), Atom::I(t.getDayOfMonth()), Atom::I(t.getYear()) };
        if (m.sel == sym("ticks")) { c.outF(this, 2, juce::Time::getMillisecondCounterHiRes()); return; }
        if (m.sel == sym("date")) c.out(this, 0, Msg::fromAtoms(dt, 3)); else c.out(this, 1, Msg::fromAtoms(tm, 3)); } };
// [beat 16n]: bangs on the app's beat grid - in time with the loops, the drums, the studio
struct Beat : Obj {
    double div = 1; long long last = -1; bool on = true;
    explicit Beat(const Args& a) : Obj() { setIO(2, 2); wantsBeat = true; setDiv(a.empty() ? Atom::Sy("4n") : a[0]); }
    void setDiv(const Atom& x) { if (x.num()) { div = juce::jmax(0.01, x.d()); return; } auto s = symStr(x.s); double n = std::atof(s.c_str()); if (n <= 0) n = 4;
        div = 4.0 / n; if (s.find('t') != std::string::npos) div *= 2.0 / 3.0; if (s.find('d') != std::string::npos) div *= 1.5; if (s == "bar" || s == "1bar") div = 4; }
    void msg(Ctx&, int in, const Msg& m) override { if (in == 1) { if (m.n) setDiv(m.a[0]); return; } if (m.isNum()) on = m.f() != 0; else if (m.sel == S::stop) on = false; else if (m.sel == S::start) on = true; }
    void beatPoll(Ctx& c) override { poll(c, c.beat); }
    void poll(Ctx& c, double beatNow) { long long k = (long long) std::floor(beatNow / div + 1e-9); if (k != last) { bool first = last < 0; last = k; if (!first && on) { c.outI(this, 1, (double) k); c.bang(this, 0); uiFlash++; } } }
};
// [transport]: the app's clock - running, tempo, bar, beat; play / stop / tempo 120 change it
struct Transport : Obj {
    explicit Transport(const Args&) { setIO(1, 5); }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == sym("play") || m.sel == S::start || (m.isNum() && m.f() != 0)) { c.action("play", 1); return; }
        if (m.sel == S::stop || (m.isNum() && m.f() == 0)) { c.action("play", 0); return; }
        if (m.sel == sym("tempo") && m.n) { c.action("tempo", m.f()); return; }
        double b = c.beat; int bpb = juce::jmax(1, c.beatsPerBar);
        c.outF(this, 4, b); c.outI(this, 3, ((long long) std::floor(b)) % bpb + 1); c.outI(this, 2, std::floor(b / bpb) + 1); c.outF(this, 1, c.bpm); c.outI(this, 0, c.running ? 1 : 0);
    }
};
struct TempoObj : Obj { double last = -1; explicit TempoObj(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.isNum()) { c.action("tempo", m.f()); return; } c.outF(this, 0, c.bpm); } };

// ================================================================== notes and MIDI
struct Notein : Obj { int ch; explicit Notein(const Args& a) { ch = (int) argI(a, 0, 0); setIO(1, ch ? 2 : 3); wantsMidi = true; }
    void midi(Ctx& c, const unsigned char* b, int) override { int st = b[0] & 0xF0, chan = (b[0] & 15) + 1; if (st != 0x90 && st != 0x80) return; if (ch && chan != ch) return;
        int v = st == 0x80 ? 0 : b[2]; if (!ch) c.outI(this, 2, chan); c.outI(this, 1, v); c.outI(this, 0, b[1]); } };
struct Ctlin : Obj { int cc, ch; explicit Ctlin(const Args& a) { cc = (int) argI(a, 0, -1); ch = (int) argI(a, 1, 0); setIO(1, 3); wantsMidi = true; }
    void midi(Ctx& c, const unsigned char* b, int) override { if ((b[0] & 0xF0) != 0xB0) return; int chan = (b[0] & 15) + 1; if (ch && chan != ch) return; if (cc >= 0 && b[1] != cc) return;
        c.outI(this, 2, chan); c.outI(this, 1, b[1]); c.outI(this, 0, b[2]); } };
struct Bendin : Obj { explicit Bendin(const Args&) { setIO(1, 2); wantsMidi = true; }
    void midi(Ctx& c, const unsigned char* b, int) override { if ((b[0] & 0xF0) != 0xE0) return; c.outI(this, 1, (b[0] & 15) + 1); c.outI(this, 0, b[2]); } };
struct Pgmin : Obj { explicit Pgmin(const Args&) { setIO(1, 2); wantsMidi = true; }
    void midi(Ctx& c, const unsigned char* b, int) override { if ((b[0] & 0xF0) != 0xC0) return; c.outI(this, 1, (b[0] & 15) + 1); c.outI(this, 0, b[1] + 1); } };
struct Touchin : Obj { explicit Touchin(const Args&) { setIO(1, 2); wantsMidi = true; }
    void midi(Ctx& c, const unsigned char* b, int) override { if ((b[0] & 0xF0) != 0xD0) return; c.outI(this, 1, (b[0] & 15) + 1); c.outI(this, 0, b[1]); } };
struct Midiin : Obj { explicit Midiin(const Args&) { setIO(1, 1); wantsMidi = true; }
    void midi(Ctx& c, const unsigned char* b, int) override { int st = b[0] & 0xF0; int n = (st == 0xC0 || st == 0xD0) ? 2 : 3; for (int i = 0; i < n; ++i) c.outI(this, 0, b[i]); } };
// notes out: the app's instrument (the KEYBOARD's sound, or the SYNTH) plays them
struct Noteout : Obj { int ch = 1, vel = 0; explicit Noteout(const Args& a) { ch = (int) argI(a, 0, 1); setIO(3, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { vel = (int) m.f(); return; } if (in == 2) { ch = (int) m.f(); return; }
        int note = (int) m.f(0); if (m.isList() && m.n > 1) vel = (int) m.f(1); c.action("note", juce::jlimit(0, 127, note), juce::jlimit(0, 127, vel), ch); } };
struct Ctlout : Obj { int cc = 1, ch = 1; explicit Ctlout(const Args& a) { cc = (int) argI(a, 0, 1); ch = (int) argI(a, 1, 1); setIO(3, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { cc = (int) m.f(); return; } if (in == 2) { ch = (int) m.f(); return; } c.action("cc", cc, juce::jlimit(0, 127, (int) m.f()), ch); } };
struct Bendout : Obj { explicit Bendout(const Args&) { setIO(2, 0); } void msg(Ctx& c, int in, const Msg& m) override { if (in == 0) c.action("bend", (m.f() - 64) / 64.0); } };
struct Midiout : Obj { std::vector<int> b; explicit Midiout(const Args&) { setIO(1, 0); }
    void msg(Ctx& c, int, const Msg& m) override { int v = (int) m.f(); if (v >= 128) b.clear(); b.push_back(v); if (b.size() == 3 && (b[0] & 0xF0) == 0x90) { c.action("note", b[1], b[2], (b[0] & 15) + 1); b.clear(); }
        else if (b.size() == 3 && (b[0] & 0xF0) == 0x80) { c.action("note", b[1], 0, (b[0] & 15) + 1); b.clear(); } else if (b.size() >= 3) b.clear(); } };
struct Makenote : Obj {
    int vel; double dur; struct Held { int note; bool used = false; }; std::vector<Held> h;
    explicit Makenote(const Args& a) { setIO(3, 2); vel = (int) argI(a, 0, 100); dur = argF(a, 1, 250); h.resize(64); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { vel = (int) m.f(); return; } if (in == 2) { dur = m.f(); return; }
        if (m.sel == sym("stop") || m.sel == S::clear) { for (int t = 0; t < 64; ++t) if (h[(size_t) t].used) { c.cancel(this, t); tick(c, t); } return; }
        int note = (int) m.f(0); if (m.isList() && m.n > 1) vel = (int) m.f(1); if (m.isList() && m.n > 2) dur = m.f(2);
        for (int t = 0; t < 64; ++t) if (!h[(size_t) t].used) { h[(size_t) t] = { note, true }; c.delay(this, dur, t); break; }
        c.outI(this, 1, vel); c.outI(this, 0, note);
    }
    void tick(Ctx& c, int t) override { if (t < 0 || t >= 64 || !h[(size_t) t].used) return; h[(size_t) t].used = false; c.outI(this, 1, 0); c.outI(this, 0, h[(size_t) t].note); }
};
struct Stripnote : Obj { int vel = 0; explicit Stripnote(const Args&) { setIO(2, 2); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { vel = (int) m.f(); return; } if (m.isList() && m.n > 1) vel = (int) m.f(1); if (vel == 0) return; c.outI(this, 1, vel); c.outI(this, 0, m.f(0)); } };
struct Flush : Obj { int vel = 0; std::set<int> held; explicit Flush(const Args&) { setIO(2, 2); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { vel = (int) m.f(); return; }
        if (m.isBang()) { for (int n : held) { c.outI(this, 1, 0); c.outI(this, 0, n); } held.clear(); return; }
        if (m.sel == S::clear) { held.clear(); return; }
        int note = (int) m.f(0); if (m.isList() && m.n > 1) vel = (int) m.f(1); if (vel > 0) held.insert(note); else held.erase(note);
        c.outI(this, 1, vel); c.outI(this, 0, note); } };
struct Sustain : Obj { int vel = 0; bool on = false; std::set<int> held; explicit Sustain(const Args&) { setIO(3, 2); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { vel = (int) m.f(); return; }
        if (in == 2) { on = m.f() != 0; if (!on) { for (int n : held) { c.outI(this, 1, 0); c.outI(this, 0, n); } held.clear(); } return; }
        int note = (int) m.f(0); if (m.isList() && m.n > 1) vel = (int) m.f(1);
        if (vel == 0 && on) { held.insert(note); return; } held.erase(note); c.outI(this, 1, vel); c.outI(this, 0, note); } };
struct Poly : Obj {   // gives every note a voice number (for [poly~]-like patches, or routing notes)
    int n; std::vector<int> voice; std::vector<long long> age; long long clock = 0; int vel = 0; bool steal;
    explicit Poly(const Args& a) { n = clampN(argI(a, 0, 4), 1, 64); steal = argI(a, 1, 0) != 0; setIO(2, 3); voice.assign((size_t) n, -1); age.assign((size_t) n, 0); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { vel = (int) m.f(); return; }
        int note = (int) m.f(0); if (m.isList() && m.n > 1) vel = (int) m.f(1);
        if (vel > 0) { int k = -1; for (int i = 0; i < n; ++i) if (voice[(size_t) i] < 0) { k = i; break; }
            if (k < 0) { if (!steal) return; k = 0; for (int i = 1; i < n; ++i) if (age[(size_t) i] < age[(size_t) k]) k = i; c.outI(this, 2, 0); c.outI(this, 1, voice[(size_t) k]); c.outI(this, 0, k + 1); }
            voice[(size_t) k] = note; age[(size_t) k] = ++clock; c.outI(this, 2, vel); c.outI(this, 1, note); c.outI(this, 0, k + 1); }
        else for (int i = 0; i < n; ++i) if (voice[(size_t) i] == note) { voice[(size_t) i] = -1; c.outI(this, 2, 0); c.outI(this, 1, note); c.outI(this, 0, i + 1); return; }
    }
};
struct Midiparse : Obj { std::vector<int> b; explicit Midiparse(const Args&) { setIO(1, 8); }
    void msg(Ctx& c, int, const Msg& m) override { int v = (int) m.f(); if (v >= 128) b.clear(); b.push_back(v); if (b.empty() || b[0] < 128) { b.clear(); return; }
        int st = b[0] & 0xF0, ch = (b[0] & 15) + 1; size_t need = (st == 0xC0 || st == 0xD0) ? 2 : 3; if (b.size() < need) return;
        c.outI(this, 6, ch);
        Atom two[2] = { Atom::I(b.size() > 1 ? b[1] : 0), Atom::I(b.size() > 2 ? b[2] : 0) };
        switch (st) { case 0x90: case 0x80: if (st == 0x80) two[1] = Atom::I(0); c.out(this, 0, Msg::fromAtoms(two, 2)); break; case 0xA0: c.out(this, 1, Msg::fromAtoms(two, 2)); break;
            case 0xB0: c.out(this, 2, Msg::fromAtoms(two, 2)); break; case 0xC0: c.outI(this, 3, b[1]); break; case 0xD0: c.outI(this, 4, b[1]); break; case 0xE0: c.outI(this, 5, b[2]); break; }
        b.clear(); } };
struct Midiformat : Obj { int ch; explicit Midiformat(const Args& a) { ch = (int) argI(a, 0, 1); setIO(7, 1); }
    void msg(Ctx& c, int in, const Msg& m) override { int st[6] = { 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0 }; if (in == 6) { ch = (int) m.f(); return; }
        int s = st[juce::jlimit(0, 5, in)] | ((ch - 1) & 15); c.outI(this, 0, s);
        if (in <= 2) { c.outI(this, 0, (int) m.f(0) & 127); c.outI(this, 0, (int) m.f(1) & 127); } else if (in == 5) { c.outI(this, 0, 0); c.outI(this, 0, (int) m.f() & 127); } else c.outI(this, 0, (int) m.f() & 127); } };
// [toscale major 0]: any note into the nearest note of a scale (ARCO)
struct ToScale : Obj {
    std::vector<int> steps; int root = 0;
    explicit ToScale(const Args& a) { setIO(3, 1); setScale(a.empty() ? sym("major") : argS(a, 0, sym("major"))); root = (int) argI(a, 1, 0); if (!a.empty() && a[0].num()) { steps.clear(); for (auto& x : a) steps.push_back((int) x.d() % 12); } }
    void setScale(int s) {
        static const std::map<std::string, std::vector<int>> sc = { { "major", { 0, 2, 4, 5, 7, 9, 11 } }, { "minor", { 0, 2, 3, 5, 7, 8, 10 } }, { "dorian", { 0, 2, 3, 5, 7, 9, 10 } },
            { "phrygian", { 0, 1, 3, 5, 7, 8, 10 } }, { "lydian", { 0, 2, 4, 6, 7, 9, 11 } }, { "mixolydian", { 0, 2, 4, 5, 7, 9, 10 } }, { "pentatonic", { 0, 2, 4, 7, 9 } },
            { "minorpentatonic", { 0, 3, 5, 7, 10 } }, { "blues", { 0, 3, 5, 6, 7, 10 } }, { "harmonic", { 0, 2, 3, 5, 7, 8, 11 } }, { "chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "wholetone", { 0, 2, 4, 6, 8, 10 } }, { "japanese", { 0, 1, 5, 7, 8 } }, { "arabic", { 0, 1, 4, 5, 7, 8, 11 } } };
        auto it = sc.find(symStr(s)); steps = it != sc.end() ? it->second : sc.at("major"); if (it == sc.end()) err = "scales: major minor dorian phrygian lydian mixolydian pentatonic minorpentatonic blues harmonic chromatic wholetone japanese arabic"; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) setScale(m.a[0].s); else if (m.n == 0 && !m.isBang()) setScale(m.sel); return; }
        if (in == 2) { root = (int) m.f(); return; }
        if (!m.isNum()) { if (m.sel != S::list) { setScale(m.sel); err = {}; } return; }
        double x = m.f(); int n = (int) std::round(x); int best = n, bd = 99;
        for (int o = -1; o <= 1; ++o) for (int s : steps) { int cand = ((n - root) / 12 + o) * 12 + root + s; if (n - root < 0) cand -= 12; int d = std::abs(cand - n); if (d < bd || (d == bd && cand < best)) { bd = d; best = cand; } }
        c.outI(this, 0, best);
    }
};
// [chord maj]: a note in, the notes of its chord out (a list)
struct Chord : Obj {
    std::vector<int> iv;
    explicit Chord(const Args& a) { setIO(2, 1); set(a.empty() ? sym("maj") : argS(a, 0, sym("maj"))); }
    void set(int s) { static const std::map<std::string, std::vector<int>> ch = { { "maj", { 0, 4, 7 } }, { "min", { 0, 3, 7 } }, { "7", { 0, 4, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } },
        { "min7", { 0, 3, 7, 10 } }, { "dim", { 0, 3, 6 } }, { "aug", { 0, 4, 8 } }, { "sus2", { 0, 2, 7 } }, { "sus4", { 0, 5, 7 } }, { "add9", { 0, 4, 7, 14 } }, { "min9", { 0, 3, 7, 10, 14 } },
        { "fifth", { 0, 7 } }, { "octave", { 0, 12 } }, { "6", { 0, 4, 7, 9 } } };
        auto it = ch.find(symStr(s)); iv = it != ch.end() ? it->second : ch.at("maj"); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1 || (m.n == 0 && !m.isBang() && !m.isNum())) { set(m.sel == S::symbol && m.n ? m.a[0].s : m.sel); return; }
        int n = (int) m.f(); Atom o[16]; int k = 0; for (int x : iv) if (k < 16) o[k++] = Atom::I(n + x); c.out(this, 0, Msg::fromAtoms(o, k)); } };

// ================================================================== remember
struct Value : Obj {
    int name; bool local;
    Value(const Args& a, bool l) : local(l) { setIO(1, 1); name = argS(a, 0, sym("value")); if (local) name = sym("pv." + symStr(name)); }
    void msg(Ctx& c, int, const Msg& m) override { auto* v = c.value(name); if (m.isBang()) { if (v->n || v->sel) c.out(this, 0, *v); return; } *v = m; }
};
struct Send : Obj { int name; explicit Send(const Args& a) { name = argS(a, 0, 0); setIO(name ? 1 : 2, 0); if (!name) err = "send needs a name: [send something]"; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { if (m.sel == S::symbol && m.n) name = m.a[0].s; return; } if (m.sel == S::set && m.n) { name = m.a[0].s; return; } c.send(name, m); } };
struct Receive : Obj { int name; explicit Receive(const Args& a) { name = argS(a, 0, 0); setIO(1, 1); }
    int recvName() const override { return name; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 999) { c.out(this, 0, m); return; } if (m.sel == S::set && m.n) name = m.a[0].s; } };
struct Forward : Obj { int name = 0; explicit Forward(const Args& a) { name = argS(a, 0, 0); setIO(1, 0); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.sel == sym("send") && m.n) { name = m.a[0].s; return; } c.send(name, m); } };
struct Coll : Obj {
    std::map<std::string, std::vector<Atom>> data;   // keys: numbers as text, or words
    std::vector<std::string> order; int pos = 0;
    explicit Coll(const Args&) { setIO(1, 4); }
    static std::string keyOf(const Atom& a) { return a.num() ? std::to_string((long long) a.d()) : symStr(a.s); }
    static Atom atomOf(const std::string& k) { auto v = parseAtoms(k); return v.empty() ? Atom::Sy(k) : v[0]; }
    void sortOrder() { order.clear(); for (auto& [k, v] : data) order.push_back(k); std::stable_sort(order.begin(), order.end(), [](const std::string& a, const std::string& b) { bool fa, fb; auto na = parseAtoms(a), nb = parseAtoms(b);
        if (!na.empty() && !nb.empty() && na[0].num() && nb[0].num()) return na[0].d() < nb[0].d(); juce::ignoreUnused(fa, fb); return a < b; }); }
    void outAt(Ctx& c, const std::string& k) { auto it = data.find(k); if (it == data.end()) return; c.out(this, 1, Msg::num(atomOf(k))); c.out(this, 0, Msg::fromAtoms(it->second.data(), (int) it->second.size())); }
    void msg(Ctx& c, int, const Msg& m) override {
        auto W = [&](const char* w) { return m.sel == sym(w); };
        if (m.isList() && m.n >= 1) { data[keyOf(m.a[0])] = std::vector<Atom>(m.a + 1, m.a + m.n); sortOrder(); uiGen++; return; }
        if (m.isNum()) { outAt(c, keyOf(m.a[0])); return; }
        if (m.sel == S::symbol && m.n) { outAt(c, keyOf(m.a[0])); return; }
        if (W("store") && m.n >= 1) { data[keyOf(m.a[0])] = std::vector<Atom>(m.a + 1, m.a + m.n); sortOrder(); uiGen++; return; }
        if (W("remove") || W("delete")) { if (m.n) data.erase(keyOf(m.a[0])); sortOrder(); uiGen++; return; }
        if (m.sel == S::clear) { data.clear(); order.clear(); pos = 0; uiGen++; return; }
        if (W("length")) { c.outI(this, 0, (double) data.size()); return; }
        if (m.sel == S::dump) { for (auto& k : order) outAt(c, k); c.bang(this, 3); return; }
        if (m.sel == S::next || m.isBang()) { if (order.empty()) return; pos = (pos % (int) order.size() + (int) order.size()) % (int) order.size(); outAt(c, order[(size_t) pos]); pos = (pos + 1) % (int) order.size(); return; }
        if (m.sel == S::prev) { if (order.empty()) return; pos = (pos - 2 + 2 * (int) order.size()) % (int) order.size(); outAt(c, order[(size_t) pos]); pos = (pos + 1) % (int) order.size(); return; }
        if (W("goto")) { for (size_t i = 0; i < order.size(); ++i) if (m.n && order[i] == keyOf(m.a[0])) pos = (int) i; return; }
        if (W("nth")) { auto it = data.find(keyOf(m.a[0])); if (it != data.end() && m.n > 1) { int k = (int) m.f(1) - 1; if (k >= 0 && k < (int) it->second.size()) c.out(this, 0, Msg::num(it->second[(size_t) k])); } return; }
        if (W("insert") && m.n >= 1) { data[keyOf(m.a[0])] = std::vector<Atom>(m.a + 1, m.a + m.n); sortOrder(); return; }
        // a word key: "red" (nothing more) asks for it
        if (m.n == 0) { outAt(c, symStr(m.sel)); return; }
        data[symStr(m.sel)] = std::vector<Atom>(m.a, m.a + m.n); sortOrder(); uiGen++;
    }
    juce::var keep() const override { auto* o = new juce::DynamicObject(); for (auto& [k, v] : data) { std::string t; for (auto& x : v) { if (!t.empty()) t += " "; t += x.str(); } o->setProperty(juce::Identifier(juce::String(k.empty() ? "_" : k)), juce::String(t)); } return juce::var(o); }
    void restore(const juce::var& s) override { if (auto* o = s.getDynamicObject()) for (auto& p : o->getProperties()) data[p.name.toString().toStdString()] = parseAtoms(p.value.toString().toStdString()); sortOrder(); }
};
struct Table : Obj {
    int name; std::vector<float>* t = nullptr; int nextIdx = -1; Ctx* cc = nullptr; std::vector<float> own; int size;
    explicit Table(const Args& a) { name = argS(a, 0, 0); size = clampN(argI(a, name ? 1 : 0, 128), 1, 65536); setIO(2, 2); own.assign((size_t) size, 0.0f); }
    std::vector<float>& T() { return own; }
    void msg(Ctx& c, int in, const Msg& m) override {
        auto& v = T();
        if (in == 1) { nextIdx = (int) m.f(); juce::ignoreUnused(nextIdx); setNext = m.f(); haveNext = true; return; }
        if (m.sel == S::size) { v.assign((size_t) clampN((long long) m.f(), 1, 65536), 0.0f); uiGen++; return; }
        if (m.sel == S::clear) { std::fill(v.begin(), v.end(), 0.0f); uiGen++; return; }
        if (m.sel == S::set && m.n >= 1) { int st = (int) m.f(0); for (int i = 1; i < m.n; ++i) if (st + i - 1 >= 0 && st + i - 1 < (int) v.size()) v[(size_t) (st + i - 1)] = (float) m.f(i); uiGen++; return; }
        if (m.sel == sym("length")) { c.outI(this, 0, (double) v.size()); return; }
        if (m.sel == sym("sum")) { double s = 0; for (float x : v) s += x; c.outF(this, 0, s); return; }
        if (m.isList() && m.n >= 2) { int i = (int) m.f(0); if (i >= 0 && i < (int) v.size()) v[(size_t) i] = (float) m.f(1); uiGen++; return; }
        if (m.isBang() || m.sel == sym("quantile")) {  // a random index, weighted by the values
            double s = 0; for (float x : v) s += juce::jmax(0.0f, x); if (s <= 0) return; double r = c.rand() / 16777216.0 * s;
            for (size_t i = 0; i < v.size(); ++i) { r -= juce::jmax(0.0f, v[i]); if (r <= 0) { c.outI(this, 0, (double) i); return; } } return; }
        if (m.isNum()) { int i = (int) m.f(); if (haveNext) { haveNext = false; if (i >= 0 && i < (int) v.size()) v[(size_t) i] = (float) setNext; uiGen++; return; }
            if (i >= 0 && i < (int) v.size()) c.outF(this, 0, v[(size_t) i]); }
    }
    double setNext = 0; bool haveNext = false;
    int uiArray(float* out, int cap) const override { int n = juce::jmin(cap, (int) own.size()); for (int i = 0; i < n; ++i) out[i] = own[(size_t) i]; return n; }
    juce::var keep() const override { juce::Array<juce::var> a; for (float x : own) a.add(x); return a; }
    void restore(const juce::var& s) override { if (auto* a = s.getArray()) { own.clear(); for (auto& x : *a) own.push_back((float) (double) x); if (own.empty()) own.assign(128, 0.0f); } }
};

} // namespace

void registerControl() {
    // ---- start here
    def("message", "Start here", "A message: click it (or send it a bang) and it sends what is written. $1 is replaced by what comes in; commas send several; \"; name hello\" sends to [receive name].",
        "bang: send it | set: change it", "the message", "", "message box click text", mk<Message>());
    def("print", "Start here", "Writes whatever comes in to the console (the list at the bottom) - the best way to see what a cord carries.", "anything", "", "print", "show see console debug log", mk<Print>());
    def("loadbang", "Start here", "Sends a bang when the patch opens - to set things up.", "bang", "bang when the patch opens", "loadbang", "start open init setup", mk<Loadbang>());
    def("loadmess", "Start here", "Sends its words when the patch opens ([loadmess 120] sends 120).", "bang: again", "the words", "loadmess 120", "start open init value", mk<Loadmess>());
    def("closebang", "Start here", "(Kept for Max patches: does nothing here.)", "", "", "closebang", "", [](const Args&) { return std::make_shared<Nothing>(0, 1); });
    def("bangbang", "Logic & choosing", "One bang in, several out - right to left ([b 3]: three outlets).", "anything", "bang|bang", "b 3", "order several bang", mk<Bang>());
    alias("b", "bangbang");
    def("trigger", "Logic & choosing", "Sends what comes in to several outlets, right to left, changed into what you say: b bang, i int, f float, l list, s symbol, or a fixed number / word. The way to put things in order.",
        "anything", "per argument", "t b i", "order sequence first then convert", mk<Trigger>());
    alias("t", "trigger");
    def("inlet", "Patch structure", "Inside a subpatch [p ...]: a way in (left to right, as they are placed).", "", "what comes in", "inlet", "subpatch input", mk<InletObj>());
    def("outlet", "Patch structure", "Inside a subpatch [p ...]: a way out.", "what goes out", "", "outlet", "subpatch output", mk<OutletObj>());
    def("inlet~", "Patch structure", "Inside a subpatch: a way in for sound.", "", "sound", "inlet~", "subpatch input signal", [](const Args&) { auto o = std::make_shared<Nothing>(0, 1); o->sigOutlet(0); return o; });
    def("outlet~", "Patch structure", "Inside a subpatch: a way out for sound.", "sound", "", "outlet~", "subpatch output signal", [](const Args&) { auto o = std::make_shared<Nothing>(1, 0); o->sigInlet(0); return o; });
    def("p", "Patch structure", "A subpatch: a patch inside a box, to keep things tidy. Double-click it to open; put [inlet] and [outlet] inside.", "its inlets", "its outlets", "p effects", "subpatch group folder tidy patcher", [](const Args&) { return std::make_shared<Nothing>(0, 0); });
    alias("patcher", "p");
    // ---- numbers & math
    struct O { const char* n; Op op; const char* d; const char* w; };
    static const O ops[] = {
        { "+", OAdd, "Adds: left + right. The left inlet says the answer, the right one only remembers. [+ 1.] works with decimals.", "add plus sum" },
        { "-", OSub, "Subtracts: left - right.", "minus subtract" },
        { "*", OMul, "Multiplies: left x right.", "times multiply" },
        { "/", ODiv, "Divides: left / right ([/ 2.] for decimals).", "divide" },
        { "%", OMod, "What is left after dividing (12 % 5 = 2) - to count round in circles.", "modulo remainder wrap circle" },
        { "!-", ORSub, "Subtracts the other way: right - left.", "minus reverse" },
        { "!/", ORDiv, "Divides the other way: right / left.", "divide reverse" },
        { "!%", ORMod, "Modulo the other way: right % left.", "modulo reverse" },
        { "pow", OPow, "Left to the power of right.", "power exponent" },
        { "min", OMin, "The smaller of the two.", "smallest minimum" }, { "max", OMax, "The bigger of the two.", "biggest maximum" },
        { "==", OEq, "1 if both are the same, else 0.", "equal same compare" }, { "!=", ONe, "1 if they differ, else 0.", "different not equal compare" },
        { "<", OLt, "1 if left is smaller than right.", "less smaller compare" }, { ">", OGt, "1 if left is bigger than right.", "greater bigger compare" },
        { "<=", OLe, "1 if left is smaller or the same.", "compare" }, { ">=", OGe, "1 if left is bigger or the same.", "compare" },
        { "&&", OAnd, "1 if both are not 0 (and).", "and logic both" }, { "||", OOr, "1 if either is not 0 (or).", "or logic either" },
        { "&", OBAnd, "Bits that are in both.", "bits bitwise and" }, { "|", OBOr, "Bits in either.", "bits bitwise or" },
        { "<<", OShl, "Shifts the bits left (x 2 each).", "bits shift" }, { ">>", OShr, "Shifts the bits right (/ 2 each).", "bits shift" },
        { "atan2", OAtan2, "The angle of a point (y left, x right), in radians.", "angle" } };
    for (auto& o : ops) { Op op = o.op; def(o.n, o.n[0] == '=' || o.n[0] == '!' || o.n[0] == '<' || o.n[0] == '>' || o.n[0] == '&' || o.n[0] == '|' ? "Logic & choosing" : "Numbers & math", o.d, "a number: the answer | a number: kept", "the answer",
        (std::string(o.n) + " 1").c_str(), o.w, [op](const Args& a) { return std::make_shared<BinOp>(a, op); }); }
    def("abs", "Numbers & math", "Makes a number positive.", "number", "number", "abs", "absolute positive", [](const Args& a) { return std::make_shared<Unary>([](double x) { return std::abs(x); }, !argIsFloat(a, 0)); });
    def("sqrt", "Numbers & math", "The square root.", "number", "number", "sqrt", "root", [](const Args&) { return std::make_shared<Unary>([](double x) { return x > 0 ? std::sqrt(x) : 0.0; }, false); });
    def("round", "Numbers & math", "Rounds to the nearest whole number - or to the nearest step ([round 0.5]).", "number", "number", "round", "round nearest step", [](const Args& a) { double s = argF(a, 0, 1); bool f = argIsFloat(a, 0); if (s == 0) s = 1; return std::make_shared<Unary>([s](double x) { return std::round(x / s) * s; }, !f); });
    def("trunc", "Numbers & math", "Cuts the decimals off.", "number", "whole number", "trunc", "int integer cut", [](const Args&) { return std::make_shared<Unary>([](double x) { return std::trunc(x); }, true); });
    def("int", "Numbers & math", "Remembers a whole number; a bang says it again. The right inlet only remembers.", "number or bang | number: kept", "number", "int 5", "store remember integer whole", [](const Args& a) { return std::make_shared<IntBox>(a, false); });
    alias("i", "int");
    def("float", "Numbers & math", "Remembers a decimal number; a bang says it again.", "number or bang | number: kept", "number", "float 0.5", "store remember decimal", [](const Args& a) { return std::make_shared<IntBox>(a, true); });
    alias("f", "float");
    def("scale", "Numbers & math", "Maps one range onto another: [scale 0 127 0. 1.] turns 0-127 into 0-1. A fifth number curves it.", "number | in low | in high | out low | out high | curve", "number", "scale 0 127 0. 1.", "map range convert", mk<Scale>());
    alias("zmap", "scale");
    def("clip", "Numbers & math", "Keeps a number between a low and a high.", "number | low | high", "number", "clip 0 127", "limit range keep between", mk<Clip>());
    def("mtof", "Notes & MIDI", "A MIDI note number into its frequency (69 -> 440 Hz).", "note", "Hz", "mtof", "note frequency pitch hz", [](const Args&) { return std::make_shared<Unary>([](double x) { return mtof(x); }, false); });
    def("ftom", "Notes & MIDI", "A frequency into a MIDI note number (with decimals).", "Hz", "note", "ftom", "frequency note pitch", [](const Args&) { return std::make_shared<Unary>([](double x) { return ftom(x); }, false); });
    def("dbtoa", "Numbers & math", "Decibels into a level (0 dB -> 1, -6 dB -> 0.5).", "dB", "level", "dbtoa", "decibel volume gain", [](const Args&) { return std::make_shared<Unary>([](double x) { return std::pow(10.0, x / 20.0); }, false); });
    def("atodb", "Numbers & math", "A level into decibels.", "level", "dB", "atodb", "decibel volume gain", [](const Args&) { return std::make_shared<Unary>([](double x) { return x > 0 ? 20.0 * std::log10(x) : -999.0; }, false); });
    def("expr", "Numbers & math", "Writes math as you would on paper: [expr $f1 * 2 + sin($f2)]. $i1, $f1 ... are the inlets. Functions: sin cos tan pow sqrt abs min max floor ceil round random(lo,hi) mtof ftom scale clip if(c,a,b).",
        "$1 (says the answer) | $2 ...", "the answer", "expr $f1 * 2 + 1", "math formula calculate equation", mk<ExprObj>());
    def("vexpr", "Lists & words", "Like [expr] but on every number of a list: [vexpr $f1 * 2].", "list | list", "list", "vexpr $f1 * 2", "list math", mk<VExpr>());
    def("if", "Logic & choosing", "If something is true, send one thing, else another: [if $i1 > 64 then 1 else 0]. \"out2\" sends from a second outlet.", "$1 ...", "the answer", "if $i1 > 64 then 1 else 0", "condition test true false choose", mk<IfObj>());
    def("random", "Numbers & math", "A random whole number from 0 to N-1 for each bang ([random 8]: 0-7). [random 1.]: decimals.", "bang | the range", "number", "random 8", "chance dice surprise", mk<Random>());
    def("drunk", "Numbers & math", "A random walk: each bang moves a little from where it was - wandering melodies.", "bang | range | step", "number", "drunk 128 3", "random walk wander", mk<Drunk>());
    def("urn", "Numbers & math", "Random numbers that never repeat until all are used (like drawing from a hat).", "bang | how many", "number | bang when empty", "urn 8", "random shuffle no repeat", mk<Urn>());
    def("decide", "Numbers & math", "Flips a coin: 0 or 1.", "bang", "0 or 1", "decide", "coin random chance", mk<Decide>());
    def("counter", "Numbers & math", "Counts on each bang: [counter 8] goes 0 1 2 ... 8 and round again. Messages: up, down, updown, set, reset, min, max.", "bang: count | direction | set next | maximum", "count | below the bottom | over the top | times round", "counter 0 7", "count step loop sequence", mk<Counter>());
    def("accum", "Numbers & math", "Keeps a running total: the middle inlet adds, the right one multiplies.", "number: set and say, bang: say | add | multiply", "total", "accum", "add total sum", mk<Accum>());
    def("change", "Numbers & math", "Lets a number through only when it differs from the one before.", "number", "number | 1 from 0 | 0", "change", "only new different filter repeats", mk<Change>());
    def("past", "Numbers & math", "Bangs when the numbers go above a level.", "number | the level", "bang", "past 100", "threshold above level", mk<Past>());
    def("peak", "Numbers & math", "Says a number when it is the highest so far.", "number | set", "highest | 1 new | 0 not", "peak", "highest maximum", [](const Args& a) { return std::make_shared<Peak>(a, false); });
    def("trough", "Numbers & math", "Says a number when it is the lowest so far.", "number | set", "lowest | 1 new | 0 not", "trough", "lowest minimum", [](const Args& a) { return std::make_shared<Peak>(a, true); });
    def("maximum", "Numbers & math", "The biggest number of a list (or of the two inlets).", "list or number | number", "biggest | where", "maximum", "biggest largest", [](const Args& a) { return std::make_shared<MinMax>(a, true); });
    def("minimum", "Numbers & math", "The smallest number of a list (or of the two inlets).", "list or number | number", "smallest | where", "minimum", "smallest", [](const Args& a) { return std::make_shared<MinMax>(a, false); });
    def("mean", "Numbers & math", "The average of everything that came in (or of a list).", "number or list", "average | how many", "mean", "average", mk<Mean>());
    def("histo", "Numbers & math", "Counts how often each number came.", "number", "number | its count", "histo 128", "count often", mk<Histo>());
    // ---- logic & choosing
    def("select", "Logic & choosing", "Bangs an outlet when its number (or word) comes: [sel 1 5 9]. Anything else goes out on the right.", "number or word | (one argument) what to look for", "bang per argument | everything else", "sel 0 4 8", "choose match if equal pick", mk<Select>());
    alias("sel", "select");
    def("route", "Logic & choosing", "Sends a message on by its first word: [route freq amp] - \"freq 440\" goes out of the first outlet as 440.", "anything", "per word | everything else", "route freq amp", "choose by name address dispatch", [](const Args& a) { return std::make_shared<Route>(a, false); });
    def("routepass", "Logic & choosing", "Like [route] but keeps the first word.", "anything", "per word | everything else", "routepass freq amp", "route keep", [](const Args& a) { return std::make_shared<Route>(a, true); });
    def("gate", "Logic & choosing", "A door: the left inlet opens it (0 closed, 1 outlet 1, 2 outlet 2 ...), the right inlet is what goes through.", "0 closed, N: which outlet | what goes through", "outlets", "gate 2", "door open close route switch", mk<Gate>());
    alias("ggate", "gate");
    def("switch", "Logic & choosing", "Chooses which inlet gets through to the outlet (left: 0 none, 1 the first ...).", "which | inlets", "what gets through", "switch 2", "choose input select", mk<Switch>());
    alias("gswitch", "switch");
    def("onebang", "Logic & choosing", "Lets one bang through, then waits until the right inlet opens it again.", "bang | open again", "bang | blocked", "onebang 1", "once only first", mk<Onebang>());
    def("togedge", "Logic & choosing", "Bangs left when a toggle goes on, right when it goes off.", "0 / 1", "on | off", "togedge", "edge toggle change on off", mk<Togedge>());
    def("split", "Logic & choosing", "Numbers inside a range go left, the rest right.", "number | low | high", "inside | outside", "split 0 63", "range inside choose", mk<Split>());
    def("speedlim", "Time & rhythm", "Lets at most one message through every so many ms (the last one waits).", "anything | ms", "anything", "speedlim 50", "slow limit rate throttle", mk<Speedlim>());
    def("qlim", "Time & rhythm", "Like [speedlim]: at most one message every so many ms.", "anything | ms", "anything", "qlim 50", "limit rate", mk<Qlim>());
    def("uzi", "Logic & choosing", "Bangs many times at once ([uzi 8]: eight bangs, numbered on the right).", "bang | how many", "bang each time | bang when done | the number", "uzi 8", "loop repeat many times for", mk<Uzi>());
    def("cycle", "Logic & choosing", "Each message goes to the next outlet, round and round.", "anything | reset", "outlets", "cycle 4", "round robin next distribute", mk<CycleObj>());
    def("spray", "Logic & choosing", "\"2 100\" sends 100 out of the 3rd outlet (the first number chooses).", "index value", "outlets", "spray 4", "distribute index", mk<Spray>());
    def("funnel", "Logic & choosing", "Many inlets into one outlet, each with its number in front (\"2 100\").", "inlets", "index value", "funnel 4", "merge index", mk<Funnel>());
    def("buddy", "Logic & choosing", "Waits until every inlet has had something, then sends them all together.", "inlets", "outlets", "buddy 2", "sync wait together", mk<Buddy>());
    def("swap", "Logic & choosing", "Swaps two numbers: left goes out right, right goes out left.", "number | number", "the right one | the left one", "swap", "exchange", mk<Swap>());
    def("bucket", "Logic & choosing", "Passes numbers along its outlets like a bucket brigade (each one moves one along).", "number", "outlets", "bucket 4", "shift delay line register", mk<Bucket>());
    // ---- lists
    def("pack", "Lists & words", "Puts numbers together into a list: [pack 0 0]. The left inlet sends it.", "the first (sends) | the others", "list", "pack 0 0", "list together combine join", [](const Args& a) { return std::make_shared<Pack>(a, false); });
    def("pak", "Lists & words", "Like [pack] but any inlet sends the list.", "inlets", "list", "pak 0 0", "list together", [](const Args& a) { return std::make_shared<Pack>(a, true); });
    def("unpack", "Lists & words", "Takes a list apart: each item out of its own outlet.", "list", "one per item", "unpack 0 0", "list apart split", mk<Unpack>());
    def("join", "Lists & words", "Joins whole lists (or words) together; the left inlet sends.", "inlets", "list", "join 2", "list together combine", mk<Join>());
    def("iter", "Lists & words", "Sends the items of a list one by one.", "list", "items", "iter", "list one by one each", mk<Iter>());
    def("prepend", "Lists & words", "Puts words in front of what comes in: [prepend set].", "anything | new words", "anything", "prepend set", "add before front", [](const Args& a) { return std::make_shared<Prepend>(a, false); });
    def("append", "Lists & words", "Puts words after what comes in.", "anything | new words", "anything", "append 100", "add after end", [](const Args& a) { return std::make_shared<Prepend>(a, true); });
    def("zl", "Lists & words", "Lists, all the ways: [zl len] [zl rev] [zl sort] [zl nth 2] [zl group 4] [zl stream 4] [zl slice 2] [zl join] [zl sum] [zl mean] [zl median] [zl reg] [zl lookup] [zl union] [zl sect] [zl filter] [zl scramble] [zl unique] [zl rot 1] [zl queue] [zl stack] [zl change] [zl lace] [zl delace].",
        "list | the other list / the number", "the result | the rest", "zl len", "list length reverse sort group nth", mk<Zl>());
    def("sprintf", "Lists & words", "Writes text with numbers in it: [sprintf %d Hz] - %d whole number, %f decimal, %s word.", "values", "the text", "sprintf note %d", "format text string", mk<Sprintf>());
    def("fromsymbol", "Lists & words", "Turns a word like \"1 2 3\" into a list.", "symbol", "list", "fromsymbol", "text to list parse", mk<FromSymbol>());
    def("tosymbol", "Lists & words", "Turns anything into one word.", "anything", "symbol", "tosymbol", "list to text string", mk<ToSymbol>());
    def("atoi", "Lists & words", "A word into its letter codes.", "symbol", "list", "atoi", "letters ascii", mk<Atoi>());
    def("itoa", "Lists & words", "Letter codes into a word.", "list", "symbol", "itoa", "letters ascii", mk<Itoa>());
    def("regexp", "Lists & words", "Finds a pattern in text ([regexp (\\\\d+)]); @substitute replaces it.", "text | pattern", "the match | its parts | | no match", "regexp ([a-z]+)", "find pattern text search replace", mk<Regexp>());
    def("substitute", "Lists & words", "Replaces one item with another in a list.", "list | what | with", "list | unchanged", "substitute 0 1", "replace", mk<Substitute>());
    def("thresh", "Lists & words", "Collects what arrives close together (within ms) into one list.", "numbers | ms", "list", "thresh 20", "collect chord together", mk<Thresh>());
    // ---- time
    def("metro", "Time & rhythm", "A metronome: a bang every so many ms while it is on ([metro 500], or in the app's tempo: [metro 8n]). 1 / 0 or a toggle turns it on and off.",
        "1 on, 0 off, bang start | the time", "bang", "metro 4n", "clock tick pulse repeat timer beat", mk<Metro>());
    def("delay", "Time & rhythm", "Waits, then bangs ([delay 500], [delay 8n]). A new bang starts the wait again; stop cancels.", "bang: start, stop | the time", "bang", "delay 500", "wait later after", mk<Delay>());
    alias("del", "delay");
    def("pipe", "Time & rhythm", "Delays numbers or lists - each one comes out after the time ([pipe 0 500]).", "number(s) | time", "number(s)", "pipe 500", "delay later numbers echo", mk<Pipe>());
    def("deferlow", "Time & rhythm", "Sends what comes in a moment later - breaks loops and lets the screen catch up.", "anything", "anything", "deferlow", "later loop defer", mk<Deferlow>());
    alias("defer", "deferlow");
    def("line", "Time & rhythm", "Glides to a number in a time, saying every step: \"100 2000\" goes to 100 in 2 s.", "target, or \"target time\" | time | step ms", "the numbers | bang at the end", "line 0. 20", "ramp glide fade slide", mk<LineObj>());
    def("timer", "Time & rhythm", "Measures time: left starts, right says how many ms since.", "start | stop: say", "ms", "timer", "measure stopwatch elapsed", mk<Timer>());
    def("clocker", "Time & rhythm", "While on, says every so many ms how long it has been on.", "1 / 0 | interval", "ms", "clocker 100", "elapsed time", mk<Clocker>());
    def("cpuclock", "Time & rhythm", "The computer's clock in ms (to measure precisely).", "bang", "ms", "cpuclock", "time now", mk<Cpuclock>());
    def("date", "Time & rhythm", "The time and the date: \"time\" -> hours minutes seconds, \"date\" -> month day year.", "time / date", "date | time | ms", "date", "clock now today hour", mk<DateObj>());
    def("beat", "ARCO", "Bangs in time with ARCO: on every quarter, eighth, sixteenth of the app's clock ([beat 16n]) - so it stays with the loops, the drums and the studio.",
        "1 on / 0 off | the division", "bang | the count", "beat 16n", "tempo sync clock rhythm grid quantize in time", [](const Args& a) { return std::make_shared<Beat>(a); });
    def("transport", "ARCO", "The app's clock: is it playing, the tempo, the bar, the beat. Send it play, stop, or \"tempo 120\".", "bang: say, play, stop, tempo N", "playing | tempo | bar | beat | beats", "transport", "tempo bpm play stop bar clock", mk<Transport>());
    def("tempo", "ARCO", "The app's tempo: a bang says it, a number sets it.", "bang | a tempo", "bpm", "tempo", "bpm speed", mk<TempoObj>());
    // ---- notes
    def("notein", "Notes & MIDI", "The notes you play (a MIDI keyboard, the computer keys, the keys on screen): note, velocity, channel.", "", "note | velocity | channel", "notein", "keyboard midi play keys input", mk<Notein>());
    def("ctlin", "Notes & MIDI", "MIDI knobs and faders (controllers): value, number, channel. [ctlin 1] listens to the mod wheel only.", "", "value | controller | channel", "ctlin", "knob fader cc controller", mk<Ctlin>());
    def("bendin", "Notes & MIDI", "The pitch-bend wheel (0-127, 64 is the middle).", "", "bend | channel", "bendin", "pitch wheel bend", mk<Bendin>());
    def("pgmin", "Notes & MIDI", "Program changes.", "", "program | channel", "pgmin", "program preset", mk<Pgmin>());
    def("touchin", "Notes & MIDI", "Aftertouch (pressing harder on the keys).", "", "pressure | channel", "touchin", "aftertouch pressure", mk<Touchin>());
    def("midiin", "Notes & MIDI", "Every MIDI byte that comes in.", "", "bytes", "midiin", "raw bytes", mk<Midiin>());
    def("noteout", "Notes & MIDI", "Plays notes on ARCO's instrument (the KEYBOARD's sound or the SYNTH): note and velocity (0 stops it).", "note | velocity | channel", "", "noteout", "play sound instrument synth keyboard output", mk<Noteout>());
    def("ctlout", "Notes & MIDI", "Sends a controller (the mod wheel is 1, the sustain pedal 64).", "value | number | channel", "", "ctlout 1", "cc controller", mk<Ctlout>());
    def("bendout", "Notes & MIDI", "Bends the pitch of ARCO's synth (0-127, 64 is the middle).", "bend", "", "bendout", "pitch bend", mk<Bendout>());
    def("midiout", "Notes & MIDI", "MIDI bytes out (notes play on ARCO's instrument).", "bytes", "", "midiout", "raw bytes", mk<Midiout>());
    def("makenote", "Notes & MIDI", "Makes a whole note from a number: note on now, note off after a time ([makenote 100 250]).", "note | velocity | duration ms", "note | velocity", "makenote 100 250", "note duration length play", mk<Makenote>());
    def("stripnote", "Notes & MIDI", "Only lets note-ons through (velocity above 0).", "note | velocity", "note | velocity", "stripnote", "filter note on", mk<Stripnote>());
    def("flush", "Notes & MIDI", "Remembers the notes held; a bang stops them all.", "note | velocity", "note | velocity", "flush", "panic stop all notes", mk<Flush>());
    def("sustain", "Notes & MIDI", "A sustain pedal for notes: while the right inlet is 1, the notes hold.", "note | velocity | pedal 1 / 0", "note | velocity", "sustain", "pedal hold", mk<Sustain>());
    def("poly", "Notes & MIDI", "Gives each note a voice number (1..N) - to play chords on several voices.", "note | velocity", "voice | note | velocity", "poly 4", "voices polyphony chord allocate", mk<Poly>());
    def("midiparse", "Notes & MIDI", "MIDI bytes into notes, controllers, bend ...", "bytes", "note vel | poly pressure | cc | program | aftertouch | bend | channel | ", "midiparse", "decode parse", mk<Midiparse>());
    def("midiformat", "Notes & MIDI", "Notes, controllers ... into MIDI bytes.", "note vel | pressure | cc | program | aftertouch | bend | channel", "bytes", "midiformat", "encode", mk<Midiformat>());
    def("toscale", "ARCO", "Moves any note to the nearest note of a scale ([toscale minor 2]: D minor). Scales: major minor dorian phrygian lydian mixolydian pentatonic minorpentatonic blues harmonic chromatic wholetone japanese arabic.",
        "note | scale | root (0 = C)", "note", "toscale pentatonic", "scale key quantize harmony in tune", mk<ToScale>());
    def("chord", "ARCO", "A note in, the notes of a chord out (a list): maj min 7 maj7 min7 dim aug sus2 sus4 add9 min9 6 fifth octave.", "note | chord type", "list of notes", "chord min7", "harmony chord triad", mk<Chord>());
    // ---- remember
    def("value", "Remember", "Shares a number between all the [value name] with the same name - a bang says it.", "number: keep, bang: say", "the value", "value speed", "shared variable global store", [](const Args& a) { return std::make_shared<Value>(a, false); });
    alias("v", "value");
    def("pv", "Remember", "Like [value], private to the patch.", "number: keep, bang: say", "the value", "pv speed", "private variable", [](const Args& a) { return std::make_shared<Value>(a, true); });
    def("coll", "Remember", "A list of things by number or name: \"1 60 64 67\" keeps 60 64 67 at 1; then 1 says it. next / prev / dump / clear / remove. Saved with the patch.",
        "number or word: say it, list: keep", "the data | its number | | bang after dump", "coll", "store memory list collection database sequence", mk<Coll>());
    def("table", "Remember", "Numbers in a row (128 of them): an index says its number, \"index value\" changes one. A bang picks an index at random, weighted. Saved with the patch.",
        "index, or index value | value for the next index", "value", "table", "array graph numbers store", mk<Table>());
    def("send", "Send & receive", "Sends to every [receive name] with the same name - without a cord.", "anything", "", "send notes", "wireless send cordless global", mk<Send>());
    alias("s", "send");
    def("receive", "Send & receive", "Receives what every [send name] with the same name sends.", "set name", "anything", "receive notes", "wireless receive cordless", mk<Receive>());
    alias("r", "receive");
    def("forward", "Send & receive", "Like [send], but \"send name\" changes where it goes.", "anything, send name", "", "forward notes", "send dynamic", mk<Forward>());
}

}} // namespace arco::px
