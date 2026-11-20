#pragma once
// ARCO --fluxocheck: FLUXO tested - every block made and played, every recipe built and heard, the translators on the
// cables, words changed while it plays, plug-ins made from blocks (and as STUDIO devices), saving, asking.
#include "../Engine/Fluxo.h"
#include "../Engine/PatcherLib.h"
#include <cstdio>

namespace arco {

inline int runFluxoCheck() {
    using namespace fx;
    px::registerAll();
    int fails = 0, total = 0;
    auto say = [](const char* f, auto... a) { std::printf(f, a...); std::printf("\n"); std::fflush(stdout); };
    auto check = [&](bool ok, const juce::String& w, double v = 0) { ++total; if (!ok) ++fails; say("  %s  %-86s %.4f", ok ? "ok  " : "FAIL", w.toRawUTF8(), v); };
    struct Rig {
        px::Runtime rt; Compiled c; std::vector<std::string> printed, errors; std::vector<float> L, R; double t = 0;
        Rig() { rt.prepare(48000.0); rt.onLine = [this](const px::Line& l) { (l.kind == 'e' ? errors : printed).push_back(l.text); }; }
        void load(const FluxDoc& d, bool fresh = true, bool device = false) { c = compile(d, device); rt.setDoc(c.doc, fresh); }
        double run(double ms, int input = 1) {      // input: 0 silence, 1 a sung A (220 Hz, swelling), 2 noise bursts
            int n = (int) (ms * 48.0); L.assign((size_t) n, 0); R.assign((size_t) n, 0); std::vector<float> in((size_t) n);
            for (int i = 0; i < n; ++i) { double s = (t + i) / 48000.0; in[(size_t) i] = input == 3 ? (float) (0.3 * std::sin(6.283185307 * 220 * s)) : input == 1 ? (float) (0.4 * std::sin(6.283185307 * 220 * s) * (0.6 + 0.4 * std::sin(6.283 * 0.7 * s))) : input == 2 ? (float) (((i * 7919) % 2000) / 1000.0 - 1.0) * (std::fmod(s, 0.5) < 0.1 ? 0.6f : 0.02f) : 0.0f; }
            for (int s = 0; s < n; s += 64) { int m = juce::jmin(64, n - s); const float* ip[6] = { in.data() + s, in.data() + s, in.data() + s, in.data() + s, in.data() + s, in.data() + s }; float* op[2] = { L.data() + s, R.data() + s }; rt.bpm = 120; rt.running = false; rt.process(ip, 6, op, 2, m); }
            t += n; rt.poll();
            double e = 0; for (int i = 0; i < n; ++i) e += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; return std::sqrt(e / juce::jmax(1, 2 * n));
        }
        bool finite() const { for (float v : L) if (!std::isfinite(v) || std::abs(v) > 50) return false; for (float v : R) if (!std::isfinite(v) || std::abs(v) > 50) return false; return true; }
        juce::String errs() const { juce::String s; for (auto& kv : rt.errs) if (kv.second.isNotEmpty()) s << kv.first << ": " << kv.second << "; "; for (auto& kv : c.problems) s << kv.first << ": " << kv.second << "; "; return s; }
        void key(int note, int vel) { rt.midiIn(vel > 0 ? juce::MidiMessage::noteOn(1, note, (juce::uint8) vel) : juce::MidiMessage::noteOff(1, note)); }
    };
    say("fluxo check: %d blocks on %d shelves, %d recipes", (int) library().size(), (int) shelves().size(), (int) recipes().size());

    // ---- 1: every block, fed what it takes, played into the speakers
    {
        int ok = 0; juce::String bad;
        for (auto& bd : library()) {
            if (bd.id.rfind("max.", 0) == 0) continue;
            FluxDoc d; int b = d.add(bd.id, 300, 300);
            int src = d.add("port.sound.in", 0, 0), clock = d.add("time.every", 100, 0), notes = d.add("notes.random", 200, 100), spk = d.add("out.speakers", 300, 600);
            d.connect({ clock, 0, notes, 0, {} });
            for (int i = 0; i < (int) bd.ins.size(); ++i) { auto k = bd.ins[(size_t) i].kind;
                if (k == Sound) d.connect({ src, 0, b, i, {} }); else if (k == Notes) d.connect({ notes, 0, b, i, {} }); else d.connect({ clock, 0, b, i, {} }); }
            for (int i = 0; i < (int) bd.outs.size(); ++i) if (bd.outs[(size_t) i].kind == Sound) d.connect({ b, i, spk, 0, {} });
            Rig r; r.load(d); r.run(900, 1);
            auto e = r.errs(); bool fin = r.finite();
            if (e.isNotEmpty() || !fin) bad << bd.id << " [" << (fin ? e.substring(0, 160) : juce::String("not finite")) << "] "; else ++ok;
        }
        check(bad.isEmpty(), "every block is made, fed and played (" + juce::String(ok) + ") " + bad.substring(0, 600), bad.isEmpty() ? 0 : 1);
    }
    // ---- 2: sounds that should sound
    {
        auto hear = [&](const char* type, int input, std::function<void(Rig&)> drive = nullptr) {
            FluxDoc d; int src = d.add("port.sound.in", 0, 0), b = d.add(type, 0, 100), spk = d.add("out.speakers", 0, 300);
            auto* bd = findBlock(type); if (!bd->ins.empty() && bd->ins[0].kind == Sound) d.connect({ src, 0, b, 0, {} });
            if (!bd->ins.empty() && bd->ins[0].kind == Notes) { int k = d.add("listen.keys", 0, -100); d.connect({ k, 0, b, 0, {} }); }
            if (!bd->ins.empty() && bd->ins[0].kind != Sound && bd->ins[0].kind != Notes) { int c = d.add("time.every", 0, -100); d.connect({ c, 0, b, 0, {} }); }
            d.connect({ b, 0, spk, 0, {} });
            Rig r; r.load(d); r.run(100, input); if (drive) drive(r); return r.run(1500, input);
        };
        check(hear("change.echo", 1) > 0.02, "an echo on a sung note is heard", hear("change.echo", 1));
        double syn = hear("sound.synth", 0, [](Rig& r) { r.key(60, 100); r.key(64, 100); r.key(67, 100); });
        check(syn > 0.01, "the synth plays a chord from the keys", syn);
        check(hear("sound.beat", 0) > 0.01, "the beat maker plays on its own", hear("sound.beat", 0));
        check(hear("sound.drum", 0) > 0.005, "a drum on every tap", hear("sound.drum", 0));
        check(hear("sound.noise", 0) > 0.01, "noise is heard", hear("sound.noise", 0));
        double pl = hear("sound.pluck", 0, [](Rig& r) { r.key(57, 110); });
        check(pl > 0.002, "a plucked string rings", pl);
        check(hear("change.room", 1) > 0.02 && hear("change.filter", 1) > 0.01 && hear("change.drive", 1) > 0.02, "room, filter and distortion pass the sound", hear("change.room", 1));
    }
    // ---- 3: every recipe builds, plays and stays clean
    {
        juce::String bad; int heard = 0, n = 0;
        for (auto& rc : recipes()) {
            if (std::getenv("FLUXOVERBOSE")) say("    recipe %s", rc.name.toRawUTF8());
            auto d = recipeDoc(rc); Rig r; r.load(d);
            r.run(200, 1); r.key(57, 100); r.key(60, 90); r.key(64, 90); double e = r.run(2500, 1); r.key(57, 0); r.key(60, 0); r.key(64, 0); r.run(300, 1);
            auto er = r.errs(); if (er.isNotEmpty() || !r.finite() || d.blocks.empty()) bad << rc.name << " [" << er.substring(0, 140) << "] ";
            bool silent = rc.name.contains("Watch") || rc.name.contains("STUDIO") || rc.name.contains("Grain");   // (grains: once you record)
            if (!silent) { ++n; if (e > 0.003) ++heard; else bad << rc.name << " (silent " << e << ") "; }
        }
        check(bad.isEmpty(), "every recipe builds and plays (" + juce::String(heard) + " of " + juce::String(n) + " heard) " + bad.substring(0, 500), bad.isEmpty() ? 0 : 1);
    }
    // ---- 4: the translators on the cables
    {
        FluxDoc d; int k = d.add("listen.keys", 0, 0), spk = d.add("out.speakers", 0, 200); d.connect({ k, 0, spk, 0, {} });
        Rig r; r.load(d); r.run(50, 0); r.key(69, 100); double e = r.run(600, 0);
        check(e > 0.01 && r.c.conv.size() == 1, "notes plugged into sound: a soft synth plays them (" + (r.c.conv.empty() ? juce::String() : r.c.conv[0].name) + ")", e);
        FluxDoc d2; int src = d2.add("port.sound.in", 0, 0), f = d2.add("change.filter", 0, 100), sp2 = d2.add("out.speakers", 0, 300); d2.connect({ src, 0, f, 0, {} }); d2.connect({ src, 0, f, 0, "hz" }); d2.connect({ f, 0, sp2, 0, {} });
        Rig r2; r2.load(d2); double e2 = r2.run(1200, 1);
        check(e2 > 0.005 && r2.errs().isEmpty() && r2.c.conv.size() == 1 && r2.c.conv[0].name == "follows the volume", "sound plugged into a word: it follows the volume (auto-wah)", e2);
        FluxDoc d3; int src3 = d3.add("port.sound.in", 0, 0), pt = d3.add("listen.pitch", 0, 100), syn = d3.add("sound.synth", 0, 200), sp3 = d3.add("out.speakers", 0, 300);
        d3.connect({ src3, 0, pt, 0, {} }); d3.connect({ pt, 0, syn, 0, {} }); d3.connect({ syn, 0, sp3, 0, {} });
        Rig r3; r3.load(d3); r3.run(400, 1); double e3 = r3.run(1500, 1);
        check(e3 > 0.005, "a sung note is heard and played by a synth", e3);
        FluxDoc d4; int w = d4.add("move.wave", 0, 0), tone = d4.add("sound.tone", 0, 100), sp4 = d4.add("out.speakers", 0, 200); d4.connect({ w, 0, tone, 0, "hz" }); d4.connect({ tone, 0, sp4, 0, {} });
        Rig r4; r4.load(d4); double e4 = r4.run(1500, 0);
        check(e4 > 0.01 && r4.errs().isEmpty(), "a slow wave moves the pitch of a tone", e4);
    }
    // ---- 5: words changed while it plays (no rebuild), and with a rebuild
    {
        FluxDoc d; int src = d.add("port.sound.in", 0, 0), v = d.add("change.volume", 0, 100), sp = d.add("out.speakers", 0, 200); d.connect({ src, 0, v, 0, {} }); d.connect({ v, 0, sp, 0, {} });
        Rig r; r.load(d); r.run(300, 3); double a = r.run(500, 3);
        auto* bl = d.block(v); auto* f = defOf(*bl)->field("db");
        for (auto& m : liveSet(r.c, *bl, *f, -30.0)) { auto k = m.first.substr(0, m.first.find('#')); int inl = std::stoi(m.first.substr(m.first.find('#') + 1)); r.rt.postKey(k, inl, m.second); }
        r.run(300, 3); double b = r.run(500, 3);
        bl->f["db"] = -30.0; r.load(d, false); r.run(200, 3);      // (the screen writes the word into the patch too)
        check(b < a * 0.1 && b > 0, "a word changed while playing (volume to -30 dB) changes the sound at once", b / juce::jmax(1e-9, a));
        bl->f["db"] = 0.0; r.load(d, false); r.run(300, 3); double c2 = r.run(500, 3);
        check(std::abs(c2 - a) < a * 0.2, "and back, by rebuilding the patch (the new value is said once)", c2 / juce::jmax(1e-9, a));
        FluxDoc e = d; e.block(v)->bypass = true; Rig r2; r2.load(e); r2.run(300, 3); double byp = r2.run(500, 3);
        check(byp > 0.05, "hear it without: a block bypassed passes the sound", byp);
    }
    // ---- 6: plug-ins
    {
        auto d = recipeDoc(recipes()[0]);     // mic, echo, room, speakers
        std::vector<int> sel = { d.blocks[1].id, d.blocks[2].id }; std::vector<Wire> outer;
        auto p = makePlugin(d, sel, "Space Echo", { { d.blocks[1].id, "back" }, { d.blocks[2].id, "mix" } }, "blue", &outer);
        check(p && p->ins.size() == 1 && p->outs.size() == 1 && p->fields.size() == 2 && outer.size() == 2, "a plug-in from two blocks: one way in, one way out, two words on its front", (double) outer.size());
        FluxDoc d2; for (auto& b : d.blocks) if (b.id != sel[0] && b.id != sel[1]) d2.blocks.push_back(b); d2.nextId = d.nextId;
        int pid = d2.add("plugin", 0, 150); d2.block(pid)->own = p;
        for (auto w : outer) { if (w.a == -1) w.a = pid; if (w.b == -1) w.b = pid; d2.connect(w); }
        Rig r; r.load(d2); r.run(300, 1); double e = r.run(1500, 1);
        check(e > 0.02 && r.errs().isEmpty(), "the plug-in plays in place of its blocks", e);
        auto* f = p->field(std::to_string(sel[0]) + ".back"); bool liveOk = false;
        if (f) { auto ls = liveSet(r.c, *d2.block(pid), *f, 80.0); liveOk = !ls.empty(); }
        check(liveOk, "its front words reach the blocks inside while it plays", liveOk);
        auto v = pluginToVar(*p); auto p2 = pluginFromVar(juce::JSON::parse(juce::JSON::toString(v)));
        check(p2 && p2->ins.size() == 1 && p2->fields.size() == 2 && p2->inner->blocks.size() == p->inner->blocks.size(), "the plug-in is saved and read back", (double) p2->inner->blocks.size());
        // as a STUDIO device: the track in, the result out
        auto dev = compile(*p->inner, true); bool hasAdc = false, hasDac = false; for (auto& b : dev.doc.boxes) { if (b.text.startsWith("adc~")) hasAdc = true; if (b.text.startsWith("dac~")) hasDac = true; }
        check(hasAdc && hasDac, "as a STUDIO device its ways in and out are the track", hasAdc && hasDac);
        Rig r3; r3.load(*p->inner, true, true); r3.run(200, 1); double e3 = r3.run(1000, 1);
        check(e3 > 0.02 && r3.errs().isEmpty(), "and it plays as a device", e3);
        // moved from outside: a slow wave into a front word
        FluxDoc d4 = d2; int wv = d4.add("move.wave", 400, 150); d4.connect({ wv, 0, pid, 0, std::to_string(sel[0]) + ".back" });
        Rig r4; r4.load(d4); r4.run(200, 1); double e4 = r4.run(1000, 1);
        check(e4 > 0.02 && r4.errs().isEmpty(), "a cable into a word on the plug-in's front", e4);
    }
    // ---- 7: Max boxes and classic patches are blocks too
    {
        int bad = 0; juce::String names;
        for (auto& od : px::catalogue()) { if (!od.aliasOf.empty()) continue;
            Block b; b.type = "box"; b.text = od.example.empty() ? juce::String(od.name) : juce::String(od.example); auto* df = defOf(b);
            px::PBox pb; pb.text = b.text; auto io = px::boxIO(pb);
            if (!df || (int) df->ins.size() != io.ins || (int) df->outs.size() != io.outs) { ++bad; names << od.name << " "; } }
        check(bad == 0, "every object of the PATCHER is a block with its inlets and outlets " + names.substring(0, 200), bad);
        FluxDoc d; int c = d.add("box", 0, 0); d.block(c)->text = "cycle~ 330"; int g = d.add("box", 0, 100); d.block(g)->text = "*~ 0.2"; int sp = d.add("out.speakers", 0, 200);
        d.connect({ c, 0, g, 0, {} }); d.connect({ g, 0, sp, 0, {} });
        Rig r; r.load(d); double e = r.run(500, 0);
        check(e > 0.02 && r.errs().isEmpty(), "Max boxes (cycle~ into *~) play into the speakers", e);
        px::PatchDoc pd; int i = pd.add("obj", "inlet~", 0, 0), o = pd.add("obj", "outlet~", 0, 100), m = pd.add("obj", "*~ 0.5", 0, 50); pd.connect(i, 0, m, 0); pd.connect(m, 0, o, 0);
        FluxDoc d2; int src = d2.add("port.sound.in", 0, 0), cp = d2.add("patch", 0, 100), sp2 = d2.add("out.speakers", 0, 200); d2.block(cp)->text = juce::JSON::toString(pd.toVar(), true);
        d2.connect({ src, 0, cp, 0, {} }); d2.connect({ cp, 0, sp2, 0, {} });
        Rig r2; r2.load(d2); double e2 = r2.run(500, 1);
        check(e2 > 0.02 && r2.errs().isEmpty(), "a classic patch inside a block plays", e2);
    }
    // ---- 8: saved and read back
    {
        int same = 0, n = 0;
        for (auto& rc : recipes()) { auto d = recipeDoc(rc); auto d2 = FluxDoc::fromVar(juce::JSON::parse(juce::JSON::toString(d.toVar())));
            ++n; if (juce::JSON::toString(compile(d).doc.toVar()) == juce::JSON::toString(compile(d2).doc.toVar())) ++same; }
        check(same == n, "every recipe is saved and read back the same", same);
    }
    // ---- 9: asking in words (English and Portuguese)
    {
        auto top = [](const char* q) { auto f = ask(q, 3); if (f.empty()) return juce::String(); return f[0].recipe >= 0 ? recipes()[(size_t) f[0].recipe].name : f[0].block ? juce::String(f[0].block->name) : juce::String(f[0].pxClass); };
        check(top("echo on my voice").containsIgnoreCase("echo"), "\"echo on my voice\" -> " + top("echo on my voice"));
        check(top("arpejador em la menor").containsIgnoreCase("arpeggio"), "\"arpejador em la menor\" -> " + top("arpejador em la menor"));
        check(top("bateria").containsIgnoreCase("drum") || top("bateria").containsIgnoreCase("beat"), "\"bateria\" -> " + top("bateria"));
        check(top("granular").containsIgnoreCase("grain"), "\"granular\" -> " + top("granular"));
        check(top("metro").isNotEmpty(), "\"metro\" -> " + top("metro"));
    }
    say("fluxo check: %d checks, %d failed", total, fails);
    return fails;
}

} // namespace arco
