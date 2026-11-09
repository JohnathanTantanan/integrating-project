#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstring>
#include <vector>

__global__ void addOne(int* x, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) x[i] += 1;
}

int main() {
  int count = 0;
  if (hipGetDeviceCount(&count) != hipSuccess || count == 0) {
    std::printf("no HIP devices\n");
    return 1;
  }
  int chosen = -1;
  for (int i = 0; i < count; i++) {
    hipDeviceProp_t p;
    hipGetDeviceProperties(&p, i);
    std::printf("device %d: %s, %s, %zu MB, %d compute units, %d-thread groups\n", i, p.name, p.gcnArchName,
                p.totalGlobalMem >> 20, p.multiProcessorCount, p.warpSize);
    if (chosen < 0 && std::strncmp(p.gcnArchName, "gfx1032", 7) == 0) chosen = i;
  }
  if (chosen < 0) {
    std::printf("no gfx1032 device\n");
    return 1;
  }
  hipSetDevice(chosen);
  const int n = 1 << 20;
  std::vector<int> h(n);
  for (int i = 0; i < n; i++) h[i] = i;
  int* d = nullptr;
  hipMalloc(&d, n * sizeof(int));
  hipMemcpy(d, h.data(), n * sizeof(int), hipMemcpyHostToDevice);
  hipLaunchKernelGGL(addOne, dim3((n + 255) / 256), dim3(256), 0, 0, d, n);
  hipMemcpy(h.data(), d, n * sizeof(int), hipMemcpyDeviceToHost);
  hipFree(d);
  for (int i = 0; i < n; i++) {
    if (h[i] != i + 1) {
      std::printf("wrong result at %d\n", i);
      return 1;
    }
  }
  std::printf("selected device %d; kernel ran on %d values and every result is right\n", chosen, n);
  return 0;
}
