#pragma once

#include "../../../lib/CTNative/HostContract/Analysis.h"
#include "HostContractFixtures.h"

namespace ctcompile::test::host_contract {

inline void checkClassIntrinsics(mlir::MLIRContext & context) {
    using namespace ctnative;
    constexpr const char * source = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %n = ctjs.constant #ctjs.number<1>
    %key = ctjs.constant #ctjs.string<"name">
    %helper = ctjs.load_global "HELPER"
    %result = ctjs.call %helper(ARGS)
    ctjs.return %u
  }
}
)MLIR";
    for (const auto & [name, arguments] :
         {std::pair{"__ctbrowser_class_heritage", "%u, %u, %u, %u"},
          std::pair{"__ctbrowser_bind_this", "%u, %u"},
          std::pair{"__ctbrowser_init_fields", "%u, %u, %u"},
          std::pair{"__ctbrowser_super_get", "%u, %u, %u, %u"}}) {
        const auto text = replaced(replaced(source, "HELPER", name), "ARGS", arguments);
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "class helper invocation fixture parses");
        if (!module) { continue; }
        HostContract contract;
        contract.moduleSha256 = hostContractFingerprint(*module);
        contract.entry = "script$0";
        contract.initialIntrinsics = {"__ctbrowser_class_defined", name};
        check(host_detail::initialBindingProblem(*module, contract).empty(),
              "declared class helper identity permits its exact direct invocation shape");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "binding validation preserves every original helper operation");
        auto effectful = mlir::parseSourceString<mlir::ModuleOp>(
            replaced(callableFixture, "%host = ctjs.create_object",
                     std::string{"%helper = ctjs.load_global \""} + name +
                         "\"\n%result = ctjs.call %helper(" + arguments +
                         ")\n%host = ctjs.create_object"),
            &context);
        check(static_cast<bool>(effectful), "class helper effect fixture parses");
        if (effectful) {
            auto request = contractFor(*effectful);
            request.initialIntrinsics = contract.initialIntrinsics;
            check(host_detail::initialBindingProblem(*effectful, request).empty(),
                  "ordinary host source retains a declared class helper invocation");
            check(!HostContractAnalysis(*effectful, request).proved(),
                  "helper binding identity cannot authorize its unproved effects");
        }
        for (const auto provider : {"closed-source-v1", "closed-source-session-v1",
                                    "ctbrowser-dom-v1", "ctbrowser-dom-session-v1"}) {
            const bool dom = llvm::StringRef(provider).starts_with("ctbrowser-dom");
            const auto json = std::string{"{\"version\":1,\"provider\":\""} + provider +
                              "\",\"module_sha256\":\"" + contract.moduleSha256 +
                              "\",\"entry\":\"script$0\",\"initial_intrinsics\":["
                              "\"__ctbrowser_class_defined\",\"" +
                              name + "\"]," +
                              (dom ? "\"element_parameters\":[0]}"
                                   : "\"roots\":[{\"binding\":\"host\",\"properties\":[\"slot\"]}],"
                                     "\"observations\":[\"a\"],\"absent_bindings\":[],"
                                     "\"undefined_bindings\":[]}");
            auto parsed = parseHostContract(json);
            check(parsed && parsed->initialIntrinsics == contract.initialIntrinsics,
                  "class preparation manifests preserve declared helper identities");
            if (!parsed) { llvm::consumeError(parsed.takeError()); }
            for (const auto & invalid :
                 {replaced(json, name, "__ctbrowser_unknown_class_helper"),
                  replaced(json, name, std::string{name} + "\",\"" + name)}) {
                auto rejected = parseHostContract(invalid);
                check(!rejected, "unknown and duplicate class helper identities refuse");
                if (!rejected) { llvm::consumeError(rejected.takeError()); }
            }
            if (dom) {
                auto orphan =
                    parseHostContract(replaced(json, "\"__ctbrowser_class_defined\",", ""));
                check(!orphan, "DOM helper identities require class preparation");
                if (!orphan) { llvm::consumeError(orphan.takeError()); }
            }
        }
        for (const auto & [from, to] :
             {std::pair{std::string{"ctjs.return %u"}, std::string{"ctjs.return %helper"}},
              std::pair{std::string{"%helper("} + arguments + ")",
                        std::string{"%helper("} + arguments + ", %u)"},
              std::pair{std::string{"%helper(%u,"}, std::string{"%helper(%n,"}},
              std::pair{std::string{"%helper(%u, %u"}, std::string{"%helper(%u, %helper"}},
              std::pair{std::string{"ctjs.return %u"},
                        std::string{"%read = ctjs.get_property %helper[%key]\nctjs.return %u"}},
              std::pair{std::string{"ctjs.return %u"},
                        std::string{"ctjs.store_global \""} + name + "\", %u\nctjs.return %u"},
              std::pair{std::string{"ctjs.return %u"},
                        std::string{"%binding = ctjs.constant #ctjs.string<\""} + name +
                            "\">\nctjs.set_property %this[%binding], %u\nctjs.return %u"}}) {
            auto changed =
                mlir::parseSourceString<mlir::ModuleOp>(replaced(text, from, to), &context);
            check(static_cast<bool>(changed), "class helper misuse fixture parses");
            if (!changed) { continue; }
            check(!host_detail::initialBindingProblem(*changed, contract).empty(),
                  "class helper aliases, reflection, mutation and malformed calls refuse");
        }
        for (bool absent : {false, true}) {
            auto invalid = contract;
            (absent ? invalid.absentBindings : invalid.undefinedBindings).push_back(name);
            check(!host_detail::initialBindingProblem(*module, invalid).empty(),
                  "typed helper declarations cannot also be absent or undefined");
        }
    }
}

} // namespace ctcompile::test::host_contract
