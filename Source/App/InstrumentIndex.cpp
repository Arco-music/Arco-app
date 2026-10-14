#include "InstrumentIndex.h"
#include "Instruments.h"

namespace arco {

static juce::String fold(juce::String s) {
    s = s.toLowerCase();
    const char* from[] = { "ã", "á", "â", "à", "é", "ê", "í", "ó", "ô", "õ", "ú", "ç", "ä", "ö", "ü" };
    const char* to[] = { "a", "a", "a", "a", "e", "e", "i", "o", "o", "o", "u", "c", "a", "o", "u" };
    for (int i = 0; i < 15; ++i) s = s.replace(juce::String::fromUTF8(from[i]), to[i]);
    return s;
}

const InstrumentIndex& InstrumentIndex::get() {
    static const InstrumentIndex idx = [] {
        InstrumentIndex x;
        size_t n = 0; auto* d = instrumentIndexData(n);
        juce::MemoryInputStream mi(d, n, false);
        juce::GZIPDecompressorInputStream z(&mi, false, juce::GZIPDecompressorInputStream::zlibFormat);
        juce::MemoryBlock mb; z.readIntoMemoryBlock(mb);
        auto text = juce::String::fromUTF8((const char*) mb.getData(), (int) mb.getSize());
        auto lines = juce::StringArray::fromLines(text);
        for (auto& l : lines) {
            if (l.length() < 2) continue;
            auto c = l[0];
            if (c == 'L') {
                auto p = juce::StringArray::fromTokens(l, "|", ""); while (p.size() < 9) p.add({});
                Lib lib; lib.repo = p[1]; lib.branch = p[2]; lib.name = p[3]; lib.author = p[4]; lib.licence = p[5]; lib.category = p[6]; lib.site = p[7]; lib.about = p[8];
                x.libs.push_back(std::move(lib));
            } else if (c == 'F' && !x.libs.empty()) x.libs.back().files.add(l.substring(2));
            else if (c == 'P' && !x.libs.empty()) {
                auto p = juce::StringArray::fromTokens(l, "|", ""); if (p.size() < 8) continue;
                Entry e; e.lib = (int) x.libs.size() - 1; e.name = p[1]; e.art = p[2]; e.category = p[3]; e.tags = p[4]; e.sizeMB = p[5].getIntValue(); e.samples = p[6].getIntValue();
                for (auto& t : juce::StringArray::fromTokens(p[7], ",", "")) e.files.push_back(t.getIntValue());
                auto& L = x.libs.back();
                e.lower = fold(e.name + " " + e.art + " " + e.category + " " + L.name + " " + L.author + " " + e.tags);
                x.entries.push_back(std::move(e));
            }
        }
        return x;
    }();
    return idx;
}

std::vector<int> InstrumentIndex::search(const juce::String& query, int maxResults) const {
    auto q = fold(query).trim();
    if (q.isEmpty()) return recommended();
    auto words = juce::StringArray::fromTokens(q, " ,", "");
    words.removeEmptyStrings();
    // "ensemble / section / orchestra" and "solo" steer the order rather than filter
    bool wantSection = false, wantSolo = false;
    juce::StringArray must;
    for (auto& w : words) {
        if (w == "ensemble" || w == "section" || w == "sections" || w == "group" || w == "orchestra" || w == "naipe" || w == "gruppe") wantSection = true;
        else if (w == "solo") wantSolo = true;
        else must.add(w.endsWith("s") && w.length() > 4 && w != "bass" && w != "brass" ? w.dropLastCharacters(1) : w);
    }
    if (must.isEmpty() && (wantSection || wantSolo)) must.add(wantSection ? "section" : "solo");
    struct S { int e; double s; };
    std::vector<S> hits;
    static const char* ART[] = { "sustain", "legato", "", "pizzicato", "staccato", "marcato", "spiccato", "tremolo", "harmonics" };
    for (int i = 0; i < (int) entries.size(); ++i) {
        auto& e = entries[(size_t) i];
        double s = 0; bool all = true;
        auto nm = fold(e.name);
        for (auto& w : must) {
            if (!e.lower.contains(w)) { all = false; break; }
            if (nm.startsWith(w) || nm.contains(" " + w)) s += 10; else if (nm.contains(w)) s += 7;
            else if (fold(e.art).contains(w)) s += 5; else if (fold(libs[(size_t) e.lib].name).contains(w)) s += 3; else s += 1.5;
        }
        if (!all) continue;
        bool section = e.name.containsIgnoreCase("section") || e.name.startsWith("Celli") || e.name.startsWith("All ") || e.name.endsWith("s") || e.name.containsIgnoreCase("chorus");
        bool solo = e.name.startsWith("Solo");
        if (wantSection && section) s += 6; if (wantSection && solo) s -= 3;
        if (wantSolo && solo) s += 6;
        auto art = fold(e.art);
        for (int a = 0; a < 9; ++a) if (art.startsWith(ART[a]) && (a != 2 || art.isEmpty())) { s += (9 - a) * 0.3; break; }
        if (art.contains("loop")) s -= 0.2;
        if (installed(i)) s += 0.5;
        s -= std::log10(1.0 + e.sizeMB) * 0.15;                  // smaller first among equals
        hits.push_back({ i, s });
    }
    std::stable_sort(hits.begin(), hits.end(), [](const S& a, const S& b) { return a.s > b.s; });
    std::vector<int> out; for (auto& h : hits) { out.push_back(h.e); if ((int) out.size() >= maxResults) break; }
    return out;
}

std::vector<int> InstrumentIndex::recommended() const {
    static const char* picks[][2] = { { "Splendid Grand Piano", "" }, { "Solo Cello", "Sustain" }, { "Celli (cello section)", "Sustain" }, { "Solo Violin 2", "Sustain" },
        { "All Strings", "Sustain" }, { "Solo Flute 1", "Sustain" }, { "Concert Harp", "" }, { "Mixed Chorus", "" }, { "jRhodes3c", "jRhodes stereo looped" },
        { "Solo Cello", "Pizzicato" }, { "Solo Clarinet", "Sustain" }, { "Grand Piano", "" }, { "Celeste", "" }, { "Vibraphone", "" } };
    std::vector<int> out;
    for (auto& p : picks)
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].name == p[0] && (p[1][0] == 0 || entries[(size_t) i].art == p[1])) { out.push_back(i); break; }
    return out;
}

