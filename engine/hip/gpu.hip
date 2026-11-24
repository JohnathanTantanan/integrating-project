#include <hip/hip_runtime.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "../gpu.h"
#include "../soa.h"

#define CHECK(x) do { hipError_t e_ = (x); if (e_ != hipSuccess) throw std::runtime_error(std::string(#x) + ": " + hipGetErrorString(e_)); } while (0)

namespace {

__global__ void loadKernel(soa::Args a) {
  uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
  if (t < a.n) soa::loadThread(a, t);
}

template <bool P2>
__global__ void runKernel(soa::Args a) {
  uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
  if (t < a.n) soa::runThread(a, t, soa::global(a, t), P2);
}

template <bool P2>
__global__ void ldsKernel(soa::Args a) {
  extern __shared__ uint32_t lds[];
  uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
  uint32_t b = blockDim.x, i = threadIdx.x;
  if (t >= a.n) return;
  for (uint32_t w = 0; w < 64; w++) lds[w * b + i] = a.mem[w * a.n + t];
  soa::runThread(a, t, soa::View{a.regs, lds, a.n, t, b, i}, P2);
  for (uint32_t w = 0; w < 64; w++) a.mem[w * a.n + t] = lds[w * b + i];
}

__global__ void storeKernel(soa::Args a) {
  uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
  if (t < a.n) soa::storeThread(a, t);
}

int selected = -1;

void select() {
  if (selected >= 0) return;
  int count = 0;
  CHECK(hipGetDeviceCount(&count));
  for (int i = 0; i < count; i++) {
    hipDeviceProp_t p;
    CHECK(hipGetDeviceProperties(&p, i));
    if (std::strncmp(p.gcnArchName, "gfx1032", 7) == 0) { selected = i; break; }
  }
  if (selected < 0) throw std::runtime_error("no gfx1032 device (RX 6600 XT) found");
  CHECK(hipSetDevice(selected));
}

struct Buffer {
  void* p = nullptr;
  size_t bytes = 0;
};

std::vector<hipStream_t> streamPool;
std::vector<hipEvent_t> eventPool;
std::vector<Buffer> pinnedPool, devicePool;

template <class T>
T* alloc(size_t i, size_t n, bool dry, bool& short_) {
  size_t bytes = (n > 0 ? n : 1) * sizeof(T);
  if (dry) {
    if (devicePool.size() <= i || devicePool[i].bytes < bytes) short_ = true;
    return nullptr;
  }
  if (devicePool.size() <= i) devicePool.resize(i + 1);
  Buffer& b = devicePool[i];
  if (b.bytes < bytes) {
    if (b.p) CHECK(hipFree(b.p));
    CHECK(hipMalloc(&b.p, bytes));
    b.bytes = bytes;
  }
  return static_cast<T*>(b.p);
}

void releaseDevice() {
  for (auto& b : devicePool) if (b.p) CHECK(hipFree(b.p));
  devicePool.clear();
}

hipStream_t stream(size_t i) {
  while (streamPool.size() <= i) {
    hipStream_t s;
    CHECK(hipStreamCreateWithFlags(&s, hipStreamNonBlocking));
    streamPool.push_back(s);
  }
  return streamPool[i];
}

hipEvent_t event(size_t i) {
  while (eventPool.size() <= i) {
    hipEvent_t e;
    CHECK(hipEventCreate(&e));
    eventPool.push_back(e);
  }
  return eventPool[i];
}

template <class T>
T* pinned(size_t i, size_t n) {
  if (pinnedPool.size() <= i) pinnedPool.resize(i + 1);
  Buffer& b = pinnedPool[i];
  size_t bytes = (n > 0 ? n : 1) * sizeof(T);
  if (b.bytes < bytes) {
    if (b.p) CHECK(hipHostFree(b.p));
    CHECK(hipHostMalloc(&b.p, bytes, hipHostMallocDefault));
    b.bytes = bytes;
  }
  return static_cast<T*>(b.p);
}

float between(hipEvent_t a, hipEvent_t b) {
  float ms = 0;
  CHECK(hipEventElapsedTime(&ms, a, b));
  return ms;
}

}

