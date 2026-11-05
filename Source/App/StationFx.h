#pragma once
// ARCO 2.3: THE STATION'S EFFECTS IN THE STUDIO - every effect of the loop station (its inserts, its send effects and
// the 16 newer ones) as a device on a lane, with the same knobs. The same DSP as the station runs it: one copy per
// device. Included by Studio.cpp only (it brings the station's modules).
#include "StudioFx.h"
#include "../Engine/GenModules.h"
#include "../Engine/ExtraFx.h"
#include "../Model/ParamTable.h"
#include <deque>

namespace arco {

enum StKind { StChan = 0, StMono, StFxb, StShadow, StHalo, StSpace, StExtra };
struct StFxDef { std::string key; juce::String name, desc; int kind; int extra = -1; std::vector<std::string> ids; DevInfo info; };

inline const std::vector<StFxDef>& stationCatalogue() {
    static std::deque<std::string> keep;                  // DevParam holds const char*
    static const std::vector<StFxDef> cat = [] {
        std::vector<StFxDef> v;
        auto def = [](const std::string& id) -> const ParamDef* { for (auto& d : paramTable()) if (id == d.id) return &d; return nullptr; };
        auto skip = [](const std::string& id) { return id == "SEND" || id.rfind("ret_", 0) == 0 || (id.size() > 4 && (id.substr(id.size() - 4) == "_ret" || id.substr(id.size() - 3) == "_sp" || id.substr(id.size() - 4) == "_spc")); };
        static const char* SH[13] = { "off", "-2 OCT", "-OCT", "-6th", "-5th", "-4th", "-3rd", "+3rd", "+4th", "+5th", "+6th", "+OCT", "+2 OCT" };
        auto add = [&](const std::string& key, const char* name, const char* desc, int kind, int extra, const std::vector<std::pair<const char*, const char*>>& knobs) {
            StFxDef f; f.key = key; f.name = name; f.desc = desc; f.kind = kind; f.extra = extra;
            f.info.name = name; f.info.desc = desc;
            bool insert = kind == StChan;
            f.info.p.push_back({ insert ? "AMOUNT" : "MIX", 0, 100, insert ? 100.0f : 50.0f, "%" });
            f.ids.push_back("");
            for (auto& k : knobs) {
                std::string id = k.first;
                if (skip(id) || f.info.p.size() >= 12) continue;
                auto* d = def(id); if (d == nullptr) continue;
                keep.push_back(k.second);
                f.info.p.push_back({ keep.back().c_str(), (float) d->lo, (float) d->hi, (float) d->def, "" });
                f.ids.push_back(id);
            }
            static const char* OO[2] = { "OFF", "HOLD" };
            if (key == "frz") { f.info.p.push_back({ "HOLD", 0, 1, 1, "", false, OO, 2 }); f.ids.push_back("frz_on"); }   // (automate it: the moment it holds)
            if (kind == StShadow) for (int i = 0; i < 2 && f.info.p.size() < 12; ++i) {
                keep.push_back(i == 0 ? "VOICE 1" : "VOICE 2");
                f.info.p.push_back({ keep.back().c_str(), 0, 12, i == 0 ? 10.0f : 7.0f, "", false, SH, 13 });
                f.ids.push_back(i == 0 ? "#v1" : "#v2");
            }
            v.push_back(std::move(f));
        };
        for (auto& c : cardTableBase()) {
            std::string k = c.key;
            std::vector<std::pair<const char*, const char*>> kn; for (auto& x : c.knobs) kn.push_back({ x.id, x.label });
            if (k == "eq" || k == "drv" || k == "trm" || k == "phs" || k == "swl") add(k, c.name, c.desc, StChan, -1, kn);
            else if (k == "cho" || k == "dly" || k == "rev" || k == "frz" || k == "grn") add(k, c.name, c.desc, StMono, -1, kn);
            else if (k == "fxb") add(k, c.name, c.desc, StFxb, -1, kn);
            else if (k == "shd") add(k, c.name, c.desc, StShadow, -1, kn);
            else if (k == "halo") add(k, c.name, c.desc, StHalo, -1, kn);
            else if (k == "spc") add(k, c.name, c.desc, StSpace, -1, kn);
        }
        for (int e = 0; e < (int) extraFxDefs().size() && e < ExtraFx::NFX; ++e) { auto& d = extraFxDefs()[(size_t) e]; add(d.key, d.name, d.desc, StExtra, e, d.knobs); }
        return v;
    }();
    return cat;
}

// a station module run on its own: the input on its first input, the rest silent
template <typename M> struct StMod {
    M m; std::vector<std::vector<float>> ib, ob; std::vector<const float*> ip; std::vector<float*> op;
    void init(double sr) { m.init(sr); ib.assign((size_t) M::numInputs, std::vector<float>(64, 0.0f)); ob.assign((size_t) M::numOutputs, std::vector<float>(64, 0.0f));
        ip.clear(); op.clear(); for (auto& b : ib) ip.push_back(b.data()); for (auto& b : ob) op.push_back(b.data()); }
    void set(const std::string& id, double v) { int i = M::paramIndex(id); if (i >= 0) m.setp(i, v); }
    void run(int n) { for (auto& b : ob) std::fill(b.begin(), b.begin() + n, 0.0f); m.perform(ip.data(), op.data(), n); }
};

class StationRun : public StationRunBase {
public:
    StationRun(int which, double sampleRate) {
        fx = which; sr = sampleRate;
        auto& f = stationCatalogue()[(size_t) which];
        kind = f.kind; key = f.key;
        listen.init(sr);
        switch (kind) {
            case StChan: chan = std::make_unique<StMod<Channels>>(); chan->init(sr);
                for (int s = 0; s < 2; ++s) { chan->set("vol" + std::to_string(s), 71); chan->set("pan" + std::to_string(s), 0); }
                calibrate(); break;
            case StMono:
                if (key == "cho") { cho = std::make_unique<StMod<Chorus>>(); cho->init(sr); }
                else if (key == "dly") { dly = std::make_unique<StMod<Delay>>(); dly->init(sr); }
                else if (key == "rev") { rev = std::make_unique<StMod<Reverse>>(); rev->init(sr); }
                else if (key == "frz") { frz = std::make_unique<StMod<Freeze>>(); frz->init(sr); }
                else if (key == "grn") { grn = std::make_unique<StMod<Grain>>(); grn->init(sr); }
                break;
            case StFxb: fxb = std::make_unique<StMod<FxBox>>(); fxb->init(sr); break;
            case StShadow: shd = std::make_unique<StMod<Shadow>>(); shd->init(sr); break;
            case StHalo: halo = std::make_unique<StMod<Halo>>(); halo->init(sr); halo->set("halo0", 100); break;
            case StSpace: spc = std::make_unique<StMod<Master>>(); spc->init(sr);
                for (auto* r : { "ret_cho", "ret_dly", "ret_halo", "ret_frz", "ret_rev", "ret_res", "ret_grn", "ret_fxb", "ret_drm", "ret_shd", "ret_orb", "glue", "guard" }) spc->set(r, 0);
                spc->set("ret_spc", 100); break;
            case StExtra: xfx = std::make_unique<ExtraFx>(); xfx->init(sr);
                xfx->setp(xfx->paramIndex(key + "0"), 100); xfx->setp(xfx->paramIndex(key + "_ret"), 100); break;
            default: break;
        }
    }
    void process(const StudioDevice& d, float* L, float* R, int n, double bpm, double beat) override {
        auto& f = stationCatalogue()[(size_t) fx];
        // the knobs: sent when they change
        for (size_t k = 1; k < f.ids.size() && k < 12; ++k) if (!sent || d.p[k] != last[k]) {
            const auto& id = f.ids[k];
            if (id == "frz_on") { frzWant = d.p[k] > 0.5f; if (!frzWant && frz) { frz->set("frz_on", 0); frzOn = false; } }
            else if (id == "#v1" || id == "#v2") { shVoices[id == "#v1" ? 0 : 1] = (int) std::lround(d.p[k]); if (shd) for (int v = 0; v < 12; ++v) shd->set("shd_v" + std::to_string(v), (shVoices[0] == v + 1 || shVoices[1] == v + 1) ? 1 : 0); }
            else if (chan) chan->set(id, d.p[k]); else if (cho) cho->set(id, d.p[k]); else if (dly) dly->set(id, d.p[k]); else if (rev) rev->set(id, d.p[k]);
            else if (frz) frz->set(id, d.p[k]); else if (grn) grn->set(id, d.p[k]); else if (fxb) fxb->set(id, d.p[k]); else if (shd) shd->set(id, d.p[k]);
            else if (halo) halo->set(id, d.p[k]); else if (spc) spc->set(id, d.p[k]); else if (xfx) xfx->setp(xfx->paramIndex(id), d.p[k]);
            last[k] = d.p[k];
        }
        if (kind == StChan && (!sent || d.p[0] != last[0])) { for (int s = 0; s < 2; ++s) chan->set(key + std::to_string(s), d.p[0]); last[0] = d.p[0]; }
        sent = true;
        const float mix = d.p[0] / 100.0f;
        float mono[64], wl[64], wr[64];
        for (int i = 0; i < n; ++i) mono[i] = 0.5f * (L[i] + R[i]);
        auto pitchOf = [&]() -> const float* {                      // the note heard (for SHADOW, HALO, the ring modulator / octaver)
            std::copy(mono, mono + n, listen.ib[0].begin()); listen.run(n); return listen.ob[0].data(); };
        std::fill(wl, wl + n, 0.0f); std::fill(wr, wr + n, 0.0f);
        auto two = [&](auto& M, int a, int b) { for (int i = 0; i < n; ++i) { wl[i] = M.ob[(size_t) a][(size_t) i]; wr[i] = M.ob[(size_t) b][(size_t) i]; } };
        switch (kind) {
            case StChan: {                                            // an insert: the sound goes through it (left on strip 1, right on strip 2)
                std::copy(L, L + n, chan->ib[0].begin()); std::copy(R, R + n, chan->ib[1].begin());
                for (int i = 0; i < n; ++i) chan->ib[12][(size_t) i] = (float) std::fmod(beat + i * bpm / 60.0 / sr, 1024.0);
                chan->run(n);
                for (int i = 0; i < n; ++i) { L[i] = chan->ob[0][(size_t) i] * calib; R[i] = chan->ob[1][(size_t) i] * calib; }
                return; }
            case StMono: { auto* m = cho ? (void*) cho.get() : nullptr; (void) m;
                if (cho) { std::copy(mono, mono + n, cho->ib[0].begin()); cho->run(n); two(*cho, 0, 1); }
                else if (dly) { std::copy(mono, mono + n, dly->ib[0].begin()); dly->run(n); two(*dly, 0, 1); }
                else if (rev) { std::copy(mono, mono + n, rev->ib[0].begin()); rev->run(n); two(*rev, 0, 1); }
                else if (frz) {
                    // HOLD takes what it has heard: it waits for some sound first (holding silence would hold nothing)
                    for (int i = 0; i < n; ++i) if (std::abs(mono[i]) > 1e-3f) ++frzHeard;
                    if (frzWant && !frzOn && frzHeard > (long) (0.35 * sr)) { frz->set("frz_on", 1); frzOn = true; }
                    std::copy(mono, mono + n, frz->ib[0].begin()); frz->run(n); two(*frz, 0, 1); }
                else if (grn) { std::copy(mono, mono + n, grn->ib[0].begin()); grn->run(n); two(*grn, 0, 1); }
                break; }
            case StFxb: std::copy(mono, mono + n, fxb->ib[0].begin()); for (int i = 0; i < n; ++i) fxb->ib[1][(size_t) i] = (float) std::fmod(beat + i * bpm / 60.0 / sr, 1024.0); fxb->run(n); two(*fxb, 0, 1); break;
            case StShadow: { auto* p = pitchOf(); std::copy(mono, mono + n, shd->ib[0].begin()); std::copy(p, p + n, shd->ib[1].begin()); shd->run(n); two(*shd, 0, 1); break; }
            case StHalo: { auto* p = pitchOf(); std::copy(mono, mono + n, halo->ib[0].begin()); std::copy(p, p + n, halo->ib[9].begin()); halo->run(n); two(*halo, 0, 1); break; }
            case StSpace: std::copy(mono, mono + n, spc->ib[10].begin()); spc->run(n); two(*spc, 0, 1); break;
            case StExtra: {
                const float* ch[12]; ch[0] = mono; for (int s = 1; s < 12; ++s) ch[s] = zero;
                float bp[64]; for (int i = 0; i < n; ++i) bp[i] = (float) bpm;
                const float* p = pitchOf();
                float sp[64] = {}; float* ring[ExtraFx::NFX] = {};
                xfx->process(ch, wl, wr, sp, n, bp, p, ring, nullptr);
                break; }
            default: break;
        }
        for (int i = 0; i < n; ++i) { L[i] += wl[i] * mix; R[i] += wr[i] * mix; }    // a send effect: its sound added to the dry sound
    }
private:
    double sr = 48000; int kind = 0; std::string key;
    bool sent = false; bool frzWant = false, frzOn = false; long frzHeard = 0; std::array<float, 12> last {}; int shVoices[2] = { 11, 8 };
    float calib = 1.0f; float zero[64] = {};
    StMod<Listen> listen;
    std::unique_ptr<StMod<Channels>> chan; std::unique_ptr<StMod<Chorus>> cho; std::unique_ptr<StMod<Delay>> dly; std::unique_ptr<StMod<Reverse>> rev;
    std::unique_ptr<StMod<Freeze>> frz; std::unique_ptr<StMod<Grain>> grn; std::unique_ptr<StMod<FxBox>> fxb; std::unique_ptr<StMod<Shadow>> shd;
    std::unique_ptr<StMod<Halo>> halo; std::unique_ptr<StMod<Master>> spc; std::unique_ptr<ExtraFx> xfx;
    // the strip's own level (its fader at 71 = its normal level): measured once, so the insert alone changes the sound
    void calibrate() {
        for (int s = 0; s < 2; ++s) chan->set(key + std::to_string(s), 0);
        double in2 = 0, out2 = 0; long t = 0;
        for (int b = 0; b < 160; ++b) {
            for (int i = 0; i < 64; ++i, ++t) { float x = 0.3f * (float) std::sin(2 * M_PI * 440.0 * t / sr); chan->ib[0][(size_t) i] = x; chan->ib[1][(size_t) i] = x; }
            chan->run(64);
            if (b >= 100) for (int i = 0; i < 64; ++i) { in2 += (double) chan->ib[0][(size_t) i] * chan->ib[0][(size_t) i]; out2 += (double) chan->ob[0][(size_t) i] * chan->ob[0][(size_t) i]; }
        }
        calib = out2 > 1e-12 ? (float) std::sqrt(in2 / out2) : 1.0f;
        chan->init(sr);
        for (int s = 0; s < 2; ++s) { chan->set("vol" + std::to_string(s), 71); chan->set("pan" + std::to_string(s), 0); }
    }
};

inline std::unique_ptr<StationRunBase> makeStationRunImpl(int which, double sampleRate) {
    if (which < 0 || which >= (int) stationCatalogue().size()) return nullptr;
    return std::make_unique<StationRun>(which, sampleRate);
}

} // namespace arco
