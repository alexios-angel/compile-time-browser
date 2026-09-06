#include "SourceNames.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <iterator>

namespace ctcompile::js::bytecode_detail {
namespace {

constexpr llvm::StringLiteral source_name = "ctnative.source_name";
constexpr llvm::StringLiteral source_names_by_result = "ctnative.source_names";

mlir::DictionaryAttr metadata_of(mlir::Location location) {
    if (auto fused = mlir::dyn_cast<mlir::FusedLoc>(location)) {
        return mlir::dyn_cast_if_present<mlir::DictionaryAttr>(fused.getMetadata());
    }
    return {};
}

mlir::Location with_metadata(mlir::Location original, mlir::DictionaryAttr metadata) {
    // Keep instruction identity and source position as direct children: the
    // escape oracle and global diagnostics already consume that shape.
    if (auto fused = mlir::dyn_cast<mlir::FusedLoc>(original)) {
        return mlir::FusedLoc::get(original.getContext(), fused.getLocations(), metadata);
    }
    return mlir::FusedLoc::get(original.getContext(), {original}, metadata);
}

} // namespace

source_names::source_names(const ctbrowser::script::function_proto & proto) {
    if (proto.locals.empty()) { return; }
    slots.resize(proto.frame_size);
    for (const auto & local : proto.locals) {
        if (local.name.empty() || local.reg >= slots.size() || local.first_pc >= local.last_pc) {
            continue;
        }
        slots[local.reg].push_back(&local);
    }
    for (auto & ranges : slots) {
        llvm::sort(ranges, [](local_range left, local_range right) {
            return left->first_pc < right->first_pc;
        });
        // Valid debug scopes do not overlap in one physical register. An
        // ambiguous optional table must not invent a name; the bytecode still
        // imports normally. Non-overlapping register reuse remains distinct.
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i - 1]->last_pc > ranges[i]->first_pc) {
                ranges.clear();
                break;
            }
        }
    }
}

llvm::StringRef source_names::name_at(std::size_t slot, std::size_t pc) const {
    if (slot >= slots.size()) { return {}; }
    const auto & ranges = slots[slot];
    const auto after =
        std::upper_bound(ranges.begin(), ranges.end(), pc,
                         [](std::size_t at, local_range local) { return at < local->first_pc; });
    if (after == ranges.begin()) { return {}; }
    const auto * local = *std::prev(after);
    return pc < local->last_pc ? llvm::StringRef(local->name) : llvm::StringRef{};
}

mlir::Location source_names::location(mlir::Location original, std::size_t slot,
                                      std::size_t pc) const {
    const llvm::StringRef name = name_at(slot, pc);
    if (name.empty()) { return original; }
    mlir::NamedAttrList metadata;
    if (auto existing = metadata_of(original)) { metadata.append(existing.getValue()); }
    metadata.set(source_name, mlir::StringAttr::get(original.getContext(), name));
    return with_metadata(original, metadata.getDictionary(original.getContext()));
}

void source_names::assign(mlir::Value value, std::size_t slot, std::size_t pc) const {
    auto result = mlir::dyn_cast_if_present<mlir::OpResult>(value);
    if (!result) { return; }
    const llvm::StringRef name = name_at(slot, pc);
    if (name.empty()) { return; }
    mlir::Operation * owner = result.getOwner();
    mlir::MLIRContext * context = owner->getContext();
    mlir::NamedAttrList metadata;
    if (auto existing = metadata_of(owner->getLoc())) { metadata.append(existing.getValue()); }
    if (owner->getNumResults() == 1) {
        if (auto existing = mlir::dyn_cast_if_present<mlir::StringAttr>(metadata.get(source_name));
            existing && !existing.getValue().empty()) {
            return;
        }
        metadata.set(source_name, mlir::StringAttr::get(context, name));
    } else {
        llvm::SmallVector<mlir::Attribute> names(owner->getNumResults(),
                                                 mlir::StringAttr::get(context, ""));
        if (auto existing =
                mlir::dyn_cast_if_present<mlir::ArrayAttr>(metadata.get(source_names_by_result));
            existing && existing.size() == names.size()) {
            llvm::copy(existing, names.begin());
        }
        if (auto existing = mlir::dyn_cast<mlir::StringAttr>(names[result.getResultNumber()]);
            existing && !existing.getValue().empty()) {
            return;
        }
        names[result.getResultNumber()] = mlir::StringAttr::get(context, name);
        metadata.set(source_names_by_result, mlir::ArrayAttr::get(context, names));
    }
    owner->setLoc(with_metadata(owner->getLoc(), metadata.getDictionary(context)));
}

} // namespace ctcompile::js::bytecode_detail
