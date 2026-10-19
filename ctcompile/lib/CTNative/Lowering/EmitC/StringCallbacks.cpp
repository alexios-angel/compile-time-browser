//===- StringCallbacks.cpp - Emission of replace with a function ----------===//
//
// Spells a `text.replace(/pattern/g, fn)` call closure lifting specialised
// with its JavaScript name, the RegExp literal and the lifted callback bound
// to its captures:
//
//   ctnative::String.prototype.replace.call(text, ctnative::RegExp("[A-Z]", "g"),
//                                           std::bind_front(callback, c))
//
// `text.toLowerCase()`, `text.toUpperCase()` and `text.trim()` on a String
// become `ctnative::String.prototype.toLowerCase.call(text)` and the like.
//
// The method read and the RegExp literal the call consumed lower to nothing:
// replace() skips them, and they are erased here once the call is rewritten.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"
#include "ctcompile/Support/CppLiterals.hpp"

#include "llvm/ADT/SetVector.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {
constexpr llvm::StringLiteral kRegExpHeader = "ctcompile/CTNative/Runtime/RegExp.hpp";
/// The literal's pattern and flags, kept by ClosureLifting/StringCallbacks.cpp
/// for a function replacement and by TypeInference::markStringMethodCalls for
/// any other.
constexpr llvm::StringLiteral kPattern = "ctnative.replace_pattern";
constexpr llvm::StringLiteral kFlags = "ctnative.replace_flags";

/// Adds `#include "Runtime/RegExp.hpp"` once, at the top of the module, so
/// only programs that call replace parse <regex>.
void includeRegExpHeader(mlir::ModuleOp module) {
    for (auto include : module.getBody()->getOps<ec::IncludeOp>()) {
        if (include.getInclude() == kRegExpHeader) { return; }
    }
    mlir::OpBuilder b(module.getContext());
    b.setInsertionPointToStart(module.getBody());
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(kRegExpHeader), mlir::UnitAttr{});
}

/// `ctnative::RegExp("pattern", "flags")` for the literal a replace call
/// `o` consumed, whose source its kPattern and kFlags keep.
mlir::Value regExp(mlir::OpBuilder & b, mlir::Location where, mlir::Operation * o) {
    mlir::MLIRContext * context = o->getContext();
    const llvm::StringRef pattern = o->getAttrOfType<mlir::StringAttr>(kPattern).getValue();
    const llvm::StringRef flags = o->getAttrOfType<mlir::StringAttr>(kFlags).getValue();
    const auto text = ec::PointerType::get(ec::OpaqueType::get(context, "const char"));
    return callValue(
        b, where, ec::OpaqueType::get(context, "ctnative::reg_exp"), "ctnative::RegExp",
        mlir::ValueRange{ec::LiteralOp::create(b, where, text, cpp::c_string_literal(pattern)),
                         ec::LiteralOp::create(b, where, text, cpp::c_string_literal(flags))});
}
} // namespace

