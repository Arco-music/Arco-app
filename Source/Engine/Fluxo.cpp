// ARCO 2.5: FLUXO - the patch of blocks, its words, and how it becomes a PATCHER patch.
#include "Fluxo.h"
#include "PatcherExamples.h"
#include <set>
#include <functional>

namespace arco { namespace fx {

const char* kindName(Kind k) { return k == Sound ? "sound" : k == Notes ? "notes" : k == Move ? "movement" : "anything"; }
static juce::String fxU8(const char* s) { return juce::String::fromUTF8(s); }

// ================================================================== the patch
int FluxDoc::add(const std::string& type, float x, float y) { Block b; b.id = nextId++; b.type = type; b.x = x; b.y = y; blocks.push_back(b); return b.id; }
void FluxDoc::remove(int id) {
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [id](const Block& b) { return b.id == id; }), blocks.end());
    wires.erase(std::remove_if(wires.begin(), wires.end(), [id](const Wire& w) { return w.a == id || w.b == id; }), wires.end());
}
void FluxDoc::connect(const Wire& w) {
    for (auto& x : wires) if (x.a == w.a && x.out == w.out && x.b == w.b && x.in == w.in && x.field == w.field) return;
    wires.push_back(w);
}
static juce::var mapToVar(const std::map<std::string, juce::var>& m) { auto* o = new juce::DynamicObject(); for (auto& kv : m) o->setProperty(juce::Identifier(kv.first), kv.second); return juce::var(o); }
juce::var FluxDoc::toVar() const {
    auto* o = new juce::DynamicObject(); o->setProperty("name", name); o->setProperty("notes", notes); o->setProperty("nextId", nextId); o->setProperty("chain", chain);
    juce::Array<juce::var> bs;
    for (auto& b : blocks) { auto* x = new juce::DynamicObject(); x->setProperty("id", b.id); x->setProperty("type", juce::String(b.type)); x->setProperty("x", b.x); x->setProperty("y", b.y);
        x->setProperty("f", mapToVar(b.f)); if (b.bypass) x->setProperty("bypass", true); if (!b.state.isVoid()) x->setProperty("state", b.state);
        if (b.text.isNotEmpty()) x->setProperty("text", b.text); if (b.label.isNotEmpty()) x->setProperty("label", b.label); if (b.own) x->setProperty("own", pluginToVar(*b.own));
        bs.add(juce::var(x)); }
    o->setProperty("blocks", bs);
    juce::Array<juce::var> ws; for (auto& w : wires) { juce::Array<juce::var> a; a.add(w.a); a.add(w.out); a.add(w.b); a.add(w.in); a.add(juce::String(w.field)); ws.add(a); }
    o->setProperty("wires", ws);
    return juce::var(o);
}
FluxDoc FluxDoc::fromVar(const juce::var& v) {
    FluxDoc d; if (!v.isObject()) return d;
    d.name = v["name"].toString(); if (d.name.isEmpty()) d.name = "My Fluxo"; d.notes = v["notes"].toString(); d.chain = v.hasProperty("chain") ? (bool) v["chain"] : true;
    if (auto* a = v["blocks"].getArray()) for (auto& x : *a) { Block b; b.id = x["id"]; b.type = x["type"].toString().toStdString(); b.x = (float) (double) x["x"]; b.y = (float) (double) x["y"];
        if (auto* fo = x["f"].getDynamicObject()) for (auto& p : fo->getProperties()) b.f[p.name.toString().toStdString()] = p.value;
        b.bypass = x["bypass"]; b.state = x["state"]; b.text = x["text"].toString(); b.label = x["label"].toString();
        if (x["own"].isObject()) b.own = pluginFromVar(x["own"]);
        d.blocks.push_back(b); d.nextId = juce::jmax(d.nextId, b.id + 1); }
    d.nextId = juce::jmax(d.nextId, (int) v["nextId"]);
    if (auto* a = v["wires"].getArray()) for (auto& x : *a) if (auto* w = x.getArray()) if (w->size() >= 4) { Wire q; q.a = (*w)[0]; q.out = (*w)[1]; q.b = (*w)[2]; q.in = (*w)[3]; if (w->size() > 4) q.field = (*w)[4].toString().toStdString(); d.wires.push_back(q); }
    return d;
}

