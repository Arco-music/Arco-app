#include "ArrangeView.h"

namespace score {

static Project& P() { return *host().project; }

// a small horizontal fader for a dB parameter
class MiniFader : public juce::Component {
public:
    MiniFader(Param* p, ColourFn c) : param(p), col(std::move(c)) { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }
    Param* param; ColourFn col;
    static float posOf(float db) { return juce::jlimit(0.0f, 1.0f, db <= -60 ? 0.0f : std::pow((db + 60.0f) / 66.0f, 2.2f)); }
    static float dbOf(float t) { return t <= 0.0001f ? -60.0f : std::pow(t, 1.0f / 2.2f) * 66.0f - 60.0f; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0, 4);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(r, 3);
        float t = posOf(param->get());
        g.setColour((col ? col() : Theme::txt()).withAlpha(0.6f)); g.fillRoundedRectangle(r.withWidth(r.getWidth() * t), 3);
        float z = r.getX() + r.getWidth() * posOf(0); g.setColour(Theme::txt().withAlpha(0.5f)); g.drawLine(z, r.getY(), z, r.getBottom(), 1);
        g.setColour(Theme::txt()); g.setFont(Theme::font(7.5f, 1));
        g.drawText(param->get() <= -59.9f ? juce::String("-inf") : juce::String(param->get(), 1) + " dB", getLocalBounds().reduced(4, 0), juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override { host().knobTouched(); st = posOf(param->get()); sx = e.position.x; }
    void mouseDrag(const juce::MouseEvent& e) override { param->set(dbOf(juce::jlimit(0.0f, 1.0f, st + (e.position.x - sx) / (getWidth() * (e.mods.isShiftDown() ? 4.0f : 1.0f))))); repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { host().knobTouched(); param->set(0); repaint(); }
private:
    float st = 0, sx = 0;
};

// ======================================================================== track header
TrackHeader::TrackHeader(ArrangeView& a, Track* t) : track(t), av(a) {
    for (auto* p : { &mute, &solo, &arm, &mon, &autoBtn }) { addAndMakeVisible(*p); p->fsize = 8.5f; p->radius = 4.0f; }
    mute.lit = [this] { return track->mix->find("mute")->on(); }; mute.litColour = [] { return Theme::amber(); };
    mute.onClick = [this] { host().knobTouched(); auto* m = track->mix->find("mute"); m->set(m->on() ? 0.0f : 1.0f); host().sendChangeMessage(); repaint(); };
    mute.setTooltip("mute");
    solo.lit = [this] { return track->solo; }; solo.litColour = [] { return Theme::blue(); };
    solo.onClick = [this] { host().edit("solo", [this] { track->solo = !track->solo; }); };
    solo.setTooltip("solo (only the soloed tracks play)");
    arm.lit = [this] { return track->arm; }; arm.litColour = [] { return Theme::red(); };
    arm.onClick = [this] { track->arm = !track->arm; host().rebuild(); host().sendChangeMessage(); };
    arm.setTooltip("arm for recording");
    mon.lit = [this] { return track->monitor == 2 || (track->monitor == 1 && track->arm); }; mon.litColour = [] { return Theme::green(); };
    mon.dynText = [this] { return track->monitor == 0 ? juce::String("OFF") : track->monitor == 1 ? juce::String("AUTO") : juce::String("IN"); };
    mon.onClick = [this] { track->monitor = (track->monitor + 1) % 3; host().rebuild(); host().sendChangeMessage(); };
    mon.setTooltip("hear the input: OFF / AUTO (when armed) / always IN");
    autoBtn.lit = [this] { return track->showAuto; }; autoBtn.litColour = [] { return Theme::violet(); };
    autoBtn.onClick = [this] { track->showAuto = !track->showAuto; av.refresh(); };
    autoBtn.setTooltip("automation lane");
    if (track->isInstrument()) {
        addAndMakeVisible(inst); inst.fsize = 8.5f; inst.radius = 4.0f; inst.text = juce::String(track->inst->type == "nova" ? "NOVA" : track->inst->type == "smp" ? "SAMPLER" : "DRUMS");
        inst.bg = [this] { return Theme::argb(track->colour).withAlpha(0.35f); };
        inst.onClick = [this] { if (av.onOpenInstrument) av.onOpenInstrument(track->id); };
        inst.setTooltip("open the instrument");
        mon.setVisible(false);
    }
    if (track->kind == TrackKind::Audio || track->isInstrument()) {
        meter = std::make_unique<Meter>([this] { auto rt = host().engine.runtime(track->id); return std::max(rt->peakL.load(), rt->peakR.load()); }, false);
        addAndMakeVisible(*meter);
    }
    auto* f = new MiniFader(track->mix->find("vol"), [this] { return Theme::argb(track->colour); });
    addAndMakeVisible(f); f->setComponentID("fader"); fader.reset(f);
    onLong = [this](juce::Point<int>) { menu(); };
}

void TrackHeader::paint(juce::Graphics& g) {
    bool sel = host().selTrack == track->id;
    auto r = getLocalBounds().toFloat();
    g.setColour(sel ? Theme::card2() : Theme::card()); g.fillRect(r);
    g.setColour(Theme::argb(track->colour)); g.fillRect(r.withWidth(5.0f));
    g.setColour(Theme::line().withAlpha(0.6f)); g.drawLine(0, r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f);
    g.setColour(Theme::txt()); g.setFont(Theme::font(10.5f, 2));
    int nameW = track->isInstrument() ? getWidth() - 86 : getWidth() - 24;
    g.drawFittedText(track->name, 14, 6, nameW, 16, juce::Justification::centredLeft, 1, 0.8f);
    if (track->arm && host().recording()) { g.setColour(Theme::red()); g.fillEllipse((float) getWidth() - 14, 10, 7, 7); }
    if (track->kind == TrackKind::Audio) {
        juce::String in = track->inType == InputType::None ? "no input" : track->inType == InputType::Mono ? "in " + juce::String(track->inCh + 1)
                        : track->inType == InputType::Stereo ? "in " + juce::String(track->inCh + 1) + "+" + juce::String(track->inCh + 2) : "from a track";
        if (track->recMode == 1) in << U(" · with fx");
        label(g, in, { 14, getHeight() > 70 ? getHeight() - 18 : 22, getWidth() - 20, 12 }, 7.5f, 0);
    }
    if (track->isInstrument() && track->follow) label(g, U("follows the input"), { 14, getHeight() - 18, getWidth() - 20, 12 }, 7.5f, 0, Theme::green());
    // resize grip
    g.setColour(Theme::line()); g.fillRoundedRectangle((float) getWidth() / 2 - 10, (float) getHeight() - 4, 20, 2, 1);
}

void TrackHeader::resized() {
    int y = 26, x = 14;
    for (auto* p : { &mute, &solo, &arm, &mon, &autoBtn }) {
        if (!p->isVisible()) continue;
        int w = p == &mon ? 34 : 20; p->setBounds(x, y, w, 17); x += w + 3;
    }
    if (track->isInstrument()) inst.setBounds(getWidth() - 70, 5, 62, 17);
    bool roomy = getHeight() >= 62;
    if (fader) { fader->setBounds(x + 4, y, getWidth() - x - 12, 17); fader->setVisible(true); }
    if (meter) { meter->setBounds(14, y + 21, getWidth() - 24, 4); meter->setVisible(roomy); }
}

void TrackHeader::rename() {
    auto* ed = new juce::TextEditor(); styleEditor(*ed, 10.5f); ed->setText(track->name); ed->setBounds(10, 4, getWidth() - 90, 20); ed->selectAll();
    ed->onReturnKey = [this, ed] { auto t = ed->getText().trim(); if (t.isNotEmpty()) host().edit("rename track", [this, t] { track->name = t; }, false);
        juce::MessageManager::callAsync([ed] { delete ed; }); };
    ed->onFocusLost = ed->onEscapeKey = [ed] { juce::MessageManager::callAsync([ed] { delete ed; }); };
    addAndMakeVisible(ed); ed->grabKeyboardFocus();
}

void TrackHeader::mouseDown(const juce::MouseEvent& e) {
    host().selectTrack(track->id);
    if (e.mods.isPopupMenu()) { menu(); return; }
    lpDown(e);
    if (e.y > getHeight() - 7) { resizing = true; startH = track->height; startY = e.getScreenY(); }
}
void TrackHeader::mouseDrag(const juce::MouseEvent& e) {
    lpDrag(e);
    if (resizing) { track->height = juce::jlimit(36.0f, 260.0f, startH + (float) (e.getScreenY() - startY)); av.refresh(); }
    else if (!fired && e.getDistanceFromDragStart() > 10) {
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!dc->isDragAndDropActive())
            dc->startDragging("track:" + juce::String((int) track->id), this);
    }
}
void TrackHeader::mouseUp(const juce::MouseEvent&) { lpUp(); resizing = false; }
void TrackHeader::mouseDoubleClick(const juce::MouseEvent& e) { if (e.y < 24) rename(); }

void TrackHeader::menu() {
    juce::PopupMenu m, in, col, rec, mon2;
    auto& h = host(); Track* t = track;
    m.addItem(1, "Rename");
    for (int i = 0; i < 12; ++i) col.addItem(100 + i, juce::String("Colour ") + juce::String(i + 1), true, t->colour == Project::colourFor(i));
    m.addSubMenu("Colour", col);
    if (t->kind == TrackKind::Audio || t->isInstrument()) {
        int n = juce::jmax(2, h.numInputs()); auto names = h.inputNames();
        in.addItem(200, "None", true, t->inType == InputType::None);
        for (int k = 0; k < n; ++k) in.addItem(210 + k, "Input " + juce::String(k + 1) + (k < names.size() ? "  (" + names[k] + ")" : juce::String()), true, t->inType == InputType::Mono && t->inCh == k);
        for (int k = 0; k + 1 < n; k += 2) in.addItem(260 + k, "Stereo " + juce::String(k + 1) + "+" + juce::String(k + 2), true, t->inType == InputType::Stereo && t->inCh == k);
        in.addSeparator();
        for (auto& o : P().tracks) if (o->id != t->id) in.addItem(300 + (int) (&o - &P().tracks[0]), "Record the sound of: " + o->name, true, t->inType == InputType::Track && t->inTrack == o->id);
        m.addSubMenu(t->isInstrument() ? "Input (for FOLLOW)" : "Input", in);
    }
    if (t->kind == TrackKind::Audio) {
        rec.addItem(400, "Record the dry sound (effects stay changeable)", true, t->recMode == 0);
        rec.addItem(401, "Record with this track's effects", true, t->recMode == 1);
        m.addSubMenu("Recording", rec);
    }
    if (t->isInstrument()) {
        m.addItem(410, "FOLLOW: play the notes of the input (sing or bow, it plays along)", true, t->follow);
        juce::PopupMenu fo; for (int o = -2; o <= 2; ++o) fo.addItem(420 + o + 2, (o > 0 ? "+" : "") + juce::String(o) + " octave", true, t->followOct == o);
        m.addSubMenu("FOLLOW octave", fo);
    }
    m.addSeparator();
    m.addItem(500, "Duplicate track");
    m.addItem(501, "Bounce to an audio track (with its effects)");
    m.addItem(502, "Delete track");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
        auto& h = host(); Track* t = track; Uid id = t->id;
        if (r == 1) rename();
        else if (r >= 100 && r < 112) h.edit("colour", [t, r] { t->colour = Project::colourFor(r - 100); }, false);
        else if (r == 200) h.edit("input", [t] { t->inType = InputType::None; });
        else if (r >= 210 && r < 260) h.edit("input", [t, r] { t->inType = InputType::Mono; t->inCh = r - 210; });
        else if (r >= 260 && r < 300) h.edit("input", [t, r] { t->inType = InputType::Stereo; t->inCh = r - 260; });
        else if (r >= 300 && r < 400) { auto src = P().tracks[(size_t) (r - 300)]->id; h.edit("input", [t, src] { t->inType = InputType::Track; t->inTrack = src; }); }
        else if (r == 400 || r == 401) h.edit("recording", [t, r] { t->recMode = r - 400; });
        else if (r == 410) h.edit("follow", [t] { t->follow = !t->follow; if (t->follow && t->inType == InputType::None) { t->inType = InputType::Mono; t->inCh = 0; } });
        else if (r >= 420 && r < 425) h.edit("follow octave", [t, r] { t->followOct = r - 422; });
        else if (r == 500) h.edit("duplicate track", [id] {
            auto& p = P(); auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](auto& x) { return x->id == id; }); if (it == p.tracks.end()) return;
            auto v = juce::JSON::toString(p.toVar());          // a full copy with new ids: through the file format
            Project tmp; tmp.fromVar(juce::JSON::parse(v), nullptr, [](const juce::String& f) { return host().loadSource(f); });
            for (auto& tt : tmp.tracks) if (tt->id == id) {
                tt->id = newUid(); tt->name << " copy"; tt->mix->uid = newUid(); for (auto& e : tt->fx) e->uid = newUid();
                if (tt->inst) { tt->inst->uid = newUid(); for (auto* e : tt->inst->internalEffects()) e->uid = newUid(); }
                for (auto& c : tt->clips) { c->id = newUid(); c->rt = std::make_shared<ClipRT>(); for (auto& e : c->fx) e->uid = newUid(); }
                tt->autos.clear(); tt->arm = false;
                p.tracks.insert(it + 1, tt); break; } });
        else if (r == 501) h.bounceTrack(id);
        else if (r == 502) {
            auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Delete \"" + t->name + "\"?")
                            .withMessage("The track and its clips go (UNDO brings them back).").withButton("Delete").withButton("Cancel");
            juce::AlertWindow::showAsync(opts, [id](int b) { if (b == 1) host().edit("delete track", [id] {
                auto& p = P(); p.tracks.erase(std::remove_if(p.tracks.begin(), p.tracks.end(), [&](auto& x) { return x->id == id; }), p.tracks.end()); }); });
        }
    });
}

