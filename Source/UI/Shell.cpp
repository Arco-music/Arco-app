#include "Shell.h"
#include "ShellViews.h"
#include "StudioView.h"
#include "SynthPanel.h"
#include "Panels.h"
#include "Sheets.h"

namespace arco {

static void say(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}

const char* screenTitle(Screen s) {
    switch (s) {
        case Screen::Home: return "ARCO"; case Screen::Workstation: return "WORKSTATION"; case Screen::Loops: return "LIVE LOOPS";
        case Screen::Studio: return "STUDIO"; case Screen::Drums: return "DRUM MACHINE"; case Screen::Synth: return "SYNTH";
        case Screen::Keys: return "KEYBOARD"; case Screen::Phrase: return "PHRASE"; case Screen::Modular: return "MODULAR";
        case Screen::Speakers: return "SPEAKERS"; default: return "";
    }
}

// ================================================================== KnobCell, Segments
KnobCell::KnobCell(const juce::String& n, ValueFn g, SetFn s, double lo, double hi, double def, ColourFn col, std::function<juce::String(double)> f)
    : knob(g, std::move(s), lo, hi, def, std::move(col)), name(n), fmt(std::move(f)), get(std::move(g)) {
    addAndMakeVisible(knob);
}
void KnobCell::resized() { int d = juce::jmin(getWidth() - 8, getHeight() - 30); knob.setBounds((getWidth() - d) / 2, 2, d, d); }
void KnobCell::paint(juce::Graphics& g) {
    int d = juce::jmin(getWidth() - 8, getHeight() - 30);
    say(g, name, { 0, d + 4, getWidth(), 13 }, 7.6f, Theme::sub(), 2, juce::Justification::centred);
    if (fmt) say(g, fmt(get()), { 0, d + 16, getWidth(), 12 }, 7.6f, Theme::txt(), 1, juce::Justification::centred);
}
Segments::Segments(juce::StringArray it, std::function<int()> g, std::function<void(int)> s, ColourFn c) : items(std::move(it)), get(std::move(g)), set(std::move(s)), col(std::move(c)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor); }
void Segments::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    if (Theme::softUI()) {          // 2.3: a groove in the skin; the raised capsule slides to the choice, the skin bulging round it as it goes
        softWell(g, r, r.getHeight() * 0.5f);
        int n = juce::jmax(1, items.size()), on = get ? get() : -1; float w = r.getWidth() / n;
        glide.slide = true; glide.onMove = [this] { repaint(); };
        if (on >= 0) glide.to((float) on);
        if (on >= 0) {
            float pos = glide.v, speed = std::abs(glide.vel);
            auto k = juce::Rectangle<float>(r.getX() + pos * w, r.getY(), w, r.getHeight()).reduced(3.0f);
            k = k.withSizeKeepingCentre(k.getWidth() * (1.0f + juce::jmin(0.25f, speed * 0.9f)), k.getHeight() * (1.0f - juce::jmin(0.10f, speed * 0.4f)));   // it stretches a little while it moves
            { juce::Graphics::ScopedSaveState ss(g); juce::Path cl; cl.addRoundedRectangle(r.expanded(12.0f), r.getHeight() * 0.5f + 12.0f); g.reduceClipRegion(cl);
              softRaised(g, k, k.getHeight() * 0.5f, juce::jmin(5.0f, k.getHeight() * 0.18f), Theme::btn()); }
        }
        for (int i = 0; i < n; ++i) {
            auto c = juce::Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
            float near = on >= 0 ? juce::jmax(0.0f, 1.0f - std::abs(glide.v - (float) i)) : 0.0f;
            auto cc = col ? col() : Theme::blue();
            float tw = juce::GlyphArrangement::getStringWidth(Theme::font(fsize, 2), items[i]);
            bool dot = c.getWidth() - 6 > tw + 22 && i == on;
            if (dot) softLed(g, { c.getCentreX() - tw * 0.5f - 6.0f, c.getCentreY() }, 2.4f, cc, near > 0.6f);
            say(g, items[i], c.toNearestInt().withTrimmedLeft(dot ? 8 : 0), fsize, Theme::sub().interpolatedWith(Theme::txt(), near), 2, juce::Justification::centred);
        }
        return;
    }
    if (Theme::glass()) { g.setColour(juce::Colours::black.withAlpha(0.30f)); g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
                          g.setColour(juce::Colours::white.withAlpha(0.07f)); g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f); }
    else { g.setColour(Theme::btn()); g.fillRoundedRectangle(r, juce::jmin(10.0f, r.getHeight() * 0.32f)); }
    int n = juce::jmax(1, items.size()), on = get ? get() : -1;
    float w = r.getWidth() / n;
    for (int i = 0; i < n; ++i) {
        auto c = juce::Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
        if (i == on) { auto cc = col ? col() : Theme::blue();
                       if (Theme::glass()) { auto k = c.reduced(3.0f); g.setColour(juce::Colours::white.withAlpha(0.16f)); g.fillRoundedRectangle(k, k.getHeight() * 0.5f);
                                             g.setColour(juce::Colours::white.withAlpha(0.22f)); g.drawRoundedRectangle(k.reduced(0.5f), k.getHeight() * 0.5f, 1.0f);
                                             g.setColour(cc); g.fillEllipse(k.getX() + 10, k.getCentreY() - 3, 6, 6);
                                             say(g, items[i], c.toNearestInt().withTrimmedLeft(10), fsize, Theme::txt(), 2, juce::Justification::centred); }
                       else { g.setColour(cc); g.fillRoundedRectangle(c.reduced(2.5f), juce::jmin(8.0f, c.getHeight() * 0.28f));
                              say(g, items[i], c.toNearestInt(), fsize, Theme::textOn(cc), 2, juce::Justification::centred); } }
        else { if (i > 0 && i - 1 != on && !Theme::glass()) { g.setColour(Theme::line()); g.fillRect(c.getX(), c.getY() + c.getHeight() * 0.25f, 1.0f, c.getHeight() * 0.5f); }
               say(g, items[i], c.toNearestInt(), fsize, Theme::sub(), 2, juce::Justification::centred); }
    }
}
void Segments::mouseDown(const juce::MouseEvent& e) {
    int n = juce::jmax(1, items.size()); int i = juce::jlimit(0, n - 1, (int) (e.position.x / (getWidth() / (float) n)));
    if (set) set(i); repaint();
}

// ================================================================== the shell
AppShell::AppShell(AudioHost& h) : host(h), canvas(h) {
    setOpaque(true);
    canvas.inShell = true;
    canvas.onOpenStudio = [this] { show(Screen::Studio); };
    addChildComponent(canvas);
    addAndMakeVisible(top);
    addAndMakeVisible(homeScreen);
    drums = std::make_unique<DrumView>(*this); addChildComponent(drums.get());
    phrase = std::make_unique<PhraseView>(*this); addChildComponent(phrase.get());
    loops = std::make_unique<LoopsView>(*this); addChildComponent(loops.get());
    canvas.layoutHeader();
    host.onSaveExtra = [this](const juce::File& f) { loops->save(f); };
    host.onOpenExtra = [this](const juce::File& f) { loops->load(f); };
    startTimerHz(15);
}
AppShell::~AppShell() { stopTimer(); host.onSaveExtra = nullptr; host.onOpenExtra = nullptr; }

