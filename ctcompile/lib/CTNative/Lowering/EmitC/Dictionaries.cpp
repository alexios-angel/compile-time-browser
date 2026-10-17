//===- Dictionaries.cpp - Emission of objects used as tables --------------===//
//
// Each operation of a proved table (Analysis/NativeDictionary.h) becomes one
// call of its Runtime/Dictionary.hpp helper, which does what the JavaScript
// operation does:
//
//   {}             ctnative::make_dictionary<V>()
//   o[k]           ctnative::dictionary_get(o, k)
//   o[k] = v       ctnative::dictionary_set(o, k, v)
//   delete o[k]    ctnative::dictionary_delete(o, k)
//   k in o         ctnative::dictionary_has(o, k)
//   t === undefined ctnative::dictionary_absent(t)
//
// Keys go to the helpers as their own carriers; the helpers apply
// ToPropertyKey.
//
//===----------------------------------------------------------------------===//

#include "Dictionaries.h"

#include <limits>

namespace ctcompile::ctnative::lowering_detail {

bool replaceDictionary(lowering & lower, mlir::Operation * o) {
    const llvm::StringRef action = nativeDictionaryAction(o);
    const bool site = isNativeDictionarySite(o);
    if (action.empty() && !site) { return false; }
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto swap = [&](mlir::Value with) {
        o->getResult(0).replaceAllUsesWith(with);
        lowering::eraseIfUnused(o);
    };
    const auto call = [&](llvm::StringRef callee, mlir::ValueRange operands) {
        return callValue(b, where, o->getResult(0).getType(), callee, operands);
    };
    if (site) {
        auto table = llvm::cast<DictType>(lower.typeOf(o->getResult(0)));
        swap(call("ctnative::make_dictionary<" + dictionaryValueSpelling(table) + ">", {}));
        return true;
    }
    if (action == "plumbing") {
        // A read of an intrinsic only a replaced call used: no runtime effect,
        // so any constant will do until it is erased with that call.
        swap(lower.numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return true;
    }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(o)) {
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::dictionary_set"),
                                   mlir::ValueRange{set.getObject(), set.getKey(), set.getValue()});
        o->erase();
        return true;
    }
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(o)) {
        swap(call("ctnative::dictionary_get", {get.getObject(), get.getKey()}));
        return true;
    }
    if (auto has = llvm::dyn_cast<ctjs::HasPropertyOp>(o)) {
        swap(call("ctnative::dictionary_has", {has.getObject(), has.getKey()}));
        return true;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(o)) {
        // `t === undefined` or `t == undefined` of a table read.
        // The operands are emitted values by now; the table is the one
        // spelled as a table.
        const mlir::Value table =
            isDictionaryCarrier(compare.getLhs().getType()) ? compare.getLhs() : compare.getRhs();
        swap(call("ctnative::dictionary_absent", {table}));
        return true;
    }
    auto intrinsic = llvm::cast<ctjs::CallOp>(o);
    if (action == "delete") {
        swap(call("ctnative::dictionary_delete", {intrinsic.getArgs()[0], intrinsic.getArgs()[1]}));
        return true;
    }
    // The receiver check of `in` and its ToPropertyKey both answer their
    // operand: the table is an object, and dictionary_has converts the key.
    swap(intrinsic.getArgs()[0]);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
