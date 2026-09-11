// chromaforge/tests/tests.cpp â€” dependency-free unit tests with real assertions.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "chromaforge/color.hpp"
#include "chromaforge/filters.hpp"
#include "chromaforge/image.hpp"
#include "chromaforge/lut.hpp"
#include "chromaforge/parallel.hpp"
#include "chromaforge/thread_pool.hpp"

using namespace chromaforge;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                 \
    do {                                                            \
        ++g_checks;                                                 \
        if (!(cond)) {                                              \
            ++g_failures;                                           \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        }                                                           \
    } while (0)

// Deterministic RGB gradient for tests.
static Image makeGradient(int w, int h) {
    Image img(w, h, 3);
    for (int y = 0; y < h; ++y) {
        uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x) {
            p[x * 3 + 0] = static_cast<uint8_t>((x * 255) / (w - 1));
            p[x * 3 + 1] = static_cast<uint8_t>((y * 255) / (h - 1));
            p[x * 3 + 2] = static_cast<uint8_t>(((x + y) * 255) / (w + h - 2));
        }
    }
    return img;
}

static int maxAbsDiff(const Image& a, const Image& b) {
    int m = 0;
    for (size_t i = 0; i < a.byteCount(); ++i)
        m = std::max(m, std::abs(static_cast<int>(a.data()[i]) - static_cast<int>(b.data()[i])));
    return m;
}

static void testImageRoundTrip() {
    std::printf("test: image PNM round-trip\n");
    Image img = makeGradient(64, 48);
    const std::string path = "cf_test_tmp.ppm";
    img.save(path);
    Image loaded = Image::load(path);
    CHECK(loaded.width() == 64 && loaded.height() == 48 && loaded.channels() == 3);
    CHECK(maxAbsDiff(img, loaded) == 0);
    std::remove(path.c_str());
}

static void testColorRoundTrip() {
    std::printf("test: YCbCr <-> RGB round-trip\n");
    float maxErr = 0.0f;
    for (int r = 0; r <= 255; r += 17)
        for (int g = 0; g <= 255; g += 17)
            for (int b = 0; b <= 255; b += 17) {
                float y, cb, cr, r2, g2, b2;
                color::rgbToYCbCr(static_cast<float>(r), static_cast<float>(g),
                                  static_cast<float>(b), y, cb, cr);
                color::ycbcrToRgb(y, cb, cr, r2, g2, b2);
                maxErr = std::max(maxErr, std::fabs(r2 - r));
                maxErr = std::max(maxErr, std::fabs(g2 - g));
                maxErr = std::max(maxErr, std::fabs(b2 - b));
            }
    CHECK(maxErr < 0.5f);
}

static void testTransferRoundTrip() {
    std::printf("test: sRGB transfer round-trip\n");
    float maxErr = 0.0f;
    for (int i = 0; i <= 255; ++i) {
        const float c = i / 255.0f;
        const float back = color::linearToSrgb(color::srgbToLinear(c));
        maxErr = std::max(maxErr, std::fabs(back - c));
    }
    CHECK(maxErr < 1e-4f);
}

static void testGammaIdentity(ThreadPool& pool) {
    std::printf("test: gamma(1.0) is identity\n");
    Image a = makeGradient(80, 60);
    Image b = a;
    color::gamma(b, pool, 1.0f);
    CHECK(maxAbsDiff(a, b) <= 1);
}

static void testSaturationIdentity(ThreadPool& pool) {
    std::printf("test: saturation(1.0) is identity\n");
    Image a = makeGradient(80, 60);
    Image b = a;
    color::saturation(b, pool, 1.0f);
    CHECK(maxAbsDiff(a, b) <= 1);
}

static void testSaturationGray(ThreadPool& pool) {
    std::printf("test: saturation(0) yields near-equal channels\n");
    Image a = makeGradient(80, 60);
    color::saturation(a, pool, 0.0f);
    int m = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uint8_t* p = a.row(y);
        for (int x = 0; x < a.width(); ++x) {
            m = std::max(m, std::abs(static_cast<int>(p[x * 3 + 0]) - static_cast<int>(p[x * 3 + 1])));
            m = std::max(m, std::abs(static_cast<int>(p[x * 3 + 1]) - static_cast<int>(p[x * 3 + 2])));
        }
    }
    CHECK(m <= 1);
}

static void testLutIdentity(ThreadPool& pool) {
    std::printf("test: identity 3D-LUT is a no-op\n");
    Image a = makeGradient(96, 72);
    Image b = a;
    Lut3D::identity(33).apply(b, pool);
    CHECK(maxAbsDiff(a, b) <= 1);
}

static void testCubeParse(ThreadPool& pool) {
    std::printf("test: .cube parse + apply matches identity\n");
    const std::string path = "cf_test_identity.cube";
    const int n = 9;
    {
        FILE* f = std::fopen(path.c_str(), "w");
        std::fprintf(f, "TITLE \"identity\"\nLUT_3D_SIZE %d\n", n);
        for (int b = 0; b < n; ++b)
            for (int g = 0; g < n; ++g)
                for (int r = 0; r < n; ++r)
                    std::fprintf(f, "%f %f %f\n", r / (n - 1.0f), g / (n - 1.0f), b / (n - 1.0f));
        std::fclose(f);
    }
    Lut3D lut = Lut3D::fromCubeFile(path);
    CHECK(lut.size() == n);
    Image a = makeGradient(96, 72);
    Image b = a;
    lut.apply(b, pool);
    CHECK(maxAbsDiff(a, b) <= 2);
    std::remove(path.c_str());
}

