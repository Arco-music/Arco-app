#include "Host.h"
#include "../Core/Stretch.h"

#if JUCE_IOS
void scorePreferBestMicrophone();       // IOSAudio.mm
#endif

namespace score {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }

// ======================================================================== recording writer thread
void RecWriter::drain(Item& it) {
    auto& f = it.slot->fifo;
    int ready = f.getNumReady();
    while (ready > 0) {
        int s1, n1, s2, n2; f.prepareToRead(ready, s1, n1, s2, n2);
        auto write = [&](int start, int count) {
            if (count <= 0) return;
            std::vector<float> l((size_t) count), r((size_t) count);
            for (int i = 0; i < count; ++i) { l[(size_t) i] = it.slot->data[(size_t) (start + i) * 2]; r[(size_t) i] = it.slot->data[(size_t) (start + i) * 2 + 1]; }
            const float* ch[2] = { l.data(), r.data() };
            if (it.writer) it.writer->writeFromFloatArrays(ch, 2, count);
            std::lock_guard<std::mutex> g(it.slot->peakLock);
            for (int i = 0; i < count; i += 64) { float pk = 0; for (int k = i; k < std::min(count, i + 64); ++k) pk = std::max(pk, std::max(std::abs(l[(size_t) k]), std::abs(r[(size_t) k])));
                it.slot->peaks.push_back(pk); }
        };
        write(s1, n1); write(s2, n2);
        f.finishedRead(n1 + n2);
        ready = f.getNumReady();
    }
}
void RecWriter::run() {
    while (!threadShouldExit()) {
        { std::lock_guard<std::mutex> g(lock); for (auto& it : items) drain(it); }
        wait(15);
    }
}

// ======================================================================== the host
namespace {
// every note of the keyboards (screen, computer keys, MIDI) goes to the instrument track that has the keys
struct KeysToEngine : juce::MidiKeyboardState::Listener {
    explicit KeysToEngine(Host& hh) : h(hh) {}
    Host& h;
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float vel) override { h.engine.liveNote(h.engine.keysTarget.load(), { 0, NoteEv::On, note, vel }); }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override { h.engine.liveNote(h.engine.keysTarget.load(), { 0, NoteEv::Off, note, 0 }); }
};
}

Host::Host(bool openDevice) : live(openDevice) {
    formats.registerBasicFormats();
    writer.startThread();
    engine.prepare(48000.0);
    keysListener = std::make_unique<KeysToEngine>(*this);
    keyboard.addListener(keysListener.get());
    newProject(true);
    loadSetup(openDevice);
    startTimerHz(30);
}

Host::~Host() {
    if (live) saveSetup();
    if (recording()) finishRecording();
    devices.removeMidiInputDeviceCallback({}, this);
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
    if (exportThread) exportThread->stopThread(5000);
    writer.stopThread(2000);
    // an untitled piece nobody saved, with nothing in it: no folder left behind
    if (project && !project->folder.getChildFile("project.json").existsAsFile() && audioFolder().getNumberOfChildFiles(juce::File::findFiles) == 0)
        project->folder.deleteRecursively();
}

juce::File Host::songsFolder() {
   #if JUCE_IOS
    auto f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
   #elif JUCE_MAC
    auto f = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("SCORE");
   #else
    auto f = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("SCORE");
   #endif
    f.createDirectory();
    return f;
}

juce::File Host::setupFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SCORE").getChildFile("setup.json");
}

// ======================================================================== editing, undo
void Host::edit(const juce::String& what, std::function<void()> change, bool rb) {
    auto before = juce::JSON::toString(project->toVar(), true);
    project->retire();
    change();
    undos.push_back({ what, before }); redos.clear();
    size_t total = 0; for (auto& u : undos) total += (size_t) u.second.length();
    while (undos.size() > 1 && (undos.size() > 200 || total > 60u * 1024 * 1024)) { total -= (size_t) undos.front().second.length(); undos.erase(undos.begin()); }
    dirty = true;
    if (rb) { rebuild(); ++structure; }
    sendChangeMessage();
}

void Host::beginGesture() { project->retire(); gestureBefore = juce::JSON::toString(project->toVar(), true); }
void Host::endGesture(const juce::String& what, bool changed) {
    if (gestureBefore.isEmpty()) return;
    if (changed) { undos.push_back({ what, gestureBefore }); redos.clear(); dirty = true; }
    gestureBefore = {};
    rebuild(); ++structure; sendChangeMessage();
}

void Host::knobTouched() { if (pendingKnob.isEmpty()) pendingKnob = juce::JSON::toString(project->toVar(), true); knobAt = juce::Time::getMillisecondCounterHiRes(); dirty = true; }

void Host::rebuild() {
    engine.setGraph(buildGraph(*project, engine));
    if (!stations.ownLoop) engine.setLoop(project->loopOn, project->loopStart, project->loopEnd);
}

void Host::undo() {
    if (undos.empty()) return;
    auto now = juce::JSON::toString(project->toVar(), true);
    auto u = undos.back(); undos.pop_back();
    redos.push_back({ u.first, now });
    project->fromVar(juce::JSON::parse(u.second), project.get(), [this](const juce::String& f) { return loadSource(f); });
    rebuild(); ++structure; dirty = true;
    say("undo: " + u.first);
}
void Host::redo() {
    if (redos.empty()) return;
    auto now = juce::JSON::toString(project->toVar(), true);
    auto r = redos.back(); redos.pop_back();
    undos.push_back({ r.first, now });
    project->fromVar(juce::JSON::parse(r.second), project.get(), [this](const juce::String& f) { return loadSource(f); });
    rebuild(); ++structure; dirty = true;
    say("redo: " + r.first);
}

void Host::selectTrack(Uid t) {
    selTrack = t;
    if (auto* tr = project->track(t)) if (tr->isInstrument()) engine.keysTarget.store(t);
    sendChangeMessage();
}
void Host::selectClip(Uid c, bool add) {
    if (!add) selClips.clear();
    if (c) selClips.insert(c);
    selClip = c;
    Track* owner = nullptr; if (project->clip(c, &owner) && owner) selectTrack(owner->id);
    sendChangeMessage();
}

// ======================================================================== the piece on disk
void Host::newProject(bool demo) {
    if (recording()) finishRecording();
    engine.stop();
    auto base = live ? songsFolder() : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("SCORE test songs");
    base.createDirectory();
    juce::String nm = "Untitled"; int k = 1;
    while (base.getChildFile(nm + ".score").exists()) nm = "Untitled " + juce::String(++k);
    auto p = std::make_shared<Project>();
    p->name = nm; p->folder = base.getChildFile(nm + ".score");
    p->folder.createDirectory(); p->folder.getChildFile("Audio").createDirectory();
    if (demo) {
        auto a = p->newTrack(TrackKind::Audio, "Audio 1"); a->inType = InputType::Mono; a->inCh = 0;
        p->tracks.push_back(a);
        auto s = p->newTrack(TrackKind::Instrument, "NOVA", "nova");
        if (auto* n = dynamic_cast<Nova*>(s->inst.get())) for (auto& pr : novaPresets()) if (pr.name == "Soft Keys") applyNovaPreset(*n, pr);
        p->tracks.push_back(s);
        Station st; st.id = newUid(); st.name = "Start"; st.beat = 0;
        st.snap = p->captureKnobs(); st.hasSnap = true;
        p->stations.push_back(st);
        selTrack = a->id;
    }
    if (project) { previous.push_back(project); if (previous.size() > 4) previous.erase(previous.begin()); }
    project = p;
    undos.clear(); redos.clear(); pool.clear(); selClip = 0; selClips.clear();
    stations.current = project->stations.empty() ? -1 : 0; stations.log.clear();
    for (auto& t : project->tracks) if (t->isInstrument()) { engine.keysTarget.store(t->id); break; }
    rebuild(); ++structure; dirty = false;
    sendChangeMessage();
}

