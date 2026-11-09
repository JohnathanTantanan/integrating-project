#pragma once
#include "machine.h"

namespace rv {

template <class M>
HD Counters runP0(M& m, uint32_t n, uint32_t limit) {
  Counters c{TEXT_BASE, 0, 0, 0, 0, LIMIT};
  if (!canFetch(c.pc, n)) {
    if (limit > 0) { c.cycles = 1; c.status = HALTED; }
    return c;
  }
  while (true) {
    uint32_t left = limit - c.cycles;
    if (left == 0) return c;
    uint32_t ir = readWord(m, c.pc);
    Decoded d = decode(ir);
    if (left < 2) { c.cycles += 1; return c; }
    uint32_t a = m.reg(d.rs1), b = m.reg(d.rs2);
    int32_t imm1 = d.isBranch ? (d.imm >> 1)
      : (d.isStore || d.opcode == OP_I || d.isLoad) ? d.imm : sext((ir >> 20) & 0xfff, 12);
    if (left < 3) { c.cycles += 2; return c; }
    uint32_t target = c.pc + static_cast<uint32_t>(imm1 * 2);
    bool cond = compare(d.funct3, a, b);
    uint32_t result = alu(d, a, d.isR ? b : static_cast<uint32_t>(imm1));
    if (left < 4) { c.cycles += 3; return c; }
    uint32_t next = (d.isBranch && cond) ? target : c.pc + 4;
    uint32_t addr = result & 0xff;
    uint32_t lmd = readWord(m, addr);
    bool memOp = !d.isBranch;
    if (memOp && !d.isLoad && d.isStore) writeWord(m, addr, b);
    c.pc = next;
    if (left < 5) { c.cycles += 4; return c; }
    if (memOp && d.writesRd) m.setReg(d.rd, d.isLoad ? lmd : result);
    m.setReg(0, 0);
    c.retired += 1;
    c.cycles += 5;
    if (!canFetch(c.pc, n)) { c.status = HALTED; return c; }
  }
}

}
