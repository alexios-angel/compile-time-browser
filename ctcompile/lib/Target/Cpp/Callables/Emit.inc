// Included after the shared function/body printers. Each lambda reuses those
// printers with independent state, without rewriting previously emitted text.

// The parameters after the captures, the trailing return type and the brace
// that opens a callable's lambda.
static LogicalResult printLambdaSignature(CppEmitter & emitter, emitc::FuncOp function,
                                          unsigned captures) {
    auto & os = emitter.ostream();
    if (failed(interleaveCommaWithError(
            function.getArguments().drop_front(captures), os, [&](BlockArgument argument) {
                return emitter.emitVariableDeclaration(function.getLoc(), argument.getType(),
                                                       emitter.getOrCreateName(argument),
                                                       emitter.isConstBinding(argument));
            })))
        return failure();
    os << ") -> ";
    if (failed(emitter.emitTypes(function.getLoc(), function.getFunctionType().getResults())))
        return failure();
    os << " {\n";
    return success();
}

static LogicalResult printCallable(CppEmitter & emitter, emitc::FuncOp function,
                                   const ctcompile::cpp::CallableBody & body, bool declaration) {
    if (!declaration && !emitter.shouldDeclareVariablesAtTop() && function.getBlocks().size() > 1)
        return function.emitOpError("with multiple blocks needs variables declared at top");
    CppEmitter::FunctionScope scope(emitter);
    emitter.prepareFunction(function, body.parameters);
    auto & os = emitter.ostream();
    const auto arguments = function.getArguments();
    os << body.type << " " << body.binder << "(";
    for (unsigned i = 0; i < body.captures; ++i) {
        if (i) os << ", ";
        if (failed(emitter.emitVariableDeclaration(function.getLoc(), arguments[i].getType(),
                                                   emitter.getOrCreateName(arguments[i]))))
            return failure();
    }
    os << ")";
    if (declaration) {
        os << ";";
        return success();
    }
    os << " {\n";
    os.indent();
    os << body.type << " const " << body.name << " = [";
    for (unsigned i = 0; i < body.captures; ++i) {
        if (i) os << ", ";
        auto name = emitter.getOrCreateName(arguments[i]);
        os << name << " = std::move(" << name << ")";
    }
    os << "](";
    if (failed(printLambdaSignature(emitter, function, body.captures))) return failure();
    if (body.inlineBody) {
        if (failed(printFunctionBody(emitter, function, function.getBlocks()))) return failure();
    } else {
        os.indent();
        os << "return " << function.getSymName() << "(";
        llvm::interleaveComma(arguments, os,
                              [&](Value argument) { os << emitter.getOrCreateName(argument); });
        os << ");\n";
        os.unindent();
    }
    os << "};\nreturn " << body.name << ";\n";
    os.unindent();
    os << "}";
    return success();
}

static LogicalResult printOperation(CppEmitter & emitter, emitc::FuncOp function) {
    auto body = ctcompile::cpp::callableBodyPlan(function);
    if (failed(body)) return failure();
    if (!body->emitsBinder()) return printRegularFunction(emitter, function);
    if (body->emitsFunction()) {
        if (failed(printRegularFunction(emitter, function))) return failure();
        emitter.ostream() << "\n// ctcompile: stored callable, ";
        if (auto provenance = function->getAttrOfType<StringAttr>("ctnative.provenance"))
            emitter.ostream() << provenance.getValue();
        else
            emitter.ostream() << function.getSymName();
        emitter.ostream() << "\n";
    }
    return printCallable(emitter, function, *body, false);
}

static FailureOr<bool> omitCallableDefinition(Operation & op) {
    auto function = dyn_cast<emitc::FuncOp>(&op);
    if (auto declaration = dyn_cast<emitc::DeclareFuncOp>(&op))
        function = SymbolTable::lookupNearestSymbolFrom<emitc::FuncOp>(
            declaration, declaration.getSymNameAttr());
    if (!function || !function->hasAttr("ctnative.callable_body")) return false;
    auto body = ctcompile::cpp::callableBodyPlan(function);
    if (failed(body)) return failure();
    return !body->emitsBinder() && !body->emitsFunction();
}

static bool materializedCapture(Value value) {
    // Expression arguments print their captured operand, not a declaration.
    while (auto argument = dyn_cast<BlockArgument>(value)) {
        auto expression = dyn_cast<emitc::ExpressionOp>(argument.getOwner()->getParentOp());
        if (!expression) return true;
        value = expression->getOperand(argument.getArgNumber());
    }
    auto * definition = value.getDefiningOp();
    return !hasDeferredEmission(definition) && !shouldBeInlined(definition);
}

static FailureOr<bool> printCallableCreation(CppEmitter & emitter, emitc::CallOpaqueOp call) {
    auto target = ctcompile::cpp::callableCreationTarget(call);
    if (failed(target)) return failure();
    if (!*target) return false;
    auto body = ctcompile::cpp::callableBodyPlan(*target);
    if (failed(body)) return failure();
    if (!body->inlineCreation) return false;
    if (!emitter.shouldDeclareVariablesAtTop() && target->getBlocks().size() > 1)
        return call.emitOpError("callable with multiple blocks needs variables declared at top");

    auto & os = emitter.ostream();
    if (!emitter.isEmittingExpression()) {
        if (auto source = (*target)->getAttrOfType<StringAttr>("ctnative.provenance"))
            os << "// ctcompile: " << source.getValue() << "\n";
    }
    if (failed(emitter.emitAssignPrefix(*call))) return failure();
    // auto and expression operands must retain std::function's concrete ABI;
    // a discarded construction must still perform that owning conversion.
    const bool wrap = emitter.isEmittingExpression() || call->hasAttr("ctnative.deduced") ||
                      call->hasAttr("ctnative.statement");
    if (wrap) os << body->type << "{";
    os << "[";
    for (auto [i, capture] : llvm::enumerate(call->getOperands())) {
        if (i) os << ", ";
        // Copy the actual source value. Moving a surrounding binding could
        // invalidate a live alias or the next creation of this same closure.
        os << body->parameters[i] << " = ";
        const bool convert = !materializedCapture(capture);
        if (convert) {
            // Init-capture deduces from C++ spelling: an f64 literal spelled
            // `1` would otherwise capture int. Keep the binder parameter's
            // implicit conversion, including class copy-initialization.
            os << "[](";
            if (failed(emitter.emitType(call.getLoc(), capture.getType()))) return failure();
            os << " ctn_capture) { return ctn_capture; }(";
        }
        if (failed(emitter.emitOperand(capture, true))) return failure();
        if (convert) os << ")";
    }
    os << "](";
    CppEmitter nested(os, emitter);
    CppEmitter::FunctionScope scope(nested);
    nested.prepareFunction(*target, body->parameters);
    auto & lambda = nested.ostream();
    if (failed(printLambdaSignature(nested, *target, body->captures))) return failure();
    if (failed(printFunctionBody(nested, *target, target->getBlocks()))) return failure();
    lambda << "}";
    if (wrap) lambda << "}";
    emitter.continueValueCounter(nested);
    return true;
}
