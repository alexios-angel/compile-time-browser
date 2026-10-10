//===- ctbrowser/lib/Script/compile/limits.cpp - Operand limits -----------===//
//
// compiler_impl operand limits: interning names and constants into a frame's
// pools, and the diagnostics for running out of either. `operand_limit` and
// `jump_limit` are static constexpr in compiler_impl.hpp, where the class is
// declared.
//
//===----------------------------------------------------------------------===//

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

std::string compiler_impl::frame_name(std::size_t index) const {
    const std::string & name = out_.functions[index].name;
    return "`" + (name.empty() ? std::string{"<anonymous>"} : name) + "`";
}

/// Interns `text` into `pool` and returns its index. Uses a linear scan while
/// the pool has fewer than small_pool entries, then builds and uses `index`.
std::uint32_t compiler_impl::intern_into(std::vector<std::string> & pool,
                                         flat_map<std::string, std::uint32_t> & index,
                                         std::string text) {
    if (pool.size() < small_pool) {
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (pool[i] == text) { return static_cast<std::uint32_t>(i); }
        }
        pool.push_back(std::move(text));
        return static_cast<std::uint32_t>(pool.size() - 1);
    }
    if (index.empty()) { // Crossing the threshold: catch the index up.
        for (std::size_t i = 0; i < pool.size(); ++i) {
            index.emplace(pool[i], static_cast<std::uint32_t>(i));
        }
    }
    if (const auto it = index.find(text); it != index.end()) { return it->second; }
    const auto at = static_cast<std::uint32_t>(pool.size());
    pool.push_back(text);
    index.emplace(std::move(text), at);
    return at;
}

std::uint32_t compiler_impl::intern_name(std::string text) {
    return intern_into(proto().names, fn().name_index, std::move(text));
}

std::uint32_t compiler_impl::intern_string(std::string text) {
    return intern_into(proto().strings, fn().string_index, std::move(text));
}

std::uint16_t compiler_impl::name_operand(std::string text) {
    const std::uint32_t index = intern_name(std::move(text));
    if (index > operand_limit) {
        fail(frame_name(fn().proto) + " mentions more than " + std::to_string(operand_limit + 1) +
             " distinct property names; the operand that selects one holds " +
             std::to_string(operand_limit + 1) + ". Past that it reads a DIFFERENT property.");
    }
    return static_cast<std::uint16_t>(index);
}

void compiler_impl::finish_frame(std::size_t index, std::size_t params) {
    // Every local still in scope ends here. `pop_scope` closes the ones whose
    // block ended; this closes the rest. It runs exactly once per function,
    // script or class body, after the body is compiled, so a body that returned
    // early does not leave its parameters with a live range of length zero.
    // Closing twice writes the same numbers.
    for (const local & each : fn().locals) { close_local(fn(), each); }

    function_proto & fp = out_.functions[index];
    const std::uint32_t wanted = fn().high_water;
    fp.frame_size = static_cast<std::uint16_t>(wanted);
    fp.param_count = static_cast<std::uint16_t>(params);
    if (wanted > operand_limit) {
        fail(frame_name(index) + " needs " + std::to_string(wanted) + " registers; a frame holds " +
             std::to_string(operand_limit + 1) + ". Past that its locals alias each other.");
    }
    if (params > operand_limit) {
        fail(frame_name(index) + " takes " + std::to_string(params) + " parameters; the limit is " +
             std::to_string(operand_limit) + ".");
    }
}

} // namespace ctbrowser::script::detail
