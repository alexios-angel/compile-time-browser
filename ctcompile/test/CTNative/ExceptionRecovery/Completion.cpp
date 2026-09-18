#include "../../../lib/CTNative/HostContract/Preparation.h"
#include "Tests.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"

#include "check.hpp"

namespace ctcompile::test::exception_recovery {

bool check(bool condition, llvm::StringRef label) {
    if (!condition) {
        llvm::errs() << "FAIL " << label << '\n';
        ++ctbrowser_test_failures;
    }
    return condition;
}

std::string printed(mlir::Operation * operation) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    operation->print(stream);
    return text;
}

ctjs::FuncOp guarded(mlir::ModuleOp module) {
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (function.getSymName().starts_with("guarded$")) { return function; }
    }
    return {};
}

ctjs::CallDirectOp firstCall(ctjs::FuncOp function) {
    ctjs::CallDirectOp found;
    function.walk([&](ctjs::CallDirectOp call) {
        if (!found) { found = call; }
    });
    return found;
}

unsigned countChecks(ctjs::FuncOp function) {
    unsigned count = 0;
    function.walk([&](ctjs::CheckOp) { ++count; });
    return count;
}

mlir::OwningOpRef<mlir::ModuleOp> import(mlir::MLIRContext & context, llvm::StringRef source,
                                         bool resolve) {
    auto program = ctbrowser::script::compiler::compile(source.str());
    if (!check(program.ok, "source compiles")) { return {}; }
    auto imported = ctcompile::js::import_program(program, "invocation-recovery", &context);
    if (!check(static_cast<bool>(imported.module), "source imports")) { return {}; }
    if (resolve) {
        mlir::PassManager passes(&context);
        passes.addPass(ctjs::createCTJSResolveGlobals());
        passes.addPass(ctjs::createCTJSLiftToSCF());
        if (!check(mlir::succeeded(passes.run(*imported.module)), "source prepares")) { return {}; }
    }
    return std::move(imported.module);
}

// This checks the selected completion's SSA wiring. It supplies fixed helper
// outcomes, independently of helper implementation/type inference: every normal
// call returns 20; one selected call throws the fixture's known payload. Poison
// is never a number and cannot satisfy a selected completion observation.
struct completionTrace {
    ctjs::InvokeOp failing;
    double payload;
    unsigned remaining = 4096;

    std::optional<double> value(mlir::Value value) {
        if (remaining == 0) { return {}; }
        --remaining;
        if (auto literal = value.getDefiningOp<ctjs::ConstantOp>()) {
            auto attr = literal->getAttr("value");
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(attr)) {
                return std::bit_cast<double>(number.getBits());
            }
            if (auto boolean = llvm::dyn_cast<ctjs::BooleanAttr>(attr)) {
                return boolean.getValue() ? 1 : 0;
            }
        }
        if (auto literal = value.getDefiningOp<mlir::arith::ConstantOp>()) {
            if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(literal.getValue())) {
                if (integer.getType().isInteger(1)) { return integer.getValue().isOne() ? 1 : 0; }
                return static_cast<double>(integer.getInt());
            }
        }
        if (auto truthy = value.getDefiningOp<ctjs::TruthyOp>()) {
            return this->value(truthy->getOperand(0));
        }
        if (auto comparison = value.getDefiningOp<mlir::arith::CmpIOp>()) {
            auto left = this->value(comparison.getLhs());
            auto right = this->value(comparison.getRhs());
            if (left && right && comparison.getPredicate() == mlir::arith::CmpIPredicate::eq) {
                return *left == *right ? 1 : 0;
            }
            return {};
        }
        if (llvm::isa_and_nonnull<ctjs::FromBoolOp, mlir::arith::IndexCastUIOp,
                                  mlir::arith::ExtUIOp, mlir::arith::TruncIOp>(
                value.getDefiningOp())) {
            return this->value(value.getDefiningOp()->getOperand(0));
        }
        if (auto invocation = value.getDefiningOp<ctjs::InvokeOp>()) {
            const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
            const bool failed = invocation == failing;
            if (index == 0) { return failed ? 1 : 0; }
            if (index == 1) { return failed ? std::optional<double>{} : 20; }
            if (!failed) { return {}; }
            if (index == 2) { return payload; }
            auto exit =
                llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().getTerminator());
            return this->value(exit.getState()[index - 3]);
        }
        if (auto branch = value.getDefiningOp<mlir::scf::IfOp>()) {
            auto condition = this->value(branch.getCondition());
            if (!condition) { return {}; }
            auto & region = *condition != 0 ? branch.getThenRegion() : branch.getElseRegion();
            return this->value(region.front().getTerminator()->getOperand(
                llvm::cast<mlir::OpResult>(value).getResultNumber()));
        }
        return {};
    }
};

void checkCompletion(ctjs::TryOp attempt, ctjs::InvokeOp failing, double payload,
                     double expectedState) {
    auto exit = llvm::cast<ctjs::TryExitOp>(attempt.getBody().front().getTerminator());
    completionTrace trace{failing, payload};
    check(trace.value(exit.getIsThrow()) == (failing ? 1 : 0),
          "enclosing try selects call completion");
    if (!failing) {
        check(trace.value(exit.getNormalResult()) == 20, "normal return publishes the call result");
        return;
    }
    auto & handler = attempt.getCatchBody().front();
    ctjs::BinaryOp add;
    for (auto binary : handler.getOps<ctjs::BinaryOp>()) { add = binary; }
    if (!check(static_cast<bool>(add), "fixture catch keeps its state-plus-payload addition")) {
        return;
    }
    auto saved = llvm::dyn_cast<mlir::BlockArgument>(add->getOperand(0));
    auto thrown = llvm::dyn_cast<mlir::BlockArgument>(add->getOperand(1));
    if (!check(saved && thrown && saved.getOwner() == &handler && thrown.getArgNumber() == 0,
               "catch reads its own payload and saved state")) {
        return;
    }
    check(trace.value(exit.getCaughtValues()[saved.getArgNumber()]) == expectedState,
          "catch receives state immediately before the failed call");
    check(trace.value(exit.getCaughtValues()[0]) == payload,
          "catch receives the invocation's thrown payload");
}

