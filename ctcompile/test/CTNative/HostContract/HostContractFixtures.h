#pragma once
// The fixtures and helpers shared by test/HostContract.cpp and
// test/HostContractSeededMaps.cpp - one 1,068-line file until 2026-09-08. The
// two MLIR fixtures, `check`, `contractFor` and `replaced` are verbatim from
// that file; the three builders at the bottom are the exact statements that
// used to construct the shared two-method Map fixture inline in
// checkCapturedCallables, lifted so the seeded-result executable can build the
// same string. `inline` in a named namespace only so both can include this.

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>

namespace ctcompile::test::host_contract {

namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;

inline constexpr const char * fixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %publish = ctjs.create_closure %callee[1] this %u
    %host = ctjs.create_object
    ctjs.store_global "host", %host
    %invoked = ctjs.call_direct @publish$1(%u, %u, %publish, %host)
    %alias = ctjs.load_global "host"
    %key = ctjs.constant #ctjs.string<"slot">
    %read = ctjs.get_property %alias[%key]
    ctjs.store_global "trace", %read
    ctjs.return %u
  }
  ctjs.func private @publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %host: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %key = ctjs.constant #ctjs.string<"slot">
    %value = ctjs.constant #ctjs.number<4631107791820423168>
    ctjs.set_property %host[%key], %value
    ctjs.return %u
  }
}
)MLIR";

inline constexpr const char * callableFixture = R"MLIR(
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

inline std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}

// The captured getter: callableFixture with its getter closing over a Map cell.
inline std::string capturedGetterSource() {
    auto source = replaced(callableFixture, "%getter = ctjs.create_closure %callee[2] this %u",
                           "%cell = ctjs.create_cell %u\n"
                           "    %constructor = ctjs.load_global \"Map\"\n"
                           "    %state = ctjs.construct %constructor(%constructor)\n"
                           "    ctjs.cell_set %cell, %state\n"
                           "    %getter = ctjs.create_closure %callee[2] this %u captures %cell");
    source = replaced(source,
                      "attributes {upvalue_count = 0 : i32} {\n"
                      "    %answer = ctjs.constant #ctjs.number<4631107791820423168>",
                      "attributes {upvalue_count = 1 : i32} {\n"
                      "    %state = ctjs.load_upvalue %callee[0]\n"
                      "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %answer = ctjs.get_property %state[%key]");
    return source;
}

// The same table with a sibling `put` method over the same cell.
inline std::string sharedMapSource(const std::string & source) {
    auto shared = replaced(source, "    ctjs.return %table",
                           "    %putter = ctjs.create_closure %callee[3] this %u captures %cell\n"
                           "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                           "    ctjs.set_property %table[%putKey], %putter\n"
                           "    ctjs.return %table");
    shared = replaced(shared, "\n}\n", R"MLIR(
  ctjs.func private @put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 1 : i32} {
    %state = ctjs.load_upvalue %callee[0]
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %written = ctjs.call %setter(%state, %entryKey, %value)
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR");
    return shared;
}

// ... and the entry calling that sibling before the getter, which is the form
// checkCapturedParameters and checkSeededMapResults take.
inline std::string sharedMapWithPutCall(std::string shared) {
    shared = replaced(shared, "    %answer = ctjs.call %getter(%owned)",
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    %putter = ctjs.get_property %owned[%putKey]\n"
                      "    %putResult = ctjs.call %putter(%owned)\n"
                      "    %answer = ctjs.call %getter(%owned)");
    return shared;
}

} // namespace ctcompile::test::host_contract