// ======================================================================== ruler (bars, loop, stations)
int Ruler::stationAt(juce::Point<int> p) const {
    if (p.y < 22) return -1;
    auto& st = P().stations;
    for (int i = (int) st.size() - 1; i >= 0; --i) { float x = av.xOf(st[(size_t) i].beat); if (p.x >= x - 2 && p.x <= x + 96) return i; }
    return -1;
}

void Ruler::paint(juce::Graphics& g) {
    auto& p = P(); auto r = getLocalBounds();
    g.setColour(Theme::panel()); g.fillRect(r);
    // loop band
    if (true) {
        float a = av.xOf(p.loopStart), b = av.xOf(p.loopEnd);
        g.setColour((p.loopOn ? Theme::blue() : Theme::sub()).withAlpha(p.loopOn ? 0.45f : 0.18f));
        g.fillRoundedRectangle(a, 2.0f, b - a, 17.0f, 3.0f);
    }
    // bars
    double bpb = p.beatsPerBar; double step = av.ppb * bpb < 30 ? bpb * 4 : (av.ppb * bpb < 60 ? bpb * 2 : bpb);
    double b0 = std::floor(av.scrollBeat / step) * step;
    for (double b = b0; av.xOf(b) < getWidth(); b += step) {
        float x = av.xOf(b);
        g.setColour(Theme::line()); g.drawLine(x, 12, x, 22, 1);
        label(g, juce::String((int) std::lround(b / bpb) + 1), { (int) x + 3, 2, 40, 14 }, 8.5f, 2, Theme::txt());
        if (av.ppb >= 14) for (int k = 1; k < (int) bpb && step == bpb; ++k) { float xx = av.xOf(b + k); g.setColour(Theme::line().withAlpha(0.6f)); g.drawLine(xx, 17, xx, 22, 1); }
    }
    // stations
    g.setColour(Theme::line().withAlpha(0.5f)); g.drawLine(0, 22.5f, (float) getWidth(), 22.5f);
    auto& st = p.stations; int cur = host().stations.current;
    for (int i = 0; i < (int) st.size(); ++i) {
        float x = av.xOf(st[(size_t) i].beat); if (x > getWidth()) break;
        float next = i + 1 < (int) st.size() ? av.xOf(st[(size_t) i + 1].beat) : x + 120;
        bool now = i == cur;
        auto fr = juce::Rectangle<float>(x, 25, juce::jmax(18.0f, juce::jmin(next - x - 3, 140.0f)), 17);
        g.setColour(now ? Theme::gold() : Theme::card2()); g.fillRoundedRectangle(fr, 4);
        g.setColour(now ? Theme::bg() : Theme::txt()); g.setFont(Theme::font(8.5f, 2));
        g.drawFittedText(juce::String(i + 1) + (st[(size_t) i].name.isNotEmpty() ? "  " + st[(size_t) i].name : juce::String()), fr.toNearestInt().reduced(5, 0), juce::Justification::centredLeft, 1, 0.8f);
        if (st[(size_t) i].atEnd == 1) { g.setColour(now ? Theme::bg() : Theme::blue()); g.fillEllipse(fr.getRight() - 9, fr.getCentreY() - 2.5f, 5, 5); }
    }
    if (st.empty()) label(g, U("double-click here to add a station (a moment of the score line)"), { 8, 25, 500, 17 }, 8.0f, 0);
    // playhead
    float px = av.xOf(host().engine.position());
    g.setColour(host().recording() ? Theme::red() : Theme::green());
    juce::Path tri; tri.addTriangle(px - 6, 0, px + 6, 0, px, 9); g.fillPath(tri); g.fillRect(px - 0.5f, 0.0f, 1.0f, (float) getHeight());
}

