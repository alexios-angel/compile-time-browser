# Global native document, 2026-09-21

Continued clean `21c65d1c`. The previous document-snapshot thread was committed;
the user's next request was a global document object alongside `Element`.
Separate agents implemented the runtime API and integration fixtures; a third
reviewed source proof, borrowing and emitted scope lifetime.

`096e3959` adds `inline constexpr document_object document{}` to
`Runtime/ctnative.hpp`. `Runtime/Browser.hpp` implements `documentElement()`,
`querySelector()` and `querySelectorAll()` through the existing `js_document_t`.
The noncopyable, nonmovable `document_scope` borrows a document and Style engine.
Construction validates Style before publishing a thread-local view pointer;
destruction restores the previous binding on normal and exceptional exits.
Unbound access throws `std::logic_error`. Explicit views and saved snapshots keep
their original owner when a nested scope binds another document. Separate threads
have independent bindings. Scopes are synchronous and must not span coroutine
suspension or interleaved asynchronous tasks; resources must outlive all borrows.

`42c1406f` establishes one lexical scope in generated document entries after all
input validation. Proved source accesses call `ctnative::document` directly.
The declaration uses existing EmitC operand substitution, so unused-call cleanup
cannot discard its lifetime. No printer extension or new source admission was
needed. The existing explicit host contract, null guards, snapshot bounds and
escape/effect proofs remain enforced. All browser behavior remains in public
ctbrowser DOM/Style; native output has no Script dependency.

## Focused validation

- Devbox build: explicit `ctjs-opt`, `ctjs-translate`,
  `ctcompile-test-host-contract`, `ctcompile-test-native-runtime` targets, under
  the shared build lock. Build and focused checks passed on their first runs.
- Exact CTests: `ctcompile_native_runtime` **1/1, 0.01 s** and
  `ctcompile_host_contract` **1/1, 0.56 s**; **0.58 s total**. Runtime checks cover
  missing/invalid bindings, nested documents, exception restoration, saved
  borrowed views/snapshots and isolated worker-thread scope lifetime.
- Selected `CTNative/Browser/native-dom-document.test` and
  `native-dom-document-all.test`: **2/2, 128.07 s**. Respectively **48 native
  executions/82 refusals** and **32 executions/84 refusals**, GCC/Clang,
  explicit/deduced output, both optimization policies and borrowed/owned entries.
  Existing source bodies and refusal cases are unchanged. Each client checks
  an independent outer document after successful calls, selector exceptions and
  invalid input/Style/session rejection. Repeated lifetimes leave no binding.
- All seven changed code/test SHA-256 hashes match the devbox. Pinned scoped
  formatting of five C++ files, Black/AST checks of both Python fixtures and
  `git diff --check` pass. Required `tools/format.sh --check` reports the same
  16 untouched diagnostics in ctdrive, ProviderPaths, Heap and Facts.
- Initial process check inspected 70 Linux cmdline/comm identities and 352
  Windows CIM Name/ExecutablePath/CommandLine records, finding no Claude
  executable, Node CLI or loop and no errors. No browser/shared files changed.
- Full CTest/compiler lit, broad corpus/matrix, WPT, test262, Windows, sanitizers,
  local native builds and push were skipped. No new Node/VM differential result,
  asynchronous document support or full-Bootstrap admission is claimed.

## Next boundary

The original Bootstrap `R.find` helper's omitted receiver still needs a complete
`document.documentElement` presence proof and helper/concat proof. The global
object supplies C++ syntax and scoped borrowing; it does not guarantee a root.
Remaining typed element selector carriers, the application driver, overlapping
DOM Map keys, original B/Data+B, object-valued fields, tagged Map snapshots,
broader Strings, conditional callees and mutable cells remain unfinished.