void testSource(mlir::MLIRContext & context, llvm::StringRef name, llvm::StringRef source,
                unsigned expectedCalls, double saved, double payload, ExceptionRecoveryMode mode,
                bool resolve) {
    auto module = import(context, source, resolve);
    if (!module) { return; }
    auto function = guarded(*module);
    if (!check(static_cast<bool>(function), "guarded source function survives")) { return; }
    const auto original = printed(function);
    mlir::OwningOpRef<ctjs::FuncOp> detached(llvm::cast<ctjs::FuncOp>(function->clone()));
    const auto snapshot = printed(*detached);
    const auto checks = countChecks(function);
    using ctcompile::ctnative::lowering_detail::inspectSingleInvocationRegion;
    auto inspected = inspectSingleInvocationRegion(function);
    check(inspected.proved() == (expectedCalls == 1) && inspected.steps != 0 &&
              printed(function) == original,
          "source inspection selects exactly one invocation without rewriting any source");
    if (inspected.proved()) {
        check(inspected.push && inspected.landing && inspected.frame && inspected.check &&
                  inspected.call->getParentOfType<ctjs::FuncOp>() == function &&
                  inspected.check == inspected.call->getBlock()->getTerminator() &&
                  inspected.push.getHandler() == inspected.landing->getBlock() &&
                  llvm::is_contained(inspected.prefix, &function.getBody().front()) &&
                  llvm::is_contained(inspected.prefix, inspected.push->getBlock()) &&
                  llvm::is_contained(inspected.normal, inspected.call->getBlock()) &&
                  llvm::is_contained(inspected.caught, inspected.landing->getBlock()) &&
                  llvm::equal(inspected.check.getHandlerOperands(),
                              inspected.call->getBlock()->getArguments()) &&
                  inspected.call->getResult(0).hasOneUse(),
              "inspection publishes original handler and unpublished call snapshot handles");
    } else {
        check(!inspected.push && !inspected.landing && !inspected.frame && !inspected.check &&
                  inspected.prefix.empty() && inspected.normal.empty() &&
                  inspected.caught.empty() &&
                  inspected.refusal.find("one checked call") != std::string::npos,
              "multiple invocations publish no partial source evidence");
    }
    if (name == "ordinary assignment" && inspected.proved()) {
        const auto none = inspectSingleInvocationRegion(function, 100000, 0);
        check(!none.proved() && none.chain.empty() && printed(function) == original,
              "a zero call limit refuses even one checked invocation");
        for (unsigned budget = 0; budget < inspected.steps; ++budget) {
            const auto limited = inspectSingleInvocationRegion(function, budget);
            if (!check(!limited.proved() && !limited.push && !limited.landing && !limited.frame &&
                           !limited.check && limited.steps <= budget && limited.prefix.empty() &&
                           limited.normal.empty() && limited.caught.empty() &&
                           limited.refusal.find("budget exhausted") != std::string::npos &&
                           printed(function) == original && countChecks(function) == checks,
                       "every incomplete inspection budget preserves all source and evidence")) {
                break;
            }
        }
        const auto exact = inspectSingleInvocationRegion(function, inspected.steps);
        check(exact.proved() && exact.call == inspected.call && exact.check == inspected.check &&
                  exact.steps == inspected.steps && printed(function) == original,
              "the exact inspection budget rederives the same original call");
        const unsigned width = static_cast<unsigned>(inspected.check.getContOperands().size());
        const auto saved = inspected.check.getHandlerOperands().front();
        inspected.check->setOperand(width, inspected.call->getResult(0));
        const auto changed = printed(function);
        const auto invalid = inspectSingleInvocationRegion(function);
        check(!invalid.proved() && !invalid.push && !invalid.landing && !invalid.frame &&
                  !invalid.check && invalid.prefix.empty() && invalid.normal.empty() &&
                  invalid.caught.empty() &&
                  invalid.refusal.find("invocation") != std::string::npos &&
                  printed(function) == changed,
              "fresh inspection rejects a mutated failure snapshot without editing it");
        inspected.check->setOperand(width, saved);
        check(printed(function) == original,
              "inspection mutation control restores original source");
        llvm::outs() << "single invocation inspection: " << inspected.steps
                     << " steps, every incomplete budget preserves " << checks << " checks\n";
    }
    // Successful recovery keeps these original values alive in its rollback
    // snapshot. Calls also carry the importer's literal undefined receiver.
    llvm::SmallVector<llvm::SmallVector<mlir::Value>> callInputs;
    function.walk([&](ctjs::CallOp call) { callInputs.emplace_back(call->getOperands()); });
    auto ordinary = recoverPrimitiveExceptionRegion(function);
    check(!ordinary.recovered &&
              ordinary.refusal ==
                  "native try/catch needs an explicit throw in its active handler" &&
              printed(function) == original,
          "ordinary native recovery keeps its exact throwing-call refusal and source graph");

    auto recovered = recoverPrimitiveExceptionRegion(function, 100000, mode);
    if (!check(recovered.recovered, ("checked invocation recovery succeeds for " + name).str())) {
        llvm::errs() << recovered.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "recovered completion IR verifies");
    check(recovered.original && printed(*recovered.original) == snapshot &&
              countChecks(*recovered.original) == checks && checks > expectedCalls,
          "rollback retains every original call and non-call status edge");
    llvm::SmallVector<ctjs::InvokeOp> invocations;
    ctjs::TryOp attempt;
    function.walk<mlir::WalkOrder::PreOrder>(
        [&](ctjs::InvokeOp invocation) { invocations.push_back(invocation); });
    function.walk([&](ctjs::TryOp found) { attempt = found; });
    if (!check(invocations.size() == expectedCalls && attempt,
               "one invocation per source call reaches the enclosing try")) {
        return;
    }
    for (auto [index, invocation] : llvm::enumerate(invocations)) {
        auto * call = &invocation.getBody().front().front();
        auto dispatch = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
        check(resolve ? llvm::isa<ctjs::CallDirectOp>(call) : llvm::isa<ctjs::CallOp>(call),
              "recovery preserves the original call kind without inventing a direct target");
        check(call->getResult(0).hasOneUse() && dispatch.getNormalResult() == call->getResult(0),
              "call result has only its normal dispatch use");
        if (!resolve &&
            check(index < callInputs.size() && callInputs[index].size() == call->getNumOperands(),
                  "ordinary invocation preserves every original call operand")) {
            for (auto [operand, original] : llvm::zip(call->getOperands(), callInputs[index])) {
                if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(original)) {
                    const unsigned slot = argument.getArgNumber();
                    check(slot < dispatch.getState().size() && operand == dispatch.getState()[slot],
                          "callee, receiver and arguments retain exact pre-call register slots");
                } else {
                    auto literal = original.getDefiningOp<ctjs::ConstantOp>();
                    auto copied = operand.getDefiningOp<ctjs::ConstantOp>();
                    check(literal && copied && literal.getValue() == copied.getValue(),
                          "ordinary invocation preserves the original literal operand");
                }
            }
        }
        check(llvm::all_of(dispatch.getState(),
                           [&](mlir::Value state) {
                               return !invocation.getBody().isAncestor(state.getParentRegion());
                           }),
              "every saved register is defined before the call body");
        auto normal = llvm::cast<ctjs::InvokeYieldOp>(invocation.getNormalBody().front().back());
        auto unwind = llvm::cast<ctjs::InvokeYieldOp>(invocation.getUnwindBody().front().back());
        check(
            normal.getValues()[1] == invocation.getNormalBody().front().getArgument(0) &&
                llvm::isa_and_nonnull<mlir::ub::PoisonOp>(unwind.getValues()[1].getDefiningOp()) &&
                llvm::equal(unwind.getValues().drop_front(2),
                            invocation.getUnwindBody().front().getArguments()),
            "normal publication and unwind payload/state stay separate");
    }
    checkCompletion(attempt, invocations.back(), payload, saved);
    checkCompletion(attempt, {}, payload, 0);
    if (expectedCalls == 2) { checkCompletion(attempt, invocations.front(), payload, 0); }
    // The first argument-free handler cannot be recovered a second time. A
    // failed rerun must not consume its existing continuations.
    const auto structured = printed(function);
    auto rerun = recoverPrimitiveExceptionRegion(function, 100000, mode);
    check(!rerun.recovered && printed(function) == structured,
          "recovery rerun preserves the existing invocation graph");

    // Restore exactly as the native transaction does after admission refuses.
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(function) == original, "source rollback is byte-identical");
    for (unsigned budget : {0u, 1u, 64u, 256u}) {
        auto limited = recoverPrimitiveExceptionRegion(function, budget, mode);
        check(!limited.recovered && limited.refusal.find("budget exhausted") != std::string::npos &&
                  printed(function) == original,
              "bounded recovery leaves all original status edges on failure");
    }
    if (!resolve) {
        for (unsigned budget = 0; budget < recovered.steps; ++budget) {
            auto limited = recoverPrimitiveExceptionRegion(function, budget, mode);
            if (!check(!limited.recovered &&
                           limited.refusal.find("budget exhausted") != std::string::npos &&
                           printed(function) == original && countChecks(function) == checks,
                       "every incomplete ordinary-call budget preserves source and checks")) {
                llvm::errs() << "budget " << budget << ": " << limited.refusal << '\n';
                return;
            }
        }
        auto exact = recoverPrimitiveExceptionRegion(function, recovered.steps, mode);
        if (!check(exact.recovered && exact.steps == recovered.steps,
                   "the complete ordinary-call budget succeeds exactly")) {
            return;
        }
        function.getBody().takeBody(exact.original->getBody());
        function->setAttrs((*exact.original)->getAttrs());
        check(printed(function) == original,
              "exact-budget ordinary call rollback is byte-identical");
    }
    llvm::outs() << name << ": " << expectedCalls << " checked invocation(s), " << checks
                 << " original checks retained for admission rollback, " << recovered.steps
                 << " steps, effects=" << (mode == ExceptionRecoveryMode::EffectCheckedInvocations)
                 << '\n';
}

