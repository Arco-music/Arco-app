#include "Sampler.h"

namespace arco {

// ---------------------------------------------------------------- the catalogue
const std::vector<InstrumentInfo>& instrumentCatalogue() {
    static const std::vector<InstrumentInfo> c = {
        { "splendid", "Splendid Grand Piano", "a concert grand, 5 dynamics, string resonance", "public domain",
          "https://codeload.github.com/sfzinstruments/SplendidGrandPiano/zip/refs/heads/master", "Splendid Grand Piano.sfz", 80 },
        { "salamander", "Salamander Grand Piano", "Yamaha C5, 16 dynamics - the classic free piano", "CC-BY 3.0 (Alexander Holm)",
          "https://codeload.github.com/sfzinstruments/SalamanderGrandPiano/zip/refs/heads/master", "Salamander Grand Piano V3.sfz", 1100 },
        { "rhodes", "jRhodes3d - Rhodes Mark I", "a 1977 Rhodes Stage 73, full-length notes", "samples CC BY-NC, your music CC0 (J. Learman)",
          "https://codeload.github.com/sfzinstruments/jlearman.jRhodes3d/zip/refs/heads/master", "jRhodes3d-st.sfz", 100 },
        { "dbass", "Double Bass (Rubner 1958)", "arco and pizzicato, played by D. Smolken", "CC0",
          "https://codeload.github.com/sfzinstruments/dsmolken.double-bass/zip/refs/heads/master", "d_smolken_rubner_bass_arco.sfz", 300 },
        { "meatbass", "Meatbass (Karoryfer)", "a bowed and plucked bass with character", "free (see its licence)",
          "https://codeload.github.com/sfzinstruments/karoryfer.meatbass/zip/refs/heads/master", "Programs/02_arco_3vel.sfz", 250 },
        { "drums", "Virtuosity Drums", "a real acoustic kit, many mics and layers", "free (see its licence)",
          "https://codeload.github.com/sfzinstruments/virtuosity_drums/zip/refs/heads/master", "Programs/01-basic-kit.sfz", 1700 },
    };
    return c;
}

// ---------------------------------------------------------------- reading the SFZ
static int noteNumber(const juce::String& v) {
    auto s = v.trim().toLowerCase();
    if (s.isEmpty()) return -1;
    if (juce::CharacterFunctions::isDigit(s[0]) || s[0] == '-') return s.getIntValue();
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    int l = s[0] - 'a'; if (l < 0 || l > 6) return -1;
    int n = base[l]; int i = 1;
    if (i < s.length() && s[i] == '#') { ++n; ++i; } else if (i < s.length() && s[i] == 'b') { --n; ++i; }
    int oct = s.substring(i).getIntValue();
    return juce::jlimit(0, 127, (oct + 1) * 12 + n);
}

// the text, with comments gone, #define replaced and #include files pasted in
static juce::String preprocess(const juce::File& f, const juce::File& root, std::map<juce::String, juce::String>& defs, int depth) {
    if (depth > 8 || !f.existsAsFile()) return {};
    auto t = f.loadFileAsString();
    juce::String out;
    // strip the comments: "// to the end of the line" and "/* ... */" (whichever starts first: "//****" is a line comment)
    {
        juce::String clean; clean.preallocateBytes((size_t) t.getNumBytesAsUTF8() + 1);
        auto p = t.getCharPointer(); juce::juce_wchar prev = 0;
        while (! p.isEmpty()) {
            auto c0 = *p; auto nx = p; ++nx; auto c1 = *nx;
            if (c0 == '/' && c1 == '/') { while (! p.isEmpty() && *p != '\n') ++p; continue; }
            if (c0 == '/' && c1 == '*') { ++p; ++p; while (! p.isEmpty()) { if (*p == '*') { auto q = p; ++q; if (*q == '/') { p = q; ++p; break; } } ++p; } clean << ' '; continue; }
            clean += c0; prev = c0; ++p;
        }
        juce::ignoreUnused(prev);
        t = clean;
    }
    juce::StringArray lines; lines.addLines(t);
    for (auto line : lines) {
        auto tl = line.trim();
        if (tl.startsWith("#define")) {
            auto rest = tl.substring(7).trim();
            auto key = rest.upToFirstOccurrenceOf(" ", false, false).trim(), val = rest.fromFirstOccurrenceOf(" ", false, false).trim();
            if (key.startsWith("$")) defs[key] = val;
            continue;
        }
        // $variables (longest first)
        if (line.contains("$")) {
            std::vector<juce::String> keys; for (auto& kv : defs) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end(), [](const juce::String& a, const juce::String& b) { return a.length() > b.length(); });
            for (auto& k : keys) line = line.replace(k, defs[k]);
            tl = line.trim();
        }
        if (tl.startsWith("#include")) {
            auto name = tl.fromFirstOccurrenceOf("\"", false, false).upToLastOccurrenceOf("\"", false, false).replaceCharacter('\\', '/');
            out << preprocess(root.getChildFile(name), root, defs, depth + 1) << "\n";
            continue;
        }
        out << line << "\n";
    }
    return out;
}

