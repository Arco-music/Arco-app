#include <climits>
#include "Studio.h"
#include "AudioHost.h"
#include "Analysis.h"
#include "StationFx.h"

namespace arco {

std::unique_ptr<StationRunBase> makeStationRun(int which, double sampleRate) { return makeStationRunImpl(which, sampleRate); }
int stationFxCount() { return (int) stationCatalogue().size(); }
const DevInfo& stationFxInfo(int which) { static DevInfo none { "?", "", {} }; return which >= 0 && which < stationFxCount() ? stationCatalogue()[(size_t) which].info : none; }
int stationFxGroup(int which) { if (which < 0 || which >= stationFxCount()) return 0; int k = stationCatalogue()[(size_t) which].kind; return k == StChan ? 0 : k == StExtra ? 2 : 1; }

static const char* LL[4] = { "A", "B", "C", "D" };

Studio::Studio(AudioHost& h) : host(h) {}

// ------------------------------------------------------------------ automation
std::vector<AutoParam> autoParams(const StudioLane& L) {
    std::vector<AutoParam> p = { { "vol", "VOLUME", -60, 6 }, { "pan", "PAN", -1, 1 } };
    if (L.kind != LaneReturn) { p.push_back({ "sendA", "SEND A (reverb)", 0, 1 }); p.push_back({ "sendB", "SEND B (delay)", 0, 1 }); }
    for (size_t i = 0; i < L.devices.size(); ++i) {
        auto& I = devInfoOf(L.devices[i]);
        for (size_t k = 0; k < I.p.size(); ++k)
            p.push_back({ "d" + std::to_string(i) + "p" + std::to_string(k), juce::String(I.name) + " " + I.p[k].name, I.p[k].lo, I.p[k].hi });
    }
    return p;
}
float autoValue(const StudioAuto& a, double t, float fallback) {
    auto& P = a.pts;
    if (P.empty()) return fallback;
    if (t <= P.front().t) return P.front().v;
    if (t >= P.back().t) return P.back().v;
    auto it = std::upper_bound(P.begin(), P.end(), t, [](double x, const AutoPoint& q) { return x < q.t; });
    auto& b = *it; auto& a0 = *(it - 1);
    double f = b.t > a0.t ? (t - a0.t) / (b.t - a0.t) : 0.0;
    return (float) (a0.v + (b.v - a0.v) * f);
}

// ------------------------------------------------------------------ 2.3 routing
bool StudioSong::feeds(int from, int to) const {
    if (from < 0 || from >= (int) lanes.size()) return false;
    std::vector<int> stack { from }; std::vector<char> seen(lanes.size(), 0);
    const int rA = returnA(), rB = returnB();
    while (!stack.empty()) {
        int x = stack.back(); stack.pop_back();
        if (x < 0 || x >= (int) lanes.size() || seen[(size_t) x]) continue;
        seen[(size_t) x] = 1;
        auto& L = lanes[(size_t) x];
        auto go = [&](int t) { if (t == to) return true; if (isTarget(t)) stack.push_back(t); return false; };
        if (go(L.out)) return true;
        if (L.kind != LaneReturn) { if (L.sendA > 0 && rA >= 0 && go(rA)) return true; if (L.sendB > 0 && rB >= 0 && go(rB)) return true; }
        for (auto& sd : L.sends) if (go(sd.to)) return true;
    }
    return false;
}
bool StudioSong::hidden(int i) const {
    if (i < 0 || i >= (int) lanes.size()) return false;
    int x = lanes[(size_t) i].out;
    for (int h = 0; h < 16 && x >= 0 && x < (int) lanes.size(); ++h) { if (lanes[(size_t) x].kind == LaneBus && lanes[(size_t) x].folded) return true; x = lanes[(size_t) x].out; }
    return false;
}
void Studio::laneInserted(int at) {
    auto mv = [at](int& v) { if (v >= at) ++v; };
    for (auto& L : song.lanes) { mv(L.out); for (auto& sd : L.sends) mv(sd.to); for (auto& d : L.devices) mv(d.side); }
    for (auto& d : song.master) mv(d.side);
}
void Studio::laneRemoved(int at) {
    auto mv = [at](int& v) { if (v == at) v = -1; else if (v > at) --v; };
    for (auto& L : song.lanes) { mv(L.out); for (auto& sd : L.sends) mv(sd.to);
        L.sends.erase(std::remove_if(L.sends.begin(), L.sends.end(), [](const StudioSend& x) { return x.to < 0; }), L.sends.end());
        for (auto& d : L.devices) mv(d.side); }
    for (auto& d : song.master) mv(d.side);
}
int Studio::addBus(const juce::String& name, const std::vector<int>& in) {
    const juce::ScopedLock sl(lock);
    int at = INT_MAX; for (int i : in) if (i >= 0 && i < (int) song.lanes.size() && song.lanes[(size_t) i].kind != LaneReturn) at = std::min(at, i);
    if (at == INT_MAX) { at = (int) song.lanes.size(); for (int i = 0; i < (int) song.lanes.size(); ++i) if (song.lanes[(size_t) i].kind == LaneReturn) { at = i; break; } }
    StudioLane B; B.kind = LaneBus; B.name = name; B.colour = 11; B.height = 52;
    // the bus goes where the first lane went (a lane already in a bus: the new bus inside that one)
    if (at < (int) song.lanes.size()) B.out = song.lanes[(size_t) at].out;
    song.lanes.insert(song.lanes.begin() + at, B);
    laneInserted(at);                                   // (the bus's own output moves with the others)
    laneStrip.insert(laneStrip.begin() + juce::jmin(at, (int) laneStrip.size()), -1); laneScene.insert(laneScene.begin() + juce::jmin(at, (int) laneScene.size()), 1);
    for (auto& c : song.clips) if (c.lane >= at) ++c.lane;
    for (int i : in) { int li = i >= at ? i + 1 : i; if (li >= 0 && li < (int) song.lanes.size() && li != at && song.lanes[(size_t) li].kind != LaneReturn && song.canRoute(li, at)) song.lanes[(size_t) li].out = at; }
    if (rt.lanes.size() >= (size_t) at) rt.lanes.insert(rt.lanes.begin() + at, LaneRt());
    prepareRuntime(rt, song);
    return at;
}

// ------------------------------------------------------------------ building
SourcePtr Studio::makeSource(const juce::String& name, std::vector<float> l, std::vector<float> r) {
    auto s = std::make_shared<StudioSource>();
    s->name = name; s->id = juce::Uuid().toString().substring(0, 12); s->l = std::move(l); s->r = std::move(r);
    return s;
}

void Studio::ensureReturns() {
    if (song.returnA() < 0) {
        StudioLane A; A.name = "REVERB"; A.kind = LaneReturn; A.colour = 9; A.height = 46; A.devices.push_back(StudioDevice::make(DevReverb));
        StudioLane B; B.name = "ECHO"; B.kind = LaneReturn; B.colour = 10; B.height = 46; B.devices.push_back(StudioDevice::make(DevDelay));
        song.lanes.push_back(A); song.lanes.push_back(B);
        laneStrip.push_back(-1); laneStrip.push_back(-1); laneScene.push_back(1); laneScene.push_back(1);
    }
    if (song.master.empty()) {
        // 2.2: a clean master - only a limiter at -0.3 dB that touches nothing below it (a glue compressor at -14 dB
        // squashed every imported file); add a compressor on MASTER when you want one
        auto l = StudioDevice::make(DevLimiter); l.p[1] = -0.3f;
        song.master.push_back(l);
    }
}

int Studio::addLane(int kind, const juce::String& name) {
    const juce::ScopedLock sl(lock);            // 2.2: the audio thread never sees half a new lane
    StudioLane L; L.kind = kind; L.name = name;
    int nOwn = 0; for (auto& x : song.lanes) if (x.kind != LaneReturn) ++nOwn;
    L.colour = 4 + nOwn % 8;
    if (kind == LaneInstrument)
        for (auto& d : paramTable()) for (auto& dd : d.dests) if (dd == "st_syn") L.synth[d.id] = (float) host.model.get(d.id);   // the station's SYNTH sound
    // before the returns
    int at = (int) song.lanes.size(); for (int i = 0; i < (int) song.lanes.size(); ++i) if (song.lanes[(size_t) i].kind == LaneReturn) { at = i; break; }
    song.lanes.insert(song.lanes.begin() + at, L);
    laneInserted(at);
    laneStrip.insert(laneStrip.begin() + juce::jmin(at, (int) laneStrip.size()), -1);
    laneScene.insert(laneScene.begin() + juce::jmin(at, (int) laneScene.size()), 1);
    for (auto& c : song.clips) if (c.lane >= at) ++c.lane;
    if (rt.lanes.size() >= (size_t) at) rt.lanes.insert(rt.lanes.begin() + at, LaneRt());   // the others keep their running effects
    prepareRuntime(rt, song);                   // heard at once, even while playing (no stop / start)
    return at;
}

bool Studio::buildFromStation(int repeats, juce::String& why) {
    auto& e = host.engine;
    auto sv = [&](const std::string& n) { return (double) e.snap(n).v.load(); };
    int smask = (int) sv("smask");
    if (smask == 0) { why = "Record some loops first - STUDIO lays them out as a song."; return false; }
    StudioSong s;
    s.sr = e.sr; s.beats = juce::jmax(1, (int) std::round(sv("sv_bts"))); s.den = juce::jmax(1, (int) host.model.get("beatden"));
    double bl = sv("sv_bl"); s.bpm = bl > 16 ? 60.0 * e.sr / bl : host.model.get("bpm");
    std::vector<int> ls, lsc;
    int laneOf[8]; for (auto& x : laneOf) x = -1;
    for (int t = 0; t < 8; ++t)
        for (int sc = 1; sc <= 4; ++sc)
            if (((smask >> (sc - 1)) & 1) && (((int) sv("sv_mask" + std::to_string(sc - 1)) >> t) & 1) && laneOf[t] < 0) {
                laneOf[t] = (int) s.lanes.size();
                StudioLane L; L.name = "TRACK " + juce::String(t + 1); L.colour = 4 + t;
                double v = host.model.get("vol" + std::to_string(4 + t));
                L.vol = (float) juce::jlimit(-40.0, 6.0, 20.0 * std::log10(std::max(1e-4, std::pow(v / 100.0, 2.0) * 2.0)));
                L.pan = (float) host.model.get("pan" + std::to_string(4 + t)) / 50.0f;
                s.lanes.push_back(L); ls.push_back(4 + t); lsc.push_back(sc);
            }
    long at = 0;
    for (int sc = 1; sc <= 4; ++sc) {
        if (!((smask >> (sc - 1)) & 1)) continue;
        long len = (long) sv("sv_len" + std::to_string(sc - 1)); int mask = (int) sv("sv_mask" + std::to_string(sc - 1));
        auto& b = e.buffer(std::string("st_loops") + LL[sc - 1]);
        len = std::min(len, b.len);
        if (len < 64) continue;
        s.markers.push_back({ at, juce::String("SCENE ") + LL[sc - 1] });
        for (int t = 0; t < 8; ++t) {
            if (!((mask >> t) & 1)) continue;
            s.sources.push_back(makeSource(juce::String("scene ") + LL[sc - 1] + " - track " + juce::String(t + 1), std::vector<float>(b.chan(t), b.chan(t) + len)));
            StudioClip c; c.lane = laneOf[t]; c.source = (int) s.sources.size() - 1; c.start = at; c.length = len * repeats; c.loop = true;
            c.fadeIn = c.fadeOut = (float) (0.004 * s.sr); c.name = juce::String(LL[sc - 1]) + juce::String(t + 1);
            s.clips.push_back(c);
        }
        at += len * repeats;
    }
    {
        const juce::ScopedLock sl(lock);
        song = std::move(s); laneStrip = ls; laneScene = lsc;
        ensureReturns();
        pos = 0; undoStack.clear(); redoStack.clear();
    }
    prepareRuntime(rt, song);
    return true;
}

bool Studio::addStationLoop(int scene, int track, long at, int lane, juce::String& why) {
    auto& e = host.engine;
    long len = (long) e.snap("sv_len" + std::to_string(scene - 1)).v.load(); int mask = (int) e.snap("sv_mask" + std::to_string(scene - 1)).v.load();
    if (!((mask >> track) & 1) || len < 64) { why = juce::String("Scene ") + LL[scene - 1] + " has nothing on track " + juce::String(track + 1); return false; }
    auto& b = e.buffer(std::string("st_loops") + LL[scene - 1]);
    len = std::min(len, b.len);
    // the station's audio is at the sound card's rate: the song must be at that rate before it comes in
    // (a song left at another rate was "converted" again on PLAY: the loops played slower or faster)
    if (song.lanes.empty() || song.clips.empty()) { const juce::ScopedLock sl(lock); song.sr = e.sr; } else matchRate(e.sr);
    snapshot();
    const juce::ScopedLock sl(lock);
    // the song takes the station's tempo while nothing from the station is in it yet (recorded or imported lanes
    // don't lock it), so the grid, the click and the bars match the loops; the loops themselves are never resampled
    bool fromStation = false; for (int st : laneStrip) fromStation |= st >= 4;
    if (song.lanes.empty() || !fromStation) { double bl = e.snap("sv_bl").v.load(); if (bl > 16) song.bpm = 60.0 * e.sr / bl; song.beats = juce::jmax(1, (int) std::round(e.snap("sv_bts").v.load())); }
    ensureReturns();
    if (lane < 0 || lane >= (int) song.lanes.size() || song.lanes[(size_t) lane].kind != LaneAudio) {
        lane = -1;
        for (int i = 0; i < (int) song.lanes.size() && i < (int) laneStrip.size(); ++i) if (laneStrip[(size_t) i] == 4 + track && song.lanes[(size_t) i].kind == LaneAudio) lane = i;
        if (lane < 0) { lane = addLane(LaneAudio, "TRACK " + juce::String(track + 1)); laneStrip[(size_t) lane] = 4 + track; laneScene[(size_t) lane] = scene; song.lanes[(size_t) lane].colour = 4 + track; }
    }
    song.sources.push_back(makeSource(juce::String("scene ") + LL[scene - 1] + " - track " + juce::String(track + 1), std::vector<float>(b.chan(track), b.chan(track) + len)));
    StudioClip c; c.lane = lane; c.source = (int) song.sources.size() - 1; c.start = std::max(0L, at); c.length = len; c.loop = true;
    c.fadeIn = c.fadeOut = (float) (0.004 * song.sr); c.name = juce::String(LL[scene - 1]) + juce::String(track + 1);
    song.clips.push_back(c);
    return true;
}

void Studio::refresh() { const juce::ScopedLock sl(lock); laneStrip.resize(song.lanes.size(), -1); laneScene.resize(song.lanes.size(), 1); prepareRuntime(rt, song); }

bool Studio::deleteLane(int li) {
    const juce::ScopedLock sl(lock);
    if (li < 0 || li >= (int) song.lanes.size()) return false;
    song.clips.erase(std::remove_if(song.clips.begin(), song.clips.end(), [li](const StudioClip& c) { return c.lane == li; }), song.clips.end());
    for (auto& c : song.clips) if (c.lane > li) --c.lane;
    song.lanes.erase(song.lanes.begin() + li);
    laneRemoved(li);
    if (li < (int) laneStrip.size()) laneStrip.erase(laneStrip.begin() + li);
    if (li < (int) laneScene.size()) laneScene.erase(laneScene.begin() + li);
    if (li < (int) rt.lanes.size()) rt.lanes.erase(rt.lanes.begin() + li);    // the others keep their running effects
    prepareRuntime(rt, song);
    return true;
}

int Studio::duplicateLane(int li) {
    const juce::ScopedLock sl(lock);
    if (li < 0 || li >= (int) song.lanes.size() || song.lanes[(size_t) li].kind == LaneReturn) return -1;
    auto copy = song.lanes[(size_t) li]; copy.name = (copy.name + " 2").substring(0, 20); copy.arm = false;
    int at = li + 1;
    song.lanes.insert(song.lanes.begin() + at, copy);
    laneInserted(at);
    laneStrip.insert(laneStrip.begin() + juce::jmin(at, (int) laneStrip.size()), -1);
    laneScene.insert(laneScene.begin() + juce::jmin(at, (int) laneScene.size()), 1);
    for (auto& c : song.clips) if (c.lane >= at) ++c.lane;
    std::vector<StudioClip> add; for (auto& c : song.clips) if (c.lane == li) { auto d = c; d.lane = at; add.push_back(d); }   // with its clips
    for (auto& d : add) song.clips.push_back(d);
    if (at <= (int) rt.lanes.size()) rt.lanes.insert(rt.lanes.begin() + at, LaneRt());
    prepareRuntime(rt, song);
    return at;
}

// the lane alone, through its own effects only (not its volume, pan, sends, the master): [first, last) and the tail
bool Studio::renderLaneAlone(const StudioSong& src, int lane, long first, long last, juce::AudioBuffer<float>& out) {
    StudioSong copy = src;
    if (lane < 0 || lane >= (int) copy.lanes.size()) return false;
    auto& L = copy.lanes[(size_t) lane];
    L.frozen = false; L.out = -1; L.sends.clear();             // not through its bus either
    bool tails = false; for (auto& d : L.devices) tails |= d.on && (d.type == DevReverb || d.type == DevDelay);
    L.vol = 0; L.pan = 0; L.sendA = L.sendB = 0; L.mute = false; L.solo = false;
    L.autos.erase(std::remove_if(L.autos.begin(), L.autos.end(), [](const StudioAuto& a) { return a.param == "vol" || a.param == "pan" || a.param == "sendA" || a.param == "sendB"; }), L.autos.end());
    copy.master.clear(); copy.masterDb = 0; copy.masterAutos.clear(); copy.loopOn = false;
    for (auto& o : copy.lanes) if (&o != &L) { o.solo = false; }
    long tail = (long) (copy.sr * (tails ? 3.0 : (L.kind == LaneInstrument ? 1.5 : 0.05)));
    long total = last - first + tail;
    out.setSize(2, (int) total); out.clear();
    Runtime R; prepareRuntime(R, copy);
    for (long p = 0; p < total; p += 1024) {
        int n = (int) std::min(1024L, total - p);
        mix(copy, R, out.getWritePointer(0) + (int) p, out.getWritePointer(1) + (int) p, first + p, n, lane, nullptr, false, true);
    }
    // a tail that ends in silence: trimmed
    long end = total; while (end > last - first + 64 && std::abs(out.getSample(0, (int) end - 1)) < 1e-5f && std::abs(out.getSample(1, (int) end - 1)) < 1e-5f) --end;
    out.setSize(2, (int) end, true);
    return true;
}

int Studio::printLane(int lane, juce::String& why) {
    StudioSong copy; { const juce::ScopedLock sl(lock); copy = song; }
    if (lane < 0 || lane >= (int) copy.lanes.size() || copy.lanes[(size_t) lane].kind == LaneReturn || copy.lanes[(size_t) lane].kind == LaneBus) { why = "choose a track first (a bus or return is printed with its tracks)"; return -1; }
    long first = LONG_MAX, last = 0; for (auto& c : copy.clips) if (c.lane == lane) { first = std::min(first, c.start); last = std::max(last, c.start + c.length); }
    if (first == LONG_MAX) { why = "this track has nothing on it yet"; return -1; }
    juce::AudioBuffer<float> out;
    renderLaneAlone(copy, lane, first, last, out);
    snapshot();
    const juce::ScopedLock sl(lock);
    auto& SL = song.lanes[(size_t) lane];
    SL.frozen = false;
    juce::String nm = SL.name.upToFirstOccurrenceOf(" +FX", false, false);
    auto src = makeSource(nm + " (with effects)", std::vector<float>(out.getReadPointer(0), out.getReadPointer(0) + out.getNumSamples()),
                          std::vector<float>(out.getReadPointer(1), out.getReadPointer(1) + out.getNumSamples()));
    song.sources.push_back(src);
    int target = lane;
    if (SL.kind == LaneInstrument) {                     // the synth's notes stay; the sound goes on a new audio lane under it
        StudioLane A; A.name = (nm + " +FX").substring(0, 20); A.kind = LaneAudio; A.colour = SL.colour; A.vol = SL.vol; A.pan = SL.pan; A.sendA = SL.sendA; A.sendB = SL.sendB;
        A.out = SL.out; A.sends = SL.sends;
        song.lanes.insert(song.lanes.begin() + lane + 1, A);
        laneInserted(lane + 1);
        if (rt.lanes.size() >= (size_t) lane + 1) rt.lanes.insert(rt.lanes.begin() + lane + 1, LaneRt());
        laneStrip.insert(laneStrip.begin() + juce::jmin(lane + 1, (int) laneStrip.size()), -1); laneScene.insert(laneScene.begin() + juce::jmin(lane + 1, (int) laneScene.size()), 1);
        for (auto& c : song.clips) if (c.lane > lane) ++c.lane;
        song.lanes[(size_t) lane].mute = true;
        target = lane + 1;
    } else {
        song.clips.erase(std::remove_if(song.clips.begin(), song.clips.end(), [lane](const StudioClip& c) { return c.lane == lane; }), song.clips.end());
        for (auto& d : SL.devices) d.on = false;          // already in the sound now
        SL.name = (nm + " +FX").substring(0, 20);
    }
    StudioClip c; c.lane = target; c.source = (int) song.sources.size() - 1; c.start = first; c.length = (long) src->size(); c.name = nm + " +FX";
    c.fadeIn = c.fadeOut = (float) (0.002 * song.sr);
    song.clips.push_back(c);
    prepareRuntime(rt, song);
    return (int) song.clips.size() - 1;
}

void Studio::addChain(int lane, int chain) {
    const juce::ScopedLock sl(lock);
    if (lane < -1 || lane >= (int) song.lanes.size()) return;
    auto& v = lane < 0 ? song.master : song.lanes[(size_t) lane].devices;
    auto add = [&](int type, std::initializer_list<std::pair<int, float>> set) { if (v.size() >= 16) return; auto d = StudioDevice::make(type); for (auto& kv : set) d.p[(size_t) kv.first] = kv.second; v.push_back(d); };
    switch (chain) {
        case 0: add(DevEQ, { { 0, 90 } }); add(DevComp, {}); if (lane >= 0) song.lanes[(size_t) lane].sendA = juce::jmax(song.lanes[(size_t) lane].sendA, 0.18f); break;   // voice
        case 1: add(DevEQ, { { 0, 40 } }); add(DevComp, {}); break;                                                                                                       // an instrument
        case 2: add(DevComp, {}); add(DevSat, {}); break;                                                                                                                 // drums
        case 3: add(DevSat, {}); add(DevEQ, {}); break;                                                                                                                   // warm
        case 4: add(DevDelay, {}); add(DevChorus, {}); break;                                                                                                             // wide echo
        case 5: add(DevReverb, {}); break;                                                                                                                                // a big room
        default: break;
    }
    prepareRuntime(rt, song);
}

int Studio::importAudio(const ImportPlan& plan, int lane, long at, bool fit, juce::String& why) {
    const double esr = host.engine.sr;
    if (plan.audio.getNumSamples() < 64) { why = "the file is empty"; return -1; }
    // the song at the sound card's rate (the file was read at it)
    bool anyAudio = false; for (auto& c : song.clips) anyAudio |= !c.isMidi();
    if (!anyAudio || song.clips.empty()) { const juce::ScopedLock sl(lock); song.sr = esr; } else matchRate(esr);
    snapshot();
    double fb = plan.fileBpm;
    juce::AudioBuffer<float> a;
    double down = fb > 0 ? plan.tempo.downbeatOffset : 0.0;
    if (plan.tempo.wholeBars || down < 0.06 * esr) down = 0.0;               // a loop, or a file that starts on its "one"
    bool first = !anyAudio;
    if (fit && fb > 0 && !first && std::abs(fb / song.bpm - 1.0) > 1e-4) {
        double ratio = fb / song.bpm;                                       // > 1: the file is faster than the song, it gets longer
        int outLen = (int) std::llround(plan.audio.getNumSamples() * ratio);
        a = analysis::stretch(plan.audio, esr, outLen, 0.0);
        down *= ratio;
    } else a.makeCopyOf(plan.audio);                                       // sample for sample
    const juce::ScopedLock sl(lock);
    if (first && fb > 0) { song.bpm = std::round(fb * 100.0) / 100.0; }   // the first audio gives the song its tempo
    ensureReturns();
    // its first bar line on a bar line of the song (when the tempo is the song's)
    long start = std::max(0L, at);
    if (fb > 0 && (first || fit) && down > 0) {
        double bar = song.barLen();
        double s = start - down;
        while (s < 0) s += bar;
        start = (long) std::llround(s);
    }
    if (lane < 0 || lane >= (int) song.lanes.size() || song.lanes[(size_t) lane].kind != LaneAudio) {
        lane = addLane(LaneAudio, plan.file.getFileNameWithoutExtension().substring(0, 16).toUpperCase());
    }
    std::vector<float> l(a.getReadPointer(0), a.getReadPointer(0) + a.getNumSamples()), r;
    if (a.getNumChannels() > 1) r.assign(a.getReadPointer(1), a.getReadPointer(1) + a.getNumSamples());
    auto src = makeSource(plan.file.getFileNameWithoutExtension(), std::move(l), std::move(r));
    song.sources.push_back(src);
    StudioClip c; c.lane = lane; c.source = (int) song.sources.size() - 1; c.start = start; c.length = (long) src->size(); c.loop = false;
    c.name = src->name; c.fadeIn = c.fadeOut = (float) (0.002 * song.sr);
    song.clips.push_back(c);
    prepareRuntime(rt, song);
    return (int) song.clips.size() - 1;
}

bool Studio::addStationScene(int scene, long at, int repeats, juce::String& why) {
    auto& e = host.engine;
    int mask = (int) e.snap("sv_mask" + std::to_string(scene - 1)).v.load();
    long len = (long) e.snap("sv_len" + std::to_string(scene - 1)).v.load();
    if (mask == 0 || len < 64) { why = juce::String("Scene ") + LL[scene - 1] + " is empty"; return false; }
    bool any = false;
    for (int t = 0; t < 8; ++t) if ((mask >> t) & 1) {
        for (int r = 0; r < repeats; ++r) { juce::String w; any |= addStationLoop(scene, t, at + r * len, -1, w); }
    }
    { const juce::ScopedLock sl(lock); song.markers.push_back({ at, juce::String("SCENE ") + LL[scene - 1] }); }
    return any;
}

// ------------------------------------------------------------------ the engine
void Studio::prepareRuntime(Runtime& R, const StudioSong& s) {
    R.sr = s.sr;
    if (&R == &rt) R.meters = &meters;
    R.lanes.resize(s.lanes.size());
    for (size_t i = 0; i < s.lanes.size(); ++i) {
        auto& L = s.lanes[i]; auto& lr = R.lanes[i];
        if ((int) lr.dev.size() != (int) L.devices.size()) { lr.dev.resize(L.devices.size()); lr.nDev = (int) L.devices.size(); }
        for (size_t k = 0; k < L.devices.size(); ++k)            // a new device (or another in its place): its own DSP; the others keep running
            if (lr.dev[k].key != DeviceDsp::keyOf(L.devices[k])) lr.dev[k].prepareFor(L.devices[k], s.sr);
        if (L.kind == LaneInstrument && lr.syn == nullptr) { lr.syn = std::make_unique<Synth2>(); lr.syn->init(s.sr); lr.synthSet.clear(); }
    }
    if (R.master.size() != s.master.size()) R.master.resize(s.master.size());
    for (size_t k = 0; k < s.master.size(); ++k) if (R.master[k].key != DeviceDsp::keyOf(s.master[k])) R.master[k].prepareFor(s.master[k], s.sr);
    const int B = 64;
    for (auto& b : R.buf) b.assign(B, 0.0f);
    for (auto* v : { &R.zero, &R.synL, &R.synR, &R.synM, &R.synN, &R.synLfo, &R.beat, &R.bpm, &R.pitch }) v->assign(B, 0.0f);
    if (R.busIn.size() < s.lanes.size() * 128) R.busIn.assign(std::max<size_t>(s.lanes.size(), 16) * 128 * 2, 0.0f);       // (room to grow: no allocation on most edits)
    if (R.key.size() < s.lanes.size() * 128) R.key.assign(std::max<size_t>(s.lanes.size(), 16) * 128 * 2, 0.0f);
}

void Studio::laneSynth(const StudioSong& s, const StudioLane& L, int li, LaneRt& lr, Runtime& R, long start, int n, float* outL, float* outR, bool clipsOn) {
    auto& syn = *lr.syn;
    // the sound: send what changed
    for (auto& kv : L.synth) { auto it = lr.synthSet.find(kv.first); if (it == lr.synthSet.end() || it->second != kv.second) { syn.setp(syn.paramIndex(kv.first), kv.second); lr.synthSet[kv.first] = kv.second; } }
    // the notes starting / ending in this block (from the clips)
    auto voiceOn = [&](int note, float vel) {
        int v = -1; double best = 1e18;
        for (int k = 0; k < Synth2::NV; ++k) if (lr.v[k].note < 0 && lr.v[k].age < best) { best = lr.v[k].age; v = k; }
        if (v < 0) { best = 1e18; for (int k = 0; k < Synth2::NV; ++k) if (lr.v[k].age < best) { best = lr.v[k].age; v = k; } }
        lr.v[v].note = note; lr.v[v].age = ++lr.clock; ++lr.v[v].trig;
        syn.setp(syn.paramIndex("sn" + std::to_string(v)), note); syn.setp(syn.paramIndex("sv" + std::to_string(v)), juce::jlimit(1, 127, (int) (vel * 127)));
        syn.setp(syn.paramIndex("sg" + std::to_string(v)), (double) lr.v[v].trig);
    };
    auto voiceOff = [&](int note) { for (int k = 0; k < Synth2::NV; ++k) if (lr.v[k].note == note) { lr.v[k].note = -1; lr.v[k].age = ++lr.clock; syn.setp(syn.paramIndex("sv" + std::to_string(k)), 0); } };
    if (clipsOn) for (auto& c : s.clips) {
        if (c.lane != li || !c.isMidi() || c.muted || c.midiLen <= 0) continue;
        if (c.start >= start + n || c.start + c.length <= start) continue;
        for (auto& nt : c.notes) {
            // every repeat of the content inside this block
            long rel0 = start - c.start, rel1 = start + n - c.start;
            long k0 = c.loop ? std::max(0L, (rel0 + c.offset - (long) nt.start - (long) nt.len) / c.midiLen) : 0, k1 = c.loop ? (rel1 + c.offset) / c.midiLen + 1 : 1;
            for (long k = k0; k < k1; ++k) {
                double on = nt.start + (double) k * c.midiLen - c.offset, off = on + nt.len;
                if (on >= 0 && on < c.length && on >= rel0 && on < rel1) voiceOn(nt.pitch, nt.vel);
                double offC = std::min(off, (double) c.length);
                if (offC >= rel0 && offC < rel1 && on < c.length && on >= -nt.len) voiceOff(nt.pitch);
            }
        }
    }
    // the beat for the arpeggiator / LFO sync
    double b0 = (double) start / s.beatLen();
    for (int i = 0; i < n; ++i) { R.beat[(size_t) i] = (float) std::fmod(b0 + i / s.beatLen(), 1024.0); R.bpm[(size_t) i] = (float) s.bpm; R.pitch[(size_t) i] = 0; }
    std::fill(R.synL.begin(), R.synL.begin() + n, 0.0f); std::fill(R.synR.begin(), R.synR.begin() + n, 0.0f);
    syn.process(R.pitch.data(), R.beat.data(), R.bpm.data(), R.synL.data(), R.synR.data(), R.synM.data(), R.synN.data(), R.synLfo.data(), n);
    for (int i = 0; i < n; ++i) { outL[i] += R.synL[(size_t) i]; outR[i] += R.synR[(size_t) i]; }
}

void Studio::mix(const StudioSong& s, Runtime& R, float* outL, float* outR, long start, int n, int onlyLane, const float* liveIn[64], bool metro, bool clipsOn) {
    const int nl = (int) s.lanes.size();
    if ((int) R.lanes.size() < nl) return;                  // not prepared yet
    if (nl > 128 || (int) R.busIn.size() < nl * 128 || (int) R.key.size() < nl * 128) return;
    // 2.3 ROUTING: every lane is mixed before what it feeds (its bus, the returns it sends to): by how far it is from the master
    const int rA = s.returnA(), rB = s.returnB();
    int lev[128] = {}, ord[128];
    auto targets = [&](int li, int* t) { int k = 0; auto& L = s.lanes[(size_t) li];
        if (s.isTarget(L.out) && L.out != li) t[k++] = L.out;
        if (L.kind != LaneReturn) { if (rA >= 0 && rA != li) t[k++] = rA; if (rB >= 0 && rB != li) t[k++] = rB; }
        for (auto& sd : L.sends) if (k < 12 && s.isTarget(sd.to) && sd.to != li) t[k++] = sd.to;
        return k; };
    for (int it = 0; it < 16; ++it) { bool ch = false;
        for (int li = 0; li < nl; ++li) { int t[12]; int k = targets(li, t); for (int j = 0; j < k; ++j) if (lev[li] < lev[t[j]] + 1 && lev[t[j]] < 64) { lev[li] = lev[t[j]] + 1; ch = true; } }
        if (!ch) break; }
    for (int i = 0; i < nl; ++i) ord[i] = i;
    std::stable_sort(ord, ord + nl, [&](int a, int b) { return lev[a] > lev[b]; });
    auto under = [&](int li, int anc) { int x = li; for (int h = 0; h < 16 && x >= 0 && x < nl; ++h) { if (x == anc) return true; x = s.lanes[(size_t) x].out; } return false; };
    bool anySolo = false; for (auto& L : s.lanes) anySolo |= (L.solo && L.kind != LaneReturn);
    bool soloed[128] = {};
    if (anySolo) for (int li = 0; li < nl; ++li) { int x = li; for (int h = 0; h < 16 && x >= 0 && x < nl; ++h) { if (s.lanes[(size_t) x].solo) { soloed[li] = true; break; } x = s.lanes[(size_t) x].out; } }
    for (int off = 0; off < n; off += 64) {
        int m = std::min(64, n - off);
        long t0 = start + off;
        for (int li = 0; li < nl; ++li) if (s.isTarget(li)) std::fill(R.busIn.begin() + li * 128, R.busIn.begin() + li * 128 + 128, 0.0f);
        float mL[64] = {}, mR[64] = {};
        for (int oi = 0; oi < nl; ++oi) {
                const int li = ord[oi];
                auto& L = s.lanes[(size_t) li]; auto& lr = R.lanes[(size_t) li];
                float* xL = R.buf[0].data(); float* xR = R.buf[1].data();
                std::fill(xL, xL + m, 0.0f); std::fill(xR, xR + m, 0.0f);
                if (L.kind == LaneReturn || L.kind == LaneBus) { std::copy(R.busIn.begin() + li * 128, R.busIn.begin() + li * 128 + m, xL); std::copy(R.busIn.begin() + li * 128 + 64, R.busIn.begin() + li * 128 + 64 + m, xR); }
                else if (L.frozen) {         // 2.3: FROZEN - its own recording (clips, synth and effects in it)
                    if (clipsOn && L.freezeSrc >= 0 && L.freezeSrc < (int) s.sources.size() && s.sources[(size_t) L.freezeSrc]) {
                        auto& S = *s.sources[(size_t) L.freezeSrc]; long sl = S.size(); bool st = !S.r.empty();
                        long a = std::max(t0, L.freezeAt), b = std::min(t0 + m, L.freezeAt + sl);
                        for (long p = a; p < b; ++p) { long si = p - L.freezeAt; xL[p - t0] += S.l[(size_t) si]; xR[p - t0] += st ? S.r[(size_t) si] : S.l[(size_t) si]; }
                    }
                } else {
                    if (clipsOn) {
                        int act[16]; int na = 0;          // the clips sounding in this sub-block
                        for (int ci = 0; ci < (int) s.clips.size() && na < 16; ++ci) {
                            auto& c = s.clips[(size_t) ci];
                            if (c.lane != li || c.isMidi() || c.muted || c.start >= t0 + m || c.start + c.length <= t0) continue;
                            if (c.source < 0 || c.source >= (int) s.sources.size() || !s.sources[(size_t) c.source]) continue;
                            act[na++] = ci;
                        }
                        for (int k = 0; k < na; ++k) {
                            auto& c = s.clips[(size_t) act[k]];
                            auto& S = *s.sources[(size_t) c.source];
                            long sl = S.size(); if (sl <= 0) continue;
                            bool st = !S.r.empty();
                            // 2.3: AUTO-CROSSFADE - where it overlaps the end of an earlier clip it fades in as that one fades out
                            long xinA = 0, xinB = 0, xoutA = 0, xoutB = 0; bool linIn = false, linOut = false;
                            if (s.autoXfade) for (int j = 0; j < na; ++j) if (j != k) {
                                auto& o = s.clips[(size_t) act[j]];
                                bool later = c.start > o.start || (c.start == o.start && act[k] > act[j]);
                                // the same sound continuing (a split moved back together): a straight fade sums to exactly 1
                                bool cont = o.source == c.source && o.reverse == c.reverse && (o.offset - o.start) == (c.offset - c.start);
                                if (later && c.start < o.end() && c.end() > o.end()) { xinA = c.start; xinB = o.end(); linIn = cont; }
                                if (!later && o.start > c.start && o.start < c.end() && o.end() > c.end()) { xoutA = o.start; xoutB = c.end(); linOut = cont; }
                            }
                            float g = std::pow(10.0f, c.gainDb / 20.0f);
                            long a = std::max(t0, c.start), b = std::min(t0 + m, c.start + c.length);
                            for (long p = a; p < b; ++p) {
                                long rel = p - c.start, si = rel + c.offset;
                                if (c.loop) si = ((si % sl) + sl) % sl; else if (si >= sl || si < 0) continue;   // (a take may start later: silence before it)
                                if (c.reverse) si = sl - 1 - si;
                                float w = g;
                                if (c.fadeIn > 1 && rel < c.fadeIn) w *= rel / c.fadeIn;
                                long toEnd = c.start + c.length - p;
                                if (c.fadeOut > 1 && toEnd < c.fadeOut) w *= toEnd / c.fadeOut;
                                if (p >= xinA && p < xinB) { float f = (float) (p - xinA) / (float) (xinB - xinA); w *= linIn ? f : std::sin(f * 1.5707963f); }
                                if (p >= xoutA && p < xoutB) { float f = (float) (p - xoutA) / (float) (xoutB - xoutA); w *= linOut ? 1.0f - f : std::cos(f * 1.5707963f); }
                                xL[p - t0] += S.l[(size_t) si] * w;
                                xR[p - t0] += (st ? S.r[(size_t) si] : S.l[(size_t) si]) * w;
                            }
                        }
                    }
                    if (L.kind == LaneInstrument && lr.syn) laneSynth(s, L, li, lr, R, t0, m, xL, xR, clipsOn);
                    if (liveIn != nullptr && li < 64 && liveIn[li] != nullptr) for (int i = 0; i < m; ++i) { xL[i] += liveIn[li][off + i]; xR[i] += liveIn[li][off + i]; }
                }
                // automation at the start of the sub-block
                float vol = L.vol, pan = L.pan, sa = L.sendA, sb = L.sendB;
                StudioDevice devs[16]; int nd = std::min(16, (int) L.devices.size());
                for (int k = 0; k < nd; ++k) devs[k] = L.devices[(size_t) k];
                for (auto& A : L.autos) {
                    if (A.pts.empty()) continue;
                    float v = autoValue(A, (double) t0, 0);
                    if (A.param == "vol") vol = v; else if (A.param == "pan") pan = v; else if (A.param == "sendA") sa = v; else if (A.param == "sendB") sb = v;
                    else if (A.param.size() > 3 && A.param[0] == 'd') { int di = std::atoi(A.param.c_str() + 1); auto pp = A.param.find('p'); int pk = pp != std::string::npos ? std::atoi(A.param.c_str() + pp + 1) : -1;
                        if (di >= 0 && di < nd && pk >= 0 && pk < 12) devs[di].p[(size_t) pk] = v; }
                }
                if (!L.frozen) for (int k = 0; k < nd && k < (int) lr.dev.size(); ++k) {
                    auto& dv = lr.dev[(size_t) k];
                    int sd = devs[k].side;            // SIDECHAIN: the key lane's sound (this block if it was mixed already, else the last - 1 ms)
                    if (sd >= 0 && sd < nl && sd != li) { dv.keyL = R.key.data() + sd * 128; dv.keyR = R.key.data() + sd * 128 + 64; } else { dv.keyL = dv.keyR = nullptr; }
                    dv.beatPos = (double) t0 / s.beatLen();
                    dv.process(devs[k], xL, xR, m, s.bpm);
                    dv.keyL = dv.keyR = nullptr;
                }
                std::copy(xL, xL + m, R.key.begin() + li * 128); std::copy(xR, xR + m, R.key.begin() + li * 128 + 64);
                bool src = L.kind == LaneAudio || L.kind == LaneInstrument;
                bool silent = L.mute || (anySolo && src && !soloed[li]) || (onlyLane >= 0 && src && !under(li, onlyLane));
                if (silent) continue;
                float g = std::pow(10.0f, vol / 20.0f);
                float pc = juce::jlimit(-1.0f, 1.0f, pan), pl = std::cos((pc + 1) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f, pr = std::sin((pc + 1) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
                if (R.meters != nullptr && li < 64) { float pk = 0; for (int i = 0; i < m; ++i) pk = std::max(pk, std::max(std::abs(xL[i]), std::abs(xR[i])) * g); auto& mt = (*R.meters)[(size_t) li]; if (pk > mt.l.load()) mt.l.store(pk); }
                const bool toBus = s.isTarget(L.out) && L.out != li && lev[L.out] < lev[li];
                float* dL = toBus ? R.busIn.data() + L.out * 128 : mL; float* dR = toBus ? R.busIn.data() + L.out * 128 + 64 : mR;
                float* aL = rA >= 0 && rA != li && L.kind != LaneReturn && sa > 0 ? R.busIn.data() + rA * 128 : nullptr;
                float* bL = rB >= 0 && rB != li && L.kind != LaneReturn && sb > 0 ? R.busIn.data() + rB * 128 : nullptr;
                for (int i = 0; i < m; ++i) {
                    float l = xL[i] * g, r = xR[i] * g;
                    dL[i] += l * pl; dR[i] += r * pr;
                    if (aL) { aL[i] += l * sa; aL[i + 64] += r * sa; }
                    if (bL) { bL[i] += l * sb; bL[i + 64] += r * sb; }
                }
                for (auto& sd : L.sends) {                 // the other sends: to any bus or return
                    if (!s.isTarget(sd.to) || sd.to == li || lev[sd.to] >= lev[li] || sd.amt <= 0) continue;
                    float* t = R.busIn.data() + sd.to * 128; float k = sd.amt * (sd.pre ? 1.0f : g);
                    for (int i = 0; i < m; ++i) { t[i] += xL[i] * k; t[i + 64] += xR[i] * k; }
                }
            }
        // the master chain
        float mv = s.masterDb;
        StudioDevice md[16]; int nmd = std::min(16, (int) s.master.size());
        for (int k = 0; k < nmd; ++k) md[k] = s.master[(size_t) k];
        for (auto& A : s.masterAutos) if (!A.pts.empty()) {
            float v = autoValue(A, (double) t0, 0);
            if (A.param == "vol") mv = v;
            else if (A.param.size() > 3 && A.param[0] == 'd') { int di = std::atoi(A.param.c_str() + 1); auto pp = A.param.find('p'); int pk = pp != std::string::npos ? std::atoi(A.param.c_str() + pp + 1) : -1; if (di >= 0 && di < nmd && pk >= 0 && pk < 12) md[di].p[(size_t) pk] = v; }
        }
        float g = std::pow(10.0f, mv / 20.0f);
        for (int i = 0; i < m; ++i) { mL[i] *= g; mR[i] *= g; }
        for (int k = 0; k < nmd && k < (int) R.master.size(); ++k) { R.master[(size_t) k].beatPos = (double) t0 / s.beatLen(); R.master[(size_t) k].process(md[k], mL, mR, m, s.bpm); }
        // the metronome
        if (metro) {
            double bl = s.beatLen();
            for (int i = 0; i < m; ++i) {
                double b = (t0 + i) / bl;
                if (std::floor(b) != std::floor(b - 1.0 / bl) || t0 + i == 0) { R.clickLeft = (long) (0.03 * s.sr); R.clickHz = ((long) std::floor(b) % s.beats == 0) ? 1760.0f : 1320.0f; R.clickPh = 0; }
                if (R.clickLeft > 0) { float e = (float) R.clickLeft / (float) (0.03 * s.sr); float v = 0.25f * e * e * (float) std::sin(R.clickPh); R.clickPh += 2 * M_PI * R.clickHz / s.sr; --R.clickLeft; mL[i] += v; mR[i] += v; }
            }
        }
        for (int i = 0; i < m; ++i) { outL[off + i] += mL[i]; outR[off + i] += mR[i]; }
    }
}

bool Studio::matchRate(double newSr) {
    if (newSr < 8000 || std::abs(song.sr - newSr) < 0.5) return false;
    bool wasPlaying = playing.load(); playing = false;
    // resample the audio outside the lock (it can take a moment), then swap everything in at once
    StudioSong copy; { const juce::ScopedLock sl(lock); copy = song; }
    const double k = newSr / copy.sr;
    std::vector<SourcePtr> conv;
    for (auto& sp : copy.sources) {
        if (!sp) { conv.push_back(sp); continue; }
        int ch = sp->r.empty() ? 1 : 2; int n = (int) sp->l.size();
        auto S = std::make_shared<StudioSource>(); S->name = sp->name; S->id = sp->id;
        if (n > 0) {
            juce::AudioBuffer<float> b(ch, n);
            b.copyFrom(0, 0, sp->l.data(), n); if (ch > 1) b.copyFrom(1, 0, sp->r.data(), n);
            auto o = analysis::resample(b, copy.sr, newSr);
            S->l.assign(o.getReadPointer(0), o.getReadPointer(0) + o.getNumSamples());
            if (ch > 1) S->r.assign(o.getReadPointer(1), o.getReadPointer(1) + o.getNumSamples());
        }
        conv.push_back(SourcePtr(S));
    }
    auto L = [k](long v) { return (long) std::llround(v * k); };
    copy.sources = std::move(conv);
    for (auto& c : copy.clips) {
        c.start = L(c.start); c.length = L(c.length); c.offset = L(c.offset); c.fadeIn *= (float) k; c.fadeOut *= (float) k; c.midiLen = L(c.midiLen);
        for (auto& nt : c.notes) { nt.start *= k; nt.len *= k; }
        for (auto& t : c.takes) t.delta = L(t.delta);
    }
    for (auto& ln : copy.lanes) ln.freezeAt = L(ln.freezeAt);
    copy.punchStart = L(copy.punchStart); copy.punchEnd = L(copy.punchEnd);
    for (auto& ln : copy.lanes) for (auto& a : ln.autos) for (auto& pt : a.pts) pt.t *= k;
    for (auto& a : copy.masterAutos) for (auto& pt : a.pts) pt.t *= k;
    for (auto& m : copy.markers) m.pos = L(m.pos);
    copy.loopStart = L(copy.loopStart); copy.loopEnd = L(copy.loopEnd);
    copy.sr = newSr;
    { const juce::ScopedLock sl(lock); song = std::move(copy); pos = L(pos.load()); prepareRuntime(rt, song); }
    if (wasPlaying) playing = true;
    return true;
}

void Studio::play() {
    matchRate(host.engine.sr);
    // 2.2: the song alone (as in any DAW): the station's loops, drum machine and LIVE LOOPS stop - unless a lane is
    // armed to record the station itself
    bool recordsStation = false; for (auto& L : song.lanes) recordsStation |= L.arm && L.input < 0;
    if (!recordsStation) {
        if ((int) host.engine.snap("transport").v.load() == 1) host.model.press("st_looper", "stopall");
        if (host.model.get("dm_run") > 0.5) host.model.set("dm_run", 0);
        host.engine.loopGrid.stopAll();
    }
    const juce::ScopedLock sl(lock);
    prepareRuntime(rt, song);
    if (song.loopOn && song.loopEnd > song.loopStart && (pos.load() < song.loopStart || pos.load() >= song.loopEnd)) pos = song.loopStart;
    playing = true;
}
void Studio::stop() {
    if (recording.load()) stopRecording();
    playing = false; countInLeft = 0;
    const juce::ScopedLock sl(lock);
    for (auto& lr : rt.lanes) if (lr.syn) for (int k = 0; k < Synth2::NV; ++k) { lr.v[k].note = -1; lr.syn->setp(lr.syn->paramIndex("sv" + std::to_string(k)), 0); }
}

bool Studio::renderBlock(float* l, float* r, int n) {
    bool keys = juce::Time::getMillisecondCounter() < keysUntil.load();
    if (!playing.load() && countInLeft.load() <= 0 && !monitorFresh.load() && !keys) return false;
    const juce::ScopedTryLock sl(lock);
    if (!sl.isLocked()) { missedBlocks.fetch_add(1); return false; }
    const float* liveIn[64] = {};
    if (monitorFresh.exchange(false)) for (int i = 0; i < (int) song.lanes.size() && i < 64; ++i) if (song.lanes[(size_t) i].arm && song.lanes[(size_t) i].monitor && (int) monitorBuf[i].size() >= n) liveIn[i] = monitorBuf[i].data();
    if ((int) tlBuf.size() < n) { tlBuf.resize((size_t) n); trBuf.resize((size_t) n); }      // (sized when the card starts; never here in practice)
    std::fill(tlBuf.begin(), tlBuf.begin() + n, 0.0f); std::fill(trBuf.begin(), trBuf.begin() + n, 0.0f);
    auto& tl = tlBuf; auto& tr = trBuf;
    long ci = countInLeft.load();
    if (ci > 0) {                                           // the count-in: one bar of click before recording
        double bl = song.beatLen();
        long done = (long) song.barLen() - ci;
        for (int i = 0; i < n; ++i) {
            long p = done + i;
            if (p % (long) bl == 0) { rt.clickLeft = (long) (0.03 * song.sr); rt.clickHz = p == 0 ? 1760.0f : 1320.0f; rt.clickPh = 0; }
            if (rt.clickLeft > 0) { float e = (float) rt.clickLeft / (float) (0.03 * song.sr); float v = 0.25f * e * e * (float) std::sin(rt.clickPh); rt.clickPh += 2 * M_PI * rt.clickHz / song.sr; --rt.clickLeft; l[i] += v; r[i] += v; }
        }
        countInLeft = std::max(0L, ci - n);
        if (countInLeft.load() == 0) { playing = true; recording = true; }
        return true;
    }
    long p = pos.load();
    if (!playing.load()) { mix(song, rt, tl.data(), tr.data(), p, n, -1, liveIn, false, false); }   // monitoring / playing the keys only
    else {
        int done = 0;
        while (done < n) {
            int m = n - done;
            bool loops = song.loopOn && song.loopEnd > song.loopStart + 64 && (!recording.load() || recLooped);   // 2.3: recording in the loop goes round
            if (loops && p < song.loopEnd) m = (int) std::min<long>(m, song.loopEnd - p);
            const float* li2[64] = {}; for (int i = 0; i < 64; ++i) li2[i] = liveIn[i] ? liveIn[i] + done : nullptr;
            mix(song, rt, tl.data() + done, tr.data() + done, p, m, -1, li2, song.metronome || recording.load(), true);
            p += m; done += m;
            if (loops && p >= song.loopEnd) p = song.loopStart;
        }
        if (!recording.load() && p > song.length() + (long) (song.sr * 2) && !song.loopOn && song.length() > 0) { playing = false; p = 0; }
        pos = p;
    }
    float pkL = 0, pkR = 0;
    // 2.2: transparent - the sound as it is (a tanh here bent every sound, even quiet ones); only overs are held at 0 dBFS
    for (int i = 0; i < n; ++i) { float a = juce::jlimit(-1.0f, 1.0f, tl[(size_t) i]), b = juce::jlimit(-1.0f, 1.0f, tr[(size_t) i]); l[i] += a; r[i] += b; pkL = std::max(pkL, std::abs(a)); pkR = std::max(pkR, std::abs(b)); }
    masterMeter.l.store(std::max(pkL, masterMeter.l.load() * 0.9f)); masterMeter.r.store(std::max(pkR, masterMeter.r.load() * 0.9f));
    return true;
}

// ------------------------------------------------------------------ recording
void Studio::startRecording() {
    snapshot();
    {
        const juce::ScopedLock tl(takeLock);
        takes.clear();
        const juce::ScopedLock sl(lock);
        for (int i = 0; i < (int) song.lanes.size(); ++i) {
            auto& L = song.lanes[(size_t) i];
            if (!L.arm) continue;
            if (L.kind == LaneAudio) { auto t = std::make_unique<Take>(); t->lane = i; t->startPos = pos.load(); t->stereo = L.input < 0; t->fl.assign((size_t) t->fifo.getTotalSize(), 0.0f); t->fr.assign((size_t) t->fifo.getTotalSize(), 0.0f);
                t->l.reserve((size_t) (song.sr * 120)); if (t->stereo) t->r.reserve((size_t) (song.sr * 120)); takes.push_back(std::move(t)); }
            if (L.kind == LaneInstrument && midiRec.lane < 0) { midiRec = MidiRec(); midiRec.lane = i; midiRec.startPos = pos.load(); }
        }
        // 2.3: inside the loop region: it goes round, every pass a take (MIDI: the passes play together)
        recLooped = song.loopOn && song.loopEnd > song.loopStart + (long) (song.sr * 0.25) && pos.load() < song.loopEnd;
        recLoopStart = song.loopStart; recLoopEnd = song.loopEnd;
        midiRec.looped = recLooped.load();
        prepareRuntime(rt, song);
    }
    if (song.countIn) countInLeft = (long) song.barLen();
    else { playing = true; recording = true; }
}

void Studio::input(const float* const* in, int numIn, const float* live[4], const float* mixL, const float* mixR, int n) {
    const juce::ScopedTryLock sl(lock);
    if (!sl.isLocked()) return;
    // monitoring: armed lanes with MONITOR hear their input through their devices
    bool mon = false;
    for (int i = 0; i < (int) song.lanes.size() && i < 64; ++i) {
        auto& L = song.lanes[(size_t) i];
        if (!(L.arm && L.monitor) || L.kind != LaneAudio) continue;
        if ((int) monitorBuf[i].size() < n) continue;                // (sized on the message thread)
        const float* src = L.input >= 0 && L.input < 4 ? live[L.input] : (L.input >= 4 && L.input - 4 < numIn ? in[L.input - 4] : nullptr);
        if (src) std::copy(src, src + n, monitorBuf[i].begin()); else std::fill(monitorBuf[i].begin(), monitorBuf[i].begin() + n, 0.0f);
        mon = true;
    }
    if (mon) monitorFresh = true;
    if (!recording.load() || countInLeft.load() > 0) return;
    const juce::ScopedTryLock tl(takeLock);
    if (!tl.isLocked()) return;
    for (auto& t : takes) {
        if (t->lane < 0 || t->lane >= (int) song.lanes.size()) continue;
        int inp = song.lanes[(size_t) t->lane].input;
        const float* a = inp < 0 ? mixL : (inp < 4 ? live[inp] : (inp - 4 < numIn ? in[inp - 4] : nullptr));
        const float* b = inp < 0 ? mixR : nullptr;
        if (t->fifo.getFreeSpace() < n) continue;
        int s1, n1, s2, n2; t->fifo.prepareToWrite(n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { t->fl[(size_t) (s1 + i)] = a ? a[i] : 0.0f; t->fr[(size_t) (s1 + i)] = b ? b[i] : 0.0f; }
        for (int i = 0; i < n2; ++i) { t->fl[(size_t) (s2 + i)] = a ? a[n1 + i] : 0.0f; t->fr[(size_t) (s2 + i)] = b ? b[n1 + i] : 0.0f; }
        t->fifo.finishedWrite(n1 + n2);
    }
}

void Studio::collectRecordings() {
    // make room for monitoring
    for (int i = 0; i < 64; ++i) if (monitorBuf[i].size() < 8192) monitorBuf[i].assign(8192, 0.0f);
    const juce::ScopedLock tl(takeLock);
    for (auto& t : takes) {
        int ready = t->fifo.getNumReady(); if (ready <= 0) continue;
        int s1, n1, s2, n2; t->fifo.prepareToRead(ready, s1, n1, s2, n2);
        t->l.insert(t->l.end(), t->fl.begin() + s1, t->fl.begin() + s1 + n1); t->l.insert(t->l.end(), t->fl.begin() + s2, t->fl.begin() + s2 + n2);
        if (t->stereo) { t->r.insert(t->r.end(), t->fr.begin() + s1, t->fr.begin() + s1 + n1); t->r.insert(t->r.end(), t->fr.begin() + s2, t->fr.begin() + s2 + n2); }
        t->fifo.finishedRead(n1 + n2);
    }
}

void Studio::stopRecording() {
    if (!recording.load() && countInLeft.load() <= 0) return;
    recording = false; countInLeft = 0;
    collectRecordings();
    const juce::ScopedLock tl(takeLock);
    const juce::ScopedLock sl(lock);
    int n = 0;
    for (auto& t : takes) {
        if (t->l.size() < 256 || t->lane < 0 || t->lane >= (int) song.lanes.size()) continue;
        // the sound card's delay: what was played at time T arrives later - move it back
        long lat = std::min<long>(recordLatency, (long) t->l.size() - 64);
        std::vector<float> l(t->l.begin() + lat, t->l.end()), r;
        if (t->stereo) r.assign(t->r.begin() + lat, t->r.end());
        int k = 1; for (auto& c : song.clips) k += c.lane == t->lane;
        addTakeClip(t->lane, t->startPos, std::move(l), std::move(r), "take " + juce::String(k + n++));
    }
    takes.clear();
    recLooped = false;
    // MIDI
    if (midiRec.lane >= 0) {
        long ref = midiRec.looped ? recLoopStart : midiRec.startPos;
        for (auto& kv : midiRec.open) { auto nt = kv.second; nt.len = std::max(64.0, (double) (pos.load() - ref) - nt.start); midiRec.notes.push_back(nt); }
        if (!midiRec.notes.empty()) {
            StudioClip c; c.lane = midiRec.lane; c.source = -1; c.start = ref; c.loop = false;
            double end = 0; for (auto& nt : midiRec.notes) end = std::max(end, nt.start + nt.len);
            long bars = (long) std::ceil(end / song.barLen()); c.length = c.midiLen = (long) (std::max(1L, bars) * song.barLen());
            if (midiRec.looped) c.length = c.midiLen = recLoopEnd - recLoopStart;          // every pass in one clip (overdub)
            c.notes = midiRec.notes; c.name = "MIDI take";
            song.clips.push_back(c);
        }
        midiRec = MidiRec();
    }
}

void Studio::midiNote(int note, float vel, bool on) {
    note = juce::jlimit(0, 127, note);
    liveNotes[note] = on ? 1 : 0; liveVel[note] = (int) (vel * 127);
    // heard on the armed instrument lane (or the first instrument lane)
    {
        const juce::ScopedLock sl(lock);
        int li = -1; for (int i = 0; i < (int) song.lanes.size(); ++i) if (song.lanes[(size_t) i].kind == LaneInstrument && (li < 0 || song.lanes[(size_t) i].arm)) li = i;
        if (li >= 0 && li < (int) rt.lanes.size() && rt.lanes[(size_t) li].syn) {
            auto& lr = rt.lanes[(size_t) li]; auto& syn = *lr.syn;
            if (on) {
                int v = -1; double best = 1e18; for (int k = 0; k < Synth2::NV; ++k) if (lr.v[k].note < 0 && lr.v[k].age < best) { best = lr.v[k].age; v = k; }
                if (v < 0) v = 0;
                lr.v[v].note = note; lr.v[v].age = ++lr.clock; ++lr.v[v].trig;
                syn.setp(syn.paramIndex("sn" + std::to_string(v)), note); syn.setp(syn.paramIndex("sv" + std::to_string(v)), juce::jlimit(1, 127, (int) (vel * 127)));
                syn.setp(syn.paramIndex("sg" + std::to_string(v)), (double) lr.v[v].trig);
            } else for (int k = 0; k < Synth2::NV; ++k) if (lr.v[k].note == note) { lr.v[k].note = -1; syn.setp(syn.paramIndex("sv" + std::to_string(k)), 0); }
        }
        keysUntil = juce::Time::getMillisecondCounter() + 4000;      // the synth rings on a little
    }
    if (!recording.load() || midiRec.lane < 0) return;
    double t = (double) (pos.load() - (midiRec.looped ? recLoopStart : midiRec.startPos));
    if (t < 0) return;                                                    // (before the loop: the pre-roll)
    if (on) { StudioNote nt; nt.start = t; nt.pitch = note; nt.vel = vel; midiRec.open[note] = nt; }
    else { auto it = midiRec.open.find(note); if (it != midiRec.open.end()) { auto nt = it->second; nt.len = std::max(64.0, t - nt.start); midiRec.notes.push_back(nt); midiRec.open.erase(it); } }
}

// ------------------------------------------------------------------ 2.3 takes, punch (under the lock)
void Studio::addTakeClip(int lane, long startPos, std::vector<float> l, std::vector<float> r, const juce::String& name) {
    auto& L = song.lanes[(size_t) lane];
    const bool st = !r.empty();
    const long total = (long) l.size();
    StudioClip c; c.lane = lane; c.loop = false; c.fadeIn = c.fadeOut = (float) (0.003 * song.sr); c.name = name;
    const long Lp = recLoopEnd - recLoopStart;
    if (recLooped.load() && Lp > 64 && startPos < recLoopEnd && total > recLoopEnd - startPos + (long) (0.05 * song.sr)) {
        // it went round the loop: each pass a take of one clip on the loop region
        const long first = recLoopEnd - startPos;                          // the first pass (with the pre-roll before the loop)
        const int nPass = 1 + (int) ((total - first + Lp - 1) / Lp);
        std::vector<StudioTake> mine; int active = 0;
        for (int k = 0; k < nPass; ++k) {
            long b0 = k == 0 ? std::max(0L, recLoopStart - startPos) : first + (long) (k - 1) * Lp;
            long tl0 = k == 0 ? std::max(startPos, recLoopStart) : recLoopStart;
            long have = std::min(total, k == 0 ? first : b0 + Lp) - b0;
            if (have <= 0 || (have < (long) (0.25 * song.sr) && have < Lp / 4)) continue;      // a pass hardly begun: not a take
            std::vector<float> pl((size_t) Lp, 0.0f), pr; if (st) pr.assign((size_t) Lp, 0.0f);
            long dst = tl0 - recLoopStart;
            for (long i = 0; i < have && dst + i < Lp; ++i) { pl[(size_t) (dst + i)] = l[(size_t) (b0 + i)]; if (st) pr[(size_t) (dst + i)] = r[(size_t) (b0 + i)]; }
            song.sources.push_back(makeSource(L.name + " " + name + "." + juce::String(k + 1), std::move(pl), std::move(pr)));
            mine.push_back({ (int) song.sources.size() - 1, 0, name + "." + juce::String((int) mine.size() + 1) });
            if (have >= Lp * 9 / 10 || mine.size() == 1) active = (int) mine.size() - 1;         // the last whole pass plays
        }
        if (mine.empty()) return;
        c.start = recLoopStart; c.length = Lp; c.source = mine[(size_t) active].source;
        if (mine.size() > 1) { c.takes = mine; c.take = active; }
    } else {
        song.sources.push_back(makeSource(L.name + " " + name, std::move(l), std::move(r)));
        c.source = (int) song.sources.size() - 1; c.start = startPos; c.length = total;
    }
    // PUNCH: only what falls between the punch points stays
    if (song.punchOn && song.punchEnd > song.punchStart) {
        long a = std::max(c.start, song.punchStart), b = std::min(c.end(), song.punchEnd);
        if (b - a < 64) return;
        c.offset += a - c.start; c.length = b - a; c.start = a;
        c.fadeIn = c.fadeOut = (float) (0.005 * song.sr);
    }
    // what was there stays as earlier takes of the new clip (TAKES chooses), and is cut away where the new one plays
    std::vector<StudioTake> older; std::vector<StudioClip> pieces;
    for (auto& o : song.clips) if (o.lane == lane && !o.isMidi() && o.start < c.end() && o.end() > c.start) {
        long ov = std::min(o.end(), c.end()) - std::max(o.start, c.start);
        if (!o.loop && ov > (long) (0.1 * song.sr)) {
            long offAtC = o.offset + (c.start - o.start);
            if (o.takes.empty()) older.push_back({ o.source, offAtC - c.offset, o.name.isNotEmpty() ? o.name : juce::String("earlier") });
            else for (auto& t : o.takes) older.push_back({ t.source, offAtC + t.delta - c.offset, t.name });
        }
        if (o.start >= c.start && o.end() <= c.end()) o.length = 0;                          // fully covered: removed below
        else if (o.start < c.start && o.end() > c.end()) {                                     // the new one inside it: two pieces
            StudioClip after = o; long cut = c.end() - o.start; after.offset += cut; after.start += cut; after.length -= cut;
            o.length = c.start - o.start; pieces.push_back(after);
        }
        else if (o.start < c.start) o.length = c.start - o.start;                              // keep the part before
        else { long cut = c.end() - o.start; o.offset += cut; o.start += cut; o.length -= cut; }
    }
    for (auto& x : pieces) song.clips.push_back(x);
    song.clips.erase(std::remove_if(song.clips.begin(), song.clips.end(), [](const StudioClip& x) { return x.length <= 0; }), song.clips.end());
    if (!older.empty()) {
        if (c.takes.empty()) { c.takes.push_back({ c.source, 0, name }); c.take = 0; }
        c.takes.insert(c.takes.begin(), older.begin(), older.end()); c.take += (int) older.size();
    }
    song.clips.push_back(c);
    lastRecordName = c.name;
}

bool Studio::chooseTake(int ci, int t) {
    snapshot();
    const juce::ScopedLock sl(lock);
    if (ci < 0 || ci >= (int) song.clips.size()) return false;
    auto& c = song.clips[(size_t) ci];
    if (t < 0 || t >= (int) c.takes.size()) return false;
    long d = c.takes[(size_t) t].delta;
    for (auto& k : c.takes) k.delta -= d;                  // the chosen one at 0 again
    c.offset += d; c.source = c.takes[(size_t) t].source; c.take = t;
    return true;
}

bool Studio::deleteOtherTakes(int ci) {
    snapshot();
    const juce::ScopedLock sl(lock);
    if (ci < 0 || ci >= (int) song.clips.size()) return false;
    song.clips[(size_t) ci].takes.clear(); song.clips[(size_t) ci].take = 0;
    return true;
}

int Studio::explodeTakes(int ci) {
    snapshot();
    const juce::ScopedLock sl(lock);
    if (ci < 0 || ci >= (int) song.clips.size() || song.clips[(size_t) ci].takes.size() < 2) return -1;
    auto c = song.clips[(size_t) ci];
    int lane = c.lane, at = lane + 1, firstNew = -1;
    for (int t = 0; t < (int) c.takes.size(); ++t) {
        if (t == c.take) continue;
        StudioLane L = song.lanes[(size_t) lane]; L.name = (c.takes[(size_t) t].name.isNotEmpty() ? c.takes[(size_t) t].name : "take " + juce::String(t + 1)).substring(0, 20).toUpperCase();
        L.arm = false; L.solo = false; L.mute = true; L.autos.clear(); L.showAuto = -1; L.height = juce::jmax(46.0f, L.height * 0.7f); L.frozen = false;
        song.lanes.insert(song.lanes.begin() + at, L);
        laneInserted(at);
        laneStrip.insert(laneStrip.begin() + juce::jmin(at, (int) laneStrip.size()), -1); laneScene.insert(laneScene.begin() + juce::jmin(at, (int) laneScene.size()), 1);
        for (auto& o : song.clips) if (o.lane >= at) ++o.lane;
        if (rt.lanes.size() >= (size_t) at) rt.lanes.insert(rt.lanes.begin() + at, LaneRt());
        StudioClip n = c; n.lane = at; n.source = c.takes[(size_t) t].source; n.offset = c.offset + c.takes[(size_t) t].delta; n.takes.clear(); n.take = 0; n.name = c.takes[(size_t) t].name;
        song.clips.push_back(n);
        if (firstNew < 0) firstNew = at;
        ++at;
    }
    for (auto& o : song.clips) if (o.lane == lane && o.start == c.start && o.source == c.source) { o.takes.clear(); o.take = 0; }
    prepareRuntime(rt, song);
    return firstNew;
}

// ------------------------------------------------------------------ 2.3 freeze
bool Studio::freezeLane(int lane, juce::String& why) {
    StudioSong copy; { const juce::ScopedLock sl(lock); copy = song; }
    if (lane < 0 || lane >= (int) copy.lanes.size() || copy.lanes[(size_t) lane].kind == LaneReturn || copy.lanes[(size_t) lane].kind == LaneBus) { why = "choose a track first (a bus is not frozen - its tracks are)"; return false; }
    if (copy.lanes[(size_t) lane].frozen) return true;
    long first = LONG_MAX, last = 0; for (auto& c : copy.clips) if (c.lane == lane) { first = std::min(first, c.start); last = std::max(last, c.end()); }
    if (first == LONG_MAX) { why = "this track has nothing on it to freeze"; return false; }
    juce::AudioBuffer<float> out;
    if (!renderLaneAlone(copy, lane, first, last, out)) return false;
    snapshot();
    const juce::ScopedLock sl(lock);
    auto& L = song.lanes[(size_t) lane];
    song.sources.push_back(makeSource(L.name + " (frozen)", std::vector<float>(out.getReadPointer(0), out.getReadPointer(0) + out.getNumSamples()),
                                      std::vector<float>(out.getReadPointer(1), out.getReadPointer(1) + out.getNumSamples())));
    L.frozen = true; L.freezeSrc = (int) song.sources.size() - 1; L.freezeAt = first; L.arm = false;
    prepareRuntime(rt, song);
    return true;
}
bool Studio::unfreezeLane(int lane) {
    snapshot();
    const juce::ScopedLock sl(lock);
    if (lane < 0 || lane >= (int) song.lanes.size() || !song.lanes[(size_t) lane].frozen) return false;
    auto& L = song.lanes[(size_t) lane]; L.frozen = false; L.freezeSrc = -1;
    prepareRuntime(rt, song);
    return true;
}

// ------------------------------------------------------------------ 2.3 editing: ripple, time, clipboard, MIDI
bool Studio::removeClips(std::vector<int> ids) {
    snapshot();
    const juce::ScopedLock sl(lock);
    std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<StudioClip> gone; for (int i : ids) if (i >= 0 && i < (int) song.clips.size()) gone.push_back(song.clips[(size_t) i]);
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) if (*it >= 0 && *it < (int) song.clips.size()) song.clips.erase(song.clips.begin() + *it);
    if (song.ripple > 0) {
        // what comes after each removed clip moves left by its length (latest first, so the distances stay right)
        std::sort(gone.begin(), gone.end(), [](const StudioClip& a, const StudioClip& b) { return a.start > b.start; });
        for (auto& g : gone)
            for (auto& c : song.clips) if ((song.ripple == 2 || c.lane == g.lane) && c.start >= g.end()) c.start -= g.length;
        if (song.ripple == 2) for (auto& g : gone) for (auto& m : song.markers) if (m.pos >= g.end()) m.pos -= g.length;
    }
    return !gone.empty();
}

void Studio::removeTime(long a, long b) {
    if (b <= a) return;
    snapshot();
    const juce::ScopedLock sl(lock);
    const long len = b - a;
    std::vector<StudioClip> add;
    for (auto& c : song.clips) {
        if (c.end() <= a) continue;
        if (c.start >= b) { c.start -= len; continue; }
        if (c.start < a && c.end() > b) {                 // across it: the part after it joins the part before
            StudioClip r = c; long cut = b - c.start; r.offset += cut; r.start = a; r.length = c.end() - b; r.fadeIn = (float) (0.002 * song.sr);
            c.length = a - c.start; c.fadeOut = (float) (0.002 * song.sr); add.push_back(r);
        } else if (c.start < a) { c.length = a - c.start; c.fadeOut = juce::jmin(c.fadeOut, (float) c.length * 0.5f); }
        else if (c.end() > b) { long cut = b - c.start; c.offset += cut; c.length -= cut; c.start = a; c.fadeIn = juce::jmin(c.fadeIn, (float) c.length * 0.5f); }
        else c.length = 0;
    }
    for (auto& r : add) song.clips.push_back(r);
    song.clips.erase(std::remove_if(song.clips.begin(), song.clips.end(), [](const StudioClip& x) { return x.length <= 0; }), song.clips.end());
    auto movePts = [&](std::vector<StudioAuto>& A) { for (auto& au : A) { std::vector<AutoPoint> k; for (auto p : au.pts) { if (p.t >= a && p.t < b) continue; if (p.t >= b) p.t -= len; k.push_back(p); } au.pts = k; } };
    for (auto& L : song.lanes) { movePts(L.autos); if (L.frozen && L.freezeAt >= b) L.freezeAt -= len; }
    movePts(song.masterAutos);
    song.markers.erase(std::remove_if(song.markers.begin(), song.markers.end(), [&](const StudioMarker& m) { return m.pos >= a && m.pos < b; }), song.markers.end());
    for (auto& m : song.markers) if (m.pos >= b) m.pos -= len;
    auto mv = [&](long& p) { if (p >= b) p -= len; else if (p > a) p = a; };
    mv(song.loopStart); mv(song.loopEnd); mv(song.punchStart); mv(song.punchEnd);
}

void Studio::insertTime(long at, long len) {
    if (len <= 0) return;
    snapshot();
    const juce::ScopedLock sl(lock);
    std::vector<StudioClip> add;
    for (auto& c : song.clips) {
        if (c.start >= at) { c.start += len; continue; }
        if (c.end() > at) { StudioClip r = c; long cut = at - c.start; r.offset += cut; r.start = at + len; r.length = c.end() - at; c.length = cut; add.push_back(r); }
    }
    for (auto& r : add) song.clips.push_back(r);
    auto movePts = [&](std::vector<StudioAuto>& A) { for (auto& au : A) for (auto& p : au.pts) if (p.t >= at) p.t += len; };
    for (auto& L : song.lanes) { movePts(L.autos); if (L.frozen && L.freezeAt >= at) L.freezeAt += len; }
    movePts(song.masterAutos);
    for (auto& m : song.markers) if (m.pos >= at) m.pos += len;
    for (long* p : { &song.loopStart, &song.loopEnd, &song.punchStart, &song.punchEnd }) if (*p >= at) *p += len;
}

void Studio::copyClips(const std::vector<int>& ids) {
    const juce::ScopedLock sl(lock);
    clipboard.clear(); clipboardStart = LONG_MAX;
    for (int i : ids) if (i >= 0 && i < (int) song.clips.size()) { clipboard.push_back(song.clips[(size_t) i]); clipboardStart = std::min(clipboardStart, song.clips[(size_t) i].start); }
}

std::vector<int> Studio::pasteClips(long at, int lane) {
    std::vector<int> out;
    if (clipboard.empty()) return out;
    snapshot();
    const juce::ScopedLock sl(lock);
    int top = INT_MAX; for (auto& c : clipboard) top = std::min(top, c.lane);
    for (auto c : clipboard) {
        if (!c.isMidi() && (c.source < 0 || c.source >= (int) song.sources.size())) continue;      // (from before an undo)
        int nl = lane >= 0 ? c.lane - top + lane : c.lane;
        auto fits = [&](int l) { return l >= 0 && l < (int) song.lanes.size() && song.lanes[(size_t) l].kind != LaneReturn && (song.lanes[(size_t) l].kind == LaneInstrument) == c.isMidi(); };
        if (!fits(nl)) nl = c.lane;
        if (!fits(nl)) continue;
        c.lane = nl; c.start = std::max(0L, at + (c.start - clipboardStart));
        for (auto& t : c.takes) if (t.source >= (int) song.sources.size()) { c.takes.clear(); c.take = 0; break; }
        song.clips.push_back(c); out.push_back((int) song.clips.size() - 1);
    }
    return out;
}

void Studio::quantise(int ci, double q, float strength, bool ends) {
    if (q < 16) return;
    snapshot();
    const juce::ScopedLock sl(lock);
    if (ci < 0 || ci >= (int) song.clips.size() || !song.clips[(size_t) ci].isMidi()) return;
    auto& c = song.clips[(size_t) ci];
    double zero = (double) (c.start - c.offset);          // the timeline position of the content's start: the grid is the song's
    for (auto& n : c.notes) {
        double abs = zero + n.start, end = abs + n.len;
        double qs = std::round(abs / q) * q;
        n.start = juce::jlimit(0.0, (double) juce::jmax(1L, c.midiLen) - 1, n.start + (qs - abs) * strength);
        if (ends) { double qe = std::round(end / q) * q; if (qe <= qs) qe = qs + q; double ne = end + (qe - end) * strength; n.len = juce::jmax(q / 4, ne - (zero + n.start)); }
    }
    std::sort(c.notes.begin(), c.notes.end(), [](const StudioNote& a, const StudioNote& b) { return a.start < b.start; });
}

int Studio::importMidi(const juce::File& f, int lane, long at, juce::String& why) {
    juce::FileInputStream in(f);
    juce::MidiFile mf;
    if (!in.openedOk() || !mf.readFrom(in)) { why = "not a MIDI file"; return -1; }
    const short tf = mf.getTimeFormat();
    double fileBpm = 0;
    for (int t = 0; t < mf.getNumTracks() && fileBpm <= 0; ++t) for (auto* e : *mf.getTrack(t)) if (e->message.isTempoMetaEvent()) { fileBpm = 60.0 / e->message.getTempoSecondsPerQuarterNote(); break; }
    if (tf <= 0) mf.convertTimestampTicksToSeconds();
    snapshot();
    const juce::ScopedLock sl(lock);
    if (song.clips.empty() && fileBpm > 20 && fileBpm < 400) song.bpm = std::round(fileBpm * 100) / 100;   // an empty song takes its tempo
    ensureReturns();
    auto toS = [&](double ts) { return tf > 0 ? ts / tf * song.beatLen() : ts * song.sr; };
    int firstClip = -1, used = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t) {
        juce::MidiMessageSequence seq(*mf.getTrack(t));
        seq.updateMatchedPairs();
        std::vector<StudioNote> notes; juce::String tname;
        for (int i = 0; i < seq.getNumEvents(); ++i) {
            auto& m = seq.getEventPointer(i)->message;
            if (m.isTrackNameEvent() && tname.isEmpty()) tname = m.getTextFromTextMetaEvent();
            if (!m.isNoteOn()) continue;
            double on = toS(m.getTimeStamp()), off = on + song.beatLen() / 4;
            if (auto* up = seq.getEventPointer(i)->noteOffObject) off = toS(up->message.getTimeStamp());
            notes.push_back({ on, juce::jmax(64.0, off - on), m.getNoteNumber(), m.getFloatVelocity() });
        }
        if (notes.empty()) continue;
        int li = (used == 0 && lane >= 0 && lane < (int) song.lanes.size() && song.lanes[(size_t) lane].kind == LaneInstrument) ? lane : -1;
        if (li < 0) li = addLane(LaneInstrument, (tname.isNotEmpty() ? tname : f.getFileNameWithoutExtension()).substring(0, 16).toUpperCase());
        double end = 0; for (auto& n : notes) end = std::max(end, n.start + n.len);
        StudioClip c; c.lane = li; c.source = -1; c.start = std::max(0L, at); c.loop = false; c.notes = notes;
        c.midiLen = c.length = (long) (std::max(1.0, std::ceil(end / song.barLen())) * song.barLen());
        c.name = tname.isNotEmpty() ? tname : f.getFileNameWithoutExtension();
        song.clips.push_back(c); if (firstClip < 0) firstClip = (int) song.clips.size() - 1;
        ++used;
    }
    if (firstClip < 0) why = "the MIDI file has no notes";
    prepareRuntime(rt, song);
    return firstClip;
}

bool Studio::exportMidi(int ci, const juce::File& f) {
    StudioClip c; double bl, bpm; int beats;
    { const juce::ScopedLock sl(lock); if (ci < 0 || ci >= (int) song.clips.size() || !song.clips[(size_t) ci].isMidi()) return false; c = song.clips[(size_t) ci]; bl = song.beatLen(); bpm = song.bpm; beats = song.beats; }
    const int ppq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::lround(60000000.0 / bpm)), 0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(beats, 4), 0);
    seq.addEvent(juce::MidiMessage::textMetaEvent(3, c.name.isNotEmpty() ? c.name : juce::String("ARCO")), 0);
    // as it plays: the content repeated (loop) and cut at the clip's ends
    long reps = c.loop && c.midiLen > 0 ? (c.length + c.offset) / c.midiLen + 1 : 1;
    for (long k = 0; k < reps; ++k) for (auto& n : c.notes) {
        double on = n.start + (double) k * c.midiLen - c.offset, off = on + n.len;
        if (on < 0 || on >= c.length) continue;
        off = std::min(off, (double) c.length);
        seq.addEvent(juce::MidiMessage::noteOn(1, n.pitch, juce::jlimit(0.01f, 1.0f, n.vel)), on / bl * ppq);
        seq.addEvent(juce::MidiMessage::noteOff(1, n.pitch), off / bl * ppq);
    }
    seq.sort(); seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote(ppq); mf.addTrack(seq);
    f.deleteFile();
    juce::FileOutputStream out(f);
    return out.openedOk() && mf.writeTo(out);
}

