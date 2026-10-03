#include "StationView.h"

namespace score {

static Project& P() { return *host().project; }

static double regionEnd(int i) {
    auto& p = P(); auto& st = p.stations;
    if (i + 1 < (int) st.size()) return st[(size_t) i + 1].beat;
    return std::max(st[(size_t) i].beat + p.beatsPerBar * 4, p.length());
}

// ======================================================================== the line of stations
StationLine::StationLine() {}

juce::Rectangle<float> StationLine::cardRect(int i) const {
    return { 20.0f + i * (float) (cardW + gap) - scroll, 18.0f, (float) cardW, (float) getHeight() - 30.0f };
}

void StationLine::paint(juce::Graphics& g) {
    auto& st = P().stations; auto& sp = host().stations;
    float cy = getHeight() * 0.5f + 6;
    g.setColour(Theme::line()); g.fillRoundedRectangle(10, cy - 1.5f, (float) getWidth() - 20, 3, 1.5f);
    for (int i = 0; i < (int) st.size(); ++i) {
        auto r = cardRect(i); if (r.getRight() < 0 || r.getX() > getWidth()) continue;
        auto& s = st[(size_t) i];
        bool now = i == sp.current, isShown = i == shown;
        // the glide into the current station runs along the line
        if (now && sp.gliding() && i > 0) { auto pr = cardRect(i - 1); float x0 = pr.getRight(), x1 = r.getX();
            g.setColour(Theme::gold()); g.fillRoundedRectangle(x0, cy - 2, (x1 - x0) * (float) sp.glideProgress(), 4, 2); }
        g.setColour(now ? Theme::gold() : Theme::card()); g.fillRoundedRectangle(r, 12);
        if (isShown) { g.setColour(Theme::blue()); g.drawRoundedRectangle(r.reduced(1), 12, 2.0f); }
        auto tc = now ? (Theme::lightMode() ? juce::Colours::white : juce::Colour(0xff1a1a1a)) : Theme::txt();
        // the number
        auto nr = juce::Rectangle<float>(r.getX() + 10, r.getY() + 10, 26, 26);
        g.setColour(now ? juce::Colour(0x33000000) : Theme::card2()); g.fillEllipse(nr);
        g.setColour(tc); g.setFont(Theme::font(11, 2)); g.drawText(juce::String(i + 1), nr, juce::Justification::centred);
        g.setFont(Theme::font(10.5f, 2)); g.drawFittedText(s.name.isEmpty() ? juce::String("Station " + juce::String(i + 1)) : s.name, r.toNearestInt().withTrimmedLeft(44).withTrimmedRight(8).withHeight(26).translated(0, 10), juce::Justification::centredLeft, 2, 0.8f);
        auto sub = now ? tc.withAlpha(0.7f) : Theme::sub();
        int bar = (int) (s.beat / P().beatsPerBar) + 1;
        label(g, "bar " + juce::String(bar) + U("  ·  glide ") + juce::String(s.glide, 1) + " s", r.toNearestInt().reduced(12, 0).withY((int) r.getY() + 44).withHeight(14), 8.0f, 1, sub);
        juce::String end = s.atEnd == 0 ? U("then goes on") : s.atEnd == 1 ? U("loops until GO") : U("waits for GO");
        if (s.autoGo > 0) end = U("GO by itself after ") + juce::String(s.autoGo, 1) + " s";
        label(g, end, r.toNearestInt().reduced(12, 0).withY((int) r.getY() + 60).withHeight(14), 8.0f, 1, sub);
        int clips = 0; double a = s.beat, b = regionEnd(i);
        for (auto& t : P().tracks) for (auto& c : t->clips) if (c->start < b - 1e-6 && c->end() > a + 1e-6) ++clips;
        if (r.getHeight() > 92) label(g, (clips ? juce::String(clips) + (clips == 1 ? " clip plays here" : " clips play here") : juce::String("nothing recorded here yet")) + (s.note.isNotEmpty() ? U("  ·  ") + s.note : juce::String()),
                                      r.toNearestInt().reduced(12, 0).withY((int) r.getY() + 76).withHeight(14), 7.5f, 0, sub);
        if (!s.hasSnap) label(g, "no sound kept", r.toNearestInt().reduced(12, 6).removeFromBottom(12), 7.5f, 2, now ? tc : Theme::amber());
    }
    // the + card
    auto r = cardRect((int) st.size()).withWidth(110);
    g.setColour(Theme::line()); juce::Path p; p.addRoundedRectangle(r, 12); float dash[2] = { 5, 4 }; juce::PathStrokeType(1.2f).createDashedStroke(p, p, dash, 2); g.fillPath(p);
    label(g, U("+ station"), r.toNearestInt(), 10.0f, 2, Theme::sub(), juce::Justification::centred);
}

void StationLine::mouseDown(const juce::MouseEvent& e) {
    auto& st = P().stations;
    for (int i = 0; i <= (int) st.size(); ++i) if (cardRect(i).contains(e.position)) {
        if (i == (int) st.size()) { if (auto* v = findParentComponentOfClass<StationView>()) v->addStation(false); return; }
        shown = i; dragFrom = i; dragging = false;
        auto& sp = host().stations;
        if (sp.perform) sp.goTo(i); else sp.select(i);
        if (onShow) onShow(); repaint(); return;
    }
}
void StationLine::mouseDrag(const juce::MouseEvent& e) {
    if (dragFrom < 0) { scroll = juce::jmax(0.0f, scroll - (float) e.getDistanceFromDragStartX() * 0.1f); repaint(); }
}
void StationLine::mouseUp(const juce::MouseEvent&) { dragFrom = -1; }
void StationLine::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    float total = (float) (P().stations.size() + 1) * (cardW + gap) + 140;
    scroll = juce::jlimit(0.0f, juce::jmax(0.0f, total - getWidth()), scroll - (w.deltaX != 0 ? w.deltaX : w.deltaY) * 300.0f); repaint();
}

