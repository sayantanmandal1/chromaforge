// chromaforge/bench/bench.cpp — throughput and thread-scaling benchmarks.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

#include "chromaforge/color.hpp"
#include "chromaforge/filters.hpp"
#include "chromaforge/image.hpp"
#include "chromaforge/lut.hpp"
#include "chromaforge/thread_pool.hpp"

using namespace chromaforge;
using Clock = std::chrono::steady_clock;

// A sink the optimizer cannot prove is unused, so it can't elide the work.
static volatile uint64_t g_sink = 0;

static Image makeImage(int w, int h) {
    Image img(w, h, 3);
    for (int y = 0; y < h; ++y) {
        uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x) {
            p[x * 3 + 0] = static_cast<uint8_t>((x * 37 + y * 11) & 0xFF);
            p[x * 3 + 1] = static_cast<uint8_t>((x * 17 + y * 29) & 0xFF);
            p[x * 3 + 2] = static_cast<uint8_t>((x * 5 + y * 53) & 0xFF);
        }
    }
    return img;
}

// Mean of a single burst. Kept separate so the outer loop can take a median across bursts.
static double timeBurst(const std::function<void()>& op, int reps) {
    const auto t0 = Clock::now();
    for (int i = 0; i < reps; ++i) op();
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
}

// Median of several independent bursts.
//
// A single timing is not reportable on a modern laptop: hybrid P-core/E-core scheduling, turbo
// residency and thermal state move results by 30-50% between otherwise identical runs. The median
// rejects the occasional burst that landed on efficiency cores, and the min/max spread is printed
// so the reader can see how noisy the measurement actually was rather than trusting one number.
struct Timing {
    double median;
    double best;
    double worst;
};

static Timing timeReps(const std::function<void()>& op, int reps, int bursts = 5) {
    op();  // warm-up: first touch, page faults, branch predictor
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(bursts));
    for (int b = 0; b < bursts; ++b) samples.push_back(timeBurst(op, reps));
    std::sort(samples.begin(), samples.end());
    return {samples[samples.size() / 2], samples.front(), samples.back()};
}

int main(int argc, char** argv) {
    int w = 3840, h = 2160;  // 4K UHD
    if (argc >= 3) {
        w = std::atoi(argv[1]);
        h = std::atoi(argv[2]);
    }
    const double mpix = static_cast<double>(w) * h / 1e6;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::printf("chromaforge benchmark - %dx%d (%.1f Mpix), %u hardware threads\n\n", w, h, mpix, hw);

    const Image base = makeImage(w, h);
    const int reps = 5;

    std::printf("Separable Gaussian blur (sigma=4) - thread scaling (median of 5 bursts):\n");
    double t1 = 0.0;
    for (unsigned t : {1u, 2u, 4u, 8u}) {
        ThreadPool pool(t);
        const Timing tm = timeReps(
            [&] {
                Image r = filters::gaussianBlur(base, pool, 4.0f);
                g_sink += r.data()[0];
            },
            reps);
        if (t == 1u) t1 = tm.median;
        std::printf("  %2u threads: %8.1f ms   %5.2fx   %7.0f Mpix/s   [%.0f-%.0f ms]\n", t,
                    tm.median, t1 / tm.median, mpix / (tm.median / 1000.0), tm.best, tm.worst);
    }

    std::printf("\nAlgorithmic optimization (sigma=4, %u threads):\n", hw);
    {
        ThreadPool pool(hw);
        const Timing sep = timeReps(
            [&] {
                Image r = filters::gaussianBlur(base, pool, 4.0f);
                g_sink += r.data()[0];
            },
            reps);
        const Timing naive = timeReps(
            [&] {
                Image r = filters::gaussianBlurNaive(base, pool, 4.0f);
                g_sink += r.data()[0];
            },
            2, 3);
        std::printf("  separable  O(k)  : %8.1f ms\n", sep.median);
        std::printf("  naive 2D   O(k^2): %8.1f ms   -> %.1fx slower\n", naive.median,
                    naive.median / sep.median);
    }

    std::printf("\nPer-pixel operator throughput (%u threads, median of 5 bursts):\n", hw);
    {
        ThreadPool pool(hw);
        const Lut3D lut = Lut3D::identity(33);
        const float lift[3] = {0.02f, 0.0f, -0.01f};
        const float gam[3] = {1.1f, 1.0f, 0.95f};
        const float gain[3] = {1.05f, 1.0f, 0.98f};
        auto rep = [&](const char* name, const std::function<void(Image&)>& op) {
            const Timing tm = timeReps(
                [&] {
                    Image work = base;
                    op(work);
                    g_sink += work.data()[0];
                },
                reps);
            std::printf("  %-22s %8.1f ms   %7.0f Mpix/s   [%.0f-%.0f ms]\n", name, tm.median,
                        mpix / (tm.median / 1000.0), tm.best, tm.worst);
        };
        rep("3D LUT (33^3) apply", [&](Image& im) { lut.apply(im, pool); });
        rep("lift/gamma/gain", [&](Image& im) { color::liftGammaGain(im, pool, lift, gam, gain); });
        rep("ACES tone map", [&](Image& im) { color::toneMapACES(im, pool, 0.5f); });
        rep("Reinhard tone map", [&](Image& im) { color::toneMapReinhard(im, pool, 0.5f); });
        rep("white balance", [&](Image& im) { color::whiteBalance(im, pool, 0.2f, -0.1f); });
        rep("exposure", [&](Image& im) { color::exposure(im, pool, 0.75f); });
        rep("brightness/contrast", [&](Image& im) { color::brightnessContrast(im, pool, 0.1f, 1.2f); });
        rep("saturation", [&](Image& im) { color::saturation(im, pool, 1.3f); });
    }
    return 0;
}