bool lowering::replaceStringCallback(mlir::Operation * o) {
    if (TypeInference::consumedByStringCallback(o)) { return true; }
    if (auto group = o->getAttrOfType<ctjs::NumberAttr>(kNativeMatchGroup)) {
        // ctnative::String.prototype.match.call(text, ctnative::RegExp(...)).group(k);
        // the match, its method read, the literal and its global go.
        includeRegExpHeader(o->getParentOfType<mlir::ModuleOp>());
        auto match = llvm::cast<ctjs::GetPropertyOp>(o).getObject().getDefiningOp<ctjs::CallOp>();
        mlir::OpBuilder b(o);
        const mlir::Location where = o->getLoc();
        const mlir::Value found = callValue(
            b, where, ec::OpaqueType::get(context, "ctnative::match_result"),
            "ctnative::String.prototype.match.call",
            mlir::ValueRange{cellPlace(b, where, match.getReceiver()), regExp(b, where, o)});
        const mlir::Value index =
            ec::LiteralOp::create(b, where, ec::OpaqueType::get(context, "std::size_t"),
                                  std::to_string(static_cast<unsigned>(group.getDouble())));
        o->getResult(0).replaceAllUsesWith(
            memberCall(b, where, o->getResult(0).getType(), found, "group", index));
        mlir::Operation * read = match.getCallee().getDefiningOp();
        mlir::Operation * literal = match.getArgs()[0].getDefiningOp();
        o->erase();
        eraseIfUnused(match);
        if (read) { eraseIfUnused(read); }
        if (literal) {
            mlir::Operation * global = literal->getOperand(0).getDefiningOp();
            eraseIfUnused(literal);
            if (global) { eraseIfUnused(global); }
        }
        return true;
    }
    if (TypeInference::isStringMethodCall(o)) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        mlir::OpBuilder b(o);
        mlir::Operation * read = call.getCallee().getDefiningOp();
        const llvm::StringRef name = o->getAttrOfType<mlir::StringAttr>(kNativeStringMethod);
        if (name == "test") {
            // ctnative::RegExp.prototype.test.call(ctnative::RegExp(...), text);
            // the RegExp the call consumed and its global go.
            includeRegExpHeader(o->getParentOfType<mlir::ModuleOp>());
            mlir::Operation * made = call.getReceiver().getDefiningOp();
            o->getResult(0).replaceAllUsesWith(callValue(
                b, o->getLoc(), o->getResult(0).getType(), "ctnative::RegExp.prototype.test.call",
                mlir::ValueRange{regExp(b, o->getLoc(), o),
                                 cellPlace(b, o->getLoc(), call.getArgs()[0])}));
            eraseIfUnused(o);
            eraseIfUnused(read);
            llvm::SmallSetVector<mlir::Operation *, 2> globals;
            for (mlir::Value operand : made->getOperands()) {
                if (auto global = operand.getDefiningOp<ctjs::LoadGlobalOp>()) {
                    globals.insert(global);
                }
            }
            eraseIfUnused(made);
            for (mlir::Operation * global : globals) { eraseIfUnused(global); }
            return true;
        }
        llvm::SmallVector<mlir::Value, 3> operands{cellPlace(b, o->getLoc(), call.getReceiver())};
        mlir::Operation * literal = nullptr;
        if (name == "replace") {
            // The RegExp literal, then the String replacement.
            includeRegExpHeader(o->getParentOfType<mlir::ModuleOp>());
            operands.push_back(regExp(b, o->getLoc(), o));
            operands.push_back(cellPlace(b, o->getLoc(), call.getArgs()[1]));
            literal = call.getArgs()[0].getDefiningOp();
        }
        o->getResult(0).replaceAllUsesWith(
            callValue(b, o->getLoc(), o->getResult(0).getType(),
                      ("ctnative::String.prototype." + name + ".call").str(), operands));
        eraseIfUnused(o);
        eraseIfUnused(read);
        if (literal) {
            mlir::Operation * global = literal->getOperand(0).getDefiningOp();
            eraseIfUnused(literal);
            if (global) { eraseIfUnused(global); }
        }
        return true;
    }
    if (!TypeInference::isStringCallbackCall(o)) { return false; }
    auto call = llvm::cast<ctjs::CallOp>(o);
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    includeRegExpHeader(o->getParentOfType<mlir::ModuleOp>());
    mlir::Value literal = regExp(b, where, o);
    const llvm::StringRef symbol =
        o->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeStringCallback).getValue();
    const auto named = names.find(symbol);
    const std::string callback = named == names.end() ? cIdentifier(symbol) : named->second;
    const auto any = ec::OpaqueType::get(context, "auto");
    mlir::Value bound = ec::LiteralOp::create(b, where, any, callback);
    llvm::SmallVector<mlir::Value> captured{bound};
    for (mlir::Value capture : call.getArgs().drop_front()) {
        captured.push_back(cellPlace(b, where, capture));
    }
    if (captured.size() > 1) { bound = callValue(b, where, any, "std::bind_front", captured); }
    mlir::Value replaced =
        callValue(b, where, o->getResult(0).getType(), "ctnative::String.prototype.replace.call",
                  mlir::ValueRange{cellPlace(b, where, call.getReceiver()), literal, bound});
    // The consumed method read, RegExp literal and its factory's global.
    llvm::SmallVector<mlir::Operation *, 3> consumed;
    if (auto * read = call.getCallee().getDefiningOp()) { consumed.push_back(read); }
    if (auto * made = call.getArgs()[0].getDefiningOp()) {
        consumed.push_back(made);
        if (auto * global = made->getOperand(0).getDefiningOp()) { consumed.push_back(global); }
    }
    o->getResult(0).replaceAllUsesWith(replaced);
    eraseIfUnused(o);
    for (mlir::Operation * op : consumed) { eraseIfUnused(op); }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
