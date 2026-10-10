//===- CTJSEscapeEffects.h - Escape-analysis effects ------------*- C++ -*-===//
//
// Escape-analysis effects: the three things an operand position can do to an
// object (sink, carry, boxed site), and the destinations an escaping object
// can reach.
//
// The sinks-and-carriers table itself is not here. It is in Ops/*.td, as an
// Arg<..., [CTJS_Sink...]> decorator beside the operand it describes, and this
// header only names the C++ classes those decorators expand to. That is the
// same split mlir/Interfaces/SideEffectInterfaces.h makes for MemoryEffects,
// whose Read/Write/Allocate/Free are four empty structs and whose table is
// every [MemRead] in every dialect's .td. The table is where soundness bugs
// live, so it is kept in the .td files, where a wrong row shows up as a
// one-line diff.
//
// These use a custom EffectOpInterfaceBase, not MemoryEffects. Canonicalize,
// CSE and DCE consult only MemoryEffectOpInterface, so no MLIR pass reads
// these effects and the lit test output is unchanged by them.
//
// The escape reason is carried as the effect's Resource. The analysis needs a
// reason per operand (`returned` and `stored` lead to different owners), and a
// SideEffect decorator has exactly one per-instance slot that names a C++
// class: the Resource, which for an escape is its destination (the caller's
// frame, the heap, the globals table, a callee). So each route below is a
// Resource whose getName() is the spelling of the matching
// CTNative_EscapeReason case, and the analysis maps it back with
// symbolizeEscapeReason. A route the enum does not know fails the unit test.
//
//===----------------------------------------------------------------------===//

#pragma once
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

namespace ctcompile::ctjs::EscapeEffects {

/// The base of the three escape effects, so EscapeEffectOpInterface can be
/// instantiated over one class the way MemoryEffectOpInterface is over
/// MemoryEffects::Effect.
struct Effect : public mlir::SideEffects::Effect {
    using mlir::SideEffects::Effect::Effect;
    template <typename Derived> using Base = mlir::SideEffects::Effect::Base<Derived, Effect>;
    static bool classof(const mlir::SideEffects::Effect * effect);
};

/// SINK: the operand's object is handed to something that outlives the frame
/// or can run user code with it. Every site in the operand's alias set escapes,
/// with the reason the instance's resource names.
struct Sink : public Effect::Base<Sink> {};

/// CARRY: the result may be the operand's object (ctjs.iterable's array arm
/// returns the same array). The result's alias set includes the operand's.
struct Carry : public Effect::Base<Carry> {};

/// BOXED-SITE, on a result: an allocation the escape analysis does not track
/// yet. It is claimed `escapes(reason)` so the escape oracle
/// (tools/check/escape-oracle.py) lists it as known untracked work rather than
/// as an unclaimed site. The resource names the reason.
struct BoxedSite : public Effect::Base<BoxedSite> {};

inline bool Effect::classof(const mlir::SideEffects::Effect * effect) {
    return llvm::isa<Sink, Carry, BoxedSite>(effect);
}

/// Where an escaping object goes, one resource per CTNative_EscapeReason case
/// an operand or result can name. getName() is the enum case's spelling.
#define CTJS_ESCAPE_ROUTE(Name, spelling)                                                          \
    struct Name final : public mlir::SideEffects::Resource::Base<Name> {                           \
        llvm::StringRef getName() const final { return spelling; }                                 \
    };
CTJS_ESCAPE_ROUTE(Returned, "returned")
CTJS_ESCAPE_ROUTE(Thrown, "thrown")
CTJS_ESCAPE_ROUTE(Stored, "stored")
CTJS_ESCAPE_ROUTE(StoredGlobal, "stored_global")
CTJS_ESCAPE_ROUTE(Captured, "captured")
CTJS_ESCAPE_ROUTE(Passed, "passed")
CTJS_ESCAPE_ROUTE(Converted, "converted")
CTJS_ESCAPE_ROUTE(ProtoMutated, "proto_mutated")
CTJS_ESCAPE_ROUTE(AccessorDefined, "accessor_defined")
// The boxed-site reasons, named only by a result decorator.
CTJS_ESCAPE_ROUTE(Arguments, "arguments")
CTJS_ESCAPE_ROUTE(RuntimeArray, "runtime_array")
// `phase59`: closures and cells, which the escape analysis does not track yet.
// The name is historical and must match the CTNative_EscapeReason case.
CTJS_ESCAPE_ROUTE(Phase59, "phase59")
CTJS_ESCAPE_ROUTE(NotTracked, "not_tracked")
#undef CTJS_ESCAPE_ROUTE

} // namespace ctcompile::ctjs::EscapeEffects
