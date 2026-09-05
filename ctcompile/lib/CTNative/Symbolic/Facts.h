#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"

#include <optional>

namespace ctcompile::ctnative::symbolic {

enum Domain : unsigned {
    Unknown = 0,
    Number = 1,
    Boolean = 2,
    String = 4,
    Null = 8,
    Undefined = 16
};

// A fact describes a normal result, not whether evaluating its producer is
// effect-free. In particular, a call returning true must still execute.
struct Fact {
    unsigned domains = Unknown;
    mlir::Attribute literal;
    bool operator==(const Fact &) const = default;
};

struct Budget {
    unsigned limit, steps = 0;
    bool exhausted = false;
    bool take() {
        if (steps == limit) {
            exhausted = true;
            return false;
        }
        ++steps;
        return true;
    }
};

Fact literalFact(mlir::Attribute literal);
Fact join(Fact left, Fact right);
llvm::StringRef domainName(Fact fact);
std::optional<bool> condition(Fact fact);

class Analysis {
public:
    Analysis(mlir::ModuleOp module, Budget & budget) : module(module), budget(budget) {}
    void run();
    Fact get(mlir::Value value) const;

private:
    mlir::ModuleOp module;
    Budget & budget;
    llvm::DenseMap<mlir::Value, Fact> facts;
    llvm::DenseMap<mlir::Operation *, Fact> returns;
    bool region(mlir::Region & region);
    Fact operation(mlir::Operation * op);
};

struct Changes {
    unsigned expressions = 0, branches = 0;
};
Changes rewrite(mlir::ModuleOp module, const Analysis & analysis, Budget & budget);

} // namespace ctcompile::ctnative::symbolic
