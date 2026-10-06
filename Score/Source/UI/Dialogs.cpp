#include "Dialogs.h"

namespace score {

static Project& P() { return *host().project; }

void Overlay::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.55f));
    auto c = card().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.3f)); g.fillRoundedRectangle(c.translated(0, 6), 16);
    g.setColour(Theme::panel()); g.fillRoundedRectangle(c, 16);
    g.setColour(Theme::line()); g.drawRoundedRectangle(c, 16, 1);
    label(g, title, card().reduced(22, 0).withHeight(50), 14.0f, 2, Theme::txt());
}
void Overlay::resized() { auto c = card(); close.setBounds(c.getRight() - 90, c.getY() + 14, 74, 24); layout(); }

// ======================================================================== settings
static void menuFor(Pill& p, juce::StringArray items, int current, std::function<void(int)> f) {
    juce::PopupMenu m; for (int i = 0; i < items.size(); ++i) m.addItem(i + 1, items[i], true, i == current);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&p), [f](int r) { if (r > 0) f(r - 1); });
}

SettingsPanel::SettingsPanel() : Overlay("SETTINGS", 880, 640) {
    for (auto* p : { &outBtn, &inBtn, &rateBtn, &bufBtn, &outChBtn, &clickBtn, &toneBtn, &themeBtn, &scaleBtn }) { addAndMakeVisible(*p); p->fsize = 9.0f; p->radius = 6.0f; p->weight = 1; }
    auto& dm = host().devices;
    outBtn.dynText = [&dm] { auto* d = dm.getCurrentAudioDevice(); auto s = dm.getAudioDeviceSetup(); return d ? (s.outputDeviceName.isEmpty() ? d->getName() : s.outputDeviceName) : juce::String("no sound card"); };
    inBtn.dynText = [&dm] { auto s = dm.getAudioDeviceSetup(); return s.inputDeviceName.isEmpty() ? juce::String("no input") : s.inputDeviceName; };
    rateBtn.dynText = [&dm] { auto* d = dm.getCurrentAudioDevice(); return d ? juce::String((int) d->getCurrentSampleRate()) + " Hz" : juce::String("-"); };
    bufBtn.dynText = [&dm] { auto* d = dm.getCurrentAudioDevice(); return d ? juce::String(d->getCurrentBufferSizeSamples()) + " samples  (" + juce::String(d->getCurrentBufferSizeSamples() * 1000.0 / juce::jmax(1.0, d->getCurrentSampleRate()), 1) + " ms)" : juce::String("-"); };
    auto names = [&dm](bool input) { juce::StringArray a; if (auto* t = dm.getCurrentDeviceTypeObject()) { t->scanForDevices(); a = t->getDeviceNames(input); } return a; };
    outBtn.onClick = [this, names, &dm] { auto a = names(false); auto s = dm.getAudioDeviceSetup();
        menuFor(outBtn, a, a.indexOf(s.outputDeviceName), [a, &dm](int i) { auto st = dm.getAudioDeviceSetup(); auto m = host().openAudio(a[i], st.inputDeviceName, st.sampleRate, st.bufferSize); if (m.isNotEmpty()) host().say(m); }); };
    inBtn.onClick = [this, names, &dm] { auto a = names(true); a.insert(0, "no input"); auto s = dm.getAudioDeviceSetup();
        menuFor(inBtn, a, juce::jmax(0, a.indexOf(s.inputDeviceName)), [a, &dm](int i) { auto st = dm.getAudioDeviceSetup(); auto m = host().openAudio(st.outputDeviceName, i == 0 ? juce::String() : a[i], st.sampleRate, st.bufferSize); if (m.isNotEmpty()) host().say(m); }); };
    rateBtn.onClick = [this, &dm] { auto* d = dm.getCurrentAudioDevice(); if (!d) return; juce::StringArray a; auto rs = d->getAvailableSampleRates(); for (auto r : rs) a.add(juce::String((int) r) + " Hz");
        menuFor(rateBtn, a, rs.indexOf(d->getCurrentSampleRate()), [rs, &dm](int i) { auto st = dm.getAudioDeviceSetup(); host().openAudio(st.outputDeviceName, st.inputDeviceName, rs[i], st.bufferSize); }); };
    bufBtn.onClick = [this, &dm] { auto* d = dm.getCurrentAudioDevice(); if (!d) return; juce::StringArray a; auto bs = d->getAvailableBufferSizes(); for (auto b : bs) a.add(juce::String(b) + " samples");
        menuFor(bufBtn, a, bs.indexOf(d->getCurrentBufferSizeSamples()), [bs, &dm](int i) { auto st = dm.getAudioDeviceSetup(); host().openAudio(st.outputDeviceName, st.inputDeviceName, st.sampleRate, bs[i]); }); };
    outChBtn.dynText = [] { int o = host().engine.outFirst.load(); return "outputs " + juce::String(o + 1) + " + " + juce::String(o + 2); };
    outChBtn.onClick = [this] { juce::StringArray a; int n = juce::jmax(2, host().numOutputs()); for (int k = 0; k + 1 < n; k += 2) a.add("outputs " + juce::String(k + 1) + " + " + juce::String(k + 2));
        menuFor(outChBtn, a, host().engine.outFirst.load() / 2, [](int i) { host().engine.outFirst.store(i * 2); host().saveSetup(); }); };
    clickBtn.dynText = [] { int c = host().engine.clickFirst.load(); return c < 0 ? juce::String("with the music") : "outputs " + juce::String(c + 1) + " + " + juce::String(c + 2) + " (only you hear it)"; };
    clickBtn.onClick = [this] { juce::StringArray a; a.add("with the music"); int n = juce::jmax(2, host().numOutputs()); for (int k = 0; k + 1 < n; k += 2) a.add("outputs " + juce::String(k + 1) + " + " + juce::String(k + 2));
        int c = host().engine.clickFirst.load(); menuFor(clickBtn, a, c < 0 ? 0 : c / 2 + 1, [](int i) { host().engine.clickFirst.store(i == 0 ? -1 : (i - 1) * 2); host().saveSetup(); }); };
    toneBtn.onClick = [] { host().testTone.store(48000); };
    themeBtn.dynText = [] { return Theme::lightMode() ? juce::String("LIGHT") : juce::String("DARK"); };
    themeBtn.onClick = [] { host().theme = host().theme ? 0 : 1; Theme::lightMode() = host().theme == 1; host().saveSetup(); if (auto* top = juce::TopLevelWindow::getActiveTopLevelWindow()) top->repaint(); if (Ui::get()->refresh) Ui::get()->refresh(); };
    scaleBtn.dynText = [] { return juce::String((int) std::lround(host().uiScale * 100)) + " %"; };
    scaleBtn.onClick = [this] { juce::StringArray a { "85 %", "100 %", "115 %", "130 %" }; const float S[4] = { 0.85f, 1.0f, 1.15f, 1.3f };
        int cur = 1; for (int i = 0; i < 4; ++i) if (std::abs(host().uiScale - S[i]) < 0.01f) cur = i;
        menuFor(scaleBtn, a, cur, [](int i) { const float S2[4] = { 0.85f, 1.0f, 1.15f, 1.3f }; host().uiScale = S2[i]; juce::Desktop::getInstance().setGlobalScaleFactor(S2[i]); host().saveSetup(); }); };
    for (int a = 0; a < Host::NumActions; ++a) {
        auto* p = new Pill(""); p->fsize = 9.0f; p->radius = 6.0f; p->weight = 1;
        p->dynText = [a] { return host().learning.load() == a ? juce::String(U("press the pedal / key …")) : Host::pedalText(host().pedal[a]); };
        p->lit = [a] { return host().learning.load() == a; }; p->litColour = [] { return Theme::amber(); };
        p->onClick = [a] { if (host().learning.load() == a) { host().learning.store(-1); host().pedal[a] = {}; host().saveSetup(); } else host().learning.store(a); };
        p->setTooltip("click, then press the pedal (MIDI note or controller). Click again while learning to clear it.");
        learn.add(p); addAndMakeVisible(p);
    }
    refreshDevices();
    startTimerHz(20);
}

