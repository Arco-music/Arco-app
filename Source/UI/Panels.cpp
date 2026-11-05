#include "Panels.h"
#include "MainView.h"
#if JUCE_IOS
 #include "../App/IOSAudio.h"
#endif

namespace arco {

static void lab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.85f);
}

// ------------------------------------------------------------------ AUDIO
AudioBox::AudioBox(Canvas& c, juce::LookAndFeel* laf) : cv(c) {
    for (auto* cb : { &out, &in, &rate, &buf }) { addAndMakeVisible(cb); cb->setLookAndFeel(laf); }
    out.setBounds(110, 0, 320, 26); in.setBounds(110, 40, 320, 26);
    rate.setBounds(110, 80, 130, 26); buf.setBounds(330, 80, 180, 26);
    out.onChange = in.onChange = rate.onChange = buf.onChange = [this] { if (!filling) apply(); };
    auto* test = new Pill("TEST", [] { return Theme::sel(); }, 8.5f, 6.0f); kids.add(test); addAndMakeVisible(test);
    test->setBounds(440, 0, 70, 26);
    test->onClick = [this] { cv.host.testTone.store(48000); };
    auto* rs = new Pill(U("↻ LOOK AGAIN"), [] { return Theme::btn(); }, 7.5f, 6.0f); kids.add(rs); addAndMakeVisible(rs);
    rs->setBounds(0, 196, 100, 22);
    rs->onClick = [this] { if (auto* t = cv.host.devices.getCurrentDeviceTypeObject()) t->scanForDevices(); cv.host.scanMidi(); refresh(); };
    cv.host.devices.addChangeListener(this);
   #if JUCE_IOS
    // iPad: iOS has one sound device; the choice is the microphone (built-in, cable, USB, Bluetooth) and where the sound goes
    out.setVisible(false);
    auto* pick = new Pill(U("CHOOSE OUTPUT ▸"), [] { return Theme::blue(); }, 7.5f, 6.0f); kids.add(pick); addAndMakeVisible(pick);
    pick->setBounds(110, 0, 120, 26);
    pick->onClick = [] { ios::showOutputPicker(); };
    auto* spk = new Pill(U("SPEAKER"), [] { return ios::speakerForced() ? Theme::amber() : Theme::btn(); }, 7.5f, 6.0f); kids.add(spk); addAndMakeVisible(spk);
    spk->setBounds(236, 0, 70, 26);
    spk->onClick = [this] { ios::forceSpeaker(! ios::speakerForced()); cv.host.saveSetup(); repaint(); };
    in.onChange = [this] { if (filling) return; int i = in.getSelectedItemIndex(); if (i >= 0 && i < iosIds.size()) { ios::chooseInput(iosIds[i]); cv.host.saveSetup(); } };
   #endif
    refresh();
    startTimerHz(10);
}
AudioBox::~AudioBox() { cv.host.devices.removeChangeListener(this); for (auto* cb : { &out, &in, &rate, &buf }) cb->setLookAndFeel(nullptr); }

void AudioBox::refresh() {
    filling = true;
    auto& dm = cv.host.devices;
    auto* type = dm.getCurrentDeviceTypeObject();
    if (type == nullptr && !dm.getAvailableDeviceTypes().isEmpty()) type = dm.getAvailableDeviceTypes()[0];
    auto setup = dm.getAudioDeviceSetup();
    outNames = type ? type->getDeviceNames(false) : juce::StringArray();
    inNames = type ? type->getDeviceNames(true) : juce::StringArray();
    out.clear(juce::dontSendNotification); in.clear(juce::dontSendNotification);
    out.addItemList(outNames, 1);
    in.addItem("no input", 1); in.addItemList(inNames, 2);
    out.setSelectedItemIndex(outNames.indexOf(setup.outputDeviceName), juce::dontSendNotification);
    if (out.getSelectedItemIndex() < 0) out.setText("choose an output", juce::dontSendNotification);
    in.setSelectedItemIndex(setup.inputDeviceName.isEmpty() ? 0 : inNames.indexOf(setup.inputDeviceName) + 1, juce::dontSendNotification);
   #if JUCE_IOS
    in.clear(juce::dontSendNotification); iosIds.clear();
    for (auto& p : ios::inputs()) { iosIds.add(p.id); in.addItem(p.name, iosIds.size()); }
    in.setSelectedItemIndex(juce::jmax(0, iosIds.indexOf(ios::chosenInput())), juce::dontSendNotification);
    lastRoute = ios::currentInputName() + ios::currentOutputName();
   #endif
    rate.clear(juce::dontSendNotification); buf.clear(juce::dontSendNotification); rates.clear(); bufs.clear();
    if (auto* d = dm.getCurrentAudioDevice()) {
        for (auto r : d->getAvailableSampleRates()) { rates.add(r); rate.addItem(juce::String((int) r) + " Hz", rates.size()); }
        for (auto b : d->getAvailableBufferSizes()) {
            bufs.add(b); buf.addItem(juce::String(b) + " samples  (" + juce::String(b * 1000.0 / d->getCurrentSampleRate(), 1) + " ms)", bufs.size()); }
        rate.setSelectedItemIndex(rates.indexOf(d->getCurrentSampleRate()), juce::dontSendNotification);
        buf.setSelectedItemIndex(bufs.indexOf(d->getCurrentBufferSizeSamples()), juce::dontSendNotification);
    }
    filling = false;
    repaint();
}

