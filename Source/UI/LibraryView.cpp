// ARCO 2.6: SESSIONS - the library of the sessions saved inside ARCO.
#include "LibraryView.h"
#include "MainView.h"
#include "../App/AudioHost.h"
#include <map>
#include "Sheets.h"

namespace arco {

// ================================================================== the sessions on disk
namespace sessions {
juce::File dir() {
   #if JUCE_IOS
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ARCO Songs");
   #else
    auto d = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("ARCO Songs");
   #endif
    d.createDirectory(); return d;
}
static juce::File settingsOf(const juce::File& folder) { auto fs = folder.findChildFiles(juce::File::findFiles, false, "*-settings.json"); return fs.isEmpty() ? juce::File() : fs[0]; }
bool isSession(const juce::File& folder) { return folder.isDirectory() && settingsOf(folder).existsAsFile(); }
juce::String suggestName(double bpm) {
    int top = 0;
    for (auto& f : dir().findChildFiles(juce::File::findDirectories, false)) { auto n = f.getFileName(); if (n.startsWith("ARC-")) top = juce::jmax(top, n.substring(4).getIntValue()); }
    juce::String b = std::abs(bpm - std::round(bpm)) < 0.05 ? juce::String((int) std::lround(bpm)) : juce::String(bpm, 1);
    return "ARC-" + juce::String(top + 1).paddedLeft('0', 3) + juce::String::fromUTF8(" · ") + b + " BPM";
}
juce::File uniqueFolder(const juce::String& name) {
    auto legal = juce::File::createLegalFileName(name.trim()); if (legal.isEmpty()) legal = "Session";
    auto f = dir().getChildFile(legal); for (int i = 2; f.exists(); ++i) f = dir().getChildFile(legal + " " + juce::String(i));
    return f;
}
// the files of a song begin with its name ("ARC-007-settings.json", "ARC-007-A.wav"): they follow a new name
static void renameInside(const juce::File& folder, const juce::String& oldBase, const juce::String& newBase) {
    if (oldBase == newBase) return;
    for (auto& f : folder.findChildFiles(juce::File::findFiles, false))
        if (f.getFileName().startsWith(oldBase + "-")) f.moveFileTo(folder.getChildFile(newBase + f.getFileName().substring(oldBase.length())));
}
static juce::String baseOf(const juce::File& folder) { auto s = settingsOf(folder); return s.getFileName().upToLastOccurrenceOf("-settings.json", false, false); }
bool rename(const juce::File& folder, const juce::String& newName, juce::File& result) {
    auto legal = juce::File::createLegalFileName(newName.trim()); if (legal.isEmpty()) return false;
    auto target = folder.getSiblingFile(legal);
    if (target == folder) { result = folder; return true; }
    if (target.exists()) return false;
    auto base = baseOf(folder);
    if (!folder.moveFileTo(target)) return false;
    renameInside(target, base, legal); result = target; return true;
}
bool duplicate(const juce::File& folder, juce::File& result) {
    auto target = uniqueFolder(folder.getFileName() + " copy"); auto base = baseOf(folder);
    if (!folder.copyDirectoryTo(target)) return false;
    renameInside(target, base, target.getFileName()); result = target; return true;
}
std::vector<Info> list() {
    static std::map<juce::String, std::pair<juce::Time, Info>> cache;      // a song's file is read again only when it changes
    std::vector<Info> v;
    for (auto& f : dir().findChildFiles(juce::File::findDirectories, false)) {
        auto st = settingsOf(f); if (!st.existsAsFile()) continue;
        auto mod = st.getLastModificationTime(); auto key = st.getFullPathName();
        auto it = cache.find(key);
        if (it == cache.end() || it->second.first != mod) {
            Info in; in.folder = f; in.name = f.getFileName(); in.saved = mod; in.created = f.getCreationTime();
            if (st.getSize() < 64 * 1024 * 1024) { auto j = juce::JSON::parse(st); auto m = j["meta"];
                if (m.isObject()) { in.saved = juce::Time::fromISO8601(m["saved"].toString()); if (in.saved.toMilliseconds() == 0) in.saved = mod;
                    auto c = juce::Time::fromISO8601(m["created"].toString()); if (c.toMilliseconds() != 0) in.created = c;
                    in.bpm = m["bpm"]; in.beats = m.getProperty("beats", 4); in.den = m.getProperty("den", 4); in.loops = m["loops"]; in.clips = m["clips"]; in.tracks = m["tracks"]; } }
            cache[key] = { mod, in }; it = cache.find(key);
        }
        auto in = it->second.second; in.folder = f; in.name = f.getFileName(); v.push_back(in);
    }
    std::sort(v.begin(), v.end(), [](const Info& a, const Info& b) { return a.saved > b.saved; });
    return v;
}
} // namespace sessions

static void ltxt(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, float fs, juce::Colour c, int w = 0, juce::Justification j = juce::Justification::centredLeft, int lines = 1) {
    g.setColour(c); g.setFont(Theme::font(fs, w)); g.drawFittedText(t, r, j, lines, 0.9f); }
static juce::String when(juce::Time t) {
    auto now = juce::Time::getCurrentTime(); auto day = [](juce::Time x) { return x.getYear() * 400 + x.getDayOfYear(); };
    auto hm = t.formatted("%H:%M");
    if (day(t) == day(now)) return "Today, " + hm;
    if (day(t) == day(now - juce::RelativeTime::days(1))) return "Yesterday, " + hm;
    return t.formatted("%d %b %Y, ") + hm;
}

// ================================================================== the cards
struct LibraryView::Grid : public juce::Component {
    LibraryView& v; std::vector<sessions::Info> all, shown; int hover = -1;
    std::vector<juce::Rectangle<int>> cards;
    explicit Grid(LibraryView& lv) : v(lv) {}
    static constexpr int CW = 330, CH = 176, GAP = 22;
    int columns() const { return juce::jmax(1, (getWidth() + GAP) / (CW + GAP)); }
    void layoutCards(int width) {
        const int M = 22;   // room for the cards' shades: never cut at the edge
        cards.clear(); int inner = juce::jmax(CW, width - 2 * M); int cols = juce::jmax(1, (inner + GAP) / (CW + GAP)); int cw = (inner - (cols - 1) * GAP) / cols;
        for (int i = 0; i < (int) shown.size(); ++i) cards.push_back({ M + (i % cols) * (cw + GAP), M + (i / cols) * (CH + GAP), cw, CH });
        setSize(width, juce::jmax(400, M + (int) ((shown.size() + cols - 1) / cols) * (CH + GAP) + 40));
    }
    juce::Rectangle<int> moreR(int i) const { auto r = cards[(size_t) i]; return { r.getRight() - 52, r.getY() + 12, 38, 30 }; }
    void paint(juce::Graphics& g) override {
        if (shown.empty()) {
            auto r = getLocalBounds().withHeight(260).reduced(20, 40);
            if (all.empty()) {
                ltxt(g, "No sessions yet", r.removeFromTop(40), 17.0f, Theme::txt(), 2, juce::Justification::centred);
                ltxt(g, juce::String::fromUTF8("Press Save  ›  Save session, and the song comes here.\nEvery session keeps its loops, scenes, drums, instruments and the STUDIO arrangement."), r, 10.0f, Theme::sub(), 0, juce::Justification::centredTop, 3);
            } else ltxt(g, "No session has that name.", r, 12.0f, Theme::sub(), 1, juce::Justification::centred);
            return;
        }
        auto open = v.shell.host.songFolder;
        for (int i = 0; i < (int) shown.size(); ++i) {
            auto& s = shown[(size_t) i]; auto r = cards[(size_t) i]; auto rf = r.toFloat();
            bool isOpen = open.isDirectory() && open == s.folder; auto acc = Theme::blue();
            if (Theme::softUI()) softRaised(g, rf, 22, i == hover ? 6.0f : 4.0f, Theme::card()); else { g.setColour(Theme::card()); g.fillRoundedRectangle(rf, 14); }
            if (isOpen) { g.setColour(acc.withAlpha(0.8f)); g.drawRoundedRectangle(rf.reduced(1.5f), 21, 2.0f); }
            auto in = r.reduced(22, 18);
            // the four loops (A B C D): lit when the session has them
            for (int k = 0; k < 4; ++k) { auto lr = juce::Rectangle<float>((float) in.getX() + k * 22, (float) in.getY() + 4, 16, 16);
                if (k < s.loops) { g.setColour(Theme::pal().acc[(size_t) (k % 8)]); g.fillRoundedRectangle(lr, 5); }
                else { if (Theme::softUI()) softWellLite(g, lr, 5); else { g.setColour(Theme::line()); g.drawRoundedRectangle(lr, 5, 1); } } }
            if (isOpen) ltxt(g, "OPEN NOW", { in.getX() + 98, in.getY() + 2, 90, 20 }, 7.6f, Theme::ink(acc), 2);
            // the "..." button
            auto mr = moreR(i); if (Theme::softUI()) softKeyLite(g, mr.toFloat(), 12, false); else { g.setColour(Theme::btn()); g.fillRoundedRectangle(mr.toFloat(), 8); }
            g.setColour(Theme::sub()); for (int d = 0; d < 3; ++d) g.fillEllipse((float) mr.getCentreX() - 9 + d * 7, (float) mr.getCentreY() - 2, 4, 4);
            ltxt(g, s.name, { in.getX(), in.getY() + 34, in.getWidth(), 30 }, 13.0f, Theme::txt(), 2);
            ltxt(g, when(s.saved), { in.getX(), in.getY() + 66, in.getWidth(), 20 }, 9.0f, Theme::sub(), 0);
            // what is in it
            juce::StringArray tags;
            if (s.bpm > 0) tags.add((std::abs(s.bpm - std::round(s.bpm)) < 0.05 ? juce::String((int) std::lround(s.bpm)) : juce::String(s.bpm, 1)) + " BPM");
            tags.add(juce::String(s.beats) + "/" + juce::String(s.den));
            tags.add(s.loops == 0 ? juce::String("no loops") : s.loops == 1 ? juce::String("1 loop") : juce::String(s.loops) + " loops");
            if (s.clips > 0) tags.add("STUDIO " + juce::String(s.clips) + (s.clips == 1 ? " clip" : " clips"));
            int tx = in.getX(), ty = in.getBottom() - 30;
            for (auto& t : tags) { int w = (int) juce::GlyphArrangement::getStringWidth(Theme::font(8.2f, 2), t) + 22; if (tx + w > in.getRight()) break;
                auto tr = juce::Rectangle<int>(tx, ty, w, 28); if (Theme::softUI()) softWellLite(g, tr.toFloat(), 14); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(tr.toFloat(), 10); }
                ltxt(g, t, tr, 8.2f, Theme::txt().withAlpha(0.85f), 2, juce::Justification::centred); tx += w + 8; }
        }
    }
    int cardAt(juce::Point<int> p) const { for (int i = 0; i < (int) cards.size(); ++i) if (cards[(size_t) i].contains(p)) return i; return -1; }
    void mouseMove(const juce::MouseEvent& e) override { int h = cardAt(e.getPosition()); if (h != hover) { hover = h; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        int i = cardAt(e.getPosition()); if (i < 0) return;
        if (e.mods.isPopupMenu() || moreR(i).contains(e.getPosition())) { menu(i); return; }
        openSession(shown[(size_t) i].folder);
    }
    void openSession(const juce::File& f) {
        auto& sh = v.shell; juce::Component::SafePointer<LibraryView> safe(&v);
        if (sh.host.songFolder == f && f.isDirectory()) { sh.show(Screen::Workstation); return; }
        sh.canvas.askSaveThen([safe, f] { if (!safe) return; auto& s = safe->shell;
            if (s.host.openSong(f)) s.show(Screen::Workstation); else safe->repaint(); });
    }
    void menu(int i) {
        auto s = shown[(size_t) i]; juce::PopupMenu m;
        m.addItem(1, "Open"); m.addItem(2, juce::String::fromUTF8("Rename…")); m.addItem(3, "Duplicate");
        m.addItem(4, "Show in Finder"); m.addSeparator(); m.addItem(5, juce::String::fromUTF8("Move to the Bin…"));
        juce::Component::SafePointer<Grid> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe, s](int r) { if (!safe) return; auto& lv = safe->v; auto& host = lv.shell.host;
            if (r == 1) safe->openSession(s.folder);
            if (r == 2) {
                auto* w = new juce::AlertWindow("Rename the session", "Its new name:", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor("name", s.name); w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                juce::Component::SafePointer<LibraryView> sl(&lv);
                w->enterModalState(true, juce::ModalCallbackFunction::create([sl, w, s](int rr) { if (rr != 1 || !sl) return; auto& h = sl->shell.host;
                    juce::File res; bool wasOpen = h.songFolder == s.folder;
                    if (sessions::rename(s.folder, w->getTextEditorContents("name"), res)) { if (wasOpen) h.songFolder = res; h.status = "renamed \"" + res.getFileName() + "\""; }
                    else h.status = "a session with that name already exists";
                    sl->refresh(); }), true);
            }
            if (r == 3) { juce::File res; if (sessions::duplicate(s.folder, res)) host.status = "made \"" + res.getFileName() + "\""; lv.refresh(); }
            if (r == 4) s.folder.revealToUser();
            if (r == 5) {
                juce::Component::SafePointer<LibraryView> sl(&lv);
                auto opts = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Move \"" + s.name + "\" to the Bin?")
                                .withMessage("You can still take it back out of the Bin.").withButton("Move to the Bin").withButton("Cancel");
                juce::AlertWindow::showAsync(opts, [sl, s](int rr) { if (rr != 1 || !sl) return; auto& h = sl->shell.host;
                    bool wasOpen = h.songFolder == s.folder;
                    if (s.folder.moveToTrash()) { if (wasOpen) h.songFolder = juce::File(); h.status = "\"" + s.name + "\" is in the Bin"; }
                    sl->refresh(); });
            }
        });
    }
};

