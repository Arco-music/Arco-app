// ARCO 1.2: every menu item and shortcut, in one place.
#include "MainView.h"
#include "Panels.h"
#include "SynthPanel.h"
#include "Sheets.h"
#include "HelpText.h"
#include "LearnOverlay.h"

namespace arco {

static int helpIndex(const char* title) {
    auto& S = helpSections();
    for (int i = 0; i < (int) S.size(); ++i) if (juce::String(S[(size_t) i].title).startsWithIgnoreCase(title)) return i;
    return 0;
}

bool Canvas::autoMixAvailable() const { return autoMixSheet && autoMixSheet->hasResult(); }

bool Canvas::anyOverlay() const {
    for (juce::Component* c : std::initializer_list<juce::Component*> { setup.get(), look.get(), synthWin.get(), exportSheet.get(), helpSheet.get(), aboutSheet.get(), importSheet.get(), autoMixSheet.get(), overlayExtra() })
        if (c != nullptr && c->isVisible()) return true;
    return false;
}

// (filled in as the big windows arrive)
juce::Component* Canvas::overlayExtra() const { return nullptr; }
void Canvas::openStudio() { showMessage("STUDIO opens here"); }
void Canvas::setMidiLearn(bool on) {
    midiLearn = on;
    if (on) learnLayer->start(); else learnLayer->stop();
    showMessage(on ? juce::String("MIDI LEARN: click a control, then move a MIDI knob") : juce::String());
    repaint();
}
juce::Component* Canvas::topWindow() const {
    juce::Component* best = nullptr;
    for (int i = getNumChildComponents(); --i >= 0;) {
        auto* c = getChildComponent(i);
        if (c == learnLayer.get() || !c->isVisible()) continue;
        if (c == setup.get() || c == look.get() || c == synthWin.get() || c == exportSheet.get() || c == helpSheet.get() || c == aboutSheet.get() || c == importSheet.get() || c == autoMixSheet.get() || c == overlayExtra()) { best = c; break; }
    }
    return best;
}
void Canvas::openAutoMix() { autoMixSheet->setVisible(true); autoMixSheet->toFront(true); }
void Canvas::autoMixCompare() { if (autoMixSheet->hasResult()) { autoMixSheet->showAB(!autoMixSheet->after); showMessage(autoMixSheet->after ? "AUTO MIX: after" : "AUTO MIX: before"); } }

void Canvas::showMenu() {
    if (!menu) return;
    menu->everything().showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(juce::Rectangle<int>(16, 6, 120, 30))),
                                     [this](int r) { if (r > 0) command(r); });
}

void Canvas::importInto(int track) {
    chooseFile("Import an audio file into TRACK " + juce::String(track), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.m4a;*.caf", false,
               [this, track](juce::File f) { requestImport(f, track); });
}
void Canvas::requestImport(const juce::File& f, int track) { importSheet->open(f, track); }

void Canvas::command(int id) {
    auto* win = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
    auto askThen = [this](std::function<void()> then) {          // unsaved loops? ask before throwing them away
        if (!host.unsavedLoops()) { then(); return; }
        auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Save the song first?")
                        .withMessage("Your loops are not saved yet.").withButton("Save").withButton("Don't save").withButton("Cancel");
        juce::AlertWindow::showAsync(opts, [this, then](int r) {
            if (r == 2) then();
            else if (r == 1) { if (host.songFolder.isDirectory()) { host.saveSong(host.songFolder); then(); } else doSave(true); }
        });
    };
    if (id >= cmdPalette && id < cmdPalette + 50) { Theme::setPalette(id - cmdPalette); host.saveSetup(); themeChanged(); return; }
    if (id >= cmdRecent && id < cmdRecent + 50) {
        int i = id - cmdRecent;
        if (i < host.recentSongs.size()) { juce::File f(host.recentSongs[i]); askThen([this, f] { host.openSong(f); }); }
        return;
    }
    if (id >= cmdImport1 && id < cmdImport1 + 8) { importInto(id - cmdImport1 + 1); return; }
    switch (id) {
        case cmdNewSong: askThen([this] { host.newSong(); }); break;
        case cmdOpen: askThen([this] { doOpen(); }); break;
        case cmdSave: doSave(false); break;
        case cmdSaveAs: doSave(true); break;
        case cmdExport: exportSheet->setVisible(true); exportSheet->toFront(true); break;
        case cmdRecordShow: doRecShow(); break;
        case cmdStudio: openStudio(); break;
        case cmdUndo: host.doAction(AudioHost::ActUndo); break;
        case cmdResetAll:
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Clear all loops?", "Every loop of every scene is cleared.", "Clear", "Cancel", nullptr,
                juce::ModalCallbackFunction::create([this](int r) { if (r == 1) model().press("st_looper", "rstall"); }));
            break;
        case cmdMidiLearn: setMidiLearn(!midiLearn); break;
        case cmdMidiClear: host.clearMidiMap(); showMessage("all MIDI connections forgotten"); break;
        case cmdAutoMix: openAutoMix(); break;
        case cmdAB: autoMixCompare(); break;
        case cmdToggleLight: Theme::toggleLight(); host.saveSetup(); themeChanged(); break;
        case cmdLook: openLook(true); break;
        case cmdSynth: openSynth(true); break;
        case cmdSetup: openSetup(true); break;
        case cmdMicView: openMic(true); break;
        case cmdFullScreen: if (win) win->setFullScreen(!win->isFullScreen()); break;
        case cmdMinimise: if (win) win->setMinimised(true); break;
        case cmdHelp: helpSheet->show(0); helpSheet->setVisible(true); helpSheet->toFront(true); break;
        case cmdShortcuts: helpSheet->show(helpIndex("Keys")); helpSheet->setVisible(true); helpSheet->toFront(true); break;
        case cmdWhatsNew: helpSheet->show(helpIndex("What")); helpSheet->setVisible(true); helpSheet->toFront(true); break;
        case cmdUpdates: openSetup(true); setup->checkForUpdates(); break;
        case cmdAbout: aboutSheet->setVisible(true); aboutSheet->toFront(true); break;
        default: break;
    }
}

} // namespace arco
