#pragma once
// ARCO 1.2: the parameters and cards of the new effects (they run in Engine/ExtraFx.h, message bus "st_xfx").
// Every new effect is a send effect: each strip has its own SEND (saved with the scene), the effect's own
// knobs are saved with the song. They are hidden until chosen in SETUP > EFFECTS.
// (included at the end of ParamTable.h)
#include <deque>
#include <JuceHeader.h>
#include "../Engine/Synth2.h"

namespace arco {

struct ExtraFxDef {
    const char* key; const char* name; const char* desc;
    std::vector<std::pair<const char*, const char*>> knobs;     // param id (or "SEND"), label
    std::vector<double> defs;                                  // defaults for the knobs after SEND
};

inline const std::vector<ExtraFxDef>& extraFxDefs() {
    static const std::vector<ExtraFxDef> d = {
        { "flg", "FLANGER", "jet sweeps, in tempo or free", { { "SEND", "SEND" }, { "flg_rate", "RATE" }, { "flg_depth", "DEPTH" }, { "flg_fb", "FEEDBACK" }, { "flg_man", "MANUAL" }, { "flg_ret", "RETURN" } },
          { 30, 60, 40, 35, 70 } },
        { "png", "PING-PONG", "echoes bouncing left - right", { { "SEND", "SEND" }, { "png_time", "TIME" }, { "png_fb", "FEEDBACK" }, { "png_tone", "TONE" }, { "png_width", "WIDTH" }, { "png_ret", "RETURN" }, { "png_spc", "→ SPACE" } },
          { 50, 45, 60, 90, 70, 15 } },
        { "wah", "AUTO-WAH", "a wah that follows your playing", { { "SEND", "SEND" }, { "wah_sens", "SENSE" }, { "wah_range", "RANGE" }, { "wah_reso", "RESO" }, { "wah_mode", "ENV / LFO" }, { "wah_ret", "RETURN" } },
          { 55, 60, 55, 0, 75 } },
        { "oct", "OCTAVER", "an octave below, above, a fifth", { { "SEND", "SEND" }, { "oct_down", "-1 OCT" }, { "oct_up", "+1 OCT" }, { "oct_fifth", "+5TH" }, { "oct_tone", "TONE" }, { "oct_ret", "RETURN" } },
          { 70, 0, 0, 55, 70 } },
        { "tap", "TAPE", "warm tape: saturation, wow, flutter", { { "SEND", "SEND" }, { "tap_drive", "DRIVE" }, { "tap_wow", "WOW" }, { "tap_flut", "FLUTTER" }, { "tap_tone", "TONE" }, { "tap_ret", "RETURN" } },
          { 35, 25, 20, 60, 70 } },
        { "flt", "FILTER", "resonant filter sweeping in tempo", { { "SEND", "SEND" }, { "flt_type", "LOW/BAND/HIGH" }, { "flt_cut", "CUTOFF" }, { "flt_res", "RESO" }, { "flt_rate", "RATE" }, { "flt_depth", "DEPTH" }, { "flt_ret", "RETURN" } },
          { 0, 45, 40, 40, 50, 75 } },
        { "rng", "RING MOD", "metallic bells, can follow your note", { { "SEND", "SEND" }, { "rng_freq", "FREQ" }, { "rng_follow", "FOLLOW" }, { "rng_shape", "SHAPE" }, { "rng_tone", "TONE" }, { "rng_ret", "RETURN" } },
          { 35, 0, 0, 70, 60 } },
        { "stu", "STUTTER", "beat-repeat glitches in tempo", { { "SEND", "SEND" }, { "stu_div", "GRID" }, { "stu_chance", "CHANCE" }, { "stu_len", "REPEATS" }, { "stu_pitch", "PITCH DROP" }, { "stu_ret", "RETURN" } },
          { 50, 35, 40, 0, 80 } },
        { "vib", "ROTARY", "a spinning speaker - slow / fast", { { "SEND", "SEND" }, { "vib_speed", "SLOW/FAST" }, { "vib_drive", "DRIVE" }, { "vib_horn", "HORN/DRUM" }, { "vib_width", "WIDTH" }, { "vib_ret", "RETURN" } },
          { 0, 25, 60, 80, 75 } },
        { "crs", "CRUSH", "bits and samples, crunchy", { { "SEND", "SEND" }, { "crs_bits", "BITS" }, { "crs_rate", "DOWNSAMPLE" }, { "crs_tone", "TONE" }, { "crs_ret", "RETURN" } },
          { 40, 35, 65, 60 } },
    };
    return d;
}

// the parameters: 12 strip sends per effect (scene) + the effect knobs (song)
inline const std::vector<ParamDef>& extraParams() {
    static std::deque<std::string> names;          // keeps the id strings alive (ParamDef holds const char*)
    static const std::vector<ParamDef> p = [] {
        std::vector<ParamDef> v;
        for (auto& e : extraFxDefs()) {
            for (int s = 0; s < 12; ++s) { names.push_back(std::string(e.key) + std::to_string(s)); v.push_back({ names.back().c_str(), { "st_xfx" }, 0.0, 0.0, 100.0, 0, 0 }); }
            for (size_t k = 1; k < e.knobs.size(); ++k) {
                double lo = 0, hi = 100;
                if (std::string(e.knobs[k].first) == "flg_fb") lo = -100;
                v.push_back({ e.knobs[k].first, { "st_xfx" }, e.defs[k - 1], lo, hi, 1, 0 });
            }
        }
        return v;
    }();
    return p;
}

// the mixing desk (AUTO MIX, STUDIO): per strip, saved with the scene; the master polish with the song
inline const std::vector<ParamDef>& mixParams() {
    static std::deque<std::string> names;
    static const std::vector<ParamDef> p = [] {
        std::vector<ParamDef> v;
        struct F { const char* k; double def, lo, hi; };
        const F fs[] = { { "hp", 0, 0, 400 }, { "lsg", 0, -15, 15 }, { "lsf", 120, 30, 500 }, { "pkg", 0, -15, 15 }, { "pkf", 400, 60, 12000 }, { "pkq", 1, 0.2, 8 },
                         { "hsg", 0, -15, 15 }, { "hsf", 8000, 1500, 16000 }, { "cth", 0, -48, 0 }, { "crt", 1, 1, 20 }, { "cat", 10, 0.1, 100 }, { "crl", 120, 10, 1000 },
                         { "cmk", 0, 0, 24 }, { "trim", 0, -24, 12 } };
        for (int s = 0; s < 12; ++s)
            for (auto& f : fs) { names.push_back(std::string("mx_") + f.k + std::to_string(s)); v.push_back({ names.back().c_str(), { "st_mix" }, f.def, f.lo, f.hi, 0, 0 }); }
        v.push_back({ "ms_lsg", { "st_mst" }, 0, -6, 6, 1, 0 });
        v.push_back({ "ms_hsg", { "st_mst" }, 0, -6, 6, 1, 0 });
        v.push_back({ "ms_gain", { "st_mst" }, 0, -12, 18, 1, 0 });
        v.push_back({ "ms_lim", { "st_mst" }, 0, 0, 1, 1, 1 });
        v.push_back({ "ms_ceil", { "st_mst" }, -1, -6, 0, 1, 0 });
        return v;
    }();
    return p;
}

// SYNTH 2: the new parameters (the old SYNTH's stay as they were), saved with the song
inline const std::vector<ParamDef>& synthParams() {
    static std::deque<std::string> names;
    static const std::vector<ParamDef> p = [] {
        std::vector<ParamDef> v;
        auto add = [&](const std::string& id, double def, double lo, double hi, int kind) { names.push_back(id); v.push_back({ names.back().c_str(), { "st_syn" }, def, lo, hi, 1, kind }); };
        add("c_wave", 25, 0, 100, 0); add("c_warp", 0, 0, 100, 0); add("c_semi", 12, -24, 24, 2); add("c_fine", 0, -50, 50, 0);
        add("c_lvl", 0, 0, 100, 0); add("c_uni", 1, 1, 5, 2); add("c_det", 25, 0, 100, 0);
        add("a_mode", 0, 0, 3, 2); add("b_mode", 0, 0, 3, 2); add("c_mode", 0, 0, 3, 2);
        add("f_slope", 0, 0, 1, 1);
        add("e3_a", 20, 0, 100, 0); add("e3_d", 50, 0, 100, 0); add("e3_s", 0, 0, 100, 0); add("e3_r", 40, 0, 100, 0);
        add("l2_rate", 20, 0, 100, 0); add("l2_shape", 0, 0, 100, 0); add("l2_sync", 0, 0, 1, 1);
        add("l3_rate", 10, 0, 100, 0); add("l3_shape", 90, 0, 100, 0); add("l3_sync", 0, 0, 1, 1);
        for (int m = 0; m < kModSlots; ++m) {
            add("m" + std::to_string(m) + "_src", 0, 0, (double) synthModSources().size() - 1, 2);
            add("m" + std::to_string(m) + "_dst", 0, 0, (double) synthModDests().size() - 1, 2);
            add("m" + std::to_string(m) + "_amt", 0, -100, 100, 0);
        }
        for (int m = 1; m <= 4; ++m) add("mc" + std::to_string(m), 0, 0, 100, 0);
        add("arp_on", 0, 0, 1, 1); add("arp_mode", 0, 0, 5, 2); add("arp_rate", 3, 0, 6, 2); add("arp_oct", 1, 1, 4, 2); add("arp_gate", 60, 10, 100, 0); add("arp_hold", 0, 0, 1, 1);
        add("vel_amt", 65, 0, 100, 0); add("bend_rng", 2, 1, 12, 2);
        return v;
    }();
    return p;
}

inline const std::vector<ParamDef>& paramTable() {
    static const std::vector<ParamDef> t = [] { auto v = paramTableBase(); for (auto& d : extraParams()) v.push_back(d); for (auto& d : mixParams()) v.push_back(d); for (auto& d : synthParams()) v.push_back(d); return v; }();
    return t;
}

inline const std::vector<CardDef>& cardTable() {
    static const std::vector<CardDef> t = [] {
        auto v = cardTableBase();
        for (auto& e : extraFxDefs()) {
            CardDef c { e.key, e.name, e.desc, 3, {} };
            for (auto& k : e.knobs) c.knobs.push_back({ k.first, k.second });
            v.push_back(c);
        }
        return v;
    }();
    return t;
}

// the cards shown on a fresh install: the original 18
inline juce::StringArray defaultVisibleCards() {
    juce::StringArray a; for (auto& c : cardTableBase()) a.add(c.key); return a;
}

} // namespace arco