void AudioBox::apply() {
    int oi = out.getSelectedItemIndex(), ii = in.getSelectedItemIndex();
    if (oi < 0) return;
    double r = rate.getSelectedItemIndex() >= 0 ? rates[rate.getSelectedItemIndex()] : 0.0;
    int b = buf.getSelectedItemIndex() >= 0 ? bufs[buf.getSelectedItemIndex()] : 0;
    auto prev = cv.host.devices.getAudioDeviceSetup();
    // a new device: let it choose its own rate / buffer
    if (outNames[oi] != prev.outputDeviceName || (ii > 0 ? inNames[ii - 1] : juce::String()) != prev.inputDeviceName) { r = 0; b = 0; }
    cv.host.openAudio(outNames[oi], ii > 0 ? inNames[ii - 1] : juce::String(), r, b);
    refresh();
}

void AudioBox::paint(juce::Graphics& g) {
    auto L = [&](const juce::String& t, int x, int y, int w, int h, float fs, juce::Colour c, int wt = 0) {
        g.setColour(c); g.setFont(Theme::font(fs, wt)); g.drawFittedText(t, { x, y, w, h }, juce::Justification::centredLeft, 4, 0.9f); };
    L("OUTPUT", 0, 0, 100, 26, 8.5f, Theme::txt(), 2);
   #if JUCE_IOS
    if (lastRoute != ios::currentInputName() + ios::currentOutputName()) juce::MessageManager::callAsync([sp = juce::Component::SafePointer<AudioBox>(this)] { if (sp) sp->refresh(); });
    L(U("now: ") + ios::currentOutputName() + (ios::outputIsBluetooth() ? U("  ·  Bluetooth delays the sound a little") : juce::String()), 314, 0, 260, 26, 7.5f, ios::outputIsBluetooth() ? Theme::amber() : Theme::sub(), 1);
   #endif
    L("INPUT", 0, 40, 100, 26, 8.5f, Theme::txt(), 2);
    L("SAMPLE RATE", 0, 80, 100, 26, 8.5f, Theme::txt(), 2);
    L("BUFFER", 260, 80, 70, 26, 8.5f, Theme::txt(), 2);
    // input level
    float lv = juce::jmin(1.0f, cv.engine().peak("live_in1") * 2.0f);
    g.setColour(Theme::card2()); g.fillRoundedRectangle(440, 46, 70, 14, 4);
    g.setColour(lv > 0.9f ? Theme::red() : Theme::green()); g.fillRoundedRectangle(440, 46, 70 * lv, 14, 4);
    auto* d = cv.host.devices.getCurrentAudioDevice();
    juce::String st; juce::Colour sc = Theme::green();
    if (d != nullptr) {
        auto s = cv.host.devices.getAudioDeviceSetup();
        st = U("✓ playing: ") + s.outputDeviceName + (s.inputDeviceName.isNotEmpty() ? U("  ·  listening: ") + s.inputDeviceName : juce::String("  ·  no input"))
           + "\n" + juce::String((int) d->getCurrentSampleRate()) + " Hz  ·  " + juce::String(d->getCurrentBufferSizeSamples()) + " samples  ·  "
           + juce::String(d->getActiveInputChannels().countNumberOfSetBits()) + " inputs, " + juce::String(d->getActiveOutputChannels().countNumberOfSetBits()) + " outputs  ·  delay about "
           + juce::String(juce::roundToInt((d->getInputLatencyInSamples() + d->getOutputLatencyInSamples() + 2 * d->getCurrentBufferSizeSamples()) * 1000.0 / d->getCurrentSampleRate())) + " ms";
    } else { st = "no sound card open - choose an OUTPUT above"; sc = Theme::amber(); }
    L(st, 0, 118, 520, 36, 8.0f, sc, 1);
    if (cv.host.audioMessage.isNotEmpty()) L(cv.host.audioMessage, 0, 154, 520, 38, 8.0f, Theme::amber(), 1);
    juce::StringArray midi; for (auto& m : juce::MidiInput::getAvailableDevices()) midi.add(m.name);
    L("MIDI: " + (midi.isEmpty() ? juce::String("nothing plugged in") : midi.joinIntoString(", ")), 110, 196, 410, 22, 7.5f, Theme::sub());
   #if JUCE_IOS
    L(U("iPad: INPUT chooses the microphone (built-in, a cable or USB interface, a Bluetooth headset). The round button sends the sound to Bluetooth / AirPlay / a cable; SPEAKER forces the iPad's own. "
        "Bluetooth has a big delay - fine for listening, not for playing along."), 0, 226, 520, 40, 7.5f, Theme::sub());
    return;
   #endif
    L(U("Headphones on a Mac: choose them (or your sound card) as OUTPUT here. TEST plays a soft A on outputs 1-2 and in the headphones. "
        "Bluetooth headphones have a big delay - fine for listening, not for playing along."), 0, 226, 520, 40, 7.5f, Theme::sub());
}

