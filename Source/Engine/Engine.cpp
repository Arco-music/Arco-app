#include "Engine.h"
#include <chrono>
#include <cstring>
#include <algorithm>

namespace arco {

static std::vector<std::string> names(const std::string& prefix, int a, int b) {
    std::vector<std::string> v; for (int i = a; i <= b; ++i) v.push_back(prefix + std::to_string(i)); return v; }
static std::vector<std::string> cat(std::initializer_list<std::vector<std::string>> l) {
    std::vector<std::string> v; for (auto& x : l) v.insert(v.end(), x.begin(), x.end()); return v; }

float* Engine::B(const std::string& n) {
    if (n.empty() || n == "-") return nullptr;
    auto it = bus.find(n);
    if (it == bus.end()) it = bus.emplace(n, std::vector<float>(SUB, 0.0f)).first;
    return it->second.data();
}

Engine::Engine() {
    for (auto& r : grid) for (auto& c : r) c.store(0.0f);
}

// one module: its message bus, its input and output buses
template <typename M>
static void addNode(Engine*, std::vector<std::unique_ptr<IModule>>& mods, std::map<std::string, int>& busToMod,
                    std::vector<std::function<void()>>& graph, std::function<float*(const std::string&)> B,
                    const std::string& msgbus, std::vector<std::string> ins, std::vector<std::string> outs) {
    auto* mod = new Mod<M>();
    mods.emplace_back(mod);
    if (!msgbus.empty()) busToMod[msgbus] = (int) mods.size() - 1;
    std::vector<const float*> ip; std::vector<float*> op;
    for (auto& n : ins) ip.push_back(B(n));
    for (auto& n : outs) op.push_back(B(n));
    ip.resize((size_t) M::numInputs, nullptr); op.resize((size_t) M::numOutputs, nullptr);
    graph.push_back([mod, ip, op]() mutable { mod->m.perform(ip.data(), op.data(), SUB); });
}

void Engine::prepare(double sampleRate) {
    sr = sampleRate;
    fifoMode = false; accN = 0; outQ = 0;
    for (auto& v : accIn) v.assign(SUB, 0.0f);
    for (auto& v : qOut) v.assign(SUB, 0.0f);
    for (auto& a : accPhone) a.assign(SUB, 0.0f);
    mods.clear(); busToMod.clear(); graph.clear(); bus.clear();
    // ---- the shared buffers (sizes like the patch: 40 s loops, 2.3 s of pitch, 22 ms wave screens)
    auto mk = [&](const std::string& n, long frames, long ch) { auto& b = bufs[n]; b.name = n; b.resize(frames, ch); };
    for (const char* L : { "A", "B", "C", "D" }) {
        mk(std::string("st_loops") + L, (long) (40.0 * sr), 8);
        mk(std::string("st_pitch") + L, (long) (2.3 * sr / 1.0), 8);
        mk(std::string("st_wave") + L, (long) (0.022 * sr), 8);
    }
    mk("st_wavel", (long) (0.022 * sr), 4);
    mk("st_undo", (long) (40.0 * sr), 1);
    importBuf = std::make_shared<SharedBuffer>(); importBuf->name = "st_import"; importBuf->resize(16, 2);

    auto b = [this](const std::string& n) { return B(n); };
    auto T = names("trk", 1, 8), TP = names("tp", 1, 8), CH = names("ch", 0, 11);
    // order matters: the same order the patch's signal flows in (the SYNTH uses the previous block's beat)
    addNode<Listen>(this, mods, busToMod, graph, b, "st_listen", { "live_in1" }, { "pitch", "-", "lev1" });
    addNode<Tune>(this, mods, busToMod, graph, b, "st_tune", { "live_in1", "pitch" }, { "tune_out", "tn_pitch", "tn_corr", "tn_note" });
    {   // SYNTH 2
        auto* sm = new Synth2Mod(); mods.emplace_back(sm); busToMod["st_syn"] = (int) mods.size() - 1; sm->syn.init(sr);
        const float* pi = B("pitch"); const float* be = B("beat_prev"); const float* bp = B("ebpm");
        float* o[5] = { B("synL"), B("synR"), B("syn_mono"), B("syn_n"), B("syn_lfo") };
        graph.push_back([sm, pi, be, bp, o]() { sm->syn.process(pi, be, bp, o[0], o[1], o[2], o[3], o[4], SUB); });
        graph.push_back([this, be, bp, o]() {     // the KEYBOARD's instrument joins it
            auto* x = ext.load(); if (x == nullptr) return;
            float l[SUB] = {}, r[SUB] = {};
            x->render(l, r, SUB, sr, bp[0], be[0]);
            for (int i = 0; i < SUB; ++i) { o[0][i] += l[i]; o[1][i] += r[i]; o[2][i] += (l[i] + r[i]) * 0.5f; }
        });
    }
    graph.push_back([this]() {                     // live 1 = input or TUNE; live 2-4 = input or SYNTH
        float* li = B("live_in1"); float* tu = B("tune_out"); float* l1 = B("live1");
        float target = tuneAmt > 0 ? 1.0f : 0.0f, k = 1.0f / (float) (0.02 * sr);
        for (int i = 0; i < SUB; ++i) { tuneMix += (tuneMix < target ? 1 : -1) * std::min(k, std::abs(target - tuneMix));
            l1[i] = li[i] * (1 - tuneMix) + tu[i] * tuneMix; }
        float* sy = B("syn_mono");
        for (int j = 0; j < 3; ++j) {
            float* raw = B("live_in" + std::to_string(j + 2)); float* lv = B("live" + std::to_string(j + 2));
            float tg = synthInto.load() == j + 1 ? 1.0f : 0.0f; float kk = 1.0f / (float) (0.03 * sr);
            for (int i = 0; i < SUB; ++i) { synG[j] += (synG[j] < tg ? 1 : -1) * std::min(kk, std::abs(tg - synG[j]));
                lv[i] = raw[i] * (1 - synG[j]) + sy[i] * synG[j]; }
        }
    });
    auto LOUT = cat({ T, TP, { "click", "statA", "statB", "beat", "barno", "beatno", "loopbars", "scenes", "transport", "smask", "ebpm",
                     "sv_bl", "sv_bts", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3",
                     "spdk", "undoav", "sv_spd0", "sv_spd1", "sv_spd2", "sv_spd3", "pvw", "ntn", "sv_ntr", "lphase", "lscene", "imp_busy" } });
    addNode<Looper>(this, mods, busToMod, graph, b, "st_looper", { "live1", "live2", "live3", "live4", "pitch" }, LOUT);
    graph.push_back([this]() { std::memcpy(B("beat_prev"), B("beat"), sizeof(float) * SUB); });
    {   // the mixing desk of every strip: high-pass, EQ, compressor (flat = skipped)
        auto* sm = new StripMixMod(); mods.emplace_back(sm); busToMod["st_mix"] = (int) mods.size() - 1; sm->mx.init(sr); stripMix = &sm->mx;
        std::array<const float*, 12> ip; std::array<float*, 12> op;
        for (int i = 0; i < 4; ++i) { ip[(size_t) i] = B("live" + std::to_string(i + 1)); op[(size_t) i] = B("mx_l" + std::to_string(i + 1)); }
        for (int i = 0; i < 8; ++i) { ip[(size_t) i + 4] = B("trk" + std::to_string(i + 1)); op[(size_t) i + 4] = B("mx_t" + std::to_string(i + 1)); }
        graph.push_back([sm, ip, op]() mutable { sm->mx.process(ip.data(), op.data(), SUB); });
    }
    addNode<Channels>(this, mods, busToMod, graph, b, "st_ch", cat({ { "mx_l1", "mx_l2", "mx_l3", "mx_l4" }, names("mx_t", 1, 8), { "beat" } }),
                      cat({ CH, { "dryL", "dryR", "cueL", "cueR", "-", "to_cho", "to_dly", "to_rev", "to_fxb", "to_shd", "to_orb",
                                  "to_res", "to_grn", "to_frz", "to_spc", "fxused" } }));
    addNode<Halo>(this, mods, busToMod, graph, b, "st_halo", cat({ { "ch0" }, names("ch", 4, 11), { "pitch" }, TP }),
                  { "haloL", "haloR", "halo_note", "halo_key", "halo_n", "halo_mask", "halo_used" });
    addNode<Chorus>(this, mods, busToMod, graph, b, "st_cho", { "to_cho" }, { "choL", "choR" });
    addNode<Delay>(this, mods, busToMod, graph, b, "st_dly", { "to_dly" }, { "dlyL", "dlyR" });
    addNode<Reverse>(this, mods, busToMod, graph, b, "st_rev", { "to_rev" }, { "revL", "revR" });
    addNode<Freeze>(this, mods, busToMod, graph, b, "st_frz", { "to_frz" }, { "frzL", "frzR" });
    addNode<Reson>(this, mods, busToMod, graph, b, "st_res", { "to_res", "halo_mask" }, { "resL", "resR" });
    addNode<Grain>(this, mods, busToMod, graph, b, "st_grn", { "to_grn" }, { "grnL", "grnR" });
    addNode<FxBox>(this, mods, busToMod, graph, b, "st_fxb", { "to_fxb", "beat" }, { "fxbL", "fxbR", "fxb_mode" });
    addNode<Drums>(this, mods, busToMod, graph, b, "st_drm", { "beat", "transport", "loopbars" }, { "drmL", "drmR", "drm_sty", "drm_kit" });
    addNode<Shadow>(this, mods, busToMod, graph, b, "st_shd", { "to_shd", "pitch", "halo_key" }, { "shdL", "shdR" });
    addNode<Orbit>(this, mods, busToMod, graph, b, "st_orb", { "to_orb" }, { "orbL", "orbR", "orb_pos", "orb_dm", "orb_mono" });
    {   // the new send effects: their returns join the dry mix (and SPACE) before the master
        auto* xm = new XMod(); mods.emplace_back(xm); busToMod["st_xfx"] = (int) mods.size() - 1; xm->fx.init(sr);
        std::array<const float*, 12> chp; for (int i = 0; i < 12; ++i) chp[(size_t) i] = B("ch" + std::to_string(i));
        std::array<float*, ExtraFx::NFX> rp; for (int e = 0; e < ExtraFx::NFX; ++e) rp[(size_t) e] = B("xr_" + std::string(extraFxDefs()[(size_t) e].key));
        float* dl = B("dryL"); float* dr = B("dryR"); float* sp = B("to_spc"); float* bpm = B("ebpm"); float* pit = B("pitch");
        graph.push_back([xm, chp, rp, dl, dr, sp, bpm, pit]() mutable { xm->fx.process(chp.data(), dl, dr, sp, SUB, bpm, pit, rp.data()); });
    }
    addNode<Master>(this, mods, busToMod, graph, b, "st_master",
                    { "dryL", "dryR", "choL", "choR", "dlyL", "dlyR", "haloL", "haloR", "frzL", "frzR", "to_spc", "-",
                      "revL", "revR", "resL", "resR", "grnL", "grnR", "fxbL", "fxbR", "drmL", "drmR", "shdL", "shdR", "orbL", "orbR" },
                    { "mainL", "mainR", "grd1", "grd2", "glue_gr" });
    {   // the master polish: tilt EQ, gain, limiter
        auto* mm = new MasterMixMod(); mods.emplace_back(mm); busToMod["st_mst"] = (int) mods.size() - 1; mm->mx.init(sr); masterMix = &mm->mx;
        float* l = B("mainL"); float* r = B("mainR");
        graph.push_back([mm, l, r]() { mm->mx.process(l, r, SUB); });
    }
    addNode<Room>(this, mods, busToMod, graph, b, "st_room", { "mainL", "mainR", "orb_mono", "orb_pos", "orb_dm", "click", "pvw", "cueL", "cueR" },
                  cat({ names("spk", 1, NSPK), { "phL", "phR", "mvx", "mvy" } }));
    {   // AMBISONICS (it plays at the speaker outputs, see runSub)
        auto* am = new AmbiMod(); mods.emplace_back(am); busToMod["st_amb"] = (int) mods.size() - 1; am->amb.prepare(sr); ambi = &am->amb;
    }
    addNode<Scope>(this, mods, busToMod, graph, b, "", cat({ CH, { "lphase", "lscene" } }), { "-" });
    // the selected strip, for the insert cards' screens
    graph.push_back([this]() {
        int s = std::clamp(selStrip.load(), 0, NSTRIP - 1);
        std::memcpy(B("selsig"), B("ch" + std::to_string(s)), sizeof(float) * SUB); });
    // init every module, bind the buffers
    for (auto& m : mods) {
        (void) m;
    }
    // (init is per concrete type; Mod<M> holds m: call init through a small trick)
    for (auto& mp : mods) {
        if (auto* x = dynamic_cast<Mod<Listen>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Tune>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Looper>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Channels>*>(mp.get())) { x->m.init(sr); ambGate = x->m.ambGate; }
        else if (auto* x = dynamic_cast<Mod<Halo>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Chorus>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Delay>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Reverse>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Freeze>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Reson>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Grain>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<FxBox>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Drums>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Shadow>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Orbit>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Master>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Room>*>(mp.get())) x->m.init(sr);
        else if (auto* x = dynamic_cast<Mod<Scope>*>(mp.get())) x->m.init(sr);
        for (auto& kv : bufs) mp->bind(kv.first, kv.second);
        mp->bind("st_import", *importBuf);
    }
    // what the interface reads
    snaps.clear(); peaks.clear(); rings.clear(); snapList.clear(); peakList.clear(); ringList.clear();
    for (auto& n : std::vector<std::string> { "statA", "statB", "beat", "barno", "beatno", "loopbars", "scenes", "transport", "smask", "ebpm",
             "sv_bl", "sv_bts", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3",
             "spdk", "undoav", "sv_spd0", "sv_spd1", "sv_spd2", "sv_spd3", "pvw", "ntn", "sv_ntr", "lphase", "lscene", "imp_busy",
             "pitch", "tn_pitch", "tn_corr", "halo_key", "halo_n", "halo_used", "fxused", "orb_pos", "orb_dm", "glue_gr", "grd1", "grd2",
             "syn_n", "syn_lfo", "mvx", "mvy", "lev1" }) {
        snapList.push_back({ B(n), &snaps[n] });
    }
    std::vector<std::string> pk = { "live_in1", "live_in2", "live_in3", "live_in4", "mainL", "mainR", "phL", "glue_gr" };
    for (int i = 0; i < NSTRIP; ++i) pk.push_back("ch" + std::to_string(i));
    for (int i = 1; i <= NSPK; ++i) pk.push_back("spk" + std::to_string(i));
    for (auto& n : pk) { peaks[n].store(0.0f); peakList.push_back({ B(n), &peaks[n] }); }
    for (auto& e : extraFxDefs()) ringList.push_back({ B("xr_" + std::string(e.key)), &rings["xr_" + std::string(e.key)] });
    for (auto& n : std::vector<std::string> { "selsig", "choL", "dlyL", "revL", "fxbL", "haloL", "resL", "grnL", "frzL", "to_spc", "to_orb",
                                              "drmL", "live1", "synL" })
        ringList.push_back({ B(n), &rings[n] });
}

int Engine::moduleId(const std::string& b) const { auto it = busToMod.find(b); return it == busToMod.end() ? -1 : it->second; }

void Engine::set(const std::string& b, const std::string& name, double v) {
    if (b == "st_tune" && name == "tn_amt") tuneAmt = v;
    int m = moduleId(b); if (m < 0) return;
    int i = mods[(size_t) m]->paramIndex(name); if (i < 0) return;
    q.push({ m, i, v });
}

SharedBuffer& Engine::buffer(const std::string& name) { if (name == "st_import") return *importBuf; return bufs[name]; }
void Engine::setImport(std::shared_ptr<SharedBuffer> nb) { importPending = std::move(nb); importSwap.store(true); }
Snap& Engine::snap(const std::string& n) { return snaps[n]; }
float Engine::peak(const std::string& n) { auto it = peaks.find(n); if (it == peaks.end()) return 0; return it->second.exchange(0.0f); }
Ring& Engine::ring(const std::string& n) { return rings[n]; }

void Engine::process(const float* const* in, int numIn, float* const* out, int numOut, int numSamples) {
    auto t0 = std::chrono::steady_clock::now();
    for (int c = 0; c < numOut; ++c) if (out[c]) std::memset(out[c], 0, sizeof(float) * (size_t) numSamples);
    if (!fifoMode && numSamples % SUB != 0) { fifoMode = true; accN = 0; outQ = SUB; for (auto& q : qOut) std::fill(q.begin(), q.end(), 0.0f); }
    if (fifoMode) processFifo(in, numIn, out, numOut, numSamples);
    else for (int off = 0; off < numSamples; off += SUB) runSub(in, numIn, out, numOut, off, SUB);
    double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double avail = numSamples / sr;
    float c = (float) (used / std::max(1e-9, avail));
    cpu.store(cpu.load() * 0.9f + c * 0.1f);
}

void Engine::processFifo(const float* const* in, int numIn, float* const* out, int numOut, int numSamples) {
    numIn = std::min(numIn, MAXIO); numOut = std::min(numOut, MAXIO);
    const float* phoneSrc[4] = { phoneData[0], phoneData[1], phoneData[2], phoneData[3] };
    const float* ip[MAXIO]; float* op[MAXIO];
    for (int c = 0; c < MAXIO; ++c) { ip[c] = accIn[c].data(); op[c] = qOut[c].data(); }
    int i = 0;
    while (i < numSamples) {
        // hand out what is ready (the block computed last time)
        if (outQ > 0) {
            int k = std::min(outQ, numSamples - i), from = SUB - outQ;
            for (int c = 0; c < numOut; ++c) if (out[c]) std::memcpy(out[c] + i, qOut[c].data() + from, sizeof(float) * (size_t) k);
            // collect the same stretch of input
            for (int c = 0; c < numIn; ++c) {
                if (in[c]) std::memcpy(accIn[c].data() + accN, in[c] + i, sizeof(float) * (size_t) k);
                else std::memset(accIn[c].data() + accN, 0, sizeof(float) * (size_t) k);
            }
            for (int j = 0; j < 4; ++j) if (phoneSrc[j]) std::memcpy(accPhone[j].data() + accN, phoneSrc[j] + i, sizeof(float) * (size_t) k);
            accN += k; outQ -= k; i += k;
        }
        if (accN == SUB) {                      // a full block of input: compute the next block of output
            for (int c = 0; c < numOut; ++c) std::memset(qOut[c].data(), 0, sizeof(float) * SUB);
            for (int j = 0; j < 4; ++j) phoneData[j] = phoneSrc[j] ? accPhone[j].data() : nullptr;
            runSub(ip, numIn, op, numOut, 0, SUB);
            accN = 0; outQ = SUB;
        }
    }
    for (int j = 0; j < 4; ++j) phoneData[j] = phoneSrc[j];
}

void Engine::runSub(const float* const* in, int numIn, float* const* out, int numOut, int off, int n) {
    Cmd c;
    while (q.pop(c)) mods[(size_t) c.module]->setp(c.index, c.value);
    if (importSwap.exchange(false)) {
        importBuf = importPending;
        for (auto& m : mods) m->bind("st_import", *importBuf);
    }
    // live inputs (a partial sub-block is padded with silence)
    for (int j = 0; j < 4; ++j) {
        float* d = B("live_in" + std::to_string(j + 1));
        int ch = inputChannel[j].load();
        if (phoneData[j] != nullptr) { std::memcpy(d, phoneData[j] + off, sizeof(float) * (size_t) n);
            if (n < SUB) std::memset(d + n, 0, sizeof(float) * (size_t) (SUB - n)); }
        else if (in != nullptr && ch >= 0 && ch < numIn && in[ch] != nullptr) { std::memcpy(d, in[ch] + off, sizeof(float) * (size_t) n);
            if (n < SUB) std::memset(d + n, 0, sizeof(float) * (size_t) (SUB - n)); }
        else std::memset(d, 0, sizeof(float) * SUB);
    }
    if (ambi != nullptr && ambGate != nullptr) for (int s2 = 0; s2 < 12; ++s2) ambGate[s2] = ambi->sourceOn(s2) ? 1.0 : 0.0;
    for (auto& g : graph) g();
    // speakers: ROOM + the routing grid's direct outs
    static const char* srcs[14] = { "ch0", "ch1", "ch2", "ch3", "ch4", "ch5", "ch6", "ch7", "ch8", "ch9", "ch10", "ch11", "drm", "halo" };
    float drm[SUB], hal[SUB];
    { float* a = B("drmL"); float* b2 = B("drmR"); float* h1 = B("haloL"); float* h2 = B("haloR");
      for (int i = 0; i < SUB; ++i) { drm[i] = a[i] + b2[i]; hal[i] = h1[i] + h2[i]; } }
    for (int k = 0; k < NSPK && k < numOut; ++k) {
        if (out[k] == nullptr) continue;
        float* sp = B("spk" + std::to_string(k + 1));
        float* o = out[k] + off;
        for (int i = 0; i < n; ++i) o[i] += sp[i];
        for (int s = 0; s < 14; ++s) {
            float g = grid[s][k].load(std::memory_order_relaxed);
            if (g <= 0) continue;
            const float* x = s == 12 ? drm : (s == 13 ? hal : B(srcs[s]));
            for (int i = 0; i < n; ++i) o[i] += x[i] * g;
        }
    }
    if (ambi != nullptr && ambi->active()) {
        const float* chs[12]; for (int s2 = 0; s2 < 12; ++s2) chs[s2] = B(srcs[s2]);
        float* outs[NSPK] = {}; for (int k = 0; k < NSPK && k < numOut; ++k) outs[k] = out[k] != nullptr ? out[k] + off : nullptr;
        ambi->process(chs, outs, std::min(numOut, NSPK), B("phL"), B("phR"), n, B("ebpm")[0]);
    }
    int pf = phonesFirst.load();
    if (pf >= 0 && pf + 1 < numOut) {
        float* l = B("phL"); float* r = B("phR");
        if (out[pf]) for (int i = 0; i < n; ++i) out[pf][off + i] += l[i];
        if (out[pf + 1]) for (int i = 0; i < n; ++i) out[pf + 1][off + i] += r[i];
    }
    // what the interface reads
    for (auto& s : snapList) s.second->v.store(s.first[n - 1], std::memory_order_relaxed);
    for (auto& p : peakList) { float m = p.second->load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) m = std::max(m, std::abs(p.first[i])); p.second->store(m, std::memory_order_relaxed); }
    for (auto& r : ringList) r.second->push(r.first, n);
    if (recorder) recorder(B("mainL"), B("mainR"), n);
}

} // namespace arco
