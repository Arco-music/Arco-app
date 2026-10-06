#include "Browser.h"

namespace score {

static Project& P() { return *host().project; }

static bool isAudio(const juce::File& f) {
    auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".mp3" || e == ".ogg" || e == ".m4a" || e == ".caf" || e == ".aac";
}

// ======================================================================== the list
void BrowserList::paint(juce::Graphics& g) {
    int y = 0;
    for (int i = 0; i < (int) items.size(); ++i) {
        auto& it = items[(size_t) i]; int h = rowH(it);
        auto r = juce::Rectangle<int>(0, y, getWidth(), h); y += h;
        if (it.kind == 5) { label(g, it.text, r.reduced(10, 0).withTrimmedTop(6), 8.0f, 2, Theme::sub()); continue; }
        if (i == selected || i == hover) { g.setColour(i == selected ? Theme::sel() : Theme::card2()); g.fillRoundedRectangle(r.reduced(4, 1).toFloat(), 6); }
        auto ic = r.removeFromLeft(30).toFloat().withSizeKeepingCentre(15, 15);
        Icon icon = it.kind == 0 || it.kind == 6 ? Icon::Folder : it.kind == 1 ? Icon::Wave : it.kind == 2 || it.kind == 7 ? Icon::Synth : it.kind == 3 ? Icon::Mixer : Icon::Line;
        auto c = it.kind == 1 ? Theme::blue() : it.kind == 2 || it.kind == 7 ? Theme::violet() : it.kind == 3 ? Theme::amber() : it.kind == 4 ? Theme::gold() : Theme::sub();
        if (it.kind == 1 && host().previewing() == juce::File(it.payload)) { c = Theme::green(); icon = Icon::Play; }
        g.setColour(c); g.fillPath(iconPath(icon, ic));
        if (it.sub.isNotEmpty()) { label(g, it.text, r.withHeight(18).withTrimmedTop(3), 9.0f, 1, Theme::txt()); label(g, it.sub, r.withTrimmedTop(17).withHeight(13), 7.5f, 0); }
        else label(g, it.text, r, 9.0f, it.kind == 6 ? 2 : 1, Theme::txt());
        if (it.kind == 1 && host().previewing() == juce::File(it.payload)) { float p = host().previewProgress(); if (p >= 0) { g.setColour(Theme::green()); g.fillRect(30.0f, (float) r.getBottom() - 2, (getWidth() - 40) * p, 2.0f); } }
    }
}
void BrowserList::mouseDown(const juce::MouseEvent& e) {
    int i = indexAt(e.y); if (i < 0 || items[(size_t) i].kind == 5) return;
    selected = i; repaint(); if (onClick) onClick(items[(size_t) i]);
}
void BrowserList::mouseDrag(const juce::MouseEvent& e) {
    if (e.getDistanceFromDragStart() < 8 || selected < 0 || !dragDesc) return;
    auto d = dragDesc(items[(size_t) selected]); if (d.isEmpty()) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!dc->isDragAndDropActive()) {
        juce::Image img(juce::Image::ARGB, 180, 24, true); { juce::Graphics g(img); g.setColour(Theme::sel()); g.fillRoundedRectangle(0, 0, 180, 24, 6); label(g, items[(size_t) selected].text, { 8, 0, 168, 24 }, 9.0f, 2, Theme::txt()); }
        dc->startDragging(d, this, juce::ScaledImage(img)); }
}
void BrowserList::mouseDoubleClick(const juce::MouseEvent& e) { int i = indexAt(e.y); if (i >= 0 && onDouble && items[(size_t) i].kind != 5) onDouble(items[(size_t) i]); }

// ======================================================================== the browser
static juce::File foldersFile() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SCORE").getChildFile("folders.txt"); }
void Browser::loadFolders() { extraFolders.clear(); juce::StringArray l; l.addLines(foldersFile().loadFileAsString()); for (auto& s : l) if (s.trim().isNotEmpty() && juce::File(s.trim()).isDirectory()) extraFolders.push_back(juce::File(s.trim())); }
void Browser::saveFolders() { juce::StringArray l; for (auto& f : extraFolders) l.add(f.getFullPathName()); foldersFile().getParentDirectory().createDirectory(); foldersFile().replaceWithText(l.joinIntoString("\n")); }

