// ARCO 2.5: PATCHER - the musical objects FLUXO is built from (they are ordinary boxes too: type them in the PATCHER).
// lfo, euclid, steps, rnote, arp, chordnotes, ntrans, nscale, nlen, nvel, humanize, notecho, quantize, chance, nsplit,
// synth~, drum~, follow~, fzero~, grain~, noisegate~, tremolo~, widen~, ringmod~.
// A "notes" cable carries lists "pitch velocity" (velocity 0 ends the note).
#include "PatcherLib.h"
#include <array>
#include <deque>
#include <set>

namespace arco { namespace px {

namespace {
inline Msg noteMsg(int p, int v) { Msg m; m.sel = S::list; m.push(Atom::I(p)); m.push(Atom::I(v)); return m; }
inline bool readNote(const Msg& m, int& p, int& v) {
    if (m.isList() && m.n >= 2) { p = (int) m.f(0); v = (int) m.f(1); return true; }
    if (m.isNum()) { p = (int) m.f(0); v = 100; return true; }
    return false;
}
double rnd01(Ctx& c) { return (c.rand() & 0xFFFFFF) / 16777216.0; }
// a time as beats when it is a note value ("4n" = 1 beat, "1n" = 4, "2bar" = 8), or < 0 when it is Hz / ms
double beatsOf(const Atom& a) {
    if (a.num()) return -1;
    auto s = symStr(a.s); if (s.empty()) return -1;
    if (s.find("bar") != std::string::npos) { double n = std::atof(s.c_str()); return 4.0 * (n > 0 ? n : 1); }
    double n = std::atof(s.c_str()); if (n <= 0) return -1;
    double b = 4.0 / n; if (s.find('t') != std::string::npos) b *= 2.0 / 3.0; if (s.find('d') != std::string::npos) b *= 1.5; return b;
}
const std::map<std::string, std::vector<int>>& scales() {
    static const std::map<std::string, std::vector<int>> sc = { { "major", { 0, 2, 4, 5, 7, 9, 11 } }, { "minor", { 0, 2, 3, 5, 7, 8, 10 } }, { "dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian", { 0, 1, 3, 5, 7, 8, 10 } }, { "lydian", { 0, 2, 4, 6, 7, 9, 11 } }, { "mixolydian", { 0, 2, 4, 5, 7, 9, 10 } }, { "pentatonic", { 0, 2, 4, 7, 9 } },
        { "minorpentatonic", { 0, 3, 5, 7, 10 } }, { "blues", { 0, 3, 5, 6, 7, 10 } }, { "harmonic", { 0, 2, 3, 5, 7, 8, 11 } }, { "chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "wholetone", { 0, 2, 4, 6, 8, 10 } }, { "japanese", { 0, 1, 5, 7, 8 } }, { "arabic", { 0, 1, 4, 5, 7, 8, 11 } } };
    return sc;
}
std::vector<int> scaleOf(int s) { auto it = scales().find(symStr(s)); return it == scales().end() ? scales().at("major") : it->second; }
int snapTo(int note, const std::vector<int>& st, int root) {
    int best = note, bd = 99;
    for (int d = 0; d <= 6; ++d) for (int sg : { -1, 1 }) { int n = note + d * sg; int pc = ((n - root) % 12 + 12) % 12;
        if (std::find(st.begin(), st.end(), pc) != st.end() && d < bd) { bd = d; best = n; } }
    return best;
}

// ================================================================== lfo: a slow wave, 0..1
struct Lfo : Obj {
    Atom rate; int shape; double ph = 0, last = -1, rv = 0.5, rv0 = 0.5, rvT = 0.5; bool on = true; int k = 0; double lastMs = -1;
    explicit Lfo(const Args& a) { setIO(3, 1); wantsBeat = true; shape = argS(a, 0, sym("sine")); rate = a.size() > 1 ? a[1] : Atom::Sy("1n"); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.n) rate = m.a[0]; return; }
        if (in == 2) { if (m.sel == S::symbol && m.n) shape = m.a[0].s; else if (m.n == 0 && !m.isBang()) shape = m.sel; return; }
        if (m.isBang() || m.sel == S::reset) { ph = 0; return; }
        if (m.isNum()) on = m.f() != 0; else if (m.sel == S::stop) on = false; else if (m.sel == S::start) on = true;
        (void) c;
    }
    double wave(double p) {
        auto s = symStr(shape);
        if (s == "triangle") return p < 0.5 ? p * 2 : 2 - p * 2;
        if (s == "saw" || s == "up") return p;
        if (s == "down" || s == "ramp") return 1 - p;
        if (s == "square") return p < 0.5 ? 1 : 0;
        if (s == "random" || s == "steps") return rv;
        if (s == "smooth") { double t = p; t = t * t * (3 - 2 * t); return rv0 + (rvT - rv0) * t; }
        return 0.5 - 0.5 * std::cos(TWO_PI * p);
    }
    void beatPoll(Ctx& c) override {
        if (!on) return;
        double now = c.nowMs; double dt = lastMs < 0 ? 0 : (now - lastMs) / 1000.0; lastMs = now;
        double b = beatsOf(rate), prev = ph;
        if (b > 0) { if (c.running) ph = std::fmod(c.beat / b, 1.0); else ph = std::fmod(ph + dt * (c.bpm / 60.0) / b, 1.0); }
        else { double hz = juce::jlimit(0.001, 200.0, rate.d()); ph = std::fmod(ph + dt * hz, 1.0); }
        if (ph < prev) { rv = rnd01(c); rv0 = rvT; rvT = rnd01(c); }
        if (++k % 6) return;
        double v = juce::jlimit(0.0, 1.0, wave(ph));
        if (std::abs(v - last) > 1e-5) { last = v; uiValue = v; c.outF(this, 0, v); }
    }
};

// ================================================================== euclid: hits spread evenly over steps
struct Euclid : Obj {
    int hits, steps, rot; long long step = -1;
    explicit Euclid(const Args& a) { setIO(4, 2); hits = (int) argI(a, 0, 3); steps = (int) argI(a, 1, 8); rot = (int) argI(a, 2, 0); }
    bool hitAt(long long s) const { if (steps <= 0) return false; long long k = ((s + rot) % steps + steps) % steps; return (k * hits) % steps < hits; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { hits = juce::jlimit(0, 64, (int) m.f()); uiGen++; return; } if (in == 2) { steps = juce::jlimit(1, 64, (int) m.f()); uiGen++; return; } if (in == 3) { rot = (int) m.f(); uiGen++; return; }
        if (m.sel == S::reset) { step = -1; return; }
        if (m.isNum()) step = (long long) m.f() - 1;
        step = (step + 1) % juce::jmax(1, steps); uiValue = (double) step;
        c.outI(this, 1, (double) step); if (hitAt(step)) { uiFlash++; c.bang(this, 0); }
    }
    int uiArray(float* o, int cap) const override { int n = juce::jmin(cap, steps); for (int i = 0; i < n; ++i) o[i] = hitAt(i) ? 1.0f : 0.0f; return n; }
};

// ================================================================== steps: a row of values you draw; a bang plays the next one
struct Steps : Obj {
    std::vector<float> v; std::vector<char> on; int len, cur = -1;
    explicit Steps(const Args& a) { setIO(3, 3); len = juce::jlimit(1, 64, (int) argI(a, 0, 8)); v.assign(64, 0.5f); on.assign(64, 1); for (int i = 0; i < 64; ++i) v[(size_t) i] = 0.25f + 0.5f * (float) ((i * 5) % 8) / 8.0f; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { len = juce::jlimit(1, 64, (int) m.f()); uiGen++; return; }
        if (in == 2 || m.sel == S::set) { if (m.n >= 2) { int i = (int) m.f(0); if (i >= 0 && i < 64) { v[(size_t) i] = (float) juce::jlimit(0.0, 1.0, m.f(1)); if (m.n >= 3) on[(size_t) i] = m.f(2) != 0 ? 1 : 0; uiGen++; } } return; }
        if (m.sel == sym("toggle") && m.n) { int i = (int) m.f(); if (i >= 0 && i < 64) { on[(size_t) i] ^= 1; uiGen++; } return; }
        if (m.sel == sym("random")) { for (int i = 0; i < len; ++i) v[(size_t) i] = (float) rnd01(c); uiGen++; return; }
        if (m.sel == S::reset) { cur = -1; return; }
        if (m.isNum()) cur = (int) m.f() - 1;
        cur = (cur + 1) % len; uiValue = cur;
        c.outI(this, 2, cur);
        if (on[(size_t) cur]) { c.bang(this, 1); c.outF(this, 0, v[(size_t) cur]); uiFlash++; }
    }
    int uiArray(float* o, int cap) const override { int n = 0; for (int i = 0; i < len && n + 2 <= cap; ++i) { o[n++] = v[(size_t) i]; o[n++] = on[(size_t) i]; } return n; }
    juce::var keep() const override { juce::Array<juce::var> a; for (int i = 0; i < 64; ++i) a.add(on[(size_t) i] ? v[(size_t) i] : -1.0f - v[(size_t) i]); return a; }
    void restore(const juce::var& x) override { if (auto* a = x.getArray()) for (int i = 0; i < juce::jmin(64, a->size()); ++i) { float f = (float) (double) (*a)[i]; on[(size_t) i] = f >= 0; v[(size_t) i] = f >= 0 ? f : -1.0f - f; } }
};

// ================================================================== rnote: a random note of a scale
struct RNote : Obj {
    int scale, root, lo, hi, last = -1;
    explicit RNote(const Args& a) { setIO(5, 1); scale = argS(a, 0, sym("major")); root = (int) argI(a, 1, 0); lo = (int) argI(a, 2, 48); hi = (int) argI(a, 3, 72); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) scale = m.a[0].s; else if (m.n == 0 && !m.isBang()) scale = m.sel; return; }
        if (in == 2) { root = (int) m.f(); return; } if (in == 3) { lo = (int) m.f(); return; } if (in == 4) { hi = (int) m.f(); return; }
        auto st = scaleOf(scale); std::vector<int> pool;
        int a = juce::jmin(lo, hi), b = juce::jmax(lo, hi);
        for (int n = a; n <= b; ++n) { int pc = ((n - root) % 12 + 12) % 12; if (std::find(st.begin(), st.end(), pc) != st.end()) pool.push_back(n); }
        if (pool.empty()) return;
        int pick = pool[(size_t) (c.rand() % pool.size())]; if (pool.size() > 2 && pick == last) pick = pool[(size_t) (c.rand() % pool.size())];
        last = pick; uiValue = pick; c.outI(this, 0, pick); (void) m;
    }
};

// ================================================================== arp: the notes you hold, one at a time
struct Arp : Obj {
    std::vector<int> held, order; int mode, oct; int pos = -1, dir = 1; bool latch = false; bool anyDown = false;
    explicit Arp(const Args& a) { setIO(4, 1); mode = argS(a, 0, sym("up")); oct = juce::jlimit(1, 4, (int) argI(a, 1, 1)); }
    std::vector<int> seq() const {
        std::vector<int> base = symStr(mode) == "played" ? order : held; if (symStr(mode) != "played") std::sort(base.begin(), base.end());
        std::vector<int> s; for (int o = 0; o < oct; ++o) for (int n : base) s.push_back(n + 12 * o); return s;
    }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) mode = m.a[0].s; else if (m.n == 0 && !m.isBang()) mode = m.sel; return; }
        if (in == 2) { oct = juce::jlimit(1, 4, (int) m.f()); return; }
        if (in == 3) { latch = m.f() != 0; if (!latch && !anyDown) { held.clear(); order.clear(); } return; }
        int p, v;
        if (!m.isBang() && readNote(m, p, v)) {
            if (v > 0) { bool fresh = !anyDown && latch; if (fresh) { held.clear(); order.clear(); }
                if (std::find(held.begin(), held.end(), p) == held.end()) { held.push_back(p); order.push_back(p); } anyDown = true; downs.insert(p); }
            else { downs.erase(p); anyDown = !downs.empty(); if (!latch) { held.erase(std::remove(held.begin(), held.end(), p), held.end()); order.erase(std::remove(order.begin(), order.end(), p), order.end()); } }
            return;
        }
        auto s = seq(); if (s.empty()) { pos = -1; return; }
        auto md = symStr(mode); int N = (int) s.size();
        if (md == "random") pos = (int) (c.rand() % (unsigned) N);
        else if (md == "down") { pos = pos <= 0 || pos >= N ? N - 1 : pos - 1; }
        else if (md == "updown") { if (N == 1) pos = 0; else { pos += dir; if (pos >= N) { dir = -1; pos = N - 2; } if (pos < 0) { dir = 1; pos = juce::jmin(1, N - 1); } } }
        else pos = (pos + 1) % N;
        pos = juce::jlimit(0, N - 1, pos); uiValue = s[(size_t) pos]; uiFlash++;
        c.outI(this, 0, s[(size_t) pos]);
    }
    std::set<int> downs;
};

