#pragma once

// PHASE 54A'S TRANSFER FUNCTION, ONE JAVASCRIPT FACT AT A TIME.
//
// The oracle in TypeOracle.cpp answers "is the inference sound over a corpus",
// which is the question that matters and is also the question that cannot say
// WHICH rule is wrong. This file is the other half: a table of small functions
// whose right answer is a statement about JavaScript, checked individually, so
// a regression names the operator.
//
// THE NEGATIVE ROWS ARE THE POINT OF THE TABLE. An inference that answered
// `num<i32>` for every `|` would pass a table containing only the positive
// rows, and it would be WRONG, because `1n | 2n` is `3n`. So for each numeric
// operator there are two rows - one where the operands are known numbers and
// the claim is allowed, and one where they are function parameters and the
// claim must collapse to `boxed`. If a change makes the analysis unconditional,
// the negative rows go red and say which operator did it.
//
// Part 23 §1.4's ratio note: this file is a TEST, and tests are not counted
// against the ODS-first rule - there is no TableGen way to assert that
// `-0 | 0` is an int32 and `-0` alone is not.
#include "../../Ownership/GlobalMethodsFixtures.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ctcompile::test::type_inference {

using ctcompile::ctnative::TypeInference;
using ctcompile::ctnative::TypeLattice;

extern int failures;

// One row: a ctjs function, and what the operation marked `check` must infer.
// `body` OWNS ITS TEXT rather than pointing at it. Several rows build their
// body by concatenation, and a `const char *` taken from such a temporary
// dangles at the end of the initializer's full-expression - which reads as a
// parse failure in a row that is written correctly.
struct row {
    const char * what;     // the JavaScript fact, for the failure message
    std::string body;      // the operations, inside a ctjs.func
    const char * expected; // the printed ctnative type
    bool wholeModule = false;
    int fieldAssigned = -1; // -1 leaves historical rows unchanged; 0/1 checks live presence.
};

// THE FUNCTION HEADER EVERY ROW SHARES. ctjs.func takes three implicit
// arguments before the JavaScript ones - receiver, new_target, callee - and
// %p and %q are ordinary parameters, which is what makes them `boxed`: nothing
// is known about a caller, so nothing is known about an argument. That is
// precisely the state in which a BigInt claim cannot be ruled out.
inline constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

// 5.0, 1.5, -0.0 and 2**31 as the bit patterns ctjs.number carries. Spelled as
// bits because a NumberAttr is bits - see CTJSAttrs.td, which explains that a
// builtin FloatAttr would lose the difference between -0.0 and 0.0, and that
// difference is one of the rows below.
inline constexpr const char * kFive = "#ctjs.number<4617315517961601024>";         // 5.0
inline constexpr const char * kOneAndAHalf = "#ctjs.number<4609434218613702656>";  // 1.5
inline constexpr const char * kNegativeZero = "#ctjs.number<9223372036854775808>"; // -0.0
inline constexpr const char * kTwoToThe31 = "#ctjs.number<4746794007248502784>";   // 2147483648.0

extern const std::string kIdentityFieldPrelude;
extern const std::string kIdentityMapPrelude;

std::string prologue();
void check(mlir::ModuleOp module, const char * what, const char * expected,
           const ctcompile::ctnative::OwnedGlobalRoots * owner = nullptr);
void check(mlir::MLIRContext & context, const row & r);
std::string invokeModule(const std::string & helpers, llvm::StringRef observed = "%payload",
                         bool observeNormal = false);
std::string throwingHelper(llvm::StringRef payload, llvm::StringRef prefix = "");
std::string conditionalHelper(llvm::StringRef result, llvm::StringRef payload,
                              llvm::StringRef prefix = "");
std::string helperChain(unsigned depth);
std::string identityFieldStore(llvm::StringRef object = "%first", llvm::StringRef value = "%one",
                               llvm::StringRef key = "%key");
std::string identityFieldRead(llvm::StringRef object = "%first");
std::string identityMapSet(llvm::StringRef result, llvm::StringRef object = "%first");
std::string identityMapGet(llvm::StringRef result = "%saved", llvm::StringRef key = "%slot");
ctcompile::ctjs::GetPropertyOp checkedField(mlir::ModuleOp module);
void checkFieldBudgets(mlir::ModuleOp module, const char * what, bool assigned);
void checkIdentityFieldRows(mlir::MLIRContext & context);
void checkIdentityMapFieldRows(mlir::MLIRContext & context);
void checkMapZeroSizePresence(mlir::MLIRContext & context);
void checkMapExactSizePresence(mlir::MLIRContext & context);
void checkMapDeleteSizePresence(mlir::MLIRContext & context);
void checkStaleFieldEffects(mlir::MLIRContext & context);
void checkComparisonIdentityRows(mlir::MLIRContext & context);
void checkComparisonIdentityMutations(mlir::MLIRContext & context);
void checkSavedScalarGlobalTypes(mlir::MLIRContext & context);

} // namespace ctcompile::test::type_inference
