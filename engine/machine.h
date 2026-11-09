#pragma once
#include "rv32.h"

namespace rv {

constexpr uint32_t HALTED = 1, LIMIT = 2;

struct Counters {
  uint32_t pc, cycles, retired, stalls, flushes, status;
};

template <class M>
HD uint32_t readWord(const M& m, uint32_t addr) {
  uint32_t a = addr & 0xff;
  return static_cast<uint32_t>(m.byte(a)) | (static_cast<uint32_t>(m.byte((a + 1) & 0xff)) << 8)
       | (static_cast<uint32_t>(m.byte((a + 2) & 0xff)) << 16) | (static_cast<uint32_t>(m.byte((a + 3) & 0xff)) << 24);
}

template <class M>
HD void writeWord(M& m, uint32_t addr, uint32_t v) {
  uint32_t a = addr & 0xff;
  for (uint32_t i = 0; i < 4; i++) m.setByte((a + i) & 0xff, static_cast<uint8_t>(v >> (8 * i)));
}

struct Flat {
  uint32_t regs[32];
  uint8_t mem[256];
  uint32_t reg(uint32_t i) const { return regs[i]; }
  void setReg(uint32_t i, uint32_t v) { regs[i] = v; }
  uint8_t byte(uint32_t a) const { return mem[a]; }
  void setByte(uint32_t a, uint8_t v) { mem[a] = v; }
};

}
