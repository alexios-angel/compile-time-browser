//===- Vectors.cpp - Emission of vectors of one proved element type ------===//
//
// Lowers arrays of Strings to `std::vector<ctnative::js_string>` and the
// runtime's built-in objects: a literal is a local vector its appends fill,
// `length` is vec_length, `a[i]` is vec_at, and push, join, sort and split
// are called with their JavaScript spelling,
// `ctnative::Array.prototype.push.call(a, x)` and
// `ctnative::String.prototype.split.call(s, sep)`.
// Everything decided here was decided by the proof and by admission; this
// file only spells it.
//
//===----------------------------------------------------------------------===//

#include "Vectors.h"

#include <array>
#include <limits>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// The attribute holding an operation's planned lowering.
constexpr llvm::StringLiteral kStringArrayAction = "ctnative.string_array";
constexpr llvm::StringLiteral kArrayHeader = "ctcompile/CTNative/Runtime/Array.hpp";

/// Adds `#include "Runtime/Array.hpp"` once, at the top of the module. The
/// runtime header and its defines are inserted in front of it later
/// (lowering::declareGlobals), and Array.hpp includes ctnative.hpp itself.
void includeArrayHeader(mlir::ModuleOp module) {
    for (auto include : module.getBody()->getOps<ec::IncludeOp>()) {
        if (include.getInclude() == kArrayHeader) { return; }
    }
    mlir::OpBuilder b(module.getContext());
    b.setInsertionPointToStart(module.getBody());
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(kArrayHeader), mlir::UnitAttr{});
}

} // namespace

llvm::DenseSet<mlir::Value> planStringArrays(lowering & lower, ctjs::FuncOp fn) {
    using VectorCall = TypeInference::VectorCall;
    llvm::DenseSet<mlir::Value> arrays;
    const auto consider = [&](mlir::Value v) {
        if (isStringArray(v, lower.typeOf(v))) { arrays.insert(v); }
    };
    for (mlir::Block & block : fn.getBody()) {
        for (mlir::BlockArgument argument : block.getArguments()) { consider(argument); }
    }
    fn.getBody().walk([&](mlir::Operation * op) {
        for (mlir::Value result : op->getResults()) { consider(result); }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) { consider(argument); }
            }
        }
    });
    const auto mark = [&](mlir::Operation * op, llvm::StringRef action) {
        op->setAttr(kStringArrayAction, mlir::StringAttr::get(op->getContext(), action));
    };
    fn.getBody().walk([&](mlir::Operation * op) {
        if (isVectorCallPlumbing(op)) {
            mark(op, "plumbing");
            return;
        }
        if (auto literal = llvm::dyn_cast<ctjs::CreateArrayOp>(op)) {
            if (arrays.contains(literal.getResult())) { mark(op, "create"); }
            return;
        }
        if (auto append = llvm::dyn_cast<ctjs::AppendOp>(op)) {
            if (arrays.contains(append.getArray())) { mark(op, "append"); }
            return;
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            if (!arrays.contains(read.getObject())) { return; }
            mark(op, ctjs::constantKey(read.getKey()) == "length" ? "length" : "at");
            return;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        if (!call) { return; }
        switch (TypeInference::vectorCallOf(call)) {
        case VectorCall::push:
            if (arrays.contains(call.getReceiver())) { mark(op, "push"); }
            break;
        case VectorCall::join:
            if (arrays.contains(call.getReceiver())) { mark(op, "join"); }
            break;
        case VectorCall::sort:
            if (arrays.contains(call.getReceiver())) { mark(op, "sort"); }
            break;
        case VectorCall::split: mark(op, "split"); break;
        case VectorCall::splitSpace: mark(op, "split_space"); break;
        case VectorCall::regexp:
        case VectorCall::none: break;
        }
    });
    return arrays;
}

