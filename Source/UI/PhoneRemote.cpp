#include "PhoneRemote.h"
#if JUCE_IOS
 #include "../App/IOSAudio.h"
#endif

namespace arco {

static juce::String U8(const char* s) { return juce::String::fromUTF8(s); }

// ================================================================== the station's state on the phone
const ParamDef* RemoteState::defOf(const std::string& id) {
    static std::map<std::string, const ParamDef*> m = [] { std::map<std::string, const ParamDef*> x; for (auto& d : paramTable()) x[d.id] = &d; return x; }();
    auto it = m.find(id); return it == m.end() ? nullptr : it->second;
}
void RemoteState::apply(const juce::StringArray& lines) {
    double now = juce::Time::getMillisecondCounterHiRes();
    for (auto& l : lines) {
        if (l.length() < 3) continue;
        auto kind = l[0];
        auto rest = l.substring(2);
        auto id = rest.upToFirstOccurrenceOf(" ", false, false).toStdString();
        auto val = rest.fromFirstOccurrenceOf(" ", false, false);
        if (kind == 'p') { auto it = localUntil.find(id); if (it != localUntil.end() && it->second > now) continue; p[id] = val.getDoubleValue(); }
        else if (kind == 's') s[id] = val.getDoubleValue();
        else if (kind == 'i') info[id] = val;
    }
}

// ================================================================== the phone screen
PhoneRemote::PhoneRemote(AudioHost& h) : host(h) {
    host.phoneLink.onState = [this](const juce::StringArray& lines) {
        st.apply(lines);
        auto pal = st.inf("palette");
        if (pal.isNotEmpty() && pal != lastPalette) { lastPalette = pal; Theme::setPalette(Theme::paletteByName(pal)); if (auto* t = getTopLevelComponent()) t->repaint(); }
        repaint();
    };
    host.phoneLink.onConnection = [this](bool on) { st.p.clear(); st.s.clear(); st.info.clear(); if (on) send("listen " + juce::String(host.listenWant.load())); repaint(); };
    setWantsKeyboardFocus(false);
    startTimerHz(20);
}
PhoneRemote::~PhoneRemote() { host.phoneLink.onState = nullptr; host.phoneLink.onConnection = nullptr; stop(); }

void PhoneRemote::start() {
    auto saved = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("phone-name.txt");
    myName = saved.loadFileAsString().trim();
    if (myName.isEmpty()) {
        auto n = juce::SystemStats::getComputerName().trim();
        if (n.isEmpty() || n == "iPhone" || n == "iPad") n = (AudioHost::isPhone() ? "iPhone " : "iPad ") + juce::String::charToString((juce::juce_wchar) ('A' + juce::Random::getSystemRandom().nextInt(26)));
        myName = n;
        saved.getParentDirectory().createDirectory(); saved.replaceWithText(myName);
    }
    host.synth.allOff();
    host.phoneLink.start(myName);
    host.micMode.store(true);
   #if ! JUCE_LINUX                                  // (the Linux test build has no screen saver to talk to)
    juce::Desktop::getInstance().setScreenSaverEnabled(false);
   #endif
}
void PhoneRemote::stop() {
    host.micMode.store(false);
    host.phoneLink.stop();
   #if ! JUCE_LINUX
    juce::Desktop::getInstance().setScreenSaverEnabled(true);
   #endif
}
void PhoneRemote::rename() {
    auto* w = new juce::AlertWindow("This phone's name", "The station shows it (SETUP → iPHONES):", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("name", myName);
    w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        auto n = w->getTextEditorContents("name").trim().substring(0, 40);
        if (r == 1 && n.isNotEmpty() && n != myName) {
            myName = n;
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("phone-name.txt").replaceWithText(n);
            host.phoneLink.stop(); host.phoneLink.start(myName);
        } }), true);
}

void PhoneRemote::timerCallback() { if (isShowing()) repaint(); }

void PhoneRemote::setPar(const std::string& id, double v) {
    if (id == "listenvol") { host.listenVol.store((float) juce::jlimit(0.0, 1.0, v)); return; }
    auto* d = RemoteState::defOf(id); if (d == nullptr) return;
    v = juce::jlimit(d->lo, d->hi, v); if (d->kind != 0) v = std::round(v);
    st.p[id] = v; st.localUntil[id] = juce::Time::getMillisecondCounterHiRes() + 600;
    send("set " + juce::String(id) + " " + juce::String(v, 3));
}

void PhoneRemote::resized() {
    u = juce::jlimit(0.8f, 1.6f, juce::jmin(getWidth() / 390.0f, getHeight() / 700.0f));
}

