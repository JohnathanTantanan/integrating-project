#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <numeric>
#include <string>
#include <vector>
#include "batch.h"
#include "cpu.h"
#include "soa.h"
#if defined(WITH_HIP)
#include "gpu.h"
#endif

namespace {

using Args = std::map<std::string, std::string>;

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 2; i + 1 < argc; i += 2) a[std::string(argv[i]).substr(2)] = argv[i + 1];
  return a;
}

std::string get(const Args& a, const std::string& k, const std::string& fallback = "") {
  auto it = a.find(k);
  if (it != a.end()) return it->second;
  if (fallback.empty()) throw std::runtime_error("missing --" + k);
  return fallback;
}

template <class T>
std::vector<T> tile(const std::vector<T>& p, size_t n) {
  std::vector<T> out(n);
  for (size_t i = 0; i < n; i++) out[i] = p[i % p.size()];
  return out;
}

double ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

size_t passed(const std::vector<batch::Result>& r) {
  size_t k = 0;
  for (const auto& x : r) k += x.pass;
  return k;
}

std::vector<uint32_t> ordering(const std::string& order, const std::vector<batch::Program>& p, const std::vector<batch::Result>& e) {
  std::vector<uint32_t> perm(p.size());
  std::iota(perm.begin(), perm.end(), 0u);
  if (order == "count") {
    std::stable_sort(perm.begin(), perm.end(), [&](uint32_t x, uint32_t y) { return p[x].instructionCount < p[y].instructionCount; });
  } else if (order == "cycles") {
    std::stable_sort(perm.begin(), perm.end(), [&](uint32_t x, uint32_t y) { return e[x].cycles < e[y].cycles; });
  } else if (order != "none") {
    throw std::runtime_error("--order must be none, count or cycles");
  }
  return perm;
}

template <class T>
std::vector<T> permute(const std::vector<T>& v, const std::vector<uint32_t>& perm) {
  std::vector<T> out(perm.size());
  for (size_t i = 0; i < perm.size(); i++) out[i] = v[perm[i]];
  return out;
}

template <class T>
std::vector<T> unpermute(const std::vector<T>& v, const std::vector<uint32_t>& perm) {
  std::vector<T> out(perm.size());
  for (size_t i = 0; i < perm.size(); i++) out[perm[i]] = v[i];
  return out;
}

double efficiency(const std::vector<batch::Result>& r, uint32_t group) {
  double used = 0, spent = 0;
  for (size_t g = 0; g < r.size(); g += group) {
    uint32_t most = 0;
    size_t end = std::min(r.size(), g + group);
    for (size_t i = g; i < end; i++) {
      used += r[i].cycles;
      most = std::max(most, r[i].cycles);
    }
    spent += double(most) * group;
  }
  return spent > 0 ? used / spent : 0;
}

#if defined(WITH_HIP)
gpu::Options options(const Args& a, bool pipelined, bool full) {
  gpu::Options o;
  o.full = full;
  o.pipelined = pipelined;
  o.lds = get(a, "lds", "0") == "1";
  o.pinned = get(a, "pinned", "0") == "1";
  o.block = static_cast<uint32_t>(std::stoul(get(a, "block", "256")));
  o.streams = static_cast<uint32_t>(std::stoul(get(a, "streams", "1")));
  return o;
}
#endif

int run(const Args& a) {
  bool pipelined = get(a, "model") == "p2";
  std::string engine = get(a, "engine", "cpu");
  auto programs = batch::load<batch::Program>(get(a, "batch"), batch::BATCH_MAGIC);
  auto expected = batch::load<batch::Result>(get(a, "expected"), batch::RESULT_MAGIC);
  if (expected.size() != programs.size()) throw std::runtime_error("batch and expected sizes differ");
  auto perm = ordering(get(a, "order", "none"), programs, expected);
  programs = permute(programs, perm);
  expected = permute(expected, perm);
  std::vector<batch::Result> out;
  auto t0 = std::chrono::steady_clock::now();
  if (engine == "cpu") {
    cpu::run(programs, expected, out, pipelined);
  } else if (engine == "soa") {
    soa::runOnHost(programs, expected, out, pipelined);
  } else {
#if defined(WITH_HIP)
    std::printf("device: %s\n", gpu::device().c_str());
    gpu::Timing t{};
    gpu::run(programs, expected, out, t, options(a, pipelined, true));
    std::printf("kernel %.3f ms, layout %.3f ms, end to end %.3f ms, SIMT efficiency %.1f%% (groups of %u)\n", t.kernelMs,
                t.layoutMs, t.totalMs, 100 * efficiency(out, gpu::warpSize()), gpu::warpSize());
#else
    throw std::runtime_error("built without HIP");
#endif
  }
  auto t1 = std::chrono::steady_clock::now();
  out = unpermute(out, perm);
  batch::save(get(a, "out"), batch::RESULT_MAGIC, out);
  std::printf("%s %s: %zu programs, %zu pass, %zu fail, %.3f ms\n", get(a, "model").c_str(), engine.c_str(),
              out.size(), passed(out), out.size() - passed(out), ms(t0, t1));
  return passed(out) == out.size() ? 0 : 1;
}