bool replaceStringArray(lowering & lower, mlir::Operation * o) {
    auto action = o->getAttrOfType<mlir::StringAttr>(kStringArrayAction);
    if (!action) { return false; }
    includeArrayHeader(lower.module);
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto swap = [&](mlir::Value with) {
        o->getResult(0).replaceAllUsesWith(with);
        lowering::eraseIfUnused(o);
    };
    const auto place = [&](mlir::Value array) { return lowering::cellPlace(b, where, array); };
    const auto numeric = carrierType(context, carrier::number);
    const auto vectorValue = ec::OpaqueType::get(context, kStringArrayType);
    const llvm::StringRef kind = action.getValue();
    if (kind == "plumbing") {
        // A method read or RegExp literal the call below it consumes: a dead
        // placeholder, as for an erased property-key constant.
        swap(lower.numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return true;
    }
    if (kind == "create") {
        // The vector, by value, in this frame (or in the region that made
        // it, when nothing carries it out), filled by the appends below.
        auto literal = llvm::cast<ctjs::CreateArrayOp>(o);
        mlir::Value local = ec::VariableOp::create(b, where, literal.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        for (mlir::Value element : literal.getElements()) { lower.push(b, where, local, element); }
        swap(local);
        return true;
    }
    if (kind == "append") {
        auto append = llvm::cast<ctjs::AppendOp>(o);
        lower.push(b, where, place(append.getArray()), append.getElement());
        lowering::eraseIfUnused(o);
        return true;
    }
    if (kind == "length") {
        swap(callValue(b, where, numeric, "ctnative::vec_length", place(o->getOperand(0))));
        return true;
    }
    if (kind == "at") {
        const mlir::Type result = o->getResult(0).getType();
        const mlir::Value value =
            callValue(b, where, carrierType(context, carrier::nullableString), "ctnative::vec_at",
                      mlir::ValueRange{place(o->getOperand(0)), o->getOperand(1)});
        swap(lower.convertScalar(b, where, value, result));
        return true;
    }
    auto call = llvm::cast<ctjs::CallOp>(o);
    if (kind == "push") {
        const std::array<mlir::Value, 2> operands{place(call.getReceiver()), call.getArgs()[0]};
        if (call.getResult().use_empty()) {
            // No result, so no unused variable under -Werror (see lowering::push).
            callWithConstValueOperands(b, where, mlir::TypeRange{},
                                       b.getStringAttr("ctnative::Array.prototype.push.call"),
                                       operands);
            lowering::eraseIfUnused(o);
        } else {
            swap(callValue(b, where, numeric, "ctnative::Array.prototype.push.call", operands));
        }
        return true;
    }
    if (kind == "join") {
        // join() with no argument joins with "," (ECMA-262 23.1.3.18 step 3).
        const mlir::Value separator =
            call.getArgs().empty() ? lower.stringConstant(b, where, ",") : call.getArgs()[0];
        swap(callValue(b, where, carrierType(context, carrier::string),
                       "ctnative::Array.prototype.join.call",
                       mlir::ValueRange{place(call.getReceiver()), separator}));
        return true;
    }
    if (kind == "sort") {
        // `a.sort()` sorts in place and answers `a`.
        const mlir::Value sorted = place(call.getReceiver());
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::Array.prototype.sort.call"),
                                   mlir::ValueRange{sorted});
        swap(sorted);
        return true;
    }
    if (kind == "split" || kind == "split_space") {
        mlir::Value local = ec::VariableOp::create(b, where, call.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        // The recognised /\s+/ separator is the runtime's tag for it.
        const mlir::Value separator =
            kind == "split"
                ? mlir::Value(call.getArgs()[0])
                : mlir::Value(ec::LiteralOp::create(
                      b, where, ec::OpaqueType::get(context, "ctnative::whitespace_run_t"),
                      "ctnative::whitespace_run"));
        const mlir::Value pieces =
            callValue(b, where, vectorValue, "ctnative::String.prototype.split.call",
                      mlir::ValueRange{call.getReceiver(), separator});
        ec::AssignOp::create(b, where, local, pieces);
        swap(local);
        return true;
    }
    llvm::report_fatal_error(llvm::Twine("ctnative lowering: unknown string array action `") +
                             kind + "`");
}

} // namespace ctcompile::ctnative::lowering_detail