// ======================================================================== the view
StationView::StationView() {
    addAndMakeVisible(line);
    line.onShow = [this] { showStation(line.shown); };
    for (auto* b : { &goBtn, &backBtn }) { addAndMakeVisible(*b); b->pad = 0.3f; }
    goBtn.bg = [] { return Theme::gold(); }; goBtn.iconColour = [] { return Theme::lightMode() ? juce::Colours::white : juce::Colour(0xff1a1a1a); };
    goBtn.onClick = [] { auto& sp = host().stations; if (!sp.perform) { sp.perform = true; } sp.go(); };
    backBtn.onClick = [] { host().stations.back(); };
    for (auto* p : { &startBtn, &modeBtn, &storeBtn, &autoBtn, &followBtn, &addBtn, &addHereBtn, &recHere, &playHere, &loopHere, &dupBtn, &delBtn, &sameBtn, &curveBtn, &endBtn, &jumpBtn }) {
        addAndMakeVisible(*p); p->fsize = 9.0f; p->radius = 6.0f; }
    startBtn.dynText = [] { return host().stations.pieceSeconds() > 0 ? juce::String("STOP THE PIECE") : juce::String("START THE PIECE"); };
    startBtn.lit = [] { return host().stations.pieceSeconds() > 0; }; startBtn.litColour = [] { return Theme::red(); };
    startBtn.onClick = [] { auto& sp = host().stations; if (sp.pieceSeconds() > 0) { sp.stopPerformance(); host().stop(); } else sp.startPerformance(); host().sendChangeMessage(); };
    startBtn.setTooltip("from the first station; the log below keeps the times");
    modeBtn.dynText = [] { return host().stations.perform ? juce::String("PERFORM") : juce::String("COMPOSE"); };
    modeBtn.lit = [] { return host().stations.perform; }; modeBtn.litColour = [] { return Theme::green(); };
    modeBtn.onClick = [] { auto& sp = host().stations; sp.perform = !sp.perform; if (!sp.perform) sp.stopTransportGuards(); host().sendChangeMessage(); };
    modeBtn.setTooltip("PERFORM: GO plays the glide and the transport follows. COMPOSE: a click jumps to a station's sound at once, nothing plays");
    storeBtn.bg = [] { return Theme::gold().withAlpha(0.85f); }; storeBtn.textColour = [] { return Theme::lightMode() ? juce::Colours::white : juce::Colour(0xff1a1a1a); };
    storeBtn.onClick = [] { host().stations.store(); };
    storeBtn.setTooltip("keep every knob of the piece, as it is now, in the current station");
    autoBtn.lit = [] { return host().stations.autoStore; }; autoBtn.litColour = [] { return Theme::gold().darker(0.25f); };
    autoBtn.onClick = [] { host().stations.autoStore = !host().stations.autoStore; host().sendChangeMessage(); };
    autoBtn.setTooltip("every knob you turn is kept in the station you are in (when you move on, the next station brings back its own knobs)");
    followBtn.lit = [] { return host().stations.follow; }; followBtn.litColour = [] { return Theme::blue(); };
    followBtn.onClick = [] { host().stations.follow = !host().stations.follow; host().sendChangeMessage(); };
    followBtn.setTooltip("while the arranger plays, reaching a station brings its sound (with its glide)");
    addBtn.onClick = [this] { addStation(false); }; addHereBtn.onClick = [this] { addStation(true); };
    addBtn.bg = addHereBtn.bg = [] { return Theme::blue().withAlpha(0.75f); };
    recHere.bg = [] { return Theme::red().withAlpha(0.85f); };
    recHere.dynText = [] { return host().recording() ? juce::String("STOP RECORDING") : juce::String("RECORD IN THIS STATION"); };
    recHere.onClick = [this] {
        auto& h = host(); if (h.recording()) { h.record(); return; }
        if (!st()) return;
        auto* t = h.selectedTrack();
        if (!t || (t->kind != TrackKind::Audio && !t->isInstrument())) { for (auto& x : P().tracks) if (x->kind == TrackKind::Audio) { t = x.get(); break; } }
        if (!t) { h.say("add an audio track first (+ TRACK in the arranger)"); return; }
        if (!t->arm) { for (auto& x : P().tracks) x->arm = false; t->arm = true; h.selectTrack(t->id); h.rebuild(); }
        h.stations.select(line.shown);
        h.engine.stop(); h.locate(st()->beat);
        h.record();
        h.say("recording into station " + juce::String(line.shown + 1) + " on " + t->name + U(" … it plays whenever you come back to this station"));
    };
    recHere.setTooltip("arms the selected track (or the first audio track), starts at the station and records: the take belongs to this station");
    playHere.onClick = [this] { if (!st()) return; host().stations.select(line.shown); host().locate(st()->beat); host().play(); };
    loopHere.onClick = [this] { if (!st()) return; int i = line.shown; P().loopOn = true; host().setLoopRange(st()->beat, regionEnd(i)); host().locate(st()->beat); host().say("looping station " + juce::String(i + 1)); };
    loopHere.lit = [this] { auto* s = st(); return s && P().loopOn && std::abs(P().loopStart - s->beat) < 1e-6; };
    dupBtn.onClick = [this] { int i = line.shown; if (!st()) return;
        host().edit("duplicate station", [i] { auto& v = P().stations; Station s = v[(size_t) i]; s.id = newUid(); s.name << " copy"; s.beat = regionEnd(i);
            for (size_t k = (size_t) i + 1; k < v.size(); ++k) v[k].beat += regionEnd(i) - v[(size_t) i].beat;
            v.push_back(s); P().sortStations(); }, false);
        line.shown = i + 1; showStation(i + 1); };
    delBtn.onClick = [this] { int i = line.shown; if (!st()) return;
        host().edit("delete station", [i] { auto& v = P().stations; v.erase(v.begin() + i); auto& sp = host().stations; if (sp.current >= (int) v.size()) sp.current = (int) v.size() - 1; }, false);
        showStation(juce::jmax(0, i - 1)); };
    delBtn.bg = [] { return Theme::btn(); };
    sameBtn.onClick = [this] { int i = line.shown; if (i <= 0 || !st()) return;
        host().edit("same sound", [i] { auto& v = P().stations; v[(size_t) i].snap = v[(size_t) i - 1].snap; v[(size_t) i].hasSnap = v[(size_t) i - 1].hasSnap; }, false);
        host().stations.select(i); computeChanges(); };
    sameBtn.setTooltip("this station takes the sound of the station before it (then change what you want)");
    for (auto* e : { &name, &note }) { addAndMakeVisible(*e); styleEditor(*e, 10.5f); }
    name.setTextToShowWhenEmpty("name", Theme::sub()); note.setTextToShowWhenEmpty(U("a note for you (what to play, where to look …)"), Theme::sub());
    name.onTextChange = [this] { if (auto* s = st()) { s->name = name.getText(); host().dirty = true; line.repaint(); repaint(); } };
    note.onTextChange = [this] { if (auto* s = st()) { s->note = note.getText(); host().dirty = true; line.repaint(); } };
    barBox = std::make_unique<ValueBox>([this] { auto* s = st(); return s ? s->beat / P().beatsPerBar + 1 : 1.0; },
                                        [this](double v) { if (auto* s = st()) { host().knobTouched(); Uid id = s->id; s->beat = (std::round(v) - 1) * P().beatsPerBar; P().sortStations(); line.shown = P().stationIndex(id); host().sendChangeMessage(); } }, 1, 999, 0, 1);
    glideBox = std::make_unique<ValueBox>([this] { auto* s = st(); return s ? (double) s->glide : 0.0; }, [this](double v) { if (auto* s = st()) { host().knobTouched(); s->glide = (float) v; line.repaint(); } }, 0, 60, 1, 0.1);
    autoGoBox = std::make_unique<ValueBox>([this] { auto* s = st(); return s ? (double) s->autoGo : 0.0; }, [this](double v) { if (auto* s = st()) { host().knobTouched(); s->autoGo = (float) v; line.repaint(); } }, 0, 600, 1, 0.5);
    glideBox->format = [](double v) { return v <= 0.001 ? juce::String("at once") : juce::String(v, 1) + " s"; };
    autoGoBox->format = [](double v) { return v <= 0.001 ? juce::String("wait for GO") : juce::String(v, 1) + " s"; };
    barBox->format = [](double v) { return "bar " + juce::String((int) v); };
    for (auto* b : { barBox.get(), glideBox.get(), autoGoBox.get() }) { addAndMakeVisible(*b); b->fsize = 10.0f; }
    curveBtn.dynText = [this] { static const char* N[4] = { "LINEAR", "SMOOTH", "SLOW START", "FAST START" }; auto* s = st(); return juce::String(s ? N[juce::jlimit(0, 3, s->curve)] : ""); };
    curveBtn.onClick = [this] { juce::PopupMenu m; const char* N[4] = { "Linear", "Smooth (S-curve)", "Slow start", "Fast start" }; auto* s = st(); if (!s) return;
        for (int i = 0; i < 4; ++i) m.addItem(i + 1, N[i], true, s->curve == i);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&curveBtn), [this](int r) { if (auto* s2 = st(); s2 && r > 0) { host().knobTouched(); s2->curve = r - 1; repaint(); } }); };
    endBtn.dynText = [this] { static const char* N[3] = { "GO ON", "LOOP UNTIL GO", "WAIT FOR GO" }; auto* s = st(); return juce::String(s ? N[juce::jlimit(0, 2, s->atEnd)] : ""); };
    endBtn.onClick = [this] { juce::PopupMenu m; auto* s = st(); if (!s) return;
        m.addItem(1, "Go on into the next station", true, s->atEnd == 0); m.addItem(2, "Loop this station until GO", true, s->atEnd == 1); m.addItem(3, "Stop and wait for GO", true, s->atEnd == 2);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&endBtn), [this](int r) { if (auto* s2 = st(); s2 && r > 0) { host().knobTouched(); s2->atEnd = r - 1; line.repaint(); repaint(); } }); };
    jumpBtn.dynText = [this] { static const char* N[3] = { "AT ONCE", "ON THE NEXT BEAT", "ON THE NEXT BAR" }; auto* s = st(); return juce::String(s ? N[juce::jlimit(0, 2, s->jump)] : ""); };
    jumpBtn.onClick = [this] { juce::PopupMenu m; auto* s = st(); if (!s) return;
        m.addItem(1, "At once", true, s->jump == 0); m.addItem(2, "On the next beat", true, s->jump == 1); m.addItem(3, "On the next bar", true, s->jump == 2);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&jumpBtn), [this](int r) { if (auto* s2 = st(); s2 && r > 0) { host().knobTouched(); s2->jump = r - 1; repaint(); } }); };
    startTimerHz(30);
}

