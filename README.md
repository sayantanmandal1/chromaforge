# chromaforge

A multithreaded C++17 colour-grading and image-processing engine — the kind of core that sits under
a video or photo editor. It implements the colour and imaging maths directly (no OpenCV, no
OpenColorIO), parallelises every operator across a thread pool, and ships with a benchmark harness
and a unit-test suite.

Built to work through the performance and correctness problems that show up in real colour
pipelines: separable convolution, trilinear 3D-LUT sampling, linear-light tone mapping, and getting
near-linear speedups out of a work queue.

---

## Features

- **Colour science** — sRGB ⇄ linear transfer functions, BT.601 YCbCr, exposure, gamma,
  brightness/contrast, saturation, white balance.
- **Professional grading** — ASC-CDL style lift/gamma/gain, and 3D LUT (`.cube`) application with
  trilinear interpolation.
- **HDR tone mapping** — Reinhard and ACES (Narkowicz) curves, applied in linear light.
- **Convolution filters** — separable Gaussian (with a naive 2D reference for validation), box blur,
  Sobel edges, unsharp-mask sharpen.
- **Parallelism** — a condition-variable thread pool plus a tiled `parallelFor`; every per-pixel and
  per-row operator scales across cores.
- **Tooling** — a `cf` CLI, a benchmark harness, 28 unit-test assertions, and a dependency-free
  `.ppm`/`.cube` I/O path.

---

## Benchmarks

Measured on an **Intel Core Ultra 7 265U** (14 logical threads), MSVC `/O2`, 4K UHD frames
(3840×2160, 8.3 Mpix).

> **On measurement honesty.** An early version of this harness reported a single timing per
> operator. On a hybrid P-core/E-core CPU that is not reportable: consecutive identical runs
> disagreed by 30–50 % (white balance measured 515 Mpix/s, then 720 Mpix/s), purely depending on
> whether the scheduler parked the work on performance or efficiency cores. The harness now reports
> the **median of 5 independent bursts** and prints the min–max spread alongside it, so the noise is
> visible instead of hidden. Every number below is a median.

### Thread scaling — separable Gaussian (σ = 4)

| threads | median | speedup | throughput | spread |
|--------:|-------:|--------:|-----------:|--------|
| 1 | 1895.7 ms | 1.00× | 4 Mpix/s | 1800–3286 ms |
| 2 | 1646.4 ms | 1.15× | 5 Mpix/s | 1381–2230 ms |
| 4 | 823.8 ms | 2.30× | 10 Mpix/s | 744–848 ms |
| 8 | 633.6 ms | 2.99× | 13 Mpix/s | 541–726 ms |

Scaling is sub-linear and the low thread counts are the noisiest rows — both are consequences of the
hybrid core layout rather than of lock contention: `parallelFor` hands each worker one contiguous,
disjoint range of output rows, so there is no locking on the hot path at all.

### Algorithmic optimisation

| implementation | median | |
|----------------|-------:|--|
| separable, O(k) | 547.2 ms | |
| naive 2D, O(k²) | 7352.4 ms | **13.4× slower** |

The naive 2D kernel is kept in the codebase specifically so the test suite can assert that the fast
separable path produces the same image. An optimisation nobody validates is a guess.

### Per-pixel operator throughput (14 threads)

| operator | median | throughput | spread |
|----------|-------:|-----------:|--------|
| brightness/contrast | 17.0 ms | **489 Mpix/s** | 16–37 ms |
| white balance | 17.5 ms | **473 Mpix/s** | 17–20 ms |
| lift/gamma/gain | 17.6 ms | **472 Mpix/s** | 14–19 ms |
| ACES tone map | 19.8 ms | **419 Mpix/s** | 18–22 ms |
| exposure | 21.0 ms | **395 Mpix/s** | 20–26 ms |
| Reinhard tone map | 23.3 ms | **356 Mpix/s** | 21–24 ms |
| saturation | 31.4 ms | **264 Mpix/s** | 30–32 ms |
| 3D LUT (33³) trilinear | 96.6 ms | **86 Mpix/s** | 89–104 ms |

### The optimisation that matters here

