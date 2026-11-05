#pragma once
// ARCO 2.4: PATCHER - write your own programs, effects and instruments by connecting objects, the way Max / MSP does it.
// Objects are typed as text ("cycle~ 440", "metro 4n", "+ 1"), messages travel right to left, the left inlet is hot,
// signals (~) run every sample. It runs on the audio thread: the control world first, then the signal world, each block.
//
//  PatchDoc   the patch as you see it (boxes, cables) - message thread, saved as JSON
//  Program    a compiled patch: nodes, cables in order, the signal chain - swapped in for the audio thread
//  Runtime    one running patcher (the PATCHER screen has one, every STUDIO device has its own)
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <string>

namespace arco { namespace px {

// ------------------------------------------------------------------ symbols (interned once, an int after)
int sym(const std::string& s);
const std::string& symStr(int s);
namespace S { extern int bang, intS, floatS, list, symbol, set, clear, reset, stop, start, next, prev, size, dump, read, write, clearS, empty; }

// ------------------------------------------------------------------ atoms and messages
struct Atom {
    double v = 0; int s = 0;
    char t = 'i';            // 'i' int, 'f' float, 's' symbol (16 bytes: a message of 64 is 1 kB on the stack)
    static Atom I(double x) { Atom a; a.t = 'i'; a.v = (double) (long long) x; return a; }
    static Atom F(double x) { Atom a; a.t = 'f'; a.v = x; return a; }
    static Atom Sy(int s) { Atom a; a.t = 's'; a.s = s; return a; }
    static Atom Sy(const std::string& s) { return Sy(sym(s)); }
    bool num() const { return t != 's'; }
    double d() const { return t == 's' ? 0.0 : v; }
    long long i() const { return t == 's' ? 0 : (long long) v; }
    std::string str() const;
    bool operator==(const Atom& o) const { return t == 's' ? (o.t == 's' && s == o.s) : (o.t != 's' && v == o.v); }
};
constexpr int MAXA = 64;
struct Msg {
    int sel = 0;             // S::bang, S::intS, S::floatS, S::list, S::symbol, or any word ("set", "clear" ...)
    int n = 0;
    Atom a[MAXA];
    static Msg bang() { Msg m; m.sel = S::bang; return m; }
    static Msg i(double x) { Msg m; m.sel = S::intS; m.n = 1; m.a[0] = Atom::I(x); return m; }
    static Msg f(double x) { Msg m; m.sel = S::floatS; m.n = 1; m.a[0] = Atom::F(x); return m; }
    static Msg num(const Atom& x) { Msg m; if (x.t == 's') { m.sel = S::symbol; m.n = 1; m.a[0] = x; } else { m.sel = x.t == 'i' ? S::intS : S::floatS; m.n = 1; m.a[0] = x; } return m; }
    static Msg symbolM(int s) { Msg m; m.sel = S::symbol; m.n = 1; m.a[0] = Atom::Sy(s); return m; }
    static Msg word(int s) { Msg m; m.sel = s; return m; }
    // a list of atoms as Max would send it: one number = int / float, numbers first = list, a word first = that word
    static Msg fromAtoms(const Atom* at, int n);
    bool isBang() const { return sel == S::bang; }
    bool isNum() const { return (sel == S::intS || sel == S::floatS) && n >= 1; }
    bool isList() const { return sel == S::list; }
    double f(int k = 0) const { return k < n ? a[k].d() : 0.0; }
    // the message as a flat list of atoms (a word message: the word first)
    int atoms(Atom* out, int cap = MAXA) const;
    void push(const Atom& x) { if (n < MAXA) a[n++] = x; }
    std::string text() const;
};
// "1 2 foo" -> atoms (numbers with a dot are floats)
std::vector<Atom> parseAtoms(const std::string& text);
std::vector<std::string> tokenize(const std::string& text);   // spaces, "quoted words", commas and semicolons apart

// ------------------------------------------------------------------ buffers (buffer~): named sound in memory
struct Buf {
    int name = 0;
    std::vector<std::vector<float>> ch;     // channels
    double sr = 48000.0;
    long frames() const { return ch.empty() ? 0 : (long) ch[0].size(); }
    int chans() const { return (int) ch.size(); }
    float get(int c, long i) const { if (ch.empty()) return 0; c = juce::jlimit(0, chans() - 1, c); return i >= 0 && i < frames() ? ch[(size_t) c][(size_t) i] : 0.0f; }
    float interp(int c, double pos) const { long i = (long) std::floor(pos); float fr = (float) (pos - i); return get(c, i) * (1 - fr) + get(c, i + 1) * fr; }
    void resize(long n, int c) { c = juce::jlimit(1, 8, c); n = juce::jlimit(1L, (long) (48000 * 600), n); ch.resize((size_t) c); for (auto& x : ch) x.resize((size_t) n, 0.0f); }
};
using BufPtr = std::shared_ptr<Buf>;

struct Ctx;

// ------------------------------------------------------------------ an object
struct Obj {
    virtual ~Obj() = default;
    int nIn = 1, nOut = 1;
    unsigned long long sigIn = 0, sigOut = 0;          // which inlets / outlets carry signals
    std::vector<float> sv;                             // a signal inlet with nothing plugged in: this value
    juce::String err;                                  // a problem to show on the box (set at creation)
    int slot[2] = { -1, -1 };                          // its node in the program (two programs live at once: by parity)
    int boxId = 0;
    bool isSigIn(int k) const { return k < 64 && (sigIn >> k) & 1ULL; }
    bool isSigOut(int k) const { return k < 64 && (sigOut >> k) & 1ULL; }
    void setIO(int ins, int outs) { nIn = ins; nOut = outs; sv.assign((size_t) juce::jmax(1, nIn), 0.0f); }
    void sigInlet(int k, float v = 0) { sigIn |= 1ULL << k; if ((int) sv.size() <= k) sv.resize((size_t) k + 1); sv[(size_t) k] = v; }
    void sigOutlet(int k) { sigOut |= 1ULL << k; }
    bool hasSignal() const { return sigIn != 0 || sigOut != 0; }
    // a message into inlet k (the default: a number into a signal inlet sets its value)
    virtual void msg(Ctx& c, int in, const Msg& m);
    // the signal world: n samples (in[k] for every inlet, signal or not - a non-signal inlet gets its value)
    virtual void dsp(Ctx&, const float* const* /*in*/, float* const* /*out*/, int /*n*/) {}
    virtual void prepare(double /*sr*/) {}
    virtual void tick(Ctx&, int /*tag*/) {}            // a clock it set went off
    virtual void loadbang(Ctx&) {}
    virtual void dspStarted(Ctx&) {}                   // (after a compile: things that look at signal connections)
    bool wantsMidi = false, wantsBeat = false;
    virtual void beatPoll(Ctx&) {}                     // [beat]: looks at the app's clock every block                            // notein, ctlin, midiin ...: every MIDI message comes here
    virtual void midi(Ctx&, const unsigned char* /*b*/, int /*n*/) {}
    virtual int recvName() const { return 0; }         // [receive name]: what it listens to now
    virtual int sigSendName() const { return 0; }      // [send~ name] (runs before its receive~)
    virtual int sigRecvName() const { return 0; }
    virtual juce::var keep() const { return {}; }      // what it keeps with the patch (a slider's value, a table)
    virtual void restore(const juce::var&) {}
    // what the screen shows (written on the audio thread, read on the message thread)
    std::atomic<double> uiValue { 0 };
    std::atomic<int> uiFlash { 0 };
    std::atomic<int> uiGen { 0 };                      // something changed (arrays, text): redraw
    virtual int uiArray(float* /*out*/, int /*cap*/) const { return 0; }   // multislider, scope~, meter~, function, waveform
    virtual juce::String uiText() const { return {}; }
    std::atomic<bool> connectedIn[16] {};              // (for display)
};
using ObjPtr = std::shared_ptr<Obj>;

// ------------------------------------------------------------------ the catalogue of objects
struct ObjDef {
    std::string name;            // as typed
    std::string cat;             // the shelf
    std::string desc;            // what it does, in plain words
    std::string ins, outs;       // "what|what" per inlet / outlet
    std::string example;         // a line of text you could type ("metro 500")
    std::string words;           // words people search with ("echo repeat delay")
    std::function<ObjPtr(const std::vector<Atom>& args)> make;
    int uiW = 0, uiH = 0;        // an object you play with (a toggle, a slider): its size; 0 = a text box
    std::string aliasOf;         // a short name for another object
};
std::vector<ObjDef>& catalogue();
const ObjDef* findDef(const std::string& name);
std::vector<std::string> categories();
// the closest names to a word typed wrong ("cylce~" -> "cycle~")
std::vector<std::string> suggest(const std::string& word, int maxN = 3);
// a search by name and by what you want to do ("echo", "random", "make a beat")
std::vector<const ObjDef*> searchDefs(const std::string& query, int maxN = 40);
void registerAll();          // (once)

// ------------------------------------------------------------------ the patch as you see it
struct PBox {
    int id = 0;
    juce::String kind = "obj";        // "obj", "msg", "comment", "sub" (a subpatch: "p name")
    juce::String text;                // "cycle~ 440", the message's text, the comment
    float x = 0, y = 0, w = 0, h = 0;  // w = 0: the box's own width
    juce::var state;                  // what an object keeps with the patch (a slider's value, a table's numbers)
    std::shared_ptr<struct PatchDoc> sub;   // "p name": the patch inside
    juce::String colour;              // (comments, panels)
};
struct PLine { int from = 0, outlet = 0, to = 0, inlet = 0; };
struct PatchDoc {
    juce::String name = "Untitled";
    std::vector<PBox> boxes;
    std::vector<PLine> lines;
    int nextId = 1;
    juce::String notes;               // what the patch is, how to use it
    PBox* box(int id) { for (auto& b : boxes) if (b.id == id) return &b; return nullptr; }
    const PBox* box(int id) const { for (auto& b : boxes) if (b.id == id) return &b; return nullptr; }
    int add(const juce::String& kind, const juce::String& text, float x, float y) { PBox b; b.id = nextId++; b.kind = kind; b.text = text; b.x = x; b.y = y;
        if (kind == "sub") b.sub = std::make_shared<PatchDoc>(); boxes.push_back(b); return b.id; }
    void connect(int a, int o, int b, int i) { for (auto& l : lines) if (l.from == a && l.outlet == o && l.to == b && l.inlet == i) return; lines.push_back({ a, o, b, i }); }
    void remove(int id) { boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [id](const PBox& b) { return b.id == id; }), boxes.end());
        lines.erase(std::remove_if(lines.begin(), lines.end(), [id](const PLine& l) { return l.from == id || l.to == id; }), lines.end()); }
    juce::var toVar() const;
    static PatchDoc fromVar(const juce::var& v);
    static PatchDoc fromMaxpat(const juce::var& v);   // a Max patch (.maxpat): its boxes and cords
    juce::var toMaxpat() const;
};
// how many inlets / outlets a box has, and which carry signals (asks the catalogue; a message box: 1 / 1)
struct BoxIO { int ins = 0, outs = 0; unsigned long long sigIn = 0, sigOut = 0; bool ok = true; juce::String err; bool ui = false; int uiW = 0, uiH = 0; std::string cls; };
BoxIO boxIO(const PBox& b);

