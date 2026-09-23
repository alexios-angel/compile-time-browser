#include "Body.hpp"

namespace ctcompile::ctnative::dom_entry_detail {

bool Body::visit(mlir::Block & body, unsigned depth, mlir::Value & frame) {
    if (depth == 64 || (!body.getArguments().empty() && depth != 0 &&
                        !llvm::isa<ctjs::InvokeOp, mlir::scf::WhileOp>(body.getParentOp()))) {
        refusal = "DOM entry branch depth or block arguments are unsupported";
        return false;
    }
    bool entered = false, returned = false, structuralTail = false;
    for (mlir::Operation & operation : body) {
        if (!spend()) { return false; }
        // Pure callbacks observe only their String argument. No implicit
        // receiver, allocation, global, capture, callback or browser effect.
        if (callbackBody && !(replacementBody && llvm::isa<ctjs::BinaryOp>(operation)) &&
            !llvm::isa<ctjs::ConstantOp, ctjs::FrameEnterOp, ctjs::FrameExitOp, ctjs::RootOp,
                       ctjs::ReturnOp, ctjs::GetPropertyOp, ctjs::CallOp, ctjs::TruthyOp,
                       ctjs::UnaryOp, mlir::scf::IfOp, mlir::scf::YieldOp>(operation)) {
            refusal = "DOM filter callback contains an unsupported source effect";
            return false;
        }
        if ((operation.getNumRegions() != 0 &&
             !llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ExecuteRegionOp,
                        ctjs::InvokeOp>(operation)) ||
            operation.getNumSuccessors() != 0 || returned ||
            (nonReturning.contains(&body) && !llvm::isa<mlir::scf::YieldOp>(operation) &&
             !(structuralTail &&
               llvm::isa<ctjs::ConstantOp, ctjs::FrameExitOp, ctjs::GetPropertyOp, ctjs::CallOp>(
                   operation)))) {
            refusal = "DOM entry does not admit nested control flow or a source continuation";
            return false;
        }
        // A fully proved throwing branch makes its suffix unreachable. Retain
        // and check each remaining DOM read/call with the ordinary proof below;
        // the emitted throw still precedes it. Direct abrupt regions stay strict.
        for (mlir::OpOperand & use : operation.getOpOperands()) {
            const mlir::Value operand = use.get();
            if (!spend()) { return false; }
            if ((!values.contains(operand) && operand != frame) ||
                !dominance.dominates(operand, &operation)) {
                refusal = "DOM entry operand has no preceding local definition";
                return false;
            }
            if (auto found = activeRefinements.find(operand);
                found != activeRefinements.end() && !llvm::isa<ctjs::RootOp>(operation)) {
                if (!spend()) { return false; }
                provedRefinements[found->second].uses.push_back(
                    {&operation, use.getOperandNumber()});
            }
        }
        if (const auto handled = controlFlow(operation, body, depth, frame, returned)) {
            if (!*handled) { return false; }
            structuralTail |= llvm::isa<mlir::scf::IfOp>(operation) && nonReturning.contains(&body);
            continue;
        }
        if (auto constant = llvm::dyn_cast<ctjs::ConstantOp>(operation)) {
            if (llvm::isa<ctjs::StringAttr>(constant.getValue())) {
                values[constant.getResult()] = Kind::string;
            } else if (llvm::isa<ctjs::BooleanAttr>(constant.getValue())) {
                values[constant.getResult()] = Kind::boolean;
                if (callbackBody &&
                    !llvm::cast<ctjs::BooleanAttr>(constant.getValue()).getValue()) {
                    prefixRequired.insert(constant.getResult());
                }
            } else if (llvm::isa<ctjs::NumberAttr>(constant.getValue())) {
                values[constant.getResult()] = Kind::number;
            } else if (llvm::isa<ctjs::NullAttr>(constant.getValue())) {
                values[constant.getResult()] = Kind::null;
            } else if (llvm::isa<ctjs::UndefinedAttr>(constant.getValue())) {
                values[constant.getResult()] = Kind::undefined;
            } else {
                refusal = "DOM entry constant has no supported scalar contract";
                return false;
            }
            continue;
        }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation)) {
            values[object.getResult()] = Kind::jsonAggregate;
            provedJSONObjects.push_back(object);
            continue;
        }
        if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(operation)) {
            auto target = copy.getTarget().getDefiningOp<ctjs::CreateObjectOp>();
            if (!target || target->getBlock() != copy->getBlock() ||
                copy.getTarget() == copy.getSource() ||
                !hasKind(copy.getSource(), Kind::jsonAggregate)) {
                refusal = "DOM JSON spread requires a fresh local target and an "
                          "object-tagged JSON source";
                return false;
            }
            provedJSONCopies.push_back(copy);
            continue;
        }
        if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation)) {
            auto constant = write.getKey().getDefiningOp<ctjs::ConstantOp>();
            auto name = constant ? llvm::dyn_cast<ctjs::StringAttr>(constant.getValue())
                                 : ctjs::StringAttr{};
            auto target = write.getObject().getDefiningOp<ctjs::CreateObjectOp>();
            if (!target || write.getObject() == write.getValue() ||
                (!hasKind(write.getValue(), Kind::string) &&
                 !hasKind(write.getValue(), Kind::optionalString) &&
                 !hasKind(write.getValue(), Kind::null) &&
                 !hasKind(write.getValue(), Kind::boolean) &&
                 !hasKind(write.getValue(), Kind::number) &&
                 !hasKind(write.getValue(), Kind::json) &&
                 !hasKind(write.getValue(), Kind::jsonAggregate))) {
                refusal = "DOM JSON assignment requires a fresh target and an owning value";
                return false;
            }
            if (!name || name.getValue() == "__proto__") {
                // The proved transforms have only one __proto__ preimage.
                // This permits one inherited setter invocation, whose
                // prototype is unobservable under the final owning data
                // contract. Ordinary collisions retain ordered overwrites.
                auto key = write.getKey().getDefiningOp<ctjs::GetPropertyOp>();
                if (!key) {
                    if (!spend()) { return false; }
                    key = strippedAssignmentKeys.lookup(write.getKey());
                }
                auto index =
                    key ? llvm::dyn_cast<mlir::BlockArgument>(key.getKey()) : mlir::BlockArgument{};
                auto loop =
                    index ? llvm::dyn_cast<mlir::scf::WhileOp>(index.getOwner()->getParentOp())
                          : mlir::scf::WhileOp{};
                auto * snapshot = key ? key.getObject().getDefiningOp() : nullptr;
                if (!key || !keyOrigins.contains(key.getResult()) ||
                    !increasingIndices.contains(index) || !loop || !snapshot ||
                    loop->isAncestor(snapshot) || loop->isAncestor(target) ||
                    !loop->isAncestor(write)) {
                    refusal = "DOM JSON assignment key requires a constant non-prototype "
                              "String or one direct snapshot traversal";
                    return false;
                }
                for (auto * parent = write->getParentOp(); parent != function;
                     parent = parent->getParentOp()) {
                    if (!spend()) { return false; }
                    if (llvm::isa<mlir::scf::WhileOp>(parent) && parent != loop) {
                        refusal = "DOM JSON snapshot assignment cannot repeat in another loop";
                        return false;
                    }
                }
                for (mlir::OpOperand & use : target.getResult().getUses()) {
                    if (!spend()) { return false; }
                    auto * user = use.getOwner();
                    if (use.getOperandNumber() == 0 && user != write &&
                        llvm::isa<ctjs::SetPropertyOp, ctjs::CopyPropsOp>(user)) {
                        refusal = "DOM JSON snapshot assignment requires the sole target writer";
                        return false;
                    }
                }
                provedSnapshotAssignments.push_back(write);
            }
            provedJSONAssignments.push_back(write);
            continue;
        }
        if (auto enter = llvm::dyn_cast<ctjs::FrameEnterOp>(operation)) {
            if (depth || entered) {
                refusal = "DOM entry has more than one shadow frame";
                return false;
            }
            entered = true;
            frame = enter.getContext();
            continue;
        }
        if (auto root = llvm::dyn_cast<ctjs::RootOp>(operation)) {
            if (!frame || root.getContext() != frame) {
                refusal = "DOM entry root is outside its local shadow frame";
                return false;
            }
            continue;
        }
        if (auto exit = llvm::dyn_cast<ctjs::FrameExitOp>(operation)) {
            if (!frame || exit.getContext() != frame) {
                refusal = "DOM entry exits an unknown shadow frame";
                return false;
            }
            for (auto * parent = exit->getParentOp(); parent != function;
                 parent = parent->getParentOp()) {
                if (!spend()) { return false; }
                if (!llvm::isa<mlir::scf::IfOp>(parent)) {
                    refusal = "DOM entry frame exit requires an acyclic conditional path";
                    return false;
                }
            }
            frame = {};
            continue;
        }
        if (auto result = llvm::dyn_cast<ctjs::ReturnOp>(operation)) {
            if (callbackBody &&
                !hasKind(result.getValue(), replacementBody ? Kind::string : Kind::boolean)) {
                refusal = "DOM callback must return its proved scalar kind";
                return false;
            }
            if (depth || frame ||
                (!hasKind(result.getValue(), Kind::undefined) &&
                 !hasKind(result.getValue(), Kind::boolean) &&
                 !hasKind(result.getValue(), Kind::number) &&
                 !hasKind(result.getValue(), Kind::string) &&
                 !hasKind(result.getValue(), Kind::undefinedString) &&
                 !hasKind(result.getValue(), Kind::optionalString) &&
                 !hasKind(result.getValue(), Kind::json) &&
                 !hasKind(result.getValue(), Kind::jsonAggregate) &&
                 !hasKind(result.getValue(), Kind::stringVector) &&
                 !hasKind(result.getValue(), Kind::symbol))) {
                refusal = "DOM entry return must be a scalar with no borrowed browser handle";
                return false;
            }
            returned = true;
            if (!callbackBody) {
                provedUndefinedReturn = hasKind(result.getValue(), Kind::undefined);
            }
            continue;
        }
        if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(operation)) {
            auto callback = indexedCallbacks.lookup(static_cast<unsigned>(closure.getFunction()));
            const bool replacement = replacementCallbacks.contains(callback);
            if (closure.getFunctionAttr().getInt() < 0 ||
                (replacement ? !suppliedRegExp : !suppliedArray) || !suppliedString || !callback ||
                !closure.getUpvalues().empty() ||
                closure.getEnclosingClosure() != block.getArgument(ctjs::arg_callee) ||
                (closure.getEnclosingThis() != block.getArgument(ctjs::arg_receiver) &&
                 !hasKind(closure.getEnclosingThis(), Kind::undefined))) {
                refusal = "DOM filter callback lacks original intrinsics or a capture-free "
                          "target";
                return false;
            }
            unsigned calls = 0;
            for (mlir::OpOperand & use : closure.getResult().getUses()) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                if (!call || use.getOperandNumber() != (replacement ? 3u : 2u) ||
                    call.getArgs().size() != (replacement ? 2u : 1u) ||
                    !dominance.properlyDominates(closure.getOperation(), call.getOperation())) {
                    refusal = "DOM filter callback escapes its exact local invocation";
                    return false;
                }
                ++calls;
            }
            if (!calls) {
                refusal = "DOM filter callback has no source invocation";
                return false;
            }
            values[closure.getResult()] = replacement ? Kind::replacementCallback : Kind::callback;
            provedClosures.emplace_back(closure, callback);
            usedCallbacks.insert(callback);
            continue;
        }
        if (const auto handled = browserOperation(operation)) {
            if (!*handled) { return false; }
            continue;
        }
        if (auto binary = llvm::dyn_cast<ctjs::BinaryStaticOp>(operation);
            binary && binary.getKind() == ctjs::BinaryKind::Add &&
            hasKind(binary.getLhs(), Kind::number) && hasKind(binary.getRhs(), Kind::number)) {
            values[binary.getResult()] = Kind::number;
            continue;
        }
        if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
            binary && binary.getKind() == ctjs::BinaryKind::Add &&
            hasKind(binary.getLhs(), Kind::number) && hasKind(binary.getRhs(), Kind::number)) {
            values[binary.getResult()] = Kind::number;
            continue;
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
            compare &&
            (compare.getKind() == ctjs::CompareKind::Lt ||
             compare.getKind() == ctjs::CompareKind::Gt) &&
            hasKind(compare.getLhs(), Kind::number) && hasKind(compare.getRhs(), Kind::number)) {
            values[compare.getResult()] = Kind::boolean;
            continue;
        }
        if (auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
            binary &&
            (binary.getKind() == ctjs::BinaryKind::Add ||
             binary.getKind() == ctjs::BinaryKind::Concat) &&
            hasKind(binary.getLhs(), Kind::string) && hasKind(binary.getRhs(), Kind::string)) {
            values[binary.getResult()] = Kind::string;
            if (!spend() || !spend()) { return false; }
            const auto original = loweredFirstUnits.lookup(binary.getLhs());
            if (original && stringTails.lookup(binary.getRhs()) == original) {
                if (!spend()) { return false; }
                if (auto key = strippedAssignmentKeys.lookup(original)) {
                    // Full lowercase may expand or collide, but only '_'
                    // lowers to '_'. With the unchanged tail, __proto__
                    // still has exactly one original bs__proto__ preimage.
                    // This never grants membership in the source dataset.
                    strippedAssignmentKeys[binary.getResult()] = key;
                }
            }
            continue;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
            unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
            (hasKind(unary.getOperand(), Kind::optionalString) ||
             hasKind(unary.getOperand(), Kind::undefinedString) ||
             hasKind(unary.getOperand(), Kind::string) || hasKind(unary.getOperand(), Kind::null) ||
             hasKind(unary.getOperand(), Kind::json) ||
             hasKind(unary.getOperand(), Kind::jsonAggregate) ||
             hasKind(unary.getOperand(), Kind::symbol))) {
            if (!spend()) { return false; }
            values[unary.getResult()] = Kind::string;
            // JSON's object tag includes null and arrays, never member
            // authority. It permits only their own-data spread.
            if (hasKind(unary.getOperand(), Kind::optionalString) ||
                hasKind(unary.getOperand(), Kind::undefinedString) ||
                hasKind(unary.getOperand(), Kind::json)) {
                typeQueries[unary.getResult()] = unary.getOperand();
            }
            continue;
        }
        if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
            compare && (compare.getKind() == ctjs::CompareKind::StrictEq ||
                        compare.getKind() == ctjs::CompareKind::Eq)) {
            const auto elementIdentity = [&](mlir::Value value) {
                return hasKind(value, Kind::element) || hasKind(value, Kind::nullableElement);
            };
            const auto stringOrNull = [&](mlir::Value value) {
                return hasKind(value, Kind::string) || hasKind(value, Kind::optionalString) ||
                       hasKind(value, Kind::null);
            };
            const bool strings =
                hasKind(compare.getLhs(), Kind::string) && hasKind(compare.getRhs(), Kind::string);
            const auto numberOrBoolean = [&](mlir::Value value) {
                return hasKind(value, Kind::number) || hasKind(value, Kind::boolean);
            };
            const bool scalars =
                numberOrBoolean(compare.getLhs()) && numberOrBoolean(compare.getRhs());
            const bool strict = compare.getKind() == ctjs::CompareKind::StrictEq;
            const bool description = hasKind(compare.getLhs(), Kind::undefinedString) ||
                                     hasKind(compare.getRhs(), Kind::undefinedString);
            if (description) {
                const auto stringOrUndefined = [&](mlir::Value value) {
                    return hasKind(value, Kind::string) || hasKind(value, Kind::undefinedString) ||
                           hasKind(value, Kind::undefined);
                };
                if (stringOrUndefined(compare.getLhs()) && stringOrUndefined(compare.getRhs())) {
                    for (auto [query, absent] : {std::pair{compare.getLhs(), compare.getRhs()},
                                                 std::pair{compare.getRhs(), compare.getLhs()}}) {
                        if (!spend()) { return false; }
                        if (hasKind(query, Kind::undefinedString) &&
                            hasKind(absent, Kind::undefined)) {
                            predicates[compare.getResult()] = {query, false};
                        }
                    }
                    values[compare.getResult()] = Kind::boolean;
                    continue;
                }
                // This owning source result can be undefined, never DOM null.
                if (strict && (hasKind(compare.getLhs(), Kind::null) ||
                               hasKind(compare.getRhs(), Kind::null))) {
                    if (!spend()) { return false; }
                    values[compare.getResult()] = Kind::boolean;
                    constantBooleans[compare.getResult()] = false;
                    provedBooleans.emplace_back(compare.getResult(), false);
                    continue;
                }
            }
            if (strict && (hasKind(compare.getLhs(), Kind::undefined) ||
                           hasKind(compare.getRhs(), Kind::undefined))) {
                const auto scalar = [&](mlir::Value value) {
                    return stringOrNull(value) || hasKind(value, Kind::undefined) ||
                           hasKind(value, Kind::boolean) || hasKind(value, Kind::number) ||
                           hasKind(value, Kind::json) || hasKind(value, Kind::jsonAggregate);
                };
                if (scalar(compare.getLhs()) && scalar(compare.getRhs())) {
                    if (!spend()) { return false; }
                    const bool equal = hasKind(compare.getLhs(), Kind::undefined) &&
                                       hasKind(compare.getRhs(), Kind::undefined);
                    values[compare.getResult()] = Kind::boolean;
                    constantBooleans[compare.getResult()] = equal;
                    provedBooleans.emplace_back(compare.getResult(), equal);
                    continue;
                }
            }
            const bool symbols =
                hasKind(compare.getLhs(), Kind::symbol) && hasKind(compare.getRhs(), Kind::symbol);
            if (strings || symbols || scalars ||
                (strict &&
                 ((elementIdentity(compare.getLhs()) && elementIdentity(compare.getRhs())) ||
                  (stringOrNull(compare.getLhs()) && stringOrNull(compare.getRhs()))))) {
                for (auto [query, literal] : {std::pair{compare.getLhs(), compare.getRhs()},
                                              std::pair{compare.getRhs(), compare.getLhs()}}) {
                    if (!spend()) { return false; }
                    const auto found = typeQueries.find(query);
                    const auto name = ctjs::constantKey(literal);
                    if (hasKind(query, Kind::optionalString) && hasKind(literal, Kind::null)) {
                        predicates[compare.getResult()] = {query, false};
                    }
                    if (found != typeQueries.end()) {
                        const bool json = hasKind(found->second, Kind::json);
                        const bool description = hasKind(found->second, Kind::undefinedString);
                        if ((!json && (name == "string" ||
                                       name == (description ? "undefined" : "object"))) ||
                            (json && name == "object")) {
                            predicates[compare.getResult()] = {found->second,
                                                               json || name == "string"};
                        }
                    }
                }
                values[compare.getResult()] = Kind::boolean;
                continue;
            }
        }
        if (auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation);
            truth &&
            (hasKind(truth.getValue(), Kind::boolean) || hasKind(truth.getValue(), Kind::number) ||
             hasKind(truth.getValue(), Kind::string) ||
             hasKind(truth.getValue(), Kind::optionalString) ||
             hasKind(truth.getValue(), Kind::undefinedString) ||
             hasKind(truth.getValue(), Kind::nullableElement) ||
             hasKind(truth.getValue(), Kind::null) || hasKind(truth.getValue(), Kind::symbol))) {
            values[truth.getResult()] = Kind::boolean;
            if (!spend()) { return false; }
            if (auto found = constantBooleans.find(truth.getValue());
                found != constantBooleans.end()) {
                const bool value = found->second;
                constantBooleans[truth.getResult()] = value;
                provedBooleans.emplace_back(truth.getResult(), value);
            }
            if (auto found = predicates.find(truth.getValue()); found != predicates.end()) {
                const Predicate predicate = found->second;
                predicates[truth.getResult()] = predicate;
            }
            if (hasKind(truth.getValue(), Kind::nullableElement)) {
                if (!spend()) { return false; }
                predicates[truth.getResult()] = {truth.getValue(), true};
            }
            if (callbackBody) {
                if (!spend()) { return false; }
                if (prefixRequired.contains(truth.getValue())) {
                    prefixRequired.insert(truth.getResult());
                }
            }
            continue;
        }
        if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
            unary && unary.getKind() == ctjs::UnaryKind::Not &&
            (hasKind(unary.getOperand(), Kind::boolean) ||
             hasKind(unary.getOperand(), Kind::number) ||
             hasKind(unary.getOperand(), Kind::optionalString) ||
             hasKind(unary.getOperand(), Kind::undefinedString) ||
             hasKind(unary.getOperand(), Kind::string) ||
             hasKind(unary.getOperand(), Kind::nullableElement) ||
             hasKind(unary.getOperand(), Kind::null) ||
             hasKind(unary.getOperand(), Kind::symbol))) {
            values[unary.getResult()] = Kind::boolean;
            if (!spend()) { return false; }
            if (auto found = predicates.find(unary.getOperand()); found != predicates.end()) {
                const Predicate predicate = found->second;
                predicates[unary.getResult()] = {predicate.optional, !predicate.stringOnTrue};
            }
            if (hasKind(unary.getOperand(), Kind::nullableElement)) {
                if (!spend()) { return false; }
                predicates[unary.getResult()] = {unary.getOperand(), false};
            }
            continue;
        }
        refusal = ("DOM entry operation lacks a typed browser contract: " +
                   operation.getName().getStringRef())
                      .str();
        return false;
    }
    if (!returned) {
        refusal = "DOM entry requires a complete return and exact source declaration";
        return false;
    }
    return true;
}

} // namespace ctcompile::ctnative::dom_entry_detail
