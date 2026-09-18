// RUN: python3 %S/class_dom.py --translate ctjs-translate --opt ctjs-opt --clang %dom_clang --build %build --include %{monorepo}/ctbrowser/include --work %t --nm %nm --node %node --reference %native_reference

// Direct private helpers use each call's actual DOM receiver. Equivalent
// source observations pin distinct receivers, repeated calls and write order.
// Fresh local fields forward initialized snapshots, including borrowed elements,
// while unused value producers retain effects and escaping holders still refuse.
// Original class_key/class_order sources compose unchanged class proofs with
// typed DOM entries; the public class pass remains closed-source-only. Original
// class method DOM effects and every unused source body still require proof.
