//===- Walk.hpp - Map body walk ---------------------------------*- C++ -*-===//
//
// The state of one captured-Map method body proof (see CapturedMapBody.cpp
// and Walk.cpp).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../Analysis/NativeMap/SnapshotCopies.h"
#include "../../Analysis/PrimitiveMapKey.h"
#include "../Analysis.h"
#include "mlir/Dialect/SCF/IR/SCF.h"

namespace ctcompile::ctnative::host_detail::captured_map_detail {

/// The known payload of one own field of a method-local object.
struct field_fact {
    mlir::Value object;
    llvm::StringRef key;
    PrimitiveAlternatives payload;
};

/// A Map's size at one point: a proved lower bound, and the exact size when
/// known.
struct size_fact {
    unsigned lower = 0;
    std::optional<unsigned> exact;
};

/// One captured-Map method body proof (analyzer::capturedMapBody): its inputs,
/// the facts gathered while walking the body, and the final check of every
/// use.
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

    /// Values proved primitive, and i1 values usable as branch conditions.
    llvm::DenseSet<mlir::Value> primitives, flags;
    /// Caller-supplied leaf objects. They permit non-coercing observations,
    /// never an own-field receiver or a primitive category without separate
    /// evidence.
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
    /// Method-local objects, each mapped to its allocation. A saved read names
    /// its allocation, not the current Map entry, so these origins never change
    /// when that entry is overwritten or deleted.
    llvm::DenseMap<mlir::Value, mlir::Value> objects;
    llvm::DenseSet<mlir::Operation *> objectWrites, objectReads, objectStores, identities;
    llvm::DenseMap<mlir::Value, PrimitiveAlternatives> alternatives;
    llvm::SmallVector<field_fact> fields;
    /// The known contents of each Map origin at the current point of the walk.
    ///
    /// Capture loads and each receiver's fluent `set` returns keep its origin.
    /// Captured contents start unknown each invocation. A `set` keeps presence
    /// even when its key may alias an earlier entry; the payload then holds
    /// the union of both proved primitive alternatives. Presence alone never
    /// supplies payload evidence. A possibly aliasing `delete` removes definite
    /// membership but cannot change a surviving payload, whose alternatives
    /// stay valid whenever present; an exact `set` replaces them.
    /// Definite absence is tracked separately from possible absence: only an
    /// exact deletion or `clear` establishes it. A possibly aliasing write
    /// invalidates absence, whereas another deletion cannot make an absent key
    /// present. These facts are local to this walk and publish alternatives
    /// only after the whole body and use check succeeds.
    llvm::DenseMap<CapturedMapOrigin, CapturedMapState> mapStates;
    /// Read-time `size` facts: a lower bound and, when known, the exact size
    /// each `size` read observed. Mutable state belongs to a runtime origin,
    /// never a shared C++ schema: captured contents start unknown on every
    /// invocation, a fresh child starts empty, and saved aliases continue to
    /// denote that child after outer mutation.
    llvm::DenseMap<mlir::Value, unsigned> sizeBounds;
    llvm::DenseMap<mlir::Value, unsigned> exactSizes;
    llvm::DenseSet<mlir::Operation *> reads, calls, upvalues;
    ctjs::ReturnOp returned;
    ctjs::FrameEnterOp frame;
    llvm::DenseSet<mlir::Operation *> frameUses;
    bool frameExited = false;

    Walk(analyzer & owner, ctjs::FuncOp function, bool prepared, bool primitiveContents,
         const HostMethodParameters & parameters, HostCapturedMap & result,
         PrimitiveAlternatives & returnAlternatives, CapturedMapInvocation * invocation)
        : owner(owner), function(function), prepared(prepared),
          primitiveContents(primitiveContents), parameters(parameters), result(result),
          returnAlternatives(returnAlternatives), invocation(invocation),
          body(function.getBody().front()) {}

    /// Runs the whole proof: walks the body, then checks every use of each
    /// value it admitted. Returns false to refuse.
    bool run();
    bool walk(mlir::Block & block, unsigned depth);
    bool walkIf(mlir::scf::IfOp branch, unsigned depth);
    bool mapCall(ctjs::CallOp invoke);
    bool prepareSnapshots();
    bool chargeStates(const llvm::DenseMap<CapturedMapOrigin, CapturedMapState> & states);
    mlir::Value actualKey(mlir::Value value);
    bool stableKey(mlir::Value key);
    PrimitiveMapKeyRelation keyRelation(mlir::Value left, mlir::Value right,
                                        PrimitiveMapKeyEvidence leftEvidence = {},
                                        PrimitiveMapKeyEvidence rightEvidence = {});
    PrimitiveMapKeyEvidence keyEvidence(mlir::Value key);
    /// keyRelation with both keys' current-path evidence.
    PrimitiveMapKeyRelation pathRelation(mlir::Value left, mlir::Value right) {
        return keyRelation(left, right, keyEvidence(left), keyEvidence(right));
    }
    /// Records a primitive SSA value with its first-recorded alternatives.
    void scalar(mlir::Value value, PrimitiveAlternatives fact) {
        primitives.insert(value);
        alternatives.try_emplace(value, fact);
    }
    template <class Tag> void scalar(mlir::Value value) {
        scalar(value, PrimitiveAlternatives::forTag(mlir::TypeID::get<Tag>()));
    }
    /// Whether every use of `value` is dominated by it and `allowed`; each use
    /// costs a step.

    bool closedUses(mlir::Value value, llvm::function_ref<bool(mlir::OpOperand &)> allowed);
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
