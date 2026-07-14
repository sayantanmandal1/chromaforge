// chromaforge/bench/bench.cpp — throughput and thread-scaling benchmarks.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

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

static double timeReps(const std::function<void()>& op, int reps) {
    op();  // warm-up
    const auto t0 = Clock::now();
    for (int i = 0; i < reps; ++i) op();
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
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

    std::printf("Separable Gaussian blur (sigma=4) - thread scaling:\n");
    double t1 = 0.0;
    for (unsigned t : {1u, 2u, 4u, 8u}) {
        ThreadPool pool(t);
        const double ms = timeReps(
            [&] {
                Image r = filters::gaussianBlur(base, pool, 4.0f);
                g_sink += r.data()[0];
            },
            reps);
        if (t == 1u) t1 = ms;
        std::printf("  %2u threads: %8.1f ms   %5.2fx   %7.0f Mpix/s\n", t, ms, t1 / ms,
                    mpix / (ms / 1000.0));
    }

    std::printf("\nAlgorithmic optimization (sigma=4, %u threads):\n", hw);
    {
        ThreadPool pool(hw);
        const double sep = timeReps(
            [&] {
                Image r = filters::gaussianBlur(base, pool, 4.0f);
                g_sink += r.data()[0];
            },
            reps);
        const double naive = timeReps(
            [&] {
                Image r = filters::gaussianBlurNaive(base, pool, 4.0f);
                g_sink += r.data()[0];
            },
            2);
        std::printf("  separable  O(k)  : %8.1f ms\n", sep);
        std::printf("  naive 2D   O(k^2): %8.1f ms   -> %.1fx slower\n", naive, naive / sep);
    }

    std::printf("\nPer-pixel operator throughput (%u threads):\n", hw);
    {
        ThreadPool pool(hw);
        const Lut3D lut = Lut3D::identity(33);
        const float lift[3] = {0.02f, 0.0f, -0.01f};
        const float gam[3] = {1.1f, 1.0f, 0.95f};
        const float gain[3] = {1.05f, 1.0f, 0.98f};
        auto rep = [&](const char* name, const std::function<void(Image&)>& op) {
            const double ms = timeReps(
                [&] {
                    Image work = base;
                    op(work);
                    g_sink += work.data()[0];
                },
                reps);
            std::printf("  %-22s %8.1f ms   %7.0f Mpix/s\n", name, ms, mpix / (ms / 1000.0));
        };
        rep("3D LUT (33^3) apply", [&](Image& im) { lut.apply(im, pool); });
        rep("lift/gamma/gain", [&](Image& im) { color::liftGammaGain(im, pool, lift, gam, gain); });
        rep("ACES tone map", [&](Image& im) { color::toneMapACES(im, pool, 0.5f); });
        rep("white balance", [&](Image& im) { color::whiteBalance(im, pool, 0.2f, -0.1f); });
    }
    return 0;
}
