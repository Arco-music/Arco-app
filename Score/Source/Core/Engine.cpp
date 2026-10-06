#include "Engine.h"
#include <chrono>

namespace score {

Engine::Engine() { for (auto& p : inPeak) p.store(0); }

void Engine::prepare(double sampleRate) {
    sr = sampleRate;
    for (auto* v : { &mL, &mR, &aL, &aR, &bL, &bR, &xL, &xR }) v->assign(MAXB, 0.0f);
    std::lock_guard<std::mutex> l(rtLock);
    for (auto& kv : rts) kv.second->prepare(sr);
}

TrackRTPtr Engine::runtime(Uid track) {
    std::lock_guard<std::mutex> l(rtLock);
    auto& r = rts[track];
    if (!r) { r = std::make_shared<TrackRT>(); r->id = track; r->prepare(sr); }
    return r;
}
void Engine::forget(Uid track) { std::lock_guard<std::mutex> l(rtLock); rts.erase(track); }

void Engine::setGraph(GraphPtr g) { std::atomic_store(&cur, std::move(g)); }

void Engine::play() { cmds.push({ CPlay }); playing.store(true); }
void Engine::stop() { cmds.push({ CStop }); playing.store(false); }
void Engine::locate(double beat) { cmds.push({ CLocate, beat }); posBeat.store(beat); }
void Engine::jumpAt(double atBeat, double toBeat) { cmds.push({ CJump, atBeat, toBeat }); }
void Engine::stopAt(double beat) { cmds.push({ CStopAt, beat }); }
void Engine::liveNote(Uid track, const NoteEv& e) { liveQ.push({ track, e }); }

void Engine::startRecording(std::vector<RecSlotPtr> s, double fromBeat, double countInBeats) {
    { std::lock_guard<std::mutex> l(recLock); slots = std::move(s); }
    recStartBeat.store(fromBeat);
    recording.store(true); playing.store(true);
    cmds.push({ CRec, fromBeat, countInBeats });
}
void Engine::stopRecording() { cmds.push({ CRecStop }); recording.store(false); }

void Engine::resetAll() {
    auto g = graph(); if (!g) return;
    auto rs = [&](const GTrack& t) {
        for (auto* e : t.fx) e->reset();
        if (t.inst) { t.inst->allNotesOff(true); for (auto* e : t.inst->internalEffects()) e->reset(); }
        for (auto& c : t.clips) { for (auto* e : c.fx) e->reset(); if (c.rt) c.rt->tail = 0; }
        if (t.rt) { std::fill(std::begin(t.rt->clipOn), std::end(t.rt->clipOn), false); t.rt->followNote = -1; }
    };
    for (auto& t : g->tracks) rs(t);
    rs(g->retA); rs(g->retB); rs(g->master);
}

// ======================================================================== the device callback
void Engine::process(const float* const* in, int numIn, float* const* out, int numOut, int numSamples) {
    callbacks.fetch_add(1);
    for (int c = 0; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear(out[c], numSamples);
    if (offline.load()) return;
    auto g = graph();
    if (!g) return;
    auto t0 = std::chrono::steady_clock::now();
    for (int off = 0; off < numSamples; off += MAXB) runBlock(in, numIn, out, numOut, off, std::min(MAXB, numSamples - off), *g, 0);
    double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    cpu.store(cpu.load() * 0.95f + (float) (used / (numSamples / sr)) * 0.05f);
    // input meters
    for (int c = 0; c < 8; ++c) {
        float pk = inPeak[c].load() * 0.9f;
        if (c < numIn && in[c]) for (int i = 0; i < numSamples; ++i) pk = std::max(pk, std::abs(in[c][i]));
        inPeak[c].store(pk);
    }
}

void Engine::renderBlock(float* L, float* R, int n, Uid onlyTrack) {
    auto g = graph(); if (!g) return;
    float* o[2] = { L, R };
    int saved = outFirst.load(); int savedClick = clickFirst.load();
    outFirst.store(0); clickFirst.store(-2);                   // -2: no metronome in an export
    for (int off = 0; off < n; off += MAXB) {
        float* oo[2] = { o[0] + off, o[1] + off };
        int m = std::min(MAXB, n - off);
        juce::FloatVectorOperations::clear(oo[0], m); juce::FloatVectorOperations::clear(oo[1], m);
        runBlock(nullptr, 0, oo, 2, 0, m, *g, onlyTrack);
    }
    outFirst.store(saved); clickFirst.store(savedClick);
}

void Engine::runBlock(const float* const* in, int numIn, float* const* out, int numOut, int off, int n, const Graph& g, Uid only) {
    const double spb = g.spb;
    // ---- commands
    Cmd c;
    while (cmds.pop(c)) {
        switch (c.t) {
            case CPlay: rolling = true; break;
            case CStop: rolling = false; recWanted = false; jumpAtB = -1; stopAtB = -1; break;
            case CLocate: pos = (int64_t) std::llround(c.a * spb); for (auto& t : g.tracks) if (t.rt) t.rt->ev.push_back({ 0, NoteEv::AllOff, 0, 0 }); break;
            case CJump: jumpAtB = c.a; jumpToB = c.b; break;
            case CStopAt: stopAtB = c.a; break;
            case CRec: {
                { std::unique_lock<std::mutex> l(recLock, std::try_to_lock); if (l.owns_lock()) { activeSlots.clear(); for (auto& s : slots) activeSlots.push_back(s.get()); } }
                recFrom = (int64_t) std::llround(c.a * spb);
                if (!rolling) { pos = recFrom - (int64_t) std::llround(c.b * spb); rolling = true; }
                else if (pos > recFrom) recFrom = pos;
                recWanted = true; break; }
            case CRecStop: recWanted = false; recRolling.store(false); activeSlots.clear(); break;
        }
    }
    // ---- live notes
    LiveEv le;
    while (liveQ.pop(le)) {
        for (auto& t : g.tracks) if (t.t && t.t->id == le.track && t.rt) {
            t.rt->ev.push_back(le.e);
            if (recRolling.load() && t.arm) for (auto* s : activeSlots) if (s->track == le.track && s->midi) s->notes.push({ (double) pos / spb, le.e });
        }
    }
    // ---- the block, split where the loop wraps / a jump happens / recording starts
    int done = 0;
    while (done < n) {
        int m = n - done;
        bool hitLoop = false, hitJump = false, hitStop = false;
        if (rolling) {
            int64_t le_ = (int64_t) std::llround(loopB.load() * spb), ls = (int64_t) std::llround(loopA.load() * spb);
            if (loopOn.load() && !recWanted && le_ > ls && pos < le_ && pos + m >= le_) { m = (int) (le_ - pos); hitLoop = true; }
            if (jumpAtB >= 0) { int64_t ja = (int64_t) std::llround(jumpAtB * spb); if (pos >= ja) { m = 0; hitJump = true; } else if (pos + m > ja) { m = (int) (ja - pos); hitJump = true; } }
            if (stopAtB >= 0) { int64_t sa = (int64_t) std::llround(stopAtB * spb); if (pos >= sa) { m = 0; hitStop = true; } else if (pos + m > sa) { m = (int) (sa - pos); hitStop = true; } }
            if (recWanted && !recRolling.load() && pos < recFrom && pos + m > recFrom) m = (int) (recFrom - pos);
        }
        if (m > 0) {
            if (recWanted && rolling && pos >= recFrom && !recRolling.load()) recRolling.store(true);
            // inputs for this stretch
            const float* inp[64]; int ni = std::min(numIn, 64);
            for (int k = 0; k < ni; ++k) inp[k] = in && in[k] ? in[k] + off + done : nullptr;
            std::fill(mL.begin(), mL.end(), 0.0f); std::fill(mR.begin(), mR.end(), 0.0f);
            std::fill(aL.begin(), aL.end(), 0.0f); std::fill(aR.begin(), aR.end(), 0.0f); std::fill(bL.begin(), bL.end(), 0.0f); std::fill(bR.begin(), bR.end(), 0.0f);
            for (auto& t : g.tracks) {
                doTrack(t, g, pos, m, rolling, inp, ni);
                if (!t.rt) continue;
                bool muted = (t.mix && t.mix->params[4]->on()) || (g.anySolo && !t.solo) || (only != 0 && t.t && t.t->id != only);
                float vol = t.mix ? dbToGain(t.mix->params[0]->get()) : 1.0f;
                float pan = t.mix ? t.mix->params[1]->get() / 50.0f : 0.0f;
                float pa = (pan + 1) * 0.25f * kPiF, gl = std::cos(pa) * 1.4142f * vol, gr = std::sin(pa) * 1.4142f * vol;
                if (muted) gl = gr = 0;
                float* L = t.rt->L.data(); float* R = t.rt->R.data();
                float pkL = t.rt->peakL.load() * 0.93f, pkR = t.rt->peakR.load() * 0.93f;
                float sa = t.mix ? dbToGain(t.mix->params[2]->get()) : 0.0f, sb = t.mix ? dbToGain(t.mix->params[3]->get()) : 0.0f;
                if (t.mix && t.mix->params[2]->get() <= -59.9f) sa = 0;
                if (t.mix && t.mix->params[3]->get() <= -59.9f) sb = 0;
                for (int i = 0; i < m; ++i) {
                    float l = L[i] * gl, r = R[i] * gr;
                    t.rt->lastL[(size_t) i] = l; t.rt->lastR[(size_t) i] = r;
                    mL[(size_t) i] += l; mR[(size_t) i] += r;
                    if (sa > 0) { aL[(size_t) i] += l * sa; aR[(size_t) i] += r * sa; }
                    if (sb > 0) { bL[(size_t) i] += l * sb; bR[(size_t) i] += r * sb; }
                    pkL = std::max(pkL, std::abs(l)); pkR = std::max(pkR, std::abs(r));
                }
                t.rt->peakL.store(pkL); t.rt->peakR.store(pkR);
            }
            // the returns
            for (auto* rp : { &g.retA, &g.retB }) {
                auto& rt = *rp; if (!rt.rt) continue;
                float* L = rt.rt->L.data(); float* R = rt.rt->R.data();
                const auto& srcL = rp == &g.retA ? aL : bL; const auto& srcR = rp == &g.retA ? aR : bR;
                std::copy(srcL.begin(), srcL.begin() + m, L); std::copy(srcR.begin(), srcR.begin() + m, R);
                TimeInfo ti; ti.sr = sr; ti.bpm = g.bpm; ti.beat = (double) pos / spb; ti.playing = rolling; ti.beatsPerBar = g.bpb;
                if (rolling) applyAutos(rt, pos);
                for (auto* e : rt.fx) e->run(L, R, m, ti);
                bool muted = rt.mix && rt.mix->params[4]->on();
                float vol = muted ? 0.0f : (rt.mix ? dbToGain(rt.mix->params[0]->get()) : 1.0f);
                float pkL = rt.rt->peakL.load() * 0.93f, pkR = rt.rt->peakR.load() * 0.93f;
                for (int i = 0; i < m; ++i) { float l = L[i] * vol, r = R[i] * vol; mL[(size_t) i] += l; mR[(size_t) i] += r; pkL = std::max(pkL, std::abs(l)); pkR = std::max(pkR, std::abs(r)); }
                rt.rt->peakL.store(pkL); rt.rt->peakR.store(pkR);
            }
            // master
            {
                auto& ms = g.master;
                TimeInfo ti; ti.sr = sr; ti.bpm = g.bpm; ti.beat = (double) pos / spb; ti.playing = rolling; ti.beatsPerBar = g.bpb;
                if (rolling) applyAutos(ms, pos);
                for (auto* e : ms.fx) e->run(mL.data(), mR.data(), m, ti);
                float vol = ms.mix ? dbToGain(ms.mix->params[0]->get()) : 1.0f; if (ms.mix && ms.mix->params[4]->on()) vol = 0;
                float pkL = masterL.load() * 0.93f, pkR = masterR.load() * 0.93f;
                for (int i = 0; i < m; ++i) {
                    float l = mL[(size_t) i] * vol, r = mR[(size_t) i] * vol;
                    if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0; }
                    l = clampf(l, -1.0f, 1.0f); r = clampf(r, -1.0f, 1.0f);     // never more than full scale
                    mL[(size_t) i] = l; mR[(size_t) i] = r;
                    pkL = std::max(pkL, std::abs(l)); pkR = std::max(pkR, std::abs(r));
                }
                masterL.store(pkL); masterR.store(pkR);
                if (ms.rt) { ms.rt->peakL.store(pkL); ms.rt->peakR.store(pkR); }
                int of = outFirst.load();
                if (of >= 0 && of < numOut && out[of]) for (int i = 0; i < m; ++i) out[of][off + done + i] += mL[(size_t) i];
                if (of + 1 >= 0 && of + 1 < numOut && out[of + 1]) for (int i = 0; i < m; ++i) out[of + 1][off + done + i] += mR[(size_t) i];
                // the metronome (not in exports)
                int cf = clickFirst.load();
                if (cf != -2) {
                    std::fill(xL.begin(), xL.begin() + m, 0.0f); std::fill(xR.begin(), xR.begin() + m, 0.0f);
                    metronome(g, xL.data(), xR.data(), pos, m);
                    int c0 = cf >= 0 ? cf : of;
                    if (c0 >= 0 && c0 < numOut && out[c0]) for (int i = 0; i < m; ++i) out[c0][off + done + i] += xL[(size_t) i];
                    if (c0 + 1 < numOut && out[c0 + 1]) for (int i = 0; i < m; ++i) out[c0 + 1][off + done + i] += xR[(size_t) i];
                }
            }
            if (rolling) pos += m;
            done += m;
        }
        if (rolling) {
            if (hitLoop) { pos = (int64_t) std::llround(loopA.load() * spb); }
            if (hitJump && (m > 0 || pos >= (int64_t) std::llround(jumpAtB * spb))) { pos = (int64_t) std::llround(jumpToB * spb); jumpAtB = -1;
                for (auto& t : g.tracks) if (t.rt) t.rt->ev.push_back({ 0, NoteEv::AllOff, 0, 0 }); }
            if (hitStop && (m > 0 || pos >= (int64_t) std::llround(stopAtB * spb))) { rolling = false; stopAtB = -1; playing.store(false); }
        }
        if (m == 0 && !hitJump && !hitStop) break;
        if (!rolling && done < n) { // stopped in the middle: the rest of the block is processed stopped
            continue;
        }
    }
    posBeat.store((double) pos / spb);
    if (playing.load() != rolling && !rolling) playing.store(false);
}

// ======================================================================== one track
void Engine::doTrack(const GTrack& t, const Graph& g, int64_t p0, int n, bool roll, const float* const* in, int numIn) {
    if (!t.rt) return;
    TrackRT& rt = *t.rt;
    float* L = rt.L.data(); float* R = rt.R.data();
    std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f);
    // ---- input
    float* iL = rt.inL.data(); float* iR = rt.inR.data();
    std::fill(iL, iL + n, 0.0f); std::fill(iR, iR + n, 0.0f);
    if (t.inType == InputType::Mono && t.inCh >= 0 && t.inCh < numIn && in[t.inCh]) { std::copy(in[t.inCh], in[t.inCh] + n, iL); std::copy(in[t.inCh], in[t.inCh] + n, iR); }
    else if (t.inType == InputType::Stereo && t.inCh >= 0 && t.inCh + 1 < numIn && in[t.inCh] && in[t.inCh + 1]) { std::copy(in[t.inCh], in[t.inCh] + n, iL); std::copy(in[t.inCh + 1], in[t.inCh + 1] + n, iR); }
    else if (t.inType == InputType::Track && t.inTrack >= 0 && t.inTrack < (int) g.tracks.size() && g.tracks[(size_t) t.inTrack].rt) {
        auto& src = *g.tracks[(size_t) t.inTrack].rt; std::copy(src.lastL.begin(), src.lastL.begin() + n, iL); std::copy(src.lastR.begin(), src.lastR.begin() + n, iR); }
    { float pk = rt.inPeak.load() * 0.93f; for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::abs(iL[i]), std::abs(iR[i]))); rt.inPeak.store(pk); }
    bool rec = recRolling.load() && t.arm;
    RecSlot* slot = nullptr;
    if (rec) for (auto* s : activeSlots) if (t.t && s->track == t.t->id && !s->midi) slot = s;
    if (slot && t.recMode == 0) slot->push(iL, iR, n);                     // the dry sound
    TimeInfo ti; ti.sr = sr; ti.bpm = g.bpm; ti.beat = (double) p0 / g.spb; ti.playing = roll; ti.beatsPerBar = g.bpb;
    if (roll) applyAutos(t, p0);
    // ---- sound
    if (t.inst) {
        if (t.follow) followPitch(t, rt, n);
        clipNotes(t, rt, p0, n, roll);
        std::stable_sort(rt.ev.begin(), rt.ev.end(), [](const NoteEv& a, const NoteEv& b) { return a.offset < b.offset; });
        for (auto& e : rt.ev) e.offset = std::max(0, std::min(n - 1, e.offset));
        t.inst->render(L, R, n, rt.ev.data(), (int) rt.ev.size(), ti);
        rt.ev.clear();
    } else {
        bool replace = slot != nullptr;                                   // while this track records, its clips stay quiet
        for (auto& c : t.clips) { if (!c.midi) clipAudio(t, const_cast<GClip&>(c), L, R, roll && !replace ? p0 : -1, n, ti); }
        bool mon = t.monitor == 2 || (t.monitor == 1 && t.arm);
        if (mon) for (int i = 0; i < n; ++i) { L[i] += iL[i]; R[i] += iR[i]; }
        rt.ev.clear();
    }
    for (auto* e : t.fx) e->run(L, R, n, ti);
    if (slot && t.recMode == 1) slot->push(L, R, n);                       // with this track's effects
}