// ------------------------------------------------------------------ export
bool Studio::renderAll(juce::AudioBuffer<float>& out, std::function<bool(float)> progress, int onlyLane) {
    StudioSong copy; { const juce::ScopedLock sl(lock); copy = song; }
    long total = copy.length() + (long) (copy.sr * 2.0);
    if (copy.length() <= 0) return false;
    out.setSize(2, (int) total); out.clear();
    Runtime R; prepareRuntime(R, copy);
    const int B = 1024;
    bool ok = true;
    for (long p = 0; p < total; p += B) {
        int n = (int) std::min((long) B, total - p);
        mix(copy, R, out.getWritePointer(0) + (int) p, out.getWritePointer(1) + (int) p, p, n, onlyLane, nullptr, false, true);
        if (progress && (p / B) % 64 == 0 && !progress((float) p / total)) { ok = false; break; }
    }
    return ok;
}

void Studio::snapshot() { const juce::ScopedLock sl(lock); undoStack.push_back(song); if (undoStack.size() > 80) undoStack.erase(undoStack.begin()); redoStack.clear(); }
bool Studio::undo() { const juce::ScopedLock sl(lock); if (undoStack.empty()) return false; redoStack.push_back(song); song = undoStack.back(); undoStack.pop_back(); laneStrip.resize(song.lanes.size(), -1); laneScene.resize(song.lanes.size(), 1); prepareRuntime(rt, song); return true; }
bool Studio::redo() { const juce::ScopedLock sl(lock); if (redoStack.empty()) return false; undoStack.push_back(song); song = redoStack.back(); redoStack.pop_back(); laneStrip.resize(song.lanes.size(), -1); laneScene.resize(song.lanes.size(), 1); prepareRuntime(rt, song); return true; }

