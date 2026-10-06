#include "MixerView.h"

namespace score {

static Project& P() { return *host().project; }

Strip::Strip(Track* t) : track(t) {
    auto col = [this] { return Theme::argb(track->colour); };
    fader = std::make_unique<Fader>(t->mix->find("vol"), col); addAndMakeVisible(*fader);
    bool isMaster = t->kind == TrackKind::Master;
    auto peak = [this, isMaster](bool left) { return [this, isMaster, left] {
        if (isMaster) return left ? host().engine.masterL.load() : host().engine.masterR.load();
        auto rt = host().engine.runtime(track->id); return left ? rt->peakL.load() : rt->peakR.load(); }; };
    meterL = std::make_unique<Meter>(peak(true)); meterR = std::make_unique<Meter>(peak(false));
    addAndMakeVisible(*meterL); addAndMakeVisible(*meterR);
    pan = std::make_unique<ParamKnob>(t->mix->find("pan"), col, t->id); addAndMakeVisible(*pan);
    if (t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument) {
        sa = std::make_unique<ParamKnob>(t->mix->find("sa"), [] { return juce::Colour(0xff7380ff); }, t->id); sa->label = "SEND A"; addAndMakeVisible(*sa);
        sb = std::make_unique<ParamKnob>(t->mix->find("sb"), [] { return juce::Colour(0xff52a0ff); }, t->id); sb->label = "SEND B"; addAndMakeVisible(*sb);
    }
    for (auto* p : { &mute, &solo, &arm }) { p->fsize = 8.5f; p->radius = 4.0f; }
    if (!isMaster) {
        addAndMakeVisible(mute); mute.lit = [this] { return track->mix->find("mute")->on(); }; mute.litColour = [] { return Theme::amber(); };
        mute.onClick = [this] { host().knobTouched(); auto* m = track->mix->find("mute"); m->set(m->on() ? 0.0f : 1.0f); host().sendChangeMessage(); };
    }
    if (t->kind == TrackKind::Audio || t->kind == TrackKind::Instrument) {
        addAndMakeVisible(solo); solo.lit = [this] { return track->solo; }; solo.onClick = [this] { host().edit("solo", [this] { track->solo = !track->solo; }); };
        addAndMakeVisible(arm); arm.lit = [this] { return track->arm; }; arm.litColour = [] { return Theme::red(); };
        arm.onClick = [this] { track->arm = !track->arm; host().rebuild(); host().sendChangeMessage(); };
    }
}

void Strip::resized() {
    auto r = getLocalBounds().reduced(6, 6);
    r.removeFromTop(26);                                     // name
    fxArea = r.removeFromTop(70);
    r.removeFromTop(4);
    if (sa) { auto row = r.removeFromTop(56); sa->setBounds(row.removeFromLeft(row.getWidth() / 2)); sb->setBounds(row); }
    pan->setBounds(r.removeFromTop(56).withSizeKeepingCentre(52, 56));
    auto bottom = r.removeFromBottom(22);
    int n = (mute.isVisible() ? 1 : 0) + (solo.isVisible() ? 1 : 0) + (arm.isVisible() ? 1 : 0);
    if (n > 0) { int w = (bottom.getWidth() - (n - 1) * 3) / n; for (auto* p : { &mute, &solo, &arm }) if (p->isVisible()) { p->setBounds(bottom.removeFromLeft(w)); bottom.removeFromLeft(3); } }
    r.removeFromBottom(18);
    auto fr = r.withSizeKeepingCentre(juce::jmin(r.getWidth(), 64), r.getHeight());
    meterR->setBounds(fr.removeFromRight(5)); fr.removeFromRight(2); meterL->setBounds(fr.removeFromRight(5));
    fader->setBounds(fr);
}

void Strip::paint(juce::Graphics& g) {
    bool sel = host().selTrack == track->id;
    auto r = getLocalBounds().toFloat().reduced(2);
    g.setColour(sel ? Theme::card2() : Theme::card()); g.fillRoundedRectangle(r, 10);
    auto col = Theme::argb(track->colour);
    g.setColour(col); g.fillRoundedRectangle(r.withHeight(4).reduced(10, 0), 2);
    label(g, track->name, getLocalBounds().reduced(8, 0).withY(9).withHeight(18), 9.5f, 2, Theme::txt(), juce::Justification::centred);
    // what is on the channel
    auto fa = fxArea; g.setColour(Theme::scr()); g.fillRoundedRectangle(fa.toFloat(), 6);
    fa.reduce(5, 4);
    if (track->inst) { label(g, track->inst->type == "nova" ? "NOVA" : track->inst->type == "smp" ? "SAMPLER" : "DRUMS", fa.removeFromTop(13), 7.5f, 2, col); }
    for (auto& e : track->fx) { if (fa.getHeight() < 12) { label(g, U("…"), fa, 7.5f, 1); break; }
        auto* inf = effectInfo(e->type); label(g, inf ? inf->name : e->type.c_str(), fa.removeFromTop(12), 7.5f, 1, e->enabled.load() ? Theme::txt() : Theme::off()); }
    if (track->fx.empty() && !track->inst) label(g, "no effects", fa.removeFromTop(12), 7.5f, 0);
    // level
    auto* v = track->mix->find("vol");
    label(g, v->get() <= -59.9f ? juce::String("-inf") : juce::String(v->get(), 1) + " dB", { 4, getHeight() - 46, getWidth() - 8, 14 }, 8.0f, 1, Theme::txt(), juce::Justification::centred);
}

void Strip::mouseDown(const juce::MouseEvent& e) {
    host().selectTrack(track->id);
    if (e.mods.isPopupMenu() && (track->kind == TrackKind::Audio || track->kind == TrackKind::Instrument)) {
        juce::PopupMenu m; m.addItem(1, "Bounce to an audio track (with its effects)");
        m.showMenuAsync(juce::PopupMenu::Options(), [id = track->id](int r) { if (r == 1) host().bounceTrack(id); });
    }
}

void Strip::tick() { meterL->update(); meterR->update(); }

// ======================================================================== the mixer
MixerView::MixerView() {
    view.setViewedComponent(&content, false); view.setScrollBarsShown(false, true, false, true); view.setScrollBarThickness(8);
    addAndMakeVisible(view);
    startTimerHz(30);
}

void MixerView::refresh() {
    strips.clear(); fixed.clear();
    for (auto& t : P().tracks) { auto* s = new Strip(t.get()); strips.add(s); content.addAndMakeVisible(s); }
    for (auto* t : { P().retA.get(), P().retB.get(), P().master.get() }) { auto* s = new Strip(t); fixed.add(s); addAndMakeVisible(s); }
    seenStructure = host().structure;
    resized(); repaint();
}

void MixerView::resized() {
    auto r = getLocalBounds().reduced(8, 8);
    const int w = 96;
    auto right = r.removeFromRight(w * 3 + 18);
    int x = right.getX() + 6;
    for (auto* s : fixed) { s->setBounds(x, right.getY(), w, right.getHeight()); x += w + 4; }
    view.setBounds(r);
    int cx = 0; for (auto* s : strips) { s->setBounds(cx, 0, w, r.getHeight() - 10); cx += w + 4; }
    content.setBounds(0, 0, juce::jmax(cx, r.getWidth()), r.getHeight() - 2);
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    if (!fixed.isEmpty()) { g.setColour(Theme::line()); g.drawVerticalLine(fixed[0]->getX() - 6, 12.0f, (float) getHeight() - 12); }
    if (strips.isEmpty()) label(g, "no tracks yet", view.getBounds(), 10.0f, 1, Theme::sub(), juce::Justification::centred);
}

void MixerView::timerCallback() {
    if (host().structure != seenStructure) refresh();
    if (!isShowing()) return;
    for (auto* s : strips) s->tick(); for (auto* s : fixed) s->tick();
    static int k = 0; if (++k % 10 == 0) { for (auto* s : strips) s->repaint(); for (auto* s : fixed) s->repaint(); }
}

} // namespace score
