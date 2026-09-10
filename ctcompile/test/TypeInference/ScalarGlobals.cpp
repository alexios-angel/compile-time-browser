#include "Tests.h"

namespace ctcompile::test::type_inference {

// Exercise the dependency, rather than relying on a particular worklist order:
// the literal arrives only after the alias first waits, then the actual saved
// SSA lattice widens three times. These are sound overapproximations of the
// same immutable program. No host category supplies a native type here.
class ScheduledScalarInference final : public TypeInference {
public:
    enum class Kind {
        Number,
        Boolean,
        String
    };

    ScheduledScalarInference(mlir::DataFlowSolver & solver,
                             const ctcompile::ctnative::OwnedGlobalRoots * owner,
                             mlir::Operation * literal, mlir::Value saved,
                             mlir::Operation * observed, Kind kind = Kind::Number)
        : TypeInference(solver, owner), solver_(solver), literal_(literal), saved_(saved),
          observed_(observed), kind_(kind) {}

    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const TypeLattice *> operands,
                                       llvm::ArrayRef<TypeLattice *> results) override {
        if (op == literal_ && stage_ == 0) { return mlir::success(); }
        if (failed(TypeInference::visitOperation(op, operands, results))) {
            return mlir::failure();
        }
        if (op != observed_) { return mlir::success(); }
        using namespace ctcompile::ctnative;
        const auto type = results.front()->getValue().getType();
        auto * context = op->getContext();
        const mlir::Type initial = kind_ == Kind::String    ? mlir::Type(defaultStringType(context))
                                   : kind_ == Kind::Boolean ? mlir::Type(BoolType::get(context))
                                                            : NumType::get(context, NumKind::I32);
        const mlir::Type wider =
            kind_ == Kind::String
                ? mlir::Type(VariantType::get(context, {BoolType::get(context), initial}))
            : kind_ == Kind::Boolean
                ? mlir::Type(VariantType::get(
                      context, {BoolType::get(context), NumType::get(context, NumKind::I32)}))
                : NumType::get(context, NumKind::F64);
        if (stage_ == 0 && !type) {
            ++stage_;
            solver_.enqueue({solver_.getProgramPointAfter(literal_), this});
        } else if (stage_ == 1 && type == initial) {
            ++stage_;
            widen(wider);
        } else if (stage_ == 2 && type == wider) {
            ++stage_;
            widen(OptType::get(context, type));
        } else if (stage_ == 3 && type == OptType::get(context, wider)) {
            ++stage_;
            widen(BoxedType::get(context));
        } else if (stage_ == 4 && type && llvm::isa<BoxedType>(type)) {
            ++stage_;
        }
        return mlir::success();
    }

    [[nodiscard]] unsigned stages() const { return stage_; }

private:
    void widen(mlir::Type type) {
        auto * producer = solver_.getOrCreateState<TypeLattice>(saved_);
        propagateIfChanged(producer, producer->join(ctcompile::ctnative::TypeValue{type}));
    }

    mlir::DataFlowSolver & solver_;
    mlir::Operation * literal_;
    mlir::Value saved_;
    mlir::Operation * observed_;
    Kind kind_;
    unsigned stage_ = 0;
};