// ================================================================== words
juce::String noteName(int n) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1); }
juce::var valueOf(const Block& b, const FieldDef& f) {
    auto it = b.f.find(f.id); if (it != b.f.end() && !it->second.isVoid()) return it->second;
    if (f.type == 's') return juce::String(f.defText); return f.def;
}
static std::string fmtNum(double v) {
    if (std::abs(v - std::round(v)) < 1e-9 && std::abs(v) < 1e9) return std::to_string((long long) std::llround(v));
    auto s = juce::String(v, 6); while (s.endsWithChar('0')) s = s.dropLastCharacters(1); if (s.endsWithChar('.')) s << "0"; return s.toStdString();
}
std::string pxValue(const FieldDef& f, const juce::var& v) {
    if (f.type == 's') return v.toString().toStdString();
    if (f.type == 'c') { int i = juce::jlimit(0, juce::jmax(0, (int) f.opts.size() - 1), (int) std::lround((double) v)); return f.opts.empty() ? std::string() : f.opts[(size_t) i].value; }
    double x = (double) v; if (f.type == 'k') return fmtNum(std::round(x));
    double y = x * f.mul; if (f.mul != 1 && !(f.step >= 1 && f.mul == 1)) { if (std::abs(y - std::round(y)) < 1e-9) return fmtNum(y) + ".0"; } return fmtNum(y);
}
juce::String shown(const FieldDef& f, const juce::var& v) {
    if (f.type == 's') { auto s = v.toString(); return s.isEmpty() ? juce::String("...") : s; }
    if (f.type == 'c') { int i = juce::jlimit(0, juce::jmax(0, (int) f.opts.size() - 1), (int) std::lround((double) v)); return f.opts.empty() ? juce::String() : fxU8(f.opts[(size_t) i].label.c_str()); }
    double x = (double) v; if (f.type == 'k') return noteName((int) std::lround(x));
    juce::String t = (f.step >= 1 || std::abs(x) >= 100) ? juce::String((long long) std::llround(x)) : std::abs(x) >= 10 ? juce::String(x, 1) : juce::String(x, 2);
    if (t.contains(".")) { while (t.endsWithChar('0')) t = t.dropLastCharacters(1); if (t.endsWithChar('.')) t = t.dropLastCharacters(1); }
    return f.unit.empty() ? t : t + (f.unit == "%" ? " %" : " " + fxU8(f.unit.c_str()));
}
double toNorm(const FieldDef& f, const juce::var& v) {
    if (f.type == 's') return 0; double x = (double) v; if (f.hi == f.lo) return 0;
    if (f.expo && f.lo > 0) return juce::jlimit(0.0, 1.0, std::log(juce::jmax(f.lo, x) / f.lo) / std::log(f.hi / f.lo));
    return juce::jlimit(0.0, 1.0, (x - f.lo) / (f.hi - f.lo));
}
juce::var fromNorm(const FieldDef& f, double t) {
    t = juce::jlimit(0.0, 1.0, t);
    double x = f.expo && f.lo > 0 ? f.lo * std::pow(f.hi / f.lo, t) : f.lo + t * (f.hi - f.lo);
    if (f.type == 'c' || f.type == 'k') x = std::round(x);
    else if (f.step > 0) x = std::round(x / f.step) * f.step;
    return x;
}
std::vector<Piece> pieces(const BlockDef& d) {
    std::vector<Piece> v; juce::String s = fxU8(d.sentence.c_str()); int i = 0;
    while (i < s.length()) { int a = s.indexOfChar(i, '{'); if (a < 0) { v.push_back({ false, s.substring(i), {} }); break; }
        if (a > i) v.push_back({ false, s.substring(i, a), {} });
        int b = s.indexOfChar(a, '}'); if (b < 0) { v.push_back({ false, s.substring(a), {} }); break; }
        v.push_back({ true, {}, s.substring(a + 1, b).toStdString() }); i = b + 1; }
    return v;
}
static std::string resolve(std::string pat, const std::string& val) { size_t k = 0; while ((k = pat.find("$1", k)) != std::string::npos) { pat.replace(k, 2, val); k += val.size(); } return pat; }

// ================================================================== what a block is
static std::map<std::string, std::shared_ptr<BlockDef>>& boxCache() { static std::map<std::string, std::shared_ptr<BlockDef>> c; return c; }
static std::shared_ptr<BlockDef> boxDef(const juce::String& text) {
    auto key = text.toStdString(); auto& c = boxCache(); auto it = c.find(key); if (it != c.end()) return it->second;
    if (c.size() > 2000) c.clear();
    auto d = std::make_shared<BlockDef>(); d->id = "box"; d->shelf = "Max boxes";
    px::PBox pb; pb.kind = "obj"; pb.text = text; auto io = px::boxIO(pb);
    d->name = io.cls.empty() ? "Max box" : io.cls; d->sentence = key;
    if (auto* od = px::findDef(io.cls)) { d->help = od->desc; d->words = od->words; }
    auto names = [&](const std::string& s) { std::vector<std::string> v; juce::StringArray a; a.addTokens(juce::String(s), "|", ""); for (auto& x : a) v.push_back(x.trim().toStdString()); return v; };
    std::vector<std::string> inN, outN; if (auto* od = px::findDef(io.cls)) { inN = names(od->ins); outN = names(od->outs); }
    for (int k = 0; k < io.ins; ++k) d->in("i" + std::to_string(k), k < (int) inN.size() ? inN[(size_t) k] : "in", (io.sigIn >> k) & 1ULL ? Sound : Any, "o", k);
    for (int k = 0; k < io.outs; ++k) d->out("o" + std::to_string(k), k < (int) outN.size() ? outN[(size_t) k] : "out", (io.sigOut >> k) & 1ULL ? Sound : Any, "o", k);
    d->body = "obj o 0 0 " + key;
    if (io.ui) { d->ui = "px:" + io.cls; d->uiBox = "o"; d->uiH = juce::jlimit(24, 160, io.uiH > 0 ? io.uiH : 40); }
    if (!io.ok) d->help = io.err.toStdString();
    c[key] = d; return d;
}
static std::shared_ptr<BlockDef> patchDef(const juce::String& json) {
    auto key = "patch:" + juce::String::toHexString(json.hashCode64()).toStdString(); auto& c = boxCache(); auto it = c.find(key); if (it != c.end()) return it->second;
    auto d = std::make_shared<BlockDef>(); d->id = "patch"; d->shelf = "Max boxes"; d->name = "Classic patch"; d->sentence = "A classic patch (Max / MSP) - double-click it to change it";
    d->help = "A whole PATCHER patch inside one block. Its [inlet] and [outlet] boxes are its ways in and out.";
    auto doc = px::PatchDoc::fromVar(juce::JSON::parse(json));
    std::vector<std::pair<float, std::string>> ins, outs; for (auto& b : doc.boxes) { auto c2 = px::clsOf(b.text); if (c2 == "inlet" || c2 == "inlet~") ins.push_back({ b.x, c2 }); if (c2 == "outlet" || c2 == "outlet~") outs.push_back({ b.x, c2 }); }
    std::sort(ins.begin(), ins.end()); std::sort(outs.begin(), outs.end());
    for (int k = 0; k < (int) ins.size(); ++k) d->in("i" + std::to_string(k), ins[(size_t) k].second == "inlet~" ? "sound" : "in", ins[(size_t) k].second == "inlet~" ? Sound : Any, "#", k);
    for (int k = 0; k < (int) outs.size(); ++k) d->out("o" + std::to_string(k), outs[(size_t) k].second == "outlet~" ? "sound" : "out", outs[(size_t) k].second == "outlet~" ? Sound : Any, "#", k);
    c[key] = d; return d;
}
const BlockDef* defOf(const Block& b) {
    if (b.own) return b.own.get();
    if (b.type == "box") return boxDef(b.text).get();
    if (b.type == "patch") return patchDef(b.text).get();
    return findBlock(b.type);
}

