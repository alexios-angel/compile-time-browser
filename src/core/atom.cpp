#include <ctbrowser/core/atom.hpp>

#include <mutex>

namespace ctbrowser {

atom_table::atom_table() {
    storage_.emplace_back(); // id 0 == ""
    index_.emplace(std::string_view{}, 0u);
}

atom atom_table::intern(std::string_view text) {
    {
        const std::shared_lock read{mutex_};
        if (const auto it = index_.find(text); it != index_.end()) { return atom{it->second}; }
    }
    const std::unique_lock write{mutex_};
    // re-check: another thread may have interned it between the two locks
    if (const auto it = index_.find(text); it != index_.end()) { return atom{it->second}; }
    const auto id = static_cast<std::uint32_t>(storage_.size());
    // The view must key off the STORED copy, not the caller's argument,
    // which may dangle the moment intern() returns.
    const std::string & stored = storage_.emplace_back(text);
    index_.emplace(std::string_view{stored}, id);
    return atom{id};
}

atom atom_table::intern_lower(std::string_view text) {
    std::string folded;
    folded.reserve(text.size());
    for (const char c : text) {
        folded.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return intern(folded);
}

std::string_view atom_table::text(atom a) const {
    const std::shared_lock read{mutex_};
    return a.id < storage_.size() ? std::string_view{storage_[a.id]} : std::string_view{};
}

std::size_t atom_table::size() const {
    const std::shared_lock read{mutex_};
    return storage_.size();
}

} // namespace ctbrowser