bool Host::openProject(const juce::File& sel) {
    juce::File folder = sel, json;
    if (sel.hasFileExtension("zip")) {                    // a piece sent as one file: unpack it next to the others
        juce::ZipFile z(sel);
        auto dest = songsFolder();
        if (z.uncompressTo(dest).failed()) { say("could not open " + sel.getFileName()); return false; }
        auto name = z.getNumEntries() > 0 ? z.getEntry(0)->filename.upToFirstOccurrenceOf("/", false, false) : juce::String();
        folder = dest.getChildFile(name);
    }
    if (folder.isDirectory()) json = folder.getChildFile("project.json");
    else { json = folder; folder = folder.getParentDirectory(); }
    auto v = juce::JSON::parse(json);
    if (!v.isObject()) { say("no SCORE piece in " + sel.getFileName()); return false; }
    if (recording()) finishRecording();
    engine.stop();
    auto p = std::make_shared<Project>();
    p->folder = folder;
    auto old = project; if (old) { previous.push_back(old); if (previous.size() > 4) previous.erase(previous.begin()); } project = p; pool.clear();
    if (!p->fromVar(v, nullptr, [this](const juce::String& f) { return loadSource(f); })) { project = old; say("the piece could not be read"); return false; }
    p->name = folder.getFileNameWithoutExtension();
    audioFolder().createDirectory();
    undos.clear(); redos.clear(); selClip = 0; selClips.clear();
    selTrack = p->tracks.empty() ? 0 : p->tracks.front()->id;
    for (auto& t : p->tracks) if (t->isInstrument()) { engine.keysTarget.store(t->id); break; }
    stations.current = p->stations.empty() ? -1 : 0; stations.log.clear();
    engine.locate(0);
    rebuild(); ++structure; dirty = false;
    recent.removeString(folder.getFullPathName()); recent.insert(0, folder.getFullPathName()); while (recent.size() > 12) recent.remove(12);
    // anything missing?
    int missing = 0; for (auto& t : p->tracks) for (auto& c : t->clips) if (!c->midi && !c->src) ++missing;
    say(missing > 0 ? juce::String(missing) + " sound file(s) are missing in " + p->name : "\"" + p->name + U8("\" ✓"));
    if (live) saveSetup();
    return true;
}

bool Host::saveProject() {
    project->folder.createDirectory();
    auto json = project->folder.getChildFile("project.json");
    if (json.existsAsFile()) json.copyFileTo(project->folder.getChildFile("project.json.bak"));
    if (!json.replaceWithText(juce::JSON::toString(project->toVar()))) { say("could not save (is the disk full?)"); return false; }
    project->folder.getChildFile("autosave.json").deleteFile();
    dirty = false;
    recent.removeString(project->folder.getFullPathName()); recent.insert(0, project->folder.getFullPathName()); while (recent.size() > 12) recent.remove(12);
    say("saved \"" + project->name + U8("\" ✓"));
    if (live) saveSetup();
    return true;
}

bool Host::saveProjectAs(const juce::String& rawName) {
    auto name = juce::File::createLegalFileName(rawName.trim()); if (name.isEmpty()) return false;
    auto base = project->folder.getParentDirectory();
    auto dest = base.getChildFile(name + ".score");
    if (dest == project->folder) return saveProject();
    if (dest.exists()) { say("\"" + name + "\" already exists - choose another name"); return false; }
    auto src = project->folder;
    bool wasUntitled = !src.getChildFile("project.json").existsAsFile();
    if (!src.copyDirectoryTo(dest)) { dest.createDirectory(); dest.getChildFile("Audio").createDirectory(); }
    project->folder = dest; project->name = name;
    bool ok = saveProject();
    if (ok && wasUntitled) src.deleteRecursively();      // an untitled piece becomes this one
    return ok;
}

bool Host::exportProjectBundle(const juce::File& zip) {
    saveProject();
    juce::ZipFile::Builder b;
    for (auto& f : project->folder.findChildFiles(juce::File::findFiles, true)) {
        if (f.getFileName().endsWith(".bak") || f.getFileName() == "autosave.json") continue;
        b.addFile(f, 6, project->folder.getFileName() + "/" + f.getRelativePathFrom(project->folder).replaceCharacter('\\', '/'));
    }
    zip.deleteFile();
    juce::FileOutputStream os(zip);
    if (!os.openedOk()) return false;
    return b.writeToStream(os, nullptr);
}

void Host::autosave() {
    if (!dirty || !project->folder.isDirectory()) return;
    project->folder.getChildFile("autosave.json").replaceWithText(juce::JSON::toString(project->toVar()));
}

// ======================================================================== sounds
bool Host::canRead(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f)); return r != nullptr;
}

static std::shared_ptr<AudioSource> readSource(juce::AudioFormatManager& fm, const juce::File& f, double engineRate) {
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) return nullptr;
    auto s = std::make_shared<AudioSource>();
    s->name = f.getFileNameWithoutExtension(); s->fileRate = r->sampleRate; s->sr = engineRate;
    auto n = (int) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 60 * 30));
    juce::AudioBuffer<float> tmp((int) juce::jmax(1u, r->numChannels), juce::jmax(1, n));
    r->read(&tmp, 0, n, 0, true, true);
    if (tmp.getNumChannels() == 1) { tmp.setSize(2, n, true); tmp.copyFrom(1, 0, tmp, 0, 0, n); }
    if (std::abs(r->sampleRate - engineRate) < 1.0) { s->buf.setSize(2, n); s->buf.copyFrom(0, 0, tmp, 0, 0, n); s->buf.copyFrom(1, 0, tmp, 1, 0, n); }
    else {
        double ratio = r->sampleRate / engineRate;
        int m = (int) std::floor(n / ratio);
        s->buf.setSize(2, juce::jmax(1, m)); s->buf.clear();
        for (int c = 0; c < 2; ++c) { juce::WindowedSincInterpolator ip; ip.process(ratio, tmp.getReadPointer(c), s->buf.getWritePointer(c), m); }
    }
    int len = s->buf.getNumSamples();
    s->peaks.assign((size_t) (len / 64 + 1), 0.0f);
    for (int c = 0; c < 2; ++c) { auto* d = s->buf.getReadPointer(c); for (int i = 0; i < len; ++i) { auto& p = s->peaks[(size_t) (i >> 6)]; p = std::max(p, std::abs(d[i])); } }
    return s;
}

SourcePtr Host::peekSource(const juce::File& f) {
    auto key = f.getFullPathName();
    auto it = pool.find(key); if (it != pool.end() && it->second && std::abs(it->second->sr - engine.sr) < 1) return it->second;
    if (f.getSize() > 200ll * 1024 * 1024) return nullptr;
    auto s = readSource(formats, f, engine.sr); if (!s) return nullptr;
    s->file = f.getFullPathName(); pool[key] = s; return s;
}
void Host::preview(const juce::File& f) {
    if (previewFile == f && std::atomic_load(&previewSrc)) { stopPreview(); return; }
    auto s = peekSource(f); if (!s) { say("that file can't be read"); return; }
    previewPos.store(0); previewFile = f; std::atomic_store(&previewSrc, std::shared_ptr<const AudioSource>(s));
}
void Host::stopPreview() { std::atomic_store(&previewSrc, std::shared_ptr<const AudioSource>()); previewFile = juce::File(); }
float Host::previewProgress() const { auto s = std::atomic_load(&previewSrc); return s && s->length() > 0 ? (float) previewPos.load() / (float) s->length() : -1.0f; }

SourcePtr Host::loadSource(const juce::String& file) {
    if (file.isEmpty()) return nullptr;
    juce::File f = juce::File::isAbsolutePath(file) ? juce::File(file) : project->folder.getChildFile(file);
    auto key = f.getFullPathName();
    auto it = pool.find(key); if (it != pool.end() && it->second && std::abs(it->second->sr - engine.sr) < 1) return it->second;
    auto s = readSource(formats, f, engine.sr);
    if (!s) return nullptr;
    s->file = juce::File::isAbsolutePath(file) && f.isAChildOf(project->folder) ? f.getRelativePathFrom(project->folder) : file;
    pool[key] = s;
    return s;
}

SourcePtr Host::importAudio(const juce::File& f, juce::String* error) {
    if (f.isAChildOf(project->folder)) return loadSource(f.getRelativePathFrom(project->folder));
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
   #if JUCE_MAC || JUCE_IOS
    const char* kinds = "WAV, AIFF, FLAC, MP3, OGG, M4A";
   #else
    const char* kinds = "WAV, AIFF, FLAC, MP3, OGG";
   #endif
    if (r == nullptr) { if (error) *error = "can't read " + f.getFileName() + " (" + kinds + ")"; return nullptr; }
    audioFolder().createDirectory();
    auto dest = audioFolder().getChildFile(juce::File::createLegalFileName(f.getFileNameWithoutExtension()) + ".wav").getNonexistentSibling();
    {
        auto n = juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 60 * 30));
        std::unique_ptr<juce::FileOutputStream> os(dest.createOutputStream().release());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), r->sampleRate, juce::jmax(1u, juce::jmin(2u, r->numChannels)), 32, {}, 0));
        if (w == nullptr) { if (error) *error = "could not write into the project folder"; return nullptr; }
        os.release();
        w->writeFromAudioReader(*r, 0, n);
    }
    return loadSource(dest.getRelativePathFrom(project->folder));
}

