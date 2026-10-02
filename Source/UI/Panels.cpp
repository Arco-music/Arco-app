#include "Panels.h"
#include "MainView.h"

namespace arco {

static void lab(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0,
                juce::Justification j = juce::Justification::centredLeft) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, 2, 0.85f);
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
    L(U("Headphones on a Mac: choose them (or your sound card) as OUTPUT here. TEST plays a soft A on outputs 1-2 and in the headphones. "
        "Bluetooth headphones have a big delay - fine for listening, not for playing along."), 0, 226, 520, 40, 7.5f, Theme::sub());
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
    add<Knob>({ 600, 330, 36, 36 }, [&m] { return m.get("main_w"); }, [&m](double v) { m.set("main_w", v); }, 0, 100, 0, [] { return Theme::txt(); });
    add<Knob>({ 674, 330, 36, 36 }, [&m] { return m.get("mv_blur"); }, [&m](double v) { m.set("mv_blur", v); }, 0, 100, 30, [] { return Theme::fx("orb"); });
    add<Knob>({ 748, 330, 36, 36 }, [&m] { return m.get("room_vol"); }, [&m](double v) { m.set("room_vol", v); }, 0, 100, 100, [] { return Theme::txt(); });
    // ---- timing, practice, tuner, guard
    vb({ 690, 482, 50, 24 }, [&m] { return m.get("lat"); }, [&m](double v) { m.set("lat", v); }, 0, 250, [] { return Theme::amber(); });
    add<Knob>({ 600, 530, 36, 36 }, [&m] { return m.get("prac"); }, [&m](double v) { m.set("prac", v); }, 50, 100, 100, [] { return Theme::amber(); });
    vb({ 760, 538, 54, 24 }, [&m] { return m.get("a4"); }, [&m](double v) { m.set("a4", v); }, 415, 466, [] { return Theme::amber(); });
    auto* gd = add<Pill>({ 900, 540, 22, 20 }, "", [&m] { return m.get("guard") > 0.5 ? Theme::green() : Theme::card2(); }, 8.0f, 5.0f);
    gd->onClick = [&m] { m.set("guard", m.get("guard") > 0.5 ? 0 : 1); };
}

SetupPanel::~SetupPanel() { audio = nullptr; phones.setLookAndFeel(nullptr); }

void SetupPanel::visibilityChanged() {
    if (isVisible()) { phones.setLookAndFeel(nullptr); audio = nullptr; styleLaf(); audio = std::make_unique<AudioBox>(cv, laf.get()); addAndMakeVisible(*audio); audio->setBounds(box.getX() + 24, box.getY() + 98, 520, 300); phones.setLookAndFeel(laf.get()); phones.setSelectedItemIndex(cv.host.phonesPair, juce::dontSendNotification); }
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
        g.setColour(Theme::card2()); g.fillRect(X + x - 8, Y + y - 10, w, 2);
        L(t, x, y, w, 11.0f, Theme::txt(), 2); L(s, x, y + 18, w - 20, 8.0f, Theme::sub()); };
    L("SETUP", 24, 14, 200, 17.0f, Theme::txt(), 2);
    L("audio, inputs, headphones, speakers - changes act at once; this computer remembers them", 130, 22, 700, 9.0f, Theme::sub());
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
    L("This window is remembered on this computer (Application Support / ARCO / setup.json).", 24, 700, 700, 8.0f, Theme::sub());
}

} // namespace arco