Station* StationView::st() { auto& v = P().stations; return line.shown >= 0 && line.shown < (int) v.size() ? &v[(size_t) line.shown] : nullptr; }

void StationView::addStation(bool atPlayhead) {
    int made = 0;
    host().edit("new station", [&] {
        auto& p = P(); Station s; s.id = newUid();
        double b = atPlayhead ? std::floor(host().engine.position() / p.beatsPerBar) * p.beatsPerBar
                              : (p.stations.empty() ? 0.0 : std::max(p.stations.back().beat + p.beatsPerBar * 4, std::ceil(p.length() / p.beatsPerBar) * p.beatsPerBar));
        s.beat = b; s.name = "Station " + juce::String((int) p.stations.size() + 1);
        s.snap = p.captureKnobs(); s.hasSnap = true;
        p.stations.push_back(s); p.sortStations(); made = p.stationIndex(s.id);
    }, false);
    host().stations.select(made);
    showStation(made);
    host().say("station " + juce::String(made + 1) + U(" keeps the sound as it is now · turn knobs to change it"));
}

void StationView::showStation(int i) {
    line.shown = juce::jlimit(0, juce::jmax(0, (int) P().stations.size() - 1), i);
    if (auto* s = st()) { name.setText(s->name, false); note.setText(s->note, false); }
    else { name.setText({}, false); note.setText({}, false); }
    float x = line.cardRect(line.shown).getX();
    if (x < 0 || x > line.getWidth() - StationLine::cardW) line.scroll = juce::jmax(0.0f, line.scroll + x - 40);
    computeChanges();
    resized(); repaint(); line.repaint();
}

