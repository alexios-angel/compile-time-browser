// Bootstrap's behaviour on a real page, for check-workload.py's script-run.
//
// bootstrap.json runs this after bootstrap.bundle.js on each page it lists
// (ctcompile-test-native-reference --page PAGE --script bundle --script this).
// It calls the components the pages contain on their own elements, then logs
// the DOM state each call changed: the class list, the aria-* attributes and
// style.display, one line per element, in document order. The VM and the
// native build must print the same lines. A page with none of these elements
// prints its zero counts only.

function bootstrap_probe_state(element) {
    var aria = [];
    for (var i = 0; i < element.attributes.length; i++) {
        var attribute = element.attributes[i];
        if (attribute.name.indexOf('aria-') === 0) {
            aria.push(attribute.name + '=' + attribute.value);
        }
    }
    aria.sort();
    return element.tagName.toLowerCase() + ' class="' + element.className + '" ' +
        aria.join(' ') + ' display=' + element.style.display;
}

function bootstrap_probe_each(selector, label, act) {
    var elements = document.querySelectorAll(selector);
    console.log(label + ': ' + elements.length);
    for (var i = 0; i < elements.length; i++) {
        act(elements[i]);
        console.log(label + '[' + i + '] ' + bootstrap_probe_state(elements[i]));
    }
}

function bootstrap_probe_run() {
    bootstrap_probe_each('.btn:not(.dropdown-toggle)', 'button.toggle', function (element) {
        bootstrap.Button.getOrCreateInstance(element).toggle();
    });
    bootstrap_probe_each('.collapse', 'collapse.toggle', function (element) {
        bootstrap.Collapse.getOrCreateInstance(element, {
            toggle: false
        }).toggle();
    });
    bootstrap_probe_each('.dropdown-toggle', 'dropdown.toggle', function (element) {
        bootstrap.Dropdown.getOrCreateInstance(element).toggle();
    });
    bootstrap_probe_each('.alert', 'alert.close', function (element) {
        bootstrap.Alert.getOrCreateInstance(element).close();
    });
}

bootstrap_probe_run();
