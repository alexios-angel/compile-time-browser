//===- ControlFlow.cpp - DOM entry control flow ---------------------------===//
//
// Body::controlFlow: loops, branches, saved throws and try/catch invokes in a DOM
// entry body, plus the kind join for values that leave a branch.
//
//===----------------------------------------------------------------------===//

#include "Body.hpp"

#include "mlir/IR/Matchers.h"

#include <ctbrowser/dom/element.hpp>
#include <ctbrowser/style/css/parser.hpp>

bool ctcompile::ctnative::validSelector(llvm::StringRef text) {
    ctbrowser::atom_table atoms;
    bool invalid = false;
    (void)ctbrowser::style::css::parse_selector_text({text.data(), text.size()}, atoms, invalid);
    return !invalid;
}

namespace ctcompile::ctnative::dom_entry_detail {

namespace {
/// Scalar kinds an scf.if arm may yield.
constexpr std::array yieldKinds{Kind::boolean,        Kind::optionalBoolean, Kind::number,
                                Kind::optionalNumber, Kind::string,          Kind::null,
                                Kind::optionalString, Kind::undefined,       Kind::json,
                                Kind::jsonAggregate,  Kind::symbol,          Kind::undefinedString,
                                Kind::element,        Kind::mapPayload};

/// The kind two arms' yields join to, or std::nullopt when they are
/// incompatible.
std::optional<Kind> joinKind(Kind a, Kind b) {
    if (a == b) { return a; }
    const auto both = [&](std::initializer_list<Kind> kinds) {
        return llvm::is_contained(kinds, a) && llvm::is_contained(kinds, b);
    };
    // A Map read joined with another primitive is still a primitive or
    // undefined; native inference picks its carrier.
    if ((a == Kind::mapPayload || b == Kind::mapPayload) &&
        both({Kind::mapPayload, Kind::boolean, Kind::number, Kind::string, Kind::null,
              Kind::undefined})) {
        return Kind::mapPayload;
    }
    if (both({Kind::boolean, Kind::null, Kind::optionalBoolean})) { return Kind::optionalBoolean; }
    if (both({Kind::number, Kind::null, Kind::optionalNumber})) { return Kind::optionalNumber; }
    const auto json = [](Kind k) { return k == Kind::json || k == Kind::jsonAggregate; };
    const auto jsonScalar = [](Kind k) {
        return k == Kind::string || k == Kind::boolean || k == Kind::number || k == Kind::null ||
               k == Kind::optionalString;
    };
    // A JSON value joined with a primitive becomes an owned JSON value.
    // Undefined and borrowed browser values have no JSON representation.
    if ((json(a) && (json(b) || jsonScalar(b))) || (jsonScalar(a) && json(b))) {
        return Kind::json;
    }
    if (both({Kind::string, Kind::undefined, Kind::undefinedString})) {
        return Kind::undefinedString;
    }
    if (both({Kind::string, Kind::null, Kind::optionalString})) { return Kind::optionalString; }
    return std::nullopt;
}
} // namespace

std::optional<bool> Body::controlFlow(mlir::Operation & operation, mlir::Block & body,
                                      unsigned depth, mlir::Value & frame, bool & returned) {
    if (auto abrupt = llvm::dyn_cast<mlir::scf::ExecuteRegionOp>(operation)) {
        auto thrown = savedThrow(abrupt);
        if (!llvm::isa<mlir::scf::IfOp>(body.getParentOp()) || !thrown) {
            return fail("DOM abrupt region requires one exact saved primitive throw");
        }
        if (!onlyBranchAncestors(abrupt,
                                 "DOM saved throw requires an acyclic unprotected branch")) {
            return false;
        }
        if (!spend() || !spend()) { return false; }
        if (hasKind(thrown.getValue(), {Kind::element, Kind::nullableElement})) {
            // The host keeps nodes alive only for this invocation. An escaping
            // exception could outlive the document while the stack unwinds.
            return fail("DOM thrown element requires an owner that outlives the exception");
        }
        if (!hasKind(thrown.getValue(), {Kind::number, Kind::boolean, Kind::string,
                                         Kind::optionalString, Kind::null}) ||
            !dominance.dominates(thrown.getValue(), thrown)) {
            return fail("DOM saved throw requires a preceding owning primitive");
        }
        proved.throws.push_back(thrown);
        nonReturning.insert(&body);
        return true;
    }
    if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
        if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock()) {
            return fail("DOM loop requires complete scalar regions");
        }
        auto & before = loop.getBefore().front();
        auto & after = loop.getAfter().front();
        auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(before.getTerminator());
        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(after.getTerminator());
        if (!condition || !yield || before.getNumArguments() != loop.getInits().size() ||
            after.getNumArguments() != loop.getNumResults() ||
            yield.getNumOperands() != loop.getInits().size() ||
            condition.getArgs().size() != loop.getNumResults()) {
            return fail("DOM loop lost scalar state correspondence");
        }
        for (auto [argument, initial] : llvm::zip(before.getArguments(), loop.getInits())) {
            if (!spend()) { return false; }
            if (!hasKind(initial, loopKinds)) {
                return fail("DOM loop cannot carry borrowed or unknown values");
            }
            values[argument] = values[initial];
            auto zero = ctjs::constantAttr<ctjs::NumberAttr>(initial);
            if (!zero || zero.getDouble() != 0) { continue; }
            const auto increments = [](mlir::Value next, mlir::Value previous) {
                auto add = next.getDefiningOp<ctjs::BinaryStaticOp>();
                auto one =
                    add ? ctjs::constantAttr<ctjs::NumberAttr>(add.getRhs()) : ctjs::NumberAttr{};
                return add && add.getKind() == ctjs::BinaryKind::Add && add.getLhs() == previous &&
                       one && one.getDouble() == 1;
            };
            const auto next = yield.getOperand(argument.getArgNumber());
            // An ordinary for loop forwards the tested index into the after
            // body. The same value must reach both the condition and the
            // backedge for the index to count as increasing.
            if (auto add = next.getDefiningOp<ctjs::BinaryStaticOp>()) {
                if (!spend()) { return false; }
                auto index = llvm::dyn_cast<mlir::BlockArgument>(add.getLhs());
                if (index && index.getOwner() == &after &&
                    condition.getArgs()[index.getArgNumber()] == argument &&
                    increments(next, index)) {
                    increasingIndices.insert(index);
                }
            }
            auto forwarded = llvm::dyn_cast<mlir::BlockArgument>(next);
            if (!forwarded || forwarded.getOwner() != &after) { continue; }
            const unsigned slot = forwarded.getArgNumber();
            // The normalizer returns the condition and its whole continuation
            // tuple together from each selected arm. The recursion through
            // nested scf.if conditions is capped at 64 levels.
            const auto advances = [&](auto && check, mlir::Value flag, mlir::Value next,
                                      unsigned level) -> bool {
                if (!spend() || level == 64) { return false; }
                if (auto branch = flag.getDefiningOp<mlir::scf::IfOp>()) {
                    auto flagResult = llvm::cast<mlir::OpResult>(flag);
                    for (mlir::Region & region : branch->getRegions()) {
                        if (!spend() || !region.hasOneBlock()) { return false; }
                        auto exit =
                            llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
                        if (!exit) { return false; }
                        auto nextResult = llvm::dyn_cast<mlir::OpResult>(next);
                        const auto value = nextResult && nextResult.getOwner() == branch
                                               ? exit.getOperand(nextResult.getResultNumber())
                                               : next;
                        if (!check(check, exit.getOperand(flagResult.getResultNumber()), value,
                                   level + 1)) {
                            return false;
                        }
                    }
                    return true;
                }
                mlir::IntegerAttr bit;
                if (!mlir::matchPattern(flag, mlir::m_Constant(&bit)) ||
                    !bit.getType().isInteger(1)) {
                    return false;
                }
                if (!bit.getInt()) { return true; }
                return increments(next, argument);
            };
            if (advances(advances, condition.getCondition(), condition.getArgs()[slot], 0)) {
                increasingIndices.insert(argument);
            }
            if (budgetExhausted) { return false; }
        }
        const unsigned loopEpoch = mutationEpoch;
        if (!visit(before, depth + 1, frame)) { return false; }
        for (auto [argument, result, value] :
             llvm::zip(after.getArguments(), loop.getResults(), condition.getArgs())) {
            if (!spend()) { return false; }
            if (!hasKind(value, loopKinds)) {
                return fail("DOM loop result is not an invariant scalar");
            }
            values[argument] = values[result] = values[value];
            if (values[value] == Kind::string) {
                proved.strings.push_back(argument);
                proved.strings.push_back(result);
            }
        }
        if (!visit(after, depth + 1, frame)) { return false; }
        // Supported attribute and class writes cannot free nodes or change a
        // querySelectorAll snapshot, but they can change a dataset, and
        // dataset aliases across the backedge are not proved, so refuse.
        if (mutationEpoch != loopEpoch && !proved.datasetElements.empty()) {
            return fail("DOM loop mutation needs a backedge dataset-alias proof");
        }
        for (auto [value, initial] : llvm::zip(yield.getOperands(), loop.getInits())) {
            if (!spend()) { return false; }
            if (values[value] != values[initial]) {
                return fail("DOM loop changes its scalar state kind");
            }
        }
        return true;
    }
    if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
        if (!llvm::isa<mlir::scf::WhileOp>(body.getParentOp()) ||
            !hasKind(condition.getCondition(), Kind::boolean)) {
            return fail("DOM loop condition lacks a proved Boolean");
        }
        returned = true;
        return true;
    }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
        if (!constant.getType().isInteger(1)) {
            return fail("DOM completion arithmetic must be a Boolean constant");
        }
        values[constant.getResult()] = Kind::boolean;
        return true;
    }
    if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
        if (!hasKind(branch.getCondition(), Kind::boolean) ||
            !llvm::hasSingleElement(branch.getThenRegion()) ||
            (!branch.getElseRegion().empty() && !llvm::hasSingleElement(branch.getElseRegion()))) {
            return fail("DOM entry branch lacks a proved Boolean and complete arms");
        }
        llvm::SmallVector<Kind> joined;
        mlir::Value thenFrame = frame, elseFrame = frame;
        bool thenThrows = false, elseThrows = false, joinedArm = false;
        const auto known = constantBooleans.find(branch.getCondition());
        std::optional<bool> selected =
            known == constantBooleans.end() ? std::nullopt : std::optional(known->second);
        if (!selected && !branch.getNumResults()) {
            // Iterator completion keeps a literal `done` flag. Treat only the
            // arm it selects as reachable, without narrowing ordinary scalar
            // joins.
            if (!spend()) { return false; }
            auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto boolean = truth ? ctjs::constantAttr<ctjs::BooleanAttr>(truth.getValue())
                                 : ctjs::BooleanAttr{};
            if (boolean) { selected = boolean.getValue(); }
        }
        for (mlir::Region & region : branch->getRegions()) {
            if (region.empty()) { continue; }
            if (!spend()) { return false; }
            const bool first = &region == &branch.getThenRegion();
            mlir::Value refined;
            Kind previous = Kind::implicit;
            const bool previousRootPresent = documentRootPresent;
            const auto restore = llvm::make_scope_exit([&] {
                documentRootPresent = previousRootPresent;
                if (!refined) { return; }
                // Look up by key, not a saved iterator: recursive visits can
                // rehash both maps.
                values[refined] = previous;
                activeRefinements.erase(refined);
            });
            if (auto predicate = predicates.find(branch.getCondition());
                predicate != predicates.end() &&
                hasKind(predicate->second.optional, {Kind::optionalString, Kind::undefinedString,
                                                     Kind::nullableElement, Kind::json})) {
                // Charge for the save, the restore and the new evidence
                // before visiting.
                if (!spend() || !spend() || !spend()) { return false; }
                refined = predicate->second.optional;
                previous = values[refined];
                const bool present = first == predicate->second.stringOnTrue;
                if (present && previous == Kind::nullableElement &&
                    proved.documentRoots.contains(refined.getDefiningOp<ctjs::GetPropertyOp>())) {
                    // No accepted operation can remove the document root:
                    // tree mutation and script reentry are refused.
                    documentRootPresent = true;
                }
                // A closest() result borrows from the original document. Only
                // the arm where it is present may dereference it; the absent
                // arm learns nothing beyond "not an element".
                values[refined] =
                    previous == Kind::nullableElement
                        ? (present ? Kind::element : Kind::nullableElement)
                    : previous == Kind::json ? (present ? Kind::jsonAggregate : Kind::json)
                    : previous == Kind::undefinedString ? (present ? Kind::string : Kind::undefined)
                                                        : (present ? Kind::string : Kind::null);
                if (present &&
                    (previous == Kind::optionalString || previous == Kind::undefinedString)) {
                    activeRefinements[refined] = proved.refinements.size();
                    proved.refinements.push_back({&region.front(), refined, {}});
                }
            }
            auto & armFrame = first ? thenFrame : elseFrame;
            if (!visit(region.front(), depth + 1, armFrame)) { return false; }
            const bool throws = nonReturning.contains(&region.front());
            (first ? thenThrows : elseThrows) = throws;
            auto yielded = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
            if (!yielded || yielded.getNumOperands() != branch.getNumResults()) {
                return fail("DOM entry branch requires exact scalar yields");
            }
            for (auto [index, operand] : llvm::enumerate(yielded.getOperands())) {
                if (!spend()) { return false; }
                const auto found = values.find(operand);
                if (found == values.end()) { return fail("DOM entry yield lacks a proved value"); }
                const Kind kind = found->second;
                if (!llvm::is_contained(yieldKinds, kind)) {
                    return fail("DOM entry branch cannot carry a borrowed or callable value");
                }
                // Both arms were fully checked above, even when a constant
                // condition selects one; only reachable, non-throwing arms
                // contribute to the joined result kind.
                if (throws || (selected && first != *selected)) { continue; }
                if (!joinedArm) {
                    joined.push_back(kind);
                    continue;
                }
                const auto join = joinKind(joined[index], kind);
                if (!join) { return fail("DOM entry branch has incompatible scalar alternatives"); }
                joined[index] = *join;
            }
            if (!throws && (!selected || first == *selected)) { joinedArm = true; }
        }
        if (!thenThrows && !elseThrows && thenFrame != elseFrame) {
            return fail("DOM entry branch has inconsistent shadow frame exits");
        }
        frame = thenThrows ? elseFrame : thenFrame;
        if (selected ? (*selected ? thenThrows : elseThrows) : (thenThrows && elseThrows)) {
            nonReturning.insert(&body);
        }
        if (thenThrows && elseThrows) {
            // Neither arm reaches its yield. Give the unreachable results a
            // placeholder primitive kind; both arms' payloads and effects were
            // proved above.
            for (unsigned i = 0; i < branch.getNumResults(); ++i) {
                if (!spend()) { return false; }
                joined.push_back(Kind::boolean);
            }
        }
        if (joined.size() != branch.getNumResults() ||
            (branch.getNumResults() && branch.getElseRegion().empty())) {
            return fail("DOM entry branch is missing a scalar arm");
        }
        for (auto & region : branch->getRegions()) {
            if (region.empty() || !nonReturning.contains(&region.front())) { continue; }
            if (!spend()) { return false; }
            HostDOMUnreachableYield padding{region.front().getTerminator(), {}};
            for (Kind kind : joined) {
                if (!spend()) { return false; }
                auto * context = branch.getContext();
                if (kind == Kind::boolean) {
                    padding.values.push_back(ctjs::BooleanAttr::get(context, false));
                } else if (kind == Kind::number) {
                    padding.values.push_back(ctjs::NumberAttr::get(context, 0));
                } else if (kind == Kind::string) {
                    padding.values.push_back(ctjs::StringAttr::get(context, ""));
                } else {
                    return fail("DOM throw join requires a primitive normal result");
                }
            }
            proved.throwYields.push_back(std::move(padding));
        }
        for (auto [value, kind] : llvm::zip(branch.getResults(), joined)) {
            values[value] = kind;
            if (kind == Kind::optionalString || kind == Kind::null) {
                proved.optionalStrings.push_back(value);
            } else if (kind == Kind::string) {
                proved.strings.push_back(value);
            }
        }
        if (callbackBody && branch.getNumResults()) {
            auto yes =
                llvm::cast<mlir::scf::YieldOp>(branch.getThenRegion().front().getTerminator());
            auto no =
                llvm::cast<mlir::scf::YieldOp>(branch.getElseRegion().front().getTerminator());
            for (auto [result, first, second] :
                 llvm::zip(branch.getResults(), yes.getOperands(), no.getOperands())) {
                if (!spend() || !spend() || !spend()) { return false; }
                if (prefixRequired.contains(second) &&
                    (prefixRequired.contains(branch.getCondition()) ||
                     prefixRequired.contains(first))) {
                    prefixRequired.insert(result);
                }
            }
        }
        return true;
    }
    if (auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(operation)) {
        if (invocation.getNumResults() == 0) {
            // A try/catch around an unused setAttribute or matches call with
            // empty continuations can be dropped. The DOM proof below rules out
            // a bad receiver, coercion and reentry, and a valid literal name or
            // selector rules out the only exception left.
            if (invocation->getParentOfType<ctjs::InvokeOp>() ||
                !invocation.getBody().hasOneBlock() || !invocation.getNormalBody().hasOneBlock() ||
                !invocation.getUnwindBody().hasOneBlock()) {
                return fail("DOM suppressed attribute requires complete unnested continuations");
            }
            auto & called = invocation.getBody().front();
            auto call = invokedCall(invocation);
            if (!call || !llvm::cast<ctjs::InvokeExitOp>(called.back()).getState().empty() ||
                !hasKind(call.getReceiver(), Kind::element)) {
                return fail("DOM suppression requires one typed unused Element call");
            }
            const bool matches = hasKind(call.getCallee(), Kind::matches);
            if ((!matches && !hasKind(call.getCallee(), Kind::attribute)) ||
                call.getArgs().size() != (matches ? 1u : 2u) ||
                !hasKind(call.getArgs()[0], Kind::string) ||
                (!matches &&
                 !hasKind(call.getArgs()[1], {Kind::string, Kind::boolean, Kind::number}))) {
                return fail("DOM suppression requires typed setAttribute or matches arguments");
            }
            for (mlir::Region * region :
                 {&invocation.getNormalBody(), &invocation.getUnwindBody()}) {
                if (!spend()) { return false; }
                auto & continuation = region->front();
                auto yield = continuation.empty()
                                 ? ctjs::InvokeYieldOp{}
                                 : llvm::dyn_cast<ctjs::InvokeYieldOp>(continuation.front());
                if (continuation.getNumArguments() != 1 ||
                    !llvm::isa<ctjs::ValueType>(continuation.getArgument(0).getType()) ||
                    !continuation.getArgument(0).use_empty() || !yield ||
                    !yield.getValues().empty() || yield->getNextNode()) {
                    return fail("DOM suppressed attribute requires empty unobserved continuations");
                }
            }
            auto name = ctjs::constantAttr<ctjs::StringAttr>(call.getArgs()[0]);
            if (!name) { return fail("DOM suppression requires a valid literal name or selector"); }
            // Charge one step per byte the validator will scan.
            for (std::size_t i = 0; i < name.getValue().size(); ++i) {
                if (!spend()) { return false; }
            }
            if (matches && !validSelector(name.getValue())) {
                return fail("DOM suppressed matches requires a valid literal selector");
            }
            if (!matches && !ctbrowser::is_valid_attribute_name(
                                {name.getValue().data(), name.getValue().size()})) {
                return fail("DOM suppressed attribute requires a valid literal name");
            }
            if (!visit(called, depth + 1, frame)) { return false; }
            proved.invocations.push_back(invocation);
            return true;
        }
        // A nested invoke may only sit in the enclosing invoke's normal
        // (success) continuation.
        auto parent = invocation->getParentOfType<ctjs::InvokeOp>();
        if ((parent && (!llvm::is_contained(proved.invocations, parent) ||
                        invocation->getParentRegion() != &parent.getNormalBody())) ||
            invocation.getNumResults() != 1 || !invocation.getBody().hasOneBlock() ||
            !invocation.getNormalBody().hasOneBlock() ||
            !invocation.getUnwindBody().hasOneBlock()) {
            return fail("DOM URI invocation requires complete unnested continuations");
        }
        auto & called = invocation.getBody().front();
        auto & normal = invocation.getNormalBody().front();
        auto & caught = invocation.getUnwindBody().front();
        auto call = invokedCall(invocation);
        if (!call || !llvm::cast<ctjs::InvokeExitOp>(called.back()).getState().empty() ||
            normal.getNumArguments() != 1 || caught.getNumArguments() != 1 ||
            !llvm::isa<ctjs::ValueType>(normal.getArgument(0).getType()) ||
            !llvm::isa<ctjs::ValueType>(caught.getArgument(0).getType()) ||
            !llvm::isa<ctjs::ValueType>(invocation.getResult(0).getType()) ||
            !llvm::isa<ctjs::ValueType>(call.getResult().getType()) ||
            !caught.getArgument(0).use_empty() ||
            !hasKind(call.getCallee(), {Kind::uriIntrinsic, Kind::jsonParse})) {
            return fail("DOM URI invocation requires exact call and unused catch payload");
        }
        const bool parses = hasKind(call.getCallee(), Kind::jsonParse);
        // Record this invoke before visiting, so nested invokes find it as
        // their proved parent.
        proved.invocations.push_back(invocation);
        if (!visit(called, depth + 1, frame)) { return false; }
        values[normal.getArgument(0)] = parses ? Kind::json : Kind::string;
        Kind result = Kind::string;
        for (mlir::Block * continuation : {&normal, &caught}) {
            if (!visit(*continuation, depth + 1, frame)) { return false; }
            auto yielded = llvm::dyn_cast<ctjs::InvokeYieldOp>(continuation->getTerminator());
            const bool string = yielded && yielded.getValues().size() == 1 &&
                                hasKind(yielded.getValues().front(), Kind::string);
            const bool tree = yielded && yielded.getValues().size() == 1 &&
                              hasKind(yielded.getValues().front(), Kind::json);
            if (!string && !tree) {
                return fail("DOM URI continuations must both return owning Strings");
            }
            if (tree) { result = Kind::json; }
        }
        values[invocation.getResult(0)] = result;
        if (result == Kind::string) { proved.strings.push_back(invocation.getResult(0)); }
        return true;
    }
    if (llvm::isa<ctjs::InvokeExitOp, ctjs::InvokeYieldOp>(operation)) {
        auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(body.getParentOp());
        if (!invocation || &operation != body.getTerminator() ||
            (llvm::isa<ctjs::InvokeExitOp>(operation) !=
             (&body == &invocation.getBody().front()))) {
            return fail("DOM URI completion is outside its original continuation");
        }
        returned = true;
        return true;
    }
    if (llvm::isa<mlir::scf::YieldOp>(operation)) {
        if (!depth) { return fail("DOM entry yield is outside a branch"); }
        returned = true;
        return true;
    }
    return std::nullopt;
}

} // namespace ctcompile::ctnative::dom_entry_detail
