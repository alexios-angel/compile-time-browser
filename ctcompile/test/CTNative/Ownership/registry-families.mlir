// A Bootstrap-style registry family: a Map of Maps in a run-once static
// (Bootstrap's Data), a Base whose constructor registers `this` through
// Data.set under the subclass's static DATA_KEY, and subclasses made only
// through getOrCreateInstance. The registry proof
// (lib/CTNative/Analysis/Inference/Instances.cpp) covers the subclass's copy
// of Base's constructor (`super:<P>:constructor`), whose `this` it checks
// with the constructor's; markInstanceRegistries marks such a copy
// ctnative.instance_ctor_copy, because the `super:` store the proof read is
// gone by admission.
//
// Each program is its own file: the proof stops at its first refusal, so two
// clauses in one program would test only the first.

// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/family.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=FAMILY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/family.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=BOUNDARY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/leak.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=LEAK

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/inherited.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=INHERITED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/twice.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=TWICE
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/early.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=EARLY
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/reassigned.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=REASSIGNED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/escaped.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf \
// RUN:              --ctnative-test-instance-registries=lifted=true \
// RUN:   | FileCheck %s --check-prefix=ESCAPED

// INHERITED: ctjs.func @Alert{{.*}}ctnative.instance_key = "bs.alert"
// INHERITED-SAME: ctnative.instance_registry = "proved"
// INHERITED: ctjs.func @Button{{.*}}ctnative.instance_key = "bs.button"
// INHERITED-SAME: ctnative.instance_registry = "proved"
// TWICE-LABEL: ctjs.func private @fn$
// TWICE: ctjs.create_cell {{.*}}ctnative.static_cell = "static_binding_2"
// TWICE: [[TWICE_VALUE:%[0-9]+]] = ctjs.cell_get
// TWICE: ctjs.return [[TWICE_VALUE]]
// EARLY-LABEL: ctjs.func private @fn$
// EARLY: ctjs.create_cell {{.*}}ctnative.static_cell = "static_binding"
// EARLY: [[EARLY_VALUE:%[0-9]+]] = ctjs.cell_get
// EARLY: ctjs.return [[EARLY_VALUE]]
// REASSIGNED: ctjs.define_accessor "NAME"
// REASSIGNED: ctjs.get_property
// ESCAPED: ctjs.define_accessor "NAME"
// ESCAPED: ctjs.get_property

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/receivers.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=RECEIVERS
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/shadowed.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHADOWED
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/ambiguous.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=AMBIGUOUS

// RECEIVERS: ctjs.func {{.*}}@Alert${{.*}}ctnative.instance_class = "Alert"
// RECEIVERS: ctjs.func {{.*}}@bump$[[ALERT_BUMP:[0-9]+]](
// RECEIVERS: ctjs.func {{.*}}@_isShown$[[ALERT_SHOWN:[0-9]+]](
// RECEIVERS: ctjs.func @refresh${{.*}}ctnative.instance_method = "Alert"
// RECEIVERS: ctjs.call_direct @bump$[[ALERT_BUMP]](%arg0,
// RECEIVERS: ctjs.call_direct @_isShown$[[ALERT_SHOWN]](%arg0,
// RECEIVERS: ctjs.func {{.*}}@Button${{.*}}ctnative.instance_class = "Button"
// RECEIVERS: ctjs.func {{.*}}@bump$[[BUTTON_BUMP:[0-9]+]](
// RECEIVERS: ctjs.func @refresh${{.*}}ctnative.instance_method = "Button"
// RECEIVERS: ctjs.call_direct @bump$[[BUTTON_BUMP]](%arg0,
// SHADOWED: ctjs.func @refresh${{.*}}ctnative.instance_method = "Alert"
// SHADOWED-SAME: ctnative.not_native = "uses `this` - a method field: it passes `this` to a call this rewrite cannot make direct"
// SHADOWED: ctjs.get_property
// AMBIGUOUS: ctjs.func private @run$
// AMBIGUOUS-SAME: ctnative.not_native = "native Map needs supported keys
// AMBIGUOUS: scf.if
// AMBIGUOUS: ctjs.constant #ctjs.string<"bump">
// AMBIGUOUS-NEXT: {{.*}}ctjs.get_property
// AMBIGUOUS-NEXT: {{.*}}ctjs.call

// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/unknown-alias.js 2>/dev/null \
// RUN:   | ctjs-opt --ctjs-resolve-globals --ctjs-lift-to-scf --ctnative-lower-to-emitc \
// RUN:   | FileCheck %s --check-prefix=SHADOWED

// --- The family proves, per class ------------------------------------------
// Both subclasses prove, each under its own DATA_KEY (the per-family key
// rule of markInstanceRegistries). Base itself is never constructed and
// proves nothing.
// FAMILY: ctjs.func @Alert{{.*}}ctnative.instance_key = "bs.alert"
// FAMILY-SAME: ctnative.instance_registry = "proved"
// FAMILY: ctjs.func @Button{{.*}}ctnative.instance_key = "bs.button"
// FAMILY-SAME: ctnative.instance_registry = "proved"

// --- Where the lowering of it stands ----------------------------------------
// Each constructor copy calls a different Data.set copy, so the two exact
// instance classes never share one parameter. The shared Map still needs a
// keyed representation; copying the setter does not partition its payload.
// BOUNDARY: ctnative.copies = {{.*}}[[SET_COPY:fn\$[0-9]+]] = "fn$2"
// BOUNDARY: a shared binding of type !ctnative.map<!ctnative.num<i32>, !ctnative.map<!ctnative.str<utf8>, !ctnative.variant<!ctnative.instance<\22Alert\22>, !ctnative.instance<\22Button\22>>>>, which has no native carrier
// BOUNDARY: ctjs.func private @fn$2(
// BOUNDARY-NOT: parameter 2 is
// BOUNDARY: ctjs.func private @[[SET_COPY]](
// BOUNDARY-NOT: parameter 2 is
// BOUNDARY: ctjs.func private @Base_Button{{.*}}ctnative.instance_ctor_copy = "Button"
// BOUNDARY: ctjs.call_direct @fn$2(
// BOUNDARY: ctjs.func private @Base_Alert{{.*}}ctnative.instance_ctor_copy = "Alert"
// BOUNDARY: ctjs.call_direct @[[SET_COPY]](

// --- A copy the proof did not check still refuses on its leak ---------------
// This subclass's copy of Base's constructor also hands `this` to an
// unproven call, so the registry proof refuses the class, the copy carries
// no ctnative.instance_ctor_copy, and the method rule's own leak check
// refuses it (ClosureLifting/Methods.cpp, whyThisLeaks).
// LEAK: ctjs.func @Base_Alert
// LEAK-NOT: ctnative.instance_ctor_copy
// LEAK-SAME: ctnative.not_native = "uses `this`

//--- family.js
var total = (function () {
    const elements = new Map();
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
        }
        static getInstance(element) {
            return Data.get(element, this.DATA_KEY);
        }
        static getOrCreateInstance(element) {
            return this.getInstance(element) || new this(element);
        }
    }
    class Alert extends Base {
        constructor(element) {
            super(element);
            this.kind = 1;
        }
        static get DATA_KEY() { return "bs.alert"; }
        close() { return this.kind + 10; }
    }
    class Button extends Base {
        constructor(element) {
            super(element);
            this.kind = 2;
        }
        static get DATA_KEY() { return "bs.button"; }
        toggle() { return this.kind + 20; }
    }
    let t = 0;
    t = t + Alert.getOrCreateInstance(1).close();
    t = t + Button.getOrCreateInstance(1).toggle();
    t = t + Alert.getOrCreateInstance(1).close();
    return t;
})();

//--- inherited.js
var total = (function () {
    const elements = new Map();
    const alertName = "alert";
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) { elements.set(element, new Map()); }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
        }
        static get DATA_KEY() { return `bs.${this.NAME}`; }
        static getInstance(element) { return Data.get(element, this.DATA_KEY); }
        static getOrCreateInstance(element) {
            return this.getInstance(element) || new this(element);
        }
    }
    class Alert extends Base {
        constructor(element) { super(element); this.kind = 1; }
        static get NAME() { return alertName; }
        close() { return this.kind + 10; }
    }
    class Button extends Base {
        constructor(element) { super(element); this.kind = 2; }
        static get NAME() { return "button"; }
        toggle() { return this.kind + 20; }
    }
    return Alert.getOrCreateInstance(1).close() + Button.getOrCreateInstance(1).toggle();
})();

