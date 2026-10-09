#include "Instruments.h"

namespace arco {

// ------------------------------------------------------------------ the background jobs (they may outlive the window, never the rack)
struct InstrumentRack::Download : public juce::URL::DownloadTask::Listener {
    InstrumentRack& rack; InstrumentInfo info; juce::File zip, dest;
    std::unique_ptr<juce::URL::DownloadTask> task;
    std::atomic<int> state { 0 };                   // 0 downloading, 1 unzipping, 2 done, 3 failed
    juce::String error; juce::File sfz;
    std::unique_ptr<juce::Thread> unzipper;
    Download(InstrumentRack& r, const InstrumentInfo& i) : rack(r), info(i) {}
    ~Download() override { task.reset(); if (unzipper) unzipper->stopThread(60000); }
    void progress(juce::URL::DownloadTask*, juce::int64 done, juce::int64 total) override {
        double t = total > 0 ? (double) total : info.sizeMB * 1.0e6 * 0.9;
        rack.downloadProgress.store((float) juce::jlimit(0.0, 0.99, done / t));
    }
    void finished(juce::URL::DownloadTask* t, bool ok) override {
        if (!ok || t->hadError() || !zip.existsAsFile() || zip.getSize() < 1000) { error = "the download did not work (check the internet connection)"; state = 3; return; }
        state = 1;
        struct Unzip : juce::Thread { Download& d; explicit Unzip(Download& x) : juce::Thread("ARCO unzip"), d(x) {}
            void run() override {
                juce::ZipFile z(d.zip);
                auto tmpDir = d.dest.getSiblingFile(d.dest.getFileName() + "-unpacking");
                tmpDir.deleteRecursively(); tmpDir.createDirectory();
                for (int i = 0; i < z.getNumEntries(); ++i) {
                    if (threadShouldExit()) return;
                    auto r = z.uncompressEntry(i, tmpDir, true);
                    if (r.failed()) { d.error = "could not unpack: " + r.getErrorMessage(); d.state = 3; return; }
                    d.rack.downloadProgress.store(0.99f);
                }
                d.zip.deleteFile();
                d.dest.deleteRecursively();
                tmpDir.moveFileTo(d.dest);
                d.sfz = InstrumentRack::installedSfz(d.info);
                if (!d.sfz.existsAsFile()) { d.error = "downloaded, but its .sfz was not found"; d.state = 3; return; }
                d.state = 2;
            } };
        unzipper = std::make_unique<Unzip>(*this); unzipper->startThread();
    }
};

struct SfzLoader : juce::Thread {
    juce::File file; std::shared_ptr<SfzInstrument> inst = std::make_shared<SfzInstrument>(); juce::String error; std::atomic<bool> done { false }, ok { false };
    std::atomic<float>& progress;
    SfzLoader(const juce::File& f, std::atomic<float>& p) : juce::Thread("ARCO SFZ"), file(f), progress(p) {}
    void run() override { ok = inst->load(file, error, [this](float x) { progress.store(x); return !threadShouldExit(); }); done = true; }
};

// ------------------------------------------------------------------
InstrumentRack::InstrumentRack() {
    formats.addDefaultFormats();
    sampler.prepare(sr);
    startTimerHz(10);
}
InstrumentRack::~InstrumentRack() {
    stopTimer();
    pluginWindow.reset();
    if (loader) loader->stopThread(10000);
    download.reset();
    const juce::ScopedLock sl(pluginLock); plugin.reset();
}

void InstrumentRack::prepare(double sampleRate) {
    sr = sampleRate;
    modular.prepare(sr);
    sampler.prepare(sr);
    const juce::ScopedLock sl(pluginLock);
    if (plugin) { plugin->releaseResources(); plugin->setRateAndBufferSizeDetails(sr, SUB); plugin->prepareToPlay(sr, SUB); }
}

void InstrumentRack::setKind(int k) {
    if (k == kind.load()) return;
    allOff();
    kind.store(juce::jlimit(0, 3, k));
    if (onChange) onChange();
}

// ------------------------------------------------------------------ notes
void InstrumentRack::noteOn(int note, float vel) {
    switch (kind.load()) {
    case KModular: modular.noteOn(note, vel); break;
    case KSampler: sampler.noteOn(note, vel); break;
    case KPlugin: { const juce::SpinLock::ScopedLockType sl(midiLock); midiPending.addEvent(juce::MidiMessage::noteOn(1, note, juce::jlimit(0.01f, 1.0f, vel)), 0); break; }
    default: break;
    }
}
void InstrumentRack::noteOff(int note) {
    switch (kind.load()) {
    case KModular: modular.noteOff(note); break;
    case KSampler: sampler.noteOff(note); break;
    case KPlugin: { const juce::SpinLock::ScopedLockType sl(midiLock); midiPending.addEvent(juce::MidiMessage::noteOff(1, note), 0); break; }
    default: break;
    }
}
void InstrumentRack::sustain(bool down) {
    sampler.sustain(down);
    const juce::SpinLock::ScopedLockType sl(midiLock); midiPending.addEvent(juce::MidiMessage::controllerEvent(1, 64, down ? 127 : 0), 0);
}
void InstrumentRack::allOff() {
    for (int n = 0; n < 128; ++n) modular.noteOff(n);
    sampler.allOff();
    const juce::SpinLock::ScopedLockType sl(midiLock);
    midiPending.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
    midiPending.addEvent(juce::MidiMessage::allNotesOff(1), 0);
}

// ------------------------------------------------------------------ audio
void InstrumentRack::render(float* L, float* R, int n, double sampleRate, double bpm, double beat) {
    juce::ignoreUnused(sampleRate);
    // the loops' beat while they run; still -> MODULAR keeps its own clock at the tempo
    if (beat == lastBeat) ++stillBlocks; else stillBlocks = 0;
    lastBeat = beat;
    double b = stillBlocks > 2 ? -1.0 : beat;
    int k = kind.load();
    // MODULAR keeps running when chosen (its sequences); the sampler and a plug-in let their notes ring out
    if (k == KModular) modular.process(L, R, n, bpm, b);
    sampler.process(L, R, n);
    {
        const juce::ScopedTryLock sl(pluginLock);
        if (sl.isLocked() && plugin != nullptr && pbuf.getNumChannels() >= 2 && pbuf.getNumSamples() >= n) {
            midiIn.clear();
            { const juce::SpinLock::ScopedTryLockType ml(midiLock); if (ml.isLocked()) { midiIn.swapWith(midiPending); } }
            pbuf.clear();
            juce::AudioBuffer<float> view(pbuf.getArrayOfWritePointers(), pbuf.getNumChannels(), n);
            plugin->processBlock(view, midiIn);
            auto* a = view.getReadPointer(0); auto* c = view.getReadPointer(juce::jmin(1, view.getNumChannels() - 1));
            for (int i = 0; i < n; ++i) { float x = a[i], y = c[i]; if (!std::isfinite(x)) x = 0; if (!std::isfinite(y)) y = 0; L[i] += juce::jlimit(-2.0f, 2.0f, x); R[i] += juce::jlimit(-2.0f, 2.0f, y); }
        }
    }
    float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(L[i]), std::abs(R[i]));
    level.store(juce::jmax(pk, level.load() * 0.93f));
}