// ================================================================== compiling
namespace {
struct Pin { int box; int io; };
struct Pins { std::vector<std::vector<Pin>> in, out; std::map<std::string, Pin> field; };
struct Builder {
    Compiled& C; int mode;            // 0 the FLUXO screen, 1 a STUDIO device, 2 inside a plug-in
    px::PatchDoc& D; std::string prefix;
    std::map<int, Pins> pins;
    Builder(Compiled& c, int m, px::PatchDoc& d, std::string p) : C(c), mode(m), D(d), prefix(std::move(p)) {}

    // the little patch of a block, from its text
    static std::map<std::string, int> parseBody(px::PatchDoc& in, const std::string& body) {
        std::map<std::string, int> ids;
        for (auto line : juce::StringArray::fromLines(juce::String(body))) {
            line = line.trim(); if (line.isEmpty()) continue;
            if (line.startsWith("- ")) {
                for (auto seg : juce::StringArray::fromTokens(line.substring(2), ";", "")) { auto t = juce::StringArray::fromTokens(seg.trim(), " ", ""); t.removeEmptyStrings();
                    if (t.size() < 4) continue; auto a = ids.find(t[0].toStdString()), b = ids.find(t[2].toStdString()); if (a == ids.end() || b == ids.end()) continue;
                    in.connect(a->second, t[1].getIntValue(), b->second, t[3].getIntValue()); }
                continue; }
            auto t = juce::StringArray::fromTokens(line, " ", ""); t.removeEmptyStrings(); if (t.size() < 4) continue;
            juce::String kind = t[0] == "obj" ? "obj" : t[0] == "msg" ? "msg" : t[0] == "com" ? "comment" : ""; if (kind.isEmpty()) continue;
            juce::String rest; for (int i = 4; i < t.size(); ++i) rest << (i > 4 ? " " : "") << t[i];
            ids[t[1].toStdString()] = in.add(kind, rest, (float) t[2].getIntValue(), (float) t[3].getIntValue());
        }
        return ids;
    }
    static std::string fill(const BlockDef& d, const Block& b, bool current) {
        std::string s = d.body;
        for (auto& f : d.fields) { std::string tag = "{" + f.id + "}"; if (s.find(tag) == std::string::npos) continue;
            juce::var v = (f.bake || current) ? valueOf(b, f) : (f.type == 's' ? juce::var(juce::String(f.defText)) : juce::var(f.def));
            auto val = pxValue(f, v); if (val.empty()) val = "0"; size_t k; while ((k = s.find(tag)) != std::string::npos) s.replace(k, tag.size(), val); }
        return s;
    }
    // a word moved by a cable (0..1 in): the boxes that turn it into the word's value
    void fieldChain(px::PatchDoc& in, int from, const FieldDef& f, const std::map<std::string, int>& ids, int x) {
        auto target = [&](int src, const std::string& pat) { for (auto& t : f.to) { auto it = ids.find(t.box); if (it == ids.end()) continue;
            int m = in.add("msg", resolve(t.pat, pat), (float) x, 5200); in.connect(src, 0, m, 0); in.connect(m, 0, it->second, t.inlet); } };
        if (f.type == 'c') {
            int n = juce::jmax(1, (int) f.opts.size());
            int sc = in.add("obj", "scale 0. 1. 0. " + juce::String(n - 0.001, 3), (float) x, 5040); in.connect(from, 0, sc, 0);
            int tr = in.add("obj", "trunc", (float) x, 5080); in.connect(sc, 0, tr, 0);
            int ch = in.add("obj", "change", (float) x, 5100); in.connect(tr, 0, ch, 0);
            juce::String sel = "sel"; for (int k = 0; k < n; ++k) sel << " " << k;
            int se = in.add("obj", sel, (float) x, 5140); in.connect(ch, 0, se, 0);
            for (int k = 0; k < n; ++k) for (auto& t : f.to) { auto it = ids.find(t.box); if (it == ids.end()) continue;
                int m = in.add("msg", resolve(t.pat, f.opts[(size_t) k].value), (float) x + k * 30, 5200); in.connect(se, k, m, 0); in.connect(m, 0, it->second, t.inlet); }
            return;
        }
        if (f.type == 's') return;
        int last;
        if (f.expo && f.lo > 0) last = in.add("obj", "expr " + juce::String(f.lo) + " * pow(" + juce::String(f.hi / f.lo) + ", $f1)", (float) x, 5040);
        else last = in.add("obj", "scale 0. 1. " + juce::String(fmtNum(f.lo)) + ". " + juce::String(fmtNum(f.hi)) + ".", (float) x, 5040);
        in.connect(from, 0, last, 0);
        if (f.step > 0) { int r = in.add("obj", "round " + juce::String(fmtNum(f.step)), (float) x, 5080); in.connect(last, 0, r, 0); last = r; }
        if (f.mul != 1) { int m = in.add("obj", "* " + juce::String(fmtNum(f.mul)) + (std::abs(f.mul - std::round(f.mul)) > 1e-9 ? "" : "."), (float) x, 5120); in.connect(last, 0, m, 0); last = m; }
        target(last, "$1");
    }

