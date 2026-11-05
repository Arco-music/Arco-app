#include "StudioView.h"
#include "MainView.h"
#include <climits>
#include <thread>

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }
static void txt(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f);
}
static void txtOn(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour bg, int w = 2) {   // text on a coloured surface
    g.setColour(Theme::textOn(bg)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, juce::Justification::centredLeft, 1, 0.8f);
}
static juce::String noteName(int p) { return juce::MidiMessage::getMidiNoteName(p, true, true, 4); }
static const char* GRID[5] = { "OFF", "BAR", "1/4", "1/8", "1/16" };

// ================================================================== building
StudioView::StudioView(Canvas& c) : studio(c.host.studio), cv(c), juce::Thread("ARCO studio job") {
    getProperties().set("softShades", true);
    setWantsKeyboardFocus(true);
    buildTop();
    startTimerHz(30);
}
StudioView::~StudioView() { cv.host.studioKeys = false; stopThread(8000); }

void StudioView::buildTop() {
    struct B { juce::String id, t; std::function<void()> f; std::function<juce::Colour()> col; };
    auto& S = studio;
    std::vector<B> bs = {
        { "home", U8("⏮"), [this] { studio.pos = studio.song.loopOn ? studio.song.loopStart : 0; scrollSec = 0; repaint(); }, nullptr },
        { "play", U8("▶"), [this] { togglePlay(); }, [this] { return studio.playing.load() ? Theme::green() : Theme::btn(); } },
        { "rec", U8("●"), [this] { toggleRecord(); }, [this] { return studio.recording.load() || studio.countInLeft.load() > 0 ? Theme::red() : Theme::btn(); } },
        { "loop", "LOOP", [this, &S] { const juce::ScopedLock sl(S.lock); auto& s = S.song; if (s.loopEnd <= s.loopStart + 64) { s.loopStart = 0; s.loopEnd = (long) (s.barLen() * 4); } s.loopOn = !s.loopOn; repaint(); },
          [this] { return studio.song.loopOn ? Theme::amber() : Theme::btn(); } },
        { "punch", "PUNCH", [this] { togglePunch(); }, [this] { return studio.song.punchOn ? Theme::red() : Theme::btn(); } },
        { "edit", U8("EDIT ▾"), [this] { editMenu(); }, nullptr },
        { "metro", "CLICK", [this] { studio.song.metronome = !studio.song.metronome; }, [this] { return studio.song.metronome ? Theme::amber() : Theme::btn(); } },
        { "count", "COUNT-IN", [this] { studio.song.countIn = !studio.song.countIn; }, [this] { return studio.song.countIn ? Theme::amber() : Theme::btn(); } },
        { "undo", U8("↶"), [this] { if (studio.undo()) { selClip = -1; selClips.clear(); repaint(); } }, nullptr },
        { "redo", U8("↷"), [this] { if (studio.redo()) { selClip = -1; selClips.clear(); repaint(); } }, nullptr },
        { "arrange", "ARRANGE", [this] { mixView = false; repaint(); }, [this] { return !mixView ? Theme::sel() : Theme::btn(); } },
        { "mixer", "MIXER", [this] { mixView = true; repaint(); }, [this] { return mixView ? Theme::sel() : Theme::btn(); } },
        { "export", "EXPORT", [this] { doExport(); }, [] { return Theme::blue(); } },
        { "save", "SAVE", [this] { if (cv.host.songFolder.isDirectory()) { cv.host.saveSong(cv.host.songFolder); report = studio.save(cv.host.songFolder) ? U8("✓ STUDIO saved with the song") : U8("could not save"); }
                                   else report = "save the song on the station first (File > Save) - STUDIO is kept inside it"; }, nullptr },
        { "close", U8("✕"), [this] { studio.stop(); if (onClose) onClose(); }, nullptr },
    };
    for (auto& b : bs) {
        auto col = b.col;
        bool big = b.id == "play" || b.id == "rec" || b.id == "home" || b.id == "undo" || b.id == "redo";
        auto* p = add<Pill>(b.t, col ? col : std::function<juce::Colour()>([] { return Theme::btn(); }), big ? 14.0f : 8.5f, 8.0f);
        if (b.id == "play") { p->text = ""; p->iconFn = [this] { return studio.playing.load() ? 2 : 1; }; }
        if (b.id == "home") { p->text = ""; p->icon = 11; }
        if (b.id == "rec") { p->text = ""; p->icon = 3; }
        if (b.id == "undo") p->setTooltip("undo (Cmd Z)"); if (b.id == "redo") p->setTooltip("redo (Shift Cmd Z)");
        p->setComponentID("studio:" + b.id);
        p->onClick = b.f;
    }
}

void StudioView::open() {
    if (studio.song.clips.empty()) {
        bool loaded = cv.host.songFolder.isDirectory() && studio.load(cv.host.songFolder);
        juce::String why;
        if (!loaded && !studio.buildFromStation(4, why)) { report = U8("an empty song: + Audio lane and ● records, or drop a file (it keeps its own speed)"); const juce::ScopedLock sl(studio.lock); studio.ensureReturns(); }
    }
    { const juce::ScopedLock sl(studio.lock); studio.ensureReturns(); }
    studio.matchRate(cv.host.engine.sr);              // at the sound card's rate (a song from another rate is converted)
    builtFor = -2; rebuildDetail(); grabKeyboardFocus(); repaint();
}

void StudioView::visibilityChanged() {
    bool hasInst = false; for (auto& L : studio.song.lanes) hasInst |= L.kind == LaneInstrument;
    cv.host.studioKeys = isVisible() && hasInst;
}

// ================================================================== layout
juce::Rectangle<int> StudioView::browserArea() const { return showBrowser ? juce::Rectangle<int>(0, (int) topH, (int) browserW, getHeight() - (int) topH - (showDetail ? (int) detailH : 0)) : juce::Rectangle<int>(); }
juce::Rectangle<int> StudioView::rulerArea() const { int x = (showBrowser ? (int) browserW : 0) + (int) headW; return { x, (int) topH, getWidth() - x, (int) rulerH }; }
juce::Rectangle<int> StudioView::headArea() const { int x = showBrowser ? (int) browserW : 0; int y = (int) (topH + rulerH); return { x, y, (int) headW, getHeight() - y - (showDetail ? (int) detailH : 0) }; }
juce::Rectangle<int> StudioView::clipArea() const { auto h = headArea(); return { h.getRight(), h.getY(), getWidth() - h.getRight(), h.getHeight() }; }
juce::Rectangle<int> StudioView::detailArea() const { return showDetail ? juce::Rectangle<int>(0, getHeight() - (int) detailH, getWidth(), (int) detailH) : juce::Rectangle<int>(); }

float StudioView::laneHeight(int li) const { auto& L = studio.song.lanes; return li >= 0 && li < (int) L.size() ? L[(size_t) li].height : 74.0f; }
float StudioView::laneTop(int li) const {
    float y = clipArea().getY() - scrollY;
    auto& L = studio.song.lanes;
    for (int i = 0; i < li && i < (int) L.size(); ++i) if (!studio.song.hidden(i)) y += L[(size_t) i].height + (L[(size_t) i].showAuto >= 0 ? autoH : 0) + laneGap;
    return y;
}
int StudioView::laneAt(float y, bool* inAuto) const {
    auto& L = studio.song.lanes;
    float t = clipArea().getY() - scrollY;
    for (int i = 0; i < (int) L.size(); ++i) {
        if (studio.song.hidden(i)) continue;                // inside a folded bus
        float h = L[(size_t) i].height, a = L[(size_t) i].showAuto >= 0 ? autoH : 0;
        if (y >= t && y < t + h + a + laneGap) { if (inAuto) *inAuto = y >= t + h; return i; }
        t += h + a + laneGap;
    }
    if (inAuto) *inAuto = false;
    return -1;
}
long StudioView::snapTo(long s, bool fine) const {
    if (grid == 0 || fine) return s;
    double beat = studio.song.beatLen();
    double g = grid == 1 ? beat * studio.song.beats : grid == 2 ? beat : grid == 3 ? beat / 2 : beat / 4;
    return (long) std::llround(std::round(s / g) * g);
}

void StudioView::resized() {
    // a short screen (an iPhone lying down): the timeline gets the room - the inspector opens from a lane or a clip
    static bool shortSeen = false;
    if (getHeight() > 0 && getHeight() < 640 && !shortSeen) { shortSeen = true; showDetail = false; detailH = 200; }
    // the top bar: the transport and the position in the middle-left, the views and files on the right - all the same height
    std::map<juce::String, int> W = { { "home", 46 }, { "play", 58 }, { "rec", 58 }, { "loop", 62 }, { "punch", 70 }, { "edit", 72 }, { "metro", 62 }, { "count", 82 },
                                      { "undo", 44 }, { "redo", 44 }, { "arrange", 96 }, { "mixer", 86 }, { "export", 88 }, { "save", 70 }, { "close", 44 } };
    auto put = [&](const juce::String& id, int x) { for (auto* k : kids) if (k->getComponentID() == "studio:" + id) k->setBounds(x, 10, W[id], 40); return x + W[id] + 6; };
    if (embedded) {         // the shell's bar plays, records and shows the position: here only the tools, smaller
        auto put2 = [&](const juce::String& id, int x) { for (auto* k : kids) if (k->getComponentID() == "studio:" + id) { k->setVisible(true); k->setBounds(x, 8, W[id], 36); } return x + W[id] + 6; };
        for (auto* k : kids) if (k->getComponentID().startsWith("studio:")) k->setVisible(false);
        int x2 = 16;
        x2 = put2("loop", x2); x2 = put2("punch", x2); x2 += 14;          // (CLICK, COUNT-IN, UNDO and SAVE are on the bar above)
        x2 = put2("redo", x2); x2 = put2("edit", x2);
        int r2 = getWidth() - 12;
        for (auto& id : juce::StringArray { "export" }) { r2 -= W[id]; put2(id, r2); r2 -= 6; }
        r2 -= 14;
        for (auto& id : juce::StringArray { "mixer", "arrange" }) { r2 -= W[id]; put2(id, r2); r2 -= 6; }
        builtFor = -2; rebuildDetail(); return;
    }
    int x = 124;
    x = put("home", x); x = put("play", x); x = put("rec", x);
    displayX = x + 4; x = displayX + 240 + 10;               // the position / tempo display
    x = put("loop", x); x = put("punch", x); x = put("metro", x); x = put("count", x); x = put("edit", x + 8);
    int r = getWidth() - 10;
    for (auto& id : juce::StringArray { "close", "export", "save" }) { r -= W[id]; put(id, r); r -= 6; }
    r -= 10;
    for (auto& id : juce::StringArray { "mixer", "arrange" }) { r -= W[id]; put(id, r); r -= 6; }
    r -= 10;
    for (auto& id : juce::StringArray { "redo", "undo" }) { r -= W[id]; put(id, r); r -= 6; }
    builtFor = -2; rebuildDetail();
}

// ================================================================== peaks
const std::vector<float>& StudioView::peaksOf(const StudioSource* s) {
    auto it = peaks.find(s);
    if (it != peaks.end()) return it->second;
    std::vector<float> p((size_t) (s->size() / 256 + 1), 0.0f);
    for (long i = 0; i < s->size(); ++i) { float v = std::abs(s->l[(size_t) i]); if (!s->r.empty()) v = juce::jmax(v, std::abs(s->r[(size_t) i])); auto& q = p[(size_t) (i / 256)]; q = juce::jmax(q, v); }
    if (peaks.size() > 400) peaks.clear();
    return peaks[s] = std::move(p);
}

// ================================================================== painting
void StudioView::paint(juce::Graphics& g) {
    struct ShadeAtEnd { juce::Graphics& g; juce::Component& c; ~ShadeAtEnd() { drawSoftShades(g, c); } } shadeAtEnd { g, *this };
    hits.clear(); dynLabels.clear();
    rebuildDetail();
    Theme::backdrop(g, getLocalBounds());
    // 2.2: drawing only reads the song, on the same thread that edits it - it needs no lock. (Holding the lock while
    // drawing made the sound skip every time the screen was drawn: the audio thread found it locked and played silence.)
    paintTop(g);
    if (showBrowser) paintBrowser(g);
    if (mixView) paintMixer(g);
    else { paintLanes(g); paintRuler(g); }
    if (showDetail) paintDetail(g);
    else {          // the details are hidden: a tab brings them back
        auto r = juce::Rectangle<int>(getWidth() - 130, getHeight() - 30, 118, 24);
        g.setColour(Theme::sel()); g.fillRoundedRectangle(r.toFloat(), 6);
        txt(g, U8("DETAILS ▴"), r, 8.0f, Theme::textOn(Theme::sel()), 2, juce::Justification::centred);
        hits.push_back({ r.toFloat(), [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<StudioView>(this)] { if (sp) { sp->showDetail = true; sp->builtFor = -2; sp->resized(); sp->repaint(); } }); }, nullptr, nullptr });
    }
    // a loop being dragged from the station
    if (drag == Browser && dragItem >= 0 && dragItem < (int) browserItems.size()) {
        auto& it = browserItems[(size_t) dragItem];
        auto r = juce::Rectangle<float>(dragNow.x - 6, dragNow.y - 12, 150, 24);
        g.setColour(Theme::sel().withMultipliedAlpha(0.9f)); g.fillRoundedRectangle(r, 6);
        txt(g, it.text, r.toNearestInt().reduced(6, 0), 8.5f, Theme::textOn(Theme::sel()), 2);
        if (clipArea().contains(dragNow.toInt())) { float x = xAt(snapTo(sampleAt(dragNow.x), false) / studio.song.sr); g.setColour(Theme::sel()); g.fillRect(x - 1, (float) clipArea().getY(), 2.0f, (float) clipArea().getHeight()); }
    }
}

void StudioView::paintTop(juce::Graphics& g) {
    auto& S = studio.song;
    if (!Theme::softUI()) { g.setColour(Theme::card()); g.fillRect(0, 0, getWidth(), (int) topH); }
    if (embedded) {
        if (!Theme::softUI()) { g.setColour(Theme::line()); g.fillRect(0, (int) topH - 1, getWidth(), 1); }
        // a message stays 8 s, then the hints come back
        static juce::String lastReport; static double reportAt = 0; double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (report != lastReport) { lastReport = report; reportAt = nowMs; }
        bool fresh = report.isNotEmpty() && (nowMs - reportAt < 8000 || listening.load() > 0);
        juce::String line = fresh ? report : hint;
        if (line.isNotEmpty()) txt(g, line, { 360, 8, juce::jmax(100, getWidth() - 360 - 330), 36 }, 7.5f, fresh ? (Theme::softUI() ? Theme::ink(Theme::green()) : Theme::green()) : Theme::sub(), 1, juce::Justification::centred);
        if (isThreadRunning() || (progress > 0 && progress < 1)) {
            auto barR = juce::Rectangle<float>(0, topH - 3, (float) getWidth(), 3);
            g.setColour(Theme::card2()); g.fillRect(barR); g.setColour(Theme::green()); g.fillRect(barR.withWidth(barR.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())));
        }
        return;
    }
    txt(g, "STUDIO", { 16, 10, 100, 40 }, 17.0f, Theme::txt(), 2);
    int x0 = displayX;
    auto box = juce::Rectangle<int>(x0, 10, 240, 40);
    g.setColour(Theme::scr()); g.fillRoundedRectangle(box.toFloat(), 7);
    double sec = studio.pos.load() / S.sr, beats = studio.pos.load() / S.beatLen();
    int bar = (int) std::floor(beats / S.beats) + 1, bt = (int) std::floor(std::fmod(beats, (double) S.beats)) + 1;
    txt(g, juce::String(bar) + "." + juce::String(bt), box.withWidth(56).translated(8, 0), 13.0f, studio.recording.load() ? Theme::red() : Theme::txt(), 2);
    txt(g, juce::String((int) sec / 60) + ":" + juce::String((int) sec % 60).paddedLeft('0', 2), box.withWidth(44).translated(62, 0), 9.0f, Theme::sub(), 1);
    auto bpmR = juce::Rectangle<int>(box.getX() + 104, box.getY(), 58, 40);
    txt(g, juce::String(S.bpm, 1), bpmR, 12.0f, Theme::txt(), 2, juce::Justification::centredRight);
    txt(g, "BPM", { bpmR.getRight() + 4, box.getY(), 28, 40 }, 7.5f, Theme::sub(), 1);
    txt(g, juce::String(S.beats) + "/4", { box.getRight() - 34, box.getY(), 30, 40 }, 9.5f, Theme::txt(), 1, juce::Justification::centredRight);
    hits.push_back({ bpmR.toFloat(), nullptr, [this](juce::Point<float> d, const juce::MouseEvent& e) {
        const juce::ScopedLock sl(studio.lock); studio.song.bpm = juce::jlimit(30.0, 300.0, std::round((studio.song.bpm - d.y * (e.mods.isShiftDown() ? 0.02 : 0.2)) * 10) / 10); }, nullptr });
    hits.push_back({ juce::Rectangle<float>((float) box.getRight() - 34, (float) box.getY(), 32, 32), [this] { const juce::ScopedLock sl(studio.lock); studio.song.beats = studio.song.beats % 7 + 2; }, nullptr, nullptr });
    if (studio.countInLeft.load() > 0) txt(g, U8("count-in …"), { x0 + 210, 44, 120, 8 }, 6.5f, Theme::red(), 2);
    // a line of help, the result of the last job, a progress bar
    juce::String line = report.isNotEmpty() ? report : hint;
    if (line.isNotEmpty()) txt(g, line, { 124, (int) topH - 10, getWidth() - 400, 10 }, 6.5f, report.isNotEmpty() ? Theme::green() : Theme::sub(), 1);
    if (isThreadRunning() || (progress > 0 && progress < 1)) {
        auto barR = juce::Rectangle<float>(0, topH - 3, (float) getWidth(), 3);
        g.setColour(Theme::card2()); g.fillRect(barR); g.setColour(Theme::green()); g.fillRect(barR.withWidth(barR.getWidth() * juce::jlimit(0.0f, 1.0f, progress.load())));
    }
}

// 2.2: a painted key in the silicone skin (the studio draws many small buttons itself): raised; latched = pressed in,
// lit from below in its colour
static void studioKey(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, bool on, juce::Colour c, float fs) {
    float rad = juce::jmin(r.getHeight() * 0.5f, 14.0f);
    if (Theme::softUI()) {
        softKeyLite(g, r, rad, on, c, on ? 0.9f : 0.0f);
        txt(g, Theme::keyCase(t), r.toNearestInt(), fs * 1.12f, on ? Theme::ink(c) : Theme::txt(), 1, juce::Justification::centred);
        return;
    }
    g.setColour(on ? c : Theme::btn()); g.fillRoundedRectangle(r, 5);
    txt(g, t, r.toNearestInt(), fs, Theme::textOn(on ? c : Theme::btn()), 2, juce::Justification::centred);
}

