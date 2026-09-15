// The shared Core reader must preserve JSON's observable VM adapter behavior.
#include "vm_expect.hpp"

int main() {
    expect_result(R"JS(
        const p = JSON.parse('{"b":1,"2":2,"__proto__":{"n":3},"a":4,"2":5,"b":6,"01":8}');
        const d = Object.getOwnPropertyDescriptor(p, '__proto__');
        return JSON.stringify(p) + '|' + (Object.getPrototypeOf(p) === Object.prototype) +
            '|' + (Object.getPrototypeOf(p.__proto__) === Object.prototype) +
            '|' + [d.writable, d.enumerable, d.configurable].join();
    )JS",
                  R"({"2":5,"b":6,"__proto__":{"n":3},"a":4,"01":8}|true|true|true,true,true)");
    expect_result(R"JS(
        const p = JSON.parse('{"__proto__":1,"__proto__":null,"a\\u0000b":"x\\u0000y"}');
        return JSON.stringify(p) + '|' + Object.keys(p)[1].length +
            '|' + p['a\u0000b'].length + '|' + (Object.getPrototypeOf(p) === Object.prototype);
    )JS",
                  R"({"__proto__":null,"a\u0000b":"x\u0000y"}|3|3|true)");
    expect_result(R"JS(
        const p = JSON.parse('[{"a":[]},null,true,false,"x",-0]');
        return (Object.getPrototypeOf(p) === Array.prototype) + '|' +
            (Object.getPrototypeOf(p[0]) === Object.prototype) + '|' +
            (Object.getPrototypeOf(p[0].a) === Array.prototype) + '|' +
            Object.is(p[5], -0) + '|' + JSON.stringify(p);
    )JS",
                  R"(true|true|true|true|[{"a":[]},null,true,false,"x",0])");
    // These are the original byte-string and from_chars deviations, not V8's.
    expect_result(R"JS(
        return JSON.parse('"\\ud83d\\ude00"').length + '|' +
            JSON.parse('"\\ud800"').length + '|' + JSON.parse('1e400') + '|' +
            Object.is(JSON.parse('-1e-400'), 0);
    )JS",
                  "4|3|0|true");

    expect_result(R"JS(
        const seen = [];
        const p = JSON.parse('{"b":[1,{"x":2}],"a":3}', function(k, v) {
            seen.push(k);
            return typeof v === 'number' ? v * 10 : v;
        });
        return seen.join('|') + ':' + JSON.stringify(p);
    )JS",
                  R"(0|x|1|b|a|:{"b":[10,{"x":20}],"a":30})");
    expect_result(R"JS(
        const seen = [];
        const p = JSON.parse('{"a":1,"b":2}', function(k, v) {
            seen.push(k);
            if (k === 'a') { this.b = 9; this.c = 7; return undefined; }
            return v;
        });
        return seen.join('|') + ':' + JSON.stringify(p);
    )JS",
                  R"(a|b|:{"b":9,"c":7})");
    expect_result(R"JS(
        const p = JSON.parse('[1,2,3]', function(k, v) {
            return k === '1' ? undefined : v;
        });
        return p.length + '|' + (1 in p) + '|' + JSON.stringify(p);
    )JS",
                  "3|false|[1,null,3]");
    expect_result(R"JS(
        const marker = {};
        const seen = [];
        try {
            JSON.parse('{"a":1,"b":2}', function(k, v) {
                seen.push(k);
                if (k === 'b') { throw marker; }
                return v;
            });
            return 'missed throw';
        } catch (e) { return (e === marker) + '|' + seen.join(); }
    )JS",
                  "true|a,b");
    expect_result(R"JS(
        const seen = [];
        try {
            JSON.parse({toString: function() { seen.push('source'); return '1e \tX'; }},
                function(k, v) { seen.push('reviver'); return v; });
            return 'missed throw';
        } catch (e) { return seen.join() + '|' + e.name + '|' + e.message; }
    )JS",
                  "source|SyntaxError|Unexpected token in JSON at position 4");
    expect_result(R"JS(
        try { JSON.parse('"\\ud800\\uZZZZ"'); }
        catch (e) { return e.name + '|' + e.message; }
    )JS",
                  "SyntaxError|Unexpected token in JSON at position 9");

    expect_result(R"JS(
        const r = JSON.rawJSON('1e400');
        const d = Object.getOwnPropertyDescriptor(r, 'rawJSON');
        return JSON.stringify([r, JSON.rawJSON('"x"'), JSON.rawJSON('null')]) + '|' +
            JSON.isRawJSON(r) + '|' + Object.isFrozen(r) + '|' +
            (Object.getPrototypeOf(r) === null) + '|' +
            [d.writable, d.enumerable, d.configurable].join();
    )JS",
                  R"([1e400,"x",null]|true|true|true|false,true,false)");
    expect_result(R"JS(
        const texts = ['', '{}', '[]', ' 1', '1 ', '01', '+1', '1.', 'tru', '"\\x"'];
        let errors = 0;
        for (const text of texts) {
            try { JSON.rawJSON(text); }
            catch (e) {
                if (e.name !== 'SyntaxError' || e.message !== 'Invalid JSON text for JSON.rawJSON') {
                    return e.name + '|' + e.message;
                }
                errors++;
            }
        }
        return errors;
    )JS",
                  "10");
    expect_result(R"JS(
        const marker = {};
        try { JSON.rawJSON({toString: function() { throw marker; }}); }
        catch (e) { return e === marker; }
    )JS",
                  "true");
    REPORT("vm_json_core");
}
