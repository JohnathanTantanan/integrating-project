#pragma once
#include <cstdint>

#if defined(__HIPCC__)
#define HD __host__ __device__ inline
#else
#define HD inline
#endif

namespace rv {

constexpr uint32_t TEXT_BASE = 0x80;
constexpr uint32_t MAX_INSTRUCTIONS = 32;
constexpr uint32_t OP_R = 0x33, OP_I = 0x13, OP_LOAD = 0x03, OP_STORE = 0x23, OP_BRANCH = 0x63;

struct Decoded {
  uint32_t opcode, rd, rs1, rs2, funct3, funct7;
  int32_t imm;
  bool valid, isR, usesRs1, usesRs2, writesRd, isLoad, isStore, isBranch;
};

HD int32_t sext(uint32_t v, int bits) {
  return static_cast<int32_t>(v << (32 - bits)) >> (32 - bits);
}

HD Decoded decode(uint32_t w) {
  Decoded d;
  d.opcode = w & 0x7f;
  d.rd = (w >> 7) & 0x1f;
  d.funct3 = (w >> 12) & 7;
  d.rs1 = (w >> 15) & 0x1f;
  d.rs2 = (w >> 20) & 0x1f;
  d.funct7 = (w >> 25) & 0x7f;
  d.isLoad = d.opcode == OP_LOAD;
  d.isStore = d.opcode == OP_STORE;
  d.isBranch = d.opcode == OP_BRANCH;
  bool r = d.opcode == OP_R, i = d.opcode == OP_I;
  int32_t immI = sext((w >> 20) & 0xfff, 12);
  int32_t immS = sext((((w >> 25) & 0x7f) << 5) | ((w >> 7) & 0x1f), 12);
  int32_t immB = sext((((w >> 31) & 1) << 12) | (((w >> 7) & 1) << 11) | (((w >> 25) & 0x3f) << 5) | (((w >> 8) & 0xf) << 1), 13);
  d.imm = (i || d.isLoad) ? ((i && d.funct3 == 1) ? static_cast<int32_t>((w >> 20) & 0x1f) : immI)
        : d.isStore ? immS : d.isBranch ? immB : 0;
  uint32_t f3 = d.funct3, f7 = d.funct7;
  bool rv = r && ((f3 == 0 && (f7 == 0 || f7 == 0x20)) || (f7 == 0 && (f3 == 1 || f3 == 2 || f3 == 6 || f3 == 7)));
  bool iv = i && (f3 == 0 || f3 == 1 || f3 == 6);
  bool mv = (d.isLoad || d.isStore) && f3 == 2;
  bool bv = d.isBranch && (f3 == 0 || f3 == 1 || f3 == 4 || f3 == 5);
  d.valid = rv || iv || mv || bv;
  d.isR = rv;
  d.usesRs1 = d.valid;
  d.usesRs2 = d.valid && (r || d.isStore || d.isBranch);
  d.writesRd = d.valid && (r || i || d.isLoad) && d.rd != 0;
  return d;
}

HD uint32_t alu(const Decoded& d, uint32_t a, uint32_t b) {
  uint32_t add = a + b, sub = a - b, sll = a << (b & 0x1f), slt = static_cast<int32_t>(a) < static_cast<int32_t>(b) ? 1u : 0u;
  uint32_t orv = a | b, andv = a & b;
  uint32_t f3 = d.funct3;
  uint32_t rres = (d.funct7 == 0x20 && f3 == 0) ? sub
    : f3 == 0 ? add : f3 == 1 ? sll : f3 == 2 ? slt : f3 == 6 ? orv : f3 == 7 ? andv : 0u;
  uint32_t ires = f3 == 0 ? add : f3 == 1 ? sll : f3 == 6 ? orv : 0u;
  return d.opcode == OP_R ? rres : d.opcode == OP_I ? ires : (d.isLoad || d.isStore) ? add : 0u;
}

HD bool compare(uint32_t funct3, uint32_t a, uint32_t b) {
  bool eq = a == b, lt = static_cast<int32_t>(a) < static_cast<int32_t>(b);
  return funct3 == 0 ? eq : funct3 == 1 ? !eq : funct3 == 4 ? lt : funct3 == 5 ? !lt : false;
}

HD uint32_t textEnd(uint32_t n) {
  return TEXT_BASE + (n < MAX_INSTRUCTIONS ? n : MAX_INSTRUCTIONS) * 4;
}

HD bool canFetch(uint32_t pc, uint32_t n) {
  return pc >= TEXT_BASE && pc < 0x100 && pc < textEnd(n);
}

}
