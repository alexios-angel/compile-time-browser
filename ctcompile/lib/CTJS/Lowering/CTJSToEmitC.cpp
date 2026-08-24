// A COMPILED FUNCTION, AS C++ THE HOST COMPILER WILL ACCEPT.
//
// This is the narrow end of the backend: it turns a ctjs.func into an
// emitc.func shaped exactly like ct_aot_entry_fn, so that mlir-translate
// --mlir-to-cpp produces a translation unit that compiles against the real
// aot.hpp. test/Lowering/EmitC/entry-shape.mlir is the target it aims at, and
// that file compiles what it describes rather than only matching text.
//
// IT REFUSES MOST FUNCTIONS ON PURPOSE, and says so on each one it leaves. The
// importer set the precedent - "the count of what it leaves behind is the work
// list" - and a backend that half-lowered a function it did not understand
// would produce a translation unit that compiles and computes the wrong thing,
// which is this project's recurring failure mode. What it refuses and why is
// recorded as an attribute on the operation, so the work list is readable with
// ctjs-opt rather than only from a log.
//
// WHAT IT ACCEPTS TODAY: a body of frame_enter, frame_exit, return and
// constants - which is `function f(a) { return a; }` and not much more. That is
// the whole point of doing it now: the pipeline from JavaScript to a compiled
// .cpp is either connected or it is not, and every operation added afterwards
// is an increment on something that demonstrably works end to end.
//
// THREE CONSTRAINTS FROM THE RUNTIME SHAPE THIS FILE, none of them obvious:
//
//   argv DIES AT ct_aot_enter. It is an interior pointer into
//   context::registers_, and enter resizes that vector - so the parameters are
//   copied into locals BEFORE the call, never read after it. There is no way
//   to re-derive it: ct_aot_slots hands back the compiled frame's own span,
//   not the caller's argument window.
//
//   new.target AND callee CANNOT BE DELIVERED AT ALL. The importer gives every
//   function three implicit arguments before its declared ones - receiver,
//   new.target, callee - because the bytecode reads them with their own
//   opcodes. `receiver` arrives in the entry signature. The other two come only
//   from ct_aot_new_target and ct_aot_callee, and NEITHER HAS A BODY: they are
//   declared in aot.hpp and defined nowhere, so emitting a call to either is a
//   link error. A function that uses them is refused rather than given
//   undefined, because undefined is an answer and a wrong one.
//
//   THE ENTRY BLOCK'S ARGUMENTS ARE SAFE; NO OTHER BLOCK'S ARE. The C++ emitter
//   loses a copy on a block-argument edge - see
//   test/Lowering/EmitC/block-argument-hazard.mlir, which compiles and runs the
//   miscompile. The entry block's arguments become real C++ parameters and are
//   unaffected. Every other block's would have to be lowered to variables
//   first, and until that exists this pass refuses any function with more than
//   one block.
#include "ctcompile/CTJS/Transforms/Passes.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Transforms/RegionUtils.h"

#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/bytecode.hpp>

#include "ctcompile/CTJS/Lowering/OpcodeMapping.hpp"

namespace ctcompile::ctjs {

#define GEN_PASS_DEF_CTJSLOWERTOEMITC
#include "ctcompile/CTJS/Transforms/Passes.h.inc"

namespace {

namespace ec = mlir::emitc;

// THE IMPLICIT ARGUMENTS THE IMPORTER PREPENDS, named rather than counted.
// BytecodeImport.cpp declares the same three; they are repeated here because
// this file has to know which of them the entry ABI can actually supply.
constexpr unsigned arg_receiver = 0;
constexpr unsigned arg_new_target = 1;
constexpr unsigned arg_callee = 2;
constexpr unsigned implicit_arguments = 3;

// The frame block's element type, aligned for whatever the runtime constructs
// in it rather than for a byte.
constexpr const char * kFrameStorageElement = "alignas(::std::max_align_t) unsigned char";

// THE C++ SPELLINGS, IN ONE PLACE.
//
// A JavaScript value is `uint64_t` HERE and nothing else. It is not
// ctbrowser::script::value - that class keeps its bits private and offers no
// conversion, so `value *` where the ABI wants `uint64_t *out` is a compile
// error - and it is not i64, which this emitter prints as `int64_t` and which
// fails the same way against `uint64_t *`.
ec::OpaqueType opaque(mlir::MLIRContext * context, llvm::StringRef spelling) {
    return ec::OpaqueType::get(context, spelling);
}

ec::PointerType pointer_to(mlir::MLIRContext * context, llvm::StringRef spelling) {
    return ec::PointerType::get(opaque(context, spelling));
}

// AN OPCODE'S C++ ENUMERATOR NAME, from the same file the enum is generated
// from.
//
// `uint32_t op_kind` in the ABI is a ctbrowser::script::op and aot_bridge.cpp
// casts it back, so the emitted call must name the OPERATOR. Spelling it as a
// number would survive the renumbering Phases 13 and 14 do deliberately and
// silently mean something else; spelling it as an enumerator makes that a build
// error in the generated translation unit.
#define CT_OPCODE(name_, ...) #name_,
constexpr std::string_view opcode_names[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};
#undef CT_OPCODE

static_assert(std::size(opcode_names) == ctbrowser::script::opcode_count,
              "the backend's opcode-name table and `enum class op` disagree");

std::string opcode_spelling(ctbrowser::script::op which) {
    const auto index = static_cast<std::size_t>(which);
    return "static_cast<uint32_t>(ctbrowser::script::op::" + std::string(opcode_names[index]) + ")";
}

// EVERY HELPER IS CALLED BY ITS QUALIFIED C++ NAME.
//
// aot.hpp puts the extern "C" prototypes INSIDE namespace ctbrowser::aot, so
// they have C linkage and an unmangled linker symbol while their C++ name is
// qualified. An unqualified call compiles only by argument-dependent lookup off
// a ct_aot_frame * argument, which the frameless rows do not have. The helper
// table's `symbol` is the LINKER name and is not this string.
std::string callee(llvm::StringRef helper) {
    return ("ctbrowser::aot::" + helper).str();
}

// A C++ EXPRESSION WITH NO SSA SOURCE - an enumerator, a literal, nullptr.
mlir::Value literal(mlir::OpBuilder & build, mlir::Location where, mlir::Type type,
                    llvm::StringRef text) {
    return ec::LiteralOp::create(build, where, type, text);
}

// THE SYMBOL, AS C++ ACTUALLY SPELLS IDENTIFIERS.
//
// The importer suffixes every function with `$index` because a real program has
// many functions sharing a name - p5.js has dozens called `constructor` - and
// without it the module fails to verify on a duplicate symbol. `$` is not in
// C++'s basic character set: GCC and Clang accept it as an extension, so the
// emitted unit compiles here today and would stop compiling the first time
// anything is built with a compiler that does not.
//
// THE SUFFIX STILL SEPARATES, so replacing it loses nothing. Every name the
// importer produces is `<sanitised>$<index>` where the sanitised part already
// has no `$` and the index is decimal, so mapping `$` to `_` keeps distinct
// names distinct: `a$1` becomes `a_1` and `a_1$2` becomes `a_1_2`.
std::string c_identifier(llvm::StringRef symbol) {
    std::string spelled = symbol.str();
    for (char & c : spelled) {
        if (c == '$') { c = '_'; }
    }
    return spelled;
}

// EVERYTHING ONE COMPILED ENTRY NEEDS TO KNOW ABOUT ITSELF.
//
// It exists because an operation with an exception edge needs more than its own
// operands: the frame to leave, the entry's `receiver` and `constructing` for
// the return protocol, the out-pointer to write, and a shared block to branch
// to when a helper fails. Threading seven arguments through every conversion
// was how this started and it did not survive the first one that needed an
// eighth.
struct compiled_entry {
    ec::FuncOp entry;
    mlir::Value frame;
    mlir::Value receiver;
    mlir::Value constructing;
    mlir::Value out;
    mlir::Type value;
    mlir::Type status;
    // Built on first use, because a function whose helpers cannot fail should
    // not carry an unreachable epilogue.
    mlir::Block * propagate = nullptr;

    // WHERE EACH JAVASCRIPT VALUE IS ROOTED.
    //
    // THE COLLECTOR IS PRECISE and walks exactly the roots in GCRoots.def. A
    // value living only in a C++ local of the emitted function is reachable
    // from NONE of them, so a helper that collects can free it while the
    // generated code still holds its bits - and 33 of the 69 ABI rows are
    // is_safepoint, including every arithmetic one.
    //
    // This was a real defect, not a hypothetical: `function f(a,b,c){return
    // a+b+c;}` compiled to code that kept (a+b) in a plain uint64_t across the
    // second ct_aot_binary_op, and under set_gc_stress it returned "qqqqqZ"
    // where the interpreter returned the correct 65-character string. ASan
    // called it a heap-use-after-free, freed and read inside the same call.
    //
    // The runtime's own reference body says what to do instead - "parked in a
    // slot, which is the whole discipline this phase exists to make possible" -
    // so every value this backend produces goes into a frame slot as soon as it
    // exists. The mapping is one slot per produced value, never reused: keeping
    // a dead value alive is a leak until the frame is left, and losing a live
    // one is a use-after-free.
    llvm::DenseMap<mlir::Value, unsigned> slots;

    // AND WHERE EACH CALL'S ARGUMENT WINDOW STARTS.
    //
    // ct_aot_call takes `const uint64_t *argv` - a CONTIGUOUS run - and the
    // arguments are individually rooted in slots of their own, which are not
    // adjacent. So each call site gets a run reserved for it, written just
    // before the call. In the frame rather than in a C++ array, for the reason
    // everything else is: ct_aot_call is a safepoint that runs arbitrary user
    // JavaScript before it reads the arguments.
    llvm::DenseMap<mlir::Operation *, unsigned> argument_windows;

    // THE KEY ct_aot_new_string MEMOISES BY, AND IT IS NOT THE ENTRY'S `site`.
    //
    // The entry's site IS the function_proto, and the interpreter keys the same
    // cache by that proto with the string's CONSTANT-POOL index as the slot.
    // This backend numbers its slots in walk order, which is a different
    // numbering - so sharing the key means a compiled body can read a slot the
    // interpreter filled with a DIFFERENT literal and return the wrong string.
    //
    // It coincided on the first fixture and hid a mutation that halved every
    // length: the interpreted baseline ran first, filled the cache, and the
    // compiled body never allocated at all.
    //
    // So each compiled function memoises under an address of its OWN. The two
    // tiers then allocate one object each for the same literal instead of
    // sharing one, which is invisible - string identity is unobservable, strict
    // equality compares text - and the ceiling is still bounded by one
    // allocation per site and slot, which is what the row requires.
    mlir::Value memo_site;

