#include "MainView.h"

namespace score {

static Project& P() { return *host().project; }

// ======================================================================== top bar
TopBar::TopBar() {
    for (auto* b : { &toStart, &play, &stop, &rec, &loop, &metro, &undo, &redo, &browser, &keysBtn, &settings, &exportBtn }) addAndMakeVisible(*b);
    toStart.onClick = [] { host().locate(0); };
    play.dynIcon = [] { return host().engine.isPlaying() ? Icon::Pause : Icon::Play; };
    play.lit = [] { return host().engine.isPlaying(); }; play.litColour = [] { return Theme::green(); };
    play.onClick = [] { host().togglePlay(); };
    stop.onClick = [] { if (!host().engine.isPlaying()) host().locate(0); host().stop(); };
    rec.lit = [] { return host().recording(); }; rec.litColour = [] { return Theme::red(); };
    rec.iconColour = [] { return host().recording() ? juce::Colours::white : Theme::red(); };
    rec.onClick = [] { host().record(); };
    loop.lit = [] { return P().loopOn; }; loop.onClick = [] { host().toggleLoop(); };
    metro.lit = [] { return P().metro; }; metro.litColour = [] { return Theme::amber(); };
    metro.onClick = [] { P().metro = !P().metro; host().rebuild(); host().sendChangeMessage(); };
    undo.onClick = [] { host().undo(); }; redo.onClick = [] { host().redo(); };
    browser.onClick = [this] { if (onBrowser) onBrowser(); };
    keysBtn.onClick = [this] { if (onKeys) onKeys(); };
    settings.onClick = [this] { if (onSettings) onSettings(); };
    exportBtn.onClick = [this] { if (onExport) onExport(); };
    int k = 0;
    for (auto* p : { &vArr, &vLine, &vMix }) { addAndMakeVisible(*p); p->fsize = 9.0f; p->radius = 6.0f; int kk = k++;
        p->lit = [this, kk] { return view == kk; }; p->litColour = [kk] { return kk == 1 ? Theme::gold().darker(0.2f) : Theme::blue(); };
        p->onClick = [this, kk] { if (onView) onView(kk); }; }
    vArr.setTooltip("compose: tracks and clips on bars (1)"); vLine.setTooltip("perform: the stations of the piece (2)"); vMix.setTooltip("mix (3)");
    addAndMakeVisible(tap); tap.fsize = 8.5f; tap.radius = 5.0f; tap.onClick = [] { host().tapTempo(); };
    addAndMakeVisible(goBtn); goBtn.fsize = 10.0f; goBtn.radius = 6.0f; goBtn.bg = [] { return Theme::gold(); }; goBtn.textColour = [] { return Theme::lightMode() ? juce::Colours::white : juce::Colour(0xff1a1a1a); };
    goBtn.onClick = [] { auto& sp = host().stations; sp.perform = true; sp.go(); };
    goBtn.dynText = [] { auto& sp = host().stations; return "GO  " + juce::String(sp.current + 2 <= (int) P().stations.size() ? juce::String(sp.current + 2) : juce::String(U("·"))); };
    goBtn.setTooltip(U("the next station (its sound glides in) · pedal / space in PERFORM"));
    tempo = std::make_unique<ValueBox>([] { return P().bpm; }, [](double v) { host().knobTouched(); P().bpm = juce::jlimit(20.0, 300.0, v); P().syncAudioLengths(); host().rebuild(); host().sendChangeMessage(); }, 20, 300, 1, 1.0);
    tempo->format = [](double v) { return juce::String(v, std::abs(v - std::round(v)) < 0.05 ? 0 : 1) + " BPM"; }; tempo->fsize = 11.0f;
    addAndMakeVisible(*tempo);
    sig = std::make_unique<ValueBox>([] { return (double) P().beatsPerBar; }, [](double v) { host().knobTouched(); P().beatsPerBar = juce::jlimit(1, 15, (int) v); host().rebuild(); host().sendChangeMessage(); }, 1, 15, 0, 1);
    sig->format = [](double v) { return juce::String((int) v) + "/4"; }; sig->fsize = 11.0f; addAndMakeVisible(*sig);
    mL = std::make_unique<Meter>([] { return host().engine.masterL.load(); }); mR = std::make_unique<Meter>([] { return host().engine.masterR.load(); });
    addAndMakeVisible(*mL); addAndMakeVisible(*mR);
}

void TopBar::resized() {
    auto r = getLocalBounds().reduced(10, 9);
    nameArea = r.removeFromLeft(170); r.removeFromLeft(6);
    browser.setBounds(r.removeFromLeft(38)); r.removeFromLeft(10);
    for (auto* b : { &toStart, &play, &stop, &rec, &loop, &metro }) { b->setBounds(r.removeFromLeft(b == &play ? 52 : 38)); r.removeFromLeft(4); }
    r.removeFromLeft(8);
    posArea = r.removeFromLeft(150); r.removeFromLeft(8);
    tempo->setBounds(r.removeFromLeft(92)); r.removeFromLeft(3); tap.setBounds(r.removeFromLeft(36).reduced(0, 5)); r.removeFromLeft(4);
    sig->setBounds(r.removeFromLeft(48)); r.removeFromLeft(14);
    auto right = r;
    mR->setBounds(right.removeFromRight(5)); right.removeFromRight(2); mL->setBounds(right.removeFromRight(5)); right.removeFromRight(10);
    settings.setBounds(right.removeFromRight(38)); right.removeFromRight(4);
    exportBtn.setBounds(right.removeFromRight(38)); right.removeFromRight(4);
    keysBtn.setBounds(right.removeFromRight(38)); right.removeFromRight(10);
    redo.setBounds(right.removeFromRight(34)); right.removeFromRight(3); undo.setBounds(right.removeFromRight(34)); right.removeFromRight(12);
    goBtn.setBounds(right.removeFromRight(70)); right.removeFromRight(12);
    int vw = juce::jmin(110, (right.getWidth() - 8) / 3);
    if (vw < 60) { vArr.setBounds({}); vLine.setBounds({}); vMix.setBounds({}); return; }
    auto views = right.withSizeKeepingCentre(vw * 3 + 8, right.getHeight());
    vArr.setBounds(views.removeFromLeft(vw)); views.removeFromLeft(4); vLine.setBounds(views.removeFromLeft(vw)); views.removeFromLeft(4); vMix.setBounds(views);
}

void TopBar::paint(juce::Graphics& g) {
    g.fillAll(Theme::panel());
    g.setColour(Theme::line()); g.drawHorizontalLine(getHeight() - 1, 0, (float) getWidth());
    // logo + piece
    auto n = nameArea;
    g.setColour(Theme::gold()); g.fillRoundedRectangle((float) n.getX(), (float) n.getY() + 6, 26, 26, 7);
    g.setColour(juce::Colour(0xff1a1a1a)); g.setFont(Theme::font(13, 2)); g.drawText("S", n.getX(), n.getY() + 6, 26, 26, juce::Justification::centred);
    label(g, "SCORE", { n.getX() + 34, n.getY() + 2, 120, 16 }, 10.0f, 2, Theme::txt());
    label(g, P().name + (host().dirty ? U(" •") : juce::String()) + U("  ▾"), { n.getX() + 34, n.getY() + 18, n.getWidth() - 34, 16 }, 8.5f, 1);
    // position
    auto p = posArea; g.setColour(Theme::scr()); g.fillRoundedRectangle(p.toFloat(), 7);
    double b = host().engine.position(); int bpb = P().beatsPerBar;
    int bar = (int) std::floor(b / bpb) + 1, beat = (int) std::floor(std::fmod(b, (double) bpb)) + 1, six = (int) std::floor(std::fmod(b, 1.0) * 4) + 1;
    double sec = b * 60.0 / P().bpm;
    g.setColour(host().recording() ? Theme::red() : Theme::txt()); g.setFont(Theme::font(14, 2));
    g.drawText(juce::String(bar) + "." + juce::String(beat) + "." + juce::String(six), p.withTrimmedLeft(10).withWidth(80), juce::Justification::centredLeft);
    label(g, juce::String((int) sec / 60) + ":" + juce::String(std::fmod(sec, 60.0), 1).paddedLeft('0', 4), p.withTrimmedRight(10), 9.0f, 1, Theme::sub(), juce::Justification::centredRight);
    if (host().recording()) label(g, "REC " + juce::String(host().recordingSeconds(), 1) + " s", p.withTrimmedLeft(10).withTrimmedTop(26), 7.0f, 2, Theme::red());
}

void TopBar::mouseDown(const juce::MouseEvent& e) {
    if (nameArea.contains(e.getPosition()) && onFileMenu) onFileMenu();
    if (metro.getBounds().contains(e.getPosition()) && e.mods.isPopupMenu()) {}
}

void TopBar::tick() {
    mL->update(); mR->update();
    repaint(posArea); repaint(nameArea);
    for (auto* c : getChildren()) if (dynamic_cast<IconButton*>(c) || dynamic_cast<Pill*>(c)) c->repaint();
}

// ======================================================================== main view
MainView::MainView(Host& h) : UiHolder(h), host_(h) {
    ui.automate = [this](Param* p, Uid t) { setView(0); arrange.openAutomation(p, t); };
    ui.showOverlay = [this](juce::Component* c) { showOverlay(c); };
    ui.closeOverlay = [this] { closeOverlay(); };
    ui.refresh = [this] { rebuildAll(); };
    Theme::lightMode() = h.theme == 1;
    setWantsKeyboardFocus(true);
    addAndMakeVisible(top); addAndMakeVisible(browser); addAndMakeVisible(arrange); addChildComponent(line); addChildComponent(mixer);
    addAndMakeVisible(rack); addChildComponent(editor);
    for (auto* p : { &tDev, &tEd }) { addAndMakeVisible(*p); p->fsize = 8.5f; p->radius = 5.0f; }
    tDev.lit = [this] { return bottomTab == 0; }; tEd.lit = [this] { return bottomTab == 1; };
    tDev.onClick = [this] { bottomTab = 0; resized(); }; tEd.onClick = [this] { bottomTab = 1; resized(); };
    top.onView = [this](int v) { setView(v); };
    top.onFileMenu = [this] { fileMenu(); };
    top.onBrowser = [this] { showBrowser = !showBrowser; resized(); };
    top.onKeys = [this] { showKeys = !showKeys; resized(); };
    top.onSettings = [this] { showOverlay(new SettingsPanel()); };
    top.onExport = [this] { showOverlay(new ExportPanel()); };
    arrange.onOpenEditor = [this] { bottomTab = 1; editor.update(); resized(); };
    arrange.onOpenInstrument = [this](Uid t) { openInstrument(t); };
    rack.onOpenInstrument = [this](Uid t) { openInstrument(t); };
    editor.onClipFx = [this] { bottomTab = 0; rack.clipMode = true; rack.update(); resized(); };
    keys = std::make_unique<juce::MidiKeyboardComponent>(h.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard);
    keys->setLowestVisibleKey(36); keys->setWantsKeyboardFocus(false);
    addChildComponent(*keys);
    h.addChangeListener(this);
    h.onAction = [this](int a) { if (a == Host::ActGo || a == Host::ActBack) line.update(); };
    rebuildAll();
    startTimerHz(30);
   #if JUCE_IOS
    showBrowser = false;
   #endif
}

MainView::~MainView() { host_.removeChangeListener(this); host_.onAction = nullptr; inst = nullptr; overlay = nullptr; }

void MainView::rebuildAll() {
    keys->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, Theme::lightMode() ? juce::Colours::white : Theme::rgb(0.86f, 0.87f, 0.9f));
    keys->setColour(juce::MidiKeyboardComponent::blackNoteColourId, Theme::rgb(0.1f, 0.1f, 0.12f));
    keys->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::blue());
    keys->setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    arrange.refresh(); mixer.refresh(); rack.update(); editor.update(); line.update();
    resized(); repaint();
}

