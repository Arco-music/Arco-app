#include "Sheets.h"
#include "MainView.h"
#include "HelpText.h"
#include "../App/Updater.h"

namespace arco {

// ================================================================== Sheet
void Sheet::text(juce::Graphics& g, const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt, int lines) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, wt));
    g.drawFittedText(t, { box.getX() + x, box.getY() + y, w, (int) (fs * 1.6f * lines) + 4 }, juce::Justification::topLeft, lines, 0.9f);
}
void Sheet::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.25f : 0.55f));
    g.setColour(Theme::bg()); g.fillRoundedRectangle(box.toFloat(), 16.0f);
    g.setColour(Theme::line()); g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 16.0f, 1.0f);
    text(g, title, 24, 16, 600, 17.0f, Theme::txt(), 2, 1);
    if (subtitle.isNotEmpty()) text(g, subtitle, 24, 46, box.getWidth() - 160, 8.5f, Theme::sub(), 0, 2);
    paintContent(g);
}
void Sheet::choices(int x, int y, const juce::StringArray& names, int w, std::function<int()> get, std::function<void(int)> set) {
    for (int i = 0; i < names.size(); ++i) {
        auto* p = new Pill(names[i], [get, i] { return get() == i ? Theme::sel() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [this, set, i] { set(i); repaint(); };
        kids.add(p); addAndMakeVisible(p); p->setBounds(box.getX() + x + i * (w + 6), box.getY() + y, w, 26);
    }
}

// ================================================================== EXPORT
ExportSheet::ExportSheet(Canvas& c) : Sheet(c, "EXPORT", { 200, 50, 1040, 780 }), juce::Thread("ARCO export") {
    subtitle = juce::String::fromUTF8("the song rendered at full quality, faster than real time - measured, levelled for where it is going, never clipping");
    int y = 100, L = 200;
    choices(L, y, { "THIS SCENE", juce::String::fromUTF8("THE WHOLE SONG  (A → D)") }, 220, [this] { return opt.what; }, [this](int i) { opt.what = i; });
    y += 62;
    choices(L, y, { "1", "2", "4", "8", "16" }, 56, [this] { int r = opt.repeats; return r == 1 ? 0 : r == 2 ? 1 : r == 4 ? 2 : r == 8 ? 3 : 4; },
            [this](int i) { static const int R[5] = { 1, 2, 4, 8, 16 }; opt.repeats = R[i]; });
    y += 62;
    choices(L, y, { "NONE", "2 s", "4 s", "8 s" }, 70, [this] { double t = opt.tailSeconds; return t < 1 ? 0 : t < 3 ? 1 : t < 6 ? 2 : 3; },
            [this](int i) { static const double T[4] = { 0, 2, 4, 8 }; opt.tailSeconds = T[i]; });
    y += 62;
    choices(L, y, { "WAV 16-bit", "WAV 24-bit", "WAV 32-bit float", "FLAC 16-bit", "FLAC 24-bit" }, 136, [this] { return fileChoice; },
            [this](int i) { fileChoice = i; static const int B[5] = { 16, 24, 32, 16, 24 }; opt.bits = B[i]; opt.flac = i >= 3; });
    y += 62;
    choices(L, y, { "44.1 kHz", "48 kHz", "96 kHz" }, 100, [this] { return rateChoice; },
            [this](int i) { rateChoice = i; static const double S[3] = { 44100, 48000, 96000 }; opt.sampleRate = S[i]; });
    y += 62;
    choices(L, y, { "AS IS", juce::String::fromUTF8("STREAMING  −14 LUFS"), juce::String::fromUTF8("LOUD  −10 LUFS"), juce::String::fromUTF8("BROADCAST  −23 LUFS") }, 176,
            [this] { return opt.loudness; }, [this](int i) { opt.loudness = i; });
    y += 62;
    choices(L, y, { "MIX ONLY", "+ STEMS  (AS RECORDED)", "+ STEMS  (WITH EFFECTS)" }, 216, [this] { return stemChoice; },
            [this](int i) { stemChoice = i; opt.stems = i > 0; opt.stemsDry = i == 1; });
    opt.bits = 24; opt.sampleRate = 48000;
    addAndMakeVisible(go); go.setBounds(box.getRight() - 224, box.getBottom() - 70, 200, 46);
    go.onClick = [this] { start(); };
    addChildComponent(share); share.setBounds(box.getRight() - 340, box.getBottom() - 64, 104, 34);
    share.onClick = [this] {
        if (written.isEmpty()) return;
       #if JUCE_IOS
        juce::Array<juce::URL> urls; for (auto& f : written) urls.add(juce::URL(f));
        juce::ContentSharer::shareFilesScoped(urls, [](bool, const juce::String&) {});
       #else
        written[0].revealToUser();
       #endif
    };
   #if JUCE_IOS
    share.text = "SHARE";
   #endif
    startTimerHz(15);
}
ExportSheet::~ExportSheet() { stopThread(5000); }

void ExportSheet::paintContent(juce::Graphics& g) {
    const char* rows[7] = { "WHAT", "LOOPS", "ECHO TAIL", "FILE", "SAMPLE RATE", "LEVEL", "STEMS" };
    const char* help[7] = { "the scene you are on, or every scene that has loops, one after the other",
                            "how many times each scene plays",
                            "after the last loop the effects ring out",
                            "WAV for everything; FLAC is the same quality, smaller; 32-bit float for further mixing",
                            "48 kHz for video, 44.1 kHz for CD, 96 kHz for archives",
                            "streaming: Spotify, Apple Music, YouTube level. The true peak always stays below -1 dB",
                            "every track as its own file too, in a folder next to the mix (for STUDIO or any other program)" };
    for (int r = 0; r < 7; ++r) {
        text(g, rows[r], 24, 104 + r * 62, 170, 9.5f, Theme::txt(), 2, 1);
        text(g, juce::String::fromUTF8(help[r]), 200, 132 + r * 62, 800, 7.5f, Theme::sub(), 0, 1);
    }
    auto bar = juce::Rectangle<float>((float) box.getX() + 24, (float) box.getBottom() - 116, (float) box.getWidth() - 48, 8);
    if (busy || progress > 0) {
        g.setColour(Theme::card2()); g.fillRoundedRectangle(bar, 4);
        g.setColour(Theme::green()); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())), 4);
    }
    juce::String st = busy ? juce::String::fromUTF8("rendering … ") + juce::String((int) (progress * 100)) + " %" : (error.isNotEmpty() ? error : report);
    text(g, st, 24, box.getHeight() - 96, box.getWidth() - 400, 8.5f, error.isNotEmpty() ? Theme::red() : Theme::txt(), 1, 3);
    if (cv.engine().snap("smask").v.load() < 0.5f) text(g, "Record some loops first - there is nothing to export yet.", 24, box.getHeight() - 60, 600, 9.0f, Theme::amber(), 1, 1);
}