// ================================================================== notes in, notes out
struct ChordNotes : Obj {   // each note becomes a chord (the ends end the chord)
    int type, inv; std::map<int, std::vector<int>> on;
    explicit ChordNotes(const Args& a) { setIO(3, 1); type = argS(a, 0, sym("maj")); inv = (int) argI(a, 1, 0); }
    std::vector<int> shape() const {
        static const std::map<std::string, std::vector<int>> t = { { "maj", { 0, 4, 7 } }, { "min", { 0, 3, 7 } }, { "7", { 0, 4, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } }, { "min7", { 0, 3, 7, 10 } },
            { "dim", { 0, 3, 6 } }, { "aug", { 0, 4, 8 } }, { "sus2", { 0, 2, 7 } }, { "sus4", { 0, 5, 7 } }, { "add9", { 0, 4, 7, 14 } }, { "min9", { 0, 3, 7, 10, 14 } }, { "6", { 0, 4, 7, 9 } },
            { "fifth", { 0, 7 } }, { "octave", { 0, 12 } }, { "stack", { 0, 7, 12, 19 } } };
        auto it = t.find(symStr(type)); auto s = it == t.end() ? t.at("maj") : it->second;
        for (int i = 0; i < inv && i < (int) s.size(); ++i) s[(size_t) i] += 12; std::sort(s.begin(), s.end()); return s;
    }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) type = m.a[0].s; else if (m.n == 0 && !m.isBang()) type = m.sel; return; }
        if (in == 2) { inv = (int) m.f(); return; }
        int p, v; if (!readNote(m, p, v)) return;
        if (v > 0) { auto s = shape(); auto& o = on[p]; for (int x : o) c.out(this, 0, noteMsg(x, 0)); o.clear(); for (int d : s) { o.push_back(p + d); c.out(this, 0, noteMsg(p + d, v)); } }
        else { auto it = on.find(p); if (it == on.end()) return; for (int x : it->second) c.out(this, 0, noteMsg(x, 0)); on.erase(it); }
    }
};
struct NTrans : Obj {      // up or down by semitones (an end goes where its start went)
    int by; std::map<int, int> on;
    explicit NTrans(const Args& a) { setIO(2, 1); by = (int) argI(a, 0, 0); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { by = (int) m.f(); return; }
        int p, v; if (!readNote(m, p, v)) return;
        if (v > 0) { int q = juce::jlimit(0, 127, p + by); on[p] = q; c.out(this, 0, noteMsg(q, v)); }
        else { auto it = on.find(p); int q = it == on.end() ? juce::jlimit(0, 127, p + by) : it->second; if (it != on.end()) on.erase(it); c.out(this, 0, noteMsg(q, 0)); }
    }
};
struct NScale : Obj {      // keeps notes in a scale
    int scale, root; std::map<int, int> on;
    explicit NScale(const Args& a) { setIO(3, 1); scale = argS(a, 0, sym("major")); root = (int) argI(a, 1, 0); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) scale = m.a[0].s; else if (m.n == 0 && !m.isBang()) scale = m.sel; return; }
        if (in == 2) { root = (int) m.f(); return; }
        if (m.isNum()) { c.outI(this, 0, snapTo((int) std::lround(m.f()), scaleOf(scale), root)); return; }
        int p, v; if (!readNote(m, p, v)) return;
        if (v > 0) { int q = snapTo(p, scaleOf(scale), root); on[p] = q; c.out(this, 0, noteMsg(q, v)); }
        else { auto it = on.find(p); int q = it == on.end() ? snapTo(p, scaleOf(scale), root) : it->second; if (it != on.end()) on.erase(it); c.out(this, 0, noteMsg(q, 0)); }
    }
};
struct NLen : Obj {        // every note lasts this long (ms or 8n); a number in plays that note
    Atom len; int vel; struct H { int p; }; std::deque<std::pair<double, int>> offs; int tagN = 0;
    explicit NLen(const Args& a) { setIO(3, 1); len = a.empty() ? Atom::Sy("8n") : a[0]; vel = (int) argI(a, 1, 100); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { if (m.n) len = m.a[0]; return; } if (in == 2) { vel = (int) m.f(); return; }
        if (m.sel == S::stop || m.sel == sym("flush")) { for (auto& o : offs) c.out(this, 0, noteMsg(o.second, 0)); offs.clear(); c.cancel(this); return; }
        int p, v; if (m.isNum()) { p = (int) std::lround(m.f()); v = vel; } else if (!readNote(m, p, v)) return;
        if (v <= 0) return;
        c.out(this, 0, noteMsg(p, v));
        offs.push_back({ c.nowMs + juce::jmax(5.0, c.noteMs(len, 200)), p }); std::sort(offs.begin(), offs.end());
        c.delay(this, offs.front().first - c.nowMs);
    }
    void tick(Ctx& c, int) override { while (!offs.empty() && offs.front().first <= c.nowMs + 0.01) { int p = offs.front().second; offs.pop_front(); c.out(this, 0, noteMsg(p, 0)); }
        if (!offs.empty()) c.delay(this, offs.front().first - c.nowMs); }
};
struct NVel : Obj {        // how hard: fixed, scaled or with some chance
    double amt, rnd; explicit NVel(const Args& a) { setIO(3, 1); amt = argF(a, 0, 1); rnd = argF(a, 1, 0); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { amt = m.f(); return; } if (in == 2) { rnd = m.f(); return; }
        int p, v; if (!readNote(m, p, v)) return; if (v > 0) v = juce::jlimit(1, 127, (int) std::lround(v * amt + (rnd01(c) * 2 - 1) * rnd * 64)); c.out(this, 0, noteMsg(p, v)); }
};
struct Humanize : Obj {    // a little late, a little softer or louder - like a person
    double ms, velAmt; std::vector<std::pair<double, Msg>> q; std::map<int, double> lag;
    explicit Humanize(const Args& a) { setIO(3, 1); ms = argF(a, 0, 20); velAmt = argF(a, 1, 0.15); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { ms = m.f(); return; } if (in == 2) { velAmt = m.f(); return; }
        int p, v; if (!readNote(m, p, v)) return;
        double d; if (v > 0) { d = rnd01(c) * ms; lag[p] = d; v = juce::jlimit(1, 127, (int) std::lround(v * (1 + (rnd01(c) * 2 - 1) * velAmt))); } else { d = lag.count(p) ? lag[p] : 0; lag.erase(p); }
        q.push_back({ c.nowMs + d, noteMsg(p, v) }); std::stable_sort(q.begin(), q.end(), [](auto& a, auto& b) { return a.first < b.first; }); c.delay(this, q.front().first - c.nowMs); }
    void tick(Ctx& c, int) override { while (!q.empty() && q.front().first <= c.nowMs + 0.01) { auto m = q.front().second; q.erase(q.begin()); c.out(this, 0, m); } if (!q.empty()) c.delay(this, q.front().first - c.nowMs); }
};
struct NotEcho : Obj {     // the notes come back, softer each time
    Atom time; int count; double fade; std::vector<std::pair<double, Msg>> q;
    explicit NotEcho(const Args& a) { setIO(4, 1); time = a.empty() ? Atom::Sy("8n") : a[0]; count = (int) argI(a, 1, 3); fade = argF(a, 2, 0.6); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { if (m.n) time = m.a[0]; return; } if (in == 2) { count = juce::jlimit(0, 32, (int) m.f()); return; } if (in == 3) { fade = juce::jlimit(0.0, 1.0, m.f()); return; }
        int p, v; if (!readNote(m, p, v)) return; c.out(this, 0, noteMsg(p, v));
        double t = c.noteMs(time, 250); double vv = v;
        for (int k = 1; k <= count; ++k) { if (v > 0) { vv *= fade; if (vv < 1) break; } if ((int) q.size() > 512) break; q.push_back({ c.nowMs + t * k, noteMsg(p, v > 0 ? (int) std::lround(vv) : 0) }); }
        std::stable_sort(q.begin(), q.end(), [](auto& a, auto& b) { return a.first < b.first; }); if (!q.empty()) c.delay(this, q.front().first - c.nowMs); }
    void tick(Ctx& c, int) override { while (!q.empty() && q.front().first <= c.nowMs + 0.01) { auto m = q.front().second; q.erase(q.begin()); c.out(this, 0, m); } if (!q.empty()) c.delay(this, q.front().first - c.nowMs); }
};
struct Quantize : Obj {    // waits for the next 16th (or 8n, 4n ...) of ARCO's clock
    double div; std::vector<Msg> wait; long long last = -1;
    explicit Quantize(const Args& a) { setIO(2, 1); wantsBeat = true; div = a.empty() ? 0.25 : juce::jmax(0.01, beatsOf(a[0]) > 0 ? beatsOf(a[0]) : 0.25); }
    void msg(Ctx&, int in, const Msg& m) override { if (in == 1) { if (m.n) { double b = beatsOf(m.a[0]); if (b > 0) div = b; } return; } if ((int) wait.size() < 256) wait.push_back(m); }
    void beatPoll(Ctx& c) override { long long k = (long long) std::floor(c.beat / div + 1e-9); if (!c.running) { for (auto& m : wait) c.out(this, 0, m); wait.clear(); last = k; return; }
        if (k != last) { last = k; auto w = wait; wait.clear(); for (auto& m : w) c.out(this, 0, m); } }
};
struct Chance : Obj {      // lets things through with a chance (0-1)
    double p; bool lastPass = true; std::set<int> passed;
    explicit Chance(const Args& a) { setIO(2, 2); p = argF(a, 0, 0.5); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { p = juce::jlimit(0.0, 1.0, m.f()); return; }
        int pp, v; if (m.isList() && readNote(m, pp, v)) { if (v > 0) { if (rnd01(c) < p) { passed.insert(pp); c.out(this, 0, m); } else c.out(this, 1, m); } else { if (passed.erase(pp)) c.out(this, 0, m); else c.out(this, 1, m); } return; }
        if (rnd01(c) < p) { uiFlash++; c.out(this, 0, m); } else c.out(this, 1, m); }
};
struct NSplit : Obj {      // notes from low to high go left, the others right
    int lo, hi; std::map<int, int> side;
    explicit NSplit(const Args& a) { setIO(3, 2); lo = (int) argI(a, 0, 0); hi = (int) argI(a, 1, 59); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { lo = (int) m.f(); return; } if (in == 2) { hi = (int) m.f(); return; }
        int p, v; if (!readNote(m, p, v)) return; int s = (p >= lo && p <= hi) ? 0 : 1; if (v > 0) side[p] = s; else { auto it = side.find(p); if (it != side.end()) { s = it->second; side.erase(it); } } c.out(this, s, m); }
};

