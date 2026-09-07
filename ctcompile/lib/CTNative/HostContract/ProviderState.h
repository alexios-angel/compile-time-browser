#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"

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
// member identity, receivers, privacy and normal return before committing a
// copy. This helper neither executes source effects nor grants native admission.
class providerState {
public:
    using Spend = llvm::function_ref<bool(unsigned)>;

    providerState() = default;
    providerState(const providerState &) = delete;
    providerState & operator=(const providerState &) = delete;
    providerState(providerState &&) = default;
    providerState & operator=(providerState &&) = default;

    // Every operation leaves its state and output arguments unchanged on
    // failure. The caller must still discard the entire attempted transaction.
    // Copying charges every Map and entry; reads charge each key comparison;
    // writes additionally charge mutation and every traversed resource edge.
    bool cloneTo(providerState & out, Spend spend) const;
    bool create(providerMapProvenance provenance, unsigned & outId, Spend spend);
    bool lookup(unsigned mapId, providerValue key, bool & found, providerValue & outValue,
                Spend spend) const;
    bool set(unsigned mapId, providerValue key, providerValue value, Spend spend);
    bool erase(unsigned mapId, providerValue key, bool & outRemoved, Spend spend);
    bool size(unsigned mapId, unsigned & outSize, Spend spend) const;

    // Replace outKeys with an independently owned, insertion-ordered snapshot.
    // Charge every copied key before allocating; failure leaves outKeys intact.
    // This is not a live Map iterator: the caller separately proves when and
    // how iteration is consumed. Interned literals and object tokens retain
    // their identity and the caller's existing context/heap lifetime rules.
    bool keys(unsigned mapId, llvm::SmallVectorImpl<providerValue> & outKeys, Spend spend) const;

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
