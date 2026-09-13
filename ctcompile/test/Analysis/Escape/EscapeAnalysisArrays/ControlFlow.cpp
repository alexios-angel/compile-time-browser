#include "Harness.h"

namespace ctcompile::test::escape::arrays {

void checkArrayFrames(mlir::MLIRContext & context) {
    const std::string enter = "  %frame = ctjs.frame_enter 4\n";
    const std::string array =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
        "  ctjs.append %x to %a\n";
    const std::string root = "  ctjs.root %x in %frame\n  ctjs.root %a in %frame\n";
    const std::string leave = "  ctjs.frame_exit %frame\n";
    const std::string done = "  ctjs.return %zero\n";
    const std::vector<contents_row> rows = {
        {.what = "an imported frame with exact local roots discharges private elements",
         .body = enter + array + root + leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "matching frame exit does not discharge a returned container's child",
         .body = enter + array + root + leave + "  ctjs.return %a\n",
         .arrays = "a:[x]",
         .exit = "a -> {a,x}"},
        {.what = "a root of a saved read uses the checked original element",
         .body = enter + array +
                 "  %read = ctjs.get_property %a[%zero]\n"
                 "  ctjs.root %read in %frame\n" +
                 leave + done,
         .arrays = "a:[x]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        {.what = "raw import needs no explicit roots to balance its frame",
         .body = enter + array + leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "a successor allocation still belongs to the entry's active frame",
         .body = enter + "  cf.br ^next\n^next:\n" + array + root + leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "a two-edge chain forwards the exact frame, array, loaded value and index",
         .body = enter + array +
                 "  cf.br ^next(%frame, %a, %zero : !ctjs.context, !ctjs.value, !ctjs.value)\n"
                 "^next(%active: !ctjs.context, %base: !ctjs.value, %key: !ctjs.value):\n"
                 "  %read = ctjs.get_property %base[%key]\n"
                 "  ctjs.root %read in %active\n  ctjs.set_property %base[%key], %zero\n"
                 "  cf.br ^exit(%read, %active : !ctjs.value, !ctjs.context)\n"
                 "^exit(%saved: !ctjs.value, %closing: !ctjs.context):\n"
                 "  ctjs.root %saved in %closing\n  ctjs.frame_exit %closing\n"
                 "  ctjs.return %saved\n",
         .arrays = "a:[zero]",
         .reads = "a[0]=x",
         .exit = "x -> {x}"},
        {.what = "two forwarded aliases update one array even when block layout differs",
         .body = enter + array + "  cf.br ^next(%a, %a : !ctjs.value, !ctjs.value)\n^exit:\n" +
                 leave + done +
                 "^next(%first: !ctjs.value, %second: !ctjs.value):\n"
                 "  ctjs.append %x to %first\n  %read = ctjs.get_property %second[%zero]\n"
                 "  ctjs.root %read in %frame\n  cf.br ^exit\n",
         .arrays = "a:[x,x]",
         .reads = "a[0]=x",
         .exit = "zero -> {}"},
        {.what = "an unused opaque entry value forwards without proving its contents",
         .body = enter + array +
                 "  cf.br ^next(%p : !ctjs.value)\n"
                 "^next(%unused: !ctjs.value):\n" +
                 leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "frame exit before a branch cannot release roots used by a later block",
         .body = enter + array + leave + "  cf.br ^next\n^next:\n" + root + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "entry in a successor is too late for the frame-failure proof",
         .body = "  cf.br ^next\n^next:\n" + enter + array + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "a successor's late raw-frame capture invalidates the whole chain",
         .body = enter + array + "  cf.br ^next\n^next:\n  %args = ctjs.make_arguments\n" + leave +
                 done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "the imported unreachable default-return block keeps private children local",
         .body = enter + array + leave + done +
                 "^dead(%unused: !ctjs.value):\n"
                 "  %undefined = ctjs.constant #ctjs.undefined\n" +
                 leave + "  ctjs.return %undefined\n",
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "dead publication, raw-frame capture and unknown effects cannot run",
         .body = enter + array + root + leave + done +
                 "^dead:\n"
                 "  ctjs.store_global \"held\", %a\n"
                 "  %args = ctjs.make_arguments\n"
                 "  \"test.retain_frame\"(%frame) : (!ctjs.context) -> ()\n" +
                 leave + done,
         .arrays = "a:[x]",
         .exit = "zero -> {}"},
        {.what = "a real edge to the publication block refuses independent contents",
         .body = enter + array + root +
                 "  cf.br ^next\n^next:\n"
                 "  ctjs.store_global \"held\", %a\n" +
                 leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "frame entry after an allocation cannot borrow the entry-failure proof",
         .body = array + enter + root + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "even a constant before frame entry stays outside the importer shape",
         .body = "  %early = ctjs.constant #ctjs.undefined\n" + enter + array + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "negative frame size refuses the complete query",
         .body = "  %frame = ctjs.frame_enter -1\n" + array + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "a second active frame is not this activation's root window",
         .body = enter + array + "  %other = ctjs.frame_enter 4\n" + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "an unclosed frame cannot discard its retained register window",
         .body = enter + array + root + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "double exit cannot pop a caller frame",
         .body = enter + array + leave + leave + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "a root cannot write a dead frame window",
         .body = enter + array + leave + root + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "an operation after frame exit cannot allocate in a caller frame",
         .body = enter + array + leave + "  %late = ctjs.create_object\n" + done,
         .failure = ArrayContentsFailure::InvalidFrame},
        {.what = "an unknown rooted value cannot borrow complete local contents",
         .body = enter + array + "  ctjs.root %p in %frame\n" + leave + done,
         .failure = ArrayContentsFailure::UnknownValue},
        {.what = "an unknown frame-handle user may keep the whole root window",
         .body = enter + array + root +
                 "  \"test.retain_frame\"(%frame) : (!ctjs.context) -> ()\n" + leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "balanced roots do not excuse a late call",
         .body = enter + array + root + "  %called = ctjs.call %p(%q)\n" + leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
        {.what = "balanced roots do not excuse implicit arguments retention",
         .body = enter + array + root + "  %args = ctjs.make_arguments\n" + leave + done,
         .failure = ArrayContentsFailure::UnsupportedOperation},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const contents_row & expected) {
        checkArrayContents(module, expected);
        const bool complete = expected.failure == ArrayContentsFailure::None;
        budgets += checkArrayRetention(
            module,
            {.what = expected.what,
             .body = expected.body,
             .discharged = complete && llvm::StringRef(expected.exit) == "zero -> {}" ? "x" : "",
             .complete = complete});
    };
    for (const contents_row & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(row{.what = expected.what, .body = expected.body, .expected = ""},
                 "the frame fixture did not parse");
        }
    }

    // Rebuild both solver and queries after each live edit. Forged completion
    // markers never supply a missing frame, root origin or retention proof.
    contents_row mutation = rows.front();
    mutation.what = "live imported frame, root and exit mutations rebuild every proof";
    mutation.body += "^dead:\n  ctjs.store_global \"held\", %a\n" + done;
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::FrameEnterOp entered;
        ctjs::FrameExitOp exited;
        ctjs::RootOp rooted;
        module->walk([&](ctjs::FrameEnterOp op) { entered = op; });
        module->walk([&](ctjs::FrameExitOp op) { exited = op; });
        module->walk([&](ctjs::RootOp op) { rooted = op; });
        const mlir::Value value = rooted.getValue();
        mlir::OpBuilder builder(exited);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        value.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        rooted->setOperand(1, function.getBody().front().getArgument(3));
        mutation.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        rooted->setOperand(1, value);
        exited->moveBefore(rooted);
        mutation.failure = ArrayContentsFailure::InvalidFrame;
        check(*module, mutation);
        exited->moveBefore(function.getBody().front().getTerminator());
        entered->moveAfter(value.getDefiningOp());
        check(*module, mutation);
        entered->moveBefore(&function.getBody().front().front());
        auto published = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", value);
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        published.erase();
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);

