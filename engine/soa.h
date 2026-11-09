#pragma once
#include <vector>
#include "batch.h"
#include "p0.h"

namespace soa {

HD uint32_t loadWord(const uint8_t* p) {
  uint32_t w;
  __builtin_memcpy(&w, p, 4);
  return w;
}

HD void storeWord(uint8_t* p, uint32_t w) {
  __builtin_memcpy(p, &w, 4);
}

struct View {
  uint32_t* regs;
  uint32_t* mem;
  uint32_t n, t;
  HD uint32_t reg(uint32_t i) const { return regs[i * n + t]; }
  HD void setReg(uint32_t i, uint32_t v) { regs[i * n + t] = v; }
  HD uint8_t byte(uint32_t a) const { return static_cast<uint8_t>(mem[(a >> 2) * n + t] >> ((a & 3) * 8)); }
  HD void setByte(uint32_t a, uint8_t v) {
    uint32_t& w = mem[(a >> 2) * n + t];
    uint32_t s = (a & 3) * 8;
    w = (w & ~(0xffu << s)) | (static_cast<uint32_t>(v) << s);
  }
};

struct Args {
  uint32_t n, unique;
  uint32_t *regs, *mem, *count, *limit, *pc, *cycles, *retired, *status, *pass;
  const batch::Program* in;
  const batch::Result* want;
  batch::Result* out;
};

HD void loadThread(const Args& a, uint32_t t) {
  const batch::Program& p = a.in[t];
  View v{a.regs, a.mem, a.n, t};
  v.setReg(0, 0);
  for (uint32_t r = 1; r < 32; r++) v.setReg(r, p.regs[r]);
  for (uint32_t w = 0; w < 64; w++) a.mem[w * a.n + t] = loadWord(p.mem + 4 * w);
  a.count[t] = p.instructionCount;
  a.limit[t] = p.cycleLimit;
}

HD void p0Thread(const Args& a, uint32_t t) {
  View v{a.regs, a.mem, a.n, t};
  rv::Counters c = rv::runP0(v, a.count[t], a.limit[t]);
  a.pc[t] = c.pc;
  a.cycles[t] = c.cycles;
  a.retired[t] = c.retired;
  a.status[t] = c.status;
}

HD void storeThread(const Args& a, uint32_t t) {
  View v{a.regs, a.mem, a.n, t};
  const batch::Result& w = a.want[t % a.unique];
  bool ok = a.pc[t] == w.pc && a.cycles[t] == w.cycles && a.retired[t] == w.retired && w.stalls == 0 && w.flushes == 0
         && a.status[t] == w.status;
  for (uint32_t k = 0; k < 32; k++) ok = ok && v.reg(k) == w.regs[k];
  for (uint32_t k = 0; k < 64; k++) ok = ok && a.mem[k * a.n + t] == loadWord(w.mem + 4 * k);
  a.pass[t] = ok ? 1u : 0u;
  if (!a.out) return;
  batch::Result& r = a.out[t];
  for (uint32_t k = 0; k < 32; k++) r.regs[k] = v.reg(k);
  for (uint32_t k = 0; k < 64; k++) storeWord(r.mem + 4 * k, a.mem[k * a.n + t]);
  r.pc = a.pc[t];
  r.cycles = a.cycles[t];
  r.retired = a.retired[t];
  r.stalls = 0;
  r.flushes = 0;
  r.status = a.status[t];
  r.pass = a.pass[t];
}

inline void runOnHost(const std::vector<batch::Program>& programs, const std::vector<batch::Result>& expected,
                      std::vector<batch::Result>& out) {
  uint32_t n = static_cast<uint32_t>(programs.size());
  std::vector<uint32_t> regs(32ull * n), mem(64ull * n), count(n), limit(n), pc(n), cycles(n), retired(n), status(n), pass(n);
  out.assign(n, batch::Result{});
  Args a{n, static_cast<uint32_t>(expected.size()), regs.data(), mem.data(), count.data(), limit.data(), pc.data(),
         cycles.data(), retired.data(), status.data(), pass.data(), programs.data(), expected.data(), out.data()};
  for (uint32_t t = 0; t < n; t++) loadThread(a, t);
  for (uint32_t t = 0; t < n; t++) p0Thread(a, t);
  for (uint32_t t = 0; t < n; t++) storeThread(a, t);
}

}
