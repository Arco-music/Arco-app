// ARCO 2.4: PATCHER - the core: symbols, messages, the patch file, compiling a patch, running it, expressions, search.
#include "Patcher.h"
#include <deque>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace arco { namespace px {

// ================================================================== symbols
namespace {
struct SymTab {
    juce::SpinLock lock;
    std::deque<std::string> names;
    std::unordered_map<std::string, int> ids;
    SymTab() { names.push_back(""); ids[""] = 0; }
};
SymTab& symtab() { static SymTab t; return t; }
}
int sym(const std::string& s) {
    auto& t = symtab(); const juce::SpinLock::ScopedLockType sl(t.lock);
    auto it = t.ids.find(s); if (it != t.ids.end()) return it->second;
    int id = (int) t.names.size(); t.names.push_back(s); t.ids[s] = id; return id;
}
const std::string& symStr(int s) {
    auto& t = symtab(); const juce::SpinLock::ScopedLockType sl(t.lock);
    return s >= 0 && s < (int) t.names.size() ? t.names[(size_t) s] : t.names[0];
}
namespace S {
int bang = sym("bang"), intS = sym("int"), floatS = sym("float"), list = sym("list"), symbol = sym("symbol"), set = sym("set"), clear = sym("clear"),
    reset = sym("reset"), stop = sym("stop"), start = sym("start"), next = sym("next"), prev = sym("prev"), size = sym("size"), dump = sym("dump"),
    read = sym("read"), write = sym("write"), clearS = sym("clear"), empty = sym("");
}

// ================================================================== atoms and messages
std::string Atom::str() const {
    if (t == 's') return symStr(s);
    if (t == 'i') return std::to_string((long long) v);
    if (std::isnan(v)) return "nan";
    char b[48]; std::snprintf(b, sizeof b, "%.6g", v);
    std::string r = b;
    if (r.find('.') == std::string::npos && r.find('e') == std::string::npos && r.find("inf") == std::string::npos) r += ".";
    return r;
}
Msg Msg::fromAtoms(const Atom* at, int n) {
    Msg m;
    if (n <= 0) { m.sel = S::bang; return m; }
    if (at[0].t == 's') {
        int w = at[0].s;
        if (w == S::list || w == S::intS || w == S::floatS) { m.sel = w; for (int k = 1; k < n && m.n < MAXA; ++k) m.a[m.n++] = at[k];
            if (w != S::list && m.n >= 1) { m.a[0] = w == S::intS ? Atom::I(m.a[0].d()) : Atom::F(m.a[0].d()); m.n = 1; }
            else if (w == S::list && m.n == 1 && m.a[0].num()) return num(m.a[0]);
            else if (m.n == 0) m.sel = S::bang;
            return m; }
        if (w == S::bang && n == 1) { m.sel = S::bang; return m; }
        m.sel = w; for (int k = 1; k < n && m.n < MAXA; ++k) m.a[m.n++] = at[k]; return m;
    }
    if (n == 1) return num(at[0]);
    m.sel = S::list; for (int k = 0; k < n && k < MAXA; ++k) m.a[m.n++] = at[k];
    return m;
}
int Msg::atoms(Atom* out, int cap) const {
    int k = 0;
    if (sel == S::bang) return 0;
    if (sel != S::intS && sel != S::floatS && sel != S::list && sel != S::symbol) { if (cap > 0) out[k++] = Atom::Sy(sel); }
    for (int j = 0; j < n && k < cap; ++j) out[k++] = a[j];
    return k;
}
std::string Msg::text() const {
    if (sel == S::bang) return "bang";
    std::string r;
    if (sel != S::intS && sel != S::floatS && sel != S::list && sel != S::symbol) r = symStr(sel);
    for (int j = 0; j < n; ++j) { if (!r.empty()) r += " "; r += a[j].str(); }
    return r;
}
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out; std::string cur; bool q = false, any = false;
    auto flush = [&] { if (any) out.push_back(cur); cur.clear(); any = false; };
    for (size_t i = 0; i < text.size(); ++i) {
        char ch = text[i];
        if (ch == '\\' && i + 1 < text.size()) { cur += text[++i]; any = true; continue; }
        if (ch == '"') { q = !q; any = true; continue; }
        if (!q && (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')) { flush(); continue; }
        if (!q && (ch == ',' || ch == ';')) { flush(); out.push_back(std::string(1, ch)); continue; }
        cur += ch; any = true;
    }
    flush();
    return out;
}
static bool isNumber(const std::string& s, bool& isFloat) {
    if (s.empty()) return false;
    size_t i = 0; if (s[0] == '-' || s[0] == '+') i = 1;
    if (i >= s.size()) return false;
    bool dig = false, dot = false, ex = false; isFloat = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') dig = true;
        else if (c == '.' && !dot && !ex) { dot = true; isFloat = true; }
        else if ((c == 'e' || c == 'E') && dig && !ex) { ex = true; isFloat = true; if (i + 1 < s.size() && (s[i + 1] == '-' || s[i + 1] == '+')) ++i; dig = false; }
        else return false;
    }
    return dig;
}
std::vector<Atom> parseAtoms(const std::string& text) {
    std::vector<Atom> r;
    for (auto& t : tokenize(text)) {
        bool fl; if (isNumber(t, fl)) r.push_back(fl ? Atom::F(std::atof(t.c_str())) : Atom::I((double) std::atoll(t.c_str())));
        else r.push_back(Atom::Sy(t));
    }
    return r;
}
std::string clsOf(const juce::String& text) { auto t = tokenize(text.toStdString()); return t.empty() ? std::string() : t[0]; }

// the default: a number into a signal inlet sets what it plays when nothing is plugged in
void Obj::msg(Ctx& c, int in, const Msg& m) {
    if (isSigIn(in) && m.isNum()) { sv[(size_t) in] = (float) m.f(); return; }
    if (isSigIn(in) && m.isList() && m.n >= 1) { sv[(size_t) in] = (float) m.f(); return; }
    c.error(this, "this inlet doesn't take \"" + m.text().substr(0, 40) + "\"");
}

