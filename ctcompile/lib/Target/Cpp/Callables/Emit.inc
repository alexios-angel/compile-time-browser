// Included after the shared function/body printers. Keeping this adapter here
// avoids a second emitter or textual rewriting of a previously emitted body.
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
    for (unsigned i = body.captures; i < arguments.size(); ++i) {
        if (i != body.captures) os << ", ";
        if (failed(emitter.emitVariableDeclaration(function.getLoc(), arguments[i].getType(),
                                                   emitter.getOrCreateName(arguments[i]),
                                                   emitter.isConstBinding(arguments[i]))))
            return failure();
    }
    os << ") -> ";
    if (failed(emitter.emitTypes(function.getLoc(), function.getFunctionType().getResults())))
        return failure();
    os << " {\n";
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
    if (body->binder.empty()) return printRegularFunction(emitter, function);
    if (body->retainFunction) {
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
