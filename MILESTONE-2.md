# SageV-Batch: Week 10 mid-project milestone

Lance Xavier Lim and James Russel Padua. Advanced Computer Architecture, 1st Term SY 2026-2027.

## What the mid-project milestone promised

The proposal PDF in this repository does not have the Mid-project milestones section that the course outline asks for; it is being added to the proposal. The milestone was stated in the Week 6 video pitch:

> For the mid-project demo in Week 10, we will show a sequential C++ batch engine that matches SageV exactly. We will also show a HIP kernel for a simpler, non-pipelined RV32I model that matches the C++ engine, with our first timings.

The split was chosen so that errors in porting SageV to C++ show up apart from errors in moving the work to the GPU.

## Cross-check: planned against accomplished

| Planned for Week 10 | Status | Evidence |
| --- | --- | --- |
| A sequential C++ batch engine that matches SageV exactly | Done | `engine/p2.h` reproduces SageV's five-stage pipeline (its Pipeline 2). We ran 65,536 random programs: 1,539,566 simulated cycles, 126,568 stall cycles and 87,887 flushes. For every program, these match SageV's TypeScript core with no mismatch: every register, all 256 bytes of memory, the PC, the cycle count, instructions retired, stall cycles, flushes and the halt status. |
| A HIP kernel for a simpler, non-pipelined RV32I model that matches the C++ engine | Done | `engine/p0.h` is SageV's Pipeline 0, written once and compiled for both the CPU and the GPU. On the RX 6600 XT, the kernel's results for the 65,536 programs are byte for byte identical to the C++ engine's, and both match SageV. At every batch size of the timing sweep, up to 4,194,304 programs, every GPU result passed. |
| First timings | Done | [results/p0_timing.md](results/p0_timing.md): sequential C++ against HIP from 1,024 to 4,194,304 programs, with kernel time and end-to-end time reported separately. |

### First timings

The timings are for the non-pipelined model: the RX 6600 XT against one thread of the Ryzen 5 5600G, with both versions built by the same compiler. These are selected rows; the full table is in the results folder.

| Programs | C++ (ms) | GPU kernel (ms) | GPU end to end, full results (ms) | GPU end to end, flags only (ms) | Kernel speedup | End-to-end speedup, full | End-to-end speedup, flags |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 0.330 | 0.071 | 0.909 | 0.672 | 4.65 | 0.36 | 0.49 |
| 16,384 | 5.279 | 0.109 | 8.481 | 5.880 | 48.48 | 0.62 | 0.90 |
| 262,144 | 85.386 | 2.256 | 81.240 | 48.496 | 37.84 | 1.05 | 1.76 |
| 1,048,576 | 341.592 | 9.978 | 321.154 | 181.679 | 34.23 | 1.06 | 1.88 |
| 4,194,304 | 1,363.012 | 52.004 | 1,258.493 | 687.522 | 26.21 | 1.08 | 1.98 |

The simulation kernel is 26 to 48 times faster than one CPU thread from 8,192 programs up. End to end the gain is small, because this model does little work for the data it moves: an average program runs about 82 cycles (16.5 instructions), yet carries 392 bytes to the GPU and 412 bytes back. Returning only the pass/fail flags, which is all that verification needs, makes the GPU 1.9 to 2.0 times as fast as the CPU from 524,288 programs up. The pipelined model does more work for the same data, and is measured in Week 13.

### Changes from the proposal

- The proposal and pitch said that the host would reorder the batch into a structure of arrays. With the reordering on the host, our first version took 281 ms end to end for 65,536 programs, against 21 ms for the CPU engine. The reordering now happens on the GPU, and together with the change below this brought that time down to 28 ms: the host copies each program's record as it is, and a GPU kernel spreads it into the structure of arrays.
- Each machine's memory is stored in the structure of arrays as 64 words rather than 256 bytes, so each thread moves a quarter as many values.

### What else is in place

- **Integration with SageV.** SageV is a git submodule of this repository, pinned to the commit the engine was checked against. The batch is produced by SageV's own random program generator, assembler and core (`tools/export.ts`), so the expected results come from the simulator students use, not from a second model written for this project.
- **Checking.** `tools/check.ts` compares every field of every result with SageV's expected result, then re-runs a random sample of 500 programs through SageV's core again.
- **The check finds faults.** We planted 22 faults in the C++ engine, one at a time, and every one made the check fail. They included:
  - wrong forwarding priority;
  - a missing load-use stall;
  - a branch that waits too little;
  - no re-read during a stall;
  - the wrong flush marking;
  - a stall counter that misses half the stalls;
  - a memory read that does not wrap;
  - an unsigned SLT.

  We then planted 11 faults in the GPU-only code (the data layout, the conversion kernels, the comparison and the copies back); every one was also caught.
- **Pass/fail on the GPU.** Each thread compares its final state with the expected state, as the proposal describes. `tools/flagtest.ts` corrupts one field each in eight expected results: a register, the first and the last byte of memory, the PC and the counters. The GPU flags exactly those eight programs out of 65,536.
- **Memory contents.** In a quarter of the programs, every memory byte not used by the program or its data starts with a random value, so every byte of the 256 is checked with something in it.
- **Cycle limits.** About 3% of the programs are given a cycle limit shorter than their run, so the engines are also checked on machines stopped in the middle of an instruction.
- **Device selection.** The host code picks the device whose architecture is gfx1032 (the RX 6600 XT), never the default device. The Ryzen 5 5600G has an integrated GPU, so the default could be the wrong one.
- **HIP version.** HIP SDK 6.2.4 runs on the RX 6600 XT as planned. The build has to use the Visual Studio 2022 C++ tools, because the compiler in HIP 6.2 cannot read the headers of newer Visual Studio versions; `build.ps1` selects them.

## Tasks yet to be completed

For the Week 13 submission:

1. Write the predicated five-stage pipeline kernel. Its pipeline registers will be stored as a structure of arrays, and it must match SageV exactly, as the C++ engine does.
2. Measure the cost of threads that finish early, and test grouping programs of similar length into the same thread group.
3. Compare keeping each machine's memory in the local data share (LDS) with keeping it in global memory.
4. Reduce the cost of moving data:
   - lay out the program records so that neighbouring threads read neighbouring words;
   - use pinned host memory;
   - overlap the copies with the simulation.
5. Run the timing sweep for both models, from 1,024 programs, doubling until GPU memory runs out.
6. Write the discussion of the parallel algorithm and the timing comparison for this repository.
7. Confirm with the instructor that HIP on an AMD GPU is accepted for the "GPU/CUDA (SIMT)" option.
8. Optionally, write the conference paper.

## Video pitch

The 5 to 8 minute milestone video: (link to be added).

## Reproducing the results

See [README.md](README.md). In short:

```
git clone --recurse-submodules https://github.com/JohnathanTantanan/integrating-project.git
npm install
npx tsx tools/export.ts --count 65536 --out data
powershell -File build.ps1
build\hello.exe
build\sagev-batch.exe run --model p2 --engine cpu --batch data\batch.bin --expected data\expected_p2.bin --out data\result_p2_cpu.bin
build\sagev-batch.exe run --model p0 --engine hip --batch data\batch.bin --expected data\expected_p0.bin --out data\result_p0_hip.bin
npx tsx tools/check.ts --model p0 --batch data\batch.bin --expected data\expected_p0.bin --results data\result_p0_hip.bin
npx tsx tools/flagtest.ts
build\sagev-batch.exe bench --model p0 --batch data\batch.bin --expected data\expected_p0.bin --from 1024 --to 4194304 --csv results\p0_timing.csv
```
