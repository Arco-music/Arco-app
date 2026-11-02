#include "AudioHost.h"
#include "../UI/Theme.h"
#include "FxPresets.h"

#if JUCE_IOS
 #include "IOSAudio.h"
#endif

namespace arco {

AudioHost::AudioHost(bool openDevice) : live(openDevice) {
    keys.addListener(this);
    synth.setParamSource([this](const char* n) { return model.get(n); });
    synth.onFirstNote = [this] { synthNeedsStrip(); };
    formats.registerBasicFormats();
    writerThread.startThread();
    engine.prepare(48000.0);
    rack.prepare(48000.0); engine.ext.store(&rack);
    // 2.0: the DRUM MACHINE's kits are made in the background (the samples decoded, the classic machines modelled)
    juce::Thread::launch([this] { auto* b = DrumKits::bank(); engine.drumMachine.bank.store(b); });
    model.sendAll();
    loadSetup(openDevice);
    if (openDevice) fxpresets::applyDefaults(model);
    engine.recorder = [this](const float* l, const float* r, int n) {
        const juce::ScopedTryLock sl(recLock);
        if (sl.isLocked() && threaded != nullptr) { const float* ch[2] = { l, r }; threaded->write(ch, n); recSamples += n; }
    };
    if (openDevice && !isPhone()) phones.setEnabled(true);       // find the phones (Wi-Fi, cable)
    startTimerHz(10);
}

bool AudioHost::isPhone() {
   #if JUCE_IOS
    return juce::SystemStats::getDeviceDescription().containsIgnoreCase("iPhone") || juce::SystemStats::getDeviceDescription().containsIgnoreCase("iPod");
   #else
    return false;
   #endif
}

juce::String AudioHost::phoneOnLive(int live) {
    for (auto* c : phones.conns) if (c->live.load() == live && c->connected()) return c->name();
    return {};
}

// ------------------------------------------------------------------ phones: their commands, their screens
void AudioHost::phoneCommand(PhoneConn& c, const juce::String& line) {
    auto t = juce::StringArray::fromTokens(line, " ", "\"");
    if (t.isEmpty()) return;
    auto& w = t[0];
    auto num = [&](int i) { return t[i].getDoubleValue(); };
    if (w == "set" && t.size() >= 3) model.set(t[1].toStdString(), num(2));
    else if (w == "press" && t.size() >= 3) model.press(t[1].toStdString(), t[2].toStdString(), t.size() > 3 ? t[3].getIntValue() : 1, t.size() > 4 ? t[4].getIntValue() : 0);
    else if (w == "act" && t.size() >= 2) doAction(t[1].getIntValue());
    else if (w == "sel" && t.size() >= 2) model.select(juce::jlimit(0, NSTRIP - 1, t[1].getIntValue()));
    else if (w == "live" && t.size() >= 2) { phones.setLive(&c, juce::jlimit(0, 4, t[1].getIntValue())); saveSetup(); }
    else if (w == "listen" && t.size() >= 2) c.listenMix.store(juce::jlimit(0, 2, t[1].getIntValue()));
    else if (w == "noteon" && t.size() >= 3) keys.noteOn(1, juce::jlimit(0, 127, t[1].getIntValue()), (float) juce::jlimit(0.01, 1.0, num(2)));
    else if (w == "noteoff" && t.size() >= 2) keys.noteOff(1, juce::jlimit(0, 127, t[1].getIntValue()), 0.0f);
    else if (w == "alloff") { latched.clear(); synth.allOff(); synth.padOff(); rack.allOff(); }
    else if (w == "hold" && t.size() >= 2) setLatch(t[1].getIntValue() != 0);
    else if (w == "padon" && t.size() >= 3) synth.padOn((float) num(1), (float) num(2));
    else if (w == "padmove" && t.size() >= 3) synth.padMove((float) num(1), (float) num(2));
    else if (w == "padoff") synth.padOff();
    else if (w == "bend" && t.size() >= 2) synth.pitchBend((float) num(1));
    else if (w == "expr" && t.size() >= 2) synth.setExpression((float) juce::jlimit(0.0, 1.0, num(1)));
    else if (w == "sustain" && t.size() >= 2) setSustain(t[1].getIntValue() != 0);
    else if (w == "rec") {
        if (isRecording()) stopRecording();
        else {
           #if JUCE_IOS
            auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Recordings");
           #else
            auto d = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("ARCO Recordings");
           #endif
            d.createDirectory();
            startRecording(d.getChildFile("show " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + ".wav"));
        }
    }
    else if (w == "save") { if (songFolder.isDirectory()) saveSong(songFolder); else status = "save the song on the station first (it needs a name)"; }
    else if (w == "card" && t.size() >= 3) { if (t[2] == "1") visibleCards.addIfNotAlreadyThere(t[1]); else visibleCards.removeString(t[1]); if (onCardsChanged) onCardsChanged(); saveSetup(); }
}

void AudioHost::tickPhones() {
    phones.tick();
    static const char* SN[] = { "statA", "statB", "transport", "scenes", "smask", "ebpm", "barno", "beatno", "loopbars", "ntn", "undoav", "spdk", "lscene", "sv_bts", "beat", "halo_key", "pitch", "tn_pitch", "syn_n" };
    for (auto* c : phones.conns) {
        if (!c->connected() || c->legacy()) { c->needFull = true; c->sent.clear(); c->sentInfo.clear(); continue; }
        for (auto& line : c->takeCommands()) phoneCommand(*c, line);
        bool full = c->needFull; c->needFull = false;
        if (full) { c->sent.clear(); c->sentInfo.clear(); }
        juce::String out;
        auto num = [&](const char* tag, const std::string& id, double v, double eps) {
            auto key = std::string(tag) + id;
            auto it = c->sent.find(key);
            if (it != c->sent.end() && std::abs(it->second - v) <= eps) return;
            c->sent[key] = v; out << tag << " " << juce::String(id) << " " << juce::String(v, 3) << "\n"; };
        auto info = [&](const std::string& id, const juce::String& v) {
            auto it = c->sentInfo.find(id);
            if (it != c->sentInfo.end() && it->second == v) return;
            c->sentInfo[id] = v; out << "i " << juce::String(id) << " " << v.replaceCharacters("\n\r", "  ") << "\n"; };
        for (auto& d : paramTable()) num("p", d.id, model.get(d.id), 1e-6);
        for (auto* n : SN) num("s", n, engine.snap(n).v.load(), 1e-6);
        num("s", "sel", model.selected, 0); num("s", "nlive", nLive, 0); num("s", "live", c->live.load(), 0); num("s", "rec", isRecording() ? 1 : 0, 0);
        num("s", "synthInto", synthInto, 0); num("s", "scene", model.sceneNow, 0);
        for (int s2 = 0; s2 < NSTRIP; ++s2) num("s", "lv" + std::to_string(s2), std::round(engine.peak("ch" + std::to_string(s2)) * 100.0) / 100.0, 0.02);
        info("song", songFolder.isDirectory() ? songFolder.getFileName() : juce::String("new song"));
        info("cards", visibleCards.joinIntoString(","));
        info("status", status);
        info("palette", Theme::pal().name);
        for (int j = 1; j <= 4; ++j) info("phone" + std::to_string(j), phoneOnLive(j));
        if (out.isNotEmpty()) c->sendState(out);
    }
}

AudioHost::~AudioHost() {
    engine.ext.store(nullptr);
    if (live) saveSetup();
    devices.removeMidiInputDeviceCallback({}, this);
    stopTimer();
    phoneLink.stop();
    phones.setEnabled(false);
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
    studio.recordLatency = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
    for (auto& b : phoneBuf) b.assign((size_t) juce::jmax(8192, d->getCurrentBufferSizeSamples() * 2), 0.0f);
    mixTapL.assign((size_t) juce::jmax(8192, d->getCurrentBufferSizeSamples() * 2), 0.0f); mixTapR = mixTapL;
    if (r != lastRate) {                     // a new sample rate: the engine starts again (loops are kept only at the same rate)
        lastRate = r;
        engine.prepare(r);
        rack.prepare(r);
        engineRestarted = true;
    }
    triggerAsyncUpdate();                    // every (re)start: the inputs / outputs may be different now
}

void AudioHost::handleAsyncUpdate() {
    if (!studio.song.clips.empty()) studio.matchRate(engine.sr);       // the sound card's rate changed: STUDIO follows
   #if JUCE_IOS
    // the microphone raw: no voice processing (noise gate, automatic level, echo cancelling) - those spoil an instrument
    if (auto* d = devices.getCurrentAudioDevice()) if (d != rawDone) { rawDone = d; d->setAudioPreprocessingEnabled(false); ios::applyInput(); }
   #endif
    if (engineRestarted.exchange(false)) { model.sendAll(); applySpeakers(); setSynthInto(synthInto); }
    applyRouting();
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                 const juce::AudioIODeviceCallbackContext&) {
    if (micMode.load()) {                        // this device is a phone for a station: only send the microphone
        if (numIn > 0 && in[0] != nullptr) phoneLink.push(in[0], n, lastRate > 0 ? lastRate : 48000.0);
        for (int c = 0; c < numOut; ++c) if (out[c]) juce::FloatVectorOperations::clear(out[c], n);
        if (listenWant.load() > 0 && numOut >= 1 && out[0] != nullptr && (int) phoneBuf[0].size() >= n) {   // the station's sound in this phone's headphones
            float* R = numOut >= 2 && out[1] != nullptr ? out[1] : phoneBuf[1].data();
            phoneLink.listen.pull(out[0], R, n, lastRate > 0 ? lastRate : 48000.0);
            float v = listenVol.load(); v *= v;
            juce::FloatVectorOperations::multiply(out[0], v, n); if (R != phoneBuf[1].data()) juce::FloatVectorOperations::multiply(R, v, n);
            else juce::FloatVectorOperations::addWithMultiply(out[0], R, v, n), juce::FloatVectorOperations::multiply(out[0], 0.5f, n);
        }
        return;
    }
    {   // the phones playing as LIVE inputs
        const juce::ScopedTryLock sl(phones.audioLock);
        for (int j = 0; j < 4; ++j) {
            PhoneConn* c = sl.isLocked() ? phones.forLive(j + 1) : nullptr;
            if (c != nullptr && (int) phoneBuf[j].size() >= n) { c->pull(phoneBuf[j].data(), n, lastRate); engine.phoneData[j] = phoneBuf[j].data(); }
            else engine.phoneData[j] = nullptr;
        }
    }
    {   // the clock: STUDIO's position and tempo (it runs on top of the station, the arpeggiator / drums follow it)
        bool sp = studio.playing.load();
        engine.clock.studioRun.store(sp);
        if (sp) { double bl = 60.0 / juce::jmax(20.0, studio.song.bpm) * juce::jmax(8000.0, studio.song.sr);
                  engine.clock.studioBeat.store((double) studio.pos.load() / bl); engine.clock.studioBpm.store(studio.song.bpm); engine.clock.studioSeq.fetch_add(1); }
    }
    engine.process(in, numIn, out, numOut, n);
    {   // STUDIO: what it can record (the station's inputs, the sound card's inputs, the station's mix), then it plays on top
        const float* live[4] = {};
        for (int j = 0; j < 4; ++j) { int ch = engine.inputChannel[j].load(); if (ch < 0) ch = inputCh[j] - 1;
            live[j] = engine.phoneData[j] != nullptr ? engine.phoneData[j] : (ch >= 0 && ch < numIn ? in[ch] : nullptr); }
        if ((int) mixTapL.size() < n) { mixTapL.resize((size_t) n); mixTapR.resize((size_t) n); }      // (sized in audioDeviceAboutToStart; never here in practice)
        engine.pullMainMix(mixTapL.data(), mixTapR.data(), n);
        studio.input(in, numIn, live, mixTapL.data(), mixTapR.data(), n);
    }
    if (numOut >= 2 && out[0] != nullptr && out[1] != nullptr) {     // STUDIO plays on top
        bool rendered = studio.renderBlock(out[0], out[1], n);
        int pf = engine.phonesFirst.load();
        if (rendered && pf >= 2 && pf + 1 < numOut && out[pf] && out[pf + 1]) { juce::FloatVectorOperations::copy(out[pf], out[0], n); juce::FloatVectorOperations::copy(out[pf + 1], out[1], n); }
    }
    {   // phones that listen: the main mix (outputs 1-2) or the headphone mix
        const juce::ScopedTryLock sl(phones.audioLock);
        if (sl.isLocked() && numOut >= 2 && out[0] && out[1])
            for (auto* c : phones.conns) {
                int m = c->listenMix.load(); if (m <= 0) continue;
                int pf = engine.phonesFirst.load();
                bool hp = m == 2 && pf >= 2 && pf + 1 < numOut && out[pf] && out[pf + 1];
                c->pushListen(hp ? out[pf] : out[0], hp ? out[pf + 1] : out[1], n, lastRate > 0 ? lastRate : 48000.0);
            }
    }
    {   // one speaker's test: pink-ish noise bursts
        int ts = testSpeaker.load(), left = testSpkLeft.load();
        if (ts >= 0 && left > 0 && ts < numOut && out[ts] != nullptr) {
            for (int i = 0; i < n; ++i) { float w = rnd.nextFloat() * 2 - 1; pinkS += 0.05f * (w - pinkS); float env = ((left - i) / 6000) % 2 == 0 ? 1.0f : 0.0f; out[ts][i] += 0.12f * pinkS * 4.0f * env; }
            testSpkLeft.store(juce::jmax(0, left - n));
        }
    }
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
    // ---- MIDI learn and the learned controls come first
    {
        int lt = 0, ln = 0, lv = 0;
        if (m.isNoteOn()) { lt = 1; ln = m.getNoteNumber(); lv = m.getVelocity(); }
        else if (m.isController()) { lt = 2; ln = m.getControllerNumber(); lv = m.getControllerValue(); }
        if (lt != 0) {
            const juce::ScopedLock sl(midiLock);
            if (learnTarget.isNotEmpty()) {
                auto id = learnTarget; learnTarget = {};
                midiLinks.erase(std::remove_if(midiLinks.begin(), midiLinks.end(), [&](auto& l) { return l.id == id || (l.type == lt && l.num == ln && l.ch == ch); }), midiLinks.end());
                midiLinks.push_back({ id, lt, ln, ch });
                juce::MessageManager::callAsync([this] { saveSetup(); if (onLearned) onLearned(); });
                return;
            }
            bool handled = false;
            int was = lt == 2 ? ccLast[(ch - 1) & 15][ln & 127] : 0;
            for (auto& l : midiLinks)
                if (l.type == lt && l.num == ln && l.ch == ch) {
                    handled = true;
                    bool press = lt == 1 || (lv >= 64 && was < 64);
                    auto id = l.id; int t = lt, v = lv;
                    juce::MessageManager::callAsync([this, id, t, v, press] { if (onMidiControl) onMidiControl(id, t, v, press); });
                }
            if (handled) { if (lt == 2) ccLast[(ch - 1) & 15][ln & 127] = lv; return; }
        }
        if (m.isNoteOff()) { const juce::ScopedLock sl(midiLock); for (auto& l : midiLinks) if (l.type == 1 && l.num == m.getNoteNumber() && l.ch == ch) return; }
    }
    if (m.isNoteOn()) { type = 1; num = m.getNoteNumber(); }
    else if (m.isController() && (m.getControllerNumber() == 1 || m.getControllerNumber() == 64) && !isPedalCC(m.getControllerNumber(), ch)) {
        int c = m.getControllerNumber(), v = m.getControllerValue();
        if (c == 1) juce::MessageManager::callAsync([this, v] { model.set("l_pitch", v / 127.0 * 60.0); });   // mod wheel = vibrato
        else juce::MessageManager::callAsync([this, v] { setSustain(v >= 64); model.set("pdl_down", v >= 64 ? 1 : 0); });   // sustain pedal (the synth and the PEDAL effect)
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
    synthNeedsStrip();
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
    if (synthInto > 0 && synthInto < nLive) synthNeedsStrip();      // that strip is an input now: the synth moves on
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
        else if (speakerLayout == 1) a = -60.0 + i * 120.0 / juce::jmax(1, n - 1);
        else a = model.get("spk_az" + std::to_string(i));                 // 1.3: the layout drawn in SPEAKERS & SPACE
        // the same speakers for AMBISONICS (ring / arc: on the floor, all at the same distance)
        if (speakerLayout != 2 && i < n && std::abs(model.get("spk_az" + std::to_string(i)) - a) > 0.01) { model.set("spk_az" + std::to_string(i), a); model.set("spk_el" + std::to_string(i), 0); }
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

// a fresh song: no loops, every song and scene setting back to its start (this computer's settings stay)
void AudioHost::newSong() {
    studio.playing = false; { const juce::ScopedLock sl(studio.lock); studio.song = StudioSong(); }
    model.press("st_looper", "rstall");
    model.resetSong();
    rack.fromVar({});
    fxpresets::applyDefaults(model);          // the effects as you like them for new songs
    loadDrums(juce::File());                  // 2.0: empty patterns, empty LIVE LOOPS
    if (onOpenExtra) onOpenExtra(juce::File());
    songFolder = juce::File();
    sigSettle = 20;
    status = "new song";
}

juce::String AudioHost::loopsSignature() {
    juce::String sig;
    for (auto* k : { "smask", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3", "undoav" })
        sig << (int) engine.snap(k).v.load() << ",";
    return sig;
}

bool AudioHost::saveSong(const juce::File& folder) {
    if (!folder.createDirectory()) { status = "could not make the folder"; return false; }
    if (!studio.song.clips.empty()) studio.save(folder);           // STUDIO's arrangement goes with the song
    saveDrums(folder);                                             // 2.0: the drum machine's patterns
    if (onSaveExtra) onSaveExtra(folder);                          // ... and LIVE LOOPS
    auto name = folder.getFileName();
    auto root = model.songState();
    root.getDynamicObject()->setProperty("instrument", rack.toVar());   // the KEYBOARD's instrument (MODULAR patch, sampled instrument, plug-in)
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
    addRecent(folder);
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
    studio.playing = false; { const juce::ScopedLock sl(studio.lock); studio.song = StudioSong(); }
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
    rack.fromVar(v["instrument"]);
    loadDrums(folder);
    if (onOpenExtra) onOpenExtra(folder);
    auto s = v["session"];
    static const char* RS[15] = { "rs_bl", "rs_bts", "rs_len0", "rs_len1", "rs_len2", "rs_len3", "rs_mask0", "rs_mask1", "rs_mask2", "rs_mask3",
                                  "rs_spd0", "rs_spd1", "rs_spd2", "rs_spd3", "rs_ntr" };
    static const char* SV[15] = { "sv_bl", "sv_bts", "sv_len0", "sv_len1", "sv_len2", "sv_len3", "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3",
                                  "sv_spd0", "sv_spd1", "sv_spd2", "sv_spd3", "sv_ntr" };
    for (int i = 0; i < 15; ++i) engine.set("st_looper", RS[i], (double) s[SV[i]]);
    model.press("st_looper", "rs_go");
    songFolder = folder;
    addRecent(folder);
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
    if (n < 17) { status = f.getFileName() + " is empty"; return false; }
    auto b = std::make_shared<SharedBuffer>(); b->name = "st_import"; b->resize(n, 2);
    juce::AudioBuffer<float> tmp((int) juce::jmax(1u, r->numChannels), (int) n);
    r->read(&tmp, 0, (int) n, 0, true, true);
    std::memcpy(b->chan(0), tmp.getReadPointer(0), sizeof(float) * (size_t) n);
    std::memcpy(b->chan(1), tmp.getReadPointer(juce::jmin(1, tmp.getNumChannels() - 1)), sizeof(float) * (size_t) n);
    imports.push_back({ b, track, r->sampleRate, f.getFileName() });
    status = U("importing ") + f.getFileName() + U(" …");
    return true;
}

bool AudioHost::importBuffer(const juce::AudioBuffer<float>& in, int track, const juce::String& name) {
    long n = in.getNumSamples();
    if (n < 17 || in.getNumChannels() < 1) { status = "nothing to import"; return false; }
    auto b = std::make_shared<SharedBuffer>(); b->name = "st_import"; b->resize(n, 2);
    std::memcpy(b->chan(0), in.getReadPointer(0), sizeof(float) * (size_t) n);
    std::memcpy(b->chan(1), in.getReadPointer(juce::jmin(1, in.getNumChannels() - 1)), sizeof(float) * (size_t) n);
    imports.push_back({ b, track, engine.sr, name });
    status = U("importing ") + name + U(" …");
    return true;
}

// imports, one after the other: the looper takes a file only when it is free (not counting in, not copying another)
int AudioHost::trackState(int track) {
    int k = juce::jlimit(1, 8, track) - 1;
    float pack = engine.snap(k < 4 ? "statA" : "statB").v.load();
    return ((int) pack >> (4 * (k % 4))) & 15;
}
void AudioHost::pollImport() {
    if (imports.empty()) return;
    auto& im = imports.front();
    double now = juce::Time::getMillisecondCounterHiRes();
    if (im.stage == 0) {                                   // hand the file to the engine
        engine.setImport(im.buf);
        engine.set("st_looper", "imp_trk", im.track);
        engine.set("st_looper", "imp_sr", im.sr);
        im.stage = 1; im.t = now; im.started = now; im.stateBefore = trackState(im.track);
        return;
    }
    if (im.stage == 1) {                                   // ask the looper (again, if it was busy)
        if (now - im.t < 50) return;
        model.press("st_looper", "imp_go");
        im.stage = 2; im.t = now;
        status = "copying " + im.name + U(" into the track …");
        return;
    }
    // stage 2: did it start?
    bool busy = engine.snap("imp_busy").v.load() > 0.5f;
    if (busy) { im.seenBusy = true; return; }
    if (im.seenBusy || (trackState(im.track) != 0 && trackState(im.track) != im.stateBefore)) {
        status = U("✓ ") + im.name + " imported";
        imports.pop_front(); return;
    }
    if (now - im.started > 12000) { status = im.name + U(" could not be imported - try again"); imports.pop_front(); return; }
    if (now - im.t > 300) im.stage = 1;                    // the looper was busy (a count-in, another file): ask again
}

PieceInfo AudioHost::pieceInfo(bool listenForKey) {
    PieceInfo p; p.sr = engine.sr;
    auto sv = [this](const char* n) { return (double) engine.snap(n).v.load(); };
    int smask = (int) sv("smask");
    p.hasLoops = smask > 0;
    p.beats = juce::jmax(1, (int) std::round(p.hasLoops ? sv("sv_bts") : model.get("beats")));
    if (p.hasLoops && sv("sv_bl") > 16) { p.beatLen = sv("sv_bl"); p.bpm = 60.0 * engine.sr / p.beatLen; }
    else { p.bpm = juce::jlimit(30.0, 300.0, model.get("bpm")); p.beatLen = 60.0 * engine.sr / p.bpm; }
    int sc = juce::jlimit(1, 4, model.sceneNow);
    const char* LEN[4] = { "sv_len0", "sv_len1", "sv_len2", "sv_len3" };
    p.loopLen = (long) sv(LEN[sc - 1]);
    p.scene = sc;
    if (p.hasLoops) { int hk = (int) sv("halo_key"); if (hk >= 0 && sv("halo_used") > 0.5) { p.key.tonic = hk % 12; p.key.minor = false; p.key.confidence = 0.4; } }
    if (listenForKey) listenKey(p);
    return p;
}
void AudioHost::listenKey(PieceInfo& p) {
    auto sv = [this](const char* n) { return (double) engine.snap(n).v.load(); };
    int smask = (int) sv("smask"), sc = p.scene;
    const char* LEN[4] = { "sv_len0", "sv_len1", "sv_len2", "sv_len3" };
    if (smask == 0) return;
    // listen to the loops of this scene (or the first scene that has some)
    const char* L[4] = { "A", "B", "C", "D" }; const char* MASK[4] = { "sv_mask0", "sv_mask1", "sv_mask2", "sv_mask3" };
    int use = (smask >> (sc - 1)) & 1 ? sc : 0;
    for (int s = 1; s <= 4 && use == 0; ++s) if ((smask >> (s - 1)) & 1) use = s;
    if (use > 0) {
        long len = (long) sv(LEN[use - 1]); int mask = (int) sv(MASK[use - 1]);
        auto& b = engine.buffer(std::string("st_loops") + L[use - 1]);
        len = juce::jmin(len, b.len, (long) (engine.sr * 90));
        if (len > 1024) {
            std::vector<float> mix((size_t) len, 0.0f);
            for (int t = 0; t < 8; ++t) if ((mask >> t) & 1) { const float* d = b.chan(t); for (long i = 0; i < len; ++i) mix[(size_t) i] += d[i]; }
            auto k = analysis::key(mix.data(), (int) len, engine.sr);
            if (k.tonic >= 0) p.key = k;
        }
    }
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

// ------------------------------------------------------------------ 2.0: the drum machine's patterns (drums.json in the song)
void AudioHost::saveDrums(const juce::File& folder) {
    auto& D = engine.drumMachine;
    auto* o = new juce::DynamicObject(); juce::Array<juce::var> pats;
    bool any = false;
    for (int p = 0; p < DrumMachine::NPAT; ++p) {
        auto* po = new juce::DynamicObject(); po->setProperty("len", D.patLen[p].load()); po->setProperty("res", D.patRes[p].load());
        juce::Array<juce::var> rows;
        for (int pd = 0; pd < DrumMachine::NPAD; ++pd) { juce::String r; for (int s = 0; s < DrumMachine::NSTEP; ++s) { auto w = D.steps[p][pd][s].load(); any |= w != 0; r << juce::String::toHexString((juce::int64) w) << (s + 1 < DrumMachine::NSTEP ? "," : ""); } rows.add(r); }
        po->setProperty("steps", rows); pats.add(juce::var(po));
    }
    auto f = folder.getChildFile("drums.json");
    if (!any) { f.deleteFile(); return; }
    o->setProperty("patterns", pats);
    f.replaceWithText(juce::JSON::toString(juce::var(o)));
}
void AudioHost::loadDrums(const juce::File& folder) {
    auto& D = engine.drumMachine;
    for (auto& a : D.steps) for (auto& b : a) for (auto& c : b) c.store(0);
    for (int p = 0; p < DrumMachine::NPAT; ++p) { D.patLen[p].store(16); D.patRes[p].store(1); }
    auto v = juce::JSON::parse(folder.getChildFile("drums.json"));
    if (auto* pats = v["patterns"].getArray())
        for (int p = 0; p < juce::jmin(DrumMachine::NPAT, pats->size()); ++p) {
            juce::var po = (*pats)[p];
            D.patLen[p].store(juce::jlimit(1, DrumMachine::NSTEP, (int) po.getProperty("len", 16))); D.patRes[p].store(juce::jlimit(0, 3, (int) po.getProperty("res", 1)));
            if (auto* rows = po["steps"].getArray())
                for (int pd = 0; pd < juce::jmin(DrumMachine::NPAD, rows->size()); ++pd) {
                    auto t = juce::StringArray::fromTokens((*rows)[pd].toString(), ",", "");
                    for (int s = 0; s < juce::jmin(DrumMachine::NSTEP, t.size()); ++s) D.steps[p][pd][s].store((uint32_t) t[s].getHexValue64());
                }
        }
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
    o->setProperty("phonesPair", phonesPair);
   #if JUCE_IOS
    o->setProperty("iosInput", ios::chosenInput());
    o->setProperty("iosSpeaker", ios::speakerForced());
   #endif
    o->setProperty("listenVol", listenVol.load());
    o->setProperty("layout", speakerLayout);
    o->setProperty("palette", Theme::pal().name);
    o->setProperty("lastDark", Theme::palettes()[(size_t) Theme::lastDark()].name);
    o->setProperty("lastLight", Theme::palettes()[(size_t) Theme::lastLight()].name);
    juce::Array<juce::var> g;
    for (int s = 0; s < 14; ++s) for (int k = 0; k < NSPK; ++k) if (engine.grid[s][k].load() > 0) { g.add(s); g.add(k); }
    o->setProperty("grid", g);
    { juce::Array<juce::var> ph; for (auto& kv : phones.remembered) { auto* x = new juce::DynamicObject(); x->setProperty("name", kv.first); x->setProperty("live", kv.second); ph.add(juce::var(x)); }
      o->setProperty("phones", ph); }
    { juce::Array<juce::var> pm; for (auto& a : phones.manual) pm.add(a); o->setProperty("phoneAddresses", pm); }
    { juce::Array<juce::var> vc; for (auto& k : visibleCards) vc.add(k); o->setProperty("cards", vc); }
    { juce::Array<juce::var> rs; for (auto& k : recentSongs) rs.add(k); o->setProperty("recent", rs); }
    juce::Array<juce::var> pd; for (auto& p : pedal) { pd.add(p.type); pd.add(p.num); pd.add(p.ch); }
    { const juce::ScopedLock sl(midiLock); juce::Array<juce::var> ml;
      for (auto& l : midiLinks) { auto* x = new juce::DynamicObject(); x->setProperty("id", l.id); x->setProperty("t", l.type); x->setProperty("n", l.num); x->setProperty("c", l.ch); ml.add(juce::var(x)); }
      o->setProperty("midimap", ml); }
    o->setProperty("pedals", pd);
    { juce::Array<juce::var> us; for (auto& k : rack.userSfz) us.add(k); o->setProperty("userSfz", us); }
    { juce::Array<juce::var> cp; for (auto& pad : chordPads) { juce::Array<juce::var> a; for (int n : pad) a.add(n); cp.add(a); } o->setProperty("chordPads", cp); }
    if (auto kx = rack.known.createXml()) o->setProperty("plugins", kx->toString());
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
        phonesPair = v["phonesPair"].isVoid() ? 0 : juce::jlimit(0, 15, (int) v["phonesPair"]);   // (1.2 wrote it under "phones", overwritten by the phone list)
       #if JUCE_IOS
        ios::chooseInput(v.getProperty("iosInput", "auto").toString());
        if ((bool) v.getProperty("iosSpeaker", false)) ios::forceSpeaker(true);
       #endif
        listenVol.store((float) v.getProperty("listenVol", 0.8));
        speakerLayout = (int) v.getProperty("layout", 0);
        Theme::lastDark() = juce::jmin(1, Theme::paletteByName(v.getProperty("lastDark", "Black").toString()));
        Theme::lastLight() = juce::jmax(2, Theme::paletteByName(v.getProperty("lastLight", "Light").toString()));
        if (v.hasProperty("palette")) Theme::setPalette(Theme::paletteByName(v["palette"].toString()));
        else Theme::setPalette((int) v.getProperty("theme", 0) == 1 ? 2 : 0);
        if (auto* g = v["grid"].getArray()) for (int i = 0; i + 1 < g->size(); i += 2) engine.grid[(int) (*g)[i]][(int) (*g)[i + 1]].store(1.0f);
        if (auto* ph = v["phones"].getArray()) for (auto& x : *ph) phones.remembered[x["name"].toString()] = juce::jlimit(0, 4, (int) x["live"]);
        if (auto* pm = v["phoneAddresses"].getArray()) for (auto& a : *pm) phones.addAddress(a.toString());
        { auto old = v.getProperty("phoneWifi", "").toString(); if (old.isNotEmpty() && (int) v.getProperty("phoneLive", 0) > 0) phones.addAddress(old); }
        if (auto* rs = v["recent"].getArray()) for (auto& k : *rs) if (juce::File(k.toString()).isDirectory()) recentSongs.add(k.toString());
        if (auto* vc = v["cards"].getArray()) { visibleCards.clear(); for (auto& k : *vc) visibleCards.add(k.toString()); }
        if (auto* ml = v["midimap"].getArray()) { const juce::ScopedLock sl(midiLock); midiLinks.clear();
            for (auto& x : *ml) midiLinks.push_back({ x["id"].toString(), (int) x["t"], (int) x["n"], (int) x["c"] }); }
        if (auto* pd = v["pedals"].getArray())
            for (int a = 0; a < NumActions && a * 3 + 2 < pd->size(); ++a) pedal[a] = { (int) (*pd)[a * 3], (int) (*pd)[a * 3 + 1], (int) (*pd)[a * 3 + 2] };
        if (v["audio"].isString()) xml = juce::parseXML(v["audio"].toString());
        if (auto* cp = v["chordPads"].getArray()) for (int i = 0; i < juce::jmin(8, cp->size()); ++i) { chordPads[(size_t) i].clear(); if (auto* a = (*cp)[i].getArray()) for (auto& n : *a) chordPads[(size_t) i].push_back((int) n); }
        if (auto* us = v["userSfz"].getArray()) for (auto& k : *us) if (juce::File(k.toString()).existsAsFile()) rack.userSfz.addIfNotAlreadyThere(k.toString());
        if (v["plugins"].isString()) if (auto kx = juce::parseXML(v["plugins"].toString())) rack.known.recreateFromXml(*kx);
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
