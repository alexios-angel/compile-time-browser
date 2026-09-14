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
                             unsigned calls = 1, unsigned inputs = 1) {
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
            for (unsigned index = 0; index < inputs; ++index) {
                expectedInputs.push_back(entry.getBody().front().getArgument(3 + index));
            }
            check(llvm::equal(owner.domInputs(), expectedInputs),
                  "DOM source ownership publishes only the actual external input origins");
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
    variant(distinct, false, "an unused input has no complete outer-key provenance");
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