// ================================================================== the patch file
static juce::var docToVar(const PatchDoc& d) {
    auto* o = new juce::DynamicObject();
    o->setProperty("name", d.name); o->setProperty("notes", d.notes); o->setProperty("nextId", d.nextId); o->setProperty("app", "ARCO patcher 1");
    juce::Array<juce::var> bx;
    for (auto& b : d.boxes) {
        auto* x = new juce::DynamicObject();
        x->setProperty("id", b.id); x->setProperty("kind", b.kind); x->setProperty("text", b.text);
        x->setProperty("x", b.x); x->setProperty("y", b.y); if (b.w > 0) x->setProperty("w", b.w); if (b.h > 0) x->setProperty("h", b.h);
        if (!b.state.isVoid()) x->setProperty("state", b.state);
        if (b.colour.isNotEmpty()) x->setProperty("colour", b.colour);
        if (b.sub) x->setProperty("sub", docToVar(*b.sub));
        bx.add(juce::var(x));
    }
    o->setProperty("boxes", bx);
    juce::Array<juce::var> ln;
    for (auto& l : d.lines) { juce::Array<juce::var> a; a.add(l.from); a.add(l.outlet); a.add(l.to); a.add(l.inlet); ln.add(a); }
    o->setProperty("lines", ln);
    return juce::var(o);
}
juce::var PatchDoc::toVar() const { return docToVar(*this); }
PatchDoc PatchDoc::fromVar(const juce::var& v) {
    PatchDoc d;
    if (!v.isObject()) return d;
    if (v["patcher"].isObject()) return fromMaxpat(v);
    d.name = v.getProperty("name", "Untitled").toString(); d.notes = v["notes"].toString(); d.nextId = (int) v.getProperty("nextId", 1);
    if (auto* a = v["boxes"].getArray()) for (auto& x : *a) {
        PBox b; b.id = (int) x["id"]; b.kind = x.getProperty("kind", "obj").toString(); b.text = x["text"].toString();
        b.x = (float) (double) x["x"]; b.y = (float) (double) x["y"]; b.w = (float) (double) x.getProperty("w", 0.0); b.h = (float) (double) x.getProperty("h", 0.0);
        b.state = x["state"]; b.colour = x["colour"].toString();
        if (x["sub"].isObject()) b.sub = std::make_shared<PatchDoc>(fromVar(x["sub"]));
        else if (b.kind == "sub") b.sub = std::make_shared<PatchDoc>();
        d.nextId = juce::jmax(d.nextId, b.id + 1);
        d.boxes.push_back(b);
    }
    if (auto* a = v["lines"].getArray()) for (auto& x : *a) if (auto* q = x.getArray()) if (q->size() >= 4)
        d.lines.push_back({ (int) (*q)[0], (int) (*q)[1], (int) (*q)[2], (int) (*q)[3] });
    return d;
}
// a Max patch: boxes ("newobj", "message", "comment", the interface objects), cords, subpatchers
PatchDoc PatchDoc::fromMaxpat(const juce::var& root) {
    PatchDoc d;
    auto pv = root["patcher"].isObject() ? root["patcher"] : root;
    std::map<juce::String, int> ids;
    static const std::map<juce::String, juce::String> ui = {
        { "toggle", "toggle" }, { "button", "button" }, { "number", "number" }, { "flonum", "flonum" }, { "slider", "slider" }, { "dial", "dial" },
        { "kslider", "kslider" }, { "multislider", "multislider" }, { "ezdac~", "ezdac~" }, { "ezadc~", "ezadc~" }, { "meter~", "meter~" },
        { "scope~", "scope~" }, { "umenu", "umenu" }, { "number~", "number~" }, { "gain~", "gain~" }, { "live.dial", "dial" }, { "live.toggle", "toggle" },
        { "live.button", "button" }, { "live.numbox", "flonum" }, { "live.slider", "slider" }, { "live.gain~", "gain~" }, { "function", "function" },
        { "panel", "panel" }, { "radiogroup", "radiogroup" }, { "textbutton", "button" }, { "spectroscope~", "spectroscope~" }, { "levelmeter~", "meter~" },
        { "inlet", "inlet" }, { "outlet", "outlet" }, { "waveform~", "waveform~" }, { "rslider", "slider" }, { "pictslider", "slider" }, { "incdec", "number" } };
    if (auto* a = pv["boxes"].getArray()) for (auto& w : *a) {
        auto bv = w["box"]; if (!bv.isObject()) continue;
        juce::String cls = bv["maxclass"].toString(), text = bv["text"].toString();
        PBox b; b.id = d.nextId++;
        if (auto* r = bv["patching_rect"].getArray(); r != nullptr && r->size() >= 4) { b.x = (float) (double) (*r)[0]; b.y = (float) (double) (*r)[1]; }
        if (cls == "newobj") {
            b.kind = "obj"; b.text = text;
            auto c = clsOf(text);
            if ((c == "p" || c == "patcher") && bv["patcher"].isObject()) { b.kind = "sub"; b.sub = std::make_shared<PatchDoc>(fromMaxpat(bv)); }
        } else if (cls == "message") { b.kind = "msg"; b.text = text; }
        else if (cls == "comment") { b.kind = "comment"; b.text = text; }
        else if (ui.count(cls)) { b.kind = "obj"; b.text = ui.at(cls); }
        else { b.kind = "obj"; b.text = cls + (text.isNotEmpty() ? " " + text : juce::String()); }
        ids[bv["id"].toString()] = b.id;
        d.boxes.push_back(b);
    }
    if (auto* a = pv["lines"].getArray()) for (auto& w : *a) {
        auto lv = w["patchline"]; auto* s = lv["source"].getArray(); auto* t = lv["destination"].getArray();
        if (s == nullptr || t == nullptr || s->size() < 2 || t->size() < 2) continue;
        auto fi = ids.find((*s)[0].toString()), ti = ids.find((*t)[0].toString());
        if (fi == ids.end() || ti == ids.end()) continue;
        d.lines.push_back({ fi->second, (int) (*s)[1], ti->second, (int) (*t)[1] });
    }
    d.name = root["name"].toString().isNotEmpty() ? root["name"].toString() : juce::String("From Max");
    return d;
}
juce::var PatchDoc::toMaxpat() const {
    auto* pat = new juce::DynamicObject();
    juce::Array<juce::var> bx, ln;
    for (auto& b : boxes) {
        auto io = boxIO(b);
        auto* box = new juce::DynamicObject();
        box->setProperty("id", "obj-" + juce::String(b.id));
        auto cls = juce::String(io.cls);
        if (b.kind == "msg") { box->setProperty("maxclass", "message"); box->setProperty("text", b.text); }
        else if (b.kind == "comment") { box->setProperty("maxclass", "comment"); box->setProperty("text", b.text); }
        else if (io.ui && (cls == "toggle" || cls == "button" || cls == "number" || cls == "flonum" || cls == "slider" || cls == "dial" || cls == "kslider" || cls == "multislider"
                           || cls == "ezdac~" || cls == "meter~" || cls == "scope~" || cls == "umenu" || cls == "gain~" || cls == "number~" || cls == "function"))
            box->setProperty("maxclass", cls);
        else { box->setProperty("maxclass", "newobj"); box->setProperty("text", b.text); if (b.sub) box->setProperty("patcher", b.sub->toMaxpat()["patcher"]); }
        box->setProperty("numinlets", io.ins); box->setProperty("numoutlets", io.outs);
        juce::Array<juce::var> r; r.add(b.x); r.add(b.y); r.add(b.w > 0 ? b.w : 80.0f); r.add(22.0f);
        box->setProperty("patching_rect", r);
        auto* w = new juce::DynamicObject(); w->setProperty("box", juce::var(box)); bx.add(juce::var(w));
    }
    for (auto& l : lines) {
        auto* pl = new juce::DynamicObject();
        juce::Array<juce::var> s, t; s.add("obj-" + juce::String(l.from)); s.add(l.outlet); t.add("obj-" + juce::String(l.to)); t.add(l.inlet);
        pl->setProperty("source", s); pl->setProperty("destination", t);
        auto* w = new juce::DynamicObject(); w->setProperty("patchline", juce::var(pl)); ln.add(juce::var(w));
    }
    pat->setProperty("boxes", bx); pat->setProperty("lines", ln);
    auto* fv = new juce::DynamicObject(); fv->setProperty("major", 8); fv->setProperty("minor", 0); pat->setProperty("fileversion", 1);
    auto* root = new juce::DynamicObject(); root->setProperty("patcher", juce::var(pat));
    return juce::var(root);
}

