#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkArrayContents(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string read = "  %read = ctjs.get_property %a[%zero]\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "complete contents: inline/append/overwrite preserve exact earlier reads",
         .body = array + read +
                 "  ctjs.append %y to %a\n"
                 "  ctjs.set_property %a[%zero], %y\n"
                 "  %later = ctjs.get_property %a[%zero]\n"
                 "  %second = ctjs.get_property %a[%one]\n"
                 "  ctjs.return %a\n",
         .arrays = "a:[y,y]",
         .reads = "a[0]=x; a[0]=y; a[1]=y",
         .exit = "a -> {a,y}",
         .writes = "ctjs.create_array[0]:a[0]=x; ctjs.append[1]:a[1]=y; "
                   "ctjs.set_property[2]:a[0]=y"},
        {.what = "complete contents: loaded aliases write the same nested array instance",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                          "  %alias = ctjs.get_property %b[%zero]\n"
                          "  ctjs.append %x to %alias\n"
                          "  %read = ctjs.get_property %a[%zero]\n"
                          "  ctjs.return %b\n",
         .arrays = "a:[x]; b:[a]",
         .reads = "b[0]=a; a[0]=x",
         .exit = "b -> {a,b,x}",
         .writes = "ctjs.create_array[0]:b[0]=a; ctjs.append[1]:a[0]=x"},
        {.what = "complete contents: a saved read survives a later overwrite",
         .body = array + read +
                 "  ctjs.set_property %a[%zero], %y\n"
                 "  ctjs.return %read\n",
         .arrays = "a:[y]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        {.what = "complete contents: a loaded key and loaded stored value have exact origins",
         .body = array + "  %keys = ctjs.create_array [%zero] {storage_test_id = \"keys\"}\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  %value = ctjs.get_property %a[%key]\n"
                         "  ctjs.append %value to %a\n"
                         "  ctjs.return %a\n",
         .arrays = "a:[x,x]; keys:[zero]",
         .reads = "keys[0]=zero; a[0]=x",
         .exit = "a -> {a,x}"},
        {.what = "complete contents: self cycle has one identity without a confinement claim",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  ctjs.append %a to %a\n"
                          "  %read = ctjs.get_property %a[%zero]\n"
                          "  ctjs.return %read\n",
         .arrays = "a:[a]",
         .reads = "a[0]=a",
         .exit = "a -> {a}"},
        {.what = "complete contents: mutual cycles terminate with both exact identities",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                          "  ctjs.append %b to %a\n"
                          "  ctjs.return %b\n",
         .arrays = "a:[b]; b:[a]",
         .exit = "b -> {a,b}"},
        {.what = "complete contents: stored locals need not be reachable from a primitive return",
         .body = array + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "complete contents: an empty returned array has no element paths",
         .body = "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n  ctjs.return %a\n",
         .arrays = "a:[]",
         .exit = "a -> {a}"},
        {.what = "contents refuses an unknown initializer",
         .body = values + "  %a = ctjs.create_array [%p]\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "contents refuses an unknown appended value",
         .body = array + read + "  ctjs.append %p to %a\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "contents refuses an unknown replacement",
         .body = array + read + "  ctjs.set_property %a[%zero], %p\n" + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "contents refuses an external base",
         .body = array + "  %read = ctjs.get_property %p[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownArray},
        {.what = "contents refuses numeric property coercion on a fresh ordinary object",
         .body = array + "  %read = ctjs.get_property %x[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnknownPropertyKey},
        {.what = "contents refuses a dynamic key",
         .body = array + "  %read = ctjs.get_property %a[%p]\n" + done,
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "a direct String array write cannot erase the VM's unchanged child",
         .body = array + read +
                 "  %key = ctjs.constant #ctjs.string<\"0\">\n"
                 "  ctjs.set_property %a[%key], %y\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "an array-loaded String write key cannot erase an unchanged child",
         .body = array + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                         "  %keys = ctjs.create_array [%string]\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  ctjs.set_property %a[%key], %y\n  ctjs.return %a\n",
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "an array-loaded String read key cannot assume a dense own element",
         .body = array + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                         "  %keys = ctjs.create_array [%string]\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  %read = ctjs.get_property %a[%key]\n  ctjs.return %read\n",
         .failure = ArrayContentsFailure::UnknownIndex},
        {.what = "contents refuses an uninitialized read",
         .body = array + "  %read = ctjs.get_property %a[%one]\n" + done,
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "contents refuses extension by generic write even without a hole",
         .body = array + "  ctjs.set_property %a[%one], %x\n" + done,
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "contents refuses a late deletion",
         .body = array + read + "  ctjs.delete_property %a[%zero]\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses a late prototype mutation",
         .body = array + read + "  ctjs.set_proto %p on %a\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses a late accessor",
         .body = array + read + "  ctjs.define_accessor \"0\" on %a get %p set %q\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses a late call even when no array is an explicit actual",
         .body = array + read + "  %called = ctjs.call %p(%q)\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses publication after every own read",
         .body = array + read + "  ctjs.store_global \"held\", %read\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses cell retention after every own read",
         .body = array + read + "  %cell = ctjs.create_cell %read\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses an unsupported carrier",
         .body = array + read + "  %carried = ctjs.iterable of %a\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses throw's implicit diagnostic formatting call",
         .body = array + read + "  ctjs.throw %read\n",
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses an unrelated global getter",
         .body = array + read + "  %global = ctjs.load_global \"later\"\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses late arguments retention",
         .body = array + read + "  %args = ctjs.make_arguments\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses late rest retention",
         .body = array + read + "  %rest = ctjs.gather_rest from 0\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses suspension retention",
         .body = array + read + "  %suspended = ctjs.suspend await %p\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "contents refuses nested region retention",
         .body = array + read +
                 "  %region = scf.execute_region -> !ctjs.value {\n"
                 "    ctjs.store_global \"held\", %read\n"
                 "    scf.yield %read : !ctjs.value\n"
                 "  }\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
        {.what = "contents ignores retention in a structurally unreachable block",
         .body = array + read + done + "^dead:\n  %args = ctjs.make_arguments\n" + done,
         .arrays = "a:[x]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        {.what = "contents follows an unconditional successor with exact local values",
         .body = array + "  cf.br ^next\n^next:\n" + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "contents ignores a structural join's unreachable predecessor",
         .body = array + "  cf.br ^join\n^dead:\n  cf.br ^join\n^join:\n" + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "contents refuses opaque successor semantics",
         .body = array + "  \"test.branch\"()[^next] : () -> ()\n^next:\n" + done,
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
        {.what = "contents checks both conditional edges even when truthy has a constant input",
         .body = array +
                 "  %condition = ctjs.truthy %zero\n"
                 "  cf.cond_br %condition, ^left, ^right\n^left:\n" +
                 done + "^right:\n" + done,
         .arrays = "a:[x] | a:[x]",
         .exit = "zero -> {}; zero -> {}"},
        {.what = "contents refuses a loop rather than collapsing repeated allocation instances",
         .body = "  cf.br ^loop\n^loop:\n  %a = ctjs.create_array []\n  cf.br ^loop\n",
         .failure = ArrayContentsFailure::UnsupportedControlFlow},
    };
    const auto run = [&](const contents_row & r) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + r.body + "}\n", &context);
        if (!module) {
            fail(row{.what = r.what, .body = r.body, .expected = ""},
                 "the contents fixture did not parse");
            return;
        }
        checkArrayContents(*module, r);
    };
    for (const contents_row & r : rows) { run(r); }

    // The key interpretation is independent of the VM's permissive numeric
    // cast. Fractional/NaN/infinite numbers must never borrow a dense slot
    // proof. Number -0 qualifies. Even canonical String "0" stays refused
    // because the VM's named-property path does not access dense array slots.
    const std::vector<std::pair<std::string, bool>> keys = {
        {"#ctjs.number<9223372036854775808>", true}, // -0
        {"#ctjs.string<\"0\">", false},
        {"#ctjs.string<\"-0\">", false},
        {"#ctjs.string<\"00\">", false},
        {"#ctjs.string<\"+0\">", false},
        {"#ctjs.string<\"0.0\">", false},
        {"#ctjs.string<\"\">", false},
        {"#ctjs.string<\"length\">", false},
        {"#ctjs.string<\"4294967295\">", false},
        {"#ctjs.string<\"100000000000000000000000000000000000\">", false},
        {"#ctjs.number<4602678819172646912>", false},  // 0.5
        {"#ctjs.number<13830554455654793216>", false}, // -1
        {"#ctjs.number<9221120237041090560>", false},  // NaN
        {"#ctjs.number<9218868437227405312>", false},  // infinity
    };
    for (const auto & [key, supported] : keys) {
        run({.what = "contents independently validates canonical own-element keys",
             .body = array + "  %key = ctjs.constant " + key +
                     "\n"
                     "  %read = ctjs.get_property %a[%key]\n" +
                     done,
             .failure = supported ? ArrayContentsFailure::None : ArrayContentsFailure::UnknownIndex,
             .arrays = supported ? "a:[x]" : "",
             .reads = supported ? "a[0]=x" : "",
             .exit = supported ? "zero -> {}" : ""});
    }

    contents_row mutation{.what =
                              "contents is recomputed after live operand and late-sink mutations",
                          .body = array + read +
                                  "  ctjs.set_property %a[%zero], %y\n"
                                  "  %later = ctjs.get_property %a[%zero]\n"
                                  "  ctjs.return %a\n",
                          .arrays = "a:[y]",
                          .reads = "a[0]=x; a[0]=y",
                          .exit = "a -> {a,y}"};
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        ctjs::GetPropertyOp load;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        const mlir::Value original = store.getValue();
        const mlir::Value base = load.getObject();
        const mlir::Value key = load.getKey();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        checkArrayContents(*module, mutation);

        store->setOperand(2,
                          llvm::cast<ctjs::CreateArrayOp>(base.getDefiningOp()).getElements()[0]);
        mutation.arrays = "a:[x]";
        mutation.reads = "a[0]=x; a[0]=x";
        mutation.exit = "a -> {a,x}";
        checkArrayContents(*module, mutation);
        store->setOperand(2, parameter);
        mutation.failure = ArrayContentsFailure::UnknownValue;
        checkArrayContents(*module, mutation);
        store->setOperand(2, original);
        load->setOperand(1, parameter);
        mutation.failure = ArrayContentsFailure::UnknownIndex;
        checkArrayContents(*module, mutation);
        load->setOperand(1, key);
        load->setOperand(0, parameter);
        mutation.failure = ArrayContentsFailure::UnknownArray;
        checkArrayContents(*module, mutation);
        load->setOperand(0, base);

        // A forged completion marker cannot rescue a proof after publication.
        mlir::OpBuilder builder(function.getBody().front().getTerminator());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        base.getDefiningOp()->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        checkArrayContents(*module, mutation);
        published.erase();
        base.getDefiningOp()->removeAttr("ctnative.array_contents_complete");
        mutation.failure = ArrayContentsFailure::None;
        mutation.arrays = "a:[y]";
        mutation.reads = "a[0]=x; a[0]=y";
        mutation.exit = "a -> {a,y}";
        checkArrayContents(*module, mutation);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live contents mutation fixture did not parse");
    }
    std::printf("array contents: %zu rows, %zu key controls, seven live states\n", rows.size(),
                keys.size());
}

