#pragma once

#include "../../Analysis/NativeMap/SnapshotCopies.h"
#include "../../Analysis/PrimitiveMapKey.h"
#include "../Analysis.h"

namespace ctcompile::ctnative::host_detail::captured_map_detail {

struct field_fact {
    mlir::Value object;
    llvm::StringRef key;
    PrimitiveAlternatives payload;
};

struct size_fact {
    unsigned lower = 0;
    std::optional<unsigned> exact;
};

// Borrows the active proof's state; all referenced facts and callbacks outlive the walk.
struct Walk {
    analyzer & owner;
    mlir::Block & body;
    const bool & prepared;
    const bool & primitiveContents;
    const HostMethodParameters & parameters;
    HostCapturedMap & result;
    CapturedMapInvocation *& invocation;
    llvm::DenseSet<mlir::Value> & primitives;
    llvm::DenseSet<mlir::Value> & flags;
    llvm::DenseSet<mlir::Value> & leafValues;
    llvm::DenseSet<mlir::Value> & snapshotElements;
    llvm::DenseSet<mlir::Value> & stringSnapshotElements;
    llvm::DenseSet<mlir::Operation *> & concatenations;
    llvm::DenseSet<mlir::Operation *> & snapshotReads;
    llvm::DenseSet<mlir::Operation *> & callbackReads;
    llvm::DenseSet<mlir::Operation *> & callbackCalls;
    llvm::DenseSet<mlir::Operation *> & constructors;
    llvm::DenseSet<mlir::Operation *> & allocations;
    llvm::DenseSet<mlir::Operation *> & mapStores;
    llvm::DenseSet<mlir::Operation *> & objectWrites;
    llvm::DenseSet<mlir::Operation *> & objectReads;
    llvm::DenseSet<mlir::Operation *> & objectStores;
    llvm::DenseSet<mlir::Operation *> & identities;
    llvm::DenseSet<mlir::Operation *> & reads;
    llvm::DenseSet<mlir::Operation *> & calls;
    llvm::DenseSet<mlir::Operation *> & upvalues;
    llvm::DenseSet<mlir::Operation *> & frameUses;
    std::optional<map_detail::snapshotCopies> & copies;
    llvm::DenseMap<mlir::Value, CapturedMapOrigin> & maps;
    const CapturedMapOrigin & capturedOrigin;
    llvm::DenseMap<mlir::Value, mlir::Value> & exactLeaves;
    llvm::DenseMap<mlir::Value, mlir::Value> & objects;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> & alternatives;
    llvm::SmallVector<field_fact> & fields;
    llvm::DenseMap<CapturedMapOrigin, CapturedMapState> & mapStates;
    llvm::DenseMap<mlir::Value, unsigned> & sizeBounds;
    llvm::DenseMap<mlir::Value, unsigned> & exactSizes;
    ctjs::ReturnOp & returned;
    ctjs::FrameEnterOp & frame;
    bool & frameExited;
    llvm::function_ref<bool()> prepareSnapshots;
    llvm::function_ref<bool(const llvm::DenseMap<CapturedMapOrigin, CapturedMapState> &)>
        chargeStates;
    llvm::function_ref<PrimitiveMapKeyEvidence(mlir::Value)> keyEvidence;
    llvm::function_ref<std::optional<size_fact>(const CapturedMapState &)> sizeFacts;
    llvm::function_ref<bool(mlir::Value, bool)> learn;
    llvm::function_ref<bool(CapturedMapOrigin)> invalidateAliases;
    llvm::function_ref<std::optional<bool>(mlir::Value)> knownTruth;
    llvm::function_ref<PrimitiveMapKeyRelation(mlir::Value, mlir::Value, PrimitiveMapKeyEvidence,
                                               PrimitiveMapKeyEvidence)>
        keyRelationImpl;
    llvm::function_ref<std::optional<bool>(mlir::Value, llvm::ArrayRef<CapturedMapEntry>, bool,
                                           llvm::ArrayRef<mlir::Value>, bool)>
        absentImpl;
    llvm::function_ref<bool(CapturedMapState &, mlir::Value, bool, PrimitiveAlternatives,
                            mlir::Value, CapturedMapOrigin)>
        mutateImpl;
    using entry_fact = CapturedMapEntry;

    PrimitiveMapKeyRelation keyRelation(mlir::Value left, mlir::Value right,
                                        PrimitiveMapKeyEvidence leftEvidence = {},
                                        PrimitiveMapKeyEvidence rightEvidence = {}) {
        return keyRelationImpl(left, right, leftEvidence, rightEvidence);
    }
    std::optional<bool> absent(mlir::Value key, llvm::ArrayRef<CapturedMapEntry> facts,
                               bool complete, llvm::ArrayRef<mlir::Value> possible,
                               bool currentPath = true) {
        return absentImpl(key, facts, complete, possible, currentPath);
    }
    bool mutate(CapturedMapState & state, mlir::Value key, bool erase,
                PrimitiveAlternatives payload = {}, mlir::Value object = {},
                CapturedMapOrigin child = {}) {
        return mutateImpl(state, key, erase, payload, object, child);
    }
    bool walk(mlir::Block & block, unsigned depth);
};

} // namespace ctcompile::ctnative::host_detail::captured_map_detail