// ================================================================== synth~: a polyphonic synth (notes in, sound out L R)
struct SynthSig : Obj {
    struct V { int note = -1; double f = 0, ph[2] = { 0, 0 }, env = 0, vel = 0, lpL = 0, lpL2 = 0, lpR = 0, lpR2 = 0; int stage = 0; long long age = 0; };
    std::array<V, 8> v; int wave; double bright = 0.6, atk = 10, rel = 300, width = 0.3; long long clock = 0; double sr = 48000;
    explicit SynthSig(const Args& a) { setIO(5, 2); sigOutlet(0); sigOutlet(1); wave = argS(a, 0, sym("saw")); bright = argF(a, 1, 0.6); atk = argF(a, 2, 10); rel = argF(a, 3, 300); }
    void prepare(double s) override { sr = s; }
    void msg(Ctx&, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) wave = m.a[0].s; else if (m.n == 0 && !m.isBang()) wave = m.sel; return; }
        if (in == 2) { bright = juce::jlimit(0.0, 1.0, m.f()); return; } if (in == 3) { atk = juce::jmax(0.5, m.f()); return; } if (in == 4) { rel = juce::jmax(5.0, m.f()); return; }
        if (m.sel == S::stop || m.sel == sym("flush")) { for (auto& x : v) x.stage = 3; return; }
        int p, vl; if (!readNote(m, p, vl)) return;
        if (vl > 0) { V* best = nullptr; for (auto& x : v) if (x.note == p && x.stage > 0) best = &x;
            if (!best) for (auto& x : v) if (x.stage == 0) { best = &x; break; }
            if (!best) { best = &v[0]; for (auto& x : v) if (x.age < best->age) best = &x; }
            best->note = p; best->f = mtof(p); best->vel = vl / 127.0; best->stage = 1; best->age = ++clock; if (best->env < 1e-4) { best->ph[0] = 0; best->ph[1] = 0.37; } uiFlash++; }
        else for (auto& x : v) if (x.note == p && x.stage > 0 && x.stage < 3) x.stage = 3;
    }
    static double blep(double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
    double osc(double& ph, double dt) {
        auto s = symStr(wave); double y;
        if (s == "sine") y = std::sin(TWO_PI * ph);
        else if (s == "square") { y = ph < 0.5 ? 1 : -1; y += blep(ph, dt); y -= blep(std::fmod(ph + 0.5, 1.0), dt); y *= 0.8; }
        else if (s == "triangle") y = ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph;
        else if (s == "soft") y = std::sin(TWO_PI * ph) * 0.7 + std::sin(2 * TWO_PI * ph) * 0.2 + std::sin(3 * TWO_PI * ph) * 0.1;
        else { y = 2 * ph - 1; y -= blep(ph, dt); y *= 0.8; }
        ph += dt; if (ph >= 1) ph -= 1; return y;
    }
    void dsp(Ctx&, const float* const*, float* const* out, int n) override {
        double a = 1.0 / juce::jmax(1.0, atk * sr / 1000.0), r = std::exp(-1.0 / juce::jmax(1.0, rel * sr / 1000.0 / 4.0));
        double cut = 120.0 * std::pow(150.0, bright); double g0 = 1 - std::exp(-TWO_PI * juce::jmin(cut, sr * 0.45) / sr);
        std::fill(out[0], out[0] + n, 0.0f); std::fill(out[1], out[1] + n, 0.0f);
        for (auto& x : v) { if (x.stage == 0) continue;
            double det = 1.0 + width * 0.004, dt0 = x.f / sr, dt1 = x.f * det / sr; double g = juce::jmin(0.95, g0 * (0.6 + 0.8 * x.vel));
            for (int i = 0; i < n; ++i) {
                if (x.stage == 1) { x.env += a; if (x.env >= 1) { x.env = 1; x.stage = 2; } } else if (x.stage == 3) { x.env *= r; if (x.env < 1e-4) { x.env = 0; x.stage = 0; x.note = -1; break; } }
                double l = osc(x.ph[0], dt0), rr = osc(x.ph[1], dt1);
                x.lpL += g * (l - x.lpL); x.lpL2 += g * (x.lpL - x.lpL2); x.lpR += g * (rr - x.lpR); x.lpR2 += g * (x.lpR - x.lpR2);
                double e = x.env * x.vel * 0.22; out[0][i] += (float) (x.lpL2 * e); out[1][i] += (float) (x.lpR2 * e);
            } }
        float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(out[0][i])); uiValue = pk;
    }
};