void checkCompletionTypes(mlir::ModuleOp module, ctjs::FuncOp function, llvm::StringRef normalType,
                          llvm::StringRef stateType,
                          llvm::StringRef payloadType = "!ctnative.boxed") {
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<ctcompile::ctnative::TypeInference>();
    const auto before = printed(module);
    if (!check(mlir::succeeded(solver.initializeAndRun(module)),
               "recovered source completion inference converges")) {
        return;
    }
    const auto expect = [&](mlir::Value value, llvm::StringRef wanted, llvm::StringRef label) {
        const auto * lattice = solver.lookupState<ctcompile::ctnative::TypeLattice>(value);
        std::string type;
        if (lattice) {
            llvm::raw_string_ostream into(type);
            lattice->getValue().print(into);
        }
        if (!check(type == wanted, label)) {
            llvm::errs() << "  wanted " << wanted << ", found " << type << '\n';
        }
    };
    unsigned invokes = 0, states = 0;
    function.walk([&](ctjs::InvokeOp invocation) {
        ++invokes;
        auto * call = &invocation.getBody().front().front();
        expect(call->getResult(0), normalType, "successful source call has its own normal type");
        expect(invocation.getNormalBody().front().getArgument(0), normalType,
               "normal continuation receives only the successful returned value");
        expect(invocation.getUnwindBody().front().getArgument(0), payloadType,
               "unwind inference preserves the imported frame's failure alternative");
    });
    function.walk([&](ctjs::TryOp attempt) {
        auto & caught = attempt.getCatchBody().front();
        expect(caught.getArgument(0), payloadType,
               "enclosing catch cannot narrow an unproved frame failure");
        for (auto saved : caught.getArguments().drop_front()) {
            ++states;
            expect(saved, stateType, "pre-call catch state is independent of the payload type");
        }
    });
    check(invokes != 0 && states != 0, "source type witness includes invocation and saved state");
    check(printed(module) == before, "completion inference does not change source operations");
}

