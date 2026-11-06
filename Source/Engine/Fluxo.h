#pragma once
// ARCO 2.5: FLUXO - make anything (instruments, effects, rhythms, generative music, controllers) from blocks you read like
// sentences: "Repeat every [eighth], [3 times], [45 %] back". The words in brackets are buttons. Three cables: SOUND,
// NOTES, MOVEMENT. Plug the wrong kind in and FLUXO puts the right translator on the cable by itself.
// Under it all runs the PATCHER (Max / MSP): every block is a little patch, and any Max object is a block too.
#include "Patcher.h"

namespace arco { namespace fx {

enum Kind { Sound = 0, Notes = 1, Move = 2, Any = 3 };
const char* kindName(Kind k);

struct Opt { std::string label, value; };
struct To { std::string box; int inlet = 0; std::string pat = "$1"; };     // where a word's value goes (pat: the message)
struct FieldDef {
    std::string id;
    char type = 'n';               // 'n' number, 'c' choice, 'k' a note (shown as C4), 's' words you type
    double lo = 0, hi = 1, def = 0, step = 0, mul = 1; std::string unit; bool expo = false;
    std::vector<Opt> opts; std::string defText;
    std::vector<To> to;
    bool bake = false;             // written into the boxes' text ({id}) - changing it rebuilds the block
    bool front = true;             // a plug-in can show it
};
struct PortDef { std::string id, label; Kind kind = Sound; std::string box; int io = 0; std::string boxR; int ioR = 0; bool stereo() const { return !boxR.empty(); } };
struct FluxDoc;
struct BlockDef {
    std::string id, shelf, name, sentence, help, words, tryThis;
    std::vector<FieldDef> fields; std::vector<PortDef> ins, outs;
    std::string body;              // the little patch: "obj id x y text" / "msg ..." / "- a o b i"; {field} = its value
    std::string ui, uiBox; int uiH = 0;   // a thing on the block you play with or watch (drawn by the screen)
    std::string port;              // a way in / out of a plug-in: "in" / "out"
    // a plug-in: a block made of blocks
    std::shared_ptr<FluxDoc> inner; std::vector<std::pair<int, std::string>> front; std::string colour;
    // building it (the library)
    BlockDef& in(const std::string& id, const std::string& label, Kind k, const std::string& box, int io = 0, const std::string& boxR = {}, int ioR = 0) { ins.push_back({ id, label, k, box, io, boxR, ioR }); return *this; }
    BlockDef& out(const std::string& id, const std::string& label, Kind k, const std::string& box, int io = 0, const std::string& boxR = {}, int ioR = 0) { outs.push_back({ id, label, k, box, io, boxR, ioR }); return *this; }
    BlockDef& num(const std::string& id, double lo, double hi, double def, const std::string& unit, std::vector<To> to, double mul = 1, double step = 0, bool expo = false) {
        FieldDef f; f.id = id; f.type = 'n'; f.lo = lo; f.hi = hi; f.def = def; f.unit = unit; f.to = std::move(to); f.mul = mul; f.step = step; f.expo = expo; fields.push_back(f); return *this; }
    BlockDef& note(const std::string& id, int def, std::vector<To> to, int lo = 12, int hi = 120) { FieldDef f; f.id = id; f.type = 'k'; f.lo = lo; f.hi = hi; f.def = def; f.step = 1; f.to = std::move(to); fields.push_back(f); return *this; }
    BlockDef& choice(const std::string& id, std::vector<Opt> opts, int def, std::vector<To> to) { FieldDef f; f.id = id; f.type = 'c'; f.opts = std::move(opts); f.def = def; f.lo = 0; f.hi = (double) f.opts.size() - 1; f.to = std::move(to); fields.push_back(f); return *this; }
    BlockDef& text(const std::string& id, const std::string& def, std::vector<To> to, bool bake = false) { FieldDef f; f.id = id; f.type = 's'; f.defText = def; f.to = std::move(to); f.bake = bake; fields.push_back(f); return *this; }
    BlockDef& baked() { if (!fields.empty()) fields.back().bake = true; return *this; }
    BlockDef& hidden() { if (!fields.empty()) fields.back().front = false; return *this; }
    BlockDef& patch(const std::string& b) { body = b; return *this; }
    BlockDef& widget(const std::string& w, const std::string& box, int h) { ui = w; uiBox = box; uiH = h; return *this; }
    BlockDef& tip(const std::string& t) { tryThis = t; return *this; }
    const FieldDef* field(const std::string& f) const { for (auto& x : fields) if (x.id == f) return &x; return nullptr; }
};
const std::vector<BlockDef>& library();
const BlockDef* findBlock(const std::string& id);
std::vector<std::string> shelves();
const std::vector<Opt>& timeOpts();       // sixteenth ... 4 bars
const std::vector<Opt>& scaleOpts();
const std::vector<Opt>& rootOpts();

// ------------------------------------------------------------------ the patch you build
struct Block {
    int id = 0; std::string type; float x = 0, y = 0;
    std::map<std::string, juce::var> f;          // the words you chose
    bool bypass = false;                         // "hear it without"
    juce::var state;                             // what its things keep (a slider, the steps you drew)
    juce::String text;                           // a Max box: what it is ("metro 8n"); a classic patch: its JSON
    std::shared_ptr<BlockDef> own;               // a plug-in (made of blocks) carried with the patch
    juce::String label;                          // your own name for it
};
struct Wire { int a = 0, out = 0, b = 0, in = 0; std::string field; };    // field: into a word of the block, not a way in
struct FluxDoc {
    juce::String name = "My Fluxo", notes;
    std::vector<Block> blocks; std::vector<Wire> wires;
    int nextId = 1; bool chain = true;
    Block* block(int id) { for (auto& b : blocks) if (b.id == id) return &b; return nullptr; }
    const Block* block(int id) const { for (auto& b : blocks) if (b.id == id) return &b; return nullptr; }
    int add(const std::string& type, float x, float y);
    void remove(int id);
    void connect(const Wire& w);
    juce::var toVar() const; static FluxDoc fromVar(const juce::var& v);
};
const BlockDef* defOf(const Block& b);           // its library entry, a Max box, a classic patch or a plug-in
juce::var valueOf(const Block& b, const FieldDef& f);
juce::String shown(const FieldDef& f, const juce::var& v);        // "eighth", "45 %", "C4"
std::string pxValue(const FieldDef& f, const juce::var& v);       // what goes into the patch
double toNorm(const FieldDef& f, const juce::var& v); juce::var fromNorm(const FieldDef& f, double t);
juce::String noteName(int n);
// a sentence cut into its pieces: words, and {field}s
struct Piece { bool field = false; juce::String text; std::string id; };
std::vector<Piece> pieces(const BlockDef& d);

// ------------------------------------------------------------------ into a patch the PATCHER runs
struct Target { std::string key; int inlet; std::string pat; };
struct Converter { int wire; juce::String name; };      // a translator FLUXO put on a cable
struct Compiled {
    px::PatchDoc doc;
    std::map<std::pair<int, std::string>, std::vector<Target>> fieldTargets;   // (block, word) -> where it goes now
    std::map<std::string, std::string> uiKey;            // block ("12", or "7/12" inside plug-in 7) -> the box its widget is
    std::vector<Converter> conv;
    std::map<int, juce::String> problems;                // block -> what is wrong
};
// device: the plug-in ways in / out become the track (adc~) and what goes on (dac~)
Compiled compile(const FluxDoc& d, bool device = false);
juce::String converterName(Kind from, Kind to, bool intoWord);
// the messages that set a word of a block right now (when you drag it: no rebuild)
std::vector<std::pair<std::string, px::Msg>> liveSet(const Compiled& c, const Block& b, const FieldDef& f, const juce::var& v);

// ------------------------------------------------------------------ "write what you want"
struct Recipe { juce::String name, says, words; juce::String text; };
const std::vector<Recipe>& recipes();
FluxDoc recipeDoc(const Recipe& r);                    // blocks: "b id type x y word=value ...", wires: "w a out b in" or "w a out b field"
struct Found { int recipe = -1; const BlockDef* block = nullptr; std::string pxClass; double score = 0; };
std::vector<Found> ask(const juce::String& what, int maxN = 12);
// a plug-in made from some blocks of a patch (the cables that cross become its ways in and out)
// outer: the cables of the patch that now go to the plug-in (its block is called -1 in them)
std::shared_ptr<BlockDef> makePlugin(const FluxDoc& d, const std::vector<int>& sel, const juce::String& name, const std::vector<std::pair<int, std::string>>& front, const juce::String& colour, std::vector<Wire>* outer);
juce::var pluginToVar(const BlockDef& p); std::shared_ptr<BlockDef> pluginFromVar(const juce::var& v);

}} // namespace arco::fx