StudioView* AppShell::studioView() { return canvas.studioView; }

void AppShell::setLayout(bool tall, int w, int h) {
    portrait = tall; W = w; H = h;
    top.setBounds(0, 0, W, TOPH);
    auto c = content();
    canvas.setLayout(tall, c.getWidth(), c.getHeight());
    canvas.layoutHeader(); canvas.layoutStrips(); canvas.layoutCards();
    canvas.setBounds(c);
    homeScreen.setBounds(0, 0, W, H);                // HOME has no bar on top: the whole screen
    for (juce::Component* v : { (juce::Component*) drums.get(), (juce::Component*) phrase.get(), (juce::Component*) loops.get() }) if (v) v->setBounds(c);
    if (auto* sv = studioView()) if (sv->getParentComponent() == this) sv->setBounds(c);
}

void AppShell::show(Screen s) {
    // close what the station had open over itself
    auto closeOverlays = [this] { canvas.openSynth(false); canvas.openKeyboard(false); canvas.openModular(false); canvas.openSpatial(false); };
    if (screen == Screen::Studio && s != Screen::Studio) { if (auto* sv = studioView()) sv->setVisible(false); }
    screen = s;
    homeScreen.setVisible(s == Screen::Home);
    top.setVisible(s != Screen::Home);
    drums->setVisible(s == Screen::Drums);
    phrase->setVisible(s == Screen::Phrase);
    loops->setVisible(s == Screen::Loops);
    bool station = s == Screen::Workstation || s == Screen::Synth || s == Screen::Keys || s == Screen::Modular || s == Screen::Speakers;
    canvas.setVisible(station);
    canvas.showStation(s == Screen::Workstation);
    closeOverlays();
    if (s == Screen::Synth) canvas.openSynth(true);
    if (s == Screen::Keys) canvas.openKeyboard(true);
    if (s == Screen::Modular) canvas.openModular(true);
    if (s == Screen::Speakers) canvas.openSpatial(true);
    if (s == Screen::Studio) {
        if (!canvas.studioWindow) {
            auto* v = new StudioView(canvas); canvas.studioView = v; canvas.studioWindow.reset(v);
            v->setEmbedded(true);
            v->onClose = [this] { show(Screen::Home); };
            addChildComponent(v);
        }
        canvas.studioView->setBounds(content());
        canvas.studioView->setVisible(true); canvas.studioView->toFront(true);
        canvas.studioView->open();
    }
    if (s == Screen::Home) homeScreen.grabKeyboardFocus();
    if (s == Screen::Drums) drums->grabKeyboardFocus();
    top.repaint(); repaint();
}

void AppShell::timerCallback() {
    // a window of the station closed by its own button: back home
    if (screen == Screen::Synth && !canvas.synthOpen()) show(Screen::Home);
    else if (screen == Screen::Keys && !canvas.keyboardOpen()) show(Screen::Home);
    else if (screen == Screen::Modular && !canvas.modularOpen()) show(Screen::Home);
    else if (screen == Screen::Speakers && !canvas.spatialOpen()) show(Screen::Home);
    else if (screen == Screen::Workstation) {     // ... or opened from inside the station (a key, a menu)
        if (canvas.synthOpen()) screen = Screen::Synth; else if (canvas.keyboardOpen()) screen = Screen::Keys;
        else if (canvas.modularOpen()) screen = Screen::Modular; else if (canvas.spatialOpen()) screen = Screen::Speakers;
    }
}

// ---- the transport of the open screen
void AppShell::transportPlay() {
    auto& m = host.model;
    switch (screen) {
        case Screen::Studio: if (auto* sv = studioView()) sv->togglePlay(); break;
        case Screen::Drums: {
            bool stationRuns = (int) host.engine.snap("transport").v.load() == 1;
            if (stationRuns) m.press("st_looper", "stopall"); else m.set("dm_run", m.get("dm_run") > 0.5 ? 0 : 1);
            break; }
        case Screen::Phrase: m.press("st_phr", "phr_play"); break;
        case Screen::Loops: loops->togglePlay(); break;
        default: {
            int t = (int) host.engine.snap("transport").v.load();
            m.press("st_looper", t == 1 || t == 2 ? "stopall" : "playall");
            if (t != 1 && t != 2 && host.engine.snap("smask").v.load() < 0.5f) {      // nothing recorded yet: the clock still starts (drums, loops, phrase)
                host.engine.clock.restart = true; }
        }
    }
}
void AppShell::transportStop() {
    auto& m = host.model;
    if (screen == Screen::Studio) { host.studio.stop(); return; }
    m.press("st_looper", "stopall"); m.set("dm_run", 0);
    if (loops) loops->stopAll();
}
void AppShell::transportRewind() {
    if (screen == Screen::Studio) { if (auto* sv = studioView()) sv->rewind(); return; }
    host.engine.clock.restart = true;
}
void AppShell::transportRecord() {
    auto& m = host.model;
    switch (screen) {
        case Screen::Studio: if (auto* sv = studioView()) sv->toggleRecord(); break;
        case Screen::Drums: m.set("dm_rec", m.get("dm_rec") > 0.5 ? 0 : 1); break;
        case Screen::Phrase: m.press("st_phr", "phr_rec"); break;
        case Screen::Loops: loops->recordSelected(); break;
        default: host.doAction(AudioHost::ActRec);
    }
}
bool AppShell::isPlaying() {
    switch (screen) {
        case Screen::Studio: return host.studio.playing.load();
        case Screen::Drums: return host.model.get("dm_run") > 0.5 || (int) host.engine.snap("transport").v.load() == 1;
        case Screen::Phrase: return host.engine.phrase.isPlaying.load();
        case Screen::Loops: return loops->anyPlaying();
        default: { int t = (int) host.engine.snap("transport").v.load(); return t == 1 || t == 2; }
    }
}
bool AppShell::isRecording() {
    switch (screen) {
        case Screen::Studio: return host.studio.recording.load() || host.studio.countInLeft.load() > 0;
        case Screen::Drums: return host.model.get("dm_rec") > 0.5;
        case Screen::Phrase: { int s = host.engine.phrase.stateOut.load(); return s == 1 || s == 2; }
        case Screen::Loops: return loops->anyRecording();
        default: for (int t = 0; t < 8; ++t) { int a = (int) host.engine.snap(t < 4 ? "statA" : "statB").v.load(); int st = (a >> (4 * (t % 4))) & 15; if (st == 2 || st == 7) return true; }
                 return false;
    }
}
juce::String AppShell::positionText() {
    if (screen == Screen::Studio) {
        auto& S = host.studio.song; double beats = host.studio.pos.load() / S.beatLen();
        return juce::String((int) std::floor(beats / S.beats) + 1) + "." + juce::String((int) std::floor(std::fmod(beats, (double) S.beats)) + 1);
    }
    int t = (int) host.engine.snap("transport").v.load();
    if (screen == Screen::Workstation && (t == 1 || t == 2))
        return juce::String((int) host.engine.snap("barno").v.load()) + "." + juce::String((int) host.engine.snap("beatno").v.load());
    double b = host.engine.clock.beatNow.load(); int bpb = beatsPerBar();
    return juce::String((int) std::floor(b / bpb) % 999 + 1) + "." + juce::String((int) std::floor(std::fmod(b, (double) bpb)) + 1);
}
double AppShell::tempo() {
    if (screen == Screen::Studio) return host.studio.song.bpm;
    return host.engine.snap("smask").v.load() > 0 ? std::round(host.engine.snap("ebpm").v.load() * 10) / 10 : host.model.get("bpm");
}
void AppShell::setTempo(double b) {
    b = juce::jlimit(30.0, 300.0, std::round(b * 10) / 10);
    if (screen == Screen::Studio) { const juce::ScopedLock sl(host.studio.lock); host.studio.song.bpm = b; return; }
    host.model.set("bpm", b);
    if (host.studio.song.clips.empty()) host.studio.song.bpm = b;          // an empty STUDIO keeps the station's tempo
}
int AppShell::beatsPerBar() { return screen == Screen::Studio ? host.studio.song.beats : (int) host.model.get("beats"); }
void AppShell::setBeatsPerBar(int b) { if (screen == Screen::Studio) host.studio.song.beats = juce::jlimit(1, 16, b); else host.model.set("beats", juce::jlimit(1, 16, b)); }
bool AppShell::clickOn() { return screen == Screen::Studio ? host.studio.song.metronome : host.model.get("clickall") > 0.5; }
void AppShell::toggleClick() { if (screen == Screen::Studio) host.studio.song.metronome = !host.studio.song.metronome; else host.model.set("clickall", clickOn() ? 0 : 1); }
bool AppShell::countOn() { return screen == Screen::Studio ? host.studio.song.countIn : host.model.get("countin") > 0.5; }
void AppShell::toggleCount() { if (screen == Screen::Studio) host.studio.song.countIn = !host.studio.song.countIn; else host.model.set("countin", countOn() ? 0 : 1); }
void AppShell::undo() {
    if (screen == Screen::Studio) { host.studio.undo(); if (auto* sv = studioView()) sv->repaint(); return; }
    if (screen == Screen::Drums) { drums->undo(); return; }
    host.doAction(AudioHost::ActUndo);
}

