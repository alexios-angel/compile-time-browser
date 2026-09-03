// THE FIRST NATIVE ARTEFACT - part 24 Phase 62½-C.
//
// Everything before this file is an analysis. This is the lowering: a
// `ctjs.func` whose every value has a PROVED type the native tier can carry
// becomes an `emitc.func` over `double` and `bool`, and a function that does
// not is refused with a diagnostic naming the first value or operation that
// failed. Part 24 §1.2: the output links neither the interpreter nor its
// collector, and there is no boxed fallback - a refusal is a reason on the
// function, and the compilation-unit gate says whether the program is native.
//
// HOW A VALUE IS REPRESENTED, and where the representation is exact:
//
//   bool               bool     exactly
//   num<i32|i64|f64>   double   exactly - every JavaScript number is one
//   opt<num<...>>      double   with undefined as NaN. EXACT in arithmetic
//   opt<bottom>                 (undefined + 1 is NaN), relational comparison
//                               (undefined < 1 is false, as NaN < 1 is), and
//                               truthiness (both are falsy). NOT exact for
//                               equality with undefined/null, `typeof`, or
//                               printing - so every use in which the
//                               difference is observable is refused.
//
// The two `opt` rows exist because of the closed-world global rule: a global
// is undefined until its first store runs and nothing orders a load after
// one, so a numeric global is `opt<num>` (TypeInference.h). Numbers stay
// `double` even when proved `i32`: an int32_t representation is a Phase 63
// measurement, not a Phase 62½ obligation, and `double` is always correct.
//
// NOT A DIALECT CONVERSION, deliberately. After --ctjs-lift-to-scf the body is
// structured, and after the inference every value's type is known; retyping
// each value in place from the lattice and replacing each ctjs operation with
// its EmitC form leaves the whole of `scf` untouched for upstream's
// --convert-scf-to-emitc, which already handles `scf.if`, `scf.for` and
// `scf.while`. A TypeConverter converts by TYPE, and every JavaScript value
// has the same type; the lattice is per VALUE.
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
// mlir-pdll's output CALLS mlir::parseSourceString: a declarative pattern in
// this release is PDL text the generated constructor parses, not generated
// code. Without this header the .inc fails with "no member named
// 'parseSourceString'", which reads like a bad pattern and is a missing
// include.
#include "mlir/Parser/Parser.h"
#include "mlir/Rewrite/FrozenRewritePatternSet.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVELOWERTOEMITC
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

// --- the PDLL pattern's native body -------------------------------------------
//
// NOT IN THE ANONYMOUS NAMESPACE BELOW: the generated header spells it by
// qualified name. See PruneDeadStores.cpp for the same shape and the same two
// rules - one call into a named function, and the rewriter threaded through
// whether the body wants it or not, because mlir-pdll emits the wrapper with
// that parameter and -Wextra -Werror makes an unused one a build failure.
namespace pdll {

// THE KIND TEST FOR UnaryPlusIsIdentity.pdll. It is C++ rather than an
// attribute literal in the pattern because the literal does not work: mlir-pdll
// cannot parse `attr<"#ctjs.unary_kind<plus>">` without our dialect registered,
// and it drops the constraint and exits 0 rather than saying so.
//
// IT TAKES A ctjs::UnaryOp, NOT AN Operation *, and the pattern's
// `Op<ctjs.unary>` parameter is what does that. The reference's "Native
// Constraint Type Translations" says a NAMED operation constraint whose ODS has
// been included translates to the qualified C++ class rather than to
// `::mlir::Operation *`, and the generated wrapper here is
// `IsUnaryPlusPDLFn(::mlir::PatternRewriter &, ::ctcompile::ctjs::UnaryOp)`.
// The framework has already checked the type by then - ProcessDerivedPDLValue's
// verifyAsArg is a TypeSwitch that fails the constraint on a mismatch - so
// there is nothing left here to dyn_cast and nothing to null-check.
mlir::LogicalResult isUnaryPlus(mlir::PatternRewriter &, ctjs::UnaryOp o) {
    return mlir::success(o.getKind() == ctjs::UnaryKind::Plus);
}

} // namespace pdll

namespace {

namespace ec = mlir::emitc;

// mlir-pdll's output, from UnaryPlusIsIdentity.pdll. Generated into the BUILD
// tree by add_mlir_pdll_library - Principle 9: never committed, never a source.
#include "UnaryPlusIsIdentity.h.inc"

// --- representation -----------------------------------------------------------

enum class carrier {
    none,
    boolean,
    number,
    structure,
    vector
};

// What C++ type carries a value of this ctnative type, per the table above.
// `none` is "not representable here", and is the reason for a refusal.
carrier carrierOf(mlir::Type type) {
    if (type == nullptr) { return carrier::none; }
    if (llvm::isa<BoolType>(type)) { return carrier::boolean; }
    if (llvm::isa<NumType>(type)) { return carrier::number; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        if (llvm::isa<BottomType, NumType>(opt.getElementType())) { return carrier::number; }
        // A boolean-or-undefined, as a bool whose undefined is false: exact
        // in a branch, under `!` and as truthiness (both are falsy), refused
        // where the difference shows (equality) - the number rows' shape.
        if (llvm::isa<BoolType>(opt.getElementType())) { return carrier::boolean; }
    }
    // PHASE 57A: A DENSE ARRAY IS A `std::vector<double>` AND NOTHING ELSE
    // YET. The element carrier decides: `vector<bool>` is a bit-packed
    // specialisation whose `operator[]` returns a proxy that aliases the
    // container and converts differently from `bool` (part 24 Stage 57A says
    // so by name), and a vector of anything with no carrier has none either.
    // So only a numeric element has a representation here, and the refusal
    // for the rest is named at the literal.
    if (auto elements = llvm::dyn_cast<VecType>(type)) {
        return carrierOf(elements.getElementType()) == carrier::number ? carrier::vector
                                                                       : carrier::none;
    }
    return carrier::none;
}

// The one C++ type a dense array lowers to. Spelled once: the emitted
// declaration, the helper signatures and the lit test all have to agree, and
// three copies of a string is how they stop agreeing.
constexpr llvm::StringLiteral kVectorType = "std::vector<double>";

mlir::Type vectorCarrierType(mlir::MLIRContext * c) {
    return ec::LValueType::get(ec::OpaqueType::get(c, kVectorType));
}

// Can this value's carrier be undefined? True for the two `opt` rows, whose
// NaN representation is exact only in arithmetic, comparison and truthiness.
bool mayBeUndefined(mlir::Type type) {
    return llvm::isa<OptType>(type);
}

mlir::Type carrierType(mlir::MLIRContext * c, carrier which) {
    // `none` HAS NO REPRESENTATION, and returning f64 for it was a silent
    // guess at the one thing this tier exists not to guess at. A value with no
    // proved carrier must be refused by admission long before it gets here;
    // reaching this point means a rule let one through, and a crash naming
    // that is worth far more than a double that happens to verify.
    switch (which) {
    case carrier::boolean: return mlir::IntegerType::get(c, 1);
    case carrier::number: return mlir::Float64Type::get(c);
    case carrier::structure:
    case carrier::vector:
    case carrier::none: break;
    }
    llvm::report_fatal_error("ctnative lowering: asked for the C++ carrier of a value that has "
                             "none - admission should have refused it");
}

std::string printed(mlir::Type type) {
    std::string out;
    llvm::raw_string_ostream os{out};
    if (type == nullptr) {
        os << "<unvisited>";
    } else {
        os << type;
    }
    return out;
}

// --- PHASE 59 SLICE 1: A CLOSURE CARRIES BY LIFTING, NOT BY ALLOCATING ----------
//
// A JavaScript closure is a function plus the bindings it captured. The obvious
// C++ for it is a lambda with a capture list, or a `std::function` where the
// callee is not known - and both of those own storage, which is the one thing a
// tier with no collector has to be most careful about. So this slice does not
// build a closure at all. It LIFTS: the captured values become extra LEADING
// parameters of the target function, `ctjs.load_upvalue i` inside it becomes a
// reference to parameter i, `ctjs.create_closure` lowers to nothing exactly as a
// declaration closure already does, and each call passes the captured values as
// ordinary arguments. Zero allocation, no functor, no ownership question.
//
// IT IS AN IR REWRITE THAT RUNS BEFORE THE SOLVE, and that is what makes it
// cheap rather than a second dataflow analysis. Once a closure call is a
// `ctjs.call_direct`, every piece of machinery this file already has works
// unchanged: MLIR's CallOpInterface makes the target reachable to
// DeadCodeAnalysis (an uncalled private function is dead and its types read
// `<unvisited>`), TypeInference propagates each capture's proved type into the
// leading parameter it became, and the call-graph fixpoint in runOnOperation
// closes over the new edge in both directions. The lowering below needed no new
// arm for the call and no new carrier.
//
// WHAT THE IR ACTUALLY DOES, AND WHERE THE BRIEF FOR THIS WORK WAS WRONG.
// "Captures are parent-frame VALUES at construction" is not what the bytecode
// emits: `compiler_impl::is_captured` sets `local::boxed` for a local MENTIONED
// inside a nested function, mutated or not, and `op::new_cell` then boxes it -
// so EVERY from_parent_local capture operand is a `ctjs.create_cell` result and
// none of them has a carrier. Taken literally the admission rule "every
// capture's value has a carrier" would lift nothing at all. What is true is the
// sentence after it: a cell nothing ever writes is a constant box, so this
// unboxes exactly those - every use a `ctjs.cell_get` or a capture of a lifted
// closure, no `ctjs.cell_set`, and no `ctjs.store_upvalue` anywhere the cell can
// reach - and the carrier check then applies to what the cell HOLDS.
//
// AND A CAPTURE THAT IS NOT A CELL IS THE PLACEHOLDER. The importer pushes
// `undefined` for every descriptor that is not from_parent_local, because the VM
// fills those from the enclosing closure and never looks at the operand
// (BytecodeImport.cpp, op::closure). Lifting one would capture `undefined` where
// the program captured a binding, so requiring every capture to be a cell of
// THIS frame is not a convenience: it is what makes the rewrite sound.

// The function index the importer put after the last `$` of the symbol. The
// same reading ResolveGlobals does, and the only link there is between a
// `ctjs.create_closure`'s `$function` attribute and the `ctjs.func` it names.
std::optional<unsigned> functionIndexOf(ctjs::FuncOp fn) {
    const llvm::StringRef name = fn.getSymName();
    const std::size_t dollar = name.rfind('$');
    if (dollar == llvm::StringRef::npos) { return std::nullopt; }
    unsigned index = 0;
    if (name.substr(dollar + 1).getAsInteger(10, index)) { return std::nullopt; }
    return index;
}

// Where a `ctjs.create_closure`'s captures start: after $enclosing_closure and
// $enclosing_this, which are operands and not attributes.
constexpr unsigned kFirstCapture = 2;

bool isUndefinedConstant(mlir::Value v) {
    auto k = v.getDefiningOp<ctjs::ConstantOp>();
    return k && llvm::isa<ctjs::UndefinedAttr>(k.getValue());
}

// What the rewrite did, for the `report` remark. Pass statistics are compiled
// out of the LLVM package this builds against, so a counter that is asserted
// has to be printed.
struct liftReport {
    unsigned functions = 0; // ctjs.funcs whose captures became parameters
    unsigned closures = 0;  // ctjs.create_closures that now lower to nothing
    unsigned captures = 0;  // capture operands turned into arguments
    unsigned calls = 0;     // ctjs.calls rewritten to ctjs.call_direct
    unsigned cells = 0;     // ctjs.create_cells proved constant and unboxed
};

struct closureLifter {
    mlir::ModuleOp module;
    mlir::MLIRContext * context;

    llvm::DenseMap<unsigned, ctjs::FuncOp> byIndex;
    // A function that may write one of its OWN upvalue slots, transitively
    // through the closures it makes. `ctjs.store_upvalue %arg2[j]` inside G
    // writes the cell that G's creator put in slot j, so a cell captured into
    // any such G is not constant - and a cell captured into G and re-captured
    // by an H that writes it is not either, which is why this is a fixpoint
    // and not a one-line test.
    llvm::DenseSet<mlir::Operation *> mutatesUpvalue;
    // A call inside one of these may not become a ctjs.call_direct: op::call
    // pushes its frame with the PENDING new.target, and call_direct
    // materialises undefined for it. ResolveGlobals refuses the same shape.
    llvm::DenseSet<mlir::Operation *> passesNewTarget;
    // THERE IS NO "ALREADY CALLED BY SYMBOL" GUARD, and there was one until it
    // was tested. It refused to lift a target a ctjs.call_direct already
    // names, on the grounds that such a call passes exactly the entry block's
    // operands and inserting capture parameters would break it. No program
    // reaches it: --ctjs-resolve-globals resolves only a global bound in the
    // top level's prologue to a create_closure, and that closure's single use
    // is the store - which isDeclarationClosure exempts before this rewrite
    // looks at it - while a source function compiles to exactly one `closure`
    // opcode, so no ctjs.func is both. Running the pass TWICE, which is the
    // one shape that could, is already idempotent for two independent reasons
    // the double run in closure-refusals.mlir pins: `upvalue_count` is set to
    // 0 by the first lift, so the second sees a capture list that disagrees
    // with the descriptors, and a lifted closure's only remaining use is a
    // call_direct's callee value, which is not a call this rewrite lowers.
    // Removing the guard and running the lowering twice changed nothing, so it
    // was decoration and is gone. If the reasoning above is wrong the failure
    // is CallDirectOp::verifySymbolUses on an operand count - a hard verifier
    // error, not a wrong answer.

    llvm::SmallVector<ctjs::CreateClosureOp> closures;

    explicit closureLifter(mlir::ModuleOp m) : module(m), context(m.getContext()) {}

    ctjs::FuncOp targetOf(ctjs::CreateClosureOp c) {
        return byIndex.lookup(static_cast<unsigned>(c.getFunction()));
    }

    void census() {
        module.walk([&](ctjs::FuncOp fn) {
            if (const std::optional<unsigned> index = functionIndexOf(fn)) {
                byIndex.try_emplace(*index, fn);
            }
        });
        module.walk([&](mlir::Operation * o) {
            auto holder = o->getParentOfType<ctjs::FuncOp>();
            if (llvm::isa<ctjs::StoreUpvalueOp>(o)) {
                if (holder) { mutatesUpvalue.insert(holder.getOperation()); }
            } else if (llvm::isa<ctjs::PassNewTargetOp>(o)) {
                if (holder) { passesNewTarget.insert(holder.getOperation()); }
            } else if (auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(o)) {
                closures.push_back(made);
            }
        });
        // The fixpoint over the closure-target graph.
        for (bool changed = true; changed;) {
            changed = false;
            for (ctjs::CreateClosureOp c : closures) {
                ctjs::FuncOp target = targetOf(c);
                if (!target || !mutatesUpvalue.contains(target.getOperation())) { continue; }
                auto maker = c->getParentOfType<ctjs::FuncOp>();
                if (!maker) { continue; }
                changed |= mutatesUpvalue.insert(maker.getOperation()).second;
            }
        }
    }