SourcePtr Host::writeSource(const juce::String& name, const std::vector<float>& L, const std::vector<float>& R) {
    audioFolder().createDirectory();
    auto dest = audioFolder().getChildFile(juce::File::createLegalFileName(name) + ".wav").getNonexistentSibling();
    std::unique_ptr<juce::FileOutputStream> os(dest.createOutputStream().release());
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), engine.sr, 2, 32, {}, 0));
    if (w == nullptr) return nullptr;
    os.release();
    const float* ch[2] = { L.data(), R.data() };
    w->writeFromFloatArrays(ch, 2, (int) L.size());
    w.reset();
    return loadSource(dest.getRelativePathFrom(project->folder));
}

std::shared_ptr<SampleData> Host::toSample(SourcePtr s) {
    if (!s) return nullptr;
    auto d = std::make_shared<SampleData>(); d->sr = s->sr; d->path = s->file.toStdString(); d->name = s->name.toStdString();
    d->L.assign(s->buf.getReadPointer(0), s->buf.getReadPointer(0) + s->length());
    d->R.assign(s->buf.getReadPointer(1), s->buf.getReadPointer(1) + s->length());
    d->slices = findOnsets(*d, 32);
    return d;
}

juce::String Host::importClip(const juce::File& f, Uid trackId, double beat) {
    juce::String err;
    auto src = importAudio(f, &err);
    if (!src) return err;
    edit("import " + f.getFileName(), [&] {
        Track* t = project->track(trackId);
        if (t == nullptr || t->kind != TrackKind::Audio) {
            auto nt = project->newTrack(TrackKind::Audio, src->name);
            nt->inType = InputType::Mono;
            // under the track it was dropped on
            auto it = std::find_if(project->tracks.begin(), project->tracks.end(), [&](const TrackPtr& x) { return x->id == trackId; });
            project->tracks.insert(it == project->tracks.end() ? project->tracks.end() : it + 1, nt);
            t = nt.get();
        }
        auto c = project->newAudioClip(src, std::max(0.0, beat));
        t->clips.push_back(c);
        selClip = c->id; selClips = { c->id }; selTrack = t->id;
    });
    say("imported " + f.getFileName() + " (" + juce::String(src->seconds(), 1) + " s)");
    return {};
}

// ======================================================================== transport
void Host::play() { engine.play(); }
void Host::stop() {
    if (recording()) finishRecording();
    engine.stop();
    stations.stopTransportGuards();
    allNotesOff();
}
void Host::togglePlay() { if (engine.isPlaying()) stop(); else play(); }
void Host::locate(double beat) { engine.locate(std::max(0.0, beat)); }
void Host::toggleLoop() { project->loopOn = !project->loopOn; rebuild(); sendChangeMessage(); }
void Host::setLoopRange(double a, double b) { project->loopStart = std::max(0.0, std::min(a, b)); project->loopEnd = std::max(a, b); if (project->loopEnd - project->loopStart < 0.25) project->loopEnd = project->loopStart + 0.25; rebuild(); sendChangeMessage(); }
void Host::setTempo(double bpm) {
    bpm = juce::jlimit(20.0, 300.0, bpm);
    if (std::abs(bpm - project->bpm) < 1e-6) return;
    edit("tempo", [&] { double pos = engine.position() * project->bpm; project->bpm = bpm; project->syncAudioLengths(); (void) pos; });
}
void Host::tapTempo() {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (!taps.empty() && now - taps.back() > 2000) taps.clear();
    taps.push_back(now); if (taps.size() > 6) taps.erase(taps.begin());
    if (taps.size() >= 3) { double avg = (taps.back() - taps.front()) / (taps.size() - 1); setTempo(std::round(60000.0 / avg * 10) / 10); }
}

double Host::recordingSeconds() const { return recording() ? (juce::Time::getMillisecondCounterHiRes() - recStartedAt) / 1000.0 : 0.0; }

void Host::record() {
    if (recording()) { finishRecording(); return; }
    std::vector<Track*> armed; for (auto& t : project->tracks) if (t->arm) armed.push_back(t.get());
    if (armed.empty()) {
        if (auto* t = selectedTrack(); t && (t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument)) { t->arm = true; armed.push_back(t); rebuild(); }
        else { say("arm a track first (R on the track)"); return; }
    }
    audioFolder().createDirectory();
    std::vector<RecSlotPtr> slots;
    {
        std::lock_guard<std::mutex> g(writer.lock);
        writer.items.clear();
        for (auto* t : armed) {
            auto s = std::make_shared<RecSlot>(); s->track = t->id; s->midi = t->isInstrument() && t->inType != InputType::Track;
            if (!s->midi) {
                RecWriter::Item it; it.slot = s;
                it.file = audioFolder().getChildFile(juce::File::createLegalFileName(t->name) + " take.wav").getNonexistentSibling();
                std::unique_ptr<juce::FileOutputStream> os(it.file.createOutputStream().release());
                juce::WavAudioFormat wav;
                it.writer.reset(wav.createWriterFor(os.get(), engine.sr, 2, 32, {}, 0));
                if (it.writer) os.release();
                writer.items.push_back(std::move(it));
            }
            slots.push_back(s);
        }
    }
    recSlotsNow = slots;
    bool rolling = engine.isPlaying();
    double from = engine.position();
    double countIn = rolling ? 0.0 : project->countIn * project->beatsPerBar;
    engine.startRecording(slots, from, countIn);
    recStartedAt = juce::Time::getMillisecondCounterHiRes() + countIn * 60000.0 / project->bpm;
    say(countIn > 0 ? U8("count-in … then recording") : U8("recording …"));
    sendChangeMessage();
}

// cut a hole in a track's clips (a new recording replaces what was there)
static void carve(Project& p, Track& t, double a, double b) {
    std::vector<ClipPtr> out;
    for (auto& c : t.clips) {
        if (c->end() <= a + 1e-9 || c->start >= b - 1e-9) { out.push_back(c); continue; }
        double spb = 60.0 / p.bpm;
        if (c->start < a) {                                          // the part before stays
            auto first = std::make_shared<Clip>(*c); first->id = c->id; first->rt = c->rt;
            double keep = a - c->start;
            if (!c->midi) { first->srcLen = keep * spb; first->length = keep; first->fadeOut = std::min(first->fadeOut, first->srcLen * 0.5); }
            else { first->length = keep; }
            out.push_back(first);
        }
        if (c->end() > b) {                                          // the part after stays
            auto last = std::make_shared<Clip>(*c); last->id = newUid(); last->rt = std::make_shared<ClipRT>();
            double cut = b - c->start;
            if (!c->midi) { last->offset = c->offset + cut * spb; last->srcLen = c->srcLen - cut * spb; last->start = b; last->length = last->srcLen / spb; last->fadeIn = 0; }
            else { last->start = b; last->length = c->end() - b; std::vector<Note> ns; for (auto n : c->notes) { n.start -= cut; if (n.start >= 0) ns.push_back(n); } last->notes = ns; }
            last->fx.clear(); for (auto& e : c->fx) { auto x = makeEffect(e->type); for (size_t i = 0; i < e->params.size(); ++i) x->params[i]->set(e->params[i]->get()); last->fx.push_back(std::shared_ptr<Effect>(std::move(x))); }
            out.push_back(last);
        }
    }
    t.clips = out;
}

