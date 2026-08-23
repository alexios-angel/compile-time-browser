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
        if (mlir::isa<FrameEnterOp, FrameExitOp, ReturnOp, TruthyOp>(op)) { return; }
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
            if (mlir::isa<UndefinedAttr, NullAttr, BooleanAttr, NumberAttr>(constant.getValue())) {
                return;
            }
            supported = false;
            why = "no lowering yet for this constant - a string reaches ct_aot_new_string, which "
                  "is a safepoint, and nothing roots the result yet";
            return;
        }
        supported = false;
        why = ("no lowering yet for " + op->getName().getStringRef()).str();
    });
    return supported;
}

struct CTJSLowerToEmitCPass : impl::CTJSLowerToEmitCBase<CTJSLowerToEmitCPass> {
    using CTJSLowerToEmitCBase::CTJSLowerToEmitCBase;

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
            ec::IncludeOp::create(build, module.getLoc(),
                                  build.getStringAttr("ctbrowser/aot/aot.hpp"),
                                  /*is_standard_include=*/build.getUnitAttr());
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
        if (!body.getArgument(arg_callee).use_empty()) {
            refuse(function, "uses the callee, and ct_aot_callee has no implementation");
            return false;
        }

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

        // ---- the frame ----------------------------------------------------
        //
        // CT_AOT_FRAME_BYTES OF CALLER-ALLOCATED SPACE, sized from the macro
        // the runtime's own header defines rather than from a number written
        // here. The array is passed directly: C++ decays it to `unsigned char *`
        // and converts that to the `void *` the row declares, which needs no
        // subscript, no address-of, and no size_t - and !emitc.size_t emits a
        // bare `size_t` that does not compile.
        const auto block_type = ec::ArrayType::get({static_cast<std::int64_t>(CT_AOT_FRAME_BYTES)},
                                                   opaque(context, "unsigned char"));
        auto storage =
            ec::VariableOp::create(build, where, block_type, ec::OpaqueAttr::get(context, ""));
        const mlir::Value registers =
            literal(build, where, u32, std::to_string(entered.getRegCount()));
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
        mlir::IRMapping mapping;
        mapping.map(body.getArgument(arg_receiver), in_receiver);
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
        for (mlir::Block & block : function.getBody()) {
            build.setInsertionPointToEnd(mapping.lookup(&block));
            for (mlir::Operation & op : block) {
                convert(op, build, mapping, value, in_receiver, in_constructing, in_out, status);
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
    void convert(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                 mlir::Type value, mlir::Value receiver, mlir::Value constructing, mlir::Value out,
                 mlir::Type status) {
        const mlir::Location where = op.getLoc();

        // The frame is established by the entry block, not by this operation.
        if (mlir::isa<FrameEnterOp>(op)) { return; }

        if (auto exit = mlir::dyn_cast<FrameExitOp>(op)) {
            ec::CallOpaqueOp::create(build, where, mlir::TypeRange{}, callee("ct_aot_leave"),
                                     mlir::ValueRange{mapping.lookup(exit.getContext())});
            return;
        }

        if (auto constant = mlir::dyn_cast<ConstantOp>(op)) {
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

        llvm_unreachable("body_is_supported and convert disagree about what is supported");
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