// ------------------------------------------------------------------ clip tools
bool Studio::reverseClip(int ci) { const juce::ScopedLock sl(lock); if (ci < 0 || ci >= (int) song.clips.size() || song.clips[(size_t) ci].isMidi()) return false; song.clips[(size_t) ci].reverse = !song.clips[(size_t) ci].reverse; return true; }
bool Studio::normaliseClip(int ci) {
    const juce::ScopedLock sl(lock);
    if (ci < 0 || ci >= (int) song.clips.size()) return false;
    auto& c = song.clips[(size_t) ci]; if (c.isMidi() || !song.sources[(size_t) c.source]) return false;
    auto& S = *song.sources[(size_t) c.source];
    float pk = 0; for (float v : S.l) pk = std::max(pk, std::abs(v)); for (float v : S.r) pk = std::max(pk, std::abs(v));
    if (pk < 1e-6f) return false;
    c.gainDb = 20.0f * std::log10(std::pow(10.0f, -1.0f / 20.0f) / pk);
    return true;
}
bool Studio::transposeClip(int ci, double semis) {
    SourcePtr src; { const juce::ScopedLock sl(lock); if (ci < 0 || ci >= (int) song.clips.size()) return false;
        auto& c = song.clips[(size_t) ci];
        if (c.isMidi()) { for (auto& nt : c.notes) nt.pitch = juce::jlimit(0, 127, nt.pitch + (int) std::lround(semis)); return true; }
        src = song.sources[(size_t) c.source]; }
    if (!src) return false;
    juce::AudioBuffer<float> b(src->r.empty() ? 1 : 2, (int) src->size());
    b.copyFrom(0, 0, src->l.data(), (int) src->size()); if (!src->r.empty()) b.copyFrom(1, 0, src->r.data(), (int) src->size());
    auto o = analysis::stretch(b, song.sr, b.getNumSamples(), semis);
    std::vector<float> l(o.getReadPointer(0), o.getReadPointer(0) + o.getNumSamples()), r;
    if (o.getNumChannels() > 1) r.assign(o.getReadPointer(1), o.getReadPointer(1) + o.getNumSamples());
    auto ns = makeSource(src->name + (semis > 0 ? " +" : " ") + juce::String((int) std::lround(semis)), std::move(l), std::move(r));
    const juce::ScopedLock sl(lock);
    song.sources.push_back(ns); song.clips[(size_t) ci].source = (int) song.sources.size() - 1;
    return true;
}