void ExportSheet::start() {
    if (busy) return;
    if (cv.engine().snap("smask").v.load() < 0.5f) { error = "nothing recorded yet"; repaint(); return; }
    juce::String name = cv.host.songFolder.isDirectory() ? cv.host.songFolder.getFileName() : juce::String("ARCO mix");
    juce::String ext = opt.flac ? ".flac" : ".wav";
    auto begin = [this](juce::File f) {
        target = f; error = {}; report = {}; written.clear(); share.setVisible(false);
        snap = Renderer(cv.host).snapshot();
        busy = true; progress = 0.001f;
        startThread();
    };
   #if JUCE_IOS
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Exports"); d.createDirectory();
    begin(d.getChildFile(name + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M") + ext));
   #else
    chooser = std::make_unique<juce::FileChooser>("Export the mix as", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(name + ext), "*" + ext);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting | juce::FileBrowserComponent::canSelectFiles,
        [this, ext, begin](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) begin(f.withFileExtension(ext.substring(1))); });
   #endif
}

void ExportSheet::run() {
    Renderer rd(cv.host);
    juce::AudioBuffer<float> mix; double rate = 0;
    int nStems = 0;
    juce::Array<int> stemStrips;
    if (opt.stems && !opt.stemsDry) {
        int mask = 0;
        for (int s = 1; s <= 4; ++s) if (opt.what == ExportOptions::AllScenes || s == snap.sceneNow) mask |= (int) snap.sv[6 + s - 1];
        for (int t = 0; t < 8; ++t) if ((mask >> t) & 1) stemStrips.add(4 + t);
        nStems = stemStrips.size();
    }
    float share1 = 1.0f / (1 + nStems);
    bool ok = rd.renderMix(snap, opt, mix, rate, -1, [this, share1](float p) { progress = p * share1; return !threadShouldExit(); });
    if (!ok || threadShouldExit()) { error = "the export was cancelled or there was nothing to render"; return; }
    juce::String rep;
    if (!Renderer::write(mix, rate, opt, target, rep)) { error = rep; return; }
    written.add(target);
    juce::String out = juce::String::fromUTF8("✓ ") + target.getFileName() + "   " + rep;
    if (opt.stems) {
        auto dir = target.getParentDirectory().getChildFile(target.getFileNameWithoutExtension() + " stems");
        dir.createDirectory();
        ExportOptions so = opt; so.loudness = 0;                // stems keep their balance: no levelling
        int count = 0;
        if (opt.stemsDry) {
            juce::OwnedArray<juce::AudioBuffer<float>> st; juce::StringArray nm; double r2 = 0;
            for (int s = 1; s <= 4; ++s) {
                if (!(opt.what == ExportOptions::AllScenes || s == snap.sceneNow)) continue;
                st.clear(); nm.clear();
                if (!rd.dryStems(s, st, nm, r2)) continue;
                for (int i = 0; i < st.size(); ++i) {
                    juce::String r3; auto f = dir.getChildFile(juce::String::charToString((juce::juce_wchar) ('A' + s - 1)) + " - " + nm[i] + (opt.flac ? ".flac" : ".wav"));
                    if (Renderer::write(*st[i], r2, so, f, r3, false)) { written.add(f); ++count; }
                }
            }
        } else {
            for (int k = 0; k < stemStrips.size(); ++k) {
                juce::AudioBuffer<float> sb; double r2 = 0;
                float base = share1 * (1 + k);
                if (!rd.renderMix(snap, opt, sb, r2, stemStrips[k], [this, base, share1](float p) { progress = base + p * share1; return !threadShouldExit(); })) continue;
                juce::String r3; auto f = dir.getChildFile("Track " + juce::String(stemStrips[k] - 3) + (opt.flac ? ".flac" : ".wav"));
                if (Renderer::write(sb, r2, so, f, r3, false)) { written.add(f); ++count; }
            }
        }
        out << "\n+ " << count << " stems in \"" << dir.getFileName() << "\"";
    }
    progress = 1.0f;
    report = out;
}

