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

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/DenseSet.h"
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

/// Whether a table may hold values of `type`: a scalar storable() admits, or
/// tables. A table of tables may hold undefined, which its null pointer
/// stores as an entry, so `in` and a missing key still tell it apart.
bool storableValue(mlir::Type type) {
    return storable(carrierOf(type)) || carrierOf(type) == carrier::dictionary;
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

/// What a key can be, as far as inheritedKeyProblem proves it: the String
/// constants it may equal, or why it may be anything.
struct keyDomain {
    llvm::SmallVector<std::string> constants;
    std::string problem;
};

/// Whether some part of a concatenation is a literal with a character that
/// no name Object.prototype answers for has.
bool marked(mlir::Value key) {
    llvm::SmallVector<mlir::Value> parts{key};
    for (std::size_t at = 0; at < parts.size() && parts.size() < 64; ++at) {
        if (auto text = ctjs::constantAttr<ctjs::StringAttr>(parts[at]);
            text && hasNonNameCharacter(text.getValue())) {
            return true;
        }
        auto add = parts[at].getDefiningOp<ctjs::BinaryOp>();
        if (add && add.getKind() == ctjs::BinaryKind::Add) {
            parts.push_back(add.getLhs());
            parts.push_back(add.getRhs());
        }
    }
    return false;
}

/// Adds what `key` can be to `domain`, following it back through
/// parameters (every direct call's argument), `if` results and local
/// variables to its sources. A source is proved when it is a Number, a
/// literal, or a concatenation marked() proves; anything else sets
/// `domain.problem`. `seen` stops a cycle, which adds no new source.
void collectKeys(admission & self, mlir::Value key, keyDomain & domain,
                 llvm::DenseSet<mlir::Value> & seen) {
    if (!domain.problem.empty() || !seen.insert(key).second) { return; }
    if (seen.size() > 256) {
        domain.problem = "a key whose sources are too many to follow";
        return;
    }
    if (carrierOf(self.typeOf(key)) == carrier::number || marked(key)) { return; }
    if (auto text = ctjs::constantAttr<ctjs::StringAttr>(key)) {
        if (admission::namesObjectPrototypeMember(text.getValue())) {
            domain.problem = ("`" + text.getValue() + "`, which Object.prototype answers").str();
        } else if (!llvm::is_contained(domain.constants, text.getValue())) {
            domain.constants.push_back(text.getValue().str());
        }
        return;
    }
    if (auto choice = key.getDefiningOp<mlir::scf::IfOp>(); choice && choice.elseBlock()) {
        const unsigned index = llvm::cast<mlir::OpResult>(key).getResultNumber();
        collectKeys(self, choice.thenYield().getOperand(index), domain, seen);
        collectKeys(self, choice.elseYield().getOperand(index), domain, seen);
        return;
    }
    if (auto read = key.getDefiningOp<ctjs::CellGetOp>()) {
        bool written = false;
        for (mlir::Operation * user : read.getCell().getUsers()) {
            if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user);
                write && write.getCell() == read.getCell()) {
                written = true;
                collectKeys(self, write.getValue(), domain, seen);
            }
        }
        if (!written) { domain.problem = "a key read from a variable nothing assigns"; }
        return;
    }
    if (auto parameter = llvm::dyn_cast<mlir::BlockArgument>(key)) {
        auto function = llvm::dyn_cast<ctjs::FuncOp>(parameter.getOwner()->getParentOp());
        bool called = false;
        if (function && parameter.getOwner()->isEntryBlock() && parameter.getArgNumber() >= 3) {
            function->getParentOfType<mlir::ModuleOp>().walk([&](ctjs::CallDirectOp call) {
                if (call.getTarget() != function ||
                    parameter.getArgNumber() >= call->getNumOperands()) {
                    return;
                }
                called = true;
                collectKeys(self, call->getOperand(parameter.getArgNumber()), domain, seen);
            });
        }
        if (!called) { domain.problem = "a key parameter no direct call passes"; }
        return;
    }
    domain.problem = "a computed key that is not a Number, a literal or a concatenation with a "
                     "literal part holding a character Object.prototype's names lack, so it "
                     "could be `toString` or `__proto__`";
}

/// Whether ToPropertyKey of `key` is provably never a name Object.prototype
/// answers for (admission::namesObjectPrototypeMember, ECMA-262 20.1.3 and
/// B.2.2), so a read of a key the table lacks is undefined, as in
/// JavaScript. The key is followed back to its sources (collectKeys); each
/// must be a Number (its text is digits, a sign, a point and an exponent, or
/// `NaN` or `Infinity`), a String literal that is not such a name, or a
/// concatenation with a literal part that has a character no such name has,
/// such as EventHandler's `${type}::${uid}`. Those names are letters and
/// underscores only. When literals were among the sources, the read records
/// them in ctnative.dict_key_constants, so a reader sees what was proved.
/// The reason is empty when proved, else what the key is.
std::string inheritedKeyProblem(admission & self, mlir::Operation * read, mlir::Value key) {
    keyDomain domain;
    llvm::DenseSet<mlir::Value> seen;
    collectKeys(self, key, domain, seen);
    if (!domain.problem.empty()) { return domain.problem; }
    if (!domain.constants.empty()) {
        llvm::SmallVector<mlir::Attribute> names;
        for (const std::string & name : domain.constants) {
            names.push_back(mlir::StringAttr::get(read->getContext(), name));
        }
        read->setAttr("ctnative.dict_key_constants",
                      mlir::ArrayAttr::get(read->getContext(), names));
    }
    return {};
}

} // namespace

std::optional<bool> dictionaryOperation(admission & self, mlir::Operation * o) {
    if (isNativeDictionarySite(o)) {
        const mlir::Type type = self.typeOf(o->getResult(0));
        auto table = llvm::dyn_cast_or_null<DictType>(type);
        if (!table || !storableValue(table.getElementType())) {
            return self.refuse("an object used as a table whose values are " +
                               printed(table ? table.getElementType() : type) +
                               ", not one of Number, String, Boolean, Number|String or a table");
        }
        return true;
    }
    // A literal the proof declined (kNativeDictionaryReason says why) is not
    // a table; the record rules admit or refuse it with their own reason.
    const llvm::StringRef action = nativeDictionaryAction(o);
    if (action.empty()) { return std::nullopt; }
    if (action == "plumbing" || action == "object" || action == "has" || action == "absent") {
        return true;
    }
    if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(o)) {
        return spelledKey(self, set.getKey(), "writes");
    }
    if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(o)) {
        if (!spelledKey(self, get.getKey(), "reads")) { return false; }
        // A key the table lacks reads what Object.prototype has under it,
        // which no V holds; dictionary_get stops the program if the proof
        // below was ever wrong.
        if (const std::string problem = inheritedKeyProblem(self, o, get.getKey());
            !problem.empty()) {
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
