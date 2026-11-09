# Non-pipelined model: sequential C++ against HIP

AMD Radeon RX 6600 XT (gfx1032) against one thread of the Ryzen 5 5600G. Both versions are built by the HIP SDK 6.2.4 compiler with -O2, from the same step function (`engine/p0.h`). Each time is the best of 3 runs, after one warm-up run of the GPU. The 65,536 programs exported from SageV are repeated to fill the larger batches. Every GPU result passed its check against SageV's expected result at every batch size. The raw numbers are in [p0_timing.csv](p0_timing.csv).

- **Kernel** is the simulation kernel alone, measured with HIP events.
- **Layout** is the two GPU kernels that convert each program's record into the structure-of-arrays layout and back.
- **End to end** is measured with the host clock, from allocating GPU memory to having the results in host memory. It includes copying the programs and expected results to the GPU, all three kernels, copying back and freeing.
- **Full results** copies back every register, every memory byte and the counters.
- **Flags only** copies back just the pass/fail flags, which is what verification needs.

| Programs | C++ (ms) | Kernel (ms) | Layout (ms) | End to end, full results (ms) | End to end, flags only (ms) | Kernel speedup | End-to-end speedup, full | End-to-end speedup, flags |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 0.330 | 0.071 | 0.103 | 0.909 | 0.672 | 4.65 | 0.36 | 0.49 |
| 2,048 | 0.673 | 0.064 | 0.124 | 1.155 | 0.822 | 10.49 | 0.58 | 0.82 |
| 4,096 | 1.318 | 0.051 | 0.152 | 1.728 | 1.321 | 26.10 | 0.76 | 1.00 |
| 8,192 | 2.644 | 0.083 | 0.255 | 2.761 | 1.954 | 32.05 | 0.96 | 1.35 |
| 16,384 | 5.279 | 0.109 | 0.685 | 8.481 | 5.880 | 48.48 | 0.62 | 0.90 |
| 32,768 | 10.633 | 0.233 | 2.431 | 15.954 | 11.171 | 45.73 | 0.67 | 0.95 |
| 65,536 | 21.113 | 0.532 | 4.728 | 28.088 | 19.285 | 39.67 | 0.75 | 1.09 |
| 131,072 | 42.565 | 1.083 | 9.377 | 45.799 | 29.393 | 39.30 | 0.93 | 1.45 |
| 262,144 | 85.386 | 2.256 | 18.309 | 81.240 | 48.496 | 37.84 | 1.05 | 1.76 |
| 524,288 | 172.470 | 4.960 | 35.798 | 166.276 | 91.800 | 34.77 | 1.04 | 1.88 |
| 1,048,576 | 341.592 | 9.978 | 68.363 | 321.154 | 181.679 | 34.23 | 1.06 | 1.88 |
| 2,097,152 | 691.427 | 20.681 | 135.265 | 631.467 | 347.730 | 33.43 | 1.09 | 1.99 |
| 4,194,304 | 1,363.012 | 52.004 | 256.814 | 1,258.493 | 687.522 | 26.21 | 1.08 | 1.98 |

## Reading the table

The simulation kernel is 26 to 48 times faster than one CPU thread once the batch has 8,192 programs or more. The gain at the end-to-end level is much smaller.

For this model the GPU spends most of its time moving data, not simulating. An average program runs for about 82 cycles, which is 16.5 instructions. For each program, 392 bytes go to the GPU, and 412 bytes come back when the full state is returned. From 65,536 programs up, the two layout kernels alone take five to nine times as long as the simulation kernel, because each thread reads and writes a whole 392- or 412-byte record that does not sit next to its neighbours' fields.

Returning only the pass/fail flags removes most of the copy back, and the GPU is then 1.9 to 2.0 times as fast as the CPU from 524,288 programs up. With full results, it is between 1.04 and 1.09 times as fast from 262,144 programs up.

The pipelined model does more work per cycle on the same amount of data, so its ratio of work to copying is higher. Measuring it is a Week 13 task.