// ------------------------------------------------------------------ save / open
static juce::var devToVar(const StudioDevice& d) { auto* x = new juce::DynamicObject(); x->setProperty("type", d.type); x->setProperty("on", d.on); if (d.side >= 0) x->setProperty("side", d.side); if (d.type == DevStation) { x->setProperty("fx", d.fx); if (d.fx >= 0 && d.fx < stationFxCount()) x->setProperty("fxk", juce::String(stationCatalogue()[(size_t) d.fx].key)); }
    juce::Array<juce::var> p; for (float v : d.p) p.add(v); x->setProperty("p", p); return juce::var(x); }
static StudioDevice devFromVar(const juce::var& v) { StudioDevice d = StudioDevice::make(juce::jlimit(0, DevCount - 1, (int) v["type"])); d.on = v.getProperty("on", true); d.side = v.getProperty("side", -1);
    if ((int) v["type"] == DevStation) { int w = juce::jlimit(0, stationFxCount() - 1, (int) v.getProperty("fx", 0)); auto k = v.getProperty("fxk", "").toString().toStdString();
        for (int i = 0; i < stationFxCount(); ++i) if (!k.empty() && stationCatalogue()[(size_t) i].key == k) w = i;      // by name: the list may grow
        d = StudioDevice::makeStation(w); d.on = v.getProperty("on", true); d.side = v.getProperty("side", -1); }
    if (auto* a = v["p"].getArray()) for (int i = 0; i < juce::jmin(12, a->size()); ++i) d.p[(size_t) i] = (float) (*a)[i]; return d; }
