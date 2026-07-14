// chromaforge/filters.cpp — separable convolution, Sobel, box blur, sharpen.
#include "chromaforge/filters.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "chromaforge/parallel.hpp"

namespace chromaforge {
namespace filters {

std::vector<float> gaussianKernel1D(float sigma, int& radiusOut) {
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
    std::vector<float> k(static_cast<size_t>(2 * radius + 1));
    const float twoSigma2 = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float w = std::exp(-(static_cast<float>(i) * i) / twoSigma2);
        k[static_cast<size_t>(i + radius)] = w;
        sum += w;
    }
    for (auto& w : k) w /= sum;
    radiusOut = radius;
    return k;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

Image gaussianBlur(const Image& src, ThreadPool& pool, float sigma) {
    if (sigma <= 0.0f) return src;
    int radius = 0;
    const std::vector<float> k = gaussianKernel1D(sigma, radius);

    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();

    // Pass 1 (horizontal) into a float scratch buffer to avoid intermediate
    // quantization between the two 1D passes.
    std::vector<float> tmp(static_cast<size_t>(w) * h * c);
    parallelFor(pool, 0, h, [&](int y) {
        const uint8_t* srow = src.row(y);
        float* trow = tmp.data() + static_cast<size_t>(y) * w * c;
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int i = -radius; i <= radius; ++i) {
                    const int xx = clampi(x + i, 0, w - 1);
                    acc += srow[xx * c + ch] * k[static_cast<size_t>(i + radius)];
                }
                trow[x * c + ch] = acc;
            }
        }
    });

    // Pass 2 (vertical) from the scratch buffer to the output image.
    Image dst(w, h, c);
    parallelFor(pool, 0, h, [&](int y) {
        uint8_t* drow = dst.row(y);
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int i = -radius; i <= radius; ++i) {
                    const int yy = clampi(y + i, 0, h - 1);
                    acc += tmp[(static_cast<size_t>(yy) * w + x) * c + ch] *
                           k[static_cast<size_t>(i + radius)];
                }
                drow[x * c + ch] = clampByte(acc);
            }
        }
    });
    return dst;
}

Image gaussianBlurNaive(const Image& src, ThreadPool& pool, float sigma) {
    if (sigma <= 0.0f) return src;
    int radius = 0;
    const std::vector<float> k1 = gaussianKernel1D(sigma, radius);
    const int ksize = 2 * radius + 1;
    // Full 2D kernel = outer product of the 1D kernel with itself.
    std::vector<float> k2(static_cast<size_t>(ksize) * ksize);
    for (int j = 0; j < ksize; ++j)
        for (int i = 0; i < ksize; ++i)
            k2[static_cast<size_t>(j) * ksize + i] = k1[i] * k1[j];

    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();
    Image dst(w, h, c);
    parallelFor(pool, 0, h, [&](int y) {
        uint8_t* drow = dst.row(y);
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int j = -radius; j <= radius; ++j) {
                    const int yy = clampi(y + j, 0, h - 1);
                    for (int i = -radius; i <= radius; ++i) {
                        const int xx = clampi(x + i, 0, w - 1);
                        acc += src.at(xx, yy, ch) *
                               k2[static_cast<size_t>(j + radius) * ksize + (i + radius)];
                    }
                }
                drow[x * c + ch] = clampByte(acc);
            }
        }
    });
    return dst;
}

Image boxBlur(const Image& src, ThreadPool& pool, int radius) {
    if (radius <= 0) return src;
    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();
    const float norm = 1.0f / (2 * radius + 1);

    std::vector<float> tmp(static_cast<size_t>(w) * h * c);
    parallelFor(pool, 0, h, [&](int y) {
        const uint8_t* srow = src.row(y);
        float* trow = tmp.data() + static_cast<size_t>(y) * w * c;
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                    acc += srow[clampi(x + i, 0, w - 1) * c + ch];
                trow[x * c + ch] = acc * norm;
            }
    });

    Image dst(w, h, c);
    parallelFor(pool, 0, h, [&](int y) {
        uint8_t* drow = dst.row(y);
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                float acc = 0.0f;
                for (int i = -radius; i <= radius; ++i)
                    acc += tmp[(static_cast<size_t>(clampi(y + i, 0, h - 1)) * w + x) * c + ch];
                drow[x * c + ch] = clampByte(acc * norm);
            }
    });
    return dst;
}

Image sobelEdges(const Image& src, ThreadPool& pool) {
    static const int gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    static const int gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};
    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();
    Image dst(w, h, c);
    parallelFor(pool, 0, h, [&](int y) {
        for (int x = 0; x < w; ++x) {
            for (int ch = 0; ch < c; ++ch) {
                float sx = 0.0f, sy = 0.0f;
                for (int j = -1; j <= 1; ++j) {
                    const int yy = clampi(y + j, 0, h - 1);
                    for (int i = -1; i <= 1; ++i) {
                        const int xx = clampi(x + i, 0, w - 1);
                        const float v = src.at(xx, yy, ch);
                        sx += v * gx[j + 1][i + 1];
                        sy += v * gy[j + 1][i + 1];
                    }
                }
                dst.at(x, y, ch) = clampByte(std::sqrt(sx * sx + sy * sy));
            }
        }
    });
    return dst;
}

Image sharpen(const Image& src, ThreadPool& pool, float amount, float sigma) {
    const Image blurred = gaussianBlur(src, pool, sigma);
    const int w = src.width();
    const int h = src.height();
    const int c = src.channels();
    Image dst(w, h, c);
    parallelFor(pool, 0, h, [&](int y) {
        const uint8_t* s = src.row(y);
        const uint8_t* b = blurred.row(y);
        uint8_t* d = dst.row(y);
        for (int i = 0; i < w * c; ++i) {
            const float detail = static_cast<float>(s[i]) - static_cast<float>(b[i]);
            d[i] = clampByte(static_cast<float>(s[i]) + amount * detail);
        }
    });
    return dst;
}

}  // namespace filters
}  // namespace chromaforge