void ExportSheet::finished() {
    busy = false;
    share.setVisible(!written.isEmpty());
    repaint();
}

// ================================================================== IMPORT: keep, or match the piece
ImportSheet::ImportSheet(Canvas& c) : Sheet(c, "IMPORT", { 320, 110, 800, 660 }), juce::Thread("ARCO import") {
    addAndMakeVisible(go); addAndMakeVisible(cancel);
    go.setBounds(box.getRight() - 224, box.getBottom() - 66, 200, 46);
    cancel.setBounds(box.getRight() - 340, box.getBottom() - 60, 104, 34);
    go.onClick = [this] { doImport(); };
    cancel.onClick = [this] { if (isThreadRunning()) signalThreadShouldExit(); else dismiss(); };
    bpmBox = std::make_unique<ValueBox>([this] { return plan.fileBpm; }, [this](double v) { plan.fileBpm = v; replan(); }, 40, 240, 1, [] { return Theme::red(); });
    bpmBox->fsize = 11.0f;
    addAndMakeVisible(*bpmBox); bpmBox->setBounds(box.getX() + 470, box.getY() + 166, 74, 26);
    for (auto* p : { &half, &dbl }) addAndMakeVisible(p);
    half.setBounds(box.getX() + 552, box.getY() + 166, 70, 26); dbl.setBounds(box.getX() + 628, box.getY() + 166, 70, 26);
    half.onClick = [this] { if (plan.fileBpm > 80) { plan.fileBpm *= 0.5; replan(); } };
    dbl.onClick = [this] { if (plan.fileBpm > 0 && plan.fileBpm < 120) { plan.fileBpm *= 2.0; replan(); } };
    keepBox = { box.getX() + 24, box.getY() + 260, 364, 236 };
    matchBox = { box.getX() + 412, box.getY() + 260, 364, 236 };
    addAndMakeVisible(tempoT); addAndMakeVisible(keyT);
    tempoT.setBounds(matchBox.getX() + 18, matchBox.getBottom() - 44, 90, 28);
    keyT.setBounds(matchBox.getX() + 116, matchBox.getBottom() - 44, 90, 28);
    tempoT.onClick = [this] { plan.matchTempo = !plan.matchTempo; match = true; replan(); };
    keyT.onClick = [this] { plan.matchKey = !plan.matchKey; match = true; replan(); };
    startTimerHz(15);
}
ImportSheet::~ImportSheet() { stopThread(8000); }

void ImportSheet::open(const juce::File& f, int t) {
    if (isThreadRunning()) return;
    file = f; track = juce::jlimit(1, 8, t);
    title = "IMPORT INTO TRACK " + juce::String(track);
    subtitle = f.getFileName();
    plan = {}; error = {}; result = {}; thumb.clear();
    piece = cv.host.pieceInfo(false);
    match = piece.hasLoops;                           // the first file of a piece sets its tempo: keep it as it is
    stage = Listening; wantRender = false; progress = 0; jobDone = false;
    setVisible(true); toFront(true);
    startThread();
}

void ImportSheet::run() {
    if (wantRender) {
        result = renderImport(plan, piece, [this](float p) { progress = p; return !threadShouldExit(); });
        jobDone = true; return;
    }
    juce::String err;
    ImportPlan p;
    if (!prepareImport(file, cv.engine().sr, p, cv.host.formatManager(), err)) { error = err; jobDone = true; return; }
    cv.host.listenKey(piece);
    // a thumbnail of the file
    std::vector<float> th(600, 0.0f);
    int n = p.audio.getNumSamples();
    for (int i = 0; i < n; ++i) { int k = (int) ((long) i * 600 / n); th[(size_t) k] = juce::jmax(th[(size_t) k], std::abs(p.audio.getSample(0, i))); }
    plan = std::move(p); thumb = std::move(th);
    jobDone = true;
}

void ImportSheet::timerCallback() {
    if (!isVisible()) return;
    if (jobDone && !isThreadRunning()) {
        jobDone = false;
        if (error.isNotEmpty()) stage = Failed;
        else if (stage == Listening) { stage = Ready; replan(); }
        else if (stage == Rendering) {
            if (result.getNumSamples() > 0 && !threadShouldExit()) {
                cv.host.importBuffer(result, track, file.getFileName());
                setVisible(false); plan = {}; result = {};
                stage = Idle; return;
            }
            stage = Ready;
        }
    }
    bool ready = stage == Ready;
    go.setVisible(ready); bpmBox->setVisible(ready && plan.fileBpm > 0); half.setVisible(ready && plan.fileBpm > 0); dbl.setVisible(ready && plan.fileBpm > 0);
    tempoT.setVisible(ready); keyT.setVisible(ready);
    cancel.text = stage == Rendering ? "STOP" : "CANCEL";
    repaint();
}

void ImportSheet::replan() { planImport(plan, piece); repaint(); }

void ImportSheet::mouseDown(const juce::MouseEvent& e) {
    auto p = e.getPosition();
    if (stage == Ready && keepBox.contains(p)) { match = false; repaint(); return; }
    if (stage == Ready && matchBox.contains(p)) { match = true; repaint(); return; }
    Sheet::mouseDown(e);
}