void PhoneRemote::text(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float fs, juce::Colour c, int w, juce::Justification j) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs * u, w)); g.drawFittedText(t, r.toNearestInt(), j, 2, 0.8f);
}
void PhoneRemote::button(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c, std::function<void()> tap, float fs, bool scrolls) {
    bool down = active >= 0 && active < (int) ctls.size() && ctls[(size_t) active].r == r && !dragging;
    if (Theme::softUI()) {          // 2.2: the station's silicone keys - plain keys raised, a key that is on pressed in and lit
        bool plain = c == Theme::btn() || c == Theme::card() || c == Theme::card2() || c == Theme::bg();
        float rad = juce::jmin(r.getHeight() * 0.5f, 22.0f * u);
        softKeyLite(g, r, rad, down || !plain, c, plain ? 0.0f : 0.85f);
        g.setColour(plain ? Theme::txt() : Theme::ink(c)); g.setFont(Theme::font((fs > 0 ? fs : 11.0f) * u, 2));
        g.drawFittedText(t, r.toNearestInt().reduced(4, 0), juce::Justification::centred, 2, 0.75f);
        ctls.push_back({ r, 0, {}, 0, 0, std::move(tap), scrolls });
        return;
    }
    auto col = down ? c.darker(0.2f) : c;
    g.setColour(col); g.fillRoundedRectangle(r, 10.0f * u);
    g.setColour(Theme::textOn(col)); g.setFont(Theme::font((fs > 0 ? fs : 11.0f) * u, 2));
    g.drawFittedText(t, r.toNearestInt().reduced(4, 0), juce::Justification::centred, 2, 0.75f);
    ctls.push_back({ r, 0, {}, 0, 0, std::move(tap), scrolls });
}
void PhoneRemote::slider(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const std::string& id, juce::Colour c, bool scrolls) {
    auto* d = RemoteState::defOf(id); if (d == nullptr) return;
    double v = st.par(id), t = (v - d->lo) / juce::jmax(1e-9, d->hi - d->lo);
    float zero = d->lo < 0 && d->hi > 0 ? (float) (-d->lo / (d->hi - d->lo)) : 0.0f;
    float a = juce::jmin(zero, (float) t), b = juce::jmax(zero, (float) t);
    if (Theme::softUI()) {          // a groove in the skin, the value lit inside it
        float rad = r.getHeight() * 0.5f;
        softWellLite(g, r, rad);
        auto in = r.reduced(3.0f * u);
        auto f = in.withX(in.getX() + in.getWidth() * a).withWidth(juce::jmax(in.getHeight(), in.getWidth() * (b - a)));
        g.setColour(c.withAlpha(Theme::lightMode() ? 0.55f : 0.45f)); g.fillRoundedRectangle(f, in.getHeight() * 0.5f);
    } else {
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, r.getHeight() * 0.3f);
        g.setColour(Theme::arc(c).withAlpha(0.9f)); g.fillRoundedRectangle(r.withX(r.getX() + r.getWidth() * a).withWidth(juce::jmax(r.getHeight() * 0.3f, r.getWidth() * (b - a))), r.getHeight() * 0.3f);
    }
    text(g, label, r.reduced(12 * u, 0), 9.5f, Theme::txt(), 2);
    juce::String vs = d->kind == 1 ? (v > 0.5 ? "ON" : "OFF") : juce::String((int) std::round(v));
    text(g, vs, r.reduced(12 * u, 0), 9.5f, Theme::txt(), 1, juce::Justification::centredRight);
    ctls.push_back({ r, d->kind == 1 ? 0 : 1, id, d->lo, d->hi, d->kind == 1 ? std::function<void()>([this, id] { setPar(id, st.par(id) > 0.5 ? 0 : 1); }) : nullptr, scrolls });
}

static const char* STATE[11] = { "empty", "waiting", "recording", "playing", "armed", "overdub", "count-in", "recording", "stopped", "starting", "stopping" };

void PhoneRemote::paint(juce::Graphics& g) {
    ctls.clear();
    Theme::backdrop(g, getLocalBounds());
    auto area = getLocalBounds().toFloat();
   #if JUCE_IOS
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->safeAreaInsets.subtractedFrom(getLocalBounds()).toFloat();
   #endif
    area = area.reduced(14 * u, 6 * u);
    bool conn = host.phoneLink.connected() || demo;
    // ---- header: who, the station, the microphone
    auto head = area.removeFromTop(46 * u);
    text(g, "ARCO", head.removeFromLeft(70 * u), 17.0f, Theme::txt(), 2);
    if (showBack) button(g, head.removeFromRight(70 * u).reduced(0, 8 * u), U8("STATION"), Theme::btn(), [this] { if (onBack) onBack(); }, 8.5f, false);
    {
        auto info = head.reduced(4 * u, 4 * u);
        g.setColour(conn ? Theme::green() : Theme::amber()); g.fillEllipse(info.getX(), info.getCentreY() - 4 * u, 8 * u, 8 * u);
        text(g, conn ? st.inf("song") : U8("looking for the station …"), info.withTrimmedLeft(14 * u).removeFromTop(info.getHeight() * 0.55f), 10.0f, Theme::txt(), 2);
        text(g, myName + U8("  ·  tap to rename"), info.withTrimmedLeft(14 * u).withTrimmedTop(info.getHeight() * 0.55f), 7.5f, Theme::sub());
        ctls.push_back({ info, 0, {}, 0, 0, [this] { rename(); }, false });
    }
    // microphone: on / off, level, which LIVE input
    auto mic = area.removeFromTop(44 * u);
    {
        bool on = host.phoneLink.micOn.load();
        auto mb = mic.removeFromLeft(96 * u).reduced(0, 4 * u);
        button(g, mb, on ? U8("● MIC ON") : U8("MIC OFF"), on ? Theme::red() : Theme::btn(), [this] { host.phoneLink.micOn.store(!host.phoneLink.micOn.load()); }, 9.5f, false);
        mic.removeFromLeft(8 * u);
        auto lv = mic.removeFromLeft(60 * u).withSizeKeepingCentre(60 * u, 8 * u);
        if (Theme::softUI()) softWellLite(g, lv, lv.getHeight() * 0.5f); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(lv, 3 * u); }
        g.setColour(Theme::green()); g.fillRoundedRectangle(lv.withWidth(lv.getWidth() * juce::jmin(1.0f, host.phoneLink.level.load() * 2.0f)), 4 * u);
        mic.removeFromLeft(8 * u);
        int live = (int) st.snap("live");
        float w = mic.getWidth() / 5.0f;
        for (int l = 0; l <= 4; ++l) {
            auto r = mic.removeFromLeft(w).reduced(2 * u, 6 * u);
            bool sel = live == l;
            button(g, r, l == 0 ? U8("–") : "L" + juce::String(l), sel ? (l == 0 ? Theme::sel() : Theme::strip(l - 1)) : Theme::btn(), [this, l] { send("live " + juce::String(l)); st.s["live"] = l; }, 9.0f, false);
        }
    }
    // ---- the tabs
    auto tabs = area.removeFromBottom(52 * u);
    const char* T[5] = { "PLAY", "MIX", "EFFECTS", "SYNTH", "AUDIO" };
    for (int i = 0; i < 5; ++i) button(g, tabs.removeFromLeft(tabs.getWidth() / (5 - i)).reduced(2 * u, 6 * u), T[i], page == i ? Theme::sel() : Theme::card(), [this, i] { setPage(i); }, 8.5f, false);
   #if JUCE_IOS
   #endif
    area.removeFromBottom(4 * u);
    content = area;
    if (page == 4) {                              // the phone's own sound: works with or without a station
        g.saveState(); g.reduceClipRegion(content.toNearestInt());
        drawAudio(g, content.withHeight(5000).translated(0, -scroll));
        g.restoreState();
        ctls.erase(std::remove_if(ctls.begin(), ctls.end(), [this](const Ctl& c) { return c.scrolls && !content.intersects(c.r); }), ctls.end());
        return;
    }
    if (!conn) {
        auto r = area.reduced(10 * u, 30 * u);
        text(g, U8("Open ARCO on your Mac or iPad."), r.removeFromTop(30 * u), 12.0f, Theme::txt(), 2, juce::Justification::centred);
        text(g, U8("Same Wi-Fi, or plug this phone into the Mac - the station finds it by itself (SETUP → iPHONES)."), r.removeFromTop(60 * u), 9.5f, Theme::sub(), 0, juce::Justification::centred);
        auto ips = PhoneServer::localAddresses();
        if (!ips.isEmpty()) text(g, U8("not found? type there:  ") + ips.joinIntoString("  or  "), r.removeFromTop(40 * u), 8.5f, Theme::sub(), 0, juce::Justification::centred);
        return;
    }
    g.saveState();
    g.reduceClipRegion(content.toNearestInt());
    auto inner = content.withHeight(5000).translated(0, -scroll);
    float used = 0;
    if (page == 0) drawPlay(g, inner);
    else if (page == 1) drawMix(g, inner);
    else if (page == 2) drawFx(g, inner);
    else drawSynth(g, content);
    juce::ignoreUnused(used);
    g.restoreState();
    // only controls inside the visible area can be touched
    ctls.erase(std::remove_if(ctls.begin(), ctls.end(), [this](const Ctl& c) { return c.scrolls && !content.intersects(c.r); }), ctls.end());
}

