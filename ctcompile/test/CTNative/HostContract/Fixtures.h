//===- Fixtures.h - Shared fixtures -----------------------------*- C++ -*-===//
//
// Fixtures and helpers shared by HostContract.cpp, its DOM/ headers, the
// SeededMaps/ tests and the Analysis/Ownership tests: the two MLIR fixtures,
// `contractFor`, `mapContractFor`, `withheld`, `parsed`, `replaced`, and the
// builders for the shared two-method Map fixture. Everything is `inline` so
// several test executables can include it.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#include "check.hpp"

namespace ctcompile::test::host_contract {

namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::HostContract;
using ctcompile::ctnative::HostContractAnalysis;
using ctcompile::ctnative::hostContractFingerprint;

/// A script that creates `host`, calls publish$1 to store 42 in `host.slot`,
/// then reads `host.slot` back into the `trace` global.
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

/// A script that stores a table from make$1 in `host.slot` and calls the
/// table's `get` method (get$2, returning 42), storing the result in `trace`.
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

/// A contract for `module` with entry script$0, root `host.slot` and the
/// `trace` observation, bound to the module's current fingerprint.
inline HostContract contractFor(mlir::ModuleOp module) {
    HostContract contract;
    contract.moduleSha256 = hostContractFingerprint(module);
    contract.entry = "script$0";
    contract.roots = {{"host", {"slot"}}};
    contract.observations = {"trace"};
    return contract;
}

/// contractFor with the Map intrinsic requested.
inline HostContract mapContractFor(mlir::ModuleOp module) {
    auto contract = contractFor(module);
    contract.initialIntrinsics = {"Map"};
    return contract;
}

/// True when `query` exposes no callable family, callable lookup or slot lookup.
inline bool withheld(mlir::ModuleOp module, const HostContractAnalysis & query) {
    bool none = query.callables().empty();
    module.walk([&](mlir::Operation * operation) {
        none &= !query.callable(operation);
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
            none &= !query.property(read);
        }
    });
    return none;
}

/// parseSourceString, with the parse checked under `label`.
inline mlir::OwningOpRef<mlir::ModuleOp> parsed(mlir::MLIRContext & context, llvm::StringRef text,
                                                std::string_view label) {
    auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    check(static_cast<bool>(module), label);
    return module;
}

/// `source` with the first occurrence of `from` replaced by `to`, or an empty
/// string when `from` does not occur.
inline std::string replaced(std::string source, llvm::StringRef from, llvm::StringRef to) {
    const auto offset = source.find(from.str());
    if (offset == std::string::npos) { return {}; }
    source.replace(offset, from.size(), to.str());
    return source;
}

/// callableFixture with its getter closing over a Map cell and returning its size.
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

/// `source` with a sibling `put` method (put$3) over the same Map cell.
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

/// `shared` with the entry calling `put` before the getter: the form that
/// checkCapturedParameters and checkSeededMapResults take.
inline std::string sharedMapWithPutCall(std::string shared) {
    shared = replaced(shared, "    %answer = ctjs.call %getter(%owned)",
                      "    %putKey = ctjs.constant #ctjs.string<\"put\">\n"
                      "    %putter = ctjs.get_property %owned[%putKey]\n"
                      "    %putResult = ctjs.call %putter(%owned)\n"
                      "    %answer = ctjs.call %getter(%owned)");
    return shared;
}

/// A put-family source whose three putters (prepared environments `putterEnvs`)
/// also take %other and %choice, and whose getter `getter` takes the put result:
/// the base of the captured-clear and definite-absence checks.
inline std::string choiceMapSource(const std::string & source, bool prepared,
                                   const std::array<const char *, 3> & putterEnvs,
                                   const char * getter) {
    auto fixture =
        replaced(source, "%entryKey: !ctjs.value)",
                 "%entryKey: !ctjs.value, %aliasKey: !ctjs.value, %choice: !ctjs.value)");
    fixture = replaced(fixture, "    %actual =",
                       "    %other = ctjs.constant #ctjs.string<\"z\">\n"
                       "    %choice = ctjs.constant #ctjs.boolean<true>\n"
                       "    %actual =");
    for (const auto & [env, actual] : {std::pair{putterEnvs[0], "actual"},
                                       {putterEnvs[1], "actual"},
                                       {putterEnvs[2], "future"}}) {
        const std::string prefix = prepared ? std::string("%") + env + ", " : "%owned, ";
        fixture = replaced(fixture, prefix + "%" + actual + ")",
                           prefix + "%" + actual + ", %other, %choice)");
    }
    const std::string getterSignature =
        std::string(getter) + "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
        std::string(prepared ? ", %state: !ctjs.value" : "") + ")";
    fixture =
        replaced(fixture, getterSignature,
                 getterSignature.substr(0, getterSignature.size() - 1) + ", %input: !ctjs.value)");
    return replaced(fixture, prepared ? "%getterEnv)" : "%getter(%owned)",
                    prepared ? "%getterEnv, %putResult)" : "%getter(%owned, %putResult)");
}

} // namespace ctcompile::test::host_contract