Browser::Browser() {
    loadFolders();
    for (auto* p : { &tSounds, &tInst, &tFx, &tPieces, &importBtn, &upBtn, &addFolder }) { addAndMakeVisible(*p); p->fsize = 8.5f; p->radius = 5.0f; }
    int k = 0; for (auto* p : { &tSounds, &tInst, &tFx, &tPieces }) { int kk = k++; p->lit = [this, kk] { return tab == kk; }; p->onClick = [this, kk] { show(kk); }; }
    importBtn.bg = [] { return Theme::blue().withAlpha(0.8f); };
    importBtn.onClick = [this] { importFiles(); };
    importBtn.setTooltip(U("pick sound files anywhere (Downloads, iCloud, a USB stick …): they go on the selected track at the playhead"));
    upBtn.onClick = [this] { if (folder != juce::File()) { auto p = folder.getParentDirectory(); bool isPlace = false; for (auto& pl : places) if (pl == folder) isPlace = true; openFolder(isPlace || p == folder ? juce::File() : p); } };
    addFolder.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("A folder with sounds", juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            auto r = fc.getResult(); if (r.isDirectory()) { extraFolders.push_back(r); saveFolders(); openFolder(r); } }); };
    addAndMakeVisible(search); styleEditor(search, 9.5f); search.setTextToShowWhenEmpty(U("search …"), Theme::sub());
    search.onTextChange = [this] { fill(); };
    view.setViewedComponent(&list, false); view.setScrollBarsShown(true, false); view.setScrollBarThickness(6);
    addAndMakeVisible(view);
    list.dragDesc = [](const BItem& it) -> juce::String {
        if (it.kind == 1) return "file:" + it.payload; if (it.kind == 3) return "fx:" + it.payload; return {}; };
    list.onClick = [this](const BItem& it) {
        if (it.kind == 0 || it.kind == 6) openFolder(juce::File(it.payload));
        else if (it.kind == 1) host().preview(juce::File(it.payload));
        else if (it.kind == 2) {                    // a NOVA sound: on the selected NOVA track (or a new one)
            Track* t = host().selectedTrack();
            if (!t || !t->inst || t->inst->type != "nova") { for (auto& x : P().tracks) if (x->inst && x->inst->type == "nova") { t = x.get(); break; } }
            int idx = it.payload.getIntValue();
            if (!t) { Uid made = 0; host().edit("new track", [&] { auto nt = P().newTrack(TrackKind::Instrument, novaPresets()[(size_t) idx].name, "nova"); P().tracks.push_back(nt); made = nt->id; }); host().selectTrack(made); t = P().track(made); }
            if (auto* n = t ? dynamic_cast<Nova*>(t->inst.get()) : nullptr) { host().knobTouched(); applyNovaPreset(*n, novaPresets()[(size_t) idx]); host().selectTrack(t->id);
                host().say(t->name + U(" plays “") + juce::String(novaPresets()[(size_t) idx].name) + U("” · play the keys (A–L on the computer, or a MIDI keyboard)")); }
        }
        else if (it.kind == 3) { Track* t = host().selectedTrack(); if (!t) { host().say("select a track first"); return; }
            auto type = it.payload.toStdString(); host().edit("add effect", [t, type] { if (auto e = makeEffect(type)) t->fx.push_back(std::shared_ptr<Effect>(std::move(e))); });
            host().say(it.text + U(" added to ") + t->name); }
        else if (it.kind == 4) {
            juce::File f(it.payload); if (f == P().folder) return;
            if (host().dirty) host().saveProject();
            if (!host().openProject(f)) host().say("that piece can't be opened");
        }
        else if (it.kind == 7) {
            Uid made = 0; auto type = it.payload.toStdString();
            host().edit("new track", [&] { auto nt = P().newTrack(TrackKind::Instrument, it.text, type); for (auto& s : P().stations) if (s.hasSnap) for (auto& kv : P().captureKnobs()) s.snap.insert(kv);
                P().tracks.push_back(nt); made = nt->id; });
            host().selectTrack(made);
        }
    };
    list.onDouble = [](const BItem& it) {
        if (it.kind == 1) { host().stopPreview(); Track* t = host().selectedTrack(); auto e = host().importClip(juce::File(it.payload), t && t->kind == TrackKind::Audio ? t->id : 0, host().engine.position());
            if (e.isNotEmpty()) host().say(e); else host().say(U("“") + it.text + U("” is on the track at the playhead")); }
    };
    places.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads"));
    places.add(juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    places.add(juce::File::getSpecialLocation(juce::File::userDesktopDirectory));
    places.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));
    show(0);
    startTimerHz(10);
}