// ================================================================== LOOK & EFFECTS
LookPanel::LookPanel(Canvas& c) : cv(c) {
    auto mk = [this](juce::Rectangle<int> r, const juce::String& t, ColourFn col, std::function<void()> f) {
        auto* p = new Pill(t, std::move(col), 8.5f, 7.0f); p->onClick = std::move(f); kids.add(p); addAndMakeVisible(p);
        p->setBounds(r.translated(box.getX(), box.getY())); return p; };
    mk({ 1140 - 104, 16, 88, 26 }, U("✕ CLOSE"), [] { return Theme::btn(); }, [this] { cv.host.saveSetup(); setVisible(false); });
    mk({ 24 + 150, 14, 120, 26 }, "AUDIO & INPUTS", [] { return Theme::btn(); }, [this] { setVisible(false); cv.openSetup(true); });
    mk({ 24 + 276, 14, 130, 26 }, U("LOOK & EFFECTS"), [] { return Theme::sel(); }, [] {});
    auto& h = cv.host;
    mk({ 760, 286, 104, 24 }, "DEFAULT", [] { return Theme::btn(); }, [this, &h] { h.visibleCards = defaultVisibleCards(); cv.layoutCards(); repaint(); });
    mk({ 870, 286, 70, 24 }, "ALL", [] { return Theme::btn(); }, [this, &h] { h.visibleCards.clear(); for (auto& d : cardTable()) h.visibleCards.add(d.key); cv.layoutCards(); repaint(); });
    mk({ 946, 286, 70, 24 }, "NONE", [] { return Theme::btn(); }, [this, &h] { h.visibleCards.clear(); cv.layoutCards(); repaint(); });
}

juce::Rectangle<int> LookPanel::paletteTile(int i) const {
    return { box.getX() + 24 + i * 262, box.getY() + 108, 250, 120 };
}
juce::Rectangle<int> LookPanel::effectTile(int i) const {
    return { box.getX() + 24 + (i % 7) * 156, box.getY() + 322 + (i / 7) * 72, 148, 64 };
}

void LookPanel::mouseDown(const juce::MouseEvent& e) {
    auto pt = e.getPosition();
    if (!box.contains(pt)) { cv.host.saveSetup(); setVisible(false); return; }
    auto& ct = cardTable();
    for (int i = 0; i < (int) ct.size(); ++i)
        if (effectTile(i).contains(pt)) {
            auto& v = cv.host.visibleCards; juce::String k = ct[(size_t) i].key;
            if (v.contains(k)) v.removeString(k); else v.add(k);
            cv.layoutCards(); repaint(); return;
        }
}

void LookPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.25f : 0.55f));
    g.setColour(Theme::bg()); g.fillRoundedRectangle(box.toFloat(), 16.0f);
    int X = box.getX(), Y = box.getY();
    auto L = [&](const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt = 0) { lab(g, t, { X + x, Y + y, w, (int) (fs * 2.4f) }, fs, c, wt); };
    L("EFFECTS", 24, 14, 140, 17.0f, Theme::txt(), 2);
    auto& ct = cardTable();
    int shown = 0; for (auto& d : ct) shown += cv.host.visibleCards.contains(d.key);
    L("EFFECTS ON THE MAIN WINDOW", 24, 262, 400, 11.0f, Theme::txt(), 2);
    L(juce::String(shown) + " of " + juce::String((int) ct.size()) + U(" shown  ·  tap to show or hide  ·  on the main window they are sorted into six families (TONE, MOVEMENT, SPACE, ECHOES, TEXTURE, RHYTHM)  ·  hidden effects keep working"),
      24, 290, 720, 8.0f, Theme::sub());
    int nBase = (int) cardTableBase().size();
    for (int i = 0; i < (int) ct.size(); ++i) {
        auto r = effectTile(i).toFloat(); auto& d = ct[(size_t) i];
        bool on = cv.host.visibleCards.contains(d.key);
        g.setColour(on ? Theme::card2() : Theme::card()); g.fillRoundedRectangle(r, 9.0f);
        g.setColour(on ? Theme::fx(d.key) : Theme::line()); g.drawRoundedRectangle(r.reduced(0.5f), 9.0f, on ? 2.0f : 1.0f);
        g.setColour(on ? Theme::fx(d.key) : Theme::off()); g.fillEllipse(r.getX() + 10, r.getY() + 11, 9, 9);
        lab(g, U(d.name), juce::Rectangle<int>((int) r.getX() + 24, (int) r.getY() + 5, 110, 20), 9.5f, on ? Theme::txt() : Theme::sub(), 2);
        lab(g, U(d.desc), juce::Rectangle<int>((int) r.getX() + 10, (int) r.getY() + 28, (int) r.getWidth() - 16, 30), 7.0f, Theme::sub(), 0);
        if (i >= nBase) { g.setColour(Theme::amber()); g.setFont(Theme::font(6.5f, 2)); g.drawText("NEW", r.withTrimmedRight(8).withHeight(22), juce::Justification::centredRight); }
    }
}

