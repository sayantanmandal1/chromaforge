// chromaforge/lut.cpp — .cube parsing and trilinear 3D-LUT application.
#include "chromaforge/lut.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "chromaforge/parallel.hpp"

namespace chromaforge {

Lut3D Lut3D::identity(int size) {
    if (size < 2) throw std::invalid_argument("Lut3D::identity: size must be >= 2");
    Lut3D lut;
    lut.size_ = size;
    lut.data_.resize(static_cast<size_t>(size) * size * size * 3);
    const float denom = static_cast<float>(size - 1);
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                float* n = &lut.data_[(((static_cast<size_t>(b) * size + g) * size + r)) * 3];
                n[0] = r / denom;
                n[1] = g / denom;
                n[2] = b / denom;
            }
    return lut;
}

Lut3D Lut3D::fromCubeFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open LUT: " + path);

    int size = 0;
    std::vector<float> data;
    std::string line;
    while (std::getline(in, line)) {
        const size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || line[start] == '#') continue;

        std::istringstream ss(line.substr(start));
        std::string token;
        ss >> token;

        if (token == "LUT_3D_SIZE") {
            ss >> size;
            if (size < 2 || size > 256) throw std::runtime_error("LUT: unsupported LUT_3D_SIZE");
            data.reserve(static_cast<size_t>(size) * size * size * 3);
            continue;
        }
        if (token == "TITLE" || token == "DOMAIN_MIN" || token == "DOMAIN_MAX" ||
            token == "LUT_1D_SIZE" || token == "LUT_3D_INPUT_RANGE" ||
            token == "LUT_1D_INPUT_RANGE") {
            continue;
        }

        // Otherwise treat the line as an "R G B" data row.
        try {
            const float r = std::stof(token);
            float g = 0.0f, b = 0.0f;
            if (!(ss >> g >> b)) continue;
            data.push_back(r);
            data.push_back(g);
            data.push_back(b);
        } catch (const std::exception&) {
            continue;
        }
    }

    if (size == 0) throw std::runtime_error("LUT: missing LUT_3D_SIZE");
    if (data.size() != static_cast<size_t>(size) * size * size * 3) {
        throw std::runtime_error("LUT: entry count does not match LUT_3D_SIZE");
    }

    Lut3D lut;
    lut.size_ = size;
    lut.data_ = std::move(data);
    return lut;
}

void Lut3D::apply(Image& img, ThreadPool& pool) const {
    if (empty() || img.channels() != 3) return;
    const int w = img.width();
    const int n = size_;
    const float scale = static_cast<float>(n - 1);

    parallelFor(pool, 0, img.height(), [&](int y) {
        uint8_t* p = img.row(y);
        for (int x = 0; x < w; ++x) {
            const float rf = p[x * 3 + 0] / 255.0f * scale;
            const float gf = p[x * 3 + 1] / 255.0f * scale;
            const float bf = p[x * 3 + 2] / 255.0f * scale;

            const int r0 = std::min(static_cast<int>(rf), n - 1);
            const int g0 = std::min(static_cast<int>(gf), n - 1);
            const int b0 = std::min(static_cast<int>(bf), n - 1);
            const int r1 = std::min(r0 + 1, n - 1);
            const int g1 = std::min(g0 + 1, n - 1);
            const int b1 = std::min(b0 + 1, n - 1);
            const float dr = rf - r0, dg = gf - g0, db = bf - b0;

            for (int c = 0; c < 3; ++c) {
                const float c000 = node(r0, g0, b0)[c], c100 = node(r1, g0, b0)[c];
                const float c010 = node(r0, g1, b0)[c], c110 = node(r1, g1, b0)[c];
                const float c001 = node(r0, g0, b1)[c], c101 = node(r1, g0, b1)[c];
                const float c011 = node(r0, g1, b1)[c], c111 = node(r1, g1, b1)[c];
                const float c00 = c000 * (1 - dr) + c100 * dr;
                const float c10 = c010 * (1 - dr) + c110 * dr;
                const float c01 = c001 * (1 - dr) + c101 * dr;
                const float c11 = c011 * (1 - dr) + c111 * dr;
                const float c0 = c00 * (1 - dg) + c10 * dg;
                const float c1 = c01 * (1 - dg) + c11 * dg;
                p[x * 3 + c] = clampByte((c0 * (1 - db) + c1 * db) * 255.0f);
            }
        }
    });
}

}  // namespace chromaforge