void Engine::clipAudio(const GTrack&, GClip& c, float* L, float* R, int64_t p0, int n, const TimeInfo& ti) {
    bool overlaps = p0 >= 0 && !c.muted && c.src && p0 < c.e && p0 + n > c.s;
    bool hasFx = !c.fx.empty();
    if (!overlaps && !(hasFx && c.rt && c.rt->tail > 0)) return;
    float* dL = L; float* dR = R;
    float tmpL[MAXB], tmpR[MAXB];
    if (hasFx) { std::fill(tmpL, tmpL + n, 0.0f); std::fill(tmpR, tmpR + n, 0.0f); dL = tmpL; dR = tmpR; }
    if (overlaps) {
        const float* sL = c.src->buf.getReadPointer(0); const float* sR = c.src->buf.getReadPointer(1); int64_t sn = c.src->length();
        int64_t a = std::max(p0, c.s), b = std::min(p0 + n, c.e);
        int64_t fin = std::max<int64_t>(c.fin, 48), fout = std::max<int64_t>(c.fout, 48);
        for (int64_t tt = a; tt < b; ++tt) {
            int64_t k = tt - c.s;
            int64_t si = c.rev ? c.off + c.srcN - 1 - k : c.off + k;
            if (si < 0 || si >= sn) continue;
            float g = c.gain;
            if (k < fin) g *= (float) k / (float) fin;
            int64_t left = c.e - tt; if (left < fout) g *= (float) left / (float) fout;
            int i = (int) (tt - p0);
            dL[i] += sL[si] * g; dR[i] += sR[si] * g;
        }
        if (c.rt) { double tail = 0; for (auto* e : c.fx) tail = std::max(tail, e->tailSeconds()); c.rt->tail = (int64_t) (tail * sr); }
    } else if (c.rt) c.rt->tail -= n;
    if (hasFx) {
        for (auto* e : c.fx) e->run(tmpL, tmpR, n, ti);
        for (int i = 0; i < n; ++i) { L[i] += tmpL[i]; R[i] += tmpR[i]; }
    }
}

