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

std::string compiler_impl::member_key(std::string_view text) {
    if (!text.starts_with('#')) { return std::string{text}; }
    std::string key = std::string{private_key_prefix} + std::string{text};
    for (std::size_t i = private_scopes_.size(); i-- > 0;) {
        const private_scope & scope = private_scopes_[i];
        if (std::find(scope.names.begin(), scope.names.end(), text) != scope.names.end()) {
            key += ':' + std::to_string(scope.klass);
            break;
        }
    }
    return key;
}

} // namespace ctbrowser::script::detail