// ---------------------------------------------------------------- PLAY: transport, scenes, tracks
void PhoneRemote::drawPlay(juce::Graphics& g, juce::Rectangle<float> r) {
    float y0 = r.getY();
    // the beat
    auto top = r.removeFromTop(40 * u);
    double bpm = st.snap("smask") > 0 ? st.snap("ebpm") : st.par("bpm");
    text(g, juce::String(bpm, 1) + " BPM", top.removeFromLeft(top.getWidth() * 0.4f), 13.0f, Theme::txt(), 2);
    text(g, "bar " + juce::String((int) st.snap("barno")) + U8("  ·  beat ") + juce::String((int) st.snap("beatno")), top, 10.0f, Theme::sub(), 1, juce::Justification::centredRight);
    // transport
    auto row = r.removeFromTop(78 * u);
    int tr = (int) st.snap("transport");
    float w = row.getWidth() / 4.0f;
    button(g, row.removeFromLeft(w).reduced(3 * u), U8("▶"), tr == 1 ? Theme::green() : Theme::btn(), [this] { press("st_looper", "playall"); }, 20.0f);
    button(g, row.removeFromLeft(w).reduced(3 * u), U8("■"), Theme::btn(), [this] { press("st_looper", "stopall"); }, 18.0f);
    button(g, row.removeFromLeft(w).reduced(3 * u), "REC", Theme::red(), [this] { send("act 0"); }, 13.0f);
    button(g, row.removeFromLeft(w).reduced(3 * u), "UNDO", st.snap("undoav") > 0.5 ? Theme::amber() : Theme::btn(), [this] { send("act 2"); }, 11.0f);
    // scenes
    auto sc = r.removeFromTop(58 * u);
    int scenes = (int) st.snap("scenes"), mask = (int) st.snap("smask");
    float sw = sc.getWidth() / 5.0f;
    for (int i = 0; i < 4; ++i) {
        int n = i + 1;
        auto col = scenes % 8 == n ? Theme::green() : (scenes / 8 == n ? Theme::amber() : ((mask >> i) & 1) ? Theme::blue() : Theme::btn());
        button(g, sc.removeFromLeft(sw).reduced(3 * u, 6 * u), juce::String::charToString((juce::juce_wchar) ('A' + i)), col, [this, n] { press("st_looper", "scene", 8, n); }, 15.0f);
    }
    button(g, sc.reduced(3 * u, 6 * u), U8("NEXT"), Theme::btn(), [this] { send("act 3"); }, 9.0f);
    // tracks
    r.removeFromTop(8 * u);
    text(g, "TRACKS", r.removeFromTop(22 * u), 9.0f, Theme::sub(), 2);
    int ntn = juce::jlimit(1, 8, (int) st.snap("ntn")); if (st.snap("ntn") < 1) ntn = 4;
    for (int k = 0; k < ntn; ++k) {
        auto row2 = r.removeFromTop(56 * u).reduced(0, 3 * u);
        float pack = (float) st.snap(k < 4 ? "statA" : "statB");
        int s = ((int) pack >> (4 * (k % 4))) & 15;
        if (Theme::softUI()) softRaised(g, row2, 18 * u, 5.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(row2, 12 * u); }
        g.setColour(Theme::strip(4 + k)); g.fillEllipse(row2.getX() + 12 * u, row2.getCentreY() - 5 * u, 10 * u, 10 * u);
        auto lab = row2.withTrimmedLeft(30 * u).removeFromLeft(110 * u);
        text(g, "TRACK " + juce::String(k + 1), lab.removeFromTop(lab.getHeight() * 0.55f), 11.0f, Theme::txt(), 2);
        text(g, STATE[juce::jlimit(0, 10, s)], lab, 8.5f, s == 2 || s == 7 ? Theme::red() : (s == 3 ? Theme::green() : Theme::sub()), 1);
        auto b = row2.withTrimmedLeft(146 * u).reduced(4 * u, 6 * u);
        float bw = b.getWidth() / 3.0f;
        juce::String n = juce::String(k + 1);
        button(g, b.removeFromLeft(bw).reduced(3 * u, 0), "REC", Theme::red(), [this, n] { press("st_looper", "rec" + n); }, 10.0f);
        button(g, b.removeFromLeft(bw).reduced(3 * u, 0), U8("▶ ■"), Theme::btn(), [this, n] { press("st_looper", "tog" + n); }, 10.0f);
        button(g, b.removeFromLeft(bw).reduced(3 * u, 0), "CLR", Theme::btn(), [this, n] { press("st_looper", "clr" + n); }, 10.0f);
    }
    // the show
    r.removeFromTop(10 * u);
    auto bot = r.removeFromTop(54 * u);
    bool rec = st.snap("rec") > 0.5;
    button(g, bot.removeFromLeft(bot.getWidth() / 2).reduced(3 * u, 4 * u), rec ? U8("■ STOP RECORDING") : U8("● RECORD THE SHOW"), rec ? Theme::red() : Theme::btn(), [this] { send("rec"); }, 9.0f);
    button(g, bot.reduced(3 * u, 4 * u), "SAVE THE SONG", Theme::btn(), [this] { send("save"); }, 9.0f);
    auto status = st.inf("status");
    if (status.isNotEmpty()) text(g, status, r.removeFromTop(30 * u), 8.0f, Theme::sub(), 0, juce::Justification::centred);
    scrollMax = juce::jmax(0.0f, (r.getY() + scroll - y0) - content.getHeight());
}

