# Constant-expression bindings in native C++

Native emission combines backward binding-mutability analysis with forward
binding-time analysis of the final EmitC initializers. An initialized scalar
can become `constexpr js_num value` or `constexpr auto value` only when its
binding is immutable and its actual C++ initializer is a checked constant
expression. Parameters remain runtime values. Exact deduction pins continue
to require the resulting top-level const type.
Native output declares `using js_num = double;`; this spelling preserves the
underlying floating type, function signatures and constant-expression rules.
The same analysis runs on bodies emitted inside owning lambdas.

The forward worklist starts with typed integer and finite floating literals.
It propagates exact values through supported scalar arithmetic, comparisons,
logical operations, conditionals and representable casts. Every operand must
already have static bindings; the analysis does not replace the initializer,
remove effects or manufacture a constant from a known runtime result. Each
result becomes static once and wakes its users. After 100,000 transfer steps,
remaining declarations retain their ordinary const qualification.

C++ legality is checked independently of static binding time. Signed overflow,
zero divisors, invalid shifts and out-of-range conversions retain runtime
expressions. Narrow integer arithmetic remains runtime because C++ promotes
its intermediates. Floating arithmetic must be finite and exact under APFloat;
inexact, underflowing and overflowing operations remain runtime. Zero-producing
addition/subtraction also remains runtime because rounding can affect its sign.

The source CTJS BTA answers whether the partial evaluator may stage an operation
in its source execution context. That alone does not establish C++ constant
expression legality. A parameter known at one source call is still a parameter
in an unspecialized C++ body; an owning compiler heap cannot persist in a C++
constant expression merely because PE knows it. The target analysis therefore
rederives its facts from final EmitC, ignoring supplied BTA or constexpr claims.
Source precomputation and heap residualisation feed this stage through their
ordinary emitted literals and runtime operations.

Mutable bindings, loads, pointers, opaque text and calls, string/heap carriers,
loop/join storage and hoisted locals retain their previous policy. The native
helper `const_operands` contract describes reference acceptance, not constexpr
execution. Functions and their parameters are not marked constexpr by this
slice; qualifying a function body requires a separate callable contract.

Native lowering enables this printing policy with the nearest module's unit
attribute `ctnative.constexpr_bindings`; the existing const-binding proof is
also required. Unmarked and const-only modules retain their previous spelling.
Implementation lives in `lib/Target/Cpp/Constexpr/`, separate from the mutable
binding proof and source-name allocation.

The target regression compiles and executes explicit, deduced and hoisted
declarations with GCC 13 and Clang 18. Assertions inserted after analysis require
transitive floating results and conversions to be usable as constant expressions.
Negative type-pin tests reject an incorrect deduced type. Runtime cases cover
different function inputs, effectful calls, mutable references, exceptional and
inexact floating operations, signed zero and unsigned wrapping. Invalid integer
expressions are printer-only refusal witnesses; they are never executed.
