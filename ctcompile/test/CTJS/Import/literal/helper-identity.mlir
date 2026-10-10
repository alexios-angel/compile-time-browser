// RUN: split-file %s %t
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/result.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=RESULT --implicit-check-not=ctjs.define_own
// RUN: ctjs-translate --ctbrowser-js-to-ctjs %t/capture.js 2>/dev/null | ctjs-opt --ctjs-resolve-globals | FileCheck %s --check-prefix=CAPTURE --implicit-check-not=ctjs.define_own
// RUN: %native_reference %t/result.js | FileCheck %s --check-prefix=VALUE
// RUN: %native_reference %t/capture.js | FileCheck %s --check-prefix=VALUE

// Explicit calls to this mutable global are ordinary calls. Recognizing an
// object literal's definition must not discard their result or reload their
// callee after an argument has replaced it through the script receiver.

// RESULT-LABEL: ctjs.func {{(private )?}}@literal$
// RESULT: %[[HELPER:[^ ]+]] = ctjs.load_global "__ctbrowser_define_own"
// RESULT: %[[RETURNED:[^ ]+]] = ctjs.call %[[HELPER]](
// RESULT: ctjs.return %[[RETURNED]]

// CAPTURE-LABEL: ctjs.func {{(private )?}}@captured$
// CAPTURE: %[[ORIGINAL:[^ ]+]] = ctjs.load_global "__ctbrowser_define_own"
// CAPTURE: ctjs.set_property %arg0[
// CAPTURE-NOT: ctjs.load_global "__ctbrowser_define_own"
// CAPTURE: %[[RETURNED:[^ ]+]] = ctjs.call %[[ORIGINAL]](
// CAPTURE: ctjs.return %[[RETURNED]]

// VALUE: r=17{{$}}

//--- result.js
this.__ctbrowser_define_own = function(o, k, v, e) {
    o[k] = v;
    return 17;
};
function literal() {
    var o = {};
    return __ctbrowser_define_own(o, "x", 1, true);
}
var r = literal();

//--- capture.js
this.__ctbrowser_define_own = function(o, k, v, e) {
    o[k] = v;
    return 17;
};
function captured() {
    var o = {};
    return __ctbrowser_define_own(o, "x", (this.__ctbrowser_define_own = function() {
        return 23;
    }, 1), true);
}
var r = captured();