void ImportSheet::doImport() {
    if (stage != Ready) return;
    if (!match || (!plan.matchTempo && (plan.semitones == 0 || !plan.matchKey))) {
        // as it is: the looper reads the file itself
        cv.host.importFile(file, track);
        setVisible(false); plan = {}; stage = Idle; return;
    }
    stage = Rendering; wantRender = true; progress = 0.02f; jobDone = false;
    startThread();
}

void ImportSheet::paintContent(juce::Graphics& g) {
    auto bx = box.getX(), by = box.getY();
    // the file
    auto wr = juce::Rectangle<float>((float) bx + 24, (float) by + 80, (float) box.getWidth() - 48, 70);
    g.setColour(Theme::scr()); g.fillRoundedRectangle(wr, 8.0f);
    if (stage != Listening && !thumb.empty()) {
        juce::Path p; float cy = wr.getCentreY(), h = wr.getHeight() * 0.44f; int N = (int) thumb.size();
        float mx = 1e-6f; for (float v : thumb) mx = juce::jmax(mx, v);
        p.startNewSubPath(wr.getX() + 6, cy);
        for (int i = 0; i < N; ++i) p.lineTo(wr.getX() + 6 + (wr.getWidth() - 12) * i / (N - 1), cy - thumb[(size_t) i] / mx * h);
        for (int i = N - 1; i >= 0; --i) p.lineTo(wr.getX() + 6 + (wr.getWidth() - 12) * i / (N - 1), cy + thumb[(size_t) i] / mx * h);
        p.closeSubPath();
        g.setColour(Theme::arc(Theme::blue())); g.fillPath(p);
        // where it will start (the first bar line)
        if (plan.audio.getNumSamples() > 0 && plan.fileBpm > 0) {
            double barN = plan.audio.getNumSamples(), beat = 60.0 * cv.engine().sr / plan.fileBpm;
            g.setColour(Theme::txt().withAlpha(0.25f));
            for (double x = plan.tempo.downbeatOffset; x < barN; x += beat * piece.beats)
                g.fillRect((float) (wr.getX() + 6 + (wr.getWidth() - 12) * x / barN), wr.getY() + 4, 1.0f, wr.getHeight() - 8);
        }
    }
    if (stage == Listening) { text(g, juce::String::fromUTF8("listening to the file: tempo, beat, key …"), 24, 176, 600, 10.0f, Theme::amber(), 1, 1); return; }
    if (stage == Failed) { text(g, error, 24, 176, 740, 10.0f, Theme::red(), 1, 2); return; }
    auto pieceKey = piece.key.tonic >= 0 ? piece.key.name() : juce::String("no key yet");
    // what was heard
    text(g, "THIS FILE", 24, 170, 120, 9.0f, Theme::sub(), 2, 1);
    juce::String fl = plan.fileBpm > 0 ? juce::String(plan.fileBpm, 1) + " BPM" : juce::String("no clear beat");
    fl << juce::String::fromUTF8("  ·  ") << plan.key.name();
    int secs = (int) std::round(plan.seconds);
    fl << juce::String::fromUTF8("  ·  ") << secs / 60 << ":" << juce::String(secs % 60).paddedLeft('0', 2);
    if (plan.tempo.wholeBars && plan.tempo.bars > 0) fl << juce::String::fromUTF8("  ·  a ") << plan.tempo.bars << "-bar loop";
    text(g, fl, 130, 168, 330, 10.5f, Theme::txt(), 2, 1);
    if (plan.fileBpm > 0) text(g, "not right?", 392, 172, 80, 7.5f, Theme::sub(), 0, 1);
    text(g, "THE PIECE", 24, 214, 120, 9.0f, Theme::sub(), 2, 1);
    juce::String pl = juce::String(piece.bpm, 1) + " BPM  ·  " + juce::String(piece.beats) + "/4  ·  " + pieceKey;
    if (piece.loopLen > 0) pl << juce::String::fromUTF8("  ·  this scene loops ") << (int) std::round(piece.loopLen / (piece.beatLen * piece.beats)) << " bars";
    else if (!piece.hasLoops) pl << juce::String::fromUTF8("  ·  nothing recorded yet");
    text(g, pl, 130, 212, 640, 10.5f, Theme::txt(), 2, 1);
    // the two choices
    auto card = [&](juce::Rectangle<int> r, bool on, const juce::String& head, const juce::StringArray& lines) {
        g.setColour(on ? Theme::sel().withAlpha(Theme::lightMode() ? 0.35f : 0.45f) : Theme::card()); g.fillRoundedRectangle(r.toFloat(), 14.0f);
        g.setColour(on ? Theme::ink(Theme::sel()) : Theme::line()); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 14.0f, on ? 2.2f : 1.0f);
        g.setColour(Theme::ink(Theme::txt())); g.setFont(Theme::font(13.0f, 2));
        g.drawText((on ? juce::String::fromUTF8("●  ") : juce::String::fromUTF8("○  ")) + head, r.getX() + 18, r.getY() + 14, r.getWidth() - 30, 24, juce::Justification::centredLeft);
        int y = r.getY() + 50;
        for (auto& l : lines) { g.setColour(Theme::ink(Theme::sub())); g.setFont(Theme::font(9.0f, 0)); g.drawFittedText(l, r.getX() + 18, y, r.getWidth() - 34, 30, juce::Justification::topLeft, 2, 0.9f); y += 30; }
    };
    juce::StringArray keep { juce::String::fromUTF8("plays exactly as recorded - its own tempo and key") };
    if (!piece.hasLoops) keep.add(juce::String::fromUTF8("the first file sets the tempo of the piece") + (plan.fileBpm > 0 ? " (about " + juce::String((int) std::round(plan.fileBpm)) + " BPM)" : juce::String()));
    else keep.add(juce::String::fromUTF8("cut or filled to the loop of this scene"));
    keep.add(juce::String::fromUTF8("best for sounds without a beat: drones, textures, voices"));
    card(keepBox, !match, "KEEP AS IT IS", keep);
    juce::StringArray ml;
    if (plan.fileBpm <= 0) ml.add(juce::String::fromUTF8("no clear beat in the file: the tempo stays"));
    else if (plan.matchTempo) ml.add("tempo  " + juce::String(plan.fileBpm, 1) + juce::String::fromUTF8(" → ") + juce::String(piece.bpm, 1) + " BPM  (" + juce::String((int) std::round(plan.fileBpm / piece.bpm * 100)) + " % speed)"
                                  + (plan.tempo.downbeatOffset > cv.engine().sr * 0.02 ? juce::String::fromUTF8(", from its first bar line") : juce::String()));
    else ml.add("tempo: as it is (TEMPO is off)");
    if (!plan.matchKey) ml.add("key: as it is (KEY is off)");
    else if (plan.key.tonic < 0) ml.add(juce::String::fromUTF8("no clear key in the file: the pitch stays"));
    else if (piece.key.tonic < 0) ml.add(juce::String::fromUTF8("the piece has no key yet - record something first"));
    else if (plan.semitones == 0) ml.add("already in the key of the piece (" + pieceKey + ")");
    else ml.add(juce::String(plan.semitones > 0 ? "+" : "") + juce::String(plan.semitones) + (std::abs(plan.semitones) == 1 ? " semitone" : " semitones") + juce::String::fromUTF8(" → ") + pieceKey);
    ml.add(juce::String::fromUTF8("length: ") + plan.fitText(piece));
    card(matchBox, match, "MATCH THE PIECE", ml);
    if (stage == Rendering || progress > 0.01f) {
        auto bar = juce::Rectangle<float>((float) bx + 24, (float) box.getBottom() - 100, (float) box.getWidth() - 48, 8);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(bar, 4);
        g.setColour(Theme::green()); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())), 4);
    }
    text(g, stage == Rendering ? juce::String::fromUTF8("stretching and tuning at full quality …") : juce::String::fromUTF8("drop more files on any track - this window opens each time"),
         24, box.getHeight() - 60, 420, 8.5f, Theme::sub(), 0, 2);
}

