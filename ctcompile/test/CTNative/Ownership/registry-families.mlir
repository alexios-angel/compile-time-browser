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

// --- The family proves, per class ------------------------------------------
// Both subclasses prove, each under its own DATA_KEY (the per-family key
// rule of markInstanceRegistries). Base itself is never constructed and
// proves nothing.
// FAMILY: ctjs.func @Alert{{.*}}ctnative.instance_key = "bs.alert"
// FAMILY-SAME: ctnative.instance_registry = "proved"
// FAMILY: ctjs.func @Button{{.*}}ctnative.instance_key = "bs.button"
// FAMILY-SAME: ctnative.instance_registry = "proved"

// --- Where the lowering of it stands ----------------------------------------
// The copy of Base's constructor each subclass runs is marked
// ctnative.instance_ctor_copy: the constructor lift's leak check and
// admission's closed-receiver check both let the registry proof stand for
// it, so it refuses only on what it calls. The next blocker the family hits
// is Data.set's instance parameter, which one family member passes an Alert
// and the other a Button: a variant of two instance types has no native
// carrier, and until it has one the Map's payload stays boxed and the rest
// of the chain (Data.get, getInstance, getOrCreateInstance, the classes
// themselves) refuses behind it. Whoever takes that blocker updates these
// lines.
// BOUNDARY-DAG: ctnative.instance_ctor_copy = "Alert"
// BOUNDARY-DAG: ctnative.instance_ctor_copy = "Button"
// BOUNDARY-DAG: parameter 2 is !ctnative.variant<!ctnative.instance<\22Alert\22>, !ctnative.instance<\22Button\22>>, which has no native carrier yet

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
