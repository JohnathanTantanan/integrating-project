# SageV-Batch

Data-parallel, cycle-accurate simulation of a pipelined RV32I processor on an AMD GPU. This is the integrating project of Lance Xavier Lim and James Russel Padua for Advanced Computer Architecture, 1st Term SY 2026-2027, De La Salle University.

SageV is a web simulator of a five-stage RV32I pipeline. Its core runs one program at a time. SageV-Batch runs thousands of independent programs at once, one GPU thread per simulated machine, through HIP on an AMD Radeon RX 6600 XT. Every result is checked against SageV's own TypeScript core, and the timing is compared with a sequential C++ version of the same step function.

- Proposal: [SageV-Batch_Project_Proposal_3.pdf](SageV-Batch_Project_Proposal_3.pdf)
- Week 6 video pitch: [video pitch.mp4](video%20pitch.mp4)
- Week 10 milestone report: [MILESTONE-2.md](MILESTONE-2.md)

## Layout

| Path | Contents |
| --- | --- |
| `sagev/` | SageV itself, as a git submodule pinned to the commit the engine was checked against |
| `tools/export.ts` | Generates random programs with SageV's generator, assembles them with SageV's assembler, runs them on SageV's core, and writes the batch and the expected results |
| `tools/check.ts` | Compares engine results with SageV's expected results field by field, and re-runs a random sample on SageV's core |
| `tools/flagtest.ts` | Corrupts one field in each of eight expected results and checks that exactly those programs are flagged as failed |
| `engine/rv32.h`, `engine/machine.h` | Decoder, ALU, branch comparator and memory access shared by every model |
| `engine/p2.h` | The five-stage pipeline (SageV's Pipeline 2): forwarding, load-use and branch stalls, branches resolved in ID, flushes |
| `engine/p0.h` | The non-pipelined model (SageV's Pipeline 0), written once for both the CPU and the GPU |
| `engine/soa.h` | The per-thread work of the GPU: converting a program record into the structure of arrays, running it, and checking and returning the result. It also runs on the CPU (`--engine soa`) as a check of the kernel code |
| `engine/hip/gpu.hip` | The HIP side: device selection, copies and the three kernel launches |
| `engine/hip/hello.hip` | One-kernel test that the RX 6600 XT is found and selected |
| `engine/main.cpp` | Command line: `run` and `bench` |
| `results/` | Timing tables: [p0_timing.md](results/p0_timing.md) |

## Running it

Requirements: Node.js 20 or later, Visual Studio 2022 Build Tools with the C++ workload, and AMD HIP SDK 6.2.4 for Windows. The compiler in HIP 6.2 cannot read the headers of Visual Studio versions newer than 2022, so `build.ps1` selects the 2022 tools even when a newer Visual Studio is installed.

```
git clone --recurse-submodules https://github.com/JohnathanTantanan/integrating-project.git
cd integrating-project
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

The export is deterministic: the same `--seed` (default 20261109) gives the same programs.
