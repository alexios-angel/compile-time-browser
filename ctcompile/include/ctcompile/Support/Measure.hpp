#pragma once

#include "llvm/Support/MemoryBuffer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace ctcompile {

// A whole file, or empty when it cannot be read.
[[nodiscard]] inline std::string read_file(const std::filesystem::path & path) {
    auto buffer = llvm::MemoryBuffer::getFile(path.string());
    return buffer ? (*buffer)->getBuffer().str() : std::string{};
}

// The median of several runs, not the mean and not the best.
//
// The mean is dragged by one scheduler hiccup and the best is a number the
// machine produced once and will not reproduce - a baseline is for comparing
// against later, so it has to be the number that comes back tomorrow.
template <typename F> [[nodiscard]] double median_ms(int runs, F && once) {
    using clock_type = std::chrono::steady_clock;
    std::vector<double> taken;
    taken.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i) {
        const auto start = clock_type::now();
        once();
        const auto end = clock_type::now();
        taken.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::ranges::sort(taken);
    return taken[taken.size() / 2];
}

} // namespace ctcompile
