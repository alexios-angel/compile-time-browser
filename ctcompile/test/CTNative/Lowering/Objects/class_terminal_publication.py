"""Check leaf/helper and deferred base publication with initialization boundaries."""

from CTNative.Lowering.Objects import class_initialization as check

SELECTED = {
    "class-map-record-constructor-direct",
    "class-map-record-constructor-inherited",
    "class-map-record-constructor-helper",
    "class-map-record-constructor-observer",
    "class-map-record-constructor-throw",
    "class-map-record-constructor-map-escaped",
    "class-map-record-constructor-return-object",
    "class-map-record-constructor-snapshot-dispose",
    "class-map-record-alias-snapshot-inherited",
    "inherited-own-fields-loop",
    "inherited-own-fields-before-store",
    "inherited-own-fields-added",
    "class-map-inherited-constructor",
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
            "class-map-inherited-leaf-",
            "class-map-record-constructor-helper-",
            "class-map-inherited-base-",
        )
    )
}
assert len(check.OBSERVATIONS) == len(SELECTED) + 43
check.main()