void StationView::computeChanges() {
    changeList.clear();
    auto& p = P(); int i = line.shown;
    if (i < 0 || i >= (int) p.stations.size()) return;
    auto& s = p.stations[(size_t) i];
    if (!s.hasSnap) { changeList.add(U("this station keeps no sound yet: STORE keeps the knobs as they are now")); return; }
    if (i == 0) { changeList.add(U("the first station: the piece starts with this sound")); }
    auto m = p.paramMap();
    std::map<Uid, juce::String> fxName;
    for (auto* t : p.allTracks()) { auto add = [&](Effect& e) { auto* inf = effectInfo(e.type); fxName[e.uid] = t->name + U(" · ") + (inf ? inf->name : e.type.c_str()); };
        for (auto& e : t->fx) add(*e); for (auto& c : t->clips) for (auto& e : c->fx) add(*e); if (t->inst) for (auto* e : t->inst->internalEffects()) add(*e); }
    if (i == 0) return;
    auto& prev = p.stations[(size_t) i - 1];
    int n = 0;
    for (auto& kv : s.snap) {
        auto it = prev.snap.find(kv.first);
        float before = it != prev.snap.end() ? it->second : kv.second;
        if (std::abs(before - kv.second) < 1e-4f) continue;
        if (kv.first.find("/@on") != std::string::npos) {
            Uid u = (Uid) std::stoul(kv.first.substr(0, kv.first.find('/')));
            changeList.add((fxName.count(u) ? fxName[u] : juce::String("an effect")) + (kv.second > 0.5f ? U("  switches ON") : U("  switches OFF")));
        } else if (auto mt = m.find(kv.first); mt != m.end()) {
            changeList.add(p.describe(kv.first) + U("   ") + juce::String(mt->second->format(before)) + U("  →  ") + juce::String(mt->second->format(kv.second)));
        }
        if (++n > 60) { changeList.add(U("…")); break; }
    }
    if (n == 0) changeList.add(U("the same sound as station ") + juce::String(i) + U(" (turn knobs now to make this station different)"));
}