void Ruler::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { stationMenu(e.getPosition()); return; }
    lpDown(e); moved = false;
    int s = stationAt(e.getPosition());
    if (s >= 0) { dragStation = s; mode = 3; host().beginGesture(); return; }
    if (e.y < 22) {
        if (e.mods.isShiftDown() || e.mods.isAltDown() || e.y < 11) { mode = 2; a0 = av.snap(av.beatAt((float) e.x), true); }     // drag on the top half: the loop
        else { mode = 1; host().locate(std::max(0.0, av.snap(av.beatAt((float) e.x)))); }
    }
}
void Ruler::mouseDrag(const juce::MouseEvent& e) {
    lpDrag(e);
    if (fired) return;
    if (e.getDistanceFromDragStart() > 3) moved = true;
    if (mode == 1) host().locate(std::max(0.0, av.snap(av.beatAt((float) e.x))));
    else if (mode == 2 && moved) { double b = av.snap(av.beatAt((float) e.x), true); host().project->loopOn = true; host().setLoopRange(std::min(a0, b), std::max(a0, b)); }
    else if (mode == 3 && dragStation >= 0 && moved) {
        auto& st = P().stations[(size_t) dragStation];
        st.beat = std::max(0.0, av.snap(av.beatAt((float) e.x - 4))); repaint(); av.lanes.repaint();
    }
}
void Ruler::mouseUp(const juce::MouseEvent&) {
    lpUp();
    if (mode == 3) {
        if (moved) { P().sortStations(); host().endGesture("move station"); }
        else { host().endGesture("", false); auto& sp = host().stations; if (sp.perform) sp.goTo(dragStation); else sp.select(dragStation); }
    }
    if (mode == 2 && !moved) host().toggleLoop();
    mode = 0; dragStation = -1;
}
void Ruler::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.y >= 22 && stationAt(e.getPosition()) < 0) {
        double b = std::max(0.0, std::floor(av.beatAt((float) e.x) / P().beatsPerBar) * P().beatsPerBar);
        host().edit("new station", [b] {
            auto& p = P(); Station s; s.id = newUid(); s.beat = b; s.name = "Station " + juce::String((int) p.stations.size() + 1);
            s.snap = p.captureKnobs(); s.hasSnap = true; p.stations.push_back(s); p.sortStations(); }, false);
    }
}
void Ruler::stationMenu(juce::Point<int> pt) {
    int s = stationAt(pt); if (s < 0) return;
    juce::PopupMenu m;
    m.addItem(1, "Go there"); m.addItem(2, "Store the sound now in this station"); m.addItem(3, "Move it to the playhead"); m.addSeparator(); m.addItem(4, "Delete station");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [s](int r) {
        auto& h = host();
        if (r == 1) h.stations.select(s);
        if (r == 2) { h.stations.current = s; h.stations.store(); }
        if (r == 3) h.edit("move station", [s] { P().stations[(size_t) s].beat = std::floor(host().engine.position() * 4) / 4; P().sortStations(); }, false);
        if (r == 4) h.edit("delete station", [s] { auto& st = P().stations; st.erase(st.begin() + s); if (host().stations.current >= (int) st.size()) host().stations.current = (int) st.size() - 1; }, false);
    });
}

// ======================================================================== lanes
Lanes::Hit Lanes::hitTest(juce::Point<int> p) {
    Hit h;
    int yy = p.y + av.scrollY;
    Row* row = av.rowAt(yy); if (!row || yy > row->y + row->h) return h;
    double beat = av.beatAt((float) p.x);
    for (int i = (int) row->t->clips.size() - 1; i >= 0; --i) {
        auto& c = *row->t->clips[(size_t) i];
        if (beat < c.start || beat >= c.end()) continue;
        h.c = &c; h.t = row->t;
        float x0 = av.xOf(c.start), x1 = av.xOf(c.end()); int top = row->y - av.scrollY + 2;
        int edge = juce::jmin(8, (int) ((x1 - x0) / 4));
        if (!c.midi && p.y < top + 12 && p.x < x0 + 12 + (float) c.fadeIn * av.ppb * P().bpm / 60.0) { if (p.x <= x0 + std::max(10.0, c.fadeIn * av.ppb * P().bpm / 60.0) + 4 && p.x >= x0 + c.fadeIn * av.ppb * P().bpm / 60.0 - 6) { h.part = FadeIn; return h; } }
        if (!c.midi && p.y < top + 12) { float fo = (float) (c.fadeOut * av.ppb * P().bpm / 60.0); if (p.x >= x1 - fo - 10 && p.x <= x1 - fo + 6) { h.part = FadeOut; return h; } }
        if (p.x < x0 + edge) h.part = TrimL; else if (p.x > x1 - edge) h.part = TrimR; else h.part = Move;
        return h;
    }
    return h;
}

