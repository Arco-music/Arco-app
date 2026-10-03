#include "Rack.h"

namespace score {

static Project& P() { return *host().project; }

juce::Colour FxCard::colourOf(const std::string& type) {
    if (auto* i = effectInfo(type)) return Theme::c(juce::Colour::fromFloatRGBA(i->r, i->g, i->b, 1.0f));
    return Theme::blue();
}
juce::Colour FxCard::colour() const { return colourOf(fx->type); }

// ======================================================================== an effect card
FxCard::FxCard(std::shared_ptr<Effect> e, Uid ownerTrack, std::vector<std::shared_ptr<Effect>>* ch) : fx(std::move(e)), track(ownerTrack), chain(ch) {
    auto col = [this] { return colour(); };
    for (auto& p : fx->params) {
        if (p->unit == Unit::OnOff) {
            auto* t = new ParamToggle(p.get(), juce::String(p->name), col); controls.add(t); addAndMakeVisible(t); ++toggles;
        } else if (p->unit == Unit::Choice) {
            auto* c = new ParamChoice(p.get(), col); c->showName = false; controls.add(c); addAndMakeVisible(c); ++choices;
            c->setTooltip(juce::String(p->name));
        } else {
            auto* k = new ParamKnob(p.get(), col, ownerTrack); controls.add(k); knobs.push_back(k); addAndMakeVisible(k);
        }
    }
}

int FxCard::knobRows(int h) const {
    int n = (int) knobs.size(); if (n <= 2) return 1;
    int small = toggles + choices;
    int screenH = h < 200 ? 26 : 42;
    int left = h - 28 - screenH - 6 - (small > 0 ? 24 : 0) - 8;
    return left >= 120 ? 2 : 1;
}
int FxCard::preferredWidth(int h) const {
    int rows = knobRows(h);
    int cols = ((int) knobs.size() + rows - 1) / rows;
    int small = toggles + choices;
    int smallW = small * 70;
    return juce::jlimit(150, 520, juce::jmax(cols * 62 + 16, juce::jmin(smallW / 2 + 16, 360)));
}

void FxCard::resized() {
    auto r = getLocalBounds().reduced(8, 0);
    r.removeFromTop(28);
    onBox = { 8, 6, 26, 16 }; menuBox = { getWidth() - 26, 5, 20, 18 };
    screen = r.removeFromTop(getHeight() < 200 ? 26 : 42);
    r.removeFromTop(6);
    // toggles and choices in a row (or two)
    int small = toggles + choices;
    if (small > 0) {
        int perRow = juce::jmax(1, r.getWidth() / 66);
        int rowsN = (small + perRow - 1) / perRow;
        int w = (r.getWidth() - (juce::jmin(perRow, small) - 1) * 4) / juce::jmin(perRow, small);
        int i = 0;
        for (auto* c : controls) {
            if (dynamic_cast<ParamKnob*>(c)) continue;
            int row = i / perRow, col = i % perRow;
            c->setBounds(r.getX() + col * (w + 4), r.getY() + row * 22, w, 18); ++i;
        }
        r.removeFromTop(rowsN * 22 + 2);
    }
    int n = (int) knobs.size(); if (n == 0) return;
    int rowsK = knobRows(getHeight());
    int cols = (n + rowsK - 1) / rowsK;
    int cw = r.getWidth() / juce::jmax(1, cols), ch = juce::jmin(72, r.getHeight() / rowsK);
    for (int i = 0; i < n; ++i) knobs[(size_t) i]->setBounds(r.getX() + (i % cols) * cw, r.getY() + (i / cols) * ch, cw, ch);
}

void FxCard::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    bool on = fx->enabled.load();
    cardBg(g, r, 10.0f);
    auto col = colour();
    g.setColour(col.withAlpha(on ? 0.9f : 0.25f)); g.fillRoundedRectangle(r.withHeight(3.0f).reduced(10, 0), 1.5f);
    // on / off
    g.setColour(on ? col : Theme::btn()); g.fillRoundedRectangle(onBox.toFloat(), 8.0f);
    g.setColour(juce::Colours::white); g.fillEllipse(on ? (float) onBox.getRight() - 14 : (float) onBox.getX() + 2, (float) onBox.getY() + 2, 12, 12);
    auto* info = effectInfo(fx->type);
    g.setColour(on ? Theme::txt() : Theme::sub()); g.setFont(Theme::font(10.5f, 2));
    g.drawFittedText(info ? info->name : fx->type, 40, 5, getWidth() - 70, 18, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour(Theme::sub()); g.fillPath(iconPath(Icon::Menu, menuBox.toFloat().reduced(3)));
    // the little screen
    auto s = screen.toFloat();
    g.setColour(Theme::scr()); g.fillRoundedRectangle(s, 6.0f);
    if (fx->hasCurve()) {
        juce::Path p; bool first = true;
        for (int x = 0; x <= (int) s.getWidth(); x += 2) {
            double f = 20.0 * std::pow(1000.0, x / s.getWidth());
            float db = juce::jlimit(-24.0f, 24.0f, fx->curveDb(f));
            float y = s.getCentreY() - db / 24.0f * (s.getHeight() * 0.45f);
            if (first) { p.startNewSubPath(s.getX() + x, y); first = false; } else p.lineTo(s.getX() + x, y);
        }
        g.setColour(Theme::line()); g.drawHorizontalLine((int) s.getCentreY(), s.getX() + 4, s.getRight() - 4);
        g.setColour(col.withAlpha(on ? 1.0f : 0.4f)); g.strokePath(p, juce::PathStrokeType(1.6f));
    } else {
        auto& sc = fx->scope; int w = sc.w.load(std::memory_order_acquire); int N = ScopeRing::N;
        int pts = juce::jmin(N, (int) s.getWidth() * 2);
        juce::Path p;
        for (int i = 0; i < pts; ++i) {
            float v = sc.d[(size_t) ((w - pts + i + N) % N)];
            float x = s.getX() + s.getWidth() * i / (float) pts, y = s.getCentreY() - juce::jlimit(-1.0f, 1.0f, v * 1.4f) * s.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(col.withAlpha(on ? 0.95f : 0.35f)); g.strokePath(p, juce::PathStrokeType(1.2f));
    }
    // live readouts
    juce::String ro; float m = fx->meter[0].load();
    if (fx->type == "comp" && m < -0.05f) ro = "GR " + juce::String(m, 1) + " dB";
    else if (fx->type == "lim" && m > 0.05f) ro = "GR -" + juce::String(m, 1) + " dB";
    else if (fx->type == "gate") ro = m > 0.5f ? "OPEN" : "closed";
    else if (fx->type == "halo" && m > 20) ro = juce::MidiMessage::getMidiNoteName((int) std::lround(69 + 12 * std::log2(m / 440.0)), true, true, 4);
    else if (fx->type == "frz") ro = m > 1.5f ? "HOLDING" : m > 0.5f ? "CAPTURING" : "";
    else if (fx->type == "stut" && m > 0.5f) ro = "REPEAT";
    if (ro.isNotEmpty()) label(g, ro, screen.reduced(6, 3).withHeight(12), 7.5f, 2, col, juce::Justification::topRight);
}

void FxCard::tick() {
    if (isShowing()) { repaint(screen); for (auto* k : knobs) k->repaint(); for (auto* c : controls) c->repaint(); }
}

void FxCard::mouseDown(const juce::MouseEvent& e) {
    if (onBox.expanded(4).contains(e.getPosition())) {
        host().knobTouched(); fx->enabled.store(!fx->enabled.load()); repaint(); host().sendChangeMessage(); return;
    }
    if (menuBox.expanded(4).contains(e.getPosition()) || e.mods.isPopupMenu()) { menu(); return; }
}
void FxCard::mouseDrag(const juce::MouseEvent& e) {
    if (e.getMouseDownY() < 28 && e.getDistanceFromDragStart() > 8)
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!dc->isDragAndDropActive())
            dc->startDragging("card:" + juce::String((int) fx->uid), this);
}
void FxCard::itemDropped(const SourceDetails& d) {
    Uid id = (Uid) d.description.toString().substring(5).getIntValue();
    if (id == fx->uid || !chain) return;
    auto* ch = chain; Uid to = fx->uid;
    host().edit("move effect", [ch, id, to] {
        auto it = std::find_if(ch->begin(), ch->end(), [&](auto& x) { return x->uid == id; }); if (it == ch->end()) return;
        auto e = *it; ch->erase(it);
        auto jt = std::find_if(ch->begin(), ch->end(), [&](auto& x) { return x->uid == to; }); ch->insert(jt, e); });
}

void FxCard::menu() {
    juce::PopupMenu m;
    auto* info = effectInfo(fx->type);
    if (info) m.addSectionHeader(juce::String(info->name) + U(" · ") + info->about);
    m.addItem(1, fx->enabled.load() ? "Switch off" : "Switch on");
    m.addItem(2, "Reset its knobs");
    m.addItem(3, "Move left"); m.addItem(4, "Move right");
    m.addItem(5, "Duplicate");
    m.addSeparator();
    m.addItem(7, "Use these knobs in every station");
    m.addItem(6, "Remove");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int r) {
        auto* ch = chain; Uid id = fx->uid; auto e = fx;
        auto idx = [ch, id] { for (size_t i = 0; i < ch->size(); ++i) if ((*ch)[i]->uid == id) return (int) i; return -1; };
        if (r == 1) { host().knobTouched(); e->enabled.store(!e->enabled.load()); }
        else if (r == 2) { host().knobTouched(); e->resetToDefaults(); }
        else if (r == 3 || r == 4) host().edit("move effect", [ch, idx, r] { int i = idx(), j = i + (r == 3 ? -1 : 1); if (i >= 0 && j >= 0 && j < (int) ch->size()) std::swap((*ch)[(size_t) i], (*ch)[(size_t) j]); });
        else if (r == 5) host().edit("duplicate effect", [ch, idx, e] { int i = idx(); auto d = makeEffect(e->type); if (!d) return;
            for (size_t k = 0; k < e->params.size(); ++k) d->params[k]->set(e->params[k]->get()); d->uid = newUid();
            ch->insert(ch->begin() + i + 1, std::shared_ptr<Effect>(std::move(d))); });
        else if (r == 6) host().edit("remove effect", [ch, idx] { int i = idx(); if (i >= 0) ch->erase(ch->begin() + i); });
        else if (r == 7) host().edit("same in every station", [e] {
            for (auto& s : P().stations) { for (auto& p : e->params) s.snap[Project::path(*e, *p)] = p->get(); s.snap[std::to_string(e->uid) + "/@on"] = e->enabled.load() ? 1.0f : 0.0f; } }, false);
        host().sendChangeMessage(); repaint();
    });
}

