#include "Tests.h"

using namespace ctcompile::test::exception_recovery;

int main(int argc, char ** argv) {
    if (argc != 2) {
        std::fputs("usage: ctcompile-test-exception-recovery invocation-state.mlir\n", stderr);
        return 1;
    }
    auto file = llvm::MemoryBuffer::getFile(argv[1]);
    if (!file) { return 1; }
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    const auto source = [&](llvm::StringRef name) {
        auto tail = (*file)->getBuffer().split(("//--- " + name + ".js\n").str()).second;
        return tail.split("//--- ").first;
    };
    for (auto mode : {ExceptionRecoveryMode::CheckedInvocations,
                      ExceptionRecoveryMode::EffectCheckedInvocations}) {
        testSource(context, "assignment", source("assignment"), 1, 10, 32, mode);
        testSource(context, "sequential", source("sequential"), 2, 20, 32, mode);
        testSource(context, "argument", source("argument"), 1, 14, 14, mode);
    }
    testMutations(context, source("assignment"));
    testBindings(context, source("assignment"));
    for (auto name : {"assignment", "sequential", "argument"}) {
        testSourceCompletionTypes(context, name, source(name), "!ctnative.num<i32>",
                                  "!ctnative.num<i32>");
    }
    testSourceCompletionTypes(context, "string", R"js(
function choose(flag) {
    if (flag) { throw "payload"; }
    return "normal";
}
function guarded(flag) {
    var mark = "entry";
    try { mark = "saved"; mark = choose(flag); }
    catch (value) { return mark + value; }
    return mark;
}
var caught = guarded(true);
var normal = guarded(false);
)js",
                              "!ctnative.str<utf8>", "!ctnative.str<utf8>");
    testSourceCompletionTypes(context, "boolean", R"js(
function choose(flag) {
    if (flag) { throw true; }
    return false;
}
function guarded(flag) {
    var mark = false;
    try { mark = true; mark = choose(flag); }
    catch (value) { return mark === value; }
    return mark;
}
var caught = guarded(true);
var normal = guarded(false);
)js",
                              "!ctnative.bool", "!ctnative.bool");
    testSourceCompletionMutations(context, source("assignment"));
    for (auto [name, calls, saved, payload, parameters] :
         {std::tuple{"assignment", 1u, 10.0, 32.0, false},
          std::tuple{"sequential", 2u, 20.0, 32.0, false},
          std::tuple{"argument", 1u, 14.0, 14.0, true}}) {
        testSource(context, ("transitive " + std::string(name)),
                   nestedSource(source(name), 3, parameters), calls, saved, payload,
                   ExceptionRecoveryMode::EffectCheckedInvocations);
    }
    testTransitive(context, source("argument"));
    testSourceCompletionTypes(context, "transitive callee lookup",
                              nestedSource(source("assignment"), 3, false), "!ctnative.boxed",
                              "!ctnative.num<i32>");
    testSelectedActuals(context);
    testEffects(context);
    return failures == 0 ? 0 : 1;
}
