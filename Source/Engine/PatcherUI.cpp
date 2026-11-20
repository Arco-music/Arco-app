// ARCO 2.4: PATCHER - the objects you touch (buttons, toggles, numbers, sliders, keys, menus) and the ones you watch (meters, scopes).
// Their sound and messages live here; how they look is in the PATCHER screen.
#include "PatcherLib.h"

namespace arco { namespace px {

namespace {

struct Button : Obj { explicit Button(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg&) override { uiFlash++; c.bang(this, 0); } };
struct Toggle : Obj {
    double v = 0; explicit Toggle(const Args&) { setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == S::set) { v = m.f() != 0 ? 1 : 0; uiValue = v; return; }
        if (m.isNum()) v = m.f() != 0 ? 1 : 0; else if (m.isBang()) v = v ? 0 : 1; else return;
        uiValue = v; c.outI(this, 0, v);
    }
    juce::var keep() const override { return (int) v; }
    void restore(const juce::var& s) override { v = (int) s ? 1 : 0; uiValue = v; }
};
struct Number : Obj {
    double v = 0, lo, hi; bool fl;
    Number(const Args& a, bool f) : fl(f) { setIO(2, 1); lo = argF(a, 0, -1e15); hi = argF(a, 1, 1e15); if (a.size() < 2) { lo = -1e15; hi = 1e15; } }
    void say(Ctx& c) { if (fl) c.outF(this, 0, v); else c.outI(this, 0, v); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1 || m.sel == S::set) { v = clampV(m.f()); uiValue = v; return; }
        if (m.sel == sym("min")) { lo = m.f(); return; } if (m.sel == sym("max")) { hi = m.f(); return; }
        if (m.isNum() || m.isList()) v = clampV(m.f()); else if (!m.isBang()) return;
        uiValue = v; say(c);
    }
    double clampV(double x) const { x = juce::jlimit(lo, hi, x); return fl ? x : std::trunc(x); }
    juce::var keep() const override { return v; }
    void restore(const juce::var& s) override { v = (double) s; uiValue = v; }
};
struct Slider : Obj {
    double v = 0, lo, hi; bool fl;
    explicit Slider(const Args& a) { setIO(1, 1); lo = argF(a, 0, 0); hi = argF(a, 1, a.empty() ? 127 : 1); fl = argIsFloat(a, 0) || argIsFloat(a, 1); if (a.empty()) hi = 127; uiValue = v; }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == S::set) { v = juce::jlimit(std::min(lo, hi), std::max(lo, hi), m.f()); uiValue = v; return; }
        if (m.sel == sym("min")) { lo = m.f(); return; } if (m.sel == sym("max")) { hi = m.f(); return; }
        if (m.sel == sym("range") && m.n >= 2) { lo = m.f(0); hi = m.f(1); return; }
        if (m.isNum()) v = juce::jlimit(std::min(lo, hi), std::max(lo, hi), m.f()); else if (!m.isBang()) return;
        if (!fl) v = std::round(v);
        uiValue = v; if (fl) c.outF(this, 0, v); else c.outI(this, 0, v);
    }
    juce::var keep() const override { return v; }
    void restore(const juce::var& s) override { v = (double) s; uiValue = v; }
};
struct Kslider : Obj {
    int vel = 0; explicit Kslider(const Args&) { setIO(2, 2); uiValue = -1; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { vel = (int) m.f(); return; }
        if (m.sel == S::set) { uiValue = m.f(); return; }
        if (m.isList() && m.n >= 2) { vel = (int) m.f(1); }
        if (!m.isNum() && !m.isList()) return;
        int note = (int) m.f(0); uiValue = vel > 0 ? note : -1; if (!m.isList() && vel == 0) vel = 100;
        c.outI(this, 1, vel); c.outI(this, 0, note);
    }
};
struct Multislider : Obj {
    std::vector<float> v; double lo, hi;
    explicit Multislider(const Args& a) { setIO(1, 2); v.assign((size_t) clampN(argI(a, 0, 8), 1, 128), 0.0f); lo = argF(a, 1, 0); hi = argF(a, 2, 127); if (a.size() < 3) { lo = 0; hi = a.size() == 2 ? argF(a, 1, 127) : 127; } fl = argIsFloat(a, 1) || argIsFloat(a, 2); }
    bool fl = false;
    void out(Ctx& c) { Atom o[MAXA]; int n = juce::jmin(MAXA, (int) v.size()); for (int i = 0; i < n; ++i) o[i] = fl ? Atom::F(v[(size_t) i]) : Atom::I(v[(size_t) i]); c.out(this, 0, Msg::fromAtoms(o, n)); }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == S::set || m.isList()) { Atom at[MAXA]; int n = m.atoms(at); int st = m.sel == S::set ? 0 : 0;
            if (m.sel == S::set && n == 2 && (int) v.size() != 2) { int k = (int) at[0].d(); if (k >= 0 && k < (int) v.size()) v[(size_t) k] = (float) juce::jlimit(lo, hi, at[1].d()); uiGen++; return; }
            if (m.sel == S::set && n >= 1 && v.size() > 1 && n == 2) {}
            for (int i = st; i < n && i < (int) v.size(); ++i) v[(size_t) i] = (float) juce::jlimit(std::min(lo, hi), std::max(lo, hi), at[i].d());
            uiGen++; if (m.sel != S::set) out(c); return; }
        if (m.sel == sym("size")) { v.resize((size_t) clampN((long long) m.f(), 1, 128), 0.0f); uiGen++; return; }
        if (m.sel == sym("fetch")) { int k = (int) m.f(); if (k >= 0 && k < (int) v.size()) c.outF(this, 1, v[(size_t) k]); return; }
        if (m.isNum() && v.size() == 1) { v[0] = (float) juce::jlimit(std::min(lo, hi), std::max(lo, hi), m.f()); uiGen++; out(c); return; }
        if (m.isBang()) out(c);
    }
    int uiArray(float* o, int cap) const override { int n = juce::jmin(cap, (int) v.size()); for (int i = 0; i < n; ++i) o[i] = (float) ((v[(size_t) i] - lo) / (hi == lo ? 1 : hi - lo)); return n; }
    juce::var keep() const override { juce::Array<juce::var> a; for (float x : v) a.add(x); return a; }
    void restore(const juce::var& s) override { if (auto* a = s.getArray()) for (int i = 0; i < a->size() && i < (int) v.size(); ++i) v[(size_t) i] = (float) (double) (*a)[i]; uiGen++; }
};
struct Umenu : Obj {
    std::vector<std::string> items; int idx = 0;
    explicit Umenu(const Args& a) { setIO(1, 2); std::string cur; for (auto& x : a) { auto s = x.str(); if (s == ",") { if (!cur.empty()) items.push_back(cur); cur.clear(); continue; } if (!cur.empty()) cur += " "; cur += s; } if (!cur.empty()) items.push_back(cur);
        bool commas = false; for (auto& x : a) commas |= x.str() == ","; if (!commas) { items.clear(); for (auto& x : a) items.push_back(x.str()); } }
    void msg(Ctx& c, int, const Msg& m) override {
        if (m.sel == S::set) { idx = (int) m.f(); uiValue = idx; return; }
        if (m.sel == S::clear) { items.clear(); uiGen++; return; }
        if (m.sel == sym("append")) { std::string s; for (int i = 0; i < m.n; ++i) { if (i) s += " "; s += m.a[i].str(); } items.push_back(s); uiGen++; return; }
        if (m.isNum()) idx = (int) m.f(); else if (!m.isBang()) return;
        if (items.empty()) return; idx = juce::jlimit(0, (int) items.size() - 1, idx); uiValue = idx;
        auto at = parseAtoms(items[(size_t) idx]); c.out(this, 1, Msg::fromAtoms(at.data(), (int) at.size())); c.outI(this, 0, idx);
    }
    juce::String uiText() const override { juce::String s; for (auto& i : items) s << juce::String(i) << "\n"; return s; }
    juce::var keep() const override { return idx; }
    void restore(const juce::var& s) override { idx = (int) s; uiValue = idx; }
};
struct Radio : Obj { int n, idx = 0; explicit Radio(const Args& a) { n = clampN(argI(a, 0, 4), 2, 32); setIO(1, 1); }
    void msg(Ctx& c, int, const Msg& m) override { if (m.sel == S::set) { idx = (int) m.f(); uiValue = idx; return; } if (m.isNum()) idx = juce::jlimit(0, n - 1, (int) m.f()); else if (!m.isBang()) return; uiValue = idx; c.outI(this, 0, idx); }
    juce::var keep() const override { return idx; } void restore(const juce::var& s) override { idx = (int) s; uiValue = idx; } };