// ================================================================== AUTO MIX
AutoMixSheet::AutoMixSheet(Canvas& c) : Sheet(c, "AUTO MIX", { 170, 40, 1100, 800 }), juce::Thread("ARCO auto mix") {
    subtitle = juce::String::fromUTF8("ARCO listens to every track, understands what it is, and mixes the piece - levels, EQ, compression, panning, effects in tempo, timing, master. Compare before / after at any time.");
    struct T { const char* name; bool* v; };
    T ts[7] = { { "LEVELS", &opt.levels }, { "EQ", &opt.eq }, { "COMPRESSION", &opt.dynamics }, { "PANNING", &opt.pan }, { "EFFECTS IN TEMPO", &opt.effects }, { "TIMING", &opt.timing }, { "MASTER", &opt.master } };
    for (int i = 0; i < 7; ++i) {
        bool* v = ts[i].v;
        auto* p = new Pill(ts[i].name, [v] { return *v ? Theme::green() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [this, v] { *v = !*v; repaint(); };
        toggles.add(p); addAndMakeVisible(p);
        p->setBounds(box.getX() + 24 + (i % 4) * 150, box.getY() + 110 + (i / 4) * 34, 142, 28);
    }
    choices(24, 200, { "NATURAL", "BALANCED", "PUNCHY" }, 120, [this] { return opt.style; }, [this](int i) { opt.style = i; });
    choices(24, 262, { "OFF", "GENTLE", "HALF WAY", "TIGHT" }, 100, [this] { return opt.timing ? timingChoice : 0; },
            [this](int i) { timingChoice = i; opt.timing = i > 0; static const float A[4] = { 0, 0.3f, 0.5f, 0.8f }; opt.timingAmount = A[i]; });
    choices(24, 324, { "THIS SCENE", "ALL SCENES" }, 150, [this] { return opt.allScenes ? 1 : 0; }, [this](int i) { opt.allScenes = i == 1; });
    opt.timingAmount = 0.3f;
    addAndMakeVisible(go); go.setBounds(box.getX() + 680, box.getY() + 120, 380, 64);
    go.onClick = [this] { start(); };
    for (auto* p : { &before, &afterP, &keep, &undo }) addChildComponent(p);
    before.onClick = [this] { showAB(false); };
    afterP.onClick = [this] { showAB(true); };
    keep.onClick = [this] { setVisible(false); };
    undo.onClick = [this] { if (hasResult()) { automix::apply(cv.host, *result, false); result.reset(); report.r = nullptr; cv.showMessage("AUTO MIX undone"); repaint(); } };
    view.setViewedComponent(&report, false); view.setScrollBarsShown(true, false);
    addAndMakeVisible(view);
    startTimerHz(15);
}
AutoMixSheet::~AutoMixSheet() { stopThread(10000); }

void AutoMixSheet::resized() {
    Sheet::resized();
    before.setBounds(box.getX() + 680, box.getY() + 200, 120, 34);
    afterP.setBounds(box.getX() + 806, box.getY() + 200, 120, 34);
    undo.setBounds(box.getX() + 940, box.getY() + 200, 120, 34);
    keep.setBounds(box.getX() + 940, box.getY() + 244, 120, 34);
    view.setBounds(box.getX() + 24, box.getY() + 390, box.getWidth() - 48, box.getHeight() - 414);
    report.setSize(view.getWidth() - 14, juce::jmax(view.getHeight(), report.heightFor(view.getWidth() - 14)));
}

void AutoMixSheet::start() {
    if (busy) return;
    if (cv.engine().snap("smask").v.load() < 0.5f) { cv.showMessage("record some loops first"); return; }
    if (hasResult() && !after) automix::apply(cv.host, *result, true);   // start from the mix as it is now
    job = automix::prepare(cv.host, opt);
    busy = true; jobDone = false; progress = 0.01f;
    startThread();
}
void AutoMixSheet::run() {
    auto r = std::make_unique<AutoMixResult>(automix::run(job, [this](float p) { progress = p; return !threadShouldExit(); }));
    pending = std::move(r);
    jobDone = true;
}
void AutoMixSheet::timerCallback() {
    if (jobDone && !isThreadRunning()) {
        jobDone = false; busy = false;
        if (pending && pending->ok) {
            result = std::move(pending);
            automix::apply(cv.host, *result, true); after = true;
            report.r = result.get();
            cv.showMessage(juce::String::fromUTF8("AUTO MIX ✓ ") + result->headline);
            resized();
        } else if (pending) { cv.showMessage(pending->error); report.r = pending.get(); }
    }
    bool has = hasResult();
    before.setVisible(has); afterP.setVisible(has); keep.setVisible(has); undo.setVisible(has);
    go.text = busy ? juce::String::fromUTF8("listening …") : (has ? "MIX AGAIN" : "MIX IT");
    if (isVisible()) repaint();
}
void AutoMixSheet::showAB(bool a) {
    if (!hasResult() || a == after) return;
    automix::apply(cv.host, *result, a); after = a; repaint();
}

void AutoMixSheet::paintContent(juce::Graphics& g) {
    text(g, "MAY CHANGE", 24, 88, 300, 8.5f, Theme::sub(), 2, 1);
    text(g, "CHARACTER", 24, 180, 300, 8.5f, Theme::sub(), 2, 1);
    text(g, juce::String::fromUTF8("TIMING - notes pulled towards the beat (the groove and swing stay)"), 24, 242, 600, 8.5f, Theme::sub(), 2, 1);
    text(g, "WHICH LOOPS", 24, 304, 300, 8.5f, Theme::sub(), 2, 1);
    if (busy || (progress > 0 && progress < 1)) {
        auto bar = juce::Rectangle<float>((float) box.getX() + 680, (float) box.getY() + 194, 380, 6);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(bar, 3);
        g.setColour(Theme::green()); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())), 3);
    }
    if (hasResult()) {
        text(g, result->headline, 680, 290, 400, 10.0f, Theme::txt(), 2, 2);
        text(g, after ? juce::String::fromUTF8("you hear the mix") : juce::String::fromUTF8("you hear it as it was before"), 680, 326, 400, 8.5f, after ? Theme::green() : Theme::amber(), 1, 1);
    }
    g.setColour(Theme::card()); g.fillRoundedRectangle(view.getBounds().toFloat().expanded(8), 10.0f);
    if (report.r == nullptr) text(g, juce::String::fromUTF8("What AUTO MIX did will be listed here, part by part."), 40, 404, 700, 9.0f, Theme::sub(), 0, 1);
}

