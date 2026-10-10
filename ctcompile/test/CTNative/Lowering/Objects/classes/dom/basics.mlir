// RUN: python3 %S/basics.py --translate ctjs-translate --opt ctjs-opt --clang %dom_clang --build %build --include %{monorepo}/ctbrowser/include --work %t --nm %nm --node %node --reference %native_reference

// Runs basics.py, which lowers class-based DOM programs to native code and checks that
// each one either matches the interpreter and Node byte-for-byte or is refused with a
// named reason. The cases live in cases.py, text_cases.py and iteration_cases.py.
// Many copy Bootstrap's minified helpers verbatim: M decodes a data-attribute value
// (boolean, Number, null, JSON, URI), F turns a camelCase key into kebab-case, and H is
// the object that reads and writes data-bs-* attributes. "Refused" means lowering must fail.
//
//
// Receivers and fields:
// - A private helper called directly uses each call's actual DOM element as receiver;
//   distinct receivers, repeated calls and write order are kept.
// - Fresh local fields forward their initialized values, including borrowed elements.
//   Unused value producers keep their effects; holders that escape are refused.
//
// Classes over DOM entries:
// - The class_key/class_order sources lower unchanged; the public class pass still
//   accepts only closed sources.
// - Class methods reach the DOM through the element stored by the constructor. Every
//   unused method body is still proved before it is removed.
// - A captured local getter can supply a DOM key once the constructor closure is proved
//   across every write to its local variable.
//
// Method parameters and defaults:
// - Parameterized methods use each call's arguments and field state; two- and three-hop
//   method chains keep argument, field and DOM write order. Unused parameters, uncalled
//   instances, invalid or dead calls and recursive dependencies are refused.
// - Missing arguments stay undefined. Default expressions see the receiver, tell explicit
//   undefined from null, and run in argument/default/body order, only for omitted or
//   undefined arguments. DefaultType keeps its getter that returns a fresh empty object.
//   Unused defaults keep their effects; unknown defaults skipped by a supplied argument
//   are still refused.
//
// Errors and intrinsics:
// - Unused throwing NAME getters need the Error identity declared and a full body proof.
//   Replaced bindings, escaping errors, effectful messages and live throws are refused.
// - Mixed class/DOM declarations keep their identities. Number conversion reuses the DOM
//   implementation; unused identity escapes, replacements and unknown effects are refused.
//
// Bootstrap helpers M and F:
// - M stays local to the entry and consumes the class method's result. Its Number, JSON
//   and URI success and failure paths use the shared DOM proof; M keeps its source body.
// - A class method may be the only caller of the captured M. Unused helpers, unsafe
//   methods, rebinding M, escaped identities and unused-method effects are refused.
// - F keeps its nested replacement callback until every direct-call String input is
//   proved. Known inputs keep repeated ASCII normalization and leave Unicode untouched.
// - F also accepts dynamic Strings from proved dataset keys and values; saved converted
//   keys survive DOM writes. Unknown, nullable or object inputs, changed callbacks or
//   builtins, observed offsets, discarded effects and intrinsic mutation are refused.
//
// Dataset filters and callable holders:
// - Captured dataset filters keep Bootstrap's predicate and each call's callback
//   enclosure. Escapes, implicit arguments and unknown effects are refused.
// - Entry-local holder objects keep every method body for the shared DOM proof,
//   including filter callbacks. Uncalled methods (even pure ones), effects, callback or
//   holder escapes, replacement and invalid later inputs are refused.
// - Holder methods can call fixed sibling M/F captures, with JSON/URI fallbacks kept
//   across repeated calls. Class methods can capture the fixed holder or its fixed local
//   aliases, keeping repeated and different keys, DOM writes and branch returns.
// - Changed holders or methods, escapes, invalid later arguments, unused methods, broad
//   JSON-result or String comparisons and unknown effects are refused.
// - One method can combine a captured M and a filter callback; direct and class-method
//   calls keep callback execution and DOM write order.
//
// Bootstrap helper H and dataset iteration:
// - Constructor-only classes compose with H's for-of loop over dynamic dataset reads
//   and fresh-result writes, including prefix-stripped keys. Stale, changed or
//   transformed keys, coercion hooks, repeated writers and unused effects are refused;
//   nested iterators and unused H methods remain separate refusals.
// - charAt(0) and slice(1) keep UTF-16 code units through the public Core converters,
//   including empty and NUL strings, BMP text, surrogate pairs and lone surrogates.
//   Concatenation rejoins surrogate halves. The Node/native expectations keep the VM's
//   separately pinned byte-indexing divergence.
// - A proved charAt(0) may lowercase through Core's Unicode 17 mappings. Dataset keys
//   stay separate from normalized output keys: collisions overwrite in order and
//   __proto__ has one source key. Whole-string casing, other indices or coercions,
//   detached methods and replacement are refused.
// - The full H lowers when every method has entry calls. Sibling String arguments bind
//   before F is specialized; distinct literal keys keep their union. Unknown later keys
//   are refused.
// - The full H also runs inside instance methods. Fresh result literals, including fixed
//   local aliases, cannot overwrite prototype methods; constructor fields keep their DOM
//   receiver. Real prototype or instance aliases, joined fresh/instance locals and
//   unknown parameters still count as writes. Replacement, deletion, borrowed methods
//   and prototype escape are refused.
//
// Method-local state and ordering:
// - Class methods keep the filter predicate and its lexical-this enclosure when the
//   callback never reads `this`, and keep source order for constructor fields, arguments,
//   early-return branches and repeated calls.
// - Fixed method-local variables keep their ordered writes through loops and switches;
//   a later write cannot replace an earlier read. Method-value checks in constructors
//   follow fresh instances and possible parameter aliases; prototype reads stay
//   conservative. Changed variables, observed `this`, callback identity, detached
//   methods and nested iterators are refused.
// - Direct entry helpers prove their callback enclosures before the unused-receiver
//   checks, like captured helpers. Observed this/callee/new.target, escapes, invalid
//   arguments, replacement and unused helpers are refused.
// - Sequential and conditional iterators re-prove each snapshot prefix, keeping condition
//   values, earlier loops, saved results and intervening writes; both branches must leave
//   the same frame. Loop-nested iterators and stale snapshots are refused.
// - Helper calls before and after an early return keep saved results and DOM writes.
// - Uncalled straight-line functions are proved without parameter facts: literals,
//   identity returns, typeof, !, void and strict equality are inert. Captures, calls,
//   coercions and unknown properties are refused.
// - Parameter and local variables confined to a function keep arbitrary values and
//   branch writes. Uncalled nested declarations need their own full proof.
