#pragma once
#include <string>
#include <vector>
#include "batch.h"

namespace gpu {

struct Timing {
  double kernelMs, layoutMs, totalMs;
};

struct Options {
  bool full = true;
  bool pipelined = false;
  bool lds = false;
  bool pinned = false;
  uint32_t block = 256;
  uint32_t streams = 1;
};

std::string device();
uint32_t warpSize();
void run(const std::vector<batch::Program>& programs, const std::vector<batch::Result>& expected,
         std::vector<batch::Result>& out, Timing& timing, const Options& options);

}