void StudioView::paintBrowser(juce::Graphics& g) {
    auto r = browserArea();
    if (Theme::softUI()) softRaised(g, r.toFloat().reduced(10.0f, 6.0f).withTrimmedRight(16.0f), 22.0f, 6.0f, Theme::card());
    else { g.setColour(Theme::card()); g.fillRect(r.withTrimmedRight(2)); }
    browserItems.clear();
    auto& e = cv.host.engine;
    int y = r.getY() + 10;
    int tx = Theme::softUI() ? 26 : 14;
    if (Theme::softUI()) y += 10;
    if (Theme::softUI()) txt(g, "The station", { tx, y, r.getWidth() - 20, 16 }, 11.0f, Theme::txt(), 2); else txt(g, "THE STATION", { tx, y, r.getWidth() - 20, 16 }, 9.5f, Theme::txt(), 2); y += 18;
    txt(g, U8("drag a scene or a loop onto the timeline"), { tx, y, r.getWidth() - 20, 12 }, 7.0f, Theme::sub()); y += 18;
    int smask = (int) e.snap("smask").v.load();
    static const char* LL[4] = { "A", "B", "C", "D" };
    double bl = e.snap("sv_bl").v.load(); int bts = juce::jmax(1, (int) std::round(e.snap("sv_bts").v.load()));
    for (int sc = 1; sc <= 4; ++sc) {
        bool has = (smask >> (sc - 1)) & 1;
        long len = (long) e.snap("sv_len" + std::to_string(sc - 1)).v.load(); int mask = (int) e.snap("sv_mask" + std::to_string(sc - 1)).v.load();
        double bars = bl > 16 ? len / bl / bts : 0;
        auto row = juce::Rectangle<int>(Theme::softUI() ? 22 : 10, y, r.getWidth() - (Theme::softUI() ? 60 : 20), 26);
        if (Theme::softUI()) { if (has) softKeyLite(g, row.toFloat(), 13.0f, false); else softWellLite(g, row.toFloat(), 13.0f); }
        else { g.setColour(has ? Theme::card2() : Theme::card()); g.fillRoundedRectangle(row.toFloat(), 6); }
        txt(g, Theme::keyCase(juce::String("SCENE ") + LL[sc - 1]), row.withTrimmedLeft(12).withWidth(70), 9.5f, has ? Theme::txt() : Theme::sub(), 1);
        txt(g, has ? juce::String(juce::roundToInt(bars)) + " bars" : juce::String("empty"), row.withTrimmedLeft(84), 7.5f, Theme::sub(), 1);
        if (has) browserItems.push_back({ sc, -1, juce::String("SCENE ") + LL[sc - 1] + U8(" ▸"), row, 0 });
        y += 30;
        if (has) {
            int col = 0;
            for (int t = 0; t < 8; ++t) if ((mask >> t) & 1) {
                auto tr = juce::Rectangle<int>(18 + col * 44, y, 40, 20);
                g.setColour(Theme::strip(4 + t).withAlpha(0.85f)); g.fillRoundedRectangle(tr.toFloat(), 5);
                txt(g, juce::String(LL[sc - 1]) + juce::String(t + 1), tr, 8.0f, Theme::textOn(Theme::strip(4 + t)), 2, juce::Justification::centred);
                browserItems.push_back({ sc, t, juce::String("loop ") + LL[sc - 1] + juce::String(t + 1), tr, 0 });
                if (++col == 4) { col = 0; y += 24; }
            }
            if (col != 0) y += 24;
            y += 4;
        }
    }
    y += 8;
    auto button = [&](const juce::String& t, int action, juce::Colour c) {
        auto br = juce::Rectangle<int>(Theme::softUI() ? 22 : 10, y, r.getWidth() - (Theme::softUI() ? 60 : 20), Theme::softUI() ? 30 : 26);
        if (Theme::softUI()) studioKey(g, br.toFloat(), t, false, c, 8.5f);
        else { g.setColour(c); g.fillRoundedRectangle(br.toFloat(), 6); txt(g, t, br, 8.5f, Theme::textOn(c), 2, juce::Justification::centred); }
        browserItems.push_back({ 0, -1, t, br, action }); y += Theme::softUI() ? 38 : 30; };
    button(U8("LAY OUT ALL SCENES"), 1, Theme::btn());
    if (Theme::softUI()) { g.setColour(Theme::sub()); g.setFont(Theme::mono(7.0f)); g.drawText("ADD", juce::Rectangle<int>(tx, y + 4, 100, 14), juce::Justification::centredLeft); }
    else txt(g, "ADD", { tx, y + 4, 100, 14 }, 9.0f, Theme::txt(), 2);
    y += 22;
    button(U8("+ AUDIO LANE"), 2, Theme::btn());
    button(U8("+ INSTRUMENT (SYNTH)"), 3, Theme::btn());
    button(Theme::softUI() ? U8("+ Bus (a group)") : U8("+ BUS (A GROUP)"), 6, Theme::btn());
    button(U8("+ RECORD THE STATION"), 4, Theme::btn());
    button(Theme::softUI() ? U8("Import a file …") : U8("IMPORT A FILE …"), 5, Theme::btn());
    y += 6;
    g.setColour(Theme::ink(Theme::sub())); g.setFont(Theme::font(7.0f, 0));
    g.drawFittedText(U8("The station is for playing live; STUDIO turns what you played into a song. Record here too: press ● above - it records into the selected track, or a new one."),
                     { tx, y, r.getWidth() - tx - 38, juce::jmax(10, juce::jmin(60, r.getBottom() - y - 12)) }, juce::Justification::topLeft, 5, 0.9f);
}

void StudioView::paintRuler(juce::Graphics& g) {
    auto& S = studio.song;
    auto ra = rulerArea();
    g.setColour(Theme::card()); g.fillRect(ra);
    auto corner = juce::Rectangle<int>(headArea().getX(), ra.getY(), (int) headW, ra.getHeight());
    g.setColour(Theme::card()); g.fillRect(corner);
    {   // the corner: the station drawer, snapping, zoom
        auto cbtn = [&](juce::Rectangle<int> r, const juce::String& t, bool on, std::function<void()> f) {
            studioKey(g, r.toFloat(), t, on, Theme::blue(), 7.5f);
            hits.push_back({ r.toFloat(), std::move(f), nullptr, nullptr }); };
        int cx = corner.getX() + 6, cy = ra.getY() + 5, ch = ra.getHeight() - 10;
        cbtn({ cx, cy, 30, ch }, U8("◧"), showBrowser, [this] { showBrowser = !showBrowser; resized(); repaint(); });
        cbtn({ cx + 34, cy, 92, ch }, U8("SNAP ") + GRID[grid] + U8(" ▾"), grid > 0, [this] {
            juce::PopupMenu m; for (int i = 0; i < 5; ++i) m.addItem(i + 1, i == 0 ? juce::String("off (free)") : juce::String("to the ") + (i == 1 ? "bar" : i == 2 ? "beat" : GRID[i]), true, grid == i);
            m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) { if (r > 0) grid = r - 1; repaint(); }); });
        cbtn({ cx + 130, cy, 26, ch }, U8("−"), false, [this] { pxPerSec = juce::jmax(2.0, pxPerSec / 1.5); repaint(); });
        cbtn({ cx + 160, cy, 26, ch }, "+", false, [this] { pxPerSec = juce::jmin(1200.0, pxPerSec * 1.5); repaint(); });
        float ml = studio.masterMeter.l.load(), mr = studio.masterMeter.r.load();
        int mx = cx + 192, mw = corner.getRight() - mx - 6;
        if (mw > 8) for (int k = 0; k < 2; ++k) {            // the master meter: two thin grooves, lit
            auto mr2 = juce::Rectangle<float>((float) mx, (float) ra.getY() + 9 + k * 9, (float) mw, 6.0f);
            if (Theme::softUI()) softWellLite(g, mr2, 3.0f); else { g.setColour(Theme::scr()); g.fillRect(mr2); }
            float lv = juce::jmin(1.0f, k == 0 ? ml : mr);
            g.setColour((lv > 0.95f ? Theme::red() : Theme::green()).withAlpha(0.85f)); g.fillRoundedRectangle(mr2.reduced(1.0f).withWidth(juce::jmax(0.0f, (mw - 2) * lv)), 2.5f);
        }
    }
    g.saveState(); g.reduceClipRegion(ra);
    double barSec = S.barLen() / S.sr;
    int every = pxPerSec * barSec < 26 ? (pxPerSec * barSec < 8 ? 16 : 4) : 1;
    for (int b = juce::jmax(0, (int) std::floor(scrollSec / barSec)); ; ++b) {
        float x = xAt(b * barSec); if (x > getWidth()) break;
        bool major = b % every == 0;
        g.setColour(major ? Theme::sub() : Theme::line()); g.fillRect(x, (float) ra.getBottom() - (major ? 12 : 6), 1.0f, major ? 12.0f : 6.0f);
        if (major) txt(g, juce::String(b + 1), { (int) x + 3, ra.getY() + 14, 40, 12 }, 7.5f, Theme::sub(), 1);
    }
    for (auto& m : S.markers) { float x = xAt(m.pos / S.sr); g.setColour(Theme::amber()); g.fillRect(x, (float) ra.getY(), 2.0f, 12.0f); txt(g, m.name, { (int) x + 4, ra.getY(), 90, 12 }, 7.0f, Theme::amber(), 2); }
    if (S.loopEnd > S.loopStart) {
        float a = xAt(S.loopStart / S.sr), b = xAt(S.loopEnd / S.sr);
        g.setColour((S.loopOn ? Theme::amber() : Theme::sub()).withAlpha(S.loopOn ? 0.45f : 0.2f)); g.fillRect(a, (float) ra.getBottom() - 8, b - a, 6.0f);
    }
    if (S.punchEnd > S.punchStart) {           // 2.3: the punch points (red): only what falls between them is recorded
        float a = xAt(S.punchStart / S.sr), b = xAt(S.punchEnd / S.sr);
        g.setColour(Theme::red().withAlpha(S.punchOn ? 0.55f : 0.18f)); g.fillRect(a, (float) ra.getY() + 13, b - a, 4.0f);
        if (S.punchOn) { g.fillRect(a - 1, (float) ra.getY() + 10, 2.0f, 10.0f); g.fillRect(b - 1, (float) ra.getY() + 10, 2.0f, 10.0f); }
    }
    g.restoreState();
    float px = xAt(studio.pos.load() / S.sr);
    if (px >= ra.getX()) { g.setColour(studio.recording.load() ? Theme::red() : Theme::txt()); juce::Path tri; tri.addTriangle(px - 6, (float) ra.getBottom() - 9, px + 6, (float) ra.getBottom() - 9, px, (float) ra.getBottom()); g.fillPath(tri); }
}

void StudioView::paintLanes(juce::Graphics& g) {
    auto& S = studio.song;
    auto ha = headArea(), ca = clipArea();
    g.saveState(); g.reduceClipRegion(ha.getUnion(ca));
    if (Theme::softUI()) { g.setColour(Theme::bg()); g.fillRect(ha); }     // the track column on clean skin (no shade from beside it)
    double barSec = S.barLen() / S.sr;
    {
        int every = pxPerSec * barSec < 26 ? (pxPerSec * barSec < 8 ? 16 : 4) : 1;
        for (int b = juce::jmax(0, (int) std::floor(scrollSec / barSec)); ; ++b) {
            float x = xAt(b * barSec); if (x > getWidth()) break;
            g.setColour(Theme::line().withMultipliedAlpha(b % every == 0 ? 0.9f : 0.35f)); g.fillRect(x, (float) ca.getY(), 1.0f, (float) ca.getHeight());
            if (pxPerSec * barSec > 160) for (int k = 1; k < S.beats; ++k) { float xb = xAt(b * barSec + k * barSec / S.beats); g.setColour(Theme::line().withMultipliedAlpha(0.18f)); g.fillRect(xb, (float) ca.getY(), 1.0f, (float) ca.getHeight()); }
        }
        if (S.loopOn && S.loopEnd > S.loopStart) { float a = xAt(S.loopStart / S.sr), b = xAt(S.loopEnd / S.sr); g.setColour(Theme::amber().withAlpha(0.05f)); g.fillRect(a, (float) ca.getY(), b - a, (float) ca.getHeight()); }
        if (S.punchOn && S.punchEnd > S.punchStart) { float a = xAt(S.punchStart / S.sr), b = xAt(S.punchEnd / S.sr); g.setColour(Theme::red().withAlpha(0.045f)); g.fillRect(a, (float) ca.getY(), b - a, (float) ca.getHeight()); }
    }
    for (int li = 0; li < (int) S.lanes.size(); ++li) {
        auto& L = S.lanes[(size_t) li];
        if (S.hidden(li)) continue;
        float y = laneTop(li), h = L.height;
        if (y > ca.getBottom()) break;
        float tot = h + (L.showAuto >= 0 ? autoH : 0);
        if (y + tot < ca.getY()) continue;
        auto col = Theme::strip(L.colour % 12);
        auto hr = juce::Rectangle<float>((float) ha.getX(), y, headW - 2, tot);
        if (Theme::softUI()) {
            auto cr = hr.reduced(3.0f, 2.0f);
            if (li == selLane) softWellLite(g, cr, 14.0f); else softRaised(g, cr, 14.0f, 3.0f, Theme::card());
            g.setColour(L.kind == LaneReturn ? Theme::sub() : col); g.fillRoundedRectangle(juce::Rectangle<float>(cr.getX() + 6, cr.getY() + 9, 4, juce::jmax(6.0f, cr.getHeight() - 18)), 2.0f);
        } else {
            g.setColour(li == selLane ? Theme::card2() : Theme::card()); g.fillRoundedRectangle(hr, 6);
            g.setColour(L.kind == LaneReturn ? Theme::sub() : col); g.fillRoundedRectangle(hr.withWidth(5), 2);
        }
        const float ix = Theme::softUI() ? 20.0f : 14.0f;      // the text clear of the colour pill
        int depth = 0; for (int x = L.out; x >= 0 && x < (int) S.lanes.size() && depth < 4; x = S.lanes[(size_t) x].out) ++depth;
        auto hx = (float) ha.getX() + depth * 10.0f;        // 2.3: inside a bus: indented under it
        bool ret = L.kind == LaneReturn || L.kind == LaneBus;
        juce::String kindT = juce::String(L.frozen ? U8("❄ ") : juce::String()) + (L.kind == LaneInstrument ? U8("♪ ") : L.kind == LaneReturn ? U8("↩ ") : juce::String());
        if (L.kind == LaneBus) {           // the fold key: its tracks shown or tucked away
            auto fr = juce::Rectangle<float>(hx + ix - 4, y + 7, 18, 18);
            studioKey(g, fr, L.folded ? U8("▸") : U8("▾"), false, Theme::btn(), 8.0f);
            hits.push_back({ fr, [this, li] { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.folded = !l.folded; }, nullptr, nullptr });
            kindT = juce::String("      ");
        }
        // row 1: the name, then M S ● - row 2: the volume (and its meter), ⋯ - row 3 (tall lanes): what it records
        txt(g, kindT + Theme::nameCase(L.name), { (int) (hx + ix), (int) y + 6, (int) (headW - 90 - ix - depth * 10), 20 }, 9.5f, Theme::txt(), 2);
        hits.push_back({ { hx + ix + (L.kind == LaneBus ? 20 : 0), y + 6, headW - 90 - ix - depth * 10, 20 }, [this, li] { selLane = li; detail = DLane; selClip = -1; }, nullptr, [this, li] { rename(li); } });
        auto btn = [&](juce::Rectangle<float> r, const juce::String& t, bool on, juce::Colour c, std::function<void()> f) {
            studioKey(g, r, t, on, c, 8.0f);
            hits.push_back({ r, f, nullptr, nullptr }); };
        float bx = (float) ha.getX() + headW - 88;
        btn({ bx, y + 6, 24, 20 }, "M", L.mute, Theme::red(), [this, li] { const juce::ScopedLock sl(studio.lock); studio.song.lanes[(size_t) li].mute = !studio.song.lanes[(size_t) li].mute; });
        btn({ bx + 27, y + 6, 24, 20 }, "S", L.solo, Theme::blue(), [this, li] { const juce::ScopedLock sl(studio.lock); studio.song.lanes[(size_t) li].solo = !studio.song.lanes[(size_t) li].solo; });
        if (!ret) btn({ bx + 54, y + 6, 24, 20 }, U8("●"), L.arm, Theme::red(), [this, li] { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.arm = !l.arm; });
        float ry = ret ? y + 30 : y + 32;
        auto vb = juce::Rectangle<float>(hx + ix, ry + 2, headW - 98 - ix - depth * 10, 8);
        if (Theme::softUI()) softWellLite(g, vb, 4.0f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(vb, 4); }
        g.setColour(Theme::arc(col)); g.fillRoundedRectangle(vb.reduced(Theme::softUI() ? 1.5f : 0.0f).withWidth((vb.getWidth() - (Theme::softUI() ? 3 : 0)) * juce::jlimit(0.0f, 1.0f, (L.vol + 60.0f) / 66.0f)), 3.5f);
        if (li < 64) { float m = studio.meters[(size_t) li].l.load(); g.setColour(Theme::green().withAlpha(0.85f)); g.fillRect(vb.getX(), vb.getBottom() + 2, vb.getWidth() * juce::jmin(1.0f, m), 2.0f); }
        txt(g, juce::String(std::abs(L.vol) < 0.05f ? 0.0f : L.vol, 1) + " dB", { (int) vb.getRight() + 4, (int) ry - 2, 44, 16 }, 7.0f, Theme::sub(), 1);
        hits.push_back({ vb.expanded(0, 6), [this, li] { selLane = li; detail = DLane; selClip = -1; },
            [this, li](juce::Point<float> d, const juce::MouseEvent& e) { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.vol = juce::jlimit(-60.0f, 6.0f, l.vol + d.x * (e.mods.isShiftDown() ? 0.02f : 0.25f)); }, nullptr });
        btn({ (float) ha.getX() + headW - 36, ry - 3, 26, 18 }, U8("⋯"), L.showAuto >= 0, Theme::amber(), [this, li] { laneMenu(li); });
        if (h >= 62 && !ret) {
            juce::String sub;
            if (L.kind == LaneAudio) sub = (L.input < 0 ? U8("Records the station") : L.input < 4 ? "Records Live " + juce::String(L.input + 1) : "Records input " + juce::String(L.input - 3)) + (L.monitor ? U8(" · hearing it") : juce::String()) + U8(" ▾");
            else sub = "Plays Synth 2";
            auto sr2 = juce::Rectangle<float>(hx + ix, y + 48, headW - 78 - ix - depth * 10, 14);
            txt(g, sub, sr2.toNearestInt(), 7.0f, L.monitor ? Theme::amber() : Theme::sub(), 1);
            if (L.kind == LaneAudio) hits.push_back({ sr2, [this, li] { inputMenu(li); }, nullptr, nullptr });
            // the effects of this track: one tap (none yet: the menu opens)
            int nOn = 0; for (auto& dv : L.devices) nOn += dv.on;
            auto fxr = juce::Rectangle<float>((float) ha.getX() + headW - 64, y + 44, 54, 20);
            btn(fxr, nOn > 0 ? "FX " + juce::String(nOn) : juce::String("+ FX"), nOn > 0, Theme::violet(), [this, li] {
                selLane = li; selClip = -1; detail = DLane; showDetail = true; builtFor = -2;
                if (studio.song.lanes[(size_t) li].devices.empty()) addDeviceMenu(li);
                juce::MessageManager::callAsync([sp = juce::Component::SafePointer<StudioView>(this)] { if (sp) { sp->resized(); sp->repaint(); } }); });
        }
        hits.push_back({ { hx, y + h - 4, headW, 6 }, nullptr, [this, li](juce::Point<float> d, const juce::MouseEvent&) { auto& l = studio.song.lanes[(size_t) li]; l.height = juce::jlimit(40.0f, 300.0f, l.height + d.y); }, nullptr });
        hits.push_back({ hr, [this, li] { selLane = li; detail = DLane; selClip = -1; }, nullptr, nullptr });
        g.setColour(li % 2 ? Theme::bg() : Theme::bg().interpolatedWith(Theme::card(), 0.25f)); g.fillRect((float) ca.getX(), y, (float) ca.getWidth(), h);
        if (L.kind == LaneBus) {           // a bus has no clips: what goes into it
            int nIn = 0; for (auto& o : S.lanes) nIn += o.out == li;
            g.setColour(Theme::strip(L.colour % 12).withAlpha(0.08f)); g.fillRect((float) ca.getX(), y + 3, (float) ca.getWidth(), h - 6);
            txt(g, U8("BUS  ·  ") + juce::String(nIn) + (nIn == 1 ? " track goes into it" : " tracks go into it") + (L.folded ? U8("  ·  folded (▸ opens)") : juce::String()) + U8("  ·  its effects and fader act on them all"),
                { ca.getX() + 14, (int) y, ca.getWidth() - 20, (int) h }, 7.5f, Theme::sub(), 1);
        }
        if (L.showAuto >= 0) paintAuto(g, li, { (float) ca.getX(), y + h, (float) ca.getWidth(), autoH });
    }
    g.saveState(); g.reduceClipRegion(ca);      // 2.3: the clips stay on the timeline - never over the track column (M, S, ● stay reachable while it scrolls)
    for (int ci = 0; ci < (int) S.clips.size(); ++ci) {
        auto& c = S.clips[(size_t) ci];
        if (c.lane < 0 || c.lane >= (int) S.lanes.size() || S.hidden(c.lane)) continue;
        float y = laneTop(c.lane) + 2, h = S.lanes[(size_t) c.lane].height - 4;
        float x0 = xAt(c.start / S.sr), x1 = xAt((c.start + c.length) / S.sr);
        if (x1 < ca.getX() || x0 > getWidth() || y > ca.getBottom() || y + h < ca.getY()) continue;
        paintClip(g, c, ci, { x0, y, x1 - x0, h });
    }
    if (S.autoXfade)            // 2.3: where two clips overlap on a lane: the crossfade, drawn as an X
        for (int i = 0; i < (int) S.clips.size(); ++i) for (int j = 0; j < (int) S.clips.size(); ++j) {
            auto& a = S.clips[(size_t) i]; auto& b = S.clips[(size_t) j];
            if (i == j || a.lane != b.lane || a.isMidi() || b.isMidi() || a.muted || b.muted || !(b.start > a.start && b.start < a.end() && b.end() > a.end())) continue;
            if (a.lane < 0 || a.lane >= (int) S.lanes.size()) continue;
            float y = laneTop(a.lane) + 16, h = S.lanes[(size_t) a.lane].height - 20;
            float x0 = xAt(b.start / S.sr), x1 = xAt(a.end() / S.sr);
            if (x1 < ca.getX() || x0 > getWidth()) continue;
            g.setColour(Theme::txt().withAlpha(0.12f)); g.fillRect(x0, y, x1 - x0, h);
            g.setColour(Theme::txt().withAlpha(0.7f)); g.drawLine(x0, y + h, x1, y, 1.2f); g.drawLine(x0, y, x1, y + h, 1.2f);
        }
    float px = xAt(studio.pos.load() / S.sr);
    if (px >= ca.getX()) { g.setColour(studio.recording.load() ? Theme::red() : Theme::txt().withAlpha(0.8f)); g.fillRect(px - 0.5f, (float) ca.getY(), 1.5f, (float) ca.getHeight()); }
    g.restoreState();
    float yEnd = laneTop((int) S.lanes.size());
    if (S.clips.empty()) {          // an empty song: three big ways to start
        auto box = juce::Rectangle<int>(0, 0, juce::jmin(560, ca.getWidth() - 40), 214).withCentre({ ca.getCentreX(), ca.getY() + juce::jmin(ca.getHeight() / 2, 200) });
        g.setColour(Theme::card()); g.fillRoundedRectangle(box.toFloat(), 14);
        g.setColour(Theme::line()); g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 14, 1.0f);
        txt(g, "START YOUR SONG", { box.getX(), box.getY() + 18, box.getWidth(), 22 }, 13.0f, Theme::txt(), 2, juce::Justification::centred);
        txt(g, U8("or drop audio files here · drag loops from the station drawer (◧)"), { box.getX(), box.getY() + 42, box.getWidth(), 14 }, 7.5f, Theme::sub(), 1, juce::Justification::centred);
        const char* T[3] = { "LAY OUT THE SCENES", "RECORD THE STATION", "IMPORT AUDIO" };
        juce::String D[3] = { "every scene you played, in order", U8("arm it, then press ● REC"), "WAV, AIFF, MP3, FLAC, M4A" };
        const int A[3] = { 1, 4, 5 };
        int bw = (box.getWidth() - 48) / 3;
        for (int i = 0; i < 3; ++i) {
            auto r = juce::Rectangle<int>(box.getX() + 16 + i * (bw + 8), box.getY() + 74, bw, 110);
            auto c = i == 1 ? Theme::red() : Theme::sel();
            g.setColour(Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 10);
            g.setColour(c); g.fillRoundedRectangle(r.removeFromTop(6).toFloat(), 3);
            txt(g, i == 0 ? U8("▤") : i == 1 ? U8("●") : U8("⇩"), r.withHeight(44).translated(0, 6), 18.0f, c, 2, juce::Justification::centred);
            txt(g, T[i], r.withY(r.getY() + 52).withHeight(18), 8.5f, Theme::txt(), 2, juce::Justification::centred);
            txt(g, D[i], r.withY(r.getY() + 72).withHeight(14).reduced(4, 0), 6.5f, Theme::sub(), 1, juce::Justification::centred);
            int act = A[i]; auto full = juce::Rectangle<int>(box.getX() + 16 + i * (bw + 8), box.getY() + 74, bw, 110);
            hits.push_back({ full.toFloat(), [this, act] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<StudioView>(this), act] { if (sp) sp->stationAction(act); }); }, nullptr, nullptr });
        }
    } else if (yEnd < ca.getBottom() - 30) txt(g, U8("drop audio files here · drag loops from the station drawer (◧)"), { ca.getX() + 20, (int) yEnd + 10, 600, 18 }, 8.0f, Theme::sub());
    g.restoreState();
}

