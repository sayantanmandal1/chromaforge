// chromaforge/filters.hpp — convolution-based image filters.
#pragma once

#include <vector>

#include "chromaforge/image.hpp"
#include "chromaforge/thread_pool.hpp"

namespace chromaforge {
namespace filters {

// Separable Gaussian blur (two 1D passes: O(k) work per pixel instead of the
// O(k^2) of a full 2D kernel). Returns a new image; edges use clamp addressing.
Image gaussianBlur(const Image& src, ThreadPool& pool, float sigma);

// Reference non-separable Gaussian (full 2D kernel) — used to benchmark and
// validate the separable implementation.
Image gaussianBlurNaive(const Image& src, ThreadPool& pool, float sigma);

// Box blur of the given radius (separable moving sum).
Image boxBlur(const Image& src, ThreadPool& pool, int radius);

// Sobel gradient magnitude per channel (edge map).
Image sobelEdges(const Image& src, ThreadPool& pool);

// Unsharp mask: out = src + amount * (src - gaussianBlur(src, sigma)).
Image sharpen(const Image& src, ThreadPool& pool, float amount, float sigma);

// Build a normalized 1D Gaussian kernel for a given sigma.
std::vector<float> gaussianKernel1D(float sigma, int& radiusOut);

}  // namespace filters
}  // namespace chromaforge
