#include "Tests.h"

#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "llvm/Support/Error.h"

namespace ctcompile::test::owned_global_shared_map {

void checkDOMKeyInputs(mlir::MLIRContext & context, const std::string & source,
                       const std::string & family, bool prepared) {
    const auto external = [&](std::string text) {
        text =
            replaced(text, "@script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                     "@script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                     "%element: !ctjs.value)");
        text = replaced(text, "    %actual = ctjs.create_object\n", "");
        while (text.find("%actual") != std::string::npos) {
            text = replaced(text, "%actual", "%element");
        }
        return text;
    };
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.provider = HostContract::Provider::ctbrowserDOMDataSession;
        contract.initialIntrinsics = {"Map", "Array"};
        if (module.lookupSymbol<ctjs::FuncOp>("dataEntry$9")) { contract.entry = "dataEntry$9"; }
        auto entry = module.lookupSymbol<ctjs::FuncOp>(contract.entry);
        for (unsigned index = 3; index < entry.getBody().front().getNumArguments(); ++index) {
            contract.elementParameters.push_back(index - 3);
        }
        return contract;
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected, const char * message,
                             unsigned calls = 1, unsigned inputs = 1, unsigned unusedInputs = 0) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "DOM key input fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        check(host.proved() == expected && !host.exhausted(), message);
        if (host.proved() != expected) {
            std::fprintf(stderr, "DOM key input %s row %u: %s\n", prepared ? "prepared" : "source",
                         rows, host.reason().str().c_str());
        }
        OwnedGlobalRoots owner(*module, contract);
        check(owner.proved() == expected, "DOM Data revalidates the complete source owner");
        if (owner.proved() != expected) {
            std::fprintf(stderr, "DOM source owner row %u: %s\n", rows,
                         owner.reason().str().c_str());
        }
        if (expected && host.proved()) {
            check(owner.roots().size() == 1 && owner.roots().front().methodTable &&
                      owner.roots().front().methodTable->capturedMap &&
                      owner.roots().front().methodTable->calls.size() == calls &&
                      owner.wrapper() == host.wrapper(),
                  "DOM source ownership retains the exact allocation and complete call family");
            auto entry = module->lookupSymbol<ctjs::FuncOp>(contract.entry);
            std::vector<mlir::BlockArgument> expectedInputs;
            for (unsigned index = 0; index < inputs + unusedInputs; ++index) {
                expectedInputs.push_back(entry.getBody().front().getArgument(3 + index));
            }
            check(llvm::equal(owner.domInputs(), expectedInputs),
                  "DOM source ownership retains every declared external input origin");
            // Unused inputs still cross the validated entry boundary, but only
            // the observed identities receive captured Map key authority.
            expectedInputs.resize(inputs);
            if (rows == 1 && owner.proved()) {
                using namespace ctcompile::ctnative;
                context.getOrLoadDialect<CTNativeDialect>();
                for (const bool authorized : {false, true}) {
                    mlir::DataFlowSolver solver;
                    solver.load<mlir::dataflow::DeadCodeAnalysis>();
                    solver.load<mlir::dataflow::SparseConstantPropagation>();
                    solver.load<TypeInference>(authorized ? &owner : nullptr);
                    check(succeeded(solver.initializeAndRun(*module)),
                          "DOM input type inference converges");
                    const auto * lattice = solver.lookupState<TypeLattice>(expectedInputs.front());
                    check(lattice && !lattice->getValue().isUninitialized() &&
                              llvm::isa<DOMElementType>(lattice->getValue().getType()) ==
                                  authorized,
                          "only the complete owner proof seeds an external DOM input type");
                }
            }
            bool complete = host.callables().size() == calls;
            for (const auto & edge : host.callables()) {
                complete &= edge.capturedMap && edge.arguments.size() == 1;
                if (!edge.capturedMap || edge.arguments.size() != 1) { continue; }
                const auto & argument = edge.arguments.front();
                complete &=
                    argument.element && !argument.object && argument.actual == argument.element &&
                    llvm::is_contained(expectedInputs, argument.element) &&
                    llvm::is_contained(edge.capturedMap->outerKeyParameters, argument.parameter) &&
                    edge.capturedMap->outerKeyInputs == expectedInputs &&
                    edge.capturedMap->outerKeyObjects.empty() &&
                    argument.alternatives == ctcompile::ctnative::PrimitiveAlternatives{};
            }
            check(complete,
                  "every call retains separate explicit DOM origins and complete family roles");
        } else if (!expected) {
            check(empty(*module, owner) && !owner.wrapper() && owner.domInputs().empty(),
                  "a failed DOM source owner exposes no storage or declaration evidence");
            check(host.callables().empty() && !host.wrapper(),
                  "failed DOM provenance publishes no partial family or declaration");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "DOM provenance preserves the actual source arguments and operations");
    };
    const auto normal = external(source);
    const auto siblings = external(family);
    const std::string observation = "    ctjs.store_global \"trace\", %answer\n";
    const std::string has = "    %found = ctjs.call %method(%state, %entryKey)\n";
    const std::string retain = R"MLIR(
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %stored = ctjs.call %setter(%state, %entryKey, %one)
)MLIR";
    const auto repeat = [&](const std::string & text, const char * actual) {
        return replaced(text, observation,
                        "    %again = ctjs.get_property %owned[%key]\n" +
                            std::string(prepared ? "    %againEnv = ctjs.load_upvalue %again[0]\n"
                                                   "    %againResult = ctjs.call_direct @get$3("
                                                   "%owned, %u, %again, %againEnv, "
                                                 : "    %againResult = ctjs.call %again(%owned, ") +
                            actual + ")\n" + observation);
    };
    variant(normal, true, "an explicit DOM argument has source provenance for a direct outer key");
    variant(replaced(normal, observation,
                     "    %self = ctjs.create_closure %callee[0] this %u\n" + observation),
            false, "a source callable cannot invent another DOM entry activation");
    variant(replaced(normal, observation,
                     "    %self = ctjs.create_closure %callee[0] this %u\n"
                     "    %reentered = ctjs.call_direct @script$0(%u, %u, %self, %u)\n" +
                         observation),
            false, "a source call cannot supply an unvalidated DOM input to the entry");
    variant(repeat(normal, "%element"), true, "repeated actuals preserve the same DOM origin", 2);
    auto distinct =
        replaced(normal, "%element: !ctjs.value)", "%element: !ctjs.value, %other: !ctjs.value)");
    variant(repeat(distinct, "%other"), true,
            "different DOM inputs retain separate origins without a disjointness promise", 2, 2);
    variant(distinct, true, "an unused input has no captured Map key authority", 1, 1, 1);
    variant(replaced(distinct, observation,
                     "    %field = ctjs.get_property %other[%key]\n" + observation),
            false, "an observed extra input cannot borrow the first input's Map key permission");
    variant(replaced(normal, has, retain + has), true,
            "retention as an outer key supplies provenance without native storage authority");
    variant(siblings, true, "both captured siblings revalidate the same explicit DOM origin", 2);
    variant(replaced(siblings, "%state, %entryKey, %value)", "%state, %entryKey, %entryKey)"),
            false, "one sibling payload use rejects the entire DOM input family");
    variant(replaced(normal, has,
                     retain +
                         "    %map = ctjs.load_global \"Map\"\n"
                         "    %child = ctjs.construct %map(%map)\n"
                         "    %childSet = ctjs.get_property %child[%setKey]\n"
                         "    %childWrite = ctjs.call %childSet(%child, %entryKey, %one)\n" +
                         has),
            false, "a child Map key cannot inherit outer DOM key permission");
    variant(replaced(normal, "ctjs.return %found", "ctjs.return %entryKey"), false,
            "a DOM formal cannot escape through a method result");
    variant(replaced(normal, "    ctjs.return %u\n  }", "    ctjs.return %element\n  }"), false,
            "an entry cannot return its DOM input");
    variant(replaced(normal, has, "    %field = ctjs.get_property %entryKey[%key]\n" + has), false,
            "a key formal has no arbitrary DOM property contract");
    variant(
        replaced(normal, observation, "    ctjs.set_property %element[%key], %u\n" + observation),
        false, "a DOM actual cannot inherit scalar-field object ownership");
    variant(replaced(normal, observation,
                     "    ctjs.store_global \"escaped\", %element\n" + observation),
            false, "DOM inputs cannot escape through named global aliases");
    variant(replaced(normal, observation,
                     "    ctjs.store_global \"extracted\", %getter\n" + observation),
            false, "a DOM method family cannot publish an extracted callable");
    variant(replaced(normal, observation,
                     "    %detached = ctjs.get_property %owned[%key]\n"
                     "    ctjs.store_global \"extracted\", %detached\n" +
                         observation),
            false, "a second uncalled getter cannot publish a retained DOM method");
    variant(replaced(normal, observation,
                     "    ctjs.store_global \"escapedTable\", %owned\n" + observation),
            false, "an alias of the whole table cannot retain borrowed DOM keys");
    const std::string snapshot = R"MLIR(
    %array = ctjs.load_global "Array"
    %fromKey = ctjs.constant #ctjs.string<"from">
    %from = ctjs.get_property %array[%fromKey]
    %keysKey = ctjs.constant #ctjs.string<"keys">
    %keys = ctjs.get_property %state[%keysKey]
    %iterator = ctjs.call %keys(%state)
    %copy = ctjs.call %from(%array, %iterator)
)MLIR";
    variant(replaced(normal, has, retain + snapshot + has), false,
            "even an unused outer snapshot withholds DOM key permission");
    variant(replaced(normal, has,
                     retain +
                         replaced(replaced(snapshot, "%state[%keysKey]", "%stored[%keysKey]"),
                                  "%keys(%state)", "%keys(%stored)") +
                         has),
            false, "a fluent outer snapshot cannot bypass the complete input-role census");

    // Keep the local record and Map computations alongside the complete public
    // family. Only the joined scalar reaches that family, never either owner.
    const std::string localGraph = R"MLIR(
    %localLiteral = ctjs.constant #ctjs.number<4607182418800017408>
    %localInert = ctjs.create_object
    %localConstructor = ctjs.create_closure %callee[8] this %u
    %localRecord = ctjs.construct %localConstructor(%localConstructor, %localLiteral)
    %localMapConstructor = ctjs.load_global "Map"
    %localMap = ctjs.construct %localMapConstructor(%localMapConstructor)
    %localKey = ctjs.constant #ctjs.string<"x">
    %localSetKey = ctjs.constant #ctjs.string<"set">
    %localSet = ctjs.get_property %localMap[%localSetKey]
    %localWritten = ctjs.call %localSet(%localMap, %localKey, %localRecord)
    %localGetKey = ctjs.constant #ctjs.string<"get">
    %localGet = ctjs.get_property %localMap[%localGetKey]
    %localAlias = ctjs.call %localGet(%localMap, %localKey)
    %localName = ctjs.constant #ctjs.string<"number">
    %localSizeKey = ctjs.constant #ctjs.string<"size">
    %localSize = ctjs.get_property %localMap[%localSizeKey]
    %localCondition = ctjs.truthy %localSize
    %localSelected = scf.if %localCondition -> (!ctjs.value) {
      %localThen = ctjs.get_property %localAlias[%localName]
      scf.yield %localThen : !ctjs.value
    } else {
      %localElse = ctjs.get_property %localRecord[%localName]
      %localSum = ctjs.binary add %localElse, %localLiteral
      scf.yield %localSum : !ctjs.value
    }
)MLIR";
    auto records = replaced(siblings, "    %putResult =", localGraph + "    %putResult =");
    records = replaced(records, prepared ? "%putterEnv, %element)" : "%putter(%owned, %element)",
                       prepared ? "%putterEnv, %element, %localSelected)"
                                : "%putter(%owned, %element, %localSelected)");
    records =
        replaced(records, "    %value = ctjs.constant #ctjs.number<4607182418800017408>\n", "");
    const auto setter = records.find("ctjs.func private @put$4(");
    const auto formal = records.find("%entryKey: !ctjs.value)", setter);
    check(formal != std::string::npos, "local record fixture retains the public setter formal");
    if (formal != std::string::npos) {
        records.replace(formal, std::string("%entryKey: !ctjs.value)").size(),
                        "%entryKey: !ctjs.value, %value: !ctjs.value)");
    }
    records = replaced(records, "\n}\n", R"MLIR(
  ctjs.func private @record$8(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %initial: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %name = ctjs.constant #ctjs.string<"number">
    ctjs.set_property %this[%name], %initial
    %recordUndefined = ctjs.constant #ctjs.undefined
    ctjs.return %recordUndefined
  }
}
)MLIR");
    unsigned recordRows = 0;
    const auto localVariant = [&](const std::string & text, bool provider, bool owned,
                                  const char * message) {
        ++recordRows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "local record owner fixture parses");
        if (!module) { return module; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        check(host.proved() == provider && owner.proved() == owned && !host.exhausted() &&
                  !owner.exhausted(),
              message);
        if (host.proved() != provider || owner.proved() != owned) {
            std::fprintf(stderr, "DOM local records %s row %u: provider %s; owner %s\n",
                         prepared ? "prepared" : "source", recordRows, host.reason().str().c_str(),
                         owner.reason().str().c_str());
        }
        if (!provider) {
            check(host.localRecords().constructors.empty() &&
                      host.localRecords().operations.empty() &&
                      !host.localRecords().primitiveFields && host.callables().empty(),
                  "a failed provider exposes no partial constructor or public family graph");
        }
        if (!owned) {
            check(empty(*module, owner) && owner.domInputs().empty() && !owner.wrapper(),
                  "a failed local record owner exposes no storage or DOM input evidence");
        } else if (host.proved() && owner.proved()) {
            const auto & local = host.localRecords();
            const auto count = [&](auto matches) {
                return llvm::count_if(local.operations, matches);
            };
            check(
                local.primitiveFields && local.constructors.size() == 1 &&
                    local.constructors.front() == module->lookupSymbol<ctjs::FuncOp>("record$8") &&
                    local.operations.size() == 17 &&
                    count([](auto * op) { return llvm::isa<ctjs::ConstructOp>(op); }) == 2 &&
                    count([](auto * op) { return llvm::isa<ctjs::CallOp>(op); }) == 2 &&
                    count([](auto * op) { return llvm::isa<ctjs::BinaryOp>(op); }) == 1 &&
                    count([](auto * op) { return llvm::isa<ctjs::GetPropertyOp>(op); }) == 5 &&
                    count([](auto * op) { return llvm::isa<ctjs::SetPropertyOp>(op); }) == 1 &&
                    count([](auto * op) { return llvm::isa<ctjs::CreateClosureOp>(op); }) == 1 &&
                    llvm::all_of(local.operations,
                                 [&](auto * op) { return llvm::count(local.operations, op) == 1; }),
                "the local census retains every constructor, Map operation and branch read once");
            unsigned functions = 0, constructions = 0, objects = 0;
            module->walk([&](ctjs::FuncOp) { ++functions; });
            module->walk([&](ctjs::ConstructOp) { ++constructions; });
            module->walk([&](ctjs::CreateObjectOp) { ++objects; });
            check(
                functions == 6 && constructions == 3 && objects == 3 && owner.roots().size() == 1 &&
                    owner.domInputs().size() == 1 && owner.roots().front().methodTable &&
                    owner.roots().front().methodTable->methods.size() == 2 &&
                    owner.roots().front().methodTable->calls.size() == 2 &&
                    owner.roots().front().methodTable->capturedMap && host.callables().size() == 2,
                "ownership preserves the complete function/allocation census and public family");
            for (const auto & edge : host.callables()) {
                auto function = edge.function;
                const bool put = function.getSymName() == "put$4";
                check(
                    edge.arguments.size() == (put ? 2u : 1u) && edge.capturedMap &&
                        owner.domInputs().size() == 1 &&
                        edge.arguments.front().element == owner.domInputs().front() &&
                        (!put || (!edge.arguments.back().object && !edge.arguments.back().element &&
                                  edge.arguments.back().alternatives.tag() ==
                                      mlir::TypeID::get<ctjs::NumberAttr>())),
                    "only the record's scalar branch result crosses into the public family");
            }
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "local ownership queries preserve the original source computations");
        return module;
    };
    auto localModule =
        localVariant(records, true, true,
                     "closed local constructors and Map aliases compose with DOM ownership");
    for (const auto & [from, to] : {
             std::pair{"ctjs.set_property %this[%name], %initial",
                       "ctjs.store_global \"ctorEffect\", %initial\n"
                       "    ctjs.set_property %this[%name], %initial"},
             {"ctjs.return %recordUndefined", " %replacement = ctjs.create_object\n"
                                              "    ctjs.return %replacement"},
             {"%localConstructor(%localConstructor, %localLiteral)",
              "%localConstructor(%localConstructor, %element)"},
             {"%localSet(%localMap, %localKey, %localRecord)",
              "%localSet(%localRecord, %localKey, %localRecord)"},
             {"    %putResult =", "    ctjs.store_global \"escaped\", %localMap\n    %putResult ="},
             {"    %putResult =", "    %captureRecord = ctjs.create_closure %callee[8] this %u "
                                  "captures %localRecord\n    %putResult ="},
             {"      scf.yield %localThen", "      ctjs.set_property %localAlias[%localName], "
                                            "%localLiteral\n      scf.yield %localThen"},
             {"%localSet(%localMap, %localKey, %localRecord)",
              "%localSet(%localMap, %localLiteral, %localRecord)"},
             {"ctjs.binary add %localElse, %localLiteral", "ctjs.binary add %localElse, %element"},
             {"    %putResult =",
              "    ctjs.store_global \"escaped\", %localAlias\n    %putResult ="},
         }) {
        localVariant(
            replaced(records, from, to), false, false,
            "unknown effects, replacement, escapes and branch mutation refuse the whole graph");
    }
    localVariant(replaced(records, "    %putResult =",
                          "    ctjs.store_global \"escapedInert\", %localInert\n    %putResult ="),
                 true, false, "a retained source allocation must have no observable use");
    auto unread = localVariant(
        replaced(records, "    %putResult =",
                 "    %extra = ctjs.create_object\n"
                 "    %extraKey = ctjs.constant #ctjs.string<\"extra\">\n"
                 "    ctjs.set_property %localAlias[%extraKey], %extra\n    %putResult ="),
        true, false, "an unread object field cannot borrow primitive native record ownership");
    if (unread) {
        HostContractAnalysis host(*unread, requested(*unread));
        check(host.proved() && !host.localRecords().constructors.empty() &&
                  !host.localRecords().primitiveFields,
              "provider field snapshots and native storage authorization remain separate");
    }
    localVariant(replaced(records, "\n}\n", R"MLIR(
  ctjs.func private @extra$9(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    ctjs.return %u
  }
}
)MLIR"),
                 true, false, "an uncensused function cannot join the complete native owner graph");
    if (localModule) {
        const auto manifest = requested(*localModule);
        HostContractAnalysis host(*localModule, manifest);
        OwnedGlobalRoots owner(*localModule, manifest);
        if (host.proved() && owner.proved()) {
            for (unsigned budget : {0u, host.steps() - 1}) {
                HostContractAnalysis limited(*localModule, manifest, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          limited.localRecords().constructors.empty() &&
                          limited.localRecords().operations.empty() &&
                          !limited.localRecords().primitiveFields && limited.callables().empty(),
                      "an incomplete provider budget publishes no local record graph");
            }
            for (unsigned budget : {0u, host.steps(), owner.steps() - 1}) {
                OwnedGlobalRoots limited(*localModule, manifest, budget);
                check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                          empty(*localModule, limited) && limited.domInputs().empty(),
                      "an incomplete owner budget publishes no local storage authority");
            }
            check(HostContractAnalysis(*localModule, manifest, host.steps()).proved() &&
                      OwnedGlobalRoots(*localModule, manifest, owner.steps()).proved(),
                  "exact budgets restore both complete source and ownership proofs");
            mlir::Builder attributes(&context);
            (*localModule)->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
            check(OwnedGlobalRoots(*localModule, manifest).proved(),
                  "forged record reports do not obstruct independent source ownership");
            auto entry = localModule->lookupSymbol<ctjs::FuncOp>(manifest.entry);
            auto construction = *entry.getBody().front().getOps<ctjs::ConstructOp>().begin();
            construction->setOperand(2, entry.getBody().front().getArgument(3));
            HostContractAnalysis staleHost(*localModule, manifest);
            OwnedGlobalRoots staleOwner(*localModule, manifest);
            const auto fresh = requested(*localModule);
            HostContractAnalysis freshHost(*localModule, fresh);
            OwnedGlobalRoots freshOwner(*localModule, fresh);
            check(!staleHost.proved() && staleHost.reason().contains("fingerprint") &&
                      !staleOwner.proved() && staleOwner.reason().contains("fingerprint") &&
                      !freshHost.proved() && freshHost.localRecords().constructors.empty() &&
                      freshHost.localRecords().operations.empty() && !freshOwner.proved() &&
                      empty(*localModule, freshOwner),
                  "stale fingerprints and forged records cannot authorize a nonliteral actual");
        }
    }
    std::printf("DOM local record owners %s: %u rows, exact census, report and budget controls\n",
                prepared ? "prepared" : "source", recordRows);

    for (const bool different : {false, true}) {
        auto readback = replaced(siblings, "#ctjs.string<\"has\">", "#ctjs.string<\"get\">");
        if (different) {
            readback = replaced(readback, "%element: !ctjs.value)",
                                "%element: !ctjs.value, %other: !ctjs.value)");
            readback = replaced(
                readback, prepared ? "%getter, %getterEnv, %element)" : "%getter(%owned, %element)",
                prepared ? "%getter, %getterEnv, %other)" : "%getter(%owned, %other)");
        }
        variant(readback, true, "entry replay retains actual DOM key alias uncertainty", 2,
                different ? 2 : 1);
        readback = replaced(readback, "    ctjs.return %found", R"MLIR(
    %present = ctjs.truthy %found
    %selected = scf.if %present -> (!ctjs.value) {
      scf.yield %found : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.null
      scf.yield %missing : !ctjs.value
    }
    ctjs.return %selected
)MLIR");
        variant(readback, true, "alias partitions close a conditional scalar return", 2,
                different ? 2 : 1);
        auto program = mlir::parseSourceString<mlir::ModuleOp>(readback, &context);
        if (!program) { continue; }
        const auto manifest = requested(*program);
        HostContractAnalysis query(*program, manifest);
        OwnedGlobalRoots owner(*program, manifest);
        using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
        const Alternatives expected{Alternatives::Number, different ? Alternatives::Null : 0u,
                                    true};
        bool observed = false;
        mlir::Value answer;
        for (const auto & edge : query.callables()) {
            auto function = edge.function;
            if (function.getSymName() != "get$3" || !edge.capturedMap) { continue; }
            answer = edge.call->getResult(0);
            for (const auto & result : edge.capturedMap->returnedScalars) {
                if (result.call != edge.call) { continue; }
                observed = result.alternatives == expected;
            }
        }
        check(query.proved() && owner.proved() && observed && answer &&
                  owner.returnedScalar(answer) == expected && !owner.returnedLeaf(answer),
              different ? "every alias partition contributes to the exact Number-or-Null result"
                        : "reusing one DOM input proves only the preceding Number insertion");
        if (!different || !query.proved() || !owner.proved() || !answer) { continue; }
        for (unsigned budget : {0u, query.steps() - 1}) {
            HostContractAnalysis partial(*program, manifest, budget);
            check(!partial.proved() && partial.exhausted() && partial.steps() <= budget &&
                      partial.callables().empty(),
                  "an incomplete alias family publishes no joined scalar evidence");
        }
        for (unsigned budget : {0u, query.steps(), owner.steps() - 1}) {
            OwnedGlobalRoots partial(*program, manifest, budget);
            check(!partial.proved() && partial.exhausted() && partial.steps() <= budget &&
                      empty(*program, partial) && partial.domInputs().empty() &&
                      partial.returnedScalar(answer) == Alternatives{} &&
                      !partial.returnedLeaf(answer),
                  "an incomplete alias owner publishes no scalar, leaf or DOM input evidence");
        }
        OwnedGlobalRoots exact(*program, manifest, owner.steps());
        check(exact.proved() && exact.steps() == owner.steps() &&
                  exact.returnedScalar(answer) == expected,
              "the exact budget restores the complete alias result union");
    }

    const std::string declaration = R"MLIR(
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)
      -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %frame = ctjs.frame_enter 1
    %u = ctjs.constant #ctjs.undefined
    %declared = ctjs.create_closure %callee[9] this %this
    ctjs.root %declared in %frame
    ctjs.store_global "dataEntry", %declared
    ctjs.frame_exit %frame
    ctjs.return %u
  }
)MLIR";
    const auto wrapped = replaced(replaced(normal, "@script$0(", "@dataEntry$9("), "module {",
                                  "module {" + declaration);
    variant(wrapped, true, "an inert imported declaration preserves the DOM Data input proof");
    variant(replaced(wrapped, "this %this", "this %u"), true,
            "a normalized undefined receiver preserves the exact declaration");
    variant(replaced(wrapped, "ctjs.store_global \"dataEntry\", %declared",
                     "ctjs.store_global \"wrongName\", %declared"),
            false, "declaration publication must name the exact entry binding");
    variant(replaced(wrapped, "ctjs.frame_exit %frame",
                     "ctjs.store_global \"sideEffect\", %u\n    ctjs.frame_exit %frame"),
            false, "source effects beside the declaration cannot be omitted");
    variant(replaced(wrapped, observation,
                     "    %published = ctjs.load_global \"dataEntry\"\n" + observation),
            false, "even an unused source read observes the entry publication");
    variant(
        replaced(wrapped, observation, "    ctjs.store_global \"dataEntry\", %u\n" + observation),
        false, "entry publication cannot be replaced later in source");
    variant(replaced(wrapped, "%callee[9] this %this", "%callee[9] this %this captures %u"), false,
            "a wrapper cannot capture another activation for the DOM entry");
    variant(replaced(wrapped, observation,
                     "    %redeclare = ctjs.create_closure %callee[9] this %u\n" + observation),
            false, "the complete source has exactly one declaration of its host entry");
    variant(replaced(wrapped, observation,
                     "    %script = ctjs.create_closure %callee[0] this %u\n" + observation),
            false, "the omitted wrapper cannot itself become a source callable");
    auto declared = mlir::parseSourceString<mlir::ModuleOp>(wrapped, &context);
    if (declared) {
        const auto manifest = requested(*declared);
        HostContractAnalysis query(*declared, manifest);
        check(query.proved() && query.wrapper() == declared->lookupSymbol<ctjs::FuncOp>("script$0"),
              "only the complete proof exposes the exact inert wrapper");
        if (query.proved()) {
            OwnedGlobalRoots owner(*declared, manifest);
            check(owner.proved() && owner.wrapper() == query.wrapper(),
                  "complete ownership retains the inert declaration separately from Data");
            if (owner.proved()) {
                for (unsigned budget : {0u, query.steps(), owner.steps() - 1}) {
                    OwnedGlobalRoots partial(*declared, manifest, budget);
                    check(!partial.proved() && partial.exhausted() && empty(*declared, partial) &&
                              !partial.wrapper() && partial.domInputs().empty(),
                          "partial DOM ownership exposes no wrapper or storage evidence");
                }
                check(OwnedGlobalRoots(*declared, manifest, owner.steps()).proved(),
                      "the exact DOM source ownership budget completes");
            }
            for (unsigned budget : {0u, 1u, query.steps() / 2, query.steps() - 1}) {
                HostContractAnalysis partial(*declared, manifest, budget);
                check(!partial.proved() && partial.exhausted() && !partial.wrapper() &&
                          partial.callables().empty() && partial.steps() <= budget,
                      "an incomplete declaration/family proof publishes no removable wrapper");
            }
            HostContractAnalysis exact(*declared, manifest, query.steps());
            check(exact.proved() && exact.wrapper() == query.wrapper(),
                  "the exact declaration work budget preserves complete evidence");
        }
    }

    auto module = mlir::parseSourceString<mlir::ModuleOp>(normal, &context);
    if (!module) { return; }
    const auto contract = requested(*module);
    HostContractAnalysis host(*module, contract);
    if (!host.proved()) { return; }
    for (const std::vector<unsigned> & indices :
         {std::vector<unsigned>{}, std::vector<unsigned>{1}, std::vector<unsigned>{0, 0},
          std::vector<unsigned>{0, 1}}) {
        auto changed = contract;
        changed.elementParameters = indices;
        HostContractAnalysis refused(*module, changed);
        check(!refused.proved() && refused.callables().empty(),
              "programmatic input contracts require every exact explicit parameter in order");
    }
    for (const auto provider :
         {HostContract::Provider::closedSource, HostContract::Provider::closedSourceSession,
          HostContract::Provider::ctbrowserDOMSession}) {
        auto changed = contract;
        changed.provider = provider;
        check(!HostContractAnalysis(*module, changed).proved(),
              "another provider cannot borrow DOM Data input authorization");
    }
    mlir::Builder attributes(&context);
    auto edge = host.callables().front();
    (*module)->setAttr("ctnative.host_outer_key_inputs", attributes.getI64IntegerAttr(99));
    edge.call->setAttr("ctnative.dom_element", attributes.getUnitAttr());
    check(HostContractAnalysis(*module, requested(*module)).proved(),
          "untrusted DOM reports cannot replace or obstruct live source proof");
    const auto argument = edge.arguments.front();
    edge.call->setOperand(prepared ? 4 : 2, edge.read.getObject());
    HostContractAnalysis stale(*module, contract);
    HostContractAnalysis fresh(*module, requested(*module));
    check(!stale.proved() && stale.reason().contains("fingerprint") && stale.callables().empty() &&
              !fresh.proved() && fresh.callables().empty(),
          "fresh fingerprints and forged reports cannot turn a table into a DOM actual");
    edge.call->setOperand(prepared ? 4 : 2, argument.actual);
    (*module)->removeAttr("ctnative.host_outer_key_inputs");
    edge.call->removeAttr("ctnative.dom_element");
    check(hostContractFingerprint(*module) == contract.moduleSha256,
          "restoring the source argument restores its exact fingerprint");
    auto input = argument.element;
    const auto originalType = input.getType();
    input.setType(attributes.getI64Type());
    HostContractAnalysis typed(*module, requested(*module));
    check(!typed.proved() && typed.callables().empty(),
          "a native typed argument cannot forge an original JavaScript DOM input");
    input.setType(originalType);
    auto entry = module->lookupSymbol<ctjs::FuncOp>(contract.entry);
    entry->setAttr("upvalue_count", attributes.getI32IntegerAttr(1));
    check(!HostContractAnalysis(*module, requested(*module)).proved(),
          "DOM Data inputs do not permit a captured entry activation");
    entry->setAttr("upvalue_count", attributes.getI32IntegerAttr(0));
    const unsigned completion = host.steps();
    for (unsigned budget : {0u, 1u, completion / 2, completion - 1}) {
        HostContractAnalysis limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  limited.callables().empty(),
              "incomplete DOM input censuses expose no partial family proof");
    }
    HostContractAnalysis exact(*module, contract, completion);
    check(exact.proved() && exact.steps() == completion,
          "the exact input proof budget rederives complete provenance");

    const std::string manifest = R"json({"version":1,"provider":"ctbrowser-dom-data-session-v1",