void StudioView::paintClip(juce::Graphics& g, const StudioClip& c, int ci, juce::Rectangle<float> r) {
    auto& S = studio.song;
    auto& L = S.lanes[(size_t) c.lane];
    auto col = c.colour >= 0 ? Theme::strip(c.colour % 12) : Theme::strip(L.colour % 12);
    bool sel = ci == selClip || std::find(selClips.begin(), selClips.end(), ci) != selClips.end();
    auto ca = clipArea();
    auto vr = r.getIntersection(ca.toFloat());
    if (vr.isEmpty()) return;
    if (L.frozen) col = col.withSaturation(col.getSaturation() * 0.25f);       // frozen: greyed (UNFREEZE to edit)
    g.setColour(col.withAlpha(c.muted ? 0.12f : (sel ? 0.5f : 0.3f))); g.fillRoundedRectangle(r, 5);
    g.setColour(col.withAlpha(c.muted ? 0.3f : 0.9f)); g.fillRoundedRectangle(r.withHeight(14), 5);
    if (sel) { g.setColour(Theme::txt()); g.drawRoundedRectangle(r, 5, 1.5f); }
    juce::String name = c.name.isNotEmpty() ? c.name : (c.isMidi() ? juce::String("MIDI") : (c.source >= 0 && c.source < (int) S.sources.size() && S.sources[(size_t) c.source] ? S.sources[(size_t) c.source]->name : juce::String()));
    juce::String extra; if (std::abs(c.gainDb) > 0.05f) extra << "  " << juce::String(c.gainDb, 1) << " dB"; if (c.reverse) extra << U8("  ◀ reversed"); if (c.loop) extra << U8("  ⟲");
    if (c.takes.size() > 1 && r.getWidth() > 60) {      // 2.3: TAKES - a key on the clip: which one plays, tap for the others
        auto tb = juce::Rectangle<float>(juce::jmin(r.getRight() - 58.0f, juce::jmax(r.getX(), (float) ca.getX()) + 4.0f), r.getY() + 16, 54, 16);
        if (tb.getX() >= r.getX() && tb.getRight() <= r.getRight()) {
            g.setColour(Theme::txt().withAlpha(0.85f)); g.fillRoundedRectangle(tb, 8);
            txt(g, "TAKE " + juce::String(c.take + 1) + "/" + juce::String((int) c.takes.size()), tb.toNearestInt(), 6.5f, Theme::bg(), 2, juce::Justification::centred);
            hits.push_back({ tb, [this, ci] { selClip = ci; juce::MessageManager::callAsync([sp = juce::Component::SafePointer<StudioView>(this), ci] { if (sp) sp->takesMenu(ci); }); }, nullptr, nullptr });
        }
    }
    if (L.frozen) extra << U8("  ❄");
    txtOn(g, Theme::nameCase(name) + extra, { (int) juce::jmax(r.getX(), (float) ca.getX()) + 5, (int) r.getY(), (int) juce::jmax(10.0f, r.getRight() - juce::jmax(r.getX(), (float) ca.getX()) - 8), 14 }, 7.5f, col.withAlpha(c.muted ? 0.3f : 0.9f).overlaidWith(juce::Colours::transparentBlack));
    auto body = r.withTrimmedTop(15).reduced(0, 2);
    if (!c.isMidi()) {
        if (c.source < 0 || c.source >= (int) S.sources.size() || !S.sources[(size_t) c.source]) return;
        auto* src = S.sources[(size_t) c.source].get();
        auto& pk = peaksOf(src); long sl = src->size(); if (sl <= 0) return;
        float g0 = std::pow(10.0f, c.gainDb / 20.0f);
        g.setColour(Theme::arc(col).withAlpha(c.muted ? 0.4f : 1.0f));
        for (float x = juce::jmax(vr.getX(), r.getX()); x < juce::jmin(vr.getRight(), r.getRight()); x += 1.0f) {
            long smp = (long) (secAt(x) * S.sr) - c.start + c.offset;
            long si = c.loop ? (smp % sl + sl) % sl : smp;
            if (si < 0 || si >= sl) continue;
            if (c.reverse) si = sl - 1 - si;
            float v = juce::jmin(1.0f, pk[(size_t) (si / 256)] * g0);
            float hh = v * body.getHeight() * 0.5f;
            g.fillRect(x, body.getCentreY() - hh, 1.0f, juce::jmax(1.0f, 2 * hh));
        }
        if (c.loop) for (long k = sl - c.offset % sl; k < c.length; k += sl) { float x = xAt((c.start + k) / S.sr); if (x > r.getX() && x < r.getRight()) { g.setColour(Theme::txt().withAlpha(0.2f)); g.fillRect(x, body.getY(), 1.0f, body.getHeight()); } }
    } else {
        if (c.midiLen > 0 && !c.notes.empty()) {
            int lo = 127, hi = 0; for (auto& n : c.notes) { lo = juce::jmin(lo, n.pitch); hi = juce::jmax(hi, n.pitch); }
            int span = juce::jmax(12, hi - lo + 1);
            g.setColour(Theme::textOn(col).withAlpha(0.8f));
            for (long k = 0; k * c.midiLen < c.length + c.offset && k < 512; ++k)
                for (auto& n : c.notes) {
                    double on = n.start + k * c.midiLen - c.offset; if (on + n.len < 0 || on >= c.length) continue;
                    float x = xAt((c.start + juce::jmax(0.0, on)) / S.sr), w = (float) (juce::jmin(n.len, c.length - on) / S.sr * pxPerSec);
                    float yy = body.getBottom() - (n.pitch - lo + 1) * body.getHeight() / span;
                    g.fillRect(x, yy, juce::jmax(1.5f, w), juce::jmax(1.5f, body.getHeight() / span - 1));
                }
        } else txt(g, U8("double-click: draw notes"), body.toNearestInt().reduced(4, 0), 7.5f, Theme::textOn(col));
    }
    g.setColour(Theme::txt().withAlpha(0.6f));
    if (c.fadeIn > 1) g.drawLine(r.getX(), r.getBottom(), xAt((c.start + c.fadeIn) / S.sr), body.getY(), 1.2f);
    if (c.fadeOut > 1) g.drawLine(xAt((c.start + c.length - c.fadeOut) / S.sr), body.getY(), r.getRight(), r.getBottom(), 1.2f);
}

void StudioView::paintAuto(juce::Graphics& g, int li, juce::Rectangle<float> r) {
    auto& L = studio.song.lanes[(size_t) li];
    auto ps = autoParams(L);
    if (L.showAuto >= (int) ps.size()) return;
    auto& P = ps[(size_t) L.showAuto];
    g.setColour(Theme::scr().withMultipliedAlpha(0.6f)); g.fillRect(r);
    const StudioAuto* A = nullptr; for (auto& a : L.autos) if (a.param == P.id) A = &a;
    float fallback = P.id == "vol" ? L.vol : P.id == "pan" ? L.pan : P.id == "sendA" ? L.sendA : P.id == "sendB" ? L.sendB : 0.0f;
    if (P.id.size() > 2 && P.id[0] == 'd') { int di = std::atoi(P.id.c_str() + 1); int pk = std::atoi(P.id.c_str() + P.id.find('p') + 1); if (di < (int) L.devices.size()) fallback = L.devices[(size_t) di].p[(size_t) pk]; }
    auto yOf = [&](float v) { return r.getBottom() - 4 - (v - P.lo) / (P.hi - P.lo) * (r.getHeight() - 8); };
    auto col = Theme::amber();
    txt(g, P.name + (A == nullptr || A->pts.empty() ? U8("  ·  click to add points (alt-click removes)") : juce::String()), { (int) r.getX() + 6, (int) r.getY() + 2, 400, 12 }, 7.0f, Theme::sub(), 1);
    if (A == nullptr || A->pts.empty()) { g.setColour(col.withAlpha(0.5f)); g.drawHorizontalLine((int) yOf(fallback), r.getX(), r.getRight()); return; }
    juce::Path p;
    float x0 = xAt(A->pts.front().t / studio.song.sr);
    p.startNewSubPath(r.getX(), yOf(A->pts.front().v)); p.lineTo(x0, yOf(A->pts.front().v));
    for (auto& pt : A->pts) p.lineTo(xAt(pt.t / studio.song.sr), yOf(pt.v));
    p.lineTo(r.getRight(), yOf(A->pts.back().v));
    g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.5f));
    for (auto& pt : A->pts) { float x = xAt(pt.t / studio.song.sr); if (x < r.getX() - 4 || x > r.getRight() + 4) continue; g.fillEllipse(x - 3.5f, yOf(pt.v) - 3.5f, 7, 7); }
}

