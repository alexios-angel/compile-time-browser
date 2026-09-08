// Owned global method tables: the one-method owner census over the small
// fixture, its mutation refusals, and the captured-Map family (source,
// indirect, imported and prepared forms).
//
// SPLIT 2026-09-08: checkSharedMap moved to OwnedGlobalSharedMap.cpp (its own
// executable) and the fixtures, `check`, `contractFor`, `empty` and `replaced`
// to OwnedGlobalMethodsFixtures.h. Everything else is verbatim, in order.

#include "OwnedGlobalMethodsFixtures.h"

using namespace ctcompile::test::owned_global_methods;

namespace {

bool complete(const OwnedGlobalRoots & query, unsigned calls = 1) {
    if (!query.proved() || query.exhausted() || query.roots().size() != 1) { return false; }
    const auto & root = query.roots().front();
    if (!root.methodTable || root.binding != "host" || root.property != "slot" ||
        root.loads.size() != 1 || root.reads.size() != 1 || query.lookup(root.owner) != &root ||
        query.lookup(root.initialization) != &root ||
        query.lookup(root.fieldInitialization) != &root ||
        query.lookup(root.loads.front()) != &root || query.lookup(root.reads.front()) != &root) {
        return false;
    }
    const auto & table = *root.methodTable;
    auto factoryCall = table.factoryCall;
    auto field = root.fieldInitialization;
    return table.methods.size() == 1 && table.calls.size() == calls &&
           table.calls.front().function == table.methods.front().function &&
           table.calls.front().closure == table.methods.front().closure &&
           table.calls.front().write == table.methods.front().initialization &&
           factoryCall->getResult(0) == field.getValue() && !query.lookup(table.table) &&
           !query.lookup(table.calls.front().call);
}

void checkCapturedMap(mlir::MLIRContext & context) {
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto proved = [](const OwnedGlobalRoots & query, bool prepared) {
        if (!query.proved() || query.roots().size() != 1) { return false; }
        const auto & root = query.roots().front();
        if (!root.methodTable || root.loads.size() != 2 || root.reads.size() != 1) { return false; }
        const auto & table = *root.methodTable;
        if (!table.wrapper || !table.wrapperCall || !table.capturedMap || table.calls.size() != 1 ||
            !table.calls.front().capturedMap) {
            return false;
        }
        const auto & capture = *table.capturedMap;
        return capture.intrinsic && capture.allocation && !capture.reads.empty() &&
               capture.allocation == table.calls.front().capturedMap->allocation &&
               capture.reads == table.calls.front().capturedMap->reads &&
               capture.calls == table.calls.front().capturedMap->calls &&
               (prepared ? (!capture.cell && !capture.initialization && capture.upvalues.empty() &&
                            capture.argument)
                         : (capture.cell && capture.initialization && !capture.upvalues.empty() &&
                            !capture.argument));
    };
    auto prepared = replaced(capturedFixture, "    %cell = ctjs.create_cell %u\n", "");
    prepared = replaced(prepared, "    ctjs.cell_set %cell, %state\n", "");
    prepared = replaced(prepared, "captures %cell", "captures %state");
    prepared = replaced(prepared, "    %state = ctjs.load_upvalue %callee[0]\n", "");
    prepared =
        replaced(prepared, "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                 "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%state: !ctjs.value)");
    prepared = replaced(prepared, "upvalue_count = 1 : i32", "upvalue_count = 0 : i32");
    prepared = replaced(prepared, "%answer = ctjs.call %getter(%owned)",
                        "%environment = ctjs.load_upvalue %getter[0]\n"
                        "    %answer = ctjs.call_direct @get$3(%owned, %u, %getter, %environment)");
    auto specialized =
        replaced(prepared, "    %factory = ctjs.create_closure %callee[2] this %u\n", "");
    specialized = replaced(specialized, "@publish$1(%u, %u, %wrapper, %factory)",
                           "@publish$1(%u, %u, %wrapper)");
    specialized =
        replaced(specialized,
                 "@publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                 "%factory: !ctjs.value)",
                 "@publish$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)");
    specialized = replaced(specialized, "    %host = ctjs.load_global \"host\"",
                           "    %factory = ctjs.create_closure %callee[2] this %u\n"
                           "    %host = ctjs.load_global \"host\"");
    const auto indirect = replaced(capturedFixture, "ctjs.call_direct @make$2(%u, %u, %factory)",
                                   "ctjs.call %factory(%u)");
    constexpr llvm::StringLiteral set = R"MLIR(
    %entryKey = ctjs.constant #ctjs.string<"x">
    %value = ctjs.constant #ctjs.number<4607182418800017408>
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %written = ctjs.call %setter(%state, %entryKey, %value)
)MLIR";
    const auto withSet = [&](llvm::StringRef source) {
        return replaced(source.str(), "    %key = ctjs.constant #ctjs.string<\"size\">",
                        set.str() + "    %key = ctjs.constant #ctjs.string<\"size\">");
    };
    const auto mutated = withSet(indirect);
    const auto liftedMutation = withSet(prepared);
    auto importedMutation = replaced(importedCapturedFixture,
                                     "    %2 = ctjs.constant #ctjs.string<\"size\">\n"
                                     "    %3 = ctjs.get_property %1[%2]",
                                     "    %2 = ctjs.constant #ctjs.string<\"set\">\n"
                                     "    %3 = ctjs.get_property %1[%2]\n"
                                     "    %4 = ctjs.constant #ctjs.string<\"x\">\n"
                                     "    %5 = ctjs.constant #ctjs.number<4607182418800017408>\n"
                                     "    %6 = ctjs.call %3(%1, %4, %5)\n"
                                     "    %7 = ctjs.load_upvalue %arg2[0]\n"
                                     "    %8 = ctjs.constant #ctjs.string<\"size\">\n"
                                     "    %9 = ctjs.get_property %7[%8]");
    importedMutation = replaced(importedMutation, "ctjs.return %3", "ctjs.return %9");
    constexpr llvm::StringLiteral actions = R"MLIR(
    %getKey = ctjs.constant #ctjs.string<"get">
    %getMethod = ctjs.get_property %state[%getKey]
    %lookup = ctjs.call %getMethod(%state, %entryKey)
    %hasKey = ctjs.constant #ctjs.string<"has">
    %hasMethod = ctjs.get_property %state[%hasKey]
    %found = ctjs.call %hasMethod(%state, %entryKey)
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleteMethod = ctjs.get_property %state[%deleteKey]
    %removed = ctjs.call %deleteMethod(%state, %entryKey)
)MLIR";
    struct specimen {
        std::string source;
        bool lifted;
        unsigned reads;
        unsigned calls;
    };
    std::vector<specimen> specimens{{capturedFixture, false, 1, 0},
                                    {indirect, false, 1, 0},
                                    {importedCapturedFixture, false, 1, 0},
                                    {prepared, true, 1, 0},
                                    {specialized, true, 1, 0},
                                    {withSet(capturedFixture), false, 2, 1},
                                    {mutated, false, 2, 1},
                                    {importedMutation, false, 2, 1},
                                    {liftedMutation, true, 2, 1},
                                    {withSet(specialized), true, 2, 1}};
    for (const auto & [source, lifted] :
         {std::pair{mutated, false}, std::pair{liftedMutation, true}}) {
        specimens.push_back(
            {replaced(source, "    %key = ctjs.constant #ctjs.string<\"size\">",
                      actions.str() + "    %key = ctjs.constant #ctjs.string<\"size\">"),
             lifted, 5, 4});
        specimens.push_back(
            {replaced(source, "%written = ctjs.call %setter(%state, %entryKey, %value)",
                      "%sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                      "    %before = ctjs.get_property %state[%sizeKey]\n"
                      "    %written = ctjs.call %setter(%state, %before, %value)"),
             lifted, 3, 1});
        specimens.push_back({replaced(source, "%size = ctjs.get_property %state[%key]",
                                      "%size = ctjs.get_property %written[%key]"),
                             lifted, 2, 1});
    }
    for (const auto & [source, lifted, reads, calls] : specimens) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(static_cast<bool>(module), "captured Map source and prepared fixtures parse");
        if (!module) { continue; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots query(*module, contract);
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "raw factory resolution preserves all fingerprinted source operations");
        if (!host.proved()) {
            std::fprintf(stderr, "capture host: %s\n", host.reason().str().c_str());
        }
        if (!query.proved()) {
            std::fprintf(stderr, "capture owner: %s\n", query.reason().str().c_str());
        }
        check(proved(query, lifted),
              "live capture proof preserves wrapper publication and the sole Map identity");
        if (!query.proved()) { continue; }
        const auto & effects = *query.roots().front().methodTable->capturedMap;
        check(effects.reads.size() == reads && effects.calls.size() == calls,
              "captured source ownership records every live standard Map read and call");
        const unsigned completion = query.steps();
        check(completion > host.steps() && completion < 10000,
              "capture ownership charges bounded work after its host proof");
        if (completion >= 10000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      empty(*module, limited),
                  "every incomplete capture budget withholds all owning source edges");
        }
        check(proved(OwnedGlobalRoots(*module, contract, completion), lifted),
              "exact capture completion budget publishes its source graph");
        check(!OwnedGlobalRoots(*module, contractFor(*module)).proved(),
              "a Map capture requires the explicit standard intrinsic identity");
        auto method = module->lookupSymbol<ctjs::FuncOp>("get$3");
        auto returned = llvm::cast<ctjs::ReturnOp>(method.getBody().front().getTerminator());
        returned->setOperand(0, method.getBody().front().getArgument(0));
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "changed capture source cannot silently refresh its original fingerprint");
        OwnedGlobalRoots changed(*module, requested(*module));
        check(!changed.proved() && empty(*module, changed),
              "a fresh fingerprint cannot authorize a receiver-observing captured method");
        std::printf("captured Map %s proof (%u reads, %u calls) and all %u incomplete budgets "
                    "checked\n",
                    lifted ? "prepared" : "source", reads, calls, completion);
    }
    const auto refuse = [&](llvm::StringRef source, llvm::StringRef from, llvm::StringRef to,
                            const char * message) {
        auto module =
            mlir::parseSourceString<mlir::ModuleOp>(replaced(source.str(), from, to), &context);
        check(static_cast<bool>(module), "captured Map refusal variant parses");
        if (!module) { return; }
        OwnedGlobalRoots result(*module, requested(*module));
        check(!result.proved() && !result.exhausted() && empty(*module, result), message);
    };
    refuse(capturedFixture, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u",
           "multiple writes revoke immutable captured environment ownership");
    refuse(capturedFixture, "ctjs.cell_set %cell, %state", "ctjs.cell_set %cell, %cell",
           "a cyclic capture cannot inherit the Map allocation identity");
    refuse(capturedFixture, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.store_global \"escape\", %state",
           "separate Map publication is outside the captured owner graph");
    refuse(capturedFixture, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.store_global \"escape\", %cell",
           "a published capture cell cannot inherit a private environment proof");
    refuse(capturedFixture, "%state = ctjs.construct %constructor(%constructor)",
           "%state = ctjs.construct %constructor(%constructor, %u)",
           "Map iterables retain their iteration and exception boundary");
    refuse(capturedFixture, "%constructor = ctjs.load_global \"Map\"",
           "ctjs.store_global \"Map\", %u\n    %constructor = ctjs.load_global \"Map\"",
           "source replacement revokes the standard Map identity");
    refuse(capturedFixture, "ctjs.return %size", "ctjs.throw %size",
           "throwing captured methods need an explicit exceptional boundary");
    refuse(capturedFixture, "ctjs.return %size",
           "%again = ctjs.call %callee(%this)\n    ctjs.return %size",
           "method reentry cannot be treated as an inert size getter");
    refuse(capturedFixture, "#ctjs.string<\"size\">", "#ctjs.string<\"get\">",
           "other Map methods do not inherit the checked size read");
    refuse(indirect, "ctjs.call %factory(%u)", "ctjs.call %factory(%u, %u)",
           "an indirect factory argument window is outside the captured Map proof");
    refuse(indirect, "ctjs.call %factory(%u)", "ctjs.call %factory(%host)",
           "an indirect factory retains its source receiver convention");
    refuse(indirect, "ctjs.call %factory(%u)",
           "ctjs.call %factory(%u)\n    %again = ctjs.call %factory(%u)",
           "multiple indirect factory invocations cannot merge Map identities");
    refuse(indirect, "ctjs.cell_set %cell, %state",
           "ctjs.cell_set %cell, %state\n    ctjs.cell_set %cell, %u",
           "indirect callback resolution cannot authorize mutable capture state");
    refuse(indirect, "%factory = ctjs.create_closure %callee[2] this %u",
           "%factory = ctjs.create_closure %u[2] this %u",
           "indirect callbacks retain their supplied source program identity");
    refuse(capturedFixture, "%table = ctjs.call_direct @make$2(%u, %u, %factory)",
           "%table = ctjs.call_direct @make$2(%u, %u, %factory)\n"
           "    %another = ctjs.call_direct @make$2(%u, %u, %factory)",
           "repeated factory invocations cannot merge fresh Map identities");
    refuse(capturedFixture, "%invoked = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)",
           "%invoked = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)\n"
           "    %again = ctjs.call_direct @publish$1(%u, %u, %wrapper, %factory)",
           "repeated wrapper invocations cannot merge descendant Map identities");
    refuse(capturedFixture, "%table = ctjs.call_direct @make$2(%u, %u, %factory)",
           "%table = ctjs.call_direct @make$2(%u, %u, %factory)\n"
           "    ctjs.store_global \"escapedFactory\", %factory",
           "transported factory parameters cannot acquire an exported alias");
    refuse(capturedFixture, "%factory = ctjs.create_closure %callee[2] this %u",
           "%factory = ctjs.create_closure %u[2] this %u",
           "transported factories retain their creating activation's source identity");
    refuse(capturedFixture, "ctjs.set_property %host[%slot], %table",
           "ctjs.set_property %host[%slot], %table\n    ctjs.store_global \"table\", %table",
           "additional table publication remains outside the fixed owning field");
    refuse(prepared, "ctjs.load_upvalue %getter[0]", "ctjs.load_upvalue %getter[1]",
           "prepared capture operands need the actual stored environment slot");
    refuse(prepared, "@get$3(%owned, %u, %getter, %environment)", "@get$3(%owned, %u, %getter, %u)",
           "a forged prepared capture argument cannot replace the owning Map");
    refuse(prepared, "captures %state", "captures %u",
           "native environment markers cannot supply a missing Map producer");
    for (const auto & source : {mutated, liftedMutation}) {
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey)",
               "source and prepared Map effects require the exact builtin arity");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%this, %entryKey, %value)",
               "source and prepared Map calls retain the exact method receiver");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey, %state)",
               "Map cycles cannot inherit the primitive captured contents proof");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %state, %value)",
               "a Map key cannot retain an owning alias through primitive contents");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey, %setter)",
               "a detached method cannot enter the primitive captured contents");
        refuse(source, "ctjs.return %size", "ctjs.return %written",
               "a Map-valued return needs another owning publication proof");
        refuse(source, "ctjs.return %size", "ctjs.return %setter",
               "detached builtin method values remain outside primitive returns");
        refuse(source, "ctjs.return %size",
               "ctjs.store_global \"escapedMap\", %written\n    ctjs.return %size",
               "a fluent Map alias cannot acquire another global owner");
        refuse(source, "ctjs.return %size",
               "%opaque = ctjs.call %callee(%this)\n    ctjs.return %size",
               "proved Map mutation cannot authorize later opaque or reentrant effects");
        refuse(source, "ctjs.call %setter(%state, %entryKey, %value)",
               "ctjs.call %setter(%state, %entryKey) {ctnative.map_action = \"set\", "
               "ctnative.host_proved = true}",
               "forged native effect markers cannot repair an incomplete source call");
        refuse(source, "#ctjs.string<\"set\">", "#ctjs.string<\"clear\">",
               "other standard methods require their own captured effect boundary");
        refuse(source, "ctjs.create_closure %callee[3]", "ctjs.create_closure %u[3]",
               "effectful source and prepared methods retain their source program identity");
        refuse(source, "ctjs.create_closure %callee[2]", "ctjs.create_closure %u[2]",
               "effectful wrapper factories retain their source program identity");
        refuse(source, "ctjs.return %size",
               "ctjs.set_property %state[%setKey], %setter\n    ctjs.return %size",
               "a captured Map method replacement invalidates later builtin effects");
    }
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    checkCapturedMap(context);
    // checkSharedMap(context) is OwnedGlobalSharedMap.cpp now.
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "owned global method fixture parses");
    if (!module) { return 1; }
    const auto contract = contractFor(*module);
    HostContractAnalysis host(*module, contract);
    check(host.proved(), "complete host analysis proves the current getter");
    OwnedGlobalRoots query(*module, contract);
    check(complete(query), "root storage and current callable share the source table family");
    if (!query.proved()) { std::fprintf(stderr, "%s\n", query.reason().str().c_str()); }
    if (query.roots().empty()) { return 1; }
    for (const auto & [call, count] :
         {std::pair{"%answer = ctjs.call_direct @get$2(%owned, %u, %getter)", 1u},
          std::pair{"%answer = ctjs.call %getter(%owned)\n"
                    "    %again = ctjs.call %getter(%owned)\n"
                    "    ctjs.store_global \"trace\", %again",
                    2u}}) {
        std::string source = fixture;
        constexpr llvm::StringLiteral original = "%answer = ctjs.call %getter(%owned)";
        source.replace(source.find(original.str()), original.size(), call);
        auto variant = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
        check(variant && complete(OwnedGlobalRoots(*variant, contractFor(*variant)), count),
              "resolved and repeated getter calls retain every checked table edge");
    }
    const unsigned completion = query.steps();
    check(completion > host.steps() && completion <= 10000,
          "owner census charges additional bounded work after the complete host proof");
    if (completion > 10000) { return 1; }
    for (unsigned budget = 0; budget < completion; ++budget) {
        OwnedGlobalRoots limited(*module, contract, budget);
        check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                  empty(*module, limited),
              "every incomplete budget withholds the entire owning table plan");
    }
    check(complete(OwnedGlobalRoots(*module, contract, completion)),
          "exact completion budget publishes the complete owner and callable graph");
    auto root = query.roots().front();
    auto table = *root.methodTable;
    mlir::Builder attributes(&context);
    (*module)->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
    (*module)->setAttr("ctnative.host_callable", attributes.getStringAttr("forged"));
    check(complete(OwnedGlobalRoots(*module, contract)),
          "forged diagnostic reports do not replace the live owner census");
    const auto refused = [&](const char * message) {
        OwnedGlobalRoots stale(*module, contract);
        check(!stale.proved() && stale.reason().contains("fingerprint") && empty(*module, stale),
              "semantic mutation cannot silently rebind the supplied source manifest");
        OwnedGlobalRoots fresh(*module, contractFor(*module));
        check(!fresh.proved() && !fresh.exhausted() && empty(*module, fresh), message);
    };
    const auto restored = [&] {
        check(complete(OwnedGlobalRoots(*module, contract)),
              "restoring source restores its independently rebuilt proof");
    };
    mlir::OpBuilder builder(&context);
    builder.setInsertionPointAfter(root.fieldInitialization);
    auto * rewrite = builder.clone(*root.fieldInitialization.getOperation());
    refused("root field replacement revokes its fixed owning table");
    rewrite->erase();
    restored();

    builder.setInsertionPointAfter(root.initialization);
    auto alias =
        ctjs::StoreGlobalOp::create(builder, root.owner.getLoc(), "alias", root.owner.getResult());
    refused("a second global root alias requires a separate owner contract");
    alias.erase();
    restored();

    builder.setInsertionPointAfter(root.fieldInitialization);
    auto detached = ctjs::StoreGlobalOp::create(builder, root.owner.getLoc(), "detached",
                                                table.factoryCall->getResult(0));
    refused("detached table publication is not a closed fixed-field export");
    detached.erase();
    restored();

    auto getterRead = table.calls.front().read;
    builder.setInsertionPointAfter(getterRead);
    auto detachedGetter = ctjs::StoreGlobalOp::create(builder, root.owner.getLoc(),
                                                      "detachedGetter", getterRead.getResult());
    refused("publishing the loaded callable invalidates the complete owning table proof");
    detachedGetter.erase();
    restored();

    builder.setInsertionPointAfter(table.factoryCall);
    auto * secondCall = builder.clone(*table.factoryCall);
    refused("two factory invocations cannot share a source allocation identity");
    secondCall->erase();
    restored();

    builder.setInsertionPointAfter(table.table);
    auto extra = ctjs::CreateObjectOp::create(builder, root.owner.getLoc());
    refused("equal-shaped fresh allocations are not the same owning table");
    extra.erase();
    restored();

    builder.setInsertionPointAfter(table.methods.front().initialization);
    auto * methodWrite = builder.clone(*table.methods.front().initialization.getOperation());
    refused("method replacement is outside the fixed callable storage tier");
    methodWrite->erase();
    restored();

    const auto oldValue = root.fieldInitialization.getValue();
    root.fieldInitialization->setOperand(2, root.owner.getResult());
    refused("a cyclic publication is not an owning callable field");
    root.fieldInitialization->setOperand(2, oldValue);
    restored();

    root.fieldInitialization->moveAfter(root.reads.front());
    refused("reads before owning table publication have no definite field");
    root.fieldInitialization->moveBefore(root.loads.front());
    restored();

    auto * methodTerminator = table.methods.front().function.getBody().front().getTerminator();
    const auto answer = methodTerminator->getOperand(0);
    methodTerminator->setOperand(0,
                                 table.methods.front().function.getBody().front().getArgument(0));
    refused("a receiver-observing getter needs an additional invocation proof");
    methodTerminator->setOperand(0, answer);
    restored();

    builder.setInsertionPointAfter(table.methods.front().initialization);
    auto extraKey = ctjs::ConstantOp::create(builder, root.owner.getLoc(),
                                             ctjs::StringAttr::get(&context, "extra"));
    ctjs::ConstantOp factoryUndefined;
    table.factory.walk([&](ctjs::ConstantOp constant) {
        if (llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) { factoryUndefined = constant; }
    });
    auto extraField =
        ctjs::SetPropertyOp::create(builder, root.owner.getLoc(), table.table.getResult(),
                                    extraKey.getResult(), factoryUndefined.getResult());
    refused("an extended table schema is outside the one-method owner tier");
    extraField.erase();
    extraKey.erase();
    restored();

    auto changedContract = contract;
    changedContract.observations.push_back("host");
    OwnedGlobalRoots observed(*module, changedContract);
    check(!observed.proved() && empty(*module, observed),
          "driver observations cannot expose the owning root as a scalar");
    if (failures == 0) {
        std::printf("owned global method proof and all %u incomplete budgets passed\n", completion);
    }
    return failures == 0 ? 0 : 1;
}