    // A CELL NOTHING EVER WRITES, which is the whole of the immutability proof.
    // Every use is a read or a capture into a function that writes no upvalue;
    // a `ctjs.cell_set`, or any use this does not name, fails it.
    bool isConstantCell(ctjs::CreateCellOp cell) {
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
            auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
            if (!made || use.getOperandNumber() < kFirstCapture) { return false; }
            ctjs::FuncOp target = targetOf(made);
            if (!target || mutatesUpvalue.contains(target.getOperation())) { return false; }
        }
        return true;
    }

    // The four admission conditions of slice 1, as one sentence each. The
    // reason is written onto the closure so that the function containing it is
    // refused by NAME rather than by "`ctjs.create_closure` is not native yet".
    std::optional<std::string> whyNotLiftable(ctjs::CreateClosureOp c) {
        ctjs::FuncOp target = targetOf(c);
        if (!target) {
            return "its target emitted no ctjs.func - the importer refused it (ctjs.skipped)";
        }
        if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
            return "its target has no body";
        }
        mlir::Block & entry = target.getBody().front();
        const unsigned parameters = entry.getNumArguments() - 3;
        const auto captures = static_cast<unsigned>(c.getUpvalues().size());
        if (captures != static_cast<unsigned>(target.getUpvalueCount())) {
            return "its capture list disagrees with the descriptors of the function it names";
        }
        // AN ARROW'S `this` IS LEXICAL, and after the importer's correction the
        // presence of a non-undefined $enclosing_this is the only place the IR
        // says a target is one. A lifted call passes the CALL's receiver as
        // %arg0, which for an arrow is not what the interpreter reads - so an
        // arrow may be lifted only when it never looks.
        if (!isUndefinedConstant(c.getEnclosingThis()) && !entry.getArgument(0).use_empty()) {
            return "it is an arrow function that reads its lexical `this` - Stage 59B";
        }
        // CONDITION 1 and CONDITION 2, which are one test on the cell: a
        // capture is a constant box in this frame, or it is not liftable.
        for (unsigned i = 0; i < captures; ++i) {
            auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>();
            if (!cell) {
                return "capture " + std::to_string(i) +
                       " is not a cell of this frame - it is filled from the enclosing closure, "
                       "which slice 1 does not carry";
            }
            if (!isConstantCell(cell)) {
                return "capture " + std::to_string(i) +
                       " is a binding that is reassigned - a shared cell is Phase 59 slice 2";
            }
        }
        // CONDITION 4: every use of the closure VALUE is a call this lowers.
        if (c.getResult().use_empty()) { return "nothing calls it"; }
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            auto call = llvm::dyn_cast<ctjs::CallOp>(user);
            if (!call || use.getOperandNumber() != 0) {
                if (llvm::isa<ctjs::StoreGlobalOp>(user)) {
                    return "it is stored to a global - Phase 59 slice 2";
                }
                if (llvm::isa<ctjs::ReturnOp>(user)) { return "it is returned - Phase 59 slice 2"; }
                if (llvm::isa<ctjs::SetPropertyOp, ctjs::CreateObjectOp, ctjs::AppendOp,
                              ctjs::CreateArrayOp>(user)) {
                    return "it is stored into an object or an array - Phase 59 slice 2";
                }
                if (llvm::isa<ctjs::ConstructOp>(user) && use.getOperandNumber() == 0) {
                    return "it is used as a constructor - Phase 60 owns `new`";
                }
                if (call || llvm::isa<ctjs::CallDirectOp, ctjs::ConstructOp>(user)) {
                    // PASSING A CLOSURE IS NOT A LIFT, and this is the one
                    // place the brief for this work asked for something the
                    // mechanism cannot give. Lifting moves captures to the
                    // CALL SITE; a callee that receives a function value has
                    // no call site to move them to, and lowering it needs the
                    // callee specialised per closure - Phase 63's monomorphism
                    // proof, not this.
                    return "it is passed as an argument - lifting has no call site to move the "
                           "captures to, so this needs a specialised callee (Phase 63), not a "
                           "lift";
                }
                return ("it reaches `" + user->getName().getStringRef() +
                        "`, which slice 1 does "
                        "not lower")
                    .str();
            }
            if (call.getArgs().size() > parameters) {
                return "a call passes " + std::to_string(call.getArgs().size()) +
                       " argument(s) to " + std::to_string(parameters) +
                       " parameter(s) - the surplus has frame semantics";
            }
            auto caller = call->getParentOfType<ctjs::FuncOp>();
            if (caller && passesNewTarget.contains(caller.getOperation())) {
                return "a call of it sits in a function that passes new.target";
            }
        }
        // THE TARGET'S OWN CLOSURE FEEDS NOTHING BUT NESTED CLOSURES AND
        // UPVALUE READS, which is ResolveGlobals' clause 4 word for word and
        // is here for its reason: the lift marks the target `private`, and
        // `private` is the claim that EVERY caller is visible. A target that
        // leaks its own closure value can be called through that value by
        // something this IR cannot see, and the claim would be false.
        for (mlir::OpOperand & use : entry.getArgument(2).getUses()) {
            mlir::Operation * user = use.getOwner();
            if (use.getOperandNumber() == 0 &&
                llvm::isa<ctjs::CreateClosureOp, ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(user)) {
                continue;
            }
            return ("its target's own closure escapes into `" + user->getName().getStringRef() +
                    "`, so a call of it may come from somewhere this rewrite cannot see")
                .str();
        }
        // CONDITION 3 is the existing call-graph fixpoint's, not this one's -
        // but the target's own upvalue reads have to be the shape the rewrite
        // replaces, or a load would be left naming a closure that is gone.
        std::optional<std::string> bad;
        target.getBody().walk([&](mlir::Operation * o) {
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
                if (read.getClosure() != entry.getArgument(2) ||
                    static_cast<unsigned>(read.getIndex()) >= captures) {
                    bad = "its target reads an upvalue this rewrite cannot name";
                }
            }
            if (llvm::isa<ctjs::StoreUpvalueOp>(o)) {
                bad = "its target reassigns a captured binding - a shared cell is Phase 59 "
                      "slice 2";
            }
        });
        return bad;
    }

    liftReport run() {
        census();
        liftReport out;
        // Per target: the closures that name it, and the first reason any of
        // them could not be lifted. A target's signature changes for the whole
        // program, so ONE unliftable creation site blocks every other.
        llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> byTarget;
        llvm::DenseMap<mlir::Operation *, std::string> blocked;
        llvm::DenseMap<mlir::Operation *, std::string> reasonOf; // closure -> its own reason
        for (ctjs::CreateClosureOp c : closures) {
            // A DECLARATION IS A BINDING, NOT A VALUE, and lowers to nothing
            // already. Leave it to admission::isDeclarationClosure.
            if (admissionIsDeclaration(c)) { continue; }
            ctjs::FuncOp target = targetOf(c);
            const std::optional<std::string> why = whyNotLiftable(c);
            if (why) {
                reasonOf[c.getOperation()] = *why;
                if (target) { blocked.try_emplace(target.getOperation(), *why); }
                continue;
            }
            byTarget[target.getOperation()].push_back(c);
        }
        for (auto & [target, made] : byTarget) {
            if (blocked.contains(target)) {
                for (ctjs::CreateClosureOp c : made) {
                    reasonOf[c.getOperation()] =
                        "the function it names is also made somewhere this tier cannot lift: " +
                        blocked.lookup(target);
                }
                continue;
            }
            lift(llvm::cast<ctjs::FuncOp>(target), made, out);
        }
        // The reasons, onto the closures that kept them, so that the function
        // holding one is refused by name.
        for (const auto & [op, why] : reasonOf) {
            op->setAttr("ctnative.closure_reason", mlir::StringAttr::get(context, why));
        }
        unboxCells(out);
        return out;
    }

    // isDeclarationClosure, spelled here because admission is declared below
    // and this rewrite runs before it. Kept to one line so the two cannot
    // drift into disagreeing about what a declaration is.
    static bool admissionIsDeclaration(ctjs::CreateClosureOp c) {
        return c.getResult().hasOneUse() &&
               llvm::isa<ctjs::StoreGlobalOp>(*c.getResult().getUsers().begin());
    }

    void lift(ctjs::FuncOp target, llvm::ArrayRef<ctjs::CreateClosureOp> made, liftReport & out) {
        mlir::Block & entry = target.getBody().front();
        const auto valueType = ctjs::ValueType::get(context);
        const unsigned captures = static_cast<unsigned>(target.getUpvalueCount());
        const unsigned parameters = entry.getNumArguments() - 3;

        // THE CAPTURES BECOME LEADING PARAMETERS, inserted after the three
        // implicit arguments so that ctjs.call_direct's operand order - which
        // IS the entry block's argument order - still lines up, and so that
        // lower()'s existing `for (i = 3; ...)` picks them up with no change.
        for (unsigned i = 0; i < captures; ++i) {
            entry.insertArgument(3 + i, valueType, target.getLoc());
        }
        llvm::SmallVector<mlir::Type> inputs(entry.getNumArguments(), valueType);
        target.setFunctionTypeAttr(
            mlir::TypeAttr::get(mlir::FunctionType::get(context, inputs, {valueType})));

        llvm::SmallVector<ctjs::LoadUpvalueOp> reads;
        target.getBody().walk([&](ctjs::LoadUpvalueOp read) { reads.push_back(read); });
        for (ctjs::LoadUpvalueOp read : reads) {
            read.getResult().replaceAllUsesWith(
                entry.getArgument(3 + static_cast<unsigned>(read.getIndex())));
            read.erase();
        }
        // NO UPVALUES LEFT, and the attribute says so: after this the function
        // reads its bindings out of its own frame like any other parameter.
        target->setAttr("upvalue_count", mlir::Builder(context).getI32IntegerAttr(0));
        target->setAttr("ctnative.captures",
                        mlir::Builder(context).getI32IntegerAttr(static_cast<int>(captures)));
        // PRIVATE, AND IT IS NOT COSMETIC. MLIR's DeadCodeAnalysis gives a
        // PUBLIC symbol unknown predecessors, so TypeInference falls back to
        // setToEntryState and every parameter - captures included - reads
        // `!ctnative.boxed` however many call sites the module holds. That was
        // measured here: the whole lift worked and every lifted function was
        // then refused with "capture 0 is !ctnative.boxed - no caller proves
        // it". ResolveGlobals sets the same bit for the same reason, and gates
        // it on the same claim: every caller of this function is visible,
        // which the conditions above have just established.
        mlir::SymbolTable::setSymbolVisibility(target, mlir::SymbolTable::Visibility::Private);
        ++out.functions;

        for (ctjs::CreateClosureOp c : made) {
            llvm::SmallVector<mlir::Value> captured;
            for (mlir::Value cell : c.getUpvalues()) {
                captured.push_back(cell.getDefiningOp<ctjs::CreateCellOp>().getInitial());
            }
            llvm::SmallVector<ctjs::CallOp> calls;
            for (mlir::Operation * user : c.getResult().getUsers()) {
                calls.push_back(llvm::cast<ctjs::CallOp>(user));
            }
            for (ctjs::CallOp call : calls) {
                mlir::OpBuilder at(call);
                const mlir::Value undefined = ctjs::ConstantOp::create(
                    at, call.getLoc(), valueType, ctjs::UndefinedAttr::get(context));
                llvm::SmallVector<mlir::Value> arguments(captured);
                arguments.append(call.getArgs().begin(), call.getArgs().end());
                // The resolver's own padding rule: op::call fills a missing
                // parameter with undefined, so a short call becomes a full one.
                while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
                auto direct = ctjs::CallDirectOp::create(
                    at, call.getLoc(), valueType,
                    mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()), call.getReceiver(),
                    undefined, c.getResult(), arguments,
                    /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
                call.getResult().replaceAllUsesWith(direct.getResult());
                call.erase();
                ++out.calls;
            }
            c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
            ++out.closures;
            out.captures += captures;
        }
    }

    // A CELL WHOSE EVERY CLOSURE IS LIFTED holds a value nobody can change, so
    // a read of it IS that value and the box is not built at all. Done after
    // every lift, because a cell captured by one lifted and one unlifted
    // closure must stay a cell for the unlifted one - which is refused, but
    // whose IR this pass has no business falsifying.
    void unboxCells(liftReport & out) {
        llvm::SmallVector<ctjs::CreateCellOp> cells;
        module.walk([&](ctjs::CreateCellOp cell) { cells.push_back(cell); });
        for (ctjs::CreateCellOp cell : cells) {
            std::optional<std::string> why;
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
                if (llvm::isa<ctjs::CreateClosureOp>(user) &&
                    use.getOperandNumber() >= kFirstCapture) {
                    if (user->hasAttr("ctnative.lifted")) { continue; }
                    auto reason = user->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
                    why = "the closure that captures it is not lifted" +
                          (reason ? " - " + reason.getValue().str() : std::string{});
                    break;
                }
                if (llvm::isa<ctjs::CellSetOp>(user)) {
                    why = "it is assigned after it was boxed, so its value is not the one the "
                          "cell was built with";
                    break;
                }
                why = ("it reaches `" + user->getName().getStringRef() + "`").str();
                break;
            }
            // A REASON ON THE BOX ITSELF, because the box is what admission
            // meets first: `op::new_cell` runs in the prologue, before the
            // `closure` opcode that captures it, so a walk in program order
            // reaches the cell and would otherwise refuse the function with
            // "`ctjs.create_cell` is not native yet" - a sentence that names
            // neither the binding nor what is wrong with it.
            if (why) {
                cell->setAttr("ctnative.cell_reason", mlir::StringAttr::get(context, *why));
                continue;
            }
            llvm::SmallVector<ctjs::CellGetOp> reads;
            for (mlir::Operation * user : cell.getResult().getUsers()) {
                if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(user)) { reads.push_back(read); }
            }
            for (ctjs::CellGetOp read : reads) {
                read.getResult().replaceAllUsesWith(cell.getInitial());
                read.erase();
            }
            cell->setAttr("ctnative.unboxed", mlir::UnitAttr::get(context));
            ++out.cells;
        }
    }
};

// --- the admission check --------------------------------------------------------

struct admission {
    mlir::DataFlowSolver & solver;
    std::string why;
    // The carrier every `return` in the function agrees on; `none` until the
    // first return is seen. A function with no return at all returns NaN -
    // undefined's carrier - which lower() picks when this stays `none`.
    carrier returns = carrier::none;

    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const {
        const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
        return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
    }

    bool refuse(std::string reason) {
        if (why.empty()) { why = std::move(reason); }
        return false;
    }