// ---------------------------------------------------------------- the mixer
void StudioView::paintMixer(juce::Graphics& g) {
    auto& S = studio.song;
    auto area = headArea().getUnion(clipArea()).withTop((int) topH);
    g.setColour(Theme::bg()); g.fillRect(area);
    int n = (int) S.lanes.size();
    int w = 96, x = area.getX() + 8 - (int) scrollY;
    auto strip = [&](int li, juce::Rectangle<int> r) {
        bool master = li < 0;
        const StudioLane* L = master ? nullptr : &S.lanes[(size_t) li];
        auto col = master ? Theme::txt() : Theme::strip(L->colour % 12);
        g.setColour(!master && li == selLane ? Theme::card2() : Theme::card()); g.fillRoundedRectangle(r.toFloat(), 8);
        g.setColour(col); g.fillRoundedRectangle(r.withHeight(5).toFloat(), 2);
        txt(g, master ? Theme::nameCase("MASTER") : Theme::nameCase(L->name), r.withHeight(22).withTrimmedTop(6).reduced(6, 0), 8.5f, Theme::txt(), 2, juce::Justification::centred);
        int y = r.getY() + 30;
        const auto& devs = master ? S.master : L->devices;
        for (int d = 0; d < (int) devs.size() && d < 5; ++d) {
            auto dr = juce::Rectangle<int>(r.getX() + 6, y, r.getWidth() - 12, 16);
            g.setColour(devs[(size_t) d].on ? Theme::sel().withMultipliedAlpha(0.6f) : Theme::btn()); g.fillRoundedRectangle(dr.toFloat(), 4);
            txt(g, devInfoOf(devs[(size_t) d]).name, dr, 7.0f, Theme::txt(), 1, juce::Justification::centred);
            y += 18;
        }
        y = r.getY() + 30 + 5 * 18 + 6;
        auto bar = [&](const juce::String& lab, float v, float lo, float hi, juce::Colour c, std::function<void(float)> set, float zero = 0) {
            auto br = juce::Rectangle<float>((float) r.getX() + 8, (float) y + 12, (float) r.getWidth() - 16, 8);
            txt(g, lab, { r.getX() + 8, y, r.getWidth() - 16, 11 }, 6.5f, Theme::sub(), 1);
            g.setColour(Theme::scr()); g.fillRoundedRectangle(br, 4);
            float t = (v - lo) / (hi - lo), z = (zero - lo) / (hi - lo);
            g.setColour(Theme::arc(c)); g.fillRoundedRectangle(br.withX(br.getX() + br.getWidth() * juce::jmin(t, z)).withWidth(juce::jmax(2.0f, br.getWidth() * std::abs(t - z))), 4);
            hits.push_back({ br.expanded(0, 6), nullptr, [set, lo, hi, br](juce::Point<float> d, const juce::MouseEvent&) { set(d.x / br.getWidth() * (hi - lo)); }, nullptr });
            y += 26; };
        if (!master) {
            if (L->kind != LaneReturn) {
                bar("SEND A", L->sendA, 0, 1, Theme::blue(), [this, li](float d) { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.sendA = juce::jlimit(0.0f, 1.0f, l.sendA + d); });
                bar("SEND B", L->sendB, 0, 1, Theme::violet(), [this, li](float d) { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.sendB = juce::jlimit(0.0f, 1.0f, l.sendB + d); });
            } else y += 52;
            bar("PAN", L->pan, -1, 1, Theme::txt(), [this, li](float d) { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.pan = juce::jlimit(-1.0f, 1.0f, l.pan + d); });
        } else y += 78;
        auto fr = juce::Rectangle<float>((float) r.getX() + r.getWidth() / 2.0f - 14, (float) y + 6, 12, (float) (r.getBottom() - y - 60));
        float vol = master ? S.masterDb : L->vol;
        g.setColour(Theme::scr()); g.fillRoundedRectangle(fr, 5);
        float t = (vol + 60.0f) / 66.0f;
        g.setColour(Theme::arc(col)); g.fillRoundedRectangle(fr.withTop(fr.getBottom() - fr.getHeight() * t), 5);
        g.setColour(Theme::txt()); g.fillRoundedRectangle(fr.getX() - 6, fr.getBottom() - fr.getHeight() * t - 3, 24, 6, 3);
        float m = master ? juce::jmax(studio.masterMeter.l.load(), studio.masterMeter.r.load()) : (li < 64 ? studio.meters[(size_t) li].l.load() : 0);
        auto mr = juce::Rectangle<float>(fr.getRight() + 10, fr.getY(), 6, fr.getHeight());
        g.setColour(Theme::scr()); g.fillRoundedRectangle(mr, 3);
        g.setColour(m > 0.95f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(mr.withTop(mr.getBottom() - mr.getHeight() * juce::jmin(1.0f, m)), 3);
        hits.push_back({ fr.expanded(10, 4), [this, li] { if (li >= 0) { selLane = li; detail = DLane; } else detail = DMaster; },
            [this, li, fr](juce::Point<float> d, const juce::MouseEvent& e) { const juce::ScopedLock sl(studio.lock); float dv = -d.y / fr.getHeight() * 66.0f * (e.mods.isShiftDown() ? 0.1f : 1.0f);
                if (li < 0) studio.song.masterDb = juce::jlimit(-60.0f, 6.0f, studio.song.masterDb + dv); else { auto& l = studio.song.lanes[(size_t) li]; l.vol = juce::jlimit(-60.0f, 6.0f, l.vol + dv); } }, nullptr });
        txt(g, juce::String(vol, 1) + " dB", { r.getX(), r.getBottom() - 52, r.getWidth(), 14 }, 7.5f, Theme::txt(), 1, juce::Justification::centred);
        if (!master) {
            auto b = [&](juce::Rectangle<float> rr, const juce::String& tt, bool on, juce::Colour c, std::function<void()> f) {
                studioKey(g, rr, tt, on, c, 8.0f); hits.push_back({ rr, f, nullptr, nullptr }); };
            float bx = (float) r.getX() + 8, bw = (r.getWidth() - 24) / 3.0f, by = (float) r.getBottom() - 32;
            b({ bx, by, bw, 22 }, "M", L->mute, Theme::red(), [this, li] { const juce::ScopedLock sl(studio.lock); studio.song.lanes[(size_t) li].mute = !studio.song.lanes[(size_t) li].mute; });
            b({ bx + bw + 4, by, bw, 22 }, "S", L->solo, Theme::blue(), [this, li] { const juce::ScopedLock sl(studio.lock); studio.song.lanes[(size_t) li].solo = !studio.song.lanes[(size_t) li].solo; });
            if (L->kind != LaneReturn) b({ bx + 2 * bw + 8, by, bw, 22 }, U8("●"), L->arm, Theme::red(), [this, li] { const juce::ScopedLock sl(studio.lock); studio.song.lanes[(size_t) li].arm = !studio.song.lanes[(size_t) li].arm; });
        }
        hits.push_back({ r.toFloat(), [this, li] { if (li >= 0) { selLane = li; detail = DLane; selClip = -1; } else detail = DMaster; }, nullptr, nullptr });
    };
    auto h = area.reduced(0, 8);
    for (int li = 0; li < n; ++li) { strip(li, { x, h.getY(), w, h.getHeight() }); x += w + 6; }
    x += 14; strip(-1, { x, h.getY(), w + 10, h.getHeight() });
}

// ---------------------------------------------------------------- the details
juce::Rectangle<int> StudioView::rollArea() const { auto d = detailArea(); return { 230, d.getY() + 30, d.getWidth() - 240, d.getHeight() - 40 }; }
double StudioView::rollX(double t, juce::Rectangle<int> r) const {
    auto& C = studio.song.clips; if (selClip < 0 || selClip >= (int) C.size()) return r.getX();
    double len = (double) juce::jmax(1L, C[(size_t) selClip].midiLen);
    return r.getX() + 36 + t / len * (r.getWidth() - 36);
}
double StudioView::rollT(float x, juce::Rectangle<int> r) const {
    auto& C = studio.song.clips; if (selClip < 0 || selClip >= (int) C.size()) return 0;
    double len = (double) juce::jmax(1L, C[(size_t) selClip].midiLen);
    return (x - r.getX() - 36) / (double) (r.getWidth() - 36) * len;
}

void StudioView::paintPianoRoll(juce::Graphics& g, juce::Rectangle<int> r) {
    auto& S = studio.song; auto& c = S.clips[(size_t) selClip];
    g.setColour(Theme::scr()); g.fillRect(r);
    int rows = (int) (r.getHeight() / rollRowH);
    for (int k = 0; k < rows; ++k) {
        int p = rollTopPitch - k; float y = r.getY() + k * rollRowH;
        bool black = juce::MidiMessage::isMidiNoteBlack(p);
        g.setColour(black ? Theme::bg() : Theme::card()); g.fillRect((float) r.getX() + 36, y, (float) r.getWidth() - 36, rollRowH - 0.5f);
        g.setColour(black ? Theme::bg().darker(0.3f) : Theme::txt().withAlpha(0.85f)); g.fillRect((float) r.getX(), y, 34.0f, rollRowH - 0.5f);
        if (p % 12 == 0) txt(g, noteName(p), { r.getX() + 2, (int) y - 1, 32, (int) rollRowH + 1 }, 6.5f, Theme::bg(), 2);
    }
    double bl = S.beatLen();
    double step = bl / (grid >= 4 ? 4 : grid == 3 ? 2 : 1);
    for (double t = 0; t <= c.midiLen && step > 1; t += step) {
        float x = (float) rollX(t, r); bool bar = std::fmod(t, S.barLen()) < 1;
        g.setColour(Theme::line().withMultipliedAlpha(bar ? 0.9f : 0.3f)); g.fillRect(x, (float) r.getY(), 1.0f, (float) r.getHeight());
    }
    auto col = Theme::strip(S.lanes[(size_t) c.lane].colour % 12);
    for (int i = 0; i < (int) c.notes.size(); ++i) {
        auto& n = c.notes[(size_t) i];
        int row = rollTopPitch - n.pitch; if (row < 0 || row >= rows) continue;
        float x0 = (float) rollX(n.start, r), x1 = (float) rollX(n.start + n.len, r);
        auto nr = juce::Rectangle<float>(x0, r.getY() + row * rollRowH, juce::jmax(3.0f, x1 - x0), rollRowH - 1);
        g.setColour(col.withAlpha(0.35f + 0.65f * n.vel)); g.fillRoundedRectangle(nr, 2);
        if (i == dragNote) { g.setColour(Theme::txt()); g.drawRoundedRectangle(nr, 2, 1.0f); }
    }
    long rel = studio.pos.load() - c.start + c.offset;
    if (rel >= 0 && studio.pos.load() < c.start + c.length && c.midiLen > 0) { float x = (float) rollX((double) (rel % c.midiLen), r); g.setColour(Theme::red()); g.fillRect(x, (float) r.getY(), 1.5f, (float) r.getHeight()); }
}

void StudioView::rebuildDetail() {
    auto& S = studio.song;
    if (detail == DClip && (selClip < 0 || selClip >= (int) S.clips.size())) detail = DLane;
    int key = detail == DClip ? 100000 + selClip : detail == DMaster ? -1 : selLane;
    int kind = detail == DClip ? (S.clips[(size_t) selClip].isMidi() ? 1 : 0) : (detail == DLane && selLane >= 0 && selLane < (int) S.lanes.size() ? 10 + S.lanes[(size_t) selLane].kind : 20);
    int nd = detail == DMaster ? (int) S.master.size() : (detail == DLane && selLane >= 0 && selLane < (int) S.lanes.size() ? (int) S.lanes[(size_t) selLane].devices.size() : 0);
    if (key == builtFor && kind == builtKind && nd == builtDevices) return;
    builtFor = key; builtKind = kind; builtDevices = nd;
    detailKids.clear(); devBoxes.clear(); detailLabels.clear();
    persistentLabels.clear();
    if (!showDetail || getWidth() <= 0) return;
    auto d = detailArea();
    int y0 = d.getY();
    auto pill = [&](juce::Rectangle<int> r, const juce::String& t, std::function<void()> f, std::function<juce::Colour()> col = nullptr) {
        auto* p = addD<Pill>(r, t, col ? col : std::function<juce::Colour()>([] { return Theme::btn(); }), 8.0f, 6.0f); p->onClick = std::move(f); return p; };
    auto knob = [&](juce::Rectangle<int> r, std::function<double()> get, std::function<void(double)> set, double lo, double hi, double def, juce::Colour col) {
        return addD<Knob>(r, std::move(get), std::move(set), lo, hi, def, [col] { return col; }); };
    auto label = [&](juce::Rectangle<int> r, std::function<juce::String()> f, bool title = false) { persistentLabels.push_back({ r, std::move(f), title }); };
    if (detail == DClip) {
        auto clip = [this]() -> StudioClip* { auto& C = studio.song.clips; return selClip >= 0 && selClip < (int) C.size() ? &C[(size_t) selClip] : nullptr; };
        bool midi = S.clips[(size_t) selClip].isMidi();
        int x = 16, y = y0 + 34;
        if (!midi) {
            knob({ x, y, 40, 40 }, [clip] { auto* c = clip(); return c ? (double) c->gainDb : 0.0; }, [this, clip](double v) { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->gainDb = (float) v; }, -36, 18, 0, Theme::amber());
            knob({ x + 56, y, 40, 40 }, [this, clip] { auto* c = clip(); return c ? c->fadeIn / studio.song.sr * 1000.0 : 0.0; }, [this, clip](double v) { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->fadeIn = (float) juce::jmin((double) c->length / 2, v / 1000.0 * studio.song.sr); }, 0, 8000, 4, Theme::amber());
            knob({ x + 112, y, 40, 40 }, [this, clip] { auto* c = clip(); return c ? c->fadeOut / studio.song.sr * 1000.0 : 0.0; }, [this, clip](double v) { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->fadeOut = (float) juce::jmin((double) c->length / 2, v / 1000.0 * studio.song.sr); }, 0, 8000, 4, Theme::amber());
            label({ x - 10, y + 42, 60, 12 }, [clip] { auto* c = clip(); return c ? juce::String(c->gainDb, 1) + " dB" : juce::String(); });
            label({ x + 46, y + 42, 60, 12 }, [] { return juce::String("FADE IN"); }); label({ x + 102, y + 42, 60, 12 }, [] { return juce::String("FADE OUT"); });
            int px = 190, py = y0 + 34;
            pill({ px, py, 84, 24 }, "LOOP", [this, clip] { studio.snapshot(); const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->loop = !c->loop; }, [clip] { auto* c = clip(); return c && c->loop ? Theme::amber() : Theme::btn(); });
            pill({ px + 88, py, 84, 24 }, "REVERSE", [this] { studio.snapshot(); studio.reverseClip(selClip); }, [clip] { auto* c = clip(); return c && c->reverse ? Theme::amber() : Theme::btn(); });
            pill({ px + 176, py, 84, 24 }, "MUTE", [this, clip] { studio.snapshot(); const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->muted = !c->muted; }, [clip] { auto* c = clip(); return c && c->muted ? Theme::red() : Theme::btn(); });
            pill({ px + 264, py, 100, 24 }, "NORMALISE", [this] { studio.snapshot(); studio.normaliseClip(selClip); });
            pill({ px + 368, py, 100, 24 }, "TAKES", [this] { if (selClip >= 0) takesMenu(selClip); })->dynText = [clip] { auto* c = clip(); return c && c->takes.size() > 1 ? "TAKE " + juce::String(c->take + 1) + "/" + juce::String((int) c->takes.size()) + U8(" ▾") : juce::String("1 TAKE"); };
            py += 30;
            const int TR[4] = { -12, -1, 1, 12 };
            for (int i = 0; i < 4; ++i) { int st = TR[i]; pill({ px + i * 58, py, 54, 24 }, (st > 0 ? "+" : "") + juce::String(st), [this, st] { if (!isThreadRunning()) { studio.snapshot(); job = JTranspose; jobClip = selClip; jobSemis = st; progress = 0.01f; jobDone = false; startThread(); } }); }
            label({ px + 236, py, 160, 24 }, [] { return U8("transpose (semitones, same speed)"); });
            py += 30;
            pill({ px, py, 84, 24 }, "SPLIT", [this] { split(); }); pill({ px + 88, py, 84, 24 }, "DUPLICATE", [this] { duplicate(); }); pill({ px + 176, py, 84, 24 }, "DELETE", [this] { removeClip(); });
            pill({ px + 264, py, 100, 24 }, "PRINT FX", [this] { if (!isThreadRunning()) { job = JPrint; jobLane = selLane; progress = 0.01f; jobDone = false; startThread(); } });
            pill({ px + 368, py, 70, 24 }, U8("MORE ⋯"), [this] { if (selClip >= 0) clipMenu(selClip); });
            label({ 16, y0 + 8, 700, 18 }, [this, clip] { auto* c = clip(); if (!c) return juce::String();
                auto& s = studio.song; juce::String n = c->name.isNotEmpty() ? c->name : (c->source >= 0 && c->source < (int) s.sources.size() && s.sources[(size_t) c->source] ? s.sources[(size_t) c->source]->name : juce::String());
                return "CLIP  " + n + U8("  ·  ") + juce::String(c->length / s.barLen(), 2) + U8(" bars  ·  drag its edges to trim, its top corners for fades"); }, true);
        } else {
            int py = y0 + 34;
            pill({ 16, py, 96, 24 }, U8("QUANTISE ▾"), [this] { quantiseMenu(); });
            pill({ 116, py, 50, 24 }, "+OCT", [this, clip] { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) for (auto& n : c->notes) n.pitch = juce::jmin(127, n.pitch + 12); });
            pill({ 170, py, 50, 24 }, "-OCT", [this, clip] { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) for (auto& n : c->notes) n.pitch = juce::jmax(0, n.pitch - 12); });
            py += 30;
            pill({ 16, py, 96, 24 }, U8("LENGTH ×2"), [this, clip] { studio.snapshot(); const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) { c->midiLen *= 2; if (!c->loop) c->length = c->midiLen; } });
            pill({ 116, py, 104, 24 }, U8("LENGTH ÷2"), [this, clip] { studio.snapshot(); const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) { c->midiLen = juce::jmax((long) studio.song.beatLen(), c->midiLen / 2); if (!c->loop) c->length = c->midiLen; } });
            py += 30;
            pill({ 16, py, 96, 24 }, "LOOP", [this, clip] { const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->loop = !c->loop; }, [clip] { auto* c = clip(); return c && c->loop ? Theme::amber() : Theme::btn(); });
            pill({ 116, py, 104, 24 }, "CLEAR NOTES", [this, clip] { studio.snapshot(); const juce::ScopedLock sl(studio.lock); if (auto* c = clip()) c->notes.clear(); });
            py += 30;
            pill({ 16, py, 96, 24 }, "DUPLICATE", [this] { duplicate(); }); pill({ 116, py, 50, 24 }, "DELETE", [this] { removeClip(); }); pill({ 170, py, 50, 24 }, U8("⋯"), [this] { if (selClip >= 0) clipMenu(selClip); });
            label({ 16, y0 + 8, 900, 18 }, [] { return U8("MIDI CLIP  ·  click: a note  ·  drag: move  ·  its right end: length  ·  alt-drag: velocity  ·  double-click: delete  ·  scroll: higher / lower"); }, true);
        }
        return;
    }
    bool master = detail == DMaster || selLane < 0 || selLane >= (int) S.lanes.size();
    int x = 16, y = y0 + 36;
    if (!master) {
        int li = selLane; auto& L = S.lanes[(size_t) li];
        auto lane = [this, li]() -> StudioLane* { auto& LL = studio.song.lanes; return li < (int) LL.size() ? &LL[(size_t) li] : nullptr; };
        auto col = Theme::strip(L.colour % 12);
        knob({ x, y, 40, 40 }, [lane] { auto* l = lane(); return l ? (double) l->vol : 0.0; }, [this, lane](double v) { const juce::ScopedLock sl(studio.lock); if (auto* l = lane()) l->vol = (float) v; }, -60, 6, 0, col);
        knob({ x + 52, y, 40, 40 }, [lane] { auto* l = lane(); return l ? (double) l->pan : 0.0; }, [this, lane](double v) { const juce::ScopedLock sl(studio.lock); if (auto* l = lane()) l->pan = (float) v; }, -1, 1, 0, Theme::txt());
        label({ x - 10, y + 42, 60, 12 }, [lane] { auto* l = lane(); return l ? juce::String(std::abs(l->vol) < 0.05f ? 0.0f : l->vol, 1) + " dB" : juce::String(); });
        label({ x + 42, y + 42, 60, 12 }, [lane] { auto* l = lane(); if (!l) return juce::String(); return std::abs(l->pan) < 0.02f ? juce::String("centre") : (l->pan < 0 ? "L " : "R ") + juce::String((int) std::abs(l->pan * 100)); });
        if (L.kind != LaneReturn) {
            knob({ x + 104, y, 40, 40 }, [lane] { auto* l = lane(); return l ? (double) l->sendA : 0.0; }, [this, lane](double v) { const juce::ScopedLock sl(studio.lock); if (auto* l = lane()) l->sendA = (float) v; }, 0, 1, 0, Theme::blue());
            knob({ x + 156, y, 40, 40 }, [lane] { auto* l = lane(); return l ? (double) l->sendB : 0.0; }, [this, lane](double v) { const juce::ScopedLock sl(studio.lock); if (auto* l = lane()) l->sendB = (float) v; }, 0, 1, 0, Theme::violet());
            label({ x + 94, y + 42, 60, 12 }, [] { return juce::String("REVERB"); }); label({ x + 146, y + 42, 60, 12 }, [] { return juce::String("ECHO"); });
        }
        int by = y + 62;
        if (L.kind == LaneAudio) pill({ x, by, 196, 24 }, "INPUT", [this, li] { inputMenu(li); })->dynText = [lane] { auto* l = lane(); if (!l) return juce::String(); return U8("Records: ") + (l->input < 0 ? juce::String("the station's mix") : l->input < 4 ? "Live " + juce::String(l->input + 1) : "input " + juce::String(l->input - 3)) + U8(" ▾"); };
        {   // 2.3: where it goes and what it sends (any lane)
            int oy = L.kind == LaneInstrument ? by + 56 : L.kind == LaneAudio ? by + 28 : by;
            pill({ x, oy, 196, 24 }, "OUT", [this, li] { routeMenu(li); })->dynText = [this, lane] { auto* l = lane(); if (!l) return juce::String(); auto& LL = studio.song.lanes;
                int ns = (int) l->sends.size(); return U8("Goes to: ") + (l->out >= 0 && l->out < (int) LL.size() ? Theme::nameCase(LL[(size_t) l->out].name) : juce::String("Master")) + (ns > 0 ? U8("  ·  +") + juce::String(ns) + (ns == 1 ? " send" : " sends") : juce::String()) + U8(" ▾"); };
        }
        if (L.kind == LaneInstrument) {
            pill({ x, by, 196, 24 }, U8("TAKE THE SYNTH'S SOUND"), [this, li] { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; for (auto& p : paramTable()) for (auto& dd : p.dests) if (dd == "st_syn") l.synth[p.id] = (float) cv.host.model.get(p.id); report = U8("✓ this lane plays the SYNTH's sound now"); });
            pill({ x, by + 28, 196, 24 }, U8("OPEN THE SYNTH ▸"), [this] { cv.openSynth(true); if (auto* t = cv.getTopLevelComponent()) t->toFront(true); });
        }
        label({ 16, y0 + 8, 400, 18 }, [lane] { auto* l = lane(); return l ? Theme::nameCase(l->name) + U8("  ·  ") + Theme::keyCase(l->kind == LaneInstrument ? "INSTRUMENT" : l->kind == LaneReturn ? "RETURN" : l->kind == LaneBus ? "BUS" : "AUDIO") : juce::String(); }, true);
    } else {
        knob({ x, y, 44, 44 }, [this] { return (double) studio.song.masterDb; }, [this](double v) { const juce::ScopedLock sl(studio.lock); studio.song.masterDb = (float) v; }, -60, 6, 0, Theme::txt());
        label({ x - 8, y + 46, 60, 12 }, [this] { return juce::String(studio.song.masterDb, 1) + " dB"; });
        label({ 16, y0 + 8, 300, 18 }, [] { return juce::String("MASTER"); }, true);
    }
    int dx = 240;
    const auto& devs = master ? S.master : S.lanes[(size_t) selLane].devices;
    int li = master ? -1 : selLane;
    auto devRef = [this, li](int di) -> StudioDevice* { auto& v = li < 0 ? studio.song.master : studio.song.lanes[(size_t) juce::jmin(li, (int) studio.song.lanes.size() - 1)].devices; return di < (int) v.size() ? &v[(size_t) di] : nullptr; };
    for (int di = 0; di < (int) devs.size(); ++di) {
        auto& I = devInfoOf(devs[(size_t) di]);
        int np = (int) I.p.size(), cols = (np + 1) / 2;
        int w = 18 + cols * 54;
        auto box = juce::Rectangle<int>(dx, y0 + 30, w, (int) detailH - 40);
        devBoxes.push_back({ box, di, master });
        pill({ box.getRight() - 64, box.getY() + 4, 28, 18 }, "ON", [this, devRef, di] { const juce::ScopedLock sl(studio.lock); if (auto* dv = devRef(di)) dv->on = !dv->on; }, [devRef, di] { auto* dv = devRef(di); return dv && dv->on ? Theme::green() : Theme::btn(); });
        pill({ box.getRight() - 32, box.getY() + 4, 24, 18 }, U8("✕"), [this, li, di] { studio.snapshot(); { const juce::ScopedLock sl(studio.lock); auto& v = li < 0 ? studio.song.master : studio.song.lanes[(size_t) li].devices; if (di < (int) v.size()) v.erase(v.begin() + di);
            if (li >= 0) { auto& A = studio.song.lanes[(size_t) li].autos; A.erase(std::remove_if(A.begin(), A.end(), [di](const StudioAuto& a) { return a.param.rfind("d" + std::to_string(di) + "p", 0) == 0; }), A.end()); } }
            juce::MessageManager::callAsync([sp = juce::Component::SafePointer<StudioView>(this)] { if (sp) { sp->builtFor = -2; sp->repaint(); } }); });
        if (devs[(size_t) di].type == DevComp || devs[(size_t) di].type == DevGate)      // 2.3: SIDECHAIN
            pill({ box.getX() + 10, box.getBottom() - 28, juce::jmin(150, box.getWidth() - 20), 22 }, "KEY", [this, li, di] { keyMenu(li, di); })->dynText = [this, devRef, di] {
                auto* dv = devRef(di); auto& LL = studio.song.lanes;
                return dv && dv->side >= 0 && dv->side < (int) LL.size() ? U8("Key: ") + Theme::nameCase(LL[(size_t) dv->side].name) + U8(" ▾") : U8("Key: its own sound ▾"); };
        for (int k = 0; k < np; ++k) {
            auto P = I.p[(size_t) k];
            int kx = box.getX() + 12 + (k % cols) * 54, ky = box.getY() + 30 + (k / cols) * 76;
            bool lg = P.log && P.lo > 0;
            knob({ kx + 7, ky, 38, 38 }, [devRef, di, k, lg] { auto* dv = devRef(di); double v = dv ? (double) dv->p[(size_t) k] : 1.0; return lg ? std::log(std::max(1e-6, v)) : v; },
                [this, devRef, di, k, P, lg](double v) { const juce::ScopedLock sl(studio.lock); if (auto* dv = devRef(di)) dv->p[(size_t) k] = P.choices ? (float) std::round(v) : (float) (lg ? std::exp(v) : v); },
                lg ? std::log((double) P.lo) : P.lo, lg ? std::log((double) P.hi) : P.hi, lg ? std::log((double) P.def) : P.def, Theme::fx(k % 2 ? "dly" : "eq"));
            juce::String nm = P.name; juce::String unit = P.unit; auto choices = P.choices; int nc = P.nChoices;
            label({ kx - 4, ky + 40, 60, 11 }, [nm] { return nm; });
            label({ kx - 4, ky + 51, 60, 11 }, [devRef, di, k, unit, choices, nc] { auto* dv = devRef(di); if (!dv) return juce::String(); float v = dv->p[(size_t) k];
                if (choices) return juce::String(choices[juce::jlimit(0, nc - 1, (int) std::lround(v))]);
                return (std::abs(v) >= 100 ? juce::String((int) std::lround(v)) : juce::String(v, std::abs(v) < 10 ? 1 : 0)) + " " + unit; });
        }
        dx += w + 8;
    }
    pill({ dx, y0 + 30, 120, 30 }, "+ EFFECT", [this, li] { addDeviceMenu(li); }, [] { return Theme::sel(); });
    bool anyOn = false; for (auto& dv : devs) anyOn |= dv.on;
    if (li >= 0 && anyOn) pill({ dx, y0 + 68, 120, 30 }, U8("● PRINT FX"), [this, li] { printFx(li); }, [] { return Theme::red(); });
    if (devs.empty()) label({ dx + 132, y0 + 34, 420, 26 }, [] { return U8("ready-made chains (voice, drums, warm …) or one effect at a time · every knob can be automated"); });
    else if (li >= 0) label({ dx + 132, y0 + 72, 380, 26 }, [] { return U8("PRINT FX: the track recorded with its effects (UNDO brings it back)"); });
}