        // Make the previously dead publication reachable in the same IR.
        // Even with forged markers, the new edge must invalidate refinement.
        mlir::Operation * returned = function.getBody().front().getTerminator();
        const mlir::Value result = returned->getOperand(0);
        mlir::Block & dead = function.getBody().back();
        builder.setInsertionPoint(returned);
        const mlir::Value argument = dead.addArgument(value.getType(), function.getLoc());
        dead.front().setOperand(0, argument);
        auto edge =
            mlir::cf::BranchOp::create(builder, function.getLoc(), &dead, mlir::ValueRange{value});
        returned->erase();
        exited->moveBefore(dead.getTerminator());
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        dead.front().erase();
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
        edge->setOperand(0, function.getBody().front().getArgument(3));
        // The successor no longer uses this forwarded value after its old
        // publication was erased. It remains opaque, never a known root.
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
        edge->setOperand(0, value);
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
        builder.setInsertionPointToStart(&dead);
        ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", argument);
        mutation.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        builder.setInsertionPoint(edge);
        auto restored = ctjs::ReturnOp::create(builder, function.getLoc(), result);
        edge.erase();
        exited->moveBefore(restored);
        mutation.failure = ArrayContentsFailure::None;
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.what, .body = mutation.body, .expected = ""},
             "the live frame mutation fixture did not parse");
    }
    std::printf("array frames: %zu rows, twelve live states, %zu retention budget cutoffs\n",
                rows.size(), budgets);
}

