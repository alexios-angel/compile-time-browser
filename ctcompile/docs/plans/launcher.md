# The launcher, and proving the interpreter did not run

Phases 18-20 are the ones that turn `ctcompile` from a set of test binaries into
a thing that runs. Everything before them makes the compiler cover more
JavaScript; this is where a native application is produced, linked, and shown to
be executing compiled bodies rather than bytecode.

**The gap this plan closes is one field.** `function_proto::aot_entry` is what
the runtime consults to decide whether to interpret or to enter native code, and
before this work **nothing in the tree set it except a test that typed the
answers out by hand**. A backend that emits perfect code and never has it
installed is a backend nothing runs.

Everything below was read or measured. Where a claim is a reading rather than a
measurement it says so, and there is a `What could not be verified` section at
the end.

## What landed

`ctest -R ctcompile_launcher`, one test, and the whole of it is new:

| file | what it is |
|---|---|
| `ctcompile/test/launcher.js` | a 2,963-byte JavaScript **application** — its top level does the work |
| `ctcompile/include/ctcompile/AOT/EntryTable.hpp` | the installer: symbol -> `function_proto`, and strict mode |
| `ctcompile/test/aot-entry-table.cmake` | reads the emitted C++, writes the table translation unit |
| `ctcompile/test/LauncherApp.cpp` | the fixed launcher, compiled twice from one source |
| `ctcompile/test/check-launcher.cmake` | runs both arms, compares bytes, asserts the counters |
| `ctcompile/test/extract-inline-script.cmake` | a page's inline script, as `run_scripts` assembles it |
| `ctcompile/test/LauncherPage.cpp` | the same, for a real page through the whole engine |
| `ctcompile/test/check-launcher-page.cmake` | runs both arms, compares the canvas, asserts the counters |
| `browser::set_script_prepared_hook` | +27 lines in `browser.hpp`, +4 in `browser.cpp` |

Measured on the devbox, `-DCTCOMPILE_ENABLE_MLIR=ON`, clang 24.0.0git:

| | |
|---|---|
| functions in the program | **11**, including the top level |
| functions the backend lowered | **11** — nothing refused |
| emitted C++ | 3,167 lines, 79,165 bytes |
| generated entry table | 72 lines, 3,363 bytes |
| the two executables | 1,168,616 bytes interpreted, 1,217,168 compiled (**+48,552**) |
| transcript | byte-identical between the two arms |
| `C++ -> VM` in the compiled arm | **0** |
| `C++ -> AOT` | **1** |
| `AOT -> AOT` | **80** |
| `VM -> AOT`, `AOT -> VM`, `AOT -> C++` | 0 |

**The interpreter never ran.** `run_loop` was not entered once.

And `ctest -R ctcompile_launcher_page`, which is the same question asked of
`ctbrowser/examples/pages/invaders.html` — a game, with a canvas, a sprite
sheet, two event listeners and a `requestAnimationFrame` loop, driven through
the whole engine for twenty fixed 16 ms frames:

| | |
|---|---|
| functions in the page's script | **4** — the top level, `frame`, and the two anonymous listeners |
| functions the backend lowered | **4** — nothing refused |
| the canvas after 20 frames | **byte-identical**, 320x240, 4 distinct colours |
| `C++ -> VM` | **0** |
| `C++ -> AOT` | **21** — the top level, then one per animation frame |
| `AOT -> C++` | **484** — `drawImage`, `fillRect`, `Math.floor`, `requestAnimationFrame` |
| `VM -> AOT`, `AOT -> VM`, `AOT -> AOT` | 0 |

`AOT -> AOT` is zero and that is correct rather than a miss: invaders' four
functions never call one another, only natives. **`AOT -> C++` 484 is the mixed
stack the Definition of Done asks for by name** — compiled JavaScript calling
the engine's C++ built-ins, 484 times, drawing a picture identical to the
interpreted one.

## The crux: how a symbol finds its `function_proto`

