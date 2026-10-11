//===- StringCallbacks.cpp - Emission of replace with a function ----------===//
//
// Spells a `text.replace(/pattern/g, fn)` call closure lifting specialised
// with its JavaScript name, the RegExp literal and the lifted callback bound
// to its captures:
//
//   ctnative::String.prototype.replace.call(text, ctnative::RegExp("[A-Z]", "g"),
//                                           std::bind_front(callback, c))
//
// `text.toLowerCase()` on a String becomes
// `ctnative::String.prototype.toLowerCase.call(text)`.
//
// The method read and the RegExp literal the call consumed lower to nothing:
// replace() skips them, and they are erased here once the call is rewritten.
//
//===----------------------------------------------------------------------===//

#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {
constexpr llvm::StringLiteral kRegExpHeader = "ctcompile/CTNative/Runtime/RegExp.hpp";
/// The literal's pattern and flags, kept by ClosureLifting/StringCallbacks.cpp.
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

/// `text` as a C++ string literal. The pattern is ASCII (admission refuses
/// any other), so only the quote and the backslash need escaping.
std::string quoted(llvm::StringRef text) {
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"' || ch == '\\') { out.push_back('\\'); }
        out.push_back(ch);
    }
    return out + "\"";
}
} // namespace

bool lowering::replaceStringCallback(mlir::Operation * o) {
    if (TypeInference::consumedByStringCallback(o)) { return true; }
    if (TypeInference::isStringMethodCall(o)) {
        auto call = llvm::cast<ctjs::CallOp>(o);
        mlir::OpBuilder b(o);
        mlir::Operation * read = call.getCallee().getDefiningOp();
        o->getResult(0).replaceAllUsesWith(
            callValue(b, o->getLoc(), o->getResult(0).getType(),
                      "ctnative::String.prototype.toLowerCase.call",
                      mlir::ValueRange{cellPlace(b, o->getLoc(), call.getReceiver())}));
        eraseIfUnused(o);
        eraseIfUnused(read);
        return true;
    }
    if (!TypeInference::isStringCallbackCall(o)) { return false; }
    auto call = llvm::cast<ctjs::CallOp>(o);
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    includeRegExpHeader(o->getParentOfType<mlir::ModuleOp>());
    const llvm::StringRef pattern = o->getAttrOfType<mlir::StringAttr>(kPattern).getValue();
    const llvm::StringRef flags = o->getAttrOfType<mlir::StringAttr>(kFlags).getValue();
    const auto text = ec::PointerType::get(ec::OpaqueType::get(context, "const char"));
    mlir::Value literal =
        callValue(b, where, ec::OpaqueType::get(context, "ctnative::reg_exp"), "ctnative::RegExp",
                  mlir::ValueRange{ec::LiteralOp::create(b, where, text, quoted(pattern)),
                                   ec::LiteralOp::create(b, where, text, quoted(flags))});
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
