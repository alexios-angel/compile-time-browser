// PHASE 55A'S TABLE, ONE CELL AT A TIME - the sinks-and-carriers rows: the
// sites, allocation, stores and property access, the operators and the kind
// switch, and every call position; plus allocationPc's NameLoc parse.
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
        // THE SITES
        // ===================================================================
        {.what = "an object nothing touches is confined", .body = S + R, .expected = "confined"},
        {.what = "an array nothing touches is confined", .body = A + R, .expected = "confined"},
        {.what = "create_object is a tracked site, not a boxed one",
         .body = S + R,
         .expected = "confined",
         .roles = "~ctjs.create_object",
         .boxed = ""},
        {.what = "create_array's elements SINK(stored): items traced o.cpp:42",
         .body = S + "  %a = ctjs.create_array [%s]\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.create_array sink:stored",
         .boxed = "",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},

        // ===================================================================
        // ALLOCATION - the NEITHER/CARRY positives and the SINK negatives
        // ===================================================================
        {.what = "append: $array NEITHER (o.cpp:161-163 is push_back only)",
         .body = A + "  ctjs.append %p to %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.append neither sink:stored"},
        {.what = "append: $element SINK(stored)",
         .body = S + "  %a = ctjs.create_array []\n  ctjs.append %s to %a\n" + R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},
        {.what = "create_cell: $initial SINK(stored), slot traced o.cpp:59; result phase59",
         .body = S + "  %c = ctjs.create_cell %s\n" + R,
         .expected = "escapes:stored",
         .roles = "ctjs.create_cell sink:stored",
         .boxed = "phase59",
         .storageTarget = "<uninitialized>"},
        {.what = "create_closure: $enclosing_closure NEITHER (c.cpp:881, 910-911 read only)",
         .body = S + "  %f = ctjs.create_closure %s[0] this %p captures %q\n" + R,
         .expected = "confined",
         .roles = "ctjs.create_closure neither sink:captured sink:captured",
         .boxed = "phase59"},
        {.what = "create_closure: $enclosing_this SINK(captured), c.cpp:919",
         .body = S + "  %f = ctjs.create_closure %callee[0] this %s\n" + R,
         .expected = "escapes:captured"},
        {.what = "create_closure: $upvalues SINK(captured), c.cpp:909",
         .body = S + "  %f = ctjs.create_closure %callee[0] this %p captures %s\n" + R,
         .expected = "escapes:captured"},
        {.what = "own_keys: $source NEITHER (o.cpp:204-219, no call); result runtime_array",
         .body = S + "  %k = ctjs.own_keys of %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.own_keys neither",
         .boxed = "runtime_array"},
        {.what = "iterable: $source CARRY - the site stays confined through it",
         .body = S + "  %i = ctjs.iterable of %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.iterable carry",
         .boxed = "runtime_array"},
        {.what = "iterable CARRIES: returning the iterable returns the site (c.cpp:555)",
         .body = S + "  %i = ctjs.iterable of %s\n  ctjs.return %i\n",
         .expected = "escapes:returned"},
        {.what = "make_arguments is a boxed site, reason arguments",
         .body = "  %a = ctjs.make_arguments\n" + S + R,
         .expected = "confined",
         .roles = "ctjs.make_arguments",
         .boxed = "arguments",
         .capturesAllArguments = true},
        {.what = "gather_rest is a boxed site, reason arguments",
         .body = "  %a = ctjs.gather_rest from 1\n" + S + R,
         .expected = "confined",
         .roles = "ctjs.gather_rest",
         .boxed = "arguments",
         .capturesAllArguments = true},
        {.what = "create_regexp is a boxed site, reason not_tracked",
         .body = S + "  %re = ctjs.create_regexp \"a\", \"g\"\n" + R,
         .expected = "confined",
         .roles = "ctjs.create_regexp",
         .boxed = "not_tracked"},

        // ===================================================================
        // STORES AND PROPERTY ACCESS
        // ===================================================================
        {.what = "store_global SINK(stored_global): globals_ is the first root (o.cpp:667)",
         .body = S + "  ctjs.store_global \"g\", %s\n" + R,
         .expected = "escapes:stored_global",
         .roles = "ctjs.store_global sink:stored_global"},
        {.what = "load_global's result is external, not a site",
         .body = "  %g = ctjs.load_global \"x\" {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "reloading a just-published local through a global stays external",
         .body = "  %o = ctjs.create_object\n"
                 "  ctjs.store_global \"g\", %o\n"
                 "  %g = ctjs.load_global \"g\" {check}\n" +
                 R,
         .expected = "{external}",
         .alias = true},
        {.what = "overwriting a global does not revoke the earlier publication",
         .body = S +
                 "  ctjs.store_global \"g\", %s\n"
                 "  %alias = ctjs.load_global \"g\"\n"
                 "  ctjs.store_global \"retained\", %alias\n"
                 "  ctjs.store_global \"g\", %p\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "a child published only through its container remains stored",
         .body = S +
                 "  %outer = ctjs.create_array [%s]\n"
                 "  ctjs.store_global \"g\", %outer\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "escapes:stored_global"},
        {.what = "the enclosing container keeps its distinct global-publication reason",
         .body = "  %child = ctjs.create_object\n"
                 "  %outer = ctjs.create_array [%child] {check}\n"
                 "  ctjs.store_global \"g\", %outer\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "set_property: $object NEITHER, the star proof (o.cpp:397-419 walks null)",
         .body = S + "  ctjs.set_property %s[%p], %q\n" + R,
         .expected = "confined",
         .roles = "ctjs.set_property neither sink:converted sink:stored"},
        {.what = "set_property: $key SINK(converted), to_string o.cpp:193 -> coerce.cpp:171-179",
         .body = S + "  ctjs.set_property %p[%s], %q\n" + R,
         .expected = "escapes:converted"},
        {.what = "set_property: $value SINK(stored), value.hpp:632-639",
         .body = S + "  ctjs.set_property %p[%q], %s\n" + R,
         .expected = "escapes:stored",
         .storageTarget = "{external}",
         .storageTargetVerdicts = ""},
        {.what = "get_property: $object NEITHER (o.cpp:445-464, data-only find on the table)",
         .body = S + "  %r = ctjs.get_property %s[%p]\n" + R,
         .expected = "confined",
         .roles = "ctjs.get_property neither sink:converted"},
        {.what = "get_property: $key SINK(converted), o.cpp:154",
         .body = S + "  %r = ctjs.get_property %p[%s]\n" + R,
         .expected = "escapes:converted"},
        {.what = "a property READ off a site is external - contents are not tracked",
         .body = "  %o = ctjs.create_object\n  %r = ctjs.get_property %o[%p] {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "delete_property: $object NEITHER (o.cpp:325-329 erase)",
         .body = S + "  ctjs.delete_property %s[%p]\n" + R,
         .expected = "confined",
         .roles = "ctjs.delete_property neither sink:converted"},
        {.what = "delete_property: $key SINK(converted), o.cpp:327",
         .body = S + "  ctjs.delete_property %p[%s]\n" + R,
         .expected = "escapes:converted"},
        {.what = "delete_named: $object NEITHER (o.cpp:200-202)",
         .body = S + "  ctjs.delete_named \"k\" from %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.delete_named neither"},
        {.what = "has_property: $object NEITHER (o.cpp:266-278, no call)",
         .body = S + "  %h = ctjs.has_property %p in %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.has_property neither sink:converted"},
        {.what = "has_property: $key SINK(converted), o.cpp:263/265",
         .body = S + "  %h = ctjs.has_property %s in %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "get_proto NEITHER (o.cpp:245-248 field read)",
         .body = S + "  %g = ctjs.get_proto %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.get_proto neither"},
        {.what = "set_proto: $object SINK(proto_mutated) - the chain can now reach a setter",
         .body = S + "  ctjs.set_proto %p on %s\n" + R,
         .expected = "escapes:proto_mutated",
         .roles = "ctjs.set_proto sink:proto_mutated sink:stored"},
        {.what = "set_proto: $proto SINK(stored), traced o.cpp:56",
         .body = S + "  ctjs.set_proto %s on %p\n" + R,
         .expected = "escapes:stored"},
        {.what = "define_accessor: $target SINK(accessor_defined), o.cpp:221-228 then 452-457",
         .body = S + "  ctjs.define_accessor \"k\" on %s get %p set %q\n" + R,
         .expected = "escapes:accessor_defined",
         .roles = "ctjs.define_accessor sink:accessor_defined sink:stored sink:stored"},
        {.what = "define_accessor: $getter SINK(stored), traced o.cpp:52-55",
         .body = S + "  ctjs.define_accessor \"k\" on %p get %s set %q\n" + R,
         .expected = "escapes:stored"},
        {.what = "define_accessor: $setter SINK(stored)",
         .body = S + "  ctjs.define_accessor \"k\" on %p get %q set %s\n" + R,
         .expected = "escapes:stored"},
        {.what = "copy_props: $target NEITHER (o.cpp:230-243 raw set, no accessor)",
         .body = S + "  ctjs.copy_props %p into %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.copy_props neither neither"},
        {.what = "copy_props: $source NEITHER - its contents were sunk when stored",
         .body = S + "  ctjs.copy_props %s into %p\n" + R,
         .expected = "confined"},
        {.what = "load_upvalue: $closure NEITHER (cell read, rows 0,0,0)",
         .body = S + "  %u = ctjs.load_upvalue %s[0]\n" + R,
         .expected = "confined",
         .roles = "ctjs.load_upvalue neither"},
        {.what = "store_upvalue: $closure NEITHER (rl.cpp:1240-1249 writes a slot)",
         .body = S + "  ctjs.store_upvalue %s[0], %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.store_upvalue neither sink:stored"},
        {.what = "store_upvalue: $value SINK(stored), traced o.cpp:59 via 64",
         .body = S + "  ctjs.store_upvalue %p[0], %s\n" + R,
         .expected = "escapes:stored"},
        {.what = "cell_get NEITHER (rl.cpp:1210-1214)",
         .body = S + "  %c = ctjs.cell_get %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.cell_get neither"},
        {.what = "cell_set: $cell NEITHER",
         .body = S + "  ctjs.cell_set %s, %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.cell_set neither sink:stored"},
        {.what = "cell_set: $value SINK(stored), slot traced o.cpp:59",
         .body = S + "  ctjs.cell_set %p, %s\n" + R,
         .expected = "escapes:stored"},

        // ===================================================================
        // OPERATORS - binary, binary_static, and the kind switch both ways
        // ===================================================================
        {.what = "binary: $lhs SINK(converted) - ToPrimitive runs valueOf with it as receiver",
         .body = S + "  %b = ctjs.binary add %s, %p\n" + R,
         .expected = "escapes:converted",
         .roles = "ctjs.binary sink:converted sink:converted"},
        {.what = "binary: $rhs SINK(converted)",
         .body = S + "  %b = ctjs.binary add %p, %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "binary_static: $lhs NEITHER - static to_number, coerce.cpp:285-299",
         .body = S + "  %b = ctjs.binary_static add %s, %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.binary_static neither neither"},
        {.what = "binary_static: $rhs NEITHER",
         .body = S + "  %b = ctjs.binary_static bitor %p, %s\n" + R,
         .expected = "confined"},
        // The kind switch: three operations, no annotation, C++ decides.
        {.what = "unary not NEITHER (total)",
         .body = S + "  %u = ctjs.unary not %s\n" + R,
         .expected = "confined",
         .roles = "~ctjs.unary neither"},
        {.what = "unary typeof NEITHER",
         .body = S + "  %u = ctjs.unary typeof %s\n" + R,
         .expected = "confined"},
        {.what = "unary void NEITHER",
         .body = S + "  %u = ctjs.unary void %s\n" + R,
         .expected = "confined"},
        {.what = "unary neg SINK(converted), def:337-340 negate may_reenter 1",
         .body = S + "  %u = ctjs.unary neg %s\n" + R,
         .expected = "escapes:converted",
         .roles = "~ctjs.unary sink:converted"},
        {.what = "unary plus SINK(converted), def:354-356 to_number may_reenter 1",
         .body = S + "  %u = ctjs.unary plus %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "unary bitnot SINK(converted)",
         .body = S + "  %u = ctjs.unary bitnot %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare strict_eq NEITHER on the lhs (value.hpp:293-311)",
         .body = S + "  %c = ctjs.compare strict_eq %s, %p\n" + R,
         .expected = "confined",
         .roles = "~ctjs.compare neither neither"},
        {.what = "compare strict_eq NEITHER on the rhs",
         .body = S + "  %c = ctjs.compare strict_eq %p, %s\n" + R,
         .expected = "confined"},
        {.what = "compare eq SINK(converted), def:394-396 loose_equal may_reenter 1",
         .body = S + "  %c = ctjs.compare eq %s, %p\n" + R,
         .expected = "escapes:converted",
         .roles = "~ctjs.compare sink:converted sink:converted"},
        {.what = "compare lt SINK(converted) on the rhs, def:411 less",
         .body = S + "  %c = ctjs.compare lt %p, %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare le SINK(converted)",
         .body = S + "  %c = ctjs.compare le %s, %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare gt SINK(converted)",
         .body = S + "  %c = ctjs.compare gt %s, %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "compare ge SINK(converted)",
         .body = S + "  %c = ctjs.compare ge %s, %p\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_boolean NEITHER (total)",
         .body = S + "  %c = ctjs.convert to_boolean %s\n" + R,
         .expected = "confined",
         .roles = "~ctjs.convert neither"},
        {.what = "convert to_number SINK(converted)",
         .body = S + "  %c = ctjs.convert to_number %s\n" + R,
         .expected = "escapes:converted",
         .roles = "~ctjs.convert sink:converted"},
        {.what = "convert to_string SINK(converted)",
         .body = S + "  %c = ctjs.convert to_string %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_primitive SINK(converted)",
         .body = S + "  %c = ctjs.convert to_primitive %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_property_key SINK(converted)",
         .body = S + "  %c = ctjs.convert to_property_key %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "convert to_object SINK(converted) in the MVP (a carry in principle)",
         .body = S + "  %c = ctjs.convert to_object %s\n" + R,
         .expected = "escapes:converted"},
        {.what = "truthy NEITHER, and its i1 result is not an object",
         .body = S + "  %t = ctjs.truthy %s\n" + R,
         .expected = "confined",
         .roles = "ctjs.truthy neither"},
        {.what = "instanceof: $object NEITHER (o.cpp:282-323 walks fields, def:451-452)",
         .body = S + "  %i = ctjs.instanceof %s, %p\n" + R,
         .expected = "confined",
         .roles = "ctjs.instanceof neither neither"},
        {.what = "instanceof: $constructor NEITHER - ensure_prototype stores on the ctor only",
         .body = S + "  %i = ctjs.instanceof %p, %s\n" + R,
         .expected = "confined"},

        // ===================================================================
        // CALLS - every position a sink, except the two spread arrays
        // ===================================================================
        {.what = "call: $callee SINK(passed)",
         .body = S + "  %r = ctjs.call %s(%p)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call sink:passed sink:passed"},
        {.what = "call: $receiver SINK(passed) - call_frame::receiver is a root (o.cpp:683)",
         .body = S + "  %r = ctjs.call %p(%s)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call: $args SINK(passed) - copied into the callee window (c.cpp:82-92)",
         .body = S + "  %r = ctjs.call %p(%q, %s)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call sink:passed sink:passed sink:passed"},
        {.what = "a call's result is external",
         .body = "  %r = ctjs.call %p(%q) {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        // PHASE 62½-A: the resolved form carries ctjs.call's row, all five
        // positions, and the symbol changes none of them. The callee is @f
        // itself - the only ctjs.func the harness's module has - so the
        // symbol-use verifier sees five operands against five block arguments.
        {.what = "call_direct: $receiver SINK(passed), like call",
         .body = S + "  %r = ctjs.call_direct @f(%s, %p, %q, %p, %q)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call_direct sink:passed sink:passed sink:passed sink:passed sink:passed"},
        {.what = "call_direct: $new_target SINK(passed)",
         .body = S + "  %r = ctjs.call_direct @f(%p, %s, %q, %p, %q)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call_direct: $callee_value SINK(passed)",
         .body = S + "  %r = ctjs.call_direct @f(%p, %q, %s, %p, %q)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call_direct: $args SINK(passed) - the callee window (c.cpp:82-92)",
         .body = S + "  %r = ctjs.call_direct @f(%p, %q, %p, %q, %s)\n" + R,
         .expected = "escapes:passed"},
        {.what = "a call_direct's result is external",
         .body = "  %r = ctjs.call_direct @f(%p, %q, %p, %q, %p) {check}\n" + R,
         .expected = "{external}",
         .alias = true},
        {.what = "construct: $callee SINK(passed)",
         .body = S + "  %r = ctjs.construct %s(%p)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.construct sink:passed sink:passed",
         .boxed = ""},
        {.what = "construct: $new_target SINK(passed), root o.cpp:686",
         .body = S + "  %r = ctjs.construct %p(%s)\n" + R,
         .expected = "escapes:passed"},
        {.what = "construct: $args SINK(passed)",
         .body = S + "  %r = ctjs.construct %p(%q, %s)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.construct sink:passed sink:passed sink:passed"},
        {.what = "call_spread: $callee SINK(passed)",
         .body = S + "  %r = ctjs.call_spread %s(%p, %q)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.call_spread sink:passed neither sink:passed"},
        {.what = "call_spread: $receiver SINK(passed)",
         .body = S + "  %r = ctjs.call_spread %p(%s, %q)\n" + R,
         .expected = "escapes:passed"},
        {.what = "call_spread: $args NEITHER - spread_arguments copies the items (c.cpp:711-717)",
         .body = A + "  %r = ctjs.call_spread %p(%q, %s)\n" + R,
         .expected = "confined"},
        {.what = "construct_spread: $callee SINK(passed)",
         .body = S + "  %r = ctjs.construct_spread %s(%p)\n" + R,
         .expected = "escapes:passed",
         .roles = "ctjs.construct_spread sink:passed neither"},
        {.what = "construct_spread: $args NEITHER (c.cpp:734-741)",
         .body = A + "  %r = ctjs.construct_spread %p(%s)\n" + R,
         .expected = "confined"},
        // Spread copies the argument array's CONTENTS into the callee window.
        // The array may stay confined while one of those contents outlives it;
        // the create_array/append sink must survive the spread's NEITHER row.
        {.what = "call_spread keeps its local argument array confined despite an object element",
         .body = "  %child = ctjs.create_object\n"
                 "  %args = ctjs.create_array [%child] {check}\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n" +
                 R,
         .expected = "confined"},
        {.what = "call_spread cannot revoke the escape of an element stored in its arguments",
         .body = S +
                 "  %args = ctjs.create_array [%s]\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},
        {.what = "construct_spread keeps its local argument array confined with an object element",
         .body = "  %child = ctjs.create_object\n"
                 "  %args = ctjs.create_array [] {check}\n"
                 "  ctjs.append %child to %args\n"
                 "  %r = ctjs.construct_spread %p(%args)\n" +
                 R,
         .expected = "confined"},
        {.what = "construct_spread cannot revoke an appended element's escape",
         .body = S +
                 "  %args = ctjs.create_array []\n"
                 "  ctjs.append %s to %args\n"
                 "  %r = ctjs.construct_spread %p(%args)\n" +
                 R,
         .expected = "escapes:stored",
         .storageTarget = "{ctjs.create_array}",
         .storageTargetVerdicts = "confined"},
        {.what = "an iterable alias of the spread argument array remains confined",
         .body = A +
                 "  %args = ctjs.iterable of %s\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n" +
                 R,
         .expected = "confined"},
        {.what = "a spread receiver that aliases the argument array still escapes",
         .body = A +
                 "  %args = ctjs.iterable of %s\n"
                 "  %r = ctjs.call_spread %p(%s, %args)\n" +
                 R,
         .expected = "escapes:passed"},
        {.what = "later publication of an iterable argument alias sinks the original array",
         .body = A +
                 "  %args = ctjs.iterable of %s\n"
                 "  %r = ctjs.call_spread %p(%q, %args)\n"
                 "  ctjs.store_global \"arguments\", %args\n" +
                 R,
         .expected = "escapes:stored_global"},
        {.what = "dynamic_import SINK(converted) - the specifier's toString is user code",
         .body = S + "  %m = ctjs.dynamic_import %s\n" + R,
         .expected = "escapes:converted",
         .roles = "ctjs.dynamic_import sink:converted"},
    };

    for (const row & r : rows) { check(context, r); }

    // allocationPc: the importer's NameLoc inside its FusedLoc, and nothing
    // else.
    {
        const std::string text =
            std::string{kPrologue} +
            "  %s = ctjs.create_object loc(fused[\"program:p:3:17\", \"f\":1:2])\n"
            "  %o = ctjs.create_object loc(\"program:p:3:9\")\n"
            "  %n = ctjs.create_object\n"
            "  ctjs.return %p\n}\n";
        mlir::OwningOpRef<mlir::ModuleOp> module =
            mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        std::vector<std::optional<unsigned>> pcs;
        if (module) {
            module->walk([&](ctjs::CreateObjectOp op) { pcs.push_back(allocationPc(op)); });
        }
        const std::vector<std::optional<unsigned>> expected = {17U, 9U, std::nullopt};
        if (!module || pcs != expected) {
            std::printf("FAIL allocationPc: expected 17, 9, none\n");
            ++failures;
        }
    }

    if (failures != 0) {
        std::printf("\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("escape analysis: %zu rows, every cell agrees with the VM\n", rows.size());
    return 0;
}
