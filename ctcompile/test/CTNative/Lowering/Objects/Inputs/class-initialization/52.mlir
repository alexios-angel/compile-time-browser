// Bootstrap B's constructor arm reads this.constructor.DATA_KEY, which needs
// the inherited throwing NAME: an exact class's arm ends at that read.
//--- inherited-own-fields-iterate-throwing-arm.js
function inherited_own_fields_iterate_throwing_arm(u) {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { throw new Error("NAME required"); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
        }
    }
    class Leaf extends Base {
        constructor(t) { super(), t && (this.x = t, this.y = this.constructor.NAME) }
    }
    const leaf = new Leaf(u);
    leaf.dispose();
    return (leaf.n === null) * 7;
}
var a = inherited_own_fields_iterate_throwing_arm();

//--- inherited-own-fields-iterate-throwing-arm-subclass.js
function inherited_own_fields_iterate_throwing_arm_subclass(u) {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { throw new Error("NAME required"); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
        }
    }
    class Leaf extends Base {
        constructor(t) { super(), t && (this.x = t, this.y = this.constructor.NAME) }
    }
    class Sub extends Leaf {}
    const leaf = new Leaf(u);
    leaf.dispose();
    return (leaf.n === null) * 7 + (new Sub(u).n === 3) * 0;
}
var a = inherited_own_fields_iterate_throwing_arm_subclass();

//--- inherited-own-fields-iterate-throwing-arm-static-shadow.js
function inherited_own_fields_iterate_throwing_arm_static_shadow(u) {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { throw new Error("NAME required"); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
        }
    }
    class Leaf extends Base {
        constructor(t) { super(), t && (this.x = t, this.y = this.constructor.NAME) }
        static NAME() { return 1; }
    }
    const leaf = new Leaf(u);
    leaf.dispose();
    return (leaf.n === null) * 7;
}
var a = inherited_own_fields_iterate_throwing_arm_static_shadow();

//--- inherited-own-fields-iterate-throwing-arm-returning.js
function inherited_own_fields_iterate_throwing_arm_returning(u) {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { return "base"; }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
        }
    }
    class Leaf extends Base {
        constructor(t) { super(), t && (this.x = t, this.y = this.constructor.NAME) }
    }
    const leaf = new Leaf(u);
    leaf.dispose();
    return (leaf.n === null) * 7;
}
var a = inherited_own_fields_iterate_throwing_arm_returning();

//--- inherited-own-fields-iterate-throwing-arm-escape-before.js
function inherited_own_fields_iterate_throwing_arm_escape_before(u) {
    class Base {
        constructor() { this.n = 3; }
        static get NAME() { throw new Error("NAME required"); }
        dispose() {
            for (const t of Object.getOwnPropertyNames(this)) this[t] = null
        }
    }
    class Leaf extends Base {
        constructor(t) { super(), t && (String(this), this.y = this.constructor.NAME) }
    }
    const leaf = new Leaf(u);
    leaf.dispose();
    return (leaf.n === null) * 7;
}
var a = inherited_own_fields_iterate_throwing_arm_escape_before();