The input is 8-bit. Any operator that is a **pure function of a single input byte** therefore has at
most 256 distinct outputs, so the entire mapping can be precomputed once instead of evaluated
8.3 million times per frame. `brightnessContrast`, `exposure` and `whiteBalance` were doing
per-pixel floating-point work; converting them to 256-entry lookup tables moved white balance from
**306 → 473 Mpix/s (1.55×)** with bit-identical output.

The table above is also the evidence for where that trick stops working:

- Everything LUT-able clusters at **356–489 Mpix/s**, regardless of how expensive its maths looks.
  ACES tone mapping evaluates `pow()`-heavy transfer functions and still lands at 419 Mpix/s,
  because those `pow()` calls happen 256 times, not 8.3 million times.
- **`saturation` sits alone at 264 Mpix/s** — roughly half the LUT-able operators. It cannot be
  tabulated, because each output channel depends on red, green *and* blue together, so the domain is
  2²⁴ rather than 2⁸. This is the honest control in the experiment: the same thread pool, the same
  memory layout, only the algorithmic trick removed.
- **The 3D LUT is the real cost at 86 Mpix/s**, ~5× slower than the per-channel operators. Trilinear
  interpolation reads eight neighbouring lattice entries per pixel with poor cache locality, and that
  gather dominates — the arithmetic is not the bottleneck.

Correctness is not assumed. `tests.cpp` re-derives the pre-optimisation arithmetic independently and
asserts the LUT paths match it with **zero** difference across every reachable input byte, plus that
neutral parameters leave an image untouched.

---

## Build

The repository ships a CMake build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Or directly with a compiler:

```bash
g++ -std=c++17 -O3 -pthread -Iinclude src/*.cpp apps/cli.cpp -o cf
```

On Windows without CMake, `build_and_test.bat` initialises the MSVC environment and drives `cl`
directly.

## Usage

```bash
cf --gradient in.ppm 1920 1080                      # make a test image
cf in.ppm out.ppm --tonemap aces 0.5 --cdl 0.02 1.1 1.05 --saturation 1.2 --blur 1.5
cf in.ppm graded.ppm --lut film.cube --threads 8
```

Run the benchmark:

```bash
build/cf_bench            # defaults to 4K UHD
build/cf_bench 1920 1080  # any resolution
```

## Design notes

- Images are 8-bit interleaved and cache-friendly; filters needing precision between passes use a
  float scratch buffer.
- `parallelFor` splits the row range into one contiguous chunk per worker, so workers write disjoint
  output rows — no locking on the hot path.
- The naive Gaussian and identity-LUT paths exist specifically to validate the fast paths in the test
  suite (separable ≡ naive, identity LUT ≡ no-op).
- Lookup tables are built per call, not cached across calls. At 256 entries the build cost is
  irrelevant next to a 4K frame, and it keeps the operators stateless and trivially thread-safe.

## Known limitations

Stated here rather than discovered in review:

- **8-bit only.** The 256-entry LUT trick is exactly what breaks first at 16-bit or float32, where
  real grading happens. A higher-precision pipeline would need genuine vectorisation instead.
- **No SIMD.** Operators are scalar C++ relying on the compiler's auto-vectoriser. Hand-written
  AVX2/NEON kernels are the obvious next step, and would matter most for `saturation` and the 3D LUT
  — precisely the two operators the lookup-table approach cannot help.
- **No GPU backend.** At 86 Mpix/s the 3D LUT is the natural first candidate to move to a compute
  shader.
- **`.ppm` / `.cube` I/O only.** Keeps the project dependency-free and readable, but means it cannot
  ingest real footage without a conversion step. EXR and 16-bit PNG/TIFF are the gap.
- **No colour-accuracy validation against a reference implementation.** The tests prove internal
  consistency and that fast paths match slow paths; they do not prove the ACES curve matches
  OpenColorIO to a given ΔE.
- Benchmarks come from one laptop with a hybrid core layout. The spreads are published for that
  reason — treat the ratios as the result, not the absolute throughput.

## Scope

An individual systems project written to work through the colour/imaging and multithreading problems
first-hand — not a drop-in replacement for a production library.

MIT licensed.