namespace gpu {

std::string device() {
  select();
  hipDeviceProp_t p;
  CHECK(hipGetDeviceProperties(&p, selected));
  return std::string(p.name) + " (" + p.gcnArchName + ")";
}

uint32_t warpSize() {
  select();
  hipDeviceProp_t p;
  CHECK(hipGetDeviceProperties(&p, selected));
  return static_cast<uint32_t>(p.warpSize);
}

void run(const std::vector<batch::Program>& programs, const std::vector<batch::Result>& expected,
         std::vector<batch::Result>& out, Timing& timing, const Options& o) {
  select();
  uint32_t n = static_cast<uint32_t>(programs.size());
  uint32_t u = static_cast<uint32_t>(std::min(expected.size(), programs.size()));
  uint32_t k = std::max(1u, std::min(o.streams, n));
  uint32_t chunk = (n + k - 1) / k;
  uint32_t block = o.block;
  out.resize(n);

  auto t0 = std::chrono::steady_clock::now();
  std::vector<batch::Program*> hin(k, nullptr);
  std::vector<batch::Result*> hres(k, nullptr);
  std::vector<uint32_t*> hpass(k, nullptr);
  std::vector<uint32_t> pass(o.full ? 0 : n);
  if (o.pinned) {
    for (uint32_t c = 0; c < k && c * chunk < n; c++) {
      uint32_t off = c * chunk, nc = std::min(chunk, n - off);
      hin[c] = pinned<batch::Program>(3 * c, nc);
      std::memcpy(hin[c], programs.data() + off, size_t(nc) * sizeof(batch::Program));
      if (o.full) hres[c] = pinned<batch::Result>(3 * c + 1, nc);
      else hpass[c] = pinned<uint32_t>(3 * c + 2, nc);
    }
  }

  std::vector<soa::Args> args;
  auto plan = [&](bool dry) {
    bool short_ = false;
    args.clear();
    auto* want = alloc<batch::Result>(0, u, dry, short_);
    for (uint32_t c = 0; c < k && c * chunk < n; c++) {
      uint32_t off = c * chunk, nc = std::min(chunk, n - off);
      size_t i = 1 + 13ull * c;
      uint32_t* f[9];
      for (auto& p : f) p = alloc<uint32_t>(i++, nc, dry, short_);
      auto* regs = alloc<uint32_t>(i++, 32ull * nc, dry, short_);
      auto* mem = alloc<uint32_t>(i++, 64ull * nc, dry, short_);
      auto* in = alloc<batch::Program>(i++, nc, dry, short_);
      auto* res = o.full ? alloc<batch::Result>(i, nc, dry, short_) : nullptr;
      args.push_back(soa::Args{nc, u, off, regs, mem, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], in, want, res});
    }
    return short_;
  };
  if (plan(true)) releaseDevice();
  plan(false);
  CHECK(hipMemcpy(const_cast<batch::Result*>(args[0].want), expected.data(), size_t(u) * sizeof(batch::Result), hipMemcpyHostToDevice));
  k = static_cast<uint32_t>(args.size());

  std::vector<hipStream_t> streams(k);
  for (uint32_t c = 0; c < k; c++) streams[c] = stream(c);
  std::vector<hipEvent_t> ev(4ull * k);
  for (size_t i = 0; i < ev.size(); i++) ev[i] = event(i);

  size_t shared = o.lds ? 64ull * 4 * block : 0;
  for (uint32_t c = 0; c < k; c++) {
    const soa::Args& a = args[c];
    uint32_t off = a.base, nc = a.n;
    hipStream_t s = streams[c];
    batch::Program* in = const_cast<batch::Program*>(a.in);
    dim3 grid((nc + block - 1) / block);
    if (o.pinned) {
      CHECK(hipMemcpyAsync(in, hin[c], size_t(nc) * sizeof(batch::Program), hipMemcpyHostToDevice, s));
      CHECK(hipStreamSynchronize(s));
    } else CHECK(hipMemcpy(in, programs.data() + off, size_t(nc) * sizeof(batch::Program), hipMemcpyHostToDevice));
    CHECK(hipEventRecord(ev[4 * c], s));
    hipLaunchKernelGGL(loadKernel, grid, dim3(block), 0, s, a);
    CHECK(hipEventRecord(ev[4 * c + 1], s));
    if (o.lds) {
      if (o.pipelined) hipLaunchKernelGGL(ldsKernel<true>, grid, dim3(block), shared, s, a);
      else hipLaunchKernelGGL(ldsKernel<false>, grid, dim3(block), shared, s, a);
    } else {
      if (o.pipelined) hipLaunchKernelGGL(runKernel<true>, grid, dim3(block), 0, s, a);
      else hipLaunchKernelGGL(runKernel<false>, grid, dim3(block), 0, s, a);
    }
    CHECK(hipEventRecord(ev[4 * c + 2], s));
    hipLaunchKernelGGL(storeKernel, grid, dim3(block), 0, s, a);
    CHECK(hipEventRecord(ev[4 * c + 3], s));
    CHECK(hipGetLastError());
    if (!o.pinned) continue;
    if (o.full) CHECK(hipMemcpyAsync(hres[c], a.out, size_t(nc) * sizeof(batch::Result), hipMemcpyDeviceToHost, s));
    else CHECK(hipMemcpyAsync(hpass[c], a.pass, size_t(nc) * sizeof(uint32_t), hipMemcpyDeviceToHost, s));
  }
  for (auto& s : streams) CHECK(hipStreamSynchronize(s));
  for (uint32_t c = 0; c < k; c++) {
    const soa::Args& a = args[c];
    if (o.pinned && o.full) std::memcpy(out.data() + a.base, hres[c], size_t(a.n) * sizeof(batch::Result));
    else if (o.pinned) std::memcpy(pass.data() + a.base, hpass[c], size_t(a.n) * sizeof(uint32_t));
    else if (o.full) CHECK(hipMemcpy(out.data() + a.base, a.out, size_t(a.n) * sizeof(batch::Result), hipMemcpyDeviceToHost));
    else CHECK(hipMemcpy(pass.data() + a.base, a.pass, size_t(a.n) * sizeof(uint32_t), hipMemcpyDeviceToHost));
  }
  if (!o.full) for (uint32_t t = 0; t < n; t++) out[t].pass = pass[t];
  auto t1 = std::chrono::steady_clock::now();

  double kernel = 0, layout = 0;
  for (uint32_t c = 0; c < k; c++) {
    kernel += between(ev[4 * c + 1], ev[4 * c + 2]);
    layout += between(ev[4 * c], ev[4 * c + 1]) + between(ev[4 * c + 2], ev[4 * c + 3]);
  }
  timing.kernelMs = kernel;
  timing.layoutMs = layout;
  timing.totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

}