// ================================================================== drum~: a drum sound for each hit (kick snare hat clap tom rim cowbell)
struct DrumSig : Obj {
    int kind; double tone = 0.5, decay = 0.5; double t = 1e9, vel = 1, ph = 0, n1 = 0, bp1 = 0, bp2 = 0; unsigned seed = 77; double sr = 48000;
    explicit DrumSig(const Args& a) { setIO(4, 1); sigOutlet(0); kind = argS(a, 0, sym("kick")); tone = argF(a, 1, 0.5); decay = argF(a, 2, 0.5); }
    void prepare(double s) override { sr = s; }
    void msg(Ctx&, int in, const Msg& m) override {
        if (in == 1) { if (m.sel == S::symbol && m.n) kind = m.a[0].s; else if (m.n == 0 && !m.isBang()) kind = m.sel; return; }
        if (in == 2) { tone = juce::jlimit(0.0, 1.0, m.f()); return; } if (in == 3) { decay = juce::jlimit(0.0, 1.0, m.f()); return; }
        int p, v; double vv = 1;
        if (m.isList() && readNote(m, p, v)) { if (v <= 0) return; vv = v / 127.0; } else if (m.isNum()) { if (m.f() <= 0) return; vv = m.f() > 1 ? m.f() / 127.0 : m.f(); }
        t = 0; vel = vv; ph = 0; uiFlash++;
    }
    double nz() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFF) / 32768.0 - 1.0; }
    void dsp(Ctx&, const float* const*, float* const* out, int n) override {
        auto k = symStr(kind);
        for (int i = 0; i < n; ++i) { double y = 0, s = t / sr;
            if (s < 3.0) {
                if (k == "kick") { double f = 45 + 35 * tone + 120 * std::exp(-s * 30); ph += f / sr; y = std::sin(TWO_PI * ph) * std::exp(-s / (0.12 + 0.5 * decay)); y = std::tanh(y * 1.6); }
                else if (k == "snare") { double f = 170 + 90 * tone; ph += f / sr; y = std::sin(TWO_PI * ph) * std::exp(-s * 25) * 0.6 + nz() * std::exp(-s / (0.05 + 0.25 * decay)) * 0.7; }
                else if (k == "hat" || k == "openhat") { double d = k == "hat" ? 0.02 + 0.08 * decay : 0.15 + 0.6 * decay; double x = nz(); n1 += 0.6 * (x - n1); y = (x - n1) * std::exp(-s / d) * (0.6 + 0.4 * tone); }
                else if (k == "clap") { double env = 0; for (int b = 0; b < 3; ++b) { double sb = s - b * 0.011; if (sb >= 0) env += std::exp(-sb * 120); } env += std::exp(-s / (0.08 + 0.3 * decay)) * 0.5;
                    double x = nz(); bp1 += 0.3 * (x - bp1); bp2 += 0.3 * (bp1 - bp2); y = (bp1 - bp2) * env * 1.8; }
                else if (k == "tom") { double f = 90 + 160 * tone + 60 * std::exp(-s * 20); ph += f / sr; y = std::sin(TWO_PI * ph) * std::exp(-s / (0.1 + 0.5 * decay)); }
                else if (k == "rim") { double f = 900 + 800 * tone; ph += f / sr; y = (std::sin(TWO_PI * ph) * 0.6 + nz() * 0.3) * std::exp(-s * 90); }
                else if (k == "cowbell") { double f = 540 + 300 * tone; ph += f / sr; double sq = std::fmod(ph, 1.0) < 0.5 ? 1 : -1, sq2 = std::fmod(ph * 1.48, 1.0) < 0.5 ? 1 : -1; y = (sq + sq2) * 0.3 * std::exp(-s / (0.05 + 0.3 * decay)); }
                else { double f = 60 + 60 * tone; ph += f / sr; y = std::sin(TWO_PI * ph) * std::exp(-s * 8); }
                t += 1;
            }
            out[0][i] = (float) (y * vel * 0.8); }
    }
};

