// Host contract live proof queries over the shared two-method Map: seeded and
// per-key set facts typing a later get, possible-alias joins, SameValueZero
// keys, and every incomplete presence/join budget.
//
// SPLIT 2026-09-08: this is checkSeededMapResults, verbatim, out of a
// 1,068-line test/HostContract.cpp; it takes the same `shared` fixture that
// checkCapturedCallables used to build inline, now from HostContractFixtures.h.

#include "HostContractFixtures.h"
#include "llvm/ADT/APFloat.h"

using namespace ctcompile::test::host_contract;

namespace {

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
                      calls[1].arguments.front().primitiveTag ==
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
                          changed.callables()[1].arguments.front().primitiveTag ==
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
                            result.callables()[1].arguments.front().primitiveTag == expectedTag,
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
                              fresh.callables()[1].arguments.front().primitiveTag == tag,
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
    checkSeededMapResults(context, sharedMapWithPutCall(sharedMapSource(capturedGetterSource())));
    if (failures == 0) { std::puts("host contract seeded Map result proofs passed"); }
    return failures == 0 ? 0 : 1;
}
