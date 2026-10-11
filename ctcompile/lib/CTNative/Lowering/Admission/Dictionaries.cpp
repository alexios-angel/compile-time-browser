//===- Admission/Dictionaries.cpp - Objects used as tables ----------------===//
//
// Admits the operations of an object literal prepareNativeDictionaries proved
// is used only as a table (Analysis/NativeDictionary.h). That proof says
// nothing but table operations see the object; this file checks what the
// native table (Runtime/Dictionary.hpp) can hold: one value type with a
// carrier, never undefined, and keys whose ToPropertyKey the runtime spells.
//
//===----------------------------------------------------------------------===//

#include "Admission.h"

#include "llvm/ADT/StringExtras.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether a value of this carrier can be stored in a table. A value that
/// may be undefined is not: a read of it could not tell a stored undefined
/// from a missing key, which `in` and Object.keys do tell apart.
bool storable(carrier c) {
    return c == carrier::number || c == carrier::string || c == carrier::boolean ||
           c == carrier::numberString;
}

/// Whether a key of this type has a ToPropertyKey the runtime spells
/// (property_key): a String, a Number or a Number|String.
bool spelledKey(admission & self, mlir::Value key, llvm::StringRef what) {
    const mlir::Type type = self.typeOf(key);
    if (llvm::isa_and_nonnull<SymbolType>(type)) {
        return self.refuse(
            ("a Symbol key " + what + " a table - a native table keys by text").str());
    }
    const carrier c = carrierOf(type);
    return c == carrier::string || c == carrier::number || c == carrier::numberString ||
           self.refuse(("a key of type " + printed(type) + " " + what +
                        " a table, not a String or a Number")
                           .str());
}

/// Whether text of this shape can never be a name Object.prototype answers
/// for (admission::namesObjectPrototypeMember, ECMA-262 20.1.3 and B.2.2).
/// Every such name is made of letters and underscores only, so text with any
/// other character in it is never one.
bool hasNonNameCharacter(llvm::StringRef text) {
    return llvm::any_of(text, [](char c) { return !llvm::isAlpha(c) && c != '_'; });
}

/// Whether ToPropertyKey of `key` is provably never a name Object.prototype
/// answers for, so a read of a key the table lacks is undefined, as in
/// JavaScript. Three shapes are proved:
///   - a Number: its text is digits, a sign, a point and an exponent, or
///     `NaN` or `Infinity`, none of them such a name;
///   - a String literal that is not such a name;
///   - a concatenation with a literal part that has a character no such name
///     has, such as EventHandler's `${type}::${uid}` or a namespaced
///     `click.bs.modal`.
/// The reason is empty when proved, else what the key is.
std::string inheritedKeyProblem(admission & self, mlir::Value key) {
    if (carrierOf(self.typeOf(key)) == carrier::number) { return {}; }
    if (auto text = ctjs::constantAttr<ctjs::StringAttr>(key)) {
        return admission::namesObjectPrototypeMember(text.getValue())
                   ? ("`" + text.getValue() + "`, which Object.prototype answers").str()
                   : std::string{};
    }
    llvm::SmallVector<mlir::Value> parts{key};
    for (std::size_t at = 0; at < parts.size() && parts.size() < 64; ++at) {
        if (auto text = ctjs::constantAttr<ctjs::StringAttr>(parts[at]);
            text && hasNonNameCharacter(text.getValue())) {
            return {};
        }
        auto add = parts[at].getDefiningOp<ctjs::BinaryOp>();
        if (add && add.getKind() == ctjs::BinaryKind::Add) {
            parts.push_back(add.getLhs());
            parts.push_back(add.getRhs());
        }
    }
    return "a computed key that is not a Number and has no literal part with a character "
           "Object.prototype's names lack, so it could be `toString` or `__proto__`";
}

} // namespace

std::optional<bool> dictionaryOperation(admission & self, mlir::Operation * o) {
    if (isNativeDictionarySite(o)) {
        const mlir::Type type = self.typeOf(o->getResult(0));
        auto table = llvm::dyn_cast_or_null<DictType>(type);
        if (!table || !storable(carrierOf(table.getElementType()))) {
            return self.refuse("an object used as a table whose values are " +
                               printed(table ? table.getElementType() : type) +
                               ", not one of Number, String, Boolean or Number|String");
        }
        return true;
    }
    if (auto reason = o->getAttrOfType<mlir::StringAttr>(kNativeDictionaryReason)) {
        return self.refuse(reason.getValue().str());
    }
    const llvm::StringRef action = nativeDictionaryAction(o);
    if (action.empty()) { return std::nullopt; }
    if (action == "plumbing" || action == "object" || action == "has") { return true; }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(o)) {
        return spelledKey(self, set.getKey(), "writes");
    }
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(o)) {
        if (!spelledKey(self, get.getKey(), "reads")) { return false; }
        // A key the table lacks reads what Object.prototype has under it,
        // which no V holds; dictionary_get stops the program if the proof
        // below was ever wrong.
        if (const std::string problem = inheritedKeyProblem(self, get.getKey()); !problem.empty()) {
            return self.refuse("a read of a table under " + problem);
        }
        return true;
    }
    auto call = llvm::cast<ctjs::CallOp>(o);
    // `delete o[k]` passes the key second; ToPropertyKey of an `in` key, first.
    return spelledKey(self, call.getArgs()[action == "delete" ? 1 : 0],
                      action == "delete" ? "deletes from" : "tests in");
}

} // namespace ctcompile::ctnative::lowering_detail