// ================================================================== follow~: how loud a sound is, as a number 0..1 (smooth)
struct Follow : Obj {
    double env = 0, up = 10, down = 150, gain = 1, last = -1; int k = 0; double sr = 48000;
    explicit Follow(const Args& a) { setIO(2, 1); sigInlet(0); gain = argF(a, 0, 1); up = argF(a, 1, 10); down = argF(a, 2, 150); wantsBeat = true; }
    void prepare(double s) override { sr = s; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { gain = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const*, int n) override {
        double au = std::exp(-1.0 / juce::jmax(1.0, up * sr / 1000.0)), ad = std::exp(-1.0 / juce::jmax(1.0, down * sr / 1000.0));
        for (int i = 0; i < n; ++i) { double x = std::abs(in[0][i]); env = x > env ? x + (env - x) * au : x + (env - x) * ad; }
    }
    void beatPoll(Ctx& c) override { if (++k % 8) return; double v = juce::jlimit(0.0, 1.0, std::sqrt(env) * gain); if (std::abs(v - last) > 0.002) { last = v; uiValue = v; c.outF(this, 0, v); } }
};

// ================================================================== fzero~: which note is sounding (Hz, then the loudness)
struct FZero : Obj {
    std::vector<float> buf; int w = 0, hop = 0; double sr = 48000; float lastHz = 0;
    explicit FZero(const Args&) { setIO(1, 2); sigInlet(0); buf.assign(2048, 0.0f); wantsBeat = true; }
    void prepare(double s) override { sr = s; }
    float pendHz = -1, pendAmp = 0;
    void beatPoll(Ctx& c) override { if (pendHz < 0) return; float h = pendHz, a = pendAmp; pendHz = -1; c.outF(this, 1, a); c.outF(this, 0, h); }
    void dsp(Ctx& c, const float* const* in, float* const*, int n) override {
        for (int i = 0; i < n; ++i) { buf[(size_t) w] = in[0][i]; w = (w + 1) % 2048; }
        hop += n; if (hop < 1024) return; hop = 0;
        const int W = 1024, D = 2; std::vector<float> x(2048 / D); for (int i = 0; i < 2048 / D; ++i) x[(size_t) i] = buf[(size_t) ((w + i * D) % 2048)];
        double rms = 0; for (float s : x) rms += s * s; rms = std::sqrt(rms / x.size());
        if (rms < 0.01) { if (lastHz > 0) { lastHz = 0; pendAmp = (float) rms; pendHz = 0; } return; }
        double srd = sr / D; int tMin = (int) (srd / 1500), tMax = juce::jmin(W / D - 2, (int) (srd / 50)); std::vector<double> d((size_t) tMax + 2, 0.0);
        int N = W / D; for (int t = 1; t <= tMax; ++t) { double s = 0; for (int i = 0; i < N; ++i) { double q = x[(size_t) i] - x[(size_t) (i + t)]; s += q * q; } d[(size_t) t] = s; }
        double run = 0; int best = -1; std::vector<double> dn((size_t) tMax + 2, 1.0);
        for (int t = 1; t <= tMax; ++t) { run += d[(size_t) t]; dn[(size_t) t] = run > 0 ? d[(size_t) t] * t / run : 1; }
        for (int t = juce::jmax(2, tMin); t < tMax; ++t) if (dn[(size_t) t] < 0.15 && dn[(size_t) t] <= dn[(size_t) t + 1]) { best = t; break; }
        if (best < 0) return;
        double a = dn[(size_t) best - 1], b = dn[(size_t) best], cc = dn[(size_t) best + 1], den = a - 2 * b + cc; double tt = best + (std::abs(den) > 1e-12 ? 0.5 * (a - cc) / den : 0);
        float hz = (float) (srd / tt); lastHz = hz; uiValue = hz; pendAmp = (float) rms; pendHz = hz; (void) c;
    }
};

// ================================================================== hz2note: frequencies into held notes (a sung note lasts as long as you sing it)
struct Hz2Note : Obj {
    int cur = -1, cand = -1, seen = 0, vel;
    explicit Hz2Note(const Args& a) { setIO(2, 1); vel = (int) argI(a, 0, 100); }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 1) { vel = juce::jlimit(1, 127, (int) m.f()); return; }
        if (!m.isNum()) return; double hz = m.f();
        if (hz < 20) { cand = -1; seen = 0; if (cur >= 0) { c.out(this, 0, noteMsg(cur, 0)); cur = -1; uiValue = -1; } return; }
        double nf = ftom(hz); int n = (int) std::lround(nf);
        if (cur >= 0 && std::abs(nf - cur) < 0.7) { cand = -1; seen = 0; return; }      // still the same note (a little vibrato is fine)
        if (n == cand) ++seen; else { cand = n; seen = 1; }
        if (seen < 2) return;
        if (cur >= 0) c.out(this, 0, noteMsg(cur, 0));
        cur = n; seen = 0; cand = -1; uiValue = n; uiFlash++; c.out(this, 0, noteMsg(cur, vel));
    }
};

