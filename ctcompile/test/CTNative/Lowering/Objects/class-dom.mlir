// RUN: python3 %S/class_dom.py --translate ctjs-translate --opt ctjs-opt --clang %dom_clang --build %build --include %{monorepo}/ctbrowser/include --work %t --nm %nm --node %node --reference %native_reference

// Direct private helpers use each call's actual DOM receiver. Equivalent
// source observations pin distinct receivers, repeated calls and write order.
// Fresh local fields forward initialized snapshots, including borrowed elements,
// while unused value producers retain effects and escaping holders still refuse.
// Complete class/DOM sources remain preparation refusals until their original
// body effects and constructor/method receivers compose with the DOM proof.