void checkArrayRetention(mlir::MLIRContext & context) {
    const std::string values = "  %zero = ctjs.constant #ctjs.number<0>\n"
                               "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                               "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
                               "  %y = ctjs.create_object {storage_test_id = \"y\"}\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string read = "  %read = ctjs.get_property %a[%zero]\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<retention_row> rows = {
        {.what = "retention discharges a local array's unreturned object element",
         .body = array + done,
         .discharged = "x"},
        {.what = "retention discharges every private append and repeated initializer",
         .body = array + "  ctjs.append %x to %a\n  ctjs.append %y to %a\n" + done,
         .discharged = "x,y"},
        {.what = "retention discharges a nested acyclic local array graph",
         .body = array + "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n" + done,
         .discharged = "x,a"},
        {.what = "repeated edges and shared children are not cycles",
         .body = array +
                 "  %b = ctjs.create_array [%a, %a] {storage_test_id = \"b\"}\n"
                 "  %c = ctjs.create_array [%a] {storage_test_id = \"c\"}\n" +
                 done,
         .discharged = "x,a"},
        {.what = "a returned array retains every child with its original Stored witness",
         .body = array + "  ctjs.append %y to %a\n  ctjs.return %a\n"},
        {.what = "a returned array does not retain an overwritten child",
         .body = array + "  ctjs.set_property %a[%zero], %y\n  ctjs.return %a\n",
         .discharged = "x"},
        {.what = "a saved returned read retains the old child after replacement",
         .body = array + read + "  ctjs.set_property %a[%zero], %y\n  ctjs.return %read\n",
         .discharged = "y"},
        {.what = "a loaded array alias updates the returned container's retention graph",
         .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                          "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                          "  %alias = ctjs.get_property %b[%zero]\n"
                          "  ctjs.append %x to %alias\n"
                          "  ctjs.return %b\n"},
        {.what = "a loaded child stored in a returned second array remains retained",
         .body = array + read +
                 "  %b = ctjs.create_array [%read] {storage_test_id = \"b\"}\n"
                 "  ctjs.set_property %a[%zero], %y\n"
                 "  ctjs.return %b\n",
         .discharged = "y"},
        {.what = "a saved returned array read retains its own current descendants",
         .body = array + "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                         "  %saved = ctjs.get_property %b[%zero]\n"
                         "  ctjs.set_property %b[%zero], %y\n"
                         "  ctjs.return %saved\n",
         .discharged = "y"},
        {.what = "a direct child return is not hidden by its earlier storage",
         .body = array + "  ctjs.return %x\n"},
        {.what = "a primitive loaded return cannot retain the overwritten object",
         .body = array + "  ctjs.set_property %a[%zero], %zero\n" + read + "  ctjs.return %read\n",
         .discharged = "x"},
        {.what = "branch-carried saved reads retain the old child after a successor overwrite",
         .body = array + read +
                 "  cf.br ^next(%a, %read : !ctjs.value, !ctjs.value)\n"
                 "^next(%base: !ctjs.value, %saved: !ctjs.value):\n"
                 "  ctjs.set_property %base[%zero], %y\n  ctjs.return %saved\n",
         .discharged = "y"},
        {.what = "returning a successor-mutated array releases only its overwritten child",
         .body = array + "  cf.br ^next(%a, %zero : !ctjs.value, !ctjs.value)\n"
                         "^next(%base: !ctjs.value, %key: !ctjs.value):\n"
                         "  ctjs.set_property %base[%key], %y\n  ctjs.return %base\n",
         .discharged = "x"},
        {.what = "an unreturned self cycle does not select a shared graph owner",
         .body = array + "  ctjs.append %a to %a\n" + done,
         .complete = false},
        {.what = "an unreturned mutual cycle stays outside the refinement",
         .body = array +
                 "  %b = ctjs.create_array [%a] {storage_test_id = \"b\"}\n"
                 "  ctjs.append %b to %a\n" +
                 done,
         .complete = false},
        {.what = "a transient cycle stays refused after its last edge is overwritten",
         .body = array +
                 "  ctjs.set_property %a[%zero], %a\n"
                 "  ctjs.set_property %a[%zero], %y\n" +
                 done,
         .complete = false},
        {.what = "a disconnected cycle leaves even an acyclic candidate untouched",
         .body = array +
                 "  %b = ctjs.create_array [] {storage_test_id = \"b\"}\n"
                 "  ctjs.append %b to %b\n" +
                 done,
         .complete = false},
        {.what = "an unknown return does not borrow complete local contents",
         .body = array + "  ctjs.return %p\n",
         .complete = false},
        {.what = "missing solver lattices keep their unvisited refusal",
         .body = array + done,
         .complete = false,
         .withAnalysis = false},
    };
    std::size_t budgets = 0;
    for (const retention_row & r : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + r.body + "}\n", &context);
        if (!module) {
            fail(row{.what = r.what, .body = r.body, .expected = r.discharged},
                 "the retention fixture did not parse");
            continue;
        }
        budgets += checkArrayRetention(*module, r);
    }

    retention_row mutation{
        .what = "retention follows current returns, writes, keys and late exposures",
        .body = array + read + "  ctjs.set_property %a[%zero], %y\n  ctjs.return %a\n",
        .discharged = "x"};
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        ctjs::GetPropertyOp load;
        module->walk([&](ctjs::SetPropertyOp op) { store = op; });
        module->walk([&](ctjs::GetPropertyOp op) { load = op; });
        mlir::Operation * exit = function.getBody().front().getTerminator();
        const mlir::Value base = load.getObject();
        const mlir::Value key = load.getKey();
        const mlir::Value replacement = store.getValue();
        const mlir::Value first =
            llvm::cast<ctjs::CreateArrayOp>(base.getDefiningOp()).getElements()[0];
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        budgets += checkArrayRetention(*module, mutation);
        exit->setOperand(0, load.getResult());
        mutation.discharged = "y";
        budgets += checkArrayRetention(*module, mutation);
        store->setOperand(2, first);
        mutation.discharged = "";
        budgets += checkArrayRetention(*module, mutation);
        store->setOperand(2, replacement);
        load->setOperand(1, parameter);
        mutation.complete = false;
        budgets += checkArrayRetention(*module, mutation);
        load->setOperand(1, key);
        load->setOperand(0, parameter);
        budgets += checkArrayRetention(*module, mutation);
        load->setOperand(0, base);
        exit->setOperand(0, base);
        mlir::OpBuilder builder(exit);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        base.getDefiningOp()->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        base.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        budgets += checkArrayRetention(*module, mutation);
        publication.erase();
        // Forged markers remain present. Only restoring the complete current
        // operation/contents graph permits the same refinement again.
        mutation.complete = true;
        mutation.discharged = "x";
        budgets += checkArrayRetention(*module, mutation);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = mutation.discharged},
             "the live retention mutation fixture did not parse");
    }
    std::printf("array retention: %zu rows, seven live states, %zu budget cutoffs\n", rows.size(),
                budgets);
}