void MainView::setView(int v) {
    view = v; top.view = v;
    arrange.setVisible(v == 0); line.setVisible(v == 1); mixer.setVisible(v == 2);
    if (inst) { inst = nullptr; }
    resized(); repaint(); grabKeyboardFocus();
}

void MainView::openInstrument(Uid t) {
    Track* tr = P().track(t); if (!tr || !tr->inst) return;
    host_.selectTrack(t);
    inst = std::make_unique<InstrumentEditor>(tr);
    inst->onClose = [this] { juce::MessageManager::callAsync([this] { inst = nullptr; resized(); grabKeyboardFocus(); }); };
    addAndMakeVisible(*inst);
    resized();
}

void MainView::showOverlay(juce::Component* c) {
    overlay.reset(c); addAndMakeVisible(*c); c->setBounds(getLocalBounds()); c->toFront(true);
}
void MainView::closeOverlay() { juce::MessageManager::callAsync([this] { overlay = nullptr; grabKeyboardFocus(); repaint(); }); }

void MainView::resized() {
    auto r = getLocalBounds();
    top.setBounds(r.removeFromTop(56));
    status = r.removeFromBottom(24);
    keys->setVisible(showKeys);
    if (showKeys) { auto k = r.removeFromBottom(64); keys->setBounds(k.reduced(6, 4)); keys->setKeyWidth(juce::jmax(14.0f, (float) k.getWidth() / 52.0f)); }
    browser.setVisible(showBrowser);
    if (showBrowser) browser.setBounds(r.removeFromLeft(juce::jmin(280, r.getWidth() / 4)));
    if (r.getHeight() <= 0) return;
    int bh = juce::jlimit(150, juce::jmax(150, r.getHeight() - 220), view == 1 ? juce::jmin(bottomH, 220) : bottomH);
    auto bottom = r.removeFromBottom(bh);
    split = r.removeFromBottom(8);
    auto tabs = bottom.removeFromTop(0);
    juce::ignoreUnused(tabs);
    rack.setVisible(bottomTab == 0); editor.setVisible(bottomTab == 1);
    rack.setBounds(bottom); editor.setBounds(bottom);
    tDev.setBounds(split.getRight() - 160, split.getY() - 2, 74, 0); tEd.setBounds({});
    // the two tabs sit at the top right of the bottom area, over the split line
    auto tb = juce::Rectangle<int>(bottom.getRight() - 170, bottom.getY() - 22, 164, 20);
    tDev.setBounds(tb.removeFromLeft(80)); tb.removeFromLeft(4); tEd.setBounds(tb);
    tDev.toFront(false); tEd.toFront(false);
    arrange.setBounds(r.withTrimmedBottom(16)); line.setBounds(r.withTrimmedBottom(16)); mixer.setBounds(r.withTrimmedBottom(16));
    if (inst) inst->setBounds(juce::Rectangle<int>(showBrowser ? browser.getRight() : 0, top.getBottom(), getWidth() - (showBrowser ? browser.getRight() : 0), status.getY() - top.getBottom() - (showKeys ? 64 : 0)));
    if (inst) { inst->toFront(false); }
    if (overlay) overlay->setBounds(getLocalBounds());
}