// ---------------------------------------------------------------- MIX: every strip
void PhoneRemote::drawMix(juce::Graphics& g, juce::Rectangle<float> r) {
    float y0 = r.getY();
    int nl = juce::jlimit(1, 4, (int) st.snap("nlive")), ntn = juce::jlimit(1, 8, (int) st.snap("ntn")); if (st.snap("ntn") < 1) ntn = 4;
    auto strip = [&](int s) {
        auto box = r.removeFromTop(112 * u).reduced(0, 4 * u);
        if (Theme::softUI()) softRaised(g, box, 18 * u, 5.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(box, 12 * u); }
        auto in = box.reduced(12 * u, 8 * u);
        auto top = in.removeFromTop(26 * u);
        g.setColour(Theme::strip(s)); g.fillEllipse(top.getX(), top.getCentreY() - 5 * u, 10 * u, 10 * u);
        juce::String nm = s < 4 ? "LIVE " + juce::String(s + 1) : "TRACK " + juce::String(s - 3);
        auto ph = s < 4 ? st.inf("phone" + std::to_string(s + 1)) : juce::String();
        if (ph.isNotEmpty()) nm << U8("  ·  ") << ph;
        text(g, nm, top.withTrimmedLeft(16 * u).withTrimmedRight(110 * u), 10.5f, Theme::txt(), 2);
        float lv = (float) juce::jmin(1.0, st.snap("lv" + std::to_string(s)) * 1.5);
        auto mt = top.removeFromRight(100 * u);
        auto mm = mt.removeFromRight(46 * u).reduced(2 * u, 0);
        std::string ms = "mute" + std::to_string(s), ss = "solo" + std::to_string(s);
        button(g, mt.removeFromRight(46 * u).reduced(2 * u, 0), "M", st.par(ms) > 0.5 ? Theme::red() : Theme::btn(), [this, ms] { setPar(ms, st.par(ms) > 0.5 ? 0 : 1); }, 9.0f);
        button(g, mm, "S", st.par(ss) > 0.5 ? Theme::blue() : Theme::btn(), [this, ss] { setPar(ss, st.par(ss) > 0.5 ? 0 : 1); }, 9.0f);
        in.removeFromTop(4 * u);
        auto meter = in.removeFromTop(4 * u);
        if (Theme::softUI()) softWellLite(g, meter, meter.getHeight() * 0.5f); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(meter, 3 * u); }
        g.setColour(lv > 0.9f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * lv), 2 * u);
        in.removeFromTop(4 * u);
        auto sl = in.removeFromTop(30 * u);
        slider(g, sl.removeFromLeft(sl.getWidth() * 0.66f).reduced(0, 1 * u), "LEVEL", "vol" + std::to_string(s), Theme::strip(s));
        slider(g, sl.withTrimmedLeft(8 * u).reduced(0, 1 * u), "PAN", "pan" + std::to_string(s), Theme::sub());
    };
    for (int s = 0; s < nl; ++s) strip(s);
    for (int k = 0; k < ntn; ++k) strip(4 + k);
    r.removeFromTop(6 * u);
    auto mr = r.removeFromTop(40 * u);
    slider(g, mr.removeFromLeft(mr.getWidth() * 0.5f).reduced(2 * u), "MASTER", "master", Theme::txt());
    slider(g, mr.reduced(2 * u), "GLUE", "glue", Theme::amber());
    scrollMax = juce::jmax(0.0f, (r.getY() + scroll - y0) - content.getHeight());
}

