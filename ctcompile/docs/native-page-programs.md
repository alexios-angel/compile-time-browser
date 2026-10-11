# Native page programs: the bundle probe's native side

`tools/check/check-workload.py`'s script-run stage checks a library such as
`bootstrap.bundle.js` by behaviour, not by its globals. The bundle publishes a
single object, `bootstrap`, and the reference prints only primitive globals, so
a globals comparison would compare nothing. Instead, a probe calls the
library's components on the elements of real pages and logs the DOM state the
calls changed. The VM runs the probe, a native program runs the same probe, and
the two outputs must match on every page.

This page describes how the native program is assembled and run. The VM side
is `ctcompile-test-native-reference --page PAGE --script LIB --script PROBE`.
That command loads the page into the shell's browser, runs each script in the
page's own context, and prints the page's `console.log` lines.

## Inputs

A workload file names the probe on the script it checks:

```json
{"pattern": "ctbrowser/vendor/bootstrap/bootstrap.bundle.js",
 "script-run": {"probe": "tools/check/workloads/bootstrap-probe.js"}}
```

The pages are the workload's `pages` (`ctbrowser/examples/pages/bootstrap-*.html`).

The probe (`tools/check/workloads/bootstrap-probe.js`) is ordinary JavaScript
in named functions (`bootstrap_probe_*`). For every element it toggles a
Button, toggles a Collapse, toggles a Dropdown or closes an Alert, then logs one
line: the tag, the class list, the sorted `aria-*` attributes and
`style.display`. It logs exactly one String per `console.log` call. On
2026-10-11 the VM printed 64 lines over the six pages. Three of those pages
have no components and print only zero counts.

## Assembling the program

`page_program()` in `check-workload.py`'s script-run stage does this.

1. **One program.** The program is written to
   `<build>/check-workload-page-programs/<stem>.js` as the library's text,
   then `;`, then the probe's text. This is one classic-script program where
   the VM ran two classic scripts in one global scope. The only difference is
   hoisting: the probe's function declarations exist before the library runs.
   The library never names them, so nothing observable changes.
2. **Lowering.** The program goes through the same native pipeline as every
   fixture, `ctcompile/test/CTNative/Checks/pipeline.cmake`, then
   `ctjs-translate --mlir-to-cpp`. A refused function fails this step. A real
   check-workload run never allows a refusal, so the page program is native
   only when every function of the library and of the probe is native.
3. **Wrapping.** The generated `.cpp` is compiled through
   `tools/check/workloads/page-program-main.cpp`. The wrapper is built with
   `-DCTCOMPILE_PAGE_PROGRAM="<generated .cpp>"`. It includes `Page.hpp` with
   `CTNATIVE_DOM`, includes the generated file with its `main` renamed to
   `ctcompile_page_program_top_level`, and defines the real `main`.
4. **Building.** The program is built as build-native builds a DOM script
   (`dom_toolchain()`):
   - GCC and the build's DOM Clang (`%dom_clang`, never a fallback compiler);
   - the harness flags plus Boost and simdutf includes;
   - the link line CMake computes for `ctcompile-test-native-page`, which is
     the page library and everything under it.

## What the native `main` does, per page

`page-program-main PAGE.html`:

1. Reads the page and creates a `ctbrowser::page::static_page` at the VM
   reference's window size, 400 x 300.
2. Sets the asset base path to the page's directory, unconfined, as
   `bootstrap_layout` does.
3. Calls `load_html` and `frame()`.
4. Binds the page with `ctnative::page_scope`. Generated code then reaches
   `document`, its elements and the page views through
   `document_scope` / `page_scope::active()`.
5. Runs the program's top level: the library's statements, then the probe's.
   The probe prints its lines on stdout.
6. Returns the top level's status.

script-run runs each binary (GCC and Clang) on each page. It requires exit 0
and stdout equal, line for line, to the VM's `console.log` lines for that page.
A difference reports the first differing lines.

## Runtime pieces the program needs

None of these exist natively yet. The admission lanes reach them in this order.

| Need | Used for | Owner |
|---|---|---|
| `console.log(String)` printing one line on stdout, as the VM formats a String argument | the probe's output | native runtime (unassigned) |
| `document` bound to the page; `querySelectorAll` (with `:not()`), `tagName`, `className`, `attributes` (indexing, `name`, `value`), `style.display` | the probe's reads; Bootstrap's own reads | `Runtime/Browser.hpp` and `Page.hpp` (author-2) |
| `window`/`globalThis` as the UMD wrapper's `this`, and the `bootstrap` publication | the bundle's top level | admission (top level; today it is refused, "uses `this`") |
| `EventHandler.on`/`off` and `addEventListener` tables | component construction (Alert and Collapse listen for transitions) | `Runtime/Events.hpp` (author-8) |
| `setTimeout` and the transition-end fallback | Collapse and Alert schedule their completion | `Runtime/Timers.hpp` (author-8). The probe prints before any timer runs, and the VM runs none during `run_script`, so the native host must not drain timers before the top level returns. |
| The instance `Data` map, `getOrCreateInstance` and the static methods | every component | class statics (author-1) and Map runtime |
| Popper | `Dropdown.toggle` | admission of the bundled Popper |

## Dry run and DONE

- **The real `bootstrap` workload.** script-run never reaches the page program
  until admission passes, so the item is BLOCKED.
- **When it runs.** The page program must lower with no refusal, and every page
  must match with both compilers, before the item can PASS.
- **`bootstrap-dry-run`.** This hidden workload sets script-run's
  `"dry_run_allow_refused": true`. The program is then lowered with
  `-DALLOW_REFUSED=TRUE`, its refused functions are deleted, and an empty
  `main` is added when the top level was refused. The whole chain is exercised
  today: lowering, wrapping, both compilers, the page libraries, one run per
  page. Its `assume-admitted` stage always records a MISSING item, so a dry run
  is never DONE.
- **Dry run on 2026-10-11** (21 of 574 bundle functions native). The page
  program builds with g++-13 and the DOM Clang (3.2 MB each). On
  `bootstrap-box.html` it prints nothing where the VM prints four lines, because
  the top level is refused. That is the expected failure until admission
  reaches the top level.
