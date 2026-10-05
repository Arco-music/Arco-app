#include "AudioHost.h"
#include "../UI/Theme.h"

#if JUCE_IOS
void arcoPreferBestMicrophone();   // IOSAudio.mm (global, Objective-C++)
#endif

namespace arco {

AudioHost::AudioHost(bool openDevice) : live(openDevice) {
    keys.addListener(this);
    synth.setParamSource([this](const char* n) { return model.get(n); });
    synth.onFirstNote = [this] { if (synthInto == 0) { setSynthInto(1); status = U("the SYNTH plays into LIVE 2"); } };
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
    devices.removeMidiInputDeviceCallback({}, this);
    phone.disconnect();
    micOut.stop();
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
    phoneBuf.assign((size_t) juce::jmax(8192, d->getCurrentBufferSizeSamples() * 2), 0.0f);
    if (r != lastRate) {                     // a new sample rate: the engine starts again (loops are kept only at the same rate)
        lastRate = r;
        engine.prepare(r);
        engineRestarted = true;
    }
    triggerAsyncUpdate();                    // every (re)start: the inputs / outputs may be different now
}

void AudioHost::handleAsyncUpdate() {
   #if JUCE_IOS
    // the microphone raw: no voice processing (noise gate, automatic level, echo cancelling) - those spoil an instrument
    if (auto* d = devices.getCurrentAudioDevice()) if (d != rawDone) { rawDone = d; d->setAudioPreprocessingEnabled(false); ::arcoPreferBestMicrophone(); }
   #endif
    if (engineRestarted.exchange(false)) { model.sendAll(); applySpeakers(); setSynthInto(synthInto); }
    applyRouting();
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                 const juce::AudioIODeviceCallbackContext&) {
    if (micMode.load()) {                        // iPhone / iPad as a microphone for the Mac: only send
        if (numIn > 0 && in[0] != nullptr) micOut.push(in[0], n, lastRate > 0 ? lastRate : 48000.0);
        for (int c = 0; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear(out[c], n);
        return;
    }
    if (phoneLive > 0 && phone.connected() && (int) phoneBuf.size() >= n) {
        phone.pull(phoneBuf.data(), n, lastRate);
        engine.phoneData = phoneBuf.data();
    } else engine.phoneData = nullptr;
    engine.process(in, numIn, out, numOut, n);
    int tt = testTone.load();
    if (tt > 0) {                                // TEST: a soft A (440 Hz) on outputs 1-2 and on the headphones
        int pf = engine.phonesFirst.load();
        for (int i = 0; i < n; ++i) {
            float env = juce::jmin(1.0f, juce::jmin((float) (tt - i), (float) (48000 - (tt - i))) / 2000.0f);
            float v = 0.15f * env * (float) std::sin(testPhase); testPhase += 2 * juce::MathConstants<double>::pi * 440.0 / juce::jmax(8000.0, lastRate);
            for (int c : { 0, 1, pf, pf + 1 }) if (c >= 0 && c < numOut && out[c]) out[c][i] += v;
        }
        testTone.store(juce::jmax(0, tt - n));
    }
}

// ------------------------------------------------------------------ pedals and keys
void AudioHost::doAction(int a) {
    static const char* cmd[NumActions] = { "recnext", "playstop", "undo", "scnext" };
    if (a < 0 || a >= NumActions) return;
    model.press("st_looper", cmd[a]);
    if (onAction) onAction(a);
}

juce::String AudioHost::pedalText(const Pedal& p) {
    if (p.type == 1) return "note " + juce::MidiMessage::getMidiNoteName(p.num, true, true, 4) + " ch" + juce::String(p.ch);
    if (p.type == 2) return "CC " + juce::String(p.num) + " ch" + juce::String(p.ch);
    return "-";
}

bool AudioHost::isPedalCC(int cc, int ch) const {
    for (auto& p : pedal) if (p.type == 2 && p.num == cc && p.ch == ch) return true;
    return false;
}

void AudioHost::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    int type = 0, num = 0, ch = m.getChannel();
    if (m.isNoteOn()) { type = 1; num = m.getNoteNumber(); }
    else if (m.isController() && (m.getControllerNumber() == 1 || m.getControllerNumber() == 64) && !isPedalCC(m.getControllerNumber(), ch)) {
        int c = m.getControllerNumber(), v = m.getControllerValue();
        if (c == 1) juce::MessageManager::callAsync([this, v] { model.set("l_pitch", v / 127.0 * 60.0); });   // mod wheel = vibrato
        else juce::MessageManager::callAsync([this, v] { synth.setSustain(v >= 64); });                       // sustain pedal
        if (learning.load() < 0) return;
        if (v < 64) return;
        type = 2; num = c;
    }
    else if (m.isController()) {
        int c = m.getControllerNumber(), v = m.getControllerValue(), was = ccLast[(ch - 1) & 15][c & 127];
        ccLast[(ch - 1) & 15][c & 127] = v;
        if (v >= 64 && was < 64) { type = 2; num = c; }      // a footswitch going down
    }
    if (type == 0) {
        if (m.isNoteOff()) { int n = m.getNoteNumber(), c = ch; juce::MessageManager::callAsync([this, n, c] { keys.noteOff(c, n, 0.0f); }); }
        else if (m.isPitchWheel()) { float b = (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f; juce::MessageManager::callAsync([this, b] { synth.pitchBend(b); }); }
        return;
    }
    int l = learning.load();
    if (l >= 0) {
        learning.store(-1);
        juce::MessageManager::callAsync([this, l, type, num, ch] { pedal[l] = { type, num, ch }; saveSetup(); });
        return;
    }
    for (int a = 0; a < NumActions; ++a)
        if (pedal[a].type == type && pedal[a].num == num && pedal[a].ch == ch) {
            juce::MessageManager::callAsync([this, a] { doAction(a); });
            return;                                   // a pedal, not a note
        }
    if (type == 1) { int n = num; float v = m.getFloatVelocity(); juce::MessageManager::callAsync([this, ch, n, v] { keys.noteOn(ch, n, v); }); }
}

void AudioHost::recordSynthOn(int track) {
    if (track < 1 || track > 8) return;
    if (synthInto == 0) setSynthInto(1);
    model.set("src" + std::to_string(track), synthInto + 1);   // that track listens to the LIVE input the synth plays into
    model.press("st_looper", "rec" + std::to_string(track));
    status = "TRACK " + juce::String(track) + U(" records the SYNTH");
}

void AudioHost::scanMidi() {                     // every MIDI keyboard / pedal that is plugged in is listened to
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (!devices.isMidiInputDeviceEnabled(d.identifier)) devices.setMidiInputDeviceEnabled(d.identifier, true);
}

void AudioHost::applyRouting() {
    // every channel of the sound card is open (openAllChannels), so the callback's channel k is the card's channel k:
    // choosing a live input or the headphone outputs never has to reopen the card
    auto setup = devices.getAudioDeviceSetup();
    for (int j = 0; j < 4; ++j) {
        int ch = inputCh[j] - 1, rank = -1;
        if (j < nLive && ch >= 0 && setup.inputChannels[ch]) { rank = 0; for (int c = 0; c < ch; ++c) if (setup.inputChannels[c]) ++rank; }
        engine.inputChannel[j].store(rank);
    }
    int pf = phonesPair >= 15 ? 0 : 2 + 2 * phonesPair;
    int nOut = setup.outputChannels.countNumberOfSetBits();
    phonesShared = devices.getCurrentAudioDevice() != nullptr && pf + 1 >= nOut;
    if (phonesShared) pf = 0;                 // a laptop / 2-output card: headphones = outputs 1-2
    engine.phonesFirst.store(pf);
}

void AudioHost::openAllChannels() {
    auto* d = devices.getCurrentAudioDevice();
    if (d == nullptr) return;
    auto s = devices.getAudioDeviceSetup();
    juce::BigInteger ins, outs;
    ins.setRange(0, juce::jmin(64, d->getInputChannelNames().size()), true);
    outs.setRange(0, juce::jmin(34, d->getOutputChannelNames().size()), true);
    if (s.inputChannels != ins || s.outputChannels != outs) {
        s.inputChannels = ins; s.outputChannels = outs;
        s.useDefaultInputChannels = s.useDefaultOutputChannels = false;
        devices.setAudioDeviceSetup(s, true);
    }
}

juce::String AudioHost::openAudio(const juce::String& outName, const juce::String& inName, double rate, int buffer) {
    auto before = devices.getAudioDeviceSetup();
    auto s = before;
    s.outputDeviceName = outName; s.inputDeviceName = inName;
    s.sampleRate = rate; s.bufferSize = buffer;
    s.useDefaultInputChannels = s.useDefaultOutputChannels = true;
    juce::String msg;
    auto err = devices.setAudioDeviceSetup(s, true);
    if (err.isNotEmpty()) {                    // 1) let the card choose its own sample rate and buffer
        s.sampleRate = 0; s.bufferSize = 0;
        auto e2 = devices.setAudioDeviceSetup(s, true);
        if (e2.isEmpty()) msg = U("opened with the card's own sample rate / buffer");
        else if (inName.isNotEmpty() && inName != outName) {
            // 2) these two do not work together (often: different sample rates, e.g. Bluetooth headphones + microphone)
            s.inputDeviceName = {};
            auto e3 = devices.setAudioDeviceSetup(s, true);
            if (e3.isEmpty()) msg = "\"" + inName + "\" does not work together with \"" + outName + "\" (" + err + "). Playing without input - choose another input, or the same device for both.";
            else err = e3;
        }
        if (msg.isEmpty() && devices.getCurrentAudioDevice() == nullptr) {   // 3) nothing worked: back to what played before
            devices.setAudioDeviceSetup(before, true);
            msg = "could not open \"" + outName + "\": " + err + (devices.getCurrentAudioDevice() != nullptr ? " - kept the previous one" : "");
        }
    }
    openAllChannels();
    applyRouting();
    saveSetup();
    audioMessage = msg;
    return msg;
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

juce::String AudioHost::loopsSignature() {
    juce::String sig;
    for (auto* k : { "smask", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3", "undoav" })
        sig << (int) engine.snap(k).v.load() << ",";
    return sig;
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
    savedSignature = loopsSignature();
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
    sigSettle = 20;                          // the loops arrive in the engine over the next moments
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
    if (!live) return;                       // tests / screenshots never touch this computer's settings
    auto f = setupFile(); f.getParentDirectory().createDirectory();
    auto* o = new juce::DynamicObject();
    o->setProperty("params", model.setupState());
    o->setProperty("nLive", nLive);
    juce::Array<juce::var> ic; for (int i = 0; i < 4; ++i) ic.add(inputCh[i]);
    o->setProperty("inputs", ic);
    o->setProperty("phones", phonesPair);
    o->setProperty("layout", speakerLayout);
    o->setProperty("palette", Theme::pal().name);
    o->setProperty("lastDark", Theme::palettes()[(size_t) Theme::lastDark()].name);
    o->setProperty("lastLight", Theme::palettes()[(size_t) Theme::lastLight()].name);
    juce::Array<juce::var> g;
    for (int s = 0; s < 14; ++s) for (int k = 0; k < NSPK; ++k) if (engine.grid[s][k].load() > 0) { g.add(s); g.add(k); }
    o->setProperty("grid", g);
    o->setProperty("phoneLive", phoneLive);
    { juce::Array<juce::var> vc; for (auto& k : visibleCards) vc.add(k); o->setProperty("cards", vc); }
    o->setProperty("phoneWifi", phoneWifi);
    o->setProperty("phoneMode", (int) phone.mode());
    juce::Array<juce::var> pd; for (auto& p : pedal) { pd.add(p.type); pd.add(p.num); pd.add(p.ch); }
    o->setProperty("pedals", pd);
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
        Theme::lastDark() = juce::jmin(1, Theme::paletteByName(v.getProperty("lastDark", "Fig").toString()));
        Theme::lastLight() = juce::jmax(2, Theme::paletteByName(v.getProperty("lastLight", "Manuscript").toString()));
        if (v.hasProperty("palette")) Theme::setPalette(Theme::paletteByName(v["palette"].toString()));
        else Theme::setPalette((int) v.getProperty("theme", 0) == 1 ? 2 : 0);
        if (auto* g = v["grid"].getArray()) for (int i = 0; i + 1 < g->size(); i += 2) engine.grid[(int) (*g)[i]][(int) (*g)[i + 1]].store(1.0f);
        setPhoneLive(juce::jlimit(0, 4, (int) v.getProperty("phoneLive", 0)));
        phoneWifi = v.getProperty("phoneWifi", "").toString();
        if (auto* vc = v["cards"].getArray()) { visibleCards.clear(); for (auto& k : *vc) visibleCards.add(k.toString()); }
        if (auto* pd = v["pedals"].getArray())
            for (int a = 0; a < NumActions && a * 3 + 2 < pd->size(); ++a) pedal[a] = { (int) (*pd)[a * 3], (int) (*pd)[a * 3 + 1], (int) (*pd)[a * 3 + 2] };
        if (openDevice && phoneLive > 0) {
            int pm = (int) v.getProperty("phoneMode", 0);
            if (pm == MicReceiver::Usb) phone.connect(MicReceiver::Usb);
            else if (pm == MicReceiver::WiFi && phoneWifi.isNotEmpty()) phone.connect(MicReceiver::WiFi, phoneWifi);
        }
        if (v["audio"].isString()) xml = juce::parseXML(v["audio"].toString());
    }
    if (openDevice) {
        auto err = devices.initialise(64, 34, xml.get(), true);
        if (err.isNotEmpty() || devices.getCurrentAudioDevice() == nullptr) { devices.initialise(64, 34, nullptr, true); }   // the saved card is not there: use the Mac's own
        openAllChannels();
        devices.addAudioCallback(this);
        devices.addMidiInputDeviceCallback({}, this);
        scanMidi();
    }
    applyRouting(); applySpeakers();
}

} // namespace arco