int bench(const Args& a) {
  bool pipelined = get(a, "model") == "p2";
  auto programs = batch::load<batch::Program>(get(a, "batch"), batch::BATCH_MAGIC);
  auto expected = batch::load<batch::Result>(get(a, "expected"), batch::RESULT_MAGIC);
  size_t from = std::stoul(get(a, "from", "1024")), to = std::stoul(get(a, "to", "1048576"));
  int runs = std::stoi(get(a, "runs", "3"));
  std::string order = get(a, "order", "none");
  bool withCpu = get(a, "cpu", "1") == "1";
  FILE* csv = std::fopen(get(a, "csv").c_str(), "w");
  if (!csv) throw std::runtime_error("cannot write csv");
  uint32_t group = 32;
  std::string config = "order=" + order;
#if defined(WITH_HIP)
  std::printf("device: %s\n", gpu::device().c_str());
  group = gpu::warpSize();
  gpu::Options base = options(a, pipelined, true);
  config += " lds=" + std::to_string(base.lds) + " pinned=" + std::to_string(base.pinned) + " streams=" + std::to_string(base.streams)
          + " block=" + std::to_string(base.block);
#endif
  std::printf("%s %s\n", get(a, "model").c_str(), config.c_str());
  std::fprintf(csv, "model,programs,order,lds,pinned,streams,block,cpu_ms,gpu_kernel_ms,gpu_layout_ms,gpu_total_ms,gpu_flags_ms,"
                    "speedup_kernel,speedup_total,speedup_flags,simt_eff,cpu_pass,gpu_pass,gpu_flags_pass\n");
  std::printf("%10s %10s %10s %10s %10s %10s %9s %9s %9s %7s\n", "programs", "cpu ms", "kernel ms", "layout ms", "total ms", "flags ms",
              "x kernel", "x total", "x flags", "SIMT");
  for (size_t n = from; n <= to; n *= 2) {
    auto batchN = tile(programs, n);
    std::vector<batch::Result> want = expected;
    if (order != "none") {
      want = tile(expected, n);
      auto perm = ordering(order, batchN, want);
      batchN = permute(batchN, perm);
      want = permute(want, perm);
    }
    std::vector<batch::Result> out;
    double cpuMs = 0, kernelMs = 0, layoutMs = 0, totalMs = 0, flagsMs = 0, eff = 0;
    size_t cpuPass = 0, gpuPass = 0, flagsPass = 0;
    if (withCpu) {
      cpuMs = 1e300;
      for (int r = 0; r < runs; r++) {
        auto t0 = std::chrono::steady_clock::now();
        cpu::run(batchN, want, out, pipelined);
        auto t1 = std::chrono::steady_clock::now();
        cpuMs = std::min(cpuMs, ms(t0, t1));
      }
      cpuPass = passed(out);
      eff = efficiency(out, group);
      std::vector<batch::Result>().swap(out);
    }
#if defined(WITH_HIP)
    if (get(a, "gpu", "1") == "1") {
      kernelMs = layoutMs = totalMs = flagsMs = 1e300;
      for (int r = 0; r < runs + 1; r++) {
        gpu::Timing t{};
        gpu::run(batchN, want, out, t, base);
        if (r == 0) continue;
        kernelMs = std::min(kernelMs, t.kernelMs);
        layoutMs = std::min(layoutMs, t.layoutMs);
        totalMs = std::min(totalMs, t.totalMs);
      }
      gpuPass = passed(out);
      eff = efficiency(out, group);
      gpu::Options flags = base;
      flags.full = false;
      for (int r = 0; r < runs + 1; r++) {
        gpu::Timing f{};
        for (auto& x : out) x.pass = 0;
        gpu::run(batchN, want, out, f, flags);
        if (r == 0) continue;
        flagsMs = std::min(flagsMs, f.totalMs);
      }
      flagsPass = passed(out);
      std::vector<batch::Result>().swap(out);
    }
    std::string cfg = order + "," + std::to_string(base.lds) + "," + std::to_string(base.pinned) + "," + std::to_string(base.streams) + ","
                    + std::to_string(base.block);
#else
    std::string cfg = order + ",0,0,1,256";
#endif
    double sk = kernelMs > 0 && cpuMs > 0 ? cpuMs / kernelMs : 0, st = totalMs > 0 && cpuMs > 0 ? cpuMs / totalMs : 0,
           sf = flagsMs > 0 && cpuMs > 0 ? cpuMs / flagsMs : 0;
    std::fprintf(csv, "%s,%zu,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.4f,%zu,%zu,%zu\n", get(a, "model").c_str(), n, cfg.c_str(), cpuMs,
                 kernelMs, layoutMs, totalMs, flagsMs, sk, st, sf, eff, cpuPass, gpuPass, flagsPass);
    std::fflush(csv);
    std::printf("%10zu %10.3f %10.3f %10.3f %10.3f %10.3f %9.2f %9.2f %9.2f %6.1f%%  pass %zu/%zu/%zu of %zu\n", n, cpuMs, kernelMs,
                layoutMs, totalMs, flagsMs, sk, st, sf, 100 * eff, cpuPass, gpuPass, flagsPass, n);
  }
  std::fclose(csv);
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: sagev-batch run|bench --model p0|p2 --batch FILE --expected FILE [--engine cpu|soa|hip --out FILE | --csv FILE]\n"
                 "       [--order none|count|cycles] [--lds 0|1] [--pinned 0|1] [--streams K] [--block B] [--cpu 0|1] [--gpu 0|1]\n");
    return 2;
  }
  try {
    std::string cmd = argv[1];
    auto a = parse(argc, argv);
    if (cmd == "run") return run(a);
    if (cmd == "bench") return bench(a);
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 2;
  }
}
