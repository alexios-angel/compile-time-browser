#pragma once
// The fixtures and helpers shared by test/OwnedGlobalMethods.cpp and
// test/OwnedGlobalSharedMap.cpp - one 1,099-line file until 2026-09-08. All
// verbatim from that file; `inline` in a named namespace only so that both
// executables can include it.

#include "../../../lib/CTNative/Analysis/OwnedGlobalRoots.h"
#include "../../CTNative/HostContract/HostContractFixtures.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/Sequence.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "check.hpp"

namespace ctcompile::test::owned_global_methods {

namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;
using ctcompile::ctnative::OwnedGlobalRoots;
using host_contract::choiceMapSource;
using host_contract::contractFor;
using host_contract::mapContractFor;
using host_contract::parsed;
using host_contract::replaced;

inline constexpr const char * fixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %make = ctjs.create_closure %callee[1] this %u
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %table = ctjs.call_direct @make$1(%u, %u, %make)
    %slot = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %host[%slot], %table
    %alias = ctjs.load_global "host"
    %owned = ctjs.get_property %alias[%slot]
    %key = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %owned[%key]
    %answer = ctjs.call %getter(%owned)
    ctjs.store_global "trace", %answer
    ctjs.return %u
  }
  ctjs.func private @make$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %getter = ctjs.create_closure %callee[2] this %u
    %table = ctjs.create_object
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %getter
    ctjs.return %table
  }
  ctjs.func private @get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %answer = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.return %answer
  }
}
)MLIR";

inline constexpr const char * capturedFixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %wrapper = ctjs.create_closure %callee[1] this %u
    %factory = ctjs.create_closure %callee[2] this %u
    %invoked = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)
    %alias = ctjs.load_global "host"
    %slot = ctjs.constant #ctjs.string<"slot">
    %owned = ctjs.get_property %alias[%slot]
    %key = ctjs.constant #ctjs.string<"get">
    %getter = ctjs.get_property %owned[%key]
    %answer = ctjs.call %getter(%owned)
    ctjs.store_global "trace", %answer
    ctjs.return %u
  }
  ctjs.func private @publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %factory: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %host = ctjs.load_global "host"
    %table = ctjs.call_direct @make$2(%u, %u, %factory)
    %slot = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %host[%slot], %table
    ctjs.return %u
  }
  ctjs.func private @make$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %cell = ctjs.create_cell %u
    %constructor = ctjs.load_global "Map"
    %state = ctjs.construct %constructor(%constructor)
    ctjs.cell_set %cell, %state
    %table = ctjs.create_object
    %getter = ctjs.create_closure %callee[3] this %u captures %cell
    %key = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %table[%key], %getter
    ctjs.return %table
  }
  ctjs.func private @get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %key = ctjs.constant #ctjs.string<"size">
    %size = ctjs.get_property %state[%key]
    ctjs.return %size
  }
}
)MLIR";