// a file in the instrument folder, whatever the case of its name
static juce::File findFile(const juce::File& dir, const juce::String& rel) {
    auto direct = dir.getChildFile(rel);
    if (direct.existsAsFile()) return direct;
    juce::File cur = dir;
    for (auto& part : juce::StringArray::fromTokens(rel, "/", "")) {
        if (part.isEmpty() || part == ".") continue;
        if (part == "..") { cur = cur.getParentDirectory(); continue; }
        auto c = cur.getChildFile(part);
        if (!c.exists()) { bool found = false;
            for (auto& ch : cur.findChildFiles(juce::File::findFilesAndDirectories, false)) if (ch.getFileName().equalsIgnoreCase(part)) { c = ch; found = true; break; }
            if (!found) return {}; }
        cur = c;
    }
    return cur.existsAsFile() ? cur : juce::File();
}

bool SfzInstrument::load(const juce::File& sfz, juce::String& error, std::function<bool(float)> progress) {
    name = sfz.getFileNameWithoutExtension();
    std::map<juce::String, juce::String> defs;
    auto root = sfz.getParentDirectory();
    auto text = preprocess(sfz, root, defs, 0);
    if (text.isEmpty()) { error = "could not read " + sfz.getFileName(); return false; }
    // tokens: <header> and opcode=value (a value may hold spaces: it runs to the next opcode)
    using Ops = std::map<std::string, juce::String>;
    Ops control, global, master, group, cur; int level = 0;  // 1 control 2 global 3 master 4 group 5 region
    juce::String defaultPath;
    std::map<juce::String, int> sampleIndex;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::vector<Ops> regionOps;
    auto flush = [&] { if (level == 5) { Ops r = global; for (auto& kv : master) r[kv.first] = kv.second; for (auto& kv : group) r[kv.first] = kv.second; for (auto& kv : cur) r[kv.first] = kv.second; regionOps.push_back(r); } cur.clear(); };
    int i = 0, n = text.length();
    auto isOpChar = [](juce::juce_wchar ch) { return juce::CharacterFunctions::isLetterOrDigit(ch) || ch == '_'; };
    while (i < n) {
        auto ch = text[i];
        if (ch == '<') {
            int e = text.indexOf(i, ">"); if (e < 0) break;
            auto h = text.substring(i + 1, e).trim().toLowerCase();
            flush();
            if (h == "control") level = 1; else if (h == "global") { level = 2; global.clear(); master.clear(); group.clear(); }
            else if (h == "master") { level = 3; master.clear(); group.clear(); } else if (h == "group") { level = 4; group.clear(); }
            else if (h == "region") level = 5; else level = 0;              // <curve>, <effect> ...: ignored
            i = e + 1; continue;
        }
        if (isOpChar(ch)) {
            int s = i; while (i < n && isOpChar(text[i])) ++i;
            if (i < n && text[i] == '=') {
                auto key = text.substring(s, i).toStdString(); ++i;
                // the value: up to the next "word=" or "<" (sample names can have spaces)
                int vs = i, ve = i;
                while (ve < n && text[ve] != '<' && text[ve] != '\n') {
                    if (text[ve] == ' ' || text[ve] == '\t') {
                        int k = ve; while (k < n && (text[k] == ' ' || text[k] == '\t')) ++k;
                        int w = k; while (w < n && isOpChar(text[w])) ++w;
                        if (w > k && w < n && text[w] == '=') break;
                    }
                    ++ve;
                }
                auto val = text.substring(vs, ve).trim();
                i = ve;
                Ops* target = level == 1 ? &control : level == 2 ? &global : level == 3 ? &master : level == 4 ? &group : &cur;
                (*target)[key] = val;
                if (level == 1) {
                    if (key == "default_path") defaultPath = val.replaceCharacter('\\', '/');
                    if (key.rfind("set_hdcc", 0) == 0) ccDefaults[std::atoi(key.c_str() + 8)] = val.getFloatValue();
                    else if (key.rfind("set_cc", 0) == 0) ccDefaults[std::atoi(key.c_str() + 6)] = val.getFloatValue() / 127.0f;
                }
                continue;
            }
            continue;
        }
        ++i;
    }
    flush();
    if (regionOps.empty()) { error = "no regions in " + sfz.getFileName(); return false; }
    // the regions
    int done = 0;
    for (auto& o : regionOps) {
        SfzRegion r;
        auto get = [&](const char* k, const juce::String& d = {}) { auto it = o.find(k); return it == o.end() ? d : it->second; };
        auto f = [&](const char* k, float d) { auto it = o.find(k); return it == o.end() ? d : it->second.getFloatValue(); };
        auto sname = get("sample");
        if (sname.isEmpty() || sname.startsWith("*")) continue;                    // *sine etc: not supported
        auto rel = (defaultPath + sname).replaceCharacter('\\', '/');
        auto it = sampleIndex.find(rel);
        if (it == sampleIndex.end()) {
            auto file = findFile(root, rel);
            SfzSample S; S.path = rel;
            if (file.existsAsFile())
                if (std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(file)); rd != nullptr) {
                    S.ch = (int) juce::jlimit(1u, 2u, rd->numChannels); S.sr = rd->sampleRate; S.frames = (long) rd->lengthInSamples;
                    juce::AudioBuffer<float> b(S.ch, (int) S.frames); rd->read(&b, 0, (int) S.frames, 0, true, S.ch > 1);
                    S.data.resize((size_t) (S.frames * S.ch));
                    for (long k = 0; k < S.frames; ++k) for (int c = 0; c < S.ch; ++c) S.data[(size_t) (k * S.ch + c)] = (int16_t) juce::jlimit(-32767, 32767, (int) std::lround(b.getSample(c, (int) k) * 32767.0f));
                    auto ls = rd->metadataValues.getValue("Loop0Start", ""), le = rd->metadataValues.getValue("Loop0End", "");
                    if (ls.isNotEmpty() && le.isNotEmpty()) { S.loopStart = ls.getLargeIntValue(); S.loopEnd = le.getLargeIntValue(); }
                }
            samples.push_back(std::move(S));
            it = sampleIndex.emplace(rel, (int) samples.size() - 1).first;
            if (progress && !progress((float) ++done / (float) regionOps.size())) { error = "cancelled"; return false; }
        }
        r.sample = it->second;
        if (samples[(size_t) r.sample].frames <= 0) continue;
        int key = noteNumber(get("key", "-1"));
        r.lokey = key >= 0 ? key : noteNumber(get("lokey", "0")); r.hikey = key >= 0 ? key : noteNumber(get("hikey", "127"));
        r.keycenter = noteNumber(get("pitch_keycenter", key >= 0 ? juce::String(key) : "60"));
        r.lovel = (int) f("lovel", 1); r.hivel = (int) f("hivel", 127);
        r.tune = f("tune", 0) + f("pitch", 0); r.transpose = f("transpose", 0); r.volume = f("volume", 0) + f("group_volume", 0); r.pan = f("pan", 0);
        r.amplitude = f("amplitude", 100); r.veltrack = f("amp_veltrack", 100); r.keytrack = f("pitch_keytrack", 100);
        r.delay = f("delay", 0) + f("ampeg_delay", 0); r.attack = f("ampeg_attack", 0.001f); r.hold = f("ampeg_hold", 0); r.decay = f("ampeg_decay", 0);
        r.sustain = f("ampeg_sustain", 100); r.release = f("ampeg_release", 0.06f);
        r.releaseTrigger = get("trigger").equalsIgnoreCase("release");
        auto lm = get("loop_mode", get("loopmode"));
        r.loopMode = lm == "no_loop" ? 0 : lm == "one_shot" ? 1 : lm == "loop_continuous" ? 2 : lm == "loop_sustain" ? 3 : -1;
        r.loopStart = (long) f("loop_start", (float) f("loopstart", -1)); r.loopEnd = (long) f("loop_end", (float) f("loopend", -1)); r.offset = (long) f("offset", 0);
        r.lorand = f("lorand", 0); r.hirand = f("hirand", 1);
        r.seqLength = (int) f("seq_length", 1); r.seqPosition = (int) f("seq_position", 1);
        r.group = (int) f("group", 0); r.offBy = (int) f("off_by", 0); r.notePolyphony = (int) f("note_polyphony", 0); r.offTime = f("off_time", 0.006f);
        for (auto& kv : o) {
            const auto& k = kv.first;
            if (k.rfind("locc", 0) == 0) { int c = std::atoi(k.c_str() + 4); r.ccRange[c].first = kv.second.getIntValue(); if (!o.count("hicc" + std::to_string(c))) r.ccRange[c].second = 127; }
            else if (k.rfind("hicc", 0) == 0) { int c = std::atoi(k.c_str() + 4); r.ccRange[c].second = kv.second.getIntValue(); if (!o.count("locc" + std::to_string(c))) r.ccRange[c].first = 0; }
            auto p = k.find("_oncc");
            if (p != std::string::npos && k.find("curve") == std::string::npos) r.oncc[k.substr(0, p)].push_back({ std::atoi(k.c_str() + p + 5), kv.second.getFloatValue() });
            if (k.rfind("ampeg_release", 0) == 0 && k.size() > 13 && k.substr(13, 2) == "cc" && std::isdigit((unsigned char) k[15])) r.oncc["ampeg_release"].push_back({ std::atoi(k.c_str() + 15), kv.second.getFloatValue() });
        }
        regions.push_back(r);
    }
    if (regions.empty()) { error = "the samples of " + sfz.getFileName() + " were not found"; return false; }
    return true;
}