void SetupPanel::startUpdateCheck() { updateText = U("looking …"); repaint(); updater.check(); }

void SetupPanel::askKey(const juce::String& why) {
    auto* w = new juce::AlertWindow("Updates from your GitHub",
        (why.isNotEmpty() ? why + "\n\n" : juce::String()) +
        U("ARCO is private, so it needs a key to see new versions. Make it once (2 minutes):\n\n"
          "1. github.com → your photo → Settings → Developer settings → Personal access tokens → Fine-grained tokens → Generate new token\n"
          "2. Name: ARCO updates · Expiration: 1 year · Repository access: Only select repositories → ARCO\n"
          "3. Permissions → Repository permissions → Contents: Read-only → Generate token\n"
          "4. Copy the key (starts with github_pat_) and paste it here.\n\nThe key stays on this Mac. It can only read ARCO."),
        juce::MessageBoxIconType::NoIcon);
    w->addTextEditor("key", updater.key(), "GitHub key");
    w->addButton("Save key", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Open the download page", 2);
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
        if (r == 1) { auto k = w->getTextEditorContents("key").trim(); if (k.isNotEmpty()) { updater.setKey(k); startUpdateCheck(); } }
        if (r == 2) juce::URL(Updater::releasesPage()).launchInDefaultBrowser();
    }), true);
}

void SetupPanel::styleLaf() {
    laf = std::make_unique<ArcoLaf>(juce::LookAndFeel_V4::ColourScheme(
        Theme::card(), Theme::card(), Theme::card2(), Theme::line(), Theme::txt(), Theme::btn(), juce::Colours::white, Theme::sel(), Theme::txt()));
    laf->setDefaultSansSerifTypeface(Theme::face(0));
    laf->setColour(juce::ComboBox::backgroundColourId, Theme::card2());
    laf->setColour(juce::ComboBox::textColourId, Theme::txt());
    laf->setColour(juce::ComboBox::outlineColourId, Theme::card2());
    laf->setColour(juce::Label::textColourId, Theme::txt());
    laf->setColour(juce::ListBox::backgroundColourId, Theme::card2());
    laf->setColour(juce::TextButton::buttonColourId, Theme::btn());
    laf->setColour(juce::ToggleButton::textColourId, Theme::txt());
    laf->setColour(juce::ToggleButton::tickColourId, Theme::green());
    laf->setColour(juce::PopupMenu::backgroundColourId, Theme::card2());
    laf->setColour(juce::PopupMenu::textColourId, Theme::txt());
    laf->setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::sel());
}

