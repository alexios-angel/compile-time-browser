#include "Body.hpp"

#include <ctbrowser/dom/element.hpp>
#include <ctbrowser/style/css/parser.hpp>

namespace ctcompile::ctnative::dom_entry_detail {

std::optional<bool> Body::controlFlow(mlir::Operation & operation, mlir::Block & body,
                                      unsigned depth, mlir::Value & frame, bool & returned) {
    if (auto abrupt = llvm::dyn_cast<mlir::scf::ExecuteRegionOp>(operation)) {
        auto & region = abrupt.getRegion();
        auto thrown = region.hasOneBlock() && llvm::hasSingleElement(region.front())
                          ? llvm::dyn_cast<ctjs::ThrowOp>(region.front().front())
                          : ctjs::ThrowOp{};
        if (!llvm::isa<mlir::scf::IfOp>(body.getParentOp()) || !abrupt.getNoInline() ||
            abrupt->getNumOperands() || abrupt.getNumResults() || !thrown ||
            region.front().getNumArguments()) {
            refusal = "DOM abrupt region requires one exact saved primitive throw";
            return false;
        }
        for (auto * parent = abrupt->getParentOp(); parent != function;
             parent = parent->getParentOp()) {
            if (!spend()) { return false; }
            if (!llvm::isa<mlir::scf::IfOp>(parent)) {
                refusal = "DOM saved throw requires an acyclic unprotected branch";
                return false;
            }
        }
        if (!spend() || !spend()) { return false; }
        if ((!hasKind(thrown.getValue(), Kind::number) &&
             !hasKind(thrown.getValue(), Kind::boolean) &&
             !hasKind(thrown.getValue(), Kind::string) &&
             !hasKind(thrown.getValue(), Kind::optionalString) &&
             !hasKind(thrown.getValue(), Kind::null)) ||
            !dominance.dominates(thrown.getValue(), thrown)) {
            refusal = "DOM saved throw requires a preceding owning primitive";
            return false;
        }
        provedThrows.push_back(thrown);
        nonReturning.insert(&body);
        return true;
    }
    if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
        if (!loop.getBefore().hasOneBlock() || !loop.getAfter().hasOneBlock()) {
            refusal = "DOM loop requires complete scalar regions";
            return false;
        }
        auto & before = loop.getBefore().front();
        auto & after = loop.getAfter().front();
        auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(before.getTerminator());
        auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(after.getTerminator());
        if (!condition || !yield || before.getNumArguments() != loop.getInits().size() ||
            after.getNumArguments() != loop.getNumResults() ||
            yield.getNumOperands() != loop.getInits().size() ||
            condition.getArgs().size() != loop.getNumResults()) {
            refusal = "DOM loop lost scalar state correspondence";
            return false;
        }
        for (auto [argument, initial] : llvm::zip(before.getArguments(), loop.getInits())) {
            if (!spend()) { return false; }
            if (!hasKind(initial, Kind::number) && !hasKind(initial, Kind::string) &&
                !hasKind(initial, Kind::boolean) && !hasKind(initial, Kind::symbol) &&
                !hasKind(initial, Kind::undefinedString) && !hasKind(initial, Kind::element)) {
                refusal = "DOM loop cannot carry borrowed or unknown values";
                return false;
            }
            values[argument] = values[initial];
            auto constant = initial.getDefiningOp<ctjs::ConstantOp>();
            auto zero = constant ? llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue())
                                 : ctjs::NumberAttr{};
            if (!zero || zero.getDouble() != 0) { continue; }
            const auto increments = [](mlir::Value next, mlir::Value previous) {
                auto add = next.getDefiningOp<ctjs::BinaryStaticOp>();
                auto rhs =
                    add ? add.getRhs().getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
                auto one =
                    rhs ? llvm::dyn_cast<ctjs::NumberAttr>(rhs.getValue()) : ctjs::NumberAttr{};
                return add && add.getKind() == ctjs::BinaryKind::Add && add.getLhs() == previous &&
                       one && one.getDouble() == 1;
            };
            const auto next = yield.getOperand(argument.getArgNumber());
            // Ordinary for loops forward the tested index into the after body.
            // Require its exact identity on both the condition and backedge.
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
            // The normalizer returns the condition and its entire
            // continuation tuple together from each selected arm.
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
                auto constant = flag.getDefiningOp<mlir::arith::ConstantOp>();
                auto bit = constant ? llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())
                                    : mlir::IntegerAttr{};
                if (!bit || !bit.getType().isInteger(1)) { return false; }
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
            if (!hasKind(value, Kind::number) && !hasKind(value, Kind::string) &&
                !hasKind(value, Kind::boolean) && !hasKind(value, Kind::symbol) &&
                !hasKind(value, Kind::undefinedString) && !hasKind(value, Kind::element)) {
                refusal = "DOM loop result is not an invariant scalar";
                return false;
            }
            values[argument] = values[result] = values[value];
            if (values[value] == Kind::string) {
                provedStrings.push_back(argument);
                provedStrings.push_back(result);
            }
        }
        if (!visit(after, depth + 1, frame)) { return false; }
        // Supported attribute/class writes cannot reclaim nodes or change a
        // querySelectorAll snapshot. Dataset aliases still need a backedge proof.
        if (mutationEpoch != loopEpoch && !provedDatasetElements.empty()) {
            refusal = "DOM loop mutation needs a backedge dataset-alias proof";
            return false;
        }
        for (auto [value, initial] : llvm::zip(yield.getOperands(), loop.getInits())) {
            if (!spend()) { return false; }
            if (values[value] != values[initial]) {
                refusal = "DOM loop changes its scalar state kind";
                return false;
            }
        }
        return true;
    }
    if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(operation)) {
        if (!llvm::isa<mlir::scf::WhileOp>(body.getParentOp()) ||
            !hasKind(condition.getCondition(), Kind::boolean)) {
            refusal = "DOM loop condition lacks a proved Boolean";
            return false;
        }
        returned = true;
        return true;
    }
    if (auto constant = llvm::dyn_cast<mlir::arith::ConstantOp>(operation)) {
        if (!constant.getType().isInteger(1)) {
            refusal = "DOM completion arithmetic must be a Boolean constant";
            return false;
        }
        values[constant.getResult()] = Kind::boolean;
        return true;
    }
    if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
        if (!hasKind(branch.getCondition(), Kind::boolean) ||
            !llvm::hasSingleElement(branch.getThenRegion()) ||
            (!branch.getElseRegion().empty() && !llvm::hasSingleElement(branch.getElseRegion()))) {
            refusal = "DOM entry branch lacks a proved Boolean and complete arms";
            return false;
        }
        llvm::SmallVector<Kind> joined;
        mlir::Value thenFrame = frame, elseFrame = frame;
        bool thenThrows = false, elseThrows = false, joinedArm = false;
        const auto known = constantBooleans.find(branch.getCondition());
        std::optional<bool> selected =
            known == constantBooleans.end() ? std::nullopt : std::optional(known->second);
        if (!selected && !branch.getNumResults()) {
            // Iterator completion retains a literal done flag. Prove its
            // selected cleanup arm without narrowing ordinary scalar joins.
            if (!spend()) { return false; }
            auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto constant =
                truth ? truth.getValue().getDefiningOp<ctjs::ConstantOp>() : ctjs::ConstantOp{};
            auto boolean = constant ? llvm::dyn_cast<ctjs::BooleanAttr>(constant.getValue())
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
                // Look up by key: recursive visits can rehash both maps.
                values[refined] = previous;
                activeRefinements.erase(refined);
            });
            if (auto predicate = predicates.find(branch.getCondition());
                predicate != predicates.end() &&
                (hasKind(predicate->second.optional, Kind::optionalString) ||
                 hasKind(predicate->second.optional, Kind::undefinedString) ||
                 hasKind(predicate->second.optional, Kind::nullableElement) ||
                 hasKind(predicate->second.optional, Kind::json))) {
                // Reserve save/restore and the new evidence before visiting.
                if (!spend() || !spend() || !spend()) { return false; }
                refined = predicate->second.optional;
                previous = values[refined];
                const bool present = first == predicate->second.stringOnTrue;
                if (present && previous == Kind::nullableElement &&
                    provedDocumentRoots.contains(refined.getDefiningOp<ctjs::GetPropertyOp>())) {
                    // Every admitted effect preserves the document root:
                    // structural mutation and reentry fail the complete proof.
                    documentRootPresent = true;
                }
                // A closest result borrows the original document. Only this
                // arm may dereference it; absence grants no scalar-null facts.
                values[refined] =
                    previous == Kind::nullableElement
                        ? (present ? Kind::element : Kind::nullableElement)
                    : previous == Kind::json ? (present ? Kind::jsonAggregate : Kind::json)
                    : previous == Kind::undefinedString ? (present ? Kind::string : Kind::undefined)
                                                        : (present ? Kind::string : Kind::null);
                if (present &&
                    (previous == Kind::optionalString || previous == Kind::undefinedString)) {
                    activeRefinements[refined] = provedRefinements.size();
                    provedRefinements.push_back({&region.front(), refined, {}});
                }
            }
            auto & armFrame = first ? thenFrame : elseFrame;
            if (!visit(region.front(), depth + 1, armFrame)) { return false; }
            const bool throws = nonReturning.contains(&region.front());
            (first ? thenThrows : elseThrows) = throws;
            auto yielded = llvm::dyn_cast<mlir::scf::YieldOp>(region.front().getTerminator());
            if (!yielded || yielded.getNumOperands() != branch.getNumResults()) {
                refusal = "DOM entry branch requires exact scalar yields";
                return false;
            }
            for (auto [index, operand] : llvm::enumerate(yielded.getOperands())) {
                if (!spend()) { return false; }
                const auto found = values.find(operand);
                if (found == values.end()) {
                    refusal = "DOM entry yield lacks a proved value";
                    return false;
                }
                const Kind kind = found->second;
                if (kind != Kind::boolean && kind != Kind::number && kind != Kind::string &&
                    kind != Kind::null && kind != Kind::optionalString && kind != Kind::undefined &&
                    kind != Kind::json && kind != Kind::jsonAggregate && kind != Kind::symbol &&
                    kind != Kind::undefinedString && kind != Kind::element) {
                    refusal = "DOM entry branch cannot carry a borrowed or callable value";
                    return false;
                }
                // Both arms retain the complete typed effect proof, even
                // when an original argument decides the default branch.
                if (throws || (selected && first != *selected)) { continue; }
                if (!joinedArm) {
                    joined.push_back(kind);
                    continue;
                }
                if (joined[index] == kind) { continue; }
                const auto json = [](Kind k) {
                    return k == Kind::json || k == Kind::jsonAggregate;
                };
                const auto jsonScalar = [](Kind k) {
                    return k == Kind::string || k == Kind::boolean || k == Kind::number ||
                           k == Kind::null || k == Kind::optionalString;
                };
                // Primitive arms become owning JSON alternatives. Undefined
                // and borrowed browser values have no JSON representation.
                if ((json(joined[index]) && (json(kind) || jsonScalar(kind))) ||
                    (jsonScalar(joined[index]) && json(kind))) {
                    joined[index] = Kind::json;
                    continue;
                }
                const auto stringOrNull = [](Kind k) {
                    return k == Kind::string || k == Kind::null || k == Kind::optionalString;
                };
                const auto stringOrUndefined = [](Kind k) {
                    return k == Kind::string || k == Kind::undefined || k == Kind::undefinedString;
                };
                if (stringOrUndefined(joined[index]) && stringOrUndefined(kind)) {
                    joined[index] = Kind::undefinedString;
                    continue;
                }
                if (!stringOrNull(joined[index]) || !stringOrNull(kind)) {
                    refusal = "DOM entry branch has incompatible scalar alternatives";
                    return false;
                }
                joined[index] = Kind::optionalString;
            }
            if (!throws && (!selected || first == *selected)) { joinedArm = true; }
        }
        if (!thenThrows && !elseThrows && thenFrame != elseFrame) {
            refusal = "DOM entry branch has inconsistent shadow frame exits";
            return false;
        }
        frame = thenThrows ? elseFrame : thenFrame;
        if (selected ? (*selected ? thenThrows : elseThrows) : (thenThrows && elseThrows)) {
            nonReturning.insert(&body);
        }
        if (thenThrows && elseThrows) {
            // Neither arm reaches its yield. Give only this structural padding
            // a common primitive kind; both payloads and effects proved above.
            for (unsigned i = 0; i < branch.getNumResults(); ++i) {
                if (!spend()) { return false; }
                joined.push_back(Kind::boolean);
            }
        }
        if (joined.size() != branch.getNumResults() ||
            (branch.getNumResults() && branch.getElseRegion().empty())) {
            refusal = "DOM entry branch is missing a scalar arm";
            return false;
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
                    refusal = "DOM throw join requires a primitive normal result";
                    return false;
                }
            }
            provedThrowYields.push_back(std::move(padding));
        }
        for (auto [value, kind] : llvm::zip(branch.getResults(), joined)) {
            values[value] = kind;
            if (kind == Kind::optionalString || kind == Kind::null) {
                provedOptionalStrings.push_back(value);
            } else if (kind == Kind::string) {
                provedStrings.push_back(value);
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
            // Only the exact unused-result wrapper can disappear. Complete
            // DOM proof below excludes receiver failure, coercion and reentry;
            // a valid literal name/selector excludes the remaining source exception.
            if (invocation->getParentOfType<ctjs::InvokeOp>() ||
                !invocation.getBody().hasOneBlock() || !invocation.getNormalBody().hasOneBlock() ||
                !invocation.getUnwindBody().hasOneBlock()) {
                refusal = "DOM suppressed attribute requires complete unnested continuations";
                return false;
            }
            auto & called = invocation.getBody().front();
            auto call =
                called.empty() ? ctjs::CallOp{} : llvm::dyn_cast<ctjs::CallOp>(called.front());
            auto exit = called.empty() ? ctjs::InvokeExitOp{}
                                       : llvm::dyn_cast<ctjs::InvokeExitOp>(called.back());
            if (called.getNumArguments() || !call || !exit || call->getNextNode() != exit ||
                exit.getNormalResult() != call.getResult() || !call.getResult().hasOneUse() ||
                !exit.getState().empty() || !hasKind(call.getReceiver(), Kind::element)) {
                refusal = "DOM suppression requires one typed unused Element call";
                return false;
            }
            const bool matches = hasKind(call.getCallee(), Kind::matches);
            if ((!matches && !hasKind(call.getCallee(), Kind::attribute)) ||
                call.getArgs().size() != (matches ? 1u : 2u) ||
                !hasKind(call.getArgs()[0], Kind::string) ||
                (!matches && !hasKind(call.getArgs()[1], Kind::string) &&
                 !hasKind(call.getArgs()[1], Kind::boolean))) {
                refusal = "DOM suppression requires typed setAttribute or matches arguments";
                return false;
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
                    refusal = "DOM suppressed attribute requires empty unobserved continuations";
                    return false;
                }
            }
            auto literal = call.getArgs()[0].getDefiningOp<ctjs::ConstantOp>();
            auto name =
                literal ? llvm::dyn_cast<ctjs::StringAttr>(literal.getValue()) : ctjs::StringAttr{};
            if (!name) {
                refusal = "DOM suppression requires a valid literal name or selector";
                return false;
            }
            // Reserve the public validator's byte scan before invoking it.
            for (std::size_t i = 0; i < name.getValue().size(); ++i) {
                if (!spend()) { return false; }
            }
            const std::string_view text{name.getValue().data(), name.getValue().size()};
            if (matches) {
                ctbrowser::atom_table atoms;
                bool invalid = false;
                (void)ctbrowser::style::css::parse_selector_text(text, atoms, invalid);
                if (invalid) {
                    refusal = "DOM suppressed matches requires a valid literal selector";
                    return false;
                }
            } else if (!ctbrowser::is_valid_attribute_name(text)) {
                refusal = "DOM suppressed attribute requires a valid literal name";
                return false;
            }
            if (!visit(called, depth + 1, frame)) { return false; }
            provedInvocations.push_back(invocation);
            return true;
        }
        // A nested invoke may only continue the enclosing success.
        auto parent = invocation->getParentOfType<ctjs::InvokeOp>();
        if ((parent && (!llvm::is_contained(provedInvocations, parent) ||
                        invocation->getParentRegion() != &parent.getNormalBody())) ||
            invocation.getNumResults() != 1 || !invocation.getBody().hasOneBlock() ||
            !invocation.getNormalBody().hasOneBlock() ||
            !invocation.getUnwindBody().hasOneBlock()) {
            refusal = "DOM URI invocation requires complete unnested continuations";
            return false;
        }
        auto & called = invocation.getBody().front();
        auto & normal = invocation.getNormalBody().front();
        auto & caught = invocation.getUnwindBody().front();
        auto call = called.empty() ? ctjs::CallOp{} : llvm::dyn_cast<ctjs::CallOp>(called.front());
        auto exit = called.empty() ? ctjs::InvokeExitOp{}
                                   : llvm::dyn_cast<ctjs::InvokeExitOp>(called.back());
        if (called.getNumArguments() || !call || !exit || call->getNextNode() != exit ||
            exit.getNormalResult() != call.getResult() || !call.getResult().hasOneUse() ||
            !exit.getState().empty() || normal.getNumArguments() != 1 ||
            caught.getNumArguments() != 1 ||
            !llvm::isa<ctjs::ValueType>(normal.getArgument(0).getType()) ||
            !llvm::isa<ctjs::ValueType>(caught.getArgument(0).getType()) ||
            !llvm::isa<ctjs::ValueType>(invocation.getResult(0).getType()) ||
            !llvm::isa<ctjs::ValueType>(call.getResult().getType()) ||
            !caught.getArgument(0).use_empty() ||
            (!hasKind(call.getCallee(), Kind::uriIntrinsic) &&
             !hasKind(call.getCallee(), Kind::jsonParse))) {
            refusal = "DOM URI invocation requires exact call and unused catch payload";
            return false;
        }
        const bool parses = hasKind(call.getCallee(), Kind::jsonParse);
        // Reserve before nested invokes look this parent up.
        provedInvocations.push_back(invocation);
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
                refusal = "DOM URI continuations must both return owning Strings";
                return false;
            }
            if (tree) { result = Kind::json; }
        }
        values[invocation.getResult(0)] = result;
        if (result == Kind::string) { provedStrings.push_back(invocation.getResult(0)); }
        return true;
    }
    if (llvm::isa<ctjs::InvokeExitOp, ctjs::InvokeYieldOp>(operation)) {
        auto invocation = llvm::dyn_cast<ctjs::InvokeOp>(body.getParentOp());
        if (!invocation || &operation != body.getTerminator() ||
            (llvm::isa<ctjs::InvokeExitOp>(operation) !=
             (&body == &invocation.getBody().front()))) {
            refusal = "DOM URI completion is outside its original continuation";
            return false;
        }
        returned = true;
        return true;
    }
    if (llvm::isa<mlir::scf::YieldOp>(operation)) {
        if (!depth) {
            refusal = "DOM entry yield is outside a branch";
            return false;
        }
        returned = true;
        return true;
    }
    return std::nullopt;
}

} // namespace ctcompile::ctnative::dom_entry_detail