void checkObjectContents(mlir::MLIRContext & context) {
    // Preserve the historical object cases and their later mutations. Their
    // first assignment now refuses: an inherited setter may retain either
    // operand without installing an own field, so no later graph is evidence.
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %other = ctjs.constant #ctjs.string<\"other\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
        "  ctjs.set_property %o[%key], %x\n";
    const std::string read = "  %before = ctjs.get_property %o[%key]\n";
    const std::string replace = "  ctjs.set_property %o[%key], %y\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string split = "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^left, ^right\n^left:\n";
    struct object_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclic = true;
    };
    const std::vector<object_row> rows = {
        {.contents = {.what = "private own object fields discharge their children",
                      .body = values + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:x}",
                      .propertyReads = "",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a returned object retains its current own child",
                      .body = values + "  ctjs.return %o\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{child:x}"}},
        {.contents = {.what = "a saved own read keeps its origin after replacement",
                      .body = values + read + replace + "  ctjs.return %before\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "x -> {x}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=x"},
         .discharged = "y"},
        {.contents = {.what = "returned object replacement preserves every earlier read",
                      .body = values + read + replace +
                              "  %after = ctjs.get_property %o[%key]\n  ctjs.return %o\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o,y}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=x; o[child]=y",
                      .propertyWrites =
                          "ctjs.set_property[2]:o[child]=x; ctjs.set_property[2]:o[child]=y"},
         .discharged = "x"},
        {.contents = {.what = "equal String keys from distinct constants name one own field",
                      .body = values +
                              "  %same = ctjs.constant #ctjs.string<\"child\">\n"
                              "  ctjs.set_property %o[%same], %y\n" +
                              read + "  ctjs.return %before\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "y -> {y}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=y"},
         .discharged = "x"},
        {.contents = {.what = "an object loaded through an array mutates the same own fields",
                      .body = values + "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                                       "  %alias = ctjs.get_property %a[%zero]\n"
                                       "  ctjs.set_property %alias[%key], %y\n"
                                       "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o]",
                      .reads = "a[0]=o",
                      .exit = "a -> {a,o,y}",
                      .objects = "x:{}; y:{}; o:{child:y}"},
         .discharged = "x"},
        {.contents = {.what = "a returned object follows an array field to its current children",
                      .body = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                       "  ctjs.set_property %o[%key], %a\n"
                                       "  %alias = ctjs.get_property %o[%key]\n"
                                       "  ctjs.set_property %alias[%zero], %y\n"
                                       "  ctjs.return %o\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[y]",
                      .exit = "o -> {a,o,y}",
                      .objects = "x:{}; y:{}; o:{child:a}",
                      .propertyReads = "o[child]=a"},
         .discharged = "x"},
        {.contents = {.what = "an own read stored in a returned array keeps its earlier child",
                      .body = values + read + replace +
                              "  %a = ctjs.create_array [%before] {storage_test_id = \"a\"}\n"
                              "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}",
                      .objects = "x:{}; y:{}; o:{child:y}",
                      .propertyReads = "o[child]=x"},
         .discharged = "y"},
        {.contents = {.what = "an absent own field cannot use prototype or builtin lookup",
                      .body = values + "  %read = ctjs.get_property %o[%other]\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "constructor lookup still needs an actual own write",
                      .body = values +
                              "  %constructor = ctjs.constant #ctjs.string<\"constructor\">\n"
                              "  %read = ctjs.get_property %o[%constructor]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "literal array append cannot initialize an object property",
                      .body = values + "  ctjs.append %y to %o\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "external own-property values remain unknown",
                      .body = values + "  ctjs.set_property %o[%key], %p\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "external property keys cannot borrow fixed own fields",
                      .body = values + "  %read = ctjs.get_property %o[%p]\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what =
                          "an object-loaded String write key preserves the original array child",
                      .body = values + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                                       "  ctjs.set_property %o[%other], %string\n"
                                       "  %keyValue = ctjs.get_property %o[%other]\n"
                                       "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                       "  ctjs.set_property %a[%keyValue], %y\n  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an object-loaded String read key cannot borrow dense array contents",
                      .body = values + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                                       "  ctjs.set_property %o[%other], %string\n"
                                       "  %keyValue = ctjs.get_property %o[%other]\n"
                                       "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                       "  %read = ctjs.get_property %a[%keyValue]\n"
                                       "  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "__proto__ spelling never establishes an ordinary own field",
                      .body = values +
                              "  %proto = ctjs.constant #ctjs.string<\"__proto__\">\n"
                              "  ctjs.set_property %o[%proto], %x\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a late prototype change invalidates prior own-field evidence",
                      .body = values + read + "  ctjs.set_proto %x on %o\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a late accessor definition invalidates own-field evidence",
                      .body = values + read +
                              "  ctjs.define_accessor \"child\" on %o get %p set %q\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "private own-field deletion preserves its saved read origin",
                      .body = values + read + "  ctjs.delete_property %o[%key]\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "an unrelated late call still invalidates own-field evidence",
                      .body = values + read + "  %call = ctjs.call %p(%q)\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late publication cannot hide behind an earlier own read",
                      .body = values + read + "  ctjs.store_global \"held\", %before\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an own-field self cycle terminates without selecting an owner",
                      .body = values + "  ctjs.set_property %o[%key], %o\n  ctjs.return %o\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{child:o}"},
         .acyclic = false},
        {.contents = {.what = "an object-to-object cycle enters the complete all-write graph",
                      .body = values + "  ctjs.set_property %x[%key], %o\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{child:o}; y:{}; o:{child:x}"},
         .acyclic = false},
        {.contents = {.what = "a transient mixed array-object cycle remains refused",
                      .body = values +
                              "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                              "  ctjs.set_property %o[%key], %a\n"
                              "  ctjs.set_property %o[%key], %zero\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o]",
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:zero}"},
         .acyclic = false},
        {.contents = {.what = "both object branches overwrite a child before their shared return",
                      .body = values + split + replace + "  cf.br ^join\n^right:\n" + replace +
                              "  cf.br ^join\n^join:\n  ctjs.return %o\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o,y}; o -> {o,y}",
                      .objects = "x:{}; y:{}; o:{child:y} | x:{}; y:{}; o:{child:y}"},
         .discharged = "x"},
        {.contents = {.what = "an untouched object branch retains its original child",
                      .body = values + split + replace +
                              "  ctjs.return %o\n^right:\n  ctjs.return %o\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o,y}; o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{child:y} | x:{}; y:{}; o:{child:x}"}},
        {.contents = {.what = "a joined object target cannot overwrite both incoming objects",
                      .body = values +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %a = ctjs.create_array [%o, %b] {storage_test_id = \"a\"}\n"
                              "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^join(%o : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  ctjs.set_property %selected[%key], %zero\n"
                              "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o,b] | a:[o,b]",
                      .exit = "a -> {a,b,o,y}; a -> {a,b,o,x}",
                      .objects = "x:{}; y:{}; o:{child:zero}; b:{child:y} | "
                                 "x:{}; y:{}; o:{child:x}; b:{child:zero}"}},
        {.contents = {.what = "one missing own field discards a previous path's successful read",
                      .body = values + split +
                              "  ctjs.set_property %o[%other], %y\n"
                              "  cf.br ^join\n^right:\n  cf.br ^join\n"
                              "^join:\n"
                              "  %read = ctjs.get_property %o[%other]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "unknown second-path own values discard an earlier complete exit",
                      .body = values + split + done +
                              "^right:\n  ctjs.set_property %o[%key], %p\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "mutually exclusive mixed edges still refuse a cycle owner",
                      .body = values + "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n" +
                              split + "  ctjs.set_property %o[%key], %a\n" + done +
                              "^right:\n  ctjs.append %o to %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[] | a:[o]",
                      .exit = "zero -> {}; zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:a} | x:{}; y:{}; o:{child:x}"},
         .acyclic = false},
        {.contents = {.what = "matching imported frame roots do not publish private object fields",
                      .body = "  %frame = ctjs.frame_enter 4\n" + values +
                              "  ctjs.root %o in %frame\n  ctjs.root %x in %frame\n"
                              "  ctjs.frame_exit %frame\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}"},
         .discharged = "x"},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const object_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = "",
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclic});
    };
    const auto run = [&](const object_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the object contents fixture did not parse");
            return;
        }
        check(*module, expected);
    };
    for (const object_row & expected : rows) { run(expected); }

    const std::vector<std::pair<std::string, bool>> keys = {
        {"#ctjs.string<\"\">", true},
        {"#ctjs.string<\"0\">", true},
        {"#ctjs.string<\"00\">", true},
        {"#ctjs.string<\"length\">", true},
        {"#ctjs.string<\"constructor\">", true},
        {"#ctjs.string<\"x\\00y\">", true},
        {"#ctjs.string<\"" + std::string(256, 'k') + "\">", true},
        {"#ctjs.string<\"" + std::string(257, 'k') + "\">", false},
        {"#ctjs.string<\"__proto__\">", false},
        {"#ctjs.number<0>", false},
        {"#ctjs.boolean<true>", false},
    };
    for (const auto & [key, supported] : keys) {
        run({.contents = {.what = "fixed object keys are bounded Strings without coercion",
                          .body = values + "  %fixed = ctjs.constant " + key +
                                  "\n  ctjs.set_property %o[%fixed], %y\n"
                                  "  %result = ctjs.get_property %o[%fixed]\n"
                                  "  ctjs.return %result\n",
                          .failure = ArrayContentsFailure::UnsupportedOperation,
                          .exit = supported ? "y -> {y}" : ""},
             .discharged = supported ? "x" : ""});
    }

    object_row mutation = rows[3];
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp changed;
        ctjs::GetPropertyOp readOp;
        module->walk([&](ctjs::SetPropertyOp op) { changed = op; });
        module->walk([&](ctjs::GetPropertyOp op) { readOp = op; });
        const mlir::Value original = changed.getValue();
        const mlir::Value base = changed.getObject();
        const mlir::Value key = readOp.getKey();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::Value child;
        mlir::Value other;
        module->walk([&](ctjs::CreateObjectOp op) {
            if (contentsLabel(op) == "x") { child = op.getResult(); }
        });
        module->walk([&](ctjs::ConstantOp op) {
            auto string = llvm::dyn_cast<ctjs::StringAttr>(op.getValue());
            if (string && string.getValue() == "other") { other = op.getResult(); }
        });
        mlir::OpBuilder builder(function.getBody().front().getTerminator());
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        changed->setOperand(2, child);
        check(*module, mutation);
        changed->setOperand(2, parameter);
        check(*module, mutation);
        changed->setOperand(2, original);
        readOp->setOperand(1, other);
        check(*module, mutation);
        readOp->setOperand(1, parameter);
        check(*module, mutation);
        readOp->setOperand(1, key);
        readOp->setOperand(0, parameter);
        check(*module, mutation);
        readOp->setOperand(0, base);
        changed->setOperand(0, child);
        check(*module, mutation);
        changed->setOperand(0, base);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        check(*module, mutation);
        publication.erase();
        mutation = rows[3];
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live object contents fixture did not parse");
    }
    object_row keyMutation{
        .contents = {.what = "live Number-to-String array keys invalidate forged contents proofs",
                     .body = values + "  ctjs.set_property %o[%other], %zero\n"
                                      "  %keyValue = ctjs.get_property %o[%other]\n"
                                      "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n"
                                      "  ctjs.set_property %a[%keyValue], %y\n  ctjs.return %a\n",
                     .failure = ArrayContentsFailure::UnsupportedOperation,
                     .arrays = "a:[y]",
                     .exit = "a -> {a,y}",
                     .objects = "x:{}; y:{}; o:{child:x,other:zero}",
                     .propertyReads = "o[other]=zero"},
        .discharged = "x"};
    auto keyModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + keyMutation.contents.body + "}\n", &context);
    if (keyModule) {
        ctjs::FuncOp function = *keyModule->getOps<ctjs::FuncOp>().begin();
        ctjs::ConstantOp index;
        keyModule->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::NumberAttr>(op.getValue())) { index = op; }
        });
        const mlir::Attribute number = index.getValue();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*keyModule, keyMutation);
        index->setAttr("value", ctjs::StringAttr::get(&context, "0"));
        check(*keyModule, keyMutation);
        index->setAttr("value", number);
        check(*keyModule, keyMutation);
    } else {
        fail(row{.what = keyMutation.contents.what,
                 .body = keyMutation.contents.body,
                 .expected = ""},
             "the live array key mutation fixture did not parse");
    }
    const row setter{.what =
                         "an inherited setter may retain a child despite later property deletion",
                     .body = "  %zero = ctjs.constant #ctjs.number<0>\n"
                             "  %key = ctjs.constant #ctjs.string<\"nd3slot\">\n"
                             "  %child = ctjs.create_object {check}\n"
                             "  %local = ctjs.create_object\n"
                             "  ctjs.set_property %local[%key], %child\n"
                             "  ctjs.delete_property %local[%key]\n"
                             "  ctjs.return %zero\n",
                     .expected = "escapes:stored",
                     .by = "ctjs.set_property",
                     .position = 2};
    ctcompile::test::escape::check(context, setter);
    if (auto setterModule = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + setter.body + "}\n", &context)) {
        checkArrayContents(*setterModule, {.what = setter.what,
                                           .body = setter.body,
                                           .failure = ArrayContentsFailure::UnsupportedOperation});
    } else {
        fail(setter, "the inherited-setter retention fixture did not parse");
    }
    std::printf("object contents/retention: %zu rows, %zu key controls, twelve live states, "
                "%zu retention budget cutoffs\n",
                rows.size() + 1, keys.size(), budgets);
}

} // namespace ctcompile::test::escape::arrays
