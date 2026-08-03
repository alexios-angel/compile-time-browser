# ES modules: running ordinary JavaScript, with nothing shimmed

**Goal, stated as the user did: take regular JavaScript and use it. No shims, no
"shim-like things".** Today this engine runs one flavour of JavaScript — the
classic script — and Babylon is the corpus that proves the gap.

**Status: planned, not started.** Measured first, as the lexer, Phaser and
WebGL 2 plans were.

## What exists today: nothing, and the architecture is the reason

Measured, not assumed:

* **`import` and `export` are not keywords in ctjs.** They are absent from the
  keyword table in `vparse.hpp` entirely — not refused by name, not parsed and
  dropped. Absent.
* **The compiler has no module concept**, so there is nothing downstream either.
* **`<script type="module">` is not looked at.** `browser::run_scripts` selects
  every HTML `<script>` and does not read `type`.
* **And the blocking one:**

```cpp
// browser::run_scripts, paraphrased
std::string source;
for (each <script>) { source += its src's bytes or its text; source += '\n'; }
script_program_ = compile(source);
script_->run(*script_program_);
```

**Every script on the page is concatenated into ONE string and compiled as ONE
program.** That is correct and cheap for classic scripts, which genuinely do
share one global scope. It is not a thing modules can be bent into: a module has
its own scope, its own binding namespace, and an execution *order* derived from
a dependency graph. This is the architectural change; the syntax is the easy
half.

## Why Babylon is the right corpus for it

`tests/webgl2-ratchet.txt` records where rung 10 stops. The vendored UMD build
opens with:

```js
function _BabylonUMDDynamicImportUnsupported() {
  return Promise.reject(new Error("Dynamic import of a module specifier is not
  supported in the UMD/global Babylon build; use the ES module build"))
}
```

It asks for `StandardMaterial`'s shader body by `import()` and **rejects its own
request**, so the vertex shader arrives as 1388 bytes of `#define` lines with no
body, and 119 draws a frame paint nothing.

That is not a WebGL bug and cannot be fixed in the WebGL code. It is the corpus
telling us it wants modules.

**And it is measured**: `@babylonjs/core@9.19.0` is **9,878 files, 65.9 MB,
`"type": "module"`**. A full ES Babylon is ten thousand modules, which is what
the loader has to be designed against — not a handful.

## What has to be built

### 1. Syntax, in ctjs

Every form, because "no shims" means the page is not rewritten to avoid one:

```js
import defaultExport from "./mod.js";
import { a, b as c } from "./mod.js";
import * as ns from "./mod.js";
import "./side-effect.js";
export const x = 1;  export function f() {}  export class C {}
export { a, b as c };  export default expr;
export * from "./mod.js";  export { x } from "./mod.js";
import.meta.url
await import("./lazy.js")        // dynamic, and the one Babylon needs
```

`import`/`export` become keywords; `from`, `as` and `meta` are contextual. ctjs
already carries contextual keywords (`of`, `async`, `get`, `set`), so the shape
exists.

### 2. Module records and linking, in the VM

The hard half, and the part where a shortcut becomes a shim:

* **A module is a scope, not a program.** Each gets its own bindings; the global
  object is shared but the top level is not.
* **Imports are LIVE bindings, not copies.** `import { count } from "./c.js"`
  followed by the exporter reassigning `count` must be visible to the importer.
  Copying the value at link time is the CommonJS behaviour and is observably
  wrong — and it is exactly the kind of "works for most pages" shortcut this
  plan exists to refuse.
* **Cycles must work.** A imports B which imports A is legal and Babylon has
  them. Bindings are created before evaluation (hoisted), so a cycle sees an
  uninitialised binding rather than a missing one — a TDZ error, not a crash.
* **Order is depth-first post-order** over the graph, each module evaluated
  once. `heap_kind::coroutine` and the promise machinery already exist for
  `await`, which is what top-level await will need.

### 3. The loader, in shell

* `<script type="module">` is **deferred by default** and executed after the
  document parses, in order — different from a classic script, which runs where
  it sits.
* **Specifier resolution.** Relative (`./x.js`, `../y.js`) against the importing
  module's URL — `shell/url.cpp` resolves URLs already, though see
  `docs/ada-url-plan.md` about which standard it resolves them by. Bare
  specifiers (`import "@babylonjs/core"`) need **import maps**, which is a small
  JSON document in the page and is the standards-track answer rather than a
  bundler.
* **Transitive fetch, and the 9,878 number is the design input.** Fetch each
  module once, cache by resolved URL, fetch a module's dependencies in parallel.
  The current path is `assets_.load(url)` — synchronous, in-memory or beside the
  page, no network. Modules are asynchronous by nature.
* **Dynamic `import()` returns a Promise**, resolving to the namespace object.
  This is the single call Babylon's UMD build needs.

### 4. What must NOT be done

* **No bundler step**, no "vendor a prebuilt single file and hope". That is the
  shim.
* **No `require` shim**, no synchronous fake of `import()`.
* **No rewriting the corpus.** If Babylon needs an import map, the page carries
  one, because that is what a page carries in a browser.

## Staging

### 0 — a ratchet, before any of it
`tests/module_ratchet.cpp` + `tests/module-ratchet.txt`, the shape used three
times now: 1 `import`/`export` parse, 2 a two-module program runs, 3 live
bindings observed, 4 a cycle resolves, 5 `<script type="module">` on a page,
6 relative specifiers, 7 dynamic `import()`, 8 an import map, 9 Babylon's ES
build boots. Its first reading is the scoping measurement, and it will read 0.

### 1 — syntax only
ctjs parses every form above and the compiler REFUSES them by name, the way
`yield` was refused before generators. Nothing runs yet; the parse corpus grows.

### 2 — one module, no imports
A `<script type="module">` with no dependencies runs in its own scope. Proves
the execution path without the graph.

### 3 — the graph
Static imports, live bindings, cycles, post-order evaluation. Multiple modules
from the asset registry, no network.

### 4 — the loader
Fetching, caching, parallel dependency fetch, import maps, dynamic `import()`.

### 5 — Babylon's ES build
Which is rung 9 of the module ratchet and rung 10 of the WebGL 2 one.

## Verification

* **Classic scripts must not change.** p5 12/12 and Phaser 10/10 are both
  classic-script corpora; if either moves, the concatenation replacement is
  wrong. This is the main regression risk of the whole plan.
* The thirteen goldens byte-identical, on Linux and the Windows cross-build.
* `tools/compare.py` against Chrome for module ORDER, which is observable and
  easy to get subtly wrong.

## The honest cost

This is the largest single item in the tree's backlog — bigger than WebGL 2 was.
It touches the parser, the compiler, the VM's scoping, the shell's script
pipeline and the network layer, and it replaces an architectural decision
(concatenate everything) rather than extending one.

The argument for doing it is that it is the difference between running
JavaScript and running *a dialect of JavaScript that happens to work*. Every
corpus added from here — and Babylon today — ships as modules first and a UMD
bundle as a courtesy. That courtesy is what this engine currently depends on.