// ------------------------------------------------------------------ SAMPLER
juce::File InstrumentRack::instrumentsFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("Instruments");
}
juce::File InstrumentRack::installedSfz(const InstrumentInfo& i) {
    auto dir = instrumentsFolder().getChildFile(i.id);
    if (!dir.isDirectory()) return {};
    juce::String rel(i.sfz);
    // the zip has one folder at the top (name-master): look below it for the program
    for (auto& sub : dir.findChildFiles(juce::File::findDirectories, false)) { auto f = sub.getChildFile(rel); if (f.existsAsFile()) return f; }
    if (dir.getChildFile(rel).existsAsFile()) return dir.getChildFile(rel);
    auto name = rel.fromLastOccurrenceOf("/", false, false);
    for (auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.sfz")) if (f.getFileName().equalsIgnoreCase(name)) return f;
    return {};
}

void InstrumentRack::loadSfz(const juce::File& f) {
    if (loader) { loader->stopThread(10000); loader.reset(); }
    samplerLoading = true; samplerProgress = 0; samplerError.clear();
    samplerName = f.getFileNameWithoutExtension(); samplerPath = f.getFullPathName();
    for (auto& i : instrumentCatalogue()) if (installedSfz(i) == f) samplerName = i.name;
    loader = std::make_unique<SfzLoader>(f, samplerProgress);
    loader->startThread();
    if (onChange) onChange();
}

void InstrumentRack::startDownload(const InstrumentInfo& i) {
    if (download) return;
    instrumentsFolder().createDirectory();
    download = std::make_unique<Download>(*this, i);
    download->zip = instrumentsFolder().getChildFile(juce::String(i.id) + ".zip");
    download->dest = instrumentsFolder().getChildFile(i.id);
    downloadId = i.id; downloadProgress = 0; downloadStatus = juce::String("downloading ") + i.name + juce::String::fromUTF8(" …");
    download->task = juce::URL(i.zipUrl).downloadToFile(download->zip, juce::URL::DownloadTaskOptions().withListener(download.get()));
    if (download->task == nullptr) { downloadStatus = "the download could not start"; download.reset(); downloadId.clear(); }
    if (onChange) onChange();
}
void InstrumentRack::cancelDownload() { if (!download) return; download.reset(); downloadId.clear(); downloadStatus = "download stopped"; if (onChange) onChange(); }

// ------------------------------------------------------------------ PLUG-INS
juce::Array<juce::PluginDescription> InstrumentRack::instrumentPlugins() const {
    juce::Array<juce::PluginDescription> r;
    for (auto& d : known.getTypes()) if (d.isInstrument) r.add(d);
    std::sort(r.begin(), r.end(), [](const juce::PluginDescription& a, const juce::PluginDescription& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return r;
}

void InstrumentRack::scanPlugins(std::function<void(const juce::String&)> progress) {
    for (auto* f : formats.getFormats()) {
        auto paths = f->getDefaultLocationsToSearch();
        juce::PluginDirectoryScanner sc(known, *f, paths, true, juce::File(), false);
        juce::String name;
        int guard = 0;
        while (guard++ < 2000) {
            if (progress) progress(f->getName() + ": " + sc.getNextPluginFileThatWillBeScanned());
            if (!sc.scanNextFile(true, name)) break;
        }
    }
}

juce::String InstrumentRack::loadPlugin(const juce::PluginDescription& d) {
    pluginWindow.reset();
    juce::String err;
    auto desc = d;
    formats.createPluginInstanceAsync(desc, sr, SUB, [this, desc](std::unique_ptr<juce::AudioPluginInstance> p, const juce::String& e) {
        if (p == nullptr) { pluginName = desc.name + " could not open: " + e; if (onChange) onChange(); return; }
        p->enableAllBuses();
        p->setRateAndBufferSizeDetails(sr, SUB);
        p->prepareToPlay(sr, SUB);
        if (pendingPluginState.isNotEmpty()) { juce::MemoryBlock mb; mb.fromBase64Encoding(pendingPluginState); p->setStateInformation(mb.getData(), (int) mb.getSize()); pendingPluginState.clear(); }
        int ch = juce::jmax(2, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
        std::unique_ptr<juce::AudioPluginInstance> old;
        { const juce::ScopedLock sl(pluginLock); pbuf.setSize(ch, SUB * 2); old = std::move(plugin); plugin = std::move(p); }
        if (old) old->releaseResources();
        pluginName = desc.name;
        kind.store(KPlugin);
        if (onChange) onChange();
    });
    return err;
}
void InstrumentRack::unloadPlugin() {
    pluginWindow.reset();
    std::unique_ptr<juce::AudioPluginInstance> old;
    { const juce::ScopedLock sl(pluginLock); old = std::move(plugin); }
    pluginName.clear();
}

void InstrumentRack::showPluginWindow() {
    if (plugin == nullptr) return;
    struct Win : juce::DocumentWindow {
        Win(const juce::String& n) : juce::DocumentWindow(n, juce::Colours::black, juce::DocumentWindow::closeButton) {}
        void closeButtonPressed() override { setVisible(false); }
    };
    if (pluginWindow == nullptr) {
        auto* ed = plugin->createEditorIfNeeded();
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor(*plugin);
        auto* w = new Win(pluginName);
        w->setUsingNativeTitleBar(true);
        w->setContentOwned(ed, true);
        w->setResizable(ed->isResizable(), false);
        w->centreWithSize(w->getWidth(), w->getHeight());
        pluginWindow.reset(w);
    }
    pluginWindow->setVisible(true); pluginWindow->toFront(true);
}

// ------------------------------------------------------------------ save / open
juce::var InstrumentRack::toVar() {
    auto* o = new juce::DynamicObject();
    o->setProperty("kind", kind.load());
    o->setProperty("modular", Modular::toVar(modular.patch));
    if (samplerPath.isNotEmpty()) o->setProperty("sfz", samplerPath);
    if (plugin != nullptr) {
        o->setProperty("plugin", plugin->getPluginDescription().createXml()->toString());
        juce::MemoryBlock mb; plugin->getStateInformation(mb); o->setProperty("pstate", mb.toBase64Encoding());
    }
    return juce::var(o);
}

void InstrumentRack::fromVar(const juce::var& v) {
    allOff();
    if (!v.isObject()) { kind.store(KSynth); modular.setPatch(Modular::defaultPatch(0)); return; }
    if (v["modular"].isObject()) modular.setPatch(Modular::fromVar(v["modular"]));
    auto sfz = v["sfz"].toString();
    if (sfz.isNotEmpty() && sfz != samplerPath && juce::File(sfz).existsAsFile()) loadSfz(juce::File(sfz));
    int k = (int) v.getProperty("kind", 0);
    if (v["plugin"].isString()) {
        if (auto xml = juce::parseXML(v["plugin"].toString())) {
            juce::PluginDescription d;
            if (d.loadFromXml(*xml)) { pendingPluginState = v["pstate"].toString(); loadPlugin(d); }
        }
    }
    kind.store(juce::jlimit(0, 3, k));
    if (onChange) onChange();
}

void InstrumentRack::timerCallback() {
    if (auto* l = dynamic_cast<SfzLoader*>(loader.get()); l != nullptr && l->done.load()) {
        if (l->ok.load()) { sampler.setInstrument(l->inst); kind.store(KSampler); samplerError.clear(); }
        else samplerError = l->error;
        samplerLoading = false;
        loader->stopThread(1000); loader.reset();
        if (onChange) onChange();
    }
    if (download) {
        int st = download->state.load();
        if (st == 2) { auto f = download->sfz; downloadStatus = juce::String::fromUTF8("installed ✓"); download.reset(); downloadId.clear(); loadSfz(f); }
        else if (st == 3) { downloadStatus = download->error; download.reset(); downloadId.clear(); if (onChange) onChange(); }
        else if (st == 1) downloadStatus = juce::String::fromUTF8("unpacking …");
    }
}

} // namespace arco
