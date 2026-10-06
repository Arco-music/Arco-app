#include "Project.h"

namespace score {

float AutoLane::valueAt(double beat) const {
    if (pts.empty()) return 0.0f;
    if (beat <= pts.front().beat) return pts.front().value;
    if (beat >= pts.back().beat) return pts.back().value;
    auto it = std::upper_bound(pts.begin(), pts.end(), beat, [](double b, const AutoPoint& p) { return b < p.beat; });
    auto& b = *it; auto& a = *(it - 1);
    double t = (beat - a.beat) / std::max(1e-9, b.beat - a.beat);
    return (float) (a.value + (b.value - a.value) * t);
}

Project::Project() {
    master = std::make_shared<Track>(); master->id = newUid(); master->kind = TrackKind::Master; master->name = "MASTER"; master->colour = 0xffd0d3dc;
    retA = std::make_shared<Track>(); retA->id = newUid(); retA->kind = TrackKind::Return; retA->name = "RETURN A"; retA->colour = 0xff7380ff;
    retB = std::make_shared<Track>(); retB->id = newUid(); retB->kind = TrackKind::Return; retB->name = "RETURN B"; retB->colour = 0xff52a0ff;
    retA->fx.push_back(std::shared_ptr<Effect>(makeEffect("rev"))); retA->fx.back()->find("mix")->set(100);
    retB->fx.push_back(std::shared_ptr<Effect>(makeEffect("dly"))); retB->fx.back()->find("mix")->set(100);
    for (auto& t : { retA, retB, master }) t->inType = InputType::None;
}

juce::uint32 Project::colourFor(int i) {
    static const juce::uint32 C[12] = { 0xfff07373, 0xffffa152, 0xfff8d152, 0xff8fd96b, 0xff4dd1c2, 0xff61a3ff,
                                        0xffa380ff, 0xffed78db, 0xffc7d6f2, 0xffe6b38c, 0xff9ee8ff, 0xffd9e673 };
    return C[((i % 12) + 12) % 12];
}

double Project::length() const {
    double e = 0;
    for (auto& t : tracks) for (auto& c : t->clips) e = std::max(e, c->end());
    for (auto& s : stations) e = std::max(e, s.beat + beatsPerBar);
    return e;
}

std::vector<Track*> Project::allTracks() const {
    std::vector<Track*> v; for (auto& t : tracks) v.push_back(t.get());
    v.push_back(retA.get()); v.push_back(retB.get()); v.push_back(master.get());
    return v;
}

void Project::retire() {
    for (auto& t : tracks) { retired.push_back(t); for (auto& c : t->clips) retired.push_back(c); }
    for (auto* t : { &retA, &retB, &master }) if (*t) retired.push_back(*t);
    if (retired.size() > 4000) retired.erase(retired.begin(), retired.begin() + (long) (retired.size() - 4000));
}

Track* Project::track(Uid id) const { for (auto* t : allTracks()) if (t->id == id) return t; return nullptr; }

Clip* Project::clip(Uid id, Track** owner) const {
    for (auto& t : tracks) for (auto& c : t->clips) if (c->id == id) { if (owner) *owner = t.get(); return c.get(); }
    return nullptr;
}

std::map<std::string, Param*> Project::paramMap() const {
    std::map<std::string, Param*> m;
    auto addOwner = [&](const ParamOwner& o) { for (auto& p : o.params) if (p->modulatable) m[path(o, *p)] = p.get(); };
    for (auto* t : allTracks()) {
        addOwner(*t->mix);
        if (t->inst) { addOwner(*t->inst); for (auto* e : t->inst->internalEffects()) addOwner(*e); }
        for (auto& e : t->fx) addOwner(*e);
        for (auto& c : t->clips) for (auto& e : c->fx) addOwner(*e);
    }
    return m;
}

Param* Project::param(const std::string& p) const {
    auto slash = p.find('/'); if (slash == std::string::npos) return nullptr;
    Uid u = (Uid) std::stoul(p.substr(0, slash)); std::string id = p.substr(slash + 1);
    auto look = [&](const ParamOwner& o) -> Param* { return o.uid == u ? o.find(id) : nullptr; };
    for (auto* t : allTracks()) {
        if (auto* q = look(*t->mix)) return q;
        if (t->inst) { if (auto* q = look(*t->inst)) return q; for (auto* e : t->inst->internalEffects()) if (auto* q = look(*e)) return q; }
        for (auto& e : t->fx) if (auto* q = look(*e)) return q;
        for (auto& c : t->clips) for (auto& e : c->fx) if (auto* q = look(*e)) return q;
    }
    return nullptr;
}

juce::String Project::describe(const std::string& p) const {
    auto slash = p.find('/'); if (slash == std::string::npos) return p;
    Uid u = (Uid) std::stoul(p.substr(0, slash)); std::string id = p.substr(slash + 1);
    for (auto* t : allTracks()) {
        auto nm = [&](const ParamOwner& o, const juce::String& what) -> juce::String {
            if (o.uid != u) return {}; auto* q = o.find(id); return t->name + juce::String::fromUTF8(" · ") + what + juce::String::fromUTF8(" · ") + (q ? juce::String(q->name) : juce::String(id)); };
        if (auto s = nm(*t->mix, "MIXER"); s.isNotEmpty()) return s;
        if (t->inst) { if (auto s = nm(*t->inst, juce::String(t->inst->type).toUpperCase()); s.isNotEmpty()) return s;
            for (auto* e : t->inst->internalEffects()) if (auto s = nm(*e, "SYNTH FX"); s.isNotEmpty()) return s; }
        for (auto& e : t->fx) { auto* i = effectInfo(e->type); if (auto s = nm(*e, i ? i->name : e->type.c_str()); s.isNotEmpty()) return s; }
        for (auto& c : t->clips) for (auto& e : c->fx) { auto* i = effectInfo(e->type); if (auto s = nm(*e, c->name + " " + (i ? i->name : "")); s.isNotEmpty()) return s; }
    }
    return p;
}

TrackPtr Project::newTrack(TrackKind k, const juce::String& nm, const std::string& instType) {
    auto t = std::make_shared<Track>();
    t->id = newUid(); t->kind = k; t->name = nm; t->colour = colourFor((int) tracks.size());
    if (k == TrackKind::Instrument) {
        t->inst = std::shared_ptr<Instrument>(makeInstrument(instType.empty() ? "nova" : instType));
        t->inType = InputType::None;
        if (auto* n = dynamic_cast<Nova*>(t->inst.get())) applyNovaPreset(*n, novaPresets()[0]);
    }
    return t;
}

ClipPtr Project::newAudioClip(SourcePtr src, double startBeat) {
    auto c = std::make_shared<Clip>();
    c->id = newUid(); c->midi = false; c->src = src;
    if (src) { c->file = src->file; c->name = src->name; c->srcLen = src->seconds(); }
    c->start = startBeat; c->length = c->srcLen * bpm / 60.0;
    return c;
}

ClipPtr Project::newMidiClip(double startBeat, double lenBeats) {
    auto c = std::make_shared<Clip>();
    c->id = newUid(); c->midi = true; c->start = startBeat; c->length = lenBeats; c->name = "Notes";
    return c;
}

void Project::syncAudioLengths() {
    for (auto& t : tracks) for (auto& c : t->clips) if (!c->midi) c->length = c->srcLen * bpm / 60.0;
}

void Project::sortStations() { std::stable_sort(stations.begin(), stations.end(), [](const Station& a, const Station& b) { return a.beat < b.beat; }); }
int Project::stationIndex(Uid id) const { for (size_t i = 0; i < stations.size(); ++i) if (stations[i].id == id) return (int) i; return -1; }
Station* Project::stationAt(double beat) {
    Station* s = nullptr;
    for (auto& st : stations) if (st.beat <= beat + 1e-6) s = &st;
    return s;
}

std::map<std::string, float> Project::captureKnobs() const {
    std::map<std::string, float> v;
    for (auto& kv : paramMap()) v[kv.first] = kv.second->get();
    return v;
}
void Project::applyKnobs(const std::map<std::string, float>& v) const {
    auto m = paramMap();
    for (auto& kv : v) { auto it = m.find(kv.first); if (it != m.end()) it->second->set(kv.second); }
}

// ======================================================================== saving
juce::var paramsToVar(const ParamOwner& o) {
    auto* p = new juce::DynamicObject();
    for (auto& q : o.params) p->setProperty(juce::Identifier(q->id), q->get());
    return juce::var(p);
}
void paramsFromVar(ParamOwner& o, const juce::var& v) {
    if (auto* d = v.getDynamicObject()) for (auto& kv : d->getProperties()) if (auto* q = o.find(kv.name.toString().toStdString())) q->set((float) (double) kv.value);
}

static juce::var effectToVar(const Effect& e) {
    auto* o = new juce::DynamicObject();
    o->setProperty("type", juce::String(e.type)); o->setProperty("uid", (int) e.uid); o->setProperty("on", e.enabled.load());
    o->setProperty("p", paramsToVar(e));
    return juce::var(o);
}

static juce::var instToVar(const Instrument& i) {
    auto* o = new juce::DynamicObject();
    o->setProperty("type", juce::String(i.type)); o->setProperty("uid", (int) i.uid); o->setProperty("p", paramsToVar(i));
    juce::Array<juce::var> fx; for (auto* e : const_cast<Instrument&>(i).internalEffects()) fx.add(effectToVar(*e));
    o->setProperty("fx", fx);
    if (auto* n = dynamic_cast<const Nova*>(&i)) { juce::Array<juce::var> ut; for (int k = 0; k < 3; ++k) ut.add(juce::String(n->userTablePath[k])); o->setProperty("tables", ut); }
    if (auto* s = dynamic_cast<const Sampler*>(&i)) { auto sd = s->sample(); o->setProperty("sample", sd ? juce::String(sd->path) : juce::String()); }
    if (auto* d = dynamic_cast<const Drums*>(&i)) { juce::Array<juce::var> ps; for (int k = 0; k < Drums::PADS; ++k) { auto sd = d->padSample(k); ps.add(sd ? juce::String(sd->path) : juce::String()); } o->setProperty("pads", ps); }
    return juce::var(o);
}

static juce::var clipToVar(const Clip& c) {
    auto* o = new juce::DynamicObject();
    o->setProperty("id", (int) c.id); o->setProperty("midi", c.midi); o->setProperty("name", c.name); o->setProperty("colour", (int) c.colour);
    o->setProperty("muted", c.muted); o->setProperty("start", c.start); o->setProperty("length", c.length);
    if (!c.midi) {
        o->setProperty("file", c.file); o->setProperty("offset", c.offset); o->setProperty("srcLen", c.srcLen); o->setProperty("gain", c.gainDb);
        o->setProperty("fi", c.fadeIn); o->setProperty("fo", c.fadeOut); o->setProperty("rev", c.reverse); o->setProperty("orig", c.original);
    } else {
        juce::Array<juce::var> ns;
        for (auto& n : c.notes) { juce::Array<juce::var> a; a.add(n.start); a.add(n.len); a.add(n.pitch); a.add(n.vel); ns.add(a); }
        o->setProperty("notes", ns);
    }
    juce::Array<juce::var> fx; for (auto& e : c.fx) fx.add(effectToVar(*e)); o->setProperty("fx", fx);
    return juce::var(o);
}

static juce::var trackToVar(const Track& t) {
    auto* o = new juce::DynamicObject();
    o->setProperty("id", (int) t.id); o->setProperty("kind", (int) t.kind); o->setProperty("name", t.name); o->setProperty("colour", (int) t.colour);
    if (t.inst) o->setProperty("inst", instToVar(*t.inst));
    juce::Array<juce::var> fx; for (auto& e : t.fx) fx.add(effectToVar(*e)); o->setProperty("fx", fx);
    auto* mx = new juce::DynamicObject(); mx->setProperty("uid", (int) t.mix->uid); mx->setProperty("p", paramsToVar(*t.mix)); o->setProperty("mix", juce::var(mx));
    o->setProperty("solo", t.solo); o->setProperty("monitor", t.monitor); o->setProperty("inType", (int) t.inType); o->setProperty("inCh", t.inCh);
    o->setProperty("inTrack", (int) t.inTrack); o->setProperty("recMode", t.recMode); o->setProperty("follow", t.follow); o->setProperty("followOct", t.followOct);
    o->setProperty("height", t.height); o->setProperty("showAuto", t.showAuto); o->setProperty("autoShown", t.autoShown); o->setProperty("collapsed", t.collapsed);
    juce::Array<juce::var> cl; for (auto& c : t.clips) cl.add(clipToVar(*c)); o->setProperty("clips", cl);
    juce::Array<juce::var> au;
    for (auto& a : t.autos) { auto* ao = new juce::DynamicObject(); ao->setProperty("path", juce::String(a.path)); ao->setProperty("visible", a.visible);
        juce::Array<juce::var> pts; for (auto& p : a.pts) { juce::Array<juce::var> x; x.add(p.beat); x.add(p.value); pts.add(x); } ao->setProperty("pts", pts); au.add(juce::var(ao)); }
    o->setProperty("autos", au);
    return juce::var(o);
}

juce::var Project::toVar() const {
    auto* o = new juce::DynamicObject();
    o->setProperty("app", "SCORE 1.0"); o->setProperty("name", name); o->setProperty("bpm", bpm); o->setProperty("bpb", beatsPerBar);
    o->setProperty("key", key); o->setProperty("scale", scale);
    o->setProperty("loopOn", loopOn); o->setProperty("loopStart", loopStart); o->setProperty("loopEnd", loopEnd);
    o->setProperty("metro", metro); o->setProperty("metroLevel", metroLevel); o->setProperty("countIn", countIn);
    juce::Array<juce::var> tr; for (auto& t : tracks) tr.add(trackToVar(*t)); o->setProperty("tracks", tr);
    o->setProperty("retA", trackToVar(*retA)); o->setProperty("retB", trackToVar(*retB)); o->setProperty("master", trackToVar(*master));
    juce::Array<juce::var> st;
    for (auto& s : stations) {
        auto* so = new juce::DynamicObject();
        so->setProperty("id", (int) s.id); so->setProperty("name", s.name); so->setProperty("note", s.note); so->setProperty("beat", s.beat);
        so->setProperty("glide", s.glide); so->setProperty("curve", s.curve); so->setProperty("atEnd", s.atEnd); so->setProperty("autoGo", s.autoGo);
        so->setProperty("jump", s.jump); so->setProperty("hasSnap", s.hasSnap);
        auto* sn = new juce::DynamicObject(); for (auto& kv : s.snap) sn->setProperty(juce::Identifier(kv.first), kv.second);
        so->setProperty("snap", juce::var(sn));
        st.add(juce::var(so));
    }
    o->setProperty("stations", st);
    return juce::var(o);
}

namespace {
struct Reuse {
    std::map<Uid, std::shared_ptr<Effect>> fx; std::map<Uid, std::shared_ptr<Instrument>> inst; std::map<Uid, std::shared_ptr<Mixer>> mix;
    std::map<Uid, std::shared_ptr<ClipRT>> rt;
    std::map<Uid, TrackPtr> tracks; std::map<Uid, ClipPtr> clips;     // the same objects stay (the screen keeps pointers to them)
    explicit Reuse(const Project* p) {
        if (!p) return;
        for (auto& t : p->tracks) tracks[t->id] = t;
        for (auto* t : { &p->retA, &p->retB, &p->master }) if (*t) tracks[(*t)->id] = *t;
        for (auto& t : p->tracks) for (auto& c : t->clips) clips[c->id] = c;
        for (auto* t : p->allTracks()) {
            mix[t->mix->uid] = t->mix;
            for (auto& e : t->fx) fx[e->uid] = e;
            if (t->inst) inst[t->inst->uid] = t->inst;
            for (auto& c : t->clips) { rt[c->id] = c->rt; for (auto& e : c->fx) fx[e->uid] = e; }
        }
    }
};

std::shared_ptr<Effect> effectFromVar(const juce::var& v, Reuse& r) {
    std::string type = v["type"].toString().toStdString(); Uid uid = (Uid) (int) v["uid"];
    std::shared_ptr<Effect> e;
    auto it = r.fx.find(uid);
    if (it != r.fx.end() && it->second->type == type) e = it->second;
    else { auto u = makeEffect(type); if (!u) return nullptr; e = std::shared_ptr<Effect>(std::move(u)); if (uid) e->uid = uid; }
    bumpUid(e->uid);
    e->enabled.store((bool) v.getProperty("on", true));
    paramsFromVar(*e, v["p"]);
    return e;
}
}

bool Project::fromVar(const juce::var& v, const Project* old, std::function<SourcePtr(const juce::String&)> loadSource) {
    if (!v.isObject()) return false;
    Reuse r(old);
    auto sampleFrom = [&](const juce::String& f) -> std::shared_ptr<SampleData> {
        if (f.isEmpty()) return nullptr;
        auto s = loadSource(f); if (!s) return nullptr;
        auto d = std::make_shared<SampleData>(); d->sr = s->sr; d->path = f.toStdString(); d->name = s->name.toStdString();
        d->L.assign(s->buf.getReadPointer(0), s->buf.getReadPointer(0) + s->length()); d->R.assign(s->buf.getReadPointer(1), s->buf.getReadPointer(1) + s->length());
        d->slices = findOnsets(*d, 32);
        return d;
    };
    auto readTrack = [&](const juce::var& tv, TrackPtr into) {
        auto& t = *into;
        t.id = (Uid) (int) tv["id"]; bumpUid(t.id);
        t.kind = (TrackKind) (int) tv["kind"]; t.name = tv["name"].toString(); t.colour = (juce::uint32) (int) tv["colour"];
        auto iv = tv["inst"];
        if (iv.isObject()) {
            std::string type = iv["type"].toString().toStdString(); Uid uid = (Uid) (int) iv["uid"];
            auto it = r.inst.find(uid);
            if (it != r.inst.end() && it->second->type == type) t.inst = it->second;
            else { t.inst = std::shared_ptr<Instrument>(makeInstrument(type)); if (t.inst && uid) t.inst->uid = uid; }
            if (t.inst) {
                bumpUid(t.inst->uid);
                t.inst->resetToDefaults(); paramsFromVar(*t.inst, iv["p"]);
                auto ie = t.inst->internalEffects();
                if (auto* a = iv["fx"].getArray()) for (int k = 0; k < juce::jmin(a->size(), (int) ie.size()); ++k) {
                    ie[(size_t) k]->uid = (Uid) (int) (*a)[k]["uid"] ? (Uid) (int) (*a)[k]["uid"] : ie[(size_t) k]->uid; bumpUid(ie[(size_t) k]->uid);
                    ie[(size_t) k]->enabled.store((bool) (*a)[k].getProperty("on", true)); paramsFromVar(*ie[(size_t) k], (*a)[k]["p"]); }
                if (auto* n = dynamic_cast<Nova*>(t.inst.get())) if (auto* a = iv["tables"].getArray())
                    for (int k = 0; k < juce::jmin(3, a->size()); ++k) { auto f = (*a)[k].toString(); n->userTablePath[k] = f.toStdString();
                        if (f.isNotEmpty()) if (auto s = loadSource(f)) n->userTable[k] = wavetableFromAudio(s->buf.getReadPointer(0), s->length(), s->sr, s->name.toStdString()); }
                if (auto* s = dynamic_cast<Sampler*>(t.inst.get())) s->setSample(sampleFrom(iv["sample"].toString()));
                if (auto* d = dynamic_cast<Drums*>(t.inst.get())) if (auto* a = iv["pads"].getArray())
                    for (int k = 0; k < juce::jmin(Drums::PADS, a->size()); ++k) d->setPadSample(k, sampleFrom((*a)[k].toString()));
            }
        } else t.inst = nullptr;
        t.fx.clear(); if (auto* a = tv["fx"].getArray()) for (auto& e : *a) if (auto ef = effectFromVar(e, r)) t.fx.push_back(ef);
        auto mv = tv["mix"]; Uid mu = (Uid) (int) mv["uid"];
        auto mi = r.mix.find(mu); if (mi != r.mix.end()) t.mix = mi->second; else { t.mix = std::make_shared<Mixer>(); if (mu) t.mix->uid = mu; }
        bumpUid(t.mix->uid); t.mix->resetToDefaults(); paramsFromVar(*t.mix, mv["p"]);
        t.solo = (bool) tv["solo"]; t.monitor = (int) tv.getProperty("monitor", 1); t.inType = (InputType) (int) tv.getProperty("inType", 1);
        t.inCh = (int) tv["inCh"]; t.inTrack = (Uid) (int) tv["inTrack"]; t.recMode = (int) tv["recMode"]; t.follow = (bool) tv["follow"]; t.followOct = (int) tv["followOct"];
        t.height = (float) (double) tv.getProperty("height", 76.0); t.showAuto = (bool) tv["showAuto"]; t.autoShown = tv["autoShown"].toString(); t.collapsed = (bool) tv["collapsed"];
        t.clips.clear();
        if (auto* a = tv["clips"].getArray()) for (auto& cv : *a) {
            Uid cid = (Uid) (int) cv["id"];
            ClipPtr c; if (auto ci = r.clips.find(cid); ci != r.clips.end()) { c = ci->second; *c = Clip(); } else c = std::make_shared<Clip>();
            c->id = cid; bumpUid(c->id); auto ri = r.rt.find(c->id); if (ri != r.rt.end()) c->rt = ri->second;
            c->midi = (bool) cv["midi"]; c->name = cv["name"].toString(); c->colour = (juce::uint32) (int) cv["colour"]; c->muted = (bool) cv["muted"];
            c->start = (double) cv["start"]; c->length = (double) cv["length"];
            if (!c->midi) {
                c->file = cv["file"].toString(); c->offset = (double) cv["offset"]; c->srcLen = (double) cv["srcLen"]; c->gainDb = (float) (double) cv["gain"];
                c->fadeIn = (double) cv["fi"]; c->fadeOut = (double) cv["fo"]; c->reverse = (bool) cv["rev"]; c->original = cv["orig"].toString();
                c->src = loadSource(c->file);
            } else if (auto* ns = cv["notes"].getArray()) for (auto& nv : *ns) {
                if (auto* x = nv.getArray(); x && x->size() >= 4) c->notes.push_back({ (double) (*x)[0], (double) (*x)[1], (int) (*x)[2], (float) (double) (*x)[3] });
            }
            if (auto* fa = cv["fx"].getArray()) for (auto& e : *fa) if (auto ef = effectFromVar(e, r)) c->fx.push_back(ef);
            t.clips.push_back(c);
        }
        t.autos.clear();
        if (auto* a = tv["autos"].getArray()) for (auto& av : *a) {
            AutoLane l; l.path = av["path"].toString().toStdString(); l.visible = (bool) av.getProperty("visible", true);
            if (auto* p = av["pts"].getArray()) for (auto& x : *p) if (auto* xa = x.getArray(); xa && xa->size() >= 2) l.pts.push_back({ (double) (*xa)[0], (float) (double) (*xa)[1] });
            t.autos.push_back(l);
        }
    };
    name = v.getProperty("name", "Untitled").toString(); bpm = (double) v.getProperty("bpm", 110.0); beatsPerBar = (int) v.getProperty("bpb", 4);
    key = (int) v["key"]; scale = (int) v["scale"];
    loopOn = (bool) v["loopOn"]; loopStart = (double) v["loopStart"]; loopEnd = (double) v.getProperty("loopEnd", 16.0);
    metro = (bool) v["metro"]; metroLevel = (float) (double) v.getProperty("metroLevel", -12.0); countIn = (int) v.getProperty("countIn", 1);
    // everything that was here stays alive for a while: the screen may still point at it until it rebuilds
    retire();
    auto trackFor = [&](const juce::var& tv) { Uid id = (Uid) (int) tv["id"]; auto it = r.tracks.find(id); return it != r.tracks.end() ? it->second : std::make_shared<Track>(); };
    tracks.clear();
    if (auto* a = v["tracks"].getArray()) for (auto& tv : *a) { auto t = trackFor(tv); readTrack(tv, t); tracks.push_back(t); }
    if (v["retA"].isObject()) { retA = trackFor(v["retA"]); readTrack(v["retA"], retA); }
    if (v["retB"].isObject()) { retB = trackFor(v["retB"]); readTrack(v["retB"], retB); }
    if (v["master"].isObject()) { master = trackFor(v["master"]); readTrack(v["master"], master); }
    stations.clear();
    if (auto* a = v["stations"].getArray()) for (auto& sv : *a) {
        Station s; s.id = (Uid) (int) sv["id"]; bumpUid(s.id); s.name = sv["name"].toString(); s.note = sv["note"].toString(); s.beat = (double) sv["beat"];
        s.glide = (float) (double) sv.getProperty("glide", 2.0); s.curve = (int) sv.getProperty("curve", 1); s.atEnd = (int) sv["atEnd"];
        s.autoGo = (float) (double) sv["autoGo"]; s.jump = (int) sv["jump"]; s.hasSnap = (bool) sv["hasSnap"];
        if (auto* d = sv["snap"].getDynamicObject()) for (auto& kv : d->getProperties()) s.snap[kv.name.toString().toStdString()] = (float) (double) kv.value;
        stations.push_back(s);
    }
    sortStations();
    return true;
}

} // namespace score
