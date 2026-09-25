// Shared inherited methods require the same ordered own fields on every receiver.
// Reassigning a property in either constructor must retain its first position.
//--- inherited-own-fields-shared.js
function inherited_own_fields_shared() {
    class Base {
        constructor() { this.z = 1; this.a = 2; this.z = 3; }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return names.length * 100 + (names[0] === "z") * 10 + (names[1] === "a") + this.z;
        }
    }
    class Leaf extends Base {
        constructor() { super(); this.z = 7; this.a = 9; }
    }
    var base = new Base(), leaf = new Leaf();
    return base.read() * 1000 + leaf.read();
}
var a = inherited_own_fields_shared();

// A leaf's own snapshot can include fields added after super when its base has none.
//--- inherited-own-fields-leaf.js
function inherited_own_fields_leaf() {
    class Base { constructor() { this.z = 1; } }
    class Leaf extends Base {
        constructor() { super(); this.z = 5; this.a = 9; }
        read() {
            const names = Object.getOwnPropertyNames(this);
            return names.length * 100 + (names[0] === "z") * 10 + (names[1] === "a") + this.z;
        }
    }
    return new Leaf().read();
}
var a = inherited_own_fields_leaf();

// External observations use the exact constructed receiver, including its base fields.
//--- inherited-own-fields-external.js
function inherited_own_fields_external() {
    class Base { constructor() { this.z = 7; } }
    class Leaf extends Base { constructor() { super(); this.a = 9; } }
    var instance = new Leaf();
    const names = Object.getOwnPropertyNames(instance);
    return names.length * 100 + (names[0] === "z") * 10 + (names[1] === "a");
}
var a = inherited_own_fields_external();

// The receiver requirement propagates through intermediate explicit constructors.
//--- inherited-own-fields-chain.js
function inherited_own_fields_chain() {
    class Base {
        constructor() { this.z = 1; this.a = 2; }
        read() { return Object.getOwnPropertyNames(this).length * 100 + this.z * 10 + this.a; }
    }
    class Middle extends Base { constructor() { super(); this.z = 3; } }
    class Leaf extends Middle { constructor() { super(); this.a = 4; } }
    return new Leaf().read();
}
var a = inherited_own_fields_chain();

// Exact snapshot keys retain the inherited clearing effects on both receiver kinds.
//--- inherited-own-fields-clear.js
function inherited_own_fields_clear() {
    class Base {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            const names = Object.getOwnPropertyNames(this);
            this[names[0]] = null;
            this[names[1]] = null;
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base { constructor() { super(); this.first = 8; } }
    var base = new Base(), leaf = new Leaf();
    return base.dispose() * 100 + leaf.dispose();
}
var a = inherited_own_fields_clear();

// An empty field set is a proved requirement, not an absent proof.
//--- inherited-own-fields-empty.js
function inherited_own_fields_empty() {
    class Base {
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); } }
    var base = new Base(), leaf = new Leaf();
    return base.count() * 10 + leaf.count();
}
var a = inherited_own_fields_empty();

// The observed argument does not establish unconditional constructor field presence.
//--- inherited-own-fields-conditional.js
function inherited_own_fields_conditional() {
    class Base {
        constructor(n) { if (n) this.a = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor(n) { super(n); } }
    return new Leaf(1).count();
}
var a = inherited_own_fields_conditional();

// A shared method cannot borrow the base shape when the leaf adds a field.
//--- inherited-own-fields-added.js
function inherited_own_fields_added() {
    class Base {
        constructor() { this.a = 1; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); this.b = 2; } }
    var base = new Base(), leaf = new Leaf();
    return base.count() * 10 + leaf.count();
}
var a = inherited_own_fields_added();

// Empty snapshot requirements must also reject an added descendant field.
//--- inherited-own-fields-empty-added.js
function inherited_own_fields_empty_added() {
    class Base {
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); this.a = 7; } }
    return new Leaf().count();
}
var a = inherited_own_fields_empty_added();

// A matching intermediate shape cannot erase the inherited receiver requirement.
//--- inherited-own-fields-grandchild-added.js
function inherited_own_fields_grandchild_added() {
    class Base {
        constructor() { this.a = 1; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Middle extends Base { constructor() { super(); this.a = 2; } }
    class Leaf extends Middle { constructor() { super(); this.b = 3; } }
    return new Leaf().count();
}
var a = inherited_own_fields_grandchild_added();

// Source order must not let the first proved sibling authorize a different shape.
//--- inherited-own-fields-sibling-added.js
function inherited_own_fields_sibling_added() {
    class Base {
        constructor() { this.a = 1; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class First extends Base { constructor() { super(); this.a = 2; } }
    class Second extends Base { constructor() { super(); this.b = 3; } }
    return new First().count() * 10 + new Second().count();
}
var a = inherited_own_fields_sibling_added();

// A base's field write can shadow a method first declared by the leaf.
//--- inherited-own-fields-leaf-collision.js
function inherited_own_fields_leaf_collision() {
    class Base {
        constructor() { this.read = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        read() { return 9; }
    }
    return new Leaf().count();
}
var a = inherited_own_fields_leaf_collision();

// Calls during construction see the fields present at that exact point.
//--- inherited-own-fields-before-store.js
function inherited_own_fields_before_store() {
    class Base {
        constructor() { this.before = this.count(); this.last = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base { constructor() { super(); } }
    var instance = new Leaf();
    return instance.before * 10 + instance.count();
}
var a = inherited_own_fields_before_store();

// A leaf-only snapshot must inspect writes in inherited methods too.
//--- inherited-own-fields-ancestor-write.js
function inherited_own_fields_ancestor_write() {
    class Base {
        constructor() { this.a = 1; }
        add() { this.b = 2; }
    }
    class Leaf extends Base {
        constructor() { super(); }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    var instance = new Leaf();
    instance.add();
    return instance.count();
}
var a = inherited_own_fields_ancestor_write();

// Implicit derived rest/apply construction still needs its own super proof.
//--- inherited-own-fields-implicit.js
function inherited_own_fields_implicit() {
    class Base {
        constructor() { this.a = 7; }
        count() { return Object.getOwnPropertyNames(this).length; }
    }
    class Leaf extends Base {}
    return new Leaf().count();
}
var a = inherited_own_fields_implicit();

// Preserve the original for-of consumer and both inherited clearing effects.
//--- inherited-own-fields-loop.js
function inherited_own_fields_loop() {
    class Base {
        constructor() { this.first = 7; this.second = 9; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
            return (this.first === null) * 10 + (this.second === null);
        }
    }
    class Leaf extends Base { constructor() { super(); } }
    return new Leaf().dispose();
}
var a = inherited_own_fields_loop();
