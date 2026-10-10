//===- State.cpp - Modeled provider Map contents ----------------*- C++ -*-===//
//
// Implements providerState. Keys compare by SameValueZero, and every
// operation charges the caller's step budget before doing its work.
//
//===----------------------------------------------------------------------===//

#include "State.h"

#include "llvm/ADT/DenseSet.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace ctcompile::ctnative::host_detail {
namespace {
/// True when `value` is a known primitive literal.
bool primitive(providerValue value) {
    return value.kind == providerValue::Kind::primitive && value.id == 0 && value.literal &&
           ctjs::isPrimitiveAttr(value.literal);
}

/// True when `value` can be a Map key: a primitive or an object.
bool keyIsKnown(providerValue value) {
    return primitive(value) || (value.kind == providerValue::Kind::object && !value.literal);
}

/// Sets `same` to whether two keys are equal under SameValueZero. False when
/// out of budget.
bool sameKey(providerValue left, providerValue right, bool & same, providerState::Spend spend) {
    if (!spend(1)) { return false; }
    if (left.kind != right.kind) {
        same = false;
    } else if (left.kind == providerValue::Kind::object) {
        same = left.id == right.id;
    } else if (auto a = llvm::dyn_cast<ctjs::NumberAttr>(left.literal)) {
        auto b = llvm::dyn_cast<ctjs::NumberAttr>(right.literal);
        if (!b) {
            same = false;
        } else {
            same = ctjs::sameValueZero(a.getDouble(), b.getDouble());
        }
    } else if (auto a = llvm::dyn_cast<ctjs::StringAttr>(left.literal)) {
        auto b = llvm::dyn_cast<ctjs::StringAttr>(right.literal);
        if (!b || a.getValue().size() != b.getValue().size()) {
            same = false;
        } else if (a == b) {
            same = true;
        } else {
            // Equal literals from one MLIRContext are the same attribute.
            // For attributes from different contexts, compare the text, but
            // charge its length first.
            const auto length = a.getValue().size();
            if (length > std::numeric_limits<unsigned>::max() ||
                !spend(static_cast<unsigned>(length))) {
                return false;
            }
            same = a.getValue() == b.getValue();
        }
    } else if (auto a = llvm::dyn_cast<ctjs::BooleanAttr>(left.literal)) {
        auto b = llvm::dyn_cast<ctjs::BooleanAttr>(right.literal);
        same = b && a.getValue() == b.getValue();
    } else {
        same =
            (llvm::isa<ctjs::UndefinedAttr>(left.literal) &&
             llvm::isa<ctjs::UndefinedAttr>(right.literal)) ||
            (llvm::isa<ctjs::NullAttr>(left.literal) && llvm::isa<ctjs::NullAttr>(right.literal));
    }
    return true;
}

/// `key` with -0 replaced by +0, as Map.prototype.set does. A double whose
/// bits other than the sign bit are all zero is +0 or -0.
providerValue canonicalKey(providerValue key) {
    if (key.kind == providerValue::Kind::primitive) {
        if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(key.literal);
            number && (number.getBits() & UINT64_C(0x7fffffffffffffff)) == 0) {
            key.literal = ctjs::NumberAttr::get(key.literal.getContext(), uint64_t{0});
        }
    }
    return key;
}

/// Sets `index` to the position of `key` in `map`, or to the entry count when
/// absent. False when out of budget.
bool find(const providerMap & map, providerValue key, unsigned & index,
          providerState::Spend spend) {
    unsigned candidate = 0;
    for (const auto & entry : map.entries) {
        bool same = false;
        if (!sameKey(entry.key, key, same, spend)) { return false; }
        if (same) {
            index = candidate;
            return true;
        }
        ++candidate;
    }
    index = candidate;
    return true;
}
} // namespace

const providerMap * providerState::get(unsigned mapId) const {
    return mapId != 0 && mapId <= state.size() ? &state[mapId - 1] : nullptr;
}

bool providerState::acceptsValue(providerValue value) const {
    return keyIsKnown(value) ||
           (value.kind == providerValue::Kind::map && !value.literal && get(value.id));
}

bool providerState::cloneTo(providerState & out, Spend spend) const {
    if (!spend(1)) { return false; }
    // Charge the whole copy before allocating its storage. Maps and entry
    // lists have zero inline capacity, so moving them never copies entries.
    for (const auto & map : state) {
        if (!spend(1) || !spend(static_cast<unsigned>(map.entries.size()))) { return false; }
    }
    providerState copy;
    copy.state.reserve(state.size());
    for (const auto & map : state) {
        providerMap copied;
        copied.provenance = map.provenance;
        copied.entries.append(map.entries.begin(), map.entries.end());
        copy.state.push_back(std::move(copied));
    }
    out = std::move(copy);
    return true;
}

