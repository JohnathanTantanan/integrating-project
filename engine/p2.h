#pragma once
#include "machine.h"

namespace rv {

struct P2 {
  uint32_t pc = TEXT_BASE;
  struct { uint32_t ir = 0, pc = 0; bool slot = false, squashed = false; } ifid;
  struct { uint32_t a = 0, b = 0, imm = 0, ir = 0, pc = 0; bool slot = false, ready = true; } idex;
  struct { uint32_t alu = 0, ir = 0, b = 0, pc = 0; bool slot = false; } exmem;
  struct { uint32_t lmd = 0, ir = 0, alu = 0, pc = 0; bool slot = false; } memwb;
  bool stallPending = false, halted = false;
  uint32_t cycles = 0, retired = 0, stalls = 0, flushes = 0;
};

HD bool reads(const Decoded& c, uint32_t reg) {
  return reg != 0 && ((c.usesRs1 && c.rs1 == reg) || (c.usesRs2 && c.rs2 == reg));
}

HD uint32_t immediateOf(uint32_t ir, const Decoded& d) {
  bool own = d.isStore || d.isBranch || d.opcode == OP_I || d.isLoad;
  return static_cast<uint32_t>(own ? d.imm : sext((ir >> 20) & 0xfff, 12));
}

template <class Ex, class Mw>
HD uint32_t forwardToEX(uint32_t reg, bool needed, uint32_t rf, const Ex& exmem, const Mw& memwb) {
  if (!needed || reg == 0) return reg == 0 ? 0 : rf;
  if (exmem.slot) {
    Decoded e = decode(exmem.ir);
    if (e.writesRd && e.rd == reg && !e.isLoad) return exmem.alu;
  }
  if (memwb.slot) {
    Decoded w = decode(memwb.ir);
    if (w.writesRd && w.rd == reg) return w.isLoad ? memwb.lmd : memwb.alu;
  }
  return rf;
}

template <class Ex>
HD uint32_t forwardToID(uint32_t reg, bool needed, uint32_t rf, const Ex& exmem) {
  if (!needed || reg == 0) return reg == 0 ? 0 : rf;
  if (exmem.slot) {
    Decoded e = decode(exmem.ir);
    if (e.writesRd && e.rd == reg && !e.isLoad) return exmem.alu;
  }
  return rf;
}

enum Stall { NONE, LOAD_USE, BRANCH_OPERAND };

HD Stall detectStall(const P2& s) {
  if (!s.ifid.slot || s.ifid.squashed) return NONE;
  Decoded c = decode(s.ifid.ir);
  if (!c.valid) return NONE;
  if (s.idex.slot) {
    Decoded x = decode(s.idex.ir);
    if (!c.isBranch && x.isLoad && x.writesRd && reads(c, x.rd)) return LOAD_USE;
    if (c.isBranch && x.writesRd && reads(c, x.rd)) return BRANCH_OPERAND;
  }
  if (c.isBranch && s.exmem.slot) {
    Decoded m = decode(s.exmem.ir);
    if (m.isLoad && m.writesRd && reads(c, m.rd)) return BRANCH_OPERAND;
  }
  return NONE;
}

template <class M>
HD void stepP2(P2& s, M& m, uint32_t n) {
  if (s.halted) return;
  const P2 old = s;

  int32_t lastWrite = -1;
  if (old.memwb.slot) {
    Decoded d = decode(old.memwb.ir);
    if (d.writesRd) {
      m.setReg(d.rd, d.isLoad ? old.memwb.lmd : old.memwb.alu);
      m.setReg(0, 0);
      lastWrite = static_cast<int32_t>(d.rd);
    }
    s.retired += 1;
  }

  s.memwb.slot = false;
  if (old.exmem.slot) {
    Decoded d = decode(old.exmem.ir);
    uint32_t addr = old.exmem.alu & 0xff;
    s.memwb.ir = old.exmem.ir;
    s.memwb.alu = old.exmem.alu;
    s.memwb.lmd = readWord(m, addr);
    s.memwb.pc = old.exmem.pc;
    s.memwb.slot = true;
    if (!d.isLoad && d.isStore) writeWord(m, addr, old.exmem.b);
  }

  s.exmem.slot = false;
  if (old.idex.slot && old.idex.ready) {
    Decoded d = decode(old.idex.ir);
    uint32_t a = forwardToEX(d.rs1, d.usesRs1, old.idex.a, old.exmem, old.memwb);
    uint32_t b = forwardToEX(d.rs2, d.usesRs2, old.idex.b, old.exmem, old.memwb);
    s.exmem.alu = alu(d, a, d.isR ? b : old.idex.imm);
    s.exmem.ir = old.idex.ir;
    s.exmem.b = b;
    s.exmem.pc = old.idex.pc;
    s.exmem.slot = true;
  }

  bool frozen = old.stallPending;
  Stall decision = frozen ? NONE : detectStall(old);
  bool holdBeforeID = decision == BRANCH_OPERAND;
  bool stall = frozen || holdBeforeID;
  bool flush = false;
  uint32_t target = 0;
  s.stallPending = false;
  s.idex.slot = false;
  s.idex.ready = true;

  if (frozen) {
    Decoded d = decode(old.idex.ir);
    s.idex = old.idex;
    s.idex.ready = true;
    if (d.usesRs1 && d.rs1 != 0 && lastWrite == static_cast<int32_t>(d.rs1)) s.idex.a = m.reg(d.rs1);
    if (d.usesRs2 && d.rs2 != 0 && lastWrite == static_cast<int32_t>(d.rs2)) s.idex.b = m.reg(d.rs2);
  } else if (!holdBeforeID && old.ifid.slot && !old.ifid.squashed) {
    Decoded d = decode(old.ifid.ir);
    uint32_t a = m.reg(d.rs1), b = m.reg(d.rs2);
    s.idex.ir = old.ifid.ir;
    s.idex.pc = old.ifid.pc;
    s.idex.a = a;
    s.idex.b = b;
    s.idex.imm = immediateOf(old.ifid.ir, d);
    s.idex.slot = true;
    s.idex.ready = true;
    if (d.isBranch) {
      uint32_t ca = forwardToID(d.rs1, d.usesRs1, a, old.exmem);
      uint32_t cb = forwardToID(d.rs2, d.usesRs2, b, old.exmem);
      s.idex.slot = false;
      s.retired += 1;
      if (compare(d.funct3, ca, cb)) {
        flush = true;
        target = old.ifid.pc + static_cast<uint32_t>(d.imm);
      }
    } else if (decision == LOAD_USE) {
      s.idex.ready = false;
      s.stallPending = true;
    }
  }

  bool fetchable = canFetch(old.pc, n);
  uint32_t nextPC = old.pc;
  if (stall) {
    s.ifid = old.ifid;
  } else {
    s.ifid.slot = false;
    s.ifid.squashed = false;
    if (flush) {
      nextPC = target;
      if (fetchable) {
        s.ifid.ir = readWord(m, old.pc);
        s.ifid.pc = old.pc;
        s.ifid.slot = true;
        s.ifid.squashed = true;
      }
    } else if (fetchable) {
      s.ifid.ir = readWord(m, old.pc);
      s.ifid.pc = old.pc;
      s.ifid.slot = true;
      nextPC = old.pc + 4;
    }
  }

  s.pc = nextPC;
  s.cycles += 1;
  s.stalls += stall ? 1 : 0;
  s.flushes += flush ? 1 : 0;
  bool empty = !(s.ifid.slot && !s.ifid.squashed) && !s.idex.slot && !s.exmem.slot && !s.memwb.slot;
  s.halted = empty && !canFetch(nextPC, n);
}

template <class M>
HD Counters runP2(M& m, uint32_t n, uint32_t limit) {
  P2 s;
  while (!s.halted && s.cycles < limit) stepP2(s, m, n);
  return Counters{s.pc, s.cycles, s.retired, s.stalls, s.flushes, s.halted ? HALTED : LIMIT};
}

}