// ======================================================================== the instrument card
InstCard::InstCard(Track* t) : track(t) {
    auto col = [this] { return Theme::argb(track->colour); };
    auto& in = *t->inst;
    std::vector<std::string> ids;
    if (in.type == "nova") ids = { "m1", "m2", "m3", "m4", "f1c", "f1r", "lvl", "glide" };
    else if (in.type == "smp") ids = { "tune", "start", "a", "r", "fc", "fr", "lvl", "pan" };
    else ids = { "lvl" };
    for (auto& id : ids) if (auto* p = in.find(id)) { auto* k = new ParamKnob(p, col, t->id); knobs.add(k); addAndMakeVisible(k); }
    addAndMakeVisible(open); open.bg = [this] { return Theme::argb(track->colour).withAlpha(0.55f); };
    open.onClick = [this] { if (onOpen) onOpen(); };
    open.text = in.type == "nova" ? "OPEN NOVA" : in.type == "smp" ? "OPEN SAMPLER" : "OPEN DRUMS";
}
void InstCard::resized() {
    open.setBounds(getWidth() - 104, 6, 96, 20);
    bool compact = getHeight() < 200;
    auto r = getLocalBounds().reduced(8, 0).withTrimmedTop(compact ? 58 : 76);
    int n = knobs.size(); if (n == 0) return;
    int cols = n > 4 && !compact ? (n + 1) / 2 : n, rowsN = (n + cols - 1) / cols;
    int cw = r.getWidth() / cols, ch = juce::jmin(72, r.getHeight() / rowsN);
    for (int i = 0; i < n; ++i) knobs[i]->setBounds(r.getX() + (i % cols) * cw, r.getY() + (i / cols) * ch, cw, ch);
}
void InstCard::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    cardBg(g, r, 10.0f);
    auto col = Theme::argb(track->colour);
    g.setColour(col); g.fillRoundedRectangle(r.withHeight(3.0f).reduced(10, 0), 1.5f);
    auto& in = *track->inst;
    label(g, in.type == "nova" ? "NOVA" : in.type == "smp" ? "SAMPLER" : "DRUMS", { 10, 6, 140, 20 }, 11.0f, 2, Theme::txt());
    auto s = juce::Rectangle<float>(8, 32, r.getWidth() - 16, getHeight() < 200 ? 22.0f : 40.0f);
    g.setColour(Theme::scr()); g.fillRoundedRectangle(s, 6.0f);
    if (in.type == "drm") {
        auto* d = dynamic_cast<Drums*>(&in);
        float w = (s.getWidth() - 6) / 16.0f;
        for (int k = 0; k < 16; ++k) { float h = d ? d->hit[(size_t) k].load() : 0; auto pr = juce::Rectangle<float>(s.getX() + 3 + k * w, s.getY() + 6, w - 2, s.getHeight() - 12);
            g.setColour(col.withAlpha(0.18f + 0.8f * h)); g.fillRoundedRectangle(pr, 2); }
    } else if (auto* sm = dynamic_cast<Sampler*>(&in)) {
        auto smp = sm->sample();
        if (smp && smp->length() > 0) {
            int W = (int) s.getWidth(); g.setColour(col.withAlpha(0.8f));
            for (int x = 0; x < W; ++x) { int a = (int) ((int64_t) smp->length() * x / W), b = (int) ((int64_t) smp->length() * (x + 1) / W); float pk = 0;
                for (int i = a; i < b; i += 4) pk = std::max(pk, std::abs(smp->L[(size_t) i])); float hh = pk * s.getHeight() * 0.45f;
                g.fillRect(s.getX() + x, s.getCentreY() - hh, 1.0f, hh * 2 + 0.5f); }
            for (auto& pp : sm->playPos) { float v = pp.load(); if (v >= 0) { g.setColour(Theme::txt()); g.fillRect(s.getX() + v * s.getWidth(), s.getY(), 1.0f, s.getHeight()); } }
        } else label(g, U("drop a sound here, or open the sampler"), s.toNearestInt().reduced(8, 0), 8.0f, 0);
    } else {
        auto& sc = in.scope; int w = sc.w.load(); int N = ScopeRing::N, pts = juce::jmin(N, (int) s.getWidth() * 2);
        juce::Path p;
        for (int i = 0; i < pts; ++i) { float v = sc.d[(size_t) ((w - pts + i + N) % N)];
            float x = s.getX() + s.getWidth() * i / (float) pts, y = s.getCentreY() - juce::jlimit(-1.0f, 1.0f, v * 1.4f) * s.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
        g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.3f));
        label(g, juce::String(in.activeVoices()) + " voices", s.toNearestInt().reduced(6, 3).withHeight(12), 7.5f, 1, Theme::sub(), juce::Justification::topRight);
    }
}
void InstCard::mouseUp(const juce::MouseEvent& e) { if (e.y < 76 && onOpen) onOpen(); }