static void testSeparableMatchesNaive(ThreadPool& pool) {
    std::printf("test: separable Gaussian == naive 2D Gaussian\n");
    Image a = makeGradient(128, 96);
    Image sep = filters::gaussianBlur(a, pool, 2.5f);
    Image naive = filters::gaussianBlurNaive(a, pool, 2.5f);
    CHECK(maxAbsDiff(sep, naive) <= 1);
}

static void testBoxBlurConstant(ThreadPool& pool) {
    std::printf("test: box blur preserves a constant image\n");
    Image a(64, 64, 3);
    for (size_t i = 0; i < a.byteCount(); ++i) a.data()[i] = 137;
    Image b = filters::boxBlur(a, pool, 3);
    CHECK(maxAbsDiff(a, b) <= 1);
}

static void testThreadPoolCorrectness() {
    std::printf("test: parallelFor over disjoint indices is race-free\n");
    ThreadPool pool(8);
    const int n = 100000;
    std::vector<long long> per(static_cast<size_t>(n));
    parallelFor(pool, 0, n, [&](int i) { per[static_cast<size_t>(i)] = i; });
    long long sum = 0;
    for (int i = 0; i < n; ++i) sum += per[static_cast<size_t>(i)];
    CHECK(sum == static_cast<long long>(n) * (n - 1) / 2);
}

// The lookup-table fast paths below replaced per-pixel float math. An optimization that
// changes output is a bug, so each is compared against the arithmetic it is meant to
// replace, evaluated independently here over every reachable input byte.

static void testExposureMatchesReference(ThreadPool& pool) {
    std::printf("test: exposure LUT matches per-pixel reference\n");
    for (float stops : {-2.0f, -0.5f, 0.0f, 1.0f, 2.5f}) {
        Image img = makeGradient(97, 61);
        Image expected = img;
        const float scale = std::pow(2.0f, stops);
        for (size_t i = 0; i < expected.byteCount(); ++i)
            expected.data()[i] = clampByte(static_cast<float>(expected.data()[i]) * scale);
        color::exposure(img, pool, stops);
        CHECK(maxAbsDiff(img, expected) == 0);
    }
}

static void testBrightnessContrastMatchesReference(ThreadPool& pool) {
    std::printf("test: brightness/contrast LUT matches per-pixel reference\n");
    const float cases[][2] = {{0.0f, 1.0f}, {0.25f, 1.0f}, {-0.2f, 1.6f}, {0.1f, 0.4f}};
    for (const auto& c : cases) {
        Image img = makeGradient(83, 55);
        Image expected = img;
        const float offset = c[0] * 255.0f;
        for (size_t i = 0; i < expected.byteCount(); ++i) {
            const float v = (static_cast<float>(expected.data()[i]) - 128.0f) * c[1] + 128.0f + offset;
            expected.data()[i] = clampByte(v);
        }
        color::brightnessContrast(img, pool, c[0], c[1]);
        CHECK(maxAbsDiff(img, expected) == 0);
    }
}

static void testWhiteBalanceMatchesReference(ThreadPool& pool) {
    std::printf("test: white balance LUT matches per-pixel reference\n");
    const float cases[][2] = {{0.0f, 0.0f}, {0.4f, 0.1f}, {-0.6f, 0.3f}};
    for (const auto& c : cases) {
        Image img = makeGradient(71, 49);
        Image expected = img;
        const float gr = 1.0f + 0.5f * c[0];
        const float gg = 1.0f + 0.5f * c[1];
        const float gb = 1.0f - 0.5f * c[0];
        const int w = expected.width();
        for (int y = 0; y < expected.height(); ++y) {
            uint8_t* p = expected.row(y);
            for (int x = 0; x < w; ++x) {
                p[x * 3 + 0] = clampByte(p[x * 3 + 0] * gr);
                p[x * 3 + 1] = clampByte(p[x * 3 + 1] * gg);
                p[x * 3 + 2] = clampByte(p[x * 3 + 2] * gb);
            }
        }
        color::whiteBalance(img, pool, c[0], c[1]);
        CHECK(maxAbsDiff(img, expected) == 0);
    }
}

static void testIdentityOperatorsArePixelExact(ThreadPool& pool) {
    std::printf("test: neutral parameters leave the image untouched\n");
    Image original = makeGradient(64, 64);

    Image a = original;
    color::exposure(a, pool, 0.0f);
    CHECK(maxAbsDiff(a, original) == 0);

    Image b = original;
    color::brightnessContrast(b, pool, 0.0f, 1.0f);
    CHECK(maxAbsDiff(b, original) == 0);

    Image c = original;
    color::whiteBalance(c, pool, 0.0f, 0.0f);
    CHECK(maxAbsDiff(c, original) == 0);
}

int main() {
    std::printf("chromaforge test suite\n");
    ThreadPool pool(0);
    testImageRoundTrip();
    testColorRoundTrip();
    testTransferRoundTrip();
    testGammaIdentity(pool);
    testSaturationIdentity(pool);
    testSaturationGray(pool);
    testLutIdentity(pool);
    testCubeParse(pool);
    testSeparableMatchesNaive(pool);
    testBoxBlurConstant(pool);
    testThreadPoolCorrectness();
    testExposureMatchesReference(pool);
    testBrightnessContrastMatchesReference(pool);
    testWhiteBalanceMatchesReference(pool);
    testIdentityOperatorsArePixelExact(pool);
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("ALL TESTS PASSED\n");
    return g_failures == 0 ? 0 : 1;
}