// The actual imported publication specimen, retaining public functions, frame
// bookkeeping and the indirect wrapper callback. Only symbol names are unified
// with the small fixture above so both use the same assertions.
inline constexpr const char * importedCapturedFixture = R"MLIR(
module {
  ctjs.func @script$0(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.frame_enter 3
    %1 = ctjs.create_object
    ctjs.store_global "host", %1
    %2 = ctjs.constant #ctjs.undefined
    %3 = ctjs.create_closure %arg2[1] this %2
    %4 = ctjs.constant #ctjs.undefined
    %5 = ctjs.create_closure %arg2[2] this %4
    %6 = ctjs.constant #ctjs.undefined
    %7 = ctjs.constant #ctjs.undefined
    %8 = ctjs.call_direct @publish$1(%6, %7, %3, %5)
    %9 = ctjs.load_global "host"
    %10 = ctjs.constant #ctjs.string<"slot">
    %11 = ctjs.get_property %9[%10]
    %12 = ctjs.constant #ctjs.string<"get">
    %13 = ctjs.get_property %11[%12]
    %14 = ctjs.call %13(%11)
    ctjs.store_global "trace", %14
    %15 = ctjs.constant #ctjs.undefined
    ctjs.frame_exit %0
    ctjs.return %15
  }
  ctjs.func @publish$1(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value, %arg3: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.frame_enter 4
    %1 = ctjs.load_global "host"
    %2 = ctjs.constant #ctjs.undefined
    %3 = ctjs.call %arg3(%2)
    %4 = ctjs.constant #ctjs.string<"slot">
    ctjs.set_property %1[%4], %3
    %5 = ctjs.constant #ctjs.undefined
    ctjs.frame_exit %0
    ctjs.return %5
  }
  ctjs.func @make$2(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %0 = ctjs.frame_enter 3
    %1 = ctjs.constant #ctjs.undefined
    %2 = ctjs.create_cell %1
    %3 = ctjs.load_global "Map"
    %4 = ctjs.construct %3(%3)
    ctjs.cell_set %2, %4
    %5 = ctjs.create_object
    %6 = ctjs.constant #ctjs.undefined
    %7 = ctjs.create_closure %arg2[3] this %6 captures %2
    %8 = ctjs.constant #ctjs.string<"get">
    ctjs.set_property %5[%8], %7
    ctjs.frame_exit %0
    ctjs.return %5
  }
  ctjs.func @get$3(%arg0: !ctjs.value, %arg1: !ctjs.value, %arg2: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %0 = ctjs.frame_enter 1
    %1 = ctjs.load_upvalue %arg2[0]
    %2 = ctjs.constant #ctjs.string<"size">
    %3 = ctjs.get_property %1[%2]
    ctjs.frame_exit %0
    ctjs.return %3
  }
}
)MLIR";

inline bool empty(mlir::ModuleOp module, const OwnedGlobalRoots & query) {
    bool result = query.roots().empty();
    module.walk([&](mlir::Operation * operation) { result &= !query.lookup(operation); });
    return result;
}

// A live edit has left `contract` stale: the old report refuses on its fingerprint, and the
// edited source, fingerprinted afresh as `current`, still refuses without running out of work.
inline void checkStaleAndRefused(mlir::ModuleOp module, const HostContract & contract,
                                 const HostContract & current, std::string_view staleWhat,
                                 std::string_view refusedWhat) {
    OwnedGlobalRoots stale(module, contract);
    check(!stale.proved() && stale.reason().contains("fingerprint") && empty(module, stale),
          staleWhat);
    OwnedGlobalRoots fresh(module, current);
    check(!fresh.proved() && !fresh.exhausted() && empty(module, fresh), refusedWhat);
}

// Every budget in `budgets` (each below completion) refuses, reports exhaustion, stays within
// its budget and publishes nothing: `withheld` when given, otherwise `empty`.
template <typename Query = OwnedGlobalRoots, typename Budgets = std::initializer_list<unsigned>>
void checkIncompleteBudgets(
    mlir::ModuleOp module, const HostContract & contract, const Budgets & budgets,
    std::string_view what,
    std::type_identity_t<llvm::function_ref<bool(const Query &)>> withheld = {}) {
    for (const unsigned budget : budgets) {
        Query limited(module, contract, budget);
        const auto nothing = [&] {
            if constexpr (std::is_same_v<Query, OwnedGlobalRoots>) {
                if (!withheld) { return empty(module, limited); }
            }
            return withheld(limited);
        };
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget && nothing(),
              what);
    }
}

// Every budget below `completion`.
template <typename Query = OwnedGlobalRoots>
void checkBudgetsBelow(
    mlir::ModuleOp module, const HostContract & contract, unsigned completion,
    std::string_view what,
    std::type_identity_t<llvm::function_ref<bool(const Query &)>> withheld = {}) {
    checkIncompleteBudgets<Query>(module, contract, llvm::seq(0u, completion), what, withheld);
}

} // namespace ctcompile::test::owned_global_methods
