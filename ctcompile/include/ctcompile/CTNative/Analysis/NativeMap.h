//===- NativeMap.h - proofs for confined standard Map instances -----------===//
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

inline constexpr llvm::StringLiteral kNativeMapSite = "ctnative.map_site";
inline constexpr llvm::StringLiteral kNativeMapAction = "ctnative.map_action";
inline constexpr llvm::StringLiteral kNativeMapMethod = "ctnative.map_method";
inline constexpr llvm::StringLiteral kNativeMapConstructor = "ctnative.map_constructor";
inline constexpr llvm::StringLiteral kNativeMapReason = "ctnative.map_reason";
inline constexpr llvm::StringLiteral kNativeMapGroup = "ctnative.map_group";
inline constexpr llvm::StringLiteral kNativeMapArgGroups = "ctnative.map_arg_groups";
inline constexpr llvm::StringLiteral kNativeMapPresent = "ctnative.map_present";
// A live same-instance/key last-write proof, independent of the family schema.
inline constexpr llvm::StringLiteral kNativeMapReadType = "ctnative.map_read_type";
// Exact primitive at the live write, independent of a broader SSA union.
inline constexpr llvm::StringLiteral kNativeMapWriteType = "ctnative.map_write_type";
// Exact key at this operation, rederived independently of the storage schema.
inline constexpr llvm::StringLiteral kNativeMapKeyType = "ctnative.map_key_type";
inline constexpr llvm::StringLiteral kNativeMapSnapshotCopy = "ctnative.map_snapshot_copy";
inline constexpr llvm::StringLiteral kNativeMapSnapshotBuiltin = "ctnative.map_snapshot_builtin";
inline constexpr llvm::StringLiteral kNativeMapRecords = "ctnative.map_records";

/// Confined entry-frame record storage, rederived with the standard Map proof.
/// The payload names a schema representative; an origin names the exact saved object.
mlir::Value nativeMapRecordPayload(mlir::Value map);
mlir::Value nativeMapRecordOrigin(mlir::Value read);
bool isNativeMapRecordStore(mlir::OpOperand & use);

/// Annotate only after proving both the standard constructor/method identity
/// and every instance use. No runtime assumption or boxed fallback is added.
/// `hostTyped` names the operations a complete host proof has typed: none
/// rebinds or inspects the standard Map, writes a prototype or runs script.
void prepareNativeMaps(mlir::ModuleOp module, const OwnedGlobalRoots * globals = nullptr,
                       llvm::function_ref<bool(mlir::Operation *)> hostTyped = nullptr);

/// The operation performed by a proved call or size read, or empty.
llvm::StringRef nativeMapAction(mlir::Operation * op);

/// Names an action taking a key; this lexical check supplies no call proof.
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
/// receiver, a Map family member, with the method's exact argument count;
/// empty otherwise. Rechecks the source edges a stale annotation could skip.
llvm::StringRef liveNativeMapAction(ctjs::CallOp call);

/// A zero-argument `new Map` whose new.target is its own callee. Lexical only:
/// whether global `Map` is the standard constructor is the caller's proof.
inline bool plainMapConstruction(ctjs::ConstructOp made) {
    auto load = made.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    return load && load.getName() == "Map" && made.getArgs().empty() &&
           made.getNewTarget() == made.getCallee();
}

/// The Map scalar tag of a proved one-kind primitive set - "bool", "number" or
/// "string" - or empty. Tags stay semantic: CTJS-only callers run this proof.
inline llvm::StringRef mapScalarTag(PrimitiveAlternatives alternatives) {
    const auto tag = alternatives.tag();
    if (tag == mlir::TypeID::get<ctjs::BooleanAttr>()) { return "bool"; }
    if (tag == mlir::TypeID::get<ctjs::NumberAttr>()) { return "number"; }
    if (tag == mlir::TypeID::get<ctjs::StringAttr>()) { return "string"; }
    return {};
}

/// A proved keys/values snapshot or its confined standard Array.from copy.
bool isNativeMapSnapshot(mlir::Operation * op);

/// Schema family shared through set results, closed call arguments and returns.
/// A family may contain distinct runtime instances; it is not an identity proof.
/// Returns -1 when the value has no proved Map flow.
int64_t nativeMapGroup(mlir::Value value);

/// These values name an erased standard constructor or method, never data.
bool isNativeMapBookkeeping(mlir::Operation * op);

} // namespace ctcompile::ctnative
#endif
