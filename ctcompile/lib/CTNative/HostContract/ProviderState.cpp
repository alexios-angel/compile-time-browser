#include "ProviderState.h"

#include "llvm/ADT/DenseSet.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace ctcompile::ctnative::host_detail {
namespace {
bool primitive(providerValue value) {
    return value.kind == providerValue::Kind::primitive && value.id == 0 &&
           llvm::isa_and_nonnull<ctjs::UndefinedAttr, ctjs::NullAttr, ctjs::BooleanAttr,
                                 ctjs::NumberAttr, ctjs::StringAttr>(value.literal);
}

bool keyIsKnown(providerValue value) {
    return primitive(value) || (value.kind == providerValue::Kind::object && !value.literal);
}

bool isNaN(uint64_t bits) {
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != 0;
}

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
            const uint64_t aBits = a.getBits(), bBits = b.getBits();
            const uint64_t magnitude = UINT64_C(0x7fffffffffffffff);
            same = aBits == bBits || ((aBits & magnitude) == 0 && (bBits & magnitude) == 0) ||
                   (isNaN(aBits) && isNaN(bBits));
        }
    } else if (auto a = llvm::dyn_cast<ctjs::StringAttr>(left.literal)) {
        auto b = llvm::dyn_cast<ctjs::StringAttr>(right.literal);
        if (!b || a.getValue().size() != b.getValue().size()) {
            same = false;
        } else if (a == b) {
            same = true;
        } else {
            // Normally equal literals are interned in the same context. Keep
            // the helper correct for distinct contexts without an uncharged
            // comparison of arbitrarily long strings.
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

providerValue canonicalKey(providerValue key) {
    if (key.kind == providerValue::Kind::primitive) {
        if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(key.literal);
            number && (number.getBits() & UINT64_C(0x7fffffffffffffff)) == 0) {
            key.literal = ctjs::NumberAttr::get(key.literal.getContext(), uint64_t{0});
        }
    }
    return key;
}

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
    // Charge the whole copy before allocating its storage. Map records and
    // entries have zero inline capacity so moves never copy an entry payload.
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

bool providerState::create(providerMapProvenance provenance, unsigned & outId, Spend spend) {
    if (!spend(1) || !provenance.allocation || !provenance.invocation || provenance.factory == 0 ||
        state.size() >= std::numeric_limits<unsigned>::max()) {
        return false;
    }
    unsigned work = 1;
    if (state.size() == state.capacity()) { work += static_cast<unsigned>(state.size()); }
    if (!spend(work)) { return false; }
    state.push_back({provenance, {}});
    outId = static_cast<unsigned>(state.size());
    return true;
}

bool providerState::lookup(unsigned mapId, providerValue key, bool & found,
                           providerValue & outValue, Spend spend) const {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !keyIsKnown(key)) { return false; }
    unsigned index = 0;
    if (!find(*map, key, index, spend)) { return false; }
    found = index != map->entries.size();
    outValue = found ? map->entries[index].value : providerValue{};
    return true;
}

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

bool providerState::erase(unsigned mapId, providerValue key, bool & outRemoved, Spend spend) {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !keyIsKnown(key)) { return false; }
    unsigned index = 0;
    if (!find(*map, key, index, spend)) { return false; }
    auto & entries = state[mapId - 1].entries;
    if (index == entries.size()) {
        outRemoved = false;
        return true;
    }
    // The erased entry and every shifted entry are charged before mutation.
    if (!spend(static_cast<unsigned>(entries.size() - index))) { return false; }
    entries.erase(entries.begin() + index);
    outRemoved = true;
    return true;
}

bool providerState::size(unsigned mapId, unsigned & outSize, Spend spend) const {
    const auto * map = get(mapId);
    if (!spend(1) || !map) { return false; }
    outSize = static_cast<unsigned>(map->entries.size());
    return true;
}

bool providerState::keys(unsigned mapId, llvm::SmallVectorImpl<providerValue> & outKeys,
                         Spend spend) const {
    const auto * map = get(mapId);
    if (!spend(1) || !map || !spend(static_cast<unsigned>(map->entries.size()))) { return false; }
    // Precharge the entire traversal/copy before allocating its storage. Zero
    // inline capacity makes publication a storage transfer, without another
    // uncharged key copy, even when the caller's vector has inline storage.
    llvm::SmallVector<providerValue, 0> snapshot;
    snapshot.reserve(map->entries.size());
    for (const auto & entry : map->entries) { snapshot.push_back(entry.key); }
    outKeys = std::move(snapshot);
    return true;
}

} // namespace ctcompile::ctnative::host_detail
