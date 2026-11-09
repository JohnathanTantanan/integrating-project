#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace batch {

constexpr uint32_t BATCH_MAGIC = 0x31425653, RESULT_MAGIC = 0x31525653;

#pragma pack(push, 1)
struct Program {
  uint8_t mem[256];
  uint32_t regs[32];
  uint32_t instructionCount;
  uint32_t cycleLimit;
};

struct Result {
  uint32_t regs[32];
  uint8_t mem[256];
  uint32_t pc, cycles, retired, stalls, flushes, status, pass;
};

struct Header {
  uint32_t magic, version, count, record;
};
#pragma pack(pop)

template <class T>
std::vector<T> load(const std::string& path, uint32_t magic) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) throw std::runtime_error("cannot open " + path);
  Header h{};
  if (std::fread(&h, sizeof h, 1, f) != 1 || h.magic != magic || h.record != sizeof(T)) {
    std::fclose(f);
    throw std::runtime_error("bad file " + path);
  }
  std::vector<T> items(h.count);
  size_t got = std::fread(items.data(), sizeof(T), h.count, f);
  std::fclose(f);
  if (got != h.count) throw std::runtime_error("truncated " + path);
  return items;
}

template <class T>
void save(const std::string& path, uint32_t magic, const std::vector<T>& items) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) throw std::runtime_error("cannot write " + path);
  Header h{magic, 1, static_cast<uint32_t>(items.size()), sizeof(T)};
  std::fwrite(&h, sizeof h, 1, f);
  std::fwrite(items.data(), sizeof(T), items.size(), f);
  std::fclose(f);
}

inline bool same(const Result& a, const Result& b) {
  return std::memcmp(a.regs, b.regs, sizeof a.regs) == 0 && std::memcmp(a.mem, b.mem, sizeof a.mem) == 0
    && a.pc == b.pc && a.cycles == b.cycles && a.retired == b.retired && a.stalls == b.stalls
    && a.flushes == b.flushes && a.status == b.status;
}

}
