//===- ClassTransactions.cpp - Class/DOM preparation rollback tests -------===//
//
// Runs prepareDOMEntry on many small class-based DOM programs and checks that
// each one either proves or refuses atomically: a refusal must leave the
// module and the HostContract request exactly as they were.
//
//===----------------------------------------------------------------------===//

#include "ClassTransactions.hpp"
#include "../../../../lib/CTNative/HostContract/Preparation.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "llvm/ADT/DenseSet.h"

namespace ctcompile::test::exception_recovery {

bool testClassTransactions(mlir::MLIRContext & context) {
    // Each `control` value selects one JavaScript variant of `guarded`. The
    // variants come in ranges that start at 0, 5, 8, 13, 19, 25, 35, 46, 70, 82,
    // 92, 104, 116, 128, 140, 151, 157, 165, 172, 181 and 189. Each range
    // rebuilds or edits `source`, and later values in a range add one construct
    // the class/DOM proof must refuse. Controls 197-200 corrupt the imported IR
    // directly. A variant listed in admittedControls must prove. Every other
    // variant must refuse and leave the module and the HostContract request
    // exactly as they were, including class metadata a late refusal had
    // already consumed.
    for (const auto provider : {ctnative::HostContract::Provider::ctbrowserDOM,
                                ctnative::HostContract::Provider::ctbrowserDOMSession}) {
        for (unsigned control = 0; control < 201; ++control) {
            std::string source =
                control >= 5
                    ? "function guarded(element) { class Shape { "
                      "constructor(value) { this.element = value; } "
                      "read() { return this.element.getAttribute('x') === null; } " +
                          std::string(
                              control == 6
                                  ? "unused() { this.element = {}; return this.read(); } "
                                  : "unused() { return this.element.hasAttribute('x'); } ") +
                          "} const shape = new Shape(element); " +
                          std::string(control == 7 ? "const other = new Shape({}); " : "") +
                          "return shape.read(); }"
                    : "function guarded(element) { class Shape { constructor() { this.key = 'x'; } "
                      "read() { return this.key; } } const shape = new Shape(); " +
                          std::string(control == 1 ? "element.unknown(); " : "") +
                          "return element.getAttribute(shape.read()) === null; }";
            if (control >= 8) {
                source = "function guarded(element) { class Shape { "
                         "constructor(value) { this.element = value; } "
                         "read(key) { return this.element.getAttribute(key); } " +
                         std::string(control == 10
                                         ? "unused(target) { return target.getAttribute('x'); } "
                                         : "") +
                         "} const shape = new Shape(element); const first = shape.read('x'); " +
                         std::string(control == 11 ? "const other = new Shape(element); " : "") +
                         std::string(control == 12 ? "shape.element = {}; " : "") +
                         "return shape.read(" + std::string(control == 9 ? "{}" : "'x'") +
                         ") === first; }";
            }
            if (control >= 13) {
                source = "function guarded(element) { class Shape { "
                         "constructor(value) { this.element = value; } "
                         "read(key) { return this.element.getAttribute(key); } "
                         "forward(key) { return this.read(" +
                         std::string(control == 14 ? "{}" : "key") +
                         "); } press(key) { return this.forward(key); } "
                         "} const shape = new Shape(element); " +
                         std::string(control == 15 ? "shape.element = {}; " : "") +
                         std::string(control == 16 ? "const other = new Shape(element); " : "") +
                         (control == 18 ? "return element.hasAttribute('x'); }"
                                        : "return shape.press('x') === null; }");
            }
            if (control >= 19) {
                source = "function guarded(element) { class Shape { "
                         "constructor(value) { this.element = value; } "
                         "static get DefaultType() { return 'x'; } "
                         "read(key = this.constructor.DefaultType) { "
                         "return this.element.getAttribute(key); } "
                         "forward(key) { return this.read(key); } " +
                         std::string(control == 22 ? "unused(key = 'x') { return this.read(key); } "
                                                   : "") +
                         "} const shape = new Shape(element); return shape.forward(" +
                         std::string(control == 20   ? "null"
                                     : control == 23 ? "void 0"
                                                     : "") +
                         ") === null; }";
                if (control == 21 || control == 24) {
                    const auto at = source.find("key = this.constructor.DefaultType");
                    source.replace(at, std::string("key = this.constructor.DefaultType").size(),
                                   control == 21 ? "key = {}" : "key = element.unknown()");
                }
            }
            if (control >= 25) {
                source = "function guarded(element) { class Shape { "
                         "static get NAME() { throw new Error('unused NAME'); } "
                         "constructor(value) { this.element = value; } "
                         "read() { return this.element.getAttribute('x'); } "
                         "} const shape = new Shape(element); " +
                         std::string(control == 30 ? "Shape.NAME; " : "") +
                         std::string(control == 31 ? "element.unknown(); " : "") +
                         "return shape.read() === null; }";
                if (control == 27) { source += " Error = 9;"; }
                if (control == 34) {
                    source.insert(source.find("return shape.read()"), "Error = 9; ");
                }
                if (control == 28) { replaceOnce(source, "throw new", "return new"); }
                if (control == 29) { replaceOnce(source, "'unused NAME'", "unknown()"); }
            }
            if (control >= 35) {
                source =
                    "function guarded(element) { class Shape { "
                    "static get NAME() { throw new Error('unused NAME'); } "
                    "constructor(value) { this.element = value; } "
                    "read() { return Number(this.element.getAttribute('x')).toString(); } " +
                    std::string(control == 45
                                    ? "unused() { this.element.setAttribute('leak', Number); } "
                                : control == 38 ? "unused() { return Number; } "
                                : control == 39 ? "unused() { Number = 9; } "
                                : control == 44 ? "unused() { Number({}); } "
                                                : "") +
                    "} const shape = new Shape(element); " +
                    std::string(control == 40   ? "Number = 9; "
                                : control == 41 ? "element.unknown(); "
                                                : "") +
                    "return shape.read() === '0'; }";
                if (control == 42) { source += " Number = 9;"; }
            }
            if (control >= 46) {
                source = R"js(function guarded(element) {
                    function M(t) {
                        if ("true" === t) return !0;
                        if ("false" === t) return !1;
                        if (t === Number(t).toString()) return Number(t);
                        if ("" === t || "null" === t) return null;
                        if ("string" != typeof t) return t;
                        try { return JSON.parse(decodeURIComponent(t)) }
                        catch (e) { return t }
                    }
                    class Shape {
                        constructor(value) { this.element = value; }
                        read() { return this.element.getAttribute('x'); }
                    }
                    const shape = new Shape(element);
                    return typeof M(shape.read()) === 'object';
                })js";
                if (control == 46) {
                    source.replace(source.find("function M"),
                                   source.find("class Shape") - source.find("function M"),
                                   "function M(t) { return Number(t).toString(); } ");
                }
                if (control == 49) { source += " JSON = 9;"; }
                if (control == 50) {
                    replaceOnce(source, "return t }", "element.unknown(); return t }");
                }
                if (control == 51) {
                    source.insert(source.find("return typeof M"), "if (false) element.unknown(); ");
                }
                if (control == 52) { replaceOnce(source, "M(shape.read())", "M({})"); }
                if (control == 53) {
                    source.insert(source.find("class Shape"),
                                  "const unused = { bad() { Number({}); } }; ");
                }
                if (control == 54) { replaceOnce(source, "M(shape.read())", "shape.read()"); }
                if (control == 56) {
                    replaceOnce(source, "M(shape.read())",
                                "M(shape.read() === null ? '%7B%7D' : '%')");
                }
                if (control == 57) {
                    source.replace(source.find("function M"),
                                   source.find("class Shape") - source.find("function M"),
                                   "function M(t) { function identity(value) { return value; } "
                                   "return Number(identity(t)).toString(); } ");
                }
                if (control >= 58) {
                    replaceOnce(source, "M(shape.read())", "shape.read()");
                    const std::string read = "return this.element.getAttribute('x');";
                    replaceOnce(source, read, "return M(this.element.getAttribute('x'));");
                    if (control == 58) {
                        source.replace(source.find("function M"),
                                       source.find("class Shape") - source.find("function M"),
                                       "function M(t) { return Number(t).toString(); } ");
                    }
                    if (control == 60) {
                        source.replace(
                            source.find("M(this.element.getAttribute('x'))"), 33,
                            "M(this.element.getAttribute('x') === null ? '%7B%7D' : '%')");
                    }
                    if (control == 61) {
                        source.insert(source.find("const shape"),
                                      "M = function(t) { return t; }; ");
                    }
                    if (control == 62) {
                        source.insert(source.find("read()"), "unused() { M = 9; } ");
                    }
                    if (control == 63 || control == 64) {
                        source.insert(source.find(control == 63 ? "return M(this" : "read()"),
                                      control == 63
                                          ? "this.element.setAttribute('leak', M); "
                                          : "unused() { this.element.setAttribute('leak', M); } ");
                    }
                    if (control == 65) {
                        source.insert(source.find("read()"),
                                      "unused() { M(this.element.getAttribute('x')); "
                                      "this.element.unknown(); } ");
                    }
                    if (control == 66) { replaceOnce(source, "return t }", "return unknown(t) }"); }
                    if (control == 67) {
                        source.replace(source.find("function M"),
                                       source.find("class Shape") - source.find("function M"),
                                       "function M(t) { function identity(value) { return value; } "
                                       "return Number(identity(t)).toString(); } ");
                    }
                    if (control == 68) {
                        source.insert(source.find("return typeof"), "element.unknown(); ");
                    }
                }
            }
            if (control >= 70) {
                source = R"js(function guarded(element) {
                    function F(t) { return t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`) }
                    class Shape { read(key) { return F(key); } }
                    const shape = new Shape();
                    return shape.read('config') === 'config' && element.getAttribute('x') === null;
                })js";
                if (control == 71) {
                    source.insert(source.find("return shape"), "shape.read('toggle'); ");
                }
                if (control == 72) {
                    replaceOnce(source, "shape.read('config')", "shape.read('Config')");
                }
                if (control == 73 || control == 74) {
                    source.insert(source.find("return shape"),
                                  control == 73 ? "shape.read('Config'); "
                                                : "shape.read(element.getAttribute('x')); ");
                }
                if (control == 75) { replaceOnce(source, "t.toLowerCase()", "unknown(t)"); }
                if (control == 76) {
                    source.insert(source.find("const shape"), "F = function(t) { return t; }; ");
                }
                if (control == 77) {
                    source.insert(source.find("read(key)"), "unused() { return F('Config'); } ");
                }
                if (control == 78) { replaceOnce(source, "return F(key)", "return F"); }
                if (control == 79) { source += " __ctbrowser_regexp = 9;"; }
                if (control == 80) {
                    source.insert(source.find("return shape"), "element.unknown(); ");
                }
            }
            if (control >= 82) {
                source = R"js(function guarded(element) {
                    function datasetKeys(t) {
                        return Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig")).length;
                    }
                    class Shape {
                        constructor(value) { this.element = value; }
                        read() { return datasetKeys(this.element); }
                    }
                    const count = new Shape(element).read();
                    return 0 < count && count < 2;
                })js";
                const std::string predicate =
                    "t => t.startsWith(\"bs\") && !t.startsWith(\"bsConfig\")";
                if (control >= 83 && control <= 87) {
                    const std::string changed =
                        control == 83   ? "t => unknown(t)"
                        : control == 84 ? "t => t === element"
                        : control == 85 ? "(t, i) => i === 0"
                        : control == 86
                            ? "function(t) { return this; }"
                            : "t => { function hidden() { return t; } return hidden(); }";
                    replaceOnce(source, predicate, changed);
                }
                if (control == 88) {
                    source.insert(
                        source.find("read()"),
                        "unused() { datasetKeys(this.element); this.element.unknown(); } ");
                }
            }
            if (control >= 92) {
                source = R"js(function guarded(element) {
                    const holder = {
                        keys(t) { return Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig")).length; },
                        read(t, key) { return t.getAttribute(key); }
                    };
                    class Shape { read() { return 'x'; } }
                    const shape = new Shape();
                    const count = holder.keys(element);
                    const saved = holder.read(element, shape.read());
                    return 0 < count && count < 2 && saved === null;
                })js";
                if (control == 93) {
                    source.insert(source.find("keys(t)"), "unused() { return 'safe'; }, ");
                }
                if (control == 94) {
                    source.insert(source.find("keys(t)"), "unused(t) { return unknown(t); }, ");
                }
                if (control == 95) {
                    source.insert(source.find("keys(t)"), "unused() { return String({}); }, ");
                }
                if (control == 96) {
                    source.insert(source.find("const count"), "holder.keys({}); ");
                }
                if (control == 97) {
                    source.insert(source.find("const count"),
                                  "holder.keys = function(t) { return 1; }; ");
                }
                if (control == 98) {
                    source.insert(source.find("const count"),
                                  "element.setAttribute('leak', holder); ");
                }
                if (control == 99) { replaceOnce(source, "t.startsWith(\"bs\")", "unknown(t)"); }
                if (control == 100) {
                    source.insert(source.find("const count"), "holder.keys(element); ");
                }
            }
            if (control >= 104) {
                source = R"js(function guarded(element) {
                    function M(t) {
                        if ("true" === t) return !0;
                        if ("false" === t) return !1;
                        if (t === Number(t).toString()) return Number(t);
                        if ("" === t || "null" === t) return null;
                        if ("string" != typeof t) return t;
                        try { return JSON.parse(decodeURIComponent(t)) }
                        catch (e) { return t }
                    }
                    function F(t) { return t.replace(/[A-Z]/g, t => `-${t.toLowerCase()}`) }
                    const H = { getDataAttribute: (t, e) => M(t.getAttribute(`data-bs-${F(e)}`)) };
                    class Shape { read() { return 'x'; } }
                    const shape = new Shape();
                    element.setAttribute('data-bs-config', element.getAttribute(shape.read()) === null ? '%7B%7D' : '%');
                    return typeof H.getDataAttribute(element, 'config') === 'object';
                })js";
                if (control == 105) {
                    source.insert(source.find("return typeof"),
                                  "H.getDataAttribute(element, 'config'); ");
                }
                if (control == 106 || control == 107) {
                    source.insert(source.find("class Shape"),
                                  control == 106 ? "M = function(t) { return t; }; "
                                                 : "F = function(t) { return t; }; ");
                }
                if (control == 108) { replaceOnce(source, "t.toLowerCase()", "unknown(t)"); }
                if (control == 109) {
                    source.insert(source.find("return typeof"),
                                  "element.setAttribute('leak', H); ");
                }
                if (control == 110) {
                    source.insert(source.find("getDataAttribute:"), "unused(t) { return M(t); }, ");
                }
                if (control == 114) {
                    source.insert(source.find("return typeof"),
                                  "H.getDataAttribute(element, 'Config'); ");
                }
            }
            if (control >= 116) {
                const std::string oldClass = "class Shape { read() { return 'x'; } }";
                replaceOnce(source, oldClass,
                            "class Shape { read(element, key) { "
                            "return H.getDataAttribute(element, key); } }");
                replaceOnce(source, "element.getAttribute(shape.read())",
                            "element.getAttribute('x')");
                replaceOnce(source, "typeof H.getDataAttribute(element, 'config')",
                            "typeof shape.read(element, 'config')");
                if (control == 117) {
                    source.insert(source.find("return typeof"), "shape.read(element, 'config'); ");
                }
                if (control == 118) {
                    replaceOnce(source, "const H", "let H");
                    source.insert(source.find("const shape"), "H = {}; ");
                }
                if (control == 119) {
                    source.insert(source.find("const shape"),
                                  "H.getDataAttribute = function(t, k) { return null; }; ");
                }
                if (control == 120) {
                    source.insert(source.find("return H.getDataAttribute"),
                                  "element.setAttribute('leak', H); ");
                }
                if (control == 121) {
                    source.insert(source.find("getDataAttribute:"), "unused() { return 'x'; }, ");
                }
                if (control == 122) {
                    source.insert(source.find("return typeof"), "shape.read(element, 'Config'); ");
                }
                if (control == 123) { replaceOnce(source, "t.toLowerCase()", "unknown(t)"); }
                if (control == 126) {
                    source.insert(source.find("return typeof"), "shape.read({}, 'config'); ");
                }
                if (control == 127) {
                    source.insert(source.find("const shape"),
                                  "H.later = function(t) { return null; }; ");
                }
            }
            if (control >= 128) {
                source = R"js(function guarded(element) {
                    function M(t) {
                        if ("true" === t) return !0;
                        if ("false" === t) return !1;
                        if (t === Number(t).toString()) return Number(t);
                        if ("" === t || "null" === t) return null;
                        if ("string" != typeof t) return t;
                        try { return JSON.parse(decodeURIComponent(t)) }
                        catch (e) { return t }
                    }
                    const H = { read(t, key) {
                        const count = Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig")).length;
                        const saved = M(t.getAttribute(key));
                        return 0 < count && count < 2 && typeof saved === 'object';
                    } };
                    class Shape { read() { return 'x'; } }
                    const shape = new Shape();
                    return H.read(element, shape.read());
                })js";
                if (control == 129 || control == 130) {
                    const std::string original = "read() { return 'x'; }";
                    replaceOnce(source, original, "read(t, key) { return H.read(t, key); }");
                    replaceOnce(source, "H.read(element, shape.read())",
                                "shape.read(element, 'x')");
                }
                if (control == 130) {
                    source.insert(source.find("return shape.read"), "shape.read(element, 'x'); ");
                }
                if (control == 131) { replaceOnce(source, "t.startsWith(\"bs\")", "unknown(t)"); }
                if (control == 132) {
                    source.insert(source.find("const saved"), "t.setAttribute('leak', M); ");
                }
                if (control == 133) {
                    source.insert(source.find("class Shape"), "M = function(t) { return t; }; ");
                }
                if (control == 134) {
                    source.insert(source.find("return H.read(element"), "H.read({}, 'x'); ");
                }
                if (control == 135) {
                    source.insert(source.find("read(t, key)"), "unused(t) { return M(t); }, ");
                }
                if (control == 136) {
                    const std::string predicate =
                        "t => t.startsWith(\"bs\") && !t.startsWith(\"bsConfig\")";
                    replaceOnce(source, predicate, "function(t) { return this; }");
                }
            }
            if (control >= 140) {
                source = R"js(function guarded(element) {
                    const H = { read(t) {
                        const result = {};
                        const keys = Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"));
                        for (const n of keys) {
                            result[n] = t.dataset[n];
                        }
                        return result;
                    } };
                    class Shape { constructor() { this.key = 'x'; } }
                    const shape = new Shape();
                    return typeof H.read(element) === 'object' && element.hasAttribute(shape.key);
                })js";
                if (control == 141 || control == 149 || control == 150) {
                    source.insert(source.find("return typeof"), "H.read(element); ");
                }
                if (control == 149) {
                    source.insert(source.find("return typeof"), "element.unknown(); ");
                }
                if (control == 150) {
                    replaceOnce(source, "return typeof H.read(element)",
                                "return typeof H.read({})");
                }
                if (control == 142) {
                    source.insert(source.find("for (const"),
                                  "t.setAttribute('data-bs-new', 'x'); ");
                }
                if (control == 143) { replaceOnce(source, "t.dataset[n]", "t.dataset[n + '']"); }
                if (control == 144) { replaceOnce(source, "result[n]", "result[{}]"); }
                if (control == 145) {
                    source.insert(source.find("result[n]"), "result[n] = null; ");
                }
                if (control == 148) {
                    const std::string constructor = "constructor() { this.key = 'x'; }";
                    replaceOnce(source, constructor,
                                constructor + " read(t) { return H.read(t); }");
                    replaceOnce(source, "typeof H.read(element)", "typeof shape.read(element)");
                }
            }
            if (control >= 151) {
                source = R"js(function guarded(element) {
                    const H = { read(t) {
                        if (t.getAttribute('x') !== null) return '';
                        const keys = Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"));
                        let joined = '';
                        for (const n of keys) joined = joined + t.dataset[n] + '|';
                        return joined;
                    } };
                    class Shape { constructor() { this.key = 'x'; } }
                    const shape = new Shape();
                    return H.read(element) === 'value|' && element.hasAttribute(shape.key);
                })js";
                if (control == 152) {
                    const std::string early = "return '';";
                    replaceOnce(source, early, "{ unknown(); return ''; }");
                }
                if (control == 155) {
                    source.insert(
                        source.find("let joined"),
                        "if (t.hasAttribute('later')) t.setAttribute('data-bs-new', 'x'); ");
                }
                if (control == 156) {
                    source.insert(source.find("for (const"),
                                  "if (t.hasAttribute('later')) joined = 0; ");
                }
            }
            if (control >= 157) {
                source = R"js(function guarded(element) {
                    function readDataset(t) {
                        const keys = Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"));
                        let joined = '';
                        for (const n of keys) joined = joined + t.dataset[n] + '|';
                        return joined;
                    }
                    class Shape { constructor() { this.key = 'x'; } }
                    const shape = new Shape();
                    const saved = readDataset(element);
                    if (element.hasAttribute(shape.key)) return false;
                    return saved + readDataset(element) === 'value|value|';
                })js";
                if (control == 158) {
                    source.insert(source.find("return saved"),
                                  "element.setAttribute('marker', 'between'); ");
                }
                if (control == 159) {
                    source.insert(source.find("return saved"), "element.unknown(); ");
                }
                if (control == 160 || control == 161) {
                    const std::string call = "return saved + readDataset(element)";
                    replaceOnce(source, call,
                                control == 160 ? "return saved + readDataset({})"
                                               : "return saved + readDataset(element, 1)");
                }
                if (control == 162) {
                    source.insert(source.find("return saved"),
                                  "element.setAttribute('leak', readDataset); ");
                }
                if (control == 163) {
                    replaceOnce(source, "let joined = '';", "let joined = this;");
                }
            }
            if (control >= 165) {
                source = R"js(function guarded(element) {
                    const H = { unused(t, e) { return t; },
                        read(t) { return t.getAttribute('x'); } };
                    class Shape { constructor() { this.key = 'x'; } }
                    const shape = new Shape();
                    return H.read(element) === null && element.getAttribute(shape.key) === null;
                })js";
                const std::string expression = control == 166   ? "typeof t"
                                               : control == 167 ? "t === e"
                                               : control == 168 ? "+t"
                                               : control == 169 ? "t.value"
                                               : control == 170 ? "element"
                                                                : "t";
                replaceOnce(source, "return t;", "return " + expression + ";");
            }
            if (control >= 172 && control < 181) {
                const std::string expression = control == 173   ? "t ? (e ? typeof t : !e) : void t"
                                               : control == 174 ? "typeof t === 'string' ? t : e"
                                               : control == 175 ? "t ? (e(), t) : e"
                                               : control == 176 ? "t ? e : (e(), t)"
                                               : control == 177 ? "t ? e.value : t"
                                               : control == 178 ? "t ? e : +t"
                                               : control == 179 ? "false ? (e(), t) : t"
                                                                : "t ? e : t";
                replaceOnce(source, "return t;", "return " + expression + ";");
            }
            if (control >= 181 && control < 189) {
                const std::string body =
                    control == 182 ? "if (t) { if (e) return typeof t; return !e; } return void t;"
                    : control == 183 ? "if (t === e) return true; return false;"
                    : control == 184 ? "if (t) { e(); return t; } return e;"
                    : control == 185 ? "if (t) return e; e(); return t;"
                    : control == 186 ? "if (t) return e.value; return t;"
                    : control == 187 ? "if (false) { e(); return t; } return e;"
                                     : "if (t) return e; return t;";
                replaceOnce(source, "return t;", body);
            }
            if (control >= 189) {
                const std::string body =
                    control == 191
                        ? "let saved = t; function hidden(saved) { return typeof saved; } "
                          "saved = e; return saved;"
                    : control == 192 ? "function hidden(t) { unknown(t); } return t;"
                    : control == 193 ? "function hidden() { return t; } return t;"
                    : control == 194 ? "function hidden(t) { return t; } return hidden(t);"
                    : control == 195 ? "function hidden(t) { return t; } return t.value;"
                    : control == 189 || control == 196
                        ? "function hidden(t) { return t; } return typeof t;"
                        : "function hidden(t) { return t; } t = e; "
                          "if (t) t = !e; else t = typeof e; return t;";
                replaceOnce(source, "return t;", body);
            }
            auto candidate = import(context, source, true);
            if (!candidate) { return false; }
            // Controls 189 and up keep a local cell in a two-parameter function.
            // Controls 197-200 then corrupt that cell in the IR: they redirect
            // a read, redirect a write's cell or value, or return the cell
            // itself. The proof must refuse each of these.
            if (control >= 189) {
                ctjs::CreateCellOp cell;
                candidate->walk([&](ctjs::CreateCellOp found) {
                    if (!cell && found->getParentOfType<ctjs::FuncOp>()
                                         .getBody()
                                         .front()
                                         .getNumArguments() == ctjs::implicit_arguments + 2) {
                        cell = found;
                    }
                });
                if (!check(static_cast<bool>(cell),
                           "unused source body retains a real local cell")) {
                    return false;
                }
                auto function = cell->getParentOfType<ctjs::FuncOp>();
                if (control == 197) {
                    for (auto read : llvm::make_early_inc_range(cell.getResult().getUsers())) {
                        if (auto get = llvm::dyn_cast<ctjs::CellGetOp>(read)) {
                            get->setOperand(0, function.getBody().front().getArgument(
                                                   ctjs::implicit_arguments));
                            break;
                        }
                    }
                }
                if (control == 198 || control == 200) {
                    mlir::Value replacement = cell.getResult();
                    if (control == 198) {
                        replacement =
                            function.getBody().front().getArgument(ctjs::implicit_arguments);
                    }
                    function.walk([&](ctjs::CellSetOp write) {
                        write->setOperand(control == 198 ? 0 : 1, replacement);
                    });
                }
                if (control == 199) {
                    function.walk([&](ctjs::ReturnOp returned) {
                        returned->setOperand(0, cell.getResult());
                    });
                }
            }
            // Mark the module as if the caller had supplied its own proof.
            // Preparation must ignore that claim and re-prove from source. A
            // successful run removes the attribute, which is checked below.
            (*candidate)->setAttr("ctnative.supplied", mlir::UnitAttr::get(&context));
            const auto original = printed(*candidate);
            ctnative::HostContract request;
            request.provider = provider;
            request.entry = guarded(*candidate).getSymName().str();
            request.elementParameters = {0};
            request.initialIntrinsics = {"__ctbrowser_class_defined"};
            if (control == 2 || (control >= 25 && control != 26)) {
                request.initialIntrinsics.push_back("Error");
            }
            if (control == 33) { request.initialIntrinsics.push_back("Error"); }
            if (control >= 35 && control != 36) { request.initialIntrinsics.push_back("Number"); }
            if (control == 37) { request.initialIntrinsics.push_back("Number"); }
            if (control == 43) { request.initialIntrinsics.push_back("__ctbrowser_class_defined"); }
            if (control >= 47) {
                request.initialIntrinsics.push_back("decodeURIComponent");
                if (control != 48) { request.initialIntrinsics.push_back("JSON"); }
            }
            if (control >= 70) { request.initialIntrinsics.push_back("__ctbrowser_regexp"); }
            if (control >= 82) {
                request.initialIntrinsics = {"__ctbrowser_class_defined", "Object", "Array"};
                if (control != 89 && control != 101) {
                    request.initialIntrinsics.push_back("String");
                }
                if (control != 90 && control != 102) { request.datasetParameters = {0}; }
            }
            if (control >= 104) {
                request.datasetParameters.clear();
                request.initialIntrinsics = {"__ctbrowser_class_defined", "decodeURIComponent"};
                if (control != 111) { request.initialIntrinsics.push_back("Number"); }
                if (control != 112 && control != 124) {
                    request.initialIntrinsics.push_back("JSON");
                }
                if (control != 113) { request.initialIntrinsics.push_back("__ctbrowser_regexp"); }
            }
            if (control >= 128) {
                request.initialIntrinsics = {"__ctbrowser_class_defined",
                                             "Number",
                                             "decodeURIComponent",
                                             "Object",
                                             "Array",
                                             "String"};
                if (control != 137) { request.initialIntrinsics.push_back("JSON"); }
                if (control != 138) { request.datasetParameters = {0}; }
            }
            if (control >= 140) {
                request.initialIntrinsics = {"__ctbrowser_class_defined",
                                             "Object",
                                             "Array",
                                             "String",
                                             "__ctbrowser_for_of_open",
                                             "__ctbrowser_iter_next",
                                             "__ctbrowser_iter_close"};
                if (control == 146) { request.datasetParameters.clear(); }
                if (control == 154) { request.initialIntrinsics.pop_back(); }
            }
            request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
            const auto before = request;
            // The work budget passed to prepareDOMEntry. The controls listed
            // here get zero and must refuse for lack of budget. Controls 4 and
            // 17 get 1000, which is too little for their programs. The others
            // get 100000, or 1000000 from control 47 on, where the programs
            // are larger; both are effectively unlimited.
            static const llvm::DenseSet<unsigned> zeroBudgetControls{
                3, 32, 55, 69, 81, 91, 103, 115, 125, 139, 147, 153, 164, 171, 180, 188, 196};
            auto error = ctnative::prepareDOMEntry(*candidate, request,
                                                   zeroBudgetControls.contains(control) ? 0
                                                   : control == 4 || control == 17      ? 1000
                                                   : control >= 47                      ? 1000000
                                                                                        : 100000);
            // The variants expected to prove. Controls 57 and 67 synchronously
            // call `identity`, a capture-free helper nested in M. The class
            // proof admits that call (added in commit 7fa3a87b4).
            static const llvm::DenseSet<unsigned> admittedControls{
                0,   2,   5,   8,   13,  19,  23,  25,  35,  38,  46,  47,  56,  57,
                58,  59,  60,  67,  70,  71,  72,  73,  77,  82,  92,  93,  121, 100,
                104, 105, 114, 116, 117, 122, 128, 129, 130, 140, 141, 148, 151, 157,
                158, 165, 166, 167, 172, 173, 174, 181, 182, 183, 189, 190, 191};
            if (!admittedControls.contains(control)) {
                if (!error) {
                    llvm::errs() << "unexpected class/DOM admission: " << control << '\n';
                }
                check(static_cast<bool>(error), "unproved class/DOM composition refuses");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original &&
                          request.moduleSha256 == before.moduleSha256 &&
                          request.initialIntrinsics == before.initialIntrinsics &&
                          request.elementParameters == before.elementParameters &&
                          request.datasetParameters == before.datasetParameters &&
                          request.provider == before.provider && request.entry == before.entry,
                      "class/DOM refusal preserves original source and contract");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis checked(*candidate, request);
                check(
                    mlir::succeeded(mlir::verify(*candidate)) && checked.proved() &&
                        (control >= 140 ? request.initialIntrinsics ==
                                              std::vector<std::string>{"Object", "Array", "String",
                                                                       "__ctbrowser_for_of_open",
                                                                       "__ctbrowser_iter_next",
                                                                       "__ctbrowser_iter_close"}
                         : control >= 128
                             ? request.initialIntrinsics ==
                                   std::vector<std::string>{"Number", "decodeURIComponent",
                                                            "Object", "Array", "String", "JSON"}
                         : control >= 104
                             ? request.initialIntrinsics ==
                                   std::vector<std::string>{"decodeURIComponent", "Number", "JSON",
                                                            "__ctbrowser_regexp"}
                         : control >= 82 ? request.initialIntrinsics ==
                                               std::vector<std::string>{"Object", "Array", "String"}
                         : control >= 70
                             ? request.initialIntrinsics ==
                                   std::vector<std::string>{"Number", "decodeURIComponent", "JSON",
                                                            "__ctbrowser_regexp"}
                         : control == 47 || control >= 56
                             ? request.initialIntrinsics ==
                                   std::vector<std::string>{"Number", "decodeURIComponent", "JSON"}
                         : control >= 35
                             ? request.initialIntrinsics == std::vector<std::string>{"Number"}
                             : request.initialIntrinsics.empty()) &&
                        request.moduleSha256 == ctnative::hostContractFingerprint(*candidate) &&
                        !(*candidate)->hasAttr("ctnative.supplied"),
                    "class/DOM composition publishes only the fresh typed DOM proof");
                if (control == 157 && checked.proved()) {
                    for (const bool duplicate : {false, true}) {
                        mlir::OwningOpRef<mlir::ModuleOp> broken(candidate->clone());
                        ctjs::FrameExitOp exit;
                        guarded(*broken).walk([&](ctjs::FrameExitOp op) {
                            if (!exit && op->getParentOfType<mlir::scf::IfOp>()) { exit = op; }
                        });
                        if (!check(static_cast<bool>(exit),
                                   "conditional entry retains its exits")) {
                            continue;
                        }
                        if (duplicate) {
                            mlir::OpBuilder at(exit);
                            at.clone(*exit);
                        } else {
                            exit.erase();
                        }
                        auto malformed = request;
                        malformed.moduleSha256 = ctnative::hostContractFingerprint(*broken);
                        const ctnative::DOMEntryAnalysis refused(*broken, malformed);
                        check(!refused.proved(),
                              "missing or repeated conditional frame exits cannot receive a proof");
                    }
                }
            }
        }
        // Dataset-key normalization (strip a "bs" prefix, lowercase the first
        // character), as Bootstrap does it. Controls 0, 7 and 8 must prove.
        // The others change the key derivation (1, 2, 4), add a second writer
        // (3), remove the String intrinsic (5), give a zero budget (6, 12),
        // overwrite the class method through a computed key or an alias
        // (9, 10), or add an unknown call (11), and must refuse without
        // changing anything.
        for (unsigned control = 0; control < 13; ++control) {
            std::string source = R"js(function guarded(element) {
                const H = { read(t) {
                    const result = {};
                    const keys = Object.keys(t.dataset).filter(t => t.startsWith("bs") && !t.startsWith("bsConfig"));
                    for (const n of keys) {
                        let i = n.replace(/^bs/, "");
                        i = i.charAt(0).toLowerCase() + i.slice(1);
                        result[i] = t.dataset[n];
                    }
                    return result;
                } };
                class Shape { constructor() { this.key = 'x'; } }
                const shape = new Shape();
                return typeof H.read(element) === 'object' && element.hasAttribute(shape.key);
            })js";
            if (control == 1) { replaceOnce(source, "i.slice(1)", "n.slice(1)"); }
            if (control == 2) { replaceOnce(source, "t.dataset[n]", "t.dataset[i]"); }
            if (control == 3) {
                source.insert(source.find("return result;"), "result.other = 'second writer'; ");
            }
            if (control == 4) {
                replaceOnce(source, "i.charAt(0).toLowerCase()", "i.toLowerCase()");
            }
            if (control >= 7) {
                const std::string original = "class Shape { constructor() { this.key = 'x'; } }";
                replaceOnce(source, original,
                            "class Shape { read(element) { return typeof H.read(element) === "
                            "'object'; } }");
                const std::string observation = "return typeof H.read(element) === 'object' && "
                                                "element.hasAttribute(shape.key);";
                replaceOnce(source, observation, "return shape.read(element);");
            }
            if (control == 8) {
                replaceOnce(source, "const result = {};",
                            "const fresh = {}; const result = fresh;");
            }
            if (control == 9) {
                source.insert(source.find("return shape.read"),
                              "shape[element.getAttribute('slot')] = element => true; ");
            }
            if (control == 10) {
                source.insert(source.find("return shape.read"),
                              "const alias = shape; alias.read = element => true; ");
            }
            if (control == 11) {
                source.insert(source.find("return shape.read"), "element.unknown(); ");
            }
            auto candidate = import(context, source, true);
            if (!candidate) { continue; }
            const auto original = printed(*candidate);
            ctnative::HostContract request;
            request.provider = provider;
            request.entry = guarded(*candidate).getSymName().str();
            request.elementParameters = request.datasetParameters = {0};
            request.initialIntrinsics = {"__ctbrowser_class_defined",
                                         "Object",
                                         "Array",
                                         "RegExp",
                                         "__ctbrowser_regexp",
                                         "__ctbrowser_for_of_open",
                                         "__ctbrowser_iter_next",
                                         "__ctbrowser_iter_close"};
            if (control != 5) { request.initialIntrinsics.push_back("String"); }
            request.moduleSha256 = ctnative::hostContractFingerprint(*candidate);
            const auto before = request;
            auto error = ctnative::prepareDOMEntry(*candidate, request,
                                                   control == 6 || control == 12 ? 0 : 1000000);
            if (control != 0 && control != 7 && control != 8) {
                check(static_cast<bool>(error), "unproved normalized output keys refuse");
                llvm::consumeError(std::move(error));
                check(printed(*candidate) == original &&
                          request.moduleSha256 == before.moduleSha256 &&
                          request.initialIntrinsics == before.initialIntrinsics &&
                          request.elementParameters == before.elementParameters &&
                          request.datasetParameters == before.datasetParameters &&
                          request.provider == before.provider && request.entry == before.entry,
                      "normalized-key refusal preserves original class source and contract");
            } else {
                if (error) { llvm::errs() << llvm::toString(std::move(error)) << '\n'; }
                const ctnative::DOMEntryAnalysis proof(*candidate, request);
                check(proof.proved() && mlir::succeeded(mlir::verify(*candidate)),
                      "normalized-key preparation publishes a complete fresh DOM proof");
            }
        }
    }
    return true;
}

} // namespace ctcompile::test::exception_recovery