static juce::var autosToVar(const std::vector<StudioAuto>& A) { juce::Array<juce::var> out;
    for (auto& a : A) { auto* x = new juce::DynamicObject(); x->setProperty("param", juce::String(a.param)); juce::Array<juce::var> pts; for (auto& p : a.pts) { pts.add(p.t); pts.add(p.v); } x->setProperty("pts", pts); out.add(juce::var(x)); }
    return out; }
static std::vector<StudioAuto> autosFromVar(const juce::var& v) { std::vector<StudioAuto> out;
    if (auto* a = v.getArray()) for (auto& x : *a) { StudioAuto A; A.param = x["param"].toString().toStdString(); if (auto* p = x["pts"].getArray()) for (int i = 0; i + 1 < p->size(); i += 2) A.pts.push_back({ (double) (*p)[i], (float) (*p)[i + 1] }); out.push_back(A); }
    return out; }

bool Studio::save(const juce::File& folder) {
    auto dir = folder.getChildFile("studio"); dir.createDirectory();
    juce::WavAudioFormat wav;
    StudioSong s; { const juce::ScopedLock sl(lock); s = song; }
    juce::StringArray keep;
    for (auto& sp : s.sources) {
        if (!sp) continue;
        auto f = dir.getChildFile("src-" + sp->id + ".wav"); keep.add(f.getFileName());
        if (f.existsAsFile()) continue;                    // already written (sources never change)
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), s.sr, sp->r.empty() ? 1 : 2, 32, {}, 0));
        if (!w) return false;
        const float* ch[2] = { sp->l.data(), sp->r.empty() ? sp->l.data() : sp->r.data() };
        w->writeFromFloatArrays(ch, sp->r.empty() ? 1 : 2, (int) sp->l.size());
    }
    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.wav")) if (!keep.contains(f.getFileName())) f.deleteFile();
    auto* o = new juce::DynamicObject();
    o->setProperty("version", 3);
    o->setProperty("sr", s.sr); o->setProperty("bpm", s.bpm); o->setProperty("beats", s.beats); o->setProperty("den", s.den); o->setProperty("master", s.masterDb); o->setProperty("glue", s.glue);
    o->setProperty("loopOn", s.loopOn); o->setProperty("loopStart", (juce::int64) s.loopStart); o->setProperty("loopEnd", (juce::int64) s.loopEnd);
    o->setProperty("metronome", s.metronome); o->setProperty("countIn", s.countIn);
    o->setProperty("punchOn", s.punchOn); o->setProperty("punchStart", (juce::int64) s.punchStart); o->setProperty("punchEnd", (juce::int64) s.punchEnd);
    o->setProperty("autoXfade", s.autoXfade); o->setProperty("ripple", s.ripple);
    juce::Array<juce::var> srcs, clips, lanes, mk, mdev;
    for (auto& sp : s.sources) { auto* x = new juce::DynamicObject(); x->setProperty("name", sp ? sp->name : juce::String()); x->setProperty("id", sp ? sp->id : juce::String()); srcs.add(juce::var(x)); }
    for (auto& c : s.clips) { auto* x = new juce::DynamicObject(); x->setProperty("lane", c.lane); x->setProperty("source", c.source); x->setProperty("start", (juce::int64) c.start);
        x->setProperty("length", (juce::int64) c.length); x->setProperty("offset", (juce::int64) c.offset); x->setProperty("loop", c.loop); x->setProperty("gain", c.gainDb);
        x->setProperty("fin", c.fadeIn); x->setProperty("fout", c.fadeOut); x->setProperty("rev", c.reverse); x->setProperty("mute", c.muted); x->setProperty("name", c.name); x->setProperty("colour", c.colour);
        if (!c.takes.empty()) { juce::Array<juce::var> tk; for (auto& t : c.takes) { auto* q = new juce::DynamicObject(); q->setProperty("s", t.source); q->setProperty("d", (juce::int64) t.delta); q->setProperty("n", t.name); tk.add(juce::var(q)); }
            x->setProperty("takes", tk); x->setProperty("take", c.take); }
        if (c.isMidi()) { x->setProperty("midiLen", (juce::int64) c.midiLen); juce::Array<juce::var> ns; for (auto& n : c.notes) { ns.add(n.start); ns.add(n.len); ns.add(n.pitch); ns.add(n.vel); } x->setProperty("notes", ns); }
        clips.add(juce::var(x)); }
    for (size_t li = 0; li < s.lanes.size(); ++li) { auto& L = s.lanes[li]; auto* x = new juce::DynamicObject(); x->setProperty("name", L.name); x->setProperty("colour", L.colour); x->setProperty("kind", L.kind);
        x->setProperty("vol", L.vol); x->setProperty("pan", L.pan); x->setProperty("mute", L.mute); x->setProperty("solo", L.solo); x->setProperty("input", L.input);
        x->setProperty("sendA", L.sendA); x->setProperty("sendB", L.sendB); x->setProperty("height", L.height);
        x->setProperty("out", L.out); if (L.folded) x->setProperty("folded", true);
        if (!L.sends.empty()) { juce::Array<juce::var> sv; for (auto& sd : L.sends) { auto* q = new juce::DynamicObject(); q->setProperty("to", sd.to); q->setProperty("amt", sd.amt); q->setProperty("pre", sd.pre); sv.add(juce::var(q)); } x->setProperty("sends", sv); }
        if (L.frozen) { x->setProperty("frozen", true); x->setProperty("freezeSrc", L.freezeSrc); x->setProperty("freezeAt", (juce::int64) L.freezeAt); }
        x->setProperty("strip", li < laneStrip.size() ? laneStrip[li] : -1);
        juce::Array<juce::var> dv; for (auto& d : L.devices) dv.add(devToVar(d)); x->setProperty("devices", dv);
        x->setProperty("autos", autosToVar(L.autos));
        if (!L.synth.empty()) { auto* sy = new juce::DynamicObject(); for (auto& kv : L.synth) sy->setProperty(juce::Identifier(kv.first), kv.second); x->setProperty("synth", juce::var(sy)); }
        lanes.add(juce::var(x)); }
    for (auto& m : s.markers) { auto* x = new juce::DynamicObject(); x->setProperty("pos", (juce::int64) m.pos); x->setProperty("name", m.name); mk.add(juce::var(x)); }
    for (auto& d : s.master) mdev.add(devToVar(d));
    o->setProperty("sources", srcs); o->setProperty("clips", clips); o->setProperty("lanes", lanes); o->setProperty("markers", mk); o->setProperty("masterDevices", mdev);
    o->setProperty("masterAutos", autosToVar(s.masterAutos));
    return dir.getChildFile("studio.json").replaceWithText(juce::JSON::toString(juce::var(o)));
}