void Host::finishRecording() {
    engine.stopRecording();
    for (int i = 0; i < 40 && engine.recRolling.load(); ++i) juce::Thread::sleep(5);
    juce::Thread::sleep(20);
    std::vector<std::pair<RecSlotPtr, juce::File>> audio;
    {
        std::lock_guard<std::mutex> g(writer.lock);
        for (auto& it : writer.items) { writer.drain(it); it.writer.reset(); audio.push_back({ it.slot, it.file }); }
        writer.items.clear();
    }
    double start = engine.recStartBeat.load();
    double spb = 60.0 / project->bpm;
    int latency = 0;
    if (auto* d = devices.getCurrentAudioDevice()) latency = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
    int made = 0;
    edit("record", [&] {
        for (auto& a : audio) {
            auto* t = project->track(a.first->track); if (!t) continue;
            if (a.first->written.load() < 256) { a.second.deleteFile(); continue; }
            auto src = loadSource(a.second.getRelativePathFrom(project->folder)); if (!src) continue;
            auto c = project->newAudioClip(src, start);
            double lat = juce::jmin((double) latency / engine.sr, c->srcLen * 0.5);
            c->offset = lat; c->srcLen -= lat; c->length = c->srcLen / spb;
            c->name = t->name + " take";
            carve(*project, *t, c->start, c->end());
            t->clips.push_back(c); selClip = c->id; selClips = { c->id }; ++made;
        }
        for (auto& s : recSlotsNow) {
            if (!s->midi) continue;
            auto* t = project->track(s->track); if (!t) continue;
            std::vector<Note> notes; std::map<int, std::pair<double, float>> open;
            std::pair<double, NoteEv> e; double last = start;
            while (s->notes.pop(e)) {
                double b = e.first; last = std::max(last, b);
                if (e.second.type == NoteEv::On && e.second.b > 0) open[e.second.a] = { b, e.second.b };
                else if (e.second.type == NoteEv::Off || (e.second.type == NoteEv::On && e.second.b <= 0)) {
                    auto it = open.find(e.second.a); if (it != open.end()) { notes.push_back({ it->second.first, std::max(0.05, b - it->second.first), e.second.a, it->second.second }); open.erase(it); } }
            }
            double end = engine.position(); for (auto& o : open) notes.push_back({ o.second.first, std::max(0.05, end - o.second.first), o.first, o.second.second });
            if (notes.empty()) continue;
            double bar = project->beatsPerBar;
            double c0 = std::floor(start / bar) * bar, c1 = std::max(c0 + bar, std::ceil(std::max(last, end) / bar) * bar);
            auto c = project->newMidiClip(c0, c1 - c0); c->name = t->name + " take";
            for (auto& n : notes) { n.start -= c0; c->notes.push_back(n); }
            std::sort(c->notes.begin(), c->notes.end(), [](const Note& x, const Note& y) { return x.start < y.start; });
            carve(*project, *t, c->start, c->end());
            t->clips.push_back(c); selClip = c->id; selClips = { c->id }; ++made;
        }
    });
    recSlotsNow.clear();
    say(made > 0 ? "recorded " + juce::String(made) + (made == 1 ? " clip" : " clips") + U8(" ✓") : juce::String("nothing was recorded"));
    saveProject();
}

// ======================================================================== live notes
void Host::noteOn(int note, float vel) { keyboard.noteOn(1, note, vel); }
void Host::noteOff(int note) { keyboard.noteOff(1, note, 0.0f); }
void Host::allNotesOff() {
    keyboard.allNotesOff(1);
    for (auto& t : project->tracks) if (t->isInstrument()) engine.liveNote(t->id, { 0, NoteEv::AllOff, 0, 0 });
}

// ======================================================================== pedals / keys
juce::String Host::actionName(int a) {
    static const char* N[NumActions] = { "GO", "BACK", "PLAY / STOP", "RECORD", "STORE", "UNDO" };
    return a >= 0 && a < NumActions ? N[a] : "";
}
juce::String Host::pedalText(const Pedal& p) {
    if (p.type == 1) return "note " + juce::MidiMessage::getMidiNoteName(p.num, true, true, 4);
    if (p.type == 2) return "CC " + juce::String(p.num);
    return "-";
}
void Host::doAction(int a) {
    switch (a) {
        case ActGo: stations.go(); break;
        case ActBack: stations.back(); break;
        case ActPlay: togglePlay(); break;
        case ActRec: record(); break;
        case ActStore: stations.store(); break;
        case ActUndo: undo(); break;
        default: break;
    }
    if (onAction) onAction(a);
    sendChangeMessage();
}

void Host::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    int ch = m.getChannel();
    int type = 0, num = 0;
    if (m.isNoteOn()) { type = 1; num = m.getNoteNumber(); }
    else if (m.isController()) {
        int c = m.getControllerNumber(), v = m.getControllerValue(), was = ccLast[(ch - 1) & 15][c & 127];
        ccLast[(ch - 1) & 15][c & 127] = v;
        bool isPedal = false; for (auto& p : pedal) if (p.type == 2 && p.num == c) isPedal = true;
        if (learning.load() >= 0 && v >= 64 && was < 64) { type = 2; num = c; }
        else if (isPedal) { if (v >= 64 && was < 64) { type = 2; num = c; } else return; }
        else {
            Uid tgt = engine.keysTarget.load();
            if (c == 64) engine.liveNote(tgt, { 0, NoteEv::Sustain, 64, v / 127.0f });
            else engine.liveNote(tgt, { 0, NoteEv::CC, c, v / 127.0f });
            return;
        }
    }
    else if (m.isPitchWheel()) { engine.liveNote(engine.keysTarget.load(), { 0, NoteEv::Bend, 0, (m.getPitchWheelValue() - 8192) / 8192.0f }); return; }
    else if (m.isChannelPressure()) { engine.liveNote(engine.keysTarget.load(), { 0, NoteEv::CC, 128, m.getChannelPressureValue() / 127.0f }); return; }
    else if (m.isNoteOff()) { keyboard.noteOff(1, m.getNoteNumber(), 0.0f); return; }
    if (type == 0) return;
    int l = learning.load();
    if (l >= 0) {
        learning.store(-1);
        juce::MessageManager::callAsync([this, l, type, num, ch] { pedal[l] = { type, num, ch }; saveSetup(); sendChangeMessage(); });
        return;
    }
    for (int a = 0; a < NumActions; ++a)
        if (pedal[a].type == type && pedal[a].num == num) { juce::MessageManager::callAsync([this, a] { doAction(a); }); return; }
    if (type == 1) keyboard.noteOn(1, num, m.getFloatVelocity());
}

void Host::scanMidi() {
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (!devices.isMidiInputDeviceEnabled(d.identifier)) devices.setMidiInputDeviceEnabled(d.identifier, true);
}

// ======================================================================== the sound card
juce::String Host::openAudio(const juce::String& outName, const juce::String& inName, double rate, int buffer) {
    auto before = devices.getAudioDeviceSetup();
    auto s = before;
    s.outputDeviceName = outName; s.inputDeviceName = inName; s.sampleRate = rate; s.bufferSize = buffer;
    s.useDefaultInputChannels = s.useDefaultOutputChannels = true;
    juce::String msg;
    auto err = devices.setAudioDeviceSetup(s, true);
    if (err.isNotEmpty()) {
        s.sampleRate = 0; s.bufferSize = 0;
        auto e2 = devices.setAudioDeviceSetup(s, true);
        if (e2.isEmpty()) msg = "opened with the card's own sample rate / buffer";
        else if (inName.isNotEmpty() && inName != outName) {
            s.inputDeviceName = {};
            if (devices.setAudioDeviceSetup(s, true).isEmpty())
                msg = "\"" + inName + "\" does not work together with \"" + outName + "\". Playing without input - choose another input, or the same device for both.";
        }
        if (msg.isEmpty() && devices.getCurrentAudioDevice() == nullptr) {
            devices.setAudioDeviceSetup(before, true);
            msg = "could not open \"" + outName + "\": " + err;
        }
    }
    if (auto* d = devices.getCurrentAudioDevice()) {
        auto st = devices.getAudioDeviceSetup();
        juce::BigInteger ins, outs; ins.setRange(0, juce::jmin(32, d->getInputChannelNames().size()), true); outs.setRange(0, juce::jmin(32, d->getOutputChannelNames().size()), true);
        if (st.inputChannels != ins || st.outputChannels != outs) { st.inputChannels = ins; st.outputChannels = outs; st.useDefaultInputChannels = st.useDefaultOutputChannels = false; devices.setAudioDeviceSetup(st, true); }
    }
    saveSetup();
    audioMessage = msg;
    return msg;
}

