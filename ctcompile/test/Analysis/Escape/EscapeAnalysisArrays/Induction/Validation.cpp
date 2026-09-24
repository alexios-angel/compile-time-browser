#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::validation() {
    const std::string lengthRead = "  %key = ctjs.constant #ctjs.string<\"length\">\n"
                                   "  %length = ctjs.get_property %array[%key]\n";
    const std::string cachedPrefix = prefix + replace(lengthRead, "%array", "%a");
    std::string cachedLoop = replace(loop, lengthRead, "");
    cachedLoop =
        replace(cachedLoop,
                "^header(%a, %zero, %zero :", "^header(%a, %zero, %zero, %length : !ctjs.value,");
    cachedLoop =
        replace(cachedLoop, "%sum: !ctjs.value):", "%sum: !ctjs.value, %bound: !ctjs.value):");
    cachedLoop = replace(cachedLoop, "compare lt %index, %length", "compare lt %index, %bound");
    cachedLoop = replace(cachedLoop, "^body(%array, %index, %sum :",
                         "^body(%array, %index, %sum, %bound : !ctjs.value,");
    cachedLoop =
        replace(cachedLoop, "%s: !ctjs.value):", "%s: !ctjs.value, %savedBound: !ctjs.value):");
    cachedLoop = replace(cachedLoop, "^header(%base, %step, %added :",
                         "^header(%base, %step, %added, %savedBound : !ctjs.value,");
    const std::string cached = cachedPrefix + cachedLoop;
    run({.what = "a cached own length retains its immutable Number through CFG transport",
         .body = cached,
         .arrays = "a:[one,two,three]",
         .reads = "a[0]=one; a[1]=two; a[2]=three",
         .exit = "added -> {}"});
    std::string cachedChild = replace(cached, "  %a =",
                                      "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
                                      "  %a =");
    cachedChild = replace(cachedChild, "[%one, %two, %three]", "[%x, %zero, %x]");
    cachedChild =
        replace(cachedChild, "  %read =", "  ctjs.set_property %base[%i], %zero\n  %read =");
    cachedChild = replace(cachedChild, "ctjs.return %result", "ctjs.return %a");
    run({.what = "cached own lengths discharge children overwritten on every visit",
         .body = cachedChild,
         .arrays = "a:[zero,zero,zero]",
         .reads = "a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "cached length clearing cannot discharge a separately retained child",
         .body = replace(cachedChild, "ctjs.return %a", "ctjs.return %x"),
         .arrays = "a:[zero,zero,zero]",
         .reads = "a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "x -> {x}"});
    reject("a changed cached bound cannot reuse its initial snapshot",
           replace(cached, "%added, %savedBound :", "%added, %zero :"));
    reject("a shrink after a saved length invalidates the current extent correspondence",
           cachedPrefix + "  ctjs.set_property %a[%key], %two\n" + cachedLoop);
    reject("an append after a saved length invalidates the current extent correspondence",
           cachedPrefix + "  ctjs.append %zero to %a\n" + cachedLoop);
    reject("cached length guards retain the complete no-resize census",
           replace(cached, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("cached length guards cannot authorize stores to a different allocation",
           replace(replace(cachedChild, "  %a =",
                           "  %other = ctjs.create_array [%one, %two, %three]\n"
                           "  %a ="),
                   "get_property %a[%key]", "get_property %other[%key]"));
    const std::string loaded =
        replace(replace(cachedChild, "  %length = ctjs.get_property %a[%key]",
                        "  %box = ctjs.create_array [%a] {storage_test_id = \"box\"}\n"
                        "  %loaded = ctjs.get_property %box[%zero]\n"
                        "  %length = ctjs.get_property %loaded[%key]"),
                "^header(%a, %zero", "^header(%loaded, %zero");
    run({.what = "cached lengths retain an own-loaded preheader array identity",
         .body = loaded,
         .arrays = "a:[zero,zero,zero]; box:[a]",
         .reads = "box[0]=a; a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "replacing the holder preserves the previously loaded array snapshot",
         .body = replace(loaded, "  cf.br ^header",
                         "  %other = ctjs.create_array [%zero] {storage_test_id = \"other\"}\n"
                         "  ctjs.set_property %box[%zero], %other\n"
                         "  cf.br ^header"),
         .arrays = "a:[zero,zero,zero]; box:[other]; other:[zero]",
         .reads = "box[0]=a; a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x,other");
    run({.what = "a saved child survives clearing through a loaded array identity",
         .body = replace(replace(loaded, "  cf.br ^header",
                                 "  %saved = ctjs.get_property %loaded[%zero]\n"
                                 "  cf.br ^header"),
                         "ctjs.return %a", "ctjs.return %saved"),
         .arrays = "a:[zero,zero,zero]; box:[a]",
         .reads = "box[0]=a; a[0]=x; a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "x -> {x}"},
        "a");
    reject("loaded cached guards cannot change the saved bound",
           replace(loaded, "%added, %savedBound :", "%added, %zero :"));
    reject("loaded cached guards retain the current extent check",
           replace(loaded, "  cf.br ^header",
                   "  ctjs.set_property %loaded[%key], %two\n  cf.br ^header"));
    reject("loaded cached guards retain the full mutation census",
           replace(loaded, "  %read =", "  ctjs.set_property %box[%zero], %a\n  %read ="));
    reject("loaded cached guards cannot clear a different receiver",
           replace(replace(loaded, "  cf.br ^header",
                           "  %other = ctjs.create_array [%zero, %zero, %zero]\n"
                           "  cf.br ^header"),
                   "^header(%loaded, %zero", "^header(%other, %zero"));
    run({.what = "a single-predecessor chain retains the loaded guard execution",
         .body = replace(loaded, "  %loaded =", "  cf.br ^preheader\n^preheader:\n  %loaded ="),
         .arrays = "a:[zero,zero,zero]; box:[a]",
         .reads = "box[0]=a; a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    run({.what = "the receiver and cached length may execute in successive blocks",
         .body = replace(loaded, "  %length =", "  cf.br ^preheader\n^preheader:\n  %length ="),
         .arrays = "a:[zero,zero,zero]; box:[a]",
         .reads = "box[0]=a; a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "a -> {a}"},
        "x");
    reject("a joined loaded guard needs more than single-predecessor provenance",
           replace(loaded, "  %loaded =",
                   "  %choice = ctjs.truthy %p\n"
                   "  cf.cond_br %choice, ^left, ^right\n"
                   "^left:\n  cf.br ^preheader\n"
                   "^right:\n  cf.br ^preheader\n"
                   "^preheader:\n  %loaded ="));
    std::string longPreheader;
    for (unsigned i = 0; i < 65; ++i) {
        const auto name = "preheader" + std::to_string(i);
        longPreheader += "  cf.br ^" + name + "\n^" + name + ":\n";
    }
    reject("loaded guard provenance retains its bounded predecessor walk",
           replace(loaded, "  %loaded =", longPreheader + "  %loaded ="));
    reject("a different guard bound is not the array's own length",
           replace(original, "compare lt %index, %length", "compare lt %index, %three"));
    reject("a replaced array alias invalidates the guard certificate",
           replace(original, "^header(%base, %step, %added", "^header(%a, %step, %added"));
    std::string changedKey = replace(
        original, "  cf.br ^header(%a, %zero, %zero : !ctjs.value, !ctjs.value, !ctjs.value)",
        "  %name = ctjs.constant #ctjs.string<\"length\">\n"
        "  cf.br ^header(%a, %zero, %zero, %name : !ctjs.value, !ctjs.value, "
        "!ctjs.value, !ctjs.value)");
    changedKey =
        replace(changedKey, "%sum: !ctjs.value):", "%sum: !ctjs.value, %property: !ctjs.value):");
    changedKey = replace(changedKey, "%array[%key]", "%array[%property]");
    changedKey =
        replace(changedKey, "^header(%base, %step, %added : !ctjs.value, !ctjs.value, !ctjs.value)",
                "^header(%base, %step, %added, %zero : !ctjs.value, !ctjs.value, "
                "!ctjs.value, !ctjs.value)");
    reject("a changing guard property cannot borrow its first length snapshot", changedKey);
    run({.what = "current own-element overwrites preserve the stable bound",
         .body = replace(original, "  %read =", "  ctjs.set_property %base[%i], %zero\n  %read ="),
         .arrays = "a:[zero,zero,zero]",
         .reads = "a[0]=zero; a[1]=zero; a[2]=zero",
         .exit = "added -> {}"});
    reject("length mutation inside the loop invalidates the stable bound",
           replace(original, "  %read =", "  ctjs.set_property %base[%key], %zero\n  %read ="));
    reject("allocation in a repeated block cannot collapse dynamic instances",
           replace(original, "  %read =", "  %fresh = ctjs.create_array []\n  %read ="));
    reject("publication inside the loop is rejected structurally",
           replace(original, "  %read =", "  ctjs.store_global \"held\", %base\n  %read ="));
    reject("a zero-trip loop still refuses an unsupported operation",
           replace(replace(original, "[%one, %two, %three]", "[]"),
                   "  %read =", "  ctjs.store_global \"held\", %base\n  %read ="));
    reject("effects after a finite loop invalidate complete contents",
           replace(original, "  ctjs.return %result",
                   "  ctjs.store_global \"held\", %a\n"
                   "  ctjs.return %result"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("an offset read must independently remain in bounds on the last iteration",
           replace(original, "  %read = ctjs.get_property %base[%i]",
                   "  %shifted = ctjs.binary_static add %i, %one\n"
                   "  %read = ctjs.get_property %base[%shifted]"),
           ArrayContentsFailure::MissingElement);
    reject("a different shorter array cannot borrow the guard array's bound",
           replace(replace(original, "  %a =",
                           "  %other = ctjs.create_array [%one] "
                           "{storage_test_id = \"other\"}\n  %a ="),
                   "%base[%i]", "%other[%i]"),
           ArrayContentsFailure::MissingElement);
    reject("every later element must independently exclude object coercion",
           replace(replace(original, "  %a =",
                           "  %child = ctjs.create_array [] "
                           "{storage_test_id = \"child\"}\n  %a ="),
                   "[%one, %two, %three]", "[%one, %child, %three]"),
           ArrayContentsFailure::UnsupportedOperation);
    reject("opaque loop-carried keys cannot retain a prior exact Number fact",
           replace(replace(original, "%base[%i]", "%base[%s]"), "^header(%base, %step, %added",
                   "^header(%base, %step, %p"),
           ArrayContentsFailure::UnknownIndex);
    liveStates = 0;
    for (const std::string & source :
         {computedChild, nonzeroChild, computedUnitChild, computedStrideChild}) {
        const bool starts = source == computedChild || source == nonzeroChild;
        const bool nonunit = source == computedStrideChild;
        contents_row live{.what = "live induction facts override stale solver and forged markers",
                          .body = replace(source, "ctjs.return %result", "ctjs.return %zero"),
                          .arrays = nonunit ? "a:[one,two,x]" : "a:[one,x]",
                          .reads = source == nonzeroChild ? "a[1]=x"
                                   : nonunit              ? "a[0]=one; a[2]=x"
                                                          : "a[0]=one; a[1]=x",
                          .exit = "zero -> {}"};
        if (auto module = mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + live.body + "}\n", &context)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            ctjs::BinaryOp producer;
            mlir::Value three;
            module->walk([&](ctjs::BinaryOp op) { producer = op; });
            module->walk([&](ctjs::ConstantOp op) {
                if (contentsLabel(op) == "three") { three = op.getResult(); }
            });
            const mlir::Value one = producer.getRhs();
            mlir::OpBuilder builder(function);
            function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
            function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
            producer->setAttr("ctnative.array_index",
                              builder.getI64IntegerAttr(source == computedChild ? 0
                                                        : nonunit               ? 2
                                                                                : 1));
            mlir::DataFlowSolver stale;
            stale.load<mlir::dataflow::DeadCodeAnalysis>();
            stale.load<mlir::dataflow::SparseConstantPropagation>();
            stale.load<EscapeAnalysis>();
            if (failed(stale.initializeAndRun(*module))) {
                fail(row{.what = live.what, .body = live.body, .expected = ""},
                     "the induction stale solver did not converge");
            }
            const mlir::Value invalid = starts ? three : producer.getLhs();
            for (mlir::Value rhs : {one, invalid, one}) {
                producer->setOperand(1, rhs);
                const bool complete = rhs == one;
                live.failure = complete ? ArrayContentsFailure::None
                                        : ArrayContentsFailure::UnsupportedControlFlow;
                checkArrayContents(*module, live);
                budgets += checkArrayRetention(*module, {.what = live.what,
                                                         .body = live.body,
                                                         .discharged = complete ? "x" : "",
                                                         .complete = complete});
                const auto verdicts = computeVerdicts(stale, function);
                if (verdicts.arrayRetentionComplete != complete ||
                    verdicts.confinedStoredSites != (complete ? 1U : 0U)) {
                    fail(row{.what = live.what, .body = live.body, .expected = ""},
                         "stale solver or forged marker supplied induction authority");
                }
                ++liveStates;
            }
        } else {
            fail(row{.what = live.what, .body = live.body, .expected = ""},
                 "the live induction fixture did not parse");
        }
    }
    std::string many = "%one";
    for (unsigned i = 1; i < 32; ++i) { many += ", %one"; }
    const std::string longLoop = replace(original, "%one, %two, %three", many);
    std::size_t unitWork = 0;
    for (const std::string & source :
         {longLoop, replace(longLoop, "add %i, %one", "add %i, %two")}) {
        if (auto module = mlir::parseSourceString<mlir::ModuleOp>(
                std::string{kPrologue} + source + "}\n", &context)) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            const auto complete = computeArrayContents(function);
            const auto partial = computeArrayContents(function, 128);
            const bool unit = source == longLoop;
            if (unit) { unitWork = complete.work; }
            if (!complete.complete || complete.reads.size() != (unit ? 32U : 16U) ||
                (!unit && complete.work >= unitWork) || partial.complete ||
                partial.failure != ArrayContentsFailure::WorkLimit || partial.work != 128 ||
                !partial.arrays.empty() || !partial.reads.empty() || !partial.writes.empty() ||
                !partial.exits.empty()) {
                fail(row{.what = "finite strides charge visited iterations and obey work limits",
                         .body = source,
                         .expected = ""},
                     "a long loop lost exact reads, bounded replay or prefix isolation");
            }
        } else {
            fail(row{.what = "finite induction work budget", .body = source, .expected = ""},
                 "the long induction fixture did not parse");
        }
    }
}

} // namespace ctcompile::test::escape::arrays::induction_detail
