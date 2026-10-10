// Every `new Item` passes Numbers, including the second one that replaces the entry.
//--- class-map-inherited-base-numeric-arguments.js
function base_numeric_arguments() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n, factor) { super(n); this.extra = (n + 1) * factor - 2; }
    }
    const first = new Item(2, 3);
    const savedFirst = values.get("slot");
    const second = new Item(7, 2);
    const savedSecond = values.get("slot");
    return first.n * 10000 + savedFirst.extra * 100 + second.n * 10 + savedSecond.extra;
}
var a = base_numeric_arguments();

// Division and negation keep Infinity, NaN and -0 in fields written after Base stores `this`.
//--- class-map-inherited-base-numeric-special.js
function base_numeric_special() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.infinity = 1 / n; this.nan = n / n; this.zero = -n; }
    }
    const item = new Item(0);
    const saved = values.get("slot");
    return (saved.infinity === 1 / 0) * 100 + (saved.nan !== saved.nan) * 10 + (1 / item.zero === -1 / 0);
}
var a = base_numeric_special();

// Bitwise operators and unary plus on a Number never call a user-defined valueOf.
//--- class-map-inherited-base-numeric-bitwise.js
function base_numeric_bitwise() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = ((~n & 15) << 1) ^ (+n >>> 1); }
    }
    const item = new Item(3);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_numeric_bitwise();

// Refused: a later `new Item("7")` makes `n + 1` string concatenation.
//--- class-map-inherited-base-numeric-later-string.js
function base_numeric_later_string() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = n + 1; }
    }
    const first = new Item(2);
    const second = new Item("7");
    const saved = values.get("slot");
    return first.extra * 100 + saved.extra * 1;
}
var a = base_numeric_later_string();

// Refused: valueOf on an object argument reads the half-built object and re-enters `new`.
//--- class-map-inherited-base-numeric-coercion-reentry.js
function base_numeric_coercion_reentry() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(7); this.extra = n + 1; }
    }
    const argument = {
        valueOf() {
            const observed = values.get("slot").extra === void 0;
            new Item(2);
            return observed ? 4 : 9;
        }
    };
    const item = new Item(argument);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_numeric_coercion_reentry();

// Refused: a Boolean argument is primitive, but only Number arguments are handled.
//--- class-map-inherited-base-numeric-boolean.js
function base_numeric_boolean() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(7); this.extra = n + 1; }
    }
    const item = new Item(true);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_numeric_boolean();

// Refused: `this.n + 1` reads a property, so it is not known to be a Number.
//--- class-map-inherited-base-numeric-read.js
function base_numeric_read() {
    const values = new Map();
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = this.n + 1; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_numeric_read();

// Refused: a helper call's result needs its side effects checked separately.
//--- class-map-inherited-base-numeric-helper.js
function base_numeric_helper() {
    const values = new Map();
    function increment(n) { return n + 1; }
    class Base {
        constructor(n) { this.n = n; values.set("slot", this); }
    }
    class Item extends Base {
        constructor(n) { super(n); this.extra = increment(n) + 1; }
    }
    const item = new Item(7);
    const saved = values.get("slot");
    return saved.n * 100 + saved.extra * 10 + item.extra;
}
var a = base_numeric_helper();