void Lanes::mouseMove(const juce::MouseEvent& e) {
    auto h = hitTest(e.getPosition());
    setMouseCursor(h.part == TrimL || h.part == TrimR ? juce::MouseCursor::LeftRightResizeCursor
                   : h.part == FadeIn || h.part == FadeOut ? juce::MouseCursor::CrosshairCursor
                   : h.part == Move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void Lanes::drawClip(juce::Graphics& g, Clip& c, Track& t, juce::Rectangle<int> r, bool selected) {
    auto col = Theme::argb(c.colour ? c.colour : t.colour);
    auto rf = r.toFloat();
    g.setColour(col.withAlpha(c.muted ? 0.08f : (Theme::lightMode() ? 0.35f : 0.22f))); g.fillRoundedRectangle(rf, 5);
    auto head = rf.withHeight(13.0f);
    g.setColour(col.withAlpha(c.muted ? 0.3f : 0.85f)); g.fillRoundedRectangle(head, 5); g.fillRect(head.withTrimmedTop(6));
    g.setColour(Theme::lightMode() ? juce::Colours::white : Theme::bg()); g.setFont(Theme::font(8.0f, 2));
    juce::String nm = c.name; if (!c.fx.empty()) nm = "FX  " + nm; if (c.reverse) nm = U("◀ ") + nm;
    g.drawFittedText(nm, r.withHeight(13).reduced(5, 0), juce::Justification::centredLeft, 1, 0.8f);
    auto body = rf.withTrimmedTop(14).reduced(1, 2);
    g.saveState(); g.reduceClipRegion(r);
    if (!c.midi) {
        if (c.src && !c.src->peaks.empty()) {
            const auto& pk = c.src->peaks; double sr = c.src->sr; float gain = dbToGain(c.gainDb);
            double secPerPx = 60.0 / P().bpm / av.ppb;
            float cy = body.getCentreY(), hh = body.getHeight() * 0.5f;
            juce::Path wave; bool started = false;
            int x0 = juce::jmax((int) body.getX(), 0), x1 = juce::jmin((int) body.getRight(), getWidth());
            std::vector<float> amp((size_t) juce::jmax(0, x1 - x0));
            for (int x = x0; x < x1; ++x) {
                double tsec = (av.beatAt((float) x) - c.start) * 60.0 / P().bpm;
                if (tsec < 0 || tsec > c.srcLen) continue;
                double s0 = c.reverse ? c.offset + c.srcLen - tsec - secPerPx : c.offset + tsec;
                int i0 = (int) (s0 * sr) >> 6, i1 = (int) ((s0 + secPerPx) * sr) >> 6;
                float m = 0; for (int i = juce::jmax(0, i0); i <= juce::jmin((int) pk.size() - 1, i1); ++i) m = juce::jmax(m, pk[(size_t) i]);
                // the fades
                double fin = c.fadeIn > 0 ? juce::jmin(1.0, tsec / c.fadeIn) : 1.0, fo = c.fadeOut > 0 ? juce::jmin(1.0, (c.srcLen - tsec) / c.fadeOut) : 1.0;
                amp[(size_t) (x - x0)] = juce::jmin(1.0f, m * gain * (float) (fin * fo));
            }
            for (int x = x0; x < x1; ++x) { float a = amp[(size_t) (x - x0)] * hh; if (!started) { wave.startNewSubPath((float) x, cy - a); started = true; } else wave.lineTo((float) x, cy - a); }
            for (int x = x1 - 1; x >= x0; --x) wave.lineTo((float) x, cy + amp[(size_t) (x - x0)] * hh);
            wave.closeSubPath();
            g.setColour(col.withAlpha(c.muted ? 0.3f : 0.95f)); g.fillPath(wave);
        } else if (!c.src) { label(g, U("sound file missing"), body.toNearestInt().reduced(6, 0), 8.0f, 1, Theme::red()); }
        // fade lines
        float fi = (float) (c.fadeIn * av.ppb * P().bpm / 60.0), fo = (float) (c.fadeOut * av.ppb * P().bpm / 60.0);
        g.setColour(Theme::txt().withAlpha(0.7f));
        if (fi > 1) g.drawLine(rf.getX(), rf.getBottom(), rf.getX() + fi, rf.getY() + 13, 1.2f);
        if (fo > 1) g.drawLine(rf.getRight() - fo, rf.getY() + 13, rf.getRight(), rf.getBottom(), 1.2f);
        if (selected || isMouseOver()) { g.setColour(Theme::txt()); g.fillEllipse(rf.getX() + fi - 3, rf.getY() + 3, 7, 7); g.fillEllipse(rf.getRight() - fo - 4, rf.getY() + 3, 7, 7); }
    } else {
        int lo = 127, hi = 0; for (auto& n : c.notes) { lo = juce::jmin(lo, n.pitch); hi = juce::jmax(hi, n.pitch); }
        if (lo <= hi) {
            int span = juce::jmax(12, hi - lo + 1); float nh = juce::jmax(1.5f, body.getHeight() / span);
            g.setColour(col.brighter(0.3f).withAlpha(c.muted ? 0.3f : 0.95f));
            for (auto& n : c.notes) {
                if (n.start >= c.length) continue;
                float x = av.xOf(c.start + n.start), w = juce::jmax(2.0f, (float) (juce::jmin(n.len, c.length - n.start) * av.ppb) - 1);
                float y = body.getBottom() - (n.pitch - lo + 1) * (body.getHeight() - nh) / span - nh * 0.5f;
                g.fillRoundedRectangle(x, y, w, nh, juce::jmin(1.5f, nh * 0.4f));
            }
        } else label(g, U("double-click to write notes"), body.toNearestInt().reduced(6, 0), 8.0f, 0);
    }
    g.restoreState();
    if (selected) { g.setColour(Theme::txt()); g.drawRoundedRectangle(rf.reduced(0.5f), 5, 1.6f); }
}

void Lanes::drawAuto(juce::Graphics& g, const Row& row) {
    auto& t = *row.t;
    int y0 = row.y + row.h - av.scrollY, h = row.autoH;
    auto r = juce::Rectangle<int>(0, y0, getWidth(), h);
    g.setColour(Theme::scr().withAlpha(0.6f)); g.fillRect(r);
    AutoLane* lane = nullptr; for (auto& a : t.autos) if (juce::String(a.path) == t.autoShown) lane = &a;
    if (!lane && !t.autos.empty()) lane = &t.autos.front();
    if (!lane) { label(g, U("automation: right-click any knob of this track > Automate this knob"), r.reduced(10, 0), 8.0f, 0); return; }
    label(g, P().describe(lane->path), { 8, y0 + 2, 400, 12 }, 7.5f, 1, Theme::violet());
    auto vy = [&](float v) { return (float) y0 + 14 + (1.0f - v) * (float) (h - 20); };
    juce::Path p;
    if (lane->pts.empty()) { g.setColour(Theme::violet().withAlpha(0.5f)); label(g, U("click to add points"), r.reduced(10, 0).withTrimmedTop(14), 8.0f, 0); return; }
    p.startNewSubPath(0, vy(lane->pts.front().value));
    for (auto& pt : lane->pts) p.lineTo(av.xOf(pt.beat), vy(pt.value));
    p.lineTo((float) getWidth(), vy(lane->pts.back().value));
    g.setColour(Theme::violet()); g.strokePath(p, juce::PathStrokeType(1.6f));
    for (auto& pt : lane->pts) g.fillEllipse(av.xOf(pt.beat) - 3.5f, vy(pt.value) - 3.5f, 7, 7);
}

void Lanes::paint(juce::Graphics& g) {
    auto& p = P();
    g.fillAll(Theme::bg());
    // rows
    for (auto& row : av.rows) {
        int y = row.y - av.scrollY;
        if (y > getHeight() || y + row.h + row.autoH < 0) continue;
        bool sel = host().selTrack == row.t->id;
        g.setColour(sel ? Theme::panel().brighter(0.02f) : Theme::panel()); g.fillRect(0, y, getWidth(), row.h);
        g.setColour(Theme::line().withAlpha(0.5f)); g.drawLine(0, (float) (y + row.h + row.autoH) - 0.5f, (float) getWidth(), (float) (y + row.h + row.autoH) - 0.5f);
    }
    // grid
    double gb = av.gridBeats() > 0 ? av.gridBeats() : 1.0; while (gb * av.ppb < 8) gb *= 2;
    double b0 = std::floor(av.scrollBeat / gb) * gb;
    for (double b = b0; av.xOf(b) < getWidth(); b += gb) {
        float x = av.xOf(b); bool bar = std::fmod(b, (double) p.beatsPerBar) < 1e-6;
        g.setColour(Theme::line().withAlpha(bar ? 0.55f : 0.22f)); g.fillRect(x, 0.0f, 1.0f, (float) getHeight());
    }
    // loop region
    if (p.loopOn) { g.setColour(Theme::blue().withAlpha(0.06f)); g.fillRect(av.xOf(p.loopStart), 0.0f, av.xOf(p.loopEnd) - av.xOf(p.loopStart), (float) getHeight()); }
    // stations
    for (size_t i = 0; i < p.stations.size(); ++i) {
        float x = av.xOf(p.stations[i].beat);
        g.setColour(((int) i == host().stations.current ? Theme::gold() : Theme::gold().withAlpha(0.35f)));
        for (float y = 0; y < getHeight(); y += 8) g.fillRect(x, y, 1.0f, 4.0f);
    }
    // clips
    for (auto& row : av.rows) {
        int y = row.y - av.scrollY;
        if (y > getHeight() || y + row.h < 0) continue;
        for (auto& c : row.t->clips) {
            float x0 = av.xOf(c->start), x1 = av.xOf(c->end());
            if (x1 < 0 || x0 > getWidth()) continue;
            drawClip(g, *c, *row.t, juce::Rectangle<int>((int) x0, y + 2, juce::jmax(3, (int) (x1 - x0)), row.h - 4), host().selClips.count(c->id) > 0);
        }
        if (row.autoH > 0) drawAuto(g, row);
    }
    // a recording that is happening
    if (host().recording() && host().engine.recRolling.load()) {
        double a = host().engine.recStartBeat.load(), b = host().engine.position();
        for (auto& s : host().engine.recSlots()) for (auto& row : av.rows) if (row.t->id == s->track) {
            int y = row.y - av.scrollY + 2;
            auto rr = juce::Rectangle<float>(av.xOf(a), (float) y, av.xOf(b) - av.xOf(a), (float) row.h - 4);
            g.setColour(Theme::red().withAlpha(0.18f)); g.fillRoundedRectangle(rr, 5);
            g.setColour(Theme::red()); g.drawRoundedRectangle(rr, 5, 1.2f);
            if (!s->midi) {
                std::lock_guard<std::mutex> l(s->peakLock);
                double perPx = host().engine.sr * 60.0 / P().bpm / av.ppb / 64.0; float cy = rr.getCentreY(), hh = rr.getHeight() * 0.42f;
                for (int x = 0; x < (int) rr.getWidth(); ++x) { int i0 = (int) (x * perPx), i1 = (int) ((x + 1) * perPx); float m = 0;
                    for (int i = i0; i <= i1 && i < (int) s->peaks.size(); ++i) m = juce::jmax(m, s->peaks[(size_t) i]);
                    g.fillRect(rr.getX() + x, cy - m * hh, 1.0f, juce::jmax(1.0f, 2 * m * hh)); }
            }
        }
    }
    // the rubber band
    if (mode == Band && !band.isEmpty()) { g.setColour(Theme::blue().withAlpha(0.15f)); g.fillRect(band); g.setColour(Theme::blue()); g.drawRect(band, 1); }
    // file drop
    auto dp = av.dropAt - getPosition();
    if (av.dropAt.x >= 0 && getLocalBounds().contains(dp)) { g.setColour(Theme::amber()); g.fillRect((float) dp.x, 0.0f, 2.0f, (float) getHeight());
        label(g, U("drop: a clip here"), { dp.x + 6, dp.y - 8, 140, 16 }, 8.5f, 2, Theme::amber()); }
    // playhead
    float px = av.xOf(host().engine.position());
    g.setColour(host().recording() ? Theme::red() : Theme::green()); g.fillRect(px - 0.5f, 0.0f, 1.0f, (float) getHeight());
    if (av.rows.empty()) label(g, U("+ TRACK below adds a track · or drop sound files here"), getLocalBounds().withHeight(60).reduced(20, 0), 10.0f, 1);
}

void Lanes::mouseDown(const juce::MouseEvent& e) {
    auto& h = host();
    if (e.mods.isPopupMenu()) { contextMenu(e.getPosition()); return; }
    lpDown(e);
    down = e.getPosition(); downBeat = av.beatAt((float) e.x); changed = false; orig.clear(); alt = e.mods.isAltDown();
    int yy = e.y + av.scrollY;
    Row* row = av.rowAt(yy);
    // automation lane
    if (row && row->autoH > 0 && yy > row->y + row->h) {
        auto& t = *row->t; AutoLane* lane = nullptr; for (auto& a : t.autos) if (juce::String(a.path) == t.autoShown) lane = &a;
        if (!lane && !t.autos.empty()) lane = &t.autos.front();
        if (!lane) return;
        int y0 = row->y + row->h - av.scrollY; float v = juce::jlimit(0.0f, 1.0f, 1.0f - (float) (e.y - y0 - 14) / (float) (row->autoH - 20));
        h.beginGesture();
        autoTrack = row->t; autoIdx = -1;
        for (int i = 0; i < (int) lane->pts.size(); ++i) if (std::abs(av.xOf(lane->pts[(size_t) i].beat) - e.x) < 7) autoIdx = i;
        if (autoIdx < 0) { lane->pts.push_back({ av.snap(downBeat), v }); std::sort(lane->pts.begin(), lane->pts.end(), [](auto& a, auto& b) { return a.beat < b.beat; });
            for (int i = 0; i < (int) lane->pts.size(); ++i) if (std::abs(lane->pts[(size_t) i].beat - av.snap(downBeat)) < 1e-9) autoIdx = i; }
        t.autoShown = lane->path;
        mode = AutoPt; changed = true; repaint(); return;
    }
    auto hit = hitTest(down);
    if (hit.c) {
        if (e.mods.isShiftDown() || e.mods.isCommandDown()) { if (h.selClips.count(hit.c->id)) h.selClips.erase(hit.c->id); else h.selClips.insert(hit.c->id); h.selClip = hit.c->id; }
        else if (!h.selClips.count(hit.c->id)) h.selectClip(hit.c->id);
        else h.selClip = hit.c->id;
        h.selectTrack(hit.t->id);
        hitClip = hit.c->id; mode = hit.part;
        h.beginGesture();
        if (mode == Move && alt) {                          // option-drag: a copy
            std::set<Uid> copies;
            for (auto id : h.selClips) { Track* t = nullptr; Clip* c = P().clip(id, &t); if (!c) continue;
                auto d = std::make_shared<Clip>(*c); d->id = newUid(); d->rt = std::make_shared<ClipRT>(); d->fx.clear();
                for (auto& fx : c->fx) { auto x = makeEffect(fx->type); for (size_t i = 0; i < fx->params.size(); ++i) x->params[i]->set(fx->params[i]->get()); d->fx.push_back(std::shared_ptr<Effect>(std::move(x))); }
                t->clips.push_back(d); copies.insert(d->id); if (id == hitClip) hitClip = d->id; }
            h.selClips = copies; h.selClip = hitClip; changed = true;
        }
        for (auto id : h.selClips) { Track* t = nullptr; Clip* c = P().clip(id, &t); if (c) orig.push_back({ id, t->id, c->start, c->length, c->offset, c->srcLen, c->fadeIn, c->fadeOut, c->notes }); }
    } else {
        if (row) h.selectTrack(row->t->id);
        if (!e.mods.isShiftDown()) { h.selClips.clear(); h.selClip = 0; }
        mode = Band; band = {};
    }
    h.sendChangeMessage();
    repaint();
}

void Lanes::mouseDrag(const juce::MouseEvent& e) {
    lpDrag(e);
    if (fired) return;
    auto& h = host(); double spb = 60.0 / P().bpm;
    double raw = av.beatAt((float) e.x) - downBeat;
    bool noSnap = e.mods.isCommandDown();
    if (mode == AutoPt && autoTrack) {
        auto& t = *autoTrack; for (auto& a : t.autos) if (juce::String(a.path) == t.autoShown && autoIdx >= 0 && autoIdx < (int) a.pts.size()) {
            Row* row = nullptr; for (auto& r : av.rows) if (r.t == autoTrack) row = &r;
            if (!row) break;
            int y0 = row->y + row->h - av.scrollY;
            a.pts[(size_t) autoIdx].value = juce::jlimit(0.0f, 1.0f, 1.0f - (float) (e.y - y0 - 14) / (float) (row->autoH - 20));
            double nb = noSnap ? av.beatAt((float) e.x) : av.snap(av.beatAt((float) e.x));
            double lo = autoIdx > 0 ? a.pts[(size_t) autoIdx - 1].beat : 0.0, hi = autoIdx + 1 < (int) a.pts.size() ? a.pts[(size_t) autoIdx + 1].beat : 1e9;
            a.pts[(size_t) autoIdx].beat = juce::jlimit(lo, hi, std::max(0.0, nb));
        }
        h.rebuild(); repaint(); return;
    }
    if (mode == Band) {
        band = juce::Rectangle<int>(down, e.getPosition());
        h.selClips.clear();
        for (auto& row : av.rows) for (auto& c : row.t->clips) {
            auto r = juce::Rectangle<int>((int) av.xOf(c->start), row.y - av.scrollY, (int) (av.xOf(c->end()) - av.xOf(c->start)), row.h);
            if (r.intersects(band)) h.selClips.insert(c->id);
        }
        h.selClip = h.selClips.empty() ? 0 : *h.selClips.begin();
        repaint(); return;
    }
    if (orig.empty() || e.getDistanceFromDragStart() < 3) return;
    changed = true;
    Orig* ho = nullptr; for (auto& o : orig) if (o.id == hitClip) ho = &o;
    if (!ho) return;
    auto snapTo = [&](double b) { return noSnap ? b : av.snap(b); };
    if (mode == Move) {
        double d = snapTo(ho->start + raw) - ho->start;
        for (auto& o : orig) if (o.start + d < 0) d = std::max(d, -o.start);
        for (auto& o : orig) if (Clip* c = P().clip(o.id)) c->start = o.start + d;
        // vertical: to another track of the same kind (when all moved clips are on one track)
        Row* row = av.rowAt(e.y + av.scrollY);
        bool oneTrack = true; for (auto& o : orig) if (o.track != ho->track) oneTrack = false;
        Track* cur = nullptr; P().clip(hitClip, &cur);
        if (row && oneTrack && cur && row->t != cur && row->t->isInstrument() == cur->isInstrument() && (row->t->kind == TrackKind::Audio || row->t->isInstrument())) {
            for (auto& o : orig) {
                auto it = std::find_if(cur->clips.begin(), cur->clips.end(), [&](auto& x) { return x->id == o.id; });
                if (it != cur->clips.end()) { auto cp = *it; cur->clips.erase(it); row->t->clips.push_back(cp); }
                o.track = row->t->id;
            }
            h.selectTrack(row->t->id);
        }
    } else if (mode == TrimL) {
        for (auto& o : orig) if (Clip* c = P().clip(o.id)) {
            if (o.id != hitClip) continue;
            double ns = snapTo(o.start + raw); ns = juce::jlimit(c->midi ? -1e9 : o.start - (c->reverse ? 1e9 : o.offset / spb), o.start + o.length - 0.05, ns);
            double d = ns - o.start;
            if (!c->midi) {
                if (c->reverse) { c->srcLen = juce::jmin(c->src ? c->src->seconds() - o.offset : 1e9, o.srcLen - d * spb); }
                else { c->offset = o.offset + d * spb; c->srcLen = o.srcLen - d * spb; }
                c->start = ns; c->length = c->srcLen / spb;
            } else { c->start = std::max(0.0, ns); d = c->start - o.start; c->length = o.length - d; c->notes = o.notes; for (auto& n : c->notes) n.start -= d; }
        }
    } else if (mode == TrimR) {
        for (auto& o : orig) if (Clip* c = P().clip(o.id)) {
            if (o.id != hitClip) continue;
            double ne = snapTo(o.start + o.length + raw); ne = std::max(o.start + 0.05, ne);
            if (!c->midi) { double maxLen = c->src ? (c->reverse ? c->src->seconds() : c->src->seconds() - c->offset) : o.srcLen;
                c->srcLen = juce::jmin(maxLen, (ne - o.start) * spb); c->length = c->srcLen / spb; }
            else c->length = ne - o.start;
        }
    } else if (mode == FadeIn || mode == FadeOut) {
        if (Clip* c = P().clip(hitClip)) {
            double secs = raw * spb;
            if (mode == FadeIn) c->fadeIn = juce::jlimit(0.0, c->srcLen - c->fadeOut, ho->fadeIn + secs);
            else c->fadeOut = juce::jlimit(0.0, c->srcLen - c->fadeIn, ho->fadeOut - secs);
        }
    }
    h.rebuild();
    repaint();
}

void Lanes::mouseUp(const juce::MouseEvent& e) {
    lpUp();
    auto& h = host();
    if (mode == Move || mode == TrimL || mode == TrimR || mode == FadeIn || mode == FadeOut || mode == AutoPt) {
        h.endGesture(mode == Move ? (alt ? "copy clips" : "move") : mode == AutoPt ? "automation" : mode == FadeIn || mode == FadeOut ? "fade" : "trim", changed);
    }
    if (mode == Band && e.getDistanceFromDragStart() < 3) {
        // a click on empty space: the playhead goes there (on touch screens this is the easiest way)
        if (!fired) h.locate(std::max(0.0, av.snap(av.beatAt((float) e.x))));
    }
    mode = None; band = {}; autoTrack = nullptr;
    h.sendChangeMessage(); repaint();
}

void Lanes::mouseDoubleClick(const juce::MouseEvent& e) {
    auto hit = hitTest(e.getPosition());
    int yy = e.y + av.scrollY; Row* row = av.rowAt(yy);
    if (row && row->autoH > 0 && yy > row->y + row->h) {            // a point: remove it
        auto& t = *row->t; for (auto& a : t.autos) if (juce::String(a.path) == t.autoShown)
            for (int i = 0; i < (int) a.pts.size(); ++i) if (std::abs(av.xOf(a.pts[(size_t) i].beat) - e.x) < 7) { host().edit("remove point", [&a, i] { a.pts.erase(a.pts.begin() + i); }); return; }
        return;
    }
    if (hit.c) { host().selectClip(hit.c->id); if (av.onOpenEditor) av.onOpenEditor(); return; }
    if (row && row->t->isInstrument()) {
        double b = std::floor(av.beatAt((float) e.x) / P().beatsPerBar) * P().beatsPerBar;
        Uid made = 0; Track* t = row->t;
        host().edit("new notes clip", [&] { auto c = P().newMidiClip(std::max(0.0, b), P().beatsPerBar); t->clips.push_back(c); made = c->id; });
        host().selectClip(made); if (av.onOpenEditor) av.onOpenEditor();
    } else if (row && row->t->kind == TrackKind::Audio) host().say(U("drop a sound file here, or arm the track (R) and record"));
}

void Lanes::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { av.zoomAround(w.deltaY > 0 ? 1.15 : 1 / 1.15, (float) e.x); return; }
    if (e.mods.isAltDown()) { for (auto& t : P().tracks) t->height = juce::jlimit(36.0f, 260.0f, t->height * (w.deltaY > 0 ? 1.1f : 0.9f)); av.refresh(); return; }
    double dx = w.deltaX != 0 ? -w.deltaX : (e.mods.isShiftDown() ? -w.deltaY : 0.0), dy = e.mods.isShiftDown() ? 0.0 : -w.deltaY;
    av.scrollBeat = std::max(0.0, av.scrollBeat + dx * 400.0 / av.ppb);
    av.scrollY = juce::jlimit(0, juce::jmax(0, av.contentHeight() - getHeight() + 40), av.scrollY + (int) (dy * 300.0));
    av.followPlay = av.followPlay && std::abs(dx) < 1e-9;
    av.resized(); av.repaint();
}
void Lanes::mouseMagnify(const juce::MouseEvent& e, float scale) { av.zoomAround(scale, (float) e.x); }

void Lanes::contextMenu(juce::Point<int> pt) {
    auto hit = hitTest(pt);
    auto& h = host();
    juce::PopupMenu m;
    if (hit.c) {
        if (!h.selClips.count(hit.c->id)) h.selectClip(hit.c->id);
        Clip& c = *hit.c; Uid id = c.id;
        m.addSectionHeader(c.name);
        m.addItem(1, "Open in the editor");
        m.addItem(2, "Rename");
        m.addItem(3, c.muted ? "Unmute" : "Mute");
        m.addItem(4, "Split at the playhead");
        m.addItem(5, "Duplicate");
        m.addItem(6, "Delete");
        juce::PopupMenu col; for (int i = 0; i < 12; ++i) col.addItem(700 + i, "Colour " + juce::String(i + 1)); col.addItem(712, "Like the track");
        m.addSubMenu("Colour", col);
        m.addSeparator();
        if (!c.midi) {
            m.addItem(10, "Normalize");
            m.addItem(11, c.reverse ? "Play forwards" : "Reverse");
            juce::PopupMenu pitch; for (int s : { -12, -7, -5, -3, -2, -1, 1, 2, 3, 5, 7, 12 }) pitch.addItem(100 + s + 24, (s > 0 ? "+" : "") + juce::String(s) + " semitones");
            m.addSubMenu("Pitch (same length)", pitch);
            juce::PopupMenu st; for (auto r : { 0.5, 0.667, 0.75, 0.9, 1.1, 1.25, 1.333, 1.5, 2.0 }) st.addItem(200 + (int) std::lround(r * 1000), "x" + juce::String(r, 2) + (r > 1 ? " (slower)" : " (faster)"));
            m.addSubMenu("Stretch (same pitch)", st);
            juce::PopupMenu sp; sp.addItem(301, "Half speed (an octave down)"); sp.addItem(302, "Double speed (an octave up)"); m.addSubMenu("Speed (pitch and tempo)", sp);
            m.addItem(12, "Fit to whole bars");
            m.addItem(13, "Apply the clip's effects (FX)", !c.fx.empty());
            m.addItem(14, "Apply the track's effects to this clip");
            m.addItem(15, "Strip silence (split into the loud parts)");
            m.addSeparator();
            m.addItem(16, "Play it in a SAMPLER (new track)");
            juce::PopupMenu wt; int k = 0;
            for (auto& t : P().tracks) if (t->inst && t->inst->type == "nova") for (int o = 0; o < 3; ++o) wt.addItem(800 + k * 3 + o, t->name + U(" · OSC ") + juce::String::charToString((juce::juce_wchar) ('A' + o))), ++k;
            if (k > 0) m.addSubMenu("Make it a NOVA wavetable", wt);
            m.addItem(17, "Revert to the original sound", c.original.isNotEmpty());
        } else {
            juce::PopupMenu q; q.addItem(20, "1/4"); q.addItem(21, "1/8"); q.addItem(22, "1/16"); q.addItem(23, "1/8 triplets"); m.addSubMenu("Quantize", q);
            m.addItem(24, "Transpose +12"); m.addItem(25, "Transpose -12"); m.addItem(26, "Legato (notes reach the next one)");
        }
        m.showMenuAsync(juce::PopupMenu::Options(), [this, id](int r) {
            auto& h = host(); juce::String err;
            Track* t = nullptr; Clip* c = P().clip(id, &t); if (!c) return;
            auto busy = [&](const juce::String& op, double amt) { juce::MouseCursor::showWaitCursor(); err = h.processClip(id, op, amt); juce::MouseCursor::hideWaitCursor(); };
            if (r == 1 && av.onOpenEditor) { h.selectClip(id); av.onOpenEditor(); }
            else if (r == 2) {
                auto* aw = new juce::AlertWindow("Rename", "", juce::MessageBoxIconType::NoIcon); aw->addTextEditor("n", c->name); aw->addButton("OK", 1); aw->addButton("Cancel", 0);
                aw->enterModalState(true, juce::ModalCallbackFunction::create([aw, id](int b) { if (b == 1) { auto nm = aw->getTextEditorContents("n"); host().edit("rename clip", [id, nm] { if (auto* cc = P().clip(id)) cc->name = nm; }, false); } }), true);
            }
            else if (r == 3) h.edit("mute clip", [c] { c->muted = !c->muted; });
            else if (r == 4) h.splitClip(id, h.engine.position());
            else if (r == 5) h.duplicateClips();
            else if (r == 6) h.deleteSelection();
            else if (r >= 700 && r <= 712) h.edit("clip colour", [c, r] { c->colour = r == 712 ? 0 : Project::colourFor(r - 700); }, false);
            else if (r == 10) busy("normalize", 0);
            else if (r == 11) busy("reverse", 0);
            else if (r >= 100 && r < 150) busy("pitch", r - 124);
            else if (r >= 200 && r < 2300) busy("stretch", (r - 200) / 1000.0);
            else if (r == 301) busy("speed", 0.5); else if (r == 302) busy("speed", 2.0);
            else if (r == 12) busy("fitbars", 0);
            else if (r == 13) busy("bake", 0);
            else if (r == 14) busy("baketrack", 0);
            else if (r == 15) busy("silence", -45);
            else if (r == 16) h.clipToSampler(id);
            else if (r >= 800) { int k = (r - 800) / 3, o = (r - 800) % 3, n = 0; for (auto& tt : P().tracks) if (tt->inst && tt->inst->type == "nova") { if (n == k) { h.clipToWavetable(id, tt->id, o); h.say("OSC " + juce::String::charToString((juce::juce_wchar) ('A' + o)) + " of " + tt->name + " now plays this sound as a wavetable"); } ++n; } }
            else if (r == 17) busy("revert", 0);
            else if (r >= 20 && r <= 23) busy("quantize", r == 20 ? 1.0 : r == 21 ? 0.5 : r == 22 ? 0.25 : 1.0 / 3.0);
            else if (r == 24) busy("transpose", 12); else if (r == 25) busy("transpose", -12); else if (r == 26) busy("legato", 0);
            if (err.isNotEmpty()) h.say(err);
        });
        return;
    }
    int yy = pt.y + av.scrollY; Row* row = av.rowAt(yy);
    double b = std::floor(av.beatAt((float) pt.x) / P().beatsPerBar) * P().beatsPerBar;
    if (row && row->t->isInstrument()) m.addItem(30, "New notes clip here");
    m.addItem(31, "Add a station here");
    m.addItem(32, "Loop this bar");
    m.addItem(33, "Playhead here");
    Track* rt = row ? row->t : nullptr;
    m.showMenuAsync(juce::PopupMenu::Options(), [this, b, rt](int r) {
        auto& h = host();
        if (r == 30 && rt) { Uid made = 0; h.edit("new notes clip", [&] { auto c = P().newMidiClip(std::max(0.0, b), P().beatsPerBar); rt->clips.push_back(c); made = c->id; }); h.selectClip(made); if (av.onOpenEditor) av.onOpenEditor(); }
        if (r == 31) h.edit("new station", [b] { auto& p = P(); Station s; s.id = newUid(); s.beat = std::max(0.0, b); s.name = "Station " + juce::String((int) p.stations.size() + 1);
            s.snap = p.captureKnobs(); s.hasSnap = true; p.stations.push_back(s); p.sortStations(); }, false);
        if (r == 32) { P().loopOn = true; h.setLoopRange(std::max(0.0, b), std::max(0.0, b) + P().beatsPerBar); }
        if (r == 33) h.locate(std::max(0.0, b));
    });
}

// ======================================================================== the view
ArrangeView::ArrangeView() {
    addAndMakeVisible(ruler); addAndMakeVisible(lanes); addAndMakeVisible(headerBox);
    addAndMakeVisible(hbar); addAndMakeVisible(vbar);
    hbar.setAutoHide(false); vbar.setAutoHide(false);
    for (auto* p : { &addTrack, &snapBtn, &zoomIn, &zoomOut, &followBtn, &fitBtn }) { addAndMakeVisible(*p); p->fsize = 9.0f; p->radius = 5.0f; }
    addTrack.bg = [] { return Theme::btn(); };
    addTrack.onClick = [this] { addTrackMenu(); };
    snapBtn.dynText = [this] { static const char* N[6] = { "SNAP OFF", "SNAP BAR", "SNAP BEAT", "SNAP 1/8", "SNAP 1/16", "SNAP AUTO" }; return juce::String(N[snapMode]); };
    snapBtn.onClick = [this] { juce::PopupMenu m; const char* N[6] = { "Off", "Bar", "Beat", "1/8", "1/16", "Automatic (by zoom)" };
        for (int i = 0; i < 6; ++i) m.addItem(i + 1, N[i], true, snapMode == i);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&snapBtn), [this](int r) { if (r > 0) { snapMode = r - 1; repaint(); } }); };
    zoomIn.onClick = [this] { zoomAround(1.4, (float) lanes.getWidth() / 2); };
    zoomOut.onClick = [this] { zoomAround(1 / 1.4, (float) lanes.getWidth() / 2); };
    fitBtn.onClick = [this] { double len = std::max(16.0, P().length() + 4); ppb = juce::jlimit(2.0, 400.0, (lanes.getWidth() - 20) / len); scrollBeat = 0; resized(); repaint(); };
    followBtn.lit = [this] { return followPlay; }; followBtn.litColour = [] { return Theme::green(); };
    followBtn.onClick = [this] { followPlay = !followPlay; repaint(); };
    followBtn.setTooltip("the view follows the playhead");
    struct HL : juce::ScrollBar::Listener { ArrangeView& a; explicit HL(ArrangeView& x) : a(x) {}
        void scrollBarMoved(juce::ScrollBar* sb, double v) override {
            if (sb == &a.hbar) { a.scrollBeat = v; a.followPlay = false; } else a.scrollY = (int) v;
            a.layoutHeaders(); a.lanes.repaint(); a.ruler.repaint(); } };
    sbl = std::make_unique<HL>(*this); hbar.addListener(sbl.get()); vbar.addListener(sbl.get());
    startTimerHz(30);
}
ArrangeView::~ArrangeView() { hbar.removeListener(sbl.get()); vbar.removeListener(sbl.get()); }