    // WHICH MEMO SLOT EACH STRING CONSTANT USES. The slots must be unique
    // WITHIN the function and stable across calls; one per ctjs.constant
    // carrying a string is both. They are not frame slots - the cache is a map.
    llvm::DenseMap<mlir::Operation *, unsigned> memo_slots;
};

// A C++ STRING LITERAL FOR ARBITRARY BYTES.
//
// OCTAL ESCAPES, NOT HEX, and the difference is a real bug rather than a taste.
// A hex escape in C++ consumes as many hex digits as follow it, so a name
// containing byte 0x01 followed by the character 'F' becomes "\x01F" - one
// character, 0x1F - and the emitted program looks up a global nobody named. An
// octal escape is exactly three digits and cannot run on.
//
// A GLOBAL'S NAME IS NOT ALWAYS AN IDENTIFIER, which is why this escapes at all
// rather than trusting the input: `globalThis["\u0000"] = 1` is legal
// JavaScript, and the importer carries whatever the source said.
// WHETHER A RAW STRING LITERAL WOULD BE BETTER THAN ESCAPING.
//
// R"(...)" is not a general answer and cannot be the only path: a raw string's
// content is the LITERAL BYTES of the generated source file, so a name
// containing a zero byte or a control character would have to have that byte
// written into the .cpp - and a NUL truncates the file for most tooling. It
// also cannot contain its own terminator.
//
// AND FOR AN ORDINARY IDENTIFIER IT IS WORSE, not better: `R"(Math)"` says
// nothing that `"Math"` does not, with five more characters. The escaped form
// IS the plain form whenever nothing needs escaping.
//
// So the raw form is used exactly where escaping is the noisy one - a name
// containing a quote or a backslash, where `"a\"b\\c"` becomes `R"(a"b\c)"`.
// That is a narrow win and it is taken because the emitted code is read by
// people when something has gone wrong.
constexpr bool raw_string_is_clearer(std::string_view bytes) {
    bool noisy = false;
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        // A raw string can only carry what the source file can carry plainly.
        if (byte < 0x20 || byte >= 0x7f) { return false; }
        if (byte == '"' || byte == '\\') { noisy = true; }
    }
    // The terminator cannot appear inside; a custom delimiter would only move
    // the problem to choosing one nothing collides with.
    return noisy && bytes.find(")\"") == std::string_view::npos;
}

constexpr std::string c_string_literal(std::string_view bytes) {
    if (raw_string_is_clearer(bytes)) { return "R\"(" + std::string(bytes) + ")\""; }
    std::string spelled = "\"";
    for (const char raw : bytes) {
        const auto byte = static_cast<unsigned char>(raw);
        if (byte == '"' || byte == '\\') {
            spelled += '\\';
            spelled += raw;
        } else if (byte >= 0x20 && byte < 0x7f) {
            spelled += raw;
        } else {
            static constexpr char digits[] = "01234567";
            spelled += '\\';
            spelled += digits[(byte >> 6) & 7u];
            spelled += digits[(byte >> 3) & 7u];
            spelled += digits[byte & 7u];
        }
    }
    spelled += '"';
    return spelled;
}

// THE ESCAPE, CHECKED AT COMPILE TIME rather than by reading an emitted file.
// "Prefer a build error to a test", and these are decidable here.
static_assert(c_string_literal("Math") == "\"Math\"");
// THE ONE THAT WOULD BREAK UNDER A HEX ESCAPE: `od`, byte 0x01, `Fd`.
//
// THE INPUT IS SPELLED OCTALLY HERE FOR THE SAME REASON THE OUTPUT IS, and the
// first attempt at this assertion proved it: written "od\x01Fd" the C++
// compiler refused the source with "hex escape sequence out of range", because
// it read the escape as \x01F followed by `d`. The hazard is real enough to
// have bitten the test written to demonstrate it.
static_assert(c_string_literal("od\001Fd") == "\"od\\001Fd\"");
static_assert(c_string_literal("od\001Fd").size() == 5 + 2 + 3,
              "five source bytes, two quotes, and three extra characters for the one escape");
// QUOTES AND BACKSLASHES TAKE THE RAW FORM, because escaping them is exactly
// the case where the escaped spelling is harder to read than the name.
static_assert(c_string_literal("a\"b") == "R\"(a\"b)\"");
static_assert(c_string_literal("a\\b") == "R\"(a\\b)\"");
// AND A ZERO BYTE SURVIVES, which is why the length is emitted beside the
// pointer: strlen would stop here. It also forces the escaped path - a raw
// string cannot carry a NUL, because a NUL in the generated source truncates
// the file for most tooling.
static_assert(c_string_literal(std::string_view("a\0b", 3)) == "\"a\\000b\"");
static_assert(!raw_string_is_clearer(std::string_view("a\0b", 3)));

// THE RAW FORM IS TAKEN ONLY WHERE ESCAPING IS THE NOISY ONE.
static_assert(!raw_string_is_clearer("Math"), "nothing to escape - the plain form is clearer");
static_assert(raw_string_is_clearer("a\"b"));
static_assert(c_string_literal("a\"b\\c") == "R\"(a\"b\\c)\"");
// AND NEVER WHERE THE CONTENT WOULD CLOSE IT.
static_assert(!raw_string_is_clearer("a)\"b"), "the content contains the terminator");
static_assert(c_string_literal("a)\"b") == "\"a)\\\"b\"");

// THE HELPERS THE RUNTIME DECLARES BUT DOES NOT DEFINE.
//
// aot_helpers.def declares 69 rows and aot_bridge.cpp defines 32 of them. The
// other 37 have prototypes in aot.hpp and no body anywhere, so a call to one
// COMPILES PERFECTLY and fails at link.
//
// THAT IS EXACTLY WHAT HAPPENED. This backend emitted ct_aot_global_get and
// ct_aot_negate for two commits, and every test passed - because every EmitC
// test compiled the output with -fsyntax-only and none of them linked it.
// Linking it by hand gives "undefined reference to `ct_aot_global_get'".
//
// SO THE ROWS ARE NOT ENOUGH TO DECIDE WHAT TO EMIT, and this is the one place
// where the .def cannot be the single source of truth: it records what the ABI
// IS, not what has been built yet. This list is the second source, and it is
// held honest by ctcompile_linkable, which links a translation unit exercising
// every operation the backend accepts - so a name here that is wrong in either
// direction fails the build rather than a program.
bool runtime_defines(llvm::StringRef helper) {
    // EMPTY, AND THAT IS THE POINT OF KEEPING IT. Every row the backend can
    // name now has a body: ct_aot_global_get, ct_aot_global_set,
    // ct_aot_negate and ct_aot_bit_not were all here and are all implemented.
    //
    // The list stays because 30 of the 69 rows still have none, and the next
    // operation lowered will need it again - and because ctcompile_linkable is
    // what keeps it honest in both directions, by linking a translation unit
    // that exercises everything the backend accepts.
    static constexpr llvm::StringLiteral undefined_yet[] = {llvm::StringLiteral("")};
    for (const llvm::StringLiteral & absent : undefined_yet) {
        if (helper == absent) { return false; }
    }
    return true;
}

// WHY A FUNCTION WAS LEFT ALONE, in a form ctjs-opt prints.
void refuse(FuncOp function, llvm::StringRef because) {
    function->setAttr("ctjs.not_lowered", mlir::StringAttr::get(function.getContext(), because));
}

// Whether every operation in the body is one this pass knows how to emit.
//
// AN ALLOW-LIST, NOT A DENY-LIST. A deny-list lowers an operation nobody
// considered by default, and the cost of being wrong here is a translation unit
// that compiles and computes something else.
bool body_is_supported(FuncOp function, std::string & why) {
    bool supported = true;
    function.getBody().walk([&](mlir::Operation * op) {
        // ctjs.frame_exit MUST BE THE LAST THING BEFORE THE RETURN.
        //
        // The shared failure path leaves the frame too, so a fallible operation
        // AFTER an in-place exit would emit a second ct_aot_leave on the way
        // out. The runtime makes that harmless - leave truncates to this
        // frame's own recorded index rather than popping, and its row says it
        // "is a harmless no-op after a failure" - so this is an unchecked
        // invariant rather than a live defect. It is checked anyway, because
        // the importer emits frame_exit immediately before every return and a
        // module where that stopped being true would be one nobody had looked
        // at.
        if (mlir::isa<FrameExitOp>(op)) {
            if (op->getNextNode() == nullptr || !mlir::isa<ReturnOp>(op->getNextNode())) {
                supported = false;
                why = "ctjs.frame_exit is not immediately followed by ctjs.return - anything "
                      "fallible after it would leave the frame twice";
            }
            return;
        }
        if (mlir::isa<FrameEnterOp, ReturnOp, TruthyOp>(op)) { return; }
        // THE ARITHMETIC, AND WITH IT THE FIRST EXCEPTION EDGE. Both families
        // answer with a ct_aot_status, so each becomes a call, a test against
        // ct_aot_status::ok by name, and a branch to the shared failure path.
        // A kind the family does not serve is refused rather than compiled into
        // op::halt, which ct_aot_binary_op's switch would answer with undefined.
        // EVERY UNARY KIND LOWERS NOW. `typeof` was refused because its result
        // is a string, and that reason went stale with the same sentence
        // ctjs.constant's did: ct_aot_new_string has a body, and every value
        // the backend produces is parked in a frame slot.
        if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
            // THE LIST STILL EXISTS, and it is empty. Every helper the backend
            // can name has a body; the check stays because 27 rows still do
            // not, and ctcompile_linkable is what keeps it honest.
            if ((unary.getKind() == UnaryKind::Neg && !runtime_defines("ct_aot_negate")) ||
                (unary.getKind() == UnaryKind::BitNot && !runtime_defines("ct_aot_bit_not"))) {
                supported = false;
                why = "the helper for this unary operator is declared in aot.hpp and defined "
                      "nowhere - emitting a call to it compiles and fails at link";
                return;
            }
            return;
        }
        if (mlir::isa<CompareOp, GetPropertyOp, SetPropertyOp, CallOp, CreateClosureOp,
                      CreateCellOp, CellGetOp, CellSetOp, CreateObjectOp, CreateArrayOp, AppendOp>(
                op)) {
            return;
        }

        // THE UPVALUE OPERATIONS, WHICH ARE TWO CALLS EACH - which is why they
        // are plain CTJS_Ops rather than CTJS_RuntimeOps. The ABI splits the
        // fused opcode deliberately: ct_aot_upvalue_cell answers undefined for
        // a missing closure or an out-of-range index, ct_aot_cell_get no-ops on
        // a non-cell, and composed they are exactly the guard VM_CASE
        // (get_upvalue) writes inline.
        //
        // THE OPERAND MUST BE THIS FRAME'S OWN CLOSURE. ct_aot_upvalue_cell
        // reads the frame, so an operation naming a DIFFERENT closure would be
        // lowered into a read of the wrong one - and the importer only ever
        // names the callee argument, so refusing anything else costs nothing
        // and closes the hole for hand-written IR.
        if (mlir::isa<LoadUpvalueOp, StoreUpvalueOp>(op)) {
            const mlir::Value named = mlir::isa<LoadUpvalueOp>(op)
                                          ? mlir::cast<LoadUpvalueOp>(op).getClosure()
                                          : mlir::cast<StoreUpvalueOp>(op).getClosure();
            if (named != function.getBody().front().getArgument(arg_callee)) {
                supported = false;
                why = "an upvalue operation names a closure other than this frame's own - "
                      "ct_aot_upvalue_cell reads the frame, so it would read the wrong one";
            }
            return;
        }
        if (mlir::isa<LoadGlobalOp>(op) && !runtime_defines("ct_aot_global_get")) {
            supported = false;
            why = "ct_aot_global_get is declared in aot.hpp and defined nowhere - emitting a call "
                  "to it compiles and fails at link";
            return;
        }
        if (mlir::isa<StoreGlobalOp>(op) && !runtime_defines("ct_aot_global_set")) {
            supported = false;
            why = "ct_aot_global_set is declared in aot.hpp and defined nowhere - emitting a call "
                  "to it compiles and fails at link";
            return;
        }
        if (mlir::isa<LoadGlobalOp, StoreGlobalOp>(op)) { return; }
        if (auto binary = mlir::dyn_cast<BinaryOp>(op)) {
            if (!is_valid_binary(binary.getKind())) {
                supported = false;
                why = "ctjs.binary was given a kind only the static family serves";
            }
            return;
        }
        if (auto binary = mlir::dyn_cast<BinaryStaticOp>(op)) {
            if (!is_valid_binary_static(binary.getKind())) {
                supported = false;
                why = "ctjs.binary_static was given a kind only the re-entering family serves";
            }
            return;
        }
        // THE BRANCHES, which is what makes a function with an `if` compilable.
        // Their block arguments are handled by a pass of their own afterwards -
        // see EmitCBlockArguments.cpp - because emitting them as they stand
        // would hit the copy the C++ emitter loses.
        if (mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) { return; }
        if (auto constant = mlir::dyn_cast<ConstantOp>(op)) {
            // THE FOUR THAT NEED NO ALLOCATION. A number is included because
            // its attribute carries the double's BIT PATTERN rather than a
            // float, so it has an exact C++ spelling - see constant_value. A
            // decimal literal would not: -0.0 and 0.0 are different JavaScript
            // values and print identically. A string is still refused; it
            // reaches ct_aot_new_string, which is a safepoint, and nothing
            // roots the result yet.
            if (mlir::isa<UndefinedAttr, NullAttr, BooleanAttr, NumberAttr, StringAttr>(
                    constant.getValue())) {
                return;
            }
            supported = false;
            why = "no lowering yet for this constant - a BigInt literal reaches "
                  "ct_aot_new_bigint_literal, which has no implementation";
            return;
        }
        supported = false;
        why = ("no lowering yet for " + op->getName().getStringRef()).str();
    });
    return supported;
}

