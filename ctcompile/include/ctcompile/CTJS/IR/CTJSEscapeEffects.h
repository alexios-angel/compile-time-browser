#pragma once
// PHASE 55'S EFFECT VOCABULARY - the three things an operand position can do to
// an object, and the places an escaping object can go.
//
// The sinks-and-carriers table itself is NOT here. It is in CTJSOps.td, as an
// Arg<..., [CTJS_Sink...]> decorator beside the operand it describes, and this
// header only names the C++ classes those decorators expand to - the same
// split mlir/Interfaces/SideEffectInterfaces.h makes for MemoryEffects, whose
// Read/Write/Allocate/Free are four empty structs and whose table is every
// [MemRead] in every dialect's .td. Part 23 §1: the table is where every
// soundness bug will live, so it lives where a wrong row is a .td diff.
//
// A CUSTOM EffectOpInterfaceBase, NOT MemoryEffects. Canonicalize, CSE and DCE
// consult MemoryEffectOpInterface and nothing else, so nothing MLIR does reads
// this; ctcompile_lit is the proof and stays byte-identical.
//
// THE REASON RIDES AS THE RESOURCE. §2.3 of the design wrote CTJS_Sink with no
// reason attached, and the verdict needs one per operand - `returned` and
// `stored` get different owners in 55B. A SideEffect decorator has exactly
// one per-instance slot that names a C++ class, the Resource, and a Resource
// is by definition "what the effect applies to": for an escape that IS the
// destination - the caller's frame, the heap, the globals table, a callee.
// So each destination below is a Resource whose getName() is the spelling of
// the matching CTNative_EscapeReason case, and the analysis turns it back into
// the enum with symbolizeEscapeReason. A route with a name the enum does not
// know fails the unit test by name.
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

/// CARRY: the result may BE the operand's object (ctjs.iterable's array arm
/// returns the same array). The result's alias set includes the operand's.
struct Carry : public Effect::Base<Carry> {};

/// BOXED-SITE, on a RESULT: an allocating operation the MVP does not track,
/// claimed `escapes(reason)` so the oracle reports it as backlog rather than
/// as unclaimed. The resource names the reason.
struct BoxedSite : public Effect::Base<BoxedSite> {};

inline bool Effect::classof(const mlir::SideEffects::Effect * effect) {
    return llvm::isa<Sink, Carry, BoxedSite>(effect);
}

/// Where an escaping object goes, one resource per CTNative_EscapeReason case
/// an operand or result can name. getName() IS the enum case's spelling.
#define CTJS_ESCAPE_ROUTE(Name, spelling)                                                          \
    struct Name final : public mlir::SideEffects::Resource::Base<Name> {                           \
        llvm::StringRef getName() final { return spelling; }                                       \
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
CTJS_ESCAPE_ROUTE(Phase59, "phase59")
CTJS_ESCAPE_ROUTE(NotTracked, "not_tracked")
#undef CTJS_ESCAPE_ROUTE

} // namespace ctcompile::ctjs::EscapeEffects
