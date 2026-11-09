#pragma once
#include <string>
#include <vector>
#include "batch.h"

namespace gpu {

struct Timing {
  double kernelMs, layoutMs, totalMs;
};

std::string device();
void run(const std::vector<batch::Program>& programs, const std::vector<batch::Result>& expected,
         std::vector<batch::Result>& out, Timing& timing, bool full = true);

}
