# Interrupted conformance crash repairs, 2026-09-22

Resumed iteration 65 from the 17:10 journal and four frozen replacement files.
The parser fix had already landed as **4dafb9c4**, updating ctjs to **83275ba1**.
It guards invalid parser operands before indexing the AST; malformed dynamic
Function bodies now produce catchable SyntaxError. **b6f6f963** completes the
interrupted replacement fix, checking the existing 2^28-byte string ceiling
before expansion and both replacement accumulation paths. Numbered captures
retain one conversion; named getters and conversions retain token order.

The three exact test262 cases had reproduced two SIGSEGVs and one allocation
abort. ASan/UBSan identified invalid parser indexing and an attempted 32 GiB
replacement allocation. After repair:

| Focused check | Result |
| --- | --- |
| Standalone ctjs `run-vparse`, including five invalid operands | PASS |
| `vm_functions`, normal | 1/1 PASS, 0.04 s test |
| `vm_functions`, ASan/UBSan | 1/1 PASS, 0.85 s test / 0.88 s total |
| `regexp_model` and `string_basics`, normal | 2/2 PASS, 0.06 s total |
| `regexp_model` and `string_basics`, ASan/UBSan | 2/2 PASS, 0.89 s total |
| `test/staging/sm/String/replace-math.js` | PASS normally and in both sanitizer modes |
| `test/annexB/built-ins/Function/createdynfn-html-open-comment-body.js` | Ordinary conformance FAIL; no sanitizer crash |
| `test/staging/sm/class/methDefnGen.js` | Ordinary conformance FAIL; no sanitizer crash |

Annex B HTML comments remain unsupported; four invalid method forms are still
accepted. Those semantics gaps were not hidden by changing expectations.
An independent read-only replacement review found no blocking defect. On resume,
all four replacement source hashes matched the files used by the completed
focused gates, so those gates were not repeated. The complete formatter passed
1124 C++, 157 Python and 114 web files.

Linux command lines were fully readable with no Claude executable/CLI/loop;
Windows CIM also found none. Claude was confirmed stopped before browser edits
and landing; the final landing check inspected 71 Linux and 348 Windows
processes. Changes were claimed and Git/devbox operations locked. No native
admission rule changed in these repairs. Full CTest/compiler lit, broad corpus
or native matrices, full WPT/test262, Windows and local C++ builds were skipped.

Saved build/test commands and logs are in
`../../../../test-results/2026-09-22-crash-recovery/`.
Native work continues at the loop-carried callable boundary recorded in HANDOFF;
the application driver and full native Bootstrap remain unfinished.