// ======================================================================== the rack
Rack::Rack() {
    view.setViewedComponent(&content, false); view.setScrollBarsShown(false, true, false, true); view.setScrollBarThickness(8);
    addAndMakeVisible(view);
    for (auto* p : { &trackTab, &clipTab, &addBtn, &storeBtn, &autoBtn }) { addAndMakeVisible(*p); p->fsize = 8.5f; p->radius = 5.0f; }
    trackTab.lit = [this] { return !clipMode; }; clipTab.lit = [this] { return clipMode; };
    trackTab.onClick = [this] { clipMode = false; update(); };
    clipTab.onClick = [this] { if (host().selectedClip()) { clipMode = true; update(); } else host().say(U("select a clip first: its own effects play only on that clip")); };
    clipTab.setTooltip("effects on the selected clip only (they play only while that clip plays)");
    addBtn.onClick = [this] { addMenu(&addBtn); };
    addBtn.bg = [] { return Theme::blue().withAlpha(0.8f); };
    storeBtn.onClick = [] { host().stations.store(); };
    storeBtn.bg = [] { return Theme::gold().withAlpha(0.85f); }; storeBtn.textColour = [] { return Theme::lightMode() ? juce::Colours::white : juce::Colour(0xff1a1a1a); };
    storeBtn.setTooltip("keep every knob as it is now in the current station");
    autoBtn.lit = [] { return host().stations.autoStore; }; autoBtn.litColour = [] { return Theme::gold().darker(0.25f); };
    autoBtn.onClick = [] { host().stations.autoStore = !host().stations.autoStore; host().sendChangeMessage(); };
    autoBtn.setTooltip("AUTO STORE: every knob you turn is kept in the station you are in; going to another station brings its knobs back");
    startTimerHz(24);
}