double ArrangeView::gridBeats() const {
    double bpb = host().project->beatsPerBar;
    switch (snapMode) { case 0: return 0; case 1: return bpb; case 2: return 1; case 3: return 0.5; case 4: return 0.25;
        default: return ppb < 6 ? bpb : ppb < 26 ? 1.0 : ppb < 60 ? 0.5 : 0.25; }
}
double ArrangeView::snap(double beat, bool force) const {
    double g = gridBeats(); if (g <= 0) g = force ? 0.25 : 0;
    return g > 0 ? std::round(beat / g) * g : beat;
}
int ArrangeView::contentHeight() const { int h = 0; for (auto& r : rows) h = std::max(h, r.y + r.h + r.autoH); return h; }
Row* ArrangeView::rowAt(int y) { for (auto& r : rows) if (y >= r.y && y < r.y + r.h + r.autoH) return &r; return nullptr; }

void ArrangeView::refresh() {
    rows.clear(); int y = 0;
    for (auto& t : host().project->tracks) { Row r; r.t = t.get(); r.y = y; r.h = (int) t->height; r.autoH = t->showAuto ? 70 : 0; rows.push_back(r); y += r.h + r.autoH; }
    headers.clear();
    for (auto& r : rows) { auto* h = new TrackHeader(*this, r.t); headers.add(h); headerBox.addAndMakeVisible(h); }
    seenStructure = host().structure;
    resized(); repaint();
}