// [function]: draw an envelope; a bang sends it to a [line~] ("level time level time ...")
struct Function : Obj {
    std::vector<std::pair<float, float>> pts { { 0.0f, 0.0f }, { 0.08f, 1.0f }, { 0.4f, 0.5f }, { 1.0f, 0.0f } };   // (time 0-1, level 0-1)
    double dur;
    explicit Function(const Args& a) { setIO(2, 2); dur = argF(a, 0, 1000); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1 || m.sel == sym("setdomain")) { dur = juce::jmax(1.0, m.f()); return; }
        if (m.sel == S::clear) { pts.clear(); uiGen++; return; }
        if (m.sel == sym("points") || m.sel == S::set) { pts.clear(); for (int i = 0; i + 1 < m.n; i += 2) pts.push_back({ (float) m.f(i), (float) m.f(i + 1) }); uiGen++; return; }
        if (m.isNum()) { double t = m.f() / dur; float y = 0; for (size_t i = 0; i + 1 < pts.size(); ++i) if (t >= pts[i].first && t <= pts[i + 1].first) { float k = (float) ((t - pts[i].first) / juce::jmax(1e-6f, pts[i + 1].first - pts[i].first)); y = pts[i].second + (pts[i + 1].second - pts[i].second) * k; } c.outF(this, 0, y); return; }
        if (!m.isBang()) return;
        Atom o[MAXA]; int n = 0; float lastT = 0;
        for (auto& p : pts) { if (n + 2 > MAXA) break; o[n++] = Atom::F(p.second); o[n++] = Atom::F((p.first - lastT) * dur); lastT = p.first; }
        if (n >= 2) o[1] = Atom::F(0);
        c.out(this, 0, Msg::fromAtoms(o, n)); uiFlash++;
    }
    int uiArray(float* o, int cap) const override { int n = 0; for (auto& p : pts) { if (n + 2 > cap) break; o[n++] = p.first; o[n++] = p.second; } return n; }
    juce::var keep() const override { juce::Array<juce::var> a; for (auto& p : pts) { a.add(p.first); a.add(p.second); } return a; }
    void restore(const juce::var& s) override { if (auto* a = s.getArray()) { pts.clear(); for (int i = 0; i + 1 < a->size(); i += 2) pts.push_back({ (float) (double) (*a)[i], (float) (double) (*a)[i + 1] }); } uiGen++; }
};
struct Panel : Obj { explicit Panel(const Args&) { setIO(0, 0); } };
struct Ezdac : Obj { bool on = true; explicit Ezdac(const Args&) { setIO(2, 0); sigInlet(0); sigInlet(1); uiValue = 1; }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.isNum() && !connectedIn[in].load()) { on = m.f() != 0; uiValue = on; return; } if (m.isBang()) { on = !on; uiValue = on; return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override { if (!on) return; for (int k = 0; k < 2; ++k) if (c.dacOut[k]) for (int i = 0; i < n; ++i) c.dacOut[k][i] += in[k][i]; float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(in[0][i])); uiFlash = pk > 0.001f ? 1 : 0; }
    juce::var keep() const override { return on ? 1 : 0; } void restore(const juce::var& s) override { on = (int) s != 0; uiValue = on; } };