void StationView::update() { if (line.shown >= (int) P().stations.size()) line.shown = juce::jmax(0, (int) P().stations.size() - 1); showStation(line.shown); }

void StationView::resized() {
    auto r = getLocalBounds().reduced(14, 10);
    big = r.removeFromTop(128);
    auto controls = big.withTrimmedLeft(big.getWidth() / 2 + 10);
    controls = controls.withTrimmedTop(10);
    auto row1 = controls.removeFromTop(74);
    goBtn.setBounds(row1.removeFromRight(150)); row1.removeFromRight(8); backBtn.setBounds(row1.removeFromRight(70).withTrimmedTop(20).withTrimmedBottom(20));
    row1.removeFromRight(10);
    auto stack = row1.removeFromRight(160);
    startBtn.setBounds(stack.removeFromTop(32)); stack.removeFromTop(8); modeBtn.setBounds(stack.removeFromTop(32)); stack.removeFromTop(6);
    controls.removeFromTop(8);
    auto row2 = controls.removeFromTop(26);
    followBtn.setBounds(row2.removeFromRight(150)); row2.removeFromRight(6); autoBtn.setBounds(row2.removeFromRight(90)); row2.removeFromRight(6); storeBtn.setBounds(row2.removeFromRight(70));
    r.removeFromTop(8);
    auto lineArea = r.removeFromTop(juce::jlimit(130, 150, r.getHeight() / 3));
    auto lineTop = lineArea.removeFromTop(24);
    addHereBtn.setBounds(lineTop.removeFromRight(110).withHeight(22)); lineTop.removeFromRight(6); addBtn.setBounds(lineTop.removeFromRight(90).withHeight(22));
    line.setBounds(lineArea);
    r.removeFromTop(10);
    settings = r.removeFromLeft(juce::jmin(470, r.getWidth() / 2)); r.removeFromLeft(10);
    logArea = r.removeFromRight(juce::jmin(260, r.getWidth() / 3)); r.removeFromRight(10);
    changes = r;
    auto s = settings.reduced(14, 10); s.removeFromTop(22);
    juce::Component* F[8] = { &name, &note, barBox.get(), glideBox.get(), &curveBtn, &endBtn, autoGoBox.get(), &jumpBtn };
    int colW = (s.getWidth() - 10) / 2;
    for (int i = 0; i < 8; ++i) {
        int row = i / 2, col = i % 2;
        auto cell = juce::Rectangle<int>(s.getX() + col * (colW + 10), s.getY() + row * 40, colW, 38);
        labRect[i] = cell.removeFromTop(14); F[i]->setBounds(cell.withHeight(22));
    }
    s.removeFromTop(4 * 40 + 4);
    auto b1 = s.removeFromTop(24); int w = (b1.getWidth() - 8) / 3;
    recHere.setBounds(b1.removeFromLeft(w + 40)); b1.removeFromLeft(4); playHere.setBounds(b1.removeFromLeft(w - 20)); b1.removeFromLeft(4); loopHere.setBounds(b1);
    s.removeFromTop(6);
    auto b2 = s.removeFromTop(24);
    sameBtn.setBounds(b2.removeFromLeft(w + 60)); b2.removeFromLeft(4); dupBtn.setBounds(b2.removeFromLeft(w - 30)); b2.removeFromLeft(4); delBtn.setBounds(b2);
    bool any = st() != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &name, &note, barBox.get(), glideBox.get(), autoGoBox.get(), &curveBtn, &endBtn, &jumpBtn, &recHere, &playHere, &loopHere, &dupBtn, &delBtn, &sameBtn })
        c->setVisible(any && settings.getHeight() > 100);
}