    void block(const Block& b, const BlockDef& d, const std::set<std::string>& wiredFields, const std::vector<std::pair<std::string, int>>& extraFieldIns) {
        Pins P; P.in.resize(d.ins.size()); P.out.resize(d.outs.size());
        if (!d.port.empty()) { portBlock(b, d, P); pins[b.id] = P; return; }
        px::PBox pb; pb.id = b.id; pb.kind = "sub"; pb.x = b.x; pb.y = b.y; pb.text = "p " + juce::String(d.name).retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
        if (pb.text == "p ") pb.text = "p block";
        pb.sub = std::make_shared<px::PatchDoc>(); auto& in = *pb.sub; in.nextId = 1;
        std::map<std::string, int> ids;
        bool bypass = b.bypass && !d.ins.empty() && !d.outs.empty() && d.ins[0].kind == Sound && d.outs[0].kind == Sound;
        if (d.inner) {           // a plug-in: its blocks, one level down
            FluxDoc inner = *d.inner;
            for (auto& fr : d.front) { auto key = std::to_string(fr.first) + "." + fr.second; auto it = b.f.find(key); if (it != b.f.end()) if (auto* ib = inner.block(fr.first)) ib->f[fr.second] = it->second; }
            // the words moved from outside: a hidden way in each
            int k = 0; std::vector<std::string> proxied;
            for (auto& fr : d.front) { auto key = std::to_string(fr.first) + "." + fr.second; if (!wiredFields.count(key)) continue;
                int pid = inner.add("port.move.in", 0, 1.0e6f + (float) k++); inner.connect({ pid, 0, fr.first, 0, fr.second }); proxied.push_back(key); }
            Compiled sub; Builder bi(sub, 2, in, prefix + std::to_string(b.id) + "/"); bi.all(inner);
            for (auto& kv : sub.fieldTargets) { auto key = std::to_string(kv.first.first) + "." + kv.first.second; C.fieldTargets[{ b.id, key }] = kv.second; }
            for (auto& kv : sub.uiKey) C.uiKey[kv.first] = kv.second;
            // its ways in: the inlets inside, in order
            int nIn = 0; for (auto& x : d.ins) nIn += x.stereo() ? 2 : 1; int slot = 0;
            for (int i = 0; i < (int) d.ins.size(); ++i) { P.in[(size_t) i].push_back({ b.id, slot++ }); if (d.ins[(size_t) i].stereo()) P.in[(size_t) i].push_back({ b.id, slot++ }); }
            for (int k2 = 0; k2 < (int) proxied.size(); ++k2) P.field[proxied[(size_t) k2]] = { b.id, nIn + k2 };
            slot = 0; for (int i = 0; i < (int) d.outs.size(); ++i) { P.out[(size_t) i].push_back({ b.id, slot++ }); if (d.outs[(size_t) i].stereo()) P.out[(size_t) i].push_back({ b.id, slot++ }); }
            for (auto& kv : sub.problems) C.problems[b.id] = kv.second;
            D.boxes.push_back(pb); pins[b.id] = P; return;
        }
        if (b.type == "patch") {
            *pb.sub = px::PatchDoc::fromVar(juce::JSON::parse(b.text));
            for (int i = 0; i < (int) d.ins.size(); ++i) P.in[(size_t) i].push_back({ b.id, i });
            for (int i = 0; i < (int) d.outs.size(); ++i) P.out[(size_t) i].push_back({ b.id, i });
            D.boxes.push_back(pb); pins[b.id] = P; return;
        }
        if (!bypass) ids = parseBody(in, fill(d, b, false));
        // ways in (stereo: two), then the words moved by cables, then ways out - placed left to right
        int slot = 0, x = 0;
        auto idOf = [&](const std::string& s) { auto it = ids.find(s); return it == ids.end() ? -1 : it->second; };
        std::vector<int> passIn;
        for (int i = 0; i < (int) d.ins.size(); ++i) { auto& p = d.ins[(size_t) i]; bool snd = p.kind == Sound;
            for (int ch = 0; ch < (p.stereo() ? 2 : 1); ++ch) { int inl = in.add("obj", snd ? "inlet~" : "inlet", (float) (x += 10), 0);
                P.in[(size_t) i].push_back({ b.id, slot++ }); if (i == 0) passIn.push_back(inl);
                if (!bypass) { int t = idOf(ch ? p.boxR : p.box); if (t >= 0) in.connect(inl, 0, t, ch ? p.ioR : p.io); } } }
        int fx0 = 0;
        for (auto& f : d.fields) { if (!wiredFields.count(f.id)) continue; int inl = in.add("obj", "inlet", (float) (x += 10), 0); P.field[f.id] = { b.id, slot++ };
            if (!bypass) fieldChain(in, inl, f, ids, 3000 + 400 * fx0++); }
        (void) extraFieldIns;
        int oslot = 0;
        for (int i = 0; i < (int) d.outs.size(); ++i) { auto& p = d.outs[(size_t) i]; bool snd = p.kind == Sound;
            for (int ch = 0; ch < (p.stereo() ? 2 : 1); ++ch) { int ol = in.add("obj", snd ? "outlet~" : "outlet", (float) (x += 10), 9000);
                P.out[(size_t) i].push_back({ b.id, oslot++ });
                if (bypass) { if (i == 0 && !passIn.empty()) in.connect(passIn[(size_t) juce::jmin(ch, (int) passIn.size() - 1)], 0, ol, 0); }
                else { int t = idOf(ch ? p.boxR : p.box); if (t >= 0) in.connect(t, ch ? p.ioR : p.io, ol, 0); } } }
        if (!bypass) {
            // the words: said once when the block is made or changed
            int fi = 0;
            for (auto& f : d.fields) { if (f.bake || f.to.empty()) continue;
                auto v = valueOf(b, f); auto val = pxValue(f, v);
                auto h = juce::String::toHexString((juce::int64) std::hash<std::string>()(val + "|" + f.id)).substring(0, 10);
                int lb = in.add("obj", "loadbang " + h, (float) (4000 + 150 * fi), 6000);
                for (auto& t : f.to) { int tb = idOf(t.box); if (tb < 0) continue; int m = in.add("msg", resolve(t.pat, val), (float) (4000 + 150 * fi), 6040); in.connect(lb, 0, m, 0); in.connect(m, 0, tb, t.inlet);
                    C.fieldTargets[{ b.id, f.id }].push_back({ prefix + std::to_string(b.id) + "/" + std::to_string(tb), t.inlet, t.pat }); }
                ++fi; }
            if (!d.uiBox.empty()) { int u = idOf(d.uiBox); if (u >= 0) { C.uiKey[prefix + std::to_string(b.id)] = prefix + std::to_string(b.id) + "/" + std::to_string(u); if (!b.state.isVoid()) if (auto* bx = in.box(u)) bx->state = b.state; } }
            else if (b.type == "box") { int u = idOf("o"); if (u >= 0) C.uiKey[prefix + std::to_string(b.id)] = prefix + std::to_string(b.id) + "/" + std::to_string(u); }
        }
        D.boxes.push_back(pb); pins[b.id] = P;
    }
    // ways in and out of a plug-in (inside one), or of the whole patch (the inputs, the speakers, the keys)
    std::map<int, int> portSlot;   // block -> its first inlet / outlet
    void portBlock(const Block& b, const BlockDef& d, Pins& P) {
        bool in = d.port == "in"; Kind k = in ? d.outs[0].kind : d.ins[0].kind;
        if (mode == 2) {
            int base = portSlot[b.id]; int n = k == Sound ? 2 : 1;
            for (int ch = 0; ch < n; ++ch) {
                int id = D.add("obj", in ? (k == Sound ? "inlet~" : "inlet") : (k == Sound ? "outlet~" : "outlet"), (float) ((base + ch) * 20), in ? 0.0f : 9000.0f);
                (in ? P.out[0] : P.in[0]).push_back({ id, 0 }); }
            return;
        }
        if (k == Sound) { int id = D.add("obj", in ? "adc~ 1 2" : "dac~ 1 2", b.x, b.y); for (int ch = 0; ch < 2; ++ch) (in ? P.out[0] : P.in[0]).push_back({ id, ch }); return; }
        if (k == Notes) {
            if (in) { int ni = D.add("obj", "notein", b.x, b.y), pk = D.add("obj", "pack 0 0", b.x, b.y + 30); D.connect(ni, 0, pk, 0); D.connect(ni, 1, pk, 1); P.out[0].push_back({ pk, 0 }); }
            else { int up = D.add("obj", "unpack 0 0", b.x, b.y), no = D.add("obj", "noteout", b.x, b.y + 30); D.connect(up, 0, no, 0); D.connect(up, 1, no, 1); P.in[0].push_back({ up, 0 }); }
            return;
        }
        // movement in / out of the whole patch: nothing to connect
    }
    void all(const FluxDoc& d) {
        // the order of the ways in / out of a plug-in: top to bottom, then left to right
        { std::vector<std::pair<std::pair<float, float>, int>> ins, outs;
          for (auto& b : d.blocks) if (auto* df = defOf(b)) if (!df->port.empty()) (df->port == "in" ? ins : outs).push_back({ { b.y, b.x }, b.id });
          std::sort(ins.begin(), ins.end()); std::sort(outs.begin(), outs.end());
          int s = 0; for (auto& x : ins) { portSlot[x.second] = s; auto* df = defOf(*d.block(x.second)); s += df->outs[0].kind == Sound ? 2 : 1; }
          s = 0; for (auto& x : outs) { portSlot[x.second] = s; auto* df = defOf(*d.block(x.second)); s += df->ins[0].kind == Sound ? 2 : 1; } }
        for (auto& b : d.blocks) {
            auto* df = defOf(b); if (!df) { C.problems[b.id] = "This block is not known any more."; continue; }
            std::set<std::string> wf; for (auto& w : d.wires) if (w.b == b.id && !w.field.empty()) wf.insert(w.field);
            block(b, *df, wf, {});
        }
        // the cables (and the translators they need)
        int conv = 0;
        for (int wi = 0; wi < (int) d.wires.size(); ++wi) { auto& w = d.wires[(size_t) wi];
            auto* A = d.block(w.a); auto* B = d.block(w.b); if (!A || !B) continue; auto* da = defOf(*A); auto* db = defOf(*B); if (!da || !db) continue;
            if (w.out < 0 || w.out >= (int) da->outs.size()) continue;
            auto pa = pins.find(w.a), pb2 = pins.find(w.b); if (pa == pins.end() || pb2 == pins.end()) continue;
            auto& src = pa->second.out[(size_t) w.out]; if (src.empty()) continue;
            Kind from = da->outs[(size_t) w.out].kind;
            float mx = (A->x + B->x) * 0.5f + 40, my = (A->y + B->y) * 0.5f + 20;
            auto box = [&](const juce::String& t, float dy = 0) { int id = D.add("obj", t, mx, my + dy); ++conv; return id; };
            auto note = [&](const juce::String& n) { C.conv.push_back({ wi, n }); };
            if (!w.field.empty()) {
                auto fp = pb2->second.field.find(w.field); if (fp == pb2->second.field.end()) continue; Pin dst = fp->second;
                if (from == Sound) { int f = box("follow~ 1"); for (auto& s : src) D.connect(s.box, s.io, f, 0); D.connect(f, 0, dst.box, dst.io); note(converterName(Sound, Move, true)); }
                else if (from == Notes) { int u = box("unpack 0 0"), dv = box("/ 127.", 30); D.connect(src[0].box, src[0].io, u, 0); D.connect(u, 0, dv, 0); D.connect(dv, 0, dst.box, dst.io); note(converterName(Notes, Move, true)); }
                else D.connect(src[0].box, src[0].io, dst.box, dst.io);
                continue;
            }
            if (w.in < 0 || w.in >= (int) db->ins.size()) continue;
            auto& dst = pb2->second.in[(size_t) w.in]; if (dst.empty()) continue;
            Kind to = db->ins[(size_t) w.in].kind;
            // anything goes into anything, except sound into a box that takes numbers (and numbers into sound)
            if (from == Any && to != Sound) to = from == Any ? Any : to;
            if (to == Any && from != Sound) { D.connect(src[0].box, src[0].io, dst[0].box, dst[0].io); continue; }
            if (from == Any && to == Sound) from = Move;
            if (to == Any && from == Sound) to = Move;
            if (from == to) {
                if (from == Sound) { if (dst.size() == 2 && src.size() == 2) { D.connect(src[0].box, src[0].io, dst[0].box, dst[0].io); D.connect(src[1].box, src[1].io, dst[1].box, dst[1].io); }
                    else if (dst.size() == 2) { D.connect(src[0].box, src[0].io, dst[0].box, dst[0].io); D.connect(src[0].box, src[0].io, dst[1].box, dst[1].io); }
                    else for (auto& s : src) D.connect(s.box, s.io, dst[0].box, dst[0].io); }
                else D.connect(src[0].box, src[0].io, dst[0].box, dst[0].io);
                continue;
            }
            note(converterName(from, to, false));
            auto toDst = [&](int id, int o) { for (auto& t : dst) D.connect(id, o, t.box, t.io); };
            auto fromSrc = [&](int id) { for (auto& s : src) D.connect(s.box, s.io, id, 0); };
            if (from == Move && to == Sound) { int m = box("msg"), l = box("line~", 30); D.box(m)->kind = "msg"; D.box(m)->text = "$1 20"; D.connect(src[0].box, src[0].io, m, 0); D.connect(m, 0, l, 0); toDst(l, 0); }
            else if (from == Sound && to == Move) { int f = box("follow~ 1"); fromSrc(f); toDst(f, 0); }
            else if (from == Notes && to == Sound) { int s = box("synth~ soft 0.7 5 250"); D.connect(src[0].box, src[0].io, s, 0);
                if (dst.size() == 2) { D.connect(s, 0, dst[0].box, dst[0].io); D.connect(s, 1, dst[1].box, dst[1].io); } else { D.connect(s, 0, dst[0].box, dst[0].io); D.connect(s, 1, dst[0].box, dst[0].io); } }
            else if (from == Sound && to == Notes) { int f = box("fzero~"), n = box("hz2note 100", 30); fromSrc(f); D.connect(f, 0, n, 0); toDst(n, 0); }
            else if (from == Notes && to == Move) { int u = box("unpack 0 0"), dv = box("/ 127.", 30); D.connect(src[0].box, src[0].io, u, 0); D.connect(u, 0, dv, 0); toDst(dv, 0); }
            else if (from == Move && to == Notes) { int s = box("scale 0. 1. 48. 84."), r = box("round", 30), c = box("change", 60), n = box("nlen 8n 100", 90);
                D.connect(src[0].box, src[0].io, s, 0); D.connect(s, 0, r, 0); D.connect(r, 0, c, 0); D.connect(c, 0, n, 0); toDst(n, 0); }
        }
        (void) conv;
    }
};
} // namespace