void testDOMURITransaction(mlir::MLIRContext & context) {
    // A late typed-DOM refusal must roll back consumed class metadata too.
    for (const auto provider : {ctnative::HostContract::Provider::ctbrowserDOM,
                                ctnative::HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 35; ++control) {
            std::string source =
                control >= 5
                    ? "function guarded(element) { class Shape { "
                      "constructor(value) { this.element = value; } "
                      "read() { return this.element.getAttribute('x') === null; } " +
                          std::string(
                              control == 6
                                  ? "unused() { this.element = {}; return this.read(); } "
                                  : "unused() { return this.element.hasAttribute('x'); } ") +
                          "} const shape = new Shape(element); " +
                          std::string(control == 7 ? "const other = new Shape({}); " : "") +
                          "return shape.read(); }"
                    : "function guarded(element) { class Shape { constructor() { this.key = 'x'; } "
                      "read() { return this.key; } } const shape = new Shape(); " +
                          std::string(control == 1 ? "element.unknown(); " : "") +
                          "return element.getAttribute(shape.read()) === null; }";
            if (control >= 8) {
                source = "function guarded(element) { class Shape { "
                         "constructor(value) { this.element = value; } "
                         "read(key) { return this.element.getAttribute(key); } " +
                         std::string(control == 10
                                         ? "unused(target) { return target.getAttribute('x'); } "
                                         : "") +
                         "} const shape = new Shape(element); const first = shape.read('x'); " +
                         std::string(control == 11 ? "const other = new Shape(element); " : "") +
                         std::string(control == 12 ? "shape.element = {}; " : "") +
                         "return shape.read(" + std::string(control == 9 ? "{}" : "'x'") +
                         ") === first; }";
            }
            if (control >= 13) {
                source = "function guarded(element) { class Shape { "
                         "constructor(value) { this.element = value; } "
                         "read(key) { return this.element.getAttribute(key); } "
                         "forward(key) { return this.read(" +
                         std::string(control == 14 ? "{}" : "key") +
                         "); } press(key) { return this.forward(key); } "
                         "} const shape = new Shape(element); " +
                         std::string(control == 15 ? "shape.element = {}; " : "") +
                         std::string(control == 16 ? "const other = new Shape(element); " : "") +
                         (control == 18 ? "return element.hasAttribute('x'); }"
                                        : "return shape.press('x') === null; }");
            }
            if (control >= 19) {
                source = "function guarded(element) { class Shape { "
                         "constructor(value) { this.element = value; } "
                         "static get DefaultType() { return 'x'; } "
                         "read(key = this.constructor.DefaultType) { "
                         "return this.element.getAttribute(key); } "
                         "forward(key) { return this.read(key); } " +
                         std::string(control == 22 ? "unused(key = 'x') { return this.read(key); } "
                                                   : "") +
                         "} const shape = new Shape(element); return shape.forward(" +
                         std::string(control == 20   ? "null"
                                     : control == 23 ? "void 0"
                                                     : "") +
                         ") === null; }";
                if (control == 21 || control == 24) {
                    const auto at = source.find("key = this.constructor.DefaultType");
                    source.replace(at, std::string("key = this.constructor.DefaultType").size(),
                                   control == 21 ? "key = {}" : "key = element.unknown()");
                }
            }
            if (control >= 25) {
                source = "function guarded(element) { class Shape { "
                         "static get NAME() { throw new Error('unused NAME'); } "
                         "constructor(value) { this.element = value; } "
                         "read() { return this.element.getAttribute('x'); } "
                         "} const shape = new Shape(element); " +
                         std::string(control == 30 ? "Shape.NAME; " : "") +
                         std::string(control == 31 ? "element.unknown(); " : "") +
                         "return shape.read() === null; }";
                if (control == 27) { source += " Error = 9;"; }
                if (control == 34) {
                    source.insert(source.find("return shape.read()"), "Error = 9; ");
                }
                if (control == 28) { source.replace(source.find("throw new"), 9, "return new"); }
                if (control == 29) {
                    source.replace(source.find("'unused NAME'"), 13, "unknown()");
                }
            }
            auto candidate = import(context, source, true);
            if (!candidate) { return; }
            // Input reports cannot bypass any source proof.
            (*candidate)->setAttr("ctnative.supplied", mlir::UnitAttr::get(&context));
            const auto original = printed(*candidate);
            ctnative::HostContract request;
            request.provider = provider;
            request.entry = guarded(*candidate).getSymName().str();
            request.elementParameters = {0};
            request.initialIntrinsics = {"__ctbrowser_class_defined"};
            if (control == 2 || (control >= 25 && control != 26)) {
                request.initialIntrinsics.push_back("Error");
            }
            if (control == 33) { request.initialIntrinsics.push_back("Error"); }
            request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
            const auto before = request;
            auto error = ctnative::prepareDOMEntry(*candidate, request,
                                                   control == 3 || control == 32   ? 0
                                                   : control == 4 || control == 17 ? 1000
                                                                                   : 100000);
            if (control != 0 && control != 2 && control != 5 && control != 8 && control != 13 &&
                control != 19 && control != 23 && control != 25) {
                check(static_cast<bool>(error), "unproved class/DOM composition refuses");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original &&
                          request.moduleSha256 == before.moduleSha256 &&
                          request.initialIntrinsics == before.initialIntrinsics &&
                          request.elementParameters == before.elementParameters &&
                          request.provider == before.provider && request.entry == before.entry,
                      "class/DOM refusal preserves original source and contract");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis checked(*candidate, request);
                check(mlir::succeeded(mlir::verify(*candidate)) && checked.proved() &&
                          request.initialIntrinsics.empty() &&
                          request.moduleSha256 == ctnative::hostContractFingerprint(*candidate) &&
                          !(*candidate)->hasAttr("ctnative.supplied"),
                      "class/DOM composition publishes only the fresh typed DOM proof");
            }
        }
    }
    using ctnative::lowering_detail::inspectSingleInvocationRegion;
    using ctnative::lowering_detail::normalizeDOMURI;
    auto module = import(context, R"js(
function guarded(element) {
    var text = element.hasAttribute("x") ? "%41" : "%";
    var saved = "before";
    try { saved = decodeURIComponent(text); }
    catch (ignored) { return saved; }
    return saved;
}
)js",
                         false);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto original = printed(*module);
    const unsigned checks = countChecks(function);
    ctnative::HostContract contract;
    contract.provider = ctnative::HostContract::Provider::ctbrowserDOM;
    contract.entry = function.getSymName().str();
    contract.elementParameters = {0};
    contract.initialIntrinsics = {"decodeURIComponent"};
    contract.moduleSha256 = ctnative::hostContractFingerprint(*module);
    for (const auto provider : {ctnative::HostContract::Provider::ctbrowserDOM,
                                ctnative::HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 4; ++control) {
            mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
            auto request = contract;
            request.provider = provider;
            if (control == 1) { request.moduleSha256 = "stale"; }
            if (control == 2) { request.initialIntrinsics.clear(); }
            const auto fingerprint = request.moduleSha256;
            auto error = ctnative::prepareDOMEntry(*candidate, request, control == 3 ? 0 : 100000);
            if (control) {
                check(static_cast<bool>(error), "unproved DOM preparation refuses");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original && request.moduleSha256 == fingerprint &&
                          request.provider == provider && request.entry == contract.entry &&
                          request.elementParameters == contract.elementParameters &&
                          request.initialIntrinsics == (control == 2 ? std::vector<std::string>{}
                                                                     : contract.initialIntrinsics),
                      "DOM preparation refusal preserves source and host request together");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis prepared(*candidate, request);
                check(mlir::succeeded(mlir::verify(*candidate)) && prepared.proved() &&
                          !prepared.wrapper() && prepared.entry().isPublic() &&
                          request.moduleSha256 == ctnative::hostContractFingerprint(*candidate) &&
                          request.moduleSha256 != fingerprint,
                      "DOM preparation publishes the normalized entry with its fresh proof");
            }
        }
    }
    mlir::OwningOpRef<mlir::ModuleOp> normalized(module->clone());
    if (auto error = normalizeDOMURI(*normalized, contract)) {
        check(false, "original URI source normalizes with unused payload and saved String");
        llvm::errs() << llvm::toString(std::move(error)) << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*normalized)), "normalized original URI source verifies");
    auto fresh = contract;
    fresh.moduleSha256 = ctnative::hostContractFingerprint(*normalized);
    const ctnative::DOMEntryAnalysis proof(*normalized, fresh);
    if (!check(proof.proved(), "normalized original URI source passes fresh complete DOM proof")) {
        llvm::errs() << proof.reason() << '\n';
        return;
    }
    unsigned invocations = 0;
    normalized->walk([&](ctjs::InvokeOp invocation) {
        ++invocations;
        auto & normal = invocation.getNormalBody().front();
        auto & caught = invocation.getUnwindBody().front();
        auto exit = llvm::cast<ctjs::InvokeExitOp>(invocation.getBody().front().back());
        auto success = llvm::cast<ctjs::InvokeYieldOp>(normal.back());
        auto failure = llvm::cast<ctjs::InvokeYieldOp>(caught.back());
        auto saved = failure.getValues().front().getDefiningOp<ctjs::ConstantOp>();
        auto string =
            saved ? llvm::dyn_cast<ctjs::StringAttr>(saved.getValue()) : ctjs::StringAttr{};
        check(proof.invocation(invocation) && exit.getState().empty() &&
                  caught.getNumArguments() == 1 && caught.getArgument(0).use_empty() &&
                  success.getValues().front() == normal.getArgument(0) && string &&
                  string.getValue() == "before",
              "URI normal result and original pre-call catch String remain separate");
    });
    check(invocations == 2 && countChecks(guarded(*normalized)) == 0,
          "both dynamic prefix arms retain their URI completion and discharge proved checks");
    const auto attempt = [&](unsigned budget) {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        const auto before = printed(*candidate);
        auto error = normalizeDOMURI(*candidate, contract, budget);
        if (!error) { return true; }
        const auto reason = llvm::toString(std::move(error));
        check(reason.find("budget exhausted") != std::string::npos &&
                  printed(*candidate) == before && countChecks(guarded(*candidate)) == checks,
              "incomplete URI normalization preserves the complete source and all checks");
        return false;
    };
    unsigned lower = 0, upper = 100000;
    while (lower < upper) {
        const unsigned middle = lower + (upper - lower) / 2;
        if (attempt(middle)) {
            upper = middle;
        } else {
            lower = middle + 1;
        }
    }
    check(attempt(lower), "the first complete URI normalization budget succeeds");
    for (unsigned budget = 0; budget < lower; ++budget) {
        if (!check(!attempt(budget), "every smaller URI normalization budget refuses")) { break; }
    }
    for (bool payload : {false, true}) {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        auto source = inspectSingleInvocationRegion(guarded(*candidate));
        if (!check(source.proved(), "URI mutation starts from the exact original snapshot")) {
            return;
        }
        if (payload) {
            auto returned =
                llvm::dyn_cast<ctjs::ReturnOp>(source.landing->getBlock()->getTerminator());
            if (!check(static_cast<bool>(returned), "URI fixture catch returns its saved state")) {
                return;
            }
            returned->setOperand(0, source.landing.getThrown());
        } else {
            source.check->setOperand(static_cast<unsigned>(source.check.getContOperands().size()),
                                     source.call->getResult(0));
        }
        auto changedContract = contract;
        changedContract.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
        const auto before = printed(*candidate);
        auto error = normalizeDOMURI(*candidate, changedContract);
        if (!check(static_cast<bool>(error), "mutated URI snapshot or observed payload refuses")) {
            continue;
        }
        const auto reason = llvm::toString(std::move(error));
        check(reason.find(payload ? "semantic error payload" : "invocation") != std::string::npos &&
                  printed(*candidate) == before && countChecks(guarded(*candidate)) == checks,
              "fresh URI refusal retains the exact mutated source and every status edge");
    }
    check(printed(*module) == original,
          "URI transaction tests retain their untouched source oracle");
    llvm::outs() << "DOM URI normalization: " << lower
                 << " steps, every incomplete budget preserves " << checks << " checks\n";
}