Track* Rack::target() const { return P().track(showTrack ? showTrack : host().selTrack); }

std::vector<std::shared_ptr<Effect>>* Rack::chain() {
    if (clipMode) { if (Clip* c = host().selectedClip()) return &c->fx; return nullptr; }
    if (Track* t = target()) return &t->fx;
    return nullptr;
}

void Rack::update() {
    if (clipMode && !host().selectedClip()) clipMode = false;
    cards.clear();
    Track* t = target();
    auto* ch = chain();
    if (t && !clipMode && t->inst) {
        auto* ic = new InstCard(t); Uid id = t->id;
        ic->onOpen = [this, id] { if (onOpenInstrument) onOpenInstrument(id); };
        cards.add(ic); content.addAndMakeVisible(ic);
    }
    if (ch) for (auto& e : *ch) { auto* c = new FxCard(e, t ? t->id : 0, ch); cards.add(c); content.addAndMakeVisible(c); }
    seenTrack = t ? t->id : 0; seenClip = host().selClip; seenStructure = host().structure; seenClipMode = clipMode; seenCount = ch ? ch->size() : 0;
    resized(); repaint();
}

void Rack::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(30).reduced(8, 5);
    trackTab.setBounds(top.removeFromLeft(74)); top.removeFromLeft(4); clipTab.setBounds(top.removeFromLeft(64));
    autoBtn.setBounds(top.removeFromRight(46)); top.removeFromRight(4); storeBtn.setBounds(top.removeFromRight(56));
    top.removeFromRight(10); addBtn.setBounds(top.removeFromRight(80));
    view.setBounds(r);
    int x = 8, h = r.getHeight() - 14;
    for (auto* c : cards) {
        int w = 200;
        if (auto* f = dynamic_cast<FxCard*>(c)) w = f->preferredWidth(h); else if (auto* i = dynamic_cast<InstCard*>(c)) w = i->preferredWidth(h);
        c->setBounds(x, 2, w, h); x += w + 8;
    }
    content.setBounds(0, 0, juce::jmax(x + 180, r.getWidth()), r.getHeight() - 4);
}