void SettingsPanel::refreshDevices() {
    inMeters.clear();
    int n = juce::jmin(8, host().numInputs());
    for (int i = 0; i < n; ++i) { auto m = std::make_unique<Meter>([i] { return host().engine.inPeak[i].load(); }, false); addAndMakeVisible(*m); inMeters.push_back(std::move(m)); }
    resized();
}

void SettingsPanel::layout() {
    auto b = body();
    auto left = b.removeFromLeft(b.getWidth() / 2 - 10); b.removeFromLeft(20); auto right = b;
    audioBox = left.removeFromTop(330); left.removeFromTop(14); lookBox = left;
    pedalBox = right.removeFromTop(260); right.removeFromTop(14); keysBox = right;
    auto a = audioBox.reduced(14, 0).withTrimmedTop(32);
    auto row = [&](Pill& p) { auto r = a.removeFromTop(26); r.removeFromLeft(130); p.setBounds(r); a.removeFromTop(8); };
    row(outBtn); row(inBtn); row(rateBtn); row(bufBtn); row(outChBtn); row(clickBtn);
    auto t = a.removeFromTop(26); t.removeFromLeft(130); toneBtn.setBounds(t.removeFromLeft(110));
    a.removeFromTop(10);
    for (size_t i = 0; i < inMeters.size(); ++i) { auto r = a.removeFromTop(8); r.removeFromLeft(130); inMeters[i]->setBounds(r); a.removeFromTop(4); }
    auto p = pedalBox.reduced(14, 0).withTrimmedTop(32);
    for (auto* l : learn) { auto r = p.removeFromTop(26); r.removeFromLeft(110); l->setBounds(r); p.removeFromTop(8); }
    auto lk = lookBox.reduced(14, 0).withTrimmedTop(32);
    { auto r = lk.removeFromTop(26); r.removeFromLeft(130); themeBtn.setBounds(r.withWidth(110)); lk.removeFromTop(8); }
    { auto r = lk.removeFromTop(26); r.removeFromLeft(130); scaleBtn.setBounds(r.withWidth(110)); }
}

