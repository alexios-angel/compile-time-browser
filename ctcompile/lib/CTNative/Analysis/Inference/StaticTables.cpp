//===- StaticTables.cpp - Constant tables of run-once statics -------------===//
//
// Proves which static variables of run-once functions hold a constant table:
// an object literal of numbers, booleans, strings, nulls and arrays of numbers
// or of strings that nothing writes after it is made, read only through
// constant keys or returned. A bundle's factory keeps its components'
// defaults that way:
//
//     const Default = {interval: 5000, keyboard: true, pause: "hover"};
//     class Carousel { static get Default() { return Default; } }
//
// StaticTables.h lists the conditions. A static that holds an object literal
// but misses one gets the reason on its cells instead, which admission
// reports at every reader.
//
//===----------------------------------------------------------------------===//
#include "ctcompile/CTNative/Analysis/StaticTables.h"

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <cmath>
#include <optional>
#include <string>

namespace ctcompile::ctnative {
namespace {

using Reason = std::optional<std::string>;

/// The constant `value` is, when it may be a scalar field: a finite number, a
/// boolean, a string or null. Null otherwise.
mlir::Attribute scalarConstant(mlir::Value value) {
    auto constant = value.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    mlir::Attribute attr = constant.getValue();
    if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(attr)) {
        return std::isfinite(number.getDouble()) ? attr : mlir::Attribute{};
    }
    return llvm::isa<ctjs::BooleanAttr, ctjs::StringAttr, ctjs::NullAttr>(attr) ? attr
                                                                                : mlir::Attribute{};
}

/// One literal under proof: its fields and the operations that build it.
struct table {
    llvm::StringMap<mlir::Attribute> scalars;
    llvm::StringSet<> arrays;
    llvm::SmallVector<mlir::Operation *> parts;
};

/// Why `array`, the value of field `key`, is not an array literal of numbers
/// only or strings only used for nothing but that field; adds its operations
/// to `into` otherwise.
Reason arrayField(ctjs::CreateArrayOp array, llvm::StringRef key, table & into) {
    const std::string why =
        "holds `" + key.str() + "`, an array that is not only numbers or only strings";
    llvm::SmallVector<mlir::Value> elements(array.getElements());
    llvm::SmallVector<mlir::Operation *> parts{array};
    unsigned stores = 0;
    for (mlir::OpOperand & use : array.getResult().getUses()) {
        if (auto append = llvm::dyn_cast<ctjs::AppendOp>(use.getOwner());
            append && use.getOperandNumber() == 0) {
            elements.push_back(append.getElement());
            parts.push_back(append);
        } else if (llvm::isa<ctjs::SetPropertyOp>(use.getOwner()) && use.getOperandNumber() == 2) {
            ++stores;
        } else {
            return "holds `" + key.str() + "`, an array used for something else";
        }
    }
    if (stores != 1 || elements.empty()) { return why; }
    const bool numbers = llvm::isa_and_nonnull<ctjs::NumberAttr>(scalarConstant(elements[0]));
    for (mlir::Value element : elements) {
        const mlir::Attribute constant = scalarConstant(element);
        if (!constant || llvm::isa<ctjs::NumberAttr>(constant) != numbers ||
            (!numbers && !llvm::isa<ctjs::StringAttr>(constant))) {
            return why;
        }
    }
    into.arrays.insert(key);
    into.parts.append(parts);
    return std::nullopt;
}

/// Why `literal`, stored into its static by `write`, is not a table of
/// constants complete before that write; fills `into` otherwise.
Reason literalFields(ctjs::CreateObjectOp literal, ctjs::CellSetOp write, table & into) {
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (user == write.getOperation() && use.getOperandNumber() == 1) { continue; }
        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
        if (!store || use.getOperandNumber() != 0) {
            return "is built with `" + user->getName().getStringRef().str() +
                   "`, not only with constant fields";
        }
        const llvm::StringRef key = ctjs::constantKey(store.getKey());
        if (key.empty() || key == "__proto__") {
            return std::string("defines a field through a key that is not a constant");
        }
        if (into.scalars.contains(key) || into.arrays.contains(key)) {
            return "defines `" + key.str() + "` twice";
        }
        if (store->getBlock() != write->getBlock() || !store->isBeforeInBlock(write)) {
            return "defines `" + key.str() + "` after it is stored";
        }
        if (const mlir::Attribute constant = scalarConstant(store.getValue())) {
            into.scalars[key] = constant;
        } else if (auto array = store.getValue().getDefiningOp<ctjs::CreateArrayOp>()) {
            if (auto why = arrayField(array, key, into)) { return why; }
        } else {
            return "holds `" + key.str() +
                   "`, which is not a constant number, boolean, string, null or array of them";
        }
        into.parts.push_back(store);
    }
    return std::nullopt;
}