void checkSavedScalarGlobalTypes(mlir::MLIRContext & context) {
    namespace fixtures = ctcompile::test::owned_global_methods;
    namespace ctjs = ctcompile::ctjs;
    using ctcompile::ctnative::HostContractAnalysis;
    using ctcompile::ctnative::OwnedGlobalRoots;
    using fixtures::replaced;
    const auto require = [](bool condition, const char * message) {
        if (condition) { return; }
        std::printf("FAIL scalar global inference: %s\n", message);
        ++failures;
    };
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = fixtures::contractFor(module);
        contract.initialIntrinsics = {"Map"};
        contract.observations = {"saved", "alias", "trace"};
        return contract;
    };
    const std::string store = "    ctjs.store_global \"saved\", %answer\n";
    const std::string read = "    %saved = ctjs.load_global \"saved\"\n";
    const std::string alias = "    ctjs.store_global \"alias\", %saved\n"
                              "    %aliasResult = ctjs.load_global \"alias\" {check}\n";
    auto source =
        replaced(fixtures::capturedFixture, "    ctjs.store_global \"trace\", %answer\n",
                 store + read + alias + "    ctjs.store_global \"trace\", %aliasResult\n");
    source = replaced(source, "    ctjs.return %size\n",
                      std::string("    %literal = ctjs.constant ") + kFive +
                          " {test_scalar_producer}\n    ctjs.return %literal\n");
    unsigned rows = 0;
    unsigned states = 0;
    for (const bool prepared : {false, true}) {
        auto program = source;
        if (prepared) {
            program = replaced(program, "    %cell = ctjs.create_cell %u\n", "");
            program = replaced(program, "    ctjs.cell_set %cell, %state\n", "");
            program = replaced(program, "captures %cell", "captures %state");
            program = replaced(program, "    %state = ctjs.load_upvalue %callee[0]\n", "");
            program = replaced(
                program, "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                "%state: !ctjs.value)");
            program = replaced(program, "upvalue_count = 1 : i32", "upvalue_count = 0 : i32");
            program = replaced(program, "%answer = ctjs.call %getter(%owned)",
                               "%environment = ctjs.load_upvalue %getter[0]\n"
                               "    %answer = ctjs.call_direct @get$3(%owned, %u, %getter, "
                               "%environment)");
        }
        const char * exact = prepared ? "!ctnative.num<i32>" : "!ctnative.boxed";
        const char * optional = prepared ? "!ctnative.opt<!ctnative.num<i32>>" : "!ctnative.boxed";
        const auto variant = [&](const std::string & text, bool proved, unsigned reads,
                                 const char * expected, const char * message,
                                 mlir::TypeID tag = mlir::TypeID::get<ctjs::NumberAttr>()) {
            ++rows;
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            require(static_cast<bool>(module), "source/prepared fixture parses");
            if (!module) { return; }
            const auto contract = requested(*module);
            HostContractAnalysis host(*module, contract);
            OwnedGlobalRoots owner(*module, contract);
            require(host.proved() == proved && owner.proved() == proved && !host.exhausted() &&
                        !owner.exhausted(),
                    message);
            require(host.scalarReads().size() == reads && owner.scalarReads().size() == reads,
                    "only complete live proofs supply the expected scalar edges");
            for (const auto & edge : owner.scalarReads()) {
                auto initialization = edge.initialization;
                require(edge.alternatives.tag() == tag && edge.value == initialization.getValue(),
                        "the scalar category describes the actual stored SSA value");
            }
            check(*module, message, expected, &owner);
            require(ctcompile::ctnative::hostContractFingerprint(*module) == contract.moduleSha256,
                    "solving leaves every source operation unchanged");
        };
        variant(program, true, 2, exact,
                "saved aliases join actual SSA types, even when a host category says Number");
        variant(replaced(program, kFive, kOneAndAHalf), true, 2,
                prepared ? "!ctnative.num<f64>" : "!ctnative.boxed",
                "a fractional producer cannot be narrowed to an integer by its category");
        variant(replaced(program, kFive, kNegativeZero), true, 2,
                prepared ? "!ctnative.num<f64>" : "!ctnative.boxed",
                "negative zero keeps the real producer's f64 lattice");
        auto repeated =
            replaced(program, read, read + "    %savedAgain = ctjs.load_global \"saved\"\n");
        repeated = replaced(repeated, "ctjs.store_global \"alias\", %saved",
                            "ctjs.store_global \"alias\", %savedAgain");
        variant(repeated, true, 3, exact, "each repeated load subscribes to the same sole store");
        variant(replaced(program, store, store + store), true, 0, optional,
                "two equal stores cannot borrow a previous single-store initialization proof");
        variant(replaced(program, store + read, read + store), false, 0, optional,
                "a load preceding its store retains implicit Undefined");
        variant(replaced(program, read, "    %saved = ctjs.load_global \"unknown\"\n"), false, 0,
                "!ctnative.boxed", "an undeclared global cannot borrow another binding's edge");
        for (const char * name : {"globalThis", "window"}) {
            variant(
                replaced(program, store,
                         std::string("    %dynamic = ctjs.load_global \"") + name + "\"\n" + store),
                false, 0, "!ctnative.boxed",
                "dynamic globals remain boxed despite the unchanged saved scalar program");
        }
        auto literal = replaced(program, store,
                                std::string("    %entryLiteral = ctjs.constant ") + kFive + "\n" +
                                    "    ctjs.store_global \"saved\", %entryLiteral\n");
        variant(literal, true, 2, "!ctnative.num<i32>",
                "constant-only aliases join the literal's actual initialized type");
        variant(replaced(literal, kFive, kOneAndAHalf), true, 2, "!ctnative.num<f64>",
                "constant-only fractional aliases preserve their original f64 producer");
        variant(replaced(literal, kFive, kNegativeZero), true, 2, "!ctnative.num<f64>",
                "constant-only negative zero cannot become an i32 through its category");
        const std::string literalStore = "    ctjs.store_global \"saved\", %entryLiteral\n";
        variant(replaced(literal, literalStore,
                         "    %sum = ctjs.binary add %entryLiteral, %entryLiteral\n"
                         "    ctjs.store_global \"saved\", %sum\n"),
                true, 2, "!ctnative.num<f64>",
                "constant Number arithmetic keeps the ordinary double arithmetic type");
        variant(replaced(literal, "    ctjs.store_global \"alias\", %saved\n",
                         "    %sum = ctjs.binary add %saved, %entryLiteral\n"
                         "    ctjs.store_global \"alias\", %sum\n"),
                true, 2, "!ctnative.num<f64>",
                "arithmetic after a constant alias retains the actual arithmetic lattice");
        variant(replaced(literal, read, read + "    %savedAgain = ctjs.load_global \"saved\"\n"),
                true, 3, "!ctnative.num<i32>",
                "repeated constant reads each subscribe to the one original literal");
        variant(replaced(literal, literalStore, literalStore + literalStore), true, 0,
                "!ctnative.opt<!ctnative.num<i32>>",
                "equal constant stores retain absence without a sole initialization edge");
        variant(replaced(literal, literalStore + read, read + literalStore), false, 0,
                "!ctnative.opt<!ctnative.num<i32>>",
                "a constant read before its initialization retains implicit Undefined");
        variant(replaced(literal, literalStore,
                         "    %unrelated = ctjs.call %this(%u)\n" + literalStore),
                false, 0, "!ctnative.opt<!ctnative.num<i32>>",
                "an unrelated unknown call blocks constant initialization authority");
        for (const char * name : {"globalThis", "window"}) {
            variant(replaced(literal, literalStore,
                             std::string("    %dynamic = ctjs.load_global \"") + name + "\"\n" +
                                 literalStore),
                    false, 0, "!ctnative.boxed",
                    "dynamic global access keeps constant-only observations boxed");
        }

        const auto booleanTag = mlir::TypeID::get<ctjs::BooleanAttr>();
        const auto boolean =
            replaced(literal, std::string("%entryLiteral = ctjs.constant ") + kFive,
                     "%entryLiteral = ctjs.constant #ctjs.boolean<false>");
        const auto booleanCall = replaced(program, kFive, "#ctjs.boolean<false>");
        const char * booleanExact = prepared ? "!ctnative.bool" : "!ctnative.boxed";
        for (const bool truth : {false, true}) {
            const auto constant =
                truth ? replaced(boolean, "#ctjs.boolean<false>", "#ctjs.boolean<true>") : boolean;
            const auto called =
                truth ? replaced(booleanCall, "#ctjs.boolean<false>", "#ctjs.boolean<true>")
                      : booleanCall;
            variant(constant, true, 2, "!ctnative.bool",
                    "false and true aliases infer Boolean from the actual stored literal",
                    booleanTag);
            variant(called, true, 2, booleanExact,
                    "a Boolean host result category cannot unbox an indirect source call",
                    booleanTag);
            variant(
                replaced(constant, read, read + "    %savedAgain = ctjs.load_global \"saved\"\n"),
                true, 3, "!ctnative.bool",
                "every repeated Boolean load retains its own actual-store subscription",
                booleanTag);
            variant(replaced(constant, literalStore,
                             "    %inverted = ctjs.unary not %entryLiteral\n"
                             "    ctjs.store_global \"saved\", %inverted\n"),
                    true, 2, "!ctnative.bool",
                    "a definite negated Boolean uses the ordinary unary-result lattice",
                    booleanTag);
        }
        variant(replaced(boolean, literalStore, literalStore + literalStore), true, 0,
                "!ctnative.opt<!ctnative.bool>",
                "identical Boolean stores cannot manufacture a sole initialization edge");
        variant(replaced(boolean, literalStore + read, read + literalStore), false, 0,
                "!ctnative.opt<!ctnative.bool>",
                "a Boolean load before its initializer retains implicit Undefined");
        variant(
            replaced(boolean, literalStore, literalStore + "    ctjs.store_global \"saved\", %u\n"),
            true, 0, "!ctnative.opt<!ctnative.bool>",
            "an actual Undefined write retains optional Boolean regardless of observations");
        variant(replaced(boolean, literalStore,
                         literalStore + "    %other = ctjs.constant #ctjs.boolean<true>\n"
                                        "    ctjs.store_global \"saved\", %other\n"),
                true, 0, "!ctnative.opt<!ctnative.bool>",
                "different Boolean stores retain absence without single-store authority");
        variant(replaced(boolean, literalStore,
                         literalStore + "    %other = ctjs.constant " + kFive +
                             "\n"
                             "    ctjs.store_global \"saved\", %other\n"),
                true, 0, "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>",
                "a Number write keeps the actual mixed Boolean and Number lattice");
        variant(replaced(boolean, literalStore, "    ctjs.store_global \"saved\", %this\n"), true,
                0, "!ctnative.boxed",
                "a requested Boolean observation cannot turn an unknown stored value into bool");
        variant(replaced(boolean, literalStore,
                         "    %unrelated = ctjs.call %this(%u)\n" + literalStore),
                false, 0, "!ctnative.opt<!ctnative.bool>",
                "unknown effects withhold Boolean initialization despite its literal store");
        for (const char * name : {"globalThis", "window"}) {
            variant(replaced(boolean, literalStore,
                             std::string("    %dynamic = ctjs.load_global \"") + name + "\"\n" +
                                 literalStore),
                    false, 0, "!ctnative.boxed",
                    "dynamic global access keeps Boolean observations boxed");
        }

        const auto stringTag = mlir::TypeID::get<ctjs::StringAttr>();
        const auto string =
            replaced(literal, std::string("%entryLiteral = ctjs.constant ") + kFive,
                     "%entryLiteral = ctjs.constant #ctjs.string<\"owned scalar\">");
        const auto stringCall = replaced(program, kFive, "#ctjs.string<\"owned scalar\">");
        const char * stringExact = prepared ? "!ctnative.str<utf8>" : "!ctnative.boxed";
        for (const std::string & value :
             {std::string("owned scalar"), std::string(),
              std::string("quote\\22 newline\\0A nul\\00tail % = ;"), std::string(512, 'x')}) {
            const auto constant = replaced(string, "owned scalar", value);
            variant(constant, true, 2, "!ctnative.str<utf8>",
                    "String aliases receive their actual owning literal type for every byte value",
                    stringTag);
            variant(replaced(stringCall, "owned scalar", value), true, 2, stringExact,
                    "a String result category cannot supply the type of an indirect source call",
                    stringTag);
            variant(
                replaced(constant, read, read + "    %savedAgain = ctjs.load_global \"saved\"\n"),
                true, 3, "!ctnative.str<utf8>",
                "every repeated String read subscribes independently to the actual stored value",
                stringTag);
        }
        variant(replaced(string, literalStore,
                         "    %kind = ctjs.unary typeof %entryLiteral\n"
                         "    ctjs.store_global \"saved\", %kind\n"),
                true, 2, "!ctnative.str<utf8>",
                "a typeof initializer retains the ordinary String-producing SSA lattice",
                stringTag);
        variant(replaced(string, literalStore, literalStore + literalStore), true, 0,
                "!ctnative.opt<!ctnative.str<utf8>>",
                "identical String writes cannot acquire sole-store initialization authority");
        variant(replaced(string, literalStore + read, read + literalStore), false, 0,
                "!ctnative.opt<!ctnative.str<utf8>>",
                "a String read before initialization retains implicit Undefined");
        for (const char * absent : {"undefined", "null"}) {
            variant(replaced(string, literalStore,
                             literalStore + "    %absent = ctjs.constant #ctjs." + absent +
                                 "\n    ctjs.store_global \"saved\", %absent\n"),
                    true, 0, "!ctnative.opt<!ctnative.str<utf8>>",
                    "an actual absent store cannot be removed by the requested String observation");
        }
        variant(replaced(string, literalStore,
                         literalStore + "    %other = ctjs.constant #ctjs.string<\"different\">\n"
                                        "    ctjs.store_global \"saved\", %other\n"),
                true, 0, "!ctnative.opt<!ctnative.str<utf8>>",
                "different String stores retain the ordinary absence seed");
        variant(replaced(string, literalStore,
                         literalStore + "    %other = ctjs.constant #ctjs.boolean<true>\n"
                                        "    ctjs.store_global \"saved\", %other\n"),
                true, 0, "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>>",
                "a Boolean write keeps the actual mixed Boolean and String lattice");
        variant(replaced(string, literalStore,
                         literalStore + "    %other = ctjs.constant " + kFive +
                             "\n    ctjs.store_global \"saved\", %other\n"),
                true, 0,
                "!ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>",
                "a Number write keeps the actual mixed Number and String lattice");
        variant(replaced(string, literalStore, "    ctjs.store_global \"saved\", %this\n"), true, 0,
                "!ctnative.boxed",
                "a requested String observation cannot turn an unknown stored value into String");
        variant(
            replaced(string, literalStore, "    %unrelated = ctjs.call %this(%u)\n" + literalStore),
            false, 0, "!ctnative.opt<!ctnative.str<utf8>>",
            "unknown effects withhold String initialization despite the literal store");
        for (const char * name : {"globalThis", "window"}) {
            variant(replaced(string, literalStore,
                             std::string("    %dynamic = ctjs.load_global \"") + name + "\"\n" +
                                 literalStore),
                    false, 0, "!ctnative.boxed",
                    "dynamic global access keeps String observations boxed");
        }

        for (unsigned origin = 0; origin < 10; ++origin) {
            const bool constantOnly = origin % 2 == 1;
            const bool booleanOnly = origin >= 2 && origin < 6;
            const bool stringOnly = origin >= 6;
            const char * actualType =
                stringOnly     ? (constantOnly ? "!ctnative.str<utf8>" : stringExact)
                : booleanOnly  ? (constantOnly ? "!ctnative.bool" : booleanExact)
                : constantOnly ? "!ctnative.num<i32>"
                               : exact;
            const char * absentType =
                stringOnly     ? (constantOnly || prepared ? "!ctnative.opt<!ctnative.str<utf8>>"
                                                           : "!ctnative.boxed")
                : booleanOnly  ? (constantOnly || prepared ? "!ctnative.opt<!ctnative.bool>"
                                                           : "!ctnative.boxed")
                : constantOnly ? "!ctnative.opt<!ctnative.num<i32>>"
                               : optional;
            auto text = stringOnly    ? (constantOnly ? string : stringCall)
                        : booleanOnly ? (constantOnly ? boolean : booleanCall)
                                      : (constantOnly ? literal : program);
            if (origin == 4 || origin == 5) {
                text = replaced(text, "#ctjs.boolean<false>", "#ctjs.boolean<true>");
            } else if (origin >= 8) {
                text = replaced(text, "owned scalar", "");
            }
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            require(static_cast<bool>(module), "live-proof fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            OwnedGlobalRoots complete(*module, contract);
            require(complete.proved() && complete.scalarReads().size() == 2,
                    "the immutable fixture has complete independent initialization evidence");
            if (!complete.proved() || complete.scalarReads().size() != 2) { continue; }
            check(*module, "omitting the owner retains the ordinary global absence seed",
                  absentType);
            const unsigned completion = complete.steps();
            require(completion > 0, "owner proof has a nonzero complete-work bound");
            for (const unsigned budget : {0u, completion / 2, completion - 1}) {
                OwnedGlobalRoots limited(*module, contract, budget);
                require(!limited.proved() && limited.exhausted() && limited.scalarReads().empty(),
                        "an incomplete ownership proof supplies no partial initialization edges");
                check(*module, "exhausted ownership retains implicit global absence", absentType,
                      &limited);
            }
            OwnedGlobalRoots exactBudget(*module, contract, completion);
            require(exactBudget.proved() && exactBudget.steps() == completion,
                    "the exact completion budget supplies the complete proof");
            check(*module, "the exact owner budget enables only the actual stored-value lattice",
                  actualType, &exactBudget);

            if (prepared || constantOnly) {
                mlir::Operation * delayed = nullptr;
                mlir::Operation * observed = nullptr;
                module->walk([&](mlir::Operation * op) {
                    if (!constantOnly && op->hasAttr("test_scalar_producer")) { delayed = op; }
                    if (op->hasAttr("check")) { observed = op; }
                });
                if (constantOnly) {
                    delayed = complete.scalarReads().front().value.getDefiningOp();
                }
                require(delayed && observed, "the scheduled producer and alias observation exist");
                if (delayed && observed) {
                    mlir::DataFlowSolver solver;
                    solver.load<mlir::dataflow::DeadCodeAnalysis>();
                    solver.load<mlir::dataflow::SparseConstantPropagation>();
                    const auto kind = stringOnly    ? ScheduledScalarInference::Kind::String
                                      : booleanOnly ? ScheduledScalarInference::Kind::Boolean
                                                    : ScheduledScalarInference::Kind::Number;
                    auto * inference = solver.load<ScheduledScalarInference>(
                        &complete, delayed, complete.scalarReads().front().value, observed, kind);
                    require(succeeded(solver.initializeAndRun(*module)),
                            "the scheduled scalar producer converges");
                    require(
                        inference->stages() == 5,
                        stringOnly ? "alias waits, receives String, widens to mixed and optional, "
                                     "then becomes boxed"
                        : booleanOnly ? "alias waits, receives bool, widens to mixed and optional, "
                                        "then becomes boxed"
                                      : "alias waits, receives i32, widens to f64 and optional, "
                                        "then becomes boxed");
                }
            }

            // Rebuild the borrowed owner after every edit. An old fingerprint must
            // refuse before exposing any edges; a new one must prove the live IR.
            mlir::Builder attributes(&context);
            module->walk([&](mlir::Operation * op) {
                op->setAttr("ctnative.scalar_global", attributes.getStringAttr("number"));
                op->setAttr("ctnative.inferred_result", attributes.getStringAttr("number"));
                op->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
                op->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            });
            contract = requested(*module);
            OwnedGlobalRoots reported(*module, contract);
            require(reported.proved(),
                    "report attributes leave valid live initialization provable");
            check(*module, "reports do not change independently inferred scalar types", actualType,
                  &reported);
            auto initialization = complete.scalarReads().front().initialization;
            auto savedRead = complete.scalarReads().front().read;
            const auto checkChanged = [&](const char * expected, bool proves = false) {
                OwnedGlobalRoots stale(*module, contract);
                require(!stale.proved() && stale.reason().contains("fingerprint") &&
                            stale.scalarReads().empty(),
                        "a stale fingerprint withholds all scalar initialization edges");
                check(*module, "a stale contract cannot drop implicit absence", absentType, &stale);
                const auto freshContract = requested(*module);
                OwnedGlobalRoots fresh(*module, freshContract);
                require(fresh.proved() == proves && !fresh.exhausted() &&
                            fresh.scalarReads().empty(),
                        "fresh proof checks the actual edited source instead of Number reports");
                check(*module, "the edited live source controls the scalar load", expected, &fresh);
                mlir::OwningOpRef<mlir::ModuleOp> clone{
                    llvm::cast<mlir::ModuleOp>(module->clone())};
                OwnedGlobalRoots cloned(*clone, requested(*clone));
                require(cloned.proved() == proves && cloned.scalarReads().empty(),
                        "a fresh clone independently rechecks edited edges");
                check(*clone, "fresh cloned source preserves the edited scalar type", expected,
                      &cloned);
                ++states;
            };
            const auto restored = [&]() {
                OwnedGlobalRoots owner(*module, contract);
                require(owner.proved(),
                        "restored source independently regains initialization evidence");
                check(*module, "restored initialization joins its unchanged real SSA type",
                      actualType, &owner);
            };
            initialization->moveAfter(savedRead);
            checkChanged(absentType);
            initialization->moveBefore(savedRead);
            restored();
            mlir::OpBuilder duplicateBuilder(initialization);
            duplicateBuilder.setInsertionPointAfter(initialization);
            auto * duplicate = duplicateBuilder.clone(*initialization);
            checkChanged(absentType, true);
            duplicate->erase();
            restored();
            savedRead->setAttr("name", attributes.getStringAttr("trace"));
            // This creates an uninitialized cycle in the ordinary global lattice;
            // query the finite proof alone rather than interpreting that cycle as
            // a fresh Number fact or asking the solver to guess an initializer.
            OwnedGlobalRoots wrongName(*module, requested(*module));
            require(!wrongName.proved() && wrongName.scalarReads().empty(),
                    "a changed global name cannot consume the old edge");
            ++states;
            savedRead->setAttr("name", attributes.getStringAttr("saved"));
            restored();
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
            auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
            const auto originalReturn = returned.getValue();
            returned->setOperand(0, getter.getBody().front().getArgument(0));
            // The changed method invalidates complete ownership. Its saved
            // result becomes boxed; an independent constant keeps its actual
            // literal type plus the ordinary global absence seed.
            OwnedGlobalRoots changedReturn(*module, requested(*module));
            require(!changedReturn.proved() && changedReturn.scalarReads().empty(),
                    "an unknown return cannot inherit the former Number category");
            check(*module, "a changed method cannot preserve initialization through Number reports",
                  constantOnly ? absentType : "!ctnative.boxed", &changedReturn);
            ++states;
            returned->setOperand(0, originalReturn);
            restored();
        }
    }
    require(rows == 122 && states == 80, "all source/prepared rows and live source edits ran");
    std::printf("scalar global inference: %u source/prepared rows, %u live edits, "
                "pending/String/Boolean/i32/f64/mixed/optional/boxed subscription checked\n",
                rows, states);
}

} // namespace ctcompile::test::type_inference