struct Ezadc : Obj { bool on = true; explicit Ezadc(const Args&) { setIO(1, 2); sigOutlet(0); sigOutlet(1); uiValue = 1; }
    void msg(Ctx&, int, const Msg& m) override { if (m.isNum()) on = m.f() != 0; else on = !on; uiValue = on; }
    void dsp(Ctx& c, const float* const*, float* const* out, int n) override { for (int k = 0; k < 2; ++k) { if (on && c.adcIn[k]) std::copy(c.adcIn[k], c.adcIn[k] + n, out[k]); else std::fill(out[k], out[k] + n, 0.0f); } } };
struct Meter : Obj { float pk = 0; explicit Meter(const Args&) { setIO(1, 1); sigInlet(0); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override { float p = 0; for (int i = 0; i < n; ++i) p = juce::jmax(p, std::abs(in[0][i])); pk = juce::jmax(p, pk * (float) std::exp(-n / (0.3 * c.sr))); uiValue = pk;
        if (++blocks % 64 == 0) c.delay(this, 0); }
    int blocks = 0;
    void tick(Ctx& c, int) override { c.outF(this, 0, pk); } };
struct Scope : Obj {
    float buf[512] = {}; int w = 0; float shown[256] = {}; float last = 0; int since = 0; int hold = 0;
    explicit Scope(const Args&) { setIO(2, 0); sigInlet(0); sigInlet(1); }
    void dsp(Ctx&, const float* const* in, float* const*, int n) override {
        for (int i = 0; i < n; ++i) { float x = in[0][i];
            if (hold > 0) { buf[w++] = x; if (w >= 256) { std::copy(buf, buf + 256, shown); uiGen++; hold = 0; w = 0; } }
            else if ((last < 0 && x >= 0) || ++since > 2048) { hold = 1; since = 0; w = 0; buf[w++] = x; }
            last = x; }
    }
    int uiArray(float* o, int cap) const override { int n = juce::jmin(cap, 256); std::copy(shown, shown + n, o); return n; }
};
struct NumberSig : Obj { explicit NumberSig(const Args&) { setIO(1, 1); sigInlet(0); sigOutlet(0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (m.isNum()) { sv[0] = (float) m.f(); uiValue = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { std::copy(in[0], in[0] + n, out[0]); uiValue = in[0][n - 1]; } };
struct Gain : Obj { double v = 0.75, g = 0.75; explicit Gain(const Args&) { setIO(2, 1); sigInlet(0); sigOutlet(0); uiValue = v; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1 || m.isNum() || m.sel == S::set) { v = juce::jlimit(0.0, 1.0, m.f()); uiValue = v; return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { double tg = v <= 0.001 ? 0 : std::pow(10.0, (v - 0.75) * 60.0 / 20.0) ; for (int i = 0; i < n; ++i) { g += (tg - g) * 0.002; out[0][i] = (float) (in[0][i] * g); } }
    juce::var keep() const override { return v; } void restore(const juce::var& s) override { v = (double) s; uiValue = v; } };
struct Spectro : Obj { float buf[1024] = {}; int w = 0; explicit Spectro(const Args&) { setIO(1, 0); sigInlet(0); }
    void dsp(Ctx&, const float* const* in, float* const*, int n) override { for (int i = 0; i < n; ++i) { buf[w] = in[0][i]; w = (w + 1) & 1023; } uiGen++; }
    int uiArray(float* o, int cap) const override { int n = juce::jmin(cap, 1024); for (int i = 0; i < n; ++i) o[i] = buf[(w + i) & 1023]; return n; } };
struct Waveform : Obj { int name; std::shared_ptr<Buf> b; explicit Waveform(const Args& a) { name = argS(a, 0, 0); setIO(1, 0); }
    void msg(Ctx&, int, const Msg& m) override { if (m.sel == S::set && m.n) { name = m.a[0].s; uiGen++; } }
    void dspStarted(Ctx& c) override { b = c.buffer(name); }
    juce::String uiText() const override { return juce::String(symStr(name)); } };

} // namespace

void registerUI() {
    const char* T = "Things to touch"; const char* W = "Sound: watch & measure";
    def("button", T, "A button: click it to send a bang; it flashes when anything goes through.", "anything: flash and send a bang", "bang", "button", "bang click trigger press", mk<Button>(), 28, 28);
    alias("bng", "button");
    def("toggle", T, "A switch: on (1) or off (0). Click it; a bang flips it.", "0 / 1, bang flips | ", "1 / 0", "toggle", "switch on off checkbox", mk<Toggle>(), 28, 28);
    alias("tgl", "toggle");
    def("number", T, "A whole number you can drag up and down (or type into). [number 0 127]: only between those.", "number: show and send, set: show | set", "number", "number", "integer value box drag int", [](const Args& a) { return std::make_shared<Number>(a, false); }, 64, 24);
    def("flonum", T, "A decimal number you can drag (shift-drag for small steps).", "number | set", "number", "flonum", "float decimal value box", [](const Args& a) { return std::make_shared<Number>(a, true); }, 72, 24);
    def("slider", T, "A slider: [slider 0 127] sends whole numbers, [slider 0. 1.] decimals.", "number: move it, set: only show", "number", "slider 0 127", "fader slide volume control", mk<Slider>(), 30, 140);
    def("hslider", T, "A slider lying down.", "number", "number", "hslider 0 127", "fader horizontal", mk<Slider>(), 160, 30);
    def("dial", T, "A knob: [dial 0 127], [dial 0. 1.].", "number", "number", "dial 0 127", "knob rotary control", mk<Slider>(), 52, 52);
    alias("knob", "dial");
    def("kslider", T, "A keyboard on screen: click a key to send its note (and velocity); notes coming in light it.", "note | velocity", "note | velocity", "kslider", "keyboard piano keys notes", mk<Kslider>(), 336, 60);
    def("multislider", T, "Many sliders in a row - a list you draw with the mouse ([multislider 8 0 127]): steps, levels, a pattern.", "list: show and send, set i v", "list | one value (fetch)", "multislider 8 0 127", "sequence steps list graph bars draw", mk<Multislider>(), 160, 84);
    def("umenu", T, "A menu: choose an item ([umenu sine saw square]); it sends its number and its words.", "number: choose, append, clear", "number | the words", "umenu sine saw square", "menu dropdown choose list", mk<Umenu>(), 120, 24);
    def("radiogroup", T, "A row of choices, one at a time.", "number", "number", "radiogroup 4", "choice option", mk<Radio>(), 26, 96);
    def("function", T, "Draw an envelope (click to add points, drag them, right-click to remove); a bang sends it to a [line~]. The number is its length in ms.", "bang: send, a time: its level | length ms", "to line~ | ", "function 1000", "envelope breakpoint draw curve shape", mk<Function>(), 220, 90);
    def("panel", T, "A coloured panel to group things on the screen (does nothing else).", "", "", "panel", "background group colour", mk<Panel>(), 160, 100);
    def("ezdac~", "Sound: in & out", "A speaker: plug sound into it to hear it; click it to turn it off and on.", "left | right", "", "ezdac~", "speaker output hear sound out", mk<Ezdac>(), 46, 46);
    def("ezadc~", "Sound: in & out", "A microphone: LIVE inputs 1 and 2; click to turn it off and on.", "", "left | right", "ezadc~", "microphone input mic", mk<Ezadc>(), 46, 46);
    def("meter~", W, "Shows how loud a sound is.", "sound", "level", "meter~", "level meter loud vu", mk<Meter>(), 20, 100);
    alias("levelmeter~", "meter~");
    def("scope~", W, "An oscilloscope: shows the shape of a wave.", "sound | ", "", "scope~", "oscilloscope wave shape see", mk<Scope>(), 180, 110);
    def("number~", W, "Shows the value of a signal (and a number in sends it as a signal).", "signal or number", "signal", "number~", "value signal see", mk<NumberSig>(), 80, 24);
    def("gain~", "Sound: effects", "A volume fader for a sound.", "sound | 0-1", "sound", "gain~", "volume fader level", mk<Gain>(), 30, 140);
    alias("live.gain~", "gain~");
    def("spectroscope~", W, "Shows the frequencies in a sound (low on the left, high on the right).", "sound", "", "spectroscope~", "spectrum frequency analyzer fft", mk<Spectro>(), 220, 110);
    def("waveform~", "Sound: samples", "Shows the sound in a [buffer~]: [waveform~ mysound].", "set name", "", "waveform~ mysound", "see sample buffer display", mk<Waveform>(), 260, 90);
    alias("live.dial", "dial"); alias("live.toggle", "toggle"); alias("live.button", "button"); alias("live.numbox", "flonum"); alias("live.slider", "slider");
}

}} // namespace arco::px
