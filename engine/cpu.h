#pragma once
#include <vector>
#include "batch.h"
#include "p0.h"
#include "p2.h"

namespace cpu {

inline batch::Result runOne(const batch::Program& p, bool pipelined) {
  rv::Flat m;
  std::memcpy(m.regs, p.regs, sizeof m.regs);
  std::memcpy(m.mem, p.mem, sizeof m.mem);
  m.regs[0] = 0;
  rv::Counters c = pipelined ? rv::runP2(m, p.instructionCount, p.cycleLimit) : rv::runP0(m, p.instructionCount, p.cycleLimit);
  batch::Result r{};
  std::memcpy(r.regs, m.regs, sizeof r.regs);
  std::memcpy(r.mem, m.mem, sizeof r.mem);
  r.pc = c.pc;
  r.cycles = c.cycles;
  r.retired = c.retired;
  r.stalls = c.stalls;
  r.flushes = c.flushes;
  r.status = c.status;
  return r;
}

inline void run(const std::vector<batch::Program>& programs, const std::vector<batch::Result>& expected,
                std::vector<batch::Result>& out, bool pipelined) {
  out.resize(programs.size());
  for (size_t i = 0; i < programs.size(); i++) {
    out[i] = runOne(programs[i], pipelined);
    out[i].pass = batch::same(out[i], expected[i % expected.size()]) ? 1 : 0;
  }
}

}
