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
#include "mlir/IR/Dominance.h"
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
#include <cstdint>
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
// AND A CAPTURE THAT IS NOT A CELL COMES THROUGH THE ENCLOSING CLOSURE - PHASE
// 59 SLICE 1b. For a descriptor that is not from_parent_local the VM copies the
// enclosing closure's upvalue into the slot (context::make_closure); the
// operand the importer writes beside it is an `undefined` placeholder nothing
// reads, and WHICH upvalue is on the closure's `enclosing_indices` attribute,
// parallel to the capture list (BytecodeImport.cpp, op::closure). While the
// enclosing function is unlifted this tier does not carry that closure at all,
// and the capture is refused - naming the enclosing closure's own reason,
// because that is the obstacle. Once the enclosing function IS lifted, lift()
// has turned its upvalue k into its capture parameter: the entry-block argument
// 3 + k, in [3, 3 + ctnative.captures), holding the initial of a cell some outer
// frame proved constant. The index names a CELL in the enclosing closure and
// the argument holds the VALUE every read of that cell yields (run_loop.cpp,
// VM_CASE(get_upvalue): `reg = cell->slot`), which is what a lifted call wants:
// a nested closure whose capture is that argument captures the same constant,
// and passing the argument on is exact for exactly the reason slice 1 is. The
// classify-then-lift loop is therefore a FIXPOINT: an outer closure lifts in one
// round and the closure nested in it is judged again in the next, when its
// enclosing function carries `ctnative.captures`. This is what a UMD bundle is
// made of - the module body is the factory's frame, so every closure inside a
// nested function reaches a module binding this way - and it was 15 of the 19
// callees a direct call reaches in bootstrap before this slice.
//
// AND A CELL WITH ONE DOMINATING WRITE IS CONSTANT TOO - PHASE 59 SLICE 2 STEP
// 1. `compiler_impl::predeclare_locals` boxes every `var`/`let`/`const` of a
// body up front holding `undefined`, so a declaration is a ctjs.cell_set into
// an already-built box and slice 1 reads every local binding in the language as
// reassigned. A binding written ONCE is constant after that write, and the
// carrier is then the store's operand rather than the cell's initial. The four
// conditions, and why the initial stops being observable, are stated beside
// `closureLifter::writtenOnce`; `constantValueOf` is the one function that
// picks between the two values, because the admission, the call-site rewrite
// and the unboxing have to agree or a lifted call prints `undefined`.

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

// The constant string a property key operand carries, or empty. The same
// reading TypeInference and admission::keyOf make; spelled here because the
// lift runs before either of them is available.
llvm::StringRef constantKeyOf(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto str = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return str ? str.getValue() : llvm::StringRef{};
}

// What the rewrite did, for the `report` remark. Pass statistics are compiled
// out of the LLVM package this builds against, so a counter that is asserted
// has to be printed.
struct liftReport {
    unsigned functions = 0;    // ctjs.funcs whose captures became parameters
    unsigned closures = 0;     // ctjs.create_closures that now lower to nothing
    unsigned captures = 0;     // capture operands turned into arguments
    unsigned calls = 0;        // ctjs.calls rewritten to ctjs.call_direct
    unsigned cells = 0;        // ctjs.create_cells proved constant and unboxed
    unsigned methods = 0;      // method fields whose closure became a free function
    unsigned receivers = 0;    // of those, the ones whose `this` became a parameter
    unsigned objects = 0;      // parameters that became a pointer to a closed shape
    unsigned constructors = 0; // `new X(...)` sites turned into a frame-scope struct
    unsigned carried = 0;      // capture slots that became a pointer to a frame-local cell
    unsigned locals = 0;       // ctjs.create_cells that became a frame-local variable
};

