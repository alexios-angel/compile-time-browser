"""Check captured Map helpers against concrete record ownership and publication."""

from CTNative.Lowering.Objects import class_initialization as check

SELECTED = {
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
    if name in SELECTED or name.startswith("class-map-record-captured-helper-")
}
assert len(check.OBSERVATIONS) == len(SELECTED) + 11
check.main()