void ArrangeView::layoutHeaders() {
    for (int i = 0; i < headers.size() && i < (int) rows.size(); ++i) headers[i]->setBounds(0, rows[(size_t) i].y - scrollY, headerW, rows[(size_t) i].h);
}

void ArrangeView::resized() {
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom(bottomBar);
    addTrack.setBounds(bottom.getX() + 8, bottom.getY() + 4, 90, bottomBar - 8);
    auto right = bottom.withTrimmedLeft(headerW);
    followBtn.setBounds(right.removeFromRight(70).reduced(3, 4));
    fitBtn.setBounds(right.removeFromRight(46).reduced(3, 4));
    zoomIn.setBounds(right.removeFromRight(30).reduced(3, 4));
    zoomOut.setBounds(right.removeFromRight(30).reduced(3, 4));
    snapBtn.setBounds(right.removeFromRight(84).reduced(3, 4));
    hbar.setBounds(right.reduced(6, 9));
    auto top = r.removeFromTop(rulerH);
    ruler.setBounds(top.withTrimmedLeft(headerW));
    vbar.setBounds(r.removeFromRight(12));
    headerBox.setBounds(r.removeFromLeft(headerW));
    lanes.setBounds(r);
    double total = std::max(P().length() + 32.0, scrollBeat + lanes.getWidth() / ppb + 8);
    hbar.setRangeLimits(0, total, juce::dontSendNotification); hbar.setCurrentRange(scrollBeat, lanes.getWidth() / ppb, juce::dontSendNotification);
    int ch = contentHeight() + 60;
    vbar.setRangeLimits(0, std::max(ch, lanes.getHeight()), juce::dontSendNotification); vbar.setCurrentRange(scrollY, lanes.getHeight(), juce::dontSendNotification);
    layoutHeaders();
}

