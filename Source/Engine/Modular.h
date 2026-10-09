#pragma once
// ARCO 1.3: MODULAR - a patchable modular synthesizer (ARCO's own, in the spirit of a Eurorack system / VCV Rack).
// Modules with knobs, inputs and outputs; cables carry voltages: 1 V per octave (0 V = C4), gates 0 / 10 V,
// audio +-5 V. Every cable reads the latest value (a cable to an earlier module arrives one sample later, like hardware).
// The OUT module plays into the station (the same way as the SYNTH: into a LIVE strip), so it can be looped and recorded.
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace arco {

enum ModType { MKeys = 0, MVco, MLfo, MEnv, MVcf, MVca, MMix, MNoise, MSah, MSeq, MClock, MDelay, MOut, MAtt, MQuant, MSlew, MFold, MTypes };

struct ModPortDef { const char* name; };
struct ModParamDef { const char* name; float lo, hi, def; bool log = false; const char* const* choices = nullptr; int nChoices = 0; };
struct ModDef { const char* name; const char* desc; std::vector<const char*> ins, outs; std::vector<ModParamDef> params; int width; };

inline const std::vector<ModDef>& modDefs() {
    static const char* LFOSYNC[8] = { "FREE", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16" };
    static const char* DIVS[6] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    static const char* SCALES[6] = { "CHROMATIC", "MAJOR", "MINOR", "PENTA", "DORIAN", "BLUES" };
    static const char* KEYS[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const std::vector<ModDef> d = {
        { "KEYS", "the keyboard: pitch, gate, velocity", {}, { "PITCH", "GATE", "VEL" }, { { "OCTAVE", -3, 3, 0 }, { "GLIDE", 0, 100, 0 } }, 120 },
        { "VCO", "oscillator", { "PITCH", "FM", "PWM", "SYNC" }, { "SINE", "TRI", "SAW", "SQR" },
          { { "OCT", -4, 4, 0 }, { "TUNE", -12, 12, 0 }, { "FINE", -100, 100, 0 }, { "FM", 0, 100, 0 }, { "PW", 5, 95, 50 } }, 170 },
        { "LFO", "slow movement", { "RATE", "RESET" }, { "SINE", "TRI", "SAW", "SQR", "S&H" },
          { { "RATE", 0.02f, 40, 1, true }, { "TEMPO", 0, 7, 0, false, LFOSYNC, 8 }, { "BIPOLAR", 0, 1, 1 } }, 150 },
        { "ENV", "envelope (ADSR)", { "GATE", "RETRIG" }, { "ENV", "INV" },
          { { "ATTACK", 0.001f, 8, 0.01f, true }, { "DECAY", 0.005f, 10, 0.3f, true }, { "SUSTAIN", 0, 100, 60 }, { "RELEASE", 0.005f, 12, 0.4f, true } }, 150 },
        { "VCF", "filter (4-pole ladder + state variable)", { "IN", "CUTOFF", "RESO" }, { "LP", "BP", "HP" },
          { { "CUTOFF", 20, 18000, 1200, true }, { "RESO", 0, 100, 20 }, { "CV AMT", -100, 100, 60 }, { "DRIVE", 0, 100, 10 } }, 150 },
        { "VCA", "amplifier", { "IN", "CV" }, { "OUT" }, { { "GAIN", 0, 100, 0 }, { "CV AMT", 0, 100, 100 } }, 110 },
        { "MIXER", "four into one", { "1", "2", "3", "4" }, { "MIX" }, { { "1", 0, 100, 70 }, { "2", 0, 100, 70 }, { "3", 0, 100, 70 }, { "4", 0, 100, 70 } }, 130 },
        { "NOISE", "white and pink", {}, { "WHITE", "PINK" }, {}, 90 },
        { "S&H", "sample and hold", { "IN", "TRIG" }, { "OUT" }, {}, 90 },
        { "SEQ", "8-step sequencer", { "CLOCK", "RESET" }, { "PITCH", "GATE" },
          { { "1", -12, 24, 0 }, { "2", -12, 24, 3 }, { "3", -12, 24, 7 }, { "4", -12, 24, 10 }, { "5", -12, 24, 12 }, { "6", -12, 24, 10 }, { "7", -12, 24, 7 }, { "8", -12, 24, 3 },
            { "STEPS", 1, 8, 8 }, { "GATE %", 5, 100, 50 }, { "RATE", 0, 5, 3, false, DIVS, 6 } }, 300 },
        { "CLOCK", "the tempo of the loops", {}, { "x1", "x2", "x4", "/2" }, { { "DIV", 0, 5, 2, false, DIVS, 6 } }, 110 },
        { "DELAY", "echo", { "IN", "TIME" }, { "OUT" }, { { "TIME", 0.01f, 2, 0.375f, true }, { "FEEDBACK", 0, 95, 45 }, { "MIX", 0, 100, 40 } }, 130 },
        { "OUT", "to the station (LIVE strip)", { "L", "R" }, {}, { { "LEVEL", 0, 100, 70 } }, 100 },
        { "ATTENUATE", "scale and offset", { "A", "B" }, { "A", "B" }, { { "GAIN A", -100, 100, 100 }, { "OFFSET A", -10, 10, 0 }, { "GAIN B", -100, 100, 100 }, { "OFFSET B", -10, 10, 0 } }, 130 },
        { "QUANTIZER", "pitches into a scale", { "IN" }, { "OUT" }, { { "SCALE", 0, 5, 1, false, SCALES, 6 }, { "KEY", 0, 11, 0, false, KEYS, 12 } }, 110 },
        { "SLEW", "smooth changes", { "IN" }, { "OUT" }, { { "RISE", 0, 2, 0.05f }, { "FALL", 0, 2, 0.05f } }, 100 },
        { "FOLDER", "wave folding", { "IN", "CV" }, { "OUT" }, { { "FOLD", 0, 100, 30 }, { "SYM", -100, 100, 0 } }, 100 },
    };
    return d;
}

struct ModModule { int type = MVco; int id = 0; std::array<float, 16> p {}; };
struct ModCable { int fromMod = 0, fromPort = 0, toMod = 0, toPort = 0; int colour = 0; };
struct ModPatch { std::vector<ModModule> mods; std::vector<ModCable> cables; int nextId = 1; };

class Modular {
public:
    Modular() { setPatch(defaultPatch(0)); }
    // ---- the patch (message thread, under the lock)
    juce::CriticalSection lock;
    ModPatch patch;
    void setPatch(const ModPatch& p) { const juce::ScopedLock sl(lock); patch = p; rebuild(); }
    int addModule(int type) {
        const juce::ScopedLock sl(lock);
        ModModule m; m.type = type; m.id = patch.nextId++;
        auto& D = modDefs()[(size_t) type]; for (size_t i = 0; i < D.params.size(); ++i) m.p[i] = D.params[i].def;
        patch.mods.push_back(m); rebuild(); return m.id;
    }
    void removeModule(int id) {
        const juce::ScopedLock sl(lock);
        patch.mods.erase(std::remove_if(patch.mods.begin(), patch.mods.end(), [id](const ModModule& m) { return m.id == id; }), patch.mods.end());
        patch.cables.erase(std::remove_if(patch.cables.begin(), patch.cables.end(), [id](const ModCable& c) { return c.fromMod == id || c.toMod == id; }), patch.cables.end());
        rebuild();
    }
    void connect(int fromMod, int fromPort, int toMod, int toPort) {
        const juce::ScopedLock sl(lock);
        patch.cables.erase(std::remove_if(patch.cables.begin(), patch.cables.end(), [&](const ModCable& c) { return c.toMod == toMod && c.toPort == toPort; }), patch.cables.end());
        static int col = 0;
        patch.cables.push_back({ fromMod, fromPort, toMod, toPort, (col++) % 6 }); rebuild();
    }
    void disconnect(int toMod, int toPort) {
        const juce::ScopedLock sl(lock);
        patch.cables.erase(std::remove_if(patch.cables.begin(), patch.cables.end(), [&](const ModCable& c) { return c.toMod == toMod && c.toPort == toPort; }), patch.cables.end()); rebuild();
    }
    void setParam(int id, int k, float v) {   // (no lock: a knob turning never interrupts the sound)
        for (auto& m : patch.mods) if (m.id == id) m.p[(size_t) k] = v;
        for (auto& r : rt) if (r.id == id) r.p[(size_t) k] = v; }
    // ---- notes (any thread) - mono, the last note wins
    void noteOn(int note, float vel) { const juce::SpinLock::ScopedLockType sl(noteLock); held.removeAllInstancesOf(note); held.add(note); curNote = note; curVel = vel; gateOn = true; ++trigCount; }
    void noteOff(int note) { const juce::SpinLock::ScopedLockType sl(noteLock); held.removeAllInstancesOf(note); if (held.isEmpty()) gateOn = false; else curNote = held.getLast(); }
    std::atomic<bool> running { true };
    // ---- audio
    void prepare(double sampleRate) { sr = sampleRate; const juce::ScopedLock sl(lock); rebuild(); }
    void process(float* L, float* R, int n, double bpm, double beat);   // adds; beat = the loop's beat position (or our own clock)
    // ---- save / open
    static juce::var toVar(const ModPatch& p);
    static ModPatch fromVar(const juce::var& v);
    static ModPatch defaultPatch(int which);
    static juce::StringArray patchNames();
    std::atomic<float> outLevel { 0 };
    // the values at the ports (for the window's lights)
    float valueAt(int id, int port) const { for (auto& r : rt) if (r.id == id && port >= 0 && port < 5) return r.out[port]; return 0; }
private:
    double sr = 48000;
    struct Rt {
        int id = 0, type = 0; std::array<float, 16> p {};
        float out[5] {};
        int inMod[4] { -1, -1, -1, -1 }, inPort[4] {};      // index into rt of the source, port
        double ph = 0, ph2 = 0; float z[8] {}; float env = 0; int stage = 0; bool lastGate = false, lastTrig = false; float held = 0, slew = 0;
        int step = 0; double clk = 0; float pink[3] {}; juce::Random rnd;
        std::vector<float> buf; int bw = 0;
    };
    std::vector<Rt> rt;
    void rebuild();                                      // under the lock
    juce::SpinLock noteLock; juce::Array<int> held; int curNote = 60; float curVel = 0.8f; bool gateOn = false; int trigCount = 0, lastTrigCount = 0;
    float keysPitch = 0; double ownBeat = 0;
    float in(const Rt& m, int k) const { return m.inMod[k] >= 0 ? rt[(size_t) m.inMod[k]].out[m.inPort[k]] : 0.0f; }
    bool connected(const Rt& m, int k) const { return m.inMod[k] >= 0; }
};

} // namespace arco
