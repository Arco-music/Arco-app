#include "Model.h"

namespace arco {

Model::Model(Engine& e) : engine(e) {
    for (auto& d : paramTable()) { defs[d.id] = &d; vals[d.id] = d.def; }
}

const ParamDef* Model::def(const std::string& id) const { auto it = defs.find(id); return it == defs.end() ? nullptr : it->second; }
double Model::get(const std::string& id) const { auto it = vals.find(id); return it == vals.end() ? 0.0 : it->second; }

std::string Model::stripParam(int s, const std::string& key) {
    if (key == "halo") {
        if (s == 0) return "halo0";
        if (s < 4) return "";
        return "halo" + std::to_string(s - 3);
    }
    return key + std::to_string(s);
}

void Model::send(const ParamDef& d, double v) {
    for (auto& dest : d.dests) engine.set(dest, d.id, v);
    std::string id = d.id;
    if (id == "spk_n") engine.set("st_orb", "orb_multi", v > 2 ? 1 : 0);
}

void Model::set(const std::string& id, double v) {
    auto* d = def(id); if (d == nullptr) return;
    v = juce::jlimit(d->lo, d->hi, v);
    if (d->kind != 0) v = std::round(v);
    vals[id] = v;
    send(*d, v);
}

void Model::sendAll() {
    for (auto& d : paramTable()) send(d, vals[d.id]);
}

void Model::press(const std::string& dest, const std::string& name, int mul, int add) {
    int& c = counters[dest + "/" + name];
    c += 1;
    engine.set(dest, name, (double) (c * mul + add));
}

void Model::storeScene(int sc) {
    auto& m = sceneMem[(size_t) (sc - 1)]; m.clear();
    for (auto& d : paramTable()) if (d.scope == 0) m[d.id] = vals[d.id];
}
void Model::recallScene(int sc) {
    auto& m = sceneMem[(size_t) (sc - 1)];
    for (auto& d : paramTable()) {
        if (d.scope != 0) continue;
        auto it = m.find(d.id);
        double v = it == m.end() ? d.def : it->second;
        if (v != vals[d.id]) { vals[d.id] = v; send(d, v); }
    }
}
void Model::resetScenes() { for (auto& m : sceneMem) m.clear(); }

void Model::tick() {
    int sc = (int) engine.snap("scenes").v.load() % 8;
    if (sc >= 1 && sc <= 4 && sc != sceneNow) {
        storeScene(sceneNow);
        recallScene(sc);
        sceneNow = sc;
    }
}

juce::var Model::songState() {
    storeScene(sceneNow);
    auto* root = new juce::DynamicObject();
    auto* song = new juce::DynamicObject();
    for (auto& d : paramTable()) if (d.scope == 1) song->setProperty(juce::Identifier(d.id), vals[d.id]);
    root->setProperty("song", juce::var(song));
    juce::Array<juce::var> scenes;
    for (int i = 0; i < 4; ++i) {
        auto* o = new juce::DynamicObject();
        for (auto& kv : sceneMem[(size_t) i]) o->setProperty(juce::Identifier(kv.first), kv.second);
        scenes.add(juce::var(o));
    }
    root->setProperty("scenes", scenes);
    return juce::var(root);
}

void Model::loadSongState(const juce::var& v) {
    if (auto* song = v["song"].getDynamicObject())
        for (auto& p : song->getProperties()) set(p.name.toString().toStdString(), (double) p.value);
    resetScenes();
    if (auto* arr = v["scenes"].getArray())
        for (int i = 0; i < juce::jmin(4, arr->size()); ++i)
            if (auto* o = (*arr)[i].getDynamicObject())
                for (auto& p : o->getProperties()) sceneMem[(size_t) i][p.name.toString().toStdString()] = (double) p.value;
    sceneNow = 1;
    recallScene(1);
}

juce::var Model::setupState() {
    auto* o = new juce::DynamicObject();
    for (auto& d : paramTable()) if (d.scope == 2) o->setProperty(juce::Identifier(d.id), vals[d.id]);
    return juce::var(o);
}
void Model::loadSetupState(const juce::var& v) {
    if (auto* o = v.getDynamicObject())
        for (auto& p : o->getProperties()) set(p.name.toString().toStdString(), (double) p.value);
}

} // namespace arco
