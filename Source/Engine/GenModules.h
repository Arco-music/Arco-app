#pragma once
// ARCO - generated from the gen~ modules (do not edit by hand: run gencpp.py)
#include <cmath>
#include <cstdlib>
#include <cstring>
#if ! defined (_WIN32)
 #include <sys/mman.h>
#endif
#include <vector>
#include <string>
namespace arco {
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
inline unsigned int& g_rs() { static unsigned int r = 12345u; return r; }
inline double g_noise() { unsigned int& rs = g_rs(); rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 8388608.0 - 1.0; }
inline double g_wrap(double x, double lo, double hi) { double r = hi - lo; if (r <= 0) return lo; double y = std::fmod(x - lo, r); if (y < 0) y += r; return y + lo; }
inline double g_clamp(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double g_mix(double a, double b, double t) { return a + (b - a) * t; }
inline double g_fract(double x) { return x - std::floor(x); }
inline double g_sign(double x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
template <typename T> inline double g_peek(T* d, long len, long ch, double i, double c) {
    long ii = (long) std::floor(i), cc = (long) std::floor(c);
    if (d == nullptr || ii < 0 || ii >= len || cc < 0 || cc >= ch) return 0.0;
    return (double) d[cc * len + ii]; }
template <typename T> inline double g_poke(T* d, long len, long ch, double v, double i, double c) {
    long ii = (long) std::floor(i), cc = (long) std::floor(c);
    if (d == nullptr || ii < 0 || ii >= len || cc < 0 || cc >= ch) return 0.0;
    d[cc * len + ii] = (T) v; return 0.0; }
struct DL { double* b = nullptr; long n = 0; long w = 0; };
inline double dl_write(DL* d, double x) { d->b[d->w] = x; return 0.0; }
inline double dl_read(DL* d, double t) { if (t < 0) t = 0; if (t > d->n - 2) t = d->n - 2; double p = d->w - t; while (p < 0) p += d->n;
    long i = (long) std::floor(p); double f = p - i; return d->b[i] * (1 - f) + d->b[(i + 1) % d->n] * f; }
// a shared audio buffer (like Max's buffer~): float samples, channel-major
// 2.2: big buffers take memory only where sound is written (the Mac's tracks hold 6 minutes; an empty track costs nothing)
struct LazyFloats {
    float* p = nullptr; size_t n = 0; bool mapped = false;
    LazyFloats() = default;
    LazyFloats(const LazyFloats&) = delete; LazyFloats& operator=(const LazyFloats&) = delete;
    LazyFloats(LazyFloats&& o) noexcept { *this = std::move(o); }
    LazyFloats& operator=(LazyFloats&& o) noexcept { if (this != &o) { release(); p = o.p; n = o.n; mapped = o.mapped; o.p = nullptr; o.n = 0; o.mapped = false; } return *this; }
    ~LazyFloats() { release(); }
    void release() {
        if (p == nullptr) return;
       #if ! defined (_WIN32)
        if (mapped) munmap(p, n * sizeof(float)); else
       #endif
        std::free(p);
        p = nullptr; n = 0; mapped = false; }
    void assign(size_t count, float) {
        release(); n = count; if (n == 0) return;
       #if ! defined (_WIN32)
        if (n * sizeof(float) >= (1u << 20)) {     // fresh zero pages from the system: used only when written
            void* m = mmap(nullptr, n * sizeof(float), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            if (m != MAP_FAILED) { p = (float*) m; mapped = true; return; } }
       #endif
        p = (float*) std::calloc(n, sizeof(float)); if (p == nullptr) n = 0; }
    // all zero again, and the memory given back (the same address: the engine keeps its pointer)
    void zero() {
        if (p == nullptr) return;
       #if ! defined (_WIN32)
        if (mapped && mmap(p, n * sizeof(float), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) != MAP_FAILED) return;
       #endif
        std::memset(p, 0, n * sizeof(float)); }
    float* data() { return p; } const float* data() const { return p; }
    size_t size() const { return n; }
    float* begin() { return p; } float* end() { return p + n; }
    const float* begin() const { return p; } const float* end() const { return p + n; }
    float& operator[](size_t i) { return p[i]; } const float& operator[](size_t i) const { return p[i]; }
};
struct SharedBuffer { LazyFloats data; long len = 0; long ch = 1; std::string name;
    void resize(long frames, long chans) { len = frames; ch = chans; data.assign((size_t) frames * chans, 0.0f); }
    float* chan(long c) { return data.data() + c * len; } };
// ---------------------------------------------------------------- Listen
struct Listen {
  double SR = 44100.0;
  double P_sense = 60.0;
  std::vector<double> V_dsr; double* D_dsr = nullptr; long LEN_dsr = 1024; long CH_dsr = 1;
  std::vector<double> V_frm; double* D_frm = nullptr; long LEN_frm = 640; long CH_frm = 1;
  std::vector<double> V_dfn; double* D_dfn = nullptr; long LEN_dfn = 320; long CH_dfn = 1;
  double H_l_ph = 0.0;
  double H_l_lp1 = 0.0;
  double H_l_lp2 = 0.0;
  double H_l_w = 0.0;
  double H_l_hop = 0.0;
  double H_l_lag = 400.0;
  double H_l_pitch = -1.0;
  double H_l_conf = 0.0;
  double H_l_lev = 0.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 3;
  void init(double sr) { SR = sr;
    V_dsr.assign((size_t) 1024 * 1, 0.0); D_dsr = V_dsr.data();
    V_frm.assign((size_t) 640 * 1, 0.0); D_frm = V_frm.data();
    V_dfn.assign((size_t) 320 * 1, 0.0); D_dfn = V_dfn.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "sense") return 0;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_sense = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_sense;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 1;
  double* data(const std::string& n) {
    if (n == "dsr") return D_dsr;
    if (n == "frm") return D_frm;
    if (n == "dfn") return D_dfn;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double v_fac = 0.0;
      double v_fsd = 0.0;
      double v_winn = 0.0;
      double v_lagmax = 0.0;
      double v_lagmin = 0.0;
      double v_frn = 0.0;
      double v_x = 0.0;
      double v_lev = 0.0;
      double v_aa = 0.0;
      double v_f1 = 0.0;
      double v_f2 = 0.0;
      double v_ph = 0.0;
      double v_wpos = 0.0;
      double v_hp = 0.0;
      double v_j = 0.0;
      double v_lg = 0.0;
      double v_acc = 0.0;
      double v_dd = 0.0;
      double v_run = 0.0;
      double v_best = 0.0;
      double v_bestv = 0.0;
      double v_found = 0.0;
      double v_nv = 0.0;
      double v_k = 0.0;
      double v_q = 0.0;
      double v_pit = 0.0;
      double v_a0 = 0.0;
      double v_a1 = 0.0;
      double v_a2 = 0.0;
      double v_den = 0.0;
      double v_sh = 0.0;
      double v_hz = 0.0;
      double v_thr = 0.0;
      v_fac = (4.0);
      if (((double)((SR) > ((50000.0)))) != 0.0) {
        v_fac = (8.0);
      }
      v_fsd = (SR / v_fac);
      v_winn = (320.0);
      v_lagmax = (300.0);
      v_lagmin = (8.0);
      v_frn = (620.0);
      v_x = IN1;
      v_lev = fmax(fabs(v_x),(H_l_lev * (0.9995)));
      H_l_lev = v_lev;
      v_aa = exp((((-(2.0*M_PI)) * (1800.0)) / SR));
      v_f1 = (v_x + ((H_l_lp1 - v_x) * v_aa));
      v_f2 = (v_f1 + ((H_l_lp2 - v_f1) * v_aa));
      H_l_lp1 = v_f1;
      H_l_lp2 = v_f2;
      v_ph = (H_l_ph + (1.0));
      if (((double)((v_ph) >= (v_fac))) != 0.0) {
        v_ph = (0.0);
        v_wpos = H_l_w;
        g_poke(D_dsr,LEN_dsr,CH_dsr,v_f2,v_wpos,0);
        v_wpos = fmod((v_wpos + (1.0)),(1024.0));
        H_l_w = v_wpos;
        v_hp = (H_l_hop + (1.0));
        if (((double)((((double)((v_hp) >= ((96.0))))) && (((double)((H_l_lag) >= ((v_lagmax + (1.0)))))))) != 0.0) {
          for (v_j = (0.0); ((double)((v_j) < (v_frn))) != 0.0; v_j = v_j + ((1.0))) {
            g_poke(D_frm,LEN_frm,CH_frm,g_peek(D_dsr,LEN_dsr,CH_dsr,fmod((((v_wpos - v_frn) + v_j) + (2048.0)),(1024.0)),0),v_j,0);
          }
          H_l_lag = (1.0);
          v_hp = (0.0);
        }
        H_l_hop = v_hp;
      }
      H_l_ph = v_ph;
      v_lg = H_l_lag;
      if (((double)((v_lg) < (v_lagmax))) != 0.0) {
        v_acc = (0.0);
        for (v_j = (0.0); ((double)((v_j) < (v_winn))) != 0.0; v_j = v_j + ((1.0))) {
          v_dd = (g_peek(D_frm,LEN_frm,CH_frm,v_j,0) - g_peek(D_frm,LEN_frm,CH_frm,(v_j + v_lg),0));
          v_acc = (v_acc + (v_dd * v_dd));
        }
        g_poke(D_dfn,LEN_dfn,CH_dfn,v_acc,v_lg,0);
        v_lg = (v_lg + (1.0));
        if (((double)((v_lg) >= (v_lagmax))) != 0.0) {
          v_run = (0.0);
          v_best = (-(1.0));
          v_bestv = (2.0);
          v_found = (0.0);
          for (v_j = (1.0); ((double)((v_j) < (v_lagmax))) != 0.0; v_j = v_j + ((1.0))) {
            v_run = (v_run + g_peek(D_dfn,LEN_dfn,CH_dfn,v_j,0));
            v_nv = (1.0);
            if (((double)((v_run) > ((0.0)))) != 0.0) {
              v_nv = ((g_peek(D_dfn,LEN_dfn,CH_dfn,v_j,0) * v_j) / v_run);
            }
            g_poke(D_dfn,LEN_dfn,CH_dfn,v_nv,v_j,0);
          }
          for (v_j = v_lagmin; ((double)((v_j) < ((v_lagmax - (1.0))))) != 0.0; v_j = v_j + ((1.0))) {
            v_nv = g_peek(D_dfn,LEN_dfn,CH_dfn,v_j,0);
            if (((double)((((double)((v_found) == ((0.0))))) && (((double)((v_nv) < ((0.15))))))) != 0.0) {
              v_k = v_j;
              for (v_q = (0.0); ((double)((v_q) < ((40.0)))) != 0.0; v_q = v_q + ((1.0))) {
                if (((double)((((double)(((v_k + (1.0))) < ((v_lagmax - (1.0)))))) && (((double)((g_peek(D_dfn,LEN_dfn,CH_dfn,(v_k + (1.0)),0)) < (g_peek(D_dfn,LEN_dfn,CH_dfn,v_k,0))))))) != 0.0) {
                  v_k = (v_k + (1.0));
                }
              }
              v_best = v_k;
              v_bestv = g_peek(D_dfn,LEN_dfn,CH_dfn,v_k,0);
              v_found = (1.0);
            }
            if (((double)((((double)((v_found) == ((0.0))))) && (((double)((v_nv) < (v_bestv)))))) != 0.0) {
              v_bestv = v_nv;
              v_best = v_j;
            }
          }
          v_pit = (-(1.0));
          if (((double)((((double)((v_best) > ((0.0))))) && (((double)((v_bestv) < ((0.35))))))) != 0.0) {
            v_a0 = g_peek(D_dfn,LEN_dfn,CH_dfn,(v_best - (1.0)),0);
            v_a1 = g_peek(D_dfn,LEN_dfn,CH_dfn,v_best,0);
            v_a2 = g_peek(D_dfn,LEN_dfn,CH_dfn,(v_best + (1.0)),0);
            v_den = ((v_a0 - ((2.0) * v_a1)) + v_a2);
            v_sh = (0.0);
            if (((double)((fabs(v_den)) > ((1e-06)))) != 0.0) {
              v_sh = g_clamp((((0.5) * (v_a0 - v_a2)) / v_den),(-(0.5)),(0.5));
            }
            v_hz = (v_fsd / (v_best + v_sh));
            v_pit = ((69.0) + (((12.0) * log((v_hz / (440.0)))) / log((2.0))));
          }
          v_thr = pow(10.0,(((-(30.0)) - (P_sense * (0.4))))/20.0);
          if (((double)((v_lev) < (v_thr))) != 0.0) {
            v_pit = (-(1.0));
          }
          H_l_pitch = v_pit;
          H_l_conf = g_clamp(((1.0) - v_bestv),(0.0),(1.0));
          v_lg = (v_lagmax + (1.0));
        }
        H_l_lag = v_lg;
      }
      OUT1 = H_l_pitch;
      OUT2 = H_l_conf;
      OUT3 = v_lev;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
    }
  }
};
// ---------------------------------------------------------------- Tune
struct Tune {
  double SR = 44100.0;
  double P_tn_amt = 0.0;
  double P_tn_nat = 70.0;
  double P_tn_scale = 0.0;
  double P_a4 = 440.0;
  double P_tkey = -1.0;
  std::vector<double> M_dl; DL DL_dl;
  std::vector<double> V_rb; double* D_rb = nullptr; long LEN_rb = 8192; long CH_rb = 1;
  std::vector<double> V_dd; double* D_dd = nullptr; long LEN_dd = 160; long CH_dd = 1;
  std::vector<double> V_sc; double* D_sc = nullptr; long LEN_sc = 7; long CH_sc = 1;
  double H_t_w = 0.0;
  double H_t_hop = 0.0;
  double H_t_pp = -1.0;
  double H_t_per = 200.0;
  double H_t_T = -100.0;
  double H_t_lp = -1.0;
  double H_t_corr = 0.0;
  double H_t_ph = 0.0;
  double H_t_W = 400.0;
  double H_t_mix = 0.0;
  double H_t_init = 0.0;
  double H_t_inv = 0.0;
  static constexpr int numInputs = 2;
  static constexpr int numOutputs = 4;
  void init(double sr) { SR = sr;
    M_dl.assign(24000, 0.0); DL_dl.b = M_dl.data(); DL_dl.n = 24000; DL_dl.w = 0;
    V_rb.assign((size_t) 8192 * 1, 0.0); D_rb = V_rb.data();
    V_dd.assign((size_t) 160 * 1, 0.0); D_dd = V_dd.data();
    V_sc.assign((size_t) 7 * 1, 0.0); D_sc = V_sc.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "tn_amt") return 0;
    if (n == "tn_nat") return 1;
    if (n == "tn_scale") return 2;
    if (n == "a4") return 3;
    if (n == "tkey") return 4;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_tn_amt = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_tn_nat = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_tn_scale = v; break;
      case 3: if (v < 415.0) v = 415.0; if (v > 466.0) v = 466.0; P_a4 = v; break;
      case 4: P_tkey = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_tn_amt;
      case 1: return P_tn_nat;
      case 2: return P_tn_scale;
      case 3: return P_a4;
      case 4: return P_tkey;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 5;
  double* data(const std::string& n) {
    if (n == "rb") return D_rb;
    if (n == "dd") return D_dd;
    if (n == "sc") return D_sc;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double v_x = 0.0;
      double v_cp = 0.0;
      double v_nrb = 0.0;
      double v_w = 0.0;
      double v_hop = 0.0;
      double v_L0 = 0.0;
      double v_stp = 0.0;
      double v_nn = 0.0;
      double v_lo = 0.0;
      double v_hi = 0.0;
      double v_best = 0.0;
      double v_bv = 0.0;
      double v_en = 0.0;
      double v_j = 0.0;
      double v_a = 0.0;
      double v_lg = 0.0;
      double v_acc = 0.0;
      double v_b = 0.0;
      double v_d0 = 0.0;
      double v_d1 = 0.0;
      double v_d2 = 0.0;
      double v_den = 0.0;
      double v_shf = 0.0;
      double v_per = 0.0;
      double v_pp = 0.0;
      double v_am = 0.0;
      double v_nat = 0.0;
      double v_T = 0.0;
      double v_tcorr = 0.0;
      double v_n0 = 0.0;
      double v_inkey = 0.0;
      double v_bestn = 0.0;
      double v_bd = 0.0;
      double v_c = 0.0;
      double v_ok = 0.0;
      double v_rel = 0.0;
      double v_lp = 0.0;
      double v_lim = 0.0;
      double v_tk = 0.0;
      double v_corr = 0.0;
      double v_ratio = 0.0;
      double v_mm = 0.0;
      double v_tw = 0.0;
      double v_W = 0.0;
      double v_ph = 0.0;
      double v_p2 = 0.0;
      double v_y = 0.0;
      double v_mx = 0.0;
      if (((double)((H_t_init) == ((0.0)))) != 0.0) {
        g_poke(D_sc,LEN_sc,CH_sc,(0.0),(0.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(2.0),(1.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(4.0),(2.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(5.0),(3.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(7.0),(4.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(9.0),(5.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(11.0),(6.0),0);
        H_t_init = (1.0);
      }
      v_x = IN1;
      v_cp = IN2;
      v_nrb = (8192.0);
      v_w = H_t_w;
      g_poke(D_rb,LEN_rb,CH_rb,v_x,v_w,0);
      v_w = fmod((v_w + (1.0)),v_nrb);
      H_t_w = v_w;
      v_hop = (H_t_hop + (1.0));
      if (((double)((v_hop) >= ((128.0)))) != 0.0) {
        v_hop = (0.0);
        if (((double)((v_cp) > ((0.0)))) != 0.0) {
          v_L0 = (SR / ((440.0) * pow((2.0),((v_cp - (69.0)) / (12.0)))));
          v_stp = (2.0);
          if (((double)((v_L0) > ((320.0)))) != 0.0) {
            v_stp = (4.0);
          }
          v_nn = g_clamp(floor(((1.5) * v_L0)),(256.0),(1536.0));
          v_lo = fmax((2.0),floor((v_L0 * (0.98))));
          v_hi = fmin((v_lo + (80.0)),(ceil((v_L0 * (1.02))) + (1.0)));
          v_hi = fmin(v_hi,((v_nrb - v_nn) - (4.0)));
          v_best = (-(1.0));
          v_bv = (1000000000.0);
          v_en = (1e-06);
          for (v_j = (0.0); ((double)((v_j) < (v_nn))) != 0.0; v_j = v_j + (v_stp)) {
            v_a = g_peek(D_rb,LEN_rb,CH_rb,fmod((((v_w - (1.0)) - v_j) + (v_nrb * (2.0))),v_nrb),0);
            v_en = (v_en + (v_a * v_a));
          }
          for (v_lg = v_lo; ((double)((v_lg) <= (v_hi))) != 0.0; v_lg = v_lg + ((1.0))) {
            v_acc = (0.0);
            for (v_j = (0.0); ((double)((v_j) < (v_nn))) != 0.0; v_j = v_j + (v_stp)) {
              v_a = g_peek(D_rb,LEN_rb,CH_rb,fmod((((v_w - (1.0)) - v_j) + (v_nrb * (2.0))),v_nrb),0);
              v_b = g_peek(D_rb,LEN_rb,CH_rb,fmod(((((v_w - (1.0)) - v_j) - v_lg) + (v_nrb * (2.0))),v_nrb),0);
              v_acc = (v_acc + ((v_a - v_b) * (v_a - v_b)));
            }
            g_poke(D_dd,LEN_dd,CH_dd,v_acc,(v_lg - v_lo),0);
            if (((double)((v_acc) < (v_bv))) != 0.0) {
              v_bv = v_acc;
              v_best = v_lg;
            }
          }
          if (((double)((((double)((((double)((v_best) > (v_lo)))) && (((double)((v_best) < (v_hi))))))) && (((double)(((v_bv / v_en)) < ((0.4))))))) != 0.0) {
            v_d0 = g_peek(D_dd,LEN_dd,CH_dd,((v_best - (1.0)) - v_lo),0);
            v_d1 = g_peek(D_dd,LEN_dd,CH_dd,(v_best - v_lo),0);
            v_d2 = g_peek(D_dd,LEN_dd,CH_dd,((v_best + (1.0)) - v_lo),0);
            v_den = ((v_d0 - ((2.0) * v_d1)) + v_d2);
            v_shf = (0.0);
            if (((double)((fabs(v_den)) > ((1e-07)))) != 0.0) {
              v_shf = g_clamp((((0.5) * (v_d0 - v_d2)) / v_den),(-(0.5)),(0.5));
            }
            v_per = (v_best + v_shf);
            H_t_per = v_per;
            H_t_pp = ((69.0) + (((12.0) * log(((SR / v_per) / P_a4))) / log((2.0))));
            H_t_inv = (0.0);
          } else {
            H_t_pp = ((69.0) + (((12.0) * log((((440.0) * pow((2.0),((v_cp - (69.0)) / (12.0)))) / P_a4))) / log((2.0))));
            H_t_per = v_L0;
            H_t_inv = (0.0);
          }
        } else {
          H_t_inv = (H_t_inv + (1.0));
          if (((double)((H_t_inv) > ((6.0)))) != 0.0) {
            H_t_pp = (-(1.0));
          }
        }
      }
      H_t_hop = v_hop;
      v_pp = H_t_pp;
      v_am = (P_tn_amt / (100.0));
      v_nat = (P_tn_nat / (100.0));
      v_T = H_t_T;
      v_tcorr = (0.0);
      if (((double)((v_pp) > ((0.0)))) != 0.0) {
        v_n0 = floor((v_pp + (0.5)));
        v_inkey = ((double)((((double)((P_tn_scale) > ((50.0))))) && (((double)((P_tkey) >= ((0.0)))))));
        v_bestn = v_n0;
        v_bd = (100.0);
        for (v_c = (v_n0 - (2.0)); ((double)((v_c) <= ((v_n0 + (2.0))))) != 0.0; v_c = v_c + ((1.0))) {
          v_ok = (1.0);
          if (v_inkey != 0.0) {
            v_ok = (0.0);
            v_rel = fmod(((v_c - P_tkey) + (1200.0)),(12.0));
            for (v_j = (0.0); ((double)((v_j) < ((7.0)))) != 0.0; v_j = v_j + ((1.0))) {
              if (((double)((g_peek(D_sc,LEN_sc,CH_sc,v_j,0)) == (v_rel))) != 0.0) {
                v_ok = (1.0);
              }
            }
          }
          if (((double)((v_ok) && (((double)((fabs((v_pp - v_c))) < (v_bd)))))) != 0.0) {
            v_bd = fabs((v_pp - v_c));
            v_bestn = v_c;
          }
        }
        if (((double)((((double)((v_T) < ((0.0))))) || (((double)((fabs((v_pp - v_T))) > ((v_bd + (0.12)))))))) != 0.0) {
          v_T = v_bestn;
        }
        H_t_T = v_T;
        v_lp = H_t_lp;
        if (((double)((((double)((v_lp) < ((0.0))))) || (((double)((fabs((v_pp - v_lp))) > ((0.7))))))) != 0.0) {
          v_lp = v_pp;
        }
        v_lp = (v_lp + ((v_pp - v_lp) * ((1.0) - exp(((-(1.0)) / fmax((1.0),(((v_nat * (220.0)))*SR/1000.0)))))));
        H_t_lp = v_lp;
        v_lim = (0.7);
        if (v_inkey != 0.0) {
          v_lim = (1.1);
        }
        v_tcorr = (g_clamp((v_T - v_lp),(-v_lim),v_lim) * v_am);
      } else {
        H_t_lp = (-(1.0));
      }
      v_tk = ((((2.0) + (v_nat * (150.0))))*SR/1000.0);
      if (((double)((v_pp) <= ((0.0)))) != 0.0) {
        v_tk = (((150.0))*SR/1000.0);
      }
      v_corr = (H_t_corr + ((v_tcorr - H_t_corr) * ((1.0) - exp(((-(1.0)) / v_tk)))));
      H_t_corr = v_corr;
      v_ratio = pow((2.0),(v_corr / (12.0)));
      v_per = H_t_per;
      v_mm = fmax((1.0),ceil(((((2.5))*SR/1000.0) / v_per)));
      v_tw = g_clamp((((2.0) * v_per) * v_mm),(((4.0))*SR/1000.0),(((40.0))*SR/1000.0));
      v_W = (H_t_W + ((v_tw - H_t_W) * ((1.0) - exp(((-(1.0)) / (((4.0))*SR/1000.0))))));
      H_t_W = v_W;
      dl_write(&DL_dl,v_x);
      v_ph = g_wrap((H_t_ph + (((1.0) - v_ratio) / v_W)),(0.0),(1.0));
      H_t_ph = v_ph;
      v_p2 = g_wrap((v_ph + (0.5)),(0.0),(1.0));
      v_y = ((dl_read(&DL_dl,((1.0) + (v_ph * v_W))) * ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_ph))))) + (dl_read(&DL_dl,((1.0) + (v_p2 * v_W))) * ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_p2))))));
      v_mx = (H_t_mix + ((((double)((v_am) > ((0.001)))) - H_t_mix) * ((1.0) - exp(((-(1.0)) / (((20.0))*SR/1000.0))))));
      H_t_mix = v_mx;
      OUT1 = (v_x + ((v_y - v_x) * v_mx));
      OUT2 = v_pp;
      OUT3 = v_corr;
      OUT4 = v_T;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      DL_dl.w = (DL_dl.w + 1) % DL_dl.n;
    }
  }
};
// ---------------------------------------------------------------- Looper
struct Looper {
  double SR = 44100.0;
  double P_bpm = 90.0;
  double P_beats = 4.0;
  double P_bars = 0.0;
  double P_countin = 1.0;
  double P_clickall = 0.0;
  double P_clickvol = 60.0;
  double P_rstall = 0.0;
  double P_stopall = 0.0;
  double P_playall = 0.0;
  double P_scene = 0.0;
  double P_rs_go = 0.0;
  double P_rs_bl = 32000.0;
  double P_rs_bts = 4.0;
  double P_rs_len0 = 0.0;
  double P_rs_mask0 = 0.0;
  double P_rs_spd0 = 0.0;
  double P_rs_len1 = 0.0;
  double P_rs_mask1 = 0.0;
  double P_rs_spd1 = 0.0;
  double P_rs_len2 = 0.0;
  double P_rs_mask2 = 0.0;
  double P_rs_spd2 = 0.0;
  double P_rs_len3 = 0.0;
  double P_rs_mask3 = 0.0;
  double P_rs_spd3 = 0.0;
  double P_rs_ntr = 17476.0;
  double P_imp_go = 0.0;
  double P_imp_trk = 1.0;
  double P_imp_sr = 0.0;
  double P_prac = 100.0;
  double P_recnext = 0.0;
  double P_playstop = 0.0;
  double P_undo = 0.0;
  double P_scnext = 0.0;
  double P_ntr = 0.0;
  double P_pvw = 0.0;
  double P_lat = 0.0;
  double P_rec1 = 0.0;
  double P_clr1 = 0.0;
  double P_tog1 = 0.0;
  double P_spd1 = 0.0;
  double P_src1 = 1.0;
  double P_rec2 = 0.0;
  double P_clr2 = 0.0;
  double P_tog2 = 0.0;
  double P_spd2 = 0.0;
  double P_src2 = 1.0;
  double P_rec3 = 0.0;
  double P_clr3 = 0.0;
  double P_tog3 = 0.0;
  double P_spd3 = 0.0;
  double P_src3 = 1.0;
  double P_rec4 = 0.0;
  double P_clr4 = 0.0;
  double P_tog4 = 0.0;
  double P_spd4 = 0.0;
  double P_src4 = 1.0;
  double P_rec5 = 0.0;
  double P_clr5 = 0.0;
  double P_tog5 = 0.0;
  double P_spd5 = 0.0;
  double P_src5 = 1.0;
  double P_rec6 = 0.0;
  double P_clr6 = 0.0;
  double P_tog6 = 0.0;
  double P_spd6 = 0.0;
  double P_src6 = 1.0;
  double P_rec7 = 0.0;
  double P_clr7 = 0.0;
  double P_tog7 = 0.0;
  double P_spd7 = 0.0;
  double P_src7 = 1.0;
  double P_rec8 = 0.0;
  double P_clr8 = 0.0;
  double P_tog8 = 0.0;
  double P_spd8 = 0.0;
  double P_src8 = 1.0;
  float* D_lbA = nullptr; long LEN_lbA = 0; long CH_lbA = 1;   // buffer~ st_loopsA
  float* D_lbB = nullptr; long LEN_lbB = 0; long CH_lbB = 1;   // buffer~ st_loopsB
  float* D_lbC = nullptr; long LEN_lbC = 0; long CH_lbC = 1;   // buffer~ st_loopsC
  float* D_lbD = nullptr; long LEN_lbD = 0; long CH_lbD = 1;   // buffer~ st_loopsD
  float* D_pbA = nullptr; long LEN_pbA = 0; long CH_pbA = 1;   // buffer~ st_pitchA
  float* D_pbB = nullptr; long LEN_pbB = 0; long CH_pbB = 1;   // buffer~ st_pitchB
  float* D_pbC = nullptr; long LEN_pbC = 0; long CH_pbC = 1;   // buffer~ st_pitchC
  float* D_pbD = nullptr; long LEN_pbD = 0; long CH_pbD = 1;   // buffer~ st_pitchD
  float* D_imp = nullptr; long LEN_imp = 0; long CH_imp = 1;   // buffer~ st_import
  float* D_ub = nullptr; long LEN_ub = 0; long CH_ub = 1;   // buffer~ st_undo
  std::vector<double> V_st; double* D_st = nullptr; long LEN_st = 8; long CH_st = 4;
  std::vector<double> V_pg; double* D_pg = nullptr; long LEN_pg = 8; long CH_pg = 4;
  std::vector<double> V_we; double* D_we = nullptr; long LEN_we = 8; long CH_we = 1;
  std::vector<double> V_tl; double* D_tl = nullptr; long LEN_tl = 8; long CH_tl = 1;
  std::vector<double> V_slen; double* D_slen = nullptr; long LEN_slen = 4; long CH_slen = 1;
  std::vector<double> V_rmk; double* D_rmk = nullptr; long LEN_rmk = 4; long CH_rmk = 1;
  std::vector<double> V_lrec; double* D_lrec = nullptr; long LEN_lrec = 8; long CH_lrec = 1;
  std::vector<double> V_lclr; double* D_lclr = nullptr; long LEN_lclr = 8; long CH_lclr = 1;
  std::vector<double> V_ltog; double* D_ltog = nullptr; long LEN_ltog = 8; long CH_ltog = 1;
  std::vector<double> V_cmdr; double* D_cmdr = nullptr; long LEN_cmdr = 8; long CH_cmdr = 1;
  std::vector<double> V_cmdc; double* D_cmdc = nullptr; long LEN_cmdc = 8; long CH_cmdc = 1;
  std::vector<double> V_cmdt; double* D_cmdt = nullptr; long LEN_cmdt = 8; long CH_cmdt = 1;
  std::vector<double> V_ov; double* D_ov = nullptr; long LEN_ov = 8; long CH_ov = 1;
  std::vector<double> V_op; double* D_op = nullptr; long LEN_op = 8; long CH_op = 1;
  std::vector<double> V_spd; double* D_spd = nullptr; long LEN_spd = 8; long CH_spd = 4;
  std::vector<double> V_spa; double* D_spa = nullptr; long LEN_spa = 8; long CH_spa = 1;
  std::vector<double> V_sgn; double* D_sgn = nullptr; long LEN_sgn = 8; long CH_sgn = 1;
  std::vector<double> V_lspd; double* D_lspd = nullptr; long LEN_lspd = 8; long CH_lspd = 1;
  std::vector<double> V_cmds; double* D_cmds = nullptr; long LEN_cmds = 8; long CH_cmds = 1;
  std::vector<double> V_rsp; double* D_rsp = nullptr; long LEN_rsp = 4; long CH_rsp = 1;
  std::vector<double> V_gph; double* D_gph = nullptr; long LEN_gph = 8; long CH_gph = 1;
  std::vector<double> V_gaa; double* D_gaa = nullptr; long LEN_gaa = 8; long CH_gaa = 1;
  std::vector<double> V_gab; double* D_gab = nullptr; long LEN_gab = 8; long CH_gab = 1;
  std::vector<double> V_srcd; double* D_srcd = nullptr; long LEN_srcd = 8; long CH_srcd = 1;
  std::vector<double> V_xin; double* D_xin = nullptr; long LEN_xin = 4; long CH_xin = 1;
  std::vector<double> V_ntrs; double* D_ntrs = nullptr; long LEN_ntrs = 4; long CH_ntrs = 1;
  double H_g_init = 0.0;
  double H_g_cs = 0.0;
  double H_g_ps = -1.0;
  double H_g_os = -1.0;
  double H_g_hold = 0.0;
  double H_g_xf = 0.0;
  double H_g_opos = 0.0;
  double H_g_pos = 0.0;
  double H_g_run = 0.0;
  double H_g_bl = 32000.0;
  double H_g_bts = 4.0;
  double H_g_cin = 0.0;
  double H_g_cpos = 0.0;
  double H_g_ctrk = -1.0;
  double H_g_first = -1.0;
  double H_g_rp = 0.0;
  double H_g_stop = 0.0;
  double H_g_tgt = 0.0;
  double H_g_lres = 0.0;
  double H_g_lstop = 0.0;
  double H_g_lplay = 0.0;
  double H_g_lscn = 0.0;
  double H_g_lrs = 0.0;
  double H_g_limp = 0.0;
  double H_g_lbar = -1.0;
  double H_g_lbeat = -1.0;
  double H_g_cenv = 0.0;
  double H_g_cph = 0.0;
  double H_g_cfr = 1000.0;
  double H_g_free = 0.0;
  double H_g_auto = -1.0;
  double H_g_since = 999999.0;
  double H_g_istart = 0.0;
  double H_g_imp = 0.0;
  double H_g_ipos = 0.0;
  double H_g_itrk = 0.0;
  double H_g_iscn = 0.0;
  double H_g_ilen = 0.0;
  double H_g_irate = 1.0;
  double H_g_iflen = 0.0;
  double H_g_lrn = 0.0;
  double H_g_lps = 0.0;
  double H_g_lun = 0.0;
  double H_g_lsn = 0.0;
  double H_g_lc = 0.0;
  double H_g_olc = 0.0;
  double H_g_wrap = 0.0;
  double H_g_pm = 0.0;
  double H_g_ut = 0.0;
  double H_g_uk = 0.0;
  double H_g_us = 0.0;
  double H_g_ucnt = 0.0;
  double H_g_ur = 0.0;
  double H_g_upos = 0.0;
  double H_g_sinit = 0.0;
  double H_g_lnt = 0.0;
  double H_g_vpos = 0.0;
  double H_g_vg = 0.0;
  static constexpr int numInputs = 5;
  static constexpr int numOutputs = 49;
  void init(double sr) { SR = sr;
    V_st.assign((size_t) 8 * 4, 0.0); D_st = V_st.data();
    V_pg.assign((size_t) 8 * 4, 0.0); D_pg = V_pg.data();
    V_we.assign((size_t) 8 * 1, 0.0); D_we = V_we.data();
    V_tl.assign((size_t) 8 * 1, 0.0); D_tl = V_tl.data();
    V_slen.assign((size_t) 4 * 1, 0.0); D_slen = V_slen.data();
    V_rmk.assign((size_t) 4 * 1, 0.0); D_rmk = V_rmk.data();
    V_lrec.assign((size_t) 8 * 1, 0.0); D_lrec = V_lrec.data();
    V_lclr.assign((size_t) 8 * 1, 0.0); D_lclr = V_lclr.data();
    V_ltog.assign((size_t) 8 * 1, 0.0); D_ltog = V_ltog.data();
    V_cmdr.assign((size_t) 8 * 1, 0.0); D_cmdr = V_cmdr.data();
    V_cmdc.assign((size_t) 8 * 1, 0.0); D_cmdc = V_cmdc.data();
    V_cmdt.assign((size_t) 8 * 1, 0.0); D_cmdt = V_cmdt.data();
    V_ov.assign((size_t) 8 * 1, 0.0); D_ov = V_ov.data();
    V_op.assign((size_t) 8 * 1, 0.0); D_op = V_op.data();
    V_spd.assign((size_t) 8 * 4, 0.0); D_spd = V_spd.data();
    V_spa.assign((size_t) 8 * 1, 0.0); D_spa = V_spa.data();
    V_sgn.assign((size_t) 8 * 1, 0.0); D_sgn = V_sgn.data();
    V_lspd.assign((size_t) 8 * 1, 0.0); D_lspd = V_lspd.data();
    V_cmds.assign((size_t) 8 * 1, 0.0); D_cmds = V_cmds.data();
    V_rsp.assign((size_t) 4 * 1, 0.0); D_rsp = V_rsp.data();
    V_gph.assign((size_t) 8 * 1, 0.0); D_gph = V_gph.data();
    V_gaa.assign((size_t) 8 * 1, 0.0); D_gaa = V_gaa.data();
    V_gab.assign((size_t) 8 * 1, 0.0); D_gab = V_gab.data();
    V_srcd.assign((size_t) 8 * 1, 0.0); D_srcd = V_srcd.data();
    V_xin.assign((size_t) 4 * 1, 0.0); D_xin = V_xin.data();
    V_ntrs.assign((size_t) 4 * 1, 0.0); D_ntrs = V_ntrs.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
    if (name == "st_loopsA") { D_lbA = b.data.data(); LEN_lbA = b.len; CH_lbA = b.ch; }
    if (name == "st_loopsB") { D_lbB = b.data.data(); LEN_lbB = b.len; CH_lbB = b.ch; }
    if (name == "st_loopsC") { D_lbC = b.data.data(); LEN_lbC = b.len; CH_lbC = b.ch; }
    if (name == "st_loopsD") { D_lbD = b.data.data(); LEN_lbD = b.len; CH_lbD = b.ch; }
    if (name == "st_pitchA") { D_pbA = b.data.data(); LEN_pbA = b.len; CH_pbA = b.ch; }
    if (name == "st_pitchB") { D_pbB = b.data.data(); LEN_pbB = b.len; CH_pbB = b.ch; }
    if (name == "st_pitchC") { D_pbC = b.data.data(); LEN_pbC = b.len; CH_pbC = b.ch; }
    if (name == "st_pitchD") { D_pbD = b.data.data(); LEN_pbD = b.len; CH_pbD = b.ch; }
    if (name == "st_import") { D_imp = b.data.data(); LEN_imp = b.len; CH_imp = b.ch; }
    if (name == "st_undo") { D_ub = b.data.data(); LEN_ub = b.len; CH_ub = b.ch; }
  }
  static std::vector<std::string> bufferNames() { return {"st_loopsA", "st_loopsB", "st_loopsC", "st_loopsD", "st_pitchA", "st_pitchB", "st_pitchC", "st_pitchD", "st_import", "st_undo"}; }
  static int paramIndex(const std::string& n) {
    if (n == "bpm") return 0;
    if (n == "beats") return 1;
    if (n == "bars") return 2;
    if (n == "countin") return 3;
    if (n == "clickall") return 4;
    if (n == "clickvol") return 5;
    if (n == "rstall") return 6;
    if (n == "stopall") return 7;
    if (n == "playall") return 8;
    if (n == "scene") return 9;
    if (n == "rs_go") return 10;
    if (n == "rs_bl") return 11;
    if (n == "rs_bts") return 12;
    if (n == "rs_len0") return 13;
    if (n == "rs_mask0") return 14;
    if (n == "rs_spd0") return 15;
    if (n == "rs_len1") return 16;
    if (n == "rs_mask1") return 17;
    if (n == "rs_spd1") return 18;
    if (n == "rs_len2") return 19;
    if (n == "rs_mask2") return 20;
    if (n == "rs_spd2") return 21;
    if (n == "rs_len3") return 22;
    if (n == "rs_mask3") return 23;
    if (n == "rs_spd3") return 24;
    if (n == "rs_ntr") return 25;
    if (n == "imp_go") return 26;
    if (n == "imp_trk") return 27;
    if (n == "imp_sr") return 28;
    if (n == "prac") return 29;
    if (n == "recnext") return 30;
    if (n == "playstop") return 31;
    if (n == "undo") return 32;
    if (n == "scnext") return 33;
    if (n == "ntr") return 34;
    if (n == "pvw") return 35;
    if (n == "lat") return 36;
    if (n == "rec1") return 37;
    if (n == "clr1") return 38;
    if (n == "tog1") return 39;
    if (n == "spd1") return 40;
    if (n == "src1") return 41;
    if (n == "rec2") return 42;
    if (n == "clr2") return 43;
    if (n == "tog2") return 44;
    if (n == "spd2") return 45;
    if (n == "src2") return 46;
    if (n == "rec3") return 47;
    if (n == "clr3") return 48;
    if (n == "tog3") return 49;
    if (n == "spd3") return 50;
    if (n == "src3") return 51;
    if (n == "rec4") return 52;
    if (n == "clr4") return 53;
    if (n == "tog4") return 54;
    if (n == "spd4") return 55;
    if (n == "src4") return 56;
    if (n == "rec5") return 57;
    if (n == "clr5") return 58;
    if (n == "tog5") return 59;
    if (n == "spd5") return 60;
    if (n == "src5") return 61;
    if (n == "rec6") return 62;
    if (n == "clr6") return 63;
    if (n == "tog6") return 64;
    if (n == "spd6") return 65;
    if (n == "src6") return 66;
    if (n == "rec7") return 67;
    if (n == "clr7") return 68;
    if (n == "tog7") return 69;
    if (n == "spd7") return 70;
    if (n == "src7") return 71;
    if (n == "rec8") return 72;
    if (n == "clr8") return 73;
    if (n == "tog8") return 74;
    if (n == "spd8") return 75;
    if (n == "src8") return 76;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 30.0) v = 30.0; if (v > 300.0) v = 300.0; P_bpm = v; break;
      case 1: if (v < 1.0) v = 1.0; if (v > 16.0) v = 16.0; P_beats = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 32.0) v = 32.0; P_bars = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_countin = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_clickall = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_clickvol = v; break;
      case 6: P_rstall = v; break;
      case 7: P_stopall = v; break;
      case 8: P_playall = v; break;
      case 9: P_scene = v; break;
      case 10: P_rs_go = v; break;
      case 11: P_rs_bl = v; break;
      case 12: P_rs_bts = v; break;
      case 13: P_rs_len0 = v; break;
      case 14: P_rs_mask0 = v; break;
      case 15: P_rs_spd0 = v; break;
      case 16: P_rs_len1 = v; break;
      case 17: P_rs_mask1 = v; break;
      case 18: P_rs_spd1 = v; break;
      case 19: P_rs_len2 = v; break;
      case 20: P_rs_mask2 = v; break;
      case 21: P_rs_spd2 = v; break;
      case 22: P_rs_len3 = v; break;
      case 23: P_rs_mask3 = v; break;
      case 24: P_rs_spd3 = v; break;
      case 25: P_rs_ntr = v; break;
      case 26: P_imp_go = v; break;
      case 27: if (v < 1.0) v = 1.0; if (v > 8.0) v = 8.0; P_imp_trk = v; break;
      case 28: P_imp_sr = v; break;
      case 29: if (v < 50.0) v = 50.0; if (v > 100.0) v = 100.0; P_prac = v; break;
      case 30: P_recnext = v; break;
      case 31: P_playstop = v; break;
      case 32: P_undo = v; break;
      case 33: P_scnext = v; break;
      case 34: P_ntr = v; break;
      case 35: if (v < 0.0) v = 0.0; if (v > 4.0) v = 4.0; P_pvw = v; break;
      case 36: if (v < 0.0) v = 0.0; if (v > 250.0) v = 250.0; P_lat = v; break;
      case 37: P_rec1 = v; break;
      case 38: P_clr1 = v; break;
      case 39: P_tog1 = v; break;
      case 40: P_spd1 = v; break;
      case 41: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src1 = v; break;
      case 42: P_rec2 = v; break;
      case 43: P_clr2 = v; break;
      case 44: P_tog2 = v; break;
      case 45: P_spd2 = v; break;
      case 46: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src2 = v; break;
      case 47: P_rec3 = v; break;
      case 48: P_clr3 = v; break;
      case 49: P_tog3 = v; break;
      case 50: P_spd3 = v; break;
      case 51: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src3 = v; break;
      case 52: P_rec4 = v; break;
      case 53: P_clr4 = v; break;
      case 54: P_tog4 = v; break;
      case 55: P_spd4 = v; break;
      case 56: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src4 = v; break;
      case 57: P_rec5 = v; break;
      case 58: P_clr5 = v; break;
      case 59: P_tog5 = v; break;
      case 60: P_spd5 = v; break;
      case 61: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src5 = v; break;
      case 62: P_rec6 = v; break;
      case 63: P_clr6 = v; break;
      case 64: P_tog6 = v; break;
      case 65: P_spd6 = v; break;
      case 66: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src6 = v; break;
      case 67: P_rec7 = v; break;
      case 68: P_clr7 = v; break;
      case 69: P_tog7 = v; break;
      case 70: P_spd7 = v; break;
      case 71: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src7 = v; break;
      case 72: P_rec8 = v; break;
      case 73: P_clr8 = v; break;
      case 74: P_tog8 = v; break;
      case 75: P_spd8 = v; break;
      case 76: if (v < 1.0) v = 1.0; if (v > 4.0) v = 4.0; P_src8 = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_bpm;
      case 1: return P_beats;
      case 2: return P_bars;
      case 3: return P_countin;
      case 4: return P_clickall;
      case 5: return P_clickvol;
      case 6: return P_rstall;
      case 7: return P_stopall;
      case 8: return P_playall;
      case 9: return P_scene;
      case 10: return P_rs_go;
      case 11: return P_rs_bl;
      case 12: return P_rs_bts;
      case 13: return P_rs_len0;
      case 14: return P_rs_mask0;
      case 15: return P_rs_spd0;
      case 16: return P_rs_len1;
      case 17: return P_rs_mask1;
      case 18: return P_rs_spd1;
      case 19: return P_rs_len2;
      case 20: return P_rs_mask2;
      case 21: return P_rs_spd2;
      case 22: return P_rs_len3;
      case 23: return P_rs_mask3;
      case 24: return P_rs_spd3;
      case 25: return P_rs_ntr;
      case 26: return P_imp_go;
      case 27: return P_imp_trk;
      case 28: return P_imp_sr;
      case 29: return P_prac;
      case 30: return P_recnext;
      case 31: return P_playstop;
      case 32: return P_undo;
      case 33: return P_scnext;
      case 34: return P_ntr;
      case 35: return P_pvw;
      case 36: return P_lat;
      case 37: return P_rec1;
      case 38: return P_clr1;
      case 39: return P_tog1;
      case 40: return P_spd1;
      case 41: return P_src1;
      case 42: return P_rec2;
      case 43: return P_clr2;
      case 44: return P_tog2;
      case 45: return P_spd2;
      case 46: return P_src2;
      case 47: return P_rec3;
      case 48: return P_clr3;
      case 49: return P_tog3;
      case 50: return P_spd3;
      case 51: return P_src3;
      case 52: return P_rec4;
      case 53: return P_clr4;
      case 54: return P_tog4;
      case 55: return P_spd4;
      case 56: return P_src4;
      case 57: return P_rec5;
      case 58: return P_clr5;
      case 59: return P_tog5;
      case 60: return P_spd5;
      case 61: return P_src5;
      case 62: return P_rec6;
      case 63: return P_clr6;
      case 64: return P_tog6;
      case 65: return P_spd6;
      case 66: return P_src6;
      case 67: return P_rec7;
      case 68: return P_clr7;
      case 69: return P_tog7;
      case 70: return P_spd7;
      case 71: return P_src7;
      case 72: return P_rec8;
      case 73: return P_clr8;
      case 74: return P_tog8;
      case 75: return P_spd8;
      case 76: return P_src8;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 77;
  double* data(const std::string& n) {
    if (n == "st") return D_st;
    if (n == "pg") return D_pg;
    if (n == "we") return D_we;
    if (n == "tl") return D_tl;
    if (n == "slen") return D_slen;
    if (n == "rmk") return D_rmk;
    if (n == "lrec") return D_lrec;
    if (n == "lclr") return D_lclr;
    if (n == "ltog") return D_ltog;
    if (n == "cmdr") return D_cmdr;
    if (n == "cmdc") return D_cmdc;
    if (n == "cmdt") return D_cmdt;
    if (n == "ov") return D_ov;
    if (n == "op") return D_op;
    if (n == "spd") return D_spd;
    if (n == "spa") return D_spa;
    if (n == "sgn") return D_sgn;
    if (n == "lspd") return D_lspd;
    if (n == "cmds") return D_cmds;
    if (n == "rsp") return D_rsp;
    if (n == "gph") return D_gph;
    if (n == "gaa") return D_gaa;
    if (n == "gab") return D_gab;
    if (n == "srcd") return D_srcd;
    if (n == "xin") return D_xin;
    if (n == "ntrs") return D_ntrs;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double IN4 = in[3] ? (double) in[3][t] : 0.0;
      double IN5 = in[4] ? (double) in[4][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double OUT6 = 0.0;
      double OUT7 = 0.0;
      double OUT8 = 0.0;
      double OUT9 = 0.0;
      double OUT10 = 0.0;
      double OUT11 = 0.0;
      double OUT12 = 0.0;
      double OUT13 = 0.0;
      double OUT14 = 0.0;
      double OUT15 = 0.0;
      double OUT16 = 0.0;
      double OUT17 = 0.0;
      double OUT18 = 0.0;
      double OUT19 = 0.0;
      double OUT20 = 0.0;
      double OUT21 = 0.0;
      double OUT22 = 0.0;
      double OUT23 = 0.0;
      double OUT24 = 0.0;
      double OUT25 = 0.0;
      double OUT26 = 0.0;
      double OUT27 = 0.0;
      double OUT28 = 0.0;
      double OUT29 = 0.0;
      double OUT30 = 0.0;
      double OUT31 = 0.0;
      double OUT32 = 0.0;
      double OUT33 = 0.0;
      double OUT34 = 0.0;
      double OUT35 = 0.0;
      double OUT36 = 0.0;
      double OUT37 = 0.0;
      double OUT38 = 0.0;
      double OUT39 = 0.0;
      double OUT40 = 0.0;
      double OUT41 = 0.0;
      double OUT42 = 0.0;
      double OUT43 = 0.0;
      double OUT44 = 0.0;
      double OUT45 = 0.0;
      double OUT46 = 0.0;
      double OUT47 = 0.0;
      double OUT48 = 0.0;
      double OUT49 = 0.0;
      double v_cs = 0.0;
      double v_pscn = 0.0;
      double v_ps = 0.0;
      double v_run = 0.0;
      double v_cin = 0.0;
      double v_cpos = 0.0;
      double v_ctrk = 0.0;
      double v_fst = 0.0;
      double v_rp = 0.0;
      double v_stp = 0.0;
      double v_tgt = 0.0;
      double v_lbeat = 0.0;
      double v_pin = 0.0;
      double v_xk = 0.0;
      double v_pk = 0.0;
      double v_xf = 0.0;
      double v_ntn = 0.0;
      double v_latS = 0.0;
      double v_frames = 0.0;
      double v_pframes = 0.0;
      double v_fade = 0.0;
      double v_xfs = 0.0;
      double v_startplay = 0.0;
      double v_lnn = 0.0;
      double v_v = 0.0;
      double v_vv = 0.0;
      double v_k = 0.0;
      double v_i = 0.0;
      double v_anyl = 0.0;
      double v_idle = 0.0;
      double v_bl = 0.0;
      double v_bts = 0.0;
      double v_brl = 0.0;
      double v_nv = 0.0;
      double v_mk = 0.0;
      double v_s = 0.0;
      double v_psc = 0.0;
      double v_running = 0.0;
      double v_dostop = 0.0;
      double v_doplay = 0.0;
      double v_sq = 0.0;
      double v_t = 0.0;
      double v_rrate = 0.0;
      double v_flen = 0.0;
      double v_src = 0.0;
      double v_L = 0.0;
      double v_nbar = 0.0;
      double v_isc = 0.0;
      double v_ch2 = 0.0;
      double v_ed = 0.0;
      double v_j = 0.0;
      double v_ip = 0.0;
      double v_sp = 0.0;
      double v_y = 0.0;
      double v_i0 = 0.0;
      double v_fr = 0.0;
      double v_a = 0.0;
      double v_b = 0.0;
      double v_iw = 0.0;
      double v_uk = 0.0;
      double v_us = 0.0;
      double v_nfull = 0.0;
      double v_ok = 0.0;
      double v_up = 0.0;
      double v_vrec = 0.0;
      double v_recok = 0.0;
      double v_busy = 0.0;
      double v_sc2 = 0.0;
      double v_cc = 0.0;
      double v_tg = 0.0;
      double v_rc = 0.0;
      double v_pressed = 0.0;
      double v_fin = 0.0;
      double v_finlen = 0.0;
      double v_finpos = 0.0;
      double v_nb = 0.0;
      double v_q = 0.0;
      double v_bpos = 0.0;
      double v_bi = 0.0;
      double v_idx = 0.0;
      double v_lstart = 0.0;
      double v_barn = 0.0;
      double v_isbar = 0.0;
      double v_rt = 0.0;
      double v_pmt = 0.0;
      double v_pm = 0.0;
      double v_gsz = 0.0;
      double v_w = 0.0;
      double v_gtar = 0.0;
      double v_gg = 0.0;
      double v_md = 0.0;
      double v_sgt = 0.0;
      double v_sg = 0.0;
      double v_il = 0.0;
      double v_lcx = 0.0;
      double v_ri = 0.0;
      double v_dr_a = 0.0;
      double v_dr_b = 0.0;
      double v_dr_i0 = 0.0;
      double v_dr_fr = 0.0;
      double v_dr_i1 = 0.0;
      double v_mr = 0.0;
      double v_gp0 = 0.0;
      double v_gp = 0.0;
      double v_nwa = 0.0;
      double v_nwb = 0.0;
      double v_gq0 = 0.0;
      double v_qo = 0.0;
      double v_anc = 0.0;
      double v_wbest = 0.0;
      double v_wd = 0.0;
      double v_wi = 0.0;
      double v_wc = 0.0;
      double v_w1c = 0.0;
      double v_w1e = 0.0;
      double v_wj = 0.0;
      double v_wsa = 0.0;
      double v_wsb = 0.0;
      double v_wpa = 0.0;
      double v_wpb = 0.0;
      double v_wc0 = 0.0;
      double v_w2c = 0.0;
      double v_w2e = 0.0;
      double v_gq = 0.0;
      double v_qa = 0.0;
      double v_qb = 0.0;
      double v_ga_a = 0.0;
      double v_ga_b = 0.0;
      double v_ga_i0 = 0.0;
      double v_ga_fr = 0.0;
      double v_ga_i1 = 0.0;
      double v_ya = 0.0;
      double v_gb_a = 0.0;
      double v_gb_b = 0.0;
      double v_gb_i0 = 0.0;
      double v_gb_fr = 0.0;
      double v_gb_i1 = 0.0;
      double v_yb = 0.0;
      double v_gv = 0.0;
      double v_pix = 0.0;
      double v_osn = 0.0;
      double v_oln = 0.0;
      double v_xg = 0.0;
      double v_om = 0.0;
      double v_ol2 = 0.0;
      double v_olc = 0.0;
      double v_oi = 0.0;
      double v_need = 0.0;
      double v_ce = 0.0;
      double v_cph = 0.0;
      double v_click = 0.0;
      double v_pv = 0.0;
      double v_pvs = 0.0;
      double v_pln = 0.0;
      double v_vgt = 0.0;
      double v_vg = 0.0;
      double v_vp = 0.0;
      double v_vpl = 0.0;
      double v_pnt = 0.0;
      double v_pm2 = 0.0;
      double v_pvi = 0.0;
      double v_pa = 0.0;
      double v_pb = 0.0;
      double v_mlt = 0.0;
      double v_nbars = 0.0;
      double v_barno = 0.0;
      double v_beatno = 0.0;
      double v_tr = 0.0;
      double v_smask = 0.0;
      double v_spk = 0.0;
      double v_sv0 = 0.0;
      double v_sv1 = 0.0;
      double v_sv2 = 0.0;
      double v_sv3 = 0.0;
      double v_sk = 0.0;
      double v_tmk0 = 0.0;
      double v_tmk1 = 0.0;
      double v_tmk2 = 0.0;
      double v_tmk3 = 0.0;
      double v_lph = 0.0;
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src1 - (1.0)),(0.0),(3.0)),(0.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src2 - (1.0)),(0.0),(3.0)),(1.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src3 - (1.0)),(0.0),(3.0)),(2.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src4 - (1.0)),(0.0),(3.0)),(3.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src5 - (1.0)),(0.0),(3.0)),(4.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src6 - (1.0)),(0.0),(3.0)),(5.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src7 - (1.0)),(0.0),(3.0)),(6.0),0);
      g_poke(D_srcd,LEN_srcd,CH_srcd,g_clamp((P_src8 - (1.0)),(0.0),(3.0)),(7.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec1,(0.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr1,(0.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog1,(0.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd1,(0.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec2,(1.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr2,(1.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog2,(1.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd2,(1.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec3,(2.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr3,(2.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog3,(2.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd3,(2.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec4,(3.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr4,(3.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog4,(3.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd4,(3.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec5,(4.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr5,(4.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog5,(4.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd5,(4.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec6,(5.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr6,(5.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog6,(5.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd6,(5.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec7,(6.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr7,(6.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog7,(6.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd7,(6.0),0);
      g_poke(D_cmdr,LEN_cmdr,CH_cmdr,P_rec8,(7.0),0);
      g_poke(D_cmdc,LEN_cmdc,CH_cmdc,P_clr8,(7.0),0);
      g_poke(D_cmdt,LEN_cmdt,CH_cmdt,P_tog8,(7.0),0);
      g_poke(D_cmds,LEN_cmds,CH_cmds,P_spd8,(7.0),0);
      v_cs = H_g_cs;
      v_pscn = H_g_ps;
      v_ps = H_g_pos;
      v_run = H_g_run;
      v_cin = H_g_cin;
      v_cpos = H_g_cpos;
      v_ctrk = H_g_ctrk;
      v_fst = H_g_first;
      v_rp = H_g_rp;
      v_stp = H_g_stop;
      v_tgt = H_g_tgt;
      v_lbeat = H_g_lbeat;
      g_poke(D_xin,LEN_xin,CH_xin,IN1,(0.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN2,(1.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN3,(2.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN4,(3.0),0);
      v_pin = IN5;
      v_xk = (0.0);
      v_pk = (0.0);
      v_xf = (0.0);
      v_ntn = (4.0);
      v_latS = floor(((P_lat)*SR/1000.0));
      v_frames = ((double)LEN_lbA);
      v_pframes = ((double)LEN_pbA);
      v_fade = floor((((6.0))*SR/1000.0));
      v_xfs = (((30.0))*SR/1000.0);
      v_startplay = (0.0);
      v_lnn = g_peek(D_slen,LEN_slen,CH_slen,v_cs,0);
      v_v = (0.0);
      v_vv = (0.0);
      if (((double)((H_g_init) == ((0.0)))) != 0.0) {
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          g_poke(D_lrec,LEN_lrec,CH_lrec,g_peek(D_cmdr,LEN_cmdr,CH_cmdr,v_k,0),v_k,0);
          g_poke(D_lclr,LEN_lclr,CH_lclr,g_peek(D_cmdc,LEN_cmdc,CH_cmdc,v_k,0),v_k,0);
          g_poke(D_ltog,LEN_ltog,CH_ltog,g_peek(D_cmdt,LEN_cmdt,CH_cmdt,v_k,0),v_k,0);
        }
        H_g_lres = P_rstall;
        H_g_lstop = P_stopall;
        H_g_lplay = P_playall;
        H_g_lscn = P_scene;
        H_g_lrs = P_rs_go;
        H_g_limp = P_imp_go;
        H_g_lrn = P_recnext;
        H_g_lps = P_playstop;
        H_g_lun = P_undo;
        H_g_lsn = P_scnext;
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          g_poke(D_lspd,LEN_lspd,CH_lspd,g_peek(D_cmds,LEN_cmds,CH_cmds,v_k,0),v_k,0);
          g_poke(D_sgn,LEN_sgn,CH_sgn,(1.0),v_k,0);
        }
        for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
          g_poke(D_ntrs,LEN_ntrs,CH_ntrs,(4.0),v_i,0);
        }
        H_g_lnt = P_ntr;
        H_g_init = (1.0);
      }
      if (((double)((P_ntr) != (H_g_lnt))) != 0.0) {
        H_g_lnt = P_ntr;
        if (((double)((fmod(P_ntr,(16.0))) >= ((1.0)))) != 0.0) {
          g_poke(D_ntrs,LEN_ntrs,CH_ntrs,g_clamp(floor(fmod(P_ntr,(16.0))),(1.0),(8.0)),v_cs,0);
        }
      }
      v_anyl = (0.0);
      for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
        if (((double)((g_peek(D_slen,LEN_slen,CH_slen,v_i,0)) > ((0.0)))) != 0.0) {
          v_anyl = (1.0);
        }
      }
      v_idle = ((double)((((double)((((double)((((double)((v_anyl) == ((0.0))))) && (((double)((v_cin) == ((0.0)))))))) && (((double)((v_fst) < ((0.0)))))))) && (((double)((H_g_imp) == ((0.0)))))));
      v_bl = H_g_bl;
      v_bts = H_g_bts;
      if (v_idle != 0.0) {
        v_bl = ((SR * (60.0)) / P_bpm);
        v_bts = P_beats;
      }
      v_brl = (v_bl * v_bts);
      if (((double)((P_rstall) != (H_g_lres))) != 0.0) {
        H_g_lres = P_rstall;
        for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
          g_poke(D_slen,LEN_slen,CH_slen,(0.0),v_i,0);
          for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
            g_poke(D_st,LEN_st,CH_st,(0.0),v_k,v_i);
            g_poke(D_pg,LEN_pg,CH_pg,(0.0),v_k,v_i);
          }
        }
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          g_poke(D_we,LEN_we,CH_we,(0.0),v_k,0);
          g_poke(D_tl,LEN_tl,CH_tl,(0.0),v_k,0);
          g_poke(D_spa,LEN_spa,CH_spa,(0.0),v_k,0);
          g_poke(D_sgn,LEN_sgn,CH_sgn,(1.0),v_k,0);
          for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
            g_poke(D_spd,LEN_spd,CH_spd,(0.0),v_k,v_i);
          }
        }
        for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
          g_poke(D_ntrs,LEN_ntrs,CH_ntrs,(4.0),v_i,0);
        }
        H_g_ut = (0.0);
        H_g_ur = (0.0);
        H_g_lc = (0.0);
        v_lnn = (0.0);
        v_ps = (0.0);
        v_run = (0.0);
        v_cin = (0.0);
        v_cpos = (0.0);
        v_ctrk = (-(1.0));
        v_fst = (-(1.0));
        v_rp = (0.0);
        v_stp = (0.0);
        v_tgt = (0.0);
        v_lbeat = (-(1.0));
        v_cs = (0.0);
        v_pscn = (-(1.0));
        H_g_os = (-(1.0));
        H_g_xf = (0.0);
        H_g_imp = (0.0);
      }
      if (((double)((P_rs_go) != (H_g_lrs))) != 0.0) {
        H_g_lrs = P_rs_go;
        g_poke(D_slen,LEN_slen,CH_slen,P_rs_len0,(0.0),0);
        g_poke(D_rmk,LEN_rmk,CH_rmk,P_rs_mask0,(0.0),0);
        g_poke(D_rsp,LEN_rsp,CH_rsp,P_rs_spd0,(0.0),0);
        g_poke(D_slen,LEN_slen,CH_slen,P_rs_len1,(1.0),0);
        g_poke(D_rmk,LEN_rmk,CH_rmk,P_rs_mask1,(1.0),0);
        g_poke(D_rsp,LEN_rsp,CH_rsp,P_rs_spd1,(1.0),0);
        g_poke(D_slen,LEN_slen,CH_slen,P_rs_len2,(2.0),0);
        g_poke(D_rmk,LEN_rmk,CH_rmk,P_rs_mask2,(2.0),0);
        g_poke(D_rsp,LEN_rsp,CH_rsp,P_rs_spd2,(2.0),0);
        g_poke(D_slen,LEN_slen,CH_slen,P_rs_len3,(3.0),0);
        g_poke(D_rmk,LEN_rmk,CH_rmk,P_rs_mask3,(3.0),0);
        g_poke(D_rsp,LEN_rsp,CH_rsp,P_rs_spd3,(3.0),0);
        v_bl = fmax((100.0),P_rs_bl);
        v_bts = fmax((1.0),P_rs_bts);
        v_brl = (v_bl * v_bts);
        for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
          v_nv = fmod(floor((P_rs_ntr / pow((16.0),v_i))),(16.0));
          if (((double)((((double)((v_nv) < ((1.0))))) || (((double)((v_nv) > ((8.0))))))) != 0.0) {
            v_nv = (4.0);
          }
          g_poke(D_ntrs,LEN_ntrs,CH_ntrs,v_nv,v_i,0);
        }
        for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
          v_mk = g_peek(D_rmk,LEN_rmk,CH_rmk,v_i,0);
          for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
            v_s = (0.0);
            if (((double)((((double)((g_peek(D_slen,LEN_slen,CH_slen,v_i,0)) > ((0.0))))) && (((double)((fmod(floor((v_mk / pow((2.0),v_k))),(2.0))) == ((1.0))))))) != 0.0) {
              v_s = (3.0);
            }
            g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_i);
            g_poke(D_pg,LEN_pg,CH_pg,(0.0),v_k,v_i);
            g_poke(D_spd,LEN_spd,CH_spd,fmod(floor((g_peek(D_rsp,LEN_rsp,CH_rsp,v_i,0) / pow((4.0),v_k))),(4.0)),v_k,v_i);
          }
        }
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          g_poke(D_spa,LEN_spa,CH_spa,g_peek(D_spd,LEN_spd,CH_spd,v_k,(0.0)),v_k,0);
          g_poke(D_sgn,LEN_sgn,CH_sgn,(1.0),v_k,0);
        }
        H_g_ut = (0.0);
        H_g_ur = (0.0);
        H_g_lc = (0.0);
        v_cs = (0.0);
        v_pscn = (-(1.0));
        H_g_os = (-(1.0));
        H_g_xf = (0.0);
        v_run = (0.0);
        v_ps = (0.0);
        v_cin = (0.0);
        v_fst = (-(1.0));
        v_rp = (0.0);
        v_stp = (0.0);
        v_tgt = (0.0);
        v_lnn = g_peek(D_slen,LEN_slen,CH_slen,(0.0),0);
      }
      v_psc = (0.0);
      if (((double)((P_playstop) != (H_g_lps))) != 0.0) {
        H_g_lps = P_playstop;
        v_psc = (1.0);
      }
      v_running = ((double)((((double)((((double)((v_run) > ((0.0))))) || (((double)((v_cin) > ((0.0)))))))) || (((double)((v_fst) >= ((0.0)))))));
      v_dostop = (0.0);
      if (((double)((P_stopall) != (H_g_lstop))) != 0.0) {
        H_g_lstop = P_stopall;
        v_dostop = (1.0);
      }
      if (((double)((((double)((v_psc) > ((0.0))))) && (v_running))) != 0.0) {
        v_dostop = (1.0);
      }
      v_doplay = (0.0);
      if (((double)((P_playall) != (H_g_lplay))) != 0.0) {
        H_g_lplay = P_playall;
        v_doplay = (1.0);
      }
      if (((double)((((double)((v_psc) > ((0.0))))) && (((double)((v_running) == ((0.0))))))) != 0.0) {
        v_doplay = (1.0);
      }
      if (((double)((v_dostop) > ((0.0)))) != 0.0) {
        if (((double)((v_fst) >= ((0.0)))) != 0.0) {
          g_poke(D_st,LEN_st,CH_st,(0.0),v_fst,v_cs);
          v_fst = (-(1.0));
          v_rp = (0.0);
          v_stp = (0.0);
          v_tgt = (0.0);
        }
        if (((double)((v_cin) == ((1.0)))) != 0.0) {
          g_poke(D_st,LEN_st,CH_st,(0.0),v_ctrk,v_cs);
          v_ctrk = (-(1.0));
        }
        v_cin = (0.0);
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
          if (((double)((((double)((v_s) == ((1.0))))) || (((double)((v_s) == ((2.0))))))) != 0.0) {
            v_s = (0.0);
          }
          if (((double)((((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((v_s) == ((9.0))))))) != 0.0) {
            v_s = (3.0);
          }
          if (((double)((v_s) == ((10.0)))) != 0.0) {
            v_s = (8.0);
          }
          g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_cs);
          g_poke(D_we,LEN_we,CH_we,(0.0),v_k,0);
        }
        v_run = (0.0);
        v_ps = (0.0);
        v_pscn = (-(1.0));
      }
      if (((double)((v_doplay) > ((0.0)))) != 0.0) {
        if (((double)((((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) == ((0.0)))))))) && (((double)((v_cin) == ((0.0))))))) != 0.0) {
          v_startplay = (1.0);
        }
      }
      v_sq = (-(1.0));
      if (((double)((P_scene) != (H_g_lscn))) != 0.0) {
        H_g_lscn = P_scene;
        v_sq = (fmod(P_scene,(8.0)) - (1.0));
      }
      if (((double)((P_scnext) != (H_g_lsn))) != 0.0) {
        H_g_lsn = P_scnext;
        v_sq = (v_cs + (1.0));
        if (((double)((v_pscn) >= ((0.0)))) != 0.0) {
          v_sq = (v_pscn + (1.0));
        }
        v_sq = fmod(v_sq,(4.0));
      }
      if (((double)((v_sq) >= ((0.0)))) != 0.0) {
        if (((double)((v_sq) <= ((3.0)))) != 0.0) {
          if (((double)((v_sq) == (v_cs))) != 0.0) {
            v_pscn = (-(1.0));
          } else {
            if (((double)((((double)((((double)((v_run) == ((0.0))))) && (((double)((v_cin) == ((0.0)))))))) && (((double)((v_fst) < ((0.0))))))) != 0.0) {
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
                if (((double)((v_s) == ((1.0)))) != 0.0) {
                  v_s = (0.0);
                }
                if (((double)((((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((v_s) == ((9.0))))))) != 0.0) {
                  v_s = (3.0);
                }
                if (((double)((v_s) == ((10.0)))) != 0.0) {
                  v_s = (8.0);
                }
                g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_cs);
                g_poke(D_pg,LEN_pg,CH_pg,(0.0),v_k,v_sq);
                g_poke(D_spa,LEN_spa,CH_spa,g_peek(D_spd,LEN_spd,CH_spd,v_k,v_sq),v_k,0);
                g_poke(D_sgn,LEN_sgn,CH_sgn,(1.0),v_k,0);
              }
              v_cs = v_sq;
              v_ps = (0.0);
              v_pscn = (-(1.0));
              v_lnn = g_peek(D_slen,LEN_slen,CH_slen,v_cs,0);
              H_g_lc = (0.0);
              if (((double)((v_lnn) > ((0.0)))) != 0.0) {
                v_startplay = (1.0);
              }
            } else {
              v_pscn = v_sq;
            }
          }
        }
      }
      if (((double)((P_imp_go) != (H_g_limp))) != 0.0) {
        H_g_limp = P_imp_go;
        if (((double)((((double)((((double)((((double)((H_g_imp) == ((0.0))))) && (((double)((v_fst) < ((0.0)))))))) && (((double)((v_cin) == ((0.0)))))))) && (((double)((((double)LEN_imp)) > ((16.0))))))) != 0.0) {
          v_t = (P_imp_trk - (1.0));
          v_rrate = (1.0);
          if (((double)((((double)((P_imp_sr) > ((7999.0))))) && (((double)((P_imp_sr) < ((400000.0))))))) != 0.0) {
            v_rrate = (P_imp_sr / SR);
          }
          v_flen = ((double)LEN_imp);
          v_src = floor((v_flen / v_rrate));
          v_L = v_lnn;
          H_g_istart = ((double)((v_L) == ((0.0))));
          if (((double)((v_L) == ((0.0)))) != 0.0) {
            if (((double)((v_anyl) == ((0.0)))) != 0.0) {
              v_nbar = fmax((1.0),floor(((v_src / v_brl) + (0.5))));
              v_bl = (v_src / (v_nbar * v_bts));
              v_brl = (v_bl * v_bts);
              v_L = fmin(v_src,(v_frames - (2.0)));
            } else {
              v_nbar = fmax((1.0),floor(((v_src / v_brl) + (0.5))));
              v_L = fmin(floor(((v_nbar * v_brl) + (0.5))),(v_frames - (2.0)));
            }
            g_poke(D_slen,LEN_slen,CH_slen,v_L,v_cs,0);
            v_lnn = v_L;
            v_ps = (0.0);
          }
          g_poke(D_st,LEN_st,CH_st,(0.0),v_t,v_cs);
          H_g_imp = (1.0);
          H_g_ipos = (0.0);
          H_g_itrk = v_t;
          H_g_iscn = v_cs;
          H_g_ilen = v_L;
          H_g_irate = v_rrate;
          H_g_iflen = v_flen;
        }
      }
      if (((double)((H_g_imp) > ((0.0)))) != 0.0) {
        v_t = H_g_itrk;
        v_isc = H_g_iscn;
        v_ch2 = ((double)CH_imp);
        v_ed = (((4.0))*SR/1000.0);
        for (v_j = (0.0); ((double)((v_j) < ((8192.0)))) != 0.0; v_j = v_j + ((1.0))) {
          v_ip = H_g_ipos;
          if (((double)((v_ip) < (H_g_ilen))) != 0.0) {
            v_sp = (v_ip * H_g_irate);
            v_y = (0.0);
            if (((double)((v_sp) < ((H_g_iflen - (1.0))))) != 0.0) {
              v_i0 = floor(v_sp);
              v_fr = (v_sp - v_i0);
              v_a = g_peek(D_imp,LEN_imp,CH_imp,v_i0,(0.0));
              v_b = g_peek(D_imp,LEN_imp,CH_imp,(v_i0 + (1.0)),(0.0));
              v_y = (v_a + ((v_b - v_a) * v_fr));
              if (((double)((v_ch2) > ((1.0)))) != 0.0) {
                v_a = g_peek(D_imp,LEN_imp,CH_imp,v_i0,(1.0));
                v_b = g_peek(D_imp,LEN_imp,CH_imp,(v_i0 + (1.0)),(1.0));
                v_y = (((v_y + v_a) + ((v_b - v_a) * v_fr)) * (0.5));
              }
            }
            v_y = ((v_y * fmin((1.0),(v_ip / v_ed))) * fmin((1.0),((H_g_ilen - v_ip) / v_ed)));
            v_iw = (v_ip + v_latS);
            if (((double)((v_iw) >= (H_g_ilen))) != 0.0) {
              v_iw = (v_iw - (H_g_ilen * floor((v_iw / H_g_ilen))));
            }
            if (((double)((v_isc) == ((0.0)))) != 0.0) {
              g_poke(D_lbA,LEN_lbA,CH_lbA,v_y,v_iw,v_t);
            } else {
              if (((double)((v_isc) == ((1.0)))) != 0.0) {
                g_poke(D_lbB,LEN_lbB,CH_lbB,v_y,v_iw,v_t);
              } else {
                if (((double)((v_isc) == ((2.0)))) != 0.0) {
                  g_poke(D_lbC,LEN_lbC,CH_lbC,v_y,v_iw,v_t);
                } else {
                  if (((double)((v_isc) == ((3.0)))) != 0.0) {
                    g_poke(D_lbD,LEN_lbD,CH_lbD,v_y,v_iw,v_t);
                  }
                }
              }
            }
            if (((double)((fmod(v_ip,(64.0))) == ((0.0)))) != 0.0) {
              if (((double)((v_isc) == ((0.0)))) != 0.0) {
                g_poke(D_pbA,LEN_pbA,CH_pbA,(-(1.0)),floor((v_ip / (64.0))),v_t);
              } else {
                if (((double)((v_isc) == ((1.0)))) != 0.0) {
                  g_poke(D_pbB,LEN_pbB,CH_pbB,(-(1.0)),floor((v_ip / (64.0))),v_t);
                } else {
                  if (((double)((v_isc) == ((2.0)))) != 0.0) {
                    g_poke(D_pbC,LEN_pbC,CH_pbC,(-(1.0)),floor((v_ip / (64.0))),v_t);
                  } else {
                    if (((double)((v_isc) == ((3.0)))) != 0.0) {
                      g_poke(D_pbD,LEN_pbD,CH_pbD,(-(1.0)),floor((v_ip / (64.0))),v_t);
                    }
                  }
                }
              }
            }
            H_g_ipos = (v_ip + (1.0));
          }
        }
        if (((double)((H_g_ipos) >= (H_g_ilen))) != 0.0) {
          H_g_imp = (0.0);
          v_s = (3.0);
          if (((double)((((double)((v_run) > ((0.0))))) && (((double)((v_isc) == (v_cs)))))) != 0.0) {
            v_s = (9.0);
          }
          g_poke(D_st,LEN_st,CH_st,v_s,v_t,v_isc);
          g_poke(D_spd,LEN_spd,CH_spd,(0.0),v_t,v_isc);
          H_g_ut = (1.0);
          H_g_uk = v_t;
          H_g_us = v_isc;
          if (((double)((((double)((((double)((((double)((((double)((H_g_istart) > ((0.0))))) && (((double)((v_isc) == (v_cs))))))) && (((double)((v_run) == ((0.0)))))))) && (((double)((v_cin) == ((0.0)))))))) && (((double)((v_fst) < ((0.0))))))) != 0.0) {
            v_startplay = (1.0);
          }
        }
      }
      if (((double)((P_undo) != (H_g_lun))) != 0.0) {
        H_g_lun = P_undo;
        if (((double)((H_g_ut) == ((1.0)))) != 0.0) {
          v_uk = H_g_uk;
          v_us = H_g_us;
          g_poke(D_st,LEN_st,CH_st,(0.0),v_uk,v_us);
          if (((double)((v_us) == (v_cs))) != 0.0) {
            g_poke(D_tl,LEN_tl,CH_tl,(0.0),v_uk,0);
            g_poke(D_we,LEN_we,CH_we,(0.0),v_uk,0);
          }
          v_nfull = (0.0);
          for (v_j = (0.0); ((double)((v_j) < ((8.0)))) != 0.0; v_j = v_j + ((1.0))) {
            if (((double)((g_peek(D_st,LEN_st,CH_st,v_j,v_us)) != ((0.0)))) != 0.0) {
              v_nfull = (v_nfull + (1.0));
            }
          }
          if (((double)((v_nfull) == ((0.0)))) != 0.0) {
            g_poke(D_slen,LEN_slen,CH_slen,(0.0),v_us,0);
            if (((double)((v_us) == (v_cs))) != 0.0) {
              v_lnn = (0.0);
            }
          }
          H_g_ut = (0.0);
        } else {
          if (((double)((((double)((H_g_ut) == ((2.0))))) && (((double)((H_g_ur) == ((0.0))))))) != 0.0) {
            v_s = g_peek(D_st,LEN_st,CH_st,H_g_uk,H_g_us);
            if (((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0))))))) != 0.0) {
              g_poke(D_st,LEN_st,CH_st,(3.0),H_g_uk,H_g_us);
            }
            H_g_ur = (1.0);
            H_g_upos = (0.0);
          }
        }
      }
      if (((double)((H_g_ur) == ((1.0)))) != 0.0) {
        v_ok = (1.0);
        if (((double)((H_g_us) == (v_cs))) != 0.0) {
          v_ok = ((double)((((double)((g_peek(D_sgn,LEN_sgn,CH_sgn,H_g_uk,0)) <= ((0.0))))) && (((double)((g_peek(D_we,LEN_we,CH_we,H_g_uk,0)) <= ((0.0)))))));
        }
        if (v_ok != 0.0) {
          H_g_ur = (2.0);
        }
      }
      if (((double)((H_g_ur) == ((2.0)))) != 0.0) {
        v_uk = H_g_uk;
        v_us = H_g_us;
        for (v_j = (0.0); ((double)((v_j) < ((1024.0)))) != 0.0; v_j = v_j + ((1.0))) {
          v_up = H_g_upos;
          if (((double)((v_up) < (H_g_ucnt))) != 0.0) {
            v_v = g_peek(D_ub,LEN_ub,CH_ub,v_up,(0.0));
            if (((double)((v_us) == ((0.0)))) != 0.0) {
              g_poke(D_lbA,LEN_lbA,CH_lbA,v_v,v_up,v_uk);
            } else {
              if (((double)((v_us) == ((1.0)))) != 0.0) {
                g_poke(D_lbB,LEN_lbB,CH_lbB,v_v,v_up,v_uk);
              } else {
                if (((double)((v_us) == ((2.0)))) != 0.0) {
                  g_poke(D_lbC,LEN_lbC,CH_lbC,v_v,v_up,v_uk);
                } else {
                  if (((double)((v_us) == ((3.0)))) != 0.0) {
                    g_poke(D_lbD,LEN_lbD,CH_lbD,v_v,v_up,v_uk);
                  }
                }
              }
            }
            H_g_upos = (v_up + (1.0));
          }
        }
        if (((double)((H_g_upos) >= (H_g_ucnt))) != 0.0) {
          H_g_ur = (0.0);
          H_g_ut = (0.0);
        }
      }
      v_vrec = (-(1.0));
      v_recok = ((double)((P_prac) > ((99.5))));
      v_ntn = g_peek(D_ntrs,LEN_ntrs,CH_ntrs,v_cs,0);
      if (((double)((P_recnext) != (H_g_lrn))) != 0.0) {
        H_g_lrn = P_recnext;
        if (((double)((v_fst) >= ((0.0)))) != 0.0) {
          v_vrec = v_fst;
        } else {
          if (((double)((((double)((v_cin) == ((1.0))))) && (((double)((v_ctrk) >= ((0.0))))))) != 0.0) {
            v_vrec = v_ctrk;
          } else {
            for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
              v_s = g_peek(D_st,LEN_st,CH_st,((7.0) - v_k),v_cs);
              if (((double)((((double)((((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((v_s) == ((1.0)))))))) && (((double)((((7.0) - v_k)) < (v_ntn)))))) != 0.0) {
                v_vrec = ((7.0) - v_k);
              }
            }
            if (((double)((v_vrec) < ((0.0)))) != 0.0) {
              v_busy = (0.0);
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                if (((double)((g_peek(D_st,LEN_st,CH_st,v_k,v_cs)) == ((2.0)))) != 0.0) {
                  v_busy = (1.0);
                }
              }
              if (((double)((v_busy) == ((0.0)))) != 0.0) {
                for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                  if (((double)((((double)((g_peek(D_st,LEN_st,CH_st,((7.0) - v_k),v_cs)) == ((0.0))))) && (((double)((((7.0) - v_k)) < (v_ntn)))))) != 0.0) {
                    v_vrec = ((7.0) - v_k);
                  }
                }
              }
            }
          }
        }
      }
      for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
        v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
        v_sc2 = g_peek(D_cmds,LEN_cmds,CH_cmds,v_k,0);
        if (((double)((v_sc2) != (g_peek(D_lspd,LEN_lspd,CH_lspd,v_k,0)))) != 0.0) {
          g_poke(D_lspd,LEN_lspd,CH_lspd,v_sc2,v_k,0);
          if (((double)((((double)((((double)((((double)((((double)((v_s) != ((0.0))))) && (((double)((v_s) != ((1.0)))))))) && (((double)((v_s) != ((2.0)))))))) && (((double)((v_s) != ((6.0)))))))) && (((double)((v_s) != ((7.0))))))) != 0.0) {
            g_poke(D_spd,LEN_spd,CH_spd,fmod((g_peek(D_spd,LEN_spd,CH_spd,v_k,v_cs) + (1.0)),(4.0)),v_k,v_cs);
          }
        }
        v_cc = g_peek(D_cmdc,LEN_cmdc,CH_cmdc,v_k,0);
        if (((double)((v_cc) != (g_peek(D_lclr,LEN_lclr,CH_lclr,v_k,0)))) != 0.0) {
          g_poke(D_lclr,LEN_lclr,CH_lclr,v_cc,v_k,0);
          if (((double)((v_k) == (v_fst))) != 0.0) {
            v_fst = (-(1.0));
            v_rp = (0.0);
            v_stp = (0.0);
            v_tgt = (0.0);
          }
          if (((double)((v_k) == (v_ctrk))) != 0.0) {
            v_cin = (0.0);
            v_ctrk = (-(1.0));
          }
          g_poke(D_tl,LEN_tl,CH_tl,(0.0),v_k,0);
          g_poke(D_spd,LEN_spd,CH_spd,(0.0),v_k,v_cs);
          if (((double)((((double)((v_k) == (H_g_uk)))) && (((double)((v_cs) == (H_g_us)))))) != 0.0) {
            H_g_ut = (0.0);
            H_g_ur = (0.0);
          }
          v_s = (0.0);
          v_nfull = (0.0);
          for (v_j = (0.0); ((double)((v_j) < ((8.0)))) != 0.0; v_j = v_j + ((1.0))) {
            if (((double)((((double)((v_j) != (v_k)))) && (((double)((g_peek(D_st,LEN_st,CH_st,v_j,v_cs)) != ((0.0))))))) != 0.0) {
              v_nfull = (v_nfull + (1.0));
            }
          }
          if (((double)((((double)((v_nfull) == ((0.0))))) && (((double)((v_fst) < ((0.0))))))) != 0.0) {
            g_poke(D_slen,LEN_slen,CH_slen,(0.0),v_cs,0);
            v_lnn = (0.0);
          }
        }
        v_tg = g_peek(D_cmdt,LEN_cmdt,CH_cmdt,v_k,0);
        if (((double)((v_tg) != (g_peek(D_ltog,LEN_ltog,CH_ltog,v_k,0)))) != 0.0) {
          g_poke(D_ltog,LEN_ltog,CH_ltog,v_tg,v_k,0);
          if (((double)((((double)((v_run) == ((0.0))))) && (((double)((v_lnn) > ((0.0))))))) != 0.0) {
            if (((double)((v_s) == ((3.0)))) != 0.0) {
              v_s = (8.0);
            } else {
              if (((double)((v_s) == ((8.0)))) != 0.0) {
                v_s = (3.0);
              }
            }
          } else {
            if (((double)((v_s) == ((3.0)))) != 0.0) {
              v_s = (10.0);
            } else {
              if (((double)((v_s) == ((10.0)))) != 0.0) {
                v_s = (3.0);
              } else {
                if (((double)((v_s) == ((8.0)))) != 0.0) {
                  v_s = (9.0);
                } else {
                  if (((double)((v_s) == ((9.0)))) != 0.0) {
                    v_s = (8.0);
                  } else {
                    if (((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0))))))) != 0.0) {
                      v_s = (10.0);
                    }
                  }
                }
              }
            }
          }
        }
        v_rc = g_peek(D_cmdr,LEN_cmdr,CH_cmdr,v_k,0);
        v_pressed = (0.0);
        if (((double)((v_rc) != (g_peek(D_lrec,LEN_lrec,CH_lrec,v_k,0)))) != 0.0) {
          g_poke(D_lrec,LEN_lrec,CH_lrec,v_rc,v_k,0);
          v_pressed = (1.0);
        }
        if (((double)((v_k) == (v_vrec))) != 0.0) {
          v_pressed = (1.0);
        }
        if (((double)((((double)((((double)((v_pressed) > ((0.0))))) && (v_recok)))) && (((double)((v_k) < (v_ntn)))))) != 0.0) {
          if (((double)((v_s) == ((0.0)))) != 0.0) {
            if (((double)((((double)((v_lnn) > ((0.0))))) || (((double)((v_fst) >= ((0.0))))))) != 0.0) {
              v_s = (1.0);
              if (((double)((((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) == ((0.0)))))))) && (((double)((v_cin) == ((0.0))))))) != 0.0) {
                v_startplay = (1.0);
              }
            } else {
              if (((double)((v_cin) == ((0.0)))) != 0.0) {
                if (((double)((P_countin) > ((0.0)))) != 0.0) {
                  v_cin = (1.0);
                  v_cpos = (0.0);
                  v_ctrk = v_k;
                  v_s = (6.0);
                  v_lbeat = (-(1.0));
                } else {
                  v_fst = v_k;
                  v_rp = (0.0);
                  v_stp = (0.0);
                  v_tgt = (0.0);
                  v_s = (7.0);
                  v_lbeat = (-(1.0));
                }
              }
            }
          } else {
            if (((double)((v_s) == ((7.0)))) != 0.0) {
              if (((double)((((double)((v_stp) == ((0.0))))) && (((double)((P_bars) == ((0.0))))))) != 0.0) {
                v_stp = (1.0);
              }
            } else {
              if (((double)((v_s) == ((6.0)))) != 0.0) {
                v_cin = (0.0);
                v_ctrk = (-(1.0));
                v_s = (0.0);
              } else {
                if (((double)((v_s) == ((1.0)))) != 0.0) {
                  v_s = (0.0);
                } else {
                  if (((double)((((double)((((double)((((double)((v_s) == ((3.0))))) || (((double)((v_s) == ((8.0)))))))) || (((double)((v_s) == ((9.0)))))))) || (((double)((v_s) == ((10.0))))))) != 0.0) {
                    if (((double)((((double)((v_k) != (H_g_auto)))) || (((double)((H_g_since) > ((((0.5) * v_bl) * v_bts))))))) != 0.0) {
                      v_s = (4.0);
                      if (((double)((((double)((v_run) == ((0.0))))) && (((double)((v_cin) == ((0.0))))))) != 0.0) {
                        v_startplay = (1.0);
                      }
                    }
                  } else {
                    if (((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0))))))) != 0.0) {
                      v_s = (3.0);
                    }
                  }
                }
              }
            }
          }
        }
        g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_cs);
      }
      if (((double)((v_startplay) > ((0.0)))) != 0.0) {
        v_ps = (0.0);
        H_g_lc = (0.0);
        if (((double)((P_countin) > ((0.0)))) != 0.0) {
          v_cin = (2.0);
          v_cpos = (0.0);
          v_lbeat = (-(1.0));
        } else {
          v_run = (1.0);
        }
      }
      v_fin = (0.0);
      v_finlen = (0.0);
      v_finpos = (0.0);
      if (((double)((((double)((v_stp) == ((1.0))))) && (((double)((v_fst) >= ((0.0))))))) != 0.0) {
        v_stp = (2.0);
        v_nb = floor((v_rp / v_brl));
        v_q = (v_rp - floor(((v_nb * v_brl) + (0.5))));
        if (((double)((((double)((v_nb) >= ((1.0))))) && (((double)((v_q) < (((0.2) * v_brl))))))) != 0.0) {
          v_fin = (1.0);
          v_finlen = floor(((v_nb * v_brl) + (0.5)));
          v_finpos = v_q;
        } else {
          v_tgt = floor((((v_nb + (1.0)) * v_brl) + (0.5)));
        }
      }
      v_bpos = H_g_free;
      if (((double)((v_cin) > ((0.0)))) != 0.0) {
        v_bpos = v_cpos;
      }
      if (((double)((((double)((v_fst) >= ((0.0))))) && (((double)((v_cin) == ((0.0))))))) != 0.0) {
        v_bpos = v_rp;
      }
      if (((double)((((double)((((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_fst) < ((0.0)))))))) && (((double)((v_cin) == ((0.0)))))))) && (((double)((v_run) > ((0.0))))))) != 0.0) {
        v_bpos = v_ps;
      }
      v_bi = floor((v_bpos / v_bl));
      H_g_free = (H_g_free + (1.0));
      H_g_since = (H_g_since + (1.0));
      for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
        g_poke(D_ov,LEN_ov,CH_ov,(0.0),v_k,0);
        g_poke(D_op,LEN_op,CH_op,(-(1.0)),v_k,0);
      }
      if (((double)((v_cin) > ((0.0)))) != 0.0) {
        v_cpos = (v_cpos + (1.0));
        if (((double)((v_cpos) >= (floor((v_brl + (0.5)))))) != 0.0) {
          if (((double)((v_cin) == ((1.0)))) != 0.0) {
            v_fst = v_ctrk;
            v_ctrk = (-(1.0));
            v_rp = (0.0);
            v_stp = (0.0);
            v_tgt = (0.0);
            g_poke(D_st,LEN_st,CH_st,(7.0),v_fst,v_cs);
          } else {
            v_run = (1.0);
            v_ps = (0.0);
          }
          v_cin = (0.0);
        }
      } else {
        if (((double)((((double)((v_fst) >= ((0.0))))) && (((double)((v_fin) == ((0.0))))))) != 0.0) {
          if (((double)((v_rp) < (v_frames))) != 0.0) {
            v_xf = g_peek(D_xin,LEN_xin,CH_xin,g_peek(D_srcd,LEN_srcd,CH_srcd,v_fst,0),0);
            v_pk = v_pin;
            if (((double)((g_peek(D_srcd,LEN_srcd,CH_srcd,v_fst,0)) > ((0.0)))) != 0.0) {
              v_pk = (-(1.0));
            }
            v_v = (v_xf * fmin((1.0),(v_rp / v_fade)));
            if (((double)((v_cs) == ((0.0)))) != 0.0) {
              g_poke(D_lbA,LEN_lbA,CH_lbA,v_v,v_rp,v_fst);
            } else {
              if (((double)((v_cs) == ((1.0)))) != 0.0) {
                g_poke(D_lbB,LEN_lbB,CH_lbB,v_v,v_rp,v_fst);
              } else {
                if (((double)((v_cs) == ((2.0)))) != 0.0) {
                  g_poke(D_lbC,LEN_lbC,CH_lbC,v_v,v_rp,v_fst);
                } else {
                  if (((double)((v_cs) == ((3.0)))) != 0.0) {
                    g_poke(D_lbD,LEN_lbD,CH_lbD,v_v,v_rp,v_fst);
                  }
                }
              }
            }
            if (((double)((((double)((fmod(v_rp,(64.0))) == ((0.0))))) && (((double)((floor((v_rp / (64.0)))) < (v_pframes)))))) != 0.0) {
              if (((double)((v_cs) == ((0.0)))) != 0.0) {
                g_poke(D_pbA,LEN_pbA,CH_pbA,v_pk,floor((v_rp / (64.0))),v_fst);
              } else {
                if (((double)((v_cs) == ((1.0)))) != 0.0) {
                  g_poke(D_pbB,LEN_pbB,CH_pbB,v_pk,floor((v_rp / (64.0))),v_fst);
                } else {
                  if (((double)((v_cs) == ((2.0)))) != 0.0) {
                    g_poke(D_pbC,LEN_pbC,CH_pbC,v_pk,floor((v_rp / (64.0))),v_fst);
                  } else {
                    if (((double)((v_cs) == ((3.0)))) != 0.0) {
                      g_poke(D_pbD,LEN_pbD,CH_pbD,v_pk,floor((v_rp / (64.0))),v_fst);
                    }
                  }
                }
              }
            }
          }
          v_rp = (v_rp + (1.0));
          if (((double)((((double)((P_bars) > ((0.0))))) && (((double)((v_rp) >= (floor(((P_bars * v_brl) + (0.5))))))))) != 0.0) {
            v_fin = (1.0);
            v_finlen = floor(((P_bars * v_brl) + (0.5)));
            v_finpos = (0.0);
            H_g_auto = v_fst;
            H_g_since = (0.0);
          }
          if (((double)((((double)((((double)((v_stp) == ((2.0))))) && (((double)((v_tgt) > ((0.0)))))))) && (((double)((v_rp) >= (v_tgt)))))) != 0.0) {
            v_fin = (1.0);
            v_finlen = v_tgt;
            v_finpos = (0.0);
          }
          if (((double)((v_rp) >= ((v_frames - (2.0))))) != 0.0) {
            v_fin = (1.0);
            v_finlen = fmax(floor((v_brl + (0.5))),floor(((floor((v_rp / v_brl)) * v_brl) + (0.5))));
            v_finpos = (0.0);
          }
        } else {
          if (((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) > ((0.0))))))) != 0.0) {
            v_idx = v_ps;
            v_lstart = ((double)((((double)((v_idx) == ((0.0))))) || (((double)((H_g_wrap) > ((0.0)))))));
            H_g_wrap = (0.0);
            v_barn = floor(((v_idx / v_brl) + (1e-06)));
            v_isbar = ((double)((((double)((v_barn) != (H_g_lbar)))) || (v_lstart)));
            H_g_lbar = v_barn;
            if (((double)((((double)((((double)((v_isbar) && (((double)((v_pscn) >= ((0.0)))))))) && (((double)((H_g_imp) == ((0.0)))))))) && (((double)((H_g_hold) == ((0.0))))))) != 0.0) {
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
                if (((double)((((double)((v_s) == ((1.0))))) || (((double)((v_s) == ((2.0))))))) != 0.0) {
                  v_s = (0.0);
                }
                if (((double)((((double)((((double)((v_s) == ((4.0))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((v_s) == ((9.0))))))) != 0.0) {
                  v_s = (3.0);
                }
                if (((double)((v_s) == ((10.0)))) != 0.0) {
                  v_s = (8.0);
                }
                g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_cs);
                g_poke(D_we,LEN_we,CH_we,(0.0),v_k,0);
                g_poke(D_tl,LEN_tl,CH_tl,(0.0),v_k,0);
                g_poke(D_pg,LEN_pg,CH_pg,(0.0),v_k,v_pscn);
              }
              H_g_os = v_cs;
              H_g_opos = v_ps;
              H_g_xf = (1.0);
              H_g_olc = H_g_lc;
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                g_poke(D_spa,LEN_spa,CH_spa,g_peek(D_spd,LEN_spd,CH_spd,v_k,v_pscn),v_k,0);
                g_poke(D_sgn,LEN_sgn,CH_sgn,(1.0),v_k,0);
              }
              v_cs = v_pscn;
              v_pscn = (-(1.0));
              v_ps = (0.0);
              v_idx = (0.0);
              v_lnn = g_peek(D_slen,LEN_slen,CH_slen,v_cs,0);
              H_g_lc = (0.0);
              v_lstart = (1.0);
              v_ntn = g_peek(D_ntrs,LEN_ntrs,CH_ntrs,v_cs,0);
              H_g_lbar = (0.0);
              if (((double)((v_lnn) == ((0.0)))) != 0.0) {
                v_run = (0.0);
              }
            }
            if (v_isbar != 0.0) {
              H_g_hold = (0.0);
            }
            if (((double)((v_isbar) && (((double)((v_lnn) > ((0.0))))))) != 0.0) {
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
                if (((double)((v_s) == ((9.0)))) != 0.0) {
                  v_s = (3.0);
                }
                if (((double)((v_s) == ((10.0)))) != 0.0) {
                  v_s = (8.0);
                }
                g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_cs);
              }
            }
            if (((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) > ((0.0))))))) != 0.0) {
              v_busy = (0.0);
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
                if (((double)((((double)((((double)((((double)((((double)((((double)((v_s) == ((1.0))))) || (((double)((v_s) == ((2.0)))))))) || (((double)((v_s) == ((4.0)))))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((g_peek(D_we,LEN_we,CH_we,v_k,0)) > ((0.0)))))))) || (((double)((g_peek(D_tl,LEN_tl,CH_tl,v_k,0)) > ((0.0))))))) != 0.0) {
                  v_busy = (1.0);
                }
              }
              v_rt = (P_prac / (100.0));
              if (((double)((v_busy) > ((0.0)))) != 0.0) {
                v_rt = (1.0);
              }
              v_pmt = ((double)((v_rt) < ((0.999))));
              v_pm = H_g_pm;
              if (((double)((v_pm) < (v_pmt))) != 0.0) {
                v_pm = fmin((1.0),(v_pm + ((1.0) / (((25.0))*SR/1000.0))));
              } else {
                v_pm = fmax((0.0),(v_pm - ((1.0) / (((25.0))*SR/1000.0))));
              }
              H_g_pm = v_pm;
              v_gsz = (((70.0))*SR/1000.0);
              for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
                v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
                if (((double)((((double)((((double)((H_g_imp) > ((0.0))))) && (((double)((H_g_itrk) == (v_k))))))) && (((double)((H_g_iscn) == (v_cs)))))) != 0.0) {
                  v_s = (0.0);
                }
                v_xk = g_peek(D_xin,LEN_xin,CH_xin,g_peek(D_srcd,LEN_srcd,CH_srcd,v_k,0),0);
                v_pk = v_pin;
                if (((double)((g_peek(D_srcd,LEN_srcd,CH_srcd,v_k,0)) > ((0.0)))) != 0.0) {
                  v_pk = (-(1.0));
                }
                if (v_lstart != 0.0) {
                  if (((double)((v_s) == ((1.0)))) != 0.0) {
                    v_s = (2.0);
                    g_poke(D_spd,LEN_spd,CH_spd,(0.0),v_k,v_cs);
                    g_poke(D_spa,LEN_spa,CH_spa,(0.0),v_k,0);
                  } else {
                    if (((double)((v_s) == ((2.0)))) != 0.0) {
                      v_s = (3.0);
                      g_poke(D_tl,LEN_tl,CH_tl,v_fade,v_k,0);
                      H_g_ut = (1.0);
                      H_g_uk = v_k;
                      H_g_us = v_cs;
                    } else {
                      if (((double)((v_s) == ((4.0)))) != 0.0) {
                        v_s = (5.0);
                        g_poke(D_spd,LEN_spd,CH_spd,(0.0),v_k,v_cs);
                        g_poke(D_spa,LEN_spa,CH_spa,(0.0),v_k,0);
                        H_g_ut = (2.0);
                        H_g_uk = v_k;
                        H_g_us = v_cs;
                        H_g_ucnt = (0.0);
                        H_g_ur = (0.0);
                      }
                    }
                  }
                  g_poke(D_st,LEN_st,CH_st,v_s,v_k,v_cs);
                }
                if (((double)((v_s) == ((2.0)))) != 0.0) {
                  v_v = (v_xk * fmin((1.0),(v_idx / v_fade)));
                  if (((double)((v_cs) == ((0.0)))) != 0.0) {
                    g_poke(D_lbA,LEN_lbA,CH_lbA,v_v,v_idx,v_k);
                  } else {
                    if (((double)((v_cs) == ((1.0)))) != 0.0) {
                      g_poke(D_lbB,LEN_lbB,CH_lbB,v_v,v_idx,v_k);
                    } else {
                      if (((double)((v_cs) == ((2.0)))) != 0.0) {
                        g_poke(D_lbC,LEN_lbC,CH_lbC,v_v,v_idx,v_k);
                      } else {
                        if (((double)((v_cs) == ((3.0)))) != 0.0) {
                          g_poke(D_lbD,LEN_lbD,CH_lbD,v_v,v_idx,v_k);
                        }
                      }
                    }
                  }
                  if (((double)((((double)((fmod(v_idx,(64.0))) == ((0.0))))) && (((double)((floor((v_idx / (64.0)))) < (v_pframes)))))) != 0.0) {
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      g_poke(D_pbA,LEN_pbA,CH_pbA,v_pk,floor((v_idx / (64.0))),v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        g_poke(D_pbB,LEN_pbB,CH_pbB,v_pk,floor((v_idx / (64.0))),v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          g_poke(D_pbC,LEN_pbC,CH_pbC,v_pk,floor((v_idx / (64.0))),v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            g_poke(D_pbD,LEN_pbD,CH_pbD,v_pk,floor((v_idx / (64.0))),v_k);
                          }
                        }
                      }
                    }
                  }
                }
                v_w = g_peek(D_we,LEN_we,CH_we,v_k,0);
                if (((double)((v_s) == ((5.0)))) != 0.0) {
                  v_w = fmin((1.0),(v_w + ((1.0) / v_fade)));
                } else {
                  v_w = fmax((0.0),(v_w - ((1.0) / v_fade)));
                }
                g_poke(D_we,LEN_we,CH_we,v_w,v_k,0);
                v_t = g_peek(D_tl,LEN_tl,CH_tl,v_k,0);
                if (((double)((((double)((v_w) > ((0.0))))) || (((double)((v_t) > ((0.0))))))) != 0.0) {
                  if (((double)((v_cs) == ((0.0)))) != 0.0) {
                    v_vv = g_peek(D_lbA,LEN_lbA,CH_lbA,v_idx,v_k);
                  } else {
                    if (((double)((v_cs) == ((1.0)))) != 0.0) {
                      v_vv = g_peek(D_lbB,LEN_lbB,CH_lbB,v_idx,v_k);
                    } else {
                      if (((double)((v_cs) == ((2.0)))) != 0.0) {
                        v_vv = g_peek(D_lbC,LEN_lbC,CH_lbC,v_idx,v_k);
                      } else {
                        if (((double)((v_cs) == ((3.0)))) != 0.0) {
                          v_vv = g_peek(D_lbD,LEN_lbD,CH_lbD,v_idx,v_k);
                        }
                      }
                    }
                  }
                  if (((double)((((double)((((double)((((double)((((double)((((double)((H_g_ut) == ((2.0))))) && (((double)((H_g_uk) == (v_k))))))) && (((double)((H_g_us) == (v_cs))))))) && (((double)((H_g_ur) == ((0.0)))))))) && (((double)((H_g_ucnt) < (v_lnn))))))) && (((double)((v_w) > ((0.0))))))) != 0.0) {
                    g_poke(D_ub,LEN_ub,CH_ub,v_vv,v_idx,(0.0));
                    H_g_ucnt = fmax(H_g_ucnt,(v_idx + (1.0)));
                  }
                  v_vv = ((v_vv + (v_xk * v_w)) + (v_xk * (v_t / v_fade)));
                  if (((double)((v_cs) == ((0.0)))) != 0.0) {
                    g_poke(D_lbA,LEN_lbA,CH_lbA,v_vv,v_idx,v_k);
                  } else {
                    if (((double)((v_cs) == ((1.0)))) != 0.0) {
                      g_poke(D_lbB,LEN_lbB,CH_lbB,v_vv,v_idx,v_k);
                    } else {
                      if (((double)((v_cs) == ((2.0)))) != 0.0) {
                        g_poke(D_lbC,LEN_lbC,CH_lbC,v_vv,v_idx,v_k);
                      } else {
                        if (((double)((v_cs) == ((3.0)))) != 0.0) {
                          g_poke(D_lbD,LEN_lbD,CH_lbD,v_vv,v_idx,v_k);
                        }
                      }
                    }
                  }
                  if (((double)((((double)((((double)((((double)((v_w) > ((0.0))))) && (((double)((fmod(v_idx,(64.0))) == ((0.0)))))))) && (((double)((v_pk) > ((0.0)))))))) && (((double)((floor((v_idx / (64.0)))) < (v_pframes)))))) != 0.0) {
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      g_poke(D_pbA,LEN_pbA,CH_pbA,v_pk,floor((v_idx / (64.0))),v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        g_poke(D_pbB,LEN_pbB,CH_pbB,v_pk,floor((v_idx / (64.0))),v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          g_poke(D_pbC,LEN_pbC,CH_pbC,v_pk,floor((v_idx / (64.0))),v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            g_poke(D_pbD,LEN_pbD,CH_pbD,v_pk,floor((v_idx / (64.0))),v_k);
                          }
                        }
                      }
                    }
                  }
                  if (((double)((v_t) > ((0.0)))) != 0.0) {
                    g_poke(D_tl,LEN_tl,CH_tl,(v_t - (1.0)),v_k,0);
                  }
                }
                v_gtar = (0.0);
                if (((double)((((double)((((double)((((double)((((double)((v_s) == ((3.0))))) || (((double)((v_s) == ((4.0)))))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((v_s) == ((10.0)))))))) && (((double)((v_k) < (v_ntn)))))) != 0.0) {
                  v_gtar = (1.0);
                }
                v_gg = g_peek(D_pg,LEN_pg,CH_pg,v_k,v_cs);
                if (((double)((v_gg) < (v_gtar))) != 0.0) {
                  v_gg = fmin(v_gtar,(v_gg + ((1.0) / v_xfs)));
                } else {
                  v_gg = fmax(v_gtar,(v_gg - ((1.0) / v_xfs)));
                }
                g_poke(D_pg,LEN_pg,CH_pg,v_gg,v_k,v_cs);
                v_md = g_peek(D_spa,LEN_spa,CH_spa,v_k,0);
                v_sgt = (1.0);
                if (((double)((v_md) != (g_peek(D_spd,LEN_spd,CH_spd,v_k,v_cs)))) != 0.0) {
                  v_sgt = (0.0);
                }
                if (((double)((((double)((((double)((H_g_ur) > ((0.0))))) && (((double)((H_g_uk) == (v_k))))))) && (((double)((H_g_us) == (v_cs)))))) != 0.0) {
                  v_sgt = (0.0);
                }
                v_sg = g_peek(D_sgn,LEN_sgn,CH_sgn,v_k,0);
                if (((double)((v_sg) < (v_sgt))) != 0.0) {
                  v_sg = fmin((1.0),(v_sg + ((1.0) / v_fade)));
                } else {
                  v_sg = fmax((0.0),(v_sg - ((1.0) / v_fade)));
                }
                if (((double)((((double)((v_sg) <= ((0.0))))) && (((double)((v_md) != (g_peek(D_spd,LEN_spd,CH_spd,v_k,v_cs))))))) != 0.0) {
                  v_md = g_peek(D_spd,LEN_spd,CH_spd,v_k,v_cs);
                  g_poke(D_spa,LEN_spa,CH_spa,v_md,v_k,0);
                }
                g_poke(D_sgn,LEN_sgn,CH_sgn,v_sg,v_k,0);
                if (((double)((((double)((v_gg) > ((0.0))))) && (((double)((v_sg) > ((0.0))))))) != 0.0) {
                  v_il = (v_idx + fmin(v_latS,(v_lnn - (1.0))));
                  v_lcx = H_g_lc;
                  if (((double)((v_il) >= (v_lnn))) != 0.0) {
                    v_il = (v_il - v_lnn);
                    v_lcx = (v_lcx + (1.0));
                  }
                  v_ri = v_il;
                  if (((double)((v_md) == ((1.0)))) != 0.0) {
                    v_ri = ((v_il + (v_lnn * fmod(v_lcx,(2.0)))) * (0.5));
                  } else {
                    if (((double)((v_md) == ((2.0)))) != 0.0) {
                      v_ri = (v_il * (2.0));
                      if (((double)((v_ri) >= (v_lnn))) != 0.0) {
                        v_ri = (v_ri - v_lnn);
                      }
                    } else {
                      if (((double)((v_md) == ((3.0)))) != 0.0) {
                        v_ri = fmax((0.0),((v_lnn - (1.0)) - v_il));
                      }
                    }
                  }
                  v_v = (0.0);
                  if (((double)((v_pm) < ((1.0)))) != 0.0) {
                    v_dr_a = (0.0);
                    v_dr_b = (0.0);
                    v_dr_i0 = floor(v_ri);
                    v_dr_fr = (v_ri - v_dr_i0);
                    v_dr_i1 = (v_dr_i0 + (1.0));
                    if (((double)((v_dr_i1) >= (v_lnn))) != 0.0) {
                      v_dr_i1 = (0.0);
                    }
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_dr_a = g_peek(D_lbA,LEN_lbA,CH_lbA,v_dr_i0,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_dr_a = g_peek(D_lbB,LEN_lbB,CH_lbB,v_dr_i0,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_dr_a = g_peek(D_lbC,LEN_lbC,CH_lbC,v_dr_i0,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_dr_a = g_peek(D_lbD,LEN_lbD,CH_lbD,v_dr_i0,v_k);
                          }
                        }
                      }
                    }
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_dr_b = g_peek(D_lbA,LEN_lbA,CH_lbA,v_dr_i1,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_dr_b = g_peek(D_lbB,LEN_lbB,CH_lbB,v_dr_i1,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_dr_b = g_peek(D_lbC,LEN_lbC,CH_lbC,v_dr_i1,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_dr_b = g_peek(D_lbD,LEN_lbD,CH_lbD,v_dr_i1,v_k);
                          }
                        }
                      }
                    }
                    v_v = (v_dr_a + ((v_dr_b - v_dr_a) * v_dr_fr));
                  }
                  if (((double)((v_pm) > ((0.0)))) != 0.0) {
                    v_mr = (1.0);
                    if (((double)((v_md) == ((1.0)))) != 0.0) {
                      v_mr = (0.5);
                    }
                    if (((double)((v_md) == ((2.0)))) != 0.0) {
                      v_mr = (2.0);
                    }
                    if (((double)((v_md) == ((3.0)))) != 0.0) {
                      v_mr = (-(1.0));
                    }
                    v_gp0 = g_peek(D_gph,LEN_gph,CH_gph,v_k,0);
                    v_gp = (v_gp0 + ((1.0) / v_gsz));
                    v_nwa = ((double)((v_gp) >= ((1.0))));
                    v_nwb = ((double)((((double)((v_gp) >= ((0.5))))) && (((double)((v_gp0) < ((0.5)))))));
                    if (((double)((v_nwa) || (v_nwb))) != 0.0) {
                      v_gq0 = g_wrap((v_gp0 + (0.5)),(0.0),(1.0));
                      v_qo = (g_peek(D_gab,LEN_gab,CH_gab,v_k,0) + ((v_mr * v_gq0) * v_gsz));
                      if (v_nwb != 0.0) {
                        v_qo = (g_peek(D_gaa,LEN_gaa,CH_gaa,v_k,0) + ((v_mr * v_gp0) * v_gsz));
                      }
                      v_anc = v_ri;
                      v_wbest = (-(1000000.0));
                      v_wd = floor(((((10.0))*SR/1000.0) / (8.0)));
                      for (v_wi = ((0.0) - v_wd); ((double)((v_wi) <= (v_wd))) != 0.0; v_wi = v_wi + ((1.0))) {
                        v_wc = (v_ri + (v_wi * (8.0)));
                        v_w1c = (0.0);
                        v_w1e = (1e-06);
                        for (v_wj = (0.0); ((double)((v_wj) < ((32.0)))) != 0.0; v_wj = v_wj + ((1.0))) {
                          v_wsa = (0.0);
                          v_wsb = (0.0);
                          v_wpa = (v_wc + ((v_mr * v_wj) * (8.0)));
                          v_wpa = (v_wpa - (v_lnn * floor((v_wpa / v_lnn))));
                          v_wpb = (v_qo + ((v_mr * v_wj) * (8.0)));
                          v_wpb = (v_wpb - (v_lnn * floor((v_wpb / v_lnn))));
                          if (((double)((v_cs) == ((0.0)))) != 0.0) {
                            v_wsa = g_peek(D_lbA,LEN_lbA,CH_lbA,floor(v_wpa),v_k);
                          } else {
                            if (((double)((v_cs) == ((1.0)))) != 0.0) {
                              v_wsa = g_peek(D_lbB,LEN_lbB,CH_lbB,floor(v_wpa),v_k);
                            } else {
                              if (((double)((v_cs) == ((2.0)))) != 0.0) {
                                v_wsa = g_peek(D_lbC,LEN_lbC,CH_lbC,floor(v_wpa),v_k);
                              } else {
                                if (((double)((v_cs) == ((3.0)))) != 0.0) {
                                  v_wsa = g_peek(D_lbD,LEN_lbD,CH_lbD,floor(v_wpa),v_k);
                                }
                              }
                            }
                          }
                          if (((double)((v_cs) == ((0.0)))) != 0.0) {
                            v_wsb = g_peek(D_lbA,LEN_lbA,CH_lbA,floor(v_wpb),v_k);
                          } else {
                            if (((double)((v_cs) == ((1.0)))) != 0.0) {
                              v_wsb = g_peek(D_lbB,LEN_lbB,CH_lbB,floor(v_wpb),v_k);
                            } else {
                              if (((double)((v_cs) == ((2.0)))) != 0.0) {
                                v_wsb = g_peek(D_lbC,LEN_lbC,CH_lbC,floor(v_wpb),v_k);
                              } else {
                                if (((double)((v_cs) == ((3.0)))) != 0.0) {
                                  v_wsb = g_peek(D_lbD,LEN_lbD,CH_lbD,floor(v_wpb),v_k);
                                }
                              }
                            }
                          }
                          v_w1c = (v_w1c + (v_wsa * v_wsb));
                          v_w1e = (v_w1e + (v_wsa * v_wsa));
                        }
                        v_w1c = (v_w1c / sqrt(fmax(0.0,v_w1e)));
                        if (((double)((v_w1c) > (v_wbest))) != 0.0) {
                          v_wbest = v_w1c;
                          v_anc = v_wc;
                        }
                      }
                      v_wc0 = v_anc;
                      v_wbest = (-(1000000.0));
                      for (v_wi = ((0.0) - (8.0)); ((double)((v_wi) <= ((8.0)))) != 0.0; v_wi = v_wi + ((1.0))) {
                        v_wc = (v_wc0 + v_wi);
                        v_w2c = (0.0);
                        v_w2e = (1e-06);
                        for (v_wj = (0.0); ((double)((v_wj) < ((32.0)))) != 0.0; v_wj = v_wj + ((1.0))) {
                          v_wsa = (0.0);
                          v_wsb = (0.0);
                          v_wpa = (v_wc + ((v_mr * v_wj) * (2.0)));
                          v_wpa = (v_wpa - (v_lnn * floor((v_wpa / v_lnn))));
                          v_wpb = (v_qo + ((v_mr * v_wj) * (2.0)));
                          v_wpb = (v_wpb - (v_lnn * floor((v_wpb / v_lnn))));
                          if (((double)((v_cs) == ((0.0)))) != 0.0) {
                            v_wsa = g_peek(D_lbA,LEN_lbA,CH_lbA,floor(v_wpa),v_k);
                          } else {
                            if (((double)((v_cs) == ((1.0)))) != 0.0) {
                              v_wsa = g_peek(D_lbB,LEN_lbB,CH_lbB,floor(v_wpa),v_k);
                            } else {
                              if (((double)((v_cs) == ((2.0)))) != 0.0) {
                                v_wsa = g_peek(D_lbC,LEN_lbC,CH_lbC,floor(v_wpa),v_k);
                              } else {
                                if (((double)((v_cs) == ((3.0)))) != 0.0) {
                                  v_wsa = g_peek(D_lbD,LEN_lbD,CH_lbD,floor(v_wpa),v_k);
                                }
                              }
                            }
                          }
                          if (((double)((v_cs) == ((0.0)))) != 0.0) {
                            v_wsb = g_peek(D_lbA,LEN_lbA,CH_lbA,floor(v_wpb),v_k);
                          } else {
                            if (((double)((v_cs) == ((1.0)))) != 0.0) {
                              v_wsb = g_peek(D_lbB,LEN_lbB,CH_lbB,floor(v_wpb),v_k);
                            } else {
                              if (((double)((v_cs) == ((2.0)))) != 0.0) {
                                v_wsb = g_peek(D_lbC,LEN_lbC,CH_lbC,floor(v_wpb),v_k);
                              } else {
                                if (((double)((v_cs) == ((3.0)))) != 0.0) {
                                  v_wsb = g_peek(D_lbD,LEN_lbD,CH_lbD,floor(v_wpb),v_k);
                                }
                              }
                            }
                          }
                          v_w2c = (v_w2c + (v_wsa * v_wsb));
                          v_w2e = (v_w2e + (v_wsa * v_wsa));
                        }
                        v_w2c = (v_w2c / sqrt(fmax(0.0,v_w2e)));
                        if (((double)((v_w2c) > (v_wbest))) != 0.0) {
                          v_wbest = v_w2c;
                          v_anc = v_wc;
                        }
                      }
                      v_anc = (v_anc - (v_lnn * floor((v_anc / v_lnn))));
                      if (v_nwa != 0.0) {
                        v_gp = (v_gp - (1.0));
                        g_poke(D_gaa,LEN_gaa,CH_gaa,v_anc,v_k,0);
                      } else {
                        g_poke(D_gab,LEN_gab,CH_gab,v_anc,v_k,0);
                      }
                    }
                    g_poke(D_gph,LEN_gph,CH_gph,v_gp,v_k,0);
                    v_gq = g_wrap((v_gp + (0.5)),(0.0),(1.0));
                    v_qa = (g_peek(D_gaa,LEN_gaa,CH_gaa,v_k,0) + ((v_mr * v_gp) * v_gsz));
                    v_qa = (v_qa - (v_lnn * floor((v_qa / v_lnn))));
                    v_qb = (g_peek(D_gab,LEN_gab,CH_gab,v_k,0) + ((v_mr * v_gq) * v_gsz));
                    v_qb = (v_qb - (v_lnn * floor((v_qb / v_lnn))));
                    v_ga_a = (0.0);
                    v_ga_b = (0.0);
                    v_ga_i0 = floor(v_qa);
                    v_ga_fr = (v_qa - v_ga_i0);
                    v_ga_i1 = (v_ga_i0 + (1.0));
                    if (((double)((v_ga_i1) >= (v_lnn))) != 0.0) {
                      v_ga_i1 = (0.0);
                    }
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_ga_a = g_peek(D_lbA,LEN_lbA,CH_lbA,v_ga_i0,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_ga_a = g_peek(D_lbB,LEN_lbB,CH_lbB,v_ga_i0,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_ga_a = g_peek(D_lbC,LEN_lbC,CH_lbC,v_ga_i0,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_ga_a = g_peek(D_lbD,LEN_lbD,CH_lbD,v_ga_i0,v_k);
                          }
                        }
                      }
                    }
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_ga_b = g_peek(D_lbA,LEN_lbA,CH_lbA,v_ga_i1,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_ga_b = g_peek(D_lbB,LEN_lbB,CH_lbB,v_ga_i1,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_ga_b = g_peek(D_lbC,LEN_lbC,CH_lbC,v_ga_i1,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_ga_b = g_peek(D_lbD,LEN_lbD,CH_lbD,v_ga_i1,v_k);
                          }
                        }
                      }
                    }
                    v_ya = (v_ga_a + ((v_ga_b - v_ga_a) * v_ga_fr));
                    v_gb_a = (0.0);
                    v_gb_b = (0.0);
                    v_gb_i0 = floor(v_qb);
                    v_gb_fr = (v_qb - v_gb_i0);
                    v_gb_i1 = (v_gb_i0 + (1.0));
                    if (((double)((v_gb_i1) >= (v_lnn))) != 0.0) {
                      v_gb_i1 = (0.0);
                    }
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_gb_a = g_peek(D_lbA,LEN_lbA,CH_lbA,v_gb_i0,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_gb_a = g_peek(D_lbB,LEN_lbB,CH_lbB,v_gb_i0,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_gb_a = g_peek(D_lbC,LEN_lbC,CH_lbC,v_gb_i0,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_gb_a = g_peek(D_lbD,LEN_lbD,CH_lbD,v_gb_i0,v_k);
                          }
                        }
                      }
                    }
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_gb_b = g_peek(D_lbA,LEN_lbA,CH_lbA,v_gb_i1,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_gb_b = g_peek(D_lbB,LEN_lbB,CH_lbB,v_gb_i1,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_gb_b = g_peek(D_lbC,LEN_lbC,CH_lbC,v_gb_i1,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_gb_b = g_peek(D_lbD,LEN_lbD,CH_lbD,v_gb_i1,v_k);
                          }
                        }
                      }
                    }
                    v_yb = (v_gb_a + ((v_gb_b - v_gb_a) * v_gb_fr));
                    v_gv = ((v_ya * ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_gp))))) + (v_yb * ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_gq))))));
                    v_v = (v_v + ((v_gv - v_v) * v_pm));
                  }
                  g_poke(D_ov,LEN_ov,CH_ov,((v_v * v_gg) * v_sg),v_k,0);
                  v_pix = floor((v_ri / (64.0)));
                  if (((double)((((double)((v_gg) > ((0.5))))) && (((double)((v_pix) < (v_pframes)))))) != 0.0) {
                    if (((double)((v_cs) == ((0.0)))) != 0.0) {
                      v_vv = g_peek(D_pbA,LEN_pbA,CH_pbA,v_pix,v_k);
                    } else {
                      if (((double)((v_cs) == ((1.0)))) != 0.0) {
                        v_vv = g_peek(D_pbB,LEN_pbB,CH_pbB,v_pix,v_k);
                      } else {
                        if (((double)((v_cs) == ((2.0)))) != 0.0) {
                          v_vv = g_peek(D_pbC,LEN_pbC,CH_pbC,v_pix,v_k);
                        } else {
                          if (((double)((v_cs) == ((3.0)))) != 0.0) {
                            v_vv = g_peek(D_pbD,LEN_pbD,CH_pbD,v_pix,v_k);
                          }
                        }
                      }
                    }
                    if (((double)((((double)((v_vv) > ((0.0))))) && (((double)((v_md) == ((1.0))))))) != 0.0) {
                      v_vv = (v_vv - (12.0));
                    }
                    if (((double)((((double)((v_vv) > ((0.0))))) && (((double)((v_md) == ((2.0))))))) != 0.0) {
                      v_vv = (v_vv + (12.0));
                    }
                    g_poke(D_op,LEN_op,CH_op,v_vv,v_k,0);
                  }
                }
              }
              v_ps = (v_idx + v_rt);
              if (((double)((v_ps) >= (v_lnn))) != 0.0) {
                v_ps = (v_ps - v_lnn);
                if (((double)((v_rt) >= ((1.0)))) != 0.0) {
                  v_ps = floor((v_ps + (0.5)));
                }
                if (((double)((v_ps) >= (v_lnn))) != 0.0) {
                  v_ps = (0.0);
                }
                H_g_lc = (H_g_lc + (1.0));
                H_g_wrap = (1.0);
              }
            }
          } else {
            for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
              g_poke(D_pg,LEN_pg,CH_pg,fmax((0.0),(g_peek(D_pg,LEN_pg,CH_pg,v_k,v_cs) - ((1.0) / v_xfs))),v_k,v_cs);
            }
          }
        }
      }
      if (((double)((H_g_xf) > ((0.0)))) != 0.0) {
        v_osn = H_g_os;
        v_oln = g_peek(D_slen,LEN_slen,CH_slen,v_osn,0);
        v_xg = H_g_xf;
        if (((double)((v_oln) > ((0.0)))) != 0.0) {
          for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
            v_gg = g_peek(D_pg,LEN_pg,CH_pg,v_k,v_osn);
            if (((double)((v_gg) > ((0.0)))) != 0.0) {
              v_om = g_peek(D_spd,LEN_spd,CH_spd,v_k,v_osn);
              v_ol2 = (H_g_opos + fmin(v_latS,(v_oln - (1.0))));
              v_olc = H_g_olc;
              if (((double)((v_ol2) >= (v_oln))) != 0.0) {
                v_ol2 = (v_ol2 - v_oln);
                v_olc = (v_olc + (1.0));
              }
              v_oi = v_ol2;
              if (((double)((v_om) == ((1.0)))) != 0.0) {
                v_oi = ((v_ol2 + (v_oln * fmod(v_olc,(2.0)))) * (0.5));
              } else {
                if (((double)((v_om) == ((2.0)))) != 0.0) {
                  v_oi = (v_ol2 * (2.0));
                  if (((double)((v_oi) >= (v_oln))) != 0.0) {
                    v_oi = (v_oi - v_oln);
                  }
                } else {
                  if (((double)((v_om) == ((3.0)))) != 0.0) {
                    v_oi = fmax((0.0),((v_oln - (1.0)) - v_ol2));
                  }
                }
              }
              if (((double)((v_osn) == ((0.0)))) != 0.0) {
                v_v = g_peek(D_lbA,LEN_lbA,CH_lbA,floor(v_oi),v_k);
              } else {
                if (((double)((v_osn) == ((1.0)))) != 0.0) {
                  v_v = g_peek(D_lbB,LEN_lbB,CH_lbB,floor(v_oi),v_k);
                } else {
                  if (((double)((v_osn) == ((2.0)))) != 0.0) {
                    v_v = g_peek(D_lbC,LEN_lbC,CH_lbC,floor(v_oi),v_k);
                  } else {
                    if (((double)((v_osn) == ((3.0)))) != 0.0) {
                      v_v = g_peek(D_lbD,LEN_lbD,CH_lbD,floor(v_oi),v_k);
                    }
                  }
                }
              }
              g_poke(D_ov,LEN_ov,CH_ov,(g_peek(D_ov,LEN_ov,CH_ov,v_k,0) + ((v_v * v_gg) * v_xg)),v_k,0);
            }
          }
          H_g_opos = (H_g_opos + (1.0));
          if (((double)((H_g_opos) >= (v_oln))) != 0.0) {
            H_g_opos = (0.0);
            H_g_olc = (H_g_olc + (1.0));
          }
        }
        v_xg = (v_xg - ((1.0) / v_xfs));
        if (((double)((v_xg) <= ((0.0)))) != 0.0) {
          v_xg = (0.0);
          for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
            g_poke(D_pg,LEN_pg,CH_pg,(0.0),v_k,v_osn);
          }
        }
        H_g_xf = v_xg;
      }
      if (((double)((v_fin) == ((1.0)))) != 0.0) {
        if (((double)((v_finpos) > ((0.0)))) != 0.0) {
          for (v_j = (0.0); ((double)((v_j) < (fmin(v_finpos,v_fade)))) != 0.0; v_j = v_j + ((1.0))) {
            if (((double)((v_cs) == ((0.0)))) != 0.0) {
              v_v = g_peek(D_lbA,LEN_lbA,CH_lbA,v_j,v_fst);
            } else {
              if (((double)((v_cs) == ((1.0)))) != 0.0) {
                v_v = g_peek(D_lbB,LEN_lbB,CH_lbB,v_j,v_fst);
              } else {
                if (((double)((v_cs) == ((2.0)))) != 0.0) {
                  v_v = g_peek(D_lbC,LEN_lbC,CH_lbC,v_j,v_fst);
                } else {
                  if (((double)((v_cs) == ((3.0)))) != 0.0) {
                    v_v = g_peek(D_lbD,LEN_lbD,CH_lbD,v_j,v_fst);
                  }
                }
              }
            }
            if (((double)((v_cs) == ((0.0)))) != 0.0) {
              v_vv = g_peek(D_lbA,LEN_lbA,CH_lbA,(v_finlen + v_j),v_fst);
            } else {
              if (((double)((v_cs) == ((1.0)))) != 0.0) {
                v_vv = g_peek(D_lbB,LEN_lbB,CH_lbB,(v_finlen + v_j),v_fst);
              } else {
                if (((double)((v_cs) == ((2.0)))) != 0.0) {
                  v_vv = g_peek(D_lbC,LEN_lbC,CH_lbC,(v_finlen + v_j),v_fst);
                } else {
                  if (((double)((v_cs) == ((3.0)))) != 0.0) {
                    v_vv = g_peek(D_lbD,LEN_lbD,CH_lbD,(v_finlen + v_j),v_fst);
                  }
                }
              }
            }
            v_v = (v_v + (v_vv * ((1.0) - (v_j / v_fade))));
            if (((double)((v_cs) == ((0.0)))) != 0.0) {
              g_poke(D_lbA,LEN_lbA,CH_lbA,v_v,v_j,v_fst);
            } else {
              if (((double)((v_cs) == ((1.0)))) != 0.0) {
                g_poke(D_lbB,LEN_lbB,CH_lbB,v_v,v_j,v_fst);
              } else {
                if (((double)((v_cs) == ((2.0)))) != 0.0) {
                  g_poke(D_lbC,LEN_lbC,CH_lbC,v_v,v_j,v_fst);
                } else {
                  if (((double)((v_cs) == ((3.0)))) != 0.0) {
                    g_poke(D_lbD,LEN_lbD,CH_lbD,v_v,v_j,v_fst);
                  }
                }
              }
            }
          }
        }
        g_poke(D_tl,LEN_tl,CH_tl,fmax((0.0),(v_fade - v_finpos)),v_fst,0);
        g_poke(D_st,LEN_st,CH_st,(3.0),v_fst,v_cs);
        g_poke(D_slen,LEN_slen,CH_slen,v_finlen,v_cs,0);
        g_poke(D_spd,LEN_spd,CH_spd,(0.0),v_fst,v_cs);
        g_poke(D_spa,LEN_spa,CH_spa,(0.0),v_fst,0);
        H_g_ut = (1.0);
        H_g_uk = v_fst;
        H_g_us = v_cs;
        v_lnn = v_finlen;
        v_ps = v_finpos;
        v_fst = (-(1.0));
        v_rp = (0.0);
        v_stp = (0.0);
        v_tgt = (0.0);
        v_run = (1.0);
        H_g_hold = (1.0);
        H_g_lbar = floor((v_finpos / v_brl));
      }
      v_need = ((double)((((double)((((double)((v_cin) > ((0.0))))) || (((double)((v_fst) >= ((0.0)))))))) || (((double)((((double)((((double)((P_clickall) > ((0.0))))) && (((double)((v_lnn) > ((0.0)))))))) && (((double)((v_run) > ((0.0))))))))));
      for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
        v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_cs);
        if (((double)((((double)((v_run) > ((0.0))))) && (((double)((((double)((((double)((((double)((v_s) == ((1.0))))) || (((double)((v_s) == ((2.0)))))))) || (((double)((v_s) == ((4.0)))))))) || (((double)((v_s) == ((5.0)))))))))) != 0.0) {
          v_need = (1.0);
        }
      }
      v_ce = (H_g_cenv * exp(((-(1.0)) / (((28.0))*SR/1000.0))));
      if (((double)((v_bi) != (v_lbeat))) != 0.0) {
        v_lbeat = v_bi;
        if (((double)((v_need) > ((0.0)))) != 0.0) {
          v_ce = (1.0);
          H_g_cfr = (1000.0);
          if (((double)((fmod(v_bi,v_bts)) == ((0.0)))) != 0.0) {
            H_g_cfr = (1600.0);
          }
        }
      }
      v_cph = g_wrap((H_g_cph + (H_g_cfr / SR)),(0.0),(1.0));
      H_g_cph = v_cph;
      H_g_cenv = v_ce;
      v_click = (((sin(((2.0*M_PI) * v_cph)) * v_ce) * (P_clickvol / (100.0))) * (0.5));
      v_pv = (0.0);
      v_pvs = (floor(P_pvw) - (1.0));
      v_pln = (0.0);
      if (((double)((((double)((v_pvs) >= ((0.0))))) && (((double)((v_pvs) <= ((3.0))))))) != 0.0) {
        v_pln = g_peek(D_slen,LEN_slen,CH_slen,v_pvs,0);
      }
      v_vgt = ((double)((((double)((v_pln) > ((0.0))))) && (((double)((v_pvs) != (v_cs))))));
      v_vg = H_g_vg;
      if (((double)((v_vg) < (v_vgt))) != 0.0) {
        v_vg = fmin((1.0),(v_vg + ((1.0) / (((20.0))*SR/1000.0))));
      } else {
        v_vg = fmax((0.0),(v_vg - ((1.0) / (((20.0))*SR/1000.0))));
      }
      H_g_vg = v_vg;
      if (((double)((((double)((v_vg) > ((0.0))))) && (((double)((v_pln) > ((0.0))))))) != 0.0) {
        v_vp = H_g_vpos;
        if (((double)((((double)((((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) > ((0.0)))))))) && (((double)((v_fst) < ((0.0)))))))) && (((double)((v_cin) == ((0.0))))))) != 0.0) {
          v_vp = v_ps;
        }
        v_vp = (v_vp - (v_pln * floor((v_vp / v_pln))));
        v_vpl = (v_vp + fmin(v_latS,(v_pln - (1.0))));
        if (((double)((v_vpl) >= (v_pln))) != 0.0) {
          v_vpl = (v_vpl - v_pln);
        }
        v_pnt = g_peek(D_ntrs,LEN_ntrs,CH_ntrs,v_pvs,0);
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_pvs);
          if (((double)((((double)((((double)((((double)((((double)((((double)((v_s) == ((3.0))))) || (((double)((v_s) == ((4.0)))))))) || (((double)((v_s) == ((5.0)))))))) || (((double)((v_s) == ((9.0)))))))) || (((double)((v_s) == ((10.0)))))))) && (((double)((v_k) < (v_pnt)))))) != 0.0) {
            v_pm2 = g_peek(D_spd,LEN_spd,CH_spd,v_k,v_pvs);
            v_pvi = v_vpl;
            if (((double)((v_pm2) == ((1.0)))) != 0.0) {
              v_pvi = ((v_vpl + (v_pln * fmod((0.0),(2.0)))) * (0.5));
            } else {
              if (((double)((v_pm2) == ((2.0)))) != 0.0) {
                v_pvi = (v_vpl * (2.0));
                if (((double)((v_pvi) >= (v_pln))) != 0.0) {
                  v_pvi = (v_pvi - v_pln);
                }
              } else {
                if (((double)((v_pm2) == ((3.0)))) != 0.0) {
                  v_pvi = fmax((0.0),((v_pln - (1.0)) - v_vpl));
                }
              }
            }
            v_v = (0.0);
            if (((double)((v_pvs) == ((0.0)))) != 0.0) {
              v_v = g_peek(D_lbA,LEN_lbA,CH_lbA,floor(v_pvi),v_k);
            } else {
              if (((double)((v_pvs) == ((1.0)))) != 0.0) {
                v_v = g_peek(D_lbB,LEN_lbB,CH_lbB,floor(v_pvi),v_k);
              } else {
                if (((double)((v_pvs) == ((2.0)))) != 0.0) {
                  v_v = g_peek(D_lbC,LEN_lbC,CH_lbC,floor(v_pvi),v_k);
                } else {
                  if (((double)((v_pvs) == ((3.0)))) != 0.0) {
                    v_v = g_peek(D_lbD,LEN_lbD,CH_lbD,floor(v_pvi),v_k);
                  }
                }
              }
            }
            v_pv = (v_pv + v_v);
          }
        }
        v_pv = ((v_pv * v_vg) * (0.5));
        H_g_vpos = (v_vp + (1.0));
      }
      v_pa = (0.0);
      v_pb = (0.0);
      v_mlt = (1.0);
      for (v_k = (0.0); ((double)((v_k) < ((4.0)))) != 0.0; v_k = v_k + ((1.0))) {
        v_pa = (v_pa + (g_peek(D_st,LEN_st,CH_st,v_k,v_cs) * v_mlt));
        v_pb = (v_pb + (g_peek(D_st,LEN_st,CH_st,(v_k + (4.0)),v_cs) * v_mlt));
        v_mlt = (v_mlt * (16.0));
      }
      v_nbars = (0.0);
      if (((double)((v_lnn) > ((0.0)))) != 0.0) {
        v_nbars = floor(((v_lnn / v_brl) + (0.5)));
      }
      if (((double)((v_fst) >= ((0.0)))) != 0.0) {
        v_nbars = (floor((v_rp / v_brl)) + (1.0));
      }
      v_barno = (0.0);
      v_beatno = (0.0);
      if (((double)((((double)((((double)((v_cin) > ((0.0))))) || (((double)((v_fst) >= ((0.0)))))))) || (((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) > ((0.0)))))))))) != 0.0) {
        v_barno = (floor((v_bi / v_bts)) + (1.0));
        v_beatno = (fmod(v_bi,v_bts) + (1.0));
      }
      if (((double)((v_cin) > ((0.0)))) != 0.0) {
        v_barno = (0.0);
      }
      v_tr = (0.0);
      if (((double)((((double)((((double)((v_run) > ((0.0))))) && (((double)((v_lnn) > ((0.0)))))))) || (((double)((v_fst) >= ((0.0))))))) != 0.0) {
        v_tr = (1.0);
      }
      if (((double)((v_cin) > ((0.0)))) != 0.0) {
        v_tr = (2.0);
      }
      v_smask = (0.0);
      for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
        if (((double)((g_peek(D_slen,LEN_slen,CH_slen,v_i,0)) > ((0.0)))) != 0.0) {
          v_smask = (v_smask + pow((2.0),v_i));
        }
      }
      v_spk = (0.0);
      v_mlt = (1.0);
      for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
        v_spk = (v_spk + (g_peek(D_spd,LEN_spd,CH_spd,v_k,v_cs) * v_mlt));
        v_mlt = (v_mlt * (4.0));
      }
      v_sv0 = (0.0);
      v_sv1 = (0.0);
      v_sv2 = (0.0);
      v_sv3 = (0.0);
      for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
        v_sk = (0.0);
        v_mlt = (1.0);
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          v_sk = (v_sk + (g_peek(D_spd,LEN_spd,CH_spd,v_k,v_i) * v_mlt));
          v_mlt = (v_mlt * (4.0));
        }
        if (((double)((v_i) == ((0.0)))) != 0.0) {
          v_sv0 = v_sk;
        }
        if (((double)((v_i) == ((1.0)))) != 0.0) {
          v_sv1 = v_sk;
        }
        if (((double)((v_i) == ((2.0)))) != 0.0) {
          v_sv2 = v_sk;
        }
        if (((double)((v_i) == ((3.0)))) != 0.0) {
          v_sv3 = v_sk;
        }
      }
      v_tmk0 = (0.0);
      v_tmk1 = (0.0);
      v_tmk2 = (0.0);
      v_tmk3 = (0.0);
      for (v_i = (0.0); ((double)((v_i) < ((4.0)))) != 0.0; v_i = v_i + ((1.0))) {
        v_mk = (0.0);
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          v_s = g_peek(D_st,LEN_st,CH_st,v_k,v_i);
          if (((double)((((double)((((double)((((double)((v_s) != ((0.0))))) && (((double)((v_s) != ((1.0)))))))) && (((double)((v_s) != ((6.0)))))))) && (((double)((v_s) != ((7.0))))))) != 0.0) {
            v_mk = (v_mk + pow((2.0),v_k));
          }
        }
        if (((double)((v_i) == ((0.0)))) != 0.0) {
          v_tmk0 = v_mk;
        }
        if (((double)((v_i) == ((1.0)))) != 0.0) {
          v_tmk1 = v_mk;
        }
        if (((double)((v_i) == ((2.0)))) != 0.0) {
          v_tmk2 = v_mk;
        }
        if (((double)((v_i) == ((3.0)))) != 0.0) {
          v_tmk3 = v_mk;
        }
      }
      H_g_cs = v_cs;
      H_g_ps = v_pscn;
      H_g_pos = v_ps;
      H_g_run = v_run;
      H_g_cin = v_cin;
      H_g_cpos = v_cpos;
      H_g_ctrk = v_ctrk;
      H_g_first = v_fst;
      H_g_rp = v_rp;
      H_g_stop = v_stp;
      H_g_tgt = v_tgt;
      H_g_lbeat = v_lbeat;
      H_g_bl = v_bl;
      H_g_bts = v_bts;
      OUT1 = g_peek(D_ov,LEN_ov,CH_ov,(0.0),0);
      OUT2 = g_peek(D_ov,LEN_ov,CH_ov,(1.0),0);
      OUT3 = g_peek(D_ov,LEN_ov,CH_ov,(2.0),0);
      OUT4 = g_peek(D_ov,LEN_ov,CH_ov,(3.0),0);
      OUT5 = g_peek(D_ov,LEN_ov,CH_ov,(4.0),0);
      OUT6 = g_peek(D_ov,LEN_ov,CH_ov,(5.0),0);
      OUT7 = g_peek(D_ov,LEN_ov,CH_ov,(6.0),0);
      OUT8 = g_peek(D_ov,LEN_ov,CH_ov,(7.0),0);
      OUT9 = g_peek(D_op,LEN_op,CH_op,(0.0),0);
      OUT10 = g_peek(D_op,LEN_op,CH_op,(1.0),0);
      OUT11 = g_peek(D_op,LEN_op,CH_op,(2.0),0);
      OUT12 = g_peek(D_op,LEN_op,CH_op,(3.0),0);
      OUT13 = g_peek(D_op,LEN_op,CH_op,(4.0),0);
      OUT14 = g_peek(D_op,LEN_op,CH_op,(5.0),0);
      OUT15 = g_peek(D_op,LEN_op,CH_op,(6.0),0);
      OUT16 = g_peek(D_op,LEN_op,CH_op,(7.0),0);
      OUT17 = v_click;
      OUT18 = v_pa;
      OUT19 = v_pb;
      OUT20 = (v_bpos / v_bl);
      OUT21 = v_barno;
      OUT22 = v_beatno;
      OUT23 = v_nbars;
      OUT24 = ((v_cs + (1.0)) + ((8.0) * (v_pscn + (1.0))));
      OUT25 = v_tr;
      OUT26 = v_smask;
      OUT27 = ((SR * (60.0)) / v_bl);
      OUT28 = v_bl;
      OUT29 = v_bts;
      OUT30 = g_peek(D_slen,LEN_slen,CH_slen,(0.0),0);
      OUT31 = g_peek(D_slen,LEN_slen,CH_slen,(1.0),0);
      OUT32 = g_peek(D_slen,LEN_slen,CH_slen,(2.0),0);
      OUT33 = g_peek(D_slen,LEN_slen,CH_slen,(3.0),0);
      OUT34 = v_tmk0;
      OUT35 = v_tmk1;
      OUT36 = v_tmk2;
      OUT37 = v_tmk3;
      OUT38 = v_spk;
      OUT39 = ((double)((H_g_ut) > ((0.0))));
      OUT40 = v_sv0;
      OUT41 = v_sv1;
      OUT42 = v_sv2;
      OUT43 = v_sv3;
      OUT44 = v_pv;
      OUT45 = v_ntn;
      v_lph = g_wrap((H_g_free / fmax((1.0),v_brl)),(0.0),(1.0));
      if (((double)((((double)((v_lnn) > ((0.0))))) && (((double)((v_run) > ((0.0))))))) != 0.0) {
        v_lph = (v_ps / v_lnn);
      }
      if (((double)((v_fst) >= ((0.0)))) != 0.0) {
        v_lph = g_wrap((v_rp / fmax((1.0),v_brl)),(0.0),(1.0));
      }
      OUT47 = v_lph;
      OUT48 = v_cs;
      OUT49 = H_g_imp;
      OUT46 = (((g_peek(D_ntrs,LEN_ntrs,CH_ntrs,(0.0),0) + ((16.0) * g_peek(D_ntrs,LEN_ntrs,CH_ntrs,(1.0),0))) + ((256.0) * g_peek(D_ntrs,LEN_ntrs,CH_ntrs,(2.0),0))) + ((4096.0) * g_peek(D_ntrs,LEN_ntrs,CH_ntrs,(3.0),0)));
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
      if (out[5]) out[5][t] = (float) OUT6;
      if (out[6]) out[6][t] = (float) OUT7;
      if (out[7]) out[7][t] = (float) OUT8;
      if (out[8]) out[8][t] = (float) OUT9;
      if (out[9]) out[9][t] = (float) OUT10;
      if (out[10]) out[10][t] = (float) OUT11;
      if (out[11]) out[11][t] = (float) OUT12;
      if (out[12]) out[12][t] = (float) OUT13;
      if (out[13]) out[13][t] = (float) OUT14;
      if (out[14]) out[14][t] = (float) OUT15;
      if (out[15]) out[15][t] = (float) OUT16;
      if (out[16]) out[16][t] = (float) OUT17;
      if (out[17]) out[17][t] = (float) OUT18;
      if (out[18]) out[18][t] = (float) OUT19;
      if (out[19]) out[19][t] = (float) OUT20;
      if (out[20]) out[20][t] = (float) OUT21;
      if (out[21]) out[21][t] = (float) OUT22;
      if (out[22]) out[22][t] = (float) OUT23;
      if (out[23]) out[23][t] = (float) OUT24;
      if (out[24]) out[24][t] = (float) OUT25;
      if (out[25]) out[25][t] = (float) OUT26;
      if (out[26]) out[26][t] = (float) OUT27;
      if (out[27]) out[27][t] = (float) OUT28;
      if (out[28]) out[28][t] = (float) OUT29;
      if (out[29]) out[29][t] = (float) OUT30;
      if (out[30]) out[30][t] = (float) OUT31;
      if (out[31]) out[31][t] = (float) OUT32;
      if (out[32]) out[32][t] = (float) OUT33;
      if (out[33]) out[33][t] = (float) OUT34;
      if (out[34]) out[34][t] = (float) OUT35;
      if (out[35]) out[35][t] = (float) OUT36;
      if (out[36]) out[36][t] = (float) OUT37;
      if (out[37]) out[37][t] = (float) OUT38;
      if (out[38]) out[38][t] = (float) OUT39;
      if (out[39]) out[39][t] = (float) OUT40;
      if (out[40]) out[40][t] = (float) OUT41;
      if (out[41]) out[41][t] = (float) OUT42;
      if (out[42]) out[42][t] = (float) OUT43;
      if (out[43]) out[43][t] = (float) OUT44;
      if (out[44]) out[44][t] = (float) OUT45;
      if (out[45]) out[45][t] = (float) OUT46;
      if (out[46]) out[46][t] = (float) OUT47;
      if (out[47]) out[47][t] = (float) OUT48;
      if (out[48]) out[48][t] = (float) OUT49;
    }
  }
};
// ---------------------------------------------------------------- Channels
struct Channels {
  double ambGate[12] = {};   // ARCO 1.3: set by the engine (strips placed by AMBISONICS)
  double SR = 44100.0;
  double P_vol0 = 71.0;
  double P_pan0 = 0.0;
  double P_mute0 = 0.0;
  double P_cue0 = 0.0;
  double P_room0 = 1.0;
  double P_eq0 = 0.0;
  double P_drv0 = 0.0;
  double P_trm0 = 0.0;
  double P_phs0 = 0.0;
  double P_swl0 = 0.0;
  double P_cho0 = 0.0;
  double P_dly0 = 0.0;
  double P_rev0 = 0.0;
  double P_fxb0 = 0.0;
  double P_shd0 = 0.0;
  double P_orb0 = 0.0;
  double P_res0 = 0.0;
  double P_grn0 = 0.0;
  double P_frz0 = 0.0;
  double P_spc0 = 0.0;
  double P_solo0 = 0.0;
  double P_vol1 = 71.0;
  double P_pan1 = 0.0;
  double P_mute1 = 0.0;
  double P_cue1 = 0.0;
  double P_room1 = 1.0;
  double P_eq1 = 0.0;
  double P_drv1 = 0.0;
  double P_trm1 = 0.0;
  double P_phs1 = 0.0;
  double P_swl1 = 0.0;
  double P_cho1 = 0.0;
  double P_dly1 = 0.0;
  double P_rev1 = 0.0;
  double P_fxb1 = 0.0;
  double P_shd1 = 0.0;
  double P_orb1 = 0.0;
  double P_res1 = 0.0;
  double P_grn1 = 0.0;
  double P_frz1 = 0.0;
  double P_spc1 = 0.0;
  double P_solo1 = 0.0;
  double P_vol2 = 71.0;
  double P_pan2 = 0.0;
  double P_mute2 = 0.0;
  double P_cue2 = 0.0;
  double P_room2 = 1.0;
  double P_eq2 = 0.0;
  double P_drv2 = 0.0;
  double P_trm2 = 0.0;
  double P_phs2 = 0.0;
  double P_swl2 = 0.0;
  double P_cho2 = 0.0;
  double P_dly2 = 0.0;
  double P_rev2 = 0.0;
  double P_fxb2 = 0.0;
  double P_shd2 = 0.0;
  double P_orb2 = 0.0;
  double P_res2 = 0.0;
  double P_grn2 = 0.0;
  double P_frz2 = 0.0;
  double P_spc2 = 0.0;
  double P_solo2 = 0.0;
  double P_vol3 = 71.0;
  double P_pan3 = 0.0;
  double P_mute3 = 0.0;
  double P_cue3 = 0.0;
  double P_room3 = 1.0;
  double P_eq3 = 0.0;
  double P_drv3 = 0.0;
  double P_trm3 = 0.0;
  double P_phs3 = 0.0;
  double P_swl3 = 0.0;
  double P_cho3 = 0.0;
  double P_dly3 = 0.0;
  double P_rev3 = 0.0;
  double P_fxb3 = 0.0;
  double P_shd3 = 0.0;
  double P_orb3 = 0.0;
  double P_res3 = 0.0;
  double P_grn3 = 0.0;
  double P_frz3 = 0.0;
  double P_spc3 = 0.0;
  double P_solo3 = 0.0;
  double P_vol4 = 71.0;
  double P_pan4 = 0.0;
  double P_mute4 = 0.0;
  double P_cue4 = 0.0;
  double P_room4 = 1.0;
  double P_eq4 = 0.0;
  double P_drv4 = 0.0;
  double P_trm4 = 0.0;
  double P_phs4 = 0.0;
  double P_swl4 = 0.0;
  double P_cho4 = 0.0;
  double P_dly4 = 0.0;
  double P_rev4 = 0.0;
  double P_fxb4 = 0.0;
  double P_shd4 = 0.0;
  double P_orb4 = 0.0;
  double P_res4 = 0.0;
  double P_grn4 = 0.0;
  double P_frz4 = 0.0;
  double P_spc4 = 0.0;
  double P_solo4 = 0.0;
  double P_vol5 = 71.0;
  double P_pan5 = 0.0;
  double P_mute5 = 0.0;
  double P_cue5 = 0.0;
  double P_room5 = 1.0;
  double P_eq5 = 0.0;
  double P_drv5 = 0.0;
  double P_trm5 = 0.0;
  double P_phs5 = 0.0;
  double P_swl5 = 0.0;
  double P_cho5 = 0.0;
  double P_dly5 = 0.0;
  double P_rev5 = 0.0;
  double P_fxb5 = 0.0;
  double P_shd5 = 0.0;
  double P_orb5 = 0.0;
  double P_res5 = 0.0;
  double P_grn5 = 0.0;
  double P_frz5 = 0.0;
  double P_spc5 = 0.0;
  double P_solo5 = 0.0;
  double P_vol6 = 71.0;
  double P_pan6 = 0.0;
  double P_mute6 = 0.0;
  double P_cue6 = 0.0;
  double P_room6 = 1.0;
  double P_eq6 = 0.0;
  double P_drv6 = 0.0;
  double P_trm6 = 0.0;
  double P_phs6 = 0.0;
  double P_swl6 = 0.0;
  double P_cho6 = 0.0;
  double P_dly6 = 0.0;
  double P_rev6 = 0.0;
  double P_fxb6 = 0.0;
  double P_shd6 = 0.0;
  double P_orb6 = 0.0;
  double P_res6 = 0.0;
  double P_grn6 = 0.0;
  double P_frz6 = 0.0;
  double P_spc6 = 0.0;
  double P_solo6 = 0.0;
  double P_vol7 = 71.0;
  double P_pan7 = 0.0;
  double P_mute7 = 0.0;
  double P_cue7 = 0.0;
  double P_room7 = 1.0;
  double P_eq7 = 0.0;
  double P_drv7 = 0.0;
  double P_trm7 = 0.0;
  double P_phs7 = 0.0;
  double P_swl7 = 0.0;
  double P_cho7 = 0.0;
  double P_dly7 = 0.0;
  double P_rev7 = 0.0;
  double P_fxb7 = 0.0;
  double P_shd7 = 0.0;
  double P_orb7 = 0.0;
  double P_res7 = 0.0;
  double P_grn7 = 0.0;
  double P_frz7 = 0.0;
  double P_spc7 = 0.0;
  double P_solo7 = 0.0;
  double P_vol8 = 71.0;
  double P_pan8 = 0.0;
  double P_mute8 = 0.0;
  double P_cue8 = 0.0;
  double P_room8 = 1.0;
  double P_eq8 = 0.0;
  double P_drv8 = 0.0;
  double P_trm8 = 0.0;
  double P_phs8 = 0.0;
  double P_swl8 = 0.0;
  double P_cho8 = 0.0;
  double P_dly8 = 0.0;
  double P_rev8 = 0.0;
  double P_fxb8 = 0.0;
  double P_shd8 = 0.0;
  double P_orb8 = 0.0;
  double P_res8 = 0.0;
  double P_grn8 = 0.0;
  double P_frz8 = 0.0;
  double P_spc8 = 0.0;
  double P_solo8 = 0.0;
  double P_vol9 = 71.0;
  double P_pan9 = 0.0;
  double P_mute9 = 0.0;
  double P_cue9 = 0.0;
  double P_room9 = 1.0;
  double P_eq9 = 0.0;
  double P_drv9 = 0.0;
  double P_trm9 = 0.0;
  double P_phs9 = 0.0;
  double P_swl9 = 0.0;
  double P_cho9 = 0.0;
  double P_dly9 = 0.0;
  double P_rev9 = 0.0;
  double P_fxb9 = 0.0;
  double P_shd9 = 0.0;
  double P_orb9 = 0.0;
  double P_res9 = 0.0;
  double P_grn9 = 0.0;
  double P_frz9 = 0.0;
  double P_spc9 = 0.0;
  double P_solo9 = 0.0;
  double P_vol10 = 71.0;
  double P_pan10 = 0.0;
  double P_mute10 = 0.0;
  double P_cue10 = 0.0;
  double P_room10 = 1.0;
  double P_eq10 = 0.0;
  double P_drv10 = 0.0;
  double P_trm10 = 0.0;
  double P_phs10 = 0.0;
  double P_swl10 = 0.0;
  double P_cho10 = 0.0;
  double P_dly10 = 0.0;
  double P_rev10 = 0.0;
  double P_fxb10 = 0.0;
  double P_shd10 = 0.0;
  double P_orb10 = 0.0;
  double P_res10 = 0.0;
  double P_grn10 = 0.0;
  double P_frz10 = 0.0;
  double P_spc10 = 0.0;
  double P_solo10 = 0.0;
  double P_vol11 = 71.0;
  double P_pan11 = 0.0;
  double P_mute11 = 0.0;
  double P_cue11 = 0.0;
  double P_room11 = 1.0;
  double P_eq11 = 0.0;
  double P_drv11 = 0.0;
  double P_trm11 = 0.0;
  double P_phs11 = 0.0;
  double P_swl11 = 0.0;
  double P_cho11 = 0.0;
  double P_dly11 = 0.0;
  double P_rev11 = 0.0;
  double P_fxb11 = 0.0;
  double P_shd11 = 0.0;
  double P_orb11 = 0.0;
  double P_res11 = 0.0;
  double P_grn11 = 0.0;
  double P_frz11 = 0.0;
  double P_spc11 = 0.0;
  double P_solo11 = 0.0;
  double P_eq_lo = 50.0;
  double P_eq_mid = 50.0;
  double P_eq_frq = 50.0;
  double P_eq_hi = 50.0;
  double P_drv_amt = 40.0;
  double P_drv_tone = 60.0;
  double P_drv_char = 30.0;
  double P_drv_lvl = 50.0;
  double P_trm_rate = 40.0;
  double P_trm_shape = 20.0;
  double P_trm_st = 0.0;
  double P_phs_rate = 30.0;
  double P_phs_depth = 60.0;
  double P_phs_fb = 40.0;
  double P_phs_color = 50.0;
  double P_swl_att = 40.0;
  double P_swl_sens = 50.0;
  double P_swl_curve = 50.0;
  std::vector<double> V_tg; double* D_tg = nullptr; long LEN_tg = 21; long CH_tg = 12;
  std::vector<double> V_sm; double* D_sm = nullptr; long LEN_sm = 21; long CH_sm = 12;
  std::vector<double> V_eqc; double* D_eqc = nullptr; long LEN_eqc = 15; long CH_eqc = 12;
  std::vector<double> V_eqz; double* D_eqz = nullptr; long LEN_eqz = 6; long CH_eqz = 12;
  std::vector<double> V_dz; double* D_dz = nullptr; long LEN_dz = 3; long CH_dz = 12;
  std::vector<double> V_apz; double* D_apz = nullptr; long LEN_apz = 8; long CH_apz = 12;
  std::vector<double> V_pfb; double* D_pfb = nullptr; long LEN_pfb = 12; long CH_pfb = 1;
  std::vector<double> V_swe; double* D_swe = nullptr; long LEN_swe = 12; long CH_swe = 1;
  std::vector<double> V_swg; double* D_swg = nullptr; long LEN_swg = 12; long CH_swg = 1;
  std::vector<double> V_swq; double* D_swq = nullptr; long LEN_swq = 12; long CH_swq = 1;
  std::vector<double> V_xin; double* D_xin = nullptr; long LEN_xin = 12; long CH_xin = 1;
  std::vector<double> V_xo; double* D_xo = nullptr; long LEN_xo = 12; long CH_xo = 1;
  double H_c_cnt = 0.0;
  double H_c_init = 0.0;
  double H_c_lo = 0.0;
  double H_c_mid = 0.0;
  double H_c_hi = 0.0;
  double H_c_frq = 1000.0;
  double H_c_drv = 0.0;
  double H_c_tone = 0.0;
  double H_c_chr = 0.0;
  double H_c_lvl = 1.0;
  double H_c_pph = 0.0;
  static constexpr int numInputs = 13;
  static constexpr int numOutputs = 28;
  void init(double sr) { SR = sr;
    V_tg.assign((size_t) 21 * 12, 0.0); D_tg = V_tg.data();
    V_sm.assign((size_t) 21 * 12, 0.0); D_sm = V_sm.data();
    V_eqc.assign((size_t) 15 * 12, 0.0); D_eqc = V_eqc.data();
    V_eqz.assign((size_t) 6 * 12, 0.0); D_eqz = V_eqz.data();
    V_dz.assign((size_t) 3 * 12, 0.0); D_dz = V_dz.data();
    V_apz.assign((size_t) 8 * 12, 0.0); D_apz = V_apz.data();
    V_pfb.assign((size_t) 12 * 1, 0.0); D_pfb = V_pfb.data();
    V_swe.assign((size_t) 12 * 1, 0.0); D_swe = V_swe.data();
    V_swg.assign((size_t) 12 * 1, 0.0); D_swg = V_swg.data();
    V_swq.assign((size_t) 12 * 1, 0.0); D_swq = V_swq.data();
    V_xin.assign((size_t) 12 * 1, 0.0); D_xin = V_xin.data();
    V_xo.assign((size_t) 12 * 1, 0.0); D_xo = V_xo.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "vol0") return 0;
    if (n == "pan0") return 1;
    if (n == "mute0") return 2;
    if (n == "cue0") return 3;
    if (n == "room0") return 4;
    if (n == "eq0") return 5;
    if (n == "drv0") return 6;
    if (n == "trm0") return 7;
    if (n == "phs0") return 8;
    if (n == "swl0") return 9;
    if (n == "cho0") return 10;
    if (n == "dly0") return 11;
    if (n == "rev0") return 12;
    if (n == "fxb0") return 13;
    if (n == "shd0") return 14;
    if (n == "orb0") return 15;
    if (n == "res0") return 16;
    if (n == "grn0") return 17;
    if (n == "frz0") return 18;
    if (n == "spc0") return 19;
    if (n == "solo0") return 20;
    if (n == "vol1") return 21;
    if (n == "pan1") return 22;
    if (n == "mute1") return 23;
    if (n == "cue1") return 24;
    if (n == "room1") return 25;
    if (n == "eq1") return 26;
    if (n == "drv1") return 27;
    if (n == "trm1") return 28;
    if (n == "phs1") return 29;
    if (n == "swl1") return 30;
    if (n == "cho1") return 31;
    if (n == "dly1") return 32;
    if (n == "rev1") return 33;
    if (n == "fxb1") return 34;
    if (n == "shd1") return 35;
    if (n == "orb1") return 36;
    if (n == "res1") return 37;
    if (n == "grn1") return 38;
    if (n == "frz1") return 39;
    if (n == "spc1") return 40;
    if (n == "solo1") return 41;
    if (n == "vol2") return 42;
    if (n == "pan2") return 43;
    if (n == "mute2") return 44;
    if (n == "cue2") return 45;
    if (n == "room2") return 46;
    if (n == "eq2") return 47;
    if (n == "drv2") return 48;
    if (n == "trm2") return 49;
    if (n == "phs2") return 50;
    if (n == "swl2") return 51;
    if (n == "cho2") return 52;
    if (n == "dly2") return 53;
    if (n == "rev2") return 54;
    if (n == "fxb2") return 55;
    if (n == "shd2") return 56;
    if (n == "orb2") return 57;
    if (n == "res2") return 58;
    if (n == "grn2") return 59;
    if (n == "frz2") return 60;
    if (n == "spc2") return 61;
    if (n == "solo2") return 62;
    if (n == "vol3") return 63;
    if (n == "pan3") return 64;
    if (n == "mute3") return 65;
    if (n == "cue3") return 66;
    if (n == "room3") return 67;
    if (n == "eq3") return 68;
    if (n == "drv3") return 69;
    if (n == "trm3") return 70;
    if (n == "phs3") return 71;
    if (n == "swl3") return 72;
    if (n == "cho3") return 73;
    if (n == "dly3") return 74;
    if (n == "rev3") return 75;
    if (n == "fxb3") return 76;
    if (n == "shd3") return 77;
    if (n == "orb3") return 78;
    if (n == "res3") return 79;
    if (n == "grn3") return 80;
    if (n == "frz3") return 81;
    if (n == "spc3") return 82;
    if (n == "solo3") return 83;
    if (n == "vol4") return 84;
    if (n == "pan4") return 85;
    if (n == "mute4") return 86;
    if (n == "cue4") return 87;
    if (n == "room4") return 88;
    if (n == "eq4") return 89;
    if (n == "drv4") return 90;
    if (n == "trm4") return 91;
    if (n == "phs4") return 92;
    if (n == "swl4") return 93;
    if (n == "cho4") return 94;
    if (n == "dly4") return 95;
    if (n == "rev4") return 96;
    if (n == "fxb4") return 97;
    if (n == "shd4") return 98;
    if (n == "orb4") return 99;
    if (n == "res4") return 100;
    if (n == "grn4") return 101;
    if (n == "frz4") return 102;
    if (n == "spc4") return 103;
    if (n == "solo4") return 104;
    if (n == "vol5") return 105;
    if (n == "pan5") return 106;
    if (n == "mute5") return 107;
    if (n == "cue5") return 108;
    if (n == "room5") return 109;
    if (n == "eq5") return 110;
    if (n == "drv5") return 111;
    if (n == "trm5") return 112;
    if (n == "phs5") return 113;
    if (n == "swl5") return 114;
    if (n == "cho5") return 115;
    if (n == "dly5") return 116;
    if (n == "rev5") return 117;
    if (n == "fxb5") return 118;
    if (n == "shd5") return 119;
    if (n == "orb5") return 120;
    if (n == "res5") return 121;
    if (n == "grn5") return 122;
    if (n == "frz5") return 123;
    if (n == "spc5") return 124;
    if (n == "solo5") return 125;
    if (n == "vol6") return 126;
    if (n == "pan6") return 127;
    if (n == "mute6") return 128;
    if (n == "cue6") return 129;
    if (n == "room6") return 130;
    if (n == "eq6") return 131;
    if (n == "drv6") return 132;
    if (n == "trm6") return 133;
    if (n == "phs6") return 134;
    if (n == "swl6") return 135;
    if (n == "cho6") return 136;
    if (n == "dly6") return 137;
    if (n == "rev6") return 138;
    if (n == "fxb6") return 139;
    if (n == "shd6") return 140;
    if (n == "orb6") return 141;
    if (n == "res6") return 142;
    if (n == "grn6") return 143;
    if (n == "frz6") return 144;
    if (n == "spc6") return 145;
    if (n == "solo6") return 146;
    if (n == "vol7") return 147;
    if (n == "pan7") return 148;
    if (n == "mute7") return 149;
    if (n == "cue7") return 150;
    if (n == "room7") return 151;
    if (n == "eq7") return 152;
    if (n == "drv7") return 153;
    if (n == "trm7") return 154;
    if (n == "phs7") return 155;
    if (n == "swl7") return 156;
    if (n == "cho7") return 157;
    if (n == "dly7") return 158;
    if (n == "rev7") return 159;
    if (n == "fxb7") return 160;
    if (n == "shd7") return 161;
    if (n == "orb7") return 162;
    if (n == "res7") return 163;
    if (n == "grn7") return 164;
    if (n == "frz7") return 165;
    if (n == "spc7") return 166;
    if (n == "solo7") return 167;
    if (n == "vol8") return 168;
    if (n == "pan8") return 169;
    if (n == "mute8") return 170;
    if (n == "cue8") return 171;
    if (n == "room8") return 172;
    if (n == "eq8") return 173;
    if (n == "drv8") return 174;
    if (n == "trm8") return 175;
    if (n == "phs8") return 176;
    if (n == "swl8") return 177;
    if (n == "cho8") return 178;
    if (n == "dly8") return 179;
    if (n == "rev8") return 180;
    if (n == "fxb8") return 181;
    if (n == "shd8") return 182;
    if (n == "orb8") return 183;
    if (n == "res8") return 184;
    if (n == "grn8") return 185;
    if (n == "frz8") return 186;
    if (n == "spc8") return 187;
    if (n == "solo8") return 188;
    if (n == "vol9") return 189;
    if (n == "pan9") return 190;
    if (n == "mute9") return 191;
    if (n == "cue9") return 192;
    if (n == "room9") return 193;
    if (n == "eq9") return 194;
    if (n == "drv9") return 195;
    if (n == "trm9") return 196;
    if (n == "phs9") return 197;
    if (n == "swl9") return 198;
    if (n == "cho9") return 199;
    if (n == "dly9") return 200;
    if (n == "rev9") return 201;
    if (n == "fxb9") return 202;
    if (n == "shd9") return 203;
    if (n == "orb9") return 204;
    if (n == "res9") return 205;
    if (n == "grn9") return 206;
    if (n == "frz9") return 207;
    if (n == "spc9") return 208;
    if (n == "solo9") return 209;
    if (n == "vol10") return 210;
    if (n == "pan10") return 211;
    if (n == "mute10") return 212;
    if (n == "cue10") return 213;
    if (n == "room10") return 214;
    if (n == "eq10") return 215;
    if (n == "drv10") return 216;
    if (n == "trm10") return 217;
    if (n == "phs10") return 218;
    if (n == "swl10") return 219;
    if (n == "cho10") return 220;
    if (n == "dly10") return 221;
    if (n == "rev10") return 222;
    if (n == "fxb10") return 223;
    if (n == "shd10") return 224;
    if (n == "orb10") return 225;
    if (n == "res10") return 226;
    if (n == "grn10") return 227;
    if (n == "frz10") return 228;
    if (n == "spc10") return 229;
    if (n == "solo10") return 230;
    if (n == "vol11") return 231;
    if (n == "pan11") return 232;
    if (n == "mute11") return 233;
    if (n == "cue11") return 234;
    if (n == "room11") return 235;
    if (n == "eq11") return 236;
    if (n == "drv11") return 237;
    if (n == "trm11") return 238;
    if (n == "phs11") return 239;
    if (n == "swl11") return 240;
    if (n == "cho11") return 241;
    if (n == "dly11") return 242;
    if (n == "rev11") return 243;
    if (n == "fxb11") return 244;
    if (n == "shd11") return 245;
    if (n == "orb11") return 246;
    if (n == "res11") return 247;
    if (n == "grn11") return 248;
    if (n == "frz11") return 249;
    if (n == "spc11") return 250;
    if (n == "solo11") return 251;
    if (n == "eq_lo") return 252;
    if (n == "eq_mid") return 253;
    if (n == "eq_frq") return 254;
    if (n == "eq_hi") return 255;
    if (n == "drv_amt") return 256;
    if (n == "drv_tone") return 257;
    if (n == "drv_char") return 258;
    if (n == "drv_lvl") return 259;
    if (n == "trm_rate") return 260;
    if (n == "trm_shape") return 261;
    if (n == "trm_st") return 262;
    if (n == "phs_rate") return 263;
    if (n == "phs_depth") return 264;
    if (n == "phs_fb") return 265;
    if (n == "phs_color") return 266;
    if (n == "swl_att") return 267;
    if (n == "swl_sens") return 268;
    if (n == "swl_curve") return 269;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol0 = v; break;
      case 1: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan0 = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute0 = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue0 = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room0 = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq0 = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv0 = v; break;
      case 7: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm0 = v; break;
      case 8: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs0 = v; break;
      case 9: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl0 = v; break;
      case 10: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho0 = v; break;
      case 11: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly0 = v; break;
      case 12: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev0 = v; break;
      case 13: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb0 = v; break;
      case 14: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd0 = v; break;
      case 15: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb0 = v; break;
      case 16: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res0 = v; break;
      case 17: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn0 = v; break;
      case 18: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz0 = v; break;
      case 19: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc0 = v; break;
      case 20: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo0 = v; break;
      case 21: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol1 = v; break;
      case 22: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan1 = v; break;
      case 23: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute1 = v; break;
      case 24: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue1 = v; break;
      case 25: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room1 = v; break;
      case 26: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq1 = v; break;
      case 27: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv1 = v; break;
      case 28: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm1 = v; break;
      case 29: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs1 = v; break;
      case 30: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl1 = v; break;
      case 31: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho1 = v; break;
      case 32: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly1 = v; break;
      case 33: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev1 = v; break;
      case 34: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb1 = v; break;
      case 35: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd1 = v; break;
      case 36: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb1 = v; break;
      case 37: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res1 = v; break;
      case 38: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn1 = v; break;
      case 39: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz1 = v; break;
      case 40: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc1 = v; break;
      case 41: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo1 = v; break;
      case 42: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol2 = v; break;
      case 43: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan2 = v; break;
      case 44: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute2 = v; break;
      case 45: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue2 = v; break;
      case 46: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room2 = v; break;
      case 47: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq2 = v; break;
      case 48: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv2 = v; break;
      case 49: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm2 = v; break;
      case 50: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs2 = v; break;
      case 51: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl2 = v; break;
      case 52: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho2 = v; break;
      case 53: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly2 = v; break;
      case 54: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev2 = v; break;
      case 55: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb2 = v; break;
      case 56: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd2 = v; break;
      case 57: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb2 = v; break;
      case 58: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res2 = v; break;
      case 59: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn2 = v; break;
      case 60: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz2 = v; break;
      case 61: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc2 = v; break;
      case 62: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo2 = v; break;
      case 63: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol3 = v; break;
      case 64: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan3 = v; break;
      case 65: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute3 = v; break;
      case 66: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue3 = v; break;
      case 67: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room3 = v; break;
      case 68: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq3 = v; break;
      case 69: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv3 = v; break;
      case 70: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm3 = v; break;
      case 71: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs3 = v; break;
      case 72: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl3 = v; break;
      case 73: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho3 = v; break;
      case 74: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly3 = v; break;
      case 75: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev3 = v; break;
      case 76: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb3 = v; break;
      case 77: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd3 = v; break;
      case 78: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb3 = v; break;
      case 79: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res3 = v; break;
      case 80: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn3 = v; break;
      case 81: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz3 = v; break;
      case 82: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc3 = v; break;
      case 83: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo3 = v; break;
      case 84: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol4 = v; break;
      case 85: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan4 = v; break;
      case 86: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute4 = v; break;
      case 87: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue4 = v; break;
      case 88: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room4 = v; break;
      case 89: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq4 = v; break;
      case 90: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv4 = v; break;
      case 91: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm4 = v; break;
      case 92: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs4 = v; break;
      case 93: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl4 = v; break;
      case 94: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho4 = v; break;
      case 95: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly4 = v; break;
      case 96: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev4 = v; break;
      case 97: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb4 = v; break;
      case 98: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd4 = v; break;
      case 99: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb4 = v; break;
      case 100: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res4 = v; break;
      case 101: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn4 = v; break;
      case 102: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz4 = v; break;
      case 103: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc4 = v; break;
      case 104: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo4 = v; break;
      case 105: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol5 = v; break;
      case 106: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan5 = v; break;
      case 107: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute5 = v; break;
      case 108: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue5 = v; break;
      case 109: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room5 = v; break;
      case 110: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq5 = v; break;
      case 111: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv5 = v; break;
      case 112: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm5 = v; break;
      case 113: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs5 = v; break;
      case 114: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl5 = v; break;
      case 115: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho5 = v; break;
      case 116: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly5 = v; break;
      case 117: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev5 = v; break;
      case 118: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb5 = v; break;
      case 119: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd5 = v; break;
      case 120: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb5 = v; break;
      case 121: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res5 = v; break;
      case 122: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn5 = v; break;
      case 123: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz5 = v; break;
      case 124: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc5 = v; break;
      case 125: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo5 = v; break;
      case 126: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol6 = v; break;
      case 127: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan6 = v; break;
      case 128: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute6 = v; break;
      case 129: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue6 = v; break;
      case 130: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room6 = v; break;
      case 131: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq6 = v; break;
      case 132: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv6 = v; break;
      case 133: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm6 = v; break;
      case 134: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs6 = v; break;
      case 135: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl6 = v; break;
      case 136: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho6 = v; break;
      case 137: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly6 = v; break;
      case 138: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev6 = v; break;
      case 139: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb6 = v; break;
      case 140: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd6 = v; break;
      case 141: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb6 = v; break;
      case 142: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res6 = v; break;
      case 143: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn6 = v; break;
      case 144: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz6 = v; break;
      case 145: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc6 = v; break;
      case 146: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo6 = v; break;
      case 147: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol7 = v; break;
      case 148: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan7 = v; break;
      case 149: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute7 = v; break;
      case 150: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue7 = v; break;
      case 151: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room7 = v; break;
      case 152: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq7 = v; break;
      case 153: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv7 = v; break;
      case 154: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm7 = v; break;
      case 155: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs7 = v; break;
      case 156: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl7 = v; break;
      case 157: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho7 = v; break;
      case 158: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly7 = v; break;
      case 159: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev7 = v; break;
      case 160: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb7 = v; break;
      case 161: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd7 = v; break;
      case 162: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb7 = v; break;
      case 163: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res7 = v; break;
      case 164: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn7 = v; break;
      case 165: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz7 = v; break;
      case 166: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc7 = v; break;
      case 167: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo7 = v; break;
      case 168: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol8 = v; break;
      case 169: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan8 = v; break;
      case 170: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute8 = v; break;
      case 171: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue8 = v; break;
      case 172: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room8 = v; break;
      case 173: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq8 = v; break;
      case 174: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv8 = v; break;
      case 175: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm8 = v; break;
      case 176: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs8 = v; break;
      case 177: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl8 = v; break;
      case 178: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho8 = v; break;
      case 179: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly8 = v; break;
      case 180: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev8 = v; break;
      case 181: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb8 = v; break;
      case 182: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd8 = v; break;
      case 183: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb8 = v; break;
      case 184: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res8 = v; break;
      case 185: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn8 = v; break;
      case 186: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz8 = v; break;
      case 187: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc8 = v; break;
      case 188: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo8 = v; break;
      case 189: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol9 = v; break;
      case 190: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan9 = v; break;
      case 191: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute9 = v; break;
      case 192: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue9 = v; break;
      case 193: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room9 = v; break;
      case 194: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq9 = v; break;
      case 195: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv9 = v; break;
      case 196: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm9 = v; break;
      case 197: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs9 = v; break;
      case 198: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl9 = v; break;
      case 199: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho9 = v; break;
      case 200: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly9 = v; break;
      case 201: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev9 = v; break;
      case 202: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb9 = v; break;
      case 203: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd9 = v; break;
      case 204: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb9 = v; break;
      case 205: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res9 = v; break;
      case 206: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn9 = v; break;
      case 207: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz9 = v; break;
      case 208: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc9 = v; break;
      case 209: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo9 = v; break;
      case 210: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol10 = v; break;
      case 211: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan10 = v; break;
      case 212: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute10 = v; break;
      case 213: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue10 = v; break;
      case 214: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room10 = v; break;
      case 215: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq10 = v; break;
      case 216: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv10 = v; break;
      case 217: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm10 = v; break;
      case 218: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs10 = v; break;
      case 219: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl10 = v; break;
      case 220: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho10 = v; break;
      case 221: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly10 = v; break;
      case 222: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev10 = v; break;
      case 223: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb10 = v; break;
      case 224: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd10 = v; break;
      case 225: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb10 = v; break;
      case 226: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res10 = v; break;
      case 227: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn10 = v; break;
      case 228: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz10 = v; break;
      case 229: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc10 = v; break;
      case 230: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo10 = v; break;
      case 231: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_vol11 = v; break;
      case 232: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_pan11 = v; break;
      case 233: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mute11 = v; break;
      case 234: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_cue11 = v; break;
      case 235: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_room11 = v; break;
      case 236: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq11 = v; break;
      case 237: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv11 = v; break;
      case 238: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm11 = v; break;
      case 239: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs11 = v; break;
      case 240: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl11 = v; break;
      case 241: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho11 = v; break;
      case 242: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly11 = v; break;
      case 243: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev11 = v; break;
      case 244: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb11 = v; break;
      case 245: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd11 = v; break;
      case 246: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb11 = v; break;
      case 247: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res11 = v; break;
      case 248: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn11 = v; break;
      case 249: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz11 = v; break;
      case 250: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc11 = v; break;
      case 251: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_solo11 = v; break;
      case 252: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq_lo = v; break;
      case 253: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq_mid = v; break;
      case 254: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq_frq = v; break;
      case 255: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_eq_hi = v; break;
      case 256: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv_amt = v; break;
      case 257: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv_tone = v; break;
      case 258: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv_char = v; break;
      case 259: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drv_lvl = v; break;
      case 260: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm_rate = v; break;
      case 261: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm_shape = v; break;
      case 262: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_trm_st = v; break;
      case 263: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs_rate = v; break;
      case 264: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs_depth = v; break;
      case 265: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs_fb = v; break;
      case 266: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_phs_color = v; break;
      case 267: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl_att = v; break;
      case 268: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl_sens = v; break;
      case 269: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swl_curve = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_vol0;
      case 1: return P_pan0;
      case 2: return P_mute0;
      case 3: return P_cue0;
      case 4: return P_room0;
      case 5: return P_eq0;
      case 6: return P_drv0;
      case 7: return P_trm0;
      case 8: return P_phs0;
      case 9: return P_swl0;
      case 10: return P_cho0;
      case 11: return P_dly0;
      case 12: return P_rev0;
      case 13: return P_fxb0;
      case 14: return P_shd0;
      case 15: return P_orb0;
      case 16: return P_res0;
      case 17: return P_grn0;
      case 18: return P_frz0;
      case 19: return P_spc0;
      case 20: return P_solo0;
      case 21: return P_vol1;
      case 22: return P_pan1;
      case 23: return P_mute1;
      case 24: return P_cue1;
      case 25: return P_room1;
      case 26: return P_eq1;
      case 27: return P_drv1;
      case 28: return P_trm1;
      case 29: return P_phs1;
      case 30: return P_swl1;
      case 31: return P_cho1;
      case 32: return P_dly1;
      case 33: return P_rev1;
      case 34: return P_fxb1;
      case 35: return P_shd1;
      case 36: return P_orb1;
      case 37: return P_res1;
      case 38: return P_grn1;
      case 39: return P_frz1;
      case 40: return P_spc1;
      case 41: return P_solo1;
      case 42: return P_vol2;
      case 43: return P_pan2;
      case 44: return P_mute2;
      case 45: return P_cue2;
      case 46: return P_room2;
      case 47: return P_eq2;
      case 48: return P_drv2;
      case 49: return P_trm2;
      case 50: return P_phs2;
      case 51: return P_swl2;
      case 52: return P_cho2;
      case 53: return P_dly2;
      case 54: return P_rev2;
      case 55: return P_fxb2;
      case 56: return P_shd2;
      case 57: return P_orb2;
      case 58: return P_res2;
      case 59: return P_grn2;
      case 60: return P_frz2;
      case 61: return P_spc2;
      case 62: return P_solo2;
      case 63: return P_vol3;
      case 64: return P_pan3;
      case 65: return P_mute3;
      case 66: return P_cue3;
      case 67: return P_room3;
      case 68: return P_eq3;
      case 69: return P_drv3;
      case 70: return P_trm3;
      case 71: return P_phs3;
      case 72: return P_swl3;
      case 73: return P_cho3;
      case 74: return P_dly3;
      case 75: return P_rev3;
      case 76: return P_fxb3;
      case 77: return P_shd3;
      case 78: return P_orb3;
      case 79: return P_res3;
      case 80: return P_grn3;
      case 81: return P_frz3;
      case 82: return P_spc3;
      case 83: return P_solo3;
      case 84: return P_vol4;
      case 85: return P_pan4;
      case 86: return P_mute4;
      case 87: return P_cue4;
      case 88: return P_room4;
      case 89: return P_eq4;
      case 90: return P_drv4;
      case 91: return P_trm4;
      case 92: return P_phs4;
      case 93: return P_swl4;
      case 94: return P_cho4;
      case 95: return P_dly4;
      case 96: return P_rev4;
      case 97: return P_fxb4;
      case 98: return P_shd4;
      case 99: return P_orb4;
      case 100: return P_res4;
      case 101: return P_grn4;
      case 102: return P_frz4;
      case 103: return P_spc4;
      case 104: return P_solo4;
      case 105: return P_vol5;
      case 106: return P_pan5;
      case 107: return P_mute5;
      case 108: return P_cue5;
      case 109: return P_room5;
      case 110: return P_eq5;
      case 111: return P_drv5;
      case 112: return P_trm5;
      case 113: return P_phs5;
      case 114: return P_swl5;
      case 115: return P_cho5;
      case 116: return P_dly5;
      case 117: return P_rev5;
      case 118: return P_fxb5;
      case 119: return P_shd5;
      case 120: return P_orb5;
      case 121: return P_res5;
      case 122: return P_grn5;
      case 123: return P_frz5;
      case 124: return P_spc5;
      case 125: return P_solo5;
      case 126: return P_vol6;
      case 127: return P_pan6;
      case 128: return P_mute6;
      case 129: return P_cue6;
      case 130: return P_room6;
      case 131: return P_eq6;
      case 132: return P_drv6;
      case 133: return P_trm6;
      case 134: return P_phs6;
      case 135: return P_swl6;
      case 136: return P_cho6;
      case 137: return P_dly6;
      case 138: return P_rev6;
      case 139: return P_fxb6;
      case 140: return P_shd6;
      case 141: return P_orb6;
      case 142: return P_res6;
      case 143: return P_grn6;
      case 144: return P_frz6;
      case 145: return P_spc6;
      case 146: return P_solo6;
      case 147: return P_vol7;
      case 148: return P_pan7;
      case 149: return P_mute7;
      case 150: return P_cue7;
      case 151: return P_room7;
      case 152: return P_eq7;
      case 153: return P_drv7;
      case 154: return P_trm7;
      case 155: return P_phs7;
      case 156: return P_swl7;
      case 157: return P_cho7;
      case 158: return P_dly7;
      case 159: return P_rev7;
      case 160: return P_fxb7;
      case 161: return P_shd7;
      case 162: return P_orb7;
      case 163: return P_res7;
      case 164: return P_grn7;
      case 165: return P_frz7;
      case 166: return P_spc7;
      case 167: return P_solo7;
      case 168: return P_vol8;
      case 169: return P_pan8;
      case 170: return P_mute8;
      case 171: return P_cue8;
      case 172: return P_room8;
      case 173: return P_eq8;
      case 174: return P_drv8;
      case 175: return P_trm8;
      case 176: return P_phs8;
      case 177: return P_swl8;
      case 178: return P_cho8;
      case 179: return P_dly8;
      case 180: return P_rev8;
      case 181: return P_fxb8;
      case 182: return P_shd8;
      case 183: return P_orb8;
      case 184: return P_res8;
      case 185: return P_grn8;
      case 186: return P_frz8;
      case 187: return P_spc8;
      case 188: return P_solo8;
      case 189: return P_vol9;
      case 190: return P_pan9;
      case 191: return P_mute9;
      case 192: return P_cue9;
      case 193: return P_room9;
      case 194: return P_eq9;
      case 195: return P_drv9;
      case 196: return P_trm9;
      case 197: return P_phs9;
      case 198: return P_swl9;
      case 199: return P_cho9;
      case 200: return P_dly9;
      case 201: return P_rev9;
      case 202: return P_fxb9;
      case 203: return P_shd9;
      case 204: return P_orb9;
      case 205: return P_res9;
      case 206: return P_grn9;
      case 207: return P_frz9;
      case 208: return P_spc9;
      case 209: return P_solo9;
      case 210: return P_vol10;
      case 211: return P_pan10;
      case 212: return P_mute10;
      case 213: return P_cue10;
      case 214: return P_room10;
      case 215: return P_eq10;
      case 216: return P_drv10;
      case 217: return P_trm10;
      case 218: return P_phs10;
      case 219: return P_swl10;
      case 220: return P_cho10;
      case 221: return P_dly10;
      case 222: return P_rev10;
      case 223: return P_fxb10;
      case 224: return P_shd10;
      case 225: return P_orb10;
      case 226: return P_res10;
      case 227: return P_grn10;
      case 228: return P_frz10;
      case 229: return P_spc10;
      case 230: return P_solo10;
      case 231: return P_vol11;
      case 232: return P_pan11;
      case 233: return P_mute11;
      case 234: return P_cue11;
      case 235: return P_room11;
      case 236: return P_eq11;
      case 237: return P_drv11;
      case 238: return P_trm11;
      case 239: return P_phs11;
      case 240: return P_swl11;
      case 241: return P_cho11;
      case 242: return P_dly11;
      case 243: return P_rev11;
      case 244: return P_fxb11;
      case 245: return P_shd11;
      case 246: return P_orb11;
      case 247: return P_res11;
      case 248: return P_grn11;
      case 249: return P_frz11;
      case 250: return P_spc11;
      case 251: return P_solo11;
      case 252: return P_eq_lo;
      case 253: return P_eq_mid;
      case 254: return P_eq_frq;
      case 255: return P_eq_hi;
      case 256: return P_drv_amt;
      case 257: return P_drv_tone;
      case 258: return P_drv_char;
      case 259: return P_drv_lvl;
      case 260: return P_trm_rate;
      case 261: return P_trm_shape;
      case 262: return P_trm_st;
      case 263: return P_phs_rate;
      case 264: return P_phs_depth;
      case 265: return P_phs_fb;
      case 266: return P_phs_color;
      case 267: return P_swl_att;
      case 268: return P_swl_sens;
      case 269: return P_swl_curve;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 270;
  double* data(const std::string& n) {
    if (n == "tg") return D_tg;
    if (n == "sm") return D_sm;
    if (n == "eqc") return D_eqc;
    if (n == "eqz") return D_eqz;
    if (n == "dz") return D_dz;
    if (n == "apz") return D_apz;
    if (n == "pfb") return D_pfb;
    if (n == "swe") return D_swe;
    if (n == "swg") return D_swg;
    if (n == "swq") return D_swq;
    if (n == "xin") return D_xin;
    if (n == "xo") return D_xo;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double IN4 = in[3] ? (double) in[3][t] : 0.0;
      double IN5 = in[4] ? (double) in[4][t] : 0.0;
      double IN6 = in[5] ? (double) in[5][t] : 0.0;
      double IN7 = in[6] ? (double) in[6][t] : 0.0;
      double IN8 = in[7] ? (double) in[7][t] : 0.0;
      double IN9 = in[8] ? (double) in[8][t] : 0.0;
      double IN10 = in[9] ? (double) in[9][t] : 0.0;
      double IN11 = in[10] ? (double) in[10][t] : 0.0;
      double IN12 = in[11] ? (double) in[11][t] : 0.0;
      double IN13 = in[12] ? (double) in[12][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double OUT6 = 0.0;
      double OUT7 = 0.0;
      double OUT8 = 0.0;
      double OUT9 = 0.0;
      double OUT10 = 0.0;
      double OUT11 = 0.0;
      double OUT12 = 0.0;
      double OUT13 = 0.0;
      double OUT14 = 0.0;
      double OUT15 = 0.0;
      double OUT16 = 0.0;
      double OUT17 = 0.0;
      double OUT18 = 0.0;
      double OUT19 = 0.0;
      double OUT20 = 0.0;
      double OUT21 = 0.0;
      double OUT22 = 0.0;
      double OUT23 = 0.0;
      double OUT24 = 0.0;
      double OUT25 = 0.0;
      double OUT26 = 0.0;
      double OUT27 = 0.0;
      double OUT28 = 0.0;
      double v_bph = 0.0;
      double v_s = 0.0;
      double v_i = 0.0;
      double v_kk = 0.0;
      double v_v = 0.0;
      double v_glo = 0.0;
      double v_gmi = 0.0;
      double v_ghi = 0.0;
      double v_fmid = 0.0;
      double v_dru = 0.0;
      double v_dto = 0.0;
      double v_dch = 0.0;
      double v_dlv = 0.0;
      double v_cnt = 0.0;
      double v_am = 0.0;
      double v_aa = 0.0;
      double v_w0 = 0.0;
      double v_cs = 0.0;
      double v_al = 0.0;
      double v_sq = 0.0;
      double v_a0 = 0.0;
      double v_ri = 0.0;
      double v_cpb = 0.0;
      double v_tph = 0.0;
      double v_lfo = 0.0;
      double v_shp = 0.0;
      double v_sqw = 0.0;
      double v_tst = 0.0;
      double v_pph = 0.0;
      double v_psw = 0.0;
      double v_pfc = 0.0;
      double v_ptn = 0.0;
      double v_pco = 0.0;
      double v_pfbk = 0.0;
      double v_satt = 0.0;
      double v_sthr = 0.0;
      double v_scur = 0.0;
      double v_dgain = 0.0;
      double v_dcomp = 0.0;
      double v_dlp = 0.0;
      double v_dmx1 = 0.0;
      double v_dmx2 = 0.0;
      double v_dcb = 0.0;
      double v_dl = 0.0;
      double v_dr = 0.0;
      double v_cl = 0.0;
      double v_cr = 0.0;
      double v_anysolo = 0.0;
      double v_s_cho = 0.0;
      double v_s_dly = 0.0;
      double v_s_rev = 0.0;
      double v_s_fxb = 0.0;
      double v_s_shd = 0.0;
      double v_s_orb = 0.0;
      double v_s_res = 0.0;
      double v_s_grn = 0.0;
      double v_s_frz = 0.0;
      double v_s_spc = 0.0;
      double v_x = 0.0;
      double v_b = 0.0;
      double v_b0 = 0.0;
      double v_b1 = 0.0;
      double v_b2 = 0.0;
      double v_a1 = 0.0;
      double v_a2 = 0.0;
      double v_z1 = 0.0;
      double v_z2 = 0.0;
      double v_y = 0.0;
      double v_da = 0.0;
      double v_u = 0.0;
      double v_tube = 0.0;
      double v_od = 0.0;
      double v_fz = 0.0;
      double v_yy = 0.0;
      double v_p1 = 0.0;
      double v_p2 = 0.0;
      double v_hpy = 0.0;
      double v_lp = 0.0;
      double v_ta = 0.0;
      double v_panmod = 0.0;
      double v_pa = 0.0;
      double v_j = 0.0;
      double v_ax1 = 0.0;
      double v_ay1 = 0.0;
      double v_ay = 0.0;
      double v_wa = 0.0;
      double v_env = 0.0;
      double v_g = 0.0;
      double v_pn = 0.0;
      double v_cue = 0.0;
      double v_vg = 0.0;
      double v_rm = 0.0;
      double v_u_eq = 0.0;
      double v_u_drv = 0.0;
      double v_u_trm = 0.0;
      double v_u_phs = 0.0;
      double v_u_swl = 0.0;
      double v_u_cho = 0.0;
      double v_u_dly = 0.0;
      double v_u_rev = 0.0;
      double v_u_fxb = 0.0;
      double v_u_shd = 0.0;
      double v_u_orb = 0.0;
      double v_u_res = 0.0;
      double v_u_grn = 0.0;
      double v_u_frz = 0.0;
      double v_u_spc = 0.0;
      double v_used = 0.0;
      g_poke(D_xin,LEN_xin,CH_xin,IN1,(0.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN2,(1.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN3,(2.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN4,(3.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN5,(4.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN6,(5.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN7,(6.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN8,(7.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN9,(8.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN10,(9.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN11,(10.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN12,(11.0),0);
      g_poke(D_tg,LEN_tg,CH_tg,P_vol0,(0.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan0,(1.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute0,(2.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue0,(3.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room0,(4.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq0,(5.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv0,(6.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm0,(7.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs0,(8.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl0,(9.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho0,(10.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly0,(11.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev0,(12.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb0,(13.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd0,(14.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb0,(15.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res0,(16.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn0,(17.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz0,(18.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc0,(19.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo0,(20.0),(0.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol1,(0.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan1,(1.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute1,(2.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue1,(3.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room1,(4.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq1,(5.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv1,(6.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm1,(7.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs1,(8.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl1,(9.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho1,(10.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly1,(11.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev1,(12.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb1,(13.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd1,(14.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb1,(15.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res1,(16.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn1,(17.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz1,(18.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc1,(19.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo1,(20.0),(1.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol2,(0.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan2,(1.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute2,(2.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue2,(3.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room2,(4.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq2,(5.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv2,(6.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm2,(7.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs2,(8.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl2,(9.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho2,(10.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly2,(11.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev2,(12.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb2,(13.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd2,(14.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb2,(15.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res2,(16.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn2,(17.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz2,(18.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc2,(19.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo2,(20.0),(2.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol3,(0.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan3,(1.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute3,(2.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue3,(3.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room3,(4.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq3,(5.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv3,(6.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm3,(7.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs3,(8.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl3,(9.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho3,(10.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly3,(11.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev3,(12.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb3,(13.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd3,(14.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb3,(15.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res3,(16.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn3,(17.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz3,(18.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc3,(19.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo3,(20.0),(3.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol4,(0.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan4,(1.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute4,(2.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue4,(3.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room4,(4.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq4,(5.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv4,(6.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm4,(7.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs4,(8.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl4,(9.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho4,(10.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly4,(11.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev4,(12.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb4,(13.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd4,(14.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb4,(15.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res4,(16.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn4,(17.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz4,(18.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc4,(19.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo4,(20.0),(4.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol5,(0.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan5,(1.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute5,(2.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue5,(3.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room5,(4.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq5,(5.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv5,(6.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm5,(7.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs5,(8.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl5,(9.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho5,(10.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly5,(11.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev5,(12.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb5,(13.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd5,(14.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb5,(15.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res5,(16.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn5,(17.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz5,(18.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc5,(19.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo5,(20.0),(5.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol6,(0.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan6,(1.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute6,(2.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue6,(3.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room6,(4.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq6,(5.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv6,(6.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm6,(7.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs6,(8.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl6,(9.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho6,(10.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly6,(11.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev6,(12.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb6,(13.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd6,(14.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb6,(15.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res6,(16.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn6,(17.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz6,(18.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc6,(19.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo6,(20.0),(6.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol7,(0.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan7,(1.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute7,(2.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue7,(3.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room7,(4.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq7,(5.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv7,(6.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm7,(7.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs7,(8.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl7,(9.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho7,(10.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly7,(11.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev7,(12.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb7,(13.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd7,(14.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb7,(15.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res7,(16.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn7,(17.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz7,(18.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc7,(19.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo7,(20.0),(7.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol8,(0.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan8,(1.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute8,(2.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue8,(3.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room8,(4.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq8,(5.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv8,(6.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm8,(7.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs8,(8.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl8,(9.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho8,(10.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly8,(11.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev8,(12.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb8,(13.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd8,(14.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb8,(15.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res8,(16.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn8,(17.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz8,(18.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc8,(19.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo8,(20.0),(8.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol9,(0.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan9,(1.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute9,(2.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue9,(3.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room9,(4.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq9,(5.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv9,(6.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm9,(7.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs9,(8.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl9,(9.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho9,(10.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly9,(11.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev9,(12.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb9,(13.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd9,(14.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb9,(15.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res9,(16.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn9,(17.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz9,(18.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc9,(19.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo9,(20.0),(9.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol10,(0.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan10,(1.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute10,(2.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue10,(3.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room10,(4.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq10,(5.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv10,(6.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm10,(7.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs10,(8.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl10,(9.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho10,(10.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly10,(11.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev10,(12.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb10,(13.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd10,(14.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb10,(15.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res10,(16.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn10,(17.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz10,(18.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc10,(19.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo10,(20.0),(10.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_vol11,(0.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_pan11,(1.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_mute11,(2.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cue11,(3.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_room11,(4.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_eq11,(5.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_drv11,(6.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_trm11,(7.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_phs11,(8.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_swl11,(9.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_cho11,(10.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_dly11,(11.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_rev11,(12.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_fxb11,(13.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_shd11,(14.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_orb11,(15.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_res11,(16.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_grn11,(17.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_frz11,(18.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_spc11,(19.0),(11.0));
      g_poke(D_tg,LEN_tg,CH_tg,P_solo11,(20.0),(11.0));
      v_bph = IN13;
      if (((double)((H_c_init) == ((0.0)))) != 0.0) {
        for (v_s = (0.0); ((double)((v_s) < ((12.0)))) != 0.0; v_s = v_s + ((1.0))) {
          for (v_i = (0.0); ((double)((v_i) < ((21.0)))) != 0.0; v_i = v_i + ((1.0))) {
            g_poke(D_sm,LEN_sm,CH_sm,g_peek(D_tg,LEN_tg,CH_tg,v_i,v_s),v_i,v_s);
          }
          g_poke(D_swg,LEN_swg,CH_swg,(1.0),v_s,0);
        }
        H_c_init = (1.0);
      }
      v_kk = ((1.0) - exp(((-(1.0)) / (((10.0))*SR/1000.0))));
      for (v_s = (0.0); ((double)((v_s) < ((12.0)))) != 0.0; v_s = v_s + ((1.0))) {
        for (v_i = (0.0); ((double)((v_i) < ((21.0)))) != 0.0; v_i = v_i + ((1.0))) {
          v_v = g_peek(D_sm,LEN_sm,CH_sm,v_i,v_s);
          g_poke(D_sm,LEN_sm,CH_sm,(v_v + ((g_peek(D_tg,LEN_tg,CH_tg,v_i,v_s) - v_v) * v_kk)),v_i,v_s);
        }
      }
      v_glo = (H_c_lo + ((((P_eq_lo - (50.0)) * (0.3)) - H_c_lo) * v_kk));
      H_c_lo = v_glo;
      v_gmi = (H_c_mid + ((((P_eq_mid - (50.0)) * (0.3)) - H_c_mid) * v_kk));
      H_c_mid = v_gmi;
      v_ghi = (H_c_hi + ((((P_eq_hi - (50.0)) * (0.3)) - H_c_hi) * v_kk));
      H_c_hi = v_ghi;
      v_fmid = (H_c_frq + ((((150.0) * pow((40.0),(P_eq_frq / (100.0)))) - H_c_frq) * v_kk));
      H_c_frq = v_fmid;
      v_dru = (H_c_drv + ((P_drv_amt - H_c_drv) * v_kk));
      H_c_drv = v_dru;
      v_dto = (H_c_tone + ((P_drv_tone - H_c_tone) * v_kk));
      H_c_tone = v_dto;
      v_dch = (H_c_chr + ((P_drv_char - H_c_chr) * v_kk));
      H_c_chr = v_dch;
      v_dlv = (H_c_lvl + ((P_drv_lvl - H_c_lvl) * v_kk));
      H_c_lvl = v_dlv;
      v_cnt = (H_c_cnt + (1.0));
      if (((double)((v_cnt) >= ((32.0)))) != 0.0) {
        v_cnt = (0.0);
      }
      H_c_cnt = v_cnt;
      if (((double)((v_cnt) == ((0.0)))) != 0.0) {
        for (v_s = (0.0); ((double)((v_s) < ((12.0)))) != 0.0; v_s = v_s + ((1.0))) {
          v_am = (g_peek(D_sm,LEN_sm,CH_sm,(5.0),v_s) / (100.0));
          v_aa = pow((10.0),((v_glo * v_am) / (40.0)));
          v_w0 = (((2.0*M_PI) * (180.0)) / SR);
          v_cs = cos(v_w0);
          v_al = (sin(v_w0) * (0.7071));
          v_sq = (((2.0) * sqrt(fmax(0.0,v_aa))) * v_al);
          v_a0 = (((v_aa + (1.0)) + ((v_aa - (1.0)) * v_cs)) + v_sq);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((v_aa * (((v_aa + (1.0)) - ((v_aa - (1.0)) * v_cs)) + v_sq)) / v_a0),(0.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((((2.0) * v_aa) * ((v_aa - (1.0)) - ((v_aa + (1.0)) * v_cs))) / v_a0),(1.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((v_aa * (((v_aa + (1.0)) - ((v_aa - (1.0)) * v_cs)) - v_sq)) / v_a0),(2.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((-(2.0)) * ((v_aa - (1.0)) + ((v_aa + (1.0)) * v_cs))) / v_a0),(3.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((((v_aa + (1.0)) + ((v_aa - (1.0)) * v_cs)) - v_sq) / v_a0),(4.0),v_s);
          v_aa = pow((10.0),((v_gmi * v_am) / (40.0)));
          v_w0 = (((2.0*M_PI) * v_fmid) / SR);
          v_cs = cos(v_w0);
          v_al = (sin(v_w0) / ((2.0) * (0.9)));
          v_a0 = ((1.0) + (v_al / v_aa));
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((1.0) + (v_al * v_aa)) / v_a0),(5.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((-(2.0)) * v_cs) / v_a0),(6.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((1.0) - (v_al * v_aa)) / v_a0),(7.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((-(2.0)) * v_cs) / v_a0),(8.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((1.0) - (v_al / v_aa)) / v_a0),(9.0),v_s);
          v_aa = pow((10.0),((v_ghi * v_am) / (40.0)));
          v_w0 = (((2.0*M_PI) * (5000.0)) / SR);
          v_cs = cos(v_w0);
          v_al = (sin(v_w0) * (0.7071));
          v_sq = (((2.0) * sqrt(fmax(0.0,v_aa))) * v_al);
          v_a0 = (((v_aa + (1.0)) - ((v_aa - (1.0)) * v_cs)) + v_sq);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((v_aa * (((v_aa + (1.0)) + ((v_aa - (1.0)) * v_cs)) + v_sq)) / v_a0),(10.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((((-(2.0)) * v_aa) * ((v_aa - (1.0)) + ((v_aa + (1.0)) * v_cs))) / v_a0),(11.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((v_aa * (((v_aa + (1.0)) + ((v_aa - (1.0)) * v_cs)) - v_sq)) / v_a0),(12.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,(((2.0) * ((v_aa - (1.0)) - ((v_aa + (1.0)) * v_cs))) / v_a0),(13.0),v_s);
          g_poke(D_eqc,LEN_eqc,CH_eqc,((((v_aa + (1.0)) - ((v_aa - (1.0)) * v_cs)) - v_sq) / v_a0),(14.0),v_s);
        }
      }
      v_ri = floor(((P_trm_rate / (100.0)) * (5.99)));
      v_cpb = (0.25);
      if (((double)((v_ri) == ((1.0)))) != 0.0) {
        v_cpb = (0.5);
      }
      if (((double)((v_ri) == ((2.0)))) != 0.0) {
        v_cpb = (1.0);
      }
      if (((double)((v_ri) == ((3.0)))) != 0.0) {
        v_cpb = (2.0);
      }
      if (((double)((v_ri) == ((4.0)))) != 0.0) {
        v_cpb = (3.0);
      }
      if (((double)((v_ri) == ((5.0)))) != 0.0) {
        v_cpb = (4.0);
      }
      v_tph = g_wrap((v_bph * v_cpb),(0.0),(1.0));
      v_lfo = ((0.5) + ((0.5) * cos(((2.0*M_PI) * v_tph))));
      v_shp = (P_trm_shape / (100.0));
      v_sqw = g_clamp((((v_lfo - (0.5)) * ((1.0) + (v_shp * (30.0)))) + (0.5)),(0.0),(1.0));
      v_lfo = (v_lfo + ((v_sqw - v_lfo) * v_shp));
      v_tst = (P_trm_st / (100.0));
      v_pph = g_wrap((H_c_pph + (((0.03) * pow((100.0),(P_phs_rate / (100.0)))) / SR)),(0.0),(1.0));
      H_c_pph = v_pph;
      v_psw = ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_pph))));
      v_pfc = ((200.0) * pow((2.0),(((P_phs_color / (100.0)) * (2.5)) + ((v_psw * (P_phs_depth / (100.0))) * (4.5)))));
      v_ptn = tan(((M_PI * fmin(v_pfc,(SR * (0.45)))) / SR));
      v_pco = ((v_ptn - (1.0)) / (v_ptn + (1.0)));
      v_pfbk = ((P_phs_fb / (100.0)) * (0.85));
      v_satt = ((((30.0) + (pow((P_swl_att / (100.0)),(2.0)) * (3000.0))))*SR/1000.0);
      v_sthr = pow(10.0,(((-(60.0)) + ((P_swl_sens / (100.0)) * (40.0))))/20.0);
      v_scur = ((0.5) + ((P_swl_curve / (100.0)) * (2.5)));
      v_dgain = pow(10.0,((v_dru * (0.42)))/20.0);
      v_dcomp = (((1.0) / fmin(v_dgain,(3.5))) * (v_dlv / (50.0)));
      v_dlp = ((1.0) - exp((((-(2.0*M_PI)) * ((500.0) * pow((32.0),(v_dto / (100.0))))) / SR)));
      v_dmx1 = g_clamp((v_dch / (50.0)),(0.0),(1.0));
      v_dmx2 = g_clamp(((v_dch / (50.0)) - (1.0)),(0.0),(1.0));
      v_dcb = ((1.0) - (((2.0*M_PI) * (20.0)) / SR));
      v_dl = (0.0);
      v_dr = (0.0);
      v_cl = (0.0);
      v_cr = (0.0);
      v_anysolo = (0.0);
      for (v_s = (0.0); ((double)((v_s) < ((12.0)))) != 0.0; v_s = v_s + ((1.0))) {
        if (((double)((g_peek(D_tg,LEN_tg,CH_tg,(20.0),v_s)) > ((0.5)))) != 0.0) {
          v_anysolo = (1.0);
        }
      }
      v_s_cho = (0.0);
      v_s_dly = (0.0);
      v_s_rev = (0.0);
      v_s_fxb = (0.0);
      v_s_shd = (0.0);
      v_s_orb = (0.0);
      v_s_res = (0.0);
      v_s_grn = (0.0);
      v_s_frz = (0.0);
      v_s_spc = (0.0);
      for (v_s = (0.0); ((double)((v_s) < ((12.0)))) != 0.0; v_s = v_s + ((1.0))) {
        v_x = g_peek(D_xin,LEN_xin,CH_xin,v_s,0);
        for (v_b = (0.0); ((double)((v_b) < ((3.0)))) != 0.0; v_b = v_b + ((1.0))) {
          v_b0 = g_peek(D_eqc,LEN_eqc,CH_eqc,(v_b * (5.0)),v_s);
          v_b1 = g_peek(D_eqc,LEN_eqc,CH_eqc,((v_b * (5.0)) + (1.0)),v_s);
          v_b2 = g_peek(D_eqc,LEN_eqc,CH_eqc,((v_b * (5.0)) + (2.0)),v_s);
          v_a1 = g_peek(D_eqc,LEN_eqc,CH_eqc,((v_b * (5.0)) + (3.0)),v_s);
          v_a2 = g_peek(D_eqc,LEN_eqc,CH_eqc,((v_b * (5.0)) + (4.0)),v_s);
          v_z1 = g_peek(D_eqz,LEN_eqz,CH_eqz,(v_b * (2.0)),v_s);
          v_z2 = g_peek(D_eqz,LEN_eqz,CH_eqz,((v_b * (2.0)) + (1.0)),v_s);
          if (((double)((((double)((((double)((v_b0) == ((0.0))))) && (((double)((v_b1) == ((0.0)))))))) && (((double)((v_b2) == ((0.0))))))) != 0.0) {
            v_b0 = (1.0);
          }
          v_y = ((v_b0 * v_x) + v_z1);
          g_poke(D_eqz,LEN_eqz,CH_eqz,(((v_b1 * v_x) - (v_a1 * v_y)) + v_z2),(v_b * (2.0)),v_s);
          g_poke(D_eqz,LEN_eqz,CH_eqz,((v_b2 * v_x) - (v_a2 * v_y)),((v_b * (2.0)) + (1.0)),v_s);
          v_x = v_y;
        }
        v_da = (g_peek(D_sm,LEN_sm,CH_sm,(6.0),v_s) / (100.0));
        if (((double)((v_da) > ((0.0001)))) != 0.0) {
          v_u = (v_x * v_dgain);
          v_tube = (tanh((v_u + (0.25))) - tanh((0.25)));
          v_od = tanh(v_u);
          v_fz = g_clamp((v_u * (2.5)),(-(1.0)),(1.0));
          v_yy = (v_tube + ((v_od - v_tube) * v_dmx1));
          v_yy = (v_yy + ((v_fz - v_yy) * v_dmx2));
          v_p1 = g_peek(D_dz,LEN_dz,CH_dz,(0.0),v_s);
          v_p2 = g_peek(D_dz,LEN_dz,CH_dz,(1.0),v_s);
          v_hpy = ((v_yy - v_p1) + (v_dcb * v_p2));
          g_poke(D_dz,LEN_dz,CH_dz,v_yy,(0.0),v_s);
          g_poke(D_dz,LEN_dz,CH_dz,v_hpy,(1.0),v_s);
          v_lp = (g_peek(D_dz,LEN_dz,CH_dz,(2.0),v_s) + ((v_hpy - g_peek(D_dz,LEN_dz,CH_dz,(2.0),v_s)) * v_dlp));
          g_poke(D_dz,LEN_dz,CH_dz,v_lp,(2.0),v_s);
          v_x = (v_x + (((v_lp * v_dcomp) - v_x) * v_da));
        }
        v_ta = (g_peek(D_sm,LEN_sm,CH_sm,(7.0),v_s) / (100.0));
        v_x = (v_x * ((1.0) - ((v_ta * ((1.0) - v_tst)) * ((1.0) - v_lfo))));
        v_panmod = ((v_ta * v_tst) * (v_lfo - (0.5)));
        v_pa = (g_peek(D_sm,LEN_sm,CH_sm,(8.0),v_s) / (100.0));
        if (((double)((v_pa) > ((0.0001)))) != 0.0) {
          v_u = (v_x + (g_peek(D_pfb,LEN_pfb,CH_pfb,v_s,0) * v_pfbk));
          for (v_j = (0.0); ((double)((v_j) < ((4.0)))) != 0.0; v_j = v_j + ((1.0))) {
            v_ax1 = g_peek(D_apz,LEN_apz,CH_apz,(v_j * (2.0)),v_s);
            v_ay1 = g_peek(D_apz,LEN_apz,CH_apz,((v_j * (2.0)) + (1.0)),v_s);
            v_ay = (((v_pco * v_u) + v_ax1) - (v_pco * v_ay1));
            g_poke(D_apz,LEN_apz,CH_apz,v_u,(v_j * (2.0)),v_s);
            g_poke(D_apz,LEN_apz,CH_apz,v_ay,((v_j * (2.0)) + (1.0)),v_s);
            v_u = v_ay;
          }
          g_poke(D_pfb,LEN_pfb,CH_pfb,v_u,v_s,0);
          v_x = (v_x + ((((0.5) * (v_x + v_u)) - v_x) * v_pa));
        }
        v_wa = (g_peek(D_sm,LEN_sm,CH_sm,(9.0),v_s) / (100.0));
        v_env = fmax(fabs(v_x),(g_peek(D_swe,LEN_swe,CH_swe,v_s,0) * (0.9995)));
        g_poke(D_swe,LEN_swe,CH_swe,v_env,v_s,0);
        v_g = g_peek(D_swg,LEN_swg,CH_swg,v_s,0);
        if (((double)((v_wa) > ((0.0001)))) != 0.0) {
          if (((double)((v_env) < ((v_sthr * (0.5))))) != 0.0) {
            g_poke(D_swq,LEN_swq,CH_swq,(1.0),v_s,0);
          }
          if (((double)((((double)((v_env) > (v_sthr)))) && (((double)((g_peek(D_swq,LEN_swq,CH_swq,v_s,0)) > ((0.0))))))) != 0.0) {
            v_g = (0.0);
            g_poke(D_swq,LEN_swq,CH_swq,(0.0),v_s,0);
          }
          v_g = fmin((1.0),(v_g + ((1.0) / v_satt)));
          v_x = (v_x * (((1.0) - v_wa) + (v_wa * pow(v_g,v_scur))));
        } else {
          v_g = (1.0);
        }
        g_poke(D_swg,LEN_swg,CH_swg,v_g,v_s,0);
        v_pn = g_clamp((((g_peek(D_sm,LEN_sm,CH_sm,(1.0),v_s) + (50.0)) / (100.0)) + v_panmod),(0.0),(1.0));
        v_cue = g_peek(D_sm,LEN_sm,CH_sm,(3.0),v_s);
        v_cl = (v_cl + ((v_x * v_cue) * cos(((v_pn * M_PI) * (0.5)))));
        v_cr = (v_cr + ((v_x * v_cue) * sin(((v_pn * M_PI) * (0.5)))));
        v_vg = ((pow((g_peek(D_sm,LEN_sm,CH_sm,(0.0),v_s) / (100.0)),(2.0)) * (2.0)) * ((1.0) - g_peek(D_sm,LEN_sm,CH_sm,(2.0),v_s)));
        if (((double)((v_anysolo) > ((0.0)))) != 0.0) {
          v_vg = (v_vg * g_peek(D_sm,LEN_sm,CH_sm,(20.0),v_s));
        }
        v_x = (v_x * v_vg);
        g_poke(D_xo,LEN_xo,CH_xo,v_x,v_s,0);
        v_rm = g_peek(D_sm,LEN_sm,CH_sm,(4.0),v_s);
        // ARCO 1.3: a strip placed by AMBISONICS leaves the stereo mix (it plays on the speakers by itself) but keeps its effect sends
        bool v_amb = ambGate[(int) v_s] > 0.5;
        double v_xpre = v_x;
        v_x = (v_x * v_rm);
        if (!v_amb) {
        v_dl = (v_dl + (v_x * cos(((v_pn * M_PI) * (0.5)))));
        v_dr = (v_dr + (v_x * sin(((v_pn * M_PI) * (0.5)))));
        } else v_x = v_xpre;
        if (((double)((v_rm) > ((0.0001)))) != 0.0 || v_amb) {
          v_s_cho = (v_s_cho + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(10.0),v_s)) / (100.0)));
          v_s_dly = (v_s_dly + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(11.0),v_s)) / (100.0)));
          v_s_rev = (v_s_rev + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(12.0),v_s)) / (100.0)));
          v_s_fxb = (v_s_fxb + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(13.0),v_s)) / (100.0)));
          v_s_shd = (v_s_shd + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(14.0),v_s)) / (100.0)));
          v_s_orb = (v_s_orb + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(15.0),v_s)) / (100.0)));
          v_s_res = (v_s_res + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(16.0),v_s)) / (100.0)));
          v_s_grn = (v_s_grn + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(17.0),v_s)) / (100.0)));
          v_s_frz = (v_s_frz + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(18.0),v_s)) / (100.0)));
          v_s_spc = (v_s_spc + ((v_x * g_peek(D_sm,LEN_sm,CH_sm,(19.0),v_s)) / (100.0)));
        }
      }
      OUT1 = g_peek(D_xo,LEN_xo,CH_xo,(0.0),0);
      OUT2 = g_peek(D_xo,LEN_xo,CH_xo,(1.0),0);
      OUT3 = g_peek(D_xo,LEN_xo,CH_xo,(2.0),0);
      OUT4 = g_peek(D_xo,LEN_xo,CH_xo,(3.0),0);
      OUT5 = g_peek(D_xo,LEN_xo,CH_xo,(4.0),0);
      OUT6 = g_peek(D_xo,LEN_xo,CH_xo,(5.0),0);
      OUT7 = g_peek(D_xo,LEN_xo,CH_xo,(6.0),0);
      OUT8 = g_peek(D_xo,LEN_xo,CH_xo,(7.0),0);
      OUT9 = g_peek(D_xo,LEN_xo,CH_xo,(8.0),0);
      OUT10 = g_peek(D_xo,LEN_xo,CH_xo,(9.0),0);
      OUT11 = g_peek(D_xo,LEN_xo,CH_xo,(10.0),0);
      OUT12 = g_peek(D_xo,LEN_xo,CH_xo,(11.0),0);
      OUT13 = v_dl;
      OUT14 = v_dr;
      OUT15 = v_cl;
      OUT16 = v_cr;
      OUT17 = (0.0);
      OUT18 = v_s_cho;
      OUT19 = v_s_dly;
      OUT20 = v_s_rev;
      OUT21 = v_s_fxb;
      OUT22 = v_s_shd;
      OUT23 = v_s_orb;
      OUT24 = v_s_res;
      OUT25 = v_s_grn;
      OUT26 = v_s_frz;
      OUT27 = v_s_spc;
      v_u_eq = (0.0);
      v_u_drv = (0.0);
      v_u_trm = (0.0);
      v_u_phs = (0.0);
      v_u_swl = (0.0);
      v_u_cho = (0.0);
      v_u_dly = (0.0);
      v_u_rev = (0.0);
      v_u_fxb = (0.0);
      v_u_shd = (0.0);
      v_u_orb = (0.0);
      v_u_res = (0.0);
      v_u_grn = (0.0);
      v_u_frz = (0.0);
      v_u_spc = (0.0);
      for (v_s = (0.0); ((double)((v_s) < ((12.0)))) != 0.0; v_s = v_s + ((1.0))) {
        {
          v_u_eq = fmax(v_u_eq,((double)((g_peek(D_sm,LEN_sm,CH_sm,(5.0),v_s)) > ((0.5)))));
          v_u_drv = fmax(v_u_drv,((double)((g_peek(D_sm,LEN_sm,CH_sm,(6.0),v_s)) > ((0.5)))));
          v_u_trm = fmax(v_u_trm,((double)((g_peek(D_sm,LEN_sm,CH_sm,(7.0),v_s)) > ((0.5)))));
          v_u_phs = fmax(v_u_phs,((double)((g_peek(D_sm,LEN_sm,CH_sm,(8.0),v_s)) > ((0.5)))));
          v_u_swl = fmax(v_u_swl,((double)((g_peek(D_sm,LEN_sm,CH_sm,(9.0),v_s)) > ((0.5)))));
          v_u_cho = fmax(v_u_cho,((double)((g_peek(D_sm,LEN_sm,CH_sm,(10.0),v_s)) > ((0.5)))));
          v_u_dly = fmax(v_u_dly,((double)((g_peek(D_sm,LEN_sm,CH_sm,(11.0),v_s)) > ((0.5)))));
          v_u_rev = fmax(v_u_rev,((double)((g_peek(D_sm,LEN_sm,CH_sm,(12.0),v_s)) > ((0.5)))));
          v_u_fxb = fmax(v_u_fxb,((double)((g_peek(D_sm,LEN_sm,CH_sm,(13.0),v_s)) > ((0.5)))));
          v_u_shd = fmax(v_u_shd,((double)((g_peek(D_sm,LEN_sm,CH_sm,(14.0),v_s)) > ((0.5)))));
          v_u_orb = fmax(v_u_orb,((double)((g_peek(D_sm,LEN_sm,CH_sm,(15.0),v_s)) > ((0.5)))));
          v_u_res = fmax(v_u_res,((double)((g_peek(D_sm,LEN_sm,CH_sm,(16.0),v_s)) > ((0.5)))));
          v_u_grn = fmax(v_u_grn,((double)((g_peek(D_sm,LEN_sm,CH_sm,(17.0),v_s)) > ((0.5)))));
          v_u_frz = fmax(v_u_frz,((double)((g_peek(D_sm,LEN_sm,CH_sm,(18.0),v_s)) > ((0.5)))));
          v_u_spc = fmax(v_u_spc,((double)((g_peek(D_sm,LEN_sm,CH_sm,(19.0),v_s)) > ((0.5)))));
        }
      }
      v_used = (((((((((((((((v_u_eq * (1.0)) + (v_u_drv * (2.0))) + (v_u_trm * (4.0))) + (v_u_phs * (8.0))) + (v_u_swl * (16.0))) + (v_u_cho * (32.0))) + (v_u_dly * (64.0))) + (v_u_rev * (128.0))) + (v_u_fxb * (256.0))) + (v_u_shd * (512.0))) + (v_u_orb * (1024.0))) + (v_u_res * (4096.0))) + (v_u_grn * (8192.0))) + (v_u_frz * (16384.0))) + (v_u_spc * (32768.0)));
      OUT28 = v_used;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
      if (out[5]) out[5][t] = (float) OUT6;
      if (out[6]) out[6][t] = (float) OUT7;
      if (out[7]) out[7][t] = (float) OUT8;
      if (out[8]) out[8][t] = (float) OUT9;
      if (out[9]) out[9][t] = (float) OUT10;
      if (out[10]) out[10][t] = (float) OUT11;
      if (out[11]) out[11][t] = (float) OUT12;
      if (out[12]) out[12][t] = (float) OUT13;
      if (out[13]) out[13][t] = (float) OUT14;
      if (out[14]) out[14][t] = (float) OUT15;
      if (out[15]) out[15][t] = (float) OUT16;
      if (out[16]) out[16][t] = (float) OUT17;
      if (out[17]) out[17][t] = (float) OUT18;
      if (out[18]) out[18][t] = (float) OUT19;
      if (out[19]) out[19][t] = (float) OUT20;
      if (out[20]) out[20][t] = (float) OUT21;
      if (out[21]) out[21][t] = (float) OUT22;
      if (out[22]) out[22][t] = (float) OUT23;
      if (out[23]) out[23][t] = (float) OUT24;
      if (out[24]) out[24][t] = (float) OUT25;
      if (out[25]) out[25][t] = (float) OUT26;
      if (out[26]) out[26][t] = (float) OUT27;
      if (out[27]) out[27][t] = (float) OUT28;
    }
  }
};
// ---------------------------------------------------------------- Halo
struct Halo {
  double SR = 44100.0;
  double P_halo0 = 0.0;
  double P_halo1 = 0.0;
  double P_halo2 = 0.0;
  double P_halo3 = 0.0;
  double P_halo4 = 0.0;
  double P_halo5 = 0.0;
  double P_halo6 = 0.0;
  double P_halo7 = 0.0;
  double P_halo8 = 0.0;
  double P_density = 30.0;
  double P_reg = 50.0;
  double P_mood = 50.0;
  double P_glide = 45.0;
  double P_dark = 55.0;
  double P_swell = 40.0;
  double P_tail = 30.0;
  std::vector<double> V_ring; double* D_ring = nullptr; long LEN_ring = 96000; long CH_ring = 9;
  std::vector<double> V_xin; double* D_xin = nullptr; long LEN_xin = 9; long CH_xin = 1;
  std::vector<double> V_pin; double* D_pin = nullptr; long LEN_pin = 9; long CH_pin = 1;
  std::vector<double> V_amt; double* D_amt = nullptr; long LEN_amt = 9; long CH_amt = 1;
  std::vector<double> V_sst; double* D_sst = nullptr; long LEN_sst = 12; long CH_sst = 9;
  std::vector<double> V_vg; double* D_vg = nullptr; long LEN_vg = 32; long CH_vg = 9;
  std::vector<double> V_va; double* D_va = nullptr; long LEN_va = 32; long CH_va = 9;
  std::vector<double> V_vt; double* D_vt = nullptr; long LEN_vt = 32; long CH_vt = 9;
  std::vector<double> V_vs; double* D_vs = nullptr; long LEN_vs = 32; long CH_vs = 9;
  std::vector<double> V_vo; double* D_vo = nullptr; long LEN_vo = 32; long CH_vo = 9;
  std::vector<double> V_vq; double* D_vq = nullptr; long LEN_vq = 32; long CH_vq = 9;
  std::vector<double> V_vf; double* D_vf = nullptr; long LEN_vf = 32; long CH_vf = 9;
  std::vector<double> V_von; double* D_von = nullptr; long LEN_von = 32; long CH_von = 9;
  std::vector<double> V_vph; double* D_vph = nullptr; long LEN_vph = 32; long CH_vph = 9;
  std::vector<double> V_vw; double* D_vw = nullptr; long LEN_vw = 32; long CH_vw = 9;
  std::vector<double> V_vpl; double* D_vpl = nullptr; long LEN_vpl = 32; long CH_vpl = 1;
  std::vector<double> V_vpn; double* D_vpn = nullptr; long LEN_vpn = 32; long CH_vpn = 1;
  std::vector<double> V_hist; double* D_hist = nullptr; long LEN_hist = 12; long CH_hist = 1;
  std::vector<double> V_mo; double* D_mo = nullptr; long LEN_mo = 7; long CH_mo = 1;
  std::vector<double> V_rpM; double* D_rpM = nullptr; long LEN_rpM = 7; long CH_rpM = 1;
  std::vector<double> V_rpm; double* D_rpm = nullptr; long LEN_rpm = 7; long CH_rpm = 1;
  std::vector<double> V_scl; double* D_scl = nullptr; long LEN_scl = 7; long CH_scl = 1;
  std::vector<double> V_prank; double* D_prank = nullptr; long LEN_prank = 12; long CH_prank = 1;
  std::vector<double> V_cp; double* D_cp = nullptr; long LEN_cp = 160; long CH_cp = 1;
  std::vector<double> V_cs; double* D_cs = nullptr; long LEN_cs = 160; long CH_cs = 1;
  std::vector<double> V_cu; double* D_cu = nullptr; long LEN_cu = 160; long CH_cu = 1;
  std::vector<double> V_tp; double* D_tp = nullptr; long LEN_tp = 40; long CH_tp = 1;
  std::vector<double> V_tu; double* D_tu = nullptr; long LEN_tu = 40; long CH_tu = 1;
  std::vector<double> V_vm; double* D_vm = nullptr; long LEN_vm = 32; long CH_vm = 1;
  std::vector<double> V_pcu; double* D_pcu = nullptr; long LEN_pcu = 12; long CH_pcu = 1;
  std::vector<double> V_lk; double* D_lk = nullptr; long LEN_lk = 9; long CH_lk = 1;
  std::vector<double> V_cld; double* D_cld = nullptr; long LEN_cld = 96000; long CH_cld = 1;
  std::vector<double> V_tgp; double* D_tgp = nullptr; long LEN_tgp = 8; long CH_tgp = 1;
  std::vector<double> V_tga; double* D_tga = nullptr; long LEN_tga = 8; long CH_tga = 1;
  std::vector<double> V_tgl; double* D_tgl = nullptr; long LEN_tgl = 8; long CH_tgl = 1;
  std::vector<double> V_tgL; double* D_tgL = nullptr; long LEN_tgL = 8; long CH_tgL = 1;
  std::vector<double> V_tgR; double* D_tgR = nullptr; long LEN_tgR = 8; long CH_tgR = 1;
  double H_h_init = 0.0;
  double H_h_wi = 0.0;
  double H_h_key = -1.0;
  double H_h_live = -1.0;
  double H_h_mask = 0.0;
  double H_t_tick = 0.0;
  double H_t_lp = 0.0;
  double H_t_hp = 0.0;
  double H_t_hx = 0.0;
  double H_t_amt = 0.0;
  double H_h_bl = 0.0;
  double H_h_lph = 0.0;
  double H_h_f1l = 0.0;
  double H_h_f2l = 0.0;
  double H_h_f1r = 0.0;
  double H_h_f2r = 0.0;
  static constexpr int numInputs = 18;
  static constexpr int numOutputs = 7;
  void init(double sr) { SR = sr;
    V_ring.assign((size_t) 96000 * 9, 0.0); D_ring = V_ring.data();
    V_xin.assign((size_t) 9 * 1, 0.0); D_xin = V_xin.data();
    V_pin.assign((size_t) 9 * 1, 0.0); D_pin = V_pin.data();
    V_amt.assign((size_t) 9 * 1, 0.0); D_amt = V_amt.data();
    V_sst.assign((size_t) 12 * 9, 0.0); D_sst = V_sst.data();
    V_vg.assign((size_t) 32 * 9, 0.0); D_vg = V_vg.data();
    V_va.assign((size_t) 32 * 9, 0.0); D_va = V_va.data();
    V_vt.assign((size_t) 32 * 9, 0.0); D_vt = V_vt.data();
    V_vs.assign((size_t) 32 * 9, 0.0); D_vs = V_vs.data();
    V_vo.assign((size_t) 32 * 9, 0.0); D_vo = V_vo.data();
    V_vq.assign((size_t) 32 * 9, 0.0); D_vq = V_vq.data();
    V_vf.assign((size_t) 32 * 9, 0.0); D_vf = V_vf.data();
    V_von.assign((size_t) 32 * 9, 0.0); D_von = V_von.data();
    V_vph.assign((size_t) 32 * 9, 0.0); D_vph = V_vph.data();
    V_vw.assign((size_t) 32 * 9, 0.0); D_vw = V_vw.data();
    V_vpl.assign((size_t) 32 * 1, 0.0); D_vpl = V_vpl.data();
    V_vpn.assign((size_t) 32 * 1, 0.0); D_vpn = V_vpn.data();
    V_hist.assign((size_t) 12 * 1, 0.0); D_hist = V_hist.data();
    V_mo.assign((size_t) 7 * 1, 0.0); D_mo = V_mo.data();
    V_rpM.assign((size_t) 7 * 1, 0.0); D_rpM = V_rpM.data();
    V_rpm.assign((size_t) 7 * 1, 0.0); D_rpm = V_rpm.data();
    V_scl.assign((size_t) 7 * 1, 0.0); D_scl = V_scl.data();
    V_prank.assign((size_t) 12 * 1, 0.0); D_prank = V_prank.data();
    V_cp.assign((size_t) 160 * 1, 0.0); D_cp = V_cp.data();
    V_cs.assign((size_t) 160 * 1, 0.0); D_cs = V_cs.data();
    V_cu.assign((size_t) 160 * 1, 0.0); D_cu = V_cu.data();
    V_tp.assign((size_t) 40 * 1, 0.0); D_tp = V_tp.data();
    V_tu.assign((size_t) 40 * 1, 0.0); D_tu = V_tu.data();
    V_vm.assign((size_t) 32 * 1, 0.0); D_vm = V_vm.data();
    V_pcu.assign((size_t) 12 * 1, 0.0); D_pcu = V_pcu.data();
    V_lk.assign((size_t) 9 * 1, 0.0); D_lk = V_lk.data();
    V_cld.assign((size_t) 96000 * 1, 0.0); D_cld = V_cld.data();
    V_tgp.assign((size_t) 8 * 1, 0.0); D_tgp = V_tgp.data();
    V_tga.assign((size_t) 8 * 1, 0.0); D_tga = V_tga.data();
    V_tgl.assign((size_t) 8 * 1, 0.0); D_tgl = V_tgl.data();
    V_tgL.assign((size_t) 8 * 1, 0.0); D_tgL = V_tgL.data();
    V_tgR.assign((size_t) 8 * 1, 0.0); D_tgR = V_tgR.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "halo0") return 0;
    if (n == "halo1") return 1;
    if (n == "halo2") return 2;
    if (n == "halo3") return 3;
    if (n == "halo4") return 4;
    if (n == "halo5") return 5;
    if (n == "halo6") return 6;
    if (n == "halo7") return 7;
    if (n == "halo8") return 8;
    if (n == "density") return 9;
    if (n == "reg") return 10;
    if (n == "mood") return 11;
    if (n == "glide") return 12;
    if (n == "dark") return 13;
    if (n == "swell") return 14;
    if (n == "tail") return 15;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo0 = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo1 = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo2 = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo3 = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo4 = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo5 = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo6 = v; break;
      case 7: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo7 = v; break;
      case 8: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo8 = v; break;
      case 9: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_density = v; break;
      case 10: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_reg = v; break;
      case 11: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_mood = v; break;
      case 12: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_glide = v; break;
      case 13: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dark = v; break;
      case 14: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_swell = v; break;
      case 15: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_tail = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_halo0;
      case 1: return P_halo1;
      case 2: return P_halo2;
      case 3: return P_halo3;
      case 4: return P_halo4;
      case 5: return P_halo5;
      case 6: return P_halo6;
      case 7: return P_halo7;
      case 8: return P_halo8;
      case 9: return P_density;
      case 10: return P_reg;
      case 11: return P_mood;
      case 12: return P_glide;
      case 13: return P_dark;
      case 14: return P_swell;
      case 15: return P_tail;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 16;
  double* data(const std::string& n) {
    if (n == "ring") return D_ring;
    if (n == "xin") return D_xin;
    if (n == "pin") return D_pin;
    if (n == "amt") return D_amt;
    if (n == "sst") return D_sst;
    if (n == "vg") return D_vg;
    if (n == "va") return D_va;
    if (n == "vt") return D_vt;
    if (n == "vs") return D_vs;
    if (n == "vo") return D_vo;
    if (n == "vq") return D_vq;
    if (n == "vf") return D_vf;
    if (n == "von") return D_von;
    if (n == "vph") return D_vph;
    if (n == "vw") return D_vw;
    if (n == "vpl") return D_vpl;
    if (n == "vpn") return D_vpn;
    if (n == "hist") return D_hist;
    if (n == "mo") return D_mo;
    if (n == "rpM") return D_rpM;
    if (n == "rpm") return D_rpm;
    if (n == "scl") return D_scl;
    if (n == "prank") return D_prank;
    if (n == "cp") return D_cp;
    if (n == "cs") return D_cs;
    if (n == "cu") return D_cu;
    if (n == "tp") return D_tp;
    if (n == "tu") return D_tu;
    if (n == "vm") return D_vm;
    if (n == "pcu") return D_pcu;
    if (n == "lk") return D_lk;
    if (n == "cld") return D_cld;
    if (n == "tgp") return D_tgp;
    if (n == "tga") return D_tga;
    if (n == "tgl") return D_tgl;
    if (n == "tgL") return D_tgL;
    if (n == "tgR") return D_tgR;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double IN4 = in[3] ? (double) in[3][t] : 0.0;
      double IN5 = in[4] ? (double) in[4][t] : 0.0;
      double IN6 = in[5] ? (double) in[5][t] : 0.0;
      double IN7 = in[6] ? (double) in[6][t] : 0.0;
      double IN8 = in[7] ? (double) in[7][t] : 0.0;
      double IN9 = in[8] ? (double) in[8][t] : 0.0;
      double IN10 = in[9] ? (double) in[9][t] : 0.0;
      double IN11 = in[10] ? (double) in[10][t] : 0.0;
      double IN12 = in[11] ? (double) in[11][t] : 0.0;
      double IN13 = in[12] ? (double) in[12][t] : 0.0;
      double IN14 = in[13] ? (double) in[13][t] : 0.0;
      double IN15 = in[14] ? (double) in[14][t] : 0.0;
      double IN16 = in[15] ? (double) in[15][t] : 0.0;
      double IN17 = in[16] ? (double) in[16][t] : 0.0;
      double IN18 = in[17] ? (double) in[17][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double OUT6 = 0.0;
      double OUT7 = 0.0;
      double v_v = 0.0;
      double v_pn = 0.0;
      double v_s = 0.0;
      double v_h_used = 0.0;
      double v_nring = 0.0;
      double v_wi = 0.0;
      double v_dn = 0.0;
      double v_rg = 0.0;
      double v_md = 0.0;
      double v_upr = 0.0;
      double v_dnr = 0.0;
      double v_relr = 0.0;
      double v_gtime = 0.0;
      double v_gfar = 0.0;
      double v_dkd = 0.0;
      double v_lph = 0.0;
      double v_brn = 0.0;
      double v_nact = 0.0;
      double v_budget = 0.0;
      double v_stable = 0.0;
      double v_outl = 0.0;
      double v_outr = 0.0;
      double v_tvoices = 0.0;
      double v_lnst = 0.0;
      double v_am = 0.0;
      double v_alive = 0.0;
      double v_p = 0.0;
      double v_held = 0.0;
      double v_trig = 0.0;
      double v_nq = 0.0;
      double v_cnt = 0.0;
      double v_dt = 0.0;
      double v_dk = 0.0;
      double v_h = 0.0;
      double v_hpc = 0.0;
      double v_k = 0.0;
      double v_tot = 0.0;
      double v_key = 0.0;
      double v_bsc = 0.0;
      double v_sm = 0.0;
      double v_j = 0.0;
      double v_inn = 0.0;
      double v_ext = 0.0;
      double v_prev = 0.0;
      double v_brt = 0.0;
      double v_bth = 0.0;
      double v_bfi = 0.0;
      double v_bse = 0.0;
      double v_bnn = 0.0;
      double v_bex = 0.0;
      double v_binfo = 0.0;
      double v_c = 0.0;
      double v_r = 0.0;
      double v_bor = 0.0;
      double v_rt = 0.0;
      double v_th = 0.0;
      double v_fi = 0.0;
      double v_se = 0.0;
      double v_nn = 0.0;
      double v_el = 0.0;
      double v_tr = 0.0;
      double v_ok = 0.0;
      double v_q = 0.0;
      double v_iv = 0.0;
      double v_ro = 0.0;
      double v_pr = 0.0;
      double v_sc = 0.0;
      double v_ct0 = 0.0;
      double v_ct1 = 0.0;
      double v_ct2 = 0.0;
      double v_ct3 = 0.0;
      double v_ct4 = 0.0;
      double v_ct5 = 0.0;
      double v_mk = 0.0;
      double v_bit = 0.0;
      double v_uu = 0.0;
      double v_wd = 0.0;
      double v_lo = 0.0;
      double v_hi = 0.0;
      double v_foc = 0.0;
      double v_zone = 0.0;
      double v_tz = 0.0;
      double v_nv = 0.0;
      double v_ncls = 0.0;
      double v_minsp = 0.0;
      double v_stp = 0.0;
      double v_wrk = 0.0;
      double v_wds = 0.0;
      double v_rkmax = 0.0;
      double v_hw = 0.0;
      double v_nc = 0.0;
      double v_pp = 0.0;
      double v_ip = 0.0;
      double v_rk = 0.0;
      double v_dmin = 0.0;
      double v_nch = 0.0;
      double v_bi = 0.0;
      double v_tq = 0.0;
      double v_need = 0.0;
      double v_it = 0.0;
      double v_bd = 0.0;
      double v_bv = 0.0;
      double v_bn = 0.0;
      double v_i = 0.0;
      double v_dd = 0.0;
      double v_det = 0.0;
      double v_nw = 0.0;
      double v_srcon = 0.0;
      double v_nvs = 0.0;
      double v_nrm = 0.0;
      double v_cntal = 0.0;
      double v_hh = 0.0;
      double v_g = 0.0;
      double v_tg = 0.0;
      double v_a0 = 0.0;
      double v_t0 = 0.0;
      double v_sp = 0.0;
      double v_gq = 0.0;
      double v_wn = 0.0;
      double v_ph = 0.0;
      double v_ph2 = 0.0;
      double v_cw = 0.0;
      double v_w1 = 0.0;
      double v_w2 = 0.0;
      double v_pos = 0.0;
      double v_i0 = 0.0;
      double v_fr = 0.0;
      double v_a = 0.0;
      double v_b = 0.0;
      double v_y1 = 0.0;
      double v_y2 = 0.0;
      double v_y = 0.0;
      double v_tam = 0.0;
      double v_ctl = 0.0;
      double v_ctr = 0.0;
      double v_cmo = 0.0;
      double v_tsec = 0.0;
      double v_fb = 0.0;
      double v_glen = 0.0;
      double v_tk = 0.0;
      double v_bg = 0.0;
      double v_back = 0.0;
      double v_ag = 0.0;
      double v_gl2 = 0.0;
      double v_wv = 0.0;
      double v_lpv = 0.0;
      double v_hpv = 0.0;
      double v_bl = 0.0;
      double v_fc = 0.0;
      double v_fg = 0.0;
      double v_fk = 0.0;
      double v_fa = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_v1 = 0.0;
      double v_v2 = 0.0;
      double v_v1r = 0.0;
      double v_v2r = 0.0;
      if (((double)((H_h_init) == ((0.0)))) != 0.0) {
        g_poke(D_mo,LEN_mo,CH_mo,(0.0),(0.0),0);
        g_poke(D_mo,LEN_mo,CH_mo,(2.0),(1.0),0);
        g_poke(D_mo,LEN_mo,CH_mo,(4.0),(2.0),0);
        g_poke(D_mo,LEN_mo,CH_mo,(5.0),(3.0),0);
        g_poke(D_mo,LEN_mo,CH_mo,(7.0),(4.0),0);
        g_poke(D_mo,LEN_mo,CH_mo,(9.0),(5.0),0);
        g_poke(D_mo,LEN_mo,CH_mo,(11.0),(6.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(1.0),(0.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(0.8),(1.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(0.55),(2.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(0.9),(3.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(0.75),(4.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(0.85),(5.0),0);
        g_poke(D_rpM,LEN_rpM,CH_rpM,(0.1),(6.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(0.7),(0.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(0.9),(1.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(0.55),(2.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(0.9),(3.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(0.75),(4.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(1.0),(5.0),0);
        g_poke(D_rpm,LEN_rpm,CH_rpm,(0.1),(6.0),0);
        for (v_v = (0.0); ((double)((v_v) < ((32.0)))) != 0.0; v_v = v_v + ((1.0))) {
          v_pn = ((0.5) + (((fmod(v_v,(2.0)) * (2.0)) - (1.0)) * fmin((0.45),((0.08) + (v_v * (0.02))))));
          g_poke(D_vpl,LEN_vpl,CH_vpl,cos(((v_pn * M_PI) * (0.5))),v_v,0);
          g_poke(D_vpn,LEN_vpn,CH_vpn,sin(((v_pn * M_PI) * (0.5))),v_v,0);
          for (v_s = (0.0); ((double)((v_s) < ((9.0)))) != 0.0; v_s = v_s + ((1.0))) {
            g_poke(D_vw,LEN_vw,CH_vw,(((70.0))*SR/1000.0),v_v,v_s);
            g_poke(D_vph,LEN_vph,CH_vph,(v_v / (32.0)),v_v,v_s);
          }
        }
        for (v_s = (0.0); ((double)((v_s) < ((9.0)))) != 0.0; v_s = v_s + ((1.0))) {
          g_poke(D_sst,LEN_sst,CH_sst,(-(1.0)),(0.0),v_s);
          g_poke(D_sst,LEN_sst,CH_sst,(-(1.0)),(1.0),v_s);
          g_poke(D_sst,LEN_sst,CH_sst,(-(1.0)),(5.0),v_s);
          g_poke(D_lk,LEN_lk,CH_lk,(-(1.0)),v_s,0);
        }
        H_h_init = (1.0);
      }
      g_poke(D_xin,LEN_xin,CH_xin,IN1,(0.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN2,(1.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN3,(2.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN4,(3.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN5,(4.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN6,(5.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN7,(6.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN8,(7.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN9,(8.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN10,(0.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN11,(1.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN12,(2.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN13,(3.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN14,(4.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN15,(5.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN16,(6.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN17,(7.0),0);
      g_poke(D_pin,LEN_pin,CH_pin,IN18,(8.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo0 / (100.0)),(0.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo1 / (100.0)),(1.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo2 / (100.0)),(2.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo3 / (100.0)),(3.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo4 / (100.0)),(4.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo5 / (100.0)),(5.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo6 / (100.0)),(6.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo7 / (100.0)),(7.0),0);
      g_poke(D_amt,LEN_amt,CH_amt,(P_halo8 / (100.0)),(8.0),0);
      v_h_used = ((double)((((double)((((double)((((double)((((double)((((double)((((double)((((double)((((double)((P_halo0) > ((0.0))))) || (((double)((P_halo1) > ((0.0)))))))) || (((double)((P_halo2) > ((0.0)))))))) || (((double)((P_halo3) > ((0.0)))))))) || (((double)((P_halo4) > ((0.0)))))))) || (((double)((P_halo5) > ((0.0)))))))) || (((double)((P_halo6) > ((0.0)))))))) || (((double)((P_halo7) > ((0.0)))))))) || (((double)((P_halo8) > ((0.0)))))));
      v_nring = (96000.0);
      v_wi = H_h_wi;
      for (v_s = (0.0); ((double)((v_s) < ((9.0)))) != 0.0; v_s = v_s + ((1.0))) {
        g_poke(D_ring,LEN_ring,CH_ring,g_peek(D_xin,LEN_xin,CH_xin,v_s,0),v_wi,v_s);
      }
      v_dn = (P_density / (100.0));
      v_rg = (P_reg / (100.0));
      v_md = (P_mood / (100.0));
      v_upr = ((1.0) / ((((40.0) + ((P_swell / (100.0)) * (4000.0))))*SR/1000.0));
      v_dnr = ((1.0) / (((15.0))*SR/1000.0));
      v_relr = ((1.0) / (((300.0))*SR/1000.0));
      v_gtime = ((((4000.0) * pow((P_glide / (100.0)),(1.6))))*SR/1000.0);
      v_gfar = ((7.0) + ((P_glide / (100.0)) * (5.0)));
      v_dkd = ((1.0) - (((0.55) * P_glide) / (100.0)));
      v_lph = g_wrap((H_h_lph + ((0.17) / SR)),(0.0),(1.0));
      H_h_lph = v_lph;
      v_brn = (0.0);
      v_nact = (0.0);
      for (v_s = (0.0); ((double)((v_s) < ((9.0)))) != 0.0; v_s = v_s + ((1.0))) {
        if (((double)((g_peek(D_amt,LEN_amt,CH_amt,v_s,0)) > ((0.005)))) != 0.0) {
          v_nact = (v_nact + (1.0));
        }
      }
      v_budget = fmin((32.0),floor(((48.0) / fmax((1.0),v_nact))));
      v_stable = (((50.0))*SR/1000.0);
      v_outl = (0.0);
      v_outr = (0.0);
      v_tvoices = (0.0);
      v_lnst = (0.0577623);
      for (v_s = (0.0); ((double)((v_s) < ((9.0)))) != 0.0; v_s = v_s + ((1.0))) {
        v_am = g_peek(D_amt,LEN_amt,CH_amt,v_s,0);
        v_alive = g_peek(D_sst,LEN_sst,CH_sst,(9.0),v_s);
        if (((double)((((double)((v_am) > ((0.005))))) || (((double)((v_alive) > ((0.0))))))) != 0.0) {
          v_p = g_peek(D_pin,LEN_pin,CH_pin,v_s,0);
          v_held = g_peek(D_sst,LEN_sst,CH_sst,(0.0),v_s);
          v_trig = (0.0);
          if (((double)((v_p) > ((20.0)))) != 0.0) {
            v_nq = floor((v_p + (0.5)));
            v_cnt = g_peek(D_sst,LEN_sst,CH_sst,(2.0),v_s);
            if (((double)((v_nq) == (g_peek(D_sst,LEN_sst,CH_sst,(1.0),v_s)))) != 0.0) {
              v_cnt = (v_cnt + (1.0));
            } else {
              g_poke(D_sst,LEN_sst,CH_sst,v_nq,(1.0),v_s);
              v_cnt = (0.0);
            }
            g_poke(D_sst,LEN_sst,CH_sst,v_cnt,(2.0),v_s);
            if (((double)((((double)((v_cnt) > (v_stable)))) && (((double)((v_nq) != (v_held)))))) != 0.0) {
              v_held = v_nq;
              v_trig = (1.0);
            }
          }
          v_dt = (0.0);
          if (((double)((((double)((((double)((v_p) > ((20.0))))) && (((double)((v_held) >= ((0.0)))))))) && (((double)((fabs((v_p - v_held))) > ((0.7))))))) != 0.0) {
            v_dt = (1.0);
          }
          v_dk = g_peek(D_sst,LEN_sst,CH_sst,(6.0),v_s);
          if (((double)((v_dt) > (v_dk))) != 0.0) {
            v_dk = fmin((1.0),(v_dk + ((1.0) / (((30.0))*SR/1000.0))));
          } else {
            v_dk = fmax((0.0),(v_dk - ((1.0) / (((60.0))*SR/1000.0))));
          }
          g_poke(D_sst,LEN_sst,CH_sst,v_dk,(6.0),v_s);
          if (((double)((((double)((v_held) >= ((0.0))))) && (((double)((v_trig) == ((0.0))))))) != 0.0) {
            if (((double)((((double)((((double)((((double)((fabs((v_dn - g_peek(D_sst,LEN_sst,CH_sst,(3.0),v_s)))) > ((0.015))))) || (((double)((fabs((v_rg - g_peek(D_sst,LEN_sst,CH_sst,(4.0),v_s)))) > ((0.015)))))))) || (((double)((fabs((v_md - g_peek(D_sst,LEN_sst,CH_sst,(10.0),v_s)))) > ((0.03)))))))) || (((double)((v_budget) != (g_peek(D_sst,LEN_sst,CH_sst,(8.0),v_s))))))) != 0.0) {
              v_trig = (2.0);
            }
          }
          if (((double)((v_am) <= ((0.005)))) != 0.0) {
            v_trig = (0.0);
          }
          if (((double)((v_trig) > ((0.0)))) != 0.0) {
            v_h = v_held;
            g_poke(D_sst,LEN_sst,CH_sst,v_h,(0.0),v_s);
            g_poke(D_sst,LEN_sst,CH_sst,v_dn,(3.0),v_s);
            g_poke(D_sst,LEN_sst,CH_sst,v_rg,(4.0),v_s);
            g_poke(D_sst,LEN_sst,CH_sst,v_budget,(8.0),v_s);
            g_poke(D_sst,LEN_sst,CH_sst,v_md,(10.0),v_s);
            v_hpc = fmod(v_h,(12.0));
            if (((double)((v_trig) == ((1.0)))) != 0.0) {
              v_brn = (1.0);
              for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
                g_poke(D_hist,LEN_hist,CH_hist,(g_peek(D_hist,LEN_hist,CH_hist,v_k,0) * (0.9)),v_k,0);
              }
              g_poke(D_hist,LEN_hist,CH_hist,(g_peek(D_hist,LEN_hist,CH_hist,v_hpc,0) + (1.0)),v_hpc,0);
            }
            v_tot = (0.0);
            for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
              if (((double)((g_peek(D_hist,LEN_hist,CH_hist,v_k,0)) > ((0.2)))) != 0.0) {
                v_tot = (v_tot + (1.0));
              }
            }
            v_key = g_peek(D_lk,LEN_lk,CH_lk,v_s,0);
            if (((double)((v_key) < ((0.0)))) != 0.0) {
              v_key = fmod((v_h + (3.0)),(12.0));
              if (((double)((v_md) > ((0.5)))) != 0.0) {
                v_key = fmod(v_h,(12.0));
              }
            }
            if (((double)((v_tot) >= ((2.0)))) != 0.0) {
              v_bsc = (-(1.0));
              for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
                v_sm = (0.0);
                for (v_j = (0.0); ((double)((v_j) < ((7.0)))) != 0.0; v_j = v_j + ((1.0))) {
                  v_sm = (v_sm + g_peek(D_hist,LEN_hist,CH_hist,fmod((v_k + g_peek(D_mo,LEN_mo,CH_mo,v_j,0)),(12.0)),0));
                }
                v_inn = (0.0);
                for (v_j = (0.0); ((double)((v_j) < ((7.0)))) != 0.0; v_j = v_j + ((1.0))) {
                  if (((double)((fmod((v_k + g_peek(D_mo,LEN_mo,CH_mo,v_j,0)),(12.0))) == (v_hpc))) != 0.0) {
                    v_inn = (1.0);
                  }
                }
                v_sm = (v_sm + (v_inn * (100.0)));
                if (((double)((v_k) == (g_peek(D_lk,LEN_lk,CH_lk,v_s,0)))) != 0.0) {
                  v_sm = (v_sm + (0.5));
                }
                if (((double)((v_sm) > (v_bsc))) != 0.0) {
                  v_bsc = v_sm;
                  v_key = v_k;
                }
              }
            }
            g_poke(D_lk,LEN_lk,CH_lk,v_key,v_s,0);
            if (((double)((v_s) == ((0.0)))) != 0.0) {
              H_h_key = v_key;
            }
            for (v_j = (0.0); ((double)((v_j) < ((7.0)))) != 0.0; v_j = v_j + ((1.0))) {
              g_poke(D_scl,LEN_scl,CH_scl,fmod((v_key + g_peek(D_mo,LEN_mo,CH_mo,v_j,0)),(12.0)),v_j,0);
            }
            v_ext = (fabs((v_md - (0.5))) * (2.0));
            v_prev = g_peek(D_sst,LEN_sst,CH_sst,(5.0),v_s);
            v_bsc = (-(99.0));
            v_brt = v_hpc;
            v_bth = (4.0);
            v_bfi = (7.0);
            v_bse = (11.0);
            v_bnn = (2.0);
            v_bex = (-(1.0));
            v_binfo = (0.0);
            for (v_c = (0.0); ((double)((v_c) < ((14.0)))) != 0.0; v_c = v_c + ((1.0))) {
              v_r = fmod(v_c,(7.0));
              v_bor = floor((v_c / (7.0)));
              v_rt = g_peek(D_scl,LEN_scl,CH_scl,v_r,0);
              v_th = fmod(((g_peek(D_scl,LEN_scl,CH_scl,fmod((v_r + (2.0)),(7.0)),0) - v_rt) + (12.0)),(12.0));
              v_fi = fmod(((g_peek(D_scl,LEN_scl,CH_scl,fmod((v_r + (4.0)),(7.0)),0) - v_rt) + (12.0)),(12.0));
              v_se = fmod(((g_peek(D_scl,LEN_scl,CH_scl,fmod((v_r + (6.0)),(7.0)),0) - v_rt) + (12.0)),(12.0));
              v_nn = fmod(((g_peek(D_scl,LEN_scl,CH_scl,fmod((v_r + (1.0)),(7.0)),0) - v_rt) + (12.0)),(12.0));
              v_el = fmod(((g_peek(D_scl,LEN_scl,CH_scl,fmod((v_r + (3.0)),(7.0)),0) - v_rt) + (12.0)),(12.0));
              v_tr = fmod(((g_peek(D_scl,LEN_scl,CH_scl,fmod((v_r + (5.0)),(7.0)),0) - v_rt) + (12.0)),(12.0));
              v_ok = (1.0);
              if (((double)((v_bor) > ((0.0)))) != 0.0) {
                if (((double)((v_fi) != ((7.0)))) != 0.0) {
                  v_ok = (0.0);
                }
                if (((double)((v_th) == ((4.0)))) != 0.0) {
                  v_th = (3.0);
                  v_se = (10.0);
                  v_nn = (2.0);
                  v_el = (5.0);
                  v_tr = (8.0);
                } else {
                  v_th = (4.0);
                  v_se = (11.0);
                  v_nn = (2.0);
                  v_el = (5.0);
                  v_tr = (9.0);
                }
              }
              v_q = (0.0);
              if (((double)((v_th) == ((4.0)))) != 0.0) {
                v_q = (1.0);
              }
              v_iv = fmod(((v_hpc - v_rt) + (12.0)),(12.0));
              v_ro = (-(1.0));
              if (((double)((v_iv) == ((0.0)))) != 0.0) {
                v_ro = (0.75);
              }
              if (((double)((v_iv) == (v_th))) != 0.0) {
                v_ro = (1.0);
              }
              if (((double)((v_iv) == (v_fi))) != 0.0) {
                v_ro = (0.9);
              }
              if (((double)((v_iv) == (v_se))) != 0.0) {
                v_ro = (0.8);
              }
              if (((double)((v_iv) == (v_nn))) != 0.0) {
                v_ro = (0.95);
              }
              if (((double)((v_iv) == (v_el))) != 0.0) {
                v_ro = (0.5);
                if (((double)((v_q) == ((1.0)))) != 0.0) {
                  v_ro = (0.2);
                }
              }
              if (((double)((v_iv) == (v_tr))) != 0.0) {
                v_ro = (0.6);
                if (((double)((v_tr) == ((8.0)))) != 0.0) {
                  v_ro = (0.25);
                }
              }
              if (((double)((((double)((((double)((((double)((v_bor) > ((0.0))))) && (((double)((v_ro) != ((0.75)))))))) && (((double)((v_ro) != ((1.0)))))))) && (((double)((v_ro) != ((0.9))))))) != 0.0) {
                v_ok = (0.0);
              }
              if (((double)((((double)((v_ro) > ((0.0))))) && (((double)((v_ok) > ((0.0))))))) != 0.0) {
                v_pr = ((g_peek(D_rpM,LEN_rpM,CH_rpM,v_r,0) * v_md) + (g_peek(D_rpm,LEN_rpm,CH_rpm,v_r,0) * ((1.0) - v_md)));
                if (((double)((v_fi) == ((6.0)))) != 0.0) {
                  v_pr = (0.05);
                }
                if (((double)((v_bor) > ((0.0)))) != 0.0) {
                  v_pr = (0.72);
                }
                v_sc = (v_pr * v_ro);
                v_sc = (v_sc + (((0.32) * v_ext) * ((1.0) - fabs((v_q - v_md)))));
                if (((double)((v_bor) > ((0.0)))) != 0.0) {
                  v_sc = ((v_sc - (0.08)) - ((0.7) * v_ext));
                }
                if (((double)((v_rt) == (v_prev))) != 0.0) {
                  v_sc = (v_sc + (0.12));
                }
                if (((double)((((double)((v_prev) >= ((0.0))))) && (((double)((((double)((fmod(((v_rt - v_prev) + (12.0)),(12.0))) == ((5.0))))) || (((double)((fmod(((v_rt - v_prev) + (12.0)),(12.0))) == ((7.0)))))))))) != 0.0) {
                  v_sc = (v_sc + (0.06));
                }
                v_sc = (v_sc + (g_noise() * (0.05)));
                if (((double)((v_sc) > (v_bsc))) != 0.0) {
                  v_bsc = v_sc;
                  v_brt = v_rt;
                  v_bth = v_th;
                  v_bfi = v_fi;
                  v_bse = v_se;
                  v_bnn = v_nn;
                  v_bex = (-(1.0));
                  v_binfo = (((v_rt + ((12.0) * v_q)) + ((24.0) * v_bor)) + ((48.0) * ((double)((v_fi) == ((6.0))))));
                  if (((double)((v_tr) != ((8.0)))) != 0.0) {
                    v_bex = v_tr;
                  }
                  if (((double)((((double)((v_q) == ((0.0))))) && (((double)((v_el) == ((5.0))))))) != 0.0) {
                    v_bex = v_el;
                  }
                }
              }
            }
            v_ct0 = v_brt;
            v_ct1 = fmod((v_brt + v_bfi),(12.0));
            v_ct2 = fmod((v_brt + v_bth),(12.0));
            v_ct3 = fmod((v_brt + v_bse),(12.0));
            v_ct4 = fmod((v_brt + v_bnn),(12.0));
            v_ct5 = (-(1.0));
            if (((double)((v_bex) >= ((0.0)))) != 0.0) {
              v_ct5 = fmod((v_brt + v_bex),(12.0));
            }
            g_poke(D_sst,LEN_sst,CH_sst,v_ct0,(5.0),v_s);
            g_poke(D_sst,LEN_sst,CH_sst,v_binfo,(11.0),v_s);
            for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
              g_poke(D_prank,LEN_prank,CH_prank,(30.0),v_k,0);
            }
            for (v_j = (0.0); ((double)((v_j) < ((7.0)))) != 0.0; v_j = v_j + ((1.0))) {
              g_poke(D_prank,LEN_prank,CH_prank,(20.0),g_peek(D_scl,LEN_scl,CH_scl,v_j,0),0);
            }
            if (((double)((v_ct5) >= ((0.0)))) != 0.0) {
              g_poke(D_prank,LEN_prank,CH_prank,(5.0),v_ct5,0);
            }
            g_poke(D_prank,LEN_prank,CH_prank,(4.0),v_ct4,0);
            g_poke(D_prank,LEN_prank,CH_prank,(3.0),v_ct3,0);
            g_poke(D_prank,LEN_prank,CH_prank,(2.0),v_ct2,0);
            g_poke(D_prank,LEN_prank,CH_prank,(1.0),v_ct1,0);
            g_poke(D_prank,LEN_prank,CH_prank,(0.0),v_ct0,0);
            if (((double)((((double)((v_s) == ((0.0))))) || (((double)((H_h_live) < ((0.0))))))) != 0.0) {
              v_mk = (0.0);
              v_bit = (1.0);
              for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
                if (((double)((g_peek(D_prank,LEN_prank,CH_prank,v_k,0)) <= ((3.0)))) != 0.0) {
                  v_mk = (v_mk + v_bit);
                }
                v_bit = (v_bit * (2.0));
              }
              H_h_mask = v_mk;
            }
            v_uu = fmin((1.0),(v_dn / (0.6)));
            v_wd = ((24.0) + (v_uu * (16.0)));
            v_lo = ((v_h - v_wd) + ((v_wd + (1.0)) * v_rg));
            v_hi = ((v_lo + v_wd) - (1.0));
            v_foc = ((v_lo + v_hi) / (2.0));
            v_lo = fmax((31.0),v_lo);
            v_hi = fmin((98.0),v_hi);
            v_zone = (0.0);
            v_tz = (0.0);
            v_nv = floor((((4.0) + (v_uu * (10.0))) + (0.5)));
            v_ncls = ((4.0) + floor((v_uu * (2.99))));
            v_minsp = (3.0);
            if (((double)((v_uu) > ((0.5)))) != 0.0) {
              v_minsp = (2.0);
            }
            v_stp = (1.0);
            v_wrk = (0.35);
            v_wds = (0.45);
            v_rkmax = (99.0);
            if (((double)((v_dn) > ((0.6)))) != 0.0) {
              v_zone = (1.0);
              v_tz = ((v_dn - (0.6)) / (0.4));
              v_nv = floor((((14.0) + (v_tz * (18.0))) + (0.5)));
              v_hw = ((14.5) - (v_tz * (7.0)));
              v_lo = fmax(v_lo,(v_foc - v_hw));
              v_hi = fmin(v_hi,(v_foc + v_hw));
              v_rkmax = (25.0);
              if (((double)((v_tz) >= ((0.35)))) != 0.0) {
                v_rkmax = (99.0);
              }
              v_minsp = (1.0);
              if (((double)((v_tz) > ((0.7)))) != 0.0) {
                v_stp = (0.5);
                v_minsp = (0.5);
              }
              v_wrk = (0.02);
              v_wds = (1.0);
            }
            v_nv = fmin(v_nv,v_budget);
            v_nc = (0.0);
            for (v_pp = ceil(v_lo); ((double)((v_pp) <= (v_hi))) != 0.0; v_pp = v_pp + (v_stp)) {
              v_ip = floor(v_pp);
              v_rk = g_peek(D_prank,LEN_prank,CH_prank,fmod(v_ip,(12.0)),0);
              if (((double)(((v_pp - v_ip)) > ((0.1)))) != 0.0) {
                v_rk = (40.0);
              }
              v_ok = (1.0);
              if (((double)((((double)((v_zone) == ((0.0))))) && (((double)((v_rk) >= (v_ncls)))))) != 0.0) {
                v_ok = (0.0);
              }
              if (((double)((((double)((((double)((v_zone) == ((1.0))))) && (((double)((v_rk) > (v_rkmax))))))) && (((double)((v_rk) < ((40.0))))))) != 0.0) {
                v_ok = (0.0);
              }
              if (((double)((fabs((v_pp - v_h))) < ((0.4)))) != 0.0) {
                v_ok = (0.0);
              }
              if (((double)((((double)((v_zone) == ((0.0))))) && (((double)((fabs((v_pp - v_h))) < ((2.0))))))) != 0.0) {
                v_ok = (0.0);
              }
              if (((double)((((double)((((double)((v_zone) == ((0.0))))) && (((double)((v_pp) < ((40.0)))))))) && (((double)((v_rk) > ((1.0))))))) != 0.0) {
                v_ok = (0.0);
              }
              if (((double)((((double)((v_ok) > ((0.0))))) && (((double)((v_nc) < ((160.0))))))) != 0.0) {
                v_sc = (((fmin(v_rk,(40.0)) * v_wrk) + ((fabs((v_pp - v_foc)) / (12.0)) * v_wds)) + (g_noise() * (0.02)));
                if (((double)((v_zone) == ((0.0)))) != 0.0) {
                  v_dmin = (99.0);
                  for (v_v = (0.0); ((double)((v_v) < ((32.0)))) != 0.0; v_v = v_v + ((1.0))) {
                    if (((double)((g_peek(D_von,LEN_von,CH_von,v_v,v_s)) > ((0.0)))) != 0.0) {
                      v_dmin = fmin(v_dmin,fabs((g_peek(D_vt,LEN_vt,CH_vt,v_v,v_s) - v_pp)));
                    }
                  }
                  if (((double)((v_dmin) < ((99.0)))) != 0.0) {
                    v_sc = (v_sc + (fmin(v_dmin,(4.0)) * (0.07)));
                  }
                }
                g_poke(D_cp,LEN_cp,CH_cp,v_pp,v_nc,0);
                g_poke(D_cs,LEN_cs,CH_cs,v_sc,v_nc,0);
                g_poke(D_cu,LEN_cu,CH_cu,(0.0),v_nc,0);
                v_nc = (v_nc + (1.0));
              }
            }
            for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
              g_poke(D_pcu,LEN_pcu,CH_pcu,(0.0),v_k,0);
            }
            v_nch = (0.0);
            for (v_v = (0.0); ((double)((v_v) < (v_nv))) != 0.0; v_v = v_v + ((1.0))) {
              if (((double)((v_nch) == (v_v))) != 0.0) {
                v_bi = (-(1.0));
                v_bsc = (99999.0);
                for (v_c = (0.0); ((double)((v_c) < (v_nc))) != 0.0; v_c = v_c + ((1.0))) {
                  if (((double)((g_peek(D_cu,LEN_cu,CH_cu,v_c,0)) == ((0.0)))) != 0.0) {
                    v_pp = g_peek(D_cp,LEN_cp,CH_cp,v_c,0);
                    v_ok = (1.0);
                    for (v_q = (0.0); ((double)((v_q) < (v_nch))) != 0.0; v_q = v_q + ((1.0))) {
                      v_tq = g_peek(D_tp,LEN_tp,CH_tp,v_q,0);
                      v_need = v_minsp;
                      if (((double)((((double)((v_zone) == ((0.0))))) && (((double)((fmin(v_pp,v_tq)) < ((52.0))))))) != 0.0) {
                        v_need = fmax(v_need,(3.0));
                      }
                      if (((double)((((double)((v_zone) == ((0.0))))) && (((double)((fmin(v_pp,v_tq)) < ((40.0))))))) != 0.0) {
                        v_need = (5.0);
                      }
                      if (((double)((fabs((v_pp - v_tq))) < ((v_need - (0.01))))) != 0.0) {
                        v_ok = (0.0);
                      }
                    }
                    if (((double)((v_ok) > ((0.0)))) != 0.0) {
                      v_sc = g_peek(D_cs,LEN_cs,CH_cs,v_c,0);
                      if (((double)((v_zone) == ((0.0)))) != 0.0) {
                        v_sc = (v_sc + (g_peek(D_pcu,LEN_pcu,CH_pcu,fmod(floor(v_pp),(12.0)),0) * (1.2)));
                      }
                      if (((double)((v_sc) < (v_bsc))) != 0.0) {
                        v_bsc = v_sc;
                        v_bi = v_c;
                      }
                    }
                  }
                }
                if (((double)((v_bi) >= ((0.0)))) != 0.0) {
                  v_pp = g_peek(D_cp,LEN_cp,CH_cp,v_bi,0);
                  g_poke(D_tp,LEN_tp,CH_tp,v_pp,v_nch,0);
                  g_poke(D_tu,LEN_tu,CH_tu,(0.0),v_nch,0);
                  g_poke(D_cu,LEN_cu,CH_cu,(1.0),v_bi,0);
                  g_poke(D_pcu,LEN_pcu,CH_pcu,(g_peek(D_pcu,LEN_pcu,CH_pcu,fmod(floor(v_pp),(12.0)),0) + (1.0)),fmod(floor(v_pp),(12.0)),0);
                  v_nch = (v_nch + (1.0));
                }
              }
            }
            g_poke(D_sst,LEN_sst,CH_sst,v_nch,(7.0),v_s);
            for (v_v = (0.0); ((double)((v_v) < ((32.0)))) != 0.0; v_v = v_v + ((1.0))) {
              g_poke(D_vm,LEN_vm,CH_vm,(0.0),v_v,0);
            }
            for (v_it = (0.0); ((double)((v_it) < (v_nch))) != 0.0; v_it = v_it + ((1.0))) {
              v_bd = (99.0);
              v_bv = (-(1.0));
              v_bn = (-(1.0));
              for (v_i = (0.0); ((double)((v_i) < (v_nch))) != 0.0; v_i = v_i + ((1.0))) {
                if (((double)((g_peek(D_tu,LEN_tu,CH_tu,v_i,0)) == ((0.0)))) != 0.0) {
                  v_tq = g_peek(D_tp,LEN_tp,CH_tp,v_i,0);
                  for (v_v = (0.0); ((double)((v_v) < ((32.0)))) != 0.0; v_v = v_v + ((1.0))) {
                    if (((double)((((double)((g_peek(D_vm,LEN_vm,CH_vm,v_v,0)) == ((0.0))))) && (((double)((g_peek(D_von,LEN_von,CH_von,v_v,v_s)) > ((0.0))))))) != 0.0) {
                      v_dd = fabs((g_peek(D_vt,LEN_vt,CH_vt,v_v,v_s) - v_tq));
                      if (((double)((v_dd) < (v_bd))) != 0.0) {
                        v_bd = v_dd;
                        v_bv = v_v;
                        v_bn = v_i;
                      }
                    }
                  }
                }
              }
              if (((double)((((double)((v_bv) >= ((0.0))))) && (((double)((v_bd) <= (v_gfar)))))) != 0.0) {
                v_tq = g_peek(D_tp,LEN_tp,CH_tp,v_bn,0);
                v_det = ((fmod((v_bv * (7.0)),(5.0)) - (2.0)) / (100.0));
                if (((double)((v_zone) == ((1.0)))) != 0.0) {
                  v_det = (g_noise() * ((0.04) + (v_tz * (0.22))));
                }
                if (((double)((fabs((g_peek(D_vt,LEN_vt,CH_vt,v_bv,v_s) - v_tq))) < ((0.3)))) != 0.0) {
                  v_det = (g_peek(D_vt,LEN_vt,CH_vt,v_bv,v_s) - v_tq);
                }
                g_poke(D_vt,LEN_vt,CH_vt,(v_tq + v_det),v_bv,v_s);
                if (((double)((((double)((fabs((g_peek(D_vt,LEN_vt,CH_vt,v_bv,v_s) - (v_tq + v_det)))) > ((0.001))))) || (((double)((g_peek(D_vq,LEN_vq,CH_vq,v_bv,v_s)) >= ((1.0))))))) != 0.0) {
                  g_poke(D_vo,LEN_vo,CH_vo,g_peek(D_va,LEN_va,CH_va,v_bv,v_s),v_bv,v_s);
                  g_poke(D_vq,LEN_vq,CH_vq,(0.0),v_bv,v_s);
                  g_poke(D_vs,LEN_vs,CH_vs,((1.0) / fmax((1.0),(v_gtime * ((0.8) + (((0.4) * fmod((v_bv * (5.0)),(7.0))) / (6.0)))))),v_bv,v_s);
                }
                g_poke(D_vm,LEN_vm,CH_vm,(1.0),v_bv,0);
                g_poke(D_tu,LEN_tu,CH_tu,(1.0),v_bn,0);
              }
            }
            for (v_i = (0.0); ((double)((v_i) < (v_nch))) != 0.0; v_i = v_i + ((1.0))) {
              if (((double)((g_peek(D_tu,LEN_tu,CH_tu,v_i,0)) == ((0.0)))) != 0.0) {
                v_tq = g_peek(D_tp,LEN_tp,CH_tp,v_i,0);
                v_bv = (-(1.0));
                for (v_v = (31.0); ((double)((v_v) >= ((0.0)))) != 0.0; v_v = v_v - ((1.0))) {
                  if (((double)((((double)((((double)((g_peek(D_vm,LEN_vm,CH_vm,v_v,0)) == ((0.0))))) && (((double)((g_peek(D_von,LEN_von,CH_von,v_v,v_s)) == ((0.0)))))))) && (((double)((g_peek(D_vg,LEN_vg,CH_vg,v_v,v_s)) == ((0.0))))))) != 0.0) {
                    v_bv = v_v;
                  }
                }
                if (((double)((v_bv) < ((0.0)))) != 0.0) {
                  for (v_v = (31.0); ((double)((v_v) >= ((0.0)))) != 0.0; v_v = v_v - ((1.0))) {
                    if (((double)((((double)((g_peek(D_vm,LEN_vm,CH_vm,v_v,0)) == ((0.0))))) && (((double)((g_peek(D_von,LEN_von,CH_von,v_v,v_s)) == ((0.0))))))) != 0.0) {
                      v_bv = v_v;
                    }
                  }
                }
                if (((double)((v_bv) >= ((0.0)))) != 0.0) {
                  v_det = ((fmod((v_bv * (7.0)),(5.0)) - (2.0)) / (100.0));
                  if (((double)((v_zone) == ((1.0)))) != 0.0) {
                    v_det = (g_noise() * ((0.04) + (v_tz * (0.22))));
                  }
                  g_poke(D_vt,LEN_vt,CH_vt,(v_tq + v_det),v_bv,v_s);
                  g_poke(D_vs,LEN_vs,CH_vs,(0.0),v_bv,v_s);
                  g_poke(D_vq,LEN_vq,CH_vq,(1.0),v_bv,v_s);
                  if (((double)((g_peek(D_vg,LEN_vg,CH_vg,v_bv,v_s)) > ((0.0)))) != 0.0) {
                    g_poke(D_vf,LEN_vf,CH_vf,(1.0),v_bv,v_s);
                  } else {
                    g_poke(D_va,LEN_va,CH_va,(v_tq + v_det),v_bv,v_s);
                  }
                  v_nw = ((((60.0) + (fmod(v_bv,(5.0)) * (8.0))))*SR/1000.0);
                  if (((double)(((v_tq - v_h)) < ((-(8.0))))) != 0.0) {
                    v_nw = (((105.0))*SR/1000.0);
                  }
                  g_poke(D_vw,LEN_vw,CH_vw,v_nw,v_bv,v_s);
                  g_poke(D_vm,LEN_vm,CH_vm,(1.0),v_bv,0);
                  g_poke(D_tu,LEN_tu,CH_tu,(1.0),v_i,0);
                }
              }
            }
            for (v_v = (0.0); ((double)((v_v) < ((32.0)))) != 0.0; v_v = v_v + ((1.0))) {
              g_poke(D_von,LEN_von,CH_von,g_peek(D_vm,LEN_vm,CH_vm,v_v,0),v_v,v_s);
            }
          }
          if (((double)((v_s) == ((0.0)))) != 0.0) {
            H_h_live = v_held;
          }
          v_srcon = (0.0);
          if (((double)((v_am) > ((0.005)))) != 0.0) {
            v_srcon = ((1.0) - (v_dk * v_dkd));
          }
          v_nvs = fmax((1.0),g_peek(D_sst,LEN_sst,CH_sst,(7.0),v_s));
          v_nrm = ((v_am * (1.3)) / sqrt(fmax(0.0,v_nvs)));
          v_cntal = (0.0);
          v_hh = g_peek(D_sst,LEN_sst,CH_sst,(0.0),v_s);
          for (v_v = (0.0); ((double)((v_v) < ((32.0)))) != 0.0; v_v = v_v + ((1.0))) {
            v_g = g_peek(D_vg,LEN_vg,CH_vg,v_v,v_s);
            v_tg = (g_peek(D_von,LEN_von,CH_von,v_v,v_s) * v_srcon);
            if (((double)((g_peek(D_vf,LEN_vf,CH_vf,v_v,v_s)) > ((0.0)))) != 0.0) {
              v_g = (v_g - v_dnr);
              if (((double)((v_g) <= ((0.0)))) != 0.0) {
                v_g = (0.0);
                g_poke(D_va,LEN_va,CH_va,g_peek(D_vt,LEN_vt,CH_vt,v_v,v_s),v_v,v_s);
                g_poke(D_vf,LEN_vf,CH_vf,(0.0),v_v,v_s);
              }
            } else {
              if (((double)((v_g) < (v_tg))) != 0.0) {
                v_g = fmin(v_tg,(v_g + v_upr));
              } else {
                v_g = fmax(v_tg,(v_g - v_relr));
              }
            }
            g_poke(D_vg,LEN_vg,CH_vg,v_g,v_v,v_s);
            if (((double)((v_g) > ((0.0)))) != 0.0) {
              v_cntal = (v_cntal + (1.0));
              v_a0 = g_peek(D_va,LEN_va,CH_va,v_v,v_s);
              v_t0 = g_peek(D_vt,LEN_vt,CH_vt,v_v,v_s);
              if (((double)((v_a0) != (v_t0))) != 0.0) {
                v_sp = g_peek(D_vs,LEN_vs,CH_vs,v_v,v_s);
                v_gq = (1.0);
                if (((double)((v_sp) > ((0.0)))) != 0.0) {
                  v_gq = fmin((1.0),(g_peek(D_vq,LEN_vq,CH_vq,v_v,v_s) + v_sp));
                }
                g_poke(D_vq,LEN_vq,CH_vq,v_gq,v_v,v_s);
                v_a0 = (g_peek(D_vo,LEN_vo,CH_vo,v_v,v_s) + ((((v_t0 - g_peek(D_vo,LEN_vo,CH_vo,v_v,v_s)) * v_gq) * v_gq) * ((3.0) - ((2.0) * v_gq))));
                if (((double)((v_gq) >= ((1.0)))) != 0.0) {
                  v_a0 = v_t0;
                }
                g_poke(D_va,LEN_va,CH_va,v_a0,v_v,v_s);
              }
              v_a0 = (v_a0 + ((0.045) * sin(((2.0*M_PI) * g_wrap(((v_lph * ((1.0) + (v_v * (0.137)))) + (v_v * (0.31))),(0.0),(1.0))))));
              v_r = exp(((v_a0 - v_hh) * v_lnst));
              v_wn = g_peek(D_vw,LEN_vw,CH_vw,v_v,v_s);
              v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,v_v,v_s) + (((1.0) - v_r) / v_wn)),(0.0),(1.0));
              g_poke(D_vph,LEN_vph,CH_vph,v_ph,v_v,v_s);
              v_ph2 = g_wrap((v_ph + (0.5)),(0.0),(1.0));
              v_cw = cos(((2.0*M_PI) * v_ph));
              v_w1 = (((1.0) - v_cw) * (0.5));
              v_w2 = (((1.0) + v_cw) * (0.5));
              v_pos = g_wrap((v_wi - ((v_ph * v_wn) + (2.0))),(0.0),v_nring);
              v_i0 = floor(v_pos);
              v_fr = (v_pos - v_i0);
              v_a = g_peek(D_ring,LEN_ring,CH_ring,v_i0,v_s);
              v_b = g_peek(D_ring,LEN_ring,CH_ring,g_wrap((v_i0 + (1.0)),(0.0),v_nring),v_s);
              v_y1 = (v_a + ((v_b - v_a) * v_fr));
              v_pos = g_wrap((v_wi - ((v_ph2 * v_wn) + (2.0))),(0.0),v_nring);
              v_i0 = floor(v_pos);
              v_fr = (v_pos - v_i0);
              v_a = g_peek(D_ring,LEN_ring,CH_ring,v_i0,v_s);
              v_b = g_peek(D_ring,LEN_ring,CH_ring,g_wrap((v_i0 + (1.0)),(0.0),v_nring),v_s);
              v_y2 = (v_a + ((v_b - v_a) * v_fr));
              v_y = ((((v_y1 * v_w1) + (v_y2 * v_w2)) * v_g) * v_nrm);
              v_outl = (v_outl + (v_y * g_peek(D_vpl,LEN_vpl,CH_vpl,v_v,0)));
              v_outr = (v_outr + (v_y * g_peek(D_vpn,LEN_vpn,CH_vpn,v_v,0)));
            }
          }
          g_poke(D_sst,LEN_sst,CH_sst,v_cntal,(9.0),v_s);
          v_tvoices = (v_tvoices + v_cntal);
        }
      }
      v_tam = (H_t_amt + (((P_tail / (100.0)) - H_t_amt) * (0.0005)));
      H_t_amt = v_tam;
      v_ctl = (0.0);
      v_ctr = (0.0);
      v_cmo = (0.0);
      if (((double)((v_tam) > ((0.002)))) != 0.0) {
        v_tsec = ((0.2) + ((v_tam * v_tam) * (6.0)));
        v_fb = pow((10.0),(((-(3.0)) * (0.55)) / v_tsec));
        v_glen = (((260.0))*SR/1000.0);
        v_tk = (H_t_tick + (1.0));
        if (((double)((v_tk) >= ((v_glen / (4.0))))) != 0.0) {
          v_tk = (0.0);
          v_bg = (0.0);
          for (v_g = (0.0); ((double)((v_g) < ((8.0)))) != 0.0; v_g = v_g + ((1.0))) {
            if (((double)((g_peek(D_tga,LEN_tga,CH_tga,v_g,0)) >= (g_peek(D_tgl,LEN_tgl,CH_tgl,v_g,0)))) != 0.0) {
              v_bg = v_g;
            }
          }
          v_back = ((((200.0))*SR/1000.0) + (((0.5) + ((0.5) * g_noise())) * (((700.0))*SR/1000.0)));
          g_poke(D_tgp,LEN_tgp,CH_tgp,g_wrap(((v_wi - v_back) - v_glen),(0.0),v_nring),v_bg,0);
          g_poke(D_tga,LEN_tga,CH_tga,(0.0),v_bg,0);
          g_poke(D_tgl,LEN_tgl,CH_tgl,(v_glen * ((0.8) + ((0.4) * ((0.5) + ((0.5) * g_noise()))))),v_bg,0);
          v_pn = ((0.5) + (g_noise() * (0.45)));
          g_poke(D_tgL,LEN_tgL,CH_tgL,cos(((v_pn * M_PI) * (0.5))),v_bg,0);
          g_poke(D_tgR,LEN_tgR,CH_tgR,sin(((v_pn * M_PI) * (0.5))),v_bg,0);
        }
        H_t_tick = v_tk;
        for (v_g = (0.0); ((double)((v_g) < ((8.0)))) != 0.0; v_g = v_g + ((1.0))) {
          v_ag = g_peek(D_tga,LEN_tga,CH_tga,v_g,0);
          v_gl2 = g_peek(D_tgl,LEN_tgl,CH_tgl,v_g,0);
          if (((double)((v_ag) < (v_gl2))) != 0.0) {
            v_pos = g_wrap((g_peek(D_tgp,LEN_tgp,CH_tgp,v_g,0) + v_ag),(0.0),v_nring);
            v_wv = sin(((M_PI * v_ag) / v_gl2));
            v_v = ((g_peek(D_cld,LEN_cld,CH_cld,floor(v_pos),0) * v_wv) * v_wv);
            v_ctl = (v_ctl + (v_v * g_peek(D_tgL,LEN_tgL,CH_tgL,v_g,0)));
            v_ctr = (v_ctr + (v_v * g_peek(D_tgR,LEN_tgR,CH_tgR,v_g,0)));
            v_cmo = (v_cmo + v_v);
            g_poke(D_tga,LEN_tga,CH_tga,(v_ag + (1.0)),v_g,0);
          }
        }
        v_ctl = (v_ctl * (0.62));
        v_ctr = (v_ctr * (0.62));
        v_lpv = (H_t_lp + ((v_cmo - H_t_lp) * (0.6)));
        H_t_lp = v_lpv;
        v_hpv = ((v_lpv - H_t_hx) + ((0.996) * H_t_hp));
        H_t_hx = v_lpv;
        H_t_hp = v_hpv;
        g_poke(D_cld,LEN_cld,CH_cld,(((v_outl + v_outr) * (0.5)) + (tanh(((v_hpv * v_fb) * (1.1))) / (1.1))),v_wi,0);
      } else {
        g_poke(D_cld,LEN_cld,CH_cld,((v_outl + v_outr) * (0.5)),v_wi,0);
      }
      H_h_wi = fmod((v_wi + (1.0)),v_nring);
      v_bl = (H_h_bl * exp(((-(1.0)) / ((((300.0) + ((v_gtime / SR) * (500.0))))*SR/1000.0))));
      if (((double)((v_brn) > ((0.0)))) != 0.0) {
        v_bl = (1.0);
      }
      H_h_bl = v_bl;
      v_fc = fmin((16000.0),(((16000.0) * pow((0.04),(P_dark / (100.0)))) * ((1.0) + (v_bl * (1.5)))));
      v_fg = tan(((M_PI * fmin(v_fc,(SR * (0.45)))) / SR));
      v_fk = (1.2);
      v_fa = ((1.0) / ((1.0) + (v_fg * (v_fg + v_fk))));
      v_yl = (v_outl + (v_ctl * fmin((1.0),(v_tam * (3.0)))));
      v_yr = (v_outr + (v_ctr * fmin((1.0),(v_tam * (3.0)))));
      v_v1 = ((H_h_f1l + (v_fg * (v_yl - H_h_f2l))) * v_fa);
      v_v2 = (H_h_f2l + (v_fg * v_v1));
      H_h_f1l = (((2.0) * v_v1) - H_h_f1l);
      H_h_f2l = (((2.0) * v_v2) - H_h_f2l);
      v_v1r = ((H_h_f1r + (v_fg * (v_yr - H_h_f2r))) * v_fa);
      v_v2r = (H_h_f2r + (v_fg * v_v1r));
      H_h_f1r = (((2.0) * v_v1r) - H_h_f1r);
      H_h_f2r = (((2.0) * v_v2r) - H_h_f2r);
      OUT1 = v_v2;
      OUT2 = v_v2r;
      OUT3 = H_h_live;
      OUT4 = H_h_key;
      OUT5 = v_tvoices;
      OUT6 = H_h_mask;
      OUT7 = v_h_used;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
      if (out[5]) out[5][t] = (float) OUT6;
      if (out[6]) out[6][t] = (float) OUT7;
    }
  }
};
// ---------------------------------------------------------------- Chorus
struct Chorus {
  double SR = 44100.0;
  double P_cho_rate = 30.0;
  double P_cho_depth = 50.0;
  double P_cho_width = 70.0;
  std::vector<double> M_cd; DL DL_cd;
  double H_h_ph = 0.0;
  double H_h_nz = 0.0;
  double H_h_rt = 0.5;
  double H_h_dp = 3.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    M_cd.assign(9600, 0.0); DL_cd.b = M_cd.data(); DL_cd.n = 9600; DL_cd.w = 0;
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "cho_rate") return 0;
    if (n == "cho_depth") return 1;
    if (n == "cho_width") return 2;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho_rate = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho_depth = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho_width = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_cho_rate;
      case 1: return P_cho_depth;
      case 2: return P_cho_width;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 3;
  double* data(const std::string& n) {
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_rt = 0.0;
      double v_dp = 0.0;
      double v_ph = 0.0;
      double v_nz = 0.0;
      double v_m1 = 0.0;
      double v_m2 = 0.0;
      double v_m3 = 0.0;
      double v_v1 = 0.0;
      double v_v2 = 0.0;
      double v_v3 = 0.0;
      double v_wd = 0.0;
      double v_cl = 0.0;
      double v_cr = 0.0;
      dl_write(&DL_cd,IN1);
      v_rt = (H_h_rt + ((((0.05) * pow((60.0),(P_cho_rate / (100.0)))) - H_h_rt) * (0.001)));
      H_h_rt = v_rt;
      v_dp = (H_h_dp + ((((P_cho_depth / (100.0)) * (6.0)) - H_h_dp) * (0.001)));
      H_h_dp = v_dp;
      v_ph = g_wrap((H_h_ph + (v_rt / SR)),(0.0),(1.0));
      H_h_ph = v_ph;
      v_nz = (H_h_nz + ((g_noise() - H_h_nz) * (0.0005)));
      H_h_nz = v_nz;
      v_m1 = sin(((2.0*M_PI) * v_ph));
      v_m2 = sin(((2.0*M_PI) * (v_ph + (0.3333))));
      v_m3 = sin(((2.0*M_PI) * (v_ph + (0.6667))));
      v_v1 = dl_read(&DL_cd,(((((11.0) + (v_dp * ((0.5) + ((0.5) * v_m1)))) + (v_nz * (0.4))))*SR/1000.0));
      v_v2 = dl_read(&DL_cd,(((((16.0) + (v_dp * ((0.5) + ((0.5) * v_m2)))) - (v_nz * (0.3))))*SR/1000.0));
      v_v3 = dl_read(&DL_cd,(((((21.0) + (v_dp * ((0.5) + ((0.5) * v_m3)))) + (v_nz * (0.2))))*SR/1000.0));
      v_wd = (P_cho_width / (100.0));
      v_cl = ((v_v1 + (v_v2 * (0.7))) + ((v_v3 * ((1.0) - v_wd)) * (0.7)));
      v_cr = ((v_v3 + (v_v2 * (0.7))) + ((v_v1 * ((1.0) - v_wd)) * (0.7)));
      OUT1 = (v_cl * (0.55));
      OUT2 = (v_cr * (0.55));
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      DL_cd.w = (DL_cd.w + 1) % DL_cd.n;
    }
  }
};
// ---------------------------------------------------------------- Delay
struct Delay {
  double SR = 44100.0;
  double P_bpm = 90.0;
  double P_prac = 100.0;
  double P_dly_time = 55.0;
  double P_dly_fb = 45.0;
  double P_dly_tone = 55.0;
  double P_dly_wob = 25.0;
  double P_dly_st = 60.0;
  double P_dly_dirt = 25.0;
  std::vector<double> M_dll; DL DL_dll;
  std::vector<double> M_dlr; DL DL_dlr;
  double H_h_t = 24000.0;
  double H_h_w = 0.0;
  double H_h_f = 0.0;
  double H_h_nz = 0.0;
  double H_h_lpl = 0.0;
  double H_h_lpr = 0.0;
  double H_h_hpl = 0.0;
  double H_h_hpr = 0.0;
  double H_h_xl = 0.0;
  double H_h_xr = 0.0;
  double H_h_yl = 0.0;
  double H_h_yr = 0.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    M_dll.assign(420000, 0.0); DL_dll.b = M_dll.data(); DL_dll.n = 420000; DL_dll.w = 0;
    M_dlr.assign(420000, 0.0); DL_dlr.b = M_dlr.data(); DL_dlr.n = 420000; DL_dlr.w = 0;
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "bpm") return 0;
    if (n == "prac") return 1;
    if (n == "dly_time") return 2;
    if (n == "dly_fb") return 3;
    if (n == "dly_tone") return 4;
    if (n == "dly_wob") return 5;
    if (n == "dly_st") return 6;
    if (n == "dly_dirt") return 7;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 30.0) v = 30.0; if (v > 300.0) v = 300.0; P_bpm = v; break;
      case 1: if (v < 50.0) v = 50.0; if (v > 100.0) v = 100.0; P_prac = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_time = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_fb = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_tone = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_wob = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_st = v; break;
      case 7: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_dirt = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_bpm;
      case 1: return P_prac;
      case 2: return P_dly_time;
      case 3: return P_dly_fb;
      case 4: return P_dly_tone;
      case 5: return P_dly_wob;
      case 6: return P_dly_st;
      case 7: return P_dly_dirt;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 8;
  double* data(const std::string& n) {
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_beat = 0.0;
      double v_ti = 0.0;
      double v_mult = 0.0;
      double v_target = 0.0;
      double v_tt = 0.0;
      double v_wamt = 0.0;
      double v_wph = 0.0;
      double v_fph = 0.0;
      double v_nz = 0.0;
      double v_md = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_tn = 0.0;
      double v_lpc = 0.0;
      double v_hpc = 0.0;
      double v_lpl = 0.0;
      double v_lpr = 0.0;
      double v_hl = 0.0;
      double v_hr = 0.0;
      double v_dg = 0.0;
      double v_sl = 0.0;
      double v_sr2 = 0.0;
      double v_fbk = 0.0;
      double v_stw = 0.0;
      double v_fl = 0.0;
      double v_fr = 0.0;
      v_beat = (((60.0) / ((P_bpm * P_prac) / (100.0))) * SR);
      v_ti = floor(((P_dly_time / (100.0)) * (7.99)));
      v_mult = (0.25);
      if (((double)((v_ti) == ((1.0)))) != 0.0) {
        v_mult = ((1.0) / (3.0));
      }
      if (((double)((v_ti) == ((2.0)))) != 0.0) {
        v_mult = (0.5);
      }
      if (((double)((v_ti) == ((3.0)))) != 0.0) {
        v_mult = (0.75);
      }
      if (((double)((v_ti) == ((4.0)))) != 0.0) {
        v_mult = (1.0);
      }
      if (((double)((v_ti) == ((5.0)))) != 0.0) {
        v_mult = (1.5);
      }
      if (((double)((v_ti) == ((6.0)))) != 0.0) {
        v_mult = (2.0);
      }
      if (((double)((v_ti) == ((7.0)))) != 0.0) {
        v_mult = (4.0);
      }
      v_target = fmin((v_beat * v_mult),(400000.0));
      v_tt = (H_h_t + ((v_target - H_h_t) * (0.00015)));
      H_h_t = v_tt;
      v_wamt = (P_dly_wob / (100.0));
      v_wph = g_wrap((H_h_w + ((0.55) / SR)),(0.0),(1.0));
      H_h_w = v_wph;
      v_fph = g_wrap((H_h_f + ((5.7) / SR)),(0.0),(1.0));
      H_h_f = v_fph;
      v_nz = (H_h_nz + ((g_noise() - H_h_nz) * (0.0008)));
      H_h_nz = v_nz;
      v_md = (((v_wamt * ((((1.6) * sin(((2.0*M_PI) * v_wph))) + ((0.25) * sin(((2.0*M_PI) * v_fph)))) + ((1.2) * v_nz))))*SR/1000.0);
      v_yl = dl_read(&DL_dll,((v_tt + v_md) + (2.0)));
      v_yr = dl_read(&DL_dlr,(((v_tt * (1.004)) - v_md) + (2.0)));
      v_tn = (P_dly_tone / (100.0));
      v_lpc = ((1.0) - exp((((-(2.0*M_PI)) * ((900.0) * pow((20.0),v_tn))) / SR)));
      v_hpc = exp((((-(2.0*M_PI)) * ((60.0) + ((240.0) * ((1.0) - v_tn)))) / SR));
      v_lpl = (H_h_lpl + ((v_yl - H_h_lpl) * v_lpc));
      H_h_lpl = v_lpl;
      v_lpr = (H_h_lpr + ((v_yr - H_h_lpr) * v_lpc));
      H_h_lpr = v_lpr;
      v_hl = (v_hpc * ((H_h_hpl + v_lpl) - H_h_xl));
      H_h_hpl = v_hl;
      H_h_xl = v_lpl;
      v_hr = (v_hpc * ((H_h_hpr + v_lpr) - H_h_xr));
      H_h_hpr = v_hr;
      H_h_xr = v_lpr;
      v_dg = ((1.0) + ((P_dly_dirt / (100.0)) * (5.0)));
      v_sl = ((tanh((v_hl * v_dg)) / v_dg) * ((1.0) + ((P_dly_dirt / (100.0)) * (0.6))));
      v_sr2 = ((tanh((v_hr * v_dg)) / v_dg) * ((1.0) + ((P_dly_dirt / (100.0)) * (0.6))));
      v_fbk = ((P_dly_fb / (100.0)) * (1.02));
      v_stw = (P_dly_st / (100.0));
      v_fl = (v_sl + ((v_sr2 - v_sl) * v_stw));
      v_fr = (v_sr2 + ((v_sl - v_sr2) * v_stw));
      dl_write(&DL_dll,(IN1 + (v_fl * v_fbk)));
      dl_write(&DL_dlr,((IN1 * ((1.0) - v_stw)) + (v_fr * v_fbk)));
      OUT1 = v_yl;
      OUT2 = v_yr;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      DL_dll.w = (DL_dll.w + 1) % DL_dll.n;
      DL_dlr.w = (DL_dlr.w + 1) % DL_dlr.n;
    }
  }
};
// ---------------------------------------------------------------- Reverse
struct Reverse {
  double SR = 44100.0;
  double P_bpm = 90.0;
  double P_prac = 100.0;
  double P_rev_time = 55.0;
  double P_rev_fb = 30.0;
  double P_rev_tone = 55.0;
  std::vector<double> V_rbuf; double* D_rbuf = nullptr; long LEN_rbuf = 800000; long CH_rbuf = 1;
  double H_r_w = 0.0;
  double H_r_ph = 0.0;
  double H_r_T = 24000.0;
  double H_r_fb = 0.0;
  double H_r_lp = 0.0;
  double H_r_lp2 = 0.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    V_rbuf.assign((size_t) 800000 * 1, 0.0); D_rbuf = V_rbuf.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "bpm") return 0;
    if (n == "prac") return 1;
    if (n == "rev_time") return 2;
    if (n == "rev_fb") return 3;
    if (n == "rev_tone") return 4;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 30.0) v = 30.0; if (v > 300.0) v = 300.0; P_bpm = v; break;
      case 1: if (v < 50.0) v = 50.0; if (v > 100.0) v = 100.0; P_prac = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev_time = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev_fb = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev_tone = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_bpm;
      case 1: return P_prac;
      case 2: return P_rev_time;
      case 3: return P_rev_fb;
      case 4: return P_rev_tone;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 5;
  double* data(const std::string& n) {
    if (n == "rbuf") return D_rbuf;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_nrb = 0.0;
      double v_beat = 0.0;
      double v_ti = 0.0;
      double v_mult = 0.0;
      double v_T = 0.0;
      double v_ph = 0.0;
      double v_Tn = 0.0;
      double v_w = 0.0;
      double v_ph2 = 0.0;
      double v_d1 = 0.0;
      double v_d2 = 0.0;
      double v_p1 = 0.0;
      double v_p2 = 0.0;
      double v_i1 = 0.0;
      double v_f1 = 0.0;
      double v_i2 = 0.0;
      double v_f2 = 0.0;
      double v_y1 = 0.0;
      double v_y2 = 0.0;
      double v_c1 = 0.0;
      double v_c2 = 0.0;
      double v_y = 0.0;
      double v_lpc = 0.0;
      double v_lp = 0.0;
      double v_lp2 = 0.0;
      v_nrb = (800000.0);
      v_beat = (((60.0) / ((P_bpm * P_prac) / (100.0))) * SR);
      v_ti = floor(((P_rev_time / (100.0)) * (5.99)));
      v_mult = (0.5);
      if (((double)((v_ti) == ((1.0)))) != 0.0) {
        v_mult = (1.0);
      }
      if (((double)((v_ti) == ((2.0)))) != 0.0) {
        v_mult = (1.5);
      }
      if (((double)((v_ti) == ((3.0)))) != 0.0) {
        v_mult = (2.0);
      }
      if (((double)((v_ti) == ((4.0)))) != 0.0) {
        v_mult = (3.0);
      }
      if (((double)((v_ti) == ((5.0)))) != 0.0) {
        v_mult = (4.0);
      }
      v_T = fmin((v_beat * v_mult),((v_nrb / (2.0)) - (10.0)));
      v_ph = (H_r_ph + ((1.0) / H_r_T));
      if (((double)((v_ph) >= ((1.0)))) != 0.0) {
        v_ph = (v_ph - (1.0));
        H_r_T = v_T;
      }
      H_r_ph = v_ph;
      v_Tn = H_r_T;
      v_w = H_r_w;
      v_ph2 = g_wrap((v_ph + (0.5)),(0.0),(1.0));
      v_d1 = ((((2.0) * v_ph) * v_Tn) + (2.0));
      v_d2 = ((((2.0) * v_ph2) * v_Tn) + (2.0));
      v_p1 = g_wrap((v_w - v_d1),(0.0),v_nrb);
      v_p2 = g_wrap((v_w - v_d2),(0.0),v_nrb);
      v_i1 = floor(v_p1);
      v_f1 = (v_p1 - v_i1);
      v_i2 = floor(v_p2);
      v_f2 = (v_p2 - v_i2);
      v_y1 = (g_peek(D_rbuf,LEN_rbuf,CH_rbuf,v_i1,0) + ((g_peek(D_rbuf,LEN_rbuf,CH_rbuf,g_wrap((v_i1 + (1.0)),(0.0),v_nrb),0) - g_peek(D_rbuf,LEN_rbuf,CH_rbuf,v_i1,0)) * v_f1));
      v_y2 = (g_peek(D_rbuf,LEN_rbuf,CH_rbuf,v_i2,0) + ((g_peek(D_rbuf,LEN_rbuf,CH_rbuf,g_wrap((v_i2 + (1.0)),(0.0),v_nrb),0) - g_peek(D_rbuf,LEN_rbuf,CH_rbuf,v_i2,0)) * v_f2));
      v_c1 = sin((M_PI * v_ph));
      v_c2 = sin((M_PI * v_ph2));
      v_y = (((v_y1 * v_c1) * v_c1) + ((v_y2 * v_c2) * v_c2));
      v_lpc = ((1.0) - exp((((-(2.0*M_PI)) * ((700.0) * pow((22.0),(P_rev_tone / (100.0))))) / SR)));
      v_lp = (H_r_lp + ((v_y - H_r_lp) * v_lpc));
      H_r_lp = v_lp;
      g_poke(D_rbuf,LEN_rbuf,CH_rbuf,(IN1 + tanh((((v_lp * P_rev_fb) / (100.0)) * (1.05)))),v_w,0);
      H_r_w = fmod((v_w + (1.0)),v_nrb);
      OUT1 = v_lp;
      v_lp2 = (H_r_lp2 + (((((((v_y1 * v_c1) * v_c1) * (0.6)) + (((v_y2 * v_c2) * v_c2) * (1.4))) * (0.5)) - H_r_lp2) * v_lpc));
      H_r_lp2 = v_lp2;
      OUT2 = ((v_lp * (0.5)) + (v_lp2 * (0.5)));
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
    }
  }
};
// ---------------------------------------------------------------- Freeze
struct Freeze {
  double SR = 44100.0;
  double P_frz_on = 0.0;
  double P_frz_size = 45.0;
  double P_frz_drift = 30.0;
  double P_frz_fade = 40.0;
  std::vector<double> V_rb; double* D_rb = nullptr; long LEN_rb = 192000; long CH_rb = 1;
  std::vector<double> V_gpos; double* D_gpos = nullptr; long LEN_gpos = 8; long CH_gpos = 1;
  std::vector<double> V_gcnt; double* D_gcnt = nullptr; long LEN_gcnt = 8; long CH_gcnt = 1;
  std::vector<double> V_glen; double* D_glen = nullptr; long LEN_glen = 8; long CH_glen = 1;
  std::vector<double> V_grat; double* D_grat = nullptr; long LEN_grat = 8; long CH_grat = 1;
  std::vector<double> V_gpl; double* D_gpl = nullptr; long LEN_gpl = 8; long CH_gpl = 1;
  std::vector<double> V_gpr; double* D_gpr = nullptr; long LEN_gpr = 8; long CH_gpr = 1;
  double H_h_w = 0.0;
  double H_h_cap = 0.0;
  double H_h_last = 0.0;
  double H_h_env = 0.0;
  double H_h_tick = 0.0;
  double H_h_next = 0.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    V_rb.assign((size_t) 192000 * 1, 0.0); D_rb = V_rb.data();
    V_gpos.assign((size_t) 8 * 1, 0.0); D_gpos = V_gpos.data();
    V_gcnt.assign((size_t) 8 * 1, 0.0); D_gcnt = V_gcnt.data();
    V_glen.assign((size_t) 8 * 1, 0.0); D_glen = V_glen.data();
    V_grat.assign((size_t) 8 * 1, 0.0); D_grat = V_grat.data();
    V_gpl.assign((size_t) 8 * 1, 0.0); D_gpl = V_gpl.data();
    V_gpr.assign((size_t) 8 * 1, 0.0); D_gpr = V_gpr.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "frz_on") return 0;
    if (n == "frz_size") return 1;
    if (n == "frz_drift") return 2;
    if (n == "frz_fade") return 3;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_frz_on = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz_size = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz_drift = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz_fade = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_frz_on;
      case 1: return P_frz_size;
      case 2: return P_frz_drift;
      case 3: return P_frz_fade;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 4;
  double* data(const std::string& n) {
    if (n == "rb") return D_rb;
    if (n == "gpos") return D_gpos;
    if (n == "gcnt") return D_gcnt;
    if (n == "glen") return D_glen;
    if (n == "grat") return D_grat;
    if (n == "gpl") return D_gpl;
    if (n == "gpr") return D_gpr;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_nrb = 0.0;
      double v_x = 0.0;
      double v_on = 0.0;
      double v_fd = 0.0;
      double v_en = 0.0;
      double v_gl = 0.0;
      double v_region = 0.0;
      double v_dr = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_tk = 0.0;
      double v_best = 0.0;
      double v_g = 0.0;
      double v_rt = 0.0;
      double v_st0 = 0.0;
      double v_pn = 0.0;
      double v_c = 0.0;
      double v_gle = 0.0;
      double v_ps = 0.0;
      double v_i0 = 0.0;
      double v_fr = 0.0;
      double v_a = 0.0;
      double v_b = 0.0;
      double v_wv = 0.0;
      double v_v = 0.0;
      v_nrb = (192000.0);
      v_x = IN1;
      v_on = P_frz_on;
      if (((double)((v_on) == ((0.0)))) != 0.0) {
        g_poke(D_rb,LEN_rb,CH_rb,v_x,H_h_w,0);
        H_h_w = fmod((H_h_w + (1.0)),v_nrb);
      }
      if (((double)((((double)((v_on) > ((0.0))))) && (((double)((H_h_last) == ((0.0))))))) != 0.0) {
        H_h_cap = H_h_w;
        H_h_tick = (0.0);
        H_h_next = (0.0);
      }
      H_h_last = v_on;
      v_fd = ((((50.0) + ((P_frz_fade / (100.0)) * (6000.0))))*SR/1000.0);
      v_en = H_h_env;
      if (((double)((v_on) > ((0.0)))) != 0.0) {
        v_en = fmin((1.0),(v_en + ((1.0) / v_fd)));
      } else {
        v_en = fmax((0.0),(v_en - ((1.0) / v_fd)));
      }
      H_h_env = v_en;
      v_gl = ((((80.0) + ((P_frz_size / (100.0)) * (520.0))))*SR/1000.0);
      v_region = fmin((((1500.0))*SR/1000.0),(v_nrb - (v_gl * (3.0))));
      v_dr = (P_frz_drift / (100.0));
      v_yl = (0.0);
      v_yr = (0.0);
      if (((double)((v_en) > ((0.0)))) != 0.0) {
        v_tk = (H_h_tick + (1.0));
        if (((double)((v_tk) >= (H_h_next))) != 0.0) {
          v_tk = (0.0);
          H_h_next = (v_gl / (4.0));
          v_best = (0.0);
          for (v_g = (0.0); ((double)((v_g) < ((8.0)))) != 0.0; v_g = v_g + ((1.0))) {
            if (((double)((g_peek(D_gcnt,LEN_gcnt,CH_gcnt,v_g,0)) >= (g_peek(D_glen,LEN_glen,CH_glen,v_g,0)))) != 0.0) {
              v_best = v_g;
            }
          }
          v_rt = pow((2.0),(((g_noise() * v_dr) * (25.0)) / (1200.0)));
          v_st0 = ((H_h_cap - (v_gl * (1.2))) - ((((0.5) + ((0.5) * g_noise())) * v_region) * ((0.3) + ((0.7) * v_dr))));
          g_poke(D_gpos,LEN_gpos,CH_gpos,g_wrap(v_st0,(0.0),v_nrb),v_best,0);
          g_poke(D_gcnt,LEN_gcnt,CH_gcnt,(0.0),v_best,0);
          g_poke(D_glen,LEN_glen,CH_glen,v_gl,v_best,0);
          g_poke(D_grat,LEN_grat,CH_grat,v_rt,v_best,0);
          v_pn = ((0.5) + (g_noise() * (0.45)));
          g_poke(D_gpl,LEN_gpl,CH_gpl,cos(((v_pn * M_PI) * (0.5))),v_best,0);
          g_poke(D_gpr,LEN_gpr,CH_gpr,sin(((v_pn * M_PI) * (0.5))),v_best,0);
        }
        H_h_tick = v_tk;
        for (v_g = (0.0); ((double)((v_g) < ((8.0)))) != 0.0; v_g = v_g + ((1.0))) {
          v_c = g_peek(D_gcnt,LEN_gcnt,CH_gcnt,v_g,0);
          v_gle = g_peek(D_glen,LEN_glen,CH_glen,v_g,0);
          if (((double)((v_c) < (v_gle))) != 0.0) {
            v_ps = g_wrap((g_peek(D_gpos,LEN_gpos,CH_gpos,v_g,0) + (v_c * g_peek(D_grat,LEN_grat,CH_grat,v_g,0))),(0.0),v_nrb);
            v_i0 = floor(v_ps);
            v_fr = (v_ps - v_i0);
            v_a = g_peek(D_rb,LEN_rb,CH_rb,v_i0,0);
            v_b = g_peek(D_rb,LEN_rb,CH_rb,g_wrap((v_i0 + (1.0)),(0.0),v_nrb),0);
            v_wv = sin(((M_PI * v_c) / v_gle));
            v_v = (((v_a + ((v_b - v_a) * v_fr)) * v_wv) * v_wv);
            v_yl = (v_yl + (v_v * g_peek(D_gpl,LEN_gpl,CH_gpl,v_g,0)));
            v_yr = (v_yr + (v_v * g_peek(D_gpr,LEN_gpr,CH_gpr,v_g,0)));
            g_poke(D_gcnt,LEN_gcnt,CH_gcnt,(v_c + (1.0)),v_g,0);
          }
        }
      }
      OUT1 = ((v_yl * v_en) * (0.7));
      OUT2 = ((v_yr * v_en) * (0.7));
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
    }
  }
};
// ---------------------------------------------------------------- Reson
struct Reson {
  double SR = 44100.0;
  double P_res_decay = 55.0;
  double P_res_bright = 45.0;
  double P_res_spread = 30.0;
  std::vector<double> V_sb; double* D_sb = nullptr; long LEN_sb = 4800; long CH_sb = 12;
  std::vector<double> V_slen; double* D_slen = nullptr; long LEN_slen = 12; long CH_slen = 1;
  std::vector<double> V_snew; double* D_snew = nullptr; long LEN_snew = 12; long CH_snew = 1;
  std::vector<double> V_sg; double* D_sg = nullptr; long LEN_sg = 12; long CH_sg = 1;
  std::vector<double> V_slp; double* D_slp = nullptr; long LEN_slp = 12; long CH_slp = 1;
  std::vector<double> V_spl; double* D_spl = nullptr; long LEN_spl = 12; long CH_spl = 1;
  std::vector<double> V_spr; double* D_spr = nullptr; long LEN_spr = 12; long CH_spr = 1;
  std::vector<double> V_pcs; double* D_pcs = nullptr; long LEN_pcs = 12; long CH_pcs = 1;
  double H_r_init = 0.0;
  double H_r_mask = -1.0;
  double H_r_w = 0.0;
  double H_r_dc = 0.0;
  double H_r_dx = 0.0;
  static constexpr int numInputs = 2;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    V_sb.assign((size_t) 4800 * 12, 0.0); D_sb = V_sb.data();
    V_slen.assign((size_t) 12 * 1, 0.0); D_slen = V_slen.data();
    V_snew.assign((size_t) 12 * 1, 0.0); D_snew = V_snew.data();
    V_sg.assign((size_t) 12 * 1, 0.0); D_sg = V_sg.data();
    V_slp.assign((size_t) 12 * 1, 0.0); D_slp = V_slp.data();
    V_spl.assign((size_t) 12 * 1, 0.0); D_spl = V_spl.data();
    V_spr.assign((size_t) 12 * 1, 0.0); D_spr = V_spr.data();
    V_pcs.assign((size_t) 12 * 1, 0.0); D_pcs = V_pcs.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "res_decay") return 0;
    if (n == "res_bright") return 1;
    if (n == "res_spread") return 2;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res_decay = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res_bright = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res_spread = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_res_decay;
      case 1: return P_res_bright;
      case 2: return P_res_spread;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 3;
  double* data(const std::string& n) {
    if (n == "sb") return D_sb;
    if (n == "slen") return D_slen;
    if (n == "snew") return D_snew;
    if (n == "sg") return D_sg;
    if (n == "slp") return D_slp;
    if (n == "spl") return D_spl;
    if (n == "spr") return D_spr;
    if (n == "pcs") return D_pcs;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_nb = 0.0;
      double v_k = 0.0;
      double v_pn = 0.0;
      double v_mk = 0.0;
      double v_n = 0.0;
      double v_oc = 0.0;
      double v_pc = 0.0;
      double v_note = 0.0;
      double v_hz = 0.0;
      double v_dt = 0.0;
      double v_x = 0.0;
      double v_hpx = 0.0;
      double v_tdec = 0.0;
      double v_dmp = 0.0;
      double v_w = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_kill = 0.0;
      double v_L = 0.0;
      double v_g = 0.0;
      double v_pos = 0.0;
      double v_i0 = 0.0;
      double v_fr = 0.0;
      double v_a = 0.0;
      double v_b = 0.0;
      double v_y = 0.0;
      double v_lp = 0.0;
      double v_fbk = 0.0;
      v_nb = (4800.0);
      if (((double)((H_r_init) == ((0.0)))) != 0.0) {
        for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
          v_pn = ((0.5) + (((fmod(v_k,(2.0)) * (2.0)) - (1.0)) * ((0.15) + ((0.03) * v_k))));
          g_poke(D_spl,LEN_spl,CH_spl,cos(((v_pn * M_PI) * (0.5))),v_k,0);
          g_poke(D_spr,LEN_spr,CH_spr,sin(((v_pn * M_PI) * (0.5))),v_k,0);
          g_poke(D_slen,LEN_slen,CH_slen,(400.0),v_k,0);
          g_poke(D_snew,LEN_snew,CH_snew,(400.0),v_k,0);
          g_poke(D_sg,LEN_sg,CH_sg,(0.0),v_k,0);
        }
        H_r_init = (1.0);
      }
      v_mk = floor((IN2 + (0.5)));
      if (((double)((v_mk) != (H_r_mask))) != 0.0) {
        H_r_mask = v_mk;
        v_n = (0.0);
        for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
          if (((double)((((double)((v_mk) > ((0.0))))) && (((double)((fmod(floor((v_mk / pow((2.0),v_k))),(2.0))) == ((1.0))))))) != 0.0) {
            g_poke(D_pcs,LEN_pcs,CH_pcs,v_k,v_n,0);
            v_n = (v_n + (1.0));
          }
        }
        if (((double)((v_n) == ((0.0)))) != 0.0) {
          g_poke(D_pcs,LEN_pcs,CH_pcs,(0.0),(0.0),0);
          g_poke(D_pcs,LEN_pcs,CH_pcs,(7.0),(1.0),0);
          g_poke(D_pcs,LEN_pcs,CH_pcs,(2.0),(2.0),0);
          g_poke(D_pcs,LEN_pcs,CH_pcs,(9.0),(3.0),0);
          g_poke(D_pcs,LEN_pcs,CH_pcs,(4.0),(4.0),0);
          v_n = (5.0);
        }
        for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
          v_oc = floor((v_k / v_n));
          v_pc = g_peek(D_pcs,LEN_pcs,CH_pcs,fmod(v_k,v_n),0);
          v_note = (((36.0) + v_pc) + ((12.0) * fmin(v_oc,(3.0))));
          if (((double)((v_note) > ((84.0)))) != 0.0) {
            v_note = (v_note - (12.0));
          }
          v_hz = ((440.0) * pow((2.0),((v_note - (69.0)) / (12.0))));
          v_dt = (((P_res_spread / (100.0)) * (0.004)) * (fmod(v_k,(3.0)) - (1.0)));
          g_poke(D_snew,LEN_snew,CH_snew,(SR / (v_hz * ((1.0) + v_dt))),v_k,0);
        }
      }
      v_x = IN1;
      v_hpx = ((v_x - H_r_dx) + ((0.995) * H_r_dc));
      H_r_dx = v_x;
      H_r_dc = v_hpx;
      v_tdec = ((0.3) + (pow((P_res_decay / (100.0)),(2.0)) * (14.0)));
      v_dmp = ((0.15) + (((0.8) * P_res_bright) / (100.0)));
      v_w = H_r_w;
      v_yl = (0.0);
      v_yr = (0.0);
      v_kill = (1.0);
      for (v_k = (0.0); ((double)((v_k) < ((12.0)))) != 0.0; v_k = v_k + ((1.0))) {
        v_L = g_peek(D_slen,LEN_slen,CH_slen,v_k,0);
        v_g = g_peek(D_sg,LEN_sg,CH_sg,v_k,0);
        if (((double)((fabs((g_peek(D_snew,LEN_snew,CH_snew,v_k,0) - v_L))) > ((0.01)))) != 0.0) {
          v_g = (v_g - ((1.0) / (((20.0))*SR/1000.0)));
          if (((double)((v_g) <= ((0.0)))) != 0.0) {
            v_g = (0.0);
            v_L = g_peek(D_snew,LEN_snew,CH_snew,v_k,0);
            g_poke(D_slen,LEN_slen,CH_slen,v_L,v_k,0);
            g_poke(D_slp,LEN_slp,CH_slp,(0.0),v_k,0);
          }
          v_kill = v_g;
        } else {
          v_g = fmin((1.0),(v_g + ((1.0) / (((60.0))*SR/1000.0))));
        }
        g_poke(D_sg,LEN_sg,CH_sg,v_g,v_k,0);
        if (((double)((fabs((g_peek(D_snew,LEN_snew,CH_snew,v_k,0) - v_L))) <= ((0.01)))) != 0.0) {
          v_kill = (1.0);
        }
        v_pos = g_wrap((v_w - v_L),(0.0),v_nb);
        v_i0 = floor(v_pos);
        v_fr = (v_pos - v_i0);
        v_a = g_peek(D_sb,LEN_sb,CH_sb,v_i0,v_k);
        v_b = g_peek(D_sb,LEN_sb,CH_sb,g_wrap((v_i0 + (1.0)),(0.0),v_nb),v_k);
        v_y = (v_a + ((v_b - v_a) * v_fr));
        v_lp = (g_peek(D_slp,LEN_slp,CH_slp,v_k,0) + ((v_y - g_peek(D_slp,LEN_slp,CH_slp,v_k,0)) * v_dmp));
        g_poke(D_slp,LEN_slp,CH_slp,v_lp,v_k,0);
        v_fbk = pow((10.0),(((-(3.0)) * v_L) / (v_tdec * SR)));
        g_poke(D_sb,LEN_sb,CH_sb,(((v_hpx * (0.08)) * v_g) + ((v_lp * v_fbk) * v_kill)),v_w,v_k);
        v_yl = (v_yl + ((v_y * v_g) * g_peek(D_spl,LEN_spl,CH_spl,v_k,0)));
        v_yr = (v_yr + ((v_y * v_g) * g_peek(D_spr,LEN_spr,CH_spr,v_k,0)));
      }
      H_r_w = fmod((v_w + (1.0)),v_nb);
      OUT1 = tanh((v_yl * (0.9)));
      OUT2 = tanh((v_yr * (0.9)));
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
    }
  }
};
// ---------------------------------------------------------------- Grain
struct Grain {
  double SR = 44100.0;
  double P_grn_size = 35.0;
  double P_grn_dens = 45.0;
  double P_grn_scat = 40.0;
  double P_grn_pitch = 0.0;
  double P_grn_rev = 20.0;
  std::vector<double> V_rb; double* D_rb = nullptr; long LEN_rb = 192000; long CH_rb = 1;
  std::vector<double> V_gp; double* D_gp = nullptr; long LEN_gp = 24; long CH_gp = 1;
  std::vector<double> V_ga; double* D_ga = nullptr; long LEN_ga = 24; long CH_ga = 1;
  std::vector<double> V_gl; double* D_gl = nullptr; long LEN_gl = 24; long CH_gl = 1;
  std::vector<double> V_gr; double* D_gr = nullptr; long LEN_gr = 24; long CH_gr = 1;
  std::vector<double> V_gL; double* D_gL = nullptr; long LEN_gL = 24; long CH_gL = 1;
  std::vector<double> V_gR; double* D_gR = nullptr; long LEN_gR = 24; long CH_gR = 1;
  double H_g_w = 0.0;
  double H_g_t = 0.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    V_rb.assign((size_t) 192000 * 1, 0.0); D_rb = V_rb.data();
    V_gp.assign((size_t) 24 * 1, 0.0); D_gp = V_gp.data();
    V_ga.assign((size_t) 24 * 1, 0.0); D_ga = V_ga.data();
    V_gl.assign((size_t) 24 * 1, 0.0); D_gl = V_gl.data();
    V_gr.assign((size_t) 24 * 1, 0.0); D_gr = V_gr.data();
    V_gL.assign((size_t) 24 * 1, 0.0); D_gL = V_gL.data();
    V_gR.assign((size_t) 24 * 1, 0.0); D_gR = V_gR.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "grn_size") return 0;
    if (n == "grn_dens") return 1;
    if (n == "grn_scat") return 2;
    if (n == "grn_pitch") return 3;
    if (n == "grn_rev") return 4;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn_size = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn_dens = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn_scat = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn_pitch = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn_rev = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_grn_size;
      case 1: return P_grn_dens;
      case 2: return P_grn_scat;
      case 3: return P_grn_pitch;
      case 4: return P_grn_rev;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 5;
  double* data(const std::string& n) {
    if (n == "rb") return D_rb;
    if (n == "gp") return D_gp;
    if (n == "ga") return D_ga;
    if (n == "gl") return D_gl;
    if (n == "gr") return D_gr;
    if (n == "gL") return D_gL;
    if (n == "gR") return D_gR;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_nrb = 0.0;
      double v_x = 0.0;
      double v_w = 0.0;
      double v_glen = 0.0;
      double v_grate = 0.0;
      double v_tk = 0.0;
      double v_bg = 0.0;
      double v_g = 0.0;
      double v_ln3 = 0.0;
      double v_pz = 0.0;
      double v_st = 0.0;
      double v_ch = 0.0;
      double v_pick = 0.0;
      double v_rt = 0.0;
      double v_back = 0.0;
      double v_need = 0.0;
      double v_pn = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_a0 = 0.0;
      double v_le = 0.0;
      double v_ps = 0.0;
      double v_i0 = 0.0;
      double v_fr = 0.0;
      double v_a = 0.0;
      double v_b = 0.0;
      double v_wv = 0.0;
      double v_v = 0.0;
      double v_k2 = 0.0;
      v_nrb = (192000.0);
      v_x = IN1;
      v_w = H_g_w;
      g_poke(D_rb,LEN_rb,CH_rb,v_x,v_w,0);
      H_g_w = fmod((v_w + (1.0)),v_nrb);
      v_glen = ((((20.0) + (pow((P_grn_size / (100.0)),(1.6)) * (480.0))))*SR/1000.0);
      v_grate = ((2.0) + (pow((P_grn_dens / (100.0)),(1.5)) * (58.0)));
      v_tk = (H_g_t + (v_grate / SR));
      if (((double)((v_tk) >= ((1.0)))) != 0.0) {
        v_tk = (v_tk - (1.0));
        v_bg = (-(1.0));
        for (v_g = (0.0); ((double)((v_g) < ((24.0)))) != 0.0; v_g = v_g + ((1.0))) {
          if (((double)((g_peek(D_ga,LEN_ga,CH_ga,v_g,0)) >= (g_peek(D_gl,LEN_gl,CH_gl,v_g,0)))) != 0.0) {
            v_bg = v_g;
          }
        }
        if (((double)((v_bg) >= ((0.0)))) != 0.0) {
          v_ln3 = (v_glen * ((0.7) + ((0.6) * ((0.5) + ((0.5) * g_noise())))));
          v_pz = (P_grn_pitch / (100.0));
          v_st = ((g_noise() * fmin(v_pz,(0.5))) * (0.6));
          if (((double)((v_pz) > ((0.5)))) != 0.0) {
            v_ch = ((0.5) + ((0.5) * g_noise()));
            if (((double)((v_ch) < (((v_pz - (0.5)) * (1.6))))) != 0.0) {
              v_pick = floor((((0.5) + ((0.5) * g_noise())) * (4.99)));
              v_st = (-(12.0));
              if (((double)((v_pick) == ((1.0)))) != 0.0) {
                v_st = (12.0);
              }
              if (((double)((v_pick) == ((2.0)))) != 0.0) {
                v_st = (7.0);
              }
              if (((double)((v_pick) == ((3.0)))) != 0.0) {
                v_st = (-(5.0));
              }
              if (((double)((v_pick) == ((4.0)))) != 0.0) {
                v_st = (19.0);
              }
            }
          }
          v_rt = pow((2.0),(v_st / (12.0)));
          if (((double)((((0.5) + ((0.5) * g_noise()))) < ((P_grn_rev / (100.0))))) != 0.0) {
            v_rt = (-v_rt);
          }
          v_back = ((((10.0))*SR/1000.0) + (((0.5) + ((0.5) * g_noise())) * ((((P_grn_scat / (100.0)) * (2000.0)))*SR/1000.0)));
          v_need = ((v_ln3 * fabs(v_rt)) + (((5.0))*SR/1000.0));
          if (((double)((v_rt) > ((0.0)))) != 0.0) {
            v_back = (v_back + v_need);
          } else {
            v_back = (v_back + (((5.0))*SR/1000.0));
          }
          g_poke(D_gp,LEN_gp,CH_gp,g_wrap((v_w - v_back),(0.0),v_nrb),v_bg,0);
          g_poke(D_ga,LEN_ga,CH_ga,(0.0),v_bg,0);
          g_poke(D_gl,LEN_gl,CH_gl,v_ln3,v_bg,0);
          g_poke(D_gr,LEN_gr,CH_gr,v_rt,v_bg,0);
          v_pn = ((0.5) + (g_noise() * (0.48)));
          g_poke(D_gL,LEN_gL,CH_gL,cos(((v_pn * M_PI) * (0.5))),v_bg,0);
          g_poke(D_gR,LEN_gR,CH_gR,sin(((v_pn * M_PI) * (0.5))),v_bg,0);
        }
      }
      H_g_t = v_tk;
      v_yl = (0.0);
      v_yr = (0.0);
      for (v_g = (0.0); ((double)((v_g) < ((24.0)))) != 0.0; v_g = v_g + ((1.0))) {
        v_a0 = g_peek(D_ga,LEN_ga,CH_ga,v_g,0);
        v_le = g_peek(D_gl,LEN_gl,CH_gl,v_g,0);
        if (((double)((v_a0) < (v_le))) != 0.0) {
          v_ps = g_wrap((g_peek(D_gp,LEN_gp,CH_gp,v_g,0) + (v_a0 * g_peek(D_gr,LEN_gr,CH_gr,v_g,0))),(0.0),v_nrb);
          v_i0 = floor(v_ps);
          v_fr = (v_ps - v_i0);
          v_a = g_peek(D_rb,LEN_rb,CH_rb,v_i0,0);
          v_b = g_peek(D_rb,LEN_rb,CH_rb,g_wrap((v_i0 + (1.0)),(0.0),v_nrb),0);
          v_wv = sin(((M_PI * v_a0) / v_le));
          v_v = (((v_a + ((v_b - v_a) * v_fr)) * v_wv) * v_wv);
          v_yl = (v_yl + (v_v * g_peek(D_gL,LEN_gL,CH_gL,v_g,0)));
          v_yr = (v_yr + (v_v * g_peek(D_gR,LEN_gR,CH_gR,v_g,0)));
          g_poke(D_ga,LEN_ga,CH_ga,(v_a0 + (1.0)),v_g,0);
        }
      }
      v_k2 = ((1.4) / sqrt(fmax(0.0,fmax((1.0),((v_grate * v_glen) / SR)))));
      OUT1 = (v_yl * v_k2);
      OUT2 = (v_yr * v_k2);
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
    }
  }
};
// ---------------------------------------------------------------- FxBox
struct FxBox {
  double SR = 44100.0;
  double P_fxb_mode = 0.0;
  double P_fxb_amt = 50.0;
  double P_fxb_rate = 50.0;
  double P_fxb_col = 50.0;
  std::vector<double> V_rb; double* D_rb = nullptr; long LEN_rb = 192000; long CH_rb = 1;
  double H_f_w = 0.0;
  double H_f_hold = 0.0;
  double H_f_hc = 0.0;
  double H_f_rph = 0.0;
  double H_f_s1 = 0.0;
  double H_f_s2 = 0.0;
  double H_f_s3 = 0.0;
  double H_f_s4 = 0.0;
  double H_f_st = 0.0;
  double H_f_rep = 0.0;
  double H_f_rst = 0.0;
  double H_f_rpos = 0.0;
  double H_f_rlen = 1000.0;
  double H_f_lstep = -1.0;
  double H_f_pan = 0.5;
  double H_f_lp = 0.0;
  double H_f_ph1 = 0.0;
  double H_f_ph2 = 0.5;
  double H_f_ph3 = 0.0;
  double H_f_ph4 = 0.5;
  double H_f_amt = 0.5;
  double H_f_col = 0.5;
  static constexpr int numInputs = 2;
  static constexpr int numOutputs = 3;
  void init(double sr) { SR = sr;
    V_rb.assign((size_t) 192000 * 1, 0.0); D_rb = V_rb.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "fxb_mode") return 0;
    if (n == "fxb_amt") return 1;
    if (n == "fxb_rate") return 2;
    if (n == "fxb_col") return 3;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb_mode = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb_amt = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb_rate = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb_col = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_fxb_mode;
      case 1: return P_fxb_amt;
      case 2: return P_fxb_rate;
      case 3: return P_fxb_col;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 4;
  double* data(const std::string& n) {
    if (n == "rb") return D_rb;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double v_nrb = 0.0;
      double v_x = 0.0;
      double v_bp = 0.0;
      double v_w = 0.0;
      double v_mode = 0.0;
      double v_am = 0.0;
      double v_co = 0.0;
      double v_ri = 0.0;
      double v_dv = 0.0;
      double v_lfo = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_hold = 0.0;
      double v_hc = 0.0;
      double v_bits = 0.0;
      double v_q = 0.0;
      double v_y = 0.0;
      double v_lp = 0.0;
      double v_fr = 0.0;
      double v_ph = 0.0;
      double v_res = 0.0;
      double v_c1 = 0.0;
      double v_c2 = 0.0;
      double v_k1 = 0.0;
      double v_g1 = 0.0;
      double v_a1 = 0.0;
      double v_a2 = 0.0;
      double v_a3 = 0.0;
      double v_v3 = 0.0;
      double v_v1 = 0.0;
      double v_l1 = 0.0;
      double v_g2 = 0.0;
      double v_b1 = 0.0;
      double v_b2 = 0.0;
      double v_b3 = 0.0;
      double v_w3 = 0.0;
      double v_w1 = 0.0;
      double v_l2 = 0.0;
      double v_stp = 0.0;
      double v_sl = 0.0;
      double v_gl = 0.0;
      double v_p = 0.0;
      double v_ev = 0.0;
      double v_f0 = 0.0;
      double v_dl = 0.0;
      double v_fb = 0.0;
      double v_p1 = 0.0;
      double v_i0 = 0.0;
      double v_fr2 = 0.0;
      double v_d1 = 0.0;
      double v_p2 = 0.0;
      double v_oi = 0.0;
      double v_sh = 0.0;
      double v_r1 = 0.0;
      double v_r2 = 0.0;
      double v_W = 0.0;
      double v_q1 = 0.0;
      double v_q2 = 0.0;
      double v_yo = 0.0;
      double v_yf = 0.0;
      double v_j = 0.0;
      double v_ph1 = 0.0;
      double v_pp = 0.0;
      double v_cw = 0.0;
      double v_ph2 = 0.0;
      v_nrb = (192000.0);
      v_x = IN1;
      v_bp = IN2;
      v_w = H_f_w;
      g_poke(D_rb,LEN_rb,CH_rb,v_x,v_w,0);
      H_f_w = fmod((v_w + (1.0)),v_nrb);
      v_mode = floor(((P_fxb_mode / (100.0)) * (5.99)));
      v_am = (H_f_amt + (((P_fxb_amt / (100.0)) - H_f_amt) * (0.002)));
      H_f_amt = v_am;
      v_co = (H_f_col + (((P_fxb_col / (100.0)) - H_f_col) * (0.002)));
      H_f_col = v_co;
      v_ri = floor(((P_fxb_rate / (100.0)) * (5.99)));
      v_dv = (4.0);
      if (((double)((v_ri) == ((1.0)))) != 0.0) {
        v_dv = (2.0);
      }
      if (((double)((v_ri) == ((2.0)))) != 0.0) {
        v_dv = (1.0);
      }
      if (((double)((v_ri) == ((3.0)))) != 0.0) {
        v_dv = (0.5);
      }
      if (((double)((v_ri) == ((4.0)))) != 0.0) {
        v_dv = (0.25);
      }
      if (((double)((v_ri) == ((5.0)))) != 0.0) {
        v_dv = (0.125);
      }
      v_lfo = g_wrap((v_bp / v_dv),(0.0),(1.0));
      v_yl = (0.0);
      v_yr = (0.0);
      if (((double)((v_mode) == ((0.0)))) != 0.0) {
        v_hold = ((1.0) + floor(((v_am * v_am) * (40.0))));
        v_hc = (H_f_hc + (1.0));
        if (((double)((v_hc) >= (v_hold))) != 0.0) {
          v_hc = (0.0);
          H_f_hold = v_x;
        }
        H_f_hc = v_hc;
        v_bits = ((16.0) - (v_co * (13.0)));
        v_q = pow((2.0),(v_bits - (1.0)));
        v_y = (floor(((H_f_hold * v_q) + (0.5))) / v_q);
        v_lp = (H_f_lp + ((v_y - H_f_lp) * ((0.2) + ((0.8) * ((1.0) - v_am)))));
        H_f_lp = v_lp;
        v_yl = v_lp;
        v_yr = v_lp;
      }
      if (((double)((v_mode) == ((1.0)))) != 0.0) {
        v_fr = (((30.0) * pow((66.0),v_co)) * pow((2.0),((v_am * (0.5)) * sin(((2.0*M_PI) * v_lfo)))));
        v_ph = g_wrap((H_f_rph + (v_fr / SR)),(0.0),(1.0));
        H_f_rph = v_ph;
        v_yl = (v_x * (((1.0) - v_am) + (v_am * sin(((2.0*M_PI) * v_ph)))));
        v_yr = (v_x * (((1.0) - v_am) + (v_am * sin(((2.0*M_PI) * (v_ph + (0.25)))))));
      }
      if (((double)((v_mode) == ((2.0)))) != 0.0) {
        v_res = ((0.5) + (v_co * (11.0)));
        v_c1 = ((180.0) * pow((2.0),((v_am * (6.0)) * ((0.5) + ((0.5) * sin(((2.0*M_PI) * v_lfo)))))));
        v_c2 = ((180.0) * pow((2.0),((v_am * (6.0)) * ((0.5) - ((0.5) * sin(((2.0*M_PI) * v_lfo)))))));
        v_k1 = ((1.0) / v_res);
        v_g1 = tan(((M_PI * fmin(v_c1,(SR * (0.45)))) / SR));
        v_a1 = ((1.0) / ((1.0) + (v_g1 * (v_g1 + v_k1))));
        v_a2 = (v_g1 * v_a1);
        v_a3 = (v_g1 * v_a2);
        v_v3 = (v_x - H_f_s2);
        v_v1 = ((v_a1 * H_f_s1) + (v_a2 * v_v3));
        v_l1 = ((H_f_s2 + (v_a2 * H_f_s1)) + (v_a3 * v_v3));
        H_f_s1 = (((2.0) * v_v1) - H_f_s1);
        H_f_s2 = (((2.0) * v_l1) - H_f_s2);
        v_g2 = tan(((M_PI * fmin(v_c2,(SR * (0.45)))) / SR));
        v_b1 = ((1.0) / ((1.0) + (v_g2 * (v_g2 + v_k1))));
        v_b2 = (v_g2 * v_b1);
        v_b3 = (v_g2 * v_b2);
        v_w3 = (v_x - H_f_s4);
        v_w1 = ((v_b1 * H_f_s3) + (v_b2 * v_w3));
        v_l2 = ((H_f_s4 + (v_b2 * H_f_s3)) + (v_b3 * v_w3));
        H_f_s3 = (((2.0) * v_w1) - H_f_s3);
        H_f_s4 = (((2.0) * v_l2) - H_f_s4);
        v_yl = tanh((v_l1 * (0.9)));
        v_yr = tanh((v_l2 * (0.9)));
      }
      if (((double)((v_mode) == ((3.0)))) != 0.0) {
        v_stp = floor((v_bp / v_dv));
        if (((double)((v_stp) != (H_f_lstep))) != 0.0) {
          H_f_lstep = v_stp;
          v_sl = H_f_rlen;
          if (((double)((((g_noise() * (0.5)) + (0.5))) < (v_am))) != 0.0) {
            H_f_rep = (1.0);
            H_f_rst = g_wrap((v_w - v_sl),(0.0),v_nrb);
            H_f_rpos = (0.0);
            H_f_pan = ((0.5) + (g_noise() * (0.4)));
          } else {
            H_f_rep = (0.0);
          }
          H_f_st = (0.0);
        }
        H_f_st = (H_f_st + (1.0));
        if (((double)((H_f_rep) == ((0.0)))) != 0.0) {
          H_f_rlen = fmax((64.0),fmin((v_nrb / (2.0)),H_f_st));
        }
        v_y = (0.0);
        if (((double)((H_f_rep) > ((0.0)))) != 0.0) {
          v_gl = (H_f_rlen * ((0.3) + ((0.7) * v_co)));
          v_p = fmod(H_f_rpos,H_f_rlen);
          if (((double)((v_p) < (v_gl))) != 0.0) {
            v_ev = fmin((1.0),(fmin(v_p,(v_gl - v_p)) / (((2.0))*SR/1000.0)));
            v_y = (g_peek(D_rb,LEN_rb,CH_rb,g_wrap((H_f_rst + v_p),(0.0),v_nrb),0) * v_ev);
          }
          H_f_rpos = (H_f_rpos + (1.0));
        }
        v_yl = ((v_y * cos(((H_f_pan * M_PI) * (0.5)))) * (1.41));
        v_yr = ((v_y * sin(((H_f_pan * M_PI) * (0.5)))) * (1.41));
      }
      if (((double)((v_mode) == ((4.0)))) != 0.0) {
        v_f0 = ((40.0) * pow((50.0),v_co));
        v_dl = ((SR / v_f0) * ((1.0) + ((0.02) * sin(((2.0*M_PI) * v_lfo)))));
        v_fb = ((0.5) + (v_am * (0.48)));
        v_p1 = g_wrap((v_w - v_dl),(0.0),v_nrb);
        v_i0 = floor(v_p1);
        v_fr2 = (v_p1 - v_i0);
        v_d1 = (g_peek(D_rb,LEN_rb,CH_rb,v_i0,0) + ((g_peek(D_rb,LEN_rb,CH_rb,g_wrap((v_i0 + (1.0)),(0.0),v_nrb),0) - g_peek(D_rb,LEN_rb,CH_rb,v_i0,0)) * v_fr2));
        v_y = (v_x + (v_d1 * v_fb));
        g_poke(D_rb,LEN_rb,CH_rb,tanh(v_y),v_w,0);
        v_yl = (v_y * (0.5));
        v_p2 = g_wrap((v_w - (v_dl * (1.5))),(0.0),v_nrb);
        v_i0 = floor(v_p2);
        v_fr2 = (v_p2 - v_i0);
        v_yr = ((v_x + ((g_peek(D_rb,LEN_rb,CH_rb,v_i0,0) + ((g_peek(D_rb,LEN_rb,CH_rb,g_wrap((v_i0 + (1.0)),(0.0),v_nrb),0) - g_peek(D_rb,LEN_rb,CH_rb,v_i0,0)) * v_fr2)) * v_fb)) * (0.5));
      }
      if (((double)((v_mode) == ((5.0)))) != 0.0) {
        v_oi = floor((v_co * (3.99)));
        v_sh = (-(24.0));
        if (((double)((v_oi) == ((1.0)))) != 0.0) {
          v_sh = (-(12.0));
        }
        if (((double)((v_oi) == ((2.0)))) != 0.0) {
          v_sh = (12.0);
        }
        if (((double)((v_oi) == ((3.0)))) != 0.0) {
          v_sh = (24.0);
        }
        v_r1 = pow((2.0),(v_sh / (12.0)));
        v_r2 = pow((2.0),((v_sh + (7.0)) / (12.0)));
        v_W = (((90.0))*SR/1000.0);
        v_q1 = g_wrap((H_f_ph1 + (((1.0) - v_r1) / v_W)),(0.0),(1.0));
        H_f_ph1 = v_q1;
        v_q2 = g_wrap((H_f_ph3 + (((1.0) - v_r2) / v_W)),(0.0),(1.0));
        H_f_ph3 = v_q2;
        v_yo = (0.0);
        v_yf = (0.0);
        for (v_j = (0.0); ((double)((v_j) < ((2.0)))) != 0.0; v_j = v_j + ((1.0))) {
          v_ph1 = g_wrap((v_q1 + (v_j * (0.5))),(0.0),(1.0));
          v_pp = g_wrap(((v_w - (v_ph1 * v_W)) - (2.0)),(0.0),v_nrb);
          v_i0 = floor(v_pp);
          v_fr2 = (v_pp - v_i0);
          v_cw = (((1.0) - cos(((2.0*M_PI) * v_ph1))) * (0.5));
          v_yo = (v_yo + ((g_peek(D_rb,LEN_rb,CH_rb,v_i0,0) + ((g_peek(D_rb,LEN_rb,CH_rb,g_wrap((v_i0 + (1.0)),(0.0),v_nrb),0) - g_peek(D_rb,LEN_rb,CH_rb,v_i0,0)) * v_fr2)) * v_cw));
          v_ph2 = g_wrap((v_q2 + (v_j * (0.5))),(0.0),(1.0));
          v_pp = g_wrap(((v_w - (v_ph2 * v_W)) - (2.0)),(0.0),v_nrb);
          v_i0 = floor(v_pp);
          v_fr2 = (v_pp - v_i0);
          v_cw = (((1.0) - cos(((2.0*M_PI) * v_ph2))) * (0.5));
          v_yf = (v_yf + ((g_peek(D_rb,LEN_rb,CH_rb,v_i0,0) + ((g_peek(D_rb,LEN_rb,CH_rb,g_wrap((v_i0 + (1.0)),(0.0),v_nrb),0) - g_peek(D_rb,LEN_rb,CH_rb,v_i0,0)) * v_fr2)) * v_cw));
        }
        v_yl = (v_yo + ((v_yf * v_am) * (0.7)));
        v_yr = (v_yo + ((v_yf * v_am) * (0.7)));
      }
      OUT1 = v_yl;
      OUT2 = v_yr;
      OUT3 = v_mode;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
    }
  }
};
// ---------------------------------------------------------------- Drums
struct Drums {
  double SR = 44100.0;
  double P_drm_on = 0.0;
  double P_drm_style = 0.0;
  double P_drm_kit = 0.0;
  double P_drm_busy = 20.0;
  double P_drm_lvl = 40.0;
  double P_drm_swing = 0.0;
  double P_drm_tone = 45.0;
  double P_beats = 4.0;
  std::vector<double> V_pat; double* D_pat = nullptr; long LEN_pat = 16; long CH_pat = 88;
  std::vector<double> V_env; double* D_env = nullptr; long LEN_env = 12; long CH_env = 1;
  std::vector<double> V_vph; double* D_vph = nullptr; long LEN_vph = 12; long CH_vph = 1;
  std::vector<double> V_pen; double* D_pen = nullptr; long LEN_pen = 12; long CH_pen = 1;
  std::vector<double> V_vel; double* D_vel = nullptr; long LEN_vel = 12; long CH_vel = 1;
  std::vector<double> V_hp6; double* D_hp6 = nullptr; long LEN_hp6 = 6; long CH_hp6 = 1;
  double H_d_init = 0.0;
  double H_d_lstep = -1.0;
  double H_d_lbeat = -1.0;
  double H_d_run = 0.0;
  double H_d_bus1 = 0.0;
  double H_d_bus2 = 0.0;
  double H_d_hpx = 0.0;
  double H_d_hpy = 0.0;
  double H_d_hpx2 = 0.0;
  double H_d_hpy2 = 0.0;
  double H_d_rdx = 0.0;
  double H_d_rdy = 0.0;
  double H_d_snl = 0.0;
  double H_d_sn2 = 0.0;
  double H_d_cll = 0.0;
  double H_d_cl2 = 0.0;
  double H_d_hold = 0.0;
  double H_d_hcnt = 0.0;
  double H_d_clk = 0.0;
  double H_d_crush = 0.0;
  double H_d_sbh = 0.0;
  double H_d_sbl = 0.0;
  double H_d_sbx = 0.0;
  double H_d_hlp = 0.0;
  double H_d_cbh = 0.0;
  double H_d_cbx = 0.0;
  double H_d_lim = 1.0;
  static constexpr int numInputs = 3;
  static constexpr int numOutputs = 4;
  void init(double sr) { SR = sr;
    V_pat.assign((size_t) 16 * 88, 0.0); D_pat = V_pat.data();
    V_env.assign((size_t) 12 * 1, 0.0); D_env = V_env.data();
    V_vph.assign((size_t) 12 * 1, 0.0); D_vph = V_vph.data();
    V_pen.assign((size_t) 12 * 1, 0.0); D_pen = V_pen.data();
    V_vel.assign((size_t) 12 * 1, 0.0); D_vel = V_vel.data();
    V_hp6.assign((size_t) 6 * 1, 0.0); D_hp6 = V_hp6.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "drm_on") return 0;
    if (n == "drm_style") return 1;
    if (n == "drm_kit") return 2;
    if (n == "drm_busy") return 3;
    if (n == "drm_lvl") return 4;
    if (n == "drm_swing") return 5;
    if (n == "drm_tone") return 6;
    if (n == "beats") return 7;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_drm_on = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_style = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_kit = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_busy = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_lvl = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_swing = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_tone = v; break;
      case 7: if (v < 1.0) v = 1.0; if (v > 16.0) v = 16.0; P_beats = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_drm_on;
      case 1: return P_drm_style;
      case 2: return P_drm_kit;
      case 3: return P_drm_busy;
      case 4: return P_drm_lvl;
      case 5: return P_drm_swing;
      case 6: return P_drm_tone;
      case 7: return P_beats;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 8;
  double* data(const std::string& n) {
    if (n == "pat") return D_pat;
    if (n == "env") return D_env;
    if (n == "vph") return D_vph;
    if (n == "pen") return D_pen;
    if (n == "vel") return D_vel;
    if (n == "hp6") return D_hp6;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double v_bp = 0.0;
      double v_tr = 0.0;
      double v_hasloop = 0.0;
      double v_sty = 0.0;
      double v_kit = 0.0;
      double v_busy = 0.0;
      double v_bts = 0.0;
      double v_play = 0.0;
      double v_e8 = 0.0;
      double v_i8 = 0.0;
      double v_f8 = 0.0;
      double v_th = 0.0;
      double v_stp16 = 0.0;
      double v_spb = 0.0;
      double v_sib = 0.0;
      double v_st16 = 0.0;
      double v_bar = 0.0;
      double v_fill = 0.0;
      double v_ii = 0.0;
      double v_v = 0.0;
      double v_vi = 0.0;
      double v_vv = 0.0;
      double v_fi = 0.0;
      double v_fv = 0.0;
      double v_fvi = 0.0;
      double v_sr = 0.0;
      double v_nz = 0.0;
      double v_kf0 = 0.0;
      double v_kf1 = 0.0;
      double v_kpd = 0.0;
      double v_kad = 0.0;
      double v_kcl = 0.0;
      double v_kdr = 0.0;
      double v_sf1 = 0.0;
      double v_sf2 = 0.0;
      double v_sad = 0.0;
      double v_snd = 0.0;
      double v_snm = 0.0;
      double v_hcd = 0.0;
      double v_hod = 0.0;
      double v_hmet = 0.0;
      double v_ev = 0.0;
      double v_kick = 0.0;
      double v_pe = 0.0;
      double v_f = 0.0;
      double v_ph = 0.0;
      double v_cl = 0.0;
      double v_snare = 0.0;
      double v_tone = 0.0;
      double v_sbh = 0.0;
      double v_bn = 0.0;
      double v_hm = 0.0;
      double v_j = 0.0;
      double v_rt = 0.0;
      double v_q = 0.0;
      double v_hlp = 0.0;
      double v_h1 = 0.0;
      double v_h2 = 0.0;
      double v_hatc = 0.0;
      double v_hato = 0.0;
      double v_clap = 0.0;
      double v_t = 0.0;
      double v_burst = 0.0;
      double v_cbh = 0.0;
      double v_rim = 0.0;
      double v_tl = 0.0;
      double v_th2 = 0.0;
      double v_ride = 0.0;
      double v_rb2 = 0.0;
      double v_shk = 0.0;
      double v_L = 0.0;
      double v_R = 0.0;
      double v_lc = 0.0;
      double v_b1 = 0.0;
      double v_b2 = 0.0;
      double v_hc = 0.0;
      double v_lv = 0.0;
      double v_o1 = 0.0;
      double v_o2 = 0.0;
      double v_pk = 0.0;
      double v_lg = 0.0;
      if (((double)((H_d_init) == ((0.0)))) != 0.0) {
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(0.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(8.0),(0.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(10.0),(0.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(4.0),(1.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(12.0),(1.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(0.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(2.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(4.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(6.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(8.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(10.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(12.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(14.0),(2.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(5.0)),(14.0),(3.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(2.0),(9.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(6.0),(9.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(10.0),(9.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(14.0),(9.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(6.0),(10.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(14.0),(10.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(11.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(7.0),(11.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(10.0),(11.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(4.0),(12.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(12.0),(12.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(0.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(1.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(2.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(3.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(4.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(5.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(6.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(7.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(8.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(9.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(10.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(11.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(12.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(13.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(14.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(15.0),(13.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(6.0)),(6.0),(14.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(6.0)),(14.0),(14.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(2.0),(21.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(5.0),(21.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(7.0),(21.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(10.0),(21.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(12.0),(21.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(15.0),(21.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(22.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(9.0),(22.0));
        g_poke(D_pat,LEN_pat,CH_pat,(1.0),(10.0),(22.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(4.0),(23.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(12.0),(23.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(0.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(2.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(4.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(6.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(8.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(10.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(12.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(14.0),(24.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(5.0)),(14.0),(25.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(12.0),(27.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(7.0),(32.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(14.0),(32.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(33.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(4.0),(33.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(8.0),(33.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(12.0),(33.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(2.0),(35.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(6.0),(35.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(10.0),(35.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(14.0),(35.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(2.0),(36.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(6.0),(36.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(10.0),(36.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(14.0),(36.0));
        g_poke(D_pat,LEN_pat,CH_pat,(8.0),(4.0),(37.0));
        g_poke(D_pat,LEN_pat,CH_pat,(8.0),(12.0),(37.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(0.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(2.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(4.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(6.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(8.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(10.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(12.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(14.0),(42.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(3.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(4.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(7.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(8.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(11.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(12.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(15.0),(44.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(0.0),(49.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(3.0),(49.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(6.0),(49.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(10.0),(49.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(13.0),(49.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(0.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(1.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(2.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(3.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(4.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(5.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(6.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(7.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(8.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(9.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(10.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(11.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(12.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(13.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(14.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(15.0),(53.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(14.0),(54.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(0.0),(55.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(8.0),(55.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(6.0)),(14.0),(56.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(4.0),(57.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(12.0),(57.0));
        g_poke(D_pat,LEN_pat,CH_pat,(8.0),(0.0),(63.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(4.0),(63.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(6.0),(63.0));
        g_poke(D_pat,LEN_pat,CH_pat,(8.0),(8.0),(63.0));
        g_poke(D_pat,LEN_pat,CH_pat,(6.0),(12.0),(63.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(14.0),(63.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(2.0),(65.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(7.0),(65.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(9.0),(65.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(12.0),(65.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(8.0),(66.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(0.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(2.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(4.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(6.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(8.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(10.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(12.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(5.0),(14.0),(68.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(6.0)),(12.0),(69.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(8.0),(71.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(1.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(3.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(5.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(7.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(9.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(11.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(13.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(15.0),(75.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(77.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(4.0)),(8.0),(82.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(0.0),(83.0));
        g_poke(D_pat,LEN_pat,CH_pat,(9.0),(10.0),(83.0));
        g_poke(D_pat,LEN_pat,CH_pat,(7.0),(6.0),(84.0));
        g_poke(D_pat,LEN_pat,CH_pat,(-(3.0)),(0.0),(85.0));
        g_poke(D_pat,LEN_pat,CH_pat,(4.0),(0.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(2.0),(2.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(4.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(2.0),(6.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(4.0),(8.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(2.0),(10.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(3.0),(12.0),(86.0));
        g_poke(D_pat,LEN_pat,CH_pat,(2.0),(14.0),(86.0));
        H_d_init = (1.0);
      }
      v_bp = IN1;
      v_tr = IN2;
      v_hasloop = ((double)((IN3) > ((0.0))));
      v_sty = floor(((P_drm_style / (100.0)) * (7.99)));
      v_kit = floor(((P_drm_kit / (100.0)) * (3.99)));
      v_busy = (P_drm_busy / (100.0));
      v_bts = P_beats;
      v_play = ((double)((((double)((P_drm_on) > ((0.0))))) && (((double)((((double)((v_tr) > ((0.0))))) || (((double)((v_hasloop) == ((0.0))))))))));
      v_e8 = (v_bp * (2.0));
      v_i8 = floor(v_e8);
      v_f8 = (v_e8 - v_i8);
      v_th = ((0.5) + ((P_drm_swing / (100.0)) * (0.2)));
      v_stp16 = (v_i8 * (2.0));
      if (((double)((v_f8) >= (v_th))) != 0.0) {
        v_stp16 = (v_stp16 + (1.0));
      }
      if (((double)((v_bp) < ((H_d_lbeat - (0.01))))) != 0.0) {
        H_d_lstep = (-(1.0));
      }
      H_d_lbeat = v_bp;
      if (((double)((v_play) == ((0.0)))) != 0.0) {
        H_d_lstep = v_stp16;
      }
      if (((double)((((double)((v_play) > ((0.0))))) && (((double)((v_stp16) != (H_d_lstep)))))) != 0.0) {
        H_d_lstep = v_stp16;
        v_spb = (v_bts * (4.0));
        v_sib = fmod(v_stp16,v_spb);
        v_st16 = fmod(v_sib,(16.0));
        v_bar = floor((v_stp16 / v_spb));
        v_fill = ((double)((((double)((((double)((v_busy) > ((0.75))))) && (((double)((fmod(v_bar,(4.0))) == ((3.0)))))))) && (((double)((v_sib) >= ((v_spb - (4.0))))))));
        for (v_ii = (0.0); ((double)((v_ii) < ((11.0)))) != 0.0; v_ii = v_ii + ((1.0))) {
          v_v = g_peek(D_pat,LEN_pat,CH_pat,v_st16,((v_sty * (11.0)) + v_ii));
          if (((double)((v_v) < ((0.0)))) != 0.0) {
            if (((double)((v_busy) > ((0.55)))) != 0.0) {
              v_v = ((-v_v) * fmin((1.0),((v_busy - (0.55)) * (3.0))));
            } else {
              v_v = (0.0);
            }
          }
          if (((double)((v_ii) == ((2.0)))) != 0.0) {
            v_v = (v_v * g_clamp((v_busy / (0.3)),(0.0),(1.0)));
          }
          if (((double)((((double)((v_busy) < ((0.38))))) && (((double)((((double)((((double)((((double)((((double)((((double)((v_ii) == ((3.0))))) || (((double)((v_ii) == ((5.0)))))))) || (((double)((v_ii) == ((6.0)))))))) || (((double)((v_ii) == ((7.0)))))))) || (((double)((v_ii) == ((8.0)))))))) || (((double)((v_ii) == ((9.0)))))))))) != 0.0) {
            v_v = (0.0);
          }
          if (((double)((((double)((v_busy) < ((0.15))))) && (((double)((v_ii) == ((4.0))))))) != 0.0) {
            v_v = (0.0);
          }
          if (((double)((v_ii) == ((10.0)))) != 0.0) {
            if (((double)((v_busy) > ((0.7)))) != 0.0) {
              v_v = ((v_v * (v_busy - (0.7))) * (3.0));
            } else {
              v_v = (0.0);
            }
          }
          if (((double)((v_fill) && (((double)((((double)((((double)((((double)((v_ii) == ((1.0))))) || (((double)((v_ii) == ((6.0)))))))) || (((double)((v_ii) == ((7.0)))))))) || (((double)((v_ii) == ((10.0)))))))))) != 0.0) {
            v_v = (0.0);
          }
          if (((double)((v_v) > ((0.0)))) != 0.0) {
            v_vi = v_ii;
            if (((double)((v_ii) == ((10.0)))) != 0.0) {
              v_vi = (1.0);
              v_v = (v_v * (0.35));
            }
            v_vv = ((v_v / (9.0)) * ((0.88) + ((0.12) * g_noise())));
            g_poke(D_env,LEN_env,CH_env,fmax(g_peek(D_env,LEN_env,CH_env,v_vi,0),v_vv),v_vi,0);
            g_poke(D_vel,LEN_vel,CH_vel,v_vv,v_vi,0);
            g_poke(D_pen,LEN_pen,CH_pen,(1.0),v_vi,0);
            if (((double)((v_vi) == ((2.0)))) != 0.0) {
              g_poke(D_env,LEN_env,CH_env,(0.0),(3.0),0);
            }
          }
        }
        if (v_fill != 0.0) {
          v_fi = (v_sib - (v_spb - (4.0)));
          v_fv = ((0.55) + (v_fi * (0.13)));
          v_fvi = (1.0);
          if (((double)((v_fi) == ((1.0)))) != 0.0) {
            v_fvi = (7.0);
          }
          if (((double)((v_fi) >= ((2.0)))) != 0.0) {
            v_fvi = (6.0);
          }
          g_poke(D_env,LEN_env,CH_env,v_fv,v_fvi,0);
          g_poke(D_vel,LEN_vel,CH_vel,v_fv,v_fvi,0);
          g_poke(D_pen,LEN_pen,CH_pen,(1.0),v_fvi,0);
        }
      }
      H_d_run = v_play;
      v_sr = SR;
      v_nz = g_noise();
      v_kf0 = (55.0);
      v_kf1 = (150.0);
      v_kpd = (25.0);
      v_kad = (350.0);
      v_kcl = (0.5);
      v_kdr = (1.2);
      v_sf1 = (190.0);
      v_sf2 = (280.0);
      v_sad = (140.0);
      v_snd = (230.0);
      v_snm = (0.75);
      v_hcd = (45.0);
      v_hod = (320.0);
      v_hmet = (0.4);
      if (((double)((v_kit) == ((1.0)))) != 0.0) {
        v_kf0 = (47.0);
        v_kf1 = (95.0);
        v_kpd = (70.0);
        v_kad = (600.0);
        v_kcl = (0.05);
        v_kdr = (1.6);
        v_sf1 = (180.0);
        v_sf2 = (330.0);
        v_sad = (120.0);
        v_snd = (160.0);
        v_snm = (0.55);
        v_hcd = (40.0);
        v_hod = (420.0);
        v_hmet = (1.0);
      }
      if (((double)((v_kit) == ((2.0)))) != 0.0) {
        v_kf0 = (52.0);
        v_kf1 = (230.0);
        v_kpd = (18.0);
        v_kad = (480.0);
        v_kcl = (0.8);
        v_kdr = (1.4);
        v_sf1 = (200.0);
        v_sf2 = (350.0);
        v_sad = (110.0);
        v_snd = (260.0);
        v_snm = (0.85);
        v_hcd = (50.0);
        v_hod = (380.0);
        v_hmet = (0.7);
      }
      if (((double)((v_kit) == ((3.0)))) != 0.0) {
        v_kf0 = (55.0);
        v_kf1 = (120.0);
        v_kpd = (35.0);
        v_kad = (300.0);
        v_kcl = (0.3);
        v_kdr = (2.0);
        v_sad = (120.0);
        v_snd = (180.0);
        v_snm = (0.7);
        v_hcd = (35.0);
        v_hod = (260.0);
        v_hmet = (0.5);
      }
      v_ev = g_peek(D_env,LEN_env,CH_env,(0.0),0);
      v_kick = (0.0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_pe = g_peek(D_pen,LEN_pen,CH_pen,(0.0),0);
        v_f = (v_kf0 + ((v_kf1 - v_kf0) * v_pe));
        v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,(0.0),0) + (v_f / v_sr)),(0.0),(1.0));
        g_poke(D_vph,LEN_vph,CH_vph,v_ph,(0.0),0);
        v_cl = ((((((((((v_pe * v_pe) * v_pe) * v_pe) * v_pe) * v_pe) * v_pe) * v_pe) * sin((((2.0*M_PI) * v_ph) * (23.0)))) * v_kcl) * (0.6));
        v_kick = (tanh((((sin(((2.0*M_PI) * v_ph)) * v_ev) + (v_cl * v_ev)) * v_kdr)) / tanh(v_kdr));
        g_poke(D_pen,LEN_pen,CH_pen,(v_pe * exp(((-(1.0)) / ((v_kpd)*SR/1000.0)))),(0.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / ((v_kad)*SR/1000.0)))),(0.0),0);
      } else {
        g_poke(D_vph,LEN_vph,CH_vph,(0.0),(0.0),0);
      }
      v_ev = g_peek(D_env,LEN_env,CH_env,(1.0),0);
      v_snare = (0.0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_pe = g_peek(D_pen,LEN_pen,CH_pen,(1.0),0);
        v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,(1.0),0) + ((v_sf1 * ((1.0) + (((0.5) * v_pe) * v_pe))) / v_sr)),(0.0),(1.0));
        g_poke(D_vph,LEN_vph,CH_vph,v_ph,(1.0),0);
        v_tone = ((sin(((2.0*M_PI) * v_ph)) + ((0.6) * sin(((((2.0*M_PI) * v_ph) * v_sf2) / v_sf1)))) * v_pe);
        v_sbh = ((0.86) * ((H_d_sbh + v_nz) - H_d_sbx));
        H_d_sbx = v_nz;
        H_d_sbh = v_sbh;
        v_bn = (H_d_sbl + ((v_sbh - H_d_sbl) * (0.6)));
        H_d_sbl = v_bn;
        v_snare = (((v_tone * ((1.0) - v_snm)) + (((v_bn * v_snm) * (1.3)) * ((0.4) + (((0.6) * v_ev) / fmax((0.001),g_peek(D_vel,LEN_vel,CH_vel,(1.0),0)))))) * v_ev);
        g_poke(D_pen,LEN_pen,CH_pen,(v_pe * exp(((-(1.0)) / ((v_sad)*SR/1000.0)))),(1.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / ((v_snd)*SR/1000.0)))),(1.0),0);
      }
      v_hm = (0.0);
      if (((double)((((double)((((double)((g_peek(D_env,LEN_env,CH_env,(2.0),0)) > ((0.0001))))) || (((double)((g_peek(D_env,LEN_env,CH_env,(3.0),0)) > ((0.0001)))))))) || (((double)((g_peek(D_env,LEN_env,CH_env,(8.0),0)) > ((0.0001))))))) != 0.0) {
        for (v_j = (0.0); ((double)((v_j) < ((6.0)))) != 0.0; v_j = v_j + ((1.0))) {
          v_rt = (2.0);
          if (((double)((v_j) == ((1.0)))) != 0.0) {
            v_rt = (3.0);
          }
          if (((double)((v_j) == ((2.0)))) != 0.0) {
            v_rt = (4.16);
          }
          if (((double)((v_j) == ((3.0)))) != 0.0) {
            v_rt = (5.43);
          }
          if (((double)((v_j) == ((4.0)))) != 0.0) {
            v_rt = (6.79);
          }
          if (((double)((v_j) == ((5.0)))) != 0.0) {
            v_rt = (8.21);
          }
          v_q = g_wrap((g_peek(D_hp6,LEN_hp6,CH_hp6,v_j,0) + ((((40.0) * v_rt) * (1.53)) / v_sr)),(0.0),(1.0));
          g_poke(D_hp6,LEN_hp6,CH_hp6,v_q,v_j,0);
          v_hm = ((v_hm + ((double)((v_q) < ((0.5))))) - (0.5));
        }
        v_hm = (((v_hm * (0.3)) * v_hmet) + (v_nz * ((1.0) - (v_hmet * (0.6)))));
      }
      v_hlp = (H_d_hlp + ((v_hm - H_d_hlp) * (0.72)));
      H_d_hlp = v_hlp;
      v_hm = v_hlp;
      v_h1 = ((0.97) * ((H_d_hpy + v_hm) - H_d_hpx));
      H_d_hpx = v_hm;
      H_d_hpy = v_h1;
      v_h2 = ((0.97) * ((H_d_hpy2 + v_h1) - H_d_hpx2));
      H_d_hpx2 = v_h1;
      H_d_hpy2 = v_h2;
      v_ev = g_peek(D_env,LEN_env,CH_env,(2.0),0);
      v_hatc = (v_h2 * v_ev);
      g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / ((v_hcd)*SR/1000.0)))),(2.0),0);
      v_ev = g_peek(D_env,LEN_env,CH_env,(3.0),0);
      v_hato = (v_h2 * v_ev);
      g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / ((v_hod)*SR/1000.0)))),(3.0),0);
      v_ev = g_peek(D_env,LEN_env,CH_env,(4.0),0);
      v_clap = (0.0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_pe = g_peek(D_pen,LEN_pen,CH_pen,(4.0),0);
        v_t = (((1.0) - v_pe) * (1000.0));
        v_burst = ((((double)((v_t) < ((1.0)))) + ((double)((((double)((v_t) > ((9.0))))) && (((double)((v_t) < ((10.0)))))))) + ((double)((((double)((v_t) > ((19.0))))) && (((double)((v_t) < ((20.0))))))));
        v_cbh = ((0.9) * ((H_d_cbh + v_nz) - H_d_cbx));
        H_d_cbx = v_nz;
        H_d_cbh = v_cbh;
        v_bn = (H_d_cll + ((v_cbh - H_d_cll) * (0.3)));
        H_d_cll = v_bn;
        v_clap = (((v_bn * v_ev) * ((0.6) + (v_burst * (0.8)))) * (1.6));
        g_poke(D_pen,LEN_pen,CH_pen,fmax((0.0),(v_pe - ((1.0) / (((1000.0))*SR/1000.0)))),(4.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / (((150.0))*SR/1000.0)))),(4.0),0);
      }
      v_ev = g_peek(D_env,LEN_env,CH_env,(5.0),0);
      v_rim = (0.0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,(5.0),0) + ((1750.0) / v_sr)),(0.0),(1.0));
        g_poke(D_vph,LEN_vph,CH_vph,v_ph,(5.0),0);
        v_rim = (((sin(((2.0*M_PI) * v_ph)) * (0.7)) + (v_nz * (0.3))) * v_ev);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / (((28.0))*SR/1000.0)))),(5.0),0);
      }
      v_tl = (0.0);
      v_th2 = (0.0);
      v_ev = g_peek(D_env,LEN_env,CH_env,(6.0),0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_pe = g_peek(D_pen,LEN_pen,CH_pen,(6.0),0);
        v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,(6.0),0) + (((95.0) + ((45.0) * v_pe)) / v_sr)),(0.0),(1.0));
        g_poke(D_vph,LEN_vph,CH_vph,v_ph,(6.0),0);
        v_tl = ((sin(((2.0*M_PI) * v_ph)) + ((v_nz * (0.08)) * v_pe)) * v_ev);
        g_poke(D_pen,LEN_pen,CH_pen,(v_pe * exp(((-(1.0)) / (((60.0))*SR/1000.0)))),(6.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / (((380.0))*SR/1000.0)))),(6.0),0);
      }
      v_ev = g_peek(D_env,LEN_env,CH_env,(7.0),0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_pe = g_peek(D_pen,LEN_pen,CH_pen,(7.0),0);
        v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,(7.0),0) + (((150.0) + ((60.0) * v_pe)) / v_sr)),(0.0),(1.0));
        g_poke(D_vph,LEN_vph,CH_vph,v_ph,(7.0),0);
        v_th2 = ((sin(((2.0*M_PI) * v_ph)) + ((v_nz * (0.08)) * v_pe)) * v_ev);
        g_poke(D_pen,LEN_pen,CH_pen,(v_pe * exp(((-(1.0)) / (((50.0))*SR/1000.0)))),(7.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / (((300.0))*SR/1000.0)))),(7.0),0);
      }
      v_ev = g_peek(D_env,LEN_env,CH_env,(8.0),0);
      v_ride = (0.0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,(8.0),0) + ((2480.0) / v_sr)),(0.0),(1.0));
        g_poke(D_vph,LEN_vph,CH_vph,v_ph,(8.0),0);
        v_rb2 = (H_d_rdy + ((v_hm - H_d_rdy) * (0.25)));
        H_d_rdy = v_rb2;
        v_ride = (((v_h1 * (0.6)) + ((sin(((2.0*M_PI) * v_ph)) * (0.25)) * g_peek(D_pen,LEN_pen,CH_pen,(8.0),0))) * v_ev);
        g_poke(D_pen,LEN_pen,CH_pen,(g_peek(D_pen,LEN_pen,CH_pen,(8.0),0) * exp(((-(1.0)) / (((120.0))*SR/1000.0)))),(8.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / (((1100.0))*SR/1000.0)))),(8.0),0);
      }
      v_ev = g_peek(D_env,LEN_env,CH_env,(9.0),0);
      v_shk = (0.0);
      if (((double)((v_ev) > ((0.0001)))) != 0.0) {
        v_pe = g_peek(D_pen,LEN_pen,CH_pen,(9.0),0);
        v_shk = (((v_h2 * v_ev) * ((1.0) - (v_pe * v_pe))) * (1.4));
        g_poke(D_pen,LEN_pen,CH_pen,(v_pe * exp(((-(1.0)) / (((6.0))*SR/1000.0)))),(9.0),0);
        g_poke(D_env,LEN_env,CH_env,(v_ev * exp(((-(1.0)) / (((70.0))*SR/1000.0)))),(9.0),0);
      }
      v_L = ((((((((((v_kick * (0.9)) + (v_snare * (0.5))) + ((v_hatc * (0.22)) * (1.1))) + (v_hato * (0.2))) + (v_clap * (0.45))) + (v_rim * (0.35))) + (v_tl * (0.6))) + (v_th2 * (0.45))) + (v_ride * (0.25))) + (v_shk * (0.25)));
      v_R = ((((((((((v_kick * (0.9)) + (v_snare * (0.5))) + ((v_hatc * (0.22)) * (0.8))) + (v_hato * (0.24))) + (v_clap * (0.45))) + (v_rim * (0.3))) + (v_tl * (0.45))) + (v_th2 * (0.6))) + (v_ride * (0.35))) + (v_shk * (0.3)));
      v_lc = ((1.0) - exp((((-(2.0*M_PI)) * ((1500.0) * pow((12.0),(P_drm_tone / (100.0))))) / v_sr)));
      v_b1 = (H_d_bus1 + ((v_L - H_d_bus1) * v_lc));
      H_d_bus1 = v_b1;
      v_b2 = (H_d_bus2 + ((v_R - H_d_bus2) * v_lc));
      H_d_bus2 = v_b2;
      if (((double)((v_kit) == ((3.0)))) != 0.0) {
        v_hc = (H_d_hcnt + (1.0));
        if (((double)((v_hc) >= ((3.0)))) != 0.0) {
          v_hc = (0.0);
          H_d_hold = v_b1;
          H_d_crush = v_b2;
        }
        H_d_hcnt = v_hc;
        v_b1 = (floor((tanh((H_d_hold * (1.5))) * (128.0))) / (128.0));
        v_b2 = (floor((tanh((H_d_crush * (1.5))) * (128.0))) / (128.0));
      }
      v_lv = ((0.9) * pow((P_drm_lvl / (100.0)),(1.6)));
      v_o1 = (v_b1 * v_lv);
      v_o2 = (v_b2 * v_lv);
      v_pk = fmax(fabs(v_o1),fabs(v_o2));
      v_lg = H_d_lim;
      if (((double)(((v_pk * v_lg)) > ((0.5)))) != 0.0) {
        v_lg = ((0.5) / v_pk);
      } else {
        v_lg = fmin((1.0),(v_lg + (((1.0) - v_lg) * ((1.0) - exp(((-(1.0)) / (((150.0))*SR/1000.0)))))));
      }
      H_d_lim = v_lg;
      OUT1 = (v_o1 * v_lg);
      OUT2 = (v_o2 * v_lg);
      OUT3 = v_sty;
      OUT4 = v_kit;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
    }
  }
};
// ---------------------------------------------------------------- Shadow
struct Shadow {
  double SR = 44100.0;
  double P_shd_v0 = 0.0;
  double P_shd_v1 = 1.0;
  double P_shd_v2 = 0.0;
  double P_shd_v3 = 0.0;
  double P_shd_v4 = 0.0;
  double P_shd_v5 = 0.0;
  double P_shd_v6 = 0.0;
  double P_shd_v7 = 0.0;
  double P_shd_v8 = 0.0;
  double P_shd_v9 = 0.0;
  double P_shd_v10 = 0.0;
  double P_shd_v11 = 0.0;
  double P_shd_tone = 45.0;
  double P_shd_feel = 30.0;
  double P_shd_pan = 35.0;
  double P_shd_spread = 40.0;
  std::vector<double> M_dl; DL DL_dl;
  std::vector<double> V_sc; double* D_sc = nullptr; long LEN_sc = 7; long CH_sc = 1;
  std::vector<double> V_von; double* D_von = nullptr; long LEN_von = 12; long CH_von = 1;
  std::vector<double> V_vfix; double* D_vfix = nullptr; long LEN_vfix = 12; long CH_vfix = 1;
  std::vector<double> V_vdeg; double* D_vdeg = nullptr; long LEN_vdeg = 12; long CH_vdeg = 1;
  std::vector<double> V_vph; double* D_vph = nullptr; long LEN_vph = 12; long CH_vph = 1;
  std::vector<double> V_vsm; double* D_vsm = nullptr; long LEN_vsm = 12; long CH_vsm = 1;
  std::vector<double> V_vg; double* D_vg = nullptr; long LEN_vg = 12; long CH_vg = 1;
  std::vector<double> V_vlat; double* D_vlat = nullptr; long LEN_vlat = 12; long CH_vlat = 1;
  std::vector<double> V_vdr; double* D_vdr = nullptr; long LEN_vdr = 12; long CH_vdr = 1;
  std::vector<double> V_vdt; double* D_vdt = nullptr; long LEN_vdt = 12; long CH_vdt = 1;
  double H_s_w = 2880.0;
  double H_s_lnote = -1.0;
  double H_s_lp1 = 0.0;
  double H_s_lp2 = 0.0;
  double H_s_lp3 = 0.0;
  double H_s_lp4 = 0.0;
  double H_s_hpl = 0.0;
  double H_s_hxl = 0.0;
  double H_s_hpr = 0.0;
  double H_s_hxr = 0.0;
  double H_s_ton = 0.5;
  double H_s_pan = 0.35;
  double H_s_spr = 0.4;
  double H_s_nrm = 1.0;
  double H_s_init = 0.0;
  static constexpr int numInputs = 3;
  static constexpr int numOutputs = 2;
  void init(double sr) { SR = sr;
    M_dl.assign(144000, 0.0); DL_dl.b = M_dl.data(); DL_dl.n = 144000; DL_dl.w = 0;
    V_sc.assign((size_t) 7 * 1, 0.0); D_sc = V_sc.data();
    V_von.assign((size_t) 12 * 1, 0.0); D_von = V_von.data();
    V_vfix.assign((size_t) 12 * 1, 0.0); D_vfix = V_vfix.data();
    V_vdeg.assign((size_t) 12 * 1, 0.0); D_vdeg = V_vdeg.data();
    V_vph.assign((size_t) 12 * 1, 0.0); D_vph = V_vph.data();
    V_vsm.assign((size_t) 12 * 1, 0.0); D_vsm = V_vsm.data();
    V_vg.assign((size_t) 12 * 1, 0.0); D_vg = V_vg.data();
    V_vlat.assign((size_t) 12 * 1, 0.0); D_vlat = V_vlat.data();
    V_vdr.assign((size_t) 12 * 1, 0.0); D_vdr = V_vdr.data();
    V_vdt.assign((size_t) 12 * 1, 0.0); D_vdt = V_vdt.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "shd_v0") return 0;
    if (n == "shd_v1") return 1;
    if (n == "shd_v2") return 2;
    if (n == "shd_v3") return 3;
    if (n == "shd_v4") return 4;
    if (n == "shd_v5") return 5;
    if (n == "shd_v6") return 6;
    if (n == "shd_v7") return 7;
    if (n == "shd_v8") return 8;
    if (n == "shd_v9") return 9;
    if (n == "shd_v10") return 10;
    if (n == "shd_v11") return 11;
    if (n == "shd_tone") return 12;
    if (n == "shd_feel") return 13;
    if (n == "shd_pan") return 14;
    if (n == "shd_spread") return 15;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v0 = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v1 = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v2 = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v3 = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v4 = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v5 = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v6 = v; break;
      case 7: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v7 = v; break;
      case 8: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v8 = v; break;
      case 9: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v9 = v; break;
      case 10: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v10 = v; break;
      case 11: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_shd_v11 = v; break;
      case 12: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd_tone = v; break;
      case 13: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd_feel = v; break;
      case 14: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd_pan = v; break;
      case 15: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd_spread = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_shd_v0;
      case 1: return P_shd_v1;
      case 2: return P_shd_v2;
      case 3: return P_shd_v3;
      case 4: return P_shd_v4;
      case 5: return P_shd_v5;
      case 6: return P_shd_v6;
      case 7: return P_shd_v7;
      case 8: return P_shd_v8;
      case 9: return P_shd_v9;
      case 10: return P_shd_v10;
      case 11: return P_shd_v11;
      case 12: return P_shd_tone;
      case 13: return P_shd_feel;
      case 14: return P_shd_pan;
      case 15: return P_shd_spread;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 16;
  double* data(const std::string& n) {
    if (n == "sc") return D_sc;
    if (n == "von") return D_von;
    if (n == "vfix") return D_vfix;
    if (n == "vdeg") return D_vdeg;
    if (n == "vph") return D_vph;
    if (n == "vsm") return D_vsm;
    if (n == "vg") return D_vg;
    if (n == "vlat") return D_vlat;
    if (n == "vdr") return D_vdr;
    if (n == "vdt") return D_vdt;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double v_v = 0.0;
      double v_x = 0.0;
      double v_pit = 0.0;
      double v_key = 0.0;
      double v_pn = 0.0;
      double v_rel = 0.0;
      double v_d0 = 0.0;
      double v_j = 0.0;
      double v_tw = 0.0;
      double v_per = 0.0;
      double v_n = 0.0;
      double v_w = 0.0;
      double v_fe = 0.0;
      double v_gk = 0.0;
      double v_ik = 0.0;
      double v_pn2 = 0.0;
      double v_spr = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_tot = 0.0;
      double v_cnt = 0.0;
      double v_vonv = 0.0;
      double v_g = 0.0;
      double v_tsemi = 0.0;
      double v_dg = 0.0;
      double v_dt = 0.0;
      double v_oc = 0.0;
      double v_semi = 0.0;
      double v_vlt = 0.0;
      double v_dr = 0.0;
      double v_ratio = 0.0;
      double v_ph = 0.0;
      double v_p2 = 0.0;
      double v_y = 0.0;
      double v_pv = 0.0;
      double v_nrm = 0.0;
      double v_tn = 0.0;
      double v_fc = 0.0;
      double v_a = 0.0;
      double v_l1 = 0.0;
      double v_l2 = 0.0;
      double v_r1 = 0.0;
      double v_r2 = 0.0;
      double v_hl = 0.0;
      double v_hr = 0.0;
      if (((double)((H_s_init) == ((0.0)))) != 0.0) {
        g_poke(D_sc,LEN_sc,CH_sc,(0.0),(0.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(2.0),(1.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(4.0),(2.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(5.0),(3.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(7.0),(4.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(9.0),(5.0),0);
        g_poke(D_sc,LEN_sc,CH_sc,(11.0),(6.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(-(24.0)),(0.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(-(12.0)),(1.0),0);
        g_poke(D_vdeg,LEN_vdeg,CH_vdeg,(-(5.0)),(2.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(-(7.0)),(3.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(-(5.0)),(4.0),0);
        g_poke(D_vdeg,LEN_vdeg,CH_vdeg,(-(2.0)),(5.0),0);
        g_poke(D_vdeg,LEN_vdeg,CH_vdeg,(2.0),(6.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(5.0),(7.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(7.0),(8.0),0);
        g_poke(D_vdeg,LEN_vdeg,CH_vdeg,(5.0),(9.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(12.0),(10.0),0);
        g_poke(D_vfix,LEN_vfix,CH_vfix,(24.0),(11.0),0);
        for (v_v = (0.0); ((double)((v_v) < ((12.0)))) != 0.0; v_v = v_v + ((1.0))) {
          g_poke(D_vph,LEN_vph,CH_vph,(v_v / (12.0)),v_v,0);
          g_poke(D_vsm,LEN_vsm,CH_vsm,(g_peek(D_vfix,LEN_vfix,CH_vfix,v_v,0) + (g_peek(D_vdeg,LEN_vdeg,CH_vdeg,v_v,0) * (1.7))),v_v,0);
          g_poke(D_vlat,LEN_vlat,CH_vlat,(((2.0))*SR/1000.0),v_v,0);
        }
        H_s_init = (1.0);
      }
      g_poke(D_von,LEN_von,CH_von,P_shd_v0,(0.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v1,(1.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v2,(2.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v3,(3.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v4,(4.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v5,(5.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v6,(6.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v7,(7.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v8,(8.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v9,(9.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v10,(10.0),0);
      g_poke(D_von,LEN_von,CH_von,P_shd_v11,(11.0),0);
      v_x = IN1;
      v_pit = IN2;
      v_key = IN3;
      dl_write(&DL_dl,v_x);
      v_pn = H_s_lnote;
      if (((double)((v_pit) > ((0.0)))) != 0.0) {
        v_pn = floor((v_pit + (0.5)));
      }
      H_s_lnote = v_pn;
      v_rel = (-(1.0));
      v_d0 = (0.0);
      if (((double)((((double)((v_pn) >= ((0.0))))) && (((double)((v_key) >= ((0.0))))))) != 0.0) {
        v_rel = fmod(((v_pn - v_key) + (120.0)),(12.0));
        for (v_j = (0.0); ((double)((v_j) < ((7.0)))) != 0.0; v_j = v_j + ((1.0))) {
          if (((double)((g_peek(D_sc,LEN_sc,CH_sc,v_j,0)) <= (v_rel))) != 0.0) {
            v_d0 = v_j;
          }
        }
      }
      v_tw = (((60.0))*SR/1000.0);
      if (((double)((v_pit) > ((20.0)))) != 0.0) {
        v_per = (SR / ((440.0) * pow((2.0),((v_pit - (69.0)) / (12.0)))));
        v_n = ceil(((((22.0))*SR/1000.0) / v_per));
        v_tw = (((2.0) * v_per) * v_n);
      }
      v_tw = g_clamp(v_tw,(((24.0))*SR/1000.0),(((110.0))*SR/1000.0));
      v_w = (H_s_w + ((v_tw - H_s_w) * (0.0008)));
      H_s_w = v_w;
      v_fe = (P_shd_feel / (100.0));
      v_gk = ((1.0) - exp(((-(1.0)) / (((20.0))*SR/1000.0))));
      v_ik = ((1.0) - exp(((-(1.0)) / (((25.0))*SR/1000.0))));
      v_pn2 = (H_s_pan + (((P_shd_pan / (100.0)) - H_s_pan) * (0.001)));
      H_s_pan = v_pn2;
      v_spr = (H_s_spr + (((P_shd_spread / (100.0)) - H_s_spr) * (0.001)));
      H_s_spr = v_spr;
      v_yl = (0.0);
      v_yr = (0.0);
      v_tot = (0.0);
      v_cnt = (0.0);
      for (v_v = (0.0); ((double)((v_v) < ((12.0)))) != 0.0; v_v = v_v + ((1.0))) {
        v_vonv = ((double)((g_peek(D_von,LEN_von,CH_von,v_v,0)) > ((0.5))));
        if (v_vonv != 0.0) {
          v_cnt = (v_cnt + (1.0));
        }
        if (((double)((v_cnt) > ((10.0)))) != 0.0) {
          v_vonv = (0.0);
        }
        v_g = (g_peek(D_vg,LEN_vg,CH_vg,v_v,0) + ((v_vonv - g_peek(D_vg,LEN_vg,CH_vg,v_v,0)) * v_gk));
        g_poke(D_vg,LEN_vg,CH_vg,v_g,v_v,0);
        if (((double)((v_g) > ((0.0001)))) != 0.0) {
          v_tsemi = g_peek(D_vfix,LEN_vfix,CH_vfix,v_v,0);
          v_dg = g_peek(D_vdeg,LEN_vdeg,CH_vdeg,v_v,0);
          if (((double)((v_dg) != ((0.0)))) != 0.0) {
            if (((double)((v_rel) >= ((0.0)))) != 0.0) {
              v_dt = (v_d0 + v_dg);
              v_oc = floor((v_dt / (7.0)));
              v_tsemi = ((g_peek(D_sc,LEN_sc,CH_sc,(v_dt - (v_oc * (7.0))),0) + ((12.0) * v_oc)) - v_rel);
            } else {
              v_tsemi = (4.0);
              if (((double)((v_dg) == ((-(2.0))))) != 0.0) {
                v_tsemi = (-(3.0));
              }
              if (((double)((v_dg) == ((5.0)))) != 0.0) {
                v_tsemi = (9.0);
              }
              if (((double)((v_dg) == ((-(5.0))))) != 0.0) {
                v_tsemi = (-(8.0));
              }
            }
          }
          v_semi = (g_peek(D_vsm,LEN_vsm,CH_vsm,v_v,0) + ((v_tsemi - g_peek(D_vsm,LEN_vsm,CH_vsm,v_v,0)) * v_ik));
          g_poke(D_vsm,LEN_vsm,CH_vsm,v_semi,v_v,0);
          v_vlt = (g_peek(D_vlat,LEN_vlat,CH_vlat,v_v,0) + ((((((2.0) + (v_fe * ((20.0) + ((3.0) * v_v)))))*SR/1000.0) - g_peek(D_vlat,LEN_vlat,CH_vlat,v_v,0)) * (0.0005)));
          g_poke(D_vlat,LEN_vlat,CH_vlat,v_vlt,v_v,0);
          if (((double)((g_noise()) > ((0.9996)))) != 0.0) {
            g_poke(D_vdt,LEN_vdt,CH_vdt,((g_noise() * v_fe) * (9.0)),v_v,0);
          }
          v_dr = (g_peek(D_vdr,LEN_vdr,CH_vdr,v_v,0) + ((g_peek(D_vdt,LEN_vdt,CH_vdt,v_v,0) - g_peek(D_vdr,LEN_vdr,CH_vdr,v_v,0)) * (4e-05)));
          g_poke(D_vdr,LEN_vdr,CH_vdr,v_dr,v_v,0);
          v_ratio = pow((2.0),((v_semi + (v_dr / (100.0))) / (12.0)));
          v_ph = g_wrap((g_peek(D_vph,LEN_vph,CH_vph,v_v,0) + (((1.0) - v_ratio) / v_w)),(0.0),(1.0));
          g_poke(D_vph,LEN_vph,CH_vph,v_ph,v_v,0);
          v_p2 = g_wrap((v_ph + (0.5)),(0.0),(1.0));
          v_y = ((dl_read(&DL_dl,(v_vlt + (v_ph * v_w))) * ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_ph))))) + (dl_read(&DL_dl,(v_vlt + (v_p2 * v_w))) * ((0.5) - ((0.5) * cos(((2.0*M_PI) * v_p2))))));
          v_y = (v_y * v_g);
          v_pv = g_clamp((v_pn2 + (v_spr * ((v_v / (11.0)) - (0.5)))),(0.0),(1.0));
          v_yl = (v_yl + (v_y * cos(((v_pv * M_PI) * (0.5)))));
          v_yr = (v_yr + (v_y * sin(((v_pv * M_PI) * (0.5)))));
          v_tot = (v_tot + v_g);
        }
      }
      v_nrm = (H_s_nrm + ((((1.0) / sqrt(fmax(0.0,fmax((1.0),v_tot)))) - H_s_nrm) * (0.001)));
      H_s_nrm = v_nrm;
      v_yl = (v_yl * v_nrm);
      v_yr = (v_yr * v_nrm);
      v_tn = (H_s_ton + (((P_shd_tone / (100.0)) - H_s_ton) * (0.001)));
      H_s_ton = v_tn;
      v_fc = ((300.0) * pow((50.0),v_tn));
      v_a = ((1.0) - exp((((-(2.0*M_PI)) * v_fc) / SR)));
      v_l1 = (H_s_lp1 + ((v_yl - H_s_lp1) * v_a));
      H_s_lp1 = v_l1;
      v_l2 = (H_s_lp2 + ((v_l1 - H_s_lp2) * v_a));
      H_s_lp2 = v_l2;
      v_r1 = (H_s_lp3 + ((v_yr - H_s_lp3) * v_a));
      H_s_lp3 = v_r1;
      v_r2 = (H_s_lp4 + ((v_r1 - H_s_lp4) * v_a));
      H_s_lp4 = v_r2;
      v_hl = ((0.996) * ((H_s_hpl + v_l2) - H_s_hxl));
      H_s_hxl = v_l2;
      H_s_hpl = v_hl;
      v_hr = ((0.996) * ((H_s_hpr + v_r2) - H_s_hxr));
      H_s_hxr = v_r2;
      H_s_hpr = v_hr;
      OUT1 = v_hl;
      OUT2 = v_hr;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      DL_dl.w = (DL_dl.w + 1) % DL_dl.n;
    }
  }
};
// ---------------------------------------------------------------- Orbit
struct Orbit {
  double SR = 44100.0;
  double P_orb_ang = 0.0;
  double P_orb_dist = 30.0;
  double P_orb_speed = 25.0;
  double P_orb_shape = 0.0;
  double P_orb_path = 0.0;
  double P_orb_multi = 0.0;
  std::vector<double> M_prop; DL DL_prop;
  std::vector<double> M_itd; DL DL_itd;
  std::vector<double> M_a1; DL DL_a1;
  std::vector<double> M_a2; DL DL_a2;
  std::vector<double> M_a3; DL DL_a3;
  std::vector<double> M_a4; DL DL_a4;
  std::vector<double> M_cl; DL DL_cl;
  std::vector<double> M_cr; DL DL_cr;
  double H_o_ph = 0.0;
  double H_o_ang = 0.0;
  double H_o_dist = 0.3;
  double H_o_shape = 0.0;
  double H_o_spd = 0.0;
  double H_o_dm = 3.0;
  double H_o_lpa = 0.0;
  double H_o_lpb = 0.0;
  double H_o_shl = 0.0;
  double H_o_shr = 0.0;
  double H_o_fl = 0.0;
  double H_o_fr = 0.0;
  double H_o_init = 0.0;
  double H_o_wf = 0.0;
  double H_o_wt = 0.0;
  double H_o_wq = 1.0;
  double H_o_wp = 0.0;
  double H_o_mul = 0.0;
  static constexpr int numInputs = 1;
  static constexpr int numOutputs = 5;
  void init(double sr) { SR = sr;
    M_prop.assign(8192, 0.0); DL_prop.b = M_prop.data(); DL_prop.n = 8192; DL_prop.w = 0;
    M_itd.assign(256, 0.0); DL_itd.b = M_itd.data(); DL_itd.n = 256; DL_itd.w = 0;
    M_a1.assign(1200, 0.0); DL_a1.b = M_a1.data(); DL_a1.n = 1200; DL_a1.w = 0;
    M_a2.assign(1200, 0.0); DL_a2.b = M_a2.data(); DL_a2.n = 1200; DL_a2.w = 0;
    M_a3.assign(1200, 0.0); DL_a3.b = M_a3.data(); DL_a3.n = 1200; DL_a3.w = 0;
    M_a4.assign(1200, 0.0); DL_a4.b = M_a4.data(); DL_a4.n = 1200; DL_a4.w = 0;
    M_cl.assign(6000, 0.0); DL_cl.b = M_cl.data(); DL_cl.n = 6000; DL_cl.w = 0;
    M_cr.assign(6000, 0.0); DL_cr.b = M_cr.data(); DL_cr.n = 6000; DL_cr.w = 0;
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "orb_ang") return 0;
    if (n == "orb_dist") return 1;
    if (n == "orb_speed") return 2;
    if (n == "orb_shape") return 3;
    if (n == "orb_path") return 4;
    if (n == "orb_multi") return 5;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb_ang = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb_dist = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb_speed = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb_shape = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb_path = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_orb_multi = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_orb_ang;
      case 1: return P_orb_dist;
      case 2: return P_orb_speed;
      case 3: return P_orb_shape;
      case 4: return P_orb_path;
      case 5: return P_orb_multi;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 6;
  double* data(const std::string& n) {
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double v_x = 0.0;
      double v_k = 0.0;
      double v_da = 0.0;
      double v_ang = 0.0;
      double v_dist = 0.0;
      double v_shp = 0.0;
      double v_sp = 0.0;
      double v_hz = 0.0;
      double v_spd = 0.0;
      double v_ph = 0.0;
      double v_path = 0.0;
      double v_pos = 0.0;
      double v_wdt = 0.0;
      double v_wq = 0.0;
      double v_wm = 0.0;
      double v_az = 0.0;
      double v_sa = 0.0;
      double v_ca = 0.0;
      double v_base = 0.0;
      double v_dm = 0.0;
      double v_dms = 0.0;
      double v_y = 0.0;
      double v_g = 0.0;
      double v_fc = 0.0;
      double v_aa = 0.0;
      double v_lpa = 0.0;
      double v_lpb = 0.0;
      double v_d = 0.0;
      double v_near = 0.0;
      double v_wd = 0.0;
      double v_gl = 0.0;
      double v_gr = 0.0;
      double v_tl = 0.0;
      double v_tr = 0.0;
      double v_el = 0.0;
      double v_er = 0.0;
      double v_fsl = 0.0;
      double v_fsr = 0.0;
      double v_bl = 0.0;
      double v_br = 0.0;
      double v_shl = 0.0;
      double v_shr = 0.0;
      double v_dirl = 0.0;
      double v_dirr = 0.0;
      double v_scl = 0.0;
      double v_r = 0.0;
      double v_w = 0.0;
      double v_v = 0.0;
      double v_fl = 0.0;
      double v_fr = 0.0;
      double v_room = 0.0;
      double v_omx = 0.0;
      v_x = IN1;
      v_k = (0.0005);
      if (((double)((H_o_init) == ((0.0)))) != 0.0) {
        H_o_ang = (P_orb_ang / (100.0));
        H_o_dist = (P_orb_dist / (100.0));
        H_o_wf = H_o_ang;
        H_o_wt = H_o_ang;
        H_o_wp = H_o_ang;
        H_o_init = (1.0);
      }
      v_da = g_wrap(((P_orb_ang / (100.0)) - H_o_ang),(-(0.5)),(0.5));
      v_ang = g_wrap((H_o_ang + (v_da * v_k)),(0.0),(1.0));
      H_o_ang = v_ang;
      v_dist = (H_o_dist + (((P_orb_dist / (100.0)) - H_o_dist) * v_k));
      H_o_dist = v_dist;
      v_shp = (H_o_shape + (((P_orb_shape / (100.0)) - H_o_shape) * v_k));
      H_o_shape = v_shp;
      v_sp = (P_orb_speed / (100.0));
      v_hz = (0.0);
      if (((double)((v_sp) > ((0.005)))) != 0.0) {
        v_hz = ((0.02) * pow((150.0),v_sp));
      }
      v_spd = (H_o_spd + ((v_hz - H_o_spd) * (5e-05)));
      H_o_spd = v_spd;
      v_ph = g_wrap((H_o_ph + (v_spd / SR)),(0.0),(1.0));
      H_o_ph = v_ph;
      v_path = floor(((P_orb_path / (100.0)) * (2.99)));
      v_pos = g_wrap((v_ph + v_ang),(0.0),(1.0));
      v_wdt = ((0.06) + (v_shp * (0.44)));
      if (((double)((v_path) == ((1.0)))) != 0.0) {
        v_pos = g_wrap((v_ang + (v_wdt * sin(((2.0*M_PI) * v_ph)))),(0.0),(1.0));
      }
      if (((double)((v_path) == ((2.0)))) != 0.0) {
        v_wq = (H_o_wq + (v_spd / SR));
        if (((double)((v_wq) >= ((1.0)))) != 0.0) {
          v_wq = (0.0);
          H_o_wf = H_o_wp;
          H_o_wt = g_wrap((v_ang + (v_wdt * g_noise())),(0.0),(1.0));
        }
        H_o_wq = v_wq;
        if (((double)((v_spd) <= ((0.0)))) != 0.0) {
          H_o_wt = v_ang;
          H_o_wq = (1.0);
          v_wq = (1.0);
        }
        v_wm = ((v_wq * v_wq) * ((3.0) - ((2.0) * v_wq)));
        v_pos = g_wrap((H_o_wf + (g_wrap((H_o_wt - H_o_wf),(-(0.5)),(0.5)) * v_wm)),(0.0),(1.0));
      }
      H_o_wp = v_pos;
      v_az = ((2.0*M_PI) * v_pos);
      v_sa = sin(v_az);
      v_ca = cos(v_az);
      v_base = ((0.5) + ((v_dist * v_dist) * (14.5)));
      v_dm = fmax((0.35),(v_base * ((1.0) - ((((0.75) * v_shp) * v_ca) * ((double)((v_path) == ((0.0))))))));
      v_dms = (H_o_dm + ((v_dm - H_o_dm) * (0.0004)));
      H_o_dm = v_dms;
      dl_write(&DL_prop,v_x);
      v_y = dl_read(&DL_prop,((v_dms / (343.0)) * SR));
      v_g = ((1.6) / ((1.0) + ((0.35) * v_dms)));
      v_fc = ((18000.0) / ((1.0) + (v_dms * (0.22))));
      v_fc = (v_fc * ((1.0) - ((0.55) * fmax((0.0),(-v_ca)))));
      v_aa = ((1.0) - exp((((-(2.0*M_PI)) * v_fc) / SR)));
      v_lpa = (H_o_lpa + ((v_y - H_o_lpa) * v_aa));
      H_o_lpa = v_lpa;
      v_lpb = (H_o_lpb + ((v_lpa - H_o_lpb) * v_aa));
      H_o_lpb = v_lpb;
      v_d = (v_lpb * v_g);
      v_near = g_clamp(((1.5) / v_dms),(0.0),(1.0));
      v_wd = ((0.8) + ((0.2) * v_near));
      v_gl = sqrt(fmax(0.0,((0.5) * ((1.0) - (v_sa * v_wd)))));
      v_gr = sqrt(fmax(0.0,((0.5) * ((1.0) + (v_sa * v_wd)))));
      dl_write(&DL_itd,v_d);
      v_tl = ((fmax((0.0),v_sa) * (0.00066)) * SR);
      v_tr = ((fmax((0.0),(-v_sa)) * (0.00066)) * SR);
      v_el = dl_read(&DL_itd,(v_tl + (1.0)));
      v_er = dl_read(&DL_itd,(v_tr + (1.0)));
      v_fsl = ((20000.0) * pow((0.12),fmax((0.0),v_sa)));
      v_fsr = ((20000.0) * pow((0.12),fmax((0.0),(-v_sa))));
      v_bl = ((1.0) - exp((((-(2.0*M_PI)) * v_fsl) / SR)));
      v_br = ((1.0) - exp((((-(2.0*M_PI)) * v_fsr) / SR)));
      v_shl = (H_o_shl + ((v_el - H_o_shl) * v_bl));
      H_o_shl = v_shl;
      v_shr = (H_o_shr + ((v_er - H_o_shr) * v_br));
      H_o_shr = v_shr;
      v_dirl = (v_shl * v_gl);
      v_dirr = (v_shr * v_gr);
      v_scl = (SR / (48000.0));
      v_r = (v_x * (0.35));
      v_w = dl_read(&DL_a1,((113.0) * v_scl));
      v_v = (v_r + ((0.6) * v_w));
      dl_write(&DL_a1,v_v);
      v_r = (v_w - ((0.6) * v_v));
      v_w = dl_read(&DL_a2,((271.0) * v_scl));
      v_v = (v_r + ((0.6) * v_w));
      dl_write(&DL_a2,v_v);
      v_r = (v_w - ((0.6) * v_v));
      v_w = dl_read(&DL_a3,((389.0) * v_scl));
      v_v = (v_r + ((0.6) * v_w));
      dl_write(&DL_a3,v_v);
      v_r = (v_w - ((0.6) * v_v));
      v_w = dl_read(&DL_a4,((563.0) * v_scl));
      v_v = (v_r + ((0.6) * v_w));
      dl_write(&DL_a4,v_v);
      v_r = (v_w - ((0.6) * v_v));
      v_fl = (H_o_fl + ((dl_read(&DL_cl,((2111.0) * v_scl)) - H_o_fl) * (0.35)));
      H_o_fl = v_fl;
      v_fr = (H_o_fr + ((dl_read(&DL_cr,((2617.0) * v_scl)) - H_o_fr) * (0.35)));
      H_o_fr = v_fr;
      dl_write(&DL_cl,(v_r + (v_fl * (0.55))));
      dl_write(&DL_cr,((v_r * (-(1.0))) + (v_fr * (0.55))));
      v_room = ((0.06) + ((0.22) * v_dist));
      v_omx = (H_o_mul + ((P_orb_multi - H_o_mul) * (0.001)));
      H_o_mul = v_omx;
      OUT1 = ((v_dirl + (v_fl * v_room)) * ((1.0) - v_omx));
      OUT2 = ((v_dirr + (v_fr * v_room)) * ((1.0) - v_omx));
      OUT3 = v_pos;
      OUT4 = v_dms;
      OUT5 = ((v_d + (((v_fl + v_fr) * (0.5)) * v_room)) * v_omx);
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
      DL_prop.w = (DL_prop.w + 1) % DL_prop.n;
      DL_itd.w = (DL_itd.w + 1) % DL_itd.n;
      DL_a1.w = (DL_a1.w + 1) % DL_a1.n;
      DL_a2.w = (DL_a2.w + 1) % DL_a2.n;
      DL_a3.w = (DL_a3.w + 1) % DL_a3.n;
      DL_a4.w = (DL_a4.w + 1) % DL_a4.n;
      DL_cl.w = (DL_cl.w + 1) % DL_cl.n;
      DL_cr.w = (DL_cr.w + 1) % DL_cr.n;
    }
  }
};
// ---------------------------------------------------------------- Master
struct Master {
  double SR = 44100.0;
  double P_ret_cho = 70.0;
  double P_ret_dly = 70.0;
  double P_ret_halo = 70.0;
  double P_ret_frz = 70.0;
  double P_ret_spc = 70.0;
  double P_ret_rev = 70.0;
  double P_ret_res = 70.0;
  double P_ret_grn = 70.0;
  double P_rev_sp = 30.0;
  double P_res_sp = 40.0;
  double P_grn_sp = 40.0;
  double P_ret_fxb = 70.0;
  double P_fxb_sp = 20.0;
  double P_ret_drm = 70.0;
  double P_drm_sp = 10.0;
  double P_ret_shd = 70.0;
  double P_shd_sp = 25.0;
  double P_ret_orb = 70.0;
  double P_orb_sp = 15.0;
  double P_cho_sp = 20.0;
  double P_dly_sp = 30.0;
  double P_halo_sp = 60.0;
  double P_frz_sp = 50.0;
  double P_spc_decay = 55.0;
  double P_spc_size = 60.0;
  double P_spc_tone = 50.0;
  double P_spc_shim = 20.0;
  double P_master = 75.0;
  double P_glue = 25.0;
  double P_guard = 1.0;
  double P_grd_clr = 0.0;
  std::vector<double> M_pre; DL DL_pre;
  std::vector<double> M_ap1; DL DL_ap1;
  std::vector<double> M_ap2; DL DL_ap2;
  std::vector<double> M_ap3; DL DL_ap3;
  std::vector<double> M_ap4; DL DL_ap4;
  std::vector<double> M_d0; DL DL_d0;
  std::vector<double> M_d1; DL DL_d1;
  std::vector<double> M_d2; DL DL_d2;
  std::vector<double> M_d3; DL DL_d3;
  std::vector<double> M_d4; DL DL_d4;
  std::vector<double> M_d5; DL DL_d5;
  std::vector<double> M_d6; DL DL_d6;
  std::vector<double> M_d7; DL DL_d7;
  std::vector<double> M_shd; DL DL_shd;
  double H_lp0 = 0.0;
  double H_m0 = 0.0;
  double H_lp1 = 0.0;
  double H_m1 = 0.125;
  double H_lp2 = 0.0;
  double H_m2 = 0.25;
  double H_lp3 = 0.0;
  double H_m3 = 0.375;
  double H_lp4 = 0.0;
  double H_m4 = 0.5;
  double H_lp5 = 0.0;
  double H_m5 = 0.625;
  double H_lp6 = 0.0;
  double H_m6 = 0.75;
  double H_lp7 = 0.0;
  double H_m7 = 0.875;
  double H_g_s1 = 0.0;
  double H_g_s2 = 0.0;
  double H_g_a = 0.0;
  double H_g_ps = 0.0;
  double H_g_px = 0.0;
  double H_g_pe = 0.0;
  double H_g_cnt = 0.0;
  double H_g_nf1 = 0.0;
  double H_g_nf2 = 0.0;
  double H_g_nd1 = 0.0;
  double H_g_nd2 = 0.0;
  double H_g_old = 0.0;
  double H_g_lclr = 0.0;
  double H_g_ctr = 0.0;
  double H_g_h1 = 0.0;
  double H_g_h2 = 0.0;
  double H_g_h3 = 0.0;
  double H_g_l1x1 = 0.0;
  double H_g_l1x2 = 0.0;
  double H_g_l1y1 = 0.0;
  double H_g_l1y2 = 0.0;
  double H_g_l2x1 = 0.0;
  double H_g_l2x2 = 0.0;
  double H_g_l2y1 = 0.0;
  double H_g_l2y2 = 0.0;
  double H_g_r1x1 = 0.0;
  double H_g_r1x2 = 0.0;
  double H_g_r1y1 = 0.0;
  double H_g_r1y2 = 0.0;
  double H_g_r2x1 = 0.0;
  double H_g_r2x2 = 0.0;
  double H_g_r2y1 = 0.0;
  double H_g_r2y2 = 0.0;
  double H_h_ge = 0.0;
  double H_h_sz = 1.0;
  double H_h_dc = 0.5;
  double H_h_sh = 0.0;
  double H_shph = 0.0;
  double H_shhp = 0.0;
  double H_shx = 0.0;
  double H_h_mv = 0.75;
  static constexpr int numInputs = 26;
  static constexpr int numOutputs = 5;
  void init(double sr) { SR = sr;
    M_pre.assign(20000, 0.0); DL_pre.b = M_pre.data(); DL_pre.n = 20000; DL_pre.w = 0;
    M_ap1.assign(2000, 0.0); DL_ap1.b = M_ap1.data(); DL_ap1.n = 2000; DL_ap1.w = 0;
    M_ap2.assign(2000, 0.0); DL_ap2.b = M_ap2.data(); DL_ap2.n = 2000; DL_ap2.w = 0;
    M_ap3.assign(2000, 0.0); DL_ap3.b = M_ap3.data(); DL_ap3.n = 2000; DL_ap3.w = 0;
    M_ap4.assign(2000, 0.0); DL_ap4.b = M_ap4.data(); DL_ap4.n = 2000; DL_ap4.w = 0;
    M_d0.assign(24000, 0.0); DL_d0.b = M_d0.data(); DL_d0.n = 24000; DL_d0.w = 0;
    M_d1.assign(24000, 0.0); DL_d1.b = M_d1.data(); DL_d1.n = 24000; DL_d1.w = 0;
    M_d2.assign(24000, 0.0); DL_d2.b = M_d2.data(); DL_d2.n = 24000; DL_d2.w = 0;
    M_d3.assign(24000, 0.0); DL_d3.b = M_d3.data(); DL_d3.n = 24000; DL_d3.w = 0;
    M_d4.assign(24000, 0.0); DL_d4.b = M_d4.data(); DL_d4.n = 24000; DL_d4.w = 0;
    M_d5.assign(24000, 0.0); DL_d5.b = M_d5.data(); DL_d5.n = 24000; DL_d5.w = 0;
    M_d6.assign(24000, 0.0); DL_d6.b = M_d6.data(); DL_d6.n = 24000; DL_d6.w = 0;
    M_d7.assign(24000, 0.0); DL_d7.b = M_d7.data(); DL_d7.n = 24000; DL_d7.w = 0;
    M_shd.assign(12000, 0.0); DL_shd.b = M_shd.data(); DL_shd.n = 12000; DL_shd.w = 0;
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "ret_cho") return 0;
    if (n == "ret_dly") return 1;
    if (n == "ret_halo") return 2;
    if (n == "ret_frz") return 3;
    if (n == "ret_spc") return 4;
    if (n == "ret_rev") return 5;
    if (n == "ret_res") return 6;
    if (n == "ret_grn") return 7;
    if (n == "rev_sp") return 8;
    if (n == "res_sp") return 9;
    if (n == "grn_sp") return 10;
    if (n == "ret_fxb") return 11;
    if (n == "fxb_sp") return 12;
    if (n == "ret_drm") return 13;
    if (n == "drm_sp") return 14;
    if (n == "ret_shd") return 15;
    if (n == "shd_sp") return 16;
    if (n == "ret_orb") return 17;
    if (n == "orb_sp") return 18;
    if (n == "cho_sp") return 19;
    if (n == "dly_sp") return 20;
    if (n == "halo_sp") return 21;
    if (n == "frz_sp") return 22;
    if (n == "spc_decay") return 23;
    if (n == "spc_size") return 24;
    if (n == "spc_tone") return 25;
    if (n == "spc_shim") return 26;
    if (n == "master") return 27;
    if (n == "glue") return 28;
    if (n == "guard") return 29;
    if (n == "grd_clr") return 30;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_cho = v; break;
      case 1: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_dly = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_halo = v; break;
      case 3: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_frz = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_spc = v; break;
      case 5: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_rev = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_res = v; break;
      case 7: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_grn = v; break;
      case 8: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_rev_sp = v; break;
      case 9: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_res_sp = v; break;
      case 10: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_grn_sp = v; break;
      case 11: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_fxb = v; break;
      case 12: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_fxb_sp = v; break;
      case 13: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_drm = v; break;
      case 14: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_drm_sp = v; break;
      case 15: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_shd = v; break;
      case 16: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_shd_sp = v; break;
      case 17: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ret_orb = v; break;
      case 18: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_orb_sp = v; break;
      case 19: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_cho_sp = v; break;
      case 20: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_dly_sp = v; break;
      case 21: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_halo_sp = v; break;
      case 22: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_frz_sp = v; break;
      case 23: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc_decay = v; break;
      case 24: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc_size = v; break;
      case 25: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc_tone = v; break;
      case 26: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spc_shim = v; break;
      case 27: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_master = v; break;
      case 28: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_glue = v; break;
      case 29: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_guard = v; break;
      case 30: P_grd_clr = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_ret_cho;
      case 1: return P_ret_dly;
      case 2: return P_ret_halo;
      case 3: return P_ret_frz;
      case 4: return P_ret_spc;
      case 5: return P_ret_rev;
      case 6: return P_ret_res;
      case 7: return P_ret_grn;
      case 8: return P_rev_sp;
      case 9: return P_res_sp;
      case 10: return P_grn_sp;
      case 11: return P_ret_fxb;
      case 12: return P_fxb_sp;
      case 13: return P_ret_drm;
      case 14: return P_drm_sp;
      case 15: return P_ret_shd;
      case 16: return P_shd_sp;
      case 17: return P_ret_orb;
      case 18: return P_orb_sp;
      case 19: return P_cho_sp;
      case 20: return P_dly_sp;
      case 21: return P_halo_sp;
      case 22: return P_frz_sp;
      case 23: return P_spc_decay;
      case 24: return P_spc_size;
      case 25: return P_spc_tone;
      case 26: return P_spc_shim;
      case 27: return P_master;
      case 28: return P_glue;
      case 29: return P_guard;
      case 30: return P_grd_clr;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 31;
  double* data(const std::string& n) {
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double IN4 = in[3] ? (double) in[3][t] : 0.0;
      double IN5 = in[4] ? (double) in[4][t] : 0.0;
      double IN6 = in[5] ? (double) in[5][t] : 0.0;
      double IN7 = in[6] ? (double) in[6][t] : 0.0;
      double IN8 = in[7] ? (double) in[7][t] : 0.0;
      double IN9 = in[8] ? (double) in[8][t] : 0.0;
      double IN10 = in[9] ? (double) in[9][t] : 0.0;
      double IN11 = in[10] ? (double) in[10][t] : 0.0;
      double IN12 = in[11] ? (double) in[11][t] : 0.0;
      double IN13 = in[12] ? (double) in[12][t] : 0.0;
      double IN14 = in[13] ? (double) in[13][t] : 0.0;
      double IN15 = in[14] ? (double) in[14][t] : 0.0;
      double IN16 = in[15] ? (double) in[15][t] : 0.0;
      double IN17 = in[16] ? (double) in[16][t] : 0.0;
      double IN18 = in[17] ? (double) in[17][t] : 0.0;
      double IN19 = in[18] ? (double) in[18][t] : 0.0;
      double IN20 = in[19] ? (double) in[19][t] : 0.0;
      double IN21 = in[20] ? (double) in[20][t] : 0.0;
      double IN22 = in[21] ? (double) in[21][t] : 0.0;
      double IN23 = in[22] ? (double) in[22][t] : 0.0;
      double IN24 = in[23] ? (double) in[23][t] : 0.0;
      double IN25 = in[24] ? (double) in[24][t] : 0.0;
      double IN26 = in[25] ? (double) in[25][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double v_kk = 0.0;
      double v_rsz = 0.0;
      double v_dcy = 0.0;
      double v_shm = 0.0;
      double v_mv = 0.0;
      double v_tdec = 0.0;
      double v_damp = 0.0;
      double v_scl = 0.0;
      double v_cho = 0.0;
      double v_dly = 0.0;
      double v_hal = 0.0;
      double v_frz = 0.0;
      double v_x = 0.0;
      double v_w = 0.0;
      double v_vv = 0.0;
      double v_dlen0 = 0.0;
      double v_o0 = 0.0;
      double v_a0 = 0.0;
      double v_dlen1 = 0.0;
      double v_o1 = 0.0;
      double v_a1 = 0.0;
      double v_dlen2 = 0.0;
      double v_o2 = 0.0;
      double v_a2 = 0.0;
      double v_dlen3 = 0.0;
      double v_o3 = 0.0;
      double v_a3 = 0.0;
      double v_dlen4 = 0.0;
      double v_o4 = 0.0;
      double v_a4 = 0.0;
      double v_dlen5 = 0.0;
      double v_o5 = 0.0;
      double v_a5 = 0.0;
      double v_dlen6 = 0.0;
      double v_o6 = 0.0;
      double v_a6 = 0.0;
      double v_dlen7 = 0.0;
      double v_o7 = 0.0;
      double v_a7 = 0.0;
      double v_b0 = 0.0;
      double v_b1 = 0.0;
      double v_b2 = 0.0;
      double v_b3 = 0.0;
      double v_b4 = 0.0;
      double v_b5 = 0.0;
      double v_b6 = 0.0;
      double v_b7 = 0.0;
      double v_c0 = 0.0;
      double v_c1 = 0.0;
      double v_c2 = 0.0;
      double v_c3 = 0.0;
      double v_c4 = 0.0;
      double v_c5 = 0.0;
      double v_c6 = 0.0;
      double v_c7 = 0.0;
      double v_h0 = 0.0;
      double v_h1 = 0.0;
      double v_h2 = 0.0;
      double v_h3 = 0.0;
      double v_h4 = 0.0;
      double v_h5 = 0.0;
      double v_h6 = 0.0;
      double v_h7 = 0.0;
      double v_wsh = 0.0;
      double v_sp1 = 0.0;
      double v_sp2 = 0.0;
      double v_s1 = 0.0;
      double v_s2 = 0.0;
      double v_upv = 0.0;
      double v_hpv = 0.0;
      double v_f0 = 0.0;
      double v_revl = 0.0;
      double v_revr = 0.0;
      double v_ml = 0.0;
      double v_mr = 0.0;
      double v_mono = 0.0;
      double v_rho = 0.0;
      double v_s0 = 0.0;
      double v_er = 0.0;
      double v_ps = 0.0;
      double v_ga = 0.0;
      double v_px = 0.0;
      double v_pe = 0.0;
      double v_fh = 0.0;
      double v_grow = 0.0;
      double v_howl = 0.0;
      double v_near1 = 0.0;
      double v_near2 = 0.0;
      double v_tg1 = 0.0;
      double v_tg2 = 0.0;
      double v_nd1 = 0.0;
      double v_nd2 = 0.0;
      double v_nw = 0.0;
      double v_nA = 0.0;
      double v_nal = 0.0;
      double v_ncs = 0.0;
      double v_na0 = 0.0;
      double v_ny = 0.0;
      double v_gin = 0.0;
      double v_gen2 = 0.0;
      double v_gth = 0.0;
      double v_grt = 0.0;
      double v_ggt = 0.0;
      double v_gmk = 0.0;
      double v_ggn = 0.0;
      v_kk = (5e-05);
      v_rsz = (H_h_sz + ((((0.5) + ((P_spc_size / (100.0)) * (1.1))) - H_h_sz) * v_kk));
      H_h_sz = v_rsz;
      v_dcy = (H_h_dc + ((((P_spc_decay / (100.0)) - H_h_dc) * v_kk) * (4.0)));
      H_h_dc = v_dcy;
      v_shm = (H_h_sh + (((((P_spc_shim / (100.0)) * (0.5)) - H_h_sh) * v_kk) * (4.0)));
      H_h_sh = v_shm;
      v_mv = (H_h_mv + (((P_master / (100.0)) - H_h_mv) * (0.001)));
      H_h_mv = v_mv;
      v_tdec = ((0.4) + ((v_dcy * v_dcy) * (24.0)));
      v_damp = ((0.12) + (((0.5) * P_spc_tone) / (100.0)));
      v_scl = (SR / (44100.0));
      v_cho = ((IN3 + IN4) * (0.5));
      v_dly = ((IN5 + IN6) * (0.5));
      v_hal = ((IN7 + IN8) * (0.5));
      v_frz = ((IN9 + IN10) * (0.5));
      v_x = (((((((((((IN11 + ((v_cho * P_cho_sp) / (100.0))) + ((v_dly * P_dly_sp) / (100.0))) + ((v_hal * P_halo_sp) / (100.0))) + ((v_frz * P_frz_sp) / (100.0))) + ((((IN13 + IN14) * (0.5)) * P_rev_sp) / (100.0))) + ((((IN15 + IN16) * (0.5)) * P_res_sp) / (100.0))) + ((((IN17 + IN18) * (0.5)) * P_grn_sp) / (100.0))) + ((((IN19 + IN20) * (0.5)) * P_fxb_sp) / (100.0))) + ((((IN21 + IN22) * (0.5)) * P_drm_sp) / (100.0))) + ((((IN23 + IN24) * (0.5)) * P_shd_sp) / (100.0))) + ((((IN25 + IN26) * (0.5)) * P_orb_sp) / (100.0)));
      dl_write(&DL_pre,v_x);
      v_x = dl_read(&DL_pre,((((10.0) + ((40.0) * v_rsz)))*SR/1000.0));
      v_w = dl_read(&DL_ap1,((142.0) * v_scl));
      v_vv = (v_x + ((0.7) * v_w));
      dl_write(&DL_ap1,v_vv);
      v_x = (v_w - ((0.7) * v_vv));
      v_w = dl_read(&DL_ap2,((107.0) * v_scl));
      v_vv = (v_x + ((0.7) * v_w));
      dl_write(&DL_ap2,v_vv);
      v_x = (v_w - ((0.7) * v_vv));
      v_w = dl_read(&DL_ap3,((379.0) * v_scl));
      v_vv = (v_x + ((0.65) * v_w));
      dl_write(&DL_ap3,v_vv);
      v_x = (v_w - ((0.65) * v_vv));
      v_w = dl_read(&DL_ap4,((277.0) * v_scl));
      v_vv = (v_x + ((0.65) * v_w));
      dl_write(&DL_ap4,v_vv);
      v_x = (v_w - ((0.65) * v_vv));
      H_m0 = g_wrap((H_m0 + ((0.07) / SR)),(0.0),(1.0));
      v_dlen0 = (((1399.0) * v_scl) * v_rsz);
      v_o0 = dl_read(&DL_d0,(v_dlen0 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m0)))));
      H_lp0 = (H_lp0 + (v_damp * (v_o0 - H_lp0)));
      v_a0 = (H_lp0 * pow((10.0),(((-(3.0)) * v_dlen0) / (v_tdec * SR))));
      H_m1 = g_wrap((H_m1 + ((0.101) / SR)),(0.0),(1.0));
      v_dlen1 = (((1601.0) * v_scl) * v_rsz);
      v_o1 = dl_read(&DL_d1,(v_dlen1 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m1)))));
      H_lp1 = (H_lp1 + (v_damp * (v_o1 - H_lp1)));
      v_a1 = (H_lp1 * pow((10.0),(((-(3.0)) * v_dlen1) / (v_tdec * SR))));
      H_m2 = g_wrap((H_m2 + ((0.132) / SR)),(0.0),(1.0));
      v_dlen2 = (((1847.0) * v_scl) * v_rsz);
      v_o2 = dl_read(&DL_d2,(v_dlen2 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m2)))));
      H_lp2 = (H_lp2 + (v_damp * (v_o2 - H_lp2)));
      v_a2 = (H_lp2 * pow((10.0),(((-(3.0)) * v_dlen2) / (v_tdec * SR))));
      H_m3 = g_wrap((H_m3 + ((0.163) / SR)),(0.0),(1.0));
      v_dlen3 = (((2081.0) * v_scl) * v_rsz);
      v_o3 = dl_read(&DL_d3,(v_dlen3 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m3)))));
      H_lp3 = (H_lp3 + (v_damp * (v_o3 - H_lp3)));
      v_a3 = (H_lp3 * pow((10.0),(((-(3.0)) * v_dlen3) / (v_tdec * SR))));
      H_m4 = g_wrap((H_m4 + ((0.194) / SR)),(0.0),(1.0));
      v_dlen4 = (((2351.0) * v_scl) * v_rsz);
      v_o4 = dl_read(&DL_d4,(v_dlen4 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m4)))));
      H_lp4 = (H_lp4 + (v_damp * (v_o4 - H_lp4)));
      v_a4 = (H_lp4 * pow((10.0),(((-(3.0)) * v_dlen4) / (v_tdec * SR))));
      H_m5 = g_wrap((H_m5 + ((0.225) / SR)),(0.0),(1.0));
      v_dlen5 = (((2647.0) * v_scl) * v_rsz);
      v_o5 = dl_read(&DL_d5,(v_dlen5 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m5)))));
      H_lp5 = (H_lp5 + (v_damp * (v_o5 - H_lp5)));
      v_a5 = (H_lp5 * pow((10.0),(((-(3.0)) * v_dlen5) / (v_tdec * SR))));
      H_m6 = g_wrap((H_m6 + ((0.256) / SR)),(0.0),(1.0));
      v_dlen6 = (((2969.0) * v_scl) * v_rsz);
      v_o6 = dl_read(&DL_d6,(v_dlen6 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m6)))));
      H_lp6 = (H_lp6 + (v_damp * (v_o6 - H_lp6)));
      v_a6 = (H_lp6 * pow((10.0),(((-(3.0)) * v_dlen6) / (v_tdec * SR))));
      H_m7 = g_wrap((H_m7 + ((0.287) / SR)),(0.0),(1.0));
      v_dlen7 = (((3229.0) * v_scl) * v_rsz);
      v_o7 = dl_read(&DL_d7,(v_dlen7 + (((14.0) * v_scl) * sin(((2.0*M_PI) * H_m7)))));
      H_lp7 = (H_lp7 + (v_damp * (v_o7 - H_lp7)));
      v_a7 = (H_lp7 * pow((10.0),(((-(3.0)) * v_dlen7) / (v_tdec * SR))));
      v_b0 = (v_a0 + v_a1);
      v_b1 = (v_a0 - v_a1);
      v_b2 = (v_a2 + v_a3);
      v_b3 = (v_a2 - v_a3);
      v_b4 = (v_a4 + v_a5);
      v_b5 = (v_a4 - v_a5);
      v_b6 = (v_a6 + v_a7);
      v_b7 = (v_a6 - v_a7);
      v_c0 = (v_b0 + v_b2);
      v_c1 = (v_b1 + v_b3);
      v_c2 = (v_b0 - v_b2);
      v_c3 = (v_b1 - v_b3);
      v_c4 = (v_b4 + v_b6);
      v_c5 = (v_b5 + v_b7);
      v_c6 = (v_b4 - v_b6);
      v_c7 = (v_b5 - v_b7);
      v_h0 = ((v_c0 + v_c4) * (0.353553));
      v_h1 = ((v_c1 + v_c5) * (0.353553));
      v_h2 = ((v_c2 + v_c6) * (0.353553));
      v_h3 = ((v_c3 + v_c7) * (0.353553));
      v_h4 = ((v_c0 - v_c4) * (0.353553));
      v_h5 = ((v_c1 - v_c5) * (0.353553));
      v_h6 = ((v_c2 - v_c6) * (0.353553));
      v_h7 = ((v_c3 - v_c7) * (0.353553));
      dl_write(&DL_shd,v_h0);
      v_wsh = (((90.0))*SR/1000.0);
      v_sp1 = g_wrap((H_shph - ((1.0) / v_wsh)),(0.0),(1.0));
      H_shph = v_sp1;
      v_sp2 = g_wrap((v_sp1 + (0.5)),(0.0),(1.0));
      v_s1 = sin((M_PI * v_sp1));
      v_s2 = sin((M_PI * v_sp2));
      v_upv = (((dl_read(&DL_shd,((v_sp1 * v_wsh) + (2.0))) * v_s1) * v_s1) + ((dl_read(&DL_shd,((v_sp2 * v_wsh) + (2.0))) * v_s2) * v_s2));
      v_hpv = ((v_upv - H_shx) + ((0.995) * H_shhp));
      H_shhp = v_hpv;
      H_shx = v_upv;
      v_f0 = ((v_h0 * ((1.0) - v_shm)) + (v_hpv * v_shm));
      dl_write(&DL_d0,(v_f0 + (v_x * (0.5))));
      dl_write(&DL_d1,(v_h1 - (v_x * (0.5))));
      dl_write(&DL_d2,(v_h2 + (v_x * (0.5))));
      dl_write(&DL_d3,(v_h3 - (v_x * (0.5))));
      dl_write(&DL_d4,(v_h4 + (v_x * (0.5))));
      dl_write(&DL_d5,(v_h5 - (v_x * (0.5))));
      dl_write(&DL_d6,(v_h6 + (v_x * (0.5))));
      dl_write(&DL_d7,(v_h7 - (v_x * (0.5))));
      v_revl = ((((v_o0 + v_o2) + v_o4) + v_o6) * (0.38));
      v_revr = ((((v_o1 + v_o3) + v_o5) + v_o7) * (0.38));
      v_ml = ((((((((((((IN1 + ((IN3 * P_ret_cho) / (100.0))) + ((IN5 * P_ret_dly) / (100.0))) + ((IN7 * P_ret_halo) / (100.0))) + ((IN9 * P_ret_frz) / (100.0))) + ((v_revl * P_ret_spc) / (100.0))) + ((IN13 * P_ret_rev) / (100.0))) + ((IN15 * P_ret_res) / (100.0))) + ((IN17 * P_ret_grn) / (100.0))) + ((IN19 * P_ret_fxb) / (100.0))) + ((IN21 * P_ret_drm) / (100.0))) + ((IN23 * P_ret_shd) / (100.0))) + ((IN25 * P_ret_orb) / (100.0)));
      v_mr = ((((((((((((IN2 + ((IN4 * P_ret_cho) / (100.0))) + ((IN6 * P_ret_dly) / (100.0))) + ((IN8 * P_ret_halo) / (100.0))) + ((IN10 * P_ret_frz) / (100.0))) + ((v_revr * P_ret_spc) / (100.0))) + ((IN14 * P_ret_rev) / (100.0))) + ((IN16 * P_ret_res) / (100.0))) + ((IN18 * P_ret_grn) / (100.0))) + ((IN20 * P_ret_fxb) / (100.0))) + ((IN22 * P_ret_drm) / (100.0))) + ((IN24 * P_ret_shd) / (100.0))) + ((IN26 * P_ret_orb) / (100.0)));
      v_mono = ((v_ml + v_mr) * (0.5));
      v_rho = (0.97);
      v_s0 = ((v_mono - ((v_rho * H_g_a) * H_g_s1)) - ((v_rho * v_rho) * H_g_s2));
      v_er = ((v_s0 + (H_g_a * H_g_s1)) + H_g_s2);
      v_ps = (H_g_ps + (((H_g_s1 * H_g_s1) - H_g_ps) * (0.001)));
      H_g_ps = v_ps;
      v_ga = g_clamp((H_g_a - ((((0.004) * v_er) * H_g_s1) / (v_ps + (1e-07)))),(-(1.999)),(1.999));
      H_g_a = v_ga;
      H_g_s2 = H_g_s1;
      H_g_s1 = v_s0;
      v_px = (H_g_px + (((v_mono * v_mono) - H_g_px) * (0.0007)));
      H_g_px = v_px;
      v_pe = (H_g_pe + (((v_er * v_er) - H_g_pe) * (0.0007)));
      H_g_pe = v_pe;
      H_g_ctr = (H_g_ctr + (1.0));
      if (((double)((H_g_ctr) >= ((((50.0))*SR/1000.0)))) != 0.0) {
        H_g_ctr = (0.0);
        H_g_h3 = H_g_h2;
        H_g_h2 = H_g_h1;
        H_g_h1 = v_px;
      }
      v_fh = ((acos(g_clamp(((-v_ga) / (2.0)),(-(1.0)),(1.0))) * SR) / (2.0*M_PI));
      v_grow = ((double)((((double)((v_px) > ((H_g_h3 * (2.0)))))) || (((double)((v_px) > ((0.2)))))));
      v_howl = ((double)((((double)((((double)((((double)((((double)((((double)((P_guard) > ((0.0))))) && (((double)((v_px) > ((0.004)))))))) && (((double)((v_pe) < ((v_px * (0.12))))))))) && (v_grow)))) && (((double)((v_fh) > ((80.0)))))))) && (((double)((v_fh) < ((12000.0)))))));
      if (v_howl != 0.0) {
        H_g_cnt = (H_g_cnt + (1.0));
      } else {
        H_g_cnt = fmax((0.0),(H_g_cnt - (2.0)));
      }
      if (((double)((P_grd_clr) != (H_g_lclr))) != 0.0) {
        H_g_lclr = P_grd_clr;
        H_g_nf1 = (0.0);
        H_g_nf2 = (0.0);
      }
      if (((double)((H_g_cnt) > ((((300.0))*SR/1000.0)))) != 0.0) {
        H_g_cnt = (0.0);
        v_near1 = ((double)((((double)((H_g_nf1) > ((0.0))))) && (((double)((fabs(((v_fh / fmax((1.0),H_g_nf1)) - (1.0)))) < ((0.04)))))));
        v_near2 = ((double)((((double)((H_g_nf2) > ((0.0))))) && (((double)((fabs(((v_fh / fmax((1.0),H_g_nf2)) - (1.0)))) < ((0.04)))))));
        if (((double)((((double)((v_near1) == ((0.0))))) && (((double)((v_near2) == ((0.0))))))) != 0.0) {
          if (((double)((H_g_nf1) == ((0.0)))) != 0.0) {
            H_g_nf1 = v_fh;
            H_g_nd1 = (0.0);
            H_g_old = (2.0);
          } else {
            if (((double)((H_g_nf2) == ((0.0)))) != 0.0) {
              H_g_nf2 = v_fh;
              H_g_nd2 = (0.0);
              H_g_old = (1.0);
            } else {
              if (((double)((H_g_old) == ((1.0)))) != 0.0) {
                H_g_nf1 = v_fh;
                H_g_nd1 = (0.0);
                H_g_old = (2.0);
              } else {
                H_g_nf2 = v_fh;
                H_g_nd2 = (0.0);
                H_g_old = (1.0);
              }
            }
          }
        }
      }
      v_tg1 = ((double)((((double)((P_guard) > ((0.0))))) && (((double)((H_g_nf1) > ((0.0)))))));
      v_tg2 = ((double)((((double)((P_guard) > ((0.0))))) && (((double)((H_g_nf2) > ((0.0)))))));
      v_nd1 = (H_g_nd1 + ((v_tg1 - H_g_nd1) * (0.0005)));
      H_g_nd1 = v_nd1;
      v_nd2 = (H_g_nd2 + ((v_tg2 - H_g_nd2) * (0.0005)));
      H_g_nd2 = v_nd2;
      v_nw = (((2.0*M_PI) * fmin(H_g_nf1,(SR * (0.45)))) / SR);
      v_nA = pow((10.0),(((-(24.0)) * v_nd1) / (40.0)));
      v_nal = (sin(v_nw) / (50.0));
      v_ncs = cos(v_nw);
      v_na0 = ((1.0) + (v_nal / v_nA));
      v_ny = ((((((((1.0) + (v_nal * v_nA)) * v_ml) - (((2.0) * v_ncs) * H_g_l1x1)) + (((1.0) - (v_nal * v_nA)) * H_g_l1x2)) + (((2.0) * v_ncs) * H_g_l1y1)) - (((1.0) - (v_nal / v_nA)) * H_g_l1y2)) / v_na0);
      H_g_l1x2 = H_g_l1x1;
      H_g_l1x1 = v_ml;
      H_g_l1y2 = H_g_l1y1;
      H_g_l1y1 = v_ny;
      v_ml = v_ny;
      v_nw = (((2.0*M_PI) * fmin(H_g_nf1,(SR * (0.45)))) / SR);
      v_nA = pow((10.0),(((-(24.0)) * v_nd1) / (40.0)));
      v_nal = (sin(v_nw) / (50.0));
      v_ncs = cos(v_nw);
      v_na0 = ((1.0) + (v_nal / v_nA));
      v_ny = ((((((((1.0) + (v_nal * v_nA)) * v_mr) - (((2.0) * v_ncs) * H_g_r1x1)) + (((1.0) - (v_nal * v_nA)) * H_g_r1x2)) + (((2.0) * v_ncs) * H_g_r1y1)) - (((1.0) - (v_nal / v_nA)) * H_g_r1y2)) / v_na0);
      H_g_r1x2 = H_g_r1x1;
      H_g_r1x1 = v_mr;
      H_g_r1y2 = H_g_r1y1;
      H_g_r1y1 = v_ny;
      v_mr = v_ny;
      v_nw = (((2.0*M_PI) * fmin(H_g_nf2,(SR * (0.45)))) / SR);
      v_nA = pow((10.0),(((-(24.0)) * v_nd2) / (40.0)));
      v_nal = (sin(v_nw) / (50.0));
      v_ncs = cos(v_nw);
      v_na0 = ((1.0) + (v_nal / v_nA));
      v_ny = ((((((((1.0) + (v_nal * v_nA)) * v_ml) - (((2.0) * v_ncs) * H_g_l2x1)) + (((1.0) - (v_nal * v_nA)) * H_g_l2x2)) + (((2.0) * v_ncs) * H_g_l2y1)) - (((1.0) - (v_nal / v_nA)) * H_g_l2y2)) / v_na0);
      H_g_l2x2 = H_g_l2x1;
      H_g_l2x1 = v_ml;
      H_g_l2y2 = H_g_l2y1;
      H_g_l2y1 = v_ny;
      v_ml = v_ny;
      v_nw = (((2.0*M_PI) * fmin(H_g_nf2,(SR * (0.45)))) / SR);
      v_nA = pow((10.0),(((-(24.0)) * v_nd2) / (40.0)));
      v_nal = (sin(v_nw) / (50.0));
      v_ncs = cos(v_nw);
      v_na0 = ((1.0) + (v_nal / v_nA));
      v_ny = ((((((((1.0) + (v_nal * v_nA)) * v_mr) - (((2.0) * v_ncs) * H_g_r2x1)) + (((1.0) - (v_nal * v_nA)) * H_g_r2x2)) + (((2.0) * v_ncs) * H_g_r2y1)) - (((1.0) - (v_nal / v_nA)) * H_g_r2y2)) / v_na0);
      H_g_r2x2 = H_g_r2x1;
      H_g_r2x1 = v_mr;
      H_g_r2y2 = H_g_r2y1;
      H_g_r2y1 = v_ny;
      v_mr = v_ny;
      v_gin = ((fmax(fabs(v_ml),fabs(v_mr)) * v_mv) * (1.3));
      v_gen2 = H_h_ge;
      if (((double)((v_gin) > (v_gen2))) != 0.0) {
        {
          v_gen2 = (v_gen2 + ((v_gin - v_gen2) * ((1.0) - exp(((-(1.0)) / (((8.0))*SR/1000.0))))));
        }
      } else {
        {
          v_gen2 = (v_gen2 + ((v_gin - v_gen2) * ((1.0) - exp(((-(1.0)) / (((180.0))*SR/1000.0))))));
        }
      }
      H_h_ge = v_gen2;
      v_gth = pow(10.0,(((-(4.0)) - (P_glue * (0.2))))/20.0);
      v_grt = ((1.5) + (P_glue * (0.02)));
      v_ggt = (1.0);
      if (((double)((v_gen2) > (v_gth))) != 0.0) {
        {
          v_ggt = pow((v_gen2 / v_gth),(((1.0) / v_grt) - (1.0)));
        }
      }
      v_gmk = (1.0);
      if (((double)(((0.25)) > (v_gth))) != 0.0) {
        {
          v_gmk = ((1.0) / pow(((0.25) / v_gth),(((1.0) / v_grt) - (1.0))));
        }
      }
      v_ggn = (v_ggt * v_gmk);
      OUT1 = tanh((((v_ml * v_mv) * (1.3)) * v_ggn));
      OUT2 = tanh((((v_mr * v_mv) * (1.3)) * v_ggn));
      OUT5 = ((1.0) - v_ggt);
      OUT3 = (H_g_nf1 * ((double)((v_nd1) > ((0.5)))));
      OUT4 = (H_g_nf2 * ((double)((v_nd2) > ((0.5)))));
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
      DL_pre.w = (DL_pre.w + 1) % DL_pre.n;
      DL_ap1.w = (DL_ap1.w + 1) % DL_ap1.n;
      DL_ap2.w = (DL_ap2.w + 1) % DL_ap2.n;
      DL_ap3.w = (DL_ap3.w + 1) % DL_ap3.n;
      DL_ap4.w = (DL_ap4.w + 1) % DL_ap4.n;
      DL_d0.w = (DL_d0.w + 1) % DL_d0.n;
      DL_d1.w = (DL_d1.w + 1) % DL_d1.n;
      DL_d2.w = (DL_d2.w + 1) % DL_d2.n;
      DL_d3.w = (DL_d3.w + 1) % DL_d3.n;
      DL_d4.w = (DL_d4.w + 1) % DL_d4.n;
      DL_d5.w = (DL_d5.w + 1) % DL_d5.n;
      DL_d6.w = (DL_d6.w + 1) % DL_d6.n;
      DL_d7.w = (DL_d7.w + 1) % DL_d7.n;
      DL_shd.w = (DL_shd.w + 1) % DL_shd.n;
    }
  }
};
// ---------------------------------------------------------------- Room
struct Room {
  double SR = 44100.0;
  double P_spk_n = 2.0;
  double P_spk_a0 = -30.0;
  double P_spk_d0 = 100.0;
  double P_spk_a1 = 30.0;
  double P_spk_d1 = 100.0;
  double P_spk_a2 = 0.0;
  double P_spk_d2 = 100.0;
  double P_spk_a3 = 0.0;
  double P_spk_d3 = 100.0;
  double P_spk_a4 = 0.0;
  double P_spk_d4 = 100.0;
  double P_spk_a5 = 0.0;
  double P_spk_d5 = 100.0;
  double P_spk_a6 = 0.0;
  double P_spk_d6 = 100.0;
  double P_spk_a7 = 0.0;
  double P_spk_d7 = 100.0;
  double P_spk_a8 = 0.0;
  double P_spk_d8 = 100.0;
  double P_spk_a9 = 0.0;
  double P_spk_d9 = 100.0;
  double P_spk_a10 = 0.0;
  double P_spk_d10 = 100.0;
  double P_spk_a11 = 0.0;
  double P_spk_d11 = 100.0;
  double P_spk_a12 = 0.0;
  double P_spk_d12 = 100.0;
  double P_spk_a13 = 0.0;
  double P_spk_d13 = 100.0;
  double P_spk_a14 = 0.0;
  double P_spk_d14 = 100.0;
  double P_spk_a15 = 0.0;
  double P_spk_d15 = 100.0;
  double P_spk_a16 = 0.0;
  double P_spk_d16 = 100.0;
  double P_spk_a17 = 0.0;
  double P_spk_d17 = 100.0;
  double P_spk_a18 = 0.0;
  double P_spk_d18 = 100.0;
  double P_spk_a19 = 0.0;
  double P_spk_d19 = 100.0;
  double P_spk_a20 = 0.0;
  double P_spk_d20 = 100.0;
  double P_spk_a21 = 0.0;
  double P_spk_d21 = 100.0;
  double P_spk_a22 = 0.0;
  double P_spk_d22 = 100.0;
  double P_spk_a23 = 0.0;
  double P_spk_d23 = 100.0;
  double P_spk_a24 = 0.0;
  double P_spk_d24 = 100.0;
  double P_spk_a25 = 0.0;
  double P_spk_d25 = 100.0;
  double P_spk_a26 = 0.0;
  double P_spk_d26 = 100.0;
  double P_spk_a27 = 0.0;
  double P_spk_d27 = 100.0;
  double P_spk_a28 = 0.0;
  double P_spk_d28 = 100.0;
  double P_spk_a29 = 0.0;
  double P_spk_d29 = 100.0;
  double P_spk_a30 = 0.0;
  double P_spk_d30 = 100.0;
  double P_spk_a31 = 0.0;
  double P_spk_d31 = 100.0;
  double P_main_w = 0.0;
  double P_mv_blur = 30.0;
  double P_ph_main = 70.0;
  double P_ph_cue = 100.0;
  double P_ph_click = 100.0;
  double P_ph_pvw = 100.0;
  double P_ph_vol = 80.0;
  double P_clk_room = 0.0;
  double P_room_vol = 100.0;
  std::vector<double> V_sa; double* D_sa = nullptr; long LEN_sa = 32; long CH_sa = 1;
  std::vector<double> V_sd; double* D_sd = nullptr; long LEN_sd = 32; long CH_sd = 1;
  std::vector<double> V_gtl; double* D_gtl = nullptr; long LEN_gtl = 32; long CH_gtl = 1;
  std::vector<double> V_gtr; double* D_gtr = nullptr; long LEN_gtr = 32; long CH_gtr = 1;
  std::vector<double> V_gtm; double* D_gtm = nullptr; long LEN_gtm = 32; long CH_gtm = 1;
  std::vector<double> V_gl; double* D_gl = nullptr; long LEN_gl = 32; long CH_gl = 1;
  std::vector<double> V_gr; double* D_gr = nullptr; long LEN_gr = 32; long CH_gr = 1;
  std::vector<double> V_gm; double* D_gm = nullptr; long LEN_gm = 32; long CH_gm = 1;
  std::vector<double> V_yo; double* D_yo = nullptr; long LEN_yo = 32; long CH_yo = 1;
  double H_r_cnt = 0.0;
  double H_r_mx = 0.0;
  double H_r_my = 0.0;
  double H_r_init = 0.0;
  static constexpr int numInputs = 9;
  static constexpr int numOutputs = 36;
  void init(double sr) { SR = sr;
    V_sa.assign((size_t) 32 * 1, 0.0); D_sa = V_sa.data();
    V_sd.assign((size_t) 32 * 1, 0.0); D_sd = V_sd.data();
    V_gtl.assign((size_t) 32 * 1, 0.0); D_gtl = V_gtl.data();
    V_gtr.assign((size_t) 32 * 1, 0.0); D_gtr = V_gtr.data();
    V_gtm.assign((size_t) 32 * 1, 0.0); D_gtm = V_gtm.data();
    V_gl.assign((size_t) 32 * 1, 0.0); D_gl = V_gl.data();
    V_gr.assign((size_t) 32 * 1, 0.0); D_gr = V_gr.data();
    V_gm.assign((size_t) 32 * 1, 0.0); D_gm = V_gm.data();
    V_yo.assign((size_t) 32 * 1, 0.0); D_yo = V_yo.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "spk_n") return 0;
    if (n == "spk_a0") return 1;
    if (n == "spk_d0") return 2;
    if (n == "spk_a1") return 3;
    if (n == "spk_d1") return 4;
    if (n == "spk_a2") return 5;
    if (n == "spk_d2") return 6;
    if (n == "spk_a3") return 7;
    if (n == "spk_d3") return 8;
    if (n == "spk_a4") return 9;
    if (n == "spk_d4") return 10;
    if (n == "spk_a5") return 11;
    if (n == "spk_d5") return 12;
    if (n == "spk_a6") return 13;
    if (n == "spk_d6") return 14;
    if (n == "spk_a7") return 15;
    if (n == "spk_d7") return 16;
    if (n == "spk_a8") return 17;
    if (n == "spk_d8") return 18;
    if (n == "spk_a9") return 19;
    if (n == "spk_d9") return 20;
    if (n == "spk_a10") return 21;
    if (n == "spk_d10") return 22;
    if (n == "spk_a11") return 23;
    if (n == "spk_d11") return 24;
    if (n == "spk_a12") return 25;
    if (n == "spk_d12") return 26;
    if (n == "spk_a13") return 27;
    if (n == "spk_d13") return 28;
    if (n == "spk_a14") return 29;
    if (n == "spk_d14") return 30;
    if (n == "spk_a15") return 31;
    if (n == "spk_d15") return 32;
    if (n == "spk_a16") return 33;
    if (n == "spk_d16") return 34;
    if (n == "spk_a17") return 35;
    if (n == "spk_d17") return 36;
    if (n == "spk_a18") return 37;
    if (n == "spk_d18") return 38;
    if (n == "spk_a19") return 39;
    if (n == "spk_d19") return 40;
    if (n == "spk_a20") return 41;
    if (n == "spk_d20") return 42;
    if (n == "spk_a21") return 43;
    if (n == "spk_d21") return 44;
    if (n == "spk_a22") return 45;
    if (n == "spk_d22") return 46;
    if (n == "spk_a23") return 47;
    if (n == "spk_d23") return 48;
    if (n == "spk_a24") return 49;
    if (n == "spk_d24") return 50;
    if (n == "spk_a25") return 51;
    if (n == "spk_d25") return 52;
    if (n == "spk_a26") return 53;
    if (n == "spk_d26") return 54;
    if (n == "spk_a27") return 55;
    if (n == "spk_d27") return 56;
    if (n == "spk_a28") return 57;
    if (n == "spk_d28") return 58;
    if (n == "spk_a29") return 59;
    if (n == "spk_d29") return 60;
    if (n == "spk_a30") return 61;
    if (n == "spk_d30") return 62;
    if (n == "spk_a31") return 63;
    if (n == "spk_d31") return 64;
    if (n == "main_w") return 65;
    if (n == "mv_blur") return 66;
    if (n == "ph_main") return 67;
    if (n == "ph_cue") return 68;
    if (n == "ph_click") return 69;
    if (n == "ph_pvw") return 70;
    if (n == "ph_vol") return 71;
    if (n == "clk_room") return 72;
    if (n == "room_vol") return 73;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: if (v < 2.0) v = 2.0; if (v > 32.0) v = 32.0; P_spk_n = v; break;
      case 1: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a0 = v; break;
      case 2: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d0 = v; break;
      case 3: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a1 = v; break;
      case 4: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d1 = v; break;
      case 5: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a2 = v; break;
      case 6: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d2 = v; break;
      case 7: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a3 = v; break;
      case 8: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d3 = v; break;
      case 9: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a4 = v; break;
      case 10: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d4 = v; break;
      case 11: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a5 = v; break;
      case 12: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d5 = v; break;
      case 13: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a6 = v; break;
      case 14: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d6 = v; break;
      case 15: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a7 = v; break;
      case 16: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d7 = v; break;
      case 17: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a8 = v; break;
      case 18: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d8 = v; break;
      case 19: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a9 = v; break;
      case 20: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d9 = v; break;
      case 21: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a10 = v; break;
      case 22: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d10 = v; break;
      case 23: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a11 = v; break;
      case 24: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d11 = v; break;
      case 25: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a12 = v; break;
      case 26: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d12 = v; break;
      case 27: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a13 = v; break;
      case 28: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d13 = v; break;
      case 29: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a14 = v; break;
      case 30: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d14 = v; break;
      case 31: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a15 = v; break;
      case 32: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d15 = v; break;
      case 33: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a16 = v; break;
      case 34: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d16 = v; break;
      case 35: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a17 = v; break;
      case 36: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d17 = v; break;
      case 37: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a18 = v; break;
      case 38: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d18 = v; break;
      case 39: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a19 = v; break;
      case 40: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d19 = v; break;
      case 41: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a20 = v; break;
      case 42: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d20 = v; break;
      case 43: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a21 = v; break;
      case 44: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d21 = v; break;
      case 45: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a22 = v; break;
      case 46: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d22 = v; break;
      case 47: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a23 = v; break;
      case 48: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d23 = v; break;
      case 49: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a24 = v; break;
      case 50: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d24 = v; break;
      case 51: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a25 = v; break;
      case 52: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d25 = v; break;
      case 53: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a26 = v; break;
      case 54: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d26 = v; break;
      case 55: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a27 = v; break;
      case 56: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d27 = v; break;
      case 57: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a28 = v; break;
      case 58: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d28 = v; break;
      case 59: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a29 = v; break;
      case 60: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d29 = v; break;
      case 61: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a30 = v; break;
      case 62: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d30 = v; break;
      case 63: if (v < -180.0) v = -180.0; if (v > 180.0) v = 180.0; P_spk_a31 = v; break;
      case 64: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spk_d31 = v; break;
      case 65: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_main_w = v; break;
      case 66: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_mv_blur = v; break;
      case 67: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ph_main = v; break;
      case 68: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ph_cue = v; break;
      case 69: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ph_click = v; break;
      case 70: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ph_pvw = v; break;
      case 71: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_ph_vol = v; break;
      case 72: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_clk_room = v; break;
      case 73: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_room_vol = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_spk_n;
      case 1: return P_spk_a0;
      case 2: return P_spk_d0;
      case 3: return P_spk_a1;
      case 4: return P_spk_d1;
      case 5: return P_spk_a2;
      case 6: return P_spk_d2;
      case 7: return P_spk_a3;
      case 8: return P_spk_d3;
      case 9: return P_spk_a4;
      case 10: return P_spk_d4;
      case 11: return P_spk_a5;
      case 12: return P_spk_d5;
      case 13: return P_spk_a6;
      case 14: return P_spk_d6;
      case 15: return P_spk_a7;
      case 16: return P_spk_d7;
      case 17: return P_spk_a8;
      case 18: return P_spk_d8;
      case 19: return P_spk_a9;
      case 20: return P_spk_d9;
      case 21: return P_spk_a10;
      case 22: return P_spk_d10;
      case 23: return P_spk_a11;
      case 24: return P_spk_d11;
      case 25: return P_spk_a12;
      case 26: return P_spk_d12;
      case 27: return P_spk_a13;
      case 28: return P_spk_d13;
      case 29: return P_spk_a14;
      case 30: return P_spk_d14;
      case 31: return P_spk_a15;
      case 32: return P_spk_d15;
      case 33: return P_spk_a16;
      case 34: return P_spk_d16;
      case 35: return P_spk_a17;
      case 36: return P_spk_d17;
      case 37: return P_spk_a18;
      case 38: return P_spk_d18;
      case 39: return P_spk_a19;
      case 40: return P_spk_d19;
      case 41: return P_spk_a20;
      case 42: return P_spk_d20;
      case 43: return P_spk_a21;
      case 44: return P_spk_d21;
      case 45: return P_spk_a22;
      case 46: return P_spk_d22;
      case 47: return P_spk_a23;
      case 48: return P_spk_d23;
      case 49: return P_spk_a24;
      case 50: return P_spk_d24;
      case 51: return P_spk_a25;
      case 52: return P_spk_d25;
      case 53: return P_spk_a26;
      case 54: return P_spk_d26;
      case 55: return P_spk_a27;
      case 56: return P_spk_d27;
      case 57: return P_spk_a28;
      case 58: return P_spk_d28;
      case 59: return P_spk_a29;
      case 60: return P_spk_d29;
      case 61: return P_spk_a30;
      case 62: return P_spk_d30;
      case 63: return P_spk_a31;
      case 64: return P_spk_d31;
      case 65: return P_main_w;
      case 66: return P_mv_blur;
      case 67: return P_ph_main;
      case 68: return P_ph_cue;
      case 69: return P_ph_click;
      case 70: return P_ph_pvw;
      case 71: return P_ph_vol;
      case 72: return P_clk_room;
      case 73: return P_room_vol;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 74;
  double* data(const std::string& n) {
    if (n == "sa") return D_sa;
    if (n == "sd") return D_sd;
    if (n == "gtl") return D_gtl;
    if (n == "gtr") return D_gtr;
    if (n == "gtm") return D_gtm;
    if (n == "gl") return D_gl;
    if (n == "gr") return D_gr;
    if (n == "gm") return D_gm;
    if (n == "yo") return D_yo;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double IN4 = in[3] ? (double) in[3][t] : 0.0;
      double IN5 = in[4] ? (double) in[4][t] : 0.0;
      double IN6 = in[5] ? (double) in[5][t] : 0.0;
      double IN7 = in[6] ? (double) in[6][t] : 0.0;
      double IN8 = in[7] ? (double) in[7][t] : 0.0;
      double IN9 = in[8] ? (double) in[8][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double OUT6 = 0.0;
      double OUT7 = 0.0;
      double OUT8 = 0.0;
      double OUT9 = 0.0;
      double OUT10 = 0.0;
      double OUT11 = 0.0;
      double OUT12 = 0.0;
      double OUT13 = 0.0;
      double OUT14 = 0.0;
      double OUT15 = 0.0;
      double OUT16 = 0.0;
      double OUT17 = 0.0;
      double OUT18 = 0.0;
      double OUT19 = 0.0;
      double OUT20 = 0.0;
      double OUT21 = 0.0;
      double OUT22 = 0.0;
      double OUT23 = 0.0;
      double OUT24 = 0.0;
      double OUT25 = 0.0;
      double OUT26 = 0.0;
      double OUT27 = 0.0;
      double OUT28 = 0.0;
      double OUT29 = 0.0;
      double OUT30 = 0.0;
      double OUT31 = 0.0;
      double OUT32 = 0.0;
      double OUT33 = 0.0;
      double OUT34 = 0.0;
      double OUT35 = 0.0;
      double OUT36 = 0.0;
      double v_ml = 0.0;
      double v_mr = 0.0;
      double v_ck = 0.0;
      double v_rv = 0.0;
      double v_mv = 0.0;
      double v_n = 0.0;
      double v_cnt = 0.0;
      double v_wv = 0.0;
      double v_th = 0.0;
      double v_lx = 0.0;
      double v_ly = 0.0;
      double v_rx = 0.0;
      double v_ry = 0.0;
      double v_bm = 0.0;
      double v_az = 0.0;
      double v_rr = 0.0;
      double v_mx = 0.0;
      double v_my = 0.0;
      double v_bv = 0.0;
      double v_sl = 0.0;
      double v_sr2 = 0.0;
      double v_sm = 0.0;
      double v_i = 0.0;
      double v_ga = 0.0;
      double v_gb = 0.0;
      double v_gc = 0.0;
      double v_an = 0.0;
      double v_dd = 0.0;
      double v_px = 0.0;
      double v_py = 0.0;
      double v_nl = 0.0;
      double v_nr = 0.0;
      double v_nm = 0.0;
      double v_kg = 0.0;
      double v_a = 0.0;
      double v_b = 0.0;
      double v_c = 0.0;
      double v_pm = 0.0;
      double v_pc = 0.0;
      double v_pk = 0.0;
      double v_pp = 0.0;
      double v_mono = 0.0;
      double v_hl = 0.0;
      double v_hr = 0.0;
      double v_hv = 0.0;
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a0,(0.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d0,(0.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a1,(1.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d1,(1.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a2,(2.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d2,(2.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a3,(3.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d3,(3.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a4,(4.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d4,(4.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a5,(5.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d5,(5.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a6,(6.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d6,(6.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a7,(7.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d7,(7.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a8,(8.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d8,(8.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a9,(9.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d9,(9.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a10,(10.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d10,(10.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a11,(11.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d11,(11.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a12,(12.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d12,(12.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a13,(13.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d13,(13.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a14,(14.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d14,(14.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a15,(15.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d15,(15.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a16,(16.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d16,(16.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a17,(17.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d17,(17.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a18,(18.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d18,(18.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a19,(19.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d19,(19.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a20,(20.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d20,(20.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a21,(21.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d21,(21.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a22,(22.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d22,(22.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a23,(23.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d23,(23.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a24,(24.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d24,(24.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a25,(25.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d25,(25.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a26,(26.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d26,(26.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a27,(27.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d27,(27.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a28,(28.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d28,(28.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a29,(29.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d29,(29.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a30,(30.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d30,(30.0),0);
      g_poke(D_sa,LEN_sa,CH_sa,P_spk_a31,(31.0),0);
      g_poke(D_sd,LEN_sd,CH_sd,P_spk_d31,(31.0),0);
      v_ml = IN1;
      v_mr = IN2;
      v_ck = IN6;
      if (((double)((P_clk_room) > ((0.0)))) != 0.0) {
        v_ml = (v_ml + v_ck);
        v_mr = (v_mr + v_ck);
      }
      v_rv = pow((P_room_vol / (100.0)),(2.0));
      v_ml = (v_ml * v_rv);
      v_mr = (v_mr * v_rv);
      v_mv = (IN3 * v_rv);
      v_n = g_clamp(floor(P_spk_n),(2.0),(32.0));
      v_cnt = (H_r_cnt + (1.0));
      if (((double)((((double)((v_cnt) >= ((32.0))))) || (((double)((H_r_init) == ((0.0))))))) != 0.0) {
        v_cnt = (0.0);
        v_wv = (P_main_w / (100.0));
        v_th = ((((30.0) + (v_wv * (60.0))) / (180.0)) * M_PI);
        v_lx = sin(((0.0) - v_th));
        v_ly = cos(v_th);
        v_rx = sin(v_th);
        v_ry = cos(v_th);
        v_bm = ((0.01) + (v_wv * (1.4)));
        v_az = ((2.0*M_PI) * IN4);
        v_rr = g_clamp((IN5 / (1.8)),(0.0),(1.25));
        v_mx = (sin(v_az) * v_rr);
        v_my = (cos(v_az) * v_rr);
        H_r_mx = v_mx;
        H_r_my = v_my;
        v_bv = ((0.06) + ((P_mv_blur / (100.0)) * (0.9)));
        v_sl = (0.0);
        v_sr2 = (0.0);
        v_sm = (0.0);
        for (v_i = (0.0); ((double)((v_i) < ((32.0)))) != 0.0; v_i = v_i + ((1.0))) {
          v_ga = (0.0);
          v_gb = (0.0);
          v_gc = (0.0);
          if (((double)((v_i) < (v_n))) != 0.0) {
            v_an = ((g_peek(D_sa,LEN_sa,CH_sa,v_i,0) / (180.0)) * M_PI);
            v_dd = (g_peek(D_sd,LEN_sd,CH_sd,v_i,0) / (100.0));
            v_px = (sin(v_an) * v_dd);
            v_py = (cos(v_an) * v_dd);
            v_ga = ((1.0) / sqrt(fmax(0.0,((((v_px - v_lx) * (v_px - v_lx)) + ((v_py - v_ly) * (v_py - v_ly))) + (v_bm * v_bm)))));
            v_gb = ((1.0) / sqrt(fmax(0.0,((((v_px - v_rx) * (v_px - v_rx)) + ((v_py - v_ry) * (v_py - v_ry))) + (v_bm * v_bm)))));
            v_gc = ((1.0) / sqrt(fmax(0.0,((((v_px - v_mx) * (v_px - v_mx)) + ((v_py - v_my) * (v_py - v_my))) + (v_bv * v_bv)))));
            v_ga = pow(v_ga,((1.0) + (((1.0) - v_wv) * (2.0))));
            v_gb = pow(v_gb,((1.0) + (((1.0) - v_wv) * (2.0))));
          }
          g_poke(D_gtl,LEN_gtl,CH_gtl,v_ga,v_i,0);
          g_poke(D_gtr,LEN_gtr,CH_gtr,v_gb,v_i,0);
          g_poke(D_gtm,LEN_gtm,CH_gtm,v_gc,v_i,0);
          v_sl = (v_sl + (v_ga * v_ga));
          v_sr2 = (v_sr2 + (v_gb * v_gb));
          v_sm = (v_sm + (v_gc * v_gc));
        }
        v_nl = ((1.0) / sqrt(fmax(0.0,fmax((1e-06),v_sl))));
        v_nr = ((1.0) / sqrt(fmax(0.0,fmax((1e-06),v_sr2))));
        v_nm = ((1.0) / sqrt(fmax(0.0,fmax((1e-06),v_sm))));
        for (v_i = (0.0); ((double)((v_i) < ((32.0)))) != 0.0; v_i = v_i + ((1.0))) {
          g_poke(D_gtl,LEN_gtl,CH_gtl,(g_peek(D_gtl,LEN_gtl,CH_gtl,v_i,0) * v_nl),v_i,0);
          g_poke(D_gtr,LEN_gtr,CH_gtr,(g_peek(D_gtr,LEN_gtr,CH_gtr,v_i,0) * v_nr),v_i,0);
          g_poke(D_gtm,LEN_gtm,CH_gtm,(g_peek(D_gtm,LEN_gtm,CH_gtm,v_i,0) * v_nm),v_i,0);
        }
        if (((double)((H_r_init) == ((0.0)))) != 0.0) {
          for (v_i = (0.0); ((double)((v_i) < ((32.0)))) != 0.0; v_i = v_i + ((1.0))) {
            g_poke(D_gl,LEN_gl,CH_gl,g_peek(D_gtl,LEN_gtl,CH_gtl,v_i,0),v_i,0);
            g_poke(D_gr,LEN_gr,CH_gr,g_peek(D_gtr,LEN_gtr,CH_gtr,v_i,0),v_i,0);
            g_poke(D_gm,LEN_gm,CH_gm,g_peek(D_gtm,LEN_gtm,CH_gtm,v_i,0),v_i,0);
          }
          H_r_init = (1.0);
        }
      }
      H_r_cnt = v_cnt;
      v_kg = ((1.0) - exp(((-(1.0)) / (((20.0))*SR/1000.0))));
      for (v_i = (0.0); ((double)((v_i) < ((32.0)))) != 0.0; v_i = v_i + ((1.0))) {
        v_a = g_peek(D_gl,LEN_gl,CH_gl,v_i,0);
        v_a = (v_a + ((g_peek(D_gtl,LEN_gtl,CH_gtl,v_i,0) - v_a) * v_kg));
        g_poke(D_gl,LEN_gl,CH_gl,v_a,v_i,0);
        v_b = g_peek(D_gr,LEN_gr,CH_gr,v_i,0);
        v_b = (v_b + ((g_peek(D_gtr,LEN_gtr,CH_gtr,v_i,0) - v_b) * v_kg));
        g_poke(D_gr,LEN_gr,CH_gr,v_b,v_i,0);
        v_c = g_peek(D_gm,LEN_gm,CH_gm,v_i,0);
        v_c = (v_c + ((g_peek(D_gtm,LEN_gtm,CH_gtm,v_i,0) - v_c) * v_kg));
        g_poke(D_gm,LEN_gm,CH_gm,v_c,v_i,0);
        g_poke(D_yo,LEN_yo,CH_yo,(((v_ml * v_a) + (v_mr * v_b)) + (v_mv * v_c)),v_i,0);
      }
      v_pm = (pow((P_ph_main / (100.0)),(2.0)) * (1.0));
      v_pc = (P_ph_cue / (100.0));
      v_pk = (P_ph_click / (100.0));
      v_pp = (P_ph_pvw / (100.0));
      v_mono = ((v_ck * v_pk) + (IN7 * v_pp));
      v_hl = ((((IN1 * v_pm) + ((IN3 * v_pm) * (0.7))) + (IN8 * v_pc)) + v_mono);
      v_hr = ((((IN2 * v_pm) + ((IN3 * v_pm) * (0.7))) + (IN9 * v_pc)) + v_mono);
      v_hv = (pow((P_ph_vol / (100.0)),(2.0)) * (1.5));
      OUT1 = g_peek(D_yo,LEN_yo,CH_yo,(0.0),0);
      OUT2 = g_peek(D_yo,LEN_yo,CH_yo,(1.0),0);
      OUT3 = g_peek(D_yo,LEN_yo,CH_yo,(2.0),0);
      OUT4 = g_peek(D_yo,LEN_yo,CH_yo,(3.0),0);
      OUT5 = g_peek(D_yo,LEN_yo,CH_yo,(4.0),0);
      OUT6 = g_peek(D_yo,LEN_yo,CH_yo,(5.0),0);
      OUT7 = g_peek(D_yo,LEN_yo,CH_yo,(6.0),0);
      OUT8 = g_peek(D_yo,LEN_yo,CH_yo,(7.0),0);
      OUT9 = g_peek(D_yo,LEN_yo,CH_yo,(8.0),0);
      OUT10 = g_peek(D_yo,LEN_yo,CH_yo,(9.0),0);
      OUT11 = g_peek(D_yo,LEN_yo,CH_yo,(10.0),0);
      OUT12 = g_peek(D_yo,LEN_yo,CH_yo,(11.0),0);
      OUT13 = g_peek(D_yo,LEN_yo,CH_yo,(12.0),0);
      OUT14 = g_peek(D_yo,LEN_yo,CH_yo,(13.0),0);
      OUT15 = g_peek(D_yo,LEN_yo,CH_yo,(14.0),0);
      OUT16 = g_peek(D_yo,LEN_yo,CH_yo,(15.0),0);
      OUT17 = g_peek(D_yo,LEN_yo,CH_yo,(16.0),0);
      OUT18 = g_peek(D_yo,LEN_yo,CH_yo,(17.0),0);
      OUT19 = g_peek(D_yo,LEN_yo,CH_yo,(18.0),0);
      OUT20 = g_peek(D_yo,LEN_yo,CH_yo,(19.0),0);
      OUT21 = g_peek(D_yo,LEN_yo,CH_yo,(20.0),0);
      OUT22 = g_peek(D_yo,LEN_yo,CH_yo,(21.0),0);
      OUT23 = g_peek(D_yo,LEN_yo,CH_yo,(22.0),0);
      OUT24 = g_peek(D_yo,LEN_yo,CH_yo,(23.0),0);
      OUT25 = g_peek(D_yo,LEN_yo,CH_yo,(24.0),0);
      OUT26 = g_peek(D_yo,LEN_yo,CH_yo,(25.0),0);
      OUT27 = g_peek(D_yo,LEN_yo,CH_yo,(26.0),0);
      OUT28 = g_peek(D_yo,LEN_yo,CH_yo,(27.0),0);
      OUT29 = g_peek(D_yo,LEN_yo,CH_yo,(28.0),0);
      OUT30 = g_peek(D_yo,LEN_yo,CH_yo,(29.0),0);
      OUT31 = g_peek(D_yo,LEN_yo,CH_yo,(30.0),0);
      OUT32 = g_peek(D_yo,LEN_yo,CH_yo,(31.0),0);
      OUT33 = (v_hl * v_hv);
      OUT34 = (v_hr * v_hv);
      OUT35 = H_r_mx;
      OUT36 = H_r_my;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
      if (out[5]) out[5][t] = (float) OUT6;
      if (out[6]) out[6][t] = (float) OUT7;
      if (out[7]) out[7][t] = (float) OUT8;
      if (out[8]) out[8][t] = (float) OUT9;
      if (out[9]) out[9][t] = (float) OUT10;
      if (out[10]) out[10][t] = (float) OUT11;
      if (out[11]) out[11][t] = (float) OUT12;
      if (out[12]) out[12][t] = (float) OUT13;
      if (out[13]) out[13][t] = (float) OUT14;
      if (out[14]) out[14][t] = (float) OUT15;
      if (out[15]) out[15][t] = (float) OUT16;
      if (out[16]) out[16][t] = (float) OUT17;
      if (out[17]) out[17][t] = (float) OUT18;
      if (out[18]) out[18][t] = (float) OUT19;
      if (out[19]) out[19][t] = (float) OUT20;
      if (out[20]) out[20][t] = (float) OUT21;
      if (out[21]) out[21][t] = (float) OUT22;
      if (out[22]) out[22][t] = (float) OUT23;
      if (out[23]) out[23][t] = (float) OUT24;
      if (out[24]) out[24][t] = (float) OUT25;
      if (out[25]) out[25][t] = (float) OUT26;
      if (out[26]) out[26][t] = (float) OUT27;
      if (out[27]) out[27][t] = (float) OUT28;
      if (out[28]) out[28][t] = (float) OUT29;
      if (out[29]) out[29][t] = (float) OUT30;
      if (out[30]) out[30][t] = (float) OUT31;
      if (out[31]) out[31][t] = (float) OUT32;
      if (out[32]) out[32][t] = (float) OUT33;
      if (out[33]) out[33][t] = (float) OUT34;
      if (out[34]) out[34][t] = (float) OUT35;
      if (out[35]) out[35][t] = (float) OUT36;
    }
  }
};
// ---------------------------------------------------------------- Scope
struct Scope {
  double SR = 44100.0;
  float* D_wa = nullptr; long LEN_wa = 0; long CH_wa = 1;   // buffer~ st_waveA
  float* D_wb = nullptr; long LEN_wb = 0; long CH_wb = 1;   // buffer~ st_waveB
  float* D_wc = nullptr; long LEN_wc = 0; long CH_wc = 1;   // buffer~ st_waveC
  float* D_wd = nullptr; long LEN_wd = 0; long CH_wd = 1;   // buffer~ st_waveD
  float* D_wl = nullptr; long LEN_wl = 0; long CH_wl = 1;   // buffer~ st_wavel
  std::vector<double> V_xin; double* D_xin = nullptr; long LEN_xin = 12; long CH_xin = 1;
  std::vector<double> V_pk; double* D_pk = nullptr; long LEN_pk = 12; long CH_pk = 1;
  std::vector<double> V_lb; double* D_lb = nullptr; long LEN_lb = 12; long CH_lb = 1;
  double H_s_init = 0.0;
  static constexpr int numInputs = 14;
  static constexpr int numOutputs = 1;
  void init(double sr) { SR = sr;
    V_xin.assign((size_t) 12 * 1, 0.0); D_xin = V_xin.data();
    V_pk.assign((size_t) 12 * 1, 0.0); D_pk = V_pk.data();
    V_lb.assign((size_t) 12 * 1, 0.0); D_lb = V_lb.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
    if (name == "st_waveA") { D_wa = b.data.data(); LEN_wa = b.len; CH_wa = b.ch; }
    if (name == "st_waveB") { D_wb = b.data.data(); LEN_wb = b.len; CH_wb = b.ch; }
    if (name == "st_waveC") { D_wc = b.data.data(); LEN_wc = b.len; CH_wc = b.ch; }
    if (name == "st_waveD") { D_wd = b.data.data(); LEN_wd = b.len; CH_wd = b.ch; }
    if (name == "st_wavel") { D_wl = b.data.data(); LEN_wl = b.len; CH_wl = b.ch; }
  }
  static std::vector<std::string> bufferNames() { return {"st_waveA", "st_waveB", "st_waveC", "st_waveD", "st_wavel"}; }
  static int paramIndex(const std::string& n) {
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      default: return 0.0;
    }
  }
  static constexpr int numParams = 0;
  double* data(const std::string& n) {
    if (n == "xin") return D_xin;
    if (n == "pk") return D_pk;
    if (n == "lb") return D_lb;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double IN3 = in[2] ? (double) in[2][t] : 0.0;
      double IN4 = in[3] ? (double) in[3][t] : 0.0;
      double IN5 = in[4] ? (double) in[4][t] : 0.0;
      double IN6 = in[5] ? (double) in[5][t] : 0.0;
      double IN7 = in[6] ? (double) in[6][t] : 0.0;
      double IN8 = in[7] ? (double) in[7][t] : 0.0;
      double IN9 = in[8] ? (double) in[8][t] : 0.0;
      double IN10 = in[9] ? (double) in[9][t] : 0.0;
      double IN11 = in[10] ? (double) in[10][t] : 0.0;
      double IN12 = in[11] ? (double) in[11][t] : 0.0;
      double IN13 = in[12] ? (double) in[12][t] : 0.0;
      double IN14 = in[13] ? (double) in[13][t] : 0.0;
      double OUT1 = 0.0;
      double v_ph = 0.0;
      double v_sc = 0.0;
      double v_nb = 0.0;
      double v_bin = 0.0;
      double v_i = 0.0;
      double v_v = 0.0;
      double v_p = 0.0;
      double v_b0 = 0.0;
      double v_y = 0.0;
      double v_ch = 0.0;
      g_poke(D_xin,LEN_xin,CH_xin,IN1,(0.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN2,(1.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN3,(2.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN4,(3.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN5,(4.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN6,(5.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN7,(6.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN8,(7.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN9,(8.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN10,(9.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN11,(10.0),0);
      g_poke(D_xin,LEN_xin,CH_xin,IN12,(11.0),0);
      v_ph = g_clamp(IN13,(0.0),(0.99999));
      v_sc = IN14;
      v_nb = fmax((1.0),floor((fmin(((double)LEN_wa),((double)LEN_wl)) / (2.0))));
      v_bin = floor((v_ph * v_nb));
      if (((double)((H_s_init) == ((0.0)))) != 0.0) {
        for (v_i = (0.0); ((double)((v_i) < ((12.0)))) != 0.0; v_i = v_i + ((1.0))) {
          g_poke(D_lb,LEN_lb,CH_lb,v_bin,v_i,0);
        }
        H_s_init = (1.0);
      }
      for (v_i = (0.0); ((double)((v_i) < ((12.0)))) != 0.0; v_i = v_i + ((1.0))) {
        v_v = fabs(g_peek(D_xin,LEN_xin,CH_xin,v_i,0));
        v_p = g_peek(D_pk,LEN_pk,CH_pk,v_i,0);
        v_b0 = g_peek(D_lb,LEN_lb,CH_lb,v_i,0);
        if (((double)((v_bin) != (v_b0))) != 0.0) {
          v_y = fmin((1.0),(sqrt(fmax(0.0,v_p)) * (0.9)));
          if (((double)((v_i) < ((4.0)))) != 0.0) {
            g_poke(D_wl,LEN_wl,CH_wl,v_y,(v_b0 * (2.0)),v_i);
            g_poke(D_wl,LEN_wl,CH_wl,((0.0) - v_y),((v_b0 * (2.0)) + (1.0)),v_i);
          } else {
            v_ch = (v_i - (4.0));
            if (((double)((v_sc) == ((0.0)))) != 0.0) {
              g_poke(D_wa,LEN_wa,CH_wa,v_y,(v_b0 * (2.0)),v_ch);
              g_poke(D_wa,LEN_wa,CH_wa,((0.0) - v_y),((v_b0 * (2.0)) + (1.0)),v_ch);
            } else {
              if (((double)((v_sc) == ((1.0)))) != 0.0) {
                g_poke(D_wb,LEN_wb,CH_wb,v_y,(v_b0 * (2.0)),v_ch);
                g_poke(D_wb,LEN_wb,CH_wb,((0.0) - v_y),((v_b0 * (2.0)) + (1.0)),v_ch);
              } else {
                if (((double)((v_sc) == ((2.0)))) != 0.0) {
                  g_poke(D_wc,LEN_wc,CH_wc,v_y,(v_b0 * (2.0)),v_ch);
                  g_poke(D_wc,LEN_wc,CH_wc,((0.0) - v_y),((v_b0 * (2.0)) + (1.0)),v_ch);
                } else {
                  g_poke(D_wd,LEN_wd,CH_wd,v_y,(v_b0 * (2.0)),v_ch);
                  g_poke(D_wd,LEN_wd,CH_wd,((0.0) - v_y),((v_b0 * (2.0)) + (1.0)),v_ch);
                }
              }
            }
          }
          g_poke(D_lb,LEN_lb,CH_lb,v_bin,v_i,0);
          v_p = (0.0);
        }
        g_poke(D_pk,LEN_pk,CH_pk,fmax(v_p,v_v),v_i,0);
      }
      OUT1 = v_bin;
      if (out[0]) out[0][t] = (float) OUT1;
    }
  }
};
// ---------------------------------------------------------------- Synth
struct Synth {
  double SR = 44100.0;
  double P_sn0 = 60.0;
  double P_sv0 = 0.0;
  double P_sg0 = 0.0;
  double P_sn1 = 60.0;
  double P_sv1 = 0.0;
  double P_sg1 = 0.0;
  double P_sn2 = 60.0;
  double P_sv2 = 0.0;
  double P_sg2 = 0.0;
  double P_sn3 = 60.0;
  double P_sv3 = 0.0;
  double P_sg3 = 0.0;
  double P_sn4 = 60.0;
  double P_sv4 = 0.0;
  double P_sg4 = 0.0;
  double P_sn5 = 60.0;
  double P_sv5 = 0.0;
  double P_sg5 = 0.0;
  double P_sn6 = 60.0;
  double P_sv6 = 0.0;
  double P_sg6 = 0.0;
  double P_sn7 = 60.0;
  double P_sv7 = 0.0;
  double P_sg7 = 0.0;
  double P_a_wave = 50.0;
  double P_a_warp = 0.0;
  double P_a_semi = 0.0;
  double P_a_fine = 0.0;
  double P_a_lvl = 80.0;
  double P_a_uni = 1.0;
  double P_a_det = 25.0;
  double P_b_wave = 75.0;
  double P_b_warp = 0.0;
  double P_b_semi = -12.0;
  double P_b_fine = 5.0;
  double P_b_lvl = 0.0;
  double P_b_uni = 1.0;
  double P_b_det = 25.0;
  double P_sub_lvl = 0.0;
  double P_noise_lvl = 0.0;
  double P_f_type = 0.0;
  double P_f_cut = 60.0;
  double P_f_res = 20.0;
  double P_f_drive = 10.0;
  double P_f_env = 30.0;
  double P_f_key = 50.0;
  double P_e1_a = 5.0;
  double P_e1_d = 40.0;
  double P_e1_s = 70.0;
  double P_e1_r = 35.0;
  double P_e2_a = 10.0;
  double P_e2_d = 45.0;
  double P_e2_s = 30.0;
  double P_e2_r = 40.0;
  double P_l_rate = 30.0;
  double P_l_shape = 0.0;
  double P_l_sync = 0.0;
  double P_l_wave = 0.0;
  double P_l_cut = 0.0;
  double P_l_pitch = 0.0;
  double P_e2_wave = 0.0;
  double P_s_glide = 0.0;
  double P_mono = 0.0;
  double P_spread = 50.0;
  double P_s_lvl = 70.0;
  double P_follow = 0.0;
  double P_f_oct = 0.0;
  std::vector<double> V_vin; double* D_vin = nullptr; long LEN_vin = 3; long CH_vin = 8;
  std::vector<double> V_vst; double* D_vst = nullptr; long LEN_vst = 16; long CH_vst = 8;
  std::vector<double> V_ph; double* D_ph = nullptr; long LEN_ph = 10; long CH_ph = 8;
  double H_y_init = 0.0;
  double H_y_lph = 0.0;
  double H_y_sh = 0.0;
  double H_y_lsh = 0.0;
  double H_y_fp = -1.0;
  double H_y_fh = 0.0;
  double H_y_mlast = -1.0;
  static constexpr int numInputs = 2;
  static constexpr int numOutputs = 5;
  void init(double sr) { SR = sr;
    V_vin.assign((size_t) 3 * 8, 0.0); D_vin = V_vin.data();
    V_vst.assign((size_t) 16 * 8, 0.0); D_vst = V_vst.data();
    V_ph.assign((size_t) 10 * 8, 0.0); D_ph = V_ph.data();
  }
  void bind(const std::string& name, SharedBuffer& b) {
  }
  static std::vector<std::string> bufferNames() { return {}; }
  static int paramIndex(const std::string& n) {
    if (n == "sn0") return 0;
    if (n == "sv0") return 1;
    if (n == "sg0") return 2;
    if (n == "sn1") return 3;
    if (n == "sv1") return 4;
    if (n == "sg1") return 5;
    if (n == "sn2") return 6;
    if (n == "sv2") return 7;
    if (n == "sg2") return 8;
    if (n == "sn3") return 9;
    if (n == "sv3") return 10;
    if (n == "sg3") return 11;
    if (n == "sn4") return 12;
    if (n == "sv4") return 13;
    if (n == "sg4") return 14;
    if (n == "sn5") return 15;
    if (n == "sv5") return 16;
    if (n == "sg5") return 17;
    if (n == "sn6") return 18;
    if (n == "sv6") return 19;
    if (n == "sg6") return 20;
    if (n == "sn7") return 21;
    if (n == "sv7") return 22;
    if (n == "sg7") return 23;
    if (n == "a_wave") return 24;
    if (n == "a_warp") return 25;
    if (n == "a_semi") return 26;
    if (n == "a_fine") return 27;
    if (n == "a_lvl") return 28;
    if (n == "a_uni") return 29;
    if (n == "a_det") return 30;
    if (n == "b_wave") return 31;
    if (n == "b_warp") return 32;
    if (n == "b_semi") return 33;
    if (n == "b_fine") return 34;
    if (n == "b_lvl") return 35;
    if (n == "b_uni") return 36;
    if (n == "b_det") return 37;
    if (n == "sub_lvl") return 38;
    if (n == "noise_lvl") return 39;
    if (n == "f_type") return 40;
    if (n == "f_cut") return 41;
    if (n == "f_res") return 42;
    if (n == "f_drive") return 43;
    if (n == "f_env") return 44;
    if (n == "f_key") return 45;
    if (n == "e1_a") return 46;
    if (n == "e1_d") return 47;
    if (n == "e1_s") return 48;
    if (n == "e1_r") return 49;
    if (n == "e2_a") return 50;
    if (n == "e2_d") return 51;
    if (n == "e2_s") return 52;
    if (n == "e2_r") return 53;
    if (n == "l_rate") return 54;
    if (n == "l_shape") return 55;
    if (n == "l_sync") return 56;
    if (n == "l_wave") return 57;
    if (n == "l_cut") return 58;
    if (n == "l_pitch") return 59;
    if (n == "e2_wave") return 60;
    if (n == "s_glide") return 61;
    if (n == "mono") return 62;
    if (n == "spread") return 63;
    if (n == "s_lvl") return 64;
    if (n == "follow") return 65;
    if (n == "f_oct") return 66;
    return -1;
  }
  void setp(int i, double v) {
    switch (i) {
      case 0: P_sn0 = v; break;
      case 1: P_sv0 = v; break;
      case 2: P_sg0 = v; break;
      case 3: P_sn1 = v; break;
      case 4: P_sv1 = v; break;
      case 5: P_sg1 = v; break;
      case 6: P_sn2 = v; break;
      case 7: P_sv2 = v; break;
      case 8: P_sg2 = v; break;
      case 9: P_sn3 = v; break;
      case 10: P_sv3 = v; break;
      case 11: P_sg3 = v; break;
      case 12: P_sn4 = v; break;
      case 13: P_sv4 = v; break;
      case 14: P_sg4 = v; break;
      case 15: P_sn5 = v; break;
      case 16: P_sv5 = v; break;
      case 17: P_sg5 = v; break;
      case 18: P_sn6 = v; break;
      case 19: P_sv6 = v; break;
      case 20: P_sg6 = v; break;
      case 21: P_sn7 = v; break;
      case 22: P_sv7 = v; break;
      case 23: P_sg7 = v; break;
      case 24: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_a_wave = v; break;
      case 25: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_a_warp = v; break;
      case 26: if (v < -24.0) v = -24.0; if (v > 24.0) v = 24.0; P_a_semi = v; break;
      case 27: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_a_fine = v; break;
      case 28: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_a_lvl = v; break;
      case 29: if (v < 1.0) v = 1.0; if (v > 5.0) v = 5.0; P_a_uni = v; break;
      case 30: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_a_det = v; break;
      case 31: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_b_wave = v; break;
      case 32: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_b_warp = v; break;
      case 33: if (v < -24.0) v = -24.0; if (v > 24.0) v = 24.0; P_b_semi = v; break;
      case 34: if (v < -50.0) v = -50.0; if (v > 50.0) v = 50.0; P_b_fine = v; break;
      case 35: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_b_lvl = v; break;
      case 36: if (v < 1.0) v = 1.0; if (v > 5.0) v = 5.0; P_b_uni = v; break;
      case 37: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_b_det = v; break;
      case 38: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_sub_lvl = v; break;
      case 39: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_noise_lvl = v; break;
      case 40: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_f_type = v; break;
      case 41: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_f_cut = v; break;
      case 42: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_f_res = v; break;
      case 43: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_f_drive = v; break;
      case 44: if (v < -100.0) v = -100.0; if (v > 100.0) v = 100.0; P_f_env = v; break;
      case 45: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_f_key = v; break;
      case 46: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e1_a = v; break;
      case 47: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e1_d = v; break;
      case 48: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e1_s = v; break;
      case 49: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e1_r = v; break;
      case 50: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e2_a = v; break;
      case 51: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e2_d = v; break;
      case 52: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e2_s = v; break;
      case 53: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_e2_r = v; break;
      case 54: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_l_rate = v; break;
      case 55: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_l_shape = v; break;
      case 56: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_l_sync = v; break;
      case 57: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_l_wave = v; break;
      case 58: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_l_cut = v; break;
      case 59: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_l_pitch = v; break;
      case 60: if (v < -100.0) v = -100.0; if (v > 100.0) v = 100.0; P_e2_wave = v; break;
      case 61: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_s_glide = v; break;
      case 62: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_mono = v; break;
      case 63: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_spread = v; break;
      case 64: if (v < 0.0) v = 0.0; if (v > 100.0) v = 100.0; P_s_lvl = v; break;
      case 65: if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; P_follow = v; break;
      case 66: if (v < -24.0) v = -24.0; if (v > 24.0) v = 24.0; P_f_oct = v; break;
      default: break;
    }
  }
  double getp(int i) const {
    switch (i) {
      case 0: return P_sn0;
      case 1: return P_sv0;
      case 2: return P_sg0;
      case 3: return P_sn1;
      case 4: return P_sv1;
      case 5: return P_sg1;
      case 6: return P_sn2;
      case 7: return P_sv2;
      case 8: return P_sg2;
      case 9: return P_sn3;
      case 10: return P_sv3;
      case 11: return P_sg3;
      case 12: return P_sn4;
      case 13: return P_sv4;
      case 14: return P_sg4;
      case 15: return P_sn5;
      case 16: return P_sv5;
      case 17: return P_sg5;
      case 18: return P_sn6;
      case 19: return P_sv6;
      case 20: return P_sg6;
      case 21: return P_sn7;
      case 22: return P_sv7;
      case 23: return P_sg7;
      case 24: return P_a_wave;
      case 25: return P_a_warp;
      case 26: return P_a_semi;
      case 27: return P_a_fine;
      case 28: return P_a_lvl;
      case 29: return P_a_uni;
      case 30: return P_a_det;
      case 31: return P_b_wave;
      case 32: return P_b_warp;
      case 33: return P_b_semi;
      case 34: return P_b_fine;
      case 35: return P_b_lvl;
      case 36: return P_b_uni;
      case 37: return P_b_det;
      case 38: return P_sub_lvl;
      case 39: return P_noise_lvl;
      case 40: return P_f_type;
      case 41: return P_f_cut;
      case 42: return P_f_res;
      case 43: return P_f_drive;
      case 44: return P_f_env;
      case 45: return P_f_key;
      case 46: return P_e1_a;
      case 47: return P_e1_d;
      case 48: return P_e1_s;
      case 49: return P_e1_r;
      case 50: return P_e2_a;
      case 51: return P_e2_d;
      case 52: return P_e2_s;
      case 53: return P_e2_r;
      case 54: return P_l_rate;
      case 55: return P_l_shape;
      case 56: return P_l_sync;
      case 57: return P_l_wave;
      case 58: return P_l_cut;
      case 59: return P_l_pitch;
      case 60: return P_e2_wave;
      case 61: return P_s_glide;
      case 62: return P_mono;
      case 63: return P_spread;
      case 64: return P_s_lvl;
      case 65: return P_follow;
      case 66: return P_f_oct;
      default: return 0.0;
    }
  }
  static constexpr int numParams = 67;
  double* data(const std::string& n) {
    if (n == "vin") return D_vin;
    if (n == "vst") return D_vst;
    if (n == "ph") return D_ph;
    return nullptr;
  }
  void perform(const float* const* in, float* const* out, long n) {
    for (long t = 0; t < n; t++) {
      double IN1 = in[0] ? (double) in[0][t] : 0.0;
      double IN2 = in[1] ? (double) in[1][t] : 0.0;
      double OUT1 = 0.0;
      double OUT2 = 0.0;
      double OUT3 = 0.0;
      double OUT4 = 0.0;
      double OUT5 = 0.0;
      double v_sr = 0.0;
      double v_k = 0.0;
      double v_j = 0.0;
      double v_ta1 = 0.0;
      double v_td1 = 0.0;
      double v_tr1 = 0.0;
      double v_sl1 = 0.0;
      double v_ta2 = 0.0;
      double v_td2 = 0.0;
      double v_tr2 = 0.0;
      double v_sl2 = 0.0;
      double v_gk = 0.0;
      double v_lhz = 0.0;
      double v_lph = 0.0;
      double v_di = 0.0;
      double v_bpc = 0.0;
      double v_lsh = 0.0;
      double v_lfo = 0.0;
      double v_fp = 0.0;
      double v_fh = 0.0;
      double v_outl = 0.0;
      double v_outr = 0.0;
      double v_nact = 0.0;
      double v_nua = 0.0;
      double v_nub = 0.0;
      double v_ala = 0.0;
      double v_alb = 0.0;
      double v_asub = 0.0;
      double v_anz = 0.0;
      double v_fty = 0.0;
      double v_rq = 0.0;
      double v_dgain = 0.0;
      double v_spr = 0.0;
      double v_nt = 0.0;
      double v_vl = 0.0;
      double v_tg = 0.0;
      double v_vgate = 0.0;
      double v_trig = 0.0;
      double v_e1 = 0.0;
      double v_s1 = 0.0;
      double v_e2 = 0.0;
      double v_s2 = 0.0;
      double v_pn = 0.0;
      double v_pv = 0.0;
      double v_wmod = 0.0;
      double v_vel = 0.0;
      double v_sl = 0.0;
      double v_sr2 = 0.0;
      double v_o = 0.0;
      double v_nu = 0.0;
      double v_lv = 0.0;
      double v_wv = 0.0;
      double v_wp = 0.0;
      double v_st = 0.0;
      double v_fi = 0.0;
      double v_dt = 0.0;
      double v_wpos = 0.0;
      double v_wi = 0.0;
      double v_wf = 0.0;
      double v_wexp = 0.0;
      double v_ug = 0.0;
      double v_u = 0.0;
      double v_uo = 0.0;
      double v_hz = 0.0;
      double v_inc = 0.0;
      double v_p = 0.0;
      double v_q = 0.0;
      double v_w0 = 0.0;
      double v_w1 = 0.0;
      double v_bl = 0.0;
      double v_tt = 0.0;
      double v_w2 = 0.0;
      double v_p2 = 0.0;
      double v_bl2 = 0.0;
      double v_w3 = 0.0;
      double v_p3 = 0.0;
      double v_bl3 = 0.0;
      double v_w4 = 0.0;
      double v_wa = 0.0;
      double v_wb = 0.0;
      double v_smp = 0.0;
      double v_pa = 0.0;
      double v_sp_ = 0.0;
      double v_sv_ = 0.0;
      double v_nz = 0.0;
      double v_co = 0.0;
      double v_g = 0.0;
      double v_a1 = 0.0;
      double v_xl = 0.0;
      double v_xr = 0.0;
      double v_z1 = 0.0;
      double v_z2 = 0.0;
      double v_v1 = 0.0;
      double v_v2 = 0.0;
      double v_yl = 0.0;
      double v_yr = 0.0;
      double v_amp = 0.0;
      double v_lv_ = 0.0;
      double v_ol = 0.0;
      double v_orr = 0.0;
      g_poke(D_vin,LEN_vin,CH_vin,P_sn0,(0.0),(0.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv0,(1.0),(0.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg0,(2.0),(0.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn1,(0.0),(1.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv1,(1.0),(1.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg1,(2.0),(1.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn2,(0.0),(2.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv2,(1.0),(2.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg2,(2.0),(2.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn3,(0.0),(3.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv3,(1.0),(3.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg3,(2.0),(3.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn4,(0.0),(4.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv4,(1.0),(4.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg4,(2.0),(4.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn5,(0.0),(5.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv5,(1.0),(5.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg5,(2.0),(5.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn6,(0.0),(6.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv6,(1.0),(6.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg6,(2.0),(6.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sn7,(0.0),(7.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sv7,(1.0),(7.0));
      g_poke(D_vin,LEN_vin,CH_vin,P_sg7,(2.0),(7.0));
      v_sr = SR;
      if (((double)((H_y_init) == ((0.0)))) != 0.0) {
        for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
          g_poke(D_vst,LEN_vst,CH_vst,g_peek(D_vin,LEN_vin,CH_vin,(2.0),v_k),(0.0),v_k);
          for (v_j = (0.0); ((double)((v_j) < (((2.0) * (5.0))))) != 0.0; v_j = v_j + ((1.0))) {
            g_poke(D_ph,LEN_ph,CH_ph,fabs(g_noise()),v_j,v_k);
          }
        }
        H_y_init = (1.0);
      }
      v_ta1 = ((((1.0) + (pow((P_e1_a / (100.0)),(3.0)) * (5000.0))))*SR/1000.0);
      v_td1 = ((((5.0) + (pow((P_e1_d / (100.0)),(3.0)) * (6000.0))))*SR/1000.0);
      v_tr1 = ((((5.0) + (pow((P_e1_r / (100.0)),(3.0)) * (8000.0))))*SR/1000.0);
      v_sl1 = (P_e1_s / (100.0));
      v_ta2 = ((((1.0) + (pow((P_e2_a / (100.0)),(3.0)) * (5000.0))))*SR/1000.0);
      v_td2 = ((((5.0) + (pow((P_e2_d / (100.0)),(3.0)) * (6000.0))))*SR/1000.0);
      v_tr2 = ((((5.0) + (pow((P_e2_r / (100.0)),(3.0)) * (8000.0))))*SR/1000.0);
      v_sl2 = (P_e2_s / (100.0));
      v_gk = ((1.0) - exp(((-(1.0)) / ((((1.0) + (pow((P_s_glide / (100.0)),(2.0)) * (2000.0))))*SR/1000.0))));
      v_lhz = ((0.05) * pow((400.0),(P_l_rate / (100.0))));
      v_lph = (0.0);
      if (((double)((P_l_sync) > ((0.0)))) != 0.0) {
        v_di = floor(((P_l_rate / (100.0)) * (6.99)));
        v_bpc = (16.0);
        if (((double)((v_di) == ((1.0)))) != 0.0) {
          v_bpc = (8.0);
        }
        if (((double)((v_di) == ((2.0)))) != 0.0) {
          v_bpc = (4.0);
        }
        if (((double)((v_di) == ((3.0)))) != 0.0) {
          v_bpc = (2.0);
        }
        if (((double)((v_di) == ((4.0)))) != 0.0) {
          v_bpc = (1.0);
        }
        if (((double)((v_di) == ((5.0)))) != 0.0) {
          v_bpc = (0.5);
        }
        if (((double)((v_di) == ((6.0)))) != 0.0) {
          v_bpc = (0.25);
        }
        v_lph = g_wrap((IN2 / v_bpc),(0.0),(1.0));
      } else {
        v_lph = g_wrap((H_y_lph + (v_lhz / v_sr)),(0.0),(1.0));
      }
      v_lsh = floor(((P_l_shape / (100.0)) * (4.99)));
      v_lfo = sin(((2.0*M_PI) * v_lph));
      if (((double)((v_lsh) == ((1.0)))) != 0.0) {
        v_lfo = ((1.0) - ((4.0) * fabs((v_lph - (0.5)))));
      }
      if (((double)((v_lsh) == ((2.0)))) != 0.0) {
        v_lfo = (((2.0) * v_lph) - (1.0));
      }
      if (((double)((v_lsh) == ((3.0)))) != 0.0) {
        v_lfo = ((1.0) - ((2.0) * ((double)((v_lph) >= ((0.5))))));
      }
      if (((double)((v_lsh) == ((4.0)))) != 0.0) {
        if (((double)((((double)((v_lph) < ((H_y_lph - (0.5)))))) || (((double)((((double)((P_l_sync) > ((0.0))))) && (((double)((v_lph) < (H_y_lph))))))))) != 0.0) {
          H_y_sh = g_noise();
        }
        v_lfo = H_y_sh;
      }
      H_y_lph = v_lph;
      v_fp = IN1;
      v_fh = H_y_fh;
      if (((double)((v_fp) > ((20.0)))) != 0.0) {
        H_y_fp = v_fp;
        v_fh = (((90.0))*SR/1000.0);
      } else {
        v_fh = fmax((0.0),(v_fh - (1.0)));
      }
      H_y_fh = v_fh;
      v_outl = (0.0);
      v_outr = (0.0);
      v_nact = (0.0);
      v_nua = floor(g_clamp(P_a_uni,(1.0),(5.0)));
      v_nub = floor(g_clamp(P_b_uni,(1.0),(5.0)));
      v_ala = (P_a_lvl / (100.0));
      v_alb = (P_b_lvl / (100.0));
      v_asub = (P_sub_lvl / (100.0));
      v_anz = (P_noise_lvl / (100.0));
      v_fty = floor(((P_f_type / (100.0)) * (2.99)));
      v_rq = ((2.0) - ((1.96) * pow((P_f_res / (100.0)),(0.7))));
      v_dgain = ((1.0) + ((P_f_drive / (100.0)) * (6.0)));
      v_spr = (P_spread / (100.0));
      for (v_k = (0.0); ((double)((v_k) < ((8.0)))) != 0.0; v_k = v_k + ((1.0))) {
        v_nt = g_peek(D_vin,LEN_vin,CH_vin,(0.0),v_k);
        v_vl = g_peek(D_vin,LEN_vin,CH_vin,(1.0),v_k);
        v_tg = g_peek(D_vin,LEN_vin,CH_vin,(2.0),v_k);
        if (((double)((P_follow) > ((0.0)))) != 0.0) {
          v_nt = (H_y_fp + P_f_oct);
          v_vl = (((100.0) * ((double)((v_fh) > ((0.0))))) * ((double)((v_k) == ((0.0)))));
          v_tg = g_peek(D_vst,LEN_vst,CH_vst,(0.0),v_k);
        } else {
          if (((double)((((double)((P_mono) > ((0.0))))) && (((double)((v_k) > ((0.0))))))) != 0.0) {
            v_vl = (0.0);
          }
        }
        v_vgate = ((double)((v_vl) > ((0.0))));
        v_trig = ((double)((v_tg) != (g_peek(D_vst,LEN_vst,CH_vst,(0.0),v_k))));
        g_poke(D_vst,LEN_vst,CH_vst,v_tg,(0.0),v_k);
        if (((double)((v_trig) && (v_vgate))) != 0.0) {
          g_poke(D_vst,LEN_vst,CH_vst,(1.0),(3.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(1.0),(5.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(v_vl / (127.0)),(8.0),v_k);
          if (((double)((((double)((g_peek(D_vst,LEN_vst,CH_vst,(2.0),v_k)) < ((0.0001))))) || (((double)((P_s_glide) < ((0.5))))))) != 0.0) {
            g_poke(D_vst,LEN_vst,CH_vst,v_nt,(6.0),v_k);
          }
        }
        if (((double)((v_vgate) && (((double)((g_peek(D_vst,LEN_vst,CH_vst,(3.0),v_k)) == ((0.0))))))) != 0.0) {
          g_poke(D_vst,LEN_vst,CH_vst,(1.0),(3.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(1.0),(5.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(v_vl / (127.0)),(8.0),v_k);
        }
        if (((double)((((double)((v_vgate) == ((0.0))))) && (((double)((g_peek(D_vst,LEN_vst,CH_vst,(1.0),v_k)) > ((0.0))))))) != 0.0) {
          g_poke(D_vst,LEN_vst,CH_vst,(4.0),(3.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(4.0),(5.0),v_k);
        }
        g_poke(D_vst,LEN_vst,CH_vst,v_vgate,(1.0),v_k);
        v_e1 = g_peek(D_vst,LEN_vst,CH_vst,(2.0),v_k);
        v_s1 = g_peek(D_vst,LEN_vst,CH_vst,(3.0),v_k);
        if (((double)((v_s1) == ((1.0)))) != 0.0) {
          v_e1 = (v_e1 + (((1.25) - v_e1) * ((1.0) - exp(((-(1.0)) / v_ta1)))));
          if (((double)((v_e1) >= ((1.0)))) != 0.0) {
            v_e1 = (1.0);
            v_s1 = (2.0);
          }
        } else {
          if (((double)((v_s1) == ((2.0)))) != 0.0) {
            v_e1 = (v_e1 + ((v_sl1 - v_e1) * ((1.0) - exp(((-(3.0)) / v_td1)))));
          } else {
            if (((double)((v_s1) == ((4.0)))) != 0.0) {
              v_e1 = (v_e1 * exp(((-(4.0)) / v_tr1)));
              if (((double)((v_e1) < ((0.0001)))) != 0.0) {
                v_e1 = (0.0);
                v_s1 = (0.0);
              }
            }
          }
        }
        g_poke(D_vst,LEN_vst,CH_vst,v_e1,(2.0),v_k);
        g_poke(D_vst,LEN_vst,CH_vst,v_s1,(3.0),v_k);
        v_e2 = g_peek(D_vst,LEN_vst,CH_vst,(4.0),v_k);
        v_s2 = g_peek(D_vst,LEN_vst,CH_vst,(5.0),v_k);
        if (((double)((v_s2) == ((1.0)))) != 0.0) {
          v_e2 = (v_e2 + (((1.25) - v_e2) * ((1.0) - exp(((-(1.0)) / v_ta2)))));
          if (((double)((v_e2) >= ((1.0)))) != 0.0) {
            v_e2 = (1.0);
            v_s2 = (2.0);
          }
        } else {
          if (((double)((v_s2) == ((2.0)))) != 0.0) {
            v_e2 = (v_e2 + ((v_sl2 - v_e2) * ((1.0) - exp(((-(3.0)) / v_td2)))));
          } else {
            if (((double)((v_s2) == ((4.0)))) != 0.0) {
              v_e2 = (v_e2 * exp(((-(4.0)) / v_tr2)));
              if (((double)((v_e2) < ((0.0001)))) != 0.0) {
                v_e2 = (0.0);
                v_s2 = (0.0);
              }
            }
          }
        }
        g_poke(D_vst,LEN_vst,CH_vst,v_e2,(4.0),v_k);
        g_poke(D_vst,LEN_vst,CH_vst,v_s2,(5.0),v_k);
        if (((double)((v_s1) > ((0.0)))) != 0.0) {
          v_nact = (v_nact + (1.0));
          v_pn = g_peek(D_vst,LEN_vst,CH_vst,(6.0),v_k);
          v_pn = (v_pn + ((v_nt - v_pn) * v_gk));
          g_poke(D_vst,LEN_vst,CH_vst,v_pn,(6.0),v_k);
          v_pv = (v_pn + ((v_lfo * pow((P_l_pitch / (100.0)),(2.0))) * (1.0)));
          v_wmod = ((((v_lfo * P_l_wave) / (100.0)) * (50.0)) + (((v_e2 * P_e2_wave) / (100.0)) * (50.0)));
          v_vel = g_peek(D_vst,LEN_vst,CH_vst,(8.0),v_k);
          v_sl = (0.0);
          v_sr2 = (0.0);
          for (v_o = (0.0); ((double)((v_o) < ((2.0)))) != 0.0; v_o = v_o + ((1.0))) {
            v_nu = v_nua;
            v_lv = v_ala;
            v_wv = P_a_wave;
            v_wp = P_a_warp;
            v_st = P_a_semi;
            v_fi = P_a_fine;
            v_dt = P_a_det;
            if (((double)((v_o) == ((1.0)))) != 0.0) {
              v_nu = v_nub;
              v_lv = v_alb;
              v_wv = P_b_wave;
              v_wp = P_b_warp;
              v_st = P_b_semi;
              v_fi = P_b_fine;
              v_dt = P_b_det;
            }
            if (((double)((v_lv) > ((0.001)))) != 0.0) {
              v_wpos = (g_clamp((v_wv + v_wmod),(0.0),(100.0)) / (25.0));
              v_wi = fmin((3.0),floor(v_wpos));
              v_wf = (v_wpos - v_wi);
              v_wexp = ((1.0) + ((v_wp / (100.0)) * (4.0)));
              v_ug = (v_lv / sqrt(fmax(0.0,v_nu)));
              for (v_u = (0.0); ((double)((v_u) < (v_nu))) != 0.0; v_u = v_u + ((1.0))) {
                v_uo = (0.0);
                if (((double)((v_nu) > ((1.0)))) != 0.0) {
                  v_uo = (((v_u / (v_nu - (1.0))) * (2.0)) - (1.0));
                }
                v_hz = (440.0*pow(2.0,(((((v_pv + v_st) + (v_fi / (100.0))) + (((v_uo * v_dt) / (100.0)) * (0.5))))-69.0)/12.0));
                v_inc = fmin((0.45),(v_hz / v_sr));
                v_p = g_wrap((g_peek(D_ph,LEN_ph,CH_ph,((v_o * (5.0)) + v_u),v_k) + v_inc),(0.0),(1.0));
                g_poke(D_ph,LEN_ph,CH_ph,v_p,((v_o * (5.0)) + v_u),v_k);
                v_q = v_p;
                if (((double)((v_wp) > ((0.5)))) != 0.0) {
                  v_q = pow(v_p,v_wexp);
                }
                v_w0 = sin(((2.0*M_PI) * v_q));
                v_w1 = ((1.0) - ((4.0) * fabs((v_q - (0.5)))));
                v_bl = (0.0);
                if (((double)((v_p) < (v_inc))) != 0.0) {
                  v_tt = (v_p / v_inc);
                  v_bl = (((v_tt + v_tt) - (v_tt * v_tt)) - (1.0));
                } else {
                  if (((double)((v_p) > (((1.0) - v_inc)))) != 0.0) {
                    v_tt = ((v_p - (1.0)) / v_inc);
                    v_bl = ((((v_tt * v_tt) + v_tt) + v_tt) + (1.0));
                  }
                }
                v_w2 = ((((2.0) * v_q) - (1.0)) - (v_bl * ((double)((v_wp) <= ((0.5))))));
                v_p2 = g_wrap((v_p + (0.5)),(0.0),(1.0));
                v_bl2 = (0.0);
                if (((double)((v_p2) < (v_inc))) != 0.0) {
                  v_tt = (v_p2 / v_inc);
                  v_bl2 = (((v_tt + v_tt) - (v_tt * v_tt)) - (1.0));
                } else {
                  if (((double)((v_p2) > (((1.0) - v_inc)))) != 0.0) {
                    v_tt = ((v_p2 - (1.0)) / v_inc);
                    v_bl2 = ((((v_tt * v_tt) + v_tt) + v_tt) + (1.0));
                  }
                }
                v_w3 = (((((double)((v_q) < ((0.5)))) * (2.0)) - (1.0)) + ((v_bl - v_bl2) * ((double)((v_wp) <= ((0.5))))));
                v_p3 = g_wrap((v_p + (0.88)),(0.0),(1.0));
                v_bl3 = (0.0);
                if (((double)((v_p3) < (v_inc))) != 0.0) {
                  v_tt = (v_p3 / v_inc);
                  v_bl3 = (((v_tt + v_tt) - (v_tt * v_tt)) - (1.0));
                } else {
                  if (((double)((v_p3) > (((1.0) - v_inc)))) != 0.0) {
                    v_tt = ((v_p3 - (1.0)) / v_inc);
                    v_bl3 = ((((v_tt * v_tt) + v_tt) + v_tt) + (1.0));
                  }
                }
                v_w4 = (((((((double)((v_q) < ((0.12)))) * (2.0)) - (1.0)) + ((v_bl - v_bl3) * ((double)((v_wp) <= ((0.5)))))) * (0.8)) + (0.6));
                v_wa = v_w0;
                v_wb = v_w1;
                if (((double)((v_wi) == ((1.0)))) != 0.0) {
                  v_wa = v_w1;
                  v_wb = v_w2;
                }
                if (((double)((v_wi) == ((2.0)))) != 0.0) {
                  v_wa = v_w2;
                  v_wb = v_w3;
                }
                if (((double)((v_wi) == ((3.0)))) != 0.0) {
                  v_wa = v_w3;
                  v_wb = v_w4;
                }
                v_smp = ((v_wa + ((v_wb - v_wa) * v_wf)) * v_ug);
                v_pa = ((0.5) + ((v_uo * (0.5)) * v_spr));
                v_sl = (v_sl + ((v_smp * ((1.0) - v_pa)) * (1.4142)));
                v_sr2 = (v_sr2 + ((v_smp * v_pa) * (1.4142)));
              }
            }
          }
          if (((double)((v_asub) > ((0.001)))) != 0.0) {
            v_sp_ = g_wrap((g_peek(D_vst,LEN_vst,CH_vst,(15.0),v_k) + fmin((0.45),((440.0*pow(2.0,((((v_pv + P_a_semi) - (12.0)))-69.0)/12.0)) / v_sr))),(0.0),(1.0));
            g_poke(D_vst,LEN_vst,CH_vst,v_sp_,(15.0),v_k);
            v_sv_ = (sin(((2.0*M_PI) * v_sp_)) * v_asub);
            v_sl = (v_sl + v_sv_);
            v_sr2 = (v_sr2 + v_sv_);
          }
          if (((double)((v_anz) > ((0.001)))) != 0.0) {
            v_nz = ((g_noise() * v_anz) * (0.5));
            v_sl = (v_sl + v_nz);
            v_sr2 = (v_sr2 + v_nz);
          }
          v_co = (((20.0) * pow((1000.0),(P_f_cut / (100.0)))) * pow((2.0),(((((v_e2 * P_f_env) / (100.0)) * (6.0)) + ((((v_pv - (60.0)) / (12.0)) * P_f_key) / (100.0))) + (((v_lfo * P_l_cut) / (100.0)) * (3.0)))));
          v_co = g_clamp(v_co,(20.0),(v_sr * (0.45)));
          v_g = tan(((M_PI * v_co) / v_sr));
          v_a1 = ((1.0) / ((1.0) + (v_g * (v_g + v_rq))));
          v_xl = (tanh((v_sl * v_dgain)) / sqrt(fmax(0.0,v_dgain)));
          v_xr = (tanh((v_sr2 * v_dgain)) / sqrt(fmax(0.0,v_dgain)));
          v_z1 = g_peek(D_vst,LEN_vst,CH_vst,(9.0),v_k);
          v_z2 = g_peek(D_vst,LEN_vst,CH_vst,(10.0),v_k);
          v_v1 = ((v_z1 + (v_g * (v_xl - v_z2))) * v_a1);
          v_v2 = (v_z2 + (v_g * v_v1));
          g_poke(D_vst,LEN_vst,CH_vst,(((2.0) * v_v1) - v_z1),(9.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(((2.0) * v_v2) - v_z2),(10.0),v_k);
          v_yl = v_v2;
          if (((double)((v_fty) == ((1.0)))) != 0.0) {
            v_yl = (v_v1 * v_rq);
          }
          if (((double)((v_fty) == ((2.0)))) != 0.0) {
            v_yl = ((v_xl - (v_rq * v_v1)) - v_v2);
          }
          v_z1 = g_peek(D_vst,LEN_vst,CH_vst,(11.0),v_k);
          v_z2 = g_peek(D_vst,LEN_vst,CH_vst,(12.0),v_k);
          v_v1 = ((v_z1 + (v_g * (v_xr - v_z2))) * v_a1);
          v_v2 = (v_z2 + (v_g * v_v1));
          g_poke(D_vst,LEN_vst,CH_vst,(((2.0) * v_v1) - v_z1),(11.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(((2.0) * v_v2) - v_z2),(12.0),v_k);
          v_yr = v_v2;
          if (((double)((v_fty) == ((1.0)))) != 0.0) {
            v_yr = (v_v1 * v_rq);
          }
          if (((double)((v_fty) == ((2.0)))) != 0.0) {
            v_yr = ((v_xr - (v_rq * v_v1)) - v_v2);
          }
          v_amp = (v_e1 * ((0.35) + ((0.65) * v_vel)));
          v_outl = (v_outl + (v_yl * v_amp));
          v_outr = (v_outr + (v_yr * v_amp));
        } else {
          g_poke(D_vst,LEN_vst,CH_vst,(0.0),(9.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(0.0),(10.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(0.0),(11.0),v_k);
          g_poke(D_vst,LEN_vst,CH_vst,(0.0),(12.0),v_k);
        }
      }
      v_lv_ = (pow((P_s_lvl / (100.0)),(2.0)) * (1.2));
      v_ol = tanh((v_outl * v_lv_));
      v_orr = tanh((v_outr * v_lv_));
      OUT1 = v_ol;
      OUT2 = v_orr;
      OUT3 = ((v_ol + v_orr) * (0.5));
      OUT4 = v_nact;
      OUT5 = v_lfo;
      if (out[0]) out[0][t] = (float) OUT1;
      if (out[1]) out[1][t] = (float) OUT2;
      if (out[2]) out[2][t] = (float) OUT3;
      if (out[3]) out[3][t] = (float) OUT4;
      if (out[4]) out[4][t] = (float) OUT5;
    }
  }
};
} // namespace arco
