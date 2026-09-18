// RUN: python3 %S/class_dom.py --translate ctjs-translate --opt ctjs-opt --clang %dom_clang --build %build --include %{monorepo}/ctbrowser/include --work %t --nm %nm --node %node --reference %native_reference

// Direct private helpers use each call's actual DOM receiver. Equivalent
// source observations pin distinct receivers, repeated calls and write order.
// Fresh local fields forward initialized snapshots, including borrowed elements,
// while unused value producers retain effects and escaping holders still refuse.
// Original class_key/class_order sources compose unchanged class proofs with
// typed DOM entries; the public class pass remains closed-source-only. Original
// class methods call the DOM through their constructor-stored element fields;
// every original unused source body still requires proof before erasure.
// Captured local class getter identities can supply a DOM key after the same
// original constructor closure is proved across all writes to its local cell.
// Parameterized methods use each original call's actual arguments and field
// state. Original two/three-hop method calls prove transitive-only parameters on
// each instance, preserving argument, field and DOM write order. Unused formals,
// uncalled instances, invalid/dead calls and recursive dependencies still refuse.
// Missing arguments stay undefined; original default expressions observe the
// receiver, explicit undefined versus null, and argument/default/body order.
// DefaultType keeps its original fresh-empty getter; unused formal defaults
// retain effects and defaults run only for omitted or undefined arguments.
// Unknown defaults still refuse when supplied arguments skip their execution.
// Unused throwing NAME getters require declared Error identity and complete body proof.
// Replaced bindings, escaping errors, effectful messages and live throws still refuse.