// ------------------------------------------------------------------ lock-free hand-overs (one writer, one reader)
template <typename T, int N> struct Fifo {
    T q[N]; std::atomic<int> w { 0 }, r { 0 };
    bool push(const T& x) { int a = w.load(std::memory_order_relaxed), nx = (a + 1) % N; if (nx == r.load(std::memory_order_acquire)) return false; q[a] = x; w.store(nx, std::memory_order_release); return true; }
    bool pop(T& x) { int a = r.load(std::memory_order_relaxed); if (a == w.load(std::memory_order_acquire)) return false; x = q[a]; r.store((a + 1) % N, std::memory_order_release); return true; }
};
struct UiIn { int box = 0, inlet = 0; Msg m; };
struct Line { char text[240]; char kind; int box; };       // a line for the console: 'p' print, 'e' error, 'i' info
struct Action { char what[16]; double a = 0, b = 0, c = 0; int box = 0; char text[200]; };   // asks the app (tempo, play, a note out ...)

// ------------------------------------------------------------------ a compiled patch
struct Node {
    ObjPtr obj;
    std::vector<std::vector<std::pair<int, int>>> outs;   // per outlet: (node, inlet) in the order they fire (right to left)
    std::string key;                                      // its place in the patch ("12" or "4/7" inside a subpatch)
    int box = 0;                                          // the top-level box it belongs to (for errors and the screen)
    float x = 0;
};
struct SigIn { const float* direct = nullptr; std::vector<std::pair<int, int>> sum; };   // (node, outlet)
struct DspStep { int node; std::vector<SigIn> ins; };

