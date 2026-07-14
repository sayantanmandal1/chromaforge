// chromaforge/pipeline.hpp — compose image operations into an ordered pipeline.
#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "chromaforge/image.hpp"

namespace chromaforge {

// A small Strategy/Composite: each stage is an operation on an image, applied in
// insertion order. Filters that produce a new buffer simply reassign the image
// inside their stage lambda.
class Pipeline {
public:
    using Stage = std::function<void(Image&)>;

    Pipeline& add(Stage stage) {
        stages_.push_back(std::move(stage));
        return *this;
    }

    void run(Image& img) const {
        for (const auto& stage : stages_) stage(img);
    }

    size_t size() const { return stages_.size(); }

private:
    std::vector<Stage> stages_;
};

}  // namespace chromaforge
