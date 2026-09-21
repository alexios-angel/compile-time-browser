# Native element for-of, 2026-09-21

Continued clean `a4db6abb`. Its handoff named element `for…of` as the next
boundary, matching the user's request. No interrupted branch or uncommitted
predecessor work remained. Three bounded agents were assigned fixture, proof
review and iterator-emission work; all reached rate limits without saved code.
Root finished the implementation and validation locally, building only on devbox.

`6a8415be` extends the existing String-snapshot iterator normalizer to proved
element query snapshots and confined Bootstrap `R.find` copies. The original
vendor-pinned helper remains unchanged. Prefix proofs observe length instead of
returning the snapshot, so borrowed elements never gain a return capability.
Enclosing conditions and the selected arm remain in the private prefix, retaining
the original document-root guard for omitted/undefined receivers.

Confined spread normalization runs inside that prefix with charged work and an
operation mapping across its private clone. After iterator normalization, the
whole entry's spread normalization recognizes copied indices in the importer's
guarded before-body form, as well as the existing ordinary after-body form.
Each index still needs its own copied-length guard before rebinding to the query
snapshot; complete live DOM proof checks progression, bounds, effects and borrowing.

Direct NodeList iteration retains `min(snapshot.length, 16777216)`, matching
the VM's eager proxy materialization. A confined concat copy already retains
that cap. Independent reads of the original NodeList remain uncapped. The original
Array/Element and iterator-helper identities are required; source replacement,
unknown calls, custom protocols, mutation and escapes remain refused.

Emission reuses native C++ indexed loops over `std::vector<js_element_t>` and
checked `.at(index).value()` extraction. The vector owns membership; members
borrow the caller/session document and live Style. Attribute/class writes preserve
the saved membership. No new runtime type, VM iterator or Script dependency is
introduced. Literal C++ range-for syntax is not implemented. Browser behavior
continues to reside in public ctbrowser DOM/Style; no browser/shared code changed.

## Focused validation

- Builds ran under `/tmp/ctbrowser-devbox-build.lock` through explicit
  `tools/remote-build.sh ctjs-opt ctjs-translate ctcompile-test-host-contract`
  targets. The first build found an invalid `ModuleOp.clone(mapping)` call;
  it was corrected to the underlying operation clone and typed cast. A deprecated
  scope-exit factory was also replaced with its constructor. Later builds passed.
  The final build compiled only a comment clarification and relinked affected
  targets; no functional change followed the passing tests.
- Exact `ctcompile_host_contract` passed **1/1, 0.63 s; 0.64 s total** using
  `ctest --test-dir projects/compile-time-browser/build --output-on-failure
  --no-tests=error -R '^ctcompile_host_contract$'`. New independent IR checks
  prove capped element iteration, a separate uncapped NodeList length alias,
  removal of `ctjs.iterable`, and no element/index evidence at incomplete proof
  budgets, for both DOM providers.
- Selected lit filter
  `^ctcompile :: CTNative/Browser/native-dom-(element-iteration|find-elements|dataset).test$`
  passed **3/3, 196.05 s**, excluding 401 other discovered tests.
  The new element case passed **64 native executions/118 refusals**; existing
  indexed `R.find` passed **48/114**. GCC/Clang, explicit/deduced printing, both
  optimization settings and borrowed/owned providers cover direct element queries,
  explicit helper receivers and guarded omitted/undefined defaults. Checks include
  empty/detached/replaced roots, DOM order and deduplication, identity, live hover,
  membership through writes, shadow/outside exclusion, selector exception order,
  input/Style validation and outer-document restoration. Generated text and linked
  binaries exclude Script. Refusals include missing identities, budget exhaustion,
  custom/replaced hooks, snapshot mutation/escape and nested iterator opens.
- The affected dataset case passed **28 sources, 112 Node/VM source-double
  observations, eight GCC/Clang binaries and 432 refusals**, including HTML/SVG
  and its existing lifetime sanitizer. This checks the prior String iteration path
  after changing the shared prefix and normalization order.
- The new fixture initially contained an unused `R.find` declaration in its
  direct-query case; the existing complete-source check correctly refused it.
  That declaration was removed. The fixture's iterator check then incorrectly
  inspected inert `ctjs.globals` metadata; it now checks actual emitted C++.
  These were corrected before the passing selected lit run.
- All seven code/test SHA-256 hashes match the devbox. Scoped pinned formatting
  of five C++ files, Black/AST checks of the new Python fixture and
  `git diff --check` pass. Required `tools/format.sh --check` reports the same
  16 untouched diagnostics in `ctdrive.cpp`, `ProviderPaths.h`, `Heap.h` and
  `Facts.cpp`; no repository-wide formatting pass is claimed.
- Initial availability checking inspected 74 Linux cmdline/comm identities and
  352 Windows CIM Name/ExecutablePath/CommandLine records, with no Claude
  executable, Node CLI, live loop or errors. The result was journaled before
  work selection. Broader browser authorization was not needed.
- Full CTest/compiler lit, broad corpus/matrix, WPT, test262, Windows, broad
  sanitizer replays, local builds and push were skipped. No collection above
  the 2^24 cap was allocated. No new element-specific Node/VM differential run
  or full-Bootstrap admission is claimed.

## Next boundary

Prove opens nested inside another loop using changing-prefix and scalar-state
evidence. The current normalizer deliberately admits only sequential or
conditional opens. Custom iterables need callable, effect, close and escape
proofs before native admission. Range-for syntax could reuse the typed vector
after the proved source loop's state and completion behavior are recovered;
it must preserve the proxy cap and ordinary owner/Style borrowing.

Unguarded default roots, general Array behavior, application initialization/driver,
overlapping DOM Map keys, original B/Data+B, object-valued fields, tagged Map
snapshots, broader Strings, conditional callees and mutable cells remain
unfinished. Full Bootstrap is not admitted.