// ================================================================== the top bar
TopBar::TopBar(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    auto mk = [&](const juce::String& t, ColourFn c, std::function<void()> f, int icon, float fs = 10.0f) {
        auto* p = new Pill(t, std::move(c), fs, 12.0f); p->onClick = std::move(f); p->icon = icon; owned.add(p); addAndMakeVisible(p); return p; };
    home = mk("", [this] { return shell.screen == Screen::Home ? Theme::blue() : Theme::btn(); }, [this] { shell.show(Screen::Home); }, 10);
    home->tint = true; home->onFn = [this] { return shell.screen == Screen::Home; };
    home->setTooltip("HOME: everything ARCO plays");
    rew = mk("", [] { return Theme::btn(); }, [this] { shell.transportRewind(); }, 11, 13.0f);
    rew->textColour = [] { return Theme::txt(); };
    play = mk("", [this] { return shell.isPlaying() ? Theme::green() : Theme::btn(); }, [this] { shell.transportPlay(); }, 1, 14.0f);
    play->textColour = [this] { return shell.isPlaying() ? Theme::textOn(Theme::green()) : Theme::txt(); };
    play->setComponentID("shell:play");
    rec = mk("", [this] { return shell.isRecording() ? Theme::red() : Theme::btn(); }, [this] { shell.transportRecord(); }, 3, 14.0f);
    rec->textColour = [this] { return shell.isRecording() ? Theme::textOn(Theme::red()) : Theme::ink(Theme::red()); };
    rec->setComponentID("shell:rec");
    click = mk("CLICK", [this] { return shell.clickOn() ? Theme::amber() : Theme::btn(); }, [this] { shell.toggleClick(); }, 0, 8.5f);
    click->tint = true;
    count = mk("COUNT-IN", [this] { return shell.countOn() ? Theme::amber() : Theme::btn(); }, [this] { shell.toggleCount(); }, 0, 8.5f);
    count->tint = true;
    undo = mk("", [] { return Theme::btn(); }, [this] { shell.undo(); }, 4, 10.0f);
    undo->textColour = [] { return Theme::txt(); }; undo->setTooltip("undo");
    save = mk("SAVE", [] { return Theme::btn(); }, [this] { shell.canvas.doSave(false); }, 0, 8.5f);
    save->textColour = [] { return Theme::txt(); };
    setup = mk("SETUP", [] { return Theme::btn(); }, [this] { shell.show(Screen::Workstation); shell.canvas.openSetup(true); }, 0, 8.5f);
    setup->textColour = [] { return Theme::txt(); };
    look = mk("", [] { return Theme::btn(); }, [this] { shell.canvas.command(cmdToggleLight); }, 12, 10.0f);
    look->textColour = [] { return Theme::txt(); }; look->setTooltip("light / dark"); look->setComponentID("shell:look");
    // 2.1 SOFT: words as in the design (Click, Count-in, Save, Setup); play lights up instead of turning green
    click->dynText = [] { return juce::String(Theme::softUI() ? "Click" : "CLICK"); };
    count->dynText = [] { return juce::String(Theme::softUI() ? "Count-in" : "COUNT-IN"); };
    save->dynText = [] { return juce::String(Theme::softUI() ? "Save" : "SAVE"); };
    setup->dynText = [] { return juce::String(Theme::softUI() ? "Setup" : "SETUP"); };
    for (auto* p : { click, count, save, setup }) { p->fsize = 10.5f; }
    play->onFn = [this] { return shell.isPlaying(); };
    play->textColour = [this] { return shell.isPlaying() ? (Theme::softUI() ? Theme::ink(Theme::blue()) : Theme::textOn(Theme::green())) : Theme::txt(); };
    play->bg = [this] { return shell.isPlaying() ? (Theme::softUI() ? Theme::blue() : Theme::green()) : Theme::btn(); };
    play->tint = true; play->solidFn = [] { return !Theme::softUI(); };
    startTimerHz(20);
}

