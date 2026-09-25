// Every exact leaf construction supplies Numbers, including the later Map replacement.
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

// Numeric division and negation preserve infinity, NaN and signed zero after publication.
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

// Numeric bitwise coercions and unary plus never invoke a user-defined conversion.
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

// A first numeric construction cannot authorize arithmetic for a later String argument.
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

// Object coercion can inspect the partial receiver and replace its Map entry reentrantly.
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

// A Boolean argument is primitive but outside the exact Number input proof.
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

// A property read is not an exact numeric producer, even with a numeric new-site argument.
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

// Numeric helper results need a separate effect proof before base publication can move.
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
