"""Keep constructor prefix captures when sinking terminal Data registration."""

from CTNative.Browser.dom import dom
from CTNative.harness import prepare_js, run
from CTNative.HostContract import contract as host
from CTNative.Lowering.Objects.classes import initialization as classes


def sources():
    vendor = dom.BOOTSTRAP.read_text()
    data = (
        vendor[
            vendor.index("    const t = new Map,") : vendor.index('        i = "transitionend",')
        ]
        .rstrip()
        .removesuffix(",")
        + ";\n"
    )
    source = "function probe() {\n" + data + """
  const add = n => n + 1;
  const twice = n => n * 2;
  class Item {
    constructor(n) { this.n = add(n); e.set("element", "bs.item", this); }
    count() { return Object.getOwnPropertyNames(this).length; }
  }
  const first = new Item(2);
  const saved = e.get("element", "bs.item");
  e.remove("element", "bs.item");
  const second = new Item(7);
  const current = e.get("element", "bs.item");
  e.remove("element", "bs.item");
  return saved.n * 100 + current.n * 10 + saved.count();
}
var a = probe();
"""
    single = source.replace("  const twice = n => n * 2;\n", "")
    programs = {
        "holder": (single, 381),
        "holder-first": (
            single.replace("this.n = add(n)", "const data = e; this.n = add(n)").replace(
                'e.set("element",', 'data.set("element",'
            ),
            381,
        ),
        "two-helpers": (
            source.replace(
                "this.n = add(n)", "const value = add(n); const data = e; this.n = twice(value)"
            ).replace('e.set("element",', 'data.set("element",'),
            761,
        ),
        "direct": (
            single.replace(
                data,
                "const t = new Map();\n"
                "const save = (key, slot, value) => { t.set(key, value); };\n",
            )
            .replace("e.set(", "save(")
            .replace('e.get("element", "bs.item")', 't.get("element")')
            .replace('e.remove("element", "bs.item")', 't.delete("element")'),
            381,
        ),
    }
    for name, (text, expected) in list(programs.items()):
        implicit = name == "holder-first"
        leaf = "" if implicit else "constructor(n) { super(n); this.tag = n + 1; }"
        programs["inherited-" + name] = (
            text.replace("class Item {", "class Base {").replace(
                "  const first", f"  class Item extends Base {{ {leaf} }}\n  const first"
            ),
            expected + (0 if implicit else 1),
        )
    for name in ("holder", "direct"):
        text, expected = programs[name]
        programs["guarded-" + name] = (
            text.replace("constructor(n) {", "constructor(n, enabled) { if (enabled) {")
            .replace("this); }", "this); } }")
            .replace("new Item(2)", "new Item(2, true)")
            .replace("new Item(7)", "new Item(7, 1)"),
            expected,
        )
    text, expected = programs["guarded-holder"]
    programs["guarded-else"] = (
        text.replace("if (enabled) {", "if (enabled) {} else {")
        .replace("new Item(2, true)", "new Item(2, false)")
        .replace("new Item(7, 1)", "new Item(7, 0)"),
        expected,
    )
    for name in ("holder", "direct", "else"):
        text, expected = programs["guarded-" + name]
        programs["helper-guarded-" + name] = (
            text.replace("  class Item", "  const gate = flag => flag;\n  class Item").replace(
                "if (enabled)", "if (gate(enabled))"
            ),
            expected,
        )
    text, expected = programs["helper-guarded-holder"]
    programs["helper-guarded-padded"] = (
        text.replace("flag => flag", "(flag, absent) => absent ? false : flag"),
        expected,
    )
    for name in ("guarded-holder", "guarded-direct", "guarded-else", "helper-guarded-holder"):
        text, expected = programs[name]
        implicit = name == "guarded-else"
        leaf = (
            "" if implicit else "constructor(n, enabled) { super(n, enabled); this.tag = n + 1; }"
        )
        programs["inherited-" + name] = (
            text.replace("class Item {", "class Base {").replace(
                "  const first", f"  class Item extends Base {{ {leaf} }}\n  const first"
            ),
            expected + (0 if implicit else 1),
        )
    return programs


