#include "Emitter.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"

namespace ctcompile::ctnative::lowering_detail {
namespace {
// The document every owned DOM session class holds.
constexpr llvm::StringLiteral kSessionDocument =
    "    ctbrowser::atom_table atoms_;\n    ctbrowser::document document_{atoms_};\n";
} // namespace

std::string nonmovable(llvm::StringRef name, llvm::StringRef indent) {
    const std::string n = name.str(), i = indent.str();
    return i + n + "() = default;\n" + i + n + "(const " + n + " &) = delete;\n" + i + n +
           " & operator=(const " + n + " &) = delete;\n" + i + n + "(" + n + " &&) = delete;\n" +
           i + n + " & operator=(" + n + " &&) = delete;\n";
}

void sessionInvoke(llvm::raw_ostream & out, llvm::StringRef entry, size_t count,
                   llvm::StringRef extra) {
    out << "    auto invoke(";
    for (size_t i = 0; i < count; ++i) {
        out << (i ? ", " : "") << "ctbrowser::element_ref element" << i;
    }
    out << ") {\n        if (";
    for (size_t i = 0; i < count; ++i) {
        out << (i ? " || " : "") << "element" << i << ".owner != &document_";
    }
    // Check every domain before the entry validates ANY handle. A foreign
    // pointer can already dangle, so validation must never dereference it.
    out << ") { throw std::invalid_argument(\"DOM element belongs to another session\"); }\n"
        << "        return " << entry << "(";
    for (size_t i = 0; i < count; ++i) { out << (i ? ", " : "") << "element" << i; }
    out << extra << ");\n    }\n";
}

mlir::Type lowering::tableType(MethodTableType type) const {
    if (domDataSession.empty()) { return methodTableCarrierType(type); }
    return ec::PointerType::get(
        ec::OpaqueType::get(context, "ctnative::method_" + cIdentifier(type.getSite())));
}

std::string lowering::domDataDefinition() const {
    std::string text;
    llvm::raw_string_ostream out(text);
    out << "class " << domDataSession << " {\n" << kSessionDocument;
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
    domThrows.insert(entry.savedThrows().begin(), entry.savedThrows().end());
    hasHostEntry = true;
    domDocumentParameter = entry.documentParameter();
    llvm::append_range(domStyleValues, entry.styleValues());
    needsDOM |= !entry.parameters().empty();
    if (entry.returnsUndefined()) {
        resultTypes[entry.entry().getSymName()] = mlir::NoneType::get(context);
    }
    domStringResults.insert(entry.stringResults().begin(), entry.stringResults().end());
    domStringRefinements.assign(entry.stringRefinements().begin(), entry.stringRefinements().end());
    domOptionalStrings.insert(entry.optionalStringJoins().begin(),
                              entry.optionalStringJoins().end());
    for (mlir::BlockArgument parameter : entry.parameters()) {
        domParameters.insert(parameter);
        if (entry.isDatasetElement(parameter)) { domDatasetParameters.insert(parameter); }
    }
    llvm::SmallVector<ctjs::FuncOp> functions{entry.entry()};
    llvm::append_range(functions, entry.callbacks());
    for (ctjs::FuncOp function : functions) {
        function.walk([&](ctjs::SetPropertyOp write) {
            if (entry.jsonSnapshotAssignment(write)) { domSnapshotAssignments.insert(write); }
        });
        function.walk([&](mlir::Operation * o) {
            if (domBookkeeping(entry, o)) { domReads.insert(o); }
        });
        function.walk([&](ctjs::ConstantOp constant) {
            if (llvm::isa<ctjs::NullAttr>(constant.getValue())) { domNulls.insert(constant); }
        });
        function.walk([&](ctjs::GetPropertyOp read) {
            if (entry.isDocumentElement(read)) { domDocumentRoots.insert(read); }
            if (entry.isSymbolDescription(read)) { domSymbolDescriptions.insert(read); }
            if (auto name = entry.wellKnownSymbol(read); !name.empty()) {
                domSymbols.try_emplace(read, name);
            }
            if (entry.isStringVectorLength(read) || entry.isElementVectorLength(read)) {
                vectorLengthReads.insert(read);
            }
            if (entry.isStringVectorIndex(read)) { domStringVectorIndices.insert(read); }
            if (entry.isElementVectorIndex(read)) { domElementVectorIndices.insert(read); }
            if (auto element = entry.datasetValueElement(read)) {
                domDatasetValues.try_emplace(read, element);
            }
        });
        function.walk([&](ctjs::InvokeOp invocation) {
            if (!entry.invocation(invocation)) { return; }
            domInvocations.insert(invocation);
            domUnusedPayloads.insert(invocation.getUnwindBody().front().getArgument(0));
            if (invocation.getNumResults() == 0) {
                domUnusedPayloads.insert(invocation.getNormalBody().front().getArgument(0));
            }
        });
        function.walk([&](ctjs::CallOp call) {
            if (const auto * edge = entry.call(call)) {
                domCalls[call] = *edge;
                if (edge->returnsNumber() || edge->kind == HostDOMMethod::decodeURIComponent) {
                    mlir::Value receiver = call.getReceiver();
                    if (ctjs::isUndefinedConstant(receiver) &&
                        llvm::all_of(receiver.getUses(), [&](mlir::OpOperand & use) {
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
                        domReads.insert(receiver.getDefiningOp());
                    }
                }
            }
        });
    }
    for (mlir::BlockArgument parameter : entry.parameters()) {
        if (parameter == domDocumentParameter || llvm::any_of(domCalls, [&](const auto & item) {
                return item.second.usesStyle() &&
                       (item.second.styleParameter == parameter ||
                        llvm::is_contained(item.second.styleParameters, parameter));
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
            << kSessionDocument;
        if (!domStyleParameters.empty()) {
            out << "    ctbrowser::style::engine selectors_{atoms_};\n";
        }
        out << "public:\n"
            << nonmovable(owner, "    ")
            << "    ctbrowser::document & document() { return document_; }\n";
        if (!domStyleParameters.empty()) {
            out << "    ctbrowser::style::engine & selectors() { return selectors_; }\n";
        }
        // Spell the actual checked parameter list. No callable or generic
        // argument forwarding can escape with a borrowed document capture.
        std::string styles;
        for (size_t i = 0; i < domStyleParameters.size(); ++i) { styles += ", selectors_"; }
        sessionInvoke(out, symbol, entry.parameters().size(), styles);
        out << "};\n";
        domSessionDefinition = std::move(text);
    }
}

void lowering::prepareDOMStyles(ec::FuncOp function) {
    if (domStyleValues.empty() ||
        llvm::none_of(domStyleParameters, [&](mlir::BlockArgument parameter) {
            return parameter.getOwner() == &function.getBody().front();
        })) {
        return;
    }
    const auto engine = ec::OpaqueType::get(context, "ctbrowser::style::engine");
    const auto pointer = ec::PointerType::get(engine);
    auto at = mlir::OpBuilder::atBlockBegin(&function.getBody().front());
    // Temporary SSA links permit backedges. All links are resolved and erased
    // below; only ordinary borrowed pointers reach the C++ output.
    llvm::DenseMap<mlir::Value, mlir::UnrealizedConversionCastOp> links;
    for (auto value : domStyleValues) {
        links[value] = mlir::UnrealizedConversionCastOp::create(
            at, value.getLoc(), mlir::TypeRange{pointer}, mlir::ValueRange{});
    }
    const auto source = [&](mlir::Value value) { return links.find(value)->second.getResult(0); };
    const auto bind = [&](mlir::Value value, mlir::Value style) {
        links.find(value)->second->setOperands(mlir::ValueRange{style});
    };
    for (auto value : domStyleValues) {
        if (auto style = domStyles.lookup(value)) {
            bind(value, callValue(at, value.getLoc(), pointer, "std::addressof", style));
        } else if (auto call = domCalls.find(value.getDefiningOp()); call != domCalls.end()) {
            bind(value, source(call->second.element));
        } else if (auto read = value.getDefiningOp<ctjs::GetPropertyOp>()) {
            bind(value, source(domDocumentRoots.contains(read) ? mlir::Value(domDocumentParameter)
                                                               : read.getObject()));
        }
    }
    llvm::DenseMap<mlir::Value, mlir::Value> replacements;
    const auto replaceResults = [&](mlir::Operation * old, mlir::Operation * made) {
        for (auto [index, value] : llvm::enumerate(old->getResults())) {
            replacements[value] = made->getResult(static_cast<unsigned>(index));
            if (auto link = links.lookup(value)) {
                links[made->getResult(static_cast<unsigned>(index))] = link;
            }
            value.replaceAllUsesWith(made->getResult(static_cast<unsigned>(index)));
        }
        // Source identities in the remaining frozen plans are not SSA uses.
        for (auto & item : domDatasetValues) {
            if (auto replacement = replacements.lookup(item.second)) { item.second = replacement; }
        }
    };
    llvm::SmallVector<mlir::Operation *> retired;
    function.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * operation) {
        mlir::OpBuilder before(operation);
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(operation)) {
            llvm::SmallVector<unsigned> slots;
            for (auto result : branch.getResults()) {
                if (links.contains(result)) { slots.push_back(result.getResultNumber()); }
            }
            if (slots.empty()) { return; }
            llvm::SmallVector<mlir::Type> types(branch.getResultTypes());
            types.append(slots.size(), pointer);
            auto made = mlir::scf::IfOp::create(before, branch.getLoc(), types,
                                                branch.getCondition(), true);
            made.getThenRegion().takeBody(branch.getThenRegion());
            made.getElseRegion().takeBody(branch.getElseRegion());
            for (mlir::Region & region : made->getRegions()) {
                auto yield = llvm::cast<mlir::scf::YieldOp>(region.front().getTerminator());
                llvm::SmallVector<mlir::Value> styles;
                for (unsigned slot : slots) { styles.push_back(source(yield.getOperand(slot))); }
                yield->insertOperands(yield.getNumOperands(), styles);
            }
            for (auto [offset, slot] : llvm::enumerate(slots)) {
                bind(branch.getResult(slot),
                     made.getResult(branch.getNumResults() + static_cast<unsigned>(offset)));
            }
            replaceResults(branch, made);
            retired.push_back(branch);
        } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(operation)) {
            llvm::SmallVector<unsigned> inputs, outputs;
            for (auto arg : loop.getBeforeArguments()) {
                if (links.contains(arg)) { inputs.push_back(arg.getArgNumber()); }
            }
            for (auto arg : loop.getAfterArguments()) {
                if (links.contains(arg) || links.contains(loop.getResult(arg.getArgNumber()))) {
                    outputs.push_back(arg.getArgNumber());
                }
            }
            if (inputs.empty() && outputs.empty()) { return; }
            llvm::SmallVector<mlir::Value> inits(loop.getInits());
            for (unsigned slot : inputs) { inits.push_back(source(loop.getInits()[slot])); }
            llvm::SmallVector<mlir::Type> types(loop.getResultTypes());
            types.append(outputs.size(), pointer);
            auto made = mlir::scf::WhileOp::create(before, loop.getLoc(), types, inits);
            made.getBefore().takeBody(loop.getBefore());
            made.getAfter().takeBody(loop.getAfter());
            auto & head = made.getBefore().front();
            auto & body = made.getAfter().front();
            for (unsigned slot : inputs) {
                bind(head.getArgument(slot), head.addArgument(pointer, loop.getLoc()));
            }
            auto condition = made.getConditionOp();
            llvm::SmallVector<mlir::Value> styles;
            for (auto [offset, slot] : llvm::enumerate(outputs)) {
                auto argument = body.addArgument(pointer, loop.getLoc());
                if (links.contains(body.getArgument(slot))) {
                    bind(body.getArgument(slot), argument);
                }
                if (links.contains(loop.getResult(slot))) {
                    bind(loop.getResult(slot),
                         made.getResult(loop.getNumResults() + static_cast<unsigned>(offset)));
                }
                styles.push_back(source(condition.getArgs()[slot]));
            }
            condition.getArgsMutable().append(styles);
            auto yield = made.getYieldOp();
            styles.clear();
            for (unsigned slot : inputs) { styles.push_back(source(yield.getOperand(slot))); }
            yield->insertOperands(yield.getNumOperands(), styles);
            replaceResults(loop, made);
            retired.push_back(loop);
        }
    });
    for (auto value : domStyleValues) {
        mlir::Value style = source(value);
        while (auto link = style.getDefiningOp<mlir::UnrealizedConversionCastOp>()) {
            assert(link.getInputs().size() == 1 && "complete Style transport must resolve");
            style = link.getInputs().front();
        }
        source(value).replaceAllUsesWith(style);
        auto replacement = replacements.lookup(value);
        domStylePointers[replacement ? replacement : value] = style;
    }
    for (auto value : domStyleValues) { links.find(value)->second.erase(); }
    for (auto * operation : retired) { operation->erase(); }
    domStyleValues.clear();
}

void lowering::prepareDOMStrings() {
    for (const auto & refinement : domStringRefinements) {
        if (refinement.uses.empty()) { continue; }
        mlir::OpBuilder at = mlir::OpBuilder::atBlockBegin(refinement.block);
        // Copy only inside the proved-present arm. The original optional remains
        // intact, and Invoke's call body keeps its exact call/exit shape.
        const auto rawString = ec::OpaqueType::get(context, kRawStringType);
        mlir::Value text;
        if (isNullableStringCarrier(refinement.optional.getType())) {
            text = callValue(at, refinement.optional.getLoc(), rawString, "ctnative::global_string",
                             refinement.optional);
        } else {
            text = memberCall(at, refinement.optional.getLoc(), rawString, refinement.optional,
                              "value");
        }
        auto value = convertScalar(at, refinement.optional.getLoc(), text,
                                   carrierType(context, carrier::string));
        for (const auto & use : refinement.uses) {
            use.operation->setOperand(use.operandIndex, value);
        }
    }
    domStringRefinements.clear();
}

bool lowering::replaceDOM(mlir::Operation * operation) {
    if (domReads.contains(operation)) { return true; } // erased after their calls
    if (domThrows.erase(operation)) {
        auto thrown = llvm::cast<ctjs::ThrowOp>(operation);
        auto * region = thrown->getParentOp();
        mlir::OpBuilder at(region);
        auto where = thrown.getLoc();
        // Keep a terminating scope: SCF result lowering may append unreachable
        // assignments outside it. The printer spells this unconditional scope
        // as an ordinary C++ block, without a runtime control flag.
        auto always = ec::LiteralOp::create(at, where, at.getI1Type(), "true");
        auto scope = ec::IfOp::create(at, where, always, false);
        mlir::OpBuilder body = mlir::OpBuilder::atBlockBegin(&scope.getThenRegion().front());
        const auto value = thrown.getValue();
        if (value.getType() == ec::OpaqueType::get(context, kDOMOptionalStringType)) {
            auto present = memberCall(body, where, body.getI1Type(), value, "has_value");
            auto selected = ec::IfOp::create(body, where, present, true);
            mlir::OpBuilder yes = mlir::OpBuilder::atBlockBegin(&selected.getThenRegion().front());
            auto text = memberCall(yes, where, ec::OpaqueType::get(context, kRawStringType), value,
                                   "value");
            CppThrowOp::create(
                yes, where, convertScalar(yes, where, text, carrierType(context, carrier::string)));
            ec::YieldOp::create(yes, where);
            mlir::OpBuilder no = mlir::OpBuilder::atBlockBegin(&selected.getElseRegion().front());
            auto absent = ec::LiteralOp::create(no, where,
                                                ec::OpaqueType::get(context, "ctnative::js_null_t"),
                                                "ctnative::js_null_t{}");
            CppThrowOp::create(no, where, absent);
            ec::YieldOp::create(no, where);
        } else {
            CppThrowOp::create(body, where, value);
        }
        ec::YieldOp::create(body, where);
        region->erase();
        return true;
    }
    mlir::OpBuilder at(operation);
    const auto where = operation->getLoc();
    const auto optionalString = ec::OpaqueType::get(context, kDOMOptionalStringType);
    const auto rawString = ec::OpaqueType::get(context, kRawStringType);
    const auto rawText = [&](mlir::Value value) {
        return value.getType() == carrierType(context, carrier::string)
                   ? convertScalar(at, where, value, rawString)
                   : value;
    };
    const auto documentCall = domCalls.find(operation);
    const bool documentRoot = domDocumentRoots.contains(operation);
    const bool documentAll = documentCall != domCalls.end() &&
                             documentCall->second.kind == HostDOMMethod::documentQuerySelectorAll;
    if (documentRoot || documentAll ||
        (documentCall != domCalls.end() &&
         documentCall->second.kind == HostDOMMethod::documentQuerySelector)) {
        mlir::ValueRange arguments;
        if (!documentRoot) { arguments = llvm::cast<ctjs::CallOp>(operation).getArgs(); }
        mlir::Value value = callValue(
            at, where,
            ec::OpaqueType::get(context, documentAll ? llvm::StringRef(kDOMElementViewVectorType)
                                                     : "std::optional<ctnative::js_element_t>"),
            documentRoot  ? "ctnative::document.documentElement"
            : documentAll ? "ctnative::document.querySelectorAll"
                          : "ctnative::document.querySelector",
            arguments);
        if (!documentAll) {
            value = callValue(at, where, carrierType(context, carrier::domElement),
                              "ctnative::element_or_null", value);
        }
        domStyles[value] = domStyles.lookup(domDocumentParameter);
        if (auto style = domStylePointers.lookup(operation->getResult(0))) {
            domStylePointers[value] = style;
        }
        operation->getResult(0).replaceAllUsesWith(value);
        eraseIfUnused(operation);
        return true;
    }
    if (auto found = domSymbols.find(operation); found != domSymbols.end()) {
        auto read = llvm::cast<ctjs::GetPropertyOp>(operation);
        auto value = ec::ConstantOp::create(
            at, where, carrierType(context, carrier::symbol),
            ec::OpaqueAttr::get(context, ("ctnative::Symbol." + found->second).str()));
        read.getResult().replaceAllUsesWith(value.getResult());
        read.erase();
        return true;
    }
    if (domSymbolDescriptions.contains(operation)) {
        auto read = llvm::cast<ctjs::GetPropertyOp>(operation);
        const auto type = carrierType(context, carrier::nullableString);
        auto description = memberCall(
            at, where, ec::OpaqueType::get(context, "std::optional<ctnative::js_string>"),
            read.getObject(), "description");
        auto present = memberCall(at, where, at.getI1Type(), description, "has_value");
        auto branch = mlir::scf::IfOp::create(at, where, mlir::TypeRange{type}, present, true);
        auto inside = mlir::OpBuilder::atBlockBegin(&branch.getThenRegion().front());
        auto text =
            memberCall(inside, where, carrierType(context, carrier::string), description, "value");
        mlir::scf::YieldOp::create(inside, where, convertScalar(inside, where, text, type));
        inside.setInsertionPointToStart(&branch.getElseRegion().front());
        mlir::scf::YieldOp::create(
            inside, where, convertScalar(inside, where, absentConstant(inside, where), type));
        read.getResult().replaceAllUsesWith(branch.getResult(0));
        read.erase();
        return true;
    }
    if (auto found = domDatasetValues.find(operation); found != domDatasetValues.end()) {
        auto read = llvm::cast<ctjs::GetPropertyOp>(operation);
        auto value = callValue(at, where, rawString, "ctnative::dataset_value",
                               mlir::ValueRange{found->second, rawText(read.getKey())});
        read.getResult().replaceAllUsesWith(
            convertScalar(at, where, value, read.getResult().getType()));
        read.erase();
        return true;
    }
    if (domStringVectorIndices.contains(operation) || domElementVectorIndices.contains(operation)) {
        auto read = llvm::cast<ctjs::GetPropertyOp>(operation);
        const bool typedElements =
            read.getObject().getType() == ec::OpaqueType::get(context, kDOMElementViewVectorType);
        auto index = ec::CastOp::create(at, where, ec::OpaqueType::get(context, "std::size_t"),
                                        convertScalar(at, where, read.getKey(), at.getF64Type()));
        mlir::Value value = memberCall(
            at, where,
            domStringVectorIndices.contains(operation) ? mlir::Type(rawString)
            : typedElements ? mlir::Type(ec::OpaqueType::get(context, "ctnative::js_element_t"))
                            : read.getResult().getType(),
            read.getObject(), "at", index.getResult());
        if (typedElements) {
            value = memberCall(at, where, read.getResult().getType(), value, "value");
        }
        if (domElementVectorIndices.contains(operation)) {
            domStyles[value] = domStyles.lookup(read.getObject());
            if (auto style = domStylePointers.lookup(read.getResult())) {
                domStylePointers[value] = style;
            }
        }
        read.getResult().replaceAllUsesWith(
            convertScalar(at, where, value, read.getResult().getType()));
        read.erase();
        return true;
    }
    if (domInvocations.contains(operation)) {
        auto invocation = llvm::cast<ctjs::InvokeOp>(operation);
        auto call = llvm::cast<ctjs::CallOp>(invocation.getBody().front().front());
        if (invocation.getNumResults() == 0) {
            // Complete DOM admission proved this exact setAttribute cannot
            // throw a source exception. Preserve the call at the same guard.
            call->moveBefore(invocation);
            domInvocations.erase(invocation);
            invocation.erase();
            return replaceDOM(call);
        }
        // decodeURIComponent answers std::optional<std::string>; parse_json
        // answers std::expected<json_value, std::size_t>. Both move on success.
        const bool parses = domCalls.find(call)->second.kind == HostDOMMethod::jsonParse;
        const auto fallible =
            parses
                ? ec::OpaqueType::get(context, "std::expected<ctbrowser::json_value, std::size_t>")
                : optionalString;
        const mlir::Type produced =
            parses ? carrierType(context, carrier::json) : carrierType(context, carrier::string);
        auto decoded =
            callValue(at, where, fallible,
                      parses ? "ctbrowser::parse_json" : "ctbrowser::decode_uri_component",
                      rawText(call.getArgs().front()));
        auto present = memberCall(at, where, at.getI1Type(), decoded, "has_value");
        auto branch = mlir::scf::IfOp::create(at, where, invocation.getResultTypes(), present);
        branch.getThenRegion().takeBody(invocation.getNormalBody());
        branch.getElseRegion().takeBody(invocation.getUnwindBody());
        auto & success = branch.getThenRegion().front();
        mlir::OpBuilder inside = mlir::OpBuilder::atBlockBegin(&success);
        // Move only on success. The original input and failure continuation
        // remain untouched; foreign allocation failures never select this else.
        auto value =
            ec::ExpressionOp::create(inside, where, produced, mlir::ValueRange{decoded}, false);
        value.createBody();
        inside.setInsertionPointToStart(&value.getRegion().front());
        auto moved = ec::CallOpaqueOp::create(
            inside, where,
            mlir::TypeRange{ec::OpaqueType::get(
                context, llvm::cast<ec::OpaqueType>(fallible).getValue().str() + " &&")},
            inside.getStringAttr("std::move"),
            mlir::ValueRange{value.getRegion().front().getArgument(0)});
        auto extracted = memberCall(inside, where, parses ? produced : mlir::Type(rawString),
                                    moved.getResult(0), "value");
        ec::YieldOp::create(inside, where, convertScalar(inside, where, extracted, produced));
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
        if (value.getType() != operation->getResult(0).getType()) {
            value = convertScalar(at, where, value, operation->getResult(0).getType());
        }
        operation->getResult(0).replaceAllUsesWith(value);
        eraseIfUnused(operation);
    };
    const auto null = [&] {
        return ec::ConstantOp::create(at, where, optionalString,
                                      ec::OpaqueAttr::get(context, "std::nullopt"));
    };
    const auto jsonOwner = ec::LValueType::get(carrierType(context, carrier::json));
    if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(operation);
        object && object.getResult().getType() == jsonOwner) {
        swap(ec::VariableOp::create(
            at, where, object.getResult().getType(),
            ec::OpaqueAttr::get(context,
                                "ctbrowser::json_value{ctbrowser::json_value::object{}}")));
        return true;
    }
    if (auto copy = llvm::dyn_cast<ctjs::CopyPropsOp>(operation);
        copy && copy.getTarget().getType() == jsonOwner) {
        ec::CallOpaqueOp::create(at, where, mlir::TypeRange{},
                                 at.getStringAttr("ctnative::copy_json_properties"),
                                 mlir::ValueRange{copy.getTarget(), copy.getSource()});
        copy.erase();
        return true;
    }
    if (auto write = llvm::dyn_cast<ctjs::SetPropertyOp>(operation);
        write && write.getObject().getType() == jsonOwner) {
        ec::CallOpaqueOp::create(
            at, where, mlir::TypeRange{},
            at.getStringAttr(domSnapshotAssignments.contains(operation)
                                 ? "ctnative::assign_json_snapshot_property"
                                 : "ctnative::set_json_property"),
            mlir::ValueRange{
                write.getObject(), rawText(write.getKey()),
                convertScalar(at, where, write.getValue(), carrierType(context, carrier::json))});
        write.erase();
        return true;
    }
    if (domNulls.contains(operation)) {
        swap(null());
        return true;
    }
    if (auto compare = llvm::dyn_cast<ctjs::CompareOp>(operation);
        compare && compare.getKind() == ctjs::CompareKind::StrictEq &&
        (compare.getLhs().getType() == optionalString ||
         compare.getRhs().getType() == optionalString)) {
        swap(ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::eq,
                               rawText(compare.getLhs()), rawText(compare.getRhs())));
        return true;
    }
    auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
    if (unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
        unary.getOperand().getType() == jsonOwner) {
        // Fresh spread targets stay objects throughout their proved writes.
        swap(stringConstant(at, where, "object"));
        return true;
    }
    if (unary && unary.getKind() == ctjs::UnaryKind::TypeOf &&
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
            auto holds = callValue(inside, where, inside.getI1Type(),
                                   std::string("std::holds_alternative<") + type + ">",
                                   expression.getRegion().front().getArgument(0));
            result = ec::ConditionalOp::create(inside, where, stringType, holds,
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
    if (tested && tested.getType() == carrierType(context, carrier::domElement)) {
        auto empty = ec::ConstantOp::create(
            at, where, tested.getType(), ec::OpaqueAttr::get(context, "ctbrowser::element_ref{}"));
        swap(ec::CmpOp::create(at, where, at.getI1Type(),
                               truth ? ec::CmpPredicate::ne : ec::CmpPredicate::eq, tested, empty));
        return true;
    }
    if (tested && tested.getType() == optionalString) {
        // Presence alone is not JavaScript truthiness: an empty attribute is false.
        auto present =
            ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::ne, tested, null());
        auto nonempty = ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::ne, tested,
                                          rawText(stringConstant(at, where, "")));
        auto truthy = ec::LogicalAndOp::create(at, where, at.getI1Type(), present, nonempty);
        swap(truth ? mlir::Value(truthy)
                   : ec::LogicalNotOp::create(at, where, at.getI1Type(), truthy).getResult());
        return true;
    }
    const auto found = domCalls.find(operation);
    if (found == domCalls.end()) { return false; }
    auto call = llvm::cast<ctjs::CallOp>(operation);
    if (domInvocations.contains(call->getParentOp())) {
        return true; // The enclosing invocation consumes its original call.
    }
    const auto & edge = found->second;
    // The host proof admits Number/Boolean/null literals, undefined and
    // one arithmetic operation on Number literals, including produced NaN.
    const auto numeric = [](mlir::Value argument) {
        return isNumberCarrier(argument.getType()) || argument.getType().isF64() ||
               isBooleanCarrier(argument.getType()) || argument.getType().isInteger(1);
    };
    const auto indexType = ec::OpaqueType::get(context, "std::size_t");
    const auto sizeConstant = [&](llvm::StringRef value) -> mlir::Value {
        return ec::ConstantOp::create(at, where, indexType, ec::OpaqueAttr::get(context, value));
    };
    if (edge.kind == HostDOMMethod::number && numeric(call.getArgs().front())) {
        swap(number(at, where, call.getArgs().front()));
        return true;
    }
    if (edge.kind == HostDOMMethod::symbol) {
        llvm::SmallVector<mlir::Value, 1> description;
        if (!call.getArgs().empty() &&
            call.getArgs()[0].getType() == carrierType(context, carrier::string)) {
            description.push_back(call.getArgs()[0]);
        }
        swap(callValue(at, where, carrierType(context, carrier::symbol), "ctnative::Symbol",
                       description));
        return true;
    }
    if (edge.kind == HostDOMMethod::symbolToString || edge.kind == HostDOMMethod::symbolValueOf) {
        const bool text = edge.kind == HostDOMMethod::symbolToString;
        swap(memberCall(at, where, carrierType(context, text ? carrier::string : carrier::symbol),
                        call.getReceiver(), text ? "toString" : "valueOf"));
        return true;
    }
    if (edge.kind == HostDOMMethod::decodeURIComponent || edge.kind == HostDOMMethod::jsonParse) {
        return true;
    }
    if (edge.kind == HostDOMMethod::startsWith) {
        swap(memberCall(at, where, at.getI1Type(), call.getReceiver(), "startsWith",
                        call.getArgs()));
        return true;
    }
    if (edge.kind == HostDOMMethod::removeStringPrefix) {
        auto prefix = stringConstant(at, where, "bs");
        auto matches =
            memberCall(at, where, at.getI1Type(), call.getReceiver(), "startsWith", prefix);
        auto zero = sizeConstant("0");
        auto two = sizeConstant("2");
        auto offset = ec::ConditionalOp::create(at, where, indexType, matches, two, zero);
        swap(memberCall(at, where, rawString, rawText(call.getReceiver()), "substr",
                        mlir::ValueRange{offset}));
        return true;
    }
    if (edge.kind == HostDOMMethod::replaceUppercase || edge.kind == HostDOMMethod::filterStrings) {
        const bool replace = edge.kind == HostDOMMethod::replaceUppercase;
        auto callback = edge.callback;
        swap(callValue(at, where,
                       replace ? carrierType(context, carrier::string)
                               : ec::OpaqueType::get(context, kStringVectorType),
                       (replace ? "ctnative::replace_uppercase<" : "ctnative::filter_strings<") +
                           names.lookup(callback.getSymName()) + ">",
                       call.getReceiver()));
        return true;
    }
    if (edge.kind == HostDOMMethod::stringLowercaseUnit) {
        const auto unitsType = ec::OpaqueType::get(context, "std::u16string");
        auto units = callValue(at, where, unitsType, "ctbrowser::wtf8_to_utf16",
                               rawText(call.getReceiver()));
        auto empty = memberCall(at, where, at.getI1Type(), units, "empty");
        auto branch = mlir::scf::IfOp::create(at, where, mlir::TypeRange{unitsType}, empty, true);
        mlir::OpBuilder inside = mlir::OpBuilder::atBlockBegin(&branch.getThenRegion().front());
        mlir::scf::YieldOp::create(inside, where, units);
        inside.setInsertionPointToStart(&branch.getElseRegion().front());
        auto first =
            memberCall(inside, where, ec::OpaqueType::get(context, "char16_t"), units, "front");
        auto lowered =
            callValue(inside, where, unitsType, "ctbrowser::unicode_lowercase_unit", first);
        mlir::scf::YieldOp::create(inside, where, lowered);
        swap(callValue(at, where, rawString, "ctbrowser::utf16_to_wtf8", branch.getResults()));
        return true;
    }
    if (edge.kind == HostDOMMethod::stringCharAt || edge.kind == HostDOMMethod::stringSlice) {
        const auto unitsType = ec::OpaqueType::get(context, "std::u16string");
        auto units = callValue(at, where, unitsType, "ctbrowser::wtf8_to_utf16",
                               rawText(call.getReceiver()));
        // Null selects zero; undefined retains the default slice end.
        const bool startIndex = !call.getArgs().empty() && numeric(call.getArgs().front());
        const bool endIndex = call.getArgs().size() == 2 && numeric(call.getArgs()[1]);
        const bool nullEnd =
            call.getArgs().size() == 2 && call.getArgs()[1].getType() == optionalString;
        mlir::Value length;
        if (startIndex || endIndex) { length = memberCall(at, where, indexType, units, "size"); }
        const auto offset = [&](mlir::Value argument) -> mlir::Value {
            // Truncate before testing the sign: -0.5 selects index zero, not
            // the end of the String, for both charAt and slice.
            auto truncated = callValue(at, where, at.getF64Type(), "std::trunc",
                                       convertScalar(at, where, argument, at.getF64Type()));
            auto zero = ec::ConstantOp::create(at, where, at.getF64Type(), at.getF64FloatAttr(0.0));
            auto nan = callValue(at, where, at.getI1Type(), "std::isnan", truncated);
            auto number =
                ec::ConditionalOp::create(at, where, at.getF64Type(), nan, zero, truncated);
            auto negative =
                ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::lt, number, zero);
            auto negated = ec::UnaryMinusOp::create(at, where, at.getF64Type(), number);
            auto magnitude =
                ec::ConditionalOp::create(at, where, at.getF64Type(), negative, negated, number);
            // Guard the cast itself: infinity and a Number at/above size_t's
            // limit cannot become an unsigned index. Keep length comparisons
            // in size_t so rounding a long String's length cannot move its end.
            auto maximum = sizeConstant("std::numeric_limits<std::size_t>::max()");
            auto representable =
                ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::lt, magnitude,
                                  ec::CastOp::create(at, where, at.getF64Type(), maximum));
            auto branch =
                mlir::scf::IfOp::create(at, where, mlir::TypeRange{indexType}, representable, true);
            auto inside = mlir::OpBuilder::atBlockBegin(&branch.getThenRegion().front());
            auto index = ec::CastOp::create(inside, where, indexType, magnitude);
            auto inRange = ec::CmpOp::create(inside, where, inside.getI1Type(),
                                             ec::CmpPredicate::lt, index, length);
            auto bounded =
                ec::ConditionalOp::create(inside, where, indexType, inRange, index, length);
            mlir::scf::YieldOp::create(inside, where, mlir::ValueRange{bounded});
            inside.setInsertionPointToStart(&branch.getElseRegion().front());
            mlir::scf::YieldOp::create(inside, where, mlir::ValueRange{length});
            auto clamped = branch.getResult(0);
            if (edge.kind == HostDOMMethod::stringCharAt) {
                return ec::ConditionalOp::create(at, where, indexType, negative, length, clamped);
            }
            // Subtract only the clamped magnitude; neither the String length
            // nor a negative source Number is converted to a signed index.
            auto fromEnd = ec::SubOp::create(at, where, indexType, length, clamped);
            return ec::ConditionalOp::create(at, where, indexType, negative, fromEnd, clamped);
        };
        llvm::SmallVector<mlir::Value> range{!startIndex ? sizeConstant("0")
                                                         : offset(call.getArgs().front())};
        if (edge.kind == HostDOMMethod::stringCharAt) {
            range.push_back(sizeConstant("1"));
        } else if (nullEnd) {
            range.push_back(sizeConstant("0"));
        } else if (endIndex) {
            auto end = offset(call.getArgs()[1]);
            auto reversed = ec::CmpOp::create(at, where, at.getI1Type(), ec::CmpPredicate::lt, end,
                                              range.front());
            auto limit =
                ec::ConditionalOp::create(at, where, indexType, reversed, range.front(), end);
            range.push_back(ec::SubOp::create(at, where, indexType, limit, range.front()));
        }
        auto part = memberCall(at, where, unitsType, units, "substr", range);
        swap(callValue(at, where, rawString, "ctbrowser::utf16_to_wtf8", part));
        return true;
    }
    llvm::SmallVector<mlir::Value> arguments;
    if (edge.kind == HostDOMMethod::number) {
        // Earlier replacements update the live call operands. The source
        // proof's original input may already have been erased.
        llvm::append_range(arguments, call.getArgs());
    } else if (edge.kind == HostDOMMethod::numberToString) {
        arguments.push_back(convertScalar(at, where, call.getReceiver(), at.getF64Type()));
    } else {
        // Earlier selector replacements update operands and erase the original
        // producer. Read the live receiver instead of its cached source value.
        mlir::Value element = edge.explicitReceiver ? call.getArgs().front() : call.getReceiver();
        if (edge.kind == HostDOMMethod::toggleClass || edge.kind == HostDOMMethod::containsClass ||
            edge.kind == HostDOMMethod::addClass || edge.kind == HostDOMMethod::removeClass) {
            element = element.getDefiningOp<ctjs::GetPropertyOp>().getObject();
        } else if (edge.kind == HostDOMMethod::datasetKeys) {
            element = call.getArgs().front().getDefiningOp<ctjs::GetPropertyOp>().getObject();
        }
        arguments.push_back(element);
        if (edge.usesStyle()) {
            auto style = domStyles.lookup(edge.styleParameter);
            if (!style) {
                auto pointer = domStylePointers.lookup(element);
                assert(pointer && "proved mixed receiver must retain its Style pointer");
                style = ec::DereferenceOp::create(
                    at, where,
                    ec::LValueType::get(ec::OpaqueType::get(context, "ctbrowser::style::engine")),
                    pointer);
            }
            arguments.push_back(style);
        }
        if (edge.kind != HostDOMMethod::datasetKeys) {
            llvm::append_range(arguments, call.getArgs().drop_front(edge.explicitReceiver ? 1 : 0));
        }
    }
    if (edge.kind == HostDOMMethod::matches || edge.returnsElement() ||
        edge.returnsElementVector()) {
        auto receiver =
            callValue(at, where, ec::OpaqueType::get(context, "ctnative::js_element_t"),
                      "ctnative::js_element_t", mlir::ValueRange{arguments[0], arguments[1]});
        domStyles[receiver] = arguments[1];
        arguments.erase(arguments.begin(), arguments.begin() + 2);
        arguments.insert(arguments.begin(), receiver);
    } else {
        if (edge.kind == HostDOMMethod::setAttribute &&
            isNullableCarrier(arguments.back().getType())) {
            arguments.back() =
                convertScalar(at, where, arguments.back(), carrierType(context, carrier::string));
        }
        for (mlir::Value & argument : arguments) { argument = rawText(argument); }
    }
    static const llvm::DenseMap<HostDOMMethod, llvm::StringRef> callees{
        {HostDOMMethod::datasetKeys, "ctnative::dataset_keys"},
        {HostDOMMethod::toggleClass, "ctnative::toggle_class"},
        {HostDOMMethod::containsClass, "ctnative::contains_class"},
        {HostDOMMethod::addClass, "ctnative::add_class"},
        {HostDOMMethod::removeClass, "ctnative::remove_class"},
        {HostDOMMethod::setAttribute, "ctnative::set_attribute"},
        {HostDOMMethod::getAttribute, "ctnative::get_attribute"},
        {HostDOMMethod::toggleAttribute, "ctnative::toggle_attribute"},
        {HostDOMMethod::hasAttribute, "ctnative::has_attribute"},
        {HostDOMMethod::removeAttribute, "ctnative::remove_attribute"},
        {HostDOMMethod::contains, "ctnative::contains"},
        {HostDOMMethod::matches, "ctnative::Element.prototype.matches.call"},
        {HostDOMMethod::closest, "ctnative::Element.prototype.closest.call"},
        {HostDOMMethod::querySelector, "ctnative::Element.prototype.querySelector.call"},
        {HostDOMMethod::querySelectorAll, "ctnative::Element.prototype.querySelectorAll.call"},
        {HostDOMMethod::number, "ctbrowser::string_to_number"},
        {HostDOMMethod::numberToString, "ctbrowser::number_to_string"},
    };
    const auto calleeName = callees.find(edge.kind);
    if (calleeName == callees.end()) {
        llvm_unreachable("DOM method requires specialized lowering");
    }
    llvm::StringRef callee = calleeName->second;
    if (edge.kind == HostDOMMethod::setAttribute && call.getArgs()[1].getType() == optionalString) {
        callee = "ctnative::set_optional_attribute";
    } else if (edge.kind == HostDOMMethod::number &&
               arguments.front().getType() == optionalString) {
        callee = "ctnative::dom_number";
    }
    if (edge.returnsBoolean() || edge.returnsElement() || edge.returnsOptionalString() ||
        edge.returnsNumber() || edge.returnsString() || edge.returnsStringVector() ||
        edge.returnsElementVector()) {
        const mlir::Type type =
            edge.returnsOptionalString()  ? ec::OpaqueType::get(context, kDOMOptionalStringType)
            : edge.returnsStringVector()  ? ec::OpaqueType::get(context, kStringVectorType)
            : edge.returnsElementVector() ? ec::OpaqueType::get(context, kDOMElementViewVectorType)
            : edge.returnsElement()       ? carrierType(context, carrier::domElement)
            : edge.returnsNumber()        ? carrierType(context, carrier::number)
            : edge.returnsString()        ? carrierType(context, carrier::string)
                                          : carrierType(context, carrier::boolean);
        const mlir::Type resultType =
            edge.returnsElement()
                ? ec::OpaqueType::get(context, "std::optional<ctnative::js_element_t>")
            : callee == "ctbrowser::string_to_number" ? mlir::Type(at.getF64Type())
            : edge.returnsString()                    ? mlir::Type(rawString)
                                                      : type;
        mlir::Value value = callValue(at, call.getLoc(), resultType, callee, arguments);
        if (edge.returnsElement()) {
            value = callValue(at, where, type, "ctnative::element_or_null", value);
        }
        if (edge.returnsElement() || edge.returnsElementVector()) {
            // Every chain begins with a parameter's selector call, which already
            // requires that parameter's engine in the native signature.
            domStyles[value] = domStyles.lookup(arguments.front());
            if (auto style = domStylePointers.lookup(call.getResult())) {
                domStylePointers[value] = style;
            }
        }
        call.getResult().replaceAllUsesWith(convertScalar(at, where, value, type));
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