void Rack::paint(juce::Graphics& g) {
    g.fillAll(Theme::panel());
    Track* t = target();
    auto mid = juce::Rectangle<int>(trackTab.getRight() + clipTab.getWidth() + 18, 5, addBtn.getX() - clipTab.getRight() - 24, 20);
    juce::String what = !t ? juce::String("no track selected") : clipMode ? (U("clip  ") + (host().selectedClip() ? host().selectedClip()->name : juce::String())) : t->name;
    auto& sp = host().stations; auto& st = P().stations;
    juce::String stn = sp.current >= 0 && sp.current < (int) st.size() ? U("   ·   station ") + juce::String(sp.current + 1) + (st[(size_t) sp.current].name.isNotEmpty() ? U(" “") + st[(size_t) sp.current].name + U("”") : juce::String())
                       + (sp.autoStore ? U(" keeps what you turn") : U(" · STORE keeps it")) : juce::String();
    label(g, what, mid.removeFromLeft(juce::jmin(220, mid.getWidth() / 2)), 10.0f, 2, t ? Theme::argb(t->colour) : Theme::sub());
    label(g, stn, mid, 8.5f, 1, Theme::gold());
    // the space after the last card: a drop zone
    if (cards.isEmpty() || true) {
        int x = cards.isEmpty() ? 8 : cards.getLast()->getRight() + 8 - view.getViewPositionX();
        auto z = juce::Rectangle<int>(x, 32, 170, getHeight() - 46);
        if (z.getX() < getWidth()) {
            g.setColour(Theme::line()); juce::Path p; p.addRoundedRectangle(z.toFloat(), 10.0f);
            float dash[2] = { 5, 4 }; juce::PathStrokeType(1.2f).createDashedStroke(p, p, dash, 2); g.fillPath(p);
            label(g, clipMode ? U("+ an effect for this clip\n(or drag one here)") : U("+ an effect\n(or drag one here from the browser)"), z.reduced(12), 8.5f, 1, Theme::sub(), juce::Justification::centred);
        }
    }
}