void StudioView::paintDetail(juce::Graphics& g) {
    auto d = detailArea();
    if (Theme::softUI()) softRaised(g, d.toFloat().reduced(10.0f, 8.0f), 24.0f, 8.0f, Theme::card());
    else { g.setColour(Theme::card()); g.fillRect(d);
    g.setColour(Theme::line()); g.fillRect(d.getX(), d.getY(), d.getWidth(), 1); }
    auto& S = studio.song;
    int tx = d.getRight() - 336;
    // as the other choices: a groove with the chosen one raised in it (HIDE is a key of its own)
    if (Theme::softUI()) softWellLite(g, juce::Rectangle<float>((float) tx - 4, (float) d.getY() + 2, 240.0f, 28.0f), 14.0f);
    auto tab = [&](juce::Rectangle<int> r, const juce::String& t, bool on, std::function<void()> f) {
        if (Theme::softUI()) {
            bool own = t.startsWith("HIDE");
            if (own) studioKey(g, r.toFloat(), t, false, Theme::btn(), 7.5f);
            else { if (on) softKeyLite(g, r.toFloat(), r.getHeight() * 0.5f, false);
                   txt(g, Theme::keyCase(t), r, 8.5f, on ? Theme::txt() : Theme::sub(), 1, juce::Justification::centred); }
        } else { g.setColour(on ? Theme::sel() : Theme::btn()); g.fillRoundedRectangle(r.toFloat(), 5); txt(g, t, r, 7.5f, Theme::textOn(on ? Theme::sel() : Theme::btn()), 2, juce::Justification::centred); }
        hits.push_back({ r.toFloat(), f, nullptr, nullptr }); };
    tab({ tx + 246, d.getY() + 6, 80, 20 }, U8("HIDE ▾"), false, [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<StudioView>(this)] { if (sp) { sp->showDetail = false; sp->builtFor = -2; sp->resized(); sp->repaint(); } }); });
    tab({ tx, d.getY() + 6, 76, 20 }, "CLIP", detail == DClip, [this] { if (selClip >= 0) detail = DClip; });
    tab({ tx + 80, d.getY() + 6, 76, 20 }, "LANE", detail == DLane, [this] { detail = DLane; });
    tab({ tx + 160, d.getY() + 6, 76, 20 }, "MASTER", detail == DMaster, [this] { detail = DMaster; });
    for (auto& b : devBoxes) {
        auto& devs = b.master ? S.master : (selLane >= 0 && selLane < (int) S.lanes.size() ? S.lanes[(size_t) selLane].devices : S.master);
        if (b.dev >= (int) devs.size()) continue;
        if (Theme::softUI()) softRaised(g, b.r.toFloat(), 16.0f, 6.0f, Theme::card()); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(b.r.toFloat(), 8); }
        auto& DI = devInfoOf(devs[(size_t) b.dev]);
        txt(g, DI.name, { b.r.getX() + 12, b.r.getY() + 4, b.r.getWidth() - 84, 18 }, 8.5f, devs[(size_t) b.dev].on ? Theme::txt() : Theme::sub(), 2);
        txt(g, DI.desc, { b.r.getX() + 12, b.r.getY() + 19, b.r.getWidth() - 24, 10 }, 6.0f, Theme::sub(), 1);
    }
    for (auto& l : persistentLabels) {
        auto t = l.text();
        bool caption = Theme::softUI() && !l.title && t.isNotEmpty() && t == t.toUpperCase() && t.length() < 16;   // a knob's name: small mono, as on the station
        if (caption) { g.setColour(Theme::sub()); g.setFont(Theme::mono(6.5f)); g.drawFittedText(t, l.r, juce::Justification::centred, 1, 0.8f); }
        else txt(g, t, l.r, l.title ? 10.0f : 7.0f, l.title ? Theme::txt() : Theme::sub(), l.title ? 2 : 1, l.title ? juce::Justification::centredLeft : juce::Justification::centred);
    }
    if (detail == DClip && selClip >= 0 && selClip < (int) S.clips.size() && S.clips[(size_t) selClip].isMidi()) paintPianoRoll(g, rollArea());
}

// the station drawer's buttons and the empty studio's: 1 lay out the scenes, 2 audio lane, 3 instrument, 4 record the station, 5 import
void StudioView::stationAction(int a) {
    auto& S = studio.song;
    if (a == 1) juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Lay out every scene?", "The arrangement is made again from the station's scenes, in order (your STUDIO edits go - UNDO brings them back).", "Lay out", "Cancel", nullptr,
                juce::ModalCallbackFunction::create([this](int r) { if (r == 1) { studio.snapshot(); juce::String why; if (studio.buildFromStation(4, why)) { selClip = -1; builtFor = -2; repaint(); } else report = why; } }));
            else if (a == 2) { studio.snapshot(); int n = 1; for (auto& L : S.lanes) n += L.kind == LaneAudio;
                               selLane = studio.addLane(LaneAudio, "AUDIO " + juce::String(n)); selClip = -1; detail = DLane;
                               report = U8("a new track: ● records into it (choose its input on the lane), or drop a file on it"); }
            else if (a == 3) { studio.snapshot(); int n = 1; for (auto& L : S.lanes) n += L.kind == LaneInstrument;
                               selLane = studio.addLane(LaneInstrument, "SYNTH " + juce::String(n)); selClip = -1; detail = DLane;
                                       report = U8("an instrument lane with the SYNTH's sound - play the keys / MIDI, ● arms it for recording, double-click on it for a MIDI clip"); }
            else if (a == 4) { studio.snapshot(); const juce::ScopedLock sl(studio.lock); selLane = studio.addLane(LaneAudio, "THE STATION"); auto& L = studio.song.lanes[(size_t) selLane]; L.input = -1; L.arm = true; detail = DLane;
                                       report = U8("armed: press ● to record what the station plays (loops, live, effects) into STUDIO"); }
            else if (a == 6) { studio.snapshot(); int n = 1; for (auto& L : S.lanes) n += L.kind == LaneBus;
                               std::vector<int> in; if (selLane >= 0 && selLane < (int) S.lanes.size() && (S.lanes[(size_t) selLane].kind == LaneAudio || S.lanes[(size_t) selLane].kind == LaneInstrument)) in.push_back(selLane);
                               selLane = studio.addBus("BUS " + juce::String(n), in); selClip = -1; detail = DLane;
                               report = U8("a bus: send tracks into it (their ⋯ menu > Goes to) - its effects and fader act on them all, ▾ folds them away"); }
            else if (a == 5) {
                chooser = std::make_unique<juce::FileChooser>("Import audio into STUDIO", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.m4a;*.mid;*.midi");
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems, [this](const juce::FileChooser& fc) {
                    juce::StringArray fs; for (auto& f : fc.getResults()) fs.add(f.getFullPathName());
                    if (!fs.isEmpty()) filesDropped(fs, clipArea().getX() + (int) ((studio.pos.load() / studio.song.sr - scrollSec) * pxPerSec), (int) laneTop(selLane) + 10); });
            }
    builtFor = -2; repaint();
}

// ================================================================== the mouse
void StudioView::mouseMove(const juce::MouseEvent& e) {
    auto p = e.position;
    juce::String h;
    if (clipArea().contains(p.toInt()) && !mixView) h = U8("click: playhead  ·  drag a clip to move (shift: free, alt-drag: a copy)  ·  edges: trim  ·  top corners: fades  ·  right-click: more  ·  double-click on an instrument lane: a MIDI clip");
    else if (rulerArea().contains(p.toInt()) && !mixView) h = U8("click: playhead  ·  drag: the loop region  ·  double-click: a marker");
    else if (headArea().contains(p.toInt()) && !mixView) h = U8("M mute · S solo · ● arm for recording · drag the bar: volume · ⋯ automation, input, colour · double-click the name: rename");
    if (h != hint) { hint = h; repaint(0, 0, getWidth(), (int) topH); }
}

void StudioView::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    auto p = e.position; downAt = p; dragNow = p; drag = None; hitActive = false; report = {};
    auto& S = studio.song;
    if (showBrowser && browserArea().contains(p.toInt())) {
        for (int i = 0; i < (int) browserItems.size(); ++i) if (browserItems[(size_t) i].r.contains(p.toInt())) {
            auto it = browserItems[(size_t) i];
            if (it.action == 0) { drag = Browser; dragItem = i; } else stationAction(it.action);
            builtFor = -2; repaint(); return;
        }
        return;
    }
    for (int i = (int) hits.size(); --i >= 0;) if (hits[(size_t) i].r.contains(p)) {
        if (e.mods.isPopupMenu() && headArea().contains(p.toInt()) && !mixView) { int li = laneAt(p.y); if (li >= 0) laneMenu(li); return; }
        activeHit = hits[(size_t) i]; hitActive = activeHit.drag != nullptr;
        if (activeHit.click) activeHit.click();
        builtFor = -2; repaint(); return;
    }
    if (mixView) return;
    if (detail == DClip && selClip >= 0 && selClip < (int) S.clips.size() && S.clips[(size_t) selClip].isMidi() && rollArea().contains(p.toInt())) {
        auto r = rollArea();
        int pitch = rollTopPitch - (int) ((p.y - r.getY()) / rollRowH);
        double t = rollT(p.x, r);
        if (p.x < r.getX() + 36) { studio.midiNote(pitch, 0.8f, true); juce::Timer::callAfterDelay(300, [this, pitch] { studio.midiNote(pitch, 0, false); }); return; }
        studio.snapshot();
        const juce::ScopedLock sl(studio.lock);
        auto& c = S.clips[(size_t) selClip];
        dragNote = -1;
        for (int i = (int) c.notes.size(); --i >= 0;) { auto& n = c.notes[(size_t) i]; if (n.pitch == pitch && t >= n.start && t <= n.start + n.len) { dragNote = i; break; } }
        if (dragNote < 0) {
            double q = S.beatLen() / (grid >= 4 ? 4 : grid == 3 ? 2 : 1); if (grid == 0) q = S.beatLen() / 4;
            StudioNote n; n.start = std::max(0.0, std::floor(t / q) * q); n.len = q; n.pitch = pitch; n.vel = 0.8f;
            c.notes.push_back(n); dragNote = (int) c.notes.size() - 1; drag = NoteLen;
        } else {
            auto& n = c.notes[(size_t) dragNote];
            float x1 = (float) rollX(n.start + n.len, r);
            drag = e.mods.isAltDown() ? NoteVel : (p.x > x1 - 6 ? NoteLen : NoteMove);
        }
        dragNoteStart = c.notes[(size_t) dragNote].start; dragNoteLen = c.notes[(size_t) dragNote].len; dragPitch = c.notes[(size_t) dragNote].pitch; dragVel = c.notes[(size_t) dragNote].vel;
        repaint(); return;
    }
    if (rulerArea().contains(p.toInt())) {
        long at = snapTo(sampleAt(p.x), e.mods.isShiftDown());
        if (e.mods.isAltDown()) { studio.snapshot(); drag = PunchMake; dragStart = at; }      // 2.3: alt-drag: the punch points
        else if (p.y > rulerArea().getBottom() - 12 && S.loopEnd > S.loopStart && at > S.loopStart && at < S.loopEnd) { drag = LoopMove; dragStart = S.loopStart; dragLen = S.loopEnd - S.loopStart; }
        else { drag = Seek; studio.pos = sampleAt(p.x); dragStart = at; }
        repaint(); return;
    }
    if (!clipArea().contains(p.toInt())) return;
    bool inAuto = false;
    int li = laneAt(p.y, &inAuto);
    if (inAuto && li >= 0) {
        auto& L = S.lanes[(size_t) li]; auto ps = autoParams(L);
        if (L.showAuto >= 0 && L.showAuto < (int) ps.size()) {
            auto P = ps[(size_t) L.showAuto];
            studio.snapshot();
            const juce::ScopedLock sl(studio.lock);
            StudioAuto* A = nullptr; for (auto& a : L.autos) if (a.param == P.id) A = &a;
            if (A == nullptr) { L.autos.push_back({ P.id, {} }); A = &L.autos.back(); }
            float top = laneTop(li) + L.height;
            auto vOf = [&](float yy) { return juce::jlimit(P.lo, P.hi, P.lo + (top + autoH - 4 - yy) / (autoH - 8) * (P.hi - P.lo)); };
            dragPt = -1;
            for (int i = 0; i < (int) A->pts.size(); ++i) if (std::abs(xAt(A->pts[(size_t) i].t / S.sr) - p.x) < 6) dragPt = i;
            if (dragPt >= 0 && e.mods.isAltDown()) { A->pts.erase(A->pts.begin() + dragPt); dragPt = -1; repaint(); return; }
            if (dragPt < 0) { AutoPoint ap { (double) snapTo(sampleAt(p.x), e.mods.isShiftDown()), vOf(p.y) }; auto it = std::upper_bound(A->pts.begin(), A->pts.end(), ap.t, [](double x, const AutoPoint& q) { return x < q.t; });
                dragPt = (int) (A->pts.insert(it, ap) - A->pts.begin()); }
            drag = AutoPt; dragLane = li; repaint(); return;
        }
    }
    int found = -1;
    for (int ci = (int) S.clips.size(); --ci >= 0;) {
        auto& c = S.clips[(size_t) ci];
        if (c.lane != li) continue;
        float x0 = xAt(c.start / S.sr), x1 = xAt((c.start + c.length) / S.sr);
        if (p.x < x0 - 3 || p.x > x1 + 3) continue;
        found = ci; break;
    }
    if (e.mods.isPopupMenu()) { if (found >= 0) { selClip = found; clipMenu(found); } else if (li >= 0) laneMenu(li); return; }
    if (found >= 0 && li >= 0 && li < (int) S.lanes.size() && S.lanes[(size_t) li].frozen) {    // 2.3: a frozen lane is played, not edited
        selClip = -1; selClips.clear(); selLane = li; detail = DLane;
        report = U8("❄ this lane is frozen - UNFREEZE it (its ⋯ menu) to edit its clips");
        builtFor = -2; repaint(); return;
    }
    if (found >= 0) {
        if (e.mods.isShiftDown() && selClip >= 0 && found != selClip) { if (std::find(selClips.begin(), selClips.end(), selClip) == selClips.end()) selClips.push_back(selClip); if (std::find(selClips.begin(), selClips.end(), found) == selClips.end()) selClips.push_back(found); }
        else if (std::find(selClips.begin(), selClips.end(), found) == selClips.end()) selClips.clear();
        selClip = found; selLane = li; detail = DClip;
        studio.snapshot();
        if (e.mods.isAltDown()) { const juce::ScopedLock sl(studio.lock); auto copy = S.clips[(size_t) found]; S.clips.push_back(copy); selClip = (int) S.clips.size() - 1; selClips.clear(); }
        auto& cc = S.clips[(size_t) selClip];
        float x0 = xAt(cc.start / S.sr), x1 = xAt((cc.start + cc.length) / S.sr), y = laneTop(li) + 2;
        bool top = p.y < y + 14;
        dragStart = cc.start; dragLen = cc.length; dragOff = cc.offset; dragLane = cc.lane;
        dragGroup.clear(); for (int o : selClips) if (o != selClip && o < (int) S.clips.size()) dragGroup.push_back({ o, S.clips[(size_t) o].start });
        if (top && p.x < x0 + 10) { drag = FadeIn; dragFade = cc.fadeIn; }
        else if (top && p.x > x1 - 10) { drag = FadeOut; dragFade = cc.fadeOut; }
        else if (p.x < x0 + 6) drag = TrimL;
        else if (p.x > x1 - 6) drag = TrimR;
        else drag = Move;
    } else {
        selClip = -1; selClips.clear();
        if (li >= 0) { selLane = li; detail = DLane; }
        studio.pos = snapTo(sampleAt(p.x), e.mods.isShiftDown());
    }
    builtFor = -2; repaint();
}