void Engine::clipNotes(const GTrack& t, TrackRT& rt, int64_t p0, int n, bool roll) {
    bool want[128] = {}; float wantVel[128] = {};
    if (roll) {
        int64_t p1 = p0 + n;
        for (auto& c : t.clips) {
            if (!c.midi || c.muted || c.e <= p0 || c.s >= p1) continue;
            for (auto& nt : c.notes) {
                int64_t s = nt.s, e = std::min(nt.e, c.e);
                if (s >= p1) break;
                if (e <= p0) continue;
                if (s >= p0 && s < p1) rt.ev.push_back({ (int) (s - p0), NoteEv::On, nt.pitch, nt.vel });
                if (e > p0 && e < p1) rt.ev.push_back({ (int) (e - p0), NoteEv::Off, nt.pitch, 0 });
                if (s <= p1 - 1 && e > p1 - 1) { want[nt.pitch & 127] = true; wantVel[nt.pitch & 127] = nt.vel; }
            }
        }
    }
    // what will sound after these events, compared with what should: stop hanging notes, start notes we jumped into the middle of
    bool state[128]; std::copy(std::begin(rt.clipOn), std::end(rt.clipOn), state);
    std::vector<NoteEv> mine; for (auto& e : rt.ev) if (e.type == NoteEv::On || e.type == NoteEv::Off) mine.push_back(e);
    std::stable_sort(mine.begin(), mine.end(), [](const NoteEv& a, const NoteEv& b) { return a.offset < b.offset; });
    for (auto& e : mine) state[e.a & 127] = e.type == NoteEv::On;
    for (int k = 0; k < 128; ++k) {
        if (state[k] && !want[k]) rt.ev.push_back({ n - 1, NoteEv::Off, k, 0 });
        if (!state[k] && want[k] && roll) rt.ev.push_back({ 0, NoteEv::On, k, wantVel[k] });
        rt.clipOn[k] = want[k];
    }
}

