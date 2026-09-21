"""Check captured Map helpers against concrete record ownership and publication."""

from CTNative.Lowering.Objects import class_initialization as check

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
assert len(check.OBSERVATIONS) == len(SELECTED) + 106
check.main()