void SettingsPanel::paint(juce::Graphics& g) {
    Overlay::paint(g);
    for (auto* bx : { &audioBox, &pedalBox, &lookBox, &keysBox }) cardBg(g, bx->toFloat(), 12);
    label(g, "SOUND CARD", audioBox.reduced(14, 0).withHeight(30), 9.5f, 2, Theme::gold());
    auto a = audioBox.reduced(14, 0).withTrimmedTop(32);
    for (auto* s : { "OUTPUT", "INPUT", "SAMPLE RATE", "BUFFER", "MUSIC GOES TO", "METRONOME" }) { label(g, s, a.removeFromTop(26).withWidth(126), 8.0f, 2); a.removeFromTop(8); }
    label(g, "CHECK", a.removeFromTop(26).withWidth(126), 8.0f, 2); a.removeFromTop(10);
    if (!inMeters.empty()) label(g, "INPUTS", a.withHeight(12).withWidth(126), 8.0f, 2);
    else label(g, U("no inputs open: choose an INPUT to record"), a.withHeight(14), 8.0f, 0);
    if (host().audioMessage.isNotEmpty()) label(g, host().audioMessage, { audioBox.getX() + 14, audioBox.getBottom() - 20, audioBox.getWidth() - 28, 14 }, 7.5f, 1, Theme::amber());
    label(g, "PEDALS AND KEYS", pedalBox.reduced(14, 0).withHeight(30), 9.5f, 2, Theme::gold());
    auto p = pedalBox.reduced(14, 0).withTrimmedTop(32);
    for (int i = 0; i < Host::NumActions; ++i) { label(g, Host::actionName(i), p.removeFromTop(26).withWidth(106), 8.5f, 2, Theme::txt()); p.removeFromTop(8); }
    label(g, "LOOK", lookBox.reduced(14, 0).withHeight(30), 9.5f, 2, Theme::gold());
    auto lk = lookBox.reduced(14, 0).withTrimmedTop(32);
    label(g, "THEME", lk.removeFromTop(26).withWidth(126), 8.0f, 2); lk.removeFromTop(8); label(g, "SIZE", lk.removeFromTop(26).withWidth(126), 8.0f, 2);
    lk.removeFromTop(16);
    label(g, U("SCORE ") + ProjectInfo::versionString + U(" · the composing sibling of ARCO"), lk.removeFromTop(16), 8.0f, 1);
    label(g, U("your pieces: ") + Host::songsFolder().getFullPathName(), lk.removeFromTop(16), 7.5f, 0);
    label(g, "KEYS", keysBox.reduced(14, 0).withHeight(30), 9.5f, 2, Theme::gold());
    auto k = keysBox.reduced(14, 0).withTrimmedTop(32);
    const char* rows[][2] = { { "space", "play / stop  (PERFORM: GO)" }, { "R", "record" }, { "L  /  M", "loop  /  metronome" }, { "1 2 3", "arranger · score line · mixer" },
                              { "A W S E D …", "play notes · Z X octave" }, { "cmd Z / shift cmd Z", "undo / redo" }, { "cmd S", "save" }, { "cmd D / delete", "duplicate / delete clips" }, { "cmd E", "split at the playhead" } };
    for (auto& r : rows) { if (k.getHeight() < 14) break; auto line = k.removeFromTop(16); label(g, U(r[0]), line.removeFromLeft(130), 8.0f, 2, Theme::txt()); label(g, U(r[1]), line, 8.0f, 0); }
}

