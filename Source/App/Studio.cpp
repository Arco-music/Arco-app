#include "Studio.h"
#include "AudioHost.h"

namespace arco {

static const char* LL[4] = { "A", "B", "C", "D" };

bool Studio::buildFromStation(int repeats, juce::String& why) {
    auto& e = host.engine;
    auto sv = [&](const std::string& n) { return (double) e.snap(n).v.load(); };
    int smask = (int) sv("smask");
    if (smask == 0) { why = "Record some loops first - STUDIO lays them out as a song."; return false; }
    StudioSong s;
    s.sr = e.sr; s.beats = juce::jmax(1, (int) std::round(sv("sv_bts")));
    double bl = sv("sv_bl"); s.bpm = bl > 16 ? 60.0 * e.sr / bl : host.model.get("bpm");
    laneStrip.clear(); laneScene.clear();
    int laneOf[8]; for (auto& x : laneOf) x = -1;
    // which tracks were recorded anywhere: one lane each
    for (int t = 0; t < 8; ++t)
        for (int sc = 1; sc <= 4; ++sc)
            if (((smask >> (sc - 1)) & 1) && (((int) sv("sv_mask" + std::to_string(sc - 1)) >> t) & 1) && laneOf[t] < 0) {
                laneOf[t] = (int) s.lanes.size();
                StudioLane L; L.name = "TRACK " + juce::String(t + 1); L.colour = 4 + t;
                double v = host.model.get("vol" + std::to_string(4 + t));
                L.vol = (float) juce::jlimit(-40.0, 6.0, 20.0 * std::log10(std::max(1e-4, std::pow(v / 100.0, 2.0) * 2.0)));
                L.pan = (float) host.model.get("pan" + std::to_string(4 + t)) / 50.0f;
                s.lanes.push_back(L); laneStrip.push_back(4 + t); laneScene.push_back(sc);
            }
    long at = 0;
    for (int sc = 1; sc <= 4; ++sc) {
        if (!((smask >> (sc - 1)) & 1)) continue;
        long len = (long) sv("sv_len" + std::to_string(sc - 1)); int mask = (int) sv("sv_mask" + std::to_string(sc - 1));
        auto& b = e.buffer(std::string("st_loops") + LL[sc - 1]);
        len = std::min(len, b.len);
        if (len < 64) continue;
        for (int t = 0; t < 8; ++t) {
            if (!((mask >> t) & 1)) continue;
            StudioSource src; src.name = juce::String("scene ") + LL[sc - 1] + " - track " + juce::String(t + 1);
            src.l.assign(b.chan(t), b.chan(t) + len);
            s.sources.push_back(std::move(src));
            StudioClip c; c.lane = laneOf[t]; c.source = (int) s.sources.size() - 1; c.start = at; c.length = len * repeats; c.loop = true;
            c.fadeIn = c.fadeOut = (float) (0.004 * s.sr);
            s.clips.push_back(c);
        }
        at += len * repeats;
    }
    const juce::ScopedLock sl(lock);
    song = std::move(s); dsp.clear(); pos = 0; undoStack.clear(); redoStack.clear();
    return true;
}

void Studio::design(LaneDsp& d, const StudioLane& L, double sr) {
    for (int c = 0; c < 2; ++c) {
        if (L.hp > 15) { d.f[c][0].highpass(sr, L.hp, 0.5412); d.f[c][1].highpass(sr, L.hp, 1.3066); } else { d.f[c][0].flat(); d.f[c][1].flat(); }
        if (std::abs(L.lo) > 0.05f) d.f[c][2].lowShelf(sr, 120, L.lo); else d.f[c][2].flat();
        if (std::abs(L.mid) > 0.05f) d.f[c][3].peak(sr, std::max(60.0f, L.midHz), 1.0, L.mid); else d.f[c][3].flat();
        if (std::abs(L.hi) > 0.05f) d.f[c][4].highShelf(sr, 8000, L.hi); else d.f[c][4].flat();
    }
    d.cfg = L; d.designed = true;
}

void Studio::mixRange(const StudioSong& song, float* outL, float* outR, long start, int n, std::vector<LaneDsp>& D, float& gEnv, float& gG) {
    int nl = (int) song.lanes.size();
    if ((int) D.size() != nl) D.assign((size_t) nl, LaneDsp());
    bool anySolo = false; for (auto& L : song.lanes) anySolo |= L.solo;
    std::vector<float> bl((size_t) n), br((size_t) n);
    std::vector<float> ml((size_t) n, 0.0f), mr((size_t) n, 0.0f);
    for (int li = 0; li < nl; ++li) {
        auto& L = song.lanes[(size_t) li];
        auto& d = D[(size_t) li];
        if (!d.designed || d.cfg.lo != L.lo || d.cfg.mid != L.mid || d.cfg.hi != L.hi || d.cfg.midHz != L.midHz || d.cfg.hp != L.hp || d.cfg.comp != L.comp) design(d, L, song.sr);
        if (L.mute || (anySolo && !L.solo)) continue;
        std::fill(bl.begin(), bl.end(), 0.0f); std::fill(br.begin(), br.end(), 0.0f);
        bool any = false;
        for (auto& c : song.clips) {
            if (c.lane != li || c.start >= start + n || c.start + c.length <= start) continue;
            if (c.source < 0 || c.source >= (int) song.sources.size()) continue;
            auto& S = song.sources[(size_t) c.source];
            long sl = S.size(); if (sl <= 0) continue;
            bool st = !S.r.empty();
            float g = std::pow(10.0f, c.gainDb / 20.0f);
            long a = std::max(start, c.start), b = std::min(start + n, c.start + c.length);
            for (long p = a; p < b; ++p) {
                long rel = p - c.start, si = rel + c.offset;
                if (c.loop) si %= sl; else if (si >= sl) continue;
                float w = g;
                if (c.fadeIn > 1 && rel < c.fadeIn) w *= rel / c.fadeIn;
                long toEnd = c.start + c.length - p;
                if (c.fadeOut > 1 && toEnd < c.fadeOut) w *= toEnd / c.fadeOut;
                bl[(size_t) (p - start)] += S.l[(size_t) si] * w;
                br[(size_t) (p - start)] += (st ? S.r[(size_t) si] : S.l[(size_t) si]) * w;
            }
            any = true;
        }
        if (!any && d.env < 1e-5f) continue;
        float vol = std::pow(10.0f, L.vol / 20.0f);
        float pan = juce::jlimit(-1.0f, 1.0f, L.pan), pl = std::cos((pan + 1) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f, pr = std::sin((pan + 1) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
        float thr = -6.0f - L.comp * 0.3f, ratio = 1.0f + L.comp / 25.0f;
        float att = 1.0f - std::exp(-1.0f / (float) (0.01 * song.sr)), rel = 1.0f - std::exp(-1.0f / (float) (0.12 * song.sr));
        for (int i = 0; i < n; ++i) {
            float x[2] = { bl[(size_t) i], br[(size_t) i] };
            for (int c = 0; c < 2; ++c) for (int f = 0; f < 5; ++f) x[c] = d.f[c][f].tick(x[c]);
            if (L.comp > 0.5f) {
                float a = std::max(std::abs(x[0]), std::abs(x[1]));
                d.env += (a > d.env ? att : rel) * (a - d.env);
                float lev = 20.0f * std::log10(std::max(d.env, 1e-6f)), over = lev - thr;
                float red = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
                float cg = std::pow(10.0f, (-red + L.comp * 0.06f) / 20.0f);
                d.gSm += 0.02f * (cg - d.gSm);
                x[0] *= d.gSm; x[1] *= d.gSm;
            }
            ml[(size_t) i] += x[0] * vol * pl; mr[(size_t) i] += x[1] * vol * pr;
        }
    }
    // master: glue, level, a soft safety
    float mg = std::pow(10.0f, song.masterDb / 20.0f);
    float gthr = -10.0f - song.glue * 0.1f, gratio = 1.0f + song.glue / 40.0f;
    float att = 1.0f - std::exp(-1.0f / (float) (0.02 * song.sr)), rel = 1.0f - std::exp(-1.0f / (float) (0.2 * song.sr));
    for (int i = 0; i < n; ++i) {
        float l = ml[(size_t) i], r = mr[(size_t) i];
        if (song.glue > 0.5f) {
            float a = std::max(std::abs(l), std::abs(r));
            gEnv += (a > gEnv ? att : rel) * (a - gEnv);
            float over = 20.0f * std::log10(std::max(gEnv, 1e-6f)) - gthr;
            float cg = std::pow(10.0f, -(over > 0 ? over * (1 - 1 / gratio) : 0) / 20.0f);
            gG += 0.01f * (cg - gG); l *= gG; r *= gG;
        }
        l *= mg; r *= mg;
        outL[i] += l; outR[i] += r;
    }
}

void Studio::renderBlock(float* l, float* r, int n) {
    if (!playing.load()) return;
    const juce::ScopedTryLock sl(lock);
    if (!sl.isLocked()) return;
    long p = pos.load();
    std::vector<float> tl((size_t) n, 0.0f), tr((size_t) n, 0.0f);
    mixRange(song, tl.data(), tr.data(), p, n, dsp, glueEnv, glueG);
    for (int i = 0; i < n; ++i) { l[i] += std::tanh(tl[(size_t) i]); r[i] += std::tanh(tr[(size_t) i]); }
    p += n;
    if (p > song.length() + (long) song.sr) { playing = false; p = 0; }
    pos = p;
}

bool Studio::renderAll(juce::AudioBuffer<float>& out, std::function<bool(float)> progress) {
    StudioSong copy; { const juce::ScopedLock sl(lock); copy = song; }
    long total = copy.length() + (long) (copy.sr * 1.0);
    if (total <= 0) return false;
    out.setSize(2, (int) total); out.clear();
    std::vector<LaneDsp> D; float ge = 0, gg = 1;
    const int B = 1024;
    bool ok = true;
    for (long p = 0; p < total; p += B) {
        int n = (int) std::min((long) B, total - p);
        mixRange(copy, out.getWritePointer(0) + p, out.getWritePointer(1) + p, p, n, D, ge, gg);
        if (progress && (p / B) % 64 == 0 && !progress((float) p / total)) { ok = false; break; }
    }
    return ok;
}

void Studio::snapshot() { const juce::ScopedLock sl(lock); undoStack.push_back(song); if (undoStack.size() > 60) undoStack.erase(undoStack.begin()); redoStack.clear(); }
bool Studio::undo() { const juce::ScopedLock sl(lock); if (undoStack.empty()) return false; redoStack.push_back(song); song = undoStack.back(); undoStack.pop_back(); return true; }
bool Studio::redo() { const juce::ScopedLock sl(lock); if (redoStack.empty()) return false; undoStack.push_back(song); song = redoStack.back(); redoStack.pop_back(); return true; }

bool Studio::save(const juce::File& folder) {
    auto dir = folder.getChildFile("studio"); dir.createDirectory();
    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "source*.wav")) f.deleteFile();
    juce::WavAudioFormat wav;
    StudioSong s; { const juce::ScopedLock sl(lock); s = song; }
    for (size_t i = 0; i < s.sources.size(); ++i) {
        auto& S = s.sources[i];
        auto f = dir.getChildFile("source" + juce::String((int) i) + ".wav");
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), s.sr, S.r.empty() ? 1 : 2, 32, {}, 0));
        if (!w) return false;
        const float* ch[2] = { S.l.data(), S.r.empty() ? S.l.data() : S.r.data() };
        w->writeFromFloatArrays(ch, S.r.empty() ? 1 : 2, (int) S.l.size());
    }
    auto* o = new juce::DynamicObject();
    o->setProperty("sr", s.sr); o->setProperty("bpm", s.bpm); o->setProperty("beats", s.beats); o->setProperty("master", s.masterDb); o->setProperty("glue", s.glue);
    juce::Array<juce::var> srcs, clips, lanes;
    for (auto& S : s.sources) srcs.add(S.name);
    for (auto& c : s.clips) { auto* x = new juce::DynamicObject(); x->setProperty("lane", c.lane); x->setProperty("source", c.source); x->setProperty("start", (juce::int64) c.start);
        x->setProperty("length", (juce::int64) c.length); x->setProperty("offset", (juce::int64) c.offset); x->setProperty("loop", c.loop); x->setProperty("gain", c.gainDb);
        x->setProperty("fin", c.fadeIn); x->setProperty("fout", c.fadeOut); clips.add(juce::var(x)); }
    for (auto& L : s.lanes) { auto* x = new juce::DynamicObject(); x->setProperty("name", L.name); x->setProperty("colour", L.colour); x->setProperty("vol", L.vol); x->setProperty("pan", L.pan);
        x->setProperty("mute", L.mute); x->setProperty("solo", L.solo); x->setProperty("lo", L.lo); x->setProperty("mid", L.mid); x->setProperty("hi", L.hi); x->setProperty("midHz", L.midHz);
        x->setProperty("comp", L.comp); x->setProperty("hp", L.hp); lanes.add(juce::var(x)); }
    o->setProperty("sources", srcs); o->setProperty("clips", clips); o->setProperty("lanes", lanes);
    return dir.getChildFile("studio.json").replaceWithText(juce::JSON::toString(juce::var(o)));
}