void MainView::paint(juce::Graphics& g) {
    g.fillAll(Theme::bg());
    // split handle
    g.setColour(Theme::panel()); g.fillRect(split.withTrimmedTop(-16));
    g.setColour(Theme::line()); g.fillRoundedRectangle((float) split.getCentreX() - 20, (float) split.getCentreY() - 1.5f, 40, 3, 1.5f);
    auto lbl = juce::Rectangle<int>(split.getX() + 12, split.getY() - 18, 400, 18);
    Track* t = host_.selectedTrack();
    label(g, bottomTab == 0 ? (U("DEVICES  ·  ") + (t ? t->name : juce::String("select a track"))) : juce::String("EDITOR"), lbl, 8.5f, 2, Theme::sub());
    // status line
    g.setColour(Theme::panel()); g.fillRect(status); g.setColour(Theme::line()); g.drawHorizontalLine(status.getY(), 0, (float) getWidth());
    double age = juce::Time::getMillisecondCounterHiRes() - host_.statusTime;
    auto st = host_.status.isNotEmpty() && age < 8000 ? host_.status : juce::String(U("drop sound files anywhere · double-click a lane to write notes · double-click the ruler for a station · right-click everything"));
    label(g, st, status.reduced(12, 0).withTrimmedRight(320), 8.5f, host_.status.isNotEmpty() && age < 8000 ? 1 : 0, host_.status.isNotEmpty() && age < 8000 ? Theme::txt() : Theme::sub());
    auto* d = host_.devices.getCurrentAudioDevice();
    juce::String right = d ? juce::String((int) d->getCurrentSampleRate()) + " Hz · " + juce::String(d->getCurrentBufferSizeSamples()) + U(" · CPU ") + juce::String((int) (host_.engine.cpu.load() * 100)) + "%" : juce::String("no sound card (Settings)");
    label(g, right, status.reduced(12, 0).removeFromRight(300), 8.0f, 1, Theme::sub(), juce::Justification::centredRight);
}

