// compiler_impl - the operand limits.
//
// Interning names and constants into a frame's pools, and the
// diagnostics for running out of either. `operand_limit` and `jump_limit` are
// static constexpr and stay in the header.
//
// One of the files carved out of a 3,845-line compile.cpp on 2026-08-09.
// The class is declared whole in compiler_impl.hpp beside this.

#include "compiler_impl.hpp"

namespace ctbrowser::script::detail {

std::string compiler_impl::frame_name(std::size_t index) const {
    const std::string & name = out_.functions[index].name;
    return "`" + (name.empty() ? std::string{"<anonymous>"} : name) + "`";
}

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
    if (index.empty()) { // crossing the threshold: catch the index up
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