void SettingsPanel::timerCallback() {
    if ((int) inMeters.size() != juce::jmin(8, host().numInputs())) refreshDevices();
    for (auto& m : inMeters) m->update();
    static int k = 0; if (++k % 5 == 0) { for (auto* c : getChildren()) c->repaint(); repaint(); }
}

// ======================================================================== export
ExportPanel::ExportPanel() : Overlay("EXPORT", 620, 470) {
    o.normalize = false;
    for (auto* p : { &what, &range, &bits, &norm, &tail, &start, &bundle, &reveal, &share }) { addAndMakeVisible(*p); p->fsize = 9.0f; p->radius = 6.0f; p->weight = 1; }
    what.dynText = [this] { return o.stems ? juce::String("one file per track (stems)") : juce::String("the mix (one stereo file)"); };
    what.onClick = [this] { menuFor(what, { "The mix (one stereo file)", "One file per track (stems)" }, o.stems ? 1 : 0, [this](int i) { o.stems = i == 1; repaint(); }); };
    range.dynText = [this] { if (o.range == 0) return juce::String("the whole piece"); if (o.range == 1) return juce::String("the loop"); auto& st = P().stations;
        return "station " + juce::String(o.stationIdx + 1) + (o.stationIdx < (int) st.size() ? "  " + st[(size_t) o.stationIdx].name : juce::String()); };
    range.onClick = [this] { juce::StringArray a { "The whole piece", "The loop" }; for (int i = 0; i < (int) P().stations.size(); ++i) a.add("Station " + juce::String(i + 1) + "  " + P().stations[(size_t) i].name);
        menuFor(range, a, o.range == 2 ? 2 + o.stationIdx : o.range, [this](int i) { if (i < 2) o.range = i; else { o.range = 2; o.stationIdx = i - 2; } repaint(); }); };
    bits.dynText = [this] { return "WAV " + juce::String(o.bits) + " bit"; };
    bits.onClick = [this] { menuFor(bits, { "16 bit", "24 bit", "32 bit float" }, o.bits == 16 ? 0 : o.bits == 24 ? 1 : 2, [this](int i) { o.bits = i == 0 ? 16 : i == 1 ? 24 : 32; }); };
    norm.lit = [this] { return o.normalize; }; norm.onClick = [this] { o.normalize = !o.normalize; };
    tail.dynText = [this] { return "tail " + juce::String(o.tail, 0) + " s  (reverbs ring out)"; };
    tail.onClick = [this] { menuFor(tail, { "0 s", "2 s", "5 s", "10 s" }, o.tail < 1 ? 0 : o.tail < 3 ? 1 : o.tail < 6 ? 2 : 3, [this](int i) { const double T[4] = { 0, 2, 5, 10 }; o.tail = T[i]; }); };
    start.bg = [] { return Theme::blue(); }; start.fsize = 11.0f; start.weight = 2;
    start.onClick = [this] { go(); };
    bundle.onClick = [this] {
        auto zip = P().folder.getParentDirectory().getChildFile(P().name + ".zip");
        host().saveProject();
        if (host().exportProjectBundle(zip)) { last = zip; result = zip.getFileName() + U(" · the whole piece with its sounds: open it on another Mac or iPad"); }
        else result = "could not make the zip";
        repaint(); };
    reveal.onClick = [this] { if (last.exists()) last.revealToUser(); };
    share.onClick = [this] { if (!last.exists()) return;
        chooser = std::make_unique<juce::FileChooser>("Save a copy", last);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto r = fc.getResult(); if (r != juce::File() && r != last) { last.copyFileTo(r); result = "saved: " + r.getFileName(); repaint(); } }); };
    reveal.setVisible(false); share.setVisible(false);
    startTimerHz(15);
}