void MainView::paintOverChildren(juce::Graphics&) {}

void MainView::mouseMove(const juce::MouseEvent& e) { setMouseCursor(split.expanded(0, 4).contains(e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }
void MainView::mouseDown(const juce::MouseEvent& e) { if (split.expanded(0, 4).contains(e.getPosition())) { draggingSplit = true; splitStart = bottomH; } }
void MainView::mouseDrag(const juce::MouseEvent& e) { if (draggingSplit) { bottomH = juce::jlimit(150, juce::jmax(150, getHeight() - 400), splitStart - e.getDistanceFromDragStartY()); resized(); } }

void MainView::filesDropped(const juce::StringArray& files, int, int) {
    for (auto& f : files) { juce::File file(f);
        if ((file.isDirectory() && file.getChildFile("project.json").existsAsFile()) || file.hasFileExtension("zip")) { if (host_.dirty) host_.saveProject(); host_.openProject(file); return; } }
    double beat = host_.engine.position(); Uid tid = 0; juce::String errs;
    for (auto& f : files) { auto e = host_.importClip(juce::File(f), tid, beat); if (e.isNotEmpty()) errs << e << "\n"; else if (Clip* c = host_.selectedClip()) { beat = c->end(); Track* ot = nullptr; P().clip(c->id, &ot); if (ot) tid = ot->id; } }
    if (errs.isNotEmpty()) host_.say(errs.trim());
}

void MainView::changeListenerCallback(juce::ChangeBroadcaster*) {
    arrange.updateFromHost();
    if (inst && !P().track(inst->track->id)) { inst = nullptr; resized(); }
    if (inst) { bool found = false; for (auto* t : P().allTracks()) if (t == inst->track) found = true; if (!found) { inst = nullptr; resized(); } }
    repaint(status); repaint(split.withTrimmedTop(-18));
    top.repaint();
}

void MainView::timerCallback() {
    top.tick();
    if (host_.status != lastStatus || host_.statusTime != lastStatusTime) { lastStatus = host_.status; lastStatusTime = host_.statusTime; repaint(status); }
    static int k = 0; if (++k % 15 == 0) repaint(status);
    if (overlay == nullptr && inst == nullptr && !hasKeyboardFocus(true) && isShowing() && juce::Process::isForegroundProcess()) {
        if (auto* f = juce::Component::getCurrentlyFocusedComponent()) { if (dynamic_cast<juce::TextEditor*>(f) == nullptr) grabKeyboardFocus(); }
    }
}

// ---------------------------------------------------------------- keys
static int noteForKey(int kc) {
    const char* row = "awsedftgyhujkolp;"; // C C# D D# E F F# G G# A A# B C C# D D# E
    for (int i = 0; row[i]; ++i) if (juce::CharacterFunctions::toLowerCase((juce::juce_wchar) kc) == (juce::juce_wchar) row[i]) return i;
    return -1;
}

bool MainView::keyPressed(const juce::KeyPress& k) {
    auto& h = host_;
    auto mods = k.getModifiers(); int kc = k.getKeyCode();
    if (overlay && kc == juce::KeyPress::escapeKey) { closeOverlay(); return true; }
    if (inst && kc == juce::KeyPress::escapeKey) { inst = nullptr; resized(); return true; }
    if (mods.isCommandDown()) {
        if (kc == 'Z' || kc == 'z') { if (mods.isShiftDown()) h.redo(); else h.undo(); return true; }
        if (kc == 'S' || kc == 's') { if (mods.isShiftDown() || !P().folder.getChildFile("project.json").existsAsFile()) saveAs(); else h.saveProject(); return true; }
        if (kc == 'N' || kc == 'n') { if (h.dirty) h.saveProject(); h.newProject(true); return true; }
        if (kc == 'O' || kc == 'o') { fileMenu(); return true; }
        if (kc == 'D' || kc == 'd') { h.duplicateClips(); return true; }
        if (kc == 'E' || kc == 'e') { if (h.selClip) h.splitClip(h.selClip, h.engine.position()); return true; }
        if (kc == 'I' || kc == 'i') { browser.importFiles(); return true; }
        if (kc == 'A' || kc == 'a') { h.selClips.clear(); for (auto& t : P().tracks) for (auto& c : t->clips) h.selClips.insert(c->id); h.sendChangeMessage(); return true; }
        return false;
    }
    if (kc == juce::KeyPress::spaceKey) { if (view == 1 && h.stations.perform) h.stations.go(); else h.togglePlay(); return true; }
    if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey) { h.deleteSelection(); return true; }
    if (kc == juce::KeyPress::homeKey || kc == juce::KeyPress::returnKey) { h.locate(0); return true; }
    if (kc == juce::KeyPress::tabKey) { setView((view + 1) % 3); return true; }
    if (kc == juce::KeyPress::rightKey && view == 1) { h.stations.go(); return true; }
    if (kc == juce::KeyPress::leftKey && view == 1) { h.stations.back(); return true; }
    if (kc == '1' || kc == '2' || kc == '3') { setView(kc - '1'); return true; }
    // note keys (only when an instrument has the keys); otherwise R / L / M
    Track* t = P().track(h.engine.keysTarget.load());
    bool notes = t && t->isInstrument() && (h.selectedTrack() == t || inst != nullptr);
    if (notes) {
        if (kc == 'z' || kc == 'Z') { octave = juce::jmax(0, octave - 1); h.say("keys: octave " + juce::String(octave)); return true; }
        if (kc == 'x' || kc == 'X') { octave = juce::jmin(8, octave + 1); h.say("keys: octave " + juce::String(octave)); return true; }
        int n = noteForKey(kc);
        if (n >= 0) { int note = 12 * (octave + 1) + n; if (!held.count(kc)) { held.insert(kc); h.noteOn(note, 0.8f); } return true; }
    }
    if (kc == 'r' || kc == 'R') { h.record(); return true; }
    if (kc == 'l' || kc == 'L') { h.toggleLoop(); return true; }
    if (kc == 'm' || kc == 'M') { P().metro = !P().metro; h.rebuild(); h.sendChangeMessage(); return true; }
    if (kc == 'g' || kc == 'G') { h.stations.perform = true; h.stations.go(); return true; }
    return false;
}

bool MainView::keyStateChanged(bool) {
    bool used = false;
    for (auto it = held.begin(); it != held.end();) {
        if (!juce::KeyPress::isKeyCurrentlyDown(*it)) { int n = noteForKey(*it); if (n >= 0) host_.noteOff(12 * (octave + 1) + n); it = held.erase(it); used = true; }
        else ++it;
    }
    return used;
}

// ---------------------------------------------------------------- the piece
void MainView::saveAs() {
    auto* aw = new juce::AlertWindow("Save the piece", "Its name:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("n", P().name.startsWith("Untitled") ? juce::String() : P().name); aw->addButton("Save", 1); aw->addButton("Cancel", 0);
    aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int b) { if (b == 1) { host_.saveProjectAs(aw->getTextEditorContents("n")); repaint(); } }), true);
}