void ArrangeView::paint(juce::Graphics& g) {
    g.fillAll(Theme::panel());
    auto corner = juce::Rectangle<int>(0, 0, headerW, rulerH);
    g.setColour(Theme::panel()); g.fillRect(corner);
    label(g, "TRACKS", corner.reduced(14, 0).withHeight(22), 9.0f, 2);
    label(g, juce::String((int) P().tracks.size()) + U(" · bars / beats · stations"), corner.reduced(14, 0).withTrimmedTop(22), 7.5f, 0);
    g.setColour(Theme::line()); g.drawLine(0, (float) getHeight() - bottomBar, (float) getWidth(), (float) getHeight() - bottomBar);
    g.drawLine((float) headerW - 0.5f, 0, (float) headerW - 0.5f, (float) getHeight() - bottomBar);
}

void ArrangeView::zoomAround(double f, float x) {
    double b = beatAt(x);
    ppb = juce::jlimit(2.0, 400.0, ppb * f);
    scrollBeat = std::max(0.0, b - x / ppb);
    resized(); repaint();
}

void ArrangeView::updateFromHost() {
    if (host().structure != seenStructure) refresh();
    for (auto* h : headers) h->repaint();
    lanes.repaint(); ruler.repaint();
}

void ArrangeView::timerCallback() {
    for (auto* h : headers) for (auto* c : h->getChildren()) if (auto* m = dynamic_cast<Meter*>(c)) m->update();
    double pos = host().engine.position();
    if (pos != lastPos || host().recording()) {
        // keep the playhead in view while playing
        if (followPlay && host().engine.isPlaying()) {
            float x = xOf(pos);
            if (x > lanes.getWidth() - 40 || x < 0) { scrollBeat = std::max(0.0, pos - 2.0); resized(); }
        }
        lastPos = pos; lanes.repaint(); ruler.repaint();
    }
}