void StudioView::mouseDrag(const juce::MouseEvent& e) {
    auto& S = studio.song;
    auto delta = e.position - downAt;
    if (hitActive) { activeHit.drag(e.position - dragNow, e); dragNow = e.position; repaint(); return; }
    dragNow = e.position;
    if (drag == Browser) { repaint(); return; }
    double dsec = delta.x / pxPerSec; long d = (long) (dsec * S.sr);
    bool fine = e.mods.isShiftDown();
    const juce::ScopedLock sl(studio.lock);
    if (drag == Seek) {
        long at = snapTo(sampleAt(e.position.x), fine);
        if (std::abs(delta.x) > 6) { S.loopStart = std::min(dragStart, at); S.loopEnd = std::max(dragStart, at); S.loopOn = S.loopEnd > S.loopStart; }
        else studio.pos = sampleAt(e.position.x);
    } else if (drag == PunchMake) {
        long at = snapTo(sampleAt(e.position.x), fine);
        S.punchStart = std::min(dragStart, at); S.punchEnd = std::max(dragStart, at); S.punchOn = S.punchEnd > S.punchStart + 64;
    } else if (drag == LoopMove) { S.loopStart = std::max(0L, snapTo(dragStart + d, fine)); S.loopEnd = S.loopStart + dragLen; }
    else if (drag == AutoPt && dragLane < (int) S.lanes.size()) {
        auto& L = S.lanes[(size_t) dragLane]; auto ps = autoParams(L);
        if (L.showAuto >= 0 && L.showAuto < (int) ps.size()) {
            auto& P = ps[(size_t) L.showAuto];
            for (auto& A : L.autos) if (A.param == P.id && dragPt >= 0 && dragPt < (int) A.pts.size()) {
                float top = laneTop(dragLane) + L.height;
                auto& pt = A.pts[(size_t) dragPt];
                pt.v = juce::jlimit(P.lo, P.hi, P.lo + (top + autoH - 4 - e.position.y) / (autoH - 8) * (P.hi - P.lo));
                double t = (double) snapTo(sampleAt(e.position.x), fine);
                double lo = dragPt > 0 ? A.pts[(size_t) dragPt - 1].t : 0, hi = dragPt + 1 < (int) A.pts.size() ? A.pts[(size_t) dragPt + 1].t : 1e18;
                pt.t = juce::jlimit(lo, hi, t);
            }
        }
    } else if ((drag == NoteMove || drag == NoteLen || drag == NoteVel) && selClip >= 0 && selClip < (int) S.clips.size()) {
        auto& c = S.clips[(size_t) selClip]; auto r = rollArea();
        if (dragNote >= 0 && dragNote < (int) c.notes.size()) {
            auto& n = c.notes[(size_t) dragNote];
            double q = S.beatLen() / (grid >= 4 ? 4 : grid == 3 ? 2 : 1); if (grid == 0 || fine) q = 1;
            double dt = rollT(e.position.x, r) - rollT(downAt.x, r);
            if (drag == NoteMove) { n.start = juce::jlimit(0.0, (double) c.midiLen - 1, std::round((dragNoteStart + dt) / q) * q); n.pitch = juce::jlimit(0, 127, dragPitch - (int) std::round(delta.y / rollRowH)); }
            else if (drag == NoteLen) n.len = juce::jmax(q > 1 ? q : 64.0, std::round((dragNoteLen + dt) / q) * q);
            else n.vel = juce::jlimit(0.05f, 1.0f, dragVel - delta.y / 100.0f);
        }
    } else if (selClip >= 0 && selClip < (int) S.clips.size()) {
        auto& c = S.clips[(size_t) selClip];
        long big = 1L << 40;
        long maxLen = c.loop ? big : (c.isMidi() ? c.midiLen - c.offset : (c.source >= 0 && c.source < (int) S.sources.size() && S.sources[(size_t) c.source] ? S.sources[(size_t) c.source]->size() - c.offset : big));
        if (drag == Move) {
            long ns = std::max(0L, fine ? dragStart + d : snapTo(dragStart + d, false));
            c.start = ns;
            for (auto& gp : dragGroup) if (gp.first < (int) S.clips.size()) S.clips[(size_t) gp.first].start = std::max(0L, gp.second + (ns - dragStart));
            int li = laneAt(e.position.y); if (li >= 0 && li < (int) S.lanes.size() && S.lanes[(size_t) li].kind != LaneReturn && !S.lanes[(size_t) li].frozen && (S.lanes[(size_t) li].kind == LaneInstrument) == c.isMidi()) c.lane = li;
        } else if (drag == TrimL) {
            long ns = juce::jlimit(0L, dragStart + dragLen - 256, fine ? dragStart + d : snapTo(dragStart + d, false));
            long dd = ns - dragStart; if (dragOff + dd < 0 && !c.loop) { dd = -dragOff; ns = dragStart + dd; }
            c.start = ns; c.length = dragLen - dd; c.offset = std::max(0L, dragOff + dd);
        } else if (drag == TrimR) {
            long ne = std::max(dragStart + 256, fine ? dragStart + dragLen + d : snapTo(dragStart + dragLen + d, false));
            c.length = std::min(ne - dragStart, maxLen);
        } else if (drag == FadeIn) c.fadeIn = (float) juce::jlimit(0.0, (double) c.length * 0.5, (double) dragFade + d);
        else if (drag == FadeOut) c.fadeOut = (float) juce::jlimit(0.0, (double) c.length * 0.5, (double) dragFade - d);
    }
    repaint();
}

void StudioView::mouseUp(const juce::MouseEvent& e) {
    if (drag == Browser && dragItem >= 0 && dragItem < (int) browserItems.size()) {
        auto it = browserItems[(size_t) dragItem];
        if (clipArea().contains(e.getPosition())) {
            long at = snapTo(sampleAt(e.position.x), false);
            juce::String why; bool ok;
            if (it.track < 0) ok = studio.addStationScene(it.scene, at, 1, why);
            else ok = studio.addStationLoop(it.scene, it.track, at, laneAt(e.position.y), why);
            { const juce::ScopedLock sl(studio.lock); studio.ensureReturns(); }
            report = ok ? U8("✓ ") + it.text.upToFirstOccurrenceOf(U8(" ▸"), false, false) + U8(" added - drag its right edge to make it longer") : why;
        }
        dragItem = -1;
    }
    if (drag == NoteLen || drag == NoteMove) {
        auto& S = studio.song;
        if (selClip >= 0 && selClip < (int) S.clips.size() && dragNote >= 0 && dragNote < (int) S.clips[(size_t) selClip].notes.size()) {
            int p = S.clips[(size_t) selClip].notes[(size_t) dragNote].pitch; studio.midiNote(p, 0.7f, true);
            juce::Timer::callAfterDelay(180, [sp = juce::Component::SafePointer<StudioView>(this), p] { if (sp) sp->studio.midiNote(p, 0, false); }); }
    }
    drag = None; hitActive = false; dragNote = -1;
    repaint();
}

void StudioView::mouseDoubleClick(const juce::MouseEvent& e) {
    auto p = e.position; auto& S = studio.song;
    for (int i = (int) hits.size(); --i >= 0;) if (hits[(size_t) i].r.contains(p) && hits[(size_t) i].dbl) { hits[(size_t) i].dbl(); return; }
    if (detail == DClip && selClip >= 0 && selClip < (int) S.clips.size() && S.clips[(size_t) selClip].isMidi() && rollArea().contains(p.toInt())) {
        auto r = rollArea(); auto& c = S.clips[(size_t) selClip];
        int pitch = rollTopPitch - (int) ((p.y - r.getY()) / rollRowH); double t = rollT(p.x, r);
        const juce::ScopedLock sl(studio.lock);
        for (int i = (int) c.notes.size(); --i >= 0;) { auto& n = c.notes[(size_t) i]; if (n.pitch == pitch && t >= n.start && t <= n.start + n.len) { c.notes.erase(c.notes.begin() + i); break; } }
        repaint(); return;
    }
    if (rulerArea().contains(p.toInt())) {
        studio.snapshot(); const juce::ScopedLock sl(studio.lock);
        S.markers.push_back({ snapTo(sampleAt(p.x), false), "MARK " + juce::String((int) S.markers.size() + 1) }); repaint(); return;
    }
    if (!clipArea().contains(p.toInt())) return;
    int li = laneAt(p.y);
    if (li < 0 || li >= (int) S.lanes.size()) return;
    if (S.lanes[(size_t) li].kind == LaneInstrument) {
        if (selClip >= 0 && selClip < (int) S.clips.size() && S.clips[(size_t) selClip].lane == li) { detail = DClip; showDetail = true; resized(); return; }
        newMidiClip(li, snapTo(sampleAt(p.x), false)); return;
    }
    if (selClip >= 0 && selClip < (int) S.clips.size()) { showDetail = true; detail = DClip; resized(); }
}

void StudioView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    auto& S = studio.song;
    if (detail == DClip && selClip >= 0 && selClip < (int) S.clips.size() && S.clips[(size_t) selClip].isMidi() && rollArea().contains(e.getPosition())) {
        rollTopPitch = juce::jlimit(24, 127, rollTopPitch + (w.deltaY > 0 ? 2 : -2)); repaint(); return; }
    if (mixView) { scrollY = juce::jmax(0.0f, scrollY - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 300.0f); repaint(); return; }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) {
        double at = secAt(e.position.x);
        pxPerSec = juce::jlimit(2.0, 1200.0, pxPerSec * (w.deltaY > 0 ? 1.15 : 1 / 1.15));
        scrollSec = juce::jmax(0.0, at - (e.position.x - clipArea().getX()) / pxPerSec);
    } else if (e.mods.isShiftDown() || std::abs(w.deltaX) > std::abs(w.deltaY)) scrollSec = juce::jmax(0.0, scrollSec - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 600.0 / pxPerSec);
    else {
        float total = laneTop((int) S.lanes.size()) + scrollY - clipArea().getY();
        scrollY = juce::jlimit(0.0f, juce::jmax(0.0f, total - clipArea().getHeight() + 40), scrollY - w.deltaY * 300.0f);
    }
    repaint();
}

bool StudioView::keyPressed(const juce::KeyPress& k) {
    auto c = k.getKeyCode(); bool cmd = k.getModifiers().isCommandDown();
    if (c == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (c == 'R' && !cmd) { toggleRecord(); return true; }
    if (c == 'L' && !cmd) { const juce::ScopedLock sl(studio.lock); auto& s = studio.song; if (s.loopEnd <= s.loopStart + 64) { s.loopStart = 0; s.loopEnd = (long) (s.barLen() * 4); } s.loopOn = !s.loopOn; repaint(); return true; }
    if (c == 'M' && !cmd) { studio.song.metronome = !studio.song.metronome; return true; }
    if (c == 'S' && !cmd) { split(); return true; }
    if (c == 'D' && cmd) { duplicate(); return true; }
    if (c == 'C' && cmd) { copySel(false); return true; }
    if (c == 'X' && cmd) { copySel(true); return true; }
    if (c == 'V' && cmd) { paste(); return true; }
    if (c == 'T' && !cmd) { if (selClip >= 0 && selClip < (int) studio.song.clips.size() && studio.song.clips[(size_t) selClip].takes.size() > 1) {   // the next take
                                auto& cc = studio.song.clips[(size_t) selClip]; studio.chooseTake(selClip, (cc.take + 1) % (int) cc.takes.size()); repaint(); } return true; }
    if (c == 'P' && !cmd) { togglePunch(); return true; }
    if (c == 'E' && cmd) { doExport(); return true; }
    if (c == 'Z' && cmd && k.getModifiers().isShiftDown()) { if (studio.redo()) { selClip = -1; repaint(); } return true; }
    if (c == 'Z' && cmd) { if (studio.undo()) { selClip = -1; repaint(); } return true; }
    if (c == 'Y' && cmd) { if (studio.redo()) { selClip = -1; repaint(); } return true; }
    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) { removeClip(); return true; }
    if (c == juce::KeyPress::homeKey || c == juce::KeyPress::returnKey) { studio.pos = studio.song.loopOn ? studio.song.loopStart : 0; scrollSec = 0; repaint(); return true; }
    if (c == juce::KeyPress::escapeKey) { studio.stop(); if (onClose) onClose(); return true; }
    if (c == '=' || c == '+') { pxPerSec = juce::jmin(1200.0, pxPerSec * 1.5); repaint(); return true; }
    if (c == '-') { pxPerSec = juce::jmax(2.0, pxPerSec / 1.5); repaint(); return true; }
    if (c == juce::KeyPress::tabKey) { mixView = !mixView; repaint(); return true; }
    return false;
}

// ================================================================== actions
void StudioView::togglePlay() {
    if (studio.playing.load() || studio.countInLeft.load() > 0) studio.stop(); else studio.play();
    grabKeyboardFocus(); repaint();
}
void StudioView::toggleRecord() {
    if (studio.recording.load() || studio.countInLeft.load() > 0) { studio.stopRecording(); studio.playing = false; report = studio.lastRecordName.isNotEmpty() ? U8("✓ recorded: ") + studio.lastRecordName : juce::String(); repaint(); return; }
    bool armed = false; for (auto& L : studio.song.lanes) armed |= L.arm;
    if (!armed) {
        // nothing armed: record straight away - into the selected audio track, or into a new track
        auto& S = studio.song; int li = selLane;
        if (li < 0 || li >= (int) S.lanes.size() || S.lanes[(size_t) li].kind != LaneAudio) {
            int n = 1; for (auto& L : S.lanes) n += L.kind == LaneAudio;
            studio.ensureReturns();
            li = studio.addLane(LaneAudio, "TAKE " + juce::String(n));
            S.lanes[(size_t) li].input = 0; selLane = li;
            report = U8("● recording into a new track (LIVE 1) - change its input on the lane");
        } else report = U8("● recording into ") + S.lanes[(size_t) li].name;
        S.lanes[(size_t) li].arm = true;
        studio.collectRecordings();
    }
    studio.startRecording(); repaint();
}

void StudioView::split() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size()) { report = "choose a clip first"; return; }
    long at = studio.pos.load();
    auto c = S.clips[(size_t) selClip];
    if (at <= c.start + 64 || at >= c.start + c.length - 64) { report = U8("put the playhead inside the clip (click the timeline), then SPLIT (S)"); repaint(); return; }
    studio.snapshot();
    const juce::ScopedLock sl(studio.lock);
    auto& a = S.clips[(size_t) selClip];
    StudioClip b = a;
    long firstLen = at - a.start;
    a.length = firstLen; a.fadeOut = (float) (0.002 * S.sr);
    b.start = at; b.length = c.length - firstLen; b.offset = c.offset + firstLen; b.fadeIn = (float) (0.002 * S.sr);
    S.clips.push_back(b);
    repaint();
}
void StudioView::duplicate() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size()) return;
    studio.snapshot();
    const juce::ScopedLock sl(studio.lock);
    std::vector<int> all = selClips; if (std::find(all.begin(), all.end(), selClip) == all.end()) all.push_back(selClip);
    long lo = LONG_MAX, hi = 0; for (int i : all) { lo = std::min(lo, S.clips[(size_t) i].start); hi = std::max(hi, S.clips[(size_t) i].start + S.clips[(size_t) i].length); }
    selClips.clear();
    for (int i : all) { StudioClip b = S.clips[(size_t) i]; b.start += hi - lo; S.clips.push_back(b); selClips.push_back((int) S.clips.size() - 1); }
    selClip = selClips.back();
    repaint();
}
void StudioView::removeClip() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size()) return;
    studio.removeClips(chosenClips());                  // 2.3: with RIPPLE as set (EDIT menu)
    selClip = -1; selClips.clear(); detail = DLane;
    builtFor = -2; repaint();
}

void StudioView::newMidiClip(int li, long at) {
    studio.snapshot();
    { const juce::ScopedLock sl(studio.lock);
      auto& S = studio.song;
      StudioClip c; c.lane = li; c.source = -1; c.start = std::max(0L, at); c.midiLen = (long) S.barLen(); c.length = c.midiLen * 2; c.loop = true; c.name = "MIDI";
      S.clips.push_back(c); selClip = (int) S.clips.size() - 1; selLane = li; }
    detail = DClip; showDetail = true; resized(); repaint();
}