std::optional<unsigned> providerState::create(providerMapProvenance provenance, Spend spend) {
    if (!spend(1) || !provenance.allocation || !provenance.invocation || provenance.factory == 0 ||
        state.size() >= std::numeric_limits<unsigned>::max()) {
        return std::nullopt;
    }
    unsigned work = 1;
    if (state.size() == state.capacity()) { work += static_cast<unsigned>(state.size()); }
    if (!spend(work)) { return std::nullopt; }
    state.push_back({provenance, {}});
    return static_cast<unsigned>(state.size());
}

std::optional<std::optional<providerValue>> providerState::lookup(unsigned mapId, providerValue key,
                                                                  Spend spend) const {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !keyIsKnown(key)) { return std::nullopt; }
    unsigned index = 0;
    if (!find(*map, key, index, spend)) { return std::nullopt; }
    if (index == map->entries.size()) { return std::optional<providerValue>{}; }
    return std::optional{map->entries[index].value};
}

/// Sets `found` to whether Map `target` is reachable from Map `from` through
/// entry values. False when out of budget or on a dangling id.
bool providerState::reaches(unsigned from, unsigned target, bool & found, Spend spend) const {
    if (!spend(1)) { return false; }
    llvm::SmallVector<unsigned> pending{from};
    llvm::DenseSet<unsigned> seen;
    while (!pending.empty()) {
        if (!spend(1)) { return false; }
        const unsigned current = pending.pop_back_val();
        if (current == target) {
            found = true;
            return true;
        }
        if (!seen.insert(current).second) { continue; }
        const auto * map = get(current);
        if (!map) { return false; }
        for (const auto & entry : map->entries) {
            if (!spend(1)) { return false; }
            if (entry.value.kind == providerValue::Kind::map) { pending.push_back(entry.value.id); }
        }
    }
    found = false;
    return true;
}

bool providerState::set(unsigned mapId, providerValue key, providerValue value, Spend spend) {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !keyIsKnown(key) || !acceptsValue(value)) { return false; }
    if (value.kind == providerValue::Kind::map) {
        bool cycle = false;
        if (!reaches(value.id, mapId, cycle, spend) || cycle) { return false; }
    }
    unsigned index = 0;
    if (!find(*map, key, index, spend)) { return false; }
    auto & entries = state[mapId - 1].entries;
    if (index != entries.size()) {
        if (!spend(1)) { return false; }
        entries[index].value = value;
        return true;
    }
    if (entries.size() >= std::numeric_limits<unsigned>::max()) { return false; }
    unsigned work = 1;
    if (entries.size() == entries.capacity()) { work += static_cast<unsigned>(entries.size()); }
    if (!spend(work)) { return false; }
    entries.push_back({canonicalKey(key), value});
    return true;
}

std::optional<bool> providerState::erase(unsigned mapId, providerValue key, Spend spend) {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !keyIsKnown(key)) { return std::nullopt; }
    unsigned index = 0;
    if (!find(*map, key, index, spend)) { return std::nullopt; }
    auto & entries = state[mapId - 1].entries;
    if (index == entries.size()) { return false; }
    // The erased entry and every shifted entry are charged before mutation.
    if (!spend(static_cast<unsigned>(entries.size() - index))) { return std::nullopt; }
    entries.erase(entries.begin() + index);
    return true;
}

std::optional<unsigned> providerState::size(unsigned mapId, Spend spend) const {
    const auto * map = get(mapId);
    if (!spend(1) || !map) { return std::nullopt; }
    return static_cast<unsigned>(map->entries.size());
}

std::optional<llvm::SmallVector<providerValue, 0>> providerState::keys(unsigned mapId,
                                                                       Spend spend) const {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !spend(static_cast<unsigned>(map->entries.size()))) {
        return std::nullopt;
    }
    // Charge the whole copy before allocating it. With zero inline capacity
    // returning the vector moves its buffer instead of copying the keys again
    // without charge.
    llvm::SmallVector<providerValue, 0> snapshot;
    snapshot.reserve(map->entries.size());
    for (const auto & entry : map->entries) { snapshot.push_back(entry.key); }
    return snapshot;
}

} // namespace ctcompile::ctnative::host_detail