// ---------------------------------------------------------------- playing
void SfzPlayer::setInstrument(std::shared_ptr<SfzInstrument> i) {
    allOff();
    std::atomic_store(&inst, std::move(i));
}

void SfzPlayer::release(Voice& v) {
    if (v.stage >= 5) return;
    if (v.r != nullptr) {                 // the release time with the controllers as they are now (e.g. a release that follows the pedal)
        float rel = v.r->release; auto it = v.r->oncc.find("ampeg_release");
        if (it != v.r->oncc.end()) for (auto& pr : it->second) rel += pr.second * ccVal(pr.first);
        v.rel = std::max(0.005f, rel);
    }
    v.stage = 5; v.envRel = v.env;
    v.t = 0;
}

void SfzPlayer::start(int note, float vel, bool releasePhase) {
    auto* I = playing.get(); if (I == nullptr) return;
    int vel127 = juce::jlimit(1, 127, (int) std::lround(vel * 127));
    float rr = rnd.nextFloat();
    int& seq = seqCounter[juce::jlimit(0, 127, note)];
    cc[131] = vel; cc[133] = note / 127.0f;
    bool any = false;
    for (auto& r : I->regions) {
        if (r.releaseTrigger != releasePhase) continue;
        if (note < r.lokey || note > r.hikey || vel127 < r.lovel || vel127 > r.hivel) continue;
        if (rr < r.lorand || rr >= r.hirand) continue;
        if (r.seqLength > 1 && (seq % r.seqLength) + 1 != r.seqPosition) continue;
        bool ok = true;
        for (auto& kv : r.ccRange) { int v = (int) std::lround(ccVal(kv.first) * 127); if (v < kv.second.first || v > kv.second.second) ok = false; }
        if (!ok) continue;
        // a new note on the same key / an off_by group stops the older one
        for (auto& v : voices) if (v.active && v.stage < 5 && ((r.notePolyphony > 0 && v.note == note && v.r && v.r->group == r.group) || (v.r && v.r->offBy != 0 && v.r->offBy == r.group))) { v.fastRel = v.r ? std::max(0.003f, v.r->offTime) : 0.006f; release(v); }
        // a free voice (or the oldest)
        Voice* v = nullptr; long oldest = LONG_MAX;
        for (auto& x : voices) if (!x.active) { v = &x; break; }
        if (v == nullptr) for (auto& x : voices) if (x.age < oldest) { oldest = x.age; v = &x; }
        auto& S = I->samples[(size_t) r.sample];
        auto mod = [&](const char* op, float base) { auto it = r.oncc.find(op); if (it == r.oncc.end()) return base; for (auto& pr : it->second) base += pr.second * ccVal(pr.first); return base; };
        *v = Voice();
        v->active = true; v->r = &r; v->s = &S; v->note = note; v->vel = vel; v->age = ++clock; v->held = !releasePhase;
        double semis = (note - r.keycenter) * r.keytrack / 100.0 + r.transpose + r.tune / 100.0;
        v->step = std::pow(2.0, semis / 12.0) * S.sr / sr;
        v->pos = (double) juce::jlimit(0L, S.frames - 1, r.offset);
        float veltrack = mod("amp_veltrack", r.veltrack) / 100.0f;
        float velGain = 1.0f - veltrack + veltrack * vel * vel;
        float ampl = r.oncc.count("amplitude") ? mod("amplitude", 0) : r.amplitude;
        float g = std::pow(10.0f, r.volume / 20.0f) * velGain * juce::jlimit(0.0f, 4.0f, ampl / 100.0f);
        float pan = juce::jlimit(-100.0f, 100.0f, r.pan) / 100.0f;
        v->gl = g * std::cos((pan + 1) * 0.785398f) * 1.4142f; v->gr = g * std::sin((pan + 1) * 0.785398f) * 1.4142f;
        v->delay = r.delay; v->att = std::max(0.0005f, r.attack); v->hold = std::max(0.0f, mod("ampeg_hold", r.hold)); v->dec = std::max(0.0f, mod("ampeg_decay", r.decay));
        v->sus = juce::jlimit(0.0f, 1.0f, r.sustain / 100.0f); v->rel = std::max(0.005f, mod("ampeg_release", r.release));
        int lmode = r.loopMode >= 0 ? r.loopMode : (S.loopEnd > S.loopStart && S.loopStart >= 0 ? 2 : 0);
        v->loopMode = lmode; v->loopS = r.loopStart >= 0 ? r.loopStart : S.loopStart; v->loopE = r.loopEnd >= 0 ? r.loopEnd : S.loopEnd;
        if (v->loopE <= v->loopS || v->loopE >= S.frames) v->loopMode = lmode == 1 ? 1 : 0;
        v->stage = v->delay > 0 ? 0 : 1; v->env = 0; v->t = 0;
        any = true;
    }
    if (any && !releasePhase) ++seq;
}

