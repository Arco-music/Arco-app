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

enum ModType { MKeys = 0, MVco, MLfo, MEnv, MVcf, MVca, MMix, MNoise, MSah, MSeq, MClock, MDelay, MOut, MAtt, MQuant, MSlew, MFold,
               // 2.3: many more (the order is kept: saved patches find their modules by name)
               MSupersaw, MFmOp, MWave, MString, MKick, MSnare, MHat,
               MSvf, MComb, MFormant, MEq3,
               MDrive, MCrush, MRectify, MComp,
               MReverb, MChorus, MPhaser, MPan, MStMix,
               MAr, MRandom, MChaos, MFollow,
               MDivider, MEuclid, MBernoulli, MArp, MCounter,
               MChord, MTranspose,
               MRing, MInvert, MXfade, MSwitch, MCompare, MMinMax, MConst, MLogic,
               MIn, MScope, MMix8, MTypes };
// the library's shelves
enum ModCat { CatSound = 0, CatDrum, CatFilter, CatShape, CatFx, CatMove, CatTime, CatPitch, CatMath, CatIO, CatCount };
inline const char* modCatName(int c) { static const char* N[CatCount] = { "Sound", "Drums", "Filters", "Shape", "Effects", "Movement", "Rhythm & time", "Notes", "Math & logic", "In & out" }; return N[juce::jlimit(0, CatCount - 1, c)]; }
int modCategory(int type);

struct ModPortDef { const char* name; };
struct ModParamDef { const char* name; float lo, hi, def; bool log = false; const char* const* choices = nullptr; int nChoices = 0; };
struct ModDef { const char* name; const char* desc; std::vector<const char*> ins, outs; std::vector<ModParamDef> params; int width; };

