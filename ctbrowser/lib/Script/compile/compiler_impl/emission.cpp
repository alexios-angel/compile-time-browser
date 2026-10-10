//===- emission.cpp - Completion and member keys ----------------*- C++ -*-===//
//
// Small compiler_impl members used while emitting bytecode: tracking a
// script's completion value and turning a member name into its property key.
//
//===----------------------------------------------------------------------===//
#include "../compiler_impl.hpp"

namespace ctbrowser::script::detail {

bool compiler_impl::tracking_completion() const {
    return completion_reg_ >= 0 && frames_.size() == 1 && completion_suspended_ == 0;
}

void compiler_impl::clear_completion() {
    if (tracking_completion()) {
        proto().emit(instruction{op::load_undef, static_cast<std::uint16_t>(completion_reg_)});
    }
}

std::uint16_t compiler_impl::member_operand(std::string_view text) {
    return name_operand(member_key(text));
}

// A private name (`#x`) is keyed by the private-key prefix plus the class
// that declares it, so two classes' `#x` never collide. Inside a direct eval
// the caller's classes arrive with their keys already made (scope.keys).
std::string compiler_impl::member_key(std::string_view text) {
    if (!text.starts_with('#')) { return std::string{text}; }
    std::string key = std::string{private_key_prefix} + std::string{text};
    for (std::size_t i = private_scopes_.size(); i-- > 0;) {
        const private_scope & scope = private_scopes_[i];
        const auto found = std::find(scope.names.begin(), scope.names.end(), text);
        if (found == scope.names.end()) { continue; }
        if (!scope.keys.empty()) {
            return std::string{scope.keys[static_cast<std::size_t>(found - scope.names.begin())]};
        }
        key += ':' + std::to_string(scope.klass);
        break;
    }
    return key;
}

} // namespace ctbrowser::script::detail
