"""oracle-checker.test beside this file as a pytest test. Its driver is
../Types/check-oracle.py --escape, which lives with the type oracle's own
case; the arguments are the RUN line's after the driver."""

from importlib import import_module

# A hyphenated file name is importable by name only.
check_oracle = import_module("Analysis.Types.check-oracle")


def test_oracle_checker(lit):
    check_oracle.main(
        lit.args(
            "--escape --exe ctcompile-test-type-oracle --script %{monorepo}/tools/check/escape-oracle.py --recording %t.rec"
        )
    )