// ================================================================== grain~: clouds of tiny pieces of a buffer~
struct GrainSig : Obj {
    int name; double pos = 0.3, size = 80, dens = 20, pitch = 0, spray = 0.1; bool on = true; double sr = 48000, untilNext = 0;
    struct G { double p, inc, len, age; float pan; bool live = false; }; std::array<G, 48> g; unsigned seed = 9;
    explicit GrainSig(const Args& a) { setIO(6, 2); sigOutlet(0); sigOutlet(1); name = argS(a, 0, sym("sound")); }
    void prepare(double s) override { sr = s; }
    double rn() { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFFFF) / 16777216.0; }
    void msg(Ctx& c, int in, const Msg& m) override {
        if (in == 0) { if (m.sel == S::set && m.n) name = m.a[0].s; else if (m.isNum()) on = m.f() != 0; return; }
        double v = m.f(); if (in == 1) pos = juce::jlimit(0.0, 1.0, v); else if (in == 2) size = juce::jlimit(5.0, 2000.0, v); else if (in == 3) dens = juce::jlimit(0.5, 200.0, v);
        else if (in == 4) pitch = juce::jlimit(-36.0, 36.0, v); else if (in == 5) spray = juce::jlimit(0.0, 1.0, v); (void) c; }
    void dsp(Ctx& c, const float* const*, float* const* out, int n) override {
        std::fill(out[0], out[0] + n, 0.0f); std::fill(out[1], out[1] + n, 0.0f);
        auto b = c.buffer(name); if (!b || b->frames() < 16) return; long F = b->frames(); double ratio = b->sr / sr;
        for (int i = 0; i < n; ++i) {
            if (on && (untilNext -= 1) <= 0) { untilNext = sr / dens * (0.7 + 0.6 * rn());
                for (auto& x : g) if (!x.live) { x.live = true; x.age = 0; x.len = size * sr / 1000.0; x.inc = std::pow(2.0, pitch / 12.0) * ratio;
                    x.p = juce::jlimit(0.0, (double) F - 2, (pos + (rn() * 2 - 1) * spray * 0.5) * F); x.pan = (float) rn(); break; } }
            for (auto& x : g) if (x.live) { double e = std::sin(juce::MathConstants<double>::pi * x.age / x.len); float s = b->interp(0, x.p) * (float) e * 0.5f;
                out[0][i] += s * (1 - x.pan); out[1][i] += s * x.pan; x.p += x.inc; if (x.p >= F - 2) x.p -= F - 2; x.age += 1; if (x.age >= x.len) x.live = false; }
        }
    }
};

