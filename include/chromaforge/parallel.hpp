// chromaforge/parallel.hpp — data-parallel loop over an index range.
#pragma once

#include <algorithm>
#include <future>
#include <vector>

#include "chromaforge/thread_pool.hpp"

namespace chromaforge {

// Invoke func(i) for every i in [begin, end), split into one contiguous chunk
// per worker and executed on the pool. Blocks until all chunks finish.
// `func` must be safe to call concurrently for distinct indices — image filters
// satisfy this because each call writes to its own output row.
template <class Func>
void parallelFor(ThreadPool& pool, int begin, int end, const Func& func) {
    if (end <= begin) return;
    const int total = end - begin;
    const int workers = static_cast<int>(std::max<size_t>(1, pool.size()));
    const int chunk = std::max(1, (total + workers - 1) / workers);

    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>((total + chunk - 1) / chunk));
    for (int start = begin; start < end; start += chunk) {
        const int stop = std::min(end, start + chunk);
        futures.push_back(pool.submit([start, stop, &func] {
            for (int i = start; i < stop; ++i) func(i);
        }));
    }
    for (auto& f : futures) f.get();
}

}  // namespace chromaforge