void checkArrayConditionals(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %condition = ctjs.truthy %p\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string split = "  cf.cond_br %condition, ^left, ^right\n^left:\n";
    const std::string done = "  ctjs.return %zero\n";
    struct conditional_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclicWrites = true;
    };
    const std::vector<conditional_row> rows = {
        {.contents = {.what = "a child retained on either return path stays Stored",
                      .body = array + split +
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  ctjs.return %a\n^right:\n  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[x]",
                      .exit = "a -> {a,y}; a -> {a,x}"}},
        {.contents = {.what = "both arms overwrite the old child before a shared return",
                      .body = array + split +
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  cf.br ^join\n^right:\n"
                              "  ctjs.set_property %a[%zero], %zero\n"
                              "  cf.br ^join\n^join:\n  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[zero]",
                      .exit = "a -> {a,y}; a -> {a}"},
         .discharged = "x"},
        {.contents = {.what = "duplicate successor edges keep their own overwrite target",
                      .body = array +
                              "  %b = ctjs.create_array [%y] {storage_test_id = \"b\"}\n"
                              "  %c = ctjs.create_array [%a, %b] {storage_test_id = \"c\"}\n"
                              "  cf.cond_br %condition, ^join(%a : !ctjs.value), "
                              "^join(%b : !ctjs.value)\n"
                              "^join(%selected: !ctjs.value):\n"
                              "  ctjs.set_property %selected[%zero], %zero\n"
                              "  ctjs.return %c\n",
                      .arrays = "a:[zero]; b:[y]; c:[a,b] | a:[x]; b:[zero]; c:[a,b]",
                      .exit = "c -> {a,b,c,y}; c -> {a,b,c,x}"}},
        {.contents = {.what = "each path's loaded alias retains its saved pre-overwrite child",
                      .body = array + split +
                              "  %saved = ctjs.get_property %a[%zero]\n"
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  cf.br ^join(%saved : !ctjs.value)\n^right:\n"
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  %later = ctjs.get_property %a[%zero]\n"
                              "  cf.br ^join(%later : !ctjs.value)\n"
                              "^join(%result: !ctjs.value):\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[y] | a:[y]",
                      .reads = "a[0]=x; a[0]=y",
                      .exit = "x -> {x}; y -> {y}"}},
        {.contents = {.what = "successor-local allocations keep exact path-specific origins",
                      .body = values + split +
                              "  %a = ctjs.create_array [%x] "
                              "{storage_test_id = \"a\"}\n"
                              "  cf.br ^join(%a : !ctjs.value)\n^right:\n"
                              "  %b = ctjs.create_array [%y] "
                              "{storage_test_id = \"b\"}\n"
                              "  cf.br ^join(%b : !ctjs.value)\n"
                              "^join(%base: !ctjs.value):\n"
                              "  ctjs.set_property %base[%zero], %zero\n"
                              "  ctjs.return %base\n",
                      .arrays = "a:[zero] | b:[zero]",
                      .exit = "a -> {a}; b -> {b}"},
         .discharged = "x,y"},
        {.contents = {.what = "join-local allocation may contain a different exact value per path",
                      .body = array +
                              "  cf.cond_br %condition, ^join(%x : !ctjs.value), "
                              "^join(%y : !ctjs.value)\n"
                              "^join(%element: !ctjs.value):\n"
                              "  %b = ctjs.create_array [%element] {storage_test_id = \"b\"}\n"
                              "  ctjs.return %b\n",
                      .arrays = "a:[x]; b:[x] | a:[x]; b:[y]",
                      .exit = "b -> {b,x}; b -> {b,y}"}},
        {.contents = {.what = "a missing own slot on one path refuses all earlier successful reads",
                      .body = array + split +
                              "  ctjs.append %y to %a\n"
                              "  cf.br ^join\n^right:\n  cf.br ^join\n^join:\n"
                              "  %read = ctjs.get_property %a[%one]\n"
                              "  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::MissingElement}},
        {.contents = {.what = "an unused opaque alternative stays separate from local join values",
                      .body = array +
                              "  cf.cond_br %condition, ^join(%a : !ctjs.value), "
                              "^join(%p : !ctjs.value)\n"
                              "^join(%unused: !ctjs.value):\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what =
                          "successor pairs swap together on each edge while opaque values travel",
                      .body =
                          array +
                          "  ctjs.append %y to %a\n"
                          "  cf.cond_br %condition, ^pair(%zero, %one, %p : "
                          "!ctjs.value, !ctjs.value, !ctjs.value), ^pair(%one, %zero, %q : "
                          "!ctjs.value, !ctjs.value, !ctjs.value)\n"
                          "^pair(%left: !ctjs.value, %right: !ctjs.value, %opaque: !ctjs.value):\n"
                          "  cf.br ^swapped(%right, %left, %opaque : "
                          "!ctjs.value, !ctjs.value, !ctjs.value)\n"
                          "^swapped(%first: !ctjs.value, %second: !ctjs.value, "
                          "%unused: !ctjs.value):\n"
                          "  %chosen = ctjs.get_property %a[%first]\n"
                          "  %other = ctjs.get_property %a[%second]\n"
                          "  ctjs.return %chosen\n",
                      .arrays = "a:[x,y] | a:[x,y]",
                      .reads = "a[1]=y; a[0]=x; a[0]=x; a[1]=y",
                      .exit = "y -> {y}; x -> {x}"}},
        {.contents = {.what = "a swapped opaque key cannot borrow the other path's exact key",
                      .body =
                          array +
                          "  cf.cond_br %condition, ^pair(%p, %zero : !ctjs.value, !ctjs.value), "
                          "^pair(%zero, %p : !ctjs.value, !ctjs.value)\n"
                          "^pair(%left: !ctjs.value, %right: !ctjs.value):\n"
                          "  cf.br ^swapped(%right, %left : !ctjs.value, !ctjs.value)\n"
                          "^swapped(%key: !ctjs.value, %unused: !ctjs.value):\n"
                          "  %read = ctjs.get_property %a[%key]\n"
                          "  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::UnknownIndex}},
        {.contents = {.what =
                          "saved computed categories survive slot overwrites and successor swaps",
                      .body = values +
                              "  %text = ctjs.unary typeof %x {storage_test_id = \"text\"}\n"
                              "  %literal = ctjs.constant #ctjs.bigint<\"1\">\n"
                              "  %big = ctjs.unary neg %literal {storage_test_id = \"big\"}\n"
                              "  %a = ctjs.create_array [%text, %big] {storage_test_id = \"a\"}\n"
                              "  %savedText = ctjs.get_property %a[%zero]\n"
                              "  %savedBig = ctjs.get_property %a[%one]\n"
                              "  ctjs.set_property %a[%zero], %x\n"
                              "  ctjs.set_property %a[%one], %y\n"
                              "  cf.br ^pair(%savedText, %savedBig : !ctjs.value, !ctjs.value)\n"
                              "^pair(%left: !ctjs.value, %right: !ctjs.value):\n"
                              "  cf.br ^swapped(%right, %left : !ctjs.value, !ctjs.value)\n"
                              "^swapped(%number: !ctjs.value, %string: !ctjs.value):\n"
                              "  %result = ctjs.binary add %string, %number "
                              "{storage_test_id = \"result\"}\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[x,y]",
                      .reads = "a[0]=text; a[1]=big",
                      .exit = "result -> {}"},
         .discharged = "x,y"},
        {.contents = {.what = "simultaneous transport does not authorize a swapping backedge",
                      .body = array +
                              "  cf.br ^loop(%zero, %one : !ctjs.value, !ctjs.value)\n"
                              "^loop(%left: !ctjs.value, %right: !ctjs.value):\n"
                              "  cf.cond_br %condition, ^loop(%right, %left : "
                              "!ctjs.value, !ctjs.value), ^exit\n^exit:\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
        {.contents = {.what = "truthy observes an external predicate without proving its contents",
                      .body =
                          array + split + "  ctjs.append %p to %a\n" + done + "^right:\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "a late publication on the second path invalidates the first return",
                      .body = array + split + done +
                              "^right:\n"
                              "  ctjs.store_global \"held\", %a\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a constant predicate cannot conceal an unsupported structural edge",
                      .body = array +
                              "  %constant = ctjs.truthy %zero\n"
                              "  cf.cond_br %constant, ^left, ^right\n^left:\n"
                              "  ctjs.store_global \"held\", %a\n" +
                              done + "^right:\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what =
                          "a loop on one conditional edge does not reuse an allocation identity",
                      .body = array + split + done + "^right:\n  cf.br ^right\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
        {.contents = {.what = "a balanced frame is checked separately at both return paths",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.root %a in %frame\n  ctjs.frame_exit %frame\n" + done +
                              "^right:\n  ctjs.root %x in %frame\n  ctjs.frame_exit %frame\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "frame and exact predicate forwarding survive a conditional join",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array +
                              "  cf.cond_br %condition, ^join(%frame, %a, %condition : "
                              "!ctjs.context, !ctjs.value, i1), ^join(%frame, %a, %condition : "
                              "!ctjs.context, !ctjs.value, i1)\n"
                              "^join(%active: !ctjs.context, %base: !ctjs.value, %test: i1):\n"
                              "  ctjs.root %base in %active\n  ctjs.frame_exit %active\n" +
                              done,
                      .arrays = "a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "missing frame exit on one path refuses the complete retention proof",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done + "^right:\n" + done,
                      .failure = ArrayContentsFailure::InvalidFrame}},
        {.contents = {.what = "unknown successor frame roots remain unsupported",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done +
                              "^right:\n"
                              "  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" +
                              done,
                      .failure = ArrayContentsFailure::UnknownValue}},
        {.contents = {.what = "cycles in the all-path write union remain conservatively refused",
                      .body = values +
                              "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                              "  %b = ctjs.create_array [] {storage_test_id = \"b\"}\n" +
                              split + "  ctjs.append %b to %a\n" + done +
                              "^right:\n  ctjs.append %a to %b\n" + done,
                      .arrays = "a:[b]; b:[] | a:[]; b:[a]",
                      .exit = "zero -> {}; zero -> {}"},
         .acyclicWrites = false},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const conditional_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module,
            {.what = expected.contents.what,
             .body = expected.contents.body,
             .discharged =
                 expected.contents.failure == ArrayContentsFailure::None ? expected.discharged : "",
             .complete = expected.contents.failure == ArrayContentsFailure::None &&
                         expected.acyclicWrites});
    };
    for (const auto & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (!module) {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the conditional fixture did not parse");
            continue;
        }
        check(*module, expected);
    }

    // Rebuild from live IR after changing only the second path. The first path
    // still returns the overwritten array; stale completion markers cannot
    // erase the unchanged child's alternate retention or a later publication.
    conditional_row mutation = rows[1];
    mutation.contents.what = "live alternate-path mutations invalidate and restore every proof";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        ctjs::SetPropertyOp changed;
        module->walk([&](ctjs::SetPropertyOp op) { changed = op; });
        const mlir::Value original = changed.getValue();
        const mlir::Value base = changed.getObject();
        const mlir::Value child = base.getDefiningOp<ctjs::CreateArrayOp>().getElements()[0];
        mlir::OpBuilder builder(changed);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        changed->setOperand(2, child);
        mutation.contents.arrays = "a:[y] | a:[x]";
        mutation.contents.exit = "a -> {a,y}; a -> {a,x}";
        mutation.discharged = "";
        check(*module, mutation);
        changed->setOperand(2, function.getBody().front().getArgument(3));
        mutation.contents.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        changed->setOperand(2, original);
        auto publication = ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", base);
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        publication.erase();
        mutation = rows[1];
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live conditional fixture did not parse");
    }

    // A short CFG may contain exponentially many paths. Work includes the
    // snapshots themselves; exhaustion after completed earlier exits must
    // return no proof and cannot refine even one original Stored verdict.
    std::string expanding = array + "  cf.br ^b0\n";
    for (unsigned i = 0; i < 16; ++i) {
        const std::string next = "^b" + std::to_string(i + 1);
        expanding +=
            "^b" + std::to_string(i) + ":\n  cf.cond_br %condition, " + next + ", " + next + "\n";
    }
    expanding += "^b16:\n" + done;
    auto explosion = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expanding + "}\n", &context);
    if (explosion) {
        ctjs::FuncOp function = *explosion->getOps<ctjs::FuncOp>().begin();
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        if (failed(solver.initializeAndRun(*explosion))) {
            fail(row{.what = "conditional path explosion is budgeted",
                     .body = expanding,
                     .expected = ""},
                 "the path budget fixture's solver did not converge");
            return;
        }
        const EscapeVerdicts original = computeVerdicts(solver, function, 0);
        for (std::size_t limit : {0U, 1U, 32U, 128U, 1024U}) {
            const auto result = computeArrayContents(function, limit);
            const EscapeVerdicts refined = computeVerdicts(solver, function, limit);
            if (result.complete || result.failure != ArrayContentsFailure::WorkLimit ||
                result.work != limit || !result.arrays.empty() || !result.reads.empty() ||
                !result.writes.empty() || !result.objects.empty() ||
                !result.propertyReads.empty() || !result.propertyWrites.empty() ||
                !result.propertyDeletions.empty() || !result.propertyCopies.empty() ||
                !result.exits.empty() || refined.arrayRetentionComplete ||
                refined.confinedStoredSites != 0 || refined.arrayRetentionWork != limit ||
                !llvm::all_of(original.sites, [&](const auto & entry) {
                    auto found = refined.sites.find(entry.first);
                    return found != refined.sites.end() &&
                           found->second.reason == entry.second.reason &&
                           found->second.by == entry.second.by &&
                           found->second.position == entry.second.position;
                })) {
                fail(row{.what = "conditional path explosion is budgeted",
                         .body = expanding,
                         .expected = ""},
                     "bounded path enumeration published partial evidence");
            }
        }
    } else {
        fail(row{.what = "conditional path explosion is budgeted",
                 .body = expanding,
                 .expected = ""},
             "the path budget fixture did not parse");
    }
    std::printf("array conditionals: %zu rows, five live states, %zu retention budget cutoffs, "
                "five path-explosion cutoffs\n",
                rows.size(), budgets);
}

void checkContainerSwitches(mlir::MLIRContext & context) {
    const std::string values =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> "
        "{storage_test_id = \"one\"}\n"
        "  %key = ctjs.constant #ctjs.string<\"child\">\n"
        "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
        "  %y = ctjs.create_object {storage_test_id = \"y\"}\n"
        "  %flag = ctjs.truthy %p\n";
    const std::string array = values + "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    const std::string split =
        "  cf.switch %flag : i1, [default: ^fallback, 0: ^zero, 1: ^one]\n^fallback:\n";
    const std::string done = "  ctjs.return %zero\n";
    struct switch_row {
        contents_row contents;
        const char * discharged = "";
        bool acyclicWrites = true;
    };
    const std::vector<switch_row> rows = {
        {.contents = {.what = "a default-only switch forwards exact values without a snapshot",
                      .body = array + "  cf.switch %flag : i1, [default: ^join(%a : !ctjs.value)]\n"
                                      "^join(%selected: !ctjs.value):\n"
                                      "  ctjs.set_property %selected[%zero], %y\n"
                                      "  ctjs.return %selected\n",
                      .arrays = "a:[y]",
                      .exit = "a -> {a,y}"},
         .discharged = "x"},
        {.contents = {.what = "all switch edges overwrite the old child before a shared return",
                      .body = array +
                              "  cf.switch %flag : i1, [default: ^join(%y : !ctjs.value), "
                              "0: ^join(%zero : !ctjs.value), 1: ^join(%one : !ctjs.value)]\n"
                              "^join(%replacement: !ctjs.value):\n"
                              "  ctjs.set_property %a[%zero], %replacement\n"
                              "  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[zero] | a:[one]",
                      .exit = "a -> {a,y}; a -> {a}; a -> {a}"},
         .discharged = "x"},
        {.contents = {.what =
                          "three edges to the same block preserve distinct target and value pairs",
                      .body = array +
                              "  %b = ctjs.create_array [%y] {storage_test_id = \"b\"}\n"
                              "  %c = ctjs.create_array [%a, %b] {storage_test_id = \"c\"}\n"
                              "  cf.switch %flag : i1, ["
                              "default: ^join(%a, %zero : !ctjs.value, !ctjs.value), "
                              "0: ^join(%b, %zero : !ctjs.value, !ctjs.value), "
                              "1: ^join(%a, %y : !ctjs.value, !ctjs.value)]\n"
                              "^join(%target: !ctjs.value, %replacement: !ctjs.value):\n"
                              "  ctjs.set_property %target[%zero], %replacement\n"
                              "  ctjs.return %c\n",
                      .arrays = "a:[zero]; b:[y]; c:[a,b] | a:[x]; b:[zero]; c:[a,b] | "
                                "a:[y]; b:[y]; c:[a,b]",
                      .exit = "c -> {a,b,c,y}; c -> {a,b,c,x}; c -> {a,b,c,y}"}},
        {.contents = {.what = "the final switch case retains a child overwritten on earlier edges",
                      .body = array + split +
                              "  ctjs.set_property %a[%zero], %y\n  ctjs.return %a\n"
                              "^zero:\n  ctjs.set_property %a[%zero], %zero\n"
                              "  ctjs.return %a\n^one:\n  ctjs.return %a\n",
                      .arrays = "a:[y] | a:[zero] | a:[x]",
                      .exit = "a -> {a,y}; a -> {a}; a -> {a,x}"}},
        {.contents = {.what =
                          "a saved child returned on one switch edge survives later replacement",
                      .body = array +
                              "  %saved = ctjs.get_property %a[%zero]\n"
                              "  cf.switch %flag : i1, [default: ^join(%saved : !ctjs.value), "
                              "0: ^join(%y : !ctjs.value), 1: ^join(%zero : !ctjs.value)]\n"
                              "^join(%result: !ctjs.value):\n"
                              "  ctjs.set_property %a[%zero], %y\n"
                              "  ctjs.return %result\n",
                      .arrays = "a:[y] | a:[y] | a:[y]",
                      .reads = "a[0]=x",
                      .exit = "x -> {x}; y -> {y}; zero -> {}"}},
        {.contents = {.what = "switch joins allocate one exact container instance per path",
                      .body = values +
                              "  cf.switch %flag : i1, [default: ^join(%x : !ctjs.value), "
                              "0: ^join(%y : !ctjs.value), 1: ^join(%zero : !ctjs.value)]\n"
                              "^join(%element: !ctjs.value):\n"
                              "  %b = ctjs.create_array [%element] {storage_test_id = \"b\"}\n"
                              "  ctjs.return %b\n",
                      .arrays = "b:[x] | b:[y] | b:[zero]",
                      .exit = "b -> {b,x}; b -> {b,y}; b -> {b}"}},
        {.contents = {.what = "own object reads use the replacement from their switch edge",
                      .body = values +
                              "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
                              "  ctjs.set_property %o[%key], %x\n"
                              "  cf.switch %flag : i1, [default: ^join(%y : !ctjs.value), "
                              "0: ^join(%zero : !ctjs.value), 1: ^join(%one : !ctjs.value)]\n"
                              "^join(%replacement: !ctjs.value):\n"
                              "  ctjs.set_property %o[%key], %replacement\n"
                              "  %read = ctjs.get_property %o[%key]\n  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .exit = "y -> {y}; zero -> {}; one -> {}",
                      .objects = "x:{}; y:{}; o:{child:y} | x:{}; y:{}; o:{child:zero} | "
                                 "x:{}; y:{}; o:{child:one}",
                      .propertyReads = "o[child]=y; o[child]=zero; o[child]=one"},
         .discharged = "x"},
        {.contents = {.what = "switch object targets retain exact own fields through array aliases",
                      .body = values +
                              "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
                              "  %b = ctjs.create_object {storage_test_id = \"b\"}\n"
                              "  ctjs.set_property %o[%key], %x\n"
                              "  ctjs.set_property %b[%key], %y\n"
                              "  %c = ctjs.create_array [%o, %b] {storage_test_id = \"c\"}\n"
                              "  cf.switch %flag : i1, ["
                              "default: ^join(%zero, %zero : !ctjs.value, !ctjs.value), "
                              "0: ^join(%one, %zero : !ctjs.value, !ctjs.value), "
                              "1: ^join(%zero, %y : !ctjs.value, !ctjs.value)]\n"
                              "^join(%index: !ctjs.value, %replacement: !ctjs.value):\n"
                              "  %target = ctjs.get_property %c[%index]\n"
                              "  ctjs.set_property %target[%key], %replacement\n"
                              "  ctjs.return %c\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "c:[o,b] | c:[o,b] | c:[o,b]",
                      .reads = "c[0]=o; c[1]=b; c[0]=o",
                      .exit = "c -> {b,c,o,y}; c -> {b,c,o,x}; c -> {b,c,o,y}",
                      .objects = "x:{}; y:{}; o:{child:zero}; b:{child:y} | "
                                 "x:{}; y:{}; o:{child:x}; b:{child:zero} | "
                                 "x:{}; y:{}; o:{child:y}; b:{child:y}",
                      .propertyWrites = "ctjs.set_property[2]:o[child]=x; "
                                        "ctjs.set_property[2]:b[child]=y; "
                                        "ctjs.set_property[2]:o[child]=zero; "
                                        "ctjs.set_property[2]:b[child]=zero; "
                                        "ctjs.set_property[2]:o[child]=y"}},
        {.contents = {.what =
                          "mutually exclusive switch writes cannot hide a mixed-container cycle",
                      .body = values +
                              "  %a = ctjs.create_array [] {storage_test_id = \"a\"}\n"
                              "  %o = ctjs.create_object {storage_test_id = \"o\"}\n" +
                              split + "  ctjs.append %o to %a\n" + done +
                              "^zero:\n  ctjs.set_property %o[%key], %a\n" + done + "^one:\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation,
                      .arrays = "a:[o] | a:[] | a:[]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}",
                      .objects = "x:{}; y:{}; o:{} | x:{}; y:{}; o:{child:a} | "
                                 "x:{}; y:{}; o:{}"},
         .acyclicWrites = false},
        {.contents = {.what =
                          "a transient cycle on a later case survives the final contents overwrite",
                      .body = array + split + done +
                              "^zero:\n  ctjs.set_property %a[%zero], %a\n"
                              "  ctjs.set_property %a[%zero], %zero\n" +
                              done + "^one:\n" + done,
                      .arrays = "a:[x] | a:[zero] | a:[x]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}"},
         .acyclicWrites = false},
        {.contents = {.what = "switch forwarding retains an unused opaque alternative separately",
                      .body = array +
                              "  cf.switch %flag : i1, [default: ^join(%a : !ctjs.value), "
                              "0: ^join(%x : !ctjs.value), 1: ^join(%p : !ctjs.value)]\n"
                              "^join(%unused: !ctjs.value):\n" +
                              done,
                      .arrays = "a:[x] | a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "the last switch path cannot borrow an earlier path's appended slot",
                      .body = array + split +
                              "  ctjs.append %y to %a\n  cf.br ^join\n"
                              "^zero:\n  ctjs.append %y to %a\n  cf.br ^join\n"
                              "^one:\n  cf.br ^join\n^join:\n"
                              "  %read = ctjs.get_property %a[%one]\n  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::MissingElement}},
        {.contents = {.what = "the last switch path cannot borrow an earlier path's own property",
                      .body = values + "  %o = ctjs.create_object\n" + split +
                              "  ctjs.set_property %o[%key], %x\n  cf.br ^join\n"
                              "^zero:\n  ctjs.set_property %o[%key], %y\n  cf.br ^join\n"
                              "^one:\n  cf.br ^join\n^join:\n"
                              "  %read = ctjs.get_property %o[%key]\n  ctjs.return %read\n",
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "exhaustive case values cannot hide publication on the default edge",
                      .body = array + split + "  ctjs.store_global \"held\", %a\n" + done +
                              "^zero:\n" + done + "^one:\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "late publication on the last switch case discards earlier exits",
                      .body = array + split + done + "^zero:\n" + done +
                              "^one:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a constant selector cannot conceal an unsupported case",
                      .body = array +
                              "  %constant = ctjs.truthy %zero\n"
                              "  cf.switch %constant : i1, [default: ^fallback, 1: ^one]\n"
                              "^fallback:\n" +
                              done + "^one:\n  ctjs.store_global \"held\", %a\n" + done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what = "a switch back edge refuses repeated allocation-site instances",
                      .body = array + split + done + "^zero:\n" + done + "^one:\n  cf.br ^one\n",
                      .failure = ArrayContentsFailure::UnsupportedControlFlow}},
        {.contents = {.what = "unknown selector producers do not borrow the switch whitelist",
                      .body = array +
                              "  %opaque = \"test.selector\"() : () -> i32\n"
                              "  cf.switch %opaque : i32, [default: ^join]\n^join:\n" +
                              done,
                      .failure = ArrayContentsFailure::UnsupportedOperation}},
        {.contents = {.what =
                          "all switch edges preserve the active frame and an exact forwarded flag",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array +
                              "  cf.switch %flag : i1, ["
                              "default: ^join(%frame, %a, %flag : !ctjs.context, !ctjs.value, i1), "
                              "0: ^join(%frame, %x, %flag : !ctjs.context, !ctjs.value, i1), "
                              "1: ^join(%frame, %y, %flag : !ctjs.context, !ctjs.value, i1)]\n"
                              "^join(%active: !ctjs.context, %root: !ctjs.value, %test: i1):\n"
                              "  ctjs.root %root in %active\n"
                              "  cf.switch %test : i1, [default: ^exit]\n^exit:\n"
                              "  ctjs.frame_exit %active\n" +
                              done,
                      .arrays = "a:[x] | a:[x] | a:[x]",
                      .exit = "zero -> {}; zero -> {}; zero -> {}"},
         .discharged = "x"},
        {.contents = {.what = "a missing frame exit on the last switch path refuses every root",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done +
                              "^zero:\n  ctjs.frame_exit %frame\n" + done + "^one:\n" + done,
                      .failure = ArrayContentsFailure::InvalidFrame}},
        {.contents = {.what = "an external rooted value on the last switch path remains unknown",
                      .body = "  %frame = ctjs.frame_enter 4\n" + array + split +
                              "  ctjs.frame_exit %frame\n" + done +
                              "^zero:\n  ctjs.frame_exit %frame\n" + done +
                              "^one:\n  ctjs.root %p in %frame\n  ctjs.frame_exit %frame\n" + done,
                      .failure = ArrayContentsFailure::UnknownValue}},
    };
    std::size_t budgets = 0;
    const auto check = [&](mlir::ModuleOp module, const switch_row & expected) {
        checkArrayContents(module, expected.contents);
        budgets += checkArrayRetention(
            module,
            {.what = expected.contents.what,
             .body = expected.contents.body,
             .discharged =
                 expected.contents.failure == ArrayContentsFailure::None ? expected.discharged : "",
             .complete = expected.contents.failure == ArrayContentsFailure::None &&
                         expected.acyclicWrites});
    };
    for (const auto & expected : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            std::string{kPrologue} + expected.contents.body + "}\n", &context);
        if (module) {
            check(*module, expected);
        } else {
            fail(
                row{.what = expected.contents.what, .body = expected.contents.body, .expected = ""},
                "the switch fixture did not parse");
        }
    }

    // Change only the last case's successor operands, retaining forged markers
    // while its target, value and use sites invalidate earlier complete proofs.
    switch_row mutation = rows[1];
    mutation.contents.what = "live switch edges invalidate and restore complete retention";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + mutation.contents.body + "}\n", &context);
    if (module) {
        ctjs::FuncOp function = *module->getOps<ctjs::FuncOp>().begin();
        mlir::cf::SwitchOp branch;
        ctjs::SetPropertyOp changed;
        ctjs::CreateArrayOp container;
        module->walk([&](mlir::cf::SwitchOp op) { branch = op; });
        module->walk([&](ctjs::SetPropertyOp op) { changed = op; });
        module->walk([&](ctjs::CreateArrayOp op) { container = op; });
        const mlir::Value original = branch.getCaseOperands(1).front();
        const mlir::Value child = container.getElements().front();
        mlir::OpBuilder builder(changed);
        function->setAttr("ctnative.array_contents_complete", builder.getUnitAttr());
        function->setAttr("ctnative.array_retention_complete", builder.getUnitAttr());
        child.getDefiningOp()->setAttr("ctnative.confined", builder.getUnitAttr());
        check(*module, mutation);
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{child});
        mutation.contents.arrays = "a:[y] | a:[zero] | a:[x]";
        mutation.contents.exit = "a -> {a,y}; a -> {a}; a -> {a,x}";
        mutation.discharged = "";
        check(*module, mutation);
        branch.getCaseOperandsMutable(1).assign(
            mlir::ValueRange{function.getBody().front().getArgument(3)});
        mutation.contents.failure = ArrayContentsFailure::UnknownValue;
        check(*module, mutation);
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{original});
        auto publication =
            ctjs::StoreGlobalOp::create(builder, function.getLoc(), "held", container.getResult());
        mutation.contents.failure = ArrayContentsFailure::UnsupportedOperation;
        check(*module, mutation);
        publication.erase();
        mutation = rows[1];
        check(*module, mutation);

        // These temporarily malformed edge layouts are checked directly,
        // without feeding invalid IR to the legacy sparse solver. Every
        // incomplete prefix still discards all records, and restoring the
        // valid operation restores the complete query and its refinement.
        const auto malformed = [&](ArrayContentsFailure failure) {
            const row r{.what = "malformed switch edges cannot publish partial proof",
                        .body = mutation.contents.body,
                        .expected = ""};
            const auto empty = [](const ArrayContentsEvidence & result) {
                return result.arrays.empty() && result.objects.empty() && result.writes.empty() &&
                       result.reads.empty() && result.propertyWrites.empty() &&
                       result.propertyReads.empty() && result.propertyDeletions.empty() &&
                       result.propertyCopies.empty() && result.exits.empty();
            };
            const auto result = computeArrayContents(function);
            if (result.complete || result.failure != failure || !result.refusedBy ||
                !empty(result)) {
                fail(r, "malformed edge was accepted or kept partial contents");
            }
            for (std::size_t limit = 0; limit < result.work; ++limit) {
                const auto partial = computeArrayContents(function, limit);
                if (partial.complete || partial.failure != ArrayContentsFailure::WorkLimit ||
                    partial.work != limit || !empty(partial)) {
                    fail(r, "malformed edge's incomplete budget retained evidence");
                    break;
                }
            }
            const auto exact = computeArrayContents(function, result.work);
            if (exact.complete || exact.failure != result.failure || exact.work != result.work ||
                !empty(exact)) {
                fail(r, "malformed edge's exact budget changed the refusal");
            }
        };
        const mlir::Value defaultValue = branch.getDefaultOperands().front();
        branch.getDefaultOperandsMutable().assign(mlir::ValueRange{});
        malformed(ArrayContentsFailure::UnsupportedControlFlow);
        branch.getDefaultOperandsMutable().assign(mlir::ValueRange{defaultValue});
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{});
        malformed(ArrayContentsFailure::UnsupportedControlFlow);
        branch.getCaseOperandsMutable(1).assign(mlir::ValueRange{original});
        mlir::Block * oldTarget = branch.getCaseDestinations()[1];
        auto * emptyBlock = new mlir::Block;
        function.getBody().push_back(emptyBlock);
        branch->setSuccessor(emptyBlock, 2);
        malformed(ArrayContentsFailure::UnsupportedControlFlow);
        branch->setSuccessor(oldTarget, 2);
        emptyBlock->erase();
        const mlir::Value flag = branch.getFlag();
        const auto unknown =
            function.getBody().front().addArgument(builder.getI1Type(), function.getLoc());
        branch.getFlagMutable().set(unknown);
        malformed(ArrayContentsFailure::UnknownValue);
        branch.getFlagMutable().set(flag);
        function.getBody().front().eraseArgument(unknown.getArgNumber());
        check(*module, mutation);
    } else {
        fail(row{.what = mutation.contents.what, .body = mutation.contents.body, .expected = ""},
             "the live switch fixture did not parse");
    }

    // Preserve the 3^10-path source. Its initial ordinary-object assignment
    // now refuses before any switch snapshot can establish own properties.
    std::string expanding = array + "  %o = ctjs.create_object {storage_test_id = \"o\"}\n"
                                    "  ctjs.set_property %o[%key], %a\n  cf.br ^b0\n";
    for (unsigned i = 0; i < 10; ++i) {
        const std::string next = "^b" + std::to_string(i + 1);
        expanding += "^b" + std::to_string(i) + ":\n  cf.switch %flag : i1, [default: " + next +
                     ", 0: " + next + ", 1: " + next + "]\n";
    }
    expanding += "^b10:\n" + done;
    auto explosion = mlir::parseSourceString<mlir::ModuleOp>(
        std::string{kPrologue} + expanding + "}\n", &context);
    if (explosion) {
        ctjs::FuncOp function = *explosion->getOps<ctjs::FuncOp>().begin();
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<EscapeAnalysis>();
        const row r{.what = "switch path expansion refuses its initial ordinary-object write",
                    .body = expanding,
                    .expected = ""};
        if (failed(solver.initializeAndRun(*explosion))) {
            fail(r, "the switch path-budget fixture's solver did not converge");
            return;
        }
        const EscapeVerdicts original = computeVerdicts(solver, function, 0);
        const ArrayContentsEvidence refusal = computeArrayContents(function);
        if (refusal.complete || refusal.failure != ArrayContentsFailure::UnsupportedOperation ||
            refusal.refusedBy == nullptr || !llvm::isa<ctjs::SetPropertyOp>(refusal.refusedBy)) {
            fail(r, "switch paths did not refuse their initial ordinary-object assignment");
        }
        for (std::size_t limit : {0U, 1U, 32U, 128U, 1024U}) {
            const auto result = computeArrayContents(function, limit);
            const EscapeVerdicts refined = computeVerdicts(solver, function, limit);
            const auto failure = limit < refusal.work ? ArrayContentsFailure::WorkLimit
                                                      : ArrayContentsFailure::UnsupportedOperation;
            const std::size_t work = std::min(limit, refusal.work);
            if (result.complete || result.failure != failure || result.work != work ||
                !result.arrays.empty() || !result.reads.empty() || !result.writes.empty() ||
                !result.objects.empty() || !result.propertyReads.empty() ||
                !result.propertyWrites.empty() || !result.propertyDeletions.empty() ||
                !result.propertyCopies.empty() || !result.exits.empty() ||
                refined.arrayRetentionComplete || refined.confinedStoredSites != 0 ||
                refined.arrayRetentionWork != work ||
                !llvm::all_of(original.sites, [&](const auto & entry) {
                    auto found = refined.sites.find(entry.first);
                    return found != refined.sites.end() &&
                           found->second.reason == entry.second.reason &&
                           found->second.by == entry.second.by &&
                           found->second.position == entry.second.position;
                })) {
                fail(r, "bounded switch-prefix refusal published evidence or changed verdicts");
            }
        }
    } else {
        fail(row{.what = "switch path explosion is budgeted", .body = expanding, .expected = ""},
             "the switch path-budget fixture did not parse");
    }
    std::printf("container switches: %zu rows, six live states, four malformed controls, "
                "%zu retention budget cutoffs, five bounded refusal checks\n",
                rows.size(), budgets);
}

} // namespace ctcompile::test::escape::arrays
