//===- Main.cpp - Exception-recovery test driver ----------------*- C++ -*-===//
//
// Entry point of ctcompile-test-exception-recovery. It takes a lit file whose
// "//--- NAME.js" sections hold the JavaScript fixtures and runs every test in
// Completion.cpp, Guards.cpp and the Completion/ directory on them.
//
//===----------------------------------------------------------------------===//

#include "Tests.h"

#include "check.hpp"
#include "options.hpp"

using namespace ctcompile::test::exception_recovery;

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string fixture;
    po::options_description options{
        "usage: ctcompile-test-exception-recovery invocation-state.mlir"};
    options.add_options()("fixture", po::value(&fixture)->required(),
                          "the lit file whose //--- NAME.js sections are the sources");
    po::positional_options_description positional;
    positional.add("fixture", 1);
    if (!ctbrowser_cli::parse_options(argc, argv, options, positional)) { return 1; }
    auto file = llvm::MemoryBuffer::getFile(fixture);
    if (!file) { return 1; }
    mlir::MLIRContext context;
    context.loadDialect<ctjs::CTJSDialect, mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                        mlir::scf::SCFDialect, mlir::ub::UBDialect>();
    const auto source = [&](llvm::StringRef name) {
        auto tail = (*file)->getBuffer().split(("//--- " + name + ".js\n").str()).second;
        return tail.split("//--- ").first;
    };
    // Each testSource call takes the fixture name, its source, the number of
    // calls in the try block, the value `mark` holds when the throw lands, and
    // the thrown payload.
    for (auto mode : {ExceptionRecoveryMode::CheckedInvocations,
                      ExceptionRecoveryMode::EffectCheckedInvocations}) {
        testSource(context, "assignment", source("assignment"), 1, 10, 32, mode);
        testSource(context, "sequential", source("sequential"), 2, 20, 32, mode);
        testSource(context, "argument", source("argument"), 1, 14, 14, mode);
    }
    testSource(context, "ordinary assignment", source("assignment"), 1, 10, 32,
               ExceptionRecoveryMode::CheckedInvocations, false);
    testSource(context, "ordinary sequential", source("sequential"), 2, 20, 32,
               ExceptionRecoveryMode::CheckedInvocations, false);
    testSource(context, "ordinary argument", source("argument"), 1, 14, 14,
               ExceptionRecoveryMode::CheckedInvocations, false);
    testMutations(context, source("assignment"));
    testMutations(context, source("assignment"), false);
    testOrdinaryCompletionTypes(context);
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
    testGuardedTail(context);
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