juce::String InstrumentIndex::title(int e) const { auto& x = entries[(size_t) e]; return x.art.isNotEmpty() ? x.name + juce::String::fromUTF8(" · ") + x.art : x.name; }
juce::String InstrumentIndex::source(int e) const { auto& L = libs[(size_t) entries[(size_t) e].lib]; return (L.name == entries[(size_t) e].name ? L.author : L.name) + juce::String::fromUTF8(" · ") + L.site; }
juce::File InstrumentIndex::folder(int e) const { return InstrumentRack::instrumentsFolder().getChildFile("Library").getChildFile(libs[(size_t) entries[(size_t) e].lib].repo.replaceCharacter('/', '_')); }
juce::File InstrumentIndex::sfz(int e) const { auto& x = entries[(size_t) e]; return folder(e).getChildFile(libs[(size_t) x.lib].files[x.files.front()]); }
bool InstrumentIndex::installed(int e) const { auto f = sfz(e); return f.getSiblingFile(f.getFileName() + ".arco-ok").existsAsFile(); }
juce::String InstrumentIndex::url(int e, int file) const {
    auto& L = libs[(size_t) entries[(size_t) e].lib];
    juce::StringArray parts = juce::StringArray::fromTokens(L.files[file], "/", "");
    for (auto& p : parts) p = juce::URL::addEscapeChars(p, false);
    return "https://raw.githubusercontent.com/" + L.repo + "/" + L.branch + "/" + parts.joinIntoString("/");
}
juce::String InstrumentIndex::key(int e) const { auto& x = entries[(size_t) e]; return libs[(size_t) x.lib].repo + ":" + libs[(size_t) x.lib].files[x.files.front()]; }
int InstrumentIndex::find(const juce::String& k) const { for (int i = 0; i < (int) entries.size(); ++i) if (key(i) == k) return i; return -1; }

} // namespace arco