// ================================================================== small effects
struct NoiseGate : Obj { double th, env = 0, g = 0; double sr = 48000; explicit NoiseGate(const Args& a) { setIO(2, 1); sigInlet(0); sigOutlet(0); th = argF(a, 0, -40); }
    void prepare(double s) override { sr = s; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { th = m.f(); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { double t = std::pow(10.0, th / 20.0), a = 1 - std::exp(-1.0 / (0.002 * sr)), r = 1 - std::exp(-1.0 / (0.08 * sr));
        for (int i = 0; i < n; ++i) { double x = std::abs(in[0][i]); env = juce::jmax(x, env * 0.9995); double tg = env > t ? 1 : 0; g += (tg > g ? a : r) * (tg - g); out[0][i] = (float) (in[0][i] * g); } } };
struct Tremolo : Obj { Atom rate; double depth, ph = 0; double sr = 48000; explicit Tremolo(const Args& a) { setIO(3, 1); sigInlet(0); sigOutlet(0); rate = a.empty() ? Atom::Sy("8n") : a[0]; depth = argF(a, 1, 0.5); }
    void prepare(double s) override { sr = s; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 1) { if (m.n) rate = m.a[0]; return; } if (in == 2) { depth = juce::jlimit(0.0, 1.0, m.f()); return; } Obj::msg(c, in, m); }
    void dsp(Ctx& c, const float* const* in, float* const* out, int n) override { double b = beatsOf(rate); double hz = b > 0 ? (c.bpm / 60.0) / b : juce::jlimit(0.01, 50.0, rate.d());
        for (int i = 0; i < n; ++i) { double l = 0.5 + 0.5 * std::cos(TWO_PI * ph); ph += hz / sr; if (ph >= 1) ph -= 1; out[0][i] = (float) (in[0][i] * (1 - depth * (1 - l))); } } };
struct Widen : Obj { double amt; explicit Widen(const Args& a) { setIO(3, 2); sigInlet(0); sigInlet(1); sigOutlet(0); sigOutlet(1); amt = argF(a, 0, 1.4); }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 2) { amt = juce::jlimit(0.0, 3.0, m.f()); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double mid = (in[0][i] + in[1][i]) * 0.5, side = (in[0][i] - in[1][i]) * 0.5 * amt; out[0][i] = (float) (mid + side); out[1][i] = (float) (mid - side); } } };
struct RingMod : Obj { double hz, mix, ph = 0, sr = 48000; explicit RingMod(const Args& a) { setIO(3, 1); sigInlet(0); sigInlet(1, (float) argF(a, 0, 220)); sigOutlet(0); hz = argF(a, 0, 220); mix = argF(a, 1, 1); }
    void prepare(double s) override { sr = s; }
    void msg(Ctx& c, int in, const Msg& m) override { if (in == 2) { mix = juce::jlimit(0.0, 1.0, m.f()); return; } Obj::msg(c, in, m); }
    void dsp(Ctx&, const float* const* in, float* const* out, int n) override { for (int i = 0; i < n; ++i) { double y = in[0][i] * std::sin(TWO_PI * ph); ph += juce::jlimit(0.0, sr * 0.45, (double) in[1][i]) / sr; if (ph >= 1) ph -= 1; out[0][i] = (float) (in[0][i] * (1 - mix) + y * mix); } } };
} // namespace

