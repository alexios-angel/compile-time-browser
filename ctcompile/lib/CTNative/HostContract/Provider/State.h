//===- State.h - Modeled provider Map contents ------------------*- C++ -*-===//
//
// Declares providerState, the analysis-time model of the Maps a provider
// factory captured, with every operation charged to a step budget.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctnative::host_detail {

/// A Map key or value the analysis has already proved, never one read from
/// IR attributes. Object ids index the caller's object list (zero is a valid
/// object); Map ids are one-based indices into this providerState. A
/// default-constructed value (primitive with no literal) means unknown.
struct providerValue {
    enum class Kind {
        primitive,
        object,
        map
    } kind = Kind::primitive;
    mlir::Attribute literal;
    unsigned id = 0;

    static providerValue constant(mlir::Attribute value) { return {Kind::primitive, value, 0}; }
    static providerValue object(unsigned token) { return {Kind::object, {}, token}; }
    static providerValue map(unsigned mapId) { return {Kind::map, {}, mapId}; }
};

/// Where a Map came from: its `new Map()` operation, the provider call that
/// ran it, and the one-based factory invocation it belongs to.
struct providerMapProvenance {
    ctjs::ConstructOp allocation;
    ctjs::CallOp invocation;
    unsigned factory = 0;
};

/// One modeled Map: its provenance and its entries in insertion order.
struct providerMap {
    struct entry {
        providerValue key;
        providerValue value;
    };
    providerMapProvenance provenance;
    llvm::SmallVector<entry, 0> entries;
};

/// The modeled contents of every provider Map, used as transaction storage:
/// a provider call works on a copy and the copy replaces the original only
/// when the call is fully proved. The caller proves everything else (standard
/// Map construction, method identity, receivers, object contents, privacy,
/// normal return); this class only stores entries and charges the budget.
class providerState {
public:
    using Spend = llvm::function_ref<bool(unsigned)>;

    providerState() = default;
    providerState(const providerState &) = delete;
    providerState & operator=(const providerState &) = delete;
    providerState(providerState &&) = default;
    providerState & operator=(providerState &&) = default;

    // Every operation leaves its state (and cloneTo's `out`) unchanged on
    // failure, reported as false or nullopt; the caller must still discard
    // the whole transaction. Copying charges every Map and entry, reads charge
    // each key comparison, and writes also charge the mutation and every Map
    // edge they traverse.

    /// Replaces `out` with a copy of this state.
    bool cloneTo(providerState & out, Spend spend) const;
    /// Adds an empty Map and returns its one-based id.
    std::optional<unsigned> create(providerMapProvenance provenance, Spend spend);
    /// The stored value, or an inner nullopt when the key is proved absent.
    std::optional<std::optional<providerValue>> lookup(unsigned mapId, providerValue key,
                                                       Spend spend) const;
    /// Inserts or overwrites an entry. Refuses a value that is a Map from
    /// which `mapId` is reachable, since that would make a cycle.
    bool set(unsigned mapId, providerValue key, providerValue value, Spend spend);
    /// Whether an entry was removed.
    std::optional<bool> erase(unsigned mapId, providerValue key, Spend spend);
    /// The number of entries.
    std::optional<unsigned> size(unsigned mapId, Spend spend) const;

    /// A copy of the keys in insertion order, charged before it is allocated.
    /// This is not a live iterator: the caller proves when and how the
    /// iteration is consumed. Literals and object ids keep their identity.
    std::optional<llvm::SmallVector<providerValue, 0>> keys(unsigned mapId, Spend spend) const;

    /// Read-only views of provenance and insertion order, for retention
    /// checks and reports. Any mutation may invalidate them.
    [[nodiscard]] const providerMap * get(unsigned mapId) const;
    [[nodiscard]] llvm::ArrayRef<providerMap> maps() const { return state; }

private:
    llvm::SmallVector<providerMap, 0> state;
    bool acceptsValue(providerValue value) const;
    bool reaches(unsigned from, unsigned target, bool & found, Spend spend) const;
};

} // namespace ctcompile::ctnative::host_detail