struct Runtime;
struct Ctx {
    Runtime* rt = nullptr;
    struct Program* prog = nullptr;
    double sr = 48000.0;
    double nowMs = 0;              // the patch's clock
    double bpm = 120, beat = 0;    // the app's tempo and beat (beats since its start)
    bool running = false;          // the app's transport
    int beatsPerBar = 4, beatUnit = 4;
    int depth = 0;
    // messages
    void out(Obj* o, int outlet, const Msg& m);
    void bang(Obj* o, int outlet) { out(o, outlet, Msg::bang()); }
    void outI(Obj* o, int outlet, double v) { out(o, outlet, Msg::i(v)); }
    void outF(Obj* o, int outlet, double v) { out(o, outlet, Msg::f(v)); }
    void send(int name, const Msg& m);                    // to every [receive name]
    // clocks
    void delay(Obj* o, double ms, int tag = 0);
    void cancel(Obj* o, int tag = 0);
    double noteMs(const Atom& a, double dflt) const;      // 500, "4n", "8nt", "16n", "1n" ... in ms at the tempo
    // things kept by the whole patcher
    BufPtr buffer(int name, bool make = false);
    Msg* value(int name);                                 // [value name]
    std::vector<float>* table(int name, bool make = false);
    // speaking to the screen and the app
    void print(int box, const std::string& s, char kind = 'p');
    void error(Obj* o, const std::string& s) { print(o ? o->boxId : 0, s, 'e'); }
    void action(const char* what, double a = 0, double b = 0, double c = 0, const std::string& text = {}, int box = 0);
    // the signal world's sends (send~ / receive~)
    float* sigSend(int name);
    unsigned rand();
    unsigned seed = 0x12345u;
    int nIn = 0, nOut = 0;                 // the patcher's own sound in / out (adc~ / dac~)
    const float* const* adcIn = nullptr;
    float* const* dacOut = nullptr;
};

