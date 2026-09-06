#pragma once

#include <ctbrowser/script/bytecode.hpp>

#include "mlir/IR/Location.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <cstddef>
#include <vector>

namespace ctcompile::js::bytecode_detail {

// Source spelling is a printing hint, carried by locations so that it does
// not distinguish otherwise identical operations during folding or CSE.
class source_names {
public:
    explicit source_names(const ctbrowser::script::function_proto & proto);

    [[nodiscard]] mlir::Location location(mlir::Location original, std::size_t slot,
                                          std::size_t pc) const;

    // An SSA alias has no separate location. Keep the first named binding of
    // an operation result, and never rename a block/parameter argument when
    // bytecode moves that value into another local's slot.
    void assign(mlir::Value value, std::size_t slot, std::size_t pc) const;

private:
    using local_range = const ctbrowser::script::local_desc *;
    std::vector<llvm::SmallVector<local_range, 1>> slots;

    [[nodiscard]] llvm::StringRef name_at(std::size_t slot, std::size_t pc) const;
};

} // namespace ctcompile::js::bytecode_detail
