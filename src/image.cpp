// chromaforge/image.cpp — Netpbm (P5/P6) binary image I/O.
#include "chromaforge/image.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace chromaforge {

Image::Image(int width, int height, int channels)
    : width_(width), height_(height), channels_(channels) {
    if (width <= 0 || height <= 0 || (channels != 1 && channels != 3)) {
        throw std::invalid_argument("Image: invalid dimensions or channel count");
    }
    data_.assign(static_cast<size_t>(width) * height * channels, 0);
}

namespace {

// Read one whitespace-delimited unsigned integer from a PNM header, skipping
// '#' comment lines. Consumes exactly one delimiter after the number, so after
// the maxval token the stream sits on the first pixel byte.
int readHeaderInt(std::istream& in) {
    int c = in.get();
    for (;;) {
        if (c == EOF) throw std::runtime_error("PNM: unexpected EOF in header");
        if (c == '#') {
            while (c != '\n' && c != EOF) c = in.get();
            c = in.get();
            continue;
        }
        if (!std::isspace(c)) break;
        c = in.get();
    }
    if (!std::isdigit(c)) throw std::runtime_error("PNM: expected an integer in header");
    int value = 0;
    while (std::isdigit(c)) {
        value = value * 10 + (c - '0');
        c = in.get();
    }
    return value;
}

}  // namespace

Image Image::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open image: " + path);

    const int magic0 = in.get();
    const int magic1 = in.get();
    if (magic0 != 'P' || (magic1 != '6' && magic1 != '5')) {
        throw std::runtime_error("unsupported image (need binary P5/P6): " + path);
    }
    const int channels = (magic1 == '6') ? 3 : 1;
    const int width = readHeaderInt(in);
    const int height = readHeaderInt(in);
    const int maxval = readHeaderInt(in);
    if (maxval != 255) throw std::runtime_error("only 8-bit PNM (maxval 255) is supported");

    Image img(width, height, channels);
    in.read(reinterpret_cast<char*>(img.data()),
            static_cast<std::streamsize>(img.byteCount()));
    if (in.gcount() != static_cast<std::streamsize>(img.byteCount())) {
        throw std::runtime_error("PNM: truncated pixel data in " + path);
    }
    return img;
}

void Image::save(const std::string& path) const {
    if (empty()) throw std::runtime_error("cannot save an empty image");
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write image: " + path);

    out << (channels_ == 3 ? "P6" : "P5") << '\n'
        << width_ << ' ' << height_ << '\n'
        << "255\n";
    out.write(reinterpret_cast<const char*>(data()),
              static_cast<std::streamsize>(byteCount()));
    if (!out) throw std::runtime_error("failed while writing " + path);
}

}  // namespace chromaforge
