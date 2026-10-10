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
        {.what = "a direct canonical String array write replaces the dense child",
         .body = array + read +
                 "  %key = ctjs.constant #ctjs.string<\"0\">\n"
                 "  ctjs.set_property %a[%key], %y\n  ctjs.return %a\n",
         .arrays = "a:[y]",
         .reads = "a[0]=x",
         .exit = "a -> {a,y}"},
        {.what = "an array-loaded canonical String write key replaces the dense child",
         .body = array + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                         "  %keys = ctjs.create_array [%string]\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  ctjs.set_property %a[%key], %y\n  ctjs.return %a\n",
         .arrays = "a:[y]; ctjs.create_array:[ctjs.constant]",
         .reads = "ctjs.create_array[0]=ctjs.constant",
         .exit = "a -> {a,y}"},
        {.what = "an array-loaded canonical String read key keeps its exact child origin",
         .body = array + "  %string = ctjs.constant #ctjs.string<\"0\">\n"
                         "  %keys = ctjs.create_array [%string]\n"
                         "  %key = ctjs.get_property %keys[%zero]\n"
                         "  %read = ctjs.get_property %a[%key]\n  ctjs.return %read\n",
         .arrays = "a:[x]; ctjs.create_array:[ctjs.constant]",
         .reads = "ctjs.create_array[0]=ctjs.constant; a[0]=x",
         .exit = "x -> {x}"},
        {.what = "String one reads and overwrites only the second dense element",
         .body = values + "  %a = ctjs.create_array [%x, %y] {storage_test_id = \"a\"}\n"
                          "  %key = ctjs.constant #ctjs.string<\"1\">\n"
                          "  %read = ctjs.get_property %a[%key]\n"
                          "  ctjs.set_property %a[%key], %zero\n"
                          "  ctjs.return %read\n",
         .arrays = "a:[x,zero]",
         .reads = "a[1]=y",
         .exit = "y -> {y}",
         .writes = "ctjs.create_array[0]:a[0]=x; ctjs.create_array[1]:a[1]=y; "
                   "ctjs.set_property[2]:a[1]=zero"},
        {.what = "the largest canonical String index is an absent dense element",
         .body = array +
                 "  %key = ctjs.constant #ctjs.string<\"4294967294\">\n"
                 "  %read = ctjs.get_property %a[%key]\n" +
                 done,
         .failure = ArrayContentsFailure::MissingElement},
        {.what = "a canonical String index cannot authorize a sparse write",
         .body = array +
                 "  %key = ctjs.constant #ctjs.string<\"4294967294\">\n"
                 "  ctjs.set_property %a[%key], %y\n" +
                 done,
         .failure = ArrayContentsFailure::MissingElement},
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
    // proof. Number -0 and canonical String "0" qualify; lookalike spellings
    // are named properties and cannot borrow an existing element's origin.
    const std::vector<std::pair<std::string, bool>> keys = {
        {"#ctjs.number<9223372036854775808>", true}, // -0
        {"#ctjs.string<\"0\">", true},
        {"#ctjs.string<\"-0\">", false},
        {"#ctjs.string<\"00\">", false},
        {"#ctjs.string<\"+0\">", false},
        {"#ctjs.string<\"0.0\">", false},
        {"#ctjs.string<\" 0\">", false},
        {"#ctjs.string<\"0 \">", false},
        {"#ctjs.string<\"0e0\">", false},
        {"#ctjs.string<\"0x0\">", false},
        {"#ctjs.string<\"\">", false},
        {"#ctjs.string<\"length\">", false},
        {"#ctjs.string<\"4294967295\">", false},
        {"#ctjs.string<\"4294967296\">", false},
        {"#ctjs.string<\"9999999999\">", false},
        {"#ctjs.string<\"100000000000000000000000000000000000\">", false},
        {"#ctjs.number<4602678819172646912>", false},  // 0.5
        {"#ctjs.number<13830554455654793216>", false}, // -1
        {"#ctjs.number<9221120237041090560>", false},  // NaN
        {"#ctjs.number<9218868437227405312>", false},  // infinity
    };
    for (const auto & [key, supported] : keys) {
        const bool length = key == "#ctjs.string<\"length\">";
        run({.what = "contents independently validates canonical own-element keys",
             .body = array + "  %key = ctjs.constant " + key +
                     "\n"
                     "  %read = ctjs.get_property %a[%key]\n" +
                     done,
             .failure = supported || length ? ArrayContentsFailure::None
                                            : ArrayContentsFailure::UnknownIndex,
             .arrays = supported || length ? "a:[x]" : "",
             .reads = supported ? "a[0]=x" : "",
             .exit = supported || length ? "zero -> {}" : ""});
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
    contents_row keyMutation{
        .what = "loaded and forwarded String indices are reproved after live spelling changes",
        .body = array + "  %key = ctjs.constant #ctjs.string<\"0\"> {storage_test_id = \"key\"}\n"
                        "  %keys = ctjs.create_array [%key] {storage_test_id = \"keys\"}\n"
                        "  %loaded = ctjs.get_property %keys[%zero]\n"
                        "  cf.br ^next(%loaded : !ctjs.value)\n"
                        "^next(%forwarded: !ctjs.value):\n"
                        "  ctjs.set_property %a[%forwarded], %y\n"
                        "  ctjs.return %a\n",
        .arrays = "a:[y]; keys:[key]",
        .reads = "keys[0]=key",
        .exit = "a -> {a,y}"};
    auto keyModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + keyMutation.body + "}\n", &context);
    if (keyModule) {
        ctjs::FuncOp function = *keyModule->getOps<ctjs::FuncOp>().begin();
        ctjs::ConstantOp index;
        keyModule->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::StringAttr>(op.getValue())) { index = op; }
        });
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*keyModule))) {
            fail(row{.what = keyMutation.what, .body = keyMutation.body, .expected = ""},
                 "the String index stale solver did not converge");
        }
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        for (const auto & [spelling, failure] :
             {std::pair{"0", ArrayContentsFailure::None},
              std::pair{"00", ArrayContentsFailure::UnknownIndex},
              std::pair{"4294967294", ArrayContentsFailure::MissingElement},
              std::pair{"0", ArrayContentsFailure::None}}) {
            index.setValueAttr(ctjs::StringAttr::get(&context, spelling));
            keyMutation.failure = failure;
            const bool complete = failure == ArrayContentsFailure::None;
            checkArrayContents(*keyModule, keyMutation);
            checkArrayRetention(*keyModule, {.what = keyMutation.what,
                                             .body = keyMutation.body,
                                             .discharged = complete ? "x" : "",
                                             .complete = complete});
            const auto current = computeVerdicts(stale, function);
            if (current.arrayRetentionComplete != complete ||
                current.confinedStoredSites != (complete ? 1U : 0U)) {
                fail(row{.what = keyMutation.what, .body = keyMutation.body, .expected = ""},
                     "a stale solver or forged marker supplied String index authority");
            }
        }
    } else {
        fail(row{.what = keyMutation.what, .body = keyMutation.body, .expected = ""},
             "the loaded String index mutation fixture did not parse");
    }
    std::printf("array contents: %zu rows, %zu key controls, eleven live states\n", rows.size(),
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
    const std::string captureStart = values +
                                     "  %a = ctjs.create_array [%zero] {storage_test_id = \"a\"}\n"
                                     "  %flag = ctjs.truthy %p\n"
                                     "  scf.if %flag {\n";
    const std::string captureWrite = "    ctjs.set_property %a[%zero], %x\n";
    const std::string captureEnd = "  }\n  ctjs.set_property %a[%zero], %zero\n";
    const std::string capturedStore = captureStart + captureWrite + "  }\n";
    const std::string clearedCapture = captureStart + captureWrite + captureEnd;
    const std::string receiverStart = "  %zero = ctjs.constant #ctjs.number<0>\n"
                                      "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                                      "  %a = ctjs.create_array [%zero] {storage_test_id = \"a\"}\n"
                                      "  %flag = ctjs.truthy %p\n"
                                      "  scf.if %flag {\n";
    const std::string receiverWrite = "    ctjs.set_property %a[%zero], %one\n";
    const std::string receiverRead = "    %read = ctjs.get_property %a[%zero]\n";
    const retention_row nestedOrigins{
        .what = "single-instance nested origins and their private receiver refine together",
        .body =
            receiverStart + "    %fresh = ctjs.create_array []\n" + receiverWrite + "  }\n" + done,
        .confinedReceivers = "a",
        .confinedNested = "ctjs.create_array"};
    const std::string localStart = "  %zero = ctjs.constant #ctjs.number<0>\n"
                                   "  %flag = ctjs.truthy %p\n";
    const std::string localArray =
        "    %fresh = ctjs.create_array [%zero] {storage_test_id = \"fresh\"}\n";
    const std::string localObject =
        "    %child = ctjs.create_object {storage_test_id = \"child\"}\n";
    const std::string selectedArrays =
        localStart + "  %selected = scf.if %flag -> (!ctjs.value) {\n" + localArray +
        "    scf.yield %fresh : !ctjs.value\n"
        "  } else {\n"
        "    %other = ctjs.create_array [%zero] {storage_test_id = \"other\"}\n"
        "    scf.yield %other : !ctjs.value\n"
        "  }\n";
    const std::string nestedStore =
        receiverStart + localArray + "    ctjs.set_property %a[%zero], %fresh\n  }\n";
    const std::string transportedSelection = selectedArrays +
                                             "  cf.br ^next(%selected : !ctjs.value)\n"
                                             "^next(%base: !ctjs.value):\n";
    const std::string transportedStore =
        nestedStore + "  cf.br ^next(%a : !ctjs.value)\n^next(%base: !ctjs.value):\n";
    const std::string repeatedSelection =
        localStart +
        "  %initial = ctjs.create_array []\n"
        "  cf.br ^loop(%initial : !ctjs.value)\n"
        "^loop(%previous: !ctjs.value):\n"
        "  %selected = scf.if %flag -> (!ctjs.value) {\n" +
        localArray +
        "    scf.yield %fresh : !ctjs.value\n"
        "  } else {\n    scf.yield %previous : !ctjs.value\n  }\n";
    const std::string countedStart = "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                                     "  %name = ctjs.constant #ctjs.string<\"length\">\n"
                                     "  %guard = ctjs.create_array [%zero, %zero]\n"
                                     "  cf.br ^header(%zero : !ctjs.value)\n"
                                     "^header(%index: !ctjs.value):\n"
                                     "  %length = ctjs.get_property %guard[%name]\n"
                                     "  %test = ctjs.compare lt %index, %length\n"
                                     "  %continue = ctjs.truthy %test\n"
                                     "  cf.cond_br %continue, ^body(%index : !ctjs.value), ^exit\n"
                                     "^body(%i: !ctjs.value):\n";
    const std::string countedEnd = "  %step = ctjs.binary add %i, %one\n"
                                   "  cf.br ^header(%step : !ctjs.value)\n^exit:\n" +
                                   done;
    const std::string loopValues = localStart +
                                   "  %one = ctjs.constant #ctjs.number<4607182418800017408>\n"
                                   "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n"
                                   "  %max = ctjs.constant #ctjs.number<4751297606873776128>\n"
                                   "  %negative = ctjs.unary neg %one\n"
                                   "  %name = ctjs.constant #ctjs.string<\"length\">\n";
    const auto allocationLoop = [](const std::string & body, const std::string & start = "%zero",
                                   const std::string & stride = "%one",
                                   const std::string & elements = "%zero",
                                   const std::string & header = "",
                                   const std::string & preheader = "") {
        return "  %guard = ctjs.create_array [" + elements + "] {storage_test_id = \"guard\"}\n" +
               preheader + "  %loop:2 = scf.while (%index = " + start +
               ", %saved = %zero) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value) {\n" +
               header +
               "    %length = ctjs.get_property %guard[%name]\n"
               "    %test = ctjs.compare lt %index, %length\n"
               "    %continue = ctjs.truthy %test\n"
               "    scf.condition(%continue) %index, %saved : !ctjs.value, !ctjs.value\n"
               "  } do {\n"
               "  ^body(%i: !ctjs.value, %previous: !ctjs.value):\n" +
               body + "    %step = ctjs.binary add %i, " + stride +
               "\n    scf.yield %step, %fresh : !ctjs.value, !ctjs.value\n  }\n";
    };
    const std::string loopGraph =
        localObject + "    %fresh = ctjs.create_array [%child] {storage_test_id = \"fresh\"}\n";
    const std::string storeFresh = "    ctjs.set_property %guard[%zero], %fresh\n";
    const std::string clearGuard = "  ctjs.set_property %guard[%zero], %zero\n";
    const std::string selectedStart = "  %start = scf.if %flag -> (!ctjs.value) {\n"
                                      "    scf.yield %one : !ctjs.value\n  } else {\n";
    const std::string conditionalGraph =
        "    %fresh = scf.if %flag -> (!ctjs.value) {\n" + localObject +
        "      %selected = ctjs.create_array [%child] {storage_test_id = \"selected\"}\n"
        "      scf.yield %selected : !ctjs.value\n"
        "    } else {\n"
        "      %other = ctjs.create_array [%zero] {storage_test_id = \"other\"}\n"
        "      scf.yield %other : !ctjs.value\n    }\n";
    const std::string conditionalObject = "    scf.if %flag {\n" + localObject + "    }\n";
    const std::string conditionalStores =
        "    %fresh = scf.if %flag -> (!ctjs.value) {\n" + localObject +
        "      %selected = ctjs.create_array [%child] {storage_test_id = \"selected\"}\n"
        "      ctjs.set_property %guard[%i], %selected\n"
        "      %thenSaved = ctjs.get_property %guard[%i]\n"
        "      scf.yield %thenSaved : !ctjs.value\n"
        "    } else {\n"
        "      %other = ctjs.create_array [%zero] {storage_test_id = \"other\"}\n"
        "      ctjs.set_property %guard[%zero], %other\n"
        "      %elseSaved = ctjs.get_property %guard[%zero]\n"
        "      scf.yield %elseSaved : !ctjs.value\n    }\n";
    const auto computedStore = [&](const std::string & inputs,
                                   const std::string & receiver = "%guard") {
        return localArray + "    scf.if %flag {\n" + inputs + "      ctjs.set_property " +
               receiver + "[%key], %fresh\n    }\n";
    };
    const std::string computedKey = "      %key = ctjs.binary add %i, %zero\n";
    const std::string guardAlias =
        "  %aliases = ctjs.create_array [%guard] {storage_test_id = \"aliases\"}\n";
    const std::string loadedReceiver = "      %slot = ctjs.binary add %zero, %zero\n"
                                       "      %reloaded = ctjs.get_property %aliases[%slot]\n";
    const auto selectedInput =
        [](const std::string & left, const std::string & right, const std::string & thenBody = "",
           const std::string & elseBody = "", const std::string & result = "%key") {
            return "    " + result + " = scf.if %flag -> (!ctjs.value) {\n" + thenBody +
                   "      scf.yield " + left + " : !ctjs.value\n    } else {\n" + elseBody +
                   "      scf.yield " + right + " : !ctjs.value\n    }\n";
        };
    const auto selectedStore = [&](const std::string & input,
                                   const std::string & receiver = "%guard",
                                   const std::string & key = "%key") {
        return localArray + input + "    ctjs.set_property " + receiver + "[" + key + "], %fresh\n";
    };
    const std::string selectedTuple =
        "    %key:2 = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
        "      scf.yield %zero, %zero : !ctjs.value, !ctjs.value\n"
        "    } else {\n"
        "      scf.yield %one, %zero : !ctjs.value, !ctjs.value\n    }\n";
    const std::string unequalKeys = selectedStore(selectedInput("%zero", "%one"));
    const std::string clearBoth = clearGuard + "  ctjs.set_property %guard[%one], %zero\n";
    const std::string gapKeys = selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                              selectedInput("%read", "%two"));
    const auto arithmeticInput = [&](const std::string & left, const std::string & right,
                                     const std::string & expression = "add %selectedKey, %zero",
                                     const std::string & thenBody = "",
                                     const std::string & elseBody = "") {
        return selectedInput(left, right, thenBody, elseBody, "%selectedKey") +
               "    %key = ctjs.binary " + expression + "\n";
    };
    const std::string arithmeticKeys = selectedStore(arithmeticInput("%zero", "%one"));
    const std::string arithmeticGap = selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                                    arithmeticInput("%read", "%two"));
    const std::string correlatedKeys =
        selectedStore(arithmeticInput("%zero", "%one", "sub %selectedKey, %selectedKey"));
    const std::string correlatedGap =
        selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                      arithmeticInput("%read", "%two", "sub %selectedKey, %selectedKey"));
    const auto pairedInput = [](const std::string & thenValues, const std::string & elseValues,
                                const std::string & thenBody = "",
                                const std::string & elseBody = "") {
        return "    %paired:2 = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n" + thenBody +
               "      scf.yield " + thenValues + " : !ctjs.value, !ctjs.value\n    } else {\n" +
               elseBody + "      scf.yield " + elseValues +
               " : !ctjs.value, !ctjs.value\n    }\n"
               "    %key = ctjs.binary sub %paired#0, %paired#1\n";
    };
    const std::string pairedKeys = selectedStore(pairedInput("%zero, %zero", "%one, %one"));
    const std::string pairedGap = selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                                pairedInput("%read, %read", "%two, %two"));
    const auto siblingInput = [&](const std::string & left = "%zero",
                                  const std::string & right = "%one",
                                  const std::string & condition = "%flag",
                                  const std::string & between = "", bool reversed = false) {
        auto second =
            selectedInput(reversed ? right : left, reversed ? left : right, "", "", "%secondKey");
        second.replace(second.find("%flag"), 5, condition);
        return selectedInput(left, right, "", "", "%firstKey") + between + second +
               "    %key = ctjs.binary sub %firstKey, %secondKey\n";
    };
    const std::string siblingKeys = selectedStore(siblingInput());
    const std::string siblingGap = selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                                 siblingInput("%read", "%two"));
    const auto nestedArm = [](const std::string & input) {
        return "    scf.if %flag {\n" + input + "    }\n";
    };
    const auto nestedSiblingStore = [&](const std::string & input) {
        return localArray + nestedArm(input + "      ctjs.set_property %guard[%key], %fresh\n");
    };
    const std::string nestedSiblingKeys = nestedSiblingStore(siblingInput());
    const std::string nestedSiblingGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" + siblingInput("%read", "%two"));
    const std::string recomputedFlag = "    %secondFlag = ctjs.truthy %p\n";
    const std::string recomputedKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", recomputedFlag));
    const std::string recomputedGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", recomputedFlag));
    const auto reloadedConditions = [&](const std::string & between, bool reversed = false) {
        auto input = siblingInput("%zero", "%one", "%secondFlag",
                                  between + "    %secondRead = ctjs.get_property %guard[%zero]\n"
                                            "    %secondFlag = ctjs.truthy %secondRead\n",
                                  reversed);
        input.replace(input.find("%flag"), 5, "%firstFlag");
        return selectedStore("    %firstRead = ctjs.get_property %guard[%zero]\n"
                             "    %firstFlag = ctjs.truthy %firstRead\n" +
                             input);
    };
    const std::string negatedFlag = "    %negated = ctjs.unary not %p\n"
                                    "    %secondFlag = ctjs.truthy %negated\n";
    const std::string oppositeKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", negatedFlag, true));
    const std::string oppositeGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", negatedFlag, true));
    const std::string firstNegatedFlag = "    %firstNegated = ctjs.unary not %p\n"
                                         "    %firstFlag = ctjs.truthy %firstNegated\n";
    auto negatedFirst = siblingInput("%zero", "%one", "%flag", "", true);
    negatedFirst.replace(negatedFirst.find("%flag"), 5, "%firstFlag");
    auto repeatedNot = siblingInput("%zero", "%one", "%secondFlag", negatedFlag);
    repeatedNot.replace(repeatedNot.find("%flag"), 5, "%firstFlag");
    auto reloadedNot = reloadedConditions("", true);
    reloadedNot.replace(reloadedNot.find("    %secondFlag = ctjs.truthy %secondRead\n"),
                        std::string{"    %secondFlag = ctjs.truthy %secondRead\n"}.size(),
                        "    %negated = ctjs.unary not %secondRead\n"
                        "    %secondFlag = ctjs.truthy %negated\n");
    auto mutatedNot = reloadedNot;
    mutatedNot.insert(mutatedNot.find("    %secondRead ="),
                      "    ctjs.set_property %guard[%zero], %one\n");
    const auto notChain = [](unsigned length, std::string input = "%p") {
        std::string body;
        for (unsigned i = 0; i < length; ++i) {
            const std::string result = "%not" + std::to_string(i);
            body += "    " + result + " = ctjs.unary not " + input + "\n";
            input = result;
        }
        return body + "    %secondFlag = ctjs.truthy " + input + "\n";
    };
    const std::string chainKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", notChain(3), true));
    const std::string chainGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", notChain(3), true));
    auto firstChain = siblingInput("%zero", "%one", "%flag", "", true);
    firstChain.replace(firstChain.find("%flag"), 5, "%secondFlag");
    auto reloadedChain = reloadedConditions("", true);
    reloadedChain.replace(reloadedChain.find("    %secondFlag = ctjs.truthy %secondRead\n"),
                          std::string{"    %secondFlag = ctjs.truthy %secondRead\n"}.size(),
                          notChain(3, "%secondRead"));
    auto mutatedChain = reloadedChain;
    mutatedChain.insert(mutatedChain.find("    %secondRead ="),
                        "    ctjs.set_property %guard[%zero], %one\n");
    auto unsupportedChain = notChain(3);
    unsupportedChain.replace(unsupportedChain.find("not %p"), 6, "void %p");
    const std::string firstComparison = "    %firstCompare = ctjs.compare strict_eq %p, %zero\n"
                                        "    %firstFlag = ctjs.truthy %firstCompare\n";
    const auto comparedInput = [&](const std::string & left = "%zero",
                                   const std::string & right = "%one",
                                   const std::string & comparison = "strict_eq %p, %zero",
                                   const std::string & between = "", unsigned negations = 0) {
        auto input = siblingInput(left, right, "%secondFlag",
                                  between + "    %secondCompare = ctjs.compare " + comparison +
                                      "\n" + notChain(negations, "%secondCompare"),
                                  negations % 2 != 0);
        input.replace(input.find("%flag"), 5, "%firstFlag");
        return firstComparison + input;
    };
    const std::string comparedKeys = selectedStore(comparedInput());
    const std::string comparedGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" + comparedInput("%read", "%two"));
    const auto literalComparisons = [&](const std::string & first = "#ctjs.number<0>",
                                        const std::string & second = "#ctjs.number<0>",
                                        unsigned negations = 0) {
        auto input = comparedInput("%zero", "%one", "strict_eq %p, %secondLiteral", "", negations);
        input.replace(input.find("strict_eq %p, %zero"), std::string{"strict_eq %p, %zero"}.size(),
                      "strict_eq %p, %firstLiteral");
        return "    %firstLiteral = ctjs.constant " + first +
               "\n    %secondLiteral = ctjs.constant " + second + "\n" + input;
    };
    const std::string numberComparisonGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           comparedInput("%read", "%two", "strict_eq %p, %otherZero",
                                         "    %otherZero = ctjs.constant #ctjs.number<0>\n"));
    auto stringComparisonGap = numberComparisonGap;
    stringComparisonGap.replace(stringComparisonGap.find("#ctjs.number<0>"),
                                std::string{"#ctjs.number<0>"}.size(), "#ctjs.string<\"0\">");
    stringComparisonGap.replace(stringComparisonGap.find("strict_eq %p, %zero"),
                                std::string{"strict_eq %p, %zero"}.size(),
                                "strict_eq %p, %firstLiteral");
    stringComparisonGap.insert(0, "    %firstLiteral = ctjs.constant #ctjs.string<\"0\">\n");
    const auto reversedInput = [&](const std::string & left = "%zero",
                                   const std::string & right = "%one", unsigned negations = 0) {
        return comparedInput(left, right, "strict_eq %zero, %p", "", negations);
    };
    const std::string reversedKeys = selectedStore(reversedInput());
    const std::string reversedGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" + reversedInput("%read", "%two"));
    auto firstReversed = comparedInput();
    firstReversed.replace(firstReversed.find("strict_eq %p, %zero"),
                          std::string{"strict_eq %p, %zero"}.size(), "strict_eq %zero, %p");
    auto eagerComparisons = comparedInput();
    const std::string secondComparison = "    %secondCompare = ctjs.compare strict_eq %p, %zero\n"
                                         "    %secondFlag = ctjs.truthy %secondCompare\n";
    eagerComparisons.erase(eagerComparisons.find(secondComparison), secondComparison.size());
    eagerComparisons.insert(eagerComparisons.find("    %firstKey"), secondComparison);
    auto crossBlockComparisons = comparedInput();
    crossBlockComparisons.insert(crossBlockComparisons.find("    %secondCompare"),
                                 "    scf.if %flag {\n");
    const auto crossBlockStore = [&](std::string input, const std::string & after = "") {
        input.insert(input.find("    %secondCompare"), "    scf.if %flag {\n");
        return selectedStore(input) + after + "    }\n";
    };
    const std::string crossBlockKeys = crossBlockStore(comparedInput());
    const std::string crossBlockGap = crossBlockStore(
        "    %read = ctjs.get_property %guard[%one]\n" + comparedInput("%read", "%two"));
    const std::string outerRepresentative =
        "    %outerCompare = ctjs.compare strict_eq %zero, %p\n" +
        nestedSiblingStore(crossBlockComparisons) + "    }\n";
    const auto comparedReads = [&](const std::string & between = "", bool reversed = false) {
        auto input = comparedInput("%zero", "%one",
                                   reversed ? "strict_eq %zero, %secondRead"
                                            : "strict_eq %secondRead, %zero",
                                   between + "    %secondRead = ctjs.get_property %guard[%zero]\n");
        input.replace(input.find("strict_eq %p, %zero"), std::string{"strict_eq %p, %zero"}.size(),
                      "strict_eq %firstRead, %zero");
        return selectedStore("    %firstRead = ctjs.get_property %guard[%zero]\n" + input);
    };
    auto crossBlockReads = comparedReads("    ctjs.set_property %guard[%zero], %one\n", true);
    crossBlockReads.insert(crossBlockReads.find("    %secondRead"), "    scf.if %flag {\n");
    crossBlockReads += "    }\n";
    const std::string booleanFlag = "    %boolean = ctjs.convert to_boolean %p\n"
                                    "    %secondFlag = ctjs.truthy %boolean\n";
    const std::string booleanKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", booleanFlag));
    const std::string booleanGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", booleanFlag));
    const std::string booleanNot = "    %boolean = ctjs.convert to_boolean %p\n"
                                   "    %negated = ctjs.unary not %boolean\n"
                                   "    %boxed = ctjs.convert to_boolean %negated\n"
                                   "    %secondFlag = ctjs.truthy %boxed\n";
    auto booleanComparison = comparedInput();
    booleanComparison.replace(booleanComparison.find("    %secondFlag ="),
                              std::string{"    %secondFlag = ctjs.truthy %secondCompare\n"}.size(),
                              "    %boolean = ctjs.convert to_boolean %secondCompare\n"
                              "    %secondFlag = ctjs.truthy %boolean\n");
    auto unrelatedBoolean = booleanFlag;
    unrelatedBoolean.replace(unrelatedBoolean.find("%p"), 2, "%q");
    auto numericCondition = booleanFlag;
    numericCondition.replace(numericCondition.find("to_boolean"), 10, "to_number");
    const auto strictBooleanFlag = [](const std::string & input = "%boolean",
                                      const std::string & literal = "#ctjs.boolean<true>",
                                      const std::string & producer =
                                          "    %boolean = ctjs.convert to_boolean %p\n",
                                      bool reversed = false) {
        return producer + "    %booleanLiteral = ctjs.constant " + literal +
               "\n    %booleanTest = ctjs.compare strict_eq " +
               (reversed ? "%booleanLiteral, " + input : input + ", %booleanLiteral") +
               "\n    %secondFlag = ctjs.truthy %booleanTest\n";
    };
    const std::string strictBooleanKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", strictBooleanFlag()));
    const std::string strictBooleanGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", strictBooleanFlag()));
    auto strictBooleanReads = reloadedConditions("    ctjs.set_property %guard[%zero], %one\n");
    strictBooleanReads.replace(
        strictBooleanReads.find("    %secondFlag = ctjs.truthy %secondRead\n"),
        std::string{"    %secondFlag = ctjs.truthy %secondRead\n"}.size(),
        strictBooleanFlag("%boolean", "#ctjs.boolean<true>",
                          "    %boolean = ctjs.convert to_boolean %secondRead\n"));
    const auto forwardedFlag =
        [&](const std::string & thenValue = "%p", const std::string & elseValue = "%p",
            const std::string & thenBody = "", const std::string & elseBody = "") {
            auto input = selectedInput(thenValue, elseValue, thenBody, elseBody, "%forwarded");
            input.replace(input.find("%flag"), 5, "%transportFlag");
            return "    %transportFlag = ctjs.truthy %q\n" + input +
                   "    %secondFlag = ctjs.truthy %forwarded\n";
        };
    const std::string forwardedKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", forwardedFlag()));
    const std::string forwardedGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", forwardedFlag()));
    const std::string forwardedTuple =
        "    %transportFlag = ctjs.truthy %q\n"
        "    %forwarded:2 = scf.if %transportFlag -> (!ctjs.value, !ctjs.value) {\n"
        "      scf.yield %zero, %p : !ctjs.value, !ctjs.value\n"
        "    } else {\n"
        "      scf.yield %one, %p : !ctjs.value, !ctjs.value\n"
        "    }\n"
        "    %secondFlag = ctjs.truthy %forwarded#1\n";
    auto forwardedNot = forwardedFlag("%negated", "%negated");
    forwardedNot.insert(0, "    %boolean = ctjs.convert to_boolean %p\n"
                           "    %negated = ctjs.unary not %boolean\n");
    forwardedNot.replace(forwardedNot.find("    %secondFlag ="),
                         std::string{"    %secondFlag = ctjs.truthy %forwarded\n"}.size(),
                         selectedInput("%forwarded", "%forwarded", "", "", "%again") +
                             "    %secondFlag = ctjs.truthy %again\n");
    auto forwardedComparison = comparedInput();
    forwardedComparison.replace(
        forwardedComparison.find("    %secondFlag ="),
        std::string{"    %secondFlag = ctjs.truthy %secondCompare\n"}.size(),
        forwardedFlag("%secondCompare", "%secondCompare"));
    const std::string thenBoolean = "      %thenBoolean = ctjs.convert to_boolean %p\n";
    const std::string elseBoolean = "      %elseNot = ctjs.unary not %p\n"
                                    "      %elseEven = ctjs.unary not %elseNot\n";
    const std::string booleanYields =
        forwardedFlag("%thenBoolean", "%elseEven", thenBoolean, elseBoolean);
    const std::string booleanYieldKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", booleanYields));
    const std::string booleanYieldGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", booleanYields));
    const std::string booleanYieldTuple =
        "    %transportFlag = ctjs.truthy %q\n"
        "    %forwarded:2 = scf.if %transportFlag -> (!ctjs.value, !ctjs.value) {\n" +
        thenBoolean +
        "      scf.yield %zero, %thenBoolean : !ctjs.value, !ctjs.value\n"
        "    } else {\n" +
        elseBoolean +
        "      scf.yield %one, %elseEven : !ctjs.value, !ctjs.value\n"
        "    }\n"
        "    %secondFlag = ctjs.truthy %forwarded#1\n";
    const std::string negatedYields =
        forwardedFlag("%thenNot", "%elseNot", "      %thenNot = ctjs.unary not %p\n",
                      "      %elseBoolean = ctjs.convert to_boolean %p\n"
                      "      %elseNot = ctjs.unary not %elseBoolean\n");
    auto booleanYieldComparison = comparedInput();
    booleanYieldComparison.replace(
        booleanYieldComparison.find("    %secondFlag ="),
        std::string{"    %secondFlag = ctjs.truthy %secondCompare\n"}.size(),
        forwardedFlag("%thenBoolean", "%elseEven",
                      "      %thenBoolean = ctjs.convert to_boolean %secondCompare\n",
                      "      %elseNot = ctjs.unary not %secondCompare\n"
                      "      %elseEven = ctjs.unary not %elseNot\n"));
    const auto comparisonYields = [&](const std::string & thenComparison = "strict_eq %p, %zero",
                                      const std::string & elseComparison = "strict_eq %p, %zero",
                                      const std::string & thenBody = "",
                                      const std::string & elseBody = "") {
        return forwardedFlag(
            "%thenBoolean", "%elseEven",
            "      %thenCompare = ctjs.compare " + thenComparison + "\n" + thenBody +
                "      %thenBoolean = ctjs.convert to_boolean %thenCompare\n",
            "      %elseCompare = ctjs.compare " + elseComparison + "\n" + elseBody +
                "      %elseNot = ctjs.unary not %elseCompare\n"
                "      %elseEven = ctjs.unary not %elseNot\n");
    };
    const auto representativeInput = [&](const std::string & forwarding,
                                         const std::string & left = "%zero",
                                         const std::string & right = "%one") {
        auto input = comparedInput(left, right);
        input.replace(input.find(secondComparison), secondComparison.size(), forwarding);
        return input;
    };
    const std::string representativeComparisons = representativeInput(comparisonYields());
    const std::string representativeKeys = selectedStore(representativeComparisons);
    const std::string representativeGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           representativeInput(comparisonYields(), "%read", "%two"));
    auto oppositeComparisonYield = comparisonYields();
    oppositeComparisonYield.replace(oppositeComparisonYield.find("scf.yield %elseEven"),
                                    std::string{"scf.yield %elseEven"}.size(),
                                    "scf.yield %elseNot");
    const auto independentComparisons = [&](std::string input, bool negated = false) {
        input.replace(
            input.find(firstComparison), firstComparison.size(),
            selectedInput(
                negated ? "%localThenNot" : "%localThen", negated ? "%localElseNot" : "%localElse",
                "      %localThen = ctjs.compare strict_eq %p, %zero\n" +
                    std::string(negated ? "      %localThenNot = ctjs.unary not %localThen\n" : ""),
                "      %localElse = ctjs.compare strict_eq %p, %zero\n" +
                    std::string(negated ? "      %localElseNot = ctjs.unary not %localElse\n" : ""),
                "%local") +
                "    %firstFlag = ctjs.truthy %local\n");
        return input;
    };
    const std::string siblingOnlyComparisons = independentComparisons(representativeComparisons);
    const std::string siblingComparisonKeys = selectedStore(siblingOnlyComparisons);
    const std::string siblingComparisonGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" +
        independentComparisons(representativeInput(comparisonYields(), "%read", "%two")));
    const std::string literalComparisonYields =
        "    %otherZero = ctjs.constant #ctjs.number<0>\n"
        "    %thirdZero = ctjs.constant #ctjs.number<0>\n" +
        comparisonYields("strict_eq %p, %otherZero", "strict_eq %thirdZero, %p");
    auto reversedSiblingComparisons = independentComparisons(representativeComparisons, true);
    auto firstNumbers = selectedInput("%zero", "%one", "", "", "%firstKey");
    firstNumbers.replace(firstNumbers.find("%flag"), 5, "%firstFlag");
    auto reversedNumbers = selectedInput("%one", "%zero", "", "", "%firstKey");
    reversedNumbers.replace(reversedNumbers.find("%flag"), 5, "%firstFlag");
    reversedSiblingComparisons.replace(reversedSiblingComparisons.find(firstNumbers),
                                       firstNumbers.size(), reversedNumbers);
    const auto literalSelection =
        [&](const std::string & thenValue = "%yes", const std::string & elseValue = "%no",
            const std::string & thenBody = "", const std::string & elseBody = "",
            const std::string & conditionInput = "%p") {
            auto input = forwardedFlag(thenValue, elseValue, thenBody, elseBody);
            input.replace(input.find("ctjs.truthy %q"), std::string{"ctjs.truthy %q"}.size(),
                          "ctjs.truthy " + conditionInput);
            return "    %yes = ctjs.constant #ctjs.boolean<true>\n"
                   "    %no = ctjs.constant #ctjs.boolean<false>\n" +
                   input;
        };
    const std::string literalKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", literalSelection()));
    const std::string literalGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", literalSelection()));
    auto literalTuple = literalSelection();
    literalTuple.replace(literalTuple.find("%forwarded ="),
                         std::string{"%forwarded = scf.if %transportFlag -> (!ctjs.value)"}.size(),
                         "%forwarded:2 = scf.if %transportFlag -> (!ctjs.value, !ctjs.value)");
    literalTuple.replace(literalTuple.find("scf.yield %yes : !ctjs.value"),
                         std::string{"scf.yield %yes : !ctjs.value"}.size(),
                         "scf.yield %zero, %yes : !ctjs.value, !ctjs.value");
    literalTuple.replace(literalTuple.find("scf.yield %no : !ctjs.value"),
                         std::string{"scf.yield %no : !ctjs.value"}.size(),
                         "scf.yield %one, %no : !ctjs.value, !ctjs.value");
    literalTuple.replace(literalTuple.find("ctjs.truthy %forwarded"),
                         std::string{"ctjs.truthy %forwarded"}.size(), "ctjs.truthy %forwarded#1");
    const auto strictCarrier = [&](std::string forwarding, const std::string & value = "%forwarded",
                                   const std::string & literal = "#ctjs.boolean<true>",
                                   bool reversed = false) {
        forwarding.erase(forwarding.find("    %secondFlag ="));
        return strictBooleanFlag(value, literal, forwarding, reversed);
    };
    const auto carrierFlag = strictCarrier(booleanYields);
    const auto carrierKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", carrierFlag));
    const auto carrierGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", carrierFlag));
    auto carrierReads = strictBooleanReads;
    carrierReads.replace(carrierReads.find("    %booleanLiteral ="),
                         carrierReads.find("    %secondKey =") -
                             carrierReads.find("    %booleanLiteral ="),
                         strictCarrier(forwardedFlag("%boolean", "%boolean")));
    const auto nestedCarrier = [&](std::string forwarding, const std::string & value = "%forwarded",
                                   const std::string & literal = "#ctjs.boolean<true>",
                                   bool reversed = false) {
        forwarding.erase(forwarding.find("    %secondFlag ="));
        return strictBooleanFlag("%again", literal,
                                 forwarding + selectedInput(value, value, "", "", "%again"),
                                 reversed);
    };
    const auto nestedCarrierFlag = nestedCarrier(booleanYields);
    const auto nestedCarrierKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", nestedCarrierFlag));
    const auto nestedCarrierGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", nestedCarrierFlag));
    auto nestedCarrierReads = strictBooleanReads;
    nestedCarrierReads.replace(nestedCarrierReads.find("    %booleanLiteral ="),
                               nestedCarrierReads.find("    %secondKey =") -
                                   nestedCarrierReads.find("    %booleanLiteral ="),
                               nestedCarrier(forwardedFlag("%boolean", "%boolean")));
    const auto identityCarrier = [&](const std::string & inner,
                                     const std::string & result = "%identity",
                                     const std::string & other = "%boolean") {
        return "    %boolean = ctjs.convert to_boolean %p\n" + forwardedFlag(result, other, inner);
    };
    const auto identityYield = selectedInput("%boolean", "%boolean", "", "", "%identity");
    const auto identityFlag = identityCarrier(identityYield);
    const auto identityKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", identityFlag));
    const auto identityGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", identityFlag));
    const std::string identityTuple =
        "      %identity:2 = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
        "        scf.yield %zero, %boolean : !ctjs.value, !ctjs.value\n"
        "      } else {\n"
        "        scf.yield %one, %boolean : !ctjs.value, !ctjs.value\n"
        "      }\n";
    auto identityReads = strictBooleanReads;
    auto readIdentity = strictCarrier(identityFlag);
    readIdentity.replace(readIdentity.find("to_boolean %p"), std::string{"to_boolean %p"}.size(),
                         "to_boolean %secondRead");
    identityReads.replace(identityReads.find("    %boolean ="),
                          identityReads.find("    %secondKey =") -
                              identityReads.find("    %boolean ="),
                          readIdentity);
    const auto operandCarrier = selectedInput("%p", "%p", "", "", "%operand");
    const auto operandKeys =
        selectedStore(comparedInput("%zero", "%one", "strict_eq %operand, %zero", operandCarrier));
    const auto operandGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" +
        comparedInput("%read", "%two", "strict_eq %operand, %zero", operandCarrier));
    const std::string operandTuple =
        "    %operand:2 = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
        "      scf.yield %zero, %p : !ctjs.value, !ctjs.value\n"
        "    } else {\n"
        "      scf.yield %one, %p : !ctjs.value, !ctjs.value\n"
        "    }\n";
    auto operandReads = comparedReads();
    operandReads.replace(operandReads.find("strict_eq %secondRead, %zero"),
                         std::string{"strict_eq %secondRead, %zero"}.size(),
                         "strict_eq %operand, %zero");
    operandReads.insert(operandReads.find("    %secondCompare"),
                        selectedInput("%secondRead", "%secondRead", "", "", "%operand"));
    const auto nestedOperandForwarding = selectedInput(
        "%innerOperand", "%p", selectedInput("%p", "%p", "", "", "%innerOperand"), "", "%operand");
    const auto nestedTruthiness =
        selectedInput("%operand", "%p", nestedOperandForwarding, "", "%truthiness") +
        "    %secondFlag = ctjs.truthy %truthiness\n";
    const auto nestedTruthinessGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", nestedTruthiness));
    auto nestedTruthinessNot = nestedTruthiness;
    nestedTruthinessNot.replace(nestedTruthinessNot.find("    %secondFlag ="),
                                std::string{"    %secondFlag = ctjs.truthy %truthiness\n"}.size(),
                                notChain(3, "%truthiness"));
    const auto nestedTruthinessTuple =
        selectedInput("%middleOperand", "%p",
                      selectedInput("%operand#1", "%p", operandTuple, "", "%middleOperand"), "",
                      "%truthiness") +
        "    %secondFlag = ctjs.truthy %truthiness\n";
    auto otherTruthinessSlot = nestedTruthinessTuple;
    otherTruthinessSlot.replace(otherTruthinessSlot.find("%operand#1"),
                                std::string{"%operand#1"}.size(), "%operand#0");
    auto nestedTruthinessReads = reloadedConditions("");
    nestedTruthinessReads.replace(
        nestedTruthinessReads.find("    %secondFlag ="),
        std::string{"    %secondFlag = ctjs.truthy %secondRead\n"}.size(),
        selectedInput(
            "%readOperand", "%secondRead",
            selectedInput("%innerRead", "%secondRead",
                          selectedInput("%secondRead", "%secondRead", "", "", "%innerRead"), "",
                          "%readOperand"),
            "", "%truthiness") +
            "    %secondFlag = ctjs.truthy %truthiness\n");
    auto nestedTruthinessThenEffect = nestedTruthiness;
    nestedTruthinessThenEffect.insert(nestedTruthinessThenEffect.find("      scf.yield %p"),
                                      "      %called = ctjs.call %p(%q)\n");
    auto nestedTruthinessElseEffect = nestedTruthiness;
    nestedTruthinessElseEffect.insert(nestedTruthinessElseEffect.find("    } else {\n") +
                                          std::string{"    } else {\n"}.size(),
                                      "      %called = ctjs.call %p(%q)\n");
    const auto nestedOperandGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" +
        comparedInput("%read", "%two", "strict_eq %operand, %zero", nestedOperandForwarding));
    auto nestedOperandReads = comparedReads();
    nestedOperandReads.replace(nestedOperandReads.find("strict_eq %secondRead, %zero"),
                               std::string{"strict_eq %secondRead, %zero"}.size(),
                               "strict_eq %operand, %zero");
    nestedOperandReads.insert(
        nestedOperandReads.find("    %secondCompare"),
        selectedInput("%innerOperand", "%secondRead",
                      selectedInput("%secondRead", "%secondRead", "", "", "%innerOperand"), "",
                      "%operand"));
    const auto equalOperandForwarding =
        selectedInput("%thenZero", "%elseZero", "      %thenZero = ctjs.constant #ctjs.number<0>\n",
                      "      %elseZero = ctjs.constant #ctjs.number<0>\n", "%operand");
    const auto equalOperandKeys = selectedStore(
        comparedInput("%zero", "%one", "strict_eq %p, %operand", equalOperandForwarding));
    const auto equalOperandGap = nestedSiblingStore(
        "      %read = ctjs.get_property %guard[%one]\n" +
        comparedInput("%read", "%two", "strict_eq %p, %operand", equalOperandForwarding));
    const auto primitiveOperandInput = [&](const std::string & attribute,
                                           const std::string & other = "") {
        auto input =
            comparedInput("%zero", "%one", "strict_eq %p, %operand",
                          selectedInput("%thenPrimitive", "%elsePrimitive",
                                        "      %thenPrimitive = ctjs.constant " + attribute + "\n",
                                        "      %elsePrimitive = ctjs.constant " +
                                            (other.empty() ? attribute : other) + "\n",
                                        "%operand"));
        input.replace(input.find("strict_eq %p, %zero"), std::string{"strict_eq %p, %zero"}.size(),
                      "strict_eq %p, %firstPrimitive");
        return "    %firstPrimitive = ctjs.constant " + attribute + "\n" + input;
    };
    const std::string equalOperandTuple =
        "    %operandTuple:2 = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
        "      %thenZero = ctjs.constant #ctjs.number<0>\n"
        "      scf.yield %one, %thenZero : !ctjs.value, !ctjs.value\n"
        "    } else {\n"
        "      %elseZero = ctjs.constant #ctjs.number<0>\n"
        "      scf.yield %zero, %elseZero : !ctjs.value, !ctjs.value\n"
        "    }\n";
    auto equalOperandReads = comparedReads();
    equalOperandReads.replace(equalOperandReads.find("strict_eq %secondRead, %zero"),
                              std::string{"strict_eq %secondRead, %zero"}.size(),
                              "strict_eq %secondRead, %operand");
    equalOperandReads.insert(equalOperandReads.find("    %secondCompare"), equalOperandForwarding);
    auto pairedOperandInput =
        comparedInput("%zero", "%one", "strict_eq %p, %operand", equalOperandForwarding);
    pairedOperandInput.replace(pairedOperandInput.find("strict_eq %p, %zero"),
                               std::string{"strict_eq %p, %zero"}.size(),
                               "strict_eq %p, %earlierOperand");
    pairedOperandInput =
        selectedInput("%earlierThen", "%earlierElse",
                      "      %earlierThen = ctjs.constant #ctjs.number<0>\n",
                      "      %earlierElse = ctjs.constant #ctjs.number<0>\n", "%earlierOperand") +
        pairedOperandInput;
    const auto carryLiteral = [&](std::string input, const std::string & forwarding) {
        auto comparison = input.find("    %booleanTest =");
        input.replace(input.find("%booleanLiteral", comparison),
                      std::string{"%booleanLiteral"}.size(), "%literalOperand");
        input.insert(comparison, forwarding);
        return input;
    };
    const auto literalForwarding =
        selectedInput("%booleanLiteral", "%booleanLiteral", "", "", "%literalOperand");
    const auto literalOperandFlag = carryLiteral(strictBooleanFlag(), literalForwarding);
    const auto literalOperandKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", literalOperandFlag));
    const auto literalOperandGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", literalOperandFlag));
    const auto equalLiteralForwarding = selectedInput(
        "%thenLiteral", "%elseLiteral", "      %thenLiteral = ctjs.constant #ctjs.boolean<true>\n",
        "      %elseLiteral = ctjs.constant #ctjs.boolean<true>\n", "%literalOperand");
    const auto equalLiteralFlag = carryLiteral(strictBooleanFlag(), equalLiteralForwarding);
    const auto equalLiteralKeys =
        selectedStore(siblingInput("%zero", "%one", "%secondFlag", equalLiteralFlag));
    const auto equalLiteralGap =
        nestedSiblingStore("      %read = ctjs.get_property %guard[%one]\n" +
                           siblingInput("%read", "%two", "%secondFlag", equalLiteralFlag));
    const auto equalFalseForwarding = selectedInput(
        "%thenLiteral", "%elseLiteral", "      %thenLiteral = ctjs.constant #ctjs.boolean<false>\n",
        "      %elseLiteral = ctjs.constant #ctjs.boolean<false>\n", "%literalOperand");
    const std::string equalLiteralTuple =
        "    %literalTuple:2 = scf.if %flag -> (!ctjs.value, !ctjs.value) {\n"
        "      %thenLiteral = ctjs.constant #ctjs.boolean<true>\n"
        "      scf.yield %zero, %thenLiteral : !ctjs.value, !ctjs.value\n"
        "    } else {\n"
        "      %elseLiteral = ctjs.constant #ctjs.boolean<true>\n"
        "      scf.yield %one, %elseLiteral : !ctjs.value, !ctjs.value\n"
        "    }\n";
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
        {.what = "an overwritten implicit Stored capture becomes confined",
         .body = clearedCapture + "  ctjs.return %a\n",
         .discharged = "x"},
        {.what = "an uncleared structured arm keeps the child's original capture witness",
         .body = capturedStore + "  ctjs.return %a\n"},
        {.what = "a separately returned captured child keeps its original witness",
         .body = clearedCapture + "  ctjs.return %x\n"},
        {.what = "a later yielded capture keeps the child on every retaining return path",
         .body = clearedCapture + "  %saved = scf.if %flag -> (!ctjs.value) {\n"
                                  "    scf.yield %x : !ctjs.value\n"
                                  "  } else {\n"
                                  "    scf.yield %zero : !ctjs.value\n"
                                  "  }\n"
                                  "  ctjs.return %saved\n"},
        {.what = "late publication prevents refinement of an earlier Stored capture",
         .body = clearedCapture + "  ctjs.store_global \"held\", %x\n" + done,
         .complete = false},
        {.what = "later implicit publication cannot hide behind the first Stored capture",
         .body = captureStart + captureWrite + "    ctjs.store_global \"held\", %x\n" + captureEnd +
                 done,
         .complete = false},
        {.what = "a transient structured cycle keeps even an overwritten child refused",
         .body = captureStart + "    ctjs.set_property %a[%zero], %a\n" + captureWrite +
                 captureEnd + done,
         .complete = false},
        {.what = "an earlier non-storage capture cannot borrow a later Stored role",
         .body = captureStart + "    %used = ctjs.truthy %x\n" + captureWrite + captureEnd + done,
         .confinedReceivers = "a"},
        {.what = "an earlier global capture cannot borrow a later Stored role",
         .body = captureStart + "    ctjs.store_global \"held\", %x\n" + captureWrite + captureEnd +
                 done,
         .complete = false},
        {.what = "captured storage and a single-instance nested origin refine together",
         .body = captureStart + "    %fresh = ctjs.create_array []\n" + captureWrite + captureEnd +
                 done,
         .discharged = "x",
         .confinedNested = "ctjs.create_array"},
        {.what = "an exact nested own-element write confines its private array receiver",
         .body = receiverStart + receiverWrite + "  }\n" + done,
         .confinedReceivers = "a"},
        {.what = "an exact nested own-element read confines its private array receiver",
         .body = receiverStart + receiverRead + "  }\n" + done,
         .confinedReceivers = "a"},
        {.what = "a directly returned nested receiver retains its original capture witness",
         .body = receiverStart + receiverWrite + "  }\n  ctjs.return %a\n",
         .confinedReceivers = ""},
        {.what = "a later returned selection retains the captured receiver on either path",
         .body = receiverStart + receiverRead +
                 "  }\n"
                 "  %saved = scf.if %flag -> (!ctjs.value) {\n"
                 "    scf.yield %a : !ctjs.value\n"
                 "  } else {\n"
                 "    scf.yield %zero : !ctjs.value\n"
                 "  }\n"
                 "  ctjs.return %saved\n",
         .confinedReceivers = ""},
        {.what = "late implicit publication cannot hide behind an own receiver access",
         .body = receiverStart + receiverRead + "    ctjs.store_global \"held\", %a\n  }\n" + done,
         .complete = false,
         .confinedReceivers = ""},
        {.what = "a nested array receiver still requires an existing own write slot",
         .body = receiverStart + "    ctjs.set_property %a[%one], %zero\n  }\n" + done,
         .complete = false,
         .confinedReceivers = ""},
        {.what = "a nested array receiver still requires an exact own read index",
         .body = receiverStart + "    %read = ctjs.get_property %a[%p]\n  }\n" + done,
         .complete = false,
         .confinedReceivers = ""},
        {.what = "an earlier nonreceiver capture cannot borrow a later receiver role",
         .body = receiverStart + "    %used = ctjs.truthy %a\n" + receiverRead + "  }\n" + done,
         .complete = false,
         .confinedReceivers = ""},
        {.what = "a captured array key cannot borrow the receiver operand's role",
         .body = receiverStart + "    %read = ctjs.get_property %p[%a]\n  }\n" + done,
         .complete = false,
         .confinedReceivers = ""},
        {.what = "a captured append receiver remains outside Get/Set refinement",
         .body = receiverStart + "    ctjs.append %one to %a\n  }\n" + done,
         .complete = false,
         .confinedReceivers = ""},
        {.what = "a private receiver with an overwritten structured cycle remains refused",
         .body = receiverStart + "    ctjs.set_property %a[%zero], %a\n" + receiverWrite + "  }\n" +
                 done,
         .complete = false,
         .confinedReceivers = ""},
        nestedOrigins,
        {.what = "nested objects have one frame-local instance through two conditional ancestors",
         .body = localStart + "  scf.if %flag {\n    scf.if %flag {\n" + localObject +
                 "    }\n  }\n" + done,
         .confinedNested = "child"},
        {.what = "conditional array identities survive region exit and a selected own read",
         .body = selectedArrays + "  %read = ctjs.get_property %selected[%zero]\n"
                                  "  ctjs.return %read\n",
         .confinedNested = "fresh,other"},
        {.what = "returning a selected conditional instance retains both allocation origins",
         .body = selectedArrays + "  ctjs.return %selected\n"},
        {.what = "a returned outer container retains its selected conditional child",
         .body = selectedArrays + "  %outer = ctjs.create_array [%selected]\n"
                                  "  ctjs.return %outer\n"},
        {.what = "an overwritten conditional child is confined to the containing frame",
         .body = nestedStore + "  ctjs.set_property %a[%zero], %zero\n  ctjs.return %a\n",
         .confinedNested = "fresh"},
        {.what = "a saved read retains its conditional child after the container is cleared",
         .body = nestedStore + "  %saved = ctjs.get_property %a[%zero]\n"
                               "  ctjs.set_property %a[%zero], %zero\n"
                               "  ctjs.return %saved\n",
         .confinedReceivers = "a"},
        {.what = "publication after a conditional yield refuses every nested refinement",
         .body = selectedArrays + "  ctjs.store_global \"held\", %selected\n" + done,
         .complete = false},
        {.what = "an earlier local access does not hide later nested publication",
         .body = localStart + "  scf.if %flag {\n" + localArray +
                 "    %read = ctjs.get_property %fresh[%zero]\n"
                 "    ctjs.store_global \"held\", %fresh\n  }\n" +
                 done,
         .complete = false},
        {.what = "a transient conditional cycle refuses all instance refinements",
         .body = localStart + "  scf.if %flag {\n" + localArray +
                 "    ctjs.set_property %fresh[%zero], %fresh\n"
                 "    ctjs.set_property %fresh[%zero], %zero\n  }\n" +
                 done,
         .complete = false},
        {.what = "a conditional nested allocation still requires current solver lattices",
         .body = localStart + "  scf.if %flag {\n" + localArray + "  }\n" + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "a dead conditional allocation cannot borrow a live instance lattice",
         .body = "  %zero = ctjs.constant #ctjs.number<0>\n  %no = arith.constant false\n"
                 "  scf.if %no {\n" +
                 localArray + "  }\n" + done,
         .complete = false},
        {.what = "conditional instances in a repeated structured loop remain unsupported",
         .body = localStart +
                 "  scf.while : () -> () {\n    scf.condition(%flag)\n"
                 "  } do {\n    scf.if %flag {\n" +
                 localArray + "    }\n    scf.yield\n  }\n" + done,
         .complete = false},
        {.what = "conditional instances across root CFG blocks reuse complete path nonrepetition",
         .body =
             localStart + "  cf.br ^next\n^next:\n  scf.if %flag {\n" + localArray + "  }\n" + done,
         .confinedNested = "fresh"},
        {.what = "a shared diamond join creates at most one conditional instance per call",
         .body = localStart +
                 "  cf.cond_br %flag, ^left, ^right\n"
                 "^left:\n  cf.br ^join\n^right:\n  cf.br ^join\n"
                 "^join:\n  scf.if %flag {\n" +
                 localArray + "  }\n" + done,
         .confinedNested = "fresh"},
        {.what = "successor own reads preserve selected conditional identities",
         .body = transportedSelection + "  %read = ctjs.get_property %base[%zero]\n"
                                        "  ctjs.return %read\n",
         .confinedNested = "fresh,other"},
        {.what = "a successor return retains both selected conditional origins",
         .body = transportedSelection + "  ctjs.return %base\n"},
        {.what = "every duplicate successor edge retains its own selected operand",
         .body = selectedArrays + "  cf.cond_br %flag, ^next(%zero : !ctjs.value), "
                                  "^next(%selected : !ctjs.value)\n"
                                  "^next(%saved: !ctjs.value):\n  ctjs.return %saved\n"},
        {.what = "one retaining exit prevents confinement despite an earlier primitive exit",
         .body = selectedArrays + "  cf.cond_br %flag, ^plain, ^retain\n"
                                  "^plain:\n  ctjs.return %zero\n"
                                  "^retain:\n  ctjs.return %selected\n"},
        {.what = "successor replacement releases the conditional child from a returned holder",
         .body = transportedStore + "  ctjs.set_property %base[%zero], %zero\n"
                                    "  ctjs.return %base\n",
         .confinedNested = "fresh"},
        {.what = "successor saved reads retain the child after its holder is cleared",
         .body = transportedStore + "  %saved = ctjs.get_property %base[%zero]\n"
                                    "  ctjs.set_property %base[%zero], %zero\n"
                                    "  ctjs.return %saved\n",
         .confinedReceivers = "a"},
        {.what = "successor publication refuses all conditional origin refinements",
         .body = transportedSelection + "  ctjs.store_global \"held\", %base\n" + done,
         .complete = false},
        {.what = "transient successor cycles cannot borrow acyclic CFG authority",
         .body = transportedSelection +
                 "  ctjs.set_property %base[%zero], %base\n"
                 "  ctjs.set_property %base[%zero], %zero\n" +
                 done,
         .complete = false},
        {.what = "backedges cannot conflate the saved and current conditional instances",
         .body = repeatedSelection + "  cf.cond_br %flag, ^loop(%selected : !ctjs.value), "
                                     "^exit(%previous : !ctjs.value)\n"
                                     "^exit(%saved: !ctjs.value):\n  ctjs.return %saved\n",
         .complete = false},
        {.what = "a solver-dead backedge still cannot grant conditional instance authority",
         .body = repeatedSelection + "  %no = arith.constant false\n"
                                     "  cf.cond_br %no, ^loop(%selected : !ctjs.value), "
                                     "^exit(%previous : !ctjs.value)\n"
                                     "^exit(%saved: !ctjs.value):\n  ctjs.return %saved\n",
         .complete = false},
        {.what = "a separate counted loop cannot repeat preheader conditional allocations",
         .body = selectedArrays + countedStart + countedEnd,
         .confinedNested = "fresh,other"},
        {.what = "the counted CFG exception still rejects repeated conditional allocations",
         .body =
             localStart + countedStart + "  scf.if %flag {\n" + localArray + "  }\n" + countedEnd,
         .complete = false},
        {.what = "one counted structured iteration creates one frame-confined array",
         .body = loopValues + allocationLoop(localArray) + done,
         .confinedReceivers = "guard",
         .confinedNested = "fresh"},
        {.what = "one counted iteration confines an object and its local array graph",
         .body = loopValues + allocationLoop(loopGraph) + done,
         .confinedNested = "child,fresh"},
        {.what = "a yielded counted allocation keeps both returned graph identities",
         .body = loopValues + allocationLoop(loopGraph) + "  ctjs.return %loop#1\n"},
        {.what = "a returned holder retains the exactly-once allocation graph",
         .body = loopValues + allocationLoop(loopGraph + storeFresh) + "  ctjs.return %guard\n"},
        {.what = "overwriting a holder confines its exactly-once allocation graph",
         .body = loopValues + allocationLoop(loopGraph + storeFresh) + clearGuard +
                 "  ctjs.return %guard\n",
         .confinedNested = "child,fresh"},
        {.what = "a saved counted alias retains the graph after its holder is overwritten",
         .body = loopValues + allocationLoop(loopGraph + storeFresh) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "late publication cannot borrow exactly-once allocation authority",
         .body = loopValues + allocationLoop(loopGraph) +
                 "  ctjs.store_global \"held\", %loop#1\n" + done,
         .complete = false},
        {.what = "a transient counted cycle remains refused after the holder is cleared",
         .body = loopValues +
                 allocationLoop("    %fresh = ctjs.create_array [%guard]\n" + storeFresh) +
                 clearGuard + done,
         .complete = false},
        {.what = "a zero-trip body has no proved allocation instance",
         .body = loopValues + allocationLoop(localArray, "%one") + done,
         .complete = false},
        {.what = "two iterations cannot conflate their allocation instances",
         .body = loopValues + allocationLoop(localArray, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "a positive start with one remaining iteration supplies one instance",
         .body = loopValues + allocationLoop(localArray, "%one", "%one", "%zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "an overshooting stride still proves exactly one body execution",
         .body = loopValues + allocationLoop(localArray, "%zero", "%two", "%zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "one-body cardinality cannot bypass the final-index overflow check",
         .body = loopValues + allocationLoop(localArray, "%one", "%max", "%zero, %zero") + done,
         .complete = false},
        {.what = "negative induction cannot authorize a single allocation instance",
         .body = loopValues + allocationLoop(localArray, "%zero", "%negative") + done,
         .complete = false},
        {.what = "the header allocates twice even when its body executes once",
         .body =
             loopValues + allocationLoop(localArray, "%zero", "%one", "%zero", localObject) + done,
         .complete = false},
        {.what = "exactly-once allocations still require every current solver lattice",
         .body = loopValues + allocationLoop(localArray) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "each structural path must independently prove the body runs once",
         .body = loopValues + selectedStart + "    scf.yield %one : !ctjs.value\n  }\n" +
                 allocationLoop(localArray, "%start", "%one", "%zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "one multi-trip alternative refuses every allocation refinement",
         .body = loopValues + selectedStart + "    scf.yield %zero : !ctjs.value\n  }\n" +
                 allocationLoop(localArray, "%start", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "a root CFG counted body also creates only one discharged child",
         .body = localStart + "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n" +
                 countedStart + localObject +
                 "  %fresh = ctjs.create_array [%child]\n"
                 "  %step = ctjs.binary add %i, %two\n"
                 "  cf.br ^header(%step : !ctjs.value)\n^exit:\n" +
                 done,
         .discharged = "child"},
        {.what = "the root CFG body cannot reuse one site for multiple child instances",
         .body = localStart + countedStart + localObject +
                 "  %fresh = ctjs.create_array [%child]\n" + countedEnd,
         .complete = false},
        {.what = "one counted visit preserves both conditional allocation graphs",
         .body = loopValues + allocationLoop(conditionalGraph) + done,
         .confinedNested = "child,selected,other"},
        {.what = "nested no-result arms create at most one object per counted visit",
         .body =
             loopValues +
             allocationLoop("    scf.if %flag {\n" + conditionalObject + "    }\n" + localArray) +
             done,
         .confinedNested = "child,fresh"},
        {.what = "a returned conditional loop result retains every selected graph",
         .body = loopValues + allocationLoop(conditionalGraph) + "  ctjs.return %loop#1\n"},
        {.what = "a conditional loop result read retains only the selected child",
         .body = loopValues + allocationLoop(conditionalGraph) +
                 "  %read = ctjs.get_property %loop#1[%zero]\n  ctjs.return %read\n",
         .confinedNested = "selected,other"},
        {.what = "one retaining exit prevents conditional loop graph confinement",
         .body = loopValues + allocationLoop(conditionalGraph) +
                 "  cf.cond_br %flag, ^plain, ^retain\n"
                 "^plain:\n  ctjs.return %zero\n"
                 "^retain:\n  ctjs.return %loop#1\n"},
        {.what = "a cleared holder releases both conditional loop graphs",
         .body = loopValues + allocationLoop(conditionalGraph + storeFresh) + clearGuard +
                 "  ctjs.return %guard\n",
         .confinedNested = "child,selected,other"},
        {.what = "a saved conditional loop alias survives clearing its holder",
         .body = loopValues + allocationLoop(conditionalGraph + storeFresh) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "a cycle in one conditional loop arm refuses every refinement",
         .body = loopValues +
                 allocationLoop("    %fresh = scf.if %flag -> (!ctjs.value) {\n"
                                "      %cyclic = ctjs.create_array [%guard]\n"
                                "      scf.yield %cyclic : !ctjs.value\n"
                                "    } else {\n      scf.yield %zero : !ctjs.value\n    }\n" +
                                storeFresh) +
                 clearGuard + done,
         .complete = false},
        {.what = "publication in either counted arm refuses every refinement",
         .body = loopValues +
                 allocationLoop(conditionalObject +
                                "    scf.if %flag {\n    } else {\n"
                                "      ctjs.store_global \"held\", %guard\n"
                                "    }\n" +
                                localArray) +
                 done,
         .complete = false},
        {.what = "nested counted allocations require initialized contents in every arm",
         .body = loopValues +
                 allocationLoop("    scf.if %flag {\n"
                                "      %unknown = ctjs.create_array [%p]\n    }\n" +
                                localArray) +
                 done,
         .complete = false},
        {.what = "conditional counted allocations still require current solver lattices",
         .body = loopValues + allocationLoop(conditionalGraph) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "a solver-dead counted arm cannot supply an allocation instance lattice",
         .body = loopValues + "  %no = arith.constant false\n" +
                 allocationLoop("    scf.if %no {\n" + localObject + "    }\n" + localArray) + done,
         .complete = false},
        {.what = "conditional allocations in a zero-trip body remain unproved",
         .body = loopValues + allocationLoop(conditionalGraph, "%one") + done,
         .complete = false},
        {.what = "conditional allocations in a multi-trip body remain unproved",
         .body =
             loopValues + allocationLoop(conditionalGraph, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "conditional header allocations still repeat at loop exit",
         .body = loopValues +
                 allocationLoop(localArray, "%zero", "%one", "%zero", conditionalObject) + done,
         .complete = false},
        {.what = "a single-visit arm reuses the proved guard-array store footprint",
         .body = loopValues +
                 allocationLoop("    scf.if %flag {\n"
                                "      ctjs.set_property %guard[%zero], %zero\n    }\n" +
                                localArray) +
                 done,
         .confinedReceivers = "guard",
         .confinedNested = "fresh"},
        {.what = "both counted arms retain separate stored allocation graphs",
         .body = loopValues + allocationLoop(conditionalStores) + done,
         .confinedNested = "child,selected,other"},
        {.what = "returning the counted holder retains either arm's selected graph",
         .body = loopValues + allocationLoop(conditionalStores) + "  ctjs.return %guard\n"},
        {.what = "clearing a counted holder releases both arm-local store graphs",
         .body =
             loopValues + allocationLoop(conditionalStores) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "child,selected,other"},
        {.what = "both arm-local saved reads survive clearing the counted holder",
         .body = loopValues + allocationLoop(conditionalStores) + clearGuard +
                 "  ctjs.return %loop#1\n"},
        {.what = "one later retaining exit preserves both counted arm aliases",
         .body = loopValues + allocationLoop(conditionalStores) + clearGuard +
                 "  cf.cond_br %flag, ^plain, ^retain\n"
                 "^plain:\n  ctjs.return %zero\n"
                 "^retain:\n  ctjs.return %loop#1\n"},
        {.what = "nested single-visit writes preserve a stored object through both arm depths",
         .body = loopValues +
                 allocationLoop(loopGraph + "    scf.if %flag {\n    scf.if %flag {\n" +
                                storeFresh + "    }\n    }\n") +
                 clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "child,fresh"},
        {.what = "a cleared cycle from one counted store arm still refuses refinement",
         .body = loopValues +
                 allocationLoop("    %fresh = ctjs.create_array [%guard]\n"
                                "    scf.if %flag {\n" +
                                storeFresh + "    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "an unknown value in the else store refuses both counted arms",
         .body = loopValues +
                 allocationLoop(localArray + "    scf.if %flag {\n" + storeFresh +
                                "    } else {\n"
                                "      ctjs.set_property %guard[%zero], %p\n    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "an unknown nested store key cannot borrow the single-visit proof",
         .body =
             loopValues +
             allocationLoop(localArray + "    scf.if %flag {\n"
                                         "      ctjs.set_property %guard[%p], %fresh\n    }\n") +
             done,
         .complete = false},
        {.what = "an unknown nested receiver cannot borrow the guard-array footprint",
         .body = loopValues +
                 allocationLoop(localArray + "    scf.if %flag {\n"
                                             "      ctjs.set_property %p[%zero], %fresh\n    }\n") +
                 done,
         .complete = false},
        {.what = "a nested store to another array still needs its own mutation proof",
         .body =
             loopValues +
             allocationLoop(localArray + "    scf.if %flag {\n"
                                         "      ctjs.set_property %fresh[%zero], %zero\n    }\n") +
             done,
         .complete = false},
        {.what = "a nested exact key producer reuses the admitted body proof",
         .body =
             loopValues +
             allocationLoop(localArray + "    scf.if %flag {\n"
                                         "      %key = ctjs.binary add %zero, %zero\n"
                                         "      ctjs.set_property %guard[%key], %fresh\n    }\n") +
             done,
         .confinedNested = "fresh"},
        {.what = "a branch-local induction key keeps the counted own bounds",
         .body = loopValues + allocationLoop(computedStore(computedKey)) + done,
         .confinedNested = "fresh"},
        {.what = "an untouched branch retains a child through its computed write key",
         .body =
             loopValues + allocationLoop(computedStore(computedKey)) + "  ctjs.return %guard\n"},
        {.what = "clearing a computed-key holder confines its conditional child",
         .body = loopValues + allocationLoop(computedStore(computedKey)) + clearGuard +
                 "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved computed-key alias survives clearing its holder",
         .body = loopValues + allocationLoop(computedStore(computedKey)) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "nested branch-local unary and bitwise key producers retain exact bounds",
         .body =
             loopValues +
             allocationLoop(localArray + "    scf.if %flag {\n"
                                         "      %outer = ctjs.unary plus %i\n"
                                         "      scf.if %flag {\n"
                                         "        %key = ctjs.binary_static bitxor %outer, %zero\n"
                                         "        ctjs.set_property %guard[%key], %fresh\n"
                                         "      }\n    }\n") +
             done,
         .confinedNested = "fresh"},
        {.what = "a branch-local unchanged-table read proves its own write key",
         .body = loopValues + "  %keys = ctjs.create_array [%zero]\n" +
                 allocationLoop(computedStore("      %key = ctjs.get_property %keys[%i]\n")) + done,
         .confinedNested = "fresh"},
        {.what = "an arm-local receiver reload keeps its exact unchanged holder identity",
         .body = loopValues +
                 allocationLoop(computedStore(loadedReceiver + computedKey, "%reloaded"), "%zero",
                                "%one", "%zero", "", guardAlias) +
                 done,
         .discharged = "guard",
         .confinedNested = "fresh"},
        {.what = "a returned alias holder retains the branch-local receiver's stored child",
         .body = loopValues +
                 allocationLoop(computedStore(loadedReceiver + computedKey, "%reloaded"), "%zero",
                                "%one", "%zero", "", guardAlias) +
                 "  ctjs.return %aliases\n"},
        {.what = "a disjoint guard reload proves the branch-local store key",
         .body = loopValues +
                 allocationLoop(computedStore("      %key = ctjs.get_property %guard[%one]\n"),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "an overlapping guard reload cannot prove its later branch store key",
         .body = loopValues +
                 allocationLoop(computedStore("      %key = ctjs.get_property %guard[%zero]\n")) +
                 done,
         .complete = false},
        {.what = "a receiver alias cannot hide a guard-key reload overlapping the store",
         .body =
             loopValues +
             allocationLoop(
                 computedStore(loadedReceiver + "      %key = ctjs.get_property %reloaded[%zero]\n",
                               "%reloaded"),
                 "%zero", "%one", "%zero", "", guardAlias) +
             done,
         .complete = false},
        {.what = "an out-of-bounds branch-local expression cannot extend the guard array",
         .body = loopValues +
                 allocationLoop(computedStore("      %key = ctjs.binary add %i, %one\n")) + done,
         .complete = false},
        {.what = "an opaque operand cannot borrow a branch-local arithmetic proof",
         .body = loopValues +
                 allocationLoop(computedStore("      %key = ctjs.binary add %i, %p\n")) + done,
         .complete = false},
        {.what = "a loaded alternate receiver cannot borrow the guard-array mutation proof",
         .body =
             loopValues + "  %otherArray = ctjs.create_array [%zero]\n" +
             allocationLoop(computedStore(loadedReceiver + computedKey, "%reloaded"), "%zero",
                            "%one", "%zero", "", "  %aliases = ctjs.create_array [%otherArray]\n") +
             done,
         .complete = false},
        {.what = "an unknown second arm discards a proved computed-key store",
         .body = loopValues +
                 allocationLoop(localArray + "    scf.if %flag {\n" + computedKey +
                                "      ctjs.set_property %guard[%key], %fresh\n"
                                "    } else {\n"
                                "      %badKey = ctjs.binary add %zero, %p\n"
                                "      ctjs.set_property %guard[%badKey], %fresh\n    }\n") +
                 done,
         .complete = false},
        {.what = "a transient computed-key cycle still refuses all-write confinement",
         .body =
             loopValues +
             allocationLoop("    %fresh = ctjs.create_array [%guard]\n"
                            "    scf.if %flag {\n" +
                            computedKey + "      ctjs.set_property %guard[%key], %fresh\n    }\n") +
             clearGuard + done,
         .complete = false},
        {.what = "an If result with identical yields preserves its counted store key",
         .body = loopValues +
                 allocationLoop(localArray +
                                "    %key = scf.if %flag -> (!ctjs.value) {\n"
                                "      scf.yield %zero : !ctjs.value\n"
                                "    } else {\n      scf.yield %zero : !ctjs.value\n    }\n"
                                "    ctjs.set_property %guard[%key], %fresh\n") +
                 done,
         .confinedNested = "fresh"},
        {.what = "equal If induction yields reuse the counted index range",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%i", "%i"))) + done,
         .confinedNested = "fresh"},
        {.what = "equal If expression yields preserve the original arithmetic proof",
         .body = loopValues +
                 allocationLoop(selectedStore("    %shared = ctjs.binary add %i, %zero\n" +
                                              selectedInput("%shared", "%shared"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "distinct If Number constants with equal attributes prove the same key",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput(
                     "%left", "%right", "      %left = ctjs.constant #ctjs.number<0>\n",
                     "      %right = ctjs.constant #ctjs.number<0>\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "equal If String constants keep their canonical own-key spelling",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput(
                     "%left", "%right", "      %left = ctjs.constant #ctjs.string<\"0\">\n",
                     "      %right = ctjs.constant #ctjs.string<\"0\">\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested exact result correspondence reaches the original induction value",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput("%i", "%i", "", "", "%shared") +
                                              selectedInput("%shared", "%shared"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "an equal second tuple slot cannot borrow a different first result",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedTuple, "%guard", "%key#1"), "%one", "%one",
                                "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "a differing first tuple slot proves both own positions independently",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedTuple, "%guard", "%key#0"), "%one", "%one",
                                "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "different in-bounds If keys retain both independent footprints",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput("%zero", "%one")), "%one", "%one",
                                "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "identical opaque yields cannot establish an own key",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%p", "%p"))) + done,
         .complete = false},
        {.what = "one unknown If yield discards the other arm's exact key",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%zero", "%p"))) + done,
         .complete = false},
        {.what = "separate arithmetic yields each establish their own store footprint",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput(
                     "%left", "%right", "      %left = ctjs.binary add %zero, %zero\n",
                     "      %right = ctjs.binary add %zero, %zero\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "Number and String yields independently name the same canonical own key",
         .body =
             loopValues +
             allocationLoop(selectedStore(selectedInput(
                 "%zero", "%right", "", "      %right = ctjs.constant #ctjs.string<\"0\">\n"))) +
             done,
         .confinedNested = "fresh"},
        {.what = "an equal If receiver keeps the exact unchanged array alias",
         .body =
             loopValues +
             allocationLoop(selectedStore(loadedReceiver + selectedInput("%reloaded", "%reloaded"),
                                          "%key", "%zero"),
                            "%zero", "%one", "%zero", "", guardAlias) +
             done,
         .discharged = "guard",
         .confinedNested = "fresh"},
        {.what = "different If receiver identities cannot borrow the guard footprint",
         .body = loopValues + "  %otherArray = ctjs.create_array [%zero]\n" +
                 allocationLoop(
                     selectedStore(selectedInput("%guard", "%otherArray"), "%key", "%zero")) +
                 done,
         .complete = false},
        {.what = "a returned holder retains the child stored through an equal If key",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%i", "%i"))) +
                 "  ctjs.return %guard\n"},
        {.what = "clearing an equal-key holder releases its conditional child",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%i", "%i"))) +
                 clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved equal-key store alias survives clearing its holder",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%i", "%i"))) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "a call in either equal-yield arm still refuses the complete census",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     selectedInput("%zero", "%zero", "      %call = ctjs.call %p(%q)\n"))) +
                 done,
         .complete = false},
        {.what = "publication in the other equal-yield arm refuses confinement",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput(
                     "%zero", "%zero", "", "      ctjs.store_global \"held\", %fresh\n"))) +
                 done,
         .complete = false},
        {.what = "an overlapping guard reload remains visible through equal yields",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                              selectedInput("%read", "%read"))) +
                 done,
         .complete = false},
        {.what = "a disjoint guard reload keeps its footprint through equal yields",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                              selectedInput("%read", "%read")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "a transient equal-key cycle remains in the all-write graph",
         .body = loopValues +
                 allocationLoop("    %fresh = ctjs.create_array [%guard]\n" +
                                selectedInput("%i", "%i") +
                                "    ctjs.set_property %guard[%key], %fresh\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "equal-yield correspondence still requires initialized solver inputs",
         .body = loopValues + allocationLoop(selectedStore(selectedInput("%i", "%i"))) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "unequal branch-local expressions each prove an own store key",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput(
                                    "%left", "%right", "      %left = ctjs.binary add %i, %zero\n",
                                    "      %right = ctjs.binary sub %i, %one\n")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested result choices retain all independent own-key footprints",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput("%zero", "%one", "", "", "%shared") +
                                              selectedInput("%shared", "%zero")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "separate result footprints preserve an unwritten guard reload gap",
         .body = loopValues + allocationLoop(gapKeys, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "either result footprint can invalidate the other arm's guard reload",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                              selectedInput("%read", "%one")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "a then-arm store footprint cannot hide an else-arm guard reload",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(selectedInput("%one", "%read", "",
                                                 "      %read = ctjs.get_property %guard[%one]\n")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "returning an unequal-key holder retains either selected child",
         .body = loopValues + allocationLoop(unequalKeys, "%one", "%one", "%zero, %zero") +
                 "  ctjs.return %guard\n"},
        {.what = "clearing both possible own slots releases an unequal-key child",
         .body = loopValues + allocationLoop(unequalKeys, "%one", "%one", "%zero, %zero") +
                 clearBoth + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "clearing only one result footprint leaves the other child's retaining path",
         .body = loopValues + allocationLoop(unequalKeys, "%one", "%one", "%zero, %zero") +
                 clearGuard + "  ctjs.return %guard\n"},
        {.what = "a saved unequal-key child survives clearing every holder position",
         .body = loopValues + allocationLoop(unequalKeys, "%one", "%one", "%zero, %zero") +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearBoth +
                 "  ctjs.return %read\n"},
        {.what = "one later retaining exit preserves either unequal-key child",
         .body = loopValues + allocationLoop(unequalKeys, "%one", "%one", "%zero, %zero") +
                 "  cf.cond_br %flag, ^retain, ^discard\n"
                 "^retain:\n  ctjs.return %guard\n^discard:\n" +
                 done},
        {.what = "clearing both unequal-key slots cannot remove the all-write cycle",
         .body = loopValues +
                 allocationLoop("    %fresh = ctjs.create_array [%guard]\n" +
                                    selectedInput("%zero", "%one") +
                                    "    ctjs.set_property %guard[%key], %fresh\n",
                                "%one", "%one", "%zero, %zero") +
                 clearBoth + done,
         .complete = false},
        {.what = "one out-of-bounds yield refuses both store-key footprints",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput("%zero", "%two")), "%one", "%one",
                                "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "a noncanonical String yield cannot borrow the numeric arm's own key",
         .body =
             loopValues +
             allocationLoop(selectedStore(selectedInput(
                 "%zero", "%right", "", "      %right = ctjs.constant #ctjs.string<\"00\">\n"))) +
             done,
         .complete = false},
        {.what = "publication in an unequal-key arm still rejects the full census",
         .body =
             loopValues +
             allocationLoop(selectedStore(selectedInput(
                                "%zero", "%one", "", "      ctjs.store_global \"held\", %fresh\n")),
                            "%one", "%one", "%zero, %zero") +
             done,
         .complete = false},
        {.what = "a call before an unequal yield still rejects the full census",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput("%zero", "%one",
                                                            "      %called = ctjs.call %p(%q)\n")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "unequal-key footprints require initialized current solver inputs",
         .body = loopValues + allocationLoop(unequalKeys, "%one", "%one", "%zero, %zero") + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "unequal-key choices cannot borrow a repeated body's instance proof",
         .body = loopValues + allocationLoop(unequalKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot lend an instance to unequal-key choices",
         .body = loopValues + allocationLoop(unequalKeys, "%two", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "addition consumes both unequal conditional Number keys",
         .body = loopValues + allocationLoop(arithmeticKeys, "%one", "%one", "%zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "an unequal conditional right operand preserves addition order",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%one", "add %zero, %selectedKey")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "subtraction reverses the unequal conditional Number enclosure",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%two", "sub %two, %selectedKey")),
                     "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "multiplication preserves the conditional write lattice gap",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%one", "mul %selectedKey, %two")),
                     "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "division preserves the stride between unequal singleton arms",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%two", "div %selectedKey, %two")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "arithmetic encloses nested conditional producer results",
         .body = loopValues +
                 allocationLoop(selectedStore(selectedInput("%zero", "%one", "", "", "%inner") +
                                              arithmeticInput("%inner", "%zero")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "composed arithmetic keeps unequal conditional Number bounds",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput("%zero", "%two") +
                                                  "    %final = ctjs.binary div %key, %two\n",
                                              "%guard", "%final"),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "arithmetic over unequal keys preserves an unwritten guard reload gap",
         .body = loopValues + allocationLoop(arithmeticGap, "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "either arithmetic key can overlap the other arm's guard reload",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                              arithmeticInput("%read", "%one")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "else-arm reloads remain visible through conditional arithmetic",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput(
                                    "%one", "%read", "add %selectedKey, %zero", "",
                                    "      %read = ctjs.get_property %guard[%one]\n")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "a returned arithmetic-key holder retains either possible child",
         .body = loopValues + allocationLoop(arithmeticKeys, "%one", "%one", "%zero, %zero") +
                 "  ctjs.return %guard\n"},
        {.what = "clearing every arithmetic key releases the stored child",
         .body = loopValues + allocationLoop(arithmeticKeys, "%one", "%one", "%zero, %zero") +
                 clearBoth + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved arithmetic-key child survives clearing both positions",
         .body = loopValues + allocationLoop(arithmeticKeys, "%one", "%one", "%zero, %zero") +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearBoth +
                 "  ctjs.return %read\n"},
        {.what = "one retaining exit preserves the arithmetic-key child",
         .body = loopValues + allocationLoop(arithmeticKeys, "%one", "%one", "%zero, %zero") +
                 "  cf.cond_br %flag, ^retain, ^discard\n"
                 "^retain:\n  ctjs.return %guard\n^discard:\n" +
                 done},
        {.what = "clearing arithmetic keys cannot erase an earlier ownership cycle",
         .body = loopValues +
                 allocationLoop("    %fresh = ctjs.create_array [%guard]\n" +
                                    arithmeticInput("%zero", "%one") +
                                    "    ctjs.set_property %guard[%key], %fresh\n",
                                "%one", "%one", "%zero, %zero") +
                 clearBoth + done,
         .complete = false},
        {.what = "one arithmetic arm outside the current extent refuses the store",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%one", "add %selectedKey, %one")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "one fractional arithmetic arm cannot borrow the other's exact key",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%one", "div %selectedKey, %two")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "a String arm never borrows a Number enclosure for concatenating Add",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput(
                                    "%zero", "%text", "add %selectedKey, %zero", "",
                                    "      %text = ctjs.constant #ctjs.string<\"1\">\n")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "an unknown conditional operand refuses the entire arithmetic key",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput("%zero", "%p")), "%one", "%one",
                                "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "the same finite Number cancels even when conditional arms differ",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput("%zero", "%one",
                                                              "sub %selectedKey, %selectedKey")),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "signed conditional Numbers cancel without primitive coercion",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     arithmeticInput("%negative", "%one", "sub %selectedKey, %selectedKey"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a cancelled Number result composes with an ordinary offset",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput("%zero", "%two",
                                                              "sub %selectedKey, %selectedKey") +
                                                  "    %final = ctjs.binary add %key, %one\n",
                                              "%guard", "%final"),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "cancellation retains an unwritten guard reload dependency",
         .body = loopValues + allocationLoop(correlatedGap, "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "cancellation cannot hide a guard reload overwritten by its zero key",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     "    %read = ctjs.get_property %guard[%zero]\n" +
                     arithmeticInput("%read", "%one", "sub %selectedKey, %selectedKey"))) +
                 done,
         .complete = false},
        {.what = "matching ranges of different SSA values do not prove cancellation",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(selectedInput("%one", "%zero", "", "", "%otherKey") +
                                   arithmeticInput("%zero", "%one", "sub %selectedKey, %otherKey")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "unknown values cannot borrow the finite Number cancellation proof",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     arithmeticInput("%zero", "%p", "sub %selectedKey, %selectedKey"))) +
                 done,
         .complete = false},
        {.what = "String arms need a separate conversion proof before cancellation",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     arithmeticInput("%zero", "%text", "sub %selectedKey, %selectedKey", "",
                                     "      %text = ctjs.constant #ctjs.string<\"1\">\n"))) +
                 done,
         .complete = false},
        {.what = "NaN never cancels to an array index",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput(
                     "%zero", "%nan", "sub %selectedKey, %selectedKey", "",
                     "      %nan = ctjs.constant #ctjs.number<9221120237041090560>\n"))) +
                 done,
         .complete = false},
        {.what = "Infinity never cancels to an array index",
         .body = loopValues +
                 allocationLoop(selectedStore(arithmeticInput(
                     "%zero", "%infinity", "sub %selectedKey, %selectedKey", "",
                     "      %infinity = ctjs.constant #ctjs.number<9218868437227405312>\n"))) +
                 done,
         .complete = false},
        {.what = "a returned zero-key holder retains its stored child",
         .body = loopValues + allocationLoop(correlatedKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing the proved zero key releases its stored child",
         .body =
             loopValues + allocationLoop(correlatedKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved zero-key child survives clearing the holder",
         .body = loopValues + allocationLoop(correlatedKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "finite Number cancellation still requires initialized solver inputs",
         .body = loopValues + allocationLoop(correlatedKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "matching result slots of one If cancel as finite Numbers",
         .body = loopValues + allocationLoop(pairedKeys) + done,
         .confinedNested = "fresh"},
        {.what = "paired signed conditional Number values cancel without conversion",
         .body = loopValues +
                 allocationLoop(selectedStore(pairedInput("%negative, %negative", "%one, %one"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "paired distinct constants need equal attributes in each corresponding arm",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(pairedInput("%left, %right", "%one, %one",
                                               "      %left = ctjs.constant #ctjs.number<0>\n"
                                               "      %right = ctjs.constant #ctjs.number<0>\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a paired cancellation result composes with an ordinary offset",
         .body = loopValues +
                 allocationLoop(selectedStore(pairedInput("%zero, %zero", "%one, %one") +
                                                  "    %final = ctjs.binary add %key, %one\n",
                                              "%guard", "%final"),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "paired cancellation preserves the guard read gap",
         .body =
             loopValues + allocationLoop(pairedGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "paired cancellation cannot overwrite its input guard read",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                              pairedInput("%read, %read", "%one, %one"))) +
                 done,
         .complete = false},
        {.what = "swapped arm values do not correlate merely because their ranges agree",
         .body = loopValues +
                 allocationLoop(selectedStore(pairedInput("%zero, %one", "%one, %zero")), "%one",
                                "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "corresponding unknown values still need finite Number evidence",
         .body = loopValues + allocationLoop(selectedStore(pairedInput("%zero, %zero", "%p, %p"))) +
                 done,
         .complete = false},
        {.what = "corresponding String values do not borrow the Number cancellation proof",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     pairedInput("%zero, %zero", "%text, %text", "",
                                 "      %text = ctjs.constant #ctjs.string<\"1\">\n"))) +
                 done,
         .complete = false},
        {.what = "corresponding NaN values never cancel to an own key",
         .body = loopValues +
                 allocationLoop(selectedStore(pairedInput(
                     "%zero, %zero", "%nan, %nan", "",
                     "      %nan = ctjs.constant #ctjs.number<9221120237041090560>\n"))) +
                 done,
         .complete = false},
        {.what = "corresponding Infinity values never cancel to an own key",
         .body = loopValues +
                 allocationLoop(selectedStore(pairedInput(
                     "%zero, %zero", "%infinity, %infinity", "",
                     "      %infinity = ctjs.constant #ctjs.number<9218868437227405312>\n"))) +
                 done,
         .complete = false},
        {.what = "paired cancellation cannot bypass an effect in either arm",
         .body = loopValues +
                 allocationLoop(selectedStore(pairedInput("%zero, %zero", "%one, %one", "",
                                                          "      %called = ctjs.call %p(%q)\n"))) +
                 done,
         .complete = false},
        {.what = "a returned paired-key holder retains its child",
         .body = loopValues + allocationLoop(pairedKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a paired zero key releases the stored child",
         .body = loopValues + allocationLoop(pairedKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved paired-key child survives clearing its holder",
         .body = loopValues + allocationLoop(pairedKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "paired cancellation still requires initialized solver inputs",
         .body = loopValues + allocationLoop(pairedKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "sibling Ifs with the same exact condition select corresponding Numbers",
         .body = loopValues + allocationLoop(siblingKeys) + done,
         .confinedNested = "fresh"},
        {.what = "sibling conditional signed Numbers cancel without coercion",
         .body =
             loopValues + allocationLoop(selectedStore(siblingInput("%negative", "%one"))) + done,
         .confinedNested = "fresh"},
        {.what = "sibling cancellation composes with a bounded offset",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput() + "    %final = ctjs.binary add %key, %one\n",
                                   "%guard", "%final"),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "sibling cancellation retains the guard read gap",
         .body =
             loopValues + allocationLoop(siblingGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "sibling cancellation cannot overwrite a guard dependency",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                              siblingInput("%read", "%one"))) +
                 done,
         .complete = false},
        {.what = "different predicate SSA values supply no sibling correlation",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           "    %secondFlag = ctjs.truthy %q\n"))) +
                 done,
         .complete = false},
        {.what = "recomputed truthiness retains the exact input's predicate choice",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           "    %secondFlag = ctjs.truthy %p\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "sibling unknown values still require finite Number proof",
         .body = loopValues + allocationLoop(selectedStore(siblingInput("%zero", "%p"))) + done,
         .complete = false},
        {.what = "sibling nonfinite values cannot cancel into an own index",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(siblingInput("%zero", "%nan"))) + done,
         .complete = false},
        {.what = "a returned sibling-key holder retains its child",
         .body = loopValues + allocationLoop(siblingKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a sibling zero key releases its stored child",
         .body = loopValues + allocationLoop(siblingKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved sibling-key child survives clearing the holder",
         .body = loopValues + allocationLoop(siblingKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "sibling correlation cannot hide a transient all-write cycle",
         .body = loopValues +
                 allocationLoop(siblingKeys + "    ctjs.set_property %fresh[%zero], %guard\n"
                                              "    ctjs.set_property %fresh[%zero], %zero\n") +
                 done,
         .complete = false},
        {.what = "nested no-result branches retain every structural cycle check",
         .body =
             loopValues +
             allocationLoop(siblingKeys +
                            "    scf.if %flag {\n      scf.if %flag {\n      } else {\n"
                            "        ctjs.set_property %fresh[%zero], %fresh\n      }\n    }\n") +
             done,
         .complete = false},
        {.what = "sibling choices cannot authorize repeated allocation instances",
         .body = loopValues + allocationLoop(siblingKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot establish sibling allocation identities",
         .body = loopValues + allocationLoop(siblingKeys, "%one") + done,
         .complete = false},
        {.what = "sibling correlation still needs initialized solver inputs",
         .body = loopValues + allocationLoop(siblingKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "nested sibling result branches retain the same exact predicate choice",
         .body = loopValues + allocationLoop(nestedSiblingKeys) + done,
         .confinedNested = "fresh"},
        {.what = "two conditional ancestors preserve nested sibling Number correspondence",
         .body = loopValues +
                 allocationLoop(
                     localArray +
                     nestedArm(nestedArm(siblingInput() +
                                         "      ctjs.set_property %guard[%key], %fresh\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a root result and nested result retain their common predicate snapshot",
         .body = loopValues +
                 allocationLoop(localArray +
                                siblingInput("%zero", "%one", "%flag", "    scf.if %flag {\n") +
                                "      ctjs.set_property %guard[%key], %fresh\n    }\n") +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested cancellation transports through its result-bearing ancestor",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(selectedInput("%key", "%zero", siblingInput(), "", "%outerKey"),
                                   "%guard", "%outerKey")) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested signed Number choices cancel without primitive conversion",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(siblingInput("%negative", "%one"))) + done,
         .confinedNested = "fresh"},
        {.what = "nested correspondence retains the independently proved guard read gap",
         .body = loopValues +
                 allocationLoop(nestedSiblingGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "nested cancellation cannot overwrite its current guard dependency",
         .body =
             loopValues +
             allocationLoop(nestedSiblingStore("      %read = ctjs.get_property %guard[%zero]\n" +
                                               siblingInput("%read", "%one"))) +
             done,
         .complete = false},
        {.what = "different nested condition identities cannot borrow the first choice",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(siblingInput(
                     "%zero", "%one", "%secondFlag", "      %secondFlag = ctjs.truthy %q\n"))) +
                 done,
         .complete = false},
        {.what = "recomputed nested truthiness preserves the exact input's correspondence",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(siblingInput(
                     "%zero", "%one", "%secondFlag", "      %secondFlag = ctjs.truthy %p\n"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested unknown values still require both independent Number proofs",
         .body =
             loopValues + allocationLoop(nestedSiblingStore(siblingInput("%zero", "%p"))) + done,
         .complete = false},
        {.what = "nested nonfinite operands cannot cancel into a bounded own index",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(nestedSiblingStore(siblingInput("%zero", "%nan"))) + done,
         .complete = false},
        {.what = "one retaining exit prevents nested sibling child confinement",
         .body = loopValues + allocationLoop(nestedSiblingKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing the nested zero-key holder releases the stored child",
         .body =
             loopValues + allocationLoop(nestedSiblingKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved nested-key alias survives clearing its holder",
         .body = loopValues + allocationLoop(nestedSiblingKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "nested result correlation preserves transient cycle refusal",
         .body =
             loopValues +
             allocationLoop(nestedSiblingKeys + "    ctjs.set_property %fresh[%zero], %guard\n"
                                                "    ctjs.set_property %fresh[%zero], %zero\n") +
             done,
         .complete = false},
        {.what = "nested result correlation cannot hide an unsupported producer effect",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(siblingInput(
                     "%zero", "%one", "%flag", "      %called = ctjs.call %p(%q)\n"))) +
                 done,
         .complete = false},
        {.what = "nested choices cannot authorize repeated body allocation identities",
         .body =
             loopValues + allocationLoop(nestedSiblingKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize nested result allocation identities",
         .body = loopValues + allocationLoop(nestedSiblingKeys, "%one") + done,
         .complete = false},
        {.what = "nested result correlation requires current initialized solver inputs",
         .body = loopValues + allocationLoop(nestedSiblingKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "truthiness producers evaluated before either branch share their input choice",
         .body = loopValues + recomputedFlag +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag"))) + done,
         .confinedNested = "fresh"},
        {.what = "recomputed truthiness preserves signed finite Number cancellation",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%negative", "%one", "%secondFlag", recomputedFlag))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "recomputed nested truthiness preserves the independent guard read gap",
         .body = loopValues + allocationLoop(recomputedGap, "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "recomputed truthiness cannot overwrite a guard dependency",
         .body = loopValues +
                 allocationLoop(
                     selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                   siblingInput("%read", "%one", "%secondFlag", recomputedFlag))) +
                 done,
         .complete = false},
        {.what = "separate truthiness input reads do not acquire SSA correspondence",
         .body = loopValues +
                 allocationLoop(reloadedConditions(""), "%one", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "mutation between truthiness input reads cannot reuse a predicate choice",
         .body = loopValues +
                 allocationLoop(reloadedConditions("    ctjs.set_property %guard[%zero], %fresh\n"),
                                "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "recomputed truthiness still needs independent finite Number operands",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%p", "%secondFlag", recomputedFlag))) +
                 done,
         .complete = false},
        {.what = "recomputed truthiness cannot make a nonfinite cancellation an own key",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%nan", "%secondFlag", recomputedFlag))) +
                 done,
         .complete = false},
        {.what = "a retaining exit preserves a child stored through recomputed truthiness",
         .body = loopValues + allocationLoop(recomputedKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a recomputed zero key releases the stored child",
         .body =
             loopValues + allocationLoop(recomputedKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved recomputed-key alias survives clearing its holder",
         .body = loopValues + allocationLoop(recomputedKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "recomputed predicates preserve no-result structural cycle checks",
         .body = loopValues +
                 allocationLoop(recomputedKeys +
                                "    scf.if %secondFlag {\n    } else {\n"
                                "      ctjs.set_property %fresh[%zero], %guard\n"
                                "      ctjs.set_property %fresh[%zero], %zero\n    }\n") +
                 done,
         .complete = false},
        {.what = "recomputed predicates cannot hide an unsupported producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  recomputedFlag + "    %called = ctjs.call %p(%q)\n"))) +
                 done,
         .complete = false},
        {.what = "recomputed predicates cannot authorize repeated allocation identities",
         .body =
             loopValues + allocationLoop(recomputedKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize recomputed-predicate allocation identities",
         .body = loopValues + allocationLoop(recomputedKeys, "%one") + done,
         .complete = false},
        {.what = "recomputed predicates still require current initialized solver inputs",
         .body = loopValues + allocationLoop(recomputedKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "opposite predicates align opposite finite Number arms",
         .body = loopValues + allocationLoop(oppositeKeys) + done,
         .confinedNested = "fresh"},
        {.what = "a negated first predicate records the input's correctly oriented choice",
         .body = loopValues + allocationLoop(selectedStore(firstNegatedFlag + negatedFirst)) + done,
         .confinedNested = "fresh"},
        {.what = "separately recomputed logical Not results share one exact input choice",
         .body = loopValues + allocationLoop(selectedStore(firstNegatedFlag + repeatedNot)) + done,
         .confinedNested = "fresh"},
        {.what = "a preheader negation shares its input snapshot with a later positive predicate",
         .body = loopValues + firstNegatedFlag + allocationLoop(selectedStore(negatedFirst)) + done,
         .confinedNested = "fresh"},
        {.what = "opposite nested predicates retain an independent guard read gap",
         .body =
             loopValues + allocationLoop(oppositeGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "opposite predicates retain signed finite Number cancellation",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%negative", "%one", "%secondFlag", negatedFlag, true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "opposite predicates with unswapped arms do not prove equal values",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", negatedFlag))) +
                 done,
         .complete = false},
        {.what = "logical Not cannot correlate a distinct input SSA value",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                       "    %negated = ctjs.unary not %q\n"
                                                       "    %secondFlag = ctjs.truthy %negated\n",
                                                       true))) +
             done,
         .complete = false},
        {.what = "a different unary expression cannot borrow logical Not's polarity proof",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                       "    %negated = ctjs.unary void %p\n"
                                                       "    %secondFlag = ctjs.truthy %negated\n",
                                                       true))) +
             done,
         .complete = false},
        {.what = "a negated separate read retains its own input snapshot",
         .body = loopValues + allocationLoop(reloadedNot) + done,
         .complete = false},
        {.what = "a mutation between reads cannot supply opposite-predicate correspondence",
         .body = loopValues + allocationLoop(mutatedNot) + done,
         .complete = false},
        {.what = "opposite predicates cannot overwrite their guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     siblingInput("%read", "%one", "%secondFlag", negatedFlag, true))) +
                 done,
         .complete = false},
        {.what = "opposite predicates still require both finite Number operand proofs",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%p", "%secondFlag", negatedFlag, true))) +
                 done,
         .complete = false},
        {.what = "opposite predicates cannot cancel a nonfinite Number into an own key",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%nan", "%secondFlag", negatedFlag, true))) +
                 done,
         .complete = false},
        {.what = "one retaining exit preserves a child stored through opposite predicates",
         .body = loopValues + allocationLoop(oppositeKeys) +
                 "  cf.cond_br %flag, ^plain, ^retain\n^plain:\n  ctjs.return %zero\n"
                 "^retain:\n  ctjs.return %guard\n"},
        {.what = "clearing an opposite-predicate zero key releases its child",
         .body = loopValues + allocationLoop(oppositeKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved opposite-predicate alias survives clearing its holder",
         .body = loopValues + allocationLoop(oppositeKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "opposite predicates cannot hide a no-result transient cycle",
         .body = loopValues +
                 allocationLoop(oppositeKeys +
                                "    scf.if %secondFlag {\n"
                                "      ctjs.set_property %fresh[%zero], %guard\n"
                                "      ctjs.set_property %fresh[%zero], %zero\n    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "opposite predicates cannot hide an unsupported producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  negatedFlag + "    %called = ctjs.call %p(%q)\n", true))) +
                 done,
         .complete = false},
        {.what = "opposite predicates cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(oppositeKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize opposite-predicate allocation identities",
         .body = loopValues + allocationLoop(oppositeKeys, "%one") + done,
         .complete = false},
        {.what = "opposite predicates still require current initialized solver inputs",
         .body = loopValues + allocationLoop(oppositeKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "two logical Not links preserve the input's branch orientation",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", notChain(2)))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "three logical Not links reverse the corresponding finite Number arms",
         .body = loopValues + allocationLoop(chainKeys) + done,
         .confinedNested = "fresh"},
        {.what = "an even long Not chain uses the shared work budget",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", notChain(64)))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "an odd Not chain beyond sixty-four links retains its exact polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", notChain(65), true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a first Not chain snapshots the correctly oriented input choice",
         .body = loopValues + allocationLoop(selectedStore(notChain(3) + firstChain)) + done,
         .confinedNested = "fresh"},
        {.what = "a preheader Not chain shares the exact input snapshot",
         .body = loopValues + notChain(3) + allocationLoop(selectedStore(firstChain)) + done,
         .confinedNested = "fresh"},
        {.what = "nested Not-chain correspondence preserves the independent guard read gap",
         .body =
             loopValues + allocationLoop(chainGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "an even Not chain cannot reverse unswapped operand correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", notChain(2), true))) +
                 done,
         .complete = false},
        {.what = "an odd Not chain cannot keep same-arm operand correspondence",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", notChain(3)))) +
                 done,
         .complete = false},
        {.what = "a Not chain cannot correlate a distinct SSA input",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", notChain(3, "%q"), true))) +
                 done,
         .complete = false},
        {.what = "a Not chain stops at a different unary producer",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", unsupportedChain, true))) +
                 done,
         .complete = false},
        {.what = "a Not chain over a separate read retains its own predicate snapshot",
         .body = loopValues + allocationLoop(reloadedChain) + done,
         .complete = false},
        {.what = "mutation between reads cannot provide Not-chain correspondence",
         .body = loopValues + allocationLoop(mutatedChain) + done,
         .complete = false},
        {.what = "Not-chain correspondence cannot overwrite its current guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     siblingInput("%read", "%one", "%secondFlag", notChain(3), true))) +
                 done,
         .complete = false},
        {.what = "Not-chain correspondence still proves both Number operands independently",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%p", "%secondFlag", notChain(3), true))) +
                 done,
         .complete = false},
        {.what = "Not-chain correspondence cannot cancel a nonfinite Number",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%nan", "%secondFlag", notChain(3), true))) +
                 done,
         .complete = false},
        {.what = "every retaining exit preserves a child stored through a Not chain",
         .body = loopValues + allocationLoop(chainKeys) +
                 "  cf.cond_br %flag, ^plain, ^retain\n^plain:\n  ctjs.return %zero\n"
                 "^retain:\n  ctjs.return %guard\n"},
        {.what = "clearing a Not-chain zero key releases its child",
         .body = loopValues + allocationLoop(chainKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved Not-chain alias survives clearing its holder",
         .body = loopValues + allocationLoop(chainKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "Not-chain correspondence cannot hide a no-result transient cycle",
         .body =
             loopValues +
             allocationLoop(chainKeys + "    scf.if %secondFlag {\n"
                                        "      ctjs.set_property %fresh[%zero], %guard\n"
                                        "      ctjs.set_property %fresh[%zero], %zero\n    }\n") +
             clearGuard + done,
         .complete = false},
        {.what = "a Not chain cannot hide an unsupported producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  notChain(3) + "    %called = ctjs.call %p(%q)\n", true))) +
                 done,
         .complete = false},
        {.what = "Not-chain correspondence cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(chainKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize Not-chain allocation identities",
         .body = loopValues + allocationLoop(chainKeys, "%one") + done,
         .complete = false},
        {.what = "Not-chain correspondence still requires current initialized solver inputs",
         .body = loopValues + allocationLoop(chainKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "repeated strict comparisons share exact ordered SSA operands",
         .body = loopValues + allocationLoop(comparedKeys) + done,
         .confinedNested = "fresh"},
        {.what = "nested same-block strict comparisons preserve Number correspondence",
         .body = loopValues + allocationLoop(nestedSiblingStore(comparedInput())) + done,
         .confinedNested = "fresh"},
        {.what = "strict comparison correspondence composes with odd Not parity",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(comparedInput("%zero", "%one", "strict_eq %p, %zero", "", 3))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "strict comparison producers may both precede their selecting branches",
         .body = loopValues + allocationLoop(selectedStore(eagerComparisons)) + done,
         .confinedNested = "fresh"},
        {.what = "strict comparison choices retain signed finite Number cancellation",
         .body =
             loopValues + allocationLoop(selectedStore(comparedInput("%negative", "%one"))) + done,
         .confinedNested = "fresh"},
        {.what = "strict comparison correspondence preserves the guard dependency gap",
         .body =
             loopValues + allocationLoop(comparedGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "strict comparison correspondence cannot overwrite its guard dependency",
         .body =
             loopValues +
             allocationLoop(nestedSiblingStore("      %read = ctjs.get_property %guard[%zero]\n" +
                                               comparedInput("%read", "%one"))) +
             done,
         .complete = false},
        {.what = "a different strict comparison left operand keeps an independent choice",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput("%zero", "%one", "strict_eq %q, %zero"))) +
             done,
         .complete = false},
        {.what = "a different strict comparison right operand keeps an independent choice",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput("%zero", "%one", "strict_eq %p, %one"))) +
             done,
         .complete = false},
        {.what = "reversed strict comparison operands share exact SSA correspondence",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput("%zero", "%one", "strict_eq %zero, %p"))) +
             done,
         .confinedNested = "fresh"},
        {.what = "a coercing comparison cannot borrow strict equality correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput("%zero", "%one", "eq %p, %zero"))) +
                 done,
         .complete = false},
        {.what = "identical Number bits share a strict comparison input",
         .body = loopValues + "  %otherZero = ctjs.constant #ctjs.number<0>\n" +
                 allocationLoop(
                     selectedStore(comparedInput("%zero", "%one", "strict_eq %p, %otherZero"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "separate strict comparison reads retain their own snapshots",
         .body = loopValues + allocationLoop(comparedReads()) + done,
         .complete = false},
        {.what = "mutating between strict comparison reads cannot reuse the first choice",
         .body = loopValues +
                 allocationLoop(comparedReads("    ctjs.set_property %guard[%zero], %one\n")) +
                 done,
         .complete = false},
        {.what = "dominating cross-block strict comparisons share exact SSA choices",
         .body =
             loopValues + allocationLoop(selectedStore(crossBlockComparisons) + "    }\n") + done,
         .confinedNested = "fresh"},
        {.what = "strict comparison choices still need independent finite Number operands",
         .body = loopValues + allocationLoop(selectedStore(comparedInput("%zero", "%p"))) + done,
         .complete = false},
        {.what = "strict comparison correspondence cannot cancel a nonfinite Number",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(comparedInput("%zero", "%nan"))) + done,
         .complete = false},
        {.what = "a returned strict comparison holder retains its child",
         .body = loopValues + allocationLoop(comparedKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a strict comparison zero key releases its stored child",
         .body = loopValues + allocationLoop(comparedKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved strict comparison alias survives clearing its holder",
         .body = loopValues + allocationLoop(comparedKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "every retaining exit preserves a child stored through strict comparisons",
         .body = loopValues + allocationLoop(comparedKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "strict comparison correlation preserves no-result transient cycle checks",
         .body = loopValues +
                 allocationLoop(comparedKeys + "    scf.if %secondFlag {\n"
                                               "      ctjs.set_property %fresh[%zero], %guard\n"
                                               "      ctjs.set_property %fresh[%zero], %zero\n"
                                               "    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "strict comparison correspondence cannot hide a producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput("%zero", "%one", "strict_eq %p, %zero",
                                                            "    %called = ctjs.call %p(%q)\n"))) +
                 done,
         .complete = false},
        {.what = "strict comparisons cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(comparedKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize strict comparison allocation identities",
         .body = loopValues + allocationLoop(comparedKeys, "%one") + done,
         .complete = false},
        {.what = "strict comparison choices still require initialized solver inputs",
         .body = loopValues + allocationLoop(comparedKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "a reversed first strict comparison remains the shared representative",
         .body = loopValues + allocationLoop(selectedStore(firstReversed)) + done,
         .confinedNested = "fresh"},
        {.what = "nested reversed strict comparisons preserve Number correspondence",
         .body = loopValues + allocationLoop(nestedSiblingStore(reversedInput())) + done,
         .confinedNested = "fresh"},
        {.what = "reversed strict comparisons compose with odd logical Not parity",
         .body =
             loopValues + allocationLoop(selectedStore(reversedInput("%zero", "%one", 3))) + done,
         .confinedNested = "fresh"},
        {.what = "reversed strict comparisons compose with even logical Not parity",
         .body =
             loopValues + allocationLoop(selectedStore(reversedInput("%zero", "%one", 2))) + done,
         .confinedNested = "fresh"},
        {.what = "reversed strict comparisons retain signed finite Number cancellation",
         .body =
             loopValues + allocationLoop(selectedStore(reversedInput("%negative", "%one"))) + done,
         .confinedNested = "fresh"},
        {.what = "reversed strict comparisons preserve the guard dependency gap",
         .body =
             loopValues + allocationLoop(reversedGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "reversed strict comparisons cannot overwrite their guard dependency",
         .body =
             loopValues +
             allocationLoop(nestedSiblingStore("      %read = ctjs.get_property %guard[%zero]\n" +
                                               reversedInput("%read", "%one"))) +
             done,
         .complete = false},
        {.what = "a changed reversed operand cannot reuse a strict comparison choice",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput("%zero", "%one", "strict_eq %zero, %q"))) +
             done,
         .complete = false},
        {.what = "reversed identical Number bits share a strict comparison input",
         .body = loopValues + "  %otherZero = ctjs.constant #ctjs.number<0>\n" +
                 allocationLoop(
                     selectedStore(comparedInput("%zero", "%one", "strict_eq %otherZero, %p"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "identical Number literals preserve odd Not comparison polarity",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(literalComparisons("#ctjs.number<0>", "#ctjs.number<0>", 3))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "identical Number literals retain a dominating cross-block comparison",
         .body = loopValues + allocationLoop(crossBlockStore(literalComparisons())) + done,
         .confinedNested = "fresh"},
        {.what = "identical negative-zero bits are exact strict comparison operands",
         .body = loopValues +
                 allocationLoop(selectedStore(literalComparisons(
                     "#ctjs.number<9223372036854775808>", "#ctjs.number<9223372036854775808>"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "identical NaN bits correlate predicates without Number cancellation",
         .body = loopValues +
                 allocationLoop(selectedStore(literalComparisons(
                     "#ctjs.number<9221120237041090560>", "#ctjs.number<9221120237041090560>"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "different Number bits remain independent comparison operands",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.number<0>", "#ctjs.number<4607182418800017408>"))) +
                 done,
         .complete = false},
        {.what = "opposite signed zeroes gain no literal comparison equivalence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.number<0>", "#ctjs.number<9223372036854775808>"))) +
                 done,
         .complete = false},
        {.what = "Number and String literals cannot share strict comparison inputs",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(literalComparisons("#ctjs.number<0>", "#ctjs.string<\"0\">"))) +
                 done,
         .complete = false},
        {.what = "exact String literals share strict comparison inputs",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.string<\"0\">", "#ctjs.string<\"0\">"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "exact Boolean literals share strict comparison inputs",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.boolean<false>", "#ctjs.boolean<false>"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "exact Null literals share strict comparison inputs",
         .body = loopValues +
                 allocationLoop(selectedStore(literalComparisons("#ctjs.null", "#ctjs.null"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "exact Undefined literals share strict comparison inputs",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(literalComparisons("#ctjs.undefined", "#ctjs.undefined"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "exact BigInt spellings share strict comparison inputs",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.bigint<\"1\">", "#ctjs.bigint<\"1\">"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "different Strings retain independent comparison inputs",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.string<\"0\">", "#ctjs.string<\"1\">"))) +
                 done,
         .complete = false},
        {.what = "different Booleans retain independent comparison inputs",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.boolean<false>", "#ctjs.boolean<true>"))) +
                 done,
         .complete = false},
        {.what = "Null and Undefined cannot borrow loose equality correspondence",
         .body =
             loopValues +
             allocationLoop(selectedStore(literalComparisons("#ctjs.null", "#ctjs.undefined"))) +
             done,
         .complete = false},
        {.what = "Boolean and Number literals retain distinct tags",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(literalComparisons("#ctjs.boolean<false>", "#ctjs.number<0>"))) +
                 done,
         .complete = false},
        {.what = "different BigInt spellings gain no parsed value correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.bigint<\"1\">", "#ctjs.bigint<\"01\">"))) +
                 done,
         .complete = false},
        {.what = "exact String comparison inputs preserve odd Not polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     literalComparisons("#ctjs.string<\"0\">", "#ctjs.string<\"0\">", 3))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "exact String comparison inputs retain a dominating cross-block key",
         .body = loopValues +
                 allocationLoop(crossBlockStore(
                     literalComparisons("#ctjs.string<\"0\">", "#ctjs.string<\"0\">"))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a reversed coercing comparison cannot borrow strict equality correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput("%zero", "%one", "eq %zero, %p"))) +
                 done,
         .complete = false},
        {.what = "reversed strict comparisons keep separate read snapshots distinct",
         .body = loopValues + allocationLoop(comparedReads("", true)) + done,
         .complete = false},
        {.what = "mutation between reversed strict comparison reads cannot reuse a choice",
         .body =
             loopValues +
             allocationLoop(comparedReads("    ctjs.set_property %guard[%zero], %one\n", true)) +
             done,
         .complete = false},
        {.what = "reversed strict comparisons still need independent finite Number operands",
         .body = loopValues + allocationLoop(selectedStore(reversedInput("%zero", "%p"))) + done,
         .complete = false},
        {.what = "reversed strict comparisons cannot cancel NaN Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(reversedInput("%zero", "%nan"))) + done,
         .complete = false},
        {.what = "a returned reversed-comparison holder retains its stored child",
         .body = loopValues + allocationLoop(reversedKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a reversed-comparison key releases its stored child",
         .body = loopValues + allocationLoop(reversedKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved reversed-comparison alias survives clearing its holder",
         .body = loopValues + allocationLoop(reversedKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "every retaining exit preserves a child stored through reversed comparisons",
         .body = loopValues + allocationLoop(reversedKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "reversed strict comparisons preserve no-result transient cycle checks",
         .body = loopValues +
                 allocationLoop(reversedKeys + "    scf.if %secondFlag {\n"
                                               "      ctjs.set_property %fresh[%zero], %guard\n"
                                               "      ctjs.set_property %fresh[%zero], %zero\n"
                                               "    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "reversed strict comparisons cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(reversedKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "reversed strict comparisons still require current initialized solver inputs",
         .body = loopValues + allocationLoop(reversedKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "nested comparison duplicates retain the outermost dominating representative",
         .body = loopValues + allocationLoop(outerRepresentative) + done,
         .confinedNested = "fresh"},
        {.what = "cross-block reversed comparisons compose with logical Not parity",
         .body =
             loopValues + allocationLoop(crossBlockStore(reversedInput("%zero", "%one", 3))) + done,
         .confinedNested = "fresh"},
        {.what = "cross-block correspondence preserves independent guard read gaps",
         .body = loopValues + allocationLoop(crossBlockGap, "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "cross-block correspondence cannot overwrite its guard dependency",
         .body = loopValues +
                 allocationLoop(crossBlockStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                                comparedInput("%read", "%one"))) +
                 done,
         .complete = false},
        {.what = "cross-block correspondence still needs independently finite Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(crossBlockStore(comparedInput("%zero", "%nan"))) + done,
         .complete = false},
        {.what = "cross-block comparison reads preserve distinct snapshots after mutation",
         .body = loopValues + allocationLoop(crossBlockReads) + done,
         .complete = false},
        {.what = "comparison correspondence stops at the counted loop boundary",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput().substr(firstComparison.size())),
                                "%zero", "%one", "%zero", "", firstComparison) +
                 done,
         .complete = false},
        {.what = "a returned cross-block holder retains its child on every structural path",
         .body = loopValues + allocationLoop(crossBlockKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a cross-block holder releases its stored child",
         .body =
             loopValues + allocationLoop(crossBlockKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved cross-block alias survives clearing its holder",
         .body = loopValues + allocationLoop(crossBlockKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "cross-block correlation preserves no-result transient cycle checks",
         .body = loopValues +
                 allocationLoop(crossBlockStore(comparedInput(),
                                                "    scf.if %secondFlag {\n"
                                                "      ctjs.set_property %fresh[%zero], %guard\n"
                                                "      ctjs.set_property %fresh[%zero], %zero\n"
                                                "    }\n")) +
                 clearGuard + done,
         .complete = false},
        {.what = "a dominating comparison cannot hide a nested producer effect",
         .body = loopValues +
                 allocationLoop(crossBlockStore(comparedInput(
                     "%zero", "%one", "strict_eq %p, %zero", "    %called = ctjs.call %p(%q)\n"))) +
                 done,
         .complete = false},
        {.what = "cross-block comparisons cannot authorize repeated allocation identities",
         .body =
             loopValues + allocationLoop(crossBlockKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "cross-block comparisons cannot supply missing solver inputs",
         .body = loopValues + allocationLoop(crossBlockKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "a strict true test of ToBoolean retains its exact input choice",
         .body = loopValues + allocationLoop(strictBooleanKeys) + done,
         .confinedNested = "fresh"},
        {.what = "a strict false test aligns opposite Boolean branches",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%boolean", "#ctjs.boolean<false>"), true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a Boolean literal on the left retains strict test correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictBooleanFlag("%boolean", "#ctjs.boolean<true>",
                                       "    %boolean = ctjs.convert to_boolean %p\n", true)))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a strict false test of logical Not preserves positive input polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%boolean", "#ctjs.boolean<false>",
                                                    "    %boolean = ctjs.unary not %p\n")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "strict Boolean tests preserve dominating strict comparison choices",
         .body = loopValues +
                 allocationLoop(crossBlockStore(representativeInput(strictBooleanFlag(
                     "%secondCompare", "#ctjs.boolean<true>",
                     "    %secondCompare = ctjs.compare strict_eq %p, %zero\n")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested strict false tests retain exact Boolean polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictBooleanFlag("%inner", "#ctjs.boolean<false>",
                                       "    %boolean = ctjs.convert to_boolean %p\n"
                                       "    %no = ctjs.constant #ctjs.boolean<false>\n"
                                       "    %inner = ctjs.compare strict_eq %boolean, %no\n")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "strict Boolean tests preserve the independent guard read gap",
         .body = loopValues +
                 allocationLoop(strictBooleanGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "an opaque input cannot borrow the tag of a Boolean test literal",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%p", "#ctjs.boolean<true>", "")))) +
                 done,
         .complete = false},
        {.what = "a truthy Number literal cannot stand in for a strict Boolean test",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictBooleanFlag("%boolean", "#ctjs.number<4607182418800017408>")))) +
                 done,
         .complete = false},
        {.what = "a truthy String literal cannot stand in for a strict Boolean test",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%boolean", "#ctjs.string<\"true\">")))) +
                 done,
         .complete = false},
        {.what = "a null literal cannot stand in for a strict false test",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%boolean", "#ctjs.null"), true))) +
                 done,
         .complete = false},
        {.what = "an undefined literal cannot stand in for a strict false test",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%boolean", "#ctjs.undefined"), true))) +
                 done,
         .complete = false},
        {.what = "a strict false test with unswapped arms proves no correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictBooleanFlag("%boolean", "#ctjs.boolean<false>")))) +
                 done,
         .complete = false},
        {.what = "strict Boolean tests cannot overwrite their guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     siblingInput("%read", "%two", "%secondFlag", strictBooleanFlag()))) +
                 done,
         .complete = false},
        {.what = "strict Boolean tests retain separate mutable read snapshots",
         .body = loopValues + allocationLoop(strictBooleanReads) + done,
         .complete = false},
        {.what = "strict Boolean tests cannot hide an unsupported producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictBooleanFlag("%boolean", "#ctjs.boolean<true>",
                                       "    %called = ctjs.call %p(%q)\n"
                                       "    %boolean = ctjs.convert to_boolean %p\n")))) +
                 done,
         .complete = false},
        {.what = "strict Boolean tests still require finite selected Number values",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%nan", "%secondFlag", strictBooleanFlag()))) +
                 done,
         .complete = false},
        {.what = "a returned strict Boolean test holder retains its selected child",
         .body = loopValues + allocationLoop(strictBooleanKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a strict Boolean test holder releases its selected child",
         .body =
             loopValues + allocationLoop(strictBooleanKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved strict Boolean test child survives clearing its holder",
         .body = loopValues + allocationLoop(strictBooleanKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "strict Boolean tests cannot authorize repeated allocation identities",
         .body =
             loopValues + allocationLoop(strictBooleanKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize strict Boolean test allocation identities",
         .body = loopValues + allocationLoop(strictBooleanKeys, "%one") + done,
         .complete = false},
        {.what = "strict Boolean tests still require initialized solver inputs",
         .body = loopValues + allocationLoop(strictBooleanKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "strict true tests follow both exact Boolean carrier yields",
         .body = loopValues + allocationLoop(carrierKeys) + done,
         .confinedNested = "fresh"},
        {.what = "strict false carrier tests preserve opposite branch polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictCarrier(booleanYields, "%forwarded", "#ctjs.boolean<false>"), true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "a left Boolean literal follows the selected Boolean tuple result",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag",
                 strictCarrier(booleanYieldTuple, "%forwarded#1", "#ctjs.boolean<true>", true)))) +
             done,
         .confinedNested = "fresh"},
        {.what = "strict Boolean tests follow a carrier of complementary literals",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           strictCarrier(literalSelection())))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "strict Boolean carrier tests preserve matching comparison snapshots",
         .body =
             loopValues +
             allocationLoop(selectedStore(representativeInput(strictCarrier(comparisonYields())))) +
             done,
         .confinedNested = "fresh"},
        {.what = "strict Boolean carrier tests preserve an independent guard read gap",
         .body =
             loopValues + allocationLoop(carrierGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "an opaque carrier yield cannot borrow its sibling Boolean tag",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictCarrier(forwardedFlag("%thenBoolean", "%p", thenBoolean))))) +
                 done,
         .complete = false},
        {.what = "a Number tuple result cannot borrow a sibling Boolean result tag",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictCarrier(booleanYieldTuple, "%forwarded#0")))) +
                 done,
         .complete = false},
        {.what = "different Boolean carrier inputs prove no correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictCarrier(forwardedFlag(
                                      "%thenBoolean", "%elseBoolean", thenBoolean,
                                      "      %elseBoolean = ctjs.convert to_boolean %q\n"))))) +
                 done,
         .complete = false},
        {.what = "a strict false carrier test requires swapped selected arms",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictCarrier(booleanYields, "%forwarded", "#ctjs.boolean<false>")))) +
                 done,
         .complete = false},
        {.what = "Boolean carrier tests retain separate mutable read snapshots",
         .body = loopValues + allocationLoop(carrierReads) + done,
         .complete = false},
        {.what = "Boolean carrier tests cannot hide an unsupported arm effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     strictCarrier(forwardedFlag("%thenBoolean", "%elseEven",
                                                 "      %called = ctjs.call %p(%q)\n" + thenBoolean,
                                                 elseBoolean))))) +
                 done,
         .complete = false},
        {.what = "Boolean carrier tests cannot overwrite their guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     siblingInput("%read", "%two", "%secondFlag", carrierFlag))) +
                 done,
         .complete = false},
        {.what = "Boolean carrier tests still require finite retained Number values",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%nan", "%secondFlag", carrierFlag))) +
                 done,
         .complete = false},
        {.what = "a returned Boolean carrier holder retains its selected child",
         .body = loopValues + allocationLoop(carrierKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a Boolean carrier holder releases its selected child",
         .body = loopValues + allocationLoop(carrierKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved Boolean carrier child survives clearing its holder",
         .body = loopValues + allocationLoop(carrierKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "Boolean carrier tests cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(carrierKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "Boolean carrier tests still require initialized solver inputs",
         .body = loopValues + allocationLoop(carrierKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "strict Boolean tests follow nested exact carrier yields",
         .body = loopValues + allocationLoop(nestedCarrierKeys) + done,
         .confinedNested = "fresh"},
        {.what = "nested strict false tests preserve reversed Boolean polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     nestedCarrier(booleanYields, "%forwarded", "#ctjs.boolean<false>"), true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested Boolean tuple forwarding preserves a left literal test",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag",
                 nestedCarrier(booleanYieldTuple, "%forwarded#1", "#ctjs.boolean<true>", true)))) +
             done,
         .confinedNested = "fresh"},
        {.what = "nested Boolean carriers preserve an independent guard read gap",
         .body = loopValues +
                 allocationLoop(nestedCarrierGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "a nested opaque yield cannot borrow Boolean tags from other leaves",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     nestedCarrier(forwardedFlag("%thenBoolean", "%p", thenBoolean))))) +
                 done,
         .complete = false},
        {.what = "nested forwarding cannot lend a Boolean tuple tag to a Number result",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  nestedCarrier(booleanYieldTuple, "%forwarded#0")))) +
                 done,
         .complete = false},
        {.what = "nested exact Boolean tags do not establish input correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  nestedCarrier(forwardedFlag(
                                      "%thenBoolean", "%elseBoolean", thenBoolean,
                                      "      %elseBoolean = ctjs.convert to_boolean %q\n"))))) +
                 done,
         .complete = false},
        {.what = "nested Boolean carriers retain separate mutable read snapshots",
         .body = loopValues + allocationLoop(nestedCarrierReads) + done,
         .complete = false},
        {.what = "nested Boolean forwarding cannot hide an inner arm effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     nestedCarrier(forwardedFlag("%thenBoolean", "%elseEven",
                                                 "      %called = ctjs.call %p(%q)\n" + thenBoolean,
                                                 elseBoolean))))) +
                 done,
         .complete = false},
        {.what = "nested Boolean carriers cannot overwrite their guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     siblingInput("%read", "%two", "%secondFlag", nestedCarrierFlag))) +
                 done,
         .complete = false},
        {.what = "a returned nested Boolean carrier holder retains its child",
         .body = loopValues + allocationLoop(nestedCarrierKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a nested Boolean carrier holder releases its child",
         .body =
             loopValues + allocationLoop(nestedCarrierKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved nested Boolean carrier child survives clearing its holder",
         .body = loopValues + allocationLoop(nestedCarrierKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "an inner identical SSA yield composes with an outer Boolean carrier",
         .body = loopValues + allocationLoop(identityKeys) + done,
         .confinedNested = "fresh"},
        {.what = "inner identity tuple forwarding preserves strict false test polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  strictCarrier(identityCarrier(identityTuple, "%identity#1"),
                                                "%forwarded", "#ctjs.boolean<false>", true),
                                  true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "inner identical forwarding preserves an independent guard read gap",
         .body =
             loopValues + allocationLoop(identityGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "a changed inner then yield loses exact identity forwarding",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     identityCarrier(selectedInput("%q", "%boolean", "", "", "%identity"))))) +
                 done,
         .complete = false},
        {.what = "a changed inner else yield loses exact identity forwarding",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     identityCarrier(selectedInput("%boolean", "%q", "", "", "%identity"))))) +
                 done,
         .complete = false},
        {.what = "inner identity forwarding cannot lend correspondence to another outer arm",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  identityCarrier(identityYield, "%identity", "%q")))) +
                 done,
         .complete = false},
        {.what = "inner identity tuple forwarding cannot borrow a sibling Boolean slot",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag", identityCarrier(identityTuple, "%identity#0")))) +
             done,
         .complete = false},
        {.what = "inner identity carriers preserve distinct mutable read snapshots",
         .body = loopValues + allocationLoop(identityReads) + done,
         .complete = false},
        {.what = "inner identical yields do not erase arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     identityCarrier(selectedInput("%boolean", "%boolean", "",
                                                   "      %called = ctjs.call %p(%q)\n",
                                                   "%identity"))))) +
                 done,
         .complete = false},
        {.what = "inner identical forwarding cannot overwrite its guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     siblingInput("%read", "%two", "%secondFlag", identityFlag))) +
                 done,
         .complete = false},
        {.what = "a returned inner-identity carrier holder retains its child",
         .body = loopValues + allocationLoop(identityKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing an inner-identity carrier holder releases its child",
         .body = loopValues + allocationLoop(identityKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved inner-identity carrier child survives clearing its holder",
         .body = loopValues + allocationLoop(identityKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "strict comparison operands forward an identical saved SSA value",
         .body = loopValues + allocationLoop(operandKeys) + done,
         .confinedNested = "fresh"},
        {.what = "three nested selections preserve one saved SSA truthiness",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", nestedTruthiness))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested saved truthiness preserves Not polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", nestedTruthinessNot, true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested saved truthiness follows the corresponding tuple slot",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", nestedTruthinessTuple))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested saved truthiness cannot borrow another tuple slot",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", otherTruthinessSlot))) +
                 done,
         .complete = false},
        {.what = "nested saved truthiness keeps distinct mutable read snapshots",
         .body = loopValues + allocationLoop(nestedTruthinessReads) + done,
         .complete = false},
        {.what = "nested saved truthiness cannot suppress inner then effects",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", nestedTruthinessThenEffect))) +
                 done,
         .complete = false},
        {.what = "nested saved truthiness cannot suppress inner else effects",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", nestedTruthinessElseEffect))) +
                 done,
         .complete = false},
        {.what = "identity operands preserve the corresponding tuple slot and Not polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %zero, %operand#1", operandTuple, 3))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "strict comparison operands follow nested identical SSA forwarding",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     comparedInput("%zero", "%one", "strict_eq %outerOperand, %zero",
                                   operandCarrier + selectedInput("%operand", "%operand", "", "",
                                                                  "%outerOperand")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "identity operands preserve exact immutable literal comparison inputs",
         .body = loopValues + "  %otherZero = ctjs.constant #ctjs.number<0>\n" +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %p, %operand",
                     selectedInput("%otherZero", "%otherZero", "", "", "%operand")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "strict comparisons recognize nested branches with one saved SSA leaf",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     comparedInput("%zero", "%one", "strict_eq %operand, %zero",
                                   selectedInput("%innerOperand", "%p",
                                                 selectedInput("%p", "%p", "", "", "%innerOperand"),
                                                 "", "%operand")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested saved operands retain reversed comparison order and Not polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %zero, %operand", nestedOperandForwarding, 3))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested operand branches follow the corresponding saved SSA tuple slot",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %outerOperand, %zero",
                     selectedInput("%operand#1", "%p", operandTuple, "", "%outerOperand")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested operand branches cannot borrow another tuple slot's SSA identity",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %outerOperand, %zero",
                     selectedInput("%operand#0", "%p", operandTuple, "", "%outerOperand")))) +
                 done,
         .complete = false},
        {.what = "nested saved operand leaves preserve distinct mutable read snapshots",
         .body = loopValues + allocationLoop(nestedOperandReads) + done,
         .complete = false},
        {.what = "nested saved operand leaves cannot suppress inner then arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %operand, %zero",
                     selectedInput("%innerOperand", "%p",
                                   selectedInput("%p", "%p", "      %called = ctjs.call %p(%q)\n",
                                                 "", "%innerOperand"),
                                   "", "%operand")))) +
                 done,
         .complete = false},
        {.what = "nested saved operand leaves cannot suppress inner else arm effects",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput(
                 "%zero", "%one", "strict_eq %operand, %zero",
                 selectedInput("%innerOperand", "%p",
                               selectedInput("%p", "%p", "", "      %called = ctjs.call %p(%q)\n",
                                             "%innerOperand"),
                               "", "%operand")))) +
             done,
         .complete = false},
        {.what = "strict comparisons recognize distinct equal Number operand leaves",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput(
                 "%zero", "%one", "strict_eq %p, %operand",
                 selectedInput("%thenZero", "%elseZero",
                               "      %thenZero = ctjs.constant #ctjs.number<0>\n",
                               "      %elseZero = ctjs.constant #ctjs.number<0>\n", "%operand")))) +
             done,
         .confinedNested = "fresh"},
        {.what = "equal primitive leaves retain reversed operand order and Not polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %operand, %p", equalOperandForwarding, 3))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "equal primitive leaves follow nested selected tuple slots",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %p, %operand",
                     selectedInput("%operandTuple#1", "%outerZero", equalOperandTuple,
                                   "      %outerZero = ctjs.constant #ctjs.number<0>\n",
                                   "%operand")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "another tuple slot cannot borrow equal primitive leaves",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %p, %operand",
                     selectedInput("%operandTuple#0", "%outerZero", equalOperandTuple,
                                   "      %outerZero = ctjs.constant #ctjs.number<0>\n",
                                   "%operand")))) +
                 done,
         .complete = false},
        {.what = "two selected comparison operands independently prove all primitive leaves",
         .body = loopValues + allocationLoop(selectedStore(pairedOperandInput)) + done,
         .confinedNested = "fresh"},
        {.what = "equal String operand leaves retain their exact primitive attribute",
         .body = loopValues +
                 allocationLoop(selectedStore(primitiveOperandInput("#ctjs.string<\"0\">"))) + done,
         .confinedNested = "fresh"},
        {.what = "equal BigInt operand leaves retain their exact primitive attribute",
         .body = loopValues +
                 allocationLoop(selectedStore(primitiveOperandInput("#ctjs.bigint<\"1\">"))) + done,
         .confinedNested = "fresh"},
        {.what = "equal Boolean operand leaves do not require an opaque input Boolean tag",
         .body = loopValues +
                 allocationLoop(selectedStore(primitiveOperandInput("#ctjs.boolean<true>"))) + done,
         .confinedNested = "fresh"},
        {.what = "equal null operand leaves retain their exact primitive attribute",
         .body =
             loopValues + allocationLoop(selectedStore(primitiveOperandInput("#ctjs.null"))) + done,
         .confinedNested = "fresh"},
        {.what = "equal undefined operand leaves retain their exact primitive attribute",
         .body = loopValues +
                 allocationLoop(selectedStore(primitiveOperandInput("#ctjs.undefined"))) + done,
         .confinedNested = "fresh"},
        {.what = "equal primitive leaf proof preserves Number signed-zero bits",
         .body = loopValues +
                 allocationLoop(selectedStore(primitiveOperandInput(
                     "#ctjs.number<0>", "#ctjs.number<9223372036854775808>"))) +
                 done,
         .complete = false},
        {.what = "equal primitive leaf proof preserves BigInt literal spelling",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     primitiveOperandInput("#ctjs.bigint<\"1\">", "#ctjs.bigint<\"01\">"))) +
                 done,
         .complete = false},
        {.what = "equal primitive leaves preserve distinct mutable comparison reads",
         .body = loopValues + allocationLoop(equalOperandReads) + done,
         .complete = false},
        {.what = "equal primitive leaves cannot suppress then arm effects",
         .body =
             loopValues +
             allocationLoop(selectedStore(comparedInput(
                 "%zero", "%one", "strict_eq %p, %operand",
                 selectedInput("%thenZero", "%elseZero",
                               "      %thenZero = ctjs.constant #ctjs.number<0>\n"
                               "      %called = ctjs.call %p(%q)\n",
                               "      %elseZero = ctjs.constant #ctjs.number<0>\n", "%operand")))) +
             done,
         .complete = false},
        {.what = "equal primitive leaves cannot suppress else arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %p, %operand",
                     selectedInput("%thenZero", "%elseZero",
                                   "      %thenZero = ctjs.constant #ctjs.number<0>\n",
                                   "      %elseZero = ctjs.constant #ctjs.number<0>\n"
                                   "      %called = ctjs.call %p(%q)\n",
                                   "%operand")))) +
                 done,
         .complete = false},
        {.what = "a returned equal-primitive holder retains its child",
         .body = loopValues + allocationLoop(equalOperandKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing an equal-primitive holder releases its child",
         .body =
             loopValues + allocationLoop(equalOperandKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved equal-primitive child survives clearing its holder",
         .body = loopValues + allocationLoop(equalOperandKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "identity comparison operands preserve the independent guard read gap",
         .body =
             loopValues + allocationLoop(operandGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "a changed then comparison operand yield cannot borrow saved identity",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(comparedInput("%zero", "%one", "strict_eq %operand, %zero",
                                                 selectedInput("%q", "%p", "", "", "%operand")))) +
                 done,
         .complete = false},
        {.what = "a changed else comparison operand yield cannot borrow saved identity",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(comparedInput("%zero", "%one", "strict_eq %operand, %zero",
                                                 selectedInput("%p", "%q", "", "", "%operand")))) +
                 done,
         .complete = false},
        {.what = "identity comparison operands cannot borrow a sibling tuple slot",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     comparedInput("%zero", "%one", "strict_eq %operand#0, %zero", operandTuple))) +
                 done,
         .complete = false},
        {.what = "ToBoolean changes the strict comparison operand value",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %operand, %zero",
                     "    %converted = ctjs.convert to_boolean %p\n" +
                         selectedInput("%converted", "%converted", "", "", "%operand")))) +
                 done,
         .complete = false},
        {.what = "logical Not changes the strict comparison operand value",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %operand, %zero",
                     "    %converted = ctjs.unary not %p\n" +
                         selectedInput("%converted", "%converted", "", "", "%operand")))) +
                 done,
         .complete = false},
        {.what = "identity comparison operands preserve distinct mutable read snapshots",
         .body = loopValues + allocationLoop(operandReads) + done,
         .complete = false},
        {.what = "identical comparison operand yields preserve then arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     comparedInput("%zero", "%one", "strict_eq %operand, %zero",
                                   selectedInput("%p", "%p", "      %called = ctjs.call %p(%q)\n",
                                                 "", "%operand")))) +
                 done,
         .complete = false},
        {.what = "identical comparison operand yields preserve else arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(comparedInput(
                     "%zero", "%one", "strict_eq %operand, %zero",
                     selectedInput("%p", "%p", "", "      %called = ctjs.call %p(%q)\n",
                                   "%operand")))) +
                 done,
         .complete = false},
        {.what = "identity comparison operands cannot overwrite their guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     comparedInput("%read", "%two", "strict_eq %operand, %zero", operandCarrier))) +
                 done,
         .complete = false},
        {.what = "a returned identity-operand holder retains its child",
         .body = loopValues + allocationLoop(operandKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing an identity-operand holder releases its child",
         .body = loopValues + allocationLoop(operandKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved identity-operand child survives clearing its holder",
         .body = loopValues + allocationLoop(operandKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "strict Boolean tests forward an identical literal operand",
         .body = loopValues + allocationLoop(literalOperandKeys) + done,
         .confinedNested = "fresh"},
        {.what = "strict Boolean tests recognize distinct equal literal arms",
         .body = loopValues + allocationLoop(equalLiteralKeys) + done,
         .confinedNested = "fresh"},
        {.what = "distinct left false literals preserve reversed predicate polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(strictBooleanFlag("%boolean", "#ctjs.boolean<false>",
                                                    "    %boolean = ctjs.convert to_boolean %p\n",
                                                    true),
                                  equalFalseForwarding),
                     true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "equal literal leaves survive nested selected-slot transport",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(strictBooleanFlag(),
                                  equalLiteralTuple + selectedInput("%literalTuple#1",
                                                                    "%literalTuple#1", "", "",
                                                                    "%literalOperand"))))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "another tuple slot cannot borrow equal Boolean leaves",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(strictBooleanFlag(),
                                  equalLiteralTuple + selectedInput("%literalTuple#0",
                                                                    "%literalTuple#0", "", "",
                                                                    "%literalOperand"))))) +
                 done,
         .complete = false},
        {.what = "equal literal arms preserve distinct mutable read snapshots",
         .body = loopValues +
                 allocationLoop(carryLiteral(strictBooleanReads, equalLiteralForwarding)) + done,
         .complete = false},
        {.what = "equal literal leaves cannot suppress then arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(
                         strictBooleanFlag(),
                         selectedInput("%thenLiteral", "%elseLiteral",
                                       "      %thenLiteral = ctjs.constant #ctjs.boolean<true>\n"
                                       "      %called = ctjs.call %p(%q)\n",
                                       "      %elseLiteral = ctjs.constant #ctjs.boolean<true>\n",
                                       "%literalOperand"))))) +
                 done,
         .complete = false},
        {.what = "equal literal leaves cannot suppress else arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(
                         strictBooleanFlag(),
                         selectedInput("%thenLiteral", "%elseLiteral",
                                       "      %thenLiteral = ctjs.constant #ctjs.boolean<true>\n",
                                       "      %elseLiteral = ctjs.constant #ctjs.boolean<true>\n"
                                       "      %called = ctjs.call %p(%q)\n",
                                       "%literalOperand"))))) +
                 done,
         .complete = false},
        {.what = "a returned equal-literal holder retains its child",
         .body = loopValues + allocationLoop(equalLiteralKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing an equal-literal holder releases its child",
         .body =
             loopValues + allocationLoop(equalLiteralKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved equal-literal child survives clearing its holder",
         .body = loopValues + allocationLoop(equalLiteralKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "a forwarded left false literal preserves reversed predicate polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(strictBooleanFlag("%boolean", "#ctjs.boolean<false>",
                                                    "    %boolean = ctjs.convert to_boolean %p\n",
                                                    true),
                                  literalForwarding),
                     true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "Boolean literal operands follow nested identity forwarding",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  carryLiteral(strictBooleanFlag(),
                                               selectedInput("%booleanLiteral", "%booleanLiteral",
                                                             "", "", "%innerLiteral") +
                                                   selectedInput("%innerLiteral", "%innerLiteral",
                                                                 "", "", "%literalOperand"))))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "Boolean literal forwarding preserves the independent guard read gap",
         .body = loopValues +
                 allocationLoop(literalOperandGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "literal operand forwarding keeps distinct mutable read snapshots",
         .body = loopValues + allocationLoop(carryLiteral(strictBooleanReads, literalForwarding)) +
                 done,
         .complete = false},
        {.what = "a forwarded Number literal cannot borrow a Boolean tag",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag",
                 carryLiteral(strictBooleanFlag("%boolean", "#ctjs.number<4607182418800017408>"),
                              literalForwarding)))) +
             done,
         .complete = false},
        {.what = "a forwarded Boolean literal does not prove an opaque input Boolean",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  carryLiteral(strictBooleanFlag("%p"), literalForwarding)))) +
                 done,
         .complete = false},
        {.what = "identical Boolean literal yields preserve then arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  carryLiteral(strictBooleanFlag(),
                                               selectedInput("%booleanLiteral", "%booleanLiteral",
                                                             "      %called = ctjs.call %p(%q)\n",
                                                             "", "%literalOperand"))))) +
                 done,
         .complete = false},
        {.what = "identical Boolean literal yields preserve else arm effects",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     carryLiteral(strictBooleanFlag(),
                                  selectedInput("%booleanLiteral", "%booleanLiteral", "",
                                                "      %called = ctjs.call %p(%q)\n",
                                                "%literalOperand"))))) +
                 done,
         .complete = false},
        {.what = "a returned literal-operand holder retains its child",
         .body = loopValues + allocationLoop(literalOperandKeys) + "  ctjs.return %guard\n"},
        {.what = "clearing a literal-operand holder releases its child",
         .body = loopValues + allocationLoop(literalOperandKeys) + clearGuard +
                 "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved literal-operand child survives clearing its holder",
         .body = loopValues + allocationLoop(literalOperandKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "ToBoolean preserves the exact input truthiness choice",
         .body = loopValues + allocationLoop(booleanKeys) + done,
         .confinedNested = "fresh"},
        {.what = "ToBoolean before and after Not preserves opposite branch choices",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", booleanNot, true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "ToBoolean composes with dominating strict comparison correspondence",
         .body = loopValues + allocationLoop(crossBlockStore(booleanComparison)) + done,
         .confinedNested = "fresh"},
        {.what = "ToBoolean correspondence preserves the independent guard read gap",
         .body =
             loopValues + allocationLoop(booleanGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "ToBoolean on another input cannot reuse a truthiness choice",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", unrelatedBoolean))) +
                 done,
         .complete = false},
        {.what = "numeric conversion cannot borrow ToBoolean correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", numericCondition))) +
                 done,
         .complete = false},
        {.what = "ToBoolean correspondence cannot cancel nonfinite selected Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%nan", "%secondFlag", booleanFlag))) +
                 done,
         .complete = false},
        {.what = "every retaining exit preserves the child selected through ToBoolean",
         .body = loopValues + allocationLoop(booleanKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a ToBoolean-selected holder releases its stored child",
         .body = loopValues + allocationLoop(booleanKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a ToBoolean-selected saved alias survives clearing its holder",
         .body = loopValues + allocationLoop(booleanKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "ToBoolean correspondence preserves transient no-result cycle checks",
         .body = loopValues +
                 allocationLoop(booleanKeys + "    scf.if %secondFlag {\n"
                                              "      ctjs.set_property %fresh[%zero], %guard\n"
                                              "      ctjs.set_property %fresh[%zero], %zero\n"
                                              "    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "ToBoolean correspondence cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(booleanKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "ToBoolean correspondence still requires current initialized solver inputs",
         .body = loopValues + allocationLoop(booleanKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "equal If yields retain the exact input truthiness choice",
         .body = loopValues + allocationLoop(forwardedKeys) + done,
         .confinedNested = "fresh"},
        {.what = "equal If yields use the selected tuple slot despite a differing sibling slot",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", forwardedTuple))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "nested equal If yields compose with ToBoolean and Not polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", forwardedNot, true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "equal If yields preserve dominating strict comparison correspondence",
         .body = loopValues + allocationLoop(crossBlockStore(forwardedComparison)) + done,
         .confinedNested = "fresh"},
        {.what = "equal If yields preserve the independent guard read gap",
         .body = loopValues + allocationLoop(forwardedGap, "%two", "%one", "%zero, %zero, %zero") +
                 done,
         .confinedNested = "fresh"},
        {.what = "a changed then yield cannot borrow same-input truthiness correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", forwardedFlag("%q", "%p")))) +
                 done,
         .complete = false},
        {.what = "a changed else yield cannot borrow same-input truthiness correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", forwardedFlag("%p", "%q")))) +
                 done,
         .complete = false},
        {.what = "equal If yields of another input keep their own truthiness choice",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", forwardedFlag("%q", "%q")))) +
                 done,
         .complete = false},
        {.what = "equal If yields cannot hide a then-arm producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     forwardedFlag("%p", "%p", "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "equal If yields cannot hide an else-arm producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     forwardedFlag("%p", "%p", "", "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "equal If yields still require independently finite selected Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%nan", "%secondFlag", forwardedFlag()))) +
                 done,
         .complete = false},
        {.what = "equal If yields cannot overwrite the guard dependency",
         .body = loopValues +
                 allocationLoop(
                     selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                   siblingInput("%read", "%one", "%secondFlag", forwardedFlag()))) +
                 done,
         .complete = false},
        {.what = "every retaining exit preserves a child selected through equal If yields",
         .body = loopValues + allocationLoop(forwardedKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing an equal-yield holder releases its stored child",
         .body = loopValues + allocationLoop(forwardedKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved equal-yield alias survives clearing its holder",
         .body = loopValues + allocationLoop(forwardedKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "equal If yields retain no-result transient cycle checks",
         .body = loopValues +
                 allocationLoop(forwardedKeys + "    scf.if %secondFlag {\n"
                                                "      ctjs.set_property %fresh[%zero], %guard\n"
                                                "      ctjs.set_property %fresh[%zero], %zero\n"
                                                "    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "equal If yields cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(forwardedKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "equal If yields still require current initialized solver inputs",
         .body = loopValues + allocationLoop(forwardedKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "distinct Boolean yield chains preserve the exact input truthiness choice",
         .body = loopValues + allocationLoop(booleanYieldKeys) + done,
         .confinedNested = "fresh"},
        {.what = "distinct odd Boolean yield chains reverse the selected Number arms",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", negatedYields, true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "Boolean yield chains use only their corresponding tuple result slot",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag", booleanYieldTuple))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "Boolean yield chains preserve dominating strict comparison correspondence",
         .body = loopValues + allocationLoop(crossBlockStore(booleanYieldComparison)) + done,
         .confinedNested = "fresh"},
        {.what = "Boolean yield chains preserve the independently proved guard read gap",
         .body = loopValues +
                 allocationLoop(booleanYieldGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "different yield input snapshots cannot acquire Boolean correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%one", "%secondFlag",
                                  forwardedFlag("%thenBoolean", "%elseEven",
                                                "      %thenBoolean = ctjs.convert to_boolean %q\n",
                                                elseBoolean)))) +
                 done,
         .complete = false},
        {.what = "opposite yield polarities cannot acquire Boolean correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     forwardedFlag("%thenBoolean", "%elseNot", thenBoolean, elseBoolean)))) +
                 done,
         .complete = false},
        {.what = "odd Boolean yield chains cannot retain unswapped Number arms",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", negatedYields))) +
                 done,
         .complete = false},
        {.what = "numeric conversion in a yield cannot borrow Boolean correspondence",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag",
                 forwardedFlag("%thenBoolean", "%elseEven",
                               "      %thenBoolean = ctjs.convert to_number %p\n", elseBoolean)))) +
             done,
         .complete = false},
        {.what = "Boolean yield chains cannot hide a then-arm producer effect",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag",
                 forwardedFlag("%thenBoolean", "%elseEven",
                               thenBoolean + "      %called = ctjs.call %p(%q)\n", elseBoolean)))) +
             done,
         .complete = false},
        {.what = "Boolean yield chains cannot hide an else-arm producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     forwardedFlag("%thenBoolean", "%elseEven", thenBoolean,
                                   elseBoolean + "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "Boolean yield chains still require independently finite selected Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%nan", "%secondFlag", booleanYields))) +
                 done,
         .complete = false},
        {.what = "Boolean yield chains cannot overwrite their current guard dependency",
         .body = loopValues +
                 allocationLoop(
                     selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                   siblingInput("%read", "%one", "%secondFlag", booleanYields))) +
                 done,
         .complete = false},
        {.what = "every retaining exit preserves a child selected through Boolean yields",
         .body = loopValues + allocationLoop(booleanYieldKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a Boolean-yield holder releases its stored child",
         .body =
             loopValues + allocationLoop(booleanYieldKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved Boolean-yield alias survives clearing its holder",
         .body = loopValues + allocationLoop(booleanYieldKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "Boolean yield chains retain every no-result transient cycle check",
         .body = loopValues +
                 allocationLoop(booleanYieldKeys + "    scf.if %secondFlag {\n"
                                                   "      ctjs.set_property %fresh[%zero], %guard\n"
                                                   "      ctjs.set_property %fresh[%zero], %zero\n"
                                                   "    }\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "Boolean yield chains cannot authorize repeated allocation identities",
         .body =
             loopValues + allocationLoop(booleanYieldKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot authorize Boolean-yield allocation identities",
         .body =
             loopValues + allocationLoop(booleanYieldKeys, "%two", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "Boolean yield chains require initialized solver inputs",
         .body = loopValues + allocationLoop(booleanYieldKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "distinct comparison yields share one dominating StrictEq representative",
         .body = loopValues + allocationLoop(representativeKeys) + done,
         .confinedNested = "fresh"},
        {.what = "reversed comparison yield operands preserve the dominating representative",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(
                     comparisonYields("strict_eq %zero, %p", "strict_eq %p, %zero")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "comparison yields retain their representative across enclosing If prefixes",
         .body = loopValues +
                 allocationLoop(localArray +
                                nestedArm(representativeComparisons +
                                          "    ctjs.set_property %guard[%key], %fresh\n")) +
                 done,
         .confinedNested = "fresh"},
        {.what = "comparison yields preserve independently proved guard read gaps",
         .body = loopValues +
                 allocationLoop(representativeGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "matching sibling comparisons retain their dominating If result",
         .body = loopValues + allocationLoop(selectedStore(siblingOnlyComparisons)) + done,
         .confinedNested = "fresh"},
        {.what = "identical Number literals match independent sibling comparison yields",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     independentComparisons(representativeInput(literalComparisonYields)))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "identical Number literals cannot erase sibling comparison polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(
                     independentComparisons(representativeInput(literalComparisonYields), true))) +
                 done,
         .complete = false},
        {.what = "identical Number comparison operands cannot hide sibling effects",
         .body = loopValues +
                 allocationLoop(selectedStore(independentComparisons(representativeInput(
                     "    %otherZero = ctjs.constant #ctjs.number<0>\n" +
                     comparisonYields("strict_eq %p, %otherZero", "strict_eq %otherZero, %p", "",
                                      "      %called = ctjs.call %p(%q)\n"))))) +
                 done,
         .complete = false},
        {.what = "identical Number comparison operands cannot overwrite the guard dependency",
         .body = loopValues +
                 allocationLoop(nestedSiblingStore(
                     "      %read = ctjs.get_property %guard[%zero]\n" +
                     comparedInput("%read", "%one", "strict_eq %p, %otherZero",
                                   "    %otherZero = ctjs.constant #ctjs.number<0>\n"))) +
                 done,
         .complete = false},
        {.what = "an earlier negated comparison If retains its own polarity",
         .body = loopValues + allocationLoop(selectedStore(reversedSiblingComparisons)) + done,
         .confinedNested = "fresh"},
        {.what = "negated sibling comparison results require reversed Number arms",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(independentComparisons(representativeComparisons, true))) +
                 done,
         .complete = false},
        {.what = "sibling comparison results preserve independent guard read gaps",
         .body = loopValues +
                 allocationLoop(siblingComparisonGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "a later sibling comparison with different operands stays independent",
         .body = loopValues +
                 allocationLoop(selectedStore(independentComparisons(
                     representativeInput(comparisonYields("strict_eq %q, %zero"))))) +
                 done,
         .complete = false},
        {.what = "a coercing sibling comparison cannot reuse an earlier If result",
         .body = loopValues +
                 allocationLoop(selectedStore(independentComparisons(
                     representativeInput(comparisonYields("eq %p, %zero"))))) +
                 done,
         .complete = false},
        {.what = "sibling comparison results still require finite selected Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(independentComparisons(
                     representativeInput(comparisonYields(), "%zero", "%nan")))) +
                 done,
         .complete = false},
        {.what = "sibling comparison results cannot hide an else-arm effect",
         .body = loopValues +
                 allocationLoop(selectedStore(independentComparisons(representativeInput(
                     comparisonYields("strict_eq %p, %zero", "strict_eq %p, %zero", "",
                                      "      %called = ctjs.call %p(%q)\n"))))) +
                 done,
         .complete = false},
        {.what = "every exit retains a child stored through sibling comparison results",
         .body = loopValues + allocationLoop(siblingComparisonKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a sibling comparison holder releases its child",
         .body = loopValues + allocationLoop(siblingComparisonKeys) + clearGuard +
                 "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved sibling comparison alias survives clearing its holder",
         .body = loopValues + allocationLoop(siblingComparisonKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "sibling comparison results preserve all-write cycle checks",
         .body = loopValues +
                 allocationLoop(siblingComparisonKeys +
                                "    ctjs.set_property %fresh[%zero], %guard\n"
                                "    ctjs.set_property %fresh[%zero], %zero\n") +
                 clearGuard + done,
         .complete = false},
        {.what = "sibling comparison results cannot authorize repeated allocation identities",
         .body = loopValues +
                 allocationLoop(siblingComparisonKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "sibling comparison results require current initialized solver inputs",
         .body = loopValues + allocationLoop(siblingComparisonKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "sibling comparison results cannot overwrite their guard dependency",
         .body = loopValues +
                 allocationLoop(selectedStore("    %read = ctjs.get_property %guard[%zero]\n" +
                                              independentComparisons(representativeInput(
                                                  comparisonYields(), "%read", "%one")))) +
                 done,
         .complete = false},
        {.what = "different then comparison operands cannot share the representative",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(
                     comparisonYields("strict_eq %q, %zero", "strict_eq %p, %zero")))) +
                 done,
         .complete = false},
        {.what = "different else comparison operands cannot share the representative",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(
                     comparisonYields("strict_eq %p, %zero", "strict_eq %p, %one")))) +
                 done,
         .complete = false},
        {.what = "comparison yields require matching Boolean polarity",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(oppositeComparisonYield))) + done,
         .complete = false},
        {.what = "coercing comparison yields cannot borrow strict correspondence",
         .body =
             loopValues +
             allocationLoop(selectedStore(representativeInput(comparisonYields("eq %p, %zero")))) +
             done,
         .complete = false},
        {.what = "a common comparison representative cannot hide a then-arm effect",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(
                     comparisonYields("strict_eq %p, %zero", "strict_eq %p, %zero",
                                      "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "a common comparison representative cannot hide an else-arm effect",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(
                     comparisonYields("strict_eq %p, %zero", "strict_eq %p, %zero", "",
                                      "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "comparison yields still require independently finite Number arms",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(
                     selectedStore(representativeInput(comparisonYields(), "%zero", "%nan"))) +
                 done,
         .complete = false},
        {.what = "every retaining exit keeps children selected through comparison yields",
         .body = loopValues + allocationLoop(representativeKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a comparison-yield holder releases its selected child",
         .body = loopValues + allocationLoop(representativeKeys) + clearGuard +
                 "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved comparison-yield child alias survives clearing its holder",
         .body = loopValues + allocationLoop(representativeKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "comparison yields cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(representativeKeys, "%zero", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "comparison yields require initialized solver inputs",
         .body = loopValues + allocationLoop(representativeKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "complementary Boolean yields preserve exact condition correspondence",
         .body = loopValues + allocationLoop(literalKeys) + done,
         .confinedNested = "fresh"},
        {.what = "opposite Boolean yields reverse the selected Number arms",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag", literalSelection("%no", "%yes"), true))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "complementary Boolean yields respect their own tuple result slot",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(siblingInput("%zero", "%one", "%secondFlag", literalTuple))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "complementary Boolean yields compose with total Boolean chains",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     literalSelection("%thenNot", "%elseBoolean",
                                      "      %thenNot = ctjs.unary not %no\n",
                                      "      %elseBoolean = ctjs.convert to_boolean %no\n")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "complementary Boolean yields preserve strict comparison representatives",
         .body = loopValues +
                 allocationLoop(selectedStore(representativeInput(
                     literalSelection("%yes", "%no", "", "", "%firstCompare")))) +
                 done,
         .confinedNested = "fresh"},
        {.what = "complementary Boolean yields preserve independent guard read gaps",
         .body =
             loopValues + allocationLoop(literalGap, "%two", "%one", "%zero, %zero, %zero") + done,
         .confinedNested = "fresh"},
        {.what = "equal Boolean yields cannot borrow their condition correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           literalSelection("%yes", "%yes")))) +
                 done,
         .complete = false},
        {.what = "opposite Boolean yields cannot retain unswapped Number arms",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           literalSelection("%no", "%yes")))) +
                 done,
         .complete = false},
        {.what = "numeric yields cannot borrow literal Boolean correspondence",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           literalSelection("%one", "%zero")))) +
                 done,
         .complete = false},
        {.what = "complementary Boolean yields retain their own condition snapshot",
         .body =
             loopValues +
             allocationLoop(selectedStore(siblingInput(
                 "%zero", "%one", "%secondFlag", literalSelection("%yes", "%no", "", "", "%q")))) +
             done,
         .complete = false},
        {.what = "complementary Boolean yields cannot hide a then-arm producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     literalSelection("%yes", "%no", "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "complementary Boolean yields cannot hide an else-arm producer effect",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput(
                     "%zero", "%one", "%secondFlag",
                     literalSelection("%yes", "%no", "", "      %called = ctjs.call %p(%q)\n")))) +
                 done,
         .complete = false},
        {.what = "complementary Boolean yields still require finite selected Numbers",
         .body = loopValues + "  %nan = ctjs.constant #ctjs.number<9221120237041090560>\n" +
                 allocationLoop(selectedStore(
                     siblingInput("%zero", "%nan", "%secondFlag", literalSelection()))) +
                 done,
         .complete = false},
        {.what = "complementary Boolean yields cannot overwrite their guard dependency",
         .body =
             loopValues +
             allocationLoop(nestedSiblingStore(
                                "      %read = ctjs.get_property %guard[%zero]\n" +
                                siblingInput("%read", "%two", "%secondFlag", literalSelection())),
                            "%two", "%one", "%zero, %zero, %zero") +
             done,
         .complete = false},
        {.what = "every retaining exit keeps children selected through Boolean literals",
         .body = loopValues + allocationLoop(literalKeys) +
                 "  cf.cond_br %flag, ^keep, ^drop\n^keep:\n  ctjs.return %guard\n^drop:\n" + done},
        {.what = "clearing a Boolean-literal holder releases its selected child",
         .body = loopValues + allocationLoop(literalKeys) + clearGuard + "  ctjs.return %guard\n",
         .confinedNested = "fresh"},
        {.what = "a saved Boolean-literal child survives clearing its holder",
         .body = loopValues + allocationLoop(literalKeys) +
                 "  %read = ctjs.get_property %guard[%zero]\n" + clearGuard +
                 "  ctjs.return %read\n"},
        {.what = "complementary Boolean yields cannot authorize repeated allocation identities",
         .body = loopValues + allocationLoop(literalKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "complementary Boolean yields require initialized solver inputs",
         .body = loopValues + allocationLoop(literalKeys) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "calls in an arithmetic key arm still refuse the complete census",
         .body = loopValues +
                 allocationLoop(
                     selectedStore(arithmeticInput("%zero", "%one", "add %selectedKey, %zero",
                                                   "      %called = ctjs.call %p(%q)\n")),
                     "%one", "%one", "%zero, %zero") +
                 done,
         .complete = false},
        {.what = "conditional arithmetic keys require initialized solver inputs",
         .body = loopValues + allocationLoop(arithmeticKeys, "%one", "%one", "%zero, %zero") + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "conditional arithmetic cannot borrow a repeated allocation instance",
         .body =
             loopValues + allocationLoop(arithmeticKeys, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "zero visits cannot supply a conditional arithmetic allocation instance",
         .body = loopValues + allocationLoop(arithmeticKeys, "%two", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "an arm store cannot resize the array that proves its trip count",
         .body =
             loopValues +
             allocationLoop(localArray + "    scf.if %flag {\n"
                                         "      ctjs.set_property %guard[%name], %zero\n    }\n") +
             done,
         .complete = false},
        {.what = "zero body visits cannot prove nested store allocation instances",
         .body = loopValues + allocationLoop(conditionalStores, "%one") + done,
         .complete = false},
        {.what = "repeated bodies still refuse their nested guard-array stores",
         .body =
             loopValues + allocationLoop(conditionalStores, "%zero", "%one", "%zero, %zero") + done,
         .complete = false},
        {.what = "a nested counted store still requires initialized solver inputs",
         .body = loopValues + allocationLoop(conditionalStores) + done,
         .complete = false,
         .withAnalysis = false},
        {.what = "a nested loop cannot borrow the outer counted single-visit proof",
         .body = loopValues +
                 allocationLoop("    scf.if %flag {\n"
                                "      scf.while : () -> () {\n        scf.condition(%flag)\n"
                                "      } do {\n        scf.yield\n      }\n    }\n" +
                                localArray) +
                 done,
         .complete = false},
        {.what = "a root CFG single visit also confines a conditional object",
         .body = localStart + "  %two = ctjs.constant #ctjs.number<4611686018427387904>\n" +
                 countedStart + conditionalObject +
                 "  %step = ctjs.binary add %i, %two\n"
                 "  cf.br ^header(%step : !ctjs.value)\n^exit:\n" +
                 done,
         .confinedNested = "child"},
        {.what = "conditional origins cannot bypass raw-frame suspension refusal",
         .body = localStart + "  scf.if %flag {\n" + localArray +
                 "  }\n  %paused = ctjs.suspend await %p\n" + done,
         .complete = false},
        {.what = "conditional origins cannot bypass late raw arguments retention",
         .body = localStart + "  scf.if %flag {\n" + localArray +
                 "  }\n  %args = ctjs.make_arguments\n" + done,
         .complete = false},
        {.what = "an unsupported region cannot borrow conditional instance authority",
         .body =
             localStart + "  scf.execute_region {\n" + localArray + "    scf.yield\n  }\n" + done,
         .complete = false},
        {.what = "a nested receiver without solver lattices retains its gap refusal",
         .body = receiverStart + receiverWrite + "  }\n" + done,
         .complete = false,
         .withAnalysis = false,
         .confinedReceivers = ""},
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
        if (r.body == nestedOrigins.body) {
            ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
            mlir::Operation * nested = nullptr;
            function.walk([&](ctjs::CreateArrayOp site) {
                if (site->getParentRegion() != &function.getBody()) { nested = site; }
            });
            const row inventory{.what = r.what, .body = r.body, .expected = ""};
            const ArrayContentsEvidence contents = computeArrayContents(function);
            if (!contents.complete || contents.arrays.size() != 2 ||
                !llvm::is_contained(contents.arrays, nested)) {
                fail(inventory, "complete contents lost the nested allocation origin");
            }
            mlir::DataFlowSolver solver;
            solver.load<mlir::dataflow::DeadCodeAnalysis>();
            solver.load<mlir::dataflow::SparseConstantPropagation>();
            solver.load<EscapeAnalysis>();
            if (failed(solver.initializeAndRun(*module))) {
                fail(inventory, "the nested inventory solver did not converge");
                continue;
            }
            const EscapeVerdicts original = computeVerdicts(solver, function, 0);
            const auto found = original.sites.find(nested);
            if (original.sites.size() != 2 || found == original.sites.end() ||
                found->second.reason != EscapeReason::UnknownOp || found->second.by ||
                original.directStorage.complete) {
                fail(inventory, "unrefined nested inventory must retain its null-witness refusal");
            }
        }
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
    retention_row countedMutation{
        .what = "conditional allocation confinement reproves the current body visit count",
        .body =
            loopValues + allocationLoop(conditionalGraph, "%one", "%one", "%zero, %zero") + done,
        .confinedNested = "child,selected,other"};
    auto countedModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + countedMutation.body + "}\n", &context);
    if (countedModule) {
        ctjs::FuncOp function = *countedModule->getOps<ctjs::FuncOp>().begin();
        mlir::scf::WhileOp loop;
        function.walk([&](mlir::scf::WhileOp op) { loop = op; });
        const mlir::Value original = loop.getInits()[0];
        const mlir::Value zero = loop.getInits()[1];
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*countedModule))) {
            fail(row{.what = countedMutation.what, .body = countedMutation.body, .expected = ""},
                 "the conditional loop stale solver did not converge");
        }
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        for (const bool repeated : {false, true, false}) {
            loop->setOperand(0, repeated ? zero : original);
            countedMutation.complete = !repeated;
            countedMutation.confinedNested = repeated ? "" : "child,selected,other";
            budgets += checkArrayRetention(*countedModule, countedMutation);
            const EscapeVerdicts current = computeVerdicts(stale, function);
            if (current.arrayRetentionComplete != !repeated) {
                fail(row{.what = countedMutation.what,
                         .body = countedMutation.body,
                         .expected = countedMutation.confinedNested},
                     "a stale solver or forged marker supplied a single-visit proof");
            }
        }
    } else {
        fail(row{.what = countedMutation.what, .body = countedMutation.body, .expected = ""},
             "the current counted retention fixture did not parse");
    }
    retention_row inputMutation{
        .what = "branch-local store inputs are reproved after live key and receiver changes",
        .body = loopValues +
                allocationLoop(computedStore(loadedReceiver + computedKey, "%reloaded"), "%zero",
                               "%one", "%zero", "", guardAlias) +
                done,
        .discharged = "guard",
        .confinedNested = "fresh"};
    auto inputModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + inputMutation.body + "}\n", &context);
    if (inputModule) {
        ctjs::FuncOp function = *inputModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        inputModule->walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto receiver = store.getObject().getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalKey = key.getRhs();
        const mlir::Value originalReceiver = receiver.getObject();
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*inputModule))) {
            fail(row{.what = inputMutation.what, .body = inputMutation.body, .expected = ""},
                 "the branch-local input stale solver did not converge");
        }
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        for (unsigned state = 0; state < 5; ++state) {
            key->setOperand(1, state == 1 ? opaque : originalKey);
            receiver->setOperand(0, state == 3 ? opaque : originalReceiver);
            inputMutation.complete = state % 2 == 0;
            inputMutation.discharged = inputMutation.complete ? "guard" : "";
            inputMutation.confinedNested = inputMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*inputModule, inputMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete != inputMutation.complete) {
                fail(row{.what = inputMutation.what, .body = inputMutation.body, .expected = ""},
                     "a stale solver or forged marker supplied a branch-local input proof");
            }
        }
    } else {
        fail(row{.what = inputMutation.what, .body = inputMutation.body, .expected = ""},
             "the branch-local input mutation fixture did not parse");
    }
    retention_row yieldMutation{
        .what = "equal-yield store correspondence is reproved after a live payload change",
        .body = loopValues + allocationLoop(selectedStore(selectedInput("%i", "%i"))) + done,
        .confinedNested = "fresh"};
    auto yieldModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + yieldMutation.body + "}\n", &context);
    if (yieldModule) {
        ctjs::FuncOp function = *yieldModule->getOps<ctjs::FuncOp>().begin();
        mlir::scf::IfOp branch;
        function.walk([&](mlir::scf::IfOp op) { branch = op; });
        auto * yield = branch.getElseRegion().front().getTerminator();
        const mlir::Value original = yield->getOperand(0);
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*yieldModule))) {
            fail(row{.what = yieldMutation.what, .body = yieldMutation.body, .expected = ""},
                 "the equal-yield stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (const bool changed : {false, true, false}) {
            yield->setOperand(0, changed ? opaque : original);
            yieldMutation.complete = !changed;
            yieldMutation.confinedNested = changed ? "" : "fresh";
            budgets += checkArrayRetention(*yieldModule, yieldMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete != !changed) {
                fail(row{.what = yieldMutation.what, .body = yieldMutation.body, .expected = ""},
                     "a stale solver or forged marker supplied an equal-yield proof");
            }
        }
    } else {
        fail(row{.what = yieldMutation.what, .body = yieldMutation.body, .expected = ""},
             "the equal-yield mutation fixture did not parse");
    }
    retention_row footprintMutation{
        .what = "per-arm store footprints are reproved after live yield changes",
        .body = loopValues + allocationLoop(gapKeys, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto footprintModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + footprintMutation.body + "}\n", &context);
    if (footprintModule) {
        ctjs::FuncOp function = *footprintModule->getOps<ctjs::FuncOp>().begin();
        mlir::scf::IfOp branch;
        function.walk([&](mlir::scf::IfOp op) { branch = op; });
        auto * yield = branch.getElseRegion().front().getTerminator();
        const mlir::Value original = yield->getOperand(0);
        auto load = branch.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value overlap = load->getOperand(1);
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*footprintModule))) {
            fail(
                row{.what = footprintMutation.what, .body = footprintMutation.body, .expected = ""},
                "the per-arm footprint stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (mlir::Value value : {original, overlap, opaque, original}) {
            yield->setOperand(0, value);
            footprintMutation.complete = value == original;
            footprintMutation.confinedNested = footprintMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*footprintModule, footprintMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                footprintMutation.complete) {
                fail(row{.what = footprintMutation.what,
                         .body = footprintMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied a per-arm store footprint");
            }
        }
    } else {
        fail(row{.what = footprintMutation.what, .body = footprintMutation.body, .expected = ""},
             "the per-arm footprint mutation fixture did not parse");
    }
    retention_row arithmeticMutation{
        .what = "conditional arithmetic ranges are reproved after live operand mutations",
        .body = loopValues + allocationLoop(arithmeticGap, "%two", "%one", "%zero, %zero, %zero") +
                done,
        .confinedNested = "fresh"};
    auto arithmeticModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + arithmeticMutation.body + "}\n", &context);
    if (arithmeticModule) {
        ctjs::FuncOp function = *arithmeticModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto branch = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto * yield = branch.getElseRegion().front().getTerminator();
        const mlir::Value originalYield = yield->getOperand(0);
        const mlir::Value originalOffset = key.getRhs();
        auto load = branch.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value overlap = load->getOperand(1);
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*arithmeticModule))) {
            fail(row{.what = arithmeticMutation.what,
                     .body = arithmeticMutation.body,
                     .expected = ""},
                 "the conditional arithmetic stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 5; ++state) {
            yield->setOperand(0, state == 1 ? overlap : originalYield);
            key->setOperand(1, state == 2 ? overlap : state == 3 ? opaque : originalOffset);
            arithmeticMutation.complete = state == 0 || state == 4;
            arithmeticMutation.confinedNested = arithmeticMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*arithmeticModule, arithmeticMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                arithmeticMutation.complete) {
                fail(row{.what = arithmeticMutation.what,
                         .body = arithmeticMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied a conditional arithmetic range");
            }
        }
    } else {
        fail(row{.what = arithmeticMutation.what, .body = arithmeticMutation.body, .expected = ""},
             "the conditional arithmetic mutation fixture did not parse");
    }
    retention_row correlationMutation{
        .what = "Number cancellation is reproved after identity, value and guard-read mutations",
        .body = loopValues + allocationLoop(correlatedGap, "%two", "%one", "%zero, %zero, %zero") +
                done,
        .confinedNested = "fresh"};
    auto correlationModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + correlationMutation.body + "}\n", &context);
    if (correlationModule) {
        ctjs::FuncOp function = *correlationModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto branch = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto * yield = branch.getElseRegion().front().getTerminator();
        auto load = branch.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalYield = yield->getOperand(0);
        const mlir::Value originalInput = key.getRhs();
        const mlir::Value originalPosition = load->getOperand(1);
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*correlationModule))) {
            fail(row{.what = correlationMutation.what,
                     .body = correlationMutation.body,
                     .expected = ""},
                 "the cancellation stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 5; ++state) {
            key->setOperand(1, state == 1 ? originalPosition : originalInput);
            yield->setOperand(0, state == 2 ? opaque : originalYield);
            load->setOperand(1, state == 3 ? zero : originalPosition);
            correlationMutation.complete = state == 0 || state == 4;
            correlationMutation.confinedNested = correlationMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*correlationModule, correlationMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                correlationMutation.complete) {
                fail(row{.what = correlationMutation.what,
                         .body = correlationMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied a Number cancellation proof");
            }
        }
    } else {
        fail(
            row{.what = correlationMutation.what, .body = correlationMutation.body, .expected = ""},
            "the cancellation mutation fixture did not parse");
    }
    retention_row pairedMutation{
        .what = "paired slot cancellation reproves current yields and guard reads",
        .body =
            loopValues + allocationLoop(pairedGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto pairedModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + pairedMutation.body + "}\n", &context);
    if (pairedModule) {
        ctjs::FuncOp function = *pairedModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto branch = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = branch.getThenRegion().front().getTerminator();
        auto * elseYield = branch.getElseRegion().front().getTerminator();
        auto load = thenYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalThen = thenYield->getOperand(0);
        const mlir::Value originalElse = elseYield->getOperand(1);
        const mlir::Value originalPosition = load->getOperand(1);
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*pairedModule))) {
            fail(row{.what = pairedMutation.what, .body = pairedMutation.body, .expected = ""},
                 "the paired cancellation stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 5; ++state) {
            elseYield->setOperand(1, state == 1 ? originalPosition : originalElse);
            thenYield->setOperand(0, state == 2 ? opaque : originalThen);
            thenYield->setOperand(1, state == 2 ? opaque : originalThen);
            load->setOperand(1, state == 3 ? zero : originalPosition);
            pairedMutation.complete = state == 0 || state == 4;
            pairedMutation.confinedNested = pairedMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*pairedModule, pairedMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                pairedMutation.complete) {
                fail(row{.what = pairedMutation.what, .body = pairedMutation.body, .expected = ""},
                     "a stale solver or forged marker supplied paired slot cancellation");
            }
        }
    } else {
        fail(row{.what = pairedMutation.what, .body = pairedMutation.body, .expected = ""},
             "the paired cancellation mutation fixture did not parse");
    }
    retention_row siblingMutation{
        .what = "sibling cancellation reproves conditions, yields and guard reads",
        .body =
            loopValues + allocationLoop(siblingGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto siblingModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + siblingMutation.body + "}\n", &context);
    if (siblingModule) {
        ctjs::FuncOp function = *siblingModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = first.getThenRegion().front().getTerminator();
        auto * elseYield = second.getElseRegion().front().getTerminator();
        auto load = thenYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalCondition = second.getCondition();
        const mlir::Value originalElse = elseYield->getOperand(0);
        const mlir::Value originalPosition = load->getOperand(1);
        mlir::OpBuilder at(second);
        auto independent = ctjs::TruthyOp::create(at, second.getLoc(), at.getI1Type(),
                                                  function.getBody().front().getArgument(4));
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*siblingModule))) {
            fail(row{.what = siblingMutation.what, .body = siblingMutation.body, .expected = ""},
                 "the sibling cancellation stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 5; ++state) {
            second->setOperand(0, state == 1 ? independent.getResult() : originalCondition);
            elseYield->setOperand(0, state == 2 ? originalPosition : originalElse);
            load->setOperand(1, state == 3 ? zero : originalPosition);
            siblingMutation.complete = state == 0 || state == 4;
            siblingMutation.confinedNested = siblingMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*siblingModule, siblingMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                siblingMutation.complete) {
                fail(
                    row{.what = siblingMutation.what, .body = siblingMutation.body, .expected = ""},
                    "a stale solver or forged marker supplied sibling condition correlation");
            }
        }
    } else {
        fail(row{.what = siblingMutation.what, .body = siblingMutation.body, .expected = ""},
             "the sibling cancellation mutation fixture did not parse");
    }
    retention_row nestedSiblingMutation{
        .what = "nested sibling correspondence reproves live conditions, yields and guard reads",
        .body = loopValues +
                allocationLoop(nestedSiblingGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto nestedSiblingModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + nestedSiblingMutation.body + "}\n", &context);
    if (nestedSiblingModule) {
        ctjs::FuncOp function = *nestedSiblingModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = first.getThenRegion().front().getTerminator();
        auto * elseYield = second.getElseRegion().front().getTerminator();
        auto load = thenYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalCondition = second.getCondition();
        const mlir::Value originalElse = elseYield->getOperand(0);
        const mlir::Value originalPosition = load->getOperand(1);
        mlir::OpBuilder at(second);
        auto independent = ctjs::TruthyOp::create(at, second.getLoc(), at.getI1Type(),
                                                  function.getBody().front().getArgument(4));
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*nestedSiblingModule))) {
            fail(row{.what = nestedSiblingMutation.what,
                     .body = nestedSiblingMutation.body,
                     .expected = ""},
                 "the nested sibling stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 5; ++state) {
            second->setOperand(0, state == 1 ? independent.getResult() : originalCondition);
            elseYield->setOperand(0, state == 2 ? originalPosition : originalElse);
            load->setOperand(1, state == 3 ? zero : originalPosition);
            nestedSiblingMutation.complete = state == 0 || state == 4;
            nestedSiblingMutation.confinedNested = nestedSiblingMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*nestedSiblingModule, nestedSiblingMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                nestedSiblingMutation.complete) {
                fail(row{.what = nestedSiblingMutation.what,
                         .body = nestedSiblingMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied nested predicate correspondence");
            }
        }
    } else {
        fail(row{.what = nestedSiblingMutation.what,
                 .body = nestedSiblingMutation.body,
                 .expected = ""},
             "the nested sibling mutation fixture did not parse");
    }
    retention_row recomputedMutation{
        .what = "recomputed correspondence reproves live truthiness inputs, yields and guard reads",
        .body = loopValues + allocationLoop(recomputedGap, "%two", "%one", "%zero, %zero, %zero") +
                done,
        .confinedNested = "fresh"};
    auto recomputedModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + recomputedMutation.body + "}\n", &context);
    if (recomputedModule) {
        ctjs::FuncOp function = *recomputedModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto truthy = second.getCondition().getDefiningOp<ctjs::TruthyOp>();
        auto * thenYield = first.getThenRegion().front().getTerminator();
        auto * elseYield = second.getElseRegion().front().getTerminator();
        auto load = thenYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = truthy.getValue();
        const mlir::Value originalElse = elseYield->getOperand(0);
        const mlir::Value originalPosition = load->getOperand(1);
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*recomputedModule))) {
            fail(row{.what = recomputedMutation.what,
                     .body = recomputedMutation.body,
                     .expected = ""},
                 "the recomputed truthiness stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 5; ++state) {
            truthy->setOperand(0, state == 1 ? function.getBody().front().getArgument(4)
                                             : originalInput);
            elseYield->setOperand(0, state == 2 ? originalPosition : originalElse);
            load->setOperand(1, state == 3 ? zero : originalPosition);
            recomputedMutation.complete = state == 0 || state == 4;
            recomputedMutation.confinedNested = recomputedMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*recomputedModule, recomputedMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                recomputedMutation.complete) {
                fail(row{.what = recomputedMutation.what,
                         .body = recomputedMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied recomputed truthiness "
                     "correspondence");
            }
        }
    } else {
        fail(row{.what = recomputedMutation.what, .body = recomputedMutation.body, .expected = ""},
             "the recomputed truthiness mutation fixture did not parse");
    }
    retention_row oppositeMutation{
        .what = "opposite correspondence reproves live unary kinds, inputs, yields and guard reads",
        .body =
            loopValues + allocationLoop(oppositeGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto oppositeModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + oppositeMutation.body + "}\n", &context);
    if (oppositeModule) {
        ctjs::FuncOp function = *oppositeModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto unary = second.getCondition()
                         .getDefiningOp<ctjs::TruthyOp>()
                         .getValue()
                         .getDefiningOp<ctjs::UnaryOp>();
        auto * firstYield = first.getThenRegion().front().getTerminator();
        auto * secondYield = second.getThenRegion().front().getTerminator();
        auto load = firstYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = unary.getOperand();
        const mlir::Value originalYield = secondYield->getOperand(0);
        const mlir::Value originalPosition = load->getOperand(1);
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*oppositeModule))) {
            fail(row{.what = oppositeMutation.what, .body = oppositeMutation.body, .expected = ""},
                 "the opposite predicate stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 6; ++state) {
            unary->setOperand(0, state == 1 ? function.getBody().front().getArgument(4)
                                            : originalInput);
            unary.setKindAttr(ctjs::UnaryKindAttr::get(
                &context, state == 2 ? ctjs::UnaryKind::Void : ctjs::UnaryKind::Not));
            secondYield->setOperand(0, state == 3 ? originalPosition : originalYield);
            load->setOperand(1, state == 4 ? zero : originalPosition);
            oppositeMutation.complete = state == 0 || state == 5;
            oppositeMutation.confinedNested = oppositeMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*oppositeModule, oppositeMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                oppositeMutation.complete) {
                fail(row{.what = oppositeMutation.what,
                         .body = oppositeMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied opposite predicate correspondence");
            }
        }
    } else {
        fail(row{.what = oppositeMutation.what, .body = oppositeMutation.body, .expected = ""},
             "the opposite predicate mutation fixture did not parse");
    }
    retention_row chainMutation{
        .what = "Not-chain correspondence reproves interior links, polarity, yields and guards",
        .body = loopValues + allocationLoop(chainGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto chainModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + chainMutation.body + "}\n", &context);
    if (chainModule) {
        ctjs::FuncOp function = *chainModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto outer = second.getCondition()
                         .getDefiningOp<ctjs::TruthyOp>()
                         .getValue()
                         .getDefiningOp<ctjs::UnaryOp>();
        auto middle = outer.getOperand().getDefiningOp<ctjs::UnaryOp>();
        auto inner = middle.getOperand().getDefiningOp<ctjs::UnaryOp>();
        auto * firstYield = first.getThenRegion().front().getTerminator();
        auto * secondYield = second.getThenRegion().front().getTerminator();
        auto load = firstYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = inner.getOperand();
        const mlir::Value originalYield = secondYield->getOperand(0);
        const mlir::Value originalPosition = load->getOperand(1);
        mlir::Value zero;
        for (auto literal : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue());
                number && number.getBits() == 0) {
                zero = literal;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*chainModule))) {
            fail(row{.what = chainMutation.what, .body = chainMutation.body, .expected = ""},
                 "the Not-chain stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 7; ++state) {
            inner->setOperand(0, state == 1 ? function.getBody().front().getArgument(4)
                                            : originalInput);
            middle.setKindAttr(ctjs::UnaryKindAttr::get(
                &context, state == 2 ? ctjs::UnaryKind::Void : ctjs::UnaryKind::Not));
            outer->setOperand(0, state == 3 ? inner.getResult() : middle.getResult());
            secondYield->setOperand(0, state == 4 ? originalPosition : originalYield);
            load->setOperand(1, state == 5 ? zero : originalPosition);
            chainMutation.complete = state == 0 || state == 6;
            chainMutation.confinedNested = chainMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*chainModule, chainMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete != chainMutation.complete) {
                fail(row{.what = chainMutation.what, .body = chainMutation.body, .expected = ""},
                     "a stale solver or forged marker supplied Not-chain correspondence");
            }
        }
    } else {
        fail(row{.what = chainMutation.what, .body = chainMutation.body, .expected = ""},
             "the Not-chain mutation fixture did not parse");
    }
    retention_row strictBooleanMutation{
        .what = "strict Boolean tests reprove tags, producers, operands, polarity and guards",
        .body = loopValues +
                allocationLoop(strictBooleanGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto strictBooleanModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + strictBooleanMutation.body + "}\n", &context);
    if (strictBooleanModule) {
        ctjs::FuncOp function = *strictBooleanModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto conversion = compare.getLhs().getDefiningOp<ctjs::ConvertOp>();
        auto literal = compare.getRhs().getDefiningOp<ctjs::ConstantOp>();
        auto * thenYield = second.getThenRegion().front().getTerminator();
        auto * elseYield = second.getElseRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = conversion.getOperand();
        const mlir::Value originalThen = thenYield->getOperand(0);
        const mlir::Value originalElse = elseYield->getOperand(0);
        const mlir::Value originalPosition = load.getKey();
        mlir::Value zero;
        for (auto constant : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue());
                number && number.getBits() == 0) {
                zero = constant;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*strictBooleanModule))) {
            fail(row{.what = strictBooleanMutation.what,
                     .body = strictBooleanMutation.body,
                     .expected = ""},
                 "the strict Boolean test stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 14; ++state) {
            conversion->setOperand(0, state == 1 ? function.getBody().front().getArgument(4)
                                                 : originalInput);
            conversion.setKindAttr(ctjs::ConvertKindAttr::get(
                &context, state == 2 ? ctjs::ConvertKind::ToNumber : ctjs::ConvertKind::ToBoolean));
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 3 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            literal.setValueAttr(
                state == 5
                    ? mlir::Attribute(ctjs::NumberAttr::get(&context, 4607182418800017408ULL))
                    : mlir::Attribute(ctjs::BooleanAttr::get(&context, state != 4 && state != 11)));
            compare->setOperand(0, state == 6   ? originalInput
                                   : state == 9 ? literal.getResult()
                                                : conversion.getResult());
            compare->setOperand(1, state == 9 ? conversion.getResult() : literal.getResult());
            const bool swapped = state == 11 || state == 12;
            thenYield->setOperand(0, state == 7 ? originalPosition
                                     : swapped  ? originalElse
                                                : originalThen);
            elseYield->setOperand(0, swapped ? originalThen : originalElse);
            load->setOperand(1, state == 8 ? zero : originalPosition);
            strictBooleanMutation.complete =
                state == 0 || state == 9 || state == 10 || state == 11 || state == 13;
            strictBooleanMutation.confinedNested = strictBooleanMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*strictBooleanModule, strictBooleanMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                strictBooleanMutation.complete) {
                fail(row{.what = strictBooleanMutation.what,
                         .body = strictBooleanMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied strict Boolean test correspondence");
            }
        }
    } else {
        fail(row{.what = strictBooleanMutation.what,
                 .body = strictBooleanMutation.body,
                 .expected = ""},
             "the strict Boolean test mutation fixture did not parse");
    }
    retention_row carrierMutation{
        .what = "strict Boolean carriers reprove live yields, tags, polarity and guards",
        .body =
            loopValues + allocationLoop(carrierGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto carrierModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + carrierMutation.body + "}\n", &context);
    if (carrierModule) {
        ctjs::FuncOp function = *carrierModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto carrier = compare.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto literal = compare.getRhs().getDefiningOp<ctjs::ConstantOp>();
        auto * carrierThen = carrier.getThenRegion().front().getTerminator();
        auto * carrierElse = carrier.getElseRegion().front().getTerminator();
        auto conversion = carrierThen->getOperand(0).getDefiningOp<ctjs::ConvertOp>();
        auto even = carrierElse->getOperand(0).getDefiningOp<ctjs::UnaryOp>();
        auto odd = even.getOperand().getDefiningOp<ctjs::UnaryOp>();
        auto * thenYield = second.getThenRegion().front().getTerminator();
        auto * elseYield = second.getElseRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = conversion.getOperand();
        const mlir::Value originalThen = thenYield->getOperand(0);
        const mlir::Value originalElse = elseYield->getOperand(0);
        const mlir::Value originalPosition = load.getKey();
        mlir::Value zero;
        for (auto constant : function.getBody().front().getOps<ctjs::ConstantOp>()) {
            if (auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue());
                number && number.getBits() == 0) {
                zero = constant;
            }
        }
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*carrierModule))) {
            fail(row{.what = carrierMutation.what, .body = carrierMutation.body, .expected = ""},
                 "the Boolean carrier stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 15; ++state) {
            conversion->setOperand(0, state == 1 ? function.getBody().front().getArgument(4)
                                                 : originalInput);
            conversion.setKindAttr(ctjs::ConvertKindAttr::get(
                &context, state == 2 ? ctjs::ConvertKind::ToNumber : ctjs::ConvertKind::ToBoolean));
            carrierThen->setOperand(0, state == 12 ? zero : conversion.getResult());
            carrierElse->setOperand(0, state == 3   ? odd.getResult()
                                       : state == 4 ? originalInput
                                                    : even.getResult());
            odd.setKindAttr(ctjs::UnaryKindAttr::get(&context, state == 13 ? ctjs::UnaryKind::Void
                                                                           : ctjs::UnaryKind::Not));
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 5 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            literal.setValueAttr(
                state == 7
                    ? mlir::Attribute(ctjs::NumberAttr::get(&context, 4607182418800017408ULL))
                    : mlir::Attribute(ctjs::BooleanAttr::get(&context, state != 6 && state != 9)));
            compare->setOperand(0, state == 8 ? literal.getResult() : carrier.getResult(0));
            compare->setOperand(1, state == 8 ? carrier.getResult(0) : literal.getResult());
            const bool swapped = state == 9 || state == 10;
            thenYield->setOperand(0, swapped ? originalElse : originalThen);
            elseYield->setOperand(0, swapped ? originalThen : originalElse);
            load->setOperand(1, state == 11 ? zero : originalPosition);
            carrierMutation.complete = state == 0 || state == 8 || state == 9 || state == 14;
            carrierMutation.confinedNested = carrierMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*carrierModule, carrierMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                carrierMutation.complete) {
                fail(
                    row{.what = carrierMutation.what, .body = carrierMutation.body, .expected = ""},
                    "a stale solver or forged marker supplied Boolean carrier correspondence");
            }
        }
    } else {
        fail(row{.what = carrierMutation.what, .body = carrierMutation.body, .expected = ""},
             "the Boolean carrier mutation fixture did not parse");
    }
    retention_row nestedCarrierMutation{
        .what = "nested Boolean carriers reprove every live leaf and forwarding yield",
        .body = loopValues +
                allocationLoop(nestedCarrierGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto nestedCarrierModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + nestedCarrierMutation.body + "}\n", &context);
    if (nestedCarrierModule) {
        ctjs::FuncOp function = *nestedCarrierModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto outer = compare.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto * outerThen = outer.getThenRegion().front().getTerminator();
        auto * outerElse = outer.getElseRegion().front().getTerminator();
        auto inner = outerThen->getOperand(0).getDefiningOp<mlir::scf::IfOp>();
        auto * innerThen = inner.getThenRegion().front().getTerminator();
        auto * innerElse = inner.getElseRegion().front().getTerminator();
        auto conversion = innerThen->getOperand(0).getDefiningOp<ctjs::ConvertOp>();
        auto even = innerElse->getOperand(0).getDefiningOp<ctjs::UnaryOp>();
        auto literal = compare.getRhs().getDefiningOp<ctjs::ConstantOp>();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = conversion.getOperand();
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*nestedCarrierModule))) {
            fail(row{.what = nestedCarrierMutation.what,
                     .body = nestedCarrierMutation.body,
                     .expected = ""},
                 "the nested Boolean carrier stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 10; ++state) {
            innerThen->setOperand(0, state == 1   ? originalInput
                                     : state == 2 ? zero
                                                  : conversion.getResult());
            innerElse->setOperand(0, state == 3 ? even.getOperand() : even.getResult());
            outerElse->setOperand(0, state == 4 ? originalInput : inner.getResult(0));
            outerThen->setOperand(0, state == 5 ? zero : inner.getResult(0));
            conversion->setOperand(0, state == 6 ? function.getBody().front().getArgument(4)
                                                 : originalInput);
            load->setOperand(1, state == 7 ? zero : originalPosition);
            literal.setValueAttr(
                state == 8
                    ? mlir::Attribute(ctjs::NumberAttr::get(&context, 4607182418800017408ULL))
                    : mlir::Attribute(ctjs::BooleanAttr::get(&context, true)));
            nestedCarrierMutation.complete = state == 0 || state == 9;
            nestedCarrierMutation.confinedNested = nestedCarrierMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*nestedCarrierModule, nestedCarrierMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                nestedCarrierMutation.complete) {
                fail(row{.what = nestedCarrierMutation.what,
                         .body = nestedCarrierMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied nested Boolean tags");
            }
        }
    } else {
        fail(row{.what = nestedCarrierMutation.what,
                 .body = nestedCarrierMutation.body,
                 .expected = ""},
             "the nested Boolean carrier mutation fixture did not parse");
    }
    retention_row identityMutation{
        .what = "inner identity forwarding reproves both yields, outer arms and snapshots",
        .body =
            loopValues + allocationLoop(identityGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto identityModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + identityMutation.body + "}\n", &context);
    if (identityModule) {
        ctjs::FuncOp function = *identityModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto outer = second.getCondition()
                         .getDefiningOp<ctjs::TruthyOp>()
                         .getValue()
                         .getDefiningOp<mlir::scf::IfOp>();
        auto * outerElse = outer.getElseRegion().front().getTerminator();
        auto inner = outer.getThenRegion()
                         .front()
                         .getTerminator()
                         ->getOperand(0)
                         .getDefiningOp<mlir::scf::IfOp>();
        auto * innerThen = inner.getThenRegion().front().getTerminator();
        auto * innerElse = inner.getElseRegion().front().getTerminator();
        auto conversion = outerElse->getOperand(0).getDefiningOp<ctjs::ConvertOp>();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = conversion.getOperand();
        const mlir::Value otherInput = function.getBody().front().getArgument(4);
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*identityModule))) {
            fail(row{.what = identityMutation.what, .body = identityMutation.body, .expected = ""},
                 "the inner-identity stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 8; ++state) {
            innerThen->setOperand(0, state == 1 ? otherInput : conversion.getResult());
            innerElse->setOperand(0, state == 2 ? otherInput : conversion.getResult());
            outerElse->setOperand(0, state == 3 ? otherInput : conversion.getResult());
            conversion->setOperand(0, state == 4 ? otherInput : originalInput);
            conversion.setKindAttr(ctjs::ConvertKindAttr::get(
                &context, state == 5 ? ctjs::ConvertKind::ToNumber : ctjs::ConvertKind::ToBoolean));
            load->setOperand(1, state == 6 ? zero : originalPosition);
            identityMutation.complete = state == 0 || state == 7;
            identityMutation.confinedNested = identityMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*identityModule, identityMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                identityMutation.complete) {
                fail(row{.what = identityMutation.what,
                         .body = identityMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied inner identity forwarding");
            }
        }
    } else {
        fail(row{.what = identityMutation.what, .body = identityMutation.body, .expected = ""},
             "the inner-identity mutation fixture did not parse");
    }
    retention_row operandMutation{
        .what = "identity comparison operands reprove both yields, comparisons and guards",
        .body =
            loopValues + allocationLoop(operandGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto operandModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + operandMutation.body + "}\n", &context);
    if (operandModule) {
        ctjs::FuncOp function = *operandModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto carrier = compare.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = carrier.getThenRegion().front().getTerminator();
        auto * elseYield = carrier.getElseRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = thenYield->getOperand(0);
        const mlir::Value otherInput = function.getBody().front().getArgument(4);
        const mlir::Value originalRight = compare.getRhs();
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*operandModule))) {
            fail(row{.what = operandMutation.what, .body = operandMutation.body, .expected = ""},
                 "the identity operand stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 8; ++state) {
            thenYield->setOperand(0, state == 1 || state == 3 ? otherInput : originalInput);
            elseYield->setOperand(0, state == 2 || state == 3 ? otherInput : originalInput);
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 4 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            compare->setOperand(1, state == 5 ? originalPosition : originalRight);
            load->setOperand(1, state == 6 ? zero : originalPosition);
            operandMutation.complete = state == 0 || state == 7;
            operandMutation.confinedNested = operandMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*operandModule, operandMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                operandMutation.complete) {
                fail(
                    row{.what = operandMutation.what, .body = operandMutation.body, .expected = ""},
                    "a stale solver or forged marker supplied identity comparison operands");
            }
        }
    } else {
        fail(row{.what = operandMutation.what, .body = operandMutation.body, .expected = ""},
             "the identity operand mutation fixture did not parse");
    }
    retention_row nestedTruthinessMutation{
        .what = "nested saved truthiness reproves every leaf, guard and current condition",
        .body = loopValues +
                allocationLoop(nestedTruthinessGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto nestedTruthinessModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + nestedTruthinessMutation.body + "}\n", &context);
    if (nestedTruthinessModule) {
        ctjs::FuncOp function = *nestedTruthinessModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto truthy = second.getCondition().getDefiningOp<ctjs::TruthyOp>();
        auto outer = truthy.getValue().getDefiningOp<mlir::scf::IfOp>();
        auto middle = outer.getThenRegion()
                          .front()
                          .getTerminator()
                          ->getOperand(0)
                          .getDefiningOp<mlir::scf::IfOp>();
        auto inner = middle.getThenRegion()
                         .front()
                         .getTerminator()
                         ->getOperand(0)
                         .getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = inner.getThenRegion().front().getTerminator();
        auto * elseYield = inner.getElseRegion().front().getTerminator();
        auto * middleElse = middle.getElseRegion().front().getTerminator();
        auto * outerElse = outer.getElseRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = thenYield->getOperand(0);
        const mlir::Value otherInput = function.getBody().front().getArgument(4);
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*nestedTruthinessModule))) {
            fail(row{.what = nestedTruthinessMutation.what,
                     .body = nestedTruthinessMutation.body,
                     .expected = ""},
                 "the nested truthiness stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 8; ++state) {
            thenYield->setOperand(0, state == 1 ? otherInput : originalInput);
            elseYield->setOperand(0, state == 2 ? otherInput : originalInput);
            middleElse->setOperand(0, state == 3 ? otherInput : originalInput);
            outerElse->setOperand(0, state == 4 ? otherInput : originalInput);
            truthy->setOperand(0, state == 5 ? otherInput : outer.getResult(0));
            load->setOperand(1, state == 6 ? zero : originalPosition);
            nestedTruthinessMutation.complete = state == 0 || state == 7;
            nestedTruthinessMutation.confinedNested =
                nestedTruthinessMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*nestedTruthinessModule, nestedTruthinessMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                nestedTruthinessMutation.complete) {
                fail(row{.what = nestedTruthinessMutation.what,
                         .body = nestedTruthinessMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied nested truthiness identity");
            }
        }
    } else {
        fail(row{.what = nestedTruthinessMutation.what,
                 .body = nestedTruthinessMutation.body,
                 .expected = ""},
             "the nested truthiness mutation fixture did not parse");
    }
    retention_row nestedOperandMutation{
        .what = "nested saved operands reprove every SSA leaf, comparisons and guards",
        .body = loopValues +
                allocationLoop(nestedOperandGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto nestedOperandModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + nestedOperandMutation.body + "}\n", &context);
    if (nestedOperandModule) {
        ctjs::FuncOp function = *nestedOperandModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto carrier = compare.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto inner = carrier.getThenRegion()
                         .front()
                         .getTerminator()
                         ->getOperand(0)
                         .getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = inner.getThenRegion().front().getTerminator();
        auto * elseYield = inner.getElseRegion().front().getTerminator();
        auto * outerElse = carrier.getElseRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = thenYield->getOperand(0);
        const mlir::Value otherInput = function.getBody().front().getArgument(4);
        const mlir::Value originalRight = compare.getRhs();
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*nestedOperandModule))) {
            fail(row{.what = nestedOperandMutation.what,
                     .body = nestedOperandMutation.body,
                     .expected = ""},
                 "the nested saved operand stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 8; ++state) {
            thenYield->setOperand(0, state == 1 ? otherInput : originalInput);
            elseYield->setOperand(0, state == 2 ? otherInput : originalInput);
            outerElse->setOperand(0, state == 3 ? otherInput : originalInput);
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 4 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            compare->setOperand(1, state == 5 ? originalPosition : originalRight);
            load->setOperand(1, state == 6 ? zero : originalPosition);
            nestedOperandMutation.complete = state == 0 || state == 7;
            nestedOperandMutation.confinedNested = nestedOperandMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*nestedOperandModule, nestedOperandMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                nestedOperandMutation.complete) {
                fail(row{.what = nestedOperandMutation.what,
                         .body = nestedOperandMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied nested saved comparison operands");
            }
        }
    } else {
        fail(row{.what = nestedOperandMutation.what,
                 .body = nestedOperandMutation.body,
                 .expected = ""},
             "the nested saved operand mutation fixture did not parse");
    }
    retention_row literalOperandMutation{
        .what = "Boolean literal operands reprove live yields, tags, polarity and guards",
        .body = loopValues +
                allocationLoop(literalOperandGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto literalOperandModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + literalOperandMutation.body + "}\n", &context);
    if (literalOperandModule) {
        ctjs::FuncOp function = *literalOperandModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto carrier = compare.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = carrier.getThenRegion().front().getTerminator();
        auto * elseYield = carrier.getElseRegion().front().getTerminator();
        auto literal = thenYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*literalOperandModule))) {
            fail(row{.what = literalOperandMutation.what,
                     .body = literalOperandMutation.body,
                     .expected = ""},
                 "the Boolean literal operand stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 8; ++state) {
            thenYield->setOperand(0, state == 1 ? zero : literal.getResult());
            elseYield->setOperand(0, state == 2 ? zero : literal.getResult());
            literal.setValueAttr(
                state == 3
                    ? mlir::Attribute(ctjs::NumberAttr::get(&context, 4607182418800017408ULL))
                    : mlir::Attribute(ctjs::BooleanAttr::get(&context, state != 4)));
            load->setOperand(1, state == 5 ? zero : originalPosition);
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 6 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            literalOperandMutation.complete = state == 0 || state == 7;
            literalOperandMutation.confinedNested = literalOperandMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*literalOperandModule, literalOperandMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                literalOperandMutation.complete) {
                fail(row{.what = literalOperandMutation.what,
                         .body = literalOperandMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied Boolean literal operands");
            }
        }
    } else {
        fail(row{.what = literalOperandMutation.what,
                 .body = literalOperandMutation.body,
                 .expected = ""},
             "the Boolean literal operand mutation fixture did not parse");
    }
    retention_row equalOperandMutation{
        .what = "equal primitive leaves reprove live values, tags, operands and guards",
        .body = loopValues +
                allocationLoop(equalOperandGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto equalOperandModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + equalOperandMutation.body + "}\n", &context);
    if (equalOperandModule) {
        ctjs::FuncOp function = *equalOperandModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto carrier = compare.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = carrier.getThenRegion().front().getTerminator();
        auto * elseYield = carrier.getElseRegion().front().getTerminator();
        auto thenLiteral = thenYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto elseLiteral = elseYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value originalInput = compare.getLhs();
        const mlir::Value opaque = function.getBody().front().getArgument(4);
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*equalOperandModule))) {
            fail(row{.what = equalOperandMutation.what,
                     .body = equalOperandMutation.body,
                     .expected = ""},
                 "the equal primitive leaf stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 12; ++state) {
            thenLiteral.setValueAttr(
                state == 4 ? mlir::Attribute(ctjs::BooleanAttr::get(&context, false))
                           : mlir::Attribute(ctjs::NumberAttr::get(
                                 &context, state == 1 || state == 3 ? 4607182418800017408ULL : 0)));
            elseLiteral.setValueAttr(
                state == 5 ? mlir::Attribute(ctjs::StringAttr::get(&context, "0"))
                           : mlir::Attribute(ctjs::NumberAttr::get(
                                 &context, state == 2 || state == 3 ? 4607182418800017408ULL : 0)));
            thenYield->setOperand(0, state == 6 ? opaque : thenLiteral.getResult());
            elseYield->setOperand(0, state == 7 ? opaque : elseLiteral.getResult());
            load->setOperand(1, state == 8 ? zero : originalPosition);
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 9 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            compare->setOperand(0, state == 10 ? opaque : originalInput);
            equalOperandMutation.complete = state == 0 || state == 11;
            equalOperandMutation.confinedNested = equalOperandMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*equalOperandModule, equalOperandMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                equalOperandMutation.complete) {
                fail(row{.what = equalOperandMutation.what,
                         .body = equalOperandMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied equal primitive leaves");
            }
        }
    } else {
        fail(row{.what = equalOperandMutation.what,
                 .body = equalOperandMutation.body,
                 .expected = ""},
             "the equal primitive leaf mutation fixture did not parse");
    }
    retention_row equalLiteralMutation{
        .what = "equal Boolean leaves reprove live values, tags, operands and guards",
        .body = loopValues +
                allocationLoop(equalLiteralGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto equalLiteralModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + equalLiteralMutation.body + "}\n", &context);
    if (equalLiteralModule) {
        ctjs::FuncOp function = *equalLiteralModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto compare = second.getCondition()
                           .getDefiningOp<ctjs::TruthyOp>()
                           .getValue()
                           .getDefiningOp<ctjs::CompareOp>();
        auto carrier = compare.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto * thenYield = carrier.getThenRegion().front().getTerminator();
        auto * elseYield = carrier.getElseRegion().front().getTerminator();
        auto thenLiteral = thenYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto elseLiteral = elseYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value originalInput = compare.getLhs();
        const mlir::Value opaque = function.getBody().front().getArgument(3);
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*equalLiteralModule))) {
            fail(row{.what = equalLiteralMutation.what,
                     .body = equalLiteralMutation.body,
                     .expected = ""},
                 "the equal Boolean leaf stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 12; ++state) {
            thenLiteral.setValueAttr(
                state == 4
                    ? mlir::Attribute(ctjs::NumberAttr::get(&context, 4607182418800017408ULL))
                    : mlir::Attribute(ctjs::BooleanAttr::get(&context, state != 1 && state != 3)));
            elseLiteral.setValueAttr(
                state == 5
                    ? mlir::Attribute(ctjs::NumberAttr::get(&context, 4607182418800017408ULL))
                    : mlir::Attribute(ctjs::BooleanAttr::get(&context, state != 2 && state != 3)));
            thenYield->setOperand(0, state == 6 ? zero : thenLiteral.getResult());
            elseYield->setOperand(0, state == 7 ? opaque : elseLiteral.getResult());
            load->setOperand(1, state == 8 ? zero : originalPosition);
            compare.setKindAttr(ctjs::CompareKindAttr::get(
                &context, state == 9 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
            compare->setOperand(0, state == 10 ? opaque : originalInput);
            equalLiteralMutation.complete = state == 0 || state == 11;
            equalLiteralMutation.confinedNested = equalLiteralMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*equalLiteralModule, equalLiteralMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                equalLiteralMutation.complete) {
                fail(row{.what = equalLiteralMutation.what,
                         .body = equalLiteralMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied equal Boolean leaves");
            }
        }
    } else {
        fail(row{.what = equalLiteralMutation.what,
                 .body = equalLiteralMutation.body,
                 .expected = ""},
             "the equal Boolean leaf mutation fixture did not parse");
    }
    retention_row booleanMutation{
        .what =
            "ToBoolean correspondence reproves live conversion inputs, kinds, yields and guards",
        .body =
            loopValues + allocationLoop(booleanGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto booleanModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + booleanMutation.body + "}\n", &context);
    if (booleanModule) {
        ctjs::FuncOp function = *booleanModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        ctjs::ConvertOp conversion;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        function.walk([&](ctjs::ConvertOp op) { conversion = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto * secondYield = second.getThenRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = conversion.getOperand();
        const mlir::Value originalYield = secondYield->getOperand(0);
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*booleanModule))) {
            fail(row{.what = booleanMutation.what, .body = booleanMutation.body, .expected = ""},
                 "the ToBoolean stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 6; ++state) {
            conversion->setOperand(0, state == 1 ? function.getBody().front().getArgument(4)
                                                 : originalInput);
            conversion.setKindAttr(ctjs::ConvertKindAttr::get(
                &context, state == 2 ? ctjs::ConvertKind::ToNumber : ctjs::ConvertKind::ToBoolean));
            secondYield->setOperand(0, state == 3 ? originalPosition : originalYield);
            load->setOperand(1, state == 4 ? zero : originalPosition);
            booleanMutation.complete = state == 0 || state == 5;
            booleanMutation.confinedNested = booleanMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*booleanModule, booleanMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                booleanMutation.complete) {
                fail(
                    row{.what = booleanMutation.what, .body = booleanMutation.body, .expected = ""},
                    "a stale solver or forged marker supplied ToBoolean correspondence");
            }
        }
    } else {
        fail(row{.what = booleanMutation.what, .body = booleanMutation.body, .expected = ""},
             "the ToBoolean mutation fixture did not parse");
    }
    retention_row forwardedMutation{
        .what = "equal-yield correspondence reproves both arms, result slots, choices and guards",
        .body = loopValues +
                allocationLoop(nestedSiblingStore(
                                   "      %read = ctjs.get_property %guard[%one]\n" +
                                   siblingInput("%read", "%two", "%secondFlag", forwardedTuple)),
                               "%two", "%one", "%zero, %zero, %zero") +
                done,
        .confinedNested = "fresh"};
    auto forwardedModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + forwardedMutation.body + "}\n", &context);
    if (forwardedModule) {
        ctjs::FuncOp function = *forwardedModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto truthy = second.getCondition().getDefiningOp<ctjs::TruthyOp>();
        auto forwarding = truthy.getValue().getDefiningOp<mlir::scf::IfOp>();
        auto * thenForward = forwarding.getThenRegion().front().getTerminator();
        auto * elseForward = forwarding.getElseRegion().front().getTerminator();
        auto * secondYield = second.getThenRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = thenForward->getOperand(1);
        const mlir::Value originalCondition = forwarding.getCondition();
        const mlir::Value originalYield = secondYield->getOperand(0);
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value independent = function.getBody().front().getArgument(4);
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*forwardedModule))) {
            fail(
                row{.what = forwardedMutation.what, .body = forwardedMutation.body, .expected = ""},
                "the equal-yield stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 9; ++state) {
            thenForward->setOperand(1, state == 1 || state == 3 ? independent : originalInput);
            elseForward->setOperand(1, state == 2 || state == 3 ? independent : originalInput);
            truthy->setOperand(0, forwarding.getResult(state == 4 ? 0 : 1));
            secondYield->setOperand(0, state == 5 ? originalPosition : originalYield);
            load->setOperand(1, state == 6 ? zero : originalPosition);
            forwarding->setOperand(0, state == 7 ? first.getCondition() : originalCondition);
            forwardedMutation.complete = state == 0 || state >= 7;
            forwardedMutation.confinedNested = forwardedMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*forwardedModule, forwardedMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                forwardedMutation.complete) {
                fail(row{.what = forwardedMutation.what,
                         .body = forwardedMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied equal-yield correspondence");
            }
        }
    } else {
        fail(row{.what = forwardedMutation.what, .body = forwardedMutation.body, .expected = ""},
             "the equal-yield mutation fixture did not parse");
    }
    retention_row booleanYieldMutation{
        .what = "Boolean yields reprove both chains, polarity, tuple slots, Number arms and guards",
        .body = loopValues +
                allocationLoop(nestedSiblingStore(
                                   "      %read = ctjs.get_property %guard[%one]\n" +
                                   siblingInput("%read", "%two", "%secondFlag", booleanYieldTuple)),
                               "%two", "%one", "%zero, %zero, %zero") +
                done,
        .confinedNested = "fresh"};
    auto booleanYieldModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + booleanYieldMutation.body + "}\n", &context);
    if (booleanYieldModule) {
        ctjs::FuncOp function = *booleanYieldModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto truthy = second.getCondition().getDefiningOp<ctjs::TruthyOp>();
        auto forwarding = truthy.getValue().getDefiningOp<mlir::scf::IfOp>();
        auto * thenForward = forwarding.getThenRegion().front().getTerminator();
        auto conversion = thenForward->getOperand(1).getDefiningOp<ctjs::ConvertOp>();
        auto outer = forwarding.getElseRegion()
                         .front()
                         .getTerminator()
                         ->getOperand(1)
                         .getDefiningOp<ctjs::UnaryOp>();
        auto inner = outer.getOperand().getDefiningOp<ctjs::UnaryOp>();
        auto * secondYield = second.getThenRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalInput = conversion.getOperand();
        const mlir::Value originalCondition = forwarding.getCondition();
        const mlir::Value originalYield = secondYield->getOperand(0);
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value independent = function.getBody().front().getArgument(4);
        const mlir::Value zero =
            store.getObject().getDefiningOp<ctjs::CreateArrayOp>().getElements().front();
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*booleanYieldModule))) {
            fail(row{.what = booleanYieldMutation.what,
                     .body = booleanYieldMutation.body,
                     .expected = ""},
                 "the Boolean-yield stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 12; ++state) {
            conversion->setOperand(0, state == 1 ? independent : originalInput);
            inner->setOperand(0, state == 2 ? independent : originalInput);
            conversion.setKindAttr(ctjs::ConvertKindAttr::get(
                &context, state == 3 ? ctjs::ConvertKind::ToNumber : ctjs::ConvertKind::ToBoolean));
            outer.setKindAttr(ctjs::UnaryKindAttr::get(
                &context, state == 4 ? ctjs::UnaryKind::Void : ctjs::UnaryKind::Not));
            outer->setOperand(0, state == 5 ? originalInput : inner.getResult());
            thenForward->setOperand(1, state == 6 ? originalInput : conversion.getResult());
            truthy->setOperand(0, forwarding.getResult(state == 7 ? 0 : 1));
            secondYield->setOperand(0, state == 8 ? originalPosition : originalYield);
            load->setOperand(1, state == 9 ? zero : originalPosition);
            forwarding->setOperand(0, state == 10 ? first.getCondition() : originalCondition);
            booleanYieldMutation.complete = state == 0 || state == 6 || state >= 10;
            booleanYieldMutation.confinedNested = booleanYieldMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*booleanYieldModule, booleanYieldMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                booleanYieldMutation.complete) {
                fail(row{.what = booleanYieldMutation.what,
                         .body = booleanYieldMutation.body,
                         .expected = ""},
                     "a stale solver or forged marker supplied Boolean-yield correspondence");
            }
        }
    } else {
        fail(row{.what = booleanYieldMutation.what,
                 .body = booleanYieldMutation.body,
                 .expected = ""},
             "the Boolean-yield mutation fixture did not parse");
    }
    for (const std::string & comparisonGap : {representativeGap, siblingComparisonGap}) {
        retention_row representativeMutation{
            .what = "comparison yields reprove both operands, dominating representative and guards",
            .body = loopValues +
                    allocationLoop(comparisonGap, "%two", "%one", "%zero, %zero, %zero") + done,
            .confinedNested = "fresh"};
        auto representativeModule = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + representativeMutation.body + "}\n", &context);
        if (representativeModule) {
            ctjs::FuncOp function = *representativeModule->getOps<ctjs::FuncOp>().begin();
            ctjs::SetPropertyOp store;
            function.walk([&](ctjs::SetPropertyOp op) { store = op; });
            auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
            auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
            auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
            const mlir::Value firstCondition =
                first.getCondition().getDefiningOp<ctjs::TruthyOp>().getValue();
            auto representative = firstCondition.getDefiningOp<ctjs::CompareOp>();
            if (!representative) {
                representative = firstCondition.getDefiningOp<mlir::scf::IfOp>()
                                     .getThenRegion()
                                     .front()
                                     .getTerminator()
                                     ->getOperand(0)
                                     .getDefiningOp<ctjs::CompareOp>();
            }
            auto forwarding = second.getCondition()
                                  .getDefiningOp<ctjs::TruthyOp>()
                                  .getValue()
                                  .getDefiningOp<mlir::scf::IfOp>();
            auto conversion = forwarding.getThenRegion()
                                  .front()
                                  .getTerminator()
                                  ->getOperand(0)
                                  .getDefiningOp<ctjs::ConvertOp>();
            auto thenCompare = conversion.getOperand().getDefiningOp<ctjs::CompareOp>();
            auto outer = forwarding.getElseRegion()
                             .front()
                             .getTerminator()
                             ->getOperand(0)
                             .getDefiningOp<ctjs::UnaryOp>();
            auto inner = outer.getOperand().getDefiningOp<ctjs::UnaryOp>();
            auto elseCompare = inner.getOperand().getDefiningOp<ctjs::CompareOp>();
            auto * secondYield = second.getThenRegion().front().getTerminator();
            auto load = first.getThenRegion()
                            .front()
                            .getTerminator()
                            ->getOperand(0)
                            .getDefiningOp<ctjs::GetPropertyOp>();
            const mlir::Value originalLeft = representative.getLhs();
            const mlir::Value originalRight = representative.getRhs();
            const mlir::Value originalCondition = forwarding.getCondition();
            const mlir::Value originalYield = secondYield->getOperand(0);
            const mlir::Value originalPosition = load.getKey();
            const mlir::Value independent = function.getBody().front().getArgument(4);
            mlir::DataFlowSolver stale;
            stale.load<mlir::dataflow::DeadCodeAnalysis>();
            stale.load<mlir::dataflow::SparseConstantPropagation>();
            stale.load<EscapeAnalysis>();
            if (failed(stale.initializeAndRun(*representativeModule))) {
                fail(row{.what = representativeMutation.what,
                         .body = representativeMutation.body,
                         .expected = ""},
                     "the comparison-yield stale solver did not converge");
            }
            function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
            for (unsigned state = 0; state < 14; ++state) {
                const bool reversed = state == 11;
                thenCompare->setOperand(0, state == 1 ? independent
                                           : reversed ? originalRight
                                                      : originalLeft);
                thenCompare->setOperand(1, reversed ? originalLeft : originalRight);
                elseCompare->setOperand(0, reversed ? originalRight : originalLeft);
                elseCompare->setOperand(1, state == 2 ? originalPosition
                                           : reversed ? originalLeft
                                                      : originalRight);
                thenCompare.setKindAttr(ctjs::CompareKindAttr::get(
                    &context, state == 3 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
                elseCompare.setKindAttr(ctjs::CompareKindAttr::get(
                    &context, state == 4 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
                representative.setKindAttr(ctjs::CompareKindAttr::get(
                    &context, state == 5 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
                representative->setOperand(0, state == 6 ? independent : originalLeft);
                outer->setOperand(0, state == 7 ? elseCompare.getResult() : inner.getResult());
                conversion.setKindAttr(ctjs::ConvertKindAttr::get(
                    &context,
                    state == 8 ? ctjs::ConvertKind::ToNumber : ctjs::ConvertKind::ToBoolean));
                secondYield->setOperand(0, state == 9 ? originalPosition : originalYield);
                load->setOperand(1, state == 10 ? originalRight : originalPosition);
                forwarding->setOperand(0, state == 12 ? first.getCondition() : originalCondition);
                representativeMutation.complete = state == 0 || state >= 11;
                representativeMutation.confinedNested =
                    representativeMutation.complete ? "fresh" : "";
                budgets += checkArrayRetention(*representativeModule, representativeMutation);
                if (computeVerdicts(stale, function).arrayRetentionComplete !=
                    representativeMutation.complete) {
                    fail(
                        row{.what = representativeMutation.what,
                            .body = representativeMutation.body,
                            .expected = ""},
                        "a stale solver or forged marker supplied comparison-yield correspondence");
                }
            }
        } else {
            fail(row{.what = representativeMutation.what,
                     .body = representativeMutation.body,
                     .expected = ""},
                 "the comparison-yield mutation fixture did not parse");
        }
    }
    retention_row literalMutation{
        .what =
            "Boolean literal yields reprove values, slots, polarity, conditions and guard reads",
        .body =
            loopValues + allocationLoop(literalGap, "%two", "%one", "%zero, %zero, %zero") + done,
        .confinedNested = "fresh"};
    auto literalModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + literalMutation.body + "}\n", &context);
    if (literalModule) {
        ctjs::FuncOp function = *literalModule->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp store;
        function.walk([&](ctjs::SetPropertyOp op) { store = op; });
        auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
        auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
        auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
        auto forwarding = second.getCondition()
                              .getDefiningOp<ctjs::TruthyOp>()
                              .getValue()
                              .getDefiningOp<mlir::scf::IfOp>();
        auto condition = forwarding.getCondition().getDefiningOp<ctjs::TruthyOp>();
        auto * thenYield = forwarding.getThenRegion().front().getTerminator();
        auto * elseYield = forwarding.getElseRegion().front().getTerminator();
        auto yes = thenYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto no = elseYield->getOperand(0).getDefiningOp<ctjs::ConstantOp>();
        auto * firstYield = second.getThenRegion().front().getTerminator();
        auto * secondYield = second.getElseRegion().front().getTerminator();
        auto load = first.getThenRegion()
                        .front()
                        .getTerminator()
                        ->getOperand(0)
                        .getDefiningOp<ctjs::GetPropertyOp>();
        const mlir::Value originalCondition = condition.getValue();
        const mlir::Value originalFirst = firstYield->getOperand(0);
        const mlir::Value originalSecond = secondYield->getOperand(0);
        const mlir::Value originalPosition = load.getKey();
        const mlir::Value zero =
            load.getObject().getDefiningOp<ctjs::CreateArrayOp>()->getOperand(0);
        const mlir::Value independent = function.getBody().front().getArgument(4);
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*literalModule))) {
            fail(row{.what = literalMutation.what, .body = literalMutation.body, .expected = ""},
                 "the Boolean-literal stale solver did not converge");
        }
        function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
        for (unsigned state = 0; state < 10; ++state) {
            const bool reversed = state == 8;
            yes.setValueAttr(ctjs::BooleanAttr::get(&context, state != 1 && !reversed));
            no.setValueAttr(ctjs::BooleanAttr::get(&context, state == 2 || reversed));
            thenYield->setOperand(0, state == 3 ? zero : yes.getResult());
            elseYield->setOperand(0, state == 4 ? zero : no.getResult());
            condition->setOperand(0, state == 5 ? independent : originalCondition);
            firstYield->setOperand(0, state == 6 ? originalPosition
                                      : reversed ? originalSecond
                                                 : originalFirst);
            secondYield->setOperand(0, reversed ? originalFirst : originalSecond);
            load->setOperand(1, state == 7 ? zero : originalPosition);
            literalMutation.complete = state == 0 || state >= 8;
            literalMutation.confinedNested = literalMutation.complete ? "fresh" : "";
            budgets += checkArrayRetention(*literalModule, literalMutation);
            if (computeVerdicts(stale, function).arrayRetentionComplete !=
                literalMutation.complete) {
                fail(
                    row{.what = literalMutation.what, .body = literalMutation.body, .expected = ""},
                    "a stale solver or forged marker supplied Boolean-literal correspondence");
            }
        }
    } else {
        fail(row{.what = literalMutation.what, .body = literalMutation.body, .expected = ""},
             "the Boolean-literal mutation fixture did not parse");
    }
    for (const std::string & comparisons :
         {comparedGap, crossBlockGap, numberComparisonGap, stringComparisonGap}) {
        retention_row comparisonMutation{
            .what =
                "strict comparison correspondence reproves both operations, operands and guards",
            .body = loopValues +
                    allocationLoop(comparisons, "%two", "%one", "%zero, %zero, %zero") + done,
            .confinedNested = "fresh"};
        auto comparisonModule = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + comparisonMutation.body + "}\n", &context);
        if (comparisonModule) {
            ctjs::FuncOp function = *comparisonModule->getOps<ctjs::FuncOp>().begin();
            ctjs::SetPropertyOp store;
            function.walk([&](ctjs::SetPropertyOp op) { store = op; });
            auto key = store.getKey().getDefiningOp<ctjs::BinaryOp>();
            auto first = key.getLhs().getDefiningOp<mlir::scf::IfOp>();
            auto second = key.getRhs().getDefiningOp<mlir::scf::IfOp>();
            auto firstCompare = first.getCondition()
                                    .getDefiningOp<ctjs::TruthyOp>()
                                    .getValue()
                                    .getDefiningOp<ctjs::CompareOp>();
            auto secondCompare = second.getCondition()
                                     .getDefiningOp<ctjs::TruthyOp>()
                                     .getValue()
                                     .getDefiningOp<ctjs::CompareOp>();
            auto * firstYield = first.getThenRegion().front().getTerminator();
            auto * secondYield = second.getThenRegion().front().getTerminator();
            auto load = firstYield->getOperand(0).getDefiningOp<ctjs::GetPropertyOp>();
            const mlir::Value originalLeft = firstCompare.getLhs();
            const mlir::Value originalRight = firstCompare.getRhs();
            const mlir::Value comparedRight = secondCompare.getRhs();
            const mlir::Value originalYield = secondYield->getOperand(0);
            const mlir::Value originalPosition = load->getOperand(1);
            const mlir::Value independent = function.getBody().front().getArgument(4);
            mlir::DataFlowSolver stale;
            stale.load<mlir::dataflow::DeadCodeAnalysis>();
            stale.load<mlir::dataflow::SparseConstantPropagation>();
            stale.load<EscapeAnalysis>();
            if (failed(stale.initializeAndRun(*comparisonModule))) {
                fail(row{.what = comparisonMutation.what,
                         .body = comparisonMutation.body,
                         .expected = ""},
                     "the strict comparison stale solver did not converge");
            }
            function->setAttr("ctnative.array_retention_complete", mlir::UnitAttr::get(&context));
            for (unsigned state = 0; state < 20; ++state) {
                const bool reversed = state >= 10;
                const unsigned mutation = state % 10;
                secondCompare->setOperand(0, mutation == 1 ? independent
                                             : reversed    ? comparedRight
                                                           : originalLeft);
                secondCompare->setOperand(1, mutation == 2 ? originalPosition
                                             : reversed    ? originalLeft
                                                           : comparedRight);
                secondCompare.setKindAttr(ctjs::CompareKindAttr::get(
                    &context, mutation == 3 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
                firstCompare.setKindAttr(ctjs::CompareKindAttr::get(
                    &context, mutation == 4 ? ctjs::CompareKind::Eq : ctjs::CompareKind::StrictEq));
                firstCompare->setOperand(0, mutation == 5 ? independent : originalLeft);
                firstCompare->setOperand(1, mutation == 6 ? originalPosition : originalRight);
                secondYield->setOperand(0, mutation == 7 ? originalPosition : originalYield);
                load->setOperand(1, mutation == 8 ? originalRight : originalPosition);
                comparisonMutation.complete = mutation == 0 || mutation == 9;
                comparisonMutation.confinedNested = comparisonMutation.complete ? "fresh" : "";
                budgets += checkArrayRetention(*comparisonModule, comparisonMutation);
                if (computeVerdicts(stale, function).arrayRetentionComplete !=
                    comparisonMutation.complete) {
                    fail(row{.what = comparisonMutation.what,
                             .body = comparisonMutation.body,
                             .expected = ""},
                         "a stale solver or forged marker supplied strict comparison "
                         "correspondence");
                }
            }
            if (comparisons == numberComparisonGap) {
                auto literal = comparedRight.getDefiningOp<ctjs::ConstantOp>();
                const auto original = literal.getValue();
                for (mlir::Attribute changed :
                     {mlir::Attribute(ctjs::NumberAttr::get(&context, 1)),
                      mlir::Attribute(ctjs::StringAttr::get(&context, "0")), original}) {
                    literal.setValueAttr(changed);
                    comparisonMutation.complete = changed == original;
                    comparisonMutation.confinedNested = comparisonMutation.complete ? "fresh" : "";
                    budgets += checkArrayRetention(*comparisonModule, comparisonMutation);
                    if (computeVerdicts(stale, function).arrayRetentionComplete !=
                        comparisonMutation.complete) {
                        fail(row{.what = comparisonMutation.what,
                                 .body = comparisonMutation.body,
                                 .expected = ""},
                             "stale literal bits or tags supplied comparison correspondence");
                    }
                }
            }
            if (comparisons == stringComparisonGap) {
                auto firstLiteral = originalRight.getDefiningOp<ctjs::ConstantOp>();
                auto secondLiteral = comparedRight.getDefiningOp<ctjs::ConstantOp>();
                const auto original = firstLiteral.getValue();
                for (mlir::Attribute changed :
                     {mlir::Attribute(ctjs::StringAttr::get(&context, "1")),
                      mlir::Attribute(ctjs::BooleanAttr::get(&context, false)),
                      mlir::Attribute(ctjs::NullAttr::get(&context)),
                      mlir::Attribute(ctjs::UndefinedAttr::get(&context)),
                      mlir::Attribute(ctjs::BigIntAttr::get(&context, "1"))}) {
                    for (bool same : {true, false}) {
                        firstLiteral.setValueAttr(changed);
                        secondLiteral.setValueAttr(same ? changed : original);
                        comparisonMutation.complete = same;
                        comparisonMutation.confinedNested = same ? "fresh" : "";
                        budgets += checkArrayRetention(*comparisonModule, comparisonMutation);
                        if (computeVerdicts(stale, function).arrayRetentionComplete != same) {
                            fail(row{.what = comparisonMutation.what,
                                     .body = comparisonMutation.body,
                                     .expected = ""},
                                 "stale primitive attributes supplied comparison correspondence");
                        }
                    }
                }
            }
        } else {
            fail(row{.what = comparisonMutation.what,
                     .body = comparisonMutation.body,
                     .expected = ""},
                 "the strict comparison mutation fixture did not parse");
        }
    }
    std::printf(
        "array retention: %zu rows, three hundred twenty-six live states, %zu budget cutoffs\n",
        rows.size(), budgets);
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