int AutoMixSheet::Report::heightFor(int w) const {
    if (r == nullptr) return 0;
    int h = 10;
    for (auto& l : r->report) { juce::TextLayout tl; juce::AttributedString a; a.append(l.what, Theme::font(9.0f, 0)); a.setWordWrap(juce::AttributedString::byWord); tl.createLayout(a, (float) w - 260); h += juce::jmax(34, (int) tl.getHeight() + 18); }
    return h;
}
void AutoMixSheet::Report::paint(juce::Graphics& g) {
    if (r == nullptr) return;
    if (!r->ok && r->error.isNotEmpty()) { g.setColour(Theme::ink(Theme::amber())); g.setFont(Theme::font(10.0f, 1)); g.drawText(r->error, getLocalBounds().reduced(16, 10).withHeight(24), juce::Justification::centredLeft); return; }
    int y = 10, w = getWidth();
    for (auto& l : r->report) {
        juce::TextLayout tl; juce::AttributedString a; a.append(l.what, Theme::font(9.0f, 0), Theme::ink(Theme::txt())); a.setWordWrap(juce::AttributedString::byWord);
        tl.createLayout(a, (float) w - 260);
        int h = juce::jmax(34, (int) tl.getHeight() + 18);
        auto col = l.strip >= 0 ? Theme::strip(l.strip) : Theme::blue();
        g.setColour(col); g.fillEllipse(14.0f, (float) y + 9, 9, 9);
        g.setColour(Theme::ink(Theme::txt())); g.setFont(Theme::font(9.5f, 2)); g.drawText(l.who, 32, y + 4, 200, 18, juce::Justification::centredLeft);
        g.setColour(Theme::ink(Theme::sub())); g.setFont(Theme::font(8.0f, 0)); g.drawText(l.role, 32, y + 20, 200, 14, juce::Justification::centredLeft);
        tl.draw(g, juce::Rectangle<float>(240.0f, (float) y + 6, (float) w - 260, (float) h));
        y += h;
        g.setColour(Theme::line()); g.fillRect(14, y - 2, w - 28, 1);
    }
}

