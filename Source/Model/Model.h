#pragma once
// ARCO model: the value of every control, the per-scene memory, button counters, and saving.
#include <JuceHeader.h>
#include <map>
#include <array>
#include "ParamTable.h"
#include "../Engine/Engine.h"

namespace arco {

class Model {
public:
    explicit Model(Engine& e);
    Engine& engine;

    const ParamDef* def(const std::string& id) const;
    double get(const std::string& id) const;
    void set(const std::string& id, double v);            // store + send to the engine (and the scene memory follows on leaving)
    void sendAll();                                        // after the engine was (re)started
    // buttons: the engine reacts to a changed value, so every press sends a new number
    void press(const std::string& dest, const std::string& name, int mul = 1, int add = 0);
    // the strip / effect amount names (HALO has no amount on LIVE 2-4)
    static std::string stripParam(int strip, const std::string& key);

    int selected = 0;
    void select(int s) { selected = s; engine.selStrip.store(s); }

    // called ~30 times a second by the interface: follows scene changes (store the old scene, recall the new)
    void tick();
    int sceneNow = 1;                                      // 1-4

    // song / setup files
    juce::var songState();                                 // song values + 4 scenes
    void loadSongState(const juce::var& v);
    juce::var setupState();
    void loadSetupState(const juce::var& v);
    void resetScenes();
    void resetSong();                                      // song + scene settings back to their defaults

private:
    std::map<std::string, double> vals;
    std::map<std::string, const ParamDef*> defs;
    std::array<std::map<std::string, double>, 4> sceneMem;
    std::map<std::string, int> counters;
    void send(const ParamDef& d, double v);
    void storeScene(int sc);
    void recallScene(int sc);
};

} // namespace arco