void Engine::applyAutos(const GTrack& t, int64_t p) {
    for (auto& a : t.autos) {
        if (!a.p || a.pts.empty()) continue;
        float v;
        if (p <= a.pts.front().first) v = a.pts.front().second;
        else if (p >= a.pts.back().first) v = a.pts.back().second;
        else {
            auto it = std::upper_bound(a.pts.begin(), a.pts.end(), p, [](int64_t x, const std::pair<int64_t, float>& q) { return x < q.first; });
            auto& b = *it; auto& c = *(it - 1);
            v = c.second + (b.second - c.second) * (float) (p - c.first) / (float) std::max<int64_t>(1, b.first - c.first);
        }
        a.p->set(a.p->fromNorm(v));
    }
}

void Engine::followPitch(const GTrack& t, TrackRT& rt, int n) {
    float mono[MAXB]; for (int i = 0; i < n; ++i) mono[i] = 0.5f * (rt.inL[(size_t) i] + rt.inR[(size_t) i]);
    rt.follow.push(mono, n);
    int cand = -1;
    if (rt.follow.hz > 30 && rt.follow.level > 0.01f) cand = (int) std::lround(hzToNote(rt.follow.hz)) + 12 * t.followOct;
    if (cand == rt.followCand) ++rt.followCount; else { rt.followCand = cand; rt.followCount = 0; }
    if (rt.followCount == 3 && cand != rt.followNote) {
        if (rt.followNote >= 0) rt.ev.push_back({ 0, NoteEv::Off, rt.followNote, 0 });
        if (cand >= 0 && cand < 128) rt.ev.push_back({ 0, NoteEv::On, cand, clampf(rt.follow.level * 3.0f, 0.3f, 1.0f) });
        rt.followNote = cand;
    }
}