"module_sha256":")json" + contract.moduleSha256 +
                                 R"json(","entry":"script$0",
"roots":[{"binding":"host","properties":["slot"]}],"observations":["trace"],
"absent_bindings":[],"undefined_bindings":[],"initial_intrinsics":["Map"],
"element_parameters":[0]})json";
    auto parsed = ctcompile::ctnative::parseHostContract(manifest);
    check(parsed && parsed->provider == HostContract::Provider::ctbrowserDOMDataSession &&
              parsed->elementParameters == std::vector<unsigned>{0},
          "the DOM Data contract parses separate explicit input origins");
    if (!parsed) { llvm::consumeError(parsed.takeError()); }
    for (const char * indices : {"[]", "[1]", "[0,0]", "[-1]", "[0.5]"}) {
        auto invalid = ctcompile::ctnative::parseHostContract(
            replaced(manifest, "\"element_parameters\":[0]",
                     std::string("\"element_parameters\":") + indices));
        check(!invalid, "JSON DOM input declarations reject missing, duplicate or invalid indices");
        if (!invalid) { llvm::consumeError(invalid.takeError()); }
    }
    std::printf("DOM key inputs %s: %u rows, live mutation, contract and budget controls\n",
                prepared ? "prepared" : "source", rows);
}

} // namespace ctcompile::test::owned_global_shared_map