// ================================================================== HELP
HelpSheet::HelpSheet(Canvas& c) : Sheet(c, "HELP", { 120, 40, 1200, 800 }) {
    subtitle = "everything ARCO does, in short";
    auto& S = helpSections();
    for (int i = 0; i < (int) S.size(); ++i) {
        auto* p = new Pill(juce::String::fromUTF8(S[(size_t) i].title), [this, i] { return current == i ? Theme::sel() : Theme::card(); }, 8.5f, 6.0f);
        p->onClick = [this, i] { show(i); };
        tabs.add(p); addAndMakeVisible(p);
    }
    view.setViewedComponent(&page, false);
    view.setScrollBarsShown(true, false);
    addAndMakeVisible(view);
    show(0);
}
void HelpSheet::resized() {
    Sheet::resized();
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setBounds(box.getX() + 24, box.getY() + 86 + i * 34, 220, 28);
    view.setBounds(box.getX() + 268, box.getY() + 86, box.getWidth() - 292, box.getHeight() - 110);
    page.setSize(view.getWidth() - 14, page.heightFor(view.getWidth() - 14));
}
void HelpSheet::show(int s) {
    auto& S = helpSections();
    current = juce::jlimit(0, (int) S.size() - 1, s);
    page.body = juce::String::fromUTF8(S[(size_t) current].body);
    page.setSize(juce::jmax(100, view.getWidth() - 14), page.heightFor(juce::jmax(100, view.getWidth() - 14)));
    view.setViewPosition(0, 0);
    repaint();
}
void HelpSheet::paintContent(juce::Graphics& g) {
    g.setColour(Theme::card()); g.fillRoundedRectangle(view.getBounds().toFloat().expanded(8), 10.0f);
}
static juce::AttributedString helpLayout(const juce::String& body) {
    juce::AttributedString a; a.setWordWrap(juce::AttributedString::byWord); a.setLineSpacing(4.0f);
    for (auto& line : juce::StringArray::fromLines(body)) {
        if (line.startsWith("# ")) a.append(line.substring(2) + "\n", Theme::font(13.0f, 2), Theme::ink(Theme::txt()));
        else if (line.startsWith("## ")) a.append("\n" + line.substring(3) + "\n", Theme::font(10.5f, 2), Theme::ink(Theme::txt()));
        else if (line.startsWith("- ")) a.append(juce::String::fromUTF8("•  ") + line.substring(2) + "\n", Theme::font(9.5f, 0), Theme::ink(Theme::txt()));
        else if (line.trim().isEmpty()) a.append("\n", Theme::font(6.0f, 0), Theme::txt());
        else a.append(line + "\n", Theme::font(9.5f, 0), Theme::ink(Theme::sub()));
    }
    return a;
}
int HelpSheet::Page::heightFor(int w) {
    juce::TextLayout tl; tl.createLayout(helpLayout(body), (float) w - 24);
    return (int) tl.getHeight() + 40;
}
void HelpSheet::Page::paint(juce::Graphics& g) {
    juce::TextLayout tl; tl.createLayout(helpLayout(body), (float) getWidth() - 24);
    tl.draw(g, getLocalBounds().toFloat().reduced(12, 14));
}

// ================================================================== ABOUT
AboutSheet::AboutSheet(Canvas& c) : Sheet(c, "ABOUT", { 420, 170, 600, 520 }) {}
void AboutSheet::paintContent(juce::Graphics& g) {
    auto img = juce::ImageCache::getFromMemory(BinaryData::icon_png, BinaryData::icon_pngSize);
    g.drawImage(img, juce::Rectangle<float>((float) box.getCentreX() - 80, (float) box.getY() + 70, 160, 160));
    auto c = [&](const juce::String& t, int y, float fs, juce::Colour col, int wt) {
        g.setColour(Theme::ink(col)); g.setFont(Theme::font(fs, wt));
        g.drawFittedText(t, { box.getX() + 20, box.getY() + y, box.getWidth() - 40, (int) (fs * 2.4f) }, juce::Justification::centred, 2); };
    c("ARCO", 250, 22.0f, Theme::txt(), 2);
    c("version " + juce::String(ProjectInfo::versionString) + (Updater::build() > 0 ? juce::String::fromUTF8(" · build ") + juce::String(Updater::build()) : juce::String()), 290, 9.5f, Theme::sub(), 0);
    c(juce::String::fromUTF8("a live looping station for instruments - loops, scenes, effects, a synth, a studio"), 330, 9.5f, Theme::txt(), 0);
    c(juce::String::fromUTF8("made for Paulo da Cunha Andrade"), 372, 9.0f, Theme::sub(), 1);
    c(juce::String::fromUTF8("built with JUCE · Inter typeface · Signalsmith Stretch"), 440, 8.0f, Theme::sub(), 0);
}

// ================================================================== MENUS
static juce::PopupMenu::Item item(int id, const juce::String& t, const juce::String& key = {}, bool on = true, bool ticked = false) {
    juce::PopupMenu::Item i(t); i.itemID = id; i.isEnabled = on; i.isTicked = ticked;
    if (key.isNotEmpty()) i.shortcutKeyDescription = key;
    return i;
}
#if JUCE_MAC
 #define CMDKEY juce::String::fromUTF8("⌘")