void Engine::metronome(const Graph& g, float* L, float* R, int64_t p0, int n) {
    bool countIn = recWanted && !recRolling.load() && rolling && p0 < recFrom;
    if (!rolling || !(g.metro || countIn)) { clickT = -1; return; }
    for (int i = 0; i < n; ++i) {
        int64_t p = p0 + i;
        int64_t beat = (int64_t) std::floor((double) p / g.spb);
        int64_t bs = (int64_t) std::llround((double) beat * g.spb);
        if (p == bs) { clickT = 0; clickPh = 0; clickAccent = ((beat % g.bpb) + g.bpb) % g.bpb == 0; }
        if (clickT >= 0) {
            double f = clickAccent ? 1800.0 : 1200.0;
            float env = (float) std::exp(-clickT / (0.012 * sr)) * std::min(1.0f, clickT / 24.0f);
            float s = (float) std::sin(clickPh) * env * g.metroGain * (clickAccent ? 1.0f : 0.7f);
            clickPh += 2 * kPi * f / sr; ++clickT;
            if (clickT > (int64_t) (0.08 * sr)) clickT = -1;
            L[i] += s; R[i] += s;
        }
    }
}

} // namespace score

namespace score {

// ======================================================================== the graph: what the audio thread plays, made from the piece
GraphPtr buildGraph(const Project& p, Engine& e) {
    auto g = std::make_shared<Graph>();
    g->sr = e.sr; g->bpm = p.bpm; g->spb = 60.0 / p.bpm * e.sr; g->bpb = p.beatsPerBar;
    g->metro = p.metro; g->metroGain = dbToGain(p.metroLevel) * 0.8f;
    auto prepFx = [&](Effect* x) { if (!x->isPrepared(e.sr)) x->prepare(e.sr, MAXB); };
    std::map<Uid, int> index; for (size_t i = 0; i < p.tracks.size(); ++i) index[p.tracks[i]->id] = (int) i;
    auto make = [&](const TrackPtr& tp) {
        GTrack gt; gt.track = tp; gt.t = tp.get(); gt.rt = e.runtime(tp->id); gt.kind = tp->kind;
        if (tp->inst) { g->hold.push_back(tp->inst); if (!tp->inst->isPrepared(e.sr)) tp->inst->prepare(e.sr, MAXB); for (auto* x : tp->inst->internalEffects()) prepFx(x); gt.inst = tp->inst.get(); }
        for (auto& x : tp->fx) { g->hold.push_back(x); prepFx(x.get()); gt.fx.push_back(x.get()); }
        g->hold.push_back(tp->mix); gt.mix = tp->mix.get();
        gt.solo = tp->solo; gt.arm = tp->arm; gt.monitor = tp->monitor; gt.inType = tp->inType; gt.inCh = tp->inCh; gt.recMode = tp->recMode;
        gt.follow = tp->follow; gt.followOct = tp->followOct;
        if (tp->inType == InputType::Track) { auto it = index.find(tp->inTrack); gt.inTrack = it == index.end() ? -1 : it->second; }
        for (auto& c : tp->clips) {
            GClip gc; gc.clip = c; gc.src = c->src; gc.rt = c->rt.get(); gc.midi = c->midi; gc.muted = c->muted;
            gc.s = (int64_t) std::llround(c->start * g->spb); gc.e = (int64_t) std::llround(c->end() * g->spb);
            if (!c->midi) {
                gc.off = (int64_t) std::llround(c->offset * e.sr); gc.srcN = (int64_t) std::llround(c->srcLen * e.sr);
                gc.e = gc.s + gc.srcN;
                gc.gain = dbToGain(c->gainDb); gc.fin = (int64_t) (c->fadeIn * e.sr); gc.fout = (int64_t) (c->fadeOut * e.sr); gc.rev = c->reverse;
            } else {
                for (auto& n : c->notes) {
                    if (n.start >= c->length || n.start + n.len <= 0) continue;
                    gc.notes.push_back({ gc.s + (int64_t) std::llround(std::max(0.0, n.start) * g->spb), gc.s + (int64_t) std::llround(std::min(n.start + n.len, c->length) * g->spb), n.pitch, n.vel });
                }
                std::sort(gc.notes.begin(), gc.notes.end(), [](const GNote& a, const GNote& b) { return a.s < b.s; });
            }
            for (auto& x : c->fx) { g->hold.push_back(x); prepFx(x.get()); gc.fx.push_back(x.get()); }
            gt.clips.push_back(std::move(gc));
        }
        std::sort(gt.clips.begin(), gt.clips.end(), [](const GClip& a, const GClip& b) { return a.s < b.s; });
        for (auto& a : tp->autos) {
            GAuto ga; ga.p = p.param(a.path); if (!ga.p || a.pts.empty()) continue;
            for (auto& pt : a.pts) ga.pts.push_back({ (int64_t) std::llround(pt.beat * g->spb), pt.value });
            gt.autos.push_back(std::move(ga));
        }
        return gt;
    };
    for (auto& t : p.tracks) { g->tracks.push_back(make(t)); if (t->solo) g->anySolo = true; }
    g->retA = make(p.retA); g->retB = make(p.retB); g->master = make(p.master);
    return g;
}

} // namespace score