void ExportPanel::layout() {
    auto b = body();
    auto row = [&](Pill& p) { auto r = b.removeFromTop(28); r.removeFromLeft(140); p.setBounds(r.withWidth(juce::jmin(300, r.getWidth()))); b.removeFromTop(10); };
    row(what); row(range); row(bits); row(tail);
    { auto r = b.removeFromTop(28); r.removeFromLeft(140); norm.setBounds(r.withWidth(110)); b.removeFromTop(18); }
    start.setBounds(b.removeFromTop(36).withWidth(200).withX(b.getX() + 140)); b.removeFromTop(48);
    auto r = b.removeFromTop(28); reveal.setBounds(r.removeFromLeft(130)); r.removeFromLeft(8); share.setBounds(r.removeFromLeft(130));
    bundle.setBounds(body().removeFromBottom(28).withWidth(260));
}

void ExportPanel::paint(juce::Graphics& g) {
    Overlay::paint(g);
    auto b = body();
    for (auto* s : { "WHAT", "WHICH PART", "FORMAT", "AT THE END", "LOUDNESS" }) { label(g, s, b.removeFromTop(28).withWidth(130), 8.5f, 2); b.removeFromTop(10); }
    b.removeFromTop(8 + 36 + 6);
    float pr = host().exportProgress.load();
    if (pr >= 0) { auto bar = b.removeFromTop(8).withTrimmedLeft(140).withWidth(300).toFloat(); g.setColour(Theme::card2()); g.fillRoundedRectangle(bar, 4); g.setColour(Theme::blue()); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * pr), 4); }
    else label(g, result.isNotEmpty() ? result : U("files go into the piece's folder, in Exports"), b.removeFromTop(30), 9.0f, 1, result.isNotEmpty() ? Theme::green() : Theme::sub());
}

void ExportPanel::timerCallback() {
    bool busy = host().exportProgress.load() >= 0;
    start.enabledLook = !busy;
    bool have = last.exists() && !busy;
    reveal.setVisible(have); share.setVisible(have);
   #if JUCE_IOS
    reveal.setVisible(false);
   #endif
    repaint();
}

void ExportPanel::go() {
    if (host().exportProgress.load() >= 0) return;
    auto dir = P().folder.getChildFile("Exports"); dir.createDirectory();
    juce::String base = P().name + (o.range == 1 ? " (loop)" : o.range == 2 ? " (station " + juce::String(o.stationIdx + 1) + ")" : juce::String());
    auto f = dir.getChildFile(juce::File::createLegalFileName(base) + ".wav").getNonexistentSibling();
    host().exportStems = 0;
    result = U("exporting …");
    host().exportAudio(o, f, [this, f](bool ok, juce::String msg) { result = msg; if (ok) last = f.exists() ? f : f.getParentDirectory(); repaint(); });
}

} // namespace score
