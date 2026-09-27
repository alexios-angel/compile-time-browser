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

// One captured-Map method body proof (analyzer::capturedMapBody): its inputs,
// the facts gathered while walking the body, and the final use census.
struct Walk {
    analyzer & owner;
    ctjs::FuncOp function;
    bool prepared;
    bool primitiveContents;
    const HostMethodParameters & parameters;
    HostCapturedMap & result;
    PrimitiveAlternatives & returnAlternatives;
    CapturedMapInvocation * invocation;
    mlir::Block & body;
    const std::size_t firstRead = result.reads.size();
    const std::size_t firstUpvalue = result.upvalues.size();
    const CapturedMapOrigin capturedOrigin{
        invocation ? invocation->root : body.getArgument(prepared ? 3 : 2), nullptr};

    llvm::DenseSet<mlir::Value> primitives, flags;
    // Closed scalar/leaf alternatives permit non-coercing observations, never
    // an own-field receiver or a primitive category without separate evidence.
    llvm::DenseSet<mlir::Value> leafValues;
    llvm::DenseSet<mlir::Value> snapshotElements;
    llvm::DenseSet<mlir::Value> stringSnapshotElements;
    llvm::DenseSet<mlir::Operation *> concatenations;
    llvm::DenseSet<mlir::Operation *> snapshotReads;
    llvm::DenseSet<mlir::Operation *> callbackReads, callbackCalls;
    std::optional<map_detail::snapshotCopies> copies;
    llvm::DenseMap<mlir::Value, CapturedMapOrigin> maps;
    llvm::DenseMap<mlir::Value, mlir::Value> exactLeaves;
    llvm::DenseSet<mlir::Operation *> constructors, allocations, mapStores;
    // A saved read names its allocation, not the current Map entry. These SSA
    // origins never change when that entry is overwritten or deleted.
    llvm::DenseMap<mlir::Value, mlir::Value> objects;
    llvm::DenseSet<mlir::Operation *> objectWrites, objectReads, objectStores, identities;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> alternatives;
    llvm::SmallVector<field_fact> fields;
    // Capture loads and each receiver's fluent returns preserve its origin.
    // Captured contents start unknown each invocation. A set preserves presence
    // even when its key may alias an earlier entry. In that case the payload
    // retains the union of both independently proved primitive alternatives.
    // Presence alone never supplies payload evidence. A possibly aliasing delete
    // removes definite membership, but cannot change a surviving payload. Its
    // alternatives stay valid whenever present; an exact set replaces them.
    // Definite absence is independent of possible absence: an exact deletion
    // or clear establishes it. A possibly aliasing write invalidates absence,
    // whereas another deletion cannot make an absent key present.
    // This local contents fact is independent of the family's return worklist
    // and publishes alternatives only after the entire body/use proof completes.
    llvm::DenseMap<CapturedMapOrigin, CapturedMapState> mapStates;
    // Mutable state belongs to a runtime origin, never a shared C++ schema.
    // Captured contents start unknown on every invocation; a fresh child starts
    // empty. Saved aliases continue to denote that child after outer mutation.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds;
    llvm::DenseMap<mlir::Value, unsigned> exactSizes;
    llvm::DenseSet<mlir::Operation *> reads, calls, upvalues;
    ctjs::ReturnOp returned;
    ctjs::FrameEnterOp frame;
    llvm::DenseSet<mlir::Operation *> frameUses;
    bool frameExited = false;
    using entry_fact = CapturedMapEntry;

    Walk(analyzer & owner, ctjs::FuncOp function, bool prepared, bool primitiveContents,
         const HostMethodParameters & parameters, HostCapturedMap & result,
         PrimitiveAlternatives & returnAlternatives, CapturedMapInvocation * invocation)
        : owner(owner), function(function), prepared(prepared),
          primitiveContents(primitiveContents), parameters(parameters), result(result),
          returnAlternatives(returnAlternatives), invocation(invocation),
          body(function.getBody().front()) {}

    bool run();
    bool walk(mlir::Block & block, unsigned depth);
    bool prepareSnapshots();
    bool chargeStates(const llvm::DenseMap<CapturedMapOrigin, CapturedMapState> & states);
    mlir::Value actualKey(mlir::Value value);
    bool stableKey(mlir::Value key);
    PrimitiveMapKeyRelation keyRelation(mlir::Value left, mlir::Value right,
                                        PrimitiveMapKeyEvidence leftEvidence = {},
                                        PrimitiveMapKeyEvidence rightEvidence = {});
    PrimitiveMapKeyEvidence keyEvidence(mlir::Value key);
    std::optional<size_fact> sizeFacts(const CapturedMapState & state);
    std::optional<bool> absent(mlir::Value key, llvm::ArrayRef<CapturedMapEntry> facts,
                               bool complete, llvm::ArrayRef<mlir::Value> possible,
                               bool currentPath = true);
    bool mutate(CapturedMapState & state, mlir::Value key, bool erase,
                PrimitiveAlternatives payload = {}, mlir::Value object = {},
                CapturedMapOrigin child = {});
    bool learn(mlir::Value condition, bool branch);
    bool invalidateAliases(CapturedMapOrigin origin);
    std::optional<bool> knownTruth(mlir::Value value, unsigned depth = 0);
};

} // namespace ctcompile::ctnative::host_detail::captured_map_detail
