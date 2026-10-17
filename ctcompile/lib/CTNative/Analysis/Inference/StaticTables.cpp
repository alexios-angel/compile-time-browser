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
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace ctcompile::ctnative {

std::string tableKey(mlir::Value key) {
    if (auto text = ctjs::constantAttr<ctjs::StringAttr>(key)) { return text.getValue().str(); }
    auto number = ctjs::constantAttr<ctjs::NumberAttr>(key);
    if (!number) { return {}; }
    const double value = number.getDouble();
    if (!(value >= 0.0 && value < 4294967295.0) || value != std::floor(value)) { return {}; }
    return std::to_string(static_cast<std::uint64_t>(value));
}

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
    /// Every field's key operand, in property order (OrdinaryOwnPropertyKeys:
    /// array indices ascending, then the rest as the literal defines them).
    llvm::SmallVector<mlir::Value> order;
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
    llvm::SmallVector<ctjs::SetPropertyOp> stores;
    for (mlir::OpOperand & use : literal.getResult().getUses()) {
        mlir::Operation * user = use.getOwner();
        if (user == write.getOperation() && use.getOperandNumber() == 1) { continue; }
        auto store = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
        if (!store || use.getOperandNumber() != 0) {
            return "is built with `" + user->getName().getStringRef().str() +
                   "`, not only with constant fields";
        }
        const std::string key = tableKey(store.getKey());
        if (key.empty() || key == "__proto__") {
            return std::string("defines a field through a key that is not a constant");
        }
        if (into.scalars.contains(key) || into.arrays.contains(key)) {
            return "defines `" + key + "` twice";
        }
        if (store->getBlock() != write->getBlock() || !store->isBeforeInBlock(write)) {
            return "defines `" + key + "` after it is stored";
        }
        if (const mlir::Attribute constant = scalarConstant(store.getValue())) {
            into.scalars[key] = constant;
        } else if (auto array = store.getValue().getDefiningOp<ctjs::CreateArrayOp>()) {
            if (auto why = arrayField(array, key, into)) { return why; }
        } else {
            return "holds `" + key +
                   "`, which is not a constant number, boolean, string, null or array of them";
        }
        into.parts.push_back(store);
        stores.push_back(store);
    }
    // Every store is in the write's block (checked above), so block order is
    // definition order.
    llvm::sort(stores,
               [](ctjs::SetPropertyOp a, ctjs::SetPropertyOp b) { return a->isBeforeInBlock(b); });
    for (ctjs::SetPropertyOp store : stores) { into.order.push_back(store.getKey()); }
    sortOwnKeyOrder(into.order, [](mlir::Value key) { return tableKey(key); });
    return std::nullopt;
}

/// Checks every use of `value`, which names the table, and records the field
/// reads to mark. Follows returns into the results of direct calls.
struct readers {
    const table & fields;
    const llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> & calls;
    llvm::SmallVector<std::pair<ctjs::GetPropertyOp, mlir::Attribute>> reads;
    /// Spreads of the table into a fresh literal, written out once proved.
    llvm::SmallVector<ctjs::CopyPropsOp> spreads;
    /// `Object.entries`, `keys` or `values` calls of the table, marked once
    /// proved.
    llvm::SmallVector<ctjs::CallOp> enumerations;
    llvm::DenseSet<mlir::Operation *> followed;

    /// Whether `use` is the argument of a built-in statics row that takes a
    /// constant table (Object.entries, keys, values) and every field of this
    /// table is a String, which is what the row answers.
    bool enumerates(ctjs::CallOp call, mlir::OpOperand & use) const {
        const auto * row = TypeInference::builtinStaticOf(call);
        if (!row || row->params[0] != TypeInference::BuiltinStatic::Param::staticTable ||
            call.getArgs().size() != 1 || call.getArgs().front() != use.get() ||
            !fields.arrays.empty()) {
            return false;
        }
        return llvm::all_of(fields.scalars, [](const auto & field) {
            return llvm::isa<ctjs::StringAttr>(field.getValue());
        });
    }

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
            if (auto spread = llvm::dyn_cast<ctjs::CopyPropsOp>(user);
                spread && use.getOperandNumber() == 1) {
                if (!spread.getTarget().getDefiningOp<ctjs::CreateObjectOp>()) {
                    return std::string("is spread into an object that is not a fresh literal");
                }
                if (!fields.arrays.empty()) {
                    return "is spread while it holds the array field `" +
                           fields.arrays.begin()->first().str() + "`";
                }
                spreads.push_back(spread);
                continue;
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user); call && enumerates(call, use)) {
                enumerations.push_back(call);
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

/// Writes out `spread`, a proved spread of table `name`: per field, a read
/// marked as the table's (and as a spread's, since the table may still be
/// undefined) and an own definition on the target.
void expandSpread(ctjs::CopyPropsOp spread, mlir::StringAttr name, const table & fields) {
    mlir::OpBuilder builder{spread};
    const mlir::Location where = spread.getLoc();
    const mlir::Value source = spread.getSource();
    for (mlir::Value definedKey : fields.order) {
        const std::string text = tableKey(definedKey);
        const mlir::Value key =
            ctjs::ConstantOp::create(builder, where, definedKey.getType(),
                                     ctjs::StringAttr::get(builder.getContext(), text));
        auto read = ctjs::GetPropertyOp::create(builder, where, source.getType(), source, key);
        read->setAttr(kNativeStaticTable, name);
        read->setAttr(kNativeStaticTableField, fields.scalars.lookup(text));
        read->setAttr(kNativeStaticTableSpread, builder.getUnitAttr());
        ctjs::SetPropertyOp::create(builder, where, spread.getTarget(), key, read.getResult())
            ->setAttr(ctjs::defineOwnMarker, builder.getUnitAttr());
    }
    spread.erase();
}

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
        for (ctjs::CopyPropsOp spread : uses.spreads) { expandSpread(spread, name, fields); }
        for (ctjs::CallOp call : uses.enumerations) {
            call->setAttr(kNativeStaticTableEnumeration, name);
            call->setAttr(kNativeStaticTableMethod,
                          mlir::StringAttr::get(call.getContext(), ctjs::methodKey(call)));
        }
    }
    unrollTableLoops(module);
}

} // namespace ctcompile::ctnative