bool Studio::load(const juce::File& folder) {
    auto dir = folder.getChildFile("studio");
    auto v = juce::JSON::parse(dir.getChildFile("studio.json"));
    if (!v.isObject()) return false;
    StudioSong s;
    s.sr = v.getProperty("sr", 48000.0); s.bpm = v.getProperty("bpm", 120.0); s.beats = v.getProperty("beats", 4); s.masterDb = v.getProperty("master", 0.0f); s.glue = v.getProperty("glue", 20.0f);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    if (auto* a = v["sources"].getArray())
        for (int i = 0; i < a->size(); ++i) {
            StudioSource S; S.name = (*a)[i].toString();
            std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(dir.getChildFile("source" + juce::String(i) + ".wav")));
            if (r) { juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, b.getNumSamples(), 0, true, true);
                S.l.assign(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples()); if (b.getNumChannels() > 1) S.r.assign(b.getReadPointer(1), b.getReadPointer(1) + b.getNumSamples()); }
            s.sources.push_back(std::move(S));
        }
    if (auto* a = v["clips"].getArray()) for (auto& x : *a) { StudioClip c; c.lane = x["lane"]; c.source = x["source"]; c.start = (juce::int64) x["start"]; c.length = (juce::int64) x["length"];
        c.offset = (juce::int64) x["offset"]; c.loop = x["loop"]; c.gainDb = x["gain"]; c.fadeIn = x["fin"]; c.fadeOut = x["fout"]; s.clips.push_back(c); }
    if (auto* a = v["lanes"].getArray()) for (auto& x : *a) { StudioLane L; L.name = x["name"].toString(); L.colour = x["colour"]; L.vol = x["vol"]; L.pan = x["pan"]; L.mute = x["mute"]; L.solo = x["solo"];
        L.lo = x["lo"]; L.mid = x["mid"]; L.hi = x["hi"]; L.midHz = x["midHz"]; L.comp = x["comp"]; L.hp = x["hp"]; s.lanes.push_back(L); }
    const juce::ScopedLock sl(lock);
    song = std::move(s); dsp.clear(); pos = 0; undoStack.clear(); redoStack.clear();
    laneStrip.assign(song.lanes.size(), -1); laneScene.assign(song.lanes.size(), 1);
    return true;
}

