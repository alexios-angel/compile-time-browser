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
// proof checks every direct-call String input. Known matching inputs preserve
// repeated ASCII normalization and untouched Unicode. Unknown inputs, changed
// callbacks/builtins, callback effects and helper identity escapes still refuse.
// Captured dataset filters preserve the original Bootstrap predicate and each
// call's callback enclosure. Escapes, implicit arguments and unknown effects
// refuse; full original H retains its unused-holder-slot boundary.
// Entry-local callable holders retain all original slot bodies for the shared
// DOM invocation and typed proof, including original dataset-filter callbacks.
// Uncalled slots (even pure ones), effects, callback/holder escape, replacement
// and invalid later inputs refuse. Local slots can call fixed sibling M/F
// captures after complete original identity, implicit-argument and typed proof;
// repeated calls preserve JSON/URI fallbacks. Class methods can capture the
// fixed local holder identity, preserving repeated/different keys, DOM writes
// and branch returns. Fixed local aliases retain the same identity. Changed
// holders/slots, escapes, invalid later arguments, unused slots, broad JSON
// result/String comparisons and unknown effects still refuse.
// A slot can combine an original M capture and retained filter callback.
// Direct and class-method calls preserve callback execution and DOM write order;
// callback effects/escapes, changing helpers, invalid inputs and unused slots refuse.
// Constructor-only classes compose with a local H slot's original for-of loop,
// dynamic dataset reads and fresh-result writes, including prefix-stripped keys.
// Stale/changed/transformed keys, coercion hooks, repeated writers and unused
// effects refuse. Nested iterators and unused H slots remain separate boundaries.
// charAt(0) and slice(1) preserve UTF-16 code units through public Core converters,
// including empty/NUL strings, BMP text, pairs and lone surrogates. String results
// survive DOM writes, and concatenation rejoins surrogate halves. Node/native
// expectations retain the VM's separately pinned byte-indexing divergence.
// A proved charAt(0) may lowercase through public Core Unicode 17 mappings.
// Original H keeps dataset keys separate from normalized output keys; collisions
// overwrite in order and __proto__ has one source preimage. Whole-string casing,
// other indices/coercions, detached methods and replacement still refuse.
// Complete original H admits when every slot has original entry calls. Sibling
// String arguments bind before shared F specialization; distinct literal keys
// retain their union, including matching keys. Unknown later keys still refuse.
// Complete H also runs inside original instance methods. Fresh result literals,
// including fixed local aliases, cannot overwrite prototype methods; constructor
// fields retain their original DOM receiver. Real prototype/instance aliases,
// joined fresh/instance locals and unknown formals still count as writes.
// Replacement, deletion, borrowed methods and prototype escape retain refusal.
// Direct class methods retain the original filter predicate and its lexical-this
// enclosure when that callback never reads this. Constructor fields, actual
// parameters, early-return branches and repeated calls retain their source order.
// Fixed method-local cells use the existing ordered transport proof; later writes
// cannot replace an earlier read. Observed this,
// callback effects, receiver escape, invalid later arguments and unused effects refuse.
// Fixed cells remain ordered through loop/switch reads. Constructor method-value
// checks follow fresh instances and possible parameter aliases; prototype reads
// retain the conservative census. Original called methods share their complete
// DOM proof without synthetic duplicate iterators. Captured H, stored receivers,
// aliases, parameters and loop branches preserve dynamic dataset reads; changing
// cells, callback identity/this, detached methods and nested iterators refuse.
// Direct entry helpers prove original callback enclosures before unused receiver
// checks, matching captured helpers. Argument writes and saved String results
// retain their order; observed this/callee/new.target, escapes, invalid arguments,
// replacement and unused helpers refuse.
// Sequential and conditional iterators reprove each original snapshot prefix,
// retaining condition producers, prior loops, saved results and intervening writes.
// The final entry proof checks both arms and joined scalar state. Loop-nested
// iterators, stale snapshots, unknown effects and invalid later calls still refuse.
// Original helper calls before and after entry early returns keep saved results,
// captured helper calls and intervening DOM writes. Both arms must close the same
// frame. Identity observations, escapes, implicit receivers/new.target, excess
// arguments and later invalid calls refuse.
// Uncalled straight-line leaves prove their original bodies without parameter
// facts: literals, identity returns, typeof/Not/Void and strict equality are inert.
// Both live closures and class-retired slots retain this proof; captures, calls,
// coercions and unknown properties refuse. Complete H still needs general F keys.
