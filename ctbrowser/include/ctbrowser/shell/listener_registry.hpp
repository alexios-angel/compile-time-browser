//===- listener_registry.hpp - Event listener registry ----------*- C++ -*-===//
//
// The addEventListener bookkeeping for one document: which listeners are
// registered on which target, for which type and phase. Dispatch itself (the
// propagation path and invoking callbacks) belongs to the owner, dom_bindings.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ctbrowser::shell {

/// The kind of event target a listener is registered on: an element or other
/// node, the document, the window, or a standalone EventTarget object.
enum class event_target_kind : std::uint8_t {
    node,
    document,
    window,
    object
};

/// A listener's target. A registry belongs to one document; node and
/// standalone-object identities are supplied by its owner, and document and
/// window use identity zero.
struct listener_target {
    event_target_kind kind = event_target_kind::node;
    std::uint64_t identity = 0;

    bool operator==(const listener_target &) const = default;
};

/// Registration and phase membership only: the owner chooses the propagation
/// path, invokes callbacks and retains any executing payloads. \p Payload may
/// own native state, including unique_ptrs; no callback representation is
/// required.
template <class Payload> class listener_registry {
public:
    /// One registered listener.
    struct entry {
        listener_target target;
        std::string type;
        std::uint64_t callback_id = 0;
        bool capture = false;
        bool once = false;
        bool passive = false;
        bool spent = false;
        /// An event handler property (`onclick`) rather than an
        /// addEventListener call. Its slot keeps its position in the list
        /// while the owner picks the current callback at invocation time, and
        /// `once` never spends it.
        bool handler = false;
        Payload payload;
    };

    /// Adds a listener. Returns false, and adds nothing, if \p made is already
    /// spent or an equal live listener (same target, type, capture and
    /// callback) exists, as addEventListener ignores duplicates.
    [[nodiscard]] bool add(entry made) {
        if (made.spent) { return false; }
        for (const auto & current : entries_) {
            if (!current.spent && matches(current, made.target, made.type, made.capture) &&
                current.callback_id == made.callback_id) {
                return false;
            }
        }
        entries_.push_back(std::move(made));
        return true;
    }

    /// The callback ids of the live listeners for one target, type and phase,
    /// in registration order, taken before dispatch starts. Ids, not entries:
    /// removing and re-adding the same callback before its turn selects the
    /// new entry.
    [[nodiscard]] std::vector<std::uint64_t> snapshot(listener_target target, std::string_view type,
                                                      bool capture) const {
        std::vector<std::uint64_t> result;
        for (const auto & current : entries_) {
            if (!current.spent && matches(current, target, type, capture)) {
                result.push_back(current.callback_id);
            }
        }
        return result;
    }

    /// Looks up a snapshotted listener just before invoking it: null if it was
    /// removed since the snapshot, otherwise its entry, marked spent first when
    /// it is a `once` listener.
    ///
    /// The returned row, like entries(), is invalidated by add, remove and
    /// reap, so copy or borrow stable payload state before reentrant work. The
    /// owner must delay reaping spent payloads until its outermost dispatch
    /// returns, and retain executing payloads if an explicit removal can
    /// destroy them.
    [[nodiscard]] const entry * begin(listener_target target, std::string_view type,
                                      std::uint64_t callback_id, bool capture) {
        for (auto & current : entries_) {
            if (!current.spent && current.callback_id == callback_id &&
                matches(current, target, type, capture)) {
                if (current.once && !current.handler) { current.spent = true; }
                return &current;
            }
        }
        return nullptr;
    }

    /// removeEventListener: drops the listener matching all four keys.
    void remove(listener_target target, std::string_view type, std::uint64_t callback_id,
                bool capture) {
        erase_if([&](const entry & current) {
            return current.callback_id == callback_id && matches(current, target, type, capture);
        });
    }

    /// Whether \p target has an event handler property slot for \p type.
    [[nodiscard]] bool has_handler(listener_target target, std::string_view type) const {
        for (const auto & current : entries_) {
            if (current.handler && current.target == target && current.type == type) {
                return true;
            }
        }
        return false;
    }

    /// Drops \p target's event handler property slot for \p type.
    void remove_handler(listener_target target, std::string_view type) {
        erase_if([&](const entry & current) {
            return current.handler && current.target == target && current.type == type;
        });
    }

    /// Erases spent `once` listeners; see begin() for when that is safe.
    void reap_spent() {
        erase_if([](const entry & current) { return current.spent; });
    }

    template <class Predicate> void erase_if(Predicate predicate) {
        std::erase_if(entries_, std::move(predicate));
    }

    /// Every entry in registration order, including spent ones until the owner
    /// reaps them. Any add or erase invalidates this view and references to
    /// its rows.
    [[nodiscard]] std::span<const entry> entries() const noexcept { return entries_; }

private:
    [[nodiscard]] static bool matches(const entry & current, listener_target target,
                                      std::string_view type, bool capture) {
        return current.target == target && current.type == type && current.capture == capture;
    }

    std::vector<entry> entries_;
};

} // namespace ctbrowser::shell