struct Program {
    std::vector<Node> nodes;
    std::vector<DspStep> dsp;
    std::vector<std::vector<float>> sigBuf;                // per (node, signal outlet): see sigIndex
    std::vector<std::vector<int>> sigIndex;               // node -> outlet -> buffer
    std::vector<std::vector<float>> constBuf;             // non-signal inlets: their value as a signal
    std::vector<std::vector<float>> sumBuf;
    std::unordered_map<int, std::vector<int>> receivers;   // name -> nodes of [receive name]
    std::unordered_map<int, std::vector<int>> sendsSig;    // (send~ name -> node)
    std::vector<int> loadbangs;
    int gen = 0;
    std::map<int, std::vector<int>> boxNodes;              // top-level box -> its nodes
    std::vector<int> recvNodes, midiNodes, beatNodes;
    std::vector<Obj*> objSet;                              // (sorted: what lives in this program)
    bool fresh = false;
};

// ------------------------------------------------------------------ one running patcher
struct Runtime {
    Runtime(); ~Runtime();
    // message thread
    void setDoc(const PatchDoc& doc, bool fresh);          // compile and swap in (fresh: a patch opened - loadbangs)
    void prepare(double sampleRate);
    void post(int box, int inlet, const Msg& m) { UiIn u; u.box = box; u.inlet = inlet; u.m = m; uiIn.push(u); }
    void midiIn(const juce::MidiMessage& m);
    ObjPtr objOf(int box) const { auto it = objs.find(box); return it == objs.end() ? nullptr : it->second; }
    juce::String boxError(int box) const { auto it = errs.find(box); return it == errs.end() ? juce::String() : it->second; }
    std::function<void(const Line&)> onLine;               // the console (message thread, from poll())
    std::function<void(const Action&)> onAction;           // the app: tempo, play / stop, notes out
    void poll();                                           // call often on the message thread
    // audio thread: n <= 64 per call is fine, any n works
    void process(const float* const* in, int nIn, float* const* out, int nOut, int n);
    double bpm = 120, beat = 0; bool running = false; int beatsPerBar = 4, beatUnit = 4;   // set before process
    std::atomic<float> cpu { 0 };
    std::atomic<float> outPeak { 0 };
    bool hasDac = false, hasAdc = false;                   // (message thread, after a compile)
    // kept across compiles
    std::unordered_map<int, BufPtr> bufs;
    std::unordered_map<int, Msg> values;
    std::unordered_map<int, std::vector<float>> tables;
    std::unordered_map<int, std::vector<float>> sigSends;
    juce::SpinLock bufLock;
    // (internal)
    std::atomic<Program*> live { nullptr };
    std::atomic<bool> busy { false };
    std::atomic<int> ackGen { 0 };
    int lastGen = -1;
    struct Limbo { int gen; std::unique_ptr<Program> prog; std::vector<ObjPtr> dropped; double since; };
    std::vector<Limbo> limbo;
    std::unique_ptr<Program> current;
    std::map<std::string, std::pair<std::string, ObjPtr>> pool;   // key -> (text, object): kept while a patch is edited
    std::map<int, ObjPtr> objs; std::map<int, juce::String> errs;
    Fifo<UiIn, 128> uiIn;
    Fifo<std::array<unsigned char, 3>, 512> midiQ;
    Fifo<Line, 512> lines;
    Fifo<Action, 256> actions;
    struct Ev { double t; long long seq; Obj* o; int tag; };
    std::vector<Ev> evs; long long seq = 0;
    Ctx ctx;
    double sr = 48000.0;
    int gen = 0;
    std::vector<std::vector<float>> inScratch, outScratch;
    void runBlock(Program& p, int n);
    void deliver(Program& p, int node, int inlet, const Msg& m);
};

// expressions ([expr], [if], [vexpr]): $i1 $f1 $s1, $v1 (signals in expr~), functions, operators, if(c, a, b)
struct Expr {
    struct N; std::shared_ptr<N> root; juce::String err; int maxIn = 1; bool usesSig = false; bool intMode = false;
    bool parse(const std::string& text);
    double eval(const double* in, int nIn) const;
};

// the first word of a box: what it is ("cycle~"), and its arguments
std::string clsOf(const juce::String& text);

}} // namespace arco::px
