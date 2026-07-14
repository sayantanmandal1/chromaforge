// chromaforge/image.hpp — 8-bit interleaved image with dependency-free PNM I/O.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chromaforge {

// Clamp a floating-point sample to a rounded 8-bit value.
inline uint8_t clampByte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

// A simple 8-bit-per-channel image with interleaved storage (row-major).
// Channels: 1 (grayscale) or 3 (RGB). Backed by a contiguous byte buffer so it
// is cache-friendly and trivially parallelizable by row.
class Image {
public:
    Image() = default;
    Image(int width, int height, int channels);

    int width() const { return width_; }
    int height() const { return height_; }
    int channels() const { return channels_; }
    bool empty() const { return data_.empty(); }
    size_t pixelCount() const { return static_cast<size_t>(width_) * height_; }
    size_t byteCount() const { return data_.size(); }

    uint8_t* data() { return data_.data(); }
    const uint8_t* data() const { return data_.data(); }

    uint8_t* row(int y) {
        return data_.data() + static_cast<size_t>(y) * width_ * channels_;
    }
    const uint8_t* row(int y) const {
        return data_.data() + static_cast<size_t>(y) * width_ * channels_;
    }

    uint8_t& at(int x, int y, int c) { return data_[index(x, y, c)]; }
    uint8_t at(int x, int y, int c) const { return data_[index(x, y, c)]; }

    // Load/save Netpbm binary formats: P6 (RGB) and P5 (grayscale), 8-bit.
    static Image load(const std::string& path);
    void save(const std::string& path) const;

private:
    size_t index(int x, int y, int c) const {
        return (static_cast<size_t>(y) * width_ + x) * channels_ + c;
    }

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<uint8_t> data_;
};

}  // namespace chromaforge
