#pragma once
// ARCO 2.4: PATCHER - what the objects share: how they are put in the catalogue, their arguments.
#include "Patcher.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace arco { namespace px {

using Args = std::vector<Atom>;
using Make = std::function<ObjPtr(const Args&)>;

inline ObjDef& def(const char* name, const char* cat, const char* desc, const char* ins, const char* outs, const char* example, const char* words, Make make, int uiW = 0, int uiH = 0) {
    ObjDef d; d.name = name; d.cat = cat; d.desc = desc; d.ins = ins; d.outs = outs; d.example = example; d.words = words; d.make = std::move(make); d.uiW = uiW; d.uiH = uiH;
    catalogue().push_back(d); return catalogue().back();
}
inline void alias(const char* name, const char* of) {
    for (auto& d : catalogue()) if (d.name == of) { ObjDef a = d; a.name = name; a.aliasOf = of; a.desc = "(short for " + std::string(of) + ") " + d.desc; catalogue().push_back(a); return; }
}
template <typename T> Make mk() { return [](const Args& a) -> ObjPtr { return std::make_shared<T>(a); }; }

inline double argF(const Args& a, size_t k, double d = 0) { return k < a.size() && a[k].num() ? a[k].d() : d; }
inline long long argI(const Args& a, size_t k, long long d = 0) { return k < a.size() && a[k].num() ? a[k].i() : d; }
inline int argS(const Args& a, size_t k, int d = 0) { return k < a.size() && a[k].t == 's' ? a[k].s : d; }
inline bool argIsFloat(const Args& a, size_t k) { return k < a.size() && a[k].t == 'f'; }
inline std::string argStr(const Args& a, size_t k) { return k < a.size() ? a[k].str() : std::string(); }
inline int clampN(long long n, int lo, int hi) { return (int) juce::jlimit<long long>(lo, hi, n); }
// the words of a message after its selector, as atoms (a number message: the number)
inline int listOf(const Msg& m, Atom* out, int cap = MAXA) { return m.atoms(out, cap); }
inline Msg listMsg(const Atom* a, int n) { return Msg::fromAtoms(a, n); }
inline double mtof(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
inline double ftom(double f) { return f > 0 ? 69.0 + 12.0 * std::log2(f / 440.0) : -1500.0; }
inline float flush(float v) { return std::abs(v) < 1e-20f ? 0.0f : v; }
constexpr double TWO_PI = 6.283185307179586;

}} // namespace arco::px