void ArrangeView::openAutomation(Param* p, Uid trackId) {
    auto& pr = P(); Track* t = pr.track(trackId); if (!t || !p) return;
    std::string path;
    for (auto& kv : pr.paramMap()) if (kv.second == p) path = kv.first;
    if (path.empty()) return;
    host().edit("automate", [&] {
        bool have = false; for (auto& a : t->autos) if (a.path == path) have = true;
        if (!have) { AutoLane l; l.path = path; double now = host().engine.position(); l.pts.push_back({ now, p->norm() }); t->autos.push_back(l); }
        t->showAuto = true; t->autoShown = path;
    });
    host().say(U("automation: ") + pr.describe(path) + U(" · click the lane to add points, double-click removes"));
    refresh();
}

void ArrangeView::filesDropped(const juce::StringArray& files, int x, int y) {
    dropAt = { -1, -1 };
    auto p = juce::Point<int>(x, y) - lanes.getPosition();
    double beat = std::max(0.0, snap(beatAt((float) p.x)));
    Row* row = rowAt(p.y + scrollY);
    Uid tid = row ? row->t->id : 0;
    juce::String errs;
    for (auto& f : files) {
        juce::File file(f);
        if (file.isDirectory() && file.getChildFile("project.json").existsAsFile()) { host().openProject(file); return; }
        auto e = host().importClip(file, tid, beat);
        if (e.isNotEmpty()) errs << e << "\n";
        else if (Clip* c = host().selectedClip()) { beat = c->end(); Track* ot = nullptr; P().clip(c->id, &ot); if (ot) tid = ot->id; }
    }
    if (errs.isNotEmpty()) host().say(errs.trim());
    refresh();
}

void ArrangeView::itemDropped(const SourceDetails& d) {
    dropAt = { -1, -1 };
    auto s = d.description.toString();
    auto p = d.localPosition.toInt() - lanes.getPosition();
    double beat = std::max(0.0, snap(beatAt((float) p.x)));
    Row* row = rowAt(p.y + scrollY);
    if (s.startsWith("file:")) { auto e = host().importClip(juce::File(s.substring(5)), row ? row->t->id : 0, beat); if (e.isNotEmpty()) host().say(e); refresh(); }
    else if (s.startsWith("fx:") && row) { auto type = s.substring(3).toStdString(); Track* t = row->t;
        host().edit("add effect", [t, type] { if (auto e = makeEffect(type)) t->fx.push_back(std::shared_ptr<Effect>(std::move(e))); }); host().selectTrack(t->id); }
    else if (s.startsWith("track:") && row) {               // reorder tracks
        Uid id = (Uid) s.substring(6).getIntValue(); Uid to = row->t->id;
        if (id != to) host().edit("move track", [id, to] { auto& tr = P().tracks; auto it = std::find_if(tr.begin(), tr.end(), [&](auto& x) { return x->id == id; });
            if (it == tr.end()) return; auto t = *it; tr.erase(it); auto jt = std::find_if(tr.begin(), tr.end(), [&](auto& x) { return x->id == to; }); tr.insert(jt, t); });
    }
    repaint();
}

void ArrangeView::addTrackMenu() {
    juce::PopupMenu m;
    m.addItem(1, "Audio track (record / import sounds)");
    m.addItem(2, "NOVA synth track");
    m.addItem(3, "SAMPLER track");
    m.addItem(4, "DRUMS track");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addTrack), [this](int r) {
        if (r <= 0) return;
        Uid made = 0;
        host().edit("new track", [&] {
            auto& p = P();
            TrackPtr t;
            if (r == 1) { int n = 1; for (auto& x : p.tracks) if (x->kind == TrackKind::Audio) ++n; t = p.newTrack(TrackKind::Audio, "Audio " + juce::String(n)); t->inType = InputType::Mono; }
            else t = p.newTrack(TrackKind::Instrument, r == 2 ? "NOVA" : r == 3 ? "SAMPLER" : "DRUMS", r == 2 ? "nova" : r == 3 ? "smp" : "drm");
            // a new track sounds the same in every station (its knobs join every station's sound)
            for (auto& s : p.stations) if (s.hasSnap) for (auto& kv : p.captureKnobs()) s.snap.insert(kv);
            p.tracks.push_back(t); made = t->id;
        });
        host().selectTrack(made);
        refresh();
        if ((r >= 2) && onOpenInstrument) onOpenInstrument(made);
    });
}

} // namespace score