bool Studio::load(const juce::File& folder) {
    auto dir = folder.getChildFile("studio");
    auto v = juce::JSON::parse(dir.getChildFile("studio.json"));
    if (!v.isObject()) return false;
    StudioSong s; std::vector<int> ls, lsc;
    s.sr = v.getProperty("sr", 48000.0); s.bpm = v.getProperty("bpm", 120.0); s.beats = v.getProperty("beats", 4); s.den = v.getProperty("den", 4); s.masterDb = v.getProperty("master", 0.0f); s.glue = v.getProperty("glue", 20.0f);
    s.loopOn = v.getProperty("loopOn", false); s.loopStart = (juce::int64) v.getProperty("loopStart", 0); s.loopEnd = (juce::int64) v.getProperty("loopEnd", 0);
    s.metronome = v.getProperty("metronome", false); s.countIn = v.getProperty("countIn", true);
    s.punchOn = v.getProperty("punchOn", false); s.punchStart = (juce::int64) v.getProperty("punchStart", 0); s.punchEnd = (juce::int64) v.getProperty("punchEnd", 0);
    s.autoXfade = v.getProperty("autoXfade", true); s.ripple = v.getProperty("ripple", 0);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto readWav = [&](const juce::File& f, const juce::String& name, const juce::String& id) {
        auto S = std::make_shared<StudioSource>(); S->name = name; S->id = id.isNotEmpty() ? id : juce::Uuid().toString().substring(0, 12);
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (r) { juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, b.getNumSamples(), 0, true, true);
            S->l.assign(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples()); if (b.getNumChannels() > 1) S->r.assign(b.getReadPointer(1), b.getReadPointer(1) + b.getNumSamples()); }
        return SourcePtr(S); };
    if (auto* a = v["sources"].getArray())
        for (int i = 0; i < a->size(); ++i) {
            auto x = (*a)[i];
            if (x.isObject()) s.sources.push_back(readWav(dir.getChildFile("src-" + x["id"].toString() + ".wav"), x["name"].toString(), x["id"].toString()));
            else s.sources.push_back(readWav(dir.getChildFile("source" + juce::String(i) + ".wav"), x.toString(), {}));        // 1.2
        }
    if (auto* a = v["clips"].getArray()) for (auto& x : *a) { StudioClip c; c.lane = x["lane"]; c.source = x["source"]; c.start = (juce::int64) x["start"]; c.length = (juce::int64) x["length"];
        c.offset = (juce::int64) x["offset"]; c.loop = x["loop"]; c.gainDb = x["gain"]; c.fadeIn = x["fin"]; c.fadeOut = x["fout"]; c.reverse = x.getProperty("rev", false); c.muted = x.getProperty("mute", false);
        c.name = x.getProperty("name", "").toString(); c.colour = x.getProperty("colour", -1);
        if (auto* tk = x["takes"].getArray()) { for (auto& q : *tk) c.takes.push_back({ (int) q["s"], (long) (juce::int64) q["d"], q["n"].toString() }); c.take = juce::jlimit(0, juce::jmax(0, (int) c.takes.size() - 1), (int) x.getProperty("take", 0)); }
        if (c.source < 0) { c.midiLen = (juce::int64) x.getProperty("midiLen", 0); if (auto* ns = x["notes"].getArray()) for (int i = 0; i + 3 < ns->size(); i += 4) c.notes.push_back({ (double) (*ns)[i], (double) (*ns)[i + 1], (int) (*ns)[i + 2], (float) (*ns)[i + 3] }); }
        s.clips.push_back(c); }
    if (auto* a = v["lanes"].getArray()) for (auto& x : *a) { StudioLane L; L.name = x["name"].toString(); L.colour = x["colour"]; L.kind = x.getProperty("kind", 0); L.vol = x["vol"]; L.pan = x["pan"];
        L.mute = x["mute"]; L.solo = x["solo"]; L.input = x.getProperty("input", 0);
        L.frozen = x.getProperty("frozen", false); L.freezeSrc = x.getProperty("freezeSrc", -1); L.freezeAt = (juce::int64) x.getProperty("freezeAt", 0); L.sendA = x.getProperty("sendA", 0.0f); L.sendB = x.getProperty("sendB", 0.0f); L.height = x.getProperty("height", 74.0f);
        L.out = x.getProperty("out", -1); L.folded = x.getProperty("folded", false);
        if (auto* sv = x["sends"].getArray()) for (auto& q : *sv) L.sends.push_back({ (int) q["to"], (float) q.getProperty("amt", 0.5f), (bool) q.getProperty("pre", false) });
        if (auto* dv = x["devices"].getArray()) for (auto& d : *dv) L.devices.push_back(devFromVar(d));
        L.autos = autosFromVar(x["autos"]);
        if (auto* sy = x["synth"].getDynamicObject()) for (auto& p : sy->getProperties()) L.synth[p.name.toString().toStdString()] = (float) p.value;
        // 1.2: the simple EQ / compressor
        float lo = x.getProperty("lo", 0.0f), mid = x.getProperty("mid", 0.0f), hi = x.getProperty("hi", 0.0f), hp = x.getProperty("hp", 0.0f), comp = x.getProperty("comp", 0.0f), midHz = x.getProperty("midHz", 1000.0f);
        if (!x.hasProperty("kind") && (lo != 0 || mid != 0 || hi != 0 || hp > 15)) { auto e = StudioDevice::make(DevEQ); e.p[0] = hp; e.p[1] = lo; e.p[2] = 120; e.p[3] = mid; e.p[4] = midHz; e.p[9] = hi; L.devices.push_back(e); }
        if (!x.hasProperty("kind") && comp > 0.5f) { auto c = StudioDevice::make(DevComp); c.p[0] = -6.0f - comp * 0.3f; c.p[1] = 1.0f + comp / 25.0f; L.devices.push_back(c); }
        s.lanes.push_back(L); ls.push_back(x.getProperty("strip", -1)); lsc.push_back(1); }
    if (auto* a = v["markers"].getArray()) for (auto& x : *a) s.markers.push_back({ (long) (juce::int64) x["pos"], x["name"].toString() });
    if (auto* a = v["masterDevices"].getArray()) for (auto& d : *a) s.master.push_back(devFromVar(d));
    s.masterAutos = autosFromVar(v["masterAutos"]);
    {
        const juce::ScopedLock sl(lock);
        song = std::move(s); laneStrip = ls; laneScene = lsc;
        ensureReturns();
        pos = 0; undoStack.clear(); redoStack.clear();
    }
    prepareRuntime(rt, song);
    host.model.set("beats", song.beats); host.model.set("beatden", song.den);     // 2.3: one time signature for the whole app
    return true;
}

