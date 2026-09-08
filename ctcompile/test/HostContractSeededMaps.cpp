// Host contract live proof queries over the shared two-method Map: seeded and
// per-key set facts typing a later get, possible-alias joins, SameValueZero
// keys, and every incomplete presence/join budget.
//
// SPLIT 2026-09-08: this is checkSeededMapResults, verbatim, out of a
// 1,068-line test/HostContract.cpp; it takes the same `shared` fixture that
// checkCapturedCallables used to build inline, now from HostContractFixtures.h.

#include "HostContractFixtures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/APFloat.h"

using namespace ctcompile::test::host_contract;

namespace {

void checkNullablePayloadResults(mlir::MLIRContext & context, const std::string & nullable,
                                 bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullableMask = Alternatives::String | Alternatives::Null;
    constexpr llvm::StringLiteral write =
        "    %written = ctjs.call %setter(%state, %entryKey, %entryKey)";
    constexpr llvm::StringLiteral read = "    %loaded = ctjs.call %reader(%state, %entryKey)";
    constexpr llvm::StringLiteral returned = "    ctjs.return %loaded";
    auto source =
        replaced(nullable, "    %written = ctjs.call %setter(%state, %entryKey, %value)", write);
    source = replaced(source,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                      "    %reader = ctjs.get_property %state[%getKey]\n" +
                          read.str() + "\n" + returned.str() + "\n  }\n}\n");
    source = replaced(source, "    ctjs.return %table",
                      "    %sizer = ctjs.create_closure %callee[4] this %u captures %" +
                          std::string(prepared ? "state" : "cell") +
                          "\n    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                          "    ctjs.set_property %table[%sizeKey], %sizer\n"
                          "    ctjs.return %table");
    const std::string environment = prepared ? ", %state: !ctjs.value" : "";
    source = replaced(source, "\n}\n",
                      "\n  ctjs.func private @size$4(%this: !ctjs.value, %new: !ctjs.value, "
                      "%callee: !ctjs.value" +
                          environment +
                          ", %entryKey: !ctjs.value) -> !ctjs.value attributes {upvalue_count = " +
                          std::string(prepared ? "0" : "1") + " : i32} {\n" +
                          (prepared ? "" : "    %state = ctjs.load_upvalue %callee[0]\n") + R"MLIR(
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %written = ctjs.call %setter(%state, %entryKey, %entryKey)
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %size = ctjs.get_property %state[%sizeKey]
    ctjs.return %size
  }
}
)MLIR");
    std::string consumer = "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                           "    %sizer = ctjs.get_property %owned[%sizeKey]\n";
    if (prepared) {
        consumer += "    %sizeEnvironment = ctjs.load_upvalue %sizer[0]\n"
                    "    %answer = ctjs.call_direct @size$4(%owned, %u, %sizer, "
                    "%sizeEnvironment, %putResult)";
        source = replaced(source,
                          "    %getEnvironment = ctjs.load_upvalue %getter[0]\n"
                          "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                          "%getEnvironment, %falseFlag)",
                          consumer);
    } else {
        consumer += "    %answer = ctjs.call %sizer(%owned, %putResult)";
        source = replaced(source, "    %answer = ctjs.call %getter(%owned, %falseFlag)", consumer);
    }
    source = replaced(source, "    %getter = ctjs.get_property %owned[%key]\n", "");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & query) {
        bool empty = query.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !query.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !query.property(read);
            }
        });
        return empty;
    };
    const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                           unsigned resultMask) {
        const auto calls = query.callables();
        bool complete = calls.size() == 3;
        for (unsigned index = 0; index < calls.size(); ++index) {
            const auto & call = calls[index];
            const char * name = index == 0 ? "get$2" : index == 1 ? "put$3" : "size$4";
            auto function = module.lookupSymbol<ctjs::FuncOp>(name);
            const unsigned mask = index == 0   ? Alternatives::Boolean
                                  : index == 1 ? nullableMask
                                               : resultMask;
            const Alternatives expected{
                mask & (Alternatives::Boolean | Alternatives::Number | Alternatives::String), mask,
                true};
            complete &= call.function == function && call.capturedMap && call.arguments.size() == 1;
            if (!function || !call.capturedMap || call.arguments.size() != 1) { continue; }
            const auto & argument = call.arguments.front();
            complete &=
                argument.parameter == function.getBody().front().getArgument(prepared ? 4 : 3) &&
                argument.actual == call.call->getOperand(prepared ? 4 : 2) &&
                argument.alternatives == expected;
            if (index) {
                complete &= argument.actual == calls[index - 1].call->getResult(0) &&
                            calls[index - 1].call->isBeforeInBlock(call.call);
            }
            unsigned families = 0;
            for (const auto & parameters : call.capturedMap->parameters) {
                if (parameters.function != function) { continue; }
                ++families;
                complete &= parameters.alternatives == std::vector{expected};
            }
            complete &= families == 1;
        }
        check(complete,
              "get, nullable payload readback and size retain independent complete SSA families");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             unsigned resultMask = Alternatives::String | Alternatives::Null) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nullable payload fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "nullable payload host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { edges(*module, query, resultMask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nullable payload proof preserves every source operation and operand");
    };
    variant(source, true,
            "a definitely present nullable Map read feeds a distinct published method");
    const auto conditional = replaced(source, write, R"MLIR(
    %present = ctjs.truthy %entryKey
    scf.if %present {
      %left = ctjs.call %setter(%state, %entryKey, %entryKey)
      scf.yield
    } else {
      %null = ctjs.constant #ctjs.null
      %right = ctjs.call %setter(%state, %entryKey, %null)
      scf.yield
    }
)MLIR");
    variant(conditional, true,
            "both reaching writes independently retain String and Null payloads");
    variant(replaced(source, read, R"MLIR(
    %present = ctjs.truthy %entryKey
    scf.if %present {
      %null = ctjs.constant #ctjs.null
      %right = ctjs.call %setter(%state, %entryKey, %null)
      scf.yield
    }
)MLIR" + read.str()),
            true, "a no-else nullable overwrite joins with the incoming payload");
    const auto undefined = replaced(source, write, R"MLIR(
    %present = ctjs.truthy %entryKey
    %payload = scf.if %present -> (!ctjs.value) {
      scf.yield %entryKey : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.undefined
      scf.yield %missing : !ctjs.value
    }
    %written = ctjs.call %setter(%state, %entryKey, %payload)
)MLIR");
    variant(undefined, true, "a conditional payload read preserves String and Undefined",
            Alternatives::String | Alternatives::Undefined);
    constexpr llvm::StringLiteral alias = R"MLIR(
    %aliasKey = ctjs.constant #ctjs.string<"possible">
    %aliasValue = ctjs.constant #ctjs.null
    %aliased = ctjs.call %setter(%state, %aliasKey, %aliasValue)
)MLIR";
    const auto joined = replaced(source, read, alias.str() + read.str());
    variant(joined, true, "a possibly aliasing Null write retains the finite nullable payload set");
    variant(replaced(joined, "%aliasValue = ctjs.constant #ctjs.null",
                     "%aliasValue = ctjs.constant #ctjs.boolean<true>"),
            false, "a possible truthy Boolean alias cannot borrow the nullable consumer signature");
    variant(replaced(joined, "%aliasValue = ctjs.constant #ctjs.null",
                     "%aliasValue = ctjs.load_global \"unknown\""),
            false, "a possible unknown write cannot be repaired by prior finite payload evidence");
    constexpr llvm::StringLiteral erase = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %erased = ctjs.call %deleter(%state, %entryKey)
)MLIR";
    const auto saved = replaced(source, returned, R"MLIR(
    %changed = ctjs.constant #ctjs.boolean<true>
    %overwrite = ctjs.call %setter(%state, %entryKey, %changed)
)MLIR" + erase.str() + returned.str());
    variant(saved, true, "saved nullable SSA read evidence survives exact overwrite and deletion");
    variant(replaced(source, read, erase.str() + read.str()), false,
            "payload categories cannot authorize a read after its membership was deleted");
    variant(replaced(source, write, ""), false,
            "a captured Map starts with unknown contents at each invocation");
    variant(replaced(source, read,
                     "    %missingKey = ctjs.constant #ctjs.string<\"missing\">\n"
                     "    %loaded = ctjs.call %reader(%state, %missingKey)"),
            false, "a different key cannot inherit the nullable entry's definite presence");
    variant(replaced(source, write, "    %written = ctjs.call %setter(%state, %entryKey, %this)"),
            false, "an unproved payload cannot inherit the nullable input's alternatives");
    variant(replaced(conditional, "      %right = ctjs.call %setter(%state, %entryKey, %null)", ""),
            false, "one reaching write cannot prove membership on both arms");
    variant(replaced(conditional, "%null = ctjs.constant #ctjs.null",
                     "%null = ctjs.constant #ctjs.boolean<true>"),
            false, "a truthy mixed branch remains outside the nullable consumer parameter family");
    variant(replaced(saved, returned,
                     "    %fresh = ctjs.call %reader(%state, %entryKey)\n"
                     "    ctjs.return %fresh"),
            false, "a fresh deleted read cannot borrow the saved SSA read's alternatives");
    variant(replaced(source, read, "    %loaded = ctjs.call %reader(%this, %entryKey)"), false,
            "a different receiver cannot inherit captured Map payload evidence");
    check(rows == 16, "all nullable payload propagation and refusal rows ran");
    for (const auto & [program, label] :
         {std::pair{source, "nullable payload"}, std::pair{conditional, "nullable payload join"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "nullable payload budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000,
              "the nullable payload dependency proof stays within its fixture work limit");
        if (!complete.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(
                !limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                    withheld(*module, limited),
                "every incomplete payload budget withholds the whole callable and property proof");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact payload budget reproduces all dependency edges");
        if (exact.proved()) { edges(*module, exact, nullableMask); }
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        auto result = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        auto lookup = result.getValue().getDefiningOp<ctjs::CallOp>();
        check(static_cast<bool>(lookup), "the nullable payload producer retains its source read");
        if (!lookup) { continue; }
        mlir::Builder builder(&context);
        module->walk([&](mlir::Operation * operation) {
            if (!llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation)) { return; }
            operation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", builder.getStringAttr("Opt<Str>"));
        });
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged nullable reports leave the live proof independently reproducible");
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value replacement) {
            const auto original = operation->getOperand(operand);
            operation->setOperand(operand, replacement);
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live payload mutation invalidates its earlier host fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh forged reports cannot recover unknown nullable payload evidence");
            operation->setOperand(operand, original);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the nullable payload edge restores the complete proof");
        };
        const auto unknown = setter.getBody().front().getArgument(0);
        mutation(lookup, 2, unknown);
        mutation(result, 0, lookup.getReceiver());
        setter.walk([&](ctjs::CallOp call) {
            if (call.getArgs().size() == 2) { mutation(call, 3, unknown); }
        });
        std::printf("%s host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    }
}

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
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & result) {
        bool empty = result.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !result.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !result.property(read);
            }
        });
        return empty;
    };
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullableMask = Alternatives::String | Alternatives::Null;
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
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nullable Map fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
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
    // Prepared direct calls verify arity while parsing. Keep that form valid
    // and test an unknown actual; source calls also exercise missing arity.
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
    const auto cyclicParameters =
        prepared
            ? replaced(returnsFlag, "%getEnvironment, %falseFlag)", "%getEnvironment, %putResult)")
            : replaced(returnsFlag, "%getter(%owned, %falseFlag)", "%getter(%owned, %putResult)");
    variant(cyclicParameters, false,
            "one seeded actual cannot close an unresolved cyclic result and parameter census");
    const auto checkBudgets = [&](const std::string & program, const char * label,
                                  unsigned expectedCalls) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "nullable budget fixture parses");
        if (!module) { return; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
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
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged reports leave a complete nullable proof independently reproducible");
        const auto refusal = [&]() {
            HostContractAnalysis old(*module, contract);
            check(!old.proved() && old.reason().contains("fingerprint") && withheld(*module, old),
                  "live nullable mutation invalidates the previous host fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
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
            const unsigned argument = prepared ? 4u : 2u;
            mutation(consumer, argument,
                     consumer->getParentOfType<ctjs::FuncOp>().getBody().front().getArgument(0));
            // An invalid cyclic SSA edit must not bootstrap the dependency
            // worklist from its own unproved result or a forged result marker.
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
    check(rows == 32, "all nullable result, actual-census and refusal rows ran");
    checkBudgets(normalized, "nullable", 3);
    checkBudgets(extraActual("#ctjs.null", true), "nullable census", 4);
    checkNullablePayloadResults(context, nullable, prepared);
}

void checkConditionalMapResults(mlir::MLIRContext & context, std::string source, bool prepared) {
    const std::string header =
        "@get$2(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value" +
        std::string(prepared ? ", %state: !ctjs.value" : "");
    source = replaced(source, header + ")", header + ", %flag: !ctjs.value)");
    source = replaced(source, "    %priorGetter = ctjs.get_property",
                      "    %trueFlag = ctjs.constant #ctjs.boolean<true>\n"
                      "    %falseFlag = ctjs.constant #ctjs.boolean<false>\n"
                      "    %priorGetter = ctjs.get_property");
    if (prepared) {
        source = replaced(source, "%priorEnvironment)", "%priorEnvironment, %trueFlag)");
        source = replaced(source, "%getEnvironment)", "%getEnvironment, %falseFlag)");
    } else {
        source = replaced(source, "%produced = ctjs.call %priorGetter(%owned)",
                          "%produced = ctjs.call %priorGetter(%owned, %trueFlag)");
        source = replaced(source, "%answer = ctjs.call %getter(%owned)",
                          "%answer = ctjs.call %getter(%owned, %falseFlag)");
    }
    constexpr llvm::StringLiteral continuation = R"MLIR(
    %changedPayload = ctjs.constant #ctjs.boolean<false>
    %overwritten = ctjs.call %mapSetter(%state, %seedKey, %changedPayload)
    %writeback = ctjs.call %mapSetter(%state, %seedKey, %saved)
    %answer = ctjs.call %mapGetter(%state, %seedKey)
)MLIR";
    constexpr llvm::StringLiteral selection = R"MLIR(
    %unused = arith.constant 0 : i32
    %branch = ctjs.truthy %flag
    %saved = scf.if %branch -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %left : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %right : !ctjs.value
    }
)MLIR";
    source = replaced(source, "    %answer = ctjs.call %mapGetter(%state, %probeKey)",
                      selection.str() + continuation.str());
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & result) {
        bool empty = result.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !result.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !result.property(read);
            }
        });
        return empty;
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             mlir::TypeID tag = mlir::TypeID::get<ctjs::NumberAttr>()) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared conditional Map fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis result(*module, contract);
        check(result.proved() == expected && !result.exhausted() &&
                  (expected || withheld(*module, result)),
              message);
        if (result.proved() != expected || result.exhausted()) {
            std::fprintf(stderr, "conditional Map host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, result.reason().str().c_str());
        }
        if (expected && result.proved()) {
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
            const auto calls = result.callables();
            check(calls.size() == 3 && calls[0].function == getter && calls[1].function == setter &&
                      calls[2].function == getter && calls[0].arguments.size() == 1 &&
                      calls[1].arguments.size() == 1 && calls[2].arguments.size() == 1 &&
                      calls[0].arguments.front().alternatives.tag() ==
                          mlir::TypeID::get<ctjs::BooleanAttr>() &&
                      calls[1].arguments.front().alternatives.tag() == tag &&
                      calls[1].arguments.front().actual == calls[0].call->getResult(0) &&
                      calls[1].arguments.front().parameter ==
                          setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                      calls[0].call->isBeforeInBlock(calls[1].call) &&
                      calls[1].call->isBeforeInBlock(calls[2].call),
                  "conditional producer results reach the consumer without selecting a call value");
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "conditional proofs retain every source operation and operand");
    };
    variant(source, true, "same-tag scalar yields survive overwrite and later writeback");
    for (const auto & [payload, tag] :
         {std::pair{"#ctjs.boolean<true>", mlir::TypeID::get<ctjs::BooleanAttr>()},
          std::pair{"#ctjs.string<\"owned\">", mlir::TypeID::get<ctjs::StringAttr>()}}) {
        variant(replaced(source, "#ctjs.number<4607182418800017408>", payload), true,
                "Boolean and owning String joins retain independent scalar result tags", tag);
    }
    const auto missing =
        replaced(source, "%mapGetter(%state, %probeKey)", "%mapGetter(%state, %payload)");
    variant(missing, false, "one missing arm cannot supply a scalar tag to the consumer");
    variant(replaced(missing, "%branch = ctjs.truthy %flag",
                     "%always = ctjs.constant #ctjs.boolean<true>\n"
                     "    %branch = ctjs.truthy %always"),
            false, "a literal predicate cannot exempt the other arm from the complete body proof");
    variant(replaced(source, "scf.yield %right : !ctjs.value",
                     "%wrong = ctjs.constant #ctjs.boolean<false>\n"
                     "      scf.yield %wrong : !ctjs.value"),
            false, "different yielded tags cannot be joined into a definite consumer category");
    for (const char * yielded : {"%left", "%right"}) {
        const std::string terminator = std::string("scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(source, terminator,
                         "ctjs.store_global \"trace\", " + std::string(yielded) + "\n      " +
                             terminator),
                false, "a late unsupported effect in either arm rejects the entire family");
        for (const char * escaped : {"%state", "%mapGetter"}) {
            variant(replaced(source, terminator,
                             std::string("scf.yield ") + escaped + " : !ctjs.value"),
                    false, "Map and method values cannot escape through a scalar yield");
        }
    }
    for (const char * object : {"%state", "%callee", "%this"}) {
        variant(replaced(source, "%branch = ctjs.truthy %flag",
                         std::string("%branch = ctjs.truthy ") + object),
                false, "truthiness cannot authorize a nonprimitive or unproved input");
    }
    const auto deleted = replaced(source, "      scf.yield %left : !ctjs.value", R"MLIR(
      %deleteKey = ctjs.constant #ctjs.string<"delete">
      %deleter = ctjs.get_property %state[%deleteKey]
      %erased = ctjs.call %deleter(%state, %seedKey)
      scf.yield %left : !ctjs.value
)MLIR");
    variant(deleted, true, "a saved scalar survives a source deletion on just one arm");
    variant(
        replaced(deleted, continuation, "    %answer = ctjs.call %mapGetter(%state, %seedKey)\n"),
        false, "contents intersection drops membership deleted on one reaching arm");
    const auto changed = replaced(source, "      scf.yield %left : !ctjs.value", R"MLIR(
      %boolean = ctjs.constant #ctjs.boolean<false>
      %written = ctjs.call %mapSetter(%state, %seedKey, %boolean)
      scf.yield %left : !ctjs.value
)MLIR");
    variant(
        replaced(changed, continuation, "    %answer = ctjs.call %mapGetter(%state, %seedKey)\n"),
        false, "contents intersection clears a payload changed on one reaching arm");
    variant(changed, true, "a joined saved scalar can reseed after a one-arm payload overwrite");
    for (const unsigned depth : {4u, 32u, 33u}) {
        std::string nested = "      %left = ctjs.call %mapGetter(%state, %seedKey)\n";
        for (unsigned index = 1; index < depth; ++index) {
            const std::string prior = index == 1 ? "%left" : "%nested" + std::to_string(index - 1);
            nested = "      %nested" + std::to_string(index) +
                     " = scf.if %branch -> (!ctjs.value) {\n" + nested + "      scf.yield " +
                     prior +
                     " : !ctjs.value\n"
                     "    } else {\n      scf.yield %payload : !ctjs.value\n    }\n";
        }
        nested += "      scf.yield %nested" + std::to_string(depth - 1) + " : !ctjs.value";
        variant(replaced(source,
                         "      %left = ctjs.call %mapGetter(%state, %seedKey)\n"
                         "      scf.yield %left : !ctjs.value",
                         nested),
                depth <= 32, "nested conditionals respect the bounded complete-body depth");
    }
    const auto noElse = replaced(source, "    %saved = scf.if", R"MLIR(
    scf.if %branch {
      %effect = ctjs.call %mapSetter(%state, %seedKey, %payload)
      scf.yield
    }
    %saved = scf.if)MLIR");
    variant(noElse, true,
            "an absent else preserves the incoming same-tag entry on its implicit path");
    const auto literalNoElse = replaced(noElse, "%branch = ctjs.truthy %flag",
                                        "%never = ctjs.constant #ctjs.boolean<false>\n"
                                        "    %branch = ctjs.truthy %never");
    constexpr llvm::StringLiteral effect =
        "      %effect = ctjs.call %mapSetter(%state, %seedKey, %payload)";
    variant(replaced(literalNoElse, effect, R"MLIR(
      %deleteKey = ctjs.constant #ctjs.string<"delete">
      %deleter = ctjs.get_property %state[%deleteKey]
      %effect = ctjs.call %deleter(%state, %seedKey)
)MLIR"),
            false, "a no-else delete loses membership even behind a literal false predicate");
    variant(replaced(literalNoElse, effect,
                     "      %different = ctjs.constant #ctjs.boolean<false>\n"
                     "      %effect = ctjs.call %mapSetter(%state, %seedKey, %different)"),
            false, "a no-else incompatible write loses its tag even behind a literal predicate");

    auto guarded = replaced(source, "%probeKey = ctjs.constant #ctjs.number<0>", R"MLIR(
    %probeKey = ctjs.constant #ctjs.number<4611686018427387904>
    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, %payload)
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %hasKey = ctjs.constant #ctjs.string<"has">
    %mapHas = ctjs.get_property %state[%hasKey]
)MLIR");
    guarded = replaced(guarded, selection, R"MLIR(
    %branch = ctjs.truthy %flag
    scf.if %branch {
      %erased = ctjs.call %deleter(%state, %probeKey)
      scf.yield
    }
    %observed = ctjs.call %mapHas(%state, %probeKey)
    %guard = ctjs.truthy %observed
    %saved = scf.if %guard -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %left : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %right : !ctjs.value
    }
)MLIR");
    variant(guarded, true, "a fresh has restores membership without inventing its payload tag");
    for (const auto & [payload, tag] :
         {std::pair{"#ctjs.boolean<true>", mlir::TypeID::get<ctjs::BooleanAttr>()},
          std::pair{"#ctjs.string<\"owned\">", mlir::TypeID::get<ctjs::StringAttr>()}}) {
        variant(replaced(guarded, "#ctjs.number<4607182418800017408>", payload), true,
                "guarded Boolean and String reads retain tags across a conditional deletion", tag);
    }
    constexpr llvm::StringLiteral observed =
        "    %observed = ctjs.call %mapHas(%state, %probeKey)\n";
    constexpr llvm::StringLiteral guard = "    %guard = ctjs.truthy %observed";
    constexpr llvm::StringLiteral erase = "%erased = ctjs.call %deleter(%state, %probeKey)";
    const auto staleHas = replaced(replaced(guarded, observed, ""), "    scf.if %branch {",
                                   observed.str() + "    scf.if %branch {");
    variant(staleHas, false, "a has snapshot cannot survive deletion on one structural arm");
    variant(replaced(guarded, guard,
                     "    %later = ctjs.call %deleter(%state, %probeKey)\n" + guard.str()),
            false, "a later same-key deletion invalidates the has implication");
    variant(replaced(guarded, observed, "    %observed = ctjs.call %mapHas(%state, %seedKey)\n"),
            false, "a live has for another key cannot restore the deleted key");
    variant(replaced(guarded, "%mapHas = ctjs.get_property %state[%hasKey]",
                     "%mapHas = ctjs.get_property %this[%hasKey]"),
            false, "an unproved Map receiver cannot provide a has guard");
    const auto noTag = replaced(
        guarded, "    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, %payload)\n", "");
    variant(noTag, false, "has membership cannot type unknown contents from a prior invocation");
    variant(replaced(noTag, erase, "%erased = ctjs.call %mapSetter(%state, %probeKey, %payload)"),
            false, "a tag on one arm cannot be unioned with unknown incoming contents");
    variant(replaced(guarded, erase,
                     "%different = ctjs.constant #ctjs.boolean<false>\n"
                     "      %erased = ctjs.call %mapSetter(%state, %probeKey, %different)"),
            false, "has membership cannot choose between differing payload tags across a join");
    for (const char * literal : {"true", "false"}) {
        variant(replaced(guarded, guard,
                         "    %literal = ctjs.constant #ctjs.boolean<" + std::string(literal) +
                             ">\n    %guard = ctjs.truthy %literal"),
                false, "a literal predicate supplies no missing membership proof on either arm");
    }
    auto premature =
        replaced(guarded, observed,
                 "    %premature = ctjs.call %mapGetter(%state, %probeKey)\n" + observed.str());
    premature = replaced(premature, "      %left = ctjs.call %mapGetter(%state, %probeKey)\n", "");
    variant(
        replaced(premature, "scf.yield %left : !ctjs.value", "scf.yield %premature : !ctjs.value"),
        false, "a later has guard cannot retroactively type an earlier read");
    const auto boolKey =
        replaced(guarded, "#ctjs.number<4611686018427387904>", "#ctjs.boolean<true>");
    variant(replaced(boolKey, observed,
                     "    %possible = ctjs.call %mapSetter(%state, %flag, %payload)\n" +
                         observed.str()),
            true, "possible same-tag writes preserve a guarded key's payload evidence");
    variant(
        replaced(boolKey, observed,
                 "    %possible = ctjs.call %mapSetter(%state, %flag, %flag)\n" + observed.str()),
        false, "a possible incompatible write clears the guarded payload evidence");
    variant(replaced(boolKey, guard,
                     "    %possible = ctjs.call %deleter(%state, %flag)\n" + guard.str()),
            false, "a possible alias deletion invalidates a saved has observation");
    variant(replaced(guarded, guard, R"MLIR(
    %disjoint = ctjs.constant #ctjs.number<4613937818241073152>
    %unrelated = ctjs.call %deleter(%state, %disjoint)
)MLIR" + guard.str()),
            true, "an independently disjoint deletion preserves the live has observation");
    for (const char * yielded : {"%left", "%right"}) {
        const std::string terminator = std::string("scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(guarded, terminator,
                         "ctjs.store_global \"trace\", " + std::string(yielded) + "\n      " +
                             terminator),
                false, "a has guard cannot hide an unsupported effect on either structural arm");
    }

    constexpr llvm::StringLiteral guardedSelection = R"MLIR(
    %saved = scf.if %guard -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %left : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %right : !ctjs.value
    }
)MLIR";
    constexpr llvm::StringLiteral shortSelection = R"MLIR(
    %intermediate = scf.if %guard -> (!ctjs.value) {
      %left = ctjs.call %mapGetter(%state, %probeKey)
      scf.yield %left : !ctjs.value
    } else {
      scf.yield %observed : !ctjs.value
    }
    %selected = ctjs.truthy %intermediate
    %saved = scf.if %selected -> (!ctjs.value) {
      scf.yield %intermediate : !ctjs.value
    } else {
      %right = ctjs.call %mapGetter(%state, %seedKey)
      scf.yield %right : !ctjs.value
    }
)MLIR";
    const auto shortCircuit = replaced(guarded, guardedSelection, shortSelection);
    const auto shortString =
        replaced(shortCircuit, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">");
    constexpr llvm::StringLiteral falseYield = "      scf.yield %observed : !ctjs.value";
    constexpr llvm::StringLiteral select = "    %selected = ctjs.truthy %intermediate";
    variant(shortCircuit, true, "false or Number alternatives produce a same-tag Number result");
    variant(shortString, true, "false or String alternatives produce a same-tag owning result",
            mlir::TypeID::get<ctjs::StringAttr>());
    variant(replaced(shortCircuit, "#ctjs.number<4607182418800017408>", "#ctjs.boolean<true>"),
            true, "Boolean short-circuit results retain their independent scalar tag",
            mlir::TypeID::get<ctjs::BooleanAttr>());
    for (const auto & [program, tag] :
         {std::pair{&shortCircuit, mlir::TypeID::get<ctjs::NumberAttr>()},
          std::pair{&shortString, mlir::TypeID::get<ctjs::StringAttr>()}}) {
        for (const char * literal :
             {"#ctjs.boolean<false>", "#ctjs.number<0>", "#ctjs.number<9223372036854775808>",
              "#ctjs.number<9221120237041090561>", "#ctjs.string<\"\">", "#ctjs.null",
              "#ctjs.undefined"}) {
            variant(replaced(*program, falseYield,
                             "      %falsy = ctjs.constant " + std::string(literal) +
                                 "\n      scf.yield %falsy : !ctjs.value"),
                    true, "known falsy alternatives stay internal to a same-tag scalar result",
                    tag);
        }
        variant(replaced(*program, falseYield,
                         "      %other = ctjs.constant #ctjs.boolean<true>\n"
                         "      scf.yield %other : !ctjs.value"),
                false, "a genuinely truthy Boolean cannot disappear from a mixed scalar result");
        variant(replaced(*program, falseYield, "      scf.yield %flag : !ctjs.value"), false,
                "an unrelated Boolean retains its true alternative on the false has arm");
    }
    variant(replaced(shortCircuit, "      %left = ctjs.call %mapGetter(%state, %probeKey)",
                     "      %left = ctjs.call %mapGetter(%state, %payload)"),
            false, "truthiness cannot establish a scalar tag for a missing guarded read");
    variant(replaced(shortCircuit, "      %right = ctjs.call %mapGetter(%state, %seedKey)",
                     "      %right = ctjs.call %mapGetter(%state, %payload)"),
            false, "the short-circuit fallback needs its own definite same-tag read");
    variant(
        replaced(shortCircuit, observed, "    %observed = ctjs.call %mapHas(%state, %seedKey)\n"),
        false, "a different-key has cannot type the short-circuit's guarded read");
    variant(replaced(shortCircuit,
                     "    %otherSeed = ctjs.call %mapSetter(%state, %probeKey, "
                     "%payload)\n",
                     ""),
            false, "a short-circuit cannot turn unknown prior Map contents into a scalar");
    for (const bool truth : {false, true}) {
        variant(
            replaced(shortCircuit, erase,
                     "%different = ctjs.constant #ctjs.boolean<" +
                         std::string(truth ? "true" : "false") +
                         ">\n      %erased = ctjs.call %mapSetter(%state, %probeKey, %different)"),
            !truth,
            truth ? "a truthy Boolean payload survives refinement and keeps a mixed result"
                  : "an exact false payload takes the independently proved Number fallback");
    }
    variant(replaced(replaced(shortCircuit, observed, ""), "    scf.if %branch {",
                     observed.str() + "    scf.if %branch {"),
            false, "a short-circuit cannot reuse has membership from before conditional deletion");
    variant(replaced(shortCircuit, guard,
                     "    %later = ctjs.call %deleter(%state, %probeKey)\n" + guard.str()),
            false, "deletion invalidates the guard before either short-circuit result exists");
    for (const char * literal : {"true", "false"}) {
        variant(replaced(shortCircuit, guard,
                         "    %literal = ctjs.constant #ctjs.boolean<" + std::string(literal) +
                             ">\n    %guard = ctjs.truthy %literal"),
                false, "literal predicates cannot hide the short-circuit's missing read arm");
    }
    variant(replaced(shortCircuit, select, "    %selected = ctjs.truthy %flag"), false,
            "truthiness of another SSA value cannot refine an intermediate union");
    variant(replaced(shortCircuit, "%writeback = ctjs.call %mapSetter(%state, %seedKey, %saved)",
                     "%writeback = ctjs.call %mapSetter(%state, %seedKey, %intermediate)"),
            false, "the outer true arm cannot leak its refinement past the structural join");
    for (const char * yielded : {"%left", "%observed", "%intermediate", "%right"}) {
        const std::string terminator = std::string("scf.yield ") + yielded + " : !ctjs.value";
        variant(replaced(shortCircuit, terminator,
                         "ctjs.store_global \"trace\", " + std::string(yielded) + "\n      " +
                             terminator),
                false, "every short-circuit arm is checked through its last effect");
    }
    variant(replaced(shortString, select,
                     "    %afterRead = ctjs.call %deleter(%state, %probeKey)\n" + select.str()),
            true, "a saved false or String result survives deletion before truthy refinement",
            mlir::TypeID::get<ctjs::StringAttr>());
    variant(replaced(shortString, select,
                     "    %replacement = ctjs.constant #ctjs.boolean<false>\n"
                     "    %afterRead = ctjs.call %mapSetter(%state, %probeKey, %replacement)\n" +
                         select.str()),
            true, "a saved scalar alternative is independent of subsequent payload overwrites",
            mlir::TypeID::get<ctjs::StringAttr>());

    const auto checkBudgets = [&](const std::string & program, const char * label) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        if (!module) { return; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 15000,
              "the complete conditional proof stays within its fixture work limit");
        if (!complete.proved() || completion >= 15000) { return; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      withheld(*module, limited),
                  "every incomplete conditional budget withholds all callable and argument edges");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion && exact.callables().size() == 3,
              "the exact conditional budget reproduces the completed family");
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
        mlir::scf::IfOp branch, guardedBranch;
        ctjs::CallOp seed, left, right, observedCall;
        getter.walk([&](mlir::scf::IfOp conditional) {
            branch = conditional;
            auto truthy = conditional.getCondition().getDefiningOp<ctjs::TruthyOp>();
            if (truthy) {
                if (auto call = truthy.getValue().getDefiningOp<ctjs::CallOp>()) {
                    observedCall = call;
                    guardedBranch = conditional;
                }
            }
        });
        getter.walk([&](ctjs::CallOp call) {
            if (!seed) { seed = call; }
            if (branch && call->getParentRegion() == &branch.getElseRegion()) { right = call; }
            if (guardedBranch && call->getParentRegion() == &guardedBranch.getThenRegion()) {
                left = call;
            }
        });
        check(branch && seed && right,
              "the live conditional fixture retains its seed and both arms");
        if (!branch || !seed || !right) { return; }
        mlir::Builder builder(&context);
        right->setAttr("ctnative.map_present", builder.getBoolAttr(true));
        right->setAttr("ctnative.map_read_type", builder.getStringAttr("number"));
        branch->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged reports leave an otherwise complete conditional proof reproducible");
        const auto originalKey = right.getArgs()[0];
        right->setOperand(2, seed.getArgs()[1]);
        HostContractAnalysis stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && withheld(*module, stale),
              "a changed conditional arm cannot reuse the previous fingerprint");
        HostContractAnalysis fresh(*module, requested(*module));
        check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
              "fresh forged tags cannot recover the missing arm or its consumer argument");
        right->setOperand(2, originalKey);
        check(HostContractAnalysis(*module, contract).proved(),
              "restoring the real arm restores its complete conditional result proof");
        if (observedCall) {
            const auto watchedKey = observedCall.getArgs()[0];
            observedCall->setOperand(2, seed.getArgs()[0]);
            HostContractAnalysis staleGuard(*module, contract);
            check(!staleGuard.proved() && staleGuard.reason().contains("fingerprint") &&
                      withheld(*module, staleGuard),
                  "a live guard-key edit cannot reuse its earlier host fingerprint");
            HostContractAnalysis freshGuard(*module, requested(*module));
            check(!freshGuard.proved() && !freshGuard.exhausted() && withheld(*module, freshGuard),
                  "a fresh forged report cannot turn a different-key has into membership");
            observedCall->setOperand(2, watchedKey);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the real guard restores its complete saved-value proof");
        }
        if (guardedBranch && guardedBranch != branch && left) {
            left->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            left->setAttr("ctnative.map_read_type", builder.getStringAttr("string"));
            guardedBranch->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            contract = requested(*module);
            check(HostContractAnalysis(*module, contract).proved(),
                  "forged intermediate tags leave the complete short-circuit proof reproducible");
            const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                      mlir::Value replacement) {
                const auto previous = operation->getOperand(operand);
                operation->setOperand(operand, replacement);
                HostContractAnalysis old(*module, contract);
                check(!old.proved() && old.reason().contains("fingerprint") &&
                          withheld(*module, old),
                      "a changed short-circuit SSA edge invalidates its previous fingerprint");
                HostContractAnalysis changed(*module, requested(*module));
                check(!changed.proved() && !changed.exhausted() && withheld(*module, changed),
                      "fresh forged tags cannot recover an invalid short-circuit scalar proof");
                operation->setOperand(operand, previous);
                check(HostContractAnalysis(*module, contract).proved(),
                      "restoring a real short-circuit SSA edge restores the complete proof");
            };
            auto truthy = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto falseArm = llvm::cast<mlir::scf::YieldOp>(
                guardedBranch.getElseRegion().front().getTerminator());
            const auto flag = getter.getBody().front().getArgument(prepared ? 4 : 3);
            mutation(left, 2, seed.getArgs()[1]);
            mutation(truthy, 0, flag);
            mutation(falseArm, 0, flag);
        }
        std::printf("%s Map host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    };
    checkBudgets(source, "conditional");
    checkBudgets(guarded, "guarded");
    checkBudgets(shortString, "short-circuit");
    checkNullableMapResults(context, shortString, prepared);
}