void MainView::fileMenu() {
    juce::PopupMenu m, rec;
    m.addItem(1, "New piece");
    m.addItem(2, "Open a piece ...");
    for (int i = 0; i < host_.recent.size() && i < 10; ++i) { juce::File f(host_.recent[i]); if (f.isDirectory()) rec.addItem(100 + i, f.getFileNameWithoutExtension()); }
    m.addSubMenu("Open recent", rec, rec.getNumItems() > 0);
    m.addSeparator();
    m.addItem(3, "Save"); m.addItem(4, "Save as / rename ...");
    m.addSeparator();
    m.addItem(5, "Import sound files ...");
    m.addItem(6, "Export audio ...");
    m.addItem(7, "Send the whole piece (one .zip file) ...");
    m.addSeparator();
    m.addItem(8, "Show the piece's folder");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&top).withMaximumNumColumns(1), [this](int r) {
        auto& h = host_;
        if (r == 1) { if (h.dirty && P().folder.getChildFile("project.json").existsAsFile()) h.saveProject(); h.newProject(true); }
        if (r == 2) { showBrowser = true; browser.show(3); resized();
            chooser = std::make_unique<juce::FileChooser>("Open a SCORE piece (the .score folder, its project.json or a .zip)", Host::songsFolder(), "*.json;*.zip");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
                auto f = fc.getResult(); if (f.exists()) { if (host_.dirty) host_.saveProject(); host_.openProject(f); } }); }
        if (r >= 100) { if (h.dirty) h.saveProject(); h.openProject(juce::File(h.recent[r - 100])); }
        if (r == 3) { if (!P().folder.getChildFile("project.json").existsAsFile()) saveAs(); else h.saveProject(); }
        if (r == 4) saveAs();
        if (r == 5) browser.importFiles();
        if (r == 6) showOverlay(new ExportPanel());
        if (r == 7) { auto zip = P().folder.getParentDirectory().getChildFile(P().name + ".zip");
            if (h.exportProjectBundle(zip)) { h.say(zip.getFileName() + " is ready to send");
               #if JUCE_IOS
                chooser = std::make_unique<juce::FileChooser>("Save / share the piece", zip);
                chooser->launchAsync(juce::FileBrowserComponent::saveMode, [](const juce::FileChooser&) {});
               #else
                zip.revealToUser();
               #endif
            } }
        if (r == 8) P().folder.revealToUser();
    });
}

} // namespace score