SetupPanel::SetupPanel(Canvas& c) : cv(c) {
    auto& h = cv.host; auto& m = cv.model();
    styleLaf();
    audio = std::make_unique<AudioBox>(cv, laf.get());
    addAndMakeVisible(*audio);
    audio->setBounds(box.getX() + 24, box.getY() + 98, 520, 300);
   #if ! JUCE_IOS
    auto* up = add<Pill>({ 1140 - 220, 16, 108, 26 }, U("⟳ UPDATE"), [] { return Theme::c(Theme::rgb(0.20f, 0.36f, 0.60f)); }, 8.5f, 7.0f);
    up->onClick = [this] { if (updater.key().isEmpty()) askKey({}); else startUpdateCheck(); };
    updater.onProgress = [this](const juce::String& m) { updateText = m; repaint(); };
    updater.onChecked = [this](const juce::String& m, int b, const juce::String& notes) {
        if (m == "no key") { askKey({}); return; }
        if (m == "up to date") { updateText = U("✓ you have the newest ARCO"); repaint(); return; }
        if (m == "new") {
            updateText = "build " + juce::String(b) + " is ready";
            auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::InfoIcon).withTitle("A new ARCO is ready")
                .withMessage("Build " + juce::String(b) + " (you have " + juce::String(Updater::build()) + ").\n\n" + notes.substring(0, 600)
                             + "\n\nInstall now? ARCO restarts by itself. Save your song first if you want to keep the loops.")
                .withButton("Install").withButton("Later");
            juce::AlertWindow::showAsync(opts, [this](int r) { if (r == 1) { updateText = U("downloading …"); updater.install(); } });
            repaint(); return; }
        updateText = m; repaint();
        if (m.contains("key")) askKey(m);
    };
   #endif
    auto* rs = add<Pill>({ 24, 398, 200, 26 }, U("↻ Restart the sound"), [] { return Theme::btn(); }, 8.5f, 8.0f);
    rs->onClick = [this] { cv.host.restartSound(); repaint(); };
    rs->setTooltip("if the sound stopped (another app, a call): the sound card is opened again");
    // the sections, as a list on the left
    static const char* SN[NSEC] = { "Sound card", "Live inputs", "Headphones", "Speakers", "Recording & tuner", "iPhones", "Pedals & keys" };
    for (int s = 0; s < NSEC; ++s) {
        auto* p = add<Pill>({ 22, 76 + s * 52, 206, 42 }, U(SN[s]), [] { return Theme::btn(); }, 10.0f, 10.0f);
        p->onFn = [this, s] { return section == s; }; p->tint = true;
        p->onClick = [this, s] { showSection(s); };
        p->setComponentID("setup:sec" + juce::String(s)); nav.push_back(p);
    }
    auto* lk = add<Pill>({ 22, 76 + NSEC * 52 + 16, 206, 42 }, U("Effects on the main screen ▸"), [] { return Theme::btn(); }, 9.0f, 10.0f);
    lk->onClick = [this] { cv.host.saveSetup(); cv.openSetup(false); cv.openLook(true); };
    lk->setComponentID("setup:look");
    auto* close = add<Pill>({ 1140 - 104, 16, 88, 26 }, U("✕ CLOSE"), [] { return Theme::btn(); }, 9.0f, 7.0f);
    close->onClick = [this] { cv.host.saveSetup(); cv.openSetup(false); };
    // ---- live inputs
    auto vb = [&](juce::Rectangle<int> r, std::function<double()> g, std::function<void(double)> s, double lo, double hi, ColourFn col) {
        auto* v = add<ValueBox>(r, std::move(g), std::move(s), lo, hi, 0, std::move(col)); v->fsize = 12.0f; return v; };
    vb({ 140, 482, 44, 24 }, [&h] { return (double) h.nLive; }, [&h](double v) { h.nLive = (int) v; h.applyRouting(); }, 1, 4, [] { return Theme::green(); });
    for (int j = 0; j < 4; ++j)
        vb({ 120 + (j % 2) * 220, 522 + (j / 2) * 34, 44, 24 }, [&h, j] { return (double) h.inputCh[j]; },
           [&h, j](double v) { h.inputCh[j] = (int) v; h.applyRouting(); }, 1, 64, [j] { return Theme::strip(j); });
    // ---- headphones
    addAndMakeVisible(phones); phones.setLookAndFeel(laf.get());
    juce::StringArray pairs { "3-4", "5-6", "7-8", "9-10", "11-12", "13-14", "15-16", "17-18", "19-20", "21-22", "23-24", "25-26", "27-28", "29-30", "31-32", "1-2 (same as speakers)" };
    phones.addItemList(pairs, 1);
    phones.setSelectedItemIndex(h.phonesPair, juce::dontSendNotification);
    phones.onChange = [this] { cv.host.phonesPair = phones.getSelectedItemIndex(); cv.host.applyRouting(); repaint(); };
    phones.setBounds(box.getX() + 680, box.getY() + 102, 180, 24);
    const char* PH[5][2] = { { "ph_vol", "VOLUME" }, { "ph_main", "MAIN MIX" }, { "ph_cue", "CUE" }, { "ph_click", "CLICK" }, { "ph_pvw", "PREVIEW" } };
    for (int i = 0; i < 5; ++i) {
        std::string id = PH[i][0];
        add<Knob>({ 600 + i * 74, 142, 36, 36 }, [&m, id] { return m.get(id); }, [&m, id](double v) { m.set(id, v); }, 0, 100, m.def(id)->def,
                  [i] { return i == 2 || i == 4 ? Theme::amber() : (i == 3 ? Theme::red() : Theme::txt()); });
    }
    auto* clk = add<Pill>({ 600, 206, 22, 20 }, "", [&m] { return m.get("clk_room") > 0.5 ? Theme::red() : Theme::card2(); }, 8.0f, 5.0f);
    clk->onClick = [&m] { m.set("clk_room", m.get("clk_room") > 0.5 ? 0 : 1); };
    // ---- speakers
    vb({ 690, 290, 46, 24 }, [&m] { return m.get("spk_n"); }, [&m, &h](double v) { m.set("spk_n", v); h.applySpeakers(); }, 2, 32, [] { return Theme::green(); });
    const char* LAY[2] = { "RING", "FRONT ARC" };
    for (int i = 0; i < 2; ++i) {
        auto* p = add<Pill>({ 760 + i * 92, 290, 86, 24 }, LAY[i], [&h, i] { return h.speakerLayout == i ? Theme::sel() : Theme::btn(); }, 8.0f, 6.0f);
        p->onClick = [&h, i] { h.speakerLayout = i; h.applySpeakers(); };
    }
    auto* sp3 = add<Pill>({ 944, 290, 150, 24 }, U("OUTPUTS & SPACE ▸"), [&h] { return h.speakerLayout == 2 ? Theme::sel() : Theme::violet(); }, 8.0f, 6.0f);
    sp3->onClick = [this] { cv.host.saveSetup(); cv.openSetup(false); cv.openSpatial(true); };
    add<Knob>({ 600, 330, 36, 36 }, [&m] { return m.get("main_w"); }, [&m](double v) { m.set("main_w", v); }, 0, 100, 0, [] { return Theme::txt(); });
    add<Knob>({ 674, 330, 36, 36 }, [&m] { return m.get("mv_blur"); }, [&m](double v) { m.set("mv_blur", v); }, 0, 100, 30, [] { return Theme::fx("orb"); });
    add<Knob>({ 748, 330, 36, 36 }, [&m] { return m.get("room_vol"); }, [&m](double v) { m.set("room_vol", v); }, 0, 100, 100, [] { return Theme::txt(); });
    // ---- timing, practice, tuner, guard
    vb({ 690, 482, 50, 24 }, [&m] { return m.get("lat"); }, [&m](double v) { m.set("lat", v); }, 0, 250, [] { return Theme::amber(); });
    add<Knob>({ 600, 530, 36, 36 }, [&m] { return m.get("prac"); }, [&m](double v) { m.set("prac", v); }, 50, 100, 100, [] { return Theme::amber(); });
    vb({ 760, 538, 54, 24 }, [&m] { return m.get("a4"); }, [&m](double v) { m.set("a4", v); }, 415, 466, [] { return Theme::amber(); });
    auto* gd = add<Pill>({ 900, 540, 22, 20 }, "", [&m] { return m.get("guard") > 0.5 ? Theme::green() : Theme::card2(); }, 8.0f, 5.0f);
    gd->onClick = [&m] { m.set("guard", m.get("guard") > 0.5 ? 0 : 1); };
    // ---- phones: microphones and remotes
    phoneList = std::make_unique<PhoneList>(cv);
    addAndMakeVisible(*phoneList); phoneList->setBounds(box.getX() + 24, box.getY() + 676, 540, 104);
   #if JUCE_IOS
    auto* mfm = add<Pill>({ 330, 784, 230, 22 }, U("USE THIS iPAD AS A PHONE ▸"), [] { return Theme::violet(); }, 7.5f, 6.0f);
    mfm->onClick = [this] { cv.host.saveSetup(); cv.openSetup(false); cv.openMic(true); };
   #endif
    addAndMakeVisible(wifi);
    wifi.setBounds(box.getX() + 24, box.getY() + 784, 170, 22);
    wifi.setTextToShowWhenEmpty(U("add a phone by address"), Theme::sub());
    wifi.setFont(Theme::font(8.5f, 1));
    wifi.setIndents(8, 4);
    wifi.onReturnKey = [this, &h] { h.phones.addAddress(wifi.getText().trim()); wifi.clear(); h.saveSetup(); };
    wifi.setBounds(box.getX() + 24, box.getY() + 760, 220, 26);
    // ---- pedals & keys: learn a MIDI pedal for each action
    for (int a = 0; a < AudioHost::NumActions; ++a) {
        auto* p = add<Pill>({ 600 + a * 130, 716, 120, 22 }, "LEARN", [&h, a] { return h.learning.load() == a ? Theme::amber() : Theme::btn(); }, 7.5f, 5.0f);
        p->dynText = [&h, a] { return h.learning.load() == a ? juce::String(U("press the pedal …")) : juce::String("LEARN"); };
        p->onClick = [&h, a] { h.learning.store(h.learning.load() == a ? -1 : a); };
    }
    classify();
    showSection(0);
    startTimerHz(10);
}