void TopBar::resized() {
    int h = getHeight(), y = 10, bh = h - 20;
    home->setBounds(14, y, 52, bh);
    titleR = { 78, 6, 210, h - 12 };
    if (shell.portrait) {          // an iPad upright: the same bar, tighter
        titleR = { 74, 6, 170, h - 12 };
        int x = 250;
        rew->setBounds(x, y, 40, bh); play->setBounds(x + 44, y, 56, bh); rec->setBounds(x + 104, y, 56, bh); x += 172;
        posR = { x, y, 100, bh }; x += 106; bpmR = { x, y, 104, bh }; x += 110; sigR = { x, y, 52, bh }; x += 58;
        click->setBounds(x, y, 64, bh); count->setVisible(false); meterR = {};
        int r = getWidth() - 12;
        r -= 62; setup->setBounds(r, y, 62, bh); r -= 6; r -= 56; save->setBounds(r, y, 56, bh); r -= 6; r -= 42; undo->setBounds(r, y, 42, bh);
        r -= 6; r -= 42; look->setBounds(r, y, 42, bh);
        return;
    }
    if (Theme::softUI()) {         // 2.1: round keys, one display (bar · bpm · 4/4 · the beat lights), the words
        titleR = { 74, 8, 230, h - 16 };
        int bs = juce::jmin(bh, 46), by = (h - bs) / 2;
        int cx2 = getWidth() / 2, x = cx2 - 400;
        rew->setBounds(x, by + 3, bs - 6, bs - 6); x += bs + 4; play->setBounds(x, by, bs, bs); x += bs + 8; rec->setBounds(x, by, bs, bs); x += bs + 14;
        posR = { x, by, 132, bs }; x += 132; bpmR = { x, by, 104, bs }; x += 104; sigR = { x, by, 118, bs }; x += 118 + 14;
        click->setBounds(x, by, 74, bs); x += 80; count->setBounds(x, by, 104, bs); count->setVisible(true);
        int r = getWidth() - 16;
        r -= 76; setup->setBounds(r, by, 76, bs); r -= 8; r -= 66; save->setBounds(r, by, 66, bs); r -= 8; r -= bs; undo->setBounds(r, by, bs, bs);
        r -= 8; r -= bs; look->setBounds(r, by, bs, bs);
        meterR = {};
        return;
    }
    int cx = getWidth() / 2;
    // the middle: the transport, the position, the tempo
    int tw = 46 + 6 + 64 + 6 + 64;
    int x = cx - 300;
    rew->setBounds(x, y, 46, bh); play->setBounds(x + 52, y, 64, bh); rec->setBounds(x + 122, y, 64, bh);
    x += tw + 18;
    posR = { x, y, 128, bh }; x += 128 + 8;
    bpmR = { x, y, 112, bh }; x += 112 + 8;
    sigR = { x, y, 60, bh }; x += 60 + 12;
    click->setBounds(x, y, 70, bh); x += 76; count->setBounds(x, y, 86, bh);
    int r = getWidth() - 14;
    r -= 72; setup->setBounds(r, y, 72, bh); r -= 6;
    r -= 64; save->setBounds(r, y, 64, bh); r -= 6;
    r -= 48; undo->setBounds(r, y, 48, bh); r -= 6; r -= 48; look->setBounds(r, y, 48, bh); r -= 14;
    meterR = { r - 60, y + 6, 60, bh - 12 };
    count->setVisible(true);
}

