// Lexical selection and ordinary receiver dispatch must remain distinct.
//--- inherited-super-nearest.js
function super_nearest() {
    class Base {
        read() { return 1; }
    }
    class Middle extends Base {
        constructor() { super(); }
        read() { return 2; }
    }
    class Leaf extends Middle {
        constructor() { super(); }
        read() { return 100; }
        run() { return super.read(); }
    }
    var leaf = new Leaf();
    return leaf.run() + leaf.read();
}
var a = super_nearest();

//--- inherited-super-receiver.js
function super_receiver() {
    class Base {
        constructor(n) { this.n = n; }
        tail() { return this.n; }
        read() { return this.tail(); }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        tail() { return this.n * 2; }
        read() { return super.read() + 1; }
    }
    return new Leaf(7).read();
}
var a = super_receiver();

// A method inherited from Middle retains Middle's lexical home on Leaf.
//--- inherited-super-chain.js
function super_chain() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Middle extends Base {
        constructor(n) { super(n); }
        read() { return super.read() + 1; }
        run() { return super.read(); }
    }
    class Leaf extends Middle {
        constructor(n) { super(n); }
        read() { return super.read() * 2; }
    }
    var leaf = new Leaf(3);
    return leaf.read() * 10 + leaf.run();
}
var a = super_chain();

//--- inherited-super-order.js
function super_order() {
    class Base {
        constructor() { this.n = 0; }
        step(t) { this.n = this.n * 10 + t; return this.n; }
        mix(a, b) { this.n = this.n * 10 + 3; return a * 100 + b; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        run() {
            var result = super.mix(this.step(1), this.step(2));
            return result * 1000 + this.n;
        }
    }
    return new Leaf().run();
}
var a = super_order();

// Uncalled source bodies still need proof; missing properties cannot disappear.
//--- inherited-super-missing.js
function super_missing() {
    class Base { constructor() { this.n = 7; } }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return super.missing(); }
    }
    return new Leaf().n;
}
var a = super_missing();

//--- inherited-super-replaced.js
function super_replaced() {
    class Base { read() { return 7; } }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return super.read(); }
    }
    Base.prototype.read = function() { return 9; };
    return new Leaf().read();
}
var a = super_replaced();

//--- inherited-super-identity.js
function super_identity() {
    class Base { read() { return 7; } }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return super.read === super.read ? 1 : 0; }
    }
    return new Leaf().read();
}
var a = super_identity();

//--- inherited-super-home.js
function super_home() {
    class Base {
        constructor() { this.n = 7; }
        read() { return 7; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return super.read(); }
    }
    Leaf.prototype.read.__home = Base.prototype;
    return new Leaf().n;
}
var a = super_home();

//--- inherited-super-capture.js
function super_capture() {
    const n = 7;
    class Base { read() { return n; } }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return super.read(); }
    }
    return new Leaf().read();
}
var a = super_capture();

//--- inherited-super-dynamic.js
function super_dynamic() {
    class Base { read() { return 7; } }
    class Leaf extends Base {
        constructor() { super(); }
        read(key) { return super.read[key](this); }
    }
    return new Leaf().read("call");
}
var a = super_dynamic();

//--- inherited-super-foreign.js
function super_foreign() {
    class Base {
        constructor(n) { this.n = n; }
        read() { return this.n; }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        read(other) { return super.read.call(other); }
    }
    return new Leaf(7).read(new Base(9));
}
var a = super_foreign();

// Target CFG needs a separate call/receiver proof before it can be expanded.
//--- inherited-super-target-cfg.js
function super_target_cfg() {
    class Base {
        constructor(n) { this.n = n; }
        read(t) { if (t) { return this.n; } return this.n + 1; }
    }
    class Leaf extends Base {
        constructor(n) { super(n); }
        read(t) { return super.read(t); }
    }
    return new Leaf(7).read(true);
}
var a = super_target_cfg();
