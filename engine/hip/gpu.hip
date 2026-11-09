#include <hip/hip_runtime.h>
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

__global__ void p0Kernel(soa::Args a) {
  uint32_t t = blockIdx.x * blockDim.x + threadIdx.x;
  if (t < a.n) soa::p0Thread(a, t);
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

template <class T>
T* alloc(size_t n, std::vector<void*>& owned) {
  T* d = nullptr;
  CHECK(hipMalloc(&d, n * sizeof(T)));
  owned.push_back(d);
  return d;
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

void run(const std::vector<batch::Program>& programs, const std::vector<batch::Result>& expected,
         std::vector<batch::Result>& out, Timing& timing, bool full) {
  select();
  uint32_t n = static_cast<uint32_t>(programs.size());
  uint32_t u = static_cast<uint32_t>(expected.size() < programs.size() ? expected.size() : programs.size());
  out.resize(n);
  std::vector<uint32_t> pass(full ? 0 : n);
  auto t0 = std::chrono::steady_clock::now();
  std::vector<void*> owned;
  auto* in = alloc<batch::Program>(n, owned);
  auto* want = alloc<batch::Result>(u, owned);
  soa::Args a{n, u, alloc<uint32_t>(32ull * n, owned), alloc<uint32_t>(64ull * n, owned), alloc<uint32_t>(n, owned),
              alloc<uint32_t>(n, owned), alloc<uint32_t>(n, owned), alloc<uint32_t>(n, owned), alloc<uint32_t>(n, owned),
              alloc<uint32_t>(n, owned), alloc<uint32_t>(n, owned), in, want, full ? alloc<batch::Result>(n, owned) : nullptr};
  CHECK(hipMemcpy(in, programs.data(), n * sizeof(batch::Program), hipMemcpyHostToDevice));
  CHECK(hipMemcpy(want, expected.data(), u * sizeof(batch::Result), hipMemcpyHostToDevice));

  hipEvent_t e[4];
  for (auto& x : e) CHECK(hipEventCreate(&x));
  const uint32_t block = 256;
  dim3 grid((n + block - 1) / block);
  CHECK(hipEventRecord(e[0]));
  hipLaunchKernelGGL(loadKernel, grid, dim3(block), 0, 0, a);
  CHECK(hipEventRecord(e[1]));
  hipLaunchKernelGGL(p0Kernel, grid, dim3(block), 0, 0, a);
  CHECK(hipEventRecord(e[2]));
  hipLaunchKernelGGL(storeKernel, grid, dim3(block), 0, 0, a);
  CHECK(hipEventRecord(e[3]));
  CHECK(hipGetLastError());
  CHECK(hipEventSynchronize(e[3]));
  if (full) CHECK(hipMemcpy(out.data(), a.out, n * sizeof(batch::Result), hipMemcpyDeviceToHost));
  else CHECK(hipMemcpy(pass.data(), a.pass, n * sizeof(uint32_t), hipMemcpyDeviceToHost));
  for (void* d : owned) CHECK(hipFree(d));
  auto t1 = std::chrono::steady_clock::now();
  if (!full) for (uint32_t t = 0; t < n; t++) out[t].pass = pass[t];

  timing.kernelMs = between(e[1], e[2]);
  timing.layoutMs = between(e[0], e[1]) + between(e[2], e[3]);
  timing.totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  for (auto& x : e) CHECK(hipEventDestroy(x));
}

}