void TopBar::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    if (Theme::glass()) {          // three floating capsules of glass: where you are, the transport, the tools
        Theme::backdrop(g, getLocalBounds());
        auto cap = [&](juce::Rectangle<int> r) { if (!r.isEmpty()) drawGlass(g, r.toFloat(), r.getHeight() * 0.5f, 1.0f); };
        cap(home->getBounds().getUnion(titleR).expanded(6, 4).withTrimmedRight(-10));
        auto mid = rew->getBounds().getUnion(sigR).getUnion(click->getBounds()); if (count->isVisible()) mid = mid.getUnion(count->getBounds());
        cap(mid.expanded(6, 4));
        auto right = undo->getBounds().getUnion(setup->getBounds()); if (!meterR.isEmpty()) right = right.getUnion(meterR.withTrimmedLeft(-30));
        cap(right.expanded(6, 4));
    } else if (Theme::softUI()) {   // 2.1: the soft material; the transport sits in one raised capsule
        g.fillAll(Theme::bg());
        auto mid = rew->getBounds().getUnion(sigR).getUnion(click->getBounds()); if (count->isVisible()) mid = mid.getUnion(count->getBounds());
        auto mr = mid.expanded(8, 5).toFloat(); softRaised(g, mr, mr.getHeight() * 0.5f, 7.0f, Theme::card());
        drawSoftShades(g, *this);
    } else {
        g.setColour(Theme::card()); g.fillRect(b);
        g.setColour(Theme::line()); g.fillRect(0.0f, b.getBottom() - 1, b.getWidth(), 1.0f);
    }
    auto screenBox = [&](juce::Rectangle<int> r) { if (Theme::softUI()) { softWell(g, r.toFloat(), 12.0f); return; }
        g.setColour(Theme::scr()); g.fillRoundedRectangle(r.toFloat(), Theme::glass() ? r.getHeight() * 0.5f : 10.0f);
        if (Theme::glass()) { g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), r.getHeight() * 0.5f, 1.0f); } };

    if (Theme::softUI() && !shell.portrait) {
        // ARCO, and where we are in small letters
        { auto f = Theme::font(16.0f, 2).withExtraKerningFactor(0.32f); g.setColour(Theme::txt()); g.setFont(f);
          g.drawText("ARCO", titleR.withHeight(titleR.getHeight() / 2 + 4).translated(0, 1), juce::Justification::bottomLeft); }
        auto song = shell.host.songFolder.isDirectory() ? shell.host.songFolder.getFileName() : juce::String("new song");
        bool running = shell.host.devices.getCurrentAudioDevice() != nullptr;
        g.setFont(Theme::mono(8.0f)); g.setColour(running ? Theme::sub() : Theme::ink(Theme::amber()));
        g.drawText((juce::String(screenTitle(shell.screen)) + juce::String::fromUTF8(" · ") + song + (running ? juce::String() : juce::String::fromUTF8(" · sound off"))).toUpperCase(),
                   titleR.withTrimmedTop(titleR.getHeight() / 2 + 6).withHeight(14), juce::Justification::topLeft);
        // the transport capsule's display
        auto disp = posR.getUnion(bpmR).getUnion(sigR).toFloat();
        softWell(g, disp, 14.0f);
        auto num = [&](const juce::String& n, const char* unit, juce::Rectangle<int> r, juce::Colour col) {
            auto f = Theme::font(19.0f, 0); float w = juce::GlyphArrangement::getStringWidth(f, n);
            auto uf = Theme::mono(7.5f); float uw = juce::GlyphArrangement::getStringWidth(uf, unit);
            float x0 = r.getCentreX() - (w + uw + 6) * 0.5f;
            g.setColour(col); g.setFont(f); g.drawText(n, juce::Rectangle<float>(x0, (float) r.getY(), w + 2, (float) r.getHeight()), juce::Justification::centredLeft);
            g.setColour(Theme::sub()); g.setFont(uf); g.drawText(unit, juce::Rectangle<float>(x0 + w + 6, (float) r.getY() + 4, uw + 4, (float) r.getHeight()), juce::Justification::centredLeft); };
        num(shell.positionText(), "BAR", posR, shell.isRecording() ? Theme::ink(Theme::red()) : Theme::txt());
        num(juce::String(shell.tempo(), shell.tempo() == std::round(shell.tempo()) ? 0 : 1), "BPM", bpmR, Theme::txt());
        int bpb = juce::jlimit(1, 8, shell.beatsPerBar());
        auto sr = sigR.withTrimmedRight(8);
        g.setColour(Theme::txt()); g.setFont(Theme::font(15.0f, 0));
        g.drawText(juce::String(shell.beatsPerBar()) + "/4", sr.withWidth(40), juce::Justification::centred);
        double bt = shell.host.engine.clock.beatNow.load(); int bi = ((int) std::floor(bt)) % bpb; bool run = shell.isPlaying();
        float lx = (float) sr.getX() + 44, step = juce::jmin(10.0f, (sr.getRight() - lx) / (float) bpb);
        for (int i = 0; i < bpb; ++i) softLed(g, { lx + step * (i + 0.5f), (float) sr.getCentreY() }, 2.8f, Theme::blue(), run ? i <= bi : i == 0);
        return;
    }
    // where we are
    say(g, screenTitle(shell.screen), titleR.withHeight(titleR.getHeight() / 2 + 6).translated(0, 2), shell.portrait ? 12.0f : 15.0f, Theme::txt(), 2);
    auto song = shell.host.songFolder.isDirectory() ? shell.host.songFolder.getFileName() : juce::String("new song");
    bool running = shell.host.devices.getCurrentAudioDevice() != nullptr;
    say(g, song + (running ? juce::String() : juce::String::fromUTF8("  ·  sound off")), titleR.withTrimmedTop(titleR.getHeight() / 2 + 4), 8.5f, running ? Theme::sub() : Theme::ink(Theme::amber()), 1);
    // the position
    screenBox(posR);
    say(g, shell.positionText(), posR.reduced(10, 0), 18.0f, shell.isRecording() ? Theme::ink(Theme::red()) : Theme::txt(), 2, juce::Justification::centred);
    // the tempo (drag up / down, double-click to type)
    screenBox(bpmR);
    say(g, juce::String(shell.tempo(), 1), bpmR.withTrimmedRight(40).withTrimmedLeft(10), 18.0f, Theme::txt(), 2, juce::Justification::centredRight);
    say(g, "BPM", bpmR.withTrimmedLeft(bpmR.getWidth() - 36), 8.0f, Theme::sub(), 2);
    screenBox(sigR);
    say(g, juce::String(shell.beatsPerBar()) + "/4", sigR, 14.0f, Theme::txt(), 2, juce::Justification::centred);
    // a beat light in the position screen: the downbeat brighter
    { double bt = shell.host.engine.clock.beatNow.load(); float f = (float) (bt - std::floor(bt));
      bool down = ((int) std::floor(bt)) % juce::jmax(1, shell.beatsPerBar()) == 0;
      auto c = (down ? Theme::red() : Theme::green()).withAlpha(juce::jmax(0.0f, 1.0f - f * 3.0f));
      g.setColour(c); g.fillEllipse((float) posR.getX() + 10, (float) posR.getCentreY() - 3.5f, 7.0f, 7.0f); }
    // in / out
    if (!meterR.isEmpty()) {
        inLevel = juce::jmax(shell.host.engine.peak("live_in1"), inLevel * 0.85f);
        outLevel = juce::jmax(juce::jmax(shell.host.engine.peak("mainL"), shell.host.engine.peak("mainR")), outLevel * 0.85f);
        auto bar = [&](juce::Rectangle<float> r, float v, const char* t) {
            g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 3.0f);
            float db = v > 1e-5f ? 20.0f * std::log10(v) : -60.0f; float k = juce::jlimit(0.0f, 1.0f, (db + 54.0f) / 54.0f);
            g.setColour(db > -1 ? Theme::red() : (db > -9 ? Theme::amber() : Theme::green())); g.fillRoundedRectangle(r.withWidth(r.getWidth() * k), 3.0f);
            say(g, t, r.translated(-26, 0).withWidth(24).toNearestInt(), 6.5f, Theme::sub(), 2, juce::Justification::centredRight); };
        auto m = meterR.toFloat().withTrimmedLeft(26);
        bar(m.withHeight(7).translated(0, 4), inLevel, "IN"); bar(m.withHeight(7).translated(0, m.getHeight() - 11), outLevel, "OUT");
    }
}

void TopBar::mouseDown(const juce::MouseEvent& e) {
    bpmDrag = false;
    if (bpmR.contains(e.getPosition())) { bpmDrag = true; bpmStart = (float) shell.tempo(); dragY = e.position.y; }
    else if (sigR.contains(e.getPosition())) shell.setBeatsPerBar(shell.beatsPerBar() % 7 + 2 > 7 ? 2 : shell.beatsPerBar() % 7 + 2);
    else if (titleR.contains(e.getPosition()) && shell.screen != Screen::Home) shell.canvas.showMenu();
}
void TopBar::mouseDrag(const juce::MouseEvent& e) {
    if (!bpmDrag) return;
    shell.setTempo(bpmStart + std::round((dragY - e.position.y) / 3.0f) * (e.mods.isShiftDown() ? 0.1f : 1.0f)); repaint();
}
void TopBar::mouseUp(const juce::MouseEvent&) { bpmDrag = false; }
void TopBar::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!bpmR.contains(e.getPosition())) return;
    auto* ed = new juce::TextEditor(); ed->setText(juce::String(shell.tempo(), 1)); ed->setBounds(bpmR.reduced(4));
    ed->setFont(Theme::font(14.0f, 2)); ed->setJustification(juce::Justification::centred); ed->selectAll();
    ed->onReturnKey = [this, ed] { shell.setTempo(ed->getText().getDoubleValue()); juce::MessageManager::callAsync([ed] { delete ed; }); };
    ed->onFocusLost = ed->onEscapeKey = [ed] { juce::MessageManager::callAsync([ed] { delete ed; }); };
    addAndMakeVisible(ed); ed->grabKeyboardFocus();
}

