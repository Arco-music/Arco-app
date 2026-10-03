// SCORE - NOVA's factory sounds.
#include "Instruments.h"
#include <map>

namespace score {

namespace {
// tables
enum { tBasic, tPulse, tHarm, tSync, tVowel, tOrgan, tBowed, tHollow, tPad, tMetal, tDigital, tNoisy, tGlass };
// modulation sources / targets
enum { sEnv2 = 1, sEnv3, sEnv4, sLfo1, sLfo2, sLfo3, sLfo4, sVel, sNote, sWheel, sPress, sBend, sRand, sM1, sM2, sM3, sM4, sEnv1 };
enum { dAP = 1, dAPos, dALvl, dAWarp, dAPan, dBP, dBPos, dBLvl, dBWarp, dBPan, dCP, dCPos, dCLvl, dCWarp, dCPan, dSub, dNoise,
       dF1C, dF1R, dF1D, dF2C, dF2R, dF2D, dAmp, dPan, dL1, dL2, dL3, dL4, dPitch, dDet };
// filter types
enum { fLP12, fLP24, fHP12, fHP24, fBP, fNotch, fLadder, fCombP, fCombN, fFormant };
// divisions
enum { v4bar, v2bar, v1bar, v2, v2d, v2t, v4, v4d, v4t, v8, v8d, v8t, v16, v16d, v16t, v32 };

using V = std::vector<std::pair<std::string, float>>;
// modulation slot k
V slot(int k, int src, int dst, float amt) { auto p = "ms" + std::to_string(k); return { { p + "s", (float) src }, { p + "d", (float) dst }, { p + "a", amt } }; }
V operator+(V a, const V& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
// every preset starts from: slot 0 cleared (the default ENV2 > cutoff is only in INIT)
V clear0() { return slot(0, 0, 0, 0); }
}

const std::vector<NovaPreset>& novaPresets() {
    static const std::vector<NovaPreset> P = {
        { "Init", "BASIC", {} },
        // ---------------------------------------------------------------- PADS
        { "Halo Pad", "PADS", clear0() + V { { "o1tab", tPad }, { "o1pos", 40 }, { "o1uni", 4 }, { "o1det", 25 }, { "o1spr", 85 },
            { "o2on", 1 }, { "o2tab", tPad }, { "o2pos", 70 }, { "o2oct", 1 }, { "o2lvl", 40 }, { "o2uni", 3 }, { "o2det", 30 },
            { "f1ty", fLP12 }, { "f1c", 1800 }, { "f1r", 20 }, { "e1a", 1200 }, { "e1d", 1000 }, { "e1s", 85 }, { "e1r", 3200 },
            { "l1rate", 0.15f }, { "l2rate", 0.09f }, { "l1mode", 0 }, { "l2mode", 0 },
            { "fxcho", 1 }, { "fxrev", 1 }, { "rev:mix", 40 }, { "rev:decay", 6 } } + slot(1, sLfo1, dAPos, 30) + slot(2, sLfo2, dF1C, 12) },
        { "Glass Choir", "PADS", clear0() + V { { "o1tab", tVowel }, { "o1pos", 20 }, { "o1uni", 5 }, { "o1det", 30 }, { "o2on", 1 }, { "o2tab", tGlass },
            { "o2pos", 50 }, { "o2oct", 1 }, { "o2lvl", 30 }, { "f1c", 6000 }, { "e1a", 800 }, { "e1r", 2500 }, { "e1s", 90 }, { "l1rate", 0.2f }, { "l1mode", 0 },
            { "fxrev", 1 }, { "rev:mix", 45 }, { "rev:decay", 8 }, { "fxcho", 1 } } + slot(1, sLfo1, dAPos, 40) },
        { "Bowed Air", "PADS", clear0() + V { { "o1tab", tBowed }, { "o1pos", 30 }, { "o1uni", 3 }, { "o1det", 15 }, { "noise", 15 }, { "ncol", 30 },
            { "f1ty", fLP12 }, { "f1c", 2500 }, { "f1r", 10 }, { "e1a", 400 }, { "e1s", 90 }, { "e1r", 900 }, { "l1rate", 5.2f },
            { "fxrev", 1 }, { "rev:mix", 30 } } + slot(1, sEnv2, dF1C, 20) + slot(2, sLfo1, dPitch, 0.5f) },
        { "Dark Matter", "PADS", clear0() + V { { "o1tab", tNoisy }, { "o1pos", 60 }, { "o1uni", 6 }, { "o1det", 40 }, { "sub", 30 },
            { "f1ty", fLP24 }, { "f1c", 420 }, { "f1r", 40 }, { "e1a", 2000 }, { "e1s", 100 }, { "e1r", 4000 }, { "l1rate", 0.05f }, { "l1mode", 0 },
            { "fxrev", 1 }, { "rev:mix", 50 }, { "rev:decay", 12 } } + slot(1, sLfo1, dF1C, 30) },
        { "Slow Bloom", "PADS", clear0() + V { { "o1tab", tHarm }, { "o1pos", 0 }, { "o1uni", 3 }, { "o1det", 20 }, { "e2a", 4000 }, { "e2d", 6000 }, { "e2s", 60 },
            { "f1ty", fLP12 }, { "f1c", 3000 }, { "e1a", 2500 }, { "e1r", 4000 }, { "e1s", 90 }, { "fxcho", 1 }, { "fxrev", 1 }, { "rev:mix", 40 } } + slot(1, sEnv2, dAPos, 80) },
        { "Drone", "PADS", clear0() + V { { "o1tab", tHollow }, { "o1pos", 50 }, { "o1oct", -1 }, { "o1uni", 4 }, { "o1det", 18 }, { "o2on", 1 }, { "o2tab", tBowed },
            { "o2lvl", 40 }, { "o2uni", 3 }, { "o2oct", 0 }, { "l1rate", 0.03f }, { "l2rate", 0.07f }, { "l1mode", 0 }, { "l2mode", 0 },
            { "f1ty", fLP12 }, { "f1c", 1500 }, { "e1a", 3000 }, { "e1s", 100 }, { "e1r", 6000 }, { "fxrev", 1 }, { "rev:mix", 50 }, { "rev:decay", 15 } }
            + slot(1, sLfo1, dAPos, 60) + slot(2, sLfo2, dBPos, 50) },
        // ---------------------------------------------------------------- KEYS
        { "Soft Keys", "KEYS", clear0() + V { { "o1pos", 10 }, { "o2on", 1 }, { "o2pos", 40 }, { "o2oct", 1 }, { "o2lvl", 25 }, { "f1ty", fLP12 }, { "f1c", 3000 },
            { "e1a", 2 }, { "e1d", 1500 }, { "e1s", 30 }, { "e1r", 600 }, { "e2a", 1 }, { "e2d", 800 }, { "e2s", 10 }, { "fxcho", 1 } } + slot(1, sEnv2, dF1C, 40) + slot(2, sVel, dF1C, 25) },
        { "Glass Keys", "KEYS", clear0() + V { { "o1tab", tGlass }, { "o1pos", 30 }, { "o2on", 1 }, { "o2pos", 0 }, { "o2oct", 2 }, { "o2lvl", 20 },
            { "e1a", 1 }, { "e1d", 2000 }, { "e1s", 0 }, { "e1r", 1200 }, { "fxrev", 1 }, { "rev:mix", 30 } } },
        { "Organ", "KEYS", clear0() + V { { "o1tab", tOrgan }, { "o1pos", 60 }, { "f1on", 0 }, { "e1a", 5 }, { "e1d", 10 }, { "e1s", 100 }, { "e1r", 80 },
            { "fxcho", 1 }, { "cho:rate", 5.5f }, { "cho:depth", 30 }, { "fxdrv", 1 }, { "drv:dr", 6 } } },
        { "E-Piano", "KEYS", clear0() + V { { "o1pos", 3 }, { "o2on", 1 }, { "o2pos", 3 }, { "o2oct", 2 }, { "o2lvl", 30 }, { "o2warp", 6 }, { "o2wamt", 25 },
            { "f1c", 5000 }, { "e1a", 1 }, { "e1d", 2500 }, { "e1s", 20 }, { "e1r", 500 }, { "vel", 80 }, { "fxcho", 1 } } + slot(1, sVel, dBLvl, 40) },
        { "Tape Keys", "KEYS", clear0() + V { { "o1pos", 20 }, { "f1c", 3500 }, { "l1rate", 0.6f }, { "l1mode", 0 }, { "e1d", 1200 }, { "e1s", 40 },
            { "fxdrv", 1 }, { "drv:dr", 8 }, { "fxcho", 1 } } + slot(1, sLfo1, dPitch, 0.6f) },
        // ---------------------------------------------------------------- BASS
        { "Sub Bass", "BASS", clear0() + V { { "o1pos", 0 }, { "sub", 40 }, { "f1ty", fLP12 }, { "f1c", 300 }, { "e1a", 3 }, { "e1d", 300 }, { "e1s", 100 }, { "e1r", 120 },
            { "vmode", 1 }, { "glide", 30 } } },
        { "Acid Bass", "BASS", clear0() + V { { "o1pos", 60 }, { "f1ty", fLadder }, { "f1c", 380 }, { "f1r", 75 }, { "f1k", 20 },
            { "e2a", 1 }, { "e2d", 220 }, { "e2s", 0 }, { "e2r", 150 }, { "e1s", 90 }, { "vmode", 2 }, { "glide", 60 }, { "fxdrv", 1 }, { "drv:dr", 14 } }
            + slot(1, sEnv2, dF1C, 60) + slot(2, sVel, dF1C, 20) },
        { "Reese", "BASS", clear0() + V { { "o1pos", 66 }, { "o1uni", 4 }, { "o1det", 35 }, { "o1spr", 40 }, { "f1c", 900 }, { "f1r", 25 },
            { "l1rate", 0.3f }, { "vmode", 1 }, { "fxdrv", 1 } } + slot(1, sLfo1, dF1C, 15) },
        { "Pluck Bass", "BASS", clear0() + V { { "o1tab", tPulse }, { "o1pos", 30 }, { "f1c", 1500 }, { "f1r", 30 }, { "e2d", 180 }, { "e2s", 0 },
            { "e1d", 400 }, { "e1s", 0 }, { "e1r", 120 }, { "vmode", 1 } } + slot(1, sEnv2, dF1C, 55) },
        { "Wobble", "BASS", clear0() + V { { "o1tab", tSync }, { "o1pos", 40 }, { "o1uni", 2 }, { "f1c", 600 }, { "f1r", 50 },
            { "l1sync", 1 }, { "l1div", v8 }, { "l1mode", 1 }, { "vmode", 1 }, { "fxdrv", 1 } } + slot(1, sLfo1, dF1C, 60) },
        // ---------------------------------------------------------------- LEADS
        { "Supersaw", "LEADS", clear0() + V { { "o1pos", 66 }, { "o1uni", 8 }, { "o1det", 40 }, { "o1spr", 90 }, { "o2on", 1 }, { "o2pos", 66 }, { "o2oct", 1 },
            { "o2lvl", 35 }, { "o2uni", 4 }, { "f1ty", fLP12 }, { "f1c", 6000 }, { "e1a", 5 }, { "e1s", 90 }, { "e1r", 300 },
            { "fxcho", 1 }, { "fxdly", 1 }, { "dly:div", v8d }, { "fxrev", 1 } } },
        { "Sync Lead", "LEADS", clear0() + V { { "o1tab", tSync }, { "o1pos", 30 }, { "f1c", 5000 }, { "vmode", 2 }, { "glide", 40 }, { "e2a", 5 }, { "e2d", 800 }, { "e2s", 30 },
            { "fxdly", 1 } } + slot(1, sEnv2, dAPos, 50) },
        { "Square Lead", "LEADS", clear0() + V { { "o1tab", tPulse }, { "o1pos", 0 }, { "f1c", 4000 }, { "l1rate", 6 }, { "glide", 20 }, { "vmode", 2 },
            { "fxdly", 1 } } + slot(1, sLfo1, dPitch, 0.6f) + slot(2, sWheel, dF1C, 30) },
        { "Cello Shadow", "LEADS", clear0() + V { { "o1tab", tBowed }, { "o1pos", 50 }, { "vmode", 2 }, { "glide", 80 }, { "l1rate", 5.5f }, { "l1mode", 0 },
            { "f1ty", fLP12 }, { "f1c", 3500 }, { "f1k", 50 }, { "e1a", 120 }, { "e1s", 95 }, { "e1r", 400 }, { "fxrev", 1 }, { "rev:mix", 30 } }
            + slot(1, sLfo1, dPitch, 0.5f) + slot(2, sWheel, dPitch, 0.0f) },
        { "Vowel Lead", "LEADS", clear0() + V { { "o1tab", tVowel }, { "o1pos", 30 }, { "f1on", 0 }, { "l1rate", 0.8f }, { "vmode", 2 }, { "glide", 25 }, { "fxdly", 1 } }
            + slot(1, sLfo1, dAPos, 40) },
        // ---------------------------------------------------------------- PLUCKS
        { "Crystal Pluck", "PLUCKS", clear0() + V { { "o1tab", tGlass }, { "o1pos", 40 }, { "f1c", 2500 }, { "e2d", 250 }, { "e2s", 0 }, { "e1a", 1 }, { "e1d", 600 },
            { "e1s", 0 }, { "e1r", 500 }, { "fxdly", 1 }, { "fxrev", 1 } } + slot(1, sEnv2, dF1C, 60) },
        { "Kalimba", "PLUCKS", clear0() + V { { "o1pos", 0 }, { "o2on", 1 }, { "o2tab", tMetal }, { "o2pos", 20 }, { "o2oct", 2 }, { "o2lvl", 20 },
            { "f1ty", fLP12 }, { "f1c", 4000 }, { "e1d", 900 }, { "e1s", 0 }, { "e1r", 700 }, { "fxrev", 1 }, { "rev:mix", 20 } } },
        { "Harp", "PLUCKS", clear0() + V { { "o1pos", 25 }, { "f1c", 3000 }, { "e2d", 400 }, { "e2s", 0 }, { "e1d", 2500 }, { "e1s", 0 }, { "e1r", 1500 }, { "fxrev", 1 } }
            + slot(1, sEnv2, dF1C, 50) },
        { "Arp Pluck", "PLUCKS", clear0() + V { { "o1pos", 66 }, { "o1uni", 2 }, { "f1c", 1200 }, { "f1r", 35 }, { "e2d", 150 }, { "e2s", 0 }, { "e1d", 300 },
            { "e1s", 0 }, { "e1r", 200 }, { "arp", 1 }, { "arpm", 0 }, { "arpr", v16 }, { "arpo", 1 }, { "fxdly", 1 }, { "dly:div", v8 } } + slot(1, sEnv2, dF1C, 60) },
        { "Pizzicato", "PLUCKS", clear0() + V { { "o1tab", tBowed }, { "o1pos", 70 }, { "e1a", 1 }, { "e1d", 350 }, { "e1s", 0 }, { "e1r", 300 }, { "f1c", 3000 },
            { "e2d", 120 }, { "e2s", 0 }, { "fxrev", 1 } } + slot(1, sEnv2, dF1C, 40) },
        // ---------------------------------------------------------------- BELLS
        { "Bell", "BELLS", clear0() + V { { "o1tab", tMetal }, { "o1pos", 50 }, { "o2on", 1 }, { "o2pos", 0 }, { "o2oct", 1 }, { "o2lvl", 40 }, { "o2warp", 6 }, { "o2wamt", 60 },
            { "f1on", 0 }, { "e1a", 1 }, { "e1d", 4000 }, { "e1s", 0 }, { "e1r", 3000 }, { "fxrev", 1 } } },
        { "Music Box", "BELLS", clear0() + V { { "o1tab", tGlass }, { "o1pos", 70 }, { "o1oct", 1 }, { "e1d", 1200 }, { "e1s", 0 }, { "e1r", 800 }, { "fxrev", 1 } } },
        { "Gamelan", "BELLS", clear0() + V { { "o1tab", tMetal }, { "o1pos", 80 }, { "o2on", 1 }, { "o2tab", tMetal }, { "o2pos", 30 }, { "o2semi", 7 }, { "o2lvl", 40 },
            { "f1on", 0 }, { "e1d", 3000 }, { "e1s", 0 }, { "e1r", 2000 }, { "fxrev", 1 } } },
        // ---------------------------------------------------------------- STRINGS & BRASS
        { "String Ensemble", "STRINGS", clear0() + V { { "o1pos", 66 }, { "o1uni", 6 }, { "o1det", 25 }, { "o1spr", 80 }, { "o2on", 1 }, { "o2tab", tBowed }, { "o2lvl", 30 },
            { "f1ty", fLP12 }, { "f1c", 4000 }, { "f1k", 40 }, { "e1a", 600 }, { "e1s", 90 }, { "e1r", 1200 }, { "fxcho", 1 }, { "fxrev", 1 } } },
        { "Brass", "STRINGS", clear0() + V { { "o1pos", 66 }, { "o1uni", 2 }, { "o1det", 10 }, { "f1c", 800 }, { "f1r", 10 }, { "e2a", 60 }, { "e2d", 500 }, { "e2s", 40 },
            { "e1a", 40 }, { "e1s", 90 }, { "e1r", 200 } } + slot(1, sEnv2, dF1C, 55) + slot(2, sVel, dF1C, 30) },
        { "Choir Ah", "STRINGS", clear0() + V { { "o1pos", 66 }, { "o1uni", 4 }, { "o1det", 20 }, { "f1ty", fFormant }, { "f1c", 600 }, { "f1r", 40 }, { "l1rate", 5 },
            { "e1a", 500 }, { "e1s", 95 }, { "e1r", 1500 }, { "fxrev", 1 }, { "rev:mix", 35 } } + slot(1, sLfo1, dPitch, 0.3f) },
        { "Hollow Flute", "STRINGS", clear0() + V { { "o1pos", 10 }, { "noise", 10 }, { "ncol", 20 }, { "f1ty", fLP12 }, { "f1c", 2500 }, { "l1rate", 5 },
            { "e1a", 80 }, { "e1s", 90 }, { "e1r", 200 }, { "vmode", 2 } } + slot(1, sLfo1, dPitch, 0.25f) },
        { "Big Stab", "STRINGS", clear0() + V { { "o1pos", 66 }, { "o1uni", 6 }, { "o1det", 30 }, { "o2on", 1 }, { "o2pos", 66 }, { "o2oct", 1 }, { "o2uni", 3 },
            { "f1c", 2000 }, { "e2d", 300 }, { "e2s", 0 }, { "e1d", 500 }, { "e1s", 0 }, { "e1r", 400 }, { "fxrev", 1 } } + slot(1, sEnv2, dF1C, 50) },
        // ---------------------------------------------------------------- TEXTURES & FX
        { "Wind", "TEXTURES", clear0() + V { { "o1on", 0 }, { "noise", 80 }, { "nflt", 0 }, { "f1ty", fBP }, { "f1c", 800 }, { "f1r", 70 }, { "f1k", 0 },
            { "l1rate", 0.1f }, { "l2sh", 6 }, { "l2rate", 0.37f }, { "l1mode", 0 }, { "l2mode", 0 }, { "e1a", 1500 }, { "e1s", 100 }, { "e1r", 3000 }, { "fxrev", 1 } }
            + slot(1, sLfo1, dF1C, 50) + slot(2, sLfo2, dF1C, 20) },
        { "Rain Glass", "TEXTURES", clear0() + V { { "o1tab", tGlass }, { "o1pos", 60 }, { "e1d", 150 }, { "e1s", 0 }, { "e1r", 150 },
            { "arp", 1 }, { "arpm", 3 }, { "arpr", v32 }, { "arpo", 2 }, { "fxdly", 1 }, { "fxrev", 1 }, { "rev:mix", 45 } } },
        { "Riser", "TEXTURES", clear0() + V { { "o1tab", tHarm }, { "noise", 60 }, { "e2a", 8000 }, { "e2s", 100 }, { "f1c", 300 }, { "e1a", 6000 }, { "e1s", 100 }, { "e1r", 800 },
            { "fxrev", 1 } } + slot(1, sEnv2, dF1C, 80) + slot(2, sEnv2, dAPos, 100) },
        { "Laser Zap", "TEXTURES", clear0() + V { { "o1pos", 0 }, { "e3a", 0.5f }, { "e3d", 200 }, { "e3s", 0 }, { "e1d", 250 }, { "e1s", 0 }, { "vmode", 1 }, { "f1on", 0 } }
            + slot(1, sEnv3, dPitch, 100) },
        { "Digital Swarm", "TEXTURES", clear0() + V { { "o1tab", tDigital }, { "o1pos", 40 }, { "o1uni", 6 }, { "o1det", 60 }, { "l1sh", 5 }, { "l1rate", 8 }, { "l1mode", 0 },
            { "f1c", 3000 }, { "fxrev", 1 } } + slot(1, sLfo1, dAPos, 60) },
    };
    return P;
}

// each sound's volume, measured so that all of them are about equally loud
static const std::map<std::string, float> LEVEL = {
        { "Init", -7.6f },
        { "Halo Pad", 0.9f },
        { "Glass Choir", -3.3f },
        { "Bowed Air", -8.4f },
        { "Dark Matter", -9.9f },
        { "Slow Bloom", -4.2f },
        { "Drone", -10.9f },
        { "Soft Keys", -4.7f },
        { "Glass Keys", 3.1f },
        { "Organ", -1.8f },
        { "E-Piano", -7.2f },
        { "Tape Keys", -6.0f },
        { "Sub Bass", -1.9f },
        { "Acid Bass", -9.6f },
        { "Reese", -7.4f },
        { "Pluck Bass", 10.8f },
        { "Wobble", -2.8f },
        { "Supersaw", -9.6f },
        { "Sync Lead", 2.1f },
        { "Square Lead", 9.8f },
        { "Cello Shadow", -1.6f },
        { "Vowel Lead", 3.4f },
        { "Crystal Pluck", 11.0f },
        { "Kalimba", -7.6f },
        { "Harp", -6.6f },
        { "Arp Pluck", -0.8f },
        { "Pizzicato", -5.3f },
        { "Bell", -5.6f },
        { "Music Box", 9.7f },
        { "Gamelan", -1.6f },
        { "String Ensemble", -10.4f },
        { "Brass", -7.8f },
        { "Choir Ah", -7.2f },
        { "Hollow Flute", -1.3f },
        { "Big Stab", -7.1f },
        { "Wind", 5.2f },
        { "Rain Glass", 12.0f },
        { "Riser", 1.3f },
        { "Laser Zap", 0.0f },
        { "Digital Swarm", -4.4f } };

void applyNovaPreset(Nova& n, const NovaPreset& p) {
    n.resetToDefaults();
    for (auto* e : n.internalEffects()) e->resetToDefaults();
    n.fxDrive->find("dr")->set(10); n.fxDrive->find("out")->set(-4); n.fxReverb->find("mix")->set(25);
    for (auto& kv : p.values) {
        auto c = kv.first.find(':');
        if (c != std::string::npos) {
            std::string fx = kv.first.substr(0, c), id = kv.first.substr(c + 1);
            Effect* e = fx == "drv" ? n.fxDrive.get() : fx == "cho" ? n.fxChorus.get() : fx == "dly" ? n.fxDelay.get() : n.fxReverb.get();
            if (auto* q = e->find(id)) q->set(kv.second);
            continue;
        }
        if (auto* q = n.find(kv.first)) q->set(kv.second);
    }
    auto it = LEVEL.find(p.name);
    if (it != LEVEL.end()) n.find("lvl")->set(it->second);
}

} // namespace score