void SfzPlayer::process(float* L, float* R, int n) {
    auto cur = std::atomic_load(&inst);
    if (cur != playing) { for (auto& v : voices) v.active = false; playing = cur; if (playing) for (auto& kv : playing->ccDefaults) if (kv.first >= 0 && kv.first < 140) cc[kv.first] = kv.second; }
    // events
    int ready = fifo.getNumReady();
    if (ready > 0) {
        int s1, n1, s2, n2; fifo.prepareToRead(ready, s1, n1, s2, n2);
        auto handle = [&](const Ev& e) {
            if (e.type == 1) { noteVel[e.note & 127] = e.val; start(e.note, e.val, false); }
            else if (e.type == 2) {
                for (auto& v : voices) if (v.active && v.note == e.note && v.held) { v.held = false; if (pedal) v.sustained = true; else if (v.loopMode != 1) release(v); }
                if (!pedal) start(e.note, noteVel[e.note & 127] > 0 ? noteVel[e.note & 127] : 0.6f, true);
            } else if (e.type == 3) {
                pedal = e.val > 0.5f; cc[64] = e.val;
                if (!pedal) for (auto& v : voices) if (v.active && v.sustained) { v.sustained = false; release(v); }
            } else if (e.type == 4) { for (auto& v : voices) if (v.active) { v.fastRel = 0.02f; release(v); } pedal = false; }
        };
        for (int i = 0; i < n1; ++i) handle(evs[s1 + i]);
        for (int i = 0; i < n2; ++i) handle(evs[s2 + i]);
        fifo.finishedRead(n1 + n2);
    }
    if (!playing) return;
    const float g = gain.load(), dt = 1.0f / (float) sr;
    for (auto& v : voices) {
        if (!v.active) continue;
        auto& S = *v.s; const int16_t* d = S.data.data(); const float k = 1.0f / 32767.0f;
        for (int i = 0; i < n; ++i) {
            // envelope
            v.t += dt;
            switch (v.stage) {
                case 0: if (v.t >= v.delay) { v.stage = 1; v.t = 0; } break;
                case 1: v.env = std::min(1.0f, (float) (v.t / v.att)); if (v.t >= v.att) { v.stage = v.hold > 0 ? 2 : 3; v.t = 0; } break;
                case 2: v.env = 1; if (v.t >= v.hold) { v.stage = 3; v.t = 0; } break;
                case 3: { if (v.dec <= 0) { v.env = v.sus; v.stage = 4; break; }
                          float c = std::exp(-6.9f * (float) v.t / v.dec); v.env = v.sus + (1 - v.sus) * c; if (v.t >= v.dec) { v.env = v.sus; v.stage = 4; } break; }
                case 4: if (v.sus <= 0.0001f) { v.active = false; } break;
                case 5: { float rt = v.fastRel > 0 ? v.fastRel : v.rel; v.env = v.envRel * std::exp(-6.9f * (float) v.t / rt); if (v.env < 0.0005f) v.active = false; break; }
            }
            if (!v.active) break;
            if (v.stage == 0) continue;
            // the sample (linear interpolation; loops)
            long p0 = (long) v.pos; float fr = (float) (v.pos - (double) p0);
            if (p0 + 1 >= S.frames) { v.active = false; break; }
            float l, r;
            if (S.ch == 1) { float a = d[p0] * k, b = d[p0 + 1] * k; l = r = a + (b - a) * fr; }
            else { float a = d[p0 * 2] * k, b = d[p0 * 2 + 2] * k, c = d[p0 * 2 + 1] * k, e = d[p0 * 2 + 3] * k; l = a + (b - a) * fr; r = c + (e - c) * fr; }
            L[i] += l * v.gl * v.env * g; R[i] += r * v.gr * v.env * g;
            v.pos += v.step;
            if ((v.loopMode == 2 || (v.loopMode == 3 && v.stage < 5)) && v.pos >= (double) v.loopE) v.pos -= (double) (v.loopE - v.loopS);
        }
    }
}

} // namespace arco
