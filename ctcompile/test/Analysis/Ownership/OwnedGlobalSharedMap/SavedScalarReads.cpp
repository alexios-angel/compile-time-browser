#include "Tests.h"

namespace ctcompile::test::owned_global_shared_map {

void checkSavedScalarReads(mlir::MLIRContext & context, const std::string & source, bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    using Dependencies = std::vector<std::vector<unsigned>>;
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        module.walk([&](ctjs::StoreGlobalOp store) {
            const auto name = store.getName().str();
            if (name != "host" && name != "trace" &&
                !llvm::is_contained(contract.observations, name)) {
                contract.observations.push_back(name);
            }
        });
        return contract;
    };
    const auto evidence = [&](mlir::ModuleOp module, const HostContractAnalysis & host,
                              const OwnedGlobalRoots & owner, const Dependencies & expected,
                              mlir::TypeID tag = mlir::TypeID::get<ctjs::NumberAttr>()) {
        check(host.scalarReads().size() == expected.size() &&
                  owner.scalarReads().size() == expected.size(),
              "host and owner expose every independently proved scalar load exactly once");
        check(owner.roots().size() == 1 && owner.roots().front().methodTable &&
                  owner.roots().front().methodTable->calls.size() == 4,
              "scalar evidence requires the complete original four-call published family");
        if (owner.roots().size() != 1 || !owner.roots().front().methodTable ||
            owner.roots().front().methodTable->calls.size() != 4) {
            return;
        }
        const auto & calls = owner.roots().front().methodTable->calls;
        const auto alternatives = Alternatives::forTag(tag);
        unsigned position = 0;
        auto entry = module.lookupSymbol<ctjs::FuncOp>("script$0");
        module.walk([&](ctjs::LoadGlobalOp load) {
            const auto * scalar = owner.scalarRead(load);
            const auto * hostScalar = host.scalarRead(load);
            if (!scalar) {
                check(!hostScalar,
                      "host and owner agree that an unrelated read has no scalar edge");
                return;
            }
            check(hostScalar && position < expected.size(),
                  "each scalar load has one matching host proof and expected dependency row");
            if (!hostScalar || position >= expected.size()) { return; }
            ctjs::StoreGlobalOp initialization;
            unsigned writes = 0;
            module.walk([&](ctjs::StoreGlobalOp store) {
                if (store.getName() != load.getName()) { return; }
                initialization = store;
                ++writes;
            });
            check(writes == 1 && scalar->initialization == initialization && scalar->read == load &&
                      scalar->value == initialization.getValue() &&
                      scalar->alternatives == alternatives && load->getParentOp() == entry &&
                      initialization->getParentOp() == entry &&
                      initialization->isBeforeInBlock(load) && !owner.lookup(load),
                  "the scalar edge records the exact earlier store, stored value and live load");
            check(hostScalar->initialization == scalar->initialization &&
                      hostScalar->read == scalar->read && hostScalar->value == scalar->value &&
                      hostScalar->alternatives == scalar->alternatives &&
                      hostScalar->dependencies == scalar->dependencies,
                  "owner scalar edges retain the complete host proof without inventing evidence");
            const auto & dependencies = expected[position++];
            check(scalar->dependencies.size() == dependencies.size(),
                  "constant origins neither require nor fabricate published-result dependencies");
            if (scalar->dependencies.size() != dependencies.size()) { return; }
            for (unsigned index = 0; index < dependencies.size(); ++index) {
                const auto & call = calls[dependencies[index]];
                check(scalar->dependencies[index] == call.call->getResult(0) &&
                          call.call->isBeforeInBlock(initialization) && call.capturedMap &&
                          call.capturedMap->parameters.size() == 2 && host.callable(call.call),
                      "each dependency occurrence names its exact completed published call");
            }
        });
        check(position == expected.size(), "no scalar edge is keyed by a different live load");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & text, bool expected,
                             const Dependencies & dependencies, const char * message,
                             mlir::TypeID tag = mlir::TypeID::get<ctjs::NumberAttr>(),
                             bool objectObservation = false) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        check(static_cast<bool>(module), "saved scalar source/prepared fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis host(*module, contract);
        OwnedGlobalRoots owner(*module, contract);
        const bool expectedHost = expected || objectObservation;
        check(host.proved() == expectedHost && owner.proved() == expected && !host.exhausted() &&
                  !owner.exhausted(),
              message);
        if (host.proved() != expectedHost || owner.proved() != expected) {
            std::fprintf(stderr, "scalar reads %s row %u: host=%s owner=%s\n",
                         prepared ? "prepared" : "source", rows, host.reason().str().c_str(),
                         owner.reason().str().c_str());
        }
        if (expected && host.proved() && owner.proved()) {
            evidence(*module, host, owner, dependencies, tag);
        } else if (!expected) {
            check(scalarReadsEmpty(*module, host) && scalarReadsEmpty(*module, owner) &&
                      empty(*module, owner),
                  "failed complete proofs expose no scalar loads or partial owner");
        }
        if (objectObservation) {
            check(host.objectReads().size() == 1 &&
                      owner.reason() == "object key global cannot be a scalar observation",
                  "a proved caller object still cannot become a scalar output");
            if (host.objectReads().size() == 1) {
                const auto & edge = host.objectReads().front();
                auto load = edge.read;
                auto initialization = edge.initialization;
                auto made = edge.object;
                check(load.getName() == "savedSum" &&
                          initialization.getValue() == made.getResult() && !host.scalarRead(load) &&
                          !owner.scalarRead(load),
                      "the exact object initializer and load acquire no scalar authority");
            }
        }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "scalar queries preserve the original global, arithmetic and call operations");
    };
    const std::string store = "    ctjs.store_global \"savedSum\", %sum\n";
    const std::string read = "    %savedSum = ctjs.load_global \"savedSum\"\n";
    const std::string sum = "ctjs.binary add %putResult, %secondResult";
    const std::string actual = prepared ? "%thirdPutterEnv, %savedSum)" : "%owned, %savedSum)";
    const auto actualUsing = [&](const std::string & name) {
        return prepared ? "%thirdPutterEnv, %" + name + ")" : "%owned, %" + name + ")";
    };
    variant(source, true, {{0, 1}}, "the original saved arithmetic has exact per-load evidence");
    variant(replaced(source, store, "    ctjs.store_global \"savedSum\", %putResult\n"), true,
            {{0}}, "a saved first result records its actual producing call");
    variant(replaced(source, store, "    ctjs.store_global \"savedSum\", %secondResult\n"), true,
            {{1}}, "a saved second result cannot borrow the first invocation's identity");
    variant(replaced(source, sum, "ctjs.binary add %putResult, %putResult"), true, {{0, 0}},
            "two arithmetic occurrences retain the same exact dependency twice");
    variant(replaced(source, sum, "ctjs.binary add %putResult, %actual"), true, {{0}},
            "literal operands do not fabricate a published-call dependency");
    auto repeated =
        replaced(source, read, read + "    %savedAgain = ctjs.load_global \"savedSum\"\n");
    repeated = replaced(repeated, actual, actualUsing("savedAgain"));
    variant(repeated, true, {{0, 1}, {0, 1}},
            "two reads of one sole store have separate exact load edges");
    auto alias = replaced(source, read,
                          read + "    ctjs.store_global \"savedAlias\", %savedSum\n"
                                 "    %savedAlias = ctjs.load_global \"savedAlias\"\n");
    alias = replaced(alias, actual, actualUsing("savedAlias"));
    variant(alias, true, {{0, 1}, {0, 1}},
            "an ordered saved alias retains every original published-result dependency");
    auto later = replaced(source, "    %combined =",
                          "    ctjs.store_global \"savedLater\", %thirdResult\n"
                          "    %savedLater = ctjs.load_global \"savedLater\"\n"
                          "    %combined =");
    later = replaced(later, "ctjs.binary add %sum, %thirdResult",
                     "ctjs.binary add %savedSum, %savedLater");
    variant(later, true, {{0, 1}, {2}},
            "independent saved globals preserve their separate source producing calls");
    const std::string constantStore = "    ctjs.store_global \"savedSum\", %actual\n";
    const auto constant = replaced(source, store, constantStore);
    variant(constant, true, {{}},
            "a Number literal has exact initialization evidence without a published result");
    variant(replaced(source, store, "    ctjs.store_global \"savedSum\", %u\n"), false, {},
            "Undefined cannot borrow the observation's previously proved Number category");
    for (const std::string & marker : {store, read, std::string("    %combined =")}) {
        variant(replaced(source, marker, "    ctjs.store_global \"savedSum\", %actual\n" + marker),
                false, {}, "an additional write at any source position invalidates the sole store");
    }
    const auto renamed = replaced(
        replaced(source, "ctjs.store_global \"savedSum\"", "ctjs.store_global \"ordinaryNumber\""),
        "ctjs.load_global \"savedSum\"", "ctjs.load_global \"ordinaryNumber\"");
    variant(renamed, true, {{0, 1}}, "the binding's spelling supplies no scalar proof authority");
    variant(replaced(source, read, "    %savedSum = ctjs.load_global \"unwritten\"\n"), false, {},
            "an unrelated global cannot borrow the saved store's Number proof");
    variant(replaced(source, "    %combined =",
                     "    %unrelated = ctjs.load_global \"unwritten\"\n"
                     "    %combined ="),
            false, {}, "an unproved extra global withholds every completed scalar edge");
    variant(replaced(repeated, store, constantStore), true, {{}, {}},
            "each constant-only load has its own empty-dependency initialization edge");
    variant(replaced(alias, store, constantStore), true, {{}, {}},
            "an ordered constant-only alias preserves the actual scalar origins");
    variant(replaced(source, sum, "ctjs.binary add %actual, %actual"), true, {{}},
            "constant-only Number arithmetic keeps empty published-call dependencies");
    variant(replaced(constant, read,
                     read + "    %constantSum = ctjs.binary add %savedSum, %actual\n"
                            "    ctjs.store_global \"constantAlias\", %constantSum\n"
                            "    %constantAlias = ctjs.load_global \"constantAlias\"\n"),
            true, {{}, {}}, "constant aliases can contribute to another proved Number expression");
    variant(replaced(constant, constantStore + read, read + constantStore), false, {},
            "a constant-only read before initialization cannot borrow its future literal");
    for (const std::string & marker : {constantStore, read, std::string("    %combined =")}) {
        variant(replaced(constant, marker, constantStore + marker), false, {},
                "constant Number authority still requires one write across the whole program");
    }
    variant(replaced(constant, read, "    %savedSum = ctjs.load_global \"unwritten\"\n"), false, {},
            "a constant cannot initialize a different binding by spelling alone");
    variant(
        replaced(constant, constantStore, "    %unrelated = ctjs.call %this(%u)\n" + constantStore),
        false, {}, "an unrelated unknown call invalidates the complete constant environment");
    for (const char * initializer : {"ctjs.constant #ctjs.bigint<\"7\">", "ctjs.create_object"}) {
        variant(replaced(constant, constantStore,
                         std::string("    %nonNumber = ") + initializer +
                             "\n    ctjs.store_global \"savedSum\", %nonNumber\n"),
                false, {}, "BigInt and Object initializers cannot acquire Number load authority",
                mlir::TypeID::get<ctjs::NumberAttr>(),
                llvm::StringRef(initializer) == "ctjs.create_object");
    }
    check(rows == 28, "all independently saved and constant-only scalar source controls ran");

    // Keep the numeric results and arithmetic unchanged, while every key
    // actual becomes Boolean. The saved load must still prove its own origin
    // before the third call can join the complete future-call census.
    const std::string numberLiteral =
        "    %actual = ctjs.constant #ctjs.number<4607182418800017408>\n";
    const std::string booleanLiteral = "    %actual = ctjs.constant #ctjs.boolean<false>\n";
    const auto boolean = replaced(constant, numberLiteral, booleanLiteral);
    const auto booleanTag = mlir::TypeID::get<ctjs::BooleanAttr>();
    for (const bool truth : {false, true}) {
        const auto program =
            truth ? replaced(boolean, "#ctjs.boolean<false>", "#ctjs.boolean<true>") : boolean;
        variant(program, true, {{}},
                "both Boolean literals retain exact origins without borrowing a Number category",
                booleanTag);
        variant(replaced(program, read, read + "    %savedAgain = ctjs.load_global \"savedSum\"\n"),
                true, {{}, {}}, "repeated Boolean reads each name their actual sole store",
                booleanTag);
        variant(replaced(replaced(program, read,
                                  read + "    ctjs.store_global \"savedAlias\", %savedSum\n"
                                         "    %savedAlias = ctjs.load_global \"savedAlias\"\n"),
                         actual, actualUsing("savedAlias")),
                true, {{}, {}}, "Boolean aliases preserve actual source-order initialization",
                booleanTag);
        variant(replaced(program, constantStore,
                         "    %inverted = ctjs.unary not %actual\n"
                         "    ctjs.store_global \"savedSum\", %inverted\n"),
                true, {{}}, "a proved Boolean negation retains its independent scalar category",
                booleanTag);
    }
    variant(replaced(boolean, constantStore + read, read + constantStore), false, {},
            "a Boolean read before initialization cannot borrow its future literal");
    for (const std::string & marker : {constantStore, read, std::string("    %combined =")}) {
        variant(replaced(boolean, marker, constantStore + marker), false, {},
                "a second Boolean write anywhere removes single-store argument authority");
        variant(replaced(boolean, marker, "    ctjs.store_global \"savedSum\", %u\n" + marker),
                false, {}, "a possibly absent Boolean origin cannot borrow a definite category");
        variant(replaced(boolean, marker, "    %unrelated = ctjs.call %this(%u)\n" + marker), false,
                {}, "unrelated effects invalidate the complete Boolean environment");
    }
    variant(replaced(boolean, read, "    %savedSum = ctjs.load_global \"unwritten\"\n"), false, {},
            "a different unwritten binding cannot acquire the Boolean store's proof");
    variant(replaced(replaced(boolean, actual, actualUsing("actual")), read, ""), true, {},
            "a requested Boolean observation cannot manufacture a missing scalar read");
    variant(replaced(boolean, "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n",
                     "    ctjs.store_global \"savedSum\", %entryKey\n"
                     "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"),
            false, {}, "a future published method write invalidates Boolean global authority");
    for (const char * initializer :
         {"ctjs.constant #ctjs.number<0>", "ctjs.constant #ctjs.string<\"false\">",
          "ctjs.constant #ctjs.undefined", "ctjs.constant #ctjs.bigint<\"0\">",
          "ctjs.create_object"}) {
        variant(replaced(boolean, constantStore,
                         std::string("    %nonBoolean = ") + initializer +
                             "\n    ctjs.store_global \"savedSum\", %nonBoolean\n"),
                false, {},
                "incompatible scalars and requested objects retain the Boolean owner boundary",
                booleanTag, llvm::StringRef(initializer) == "ctjs.create_object");
    }
    check(rows == 54, "all historical and Boolean scalar source controls ran");

    const std::string stringLiteral =
        "    %actual = ctjs.constant #ctjs.string<\"owned scalar\">\n";
    const auto string = replaced(constant, numberLiteral, stringLiteral);
    const auto stringTag = mlir::TypeID::get<ctjs::StringAttr>();
    for (const std::string & value :
         {std::string("owned scalar"), std::string(),
          std::string("quote\\22 newline\\0A nul\\00tail % = ;"), std::string(512, 'x')}) {
        const auto program = replaced(string, "owned scalar", value);
        variant(program, true, {{}}, "String bytes do not replace exact initialization evidence",
                stringTag);
        variant(replaced(program, read, read + "    %savedAgain = ctjs.load_global \"savedSum\"\n"),
                true, {{}, {}}, "each repeated String read subscribes to its actual sole store",
                stringTag);
        variant(replaced(replaced(program, read,
                                  read + "    ctjs.store_global \"savedAlias\", %savedSum\n"
                                         "    %savedAlias = ctjs.load_global \"savedAlias\"\n"),
                         actual, actualUsing("savedAlias")),
                true, {{}, {}}, "String copy chains preserve independent empty dependencies",
                stringTag);
    }
    variant(replaced(string, constantStore,
                     "    %kind = ctjs.unary typeof %actual\n"
                     "    ctjs.store_global \"savedSum\", %kind\n"),
            true, {{}}, "an exact typeof result retains its original String-producing SSA value",
            stringTag);
    variant(replaced(string, constantStore + read, read + constantStore), false, {},
            "a String read before initialization cannot borrow its future literal");
    for (const std::string & marker : {constantStore, read, std::string("    %combined =")}) {
        variant(replaced(string, marker, constantStore + marker), false, {},
                "a duplicate String write anywhere invalidates sole-store authority");
        variant(replaced(string, marker, "    ctjs.store_global \"savedSum\", %u\n" + marker),
                false, {}, "String and Undefined stores cannot acquire a definite String edge");
        variant(replaced(string, marker, "    %unrelated = ctjs.call %this(%u)\n" + marker), false,
                {}, "unknown effects invalidate the complete owning String environment");
    }
    variant(replaced(string, read, "    %savedSum = ctjs.load_global \"unwritten\"\n"), false, {},
            "an unwritten binding cannot borrow an owning String initializer");
    variant(replaced(replaced(string, actual, actualUsing("actual")), read, ""), true, {},
            "a requested String observation cannot invent a missing source load");
    variant(replaced(string, "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n",
                     "    ctjs.store_global \"savedSum\", %entryKey\n"
                     "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"),
            false, {}, "a future method String write invalidates the whole initialization proof");
    for (const char * initializer :
         {"ctjs.constant #ctjs.number<0>", "ctjs.constant #ctjs.boolean<false>",
          "ctjs.constant #ctjs.null", "ctjs.constant #ctjs.undefined",
          "ctjs.constant #ctjs.bigint<\"0\">", "ctjs.create_object"}) {
        const bool absent = llvm::StringRef(initializer) == "ctjs.constant #ctjs.null" ||
                            llvm::StringRef(initializer) == "ctjs.constant #ctjs.undefined";
        variant(replaced(string, constantStore,
                         std::string("    %nonString = ") + initializer +
                             "\n    ctjs.store_global \"savedSum\", %nonString\n"),
                absent, {},
                absent
                    ? "existing nullable String keys need no invented scalar String edge"
                    : "incompatible scalars and requested objects retain the String owner boundary",
                stringTag, llvm::StringRef(initializer) == "ctjs.create_object");
    }
    variant(replaced(string, constantStore, "    ctjs.store_global \"savedSum\", %this\n"), false,
            {}, "an unknown entry parameter cannot acquire String authority from observations");
    for (const char * operation : {"add", "sub", "mul", "div", "mod", "pow"}) {
        variant(replaced(string, constantStore,
                         std::string("    %calculated = ctjs.binary ") + operation +
                             " %actual, %actual\n"
                             "    ctjs.store_global \"savedSum\", %calculated\n"),
                false, {}, "String operands cannot reuse the Number-only arithmetic proof");
    }
    check(rows == 93, "all historical, Boolean and String scalar source controls ran");

    for (unsigned origin = 0; origin < 6; ++origin) {
        const bool constantOnly = origin != 0;
        const bool booleanOnly = origin == 2 || origin == 3;
        const bool stringOnly = origin >= 4;
        const auto tag = stringOnly    ? stringTag
                         : booleanOnly ? booleanTag
                                       : mlir::TypeID::get<ctjs::NumberAttr>();
        const Dependencies expected = constantOnly ? Dependencies{{}} : Dependencies{{0, 1}};
        const auto program = origin == 5  ? replaced(string, "owned scalar", "")
                             : stringOnly ? string
                             : origin == 3
                                 ? replaced(boolean, "#ctjs.boolean<false>", "#ctjs.boolean<true>")
                             : booleanOnly  ? boolean
                             : constantOnly ? constant
                                            : source;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "saved scalar live-mutation fixture parses");
        if (!module) { return; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        check(complete.proved() && complete.steps() < 18000,
              "complete saved scalar host evidence is bounded");
        if (!complete.proved() || complete.steps() >= 18000) { return; }
        const unsigned completion = complete.steps();
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      scalarReadsEmpty(*module, limited),
                  "every incomplete host budget withholds every saved scalar edge");
        }
        HostContractAnalysis exact(*module, contract, completion);
        OwnedGlobalRoots original(*module, contract);
        check(exact.proved() && exact.steps() == completion && original.proved(),
              "the exact host completion budget publishes the complete scalar proof");
        if (!exact.proved() || !original.proved()) { return; }
        evidence(*module, exact, original, expected, tag);
        const unsigned ownerCompletion = original.steps();
        for (const unsigned budget : {0u, ownerCompletion / 2, ownerCompletion - 1}) {
            OwnedGlobalRoots limited(*module, contract, budget);
            check(!limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                      scalarReadsEmpty(*module, limited) && empty(*module, limited),
                  "incomplete ownership never exposes constant or published-result scalar edges");
        }
        OwnedGlobalRoots exactOwner(*module, contract, ownerCompletion);
        check(exactOwner.proved() && exactOwner.steps() == ownerCompletion,
              "exact owner work atomically publishes the same initialized scalar edges");
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.scalar_global", attributes.getStringAttr("number"));
            operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("number"));
            operation->setAttr("ctnative.inferred_result", attributes.getStringAttr("number"));
        });
        contract = requested(*module);
        HostContractAnalysis forgedHost(*module, contract);
        OwnedGlobalRoots forgedOwner(*module, contract);
        check(forgedHost.proved() && forgedOwner.proved(),
              "forged reports do not alter independently proved scalar source edges");
        if (!forgedHost.proved() || !forgedOwner.proved()) { return; }
        evidence(*module, forgedHost, forgedOwner, expected, tag);
        mlir::OwningOpRef<mlir::ModuleOp> clone{llvm::cast<mlir::ModuleOp>(module->clone())};
        const auto clonedContract = requested(*clone);
        HostContractAnalysis clonedHost(*clone, clonedContract);
        OwnedGlobalRoots clonedOwner(*clone, clonedContract);
        check(clonedHost.proved() && clonedOwner.proved(),
              "a fresh clone rederives scalar edges against its own live operations");
        if (clonedHost.proved() && clonedOwner.proved()) {
            evidence(*clone, clonedHost, clonedOwner, expected, tag);
        }
        const auto scalar = forgedOwner.scalarReads().front();
        auto initialization = scalar.initialization;
        auto load = scalar.read;
        auto entry = module->lookupSymbol<ctjs::FuncOp>("script$0");
        const auto calls = forgedOwner.roots().front().methodTable->calls;
        unsigned mutations = 0;
        const auto refusal = [&]() {
            HostContractAnalysis staleHost(*module, contract);
            OwnedGlobalRoots staleOwner(*module, contract);
            check(!staleHost.proved() && !staleOwner.proved() &&
                      staleHost.reason().contains("fingerprint") &&
                      staleOwner.reason().contains("fingerprint") &&
                      scalarReadsEmpty(*module, staleHost) && scalarReadsEmpty(*module, staleOwner),
                  "stale fingerprints expose no previously saved scalar evidence");
            const auto freshContract = requested(*module);
            HostContractAnalysis freshHost(*module, freshContract);
            OwnedGlobalRoots freshOwner(*module, freshContract);
            check(
                !freshHost.proved() && !freshOwner.proved() && !freshHost.exhausted() &&
                    !freshOwner.exhausted() && scalarReadsEmpty(*module, freshHost) &&
                    scalarReadsEmpty(*module, freshOwner) && empty(*module, freshOwner),
                "fresh fingerprints and Number reports cannot repair changed scalar source edges");
            ++mutations;
        };
        const auto restored = [&]() {
            HostContractAnalysis host(*module, contract);
            OwnedGlobalRoots owner(*module, contract);
            check(host.proved() && owner.proved(),
                  "restoring source edges restores the scalar proof");
            if (host.proved() && owner.proved()) { evidence(*module, host, owner, expected, tag); }
        };
        const auto operand = [&](mlir::Operation * operation, unsigned index, mlir::Value value) {
            const auto old = operation->getOperand(index);
            operation->setOperand(index, value);
            refusal();
            operation->setOperand(index, old);
            restored();
        };
        operand(initialization, 0, load.getResult());
        operand(initialization, 0, calls[2].call->getResult(0));
        operand(initialization, 0, entry.getBody().front().getArgument(0));
        operand(calls[0].call, prepared ? 4u : 2u, load.getResult());
        operand(calls[2].call, prepared ? 0u : 1u, entry.getBody().front().getArgument(0));
        auto lastRead = calls[3].read;
        operand(calls[2].call, prepared ? 2u : 0u, lastRead.getResult());
        initialization->moveAfter(load);
        refusal();
        initialization->moveBefore(load);
        restored();
        auto * previous = load->getPrevNode();
        load->moveBefore(calls[0].call);
        refusal();
        load->moveAfter(previous);
        restored();
        load->setAttr("name", attributes.getStringAttr("trace"));
        refusal();
        load->setAttr("name", attributes.getStringAttr("savedSum"));
        restored();
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
        operand(setter.getBody().front().getTerminator(), 0,
                setter.getBody().front().getArgument(0));
        operand(getter.getBody().front().getTerminator(), 0,
                getter.getBody().front().getArgument(0));
        if (constantOnly) {
            // Only query the source proof after malformed SSA edits. The type
            // solver and verifier are entitled to assume lexical SSA validity.
            auto value = initialization.getValue().getDefiningOp<ctjs::ConstantOp>();
            check(static_cast<bool>(value),
                  "the constant scope fixture retains its original literal");
            if (value) {
                mlir::OpBuilder builder(getter.getBody().front().getTerminator());
                auto * foreign = builder.clone(*value);
                initialization->setOperand(0, foreign->getResult(0));
                refusal();
                initialization->setOperand(0, value.getResult());
                foreign->erase();
                restored();
                builder.setInsertionPointAfter(initialization);
                auto * later = builder.clone(*value);
                initialization->setOperand(0, later->getResult(0));
                refusal();
                initialization->setOperand(0, value.getResult());
                later->erase();
                restored();
            }
        }
        std::printf("scalar reads %s %s: %u rows, %u live edits, all %u host budgets checked\n",
                    prepared ? "prepared" : "source",
                    origin == 5    ? "String empty"
                    : stringOnly   ? "String owning"
                    : origin == 3  ? "Boolean true"
                    : booleanOnly  ? "Boolean false"
                    : constantOnly ? "constant"
                                   : "published",
                    rows, mutations, completion);
    }

    for (unsigned origin = 0; origin < 3; ++origin) {
        const bool booleanOnly = origin == 1;
        const bool stringOnly = origin == 2;
        const auto scoped =
            replaced(stringOnly    ? string
                     : booleanOnly ? boolean
                                   : constant,
                     constantStore,
                     "    %condition = ctjs.truthy %actual\n"
                     "    scf.if %condition {\n"
                     "      %thenNumber = ctjs.constant #ctjs.number<0> {test_scope}\n"
                     "      scf.yield\n"
                     "    } else {\n"
                     "      %elseNumber = ctjs.constant #ctjs.number<0> {test_scope}\n"
                     "      scf.yield\n"
                     "    }\n" +
                         constantStore);
        auto program = scoped;
        if (booleanOnly || stringOnly) {
            for (const char * name : {"thenNumber", "elseNumber"}) {
                const auto definition = std::string("%") + name + " = ctjs.constant ";
                program = replaced(program, definition + "#ctjs.number<0>",
                                   definition + (stringOnly ? "#ctjs.string<\"owned scalar\">"
                                                            : "#ctjs.boolean<false>"));
            }
        }
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "constant-only inaccessible-arm fixture parses");
        if (!module) { return; }
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.scalar_global", attributes.getStringAttr("number"));
            operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
        });
        const auto contract = requested(*module);
        const auto valid = [&]() {
            HostContractAnalysis host(*module, contract);
            OwnedGlobalRoots owner(*module, contract);
            check(host.proved() && host.scalarReads().size() == 1 && owner.proved() &&
                      owner.scalarReads().size() == 1 && owner.roots().size() == 1,
                  "valid constant scopes preserve the complete host and owner scalar evidence");
            if (host.scalarReads().size() == 1) {
                check(host.scalarReads().front().dependencies.empty(),
                      "constant host scope proof has no published-call dependency");
            }
        };
        valid();
        ctjs::StoreGlobalOp initialization;
        std::vector<mlir::Value> inaccessible;
        module->walk([&](mlir::Operation * operation) {
            if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(operation);
                store && store.getName() == "savedSum") {
                initialization = store;
            }
            if (operation->hasAttr("test_scope")) {
                inaccessible.push_back(operation->getResult(0));
            }
        });
        check(initialization && inaccessible.size() == 2,
              "both inaccessible scalar arms and the exact initializer survive source parsing");
        if (!initialization || inaccessible.size() != 2) { return; }
        const auto original = initialization.getValue();
        for (mlir::Value value : inaccessible) {
            initialization->setOperand(0, value);
            HostContractAnalysis stale(*module, contract);
            HostContractAnalysis fresh(*module, requested(*module));
            OwnedGlobalRoots owner(*module, requested(*module));
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      scalarReadsEmpty(*module, stale) && !fresh.proved() && !fresh.exhausted() &&
                      scalarReadsEmpty(*module, fresh) && !owner.proved() &&
                      scalarReadsEmpty(*module, owner),
                  "an inaccessible scalar arm cannot initialize an outer constant scalar load");
            initialization->setOperand(0, original);
            valid();
        }
        std::printf("scalar reads %s: two inaccessible constant scalar arms checked\n",
                    prepared ? "prepared" : "source");
    }
}

} // namespace ctcompile::test::owned_global_shared_map