juce::Rectangle<int> SetupPanel::secR(int s) const {
    static const juce::Rectangle<int> R[NSEC] = { { 16, 44, 570, 384 }, { 16, 428, 570, 200 }, { 586, 44, 546, 196 }, { 586, 240, 546, 186 },
                                                   { 586, 428, 546, 200 }, { 16, 628, 570, 182 }, { 586, 628, 546, 182 } };
    return R[juce::jlimit(0, NSEC - 1, s)].translated(box.getX(), box.getY());
}
juce::AffineTransform SetupPanel::secT(int s) const {
    auto r = secR(s), c = contentR();
    return juce::AffineTransform::translation((float) -r.getX(), (float) -r.getY()).scaled(1.25f).translated((float) c.getX() + 6, (float) c.getY());
}
void SetupPanel::classify() {
    placed.clear();
    std::vector<juce::Component*> all; for (auto* k : kids) all.push_back(k);
    for (juce::Component* k : { (juce::Component*) audio.get(), (juce::Component*) &phones, (juce::Component*) &wifi, (juce::Component*) phoneList.get() }) if (k) all.push_back(k);
    for (auto* c : all) {
        if (std::find(nav.begin(), nav.end(), c) != nav.end() || c->getComponentID().startsWith("setup:")) continue;
        auto ctr = c->getBounds().getCentre(); int sec = -1;
        for (int s = 0; s < NSEC; ++s) if (secR(s).contains(ctr)) { sec = s; break; }
        if (sec >= 0) placed.push_back({ c, sec });
    }
}
void SetupPanel::showSection(int s) {
    section = juce::jlimit(0, NSEC - 1, s);
    for (auto& p : placed) if (auto* c = p.c.getComponent()) {
        bool on = p.sec == section;
        c->setVisible(on);
        if (on) c->setTransform(secT(section));
    }
    repaint();
}

