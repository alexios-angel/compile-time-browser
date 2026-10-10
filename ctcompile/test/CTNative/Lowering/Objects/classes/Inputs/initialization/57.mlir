// Bootstrap's Config type check. The default reads a static getter that returns
// a fresh empty object, so Object.entries is empty and the loop never runs: the
// method is a closed effect-free path to `undefined`.
//--- own-entries-default-type.js
function own_entries_default_type() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return {}; }
        check(t, e = this.constructor.DefaultType) {
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type();

// A leaf inherits both the method and the getter it reads.
//--- inherited-own-entries-default-type.js
function inherited_own_entries_default_type() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return {}; }
        check(t, e = this.constructor.DefaultType) {
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
    }
    class Leaf extends Config {
        constructor(n) { super(n); }
        read() { this.check(this.n); return this.n; }
    }
    return new Leaf(7).read();
}
var a = inherited_own_entries_default_type();

// A getter that initializes a property is not proved at all.
//--- own-entries-default-type-fields.js
function own_entries_default_type_fields() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return { n: "undefined" }; }
        check(t, e = this.constructor.DefaultType) {
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type_fields();

// A proved getter may return a primitive: a string has an entry per code unit.
//--- own-entries-default-type-primitive.js
function own_entries_default_type_primitive() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return "undefined"; }
        check(t, e = this.constructor.DefaultType) {
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type_primitive();

// The fresh object gains a property before Object.entries reads it.
//--- own-entries-default-type-written.js
function own_entries_default_type_written() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return {}; }
        check(t, e = this.constructor.DefaultType) {
            e.n = "undefined";
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type_written();

// The fresh object escapes into an instance field before the loop.
//--- own-entries-default-type-escaped.js
function own_entries_default_type_escaped() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return {}; }
        check(t, e = this.constructor.DefaultType) {
            this.seen = e;
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type_escaped();

// Object.entries of a parameter is not a fresh object.
//--- own-entries-argument.js
function own_entries_argument() {
    class Config {
        constructor(n) { this.n = n; }
        check(t) {
            for (const [i, n] of Object.entries(t)) {
                if (!new RegExp(n).test(typeof i)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_argument();

// A call that passes the second argument leaves the default undecided.
//--- own-entries-default-type-passed.js
function own_entries_default_type_passed() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return {}; }
        check(t, e = this.constructor.DefaultType) {
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n, this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type_passed();

// Only a whole-method fold exists: an effect beside the empty loop refuses.
//--- own-entries-default-type-effect.js
function own_entries_default_type_effect() {
    class Config {
        constructor(n) { this.n = n; }
        static get DefaultType() { return {}; }
        check(t, e = this.constructor.DefaultType) {
            this.n = 7;
            for (const [i, n] of Object.entries(e)) {
                const v = t[i];
                if (!new RegExp(n).test(typeof v)) throw new TypeError(`${i}: ${n}`);
            }
        }
        read() { this.check(this.n); return this.n; }
    }
    return new Config(7).read();
}
var a = own_entries_default_type_effect();