int Host::numInputs() const { auto* d = devices.getCurrentAudioDevice(); return d ? d->getActiveInputChannels().countNumberOfSetBits() : 0; }
int Host::numOutputs() const { auto* d = devices.getCurrentAudioDevice(); return d ? d->getActiveOutputChannels().countNumberOfSetBits() : 2; }
juce::StringArray Host::inputNames() const { auto* d = devices.getCurrentAudioDevice(); return d ? d->getInputChannelNames() : juce::StringArray(); }
juce::StringArray Host::outputNames() const { auto* d = devices.getCurrentAudioDevice(); return d ? d->getOutputChannelNames() : juce::StringArray { "1", "2" }; }

void Host::audioDeviceAboutToStart(juce::AudioIODevice* d) {
    double r = d->getCurrentSampleRate();
    if (std::abs(r - lastRate) > 0.5) {
        lastRate = r;
        juce::MessageManager::callAsync([this, r] { changeRate(r); });
    }
   #if JUCE_IOS
    juce::MessageManager::callAsync([d] { d->setAudioPreprocessingEnabled(false); scorePreferBestMicrophone(); });
   #endif
}

void Host::changeRate(double r) {
    // a new sample rate: everything is prepared again, the sounds are converted again
    engine.offline.store(true); juce::Thread::sleep(30);
    engine.prepare(r);
    pool.clear();
    auto v = project->toVar();
    project->fromVar(v, project.get(), [this](const juce::String& f) { return loadSource(f); });
    for (auto* t : project->allTracks()) { for (auto& e : t->fx) e->prepare(r, MAXB); if (t->inst) { t->inst->prepare(r, MAXB); for (auto* e : t->inst->internalEffects()) e->prepare(r, MAXB); }
        for (auto& c : t->clips) for (auto& e : c->fx) e->prepare(r, MAXB); }
    rebuild(); ++structure;
    engine.offline.store(false);
    sendChangeMessage();
}

void Host::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&) {
    engine.process(in, numIn, out, numOut, n);
    if (auto ps = std::atomic_load(&previewSrc)) {
        int64_t p = previewPos.load(); int of = engine.outFirst.load();
        int m = (int) juce::jmin<int64_t>(n, ps->length() - p);
        for (int c = 0; c < 2; ++c) if (of + c < numOut && out[of + c] && m > 0) { auto* src = ps->buf.getReadPointer(c) + p; for (int i = 0; i < m; ++i) out[of + c][i] += 0.8f * src[i]; }
        previewPos.store(p + juce::jmax(0, m));
        if (m <= 0) std::atomic_store(&previewSrc, std::shared_ptr<const AudioSource>());
    }
    int tt = testTone.load();
    if (tt > 0) {
        static double ph = 0;
        for (int i = 0; i < n; ++i) {
            float env = juce::jmin(1.0f, juce::jmin((float) (tt - i), (float) (48000 - (tt - i))) / 2000.0f);
            float v = 0.15f * env * (float) std::sin(ph); ph += 2 * juce::MathConstants<double>::pi * 440.0 / juce::jmax(8000.0, lastRate);
            int of = engine.outFirst.load();
            for (int c : { of, of + 1 }) if (c >= 0 && c < numOut && out[c]) out[c][i] += v;
        }
        testTone.store(juce::jmax(0, tt - n));
    }
}

