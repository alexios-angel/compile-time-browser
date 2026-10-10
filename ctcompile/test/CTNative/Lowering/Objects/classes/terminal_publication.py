"""Run initialization.py on the fixtures about where a constructor publishes `this`.

"Publication" is a constructor storing `this` somewhere outside itself (a Map
record, a base-class registry). This narrows initialization.OBSERVATIONS to the
SELECTED fixtures, the throwing arms and the inherited-leaf, helper and
inherited-base families, then runs the full initialization check on them.
"""

from CTNative.Lowering.Objects.classes import initialization as check

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
} | set(check.THROWING_ARMS)
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
# 43 fixtures come from the three name-prefix families above.
assert len(check.OBSERVATIONS) == len(SELECTED) + 43
check.main()
