// The VM's class helpers keep their old global names in the IR, and a
// script's own use of those names is marked.
//
// ctbrowser compiles a class definition into calls of engine-internal helpers
// it names `%class_defined` and so on, out of every script's reach. The
// importer gives each the IR name it had as an ordinary global
// (`__ctbrowser_class_defined`; CTJS/Import/EngineGlobals.h), which the native
// class proofs are written against. A script that names one of those itself
// (`__ctbrowser_class_defined = 1`, `typeof __ctbrowser_super_get`) means an
// ordinary global of its own, so the importer marks that load or store
// `ctjs.engine_reserved`; the boxed backend then keeps the script's name, and
// native lowering refuses the use by name.
//
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %s 2>/dev/null | FileCheck %s

__ctbrowser_class_defined = 1;
var t = typeof __ctbrowser_super_get;
class A { m() { return 1; } }

// CHECK: ctjs.store_global "__ctbrowser_class_defined", %{{[^ ]+}} {ctjs.engine_reserved}
// CHECK: ctjs.load_global "__ctbrowser_super_get" {ctjs.engine_reserved, typeof_lookup = true}
// CHECK: ctjs.load_global "__ctbrowser_class_defined"{{$}}
