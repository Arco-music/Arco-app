#include "Instruments.h"
#include <map>
#include <climits>

namespace score {

// ======================================================================== the shared part: events + arpeggiator
void Instrument::addArpParams() {
    arpOn = addOnOff("arp", "ARP");
    arpMode = addChoice("arpm", "ARP MODE", { "Up", "Down", "Up-down", "Random", "As played", "Chord" });
    arpRate = addChoice("arpr", "ARP RATE", divisionNames(), 12);
    arpOct = addChoice("arpo", "OCTAVES", { "1", "2", "3", "4" });
    arpGate = add("arpg", "GATE", 5, 100, 60, Unit::Percent);
    arpSwing = add("arps", "SWING", 0, 75, 0, Unit::Percent);
    arpLatch = addOnOff("arpl", "LATCH");
}

void Instrument::handle(const NoteEv& e) {
    switch (e.type) {
        case NoteEv::On:
            if (e.b <= 0) { NoteEv off = e; off.type = NoteEv::Off; handle(off); return; }
            held[(size_t) (e.a & 127)].store(1); lastNote.store(e.a);
            if (arpOn && arpOn->on()) {
                if (arpLatch->on() && arpHeld.empty()) arpLatched.clear();
                arpHeld.push_back({ e.a, e.b }); if (arpLatch->on()) arpLatched.push_back({ e.a, e.b });
                return; }
            sustained.erase(std::remove(sustained.begin(), sustained.end(), e.a), sustained.end());
            noteOn(e.a, e.b); break;
        case NoteEv::Off:
            held[(size_t) (e.a & 127)].store(0);
            if (arpOn && arpOn->on()) { arpHeld.erase(std::remove_if(arpHeld.begin(), arpHeld.end(), [&](auto& p) { return p.first == e.a; }), arpHeld.end()); return; }
            if (sustain) { sustained.push_back(e.a); return; }
            noteOff(e.a); break;
        case NoteEv::Bend: pitchBend(e.b); break;
        case NoteEv::CC: controller(e.a, e.b); break;
        case NoteEv::Sustain:
            sustain = e.b > 0.5f;
            if (!sustain) { for (int n : sustained) noteOff(n); sustained.clear(); }
            break;
        case NoteEv::AllOff:
            for (auto& h : held) h.store(0);
            arpHeld.clear(); arpLatched.clear(); sustained.clear(); sustain = false;
            arpRelease();
            allNotesOff(e.a != 0); break;
    }
}

void Instrument::arpRelease() {
    for (int nn : arpSounding) noteOff(nn);
    arpSounding.clear(); arpOffAt = -1;
}

void Instrument::arpTrigger() {
    arpRelease();
    auto& src = (arpLatch->on() && arpHeld.empty()) ? arpLatched : arpHeld;
    if (src.empty()) return;
    int oct = arpOct->geti() + 1, mode = arpMode->geti();
    std::vector<int> base; for (auto& p : src) base.push_back(p.first);
    if (mode != 4) std::sort(base.begin(), base.end());
    std::vector<int> notes; for (int o = 0; o < oct; ++o) for (int b : base) notes.push_back(b + 12 * o);
    float vel = src.back().second;
    if (mode == 5) { for (int nn : base) { noteOn(nn, vel); arpSounding.push_back(nn); } return; }      // chord: all together
    int n = (int) notes.size(), idx = 0;
    switch (mode) {
        case 0: case 4: idx = arpStep % n; break;
        case 1: idx = n - 1 - (arpStep % n); break;
        case 2: { int per = std::max(1, 2 * n - 2); int k = arpStep % per; idx = k < n ? k : per - k; break; }
        default: idx = (int) (arpRng.uni() * (float) n) % n; break;
    }
    ++arpStep;
    int nn = notes[(size_t) std::max(0, std::min(n - 1, idx))];
    noteOn(nn, vel); arpSounding.push_back(nn); lastNote.store(nn); arpSteps.fetch_add(1);
}

void Instrument::render(float* L, float* R, int n, const NoteEv* ev, int nev, const TimeInfo& t) {
    bool arp = arpOn != nullptr && arpOn->on();
    if (arp != arpWas) {
        if (arp) {                                           // the notes held now become the arpeggio
            allNotesOff(false); arpHeld.clear();
            for (int k = 0; k < 128; ++k) if (held[(size_t) k].load()) arpHeld.push_back({ k, 0.8f });
            arpStep = 0; lastStep = -1;
        } else { arpRelease(); for (auto& p : arpHeld) noteOn(p.first, p.second); arpLatched.clear(); }
        arpWas = arp;
    }
    const double spb = 60.0 / std::max(20.0, t.bpm) * sr;
    const double stepLen = arp ? divisionBeats(arpRate->geti()) * spb : 1.0;
    const double base = t.playing ? t.beat * spb : arpFree;
    auto stepStart = [&](long k) { return (double) k * stepLen + ((k & 1) ? arpSwing->get() / 100.0 * 0.5 * stepLen : 0.0); };
    int pos = 0, ei = 0;
    while (pos < n) {
        while (ei < nev && ev[ei].offset <= pos) handle(ev[ei++]);
        int next = ei < nev ? std::max(pos + 1, std::min(n, ev[ei].offset)) : n;
        if (arp) {
            double abs = base + pos;
            if (arpOffAt >= 0 && abs >= arpOffAt) arpRelease();
            long k = (long) std::floor(abs / stepLen);
            if (k != lastStep && abs >= stepStart(k)) { lastStep = k; arpTrigger(); if (!arpSounding.empty()) arpOffAt = stepStart(k) + stepLen * arpGate->get() / 100.0; }
            double nb = (k == lastStep || abs >= stepStart(k)) ? stepStart(k + 1) : stepStart(k);
            if (arpOffAt >= 0 && arpOffAt > abs) nb = std::min(nb, arpOffAt);
            next = std::min(next, pos + std::max(1, (int) std::ceil(nb - abs)));
        }
        renderSub(L + pos, R + pos, next - pos, t);
        pos = next;
    }
    while (ei < nev) handle(ev[ei++]);
    if (!t.playing) arpFree += n;
    scope.push(L, R, n);
}

// ======================================================================== NOVA
std::vector<std::string> Nova::sourceNames() {
    return { "-", "ENV 2", "ENV 3", "ENV 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "VELOCITY", "NOTE", "MOD WHEEL", "PRESSURE", "BEND",
             "RANDOM", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "ENV 1" };
}
std::vector<std::string> Nova::destNames() {
    return { "-", "A PITCH", "A POSITION", "A LEVEL", "A WARP", "A PAN", "B PITCH", "B POSITION", "B LEVEL", "B WARP", "B PAN",
             "C PITCH", "C POSITION", "C LEVEL", "C WARP", "C PAN", "SUB LEVEL", "NOISE LEVEL",
             "F1 CUTOFF", "F1 RESO", "F1 DRIVE", "F2 CUTOFF", "F2 RESO", "F2 DRIVE", "VOLUME", "PAN",
             "LFO 1 RATE", "LFO 2 RATE", "LFO 3 RATE", "LFO 4 RATE", "PITCH (ALL)", "DETUNE (ALL)" };
}
enum Dst { dNone, dA, dB = 6, dC = 11, dSub = 16, dNoise, dF1C, dF1R, dF1D, dF2C, dF2R, dF2D, dAmp, dPan, dL1, dL2, dL3, dL4, dPitch, dDetune, dCount };

static std::vector<std::string> filterTypes() { return { "Low 12", "Low 24", "High 12", "High 24", "Band", "Notch", "Ladder", "Comb +", "Comb -", "Formant" }; }
static std::vector<std::string> warpNames() { return { "None", "Sync", "Bend +", "Bend -", "Squeeze", "Mirror", "FM", "Ring", "Quantize" }; }

Nova::Nova() : Instrument("nova") {
    auto tn = factoryTableNames();
    const char* O[3] = { "o1", "o2", "o3" }; const char* ON[3] = { "A", "B", "C" };
    for (int k = 0; k < 3; ++k) {
        std::string p = O[k], n = std::string("OSC ") + ON[k] + " ";
        addOnOff(p + "on", n + "ON", k == 0);
        addChoice(p + "tab", n + "TABLE", tn, k == 1 ? 0 : 0);
        add(p + "pos", n + "POSITION", 0, 100, k == 0 ? 33 : 50, Unit::Percent);
        add(p + "oct", n + "OCTAVE", -3, 3, k == 1 ? -1 : 0, Unit::Oct);
        add(p + "semi", n + "SEMI", -12, 12, 0, Unit::Semi);
        add(p + "fine", n + "FINE", -100, 100, 0, Unit::Cents);
        add(p + "lvl", n + "LEVEL", 0, 100, 70, Unit::Percent);
        add(p + "pan", n + "PAN", -50, 50, 0, Unit::Pan);
        add(p + "uni", n + "UNISON", 1, 8, 1, Unit::None);
        add(p + "det", n + "DETUNE", 0, 100, 20, Unit::Percent);
        add(p + "spr", n + "SPREAD", 0, 100, 70, Unit::Percent);
        addChoice(p + "warp", n + "WARP", warpNames());
        add(p + "wamt", n + "WARP AMT", 0, 100, 0, Unit::Percent);
        addChoice(p + "flt", n + "TO", { "Filter 1", "Filter 2", "Both", "Direct" });
        add(p + "ph", n + "PHASE", 0, 100, 100, Unit::Percent);
    }
    add("sub", "SUB", 0, 100, 0, Unit::Percent); addChoice("suboct", "SUB OCT", { "-1", "-2" }); addChoice("subsh", "SUB SHAPE", { "Sine", "Square", "Triangle" });
    add("noise", "NOISE", 0, 100, 0, Unit::Percent); add("ncol", "COLOUR", 0, 100, 50, Unit::Percent); addChoice("nflt", "NOISE TO", { "Filter 1", "Filter 2", "Both", "Direct" });
    addOnOff("f1on", "FILTER 1", true); addChoice("f1ty", "F1 TYPE", filterTypes(), 1); add("f1c", "F1 CUTOFF", 20, 20000, 4000, Unit::Hz, true);
    add("f1r", "F1 RESO", 0, 100, 15, Unit::Percent); add("f1d", "F1 DRIVE", 0, 100, 0, Unit::Percent); add("f1k", "F1 KEY", 0, 100, 30, Unit::Percent);
    addOnOff("f2on", "FILTER 2", false); addChoice("f2ty", "F2 TYPE", filterTypes(), 2); add("f2c", "F2 CUTOFF", 20, 20000, 120, Unit::Hz, true);
    add("f2r", "F2 RESO", 0, 100, 10, Unit::Percent); add("f2d", "F2 DRIVE", 0, 100, 0, Unit::Percent); add("f2k", "F2 KEY", 0, 100, 0, Unit::Percent);
    addChoice("froute", "ROUTING", { "Serial", "Parallel" });
    for (int e = 1; e <= 4; ++e) {
        std::string p = "e" + std::to_string(e), n = "ENV " + std::to_string(e) + " ";
        add(p + "a", n + "ATTACK", 0.5f, 20000, e == 1 ? 5.0f : 2.0f, Unit::Ms, true);
        add(p + "d", n + "DECAY", 1, 20000, e == 1 ? 300.0f : 600.0f, Unit::Ms, true);
        add(p + "s", n + "SUSTAIN", 0, 100, e == 1 ? 80.0f : 20.0f, Unit::Percent);
        add(p + "r", n + "RELEASE", 1, 30000, e == 1 ? 400.0f : 500.0f, Unit::Ms, true);
        add(p + "c", n + "CURVE", -100, 100, 0, Unit::None);
    }
    for (int l = 1; l <= 4; ++l) {
        std::string p = "l" + std::to_string(l), n = "LFO " + std::to_string(l) + " ";
        addChoice(p + "sh", n + "SHAPE", { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Smooth random" });
        add(p + "rate", n + "RATE", 0.01f, 40, l == 1 ? 0.5f : 2.0f, Unit::Hz, true);
        addOnOff(p + "sync", n + "SYNC");
        addChoice(p + "div", n + "DIVISION", divisionNames(), 6);
        addChoice(p + "mode", n + "MODE", { "Free", "Retrigger", "Envelope" }, 1);
    }
    for (int m = 1; m <= 4; ++m) add("m" + std::to_string(m), "MACRO " + std::to_string(m), 0, 100, 0, Unit::Percent);
    add("poly", "VOICES", 1, 16, 8, Unit::None);
    addChoice("vmode", "MODE", { "Poly", "Mono", "Legato" });
    add("glide", "GLIDE", 0, 2000, 0, Unit::Ms);
    add("vel", "VELOCITY", 0, 100, 60, Unit::Percent);
    add("bendr", "BEND RANGE", 1, 24, 2, Unit::Semi);
    add("lvl", "VOLUME", -36, 12, -6, Unit::dB);
    add("pan", "PAN", -50, 50, 0, Unit::Pan);
    addOnOff("fxdrv", "DRIVE"); addOnOff("fxcho", "CHORUS"); addOnOff("fxdly", "DELAY"); addOnOff("fxrev", "REVERB");
    auto srcs = sourceNames(), dsts = destNames();
    for (int s = 0; s < NSLOT; ++s) {
        std::string p = "ms" + std::to_string(s);
        addChoice(p + "s", "SOURCE " + std::to_string(s + 1), srcs, s == 0 ? 1 : 0);
        addChoice(p + "d", "TARGET " + std::to_string(s + 1), dsts, s == 0 ? (int) dF1C : 0);
        add(p + "a", "AMOUNT " + std::to_string(s + 1), -100, 100, s == 0 ? 35.0f : 0.0f, Unit::Percent);
    }
    addArpParams();
    fxDrive = makeEffect("drv"); fxChorus = makeEffect("cho"); fxDelay = makeEffect("dly"); fxReverb = makeEffect("rev");
    fxDrive->find("dr")->set(10); fxDrive->find("out")->set(-4); fxReverb->find("mix")->set(25);
}

int Nova::activeVoices() const { int c = 0; for (auto& v : voices) if (v.on) ++c; return c; }

void Nova::prepare(double s, int mb) {
    Instrument::prepare(s, mb);
    for (auto* e : internalEffects()) e->prepare(s, mb);
    vbL.assign(64, 0.0f); vbR.assign(64, 0.0f);
    for (auto& v : voices) for (int f = 0; f < 2; ++f) for (int c = 0; c < 2; ++c) v.comb[f][c].assign((size_t) (s / 18.0) + 4, 0.0f);
    for (auto& v : voices) v.on = false;
}

void Nova::startVoice(Voice& v, int note, float vel, bool legato) {
    float glideMs = val(Glide);
    float fromPitch = lastPlayed >= 0 ? (float) lastPlayed : (float) note;
    if (!legato) {
        bool wasOn = v.on;
        v.on = true; v.releasing = false; v.age = ++ageCounter; v.rnd = glRng.bi(); v.vel = vel;
        for (auto& e : v.env) { e.start = wasOn ? e.v : 0.0f; e.prog = 0; e.stage = 1; if (!wasOn) e.v = 0; }
        for (int k = 0; k < 3; ++k) {
            float pp = val(O1Phase + k * 15);
            for (int u = 0; u < MAXU; ++u) v.ph[k][u] = pp >= 99.5f ? glRng.uni() : pp / 100.0f * (u == 0 ? 1.0f : glRng.uni());
        }
        for (int l = 0; l < 4; ++l) { v.lfoPh[l] = params[(size_t) (L1Mode + l * 5)]->geti() == 0 ? globalLfo[l] : 0.0; v.lfoHeld[l] = glRng.bi(); v.lfoPrev[l] = 0; }
        if (!wasOn) {
            for (int c = 0; c < 2; ++c) { v.f1[c].reset(); v.f2[c].reset(); v.f1b[c].reset(); v.f2b[c].reset(); v.fa1[c].reset(); v.fb1[c].reset(); v.fa2[c].reset(); v.fb2[c].reset(); v.l1[c].reset(); v.l2[c].reset(); }
            for (int f = 0; f < 2; ++f) for (int c = 0; c < 2; ++c) std::fill(v.comb[f][c].begin(), v.comb[f][c].end(), 0.0f);
            v.subPh = 0; v.pink = 0;
        }
        v.firstBlock = true;
    }
    v.note = note;
    v.glideFrom = glideMs > 0.5f ? (legato ? v.pitch : fromPitch) : (float) note;
    v.glideT = glideMs > 0.5f ? 0.0f : 1.0f;
    v.pitch = v.glideFrom;
    lastPlayed = note;
}

void Nova::noteOn(int note, float vel) {
    int mode = params[VMode]->geti();
    if (mode > 0) {                                               // mono / legato
        monoStack.erase(std::remove(monoStack.begin(), monoStack.end(), note), monoStack.end()); monoStack.push_back(note);
        Voice& v = voices[0];
        for (size_t i = 1; i < voices.size(); ++i) if (voices[i].on) voices[i].releasing = true, voices[i].env[0].stage = 4;
        bool legato = mode == 2 && v.on && !v.releasing;
        startVoice(v, note, vel, legato);
        return;
    }
    int poly = params[Poly]->geti();
    // the same note again: retrigger that voice
    Voice* pick = nullptr; int active = 0;
    for (auto& v : voices) if (v.on) ++active;
    for (auto& v : voices) if (v.on && v.note == note && v.releasing) { pick = &v; break; }
    if (!pick) for (int i = 0; i < poly; ++i) if (!voices[(size_t) i].on) { pick = &voices[(size_t) i]; break; }
    if (!pick) {                                                  // steal: the oldest released, else the oldest
        long best = LONG_MAX;
        for (int i = 0; i < poly; ++i) { auto& v = voices[(size_t) i]; long a = v.age - (v.releasing ? 1000000000L : 0); if (a < best) { best = a; pick = &v; } }
    }
    if (pick) startVoice(*pick, note, vel, false);
}

void Nova::noteOff(int note) {
    int mode = params[VMode]->geti();
    if (mode > 0) {
        monoStack.erase(std::remove(monoStack.begin(), monoStack.end(), note), monoStack.end());
        Voice& v = voices[0];
        if (!v.on || v.note != note) return;
        if (!monoStack.empty()) { startVoice(v, monoStack.back(), v.vel, mode == 2); return; }
        v.releasing = true; for (auto& e : v.env) { e.stage = 4; e.rel0 = e.v; }
        return;
    }
    for (auto& v : voices) if (v.on && v.note == note && !v.releasing) { v.releasing = true; for (auto& e : v.env) { e.stage = 4; e.rel0 = e.v; } }
}

void Nova::allNotesOff(bool hard) {
    monoStack.clear();
    for (auto& v : voices) { if (hard) v.on = false; else if (v.on) { v.releasing = true; for (auto& e : v.env) { e.stage = 4; e.rel0 = e.v; } } }
    if (hard) for (auto* e : internalEffects()) e->reset();
}

// an envelope sample, dt = seconds per step; returns the value
float Nova::envTick(Env& e, int idx, float dt) {
    float a = val(E1A + idx * 5) * 0.001f, d = val(E1D + idx * 5) * 0.001f, s = val(E1S + idx * 5) / 100.0f, r = val(E1R + idx * 5) * 0.001f;
    float c = val(E1C + idx * 5) / 100.0f;                   // -1 = fast start (snappy), +1 = slow start (soft)
    float ex = std::pow(3.0f, c);
    switch (e.stage) {
        case 1: e.prog += dt / std::max(0.0005f, a);
            if (e.prog >= 1) { e.prog = 0; e.stage = 2; e.v = 1; } else e.v = e.start + (1 - e.start) * std::pow(e.prog, ex);
            break;
        case 2: e.prog += dt / std::max(0.001f, d);
            if (e.prog >= 1) { e.prog = 0; e.stage = 3; e.v = s; } else e.v = s + (1 - s) * std::pow(1 - e.prog, 3.0f / ex);
            break;
        case 3: e.v = s; break;
        case 4: e.prog += dt / std::max(0.001f, r);
            if (e.prog >= 1) { e.prog = 0; e.stage = 0; e.v = 0; } else e.v = e.rel0 * std::pow(1 - e.prog, 3.0f / ex);
            break;
        default: e.v = 0; break;
    }
    return e.v;
}

void Nova::renderSub(float* outL, float* outR, int n, const TimeInfo& t) {
    const int CB = 32;      // control block
    for (int o = 0; o < n; o += CB) {
        int m = std::min(CB, n - o);
        float* L = outL + o; float* R = outR + o;
        std::fill(vbL.begin(), vbL.begin() + m, 0.0f); std::fill(vbR.begin(), vbR.begin() + m, 0.0f);
        float dt = (float) (m / sr);
        // global LFO phases (FREE mode)
        for (int l = 0; l < 4; ++l) {
            bool sync = params[(size_t) (L1Sync + l * 5)]->on();
            double rate = sync ? t.bpm / 60.0 / divisionBeats(params[(size_t) (L1Div + l * 5)]->geti()) : val(L1Rate + l * 5);
            if (sync && t.playing) globalLfo[l] = std::fmod(t.beat / divisionBeats(params[(size_t) (L1Div + l * 5)]->geti()), 1.0);
            else { globalLfo[l] += rate * dt; globalLfo[l] -= std::floor(globalLfo[l]); }
        }
        float bendSemi = bend * val(BendRange);
        for (auto& v : voices) {
            if (!v.on) continue;
            // ---- modulation sources (block rate)
            float src[19] = {};
            for (int e = 1; e < 4; ++e) src[e] = envTick(v.env[e], e, dt);
            for (int l = 0; l < 4; ++l) {
                bool sync = params[(size_t) (L1Sync + l * 5)]->on(); int mode = params[(size_t) (L1Mode + l * 5)]->geti();
                double rate = sync ? t.bpm / 60.0 / divisionBeats(params[(size_t) (L1Div + l * 5)]->geti()) : val(L1Rate + l * 5);
                rate *= std::pow(2.0, v.modS[dL1 + l] * 4.0);
                if (mode == 0) v.lfoPh[l] = globalLfo[l];
                else { v.lfoPh[l] += rate * dt;
                       if (mode == 2 && v.lfoPh[l] >= 1.0) v.lfoPh[l] = 0.9999;
                       else if (v.lfoPh[l] >= 1.0) { v.lfoPh[l] -= std::floor(v.lfoPh[l]); v.lfoPrev[l] = v.lfoHeld[l]; v.lfoHeld[l] = glRng.bi(); } }
                if (mode == 0 && v.lfoPh[l] < lastGl[l]) { v.lfoPrev[l] = v.lfoHeld[l]; v.lfoHeld[l] = sharedHeld[l]; }
                src[4 + l] = lfoShape((Shape) params[(size_t) (L1Shape + l * 5)]->geti(), (float) v.lfoPh[l], v.lfoHeld[l], v.lfoPrev[l]);
            }
            src[8] = v.vel; src[9] = (v.note - 60) / 60.0f; src[10] = modWheel; src[11] = aftertouch; src[12] = bend; src[13] = v.rnd;
            for (int k = 0; k < 4; ++k) src[14 + k] = val(M1 + k) / 100.0f;
            src[18] = v.env[0].v;
            float mod[dCount] = {};
            for (int s = 0; s < NSLOT; ++s) {
                int si = params[(size_t) (MS0 + s * 3)]->geti(), di = params[(size_t) (MS0 + s * 3 + 1)]->geti();
                if (si <= 0 || di <= 0 || di >= dCount) continue;
                mod[di] += src[si] * val(MS0 + s * 3 + 2) / 100.0f;
            }
            for (int d = 0; d < dCount; ++d) { if (v.firstBlock) v.modS[d] = mod[d]; else v.modS[d] += (mod[d] - v.modS[d]) * 0.5f; }
            v.firstBlock = false;
            // ---- pitch (with glide)
            if (v.glideT < 1.0f) { v.glideT = std::min(1.0f, v.glideT + dt / std::max(0.001f, val(Glide) * 0.001f)); }
            v.pitch = v.glideFrom + ((float) v.note - v.glideFrom) * v.glideT;
            float basePitch = v.pitch + bendSemi + v.modS[dPitch] * 24.0f;
            // ---- oscillators: increments per unison voice
            struct OscRun { bool on; const Wavetable* tab; int f0, f1; float ft; int lvl; float inc[MAXU]; float gainL[MAXU], gainR[MAXU]; int uni; int warp; float wamt; int route; float level; };
            OscRun os[3];
            for (int k = 0; k < 3; ++k) {
                int b = O1On + k * 15; OscRun& r = os[k];
                r.on = params[(size_t) b]->on() && (val(O1Lvl + k * 15) + v.modS[dA + k * 5 + 2] * 100.0f) > 0.01f;
                if (!r.on) continue;
                int ti = params[(size_t) (O1Tab + k * 15)]->geti();
                auto& ft = factoryTables();
                const Wavetable* tab = ti < (int) ft.size() ? ft[(size_t) ti].get() : (userTable[k] ? userTable[k].get() : ft[0].get());
                r.tab = tab;
                float pos = clampf(val(O1Pos + k * 15) / 100.0f + v.modS[dA + k * 5 + 1], 0, 1) * (float) (tab->frames - 1);
                r.f0 = (int) pos; r.f1 = std::min(tab->frames - 1, r.f0 + 1); r.ft = pos - (float) r.f0;
                float pitch = basePitch + val(O1Oct + k * 15) * 12.0f + val(O1Semi + k * 15) + val(O1Fine + k * 15) * 0.01f + v.modS[dA + k * 5] * 24.0f;
                float hz = noteToHz(pitch);
                r.lvl = Wavetable::levelFor(hz * (params[(size_t) (O1Warp + k * 15)]->geti() == 1 ? 1.0f + val(O1WAmt + k * 15) / 100.0f * 7.0f : 1.0f), sr);
                r.uni = params[(size_t) (O1Uni + k * 15)]->geti();
                float det = (val(O1Det + k * 15) / 100.0f + v.modS[dDetune]) * 50.0f, spr = val(O1Spr + k * 15) / 100.0f;
                float pan = clampf(val(O1Pan + k * 15) / 50.0f + v.modS[dA + k * 5 + 4], -1, 1);
                r.level = clampf(val(O1Lvl + k * 15) / 100.0f + v.modS[dA + k * 5 + 2], 0, 1.5f) / std::sqrt((float) r.uni);
                for (int u = 0; u < r.uni; ++u) {
                    float off = r.uni == 1 ? 0.0f : ((float) u / (float) (r.uni - 1) * 2.0f - 1.0f);
                    r.inc[u] = (float) (hz * std::pow(2.0f, off * det / 1200.0f) / sr);
                    float pp = clampf(pan + off * spr, -1, 1); float a = (pp + 1) * 0.25f * kPiF;
                    r.gainL[u] = std::cos(a) * 1.4142f * r.level; r.gainR[u] = std::sin(a) * 1.4142f * r.level;
                }
                r.warp = params[(size_t) (O1Warp + k * 15)]->geti();
                r.wamt = clampf(val(O1WAmt + k * 15) / 100.0f + v.modS[dA + k * 5 + 3], 0, 1);
                r.route = params[(size_t) (O1Flt + k * 15)]->geti();
            }
            float subLvl = clampf(val(SubLvl) / 100.0f + v.modS[dSub], 0, 1.5f), noiseLvl = clampf(val(NoiseLvl) / 100.0f + v.modS[dNoise], 0, 1.5f);
            double subInc = noteToHz(basePitch - 12.0f * (params[SubOct]->geti() + 1)) / sr; int subSh = params[SubShape]->geti();
            int subRoute = params[O1Flt]->geti(), noiseRoute = params[NoiseFlt]->geti();
            float ncol = val(NoiseColor) / 100.0f;
            // ---- filters (block rate coefficients)
            bool f1on = params[F1On]->on(), f2on = params[F2On]->on();
            int f1t = params[F1Type]->geti(), f2t = params[F2Type]->geti();
            auto cutoff = [&](int base, int key, int dst) {
                float c = val(base) * std::pow(2.0f, (v.note - 60) / 12.0f * val(key) / 100.0f + v.modS[dst] * 6.0f);
                return clampf(c, 20.0f, (float) (sr * 0.45)); };
            float c1 = cutoff(F1Cut, F1Key, dF1C), c2 = cutoff(F2Cut, F2Key, dF2C);
            float r1 = clampf(val(F1Res) / 100.0f + v.modS[dF1R], 0, 1), r2 = clampf(val(F2Res) / 100.0f + v.modS[dF2R], 0, 1);
            float d1 = 1.0f + clampf(val(F1Drv) / 100.0f + v.modS[dF1D], 0, 1) * 6.0f, d2 = 1.0f + clampf(val(F2Drv) / 100.0f + v.modS[dF2D], 0, 1) * 6.0f;
            for (int c = 0; c < 2; ++c) {
                v.f1[c].set(sr, c1, (f1t == 1 || f1t == 3) ? r1 * 0.7f : r1); v.f2[c].set(sr, c2, (f2t == 1 || f2t == 3) ? r2 * 0.7f : r2);
                v.l1[c].set(sr, c1, r1); v.l2[c].set(sr, c2, r2);
                if (f1t == 1 || f1t == 3) v.f1b[c].set(sr, c1, 0.15f);
                if (f2t == 1 || f2t == 3) v.f2b[c].set(sr, c2, 0.15f);
                if (f1t == 9) setFormant(v.fa1[c], v.fb1[c], c1, r1);
                if (f2t == 9) setFormant(v.fa2[c], v.fb2[c], c2, r2);
            }
            bool serial = params[FRoute]->geti() == 0;
            float amp = clampf(1.0f + v.modS[dAmp], 0, 2) * dbToGain(val(Level)) * (1.0f - val(VelSens) / 100.0f * (1.0f - v.vel));
            float vpan = clampf(val(Pan) / 50.0f + v.modS[dPan], -1, 1); float pa = (vpan + 1) * 0.25f * kPiF, pl = std::cos(pa) * 1.4142f, pr = std::sin(pa) * 1.4142f;
            int combLen1 = std::max(2, std::min((int) v.comb[0][0].size() - 2, (int) (sr / c1))), combLen2 = std::max(2, std::min((int) v.comb[1][0].size() - 2, (int) (sr / c2)));
            // ---- the samples
            for (int i = 0; i < m; ++i) {
                float a1L = 0, a1R = 0, a2L = 0, a2R = 0, dL = 0, dR = 0;
                float prev = v.lastOsc;
                for (int k = 0; k < 3; ++k) {
                    OscRun& r = os[k]; if (!r.on) continue;
                    float sL = 0, sR = 0;
                    const float* t0 = r.tab->table(r.f0, r.lvl); const float* t1 = r.tab->table(r.f1, r.lvl);
                    for (int u = 0; u < r.uni; ++u) {
                        double& ph = v.ph[k][u];
                        float p = (float) ph, rp = p;
                        switch (r.warp) {
                            case 1: rp = p * (1.0f + r.wamt * 7.0f); rp -= std::floor(rp); break;
                            case 2: rp = std::pow(p, 1.0f / (1.0f + r.wamt * 3.0f)); break;
                            case 3: rp = std::pow(p, 1.0f + r.wamt * 3.0f); break;
                            case 4: { float dd = 0.5f - r.wamt * 0.45f; rp = p < dd ? p / dd * 0.5f : 0.5f + (p - dd) / (1.0f - dd) * 0.5f; break; }
                            case 5: { float mm = p < 0.5f ? 2 * p : 2 - 2 * p; rp = p + (mm * 0.5f - p) * r.wamt; if (rp < 0) rp += 1; break; }
                            case 6: rp = p + r.wamt * 2.0f * prev; rp -= std::floor(rp); break;
                            case 8: { float st = std::pow(2.0f, 1.0f + (1.0f - r.wamt) * 7.0f); rp = std::floor(p * st) / st; break; }
                            default: break;
                        }
                        float fi = rp * Wavetable::SIZE; int ii = (int) fi; float fr = fi - (float) ii; ii &= Wavetable::SIZE - 1;
                        float a = t0[ii] + (t0[ii + 1] - t0[ii]) * fr, b = t1[ii] + (t1[ii + 1] - t1[ii]) * fr;
                        float s = a + (b - a) * r.ft;
                        if (r.warp == 7) s *= 1.0f - r.wamt + r.wamt * prev;
                        sL += s * r.gainL[u]; sR += s * r.gainR[u];
                        ph += r.inc[u]; if (ph >= 1.0) ph -= 1.0;
                    }
                    v.fmPrev[k] = (sL + sR) * 0.5f;
                    prev = v.fmPrev[k];
                    if (r.route == 0 || r.route == 2) { a1L += sL; a1R += sR; }
                    if (r.route == 1 || r.route == 2) { a2L += sL; a2R += sR; }
                    if (r.route == 3) { dL += sL; dR += sR; }
                }
                v.lastOsc = prev;
                if (subLvl > 0.001f) {
                    v.subPh += subInc; if (v.subPh >= 1) v.subPh -= 1; float sp = (float) v.subPh;
                    float s = (subSh == 0 ? std::sin(kTwoPiF * sp) : subSh == 1 ? (sp < 0.5f ? 0.7f : -0.7f) : (sp < 0.5f ? sp * 4 - 1 : 3 - sp * 4)) * subLvl;
                    if (subRoute == 3) { dL += s; dR += s; } else { if (subRoute != 1) { a1L += s; a1R += s; } if (subRoute == 1 || subRoute == 2) { a2L += s; a2R += s; } }
                }
                if (noiseLvl > 0.001f) {
                    float w = glRng.bi(); v.pink = v.pink * (0.3f + 0.69f * (1.0f - ncol)) + w * (0.7f - 0.69f * (1.0f - ncol) + 0.3f);
                    float nl = (ncol > 0.5f ? w : v.pink) * noiseLvl * 0.5f, nr = nl;
                    if (noiseRoute == 3) { dL += nl; dR += nr; } else { if (noiseRoute != 1) { a1L += nl; a1R += nr; } if (noiseRoute == 1 || noiseRoute == 2) { a2L += nl; a2R += nr; } }
                }
                float in1[2] = { a1L, a1R }, out[2] = { dL, dR };
                for (int c = 0; c < 2; ++c) {
                    float y1 = f1on ? runFilter(v, 0, c, f1t, in1[c], d1, combLen1, r1) : in1[c];
                    float in2 = (c == 0 ? a2L : a2R) + (serial ? y1 : 0.0f);
                    float y2 = f2on ? runFilter(v, 1, c, f2t, in2, d2, combLen2, r2) : in2;
                    out[c] += serial ? y2 : y1 + y2;
                }
                v.combW++;
                float e1 = envTick(v.env[0], 0, (float) (1.0 / sr));
                float g = e1 * amp * 0.35f;
                vbL[(size_t) i] += out[0] * g * pl; vbR[(size_t) i] += out[1] * g * pr;
                if (v.env[0].stage == 0) { v.on = false; break; }
            }
        }
        for (int l = 0; l < 4; ++l) { if (globalLfo[l] < lastGl[l]) sharedHeld[l] = glRng.bi(); lastGl[l] = (float) globalLfo[l]; }
        for (int i = 0; i < m; ++i) { L[i] += vbL[(size_t) i]; R[i] += vbR[(size_t) i]; }
    }
    // views
    for (auto& v : voices) if (v.on) { for (int e = 0; e < 4; ++e) envView[e] = v.env[e].v; for (int l = 0; l < 4; ++l) lfoView[l] = (float) v.lfoPh[l]; break; }
    // the built-in effects (on the whole instrument)
    fxDrive->enabled.store(params[FxDrvOn]->on()); fxChorus->enabled.store(params[FxChoOn]->on());
    fxDelay->enabled.store(params[FxDlyOn]->on()); fxReverb->enabled.store(params[FxRevOn]->on());
    postFx(outL, outR, n, t);
}

float Nova::runFilter(Voice& v, int f, int c, int type, float x, float drive, int combLen, float res) {
    if (drive > 1.01f) x = softClip(x * drive) / std::sqrt(drive);
    SVF& a = f == 0 ? v.f1[c] : v.f2[c]; SVF& b = f == 0 ? v.f1b[c] : v.f2b[c];
    switch (type) {
        case 0: a.tick(x); return a.lp;
        case 1: a.tick(x); b.tick(a.lp); return b.lp;
        case 2: a.tick(x); return a.hp;
        case 3: a.tick(x); b.tick(a.hp); return b.hp;
        case 4: a.tick(x); return a.bpn();
        case 5: a.tick(x); return a.notch();
        case 6: return (f == 0 ? v.l1[c] : v.l2[c]).process(x);
        case 7: case 8: {
            auto& buf = v.comb[f][c]; int sz = (int) buf.size();
            float y = buf[(size_t) ((v.combW - combLen + sz * 4) % sz)];
            float fb = (type == 7 ? 1.0f : -1.0f) * res * 0.97f;
            float w = x + y * fb; buf[(size_t) (v.combW % sz)] = softClip(w);
            return (x + y) * 0.6f;
        }
        default: {
            SVF& fa = f == 0 ? v.fa1[c] : v.fa2[c]; SVF& fb = f == 0 ? v.fb1[c] : v.fb2[c];
            fa.tick(x); fb.tick(x); return (fa.bpn() + fb.bpn() * 0.7f) * 1.5f;
        }
    }
}

void Nova::setFormant(SVF& a, SVF& b, float cutoff, float res) {
    // the cutoff knob walks through the vowels U - O - A - E - I
    static const float F1[5] = { 300, 570, 730, 530, 270 }, F2[5] = { 870, 840, 1090, 1840, 2290 };
    float t = clampf((std::log2(cutoff / 20.0f)) / std::log2(1000.0f), 0, 1) * 4.0f;
    int i = std::min(3, (int) t); float fr = t - (float) i;
    a.set(sr, F1[i] + (F1[i + 1] - F1[i]) * fr, 0.6f + res * 0.35f); b.set(sr, F2[i] + (F2[i + 1] - F2[i]) * fr, 0.6f + res * 0.35f);
}

void Nova::postFx(float* L, float* R, int n, const TimeInfo& t) {
    bool any = false; for (auto* e : internalEffects()) if (e->enabled.load()) any = true;
    if (!any && !fxTail) return;
    for (auto* e : internalEffects()) e->run(L, R, n, t);
    fxTail = any;
}

// ======================================================================== SAMPLER
Sampler::Sampler() : Instrument("smp") {
    add("root", "ROOT", 24, 96, 60, Unit::Note); add("tune", "TUNE", -24, 24, 0, Unit::Semi); add("fine", "FINE", -100, 100, 0, Unit::Cents);
    add("start", "START", 0, 100, 0, Unit::Percent); add("end", "END", 0, 100, 100, Unit::Percent);
    addOnOff("loop", "LOOP"); add("ls", "LOOP START", 0, 100, 0, Unit::Percent); add("le", "LOOP END", 0, 100, 100, Unit::Percent);
    add("xf", "CROSSFADE", 0, 50, 5, Unit::Percent); addOnOff("rev", "REVERSE");
    addChoice("mode", "MODE", { "Keys", "Slices", "One shot" });
    add("nsl", "SLICES", 2, 32, 8, Unit::None); addChoice("slm", "SLICE BY", { "Equal parts", "Transients" });
    add("a", "ATTACK", 0.5f, 10000, 2, Unit::Ms, true); add("d", "DECAY", 1, 10000, 300, Unit::Ms, true);
    add("s", "SUSTAIN", 0, 100, 100, Unit::Percent); add("r", "RELEASE", 1, 20000, 200, Unit::Ms, true);
    addChoice("fty", "FILTER", { "Off", "Low", "High", "Band" }); add("fc", "CUTOFF", 20, 20000, 8000, Unit::Hz, true);
    add("fr", "RESO", 0, 100, 10, Unit::Percent); add("fenv", "FILTER ENV", -100, 100, 0, Unit::Percent);
    add("vel", "VELOCITY", 0, 100, 50, Unit::Percent);
    add("poly", "VOICES", 1, 24, 12, Unit::None); add("lvl", "VOLUME", -36, 12, 0, Unit::dB); add("pan", "PAN", -50, 50, 0, Unit::Pan);
    addArpParams();
}

void Sampler::prepare(double s, int mb) { Instrument::prepare(s, mb); for (auto& x : v) x.on = false; }
void Sampler::setSample(std::shared_ptr<SampleData> s) { std::atomic_store(&pending, std::move(s)); swap.store(true); }
int Sampler::activeVoices() const { int c = 0; for (auto& x : v) if (x.on) ++c; return c; }

void Sampler::noteOn(int note, float vel) {
    auto s = std::atomic_load(&cur); if (!s || s->length() < 4) return;
    int poly = pi("poly"), mode = pi("mode");
    V* pick = nullptr; long best = LONG_MAX;
    for (int i = 0; i < poly && i < (int) v.size(); ++i) { if (!v[(size_t) i].on) { pick = &v[(size_t) i]; break; } if (v[(size_t) i].age < best) { best = v[(size_t) i].age; pick = &v[(size_t) i]; } }
    if (!pick) return;
    V& x = *pick; x = V(); x.on = true; x.note = note; x.vel = vel; x.age = ++age; x.stage = 1; x.env = 0;
    int len = s->length();
    if (mode == 1) {                                           // slices from C1 up
        int k = note - 36, nsl = pi("nsl");
        std::vector<int> pts;
        if (pi("slm") == 1 && s->slices.size() > 1) pts = s->slices; else for (int i = 0; i < nsl; ++i) pts.push_back(len * i / nsl);
        if (k < 0 || k >= (int) pts.size()) { x.on = false; return; }
        x.s0 = pts[(size_t) k]; x.s1 = k + 1 < (int) pts.size() ? pts[(size_t) k + 1] : len;
        x.inc = s->sr / sr * std::pow(2.0, (p("tune") + p("fine") * 0.01) / 12.0);
    } else {
        x.s0 = (int) (p("start") / 100.0f * len); x.s1 = std::max(x.s0 + 2, (int) (p("end") / 100.0f * len));
        double semis = (mode == 2 ? 0 : note - p("root")) + p("tune") + p("fine") * 0.01;
        x.inc = s->sr / sr * std::pow(2.0, semis / 12.0);
        x.loop = find("loop")->on() && mode == 0;
    }
    x.pos = find("rev")->on() ? x.s1 - 1 : x.s0;
    for (auto& f : x.f) f.reset();
}

void Sampler::noteOff(int note) {
    if (pi("mode") == 2) return;                       // one-shot plays to the end
    for (auto& x : v) if (x.on && x.note == note && !x.rel) { x.rel = true; x.stage = 4; }
}
void Sampler::allNotesOff(bool hard) { for (auto& x : v) { if (hard) x.on = false; else if (x.on) { x.rel = true; x.stage = 4; } } }

void Sampler::renderSub(float* L, float* R, int n, const TimeInfo&) {
    if (swap.exchange(false)) { std::atomic_store(&cur, std::atomic_load(&pending)); for (auto& x : v) x.on = false; }
    auto s = std::atomic_load(&cur);
    if (!s || s->length() < 4) return;
    const float* dl = s->L.data(); const float* dr = s->R.data(); int len = s->length();
    float a = (float) (1.0 / (p("a") * 0.001 * sr)), d = (float) (1.0 / (p("d") * 0.001 * sr)), sus = p("s") / 100.0f, r = (float) (1.0 / (p("r") * 0.001 * sr));
    int fty = pi("fty"); float vel = p("vel") / 100.0f, gain = dbToGain(p("lvl")) * 0.8f;
    float pa = (p("pan") / 50.0f + 1) * 0.25f * kPiF, pl = std::cos(pa) * 1.4142f, pr = std::sin(pa) * 1.4142f;
    bool rev = find("rev")->on(); int ls = (int) (p("ls") / 100.0f * len), le = std::max(ls + 64, (int) (p("le") / 100.0f * len)); le = std::min(le, len - 1);
    int xf = (int) ((le - ls) * p("xf") / 100.0f);
    double bendR = std::pow(2.0, bend * 2.0 / 12.0);
    int vi = 0;
    for (auto& x : v) {
        if (!x.on) { if (vi < 4) playPos[vi].store(-1); ++vi; continue; }
        float fc = clampf(p("fc") * std::pow(2.0f, p("fenv") / 100.0f * 5.0f * x.env), 20, (float) (sr * 0.45));
        if (fty > 0) for (auto& f : x.f) f.set(sr, fc, p("fr") / 100.0f);
        float g = gain * (1.0f - vel + vel * x.vel);
        for (int i = 0; i < n; ++i) {
            switch (x.stage) {
                case 1: x.env += a; if (x.env >= 1) { x.env = 1; x.stage = 2; } break;
                case 2: x.env -= d * (x.env - sus + 0.001f); if (x.env <= sus + 0.0005f) { x.env = sus; x.stage = 3; } break;
                case 4: x.env -= r * (x.env + 0.001f); if (x.env <= 0.0002f) { x.on = false; } break;
                default: break;
            }
            if (!x.on) break;
            int i0 = (int) x.pos; float fr = (float) (x.pos - i0);
            if (i0 < 0 || i0 + 1 >= len) { x.on = false; break; }
            float yl = dl[i0] + (dl[i0 + 1] - dl[i0]) * fr, yr = dr[i0] + (dr[i0 + 1] - dr[i0]) * fr;
            // crossfaded loop
            if (x.loop && xf > 0 && !rev && x.pos > le - xf) {
                double p2 = x.pos - (le - ls); int j0 = (int) p2; if (j0 >= 0 && j0 + 1 < len) { float t2 = (float) ((x.pos - (le - xf)) / xf);
                    float zl = dl[j0] + (dl[j0 + 1] - dl[j0]) * fr, zr = dr[j0] + (dr[j0 + 1] - dr[j0]) * fr;
                    yl = yl * (1 - t2) + zl * t2; yr = yr * (1 - t2) + zr * t2; } }
            if (fty > 0) { x.f[0].tick(yl); x.f[1].tick(yr);
                if (fty == 1) { yl = x.f[0].lp; yr = x.f[1].lp; } else if (fty == 2) { yl = x.f[0].hp; yr = x.f[1].hp; } else { yl = x.f[0].bpn(); yr = x.f[1].bpn(); } }
            // short fades at the edges of a slice / sample: no clicks
            double edge = std::min(x.pos - x.s0, (double) x.s1 - x.pos); float ef = edge < 64 ? (float) std::max(0.0, edge / 64.0) : 1.0f;
            if (x.loop) ef = 1.0f;
            float gg = x.env * g * ef;
            L[i] += yl * gg * pl; R[i] += yr * gg * pr;
            double inc = x.inc * bendR;
            if (rev) { x.pos -= inc; if (x.pos <= x.s0) { if (x.loop) x.pos = le; else x.on = false; } }
            else { x.pos += inc;
                if (x.loop && x.pos >= le) x.pos -= (le - ls);
                else if (x.pos >= x.s1 - 1) x.on = false; }
        }
        if (vi < 4) playPos[vi].store(x.on ? (float) (x.pos / len) : -1.0f);
        ++vi;
    }
}

std::vector<int> findOnsets(const SampleData& s, int maxSlices) {
    std::vector<int> out; int n = s.length(); if (n < 2048) return { 0 };
    const int hop = 512; std::vector<float> e; for (int i = 0; i + hop <= n; i += hop) { float a = 0; for (int k = 0; k < hop; ++k) a += std::abs(s.L[(size_t) (i + k)]) + std::abs(s.R[(size_t) (i + k)]); e.push_back(a / hop); }
    out.push_back(0);
    int minGap = (int) (0.08 * s.sr / hop);
    std::vector<std::pair<float, int>> cand;
    for (size_t i = 2; i < e.size(); ++i) { float rise = e[i] - 0.5f * (e[i - 1] + e[i - 2]); if (rise > 0.02f && e[i] > 1.4f * e[i - 1] + 0.003f) cand.push_back({ rise, (int) i }); }
    std::sort(cand.begin(), cand.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<int> picked;
    for (auto& c : cand) { bool ok = true; for (int p : picked) if (std::abs(p - c.second) < minGap) ok = false; if (ok) picked.push_back(c.second); if ((int) picked.size() >= maxSlices - 1) break; }
    std::sort(picked.begin(), picked.end());
    for (int p : picked) if (p > 0) out.push_back(p * hop);
    return out;
}

// ======================================================================== DRUMS
std::vector<std::string> Drums::voiceNames() {
    return { "Kick", "Snare", "Clap", "Closed hat", "Open hat", "Low tom", "Mid tom", "High tom", "Rim", "Cowbell", "Cymbal",
             "Shaker", "Clave", "Conga", "Zap", "Noise sweep" };
}
std::string Drums::padName(int pad, int voice) { (void) pad; auto n = voiceNames(); return voice >= 0 && voice < (int) n.size() ? n[(size_t) voice] : "Sample"; }

Drums::Drums() : Instrument("drm") {
    addChoice("kit", "KIT", drumKitNames());
    auto vn = voiceNames(); vn.push_back("Sample");
    for (int p = 0; p < PADS; ++p) {
        std::string k = "p" + std::to_string(p);
        addChoice(k + "v", "PAD " + std::to_string(p + 1), vn, p);
        add(k + "t", "TUNE", -24, 24, 0, Unit::Semi);
        add(k + "d", "DECAY", 10, 200, 100, Unit::Percent);
        add(k + "c", "TONE", 0, 100, 50, Unit::Percent);
        add(k + "l", "LEVEL", -36, 6, 0, Unit::dB);
        add(k + "p", "PAN", -50, 50, 0, Unit::Pan);
    }
    master = add("lvl", "VOLUME", -36, 6, -9, Unit::dB);
    for (auto& h : hit) h.store(0);
    addArpParams();
}
void Drums::prepare(double s, int mb) { Instrument::prepare(s, mb); for (auto& h : hits) h.on = false; }
void Drums::setPadSample(int pad, std::shared_ptr<SampleData> s) { if (pad >= 0 && pad < PADS) std::atomic_store(&samples[(size_t) pad], std::move(s)); }
int Drums::activeVoices() const { int c = 0; for (auto& h : hits) if (h.on) ++c; return c; }
void Drums::allNotesOff(bool hard) { if (hard) for (auto& h : hits) h.on = false; }

void Drums::noteOn(int note, float vel) {
    int pad = note - 36; if (pad < 0 || pad >= PADS) return;
    int voice = params[(size_t) (1 + pad * 6)]->geti();
    // hats choke each other
    if (voice == 3 || voice == 4) for (auto& h : hits) if (h.on) { int vv = params[(size_t) (1 + h.pad * 6)]->geti(); if (vv == 4 && voice == 3) h.env *= 0.0f, h.on = false; }
    Hit& h = hits[(size_t) nextHit]; nextHit = (nextHit + 1) % (int) hits.size();
    h = Hit(); h.on = true; h.pad = pad; h.vel = vel; h.t = 0; h.rng = Rng((uint32_t) (note * 7919 + nextHit)); h.spos = 0;
    hit[(size_t) pad].store(1.0f);
}

float Drums::renderVoice(Hit& h, int voice, float tune, float decay, float tone, int kit) {
    double t = h.t; float tr = std::pow(2.0f, tune / 12.0f); float dk = decay; float y = 0;
    auto envExp = [&](double tau) { return (float) std::exp(-t / (tau * dk)); };
    float kitDrive = kit == 3 ? 2.0f : 1.0f;
    switch (voice) {
        case 0: {   // kick: a sine falling in pitch + a click
            double f0 = (kit == 1 ? 52 : 48) * tr, f1 = (kit == 0 ? 180 : 260) * tr;
            double f = f0 + (f1 - f0) * std::exp(-t / 0.035); h.ph[0] += f / sr;
            y = std::sin(kTwoPiF * (float) h.ph[0]) * envExp(kit == 0 ? 0.55 : 0.32);
            y += h.rng.bi() * (float) std::exp(-t / 0.003) * (0.3f + tone * 0.5f);
            y = softClip(y * (1.2f + tone) * kitDrive); break; }
        case 1: {   // snare: two tones + filtered noise
            h.ph[0] += 185 * tr / sr; h.ph[1] += 330 * tr / sr;
            float body = (std::sin(kTwoPiF * (float) h.ph[0]) + 0.6f * std::sin(kTwoPiF * (float) h.ph[1])) * envExp(0.07);
            h.f.set(sr, 1800 + tone * 6000, 0.3f); h.f.tick(h.rng.bi());
            y = body * 0.6f + h.f.bpn() * 1.6f * envExp(kit == 2 ? 0.12 : 0.18); break; }
        case 2: {   // clap: noise bursts
            float burst = 0; for (int k = 0; k < 4; ++k) { double tk = t - k * 0.009; if (tk >= 0) burst += (float) std::exp(-tk / (k == 3 ? 0.12 * dk : 0.006)); }
            h.f.set(sr, 1200 + tone * 1500, 0.55f); h.f.tick(h.rng.bi()); y = h.f.bpn() * burst * 1.8f; break; }
        case 3: case 4: case 10: {   // hats / cymbal: six square waves at metallic ratios, high-passed
            static const float RAT[6] = { 2.0f, 3.0f, 4.16f, 5.43f, 6.79f, 8.21f };
            float s = 0; for (int k = 0; k < 6; ++k) { h.ph[k] += 40.0 * RAT[k] * tr * 7.0 / sr; h.ph[k] -= std::floor(h.ph[k]); s += h.ph[k] < 0.5 ? 1.0f : -1.0f; }
            s = s / 6.0f * 0.6f + h.rng.bi() * (kit == 2 ? 0.5f : 0.25f);
            h.f.set(sr, 7000 + tone * 4000, 0.2f); h.f.tick(s);
            double tau = voice == 3 ? 0.045 : voice == 4 ? 0.35 : 1.1;
            y = h.f.hp * envExp(tau) * 1.1f; break; }
        case 5: case 6: case 7: {   // toms
            double base = (voice == 5 ? 90 : voice == 6 ? 130 : 180) * tr; double f = base * (1.0 + 0.6 * std::exp(-t / 0.05)); h.ph[0] += f / sr;
            y = std::sin(kTwoPiF * (float) h.ph[0]) * envExp(0.32) + h.rng.bi() * (float) std::exp(-t / 0.01) * 0.2f * tone; break; }
        case 8: { h.ph[0] += 1700 * tr / sr; y = (std::sin(kTwoPiF * (float) h.ph[0]) * 0.6f + h.rng.bi() * 0.4f) * envExp(0.018) * 1.4f; break; }   // rim
        case 9: { h.ph[0] += 562 * tr / sr; h.ph[1] += 845 * tr / sr; float s = (h.ph[0] - std::floor(h.ph[0]) < 0.5 ? 1.0f : -1.0f) + (h.ph[1] - std::floor(h.ph[1]) < 0.5 ? 1.0f : -1.0f);
            h.f.set(sr, 2600 * tr, 0.5f); h.f.tick(s); y = h.f.bpn() * envExp(0.12) * 0.9f; break; }       // cowbell
        case 11: { h.f.set(sr, 6000 + tone * 4000, 0.3f); h.f.tick(h.rng.bi()); float e = (float) (std::min(1.0, t / 0.012) * std::exp(-t / (0.07 * dk))); y = h.f.hp * e * 0.9f; break; }   // shaker
        case 12: { h.ph[0] += 2500 * tr / sr; y = std::sin(kTwoPiF * (float) h.ph[0]) * envExp(0.025); break; }    // clave
        case 13: { double f = 220 * tr * (1 + 0.3 * std::exp(-t / 0.02)); h.ph[0] += f / sr; y = std::sin(kTwoPiF * (float) h.ph[0]) * envExp(0.18) * 0.9f; break; }   // conga
        case 14: { double f = 2000 * tr * std::exp(-t / 0.03) + 60; h.ph[0] += f / sr; y = std::sin(kTwoPiF * (float) h.ph[0]) * envExp(0.12); break; }    // zap
        case 15: { float fc = (float) (200 + 9000 * std::min(1.0, t / (0.6 * dk))); h.f.set(sr, fc, 0.6f); h.f.tick(h.rng.bi());
            y = h.f.bpn() * (float) (std::min(1.0, t / (0.5 * dk)) * std::exp(-std::max(0.0, t - 0.5 * dk) / 0.08)) * 1.4f; break; }   // noise sweep
        default: break;
    }
    if (kit == 3) { h.z += (std::round(y * 24.0f) / 24.0f - h.z) * 0.6f; y = h.z; }      // lo-fi kit
    return y;
}

void Drums::renderSub(float* L, float* R, int n, const TimeInfo&) {
    int kit = params[0]->geti(); float mg = dbToGain(master->get());
    for (auto& h : hits) {
        if (!h.on) continue;
        int b = 1 + h.pad * 6; int voice = params[(size_t) b]->geti(); float tune = val(b + 1), decay = val(b + 2) / 100.0f, tone = val(b + 3) / 100.0f;
        float g = dbToGain(val(b + 4)) * h.vel * mg * 0.7f; float pa = (val(b + 5) / 50.0f + 1) * 0.25f * kPiF, pl = std::cos(pa) * 1.4142f, pr = std::sin(pa) * 1.4142f;
        auto smp = std::atomic_load(&samples[(size_t) h.pad]);
        double dt = 1.0 / sr; double maxT = voice == 10 ? 4.0 * decay + 0.5 : 2.5 * decay + 0.3;
        for (int i = 0; i < n; ++i) {
            float yl, yr;
            if (voice >= 16) {
                if (!smp || h.spos >= smp->length() - 1) { h.on = false; break; }
                int i0 = (int) h.spos; float fr = (float) (h.spos - i0);
                yl = smp->L[(size_t) i0] + (smp->L[(size_t) i0 + 1] - smp->L[(size_t) i0]) * fr; yr = smp->R[(size_t) i0] + (smp->R[(size_t) i0 + 1] - smp->R[(size_t) i0]) * fr;
                float e = (float) std::exp(-h.t / (2.0 * decay)); yl *= e; yr *= e;
                h.spos += smp->sr / sr * std::pow(2.0, tune / 12.0);
            } else { yl = yr = renderVoice(h, voice, tune, std::max(0.1f, decay), tone, kit); }
            L[i] += yl * g * pl; R[i] += yr * g * pr;
            h.t += dt;
            if (h.t > maxT) { h.on = false; break; }
        }
    }
    for (auto& x : hit) x.store(x.load() * 0.85f);
}

std::vector<std::string> drumKitNames() { return { "808", "909", "Acoustic", "Lo-fi" }; }
void applyDrumKit(Drums& d, int kit) {
    d.find("kit")->set((float) kit);
    // the usual General MIDI-like layout: kick, snare, clap, hats ... (pads 1-16 = notes C1-D#2)
    static const int LAYOUT[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int p = 0; p < Drums::PADS; ++p) {
        std::string k = "p" + std::to_string(p);
        d.find(k + "v")->set((float) LAYOUT[p]); d.find(k + "t")->set(0); d.find(k + "d")->set(kit == 0 ? 110.0f : kit == 2 ? 80.0f : 100.0f);
        d.find(k + "c")->set(kit == 1 ? 70.0f : 50.0f); d.find(k + "l")->set(0); d.find(k + "p")->set(p >= 5 && p <= 7 ? (float) ((p - 6) * 20) : 0.0f);
    }
}

// ======================================================================== the catalogue
std::unique_ptr<Instrument> makeInstrument(const std::string& type) {
    std::unique_ptr<Instrument> i;
    if (type == "nova") i = std::make_unique<Nova>();
    else if (type == "smp") i = std::make_unique<Sampler>();
    else if (type == "drm") { auto d = std::make_unique<Drums>(); applyDrumKit(*d, 0); i = std::move(d); }
    if (i) i->uid = newUid();
    return i;
}
const std::vector<InstrumentInfo>& instrumentCatalog() {
    static const std::vector<InstrumentInfo> c = {
        { "nova", "NOVA", "wavetable synth: 3 oscillators, 2 filters, mod matrix" },
        { "smp", "SAMPLER", "any sound across the keyboard, or sliced" },
        { "drm", "DRUMS", "16 pads: synthesised kits or your samples" } };
    return c;
}

} // namespace score
