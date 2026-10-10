//===- Tests.h - Type inference test declarations ---------------*- C++ -*-===//
//
// Unit rows for the type inference transfer functions, one JavaScript fact at
// a time.
//
// The oracle in test/Analysis/Types/Oracle.cpp answers "is the inference sound
// over a corpus", which is the question that matters but cannot say which rule
// is wrong. These tests are the other half: a table of small functions whose
// right answer is a statement about JavaScript, checked individually, so a
// regression names the operator.
//
// The negative rows are the point of the table. An inference that answered
// `num<i32>` for every `|` would pass a table containing only the positive
// rows, and it would be wrong, because `1n | 2n` is `3n`. So each numeric
// operator has two rows: one where the operands are known numbers and the
// claim is allowed, and one where they are function parameters and the claim
// must collapse to `boxed`. If a change makes the analysis unconditional, the
// negative rows go red and say which operator did it.
//
// This is hand-written C++ rather than TableGen because there is no
// declarative way to assert that `-0 | 0` is an int32 and `-0` alone is not.
//
//===----------------------------------------------------------------------===//

#pragma once

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
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "check.hpp"

namespace ctcompile::test::type_inference {

using ctcompile::ctnative::TypeInference;
using ctcompile::ctnative::TypeLattice;

/// Reports a check() failure whose message needs formatting arguments.
template <typename... Args> void fail(std::format_string<Args...> format, Args &&... args) {
    ::check(false, std::format(format, std::forward<Args>(args)...));
}

/// One row: a ctjs function, and what the operation marked `check` must infer.
/// `body` owns its text rather than pointing at it. Several rows build their
/// body by concatenation, and a `const char *` taken from such a temporary
/// dangles at the end of the initializer's full-expression, which reads as a
/// parse failure in a row that is written correctly.
struct row {
    const char * what;     ///< The JavaScript fact, for the failure message.
    std::string body;      ///< The operations, inside a ctjs.func.
    const char * expected; ///< The printed ctnative type.
    /// True when `body` is a complete module rather than a function body.
    bool wholeModule = false;
    /// -1 skips the field-presence check; 0 or 1 expects the checked field
    /// absent or assigned.
    int fieldAssigned = -1;
};

/// The function header every row shares. ctjs.func takes three implicit
/// arguments before the JavaScript ones (receiver, new_target, callee), and %p
/// and %q are ordinary parameters, which is what makes them `boxed`: nothing is
/// known about a caller, so nothing is known about an argument. That is
/// precisely the state in which a BigInt claim cannot be ruled out.
inline constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

// 5.0, 1.5, -0.0 and 2**31 as the IEEE-754 bit patterns ctjs.number carries.
// They are spelled as bits because a NumberAttr is bits: CTJSAttrs.td explains
// that a builtin FloatAttr would lose the difference between -0.0 and 0.0, and
// that difference is one of the rows.
inline constexpr const char * kFive = "#ctjs.number<4617315517961601024>";         // 5.0
inline constexpr const char * kOneAndAHalf = "#ctjs.number<4609434218613702656>";  // 1.5
inline constexpr const char * kNegativeZero = "#ctjs.number<9223372036854775808>"; // -0.0
inline constexpr const char * kTwoToThe31 = "#ctjs.number<4746794007248502784>";   // 2147483648.0

/// Function-body prologue for identity field rows; see IdentityFields.cpp.
extern const std::string kIdentityFieldPrelude;
/// kIdentityFieldPrelude plus one Map and its methods.
extern const std::string kIdentityMapPrelude;

/// Returns kPrologue as a string, for concatenation.
std::string prologue();
/// Runs type inference over \p module and checks that the single-result
/// operation marked `check` infers the printed type \p expected. \p owner
/// optionally supplies owned-global facts to the analysis.
void check(mlir::ModuleOp module, const char * what, const char * expected,
           const ctcompile::ctnative::OwnedGlobalRoots * owner = nullptr);
/// Parses one row (wrapping its body in kPrologue unless it is a whole module),
/// checks its type, and checks field presence when `r.fieldAssigned` is set.
void check(mlir::MLIRContext & context, const row & r);
/// Returns a module whose @caller invokes @helper inside `ctjs.invoke` and
/// marks \p observed with `check`, in the unwind region or, when
/// \p observeNormal, in the normal region. \p helpers supplies @helper.
std::string invokeModule(const std::string & helpers, llvm::StringRef observed = "%payload",
                         bool observeNormal = false);
/// Returns an @helper that always throws the constant \p payload.
std::string throwingHelper(llvm::StringRef payload, llvm::StringRef prefix = "");
/// Returns a nothrow-marked @helper that returns \p result when its condition
/// is truthy and throws \p payload otherwise.
std::string conditionalHelper(llvm::StringRef result, llvm::StringRef payload,
                              llvm::StringRef prefix = "");
/// Returns \p depth helpers that call each other in a chain; the last throws
/// its argument.
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