bool Studio::printEffects(int lane, std::function<bool(float)> progress) {
    if (lane < 0 || lane >= (int) laneStrip.size() || laneStrip[(size_t) lane] < 0) return false;
    Renderer rd(host);
    auto snap = rd.snapshot();
    // every source this lane plays: its scene rendered with only this track, twice round (so the echoes wrap into the loop)
    std::vector<int> srcs; { const juce::ScopedLock sl(lock); for (auto& c : song.clips) if (c.lane == lane && std::find(srcs.begin(), srcs.end(), c.source) == srcs.end()) srcs.push_back(c.source); }
    int k = 0;
    for (int si : srcs) {
        juce::String nm; long len; { const juce::ScopedLock sl(lock); nm = song.sources[(size_t) si].name; len = song.sources[(size_t) si].size(); }
        int sc = nm.contains("scene B") ? 2 : nm.contains("scene C") ? 3 : nm.contains("scene D") ? 4 : 1;
        snap.sceneNow = sc;
        ExportOptions o; o.what = ExportOptions::CurrentScene; o.repeats = 2; o.tailSeconds = 0;
        juce::AudioBuffer<float> b; double rate = 0;
        bool ok = rd.renderMix(snap, o, b, rate, laneStrip[(size_t) lane], [&](float p) { return !progress || progress((k + p) / (float) srcs.size()); });
        if (!ok || b.getNumSamples() < 2 * len) return false;
        StudioSource S; S.name = nm + " (with effects)";
        S.l.assign(b.getReadPointer(0) + len, b.getReadPointer(0) + 2 * len); S.r.assign(b.getReadPointer(1) + len, b.getReadPointer(1) + 2 * len);
        const juce::ScopedLock sl(lock);
        song.sources[(size_t) si] = std::move(S);
        ++k;
    }
    const juce::ScopedLock sl(lock);
    song.lanes[(size_t) lane].name = song.lanes[(size_t) lane].name.upToFirstOccurrenceOf(" +FX", false, false) + " +FX";
    return true;
}

} // namespace arco