inline const std::vector<ModDef>& modDefs() {
    static const char* LFOSYNC[8] = { "FREE", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16" };
    static const char* DIVS[6] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    static const char* SCALES[6] = { "CHROMATIC", "MAJOR", "MINOR", "PENTA", "DORIAN", "BLUES" };
    static const char* KEYS[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* ARPM[4] = { "UP", "DOWN", "UP-DOWN", "RANDOM" };
    static const char* CHORDS[8] = { "MAJOR", "MINOR", "7", "MAJ 7", "MIN 7", "SUS 2", "SUS 4", "DIM" };
    static const char* INSRC[6] = { "THROUGH", "LIVE 1", "LIVE 2", "LIVE 3", "LIVE 4", "STATION" };
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
        // ---- 2.3
        { "SUPERSAW", "seven saws, wide (trance, pads)", { "PITCH" }, { "L", "R" }, { { "OCT", -4, 4, 0 }, { "DETUNE", 0, 100, 35 }, { "SIDES", 0, 100, 70 }, { "VOICES", 1, 7, 7 } }, 140 },
        { "FM OP", "two operators: bells, basses, metal", { "PITCH", "INDEX" }, { "OUT" }, { { "OCT", -4, 4, 0 }, { "RATIO", 0.5f, 16, 2, true }, { "INDEX", 0, 10, 2 }, { "FEEDBACK", 0, 100, 0 } }, 140 },
        { "WAVE", "one oscillator that morphs sine > tri > saw > square", { "PITCH", "MORPH" }, { "OUT" }, { { "OCT", -4, 4, 0 }, { "TUNE", -12, 12, 0 }, { "MORPH", 0, 3, 0 }, { "MORPH CV", 0, 100, 50 } }, 130 },
        { "STRING", "a plucked string (tap the trigger)", { "TRIG", "PITCH" }, { "OUT" }, { { "OCT", -3, 3, 0 }, { "DECAY", 0, 100, 70 }, { "BRIGHT", 0, 100, 60 } }, 120 },
        { "KICK", "a bass drum", { "TRIG", "TUNE" }, { "OUT" }, { { "TUNE", 30, 120, 52 }, { "DECAY", 0.05f, 2, 0.45f, true }, { "PUNCH", 0, 100, 60 }, { "DRIVE", 0, 100, 20 } }, 120 },
        { "SNARE", "a snare drum", { "TRIG" }, { "OUT" }, { { "TONE", 100, 400, 190 }, { "SNAPPY", 0, 100, 60 }, { "DECAY", 0.03f, 1, 0.18f, true } }, 120 },
        { "HAT", "a hi-hat, closed or open", { "TRIG" }, { "OUT" }, { { "DECAY", 0.01f, 1.5f, 0.06f, true }, { "TONE", 2000, 14000, 7000, true }, { "METAL", 0, 100, 50 } }, 110 },
        { "MULTI FILTER", "low, band, high, notch - morph between them", { "IN", "CUTOFF", "RESO" }, { "OUT", "LP", "BP", "HP" },
          { { "CUTOFF", 20, 18000, 1000, true }, { "RESO", 0, 100, 25 }, { "TYPE", 0, 3, 0 }, { "CV AMT", -100, 100, 60 } }, 150 },
        { "COMB", "metallic resonance, flanging", { "IN", "TIME" }, { "OUT" }, { { "FREQ", 20, 4000, 220, true }, { "FEEDBACK", -95, 95, 70 }, { "MIX", 0, 100, 50 } }, 120 },
        { "VOWEL", "the sound says a e i o u", { "IN", "VOWEL" }, { "OUT" }, { { "VOWEL", 0, 4, 0 }, { "RESO", 0, 100, 60 }, { "MIX", 0, 100, 100 } }, 120 },
        { "EQ", "low, mid, high", { "IN" }, { "OUT" }, { { "LOW", -18, 18, 0 }, { "MID", -18, 18, 0 }, { "MID FREQ", 200, 5000, 1000, true }, { "HIGH", -18, 18, 0 } }, 130 },
        { "DRIVE", "warm to wild: soft, hard, fold, tube", { "IN", "CV" }, { "OUT" }, { { "DRIVE", 0, 100, 40 }, { "MODE", 0, 3, 0 }, { "TONE", 0, 100, 60 }, { "LEVEL", 0, 100, 70 } }, 120 },
        { "CRUSH", "fewer bits, a lower rate: crunchy", { "IN" }, { "OUT" }, { { "BITS", 1, 16, 8 }, { "DOWN", 1, 64, 4 }, { "MIX", 0, 100, 100 } }, 110 },
        { "RECTIFY", "folds the negative half up", { "IN" }, { "FULL", "HALF" }, {}, 90 },
        { "COMPRESSOR", "evens the level (a side input: ducking)", { "IN", "SIDE" }, { "OUT", "GR" }, { { "THRESH", -40, 0, -18 }, { "RATIO", 1, 20, 4, true }, { "ATTACK", 0.1f, 100, 5, true }, { "RELEASE", 10, 1000, 120, true }, { "MAKE-UP", 0, 24, 4 } }, 150 },
        { "REVERB", "a room to a hall (stereo)", { "L", "R" }, { "L", "R" }, { { "SIZE", 0, 100, 60 }, { "DAMP", 0, 100, 40 }, { "MIX", 0, 100, 35 } }, 130 },
        { "CHORUS", "width and movement (stereo)", { "IN" }, { "L", "R" }, { { "RATE", 0.05f, 5, 0.5f, true }, { "DEPTH", 0, 100, 40 }, { "MIX", 0, 100, 50 } }, 120 },
        { "PHASER", "sweeping notches", { "IN", "RATE" }, { "OUT" }, { { "RATE", 0.02f, 5, 0.3f, true }, { "DEPTH", 0, 100, 70 }, { "FEEDBACK", 0, 95, 40 }, { "MIX", 0, 100, 50 } }, 130 },
        { "PAN", "left - right (a CV moves it)", { "IN", "POS" }, { "L", "R" }, { { "POS", -100, 100, 0 }, { "CV AMT", 0, 100, 50 } }, 100 },
        { "STEREO MIXER", "four sounds, each with its place", { "1", "2", "3", "4" }, { "L", "R" },
          { { "1", 0, 100, 70 }, { "2", 0, 100, 70 }, { "3", 0, 100, 70 }, { "4", 0, 100, 70 }, { "PAN 1", -100, 100, -40 }, { "PAN 2", -100, 100, 40 }, { "PAN 3", -100, 100, -15 }, { "PAN 4", -100, 100, 15 } }, 170 },
        { "AR", "attack - release; loops as a slow wave", { "GATE" }, { "ENV", "END" }, { { "ATTACK", 0.001f, 10, 0.05f, true }, { "RELEASE", 0.005f, 10, 0.5f, true }, { "LOOP", 0, 1, 0 } }, 120 },
        { "RANDOM", "random voltages: stepped and smooth", { "CLOCK" }, { "STEP", "SMOOTH", "GATE" }, { { "RATE", 0.05f, 30, 2, true }, { "RANGE", 0, 10, 5 }, { "SMOOTH", 0, 100, 50 } }, 120 },
        { "CHAOS", "never repeats (a strange attractor)", { "RATE" }, { "X", "Y", "Z" }, { { "RATE", 0.01f, 10, 0.5f, true }, { "CHAOS", 0, 100, 60 } }, 110 },
        { "FOLLOWER", "the loudness of a sound, as a voltage", { "IN" }, { "ENV", "GATE" }, { { "ATTACK", 0.1f, 200, 5, true }, { "RELEASE", 5, 2000, 150, true }, { "GAIN", 0, 400, 100 } }, 120 },
        { "DIVIDER", "slower clocks from a clock", { "CLOCK", "RESET" }, { "/2", "/3", "/4", "/8" }, {}, 100 },
        { "EUCLID", "even rhythms: hits spread over steps", { "CLOCK", "RESET" }, { "GATE", "ACCENT" },
          { { "STEPS", 1, 32, 16 }, { "HITS", 0, 32, 5 }, { "ROTATE", 0, 31, 0 }, { "RATE", 0, 5, 4, false, DIVS, 6 } }, 140 },
        { "CHANCE", "a coin toss for every trigger", { "TRIG" }, { "A", "B" }, { { "CHANCE", 0, 100, 50 } }, 90 },
        { "ARP", "an arpeggiator for the keys you hold", { "CLOCK" }, { "PITCH", "GATE" },
          { { "MODE", 0, 3, 0, false, ARPM, 4 }, { "OCTAVES", 1, 4, 2 }, { "RATE", 0, 5, 4, false, DIVS, 6 }, { "GATE %", 5, 100, 50 } }, 140 },
        { "COUNTER", "counts triggers, a gate every N", { "CLOCK", "RESET" }, { "COUNT", "EVERY" }, { { "LENGTH", 1, 32, 4 } }, 100 },
        { "CHORD", "one note becomes a chord", { "PITCH" }, { "1", "2", "3", "4" }, { { "TYPE", 0, 7, 0, false, CHORDS, 8 }, { "INVERT", 0, 3, 0 } }, 120 },
        { "TRANSPOSE", "up or down by semitones / octaves", { "IN" }, { "OUT" }, { { "SEMI", -12, 12, 0 }, { "OCT", -4, 4, 0 } }, 100 },
        { "MULTIPLY", "A x B (ring), A + B", { "A", "B" }, { "A x B", "A + B" }, {}, 100 },
        { "INVERT", "upside down", { "IN" }, { "-IN", "10 - IN" }, {}, 90 },
        { "CROSSFADE", "from A to B", { "A", "B", "MIX" }, { "OUT" }, { { "MIX", 0, 100, 50 } }, 110 },
        { "SWITCH", "A, or B while the gate is high", { "A", "B", "GATE" }, { "OUT" }, {}, 90 },
        { "COMPARE", "a gate when A is above B (or a level)", { "A", "B" }, { "GATE", "NOT" }, { { "LEVEL", -10, 10, 1 } }, 100 },
        { "MIN MAX", "the lower and the higher", { "A", "B" }, { "MIN", "MAX" }, {}, 90 },
        { "VALUES", "three knobs as voltages (sliders for a patch)", {}, { "A", "B", "C" }, { { "A", -10, 10, 0 }, { "B", -10, 10, 0 }, { "C", -10, 10, 0 } }, 120 },
        { "LOGIC", "and, or, xor, not", { "A", "B" }, { "AND", "OR", "XOR", "NOT A" }, {}, 100 },
        { "IN", "a sound in: what goes through, a LIVE input or the station", {}, { "L", "R" }, { { "FROM", 0, 5, 0, false, INSRC, 6 }, { "GAIN", 0, 200, 100 } }, 120 },
        { "SCOPE", "see a signal (it passes through)", { "A", "B" }, { "A" }, { { "TIME", 1, 100, 20 } }, 170 },
        { "MIXER 8", "eight into one", { "1", "2", "3", "4", "5", "6", "7", "8" }, { "MIX" },
          { { "1", 0, 100, 70 }, { "2", 0, 100, 70 }, { "3", 0, 100, 70 }, { "4", 0, 100, 70 }, { "5", 0, 100, 70 }, { "6", 0, 100, 70 }, { "7", 0, 100, 70 }, { "8", 0, 100, 70 } }, 170 },
    };
    return d;
}

struct ModModule { int type = MVco; int id = 0; std::array<float, 16> p {}; float x = -1, y = -1; };   // 2.3: x, y - its place on the LAB's canvas
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
    // 2.3: sounds coming in (the IN module): what goes through (as an effect) and the station's LIVE inputs / its mix
    const float* thruL = nullptr; const float* thruR = nullptr;
    const float* live[4] {}; const float* mixL = nullptr; const float* mixR = nullptr;
    // the SCOPE's last moments (for its screen): per scope, 2 x 256 values
    struct ScopeView { int id = 0; float a[256] {}, b[256] {}; std::atomic<int> w { 0 }; };
    ScopeView scopes[8];
    const ScopeView* scopeOf(int id) const { for (auto& s : scopes) if (s.id == id) return &s; return nullptr; }
    // ---- save / open
    static juce::var toVar(const ModPatch& p);
    static ModPatch fromVar(const juce::var& v);
    static ModPatch defaultPatch(int which);
    static juce::StringArray patchNames();
    std::atomic<float> outLevel { 0 };
    // the values at the ports (for the window's lights)
    float valueAt(int id, int port) const { for (auto& r : rt) if (r.id == id && port >= 0 && port < 8) return r.out[port]; return 0; }
private:
    double sr = 48000;
    struct Rt {
        int id = 0, type = 0; std::array<float, 16> p {};
        float out[8] {};
        int inMod[8] { -1, -1, -1, -1, -1, -1, -1, -1 }, inPort[8] {};      // index into rt of the source, port
        double ph = 0, ph2 = 0; float z[16] {}; double d[8] {}; float env = 0; int stage = 0; bool lastGate = false, lastTrig = false; float held = 0, slew = 0;
        int scope = -1; int pos[16] {};
        int step = 0; double clk = 0; float pink[3] {}; juce::Random rnd;
        std::vector<float> buf; int bw = 0;
    };
    std::vector<Rt> rt;
    void rebuild();                                      // under the lock
    void stepMore(Rt& m, int i, double bt, float isr, float& outL, float& outR);   // 2.3: the newer modules (one sample)
    int blockN = 0; int blockI = 0;
    int arpNotes[16] {}; int arpCount = 0;           // the keys held, low to high (for ARP)
    juce::SpinLock noteLock; juce::Array<int> held; int curNote = 60; float curVel = 0.8f; bool gateOn = false; int trigCount = 0, lastTrigCount = 0;
    float keysPitch = 0; double ownBeat = 0;
    float in(const Rt& m, int k) const { return m.inMod[k] >= 0 ? rt[(size_t) m.inMod[k]].out[m.inPort[k]] : 0.0f; }
    bool connected(const Rt& m, int k) const { return m.inMod[k] >= 0; }
};

} // namespace arco
