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
// Mixed class/DOM declarations retain their identities through complete method
// probes and final typed proof. Number conversion reuses the DOM implementation;
// unused identity escapes, replacements and unknown effects still refuse.
// Original M stays entry-local and consumes the class method result. Its Number,
// JSON and URI success/failure paths use the shared DOM proof; unused helpers,
// unsafe methods and binding replacement still refuse. M retains its source body.
// A class method may be the captured sibling helper's only caller. Complete M
// covers Number, JSON and URI fallback through that capture; changing helper
// bindings, escaped identities and unused-method effects still refuse.
// Original captured F retains its nested replacement callback until the shared
// no-match proof checks all direct-call String inputs. Matching/unknown inputs,
// unused matching methods, callback effects and helper identity escapes refuse.
// Captured dataset filters preserve the original Bootstrap predicate and each
// call's callback enclosure. Escapes, implicit arguments and unknown effects
// refuse; full original H retains its unused-holder-slot boundary.
// Entry-local callable holders retain all original slot bodies for the shared
// DOM invocation and typed proof, including original dataset-filter callbacks.
// Uncalled slots (even pure ones), effects, callback/holder escape, replacement
// and invalid later inputs refuse; global holders and local captures stay strict.