    // A value used as a NUMBER: num, or an opt row (NaN is exact here).
    bool numeric(mlir::Value v, llvm::StringRef where) {
        if (carrierOf(typeOf(v)) != carrier::number) {
            return refuse((where + " operand is " + printed(typeOf(v)) + ", not a number").str());
        }
        return true;
    }
    // A value used as a BOOLEAN.
    bool boolean(mlir::Value v, llvm::StringRef where) {
        if (carrierOf(typeOf(v)) != carrier::boolean) {
            return refuse((where + " operand is " + printed(typeOf(v)) + ", not a boolean").str());
        }
        return true;
    }
    // A value that must not be undefined: equality is observable.
    bool defined(mlir::Value v, llvm::StringRef where) {
        if (mayBeUndefined(typeOf(v))) {
            return refuse((where + " on a value that may be undefined - NaN would not "
                                   "compare the way undefined does")
                              .str());
        }
        return true;
    }

    // A FUNCTION DECLARATION IS A BINDING, NOT A VALUE. `function f() {}` at
    // any level is `create_closure` whose only use is `store_global "f"`; in
    // the closed world the function exists as an emitc.func and calls of it
    // are direct, so the pair lowers to nothing. A closure used as a VALUE -
    // passed, returned, stored anywhere else - is not native yet (Phase 59).
    static bool isDeclarationClosure(mlir::Operation * o) {
        auto closure = llvm::dyn_cast_or_null<ctjs::CreateClosureOp>(o);
        if (!closure || !closure.getResult().hasOneUse()) { return false; }
        return llvm::isa<ctjs::StoreGlobalOp>(*closure.getResult().getUsers().begin());
    }
    static bool isDeclarationStore(mlir::Operation * o) {
        auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o);
        return store && isDeclarationClosure(store.getValue().getDefiningOp());
    }

    // PHASE 59 SLICE 1. The lift above already moved this closure's captures
    // to the call sites and rewrote every call to a ctjs.call_direct; what is
    // left of it is the `$callee_value` operand, which the call arm drops -
    // so, exactly like a declaration closure, it lowers to nothing. The cell
    // it captured is the same story one step down: the box was proved
    // constant, every read of it is already the value, and what remains is the
    // capture operand of a closure that is about to go.
    static bool isLiftedClosure(mlir::Operation * o) {
        return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(o) && o->hasAttr("ctnative.lifted");
    }
    static bool isUnboxedCell(mlir::Operation * o) {
        return llvm::isa_and_nonnull<ctjs::CreateCellOp>(o) && o->hasAttr("ctnative.unboxed");
    }
    static bool closureLowersToNothing(mlir::Operation * o) {
        return isDeclarationClosure(o) || isLiftedClosure(o);
    }
    // WHY THIS CLOSURE IS NOT ONE OF THOSE, in the words the lift wrote onto
    // it. Spelled once because it is asked in two places that used to give
    // different answers: at the operation, and at the `%arg2` operand it takes
    // - and the operand's answer was "uses its own closure", which is a
    // sentence about the ENCLOSING function reading a value it never reads.
    static std::string closureRefusal(mlir::Operation * o) {
        auto why = o->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
        return why ? ("a closure used as a value: " + why.getValue()).str()
                   : std::string{"a closure used as a value - Phase 59"};
    }

    // THE CALLEE VALUE OF A DIRECT CALL lowers to nothing: the call names its
    // function by symbol, and the boxed closure the interpreter would have
    // called through is only carried so the boxed tier can still lower the
    // same op. A load_global whose every use is that operand is exempt from
    // the carrier check for exactly that reason.
    static bool feedsOnlyDirectCallees(mlir::Value v) {
        if (v.use_empty()) { return false; }
        for (mlir::OpOperand & use : v.getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 2) { return false; }
        }
        return true;
    }

    // PHASE 56: A CLOSED SHAPE IS A STRUCT BY VALUE. TypeInference::hasClosedShape
    // is the proof - every use is a get or set through a constant key, so the
    // object never reaches anything that could add or remove a field, and never
    // leaves the frame (a return, a store, a call would all be uses that open
    // it). Each key must be a C identifier, each field a number or a boolean.
    static bool isClosedObject(mlir::Value v) { return TypeInference::hasClosedShape(v); }
    static llvm::StringRef keyOf(mlir::Value key) {
        auto constant = key.getDefiningOp<ctjs::ConstantOp>();
        if (!constant) { return {}; }
        auto str = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
        return str ? str.getValue() : llvm::StringRef{};
    }
    static bool isCIdentifier(llvm::StringRef key) {
        if (key.empty() || std::isdigit(static_cast<unsigned char>(key.front()))) { return false; }
        return llvm::all_of(
            key, [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; });
    }
    // A KEY THAT IS A C IDENTIFIER AND STILL CANNOT BE A FIELD NAME. Field
    // names are emitted verbatim - `cIdentifier()` sanitises symbols, not
    // members - so `o.class = 3` emitted `double class;` and `o.NAN = 5`
    // emitted `double NAN;` under this file's own `#include <cmath>`. Both
    // were admitted with no refusal and both are hard -Werror build failures
    // on a program the tier had declared native. Refused rather than mangled:
    // a generated field keeps the JavaScript name a reader is looking for, and
    // mangling every field to buy this rare case is a trade Phase 56C should
    // make deliberately, not this fix.
    static bool isReservedInCpp(llvm::StringRef key) {
        static constexpr llvm::StringLiteral kReserved[] = {
            // keywords a member may not be named
            "alignas",
            "alignof",
            "and",
            "and_eq",
            "asm",
            "auto",
            "bitand",
            "bitor",
            "bool",
            "break",
            "case",
            "catch",
            "char",
            "char8_t",
            "char16_t",
            "char32_t",
            "class",
            "compl",
            "concept",
            "const",
            "consteval",
            "constexpr",
            "constinit",
            "const_cast",
            "continue",
            "co_await",
            "co_return",
            "co_yield",
            "decltype",
            "default",
            "delete",
            "do",
            "double",
            "dynamic_cast",
            "else",
            "enum",
            "explicit",
            "export",
            "extern",
            "false",
            "float",
            "for",
            "friend",
            "goto",
            "if",
            "inline",
            "int",
            "long",
            "mutable",
            "namespace",
            "new",
            "noexcept",
            "not",
            "not_eq",
            "nullptr",
            "operator",
            "or",
            "or_eq",
            "private",
            "protected",
            "public",
            "register",
            "reinterpret_cast",
            "requires",
            "return",
            "short",
            "signed",
            "sizeof",
            "static",
            "static_assert",
            "static_cast",
            "struct",
            "switch",
            "template",
            "this",
            "thread_local",
            "throw",
            "true",
            "try",
            "typedef",
            "typeid",
            "typename",
            "union",
            "unsigned",
            "using",
            "virtual",
            "void",
            "volatile",
            "wchar_t",
            "while",
            "xor",
            "xor_eq",
            // macros the two headers this file emits are allowed to define
            "NAN",
            "INFINITY",
            "HUGE_VAL",
            "HUGE_VALF",
            "HUGE_VALL",
            "EOF",
            "NULL",
            "BUFSIZ",
            "FILENAME_MAX",
            "FOPEN_MAX",
            "L_tmpnam",
            "TMP_MAX",
            "SEEK_SET",
            "SEEK_CUR",
            "SEEK_END",
            "stdin",
            "stdout",
            "stderr",
            "errno",
            "MATH_ERRNO",
            "MATH_ERREXCEPT",
            "FP_FAST_FMA",
            "FP_INFINITE",
            "FP_NAN",
            "FP_NORMAL",
            "FP_SUBNORMAL",
            "FP_ZERO",
            "FP_ILOGB0",
            "FP_ILOGBNAN",
        };
        return llvm::is_contained(kReserved, key);
    }
    // NAMES OBJECT.PROTOTYPE ANSWERS FOR. A field that is only ever READ is
    // `undefined` for a plain key, which this tier carries as NaN - but these
    // names are not undefined: the literal's prototype answers them, and the
    // interpreter finds a function where the generated struct finds NaN. So
    // `if (o.constructor)` took the else branch natively and the then branch
    // in the interpreter, with no refusal anywhere. A key that IS stored
    // shadows the inherited one and is fine; only a read-only key is refused.
    static bool namesObjectPrototypeMember(llvm::StringRef key) {
        static constexpr llvm::StringLiteral kInherited[] = {
            "constructor",      "hasOwnProperty",   "isPrototypeOf",    "propertyIsEnumerable",
            "toLocaleString",   "toString",         "valueOf",          "__proto__",
            "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__",
        };
        return llvm::is_contained(kInherited, key);
    }
    // A string constant whose every use is a property key of a closed object
    // lowers to nothing: the key becomes a member name.
    static bool isKeyOnlyString(mlir::Operation * o) {
        auto constant = llvm::dyn_cast<ctjs::ConstantOp>(o);
        if (!constant || !llvm::isa<ctjs::StringAttr>(constant.getValue()) ||
            constant.getResult().use_empty()) {
            return false;
        }
        for (mlir::OpOperand & use : constant.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            mlir::Value object;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                object = get.getObject();
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                object = set.getObject();
            }
            if (!object || use.getOperandNumber() != 1 || !isClosedObject(object)) { return false; }
        }
        return true;
    }

    // PHASE 57A: A DENSE ARRAY IS A `std::vector<double>` BY VALUE.
    // TypeInference::isDenseVectorSite is the proof - every use is an append
    // onto it or a read of an index or of `length`, so nothing can make it
    // sparse, nothing renames an element, and it never leaves the frame.
    static bool isVectorSite(mlir::Value v) { return TypeInference::isDenseVectorSite(v); }

    // WHY AN ARRAY LITERAL IS NOT A DENSE VECTOR: the first use that is not an
    // append or a read, named by what it is. The two sparsity routes come
    // first, because they are the ones part 24 Stage 57A names by hand and the
    // ones a reader will not expect to be refused.
    static std::string whyNotDense(mlir::Value array) {
        for (mlir::OpOperand & use : array.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0) {
                    const llvm::StringRef key = keyOf(set.getKey());
                    if (key == "length") {
                        return "an array literal whose `length` is assigned - that resizes it, "
                               "and a resize leaves holes no `std::vector` can hold";
                    }
                    if (key.empty()) {
                        return "an array literal written through an index - `a[100] = 1` gives "
                               "`length` 101 with one element, so density is not proved";
                    }
                    return ("an array literal given the named property `" + key + "`").str();
                }
                if (use.getOperandNumber() == 2) {
                    return "an array literal that escapes - it is stored into another object";
                }
            }
            if (llvm::isa<ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(user)) {
                return "an array literal with an element deleted - `delete a[0]` punches a hole "
                       "in it, so density is not proved";
            }
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0) {
                    const llvm::StringRef key = keyOf(get.getKey());
                    if (key.empty() || key == "length") { continue; }
                    return ("an array literal read through the named property `" + key + "`").str();
                }
            }
            if (llvm::isa<ctjs::AppendOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(user)) {
                return "an array literal that is loop-carried - more than one value reaches the "
                       "variable that holds it (assigned again inside a loop, or on only one "
                       "path before it)";
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                return "an array literal that escapes - it is returned";
            }
            return ("an array literal that escapes - it reaches `" +
                    user->getName().getStringRef() + "`")
                .str();
        }
        return "an array literal that is not a dense vector";
    }

    // A string constant whose every use is the `length` key of a dense array
    // lowers to nothing: the read becomes a call to the size helper.
    //
    // WITHOUT THIS ARM `counted()` REFUSES OUTRIGHT, and the reason is worth
    // stating: isKeyOnlyString requires isClosedObject, which is false for an
    // array, so the constant falls through to the ConstantOp arm and is
    // refused as "a constant that is not a number, a boolean or undefined".
    static bool isVectorKeyString(mlir::Operation * o) {
        auto constant = llvm::dyn_cast_or_null<ctjs::ConstantOp>(o);
        if (!constant || !llvm::isa<ctjs::StringAttr>(constant.getValue()) ||
            constant.getResult().use_empty()) {
            return false;
        }
        for (mlir::OpOperand & use : constant.getResult().getUses()) {
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
            if (!get || use.getOperandNumber() != 1 || !isVectorSite(get.getObject())) {
                return false;
            }
        }
        return true;
    }

    // WHY A LITERAL'S SHAPE IS OPEN: the first use that is not a get or a set
    // through a constant key on the literal itself, named by what it is. A
    // refusal that lists every route there is tells the reader nothing about
    // which one this program took; this one names it.
    //
    // THE LOOP-CARRIED ROW IS THE ONE OBLIGATION O-3 LEAVES. --ctjs-lift-to-scf
    // now replaces a loop header's argument for a variable assigned once
    // before the loop by the variable (a trivial phi), so a literal updated
    // inside a loop is one SSA value and one stack slot. What still reaches
    // the scf.while is a REAL phi: the variable is assigned again inside the
    // loop, or on only one path before it, and two literals - two shapes,
    // two slots - would have to become one value, which is a pointer.
    static std::string whyOpen(mlir::Value object) {
        for (mlir::OpOperand & use : object.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0) {
                    if (!keyOf(get.getKey()).empty()) { continue; }
                    return "an object literal reached through a dynamic key";
                }
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0) {
                    if (!keyOf(set.getKey()).empty()) { continue; }
                    return "an object literal reached through a dynamic key";
                }
                if (use.getOperandNumber() == 2) {
                    return "an object literal that escapes - it is stored into another object";
                }
            }
            if (llvm::isa<mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(user)) {
                return "an object literal that is loop-carried - more than one value reaches "
                       "the variable that holds it (assigned again inside a loop, or on only "
                       "one path before it)";
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                return "an object literal that escapes - it is returned";
            }
            return ("an object literal that escapes - it reaches `" +
                    user->getName().getStringRef() + "`")
                .str();
        }
        return "an object literal whose shape is not closed";
    }

    // A VALUE THAT LOWERS TO NOTHING NEEDS NO CARRIER, and these are the only
    // ones: the three implicit arguments (erased once their declaration
    // closures are gone), a declaration closure's result, the lift's poison
    // (replaced by NaN), a key constant (a member name) and a load_global
    // that only names a direct call's callee. function() exempts exactly this
    // list from the carrier check; retype() asks the same question.
    static bool lowersToNothing(mlir::Value v) {
        if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(v)) {
            return arg.getOwner()->isEntryBlock() &&
                   llvm::isa<ctjs::FuncOp>(arg.getOwner()->getParentOp()) && arg.getArgNumber() < 3;
        }
        mlir::Operation * o = v.getDefiningOp();
        if (isDeclarationClosure(o) || isKeyOnlyString(o) || isVectorKeyString(o)) { return true; }
        if (isLiftedClosure(o) || isUnboxedCell(o)) { return true; }
        if (o->getName().getStringRef() == "ub.poison") { return true; }
        return llvm::isa<ctjs::LoadGlobalOp>(o) && feedsOnlyDirectCallees(v);
    }

    bool op(mlir::Operation * o) {
        using namespace ctjs;
        if (llvm::isa<FrameEnterOp, FrameExitOp, RootOp>(o)) { return true; }
        if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
            if (!isClosedObject(object.getResult())) { return refuse(whyOpen(object.getResult())); }
            // The keys this literal is ever WRITTEN with. A read of one of
            // them is an own property; a read of anything else falls through
            // to the prototype, which is what makes an inherited name wrong.
            llvm::StringSet<> written;
            for (mlir::Operation * user : object.getResult().getUsers()) {
                if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                    written.insert(keyOf(set.getKey()));
                }
            }
            // The carrier each key has been STORED so far, for the one-carrier
            // check below.
            llvm::StringMap<carrier> storedCarrier;
            for (mlir::Operation * user : object.getResult().getUsers()) {
                const llvm::StringRef key = llvm::isa<GetPropertyOp>(user)
                                                ? keyOf(llvm::cast<GetPropertyOp>(user).getKey())
                                                : keyOf(llvm::cast<SetPropertyOp>(user).getKey());
                if (!isCIdentifier(key)) {
                    return refuse(("field `" + key + "` is not a C identifier").str());
                }
                if (isReservedInCpp(key)) {
                    return refuse(("field `" + key +
                                   "` is a C++ keyword or a macro of <cmath>/<cstdio>, so the "
                                   "generated struct would not compile")
                                      .str());
                }
                if (!written.contains(key) && namesObjectPrototypeMember(key)) {
                    return refuse(("field `" + key +
                                   "` is read but never written, and Object.prototype answers "
                                   "that name - the interpreter finds a function where this "
                                   "would find undefined")
                                      .str());
                }
                if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                    const carrier c = carrierOf(typeOf(set.getValue()));
                    if (c != carrier::number && c != carrier::boolean) {
                        return refuse(("field `" + key + "` is stored a " +
                                       printed(typeOf(set.getValue())) +
                                       ", not a number or a boolean")
                                          .str());
                    }
                    // PHASE 56C: A FIELD HAS ONE CARRIER, and this is the only
                    // route by which it might not. Where the field is ever
                    // READ, the join of its stores is `!ctnative.boxed` and the
                    // read is refused for having no carrier; where it never is,
                    // both stores were admitted and the field took whichever
                    // one the use-list handed over first. That was unobservable
                    // while the class was per site. It is not now: the shape
                    // key IS the field types, so two sites of one shape whose
                    // use-lists ran in different orders would disagree and
                    // split into a template that says nothing about the
                    // program.
                    const auto [entry, fresh] = storedCarrier.try_emplace(key, c);
                    if (!fresh && entry->second != c) {
                        return refuse(("field `" + key +
                                       "` is stored a number on one path and a boolean on another")
                                          .str());
                    }
                }
            }
            return true;
        }
        if (auto array = llvm::dyn_cast<CreateArrayOp>(o)) {
            if (!isVectorSite(array.getResult())) { return refuse(whyNotDense(array.getResult())); }
            // ONE FRAME SLOT, WHICH IS OBLIGATION O-4. A literal made inside an
            // `if` or a loop body would declare its vector inside that block
            // and the storage would end at the closing brace; the function's
            // own entry block is the only place a frame-scope declaration can
            // go.
            if (!llvm::isa<ctjs::FuncOp>(o->getParentOp()) || !o->getBlock()->isEntryBlock()) {
                return refuse("an array literal created inside a branch or a loop - its storage "
                              "has to be one frame slot (obligation O-4)");
            }
            if (carrierOf(typeOf(array.getResult())) != carrier::vector) {
                auto elements = llvm::dyn_cast_or_null<VecType>(typeOf(array.getResult()));
                const mlir::Type element = elements ? elements.getElementType() : mlir::Type{};
                if (carrierOf(element) == carrier::boolean) {
                    return refuse("an array of booleans - `std::vector<bool>` is a bit-packed "
                                  "specialisation whose elements are a proxy, not a `bool`");
                }
                return refuse("an array whose elements are " + printed(element) + ", not numbers");
            }
            for (mlir::Value element : array.getElements()) {
                if (!numeric(element, "array element")) { return false; }
            }
            return true;
        }
        if (auto push = llvm::dyn_cast<AppendOp>(o)) {
            if (!isVectorSite(push.getArray())) {
                return refuse("an append onto an array that is not a dense literal");
            }
            return numeric(push.getElement(), "array element");
        }
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
            if (isVectorSite(get.getObject())) {
                // `length` is `size()`, exactly, BECAUSE the site proof is what
                // rules out a hole; every other key is an index, and the index
                // has to be a number - `a[k]` with a string `k` reads a
                // property, and `a["push"]` is a function.
                if (keyOf(get.getKey()) == "length") { return true; }
                return numeric(get.getKey(), "array index");
            }
            if (!isClosedObject(get.getObject())) {
                return refuse("a property read on an object that is not a closed-shape literal");
            }
            return true; // its result's carrier is checked with every other value
        }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            // An array literal written through is not a vector site at all, so
            // the site's own diagnostic names the sparsity route rather than
            // this one naming a closed shape the program never asked for.
            if (set.getObject().getDefiningOp<CreateArrayOp>()) {
                return refuse(whyNotDense(set.getObject()));
            }
            if (!isClosedObject(set.getObject())) {
                return refuse("a property write on an object that is not a closed-shape literal");
            }
            return true; // the value's carrier was checked at the object
        }
        if (isKeyOnlyString(o) || isVectorKeyString(o)) { return true; }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o);
            load && feedsOnlyDirectCallees(load.getResult())) {
            return true;
        }
        if (auto call = llvm::dyn_cast<CallDirectOp>(o)) {
            // receiver, new.target and the callee value are dropped; every
            // argument is a number. Whether the CALLEE is native is the
            // fixpoint in runOnOperation, not a question for one function.
            const auto operands = call.getArgOperands();
            for (unsigned i = 3; i < operands.size(); ++i) {
                if (!numeric(operands[i], "argument")) { return false; }
            }
            return true;
        }
        if (isDeclarationClosure(o) || isDeclarationStore(o)) { return true; }
        // PHASE 59 SLICE 1. A lifted closure and the constant cell it captured
        // are both gone by the time the emitter sees anything; a closure that
        // could NOT be lifted carries the reason the lift wrote onto it, which
        // is what turns "`ctjs.create_closure` is not native yet" - a name for
        // a whole phase - into a work item.
        if (isLiftedClosure(o) || isUnboxedCell(o)) { return true; }
        if (llvm::isa<CreateClosureOp>(o)) { return refuse(closureRefusal(o)); }
        if (llvm::isa<CreateCellOp>(o)) {
            auto why = o->getAttrOfType<mlir::StringAttr>("ctnative.cell_reason");
            return refuse(why ? ("a captured binding that stays a cell: " + why.getValue()).str()
                              : std::string{"a captured binding that stays a cell - Phase 59"});
        }
        // THE LIFT'S UNDEFINED VALUE for a block argument no predecessor sets:
        // never read on any executed path, and carried as NaN - the double
        // that is also undefined's carrier - so it needs no proof.
        if (o->getName().getStringRef() == "ub.poison") { return true; }
        // THE STRUCTURING PASS'S MULTIPLEXERS: --ctjs-lift-to-scf encodes which
        // edge a merged block came from as i32 flags, in arith. They carry no
        // JavaScript value and --convert-arith-to-emitc lowers them.
        if (o->getDialect() != nullptr && o->getDialect()->getNamespace() == "arith") {
            for (mlir::Value v : o->getOperands()) {
                if (llvm::isa<ctjs::ValueType>(v.getType())) {
                    return refuse("an arith op on a JavaScript value");
                }
            }
            return true;
        }
        if (auto k = llvm::dyn_cast<ConstantOp>(o)) {
            if (llvm::isa<NumberAttr, BooleanAttr, UndefinedAttr>(k.getValue())) { return true; }
            return refuse("a constant that is not a number, a boolean or undefined");
        }
        if (auto b = llvm::dyn_cast<BinaryOp>(o)) {
            switch (b.getKind()) {
            case BinaryKind::Add:
            case BinaryKind::Sub:
            case BinaryKind::Mul:
            case BinaryKind::Div:
            case BinaryKind::Mod:
            case BinaryKind::Pow:
                return numeric(b.getLhs(), "binary") && numeric(b.getRhs(), "binary");
            // (`**` is not std::pow; exponentiate() below is why.)
            default: return refuse("a bitwise or string operator is not native yet");
            }
        }
        if (auto b = llvm::dyn_cast<BinaryStaticOp>(o)) {
            if (b.getKind() != BinaryKind::Add) {
                return refuse("a static bitwise operator is not native yet");
            }
            return numeric(b.getLhs(), "++") && numeric(b.getRhs(), "++");
        }
        if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
            switch (u.getKind()) {
            case UnaryKind::Neg:
            case UnaryKind::Plus: return numeric(u.getOperand(), "unary");
            case UnaryKind::Not:
                // `!x` is ToBoolean then negation, on ANY carrier: a number's
                // truthiness is exact under the NaN representation (undefined
                // and NaN are both falsy), so `!` on a number is admitted too.
                if (carrierOf(typeOf(u.getOperand())) == carrier::none) {
                    return refuse("! of " + printed(typeOf(u.getOperand())));
                }
                return true;
            default: return refuse("typeof, void and ~ are not native yet");
            }
        }
        if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
            switch (cmp.getKind()) {
            case CompareKind::Lt:
            case CompareKind::Le:
            case CompareKind::Gt:
            case CompareKind::Ge:
                return numeric(cmp.getLhs(), "compare") && numeric(cmp.getRhs(), "compare");
            case CompareKind::Eq:
            case CompareKind::StrictEq:
                return numeric(cmp.getLhs(), "equality") && numeric(cmp.getRhs(), "equality") &&
                       defined(cmp.getLhs(), "equality") && defined(cmp.getRhs(), "equality");
            }
            return refuse("an unknown comparison");
        }
        if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
            const carrier c = carrierOf(typeOf(t.getValue()));
            if (c == carrier::none) {
                return refuse("truthiness of " + printed(typeOf(t.getValue())));
            }
            return true;
        }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
            if (carrierOf(typeOf(load.getResult())) != carrier::number) {
                return refuse(("global `" + load.getName() + "` is " +
                               printed(typeOf(load.getResult())) + ", not a number")
                                  .str());
            }
            return true;
        }
        if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
            return numeric(store.getValue(), ("store to global `" + store.getName() + "`").str());
        }
        if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
            const carrier c = carrierOf(typeOf(ret.getValue()));
            if (c == carrier::none) { return refuse("returns " + printed(typeOf(ret.getValue()))); }
            if (returns == carrier::none) { returns = c; }
            if (returns != c) {
                return refuse("returns a number on one path and a boolean on another");
            }
            return true;
        }
        if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp, mlir::scf::ConditionOp,
                      mlir::scf::YieldOp>(o)) {
            return true;
        }
        if (llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(o)) {
            return refuse("unstructured control flow - run --ctjs-lift-to-scf first");
        }
        return refuse(("`" + o->getName().getStringRef() + "` is not native yet").str());
    }

    bool function(ctjs::FuncOp fn) {
        mlir::Block & entry = fn.getBody().front();
        // THE THREE IMPLICIT ARGUMENTS - receiver, new.target, callee - have no
        // native carrier and must be unused.
        for (unsigned i = 0; i < 3 && i < entry.getNumArguments(); ++i) {
            for (mlir::Operation * user : entry.getArgument(i).getUsers()) {
                // A closure that lowers to nothing does not READ these: a
                // declaration's pair is erased with its store, and a LIFTED
                // one's `$enclosing_closure` and `$enclosing_this` operands go
                // with the ctjs.call_direct that replaced its calls.
                // ResolveGlobals makes the same exemption on the same operand
                // (own_closure_escapes), for the same reason.
                if (closureLowersToNothing(user)) { continue; }
                // AND A CLOSURE THIS TIER CANNOT CARRY IS NOT THE SAME THING
                // as a function that reads its own closure. Both are uses of
                // %arg2, and this check runs before the body walk, so every
                // one of the 2,426 `uses its own closure` refusals measured
                // over the corpora was reported with the message for the wrong
                // one - on functions whose only crime is declaring a nested
                // function. The reason the lift wrote onto the closure is the
                // one a reader can act on. (A genuine reader of %arg2 - a
                // named function expression calling itself, a ctjs.load_upvalue
                // in a function nothing lifted - still gets the old sentence,
                // which is what refusal-corpus-shapes.mlir pins.)
                if (llvm::isa<ctjs::CreateClosureOp>(user)) { return refuse(closureRefusal(user)); }
                return refuse(i == 0   ? "uses `this`"
                              : i == 1 ? "uses new.target"
                                       : "uses its own closure");
            }
        }
        // PHASE 59 SLICE 1: THE LEADING PARAMETERS ARE CAPTURES, and the
        // diagnostic has to say so or it names a parameter the JavaScript does
        // not have. `ctnative.captures` is written by the lift; it is 0 on
        // every function that was not lifted, which is the shape below
        // unchanged.
        const auto capturesAttr = fn->getAttrOfType<mlir::IntegerAttr>("ctnative.captures");
        const unsigned captures = capturesAttr ? static_cast<unsigned>(capturesAttr.getInt()) : 0u;
        for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
            const mlir::Type t = typeOf(entry.getArgument(i));
            const bool isCapture = i - 3 < captures;
            const std::string which = isCapture ? "capture " + std::to_string(i - 3)
                                                : "parameter " + std::to_string(i - 3 - captures);
            if (carrierOf(t) == carrier::none) {
                // TWO CAUSES, AND THEY SEND A READER TO DIFFERENT PLACES. This
                // said "no caller proves it (a closed-world call is Phase
                // 62½-A)" for both, and for `function tag(s){return s} tag("hi")`
                // that is false in both halves: a caller DID prove it - the
                // lattice propagated `!ctnative.str<utf8>` all the way to the
                // parameter - and the closed world is not what is missing. What
                // is missing is a CARRIER for a string.
                //
                // `boxed` (or unvisited, which is a parameter no reachable call
                // ever gave a value) is the other case and keeps the old
                // wording: nothing resolved a caller, so nothing was proved.
                // All 688 parameter refusals measured over bootstrap and p5 are
                // that kind, so the corpus numbers do not move; only the proved
                // and uncarried case gets the new sentence.
                if (t == nullptr || llvm::isa<BoxedType>(t)) {
                    return refuse(which + " is " + printed(t) +
                                  " - no caller proves it (a closed-world call is Phase 62½-A)");
                }
                return refuse(which + " is " + printed(t) + ", which has no native carrier yet");
            }
        }
        bool ok = true;
        fn.getBody().walk([&](mlir::Operation * o) {
            if (!ok) { return; }
            if (o == fn.getOperation()) { return; }
            ok = op(o);
            if (!ok) { return; }
            // A declaration closure's result is dropped with its store, and
            // so is a load_global that only names a direct call's callee;
            // neither has a carrier and neither needs one.
            if (isDeclarationClosure(o) || isLiftedClosure(o) || isUnboxedCell(o)) { return; }
            if (o->getName().getStringRef() == "ub.poison") { return; }
            if (llvm::isa<ctjs::CreateObjectOp>(o) || isKeyOnlyString(o) || isVectorKeyString(o)) {
                return;
            }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o);
                load && feedsOnlyDirectCallees(load.getResult())) {
                return;
            }
            // EVERY JAVASCRIPT VALUE THIS OPERATION DEFINES OR CARRIES has a
            // carrier - including scf results and region arguments.
            for (mlir::Value r : o->getResults()) {
                if (llvm::isa<ctjs::ValueType>(r.getType()) &&
                    carrierOf(typeOf(r)) == carrier::none) {
                    ok = refuse(("a value of type " + printed(typeOf(r)) + " from `" +
                                 o->getName().getStringRef() + "`")
                                    .str());
                    return;
                }
            }
            for (mlir::Region & region : o->getRegions()) {
                for (mlir::Block & block : region) {
                    for (mlir::BlockArgument a : block.getArguments()) {
                        if (llvm::isa<ctjs::ValueType>(a.getType()) &&
                            carrierOf(typeOf(a)) == carrier::none) {
                            ok = refuse("a loop-carried value of type " + printed(typeOf(a)));
                            return;
                        }
                    }
                }
            }
        });
        return ok;
    }
};

