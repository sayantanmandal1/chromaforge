# chromaforge

A multithreaded C++17 **color-grading and image-processing engine** — the kind of
core that sits under a video/photo editor. It implements the color and imaging
math directly (no OpenCV/OpenColorIO), parallelizes every operator across a
thread pool, and ships with a benchmark harness and a unit-test suite.

Built to explore the performance and correctness problems that show up in real
color pipelines: separable convolution, trilinear 3D-LUT sampling, linear-light
tone mapping, and getting near-linear speedups out of a work queue.

## Features

- **Color science** — sRGB ⇄ linear transfer functions, BT.601 YCbCr, exposure,
  gamma, brightness/contrast, saturation, white balance.
- **Professional grading** — ASC-CDL style lift/gamma/gain, and **3D LUT (`.cube`)**
  application with trilinear interpolation (the "core color algorithm").
- **HDR tone mapping** — Reinhard and ACES (Narkowicz) curves, applied in
  linear light.
- **Convolution filters** — separable Gaussian (with a naive 2D reference for
  validation), box blur, Sobel edges, unsharp-mask sharpen.
- **Parallelism** — a condition-variable thread pool + a tiled `parallelFor`;
  every per-pixel and per-row operator scales across cores.
- **Tooling** — a `cf` CLI, a benchmark, 13 unit tests, and a `.ppm/.cube`
  dependency-free I/O path.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build        # runs the unit tests
```

Or directly with a compiler:

```bash
g++ -std=c++17 -O3 -pthread -Iinclude src/*.cpp apps/cli.cpp -o cf
```

## Usage

```bash
cf --gradient in.ppm 1920 1080                      # make a test image
cf in.ppm out.ppm --tonemap aces 0.5 --cdl 0.02 1.1 1.05 --saturation 1.2 --blur 1.5
cf in.ppm graded.ppm --lut film.cube --threads 8
```

## Benchmarks

Measured on a 14-thread laptop at **4K UHD (3840×2160, 8.3 Mpix)**; numbers vary
by machine, but the ratios are the point.

**Thread scaling — separable Gaussian blur (σ=4):**

| threads | time | speedup |
|--------:|-----:|--------:|
| 1 | 1309 ms | 1.00× |
| 2 |  720 ms | 1.82× |
| 4 |  463 ms | 2.83× |
| 8 |  339 ms | 3.86× |

**Algorithmic optimization:** the separable O(k) blur is **~9.7× faster** than the
naive 2D O(k²) kernel for the same output.

**Profiling-driven optimization:** the tone-map operators evaluate `pow()`-heavy
transfer functions. Because the input is 8-bit, the full mapping has only 256
distinct results — precomputing it once dropped ACES tone-mapping of a 4K frame
from **843 ms → 12 ms (~70×)**.

**Per-pixel operator throughput (multithreaded):** 3D-LUT ≈ 140–160 Mpix/s,
lift/gamma/gain ≈ 730–990 Mpix/s, ACES ≈ 690 Mpix/s, white balance ≈ 550 Mpix/s.

## Design notes

- Images are 8-bit interleaved and cache-friendly; filters that need precision
  between passes use a float scratch buffer.
- `parallelFor` splits the row range into one contiguous chunk per worker, so
  workers write disjoint output rows — no locking on the hot path.
- The naive Gaussian and identity-LUT paths exist specifically to *validate* the
  fast paths in the test suite (separable ≡ naive, identity LUT ≡ no-op).

## Scope

This is an individual systems project written to work through the color/imaging
and multithreading problems first-hand — not a drop-in replacement for a
production library. `.ppm`/`.cube` I/O keeps it dependency-free and easy to read.

MIT licensed.