// --- THE RECEIVER IS A PARAMETER ------------------------------------------------
//
// `var o = { x: 1, bump: function (n) { return this.x + n; } }; o.bump(2)` is a
// method table, which is what a bundle's initialisation builds, and until this
// slice the tier refused every function in it: `uses \`this\`` was 6,649 of the
// corpus refusals and the reason was not that the callee is unknown - it is that
// there was no C++ type carrying a receiver. `carrierOf` had a `structure` row
// nothing could produce, and the one object carrier was a closed-shape struct BY
// VALUE, refused the moment it was returned, stored or passed.
//
// So the receiver lifts, exactly as a capture does. `ctjs.call_direct`'s operand
// order IS the callee's entry block - receiver, new.target, callee, parameters -
// so the receiver is ALREADY operand 0 and %arg0 is already the place it lands.
// Nothing had to be added to the call: what had to change is that the lowering
// stops dropping operand 0, that `%arg0` gets a carrier, and that the method
// call becomes a call_direct at all.
//
// THE FOUR ADMISSION CONDITIONS, each a named refusal when it fails:
//
//  1. THE RECEIVER IS A PROVED CLOSED-SHAPE LITERAL. `closedAfterLift` is
//     `hasClosedShape` as it will read once this rewrite has run: a use as the
//     receiver of a method call it is about to make direct is not an opening
//     use, and the `get_property` that loads the method is gone by then.
//  2. THE CALLEE IS PROVABLY ONE FUNCTION. The field holding it is written
//     exactly once on that literal, with a `ctjs.create_closure` of a known
//     target, and the closure value is used for nothing but such stores. Two
//     writes of one key, or a value that is not a closure made here, refuse.
//  3. THE TARGET'S `this` USES ARE ALL CONSTANT-KEY PROPERTY ACCESS on the
//     receiver, or the receiver of another method call this resolves - so
//     `this.x` becomes `self->x` and `this.other()` becomes a second direct
//     call. Returning `this`, storing it, or passing it as an argument refuses:
//     each of those needs an owner, and this slice introduces none.
//  4. THE TARGET IS OTHERWISE NATIVE - the existing call-graph fixpoint's
//     question, not this rewrite's - and its captures, if it is also a closure,
//     lift exactly as Phase 59 slice 1 already lifts them. A method IS a
//     closure in the IR, so this costs nothing: the captures become leading
//     parameters after the three implicit arguments and the receiver stays at
//     %arg0, which is where call_direct already puts it.
//
// A METHOD FIELD IS NOT A STRUCT MEMBER, AND NOT PART OF THE SHAPE KEY. The
// method is a free function; the field holds a compile-time-constant identity
// that condition 2 has just proved single, and it occupies no storage. So the
// store lowers to nothing (`ctnative.method`), the closure lowers to nothing
// (`ctnative.lifted`, the same attribute the closure lift writes), and
// `{x: 1, bump: f}` has the shape `{x}`. That is what lets `{x: 1, f: F}` and
// `{x: 10, f: G}` share ONE class while calling two different functions: the
// class is the data, the method is not in it. Including a method in the key
// would instead demand a member of a type this tier has no carrier for, and
// would split one data layout into as many classes as there are method
// identities - which is the redundant-definition failure Phase 56C exists to
// prevent.

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

    // --- PHASE 59 SLICE 2 STEP 1: A BINDING WITH ONE DOMINATING WRITE -------
    //
    // `compiler_impl::predeclare_locals` hoists every `var`/`let`/`const` of a
    // body and BOXES it up front holding `undefined`, so `var n = 5` is a
    // ctjs.create_cell of a constant undefined and a ctjs.cell_set into the box
    // that already exists. Slice 1's immutability proof reads that as a
    // reassigned binding and refuses it - which is most of a real program: the
    // shape is 250 of the 271 closures bootstrap refuses for a reassigned
    // capture, 37 of phaser's 40 reachable callees and 7 of p5's 26.
    //
    // A BINDING WRITTEN ONCE IS CONSTANT AFTER THAT WRITE, so a capture taken
    // after it is exact - and "after" is dominance, not program order. The cell
    // is admitted when, and only when:
    //
    //   1. it has exactly ONE ctjs.cell_set naming it as the box;
    //   2. that store DOMINATES every ctjs.cell_get of it;
    //   3. that store DOMINATES every CALL of every closure that captures it -
    //      asked at the lift, by whyCapturesDoNotReach, and not here;
    //   4. every other use is one slice 1 already admits - a read at operand 0,
    //      or a capture of a closure whose target writes no upvalue.
    //
    // and its value is then the STORE'S OPERAND rather than the cell's initial.
    // Conditions 2 and 3 are what make the initial unobservable: no read of the
    // binding, in this frame or in a closure over it, can happen on a path that
    // skips the store, so nothing can ever see the hoisted `undefined`. That is
    // why any initial is admitted here and not only a constant undefined - the
    // conservative rule the measurement suggested would have cost the shapes
    // whose hoisted box is initialised from a parameter, and bought nothing,
    // because condition 2 already carries the whole argument.
    //
    // CONDITION 3 IS ABOUT THE CALL AND NOT ABOUT THE `ctjs.create_closure`,
    // and the difference is the whole of this step. A FUNCTION DECLARATION IS
    // HOISTED: `function get() { return n; }` beside `var n = 5` compiles to an
    // `op::closure` in the PROLOGUE, before the store, because that is what
    // JavaScript does - `get` is callable on the first line of the body. So a
    // rule that asked the store to dominate the create_closure refused the
    // exact shape this step exists for; measured, it refused every one of them.
    // What the lift actually does is prepend the captured VALUE at each CALL,
    // and a closure reads the box when it RUNS - `run_loop.cpp`,
    // VM_CASE(get_upvalue) - so the point that has to be dominated is the call
    // site. A closure created before the store and called after it reads the
    // stored value in the interpreter and is handed the stored value here.
    //
    // MLIR'S DOMINANCE IS EXACTLY THE RIGHT INSTRUMENT, INCLUDING FOR LOOPS.
    // `properlyDominates` normalises the LATER operation into the earlier one's
    // region, so a store inside a loop body does NOT dominate a read after the
    // loop (there is a path around the body) and condition 2 refuses it, while
    // a cell created inside the body is a fresh box each iteration whose store,
    // reads and captures are all in that one region and lifts.
    //
    // Filled by census(), before any rewrite: every input is a use of the cell,
    // and the lift changes none of them.
    llvm::DenseMap<mlir::Operation *, ctjs::CellSetOp> writtenOnce;
    // Why a cell that HAS a store is not in the map, for the diagnostic. Absent
    // means the cell has no ctjs.cell_set at all, which is slice 1's shape and
    // whose refusal - if any - is about a store_upvalue somewhere instead.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotWrittenOnce;

    // --- PHASE 59 SLICE 2 STEP 2: A SHARED MUTABLE CELL, CARRIED BY POINTER -
    //
    // The cells step 1 CANNOT take: written twice, written on only one path,
    // or - the shape that dominates the reachable set - written by a closure
    // with `ctjs.store_upvalue`, which `mutatesUpvalue` marks and
    // `isConstantCell` refuses. There is no single value to copy into a
    // parameter, so nothing this tier does by VALUE can be right.
    //
    // SO THE BOX BECOMES AN ORDINARY FRAME-LOCAL VARIABLE AND THE CAPTURE
    // BECOMES A POINTER TO IT. `ctjs.create_cell` lowers to an
    // `emitc.variable` of the carrier, `ctjs.cell_get` to a load of it and
    // `ctjs.cell_set` to an assign; a lifted target takes `double *` for that
    // slot instead of `double`, its `ctjs.load_upvalue` and
    // `ctjs.store_upvalue` become a cell_get and a cell_set THROUGH the
    // pointer, and each call site passes the variable's address. That is the
    // receiver lift's convention exactly (`ctnative.receiver` emits
    // `ctn_x * self`), one operand along.
    //
    // WHY THE POINTER CANNOT DANGLE, which is the only question worth asking:
    //
    //   1. THE CLOSURE CANNOT OUTLIVE THE FRAME. Condition 4 of whyNotLiftable
    //      admits a closure only when EVERY use of its value is a call this
    //      rewrite lowers - a ctjs.call at operand 0, or a ctjs.call_direct at
    //      operand 2 the closed world named. Stored, returned or passed, it is
    //      refused by name. A closure that can be reached from nowhere but a
    //      call in this frame cannot be called after the frame is gone.
    //   2. AND EVERY CALL IS IN THE FRAME THAT OWNS THE VARIABLE.
    //      whyCapturesDoNotReach compares the ctjs.func the captured value
    //      lives in with the ctjs.func the call sits in, and refuses when they
    //      differ - which is the METHODCAP program in closure-refusals.mlir,
    //      and the one case where a bare dominance question answers "yes"
    //      because builtin.module's body is a graph region. It then asks that
    //      the cell properly dominate the call, so the variable is in scope in
    //      the emitted C++ at the point its address is taken.
    //   3. AND THE POINTER GOES NOWHERE ELSE. After the lift, the only uses of
    //      the parameter are the cell_get and cell_set that replaced the
    //      upvalue read and write - whyUpvalueReadsDoNotLift refuses a target
    //      whose load or store names anything but `%arg2` at an index this
    //      rewrite carries - plus its re-appearance at a nested lifted call,
    //      which is the same three conditions one level in.
    //
    // WHAT IS NOT PROVED HERE IS THE CARRIER, and it cannot be: this runs
    // BEFORE the type solve. `admission::function` asks it of the parameter
    // and `admission::op` of the box, both after the solve, and a cell of a
    // type with no C++ carrier is refused there - never pointed at.
    llvm::DenseSet<mlir::Operation *> carriedCells;
    // Why a cell is not carried either, for the diagnostic. Only the
    // structural clause can fail here; the carrier is admission's question.
    llvm::DenseMap<mlir::Operation *, std::string> whyNotCarried;

    // ONE PER LIFTER, and it stays valid across the classify-then-lift
    // fixpoint: lift() inserts entry-block ARGUMENTS and erases and creates
    // operations, and neither changes the block structure a dominator tree is
    // over. MLIR invalidates a block's operation-order cache itself on every
    // insertion and removal, so the intra-block half is maintained too.
    mlir::DominanceInfo dominance{nullptr};

    // --- the receiver lift's census ---------------------------------------
    llvm::SmallVector<ctjs::CreateObjectOp> objects;
    llvm::SmallVector<ctjs::CallOp> allCalls;
    llvm::SmallVector<ctjs::CallDirectOp> allDirectCalls;
    // The single closure a literal's key holds, per literal, per key.
    struct methodField {
        ctjs::SetPropertyOp store;
        ctjs::CreateClosureOp closure;
    };
    llvm::DenseMap<mlir::Value, llvm::StringMap<methodField>> methodsOf;
    // Which closed literals a receiver value may name: a literal names itself,
    // and a lifted method's `%arg0` names everything its call sites pass. The
    // second row is a fixpoint, because `this.other()` inside a method is a
    // call whose receiver is that `%arg0`.
    llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>> behind;
    // One method call this rewrite makes direct.
    struct methodCall {
        ctjs::CallOp call;
        ctjs::GetPropertyOp load;
        mlir::Value receiver;
        ctjs::FuncOp target;
    };
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<methodCall>> callsOfTarget;
    llvm::DenseSet<mlir::Operation *> methodClosures; // create_closures bound to a method field

    // --- the constructor lift ----------------------------------------------
    //
    // `new X(a, b)` WHERE X IS A ctjs.create_closure RESULT. The whole of this
    // rule is a rewrite into two operations that already lower: the instance
    // becomes an empty ctjs.create_object - a frame-scope struct, allocated
    // nowhere - and the constructor becomes the RECEIVER CARRIER's free
    // function over it, reached by the same ctjs.call_direct the method lift
    // emits. So `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes`
    // and `replace` need no constructor case at all: after the rewrite there
    // is no constructor, only a literal and a call that takes its address.
    //
    // WHAT THIS DELIBERATELY DOES NOT BUILD IS THE PROTOTYPE CHAIN. The VM
    // gives every instance one (call.cpp, `make_instance` -> `ensure_prototype`)
    // and Phase 60 owns turning that into C++ inheritance, so any program that
    // touches a constructor's `prototype` is refused by name here rather than
    // compiled to something with no chain at all.
    llvm::SmallVector<ctjs::ConstructOp> allConstructs;
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::ConstructOp>> constructsOfTarget;
    llvm::DenseSet<mlir::Operation *> constructorClosures; // used ONLY as `new` callees

    // --- THE CENSUS, which is a MEASUREMENT and not a rule ------------------
    //
    // Widening `closedAfterLift` is only worth what the corpora actually
    // contain, and this project has twice widened a rule for a shape no bundle
    // has. So the pass can be asked what is blocking every method-bearing
    // literal it refuses: one bucket per blocking use, spelled as the
    // operation and the operand position so that no assumption about what
    // "escapes" means gets to shape the answer. `soleBlocker` is the number
    // that decides anything - an object with two kinds of blocking use is not
    // unblocked by widening one of them.
    bool censusOn = false;
    llvm::StringMap<unsigned> censusUses;
    llvm::StringMap<unsigned> censusSole;
    llvm::StringMap<unsigned> censusSoleFields;
    llvm::StringMap<unsigned> censusRoot;
    llvm::StringMap<unsigned> censusDepth;
    // ONE EXAMPLE PER BUCKET, WITH ITS SOURCE LOCATION. A distribution says
    // how much a rule would be worth and nothing at all about whether the
    // shape behind it is what the label suggests; this is what makes the
    // census checkable against the JavaScript it came from.
    llvm::StringMap<mlir::Location> censusExample;
    unsigned censusOpenObjects = 0;
    unsigned censusMethodFields = 0;

    explicit closureLifter(mlir::ModuleOp m, bool doCensus = false)
        : module(m), context(m.getContext()), censusOn(doCensus) {}

    ctjs::FuncOp targetOf(ctjs::CreateClosureOp c) {
        return byIndex.lookup(static_cast<unsigned>(c.getFunction()));
    }

    // ONE CALL SITE OF A CLOSURE, IN EITHER OF THE TWO SHAPES IT NOW HAS.
    //
    // --ctjs-resolve-globals names a ctjs.call whose callee is a
    // ctjs.create_closure result, so by the time this pass runs the SAME site
    // is either a ctjs.call with the closure at operand 0 and its arguments
    // from operand 2, or a ctjs.call_direct with the closure at operand 2
    // (`$callee_value`) and its arguments from operand 3 - because
    // call_direct's operands ARE the callee's entry block in order.
    //
    // FOUR SEPARATE CENSUSES IN THIS FILE ASKED THAT QUESTION BY CASTING TO
    // ctjs::CallOp, and every one of them silently answered "not a call" for a
    // named site. Measured: with the naming in place and this abstraction
    // absent, native-object-argument-fixture.js stopped being native at all -
    // "an object literal passed to a direct call as an argument". The two
    // shapes are one question, so they are asked in one place.
    struct closureCall {
        mlir::Operation * op = nullptr;
        mlir::ValueRange args;
        mlir::Value receiver;
        explicit operator bool() const { return op != nullptr; }
    };

    closureCall callSiteOf(mlir::OpOperand & use, ctjs::FuncOp target) {
        mlir::Operation * user = use.getOwner();
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            if (use.getOperandNumber() != 0) { return {}; }
            return {user, call.getArgs(), call.getReceiver()};
        }
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
            // THE SYMBOL MUST BE THIS CLOSURE'S OWN TARGET. A call_direct whose
            // callee value is this closure but whose symbol is another function
            // is not a call of it, and the resolver never builds one - asking
            // is what keeps that an invariant rather than an assumption.
            if (use.getOperandNumber() != 2 || !target ||
                direct.getCallee() != target.getSymName()) {
                return {};
            }
            return {user, direct.getArgs(), direct.getReceiver()};
        }
        return {};
    }

    // The closure a call site dispatches on, in either shape, or null.
    static ctjs::CreateClosureOp closureCalledBy(mlir::Operation * user) {
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            return call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
        }
        if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
            return direct.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
        }
        return {};
    }

    // Its arguments, and the operand number argument 0 sits at.
    static mlir::ValueRange argsOfCallSite(mlir::Operation * user) {
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) { return call.getArgs(); }
        return llvm::cast<ctjs::CallDirectOp>(user).getArgs();
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
            } else if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(o)) {
                objects.push_back(object);
            } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(o)) {
                allCalls.push_back(call);
            } else if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(o)) {
                // THE SITES --ctjs-resolve-globals ALREADY NAMED, kept beside
                // the unnamed ones because the object-argument census has to
                // reach both: a literal handed to a named call is in exactly
                // the position a literal handed to an unnamed one is.
                allDirectCalls.push_back(direct);
            } else if (auto built = llvm::dyn_cast<ctjs::ConstructOp>(o)) {
                allConstructs.push_back(built);
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
        // AND THE CELLS WITH ONE DOMINATING WRITE, LAST, because condition 4
        // asks `mutatesUpvalue` and the fixpoint above is what settles it.
        singleWriteCensus();
        // AND THE SHARED ONES AFTER THAT, because the by-value path has first
        // refusal: a cell singleWriteCensus took is copied into a parameter,
        // which costs no pointer and no indirection.
        sharedCellCensus();
    }

    // WHICH CELLS ARE CONSTANT AFTER ONE WRITE - part 24 Phase 59 slice 2 step
    // 1, the four conditions stated beside `writtenOnce`. Run once, before any
    // rewrite, so that the verdict a closure is judged on in round 3 of the
    // lift's fixpoint is the one it was judged on in round 1.
    void singleWriteCensus() {
        module.walk([&](ctjs::CreateCellOp cell) {
            ctjs::CellSetOp store;
            // Every use that must come AFTER the store for its value to be the
            // one this cell yields: conditions 2 and 3, collected in one walk
            // because condition 1 is only known when the walk ends.
            llvm::SmallVector<mlir::Operation *> mustFollow;
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user)) {
                    // THE CELL AS THE BOX, NOT AS THE VALUE PUT IN ONE.
                    // `ctjs.cell_set %other, %cell` stores this cell INTO
                    // another and is not a write of it - and it is a use this
                    // rule does not carry, so it fails outright.
                    if (use.getOperandNumber() != 0) { return; }
                    // CONDITION 1: EXACTLY ONE. Two writes make the binding
                    // shared mutable state again, and which one a capture sees
                    // depends on the path taken to it.
                    if (store) {
                        whyNotWrittenOnce[cell.getOperation()] = "it is assigned more than once";
                        return;
                    }
                    store = write;
                    continue;
                }
                if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) {
                    mustFollow.push_back(user);
                    continue;
                }
                // CONDITION 4: the use list slice 1 already admits, word for
                // word - isConstantCell asks the same of the same uses.
                //
                // AND THE CAPTURE IS NOT ASKED TO FOLLOW THE STORE. The
                // create_closure is hoisted for a function declaration and the
                // closure reads the box when it RUNS, so it is the CALL that
                // has to follow - condition 3, asked by whyCapturesDoNotReach
                // where the call sites are known.
                auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
                if (!made || use.getOperandNumber() < kFirstCapture) { return; }
                ctjs::FuncOp target = targetOf(made);
                if (!target || mutatesUpvalue.contains(target.getOperation())) { return; }
            }
            // NO STORE AT ALL IS SLICE 1'S CELL, whose value is its initial.
            // Nothing to record, and nothing to explain.
            if (!store) { return; }
            for (mlir::Operation * later : mustFollow) {
                if (dominance.properlyDominates(store.getOperation(), later)) { continue; }
                // CONDITION 2. A read the store does not dominate yields the
                // `undefined` the hoist boxed - the honest answer, and the one
                // a lift that ignored this would silently replace with the
                // stored value.
                whyNotWrittenOnce[cell.getOperation()] =
                    "its one assignment does not dominate every read of it, and a read before "
                    "it yields the undefined the binding was hoisted with";
                return;
            }
            writtenOnce[cell.getOperation()] = store;
        });
    }

    // WHICH CELLS BECOME A FRAME-LOCAL VARIABLE - part 24 Phase 59 slice 2
    // step 2, the argument stated beside `carriedCells`. Run once, before any
    // rewrite, for the reason singleWriteCensus is: the verdict a closure is
    // judged on in round 3 of the lift's fixpoint has to be the one it was
    // judged on in round 1, and lift() ADDS a use of the cell (its address, at
    // each call site it rewrites) that a later walk would see and this one
    // must not.
    //
    // ONE STRUCTURAL CLAUSE, AND IT IS THE WHOLE OF IT: every use of the box
    // is a read of it, a write of it, or a capture. A cell stored into an
    // object, put in another cell, returned or passed is a box something else
    // holds a reference to, and a stack variable cannot stand in for one.
    //
    // NOTHING HERE ASKS WHO CAPTURES IT. A capturing closure that does not
    // lift is refused by name, and `carriedCellReason` - run after the
    // fixpoint, where the verdicts are - is what turns that into the cell's
    // own diagnostic. Asking here would be asking before the answer exists.
    void sharedCellCensus() {
        module.walk([&](ctjs::CreateCellOp cell) {
            // THE BY-VALUE PATH HAS FIRST REFUSAL - WHERE IT ACTUALLY WORKS.
            // A cell it takes is copied into a parameter: no pointer, no
            // indirection, and no aliasing question at all. But "it takes it"
            // is TWO questions and only one of them is isConstantCell:
            // whyCapturesDoNotReach then asks, at each call site, whether the
            // value and the assignment reach it, and a cell that fails THAT
            // was refused outright rather than carried. Both halves are asked
            // here so that ONE verdict per cell decides the path, and the
            // OUTERSTORE and LOOPWRITE programs - a store on one path, a store
            // in a loop whose call is after it - become pointers instead of
            // refusals.
            if (isConstantCell(cell) && !byValueMissesACall(cell)) { return; }
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
                // THE CELL AS THE BOX, NOT AS THE VALUE PUT IN ONE - the same
                // distinction singleWriteCensus draws, and for a stronger
                // reason here: `ctjs.cell_set %other, %cell` puts this box
                // inside another one, where a pointer to this frame would
                // outlive the frame.
                if (llvm::isa<ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) { continue; }
                if (llvm::isa<ctjs::CreateClosureOp>(user) &&
                    use.getOperandNumber() >= kFirstCapture) {
                    continue;
                }
                whyNotCarried[cell.getOperation()] =
                    ("it reaches `" + user->getName().getStringRef() +
                     "`, so something other than this frame holds the box and a local variable "
                     "cannot stand in for it")
                        .str();
                return;
            }
            carriedCells.insert(cell.getOperation());
        });
    }

    // WOULD THE BY-VALUE PATH REACH EVERY CALL? The question
    // whyCapturesDoNotReach asks per call site, asked here per CELL, because
    // the choice between copying a binding and pointing at it has to be made
    // once for the whole program: capturedValue, the call-site rewrite and the
    // parameter's type all read it, and two of them disagreeing is a pointer
    // passed where a double is expected.
    //
    // ONLY THE CALLS IN THE CELL'S OWN FUNCTION, and that restriction is the
    // point. A call in another ctjs.func is the METHODCAP refusal - lifting
    // has nothing to prepend there - and carrying cannot help it: the variable
    // is not in that frame either. Comparing the functions first also keeps
    // `properlyDominates` honest, since builtin.module's body is a graph
    // region in which every operation dominates every other.
    bool byValueMissesACall(ctjs::CreateCellOp cell) {
        auto owner = cell->getParentOfType<ctjs::FuncOp>();
        const mlir::Value value = constantValueOf(cell);
        ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(use.getOwner());
            if (!made || use.getOperandNumber() < kFirstCapture) { continue; }
            for (mlir::Operation * at : made.getResult().getUsers()) {
                if (at->getParentOfType<ctjs::FuncOp>() != owner) { continue; }
                if (!dominance.properlyDominates(value, at)) { return true; }
                if (write && !dominance.properlyDominates(write.getOperation(), at)) {
                    return true;
                }
            }
        }
        return false;
    }

    // IS THIS CELL CARRIED BY POINTER? Spelled as a function because the
    // census's set is keyed on the operation and three callers ask.
    bool isCarried(ctjs::CreateCellOp cell) const {
        return carriedCells.contains(cell.getOperation());
    }

    // AND IS CAPTURE SLOT i OF THIS CLOSURE ONE? Two shapes, exactly the two
    // `capturedValue` carries: the operand is a carried cell of this frame, or
    // the slot is filled from the ENCLOSING closure's upvalue k and that
    // function's own capture parameter 3 + k is already a pointer (slice 1b,
    // carried outward). The second row is what makes a shared binding reach a
    // closure two levels in, and it is read off the attribute the enclosing
    // lift wrote rather than re-derived.
    //
    // IT IS A PROPERTY OF THE SLOT AND NOT OF THE TARGET, deliberately. A
    // target that only READS a capture still takes a pointer when the operand
    // is a pointer - the alternative is one ctjs.func with two signatures.
    bool slotIsCarried(ctjs::CreateClosureOp c, unsigned i) {
        if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()) {
            return isCarried(cell);
        }
        const std::int32_t k = enclosingIndex(c, i);
        if (k < 0) { return false; }
        auto enclosing = c->getParentOfType<ctjs::FuncOp>();
        if (!enclosing) { return false; }
        auto listed = enclosing->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
        return listed && llvm::is_contained(listed.asArrayRef(),
                                            static_cast<int32_t>(
                                                captureArgument(static_cast<unsigned>(k))));
    }

    // THE VALUE EVERY READ OF A CONSTANT CELL YIELDS. The cell's initial when
    // nothing writes it - slice 1 - and the STORE'S OPERAND when one write
    // dominates every read and every capture, because from there on the box
    // holds what that write put in it and no read can see anything else.
    //
    // ONE FUNCTION, THREE CALLERS, and that is the point: capturedValue() hands
    // it to a lifted call site, unboxCells() writes it over every read, and
    // isConstantCell() decides whether either may happen. They were three
    // spellings of `cell.getInitial()` and a rule that changed the value had to
    // change all three together or lower a program that prints undefined.
    mlir::Value constantValueOf(ctjs::CreateCellOp cell) {
        if (ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation())) {
            return write.getValue();
        }
        return cell.getInitial();
    }

    // A CELL WHOSE VALUE IS THE SAME AT EVERY READ, which is the whole of the
    // immutability proof. Every use is a read, or a capture into a function
    // that writes no upvalue, or - PHASE 59 SLICE 2 STEP 1 - the ONE
    // ctjs.cell_set that singleWriteCensus proved dominates all of them. Any
    // other write, and any use this does not name, fails it.
    bool isConstantCell(ctjs::CreateCellOp cell) {
        ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
            // THE ONE DOMINATING WRITE. `writtenOnce` holds it only when the
            // census proved conditions 1 to 4 of the rule beside it, so this
            // arm is admitting a store that has ALREADY been shown to come
            // before every read and every capture in the map's own walk - the
            // two walks ask the same question of the same use list.
            if (write && user == write.getOperation()) { continue; }
            auto made = llvm::dyn_cast<ctjs::CreateClosureOp>(user);
            if (!made || use.getOperandNumber() < kFirstCapture) { return false; }
            ctjs::FuncOp target = targetOf(made);
            if (!target || mutatesUpvalue.contains(target.getOperation())) { return false; }
        }
        return true;
    }

    // THE CAPTURE CLAUSES, SHARED WITH THE RECEIVER LIFT. A method IS a closure
    // in the IR - `{ f: function () {} }` compiles to a `closure` opcode and a
    // `set_prop` - so a method that also captures a binding has to satisfy
    // exactly these, and factoring them is what makes the two rules compose
    // rather than diverge.
    std::optional<std::string> whyTargetIsNotLiftable(ctjs::CreateClosureOp c) {
        ctjs::FuncOp target = targetOf(c);
        if (!target) {
            return "its target emitted no ctjs.func - the importer refused it (ctjs.skipped)";
        }
        if (target.getBody().empty() || target.getBody().front().getNumArguments() < 3) {
            return "its target has no body";
        }
        mlir::Block & entry = target.getBody().front();
        const auto captures = static_cast<unsigned>(c.getUpvalues().size());
        if (captures != static_cast<unsigned>(target.getUpvalueCount())) {
            return "its capture list disagrees with the descriptors of the function it names";
        }
        // AN ARROW'S `this` IS LEXICAL, and after the importer's correction the
        // presence of a non-undefined $enclosing_this is the only place the IR
        // says a target is one. A lifted call passes the CALL's receiver as
        // %arg0, which for an arrow is not what the interpreter reads - so an
        // arrow may be lifted only when it never looks.
        //
        // THIS IS THE GUARD THAT KEEPS AN ARROW OUT OF THE RECEIVER LIFT, and
        // it is the one whose removal gives a WRONG ANSWER rather than a
        // refusal: `{ f: () => this.x }` reads the ENCLOSING `this`, every use
        // of it is a constant-key read that condition 3 admits, and lifting it
        // would silently rebind `this` to the literal. It is asked FIRST for
        // that reason, before any rule that admits a `this` use.
        if (!isUndefinedConstant(c.getEnclosingThis()) && !entry.getArgument(0).use_empty()) {
            return "it is an arrow function that reads its lexical `this` - Stage 59B";
        }
        return std::nullopt;
    }

    // WHERE A LIFTED FUNCTION'S UPVALUE k IS, WRITTEN ONCE. lift() ESTABLISHES
    // this layout - it inserts the captures at 3, after the three implicit
    // arguments, and rewrites every ctjs.load_upvalue k to the argument here -
    // and capturedValue() READS it, to hand a nested closure the enclosing
    // function's capture k without an operand to follow (slice 1b's attribute
    // encoding). Two places, one claim, so it is spelled in one.
    //
    // AND THE PIN IS THE POINT, NOT THE ARITHMETIC. While the index lived in an
    // operand, capturedValue range-CHECKED an argument lift() had already
    // written there, so a change to the layout could not make the two disagree:
    // the wrong shape simply failed the check and the closure was refused. A
    // reader that re-derives the position instead SELECTS an argument, and a
    // layout change that this function did not hear about selects the wrong
    // one - a capture silently swapped for a parameter, which compiles clean.
    // A shared helper makes the divergence impossible rather than detectable,
    // which is why it is preferred here to a test that would notice it.
    static constexpr unsigned captureArgument(unsigned k) { return 3 + k; }

    // WHICH UPVALUE OF THE ENCLOSING CLOSURE FILLS CAPTURE SLOT i, or -1 when
    // the operand beside it is the binding and nothing is filled. The list is
    // optional and, when present, exactly as long as the capture list -
    // ctjs.create_closure's own description, and its verifier - so a missing
    // attribute means every slot is from_parent_local, which is most closures.
    static std::int32_t enclosingIndex(ctjs::CreateClosureOp c, unsigned i) {
        const mlir::DenseI32ArrayAttr indices = c.getEnclosingIndicesAttr();
        if (!indices || i >= static_cast<unsigned>(indices.size())) { return -1; }
        return indices[i];
    }

    // THE VALUE A LIFTED CALL PASSES FOR CAPTURE SLOT i, or null when the slot
    // is neither shape a lift carries. Two shapes, and both hold the VALUE of a
    // binding, never the box:
    //
    //   * the operand is a ctjs.create_cell of this frame: its initial. A read
    //     of the capture in the target is a read of that value, because
    //     ctjs.load_upvalue reads THROUGH the cell (run_loop.cpp, get_upvalue:
    //     `reg = cell->slot`), and isConstantCell has to prove nothing ever
    //     wrote it.
    //   * PHASE 59 SLICE 1b: `enclosing_indices[i]` is a k >= 0 - the slot is
    //     filled from the ENCLOSING closure's upvalue k - and the enclosing
    //     ctjs.func has already lifted, so `ctnative.captures` is on it and
    //     k is inside that range. lift() made its upvalue k an entry-block
    //     argument at 3 + k, holding the initial of a cell an outer frame
    //     proved constant. The index names a CELL there and the argument holds
    //     what every read of that cell yields, which is what a capture is read
    //     for; passing it on is passing the same value. The capture OPERAND at
    //     i is the importer's `undefined` placeholder and is never consulted -
    //     ct_aot_make_closure does not consult it either.
    //
    // NOTHING ELSE. A parameter of the enclosing function (index at or past
    // 3 + captures) is not a capture and no index names it - a captured
    // parameter is boxed, so its slot is from_parent_local and its operand is
    // the cell; %arg0-2 are never captures; and a k the enclosing function's
    // capture range does not cover names an upvalue the lift did not carry.
    mlir::Value capturedValue(ctjs::CreateClosureOp c, unsigned i) {
        if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>()) {
            // PHASE 59 SLICE 2 STEP 2: THE BOX ITSELF, when it is carried. The
            // call site takes its ADDRESS - `replace()` does that, from the
            // `ctnative.cell_args` index, through the same `asPointer` the
            // receiver uses - so what has to be in scope and dominating at the
            // call is the variable, and that is what this hands back.
            if (isCarried(cell)) { return cell.getResult(); }
            return constantValueOf(cell);
        }
        const std::int32_t k = enclosingIndex(c, i);
        if (k < 0) { return {}; }
        auto enclosing = c->getParentOfType<ctjs::FuncOp>();
        if (!enclosing || enclosing.getBody().empty()) { return {}; }
        const auto captures = enclosing->getAttrOfType<mlir::IntegerAttr>("ctnative.captures");
        if (!captures || k >= captures.getInt()) { return {}; }
        // 3 + captures ARGUMENTS AT LEAST, which lift() guarantees by inserting
        // them into a block that already had three. Asked anyway, because this
        // reads an argument by number and the claim costs one comparison.
        mlir::Block & entry = enclosing.getBody().front();
        if (entry.getNumArguments() <= captureArgument(static_cast<unsigned>(k))) { return {}; }
        return entry.getArgument(captureArgument(static_cast<unsigned>(k)));
    }

    // The same, after admission: anything else here is a rule that let one
    // through, which this file reports as a named fatal and never as a number.
    mlir::Value liftedCapture(ctjs::CreateClosureOp c, unsigned i) {
        if (const mlir::Value value = capturedValue(c, i)) { return value; }
        llvm::report_fatal_error(
            "ctnative lowering: a capture admitted by whyCapturesDoNotLift is neither a constant "
            "cell of its frame nor a lifted capture parameter of the enclosing function - the "
            "admission and the call-site rewrite have drifted apart");
    }

    // CONDITION 3 OF THE SINGLE-WRITE RULE, AND THE AVAILABILITY OF EVERY OTHER
    // CAPTURE, ASKED AT ONE CALL SITE.
    //
    // lift() prepends `capturedValue(c, i)` at each call it rewrites, and the
    // interpreter reads the box when the closure RUNS - so the point that has
    // to come after the single write, and the point at which the value has to
    // be in scope at all, is the CALL and not the ctjs.create_closure. Both
    // halves are one dominance question and this asks it.
    //
    // IT IS NOT A THEOREM FOR ANY OF THE THREE RULES, and it used to look like
    // one for two of them. A plain closure's sites are uses of `c`'s result, so
    // `c` dominates them - but `c` is HOISTED for a function declaration and
    // the store is not, so the value need not dominate `c` at all and the chain
    // through it proves nothing. A method's sites are `obj.m()` calls reached
    // through the OBJECT, which nothing orders after the ctjs.set_property that
    // bound the field; a receiver that is another method's `%arg0` is not even
    // in the same ctjs.func, and there a bare `properlyDominates` answers "yes"
    // because builtin.module's body is a GRAPH region in which every operation
    // dominates every other. So the function is compared before the dominance
    // is, and every rule asks.
    std::optional<std::string> whyCapturesDoNotReach(ctjs::CreateClosureOp c,
                                                     mlir::Operation * at) {
        auto here = at->getParentOfType<ctjs::FuncOp>();
        for (unsigned i = 0; i < static_cast<unsigned>(c.getUpvalues().size()); ++i) {
            mlir::Value value = capturedValue(c, i);
            // Null is a slot no rule admits; whyCapturesDoNotLift says which.
            if (!value) { continue; }
            if (value.getParentRegion()->getParentOfType<ctjs::FuncOp>() != here) {
                return "capture " + std::to_string(i) +
                       " is a binding of the frame that built the closure, and this call of it "
                       "is in another function - lifting prepends the captured value at the "
                       "CALL, and there is nothing to prepend here";
            }
            if (!dominance.properlyDominates(value, at)) {
                return "capture " + std::to_string(i) +
                       " is a binding whose value does not reach this call of it - the "
                       "assignment does not dominate the call, so the interpreter reads the "
                       "undefined the binding was hoisted with";
            }
            // AND THE WRITE ITSELF, NOT ONLY THE VALUE IT STORES. These are two
            // different questions and only one of them was being asked.
            //
            // The clause above proves the value is IN SCOPE at the call. It
            // does not prove the write has HAPPENED. store-dominates-call
            // implies value-dominates-call, because the operand dominates its
            // own store; the CONVERSE does not hold, and the gap is exactly the
            // shape this rule exists to admit. In
            //
            //     function pick(k) { var v; var t = k * 2;
            //                        if (k > 0) { v = t; }
            //                        function get() { return v; } return get(); }
            //
            // `t` is computed before the branch, so it dominates every call
            // while the `ctjs.cell_set` inside the `scf.if` dominates none.
            // With only the value asked, `pick(-1)` compiled to -2 where the
            // interpreter says `undefined`: `unboxCells` had replaced the read
            // with `t` and the conditional store was erased. Three independent
            // reviews found this within one program each, through the plain,
            // method and constructor rules alike, and the same hole ate a
            // `while` whose body stores a value computed above the loop.
            //
            // A wrong answer is the one thing this tier may not produce, so the
            // question the comment above and the refusal below both describe -
            // does the ASSIGNMENT dominate the call - is now the question the
            // code asks.
            // AND NOT OF A CARRIED ONE - PHASE 59 SLICE 2 STEP 2. This
            // question is the by-VALUE path's: it asks whether the one value a
            // copy would carry has been stored by the time the call runs. A
            // binding carried BY POINTER copies nothing; the callee reads the
            // variable when it runs, and on a path where nothing was stored it
            // reads the NaN the variable was initialised with, which is the
            // `undefined` the interpreter reads. `writtenOnce` still holds
            // such a cell - it has one ctjs.cell_set - so the test is on the
            // path and not on the map.
            if (auto cell = c.getUpvalues()[i].getDefiningOp<ctjs::CreateCellOp>();
                cell && !isCarried(cell)) {
                if (ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation())) {
                    if (!dominance.properlyDominates(write.getOperation(), at)) {
                        return "capture " + std::to_string(i) +
                               " is a binding whose single assignment does not dominate this "
                               "call of it - the call can be reached without the assignment "
                               "having run, and the interpreter reads the undefined the binding "
                               "was hoisted with";
                    }
                }
            }
        }
        return std::nullopt;
    }

    // A CAPTURE THE ENCLOSING CLOSURE FILLS, WHILE THAT CLOSURE IS UNLIFTED:
    // `enclosing_indices` names an upvalue of a function that carries no
    // `ctnative.captures`, because it was not lifted. The closure is refused
    // for it, and run() appends the ENCLOSING closure's own reason to the
    // sentence once the fixpoint has settled it - which is why this map exists:
    // the reason cannot be known here.
    llvm::DenseMap<mlir::Operation *, ctjs::FuncOp> chainedThrough;

    std::optional<std::string> whyCapturesDoNotLift(ctjs::CreateClosureOp c) {
        if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
        const auto captures = static_cast<unsigned>(c.getUpvalues().size());
        auto enclosing = c->getParentOfType<ctjs::FuncOp>();
        // A capture is a constant box in this frame, or a lifted capture
        // parameter of the enclosing function, or it is not liftable.
        for (unsigned i = 0; i < captures; ++i) {
            const mlir::Value operand = c.getUpvalues()[i];
            if (auto cell = operand.getDefiningOp<ctjs::CreateCellOp>()) {
                // PHASE 59 SLICE 2 STEP 2, ASKED SECOND. A cell the
                // immutability proof takes is copied into a parameter; one it
                // does not is a frame-local variable this call passes a
                // POINTER to. Only when neither holds is the capture refused,
                // and then the box itself is the problem.
                if (isConstantCell(cell) || isCarried(cell)) { continue; }
                // THREE SENTENCES WERE HERE AND TWO ARE GONE. "a binding that
                // is reassigned - a shared cell is Phase 59 slice 2" and "its
                // one assignment does not dominate every read" were slice 1's
                // and step 1's refusals for exactly the shapes step 2 carries;
                // both now lift, and native-shared-cell-fixture.js runs them
                // against the interpreter. What is left is the one clause
                // sharedCellCensus can fail, and it has a sentence for every
                // cell isConstantCell refused - so an absent one is a rule
                // that let a cell past both censuses, which is a fatal here
                // and not a number anywhere.
                const auto shared = whyNotCarried.find(cell.getOperation());
                if (shared == whyNotCarried.end()) {
                    llvm::report_fatal_error(
                        "ctnative lowering: a cell is neither constant nor carried and the "
                        "shared-cell census wrote no reason for it - sharedCellCensus and "
                        "isConstantCell have drifted apart");
                }
                return "capture " + std::to_string(i) +
                       " is a shared binding this tier cannot make a frame-local variable: " +
                       shared->second;
            }
            if (capturedValue(c, i)) { continue; }
            if (enclosingIndex(c, i) >= 0 && enclosing &&
                !enclosing->hasAttr("ctnative.captures")) {
                chainedThrough[c.getOperation()] = enclosing;
                return "capture " + std::to_string(i) +
                       " is filled from the enclosing closure, which did not lift";
            }
            return "capture " + std::to_string(i) +
                   " is neither a cell of this frame nor a capture parameter of the enclosing "
                   "function";
        }
        return std::nullopt;
    }

    // THE TARGET'S OWN CLOSURE FEEDS NOTHING BUT NESTED CLOSURES AND UPVALUE
    // READS, which is ResolveGlobals' clause 4 word for word and is here for
    // its reason: the lift marks the target `private`, and `private` is the
    // claim that EVERY caller is visible. A target that leaks its own closure
    // value can be called through that value by something this IR cannot see,
    // and the claim would be false.
    std::optional<std::string> whyOwnClosureEscapes(ctjs::FuncOp target) {
        for (mlir::OpOperand & use : target.getBody().front().getArgument(2).getUses()) {
            mlir::Operation * user = use.getOwner();
            if (use.getOperandNumber() == 0 &&
                llvm::isa<ctjs::CreateClosureOp, ctjs::LoadUpvalueOp, ctjs::StoreUpvalueOp>(user)) {
                continue;
            }
            return ("its target's own closure escapes into `" + user->getName().getStringRef() +
                    "`, so a call of it may come from somewhere this rewrite cannot see")
                .str();
        }
        return std::nullopt;
    }

    // The target's own upvalue reads have to be the shape the rewrite replaces,
    // or a load would be left naming a closure that is gone.
    //
    // AND ITS WRITES, WHICH IS WHERE SLICE 2 STEP 2 RELAXES EXACTLY ONE
    // CLAUSE AND NOT A LINE MORE. This refused ANY `ctjs.store_upvalue`,
    // unconditionally - "its target reassigns a captured binding". A write is
    // now admitted when, and only when, the SLOT it names is one this closure
    // carries by pointer: `slotIsCarried(c, k)`, which is true for a carried
    // cell of the creating frame and for a slot filled from an enclosing
    // capture that is already a pointer. A write to any other slot is still
    // refused, and by name - a by-value capture of a mutated binding would
    // give every call its own copy, which is the wrong answer COUNTER in
    // closure-refusals.mlir was pinned for.
    //
    // THE CLOSURE IS A PARAMETER NOW, AND IT HAS TO BE: "is this slot
    // carried" is a question about the CREATION SITE's operands, not about
    // the target, and the target is what the three callers share.
    //
    // THE INDEX CHECK IS THE SAME ONE THE READ GETS, and it was not there for
    // writes at all. A store naming something other than `%arg2`, or an index
    // past the capture list, is a write this rewrite cannot place - and
    // leaving it would put a ctjs.store_upvalue in a function whose closure
    // operand is about to be erased.
    std::optional<std::string> whyUpvalueReadsDoNotLift(ctjs::CreateClosureOp c,
                                                        ctjs::FuncOp target) {
        mlir::Block & entry = target.getBody().front();
        const auto captures = static_cast<unsigned>(target.getUpvalueCount());
        std::optional<std::string> bad;
        target.getBody().walk([&](mlir::Operation * o) {
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(o)) {
                if (read.getClosure() != entry.getArgument(2) ||
                    static_cast<unsigned>(read.getIndex()) >= captures) {
                    bad = "its target reads an upvalue this rewrite cannot name";
                }
            }
            if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(o)) {
                const auto k = static_cast<unsigned>(write.getIndex());
                if (write.getClosure() != entry.getArgument(2) || k >= captures) {
                    bad = "its target writes an upvalue this rewrite cannot name";
                } else if (!slotIsCarried(c, k)) {
                    bad = "its target reassigns capture " + std::to_string(k) +
                          ", which is not a binding this tier carries by pointer - copying it "
                          "would give every call its own";
                }
            }
        });
        return bad;
    }

    // The four admission conditions of slice 1, as one sentence each. The
    // reason is written onto the closure so that the function containing it is
    // refused by NAME rather than by "`ctjs.create_closure` is not native yet".
    std::optional<std::string> whyNotLiftable(ctjs::CreateClosureOp c) {
        if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
        ctjs::FuncOp target = targetOf(c);
        mlir::Block & entry = target.getBody().front();
        const unsigned parameters = entry.getNumArguments() - 3;
        // CONDITION 4: every use of the closure VALUE is a call this lowers.
        if (c.getResult().use_empty()) { return "nothing calls it"; }
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            // A CALL THE CLOSED WORLD ALREADY NAMED, WHICH IS THE SAME CALL
            // SITE ONE OPERAND ALONG.
            //
            // --ctjs-resolve-globals rewrites a ctjs.call whose callee is a
            // ctjs.create_closure result into a ctjs.call_direct, where the
            // closure is `$callee_value` at operand 2 rather than the callee at
            // operand 0. Without this arm that use falls to the "it is passed
            // as an argument" refusal below and the lift is LOST on exactly the
            // closures the closed world just proved - measured, before this arm
            // existed, as 3 lifted closures on bootstrap, 27 on p5 and 3 on
            // phaser going to zero.
            //
            // THE ARITY NEEDS NO CHECK HERE. CallDirectOp::verifySymbolUses
            // holds the operand count equal to the entry block's, so a
            // call_direct that verifies passes exactly `parameters` arguments -
            // the resolver padded a short call and refused a long one.
            if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                direct && use.getOperandNumber() == 2 &&
                direct.getCallee() == target.getSymName()) {
                auto caller = direct->getParentOfType<ctjs::FuncOp>();
                if (caller && passesNewTarget.contains(caller.getOperation())) {
                    return "a call of it sits in a function that passes new.target";
                }
                continue;
            }
            auto call = llvm::dyn_cast<ctjs::CallOp>(user);
            if (!call || use.getOperandNumber() != 0) {
                if (llvm::isa<ctjs::StoreGlobalOp>(user)) {
                    return "it is stored to a global - Phase 59 slice 2";
                }
                if (llvm::isa<ctjs::ReturnOp>(user)) { return "it is returned - Phase 59 slice 2"; }
                // A METHOD FIELD THE RECEIVER LIFT DID NOT TAKE. Saying "it is
                // stored into an object" for `{f: function(){}}` names the
                // mechanism and not the obstacle, and the obstacle is always
                // one of two things - the shape is not closed, or the key is
                // written twice - which is exactly what a reader needs.
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    set && use.getOperandNumber() == 2) {
                    return whyNotAMethodField(set);
                }
                if (llvm::isa<ctjs::SetPropertyOp, ctjs::CreateObjectOp, ctjs::AppendOp,
                              ctjs::CreateArrayOp>(user)) {
                    return "it is stored into an object or an array - Phase 59 slice 2";
                }
                // A REFUSAL WAS HERE AND IS GONE: "it is used as a constructor
                // - Phase 60 owns `new`". Every closure a `ctjs.construct`
                // names is now in `constructorClosures` and dispatches to
                // whyNotLiftableConstructor, so this arm was unreachable - and
                // it was pinned by no test, which is how it stayed reachable-
                // looking. The mixed case it used to describe is named there
                // instead, where the clause that actually failed can be said.
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
        // AND THE CAPTURED VALUES REACH EVERY ONE OF THOSE SITES. The loop above
        // has just established that every use of the closure value IS a call
        // this rewrite lowers, so its users are exactly the sites lift() will
        // prepend the captures at.
        for (mlir::Operation * user : c.getResult().getUsers()) {
            if (const std::optional<std::string> why = whyCapturesDoNotReach(c, user)) {
                return why;
            }
        }
        if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) {
            return escapes;
        }
        // CONDITION 3 is the existing call-graph fixpoint's, not this one's.
        return whyUpvalueReadsDoNotLift(c, target);
    }

    // --- the receiver lift --------------------------------------------------

    // CONDITION 1, AS IT WILL READ AFTER THE REWRITE. `hasClosedShape` refuses
    // any use that is not a constant-key get or set, and a method call is two
    // of those uses - the object as the call's RECEIVER, and the get_property
    // that loads the method - so asking it before the rewrite would refuse
    // every object with a method on it. This is the same question asked of the
    // IR this rewrite is about to produce, where the load is gone and the
    // receiver is a call_direct operand `hasClosedShape` now admits by name.
    bool closedAfterLift(mlir::Value object) {
        if (!object.getDefiningOp<ctjs::CreateObjectOp>() && !makesAnInstance(object)) {
            return false;
        }
        return usesCloseTheShape(object);
    }

    // A `ctjs.construct` RESULT IS A LITERAL THAT HAS NOT HAPPENED YET.
    //
    // The constructor lift replaces the construct with an empty
    // ctjs.create_object and a receiver call, so by the time anything reads a
    // shape the instance IS an object literal. Every census here runs BEFORE
    // that rewrite, though, so each would see a `ctjs.construct` and answer
    // "not a literal" - which refuses the module for an instance that is
    // merely passed to a lifted function. This is the same question asked of
    // the IR the rewrite is about to produce, exactly as `closedAfterLift`
    // itself is for a method call.
    //
    // ONLY FOR A CALLEE THIS PASS HAS PROVED, which is what
    // `constructorClosures` holds and why that census runs first: a construct
    // whose callee is opaque is not going to become a literal, and admitting
    // one here would be a shape claim about an object the VM allocates.
    bool makesAnInstance(mlir::Value object) {
        auto built = object.getDefiningOp<ctjs::ConstructOp>();
        if (!built) { return false; }
        auto closure = built.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
        if (!closure || !constructorClosures.contains(closure.getOperation())) { return false; }
        // AND NOTHING BUT `new` USES IT. `constructorClosures` is every closure
        // a `new` names, so that the prototype clause can be REACHED and name
        // itself; this predicate is a different claim - that the rewrite will
        // actually happen - and a closure used anywhere else cannot support it.
        return llvm::all_of(closure.getResult().getUsers(), [](mlir::Operation * user) {
            return llvm::isa<ctjs::ConstructOp>(user);
        });
    }

    // THE USE-LIST HALF OF CONDITION 1, ASKED WITHOUT THE QUESTION OF WHAT MADE
    // THE VALUE. `closedAfterLift` asks it of a literal. The constructor lift
    // asks the identical question of a `ctjs.construct` result, because the
    // rewrite turns that result INTO a literal and its use list does not move -
    // so a second, drifting copy of this walk is exactly what is not wanted.
    bool usesCloseTheShape(mlir::Value object) {
        for (mlir::OpOperand & use : object.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() != 0 || constantKeyOf(get.getKey()).empty()) {
                    return false;
                }
                continue;
            }
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() != 0 || constantKeyOf(set.getKey()).empty()) {
                    return false;
                }
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                // The object as the RECEIVER of a call whose callee is a
                // constant-key read of that same object: a method call.
                if (use.getOperandNumber() == 1) {
                    auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (!load || load.getObject() != object ||
                        constantKeyOf(load.getKey()).empty()) {
                        return false;
                    }
                    continue;
                }
                // AND THE OBJECT AS AN ARGUMENT, which is the same carrier one
                // operand along: a parameter this rewrite will hand a
                // `ctn_x *`. `argumentCensus` decided that before anything was
                // rewritten, so this is a map lookup and not a second proof.
                if (use.getOperandNumber() >= 2) {
                    auto made = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
                    if (made && slotCarriesAnObject(made, use.getOperandNumber() - 2)) { continue; }
                }
                return false;
            }
            // THE SAME TWO QUESTIONS AT THE POSITIONS call_direct PUTS THEM.
            // Its operands are the callee's entry block in order, so argument i
            // is operand 3 + i rather than 2 + i. Without this arm a literal
            // handed to a call the closed world had already named read as an
            // OPEN shape, and the object-argument lift lost it.
            if (auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(user)) {
                if (use.getOperandNumber() >= 3) {
                    auto made = direct.getCalleeValue().getDefiningOp<ctjs::CreateClosureOp>();
                    if (made && slotCarriesAnObject(made, use.getOperandNumber() - 3)) { continue; }
                }
                return false;
            }
            return false;
        }
        return true;
    }

    // CONDITION 2: the one function a method call reaches, or null. Every
    // literal the receiver may name must bind the key exactly once, and all of
    // them must name the SAME ctjs.func - one target is one C++ signature.
    ctjs::FuncOp resolveMethod(ctjs::CallOp call, mlir::Value & receiverOut,
                               ctjs::GetPropertyOp & loadOut) {
        auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!load || !load.getResult().hasOneUse()) { return {}; }
        const mlir::Value receiver = load.getObject();
        if (receiver != call.getReceiver()) { return {}; }
        const llvm::StringRef key = constantKeyOf(load.getKey());
        if (key.empty()) { return {}; }
        const auto objects = behind.find(receiver);
        if (objects == behind.end() || objects->second.empty()) { return {}; }
        ctjs::FuncOp target;
        for (mlir::Value object : objects->second) {
            const auto fields = methodsOf.find(object);
            if (fields == methodsOf.end()) { return {}; }
            const auto field = fields->second.find(key);
            if (field == fields->second.end()) { return {}; }
            ctjs::FuncOp named = targetOf(field->second.closure);
            if (!named || (target && named != target)) { return {}; }
            target = named;
        }
        receiverOut = receiver;
        loadOut = load;
        return target;
    }

    // WHY A CLOSURE STORED INTO AN OBJECT IS NOT A METHOD FIELD. Three routes,
    // and only the third is "this is not a method table at all".
    std::string whyNotAMethodField(ctjs::SetPropertyOp set) {
        const mlir::Value object = set.getObject();
        const llvm::StringRef key = constantKeyOf(set.getKey());
        if (!object.getDefiningOp<ctjs::CreateObjectOp>()) {
            return "it is stored into something that is not an object literal made here - "
                   "Phase 59 slice 2";
        }
        if (key.empty()) {
            return "it is stored into an object under a key that is not a constant, so which "
                   "field holds it is not known here";
        }
        if (!closedAfterLift(object)) {
            return ("it is a method field of an object whose shape is not closed, so `" + key +
                    "` cannot become a free function taking that object")
                .str();
        }
        const auto fields = methodsOf.find(object);
        if (fields == methodsOf.end() || fields->second.find(key) == fields->second.end()) {
            return ("the field `" + key +
                    "` is written more than once, so which function a call through it reaches "
                    "depends on which store ran")
                .str();
        }
        return "it is stored into an object or an array - Phase 59 slice 2";
    }

    // ONE BLOCKING USE, NAMED BY WHAT IT IS AND WHERE IT SITS. No judgement:
    // the label is the operation and the operand number, refined only where
    // the operand number alone would merge two genuinely different things (a
    // dynamic key and a value store are both `set_property`).
    std::string blockingLabel(mlir::Value object, mlir::OpOperand & use) {
        mlir::Operation * user = use.getOwner();
        const unsigned n = use.getOperandNumber();
        if (llvm::isa<ctjs::GetPropertyOp>(user)) {
            return n == 0 ? "get.dynamic-key" : "get.as-key";
        }
        if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
            if (n == 1) { return "set.as-key"; }
            if (n == 2) {
                // WHERE IT IS STORED DECIDES WHETHER IT NEEDS AN OWNER. Into
                // another literal made here it could be a member of that
                // literal's class, which owns it outright; anywhere else it
                // outlives the frame that made it.
                mlir::Value into = set.getObject();
                if (!into.getDefiningOp<ctjs::CreateObjectOp>()) {
                    return "set.stored-into.not-a-literal";
                }
                if (constantKeyOf(set.getKey()).empty()) {
                    return "set.stored-into.a-literal-under-a-dynamic-key";
                }
                return closedAfterLift(into) ? "set.stored-into.a-closed-literal"
                                             : "set.stored-into.an-open-literal";
            }
            return "set.dynamic-key";
        }
        if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
            if (n == 0) { return "call.as-callee"; }
            if (n >= 2) { return "call.argument." + argumentDetail(user, n - 2); }
            auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!load) { return "call.receiver-callee-not-a-load"; }
            if (load.getObject() != object) { return "call.receiver-callee-off-another-object"; }
            return "call.receiver-dynamic-key";
        }
        if (llvm::isa<ctjs::CallDirectOp>(user)) {
            // A SITE THE CLOSED WORLD NAMED IS STILL THE SAME SITE. This census
            // is what the plan's next lever gets chosen from, so a ctjs.call
            // that --ctjs-resolve-globals turned into a ctjs.call_direct must
            // not silently change bucket from `call.argument.<detail>` to an
            // undifferentiated `call_direct.argument` - that would have erased
            // the very `callee-known` / `callee-opaque` distinction the census
            // exists to draw.
            if (n == 0) { return "call_direct.receiver"; }
            if (n >= 3 && closureCalledBy(user)) {
                return "call.argument." + argumentDetail(user, n - 3);
            }
            return "call_direct.argument";
        }
        if (auto made = llvm::dyn_cast<ctjs::ConstructOp>(user)) {
            if (n == 0) { return "construct.callee"; }
            return made.getCallee().getDefiningOp<ctjs::CreateClosureOp>()
                       ? "construct.argument.callee-known"
                       : "construct.argument.callee-opaque";
        }
        if (auto append = llvm::dyn_cast<ctjs::AppendOp>(user); append && n == 1) {
            return TypeInference::isDenseVectorSite(append.getArray())
                       ? "append.into-a-dense-array"
                       : "append.into-an-open-array";
        }
        return (user->getName().getStringRef() + "#" + llvm::Twine(n)).str();
    }

    // IS THE CALLEE ONE FUNCTION, AND DOES IT ONLY READ THE ARGUMENT? Exactly
    // the question the receiver lift asks of `this`, asked of an argument
    // position - because if the answer is yes the carrier is the same one.
    std::string argumentDetail(mlir::Operation * call, unsigned j) {
        ctjs::CreateClosureOp made = closureCalledBy(call);
        if (!made) { return "callee-opaque"; }
        ctjs::FuncOp target = targetOf(made);
        if (!target || target.getBody().empty()) { return "callee-not-imported"; }
        mlir::Block & entry = target.getBody().front();
        // `j` IS THE JAVASCRIPT ARGUMENT INDEX, not an operand number, because
        // the two call shapes number their operands differently and the entry
        // block does not: argument j always lands on entry argument j + 3.
        const unsigned slot = j + 3;
        if (slot >= entry.getNumArguments()) { return "argument-has-no-parameter"; }
        return onlyConstantKeyAccess(entry.getArgument(slot)) ? "parameter-is-read-only"
                                                              : "parameter-escapes";
    }

    // THE RECEIVER LIFT'S CONDITION 3, ASKED OF A VALUE THAT IS NOT `this`.
    // Every use has to be one a `ctn_x *` can carry, and here that is a
    // constant-key read or write and nothing else.
    //
    // DELIBERATELY NARROWER THAN `whyThisLeaks`, WHICH ALSO ADMITS `this.m()`.
    // That arm asks `resolveMethod`, whose answer depends on `behind` - a
    // fixpoint that is still moving while `methodCensus` runs - so a predicate
    // built on it gives one answer early in the pass and another late. This
    // one is a use-list walk with no state at all, which is what lets the same
    // question be asked before the census, during it, and from the lift, and
    // get the same answer every time. A parameter that calls a method on its
    // object is refused, by name, and that is Phase 59 slice 2's.
    static bool onlyConstantKeyAccess(mlir::Value v) {
        for (mlir::OpOperand & use : v.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0 && !constantKeyOf(get.getKey()).empty()) {
                    continue;
                }
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0 && !constantKeyOf(set.getKey()).empty()) {
                    continue;
                }
            }
            return false;
        }
        return true;
    }

    // --- PHASE 59 SLICE 1, ARGUMENT FORM: A PARAMETER CARRIES AN OBJECT -----
    //
    // `f(o)` IS `o.m()` WITH THE POINTER IN A DIFFERENT OPERAND. The receiver
    // lift's whole content is that a closed-shape literal reaches a function as
    // a `ctn_x *` and its fields as `self->x`; nothing in that carrier is about
    // operand 0. So an ARGUMENT gets it too, on the same proof and with the
    // same refusals - and the two share `receiverType`, `receiverArgs`,
    // `memberAccess` and the alias groups rather than growing a second copy.
    //
    // THE CONDITIONS, each a named refusal when it fails:
    //
    //  1. THE CALLEE IS ONE FUNCTION THIS REWRITE IS ABOUT TO MAKE DIRECT -
    //     a `ctjs.create_closure` made here whose every use is a call of it.
    //     Anything else has no call site to put an address at.
    //  2. EVERY CALL PASSES AN OBJECT LITERAL IN THAT POSITION, and every one
    //     of those literals is itself closed. One parameter is one C++ type;
    //     a position that is a literal at one site and a number at another has
    //     no single spelling, and a short call that omits it would pass the
    //     padding `undefined`.
    //  3. THE PARAMETER IS READ, AND ONLY THROUGH CONSTANT KEYS. Unread, the
    //     emitted parameter is `-Wunused-parameter` under -Werror; reached any
    //     other way it needs an owner, which this slice introduces none of.
    //
    // WHAT CONDITION 2 COSTS, AND WHY IT IS A FIXPOINT. Whether a literal is
    // closed depends on whether being passed here opens it, which depends on
    // whether this slot carries an object, which depends on whether the
    // literals passed to it are closed. `argumentCensus` starts from every
    // candidate slot admitted and DROPS the ones whose literals do not hold up,
    // to a fixpoint - the greatest one, which is the right reading of "no use
    // opens it": two literals passed to one read-only parameter support each
    // other, and neither opens anything.
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<unsigned, 2>> objectSlotsOf;

    // Conditions 1 and 3, which do not move. Condition 2's `closedAfterLift`
    // half is the fixpoint's, and is asked in `argumentCensus` alone.
    bool slotIsACandidate(ctjs::CreateClosureOp c, unsigned j) {
        ctjs::FuncOp target = targetOf(c);
        if (!target || target.getBody().empty()) { return false; }
        mlir::Block & entry = target.getBody().front();
        if (3 + j >= entry.getNumArguments()) { return false; }
        bool called = false;
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            const closureCall site = callSiteOf(use, target);
            if (!site) { return false; }
            called = true;
            // A SHORT CALL IS NOT A CANDIDATE, and this half is load-bearing
            // twice over: the lift pads a missing argument with `undefined`,
            // which is not an object, and the fixpoint below would index past
            // the end asking whether it is.
            //
            // AND A CLAUSE FOR "THE ARGUMENT IS AN OBJECT LITERAL" WAS HERE AND
            // IS GONE, MEASURED. The fixpoint subsumes it exactly:
            // `closedAfterLift` returns false for anything whose defining op is
            // not a ctjs.create_object, so a slot passed a number is dropped on
            // the first round anyway. Removed, `take(o) + take(2)` refuses with
            // the same sentence, from the same place, and the whole suite stays
            // green - which is the definition of decoration.
            if (j >= site.args.size()) { return false; }
        }
        const mlir::Value parameter = entry.getArgument(3 + j);
        return called && !parameter.use_empty() && onlyConstantKeyAccess(parameter);
    }

    // Is JS parameter `j` of this closure's target one this rewrite will hand a
    // pointer? Read by `closedAfterLift`, so it must answer from the map and
    // never recompute - the map IS the fixpoint's result.
    bool slotCarriesAnObject(ctjs::CreateClosureOp c, unsigned j) const {
        const auto at = objectSlotsOf.find(c.getOperation());
        return at != objectSlotsOf.end() && llvm::is_contained(at->second, j);
    }

    void argumentCensus() {
        for (ctjs::CreateClosureOp c : closures) {
            // A METHOD FIELD IS NOT THIS RULE'S, AND NEEDS NO CLAUSE HERE:
            // its closure value is STORED rather than called, which condition
            // 1's "every use is a call at operand 0" already refuses. That is
            // load-bearing for the ORDER - this census runs before
            // `methodCensus`, so `methodClosures` is still empty - and a
            // clause reading it here would have been silently vacuous.
            if (admissionIsDeclaration(c)) { continue; }
            ctjs::FuncOp target = targetOf(c);
            if (!target || target.getBody().empty()) { continue; }
            const unsigned parameters = target.getBody().front().getNumArguments() - 3;
            llvm::SmallVector<unsigned, 2> slots;
            for (unsigned j = 0; j < parameters; ++j) {
                if (slotIsACandidate(c, j)) { slots.push_back(j); }
            }
            if (!slots.empty()) { objectSlotsOf[c.getOperation()] = std::move(slots); }
        }
        // THE FIXPOINT, WHICH ONLY SHRINKS. A slot whose literal turns out to
        // be open is not a slot, and dropping it can open another literal that
        // was relying on it - so this repeats until nothing moves. It
        // terminates because `objectSlotsOf` never grows here.
        for (bool changed = true; changed;) {
            changed = false;
            for (ctjs::CreateClosureOp c : closures) {
                const auto at = objectSlotsOf.find(c.getOperation());
                if (at == objectSlotsOf.end()) { continue; }
                llvm::SmallVector<unsigned, 2> kept;
                for (unsigned j : at->second) {
                    bool ok = true;
                    for (mlir::OpOperand & use : c.getResult().getUses()) {
                        const closureCall site = callSiteOf(use, targetOf(c));
                        if (site && j < site.args.size() && !closedAfterLift(site.args[j])) {
                            ok = false;
                        }
                    }
                    if (ok) { kept.push_back(j); }
                }
                if (kept.size() == at->second.size()) { continue; }
                changed = true;
                if (kept.empty()) {
                    objectSlotsOf.erase(c.getOperation());
                } else {
                    objectSlotsOf[c.getOperation()] = std::move(kept);
                }
            }
        }
        // AND THE REASON, ONTO THE LITERAL, for every object argument this
        // rule did NOT take. It is written here rather than worked out by
        // `admission::whyOpen` because every condition that can fail is a
        // property of the CALLEE - which parameter, read how, called from
        // where - and the use-list walk that meets the escape has none of it.
        // Same idiom as `ctnative.closure_reason` and `ctnative.cell_reason`.
        llvm::SmallVector<mlir::Operation *> sites;
        for (ctjs::CallOp call : allCalls) { sites.push_back(call.getOperation()); }
        for (ctjs::CallDirectOp direct : allDirectCalls) { sites.push_back(direct.getOperation()); }
        for (mlir::Operation * site : sites) {
            ctjs::CreateClosureOp made = closureCalledBy(site);
            for (auto [j, argument] : llvm::enumerate(argsOfCallSite(site))) {
                mlir::Operation * literal = argument.getDefiningOp();
                if (!llvm::isa_and_nonnull<ctjs::CreateObjectOp>(literal)) { continue; }
                if (made && slotCarriesAnObject(made, static_cast<unsigned>(j))) { continue; }
                literal->setAttr(
                    "ctnative.object_reason",
                    mlir::StringAttr::get(context,
                                          whyNotAnObjectArgument(site, static_cast<unsigned>(j))));
            }
        }
    }

    // WHY AN OBJECT PASSED TO A CALL IS NOT A PARAMETER, in one sentence per
    // condition. Asked only where the object really is an argument, so "it is
    // passed to a call" is never the answer on its own.
    std::string whyNotAnObjectArgument(mlir::Operation * call, unsigned j) {
        ctjs::CreateClosureOp made = closureCalledBy(call);
        if (!made) {
            return "it is passed to a call whose callee is not one function this rewrite can "
                   "name, so there is no parameter to give the object's address to";
        }
        // A METHOD-FIELD CLAUSE WAS HERE AND IS GONE, FOR TWO REASONS. It was
        // VACUOUS - this runs at the end of `argumentCensus`, which is before
        // `methodCensus`, so `methodClosures` is still empty - and it was
        // REDUNDANT: a closure stored into a literal has a use that is not a
        // call, which the loop at the bottom names better ("used as a value
        // elsewhere") than "it is a method field" would. Both were measured on
        // the same program.
        ctjs::FuncOp target = targetOf(made);
        if (!target || target.getBody().empty() ||
            3 + j >= target.getBody().front().getNumArguments()) {
            return "it is passed in an argument position the callee has no parameter for - the "
                   "surplus has frame semantics";
        }
        const mlir::Value parameter = target.getBody().front().getArgument(3 + j);
        if (parameter.use_empty()) {
            return "it is passed to a parameter nothing reads, and an object parameter that is "
                   "never read is `-Wunused-parameter` in the generated C++";
        }
        if (!onlyConstantKeyAccess(parameter)) {
            return "it is passed to a parameter that reaches it through something other than a "
                   "constant key - that needs an owner, and this slice introduces none";
        }
        for (mlir::OpOperand & use : made.getResult().getUses()) {
            const closureCall other = callSiteOf(use, target);
            // A SENTENCE FOR "THE CLOSURE IS USED AS A VALUE ELSEWHERE" WAS
            // HERE AND IS GONE, BECAUSE NO PROGRAM REACHES IT. Every use of a
            // closure that is not a call of it is already refused by
            // `whyNotLiftable`, and that refusal lands on this same function
            // and is reported first: measured on `kept = take; take(o);`,
            // which says "a closure used as a value: it is stored to a global"
            // and never asks this question. The cast still needs an else.
            if (!other) { continue; }
            const mlir::ValueRange args = other.args;
            if (j >= args.size() || !args[j].getDefiningOp<ctjs::CreateObjectOp>()) {
                return "it is passed to a parameter that is an object literal at this call and "
                       "something else at another, so the parameter has no single C++ type";
            }
        }
        return "it is passed to a parameter whose other object literal is not itself a closed "
               "shape, so the two would not agree on one class";
    }

    // Every use of `object` that `closedAfterLift` would refuse, labelled.
    void blockingLabelsOf(mlir::Value object, llvm::StringSet<> & into,
                          llvm::StringMap<unsigned> * tally) {
        for (mlir::OpOperand & use : object.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0 && !constantKeyOf(get.getKey()).empty()) {
                    continue;
                }
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0 && !constantKeyOf(set.getKey()).empty()) {
                    continue;
                }
            } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                if (use.getOperandNumber() == 1) {
                    auto load = call.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
                    if (load && load.getObject() == object &&
                        !constantKeyOf(load.getKey()).empty()) {
                        continue;
                    }
                }
                // AND THE ARM `closedAfterLift` GAINED, so that the two agree
                // about what "blocking" means. Without it a literal that is
                // open for some other reason but IS passed to a carried
                // parameter would count that use as a blocker, and the
                // sole-blocker column - the only one that decides anything -
                // would be wrong for exactly the shape this slice admits.
                if (use.getOperandNumber() >= 2) {
                    auto made = call.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
                    if (made && slotCarriesAnObject(made, use.getOperandNumber() - 2)) { continue; }
                }
            }
            const std::string label = blockingLabel(object, use);
            if (tally) { ++(*tally)[label]; }
            into.insert(label);
        }
    }

    // WHERE THE NESTING ACTUALLY ROOTS, which is the only thing that says
    // whether widening the nested-literal case would cascade. `{a: {b: {c:
    // {x: 1, m: f}}}}` reports `set.stored-into.an-open-literal` three times
    // over and tells a reader nothing; what decides the work is what the
    // OUTERMOST literal in that chain is blocked by, because until that one
    // closes none of the inner ones can be a member of anything.
    std::string rootBlockingLabel(mlir::Value object, unsigned & depthOut) {
        llvm::SmallPtrSet<mlir::Operation *, 8> seen;
        mlir::Value at = object;
        for (unsigned depth = 0;; ++depth) {
            llvm::StringSet<> here;
            blockingLabelsOf(at, here, nullptr);
            depthOut = depth;
            if (here.size() != 1) { return "mixed"; }
            const llvm::StringRef only = here.begin()->first();
            if (only != "set.stored-into.an-open-literal") { return only.str(); }
            mlir::Value into;
            for (mlir::OpOperand & use : at.getUses()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                if (set && use.getOperandNumber() == 2) { into = set.getObject(); }
            }
            if (!into || !seen.insert(into.getDefiningOp()).second) { return "a-cycle"; }
            at = into;
        }
    }

    void censusOpenLiteral(mlir::Value object) {
        // ONLY THE LITERALS THIS WORK IS ABOUT: one that holds no method field
        // is refused for some other reason entirely and would drown the count.
        // Weighted by HOW MANY method fields it holds as well as counted once,
        // because a refusal is per method field and a literal is not.
        unsigned fields = 0;
        for (mlir::Operation * user : object.getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (set && set.getObject() == object && !constantKeyOf(set.getKey()).empty() &&
                set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                ++fields;
            }
        }
        if (fields == 0) { return; }
        ++censusOpenObjects;
        censusMethodFields += fields;
        llvm::StringSet<> here;
        blockingLabelsOf(object, here, &censusUses);
        if (here.size() == 1) {
            censusSole[here.begin()->first()] += 1;
            censusSoleFields[here.begin()->first()] += fields;
        }
        unsigned depth = 0;
        const std::string root = rootBlockingLabel(object, depth);
        ++censusRoot[root];
        ++censusDepth[std::to_string(depth)];
        censusExample.try_emplace(root, object.getLoc());
    }

    void methodCensus() {
        for (ctjs::CreateObjectOp object : objects) {
            if (!closedAfterLift(object.getResult())) { continue; }
            llvm::StringMap<unsigned> writes;
            llvm::StringMap<methodField> fields;
            for (mlir::Operation * user : object.getResult().getUsers()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!set || set.getObject() != object.getResult()) { continue; }
                ++writes[constantKeyOf(set.getKey())];
                if (auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>()) {
                    fields[constantKeyOf(set.getKey())] = methodField{set, made};
                }
            }
            llvm::StringMap<methodField> & into = methodsOf[object.getResult()];
            for (const auto & entry : fields) {
                // A KEY WRITTEN TWICE IS NOT A METHOD, whatever the second
                // write holds: `o.f = g` after `var o = {f: h}` makes the
                // callee depend on which store ran, which is exactly what
                // condition 2 forbids. The key is simply not admitted, and the
                // call through it stays a ctjs.call - refused by name below.
                if (writes[entry.first()] == 1) { into[entry.first()] = entry.second; }
            }
            behind[object.getResult()].push_back(object.getResult());
        }
        // THE FIXPOINT OVER THE RECEIVER CHAIN. `this.other()` inside a method
        // has `%arg0` for a receiver, and `%arg0` names whatever the call sites
        // pass - which is only known once those call sites resolve. One round
        // per link in the chain, and it terminates because `behind` only grows
        // and is bounded by the literals in the module.
        for (bool changed = true; changed;) {
            changed = false;
            for (ctjs::CallOp call : allCalls) {
                mlir::Value receiver;
                ctjs::GetPropertyOp load;
                ctjs::FuncOp target = resolveMethod(call, receiver, load);
                if (!target || target.getBody().empty() ||
                    target.getBody().front().getNumArguments() < 3) {
                    continue;
                }
                llvm::SmallVector<mlir::Value, 2> & named =
                    behind[target.getBody().front().getArgument(0)];
                for (mlir::Value object : behind.lookup(receiver)) {
                    if (!llvm::is_contained(named, object)) {
                        named.push_back(object);
                        changed = true;
                    }
                }
            }
        }
        // AND THE CALLS THEMSELVES, once `behind` has stopped moving.
        for (ctjs::CallOp call : allCalls) {
            mlir::Value receiver;
            ctjs::GetPropertyOp load;
            ctjs::FuncOp target = resolveMethod(call, receiver, load);
            if (!target || target.getBody().empty() ||
                target.getBody().front().getNumArguments() < 3) {
                continue;
            }
            callsOfTarget[target.getOperation()].push_back(
                methodCall{call, load, receiver, target});
        }
        for (const auto & entry : methodsOf) {
            for (const auto & field : entry.second) {
                ctjs::CreateClosureOp bound = field.second.closure;
                methodClosures.insert(bound.getOperation());
            }
        }
        // THE CENSUS LAST, because two of its labels ask questions - "is this
        // call one the rewrite resolves", "does that parameter escape" - whose
        // answers are only settled once `behind` and `methodsOf` have stopped
        // moving. Asking during the first loop undercounted by exactly the
        // chained receivers.
        if (censusOn) {
            for (ctjs::CreateObjectOp object : objects) {
                if (!closedAfterLift(object.getResult())) { censusOpenLiteral(object.getResult()); }
            }
        }
    }

    // CONDITION 3: what the target does with `this`. Every use has to be
    // something the receiver parameter can carry - a constant-key read or
    // write, or the receiver of another method call - and every route out of
    // the function is named, because "it leaks `this`" is not a work item and
    // "it is returned" is.
    std::optional<std::string> whyThisLeaks(ctjs::FuncOp target) {
        mlir::Block & entry = target.getBody().front();
        for (mlir::OpOperand & use : entry.getArgument(0).getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0 && !constantKeyOf(get.getKey()).empty()) {
                    continue;
                }
                return "it reads `this` through a dynamic key";
            }
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0 && !constantKeyOf(set.getKey()).empty()) {
                    continue;
                }
                if (use.getOperandNumber() == 2) {
                    return "it stores `this` into another object - that needs an owner, and this "
                           "slice introduces none";
                }
                return "it writes `this` through a dynamic key";
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                return "it returns `this` - the receiver is the caller's frame, so returning it "
                       "would outlive the object";
            }
            if (llvm::isa<ctjs::StoreGlobalOp>(user)) { return "it stores `this` into a global"; }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                // `this.other()`: the receiver of a call this rewrite also
                // makes direct. Any other position is `this` passed as an
                // argument, which has no call site to move it to.
                mlir::Value receiver;
                ctjs::GetPropertyOp load;
                if (use.getOperandNumber() == 1 && resolveMethod(call, receiver, load)) {
                    continue;
                }
                return "it passes `this` to a call this rewrite cannot make direct";
            }
            // AND A CALL --ctjs-resolve-globals ALREADY MADE DIRECT, which is
            // what `f(this)` is by the time this rewrite runs: the closed world
            // named that callee long before, so the operand sits on a
            // ctjs.call_direct and not on a ctjs.call. Without this arm the
            // commonest way there is to leak a receiver got the default
            // sentence - "it reaches `ctjs.call_direct`" - which names the
            // operation and not the mistake.
            if (llvm::isa<ctjs::CallDirectOp>(user)) {
                return "it passes `this` as an argument to another function - a receiver moves "
                       "to the CALL SITE, and an argument position has none to move to (that "
                       "needs a specialised callee, Phase 63, not a lift)";
            }
            return ("it reaches `" + user->getName().getStringRef() +
                    "`, which slice 1 does not carry a receiver through")
                .str();
        }
        return std::nullopt;
    }

    // The method form of whyNotLiftable: conditions 1 to 4, one sentence each.
    // The capture and own-closure clauses are the closure lift's, unchanged -
    // a method IS a closure in the IR, and the two rules compose.
    std::optional<std::string> whyNotLiftableMethod(ctjs::CreateClosureOp c) {
        // THE ARROW GUARD FIRST, and the rest of the target's validity with it:
        // an arrow's `this` is lexical, every use of it reads as a legal
        // constant-key access to condition 3, and admitting one would rebind
        // `this` to the object and answer wrongly rather than refuse.
        if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
        ctjs::FuncOp target = targetOf(c);
        mlir::Block & entry = target.getBody().front();
        // CONDITION 2, the other half: the closure value is used for method
        // stores and nothing else. `methodClosures` says at least one store is
        // one; this says none of them is anything else.
        for (mlir::OpOperand & use : c.getResult().getUses()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
            if (!set || use.getOperandNumber() != 2) {
                return "it is a method field that is also used as a value elsewhere - Phase 59 "
                       "slice 2";
            }
            const auto fields = methodsOf.find(set.getObject());
            if (fields == methodsOf.end() ||
                fields->second.lookup(constantKeyOf(set.getKey())).closure != c) {
                return whyNotAMethodField(set);
            }
        }
        // CONDITION 3.
        if (const std::optional<std::string> leak = whyThisLeaks(target)) { return leak; }
        // AND EVERY CALL OF IT IS ONE THIS RESOLVES. A method field nothing
        // calls has nowhere to move the receiver to, and a call the resolution
        // above could not name would be left dispatching through a closure
        // that is about to lower to nothing.
        const auto calls = callsOfTarget.find(target.getOperation());
        if (calls == callsOfTarget.end()) {
            // A METHOD READ AS A VALUE. `var g = o.m;` loads the field and does
            // not call it, so there is no call site for the receiver to move
            // to and the closure would have to become a value that carries one
            // - a bound function, which is an owner this slice does not build.
            for (mlir::OpOperand & use : c.getResult().getUses()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                if (!set) { continue; }
                const llvm::StringRef key = constantKeyOf(set.getKey());
                for (mlir::Operation * user : set.getObject().getUsers()) {
                    auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                    if (get && constantKeyOf(get.getKey()) == key) {
                        return ("its field `" + key +
                                "` is read as a value rather than called - a method used as a "
                                "function value has to carry its receiver, which is a bound "
                                "function and an owner this slice does not build")
                            .str();
                    }
                }
            }
            return "nothing calls it";
        }
        const unsigned parameters = entry.getNumArguments() - 3;
        for (methodCall at : calls->second) {
            if (at.call.getArgs().size() > parameters) {
                return "a call passes " + std::to_string(at.call.getArgs().size()) +
                       " argument(s) to " + std::to_string(parameters) +
                       " parameter(s) - the surplus has frame semantics";
            }
            auto caller = at.call->getParentOfType<ctjs::FuncOp>();
            if (caller && passesNewTarget.contains(caller.getOperation())) {
                return "a call of it sits in a function that passes new.target";
            }
            // CONDITION 1 AT THE CALL SITE, not only at the literal: the
            // receiver is either a literal this proved closed, or the `%arg0`
            // of a method whose own receivers are.
            if (behind.lookup(at.receiver).empty()) {
                return "it is called on a receiver whose shape is not a proved closed literal";
            }
        }
        // CONDITION 4: the target is otherwise native - which is the existing
        // call-graph fixpoint's question - and its captures lift as Phase 59
        // slice 1 already lifts them. LAST, so that a leaked `this` is reported
        // as a leaked `this` and not as whatever the enclosing frame happened
        // to box for it: `box.held = this` inside a method captures `box`, and
        // a hoisted `var` is always a cell that is written, so asking the
        // capture clause first answered "capture 0 is a binding that is
        // reassigned" for a program whose actual problem is the receiver.
        if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
        // AND THE CAPTURED VALUES REACH THE SITES THIS LIFT IS ABOUT TO
        // REWRITE - which for a method are the calls through the object, not
        // the uses of the closure value.
        for (methodCall at : calls->second) {
            if (const std::optional<std::string> why = whyCapturesDoNotReach(c, at.call)) {
                return why;
            }
        }
        if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) {
            return escapes;
        }
        return whyUpvalueReadsDoNotLift(c, target);
    }

    // --- the constructor lift -----------------------------------------------

    // WHICH CLOSURES ARE USED AS `new` CALLEES AND AS NOTHING ELSE.
    //
    // A closure that is BOTH called and constructed is left to
    // `whyNotLiftable`, which refuses it by name ("it is used as a
    // constructor"): one ctjs.func is one C++ signature, and a body that is a
    // free function at one site and a constructor at another would need two.
    void constructorCensus() {
        llvm::DenseSet<mlir::Operation *> constructed;
        for (ctjs::ConstructOp made : allConstructs) {
            auto closure = made.getCallee().getDefiningOp<ctjs::CreateClosureOp>();
            if (!closure) { continue; }
            constructed.insert(closure.getOperation());
            if (ctjs::FuncOp target = targetOf(closure)) {
                constructsOfTarget[target.getOperation()].push_back(made);
            }
        }
        // EVERY CLOSURE A `new` NAMES, whatever else it does. The narrower set
        // - only those used as nothing but a `new` callee - was the wrong one:
        // a closure that is ALSO written through (`Shape.prototype = {...}`)
        // then fell to `whyNotLiftable`, which met the new.target operand first
        // and answered "it is passed as an argument". The clause a reader needs
        // is the prototype, and only the constructor rule knows to say so.
        constructorClosures.insert(constructed.begin(), constructed.end());
    }

    // IS THIS VALUE PROVABLY NOT `is_object_like()`?
    //
    // THE QUESTION `new` ASKS OF A RETURN, and the reason it has to be asked.
    // `context::construct` ends `return produced.is_object_like() ? produced :
    // self` (vm/call.cpp:695, and :685 for a native), so a constructor that
    // returns an object REPLACES the instance and `new X()` is not the struct
    // this rewrite built at all. `is_object_like()` is
    // `is_object() || is_array() || is_callable() || is_kind(proxy)`
    // (Script/value.hpp:162-164) - note a STRING is not one, so
    // `return "done"` is harmless and still evaluates to the instance.
    //
    // THE ANSWER IS "NO" UNLESS THE DEFINING OPERATION SAYS OTHERWISE, which is
    // the direction that keeps this sound: a block argument, a call result, a
    // property read and anything this list does not name are all assumed to be
    // able to carry an object.
    static bool isNotObjectLike(mlir::Value v) {
        mlir::Operation * definition = v.getDefiningOp();
        if (definition == nullptr) { return false; } // a block argument: unknown
        // ctjs.constant carries undefined, null, a string, a number or a
        // boolean and nothing else; the arithmetic, predicate and conversion
        // operations all answer primitives.
        return llvm::isa<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                         ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::InstanceOfOp,
                         ctjs::HasPropertyOp, ctjs::DeletePropertyOp, ctjs::DeleteNamedOp>(
            definition);
    }

    // GUARD 3: the body must not hand back an object.
    //
    // SHADOWED FOR THE OUTCOME AND NOT FOR THE DIAGNOSTIC, measured. Removing
    // this clause does not admit `returns-object.js`: the `{v: 9}` it hands
    // back is then refused as "an object literal that escapes - it is
    // returned", and an array by the array form of the same sentence. Every
    // object-like value this tier can build already has an escape rule, so no
    // program could be found where this clause alone decides. It is kept, and
    // it runs FIRST, because it is the only one that names what `new` does -
    // and because the clause that shadows it is exactly what Phase 59 slice 2
    // exists to relax, after which this is the only thing between a returning
    // constructor and a wrong answer.
    std::optional<std::string> whyConstructorReturnsAnObject(ctjs::FuncOp target) {
        std::optional<std::string> bad;
        target.getBody().walk([&](ctjs::ReturnOp returned) {
            if (!bad && !isNotObjectLike(returned.getValue())) {
                bad = "its constructor returns a value this pass cannot prove is not an object, "
                      "and a constructor that returns one REPLACES the instance "
                      "(context::construct: `produced.is_object_like() ? produced : self`) - so "
                      "`new` would not evaluate to the struct this rewrite builds";
            }
        });
        return bad;
    }

    // GUARD 5: nothing may touch the constructor function's `prototype`.
    //
    // SHADOWED THE SAME WAY, AND KEPT FOR THE SAME REASON. Removing it leaves
    // `prototype-written.js` refused by the mixed-use clause below - "it is
    // used as a constructor and also reaches `ctjs.set_property`" - which is
    // true and tells a reader nothing about what to do next. This one names
    // Stage 60A.
    //
    // Refused BY NAME rather than falling through to the generic "used as a
    // value elsewhere", because the work item behind it is a specific one: the
    // plan's Stage 60A immutability proof, which is what turns a prototype
    // table into a base class. Without it an instance here has NO chain, so a
    // program that writes `X.prototype.m = ...` and calls `o.m()` would compile
    // to a struct with no `m` at all.
    std::optional<std::string> whyPrototypeIsTouched(ctjs::CreateClosureOp c) {
        for (mlir::Operation * user : c.getResult().getUsers()) {
            llvm::StringRef key;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                key = constantKeyOf(get.getKey());
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                key = constantKeyOf(set.getKey());
            }
            if (key == "prototype") {
                return "its constructor's `prototype` is read or written, which is a prototype "
                       "chain this slice does not build - Stage 60A's immutability proof owns it";
            }
        }
        return std::nullopt;
    }

    // THE CONSTRUCTOR FORM OF whyNotLiftable, one sentence per clause. Every
    // clause it shares with the method lift is CALLED rather than re-typed:
    // the instance is a literal and the constructor is a receiver carrier, so
    // the two rules are the same rule reached through `new`.
    std::optional<std::string> whyNotLiftableConstructor(ctjs::CreateClosureOp c) {
        // THE ARROW GUARD FIRST, as the method form does: an arrow's `this` is
        // lexical, so every use of it reads as a legal constant-key access to
        // the receiver clause below and admitting one would answer wrongly
        // rather than refuse. (An arrow is not a constructor in the VM either -
        // `ensure_prototype` returns undefined for one.)
        if (const std::optional<std::string> why = whyTargetIsNotLiftable(c)) { return why; }
        ctjs::FuncOp target = targetOf(c);
        mlir::Block & entry = target.getBody().front();
        const unsigned parameters = entry.getNumArguments() - 3;
        // GUARD 5, before the generic clauses, so the diagnostic names the
        // prototype rather than whatever else the read happens to be.
        if (const std::optional<std::string> why = whyPrototypeIsTouched(c)) { return why; }
        // AND THE CLOSURE IS USED FOR NOTHING BUT `new`. One ctjs.func is one
        // C++ signature, so a body that is a free function at one site and a
        // constructor at another needs two.
        //
        // `new X(a)` USES THE CLOSURE TWICE ON ONE OPERATION - as the callee
        // AND as new.target, which is what the VM pushes for a construct - so
        // this counts operations and not operands.
        for (mlir::Operation * user : c.getResult().getUsers()) {
            if (!llvm::isa<ctjs::ConstructOp>(user)) {
                return ("it is used as a constructor and also reaches `" +
                        user->getName().getStringRef() +
                        "` - one ctjs.func is one C++ signature, and a body that is a free "
                        "function at one site and a constructor at another needs two")
                    .str();
            }
        }
        // GUARD 2: `this` never escapes the constructor, and every use of it is
        // a constant-key access. Word for word the receiver carrier's
        // condition 3, because it IS that condition.
        if (const std::optional<std::string> leak = whyThisLeaks(target)) { return leak; }
        // GUARD 3.
        if (const std::optional<std::string> why = whyConstructorReturnsAnObject(target)) {
            return why;
        }
        const auto sites = constructsOfTarget.find(target.getOperation());
        if (sites == constructsOfTarget.end() || sites->second.empty()) {
            return "nothing constructs it";
        }
        for (ctjs::ConstructOp made : sites->second) {
            // GUARD 1 AT EVERY SITE, not only at this one: a target is one C++
            // signature, so a second `new` through a callee this pass cannot
            // name would leave that site dispatching through a closure which is
            // about to lower to nothing.
            if (made.getCallee().getDefiningOp<ctjs::CreateClosureOp>() != c) {
                return "it is constructed at a site whose callee this pass cannot name";
            }
            // GUARD 4: the instance's own uses close its shape, by the literal
            // rule, asked of the value the rewrite is about to make a literal.
            if (!usesCloseTheShape(made.getResult())) {
                return "the instance `new` makes does not have a closed shape - every use of it "
                       "has to be a constant-key read or write, or a receiver this lift carries";
            }
            // A CLAUSE WAS HERE AND IS GONE, MEASURED: "the instance's `k` is
            // read but never assigned". It refused every instance that reads a
            // key the constructor does not write, on the grounds that the VM
            // would find it on the prototype. THE HAZARD IS ALREADY COVERED
            // AND MORE PRECISELY: admission's own clause names the keys
            // Object.prototype answers ("field `constructor` is read but never
            // written, and Object.prototype answers that name - the interpreter
            // finds a function where this would find undefined"), and every
            // OTHER unwritten key is undefined in the VM too, exactly as it is
            // for a literal. Removing it, `unwritten-key.js` compiles with no
            // refusal at all and agrees with the interpreter; the clause was
            // strictly narrowing the tier for nothing.
            if (made.getArgs().size() > parameters) {
                return "a `new` passes " + std::to_string(made.getArgs().size()) +
                       " argument(s) to " + std::to_string(parameters) +
                       " parameter(s) - the surplus has frame semantics";
            }
            auto caller = made->getParentOfType<ctjs::FuncOp>();
            if (caller && passesNewTarget.contains(caller.getOperation())) {
                return "a `new` of it sits in a function that passes new.target";
            }
        }
        if (const std::optional<std::string> why = whyCapturesDoNotLift(c)) { return why; }
        // AND THE CAPTURED VALUES REACH EVERY `new` SITE, which the clause
        // above has just established are the only users of the closure value.
        for (mlir::Operation * user : c.getResult().getUsers()) {
            if (const std::optional<std::string> why = whyCapturesDoNotReach(c, user)) {
                return why;
            }
        }
        if (const std::optional<std::string> escapes = whyOwnClosureEscapes(target)) {
            return escapes;
        }
        return whyUpvalueReadsDoNotLift(c, target);
    }

    liftReport run() {
        census();
        // THE ARGUMENT SLOTS FIRST, because `closedAfterLift` reads them and
        // `methodCensus` reads `closedAfterLift`. Both censuses run before any
        // rewrite: a lifted call is a `ctjs.call_direct`, which `closedAfterLift`
        // does not know, so a decision made after the first lift would differ
        // from the same decision made before it.
        // THE `new` SITES FIRST, and the ordering is load-bearing. This census
        // is purely STRUCTURAL - which closures are used as `new` callees and
        // nowhere else - and asks no question about any shape, so it is safe
        // this early. It has to be this early: `closedAfterLift` admits a
        // `ctjs.construct` result as an object, and `argumentCensus`'s fixpoint
        // asks `closedAfterLift` of every argument. Run after, the instance
        // would still be a `ctjs.construct` there, read as "not a literal", and
        // an instance passed to a lifted function would refuse the module.
        constructorCensus();
        argumentCensus();
        methodCensus();
        liftReport out;
        llvm::DenseMap<mlir::Operation *, std::string> reasonOf; // closure -> its own reason
        llvm::DenseSet<mlir::Operation *> lifted;                // closures lift() has taken
        // THE CLOSURE THAT NAMES EACH FUNCTION, for the chained reason below:
        // a capture filled from the enclosing closure is refused with THAT
        // closure's reason, and the enclosing function's closure is the one
        // create_closure whose target it is.
        llvm::DenseMap<mlir::Operation *, ctjs::CreateClosureOp> closureOf;
        for (ctjs::CreateClosureOp c : closures) {
            if (ctjs::FuncOp target = targetOf(c)) { closureOf[target.getOperation()] = c; }
        }
        // CLASSIFY, LIFT, REPEAT - PHASE 59 SLICE 1b. One pass was enough for
        // slice 1 because every capture it carries is a cell of the closure's
        // own frame, decided before any rewrite. A capture filled from the
        // enclosing closure is decided BY a rewrite: it is the importer's
        // ctjs.load_upvalue until the enclosing function lifts, and that
        // function's capture parameter afterwards. So a closure nested one
        // level in is refused in the round its enclosing function lifts and
        // admitted in the next, and a chain of N levels settles in N rounds.
        // Every closure still unlifted is judged again each round - a full
        // re-classification, measured on phaser (7,725 functions) to cost
        // nothing a reader would notice - and the loop stops at the first
        // round that lifts nothing. That round's verdicts are the final ones,
        // and they are the reasons written below.
        //
        // THE BOUND IS THE INVARIANT: a productive round lifts at least one
        // closure and nothing is ever unlifted, so there are at most as many
        // productive rounds as closures, plus the empty one that ends it. A
        // round past that is a rule admitting a closure it does not lift, and
        // this file reports that as a named fatal rather than looping.
        for (unsigned round = 0;; ++round) {
            if (round > closures.size()) {
                llvm::report_fatal_error(
                    "ctnative lowering: the closure lift ran more rounds than there are closures "
                    "- a round that lifts nothing ends the fixpoint and every other round lifts "
                    "at least one closure it never unlifts, so a rule is admitting a closure "
                    "that lift() then leaves in place");
            }
            // Per target: the closures that name it, and the first reason any
            // of them could not be lifted. A target's signature changes for the
            // whole program, so ONE unliftable creation site blocks every other.
            llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CreateClosureOp>> byTarget;
            llvm::DenseMap<mlir::Operation *, std::string> blocked;
            reasonOf.clear();
            chainedThrough.clear();
            for (ctjs::CreateClosureOp c : closures) {
                // A DECLARATION IS A BINDING, NOT A VALUE, and lowers to
                // nothing already. Leave it to admission::isDeclarationClosure.
                if (admissionIsDeclaration(c)) { continue; }
                if (lifted.contains(c.getOperation())) { continue; }
                ctjs::FuncOp target = targetOf(c);
                // A METHOD FIELD IS A DIFFERENT ADMISSION, NOT A SPECIAL CASE
                // OF THE OTHER ONE. Its closure value is never called - it is
                // STORED, which whyNotLiftable refuses by name - and the calls
                // that reach it come through a `get_property` on the object.
                // So the two rules are asked separately and share their
                // capture clauses.
                // AND A CONSTRUCTOR IS A THIRD ADMISSION. Its closure value is
                // never called and never stored - it is the callee of a
                // `ctjs.construct` - so neither of the other two rules
                // describes it, and `whyNotLiftable` refuses it by name ("it is
                // used as a constructor") for the MIXED case this set
                // deliberately excludes: one ctjs.func is one C++ signature,
                // and a body that is a free function at one site and a
                // constructor at another needs two.
                const std::optional<std::string> why =
                    constructorClosures.contains(c.getOperation()) ? whyNotLiftableConstructor(c)
                    : methodClosures.contains(c.getOperation())    ? whyNotLiftableMethod(c)
                                                                   : whyNotLiftable(c);
                if (why) {
                    reasonOf[c.getOperation()] = *why;
                    if (target) { blocked.try_emplace(target.getOperation(), *why); }
                    continue;
                }
                byTarget[target.getOperation()].push_back(c);
            }
            // TWO REFUSALS WERE HERE AND ARE GONE, BECAUSE THEY WERE
            // DECORATION. Both asked what happens when two ctjs.create_closures
            // name ONE ctjs.func - a target that is a method field at one site
            // and a plain closure at another, whose receiver would be an
            // argument in one call and not the other; and a method created
            // twice with captures, whose two capture lists cannot both be one
            // parameter list. Neither is reachable: `compiler_impl` emits
            // exactly one `op::closure` per function proto, so a proto has
            // exactly one creation site, and a probe counting `made.size() > 1`
            // measured ZERO across bootstrap, p5 and phaser (13,053 functions)
            // and all three native fixtures. Removing them changed nothing
            // anywhere, so they are not here.
            //
            // WHAT IS HERE IS THE INVARIANT ITSELF, as a named fatal rather
            // than a refusal, which is this file's idiom for "a rule let one
            // through" (carrierType, memberName, shapeAt, eraseIfUnused). If
            // the reasoning above is ever wrong, lift() would cast a
            // ctjs.set_property to a ctjs.call and crash with no message; this
            // says which claim failed.
            for (auto & [target, made] : byTarget) {
                if (made.size() > 1) {
                    llvm::report_fatal_error(
                        llvm::Twine("ctnative lowering: `") +
                        llvm::cast<ctjs::FuncOp>(target).getSymName() +
                        "` is named by more than one ctjs.create_closure - one function proto "
                        "has one `closure` opcode, and the receiver lift's capture list and its "
                        "method test both assume it");
                }
            }
            unsigned liftedThisRound = 0;
            for (auto & [target, made] : byTarget) {
                if (blocked.contains(target)) {
                    for (ctjs::CreateClosureOp c : made) {
                        reasonOf[c.getOperation()] =
                            "the function it names is also made somewhere this tier cannot "
                            "lift: " +
                            blocked.lookup(target);
                    }
                    continue;
                }
                lift(llvm::cast<ctjs::FuncOp>(target), made, out);
                for (ctjs::CreateClosureOp c : made) { lifted.insert(c.getOperation()); }
                ++liftedThisRound;
            }
            if (liftedThisRound == 0) { break; }
        }
        // THE CHAINED REASON, written only now that the fixpoint has settled
        // every verdict. A closure refused for a capture the enclosing closure
        // fills is refused BECAUSE the enclosing function did not lift, and the
        // sentence a reader can act on names why THAT did not: its own
        // closure's reason, which may itself be chained one level further out.
        // The recursion walks outward through strictly enclosing functions, so
        // it ends; the guard says so as a fatal, not a hang, if it does not.
        llvm::DenseMap<mlir::Operation *, std::string> settled;
        auto finalReason = [&](auto & self, mlir::Operation * op, unsigned depth) -> std::string {
            if (const auto done = settled.find(op); done != settled.end()) { return done->second; }
            if (depth > closures.size()) {
                llvm::report_fatal_error(
                    "ctnative lowering: a chain of `filled from the enclosing closure` reasons is "
                    "longer than the module has closures - the enclosing-function walk has cycled");
            }
            std::string why = reasonOf.lookup(op);
            if (const auto through = chainedThrough.find(op); through != chainedThrough.end()) {
                ctjs::FuncOp enclosing = through->second;
                const auto maker = closureOf.find(enclosing.getOperation());
                if (maker == closureOf.end()) {
                    why += ": no ctjs.create_closure in this module names `" +
                           enclosing.getSymName().str() + "`";
                } else if (lifted.contains(maker->second.getOperation())) {
                    // THE FIXPOINT'S OWN INVARIANT. A closure is chained
                    // through its enclosing function only while that function
                    // is unlifted, and the last round lifted nothing - so the
                    // enclosing function of every chained closure is unlifted
                    // when this runs. One that IS lifted was lifted AFTER the
                    // closure inside it was last judged: the loop stopped one
                    // round early and is about to refuse, with a stale reason,
                    // a closure the next round would have lifted. Cut the
                    // fixpoint back to one pass and this is what fires.
                    llvm::report_fatal_error(
                        llvm::Twine("ctnative lowering: `") + enclosing.getSymName() +
                        "` was lifted after the closure inside it was last judged - the "
                        "classify-then-lift fixpoint stopped before it settled");
                } else if (admissionIsDeclaration(maker->second)) {
                    why += ": `" + enclosing.getSymName().str() +
                           "` is bound to a global by a declaration, which is not lifted";
                } else {
                    why += ": " + self(self, maker->second.getOperation(), depth + 1);
                }
            }
            settled[op] = why;
            return why;
        };
        // The reasons, onto the closures that kept them, so that the function
        // holding one is refused by name. A method field says so, because "a
        // closure used as a value" is a true sentence about `{f: function(){}}`
        // that sends a reader to the wrong slice.
        for (const auto & [op, why] : reasonOf) {
            op->setAttr("ctnative.closure_reason",
                        mlir::StringAttr::get(context, finalReason(finalReason, op, 0)));
            if (methodClosures.contains(op)) {
                op->setAttr("ctnative.method_refusal", mlir::UnitAttr::get(context));
            }
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

        // THE OBJECT PARAMETERS, READ BEFORE THE CAPTURES SHIFT THEM. The
        // census decided in terms of JS parameter numbers; the attribute is
        // written in terms of ENTRY-BLOCK indices, because that is what
        // ctjs.call_direct's operands are and what every reader downstream
        // counts in. Every creation site has to agree - one function is one
        // signature - and `made` holds exactly one closure here, which the
        // named fatal in run() enforces.
        llvm::SmallVector<int32_t> objectArgs;
        for (unsigned j = 0; j < parameters; ++j) {
            if (llvm::all_of(made,
                             [&](ctjs::CreateClosureOp c) { return slotCarriesAnObject(c, j); })) {
                objectArgs.push_back(static_cast<int32_t>(captureArgument(captures) + j));
            }
        }

        // PHASE 59 SLICE 2 STEP 2: WHICH CAPTURES ARRIVE AS A POINTER, read
        // before the arguments shift for the same reason `objectArgs` is, and
        // recorded in ENTRY-BLOCK indices because that is what
        // ctjs.call_direct's operands are. `made` holds exactly one closure -
        // the named fatal in run() enforces it - so `all_of` here is one
        // question asked of one creation site, spelled the way the object
        // parameters are so the two cannot drift.
        llvm::SmallVector<int32_t> cellArgs;
        for (unsigned i = 0; i < captures; ++i) {
            if (llvm::all_of(made,
                             [&](ctjs::CreateClosureOp c) { return slotIsCarried(c, i); })) {
                cellArgs.push_back(static_cast<int32_t>(captureArgument(i)));
            }
        }
        const auto carriedSlot = [&](unsigned k) {
            return llvm::is_contained(cellArgs, static_cast<int32_t>(captureArgument(k)));
        };

        // THE CAPTURES BECOME LEADING PARAMETERS, inserted after the three
        // implicit arguments so that ctjs.call_direct's operand order - which
        // IS the entry block's argument order - still lines up, and so that
        // lower()'s existing `for (i = 3; ...)` picks them up with no change.
        for (unsigned i = 0; i < captures; ++i) {
            entry.insertArgument(captureArgument(i), valueType, target.getLoc());
        }
        llvm::SmallVector<mlir::Type> inputs(entry.getNumArguments(), valueType);
        target.setFunctionTypeAttr(
            mlir::TypeAttr::get(mlir::FunctionType::get(context, inputs, {valueType})));
        // THE ATTRIBUTE BEFORE THE REWRITE, because a nested closure inside
        // this body is judged in a LATER round and `slotIsCarried` reads it
        // off this function to decide whether its own slot is a pointer -
        // slice 1b, one indirection out.
        if (!cellArgs.empty()) {
            target->setAttr("ctnative.cell_args",
                            mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
            out.carried += static_cast<unsigned>(cellArgs.size());
        }

        llvm::SmallVector<ctjs::LoadUpvalueOp> reads;
        target.getBody().walk([&](ctjs::LoadUpvalueOp read) { reads.push_back(read); });
        for (ctjs::LoadUpvalueOp read : reads) {
            const auto k = static_cast<unsigned>(read.getIndex());
            const mlir::Value slot = entry.getArgument(captureArgument(k));
            if (carriedSlot(k)) {
                // A READ THROUGH THE POINTER. The parameter holds the ADDRESS
                // of the owning frame's variable, so the value is one
                // indirection away - which is exactly what ctjs.cell_get says
                // and what the emitter turns into `*p`. The interpreter reads
                // the box when the closure runs (run_loop.cpp, get_upvalue:
                // `reg = cell->slot`); this reads it when the call runs, which
                // is the same moment.
                mlir::OpBuilder at(read);
                auto through = ctjs::CellGetOp::create(at, read.getLoc(), valueType, slot);
                read.getResult().replaceAllUsesWith(through.getResult());
            } else {
                read.getResult().replaceAllUsesWith(slot);
            }
            read.erase();
        }
        // AND THE WRITES, WHICH ARE THE WHOLE OF THIS STEP.
        // whyUpvalueReadsDoNotLift has already refused any target whose store
        // names a slot this closure does not carry, so every one of these is a
        // pointer - and reaching one that is not means that rule and this
        // rewrite have drifted, which is a fatal and never a number.
        llvm::SmallVector<ctjs::StoreUpvalueOp> writes;
        target.getBody().walk([&](ctjs::StoreUpvalueOp write) { writes.push_back(write); });
        for (ctjs::StoreUpvalueOp write : writes) {
            const auto k = static_cast<unsigned>(write.getIndex());
            if (!carriedSlot(k)) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: `") + target.getSymName() +
                    "` writes capture " + llvm::Twine(k) +
                    ", which the lift did not carry by pointer - whyUpvalueReadsDoNotLift "
                    "admitted a store the rewrite cannot place");
            }
            mlir::OpBuilder at(write);
            ctjs::CellSetOp::create(at, write.getLoc(), entry.getArgument(captureArgument(k)),
                                    write.getValue());
            write.erase();
        }
        // NO UPVALUES LEFT, and the attribute says so: after this the function
        // reads its bindings out of its own frame like any other parameter.
        target->setAttr("upvalue_count", mlir::Builder(context).getI32IntegerAttr(0));
        target->setAttr("ctnative.captures",
                        mlir::Builder(context).getI32IntegerAttr(static_cast<int>(captures)));
        if (!objectArgs.empty()) {
            target->setAttr("ctnative.object_args",
                            mlir::Builder(context).getDenseI32ArrayAttr(objectArgs));
            out.objects += static_cast<unsigned>(objectArgs.size());
        }
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

        // A METHOD, AND WHETHER ITS RECEIVER IS A PARAMETER AT ALL. The lift
        // marks `ctnative.receiver` only when `%arg0` is READ: a method that
        // never touches `this` needs no receiver, so the call passes undefined
        // instead of the object, exactly as the importer passes undefined for
        // a non-arrow's `$enclosing_this`. That is not a nicety - an emitted
        // parameter nothing reads is `-Wunused-parameter` under -Werror, and
        // passing the object would be a use of it that opens its shape for no
        // gain.
        const bool constructor = llvm::all_of(made, [&](ctjs::CreateClosureOp c) {
            return constructorClosures.contains(c.getOperation());
        });
        const bool method = !constructor && callsOfTarget.count(target.getOperation()) != 0 &&
                            llvm::all_of(made, [&](ctjs::CreateClosureOp c) {
                                return methodClosures.contains(c.getOperation());
                            });
        // A CONSTRUCTOR ALWAYS CARRIES ITS RECEIVER WHEN IT TOUCHES `this` -
        // the instance IS the receiver - and the test is the same one a method
        // gets: `%arg0` read at all. A constructor that never touches `this`
        // builds the empty shape, and passing it would be a use that opens it
        // for no gain.
        const bool carriesReceiver = (method || constructor) && !entry.getArgument(0).use_empty();
        if (carriesReceiver) {
            target->setAttr("ctnative.receiver", mlir::UnitAttr::get(context));
            ++out.receivers;
        }

        if (constructor) {
            // `new X(a, b)` BECOMES A LITERAL PLUS THE CALL THE RECEIVER LIFT
            // ALREADY EMITS, and that is the whole lowering.
            //
            // The instance is an EMPTY ctjs.create_object placed where the
            // `new` was, and the constructor is entered through a
            // ctjs.call_direct carrying it as operand 0 with
            // `ctnative.receiver` on the call. After this rewrite there is no
            // constructor in the IR at all - only a closed object literal and a
            // free function that writes through a pointer to it - so
            // `hasClosedShape`, `groupReceivers`, `fieldsOf`, `censusShapes`
            // and `replace` need no constructor case, and the instance lowers
            // to a frame-scope `ctn_X` variable like any other literal. Zero
            // allocation, and the object lives in the caller's frame.
            //
            // $callee_value IS UNDEFINED, as it is in the method arm and for
            // the same reason: the native call arm drops operand 2, this
            // rewrite runs inside --ctnative-lower-to-emitc, and the boxed tier
            // never sees the op. The closure is erased below.
            for (ctjs::CreateClosureOp c : made) {
                c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
                ++out.closures;
                out.captures += captures;
            }
            llvm::SmallVector<mlir::Value> captured;
            ctjs::CreateClosureOp only = made.front();
            for (unsigned i = 0; i < static_cast<unsigned>(only.getUpvalues().size()); ++i) {
                captured.push_back(liftedCapture(only, i));
            }
            for (ctjs::ConstructOp built : constructsOfTarget[target.getOperation()]) {
                mlir::OpBuilder at(built);
                const mlir::Value undefined = ctjs::ConstantOp::create(
                    at, built.getLoc(), valueType, ctjs::UndefinedAttr::get(context));
                // THE INSTANCE. An empty literal: every field it has, the
                // constructor writes through `this`, and `fieldsOf` collects
                // those over the alias group the receiver mark creates.
                auto instance = ctjs::CreateObjectOp::create(at, built.getLoc(), valueType);
                llvm::SmallVector<mlir::Value> arguments(captured);
                arguments.append(built.getArgs().begin(), built.getArgs().end());
                while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
                auto direct = ctjs::CallDirectOp::create(
                    at, built.getLoc(), valueType,
                    mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                    carriesReceiver ? instance.getResult() : undefined, undefined, undefined,
                    arguments, /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
                if (carriesReceiver) {
                    direct->setAttr("ctnative.receiver", mlir::UnitAttr::get(context));
                }
                // AND WHICH OF ITS ARGUMENTS IS AN ADDRESS. On the CALL as
                // well as the callee, for the reason the receiver mark is on
                // both: `replace()` reads it once per operand and a symbol
                // lookup there would be a lookup per argument per call.
                if (!cellArgs.empty()) {
                    direct->setAttr("ctnative.cell_args",
                                    mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
                }
                // AND `new` EVALUATES TO THE INSTANCE, NOT TO THE CALL. That is
                // the whole of context::construct's last line for a body this
                // rule admits: `produced.is_object_like() ? produced : self`,
                // and whyConstructorReturnsAnObject has just proved `produced`
                // is never object-like, so the answer is always `self`.
                built.getResult().replaceAllUsesWith(instance.getResult());
                built.erase();
                ++out.calls;
                ++out.constructors;
            }
            return;
        }

        if (method) {
            // THE FIELD LOWERS TO NOTHING, and so does the closure in it: the
            // method is a free function, not a member. The reasons are written
            // as attributes because admission meets the store and the closure
            // long before it meets the call.
            for (ctjs::CreateClosureOp c : made) {
                for (mlir::Operation * user : c.getResult().getUsers()) {
                    user->setAttr("ctnative.method", mlir::UnitAttr::get(context));
                }
                c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
                ++out.closures;
                ++out.methods;
                out.captures += captures;
            }
            ctjs::CreateClosureOp only = made.front();
            llvm::SmallVector<mlir::Value> captured;
            for (unsigned i = 0; i < static_cast<unsigned>(only.getUpvalues().size()); ++i) {
                captured.push_back(liftedCapture(only, i));
            }
            for (methodCall at : callsOfTarget[target.getOperation()]) {
                mlir::OpBuilder builder(at.call);
                const mlir::Value undefined = ctjs::ConstantOp::create(
                    builder, at.call.getLoc(), valueType, ctjs::UndefinedAttr::get(context));
                llvm::SmallVector<mlir::Value> arguments(captured);
                arguments.append(at.call.getArgs().begin(), at.call.getArgs().end());
                while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
                // THE RECEIVER IS OPERAND 0 AND ALWAYS WAS. ctjs.call_direct's
                // operands ARE the callee's entry block in order, so passing
                // the object here is the whole of "the receiver is a
                // parameter" - `%arg0` is where it lands with no reordering.
                //
                // $callee_value IS UNDEFINED, and this is the one place this
                // rewrite differs from the closure lift. There, the closure
                // value still exists and is passed. Here the callee VALUE is
                // `at.load`, a property read of the object, and keeping it
                // would leave a `!ctnative.boxed` result in an accepted
                // function for no consumer: the native call arm drops operand
                // 2, and this rewrite runs inside --ctnative-lower-to-emitc so
                // the boxed tier never sees the op. The load is erased below.
                auto direct = ctjs::CallDirectOp::create(
                    builder, at.call.getLoc(), valueType,
                    mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                    carriesReceiver ? at.receiver : undefined, undefined, undefined, arguments,
                    /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
                if (carriesReceiver) {
                    direct->setAttr("ctnative.receiver", mlir::UnitAttr::get(context));
                }
                // AND WHICH OF ITS ARGUMENTS IS AN ADDRESS. On the CALL as
                // well as the callee, for the reason the receiver mark is on
                // both: `replace()` reads it once per operand and a symbol
                // lookup there would be a lookup per argument per call.
                if (!cellArgs.empty()) {
                    direct->setAttr("ctnative.cell_args",
                                    mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
                }
                at.call.getResult().replaceAllUsesWith(direct.getResult());
                at.call.erase();
                // AND THE METHOD LOAD GOES WITH IT. Leaving it would keep a
                // use of the object that `hasClosedShape` reads as open, which
                // would refuse the very literal this rewrite just admitted.
                //
                // WITH ITS KEY CONSTANT, IF NOTHING ELSE HOLDS ONE. A string
                // constant with no users left is not a property key any more -
                // `isKeyOnlyString` needs a use to recognise one - so it falls
                // through admission to "a constant that is not a number, a
                // boolean or undefined" and refuses the whole function. The
                // importer may or may not share the constant with the store
                // that binds the field, so this asks rather than assumes.
                mlir::Value key = at.load.getKey();
                at.load.erase();
                if (mlir::Operation * made = key.getDefiningOp();
                    made != nullptr && made->use_empty()) {
                    made->erase();
                }
                ++out.calls;
            }
            return;
        }

        for (ctjs::CreateClosureOp c : made) {
            // THE VALUE, NOT THE CELL, in both shapes: a constant cell's
            // initial, or the enclosing function's capture parameter passed
            // as it is - it already holds the value (slice 1b). The slot's
            // INDEX is what selects between them, because a slot the enclosing
            // closure fills carries a placeholder operand and nothing else.
            llvm::SmallVector<mlir::Value> captured;
            for (unsigned i = 0; i < static_cast<unsigned>(c.getUpvalues().size()); ++i) {
                captured.push_back(liftedCapture(c, i));
            }
            // BOTH SHAPES OF CALL SITE, because --ctjs-resolve-globals may have
            // named this one already. `whyNotLiftable` admits a ctjs.call at
            // operand 0 and a ctjs.call_direct at operand 2; the difference
            // between them is where the receiver and the arguments are read
            // from, and nothing else - the captures still have to be prepended,
            // which is the whole reason the lift re-writes an already-direct
            // call rather than leaving it alone.
            llvm::SmallVector<mlir::Operation *> calls(c.getResult().getUsers().begin(),
                                                       c.getResult().getUsers().end());
            for (mlir::Operation * user : calls) {
                auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                auto named = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                mlir::OpBuilder at(user);
                const mlir::Value undefined = ctjs::ConstantOp::create(
                    at, user->getLoc(), valueType, ctjs::UndefinedAttr::get(context));
                llvm::SmallVector<mlir::Value> arguments(captured);
                const mlir::ValueRange supplied = call ? call.getArgs() : named.getArgs();
                arguments.append(supplied.begin(), supplied.end());
                // The resolver's own padding rule: op::call fills a missing
                // parameter with undefined, so a short call becomes a full one.
                while (arguments.size() < captures + parameters) { arguments.push_back(undefined); }
                auto direct = ctjs::CallDirectOp::create(
                    at, user->getLoc(), valueType,
                    mlir::FlatSymbolRefAttr::get(target.getSymNameAttr()),
                    call ? call.getReceiver() : named.getReceiver(), undefined, c.getResult(),
                    arguments,
                    /*arg_attrs=*/nullptr, /*res_attrs=*/nullptr);
                // ON THE CALL AS WELL AS THE CALLEE, for the reason the
                // receiver mark is: `TypeInference::hasClosedShape` is asked
                // once per property access per solver visit, and a symbol
                // lookup there would be quadratic in the module.
                if (!objectArgs.empty()) {
                    direct->setAttr("ctnative.object_args",
                                    mlir::Builder(context).getDenseI32ArrayAttr(objectArgs));
                }
                if (!cellArgs.empty()) {
                    direct->setAttr("ctnative.cell_args",
                                    mlir::Builder(context).getDenseI32ArrayAttr(cellArgs));
                }
                user->getResult(0).replaceAllUsesWith(direct.getResult());
                user->erase();
                ++out.calls;
            }
            c->setAttr("ctnative.lifted", mlir::UnitAttr::get(context));
            ++out.closures;
            out.captures += captures;
        }
    }

    // WHY A CARRIED CELL CANNOT BE A FRAME-LOCAL VARIABLE AFTER ALL, or
    // nothing. Asked once, after the lift's fixpoint has settled every verdict.
    //
    // THE USE LIST IS THE CENSUS'S PLUS ONE: lift() added the cell as an
    // ARGUMENT of every ctjs.call_direct it wrote, at an index
    // `ctnative.cell_args` lists - the address the callee reads through. That
    // use did not exist when sharedCellCensus ran and is the reason the census
    // cannot simply be re-run here.
    // `ctnative.cell_args` ON A CALL, ASKED HERE. admission has the same
    // predicate and is declared below this struct, so this is the one line of
    // it that has to be spelled twice - kept to one line for the reason
    // admissionIsDeclaration is: two copies of a rule drift, two copies of a
    // lookup cannot.
    static bool namesACellArgument(mlir::Operation * call, mlir::OpOperand & use) {
        auto listed = call->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
        return listed && llvm::is_contained(listed.asArrayRef(),
                                            static_cast<int32_t>(use.getOperandNumber()));
    }

    std::optional<std::string> whyCarriedCellStaysABox(ctjs::CreateCellOp cell) {
        for (mlir::OpOperand & use : cell.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<ctjs::CellSetOp>(user) && use.getOperandNumber() == 0) { continue; }
            if (llvm::isa<ctjs::CallDirectOp>(user) && namesACellArgument(user, use)) { continue; }
            if (llvm::isa<ctjs::CreateClosureOp>(user) &&
                use.getOperandNumber() >= kFirstCapture) {
                if (user->hasAttr("ctnative.lifted")) { continue; }
                auto reason = user->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
                return "the closure that shares it is not lifted, so the binding needs a real "
                       "box and not a variable in this frame" +
                       (reason ? " - " + reason.getValue().str() : std::string{});
            }
            return ("it reaches `" + user->getName().getStringRef() +
                    "` after the lift, which is not a use of a frame-local variable")
                .str();
        }
        return std::nullopt;
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
            // PHASE 59 SLICE 2 STEP 2: A CARRIED CELL IS NOT UNBOXED. There is
            // no one value to write over its reads - that is why it is
            // carried - so the box stays in the IR and becomes an
            // `emitc.variable` of its carrier, with every read a load of it,
            // every write an assign to it, and every lifted call passing its
            // address.
            //
            // WHAT HAS TO BE ASKED HERE AND NOWHERE ELSE: that every closure
            // capturing it actually lifted. The census could not ask - the
            // verdicts did not exist yet - and it is the condition the whole
            // pointer argument rests on, because an UNLIFTED closure over this
            // binding is a real ctjs.create_closure that needs a real box, and
            // a stack variable is not one. The function is refused either way
            // (an unlifted closure has no lowering), but the sentence has to
            // name the binding rather than the operation.
            if (isCarried(cell)) {
                if (const std::optional<std::string> why = whyCarriedCellStaysABox(cell)) {
                    cell->setAttr("ctnative.cell_reason", mlir::StringAttr::get(context, *why));
                    continue;
                }
                cell->setAttr("ctnative.carried", mlir::UnitAttr::get(context));
                ++out.locals;
                continue;
            }
            // PHASE 59 SLICE 2 STEP 1: THE ONE WRITE THIS CELL IS ALLOWED, or
            // null. The census proved it dominates every read and every
            // capture, so from it onwards the box holds one value and the box
            // itself is not needed.
            ctjs::CellSetOp write = writtenOnce.lookup(cell.getOperation());
            std::optional<std::string> why;
            for (mlir::OpOperand & use : cell.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (llvm::isa<ctjs::CellGetOp>(user) && use.getOperandNumber() == 0) { continue; }
                if (write && user == write.getOperation()) { continue; }
                if (llvm::isa<ctjs::CreateClosureOp>(user) &&
                    use.getOperandNumber() >= kFirstCapture) {
                    if (user->hasAttr("ctnative.lifted")) { continue; }
                    auto reason = user->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
                    why = "the closure that captures it is not lifted" +
                          (reason ? " - " + reason.getValue().str() : std::string{});
                    break;
                }
                if (llvm::isa<ctjs::CellSetOp>(user)) {
                    // A WRITE THE RULE ABOVE DID NOT TAKE, and the census knows
                    // which clause it failed. Without that sentence the message
                    // is the old one, which is true of a cell with two writes
                    // and says nothing about which of them is the problem.
                    const auto named = whyNotWrittenOnce.find(cell.getOperation());
                    why = named != whyNotWrittenOnce.end()
                              ? named->second
                              : std::string{"it is assigned after it was boxed, so its value is "
                                            "not the one the cell was built with"};
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
            // THE VALUE, WHICHEVER OF THE TWO IT IS. `constantValueOf` is the
            // one place that decides, and capturedValue() has already handed
            // the SAME value to every lifted call site of every closure that
            // captured this cell - which is why the two cannot be allowed to
            // disagree and are one function.
            const mlir::Value value = constantValueOf(cell);
            for (ctjs::CellGetOp read : reads) {
                read.getResult().replaceAllUsesWith(value);
                read.erase();
            }
            // AND THE WRITE GOES WITH THE BOX. There is no box left to write:
            // every read has been replaced by what the write stored, so a
            // ctjs.cell_set left behind would name a create_cell nothing else
            // uses and refuse the whole function for an operation with nothing
            // to do. Erased after the reads, because both use the cell.
            if (write) { write.erase(); }
            cell->setAttr("ctnative.unboxed", mlir::UnitAttr::get(context));
            ++out.cells;
        }
    }
};

// --- the admission check --------------------------------------------------------

// THE VALUES THAT NAME ONE OBJECT. Built once per pass run by
// TypeInference::groupReceivers - one walk, shared by admission and by the
// shape census - because a lifted method's `%arg0` and the literal it is called
// on are two values with one set of fields between them.
using receiverGroups = llvm::DenseMap<mlir::Value, llvm::SmallVector<mlir::Value, 2>>;

// Every value that names the same object as `v`, `v` included. A literal
// nothing is lifted onto is a group of one, so callers need no special case.
llvm::SmallVector<mlir::Value, 2> aliasesOf(const receiverGroups * groups, mlir::Value v) {
    if (groups != nullptr) {
        const auto entry = groups->find(v);
        if (entry != groups->end()) { return entry->second; }
    }
    return {v};
}

struct admission {
    mlir::DataFlowSolver & solver;
    std::string why;
    // Null in the unit tests that construct an admission directly; every path
    // through it then reads a literal's own uses, which is what it did before
    // a receiver could be a parameter.
    const receiverGroups * groups = nullptr;
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
        const llvm::StringRef what =
            o->hasAttr("ctnative.method_refusal") ? "a method field" : "a closure used as a value";
        auto why = o->getAttrOfType<mlir::StringAttr>("ctnative.closure_reason");
        return why ? (what + ": " + why.getValue()).str() : (what + " - Phase 59").str();
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

    // THE ENTRY-BLOCK INDICES THAT CARRY A `ctn_x *`, off a ctjs.func or off
    // one of its ctjs.call_directs - the same list on both, which is what lets
    // the caller and the callee be lowered in either order.
    static llvm::ArrayRef<int32_t> objectArgsOf(mlir::Operation * o) {
        auto listed = o->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.object_args");
        return listed ? listed.asArrayRef() : llvm::ArrayRef<int32_t>{};
    }
    static bool isObjectArg(mlir::Operation * o, unsigned index) {
        return llvm::is_contained(objectArgsOf(o), static_cast<int32_t>(index));
    }
    // AND THE ONES THAT CARRY A `double *` - PHASE 59 SLICE 2 STEP 2. Same
    // shape, same two places, same reason: the shared binding's box is a
    // variable in the CALLER's frame and the callee reads and writes it
    // through a pointer.
    static llvm::ArrayRef<int32_t> cellArgsOf(mlir::Operation * o) {
        auto listed = o->getAttrOfType<mlir::DenseI32ArrayAttr>("ctnative.cell_args");
        return listed ? listed.asArrayRef() : llvm::ArrayRef<int32_t>{};
    }
    static bool isCellArg(mlir::Operation * o, unsigned index) {
        return llvm::is_contained(cellArgsOf(o), static_cast<int32_t>(index));
    }
    // A `ctjs.create_cell` the lift made a frame-local variable.
    static bool isCarriedCell(mlir::Operation * o) {
        return llvm::isa_and_nonnull<ctjs::CreateCellOp>(o) && o->hasAttr("ctnative.carried");
    }
    // A capture parameter that arrived as one - an entry-block argument of a
    // ctjs.func whose `ctnative.cell_args` lists its number.
    static bool isCellParameter(mlir::Value v) {
        auto arg = llvm::dyn_cast<mlir::BlockArgument>(v);
        if (!arg || !arg.getOwner()->isEntryBlock()) { return false; }
        auto fn = llvm::dyn_cast<ctjs::FuncOp>(arg.getOwner()->getParentOp());
        return fn && isCellArg(fn.getOperation(), arg.getArgNumber());
    }
    // WHERE A SHARED BINDING IS REACHED FROM: the variable in this frame, or
    // the pointer a lifted call handed this one. Both are lvalues after
    // lowering, and every rule below that asks about a cell asks about either.
    static bool namesASharedCell(mlir::Value v) {
        return isCarriedCell(v.getDefiningOp()) || isCellParameter(v);
    }
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
    // THE SENTENCE THE ARGUMENT RULE LEFT ON THE LITERAL, or empty. Written by
    // `closureLifter::argumentCensus` before any rewrite ran, for the same
    // reason `ctnative.closure_reason` and `ctnative.cell_reason` are: every
    // condition that can fail is a property of the CALLEE, and a walk that
    // meets the escape has only the use.
    static std::string argumentReason(mlir::Value object) {
        mlir::Operation * literal = object.getDefiningOp();
        auto why =
            literal ? literal->getAttrOfType<mlir::StringAttr>("ctnative.object_reason") : nullptr;
        return why ? "an object literal that escapes - " + why.getValue().str() : std::string{};
    }

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
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                // A METHOD CALL THE RECEIVER LIFT DID NOT TAKE. It is still a
                // ctjs.call, so the object is passed to a dispatcher this tier
                // cannot see through; the reason the lift gave is on the
                // closure the field holds, and this points at it.
                if (use.getOperandNumber() == 1) {
                    return "an object literal whose method call was not lifted - it is still "
                           "dispatched through the callee value, so the object is passed to "
                           "something that could add or remove a field";
                }
                // AN ARGUMENT THE OBJECT-PARAMETER RULE DID NOT TAKE, and the
                // rule's own sentence rather than this one: `argumentCensus`
                // wrote it onto the literal, because the condition that failed
                // is a property of the CALLEE and this walk only has the use.
                if (const std::string why = argumentReason(object); !why.empty()) { return why; }
                return "an object literal that escapes - it is passed to a call";
            }
            if (llvm::isa<ctjs::CallDirectOp>(user)) {
                // A DIRECT CALL CARRIES A RECEIVER AND ANY PARAMETER THE LIFT
                // LISTED; anything else in an operand is still an escape.
                if (use.getOperandNumber() == 0 && user->hasAttr("ctnative.receiver")) { continue; }
                if (isObjectArg(user, use.getOperandNumber())) { continue; }
                // AND THIS IS WHERE THE ARGUMENT REASON USUALLY LANDS, NOT THE
                // ctjs.call ARM ABOVE. A callee whose parameter this rule
                // refused is still a closure the PLAIN lift takes - its uses
                // are all calls of it - so by the time admission asks, the
                // call is a ctjs.call_direct and the ctjs.call is gone. The
                // measured shape: all four negative programs reached here.
                if (const std::string why = argumentReason(object); !why.empty()) { return why; }
                return "an object literal passed to a direct call as an argument - only a "
                       "receiver and a parameter the lift proved read-only are carried";
            }
            return ("an object literal that escapes - it reaches `" +
                    user->getName().getStringRef() + "`")
                .str();
        }
        return "an object literal whose shape is not closed";
    }

    // WHY A LIFTED METHOD'S `this` IS NO LONGER CLOSED: the first use of
    // `%arg0` that is not a constant-key access or a lifted method call, named
    // by what it is. In practice there is one route - a `this.other()` whose
    // callee was refused, so the call stayed a `ctjs.call` - and the sentence
    // has to say that rather than repeat the whole rule.
    static std::string whyOpenReceiver(mlir::Value self) {
        for (mlir::OpOperand & use : self.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (llvm::isa<ctjs::GetPropertyOp, ctjs::SetPropertyOp>(user) &&
                use.getOperandNumber() == 0) {
                continue;
            }
            if (llvm::isa<ctjs::CallDirectOp>(user) && use.getOperandNumber() == 0 &&
                user->hasAttr("ctnative.receiver")) {
                continue;
            }
            if (llvm::isa<ctjs::CallDirectOp>(user) && isObjectArg(user, use.getOperandNumber())) {
                continue;
            }
            if (llvm::isa<ctjs::CallOp>(user)) {
                return "it calls another method on itself that this tier did not lift";
            }
            return ("it reaches `" + user->getName().getStringRef() + "`").str();
        }
        return "no use of it opens the shape, so the lift and this check disagree";
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
        // THE METHOD FIELD'S STORE LOWERS TO NOTHING, with the closure in it.
        // Checked before the closed-shape arms below because the value it
        // stores is a closure, which has no carrier and would be refused as a
        // field the moment the literal is examined.
        if (o->hasAttr("ctnative.method")) { return true; }
        if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
            if (!isClosedObject(object.getResult())) { return refuse(whyOpen(object.getResult())); }
            // EVERY ACCESS IN THE GROUP, NOT ONLY THE LITERAL'S OWN. A lifted
            // method reaches these fields through its `%arg0`, in a different
            // function, and those reads and writes are this shape's too - so
            // `this.class = 1` has to be refused HERE, at the one place that
            // checks a key, or it reaches the emitter as `self->class`.
            llvm::SmallVector<mlir::Operation *> accesses;
            for (mlir::Value alias : aliasesOf(groups, object.getResult())) {
                for (mlir::OpOperand & use : alias.getUses()) {
                    if (use.getOperandNumber() == 0 &&
                        llvm::isa<GetPropertyOp, SetPropertyOp>(use.getOwner())) {
                        accesses.push_back(use.getOwner());
                    }
                }
            }
            // The keys this literal is ever WRITTEN with. A read of one of
            // them is an own property; a read of anything else falls through
            // to the prototype, which is what makes an inherited name wrong.
            llvm::StringSet<> written;
            for (mlir::Operation * user : accesses) {
                if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                    // A METHOD FIELD IS NOT A FIELD. Its store lowers to
                    // nothing and it takes no space in the class, so it is not
                    // a key that shadows an inherited name either.
                    if (!user->hasAttr("ctnative.method")) { written.insert(keyOf(set.getKey())); }
                }
            }
            // The carrier each key has been STORED so far, for the one-carrier
            // check below.
            llvm::StringMap<carrier> storedCarrier;
            for (mlir::Operation * user : accesses) {
                if (user->hasAttr("ctnative.method")) { continue; }
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
            // new.target and the callee value are dropped; every argument is a
            // number. Whether the CALLEE is native is the fixpoint in
            // runOnOperation, not a question for one function.
            //
            // THE RECEIVER IS NOT DROPPED WHEN THE LIFT MARKED THIS CALL. It
            // is operand 0, it becomes the callee's first C++ parameter, and
            // its carrier is the generated class - so what has to hold here is
            // that it really is a closed-shape object and not some other
            // value the lift never looked at.
            const auto operands = call.getArgOperands();
            if (o->hasAttr("ctnative.receiver") && !isClosedObject(call.getReceiver())) {
                return refuse("a method call whose receiver is not a closed-shape object");
            }
            for (unsigned i = 3; i < operands.size(); ++i) {
                // AN OBJECT ARGUMENT IS NOT A NUMBER AND MUST NOT BE ASKED TO
                // BE ONE. The lift proved the literal closed before it wrote
                // the index; this asks the same question of the IR that came
                // out, exactly as the receiver arm above does, because a
                // refusal since then can have opened it.
                if (isObjectArg(o, i)) {
                    if (!isClosedObject(operands[i])) {
                        return refuse("it passes an object whose shape is no longer closed to a "
                                      "parameter this tier gave a pointer");
                    }
                    continue;
                }
                // A SHARED BINDING IS NOT A NUMBER EITHER: what is passed is
                // the ADDRESS of a variable in this frame, so what has to hold
                // is that the variable is still one this tier can spell and
                // that the operand still names it. A carrier of `none` is
                // refused at the box itself; this catches the operand that
                // stopped being a box at all, which would be the lift and this
                // check disagreeing.
                if (isCellArg(o, i)) {
                    if (!namesASharedCell(operands[i])) {
                        return refuse("it passes something that is not a shared binding of this "
                                      "frame to a parameter this tier gave a pointer");
                    }
                    if (carrierOf(typeOf(operands[i])) == carrier::none) {
                        return refuse("it passes a shared binding of type " +
                                      printed(typeOf(operands[i])) +
                                      ", which has no native carrier");
                    }
                    continue;
                }
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
        // PHASE 59 SLICE 2 STEP 2: THE SHARED BINDING'S BOX IS A VARIABLE IN
        // THIS FRAME, and this is where its carrier is proved. The lift ran
        // before the solve and could not ask; nothing else between here and
        // the emitter does. A cell of a type with no C++ representation is
        // refused HERE, so no pointer to one is ever taken - which is the
        // second of the three conditions the design rests on, and the only one
        // that cannot be asked at the lift.
        if (auto cell = llvm::dyn_cast<CreateCellOp>(o); cell && isCarriedCell(o)) {
            if (carrierOf(typeOf(cell.getResult())) == carrier::none) {
                return refuse("a shared binding of type " + printed(typeOf(cell.getResult())) +
                              ", which has no native carrier - a variable this tier cannot "
                              "spell is not one it may point at");
            }
            return true;
        }
        // A READ AND A WRITE OF ONE, in this frame or through the pointer a
        // lifted call handed us. The read's result carrier is asked by the
        // per-result walk in function(); what is asked here is that the write
        // stores the carrier the variable holds - two carriers in one variable
        // is a `double` assigned a `bool`, and the join that produced the
        // cell's type would have had no carrier at all, so this is belt to
        // that brace and names the store rather than the box.
        if (auto get = llvm::dyn_cast<CellGetOp>(o); get && namesASharedCell(get.getCell())) {
            return true;
        }
        if (auto set = llvm::dyn_cast<CellSetOp>(o); set && namesASharedCell(set.getCell())) {
            const carrier held = carrierOf(typeOf(set.getCell()));
            const carrier stored = carrierOf(typeOf(set.getValue()));
            if (held == carrier::none || stored != held) {
                return refuse("an assignment of " + printed(typeOf(set.getValue())) +
                              " to a shared binding of type " + printed(typeOf(set.getCell())));
            }
            return true;
        }
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
        // THE RECEIVER IS A PARAMETER when the lift said so, and %arg0 is then
        // the one implicit argument that HAS a carrier: the generated class of
        // the shape every call site passes. The lift proved every use of it is
        // a constant-key access or another lifted method call; this asks the
        // same question of the IR that came out, because the lift's proof was
        // made before its own rewrite ran and a later refusal - the callee of
        // a `this.other()` that was not lifted - can have opened it since.
        const bool carriesReceiver = fn->hasAttr("ctnative.receiver");
        if (carriesReceiver && !isClosedObject(entry.getArgument(0))) {
            return refuse("its `this` is no longer a closed-shape receiver - " +
                          whyOpenReceiver(entry.getArgument(0)));
        }
        // THE THREE IMPLICIT ARGUMENTS - receiver, new.target, callee - have no
        // native carrier and must be unused.
        for (unsigned i = carriesReceiver ? 1 : 0; i < 3 && i < entry.getNumArguments(); ++i) {
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
            // THE OBJECT PARAMETERS, ASKED THE RECEIVER'S QUESTION. Their
            // carrier is the generated class one indirection away, so
            // `carrierOf` has no row for them and the loop below would refuse
            // every one; what has to hold is that the shape is still closed.
            if (isObjectArg(fn.getOperation(), i)) {
                if (!isClosedObject(entry.getArgument(i))) {
                    return refuse("parameter " + std::to_string(i - 3 - captures) +
                                  " is no longer a closed-shape object - " +
                                  whyOpenReceiver(entry.getArgument(i)));
                }
                continue;
            }
            const mlir::Type t = typeOf(entry.getArgument(i));
            const bool isCapture = i - 3 < captures;
            // PHASE 59 SLICE 2 STEP 2: A SHARED CAPTURE SAYS SO. Its carrier
            // is the one the POINTER points at, and the refusal below is the
            // second condition of the carried-cell rule asked at the callee -
            // the same question `op` asks of the box in the owning frame, in
            // the function that reads it through the pointer.
            const std::string kind =
                !isCapture ? "parameter " : isCellArg(fn.getOperation(), i) ? "shared capture "
                                                                            : "capture ";
            const std::string which =
                kind + std::to_string(isCapture ? i - 3 : i - 3 - captures);
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
    // THE RECEIVER LIFT. The alias groups, shared with admission, and per
    // lifted method the lvalue local its `%arg0` is copied into - the one
    // `ctn_x * self;` `self = v0;` pair that `emitc.member_of_ptr` needs.
    const receiverGroups * groups = nullptr;
    llvm::DenseSet<mlir::Value> receiverArgs;
    llvm::DenseMap<mlir::Value, mlir::Value> receiverLocal;
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
    // THE RECEIVER CARRIER, AND WHY IT IS A POINTER RATHER THAN A `&`.
    //
    // The answer this slice wanted is `ctn_x & self`, and EmitC has no spelling
    // for it. `emitc.func` REJECTS an lvalue argument type outright - "cannot
    // have lvalue type as argument" is in the dialect, and it is the check that
    // makes a C++ reference parameter unrepresentable - so a parameter is
    // always a value type. `emitc.member` on a value-typed operand gives a
    // value-typed result, which can be read and not assigned, so `this.x = 5`
    // would have no lowering. `emitc.dereference` does give an lvalue, but the
    // emitter caches it as the string `*v0` and `emitc.member` then prints
    // `*v0.x` - the wrong expression, with no diagnostic anywhere.
    //
    // `emitc.member_of_ptr` on an lvalue HOLDING a pointer is the one member
    // access that survives a parameter, so the receiver is `ctn_x *`, the
    // method opens with one `ctn_x * self;` `self = v0;` pair, and every
    // access is `self->x`. That is a REFERENCE in every sense this tier cares
    // about - non-owning, no allocation, the caller's frame, dead after the
    // call - and `-O2` emits the same instructions. It is a pointer only in
    // the spelling, and the spelling is the backend's, not this slice's.
    mlir::Type receiverType(const siteShape & site) {
        return ec::PointerType::get(ec::OpaqueType::get(context, spelling(site)));
    }
    mlir::Type receiverLocalType(const siteShape & site) {
        return ec::LValueType::get(receiverType(site));
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
    llvm::SmallVector<std::pair<std::string, mlir::Type>> fieldsOf(mlir::Value object) {
        const auto carried = [&](mlir::Value v) {
            return carrierType(context, carrierOf(typeOf(v)));
        };
        llvm::StringMap<mlir::Type> stored;
        llvm::StringMap<mlir::Type> read;
        // OVER THE GROUP. A lifted method's `this.x` is a read of this shape
        // made through a different value in a different function, and a field
        // only that method touches is still a field. Collecting them here is
        // also what records the member name for the access, so replace() can
        // spell `self->x` without asking the census a second time.
        for (mlir::Value alias : aliasesOf(groups, object)) {
            for (mlir::Operation * user : alias.getUsers()) {
                // The method field takes no member: the closure it holds is a
                // free function and the store lowers to nothing. Its KEY
                // constant is still marked, so replace() drops it rather than
                // swapping a NaN in for a string nothing reads.
                if (auto method = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    method && user->hasAttr("ctnative.method")) {
                    keyConstants.insert(method.getKey().getDefiningOp());
                    continue;
                }
                mlir::Value key;
                if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                    if (get.getObject() != alias) { continue; }
                    key = get.getKey();
                    read.try_emplace(admission::keyOf(key), carried(get.getResult()));
                } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                    if (set.getObject() != alias) { continue; }
                    key = set.getKey();
                    stored.try_emplace(admission::keyOf(key), carried(set.getValue()));
                }
                if (key) {
                    accessKey[user] = admission::keyOf(key).str();
                    keyConstants.insert(key.getDefiningOp());
                }
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
                const auto fields = fieldsOf(object.getResult());
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
                // EVERY VALUE IN THE GROUP GETS THE SHAPE, which is what makes
                // a receiver parameter spellable: `%arg0` of the lifted method
                // has the same class as the literal it is called on, because
                // it IS that literal. Two literals of one shape calling one
                // method put the same answer here twice; a disagreement would
                // be two literals whose fields differ, and the group has
                // already joined those into ONE shape - which is correct, and
                // is why "two objects of the same shape share one method" needs
                // no separate check.
                for (mlir::Value alias : aliasesOf(groups, object.getResult())) {
                    shapeOf[alias] = site;
                }
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
            // PHASE 59 SLICE 2 STEP 2: A SHARED BINDING IS NOT ITS CARRIER, it
            // is a PLACE holding one. The box in the owning frame becomes an
            // `emitc.lvalue` - the type an emitc.variable has, which load and
            // assign both take - and the capture parameter that reaches it
            // from a lifted call becomes an `emitc.ptr` to the same carrier,
            // which is the receiver's convention (`ctn_x * self`) one operand
            // along. Both are asked BEFORE the scalar row below, because the
            // lattice type of either is the carrier of what is INSIDE.
            if (admission::isCarriedCell(v.getDefiningOp())) {
                v.setType(ec::LValueType::get(carrierType(context, carrierOf(typeOf(v)))));
                return;
            }
            if (admission::isCellParameter(v)) {
                v.setType(ec::PointerType::get(carrierType(context, carrierOf(typeOf(v)))));
                return;
            }
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
        // AND THE RECEIVER, which is the same shape one indirection away. It is
        // done here rather than in the loop above because `retypeValue` skips
        // a closed object - "keeps its ctjs type until its shape is known" -
        // and `%arg0` of a lifted method is exactly that.
        if (fn->hasAttr("ctnative.receiver")) {
            mlir::Value self = fn.getBody().front().getArgument(0);
            self.setType(receiverType(shapeAt(self)));
        }
        // AND THE OBJECT PARAMETERS, WHICH ARE THE SAME TYPE IN THE SAME PLACE.
        for (int32_t index : admission::objectArgsOf(fn.getOperation())) {
            mlir::Value parameter = fn.getBody().front().getArgument(static_cast<unsigned>(index));
            parameter.setType(receiverType(shapeAt(parameter)));
        }
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

    // `o.x` OR `self->x`, DECIDED BY WHAT THE OBJECT IS. A literal in this
    // frame is an `emitc.variable` and takes `emitc.member`; a lifted method's
    // receiver arrived as a pointer and takes `emitc.member_of_ptr` through the
    // local `lower()` made for it. Both give an lvalue, so the read and the
    // write above are the same two lines either way.
    // `v` OR `*p`, DECIDED BY WHAT THE BINDING IS - the exact shape of
    // memberAccess one level down, and here for the same reason: a read and a
    // write of a shared binding are the same two lines whichever side of the
    // call they are on. The variable in the owning frame is already an lvalue;
    // a capture parameter is a pointer, and `emitc.dereference` is the lvalue
    // one indirection through it.
    static mlir::Value cellPlace(mlir::OpBuilder & b, mlir::Location where, mlir::Value cell) {
        if (auto pointer = llvm::dyn_cast<ec::PointerType>(cell.getType())) {
            return ec::DereferenceOp::create(b, where, ec::LValueType::get(pointer.getPointee()),
                                             cell);
        }
        return cell;
    }

    mlir::Value memberAccess(mlir::OpBuilder & b, mlir::Location where, mlir::Value object,
                             llvm::StringRef member, mlir::Type type) {
        if (!receiverArgs.contains(object)) {
            return ec::MemberOp::create(b, where, ec::LValueType::get(type), member, object);
        }
        // AT THE FIRST FIELD, NOT AT THE FUNCTION'S FIRST LINE - AND THIS IS A
        // TIDINESS CHOICE, WHICH IS NOT WHAT THE COMMENT HERE FIRST CLAIMED.
        // `twice() { return this.area() * 2; }` reads no field of its own, so
        // building the local eagerly leaves `ctn_h_w * v2; v2 = v1;` with
        // nothing reading v2, and the prediction was that -Werror would reject
        // it. Measured: it does not. The pipeline's `canonicalize` deletes the
        // dead `emitc.variable` before any C++ is printed, `twice` compiles
        // clean either way, and the whole suite is green with the eager form.
        // So this buys nothing the pipeline was not already buying - what it
        // buys is that the RAW `--ctnative-lower-to-emitc` output, which the
        // lit and the printing gate read, has no variable no one reads in it.
        // Block start still dominates every use, so building it here is free.
        mlir::Value & local = receiverLocal[object];
        if (!local) {
            mlir::OpBuilder at =
                mlir::OpBuilder::atBlockBegin(llvm::cast<mlir::BlockArgument>(object).getOwner());
            local = ec::VariableOp::create(at, where, ec::LValueType::get(object.getType()),
                                           ec::OpaqueAttr::get(context, ""));
            ec::AssignOp::create(at, where, local, object);
        }
        return ec::MemberOfPtrOp::create(b, where, ec::LValueType::get(type), member, local);
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
        // PHASE 59 SLICE 2 STEP 2: THE SHARED BINDING, AS A VARIABLE AND TWO
        // ACCESSES OF IT.
        //
        // `ctjs.create_cell` is a `double v;` in this frame, assigned the
        // box's INITIAL - which for a hoisted `var` is the `undefined` the
        // compiler boxed, carried as NaN. That assignment is not decoration:
        // a read on a path that reaches no `ctjs.cell_set` yields exactly what
        // the interpreter yields, which is the whole reason this rule needs no
        // dominance proof where step 1 needed two.
        if (auto cell = llvm::dyn_cast<CreateCellOp>(o); cell && admission::isCarriedCell(o)) {
            mlir::Value local = ec::VariableOp::create(b, where, cell.getResult().getType(),
                                                       ec::OpaqueAttr::get(context, ""));
            ec::AssignOp::create(b, where, local, cell.getInitial());
            swap(local);
            return;
        }
        if (auto get = llvm::dyn_cast<CellGetOp>(o)) {
            mlir::Value place = cellPlace(b, where, get.getCell());
            swap(ec::LoadOp::create(b, where,
                                    llvm::cast<ec::LValueType>(place.getType()).getValueType(),
                                    place));
            return;
        }
        if (auto set = llvm::dyn_cast<CellSetOp>(o)) {
            ec::AssignOp::create(b, where, cellPlace(b, where, set.getCell()), set.getValue());
            eraseIfUnused(o);
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
        // THE METHOD FIELD IS NOT A FIELD, so its store goes and the closure it
        // held loses its last use and goes in the sweep. `this` is a parameter
        // and the method is a free function; there is nothing to write.
        if (o->hasAttr("ctnative.method")) {
            eraseIfUnused(o);
            return;
        }
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
            const mlir::Type type = get.getResult().getType();
            swap(ec::LoadOp::create(b, where, type,
                                    memberAccess(b, where, get.getObject(), memberName(o), type)));
            return;
        }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            ec::AssignOp::create(
                b, where,
                memberAccess(b, where, set.getObject(), memberName(o), set.getValue().getType()),
                set.getValue());
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
            llvm::SmallVector<mlir::Value> args;
            // THE RECEIVER GOES FIRST WHEN THE LIFT MARKED THE CALL, and this
            // one line is where the tier stops dropping it. The callee takes a
            // pointer, so a literal in this frame - an `emitc.variable`, an
            // lvalue - needs its address, and a receiver being PASSED ON by
            // `this.other()` is already one.
            const auto asPointer = [&](mlir::Value v) {
                return llvm::isa<ec::PointerType>(v.getType())
                           ? v
                           : ec::AddressOfOp::create(
                                 b, where,
                                 ec::PointerType::get(
                                     llvm::cast<ec::LValueType>(v.getType()).getValueType()),
                                 v)
                                 .getResult();
            };
            if (o->hasAttr("ctnative.receiver")) { args.push_back(asPointer(call.getReceiver())); }
            // AND THE SAME ADDRESS-OF FOR AN OBJECT ARGUMENT, through the one
            // lambda above - so the receiver and an argument cannot drift into
            // two different ways of taking one address.
            for (unsigned i = 3; i < operands.size(); ++i) {
                // AND A SHARED BINDING GOES THE SAME WAY, THROUGH THE SAME
                // LAMBDA: `&n` for the variable in this frame, and the pointer
                // unchanged when this function received one itself - which is
                // how a binding two levels out reaches the innermost closure.
                const bool byAddress =
                    admission::isObjectArg(o, i) || admission::isCellArg(o, i);
                args.push_back(byAddress ? asPointer(operands[i]) : operands[i]);
            }
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
        // THE RECEIVER IS THE FIRST PARAMETER, and this is the whole of the
        // signature change: `double bump_3(ctn_x * self, double n)`. It comes
        // first because ctjs.call_direct's operand 0 is the receiver, so the
        // caller already passes it there.
        const bool carriesReceiver = fn->hasAttr("ctnative.receiver");
        llvm::SmallVector<mlir::Type> params;
        if (carriesReceiver) { params.push_back(entry.getArgument(0).getType()); }
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

        // THE ONE LOCAL A RECEIVER COSTS, built by memberAccess() at the first
        // field it reads: `emitc.member_of_ptr` wants an lvalue HOLDING the
        // pointer and a parameter is not one, so a method that touches a field
        // opens with `ctn_x * self; self = v0;` and every `this.x` after it is
        // `self->x`. A method that only FORWARDS the receiver gets neither.
        if (carriesReceiver) { receiverArgs.insert(body.getArgument(0)); }
        // AN OBJECT PARAMETER IS A RECEIVER IN EVERY WAY THAT MATTERS HERE: it
        // arrived as a pointer, so `memberAccess` has to give it the same
        // `ctn_x * p; p = v3;` local and the same `p->x`.
        for (int32_t index : admission::objectArgsOf(fn.getOperation())) {
            receiverArgs.insert(body.getArgument(static_cast<unsigned>(index)));
        }

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
            //
            // AND SLICE 1b'S CAPTURE PLACEHOLDERS GO THE SAME WAY, which is
            // worth saying because they are new and they are DEAD BY
            // CONSTRUCTION. A slot the enclosing closure fills carries an
            // `undefined` operand nothing reads - the index is on
            // `enclosing_indices` - so once the closure is erased the constant
            // has no users at all. It costs the emitted C++ nothing: over
            // native-nested-closure-fixture.js, whose lifted `mid` functions
            // hold exactly these placeholders, the module this pass writes
            // holds ZERO `emitc.constant` of 0x7FF8000000000000, and the
            // emitted C++ is byte for byte what slice 1b emitted when the same
            // slot held a live ctjs.load_upvalue instead.
            //
            // MEASURED BETWEEN 0bf7501 AND 384dbc6, and the citation matters
            // because the figure does not reproduce on THIS tree: both of
            // those commits emit 9,665 bytes, sha256 36b671c54ab13025b5a4d971
            // 181d221b24a4fc217fb3e24c70f422404a3c292d, while the tree
            // carrying this comment emits 9,668 - three bytes more, because
            // the same commit's doc edit to native-nested-closure-fixture.js
            // shifted the JS line numbers that go into the provenance
            // comments. A bare number here would read as false to the next
            // person who checked it.
            //
            // Erasing the placeholder in lift() would therefore buy nothing,
            // which is why it is not erased there.
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
        //
        // AND A LIFTED METHOD KEEPS %arg0, which is why this counts from a
        // first index rather than always from zero: the receiver is a real
        // parameter now and new.target and the callee are the two that go.
        const unsigned first = carriesReceiver ? 1u : 0u;
        const unsigned drop = isEntry ? body.getNumArguments() : 3u - first;
        for (unsigned i = 0; i < drop; ++i) {
            if (!body.getArgument(first).use_empty()) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: implicit argument of `") + fn.getSymName() +
                    "` still has uses after lowering - admission should have refused it");
            }
            body.eraseArgument(first);
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
        closureLifter lifter{module, census};
        const liftReport lifted = lifter.run();
        if (census) {
            // ONE LINE, DETERMINISTIC. StringMap iterates in hash order, so it
            // is sorted by count and then by name - a census whose text moves
            // between runs cannot be pinned by a lit and cannot be diffed
            // between two corpora runs either.
            const auto ordered = [](const llvm::StringMap<unsigned> & from) {
                llvm::SmallVector<std::pair<llvm::StringRef, unsigned>> out;
                for (const auto & entry : from) { out.emplace_back(entry.first(), entry.second); }
                llvm::sort(out, [](const auto & a, const auto & b) {
                    return a.second != b.second ? a.second > b.second : a.first < b.first;
                });
                return out;
            };
            std::string text;
            llvm::raw_string_ostream into(text);
            into << "ctnative census: " << lifter.censusOpenObjects
                 << " method-bearing literal(s) refused as open, holding "
                 << lifter.censusMethodFields << " method field(s); blocking uses:";
            for (const auto & [label, count] : ordered(lifter.censusUses)) {
                into << " " << label << "=" << count;
            }
            into << "; sole blocker:";
            for (const auto & [label, count] : ordered(lifter.censusSole)) {
                into << " " << label << "=" << count;
            }
            into << "; sole blocker by method field:";
            for (const auto & [label, count] : ordered(lifter.censusSoleFields)) {
                into << " " << label << "=" << count;
            }
            into << "; root of the nesting:";
            for (const auto & [label, count] : ordered(lifter.censusRoot)) {
                into << " " << label << "=" << count;
                const auto at = lifter.censusExample.find(label);
                if (at != lifter.censusExample.end()) { into << "@" << at->second; }
            }
            into << "; nesting depth:";
            for (const auto & [label, count] : ordered(lifter.censusDepth)) {
                into << " " << label << "=" << count;
            }
            module.emitRemark() << text;
        }
        if (report) {
            module.emitRemark() << "ctnative: lifted " << lifted.closures << " closure(s) over "
                                << lifted.captures << " capture(s) into " << lifted.functions
                                << " function(s), rewrote " << lifted.calls << " call(s), unboxed "
                                << lifted.cells << " cell(s), " << lifted.methods
                                << " method(s) of which " << lifted.receivers
                                << " take a receiver, " << lifted.objects
                                << " object parameter(s), " << lifted.constructors
                                << " constructor site(s), " << lifted.locals
                                << " shared cell(s) made a frame-local variable carried through "
                                << lifted.carried << " pointer parameter(s)";
        }

        // THE ALIAS GROUPS, once the lift has written its attributes and before
        // anything reads a field. TypeInference::groupReceivers is the one walk
        // that says which values name one object; admission and the shape
        // census both read it, so a field a method touches is the same field
        // the literal has in both.
        const receiverGroups groups = TypeInference::groupReceivers(module);

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
            admission check{solver, {}, &groups};
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
        lower.groups = &groups;
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