// Original M's protected body: JSON.parse is looked up first, then
// decodeURIComponent runs, then parse consumes its result. Both calls become
// nested invokes on one success path; either failure reaches the same catch.
void testDOMJSONChain(mlir::MLIRContext & context) {
    using ctnative::lowering_detail::inspectSingleInvocationRegion;
    using ctnative::lowering_detail::normalizeDOMURI;
    auto module = import(context, R"js(
function guarded(text) {
    try { return JSON.parse(decodeURIComponent(text)); }
    catch (ignored) { return text; }
}
)js",
                         false);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto original = printed(*module);
    const unsigned checks = countChecks(function);
    const auto single = inspectSingleInvocationRegion(function);
    auto chain = inspectSingleInvocationRegion(function, 100000, 2);
    if (!check(!single.proved() && single.refusal.find("one checked call") != std::string::npos &&
                   chain.proved() && chain.chain.size() == 2 &&
                   chain.call == chain.chain[0].first && chain.check == chain.chain[0].second &&
                   chain.chain[1].second.getCont() != chain.chain[0].second.getCont() &&
                   printed(*module) == original,
               "a two-call chain is published only when requested and in source order")) {
        return;
    }
    for (unsigned budget = 0; budget < chain.steps; ++budget) {
        const auto limited = inspectSingleInvocationRegion(function, budget, 2);
        if (!check(!limited.proved() && !limited.push && !limited.landing && !limited.frame &&
                       !limited.check && limited.chain.empty() && limited.steps <= budget &&
                       limited.prefix.empty() && limited.normal.empty() && limited.caught.empty() &&
                       limited.refusal.find("budget exhausted") != std::string::npos &&
                       printed(*module) == original,
                   "every incomplete chain inspection budget publishes no partial evidence")) {
            break;
        }
    }
    const auto exact = inspectSingleInvocationRegion(function, chain.steps, 2);
    check(exact.proved() && exact.chain == chain.chain && exact.steps == chain.steps,
          "the exact chain inspection budget preserves source order");
    auto conditional = import(context, R"js(
function guarded(text) {
    try {
        if (text) { return JSON.parse(decodeURIComponent(text)); }
        return text;
    } catch (ignored) { return text; }
}
)js",
                              false);
    if (!conditional) { return; }
    const auto branched = inspectSingleInvocationRegion(guarded(*conditional), 100000, 2);
    check(!branched.proved() && branched.chain.empty() &&
              branched.refusal.find("one success path") != std::string::npos,
          "a conditional bypass cannot publish a straight-line invocation chain");
    ctnative::HostContract contract;
    contract.provider = ctnative::HostContract::Provider::ctbrowserDOM;
    contract.entry = function.getSymName().str();
    contract.initialIntrinsics = {"decodeURIComponent"};
    contract.moduleSha256 = ctnative::hostContractFingerprint(*module);
    {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        auto error = normalizeDOMURI(*candidate, contract);
        check(error && printed(*candidate) == original,
              "without the initial JSON binding the chain refuses and the source survives");
        llvm::consumeError(std::move(error));
    }
    contract.initialIntrinsics = {"decodeURIComponent", "JSON"};
    mlir::OwningOpRef<mlir::ModuleOp> normalized(module->clone());
    if (auto error = normalizeDOMURI(*normalized, contract)) {
        check(false, "JSON.parse(decodeURIComponent(text)) normalizes as a nested chain");
        llvm::errs() << llvm::toString(std::move(error)) << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*normalized)), "normalized JSON chain verifies");
    ctjs::InvokeOp outer, nested;
    unsigned invocations = 0;
    normalized->walk([&](ctjs::InvokeOp invocation) {
        ++invocations;
        auto & caught = invocation.getUnwindBody().front();
        auto failure = llvm::cast<ctjs::InvokeYieldOp>(caught.getTerminator());
        check(caught.getNumArguments() == 1 && caught.getArgument(0).use_empty() &&
                  failure.getValues().front() ==
                      guarded(*normalized).getBody().front().getArgument(ctjs::implicit_arguments),
              "every chained catch returns the original input without observing its payload");
        if (invocation->getParentOfType<ctjs::InvokeOp>()) {
            nested = invocation;
        } else {
            outer = invocation;
        }
    });
    if (!check(invocations == 2 && outer && nested && countChecks(guarded(*normalized)) == 0 &&
                   printed(*module) == original,
               "the parse invoke nests inside the decode success continuation")) {
        return;
    }
    auto decode = llvm::cast<ctjs::CallOp>(outer.getBody().front().front());
    auto parse = llvm::cast<ctjs::CallOp>(nested.getBody().front().front());
    auto decoder = decode.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    auto member = parse.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
    auto json =
        member ? member.getObject().getDefiningOp<ctjs::LoadGlobalOp>() : ctjs::LoadGlobalOp{};
    check(decoder && decoder.getName() == "decodeURIComponent" && member && json &&
              json.getName() == "JSON" && ctjs::constantKey(member.getKey()) == "parse" &&
              member->getBlock() == outer->getBlock() && member->isBeforeInBlock(outer) &&
              parse.getReceiver() == json.getResult() && parse.getArgs().size() == 1 &&
              parse.getArgs().front() == outer.getNormalBody().front().getArgument(0) &&
              nested->getParentRegion() == &outer.getNormalBody(),
          "the original JSON.parse lookup precedes decoding and only decode success runs parse");

    const auto attempt = [&](unsigned budget) {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        auto error = normalizeDOMURI(*candidate, contract, budget);
        if (!error) { return true; }
        const auto reason = llvm::toString(std::move(error));
        check(reason.find("budget exhausted") != std::string::npos &&
                  printed(*candidate) == original && countChecks(guarded(*candidate)) == checks,
              "incomplete JSON normalization preserves both calls and every original check");
        return false;
    };
    unsigned lower = 0, upper = 100000;
    while (lower < upper) {
        const unsigned middle = lower + (upper - lower) / 2;
        if (attempt(middle)) {
            upper = middle;
        } else {
            lower = middle + 1;
        }
    }
    check(attempt(lower), "the first complete JSON normalization budget succeeds");
    for (unsigned budget = 0; budget < lower; ++budget) {
        if (!check(!attempt(budget), "every smaller JSON normalization budget refuses")) { break; }
    }
    for (unsigned index = 0; index != 3; ++index) {
        mlir::OwningOpRef<mlir::ModuleOp> candidate(module->clone());
        auto source = inspectSingleInvocationRegion(guarded(*candidate), 100000, 2);
        if (index != 2) {
            auto [call, checked] = source.chain[index];
            checked->setOperand(static_cast<unsigned>(checked.getContOperands().size()),
                                call->getResult(0));
        } else {
            guarded(*candidate).walk([&](ctjs::GetPropertyOp read) {
                read->setOperand(
                    0, guarded(*candidate).getBody().front().getArgument(ctjs::implicit_arguments));
            });
        }
        auto changed = contract;
        changed.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
        const auto before = printed(*candidate);
        auto error = normalizeDOMURI(*candidate, changed);
        if (!check(static_cast<bool>(error),
                   "a changed invocation snapshot or JSON origin refuses")) {
            continue;
        }
        check(llvm::toString(std::move(error)).find(index == 2 ? "nonthrowing" : "invocation") !=
                      std::string::npos &&
                  printed(*candidate) == before && countChecks(guarded(*candidate)) == checks,
              "a failed chain proof retains the exact mutated source and all checks");
    }

    auto assigned = import(context, R"js(
function guarded(text) {
    var saved = "before";
    try { return JSON.parse(saved = decodeURIComponent(text)); }
    catch (ignored) { return saved; }
}
)js",
                           false);
    if (!assigned) { return; }
    auto savedContract = contract;
    savedContract.moduleSha256 = ctnative::hostContractFingerprint(*assigned);
    if (auto error = normalizeDOMURI(*assigned, savedContract)) {
        check(false, "a saved assignment between chained calls normalizes");
        llvm::errs() << llvm::toString(std::move(error)) << '\n';
        return;
    }
    assigned->walk([&](ctjs::InvokeOp invocation) {
        if (invocation->getParentOfType<ctjs::InvokeOp>()) {
            nested = invocation;
        } else {
            outer = invocation;
        }
    });
    auto decodeFailure =
        llvm::cast<ctjs::InvokeYieldOp>(outer.getUnwindBody().front().getTerminator());
    auto parseFailure =
        llvm::cast<ctjs::InvokeYieldOp>(nested.getUnwindBody().front().getTerminator());
    auto before = decodeFailure.getValues().front().getDefiningOp<ctjs::ConstantOp>();
    auto string = before ? llvm::dyn_cast<ctjs::StringAttr>(before.getValue()) : ctjs::StringAttr{};
    check(mlir::succeeded(mlir::verify(*assigned)) && string && string.getValue() == "before" &&
              parseFailure.getValues().front() == outer.getNormalBody().front().getArgument(0),
          "each failure captures its own complete pre-call register state");
    llvm::outs() << "DOM JSON normalization: " << lower
                 << " steps, every incomplete budget preserves " << checks << " checks\n";
}