// ---------------------------------------------------------------- EFFECTS: every effect, its send for the chosen strip
void PhoneRemote::drawFx(juce::Graphics& g, juce::Rectangle<float> r) {
    float y0 = r.getY();
    int nl = juce::jlimit(1, 4, (int) st.snap("nlive")), ntn = juce::jlimit(1, 8, (int) st.snap("ntn")); if (st.snap("ntn") < 1) ntn = 4;
    text(g, "SENDS FROM", r.removeFromTop(20 * u), 8.5f, Theme::sub(), 2);
    // strip chips, two rows
    std::vector<int> strips; for (int s = 0; s < nl; ++s) strips.push_back(s); for (int k = 0; k < ntn; ++k) strips.push_back(4 + k);
    int perRow = 6;
    for (size_t i = 0; i < strips.size(); i += (size_t) perRow) {
        auto row = r.removeFromTop(38 * u);
        float w = row.getWidth() / perRow;
        for (size_t j = i; j < juce::jmin(strips.size(), i + (size_t) perRow); ++j) {
            int s = strips[j];
            button(g, row.removeFromLeft(w).reduced(2 * u, 3 * u), s < 4 ? "L" + juce::String(s + 1) : "T" + juce::String(s - 3), fxStrip == s ? Theme::strip(s) : Theme::btn(), [this, s] { fxStrip = s; send("sel " + juce::String(s)); }, 9.0f);
        }
    }
    r.removeFromTop(6 * u);
    auto cards = juce::StringArray::fromTokens(st.inf("cards"), ",", "");
    for (auto& cd : cardTable()) {
        if (!cards.contains(cd.key)) continue;
        std::string key = cd.key;
        bool open = openCard == cd.key;
        auto head = r.removeFromTop(50 * u).reduced(0, 3 * u);
        if (Theme::softUI()) softRaised(g, head, 18 * u, 5.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(head, 12 * u); }
        g.setColour(Theme::fx(key)); g.fillEllipse(head.getX() + 12 * u, head.getCentreY() - 5 * u, 10 * u, 10 * u);
        text(g, U8(cd.name), head.withTrimmedLeft(30 * u).withWidth(130 * u), 11.0f, Theme::txt(), 2);
        // the send of the chosen strip, right on the header
        auto sp = Model::stripParam(fxStrip, key);
        bool hasSend = false; for (auto& k : cd.knobs) if (std::string(k.id) == "SEND") hasSend = true;
        if (hasSend && !sp.empty()) slider(g, head.withTrimmedLeft(160 * u).withTrimmedRight(44 * u).reduced(4 * u, 7 * u), "SEND", sp, Theme::fx(key));
        else if (hasSend) text(g, U8("no send on this strip"), head.withTrimmedLeft(160 * u).withTrimmedRight(44 * u), 8.0f, Theme::sub());
        button(g, head.removeFromRight(40 * u).reduced(4 * u, 8 * u), open ? U8("▴") : U8("▾"), Theme::btn(), [this, key, open] { openCard = open ? juce::String() : juce::String(key); }, 10.0f);
        if (open) {
            for (auto& k : cd.knobs) {
                std::string id = k.id; if (id == "SEND") continue;
                slider(g, r.removeFromTop(40 * u).reduced(10 * u, 4 * u), U8(k.label), id, Theme::fx(key));
            }
            if (key == "frz") slider(g, r.removeFromTop(40 * u).reduced(10 * u, 4 * u), "HOLD", "frz_on", Theme::fx(key));
            if (key == "drm") slider(g, r.removeFromTop(40 * u).reduced(10 * u, 4 * u), "DRUMS ON", "drm_on", Theme::green());
            r.removeFromTop(6 * u);
        }
    }
    r.removeFromTop(8 * u);
    text(g, U8("Which effects show: choose them on the station (EFFECTS ⚙)."), r.removeFromTop(26 * u), 8.0f, Theme::sub(), 0, juce::Justification::centred);
    scrollMax = juce::jmax(0.0f, (r.getY() + scroll - y0) - content.getHeight());
}

