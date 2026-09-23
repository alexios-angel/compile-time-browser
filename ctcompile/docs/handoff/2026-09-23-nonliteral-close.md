# Nonliteral iterator close throws and singleton power exponents, 2026-09-23

Resumed clean **ed41f1e5** and retained nonliteral mutable close `822a327b`
from HANDOFF, current00 and iteration 93's final journal. No dirty drafts or
unmerged `codex-wip-20260907` remained. Unrelated branches were preserved.
Parallel agents prepared escape analysis and source checks; the parent finished
source promotion after its agent was rate-limited.

## Landed

**bdeaa5b9** removes the literal-only restriction on a terminal suppressed
close throw. It first converts the terminal to a return carrying the original
payload, validates its definitions, dominance and shadow frame, and then replaces
only that ignored value with undefined. All producers remain for state transport,
helper expansion and complete typed DOM/effect proof. Unique closure and symbol
use, original/final holder observers, exact suppression, immediate saved-throw
completion and ordinary-close validation remain required.

The original `822a327b8a5dd37602ccf74c027a71a2bbe747ddc3d0304fcaeb962957bc1347`
executes unchanged: close throws current count 13 while the saved exception stays
Number 3. Three derived sources cover an attribute observing count 13, a throwing
return getter, and a comma-expression payload whose attribute write must remain.
The checks exercise exhaustion, reentry, two documents, ordered writes, invalid
attribute names, ordinary-close refusals and unsupported payload producers.
Raw tests retain state producers and reject frame or foreign payloads before
value elimination. Generated code uses owning native values and public DOM calls.

Parallel **425e7d47** recognizes a varying exponent with a proved singleton range
and reuses the existing invariant power transfer. Complete reload/store census,
bounded scalar arithmetic and actual-write replay remain. All 101 historical
power source bodies/CHECKs and raw cases remain unchanged; nine source controls
were added. General nonunit bases with multiple varying exponents still refuse.

## Focused validation

- Exact `ctcompile_host_contract`: **1/1 PASS, 2.40 s test / 2.41 s total**.
- Four selected saved-throw sources: **64 native executions, 120 refusals and
  16 Node/VM observations PASS**, across both providers and policies,
  explicit/deduced C++, GCC and Clang.
- Exact `ctcompile_escape_analysis_arrays`: **1/1 PASS,
  2.29 s test / 2.30 s total**.
- Exact `Analysis/Escape/escape-claims/power-index-overwrite.test`: **1/1 PASS,
  0.12 s**, with 355 other lit cases excluded.
- Full `tools/format.sh --check`: **1126 C++, 157 Python, 114 web files PASS**.
- All **seven final code/test hashes match the devbox**. All **32 generated
  C++ files** contain no Script/VM protocol or nullable-scalar fallback;
  compiled harness linked-symbol checks pass.
- Final local four-source check: **eight Node observations PASS**.

All 233 historical iterator source bodies, 86 metadata rows and 23 existing
saved-throw cases/metadata are preserved. The original nonliteral source is
byte-identical to its preceding refusal. Escape local checks cover 110 exact
Node outputs and own-key sets. Source syntax and preservation checks pass.

The first new host tests attempted to parse frame values as JavaScript values;
four parse assertions failed. Post-parse malformed operands now exercise the
proof directly. Their first build then found an incompatible C++ conditional
operand type; explicit assignments fixed it. No production changes were made
after the first successful admission probe. The native reviewer found no
production blocker before a rate limit interrupted its final test review.

Explicit build targets: `ctjs-opt`, `ctjs-translate`,
`ctcompile-test-host-contract`, `ctcompile-test-native-reference`,
`ctcompile-test-escape-analysis-arrays`, `ctcompile-test-escape-claims` and
`ctcompile-test-type-oracle`. All devbox operations held the build lock.
Evidence and commands: `../../../../test-results/2026-09-23-nonliteral-close/`.

Skipped: full CTest/compiler lit, complete custom-iterator fixture, unaffected
native-source replays, broad corpus/matrices, full Bootstrap, browser/WPT/test262,
Windows, sanitizers and local C++ builds. No push or history rewrite.
No browser/shared implementation files changed. Initial WSL-root Linux 82 and
Windows CIM 350 process checks found no Claude executable/CLI/loop identities or
errors; the later Linux 85/Windows 355 check agreed.

## Next boundary

Retained Boolean source `4def4ec7b2265c7f0afc31e06ebd309c24fb906ab9d4b0b0fce0aa212530336b`
still refuses **DOM protected helper needs an independent inert-body proof** in
both policies. Its close writes `data-closed` and then reads it with
`hasAttribute` as the ignored throw payload. Preserve this post-write producer
and the saved Boolean while extending the protected effect ordering proof.
Nonterminal exceptional state, multiple protected regions, implicit cleanup,
nested custom iterators, unguarded Bootstrap defaults, the application driver
and full native Bootstrap remain unfinished.
