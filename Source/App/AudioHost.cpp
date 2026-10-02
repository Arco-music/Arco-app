#include "AudioHost.h"
#include "../UI/Theme.h"

namespace arco {

AudioHost::AudioHost(bool openDevice) : live(openDevice) {
    formats.registerBasicFormats();
    writerThread.startThread();
    engine.prepare(48000.0);
    model.sendAll();
    loadSetup(openDevice);
    engine.recorder = [this](const float* l, const float* r, int n) {
        const juce::ScopedTryLock sl(recLock);
        if (sl.isLocked() && threaded != nullptr) { const float* ch[2] = { l, r }; threaded->write(ch, n); recSamples += n; }
    };
}

AudioHost::~AudioHost() {
    if (live) saveSetup();
    stopRecording();
    devices.removeAudioCallback(this);
    devices.closeAudioDevice();
    writerThread.stopThread(2000);
}

juce::File AudioHost::setupFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("setup.json");
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* d) {
    double r = d->getCurrentSampleRate();
    if (r != lastRate) {                     // a new sample rate: the engine starts again (loops are kept only at the same rate)
        lastRate = r;
        engine.prepare(r);
        engineRestarted = true;
    }
    triggerAsyncUpdate();                    // every (re)start: the inputs / outputs may be different now
}

void AudioHost::handleAsyncUpdate() {
    if (engineRestarted.exchange(false)) { model.sendAll(); applySpeakers(); setSynthInto(synthInto); }
    applyRouting();
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                 const juce::AudioIODeviceCallbackContext&) {
    engine.process(in, numIn, out, numOut, n);
}

void AudioHost::applyRouting() {
    // switch on exactly the interface inputs the live inputs use; the callback gets the active ones in order
    auto setup = devices.getAudioDeviceSetup();
    juce::BigInteger want;
    for (int j = 0; j < nLive; ++j) want.setBit(juce::jmax(0, inputCh[j] - 1));
    if (devices.getCurrentAudioDevice() != nullptr && want != setup.inputChannels) {
        setup.useDefaultInputChannels = false; setup.inputChannels = want;
        devices.setAudioDeviceSetup(setup, true);
        setup = devices.getAudioDeviceSetup();
    }
    for (int j = 0; j < 4; ++j) {
        int ch = inputCh[j] - 1, rank = -1;
        if (j < nLive && setup.inputChannels[ch]) { rank = 0; for (int c = 0; c < ch; ++c) if (setup.inputChannels[c]) ++rank; }
        engine.inputChannel[j].store(rank);
    }
    int pf = phonesPair >= 15 ? 0 : 2 + 2 * phonesPair;
    int nOut = setup.outputChannels.countNumberOfSetBits();
    phonesShared = devices.getCurrentAudioDevice() != nullptr && pf + 1 >= nOut;
    if (phonesShared) pf = 0;                 // a laptop / 2-output card: headphones = outputs 1-2
    engine.phonesFirst.store(pf);
}

void AudioHost::applySpeakers() {
    int n = (int) model.get("spk_n");
    for (int i = 0; i < 32; ++i) {
        double a = 0;
        // ring: speaker 1 front-left, then clockwise (2 = front-right ...), the usual numbering for 4 / 6 / 8 rings
        if (speakerLayout == 0) {
            if (n <= 2) a = i == 0 ? -30.0 : 30.0;
            else { a = -180.0 / n + i * 360.0 / n; if (a > 180.0) a -= 360.0; }
        }
        else a = -60.0 + i * 120.0 / juce::jmax(1, n - 1);
        engine.set("st_room", "spk_a" + std::to_string(i), a);
        engine.set("st_room", "spk_d" + std::to_string(i), 100);
    }
}

// ------------------------------------------------------------------ songs: one folder
static bool writeBuffer(SharedBuffer& b, const juce::File& f, double sr, long frames) {
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto* os = f.createOutputStream().release();
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os, sr, (unsigned int) b.ch, 32, {}, 0));
    if (w == nullptr) { delete os; return false; }
    std::vector<const float*> ch; for (long c = 0; c < b.ch; ++c) ch.push_back(b.chan(c));
    long n = juce::jmax(1L, juce::jmin(frames, b.len));
    return w->writeFromFloatArrays(ch.data(), (int) b.ch, (int) n);
}