bool Studio::printEffects(int lane, std::function<bool(float)> progress) {
    if (lane < 0 || lane >= (int) laneStrip.size() || laneStrip[(size_t) lane] < 0) return false;
    Renderer rd(host);
    auto snap = rd.snapshot();
    std::vector<int> srcs; { const juce::ScopedLock sl(lock); for (auto& c : song.clips) if (c.lane == lane && c.source >= 0 && std::find(srcs.begin(), srcs.end(), c.source) == srcs.end()) srcs.push_back(c.source); }
    int k = 0;
    for (int si : srcs) {
        juce::String nm; long len; { const juce::ScopedLock sl(lock); if (!song.sources[(size_t) si]) continue; nm = song.sources[(size_t) si]->name; len = song.sources[(size_t) si]->size(); }
        int sc = nm.contains("scene B") ? 2 : nm.contains("scene C") ? 3 : nm.contains("scene D") ? 4 : 1;
        snap.sceneNow = sc;
        ExportOptions o; o.what = ExportOptions::CurrentScene; o.repeats = 2; o.tailSeconds = 0;
        juce::AudioBuffer<float> b; double rate = 0;
        bool ok = rd.renderMix(snap, o, b, rate, laneStrip[(size_t) lane], [&](float p) { return !progress || progress((k + p) / (float) srcs.size()); });
        if (!ok || b.getNumSamples() < 2 * len) return false;
        auto ns = makeSource(nm + " (with effects)", std::vector<float>(b.getReadPointer(0) + len, b.getReadPointer(0) + 2 * len), std::vector<float>(b.getReadPointer(1) + len, b.getReadPointer(1) + 2 * len));
        const juce::ScopedLock sl(lock);
        song.sources[(size_t) si] = ns;
        ++k;
    }
    const juce::ScopedLock sl(lock);
    song.lanes[(size_t) lane].name = song.lanes[(size_t) lane].name.upToFirstOccurrenceOf(" +FX", false, false) + " +FX";
    return true;
}

} // namespace arco