// ================================================================== HOME
HomeScreen::HomeScreen(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    setWantsKeyboardFocus(true);
    cards = {
        { Screen::Workstation, "WORKSTATION", juce::String::fromUTF8("Loop live: four scenes, eight tracks, your instruments in, effects on every strip."), 3 },
        { Screen::Loops, "LIVE LOOPS", juce::String::fromUTF8("Cells of loops in rows. Record straight into a cell, drop audio or a station loop in, launch them in time."), 1 },
        { Screen::Studio, "STUDIO", juce::String::fromUTF8("Arrange, record, edit and mix the song on a timeline. Export it."), 2 },
        { Screen::Drums, "DRUM MACHINE", juce::String::fromUTF8("Real drums and the classic machines. Steps, swing, chance, repeats - in tempo."), 0 },
        { Screen::Synth, "SYNTH", juce::String::fromUTF8("Four oscillators, filters, modulation - and an arpeggiator on the metronome."), 4 },
        { Screen::Phrase, "PHRASE", juce::String::fromUTF8("Play four notes into it. It plays them back in tempo: faster, reversed, higher, building up."), 5 },
        { Screen::Keys, "KEYBOARD", juce::String::fromUTF8("Sampled instruments to download, your plug-ins, chords and harmony surfaces."), 6 },
        { Screen::Modular, "MODULAR", juce::String::fromUTF8("Patch a synthesizer from modules with cables."), 7 },
        { Screen::Speakers, "SPEAKERS", juce::String::fromUTF8("Outputs, speakers around the audience, ambisonics."), 3 },
    };
    auto mk = [&](const juce::String& t, std::function<void()> f) { auto* p = new Pill(t, [] { return Theme::btn(); }, 9.0f, 12.0f); p->onClick = std::move(f);
        p->textColour = [] { return Theme::txt(); }; owned.add(p); addAndMakeVisible(p); return p; };
    lookBtn = std::make_unique<Pill>("", [] { return Theme::btn(); }, 10.0f, 12.0f); lookBtn->icon = 12; lookBtn->textColour = [] { return Theme::txt(); };
    lookBtn->onClick = [this] { shell.canvas.command(cmdToggleLight); }; lookBtn->setComponentID("home:look"); addAndMakeVisible(*lookBtn);
    mk("NEW SONG", [this] { shell.canvas.command(cmdNewSong); });
    mk("OPEN SONG", [this] { shell.canvas.doOpen(); });
    mk("SAVE", [this] { shell.canvas.doSave(false); });
    mk("SETUP", [this] { shell.show(Screen::Workstation); shell.canvas.openSetup(true); });
    mk("HELP", [this] { shell.show(Screen::Workstation); shell.canvas.command(cmdHelp); });
    startTimerHz(60);
}
void HomeScreen::resized() {
    // a floating dock at the bottom
    int n = owned.size(), bw = 124, gap = 6;
    dockR = juce::Rectangle<int>(0, 0, n * bw + (n - 1) * gap + 20, 64).withCentre({ getWidth() / 2, getHeight() - 52 });
    int x = dockR.getX() + 10;
    for (auto* c : owned) { c->setBounds(x, dockR.getY() + 10, bw, 44); x += bw + gap; }
    if (lookBtn) lookBtn->setBounds(getWidth() - 46 - 44, 34, 46, 46);
}
juce::Rectangle<float> HomeScreen::cardRect(int i) const {
    float d = (float) i - pos;
    float cw = juce::jmin(500.0f, getWidth() * 0.37f), ch = juce::jmin(cw * 1.10f, getHeight() - 250.0f);
    float scale = 1.0f - juce::jmin(1.0f, std::abs(d)) * 0.16f - juce::jmax(0.0f, std::abs(d) - 1.0f) * 0.08f;
    float spacing = cw * 0.92f;
    float cx = getWidth() * 0.5f + d * spacing - (d > 0 ? 1 : -1) * juce::jmax(0.0f, std::abs(d) - 1.0f) * spacing * 0.18f;
    float cy = 110.0f + (getHeight() - 250.0f) * 0.5f + (1.0f - scale) * 20.0f;
    return { cx - cw * scale * 0.5f, cy - ch * scale * 0.5f, cw * scale, ch * scale };
}
void HomeScreen::paint(juce::Graphics& g) {
    Theme::backdrop(g, getLocalBounds());
    // the name, the song - and nothing else on top
    g.setColour(Theme::txt()); g.setFont(Theme::font(26.0f, 2).withExtraKerningFactor(0.18f)); g.drawText("ARCO", 44, 34, 300, 40, juce::Justification::centredLeft);
    auto song = shell.host.songFolder.isDirectory() ? shell.host.songFolder.getFileName() : juce::String("new song");
    say(g, song, { 46, 76, 400, 20 }, 10.0f, Theme::sub(), 1);
    say(g, "Choose what to play", { getWidth() - 444 - 64, 44, 400, 24 }, 11.0f, Theme::sub(), 1, juce::Justification::centredRight);
    if (Theme::glass()) drawGlass(g, dockR.toFloat(), dockR.getHeight() * 0.5f, 1.1f);
    else drawPanel(g, dockR.toFloat(), 22.0f);
    drawSoftShades(g, *this);
    // far cards first, the one in the middle on top
    std::vector<int> order(cards.size()); for (size_t i = 0; i < cards.size(); ++i) order[i] = (int) i;
    std::sort(order.begin(), order.end(), [this](int a, int b) { return std::abs(a - pos) > std::abs(b - pos); });
    for (int i : order) {
        auto r = cardRect(i);
        if (r.getRight() < -40 || r.getX() > getWidth() + 40) continue;
        float d = std::abs((float) i - pos);
        auto acc = Theme::pal().acc[(size_t) cards[(size_t) i].accent];
        // shadow, body (a soft wash of its colour), border
        if (Theme::softUI()) {         // 2.1: a raised soft card; its colour only as a faint light in the corner
            softRaised(g, r, 30.0f, d < 0.5f ? 14.0f : 9.0f, Theme::card());
            juce::Graphics::ScopedSaveState ss(g); juce::Path cl; cl.addRoundedRectangle(r, 30.0f); g.reduceClipRegion(cl);
            g.setGradientFill(juce::ColourGradient(acc.withAlpha(0.10f), r.getX(), r.getY(), acc.withAlpha(0.0f), r.getX() + r.getWidth() * 0.6f, r.getY() + r.getHeight() * 0.5f, true)); g.fillRect(r);
            if (d < 0.5f) { float bw = r.getWidth() * 0.18f; g.setColour(acc); g.fillRoundedRectangle(r.getCentreX() - bw * 0.5f, r.getY() + 10.0f, bw, 3.0f, 1.5f); }
        } else if (Theme::glass()) {          // a slab of glass, its colour only as a glow inside
            drawGlass(g, r, 30.0f, 1.2f);
            juce::ColourGradient tint(acc.withAlpha(0.20f), r.getX(), r.getY(), acc.withAlpha(0.0f), r.getX() + r.getWidth() * 0.7f, r.getY() + r.getHeight() * 0.6f, false);
            g.setGradientFill(tint); g.fillRoundedRectangle(r, 30.0f);
            if (d < 0.5f) { g.setColour(juce::Colours::white.withAlpha(0.30f)); g.drawRoundedRectangle(r.reduced(0.5f), 30.0f, 1.2f); }
        } else {
            g.setColour(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.12f : 0.40f)); g.fillRoundedRectangle(r.translated(0, 10).expanded(2), 30.0f);
            auto top = Theme::card().interpolatedWith(acc, Theme::lightMode() ? 0.30f : 0.26f), bot = Theme::card().interpolatedWith(acc, 0.06f);
            g.setGradientFill(juce::ColourGradient(top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r, 28.0f);
            g.setColour(acc.withAlpha(d < 0.5f ? 0.9f : 0.25f)); g.drawRoundedRectangle(r.reduced(1.0f), 28.0f, d < 0.5f ? 2.0f : 1.0f);
        }
        float s = r.getWidth() / 470.0f;
        auto art = juce::Rectangle<float>(r.getX() + 30 * s, r.getY() + 30 * s, r.getWidth() - 60 * s, r.getHeight() * 0.60f);
        drawArt(g, i, art, acc);
        float ty = art.getBottom() + 22 * s;
        say(g, cards[(size_t) i].title, juce::Rectangle<float>(r.getX() + 32 * s, ty, r.getWidth() - 64 * s, 40 * s).toNearestInt(), 23.0f * s, Theme::txt(), 2);
        g.setColour(Theme::sub()); g.setFont(Theme::font(10.5f * s, 0));
        g.drawFittedText(cards[(size_t) i].sub, juce::Rectangle<float>(r.getX() + 32 * s, ty + 44 * s, r.getWidth() - 64 * s, 60 * s).toNearestInt(),
                         juce::Justification::topLeft, 3, 0.9f);
        // dim the ones aside
        if (d > 0.05f) { g.setColour(Theme::bg().withAlpha(juce::jmin(0.45f, d * 0.30f))); g.fillRoundedRectangle(r, 28.0f); }
    }
    // dots
    float dw = 8, gap = 10, total = cards.size() * dw + (cards.size() - 1) * gap, x = (getWidth() - total) * 0.5f, y = (float) dockR.getY() - 34;
    for (size_t i = 0; i < cards.size(); ++i) { bool on = std::abs((float) i - pos) < 0.5f;
        g.setColour(on ? Theme::txt() : Theme::line()); g.fillEllipse(x + i * (dw + gap), y, dw, dw); }
}
void HomeScreen::drawArt(juce::Graphics& g, int i, juce::Rectangle<float> r, juce::Colour c) {
    auto sc = Theme::glass() ? juce::Colours::black.withAlpha(0.38f) : Theme::scr().withMultipliedAlpha(Theme::lightMode() ? 0.55f : 0.75f);
    g.setColour(sc); g.fillRoundedRectangle(r, 18.0f);
    if (Theme::glass()) { g.setColour(juce::Colours::white.withAlpha(0.06f)); g.drawRoundedRectangle(r.reduced(0.5f), 18.0f, 1.0f); }
    auto in = r.reduced(r.getWidth() * 0.08f, r.getHeight() * 0.12f);
    auto line = Theme::line();
    auto acc = [](int k) { return Theme::pal().acc[(size_t) (k % 8)]; };
    switch (cards[(size_t) i].screen) {
        case Screen::Workstation: {     // strips with faders
            int n = 6; float w = in.getWidth() / n;
            for (int k = 0; k < n; ++k) { auto col = juce::Rectangle<float>(in.getX() + k * w + w * 0.18f, in.getY(), w * 0.64f, in.getHeight());
                g.setColour(Theme::cardS().withAlpha(0.9f)); g.fillRoundedRectangle(col, 8.0f);
                float fy = col.getY() + col.getHeight() * (0.25f + 0.45f * std::abs(std::sin(k * 1.7f)));
                g.setColour(line); g.fillRect(col.getCentreX() - 1.5f, col.getY() + 14, 3.0f, col.getHeight() - 28);
                g.setColour(k < 2 ? c : acc(k + 1)); g.fillRoundedRectangle(col.getCentreX() - w * 0.24f, fy, w * 0.48f, 14.0f, 4.0f); }
            break; }
        case Screen::Loops: {           // a grid of cells
            int cols = 5, rows = 4; float w = in.getWidth() / cols, h = in.getHeight() / rows;
            for (int y2 = 0; y2 < rows; ++y2) for (int x2 = 0; x2 < cols; ++x2) {
                auto cell = juce::Rectangle<float>(in.getX() + x2 * w, in.getY() + y2 * h, w, h).reduced(5);
                bool on = ((x2 * 3 + y2 * 5) % 4) != 0;
                g.setColour(on ? acc(y2 + 1).withAlpha(x2 == 2 ? 1.0f : 0.55f) : Theme::cardS().withAlpha(0.7f)); g.fillRoundedRectangle(cell, 8.0f);
                if (on && x2 == 2) { g.setColour(Theme::textOn(acc(y2 + 1))); juce::Path p; p.addTriangle(cell.getCentreX() - 6, cell.getCentreY() - 8, cell.getCentreX() - 6, cell.getCentreY() + 8, cell.getCentreX() + 8, cell.getCentreY()); g.fillPath(p); }
            }
            break; }
        case Screen::Studio: {          // lanes with clips
            int rows = 4; float h = in.getHeight() / rows;
            for (int y2 = 0; y2 < rows; ++y2) {
                auto lane = juce::Rectangle<float>(in.getX(), in.getY() + y2 * h, in.getWidth(), h).reduced(0, 5);
                g.setColour(Theme::cardS().withAlpha(0.6f)); g.fillRoundedRectangle(lane, 6.0f);
                float x0 = lane.getX() + (y2 % 2) * lane.getWidth() * 0.2f, len = lane.getWidth() * (0.45f + 0.1f * y2);
                auto clip = juce::Rectangle<float>(x0, lane.getY(), juce::jmin(len, lane.getRight() - x0), lane.getHeight());
                g.setColour(acc(y2).withAlpha(0.8f)); g.fillRoundedRectangle(clip, 6.0f);
                g.setColour(Theme::textOn(acc(y2)).withAlpha(0.5f));
                for (float x = clip.getX() + 6; x < clip.getRight() - 6; x += 4) { float a = std::abs(std::sin(x * 0.11f + y2)) * clip.getHeight() * 0.35f; g.fillRect(x, clip.getCentreY() - a, 2.0f, a * 2); }
            }
            g.setColour(Theme::txt()); g.fillRect(in.getX() + in.getWidth() * 0.62f, in.getY() - 6, 2.0f, in.getHeight() + 12);
            break; }
        case Screen::Drums: {           // pads 4 x 3
            int cols = 4, rows = 3; float w = in.getWidth() / cols, h = in.getHeight() / rows;
            for (int y2 = 0; y2 < rows; ++y2) for (int x2 = 0; x2 < cols; ++x2) {
                auto pad = juce::Rectangle<float>(in.getX() + x2 * w, in.getY() + y2 * h, w, h).reduced(6);
                bool lit = (x2 + y2 * 2) % 5 == 0;
                g.setColour(lit ? c : Theme::cardS().withAlpha(0.95f)); g.fillRoundedRectangle(pad, 10.0f);
                g.setColour(lit ? c.brighter(0.3f) : line); g.drawRoundedRectangle(pad, 10.0f, 1.2f);
            }
            break; }
        case Screen::Synth: {           // a wave and a filter curve
            juce::Path p; for (float x = 0; x <= in.getWidth(); x += 2) { float t = x / in.getWidth(); float y2 = std::sin(t * 6.283f * 2.5f) * (1 - t * 0.5f) + 0.3f * std::sin(t * 6.283f * 7.5f);
                if (x == 0) p.startNewSubPath(in.getX() + x, in.getCentreY() - y2 * in.getHeight() * 0.32f); else p.lineTo(in.getX() + x, in.getCentreY() - y2 * in.getHeight() * 0.32f); }
            g.setColour(c); g.strokePath(p, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break; }
        case Screen::Phrase: {          // four notes, then faster and higher
            float w = in.getWidth() / 12;
            for (int k = 0; k < 12; ++k) { float hgt = (k < 4 ? 0.35f + 0.12f * k : 0.25f + 0.05f * (k % 4) + 0.04f * (k - 4)) * in.getHeight();
                float bw = k < 4 ? w * 0.8f : w * 0.45f; float x = in.getX() + (k < 4 ? k * w * 1.2f : w * 4.8f + (k - 4) * w * 0.85f);
                g.setColour(k < 4 ? c : c.withAlpha(0.45f + 0.06f * (k - 4))); g.fillRoundedRectangle(x, in.getBottom() - hgt, bw, hgt, 4.0f); }
            break; }
        case Screen::Keys: {            // keys
            int n = 10; float w = in.getWidth() / n;
            for (int k = 0; k < n; ++k) { g.setColour(Theme::txt().withAlpha(0.85f)); g.fillRoundedRectangle(in.getX() + k * w + 2, in.getY(), w - 4, in.getHeight(), 5.0f); }
            for (int k = 0; k < n - 1; ++k) if (k % 7 != 2 && k % 7 != 6) { g.setColour(Theme::scr()); g.fillRoundedRectangle(in.getX() + (k + 1) * w - w * 0.3f, in.getY(), w * 0.6f, in.getHeight() * 0.6f, 4.0f); }
            g.setColour(c); g.fillRoundedRectangle(in.getX() + 3 * w + 2, in.getBottom() - 30, w - 4, 26, 5.0f);
            break; }
        case Screen::Modular: {         // jacks and cables
            for (int k = 0; k < 8; ++k) { float x = in.getX() + (k % 4 + 0.5f) * in.getWidth() / 4, y2 = in.getY() + (k / 4 + 0.5f) * in.getHeight() / 2;
                g.setColour(Theme::card()); g.fillEllipse(x - 16, y2 - 16, 32, 32); g.setColour(line); g.drawEllipse(x - 16, y2 - 16, 32, 32, 2.0f); }
            auto jack = [&](int k) { return juce::Point<float>(in.getX() + (k % 4 + 0.5f) * in.getWidth() / 4, in.getY() + (k / 4 + 0.5f) * in.getHeight() / 2); };
            int pairs[3][2] = { { 0, 6 }, { 1, 4 }, { 3, 5 } };
            for (int k = 0; k < 3; ++k) { auto a = jack(pairs[k][0]), b = jack(pairs[k][1]); juce::Path p; p.startNewSubPath(a);
                p.cubicTo(a.x, a.y + 70, b.x, b.y + 70, b.x, b.y); g.setColour(acc(k * 2 + 1)); g.strokePath(p, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
            break; }
        case Screen::Speakers: {        // a ring of speakers
            auto ctr = in.getCentre(); float rad = juce::jmin(in.getWidth(), in.getHeight()) * 0.42f;
            g.setColour(line); g.drawEllipse(ctr.x - rad, ctr.y - rad, rad * 2, rad * 2, 1.5f);
            for (int k = 0; k < 8; ++k) { float a = k * 0.785f; float x = ctr.x + std::sin(a) * rad, y2 = ctr.y - std::cos(a) * rad;
                g.setColour(k == 1 ? c : Theme::card()); g.fillRoundedRectangle(x - 12, y2 - 12, 24, 24, 6.0f); }
            g.setColour(c); g.fillEllipse(ctr.x - 9, ctr.y - 9, 18, 18);
            break; }
        default: break;
    }
}
void HomeScreen::focusOn(Screen s) { for (size_t i = 0; i < cards.size(); ++i) if (cards[i].screen == s) target = (float) i; }
void HomeScreen::timerCallback() {
    if (!isVisible()) return;
    if (!dragged || !isMouseButtonDown()) {
        float d = target - pos;
        if (std::abs(d) > 0.0005f) { pos += d * 0.18f; repaint(); } else if (pos != target) { pos = target; repaint(); }
    }
}
void HomeScreen::mouseDown(const juce::MouseEvent& e) { downX = lastX = e.position.x; downPos = pos; dragged = false; vel = 0; lastT = juce::Time::getMillisecondCounterHiRes(); }
void HomeScreen::mouseDrag(const juce::MouseEvent& e) {
    if (std::abs(e.position.x - downX) > 6) dragged = true;
    if (!dragged) return;
    float cw = juce::jmin(470.0f, getWidth() * 0.36f) * 0.92f;
    double now = juce::Time::getMillisecondCounterHiRes();
    vel = (float) ((lastX - e.position.x) / cw / juce::jmax(1.0, now - lastT) * 1000.0); lastX = e.position.x; lastT = now;
    pos = juce::jlimit(-0.4f, (float) cards.size() - 0.6f, downPos + (downX - e.position.x) / cw);
    target = pos; repaint();
}
void HomeScreen::mouseUp(const juce::MouseEvent& e) {
    if (dragged) { target = juce::jlimit(0.0f, (float) cards.size() - 1, std::round(pos + juce::jlimit(-2.0f, 2.0f, vel * 0.18f))); dragged = false; return; }
    for (int i = 0; i < (int) cards.size(); ++i) if (cardRect(i).contains(e.position)) {
        if (std::abs(i - pos) < 0.5f) shell.show(cards[(size_t) i].screen);     // the one in the middle opens
        else target = (float) i;                                               // one aside comes to the middle
        return;
    }
}
void HomeScreen::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    float d = std::abs(w.deltaX) > std::abs(w.deltaY) ? -w.deltaX : -w.deltaY;
    target = juce::jlimit(0.0f, (float) cards.size() - 1, std::round(target + (d > 0 ? 1.0f : -1.0f) * (std::abs(d) > 0.02f ? 1.0f : 0.0f)));
}
bool HomeScreen::keyPressed(const juce::KeyPress& k) {
    if (k.getKeyCode() == juce::KeyPress::rightKey) { target = juce::jmin((float) cards.size() - 1, target + 1); return true; }
    if (k.getKeyCode() == juce::KeyPress::leftKey) { target = juce::jmax(0.0f, target - 1); return true; }
    if (k.getKeyCode() == juce::KeyPress::returnKey) { shell.show(cards[(size_t) juce::jlimit(0, (int) cards.size() - 1, (int) std::round(target))].screen); return true; }
    return false;
}

} // namespace arco