struct CTJSLowerToEmitCPass : impl::CTJSLowerToEmitCBase<CTJSLowerToEmitCPass> {
    using CTJSLowerToEmitCBase::CTJSLowerToEmitCBase;

    // Which boxing shims this module's prelude has to define. See box().
    bool boxes_bool = false;
    bool boxes_number = false;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        mlir::MLIRContext * context = &getContext();
        mlir::OpBuilder build(context);

        // COLLECTED FIRST, then rewritten. Mutating the module during its own
        // walk invalidates the iterator that is doing the walking.
        llvm::SmallVector<FuncOp> functions;
        module.walk([&](FuncOp function) { functions.push_back(function); });

        // THE HEADER THE EMITTED UNIT NEEDS, and the only declaration it
        // carries. emitc.call_opaque resolves nothing and emits no prototype,
        // so the helpers come from aot.hpp itself - which is what keeps the
        // generated code agreeing with the runtime instead of with a signature
        // this pass invented.
        bool lowered_anything = false;
        for (FuncOp function : functions) {
            if (lower(function, build, context)) { lowered_anything = true; }
        }
        if (lowered_anything) {
            build.setInsertionPointToStart(module.getBody());
            // aot.hpp for the helpers, value.hpp for the one thing a constant
            // needs: value::bits(), so no NaN-boxing pattern is written here.
            // <bit> for bit_cast and <cstdint> for UINT64_C, both of which a
            // number constant spells; value.hpp for value::bits().
            ec::IncludeOp::create(build, module.getLoc(), build.getStringAttr("bit"),
                                  /*is_standard_include=*/build.getUnitAttr());
            ec::IncludeOp::create(build, module.getLoc(), build.getStringAttr("cstdint"),
                                  /*is_standard_include=*/build.getUnitAttr());
            ec::IncludeOp::create(build, module.getLoc(),
                                  build.getStringAttr("ctbrowser/script/value.hpp"),
                                  /*is_standard_include=*/build.getUnitAttr());
            // AND bytecode.hpp, because an opcode is spelled as an enumerator
            // of ctbrowser::script::op rather than as the number the ABI
            // actually passes. That is the whole point of spelling it: a
            // renumbering becomes a build error here instead of a call to a
            // different operator.
            ec::IncludeOp::create(build, module.getLoc(),
                                  build.getStringAttr("ctbrowser/script/bytecode.hpp"),
                                  /*is_standard_include=*/build.getUnitAttr());
            ec::IncludeOp::create(build, module.getLoc(),
                                  build.getStringAttr("ctbrowser/aot/aot.hpp"),
                                  /*is_standard_include=*/build.getUnitAttr());

            // A SMALL PRELUDE OF SHIMS, AND WHY THE BACKEND EMITS ITS OWN.
            //
            // Several helpers answer with a MACHINE quantity rather than a
            // JavaScript value: ct_aot_strict_equals returns a uint32_t 0 or 1,
            // ct_aot_compare an int32_t ordering, ct_aot_to_number a double.
            // The result of the CTJS operation is a !ctjs.value, so each has to
            // be boxed - and THE ABI HAS NO ROW THAT BOXES ONE. There is no
            // ct_aot_from_bool and no ct_aot_from_double; every row that
            // returns a value builds it from something else.
            //
            // THERE ARE ONLY TWO, because a shim exists only where an SSA
            // value has to be boxed. `undefined` has no operand, so it is
            // spelled inline as a literal - member call and all - and a third
            // shim for it would be an unused function in every translation
            // unit this backend emits.
            //
            // In C++ that is `value::boolean(b).bits()`, which is a member call
            // on a temporary - and emitc.call_opaque emits `callee(args)` and
            // nothing else, so it cannot spell one. Rather than add rows to a
            // runtime ABI for a backend's convenience, the backend emits three
            // static inline functions into its own translation unit. They are
            // generated code, not engine code: nothing links against them and
            // no other backend has to agree about them.
            std::string prelude;
            if (boxes_bool) {
                prelude += "\ninline uint64_t ctc_box_bool(bool b) { return "
                           "::ctbrowser::script::value::boolean(b).bits(); }";
            }
            if (boxes_number) {
                prelude += "\ninline uint64_t ctc_box_number(double d) { return "
                           "::ctbrowser::script::value::number(d).bits(); }";
            }
            if (!prelude.empty()) {
                ec::VerbatimOp::create(build, module.getLoc(),
                                       build.getStringAttr("namespace {" + prelude + "\n}"));
            }
        }
    }

    // Returns whether the function was replaced.
    bool lower(FuncOp function, mlir::OpBuilder & build, mlir::MLIRContext * context) {
        const mlir::Location where = function.getLoc();

        // UNREACHABLE BLOCKS FIRST, because the importer emits one for every
        // function and it is not a reason to refuse anything.
        //
        // A body that ends in an explicit `return` still gets the implicit
        // `return undefined` epilogue after it, and nothing branches there. A
        // block with no predecessors has no edges, so it cannot carry the
        // block-argument copy the emitter miscompiles - refusing on its account
        // would refuse EVERY function, including the one-line one this pass
        // exists to compile. Removing it is an ordinary simplification, not a
        // decision about semantics.
        mlir::IRRewriter prune(context);
        (void)mlir::eraseUnreachableBlocks(prune, function.getBody());

        // BLOCKS ARE NO LONGER A REASON TO REFUSE. They were, until
        // --emitc-eliminate-block-arguments existed: emitting a block argument
        // as it stands hits the copy the C++ emitter loses. That pass runs
        // after this one and turns each into a variable, so what this pass
        // emits may carry block arguments and what reaches mlir-translate must
        // not. The pipeline order is the contract, and end-to-end.mlir runs it.

        std::string why;
        if (!body_is_supported(function, why)) {
            refuse(function, why);
            return false;
        }
        mlir::Block & body = function.getBody().front();
        if (body.getNumArguments() < implicit_arguments) {
            refuse(function, "fewer arguments than the importer's three implicit ones");
            return false;
        }
        // THE TWO THAT CANNOT BE DELIVERED. Their helpers are declared and
        // never defined; calling either is a link error.
        if (!body.getArgument(arg_new_target).use_empty()) {
            refuse(function, "uses new.target, and ct_aot_new_target has no implementation");
            return false;
        }
        // THE CALLEE IS DELIVERABLE NOW. ct_aot_callee has a body, and it is
        // the only way a compiled function can reach its own upvalues: they
        // live on the closure INSTANCE, while `site` is the function_proto that
        // every closure over the same function shares.
        //
        // new.target IS STILL NOT, and the two are not the same gap.
        // ct_aot_new_target has no body either, and behind that
        // ct_aot_enter takes it from pending_new_target_, which op::construct
        // never sets on the compiled path.

        FrameEnterOp entered;
        function.getBody().walk([&](FrameEnterOp op) { entered = op; });
        if (!entered) {
            refuse(function, "no ctjs.frame_enter - every compiled body must establish a frame");
            return false;
        }

        // ---- the entry signature ------------------------------------------
        const auto value = opaque(context, "uint64_t");
        const auto u32 = opaque(context, "uint32_t");
        const auto ctx_ptr = pointer_to(context, "ctbrowser::aot::ct_aot_ctx");
        const auto site_ptr = pointer_to(context, "const ctbrowser::aot::ct_aot_site");
        const auto argv_ptr = pointer_to(context, "const uint64_t");
        const auto out_ptr = pointer_to(context, "uint64_t");
        const auto frame_ptr = pointer_to(context, "ctbrowser::aot::ct_aot_frame");
        const auto status = mlir::IntegerType::get(context, 32);

        const llvm::SmallVector<mlir::Type> inputs{ctx_ptr, site_ptr, argv_ptr, u32,
                                                   value,   u32,      out_ptr};
        build.setInsertionPoint(function);

        // THE ADDRESS THIS FUNCTION'S STRING LITERALS MEMOISE UNDER, declared
        // BEFORE the function that takes it. One byte per compiled function,
        // never read - only its address matters, and it has to differ from the
        // function_proto the interpreter keys the same cache by. See
        // compiled_entry::memo_site.
        const std::string marker = "ctc_memo_" + c_identifier(function.getName().str());
        ec::VerbatimOp::create(build, where,
                               build.getStringAttr("static const char " + marker + " = 0;"));

        auto entry = ec::FuncOp::create(build, where, c_identifier(function.getName()),
                                        build.getFunctionType(inputs, {status}));
        // extern "C" SO THE SYMBOL MATCHES ct_aot_entry_fn's expectations.
        entry.setSpecifiersAttr(build.getStrArrayAttr({"extern \"C\""}));

        mlir::Block * abi = entry.addEntryBlock();
        build.setInsertionPointToStart(abi);
        const mlir::Value in_ctx = abi->getArgument(0);
        const mlir::Value in_site = abi->getArgument(1);
        const mlir::Value in_argv = abi->getArgument(2);
        const mlir::Value in_receiver = abi->getArgument(4);
        const mlir::Value in_constructing = abi->getArgument(5);
        const mlir::Value in_out = abi->getArgument(6);

        // ---- the parameters, copied BEFORE the frame exists ---------------
        //
        // argv is an interior pointer into context::registers_ and ct_aot_enter
        // resizes that vector, so every read has to happen first. The caller
        // has already filled the callee's window - "argv must already be the
        // callee's window with missing parameters filled in with undefined" -
        // so argv[i] is well-defined for every declared parameter regardless of
        // how many arguments actually arrived.
        const unsigned declared = body.getNumArguments() - implicit_arguments;
        llvm::SmallVector<mlir::Value> parameters;
        parameters.reserve(declared);
        //
        // THE PARAMETER STAYS `const uint64_t *` AND THE READ CASTS IT AWAY,
        // which is the least-bad of three options and worth saying why.
        //
        // The signature is not negotiable: this function has to be assignable
        // to ct_aot_entry_fn, which declares `const uint64_t *argv`, and
        // spelling it without the const would make the assignment ill-typed.
        // But emitc.subscript requires the result's type to equal the
        // pointee's, so reading through it yields a `const uint64_t` - and
        // under --declare-variables-at-top, which is mandatory here, EmitC
        // declares every value at the top and assigns later. `const uint64_t
        // v8; v8 = ...;` does not compile.
        //
        // So the CAST IS ON THE POINTER, once, rather than on each element.
        // It only ever reads through the result - argv is the caller's window
        // and writing to it would corrupt the caller's registers - so casting
        // the const away is a spelling concession, not a licence.
        const auto element = opaque(context, "uint64_t");
        const auto element_slot = ec::LValueType::get(element);
        const mlir::Value readable =
            declared == 0 ? mlir::Value{}
                          : ec::CastOp::create(build, where, ec::PointerType::get(element), in_argv)
                                .getResult();
        for (unsigned i = 0; i < declared; ++i) {
            const mlir::Value at =
                literal(build, where, mlir::IntegerType::get(context, 32), std::to_string(i));
            auto slot =
                ec::SubscriptOp::create(build, where, element_slot, readable, mlir::ValueRange{at});
            parameters.push_back(ec::LoadOp::create(build, where, element, slot.getResult()));
        }

        llvm::DenseMap<mlir::Value, unsigned> scope_slots;
        mlir::Value mapping_callee;

        // ---- the frame ----------------------------------------------------
        //
        // CT_AOT_FRAME_BYTES OF CALLER-ALLOCATED SPACE, sized from the macro
        // the runtime's own header defines rather than from a number written
        // ALIGNED, because `unsigned char[N]` is aligned to 1 and ct_aot_enter
        // constructs an aot_frame_storage in it - a struct with a pointer and
        // three indices. aot_bridge.cpp asserts its size against
        // CT_AOT_FRAME_BYTES but nothing asserted the ALIGNMENT, and an
        // under-aligned placement is undefined behaviour that happens to work
        // on x86-64 and need not elsewhere.
        //
        // here. The array is passed directly: C++ decays it to `unsigned char *`
        // and converts that to the `void *` the row declares, which needs no
        // subscript, no address-of, and no size_t - and !emitc.size_t emits a
        // bare `size_t` that does not compile.
        const auto block_type = ec::ArrayType::get({static_cast<std::int64_t>(CT_AOT_FRAME_BYTES)},
                                                   opaque(context, kFrameStorageElement));
        auto storage =
            ec::VariableOp::create(build, where, block_type, ec::OpaqueAttr::get(context, ""));
        // THE REGISTER WINDOW HAS TO HOLD THE ROOTS TOO.
        //
        // proto.frame_size sizes the interpreter's register file, and this
        // backend needs one slot for every JavaScript value it produces -
        // because a value in a C++ local is invisible to a precise collector.
        // The two are counted together and the larger wins: asking for more
        // than frame_size is safe (ct_aot_enter uses the number verbatim and
        // fills with undefined), asking for less makes ct_aot_slots hand back a
        // null span far from the cause.
        //
        // SLOTS ARE NEVER REUSED. A liveness analysis would pack them; keeping
        // a dead value alive until the frame is left is a bounded leak, and
        // getting liveness wrong is a use-after-free. This is the MVP's trade.
        unsigned parked = 0;
        for (unsigned i = 0; i < declared; ++i) {
            scope_slots[body.getArgument(implicit_arguments + i)] = parked++;
        }
        llvm::DenseMap<mlir::Operation *, unsigned> windows;
        // AND A MEMO SLOT PER STRING LITERAL. Not a frame slot: the cache
        // ct_aot_new_string keys by (site, slot) is a map, so these only have
        // to be unique within the function and stable across calls.
        llvm::DenseMap<mlir::Operation *, unsigned> memo_slots;
        unsigned next_memo = 0;
        function.getBody().walk([&](mlir::Operation * inner) {
            for (const mlir::Value result : inner->getResults()) {
                if (mlir::isa<ValueType>(result.getType())) { scope_slots[result] = parked++; }
            }
            // A CONTIGUOUS RUN PER CALL SITE, reserved here so the register
            // window can be sized to include it before ct_aot_enter is emitted.
            if (auto call = mlir::dyn_cast<CallOp>(inner)) {
                windows[inner] = parked;
                parked += static_cast<unsigned>(call.getArgs().size());
            }
            // A CLOSURE'S UPVALUE ARRAY IS AN ARGV BY ANOTHER NAME: a
            // contiguous run the helper reads, and one that must live in the
            // frame rather than in C++ locals, because ct_aot_make_closure
            // allocates and is therefore a safepoint.
            if (auto made = mlir::dyn_cast<CreateClosureOp>(inner)) {
                windows[inner] = parked;
                parked += static_cast<unsigned>(made.getUpvalues().size());
            }
            if (auto constant = mlir::dyn_cast<ConstantOp>(inner)) {
                if (mlir::isa<StringAttr>(constant.getValue())) { memo_slots[inner] = next_memo++; }
            }
        });
        const unsigned window = std::max(static_cast<unsigned>(entered.getRegCount()), parked);
        const mlir::Value registers = literal(build, where, u32, std::to_string(window));
        auto frame = ec::CallOpaqueOp::create(
            build, where, mlir::TypeRange{frame_ptr}, callee("ct_aot_enter"),
            mlir::ValueRange{in_ctx, in_site, registers, in_receiver, storage});

        // ITS FAILURE IS A NULL POINTER, NOT A STATUS. There is no frame, so
        // there is nothing to leave and no handler to reach.
        const mlir::Value null = literal(build, where, frame_ptr, "nullptr");
        auto ok = ec::CmpOp::create(build, where, mlir::IntegerType::get(context, 1),
                                    ec::CmpPredicate::ne, frame.getResult(0), null);

        mlir::Block * raised = entry.addBlock();
        mlir::Block * running = entry.addBlock();
        mlir::cf::CondBranchOp::create(build, where, ok, running, mlir::ValueRange{}, raised,
                                       mlir::ValueRange{});

        build.setInsertionPointToStart(raised);
        ec::ReturnOp::create(
            build, where,
            literal(build, where, status,
                    "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::failed)"));

        // ---- the blocks ---------------------------------------------------
        //
        // Every CTJS block gets one here, arguments and all. They are NOT
        // eliminated in this pass: --emitc-eliminate-block-arguments does that
        // afterwards, and keeping the two separate is what lets the elimination
        // be tested against a program that runs rather than only against the
        // output of this file.
        // THE CLOSURE, WHICH ONLY EXISTS ONCE THE FRAME DOES. ct_aot_callee
        // reads call_frame::closure, so this cannot be hoisted above
        // ct_aot_enter the way the parameters had to be pulled below it - and
        // it must be emitted BEFORE the mapping is built rather than after,
        // which is not a style point: mapping a block argument to a Value that
        // is still null is accepted silently, and the crash arrives later
        // inside Operation::create, with a stack that names neither.
        build.setInsertionPointToEnd(running);
        if (!body.getArgument(arg_callee).use_empty()) {
            mapping_callee = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                                      callee("ct_aot_callee"),
                                                      mlir::ValueRange{frame.getResult(0)})
                                 .getResult(0);
        }

        // ARGUMENT 0 IS THE EFFECTIVE RECEIVER, NOT THE ENTRY'S RAW ONE, and the
        // difference is a live bug for arrows. The importer maps op::load_this
        // to this argument, and VM_CASE(load_this) is
        // `effective_this(*vm_frame)` - which returns the enclosing method's
        // object when the frame's closure is an arrow, and the frame's own
        // receiver otherwise. Delivering the entry's `receiver` agrees for
        // every ordinary function and is wrong for every compiled arrow.
        //
        // THE RETURN PROTOCOL STILL USES THE RAW ONE. ct_aot_return_value
        // substitutes `receiver` when a constructor returns a primitive, and
        // that is the frame's own receiver by definition - an arrow cannot be
        // constructed at all.
        mlir::IRMapping mapping;
        mapping.map(body.getArgument(arg_receiver),
                    ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                             callee("ct_aot_this"),
                                             mlir::ValueRange{frame.getResult(0)})
                        .getResult(0));
        if (mapping_callee) { mapping.map(body.getArgument(arg_callee), mapping_callee); }
        for (unsigned i = 0; i < declared; ++i) {
            mapping.map(body.getArgument(implicit_arguments + i), parameters[i]);
        }
        mapping.map(entered.getResult(), frame.getResult(0));
        mapping.map(&body, running);

        for (mlir::Block & block : llvm::drop_begin(function.getBody())) {
            llvm::SmallVector<mlir::Type> types;
            llvm::SmallVector<mlir::Location> places;
            for (const mlir::BlockArgument argument : block.getArguments()) {
                types.push_back(as_emitc(argument.getType(), value));
                places.push_back(argument.getLoc());
            }
            mlir::Block * fresh = entry.addBlock();
            fresh->addArguments(types, places);
            mapping.map(&block, fresh);
            for (auto [before, after] : llvm::zip(block.getArguments(), fresh->getArguments())) {
                mapping.map(before, after);
            }
        }

        // ---- and their operations -----------------------------------------
        compiled_entry scope{
            entry,
            frame.getResult(0),
            in_receiver,
            in_constructing,
            in_out,
            value,
            status,
            nullptr,
            scope_slots,
            windows,
            literal(build, where, pointer_to(context, "const ctbrowser::aot::ct_aot_site"),
                    "reinterpret_cast<const ctbrowser::aot::ct_aot_site *>(&" + marker + ")"),
            memo_slots};

        // THE PARAMETERS ARE ROOTED FIRST, before anything can collect. They
        // were read out of argv before the frame existed - argv dies at
        // ct_aot_enter - so this is the earliest moment they can be.
        for (unsigned i = 0; i < declared; ++i) {
            park_if_tracked(scope, build, where, body.getArgument(implicit_arguments + i),
                            parameters[i]);
        }

        for (mlir::Block & block : function.getBody()) {
            build.setInsertionPointToEnd(mapping.lookup(&block));
            for (mlir::Operation & op : block) {
                convert(op, build, mapping, scope);
                // EVERY VALUE THIS OPERATION PRODUCED, ROOTED IMMEDIATELY -
                // after convert rather than inside it, so that no conversion
                // can forget. Same reason the ABI shape check is a trait on the
                // base class rather than a verifier written per operation.
                for (const mlir::Value produced : op.getResults()) {
                    if (!mapping.contains(produced)) { continue; }
                    park_if_tracked(scope, build, op.getLoc(), produced, mapping.lookup(produced));
                }
            }
        }

        function.erase();
        return true;
    }

    // ONE CTJS TYPE, IN C++. Only !ctjs.value needs translating; an i1 from
    // ctjs.truthy is already a machine bit and EmitC prints it as `bool`.
    static mlir::Type as_emitc(mlir::Type type, mlir::Type value) {
        return mlir::isa<ValueType>(type) ? value : type;
    }

    // ONE OPERATION.
    //
    // NO DEFAULT ARM AND NO FALLBACK. body_is_supported has already refused
    // anything not listed here, so an operation reaching the end is a
    // disagreement between the two - which is a bug in this file rather than in
    // its input, and is worth crashing over rather than emitting a call to
    // something plausible.
    // WHERE A FAILED HELPER GOES, built once per function and shared.
    //
    // THE STATUS ARRIVES AS A BLOCK ARGUMENT, which is safe precisely because
    // --emitc-eliminate-block-arguments runs after this pass and turns it into
    // a variable. Every status test in the body branches here with its own
    // status; writing that variable by hand would be the same thing done worse.
    //
    // ct_aot_leave IS CONDITIONAL, and that is the part that is easy to get
    // wrong. On CT_AOT_UNWOUND the unwinder has already truncated the frame
    // stack and destroyed this frame - the row says leave "must NOT run on the
    // CT_AOT_UNWOUND path" - so calling it again would pop somebody else's
    // frame. On CT_AOT_FAILED the frame is still standing and must be left.
    //
    // CT_AOT_CAUGHT CANNOT REACH HERE, and that is a fact about the input
    // rather than an assumption: `caught` is reported only when a handler in
    // THIS frame won, and nothing in the allow-list pushes one. When try/catch
    // arrives this block needs a third arm - a `caught` escaping the entry is a
    // SILENT wrong answer, because the caller writes no result and reports
    // success, so the program sees `undefined` with no error at all.
    mlir::Block * failure_path(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where) {
        if (scope.propagate) { return scope.propagate; }
        mlir::OpBuilder::InsertionGuard keep(build);

        mlir::Block * propagate = scope.entry.addBlock();
        propagate->addArgument(scope.status, where);
        mlir::Block * leaving = scope.entry.addBlock();
        mlir::Block * finish = scope.entry.addBlock();

        build.setInsertionPointToEnd(propagate);
        const mlir::Value gone =
            literal(build, where, scope.status,
                    "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::unwound)");
        auto destroyed =
            ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                              ec::CmpPredicate::eq, propagate->getArgument(0), gone);
        mlir::cf::CondBranchOp::create(build, where, destroyed, finish, mlir::ValueRange{}, leaving,
                                       mlir::ValueRange{});

        build.setInsertionPointToEnd(leaving);
        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_leave"),
                                 mlir::ValueRange{scope.frame});
        mlir::cf::BranchOp::create(build, where, finish);

        build.setInsertionPointToEnd(finish);
        ec::ReturnOp::create(build, where, propagate->getArgument(0));

        scope.propagate = propagate;
        return propagate;
    }

    // A HELPER THAT ANSWERS WITH A STATUS AND A VALUE THROUGH A POINTER.
    //
    // Most of the ABI has this shape, so it is written once: declare a local
    // for the result, take its address, call, test the status against
    // ct_aot_status::ok BY NAME, and continue in a fresh block where the result
    // is loaded.
    //
    // THE BLOCK SPLITS AND THE CALLER KEEPS EMITTING INTO THE NEW ONE. An
    // operation with an exception edge is two blocks, and everything after it
    // in the source block belongs to the second - which is why this leaves the
    // builder pointing at the continuation rather than restoring it.
    mlir::Value status_call(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                            const std::string & symbol, llvm::ArrayRef<mlir::Value> arguments,
                            mlir::Type produces) {
        // THE OUT-PARAMETER IS NOT ALWAYS A VALUE, which is why this takes a
        // type. ct_aot_binary_op writes a `uint64_t *`, ct_aot_loose_equals a
        // `uint32_t *` boolean and ct_aot_compare an `int32_t *` ORDERING. The
        // pointer's pointee has to match the row or the emitted call is a type
        // error at best and a reinterpreted write at worst.
        auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(produces),
                                           ec::OpaqueAttr::get(build.getContext(), ""));
        auto address =
            ec::AddressOfOp::create(build, where, ec::PointerType::get(produces), slot.getResult());

        llvm::SmallVector<mlir::Value> passed(arguments.begin(), arguments.end());
        passed.push_back(address.getResult());
        auto answered =
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{scope.status}, symbol, passed);

        const mlir::Value ok = literal(build, where, scope.status,
                                       "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)");
        auto survived =
            ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                              ec::CmpPredicate::eq, answered.getResult(0), ok);

        mlir::Block * carry_on = scope.entry.addBlock();
        mlir::cf::CondBranchOp::create(build, where, survived, carry_on, mlir::ValueRange{},
                                       failure_path(scope, build, where),
                                       mlir::ValueRange{answered.getResult(0)});

        // LOADED ONLY ON THE OK PATH, which the ABI requires rather than merely
        // permits: "*out written ONLY on CT_AOT_OK", so on any other status the
        // local still holds whatever it held before the call.
        build.setInsertionPointToEnd(carry_on);
        return ec::LoadOp::create(build, where, produces, slot.getResult()).getResult();
    }

    // A HELPER THAT ANSWERS WITH A STATUS AND NOTHING ELSE.
    //
    // ct_aot_set_index is the first: the bytecode performs the write and
    // evaluates the expression separately, so there is a status to test and no
    // result to load. The edge is the same one; only the out-parameter is
    // missing, and inventing a slot for a value the helper never writes would
    // be a local read before it was ever assigned.
    void status_call_void(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                          const std::string & symbol, llvm::ArrayRef<mlir::Value> arguments) {
        auto answered = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{scope.status},
                                                 symbol, arguments);
        const mlir::Value ok = literal(build, where, scope.status,
                                       "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)");
        auto survived =
            ec::CmpOp::create(build, where, mlir::IntegerType::get(build.getContext(), 1),
                              ec::CmpPredicate::eq, answered.getResult(0), ok);
        mlir::Block * carry_on = scope.entry.addBlock();
        mlir::cf::CondBranchOp::create(build, where, survived, carry_on, mlir::ValueRange{},
                                       failure_path(scope, build, where),
                                       mlir::ValueRange{answered.getResult(0)});
        build.setInsertionPointToEnd(carry_on);
    }

    void convert(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                 compiled_entry & scope) {
        const mlir::Location where = op.getLoc();
        const mlir::Type value = scope.value;
        const mlir::Value receiver = scope.receiver;
        const mlir::Value constructing = scope.constructing;
        const mlir::Value out = scope.out;
        const mlir::Type status = scope.status;

        // The frame is established by the entry block, not by this operation.
        if (mlir::isa<FrameEnterOp>(op)) { return; }

        if (auto exit = mlir::dyn_cast<FrameExitOp>(op)) {
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_leave"),
                                     mlir::ValueRange{mapping.lookup(exit.getContext())});
            return;
        }

        if (auto constant = mlir::dyn_cast<ConstantOp>(op)) {
            // A STRING LITERAL IS A CALL, not a spelling. It allocates, so it
            // needs the frame - and it is MEMOISED by (site, slot), which the
            // row insists is part of the ABI rather than an optimisation:
            // without it a literal in a loop allocates once per iteration and
            // reaches the process-lifetime allocation ceiling on a program the
            // interpreter runs forever.
            //
            // `site` IS THE ENTRY'S OWN, which is the function_proto the
            // interpreter keys the same cache by - so a literal shared between
            // a compiled body and an interpreted one is one object, not two.
            if (auto text = mlir::dyn_cast<StringAttr>(constant.getValue())) {
                const llvm::StringRef bytes = text.getValue();
                mapping.map(
                    constant.getResult(),
                    ec::CallOpaqueOp::create(
                        build, where, mlir::TypeRange{value}, callee("ct_aot_new_string"),
                        mlir::ValueRange{
                            scope.frame, scope.memo_site,
                            literal(build, where, opaque(build.getContext(), "uint32_t"),
                                    std::to_string(scope.memo_slots.lookup(&op))),
                            literal(build, where, pointer_to(build.getContext(), "const char"),
                                    c_string_literal(bytes)),
                            literal(build, where, opaque(build.getContext(), "uint32_t"),
                                    std::to_string(bytes.size()))})
                        .getResult(0));
                return;
            }
            mapping.map(constant.getResult(), constant_value(build, where, value, constant));
            return;
        }

        // ToBoolean, AND THE COMPARISON IS PART OF IT. ct_aot_truthy answers
        // with a uint32_t that is 0 or 1 - the ABI has no bool - while
        // ctjs.truthy's result is an i1, because that is what cf.cond_br takes.
        // Testing it against zero is the conversion, and doing it here rather
        // than trusting C++'s implicit narrowing keeps the emitted code saying
        // what it means.
        if (auto truthy = mlir::dyn_cast<TruthyOp>(op)) {
            const auto u32 = opaque(build.getContext(), "uint32_t");
            auto answered = ec::CallOpaqueOp::create(
                build, where, mlir::TypeRange{u32}, callee("ct_aot_truthy"),
                mlir::ValueRange{mapping.lookup(truthy.getValue())});
            auto bit = ec::CmpOp::create(
                build, where, mlir::IntegerType::get(build.getContext(), 1), ec::CmpPredicate::ne,
                answered.getResult(0), literal(build, where, u32, "0"));
            mapping.map(truthy.getResult(), bit.getResult());
            return;
        }

        // THE TWO BINARY FAMILIES, WHICH ARE TWO HELPERS AND TWO OPCODE
        // TABLES. `ctjs.binary add` is source `+` and must reach
        // op::add_generic, which runs ToPrimitive and can call a user valueOf;
        // `ctjs.binary_static add` is op::add and cannot run user code at all.
        // Folding them would make `x + y` and `x++` the same call, which is
        // what OpcodeMapping.hpp exists to prevent - and the opcode is spelled
        // as an ENUMERATOR, so the renumbering Phases 13 and 14 perform becomes
        // a build error in the generated code rather than a different operator.
        if (auto binary = mlir::dyn_cast<BinaryOp>(op)) {
            const mlir::Value kind = literal(build, where, opaque(build.getContext(), "uint32_t"),
                                             opcode_spelling(opcode_for_binary(binary.getKind())));
            mapping.map(binary.getResult(),
                        status_call(scope, build, where, callee("ct_aot_binary_op"),
                                    {scope.frame, kind, mapping.lookup(binary.getLhs()),
                                     mapping.lookup(binary.getRhs())},
                                    scope.value));
            return;
        }
        if (auto binary = mlir::dyn_cast<BinaryStaticOp>(op)) {
            const mlir::Value kind =
                literal(build, where, opaque(build.getContext(), "uint32_t"),
                        opcode_spelling(opcode_for_binary_static(binary.getKind())));
            mapping.map(binary.getResult(),
                        status_call(scope, build, where, callee("ct_aot_binary_op_static"),
                                    {scope.frame, kind, mapping.lookup(binary.getLhs()),
                                     mapping.lookup(binary.getRhs())},
                                    scope.value));
            return;
        }

        // THE UNARY OPERATORS, WHICH REACH FOUR DIFFERENT HELPERS AND ONE
        // NONE AT ALL - which is why ctjs.unary is not a CTJS_RuntimeOp and
        // why this switches on the kind.
        if (auto unary = mlir::dyn_cast<UnaryOp>(op)) {
            const mlir::Value operand = mapping.lookup(unary.getOperand());
            switch (unary.getKind()) {
            case UnaryKind::Neg:
                mapping.map(unary.getResult(),
                            status_call(scope, build, where, callee("ct_aot_negate"),
                                        {scope.frame, operand}, value));
                return;
            case UnaryKind::BitNot:
                mapping.map(unary.getResult(),
                            status_call(scope, build, where, callee("ct_aot_bit_not"),
                                        {scope.frame, operand}, value));
                return;
            case UnaryKind::Plus: {
                // `+x` IS ToNumber, AND ITS OUT-PARAMETER IS A double, not a
                // value - so the result is boxed rather than used directly.
                const mlir::Value number =
                    status_call(scope, build, where, callee("ct_aot_to_number"),
                                {scope.frame, operand}, opaque(build.getContext(), "double"));
                mapping.map(unary.getResult(), box(build, where, value, "ctc_box_number", number));
                return;
            }
            case UnaryKind::Not: {
                // `!x` IS ToBoolean NEGATED, and ToBoolean cannot fail - the
                // row is (0, 0, 0) and takes no frame - so there is no status
                // and no edge. Testing the uint32_t against zero IS the
                // negation.
                const auto u32 = opaque(build.getContext(), "uint32_t");
                auto answered =
                    ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                             callee("ct_aot_truthy"), mlir::ValueRange{operand});
                auto negated = ec::CmpOp::create(
                    build, where, mlir::IntegerType::get(build.getContext(), 1),
                    ec::CmpPredicate::eq, answered.getResult(0), literal(build, where, u32, "0"));
                mapping.map(unary.getResult(),
                            box(build, where, value, "ctc_box_bool", negated.getResult()));
                return;
            }
            case UnaryKind::Void:
                // `void x` EVALUATES ITS OPERAND AND YIELDS undefined. The
                // operand is already evaluated - it is an SSA value - so there
                // is nothing to emit but the answer.
                mapping.map(unary.getResult(), undefined(build, where, value));
                return;
            case UnaryKind::TypeOf: {
                // TWO CALLS, and the second is the point. ct_aot_type_of_name
                // is INFALLIBLE and answers with a LENGTH plus a pointer to
                // STATIC storage - "the return slot carries a LENGTH, and under
                // the return-type rule an unsigned return is data, never a
                // status" - so there is no edge. What it needs is a string.
                //
                // THE SITE IS nullptr, MEANING DO NOT MEMOISE, and
                // ct_aot_new_string's row names this exact case: it "lets
                // ct_aot_type_of_name's companion allocation stay at parity",
                // because VM_CASE(type_of) has no cache at all. Memoising here
                // would allocate FEWER times than the interpreter - a
                // divergence in the same raise tier the memo exists to protect.
                const auto u32 = opaque(build.getContext(), "uint32_t");
                const auto text_ptr = pointer_to(build.getContext(), "const char");
                auto slot = ec::VariableOp::create(build, where, ec::LValueType::get(text_ptr),
                                                   ec::OpaqueAttr::get(build.getContext(), ""));
                auto address = ec::AddressOfOp::create(build, where, ec::PointerType::get(text_ptr),
                                                       slot.getResult());
                auto len = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                                    callee("ct_aot_type_of_name"),
                                                    mlir::ValueRange{operand, address.getResult()});
                auto text = ec::LoadOp::create(build, where, text_ptr, slot.getResult());
                mapping.map(
                    unary.getResult(),
                    ec::CallOpaqueOp::create(
                        build, where, mlir::TypeRange{value}, callee("ct_aot_new_string"),
                        mlir::ValueRange{
                            scope.frame,
                            literal(build, where,
                                    pointer_to(build.getContext(), "const ctbrowser::aot::"
                                                                   "ct_aot_site"),
                                    "nullptr"),
                            literal(build, where, u32, "0"), text.getResult(), len.getResult(0)})
                        .getResult(0));
                return;
            }
            }
            llvm_unreachable("body_is_supported admitted a unary kind convert cannot emit");
        }

        // THE COMPARISONS, WHICH ARE THREE HELPERS AND THREE EFFECT PROFILES.
        if (auto compare = mlir::dyn_cast<CompareOp>(op)) {
            const mlir::Value lhs = mapping.lookup(compare.getLhs());
            const mlir::Value rhs = mapping.lookup(compare.getRhs());
            const auto u32 = opaque(build.getContext(), "uint32_t");
            const auto i32 = opaque(build.getContext(), "int32_t");
            const auto bit = mlir::IntegerType::get(build.getContext(), 1);

            if (compare.getKind() == CompareKind::StrictEq) {
                // STRICT EQUALITY CANNOT THROW AND TAKES NO FRAME: its row is
                // (0, 0, 0) and it answers with a uint32_t directly, so there
                // is no out-parameter, no status and no exception edge.
                auto answered = ec::CallOpaqueOp::create(build, where, mlir::TypeRange{u32},
                                                         callee("ct_aot_strict_equals"),
                                                         mlir::ValueRange{lhs, rhs});
                auto truth =
                    ec::CmpOp::create(build, where, bit, ec::CmpPredicate::ne,
                                      answered.getResult(0), literal(build, where, u32, "0"));
                mapping.map(compare.getResult(),
                            box(build, where, value, "ctc_box_bool", truth.getResult()));
                return;
            }
            if (compare.getKind() == CompareKind::Eq) {
                // LOOSE EQUALITY CAN, because it converts - and its
                // out-parameter is a uint32_t boolean, not a value.
                const mlir::Value answered =
                    status_call(scope, build, where, callee("ct_aot_loose_equals"),
                                {scope.frame, lhs, rhs}, u32);
                auto truth = ec::CmpOp::create(build, where, bit, ec::CmpPredicate::ne, answered,
                                               literal(build, where, u32, "0"));
                mapping.map(compare.getResult(),
                            box(build, where, value, "ctc_box_bool", truth.getResult()));
                return;
            }

            // THE FOUR RELATIONAL KINDS SHARE ONE HELPER AND AN ORDERING, and
            // they are NOT negations of one another. ct_aot_compare answers
            // with less/equivalent/greater/UNORDERED, and unordered - a NaN on
            // either side - makes all four false, including `>=`. Lowering `>=`
            // as `!(<)` would make `NaN >= NaN` true.
            //
            // THE ORDERING'S NUMBERS ARE CONTRACTUAL, unlike the status enum's -
            // aot.hpp says so in as many words - but they are still spelled as
            // enumerators, because a name that is checked costs nothing.
            const mlir::Value ordering = status_call(scope, build, where, callee("ct_aot_compare"),
                                                     {scope.frame, lhs, rhs}, i32);
            const auto is = [&](llvm::StringRef named) {
                return ec::CmpOp::create(
                           build, where, bit, ec::CmpPredicate::eq, ordering,
                           literal(build, where, i32,
                                   ("static_cast<int32_t>(ctbrowser::aot::ct_aot_ordering::" +
                                    named + ")")
                                       .str()))
                    .getResult();
            };
            mlir::Value truth;
            switch (compare.getKind()) {
            case CompareKind::Lt: truth = is("less"); break;
            case CompareKind::Gt: truth = is("greater"); break;
            case CompareKind::Le:
                truth = ec::LogicalOrOp::create(build, where, bit, is("less"), is("equivalent"))
                            .getResult();
                break;
            case CompareKind::Ge:
                truth = ec::LogicalOrOp::create(build, where, bit, is("greater"), is("equivalent"))
                            .getResult();
                break;
            default: llvm_unreachable("the two equality kinds are handled above");
            }
            mapping.map(compare.getResult(), box(build, where, value, "ctc_box_bool", truth));
            return;
        }

        // THE GLOBALS, WHICH ARE INFALLIBLE AND SO HAVE NO EDGE AT ALL.
        // Both rows are (0, 0, 0): reading an undeclared global does NOT throw
        // a ReferenceError here - the row says the absence is load-bearing -
        // and neither reads nor writes can collect. So each is one call.
        //
        // THE NAME IS BYTES AND A LENGTH, not a NUL-terminated string, which is
        // why the length is emitted rather than left to strlen: a global whose
        // name contains a zero byte is legal JavaScript and strlen would stop
        // at it.
        if (auto global = mlir::dyn_cast<LoadGlobalOp>(op)) {
            const llvm::StringRef name = global.getName();
            mapping.map(global.getResult(),
                        ec::CallOpaqueOp::create(
                            build, where, mlir::TypeRange{value}, callee("ct_aot_global_get"),
                            mlir::ValueRange{
                                scope.frame,
                                literal(build, where, pointer_to(build.getContext(), "const char"),
                                        c_string_literal(name)),
                                literal(build, where, opaque(build.getContext(), "uint32_t"),
                                        std::to_string(name.size()))})
                            .getResult(0));
            return;
        }
        if (auto global = mlir::dyn_cast<StoreGlobalOp>(op)) {
            const llvm::StringRef name = global.getName();
            ec::CallOpaqueOp::create(
                build, where, mlir::TypeRange{}, callee("ct_aot_global_set"),
                mlir::ValueRange{scope.frame,
                                 literal(build, where, pointer_to(build.getContext(), "const char"),
                                         c_string_literal(name)),
                                 literal(build, where, opaque(build.getContext(), "uint32_t"),
                                         std::to_string(name.size())),
                                 mapping.lookup(global.getValue())});
            return;
        }

        // A PROPERTY READ. Its helper's inline-cache parameter is nullptr and
        // has to be: ct_aot_ic is FORWARD-DECLARED ONLY, so nothing can
        // allocate one, and the implementation says the parameter is "taken and
        // ignored, because the signature is the thing two code generators are
        // written against and a parameter added later is a break". Phase 26
        // attaches real storage; until then this is not a shortcut but the only
        // spelling available.
        if (auto get = mlir::dyn_cast<GetPropertyOp>(op)) {
            const mlir::Value no_cache =
                literal(build, where, pointer_to(build.getContext(), "ctbrowser::aot::ct_aot_ic"),
                        "nullptr");
            mapping.map(get.getResult(),
                        status_call(scope, build, where, callee("ct_aot_get_index"),
                                    {scope.frame, mapping.lookup(get.getObject()),
                                     mapping.lookup(get.getKey()), no_cache},
                                    value));
            return;
        }

        // A CALL, WHICH IS THE FIRST OPERATION THAT NEEDS THE FRAME FOR
        // SOMETHING OTHER THAN ROOTING.
        //
        // ct_aot_call takes `const uint64_t *argv` - a CONTIGUOUS run - and the
        // arguments are rooted in slots of their own, which are not adjacent.
        // So a run was reserved for this site and the arguments are copied into
        // it here, immediately before the call. IN THE FRAME, not in a C++
        // array: ct_aot_call is a safepoint that runs arbitrary user JavaScript
        // before it reads them, and an array of locals is invisible to a
        // precise collector.
        //
        // THE SPAN IS FETCHED ONCE PER STORE AND AGAIN FOR THE POINTER, because
        // context::call resizes context::registers_ and the row says the
        // pointer is valid "NOT ONE INSTRUCTION LONGER" than the next
        // safepoint. Nothing between these stores is a safepoint, so one fetch
        // would do - and hoisting it is a decision this backend has no analysis
        // to justify.
        if (auto call = mlir::dyn_cast<CallOp>(op)) {
            const unsigned base = scope.argument_windows.lookup(&op);
            const auto arguments = call.getArgs();
            for (auto [index, argument] : llvm::enumerate(arguments)) {
                park(scope, build, where, mapping.lookup(argument),
                     base + static_cast<unsigned>(index));
            }

            const mlir::Value argv = window_pointer(scope, build, where, base);

            // `key` AND `site` ARE THE ROW'S DIAGNOSTIC ARGUMENTS and the
            // implementation ignores both. `key` names the callee in the
            // message op::call_computed would produce; `site` carries a
            // backwards scan of bytecode that an AOT frame does not have.
            // Passing undefined and nullptr is honest about having neither
            // rather than inventing one.
            mapping.map(call.getResult(),
                        status_call(scope, build, where, callee("ct_aot_call"),
                                    {scope.frame, mapping.lookup(call.getCallee()),
                                     mapping.lookup(call.getReceiver()), argv,
                                     literal(build, where, opaque(build.getContext(), "uint32_t"),
                                             std::to_string(arguments.size())),
                                     undefined(build, where, value),
                                     literal(build, where,
                                             pointer_to(build.getContext(),
                                                        "const ctbrowser::aot::ct_aot_site"),
                                             "nullptr")},
                                    value));
            return;
        }

        // READING AND WRITING A CAPTURED BINDING, each a pair of calls.
        if (auto load = mlir::dyn_cast<LoadUpvalueOp>(op)) {
            mapping.map(load.getResult(), cell_of_upvalue(scope, build, where, load.getIndex(),
                                                          /*read=*/true, mlir::Value{}));
            return;
        }
        if (auto store = mlir::dyn_cast<StoreUpvalueOp>(op)) {
            (void)cell_of_upvalue(scope, build, where, store.getIndex(), /*read=*/false,
                                  mapping.lookup(store.getValue()));
            return;
        }

        // BUILDING A CLOSURE.
        //
        // RAISE TIER ONLY, so there is no status and no exception edge: the
        // row's three failures - the allocation ceiling, no program to take a
        // function from, and an index or count that does not match - all raise,
        // and a caller polls ct_aot_failed at a back edge. It IS a safepoint, so
        // the result is parked like every other value.
        //
        // THE UPVALUES GO IN PARALLEL WITH THE DESCRIPTORS, which is what the
        // importer built and what the helper reads. Packed would capture the
        // wrong bindings and say nothing.
        if (auto made = mlir::dyn_cast<CreateClosureOp>(op)) {
            const unsigned base = scope.argument_windows.lookup(&op);
            const auto upvalues = made.getUpvalues();
            for (auto [index, captured] : llvm::enumerate(upvalues)) {
                park(scope, build, where, mapping.lookup(captured),
                     base + static_cast<unsigned>(index));
            }
            const auto u32 = opaque(build.getContext(), "uint32_t");
            mapping.map(
                made.getResult(),
                ec::CallOpaqueOp::create(
                    build, where, mlir::TypeRange{value}, callee("ct_aot_make_closure"),
                    mlir::ValueRange{scope.frame, mapping.lookup(made.getEnclosingClosure()),
                                     literal(build, where, u32, std::to_string(made.getFunction())),
                                     window_pointer(scope, build, where, base),
                                     literal(build, where, u32, std::to_string(upvalues.size())),
                                     mapping.lookup(made.getEnclosingThis())})
                    .getResult(0));
            return;
        }

        // CELLS - the boxes a captured binding lives in.
        //
        // ALL THREE ARE EDGE-FREE. cell_get and cell_set are (0, 0, 0) and take
        // no frame at all: their FAILURE line calls the silence a semantic
        // guarantee, because a non-cell argument yields undefined or is
        // dropped, and that is what lets them compose with ct_aot_upvalue_cell
        // to reproduce the fused opcodes exactly.
        //
        // ct_aot_cell_new IS may_throw AND a safepoint, but RAISE TIER ONLY -
        // allocate() raises past the ceiling and still returns a well-formed
        // cell - so it is a plain call whose result is parked, not a status
        // test.
        if (auto cell = mlir::dyn_cast<CreateCellOp>(op)) {
            mapping.map(cell.getResult(),
                        ec::CallOpaqueOp::create(
                            build, where, mlir::TypeRange{value}, callee("ct_aot_cell_new"),
                            mlir::ValueRange{scope.frame, mapping.lookup(cell.getInitial())})
                            .getResult(0));
            return;
        }
        if (auto read = mlir::dyn_cast<CellGetOp>(op)) {
            mapping.map(read.getResult(),
                        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                                 callee("ct_aot_cell_get"),
                                                 mlir::ValueRange{mapping.lookup(read.getCell())})
                            .getResult(0));
            return;
        }
        if (auto write = mlir::dyn_cast<CellSetOp>(op)) {
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_cell_set"),
                                     mlir::ValueRange{mapping.lookup(write.getCell()),
                                                      mapping.lookup(write.getValue())});
            return;
        }

        // A PROPERTY WRITE. Its inline cache is nullptr for the reason
        // ctjs.get_property's is: ct_aot_ic is forward-declared and nothing can
        // allocate one.
        //
        // ITS STATUS EDGE IS NOT COVERED BY A CASE, and that is worth saying
        // rather than leaving to be assumed. Nothing in the differential
        // fixture makes a property write FAIL - that needs a setter that
        // throws, or a proxy trap - so the branch this emits is built and never
        // taken. The write itself is covered; the edge is not.
        if (auto write = mlir::dyn_cast<SetPropertyOp>(op)) {
            status_call_void(
                scope, build, where, callee("ct_aot_set_index"),
                {scope.frame, mapping.lookup(write.getObject()), mapping.lookup(write.getKey()),
                 mapping.lookup(write.getValue()),
                 literal(build, where, pointer_to(build.getContext(), "ctbrowser::aot::ct_aot_ic"),
                         "nullptr")});
            return;
        }

        // OBJECT AND ARRAY LITERALS.
        //
        // BOTH ALLOCATE AND ARE RAISE TIER ONLY: allocate() raises past the
        // ceiling and STILL returns a well-formed object, so there is no status
        // to test - only a poll to schedule at a back edge. They are safepoints,
        // so their results are parked like every other value.
        if (auto made = mlir::dyn_cast<CreateObjectOp>(op)) {
            mapping.map(made.getResult(),
                        ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value},
                                                 callee("ct_aot_new_object"),
                                                 mlir::ValueRange{scope.frame})
                            .getResult(0));
            return;
        }
        if (auto made = mlir::dyn_cast<CreateArrayOp>(op)) {
            // `reserve_hint` IS A HINT and the row says so - an array that
            // ignores it is merely slower. The elements the operation carries
            // are appended after, which is also how the bytecode builds one:
            // new_array then one append per element.
            const auto elements = made.getElements();
            const mlir::Value array =
                ec::CallOpaqueOp::create(
                    build, where, mlir::TypeRange{value}, callee("ct_aot_new_array"),
                    mlir::ValueRange{scope.frame,
                                     literal(build, where, opaque(build.getContext(), "uint32_t"),
                                             std::to_string(elements.size()))})
                    .getResult(0);
            for (const mlir::Value element : elements) {
                ec::CallOpaqueOp::create(
                    build, where, mlir::TypeRange{}, callee("ct_aot_append"),
                    mlir::ValueRange{scope.frame, array, mapping.lookup(element)});
            }
            mapping.map(made.getResult(), array);
            return;
        }
        if (auto push = mlir::dyn_cast<AppendOp>(op)) {
            // (0, 0, 0): growing a std::vector is malloc, not allocate(), so no
            // GC object is created and there is nothing to test or park.
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_append"),
                                     mlir::ValueRange{scope.frame, mapping.lookup(push.getArray()),
                                                      mapping.lookup(push.getElement())});
            return;
        }

        if (auto returned = mlir::dyn_cast<ReturnOp>(op)) {
            // ct_aot_return_value TAKES NO FRAME HANDLE, which is why the entry
            // delivers `receiver` and `constructing` by value: the body still
            // needs them after ct_aot_leave has run. The three-argument form is
            // not optional - one compiled body serves both `f()` and `new f()`.
            const mlir::Value produced = returned.getValue() ? mapping.lookup(returned.getValue())
                                                             : undefined(build, where, value);
            auto result = ec::CallOpaqueOp::create(
                build, where, mlir::TypeRange{value}, callee("ct_aot_return_value"),
                mlir::ValueRange{produced, receiver, constructing});
            auto destination =
                ec::DereferenceOp::create(build, where, ec::LValueType::get(value), out);
            ec::AssignOp::create(build, where, destination, result.getResult(0));
            ec::ReturnOp::create(
                build, where,
                literal(build, where, status,
                        "static_cast<int32_t>(ctbrowser::aot::ct_aot_status::ok)"));
            return;
        }

        // A BRANCH IS CLONED, NOT REBUILT. cf.br and cf.cond_br carry their
        // successors as blocks, and IRMapping remaps a cloned operation's
        // successors as well as its operands - so the block map built above is
        // all this needs.
        if (mlir::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp>(op)) {
            build.clone(op, mapping);
            return;
        }

        // NOT llvm_unreachable, WHICH IS SILENT IN RELEASE. Under -DNDEBUG it
        // is __builtin_unreachable(), so an operation reaching here does not
        // abort - it produces whatever the compiler decided the impossible
        // branch should do. That happened: ctjs.create_closure was added to
        // body_is_supported and not to this switch, and instead of the loud
        // failure this line was written to give, the operation survived into
        // the output with its operands rewritten, and the module failed to
        // verify somewhere else entirely.
        llvm::report_fatal_error(llvm::Twine("ctcompile: body_is_supported admits ") +
                                 op.getName().getStringRef() + " and convert() does not emit it");
    }

    // ROOT ONE VALUE IN THE FRAME.
    //
    // THE SPAN IS RE-FETCHED EVERY TIME, and that is not laziness. The row is
    // explicit: the pointer "IS VALID UNTIL THE NEXT SAFEPOINT AND NOT ONE
    // INSTRUCTION LONGER", because ct_aot_enter and every nested call resize
    // context::registers_ and may reallocate it. "A backend that hoists this
    // call out of a loop containing a safepoint has miscompiled." One extra
    // load per store is the price of not being that backend.
    //
    // STORING ONCE IS ENOUGH because the collector does not MOVE: it marks, and
    // then deletes what it did not mark. A value reachable from a slot keeps
    // its bits, so the C++ local holding a copy stays valid. Against a moving
    // collector every use would have to reload instead.
    void park(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
              mlir::Value rooted, unsigned slot) {
        auto span = ec::CallOpaqueOp::create(build, where,
                                             mlir::TypeRange{ec::PointerType::get(scope.value)},
                                             callee("ct_aot_slots"), mlir::ValueRange{scope.frame});
        auto cell = ec::SubscriptOp::create(
            build, where, ec::LValueType::get(scope.value), span.getResult(0),
            mlir::ValueRange{literal(build, where, mlir::IntegerType::get(build.getContext(), 32),
                                     std::to_string(slot))});
        ec::AssignOp::create(build, where, cell.getResult(), rooted);
    }

    // Park it if it is a JavaScript value this function tracks.
    void park_if_tracked(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                         mlir::Value original, mlir::Value emitted) {
        const auto found = scope.slots.find(original);
        if (found != scope.slots.end()) { park(scope, build, where, emitted, found->second); }
    }

    // THE CELL AN UPVALUE INDEX NAMES, then read or written.
    //
    // NEITHER CALL CAN FAIL. All three rows are (0, 0, 0) - no status, no
    // exception edge, no safepoint - so this is two plain calls with nothing
    // between them to test.
    mlir::Value cell_of_upvalue(compiled_entry & scope, mlir::OpBuilder & build,
                                mlir::Location where, std::uint32_t index, bool read,
                                mlir::Value written) {
        auto cell = ec::CallOpaqueOp::create(
            build, where, mlir::TypeRange{scope.value}, callee("ct_aot_upvalue_cell"),
            mlir::ValueRange{scope.frame,
                             literal(build, where, opaque(build.getContext(), "uint32_t"),
                                     std::to_string(index))});
        if (!read) {
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_cell_set"),
                                     mlir::ValueRange{cell.getResult(0), written});
            return mlir::Value{};
        }
        return ec::CallOpaqueOp::create(build, where, mlir::TypeRange{scope.value},
                                        callee("ct_aot_cell_get"),
                                        mlir::ValueRange{cell.getResult(0)})
            .getResult(0);
    }

    // A POINTER TO A RESERVED RUN OF FRAME SLOTS.
    //
    // Two helpers want one - ct_aot_call's argv and ct_aot_make_closure's
    // upvalue array - and both want it in the FRAME rather than in C++ locals,
    // because both are safepoints that can collect before they read it.
    mlir::Value window_pointer(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where, unsigned base) {
        auto span = ec::CallOpaqueOp::create(build, where,
                                             mlir::TypeRange{ec::PointerType::get(scope.value)},
                                             callee("ct_aot_slots"), mlir::ValueRange{scope.frame});
        auto first = ec::SubscriptOp::create(
            build, where, ec::LValueType::get(scope.value), span.getResult(0),
            mlir::ValueRange{literal(build, where, mlir::IntegerType::get(build.getContext(), 32),
                                     std::to_string(base))});
        return ec::AddressOfOp::create(build, where, ec::PointerType::get(scope.value),
                                       first.getResult())
            .getResult();
    }

    // A MACHINE QUANTITY, MADE INTO A JAVASCRIPT VALUE.
    //
    // Through one of the shims the module's prelude defines, because the ABI
    // has no row that boxes a bool or a double and `value::boolean(b).bits()`
    // is a member call on a temporary - which emitc.call_opaque, whose whole
    // output is `callee(args)`, cannot spell.
    mlir::Value box(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                    llvm::StringRef shim, mlir::Value machine) {
        // ONLY THE SHIMS A MODULE ACTUALLY USES ARE EMITTED. A translation unit
        // carrying a function nothing calls is dead code the host compiler will
        // warn about - and this project builds with -Werror, so emitting all of
        // them unconditionally would make the generated output unbuildable
        // under the same flags as everything else.
        if (shim == "ctc_box_bool") { boxes_bool = true; }
        if (shim == "ctc_box_number") { boxes_number = true; }
        return ec::CallOpaqueOp::create(build, where, mlir::TypeRange{value}, shim,
                                        mlir::ValueRange{machine})
            .getResult(0);
    }

    // A JavaScript `undefined`, spelled the way the runtime spells it.
    mlir::Value undefined(mlir::OpBuilder & build, mlir::Location where, mlir::Type value) {
        return literal(build, where, value, "ctbrowser::script::value::undefined().bits()");
    }

    // A CONSTANT, SPELLED THE WAY THE RUNTIME SPELLS IT.
    //
    // Through value::bits() rather than a bit pattern written here. The
    // encoding is script::value's - a NaN-boxing scheme it is free to change -
    // and a backend that baked the pattern would keep compiling and start
    // producing garbage the day it did.
    mlir::Value constant_value(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                               ConstantOp constant) {
        const mlir::Attribute what = constant.getValue();
        if (mlir::isa<NullAttr>(what)) {
            return literal(build, where, value, "ctbrowser::script::value::null().bits()");
        }
        if (auto boolean = mlir::dyn_cast<BooleanAttr>(what)) {
            return literal(build, where, value,
                           boolean.getValue() ? "ctbrowser::script::value::boolean(true).bits()"
                                              : "ctbrowser::script::value::boolean(false).bits()");
        }
        if (auto number = mlir::dyn_cast<NumberAttr>(what)) {
            // THE BITS, NOT A DECIMAL LITERAL, and that is what makes it exact.
            //
            // The attribute carries `bit_cast<uint64_t>(double)` - the double's
            // pattern, not the value's - because "-0.0 and NaN are the reason
            // for APFloat and for a dedicated attribute". Printing it back as
            // decimal would reintroduce exactly what the attribute exists to
            // avoid: 0.0 and -0.0 print identically and are different
            // JavaScript values, and no decimal spelling round-trips a NaN
            // payload. bit_cast reconstructs the double exactly, and
            // value::number boxes it however script::value chooses to - so
            // nothing here depends on the NaN-boxing scheme either.
            return literal(build, where, value,
                           "ctbrowser::script::value::number(std::bit_cast<double>(UINT64_C(" +
                               std::to_string(number.getBits()) + "))).bits()");
        }
        // body_is_supported admits nothing else.
        return undefined(build, where, value);
    }
};

} // namespace

} // namespace ctcompile::ctjs
