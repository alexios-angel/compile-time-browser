#include "Tests.h"

using namespace ctcompile::test::type_inference;

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctcompile::ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    context.getOrLoadDialect<ctcompile::ctnative::CTNativeDialect>();

    const std::string five = std::string{"  %a = ctjs.constant "} + kFive + "\n" +
                             "  %b = ctjs.constant " + kFive + "\n";
    // The array rows need their own constant names: `five` spells %a and %b,
    // and %a would collide with the array in the rows below.
    const std::string ints = std::string{"  %i1 = ctjs.constant "} + kFive + "\n" +
                             "  %i2 = ctjs.constant " + kFive + "\n";

    const std::vector<row> rows = {
        // --- literals, where the bound proof is the literal itself ----------
        {"5 is an int32", std::string{"  %r = ctjs.constant "} + kFive + " {check}\n",
         "!ctnative.num<i32>"},
        {"1.5 is not an int32", std::string{"  %r = ctjs.constant "} + kOneAndAHalf + " {check}\n",
         "!ctnative.num<f64>"},
        // THE ROW MOST LIKELY TO BE GOT WRONG. -0 is integral and inside int32,
        // and `Object.is(-0, 0)` is false, so calling it an int32 loses a
        // difference JavaScript can see. type-oracle.py agrees by counting
        // NUM_NEGATIVE_ZERO as not-an-i32.
        {"-0 is integral and in range and is still NOT an int32",
         std::string{"  %r = ctjs.constant "} + kNegativeZero + " {check}\n", "!ctnative.num<f64>"},
        {"2**31 is one past the int32 maximum",
         std::string{"  %r = ctjs.constant "} + kTwoToThe31 + " {check}\n", "!ctnative.num<f64>"},
        {"undefined is an empty optional", "  %r = ctjs.constant #ctjs.undefined {check}\n",
         "!ctnative.opt<!ctnative.bottom>"},
        {"null is the same empty optional, which is a declared divergence",
         "  %r = ctjs.constant #ctjs.null {check}\n", "!ctnative.opt<!ctnative.bottom>"},
        {"a boolean literal", "  %r = ctjs.constant #ctjs.boolean<true> {check}\n",
         "!ctnative.bool"},
        {"a string literal", "  %r = ctjs.constant #ctjs.string<\"hi\"> {check}\n",
         "!ctnative.str<utf8>"},

        // --- the operators that need no operand proof -----------------------
        {"`>>>` has no BigInt form, so it is always a number",
         "  %r = ctjs.binary ushr %p, %q {check}\n", "!ctnative.num<f64>"},
        {"`typeof` is always a string", "  %r = ctjs.unary typeof %p {check}\n",
         "!ctnative.str<utf8>"},
        {"unary `+` is ToNumber, which throws on a BigInt", "  %r = ctjs.unary plus %p {check}\n",
         "!ctnative.num<f64>"},
        {"a comparison is always a boolean", "  %r = ctjs.compare lt %p, %q {check}\n",
         "!ctnative.bool"},
        {"concat never consults the BigInt arm", "  %r = ctjs.binary concat %p, %q {check}\n",
         "!ctnative.str<utf8>"},
        {"ToNumber is a double", "  %r = ctjs.convert to_number %p {check}\n",
         "!ctnative.num<f64>"},

        // --- THE NEGATIVE ROWS, which are why the table exists --------------
        {"`|` on unknown operands could be BigInt and must NOT claim i32",
         "  %r = ctjs.binary bitor %p, %q {check}\n", "!ctnative.boxed"},
        {"`-` on unknown operands could be BigInt", "  %r = ctjs.binary sub %p, %q {check}\n",
         "!ctnative.boxed"},
        {"`~` on an unknown operand could be BigInt", "  %r = ctjs.unary bitnot %p {check}\n",
         "!ctnative.boxed"},
        {"generic `+` on unknown operands could concatenate or be BigInt",
         "  %r = ctjs.binary add %p, %q {check}\n", "!ctnative.boxed"},
        {"generic `+` on one unknown operand stays boxed even beside a number",
         five + "  %r = ctjs.binary add %a, %p {check}\n", "!ctnative.boxed"},
        // --- and the two halves of `+` that ARE provable ---------------------
        {"generic `+` on two numbers is a double", five + "  %r = ctjs.binary add %a, %b {check}\n",
         "!ctnative.num<f64>"},
        {"generic `+` on a number and undefined is a double (NaN, but a number)",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %r = ctjs.binary add %a, %u {check}\n",
         "!ctnative.num<f64>"},
        {"generic `+` with a proved string on either side is a string",
         five + "  %s = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.binary add %a, %s {check}\n",
         "!ctnative.str<utf8>"},
        {"generic `+` of a string and an unknown operand is still a string",
         "  %s = ctjs.constant #ctjs.string<\"x\">\n"
         "  %r = ctjs.binary add %s, %p {check}\n",
         "!ctnative.str<utf8>"},

        // --- A SECOND BLOCK, which the single-block rows above cannot test --
        //
        // Every other row lives in the entry block, and the entry block is
        // live by fiat. This one puts the checked op behind a branch so the
        // solver has to decide the successor is live - which it cannot do
        // without SparseConstantPropagation loaded. Without it this row reads
        // `<uninitialized>`.
        {"a value behind a branch is still visited",
         "  %t = ctjs.truthy %p\n"
         "  cf.cond_br %t, ^yes, ^no\n"
         "^yes:\n"
         "  %r = ctjs.unary typeof %p {check}\n"
         "  ctjs.return %r\n"
         "^no:\n",
         "!ctnative.str<utf8>"},

        // --- THE CLOSED WORLD FOR GLOBALS (part 24 Phase 62½-A) -------------
        //
        // A load of a global is the join of every store of that name in the
        // module. Three rows: a stored number is a number; a name nothing
        // stores is boxed (it is a builtin or undeclared); and the mere
        // presence of a `globalThis` load anywhere makes every global boxed,
        // because the table may then be written by a path this rule cannot
        // see.
        {"a global stored a number loads as a number OR undefined - nothing orders the load after "
         "the store",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a global stored a number and a string loads as their join",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %s = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.store_global \"g\", %s\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>"},
        {"a global nothing stores is boxed - it is a builtin or undeclared",
         "  %r = ctjs.load_global \"Math\" {check}\n", "!ctnative.boxed"},
        {"a globalThis load anywhere makes every global boxed",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %w = ctjs.load_global \"globalThis\"\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.boxed"},

        // --- THE LIFT'S POISON IS THE IDENTITY --------------------------------
        //
        // --ctjs-lift-to-scf yields ub.poison for a loop-carried value on the
        // path that leaves the loop. Joined with a number it must stay that
        // number; typed boxed it would absorb, and every `for` loop would be
        // refused by the native lowering - which is how this row was found.
        {"a number joined with the lift's poison is still that number",
         five + "  %t = ctjs.truthy %p\n"
                "  %z = ub.poison : !ctjs.value\n"
                "  %r = scf.if %t -> (!ctjs.value) {\n"
                "    scf.yield %a : !ctjs.value\n"
                "  } else {\n"
                "    scf.yield %z : !ctjs.value\n"
                "  } {check}\n",
         "!ctnative.num<i32>"},

        // --- THE CLOSED SHAPE (part 24 Phase 56A) ----------------------------
        //
        // An object literal used only through constant keys: a read of a key
        // is the join of its stores, from undefined. Any other use opens the
        // shape and every read is boxed.
        //
        // AND THE undefined SEED IS DROPPED WHERE A STORE DOMINATES THE READ -
        // part 24 Phase 59 slice 2 step 3, the field half. "Nothing orders the
        // read after a store" is a statement about fields in general and false
        // of a particular read that a `ctjs.set_property` of the same key, on
        // the same value, in the same `ctjs.func`, properly dominates. The next
        // three rows are that rule and its two negatives, and the negatives are
        // the load-bearing half: narrowing a value that CAN be undefined is a
        // WRONG ANSWER rather than a refusal, because a global is printed as
        // `%.17g` of a double and undefined-as-NaN spells `nan` there.
        {"a closed object's field reads as its store where the store dominates the read",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n",
         "!ctnative.num<i32>"},
        {"a read BEFORE the store keeps the undefined the field started with",
         five + "  %o = ctjs.create_object\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a store on one arm of an scf.if dominates nothing after it",
         five + "  %o = ctjs.create_object\n"
                "  %t = ctjs.truthy %p\n"
                "  scf.if %t {\n"
                "    %k = ctjs.constant #ctjs.string<\"x\">\n"
                "    ctjs.set_property %o[%k], %a\n"
                "  }\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a key never stored reads as undefined",
         "  %o = ctjs.create_object\n"
         "  %k = ctjs.constant #ctjs.string<\"x\">\n"
         "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.opt<!ctnative.bottom>"},
        // --- THE CARRIED CELL (part 24 Phase 59 slice 2, steps 2 and 3) ------
        //
        // The closure lift makes a shared binding a frame-scope variable and
        // marks the `ctjs.create_cell` `ctnative.carried`; its type is then the
        // join over the box's INITIAL and every value ever assigned to it,
        // because a read on a path that reached no assignment loads what the
        // variable was built with. `compiler_impl::predeclare_locals` builds it
        // with `undefined`, so a hoisted `var` is `opt<num>`.
        //
        // AND `ctnative.assigned_before_read` IS THE LIFT SAYING THERE IS NO
        // SUCH PATH. It is written only when one `ctjs.cell_set` properly
        // dominates every read of the binding - every `ctjs.cell_get` here, and
        // every CALL of every closure that captured it, which is a question
        // about operations the lift erases and so cannot be asked in this file.
        // These two rows are the contract between the two translation units:
        // the attribute is spelled once, in TypeInference.h, and its whole
        // observable effect is the difference between them.
        {"a carried cell holds its initial as well as its stores",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %c = ctjs.create_cell %u {ctnative.carried}\n"
                "  ctjs.cell_set %c, %a\n"
                "  %r = ctjs.cell_get %c {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a carried cell the lift proved assigned before every read holds only its stores",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %c = ctjs.create_cell %u {ctnative.assigned_before_read, ctnative.carried}\n"
                "  ctjs.cell_set %c, %a\n"
                "  %r = ctjs.cell_get %c {check}\n",
         "!ctnative.num<i32>"},

        {"a dynamic key opens the shape: every read is boxed",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  ctjs.set_property %o[%p], %a\n"
                "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.boxed"},
        {"an object that reaches a call has an open shape",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  %c = ctjs.call %p(%o)\n"
                "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.boxed"},

        // --- THE DENSE ARRAY (part 24 Phase 57A) -----------------------------
        //
        // WHY THESE ROWS EXIST AT ALL. The emitter hardcodes `vector<double>`
        // and PrintDeduced::isDeducible excludes both `emitc.call_opaque` and
        // `emitc.variable`, so nothing downstream ever prints the element type
        // - a join that widened wrongly would still lower, still compile and
        // still agree with the interpreter on the fixture. The element type is
        // observable HERE and in the oracle corpus, and nowhere else.
        {"a dense array literal is a vector of the join of its appends, from undefined",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.append %i2 to %arr\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<i32>>>"},
        // TWO WIDTHS MERGE, THEY DO NOT UNION. Stage 53G normalises `num<i32>`
        // and `num<f64>` into the wider number, so `[1, 2.5]` is a
        // vector<double> and not a vector of a two-alternative variant.
        {"two numeric widths in one array are the wider number, not a union",
         ints + "  %h = ctjs.constant " + kOneAndAHalf +
             "\n"
             "  %arr = ctjs.create_array [] {check}\n"
             "  ctjs.append %i1 to %arr\n"
             "  ctjs.append %h to %arr\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<f64>>>"},
        {"a literal's own inline elements count toward the element type too",
         ints + "  %arr = ctjs.create_array [%i1] {check}\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<i32>>>"},
        {"an index read is the element type - undefined among it, because a[7] is undefined",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %r = ctjs.get_property %arr[%i1] {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        // `length` IS NEVER UNDEFINED and is never an i32 either: an array's
        // length is a uint32, which does not fit one, and nothing here proves
        // this array is short.
        {"`length` on a dense array is a number, and an f64 rather than an i32",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %k = ctjs.constant #ctjs.string<\"length\">\n"
                "  %r = ctjs.get_property %arr[%k] {check}\n",
         "!ctnative.num<f64>"},

        // --- AND THE FOUR NEGATIVE ROWS, which are why the rule is a proof ---
        //
        // An index STORE is the sparsity route part 24 Stage 57A names by
        // hand: `a[100] = 1` gives `length` 101 with one element. It opens the
        // site, so the literal and every read of it are boxed.
        {"an index store opens the site: a[100] = 1 makes it sparse",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.set_property %arr[%i1], %i2\n"
                "  %r = ctjs.get_property %arr[%i1] {check}\n",
         "!ctnative.boxed"},
        // A KEY NOTHING PROVED A NUMBER READS A PROPERTY, NOT AN ELEMENT:
        // `a["push"]` is a function. The site is still dense - a read is a
        // read - so only the key check stands between this and a claim of
        // `opt<num<i32>>` for a function.
        //
        // THIS ROW IS THE ONLY GATE ON THAT GUARD, and the oracle is not, which
        // was measured: a claim is per REGISTER, the bytecode puts the array
        // literal and the read in one slot, and the join over the two is
        // `boxed` whatever the read claims. Deleting the guard turns this row
        // red with `!ctnative.opt<!ctnative.num<i32>>` and leaves every corpus
        // in check-type-claims.cmake green.
        {"a key nothing proved a number reads a property, and is boxed",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %r = ctjs.get_property %arr[%p] {check}\n",
         "!ctnative.boxed"},
        {"a read through a named key opens the site",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  %k = ctjs.constant #ctjs.string<\"foo\">\n"
                "  %r = ctjs.get_property %arr[%k]\n",
         "!ctnative.boxed"},
        {"an array that escapes into a global is not a vector",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.store_global \"g\", %arr\n",
         "!ctnative.boxed"},

        // --- and the positive halves of the same operators ------------------
        {"`|` on two numbers is an int32", five + "  %r = ctjs.binary bitor %a, %b {check}\n",
         "!ctnative.num<i32>"},
        {"`-` on two numbers is a double", five + "  %r = ctjs.binary sub %a, %b {check}\n",
         "!ctnative.num<f64>"},
        {"`~` on a number is an int32", five + "  %r = ctjs.unary bitnot %a {check}\n",
         "!ctnative.num<i32>"},
        {"static `+` is ToNumber, so on two numbers it is a double",
         five + "  %r = ctjs.binary_static add %a, %b {check}\n", "!ctnative.num<f64>"},
    };

    for (const row & r : rows) { check(context, r); }

    // The complete contents proof needs a known return; the generic row helper
    // returns opaque %p. Keep old array controls unchanged and build these whole
    // functions with a definite scalar return instead.
    const std::string overwritePrefix = prologue() + five +
                                        "  %zero = ctjs.constant #ctjs.number<0>\n"
                                        "  %arr = ctjs.create_array [%a]\n";
    const std::string overwriteRead = "  %r = ctjs.get_property %arr[%zero] {check}\n"
                                      "  ctjs.return %a\n}\n";
    const std::string overwrite = "  ctjs.set_property %arr[%zero], %b\n";
    const std::vector<row> overwriteRows = {
        {"a bounded own overwrite joins its wider stored Number",
         overwritePrefix + "  %wide = ctjs.constant " + kOneAndAHalf +
             "\n  ctjs.set_property %arr[%zero], %wide\n" + overwriteRead,
         "!ctnative.opt<!ctnative.num<f64>>", true},
        {"an overwritten String participates in the complete element join",
         overwritePrefix +
             "  %text = ctjs.constant #ctjs.string<\"changed\">\n"
             "  ctjs.set_property %arr[%zero], %text\n" +
             overwriteRead,
         "!ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>", true},
        {"an overwritten Boolean participates in the complete element join",
         overwritePrefix +
             "  %flag = ctjs.constant #ctjs.boolean<true>\n"
             "  ctjs.set_property %arr[%zero], %flag\n" +
             overwriteRead,
         "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>>", true},
        {"an original negative zero overwrites own index zero",
         overwritePrefix + "  %key = ctjs.constant " + kNegativeZero +
             "\n  ctjs.set_property %arr[%key], %b\n" + overwriteRead,
         "!ctnative.opt<!ctnative.num<i32>>", true},
        {"an out-of-bounds overwrite never borrows a density claim",
         overwritePrefix + "  ctjs.set_property %arr[%a], %b\n" + overwriteRead, "!ctnative.boxed",
         true},
        {"a fractional overwrite never borrows a density claim",
         overwritePrefix + "  %key = ctjs.constant " + kOneAndAHalf +
             "\n  ctjs.set_property %arr[%key], %b\n" + overwriteRead,
         "!ctnative.boxed", true},
        {"an opaque index refuses the complete overwrite proof",
         overwritePrefix + "  ctjs.set_property %arr[%p], %b\n" + overwriteRead, "!ctnative.boxed",
         true},
        {"an opaque stored value refuses the complete overwrite proof",
         overwritePrefix + "  ctjs.set_property %arr[%zero], %p\n" + overwriteRead,
         "!ctnative.boxed", true},
        {"a later call invalidates the whole overwrite proof",
         overwritePrefix + overwrite + "  %call = ctjs.call %p(%a)\n" + overwriteRead,
         "!ctnative.boxed", true},
        {"even a proved length shrink remains outside native vector uses",
         overwritePrefix + overwrite +
             "  %length = ctjs.constant #ctjs.string<\"length\">\n"
             "  ctjs.set_property %arr[%length], %zero\n" +
             overwriteRead,
         "!ctnative.boxed", true},
        {"a self-stored array fails the separate local-use proof",
         overwritePrefix + "  ctjs.set_property %arr[%zero], %arr\n" + overwriteRead,
         "!ctnative.boxed", true},
    };
    for (const row & r : overwriteRows) { check(context, r); }
    {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            overwritePrefix + overwrite + overwriteRead, &context);
        if (!module) {
            std::printf("FAIL live array overwrite mutation fixture did not parse\n");
            ++failures;
        } else {
            check(*module, "own overwrite before live mutation",
                  "!ctnative.opt<!ctnative.num<i32>>");
            ctcompile::ctjs::SetPropertyOp store;
            module->walk([&](ctcompile::ctjs::SetPropertyOp found) { store = found; });
            const mlir::Value key = store.getKey();
            store->setOperand(1, store.getValue()); // 5, outside the one-element array
            check(*module, "a changed index discards the earlier density proof", "!ctnative.boxed");
            store->setOperand(1, key);
            check(*module, "restoring an own index rebuilds density from current IR",
                  "!ctnative.opt<!ctnative.num<i32>>");
        }
    }

    checkIdentityFieldRows(context);
    checkIdentityMapFieldRows(context);
    checkMapZeroSizePresence(context);
    checkMapExactSizePresence(context);
    checkMapDeleteSizePresence(context);
    checkComparisonIdentityRows(context);
    checkComparisonIdentityMutations(context);
    checkStaleFieldEffects(context);
    checkSavedScalarGlobalTypes(context);

    // The call result is not a thrown payload, and catch state is not a
    // post-call assignment. Query the actual continuation argument, keeping
    // unrelated normal-return typing out of the payload observations.
    const std::string joined = R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctnative.nothrow} {
    %bit = ctjs.truthy %condition
    cf.cond_br %bit, ^left, ^right
  ^left:
    ctjs.throw %argument
  ^right:
    %other = ctjs.constant #ctjs.string<"other">
    ctjs.throw %other
  }
)mlir";
    const std::string returning = R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.return %argument
  }
)mlir";
    std::string largeBody;
    for (unsigned index = 0; index < 4096; ++index) {
        largeBody += "    %unused" + std::to_string(index) + " = ctjs.constant #ctjs.undefined\n";
    }
    const std::vector<row> invocationRows = {
        {"invoke obtains a numeric throw from the live callee", invokeModule(throwingHelper(kFive)),
         "!ctnative.num<i32>", true},
        {"invoke preserves negative-zero payload precision",
         invokeModule(throwingHelper(kNegativeZero)), "!ctnative.num<f64>", true},
        {"invoke carries a boolean payload", invokeModule(throwingHelper("#ctjs.boolean<true>")),
         "!ctnative.bool", true},
        {"invoke carries an owning string payload",
         invokeModule(throwingHelper("#ctjs.string<\"payload\">")), "!ctnative.str<utf8>", true},
        {"invoke keeps pre-call state independent of payload and unavailable result",
         invokeModule(throwingHelper(kFive), "%saved"), "!ctnative.str<utf8>", true},
        {"invoke forwards an ordinary call result only to its normal continuation",
         invokeModule(returning, "%returned", true), "!ctnative.num<i32>", true},
        {"invoke joins every live throw and ignores a forged nonthrowing marker",
         invokeModule(joined), "!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>", true},
        {"invoke follows transitive throwing helpers", invokeModule(helperChain(3)),
         "!ctnative.num<i32>", true},
        {"invoke accepts its finite helper-depth boundary", invokeModule(helperChain(32)),
         "!ctnative.num<i32>", true},
        {"invoke refuses a helper-depth proof beyond the bound", invokeModule(helperChain(33)),
         "!ctnative.boxed", true},
        {"invoke refuses an exhausted work proof", invokeModule(throwingHelper(kFive, largeBody)),
         "!ctnative.boxed", true},
        {"invoke does not infer an explicit-only payload across unknown property effects",
         invokeModule(throwingHelper(kFive, "    %key = ctjs.constant #ctjs.string<\"x\">\n"
                                            "    %read = ctjs.get_property %receiver[%key]\n")),
         "!ctnative.boxed", true},
        {"invoke refuses recursive escaping-payload inference",
         invokeModule(throwingHelper(
             kFive, "    %recursive = ctjs.call_direct "
                    "@helper(%receiver, %new_target, %callee, %condition, %argument)\n")),
         "!ctnative.boxed", true},
    };
    for (const row & r : invocationRows) { check(context, r); }

    const std::string numericCompletion = conditionalHelper(kFive, "#ctjs.string<\"failure\">");
    std::string parameterCompletion = numericCompletion;
    parameterCompletion.replace(parameterCompletion.find("ctjs.return %normal"), 19,
                                "ctjs.return %argument");
    const std::string joinedParameterCompletion = R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %bit = ctjs.truthy %condition
    cf.cond_br %bit, ^first, ^second
  ^first:
    cf.br ^normal(%argument : !ctjs.value)
  ^second:
    %other = ctjs.constant #ctjs.number<9223372036854775808>
    cf.cond_br %bit, ^throwing, ^normal(%other : !ctjs.value)
  ^normal(%joined: !ctjs.value):
    ctjs.return %joined
  ^throwing:
    %thrown = ctjs.constant #ctjs.string<"failure">
    ctjs.throw %thrown
  }
)mlir";
    std::string ordinaryCompletion = invokeModule(numericCompletion, "%returned", true);
    const auto normalObservation = ordinaryCompletion.find("    %observed = scf.execute_region");
    ordinaryCompletion.insert(normalObservation,
                              "    %ordinary = ctjs.call_direct "
                              "@helper(%nil, %nil, %nil, %condition, %argument) {check}\n");
    ordinaryCompletion.replace(ordinaryCompletion.find("} {check}"), 9, "}");

    std::string twoReturns = numericCompletion;
    const auto firstReturn = twoReturns.find("    ctjs.return %normal");
    twoReturns.replace(firstReturn, std::string("    ctjs.return %normal").size(), R"mlir(
    cf.cond_br %bit, ^first, ^second
  ^first:
    ctjs.return %normal
  ^second:
    %other = ctjs.constant #ctjs.boolean<true>
    ctjs.return %other
)mlir");

    std::string transitive = numericCompletion;
    transitive.replace(transitive.find("@helper"), 7, "@leaf");
    transitive += R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nested = ctjs.call_direct @leaf(%receiver, %new_target, %callee, %condition, %argument)
    %normal = ctjs.constant #ctjs.boolean<true>
    ctjs.return %normal
  }
)mlir";

    std::string publicHelper = numericCompletion;
    publicHelper.erase(publicHelper.find("private "), 8);
    std::string capturedHelper = numericCompletion;
    capturedHelper.replace(capturedHelper.find("upvalue_count = 0"), 17, "upvalue_count = 1");
    const std::string propertyEffect = "    %key = ctjs.constant #ctjs.string<\"x\">\n"
                                       "    %read = ctjs.get_property %receiver[%key]\n";
    const std::string recursiveCall =
        "    %recursive = ctjs.call_direct "
        "@helper(%receiver, %new_target, %callee, %condition, %argument)\n";
    const std::vector<row> normalInvocationRows = {
        {"invoke joins normal returns independently from string throws",
         invokeModule(numericCompletion, "%returned", true), "!ctnative.num<i32>", true},
        {"invoke forwards a passed parameter across a helper with throw exits",
         invokeModule(parameterCompletion, "%returned", true), "!ctnative.num<i32>", true},
        {"invoke subscribes a passed parameter and SSA joins before normal return",
         invokeModule(joinedParameterCompletion, "%returned", true), "!ctnative.num<f64>", true},
        {"invoke preserves negative zero on normal completion",
         invokeModule(conditionalHelper(kNegativeZero, kFive), "%returned", true),
         "!ctnative.num<f64>", true},
        {"invoke preserves boolean normal completion independently from number throws",
         invokeModule(conditionalHelper("#ctjs.boolean<true>", kFive), "%returned", true),
         "!ctnative.bool", true},
        {"invoke preserves owning string normal completion independently from boolean throws",
         invokeModule(conditionalHelper("#ctjs.string<\"normal\">", "#ctjs.boolean<false>"),
                      "%returned", true),
         "!ctnative.str<utf8>", true},
        {"invoke joins all normal return alternatives", invokeModule(twoReturns, "%returned", true),
         "!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>", true},
        {"invoke takes normal returns from its own helper, not transitive callees",
         invokeModule(transitive, "%returned", true), "!ctnative.bool", true},
        {"invoke leaves an ordinary call in its normal continuation conservative",
         ordinaryCompletion, "!ctnative.boxed", true},
        {"invoke does not invent a normal result for an unconditional throw",
         invokeModule(throwingHelper(kFive), "%returned", true), "!ctnative.boxed", true},
        {"invoke refuses checked normal flow through unknown property effects",
         invokeModule(conditionalHelper(kFive, kFive, propertyEffect), "%returned", true),
         "!ctnative.boxed", true},
        {"invoke refuses checked normal flow through a recursive helper",
         invokeModule(conditionalHelper(kFive, kFive, recursiveCall), "%returned", true),
         "!ctnative.boxed", true},
        {"invoke refuses checked normal flow from an open public helper",
         invokeModule(publicHelper, "%returned", true), "!ctnative.boxed", true},
        {"invoke refuses checked normal flow from a captured helper",
         invokeModule(capturedHelper, "%returned", true), "!ctnative.boxed", true},
        {"invoke refuses checked normal flow after exhausting the work bound",
         invokeModule(conditionalHelper(kFive, kFive, largeBody), "%returned", true),
         "!ctnative.boxed", true},
    };
    for (const row & r : normalInvocationRows) { check(context, r); }

    // Every solve must inspect the same live helper again. Retain a forged
    // nothrow marker while changing its return and adding a global mutation;
    // neither the old completion query nor the marker may authorize the next.
    {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            invokeModule(numericCompletion, "%returned", true), &context);
        if (!module) {
            std::printf("FAIL invocation normal-flow mutation fixture did not parse\n");
            ++failures;
        } else {
            check(*module, "invoke normal flow before live mutation", "!ctnative.num<i32>");
            auto helper = module->lookupSymbol<ctcompile::ctjs::FuncOp>("helper");
            ctcompile::ctjs::ReturnOp returned;
            helper.walk([&](ctcompile::ctjs::ReturnOp found) { returned = found; });
            auto constant = returned.getValue().getDefiningOp<ctcompile::ctjs::ConstantOp>();
            constant->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, "changed"));
            check(*module, "invoke rederives a changed normal return", "!ctnative.str<utf8>");
            mlir::OpBuilder before(returned);
            auto effect = ctcompile::ctjs::StoreGlobalOp::create(before, returned.getLoc(),
                                                                 "published", returned.getValue());
            check(*module, "invoke refuses a newly effectful live helper", "!ctnative.boxed");
            check(*module, "invoke rerun retains the changed helper refusal", "!ctnative.boxed");
            effect.erase();
            check(*module, "invoke rebuilds completion flow after removing the effect",
                  "!ctnative.str<utf8>");
        }
    }

    // AND ONE MODULE THAT MUST NOT PARSE. `ctjs.binary_static sub` names a
    // kind context::binary_op_static has no arm for - it answers undefined -
    // so an inference claiming f64 for it would be wrong, and the fix is not
    // a narrower claim but a verifier that refuses the IR. This is the
    // refutation Phase 54A's adversarial review raised, kept as a test.
    {
        const std::string text =
            prologue() + "  %r = ctjs.binary_static sub %p, %q\n" + "  ctjs.return %r\n}\n";
        mlir::ScopedDiagnosticHandler quiet{&context,
                                            [](mlir::Diagnostic &) { return mlir::success(); }};
        mlir::OwningOpRef<mlir::ModuleOp> module =
            mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        if (module) {
            std::printf("FAIL ctjs.binary_static sub verified, and the helper has no arm for it\n");
            ++failures;
        }
    }

    if (failures != 0) {
        std::printf("\n%d row(s) failed\n", failures);
        return 1;
    }
    std::printf("type inference: every row agrees with JavaScript\n");
    return 0;
}