def prepared_source(args, name, text):
    js = args.work / (name + ".js")
    ir = args.work / (name + ".mlir")
    js.write_text(text)
    prepare_js(
        args, js, args.work / (name + ".raw.mlir"), ir, "--mlir-print-op-generic", complete=True
    )
    manifest = dict(
        host.manifest(args.opt, ir),
        initial_intrinsics=["__ctbrowser_class_defined", "Map", "Object"],
    )
    if "extends" in text:
        manifest["initial_intrinsics"] += [
            "__ctbrowser_class_heritage",
            "__ctbrowser_bind_this",
            "__ctbrowser_init_fields",
            "__ctbrowser_super_get",
        ]
    return js, ir, manifest


def main():
    args = dom.parse_args(__doc__, oracles=True)
    executions = refusals = 0
    for label, (text, expected) in sources().items():
        name = "class-map-record-publication-captures-" + label
        js, ir, manifest = prepared_source(args, name, text)
        assert run([args.node, "-e", classes.NODE, str(js)]).stdout == f"a={expected}\n"
        assert run([args.reference, str(js)]).stdout == f"a={expected}\n"
        prepared = classes.prepare(args, name, ir, manifest, success=True)
        for optimize in (False, True):
            native = args.work / f"{name}-{optimize}.native.mlir"
            run(
                [
                    args.opt,
                    str(prepared),
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            executions += classes.check_executable(args, name + str(optimize), native, expected)
            for layout in ("explicit", "deduced"):
                for index in range(len(args.compilers)):
                    binary = args.work / f"{name}{optimize}.{layout}.{index}"
                    assert "ctbrowser::script" not in run([args.nm, "-C", str(binary)]).stdout
        classes.prepare(args, name + "-budget", ir, manifest, success=False, options="max-steps=0")
        refusals += 1
    original = sources()["holder"][0]
    publication = 'e.set("element", "bs.item", this);'
    bad = {
        "changing-helper": original.replace("const add", "let add").replace(
            "  const first", "  add = n => n + 2;\n  const first"
        ),
        "unknown-helper": original.replace("n => n + 1", "n => unknown(n)"),
        "receiver-helper": original.replace("add(n)", "add(this)"),
        "suffix-helper": original.replace(publication, publication + " add(n);"),
        "suffix-observer": original.replace(publication, publication + " this.count();"),
        "replacement": original.replace(publication, publication + " return {};"),
        "second-registration": original.replace(publication, publication + publication),
        "captured-owner": original.replace("this.n = add(n)", "this.n = t.size + add(n)"),
        "binding-write": original.replace("function probe() {", "function probe() { Map = 0;"),
        "prototype-write": original.replace(
            "function probe() {", "function probe() { Map.prototype.set = () => {};"
        ),
        "retained-invalid-helper": original.replace("n => n + 1", "n => n.constructor"),
    }
    for name, text in bad.items():
        assert text != original
        _, ir, manifest = prepared_source(args, name, text)
        classes.prepare(args, name, ir, manifest, success=False)
        refusals += 1
    inherited = sources()["inherited-holder"][0]
    bad = {
        "second-leaf": inherited.replace(
            "  const first", "  class Other extends Base {}\n  const first"
        ),
        "constructed-base": inherited.replace("new Item(2)", "new Base(2)"),
        "leaf-capture": inherited.replace("this.tag = n + 1", "this.tag = add(n)"),
        "leaf-observer": inherited.replace("this.tag = n + 1", "this.tag = this.count()"),
        "leaf-replacement": inherited.replace("this.tag = n + 1;", "return {};"),
        "deeper-family": inherited.replace(
            "class Item extends Base", "class Middle extends Base {}\n  class Item extends Middle"
        ),
        "unknown-prefix": inherited.replace("n => n + 1", "n => unknown(n)"),
        "base-suffix-observer": inherited.replace(publication, publication + " this.count();"),
    }
    for name, text in bad.items():
        assert text != inherited
        _, ir, manifest = prepared_source(args, "inherited-" + name, text)
        classes.prepare(args, "inherited-" + name, ir, manifest, success=False)
        refusals += 1
    guarded = sources()["guarded-holder"][0]
    bad = {
        "mixed-guards": guarded.replace("new Item(7, 1)", "new Item(7, false)"),
        "unknown-guard": guarded.replace("if (enabled)", "if (unknown(enabled))"),
        "guard-helper": guarded.replace("if (enabled)", "if (add(enabled))"),
        "guarded-suffix": guarded.replace(publication, publication + " this.count();"),
        "guarded-replacement": guarded.replace(publication, publication + " return {};"),
        "dropped-class-setup": guarded.replace(
            "this); } }", "this); } else { __ctbrowser_class_defined(undefined); } }"
        ),
        "false-only": guarded.replace("new Item(2, true)", "new Item(2, false)")
        .replace("new Item(7, 1)", "new Item(7, 0)")
        .replace(
            "return saved.n * 100 + current.n * 10 + saved.count();",
            "return (saved === null ? 10 : 0) + (current === null ? 1 : 0);",
        ),
        "captured-constructor": guarded.replace(
            "  const first", "  const make = () => new Item(9, false);\n  const first"
        ).replace("  return saved.n", "  make();\n  return saved.n"),
    }
    for name, text in bad.items():
        assert text != guarded
        _, ir, manifest = prepared_source(args, name, text)
        classes.prepare(args, name, ir, manifest, success=False)
        refusals += 1
    inherited = sources()["inherited-guarded-holder"][0]
    bad = {
        "mixed-guards": inherited.replace("new Item(7, 1)", "new Item(7, false)"),
        "unknown-guard": inherited.replace("if (enabled)", "if (unknown(enabled))"),
        "second-leaf": inherited.replace(
            "  const first", "  class Other extends Base {}\n  const first"
        ),
        "constructed-base": inherited.replace("new Item(2, true)", "new Base(2, true)"),
        "leaf-capture": inherited.replace("this.tag = n + 1", "this.tag = add(n)"),
        "leaf-observer": inherited.replace("this.tag = n + 1", "this.tag = this.count()"),
        "leaf-replacement": inherited.replace("this.tag = n + 1;", "return {};"),
        "base-arm-observer": inherited.replace(publication, publication + " this.count();"),
        "base-after-arm-observer": inherited.replace("this); } }", "this); } this.count(); }"),
        "hidden-base-caller": inherited.replace(
            "  const first", "  const make = () => new Base(9, false);\n  const first"
        ).replace("  return saved.n", "  make();\n  return saved.n"),
        "hidden-leaf-caller": inherited.replace(
            "  const first", "  const make = () => new Item(9, false);\n  const first"
        ).replace("  return saved.n", "  make();\n  return saved.n"),
        "dropped-class-setup": inherited.replace(
            "this); } }", "this); } else { __ctbrowser_class_defined(undefined); } }"
        ),
    }
    for name, text in bad.items():
        assert text != inherited
        _, ir, manifest = prepared_source(args, "inherited-guarded-" + name, text)
        classes.prepare(args, "inherited-guarded-" + name, ir, manifest, success=False)
        refusals += 1
    guarded = sources()["helper-guarded-holder"][0]
    bad = {
        "mixed-helper-guards": guarded.replace("new Item(7, 1)", "new Item(7, false)"),
        "changing-guard-helper": guarded.replace("const gate", "let gate").replace(
            "  const first", "  gate = flag => false;\n  const first"
        ),
        "effectful-guard-helper": guarded.replace("flag => flag", "flag => unknown(flag)"),
        "captured-guard-helper": guarded.replace("flag => flag", "flag => add(flag)"),
        "ambient-undefined-helper": guarded.replace(
            "flag => flag", "(flag, absent) => absent === undefined ? flag : false"
        ),
        "receiver-guard-helper": guarded.replace("flag => flag", "function(flag) { return this; }"),
        "retained-guard-helper": guarded.replace(
            "flag => flag", "flag => flag || unknown(flag)"
        ).replace("  const first", "  gate(false);\n  const first"),
        "helper-dropped-class-setup": guarded.replace(
            "this); } }", "this); } else { __ctbrowser_class_defined(undefined); } }"
        ),
    }
    for name, text in bad.items():
        assert text != guarded
        _, ir, manifest = prepared_source(args, name, text)
        classes.prepare(args, name, ir, manifest, success=False)
        refusals += 1
    print(f"Constructor publication captures: {executions} native executions, {refusals} refusals")


if __name__ == "__main__":
    main()
