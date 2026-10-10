"""Run initialization.py on the fixtures about Map-based registries in captured helpers.

The fixtures store records in a Map of per-element Maps (Bootstrap's `Data`
registry shape) through a helper function or holder object the class captures.
This narrows initialization.OBSERVATIONS to the SELECTED fixtures and the
name-prefix families below, then runs the full initialization check on them.
"""

from CTNative.Lowering.Objects.classes import initialization as check

SELECTED = {
    "class-map-inherited-multislot-holder",
    "class-map-record-constructor-dynamic-key",
    "class-map-inherited-keyed-holder",
    "class-map-inherited-keyed-later-number",
    "class-map-record-direct",
    "class-map-record-shared-owner",
    "class-map-record-returned-alias",
    "class-map-record-captured-alias",
    "class-map-record-missing",
    "class-map-record-region-owner",
    "class-map-record-constructor-helper",
    "class-map-record-constructor-observer",
    "class-map-record-constructor-throw",
    "class-map-record-constructor-map-escaped",
    "bootstrap-base",
    "bootstrap-base-data",
}
assert SELECTED <= check.OBSERVATIONS.keys()
check.OBSERVATIONS = {
    name: value
    for name, value in check.OBSERVATIONS.items()
    if name in SELECTED
    or name.startswith(
        (
            "class-map-record-captured-helper-",
            "class-map-record-captured-holder-",
            "class-map-record-constructor-holder-",
            "class-map-record-constructor-keyed-",
            "class-map-record-constructor-multislot-",
            "class-map-record-constructor-captured-key-",
            "class-map-record-nested-",
        )
    )
}
# 229 fixtures come from the name-prefix families above; update the count when
# initialization.py gains or loses one, so a prefix that matches nothing fails.
assert len(check.OBSERVATIONS) == len(SELECTED) + 229
check.main()