void testOrdinaryCompletionTypes(mlir::MLIRContext & context) {
    testDOMURITransaction(context);
    testDOMJSONChain(context);
    auto module = import(context, R"js(
function guarded(text) {
    if (text === "skip") { return "prefix"; }
    var mark = "before";
    try { mark = decodeURIComponent(text); }
    catch (ignored) { return mark; }
    return mark;
}
)js",
                         false);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto before = printed(*module);
    const auto prefix = guardedPrefix(function);
    auto result = recoverPrimitiveExceptionRegion(function, 100000,
                                                  ExceptionRecoveryMode::CheckedInvocations);
    if (!check(result.recovered, "original URI call recovers structurally with unused payload")) {
        llvm::errs() << result.refusal << '\n';
        return;
    }
    check(mlir::succeeded(mlir::verify(*module)), "ordinary URI invocation IR verifies");
    check(guardedPrefix(function) == prefix && function->hasAttr("ctjs.not_structured"),
          "original URI guard and early return remain outside the recovered try");
    function.walk([&](ctjs::TryOp attempt) {
        auto payload = attempt.getCatchBody().front().getArgument(0);
        check(llvm::all_of(
                  payload.getUsers(),
                  [](mlir::Operation * operation) { return llvm::isa<ctjs::RootOp>(operation); }),
              "unused URI payload remains represented without inventing a primitive value");
    });
    checkCompletionTypes(*module, function, "!ctnative.boxed", "!ctnative.str<utf8>");
    function.getBody().takeBody(result.original->getBody());
    function->setAttrs((*result.original)->getAttrs());
    check(printed(*module) == before, "URI structural rollback restores the complete source");
    auto effects = recoverPrimitiveExceptionRegion(function, 100000,
                                                   ExceptionRecoveryMode::EffectCheckedInvocations);
    check(!effects.recovered && printed(*module) == before,
          "an unused URI payload supplies no host identity or effect proof");
}

