// chromaforge/lut.hpp — 3D color lookup table (.cube) with trilinear sampling.
#pragma once

#include <string>
#include <vector>

#include "chromaforge/image.hpp"
#include "chromaforge/thread_pool.hpp"

namespace chromaforge {

// A 3D LUT as used by professional color-grading tools. Stores an NxNxN grid of
// RGB output values in [0,1]; apply() maps each pixel by trilinear interpolation
// of the eight surrounding grid nodes.
class Lut3D {
public:
    Lut3D() = default;

    // Identity LUT (output == input) — useful as a baseline and for tests.
    static Lut3D identity(int size);

    // Parse an Adobe/Resolve ".cube" 3D LUT (LUT_3D_SIZE + N^3 RGB rows,
    // red index varying fastest).
    static Lut3D fromCubeFile(const std::string& path);

    int size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Apply the LUT in place (RGB channels only), parallelized by row.
    void apply(Image& img, ThreadPool& pool) const;

private:
    const float* node(int r, int g, int b) const {
        return &data_[(((static_cast<size_t>(b) * size_ + g) * size_ + r)) * 3];
    }

    int size_ = 0;
    std::vector<float> data_;  // size_^3 * 3 floats, red-fastest ordering
};

}  // namespace chromaforge