// --- the lowering ---------------------------------------------------------------

// The C identifier a lowered function gets: the importer's `name$index` with
// the `$` made a `_`, which keeps the index (two nested `helper`s stay apart)
// and keeps clear of <cmath>'s `nan`, `remainder` and friends.
// PART 24 PHASE 63 STEP 7: a provenance comment above every generated
// definition, so a C++ diagnostic on generated code maps back to the
// JavaScript site. The importer fuses a NameLoc with the FileLineColLoc of
// the site; the first file location found, at any depth, is the site.
static std::string siteOf(mlir::Location loc) {
    std::string site;
    loc->walk([&](mlir::Location l) {
        if (auto file = llvm::dyn_cast<mlir::FileLineColLoc>(l)) {
            site = file.getFilename().str() + ":" + std::to_string(file.getLine()) + ":" +
                   std::to_string(file.getColumn());
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return site.empty() ? std::string{"<no source location>"} : site;
}
// The importer gives a ctjs.func no location of its own; its first located
// operation is the function's site.
static std::string siteOfFunction(ctjs::FuncOp fn) {
    std::string site = siteOf(fn.getLoc());
    if (site != "<no source location>") { return site; }
    fn.getBody().walk([&](mlir::Operation * o) {
        const std::string here = siteOf(o->getLoc());
        if (here == "<no source location>") { return mlir::WalkResult::advance(); }
        site = here;
        return mlir::WalkResult::interrupt();
    });
    return site;
}

std::string cIdentifier(llvm::StringRef symbol) {
    std::string name = symbol.str();
    for (char & ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) { ch = '_'; }
    }
    return name;
}

// THE DENSE-ARRAY HELPERS - part 24 Phase 57A, emitted ONLY when the unit has
// a vector site, because a preamble emitted unconditionally moves every byte
// count the printing gate reports and every line the other lits pin.
//
// THREE OF THEM AND NO MORE. `push` and `size` are what the plan's rule names;
// `at` is the one that has to exist rather than being `v[i]`, because
// `a[7]` on a three-element array is `undefined` in JavaScript and undefined
// behaviour in C++, and undefined is this tier's NaN. Every out-of-range,
// fractional or negative index therefore answers NaN, which is EXACTLY what
// the element type says it may be - the join starts from `undefined` for this
// reason (TypeInference::elementTypeOf).
//
// EACH ONE UNDER ITS OWN PROVENANCE COMMENT, which is Phase 63 Step 7's rule
// for a generated definition, and `inline` so no translation unit that
// includes none of them warns about one.
constexpr llvm::StringLiteral kVectorHelpers =
    "// ctcompile: the dense-array helpers - part 24 Phase 57A\n"
    "namespace ctnative {\n"
    "// ctcompile: `a[i]`, whose out-of-range answer is undefined, which is NaN "
    "here\n"
    "inline double vec_at(const std::vector<double> & v, double i) {\n"
    "  if (!(i >= 0.0) || i != std::trunc(i) ||\n"
    "      i >= static_cast<double>(v.size())) {\n"
    "    return NAN;\n"
    "  }\n"
    "  return v[static_cast<std::vector<double>::size_type>(i)];\n"
    "}\n"
    "// ctcompile: `a.length`, which is `size()` exactly - the site proof is "
    "what rules out a hole\n"
    "inline double vec_length(const std::vector<double> & v) {\n"
    "  return static_cast<double>(v.size());\n"
    "}\n"
    "// ctcompile: one element of an array literal, in source order\n"
    "inline void vec_push(std::vector<double> & v, double x) {\n"
    "  v.push_back(x);\n"
    "}\n"
    "} // namespace ctnative";

// THE DECLARATIVE RULE'S PATTERN SET, BUILT ONCE. Freezing a PDL pattern
// compiles its bytecode, which is not something to redo per function - and
// FrozenRewritePatternSet is what both greedy entry points take.
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context) {
    mlir::RewritePatternSet patterns(context);
    populateGeneratedPDLLPatterns(patterns);
    return patterns;
}

struct lowering {
    mlir::DataFlowSolver & solver;
    mlir::MLIRContext * context;
    mlir::ModuleOp module;
    // UnaryPlusIsIdentity.pdll, frozen. Declared here so the member
    // initialisation order matches the list below and -Wreorder stays quiet.
    mlir::FrozenRewritePatternSet declarative;
    lowering(mlir::DataFlowSolver & s, mlir::MLIRContext * c, mlir::ModuleOp m)
        : solver(s), context(c), module(m), declarative(declarativePatterns(c)) {}
    llvm::StringSet<> globals; // numeric globals the emitted unit declares
    // ctjs symbol -> emitc symbol, decided for EVERY accepted function before
    // any is lowered, so a call lowered before its callee already names the
    // callee's new symbol and no symbol use is ever rewritten in place. A
    // rewrite would also reach a call_direct in a REFUSED caller, which must
    // keep naming a ctjs.func to verify.
    llvm::StringMap<std::string> names;
    // The hollowed ctjs.funcs, erased together in finish().
    llvm::SmallVector<ctjs::FuncOp> shells;
    // PHASE 56C: ONE SHAPE IS ONE DEFINITION, PROGRAM-WIDE.
    //
    // 56B emitted one class per creation SITE, so the shipped struct fixture
    // had seven classes for six shapes and two of them were byte for byte the
    // same. The key is the SHAPE instead - the ordered list of (field name,
    // field type) - so two sites with the same key get the same type and the
    // same NAME. That is what makes a generated struct passable between
    // functions at all, and it is the naming prerequisite for every later
    // phase.
    //
    // WHERE THE NAMES MATCH AND A TYPE DIFFERS, THE DEFINITION IS A TEMPLATE.
    // `{hit: false, at: 0}` and `{hit: 0, at: 0}` are one FAMILY at two
    // instantiations: `template <class T0> class ctn_at_hit { double at; T0
    // hit; };`. Only the positions the family disagrees on become parameters -
    // a position every site agrees on keeps its concrete type, which is more
    // information and not less, and a family that agrees everywhere is not a
    // template at all. NOT a variant field: each site is monomorphic and only
    // the union of the sites is not, so a variant would put a `std::visit` in
    // front of every read at both sites to pay for a polymorphism neither site
    // has (part 24 Phase 56C, steps 2 and 3).
    //
    // THE REACHABLE DOMAIN IS TWO CARRIERS, AND THE PLAN'S OWN EXAMPLE IS NOT
    // EXPRESSIBLE. A field is a number or a boolean - obligation O-2, enforced
    // by admission - so the only disagreement this tier can build a template
    // over today is `double` against `bool`. Phase 56C's written example, "the
    // same {x, y} literal at three sites, two numeric and one string", cannot
    // be written: a string has no carrier here, and `field `x` is stored a
    // !ctnative.str<utf8>, not a number or a boolean` refuses the function
    // before any shape is formed. The mechanism below is general over the
    // field types; the fixture that exercises it has to be a boolean.
    struct family {
        llvm::SmallVector<std::string> fields;     // the field names, sorted - the family key
        llvm::SmallVector<mlir::Type> types;       // the first site's carrier, per position
        llvm::BitVector varies;                    // a later site disagreed at this position
        llvm::SmallVector<std::string> parameters; // per position: "" or the template parameter
        llvm::SmallVector<std::string> where;      // the JavaScript sites, first sight first
        unsigned instantiations = 0;               // distinct (name, type) keys in this family
        std::string name;                          // ctn_at_hit, unique across the module
    };
    // ONE SITE: which family it belongs to, and the carriers ITS fields took.
    // The types are per site and the names are per family, which is the whole
    // of 56C in two lines.
    struct siteShape {
        unsigned family = 0;
        llvm::SmallVector<mlir::Type> types;
    };
    // N = 0: `family` is 320 bytes and SmallVector's default inline count
    // static_asserts above 256. There is one of these per distinct shape in the
    // whole program, so inline storage would buy nothing anyway.
    llvm::SmallVector<family, 0> families;
    llvm::StringMap<unsigned> familyIndex;          // the joined field names -> index into families
    llvm::DenseMap<mlir::Value, siteShape> shapeOf; // create_object result -> its site
    // Decided while the IR is still ctjs: by the time a key constant or an
    // access is replaced, the object it keys is already an emitc.variable and
    // no longer reads as a closed create_object.
    llvm::DenseMap<mlir::Operation *, std::string> accessKey; // get/set -> member name
    llvm::DenseSet<mlir::Operation *> keyConstants;           // constants that lower to nothing
    // PHASE 57A. Decided while the IR is still ctjs, for fieldsOf()'s reason:
    // by the time a read is replaced, the array it reads is already an
    // emitc.variable and no longer reads as a dense create_array.
    llvm::DenseSet<mlir::Operation *> vectorLengthReads;
    llvm::DenseSet<mlir::Operation *> vectorIndexReads;
    // Set by the first array lowered; the include and the helper preamble ride
    // on it. An empty unit emits neither.
    bool needsVector = false;

    // The C++ spelling of a field carrier, and there are two of them: a field
    // is a number or a boolean (O-2) and admission refuses everything else by
    // name. Needed because a template ARGUMENT is text - `ctn_at_hit<bool>` -
    // where a field's type is an mlir::Type the emitter prints.
    static const char * spelled(mlir::Type type) {
        if (llvm::isa<mlir::Float64Type>(type)) { return "double"; }
        if (auto integer = llvm::dyn_cast_or_null<mlir::IntegerType>(type);
            integer && integer.getWidth() == 1) {
            return "bool";
        }
        llvm::report_fatal_error("ctnative lowering: a struct field whose carrier is neither a "
                                 "double nor a bool - admission should have refused it");
    }
    // THE TYPE OF ONE SITE. A family that agrees everywhere is spelled by its
    // name alone; one that does not is that name with an argument for each
    // position it disagrees on, in field order.
    std::string spelling(const siteShape & site) const {
        const family & f = families[site.family];
        if (!f.varies.any()) { return f.name; }
        std::string out = f.name + "<";
        for (unsigned i = 0, written = 0; i < f.fields.size(); ++i) {
            if (!f.varies[i]) { continue; }
            if (written++ != 0) { out += ", "; }
            out += spelled(site.types[i]);
        }
        return out + ">";
    }
    mlir::Type classType(const siteShape & site) {
        return ec::LValueType::get(ec::OpaqueType::get(context, spelling(site)));
    }
    // THE MEMBER NAME OF ONE ACCESS, and a named fatal rather than
    // `accessKey.at(o)`. `at` on a key that is not there THROWS, and this
    // process cannot catch it: an access whose object never went through the
    // shape census would have been an uncaught exception with no message
    // naming the invariant. The invariant does hold - fieldsOf() records every
    // get and set on a literal that passed the closed-shape proof, and
    // admission refuses a property access on anything else - but it held by an
    // argument and not by a check, which is the difference this makes.
    [[nodiscard]] llvm::StringRef memberName(mlir::Operation * access) const {
        const auto entry = accessKey.find(access);
        if (entry == accessKey.end()) {
            llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") +
                                     access->getName().getStringRef() +
                                     "` has no recorded member name - it reads or writes an "
                                     "object the shape census never saw, and admission should "
                                     "have refused the function for a property access on "
                                     "something that is not a closed-shape literal");
        }
        return entry->second;
    }
    [[nodiscard]] const siteShape & shapeAt(mlir::Value object) const {
        const auto entry = shapeOf.find(object);
        if (entry == shapeOf.end()) {
            llvm::report_fatal_error("ctnative lowering: a closed object literal that the shape "
                                     "census never saw - censusShapes() runs over the whole "
                                     "accepted set before any function is lowered");
        }
        return entry->second;
    }

    // The fields of one closed object literal: every key read or written,
    // sorted by name, with the carrier of the field's inferred type (the join
    // of its stores, which every read carries); a key only ever read is
    // undefined, carried as NaN.
    //
    // A STORE DECIDES THE FIELD TYPE, AND A READ ONLY WHERE THERE IS NO STORE.
    // This used to take whichever user the use-list happened to hand over
    // first, which was harmless when the class was per site and is not now: the
    // shape key IS the field types, so two sites of the same shape whose
    // use-lists ran in different orders could disagree and split into a
    // template that says nothing. Admission refuses a field stored two
    // different carriers, so "any store" and "the join of the stores" are the
    // same answer here.
    //
    // AND IT READS THE LATTICE, NOT THE IR. This ran inside retype(), after
    // every value in the function had already taken its carrier, so it could
    // read `get.getResult().getType()`. The census runs before any of that and
    // asks the solver the question retype() would have asked.
    llvm::SmallVector<std::pair<std::string, mlir::Type>> fieldsOf(ctjs::CreateObjectOp object) {
        const auto carried = [&](mlir::Value v) {
            return carrierType(context, carrierOf(typeOf(v)));
        };
        llvm::StringMap<mlir::Type> stored;
        llvm::StringMap<mlir::Type> read;
        for (mlir::Operation * user : object.getResult().getUsers()) {
            mlir::Value key;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                key = get.getKey();
                read.try_emplace(admission::keyOf(key), carried(get.getResult()));
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                key = set.getKey();
                stored.try_emplace(admission::keyOf(key), carried(set.getValue()));
            }
            if (key) {
                accessKey[user] = admission::keyOf(key).str();
                keyConstants.insert(key.getDefiningOp());
            }
        }
        llvm::SmallVector<std::pair<std::string, mlir::Type>> fields;
        for (const auto & entry : stored) {
            fields.emplace_back(entry.getKey().str(), entry.getValue());
        }
        for (const auto & entry : read) {
            if (!stored.contains(entry.getKey())) {
                fields.emplace_back(entry.getKey().str(), entry.getValue());
            }
        }
        llvm::sort(fields, [](const auto & a, const auto & b) { return a.first < b.first; });
        return fields;
    }

    // THE SHAPE CENSUS, over the whole accepted set and BEFORE any function is
    // lowered - for the same reason the global census in runOnOperation() runs
    // there. A site's TYPE is `ctn_at_hit<bool>`, and WHICH positions are
    // template parameters is a property of every site in the program, so no
    // site can be spelled until all of them have been seen.
    void censusShapes(llvm::ArrayRef<ctjs::FuncOp> accepted) {
        std::vector<std::set<std::string>> keys; // per family, its distinct (name, type) keys
        for (ctjs::FuncOp fn : accepted) {
            fn.getBody().walk([&](ctjs::CreateObjectOp object) {
                const auto fields = fieldsOf(object);
                std::string nameKey; // the family key: just the names
                std::string typeKey; // the instantiation key: names AND types
                siteShape site;
                for (const auto & field : fields) {
                    nameKey += field.first;
                    nameKey.push_back('\0'); // no field name can contain one
                    typeKey += field.first;
                    typeKey += ':';
                    typeKey += spelled(field.second);
                    typeKey.push_back('\0');
                    site.types.push_back(field.second);
                }
                const auto [entry, fresh] =
                    familyIndex.try_emplace(nameKey, static_cast<unsigned>(families.size()));
                site.family = entry->second;
                if (fresh) {
                    family made;
                    for (const auto & field : fields) { made.fields.push_back(field.first); }
                    made.types = site.types;
                    made.varies = llvm::BitVector(static_cast<unsigned>(fields.size()), false);
                    families.push_back(std::move(made));
                    keys.emplace_back();
                } else {
                    family & f = families[site.family];
                    for (unsigned i = 0; i < f.types.size(); ++i) {
                        if (f.types[i] != site.types[i]) { f.varies.set(i); }
                    }
                }
                families[site.family].where.push_back(siteOf(object.getLoc()));
                keys[site.family].insert(typeKey);
                shapeOf[object.getResult()] = std::move(site);
            });
        }
        for (unsigned i = 0; i < families.size(); ++i) {
            families[i].instantiations = static_cast<unsigned>(keys[i].size());
        }
        nameFamilies();
    }

    // THE NAME IS THE SHAPE, so the same fields name the same type wherever
    // they are written: `{x, y}` is `ctn_x_y` in every function in the program.
    void nameFamilies() {
        llvm::StringSet<> taken;
        for (family & f : families) {
            std::string joined = "ctn";
            for (const std::string & field : f.fields) { joined += "_" + field; }
            // A LITERAL WITH NO FIELDS STILL NEEDS A NAME, and `ctn_` is not
            // one. `var e = {};` is admitted today - hasClosedShape's loop over
            // the uses of a literal with none is vacuously true - so this arm
            // is reachable and is what keeps the empty shape a plain class.
            if (f.fields.empty()) { joined += "_empty"; }
            // A DOUBLE UNDERSCORE IS RESERVED TO THE IMPLEMENTATION IN EVERY
            // SCOPE, and a JavaScript field named `_x` would put one here.
            std::string squeezed;
            for (char ch : joined) {
                if (ch == '_' && !squeezed.empty() && squeezed.back() == '_') { continue; }
                squeezed.push_back(ch);
            }
            // TWO FAMILIES CAN STILL WANT ONE NAME. `{a_b}` and `{a, b}` both
            // join to `ctn_a_b`, because every character a field name may
            // contain is also the character the separator is; and the squeeze
            // above makes `{a, _b}` a third. The second family to ask gets
            // `_2`. Without this the module holds two `emitc.class @ctn_a_b`
            // and the symbol-table verifier rejects it - which is how the guard
            // is proved.
            f.name = squeezed;
            for (unsigned n = 2; !taken.insert(f.name).second; ++n) {
                f.name = squeezed + "_" + std::to_string(n);
            }
            // THE TEMPLATE PARAMETERS, one per position the family disagrees on
            // and none at all where it agrees. A parameter may NOT be named for
            // a field: `template <class T0> class C { double T0; };` is
            // "declaration of 'T0' shadows template parameter", and `{T0: 1}`
            // is a perfectly ordinary JavaScript object.
            f.parameters.assign(f.fields.size(), std::string{});
            for (unsigned i = 0, next = 0; i < f.fields.size(); ++i) {
                if (!f.varies[i]) { continue; }
                std::string parameter;
                do {
                    parameter = "T" + std::to_string(next++);
                } while (llvm::is_contained(f.fields, parameter));
                f.parameters[i] = parameter;
            }
        }
    }

    // PART 24 PHASE 63 STEP 7, WITH ONE DEFINITION FOR MANY SITES. Every
    // generated definition sits under a comment naming its JavaScript site;
    // a shape written at eleven places has eleven of them, so the comment
    // names the first three, says how many there are, and says how many
    // instantiations a template has. A reader who arrives at the class from a
    // C++ diagnostic gets somewhere to start AND the fact that there are
    // others - which a single site silently chosen from the eleven would hide.
    [[nodiscard]] std::string provenanceOf(const family & f) const {
        std::string out = "object literal at ";
        const size_t shown = std::min<size_t>(3, f.where.size());
        for (size_t i = 0; i < shown; ++i) {
            if (i != 0) { out += ", "; }
            out += f.where[i];
        }
        if (f.where.size() > shown) {
            out += " and " + std::to_string(f.where.size() - shown) + " more";
        }
        if (f.where.size() > 1) {
            out += " (" + std::to_string(f.where.size()) + " sites";
            if (f.varies.any()) {
                out += ", " + std::to_string(f.instantiations) + " instantiations";
            }
            out += ")";
        }
        return out;
    }

    // The reads of one dense array, sorted into `length` and index, and the
    // `length` key constants marked as lowering to nothing - exactly what
    // fieldsOf() does for a closed object's member names.
    void collectVector(ctjs::CreateArrayOp array) {
        needsVector = true;
        for (mlir::Operation * user : array.getResult().getUsers()) {
            auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
            if (!get) { continue; }
            if (admission::keyOf(get.getKey()) == "length") {
                vectorLengthReads.insert(user);
                // AND THE KEY CONSTANT IS NOT MARKED, WHICH WAS MEASURED. The
                // obvious thing here is `keyConstants.insert(...)`, so that
                // replace() drops the `"length"` constant rather than swapping
                // a NaN double in for it - which is what fieldsOf() does for
                // a member name. It makes no difference: the `vec_length` call
                // built below takes the ARRAY and not the key, so whatever
                // replace() leaves behind has no users and lower()'s own sweep
                // erases it. Adding the line changed not one byte of the
                // emitted C++ and no test could be made to fail without it, so
                // it is not here.
            } else {
                vectorIndexReads.insert(user);
            }
        }
    }

    void finish() {
        // PROTOTYPES FIRST. main is the importer's function 0 and is emitted
        // first, and C++ needs a declaration before a use; one
        // emitc.declare_func per lowered function, at the top of the module
        // after the includes, is what the emitter prints as a prototype.
        {
            mlir::OpBuilder b(context);
            b.setInsertionPointToStart(module.getBody());
            llvm::SmallVector<ec::FuncOp> lowered;
            module.walk([&](ec::FuncOp f) {
                if (f.getSymName() != "main") { lowered.push_back(f); }
            });
            // After the includes, which declareGlobals put first.
            for (mlir::Operation & op : module.getBody()->getOperations()) {
                if (!llvm::isa<ec::IncludeOp>(op)) {
                    b.setInsertionPoint(&op);
                    break;
                }
            }
            // THE CLASSES FIRST, one per SHAPE and no longer one per site
            // (Phase 56C), public fields only: emitc.class prints exactly that.
            // A family that disagrees on a field type carries its parameter
            // list as an attribute and its varying fields as the parameters -
            // the fork's emitter is the one consumer, like every other
            // ctcompile divergence in it.
            for (const family & f : families) {
                auto cls = ec::ClassOp::create(b, module.getLoc(), f.name);
                cls->setAttr("ctnative.provenance", b.getStringAttr(provenanceOf(f)));
                if (f.varies.any()) {
                    llvm::SmallVector<mlir::Attribute> parameters;
                    for (unsigned i = 0; i < f.fields.size(); ++i) {
                        if (f.varies[i]) { parameters.push_back(b.getStringAttr(f.parameters[i])); }
                    }
                    cls->setAttr("ctnative.template_params", b.getArrayAttr(parameters));
                }
                mlir::Block & body = cls.getBody().emplaceBlock();
                mlir::OpBuilder inside = mlir::OpBuilder::atBlockEnd(&body);
                for (unsigned i = 0; i < f.fields.size(); ++i) {
                    const mlir::Type type =
                        f.varies[i] ? ec::OpaqueType::get(context, f.parameters[i]) : f.types[i];
                    ec::FieldOp::create(inside, module.getLoc(), f.fields[i], type,
                                        mlir::Attribute{});
                }
            }
            for (ec::FuncOp f : lowered) {
                ec::DeclareFuncOp::create(b, f.getLoc(),
                                          mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
            }
        }
        for (ctjs::FuncOp fn : shells) {
            if (!mlir::SymbolTable::symbolKnownUseEmpty(fn.getOperation(), module)) {
                llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                         "` still has symbol uses after every accepted function "
                                         "was lowered - the call-graph closure should have "
                                         "refused its caller");
            }
            fn.erase();
        }
        shells.clear();
    }

    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const {
        const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
        return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
    }

    mlir::Value f64Constant(mlir::OpBuilder & b, mlir::Location where, double d) {
        return ec::ConstantOp::create(b, where, mlir::Float64Type::get(context),
                                      b.getF64FloatAttr(d));
    }
    mlir::Value boolConstant(mlir::OpBuilder & b, mlir::Location where, bool v) {
        return ec::ConstantOp::create(
            b, where, mlir::IntegerType::get(context, 1),
            b.getIntegerAttr(mlir::IntegerType::get(context, 1), v ? 1 : 0));
    }
    mlir::Value lvalueOfGlobal(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef name) {
        globals.insert(name);
        return ec::GetGlobalOp::create(b, where,
                                       ec::LValueType::get(mlir::Float64Type::get(context)),
                                       mlir::FlatSymbolRefAttr::get(context, ("g_" + name).str()));
    }
    // A number's truthiness, exactly: not zero AND not NaN. `x == x` is the
    // NaN test, and NaN carries undefined too, which is also falsy.
    mlir::Value truthyNumber(mlir::OpBuilder & b, mlir::Location where, mlir::Value x) {
        const auto i1 = mlir::IntegerType::get(context, 1);
        mlir::Value nonzero =
            ec::CmpOp::create(b, where, i1, ec::CmpPredicate::ne, x, f64Constant(b, where, 0.0));
        mlir::Value notNaN = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, x, x);
        return ec::LogicalAndOp::create(b, where, i1, nonzero, notNaN);
    }
    // ZERO RESULTS, AND THAT IS THE WHOLE POINT. A call with one result that
    // nothing reads is declared as a variable by the emitter, which is
    // -Werror=unused-variable on a file this tier promises compiles clean; the
    // fork's `ctnative.statement` attribute exists for the calls that cannot
    // avoid it, and --ctnative-prune-dead-stores deliberately will not erase a
    // call to tidy up after one. A push has nothing to return, so it returns
    // nothing.
    void push(mlir::OpBuilder & b, mlir::Location where, mlir::Value into, mlir::Value element) {
        ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, b.getStringAttr("ctnative::vec_push"),
                                 mlir::ValueRange{into, element});
    }

    mlir::Value libmCall(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef fn,
                         mlir::ValueRange args) {
        return ec::CallOpaqueOp::create(b, where, mlir::TypeRange{mlir::Float64Type::get(context)},
                                        b.getStringAttr(fn), args)
            .getResult(0);
    }

    // JAVASCRIPT'S `**`, WHICH IS NOT C++'s std::pow.
    //
    // Number::exponentiate answers NaN when the base has magnitude one and the
    // exponent is NaN or infinite; C++ answers 1 for pow(1, NaN) and
    // pow(1, INFINITY). Everywhere else the two agree, including pow(NaN, 0)
    // == 1. So the whole difference is one guard, and emitting it is better
    // than refusing the operator: `2 ** 31` keeps working and `1 ** undefined`
    // stops being wrong. Undefined IS this tier's NaN, which is what made the
    // difference reachable from ordinary JavaScript rather than only from a
    // literal NaN.
    //
    // StdLibMap.td already classifies the library spelling `Math.pow` as
    // Divergent with this exact witness; this is the operator path catching up.
    mlir::Value exponentiate(mlir::OpBuilder & b, mlir::Location where, mlir::Value base,
                             mlir::Value exponent) {
        const auto i1 = mlir::IntegerType::get(context, 1);
        mlir::Value magnitude = libmCall(b, where, "std::fabs", {base});
        mlir::Value isOne = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, magnitude,
                                              f64Constant(b, where, 1.0));
        mlir::Value finite =
            ec::CallOpaqueOp::create(b, where, mlir::TypeRange{i1},
                                     b.getStringAttr("std::isfinite"), mlir::ValueRange{exponent})
                .getResult(0);
        mlir::Value notFinite = ec::LogicalNotOp::create(b, where, i1, finite);
        mlir::Value diverges = ec::LogicalAndOp::create(b, where, i1, isOne, notFinite);
        return ec::ConditionalOp::create(
            b, where, mlir::Float64Type::get(context), diverges,
            f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()),
            libmCall(b, where, "std::pow", {base, exponent}));
    }

    // Retype every JavaScript value in the function from the lattice. Done
    // BEFORE any operation is replaced, so the replacements see carriers.
    void retype(ctjs::FuncOp fn) {
        const auto retypeValue = [&](mlir::Value v) {
            if (!llvm::isa<ctjs::ValueType>(v.getType())) { return; }
            // A closed object keeps its ctjs type until its shape is known
            // below; everything else takes its carrier now.
            if (admission::isClosedObject(v)) { return; }
            const carrier c = carrierOf(typeOf(v));
            // A DENSE ARRAY TAKES ITS OWN CARRIER, which is not one of the two
            // scalars carrierType() can spell: `std::vector<double>`, by value,
            // in this frame.
            if (c == carrier::vector) {
                v.setType(vectorCarrierType(context));
                return;
            }
            // NO CARRIER IS FATAL, NOT A DOUBLE. This fell through to f64
            // for anything that was not a boolean, so a value admission
            // never looked at - a boxed object threaded through a loop, say
            // - would have been retyped to a number and lowered as one, and
            // the miscompile would have surfaced as a wrong answer at the
            // gate rather than here. Admission refuses every such function;
            // reaching this line is a bug in admission, and says so.
            if (c == carrier::none) {
                if (!admission::lowersToNothing(v)) {
                    llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                             "` holds a value of type " + printed(typeOf(v)) +
                                             " that has no native carrier - admission should "
                                             "have refused it (a literal that reaches a loop is "
                                             "obligation O-3, and whyOpen names it)");
                }
                // A PLACEHOLDER, AND ONLY FOR VALUES THAT ARE ABOUT TO BE
                // ERASED: the lift's poison, a key constant, a declaration
                // closure, the three implicit arguments. Nothing ever reads
                // this type - replace() removes each of them - but the IR has
                // to stay verifiable in between, and `!ctjs.value` among
                // retyped operands does not. It is a double for the same
                // reason `undefined` is: it is the type this tier can always
                // spell.
                //
                // It is written HERE rather than left to carrierType(), which
                // now aborts on a carrier it cannot represent. That default
                // used to answer f64 for everything non-boolean, and the
                // difference matters: a value admission never looked at got a
                // representation and was lowered as a number, so the
                // miscompile surfaced as a wrong answer at the gate instead of
                // as a diagnostic here.
                v.setType(mlir::Float64Type::get(context));
                return;
            }
            v.setType(carrierType(context, c));
        };
        for (mlir::Block & block : fn.getBody()) {
            for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
        }
        fn.getBody().walk([&](mlir::Operation * o) {
            for (mlir::Value r : o->getResults()) { retypeValue(r); }
            for (mlir::Region & region : o->getRegions()) {
                for (mlir::Block & block : region) {
                    for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
                }
            }
        });
        // NOW THE SHAPES. The census gave every closed literal in the module
        // its family before anything was lowered - a site's spelling depends on
        // every OTHER site in the program - so here each object only takes the
        // type of its own site.
        fn.getBody().walk([&](ctjs::CreateObjectOp object) {
            mlir::Value(object.getResult()).setType(classType(shapeAt(object.getResult())));
        });
        // AND THE ARRAYS, whose type was taken above; what is left is which
        // reads are `length` and which are indices.
        fn.getBody().walk([&](ctjs::CreateArrayOp array) { collectVector(array); });
    }

    // THE ONLY ERASE. An operation with uses is never erased: in a release
    // build that is a use-after-free with no diagnostic - it surfaced as a
    // crash at context teardown that came and went with the heap layout.
    static void eraseIfUnused(mlir::Operation * o) {
        if (!o->use_empty()) {
            llvm::report_fatal_error(llvm::Twine("ctnative lowering: erasing `") +
                                     o->getName().getStringRef() + "` while it still has uses");
        }
        o->erase();
    }

    void replace(mlir::Operation * o, bool isEntry, mlir::Type returnType) {
        using namespace ctjs;
        mlir::OpBuilder b(o);
        const mlir::Location where = o->getLoc();
        const auto f64 = mlir::Float64Type::get(context);
        const auto i1 = mlir::IntegerType::get(context, 1);
        const auto swap = [&](mlir::Value with) {
            o->getResult(0).replaceAllUsesWith(with);
            eraseIfUnused(o);
        };

        // FRAME BOOKKEEPING LOWERS TO NOTHING - but frame_enter's result is
        // used by every frame_exit and root after it, and walk order visits
        // it first, so its users go now and it goes in the sweep at the end.
        if (llvm::isa<FrameExitOp, RootOp>(o)) {
            eraseIfUnused(o);
            return;
        }
        if (llvm::isa<FrameEnterOp>(o)) { return; }
        if (o->getName().getStringRef() == "ub.poison") {
            swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
            return;
        }
        if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
            // The struct, by value, in this frame; every field set to its
            // undefined - NaN for a number, false for a boolean - before the
            // first store, so a read before a write is exact.
            const siteShape & site = shapeAt(object.getResult());
            const family & f = families[site.family];
            mlir::Value local =
                ec::VariableOp::create(b, where, classType(site), ec::OpaqueAttr::get(context, ""));
            // THE MEMBER TAKES THE SITE'S CONCRETE CARRIER, never the family's
            // template parameter: the assign and the load after it are typed
            // ops over a `double` or a `bool`, and `T0` is a spelling that
            // exists only inside the class.
            for (unsigned i = 0; i < f.fields.size(); ++i) {
                const mlir::Type type = site.types[i];
                mlir::Value member =
                    ec::MemberOp::create(b, where, ec::LValueType::get(type), f.fields[i], local);
                mlir::Value init =
                    llvm::isa<mlir::IntegerType>(type)
                        ? boolConstant(b, where, false)
                        : f64Constant(b, where, std::numeric_limits<double>::quiet_NaN());
                ec::AssignOp::create(b, where, member, init);
            }
            swap(local);
            return;
        }
        if (auto array = llvm::dyn_cast<CreateArrayOp>(o)) {
            // The vector, by value, in this frame - default-constructed, which
            // is the empty array the appends below fill.
            mlir::Value local = ec::VariableOp::create(b, where, vectorCarrierType(context),
                                                       ec::OpaqueAttr::get(context, ""));
            for (mlir::Value element : array.getElements()) { push(b, where, local, element); }
            swap(local);
            return;
        }
        if (auto append = llvm::dyn_cast<AppendOp>(o)) {
            push(b, where, append.getArray(), append.getElement());
            eraseIfUnused(o);
            return;
        }
        if (vectorLengthReads.contains(o)) {
            swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{f64},
                                          b.getStringAttr("ctnative::vec_length"),
                                          mlir::ValueRange{o->getOperand(0)})
                     .getResult(0));
            return;
        }
        if (vectorIndexReads.contains(o)) {
            swap(ec::CallOpaqueOp::create(b, where, mlir::TypeRange{f64},
                                          b.getStringAttr("ctnative::vec_at"),
                                          mlir::ValueRange{o->getOperand(0), o->getOperand(1)})
                     .getResult(0));
            return;
        }
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
            const mlir::Type type = get.getResult().getType();
            mlir::Value member = ec::MemberOp::create(b, where, ec::LValueType::get(type),
                                                      memberName(o), get.getObject());
            swap(ec::LoadOp::create(b, where, type, member));
            return;
        }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            mlir::Value member =
                ec::MemberOp::create(b, where, ec::LValueType::get(set.getValue().getType()),
                                     memberName(o), set.getObject());
            ec::AssignOp::create(b, where, member, set.getValue());
            eraseIfUnused(o);
            return;
        }
        if (keyConstants.contains(o)) { return; } // dead after its get/set; swept
        if (admission::isDeclarationStore(o)) {
            mlir::Operation * closure = llvm::cast<StoreGlobalOp>(o).getValue().getDefiningOp();
            eraseIfUnused(o);
            eraseIfUnused(closure);
            return;
        }
        if (auto k = llvm::dyn_cast<ConstantOp>(o)) {
            if (auto n = llvm::dyn_cast<NumberAttr>(k.getValue())) {
                swap(f64Constant(b, where, n.getDouble()));
            } else if (auto bo = llvm::dyn_cast<BooleanAttr>(k.getValue())) {
                swap(boolConstant(b, where, bo.getValue()));
            } else {
                // undefined, as the NaN the representation table says it is.
                swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
            }
            return;
        }
        if (auto bin = llvm::dyn_cast<BinaryOp>(o)) {
            const mlir::Value l = bin.getLhs(), r = bin.getRhs();
            switch (bin.getKind()) {
            case BinaryKind::Add: swap(ec::AddOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Sub: swap(ec::SubOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Mul: swap(ec::MulOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Div: swap(ec::DivOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Mod: swap(libmCall(b, where, "std::fmod", {l, r})); return;
            case BinaryKind::Pow: swap(exponentiate(b, where, l, r)); return;
            default: llvm_unreachable("admission refused it");
            }
        }
        if (auto bin = llvm::dyn_cast<BinaryStaticOp>(o)) {
            swap(ec::AddOp::create(b, where, f64, bin.getLhs(), bin.getRhs()));
            return;
        }
        if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
            switch (u.getKind()) {
            case UnaryKind::Neg:
                swap(ec::UnaryMinusOp::create(b, where, f64, u.getOperand()));
                return;
            // `+x` IS GONE BY NOW, ERASED BY UnaryPlusIsIdentity.pdll in
            // applyDeclarativeRules() above. This arm is not dead code and it
            // is not llvm_unreachable: PDL has NO DIAGNOSTIC ON A NON-MATCH, so
            // a pattern that silently stopped firing - a rename in CTJSOps.td,
            // a guard the constraint gets wrong, a driver that never ran - would
            // otherwise reach the default arm and abort with a message blaming
            // admission. Naming the file that owed the rewrite is the whole
            // difference between a bug report and a wild goose chase.
            case UnaryKind::Plus:
                llvm::report_fatal_error("ctnative lowering: a `ctjs.unary plus` reached replace() "
                                         "- UnaryPlusIsIdentity.pdll was supposed to have erased "
                                         "it, and PDL does not report a non-match");
            case UnaryKind::Not: {
                mlir::Value v = u.getOperand();
                if (!llvm::isa<mlir::IntegerType>(v.getType())) { v = truthyNumber(b, where, v); }
                swap(ec::LogicalNotOp::create(b, where, i1, v));
                return;
            }
            default: llvm_unreachable("admission refused it");
            }
        }
        if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
            ec::CmpPredicate p = ec::CmpPredicate::eq;
            switch (cmp.getKind()) {
            case CompareKind::Lt: p = ec::CmpPredicate::lt; break;
            case CompareKind::Le: p = ec::CmpPredicate::le; break;
            case CompareKind::Gt: p = ec::CmpPredicate::gt; break;
            case CompareKind::Ge: p = ec::CmpPredicate::ge; break;
            case CompareKind::Eq:
            case CompareKind::StrictEq: p = ec::CmpPredicate::eq; break;
            }
            swap(ec::CmpOp::create(b, where, i1, p, cmp.getLhs(), cmp.getRhs()));
            return;
        }
        if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
            mlir::Value v = t.getValue();
            swap(llvm::isa<mlir::IntegerType>(v.getType()) ? v : truthyNumber(b, where, v));
            return;
        }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
            if (admission::feedsOnlyDirectCallees(load.getResult())) {
                // Every use is a call_direct's callee-value operand, and the
                // call is rewritten below without it; by the sweep it is dead.
                return;
            }
            swap(ec::LoadOp::create(b, where, f64, lvalueOfGlobal(b, where, load.getName())));
            return;
        }
        if (auto call = llvm::dyn_cast<CallDirectOp>(o)) {
            // The three implicit operands go; the rest are the parameters,
            // in the callee's own order. The callee symbol still names the
            // ctjs.func here; SymbolTable::replaceAllSymbolUses renames it
            // to the emitc.func when that function is lowered, whichever
            // order the two are visited in.
            const auto operands = call.getArgOperands();
            llvm::SmallVector<mlir::Value> args(operands.begin() + 3, operands.end());
            const auto named = names.find(call.getCallee());
            const std::string target =
                named == names.end() ? cIdentifier(call.getCallee()) : named->second;
            auto made = ec::CallOp::create(b, where, mlir::SymbolRefAttr::get(context, target),
                                           mlir::TypeRange{o->getResult(0).getType()}, args);
            swap(made.getResult(0));
            return;
        }
        if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
            ec::AssignOp::create(b, where, lvalueOfGlobal(b, where, store.getName()),
                                 store.getValue());
            eraseIfUnused(o);
            return;
        }
        if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
            if (isEntry) {
                // main: print the globals, return 0. The convention the gate
                // reads: `name=%.17g`, one per line, sorted by name.
                llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(),
                                                         globals.keys().end());
                llvm::sort(names);
                for (llvm::StringRef name : names) {
                    mlir::Value current =
                        ec::LoadOp::create(b, where, f64, lvalueOfGlobal(b, where, name));
                    mlir::Value format = ec::LiteralOp::create(
                        b, where, ec::PointerType::get(ec::OpaqueType::get(context, "const char")),
                        b.getStringAttr(("\"" + name + "=%.17g\\n\"").str()));
                    ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, b.getStringAttr("printf"),
                                             mlir::ValueRange{format, current});
                }
                mlir::Value zero = ec::ConstantOp::create(
                    b, where, mlir::IntegerType::get(context, 32), b.getI32IntegerAttr(0));
                ec::ReturnOp::create(b, where, zero);
            } else {
                (void)returnType;
                ec::ReturnOp::create(b, where, ret.getValue());
            }
            eraseIfUnused(o);
            return;
        }
        // scf ops stay for --convert-scf-to-emitc.
    }

    // THE ONE DECLARATIVE STEP OF THIS PASS, AND WHERE IT HAS TO GO.
    //
    // UnaryPlusIsIdentity.pdll replaces `ctjs.unary plus %x` with `%x`. It runs
    // BEFORE retype() because that is the only window in which a PDL driver can
    // touch this function at all: retype() sets every value's type to its
    // carrier, and a `ctjs.unary` whose operand is f64 does not satisfy its own
    // ODS (`CTJS_ValueType`), so from that line onwards the function does not
    // verify and no driver may be pointed at it. Before it, the rewrite is
    // `!ctjs.value` for `!ctjs.value` and the IR stays valid throughout.
    //
    // applyOpPatternsGreedily AND NOT applyPatternsGreedily, with the worklist
    // seeded by name. Every greedy entry point "performs simple dead-code
    // elimination before attempting to match", no configuration option turns
    // that off, and this pass has its own erasure discipline - eraseIfUnused()
    // is fatal on an operation with uses, and PruneDeadStores already lost a
    // test COUNT to the driver taking over an erasure the pass used to make
    // itself. Handing the driver a list of `ctjs.unary` operations and
    // ExistingOps strictness keeps its worklist to exactly those.
    void applyDeclarativeRules(ctjs::FuncOp fn) {
        llvm::SmallVector<mlir::Operation *> unaries;
        fn.getBody().walk([&](ctjs::UnaryOp u) { unaries.push_back(u.getOperation()); });
        if (unaries.empty()) { return; }
        mlir::GreedyRewriteConfig config;
        config.setStrictness(mlir::GreedyRewriteStrictness::ExistingOps)
            .enableFolding(false)
            .enableConstantCSE(false);
        if (mlir::failed(mlir::applyOpPatternsGreedily(unaries, declarative, config))) {
            llvm::report_fatal_error("ctnative lowering: the declarative pattern driver did not "
                                     "converge over a function admission had accepted");
        }
    }

    void lower(ctjs::FuncOp fn) {
        const bool isEntry = fn.getSymName().starts_with("_script_$");
        mlir::Block & entry = fn.getBody().front();
        applyDeclarativeRules(fn);
        retype(fn);

        // The signature: the parameters after the three implicit arguments,
        // returning double (every native function returns a number; a
        // function that returns nothing returns NaN, which is undefined's
        // carrier).
        llvm::SmallVector<mlir::Type> params;
        for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
            params.push_back(entry.getArgument(i).getType());
        }
        const mlir::Type f64 = mlir::Float64Type::get(context);
        const mlir::Type i32 = mlir::IntegerType::get(context, 32);
        // THE RETURN TYPE IS WHAT THE RETURNS CARRY - retyped already, so any
        // ctjs.return's operand type is the answer; a function that never
        // returns a value returns undefined, carried as a NaN double.
        mlir::Type returnType = isEntry ? i32 : f64;
        if (!isEntry) {
            fn.getBody().walk([&](ctjs::ReturnOp ret) { returnType = ret.getValue().getType(); });
        }
        if (isEntry) { params.clear(); }

        mlir::OpBuilder b(fn);
        const auto named = names.find(fn.getSymName());
        const std::string symbol = isEntry                ? std::string{"main"}
                                   : named != names.end() ? named->second
                                                          : cIdentifier(fn.getSymName());
        auto made =
            ec::FuncOp::create(b, fn.getLoc(), symbol, b.getFunctionType(params, {returnType}));
        // The JavaScript name is the symbol before the importer's `$index`.
        const llvm::StringRef jsName = fn.getSymName().split('$').first;
        made->setAttr("ctnative.provenance",
                      b.getStringAttr((isEntry ? "the top level" : "function " + jsName.str()) +
                                      ", " + siteOfFunction(fn)));
        made.getBody().takeBody(fn.getBody());
        mlir::Block & body = made.getBody().front();

        llvm::SmallVector<mlir::Operation *> ops;
        made.getBody().walk([&](mlir::Operation * o) {
            if (o->getDialect() == fn->getDialect() || o->getName().getStringRef() == "ub.poison") {
                ops.push_back(o);
            }
        });
        for (mlir::Operation * o : ops) { replace(o, isEntry, returnType); }

        // SWEEP THE CONSTANTS THE REWRITE ORPHANED - the NaN for an `undefined`
        // that main no longer returns, a literal folded into nothing. The
        // canonicalizer will not: emitc.constant carries no memory-effect
        // interface, so dead-code elimination keeps it, and the C++ it prints
        // is an unused variable that -Werror rejects. Reverse order, so a
        // constant whose only user was another dead constant goes too.
        llvm::SmallVector<mlir::Operation *> dead;
        made.getBody().walk([&](mlir::Operation * o) {
            // PHASE 59 SLICE 1 ADDS TWO, AND THE REVERSE ORDER IS WHY THEY
            // WORK. A lifted ctjs.create_closure loses its last use when the
            // call arm drops the call_direct's callee value; the constant
            // ctjs.create_cell it captured loses ITS last use when that
            // closure goes. Program order puts the cell first, so the reversed
            // sweep erases the closure, then the cell, then the `undefined`
            // constant the importer made for its `$enclosing_this`.
            if (llvm::isa<ec::ConstantOp, ec::LiteralOp, ctjs::FrameEnterOp, ctjs::LoadGlobalOp,
                          ctjs::ConstantOp, ctjs::CreateClosureOp, ctjs::CreateCellOp>(o)) {
                dead.push_back(o);
            }
        });
        for (mlir::Operation * o : llvm::reverse(dead)) {
            if (o->use_empty()) { eraseIfUnused(o); }
        }

        // AND NOTHING OF THE ctjs DIALECT SURVIVED, WHICH IS THE WHOLE CLAIM.
        // replace() is an if-chain over operation names with no fatal default,
        // and the sweep above erases five kinds by name - so an operation that
        // neither arm handles is simply still there. `ctjs.create_closure` is
        // the live example: it is erased only by the declaration-store arm, so
        // a closure whose store did not take that route rides into the emitted
        // function and is discovered by the C++ emitter three steps later, or
        // by nobody. Asserted here, on the function that was just built, where
        // the message can name it.
        made.getBody().walk([&](mlir::Operation * o) {
            if (o->getDialect() != nullptr && o->getDialect()->getNamespace() == "ctjs") {
                llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") +
                                         o->getName().getStringRef() + "` survived into `" +
                                         made.getSymName() +
                                         "` - every ctjs operation in an accepted function has to "
                                         "be replaced or swept, and this one is handled by no arm "
                                         "of replace()");
            }
        });

        // THE IMPLICIT ARGUMENTS GO LAST, once the declaration closures that
        // named `callee` and `this` have been erased with their stores - not
        // before, as they once did: erasing a block argument that still has
        // uses is the same silent use-after-free as erasing an operation
        // with uses, and it surfaced as a crash three passes later in a fold
        // of an operation that did not exist. Same invariant, same fatal.
        const unsigned drop = isEntry ? body.getNumArguments() : 3u;
        for (unsigned i = 0; i < drop; ++i) {
            if (!body.getArgument(0).use_empty()) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: implicit argument of `") + fn.getSymName() +
                    "` still has uses after lowering - admission should have refused it");
            }
            body.eraseArgument(0);
        }

        // THE OLD ctjs.func STAYS FOR NOW, hollow: an accepted caller lowered
        // after this one still holds a call_direct naming it, and erasing it
        // here left `is_between` with a live symbol use once - the invariant
        // in finish() caught it. finish() runs after every accepted function
        // has been rewritten, when no call_direct names any of them.
        shells.push_back(fn);
    }

    void declareGlobals() {
        mlir::OpBuilder b(context);
        b.setInsertionPointToStart(module.getBody());
        // INCLUDES FIRST: the builder advances past each op it creates, so
        // creation order is file order, and a global initialised to NAN
        // needs <cmath> above it.
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cmath"), b.getUnitAttr());
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cstdio"), b.getUnitAttr());
        // ONLY WHEN A VECTOR SITE EXISTS. An include and a preamble emitted
        // unconditionally would move every byte count the printing gate
        // reports and every line the other native lits pin, for programs that
        // have no array in them.
        if (needsVector) {
            ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("vector"), b.getUnitAttr());
            ec::VerbatimOp::create(b, module.getLoc(), b.getStringAttr(kVectorHelpers));
        }
        llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(), globals.keys().end());
        llvm::sort(names);
        for (llvm::StringRef name : names) {
            // NO INITIALISER, so static zero-initialisation gives 0. Not NaN,
            // which is what this used to emit: every global then started at
            // the same bytes the gate prints for a NaN a program computed, so
            // "never written" and "computed NaN" compared EQUAL and any global
            // whose right answer is NaN was un-failable. Deleting the whole
            // body of the fixture function that exists to prove undefined-field
            // semantics kept the gate green. A global that is never stored is
            // refused outright (see the census in runOnOperation), so 0 is not
            // a value any correct program can observe here.
            auto global =
                ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(),
                                     mlir::Float64Type::get(context), mlir::Attribute{},
                                     /*extern_specifier=*/false, /*static_specifier=*/true,
                                     /*const_specifier=*/false);
            global->setAttr("ctnative.provenance", b.getStringAttr("global " + name.str()));
        }
    }
};