// ---------------------------------------------------------------- SYNTH: slide pad, keys, chords (they play the station's SYNTH)
// ---------------------------------------------------------------- AUDIO: which microphone, which output, listening to the station
void PhoneRemote::drawAudio(juce::Graphics& g, juce::Rectangle<float> r) {
    float y0 = r.getY();
    double now = juce::Time::getMillisecondCounterHiRes();
   #if JUCE_IOS
    if (now - inputsAt > 1000) { inputsAt = now; inputList.clear(); for (auto& p : ios::inputs()) inputList.add({ p.id, p.name }); }
    juce::String chosen = ios::chosenInput(), curIn = ios::currentInputName(), curOut = ios::currentOutputName();
    bool bt = ios::outputIsBluetooth();
   #else
    if (inputList.isEmpty()) inputList.add({ "auto", "AUTOMATIC" });
    juce::String chosen = "auto", curIn = "built-in mic", curOut = "speaker"; bool bt = false; juce::ignoreUnused(now);
   #endif
    text(g, U8("MICROPHONE"), r.removeFromTop(26 * u), 11.0f, Theme::txt(), 2);
    text(g, U8("now: ") + curIn, r.removeFromTop(20 * u), 8.5f, Theme::sub(), 1);
    for (auto& in : inputList) {
        auto row = r.removeFromTop(42 * u).reduced(0, 3 * u);
        bool sel = chosen == in.first;
        juce::String id = in.first;
        button(g, row, in.second, sel ? Theme::green() : Theme::btn(), [this, id] {
           #if JUCE_IOS
            ios::chooseInput(id); inputsAt = 0; host.saveSetup();
           #else
            juce::ignoreUnused(id);
           #endif
            repaint(); }, 9.0f);
    }
    text(g, U8("Plug a cable or USB microphone / interface in and it appears here. A Bluetooth headset's microphone is phone-call quality - for your instrument use the built-in or a cable one."),
         r.removeFromTop(48 * u), 7.5f, Theme::sub());
    r.removeFromTop(10 * u);
    text(g, U8("OUTPUT"), r.removeFromTop(26 * u), 11.0f, Theme::txt(), 2);
    text(g, U8("now: ") + curOut, r.removeFromTop(22 * u), 9.0f, Theme::txt(), 1);
    auto orow = r.removeFromTop(44 * u).reduced(0, 3 * u);
    button(g, orow, U8("CHOOSE WHERE THE SOUND GOES ▸"), Theme::blue(), [this] {
       #if JUCE_IOS
        ios::showOutputPicker();
       #endif
        repaint(); }, 9.0f);
    auto srow = r.removeFromTop(44 * u).reduced(0, 3 * u);
   #if JUCE_IOS
    bool spk = ios::speakerForced();
   #else
    bool spk = false;
   #endif
    button(g, srow, spk ? U8("● THE PHONE'S SPEAKER (forced)") : U8("THE PHONE'S SPEAKER"), spk ? Theme::amber() : Theme::btn(), [this] {
       #if JUCE_IOS
        ios::forceSpeaker(! ios::speakerForced()); host.saveSetup();
       #endif
        repaint(); }, 9.0f);
    text(g, U8("Bluetooth headphones or speakers, AirPlay, a cable - connected Bluetooth headphones take the sound by themselves. ")
         + (bt ? U8("Bluetooth delays the sound (about 0.2 s): good for listening, not for playing in time.") : juce::String()),
         r.removeFromTop(46 * u), 7.5f, bt ? Theme::amber() : Theme::sub());
    r.removeFromTop(10 * u);
    // ---- listening to the station
    text(g, U8("LISTEN TO THE STATION HERE"), r.removeFromTop(26 * u), 11.0f, Theme::txt(), 2);
    auto lrow = r.removeFromTop(48 * u);
    const char* LN[3] = { "OFF", "MAIN MIX", "HEADPHONE MIX" };
    float lw = lrow.getWidth() / 3.0f;
    for (int i = 0; i < 3; ++i) {
        bool sel = host.listenWant.load() == i;
        button(g, lrow.removeFromLeft(lw).reduced(3 * u, 4 * u), LN[i], sel ? (i == 0 ? Theme::sel() : Theme::green()) : Theme::btn(), [this, i] {
            host.listenWant.store(i); host.phoneLink.listen.reset(); send("listen " + juce::String(i)); host.saveSetup(); repaint(); }, 8.5f);
    }
    {   // the volume (this phone only)
        auto vr = r.removeFromTop(44 * u).reduced(0, 6 * u);
        float v = host.listenVol.load();
        if (Theme::softUI()) softWellLite(g, vr, vr.getHeight() * 0.5f); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(vr, 3 * u); }
        g.setColour(Theme::arc(Theme::green()).withAlpha(0.9f)); g.fillRoundedRectangle(vr.withWidth(juce::jmax(vr.getHeight() * 0.3f, vr.getWidth() * v)), vr.getHeight() * 0.3f);
        text(g, U8("VOLUME"), vr.reduced(12 * u, 0), 9.5f, Theme::txt(), 2);
        text(g, juce::String(juce::roundToInt(v * 100)), vr.reduced(12 * u, 0), 9.5f, Theme::txt(), 1, juce::Justification::centredRight);
        ctls.push_back({ vr, 1, "listenvol", 0, 1, nullptr, true });
        auto lv = r.removeFromTop(14 * u).withSizeKeepingCentre(vr.getWidth(), 6 * u);
        if (Theme::softUI()) softWellLite(g, lv, lv.getHeight() * 0.5f); else { g.setColour(Theme::card2()); g.fillRoundedRectangle(lv, 3 * u); }
        g.setColour(Theme::green()); g.fillRoundedRectangle(lv.withWidth(lv.getWidth() * juce::jmin(1.0f, host.phoneLink.listen.level.load())), 3 * u);
    }
    text(g, host.phoneLink.connected() ? U8("The station's sound comes to this phone over Wi-Fi or the cable (about ") + juce::String(juce::roundToInt(host.phoneLink.listen.delayMs.load() + 15)) + U8(" ms late): to hear the mix around the room, or your own headphone mix (CUE, click).")
                                       : U8("Connect to a station first (PLAY page)."), r.removeFromTop(52 * u), 7.5f, Theme::sub());
    scrollMax = juce::jmax(0.0f, r.getY() - y0 - content.getHeight() + 20 * u);
}

