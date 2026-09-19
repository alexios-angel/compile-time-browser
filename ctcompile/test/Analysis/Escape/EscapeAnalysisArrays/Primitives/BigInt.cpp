#include "../Harness.h"

namespace ctcompile::test::escape::arrays {

void checkLiteralBigIntIndices(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %key = ctjs.constant #ctjs.bigint<\"0\"> {storage_test_id = \"key\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string read = "  %saved = ctjs.get_property %a[%key]\n";
    const std::string write = "  ctjs.set_property %a[%key], %zero\n";
    const std::string done = "  ctjs.return %a\n";
    unsigned rowCount = 0;
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const contents_row & expected,
                           const char * discharged) {
        checkArrayContents(module, expected);
        const bool complete = expected.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(module, {.what = expected.what,
                                                .body = expected.body,
                                                .discharged = complete ? discharged : "",
                                                .complete = complete});
    };
    const auto run = [&](const contents_row & expected, const char * discharged = "x") {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (module) {
            check(*module, expected, discharged);
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the literal BigInt index fixture did not parse");
        }
        ++rowCount;
    };
    contents_row original{.what = "a decimal BigInt zero replaces only its exact dense child",
                          .body = array + read + write + done,
                          .arrays = "a:[zero]",
                          .reads = "a[0]=x",
                          .exit = "a -> {a}",
                          .writes = "ctjs.create_array[0]:a[0]=x; ctjs.set_property[2]:a[0]=zero"};
    run(original);
    run({.what = "a saved BigInt-indexed child remains retained after its slot is overwritten",
         .body = array + read + write + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "a decimal BigInt one reads and replaces only the second dense element",
         .body = values + "  %one = ctjs.constant #ctjs.bigint<\"1\">\n"
                          "  %a = ctjs.create_array [%zero, %x] {storage_test_id = \"a\"}\n"
                          "  %saved = ctjs.get_property %a[%one]\n"
                          "  ctjs.set_property %a[%one], %zero\n"
                          "  ctjs.return %saved\n",
         .arrays = "a:[zero,zero]",
         .reads = "a[1]=x",
         .exit = "x -> {x}",
         .writes = "ctjs.create_array[0]:a[0]=zero; ctjs.create_array[1]:a[1]=x; "
                   "ctjs.set_property[2]:a[1]=zero"},
        "");
    run({.what = "an array-loaded BigInt key retains its original literal across replacement",
         .body = array +
                 "  %keys = ctjs.create_array [%key] {storage_test_id = \"keys\"}\n"
                 "  %loaded = ctjs.get_property %keys[%zero]\n"
                 "  ctjs.set_property %keys[%zero], %x\n"
                 "  %saved = ctjs.get_property %a[%loaded]\n"
                 "  ctjs.set_property %a[%loaded], %zero\n" +
                 done,
         .arrays = "a:[zero]; keys:[x]",
         .reads = "keys[0]=key; a[0]=x",
         .exit = "a -> {a}"});
    for (const bool missing : {false, true}) {
        const std::string other = missing ? "1" : "0";
        run({.what = "BigInt key transport checks every structural arm and matching frame exit",
             .body = "  %frame = ctjs.frame_enter 8\n" + array +
                     "  %other = ctjs.constant #ctjs.bigint<\"" + other +
                     "\">\n"
                     "  %flag = ctjs.truthy %zero\n"
                     "  cf.cond_br %flag, ^left(%key : !ctjs.value), ^right(%other : !ctjs.value)\n"
                     "^left(%leftkey: !ctjs.value):\n"
                     "  ctjs.set_property %a[%leftkey], %zero\n"
                     "  ctjs.root %a in %frame\n  ctjs.frame_exit %frame\n" +
                     done +
                     "^right(%rightkey: !ctjs.value):\n"
                     "  ctjs.set_property %a[%rightkey], %zero\n"
                     "  ctjs.root %a in %frame\n  ctjs.frame_exit %frame\n" +
                     done,
             .failure = missing ? ArrayContentsFailure::MissingElement : ArrayContentsFailure::None,
             .arrays = "a:[zero] | a:[zero]",
             .exit = "a -> {a}; a -> {a}"});
    }
    // BigIntAttr holds the bytecode literal WITHOUT source 'n'. Malformed
    // attributes and unsupported spellings cannot borrow an index proof, even
    // where today's VM would substitute zero or parse an equivalent number.
    for (const auto & [spelling, failure] :
         {std::pair{"0", ArrayContentsFailure::None},
          std::pair{"1", ArrayContentsFailure::MissingElement},
          std::pair{"4294967294", ArrayContentsFailure::MissingElement},
          std::pair{"4294967295", ArrayContentsFailure::UnknownIndex},
          std::pair{"4294967296", ArrayContentsFailure::UnknownIndex},
          std::pair{"9999999999", ArrayContentsFailure::UnknownIndex},
          std::pair{"1000000000000000000000000", ArrayContentsFailure::UnknownIndex},
          std::pair{"-1", ArrayContentsFailure::UnknownIndex},
          std::pair{"-0", ArrayContentsFailure::UnknownIndex},
          std::pair{"00", ArrayContentsFailure::UnknownIndex},
          std::pair{"+0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0.0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0e0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x0", ArrayContentsFailure::None},
          std::pair{"0b0", ArrayContentsFailure::None},
          std::pair{"0o0", ArrayContentsFailure::None},
          std::pair{"0X0", ArrayContentsFailure::None},
          std::pair{"0B0", ArrayContentsFailure::None},
          std::pair{"0O0", ArrayContentsFailure::None},
          std::pair{"0x00", ArrayContentsFailure::None},
          std::pair{"0b00", ArrayContentsFailure::None},
          std::pair{"0o00", ArrayContentsFailure::None},
          std::pair{"0x1", ArrayContentsFailure::MissingElement},
          std::pair{"0b1", ArrayContentsFailure::MissingElement},
          std::pair{"0o1", ArrayContentsFailure::MissingElement},
          std::pair{"0xa", ArrayContentsFailure::MissingElement},
          std::pair{"0XA", ArrayContentsFailure::MissingElement},
          std::pair{"0xfffffffe", ArrayContentsFailure::MissingElement},
          std::pair{"0xffffffff", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x100000000", ArrayContentsFailure::UnknownIndex},
          std::pair{"0o37777777776", ArrayContentsFailure::MissingElement},
          std::pair{"0o37777777777", ArrayContentsFailure::UnknownIndex},
          std::pair{"0o40000000000", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b11111111111111111111111111111110", ArrayContentsFailure::MissingElement},
          std::pair{"0b11111111111111111111111111111111", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b100000000000000000000000000000000", ArrayContentsFailure::UnknownIndex},
          std::pair{"0xG", ArrayContentsFailure::UnknownIndex},
          std::pair{"0o8", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b2", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x", ArrayContentsFailure::UnknownIndex},
          std::pair{"0o", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x0n", ArrayContentsFailure::UnknownIndex},
          std::pair{"0o0n", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b0n", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x_0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0o0_0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b0_", ArrayContentsFailure::UnknownIndex},
          std::pair{"-0x0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x+0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0b 0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0x000000000000000000000000000000000", ArrayContentsFailure::UnknownIndex},
          std::pair{"0_0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0n", ArrayContentsFailure::UnknownIndex},
          std::pair{"1n", ArrayContentsFailure::UnknownIndex},
          std::pair{" 0", ArrayContentsFailure::UnknownIndex},
          std::pair{"0 ", ArrayContentsFailure::UnknownIndex},
          std::pair{"", ArrayContentsFailure::UnknownIndex}}) {
        for (const bool store : {false, true}) {
            run({.what = "literal BigInt reads and writes independently require an existing slot",
                 .body = array + "  %index = ctjs.constant #ctjs.bigint<\"" + spelling + "\">\n" +
                         (store ? "  ctjs.set_property %a[%index], %zero\n"
                                : "  %read = ctjs.get_property %a[%index]\n") +
                         "  ctjs.return %zero\n",
                 .failure = failure,
                 .arrays = store ? "a:[zero]" : "a:[x]",
                 .reads = store ? "" : "a[0]=x",
                 .exit = "zero -> {}"});
        }
    }
    for (const std::string producer :
         {"  %computed = ctjs.binary add %key, %key\n", "  %computed = ctjs.unary neg %key\n"}) {
        run({.what = "computed BigInt categories cannot supply concrete array indices",
             .body = array + producer + "  ctjs.set_property %a[%computed], %zero\n" + done,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    run({.what = "an object-loaded BigInt key needs independent own-data authority",
         .body = array +
                 "  %name = ctjs.constant #ctjs.string<\"index\">\n"
                 "  %object = ctjs.create_object\n"
                 "  ctjs.set_property %object[%name], %key\n"
                 "  %loaded = ctjs.get_property %object[%name]\n"
                 "  ctjs.set_property %a[%loaded], %zero\n" +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    run({.what = "a late prototype mutation invalidates an otherwise exact BigInt write",
         .body = array + read + write + "  ctjs.set_proto %p on %a\n" + done,
         .failure = ArrayContentsFailure::UnsupportedOperation});
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + original.body + "}\n", &context);
    unsigned liveStates = 0;
    if (module) {
        ctjs::ConstantOp key;
        module->walk([&](ctjs::ConstantOp op) {
            if (llvm::isa<ctjs::BigIntAttr>(op.getValue())) { key = op; }
        });
        mlir::OpBuilder builder(key);
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        for (const auto & [spelling, failure] :
             {std::pair{"0", ArrayContentsFailure::None},
              std::pair{"1", ArrayContentsFailure::MissingElement},
              std::pair{"1n", ArrayContentsFailure::UnknownIndex},
              std::pair{"4294967294", ArrayContentsFailure::MissingElement},
              std::pair{"4294967295", ArrayContentsFailure::UnknownIndex},
              std::pair{"0", ArrayContentsFailure::None}}) {
            key.setValueAttr(ctjs::BigIntAttr::get(&context, spelling));
            original.failure = failure;
            check(*module, original, "x");
            ++liveStates;
        }
        // The same bytes are an element key only for BigInt. Forged completion
        // markers and an earlier successful query cannot authorize String keys.
        for (const auto spelling : {"0x0", "0X0", "0o0", "0O0", "0b0", "0B0"}) {
            key.setValueAttr(ctjs::BigIntAttr::get(&context, spelling));
            original.failure = ArrayContentsFailure::None;
            check(*module, original, "x");
            key.setValueAttr(ctjs::StringAttr::get(&context, spelling));
            original.failure = ArrayContentsFailure::UnknownIndex;
            check(*module, original, "x");
            liveStates += 2;
        }
    } else {
        fail(row{.what = original.what, .body = original.body, .expected = ""},
             "the live literal BigInt index fixture did not parse");
    }
    const std::string calculated =
        array + "  %lhs = ctjs.constant #ctjs.bigint<\"0x1\">\n"
                "  %rhs = ctjs.constant #ctjs.bigint<\"0b1\">\n"
                "  %difference = ctjs.binary sub %lhs, %rhs {storage_test_id = \"difference\"}\n";
    const std::string access = "  %saved = ctjs.get_property %a[%difference]\n"
                               "  ctjs.set_property %a[%difference], %zero\n";
    contents_row difference{.what = "bounded original BigInt subtraction proves an exact own index",
                            .body = calculated + access + done,
                            .arrays = "a:[zero]",
                            .reads = "a[0]=x",
                            .exit = "a -> {a}",
                            .writes =
                                "ctjs.create_array[0]:a[0]=x; ctjs.set_property[2]:a[0]=zero"};
    run(difference);
    run({.what = "a saved child survives the subtraction-indexed overwrite",
         .body = calculated + access + "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        "");
    run({.what = "a loaded subtraction result keeps its origin after replacement",
         .body = calculated +
                 "  %keys = ctjs.create_array [%difference] {storage_test_id = \"keys\"}\n"
                 "  %loaded = ctjs.get_property %keys[%zero]\n"
                 "  ctjs.set_property %keys[%zero], %x\n"
                 "  ctjs.set_property %a[%loaded], %zero\n" +
                 done,
         .arrays = "a:[zero]; keys:[x]",
         .reads = "keys[0]=difference",
         .exit = "a -> {a}"});
    for (const bool opaque : {false, true}) {
        run({.what = "subtraction index transport still checks the statically untaken arm",
             .body = calculated +
                     "  %flag = ctjs.truthy %zero\n"
                     "  cf.cond_br %flag, ^left(%difference : !ctjs.value), ^right(" +
                     (opaque ? "%p" : "%difference") +
                     " : !ctjs.value)\n"
                     "^left(%leftkey: !ctjs.value):\n"
                     "  ctjs.set_property %a[%leftkey], %zero\n" +
                     done +
                     "^right(%rightkey: !ctjs.value):\n"
                     "  ctjs.set_property %a[%rightkey], %zero\n" +
                     done,
             .failure = opaque ? ArrayContentsFailure::UnknownIndex : ArrayContentsFailure::None,
             .arrays = "a:[zero] | a:[zero]",
             .exit = "a -> {a}; a -> {a}"});
    }
    for (const std::string operand : {"%difference", "%loaded"}) {
        run({.what = "a computed or loaded subtraction operand needs separate exact provenance",
             .body = calculated +
                     "  %keys = ctjs.create_array [%lhs]\n"
                     "  %loaded = ctjs.get_property %keys[%zero]\n"
                     "  %chain = ctjs.binary sub " +
                     operand +
                     ", %key\n"
                     "  ctjs.set_property %a[%chain], %zero\n" +
                     done,
             .failure = ArrayContentsFailure::UnknownIndex});
    }
    module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + difference.body + "}\n", &context);
    if (module) {
        ctjs::BinaryOp subtraction;
        module->walk([&](ctjs::BinaryOp op) { subtraction = op; });
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        mlir::OpBuilder builder(subtraction);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        mlir::DataFlowSolver stale;
        stale.load<mlir::dataflow::DeadCodeAnalysis>();
        stale.load<mlir::dataflow::SparseConstantPropagation>();
        stale.load<EscapeAnalysis>();
        if (failed(stale.initializeAndRun(*module))) {
            fail(row{.what = difference.what, .body = difference.body, .expected = ""},
                 "the subtraction fixture's stale solver did not converge");
        }
        for (const bool left : {true, false}) {
            auto operand = (left ? subtraction.getLhs() : subtraction.getRhs())
                               .getDefiningOp<ctjs::ConstantOp>();
            for (const auto & [attribute, failure] :
                 {std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "0x1")),
                            ArrayContentsFailure::None},
                  std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "0O1")),
                            ArrayContentsFailure::None},
                  std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "2")),
                            left ? ArrayContentsFailure::MissingElement
                                 : ArrayContentsFailure::UnknownIndex},
                  std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "0")),
                            left ? ArrayContentsFailure::UnknownIndex
                                 : ArrayContentsFailure::MissingElement},
                  std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "4294967295")),
                            ArrayContentsFailure::UnknownIndex},
                  std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "0x1n")),
                            ArrayContentsFailure::UnknownIndex},
                  std::pair{mlir::Attribute(ctjs::NumberAttr::get(&context, 1.0)),
                            ArrayContentsFailure::UnknownIndex},
                  std::pair{mlir::Attribute(ctjs::StringAttr::get(&context, "1")),
                            ArrayContentsFailure::UnknownIndex},
                  std::pair{mlir::Attribute(ctjs::BigIntAttr::get(&context, "0x1")),
                            ArrayContentsFailure::None}}) {
                operand.setValueAttr(attribute);
                difference.failure = failure;
                check(*module, difference, "x");
                const auto current = computeVerdicts(stale, function);
                const bool complete = failure == ArrayContentsFailure::None;
                if (current.arrayRetentionComplete != complete ||
                    current.confinedStoredSites != static_cast<unsigned>(complete)) {
                    fail(row{.what = difference.what, .body = difference.body, .expected = ""},
                         "stale solver or forged markers authorized a subtraction index");
                }
                ++liveStates;
            }
        }
    } else {
        fail(row{.what = difference.what, .body = difference.body, .expected = ""},
             "the subtraction index fixture did not parse");
    }
    std::printf("literal BigInt indices: %u rows, %u live states, %zu retention budget cutoffs\n",
                rowCount, liveStates, budgets);
}

} // namespace ctcompile::test::escape::arrays