//--- twice.js
var total = (function () {
    const elements = new Map();
    let name = "first";
    class Item {
        constructor(key) { elements.set(key, this); }
        static get NAME() { return name; }
        read() { return this.constructor.NAME; }
    }
    name = "second";
    const item = new Item(1);
    return item.read();
})();

//--- early.js
var total = (function () {
    class Item {
        static get NAME() { return name; }
        static read() { return this.NAME; }
    }
    const result = Item.read();
    var name = "late";
    return result;
})();

//--- reassigned.js
var total = (function () {
    class Item {
        static get NAME() { return "first"; }
        static read() { return this.NAME; }
    }
    Object.defineProperty(Item, "NAME", {get() { return "second"; }});
    return Item.read();
})();

//--- escaped.js
var total = (function () {
    class Item {
        static get NAME() { return "first"; }
        static read() { return this.NAME; }
    }
    unknown(Item);
    return Item.read();
})();

//--- receivers.js
function run() {
    const registry = new Map();
    class Alert {
        constructor() { this.n = 1; registry.set("alert", this); }
        bump() { return this.n + 1; }
        _isShown() { return this.n; }
        refresh() { return this.bump() + this._isShown(); }
    }
    class Button {
        constructor() { this.n = 5; this._isShown = false; registry.set("button", this); }
        bump() { return this.n + 10; }
        refresh() { return this.bump(); }
    }
    new Alert();
    new Button();
    return registry.size;
}
var total = run();

//--- shadowed.js
function run() {
    const registry = new Map();
    class Alert {
        constructor() { this.n = 1; this._isShown = false; registry.set("alert", this); }
        bump() { return this.n + 1; }
        _isShown() { return this.n; }
        refresh() { return this.bump() + this._isShown(); }
    }
    class Button {
        constructor() { this.n = 5; this._isShown = false; registry.set("button", this); }
        bump() { return this.n + 10; }
        refresh() { return this.bump(); }
    }
    new Alert();
    new Button();
    return registry.size;
}
var total = run();

//--- ambiguous.js
function run() {
    const registry = new Map();
    class Alert {
        constructor() { this.n = 1; registry.set("alert", this); }
        bump() { return this.n + 1; }
        _isShown() { return this.n; }
        refresh() { return this.bump() + this._isShown(); }
    }
    class Button {
        constructor() { this.n = 5; this._isShown = false; registry.set("button", this); }
        bump() { return this.n + 10; }
        refresh() { return this.bump(); }
    }
    new Alert();
    new Button();
    const value = registry.get("alert") || null;
    return value ? value.bump() : 0;
}
var total = run();

//--- unknown-alias.js
function poison(value) { value._isShown = false; }
function run() {
    const registry = new Map();
    class Alert {
        constructor() { this.n = 1; registry.set("alert", this); }
        bump() { return this.n + 1; }
        _isShown() { return this.n; }
        refresh() { return this.bump() + this._isShown(); }
    }
    class Button {
        constructor() { this.n = 5; this._isShown = false; registry.set("button", this); }
        bump() { return this.n + 10; }
        refresh() { return this.bump(); }
    }
    new Alert();
    new Button();
    return registry.size;
}
var total = run();

//--- leak.js
var total = (function () {
    const elements = new Map();
    function observe(x) { return x; }
    const Data = {
        set(element, key, instance) {
            if (!elements.has(element)) {
                elements.set(element, new Map());
            }
            elements.get(element).set(key, instance);
        },
        get(element, key) {
            return (elements.has(element) && elements.get(element).get(key)) || null;
        }
    };
    class Base {
        constructor(element) {
            this._element = element;
            Data.set(element, this.constructor.DATA_KEY, this);
            if (this.constructor.LEAK) { observe(this); }
        }
        static getInstance(element) {
            return Data.get(element, this.DATA_KEY);
        }
        static getOrCreateInstance(element) {
            return this.getInstance(element) || new this(element);
        }
    }
    class Alert extends Base {
        constructor(element) {
            super(element);
            this.kind = 1;
        }
        static get DATA_KEY() { return "bs.alert"; }
        static get LEAK() { return true; }
        close() { return this.kind + 10; }
    }
    let t = 0;
    t = t + Alert.getOrCreateInstance(1).close();
    t = t + Alert.getOrCreateInstance(1).close();
    return t;
})();
