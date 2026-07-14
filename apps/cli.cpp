// chromaforge/apps/cli.cpp — command-line front-end for the engine.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "chromaforge/color.hpp"
#include "chromaforge/filters.hpp"
#include "chromaforge/image.hpp"
#include "chromaforge/lut.hpp"
#include "chromaforge/thread_pool.hpp"

using namespace chromaforge;

static void usage() {
    std::printf(
        "chromaforge - multithreaded color & image processing\n"
        "usage:\n"
        "  cf <in.ppm> <out.ppm> [ops...]\n"
        "  cf --gradient <out.ppm> <W> <H>\n"
        "ops (applied left to right):\n"
        "  --exposure <stops>                 --gamma <g>\n"
        "  --saturation <a>                   --white-balance <temp> <tint>\n"
        "  --cdl <lift> <gamma> <gain>        --lut <file.cube>\n"
        "  --tonemap <reinhard|aces> <stops>  --blur <sigma>\n"
        "  --sharpen <amount> <sigma>         --sobel\n"
        "  --threads <N>\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 1;
    }

    if (std::strcmp(argv[1], "--gradient") == 0) {
        if (argc < 5) {
            usage();
            return 1;
        }
        const int w = std::atoi(argv[3]);
        const int h = std::atoi(argv[4]);
        Image img(w, h, 3);
        for (int y = 0; y < h; ++y) {
            uint8_t* p = img.row(y);
            for (int x = 0; x < w; ++x) {
                p[x * 3 + 0] = static_cast<uint8_t>((x * 255) / (w > 1 ? w - 1 : 1));
                p[x * 3 + 1] = static_cast<uint8_t>((y * 255) / (h > 1 ? h - 1 : 1));
                p[x * 3 + 2] = 128;
            }
        }
        img.save(argv[2]);
        std::printf("wrote %dx%d gradient to %s\n", w, h, argv[2]);
        return 0;
    }

    const std::string inPath = argv[1];
    const std::string outPath = argv[2];

    int threads = 0;
    for (int i = 3; i < argc; ++i)
        if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = std::atoi(argv[i + 1]);

    try {
        Image img = Image::load(inPath);
        ThreadPool pool(threads < 0 ? 0 : static_cast<size_t>(threads));

        for (int i = 3; i < argc; ++i) {
            const std::string op = argv[i];
            auto need = [&](int n) {
                if (i + n >= argc) throw std::runtime_error("missing argument(s) for " + op);
            };
            if (op == "--exposure") {
                need(1);
                color::exposure(img, pool, std::stof(argv[++i]));
            } else if (op == "--gamma") {
                need(1);
                color::gamma(img, pool, std::stof(argv[++i]));
            } else if (op == "--saturation") {
                need(1);
                color::saturation(img, pool, std::stof(argv[++i]));
            } else if (op == "--white-balance") {
                need(2);
                const float t = std::stof(argv[++i]);
                const float ti = std::stof(argv[++i]);
                color::whiteBalance(img, pool, t, ti);
            } else if (op == "--cdl") {
                need(3);
                const float l = std::stof(argv[++i]);
                const float g = std::stof(argv[++i]);
                const float ga = std::stof(argv[++i]);
                const float lift[3] = {l, l, l}, gam[3] = {g, g, g}, gain[3] = {ga, ga, ga};
                color::liftGammaGain(img, pool, lift, gam, gain);
            } else if (op == "--lut") {
                need(1);
                Lut3D::fromCubeFile(argv[++i]).apply(img, pool);
            } else if (op == "--tonemap") {
                need(2);
                const std::string m = argv[++i];
                const float s = std::stof(argv[++i]);
                if (m == "aces")
                    color::toneMapACES(img, pool, s);
                else
                    color::toneMapReinhard(img, pool, s);
            } else if (op == "--blur") {
                need(1);
                img = filters::gaussianBlur(img, pool, std::stof(argv[++i]));
            } else if (op == "--sharpen") {
                need(2);
                const float a = std::stof(argv[++i]);
                const float s = std::stof(argv[++i]);
                img = filters::sharpen(img, pool, a, s);
            } else if (op == "--sobel") {
                img = filters::sobelEdges(img, pool);
            } else if (op == "--threads") {
                ++i;  // value already parsed above
            } else {
                throw std::runtime_error("unknown option: " + op);
            }
        }

        img.save(outPath);
        std::printf("wrote %s (%dx%d, %d ch)\n", outPath.c_str(), img.width(), img.height(),
                    img.channels());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