This is the question the whole phase turns on, and the answer is that the
mapping was **already being transported and nobody was reading it**.

`ctcompile/lib/CTJS/Import/BytecodeImport.cpp`, in the loop over
`program::functions`:

```cpp
std::string name = proto.name.empty() ? std::string{"fn"} : proto.name;
for (char & c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') { c = '_'; }
}
name += "$" + std::to_string(index);
```

and `ctcompile/lib/CTJS/Lowering/CTJSToEmitC.cpp::c_identifier`:

```cpp
std::string spelled = symbol.str();
for (char & c : spelled) {
    if (c == '$') { c = '_'; }
}
```

`index` there is the loop variable over `from.functions`, so it is exactly the
index into `program::functions`. **The emitted symbol carries both halves of the
identity**: `stepShip_4` says it is the compiled body of function 4 and that
function 4 was called `stepShip`. Verified on the fixture — the eleven symbols
emitted are `_script__0`, `emit_1`, `Ship_2`, `clampTo_3`, `stepShip_4`,
`makeWave_5`, `advance_6`, `scorer_7`, `add_8`, `frame_9`, `main_10`, which is
`program::functions` in order with `<script>` sanitised to `_script_`.

So the installer needs no side file, no serialized metadata and no hash of the
bytecode. `ctcompile::aot::install` rebuilds the same name for every proto in
the program and matches on it.

**That is a second copy of the naming rule and the cost is paid deliberately.**
The alternative is a build-time tool that compiles the source a third time to
emit a metadata table, which is a third copy plus a build edge. What makes the
duplication safe is that a drift is loud rather than silent, and the two
refusals are what make it so:

* `install` refuses when a symbol in the table matches **no function**.
* `install_strict` refuses when a function matches **no symbol**.

Together they are a **bijection**, and a change to either sanitiser turns every
generated application into a startup refusal naming the symbol. This matters
more than it sounds: the failure being avoided is not a crash. It is an
application that starts, prints the right answer, interprets everything, and
reports a truthful zero — which is the failure this project treats as worst
because nothing observes it.

Falsified (see below): breaking the sanitiser makes the application refuse with
`no function in this program is named _script__0`, not run slowly.

## The generated `main`

There is not one, and Phase 18 says there must not be: *"Do not generate C++
source for the launcher and shell out to a host compiler."*

`ctcompile/test/LauncherApp.cpp` is a fixed source compiled twice. Everything
application-specific arrives as **two generated objects and one preprocessor
symbol**:

```cpp
#ifdef CTCOMPILE_LAUNCHER_AOT
extern const ctcompile::aot::entry_table CTCOMPILE_LAUNCHER_TABLE;
#endif

int main() {
    program compiled = compiler::compile(std::string(fixture));
#ifdef CTCOMPILE_LAUNCHER_AOT
    const install_report installed = install_strict(compiled, CTCOMPILE_LAUNCHER_TABLE);
    if (!installed.ok()) { /* refuse */ return 1; }
#endif
    context cx;
    install_builtins(cx);
    reset_transitions();
    cx.run(compiled);
    // the transcript on stdout, the counters on stderr
}
```

The table's NAME is a build parameter rather than a fixed symbol, because an
application with several compiled scripts has one table per script and they
cannot all be called the same thing.