juce::String converterName(Kind from, Kind to, bool intoWord) {
    if (intoWord) return from == Sound ? "follows the volume" : from == Notes ? "follows the note" : "";
    if (from == Move && to == Sound) return "smooth";
    if (from == Sound && to == Move) return "follows the volume";
    if (from == Notes && to == Sound) return "plays them with a soft sound";
    if (from == Sound && to == Notes) return "listens for the note";
    if (from == Notes && to == Move) return "how high the note is";
    if (from == Move && to == Notes) return "into notes";
    return "";
}

Compiled compile(const FluxDoc& d, bool device) {
    Compiled C; C.doc.name = d.name; C.doc.notes = d.notes;
    int maxId = 0; for (auto& b : d.blocks) maxId = juce::jmax(maxId, b.id);
    C.doc.nextId = juce::jmax(100000, maxId + 1);
    Builder bl(C, device ? 1 : 0, C.doc, ""); bl.all(d);
    // keep the blocks' boxes in id order (stable keys), the translators after
    return C;
}

std::vector<std::pair<std::string, px::Msg>> liveSet(const Compiled& c, const Block& b, const FieldDef& f, const juce::var& v) {
    std::vector<std::pair<std::string, px::Msg>> out;
    auto it = c.fieldTargets.find({ b.id, f.id }); if (it == c.fieldTargets.end()) return out;
    auto val = pxValue(f, v);
    for (auto& t : it->second) { auto atoms = px::parseAtoms(resolve(t.pat, val)); auto m = px::Msg::fromAtoms(atoms.data(), (int) atoms.size()); out.push_back({ t.key + "#" + std::to_string(t.inlet), m }); }
    return out;
}

