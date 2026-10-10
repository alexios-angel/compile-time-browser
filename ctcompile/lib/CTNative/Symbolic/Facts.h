//===- Facts.h - Symbolic primitive-value facts -----------------*- C++ -*-===//
//
// A small abstract interpretation for --ctnative-precompute: each value gets
// a Fact (which primitive kinds it may be, and its literal when exactly one
// is known). Facts flow through function arguments and return values across
// direct calls, and Rewrite.cpp folds what they prove.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/WorkBudget.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseMap.h"

#include <optional>

namespace ctcompile::ctnative::symbolic {

/// Bit flags for the primitive kinds a value may have. Unknown (0) means
/// nothing is known, including whether it is a primitive at all.
enum Domain : unsigned {
    Unknown = 0,
    Number = 1,
    Boolean = 2,
    String = 4,
    Null = 8,
    Undefined = 16
};

/// What is known about a value: a union of Domain bits, and its literal when
/// it is a single known constant. A fact describes the normal result, not
/// whether its producer is effect-free: a call known to return true must still
/// run.
struct Fact {
    unsigned domains = Unknown;
    mlir::Attribute literal;
    bool operator==(const Fact &) const = default;
};

/// The fact for a constant attribute; unknown for anything else.
Fact literalFact(mlir::Attribute literal);
/// The least fact covering both; unknown if either is.
Fact join(Fact left, Fact right);
/// A short name for the fact's domains, as printed in ctnative.symbolic_results.
llvm::StringRef domainName(Fact fact);
/// The JavaScript truthiness of the fact's literal, or nullopt if not known.
std::optional<bool> condition(Fact fact);

/// Computes facts for every value of a module, within `budget`. If the budget
/// runs out, run() stops early and the facts are incomplete.
class Analysis {
public:
    Analysis(mlir::ModuleOp module, WorkBudget & budget) : module(module), budget(budget) {}
    void run();
    Fact get(mlir::Value value) const;

private:
    mlir::ModuleOp module;
    WorkBudget & budget;
    llvm::DenseMap<mlir::Value, Fact> facts;
    llvm::DenseMap<mlir::Value, Fact> arguments;
    llvm::DenseMap<mlir::Operation *, Fact> returns;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    bool collectCallers(llvm::ArrayRef<ctjs::FuncOp> functions);
    bool region(mlir::Region & region);
    Fact operation(mlir::Operation * op);
};

/// What rewrite() changed.
struct Changes {
    unsigned expressions = 0, branches = 0;
};
/// Replaces each operation whose result `analysis` proved constant with a
/// constant, and each scf.if with a known condition by the taken branch.
Changes rewrite(mlir::ModuleOp module, const Analysis & analysis, WorkBudget & budget);

} // namespace ctcompile::ctnative::symbolic
