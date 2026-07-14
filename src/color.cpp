// chromaforge/color.cpp — color-space conversion and grading operators.
#include "chromaforge/color.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "chromaforge/parallel.hpp"

namespace chromaforge {
namespace color {

void rgbToYCbCr(float r, float g, float b, float& y, float& cb, float& cr) {
    y = 0.299f * r + 0.587f * g + 0.114f * b;
    cb = 128.0f - 0.168736f * r - 0.331264f * g + 0.5f * b;
    cr = 128.0f + 0.5f * r - 0.418688f * g - 0.081312f * b;
}

void ycbcrToRgb(float y, float cb, float cr, float& r, float& g, float& b) {
    const float c = cb - 128.0f;
    const float d = cr - 128.0f;
    r = y + 1.402f * d;
    g = y - 0.344136f * c - 0.714136f * d;
    b = y + 1.772f * c;
}

void brightnessContrast(Image& img, ThreadPool& pool, float brightness, float contrast) {
    const int w = img.width();
    const int c = img.channels();
    const float offset = brightness * 255.0f;
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int i = 0; i < w * c; ++i) {
            const float v = (static_cast<float>(p[i]) - 128.0f) * contrast + 128.0f + offset;
            p[i] = clampByte(v);
        }
    });
}

void gamma(Image& img, ThreadPool& pool, float gammaValue) {
    if (gammaValue <= 0.0f) gammaValue = 1.0f;
    const float invGamma = 1.0f / gammaValue;
    // Precompute a 256-entry lookup table so the hot loop is a memory read.
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        lut[i] = clampByte(255.0f * std::pow(i / 255.0f, invGamma));
    }
    const int w = img.width();
    const int c = img.channels();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int i = 0; i < w * c; ++i) p[i] = lut[p[i]];
    });
}

void exposure(Image& img, ThreadPool& pool, float stops) {
    const float scale = std::pow(2.0f, stops);
    const int w = img.width();
    const int c = img.channels();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int i = 0; i < w * c; ++i) p[i] = clampByte(p[i] * scale);
    });
}

void saturation(Image& img, ThreadPool& pool, float amount) {
    if (img.channels() != 3) return;
    const int w = img.width();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x) {
            const float r = p[x * 3 + 0];
            const float g = p[x * 3 + 1];
            const float b = p[x * 3 + 2];
            const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
            p[x * 3 + 0] = clampByte(luma + amount * (r - luma));
            p[x * 3 + 1] = clampByte(luma + amount * (g - luma));
            p[x * 3 + 2] = clampByte(luma + amount * (b - luma));
        }
    });
}

void liftGammaGain(Image& img, ThreadPool& pool, const float lift[3],
                   const float gammaC[3], const float gain[3]) {
    if (img.channels() != 3) return;
    // Build a per-channel 256-entry LUT: out = (in*gain + lift) ^ (1/gamma).
    std::array<std::array<uint8_t, 256>, 3> lut{};
    for (int ch = 0; ch < 3; ++ch) {
        const float invGamma = 1.0f / (gammaC[ch] > 0.0f ? gammaC[ch] : 1.0f);
        for (int i = 0; i < 256; ++i) {
            float n = i / 255.0f;
            n = n * gain[ch] + lift[ch];
            n = std::clamp(n, 0.0f, 1.0f);
            lut[ch][i] = clampByte(255.0f * std::pow(n, invGamma));
        }
    }
    const int w = img.width();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x) {
            p[x * 3 + 0] = lut[0][p[x * 3 + 0]];
            p[x * 3 + 1] = lut[1][p[x * 3 + 1]];
            p[x * 3 + 2] = lut[2][p[x * 3 + 2]];
        }
    });
}

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

void whiteBalance(Image& img, ThreadPool& pool, float temperature, float tint) {
    if (img.channels() != 3) return;
    // Warm (temperature > 0) lifts red and drops blue; tint > 0 lifts green.
    const float gr = 1.0f + 0.5f * temperature;
    const float gg = 1.0f + 0.5f * tint;
    const float gb = 1.0f - 0.5f * temperature;
    const int w = img.width();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x) {
            p[x * 3 + 0] = clampByte(p[x * 3 + 0] * gr);
            p[x * 3 + 1] = clampByte(p[x * 3 + 1] * gg);
            p[x * 3 + 2] = clampByte(p[x * 3 + 2] * gb);
        }
    });
}

namespace {
// Narkowicz 2015 ACES filmic tone curve (operates on a linear-light value).
inline float acesFilmic(float x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}
}  // namespace

void toneMapReinhard(Image& img, ThreadPool& pool, float exposureStops) {
    // The input is 8-bit, so the entire mapping has only 256 distinct results.
    // Precompute it once (256 pow() calls) instead of evaluating the transfer
    // functions per pixel (tens of millions of pow() calls at 4K).
    const float scale = std::pow(2.0f, exposureStops);
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        float lin = srgbToLinear(i / 255.0f) * scale;
        lin = lin / (1.0f + lin);  // Reinhard highlight compression
        lut[static_cast<size_t>(i)] = clampByte(255.0f * linearToSrgb(lin));
    }
    const int w = img.width();
    const int c = img.channels();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int i = 0; i < w * c; ++i) p[i] = lut[p[i]];
    });
}

void toneMapACES(Image& img, ThreadPool& pool, float exposureStops) {
    const float scale = std::pow(2.0f, exposureStops);
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const float lin = srgbToLinear(i / 255.0f) * scale;
        lut[static_cast<size_t>(i)] = clampByte(255.0f * linearToSrgb(acesFilmic(lin)));
    }
    const int w = img.width();
    const int c = img.channels();
    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int i = 0; i < w * c; ++i) p[i] = lut[p[i]];
    });
}

Image toGrayscale(const Image& src) {
    Image dst(src.width(), src.height(), 1);
    if (src.channels() == 1) {
        std::copy(src.data(), src.data() + src.byteCount(), dst.data());
        return dst;
    }
    const int w = src.width();
    const int h = src.height();
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src.row(y);
        uint8_t* d = dst.row(y);
        for (int x = 0; x < w; ++x) {
            d[x] = clampByte(0.299f * s[x * 3 + 0] + 0.587f * s[x * 3 + 1] +
                             0.114f * s[x * 3 + 2]);
        }
    }
    return dst;
}

}  // namespace color
}  // namespace chromaforge
