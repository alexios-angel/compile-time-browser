// The tuple verifier, one expected-error per clause. A one-element array
// literal is an ordinary array, so a tuple has at least two positions.
// Each check names the message it expects rather than matching "error:".

// RUN: ctjs-opt --split-input-file --verify-diagnostics %s

// expected-error @below {{a tuple needs at least two elements}}
func.func private @one(!ctnative.tuple<!ctnative.bool>)

// -----

// A two-position tuple is accepted.
func.func private @two(!ctnative.tuple<!ctnative.bool, !ctnative.str<utf8>>)
