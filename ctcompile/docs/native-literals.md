# Readable native C++ literals

Native lowering emits ordinary strings as `std::string("price", 5)` and ordinary
doubles as `100.0`, `0.1` and `-0.0`. This is the default printing policy,
independent of optional optimizations. Numeric values retain their typed
attributes in MLIR; number spelling changes only when producing C++.

The native and boxed string paths share
`include/ctcompile/Support/CppLiterals.hpp`. Printable ASCII stays readable.
Quotes and backslashes use a raw string when the contents can safely appear in
one. Other bytes use exactly three octal digits, so an embedded NUL or a control
byte followed by a digit or `A`–`F` cannot change the value. Native strings retain
their explicit byte length. Trigraph-looking text escapes its second question
mark in ordinary literals so warning-as-error builds accept it.

`lib/Target/Cpp/ReadableFloat.cpp` uses the locale-independent `std::to_chars`
shortest round-trip representation for finite IEEE `f32` and `f64` values.
Integral decimal spellings acquire `.0`; `f32` spellings also acquire `f`.
This preserves the C++ type under `auto`, as well as signed zero. Very large
and small values can still use scientific notation. NaN, infinity and other
floating-point formats retain the existing printer behavior.

The C++ emitter enables that policy only for modules carrying the unit attribute
`ctnative.readable_literals`, which native lowering sets. An unmarked module
retains upstream spelling, including when nested inside a marked module.
Vendored upstream emitter tests remain unchanged.

`CTNative/Lowering/readable-strings.test` checks readable output and compares
returned native strings against independent numeric byte arrays under GCC and
Clang. Its 12 cases include all 256 bytes, embedded NUL, UTF-8/WTF-8, raw-string
terminators and trigraphs. `Target/Cpp/readable-floats.mlir` checks spelling and
module scoping, then compiles and executes 168 exact bit patterns under both
compilers. Those cases cover signed zero, subnormal and normal boundaries,
decimal rounding, adjacent representable values and the largest finite values.

The six sample pairs in `~/Downloads/claude/ctcompile-samples/` are regenerated
through the normal compiler pipeline, then compiled under GCC and Clang and
compared with the interpreter and their independent expected outputs.
