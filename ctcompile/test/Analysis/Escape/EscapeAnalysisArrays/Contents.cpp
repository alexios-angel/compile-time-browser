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
    const auto siblingInput =
        [&](const std::string & left = "%zero", const std::string & right = "%one",
            const std::string & condition = "%flag", const std::string & between = "") {
            auto second = selectedInput(left, right, "", "", "%secondKey");
            second.replace(second.find("%flag"), 5, condition);
            return selectedInput(left, right, "", "", "%firstKey") + between + second +
                   "    %key = ctjs.binary sub %firstKey, %secondKey\n";
        };
    const std::string siblingKeys = selectedStore(siblingInput());
    const std::string siblingGap = selectedStore("    %read = ctjs.get_property %guard[%one]\n" +
                                                 siblingInput("%read", "%two"));
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
        {.what = "recomputing truthiness does not reuse another predicate choice",
         .body = loopValues +
                 allocationLoop(selectedStore(siblingInput("%zero", "%one", "%secondFlag",
                                                           "    %secondFlag = ctjs.truthy %p\n"))) +
                 done,
         .complete = false},
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
                                                  function.getBody().front().getArgument(3));
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
    std::printf("array retention: %zu rows, forty-two live states, %zu budget cutoffs\n",
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
