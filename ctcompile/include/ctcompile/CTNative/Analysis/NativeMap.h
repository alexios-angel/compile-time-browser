//===- NativeMap.h - Standard Map proofs for native code --------*- C++ -*-===//
//
// Proofs that a JavaScript `new Map` is the standard constructor and that
// every use of the instance is a standard method call native code can
// implement directly, plus the attributes prepareNativeMaps() writes to
// record them.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEMAP_H
#define CTCOMPILE_CTNATIVE_ANALYSIS_NATIVEMAP_H

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <cstdint>
#include <optional>

namespace ctcompile::ctnative {

class OwnedGlobalRoots;

/// Attribute names prepareNativeMaps() attaches to proved Map operations.
/// `map_site` marks a `new Map`, `map_action` the proved method call or `size`
/// read, `map_group` the schema family id (see nativeMapGroup()), and
/// `map_reason` why a candidate was refused.
inline constexpr llvm::StringLiteral kNativeMapSite = "ctnative.map_site";
inline constexpr llvm::StringLiteral kNativeMapAction = "ctnative.map_action";
inline constexpr llvm::StringLiteral kNativeMapMethod = "ctnative.map_method";
inline constexpr llvm::StringLiteral kNativeMapConstructor = "ctnative.map_constructor";
inline constexpr llvm::StringLiteral kNativeMapReason = "ctnative.map_reason";
inline constexpr llvm::StringLiteral kNativeMapGroup = "ctnative.map_group";
inline constexpr llvm::StringLiteral kNativeMapArgGroups = "ctnative.map_arg_groups";
inline constexpr llvm::StringLiteral kNativeMapPresent = "ctnative.map_present";
/// The type of the value last written to the same instance and key, proved
/// for this read independently of the family schema.
inline constexpr llvm::StringLiteral kNativeMapReadType = "ctnative.map_read_type";
/// The exact primitive this write stores, even when the SSA value's type is a
/// broader union.
inline constexpr llvm::StringLiteral kNativeMapWriteType = "ctnative.map_write_type";
/// The exact key type at this operation, recomputed independently of the
/// storage schema.
inline constexpr llvm::StringLiteral kNativeMapKeyType = "ctnative.map_key_type";
inline constexpr llvm::StringLiteral kNativeMapSnapshotCopy = "ctnative.map_snapshot_copy";
inline constexpr llvm::StringLiteral kNativeMapSnapshotBuiltin = "ctnative.map_snapshot_builtin";
inline constexpr llvm::StringLiteral kNativeMapRecords = "ctnative.map_records";

/// Record storage confined to the entry frame, recomputed with the standard
/// Map proof. nativeMapRecordPayload() returns a representative payload for
/// the schema; nativeMapRecordOrigin() the exact object a read gets back;
/// isNativeMapRecordStore() is true for a use that stores such a record.
mlir::Value nativeMapRecordPayload(mlir::Value map);
mlir::Value nativeMapRecordOrigin(mlir::Value read);
bool isNativeMapRecordStore(mlir::OpOperand & use);

/// Annotates Map operations, but only after proving both that the constructor
/// and methods are the standard ones and that every use of the instance is
/// supported. No run-time check or boxed fallback is added.
/// `hostTyped` names the operations a complete host proof has typed: none
/// rebinds or inspects the standard Map, writes a prototype or runs script.
/// `hostRecord` names fresh payloads that proof typed as scalar-field records:
/// they select stack-owned record storage like a lifted receiver does.
void prepareNativeMaps(mlir::ModuleOp module, const OwnedGlobalRoots * globals = nullptr,
                       llvm::function_ref<bool(mlir::Operation *)> hostTyped = nullptr,
                       llvm::function_ref<bool(ctjs::CreateObjectOp)> hostRecord = nullptr);

/// The operation performed by a proved call or size read, or empty.
llvm::StringRef nativeMapAction(mlir::Operation * op);

/// True when `action` takes a key. This only checks the name; it proves
/// nothing about a call.
bool nativeMapKeyAction(llvm::StringRef action);

/// The argument count of set/get/has/delete/clear, or nullopt for any other
/// name. `size` is a property; keys/values are left to the callers admitting them.
inline std::optional<unsigned> mapMethodArity(llvm::StringRef name) {
    return llvm::StringSwitch<std::optional<unsigned>>(name)
        .Case("set", 2U)
        .Cases({"get", "has", "delete"}, 1U)
        .Case("clear", 0U)
        .Default(std::nullopt);
}

/// The constant method name a call reads from its callee property, or empty.
inline llvm::StringRef mapMethodName(ctjs::CallOp call) {
    auto get = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    return get ? ctjs::constantKey(get.getKey()) : llvm::StringRef{};
}

/// The proved action of a call that still reads that method from its own
/// receiver (a Map family member) and passes the method's exact argument
/// count; empty otherwise. It rechecks the source edges, which a stale
/// annotation would not.
llvm::StringRef liveNativeMapAction(ctjs::CallOp call);

/// A zero-argument `new Map` whose new.target is its own callee. Lexical only:
/// whether global `Map` is the standard constructor is the caller's proof.
inline bool plainMapConstruction(ctjs::ConstructOp made) {
    auto load = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    return load && load.getName() == "Map" && made.getArgs().empty() &&
           made.getNewTarget() == made.getCallee();
}

/// "bool", "number" or "string" when `alternatives` holds exactly one of those
/// primitive kinds, otherwise empty. Returns JavaScript type names, not C++
/// types, because callers that never reach the C++ emitter use it too.
inline llvm::StringRef mapScalarTag(PrimitiveAlternatives alternatives) {
    const auto tag = alternatives.tag();
    if (tag == mlir::TypeID::get<ctjs::BooleanAttr>()) { return "bool"; }
    if (tag == mlir::TypeID::get<ctjs::NumberAttr>()) { return "number"; }
    if (tag == mlir::TypeID::get<ctjs::StringAttr>()) { return "string"; }
    return {};
}

/// A proved keys/values snapshot or its confined standard Array.from copy.
bool isNativeMapSnapshot(mlir::Operation * op);

/// The schema family id of `value`, shared through `set` results, closed call
/// arguments and returns. A family may contain distinct run-time instances, so
/// it is not an identity proof. Returns -1 when the value has no proved Map
/// flow.
int64_t nativeMapGroup(mlir::Value value);

/// True when `op` produces a reference to the standard Map constructor, one of
/// its methods, or a snapshot builtin that lowering erases; its result is
/// never data.
bool isNativeMapBookkeeping(mlir::Operation * op);

} // namespace ctcompile::ctnative
#endif