void Browser::show(int t) { tab = t; folder = juce::File(); search.clear(); fill(); resized(); repaint(); }
void Browser::openFolder(const juce::File& f) { folder = f; search.clear(); fill(); view.setViewPosition(0, 0); }

void Browser::fill() {
    auto& v = list.items; v.clear(); list.selected = -1;
    auto q = search.getText().trim().toLowerCase();
    auto match = [&](const juce::String& s) { return q.isEmpty() || s.toLowerCase().contains(q); };
    if (tab == 0) {
        if (folder == juce::File()) {
            v.push_back({ "THIS PIECE", {}, 5 });
            v.push_back({ "Recordings and sounds of this piece", P().name, 6, host().audioFolder().getFullPathName() });
            v.push_back({ "PLACES", {}, 5 });
            for (auto& p : places) if (p.isDirectory()) v.push_back({ p.getFileName(), {}, 6, p.getFullPathName() });
            for (auto& p : extraFolders) v.push_back({ p.getFileName(), p.getParentDirectory().getFullPathName(), 6, p.getFullPathName() });
            if (q.isNotEmpty()) {                     // search the places (two levels deep)
                v.push_back({ "FOUND", {}, 5 }); int n = 0;
                std::vector<juce::File> roots(places.begin(), places.end()); for (auto& e : extraFolders) roots.push_back(e);
                for (auto& r : roots) for (auto& f : r.findChildFiles(juce::File::findFiles, true, "*")) { if (n > 200) break; if (isAudio(f) && match(f.getFileName())) { v.push_back({ f.getFileNameWithoutExtension(), f.getParentDirectory().getFileName(), 1, f.getFullPathName() }); ++n; } }
            }
        } else {
            auto dirs = folder.findChildFiles(juce::File::findDirectories, false); dirs.sort();
            auto files = folder.findChildFiles(juce::File::findFiles, false); files.sort();
            for (auto& d : dirs) if (!d.isHidden() && match(d.getFileName())) v.push_back({ d.getFileName(), {}, 0, d.getFullPathName() });
            for (auto& f : files) if (isAudio(f) && match(f.getFileName())) {
                double mb = f.getSize() / 1048576.0;
                v.push_back({ f.getFileNameWithoutExtension(), f.getFileExtension().substring(1).toUpperCase() + U(" · ") + juce::String(mb, 1) + " MB", 1, f.getFullPathName() }); }
            if (v.empty()) v.push_back({ "no sound files here", {}, 5 });
        }
    } else if (tab == 1) {
        v.push_back({ "NEW INSTRUMENT TRACK", {}, 5 });
        for (auto& i : instrumentCatalog()) if (match(i.name)) v.push_back({ i.name, i.about, 7, i.type });
        auto& pr = novaPresets(); std::string grp;
        for (int i = 0; i < (int) pr.size(); ++i) { if (!match(pr[(size_t) i].name) && !match(pr[(size_t) i].group)) continue;
            if (pr[(size_t) i].group != grp) { grp = pr[(size_t) i].group; v.push_back({ U("NOVA · ") + juce::String(grp), {}, 5 }); }
            v.push_back({ pr[(size_t) i].name, {}, 2, juce::String(i) }); }
    } else if (tab == 2) {
        std::string grp;
        for (auto& e : effectCatalog()) { if (!match(e.name) && !match(e.about) && !match(e.group)) continue;
            if (e.group != grp) { grp = e.group; v.push_back({ juce::String(grp), {}, 5 }); }
            v.push_back({ e.name, e.about, 3, e.type }); }
    } else {
        auto songs = Host::songsFolder().findChildFiles(juce::File::findDirectories, false, "*.score");
        std::sort(songs.begin(), songs.end(), [](auto& a, auto& b) { return a.getChildFile("project.json").getLastModificationTime() > b.getChildFile("project.json").getLastModificationTime(); });
        v.push_back({ "YOUR PIECES", {}, 5 });
        for (auto& s : songs) if (s.getChildFile("project.json").existsAsFile() && match(s.getFileNameWithoutExtension()))
            v.push_back({ s.getFileNameWithoutExtension(), s.getChildFile("project.json").getLastModificationTime().toString(true, true, false), 4, s.getFullPathName() });
        if (v.size() == 1) v.push_back({ "saved pieces appear here", {}, 5 });
    }
    list.setSize(juce::jmax(10, view.getWidth() - 8), list.total());
    list.repaint();
}