struct CTNativeLowerToEmitCPass : impl::CTNativeLowerToEmitCBase<CTNativeLowerToEmitCPass> {
    using CTNativeLowerToEmitCBase::CTNativeLowerToEmitCBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();

        // PHASE 59 SLICE 1, AND IT RUNS BEFORE THE SOLVE. Lifting turns a
        // closure call into a ctjs.call_direct, which is what makes the target
        // reachable to DeadCodeAnalysis at all - an uncalled private function
        // is dead, and every type in it reads `<unvisited>` - and what lets
        // TypeInference carry each capture's proved type into the leading
        // parameter it became. Doing it after the solve would need a second
        // one.
        const liftReport lifted = closureLifter{module}.run();
        if (report) {
            module.emitRemark() << "ctnative: lifted " << lifted.closures << " closure(s) over "
                                << lifted.captures << " capture(s) into " << lifted.functions
                                << " function(s), rewrote " << lifted.calls << " call(s), unboxed "
                                << lifted.cells << " cell(s)";
        }

        // All three, and none optional - TypeInference.h says why.
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<TypeInference>();
        if (failed(solver.initializeAndRun(module))) {
            module.emitError("the type inference did not converge");
            return signalPassFailure();
        }

        llvm::SmallVector<ctjs::FuncOp> functions;
        module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });

        llvm::SmallVector<ctjs::FuncOp> accepted;
        for (ctjs::FuncOp fn : functions) {
            if (fn->hasAttr("ctjs.not_structured")) {
                fn->setAttr("ctnative.not_native",
                            mlir::StringAttr::get(&getContext(), "unstructured control flow"));
                continue;
            }
            admission check{solver, {}};
            if (check.function(fn)) {
                accepted.push_back(fn);
            } else {
                fn->setAttr("ctnative.not_native", mlir::StringAttr::get(&getContext(), check.why));
            }
        }

        // THE FIXPOINT: a function is native only if every function it calls
        // directly is. Drop any accepted function that calls a refused one,
        // name the callee in its diagnostic, and repeat until nothing moves -
        // a refusal anywhere in a call chain reaches every caller.
        llvm::DenseSet<mlir::Operation *> nativeSet;
        for (ctjs::FuncOp fn : accepted) { nativeSet.insert(fn.getOperation()); }
        mlir::SymbolTable symbols(module);
        // CLOSED IN BOTH DIRECTIONS. A native caller needs a native callee to
        // emit an emitc.call to; and a native CALLEE needs every caller native
        // too, because a refused caller keeps a ctjs.call_direct that must
        // name a ctjs.func with a body - which a lowered function no longer
        // is. So a refusal propagates along the call graph both ways, each
        // step naming the function that caused it, until nothing moves.
        for (bool changed = true; changed;) {
            changed = false;
            for (ctjs::FuncOp fn : functions) {
                const bool native = nativeSet.contains(fn.getOperation());
                fn.getBody().walk([&](ctjs::CallDirectOp call) {
                    mlir::Operation * callee = symbols.lookup(call.getCallee());
                    const bool calleeNative = callee != nullptr && nativeSet.contains(callee);
                    if (native && !calleeNative) {
                        nativeSet.erase(fn.getOperation());
                        fn->setAttr(
                            "ctnative.not_native",
                            mlir::StringAttr::get(
                                &getContext(),
                                ("calls `" + call.getCallee() + "`, which is not native").str()));
                        changed = true;
                    } else if (!native && calleeNative) {
                        nativeSet.erase(callee);
                        callee->setAttr(
                            "ctnative.not_native",
                            mlir::StringAttr::get(&getContext(), ("called by `" + fn.getSymName() +
                                                                  "`, which is not native")
                                                                     .str()));
                        changed = true;
                    }
                });
            }
        }
        llvm::erase_if(accepted,
                       [&](ctjs::FuncOp fn) { return !nativeSet.contains(fn.getOperation()); });

        lowering lower{solver, &getContext(), module};
        for (ctjs::FuncOp fn : accepted) {
            lower.names[fn.getSymName()] =
                fn.getSymName().starts_with("_script_$") ? "main" : cIdentifier(fn.getSymName());
        }

        // THE GLOBAL CENSUS, over the whole accepted set and BEFORE any
        // function is lowered.
        //
        // Two things depend on it. First, `main` prints the globals from this
        // set, and it used to be filled lazily as each global was first
        // touched - so a global written and read only inside a helper was
        // declared and never printed, because main is the importer's function
        // 0 and is lowered first. The differential then failed by naming the
        // missing line rather than the ordering, which is a bug report
        // pointing at the wrong file.
        //
        // Second, a global with no store anywhere in the unit is `undefined`,
        // and this tier carries undefined as NaN - exact for arithmetic and
        // comparison, NOT for printing, where the interpreter reports "not a
        // Number" and the binary would print `nan`. That difference is
        // observable, so it is refused rather than represented, which is the
        // rule this file is built on.
        // A FUNCTION'S OWN NAME IS A GLOBAL TOO, and it is not one of these.
        // `function f(){}` at the top level is a store of a closure into the
        // global "f", and every call is a load of it; both lower to nothing,
        // because the closed world turned the call into a direct one. Counting
        // them would declare `static double g_accumulate;` and print
        // `accumulate=0` beside the numbers - which is exactly what happened
        // the first time this census ran.
        const auto bindsAFunction = [](ctjs::StoreGlobalOp store) {
            return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(store.getValue().getDefiningOp());
        };
        const auto callsOnly = [](ctjs::LoadGlobalOp load) {
            return !load.getResult().use_empty() &&
                   llvm::all_of(load.getResult().getUsers(), [](mlir::Operation * user) {
                       return llvm::isa<ctjs::CallDirectOp>(user);
                   });
        };
        llvm::StringSet<> storedGlobals;
        for (ctjs::FuncOp fn : accepted) {
            fn.getBody().walk([&](ctjs::StoreGlobalOp store) {
                if (!bindsAFunction(store)) { storedGlobals.insert(store.getName()); }
            });
        }
        llvm::SmallVector<llvm::StringRef> neverStored;
        for (ctjs::FuncOp fn : accepted) {
            fn.getBody().walk([&](mlir::Operation * o) {
                llvm::StringRef name;
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o)) {
                    if (callsOnly(load)) { return; }
                    name = load.getName();
                }
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o)) {
                    if (bindsAFunction(store)) { return; }
                    name = store.getName();
                }
                if (name.empty()) { return; }
                if (lower.globals.insert(name).second && !storedGlobals.contains(name)) {
                    neverStored.push_back(name);
                }
            });
        }
        if (!neverStored.empty()) {
            llvm::sort(neverStored);
            const auto entry = llvm::find_if(
                accepted, [](ctjs::FuncOp fn) { return fn.getSymName().starts_with("_script_$"); });
            if (entry != accepted.end()) {
                entry->getOperation()->setAttr(
                    "ctnative.not_native",
                    mlir::StringAttr::get(
                        &getContext(),
                        ("global `" + neverStored.front() +
                         "` is read but never stored, so it is undefined - which this tier "
                         "carries as NaN, and printing that as a number is not what the "
                         "interpreter answers")
                            .str()));
                accepted.erase(entry);
            }
        }

        // AND THE SHAPE CENSUS, for the same reason and in the same place -
        // after the last function has left the accepted set and before the
        // first is lowered. Phase 56C keys a class on the SHAPE and not on the
        // creation site, and whether a shape's definition is a template is a
        // property of every site in the program at once, so no site's type can
        // be spelled until all of them have been seen.
        lower.censusShapes(accepted);

        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace

} // namespace ctcompile::ctnative
