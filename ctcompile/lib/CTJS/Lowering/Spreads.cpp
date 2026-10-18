//===- Spreads.cpp - Expand object spreads of known shape -----------------===//
//
// Object spread in a literal, `{...a, b: 1}`, imports as a fresh
// ctjs.create_object followed by ctjs.copy_props, which copies whatever own
// keys the source has at run time. Native lowering gives an object literal a
// C++ struct only when every key it can hold is a constant, so a copy whose
// keys are unknown leaves the target's shape open. When the source is itself a
// literal whose keys are all written in plain sight before the copy, the copy
// is just those keys, and writing them out lets the existing closed-shape
// analysis see the target's fields.
//
//===----------------------------------------------------------------------===//
#include "Spreads.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"

namespace ctcompile::ctjs {

namespace {

/// True for a canonical array index ("0", "17"): such a key is enumerated
/// before every string key (OrdinaryOwnPropertyKeys, ECMA-262 10.1.11.1), so
/// write order is not property order.
bool arrayIndexKey(llvm::StringRef key) {
    return !key.empty() && llvm::all_of(key, llvm::isDigit) && (key == "0" || key[0] != '0');
}

/// The key operands of \p source's own properties at \p copy, in property
/// order, or nullopt when they are not all known.
std::optional<llvm::SmallVector<mlir::Value>> knownKeys(CreateObjectOp source, CopyPropsOp copy) {
    mlir::Block * block = copy->getBlock();
    if (source->getBlock() != block) { return std::nullopt; }
    llvm::SmallVector<std::pair<mlir::Operation *, mlir::Value>> writes;
    for (mlir::OpOperand & use : source.getResult().getUses()) {
        mlir::Operation * user = block->findAncestorOpInBlock(*use.getOwner());
        // A use after the copy, nested or not, cannot change what it copies.
        if (user && (user == copy || copy->isBeforeInBlock(user))) { continue; }
        // Before the copy: only constant-key reads and writes on the source
        // itself, directly in this block, so every write runs once and no
        // other name for the object exists that could add a key.
        if (!user || user != use.getOwner() || use.getOperandNumber() != 0) { return std::nullopt; }
        const mlir::Value key = propertyKey(user);
        if (!key || !ordinaryKey(key) || arrayIndexKey(constantKey(key))) { return std::nullopt; }
        if (llvm::isa<SetPropertyOp>(user)) { writes.emplace_back(user, key); }
    }
    llvm::sort(writes,
               [](const auto & a, const auto & b) { return a.first->isBeforeInBlock(b.first); });
    llvm::SmallVector<mlir::Value> keys;
    llvm::StringSet<> seen;
    for (const auto & [write, key] : writes) {
        // Writing an existing key again keeps its place in property order.
        if (seen.insert(constantKey(key)).second) { keys.push_back(key); }
    }
    return keys;
}

/// True for a read of the global `undefined` that no store in the module can
/// have replaced (the realm's own binding is not writable either).
bool undefinedGlobal(mlir::Value value) {
    auto load = value.getDefiningOp<LoadGlobalOp>();
    if (!load || load.getName() != "undefined") { return false; }
    const auto stored = load->getParentOfType<mlir::ModuleOp>()->walk([](StoreGlobalOp store) {
        return store.getName() == "undefined" ? mlir::WalkResult::interrupt()
                                              : mlir::WalkResult::advance();
    });
    return !stored.wasInterrupted();
}

} // namespace

unsigned expandKnownSpreads(mlir::FunctionOpInterface body) {
    llvm::SmallVector<CopyPropsOp> copies;
    body->walk([&](CopyPropsOp copy) { copies.push_back(copy); });
    unsigned expanded = 0;
    for (CopyPropsOp copy : copies) {
        if (!copy.getTarget().getDefiningOp<CreateObjectOp>()) { continue; }
        const mlir::Value source = copy.getSource();
        // CopyDataProperties step 2: null and undefined copy nothing.
        if (constantAttr<NullAttr>(source) || isUndefinedConstant(source) ||
            undefinedGlobal(source)) {
            copy.erase();
            // Reading the global `undefined` cannot throw, so an unused read
            // goes too.
            if (auto load = source.getDefiningOp<LoadGlobalOp>(); load && load->use_empty()) {
                load.erase();
            }
            ++expanded;
            continue;
        }
        auto literal = source.getDefiningOp<CreateObjectOp>();
        if (!literal) { continue; }
        const auto keys = knownKeys(literal, copy);
        if (!keys) { continue; }
        mlir::OpBuilder builder{copy};
        for (mlir::Value key : *keys) {
            const mlir::Value value =
                GetPropertyOp::create(builder, copy.getLoc(), source.getType(), source, key);
            SetPropertyOp::create(builder, copy.getLoc(), copy.getTarget(), key, value)
                ->setAttr(defineOwnMarker, builder.getUnitAttr());
        }
        copy.erase();
        // A `{}` written only to be spread (`...(c ? o : {})` after folding)
        // has no use left.
        if (literal->use_empty()) { literal.erase(); }
        ++expanded;
    }
    return expanded;
}

} // namespace ctcompile::ctjs