void PhoneRemote::drawSynth(juce::Graphics& g, juce::Rectangle<float> r) {
    scrollMax = 0;
    auto top = r.removeFromTop(44 * u);
    const char* M[3] = { "SLIDE PAD", "KEYS", "CHORDS" };
    for (int i = 0; i < 3; ++i) button(g, top.removeFromLeft(top.getWidth() / (3 - i)).reduced(3 * u, 5 * u), M[i], synthMode == i ? Theme::violet() : Theme::btn(), [this, i] { synthMode = i; send("alloff"); }, 9.0f, false);
    auto oct = r.removeFromTop(40 * u);
    button(g, oct.removeFromLeft(56 * u).reduced(3 * u, 5 * u), U8("−"), Theme::btn(), [this] { octave = juce::jmax(1, octave - 1); }, 14.0f, false);
    text(g, "octave " + juce::String(octave), oct.removeFromLeft(90 * u), 10.0f, Theme::txt(), 2, juce::Justification::centred);
    button(g, oct.removeFromLeft(56 * u).reduced(3 * u, 5 * u), "+", Theme::btn(), [this] { octave = juce::jmin(7, octave + 1); }, 14.0f, false);
    int key = (int) st.snap("halo_key"); if (key < 0) key = 0;
    text(g, juce::String("key ") + analysis::noteName(key), oct, 9.0f, Theme::sub(), 1, juce::Justification::centredRight);
    r.removeFromTop(6 * u);
    g.setColour(Theme::scr()); g.fillRoundedRectangle(r, 14 * u);
    if (synthMode == 0) {
        // the pad: left - right = pitch (two octaves, gliding), down - up = loudness
        for (int i = 0; i <= 24; ++i) {
            float x = r.getX() + r.getWidth() * i / 24.0f;
            int pc = (key + i) % 12;
            bool inKey = pc == key || pc == (key + 2) % 12 || pc == (key + 4) % 12 || pc == (key + 5) % 12 || pc == (key + 7) % 12 || pc == (key + 9) % 12 || pc == (key + 11) % 12;
            g.setColour(Theme::txt().withAlpha(i % 12 == 0 ? 0.35f : (inKey ? 0.14f : 0.05f)));
            g.fillRect(x, r.getY() + 10 * u, 1.0f, r.getHeight() - 20 * u);
        }
        text(g, U8("← lower   ·   higher →"), r.removeFromBottom(26 * u), 8.0f, Theme::sub(), 0, juce::Justification::centred);
        text(g, U8("louder ↑"), r.withHeight(26 * u), 8.0f, Theme::sub(), 0, juce::Justification::centred);
        if (padNote >= 0) { g.setColour(Theme::violet().withAlpha(0.8f)); g.fillEllipse(juce::Rectangle<float>(44 * u, 44 * u).withCentre(downAt)); }
        ctls.push_back({ r, 2, {}, 0, 0, nullptr, false });
    } else if (synthMode == 1) {
        // two octaves of keys
        int white = 15; float ww = r.getWidth() / white;
        static const int W[7] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int i = 0; i < white; ++i) { auto k = juce::Rectangle<float>(r.getX() + i * ww, r.getY(), ww, r.getHeight()).reduced(1.5f);
            g.setColour(juce::Colours::white.withAlpha(0.92f)); g.fillRoundedRectangle(k, 6 * u);
            int note = 12 * (octave + 1) + 12 * (i / 7) + W[i % 7];
            if (padNotes.size() > 0) for (auto& kv : padNotes) if (kv.second == note) { g.setColour(Theme::violet().withAlpha(0.6f)); g.fillRoundedRectangle(k, 6 * u); } }
        static const int B[5] = { 1, 3, 6, 8, 10 }; static const float BX[5] = { 1, 2, 4, 5, 6 };
        for (int o = 0; o < 2; ++o) for (int j = 0; j < 5; ++j) {
            auto k = juce::Rectangle<float>(r.getX() + (o * 7 + BX[j]) * ww - ww * 0.32f, r.getY(), ww * 0.64f, r.getHeight() * 0.6f);
            g.setColour(juce::Colours::black.withAlpha(0.85f)); g.fillRoundedRectangle(k, 5 * u);
            int note = 12 * (octave + 1) + 12 * o + B[j];
            for (auto& kv : padNotes) if (kv.second == note) { g.setColour(Theme::violet()); g.fillRoundedRectangle(k, 5 * u); }
        }
        ctls.push_back({ r, 3, {}, 0, 0, nullptr, false });
    } else {
        // chords of the key: I ii iii IV V vi, and a sus / seventh row
        static const char* CN[8] = { "I", "ii", "iii", "IV", "V", "vi", "V7", "I+" };
        float cw = r.getWidth() / 4, ch = r.getHeight() / 2;
        for (int i = 0; i < 8; ++i) {
            auto cr = juce::Rectangle<float>(r.getX() + (i % 4) * cw, r.getY() + (i / 4) * ch, cw, ch).reduced(5 * u);
            bool on = false; for (auto& kv : padNotes) if (kv.second == 1000 + i) on = true;
            g.setColour(on ? Theme::violet() : Theme::card()); g.fillRoundedRectangle(cr, 12 * u);
            text(g, CN[i], cr, 16.0f, on ? Theme::textOn(Theme::violet()) : Theme::txt(), 2, juce::Justification::centred);
        }
        ctls.push_back({ r, 4, {}, 0, 0, nullptr, false });
    }
}

