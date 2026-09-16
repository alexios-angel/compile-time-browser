#include "Emitter.h"

namespace ctcompile::ctnative::lowering_detail {

mlir::Type lowering::tableType(MethodTableType type) const {
    if (domDataSession.empty()) { return methodTableCarrierType(type); }
    return ec::PointerType::get(
        ec::OpaqueType::get(context, "ctnative::method_" + cIdentifier(type.getSite())));
}

std::string lowering::domDataDefinition() const {
    std::string text;
    llvm::raw_string_ostream out(text);
    out << "class " << domDataSession << " {\n"
        << "    ctbrowser::atom_table atoms_;\n"
        << "    ctbrowser::document document_{atoms_};\n";
    for (auto [i, storage] : llvm::enumerate(sessionMaps)) {
        out << "    " << storage.tableName << " data_table_" << i << ";\n"
            << "    " << storage.tableName << " * reset_data_table_" << i << "() { data_table_" << i
            << " = {}; return &data_table_" << i << "; }\n";
    }
    // Function bodies and source globals follow as private ordinary members.
    return text;
}

void lowering::censusDOM(const DOMEntryAnalysis & entry, bool ownedSession) {
    if (!entry.proved()) { return; }
    needsDOM = true;
    domStringResults.insert(entry.stringResults().begin(), entry.stringResults().end());
    domStringRefinements.assign(entry.stringRefinements().begin(), entry.stringRefinements().end());
    domOptionalStrings.insert(entry.optionalStringJoins().begin(),
                              entry.optionalStringJoins().end());
    needsDOMAttributeRead |= !domOptionalStrings.empty();
    for (mlir::BlockArgument parameter : entry.parameters()) { domParameters.insert(parameter); }
    entry.entry().walk([&](ctjs::ConstantOp constant) {
        if (llvm::isa<ctjs::NullAttr>(constant.getValue())) { domNulls.insert(constant); }
    });
    entry.entry().walk([&](ctjs::GetPropertyOp read) {
        if (entry.method(read) || entry.isTokenList(read.getResult())) { domReads.insert(read); }
    });
    entry.entry().walk([&](ctjs::LoadGlobalOp load) {
        if (entry.isInitialIntrinsic(load)) { domReads.insert(load); }
    });
    entry.entry().walk([&](ctjs::InvokeOp invocation) {
        if (!entry.invocation(invocation)) { return; }
        domInvocations.insert(invocation);
        domUnusedPayloads.insert(invocation.getUnwindBody().front().getArgument(0));
    });
    entry.entry().walk([&](ctjs::CallOp call) {
        if (const auto * edge = entry.call(call)) {
            domCalls[call] = *edge;
            needsDOMToggle |= edge->kind == HostDOMMethod::toggleClass;
            needsDOMAttributes |= edge->kind == HostDOMMethod::setAttribute;
            needsDOMAttributeRead |= edge->returnsOptionalString();
            needsDOMNumber |=
                edge->kind == HostDOMMethod::number || edge->kind == HostDOMMethod::numberToString;
            needsDOMURI |= edge->kind == HostDOMMethod::decodeURIComponent;
            needsDOMJSON |= edge->kind == HostDOMMethod::jsonParse;
            needsDOMAttributeToggle |= edge->kind == HostDOMMethod::toggleAttribute;
            needsDOMAttributePresence |= edge->kind == HostDOMMethod::hasAttribute;
            needsDOMAttributeRemoval |= edge->kind == HostDOMMethod::removeAttribute;
            needsDOMContains |= edge->kind == HostDOMMethod::contains;
            needsDOMMatches |= edge->kind == HostDOMMethod::matches;
            needsDOMClosest |= edge->kind == HostDOMMethod::closest;
            if (edge->returnsNumber() || edge->kind == HostDOMMethod::decodeURIComponent) {
                auto receiver = call.getReceiver().getDefiningOp<ctjs::ConstantOp>();
                if (receiver && llvm::isa<ctjs::UndefinedAttr>(receiver.getValue()) &&
                    llvm::all_of(receiver.getResult().getUses(), [&](mlir::OpOperand & use) {
                        if (llvm::isa<ctjs::RootOp>(use.getOwner())) { return true; }
                        auto user = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                        const auto * number = user ? entry.call(user) : nullptr;
                        return number &&
                               (number->returnsNumber() ||
                                number->kind == HostDOMMethod::decodeURIComponent) &&
                               use.getOperandNumber() == 1;
                    })) {
                    // The proved builtin does not observe its undefined receiver.
                    // Keep its source identity until calls and roots are erased.
                    domReads.insert(receiver);
                }
            }
        }
    });
    for (mlir::BlockArgument parameter : entry.parameters()) {
        if (llvm::any_of(domCalls, [&](const auto & item) {
                return item.second.usesStyle() && item.second.element == parameter;
            })) {
            domStyleParameters.push_back(parameter);
        }
    }
    if (ownedSession) {
        const auto named = names.find(entry.entry().getSymName());
        const std::string symbol =
            named == names.end() ? cIdentifier(entry.entry().getSymName()) : named->second;
        const std::string owner = symbol + "_session";
        std::string text;
        llvm::raw_string_ostream out(text);
        out << "// ctcompile: owned synchronous DOM entry for " << symbol << "\n"
            << "class " << owner << " {\n"
            << "    ctbrowser::atom_table atoms_;\n"
            << "    ctbrowser::document document_{atoms_};\n";
        if (!domStyleParameters.empty()) {
            out << "    ctbrowser::style::engine selectors_{atoms_};\n";
        }
        out << "public:\n"
            << "    " << owner << "() = default;\n"
            << "    " << owner << "(const " << owner << " &) = delete;\n"
            << "    " << owner << " & operator=(const " << owner << " &) = delete;\n"
            << "    " << owner << "(" << owner << " &&) = delete;\n"
            << "    " << owner << " & operator=(" << owner << " &&) = delete;\n"
            << "    ctbrowser::document & document() { return document_; }\n";
        if (!domStyleParameters.empty()) {
            out << "    ctbrowser::style::engine & selectors() { return selectors_; }\n";
        }
        // Spell the actual checked parameter list. No callable or generic
        // argument forwarding can escape with a borrowed document capture.
        out << "    auto invoke(";
        for (unsigned i = 0; i < entry.parameters().size(); ++i) {
            if (i) { out << ", "; }
            out << "ctbrowser::element_ref element" << i;
        }
        out << ") {\n        if (";
        for (unsigned i = 0; i < entry.parameters().size(); ++i) {
            if (i) { out << " || "; }
            out << "element" << i << ".owner != &document_";
        }
        // Check every domain before the entry validates ANY handle. A foreign
        // pointer can already dangle, so validation must never dereference it.
        out << ") { throw std::invalid_argument(\"DOM element belongs to another session\"); }\n"
            << "        return " << symbol << "(";
        for (unsigned i = 0; i < entry.parameters().size(); ++i) {
            if (i) { out << ", "; }
            out << "element" << i;
        }
        for (mlir::BlockArgument parameter : domStyleParameters) {
            (void)parameter;
            out << ", selectors_";
        }
        out << ");\n    }\n};\n";
        domSessionDefinition = std::move(text);
    }
}

void lowering::prepareDOMStrings() {
    for (const auto & refinement : domStringRefinements) {
        if (refinement.uses.empty()) { continue; }
        mlir::OpBuilder at = mlir::OpBuilder::atBlockBegin(refinement.block);
        // Copy only inside the proved-present arm. The original optional remains
        // intact, and Invoke's call body keeps its exact call/exit shape.
        auto text = ec::MemberCallOpaqueOp::create(
            at, refinement.optional.getLoc(),
            mlir::TypeRange{carrierType(context, carrier::string)}, refinement.optional,
            at.getStringAttr("value"), mlir::ArrayAttr{}, mlir::ArrayAttr{}, mlir::ValueRange{});
        for (const auto & use : refinement.uses) {
            use.operation->setOperand(use.operandIndex, text.getResult(0));
        }
    }
    domStringRefinements.clear();
}

bool lowering::replaceDOM(mlir::Operation * operation) {
    if (domReads.contains(operation)) { return true; } // erased after their calls
    mlir::OpBuilder at(operation);
    const auto where = operation->getLoc();
    const auto optionalString = ec::OpaqueType::get(context, kDOMOptionalStringType);
    if (domInvocations.contains(operation)) {
        auto invocation = llvm::cast<ctjs::InvokeOp>(operation);
        auto call = llvm::cast<ctjs::CallOp>(invocation.getBody().front().front());
        // decodeURIComponent answers std::optional<std::string>; parse_json
        // answers std::expected<json_value, std::size_t>. Both move on success.
        const bool parses = domCalls.find(call)->second.kind == HostDOMMethod::jsonParse;
        const auto fallible =
            parses
                ? ec::OpaqueType::get(context, "std::expected<ctbrowser::json_value, std::size_t>")
                : optionalString;
        const mlir::Type produced =
            parses ? carrierType(context, carrier::json) : carrierType(context, carrier::string);
        auto decoded = callWithConstValueOperands(
            at, where, mlir::TypeRange{fallible},
            at.getStringAttr(parses ? "ctbrowser::parse_json" : "ctbrowser::decode_uri_component"),
            call.getArgs());
        auto present = ec::MemberCallOpaqueOp::create(
            at, where, mlir::TypeRange{at.getI1Type()}, decoded.getResult(0),
            at.getStringAttr("has_value"), mlir::ArrayAttr{}, mlir::ArrayAttr{},
            mlir::ValueRange{});
        auto branch =
            mlir::scf::IfOp::create(at, where, invocation.getResultTypes(), present.getResult(0));
        branch.getThenRegion().takeBody(invocation.getNormalBody());
        branch.getElseRegion().takeBody(invocation.getUnwindBody());
        auto & success = branch.getThenRegion().front();
        mlir::OpBuilder inside = mlir::OpBuilder::atBlockBegin(&success);
        // Move only on success. The original input and failure continuation
        // remain untouched; foreign allocation failures never select this else.
        auto value = ec::ExpressionOp::create(inside, where, produced,
                                              mlir::ValueRange{decoded.getResult(0)}, false);
        value.createBody();
        inside.setInsertionPointToStart(&value.getRegion().front());
        auto moved = ec::CallOpaqueOp::create(
            inside, where,
            mlir::TypeRange{ec::OpaqueType::get(
                context, llvm::cast<ec::OpaqueType>(fallible).getValue().str() + " &&")},
            inside.getStringAttr("std::move"),
            mlir::ValueRange{value.getRegion().front().getArgument(0)});
        auto extracted = ec::MemberCallOpaqueOp::create(
            inside, where, mlir::TypeRange{produced}, moved.getResult(0),
            inside.getStringAttr("value"), mlir::ArrayAttr{}, mlir::ArrayAttr{},
            mlir::ValueRange{});
        ec::YieldOp::create(inside, where, extracted.getResult(0));
        success.getArgument(0).replaceAllUsesWith(value.getResult());
        success.eraseArgument(0);
        branch.getElseRegion().front().eraseArgument(0);
        for (mlir::Region & region : branch->getRegions()) {
            auto yielded = llvm::cast<ctjs::InvokeYieldOp>(region.front().getTerminator());
            mlir::OpBuilder end(yielded);
            mlir::scf::YieldOp::create(end, where, yielded.getValues());
            yielded.erase();
        }
        invocation.getResult(0).replaceAllUsesWith(branch.getResult(0));
        domCalls.erase(call);
        domInvocations.erase(invocation);
        invocation.erase();
        return true;
    }
    if (llvm::isa<ctjs::InvokeExitOp, ctjs::InvokeYieldOp>(operation) &&
        domInvocations.contains(operation->getParentOp())) {
        return true; // The enclosing invocation consumes these after its children.
    }
    const auto swap = [&](mlir::Value value) {
        operation->getResult(0).replaceAllUsesWith(value);
        eraseIfUnused(operation);
    };
    const auto null = [&] {
        return ec::ConstantOp::create(at, where, optionalString,
                                      ec::OpaqueAttr::get(context, "std::nullopt"));
    };
    if (domNulls.contains(operation)) {
        swap(null());
        return true;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
        compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
        (compare.getLhs().getType() == optionalString ||
         compare.getRhs().getType() == optionalString)) {
        swap(ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::eq, compare.getLhs(),
                               compare.getRhs()));
        return true;
    }
    auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
    if (needsDOMJSON && unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
        unary.getOperand().getType() == carrierType(context, carrier::json)) {
        const auto stringType = carrierType(context, carrier::string);
        const auto dataType = ec::OpaqueType::get(context, "decltype(ctbrowser::json_value::data)");
        // Deferred member emission names the original tree's field, without a copy.
        auto data = ec::MemberOp::create(at, where, dataType, "data", unary.getOperand());
        auto expression =
            ec::ExpressionOp::create(at, where, stringType, mlir::ValueRange{data}, false);
        expression.createBody();
        mlir::OpBuilder inside = mlir::OpBuilder::atBlockBegin(&expression.getRegion().front());
        mlir::Value result = stringConstant(inside, where, "object");
        // Null, array and object alternatives share JavaScript's object tag.
        for (auto [type, tag] :
             {std::pair{"bool", "boolean"}, {"double", "number"}, {"std::string", "string"}}) {
            auto holds = callWithConstValueOperands(
                inside, where, mlir::TypeRange{inside.getI1Type()},
                inside.getStringAttr(std::string("std::holds_alternative<") + type + ">"),
                mlir::ValueRange{expression.getRegion().front().getArgument(0)});
            result = ec::ConditionalOp::create(inside, where, stringType, holds.getResult(0),
                                               stringConstant(inside, where, tag), result);
        }
        ec::YieldOp::create(inside, where, result);
        swap(expression.getResult());
        return true;
    }
    if (unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
        unary.getOperand().getType() == optionalString) {
        auto present = ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::ne,
                                         unary.getOperand(), null());
        swap(ec::ConditionalOp::create(at, where, carrierType(context, carrier::string), present,
                                       stringConstant(at, where, "string"),
                                       stringConstant(at, where, "object")));
        return true;
    }
    auto truth = llvm::dyn_cast<ctjs::TruthyOp>(operation);
    const mlir::Value tested = truth ? truth.getValue()
                               : unary && unary.getKind() == ctjs::UnaryKind::Not
                                   ? unary.getOperand()
                                   : mlir::Value{};
    if (tested && tested.getType() == optionalString) {
        // Presence alone is not JavaScript truthiness: an empty attribute is false.
        auto present =
            ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::ne, tested, null());
        auto nonempty = ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::ne, tested,
                                          stringConstant(at, where, ""));
        auto truthy = ec::LogicalAndOp::create(at, where, at.getI1Type(), present, nonempty);
        swap(truth ? mlir::Value(truthy)
                   : ec::LogicalNotOp::create(at, where, at.getI1Type(), truthy).getResult());
        return true;
    }
    const auto found = domCalls.find(operation);
    if (found == domCalls.end()) { return false; }
    auto call = llvm::cast<ctjs::CallOp>(operation);
    const auto & edge = found->second;
    if (edge.kind == HostDOMMethod::decodeURIComponent || edge.kind == HostDOMMethod::jsonParse) {
        return true;
    }
    llvm::SmallVector<mlir::Value> arguments;
    if (edge.kind == HostDOMMethod::number) {
        // Earlier replacements update the live call operands. The source
        // proof's original input may already have been erased.
        llvm::append_range(arguments, call.getArgs());
    } else if (edge.kind == HostDOMMethod::numberToString) {
        arguments.push_back(call.getReceiver());
    } else {
        arguments.push_back(edge.element);
        if (edge.usesStyle()) { arguments.push_back(domStyles.lookup(edge.element)); }
        llvm::append_range(arguments, call.getArgs());
    }
    llvm::StringRef callee;
    switch (edge.kind) {
    case HostDOMMethod::toggleClass: callee = "ctnative::toggle_class"; break;
    case HostDOMMethod::setAttribute: callee = "ctnative::set_attribute"; break;
    case HostDOMMethod::getAttribute: callee = "ctnative::get_attribute"; break;
    case HostDOMMethod::toggleAttribute: callee = "ctnative::toggle_attribute"; break;
    case HostDOMMethod::hasAttribute: callee = "ctnative::has_attribute"; break;
    case HostDOMMethod::removeAttribute: callee = "ctnative::remove_attribute"; break;
    case HostDOMMethod::contains: callee = "ctnative::contains"; break;
    case HostDOMMethod::matches: callee = "ctnative::matches"; break;
    case HostDOMMethod::closest: callee = "ctnative::closest"; break;
    case HostDOMMethod::number:
        callee = arguments.front().getType() == optionalString ? "ctnative::dom_number"
                                                               : "ctbrowser::string_to_number";
        break;
    case HostDOMMethod::numberToString: callee = "ctbrowser::number_to_string"; break;
    case HostDOMMethod::decodeURIComponent:
    case HostDOMMethod::jsonParse: llvm_unreachable("fallible call belongs to its invocation");
    }
    if (edge.returnsBoolean() || edge.returnsElement() || edge.returnsOptionalString() ||
        edge.returnsNumber() || edge.returnsString()) {
        const mlir::Type type = edge.returnsOptionalString()
                                    ? ec::OpaqueType::get(context, kDOMOptionalStringType)
                                : edge.returnsElement() ? carrierType(context, carrier::domElement)
                                : edge.returnsNumber()  ? at.getF64Type()
                                : edge.returnsString()  ? carrierType(context, carrier::string)
                                                        : at.getI1Type();
        auto value = callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{type},
                                                at.getStringAttr(callee), arguments);
        call.getResult().replaceAllUsesWith(value.getResult(0));
    } else {
        callWithConstValueOperands(at, call.getLoc(), mlir::TypeRange{}, at.getStringAttr(callee),
                                   arguments);
        if (!call.getResult().use_empty()) {
            call.getResult().replaceAllUsesWith(absentConstant(at, call.getLoc()));
        }
    }
    eraseIfUnused(call);
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
