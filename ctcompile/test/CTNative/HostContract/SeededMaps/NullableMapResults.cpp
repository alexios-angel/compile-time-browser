//===- NullableMapResults.cpp - Nullable Map getter results -----*- C++ -*-===//
//
// Host-contract proofs for a Map getter that returns either a String or null,
// and for the setter that consumes that result.
//
//===----------------------------------------------------------------------===//
#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

/// Checks the host-contract analysis on a Map getter that may return null.
/// \p scalar is the short-circuit String fixture from ConditionalMapResults,
/// and \p prepared selects its call_direct form. Each variant() row asserts
/// proved or refused, and checkBudgets() asserts that every truncated step
/// budget, forged attribute and live SSA edit withholds the proof.
void checkNullableMapResults(mlir::MLIRContext & context, const std::string & scalar,
                             bool prepared) {
    constexpr llvm::StringLiteral nullableReturn = R"MLIR(
    %present = ctjs.truthy %answer
    %nullable = scf.if %present -> (!ctjs.value) {
      scf.yield %answer : !ctjs.value
    } else {
      %null = ctjs.constant #ctjs.null
      scf.yield %null : !ctjs.value
    }
    ctjs.return %nullable
)MLIR";
    constexpr llvm::StringLiteral normalize = R"MLIR(
    %keyPresent = ctjs.truthy %entryKey
    %normalized = scf.if %keyPresent -> (!ctjs.value) {
      scf.yield %entryKey : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.string<"missing">
      scf.yield %missing : !ctjs.value
    }
    %written = ctjs.call %setter(%state, %normalized, %value)
)MLIR";
    constexpr llvm::StringLiteral write =
        "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    const auto nullable = replaced(scalar, "    ctjs.return %answer", nullableReturn);
    const auto normalized = replaced(nullable, write, normalize);
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullableMask = Alternatives::String | Alternatives::Null;
    // Checks a proved query has \p expectedCalls edges in source order: two
    // getter calls taking a Boolean and setter calls whose argument is typed by
    // \p expectedMask, exactly one of them fed by a getter result.
    const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & result,
                           unsigned expectedCalls, unsigned expectedMask) {
        auto getter = module.lookupSymbol<ctjs::FuncOp>("get$2");
        auto setter = module.lookupSymbol<ctjs::FuncOp>("put$3");
        const auto calls = result.callables();
        bool complete = calls.size() == expectedCalls;
        unsigned getters = 0, setters = 0, dependencies = 0;
        mlir::Operation * previous = nullptr;
        for (const auto & call : calls) {
            complete &= call.capturedMap && call.arguments.size() == 1 &&
                        (!previous || previous->isBeforeInBlock(call.call));
            previous = call.call;
            if (!call.capturedMap || call.arguments.size() != 1) { continue; }
            const auto & argument = call.arguments.front();
            auto direct = llvm::dyn_cast<ctjs::CallDirectOp>(call.call);
            const auto actuals =
                direct ? direct.getArgs() : llvm::cast<ctjs::CallOp>(call.call).getArgs();
            complete &= actuals.size() == (prepared ? 2u : 1u) &&
                        argument.actual == actuals[prepared ? 1 : 0];
            if (call.function == getter) {
                ++getters;
                complete &=
                    argument.parameter == getter.getBody().front().getArgument(prepared ? 4 : 3) &&
                    argument.alternatives.tag() == mlir::TypeID::get<ctjs::BooleanAttr>();
            } else if (call.function == setter) {
                ++setters;
                complete &=
                    argument.parameter == setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                    argument.alternatives ==
                        Alternatives{expectedMask & Alternatives::String, expectedMask, true};
                unsigned parameterFamilies = 0;
                for (const auto & parameters : call.capturedMap->parameters) {
                    if (parameters.function == setter) {
                        ++parameterFamilies;
                        complete &= parameters.alternatives.size() == 1 &&
                                    parameters.alternatives.front() == argument.alternatives;
                    }
                }
                complete &= parameterFamilies == 1;
                for (const auto & producer : calls) {
                    if (producer.function == getter &&
                        argument.actual == producer.call->getResult(0)) {
                        ++dependencies;
                        complete &= producer.call->isBeforeInBlock(call.call);
                    }
                }
            } else {
                complete = false;
            }
        }
        check(complete && getters == 2 && setters == expectedCalls - 2 && dependencies == 1,
              "nullable complete actual census preserves every source call and producer edge");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             unsigned expectedCalls = 3,
                             unsigned expectedMask = Alternatives::String | Alternatives::Null) {
        ++rows;
        auto module = parsed(context, program, "source/prepared nullable Map fixture parses");
        if (!module) { return; }
        const auto contract = mapContractFor(*module);
        HostContractAnalysis result(*module, contract);
        check(result.proved() == expected && !result.exhausted() &&
                  (expected || withheld(*module, result)),
              message);
        if (result.proved() != expected || result.exhausted()) {
            std::fprintf(stderr, "nullable Map host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, result.reason().str().c_str());
        }
        if (expected && result.proved()) { edges(*module, result, expectedCalls, expectedMask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nullable evidence leaves all source operations and operands intact");
    };
    variant(normalized, true, "finite nullable results reach a normalized String consumer");
    variant(nullable, true,
            "host primitive ownership does not promise a native nullable Map key carrier");
    variant(replaced(normalized, "%present = ctjs.truthy %answer", "%present = ctjs.truthy %flag"),
            true, "nullable ternary results preserve both independently checked alternatives");
    variant(replaced(normalized, "#ctjs.string<\"owned\">", "#ctjs.string<\"\">"), true,
            "a present empty String read reaches the independently proved null fallback", 3,
            Alternatives::Null);
    variant(replaced(normalized, "%normalized, %value)", "%normalized, %entryKey)"), true,
            "primitive nullable payload ownership is independent of native storage admission");
    variant(replaced(normalized, "    %answer = ctjs.call %mapGetter(%state, %seedKey)",
                     "    %answer = ctjs.call %mapGetter(%state, %payload)"),
            false, "null fallback cannot prove the tag of a missing Map read");
    variant(replaced(normalized,
                     "    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, "
                     "%payload)\n",
                     ""),
            false, "a nullable return cannot supply a missing guarded payload tag");
    variant(replaced(normalized, "    %keyPresent = ctjs.truthy %entryKey",
                     "    %keyPresent = ctjs.truthy %this"),
            false, "an unproved condition cannot normalize a nullable consumer");
    variant(replaced(normalized, "      scf.yield %missing : !ctjs.value",
                     "      scf.yield %state : !ctjs.value"),
            false, "nullable normalization cannot leak the captured Map through its fallback");
    variant(replaced(normalized, "%normalized, %value)", "%normalized, %state)"), false,
            "finite parameter evidence cannot authorize a cyclic Map payload");
    for (const char * yielded : {"%answer", "%null", "%entryKey", "%missing"}) {
        const auto terminator = std::string("      scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(normalized, terminator,
                         "      ctjs.store_global \"trace\", " + std::string(yielded) + "\n" +
                             terminator),
                false, "every nullable producer and consumer arm retains its late effect check");
    }
    // The normalized program with one more `put` call taking \p literal, placed
    // before the first getter call or before the final getter call.
    const auto extraActual = [&](const std::string & literal, bool before) {
        std::string addition = "    %extra = ctjs.constant " + literal +
                               "\n    %extraPutter = ctjs.get_property %owned[%putKey]\n";
        if (prepared) {
            addition += "    %extraEnvironment = ctjs.load_upvalue %extraPutter[0]\n"
                        "    %extraPut = ctjs.call_direct @put$3(%owned, %u, %extraPutter, "
                        "%extraEnvironment, %extra)\n";
        } else {
            addition += "    %extraPut = ctjs.call %extraPutter(%owned, %extra)\n";
        }
        const std::string marker = before     ? "    %priorGetter = ctjs.get_property"
                                   : prepared ? "    %getEnvironment = ctjs.load_upvalue"
                                              : "    %answer = ctjs.call %getter";
        return replaced(normalized, marker, addition + marker);
    };
    for (const bool before : {false, true}) {
        for (const char * literal : {"#ctjs.string<\"other\">", "#ctjs.null"}) {
            variant(extraActual(literal, before), true,
                    "all literal and nullable-result actuals join independently of call order", 4);
        }
        variant(extraActual("#ctjs.undefined", before), true,
                "a proved Undefined actual joins the closed String and Null parameter set", 4,
                nullableMask | Alternatives::Undefined);
        for (const char * literal : {"#ctjs.boolean<true>", "#ctjs.number<0>"}) {
            variant(extraActual(literal, before), false,
                    "an incompatible actual cannot borrow the nullable producer's argument proof");
        }
    }
    for (const bool nullFirst : {false, true}) {
        const auto program = extraActual(nullFirst ? "#ctjs.null" : "#ctjs.undefined", true);
        std::string addition = "    %lastActual = ctjs.constant " +
                               std::string(nullFirst ? "#ctjs.undefined" : "#ctjs.null") +
                               "\n    %lastPutter = ctjs.get_property %owned[%putKey]\n";
        if (prepared) {
            addition += "    %lastEnvironment = ctjs.load_upvalue %lastPutter[0]\n"
                        "    %lastPut = ctjs.call_direct @put$3(%owned, %u, %lastPutter, "
                        "%lastEnvironment, %lastActual)\n";
        } else {
            addition += "    %lastPut = ctjs.call %lastPutter(%owned, %lastActual)\n";
        }
        constexpr llvm::StringLiteral marker = "    %priorGetter = ctjs.get_property";
        variant(replaced(program, marker, addition + marker.str()), true,
                "an early Null/Undefined prefix waits for the complete nullable String census", 5,
                nullableMask | Alternatives::Undefined);
    }
    const auto extraString = extraActual("#ctjs.string<\"other\">", false);
    variant(replaced(extraString, "%extra = ctjs.constant #ctjs.string<\"other\">",
                     "%extra = ctjs.create_object"),
            false, "a later object actual withholds the entire nullable family");
    variant(replaced(extraString, "%extra = ctjs.constant #ctjs.string<\"other\">",
                     "%extra = ctjs.load_global \"unknown\""),
            false, "a later unknown global actual withholds the entire nullable family");
    // The parser checks call_direct arity, so the prepared form passes an
    // unknown actual (%this) instead; the source form drops the argument.
    const auto missingActual =
        prepared ? replaced(normalized, "%putEnvironment, %produced)", "%putEnvironment, %this)")
                 : replaced(normalized, "%putter(%owned, %produced)", "%putter(%owned)");
    variant(missingActual, false,
            prepared ? "an unknown prepared nullable consumer actual supplies no primitive seed"
                     : "an omitted nullable consumer actual is not a null seed");
    variant(replaced(normalized, "    ctjs.return %nullable", "    ctjs.return %state"), false,
            "a returned captured Map cannot borrow a finite nullable signature");
    const auto returnsFlag = replaced(normalized,
                                      "    %u = ctjs.constant #ctjs.undefined\n"
                                      "    ctjs.return %u\n  }\n}\n",
                                      "    %u = ctjs.constant #ctjs.boolean<false>\n"
                                      "    ctjs.return %u\n  }\n}\n");
    variant(returnsFlag, true, "an independently checked sibling may return an exact Boolean");
    const auto returningDependency =
        prepared
            ? replaced(returnsFlag, "%getEnvironment, %falseFlag)", "%getEnvironment, %putResult)")
            : replaced(returnsFlag, "%getter(%owned, %falseFlag)", "%getter(%owned, %putResult)");
    variant(returningDependency, true,
            "source-order get-to-put-to-get results close an independently checked Boolean census");
    const auto checkBudgets = [&](const std::string & program, const char * label,
                                  unsigned expectedCalls) {
        auto module = parsed(context, program, "nullable budget fixture parses");
        if (!module) { return; }
        auto contract = mapContractFor(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        // 20000 is a ceiling well above this fixture's step count.
        check(complete.proved() && completion < 20000,
              "nullable complete actual census stays within the fixture work limit");
        if (!complete.proved() || completion >= 20000) { return; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete nullable budget withholds every result and parameter edge");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact nullable completion budget reproduces the complete census");
        if (exact.proved()) { edges(*module, exact, expectedCalls, nullableMask); }
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
        auto selection = returned.getValue().getDefiningOp<mlir::scf::IfOp>();
        mlir::scf::IfOp normalization;
        setter.walk([&](mlir::scf::IfOp branch) { normalization = branch; });
        check(selection && normalization,
              "the live nullable fixture retains its producer and normalization branches");
        if (!selection || !normalization) { return; }
        mlir::Builder builder(&context);
        module->walk([&](mlir::Operation * operation) {
            if (llvm::isa<ctjs::CallOp, ctjs::CallDirectOp, mlir::scf::IfOp>(operation)) {
                operation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
                operation->setAttr("ctnative.map_read_type", builder.getStringAttr("string"));
                operation->setAttr("ctnative.map_write_type", builder.getStringAttr("string"));
                operation->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            }
        });
        contract = mapContractFor(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged reports leave a complete nullable proof independently reproducible");
        const auto refusal = [&]() {
            HostContractAnalysis old(*module, contract);
            check(!old.proved() && old.reason().contains("fingerprint") && withheld(*module, old),
                  "live nullable mutation invalidates the previous host fingerprint");
            HostContractAnalysis fresh(*module, mapContractFor(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "a fresh fingerprint and forged markers cannot repair a nullable proof");
        };
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value replacement) {
            const auto previous = operation->getOperand(operand);
            operation->setOperand(operand, replacement);
            refusal();
            operation->setOperand(operand, previous);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the real nullable edge restores its complete proof");
        };
        auto nullYield =
            llvm::cast<mlir::scf::YieldOp>(selection.getElseRegion().front().getTerminator());
        auto normalizeTruthy = normalization.getCondition().getDefiningOp<ctjs::TruthyOp>();
        mutation(nullYield, 0, getter.getBody().front().getArgument(0));
        mutation(normalizeTruthy, 0, setter.getBody().front().getArgument(0));
        mlir::Operation * consumer = nullptr;
        for (const auto & call : exact.callables()) {
            if (call.function == setter) { consumer = call.call; }
        }
        check(consumer != nullptr, "the nullable consumer survives the live proof query");
        if (consumer) {
            // The setter argument: operand 4 of call_direct (this, new, callee,
            // state, arg), operand 2 of ctjs.call (callee, this, arg).
            const unsigned argument = prepared ? 4u : 2u;
            mutation(consumer, argument,
                     consumer->getParentOfType<ctjs::FuncOp>().getBody().front().getArgument(0));
            // A call that consumes its own result is invalid SSA. The analysis
            // must not seed its dependency worklist from that unproved result or
            // from a forged result attribute.
            mutation(consumer, argument, consumer->getResult(0));
        }
        mlir::OpBuilder at(returned);
        auto effect =
            ctjs::StoreGlobalOp::create(at, returned.getLoc(), "trace", returned.getValue());
        refusal();
        effect.erase();
        check(HostContractAnalysis(*module, contract).proved(),
              "removing a late effect restores only the original complete nullable proof");
        std::printf("%s Map host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    };
    // One row per variant() call above; update this when adding a case.
    check(rows == 32, "all nullable result, actual-census and refusal rows ran");
    checkBudgets(normalized, "nullable", 3);
    checkBudgets(extraActual("#ctjs.null", true), "nullable census", 4);
    checkNullablePayloadResults(context, nullable, prepared);
}

} // namespace ctcompile::test::host_contract_seeded_maps
