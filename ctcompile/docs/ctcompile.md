# `ctcompile` — the command line

```
ctcompile [options] <application-directory>
```

Loads the entry page once with the engine that will run it, compiles every
classic `<script>` to a program image, and writes a single executable carrying
the page, its resources and those images.

```bash
ctcompile app/ -o myapp          # then ./myapp, anywhere
```

## What it produces

A copy of `ctrun` — a fixed launcher this project builds like any other tool —
with an **application bundle** appended and a 24-byte trailer saying where that
bundle starts. Nothing is generated and no linker runs: a linked ELF does not
care what follows its last section. The machine that RUNS the result needs no
toolchain.

With `--bundle` it writes the bundle alone (`.ctapp`), which `ctrun app.ctapp`
will run.

## Options

| option | what it does |
|---|---|
| `-o, --output FILE` | where to write the executable (default: the entry's stem) |
| `--entry FILE` | the page to package, relative to the application directory (default `index.html`) |
| `--manifest FILE` | also write the manifest here, as JSON |
| `--fonts DIR` | where the vendored faces are (default `$CTBROWSER_FONT_PATH`, else `fonts`) |
| `--launcher FILE` | the launcher to build the executable from (default: `ctrun` beside this compiler) |
| `--bundle` | write the `.ctapp` alone instead of an executable |
| `--verbose` | report each stage |
| `-v, --version` | the compiler version and the engine it was built against |

**There is no `--mode`.** The bundle runs the VM, and that is the only thing
packaging can produce today: the native EmitC backend exists and runs whole
applications with no interpreter (`plans/launcher.md`), but packaging is a copy
of `ctrun` with a bundle appended, and an AOT application needs a link step this
tool does not have. A flag naming a mode it cannot deliver was refused by name;
now there is no flag to refuse.

**There is no `--target`.** Nothing here cross-compiles: packaging is a copy of
a launcher, so the target is whatever that launcher was built for. Point
`--launcher` at a launcher built for another platform and the output is for that
platform — subject to the one gap below.

## What it refuses, and why each refusal exists

Every one of these is a case where the alternative is an application that RUNS
and is quietly wrong. That is the failure mode this tool is built around: a
packaged application that compiles its scripts from source produces exactly the
same document as one that does not, only slower.

* **A page that does not load cleanly.** A `<script src>` that fails to resolve
  is recorded by the engine and then that script is simply ABSENT, so every
  later count is clean and the application would ship missing its library.
* **A page with module scripts.** There is no image path into `load_module`, so
  an application built from one would parse all of its JavaScript at every start
  while every count read a truthful zero.
* **A script that does not compile.**

And one thing it only warns about: a resource the page asked for that nothing
answered. The page already tolerated it during the probe load, so refusing would
block a page that works — but a packaged application is SEALED and will not look
on disk, so this is the last chance anyone hears about it.

## The manifest

```json
{
  "ctcompile": "0.1.0",
  "engine": "ctbrowser 2.0.0, 93 bytecode operations",
  "entry": "p5-basic.html",
  "bundle_format": 1,
  "image_format": 3,
  "engine_fingerprint": "0x1b0fb1310f6b5265",
  "font_directory": "fonts",
  "bundle_bytes": 20586148,
  "scripts": [
    { "index": 1, "program_id": "0x96b6372e5c04f32f", "source_bytes": 4574628,
      "image_bytes": 7315192, "functions": 4754 }
  ],
  "resources": [ { "name": "../../vendor/p5/p5.js", "bytes": 4574626 } ]
}
```

`program_id` is the identity the runtime matches images on: the hash of the
source **the engine reported**, which is not the hash of the file — the script
walk appends a newline to every script's text. A resource `name` is whatever the
document said, which is why `p5-basic.html` lists `../../vendor/p5/p5.js`: that
is not a path relative to the application directory and is not something a
packager could invent.

## How it decides what to package

It **asks the engine**, and this is the one design point worth reading. Which
scripts a page compiles and which resources it reaches for are decided by rules
that live in the browser. A packager holding a second copy of those rules is a
packager free to drift from the one that decides at run time, and the drift
presents as an application that quietly compiles from source.

So the page is loaded, headless, by the same browser that will run it, and then
asked: `script_sources()`, `module_sources()`, `assets().requested()`.

**And then it is allowed to RUN.** `fetch` and `img.src` queue their requests
and are drained from a tick; p5 loads in `preload` and Phaser in the first game
step. The probe ticks until the page stops asking for new resources, to a
ceiling of 60 frames — a static page settles after one.

## What a packaged application does at run time

* Finds its bundle by reading its own file (`/proc/self/exe`) and checking the
  trailer.
* Refuses to start if any script had to be compiled from source, or if the page
  has module scripts. A packaged application that is merely slow is a packaging
  bug that nothing else would report.
* Answers every resource from the bundle and **never from the disk**. The
  registry is sealed: without that, a missing resource is answered by whatever
  happens to sit in the user's working directory, under the name this document
  asked for.

## A packaged application can describe itself

```
./myapp --info      # the manifest it carries, as JSON
./myapp --help
```

Those two names are taken from the application, which is a real cost: there is
no way yet for a packaged application to receive arguments of its own, so
nothing is being shadowed today. When there is one, they become a `--ctrun-`
prefix and an explicit `--` separator.

## Measured

p5-basic.html on the devbox, seven runs each, whole-process wall clock including
startup and rendering a frame:

| | ms |
|---|---|
| `ctbrowse p5-basic.html`, reading the JavaScript | **78.0** |
| the packaged executable, run from `/tmp` | **47.3** |

Reading JavaScript is about 40% of a page load and executing it is 1.4%, so this
is what deleting the parse is worth. **This path generates no native code** — the
bytecode still runs on the interpreter; the native backend and what it runs are
in `plans/launcher.md`.

## Gaps

* **Windows.** Finding the appended bundle uses `/proc/self/exe`, so a packaged
  application on Windows finds nothing and prints usage. The cross build exists;
  this half of it does not.
* **A `<script src>` ships its source twice** — once as the resource the runtime
  must re-read to reproduce the hash, and once inside the image, which keeps the
  source so `f.toString()` works. For p5 that is 4.5 MB each way.