void StationView::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    auto& p = P(); auto& sp = host().stations; auto& v = p.stations;
    // the big station
    auto left = big.withWidth(big.getWidth() / 2);
    cardBg(g, big.toFloat(), 14);
    int cur = sp.current;
    bool has = cur >= 0 && cur < (int) v.size();
    label(g, has ? "STATION " + juce::String(cur + 1) + "  /  " + juce::String((int) v.size()) : juce::String("NO STATION YET"), left.reduced(20, 0).withY(big.getY() + 14).withHeight(16), 9.5f, 2, Theme::gold());
    g.setColour(Theme::txt()); g.setFont(Theme::font(26, 2));
    g.drawFittedText(has ? (v[(size_t) cur].name.isEmpty() ? juce::String("Station " + juce::String(cur + 1)) : v[(size_t) cur].name) : juce::String("+ STATION makes the first one"),
                     left.reduced(20, 0).withY(big.getY() + 34).withHeight(40), juce::Justification::centredLeft, 1, 0.6f);
    if (has && v[(size_t) cur].note.isNotEmpty()) label(g, v[(size_t) cur].note, left.reduced(20, 0).withY(big.getY() + 76).withHeight(16), 10.0f, 1, Theme::sub());
    if (has && cur + 1 < (int) v.size() && v[(size_t) cur].note.isEmpty()) label(g, U("next:  ") + juce::String(cur + 2) + "  " + v[(size_t) cur + 1].name, left.reduced(20, 0).withY(big.getY() + 76).withHeight(14), 9.0f, 1);
    // glide bar and times
    auto gb = juce::Rectangle<float>((float) left.getX() + 20, (float) big.getBottom() - 30, (float) left.getWidth() - 40, 6);
    g.setColour(Theme::card2()); g.fillRoundedRectangle(gb, 3);
    if (has) { g.setColour(Theme::gold()); g.fillRoundedRectangle(gb.withWidth(gb.getWidth() * (float) sp.glideProgress()), 3); }
    auto tm = [](double s) { return juce::String((int) s / 60) + ":" + juce::String(std::fmod(s, 60.0), 1).paddedLeft('0', 4); };
    label(g, sp.gliding() ? U("gliding …") : U("in this station  ") + tm(sp.secondsInStation()), { (int) gb.getX(), (int) gb.getBottom() + 4, 200, 12 }, 8.0f, 1);
    if (sp.pieceSeconds() > 0) label(g, U("piece  ") + tm(sp.pieceSeconds()), { (int) gb.getRight() - 140, (int) gb.getBottom() + 4, 140, 12 }, 8.0f, 2, Theme::txt(), juce::Justification::centredRight);
    // section titles
    label(g, "SCORE LINE", { line.getX(), line.getY() - 26, 200, 22 }, 9.5f, 2);
    label(g, U("every station keeps the whole sound: all knobs, effects on / off, the synths"), { line.getX() + 100, line.getY() - 26, 500, 22 }, 8.0f, 0);
    // settings card
    cardBg(g, settings.toFloat(), 12);
    auto s = settings.reduced(14, 12);
    label(g, st() ? "STATION " + juce::String(line.shown + 1) : juce::String("STATION"), s.removeFromTop(22), 9.5f, 2, Theme::gold());
    const char* L[8] = { "NAME", "NOTE", "STARTS AT", "GLIDE", "GLIDE CURVE", "AT ITS END", "AUTO GO", "GO JUMPS" };
    if (st()) for (int i = 0; i < 8; ++i) label(g, L[i], labRect[i], 7.5f, 2);
    else label(g, U("no stations yet: + STATION (or double-click the arranger's ruler)"), s.withHeight(40), 9.5f, 1);
    // changes
    cardBg(g, changes.toFloat(), 12);
    auto c = changes.reduced(14, 12);
    label(g, "WHAT CHANGES HERE", c.removeFromTop(22), 9.5f, 2);
    for (auto& line_ : changeList) { if (c.getHeight() < 14) break; label(g, line_, c.removeFromTop(16), 8.5f, line_.contains(U("→")) ? 1 : 0, Theme::txt()); }
    // log
    cardBg(g, logArea.toFloat(), 12);
    auto l = logArea.reduced(14, 12);
    label(g, "PERFORMANCE LOG", l.removeFromTop(22), 9.5f, 2);
    if (sp.log.isEmpty()) label(g, U("START THE PIECE: every GO is written here with its time"), l.withHeight(32), 8.5f, 0);
    for (int i = juce::jmax(0, sp.log.size() - l.getHeight() / 15); i < sp.log.size(); ++i) label(g, sp.log[i], l.removeFromTop(15), 8.5f, 1, Theme::txt());
    // pedals
    auto& ped = host().pedal[Host::ActGo];
    label(g, U("GO pedal: ") + Host::pedalText(ped) + U("   ·   set it in Settings"), { logArea.getX() + 14, logArea.getBottom() - 22, logArea.getWidth() - 28, 14 }, 7.5f, 0);
}

void StationView::timerCallback() {
    auto& sp = host().stations;
    int cnt = (int) P().stations.size();
    if (cnt != seenCount || host().structure != seenStructure) { seenCount = cnt; seenStructure = host().structure; update(); }
    if (sp.current != seenCurrent) { seenCurrent = sp.current; if (sp.current >= 0) showStation(sp.current); }
    if (isShowing()) { line.repaint(); repaint(big); repaint(logArea); }
    static int k = 0; if (++k % 15 == 0 && isShowing()) { computeChanges(); repaint(changes); for (auto* c : getChildren()) c->repaint(); }
}

} // namespace score
