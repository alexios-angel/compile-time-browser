#pragma once

#include "mlir/IR/Location.h"

namespace ctcompile {

// THE JAVASCRIPT SITE OF A LOCATION. The importer fuses a NameLoc with the
// FileLineColLoc of the site; the first file location found, at any depth, is
// the site. Null when there is none.
inline mlir::FileLineColLoc sourceSite(mlir::Location loc) {
    mlir::FileLineColLoc site;
    loc->walk([&](mlir::Location l) {
        site = llvm::dyn_cast<mlir::FileLineColLoc>(l);
        return site ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
    });
    return site;
}

} // namespace ctcompile