bool AudioHost::saveSong(const juce::File& folder) {
    if (!folder.createDirectory()) { status = "could not make the folder"; return false; }
    auto name = folder.getFileName();
    auto root = model.songState();
    auto* sess = new juce::DynamicObject();
    for (auto* k : { "sv_bl", "sv_bts", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3",
                     "sv_spd0", "sv_spd1", "sv_spd2", "sv_spd3", "sv_ntr" })
        sess->setProperty(k, engine.snap(k).v.load());
    sess->setProperty("samplerate", engine.sr);
    root.getDynamicObject()->setProperty("session", juce::var(sess));
    root.getDynamicObject()->setProperty("app", "ARCO 1.0");
    if (!folder.getChildFile(name + "-settings.json").replaceWithText(juce::JSON::toString(root))) { status = "could not write the song"; return false; }
    const char* L[4] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i) {
        long len = (long) engine.snap(std::string("sv_len") + std::to_string(i)).v.load();
        if (len <= 0) { folder.getChildFile(name + "-" + L[i] + ".wav").deleteFile(); continue; }
        writeBuffer(engine.buffer(std::string("st_loops") + L[i]), folder.getChildFile(name + "-" + L[i] + ".wav"), engine.sr, len);
        writeBuffer(engine.buffer(std::string("st_pitch") + L[i]), folder.getChildFile(name + "-p" + L[i] + ".wav"), engine.sr, len / 64 + 2);
    }
    songFolder = folder;
    status = "saved \"" + name + U("\" ✓");
    return true;
}

static void readInto(juce::AudioFormatManager& fm, const juce::File& f, SharedBuffer& b) {
    std::fill(b.data.begin(), b.data.end(), 0.0f);
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) return;
    long n = juce::jmin((long) r->lengthInSamples, b.len);
    juce::AudioBuffer<float> tmp((int) r->numChannels, (int) n);
    r->read(&tmp, 0, (int) n, 0, true, true);
    for (int c = 0; c < juce::jmin((int) b.ch, (int) r->numChannels); ++c) std::memcpy(b.chan(c), tmp.getReadPointer(c), sizeof(float) * (size_t) n);
}

bool AudioHost::openSong(const juce::File& sel) {
    juce::File folder = sel.isDirectory() ? sel : sel.getParentDirectory();
    juce::File settings;
    if (sel.isDirectory()) { auto fs = folder.findChildFiles(juce::File::findFiles, false, "*-settings.json"); if (!fs.isEmpty()) settings = fs[0]; }
    else settings = sel;
    if (!settings.existsAsFile()) { status = "no song in that folder"; return false; }
    auto base = settings.getFileName().upToLastOccurrenceOf("-settings.json", false, false);
    auto v = juce::JSON::parse(settings);
    if (!v.isObject()) { status = "the song file could not be read"; return false; }
    model.press("st_looper", "stopall");
    const char* L[4] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i) {
        readInto(formats, folder.getChildFile(base + "-" + L[i] + ".wav"), engine.buffer(std::string("st_loops") + L[i]));
        readInto(formats, folder.getChildFile(base + "-p" + L[i] + ".wav"), engine.buffer(std::string("st_pitch") + L[i]));
    }
    model.loadSongState(v);
    auto s = v["session"];
    static const char* RS[15] = { "rs_bl", "rs_bts", "rs_len0", "rs_len1", "rs_len2", "rs_len3", "rs_mask0", "rs_mask1", "rs_mask2", "rs_mask3",
                                  "rs_spd0", "rs_spd1", "rs_spd2", "rs_spd3", "rs_ntr" };
    static const char* SV[15] = { "sv_bl", "sv_bts", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3",
                                  "sv_spd0", "sv_spd1", "sv_spd2", "sv_spd3", "sv_ntr" };
    for (int i = 0; i < 15; ++i) engine.set("st_looper", RS[i], (double) s[SV[i]]);
    model.press("st_looper", "rs_go");
    songFolder = folder;
    status = "\"" + base + U("\" ✓");
    return true;
}