// ================================================================== the catalogue
std::vector<ObjDef>& catalogue() { static std::vector<ObjDef> c; return c; }
static std::unordered_map<std::string, int>& defIndex() { static std::unordered_map<std::string, int> m; return m; }
void registerControl(); void registerSignal(); void registerUI();
void registerAll() {
    static bool done = false; if (done) return; done = true;
    registerControl(); registerSignal(); registerUI();
    auto& c = catalogue(); auto& ix = defIndex();
    for (int i = 0; i < (int) c.size(); ++i) ix[c[(size_t) i].name] = i;
}
const ObjDef* findDef(const std::string& name) {
    registerAll();
    auto& ix = defIndex(); auto it = ix.find(name);
    if (it == ix.end()) return nullptr;
    auto* d = &catalogue()[(size_t) it->second];
    if (!d->aliasOf.empty()) { auto jt = ix.find(d->aliasOf); if (jt != ix.end()) { auto* t = &catalogue()[(size_t) jt->second]; return t; } }
    return d;
}
std::vector<std::string> categories() {
    return { "Start here", "Numbers & math", "Logic & choosing", "Lists & words", "Time & rhythm", "Notes & MIDI", "Things to touch", "Remember",
             "Send & receive", "Patch structure", "Sound: make", "Sound: math", "Sound: filters", "Sound: effects", "Sound: delay & time",
             "Sound: samples", "Sound: watch & measure", "Sound: in & out", "ARCO" };
}
static int lev(const std::string& a, const std::string& b) {
    std::vector<int> p(b.size() + 1), q(b.size() + 1), pp(b.size() + 1, 0);
    for (size_t j = 0; j <= b.size(); ++j) p[j] = (int) j;
    for (size_t i = 1; i <= a.size(); ++i) { q[0] = (int) i;
        for (size_t j = 1; j <= b.size(); ++j) { q[j] = std::min({ p[j] + 1, q[j - 1] + 1, p[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) q[j] = std::min(q[j], pp[j - 2] + 1); }
        pp = p; std::swap(p, q); }
    return p[b.size()];
}
std::vector<std::string> suggest(const std::string& word, int maxN) {
    registerAll();
    std::vector<std::pair<int, std::string>> s;
    for (auto& d : catalogue()) { int l = lev(word, d.name); if (l <= juce::jmax(2, (int) word.size() / 3)) s.push_back({ l, d.name }); }
    std::sort(s.begin(), s.end());
    std::vector<std::string> r; for (auto& x : s) { if ((int) r.size() >= maxN) break; r.push_back(x.second); }
    return r;
}
std::vector<const ObjDef*> searchDefs(const std::string& query, int maxN) {
    registerAll();
    auto lower = [](std::string s) { for (auto& c : s) c = (char) std::tolower((unsigned char) c); return s; };
    auto q = lower(query);
    std::vector<std::string> ws; { std::string cur; for (char c : q) { if (c == ' ') { if (!cur.empty()) ws.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) ws.push_back(cur); }
    static const std::set<std::string> skip = { "a", "an", "the", "to", "of", "i", "want", "make", "how", "do", "can", "with", "and", "my", "it", "is", "in", "on", "for", "something", "some" };
    std::vector<std::pair<int, const ObjDef*>> hits;
    for (auto& d : catalogue()) {
        int sc = 0; auto n = lower(d.name), w = lower(d.words), de = lower(d.desc), ca = lower(d.cat);
        if (n == q) sc += 1000;
        else if (!q.empty() && n.rfind(q, 0) == 0) sc += 400 - (int) (n.size() - q.size());
        for (auto& x : ws) {
            if (skip.count(x) && ws.size() > 1) continue;
            if (n == x) sc += 300; else if (n.rfind(x, 0) == 0) sc += 120; else if (n.find(x) != std::string::npos) sc += 60;
            if (w.find(x) != std::string::npos) sc += 70;
            if (de.find(x) != std::string::npos) sc += 35;
            if (ca.find(x) != std::string::npos) sc += 15;
        }
        if (!d.aliasOf.empty()) sc -= 5;
        if (sc > 0) hits.push_back({ sc, &d });
    }
    std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<const ObjDef*> r; for (auto& h : hits) { if ((int) r.size() >= maxN) break; r.push_back(h.second); }
    return r;
}

// inlets / outlets of a box, as the screen draws it (made by creating the object once; cached by text)
BoxIO boxIO(const PBox& b) {
    BoxIO io;
    if (b.kind == "comment") return io;
    if (b.kind == "msg") { io.ins = 1; io.outs = 1; io.cls = "message"; return io; }
    if (b.kind == "sub" || (b.sub != nullptr)) {
        io.cls = "p"; std::vector<std::pair<float, bool>> ins, outs;
        if (b.sub) for (auto& x : b.sub->boxes) { auto c = clsOf(x.text); if (c == "inlet" || c == "inlet~") ins.push_back({ x.x, c == "inlet~" }); else if (c == "outlet" || c == "outlet~") outs.push_back({ x.x, c == "outlet~" }); }
        std::sort(ins.begin(), ins.end()); std::sort(outs.begin(), outs.end());
        io.ins = (int) ins.size(); io.outs = (int) outs.size();
        for (int k = 0; k < io.ins && k < 64; ++k) if (ins[(size_t) k].second) io.sigIn |= 1ULL << k;
        for (int k = 0; k < io.outs && k < 64; ++k) if (outs[(size_t) k].second) io.sigOut |= 1ULL << k;
        return io;
    }
    static std::map<std::string, BoxIO> cache;
    auto key = b.text.toStdString();
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto toks = tokenize(key);
    if (toks.empty()) { io.ok = false; io.err = "empty"; return io; }
    io.cls = toks[0];
    auto* d = findDef(toks[0]);
    if (d == nullptr) { io.ok = false; auto sg = suggest(toks[0], 2); io.err = "no object called \"" + juce::String(toks[0]) + "\"" + (sg.empty() ? juce::String() : " - did you mean " + juce::String(sg[0]) + "?"); io.ins = 1; io.outs = 0; }
    else {
        std::vector<Atom> args; for (size_t i = 1; i < toks.size(); ++i) { auto v = parseAtoms(toks[i]); if (!v.empty()) args.push_back(v[0]); }
        auto o = d->make(args);
        if (o) { io.ins = o->nIn; io.outs = o->nOut; io.sigIn = o->sigIn; io.sigOut = o->sigOut; if (o->err.isNotEmpty()) { io.ok = false; io.err = o->err; } }
        io.ui = d->uiW > 0; io.uiW = d->uiW; io.uiH = d->uiH; io.cls = d->name;
    }
    if (cache.size() > 4000) cache.clear();
    cache[key] = io;
    return io;
}

// ================================================================== compiling a patch
namespace {
struct FBox { std::string key; const PBox* b; int top; bool relay = false; std::string relayKind; int relayIndex = 0; std::string relayOf; };
struct Flat {
    std::vector<FBox> boxes; std::map<std::string, int> byKey;
    std::vector<std::tuple<std::string, int, std::string, int>> lines;
    std::map<std::string, std::vector<std::string>> subIns, subOuts;   // a sub box -> its inner inlet / outlet boxes in order
};
void flatten(const PatchDoc& d, const std::string& prefix, int top, Flat& f, int depth) {
    if (depth > 16) return;
    for (auto& b : d.boxes) {
        if (b.kind == "comment") continue;
        std::string key = prefix + std::to_string(b.id);
        FBox fb; fb.key = key; fb.b = &b; fb.top = top < 0 ? b.id : top;
        if (b.sub) {
            fb.relay = true; fb.relayKind = "sub";
            std::vector<std::pair<float, std::string>> ins, outs;
            for (auto& x : b.sub->boxes) { auto c = clsOf(x.text); auto k = key + "/" + std::to_string(x.id);
                if (c == "inlet" || c == "inlet~") ins.push_back({ x.x, k }); else if (c == "outlet" || c == "outlet~") outs.push_back({ x.x, k }); }
            std::sort(ins.begin(), ins.end()); std::sort(outs.begin(), outs.end());
            for (auto& x : ins) f.subIns[key].push_back(x.second);
            for (auto& x : outs) f.subOuts[key].push_back(x.second);
            f.byKey[key] = (int) f.boxes.size(); f.boxes.push_back(fb);
            flatten(*b.sub, key + "/", fb.top, f, depth + 1);
            for (int k = 0; k < (int) f.subIns[key].size(); ++k) { auto& ib = f.boxes[(size_t) f.byKey[f.subIns[key][(size_t) k]]]; ib.relay = true; ib.relayKind = "in"; ib.relayIndex = k; ib.relayOf = key; }
            for (int k = 0; k < (int) f.subOuts[key].size(); ++k) { auto& ob = f.boxes[(size_t) f.byKey[f.subOuts[key][(size_t) k]]]; ob.relay = true; ob.relayKind = "out"; ob.relayIndex = k; ob.relayOf = key; }
            continue;
        }
        f.byKey[key] = (int) f.boxes.size(); f.boxes.push_back(fb);
    }
    for (auto& l : d.lines) f.lines.push_back({ prefix + std::to_string(l.from), l.outlet, prefix + std::to_string(l.to), l.inlet });
}
}

std::unique_ptr<Program> buildProgram(Runtime& rt, const PatchDoc& doc, std::map<std::string, std::pair<std::string, ObjPtr>>& newPool, std::map<std::string, juce::String>& errs) {
    registerAll();
    auto p = std::make_unique<Program>();
    Flat f; flatten(doc, "", -1, f, 0);
    // objects
    std::map<std::string, int> nodeOf;
    for (auto& fb : f.boxes) {
        if (fb.relay) continue;
        const PBox& b = *fb.b;
        std::string text = b.kind == "msg" ? "message " + b.text.toStdString() : b.text.toStdString();
        ObjPtr o;
        auto pit = rt.pool.find(fb.key);
        if (pit != rt.pool.end() && pit->second.first == text + "|" + b.kind.toStdString()) o = pit->second.second;
        if (o == nullptr) {
            if (b.kind == "msg") { auto* d = findDef("message"); std::vector<Atom> a; a.push_back(Atom::Sy(b.text.toStdString())); o = d->make(a); }
            else {
                auto toks = tokenize(text);
                if (toks.empty()) { errs[fb.key] = "an empty box"; continue; }
                auto* d = findDef(toks[0]);
                if (d == nullptr) { auto sg = suggest(toks[0], 2); errs[fb.key] = "no object called \"" + juce::String(toks[0]) + "\"" + (sg.empty() ? juce::String() : " - did you mean " + juce::String(sg[0]) + "?"); continue; }
                std::vector<Atom> args;
                for (size_t i = 1; i < toks.size(); ++i) { if (toks[i] == "," || toks[i] == ";") { args.push_back(Atom::Sy(toks[i])); continue; }
                    bool fl; if (isNumber(toks[i], fl)) args.push_back(fl ? Atom::F(std::atof(toks[i].c_str())) : Atom::I((double) std::atoll(toks[i].c_str()))); else args.push_back(Atom::Sy(toks[i])); }
                o = d->make(args);
                if (o == nullptr) { errs[fb.key] = "could not make it"; continue; }
            }
            o->boxId = fb.top;
            o->prepare(rt.sr);
            if (!b.state.isVoid()) o->restore(b.state);
        }
        if (o->err.isNotEmpty()) errs[fb.key] = o->err;
        newPool[fb.key] = { text + "|" + b.kind.toStdString(), o };
        Node n; n.obj = o; n.key = fb.key; n.box = fb.top; n.x = b.x; n.outs.resize((size_t) juce::jmax(0, o->nOut));
        int idx = (int) p->nodes.size();
        o->slot[(rt.gen + 1) & 1] = idx;
        nodeOf[fb.key] = idx; p->nodes.push_back(n);
        p->boxNodes[fb.top].push_back(idx);
        p->keyNodes[fb.key] = idx;
        if (fb.key.find('/') == std::string::npos) rt.objs[fb.top] = o;
    }
    // cords: through subpatch inlets / outlets to the real objects
    std::map<std::pair<std::string, int>, std::vector<std::pair<std::string, int>>> edges;
    for (auto& [a, o, b, i] : f.lines) edges[{ a, o }].push_back({ b, i });
    std::function<void(const std::string&, int, std::vector<std::pair<std::string, int>>&, int)> resolve =
        [&](const std::string& key, int inlet, std::vector<std::pair<std::string, int>>& out, int depth) {
        if (depth > 64) return;
        auto it = f.byKey.find(key); if (it == f.byKey.end()) return;
        auto& fb = f.boxes[(size_t) it->second];
        if (!fb.relay) { out.push_back({ key, inlet }); return; }
        std::pair<std::string, int> from;
        if (fb.relayKind == "sub") { auto& v = f.subIns[key]; if (inlet < 0 || inlet >= (int) v.size()) return; from = { v[(size_t) inlet], 0 }; }
        else if (fb.relayKind == "out") from = { fb.relayOf, fb.relayIndex };
        else return;   // an inlet object has no inlet of its own
        for (auto& t : edges[from]) resolve(t.first, t.second, out, depth + 1);
    };
    std::vector<std::vector<std::vector<std::pair<int, int>>>> sigEdges(p->nodes.size());   // node -> inlet -> (src node, outlet)
    for (auto& [src, targets] : edges) {
        auto ni = nodeOf.find(src.first); if (ni == nodeOf.end()) continue;    // from a relay: reached through resolve
        int sn = ni->second; auto& so = *p->nodes[(size_t) sn].obj;
        if (src.second < 0 || src.second >= so.nOut) continue;
        std::vector<std::pair<std::string, int>> real;
        for (auto& t : targets) resolve(t.first, t.second, real, 0);
        std::vector<std::pair<int, int>> conn;
        for (auto& r : real) {
            auto ti = nodeOf.find(r.first); if (ti == nodeOf.end()) continue;
            int tn = ti->second; auto& to = *p->nodes[(size_t) tn].obj;
            if (r.second < 0 || r.second >= to.nIn) continue;
            if (so.isSigOut(src.second)) {
                if (!to.isSigIn(r.second)) { errs[p->nodes[(size_t) tn].key] = "a sound cord (~) goes into an inlet that takes numbers"; continue; }
                if (sigEdges[(size_t) tn].size() < (size_t) to.nIn) sigEdges[(size_t) tn].resize((size_t) to.nIn);
                sigEdges[(size_t) tn][(size_t) r.second].push_back({ sn, src.second });
                if (r.second < 16) to.connectedIn[r.second].store(true);
            } else conn.push_back({ tn, r.second });
        }
        // right to left: the cord to the box furthest right fires first
        std::stable_sort(conn.begin(), conn.end(), [&](auto& a, auto& b) { return p->nodes[(size_t) a.first].x > p->nodes[(size_t) b.first].x; });
        p->nodes[(size_t) sn].outs[(size_t) src.second] = conn;
    }
    // the signal chain: in the order the sound flows (send~ before its receive~)
    int N = (int) p->nodes.size();
    std::vector<int> indeg((size_t) N, 0); std::vector<std::vector<int>> after((size_t) N);
    std::vector<char> isDsp((size_t) N, 0);
    for (int i = 0; i < N; ++i) isDsp[(size_t) i] = p->nodes[(size_t) i].obj->hasSignal() ? 1 : 0;
    for (int t = 0; t < N; ++t) for (auto& inl : sigEdges[(size_t) t]) for (auto& e : inl) { after[(size_t) e.first].push_back(t); indeg[(size_t) t]++; }
    for (int a = 0; a < N; ++a) if (int nm = p->nodes[(size_t) a].obj->sigSendName()) for (int b = 0; b < N; ++b) if (p->nodes[(size_t) b].obj->sigRecvName() == nm) { after[(size_t) a].push_back(b); indeg[(size_t) b]++; }
    std::vector<int> order, q; std::vector<char> done((size_t) N, 0);
    for (int i = 0; i < N; ++i) if (indeg[(size_t) i] == 0) q.push_back(i);
    while (!q.empty()) {
        std::sort(q.begin(), q.end(), [&](int a, int b) { return p->nodes[(size_t) a].x < p->nodes[(size_t) b].x; });
        int v = q.front(); q.erase(q.begin()); done[(size_t) v] = 1; order.push_back(v);
        for (int t : after[(size_t) v]) if (--indeg[(size_t) t] == 0) q.push_back(t);
    }
    for (int i = 0; i < N; ++i) if (!done[(size_t) i]) order.push_back(i);   // a loop of sound: it hears the last block
    p->sigIndex.resize((size_t) N);
    for (int i = 0; i < N; ++i) { auto& o = *p->nodes[(size_t) i].obj; p->sigIndex[(size_t) i].assign((size_t) juce::jmax(0, o.nOut), -1);
        for (int k = 0; k < o.nOut; ++k) if (o.isSigOut(k)) { p->sigIndex[(size_t) i][(size_t) k] = (int) p->sigBuf.size(); p->sigBuf.emplace_back(64, 0.0f); } }
    for (int v : order) {
        if (!isDsp[(size_t) v]) continue;
        auto& o = *p->nodes[(size_t) v].obj;
        DspStep st; st.node = v; st.ins.resize((size_t) juce::jmax(0, o.nIn));
        for (int k = 0; k < o.nIn; ++k) {
            std::vector<std::pair<int, int>> src; if ((size_t) k < sigEdges[(size_t) v].size()) src = sigEdges[(size_t) v][(size_t) k];
            if (src.size() == 1) st.ins[(size_t) k].direct = p->sigBuf[(size_t) p->sigIndex[(size_t) src[0].first][(size_t) src[0].second]].data();
            else if (src.size() > 1) { st.ins[(size_t) k].sum = src; p->sumBuf.emplace_back(64, 0.0f); st.ins[(size_t) k].direct = p->sumBuf.back().data(); }
            else { p->constBuf.emplace_back(64, 0.0f); st.ins[(size_t) k].direct = p->constBuf.back().data(); st.ins[(size_t) k].sum = { { -1 - k, 0 } }; }
            if (k < 16 && src.empty()) o.connectedIn[k].store(false);
        }
        p->dsp.push_back(st);
    }
    for (int i = 0; i < N; ++i) {
        auto& o = *p->nodes[(size_t) i].obj;
        if (o.recvName() != 0 || clsOf(juce::String(p->nodes[(size_t) i].key)).empty()) {}
        p->objSet.push_back(&o);
        if (o.wantsMidi) p->midiNodes.push_back(i);
        if (o.wantsBeat) p->beatNodes.push_back(i);
    }
    for (int i = 0; i < N; ++i) { auto t = newPool[p->nodes[(size_t) i].key].first; t = t.substr(0, t.rfind('|')); auto c = tokenize(t); if (!c.empty() && (c[0] == "receive" || c[0] == "r")) p->recvNodes.push_back(i);
        if (!c.empty() && (c[0] == "loadbang" || c[0] == "loadmess")) p->loadbangs.push_back(i); }
    std::sort(p->objSet.begin(), p->objSet.end());
    return p;
}

// ================================================================== a running patcher
Runtime::Runtime() { registerAll(); ctx.rt = this; evs.reserve(4096); inScratch.assign(8, std::vector<float>(64, 0.0f)); outScratch.assign(8, std::vector<float>(64, 0.0f)); }
Runtime::~Runtime() { auto* p = live.exchange(nullptr); juce::ignoreUnused(p); while (busy.load()) std::this_thread::yield(); }
void Runtime::prepare(double sampleRate) {
    if (sampleRate == sr) return;
    sr = sampleRate;
    for (auto& [k, v] : pool) v.second->prepare(sr);
}
void Runtime::setDoc(const PatchDoc& doc, bool fresh) {
    std::map<std::string, std::pair<std::string, ObjPtr>> newPool; std::map<std::string, juce::String> e;
    objs.clear();
    auto p = buildProgram(*this, doc, newPool, e);
    p->gen = ++gen; p->fresh = fresh;
    if (fresh) for (auto& [k, v] : newPool) juce::ignoreUnused(k, v);
    else {     // an edit: only the boxes just made say loadbang
        std::vector<int> lb; for (int i : p->loadbangs) { auto it = pool.find(p->nodes[(size_t) i].key); if (it == pool.end() || it->second.second != p->nodes[(size_t) i].obj) lb.push_back(i); }
        p->loadbangs = lb; p->fresh = !lb.empty();
    }
    errs = e;
    hasDac = hasAdc = false;
    for (auto& n : p->nodes) { auto c = clsOf(juce::String(newPool[n.key].first.substr(0, newPool[n.key].first.find('|')))); if (c == "dac~" || c == "ezdac~" || c == "out~") hasDac = true; if (c == "adc~" || c == "ezadc~" || c == "in~") hasAdc = true; }
    std::vector<ObjPtr> dropped;
    for (auto& [k, v] : pool) { auto it = newPool.find(k); if (it == newPool.end() || it->second.second != v.second) dropped.push_back(v.second); }
    pool = newPool;
    Program* np = p.get();
    auto prev = std::move(current); current = std::move(p);
    live.store(np);
    while (busy.load()) std::this_thread::yield();
    if (prev) limbo.push_back({ np->gen, std::move(prev), std::move(dropped), juce::Time::getMillisecondCounterHiRes() });
    else limbo.push_back({ np->gen, nullptr, std::move(dropped), juce::Time::getMillisecondCounterHiRes() });
}
void Runtime::midiIn(const juce::MidiMessage& m) {
    static juce::SpinLock inLock;             // (the MIDI thread and the message thread both put notes in)
    const juce::SpinLock::ScopedLockType sl(inLock);
    auto* d = m.getRawData(); int n = m.getRawDataSize();
    if (n < 1 || n > 3 || d[0] < 0x80 || d[0] >= 0xF0) return;
    std::array<unsigned char, 3> b { d[0], (unsigned char) (n > 1 ? d[1] : 0), (unsigned char) (n > 2 ? d[2] : 0) };
    midiQ.push(b);
}
void Runtime::poll() {
    Line l; while (lines.pop(l)) if (onLine) onLine(l);
    Action a; while (actions.pop(a)) if (onAction) onAction(a);
    double now = juce::Time::getMillisecondCounterHiRes();
    int ack = ackGen.load();
    limbo.erase(std::remove_if(limbo.begin(), limbo.end(), [&](Limbo& x) { return x.gen <= ack || (now - x.since > 3000 && !busy.load()); }), limbo.end());
}
void Runtime::deliver(Program& p, int node, int inlet, const Msg& m) {
    if (node < 0 || node >= (int) p.nodes.size()) return;
    ctx.depth = 0;
    p.nodes[(size_t) node].obj->msg(ctx, inlet, m);
}
void Runtime::process(const float* const* in, int nIn, float* const* out, int nOut, int n) {
    for (int c = 0; c < nOut; ++c) if (out[c]) std::fill(out[c], out[c] + n, 0.0f);
    busy.store(true);
    Program* p = live.load();
    if (p == nullptr) { busy.store(false); return; }
    auto t0 = juce::Time::getHighResolutionTicks();
    int done = 0;
    while (done < n) {
        int m = juce::jmin(64, n - done);
        const float* ip[8]; float* op[8];
        for (int c = 0; c < 8; ++c) { ip[c] = c < nIn && in[c] ? in[c] + done : nullptr; op[c] = c < nOut && out[c] ? out[c] + done : outScratch[(size_t) c].data(); }
        for (int c = 0; c < 8; ++c) if (ip[c] == nullptr) { std::fill(inScratch[(size_t) c].begin(), inScratch[(size_t) c].end(), 0.0f); ip[c] = inScratch[(size_t) c].data(); }
        for (int c = nOut; c < 8; ++c) std::fill(outScratch[(size_t) c].begin(), outScratch[(size_t) c].begin() + m, 0.0f);
        ctx.adcIn = ip; ctx.dacOut = op; ctx.nIn = nIn; ctx.nOut = nOut;
        runBlock(*p, m);
        beat += m / sr * bpm / 60.0;
        done += m;
    }
    float pk = 0; for (int c = 0; c < nOut; ++c) if (out[c]) for (int i = 0; i < n; ++i) { float v = std::abs(out[c][i]); if (!(v <= 100.0f)) { out[c][i] = 0; v = 0; } pk = juce::jmax(pk, v); }
    outPeak.store(juce::jmax(pk, outPeak.load() * 0.96f));
    double us = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1e6;
    cpu.store((float) (us / (n / sr * 1e6)));
    busy.store(false);
}
void Runtime::runBlock(Program& p, int n) {
    ctx.prog = &p; ctx.sr = sr; ctx.bpm = bpm > 1 ? bpm : 120; ctx.beat = beat; ctx.running = running; ctx.beatsPerBar = beatsPerBar; ctx.beatUnit = beatUnit;
    if (p.gen != lastGen) {        // a new program: forget the clocks of objects that left, say loadbang
        lastGen = p.gen;
        evs.erase(std::remove_if(evs.begin(), evs.end(), [&](const Ev& e) { return !std::binary_search(p.objSet.begin(), p.objSet.end(), e.o); }), evs.end());
        for (auto& nd : p.nodes) nd.obj->dspStarted(ctx);
        if (p.fresh) for (int i : p.loadbangs) { ctx.depth = 0; ctx.nowMs = ctx.nowMs; p.nodes[(size_t) i].obj->loadbang(ctx); }
        ackGen.store(p.gen);
    }
    // 1 - what you did on the screen
    UiIn u; int guard = 0;
    while (guard++ < 128 && uiIn.pop(u)) {
        if (u.key[0]) { auto it = p.keyNodes.find(std::string(u.key)); if (it != p.keyNodes.end()) deliver(p, it->second, u.inlet, u.m); continue; }
        auto it = p.boxNodes.find(u.box); if (it != p.boxNodes.end() && !it->second.empty()) deliver(p, it->second.front(), u.inlet, u.m); }
    // 2 - MIDI
    std::array<unsigned char, 3> mb;
    while (midiQ.pop(mb)) for (int i : p.midiNodes) { ctx.depth = 0; p.nodes[(size_t) i].obj->midi(ctx, mb.data(), 3); }
    // 3 - the clocks that go off in this block
    double end = ctx.nowMs + n / sr * 1000.0, start = ctx.nowMs;
    int fired = 0;
    while (fired < 2000) {
        int best = -1;
        for (int i = 0; i < (int) evs.size(); ++i) if (evs[(size_t) i].t < end && (best < 0 || evs[(size_t) i].t < evs[(size_t) best].t || (evs[(size_t) i].t == evs[(size_t) best].t && evs[(size_t) i].seq < evs[(size_t) best].seq))) best = i;
        if (best < 0) break;
        Ev e = evs[(size_t) best]; evs.erase(evs.begin() + best);
        ctx.nowMs = juce::jmax(start, e.t); ctx.depth = 0;
        e.o->tick(ctx, e.tag); ++fired;
    }
    if (fired >= 2000) ctx.error(nullptr, "too many clocks in one moment (a metro of 0 ms?)");
    for (int i : p.beatNodes) { ctx.depth = 0; p.nodes[(size_t) i].obj->beatPoll(ctx); }
    ctx.nowMs = start;
    // 4 - the signal world
    for (auto& st : p.dsp) {
        auto& nd = p.nodes[(size_t) st.node]; auto& o = *nd.obj;
        const float* ins[64]; float* outs[64]; static float zero[64] = {}; static float junk[64];
        int ni = juce::jmin(64, o.nIn), no = juce::jmin(64, o.nOut);
        for (int k = 0; k < ni; ++k) {
            auto& si = st.ins[(size_t) k];
            if (!si.sum.empty() && si.sum[0].first < 0) { float v = (size_t) k < o.sv.size() ? o.sv[(size_t) k] : 0.0f; auto* d = const_cast<float*>(si.direct); for (int i = 0; i < n; ++i) d[i] = v; }
            else if (!si.sum.empty()) { auto* d = const_cast<float*>(si.direct); std::fill(d, d + n, 0.0f);
                for (auto& s : si.sum) { auto* b = p.sigBuf[(size_t) p.sigIndex[(size_t) s.first][(size_t) s.second]].data(); for (int i = 0; i < n; ++i) d[i] += b[i]; } }
            ins[k] = si.direct ? si.direct : zero;
        }
        for (int k = 0; k < no; ++k) { int bi = p.sigIndex[(size_t) st.node][(size_t) k]; outs[k] = bi >= 0 ? p.sigBuf[(size_t) bi].data() : junk; }
        o.dsp(ctx, ins, outs, n);
    }
    ctx.nowMs = end;
}

// ================================================================== the context
void Ctx::out(Obj* o, int outlet, const Msg& m) {
    if (prog == nullptr || o == nullptr) return;
    int idx = o->slot[prog->gen & 1];
    if (idx < 0 || idx >= (int) prog->nodes.size() || prog->nodes[(size_t) idx].obj.get() != o) return;
    auto& nd = prog->nodes[(size_t) idx];
    if (outlet < 0 || outlet >= (int) nd.outs.size()) return;
    if (depth > 160) { if (depth == 161) { ++depth; print(o->boxId, "stack overflow: a message goes round in a loop (put a [deferlow] or [delay 0] in it)", 'e'); } return; }
    ++depth;
    for (auto& t : nd.outs[(size_t) outlet]) prog->nodes[(size_t) t.first].obj->msg(*this, t.second, m);
    --depth;
}
void Ctx::send(int name, const Msg& m) {
    if (prog == nullptr || name == 0) return;
    if (depth > 160) return;
    ++depth;
    for (int i : prog->recvNodes) { auto& o = *prog->nodes[(size_t) i].obj; if (o.recvName() == name) o.msg(*this, 999, m); }
    --depth;
}
void Ctx::delay(Obj* o, double ms, int tag) {
    cancel(o, tag);
    if (!(ms >= 0)) ms = 0;
    if ((int) rt->evs.size() >= 4000) return;
    rt->evs.push_back({ nowMs + ms, rt->seq++, o, tag });
}
void Ctx::cancel(Obj* o, int tag) { auto& e = rt->evs; e.erase(std::remove_if(e.begin(), e.end(), [&](const Runtime::Ev& x) { return x.o == o && x.tag == tag; }), e.end()); }
double Ctx::noteMs(const Atom& a, double dflt) const {
    if (a.num()) return a.d();
    auto s = symStr(a.s);
    double beatMs = 60000.0 / juce::jmax(1.0, bpm);
    if (s.size() >= 2 && (s.back() == 'n' || s[s.size() - 2] == 'n')) {
        char mod = s.back() == 'n' ? 0 : s.back();
        auto num = s.substr(0, s.find('n'));
        double d = std::atof(num.c_str()); if (d <= 0) return dflt;
        double ms = beatMs * 4.0 / d;
        if (mod == 't') ms *= 2.0 / 3.0; else if (mod == 'd') ms *= 1.5;
        return ms;
    }
    if (s == "bar" || s == "1bar") return beatMs * beatsPerBar * 4.0 / juce::jmax(1, beatUnit);
    if (s.size() > 2 && s.substr(s.size() - 2) == "ms") return std::atof(s.c_str());
    if (s.size() > 1 && s.back() == 's') return std::atof(s.c_str()) * 1000.0;
    if (s.size() > 2 && s.substr(s.size() - 2) == "hz") { double hz = std::atof(s.c_str()); return hz > 0 ? 1000.0 / hz : dflt; }
    return dflt;
}
BufPtr Ctx::buffer(int name, bool make) {
    const juce::SpinLock::ScopedLockType sl(rt->bufLock);
    auto it = rt->bufs.find(name);
    if (it != rt->bufs.end()) return it->second;
    if (!make) return nullptr;
    auto b = std::make_shared<Buf>(); b->name = name; b->sr = sr; rt->bufs[name] = b; return b;
}
Msg* Ctx::value(int name) { return &rt->values[name]; }
std::vector<float>* Ctx::table(int name, bool make) { auto it = rt->tables.find(name); if (it != rt->tables.end()) return &it->second; if (!make) return nullptr; auto& t = rt->tables[name]; t.assign(128, 0.0f); return &t; }
float* Ctx::sigSend(int name) { auto& v = rt->sigSends[name]; if (v.size() < 64) v.assign(64, 0.0f); return v.data(); }
void Ctx::print(int box, const std::string& s, char kind) { Line l; l.kind = kind; l.box = box; std::strncpy(l.text, s.c_str(), sizeof l.text - 1); l.text[sizeof l.text - 1] = 0; rt->lines.push(l); }
void Ctx::action(const char* what, double a, double b, double c, const std::string& text, int box) {
    Action x; std::strncpy(x.what, what, sizeof x.what - 1); x.what[sizeof x.what - 1] = 0; x.a = a; x.b = b; x.c = c; x.box = box;
    std::strncpy(x.text, text.c_str(), sizeof x.text - 1); x.text[sizeof x.text - 1] = 0; rt->actions.push(x);
}
unsigned Ctx::rand() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

// ================================================================== expressions
struct Expr::N { char op = 'n'; double v = 0; int idx = 0; std::string fn; std::vector<std::shared_ptr<N>> k; };
namespace {
struct EP {
    const std::string& s; size_t i = 0; std::string err; int maxIn = 0; bool sig = false; bool anyFloat = false;
    explicit EP(const std::string& t) : s(t) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; }
    bool eat(const char* t) { ws(); size_t n = std::strlen(t); if (s.compare(i, n, t) == 0) { i += n; return true; } return false; }
    using P = std::shared_ptr<Expr::N>;
    P mk(char op, P a = nullptr, P b = nullptr) { auto n = std::make_shared<Expr::N>(); n->op = op; if (a) n->k.push_back(a); if (b) n->k.push_back(b); return n; }
    P ternary() { auto c = orr(); ws(); if (eat("?")) { auto a = ternary(); if (!eat(":")) { err = "':' missing"; return c; } auto b = ternary(); auto n = mk('?', c, a); n->k.push_back(b); return n; } return c; }
    P orr() { auto a = andd(); while (true) { if (eat("||")) a = mk('O', a, andd()); else return a; } }
    P andd() { auto a = bor(); while (true) { if (eat("&&")) a = mk('A', a, bor()); else return a; } }
    P bor() { auto a = bxor(); while (true) { ws(); if (i < s.size() && s[i] == '|' && (i + 1 >= s.size() || s[i + 1] != '|')) { ++i; a = mk('|', a, bxor()); } else return a; } }
    P bxor() { auto a = band(); while (eat("^")) a = mk('^', a, band()); return a; }
    P band() { auto a = eq(); while (true) { ws(); if (i < s.size() && s[i] == '&' && (i + 1 >= s.size() || s[i + 1] != '&')) { ++i; a = mk('&', a, eq()); } else return a; } }
    P eq() { auto a = rel(); while (true) { if (eat("==")) a = mk('=', a, rel()); else if (eat("!=")) a = mk('!', a, rel()); else return a; } }
    P rel() { auto a = shift(); while (true) { if (eat("<=")) a = mk('l', a, shift()); else if (eat(">=")) a = mk('g', a, shift()); else if (eat("<<")) a = mk('L', a, shift());
        else if (eat(">>")) a = mk('R', a, shift()); else if (eat("<")) a = mk('<', a, shift()); else if (eat(">")) a = mk('>', a, shift()); else return a; } }
    P shift() { return add(); }
    P add() { auto a = mul(); while (true) { if (eat("+")) a = mk('+', a, mul()); else if (eat("-")) a = mk('-', a, mul()); else return a; } }
    P mul() { auto a = un(); while (true) { if (eat("*")) a = mk('*', a, un()); else if (eat("/")) a = mk('/', a, un()); else if (eat("%")) a = mk('%', a, un()); else return a; } }
    P un() { if (eat("-")) return mk('u', un()); if (eat("!")) return mk('N', un()); if (eat("~")) return mk('~', un()); if (eat("+")) return un(); return prim(); }
    P prim() {
        ws(); if (i >= s.size()) { err = "the expression ends too soon"; return mk('n'); }
        char c = s[i];
        if (c == '(') { ++i; auto a = ternary(); if (!eat(")")) err = "')' missing"; return a; }
        if (c == '$') {
            ++i; if (i >= s.size()) { err = "$ needs a letter and a number ($f1)"; return mk('n'); }
            char t = (char) std::tolower((unsigned char) s[i++]); int k = 0; while (i < s.size() && std::isdigit((unsigned char) s[i])) k = k * 10 + (s[i++] - '0');
            if (k < 1 || k > 9) { err = "inlets are $f1 to $f9"; return mk('n'); }
            maxIn = std::max(maxIn, k); if (t == 'v' || t == 'x') sig = true; if (t == 'f' || t == 'v' || t == 'x') anyFloat = true;
            auto n = mk(t == 'i' ? 'i' : 'f'); n->idx = k - 1; return n;
        }
        if (std::isdigit((unsigned char) c) || c == '.') {
            size_t st = i; bool fl = false; while (i < s.size() && (std::isdigit((unsigned char) s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) { if (!std::isdigit((unsigned char) s[i])) fl = true; ++i; }
            auto n = mk('n'); n->v = std::atof(s.substr(st, i - st).c_str()); if (fl) anyFloat = true; return n;
        }
        if (std::isalpha((unsigned char) c) || c == '_') {
            size_t st = i; while (i < s.size() && (std::isalnum((unsigned char) s[i]) || s[i] == '_')) ++i;
            auto name = s.substr(st, i - st);
            if (name == "pi" || name == "PI") { auto n = mk('n'); n->v = juce::MathConstants<double>::pi; anyFloat = true; return n; }
            if (name == "e" ) { auto n = mk('n'); n->v = 2.718281828459045; anyFloat = true; return n; }
            if (!eat("(")) { err = "\"" + name + "\" needs ( )"; return mk('n'); }
            auto n = mk('F'); n->fn = name;
            ws(); if (!eat(")")) { do n->k.push_back(ternary()); while (eat(",")); if (!eat(")")) err = "')' missing after " + name; }
            static const std::set<std::string> fl = { "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "exp", "log", "ln", "log10", "log2", "sqrt", "pow", "float", "fmod", "mtof", "ftom", "dbtoa", "atodb", "scale", "fabs" };
            if (fl.count(name)) anyFloat = true;
            return n;
        }
        err = std::string("I don't understand \"") + c + "\""; ++i; return mk('n');
    }
};
double ev(const Expr::N& n, const double* in, int nIn, bool im) {
    auto K = [&](int k) { return k < (int) n.k.size() ? ev(*n.k[(size_t) k], in, nIn, im) : 0.0; };
    switch (n.op) {
        case 'n': return n.v;
        case 'i': return n.idx < nIn ? std::trunc(in[n.idx]) : 0.0;
        case 'f': return n.idx < nIn ? in[n.idx] : 0.0;
        case '+': return K(0) + K(1); case '-': return K(0) - K(1); case '*': return K(0) * K(1);
        case '/': { double b = K(1); if (b == 0) return 0; return im ? std::trunc(K(0) / b) : K(0) / b; }
        case '%': { long long b = (long long) K(1); if (b == 0) return 0; return (double) ((long long) K(0) % b); }
        case 'u': return -K(0); case 'N': return K(0) == 0 ? 1 : 0; case '~': return (double) ~(long long) K(0);
        case '=': return K(0) == K(1); case '!': return K(0) != K(1); case '<': return K(0) < K(1); case '>': return K(0) > K(1);
        case 'l': return K(0) <= K(1); case 'g': return K(0) >= K(1);
        case 'L': return (double) ((long long) K(0) << ((long long) K(1) & 63)); case 'R': return (double) ((long long) K(0) >> ((long long) K(1) & 63));
        case 'A': return (K(0) != 0 && K(1) != 0) ? 1 : 0; case 'O': return (K(0) != 0 || K(1) != 0) ? 1 : 0;
        case '&': return (double) ((long long) K(0) & (long long) K(1)); case '|': return (double) ((long long) K(0) | (long long) K(1)); case '^': return (double) ((long long) K(0) ^ (long long) K(1));
        case '?': return K(0) != 0 ? K(1) : K(2);
        case 'F': {
            const auto& f = n.fn; double a = K(0), b = K(1);
            if (f == "sin") return std::sin(a); if (f == "cos") return std::cos(a); if (f == "tan") return std::tan(a);
            if (f == "asin") return std::asin(juce::jlimit(-1.0, 1.0, a)); if (f == "acos") return std::acos(juce::jlimit(-1.0, 1.0, a)); if (f == "atan") return std::atan(a);
            if (f == "atan2") return std::atan2(a, b); if (f == "sinh") return std::sinh(a); if (f == "cosh") return std::cosh(a); if (f == "tanh") return std::tanh(a);
            if (f == "exp") return std::exp(juce::jmin(a, 700.0)); if (f == "log" || f == "ln") return a > 0 ? std::log(a) : -1000.0; if (f == "log10") return a > 0 ? std::log10(a) : -1000.0; if (f == "log2") return a > 0 ? std::log2(a) : -1000.0;
            if (f == "sqrt") return a > 0 ? std::sqrt(a) : 0.0; if (f == "pow") return std::pow(a, b);
            if (f == "abs" || f == "fabs") return std::abs(a); if (f == "floor") return std::floor(a); if (f == "ceil") return std::ceil(a); if (f == "round") return std::round(a);
            if (f == "int" || f == "trunc") return std::trunc(a); if (f == "float") return a;
            if (f == "min") return std::min(a, b); if (f == "max") return std::max(a, b); if (f == "fmod") return b != 0 ? std::fmod(a, b) : 0.0;
            if (f == "clip") return juce::jlimit(std::min(b, K(2)), std::max(b, K(2)), a);
            if (f == "if") return a != 0 ? b : K(2);
            if (f == "random") { static unsigned s = 77u; s = s * 1664525u + 1013904223u; long long lo = (long long) a, hi = (long long) b; if (hi <= lo) return (double) lo; return (double) (lo + (long long) ((s >> 8) % (unsigned) (hi - lo))); }
            if (f == "mtof") return 440.0 * std::pow(2.0, (a - 69.0) / 12.0); if (f == "ftom") return a > 0 ? 69.0 + 12.0 * std::log2(a / 440.0) : 0.0;
            if (f == "dbtoa") return std::pow(10.0, a / 20.0); if (f == "atodb") return a > 0 ? 20.0 * std::log10(a) : -999.0;
            if (f == "scale") { double il = b, ih = K(2), ol = K(3), oh = K(4); return ih == il ? ol : ol + (a - il) / (ih - il) * (oh - ol); }
            if (f == "sign") return a > 0 ? 1 : a < 0 ? -1 : 0;
            if (f == "wrap") { double lo = b, hi = K(2); if (hi <= lo) return lo; double r = std::fmod(a - lo, hi - lo); return r < 0 ? r + hi : r + lo; }
            return 0.0;
        }
    }
    return 0.0;
}
}
bool Expr::parse(const std::string& text) {
    EP p(text); root = p.ternary(); p.ws();
    if (p.err.empty() && p.i < text.size()) p.err = "something extra at the end: \"" + text.substr(p.i, 12) + "\"";
    err = juce::String(p.err); maxIn = std::max(1, p.maxIn); usesSig = p.sig;
    if (root) root->v = root->v;    // (keeps the tree)
    intMode = !p.anyFloat;
    return p.err.empty();
}
double Expr::eval(const double* in, int nIn) const { return root ? ev(*root, in, nIn, intMode) : 0.0; }

}} // namespace arco::px