/// Checks every use of `value`, which names the table, and records the field
/// reads to mark. Follows returns into the results of direct calls.
struct readers {
    const table & fields;
    const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> & calls;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, mlir::Attribute>> reads;
    llvm::DenseSet<mlir::Operation *> followed;

    Reason check(mlir::Value value) {
        for (mlir::OpOperand & use : value.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
                read && use.getOperandNumber() == 0) {
                const llvm::StringRef key = ctjs::constantKey(read.getKey());
                if (key.empty()) {
                    return std::string("is read with a key that is not a constant");
                }
                if (fields.arrays.contains(key)) {
                    return "is read at its array field `" + key.str() + "`";
                }
                const auto found = fields.scalars.find(key);
                if (found == fields.scalars.end()) {
                    return "is read at `" + key.str() + "`, which the literal does not define";
                }
                reads.emplace_back(read, found->second);
                continue;
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                auto function = user->getParentOfType<ctjs::FuncOp>();
                if (!followed.insert(function).second) { continue; }
                const auto found = calls.find(function);
                if (found == calls.end()) { continue; }
                for (ctjs::CallDirectOp call : found->second) {
                    if (auto why = check(call.getResult())) { return why; }
                }
                continue;
            }
            if (llvm::isa<ctjs::SetPropertyOp>(user) && use.getOperandNumber() == 0) {
                return std::string("is written after it is made");
            }
            if (llvm::isa<ctjs::SetPropertyOp, ctjs::AppendOp, ctjs::CellSetOp,
                          ctjs::StoreGlobalOp>(user)) {
                return std::string("is stored into another object, array or variable");
            }
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, ctjs::ConstructOp>(user)) {
                return std::string("is passed as an argument");
            }
            return "is used by `" + user->getName().getStringRef().str() + "`";
        }
        return std::nullopt;
    }
};

} // namespace

void markStaticTables(mlir::ModuleOp module) {
    // Every cell of each static variable, and every write to one, by name.
    llvm::MapVector<mlir::StringAttr, llvm::SmallVector<ctjs::CreateCellOp>> cells;
    module.walk([&](ctjs::CreateCellOp cell) {
        if (auto name = cell->getAttrOfType<mlir::StringAttr>(kNativeStaticCell)) {
            cells[name].push_back(cell);
        }
    });
    if (cells.empty()) { return; }
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> calls;
    module.walk([&](ctjs::CallDirectOp call) {
        if (auto target = call.getTarget()) { calls[target].push_back(call); }
    });

    for (auto & [name, group] : cells) {
        llvm::SmallVector<ctjs::CellSetOp> writes;
        for (ctjs::CreateCellOp cell : group) {
            for (mlir::Operation * user : cell.getResult().getUsers()) {
                if (auto write = llvm::dyn_cast<ctjs::CellSetOp>(user);
                    write && write.getCell() == cell.getResult()) {
                    writes.push_back(write);
                }
            }
        }
        if (llvm::none_of(writes, [](ctjs::CellSetOp write) {
                return write.getValue().getDefiningOp<ctjs::CreateObjectOp>() != nullptr;
            })) {
            continue;
        }
        table fields;
        readers uses{fields, calls, {}, {}};
        const Reason why = [&]() -> Reason {
            auto write = writes.front();
            auto literal = write.getValue().getDefiningOp<ctjs::CreateObjectOp>();
            auto owner = write.getCell().getDefiningOp<ctjs::CreateCellOp>();
            if (writes.size() != 1 || !literal || !owner ||
                owner->hasAttr(kNativeStaticReference) ||
                literal->getBlock() != write->getBlock()) {
                return std::string("is written after it is made");
            }
            if (auto bad = literalFields(literal, write, fields)) { return bad; }
            // An empty literal is an identity, not a table: nothing reads it,
            // and NativeObjectIdentity.h may give it its own carrier.
            if (fields.scalars.empty() && fields.arrays.empty()) {
                return std::string("defines no field");
            }
            fields.parts.push_back(literal);
            for (ctjs::CreateCellOp cell : group) {
                for (mlir::Operation * user : cell.getResult().getUsers()) {
                    if (auto read = llvm::dyn_cast<ctjs::CellGetOp>(user)) {
                        if (auto bad = uses.check(read.getResult())) { return bad; }
                    }
                }
            }
            return std::nullopt;
        }();
        if (why) {
            for (ctjs::CreateCellOp cell : group) {
                cell->setAttr(kNativeStaticTableReason,
                              mlir::StringAttr::get(cell.getContext(), *why));
            }
            continue;
        }
        for (mlir::Operation * part : fields.parts) { part->setAttr(kNativeStaticTable, name); }
        for (auto [read, constant] : uses.reads) {
            read->setAttr(kNativeStaticTable, name);
            read->setAttr(kNativeStaticTableField, constant);
        }
    }
}

} // namespace ctcompile::ctnative