void Host::saveSetup() {
    if (!live) return;
    auto f = setupFile(); f.getParentDirectory().createDirectory();
    auto* o = new juce::DynamicObject();
    o->setProperty("theme", theme); o->setProperty("scale", uiScale);
    o->setProperty("out", engine.outFirst.load()); o->setProperty("click", engine.clickFirst.load());
    juce::Array<juce::var> pd; for (auto& p : pedal) { pd.add(p.type); pd.add(p.num); pd.add(p.ch); } o->setProperty("pedals", pd);
    juce::Array<juce::var> rc; for (auto& r : recent) rc.add(r); o->setProperty("recent", rc);
    o->setProperty("last", project->folder.getChildFile("project.json").existsAsFile() ? project->folder.getFullPathName() : juce::String());
    if (auto xml = devices.createStateXml()) o->setProperty("audio", xml->toString());
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

void Host::loadSetup(bool openDevice) {
    auto v = juce::JSON::parse(setupFile());
    std::unique_ptr<juce::XmlElement> xml;
    if (v.isObject() && live) {
        theme = (int) v["theme"]; uiScale = (float) (double) v.getProperty("scale", 1.0);
        engine.outFirst.store((int) v["out"]); engine.clickFirst.store((int) v.getProperty("click", -1));
        if (auto* pd = v["pedals"].getArray()) for (int a = 0; a < NumActions && a * 3 + 2 < pd->size(); ++a) pedal[a] = { (int) (*pd)[a * 3], (int) (*pd)[a * 3 + 1], (int) (*pd)[a * 3 + 2] };
        if (auto* rc = v["recent"].getArray()) for (auto& r : *rc) if (juce::File(r.toString()).isDirectory()) recent.add(r.toString());
        if (v["audio"].isString()) xml = juce::parseXML(v["audio"].toString());
        auto last = v["last"].toString();
        if (last.isNotEmpty() && juce::File(last).getChildFile("project.json").existsAsFile()) {
            auto untitled = project->folder; openProject(juce::File(last));
            if (untitled != project->folder) untitled.deleteRecursively();
        }
    }
    if (openDevice) {
        auto err = devices.initialise(32, 32, xml.get(), true);
        if (err.isNotEmpty() || devices.getCurrentAudioDevice() == nullptr) devices.initialise(32, 32, nullptr, true);
        if (auto* d = devices.getCurrentAudioDevice()) {
            auto st = devices.getAudioDeviceSetup();
            juce::BigInteger ins, outs; ins.setRange(0, juce::jmin(32, d->getInputChannelNames().size()), true); outs.setRange(0, juce::jmin(32, d->getOutputChannelNames().size()), true);
            st.inputChannels = ins; st.outputChannels = outs; st.useDefaultInputChannels = st.useDefaultOutputChannels = false; devices.setAudioDeviceSetup(st, true);
        }
        devices.addAudioCallback(this);
        devices.addMidiInputDeviceCallback({}, this);
        scanMidi();
    }
}

// ======================================================================== the timer: autosave, knob undo, MIDI devices
void Host::timerCallback() {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (pendingKnob.isNotEmpty() && now - knobAt > 700) {
        undos.push_back({ "knob", pendingKnob }); redos.clear(); pendingKnob = {};
        if (stations.autoStore && !stations.gliding()) stations.storeQuiet();
    }
    if (now - lastAutosave > 120000) { lastAutosave = now; autosave(); }
    static int midiScan = 0;
    if (live && ++midiScan % 60 == 0) scanMidi();
}

// ======================================================================== export
void Host::exportAudio(const ExportOptions& o, const juce::File& file, std::function<void(bool, juce::String)> done) {
    if (exportThread && exportThread->isThreadRunning()) { done(false, "an export is already running"); return; }
    if (recording()) finishRecording();
    engine.stop();
    double a = 0, b = std::max((double) project->beatsPerBar, project->length());
    if (o.range == 1) { a = project->loopStart; b = project->loopEnd; }
    if (o.range == 3) { a = o.from; b = std::max(o.from + 0.25, o.to); }
    if (o.range == 2 && o.stationIdx >= 0 && o.stationIdx < (int) project->stations.size()) {
        a = project->stations[(size_t) o.stationIdx].beat;
        b = o.stationIdx + 1 < (int) project->stations.size() ? project->stations[(size_t) o.stationIdx + 1].beat : project->length();
        if (b <= a) b = a + project->beatsPerBar;
    }
    struct Job : juce::Thread {
        Host& h; ExportOptions o; juce::File file; double a, b; std::function<void(bool, juce::String)> done;
        Job(Host& hh, ExportOptions oo, juce::File f, double aa, double bb, std::function<void(bool, juce::String)> d)
            : juce::Thread("SCORE export"), h(hh), o(oo), file(f), a(aa), b(bb), done(std::move(d)) {}
        void run() override {
            auto& e = h.engine;
            e.offline.store(true);
            int c0 = e.callbacks.load(); for (int i = 0; i < 50 && e.callbacks.load() < c0 + 2; ++i) juce::Thread::sleep(4);
            double spb = 60.0 / h.project->bpm * e.sr;
            juce::int64 total = (juce::int64) ((b - a) * spb + o.tail * e.sr);
            std::vector<std::pair<Uid, juce::File>> passes;
            if (o.stems) { for (auto& t : h.project->tracks) passes.push_back({ t->id, file.getSiblingFile(file.getFileNameWithoutExtension() + " - " + juce::File::createLegalFileName(t->name) + ".wav") }); }
            else passes.push_back({ h.exportStems, file });
            bool ok = true; juce::String msg;
            int pi = 0;
            for (auto& pass : passes) {
                float gain = 1.0f;
                for (int round = o.normalize ? 0 : 1; round < 2; ++round) {
                    e.resetAll(); e.locate(a); e.play();
                    std::unique_ptr<juce::AudioFormatWriter> w;
                    if (round == 1) {
                        pass.second.deleteFile();
                        std::unique_ptr<juce::FileOutputStream> os(pass.second.createOutputStream().release());
                        juce::WavAudioFormat wav;
                        w.reset(wav.createWriterFor(os.get(), e.sr, 2, (unsigned) o.bits, {}, 0));
                        if (!w) { ok = false; msg = "could not write " + pass.second.getFileName(); break; }
                        os.release();
                    }
                    std::vector<float> L(4096), R(4096); float peak = 0;
                    for (juce::int64 done_ = 0; done_ < total && !threadShouldExit(); done_ += 4096) {
                        int n = (int) std::min<juce::int64>(4096, total - done_);
                        e.renderBlock(L.data(), R.data(), n, pass.first);
                        if (round == 0) for (int i = 0; i < n; ++i) peak = std::max(peak, std::max(std::abs(L[(size_t) i]), std::abs(R[(size_t) i])));
                        else { if (gain != 1.0f) for (int i = 0; i < n; ++i) { L[(size_t) i] *= gain; R[(size_t) i] *= gain; }
                               const float* ch[2] = { L.data(), R.data() }; w->writeFromFloatArrays(ch, 2, n); }
                        h.exportProgress.store((float) ((pi + (round + (double) done_ / total) / (o.normalize ? 2.0 : 1.0)) / passes.size()));
                    }
                    e.stop();
                    if (round == 0) gain = peak > 1e-6f ? juce::Decibels::decibelsToGain(-0.3f) / peak : 1.0f;
                }
                ++pi;
                if (!ok) break;
            }
            e.resetAll(); e.locate(a);
            e.offline.store(false);
            h.exportProgress.store(-1.0f);
            if (ok) msg = o.stems ? juce::String((int) passes.size()) + " stems exported" : file.getFileName() + " exported";
            auto d = done;
            juce::MessageManager::callAsync([d, ok, msg] { d(ok, msg); });
        }
    };
    exportProgress.store(0.0f);
    exportThread = std::make_unique<Job>(*this, o, file, a, b, std::move(done));
    exportThread->startThread();
}

// ======================================================================== bounce: a track's sound becomes audio
void Host::bounceTrack(Uid id) {
    Track* t = project->track(id); if (!t) return;
    double a = 1e9, b = 0; for (auto& c : t->clips) { a = std::min(a, c->start); b = std::max(b, c->end()); }
    if (b <= a) { say("the track has nothing to bounce"); return; }
    auto file = audioFolder().getChildFile(juce::File::createLegalFileName(t->name) + " bounce.wav").getNonexistentSibling();
    ExportOptions o; o.range = 3; o.from = a; o.to = b; o.bits = 32; o.tail = 3.0;
    exportStems = id;                                        // only this track is rendered
    exportAudio(o, file, [this, id, a, file](bool ok, juce::String msg) {
        exportStems = 0;
        if (!ok) { say(msg); return; }
        auto src = loadSource(file.getRelativePathFrom(project->folder)); if (!src) return;
        Uid made = 0;
        edit("bounce", [&] {
            Track* t2 = project->track(id); if (!t2) return;
            auto nt = project->newTrack(TrackKind::Audio, t2->name + " (audio)"); nt->colour = t2->colour;
            auto c = project->newAudioClip(src, a); c->name = t2->name + " bounce"; nt->clips.push_back(c);
            auto it = std::find_if(project->tracks.begin(), project->tracks.end(), [&](const TrackPtr& x) { return x->id == id; });
            project->tracks.insert(it == project->tracks.end() ? project->tracks.end() : it + 1, nt);
            t2->mix->find("mute")->set(1);                    // so it is not heard twice
            made = nt->id;
        });
        selectTrack(made);
        say("bounced to a new audio track (the original is muted)");
    });
}

// ======================================================================== clip tools
static void clipRegion(const Clip& c, std::vector<float>& L, std::vector<float>& R) {
    L.clear(); R.clear(); if (!c.src) return;
    int sr = (int) c.src->sr; int a = (int) std::llround(c.offset * sr), n = (int) std::llround(c.srcLen * sr);
    a = juce::jlimit(0, c.src->length(), a); n = juce::jlimit(0, c.src->length() - a, n);
    L.assign(c.src->buf.getReadPointer(0) + a, c.src->buf.getReadPointer(0) + a + n);
    R.assign(c.src->buf.getReadPointer(1) + a, c.src->buf.getReadPointer(1) + a + n);
    if (c.reverse) { std::reverse(L.begin(), L.end()); std::reverse(R.begin(), R.end()); }
}

static std::shared_ptr<Effect> cloneEffect(const Effect& e) {
    auto x = std::shared_ptr<Effect>(makeEffect(e.type));
    for (size_t i = 0; i < e.params.size(); ++i) x->params[i]->set(e.params[i]->get());
    x->enabled.store(e.enabled.load());
    return x;
}

juce::String Host::processClip(Uid id, const juce::String& op, double amount) {
    Track* t = nullptr; Clip* c = project->clip(id, &t);
    if (!c) return "select a clip first";
    if (op == "reverse") { edit("reverse", [&] { c->reverse = !c->reverse; }); return {}; }
    if (c->midi) {
        if (op == "quantize") { edit("quantize", [&] { double q = amount > 0 ? amount : 0.25; for (auto& n : c->notes) n.start = std::round(n.start / q) * q; }); return {}; }
        if (op == "transpose") { edit("transpose", [&] { for (auto& n : c->notes) n.pitch = juce::jlimit(0, 127, n.pitch + (int) amount); }); return {}; }
        if (op == "legato") { edit("legato", [&] { std::sort(c->notes.begin(), c->notes.end(), [](auto& a, auto& b) { return a.start < b.start; });
            for (size_t i = 0; i + 1 < c->notes.size(); ++i) { double nx = c->notes[i + 1].start; if (nx > c->notes[i].start + 1e-6) c->notes[i].len = nx - c->notes[i].start; } }); return {}; }
        if (op == "velocity") { edit("velocity", [&] { for (auto& n : c->notes) n.vel = (float) juce::jlimit(0.05, 1.0, n.vel * amount); }); return {}; }
        return "that works on audio clips";
    }
    if (!c->src) return "the sound file of this clip is missing";
    if (op == "normalize") {
        std::vector<float> L, R; clipRegion(*c, L, R);
        float pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max(pk, std::max(std::abs(L[i]), std::abs(R[i])));
        if (pk < 1e-6f) return "the clip is silent";
        edit("normalize", [&] { c->gainDb = -0.3f - gainToDb(pk); });
        return {};
    }
    if (op == "revert") {
        if (c->original.isEmpty()) return "this clip has not been processed";
        auto s = loadSource(c->original); if (!s) return "the original file is missing";
        edit("revert to original", [&] { c->file = c->original; c->src = s; c->offset = 0; c->srcLen = s->seconds(); c->length = c->srcLen * project->bpm / 60.0; c->original = {}; });
        return {};
    }
    if (op == "split") { splitClip(id, engine.position()); return {}; }
    std::vector<float> L, R; clipRegion(*c, L, R);
    if (L.size() < 64) return "the clip is too short";
    std::vector<float> oL, oR; juce::String label;
    double sr = engine.sr;
    if (op == "pitch") { pitchShift(L, R, amount, sr, oL, oR); label = (amount >= 0 ? "+" : "") + juce::String((int) amount) + " st"; }
    else if (op == "stretch") { wsolaStretch(L, R, amount, sr, oL, oR); label = "x" + juce::String(amount, 2); }
    else if (op == "speed") { resampleSpeed(L, amount, oL); resampleSpeed(R, amount, oR); label = "speed x" + juce::String(amount, 2); }
    else if (op == "fitbars") {
        double bars = std::max(1.0, std::round(c->length / project->beatsPerBar));
        double want = bars * project->beatsPerBar * 60.0 / project->bpm;
        wsolaStretch(L, R, want / c->srcLen, sr, oL, oR); label = juce::String((int) bars) + " bars";
    }
    else if (op == "bake" || op == "baketrack") {
        std::vector<std::shared_ptr<Effect>> chain;
        if (op == "bake") for (auto& e : c->fx) chain.push_back(cloneEffect(*e));
        else if (t) for (auto& e : t->fx) chain.push_back(cloneEffect(*e));
        if (chain.empty()) return op == "bake" ? "this clip has no effects yet" : "this track has no effects";
        double tail = 0; for (auto& e : chain) { e->prepare(sr, 512); tail = std::max(tail, std::min(12.0, e->tailSeconds())); }
        int n = (int) L.size(), total = n + (int) (tail * sr);
        oL.assign((size_t) total, 0.0f); oR.assign((size_t) total, 0.0f);
        std::copy(L.begin(), L.end(), oL.begin()); std::copy(R.begin(), R.end(), oR.begin());
        TimeInfo ti; ti.sr = sr; ti.bpm = project->bpm; ti.playing = true; ti.beat = c->start;
        for (int off = 0; off < total; off += 512) { int m = std::min(512, total - off);
            for (auto& e : chain) e->run(oL.data() + off, oR.data() + off, m, ti); ti.beat += m / sr * project->bpm / 60.0; }
        // trim a silent tail
        int last = total - 1; while (last > n && std::abs(oL[(size_t) last]) < 1e-4f && std::abs(oR[(size_t) last]) < 1e-4f) --last;
        oL.resize((size_t) last + 1); oR.resize((size_t) last + 1);
        label = "with effects";
    }
    else if (op == "silence") {
        // strip silence: keep the parts louder than the threshold (amount dB), as separate clips
        float th = dbToGain((float) (amount != 0 ? amount : -45.0)); int hop = 256, minGap = (int) (0.15 * sr), pad = (int) (0.01 * sr);
        std::vector<std::pair<int, int>> parts; int a = -1, quiet = 0;
        for (int i = 0; i < (int) L.size(); i += hop) {
            float pk = 0; for (int k = i; k < std::min((int) L.size(), i + hop); ++k) pk = std::max(pk, std::max(std::abs(L[(size_t) k]), std::abs(R[(size_t) k])));
            if (pk > th) { if (a < 0) a = i; quiet = 0; } else if (a >= 0 && (quiet += hop) > minGap) { parts.push_back({ std::max(0, a - pad), std::min((int) L.size(), i - quiet + hop + pad) }); a = -1; quiet = 0; }
        }
        if (a >= 0) parts.push_back({ std::max(0, a - pad), (int) L.size() });
        if (parts.empty()) return "nothing louder than the threshold";
        edit("strip silence", [&] {
            double spb = 60.0 / project->bpm;
            auto base = *c;
            t->clips.erase(std::remove_if(t->clips.begin(), t->clips.end(), [&](const ClipPtr& x) { return x->id == id; }), t->clips.end());
            for (auto& pr : parts) {
                auto nc = std::make_shared<Clip>(base); nc->id = newUid(); nc->rt = std::make_shared<ClipRT>(); nc->fx.clear();
                for (auto& e : base.fx) nc->fx.push_back(cloneEffect(*e));
                nc->offset = base.offset + pr.first / sr; nc->srcLen = (pr.second - pr.first) / sr; nc->start = base.start + pr.first / sr / spb; nc->length = nc->srcLen / spb;
                nc->fadeIn = std::min(0.01, nc->srcLen * 0.2); nc->fadeOut = std::min(0.03, nc->srcLen * 0.2);
                t->clips.push_back(nc);
            }
        });
        return {};
    }
    else return "unknown tool " + op;
    auto s = writeSource(c->name + " " + label, oL, oR);
    if (!s) return "could not write the new sound";
    edit(op == "bake" || op == "baketrack" ? "apply effects" : op, [&] {
        if (c->original.isEmpty()) c->original = c->file;
        c->file = s->file; c->src = s; c->offset = 0; c->srcLen = s->seconds(); c->reverse = false;
        c->length = c->srcLen * project->bpm / 60.0;
        if (op == "bake") c->fx.clear();
        if (op == "baketrack" && t) for (auto& e : t->fx) e->enabled.store(false);     // so it is not heard twice
    });
    return {};
}

void Host::splitClip(Uid id, double beat) {
    Track* t = nullptr; Clip* c = project->clip(id, &t);
    if (!c || beat <= c->start + 1e-6 || beat >= c->end() - 1e-6) return;
    edit("split", [&] {
        auto b = std::make_shared<Clip>(*c); b->id = newUid(); b->rt = std::make_shared<ClipRT>(); b->fx.clear();
        for (auto& e : c->fx) b->fx.push_back(cloneEffect(*e));
        double cut = beat - c->start, spb = 60.0 / project->bpm;
        if (!c->midi) {
            b->offset = c->offset + (c->reverse ? 0.0 : cut * spb); b->srcLen = c->srcLen - cut * spb; b->start = beat; b->length = b->srcLen / spb; b->fadeIn = 0;
            if (c->reverse) c->offset = c->offset + b->srcLen;
            c->srcLen = cut * spb; c->length = cut; c->fadeOut = 0;
        } else {
            std::vector<Note> na, nb;
            for (auto& n : c->notes) { if (n.start < cut) { auto m = n; m.len = std::min(m.len, cut - m.start); na.push_back(m); } else { auto m = n; m.start -= cut; nb.push_back(m); } }
            c->notes = na; c->length = cut; b->notes = nb; b->start = beat; b->length = b->length - cut;
        }
        t->clips.push_back(b);
    });
}

void Host::duplicateClips() {
    if (selClips.empty()) return;
    std::set<Uid> made;
    edit("duplicate", [&] {
        for (auto id : selClips) {
            Track* t = nullptr; Clip* c = project->clip(id, &t); if (!c || !t) continue;
            auto d = std::make_shared<Clip>(*c); d->id = newUid(); d->rt = std::make_shared<ClipRT>(); d->fx.clear();
            for (auto& e : c->fx) d->fx.push_back(cloneEffect(*e));
            d->start = c->end();
            t->clips.push_back(d); made.insert(d->id);
        }
    });
    selClips = made; selClip = made.empty() ? 0 : *made.begin(); sendChangeMessage();
}

void Host::deleteSelection() {
    if (selClips.empty()) return;
    edit("delete", [&] {
        for (auto& t : project->tracks) t->clips.erase(std::remove_if(t->clips.begin(), t->clips.end(), [&](const ClipPtr& c) { return selClips.count(c->id) > 0; }), t->clips.end());
    });
    selClips.clear(); selClip = 0; sendChangeMessage();
}

Uid Host::clipToSampler(Uid id) {
    Track* t = nullptr; Clip* c = project->clip(id, &t);
    if (!c || c->midi || !c->src) return 0;
    std::vector<float> L, R; clipRegion(*c, L, R);
    float g = dbToGain(c->gainDb); for (auto& x : L) x *= g; for (auto& x : R) x *= g;
    auto s = writeSource(c->name + " (sampler)", L, R);
    Uid made = 0;
    edit("sampler from clip", [&] {
        auto nt = project->newTrack(TrackKind::Instrument, c->name, "smp");
        if (auto* sm = dynamic_cast<Sampler*>(nt->inst.get())) sm->setSample(toSample(s));
        auto it = std::find_if(project->tracks.begin(), project->tracks.end(), [&](const TrackPtr& x) { return x.get() == t; });
        project->tracks.insert(it == project->tracks.end() ? project->tracks.end() : it + 1, nt);
        made = nt->id;
    });
    selectTrack(made);
    return made;
}

void Host::clipToWavetable(Uid id, Uid trackId, int osc) {
    Track* t = nullptr; Clip* c = project->clip(id, &t);
    Track* dst = project->track(trackId);
    if (!c || c->midi || !c->src || !dst || !dst->inst) return;
    auto* n = dynamic_cast<Nova*>(dst->inst.get()); if (!n) return;
    std::vector<float> L, R; clipRegion(*c, L, R);
    for (size_t i = 0; i < L.size(); ++i) L[i] = 0.5f * (L[i] + R[i]);
    auto s = writeSource(c->name + " (table)", L, L);
    edit("wavetable from clip", [&] {
        n->userTable[osc] = wavetableFromAudio(L.data(), (int) L.size(), engine.sr, c->name.toStdString());
        n->userTablePath[osc] = s ? s->file.toStdString() : std::string();
        n->find(std::string("o") + std::to_string(osc + 1) + "tab")->set((float) factoryTables().size());
        n->find(std::string("o") + std::to_string(osc + 1) + "on")->set(1);
    }, false);
}

// ======================================================================== the station player
static double curveT(double t, int c) {
    t = juce::jlimit(0.0, 1.0, t);
    switch (c) { case 0: return t; case 2: return t * t * t; case 3: return 1 - std::pow(1 - t, 3.0); default: return t * t * (3 - 2 * t); }
}

void StationPlayer::arrive(int index, bool glide, bool viaTimeline) {
    auto& p = *host.project;
    if (index < 0 || index >= (int) p.stations.size()) return;
    current = index; lastSeen = index;
    auto& s = p.stations[(size_t) index];
    arrived = juce::Time::getMillisecondCounterHiRes();
    if (started > 0) {
        double sec = pieceSeconds();
        log.add(juce::String(index + 1).paddedLeft(' ', 2) + "   " + (s.name.isEmpty() ? juce::String("-") : s.name) + "   "
                + juce::String((int) sec / 60) + ":" + juce::String(std::fmod(sec, 60.0), 1).paddedLeft('0', 4));
    }
    if (s.hasSnap) {
        auto m = p.paramMap();
        from.clear(); to.clear(); fxOn.clear();
        for (auto& kv : s.snap) {
            if (kv.first.find("/@") != std::string::npos) continue;
            auto it = m.find(kv.first); if (it == m.end()) continue;
            from[kv.first] = it->second->get(); to[kv.first] = kv.second;
        }
        // effects switched on / off in this station
        for (auto* t : p.allTracks()) {
            auto look = [&](Effect& e) { auto k = std::to_string(e.uid) + "/@on"; auto it = s.snap.find(k); if (it != s.snap.end()) fxOn[e.uid] = { e.enabled.load(), it->second > 0.5f }; };
            for (auto& e : t->fx) look(*e); for (auto& c : t->clips) for (auto& e : c->fx) look(*e);
            if (t->inst) for (auto* e : t->inst->internalEffects()) look(*e);
        }
        glideDur = glide ? s.glide : 0.0; glideCurve = s.curve; glideStart = juce::Time::getMillisecondCounterHiRes(); glideT = 0;
        for (auto* t : p.allTracks()) { auto on = [&](Effect& e) { auto it = fxOn.find(e.uid); if (it != fxOn.end() && it->second.second) e.enabled.store(true); };
            for (auto& e : t->fx) on(*e); for (auto& c : t->clips) for (auto& e : c->fx) on(*e); if (t->inst) for (auto* e : t->inst->internalEffects()) on(*e); }
        if (glideDur <= 0.001) tick();
    }
    // the end of this station's region
    stopTransportGuards();
    if (perform && !viaTimeline) {
        double end = index + 1 < (int) p.stations.size() ? p.stations[(size_t) index + 1].beat : std::max(s.beat + p.beatsPerBar, p.length());
        if (s.atEnd == 1) { ownLoop = true; host.engine.setLoop(true, s.beat, end); }
        else if (s.atEnd == 2) host.engine.stopAt(end);
        autoGoAt = s.autoGo > 0 ? arrived + s.autoGo * 1000.0 : 0;
    } else autoGoAt = 0;
    if (onChange) onChange();
    host.sendChangeMessage();
}

void StationPlayer::stopTransportGuards() {
    host.engine.stopAt(-1);
    if (ownLoop) { ownLoop = false; host.engine.setLoop(host.project->loopOn, host.project->loopStart, host.project->loopEnd); }
}

void StationPlayer::goTo(int index, bool) {
    auto& p = *host.project;
    if (index < 0 || index >= (int) p.stations.size()) return;
    if (!perform) { select(index); return; }
    auto& s = p.stations[(size_t) index];
    auto& e = host.engine;
    pendingJump = false;
    if (!e.isPlaying()) { e.locate(s.beat); e.play(); }
    else if (s.jump == 0) e.locate(s.beat);
    else {
        double pos = e.position(), unit = s.jump == 1 ? 1.0 : p.beatsPerBar;
        double at = (std::floor(pos / unit + 1e-6) + 1) * unit;
        e.jumpAt(at, s.beat);
        pendingJump = true;
    }
    if (!isTimerRunning()) startTimerHz(60);
    arrive(index, true, false);
}

void StationPlayer::go() { goTo(current + 1); }
void StationPlayer::back() { goTo(std::max(0, current - 1)); }

void StationPlayer::select(int index) {
    auto& p = *host.project;
    if (index < 0 || index >= (int) p.stations.size()) return;
    if (!isTimerRunning()) startTimerHz(60);
    host.engine.locate(p.stations[(size_t) index].beat);
    arrive(index, false, false);
}

void StationPlayer::store() {
    auto& p = *host.project;
    if (current < 0 || current >= (int) p.stations.size()) return;
    host.edit("store station", [&] {
        auto& s = p.stations[(size_t) current];
        s.snap = p.captureKnobs();
        for (auto* t : p.allTracks()) {
            for (auto& e : t->fx) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
            for (auto& c : t->clips) for (auto& e : c->fx) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
            if (t->inst) for (auto* e : t->inst->internalEffects()) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
        }
        s.hasSnap = true;
    }, false);
    host.say("stored in station " + juce::String(current + 1));
}

void StationPlayer::storeQuiet() {
    auto& p = *host.project;
    if (current < 0 || current >= (int) p.stations.size()) return;
    auto& s = p.stations[(size_t) current];
    s.snap = p.captureKnobs();
    for (auto* t : p.allTracks()) {
        for (auto& e : t->fx) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
        for (auto& c : t->clips) for (auto& e : c->fx) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
        if (t->inst) for (auto* e : t->inst->internalEffects()) s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f;
    }
    s.hasSnap = true;
    if (onChange) onChange();
}

void StationPlayer::startPerformance() {
    perform = true; log.clear(); started = juce::Time::getMillisecondCounterHiRes();
    host.engine.stop();
    if (!isTimerRunning()) startTimerHz(60);
    current = -1;
    goTo(0);
}
void StationPlayer::stopPerformance() { started = 0; stopTransportGuards(); }

void StationPlayer::tick() {
    auto& p = *host.project;
    // the glide
    if (glideT < 1.0) {
        double el = (juce::Time::getMillisecondCounterHiRes() - glideStart) / 1000.0;
        glideT = glideDur > 0.001 ? std::min(1.0, el / glideDur) : 1.0;
        double k = curveT(glideT, glideCurve);
        auto m = p.paramMap();
        for (auto& kv : to) {
            auto it = m.find(kv.first); if (it == m.end()) continue;
            Param* q = it->second; float a = from[kv.first], b = kv.second;
            if (q->isDiscrete()) q->set(k >= 0.5 ? b : a);
            else if (q->log && q->min > 0 && a > 0 && b > 0) q->set((float) (a * std::pow(b / a, k)));
            else q->set((float) (a + (b - a) * k));
        }
        if (glideT >= 1.0) {
            for (auto* t : p.allTracks()) { auto off = [&](Effect& e) { auto it = fxOn.find(e.uid); if (it != fxOn.end() && !it->second.second) e.enabled.store(false); };
                for (auto& e : t->fx) off(*e); for (auto& c : t->clips) for (auto& e : c->fx) off(*e); if (t->inst) for (auto* e : t->inst->internalEffects()) off(*e); }
        }
    }
    // auto-go
    if (autoGoAt > 0 && juce::Time::getMillisecondCounterHiRes() >= autoGoAt) { autoGoAt = 0; go(); return; }
    // the arranger plays into the next station: its sound arrives (with its glide)
    if (follow && host.engine.isPlaying() && !p.stations.empty()) {
        double pos = host.engine.position(); int idx = -1;
        for (size_t i = 0; i < p.stations.size(); ++i) if (p.stations[i].beat <= pos + 1e-6) idx = (int) i;
        if (pendingJump) { if (idx == current) { pendingJump = false; lastSeen = idx; } }      // GO waits for the next beat / bar
        else if (idx >= 0 && idx != lastSeen) { if (idx != current) arrive(idx, true, true); else lastSeen = idx; }
    }
}

} // namespace score