void StudioView::clipMenu(int ci) {
    juce::PopupMenu m;
    auto& c = studio.song.clips[(size_t) ci];
    m.addItem(1, "Split at the playhead  (S)"); m.addItem(2, U8("Duplicate  (⌘D)")); m.addItem(3, "Delete");
    m.addSeparator();
    m.addItem(4, "Loop", true, c.loop); m.addItem(5, "Mute", true, c.muted);
    if (!c.isMidi()) { m.addItem(6, "Reverse", true, c.reverse); m.addItem(7, "Normalise (-1 dB)"); m.addItem(8, "Transpose +12"); m.addItem(9, "Transpose -12"); m.addItem(10, "Print the station's effects"); }
    if (c.takes.size() > 1) m.addItem(12, U8("Takes …  (") + juce::String((int) c.takes.size()) + U8(", T = the next)"));
    if (c.isMidi()) { m.addItem(13, U8("Quantise …")); m.addItem(14, U8("Export as a MIDI file …")); }
    m.addItem(15, U8("Copy  (⌘C)")); m.addItem(16, U8("Cut  (⌘X)"));
    m.addItem(11, "Rename");
    juce::PopupMenu cols; for (int i = 0; i < 12; ++i) cols.addItem(100 + i, "colour " + juce::String(i + 1)); m.addSubMenu("Colour", cols);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, ci](int r) {
        if (r <= 0 || ci >= (int) studio.song.clips.size()) return;
        selClip = ci;
        if (r == 12) { takesMenu(ci); return; }
        if (r == 13) { quantiseMenu(); return; }
        if (r == 15 || r == 16) { copySel(r == 16); return; }
        if (r == 14) {
            chooser = std::make_unique<juce::FileChooser>("Export MIDI", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile((studio.song.clips[(size_t) ci].name.isNotEmpty() ? studio.song.clips[(size_t) ci].name : juce::String("ARCO")) + ".mid"), "*.mid");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, ci](const juce::FileChooser& fc) {
                auto f = fc.getResult(); if (f == juce::File()) return;
                report = studio.exportMidi(ci, f.withFileExtension("mid")) ? U8("✓ MIDI saved: ") + f.withFileExtension("mid").getFileName() : U8("could not save the MIDI file"); repaint(); });
            return;
        }
        if (r == 1) split(); else if (r == 2) duplicate(); else if (r == 3) removeClip();
        else if (r == 4 || r == 5 || r >= 100) { studio.snapshot(); const juce::ScopedLock sl(studio.lock); auto& cc = studio.song.clips[(size_t) ci]; if (r == 4) cc.loop = !cc.loop; else if (r == 5) cc.muted = !cc.muted; else cc.colour = r - 100; }
        else if (r == 6) { studio.snapshot(); studio.reverseClip(ci); }
        else if (r == 7) { studio.snapshot(); studio.normaliseClip(ci); }
        else if ((r == 8 || r == 9) && !isThreadRunning()) { studio.snapshot(); job = JTranspose; jobClip = ci; jobSemis = r == 8 ? 12 : -12; progress = 0.01f; jobDone = false; startThread(); }
        else if (r == 10 && !isThreadRunning()) { job = JPrint; jobLane = studio.song.clips[(size_t) ci].lane; progress = 0.01f; jobDone = false; startThread(); }
        else if (r == 11) {
            auto* w = new juce::AlertWindow("Clip name", "", juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", studio.song.clips[(size_t) ci].name);
            w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, ci](int ok) { if (ok == 1 && ci < (int) studio.song.clips.size()) { const juce::ScopedLock sl(studio.lock); studio.song.clips[(size_t) ci].name = w->getTextEditorContents("n").substring(0, 40); } repaint(); }), true);
        }
        builtFor = -2; repaint();
    });
}

void StudioView::laneMenu(int li) {
    juce::PopupMenu m;
    auto& L = studio.song.lanes[(size_t) li];
    if (L.kind != LaneReturn) { m.addItem(20, U8("Effects …")); bool anyOn = false; for (auto& dv : L.devices) anyOn |= dv.on;
                                m.addItem(21, U8("● Print the effects into the sound"), anyOn); m.addSeparator(); }
    m.addItem(23, U8("Routing …  (goes to ") + (L.out >= 0 && L.out < (int) studio.song.lanes.size() ? studio.song.lanes[(size_t) L.out].name : juce::String("MASTER")) + U8(", sends, bus)"));
    if (L.kind == LaneBus) m.addItem(24, L.folded ? U8("▾ Show its tracks") : U8("▸ Fold its tracks away"));
    if (L.kind != LaneReturn && L.kind != LaneBus) m.addItem(22, L.frozen ? U8("❄ Unfreeze (edit it again)") : U8("❄ Freeze (play it from a recording of itself - light)"));
    m.addItem(7, U8("Automation …"), true, L.showAuto >= 0);
    if (L.kind == LaneAudio) { m.addItem(8, U8("What it records …")); m.addItem(9, U8("Hear the input through it"), true, L.monitor); }
    m.addSeparator();
    m.addItem(1, "Rename"); m.addItem(2, "Duplicate the lane"); if (L.kind != LaneReturn) m.addItem(3, "Delete the lane");
    m.addSeparator();
    m.addItem(4, "Taller"); m.addItem(5, "Smaller");
    m.addItem(6, "Export this lane (stem)");
    juce::PopupMenu cols; for (int i = 0; i < 12; ++i) cols.addItem(100 + i, "colour " + juce::String(i + 1)); m.addSubMenu("Colour", cols);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, li](int r) {
        if (r <= 0 || li >= (int) studio.song.lanes.size()) return;
        if (r == 1) { rename(li); return; }
        if (r == 20) { selLane = li; selClip = -1; detail = DLane; showDetail = true; builtFor = -2; resized(); addDeviceMenu(li); return; }
        if (r == 21) { printFx(li); return; }
        if (r == 23) { routeMenu(li); return; }
        if (r == 24) { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.folded = !l.folded; repaint(); return; }
        if (r == 22) {
            if (studio.song.lanes[(size_t) li].frozen) { studio.unfreezeLane(li); report = U8("✓ unfrozen - its clips, synth and effects play again"); }
            else { juce::String why; report = studio.freezeLane(li, why) ? U8("❄ frozen - it plays from a recording of itself (UNFREEZE to edit)") : why; }
            builtFor = -2; repaint(); return;
        }
        if (r == 7) { autoMenu(li); return; }
        if (r == 8) { inputMenu(li); return; }
        if (r == 9) { const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li]; l.monitor = !l.monitor; repaint(); return; }
        if (r == 6) { if (!isThreadRunning()) { jobLane = li; job = JStems; doExport(); } return; }
        studio.snapshot();
        { const juce::ScopedLock sl(studio.lock); auto& S = studio.song;
          if (r == 2) { int d = studio.duplicateLane(li); if (d >= 0) selLane = d; }
          else if (false) { auto copy = S.lanes[(size_t) li]; copy.name << " 2"; S.lanes.insert(S.lanes.begin() + li + 1, copy); studio.laneStrip.insert(studio.laneStrip.begin() + juce::jmin(li + 1, (int) studio.laneStrip.size()), -1); studio.laneScene.insert(studio.laneScene.begin() + juce::jmin(li + 1, (int) studio.laneScene.size()), 1);
                        for (auto& c : S.clips) if (c.lane > li) ++c.lane; }
          else if (r == 3) { studio.deleteLane(li); selLane = juce::jmax(0, li - 1); selClip = -1; }
          else if (r == 4) S.lanes[(size_t) li].height = juce::jmin(300.0f, S.lanes[(size_t) li].height * 1.5f);
          else if (r == 5) S.lanes[(size_t) li].height = juce::jmax(40.0f, S.lanes[(size_t) li].height / 1.5f);
          else if (r >= 100) S.lanes[(size_t) li].colour = r - 100; }
        studio.refresh();                             // the engine takes the new lanes (still playing, if it was)
        builtFor = -2; repaint();
    });
}

void StudioView::inputMenu(int li) {
    juce::PopupMenu m;
    auto& L = studio.song.lanes[(size_t) li];
    m.addSectionHeader("This lane records");
    for (int i = 0; i < 4; ++i) m.addItem(10 + i, "LIVE " + juce::String(i + 1) + " (the station's input, dry)", true, L.input == i);
    int nIn = 0; if (auto* d = cv.host.devices.getCurrentAudioDevice()) nIn = d->getActiveInputChannels().countNumberOfSetBits();
    juce::PopupMenu card; for (int i = 0; i < juce::jmax(2, nIn); ++i) card.addItem(20 + i, "input " + juce::String(i + 1), true, L.input == 4 + i);
    m.addSubMenu("The sound card", card);
    m.addItem(5, U8("The station's mix (loops, live, effects - stereo)"), true, L.input < 0);
    m.addSeparator();
    m.addItem(6, U8("Hear the input through this lane (◉ HEAR)"), true, L.monitor);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, li](int r) {
        if (r <= 0) return;
        const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li];
        if (r >= 10 && r < 14) l.input = r - 10; else if (r >= 20) l.input = 4 + r - 20; else if (r == 5) l.input = -1; else if (r == 6) l.monitor = !l.monitor;
        builtFor = -2; repaint();
    });
}

void StudioView::autoMenu(int li) {
    juce::PopupMenu m;
    auto& L = studio.song.lanes[(size_t) li];
    auto ps = autoParams(L);
    m.addItem(1, "Hide automation", true, L.showAuto < 0);
    m.addSeparator();
    for (int i = 0; i < (int) ps.size(); ++i) {
        bool has = false; for (auto& a : L.autos) if (a.param == ps[(size_t) i].id && !a.pts.empty()) has = true;
        m.addItem(100 + i, ps[(size_t) i].name + (has ? U8("  ●") : juce::String()), true, L.showAuto == i);
    }
    m.addSeparator();
    if (L.showAuto >= 0) m.addItem(2, "Clear this automation");
    m.showMenuAsync(juce::PopupMenu::Options(), [this, li, ps](int r) {
        if (r <= 0) return;
        const juce::ScopedLock sl(studio.lock); auto& l = studio.song.lanes[(size_t) li];
        if (r == 1) l.showAuto = -1;
        else if (r == 2 && l.showAuto >= 0 && l.showAuto < (int) ps.size()) { auto id = ps[(size_t) l.showAuto].id; l.autos.erase(std::remove_if(l.autos.begin(), l.autos.end(), [&](const StudioAuto& a) { return a.param == id; }), l.autos.end()); }
        else if (r >= 100) l.showAuto = r - 100;
        repaint();
    });
}

void StudioView::addDeviceMenu(int li) {
    juce::PopupMenu m;
    m.addSectionHeader(li < 0 ? U8("EFFECTS ON THE MASTER") : U8("EFFECTS ON ") + studio.song.lanes[(size_t) li].name);
    juce::PopupMenu chains;
    static const char* CH[6] = { "Voice  ·  EQ, compressor, a little reverb", "Instrument  ·  EQ, compressor", "Drums  ·  compressor, saturation",
                                 "Warm  ·  tape saturation, EQ", "Wide echo  ·  delay, chorus", "Big room  ·  reverb" };
    for (int c = 0; c < 6; ++c) chains.addItem(100 + c, CH[c]);
    m.addSubMenu(U8("Ready-made (one tap)"), chains);
    m.addSeparator();
    m.addSectionHeader(U8("STUDIO EFFECTS"));
    for (int t = 0; t < DevCount; ++t) m.addItem(1 + t, juce::String(devInfo()[(size_t) t].name) + U8("  ·  ") + devInfo()[(size_t) t].desc);
    // 2.3: every effect of the loop station, with its own knobs (as on the station)
    juce::PopupMenu stIns, stSend, stNew;
    for (int f = 0; f < stationFxCount(); ++f) { auto& I = stationFxInfo(f); auto& g = stationFxGroup(f) == 0 ? stIns : stationFxGroup(f) == 1 ? stSend : stNew; g.addItem(1000 + f, juce::String(I.name) + U8("  ·  ") + I.desc); }
    m.addSeparator();
    m.addSectionHeader(U8("THE STATION'S EFFECTS"));
    m.addSubMenu(U8("Station inserts  (drive, tremolo, phaser, swell, EQ)"), stIns);
    m.addSubMenu(U8("Station effects  (chorus, tape delay, halo, shadow, space …)"), stSend);
    m.addSubMenu(U8("More station effects  (flanger, wah, octaver, tape, vocoder …)"), stNew);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, li](int r) {
        if (r <= 0) return;
        studio.snapshot();
        if (r >= 100) studio.addChain(li, r - 100);
        else { const juce::ScopedLock sl(studio.lock);
               auto& v = li < 0 ? studio.song.master : studio.song.lanes[(size_t) li].devices;
               if (v.size() < 16) v.push_back(r >= 1000 ? StudioDevice::makeStation(r - 1000) : StudioDevice::make(r - 1)); }
        studio.refresh();                                 // heard at once, still playing if it was
        report = U8("✓ effects on ") + (li < 0 ? juce::String("the master") : studio.song.lanes[(size_t) li].name) + U8(" - ● PRINT FX records them into the sound");
        builtFor = -2; repaint();
    });
}

void StudioView::printFx(int li) {
    juce::String why;
    int ci = studio.printLane(li, why);
    if (ci < 0) { report = why; repaint(); return; }
    selClip = ci; selLane = studio.song.clips[(size_t) ci].lane; detail = DLane;
    report = U8("✓ recorded with its effects - the effects are now in the sound (UNDO brings them back)");
    builtFor = -2; repaint();
}

void StudioView::rename(int li) {
    auto* w = new juce::AlertWindow("Lane name", "", juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", studio.song.lanes[(size_t) li].name);
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, li](int ok) { if (ok == 1 && li < (int) studio.song.lanes.size()) { const juce::ScopedLock sl(studio.lock); studio.song.lanes[(size_t) li].name = w->getTextEditorContents("n").substring(0, 30).toUpperCase(); } repaint(); }), true);
}

int StudioView::pressEverything(juce::StringArray& bad) {
    auto draw = [this] { juce::Image im(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true); juce::Graphics g(im); paintEntireComponent(g, false); };
    auto settle = [] { juce::PopupMenu::dismissAllActiveMenus(); if (auto* m = juce::ModalComponentManager::getInstance()) m->cancelAllModalComponents();
                       };
    auto consistent = [this](const juce::String& what, juce::StringArray& b) {
        auto& S = studio.song;
        for (auto& c : S.clips) if (c.lane < 0 || c.lane >= (int) S.lanes.size() || (c.source >= (int) S.sources.size())) { b.add(what + ": a clip on a lane that is not there"); return; }
        if (studio.laneStrip.size() != S.lanes.size() || studio.laneScene.size() != S.lanes.size()) b.add(what + ": lane lists out of step");
        if (selLane >= (int) S.lanes.size()) b.add(what + ": the chosen lane is gone"); };
    int pressed = 0;
    for (int pass = 0; pass < 2; ++pass) {                       // the second time with a clip chosen (its buttons)
        if (pass == 1) { for (int i = 0; i < (int) studio.song.clips.size(); ++i) if (!studio.song.clips[(size_t) i].isMidi()) { selClip = i; detail = DClip; break; } builtFor = -2; resized(); }
        draw();
        std::vector<juce::Rectangle<float>> rects; for (auto& h : hits) if (h.click) rects.push_back(h.r);
        for (auto& r : rects) {
            draw();
            for (auto& h : hits) if (h.click && h.r == r) {
                auto f = h.click; juce::String what = "the button at " + r.toNearestInt().toString();
                try { f(); } catch (...) { bad.add(what + " threw"); }
                ++pressed; settle(); draw(); consistent(what, bad);
                break;
            }
        }
        // the keys on top (LOOP, redo, ARRANGE, MIXER ...), not the ones that leave the studio or open a file window
        for (auto* k : kids) if (auto* p = dynamic_cast<Pill*>(k)) {
            auto id = p->getComponentID();
            if (!p->isVisible() || !p->onClick || id == "studio:export" || id == "studio:close" || id == "studio:home" || id == "studio:save") continue;
            try { p->onClick(); } catch (...) { bad.add(id + " threw"); }
            ++pressed; settle(); draw(); consistent(id, bad);
            if (id == "studio:mixer" || id == "studio:arrange") { try { p->onClick(); } catch (...) {} settle(); }
        }
        mixView = false; studio.stop(); settle();
    }
    return pressed;
}

void StudioView::filesDropped(const juce::StringArray& files, int x, int y) {
    int li = laneAt((float) y);
    long at = juce::jmax(0L, snapTo(sampleAt((float) x), false));
    importFiles(files, li, at);
}

void StudioView::importFiles(const juce::StringArray& inFiles, int li, long at) {
    juce::StringArray files, midi;                        // 2.3: MIDI files go onto instrument lanes
    for (auto& f : inFiles) (juce::File(f).hasFileExtension("mid;midi;smf") ? midi : files).add(f);
    if (!midi.isEmpty()) importMidiFiles(midi, li, at);
    if (files.isEmpty()) return;
    ++listening; report = U8("listening to ") + juce::File(files[0]).getFileName() + U8(" … (tempo, bars)"); repaint();
    double sr = cv.host.engine.sr; int bpb = juce::jmax(1, studio.song.beats);
    juce::Component::SafePointer<StudioView> sp(this);
    std::thread([sp, files, li, at, sr, bpb] {
        auto plans = std::make_shared<std::vector<ImportPlan>>();
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        juce::String errs;
        for (auto& f : files) { ImportPlan p; juce::String err;
            if (prepareImport(juce::File(f), sr, p, fm, err, bpb, 1200.0)) plans->push_back(std::move(p)); else errs << err << "  "; }
        juce::MessageManager::callAsync([sp, plans, li, at, errs] {
            if (sp == nullptr) return;
            --sp->listening;
            if (plans->empty()) { sp->report = errs.isNotEmpty() ? errs : U8("can't read that file (WAV, AIFF, MP3, FLAC or M4A)"); sp->repaint(); return; }
            sp->chooseImport(plans, li, at);
        });
    }).detach();
}

void StudioView::chooseImport(std::shared_ptr<std::vector<ImportPlan>> plans, int li, long at) {
    auto& p0 = plans->front();
    bool anyAudio = false; for (auto& c : studio.song.clips) anyAudio |= !c.isMidi();
    double fb = p0.fileBpm, sb = studio.song.bpm;
    juce::String heard = fb > 0 ? juce::String(fb, std::abs(fb - std::round(fb)) < 0.05 ? 0 : 1) + " BPM" : juce::String("no clear beat");
    if (fb > 0 && p0.tempo.wholeBars && p0.tempo.bars > 0) heard << U8(" · ") << p0.tempo.bars << (p0.tempo.bars == 1 ? " bar" : " bars");
    juce::PopupMenu m;
    m.addSectionHeader(p0.file.getFileName() + U8(" — ") + heard + (plans->size() > 1 ? U8("  (+") + juce::String((int) plans->size() - 1) + " more)" : juce::String()));
    if (!anyAudio && fb > 0) m.addItem(1, U8("As it is — the song takes its tempo (") + juce::String(fb, 1) + " BPM)");
    else m.addItem(1, U8("As it is — its own speed, not changed"));
    if (anyAudio && fb > 0) m.addItem(2, U8("Fit to the song — ") + juce::String(fb, 1) + U8(" → ") + juce::String(sb, 1) + U8(" BPM (stretched, same pitch)"));
    if (fb > 0) {
        juce::PopupMenu t;
        t.addItem(11, U8("It is ") + juce::String(fb * 0.5, 1) + U8(" BPM (half)"));
        t.addItem(12, U8("It is ") + juce::String(fb * 2.0, 1) + U8(" BPM (double)"));
        m.addSubMenu(U8("The tempo is wrong …"), t);
    }
    m.addSeparator(); m.addItem(9, "Cancel");
    juce::Component::SafePointer<StudioView> sp(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1)),
                    [sp, plans, li, at, anyAudio](int r) {
        if (sp == nullptr || r <= 0 || r == 9) { if (sp) { sp->report = {}; sp->repaint(); } return; }
        if (r == 11 || r == 12) { for (auto& p : *plans) if (p.fileBpm > 0) { p.fileBpm *= r == 11 ? 0.5 : 2.0; p.tempo.bars = r == 11 ? p.tempo.bars / 2 : p.tempo.bars * 2; }
                                  sp->chooseImport(plans, li, at); return; }
        bool fit = r == 2;
        long pos = at; int lane = li; juce::String last;
        for (auto& p : *plans) {
            juce::String why; int ci = sp->studio.importAudio(p, lane, pos, fit, why);
            if (ci < 0) { last = why; continue; }
            auto& c = sp->studio.song.clips[(size_t) ci]; lane = c.lane; pos = c.start + c.length; sp->selClip = ci; sp->selLane = c.lane;
            last = U8("✓ ") + p.file.getFileName() + (fit ? U8(" fitted to ") + juce::String(sp->studio.song.bpm, 1) + " BPM"
                                                           : (anyAudio ? U8(" as it is") : U8(" — the song is at ") + juce::String(sp->studio.song.bpm, 1) + " BPM"));
        }
        sp->report = last; sp->builtFor = -2; sp->resized(); sp->repaint();
    });
}

