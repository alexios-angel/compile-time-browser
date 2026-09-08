#pragma once
// The fixtures and helpers shared by test/OwnedGlobalMethods.cpp and
// test/OwnedGlobalSharedMap.cpp - one 1,099-line file until 2026-09-08. All
// verbatim from that file; `inline` in a named namespace only so that both
// executables can include it.

#include "../lib/CTNative/Analysis/OwnedGlobalRoots.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ctcompile::test::owned_global_methods {

namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;
using ctcompile::ctnative::OwnedGlobalRoots;

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

inline int failures = 0;

inline void check(bool value, const char * message) {
    if (value) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

inline HostContract contractFor(mlir::ModuleOp module) {
    HostContract contract;
    contract.moduleSha256 = hostContractFingerprint(module);
    contract.entry = "script$0";
    contract.roots = {{"host", {"slot"}}};
    contract.observations = {"trace"};
    return contract;
}

inline bool empty(mlir::ModuleOp module, const OwnedGlobalRoots & query) {
    bool result = query.roots().empty();
    module.walk([&](mlir::Operation * operation) { result &= !query.lookup(operation); });
    return result;
}

inline std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}

} // namespace ctcompile::test::owned_global_methods