void registerMore() {
    const char* M = "Music (FLUXO)";
    def("lfo", M, "A slow wave from 0 to 1 that moves things by itself ([lfo sine 1n]: once a bar, in ARCO's tempo; or a number in Hz). Shapes: sine triangle saw down square random smooth.",
        "1 / 0 on, bang restarts | rate (1n, 4n, 2bar or Hz) | shape", "0..1", "lfo sine 1n", "lfo wave slow move modulate wobble sweep", mk<Lfo>());
    def("euclid", M, "Spreads some hits as evenly as possible over some steps - the rhythms of the whole world ([euclid 3 8]). A bang moves one step.",
        "bang: next step | hits | steps | rotate", "bang on a hit | the step", "euclid 3 8", "rhythm euclidean pattern beat spread", mk<Euclid>());
    def("steps", M, "A row of values you draw (0..1): each bang plays the next step. \"set 3 0.5\", \"toggle 3\", \"random\".",
        "bang: next | how many steps | set i v", "value 0..1 | bang (step on) | the step", "steps 8", "sequencer step sequence row pattern", mk<Steps>());
    def("rnote", M, "A random note of a scale between a low and a high note, for each bang ([rnote minor 9 57 81]: A minor).",
        "bang | scale | root 0-11 | low | high", "note", "rnote major 0 48 72", "random note melody scale generate", mk<RNote>());
    def("arp", M, "An arpeggiator: hold notes (a list \"pitch velocity\"), each bang plays the next one. Modes: up down updown random played.",
        "notes, bang: next | mode | octaves | latch 1 / 0", "note", "arp up 1", "arpeggio arpeggiator chord broken", mk<Arp>());
    def("chordnotes", M, "Every note becomes a chord (notes in, notes out): maj min 7 maj7 min7 dim aug sus2 sus4 add9 min9 6 fifth octave stack.",
        "notes | chord | inversion", "notes", "chordnotes min7", "chord harmony triad", mk<ChordNotes>());
    def("ntrans", M, "Moves notes up or down by semitones (the end of a note always matches its start).", "notes | semitones", "notes", "ntrans 12", "transpose octave shift notes", mk<NTrans>());
    def("nscale", M, "Moves every note to the nearest note of a scale.", "notes or a number | scale | root 0-11", "notes", "nscale minor 9", "scale lock quantize key notes", mk<NScale>());
    def("nlen", M, "Every note lasts this long (ms or 8n); a number plays that note.", "notes or a number | length | velocity", "notes", "nlen 8n 100", "note length duration gate", mk<NLen>());
    def("nvel", M, "How hard the notes are: times a number, plus some chance.", "notes | times | random 0-1", "notes", "nvel 1 0.1", "velocity dynamics accent", mk<NVel>());
    def("humanize", M, "A little late, a little softer or harder - like a person plays.", "notes | ms | velocity 0-1", "notes", "humanize 20 0.15", "human feel loose groove", mk<Humanize>());
    def("notecho", M, "The notes come back: so many times, softer each time.", "notes | time | how many | fade 0-1", "notes", "notecho 8n 3 0.6", "note echo repeat delay midi", mk<NotEcho>());
    def("quantize", M, "Holds what comes in until the next 16th (or 8n, 4n) of ARCO's clock - everything lands in time.", "anything | grid", "anything", "quantize 16n", "quantize grid in time snap", mk<Quantize>());
    def("chance", M, "Lets things through with a chance (0-1); the rest go right.", "anything | chance", "passed | not", "chance 0.5", "probability random maybe sometimes", mk<Chance>());
    def("nsplit", M, "Notes from the low note to the high one go left, the others right.", "notes | low | high", "inside | outside", "nsplit 0 59", "split keyboard zones", mk<NSplit>());
    def("synth~", M, "A synth with 8 voices: notes in (\"pitch velocity\"), sound out (left, right). Waves: saw square triangle sine soft.",
        "notes | wave | brightness 0-1 | attack ms | release ms", "left | right", "synth~ saw 0.6 10 300", "synth polyphonic instrument keys pad lead", mk<SynthSig>());
    def("drum~", M, "A drum sound for each hit (a bang, a velocity, or notes): kick snare hat openhat clap tom rim cowbell.",
        "hit | kind | tone 0-1 | decay 0-1", "sound", "drum~ kick", "drum kick snare hat clap percussion beat", mk<DrumSig>());
    def("follow~", M, "How loud a sound is, as a smooth number 0..1 (an envelope follower).", "sound | sensitivity", "0..1", "follow~ 1", "envelope follower level loudness", mk<Follow>());
    def("fzero~", M, "Listens for the note being played or sung: its frequency (Hz) and how loud.", "sound", "Hz | loudness", "fzero~", "pitch detect tracking tuner note listen", mk<FZero>());
    def("hz2note", M, "Frequencies (Hz) into notes that last as long as the frequency stays - for [fzero~]: sing and play.", "Hz (0: silence) | velocity", "notes", "hz2note 100", "pitch to notes frequency held voice", mk<Hz2Note>());
    def("grain~", M, "Granular: clouds of tiny pieces of a [buffer~] - on / off, where 0-1, size ms, how many per second, pitch, spread.",
        "on / off, set name | position | size ms | density | pitch | spray", "left | right", "grain~ sound", "granular grains cloud texture", mk<GrainSig>());
    def("noisegate~", M, "Silence under a level (dB): cuts the hiss between notes.", "sound | threshold dB", "sound", "noisegate~ -40", "gate noise silence", mk<NoiseGate>());
    def("tremolo~", M, "The volume goes up and down (in tempo: 8n, or Hz).", "sound | rate | depth 0-1", "sound", "tremolo~ 8n 0.5", "tremolo volume wobble pulse", mk<Tremolo>());
    def("widen~", M, "Makes a stereo sound wider (or narrower).", "left | right | width 0-3", "left | right", "widen~ 1.4", "stereo wide width", mk<Widen>());
    def("ringmod~", M, "Ring modulation: multiplies the sound by a tone - bells, robots.", "sound | Hz | mix", "sound", "ringmod~ 220", "ring modulation robot bell metallic", mk<RingMod>());
}

}} // namespace arco::px