// ---------------------------------------------------------------- touch
void PhoneRemote::mouseDown(const juce::MouseEvent& e) {
    auto p = e.position;
    downAt = p; dragging = false; scrollingPage = false; scrollAtDown = scroll; active = -1;
    for (int i = (int) ctls.size(); --i >= 0;) if (ctls[(size_t) i].r.contains(p)) { active = i; break; }
    if (active >= 0) {
        auto& c = ctls[(size_t) active];
        if (c.kind == 1) valueAtDown = c.id == "listenvol" ? (double) host.listenVol.load() : st.par(c.id);
        if (c.kind >= 2) { padTouch(e, true, false); return; }
    }
    repaint();
}
void PhoneRemote::mouseDrag(const juce::MouseEvent& e) {
    auto d = e.position - downAt;
    if (active >= 0 && active < (int) ctls.size() && ctls[(size_t) active].kind >= 2) { padTouch(e, false, false); return; }
    if (!dragging && d.getDistanceFromOrigin() > 8 * u) {
        dragging = true;
        bool slide = active >= 0 && ctls[(size_t) active].kind == 1 && std::abs(d.x) > std::abs(d.y);
        scrollingPage = !slide && content.contains(downAt);
    }
    if (!dragging) return;
    if (scrollingPage) { scroll = juce::jlimit(0.0f, scrollMax, scrollAtDown - d.y); repaint(); return; }
    if (active >= 0 && ctls[(size_t) active].kind == 1) {
        auto& c = ctls[(size_t) active];
        double v = valueAtDown + d.x / juce::jmax(40.0f, c.r.getWidth()) * (c.hi - c.lo);
        setPar(c.id, v); repaint();
    }
}
void PhoneRemote::mouseUp(const juce::MouseEvent& e) {
    if (active >= 0 && active < (int) ctls.size()) {
        auto& c = ctls[(size_t) active];
        if (c.kind >= 2) { padTouch(e, false, true); active = -1; repaint(); return; }
        if (!dragging && c.r.contains(e.position)) {
            if (c.kind == 0 && c.tap) { auto f = c.tap; f(); }
            else if (c.kind == 1) {                       // a tap on a slider: jump there
                double t = (e.position.x - c.r.getX()) / c.r.getWidth();
                setPar(c.id, c.lo + t * (c.hi - c.lo));
            }
        }
    }
    active = -1; dragging = false; repaint();
}

void PhoneRemote::padTouch(const juce::MouseEvent& e, bool down, bool up) {
    auto& c = ctls[(size_t) active];
    auto r = c.r;
    int idx = e.source.getIndex();
    auto p = e.position;
    int key = juce::jmax(0, (int) st.snap("halo_key"));
    if (c.kind == 2) {                                    // slide pad: one gliding voice
        double x = juce::jlimit(0.0, 1.0, (double) ((p.x - r.getX()) / r.getWidth())), y = juce::jlimit(0.0, 1.0, 1.0 - (double) ((p.y - r.getY()) / r.getHeight()));
        double pitch = 12 * (octave + 1) + key + x * 24.0;
        if (down) { padNote = 1; send("padon " + juce::String(pitch, 3) + " " + juce::String(0.3 + 0.7 * y, 3)); downAt = p; }
        else if (up) { padNote = -1; send("padoff"); }
        else { send("padmove " + juce::String(pitch, 3) + " " + juce::String(y, 3)); downAt = p; }
        repaint(); return;
    }
    auto release = [&] { auto it = padNotes.find(idx); if (it == padNotes.end()) return;
        int n = it->second;
        if (n >= 1000) { static const int CH[8][4] = { { 0, 4, 7, 12 }, { 2, 5, 9, -1 }, { 4, 7, 11, -1 }, { 5, 9, 12, -1 }, { 7, 11, 14, -1 }, { 9, 12, 16, -1 }, { 7, 11, 14, 17 }, { 12, 16, 19, 24 } };
            for (int v : CH[n - 1000]) if (v >= 0) send("noteoff " + juce::String(12 * (octave + 1) + key + v)); }
        else send("noteoff " + juce::String(n));
        padNotes.erase(it); };
    if (up) { release(); repaint(); return; }
    int note = -1;
    if (c.kind == 3) {                                    // keys
        int white = 15; float ww = r.getWidth() / white;
        static const int W[7] = { 0, 2, 4, 5, 7, 9, 11 };
        int wi = juce::jlimit(0, white - 1, (int) ((p.x - r.getX()) / ww));
        note = 12 * (octave + 1) + 12 * (wi / 7) + W[wi % 7];
        if (p.y < r.getY() + r.getHeight() * 0.6f) {
            static const int B[5] = { 1, 3, 6, 8, 10 }; static const float BX[5] = { 1, 2, 4, 5, 6 };
            for (int o = 0; o < 2; ++o) for (int j = 0; j < 5; ++j) { float cx = r.getX() + (o * 7 + BX[j]) * ww; if (std::abs(p.x - cx) < ww * 0.32f) note = 12 * (octave + 1) + 12 * o + B[j]; }
        }
    } else {                                              // chords
        float cw = r.getWidth() / 4, chh = r.getHeight() / 2;
        int i = juce::jlimit(0, 3, (int) ((p.x - r.getX()) / cw)) + 4 * juce::jlimit(0, 1, (int) ((p.y - r.getY()) / chh));
        note = 1000 + i;
    }
    auto it = padNotes.find(idx);
    if (it != padNotes.end() && it->second == note) return;   // still on the same key
    release();
    if (note >= 1000) { static const int CH[8][4] = { { 0, 4, 7, 12 }, { 2, 5, 9, -1 }, { 4, 7, 11, -1 }, { 5, 9, 12, -1 }, { 7, 11, 14, -1 }, { 9, 12, 16, -1 }, { 7, 11, 14, 17 }, { 12, 16, 19, 24 } };
        for (int v : CH[note - 1000]) if (v >= 0) send("noteon " + juce::String(12 * (octave + 1) + key + v) + " 0.8"); }
    else {
        double vel = 0.45 + 0.55 * juce::jlimit(0.0, 1.0, (double) ((p.y - r.getY()) / r.getHeight()));   // lower on the key = louder (like a real key)
        send("noteon " + juce::String(note) + " " + juce::String(vel, 2));
    }
    padNotes[idx] = note;
    repaint();
}

} // namespace arco