void Browser::resized() {
    auto r = getLocalBounds().reduced(8, 8);
    auto tabs = r.removeFromTop(24); int w = (tabs.getWidth() - 9) / 4;
    for (auto* p : { &tSounds, &tInst, &tFx, &tPieces }) { p->setBounds(tabs.removeFromLeft(w)); tabs.removeFromLeft(3); }
    r.removeFromTop(8);
    auto row = r.removeFromTop(24);
    bool files = tab == 0;
    upBtn.setVisible(files && folder != juce::File()); addFolder.setVisible(files); importBtn.setVisible(files);
    if (files) { importBtn.setBounds(row.removeFromRight(90)); row.removeFromRight(4); addFolder.setBounds(row.removeFromRight(64)); row.removeFromRight(4); }
    search.setBounds(row);
    r.removeFromTop(6);
    if (files && folder != juce::File()) { auto crumb = r.removeFromTop(24); upBtn.setBounds(crumb.removeFromLeft(26)); }
    view.setBounds(r);
    list.setSize(juce::jmax(10, view.getWidth() - 8), list.total());
}

void Browser::paint(juce::Graphics& g) {
    g.fillAll(Theme::panel());
    g.setColour(Theme::line()); g.drawVerticalLine(getWidth() - 1, 0, (float) getHeight());
    if (tab == 0 && folder != juce::File()) label(g, folder.getFileName(), upBtn.getBounds().withX(upBtn.getRight() + 6).withWidth(getWidth() - upBtn.getRight() - 20), 9.5f, 2, Theme::txt());
    auto hint = tab == 0 ? U("click listens · drag onto a track · double-click: on the selected track") : tab == 1 ? U("click: the selected NOVA plays it") : tab == 2 ? U("click adds to the selected track · or drag it") : U("click opens (the piece now is saved first)");
    juce::ignoreUnused(hint);
}

void Browser::timerCallback() { if (host().previewing() != juce::File() && isShowing()) list.repaint(); }

void Browser::importFiles() {
    chooser = std::make_unique<juce::FileChooser>("Sound files for this piece", juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Downloads"),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf;*.aac");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [](const juce::FileChooser& fc) {
            auto rs = fc.getResults(); if (rs.isEmpty()) return;
            Track* t = host().selectedTrack(); Uid tid = t && t->kind == TrackKind::Audio ? t->id : 0;
            double beat = host().engine.position(); juce::String errs; int ok = 0;
            for (auto& f : rs) {
                auto e = host().importClip(f, tid, beat);
                if (e.isNotEmpty()) errs << f.getFileName() << ": " << e << "\n";
                else { ++ok; if (Clip* c = host().selectedClip()) { beat = c->end(); Track* ot = nullptr; P().clip(c->id, &ot); if (ot) tid = ot->id; } }
            }
            host().say(errs.isNotEmpty() ? errs.trim() : juce::String(ok) + (ok == 1 ? " sound" : " sounds") + U(" in the piece · they are copied into its Audio folder"));
        });
}

} // namespace score