void checkSeededMapResults(mlir::MLIRContext & context, const std::string & shared) {
    auto source =
        replaced(shared, "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@put$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%entryKey: !ctjs.value)");
    source = replaced(source, "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n", "");
    source = replaced(source, "    %putResult = ctjs.call %putter(%owned)",
                      "    %priorGetter = ctjs.get_property %owned[%key]\n"
                      "    %produced = ctjs.call %priorGetter(%owned)\n"
                      "    %putResult = ctjs.call %putter(%owned, %produced)");
    source = replaced(source,
                      "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %answer = ctjs.get_property %state[%key]",
                      R"MLIR(
    %seedKey = ctjs.constant #ctjs.number<0>
    %payload = ctjs.constant #ctjs.number<4607182418800017408>
    %setKey = ctjs.constant #ctjs.string<"set">
    %mapSetter = ctjs.get_property %state[%setKey]
    %seeded = ctjs.call %mapSetter(%state, %seedKey, %payload)
    %getKey = ctjs.constant #ctjs.string<"get">
    %mapGetter = ctjs.get_property %state[%getKey]
    %probeKey = ctjs.constant #ctjs.number<0>
    %answer = ctjs.call %mapGetter(%state, %probeKey)
)MLIR");
    const auto prepare = [](std::string text) {
        for (const char * name : {"get$2", "put$3"}) {
            text = replaced(text, "captures %cell", "captures %state");
            text = replaced(text,
                            std::string("@") + name +
                                "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value",
                            std::string("@") + name +
                                "(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                                "%state: !ctjs.value");
            text = replaced(text,
                            "attributes {upvalue_count = 1 : i32} {\n"
                            "    %state = ctjs.load_upvalue %callee[0]\n",
                            "attributes {upvalue_count = 0 : i32} {\n");
        }
        text = replaced(text, "%produced = ctjs.call %priorGetter(%owned)",
                        "%priorEnvironment = ctjs.load_upvalue %priorGetter[0]\n"
                        "    %produced = ctjs.call_direct @get$2(%owned, %u, %priorGetter, "
                        "%priorEnvironment)");
        text = replaced(text, "%putResult = ctjs.call %putter(%owned, %produced)",
                        "%putEnvironment = ctjs.load_upvalue %putter[0]\n"
                        "    %putResult = ctjs.call_direct @put$3(%owned, %u, %putter, "
                        "%putEnvironment, %produced)");
        return replaced(text, "%answer = ctjs.call %getter(%owned)",
                        "%getEnvironment = ctjs.load_upvalue %getter[0]\n"
                        "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                        "%getEnvironment)");
    };
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto empty = [](mlir::ModuleOp module, const HostContractAnalysis & query) {
        bool withheld = query.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            withheld &= !query.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                withheld &= !query.property(read);
            }
        });
        return withheld;
    };
    const auto singleKeySource = source;
    for (const bool prepared : {false, true}) {
        checkConditionalMapResults(context, prepared ? prepare(source) : source, prepared);
    }
    for (const bool multiple : {false, true}) {
        source = singleKeySource;
        if (multiple) {
            source = replaced(source, "    %getKey = ctjs.constant",
                              "    %otherKey = ctjs.constant #ctjs.number<4611686018427387904>\n"
                              "    %otherSeed = ctjs.call %mapSetter(%state, %otherKey, %payload)\n"
                              "    %getKey = ctjs.constant");
        }
        for (const bool prepared : {false, true}) {
            auto module = mlir::parseSourceString<mlir::ModuleOp>(
                prepared ? prepare(source) : source, &context);
            check(static_cast<bool>(module), "source/prepared seeded Map result fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            HostContractAnalysis query(*module, contract);
            check(query.proved(),
                  "live per-key set facts independently type the same-key get result");
            if (!query.proved()) {
                std::fprintf(stderr, "seeded host: %s\n", query.reason().str().c_str());
                continue;
            }
            const auto calls = query.callables();
            check(calls.size() == 3 && calls[1].arguments.size() == 1,
                  "seeded producer, consuming setter and final getter remain distinct live calls");
            if (calls.size() != 3 || calls[1].arguments.size() != 1) { continue; }
            auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$2");
            check(calls[0].function == getter && calls[1].function == setter &&
                      calls[2].function == getter &&
                      calls[0].call->isBeforeInBlock(calls[1].call) &&
                      calls[1].call->isBeforeInBlock(calls[2].call) &&
                      calls[1].arguments.front().actual == calls[0].call->getResult(0) &&
                      calls[1].arguments.front().parameter ==
                          setter.getBody().front().getArgument(prepared ? 4 : 3) &&
                      calls[1].arguments.front().alternatives.tag() ==
                          mlir::TypeID::get<ctjs::NumberAttr>(),
                  "the consuming formal keeps the producer SSA result and prepared capture offset");
            check(hostContractFingerprint(*module) == contract.moduleSha256,
                  "presence and result proofs leave source calls and operands unchanged");
            const unsigned completion = query.steps();
            check(completion < 15000, "seeded presence proof stays within its fixture work limit");
            if (completion < 15000) {
                for (unsigned budget = 0; budget < completion; ++budget) {
                    HostContractAnalysis limited(*module, contract, budget);
                    check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                              empty(*module, limited),
                          "every incomplete presence budget withholds the entire callable family");
                }
                HostContractAnalysis exact(*module, contract, completion);
                check(exact.proved() && exact.steps() == completion &&
                          exact.callables().size() == 3,
                      "the exact presence completion budget reproduces every live result edge");
            }
            ctjs::CallOp seed, lookup;
            getter.walk([&](ctjs::CallOp call) {
                if (!seed) { seed = call; }
                lookup = call;
            });
            check(seed && lookup && seed != lookup, "the live body retains its seed and lookup");
            if (!seed || !lookup || seed == lookup) { continue; }
            mlir::Builder builder(&context);
            lookup->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            lookup->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            contract = requested(*module);
            check(HostContractAnalysis(*module, contract).proved(),
                  "forged presence reports do not replace the live seed/get proof");
            const auto originalKey = lookup.getArgs().front();
            lookup->setOperand(2, seed.getArgs().back());
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      empty(*module, stale),
                  "changing the queried key invalidates the earlier presence fingerprint");
            HostContractAnalysis mismatch(*module, requested(*module));
            check(!mismatch.proved() && empty(*module, mismatch),
                  "a fresh fingerprint and forged presence cannot prove a different get key");
            lookup->setOperand(2, originalKey);

            const auto originalPayload = seed.getArgs().back();
            for (mlir::Attribute payload :
                 {mlir::Attribute(ctjs::BooleanAttr::get(&context, true)),
                  mlir::Attribute(ctjs::StringAttr::get(&context, "owned"))}) {
                mlir::OpBuilder at(seed);
                auto changedPayload = ctjs::ConstantOp::create(at, seed.getLoc(), payload);
                seed->setOperand(3, changedPayload.getResult());
                HostContractAnalysis old(*module, contract);
                check(!old.proved() && old.reason().contains("fingerprint") && empty(*module, old),
                      "a payload mutation cannot reuse the previous result fingerprint");
                HostContractAnalysis changed(*module, requested(*module));
                check(changed.proved() && changed.callables().size() == 3 &&
                          changed.callables()[1].arguments.size() == 1 &&
                          changed.callables()[1].arguments.front().actual ==
                              calls[0].call->getResult(0) &&
                          changed.callables()[1].arguments.front().alternatives.tag() ==
                              payload.getTypeID(),
                      "boolean/string payloads independently retype the consumer without a carrier "
                      "promise");
                seed->setOperand(3, originalPayload);
                changedPayload.erase();
            }
            seed->setOperand(3, getter.getBody().front().getArgument(0));
            HostContractAnalysis unknown(*module, requested(*module));
            check(!unknown.proved() && empty(*module, unknown),
                  "an unproved payload cannot inherit the last write's earlier primitive tag");
            seed->setOperand(3, originalPayload);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the live key and payload restores the independent presence proof");
            std::printf("%s Map host %s proof and all %u incomplete budgets checked\n",
                        multiple ? "per-key" : "seeded", prepared ? "prepared" : "source",
                        completion);

            const auto variant = [&](const std::string & program, bool expected,
                                     const char * message,
                                     mlir::TypeID expectedTag =
                                         mlir::TypeID::get<ctjs::NumberAttr>(),
                                     bool checkJoins = false) {
                auto changed = mlir::parseSourceString<mlir::ModuleOp>(
                    prepared ? prepare(program) : program, &context);
                check(static_cast<bool>(changed), "seeded Map presence variant parses");
                if (!changed) { return; }
                HostContractAnalysis result(*changed, requested(*changed));
                check(result.proved() == expected && !result.exhausted() &&
                          (expected || empty(*changed, result)),
                      message);
                if (expected && result.proved()) {
                    check(
                        result.callables().size() == 3 &&
                            result.callables()[1].arguments.size() == 1 &&
                            result.callables()[1].arguments.front().alternatives.tag() ==
                                expectedTag,
                        "the retained entry supplies its current payload tag, not an older write");
                }
                if (!checkJoins || !result.proved()) { return; }
                const auto joinedContract = requested(*changed);
                const unsigned joinedCompletion = result.steps();
                check(joinedCompletion < 15000,
                      "joined payload proof stays within its fixture work limit");
                if (joinedCompletion < 15000) {
                    for (unsigned budget = 0; budget < joinedCompletion; ++budget) {
                        HostContractAnalysis limited(*changed, joinedContract, budget);
                        check(!limited.proved() && limited.exhausted() &&
                                  limited.steps() <= budget && empty(*changed, limited),
                              "every incomplete join budget withholds the entire callable family");
                    }
                    HostContractAnalysis exact(*changed, joinedContract, joinedCompletion);
                    check(exact.proved() && exact.steps() == joinedCompletion &&
                              exact.callables().size() == 3,
                          "the exact join completion budget reproduces every live result edge");
                }
                auto joinedGetter = changed->lookupSymbol<ctjs::FuncOp>("get$2");
                llvm::SmallVector<ctjs::CallOp> operations;
                joinedGetter.walk([&](ctjs::CallOp call) { operations.push_back(call); });
                check(operations.size() == 3, "live join fixture retains both sets and its get");
                if (operations.size() != 3) { return; }
                auto first = operations[0], possible = operations[1], get = operations[2];
                const auto writtenKey = possible.getArgs()[0];
                const auto writtenPayload = possible.getArgs()[1];
                const auto queriedKey = get.getArgs()[0];
                const auto assertTag = [&](mlir::TypeID tag, const char * reason) {
                    HostContractAnalysis fresh(*changed, requested(*changed));
                    check(fresh.proved() && fresh.callables().size() == 3 &&
                              fresh.callables()[1].arguments.size() == 1 &&
                              fresh.callables()[1].arguments.front().alternatives.tag() == tag,
                          reason);
                };
                for (mlir::Attribute payload :
                     {mlir::Attribute(ctjs::BooleanAttr::get(&context, true)),
                      mlir::Attribute(ctjs::StringAttr::get(&context, "joined"))}) {
                    mlir::OpBuilder at(possible);
                    auto replacement = ctjs::ConstantOp::create(at, possible.getLoc(), payload);
                    possible->setOperand(3, replacement.getResult());
                    HostContractAnalysis staleJoin(*changed, joinedContract);
                    check(!staleJoin.proved() && staleJoin.reason().contains("fingerprint") &&
                              empty(*changed, staleJoin),
                          "a changed possible payload invalidates the original join fingerprint");
                    get->setAttr("ctnative.map_present", at.getBoolAttr(true));
                    get->setAttr("ctnative.host_proved", at.getBoolAttr(true));
                    HostContractAnalysis mixed(*changed, requested(*changed));
                    check(!mixed.proved() && !mixed.exhausted() && empty(*changed, mixed),
                          "fresh forged presence cannot retain a tag across incompatible payloads");
                    possible->setOperand(2, first.getArgs()[0]);
                    assertTag(payload.getTypeID(),
                              "an exact same-key overwrite replaces rather than joins its tag");
                    possible->setOperand(2, writtenKey);
                    get->setOperand(2, writtenKey);
                    assertTag(payload.getTypeID(),
                              "the new write has its own definite payload despite an unknown join");
                    get->setOperand(2, queriedKey);
                    possible->setOperand(3, writtenPayload);
                    replacement.erase();
                    assertTag(mlir::TypeID::get<ctjs::NumberAttr>(),
                              "restoring the live payload restores the independent joined tag");
                }
                get->setOperand(2, writtenPayload);
                HostContractAnalysis absent(*changed, requested(*changed));
                check(!absent.proved() && empty(*changed, absent),
                      "a numeric dynamic write cannot prove a different literal is present");
                get->setOperand(2, queriedKey);
                assertTag(mlir::TypeID::get<ctjs::NumberAttr>(),
                          "restoring the queried key restores the joined result proof");
                std::printf("joined Map host %s proof and all %u incomplete budgets checked\n",
                            prepared ? "prepared" : "source", joinedCompletion);
            };
            constexpr llvm::StringLiteral seedLine =
                "    %seeded = ctjs.call %mapSetter(%state, %seedKey, %payload)";
            variant(replaced(source, "ctjs.call %mapGetter(%state, %probeKey)",
                             "ctjs.call %mapGetter(%state, %seedKey)"),
                    true,
                    "identical SSA keys establish the same local presence as equal constants");
            variant(replaced(source, seedLine,
                             seedLine.str() + "\n    %again = ctjs.call %mapSetter(%state, "
                                              "%seedKey, %payload)"),
                    true, "a same-key overwrite replaces the last local write fact");
            variant(replaced(source, seedLine, ""), false,
                    "a sibling's prior mutation cannot seed this method's initial presence");
            variant(replaced(source, seedLine,
                             seedLine.str() + "\n    %other = ctjs.call %mapSetter(%state, "
                                              "%payload, %payload)"),
                    true, "an independently distinct key preserves the earlier payload fact");
            variant(replaced(source, seedLine, seedLine.str() + R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %seedKey)
)MLIR"),
                    false, "a delete invalidates the local presence before the getter result");
            variant(replaced(source, seedLine, seedLine.str() + R"MLIR(
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %present = ctjs.call %hasMethod(%state, %seedKey)
)MLIR"),
                    true, "a read-only has preserves the independently seeded get result");
            const std::string deleteOther = seedLine.str() + R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %payload)
)MLIR";
            variant(replaced(source, seedLine, deleteOther), true,
                    "deleting a distinct literal preserves the earlier entry");
            // An initial size has no nonempty proof. Keep this genuinely
            // possibly aliasing even after nonempty snapshots are understood.
            const std::string dynamicMutation = R"MLIR(
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %dynamicKey = ctjs.get_property %state[%sizeKey]
)MLIR" + seedLine.str() + R"MLIR(
    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)
)MLIR";
            variant(replaced(source, seedLine, dynamicMutation), true,
                    "same-tag possible writes preserve presence and join their numeric payloads",
                    mlir::TypeID::get<ctjs::NumberAttr>(), !multiple);
            const auto incompatibleMutation = replaced(
                dynamicMutation,
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)",
                "    %incompatible = ctjs.constant #ctjs.boolean<true>\n"
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %incompatible)");
            variant(replaced(source, seedLine, incompatibleMutation), false,
                    "possible bool/number overwrites lose the tag without losing presence");
            variant(
                replaced(source, seedLine,
                         incompatibleMutation +
                             "    %later = ctjs.call %mapSetter(%state, %dynamicKey, %payload)\n"),
                false, "a later possible numeric write cannot restore an unknown earlier tag");
            const std::string reseed =
                "    %reseed = ctjs.call %mapSetter(%state, %seedKey, %payload)\n";
            variant(replaced(source, seedLine, incompatibleMutation + reseed), true,
                    "an exact-key reseed restores its tag after an incompatible possible write");
            const auto unknownMutation = replaced(
                dynamicMutation,
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)",
                "    %unknownGetKey = ctjs.constant #ctjs.string<\"get\">\n"
                "    %unknownGetter = ctjs.get_property %state[%unknownGetKey]\n"
                "    %unknownPayload = ctjs.call %unknownGetter(%state, %payload)\n"
                "    %maybeAlias = ctjs.call %mapSetter(%state, %dynamicKey, %unknownPayload)");
            variant(replaced(source, seedLine, unknownMutation), false,
                    "an unproved primitive payload loses a possibly overwritten entry's tag");
            variant(replaced(source, seedLine, unknownMutation + reseed), true,
                    "an exact-key reseed replaces an earlier unknown payload tag");
            variant(
                replaced(source, seedLine,
                         unknownMutation +
                             "    %later = ctjs.call %mapSetter(%state, %dynamicKey, %payload)\n"),
                false, "unknown payloads remain unknown across later possible numeric writes");
            variant(replaced(source, seedLine,
                             replaced(dynamicMutation,
                                      "%maybeAlias = ctjs.call %mapSetter(%state, "
                                      "%dynamicKey, %payload)",
                                      "%deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                                      "    %deleter = ctjs.get_property %state[%deleteKey]\n"
                                      "    %deleted = ctjs.call %deleter(%state, %dynamicKey)")),
                    false, "a possibly aliasing delete invalidates earlier contents");
            const std::string sizeRead = R"MLIR(
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %dynamicKey = ctjs.get_property %state[%sizeKey]
)MLIR";
            const std::string sizeDelete = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %dynamicKey)
)MLIR";
            const auto nonempty =
                replaced(source, seedLine, seedLine.str() + sizeRead + sizeDelete);
            auto twoEntries =
                replaced(nonempty, seedLine,
                         seedLine.str() + "\n    %secondSeed = ctjs.call %mapSetter(%state, "
                                          "%payload, %payload)");
            twoEntries = replaced(twoEntries, "%probeKey = ctjs.constant #ctjs.number<0>",
                                  "%probeKey = ctjs.constant #ctjs.number<4607182418800017408>");
            variant(twoEntries, true,
                    "two independently distinct keys prove a size lower bound of two");
            variant(replaced(twoEntries,
                             "%secondSeed = ctjs.call %mapSetter(%state, %payload, %payload)",
                             "%secondSeed = ctjs.call %mapSetter(%state, %seedKey, %payload)"),
                    false, "a repeated runtime key cannot inflate the cardinality lower bound");
            auto savedTwo = replaced(twoEntries, sizeDelete, R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %removeZero = ctjs.call %deleter(%state, %seedKey)
    %deleted = ctjs.call %deleter(%state, %dynamicKey)
)MLIR");
            variant(savedTwo, true, "saved size bounds survive a later cardinality decrease");
            constexpr llvm::StringLiteral sizeLine =
                "    %dynamicKey = ctjs.get_property %state[%sizeKey]\n";
            auto decreased = replaced(savedTwo, sizeLine, "");
            decreased = replaced(decreased, "    %deleted = ctjs.call",
                                 sizeLine.str() + "    %deleted = ctjs.call");
            variant(decreased, false,
                    "a size read after deletion cannot inherit the earlier cardinality");
            variant(nonempty, true, "a nonempty size snapshot cannot delete the definite zero key");
            variant(replaced(source, seedLine, seedLine.str() + sizeRead + R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %emptied = ctjs.call %deleter(%state, %seedKey)
    %reseeded = ctjs.call %mapSetter(%state, %seedKey, %payload)
    %deleted = ctjs.call %deleter(%state, %dynamicKey)
)MLIR"),
                    true, "later mutations cannot change an already-read nonempty size number");
            auto positiveKey =
                replaced(nonempty, "%seedKey = ctjs.constant #ctjs.number<0>",
                         "%seedKey = ctjs.constant #ctjs.number<4607182418800017408>");
            positiveKey = replaced(positiveKey, "%probeKey = ctjs.constant #ctjs.number<0>",
                                   "%probeKey = ctjs.constant #ctjs.number<4607182418800017408>");
            variant(positiveKey, false,
                    "nonempty alone cannot distinguish a positive key from the size");
            const auto aliasingFacts = replaced(positiveKey, sizeDelete, R"MLIR(
    %possibleAlias = ctjs.call %mapSetter(%state, %dynamicKey, %payload)
    %currentSize = ctjs.get_property %state[%sizeKey]
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %deleted = ctjs.call %deleter(%state, %currentSize)
)MLIR");
            variant(aliasingFacts, false,
                    "different definite SSA keys may alias and cannot count as two entries");
            variant(replaced(twoEntries, "%seedKey = ctjs.constant #ctjs.number<0>",
                             "%seedKey = ctjs.constant #ctjs.boolean<false>"),
                    true, "independent primitive tags also prove pairwise key disjointness");
            for (const char * bits : {"9223372036854775808", "13830554455654793216",
                                      "4602678819172646912", "9221120237041090561"}) {
                auto outside =
                    replaced(nonempty, "%seedKey = ctjs.constant #ctjs.number<0>",
                             std::string("%seedKey = ctjs.constant #ctjs.number<") + bits + ">");
                outside =
                    replaced(outside, "%probeKey = ctjs.constant #ctjs.number<0>",
                             std::string("%probeKey = ctjs.constant #ctjs.number<") + bits + ">");
                variant(outside, true,
                        "negative zero, negative, subunit and NaN keys cannot equal nonempty size");
            }
            if (!multiple) {
                const auto cardinalityFixture = [&](unsigned count, unsigned probe) {
                    std::string seeds;
                    for (unsigned i = 1; i < count; ++i) {
                        seeds += "\n    %key" + std::to_string(i) +
                                 " = ctjs.constant #ctjs.number<" +
                                 std::to_string(llvm::APFloat(static_cast<double>(i))
                                                    .bitcastToAPInt()
                                                    .getZExtValue()) +
                                 ">\n    %seed" + std::to_string(i) +
                                 " = ctjs.call %mapSetter(%state, %key" + std::to_string(i) +
                                 ", %payload)\n";
                    }
                    return replaced(replaced(nonempty, seedLine, seedLine.str() + seeds),
                                    "%probeKey = ctjs.constant #ctjs.number<0>",
                                    "%probeKey = ctjs.constant #ctjs.number<" +
                                        std::to_string(llvm::APFloat(static_cast<double>(probe))
                                                           .bitcastToAPInt()
                                                           .getZExtValue()) +
                                        ">");
                };
                variant(cardinalityFixture(3, 2), true,
                        "three definite keys establish a stronger bound than nonempty");
                variant(cardinalityFixture(64, 63), true,
                        "the candidate cap includes all 64 independently distinct witnesses");
                variant(cardinalityFixture(65, 64), false,
                        "a capped subset never supplies the unexamined sixty-fifth witness");
                for (const auto & bits :
                     {std::pair{"0", "9223372036854775808"},
                      std::pair{"9221120237041090561", "9221120237041090562"}}) {
                    auto aliases = cardinalityFixture(3, 2);
                    aliases = replaced(aliases, "%seedKey = ctjs.constant #ctjs.number<0>",
                                       std::string("%seedKey = ctjs.constant #ctjs.number<") +
                                           bits.first + ">");
                    aliases = replaced(
                        aliases, "%key1 = ctjs.constant #ctjs.number<4607182418800017408>",
                        std::string("%key1 = ctjs.constant #ctjs.number<") + bits.second + ">");
                    variant(aliases, false,
                            "SameValueZero duplicates never inflate a size bound past two");
                }
                for (const auto & specimen : {nonempty, twoEntries}) {
                    auto checked = mlir::parseSourceString<mlir::ModuleOp>(
                        prepared ? prepare(specimen) : specimen, &context);
                    check(static_cast<bool>(checked), "nonempty size budget fixture parses");
                    if (!checked) { continue; }
                    const auto sizeContract = requested(*checked);
                    HostContractAnalysis complete(*checked, sizeContract);
                    const unsigned sizeCompletion = complete.steps();
                    check(complete.proved() && sizeCompletion < 15000,
                          "complete size proof stays within the fixture work limit");
                    if (!complete.proved() || sizeCompletion >= 15000) { continue; }
                    for (unsigned budget = 0; budget < sizeCompletion; ++budget) {
                        HostContractAnalysis limited(*checked, sizeContract, budget);
                        check(!limited.proved() && limited.exhausted() &&
                                  limited.steps() <= budget && empty(*checked, limited),
                              "every incomplete size budget withholds the entire callable family");
                    }
                    HostContractAnalysis exact(*checked, sizeContract, sizeCompletion);
                    check(exact.proved() && exact.steps() == sizeCompletion &&
                              exact.callables().size() == 3,
                          "the exact size completion budget reproduces every live result edge");
                    auto sizeGetter = checked->lookupSymbol<ctjs::FuncOp>("get$2");
                    llvm::SmallVector<ctjs::CallOp> operations;
                    ctjs::GetPropertyOp size;
                    sizeGetter.walk([&](ctjs::CallOp call) { operations.push_back(call); });
                    sizeGetter.walk([&](ctjs::GetPropertyOp read) {
                        auto key = read.getKey().getDefiningOp<ctjs::ConstantOp>();
                        auto name = key ? llvm::dyn_cast<ctjs::StringAttr>(key.getValue())
                                        : ctjs::StringAttr{};
                        if (name && name.getValue() == "size") { size = read; }
                    });
                    check(size && (operations.size() == 3 || operations.size() == 4),
                          "the nonempty proof keeps seed, size, delete and get operations");
                    if (!size || (operations.size() != 3 && operations.size() != 4)) { continue; }
                    auto sizeSeed = operations[0], erased = operations[operations.size() - 2];
                    auto * seedNext = sizeSeed->getNextNode();
                    sizeSeed->moveAfter(size);
                    HostContractAnalysis staleSize(*checked, sizeContract);
                    check(!staleSize.proved() && staleSize.reason().contains("fingerprint") &&
                              empty(*checked, staleSize),
                          "moving size before seed invalidates the original fingerprint");
                    size->setAttr("ctnative.nonempty_size", builder.getBoolAttr(true));
                    HostContractAnalysis beforeSeed(*checked, requested(*checked));
                    check(
                        !beforeSeed.proved() && empty(*checked, beforeSeed),
                        "a fresh fingerprint and forged size marker cannot prove initial contents");
                    sizeSeed->moveBefore(seedNext);
                    erased->setOperand(2, operations.back().getArgs()[0]);
                    HostContractAnalysis equalDelete(*checked, requested(*checked));
                    check(!equalDelete.proved() && empty(*checked, equalDelete),
                          "an equal-key delete cannot inherit disjointness from the previous "
                          "operand");
                    erased->setOperand(2, size.getResult());
                    check(HostContractAnalysis(*checked, requested(*checked)).proved(),
                          "restoring the live size order and delete key restores its independent "
                          "proof");
                    std::printf(
                        "%zu-key Map size host %s proof and all %u incomplete budgets checked\n",
                        operations.size() - 2, prepared ? "prepared" : "source", sizeCompletion);
                }
            }
            auto stringSeed = replaced(source, "%seedKey = ctjs.constant #ctjs.number<0>",
                                       "%seedKey = ctjs.constant #ctjs.string<\"seed\">");
            stringSeed = replaced(stringSeed, "%probeKey = ctjs.constant #ctjs.number<0>",
                                  "%probeKey = ctjs.constant #ctjs.string<\"seed\">");
            variant(replaced(stringSeed, seedLine, dynamicMutation), true,
                    "independent number/string tags prove a runtime mutation key is disjoint");
            // Both zero encodings and every NaN payload denote the same Map key.
            // A different payload tag after an aliasing write must replace the old
            // fact; an aliasing delete must remove it, even with unequal attributes.
            for (const auto & [seedBits, aliasBits] :
                 {std::pair{"0", "9223372036854775808"},
                  std::pair{"9221120237041090561", "9221120237041090562"}}) {
                auto equalKey = replaced(source, "%seedKey = ctjs.constant #ctjs.number<0>",
                                         std::string("%seedKey = ctjs.constant #ctjs.number<") +
                                             seedBits + ">");
                equalKey = replaced(equalKey, "%probeKey = ctjs.constant #ctjs.number<0>",
                                    std::string("%probeKey = ctjs.constant #ctjs.number<") +
                                        aliasBits + ">");
                variant(equalKey, true, "SameValueZero proves equal signed-zero and NaN keys");
                auto overwriteAlias = replaced(
                    equalKey, "    %answer = ctjs.call %mapGetter",
                    "    %booleanPayload = ctjs.constant #ctjs.boolean<true>\n"
                    "    %overwritten = ctjs.call %mapSetter(%state, %probeKey, %booleanPayload)\n"
                    "    %answer = ctjs.call %mapGetter");
                variant(overwriteAlias, true,
                        "equal zero/NaN encodings replace rather than preserve the previous tag",
                        mlir::TypeID::get<ctjs::BooleanAttr>());
                auto eraseAlias =
                    replaced(equalKey, "    %answer = ctjs.call %mapGetter",
                             "    %deleteKey = ctjs.constant #ctjs.string<\"delete\">\n"
                             "    %deleter = ctjs.get_property %state[%deleteKey]\n"
                             "    %deleted = ctjs.call %deleter(%state, %probeKey)\n"
                             "    %answer = ctjs.call %mapGetter");
                variant(eraseAlias, false,
                        "unequal zero or NaN bits never establish disjointness for a delete");
            }
        }
    }
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    checkSeededMapResults(context, sharedMapWithPutCall(sharedMapSource(capturedGetterSource())));
    if (failures == 0) { std::puts("host contract seeded Map result proofs passed"); }
    return failures == 0 ? 0 : 1;
}