void StudioView::doExport() {
    if (isThreadRunning()) return;
    bool stem = job == JStems;
    juce::PopupMenu pm;
    pm.addItem(1, U8("WAV 24-bit 48 kHz  ·  streaming −14 LUFS"));
    pm.addItem(2, U8("WAV 24-bit 48 kHz  ·  as it is (peaks at −1 dB)"));
    pm.addItem(3, U8("FLAC 24-bit 48 kHz  ·  streaming −14 LUFS"));
    pm.addItem(4, U8("WAV 16-bit 44.1 kHz  ·  CD, −14 LUFS"));
    pm.addItem(5, U8("WAV 32-bit float 96 kHz  ·  archive, as it is"));
    if (!stem) { pm.addSeparator(); pm.addItem(6, U8("Every lane on its own (stems, as they are)")); }
    juce::Component* target = nullptr; for (auto* k : kids) if (k->getComponentID() == "studio:export") target = k;
    pm.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target), [this, stem](int r) {
        if (r <= 0) { job = JNone; return; }
        ExportOptions o; o.bits = r == 4 ? 16 : (r == 5 ? 32 : 24); o.flac = r == 3; o.sampleRate = r == 4 ? 44100 : (r == 5 ? 96000 : 48000); o.loudness = (r == 2 || r == 5 || r == 6 || stem) ? 0 : 1;
        exportOpt = o;
        bool stems = r == 6;
        if (stems) jobLane = -1;
        juce::String name = (cv.host.songFolder.isDirectory() ? cv.host.songFolder.getFileName() : juce::String("ARCO studio"))
                          + (stem && jobLane >= 0 && jobLane < (int) studio.song.lanes.size() ? " - " + studio.song.lanes[(size_t) jobLane].name : juce::String(" (studio)"));
        juce::String ext = o.flac ? ".flac" : ".wav";
        Job jb = (stems || stem) ? JStems : JExport;
       #if JUCE_IOS
        auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Exports"); d.createDirectory();
        exportFile = d.getChildFile(name + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M") + ext);
        job = jb; progress = 0.01f; jobDone = false; startThread();
       #else
        chooser = std::make_unique<juce::FileChooser>(stems ? "Export the stems (a folder is made next to this name)" : "Export as", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(name + ext), "*" + ext);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting | juce::FileBrowserComponent::canSelectFiles,
            [this, ext, jb](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f == juce::File()) { job = JNone; return; } exportFile = f.withFileExtension(ext.substring(1)); job = jb; progress = 0.01f; jobDone = false; startThread(); });
       #endif
    });
}

void StudioView::run() {
    if (job == JExport) {
        juce::AudioBuffer<float> b;
        if (!studio.renderAll(b, [this](float p) { progress = p * 0.8f; return !threadShouldExit(); })) { report = "export cancelled"; jobDone = true; return; }
        juce::String rep;
        bool ok = Renderer::write(b, studio.song.sr, exportOpt, exportFile, rep);
        report = ok ? U8("✓ ") + exportFile.getFileName() + "   " + rep : rep;
    } else if (job == JStems) {
        std::vector<int> lanes;
        if (jobLane >= 0) lanes.push_back(jobLane); else for (int i = 0; i < (int) studio.song.lanes.size(); ++i) if (studio.song.lanes[(size_t) i].kind != LaneReturn) lanes.push_back(i);
        auto dir = jobLane >= 0 ? exportFile.getParentDirectory() : exportFile.getParentDirectory().getChildFile(exportFile.getFileNameWithoutExtension() + " stems");
        dir.createDirectory();
        int k = 0; juce::String last;
        for (int li : lanes) {
            juce::AudioBuffer<float> b;
            int n = (int) lanes.size();
            if (!studio.renderAll(b, [this, k, n](float p) { progress = (k + p) / (float) n; return !threadShouldExit(); }, li)) break;
            auto f = jobLane >= 0 ? exportFile : dir.getChildFile(juce::String(k + 1).paddedLeft('0', 2) + " " + studio.song.lanes[(size_t) li].name + exportFile.getFileExtension());
            juce::String rep; Renderer::write(b, studio.song.sr, exportOpt, f, rep); last = f.getFileName(); ++k;
        }
        report = U8("✓ ") + juce::String(k) + (k == 1 ? " lane exported: " + last : " stems in " + dir.getFileName());
        if (jobLane < 0) exportFile = dir;
    } else if (job == JPrint) {
        bool ok = studio.printEffects(jobLane, [this](float p) { progress = p; return !threadShouldExit(); });
        report = ok ? U8("✓ the lane now plays with the station's effects (UNDO brings back the dry sound)") : U8("this lane has no track on the station to print");
    } else if (job == JTranspose) {
        bool ok = studio.transposeClip(jobClip, jobSemis);
        report = ok ? U8("✓ transposed ") + juce::String(jobSemis > 0 ? "+" : "") + juce::String((int) jobSemis) + " semitones" : juce::String("could not transpose");
    }
    progress = 1.0f;
    jobDone = true;
}

void StudioView::timerCallback() {
    studio.collectRecordings();
    if (jobDone.exchange(false)) {
        if ((job == JExport || job == JStems) && exportFile.exists()) {
           #if JUCE_IOS
            if (exportFile.existsAsFile()) juce::ContentSharer::shareFilesScoped({ juce::URL(exportFile) }, [](bool, const juce::String&) {});
           #else
            exportFile.revealToUser();
           #endif
        }
        job = JNone; builtFor = -2;
    }
    if (!isShowing()) return;
    for (auto& m : studio.meters) m.l.store(m.l.load() * 0.85f);
    if (studio.playing.load() && drag == None) {
        double sec = studio.pos.load() / studio.song.sr, visible = clipArea().getWidth() / pxPerSec;
        if (sec > scrollSec + visible * 0.92 || sec < scrollSec) scrollSec = juce::jmax(0.0, sec - visible * 0.08);
    }
    bool hasInst = false; for (auto& L : studio.song.lanes) hasInst |= L.kind == LaneInstrument;
    cv.host.studioKeys = hasInst;
    repaint();
}

// ================================================================== 2.3 editing like a full DAW
void StudioView::togglePunch() {
    {
        const juce::ScopedLock sl(studio.lock); auto& s = studio.song;
        if (!s.punchOn && s.punchEnd <= s.punchStart + 64) {      // no punch points yet: the loop region, or two bars from the playhead
            if (s.loopEnd > s.loopStart + 64) { s.punchStart = s.loopStart; s.punchEnd = s.loopEnd; }
            else { s.punchStart = snapTo(studio.pos.load(), false); s.punchEnd = s.punchStart + (long) (s.barLen() * 2); }
        }
        s.punchOn = !s.punchOn;
    }
    report = studio.song.punchOn ? U8("PUNCH on: recording keeps only what falls between the red points (alt-drag on the ruler moves them)") : U8("PUNCH off");
    repaint();
}

void StudioView::copySel(bool cut) {
    auto ids = chosenClips();
    if (ids.empty()) { report = "choose a clip first"; repaint(); return; }
    studio.copyClips(ids);
    if (cut) removeClip();
    report = (cut ? U8("✂ cut ") : U8("✓ copied ")) + juce::String((int) ids.size()) + (ids.size() == 1 ? " clip" : " clips") + U8(" - ⌘V pastes at the playhead");
    repaint();
}

void StudioView::paste() {
    if (studio.clipboard.empty()) { report = "copy a clip first (⌘C)"; repaint(); return; }
    auto got = studio.pasteClips(studio.pos.load(), selLane);
    if (got.empty()) { report = "nothing could be pasted here"; repaint(); return; }
    selClips = got; selClip = got.back(); detail = DClip;
    report = U8("✓ pasted at the playhead");
    builtFor = -2; repaint();
}

void StudioView::takesMenu(int ci) {
    auto& S = studio.song;
    if (ci < 0 || ci >= (int) S.clips.size()) return;
    auto& c = S.clips[(size_t) ci];
    juce::PopupMenu m;
    m.addSectionHeader(U8("TAKES — the one ticked plays  ·  T: the next  ·  to comp: SPLIT (S) and choose a take for each piece"));
    for (int t = 0; t < (int) c.takes.size(); ++t) m.addItem(1 + t, juce::String(t + 1) + U8("  ·  ") + (c.takes[(size_t) t].name.isNotEmpty() ? c.takes[(size_t) t].name : "take " + juce::String(t + 1)), true, t == c.take);
    if (c.takes.size() < 2) m.addItem(999, U8("one take - record over it (or loop-record) to get more"), false);
    m.addSeparator();
    m.addItem(500, U8("Every take on a lane of its own (to compare)"), c.takes.size() > 1);
    m.addItem(501, U8("Keep only this take"), c.takes.size() > 1);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, ci](int r) {
        if (r <= 0) return;
        if (r == 500) { int l = studio.explodeTakes(ci); report = l >= 0 ? U8("✓ every take on its own lane (muted) - solo them to compare") : juce::String(); }
        else if (r == 501) { studio.deleteOtherTakes(ci); report = U8("✓ only this take is kept"); }
        else if (r >= 1 && r < 500) { studio.chooseTake(ci, r - 1); report = U8("✓ take ") + juce::String(r) + U8(" plays"); }
        builtFor = -2; repaint();
    });
}

void StudioView::quantiseMenu() {
    auto& S = studio.song;
    if (selClip < 0 || selClip >= (int) S.clips.size() || !S.clips[(size_t) selClip].isMidi()) { report = "choose a MIDI clip first"; repaint(); return; }
    juce::PopupMenu m;
    m.addSectionHeader("QUANTISE THE NOTES TO");
    static const char* G[7] = { "1/4", "1/8", "1/16", "1/32", "1/8 triplets", "1/16 triplets", "the bar" };
    static const double D[7] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, -1.0 };
    for (int i = 0; i < 7; ++i) m.addItem(1 + i, G[i]);
    juce::PopupMenu soft; for (int i = 0; i < 7; ++i) soft.addItem(11 + i, juce::String(G[i]) + U8("  ·  50% (keeps some feel)"));
    m.addSubMenu("Gently (50%)", soft);
    juce::PopupMenu ends; for (int i = 0; i < 7; ++i) ends.addItem(21 + i, juce::String(G[i]) + U8("  ·  starts and ends"));
    m.addSubMenu("Starts and ends", ends);
    m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
        if (r <= 0) return;
        int i = (r - 1) % 10; bool soft = r >= 11 && r < 21, ends = r >= 21;
        double q = D[i] < 0 ? studio.song.barLen() : studio.song.beatLen() * D[i];
        studio.quantise(selClip, q, soft ? 0.5f : 1.0f, ends);
        report = U8("✓ quantised to ") + G[i] + (soft ? " (50%)" : "");
        repaint();
    });
}

void StudioView::importMidiFiles(const juce::StringArray& files, int lane, long at) {
    int first = -1; juce::String why;
    for (auto& f : files) { int c = studio.importMidi(juce::File(f), lane, at, why); if (first < 0) first = c; }
    if (first >= 0) { selClip = first; selLane = studio.song.clips[(size_t) first].lane; detail = DClip; report = U8("✓ MIDI in: ") + juce::File(files[0]).getFileName() + U8(" - it plays on SYNTH 2"); }
    else report = why;
    cv.host.studioKeys = true;
    builtFor = -2; repaint();
}

void StudioView::editMenu() {
    auto& S = studio.song;
    bool region = S.loopEnd > S.loopStart + 64;
    bool any = selClip >= 0 && selClip < (int) S.clips.size();
    juce::PopupMenu m;
    m.addItem(1, U8("Cut  (⌘X)"), any); m.addItem(2, U8("Copy  (⌘C)"), any); m.addItem(3, U8("Paste at the playhead  (⌘V)"), !studio.clipboard.empty());
    m.addItem(4, U8("Split at the playhead  (S)"), any); m.addItem(5, U8("Delete  (⌫)"), any);
    m.addSeparator();
    m.addSectionHeader("TIME (the loop region)");
    m.addItem(10, U8("Delete the time in the loop region — what is after it moves left"), region);
    m.addItem(11, U8("Insert silence at the playhead — as long as the loop region"), region);
    m.addItem(12, U8("Insert one bar of silence at the playhead"));
    m.addSeparator();
    m.addSectionHeader("WHEN YOU DELETE A CLIP");
    m.addItem(20, U8("Nothing else moves"), true, S.ripple == 0);
    m.addItem(21, U8("Ripple this lane — what is after it on the lane moves left"), true, S.ripple == 1);
    m.addItem(22, U8("Ripple every lane — the whole song moves left"), true, S.ripple == 2);
    m.addSeparator();
    m.addItem(30, U8("Crossfade overlapping clips automatically"), true, S.autoXfade);
    m.addItem(31, U8("Punch points = the loop region"), region);
    m.addItem(32, U8("Punch recording  (P)"), true, S.punchOn);
    m.addSeparator();
    m.addItem(40, U8("Import a MIDI file …"));
    m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
        auto& S = studio.song;
        if (r <= 0) return;
        if (r == 1 || r == 2) copySel(r == 1);
        else if (r == 3) paste();
        else if (r == 4) split();
        else if (r == 5) removeClip();
        else if (r == 10) { studio.removeTime(S.loopStart, S.loopEnd); selClip = -1; selClips.clear(); report = U8("✓ the time is gone - everything after it moved left"); }
        else if (r == 11 || r == 12) { long len = r == 11 ? S.loopEnd - S.loopStart : (long) S.barLen(); studio.insertTime(studio.pos.load(), len); selClip = -1; selClips.clear(); report = U8("✓ silence inserted - everything after the playhead moved right"); }
        else if (r >= 20 && r <= 22) { S.ripple = r - 20; report = S.ripple == 0 ? U8("ripple off") : S.ripple == 1 ? U8("RIPPLE (this lane): deleting a clip pulls what follows on its lane to the left") : U8("RIPPLE (every lane): deleting a clip pulls the whole song after it to the left"); }
        else if (r == 30) { const juce::ScopedLock sl(studio.lock); S.autoXfade = !S.autoXfade; }
        else if (r == 31) { const juce::ScopedLock sl(studio.lock); S.punchStart = S.loopStart; S.punchEnd = S.loopEnd; S.punchOn = true; }
        else if (r == 32) togglePunch();
        else if (r == 40) {
            chooser = std::make_unique<juce::FileChooser>("Import MIDI into STUDIO", juce::File(), "*.mid;*.midi");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems, [this](const juce::FileChooser& fc) {
                juce::StringArray fs; for (auto& f : fc.getResults()) fs.add(f.getFullPathName());
                if (!fs.isEmpty()) importMidiFiles(fs, selLane, snapTo(studio.pos.load(), false)); });
        }
        builtFor = -2; repaint();
    });
}


void StudioView::routeMenu(int li) {
    auto& S = studio.song;
    if (li < 0 || li >= (int) S.lanes.size()) return;
    auto& L = S.lanes[(size_t) li];
    juce::PopupMenu m;
    m.addSectionHeader(L.name + U8(" — ROUTING"));
    juce::PopupMenu to;
    to.addItem(1000, "MASTER", true, L.out < 0);
    for (int t = 0; t < (int) S.lanes.size(); ++t) if (S.isTarget(t) && t != li) to.addItem(1001 + t, (S.lanes[(size_t) t].kind == LaneBus ? U8("BUS  ") : U8("RETURN  ")) + S.lanes[(size_t) t].name, S.canRoute(li, t), L.out == t);
    m.addSubMenu(U8("Goes to"), to);
    juce::PopupMenu sends;
    static const float AM[6] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 0.0f };
    for (int t = 0; t < (int) S.lanes.size(); ++t) {
        if (!S.isTarget(t) || t == li || (L.kind != LaneReturn && (t == S.returnA() || t == S.returnB()))) continue;   // (A and B have their own knobs)
        const StudioSend* cur = nullptr; for (auto& sd : L.sends) if (sd.to == t) cur = &sd;
        juce::PopupMenu one;
        for (int k = 0; k < 6; ++k) one.addItem(2000 + t * 16 + k, k == 5 ? juce::String("off") : juce::String((int) (AM[k] * 100)) + "%", S.canRoute(li, t) || cur, cur ? std::abs(cur->amt - AM[k]) < 0.01f : k == 5);
        one.addSeparator(); one.addItem(2000 + t * 16 + 8, U8("before the fader (pre)"), cur != nullptr, cur && cur->pre);
        sends.addSubMenu(S.lanes[(size_t) t].name + (cur ? U8("  ·  ") + juce::String((int) (cur->amt * 100)) + "%" + (cur->pre ? " pre" : "") : juce::String()), one);
    }
    if (sends.getNumItems() == 0) sends.addItem(999, U8("make a bus first (+ BUS on the left)"), false);
    m.addSubMenu(U8("Sends to"), sends);
    if (L.kind == LaneAudio || L.kind == LaneInstrument) { m.addSeparator(); m.addItem(3000, U8("Put it in a new bus")); }
    m.showMenuAsync(juce::PopupMenu::Options(), [this, li](int r) {
        auto& S = studio.song;
        if (r <= 0 || li >= (int) S.lanes.size()) return;
        studio.snapshot();
        if (r == 3000) { int n = 1; for (auto& x : S.lanes) n += x.kind == LaneBus; selLane = studio.addBus("BUS " + juce::String(n), { li }); report = U8("✓ in a new bus - its effects and fader act on it"); builtFor = -2; repaint(); return; }
        {
            const juce::ScopedLock sl(studio.lock);
            auto& L = S.lanes[(size_t) li];
            if (r == 1000) L.out = -1;
            else if (r > 1000 && r < 2000) { int t = r - 1001; if (S.canRoute(li, t)) L.out = t; }
            else if (r >= 2000 && r < 3000) {
                int t = (r - 2000) / 16, k = (r - 2000) % 16;
                static const float AM[6] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 0.0f };
                auto it = std::find_if(L.sends.begin(), L.sends.end(), [t](const StudioSend& x) { return x.to == t; });
                if (k == 8) { if (it != L.sends.end()) it->pre = !it->pre; }
                else if (k == 5) { if (it != L.sends.end()) L.sends.erase(it); }
                else if (it != L.sends.end()) it->amt = AM[k];
                else if (S.canRoute(li, t)) L.sends.push_back({ t, AM[k], false });
            }
        }
        studio.refresh();
        builtFor = -2; repaint();
    });
}

void StudioView::keyMenu(int li, int di) {
    auto& S = studio.song;
    juce::PopupMenu m;
    m.addSectionHeader(U8("WHAT DRIVES IT (SIDECHAIN) — e.g. the bass ducks when the kick plays"));
    auto& v = li < 0 ? S.master : S.lanes[(size_t) li].devices;
    if (di < 0 || di >= (int) v.size()) return;
    m.addItem(1, U8("Its own sound"), true, v[(size_t) di].side < 0);
    for (int t = 0; t < (int) S.lanes.size(); ++t) if (t != li) m.addItem(10 + t, S.lanes[(size_t) t].name, true, v[(size_t) di].side == t);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, li, di](int r) {
        if (r <= 0) return;
        studio.snapshot();
        { const juce::ScopedLock sl(studio.lock); auto& vv = li < 0 ? studio.song.master : studio.song.lanes[(size_t) li].devices; if (di < (int) vv.size()) vv[(size_t) di].side = r == 1 ? -1 : r - 10; }
        builtFor = -2; repaint();
    });
}


} // namespace arco
