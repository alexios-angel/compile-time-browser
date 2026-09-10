// PHASE 55A'S TABLE, ONE CELL AT A TIME - the post-pass rows: control flow
// and completion, the dead/unvisited accounting, R1, R4, the closure hole,
// the DEFAULT RULE, and region captures.
//
// One of four executables carved out of a 2,763-line test/EscapeAnalysis.cpp on
// 2026-09-08. The row harness they share - `row`, `kPrologue`, `check`, the
// role and verdict printers - is EscapeAnalysisHarness.h beside this; the rows
// themselves are verbatim, in their original order, and every one is still
// run. Registered one target each in test/cmake/Analysis.cmake.

#include "EscapeAnalysisHarness.h"

using namespace ctcompile::test::escape;

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<CTNativeDialect>();
    // For the DEFAULT-RULE rows: an operation from no dialect at all has no
    // interface and is not a branch, which is exactly the case the rule is for.
    context.allowUnregisteredDialects();

    const std::string S = "  %s = ctjs.create_object {check}\n";
    const std::string A = "  %s = ctjs.create_array [] {check}\n";
    const std::string R = "  ctjs.return %p\n";

    const std::vector<row> rows = {
        // ===================================================================
        // CONTROL FLOW AND COMPLETION - the post-pass rows
        // ===================================================================
        {.what = "return SINK(returned) - the result-less terminator the post-pass exists for",
         .body = S + "  ctjs.return %s\n",
         .expected = "escapes:returned",
         .roles = "ctjs.return sink:returned"},
        {.what = "returning a parameter sinks no site",
         .body = S + R,
         .expected = "confined",
         .roles = "ctjs.return sink:returned"},
        {.what = "throw SINK(thrown) - thrown_ is a root (o.cpp:705)",
         .body = S + "  ctjs.throw %s\n",
         .expected = "escapes:thrown",
         .roles = "ctjs.throw sink:thrown"},
        {.what = "wrap_promise SINK(stored) - the promise's __value (internal.hpp:262, 342)",
         .body = S + "  %w = ctjs.wrap_promise %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.wrap_promise sink:stored"},
        {.what = "module_export_cell: $current SINK(stored) - alias-returning, c.cpp:240",
         .body = S + "  %e = ctjs.module_export_cell \"n\" adopting %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.module_export_cell sink:stored"},
        {.what = "root NEITHER - parks into this frame's own window",
         .body = S +
                 "  %ctx = ctjs.frame_enter 4\n  ctjs.root %s in %ctx\n  ctjs.frame_exit %ctx\n" +
                 R,
         .expected = "confined",
         .roles = "ctjs.root neither neither"},
        {.what = "frame_exit has no tracked operand",
         .body = S + "  %ctx = ctjs.frame_enter 4\n  ctjs.frame_exit %ctx\n" + R,
         .expected = "confined",
         .roles = "ctjs.frame_exit neither"},
        {.what = "catch_land's thrown value is external",
         .body = S +
                 "  ctjs.push_handler ^body catch ^pad\n"
                 "^body:\n  ctjs.pop_handler\n" +
                 R +
                 "^pad:\n  %id, %e = ctjs.catch_land {check}\n"
                 "  ctjs.return %e\n",
         .expected = "{external}",
         .alias = true},

        // --- THE LOOP ROW: a site carried round a back edge through block
        // arguments stays confined; the same site returned from inside the
        // loop's exit is returned. Both depend on DeadCodeAnalysis and
        // SparseConstantPropagation being loaded, as TypeClaims.cpp records.
        {.what = "a site carried round a loop through block arguments stays confined",
         .body = S +
                 "  cf.br ^loop(%s : !ctjs.value)\n"
                 "^loop(%x: !ctjs.value):\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop(%x : !ctjs.value), ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "confined",
         .roles = "~cf.br carry"},
        {.what = "the same site returned through the loop's block argument is returned",
         .body = S + "  cf.br ^loop(%s : !ctjs.value)\n"
                     "^loop(%x: !ctjs.value):\n"
                     "  %t = ctjs.truthy %p\n"
                     "  cf.cond_br %t, ^loop(%x : !ctjs.value), ^exit\n"
                     "^exit:\n"
                     "  ctjs.return %x\n",
         .expected = "escapes:returned"},
        {.what = "a block argument joins two sites: sinking it sinks both",
         .body = "  %o = ctjs.create_object\n" + S +
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%o : !ctjs.value)\n"
                 "^join(%x: !ctjs.value):\n"
                 "  ctjs.store_global \"g\", %x\n" +
                 R,
         .expected = "escapes:stored_global"},
        // A global owner is separate from confinement. Joining a local site
        // with an external value must retain BOTH facts: publication still
        // sinks the site, and the joined value cannot become local-only.
        {.what = "an external alternative does not erase a local site's global escape",
         .body = S +
                 "  %g = ctjs.load_global \"g\"\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%g : !ctjs.value)\n"
                 "^join(%x: !ctjs.value):\n"
                 "  ctjs.store_global \"published\", %x\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "a fresh/global join retains both the local site and external identity",
         .body = "  %s = ctjs.create_object\n"
                 "  %g = ctjs.load_global \"g\"\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%g : !ctjs.value)\n"
                 "^join(%x: !ctjs.value):\n"
                 "  %joined = ctjs.truthy %x {check}\n" +
                 R,
         .expected = "{ctjs.create_object, external}",
         .aliasOperand = true},
        {.what = "an external back edge cannot erase a loop-carried local's global escape",
         .body = S +
                 "  %g = ctjs.load_global \"g\"\n"
                 "  cf.br ^loop(%s : !ctjs.value)\n"
                 "^loop(%x: !ctjs.value):\n"
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^loop(%g : !ctjs.value), ^exit\n"
                 "^exit:\n"
                 "  ctjs.store_global \"published\", %x\n" +
                 R,
         .expected = "escapes:stored_global"},

        // --- THE ctjs.check HANDLER-OPERAND ROW: the handler edge carries a
        // register snapshot into the pad's block arguments, and a site in it
        // returned from the pad is returned.
        // push_handler must pass the pad's argument too (the verifier checks
        // arity); it passes a PARAMETER, so only ctjs.check's edge carries the
        // site - which is what the row isolates.
        {.what = "ctjs.check's handler operands carry into the pad (BranchOpInterface)",
         .body = S +
                 "  ctjs.push_handler ^body catch ^pad(%p : !ctjs.value)\n"
                 "^body:\n"
                 "  %r = ctjs.call %p(%q)\n"
                 "  ctjs.check ^cont caught ^pad(%s : !ctjs.value)\n"
                 "^cont:\n"
                 "  ctjs.pop_handler\n" +
                 R +
                 "^pad(%e: !ctjs.value):\n"
                 "  %id, %thrown = ctjs.catch_land\n"
                 "  ctjs.return %e\n",
         .expected = "escapes:returned",
         .roles = "~ctjs.check carry"},
        {.what = "push_handler's body operands carry too",
         .body = S +
                 "  ctjs.push_handler ^body(%s : !ctjs.value) catch ^pad\n"
                 "^body(%b: !ctjs.value):\n"
                 "  ctjs.pop_handler\n"
                 "  ctjs.return %b\n"
                 "^pad:\n"
                 "  %id, %thrown = ctjs.catch_land\n" +
                 R,
         .expected = "escapes:returned",
         .roles = "~ctjs.push_handler carry"},

        // ===================================================================
        // THE ACCOUNTING - dead blocks, unvisited sites, unvisited operands
        // ===================================================================
        {.what = "a site in a dead block is dropped and counted, never claimed",
         .body = S + R + "^dead:\n  %d = ctjs.create_object\n  ctjs.return %d\n",
         .expected = "confined",
         .deadSites = 1},
        {.what = "a sink in a dead block does not fire",
         .body = S + R + "^dead:\n  ctjs.store_global \"g\", %s\n" + R,
         .expected = "confined"},
        {.what = "a site in a LIVE block the solver never visited is unvisited, counted",
         .body = S + "  ctjs.resume_throw\n",
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .withAnalysis = false},
        {.what = "a sink operand with no lattice is counted and refuses every site",
         .body = S + R,
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .unvisitedOperands = 1,
         .withAnalysis = false},
        {.what = "an unvisited operand alone: every site becomes unvisited_operand",
         .body = "  %o = ctjs.create_object {check}\n"
                 "  ctjs.resume_throw\n"
                 "^dead:\n"
                 "  ctjs.return %p\n",
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .withAnalysis = false},

        // ===================================================================
        // R1 - arguments, per-site, with the placement guard
        // ===================================================================
        {.what = "make_arguments in the prologue: sites confined, function flagged",
         .body = "  %a = ctjs.make_arguments\n" + S + R,
         .expected = "confined",
         .capturesAllArguments = true},
        {.what = "make_arguments AFTER a site: the whole function is arguments_late",
         .body = S + "  %a = ctjs.make_arguments\n" + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "gather_rest after a site is arguments_late too",
         .body = S + "  %a = ctjs.gather_rest from 0\n" + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "make_arguments outside the entry block is arguments_late",
         .body = "  cf.br ^b\n^b:\n  %a = ctjs.make_arguments\n" + S + R,
         .expected = "escapes:arguments_late",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "make_arguments in the prologue leaves a site in a later block confined",
         .body = "  %a = ctjs.make_arguments\n  cf.br ^b\n^b:\n" + S + R,
         .expected = "confined",
         .capturesAllArguments = true},

        // ===================================================================
        // R4 - suspend, whole function
        // ===================================================================
        {.what = "suspend refuses the whole function: a site it never touches escapes",
         .body = S + "  %r = ctjs.suspend await %p\n" + R,
         .expected = "escapes:suspended",
         .roles = "ctjs.suspend sink:stored",
         .wholeFunction = "suspended",
         .storageWrites = "ctjs.suspend[0] {external} -> <uninitialized>",
         .completeStorage = false},
        {.what = "suspend's own operand: the refusal is the first reason, not stored",
         .body = S + "  %r = ctjs.suspend yield %s\n" + R,
         .expected = "escapes:suspended",
         .wholeFunction = "suspended",
         .storageWrites = "ctjs.suspend[0] {ctjs.create_object} -> <uninitialized>",
         .completeStorage = false},

        // ===================================================================
        // THE CLOSURE HOLE - no untracked allocation ever enters an alias set
        // ===================================================================
        {.what = "a closure is external, never a site (o.cpp:590 -> c.cpp:502)",
         .body = "  %f = ctjs.create_closure %callee[0] this %p {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "a closure carried through iterable stays external",
         .body = "  %f = ctjs.create_closure %callee[0] this %p\n"
                 "  %i = ctjs.iterable of %f {check}\n" +
                 R,
         .expected = "{external}",
         .alias = true},
        {.what = "a cell is external",
         .body = "  %c = ctjs.create_cell %p {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "own_keys' array is external",
         .body = "  %k = ctjs.own_keys of %p {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "a site carried through iterable is the site plus an external array",
         .body = "  %o = ctjs.create_object\n  %i = ctjs.iterable of %o {check}\n" + R,
         .expected = "{ctjs.create_object, external}",
         .alias = true},
        {.what = "a constant is not an object",
         .body = "  %z = ctjs.constant #ctjs.undefined {check}\n" + R,
         .expected = "{}",
         .alias = true},
        {.what = "a site's own alias set is itself",
         .body = S + R,
         .expected = "{ctjs.create_object}",
         .alias = true},

        // ===================================================================
        // THE DEFAULT RULE - an operation with no annotation sinks
        // ===================================================================
        {.what = "DEFAULT RULE: an unannotated operation sinks its value operand as unknown_op",
         .body = S + "  \"test.unknown\"(%s) : (!ctjs.value) -> ()\n" + R,
         .expected = "escapes:unknown_op",
         .roles = "~test.unknown sink:unknown_op"},
        {.what = "DEFAULT RULE: an unannotated operation's result is external",
         .body = "  %r = \"test.unknown\"(%p) {check} : (!ctjs.value) -> !ctjs.value\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "DEFAULT RULE: a non-value operand is not sunk",
         .body = S + "  %t = ctjs.truthy %s\n  \"test.unknown\"(%t) : (i1) -> ()\n" + R,
         .expected = "confined",
         .roles = "~test.unknown neither"},

        // Region operands do not enumerate implicit SSA captures. The query
        // remains CFG-only, so every such capture is an unknown-op sink even
        // if a nested operation would otherwise have a NEITHER operand role.
        {.what = "a region's implicit capture cannot hide a global store",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    ctjs.store_global \"held\", %s\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op",
         .by = "ctjs.store_global",
         .roles = "~scf.if neither"},
        {.what = "the region capture scan reaches nested regions",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    scf.if %t {\n"
                 "      ctjs.store_global \"held\", %s\n"
                 "    }\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op"},
        {.what = "region capture of an iterable alias sinks the original array",
         .body = A +
                 "  %alias = ctjs.iterable of %s\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    ctjs.store_global \"held\", %alias\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op"},
        {.what = "a nested NEITHER use still requires a region proof",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %truth = ctjs.truthy %s\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op"},
        {.what = "dead nested branches do not grant region semantics",
         .body = S +
                 "  %no = arith.constant false\n"
                 "  scf.if %no {\n"
                 "    ctjs.store_global \"held\", %s\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op"},
        {.what = "an unrelated region and its local values do not sink an outer site",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %local = ctjs.create_object\n"
                 "    ctjs.store_global \"held\", %local\n"
                 "  }\n" +
                 R,
         .expected = "confined"},
        {.what = "a region in a dead CFG block cannot sink its outer capture",
         .body = S +
                 "  cf.br ^exit\n"
                 "^dead:\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    ctjs.store_global \"held\", %s\n"
                 "  }\n"
                 "  cf.br ^exit\n"
                 "^exit:\n" +
                 R,
         .expected = "confined"},
        {.what = "an unregistered region sinks outer captures but not its local block arguments",
         .body = S +
                 "  \"test.region\"() ({\n"
                 "  ^entry(%local: !ctjs.value):\n"
                 "    \"test.use\"(%local) : (!ctjs.value) -> ()\n"
                 "    ctjs.store_global \"held\", %s\n"
                 "    \"test.end\"() : () -> ()\n"
                 "  }) : () -> ()\n" +
                 R,
         .expected = "escapes:unknown_op",
         .by = "ctjs.store_global"},
        {.what = "an outer CFG alias captured by a region still sinks the original site",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  cf.cond_br %t, ^join(%s : !ctjs.value), ^join(%p : !ctjs.value)\n"
                 "^join(%alias: !ctjs.value):\n"
                 "  scf.if %t {\n"
                 "    ctjs.store_global \"held\", %alias\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op"},
        {.what = "an outer site's boolean result carries no object into a nested region",
         .body = S +
                 "  %t = ctjs.truthy %s\n"
                 "  scf.if %t {\n"
                 "    \"test.use\"(%t) : (i1) -> ()\n"
                 "  }\n" +
                 R,
         .expected = "confined"},
        {.what = "a region-local allocation has no verdict even when another region captures it",
         .body = "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %local = ctjs.create_object {check}\n"
                 "    scf.if %t {\n"
                 "      ctjs.store_global \"held\", %local\n"
                 "    }\n"
                 "  }\n" +
                 R,
         .expected = "<no verdict>"},
        {.what = "a region-local array yielded to the CFG has no confinement verdict",
         .body = "  %result = scf.execute_region -> !ctjs.value {\n"
                 "    %local = ctjs.create_array [] {check}\n"
                 "    scf.yield %local : !ctjs.value\n"
                 "  }\n"
                 "  ctjs.store_global \"held\", %result\n" +
                 R,
         .expected = "<no verdict>"},
        {.what = "a missing lattice for a region capture is counted without assuming confinement",
         .body = S + "  %t = ctjs.truthy %p\n"
                     "  scf.if %t {\n"
                     "    ctjs.store_global \"held\", %s\n"
                     "  }\n"
                     "  ctjs.resume_throw\n",
         .expected = "escapes:unvisited",
         .unvisitedSites = 1,
         .unvisitedOperands = 1,
         .withAnalysis = false},
        {.what = "a nested suspension retains outer frame sites without capturing their SSA values",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %r = ctjs.suspend await %p\n"
                 "  }\n" +
                 R,
         .expected = "escapes:suspended",
         .by = "ctjs.suspend",
         .wholeFunction = "suspended"},
        {.what = "a dead nested suspension still requires a region control-flow proof",
         .body = S +
                 "  %no = arith.constant false\n"
                 "  scf.if %no {\n"
                 "    %r = ctjs.suspend yield %p\n"
                 "  }\n" +
                 R,
         .expected = "escapes:suspended",
         .wholeFunction = "suspended"},
        {.what = "a suspension nested in a dead CFG block does not refuse the live frame",
         .body = S + R +
                 "^dead:\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %r = ctjs.suspend await %p\n"
                 "  }\n" +
                 R,
         .expected = "confined"},
        {.what = "a nested arguments builder cannot bypass the raw-frame placement guard",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %a = ctjs.make_arguments\n"
                 "  }\n" +
                 R,
         .expected = "escapes:arguments_late",
         .by = "ctjs.make_arguments",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "a nested rest builder before a site still lacks the required prologue proof",
         .body = "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %a = ctjs.gather_rest from 0\n"
                 "  }\n" +
                 S + R,
         .expected = "escapes:arguments_late",
         .by = "ctjs.gather_rest",
         .capturesAllArguments = true,
         .wholeFunction = "arguments_late"},
        {.what = "an arguments builder nested in a dead CFG block does not mark the live frame",
         .body = S + R +
                 "^dead:\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %a = ctjs.make_arguments\n"
                 "  }\n" +
                 R,
         .expected = "confined"},
        {.what = "a later nested capture preserves the first escape reason",
         .body = S +
                 "  ctjs.store_global \"first\", %s\n"
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    %truth = ctjs.truthy %s\n"
                 "  }\n" +
                 R,
         .expected = "escapes:stored_global",
         .by = "ctjs.store_global"},
        {.what = "a nested capture records its actual sinking operand for diagnostics",
         .body = S +
                 "  %t = ctjs.truthy %p\n"
                 "  scf.if %t {\n"
                 "    ctjs.set_property %p[%q], %s\n"
                 "  }\n" +
                 R,
         .expected = "escapes:unknown_op",
         .by = "ctjs.set_property",
         .position = 2},
    };

    for (const row & r : rows) { check(context, r); }

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("escape analysis: %zu rows, every cell agrees with the VM\n", rows.size());
    return 0;
}