#else
 #define CMDKEY juce::String("Ctrl+")
#endif

juce::PopupMenu AppMenu::getMenuForIndex(int index, const juce::String&) {
    juce::PopupMenu m;
    auto& h = cv.host;
    bool loops = cv.engine().snap("smask").v.load() > 0.5f;
    if (index == 0) {
        m.addItem(item(cmdNewSong, "New Song", CMDKEY + "N"));
        m.addItem(item(cmdOpen, juce::String::fromUTF8("Open Song…"), CMDKEY + "O"));
        juce::PopupMenu rec;
        for (int i = 0; i < h.recentSongs.size(); ++i) rec.addItem(item(cmdRecent + i, juce::File(h.recentSongs[i]).getFileName()));
        m.addSubMenu("Open Recent", rec, h.recentSongs.size() > 0);
        m.addSeparator();
        m.addItem(item(cmdSave, "Save", CMDKEY + "S"));
        m.addItem(item(cmdSaveAs, juce::String::fromUTF8("Save As…"), juce::String::fromUTF8("⇧") + CMDKEY + "S"));
        m.addSeparator();
        juce::PopupMenu imp; for (int t = 0; t < 8; ++t) imp.addItem(item(cmdImport1 + t, "into Track " + juce::String(t + 1)));
        m.addSubMenu("Import Audio File", imp);
        m.addItem(item(cmdExport, juce::String::fromUTF8("Export Mix & Stems…"), CMDKEY + "E", loops));
        m.addItem(item(cmdRecordShow, h.isRecording() ? "Stop Recording the Show" : "Record the Show", CMDKEY + "R"));
        m.addSeparator();
        m.addItem(item(cmdStudio, juce::String::fromUTF8("Open in STUDIO…"), CMDKEY + "T", loops));
    } else if (index == 1) {
        m.addItem(item(cmdUndo, "Undo Last Recording", CMDKEY + "Z", cv.snap("undoav") > 0.5f));
        m.addItem(item(cmdResetAll, juce::String::fromUTF8("Clear All Loops…"), {}, loops));
        m.addSeparator();
        m.addItem(item(cmdAutoMix, juce::String::fromUTF8("AUTO MIX…"), juce::String::fromUTF8("⇧") + CMDKEY + "M", loops));
        m.addItem(item(cmdAB, "Compare Before / After AUTO MIX", {}, cv.autoMixAvailable()));
        m.addSeparator();
        m.addItem(item(cmdMidiLearn, cv.midiLearnActive() ? "Stop MIDI Learn" : juce::String::fromUTF8("MIDI Learn…"), CMDKEY + "L", true, cv.midiLearnActive()));
        m.addItem(item(cmdMidiClear, "Forget All MIDI Connections"));
    } else if (index == 2) {
        auto& P = Theme::palettes();
        juce::PopupMenu pal;
        for (int i = 0; i < (int) P.size(); ++i) pal.addItem(item(cmdPalette + i, juce::String(P[(size_t) i].name) + (P[(size_t) i].light ? " (light)" : " (dark)"), {}, true, i == Theme::paletteIndex()));
        m.addSubMenu("Colours", pal);
        m.addItem(item(cmdToggleLight, Theme::lightMode() ? "Dark Mode" : "Light Mode", juce::String::fromUTF8("⇧") + CMDKEY + "L"));
        m.addItem(item(cmdLook, juce::String::fromUTF8("Choose Effects…")));
        m.addSeparator();
        m.addItem(item(cmdSynth, "SYNTH", "K"));
        m.addItem(item(cmdSpatial, juce::String::fromUTF8("Speakers & Space (Ambisonics)…")));
        m.addItem(item(cmdMicView, juce::String::fromUTF8("Use This Device as a Phone (mic + remote)")));
        m.addItem(item(cmdSetup, juce::String::fromUTF8("Settings…"), CMDKEY + ","));
       #if ! JUCE_IOS
        m.addSeparator();
        m.addItem(item(cmdFullScreen, "Full Screen", juce::String::fromUTF8("⌃") + CMDKEY + "F"));
        m.addItem(item(cmdMinimise, "Minimise", CMDKEY + "M"));
       #endif
    } else {
        m.addItem(item(cmdHelp, "ARCO Help", "F1"));
        m.addItem(item(cmdShortcuts, "Keys, Pedals & MIDI"));
        m.addItem(item(cmdWhatsNew, "What's New in 1.2"));
        m.addSeparator();
       #if ! JUCE_IOS
        m.addItem(item(cmdUpdates, juce::String::fromUTF8("Check for Updates…")));
       #endif
        m.addItem(item(cmdAbout, "About ARCO"));
    }
    return m;
}

juce::PopupMenu AppMenu::everything() {
    juce::PopupMenu m; auto names = getMenuBarNames();
    for (int i = 0; i < names.size(); ++i) m.addSubMenu(names[i], getMenuForIndex(i, names[i]));
    return m;
}

juce::PopupMenu AppMenu::appleExtras() {
    juce::PopupMenu m;
    m.addItem(item(cmdAbout, "About ARCO"));
    m.addItem(item(cmdUpdates, juce::String::fromUTF8("Check for Updates…")));
    m.addSeparator();
    m.addItem(item(cmdSetup, juce::String::fromUTF8("Settings…"), CMDKEY + ","));
    return m;
}

void AppMenu::menuItemSelected(int id, int) { cv.command(id); }

} // namespace arco
