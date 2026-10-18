"""regexp.test beside this file as a pytest test: strings.py's regexp cases
alone. The arguments are the RUN line's after the driver."""

from CTNative.Browser.dom.strings import main


def test_regexp(lit):
    main(lit.args("--regexp-only %dom_driver --node %node --reference %native_reference"))
