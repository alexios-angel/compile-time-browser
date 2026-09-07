// Typed exception regions share the enclosing function's naming and binding
// analyses, with separate declaration scopes for the try and catch bodies.
static LogicalResult printOperation(CppEmitter & emitter, ctcompile::ctnative::CppTryOp op) {
    auto & os = emitter.ostream();
    const std::string exception = emitter.getTemporaryName("ctn_exception_");
    os << "try {\n";
    os.indent();
    {
        CppEmitter::ExceptionScope scope(emitter);
        for (Operation & operation : op.getBody().front()) {
            if (failed(emitter.emitOperation(operation, /*trailingSemicolon=*/true)))
                return failure();
        }
    }
    os.unindent() << "} catch (ctnative::js_exception const & " << exception << ") {\n";
    os.indent();
    {
        CppEmitter::ExceptionScope scope(emitter);
        const BlockArgument caught = op.getCatchBody().front().getArgument(0);
        // Copy the JS payload out of the C++ exception object. Source writes
        // target explicit mutable slots, never this immutable SSA binding.
        if (failed(emitter.emitVariableDeclaration(op.getLoc(), caught.getType(),
                                                   emitter.getOrCreateName(caught),
                                                   emitter.isConstBinding(caught))))
            return failure();
        os << " = " << exception << ".value;\n";
        if (caught.use_empty())
            os << "static_cast<void>(" << emitter.getOrCreateName(caught) << ");\n";
        for (Operation & operation : op.getCatchBody().front()) {
            if (failed(emitter.emitOperation(operation, /*trailingSemicolon=*/true)))
                return failure();
        }
    }
    os.unindent() << "}";
    return success();
}

static LogicalResult printOperation(CppEmitter & emitter, ctcompile::ctnative::CppThrowOp op) {
    emitter.ostream() << "throw ctnative::js_exception{";
    if (failed(emitter.emitOperand(op.getValue()))) return failure();
    emitter.ostream() << "}";
    return success();
}
