"""The global_maps lit cases as pytest tests, one per ../*.test beside this
file: core.test runs the driver whole, the others one --group each. The
arguments are each case's RUN line after `python3 -m <driver>`."""

from CTNative.Ownership.global_maps.driver.__main__ import main

TOOLS = (
    "--translate ctjs-translate --opt ctjs-opt --work %t --node %node --reference %native_reference"
)


def test_core(lit):
    main(lit.args(TOOLS))


def test_nested_maps(lit):
    main(lit.args("--group nested-maps " + TOOLS))


def test_object_keys(lit):
    main(lit.args("--group object-keys " + TOOLS))


def test_recorder(lit):
    main(lit.args("--group recorder " + TOOLS))


def test_umd(lit):
    main(lit.args("--group umd " + TOOLS))
