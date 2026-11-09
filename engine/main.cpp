#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "batch.h"
#include "cpu.h"
#include "soa.h"
#if defined(WITH_HIP)
#include "gpu.h"
#endif

namespace {

std::map<std::string, std::string> parse(int argc, char** argv) {
  std::map<std::string, std::string> a;
  for (int i = 2; i + 1 < argc; i += 2) a[std::string(argv[i]).substr(2)] = argv[i + 1];
  return a;
}

std::string get(const std::map<std::string, std::string>& a, const std::string& k, const std::string& fallback = "") {
  auto it = a.find(k);
  if (it != a.end()) return it->second;
  if (fallback.empty()) throw std::runtime_error("missing --" + k);
  return fallback;
}

std::vector<batch::Program> tile(const std::vector<batch::Program>& p, size_t n) {
  std::vector<batch::Program> out(n);
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

int run(const std::map<std::string, std::string>& a) {
  bool pipelined = get(a, "model") == "p2";
  std::string engine = get(a, "engine", "cpu");
  auto programs = batch::load<batch::Program>(get(a, "batch"), batch::BATCH_MAGIC);
  auto expected = batch::load<batch::Result>(get(a, "expected"), batch::RESULT_MAGIC);
  if (expected.size() != programs.size()) throw std::runtime_error("batch and expected sizes differ");
  std::vector<batch::Result> out;
  auto t0 = std::chrono::steady_clock::now();
  if (engine == "cpu") {
    cpu::run(programs, expected, out, pipelined);
  } else if (engine == "soa") {
    if (pipelined) throw std::runtime_error("the structure-of-arrays path runs the non-pipelined model (p0) at this milestone");
    soa::runOnHost(programs, expected, out);
  } else {
#if defined(WITH_HIP)
    if (pipelined) throw std::runtime_error("the HIP engine runs the non-pipelined model (p0) at this milestone");
    std::printf("device: %s\n", gpu::device().c_str());
    gpu::Timing t{};
    gpu::run(programs, expected, out, t);
    std::printf("kernel %.3f ms, layout %.3f ms, end to end %.3f ms\n", t.kernelMs, t.layoutMs, t.totalMs);
#else
    throw std::runtime_error("built without HIP");
#endif
  }
  auto t1 = std::chrono::steady_clock::now();
  batch::save(get(a, "out"), batch::RESULT_MAGIC, out);
  std::printf("%s %s: %zu programs, %zu pass, %zu fail, %.3f ms\n", get(a, "model").c_str(), engine.c_str(),
              out.size(), passed(out), out.size() - passed(out), ms(t0, t1));
  return passed(out) == out.size() ? 0 : 1;
}

int bench(const std::map<std::string, std::string>& a) {
  bool pipelined = get(a, "model") == "p2";
  auto programs = batch::load<batch::Program>(get(a, "batch"), batch::BATCH_MAGIC);
  auto expected = batch::load<batch::Result>(get(a, "expected"), batch::RESULT_MAGIC);
  size_t from = std::stoul(get(a, "from", "1024")), to = std::stoul(get(a, "to", "1048576"));
  int runs = std::stoi(get(a, "runs", "3"));
  FILE* csv = std::fopen(get(a, "csv").c_str(), "w");
  if (!csv) throw std::runtime_error("cannot write csv");
#if defined(WITH_HIP)
  std::printf("device: %s\n", gpu::device().c_str());
#endif
  std::fprintf(csv, "model,programs,cpu_ms,gpu_kernel_ms,gpu_layout_ms,gpu_total_ms,gpu_flags_ms,speedup_kernel,speedup_total,speedup_flags,cpu_pass,gpu_pass,gpu_flags_pass\n");
  std::printf("%10s %10s %10s %10s %10s %10s %9s %9s %9s\n", "programs", "cpu ms", "kernel ms", "layout ms", "total ms", "flags ms", "x kernel", "x total", "x flags");
  for (size_t n = from; n <= to; n *= 2) {
    auto batchN = tile(programs, n);
    std::vector<batch::Result> outCpu, outGpu, outFlags;
    double cpuMs = 1e300, kernelMs = 0, layoutMs = 0, totalMs = 0, flagsMs = 0;
    for (int r = 0; r < runs; r++) {
      auto t0 = std::chrono::steady_clock::now();
      cpu::run(batchN, expected, outCpu, pipelined);
      auto t1 = std::chrono::steady_clock::now();
      cpuMs = std::min(cpuMs, ms(t0, t1));
    }
#if defined(WITH_HIP)
    if (get(a, "gpu", "1") == "1" && !pipelined) {
      kernelMs = layoutMs = totalMs = flagsMs = 1e300;
      for (int r = 0; r < runs + 1; r++) {
        gpu::Timing t{}, f{};
        gpu::run(batchN, expected, outGpu, t, true);
        gpu::run(batchN, expected, outFlags, f, false);
        if (r == 0) continue;
        kernelMs = std::min(kernelMs, t.kernelMs);
        layoutMs = std::min(layoutMs, t.layoutMs);
        totalMs = std::min(totalMs, t.totalMs);
        flagsMs = std::min(flagsMs, f.totalMs);
      }
    }
#endif
    double sk = kernelMs > 0 ? cpuMs / kernelMs : 0, st = totalMs > 0 ? cpuMs / totalMs : 0, sf = flagsMs > 0 ? cpuMs / flagsMs : 0;
    std::fprintf(csv, "%s,%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%zu,%zu,%zu\n", get(a, "model").c_str(), n, cpuMs, kernelMs, layoutMs,
                 totalMs, flagsMs, sk, st, sf, passed(outCpu), passed(outGpu), passed(outFlags));
    std::fflush(csv);
    std::printf("%10zu %10.3f %10.3f %10.3f %10.3f %10.3f %9.2f %9.2f %9.2f  pass %zu/%zu/%zu of %zu\n", n, cpuMs, kernelMs, layoutMs,
                totalMs, flagsMs, sk, st, sf, passed(outCpu), passed(outGpu), passed(outFlags), n);
  }
  std::fclose(csv);
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: sagev-batch run|bench --model p0|p2 --batch FILE --expected FILE [--engine cpu|hip --out FILE | --csv FILE]\n");
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
