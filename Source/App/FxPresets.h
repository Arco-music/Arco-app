#pragma once
// ARCO 1.3: presets for every effect card, and "my settings for new songs".
//   presets:  Application Support/ARCO/fx-presets/<card>.json   { "name": { "param": value, ... }, ... }
//   defaults: Application Support/ARCO/fx-defaults.json          { "<card>": { "param": value, ... }, ... }
// A preset holds the card's knobs (not the strips' SEND - that belongs to the scene).
#include <JuceHeader.h>
#include "../Model/Model.h"

namespace arco::fxpresets {

inline juce::File folder() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO"); }
inline juce::File presetFile(const juce::String& card) { return folder().getChildFile("fx-presets").getChildFile(card + ".json"); }
inline juce::File defaultsFile() { return folder().getChildFile("fx-defaults.json"); }

// the parameters a card's preset holds
inline std::vector<std::string> paramsOf(const std::string& card) {
    std::vector<std::string> out;
    for (auto& c : cardTable()) if (card == c.key) for (auto& k : c.knobs) if (std::string(k.id) != "SEND") out.push_back(k.id);
    if (card == "frz") out.push_back("frz_on");
    if (card == "shd") for (int v = 0; v < 12; ++v) out.push_back("shd_v" + std::to_string(v));
    return out;
}
inline juce::var readJson(const juce::File& f) { auto v = juce::JSON::parse(f); return v.isObject() ? v : juce::var(new juce::DynamicObject()); }
inline void writeJson(const juce::File& f, const juce::var& v) { f.getParentDirectory().createDirectory(); f.replaceWithText(juce::JSON::toString(v)); }

inline juce::var capture(const Model& m, const std::string& card) {
    auto* o = new juce::DynamicObject();
    for (auto& id : paramsOf(card)) o->setProperty(juce::Identifier(id), m.get(id));
    return juce::var(o);
}
// apply values: the song settings, and scene settings in every scene (so a new song starts the same everywhere)
inline void applyValues(Model& m, const juce::var& vals, bool allScenes) {
    if (auto* o = vals.getDynamicObject())
        for (auto& p : o->getProperties()) {
            auto id = p.name.toString().toStdString();
            auto* d = m.def(id); if (d == nullptr) continue;
            double v = juce::jlimit(d->lo, d->hi, (double) p.value);
            if (allScenes && d->scope == 0) for (int s = 1; s <= 4; ++s) m.setInScene(s, id, v);
            m.set(id, v);
        }
}

inline juce::StringArray names(const juce::String& card) {
    juce::StringArray n; if (auto* o = readJson(presetFile(card)).getDynamicObject()) for (auto& p : o->getProperties()) n.add(p.name.toString());
    n.sortNatural(); return n;
}
inline void save(const Model& m, const juce::String& card, const juce::String& name) {
    auto all = readJson(presetFile(card));
    all.getDynamicObject()->setProperty(juce::Identifier(name), capture(m, card.toStdString()));
    writeJson(presetFile(card), all);
}
inline void load(Model& m, const juce::String& card, const juce::String& name) { applyValues(m, readJson(presetFile(card))[juce::Identifier(name)], false); }
inline void remove(const juce::String& card, const juce::String& name) {
    auto all = readJson(presetFile(card)); all.getDynamicObject()->removeProperty(juce::Identifier(name)); writeJson(presetFile(card), all); }

// "my settings for new songs"
inline bool hasDefault(const juce::String& card) { return readJson(defaultsFile()).getDynamicObject()->hasProperty(juce::Identifier(card)); }
inline void setDefault(const Model& m, const juce::String& card) {
    auto all = readJson(defaultsFile()); all.getDynamicObject()->setProperty(juce::Identifier(card), capture(m, card.toStdString())); writeJson(defaultsFile(), all); }
inline void clearDefault(const juce::String& card) {
    auto all = readJson(defaultsFile()); all.getDynamicObject()->removeProperty(juce::Identifier(card)); writeJson(defaultsFile(), all); }
inline void applyDefaults(Model& m) {
    if (auto* o = readJson(defaultsFile()).getDynamicObject()) for (auto& p : o->getProperties()) applyValues(m, p.value, true);
}
// the factory settings of a card
inline void factory(Model& m, const std::string& card) { for (auto& id : paramsOf(card)) if (auto* d = m.def(id)) m.set(id, d->def); }

} // namespace arco::fxpresets
