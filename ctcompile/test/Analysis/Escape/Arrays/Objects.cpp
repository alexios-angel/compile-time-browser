#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkObjectDeletions(mlir::MLIRContext & context) {
    // Each preserved source starts with an ordinary-object assignment. Until
    // an own-data proof excludes inherited setters, neither deletion nor a
    // later mutation can discharge its child or publish the old graph records.
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\"> {storage_test_id = \"key\"}\n"
        "  %other = ctjs.constant #ctjs.string<\"other\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
        "  ctjs.set_property %o[%key], %x\n";
    const std::string read = "  %saved = ctjs.get_property %o[%key]\n";
    const std::string computed = "  ctjs.delete_property %o[%key]\n";
    const std::string named = "  ctjs.delete_named \"child\" from %o\n";
    const std::string returned = "  ctjs.return %o\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string split = "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^left, ^right\n^left:\n";
    struct deletion_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclic = true;
    };
    const std::vector<deletion_row> rows = {
        {.contents = {.what = "computed deletion removes a child from the returned object",
                      .body = values + computed + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "named deletion preserves a saved child returned directly",
                      .body = values + read + named + "  ctjs.return %saved\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "x -> {x}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x"}},
        {.contents = {.what = "a returned array keeps a saved own read across deletion",
                      .body = values + read + computed +
                              "  %a = ctjs.create_array [%saved] {storage_test_id = \"a\"}\n"
                              "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[x]",
                      .exit = "a -> {a,x}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"}},
        {.contents = {.what = "repeated named and computed deletion records exact absence",
                      .body = values + named + computed + named + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x; "
                                           "ctjs.delete_property:o[child]=absent; "
                                           "ctjs.delete_named:o[child]=absent"},
         .discharged = "x"},
        {.contents = {.what = "absent own deletion does not consult a builtin or prototype",
                      .body = values +
                              "  ctjs.delete_named \"constructor\" from %o\n"
                              "  ctjs.delete_property %o[%other]\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{child:x}",
                      .propertyDeletions = "ctjs.delete_named:o[constructor]=absent; "
                                           "ctjs.delete_property:o[other]=absent"}},
        {.contents = {.what = "deletion and reinsertion preserve unrelated own fields",
                      .body = values + "  ctjs.set_property %o[%other], %y\n" + computed +
                              "  ctjs.set_property %o[%key], %y\n" + read + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o,y}",
                      .objects = "x:{}; y:{}; o:{other:y,child:y}",
                      .propertyReads = "o[child]=y",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x; "
                                        "ctjs.set_property[2]:o[other]=y; "
                                        "ctjs.set_property[2]:o[child]=y",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a saved object alias still receives writes after its field deletion",
                      .body = values + read + named +
                              "  ctjs.set_property %saved[%other], %y\n"
                              "  ctjs.return %saved\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "x -> {x,y}",
                      .objects = "x:{other:y}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x"}},
        {.contents = {.what = "deletion through an array-loaded object alias updates one object",
                      .body = values + "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                                       "  %alias = ctjs.get_property %a[%zero]\n"
                                       "  ctjs.delete_named \"child\" from %alias\n"
                                       "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o]",
                      .reads = "a[0]=o",
                      .exit = "a -> {a,o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_named:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "deletion through an object-loaded alias updates its own snapshot",
                      .body = values + "  ctjs.set_property %y[%other], %o\n"
                                       "  %alias = ctjs.get_property %y[%other]\n"
                                       "  ctjs.delete_property %alias[%key]\n"
                                       "  ctjs.return %y\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "y -> {o,y}",
                      .objects = "x:{}; y:{other:o}; o:{}",
                      .propertyReads = "y[other]=o",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a saved String key remains usable after its own field is deleted",
                      .body = values +
                              "  ctjs.set_property %o[%other], %key\n"
                              "  %savedKey = ctjs.get_property %o[%other]\n"
                              "  ctjs.delete_named \"other\" from %o\n"
                              "  ctjs.delete_property %o[%savedKey]\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[other]=key",
                      .propertyDeletions = "ctjs.delete_named:o[other]=key; "
                                           "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a missing read after deletion refuses the whole contents proof",
                      .body = values + computed + read + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "named deletion cannot authorize a later builtin lookup",
                      .body = values +
                              "  %constructor = ctjs.constant #ctjs.string<\"constructor\">\n"
                              "  ctjs.set_property %o[%constructor], %y\n"
                              "  ctjs.delete_named \"constructor\" from %o\n"
                              "  %read = ctjs.get_property %o[%constructor]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an undeleted conditional edge still retains the original child",
                      .body = values + split + computed + returned + "^right:\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}; o -> {o,x}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{child:x}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"}},
        {.contents = {.what = "deleting on both conditional edges discharges the child",
                      .body = values + split + computed + "  cf.br ^join\n^right:\n" + named +
                              "  cf.br ^join\n^join:\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}; o -> {o}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_named:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "a joined deletion target cannot erase both alternative objects",
                      .body = values +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %a = ctjs.create_array [%o, %b] {storage_test_id = \"a\"}\n"
                              "  %condition = ctjs.truthy %p\n"
                              "  cf.cond_br %condition, ^join(%o : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  ctjs.delete_property %selected[%key]\n"
                              "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o,b] | a:[o,b]",
                      .exit = "a -> {a,b,o,y}; a -> {a,b,o,x}",
                      .objects = "x:{}; y:{}; o:{}; b:{child:y} | "
                                 "x:{}; y:{}; o:{child:x}; b:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_property:b[child]=y"}},
        {.contents = {.what = "all switch edges preserve their exact deletion and exit records",
                      .body = values +
                              "  %flag = ctjs.truthy %p\n"
                              "  cf.switch %flag : i1, [default: ^default, 0: ^left, 1: ^right]\n"
                              "^default:\n" +
                              computed + returned + "^left:\n" + named + returned + "^right:\n" +
                              computed + named + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}; o -> {o}; o -> {o}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{} | x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_named:o[child]=x; "
                                           "ctjs.delete_property:o[child]=x; "
                                           "ctjs.delete_named:o[child]=absent"},
         .discharged = "x"},
        {.contents = {.what = "a second-path missing read discards earlier deletion records",
                      .body = values + split + computed + returned + "^right:\n" + named + read +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a second-path unknown delete key discards an earlier complete exit",
                      .body = values + split + computed + returned +
                              "^right:\n  ctjs.delete_property %o[%p]\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late publication after deletion preserves every original verdict",
                      .body = values + read + computed + "  ctjs.store_global \"held\", %saved\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an accessor cannot be hidden by deleting its field",
                      .body = values + "  ctjs.define_accessor \"child\" on %o get %p set %q\n" +
                              named + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "prototype changes after deletion still refuse complete contents",
                      .body = values + named + "  ctjs.set_proto %y on %o\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a deleted self edge remains in the all-write cycle graph",
                      .body = values +
                              "  ctjs.set_property %o[%other], %o\n"
                              "  ctjs.delete_named \"other\" from %o\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; o:{child:x}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x; "
                                        "ctjs.set_property[2]:o[other]=o",
                      .propertyDeletions = "ctjs.delete_named:o[other]=o"},
         .acyclic = false},
        {.contents = {.what = "deleting a mixed array-object cycle does not select its owner",
                      .body = values +
                              "  %a = ctjs.create_array [%o] {storage_test_id = \"a\"}\n"
                              "  ctjs.set_property %o[%key], %a\n" +
                              computed + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o]",
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyDeletions = "ctjs.delete_property:o[child]=a"},
         .acyclic = false},
        {.contents = {.what = "imported roots keep saved reads until the matching frame exit",
                      .body = "  %frame = ctjs.frame_enter 4\n" + values + read + computed +
                              "  ctjs.root %saved in %frame\n"
                              "  ctjs.frame_exit %frame\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "o -> {o}",
                      .objects = "x:{}; y:{}; o:{}",
                      .propertyReads = "o[child]=x",
                      .propertyDeletions = "ctjs.delete_property:o[child]=x"},
         .discharged = "x"},
        {.contents = {.what = "an external deletion target cannot borrow local own properties",
                      .body = values + "  ctjs.delete_property %p[%key]\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "computed array deletion cannot claim JavaScript hole semantics",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.delete_property %a[%zero]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "named array deletion remains outside own-object contents",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.delete_named \"0\" from %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "String array deletion cannot borrow ordinary object erasure",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  %index = ctjs.constant #ctjs.string<\"0\">\n"
                              "  ctjs.delete_property %a[%index]\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "coercing numeric deletion keys remain outside the object proof",
                      .body = values + "  ctjs.delete_property %o[%zero]\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "coercing Boolean deletion keys remain outside the object proof",
                      .body = values +
                              "  %boolean = ctjs.constant #ctjs.boolean<true>\n"
                              "  ctjs.delete_property %o[%boolean]\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const deletion_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = "",
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclic});
    };
    const auto run = [&](const deletion_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the object deletion fixture did not parse");
            return;
        }
        check(*module, expected);
    };
    for (const deletion_row & expected : rows) { run(expected); }

    const std::vector<std::pair<std::string, bool>> keys = {
        {"\"\"", true},
        {"\"0\"", true},
        {"\"00\"", true},
        {"\"length\"", true},
        {"\"constructor\"", true},
        {"\"x\\00y\"", true},
        {"\"" + std::string(256, 'k') + "\"", true},
        {"\"" + std::string(257, 'k') + "\"", false},
        {"\"__proto__\"", false},
    };
    for (const auto & [key, supported] : keys) {
        for (bool isNamed : {false, true}) {
            // Keep both historical deletion-key shapes. The shared initial
            // object write refuses before either form of deletion is reached.
            const std::string prefix = values + "  %fixed = ctjs.constant #ctjs.string<" + key +
                                       ">\n" +
                                       (supported ? "  ctjs.set_property %o[%fixed], %y\n" : "");
            const std::string deletion = isNamed ? "  ctjs.delete_named " + key + " from %o\n"
                                                 : "  ctjs.delete_property %o[%fixed]\n";
            run({.contents = {.what = "both deletion forms validate exact bounded String keys",
                              .body = prefix + deletion + returned,
                              .failure = ArrayContentsFailure::UnsupportedOperation,
                              .exit = supported ? "o -> {o,x}" : ""},
                 .discharged = supported ? "y" : ""});
        }
    }

    deletion_row mutation = rows[0];
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::DeletePropertyOp deletion;
        mlir::Value child;
        mlir::Value other;
        module->walk([&](ctjs::DeletePropertyOp op) { deletion = op; });
        module->walk([&](ctjs::CreateObjectOp op) {
            if (contentsLabel(op) == "x") { child = op.getResult(); }
        });
        module->walk([&](ctjs::ConstantOp op) {
            auto string = llvm::dyn_cast<ctjs::StringAttr>(op.getValue());
            if (string && string.getValue() == "other") { other = op.getResult(); }
        });
        const mlir::Value base = deletion.getObject();
        const mlir::Value key = deletion.getKey();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::OpBuilder builder(function.getBody().front().getTerminator());
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        deletion->setOperand(1, other);
        check(*module, mutation);
        deletion->setOperand(1, parameter);
        check(*module, mutation);
        deletion->setOperand(1, key);
        deletion->setOperand(0, parameter);
        check(*module, mutation);
        deletion->setOperand(0, child);
        check(*module, mutation);
        deletion->setOperand(0, base);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        check(*module, mutation);
        publication.erase();
        auto missing = ctjs::GetPropertyOp::create(builder, function.getLoc(),
                                                   ctjs::ValueType::get(&context), base, key);
        check(*module, mutation);
        missing.erase();
        mutation = rows[0];
        check(*module, mutation);
        checkArrayRetention(*module, {.what = "deletion refinement still requires alias lattices",
                                      .body = mutation.contents.body,
                                      .complete = false,
                                      .withAnalysis = false});
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live computed deletion fixture did not parse");
    }

    deletion_row namedMutation{
        .contents = {.what = "live named deletion changes invalidate forged second-path proofs",
                     .body = values + split + computed + returned + "^right:\n" + named + returned,
                     .failure = ArrayContentsFailure::UnsupportedOperation,
                     .exit = "o -> {o}; o -> {o}",
                     .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{}",
                     .propertyDeletions = "ctjs.delete_property:o[child]=x; "
                                          "ctjs.delete_named:o[child]=x"},
        .discharged = "x"};
    auto namedModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + namedMutation.contents.body + "}\n", &context);
    if (namedModule) {
        ctjs::FuncOp function = *namedModule->getOps<ctjs::FuncOp>().begin();
        ctjs::DeleteNamedOp deletion;
        namedModule->walk([&](ctjs::DeleteNamedOp op) { deletion = op; });
        const mlir::Value base = deletion.getObject();
        const mlir::StringAttr name = deletion.getNameAttr();
        const mlir::Value parameter = function.getBody().front().getArgument(3);
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*namedModule, namedMutation);
        deletion->setAttr("name", builder.getStringAttr("other"));
        check(*namedModule, namedMutation);
        deletion->setAttr("name", builder.getStringAttr("__proto__"));
        check(*namedModule, namedMutation);
        deletion->setAttr("name", builder.getStringAttr(std::string(257, 'k')));
        check(*namedModule, namedMutation);
        deletion->setAttr("name", name);
        deletion->setOperand(0, parameter);
        check(*namedModule, namedMutation);
        deletion->setOperand(0, base);
        check(*namedModule, namedMutation);
    } else {
        fail(row{.what = namedMutation.contents.what,
                 .body = namedMutation.contents.body,
                 .expected = ""},
             "the live named deletion fixture did not parse");
    }
    std::printf("object deletion: %zu rows, %zu key controls, fourteen live states, "
                "one missing-lattice control, "
                "%zu retention budget cutoffs\n",
                rows.size(), keys.size() * 2, budgets);
}

