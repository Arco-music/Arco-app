#pragma once
// ARCO --patchercheck: the PATCHER tested the way Max is used - every object made and fed, then the rules that matter:
// right to left, hot and cold inlets, messages with $1, timing of metro / delay / line, signals that are the right
// frequency and the right delay, buffers recorded and played, subpatches, send / receive, edits that keep the state,
// a Max patch imported.
#include "../Engine/Patcher.h"
#include "../Engine/PatcherLib.h"
#include "../Engine/PatcherExamples.h"
#include <cstdio>

namespace arco {

inline int runPatcherCheck() {
    using namespace px;
    registerAll();
    int fails = 0, total = 0;
    auto say = [](const char* f, auto... a) { std::printf(f, a...); std::printf("\n"); std::fflush(stdout); };
    auto check = [&](bool ok, const juce::String& w, double v = 0) { ++total; if (!ok) ++fails; say("  %s  %-80s %.4f", ok ? "ok  " : "FAIL", w.toRawUTF8(), v); };
    const double sr = 48000.0;
    struct Rig {
        Runtime rt; PatchDoc d; std::vector<std::string> printed, errors;
        std::vector<float> outL, outR, inL, inR;
        Rig() { rt.prepare(48000.0); rt.onLine = [this](const Line& l) { (l.kind == 'e' ? errors : printed).push_back(l.text); }; }
        int add(const char* text, float x = 0, float y = 0) { return d.add("obj", text, x, y); }
        int msg(const char* text, float x = 0, float y = 0) { return d.add("msg", text, x, y); }
        void wire(int a, int o, int b, int i) { d.connect(a, o, b, i); }
        void load() { rt.setDoc(d, true); }
        void edit() { rt.setDoc(d, false); }
        void run(double ms, std::function<float(int)> in = nullptr) {
            int n = (int) (ms * 48.0); outL.assign((size_t) n, 0); outR.assign((size_t) n, 0); inL.assign((size_t) n, 0); inR.assign((size_t) n, 0);
            if (in) for (int i = 0; i < n; ++i) inL[(size_t) i] = inR[(size_t) i] = in(i);
            for (int s = 0; s < n; s += 64) { int m = juce::jmin(64, n - s); const float* ip[2] = { inL.data() + s, inR.data() + s }; float* op[2] = { outL.data() + s, outR.data() + s }; rt.process(ip, 2, op, 2, m); }
            rt.poll();
        }
        void click(int box, const Msg& m = Msg::bang(), int inlet = 0) { rt.post(box, inlet, m); }
        std::string last() const { return printed.empty() ? std::string() : printed.back(); }
        std::string all() const { std::string s; for (auto& p : printed) { if (!s.empty()) s += " | "; s += p; } return s; }
    };
    say("patcher check: %d objects in the catalogue", (int) catalogue().size());

    // ---- 1: every object is made from its example, fed messages and signal, and stays finite
    {
        int made = 0, bad = 0; juce::String badNames;
        for (auto& d : catalogue()) {
            if (!d.aliasOf.empty()) continue;
            Rig r; int o = r.add(d.example.empty() ? d.name.c_str() : d.example.c_str());
            auto io = boxIO(*r.d.box(o));
            int osc = r.add("cycle~ 220"), dac = r.add("dac~ 1 2"), pr = r.add("print x");
            for (int k = 0; k < io.ins; ++k) if ((io.sigIn >> k) & 1ULL) r.wire(osc, 0, o, k);
            for (int k = 0; k < io.outs; ++k) { if ((io.sigOut >> k) & 1ULL) r.wire(o, k, dac, k & 1); else r.wire(o, k, pr, 0); }
            r.load();
            if (!r.rt.boxError(o).isEmpty() && d.name != "p" && d.name != "patcher") { ++bad; badNames << d.name << " (" << r.rt.boxError(o) << ") "; continue; }
            r.run(20);
            const char* words[] = { "bang", "1", "0", "0.5", "60 100", "set 3", "clear", "start", "stop", "100 200 300", "reset", "next" };
            for (auto* w : words) { auto at = parseAtoms(w); for (int k = 0; k < juce::jmax(1, io.ins); ++k) r.click(o, Msg::fromAtoms(at.data(), (int) at.size()), k); r.run(5); }
            r.run(40);
            bool fin = true; for (float v : r.outL) fin &= std::isfinite(v) && std::abs(v) < 1000; for (float v : r.outR) fin &= std::isfinite(v) && std::abs(v) < 1000;
            if (!fin) { ++bad; badNames << d.name << " (not finite) "; } else ++made;
        }
        check(bad == 0, "every object is made, takes messages and stays finite (" + juce::String(made) + ") " + badNames.substring(0, 200), bad);
    }
    // ---- 2: messages
    { Rig r; int a = r.msg("5"), p = r.add("+ 10"), pr = r.add("print"); r.wire(a, 0, p, 0); r.wire(p, 0, pr, 0); r.load(); r.click(a); r.run(5);
      check(r.last() == "print: 15", "[5] -> [+ 10] -> 15", 0); }
    { Rig r; int a = r.msg("3"), b = r.msg("4"), p = r.add("* 2"), pr = r.add("print"); r.wire(a, 0, p, 1); r.wire(b, 0, p, 0); r.wire(p, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.printed.empty(), "a cold (right) inlet only remembers"); r.click(b); r.run(2); check(r.last() == "print: 12", "... the hot one says 4 x 3 = 12"); }
    { Rig r; int a = r.msg("2.5"), p = r.add("+ 1"), q = r.add("+ 1."), pr = r.add("print i", 0), pf = r.add("print f", 100); r.wire(a, 0, p, 0); r.wire(a, 0, q, 0); r.wire(p, 0, pr, 0); r.wire(q, 0, pf, 0);
      r.d.box(p)->x = 0; r.d.box(q)->x = 100; r.load(); r.click(a); r.run(2);
      check(r.all() == "f: 3.5 | i: 3", "[+ 1] makes whole numbers, [+ 1.] decimals - and the cord on the right fires first: " + juce::String(r.all())); }
    { Rig r; int a = r.msg("7"), t = r.add("t b i f"), pr = r.add("print"); r.wire(a, 0, t, 0); r.wire(t, 0, pr, 0); r.wire(t, 1, pr, 0); r.wire(t, 2, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.all() == "print: 7. | print: 7 | print: bang", "[t b i f]: right to left: " + juce::String(r.all())); }
    { Rig r; int a = r.msg("60 100"), m = r.msg("note $1 velocity $2, done"), pr = r.add("print"); r.wire(a, 0, m, 0); r.wire(m, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.all() == "print: note 60 velocity 100 | print: done", "a message with $1 $2 and a comma: " + juce::String(r.all())); }
    { Rig r; int a = r.msg("; hello 42"), rc = r.add("r hello"), pr = r.add("print"); r.wire(rc, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.last() == "print: 42", "\"; hello 42\" reaches [r hello]"); }
    { Rig r; int a = r.msg("freq 440"), b = r.msg("amp 0.5"), c = r.msg("other 1"), rt = r.add("route freq amp"), p0 = r.add("print f"), p1 = r.add("print a"), p2 = r.add("print x");
      for (int x : { a, b, c }) r.wire(x, 0, rt, 0); r.wire(rt, 0, p0, 0); r.wire(rt, 1, p1, 0); r.wire(rt, 2, p2, 0); r.load(); r.click(a); r.click(b); r.click(c); r.run(2);
      check(r.all() == "f: 440 | a: 0.5 | x: other 1", "[route freq amp]: " + juce::String(r.all())); }
    { Rig r; int a = r.msg("1"), b = r.msg("9"), s = r.add("sel 1 5"), p0 = r.add("print one"), p2 = r.add("print else"); r.wire(a, 0, s, 0); r.wire(b, 0, s, 0); r.wire(s, 0, p0, 0); r.wire(s, 2, p2, 0); r.load(); r.click(a); r.click(b); r.run(2);
      check(r.all() == "one: bang | else: 9", "[sel 1 5]: " + juce::String(r.all())); }
    { Rig r; int a = r.msg("1"), b = r.msg("2"), pk = r.add("pack 0 0"), pr = r.add("print"); r.wire(a, 0, pk, 1); r.wire(b, 0, pk, 0); r.wire(pk, 0, pr, 0); r.load(); r.click(a); r.click(b); r.run(2);
      check(r.last() == "print: 2 1", "[pack 0 0]: " + juce::String(r.last())); }
    { Rig r; int b = r.add("button"), c = r.add("counter 0 3"), pr = r.add("print"); r.wire(b, 0, c, 0); r.wire(c, 0, pr, 0); r.load(); for (int i = 0; i < 6; ++i) r.click(b); r.run(2);
      check(r.all() == "print: 0 | print: 1 | print: 2 | print: 3 | print: 0 | print: 1", "[counter 0 3] goes round: " + juce::String(r.all())); }
    { Rig r; int a = r.msg("3 4"), e = r.add("expr sqrt($f1 * $f1 + $f2 * $f2)"), pr = r.add("print"); r.wire(a, 0, e, 0); r.wire(e, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.last() == "print: 5.", "[expr sqrt($f1*$f1+$f2*$f2)] of 3 4 = 5: " + juce::String(r.last())); }
    { Rig r; int a = r.msg("70"), e = r.add("if $i1 > 64 then 1 else 0"), pr = r.add("print"); r.wire(a, 0, e, 0); r.wire(e, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.last() == "print: 1", "[if $i1 > 64 then 1 else 0]"); }
    { Rig r; int a = r.msg("3 1 2"), z1 = r.add("zl sort"), z2 = r.add("zl len"), z3 = r.add("zl rev"), pr = r.add("print"); for (int z : { z1, z2, z3 }) { r.wire(a, 0, z, 0); r.wire(z, 0, pr, 0); }
      r.d.box(z1)->x = 200; r.d.box(z2)->x = 100; r.d.box(z3)->x = 0; r.load(); r.click(a); r.run(2);
      check(r.all() == "print: 1 2 3 | print: 3 | print: 2 1 3", "[zl sort] [zl len] [zl rev]: " + juce::String(r.all())); }
    { Rig r; int st = r.msg("1 60 64 67"), ask = r.msg("1"), co = r.add("coll"), pr = r.add("print"); r.wire(st, 0, co, 0); r.wire(ask, 0, co, 0); r.wire(co, 0, pr, 0); r.load(); r.click(st); r.click(ask); r.run(2);
      check(r.last() == "print: 60 64 67", "[coll] keeps \"1 60 64 67\" and gives it back"); }
    { Rig r; int a = r.msg("hello 7"), s = r.add("s chan"), rc = r.add("r chan"), pr = r.add("print"); r.wire(a, 0, s, 0); r.wire(rc, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.last() == "print: hello 7", "[s chan] -> [r chan]"); }
    { Rig r; int a = r.msg("5"), v1 = r.add("v speed"), b = r.add("button"), v2 = r.add("v speed"), pr = r.add("print"); r.wire(a, 0, v1, 0); r.wire(b, 0, v2, 0); r.wire(v2, 0, pr, 0); r.load(); r.click(a); r.click(b); r.run(2);
      check(r.last() == "print: 5", "[v speed] shares a value"); }
    { Rig r; int a = r.msg("60"), sc = r.add("toscale minor 0"), ch = r.add("chord min"), m = r.add("mtof"), pr = r.add("print"); r.wire(a, 0, sc, 0); r.wire(sc, 0, ch, 0); r.wire(ch, 0, pr, 0); r.wire(a, 0, m, 0); r.wire(m, 0, pr, 0);
      r.d.box(sc)->x = 0; r.d.box(m)->x = 100; r.load(); r.click(a); r.run(2);
      check(r.all() == "print: 261.626 | print: 60 63 67", "[toscale] [chord min] [mtof]: " + juce::String(r.all())); }
    { Rig r; int a = r.add("button"), l = r.add("uzi 3"), pr = r.add("print"); r.wire(a, 0, l, 0); r.wire(l, 2, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.all() == "print: 1 | print: 2 | print: 3", "[uzi 3] counts 1 2 3"); }
    { Rig r; int a = r.msg("1"), loop = r.add("+ 1"), pr = r.add("print"); r.wire(a, 0, loop, 0); r.wire(loop, 0, loop, 0); juce::ignoreUnused(pr); r.load(); r.click(a); r.run(5);
      check(!r.errors.empty() && juce::String(r.errors.back()).contains("stack overflow"), "a message loop stops with a plain warning, the app goes on"); }
    // ---- 3: time
    { Rig r; int on = r.msg("1"), m = r.add("metro 100"), c = r.add("counter"), pr = r.add("print"); r.wire(on, 0, m, 0); r.wire(m, 0, c, 0); r.wire(c, 0, pr, 0); r.load(); r.click(on); r.run(1005);
      check(r.printed.size() == 11, "[metro 100] for 1 s: 11 bangs", (double) r.printed.size()); }
    { Rig r; r.rt.bpm = 120; int on = r.msg("1"), m = r.add("metro 8n"), pr = r.add("print"); r.wire(on, 0, m, 0); r.wire(m, 0, pr, 0); r.load(); r.click(on); r.run(1005);
      check(r.printed.size() == 5, "[metro 8n] at 120 bpm: every 250 ms", (double) r.printed.size()); }
    { Rig r; int b = r.add("button"), d = r.add("delay 300"), t = r.add("timer"), pr = r.add("print"); r.wire(b, 0, d, 0); r.wire(b, 0, t, 0); r.wire(d, 0, t, 1); r.wire(t, 0, pr, 0); r.load(); r.click(b); r.run(400);
      double ms = std::atof(r.last().substr(7).c_str()); check(std::abs(ms - 300) < 2, "[delay 300] measured with [timer]", ms); }
    { Rig r; int a = r.msg("100 1000"), l = r.add("line 0 100"), pr = r.add("print"); r.wire(a, 0, l, 0); r.wire(l, 0, pr, 0); r.load(); r.click(a); r.run(1100);
      check(r.printed.size() >= 10 && r.last() == "print: 100", "[line]: 0 to 100 in 1 s, in steps", (double) r.printed.size()); }
    { Rig r; r.rt.bpm = 120; r.rt.running = true; int bt = r.add("beat 4n"), pr = r.add("print"); r.wire(bt, 0, pr, 0); r.load(); r.run(2010);
      check(r.printed.size() == 4, "[beat 4n] follows the app's clock: 4 bangs in 2 s at 120", (double) r.printed.size()); }
    // ---- 4: signals
    { Rig r; int o = r.add("cycle~ 441"), g = r.add("*~ 0.5"), d = r.add("dac~ 1 2"); r.wire(o, 0, g, 0); r.wire(g, 0, d, 0); r.wire(g, 0, d, 1); r.load(); r.run(1000);
      int z = 0; float pk = 0; for (size_t i = 1; i < r.outL.size(); ++i) { if (r.outL[i - 1] < 0 && r.outL[i] >= 0) ++z; pk = juce::jmax(pk, std::abs(r.outL[i])); }
      check(std::abs(z - 441) <= 1 && std::abs(pk - 0.5f) < 0.01f, "[cycle~ 441] -> [*~ 0.5] -> [dac~]: 441 Hz at half", z); }
    { Rig r; int a = r.add("adc~ 1"), d = r.add("dac~ 1"); r.wire(a, 0, d, 0); r.load(); r.run(50, [](int i) { return (float) std::sin(i * 0.01); });
      double e = 0; for (size_t i = 0; i < r.outL.size(); ++i) e += std::abs(r.outL[i] - r.inL[i]); check(e < 1e-3, "[adc~ 1] -> [dac~ 1]: the input goes straight through", e); }
    { Rig r; int a = r.add("adc~ 1"), ti = r.add("tapin~ 1000"), to = r.add("tapout~ 100"), d = r.add("dac~ 1"); r.wire(a, 0, ti, 0); r.wire(ti, 0, to, 0); r.wire(to, 0, d, 0); r.load();
      r.run(300, [](int i) { return i == 480 ? 1.0f : 0.0f; });
      int at = -1; for (size_t i = 0; i < r.outL.size(); ++i) if (r.outL[i] > 0.5f) { at = (int) i; break; }
      check(at == 480 + 4800, "[tapin~] -> [tapout~ 100]: a click comes out 100 ms later", at); }
    { Rig r; int a = r.msg("1 100"), l = r.add("line~"), d = r.add("dac~ 1"); r.wire(a, 0, l, 0); r.wire(l, 0, d, 0); r.load(); r.click(a); r.run(200);
      check(std::abs(r.outL[2400] - 0.5f) < 0.02f && std::abs(r.outL[7000] - 1.0f) < 1e-4f, "[line~]: \"1 100\" is half way at 50 ms, there at 100", r.outL[2400]); }
    { Rig r; int o = r.add("cycle~ 300"), s = r.add("send~ bus"), rc = r.add("receive~ bus"), d = r.add("dac~ 1"); r.wire(o, 0, s, 0); r.wire(rc, 0, d, 0); r.d.box(rc)->x = -100; r.load(); r.run(100);
      float pk = 0; for (float v : r.outL) pk = juce::jmax(pk, std::abs(v)); check(pk > 0.9f, "[send~ bus] -> [receive~ bus] (wherever they are)", pk); }
    { Rig r; int b = r.add("buffer~ snd 500"), on = r.msg("1"), a = r.add("adc~ 1"), rec = r.add("record~ snd"), go = r.msg("start"), pl = r.add("play~ snd"), d = r.add("dac~ 1");
      juce::ignoreUnused(b); r.wire(a, 0, rec, 0); r.wire(on, 0, rec, 0); r.wire(go, 0, pl, 0); r.wire(pl, 0, d, 0); r.load(); r.run(10);
      r.click(on); r.run(600, [](int i) { return 0.25f * (float) std::sin(i * 0.05); });
      r.click(go); r.run(300);
      float pk = 0; for (size_t i = 0; i < 9000; ++i) pk = juce::jmax(pk, std::abs(r.outL[i])); check(std::abs(pk - 0.25f) < 0.02f, "[record~] into [buffer~], [play~] plays it back", pk); }
    { Rig r; int o = r.add("noise~"), f = r.add("filter~ lowpass 200 0.7"), d = r.add("dac~ 1 2"); r.wire(o, 0, f, 0); r.wire(f, 0, d, 0); r.wire(o, 0, d, 1); r.load(); r.run(500);
      double hl = 0, hn = 0; for (size_t i = 1; i < r.outL.size(); ++i) { hl += std::abs(r.outL[i] - r.outL[i - 1]); hn += std::abs(r.outR[i] - r.outR[i - 1]); }
      check(hl < hn * 0.1, "[filter~ lowpass 200]: noise gets dark", hl / hn); }
    { Rig r; int o = r.add("cycle~ 220"), rv = r.add("reverb~ 0.9 0.3 0.5"), ech = r.add("echo~ 8n 0.5 0.4"), d = r.add("dac~ 1 2"); r.wire(o, 0, ech, 0); r.wire(ech, 0, rv, 0); r.wire(rv, 0, d, 0); r.wire(rv, 1, d, 1); r.load(); r.run(1000);
      bool fin = true; float pk = 0; for (float v : r.outL) { fin &= std::isfinite(v); pk = juce::jmax(pk, std::abs(v)); } check(fin && pk > 0.1f && pk < 4, "[echo~] -> [reverb~]: sound, finite", pk); }
    // ---- 5: structure
    { Rig r; int a = r.msg("4"), sub = r.d.add("sub", "p double", 0, 0), pr = r.add("print"); auto& s = *r.d.box(sub)->sub;
      int in = s.add("obj", "inlet", 0, 0), m = s.add("obj", "* 2", 0, 40), out = s.add("obj", "outlet", 0, 80); s.connect(in, 0, m, 0); s.connect(m, 0, out, 0);
      r.wire(a, 0, sub, 0); r.wire(sub, 0, pr, 0); r.load(); r.click(a); r.run(2);
      check(r.last() == "print: 8", "a subpatch [p double] with an inlet and an outlet"); }
    { Rig r; int on = r.msg("1"), m = r.add("metro 50"), c = r.add("counter"), pr = r.add("print"); r.wire(on, 0, m, 0); r.wire(m, 0, c, 0); r.wire(c, 0, pr, 0); r.load(); r.click(on); r.run(260);
      size_t before = r.printed.size(); int extra = r.add("+ 1", 300, 0); juce::ignoreUnused(extra); r.edit(); r.run(250);
      check(before >= 5 && r.printed.size() >= before + 4 && r.last() != "print: 0", "an edit while it runs keeps the metro going and the count", (double) r.printed.size()); }
    { Rig r; int lb = r.add("loadbang"), pr = r.add("print"); r.wire(lb, 0, pr, 0); r.load(); r.run(10); r.add("+ 1"); r.edit(); r.run(10);
      check(r.printed.size() == 1, "[loadbang] once when the patch opens (not on every edit)", (double) r.printed.size()); }
    { Rig r; int x = r.add("cylce~ 440"); r.load(); check(r.rt.boxError(x).contains("cycle~"), "a word typed wrong: \"" + r.rt.boxError(x) + "\""); }
    { Rig r; int o = r.add("cycle~ 440"), p = r.add("print"); r.wire(o, 0, p, 0); r.load(); check(r.rt.boxError(p).isNotEmpty(), "a sound cord into a number inlet is refused, with a reason"); }
    // ---- 6: files
    {
        const char* maxpat = R"({"patcher":{"boxes":[{"box":{"id":"obj-1","maxclass":"newobj","text":"cycle~ 330","patching_rect":[10,10,80,22]}},
            {"box":{"id":"obj-2","maxclass":"newobj","text":"*~ 0.3","patching_rect":[10,60,50,22]}},{"box":{"id":"obj-3","maxclass":"ezdac~","patching_rect":[10,110,45,45]}},
            {"box":{"id":"obj-4","maxclass":"message","text":"hello","patching_rect":[120,10,50,22]}},{"box":{"id":"obj-5","maxclass":"comment","text":"a test","patching_rect":[120,60,50,22]}}],
            "lines":[{"patchline":{"source":["obj-1",0],"destination":["obj-2",0]}},{"patchline":{"source":["obj-2",0],"destination":["obj-3",0]}},{"patchline":{"source":["obj-2",0],"destination":["obj-3",1]}}]}})";
        auto d = PatchDoc::fromVar(juce::JSON::parse(juce::String(maxpat)));
        Rig r; r.d = d; r.load(); r.run(500);
        int z = 0; for (size_t i = 1; i < r.outL.size(); ++i) if (r.outL[i - 1] < 0 && r.outL[i] >= 0) ++z;
        check(d.boxes.size() == 5 && d.lines.size() == 3 && std::abs(z - 165) <= 1, "a Max patch (.maxpat) opens and plays (330 Hz)", z);
        auto back = PatchDoc::fromVar(juce::JSON::parse(juce::JSON::toString(d.toVar())));
        check(back.boxes.size() == 5 && back.lines.size() == 3, "saved and opened again: the same boxes and cords");
        auto mx = PatchDoc::fromMaxpat(d.toMaxpat());
        check(mx.boxes.size() == 5 && mx.lines.size() == 3, "written as a Max patch and read back");
    }
    { Rig r; int s = r.add("slider 0 127"), m = r.add("multislider 4 0 127"); r.load(); r.click(s, Msg::i(99)); r.click(m, parseAtoms("1 2 3 4").size() ? Msg::fromAtoms(parseAtoms("1 2 3 4").data(), 4) : Msg::bang()); r.run(5);
      auto vs = r.rt.objOf(s)->keep(); auto vm = r.rt.objOf(m)->keep();
      check((double) vs == 99 && vm.getArray() && vm.getArray()->size() == 4, "a slider and a multislider keep their values with the patch"); }
    // ---- 7: search
    { auto h = searchDefs("echo"); check(!h.empty() && (h[0]->name == "echo~" || h[0]->name == "tapin~" || h[0]->name == "pipe"), juce::String("search \"echo\" -> ") + (h.empty() ? "nothing" : h[0]->name.c_str())); }
    { auto h = searchDefs("random"); check(!h.empty() && h[0]->name == "random", juce::String("search \"random\" -> ") + (h.empty() ? "nothing" : h[0]->name.c_str())); }
    { auto h = searchDefs("microphone"); check(!h.empty() && (h[0]->name == "adc~" || h[0]->name == "ezadc~"), juce::String("search \"microphone\" -> ") + (h.empty() ? "nothing" : h[0]->name.c_str())); }
    { auto h = searchDefs("make a beat in time"); bool has = false; for (size_t i = 0; i < h.size() && i < 5; ++i) has |= h[i]->name == "beat" || h[i]->name == "metro"; check(has, "search \"make a beat in time\" finds [beat] / [metro]"); }
    { auto s = suggest("metor"); check(!s.empty() && s[0] == "metro", "\"metor\" -> did you mean metro?"); }
    {   // every ready-made patch opens without a problem, and the ones that sound make sound
        juce::String bad; int n = 0;
        for (auto& ex : examples()) { Rig r; r.d = ex.doc; r.load(); ++n;
            for (auto& b : ex.doc.boxes) if (r.rt.boxError(b.id).isNotEmpty()) bad << ex.name << ": " << b.text << " (" << r.rt.boxError(b.id) << ")  ";
            // turn it on: toggles 1, buttons bang, then listen
            for (auto& b : ex.doc.boxes) { auto c = clsOf(b.text); if (c == "toggle") r.click(b.id, Msg::i(1)); if (c == "button") r.click(b.id); }
            r.run(800, [](int i) { return 0.3f * (float) std::sin(i * 0.03); });
            for (auto& b : ex.doc.boxes) { auto c = clsOf(b.text); if (c == "button") r.click(b.id); if (c == "msg") {} }
            r.run(600, [](int i) { return 0.3f * (float) std::sin(i * 0.03); });
            bool fin = true; float pk = 0; for (float v : r.outL) { fin &= std::isfinite(v); pk = juce::jmax(pk, std::abs(v)); }
            if (!fin || pk > 4) bad << ex.name << " (level " << pk << ")  ";
        }
        check(bad.isEmpty(), "the " + juce::String(n) + " ready-made patches open cleanly and stay in range " + bad.substring(0, 300));
    }
    say("patcher check: %d of %d failed", fails, total);
    return fails;
}

} // namespace arco
