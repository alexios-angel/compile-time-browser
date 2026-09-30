#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <optional>

namespace ctcompile::ctnative::host_detail {

// These are already-proved values, not claims read from IR attributes. Object
// tokens belong to the caller's object heap (including token zero); MapIds are
// one-based and belong to this state. A default value is invalid/unknown.
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

struct providerMapProvenance {
    ctjs::ConstructOp allocation;
    ctjs::CallOp invocation;
    unsigned factory = 0;
};

struct providerMap {
    struct entry {
        providerValue key;
        providerValue value;
    };
    providerMapProvenance provenance;
    llvm::SmallVector<entry, 0> entries;
};

// Transaction storage only. The caller proves standard Map construction,
// member identity, receivers, object-token provenance/contents, privacy and normal return before
// committing a copy. This helper neither executes source effects nor grants native admission.
class providerState {
public:
    using Spend = llvm::function_ref<bool(unsigned)>;

    providerState() = default;
    providerState(const providerState &) = delete;
    providerState & operator=(const providerState &) = delete;
    providerState(providerState &&) = default;
    providerState & operator=(providerState &&) = default;

    // Every operation leaves its state (and cloneTo's `out`) unchanged on
    // failure, reported as false or nullopt. The caller must still discard the
    // entire attempted transaction. Copying charges every Map and entry; reads
    // charge each key comparison; writes additionally charge mutation and every
    // traversed resource edge.
    bool cloneTo(providerState & out, Spend spend) const;
    std::optional<unsigned> create(providerMapProvenance provenance, Spend spend);
    // The stored value, or an inner nullopt for proved absence.
    std::optional<std::optional<providerValue>> lookup(unsigned mapId, providerValue key,
                                                       Spend spend) const;
    bool set(unsigned mapId, providerValue key, providerValue value, Spend spend);
    // Whether an entry was removed.
    std::optional<bool> erase(unsigned mapId, providerValue key, Spend spend);
    std::optional<unsigned> size(unsigned mapId, Spend spend) const;

    // An independently owned, insertion-ordered snapshot. Every copied key is
    // charged before allocating. This is not a live Map iterator: the caller
    // separately proves when and how iteration is consumed. Interned literals
    // and object tokens retain their identity and the caller's existing
    // context/heap lifetime rules.
    std::optional<llvm::SmallVector<providerValue, 0>> keys(unsigned mapId, Spend spend) const;

    // Read-only views expose provenance and insertion order for separately
    // bounded retention checks/reporting. Mutation may invalidate them; they
    // authorize no provider operation.
    [[nodiscard]] const providerMap * get(unsigned mapId) const;
    [[nodiscard]] llvm::ArrayRef<providerMap> maps() const { return state; }

private:
    llvm::SmallVector<providerMap, 0> state;
    bool acceptsValue(providerValue value) const;
    bool reaches(unsigned from, unsigned target, bool & found, Spend spend) const;
};

} // namespace ctcompile::ctnative::host_detail