void Rack::timerCallback() {
    auto* ch = chain();
    Track* t = target();
    if ((t ? t->id : 0) != seenTrack || host().structure != seenStructure || clipMode != seenClipMode || (clipMode && host().selClip != seenClip) || (ch ? ch->size() : 0) != seenCount) update();
    for (auto* c : cards) { if (auto* f = dynamic_cast<FxCard*>(c)) f->tick(); else if (auto* i = dynamic_cast<InstCard*>(c)) i->tick(); }
    static int k = 0; if (++k % 6 == 0) repaint(0, 0, getWidth(), 30);
}

void Rack::itemDropped(const SourceDetails& d) {
    auto type = d.description.toString().substring(3).toStdString();
    auto* ch = chain(); if (!ch) return;
    host().edit("add effect", [ch, type] { if (auto e = makeEffect(type)) ch->push_back(std::shared_ptr<Effect>(std::move(e))); });
}

void Rack::addMenu(juce::Component* near) {
    auto* ch = chain();
    if (!ch) { host().say(U("select a track first")); return; }
    juce::PopupMenu m; std::map<std::string, juce::PopupMenu> groups; std::vector<std::string> order;
    auto& cat = effectCatalog();
    for (int i = 0; i < (int) cat.size(); ++i) {
        std::string gname = cat[(size_t) i].group;
        if (!groups.count(gname)) order.push_back(gname);
        groups[gname].addItem(i + 1, juce::String(cat[(size_t) i].name) + U("  —  ") + cat[(size_t) i].about);
    }
    for (auto& gname : order) m.addSubMenu(gname, groups[gname]);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(near), [this](int r) {
        if (r <= 0) return; auto* ch = chain(); if (!ch) return;
        auto type = std::string(effectCatalog()[(size_t) r - 1].type);
        host().edit("add effect", [ch, type] { if (auto e = makeEffect(type)) ch->push_back(std::shared_ptr<Effect>(std::move(e))); });
        juce::Timer::callAfterDelay(50, [this] { view.setViewPosition(content.getWidth(), 0); });
    });
}

} // namespace score