// ================================================================== the screen
LibraryView::LibraryView(AppShell& s) : shell(s) {
    getProperties().set("softShades", true);
    grid = std::make_unique<Grid>(*this); vp.setViewedComponent(grid.get(), false); vp.setScrollBarsShown(true, false); vp.setScrollBarThickness(8); addAndMakeVisible(vp);
    addAndMakeVisible(search);
    search.setFont(Theme::font(11.0f, 0)); search.setTextToShowWhenEmpty("Find a session by its name", Theme::sub());
    search.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack); search.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack); search.setColour(juce::TextEditor::textColourId, Theme::txt()); search.setIndents(4, 9);
    search.setComponentID("sessions:find");
    search.onTextChange = [this] { refresh(); };
    auto mk = [&](const juce::String& t, std::function<void()> f, const char* id) { auto* p = new Pill(t, [] { return Theme::btn(); }, 9.0f, 8.0f); p->onClick = std::move(f); p->setComponentID(juce::String("sessions:") + id); owned.add(p); addAndMakeVisible(p); return p; };
    mk("Save this session", [this] { shell.canvas.saveSession([this] { refresh(); }); }, "save");
    mk("New session", [this] { shell.canvas.command(cmdNewSong); }, "new");
    mk(juce::String::fromUTF8("Open from your computer…"), [this] { shell.canvas.askSaveThen([this] { shell.canvas.doOpen(); }); }, "open");
    mk("Show in Finder", [] { sessions::dir().revealToUser(); }, "finder");
    startTimer(1500);
}
LibraryView::~LibraryView() { vp.setViewedComponent(nullptr, false); }
void LibraryView::refresh() {
    grid->all = sessions::list(); grid->shown.clear();
    auto q = search.getText().trim().toLowerCase();
    for (auto& s : grid->all) if (q.isEmpty() || s.name.toLowerCase().contains(q)) grid->shown.push_back(s);
    grid->layoutCards(vp.getWidth() - 12); lastScan = juce::Time::currentTimeMillis(); grid->repaint(); repaint();
}
void LibraryView::visibilityChanged() { if (isVisible()) refresh(); }
void LibraryView::timerCallback() { if (isVisible()) refresh(); }
void LibraryView::paint(juce::Graphics& g) {
    Theme::backdrop(g, getLocalBounds());
    drawSoftShades(g, *this);
    ltxt(g, "SESSIONS", { headR.getX(), headR.getY(), 400, 30 }, 15.0f, Theme::txt(), 2);
    int n = (int) grid->all.size();
    ltxt(g, (n == 0 ? juce::String("No sessions yet") : n == 1 ? juce::String("1 session") : juce::String(n) + " sessions") + juce::String::fromUTF8("  ·  saved inside ARCO, in Music › ARCO Songs"),
         { headR.getX(), headR.getY() + 30, 600, 20 }, 8.6f, Theme::sub(), 0);
    if (Theme::softUI()) softWellLite(g, searchR.toFloat(), (float) searchR.getHeight() * 0.5f); else { g.setColour(Theme::scr()); g.fillRoundedRectangle(searchR.toFloat(), 12); }
    auto c = juce::Point<float>((float) searchR.getX() + 22, (float) searchR.getCentreY()); g.setColour(Theme::sub());
    g.drawEllipse(c.x - 7, c.y - 7, 12, 12, 1.8f); g.drawLine(c.x + 3.5f, c.y + 3.5f, c.x + 8, c.y + 8, 1.8f);
}
void LibraryView::resized() {
    int W = getWidth(), H = getHeight();
    headR = { 32, 18, W - 64, 56 };
    int x = W - 32; auto comp = [&](const char* id) -> juce::Component* { for (auto* c : owned) if (c->getComponentID() == juce::String("sessions:") + id) return c; return nullptr; };
    bool narrow = W < 1240;
    int by = narrow ? headR.getBottom() + 4 : headR.getY() + 8;
    for (auto* id : { "finder", "open", "new", "save" }) { int w = juce::String(id) == "open" ? 230 : juce::String(id) == "save" ? 170 : juce::String(id) == "new" ? 140 : 150;
        x -= w; comp(id)->setBounds(x, by, w, 40); x -= 10; }
    int sy = (narrow ? by + 52 : headR.getBottom() + 10);
    searchR = { 32, sy, juce::jmin(520, W - 64), 42 };
    search.setBounds(searchR.withTrimmedLeft(40).withTrimmedRight(14));
    vp.setBounds(10, searchR.getBottom() + 4, W - 20, H - searchR.getBottom() - 10);
    refresh();
}

} // namespace arco