// ================================================================== plug-ins
juce::var pluginToVar(const BlockDef& p) {
    auto* o = new juce::DynamicObject(); o->setProperty("name", juce::String(p.name)); o->setProperty("colour", juce::String(p.colour)); o->setProperty("help", juce::String(p.help));
    if (p.inner) o->setProperty("inner", p.inner->toVar());
    juce::Array<juce::var> fr; for (auto& f : p.front) { juce::Array<juce::var> a; a.add(f.first); a.add(juce::String(f.second)); fr.add(a); } o->setProperty("front", fr);
    return juce::var(o);
}
static void finishPlugin(BlockDef& d) {
    d.ins.clear(); d.outs.clear(); d.fields.clear(); d.id = "plugin"; d.shelf = "Mine";
    std::vector<std::pair<std::pair<float, float>, const Block*>> ins, outs;
    for (auto& b : d.inner->blocks) if (auto* df = defOf(b)) if (!df->port.empty()) (df->port == "in" ? ins : outs).push_back({ { b.y, b.x }, &b });
    std::sort(ins.begin(), ins.end(), [](auto& a, auto& b) { return a.first < b.first; }); std::sort(outs.begin(), outs.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& x : ins) { auto* df = defOf(*x.second); Kind k = df->outs[0].kind; d.in("p" + std::to_string(x.second->id), x.second->label.isNotEmpty() ? x.second->label.toStdString() : kindName(k), k, "#", 0, k == Sound ? "#" : "", 1); }
    for (auto& x : outs) { auto* df = defOf(*x.second); Kind k = df->ins[0].kind; d.out("p" + std::to_string(x.second->id), x.second->label.isNotEmpty() ? x.second->label.toStdString() : kindName(k), k, "#", 0, k == Sound ? "#" : "", 1); }
    std::string sentence; bool first = true;
    for (auto& fr : d.front) { auto* ib = d.inner->block(fr.first); if (!ib) continue; auto* idf = defOf(*ib); if (!idf) continue; auto* f = idf->field(fr.second); if (!f) continue;
        FieldDef c = *f; c.id = std::to_string(fr.first) + "." + fr.second; c.def = f->type == 's' ? 0.0 : (double) valueOf(*ib, *f); if (f->type == 's') c.defText = valueOf(*ib, *f).toString().toStdString();
        c.to.clear(); d.fields.push_back(c);
        sentence += (first ? "" : ", ") + (ib->label.isNotEmpty() ? ib->label.toStdString() : idf->name) + " " + std::string(fr.second) + " {" + c.id + "}"; first = false; }
    d.sentence = sentence.empty() ? std::string("Your plug-in (double-click to see inside)") : sentence; if (d.help.empty()) d.help = "Your own plug-in, made of blocks. Double-click it to see inside.";
}
std::shared_ptr<BlockDef> pluginFromVar(const juce::var& v) {
    auto d = std::make_shared<BlockDef>(); d->name = v["name"].toString().toStdString(); d->colour = v["colour"].toString().toStdString(); d->help = v["help"].toString().toStdString();
    d->inner = std::make_shared<FluxDoc>(FluxDoc::fromVar(v["inner"]));
    if (auto* a = v["front"].getArray()) for (auto& x : *a) if (auto* p = x.getArray()) if (p->size() >= 2) d->front.push_back({ (int) (*p)[0], (*p)[1].toString().toStdString() });
    finishPlugin(*d); return d;
}
std::shared_ptr<BlockDef> makePlugin(const FluxDoc& d, const std::vector<int>& sel, const juce::String& name, const std::vector<std::pair<int, std::string>>& front, const juce::String& colour, std::vector<Wire>* outer) {
    auto p = std::make_shared<BlockDef>(); p->name = name.toStdString(); p->colour = colour.toStdString(); p->front = front;
    auto inner = std::make_shared<FluxDoc>(); inner->name = name; std::set<int> S(sel.begin(), sel.end());
    float minX = 1e9f, minY = 1e9f, maxY = -1e9f; for (auto& b : d.blocks) if (S.count(b.id)) { inner->blocks.push_back(b); minX = juce::jmin(minX, b.x); minY = juce::jmin(minY, b.y); maxY = juce::jmax(maxY, b.y); }
    for (auto& b : inner->blocks) { b.x -= minX - 300; b.y -= minY - 60; }
    inner->nextId = d.nextId;
    std::map<std::pair<int, int>, int> inPort;      // (outside block, its outlet) -> the way in made for it
    std::map<std::pair<int, int>, int> outPort;     // (inside block, its outlet) -> the way out
    struct X { Wire w; int portBlock; bool in; }; std::vector<X> cross;
    for (auto& w : d.wires) {
        bool ia = S.count(w.a) > 0, ib = S.count(w.b) > 0;
        if (ia && ib) { inner->wires.push_back(w); continue; }
        if (!ia && !ib) continue;
        if (ib) {   // from outside in
            auto* A = d.block(w.a); auto* da = A ? defOf(*A) : nullptr; if (!da || w.out >= (int) da->outs.size()) continue;
            Kind k = da->outs[(size_t) w.out].kind; if (k == Any) k = Move;
            auto key = std::make_pair(w.a, w.out); int pid;
            if (!inPort.count(key)) { auto* B = inner->block(w.b); pid = inner->add(k == Sound ? "port.sound.in" : k == Notes ? "port.notes.in" : "port.move.in", 40, B ? B->y : 0); inPort[key] = pid; }
            pid = inPort[key]; inner->connect({ pid, 0, w.b, w.in, w.field }); cross.push_back({ w, pid, true });
        } else {    // from inside out
            auto* A = d.block(w.a); auto* da = A ? defOf(*A) : nullptr; if (!da || w.out >= (int) da->outs.size()) continue;
            Kind k = da->outs[(size_t) w.out].kind; if (k == Any) k = Move;
            auto key = std::make_pair(w.a, w.out); int pid;
            if (!outPort.count(key)) { pid = inner->add(k == Sound ? "port.sound.out" : k == Notes ? "port.notes.out" : "port.move.out", 900, A->y + 40); outPort[key] = pid; inner->connect({ w.a, w.out, pid, 0, {} }); }
            pid = outPort[key]; cross.push_back({ w, pid, false });
        }
    }
    p->inner = inner; finishPlugin(*p);
    if (outer) {
        auto idx = [&](bool in, int pid) { auto& v = in ? p->ins : p->outs; for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].id == "p" + std::to_string(pid)) return i; return -1; };
        for (auto& x : cross) { Wire w = x.w; if (x.in) { w.b = -1; w.in = idx(true, x.portBlock); w.field.clear(); } else { w.a = -1; w.out = idx(false, x.portBlock); }
            if ((x.in ? w.in : w.out) >= 0) { bool dup = false; for (auto& o : *outer) if (o.a == w.a && o.out == w.out && o.b == w.b && o.in == w.in && o.field == w.field) dup = true; if (!dup) outer->push_back(w); } }
    }
    return p;
}