// ------------------------------------------------------------------ import
bool AudioHost::importFile(const juce::File& f, int track) {
    status = "reading " + f.getFileName() + U(" …");
    std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
    if (r == nullptr) { status = "can't read " + f.getFileName() + " (use WAV, AIFF, MP3 or FLAC)"; return false; }
    long n = juce::jmin((long) r->lengthInSamples, (long) (r->sampleRate * 600));
    auto b = std::make_shared<SharedBuffer>(); b->name = "st_import"; b->resize(juce::jmax(17L, n), 2);
    juce::AudioBuffer<float> tmp((int) juce::jmax(1u, r->numChannels), (int) n);
    r->read(&tmp, 0, (int) n, 0, true, true);
    std::memcpy(b->chan(0), tmp.getReadPointer(0), sizeof(float) * (size_t) n);
    std::memcpy(b->chan(1), tmp.getReadPointer(juce::jmin(1, tmp.getNumChannels() - 1)), sizeof(float) * (size_t) n);
    engine.setImport(b);
    engine.set("st_looper", "imp_trk", track);
    engine.set("st_looper", "imp_sr", r->sampleRate);
    juce::Timer::callAfterDelay(60, [this, name = f.getFileName()] { model.press("st_looper", "imp_go"); status = "copying " + name + U(" into the track …"); });
    return true;
}

// ------------------------------------------------------------------ the show recording: one stereo .wav
bool AudioHost::startRecording(const juce::File& f) {
    stopRecording();
    f.deleteFile();
    auto* os = f.createOutputStream().release();
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os, engine.sr, 2, 24, {}, 0));
    if (w == nullptr) { delete os; return false; }
    const juce::ScopedLock sl(recLock);
    recSamples = 0;
    threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(w.release(), writerThread, 32768);
    status = U("recording the show …");
    return true;
}
void AudioHost::stopRecording() {
    const juce::ScopedLock sl(recLock);
    if (threaded != nullptr) { threaded.reset(); status = U("show recording saved ✓"); }
}

// ------------------------------------------------------------------ this computer
void AudioHost::saveSetup() {
    auto f = setupFile(); f.getParentDirectory().createDirectory();
    auto* o = new juce::DynamicObject();
    o->setProperty("params", model.setupState());
    o->setProperty("nLive", nLive);
    juce::Array<juce::var> ic; for (int i = 0; i < 4; ++i) ic.add(inputCh[i]);
    o->setProperty("inputs", ic);
    o->setProperty("phones", phonesPair);
    o->setProperty("layout", speakerLayout);
    o->setProperty("theme", Theme::lightMode() ? 1 : 0);
    juce::Array<juce::var> g;
    for (int s = 0; s < 14; ++s) for (int k = 0; k < NSPK; ++k) if (engine.grid[s][k].load() > 0) { g.add(s); g.add(k); }
    o->setProperty("grid", g);
    if (auto xml = devices.createStateXml()) o->setProperty("audio", xml->toString());
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}

void AudioHost::loadSetup(bool openDevice) {
    auto v = juce::JSON::parse(setupFile());
    std::unique_ptr<juce::XmlElement> xml;
    if (v.isObject()) {
        model.loadSetupState(v["params"]);
        nLive = juce::jlimit(1, 4, (int) v.getProperty("nLive", 1));
        if (auto* a = v["inputs"].getArray()) for (int i = 0; i < juce::jmin(4, a->size()); ++i) inputCh[i] = (int) (*a)[i];
        phonesPair = (int) v.getProperty("phones", 0);
        speakerLayout = (int) v.getProperty("layout", 0);
        Theme::lightMode() = (int) v.getProperty("theme", 0) == 1;
        if (auto* g = v["grid"].getArray()) for (int i = 0; i + 1 < g->size(); i += 2) engine.grid[(int) (*g)[i]][(int) (*g)[i + 1]].store(1.0f);
        if (v["audio"].isString()) xml = juce::parseXML(v["audio"].toString());
    }
    if (openDevice) {
        devices.initialise(4, 34, xml.get(), true);
        devices.addAudioCallback(this);
    }
    applyRouting(); applySpeakers();
}

} // namespace arco
