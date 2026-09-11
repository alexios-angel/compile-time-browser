// THE BOOTSTRAP CORPUS, DRIVEN - Phase 54B's recording input.
//
// `ctbrowser/vendor/bootstrap/bootstrap.bundle.js` is vendored and NO example
// page runs it. bootstrap-components.html says why in its own comment: a
// component that needs JavaScript to be visible cannot be compared against
// Chrome, so every fixture is forced open with a class and no script runs. That
// makes the markup an excellent DOM for the bundle and leaves the bundle itself
// unexecuted - which for a type recording means a corpus with an empty answer.
//
// So this executes it. Run after the bundle, by
//
//   type-oracle --page .../bootstrap-components.html \
//               --js .../vendor/bootstrap/bootstrap.bundle.js \
//               --js .../ctcompile/test/type-oracle-bootstrap.js --out rec
//
// EVERY CALL IS WRAPPED. A component that refuses on this engine must not stop
// the ones after it: the recording is of whatever DID run, and an early throw
// turns a corpus into a sample of its first component. `__oracleProbe` is left
// on the global so a caller can see how far it got - a driver that silently ran
// nothing and a driver that ran everything both record, and only one of them
// means anything.
(function () {
    var log = [];
    function attempt(name, body) {
        try {
            body();
            log.push(name + ":ok");
        } catch (e) {
            log.push(name + ":" + e);
        }
    }

    var names = ["Alert", "Button", "Carousel", "Collapse", "Dropdown", "Modal",
                 "Offcanvas", "Popover", "ScrollSpy", "Tab", "Toast", "Tooltip"];

    if (typeof bootstrap === "undefined") {
        globalThis.__oracleProbe = "the bundle did not define `bootstrap`";
        return;
    }

    // THE STATIC SURFACE FIRST. Every component class reads its own NAME and
    // VERSION off the base class through the prototype chain, so this alone
    // executes the class hierarchy the bundle spent most of its lines building.
    attempt("statics", function () {
        for (var i = 0; i < names.length; i++) {
            var C = bootstrap[names[i]];
            if (C) { log.push(names[i] + " " + C.NAME + " " + C.VERSION); }
        }
    });

    var button = document.querySelector(".btn");
    var host = document.createElement("div");
    document.body.appendChild(host);

    // AND THEN THE INSTANCES, each one constructed, shown and hidden. show()
    // and hide() are where the interesting arithmetic is - transitions,
    // getBoundingClientRect, the backdrop's z-index - and a constructor alone
    // executes almost none of it.
    attempt("Button", function () { new bootstrap.Button(button || host).toggle(); });
    attempt("Modal", function () {
        var m = new bootstrap.Modal(host);
        m.show();
        m.hide();
        m.dispose();
    });
    attempt("Collapse", function () {
        var c = new bootstrap.Collapse(host, {toggle: false});
        c.show();
        c.hide();
        c.dispose();
    });
    attempt("Dropdown", function () {
        var d = new bootstrap.Dropdown(button || host);
        d.show();
        d.hide();
        d.dispose();
    });
    attempt("Toast", function () {
        var t = new bootstrap.Toast(host, {autohide: false});
        t.show();
        t.hide();
        t.dispose();
    });
    attempt("Offcanvas", function () {
        var o = new bootstrap.Offcanvas(host);
        o.show();
        o.hide();
        o.dispose();
    });
    attempt("Tooltip", function () {
        var t = new bootstrap.Tooltip(host, {title: "x", trigger: "manual"});
        t.show();
        t.hide();
        t.dispose();
    });
    attempt("Tab", function () { new bootstrap.Tab(button || host).show(); });
    attempt("Alert", function () { new bootstrap.Alert(host).close(); });
    attempt("ScrollSpy", function () { new bootstrap.ScrollSpy(document.body, {}); });
    attempt("Carousel", function () {
        var c = new bootstrap.Carousel(host, {ride: false});
        c.next();
        c.prev();
        c.dispose();
    });

    globalThis.__oracleProbe = log.join(" | ");
})();