void checkObjectCopies(mlir::MLIRContext & context) {
    // The source initializer may call an inherited setter. Copy and mutation
    // cases keep their original IR but stop before any own fields are proved.
    struct copy_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclic = true;
    };
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %other = ctjs.constant #ctjs.string<\"other\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %s = ctjs.create_object {storage_test_id = \"s\"}\n"
        "  %t = ctjs.create_object {storage_test_id = \"t\"}\n"
        "  ctjs.set_property %s[%key], %x\n";
    const std::string copy = "  ctjs.copy_props %s into %t\n";
    const std::string returned = "  ctjs.return %t\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::string split = "  %flag = ctjs.truthy %p\n"
                              "  cf.cond_br %flag, ^left, ^right\n^left:\n";
    const std::vector<copy_row> rows = {
        {.contents = {.what = "an own-data copy retains the child without retaining its source",
                      .body = values + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyWrites = "ctjs.set_property[2]:s[child]=x",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "a private copied child can be discharged",
                      .body = values + copy + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "an empty source copies nothing and preserves target fields",
                      .body = values +
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.set_property %t[%other], %y\n" +
                              copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,y}",
                      .objects = "x:{}; y:{}; s:{}; t:{other:y}",
                      .propertyCopies = ""},
         .discharged = "x"},
        {.contents = {.what = "copy overwrites equal keys and preserves unrelated target fields",
                      .body = values +
                              "  ctjs.set_property %t[%key], %y\n"
                              "  ctjs.set_property %t[%other], %zero\n" +
                              copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x,other:zero}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "y"},
        {.contents = {.what = "a target read saved before copy keeps the overwritten child",
                      .body = values +
                              "  ctjs.set_property %t[%key], %y\n"
                              "  %saved = ctjs.get_property %t[%key]\n" +
                              copy + "  ctjs.return %saved\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "y -> {y}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyReads = "t[child]=y",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "a copied read saved before both deletions keeps its exact origin",
                      .body = values + copy +
                              "  %saved = ctjs.get_property %t[%key]\n"
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.delete_named \"child\" from %t\n"
                              "  ctjs.return %saved\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "x -> {x}",
                      .objects = "x:{}; y:{}; s:{}; t:{}",
                      .propertyReads = "t[child]=x",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "source deletion does not erase a copied target property",
                      .body = values + copy + "  ctjs.delete_named \"child\" from %s\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "target deletion does not make the source container escape",
                      .body = values + copy + "  ctjs.delete_property %t[%key]\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "source replacement does not change an earlier copied origin",
                      .body = values + copy + "  ctjs.set_property %s[%key], %y\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{child:y}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "y"},
        {.contents = {.what = "a later copy reads the current source field",
                      .body = values + copy + "  ctjs.set_property %s[%key], %y\n" + copy +
                              "  ctjs.delete_named \"child\" from %s\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,y}",
                      .objects = "x:{}; y:{}; s:{}; t:{child:y}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:s[child] -> t=y"},
         .discharged = "x"},
        {.contents = {.what = "copy chains retain one child after earlier containers are emptied",
                      .body = values + copy +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.copy_props %t into %b\n"
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.delete_named \"child\" from %t\n"
                              "  ctjs.return %b\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "b -> {b,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{}; b:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:t[child] -> b=x"}},
        {.contents = {.what = "self-copy snapshots own data and does not invent a source edge",
                      .body = values + "  ctjs.set_property %s[%other], %y\n"
                                       "  ctjs.copy_props %s into %s\n"
                                       "  ctjs.return %s\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "s -> {s,x,y}",
                      .objects = "x:{}; y:{}; s:{child:x,other:y}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> s=x; "
                                        "ctjs.copy_props:s[other] -> s=y"}},
        {.contents = {.what = "array-loaded source and target aliases preserve exact identities",
                      .body = values +
                              "  %a = ctjs.create_array [%s, %t] {storage_test_id = \"a\"}\n"
                              "  %source = ctjs.get_property %a[%zero]\n"
                              "  %target = ctjs.get_property %a[%one]\n"
                              "  ctjs.copy_props %source into %target\n"
                              "  ctjs.delete_named \"child\" from %s\n"
                              "  ctjs.return %target\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[s,t]",
                      .reads = "a[0]=s; a[1]=t",
                      .exit = "t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "s"},
        {.contents = {.what = "object-loaded copy aliases preserve mixed-container descendants",
                      .body = values + "  ctjs.set_property %y[%key], %s\n"
                                       "  ctjs.set_property %y[%other], %t\n"
                                       "  %source = ctjs.get_property %y[%key]\n"
                                       "  %target = ctjs.get_property %y[%other]\n"
                                       "  ctjs.copy_props %source into %target\n"
                                       "  ctjs.return %y\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "y -> {s,t,x,y}",
                      .objects = "x:{}; y:{child:s,other:t}; s:{child:x}; t:{child:x}",
                      .propertyReads = "y[child]=s; y[other]=t",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "a copied String key keeps its origin after source replacement",
                      .body = values + "  ctjs.set_property %s[%other], %key\n" + copy +
                              "  ctjs.set_property %s[%other], %zero\n"
                              "  %copiedKey = ctjs.get_property %t[%other]\n"
                              "  ctjs.delete_property %t[%copiedKey]\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t}",
                      .objects = "x:{}; y:{}; s:{child:x,other:zero}; t:{other:ctjs.constant}",
                      .propertyReads = "t[other]=ctjs.constant",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:s[other] -> t=ctjs.constant"},
         .discharged = "x"},
        {.contents = {.what = "a copied String array index still refuses dense element semantics",
                      .body = values +
                              "  %index = ctjs.constant #ctjs.string<\"0\">\n"
                              "  ctjs.set_property %s[%other], %index\n" +
                              copy +
                              "  %copiedKey = ctjs.get_property %t[%other]\n"
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.set_property %a[%copiedKey], %y\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "conditional copies keep separate selected source contents",
                      .body = values +
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %flag = ctjs.truthy %p\n"
                              "  cf.cond_br %flag, ^join(%s : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%source: !ctjs.value):\n"
                              "  ctjs.copy_props %source into %t\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,x}; t -> {t,y}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}; b:{child:y} | "
                                 "x:{}; y:{}; s:{child:x}; t:{child:y}; b:{child:y}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:b[child] -> t=y"}},
        {.contents = {.what = "a conditional copy target cannot overwrite both alternatives",
                      .body = values + "  ctjs.set_property %t[%key], %y\n"
                                       "  %a = ctjs.create_array [%s, %t] "
                                       "{storage_test_id = \"a\"}\n"
                                       "  %flag = ctjs.truthy %p\n"
                                       "  cf.cond_br %flag, ^join(%s : !ctjs.value), "
                                       "^join(%t : !ctjs.value)\n"
                                       "^join(%target: !ctjs.value):\n"
                                       "  ctjs.copy_props %s into %target\n"
                                       "  ctjs.return %a\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[s,t] | a:[s,t]",
                      .exit = "a -> {a,s,t,x,y}; a -> {a,s,t,x}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:y} | "
                                 "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> s=x; "
                                        "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "all switch source alternatives contribute copied retention",
                      .body = values +
                              "  ctjs.set_property %y[%key], %zero\n"
                              "  %flag = ctjs.truthy %p\n"
                              "  cf.switch %flag : i1, [default: ^join(%s : !ctjs.value), "
                              "0: ^join(%y : !ctjs.value), 1: ^join(%x : !ctjs.value)]\n"
                              "^join(%source: !ctjs.value):\n"
                              "  ctjs.copy_props %source into %t\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t,x}; t -> {t}; t -> {t}",
                      .objects = "x:{}; y:{child:zero}; s:{child:x}; t:{child:x} | "
                                 "x:{}; y:{child:zero}; s:{child:x}; t:{child:zero} | "
                                 "x:{}; y:{child:zero}; s:{child:x}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                        "ctjs.copy_props:y[child] -> t=zero"}},
        {.contents = {.what = "deletion on one source path cannot erase another path's child",
                      .body = values + split + "  ctjs.delete_named \"child\" from %s\n" + copy +
                              returned + "^right:\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t}; t -> {t,x}",
                      .objects = "x:{}; y:{}; s:{}; t:{} | x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"}},
        {.contents = {.what = "deleting copied fields on every path can discharge their child",
                      .body = values + copy + split + "  ctjs.delete_property %t[%key]\n" +
                              returned + "^right:\n  ctjs.delete_named \"child\" from %t\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "t -> {t}; t -> {t}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{} | x:{}; y:{}; s:{child:x}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "a late unsupported copy path discards all earlier copied records",
                      .body = values + split + copy + returned +
                              "^right:\n  ctjs.copy_props %p into %t\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a literal predicate cannot hide an unsupported source copy",
                      .body = values +
                              "  %false = ctjs.constant #ctjs.boolean<false>\n"
                              "  %flag = ctjs.truthy %false\n"
                              "  cf.cond_br %flag, ^left, ^right\n^left:\n"
                              "  ctjs.copy_props %p into %t\n" +
                              returned + "^right:\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an empty copy cannot authorize an absent inherited read",
                      .body = values + "  ctjs.delete_named \"child\" from %s\n" + copy +
                              "  %read = ctjs.get_property %t[%key]\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "copied self edges remain in the historical cycle graph",
                      .body = values + "  ctjs.set_property %s[%key], %t\n" + copy +
                              "  ctjs.delete_named \"child\" from %t\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:t}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=t"},
         .acyclic = false},
        {.contents = {.what = "a transient copied array edge cannot discharge a mixed cycle",
                      .body = values +
                              "  %a = ctjs.create_array [%t] {storage_test_id = \"a\"}\n"
                              "  ctjs.set_property %s[%key], %a\n" +
                              copy + "  ctjs.delete_named \"child\" from %t\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[t]",
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:a}; t:{}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=a"},
         .acyclic = false},
        {.contents = {.what = "imported roots release copied contents at the matching frame exit",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values + copy +
                              "  ctjs.root %t in %frame\n  ctjs.frame_exit %frame\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .objects = "x:{}; y:{}; s:{child:x}; t:{child:x}",
                      .propertyCopies = "ctjs.copy_props:s[child] -> t=x"},
         .discharged = "x"},
        {.contents = {.what = "copy after an imported frame exit cannot borrow the old window",
                      .body = "  %frame = ctjs.frame_enter 8\n" + values +
                              "  ctjs.frame_exit %frame\n" + copy + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an external source never supplies enumerable own-data evidence",
                      .body = values + "  ctjs.copy_props %p into %t\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an external target never borrows the fresh source's ownership",
                      .body = values + "  ctjs.copy_props %s into %p\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an array source remains outside exact own-object copy contents",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.copy_props %a into %t\n" +
                              returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "an array target cannot be mistaken for an own-data object",
                      .body = values +
                              "  %a = ctjs.create_array [%x]\n"
                              "  ctjs.copy_props %s into %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "primitive source no-op behavior supplies no object proof",
                      .body = values + "  ctjs.copy_props %zero into %t\n" + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "primitive targets stay outside object copy evidence",
                      .body = values + "  ctjs.copy_props %s into %zero\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "source accessors refuse before copy can invoke their getters",
                      .body = values + "  ctjs.define_accessor \"child\" on %s get %p set %q\n" +
                              copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "target descriptors cannot borrow fresh writable data semantics",
                      .body = values + "  ctjs.define_accessor \"child\" on %t get %p set %q\n" +
                              copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a prototype mutation invalidates the complete copy query",
                      .body = values + "  ctjs.set_proto %y on %s\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late target publication discards copied local contents",
                      .body = values + copy + "  ctjs.store_global \"held\", %t\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "unknown stored values cannot be laundered through an own copy",
                      .body = values + "  ctjs.set_property %s[%other], %p\n" + copy + returned,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const copy_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module, {.what = expected.contents.what,
                     .body = expected.contents.body,
                     .discharged = "",
                     .complete = expected.contents.failure == ArrayContentsFailure::None &&
                                 expected.acyclic});
    };
    const auto run = [&](const copy_row & expected) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the own-object copy fixture did not parse");
            return;
        }
        check(*module, expected);
    };
    for (const copy_row & expected : rows) { run(expected); }

    const std::vector<std::pair<std::string, bool>> keys = {
        {"\"\"", true},
        {"\"0\"", true},
        {"\"00\"", true},
        {"\"4294967294\"", true},
        {"\"4294967295\"", true},
        {"\"length\"", true},
        {"\"constructor\"", true},
        {"\"x\\00y\"", true},
        {"\"" + std::string(256, 'k') + "\"", true},
        {"\"" + std::string(257, 'k') + "\"", false},
        {"\"__proto__\"", false},
    };
    for (const auto & [key, supported] : keys) {
        run({.contents = {.what = "copy accepts only previously proved bounded own String keys",
                          .body = values + "  %fixed = ctjs.constant #ctjs.string<" + key +
                                  ">\n"
                                  "  ctjs.set_property %s[%fixed], %y\n" +
                                  copy + returned,
                          .failure = ArrayContentsFailure::UnsupportedOperation,
                          .exit = supported ? "t -> {t,x,y}" : ""}});
    }

    // Wide copies remain refused at the initial object write; later fields
    // cannot supply authority for that earlier assignment.
    std::string wide = values;
    std::string wideCopies = "ctjs.copy_props:s[child] -> t=x";
    for (unsigned i = 0; i != 32; ++i) {
        const std::string number = std::to_string(i);
        wide += "  %k" + number + " = ctjs.constant #ctjs.string<\"key" + number +
                "\">\n"
                "  ctjs.set_property %s[%k" +
                number + "], %y\n";
        wideCopies += "; ctjs.copy_props:s[key" + number + "] -> t=y";
    }
    wideCopies += "; ctjs.copy_props:t[child] -> t=x";
    for (unsigned i = 0; i != 32; ++i) {
        wideCopies += "; ctjs.copy_props:t[key" + std::to_string(i) + "] -> t=y";
    }
    run({.contents = {.what = "wide copies charge complete snapshots and every indirect edge",
                      .body = wide + copy + "  ctjs.copy_props %t into %t\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "zero -> {}",
                      .propertyCopies = wideCopies.c_str()},
         .discharged = "x,y"});

    copy_row mutation = rows.front();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::CopyPropsOp copied;
        ctjs::SetPropertyOp stored;
        mlir::Value child;
        mlir::Value alternate;
        module->walk([&](ctjs::CopyPropsOp op) { copied = op; });
        module->walk([&](ctjs::SetPropertyOp op) { stored = op; });
        module->walk([&](ctjs::CreateObjectOp op) {
            if (contentsLabel(op) == "x") { child = op.getResult(); }
            if (contentsLabel(op) == "y") { alternate = op.getResult(); }
        });
        const mlir::Value source = copied.getSource();
        const mlir::Value target = copied.getTarget();
        const mlir::Value parameter = function.getBody().front().getArgument(4);
        mlir::OpBuilder builder(copied);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        copied->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        const auto current = [&]() {
            ++liveStates;
            check(*module, mutation);
        };
        current();
        copied->setOperand(1, parameter);
        current();
        copied->setOperand(1, source);
        mutation = rows.front();
        current();
        copied->setOperand(0, parameter);
        current();
        copied->setOperand(0, alternate);
        current();
        copied->setOperand(0, target);
        copied->setOperand(1, child);
        current();
        copied->setOperand(1, source);
        stored->setOperand(2, target);
        current();
        stored->setOperand(2, child);
        mutation = rows.front();
        current();
        auto deletion = ctjs::DeleteNamedOp::create(builder, function.getLoc(), source, "child");
        current();
        deletion.erase();
        mutation = rows.front();
        current();
        auto accessor = ctjs::DefineAccessorOp::create(builder, function.getLoc(), source, "child",
                                                       parameter, parameter);
        current();
        accessor.erase();
        mutation = rows.front();
        current();
        builder.setInsertionPoint(function.getBody().front().getTerminator());
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", target);
        current();
        publication.erase();
        mutation = rows.front();
        current();
        budgets +=
            checkArrayRetention(*module, {.what = "copy retention requires current alias lattices",
                                          .body = mutation.contents.body,
                                          .complete = false,
                                          .withAnalysis = false});
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live own-object copy fixture did not parse");
    }

    copy_row secondPath{
        .contents = {.what = "a late copy operand edit invalidates all earlier path records",
                     .body = values + split + copy + returned + "^right:\n" + copy + returned,
                     .failure = ArrayContentsFailure::UnsupportedOperation,
                     .exit = "t -> {t,x}; t -> {t,x}",
                     .objects = "x:{}; y:{}; s:{child:x}; t:{child:x} | "
                                "x:{}; y:{}; s:{child:x}; t:{child:x}",
                     .propertyCopies = "ctjs.copy_props:s[child] -> t=x; "
                                       "ctjs.copy_props:s[child] -> t=x"}};
    auto branchModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + secondPath.contents.body + "}\n", &context);
    if (branchModule) {
        ctjs::FuncOp function = *branchModule->getOps<ctjs::FuncOp>().begin();
        ctjs::CopyPropsOp finalCopy;
        branchModule->walk([&](ctjs::CopyPropsOp op) { finalCopy = op; });
        const mlir::Value source = finalCopy.getSource();
        mlir::OpBuilder builder(function);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*branchModule, secondPath);
        finalCopy->setOperand(1, function.getBody().front().getArgument(3));
        check(*branchModule, secondPath);
        finalCopy->setOperand(1, source);
        check(*branchModule, secondPath);
        liveStates += 3;
    } else {
        fail(
            row{.what = secondPath.contents.what, .body = secondPath.contents.body, .expected = ""},
            "the live later-path copy fixture did not parse");
    }

    std::string expanding = values + "  %flag = ctjs.truthy %p\n  cf.br ^branch0\n";
    for (unsigned i = 0; i != 10; ++i) {
        const std::string next = i == 9 ? "exit" : "branch" + std::to_string(i + 1);
        expanding += "^branch" + std::to_string(i) + ":\n" + copy + "  cf.cond_br %flag, ^" + next +
                     ", ^" + next + "\n";
    }
    expanding += "^exit:\n" + done;
    auto expandingModule = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expanding + "}\n", &context);
    if (expandingModule) {
        ctjs::FuncOp function = *expandingModule->getOps<ctjs::FuncOp>().begin();
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        const row r{.what = "copy path expansion refuses its initial ordinary-object write",
                    .body = expanding,
                    .expected = ""};
        if (failed(solver.initializeAndRun(*expandingModule))) {
            fail(r, "the copy path-budget fixture's solver did not converge");
        } else {
            const EscapeVerdicts original = computeVerdicts(solver, function, 0);
            const ArrayContentsEvidence refusal = computeArrayContents(function);
            if (refusal.complete || refusal.failure != ArrayContentsFailure::UnsupportedOperation) {
                fail(r, "copy paths did not refuse their initial ordinary-object assignment");
            }
            for (std::size_t limit : {0U, 1U, 32U, 256U, 4096U}) {
                const ArrayContentsEvidence contents = computeArrayContents(function, limit);
                const EscapeVerdicts refined = computeVerdicts(solver, function, limit);
                const bool exhausted = limit < refusal.work;
                const auto failure = exhausted ? ArrayContentsFailure::WorkLimit
                                               : ArrayContentsFailure::UnsupportedOperation;
                const std::size_t work = std::min(limit, refusal.work);
                if (contents.complete || contents.failure != failure || contents.work != work ||
                    !contents.arrays.empty() || !contents.objects.empty() ||
                    !contents.writes.empty() || !contents.reads.empty() ||
                    !contents.propertyWrites.empty() || !contents.propertyReads.empty() ||
                    !contents.propertyDeletions.empty() || !contents.propertyCopies.empty() ||
                    !contents.exits.empty() || refined.arrayRetentionComplete ||
                    refined.confinedStoredSites != 0 || refined.arrayRetentionWork != work ||
                    !llvm::all_of(original.sites, [&](const auto & entry) {
                        auto found = refined.sites.find(entry.first);
                        return found != refined.sites.end() &&
                               found->second.reason == entry.second.reason &&
                               found->second.by == entry.second.by &&
                               found->second.position == entry.second.position;
                    })) {
                    fail(r, "bounded object-write refusal published evidence or changed verdicts");
                }
            }
        }
    } else {
        fail(row{.what = "copy paths are budgeted", .body = expanding, .expected = ""},
             "the copy path-budget fixture did not parse");
    }
    std::printf("object copies: %zu rows, %zu key controls, %u live states, "
                "one wide-source refusal, one missing-lattice control, "
                "%zu retention budget cutoffs, five bounded refusal checks\n",
                rows.size(), keys.size(), liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