**Installation happens before anything runs, and it must.** `context::execute`
asks `enter_compiled` for the top level *before* it pushes a frame
(`ctbrowser/lib/Script/vm/call.cpp`, the block whose comment reads "A COMPILED
TOP LEVEL, IF THIS PROGRAM HAS ONE, and for ctcompile that is the ordinary case
rather than an exotic one"). That single call is why `C++ -> AOT` is 1 and
`C++ -> VM` is 0: a program whose top level compiled is entered once from C++
and never returns to the interpreter.

## How it is built and linked

Three build edges, all in `ctcompile/test/CMakeLists.txt`, all at build time
rather than checked in — a generated file in the tree is a copy free to disagree
with its generator, and this test's value is that it exercises the *current*
backend.

```text
launcher.js
  |-- embed-js.cmake ------------> launcher.js.inc     (the driver reads the same file)
  |-- compile-js-to-cpp.cmake ---> launcher.generated.cpp
  |                                   ctjs-translate --ctbrowser-js-to-ctjs
  |                                 | ctjs-opt --ctjs-lower-to-emitc
  |                                          --ctjs-drop-uncompiled
  |                                          --emitc-eliminate-block-arguments
  |                                 | mlir-translate --mlir-to-cpp
  |                                                  --declare-variables-at-top
  '-- aot-entry-table.cmake ------> launcher.entries.cpp   (reads the emitted C++)
```

`compile-js-to-cpp.cmake` is reused **unchanged**, with `-DENTRIES=` empty.
Every other caller passes it a list of functions to rename to fixed symbols so
that a driver can *declare* them; an application declares nothing, and the
symbols have to keep the names the backend gave them because that is what
carries the index.

`aot-entry-table.cmake` reads the **emitted C++** rather than the MLIR, and that
is a decision rather than convenience: the definitive list of what a build can
install is the list of symbols that exist in the object file. A function the
backend refused leaves no definition, so a table generated from an earlier stage
would name it and fail at link with an undefined symbol instead of simply being
one row shorter. It matches on the ABI's first parameter —

```cmake
string(REGEX MATCHALL
  "extern \"C\" int32_t [A-Za-z_][A-Za-z0-9_]*\\(ctbrowser::aot::ct_aot_ctx\\*" found "${emitted}")
```

— rather than on `extern "C"` alone, because the backend also emits a
file-scope `static const char ctc_memo_<entry>` per entry and two boxing shims,
and a looser pattern collects those and then writes declarations for them.

Two link edges, from one driver source:

```cmake
add_executable(ctcompile-test-launcher-vm  LauncherApp.cpp launcher.js.inc)
add_executable(ctcompile-test-launcher-aot LauncherApp.cpp launcher.js.inc
                                           launcher.generated.cpp launcher.entries.cpp)
```

Both link `ctbrowser::script` and nothing else. **No MLIR, no LLVM** — the
generated application depends on neither, which is the Definition of Done's
"Shipping applications do not depend on MLIR or LLVM runtime libraries", checked
here by the link rather than asserted.

### A real page, and the one hook it needed

`ctcompile_launcher_page` is the same shape with the whole engine underneath:
`invaders.html` in, its canvas out, compared byte for byte.

It could not be done without a change to `ctbrowser`, and the change is four
lines. A page's classic scripts are compiled inside `browser::run_scripts` and
never leave it — `classic_programs_` is private and only its `size()` is
published — so a backend emitting perfect bodies had nowhere to put them for a
PAGE. The hook is the one `set_navigate_hook` already models:

```cpp
    void set_script_prepared_hook(std::function<void(script::program &, std::string_view)> hook);
```

called in `run_scripts` between the image lookup and `script_->run(running)`:

```cpp
        if (script_prepared_hook_) { script_prepared_hook_(*compiled, text); }
        const script::program & running = *compiled;
        classic_programs_.push_back(std::move(compiled));
```

Three decisions in it are worth stating:

* **Engine types only.** It hands over a `script::program` and knows nothing
  about entry tables, so `ctbrowser` still does not know `ctcompile` exists.
  That is Principle 2 and it is what lets the hook live in the engine at all.
* **After the image lookup**, so the hook sees the program that will actually
  run whether it came from an image or from a compile. A hook that fired only
  on one of those paths would work until the page was packaged.
* **The source TEXT and not the hash**, because an installer has to know which
  script this is, and `script_sources()` is the list it was given.

**The script text has to be byte-exact and it is only safe because it is
checked.** `run_scripts` assembles an inline script as its child text nodes
followed by ONE newline; `extract-inline-script.cmake` reproduces that, and a
copy without the trailing newline is a different program that numbers its
functions differently. Nothing guarantees the reproduction is right — what
guarantees the failure is loud is `install`, which refuses a symbol matching no
function. The extraction refuses outright on a `src`'d script, a module, or a
second `<script>`, because those are the cases where the rule is genuinely more
than "the text between the tags".

**invaders.html was chosen because its whole script lowers.** `ctjs-translate`
imports four functions from it and `ctjs-opt --ctjs-lower-to-emitc` refuses
none, so strict AOT-only is reachable on a real page *today*. Nothing else in
`ctbrowser/examples/pages/` is close — the p5, Phaser and Babylon pages carry
vendored libraries of thousands of functions.

### What this is NOT yet

`ctcompile` the tool still packages a `.ctapp` bundle of **bytecode images** and
appends it to `ctrun`; `--mode aot-only` is refused by name with a message
pointing at Phases 10A-10C, and `check-package.cmake` asserts the manifest says
`mode vm`. Nothing in that path is touched by this work. See `Next rungs`.

## Strict AOT-only, operationally

Phase 19 describes strict mode as a policy over dynamic source processing — no
`eval`, no `new Function`, no `innerHTML`, no runtime CSS text. That is the
right long-term shape and it is not yet checkable, because none of those
surfaces has a policy knob. What *is* checkable today is the JavaScript half,
and it is stated as a rule about a number:

> **An application whose program still holds a function with no compiled body is
> not an AOT-only application, and it refuses to start.**

`install_strict` enforces it, names the offenders, and — importantly — **takes
the entries back off** before returning the refusal, so a caller cannot ignore
the error into exactly the mixed build the rule forbids.

### How a fall back to the interpreter is PROVED not to have happened

Not by the output. Both arms run the same program, so an application that
installed nothing prints identical bytes and exits 0. The transcript comparison
is a control, not the assertion.

The assertion is `ctbrowser/lib/Script/dispatch.cpp`, which counts all six
transitions between C++, the interpreter and compiled code. Three of them are
ways for the interpreter to be reached, and in an AOT-only build every one must
be zero:

| counter | must be | why |
|---|---|---|
| `C++ -> VM` | 0 | `context::execute` counts this only when `enter_compiled` declined the top level |
| `VM -> AOT` | 0 | a compiled body entered *from the interpreter* means the interpreter was running |
| `AOT -> VM` | 0 | a compiled body called something that had no compiled body |
| `C++ -> AOT` | exactly 1 | the whole program is one crossing |
| `AOT -> AOT` | exactly 80 | pinned, not `> 0` — see below |

The page arm asserts the same three zeroes and two different positives, because
a page is driven from C++ rather than from its own top level:

| counter | must be | why |
|---|---|---|
| `C++ -> AOT` | at least 21 | the top level, then one per animation frame |
| `AOT -> C++` | at least 1 | a compiled body reaching `drawImage` — zero means it drew nothing |
| colours on the canvas | at least 3 | two blank canvases compare equal |

**`AOT -> AOT` is pinned rather than bounded on purpose.** `> 0` passes on an
application whose top level compiled and whose every call fell back, which is
the interesting half of the failure. 80 is the fixture's own call count; if
`launcher.js` changes the number changes with it, and the test says so in its
failure message.

**And there is a blinded arm.** The same driver with nothing generated linked
into it must report zero for all four compiled crossings. Without that, a
counter that was broken in the "always counts" direction would make every
assertion above pass for the wrong reason.

## What was falsified

Each mutation was applied, the build was confirmed to have rebuilt, and the test
was run. All four turned it red.

| mutation | result |
|---|---|
| **Install nothing but report a full install** — replace the `install_strict` call with a hand-built `install_report{11, 11, 0, {}}` | `the aot arm crossed C++ -> VM 1 time(s), so the interpreter ran. This is not an AOT-only application.` **The transcripts still matched**, and the install report still said `installed 11 of 11, interpreted 0` — so the counters, and only the counters, caught it. |
| **Leave one body out of the table** — `aot-entry-table.cmake` skips `clampTo_3` | `launcher: strict AOT-only: 1 of 11 functions have no compiled body (clampTo_3)`, exit 1. Named the function, refused to start. |
| **Break the naming rule** — `entry_symbol` sanitises to `x` instead of `_` | `launcher: no function in this program is named _script__0 - the generated bodies and the runtime disagree about how a function_proto is named, or this table is not this program's`, exit 1. |
| **Hand the page hook a COPY of the program** — `script_prepared_hook_(aside, text)` on a local copy, so every entry lands on a program nothing runs | `the aot arm crossed C++ -> VM 21 time(s), so the interpreter ran. This page is not AOT-only.` It still printed `installed 4 across 1 script(s)`, still drew four colours, and **still drew a byte-identical canvas.** |

The first and the last are the ones that matter, and they are the same failure
twice: an application that reports a full install, produces exactly the right
output, and runs entirely interpreted. It is the exact shape of the failure this
whole track exists to make visible, and it passed every check in both files
except the counters.

## One defect found and fixed while writing this

`aot-entry-table.cmake` took the table's declared length from the number of
regex MATCHES rather than from the number of rows it emitted. They are the same
number today because nothing is skipped — but the second falsification above
skipped one, and the table then declared 11 bindings over an array of 10, which
is a read past the end. It now counts the rows.

## Risks

**The pinned `AOT -> AOT` count is a maintenance edge.** Editing `launcher.js`
changes it and the test goes red with a message that says so. That is the
intended trade: a bound that survives edits would also survive the failure.

**The counters are process-global, not per-context.** `dispatch.cpp` keeps them
in a plain `std::array` of statics with a comment saying two contexts on two
threads counting into the same six numbers is "a reporting question, not a
correctness one". Both arms here are single-context single-threaded. A launcher
test that ran two pages would need `reset_transitions()` between them, and a
threaded one would need the counters to become `thread_local` first.

**`install` is linear in bindings times functions.** For 11 it is nothing; for
Phaser's 7,725 it is 60 million string comparisons. The symbols are already
precomputed once into a vector, so the fix is a `std::unordered_map` keyed on
the symbol and it is one line — deliberately not done, because 11 does not
justify a hash table and the day it does is the day a real page is packaged.

**The header lives in the wrong project.** Phase 18 says the launcher is a fixed
precompiled library in `ctbrowser/lib/Application/`, so that a shipped
application never links a compiler-side header. `EntryTable.hpp` is the
installer rather than the launcher and it moves there with it. It is header-only
in `ctcompile/include` today because a new library is a new CMake target in a
file three other branches are editing at the same time.

## Next rungs

**1. ~~A real page~~ — done, see above. What it did NOT do** is package that
page: `ctcompile_launcher_page` builds two ordinary test binaries that read
`invaders.html` off the disk. A packaged application reads it out of its own
bundle, and the two paths meet only when rung 2 exists.

**1b. The old text of this rung, kept because the hook is now engine API and
somebody will want to know why it is shaped this way.**
`browser::load_html` compiles each classic `<script>` into its own
`script::program` inside `run_scripts` and nothing outside can reach it —
`classic_programs_` is private and only its `size()` is published. The insertion
point is exact: `ctbrowser/lib/Shell/browser.cpp`, between

```cpp
        const script::program & running = *compiled;
        classic_programs_.push_back(std::move(compiled));
```

and the `script_->run(running)` on the next line. The smallest hook that works
is the one `set_navigate_hook` already models:

```cpp
    // Called once per classic script with its freshly compiled program and the
    // source text it was compiled from, before it runs. This is where a
    // packaged application stamps its compiled bodies on.
    void set_script_prepared_hook(std::function<void(script::program &, std::string_view)>);
```

`std::string_view` and not the hash, because the installer wants to know WHICH
script this is and `script_sources()` is the list it was given. It takes engine
types only, so `ctbrowser` still does not know `ctcompile` exists. It is what
Phase 18's startup diagram calls "register native entries".

**2. `ctcompile --mode aot-only` as more than a refusal.** The packaging path
appends a bundle to a fixed `ctrun`; an AOT application has to be LINKED, which
means `ctcompile` acquires a linker step it does not have. The two are not the
same product and the plan should stop pretending `--mode` selects between them
with one flag: `--mode vm` writes a bundle, `--mode aot-only` writes an
executable, and they share the probe and the manifest and nothing else.

**3. Modules.** `run_bundle` already refuses a page of `<script type="module">`
because there is no image path into `load_module`. There is no AOT path into it
either, and the refusal should stay until there is.

**4. Phase 20's matrix.** Nothing here is a matrix; it is one cell — compiled
JS, source HTML, one viewport. The four configurations the plan lists cannot be
expanded until (1) exists, because three of them are about a page.

## What could not be verified

**Nothing was measured about speed.** This test asserts that compiled bodies
ran, and says nothing about whether they ran faster. The fixture is 2,963 bytes
and would measure scheduling noise. `ctcompile/docs/baseline/` is where a number
belongs, and there is not one for AOT.

**The generated application was not packaged, shipped or run anywhere but the
build tree.** `check-package.cmake` starts its executable from a different
working directory precisely because that is a different question; neither
launcher test does, because all four arms are ordinary test binaries reading
their input off the disk.

**Only twenty frames of invaders were run, with no input.** Nothing pressed a
key, so `bullet` stays null, `score` stays 0, `playSound` is never called and
the two anonymous listeners — both compiled, both installed — are entered zero
times. What the page test proves about them is that installing them did not
break anything, not that they run compiled. Driving `input_event`s through
`page` would fix that and was not done.

**The page test's frame count and the canvas are not a golden.** They are a
comparison of two runs of the SAME build; nothing here would notice the engine
starting to draw invaders differently, which is what
`ctbrowser/test/golden/` is for.

**`AOT -> C++` is zero in the bare-context arm and that is not evidence of
anything.** `launcher.js` calls no native built-in — `+`, `%` and `typeof` are
ABI helpers rather than JavaScript calls — so it never had a reason to cross.
The page arm crosses it 484 times, which is where the mixed stack is actually
exercised.

**The claim that a function index means the same thing in both tiers rests on
both sides calling `compiler::compile` on the same text.**
`ctjs-translate::import_source` does `compiler::compile(std::string{text})` and
`LauncherApp.cpp` does the same on the embedded copy of the same file. That is
the build graph, not a check. `EntryTable.hpp` says what the bijection cannot
see: a table generated from a DIFFERENT program whose functions happen to have
the same names at the same indices would install silently and wrongly. A
`ct_aot_program_hash` in the table would close it and would need
`image_source_hash` to be reachable from the generator, which is a CMake script
and cannot call it.

**Formatting drift found and NOT fixed.** `tools/format.sh --check` reports
`ctbrowser/lib/DOM/treebuilder.cpp` and `ctbrowser/tools/ctdrive/ctdrive.cpp`
unformatted at `b3bae93`, before any of this work. Reformatting them would put
two unrelated files in this commit and conflict with whoever is editing them.

**No sanitizer build was run.** The suite was run under the `default` preset
only, green at 114/114. `install` writes eleven pointers into a vector it did
not allocate and the generated bodies do the same things `gc_roots` already
stresses, so there is no specific suspicion — only the absence of a run. The
page arm is the one that would most repay an asan run, because it is the first
thing in this project where a compiled body outlives the call that installed it:
`frame` is held by `requestAnimationFrame` and re-entered twenty times.

**The hook was not tested with an IMAGE-loaded program.** It is called after the
image lookup precisely so that it fires on both paths, and only the compile path
was exercised — `browser::add_script_image` was never called by either arm.