// ================================================================== the phones
void PhoneList::paint(juce::Graphics& g) {
    hits.clear();
    auto& hub = cv.host.phones;
    int y = 0;
    auto text = [&](const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft) {
        g.setColour(Theme::ink(c)); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 1, 0.8f); };
    int shown = 0;
    for (int i = 0; i < hub.conns.size() && shown < 4; ++i) {
        auto* c = hub.conns[i];
        if (c->busyElsewhere.load() && !c->connected()) continue;
        bool on = c->connected();
        g.setColour(on ? Theme::green() : Theme::amber()); g.fillEllipse(2.0f, (float) y + 8, 8, 8);
        auto nm = c->name().isNotEmpty() ? c->name() : (c->mode == PhoneConn::Usb ? juce::String("phone on the cable") : c->host);
        text(nm, { 16, y + 1, 150, 22 }, 9.0f, Theme::txt(), 2);
        juce::String st = on ? (juce::String(c->mode == PhoneConn::Usb ? "cable" : "Wi-Fi") + (c->live.load() > 0 ? juce::String::fromUTF8(" · ") + juce::String(c->delayMs.load(), 0) + " ms" : juce::String::fromUTF8(" · remote")))
                             : c->statusText();
        if (c->legacy()) st << " (update the phone to ARCO 1.2)";
        text(st, { 168, y + 1, 150, 22 }, 7.5f, Theme::sub());
        float lv = juce::jmin(1.0f, c->level.load() * 2.0f);
        g.setColour(Theme::card2()); g.fillRoundedRectangle(322.0f, (float) y + 9, 54.0f, 6.0f, 3.0f);
        g.setColour(Theme::green()); g.fillRoundedRectangle(322.0f, (float) y + 9, 54.0f * lv, 6.0f, 3.0f);
        for (int l = 0; l <= 4; ++l) {
            juce::Rectangle<int> r(386 + l * 31, y + 1, 28, 22);
            bool sel = c->live.load() == l;
            auto col = sel ? (l == 0 ? Theme::sel() : Theme::strip(l - 1)) : Theme::btn();
            g.setColour(col); g.fillRoundedRectangle(r.toFloat(), 6.0f);
            text(l == 0 ? juce::String::fromUTF8("–") : "L" + juce::String(l), r, 8.0f, Theme::textOn(col), 2, juce::Justification::centred);
            hits.push_back({ r, i, l });
        }
        y += 26; ++shown;
    }
    if (shown == 0) {
        text(juce::String::fromUTF8("No phone yet. Open ARCO on an iPhone on the same Wi-Fi (or plug it in) - it appears here by itself."), { 0, 2, getWidth(), 20 }, 8.0f, Theme::sub());
        text(juce::String::fromUTF8("Then choose L1-L4: that phone's microphone plays as LIVE 1-4. Every phone is also a remote for the whole station."), { 0, 24, getWidth(), 20 }, 8.0f, Theme::sub());
    }
}
void PhoneList::mouseUp(const juce::MouseEvent& e) {
    for (auto& h : hits) if (h.r.contains(e.getPosition()) && h.conn < cv.host.phones.conns.size()) {
        cv.host.phones.setLive(cv.host.phones.conns[h.conn], h.live); cv.host.saveSetup(); repaint(); return; }
}

SetupPanel::~SetupPanel() { audio = nullptr; phones.setLookAndFeel(nullptr); }

void SetupPanel::visibilityChanged() {
    wifi.setColour(juce::TextEditor::backgroundColourId, Theme::card2()); wifi.setColour(juce::TextEditor::textColourId, Theme::txt());
    wifi.setColour(juce::TextEditor::outlineColourId, Theme::card2()); wifi.setColour(juce::TextEditor::focusedOutlineColourId, Theme::green());
    wifi.applyColourToAllText(Theme::txt());
    if (isVisible()) { phones.setLookAndFeel(nullptr); audio = nullptr; styleLaf(); audio = std::make_unique<AudioBox>(cv, laf.get()); addAndMakeVisible(*audio); audio->setBounds(box.getX() + 24, box.getY() + 98, 520, 300); phones.setLookAndFeel(laf.get()); phones.setSelectedItemIndex(cv.host.phonesPair, juce::dontSendNotification);
                       classify(); showSection(section); }
}

void SetupPanel::mouseDown(const juce::MouseEvent& e) {
    if (!box.contains(e.getPosition())) { cv.host.saveSetup(); cv.openSetup(false); }
}

void SetupPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(Theme::lightMode() ? 0.25f : 0.55f));
    auto b = box.toFloat();
    g.setColour(Theme::bg()); g.fillRoundedRectangle(b, 16.0f);
    int X = box.getX(), Y = box.getY();
    auto L = [&](const juce::String& t, int x, int y, int w, float fs, juce::Colour c, int wt = 0) { lab(g, t, { X + x, Y + y, w, (int) (fs * 2.4f) }, fs, c, wt); };
    auto head = [&](const juce::String& t, const juce::String& s, int x, int y, int w) {
        L(t, x, y, w, 11.0f, Theme::txt(), 2); L(s, x, y + 18, w - 20, 8.0f, Theme::sub()); };
    L("SETUP", 24, 18, 200, 17.0f, Theme::txt(), 2);
    if (updateText.isNotEmpty()) L(updateText, 720, 26, 300, 7.5f, Theme::amber(), 1);
    // the list's panel and the content's panel
    auto navP = juce::Rectangle<float>((float) X + 10, (float) Y + 62, 230.0f, (float) box.getHeight() - 80);
    auto conP = contentR().toFloat().expanded(8.0f, 6.0f);
    if (Theme::softUI()) { softWellLite(g, navP, 22.0f); softRaised(g, conP, 22.0f, 6.0f, Theme::card()); }
    else { g.setColour(Theme::card2()); g.fillRoundedRectangle(navP, 14.0f); g.setColour(Theme::card()); g.fillRoundedRectangle(conP, 14.0f); }
    L("ARCO " + juce::String(ProjectInfo::versionString) + (Updater::build() > 0 ? U(" · build ") + juce::String(Updater::build()) : juce::String()), 30, 812 - 58, 200, 7.5f, Theme::sub(), 0);
    L(U("remembered on this computer"), 30, 812 - 42, 200, 7.0f, Theme::sub(), 0);
    g.saveState();
    g.reduceClipRegion(contentR().expanded(4));
    g.addTransform(secT(section));
    { auto sr = secR(section); g.reduceClipRegion(sr); }
    head("AUDIO", "your sound card: where the sound goes, where it comes from", 24, 58, 540);
    head("LIVE INPUTS", "how many instruments / microphones, and on which input of your sound card", 24, 440, 540);
    L("LIVE INPUTS", 24, 484, 110, 8.5f, Theme::txt(), 2);
    for (int j = 0; j < 4; ++j) L("LIVE " + juce::String(j + 1) + " = input", 24 + (j % 2) * 220, 525 + (j / 2) * 34, 100, 8.5f, Theme::txt());
    L("LIVE 1 is the one LISTEN, TUNE, HALO and SHADOW follow.", 24, 600, 500, 8.0f, Theme::sub());
    head("HEADPHONES", "the click, CUE and PREVIEW are only here; the main mix too, as much as you want", 600, 58, 520);
    L("OUTPUTS", 600, 104, 80, 8.5f, Theme::txt(), 2);
    if (cv.host.phonesShared) L(U("only 2 outputs: the click and CUE play from 1-2 too"), 870, 101, 250, 8.0f, Theme::amber());
    const char* PH[5] = { "VOLUME", "MAIN MIX", "CUE", "CLICK", "PREVIEW" };
    for (int i = 0; i < 5; ++i) lab(g, PH[i], { X + 586 + i * 74, Y + 180, 64, 14 }, 7.5f, Theme::sub(), 0, juce::Justification::centred);
    L("the click also goes to the speakers", 630, 207, 300, 8.5f, Theme::txt());
    head("SPEAKERS", "how many and where; ORBIT moves its sound over them", 600, 250, 520);
    L("SPEAKERS", 600, 292, 80, 8.5f, Theme::txt(), 2);
    const char* SK[3] = { "MAIN WIDTH", "ORBIT BLUR", "ROOM VOL" };
    for (int i = 0; i < 3; ++i) lab(g, SK[i], { X + 586 + i * 74, Y + 368, 64, 14 }, 7.5f, Theme::sub(), 0, juce::Justification::centred);
    L("The map, single speaker positions and the routing grid come in the ROOM window (full version).", 830, 332, 280, 8.0f, Theme::sub());
    head(U("RECORDING · PRACTICE · TUNER"), "", 600, 440, 520);
    L("LATENCY", 600, 484, 80, 8.5f, Theme::txt(), 2);
    L("ms - if what you record sounds late against the click, raise it (start with 10)", 750, 480, 360, 8.0f, Theme::sub());
    lab(g, "PRACTICE %", { X + 580, Y + 568, 76, 14 }, 7.5f, Theme::sub(), 0, juce::Justification::centred);
    L("A4 =", 724, 540, 40, 9.0f, Theme::txt(), 2); L("Hz", 818, 540, 30, 9.0f, Theme::sub());
    L("feedback guard", 928, 541, 140, 9.0f, Theme::txt());
    L("PRACTICE: the loops slow down, the pitch stays. Recording waits until you are back at 100.", 600, 600, 500, 8.0f, Theme::sub());
    head(U("iPHONES"), U("open ARCO on your phones (same Wi-Fi, or the cable) - each one is a microphone and a remote"), 24, 640, 540);
    head(U("PEDALS & KEYS"), U("keep your hands on the instrument"), 600, 640, 520);
    const char* AN[4] = { "REC", "PLAY / STOP", "UNDO", "NEXT SCENE" };
    for (int a = 0; a < 4; ++a) {
        L(AN[a], 600 + a * 130, 680, 120, 8.5f, Theme::txt(), 2);
        L("MIDI: " + AudioHost::pedalText(cv.host.pedal[a]), 600 + a * 130, 697, 120, 7.5f, Theme::sub());
    }
    L(U("Keyboard / page-turner pedal:  → or PgDn = REC   ·   Space or ↓ = PLAY/STOP   ·   ← or PgUp = UNDO   ·   ↑ = NEXT SCENE   ·   1-4 = scenes   ·   K = synth"),
      600, 742, 520, 7.5f, Theme::sub());
    g.restoreState();
}

} // namespace arco
