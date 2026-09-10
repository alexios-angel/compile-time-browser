#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

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
                      calls[1].arguments.front().alternatives ==
                          ctcompile::ctnative::PrimitiveAlternatives::forTag(tag) &&
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
            true, "exact deletion gives the stale guard an Undefined read and Number fallback");
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

} // namespace ctcompile::test::host_contract_seeded_maps