void testSourceCompletionTypes(mlir::MLIRContext & context, llvm::StringRef name,
                               llvm::StringRef source, llvm::StringRef normalType,
                               llvm::StringRef stateType) {
    auto module = import(context, source);
    if (!module) { return; }
    auto function = guarded(*module);
    const auto original = printed(*module);
    auto recovered = recoverPrimitiveExceptionRegion(
        function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
    if (!check(recovered.recovered, "source type witness passes independent effect recovery")) {
        llvm::errs() << recovered.refusal << '\n';
        return;
    }
    checkCompletionTypes(*module, function, normalType, stateType);
    checkCompletionTypes(*module, function, normalType, stateType);
    function.getBody().takeBody(recovered.original->getBody());
    function->setAttrs((*recovered.original)->getAttrs());
    check(printed(*module) == original, "type inference retains exact source rollback");
    auto ordinary = recoverPrimitiveExceptionRegion(function);
    check(!ordinary.recovered && printed(*module) == original,
          "normal type precision does not admit a native throwing source call");
    llvm::outs() << name << ": source normal/state types " << normalType << "/" << stateType
                 << ", unwind remains boxed across frame_enter\n";
}

void testSourceCompletionMutations(mlir::MLIRContext & context, llvm::StringRef source) {
    for (unsigned mutation = 0; mutation != 10; ++mutation) {
        auto module = import(context, source);
        if (!module) { return; }
        auto function = guarded(*module);
        auto recovered = recoverPrimitiveExceptionRegion(
            function, 100000, ExceptionRecoveryMode::EffectCheckedInvocations);
        if (!check(recovered.recovered, "live type control starts with checked source recovery")) {
            return;
        }
        checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
        auto call = firstCall(function);
        auto helper =
            mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, call.getCalleeAttr());
        ctjs::ReturnOp returned;
        ctjs::ThrowOp thrown;
        helper.walk([&](ctjs::ReturnOp operation) { returned = operation; });
        helper.walk([&](ctjs::ThrowOp operation) { thrown = operation; });
        if (!check(returned && thrown, "source type control retains both helper completions")) {
            return;
        }
        mlir::OpBuilder at(returned);
        const auto where = returned.getLoc();
        const auto type = ctjs::ValueType::get(&context);
        helper->setAttr("ctnative.nothrow", at.getUnitAttr());
        helper->setAttr("ctnative.exception_effects", at.getUnitAttr());
        call->setAttr("ctnative.type", at.getStringAttr("num<i32>"));
        llvm::StringRef expected = "!ctnative.boxed";
        llvm::StringRef payload = "!ctnative.boxed";
        llvm::SmallVector<mlir::Operation *> added;
        if (mutation < 3) {
            mlir::Attribute literal =
                mutation == 0   ? mlir::Attribute(ctjs::StringAttr::get(&context, "live"))
                : mutation == 1 ? mlir::Attribute(ctjs::BooleanAttr::get(&context, true))
                                : mlir::Attribute(ctjs::NumberAttr::get(
                                      &context, std::bit_cast<uint64_t>(-0.0)));
            auto replacement = ctjs::ConstantOp::create(at, where, type, literal);
            returned->setOperand(0, replacement);
            expected = mutation == 0   ? "!ctnative.str<utf8>"
                       : mutation == 1 ? "!ctnative.bool"
                                       : "!ctnative.num<f64>";
        } else if (mutation == 3) {
            at.setInsertionPoint(thrown);
            auto replacement = ctjs::ConstantOp::create(at, where, type,
                                                        ctjs::StringAttr::get(&context, "failure"));
            thrown->setOperand(0, replacement);
            expected = "!ctnative.num<i32>";
        } else if (mutation == 4) {
            added.push_back(ctjs::StoreGlobalOp::create(at, where, "late", returned.getValue()));
        } else if (mutation == 5) {
            auto key = ctjs::ConstantOp::create(at, where, type,
                                                ctjs::StringAttr::get(&context, "property"));
            ctjs::GetPropertyOp::create(at, where, type, helper.getBody().front().getArgument(0),
                                        key);
        } else if (mutation == 6) {
            auto arguments = helper.getBody().front().getArguments();
            ctjs::CallDirectOp::create(at, where, type, call.getCalleeAttr(), arguments[0],
                                       arguments[1], arguments[2], arguments.drop_front(3),
                                       mlir::ArrayAttr{}, mlir::ArrayAttr{});
        } else if (mutation == 7) {
            mlir::SymbolTable::setSymbolVisibility(helper, mlir::SymbolTable::Visibility::Public);
        } else if (mutation == 8) {
            unsigned operations = 0;
            helper.getBody().walk([&](mlir::Operation *) { ++operations; });
            if (!check(operations < 4096, "source completion leaves room for exact work limit")) {
                return;
            }
            for (unsigned index = operations; index != 4096; ++index) {
                added.push_back(ctjs::ConstantOp::create(at, where, type,
                                                         ctjs::BooleanAttr::get(&context, true)));
            }
            checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
            added.push_back(
                ctjs::ConstantOp::create(at, where, type, ctjs::BooleanAttr::get(&context, true)));
        } else {
            // A test-only frame-free counterpart establishes that the boxed
            // payload above is a real frame-failure obligation. This erasure
            // is not a source transform or evidence that native may do it.
            llvm::SmallVector<mlir::Operation *> bookkeeping;
            helper.walk([&](mlir::Operation * operation) {
                if (llvm::isa<ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp>(operation)) {
                    bookkeeping.push_back(operation);
                }
            });
            for (auto * operation : llvm::reverse(bookkeeping)) { operation->erase(); }
            expected = "!ctnative.num<i32>";
            payload = "!ctnative.num<i32>";
        }
        checkCompletionTypes(*module, function, expected, "!ctnative.num<i32>", payload);
        checkCompletionTypes(*module, function, expected, "!ctnative.num<i32>", payload);
        if (!added.empty()) {
            for (auto * operation : llvm::reverse(added)) { operation->erase(); }
            checkCompletionTypes(*module, function, "!ctnative.num<i32>", "!ctnative.num<i32>");
        }
    }
    llvm::outs() << "source completion types: ten live mutations, fresh solves and reruns\n";
}

} // namespace ctcompile::test::exception_recovery