// ================================================================== recipes and asking
FluxDoc recipeDoc(const Recipe& r) {
    FluxDoc d; d.name = r.name; std::map<int, int> ids;
    for (auto line : juce::StringArray::fromLines(r.text)) {
        line = line.trim(); auto t = juce::StringArray::fromTokens(line, " ", "\""); t.removeEmptyStrings(); if (t.isEmpty()) continue;
        if (t[0] == "b" && t.size() >= 5) {
            int id = d.add(t[2].toStdString(), (float) t[3].getIntValue(), (float) t[4].getIntValue()); ids[t[1].getIntValue()] = id; auto* b = d.block(id);
            if (t[2] == "box") { juce::String rest; for (int i = 5; i < t.size(); ++i) rest << (i > 5 ? " " : "") << t[i]; b->text = rest.unquoted(); continue; }
            auto* df = defOf(*b);
            for (int i = 5; i < t.size(); ++i) { auto kv = t[i]; int eq = kv.indexOfChar('='); if (eq < 0 || !df) continue; auto k = kv.substring(0, eq).toStdString(); auto val = kv.substring(eq + 1).unquoted().replace("_", " ");
                auto* f = df->field(k); if (!f) continue;
                if (f->type == 'c') { int pick = -1; for (int o = 0; o < (int) f->opts.size(); ++o) if (val == juce::String(f->opts[(size_t) o].value) || val == fxU8(f->opts[(size_t) o].label.c_str())) pick = o; if (pick < 0) pick = val.getIntValue(); b->f[k] = pick; }
                else if (f->type == 's') b->f[k] = val;
                else b->f[k] = val.getDoubleValue(); }
        } else if (t[0] == "w" && t.size() >= 5) {
            Wire w; w.a = ids[t[1].getIntValue()]; w.out = t[2].getIntValue(); w.b = ids[t[3].getIntValue()];
            if (t[4].containsOnly("0123456789")) w.in = t[4].getIntValue(); else w.field = t[4].toStdString();
            d.connect(w);
        }
    }
    return d;
}
static juce::StringArray wordsOf(const juce::String& s) {
    auto t = s.toLowerCase().replaceCharacters(fxU8(",.;:!?()[]\"'-/").toRawUTF8(), "              ");
    t = t.replace(fxU8("á"), "a").replace(fxU8("à"), "a").replace(fxU8("ã"), "a").replace(fxU8("â"), "a").replace(fxU8("é"), "e").replace(fxU8("ê"), "e").replace(fxU8("í"), "i").replace(fxU8("ó"), "o").replace(fxU8("õ"), "o").replace(fxU8("ô"), "o").replace(fxU8("ú"), "u").replace(fxU8("ç"), "c");
    juce::StringArray a; a.addTokens(t, " ", ""); a.removeEmptyStrings(); return a;
}
static double scoreText(const juce::StringArray& q, const juce::String& name, const juce::String& rest) {
    auto n = wordsOf(name), r = wordsOf(rest); double s = 0; int hit = 0;
    for (auto& w : q) { if (w.length() < 2) continue; double best = 0;
        for (auto& x : n) { if (x == w) best = juce::jmax(best, 6.0); else if (x.startsWith(w) && w.length() >= 3) best = juce::jmax(best, 4.0); else if (w.startsWith(x) && x.length() >= 4) best = juce::jmax(best, 3.0); }
        for (auto& x : r) { if (x == w) best = juce::jmax(best, 3.0); else if (x.startsWith(w) && w.length() >= 3) best = juce::jmax(best, 2.0); else if (w.startsWith(x) && x.length() >= 4) best = juce::jmax(best, 1.5); }
        if (best > 0) ++hit; s += best; }
    if (hit > 1) s *= 1.0 + 0.35 * (hit - 1);
    return s;
}
std::vector<Found> ask(const juce::String& what, int maxN) {
    std::vector<Found> v; auto q = wordsOf(what); if (q.isEmpty()) return v;
    auto& R = recipes(); for (int i = 0; i < (int) R.size(); ++i) { double s = scoreText(q, R[(size_t) i].name, R[(size_t) i].says + " " + R[(size_t) i].words); if (s > 0) { Found f; f.recipe = i; f.score = s * 1.15; v.push_back(f); } }
    for (auto& b : library()) { if (!b.port.empty()) continue; double s = scoreText(q, fxU8(b.name.c_str()), fxU8((b.words + " " + b.sentence + " " + b.help).c_str())); if (s > 0) { Found f; f.block = &b; f.score = s; v.push_back(f); } }
    for (auto& d : px::catalogue()) { if (!d.aliasOf.empty()) continue; double s = scoreText(q, juce::String(d.name).replace("~", ""), fxU8((d.words + " " + d.desc).c_str()));
        bool exact = false; for (auto& w : q) if (w == juce::String(d.name) || w + "~" == juce::String(d.name)) exact = true;
        if (exact) s += 8; if (s >= 4.5 || exact) { Found f; f.pxClass = d.name; f.score = s * 0.7; v.push_back(f); } }
    std::stable_sort(v.begin(), v.end(), [](const Found& a, const Found& b) { return a.score > b.score; });
    if ((int) v.size() > maxN) v.resize((size_t) maxN);
    return v;
}

}} // namespace arco::fx
