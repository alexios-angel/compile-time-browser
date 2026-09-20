#!/usr/bin/env python3
"""Keep class initialization, mutable helper effects and native admission distinct."""

import argparse
import json
from pathlib import Path
import re
import subprocess

from CTNative.Exports.boundary import FUNCTION, NATIVE, REFUSAL
from CTNative.HostContract import contract as host
from CTNative.harness import find_compilers, run
from CTNative.Lowering.Objects.constructor_refusals import NODE, check_mutable_helper, check_native
from Target.Cpp.harness import FLAGS

# The implementation hook and function metadata retain separate Node/VM
# observations. Inherited static getter lookup now agrees between the engines.
OBSERVATIONS = {
    "inherited-own-fields-iterate-forward-direct": (7911, 7911),
    "inherited-own-fields-iterate-forward-captured": (8011, 8011),
    "inherited-own-fields-iterate-forward-holder": (7911, 7911),
    "inherited-own-fields-iterate-forward-arguments": (35311, 35311),
    "inherited-own-fields-iterate-forward-inherited": (121137611, 121137611),
    "inherited-own-fields-iterate-forward-missing": (321107111, 321107111),
    "inherited-own-fields-iterate-forward-store": (77111, 77111),
    "inherited-own-fields-iterate-forward-return": (7711, 7711),
    "inherited-own-fields-iterate-forward-write": (8811, 8811),
    "inherited-own-fields-iterate-forward-dynamic": (7711, 7711),
    "inherited-own-fields-iterate-forward-recursive": (7711, 7711),
    "inherited-own-fields-iterate-forward-unused-effects": (7911, 7911),
    "inherited-own-fields-iterate-borrow-direct": (7911, 7911),
    "inherited-own-fields-iterate-borrow-captured": (7911, 7911),
    "inherited-own-fields-iterate-borrow-holder": (7911, 7911),
    "inherited-own-fields-iterate-borrow-two-arguments": (14711, 14711),
    "inherited-own-fields-iterate-borrow-inherited": (241108011, 241108011),
    "inherited-own-fields-iterate-borrow-arguments": (121137511, 121137511),
    "inherited-own-fields-iterate-borrow-missing": (321107111, 321107111),
    "inherited-own-fields-iterate-borrow-store": (77111, 77111),
    "inherited-own-fields-iterate-borrow-return": (7711, 7711),
    "inherited-own-fields-iterate-borrow-write": (8811, 8811),
    "inherited-own-fields-iterate-borrow-forward": (7711, 7711),
    "inherited-own-fields-iterate-borrow-incompatible": (771111, 771111),
    "inherited-own-fields-iterate-borrow-slot-replaced": (8011, 8011),
    "inherited-own-fields-iterate-borrow-unused-effects": (7911, 7911),
    "inherited-own-fields-iterate-borrow-constructor": (8011, 8011),
    "inherited-own-fields-iterate-borrow-method": (7911, 7911),
    "inherited-own-fields-iterate-borrow-mixed-primitive": (77111, 77111),
    "inherited-own-fields-iterate-borrow-mixed-literal": (77111, 77111),
    "inherited-own-fields-iterate-borrow-missing-argument": (None, None),
    "inherited-own-fields-iterate-getter-direct": (7211, 7211),
    "inherited-own-fields-iterate-getter-method": (8211, 8211),
    "inherited-own-fields-iterate-getter-nested": (8311, 8311),
    "inherited-own-fields-iterate-getter-chain": (8511, 8511),
    "inherited-own-fields-iterate-getter-arguments": (151137811, 151137811),
    "inherited-own-fields-iterate-getter-fresh": (7611, 7611),
    "inherited-own-fields-iterate-getter-fresh-empty": (7611, 7611),
    "inherited-own-fields-iterate-getter-inherited": (251108011, 251108011),
    "inherited-own-fields-iterate-getter-leaf": (8011, 8011),
    "inherited-own-fields-iterate-getter-nearest": (8211, 8211),
    "inherited-own-fields-iterate-getter-distinct": (231136114911, 231136114911),
    "inherited-own-fields-iterate-getter-shadow": (79111, 79111),
    "inherited-own-fields-iterate-getter-overwrite": (7311, 7311),
    "inherited-own-fields-iterate-getter-prototype": (7911, 7911),
    "inherited-own-fields-iterate-getter-effects": (80111, 80111),
    "inherited-own-fields-iterate-getter-receiver": (7911, 7911),
    "inherited-own-fields-iterate-getter-missing": (749111, 749111),
    "inherited-own-fields-iterate-getter-snapshot": (7411, 7411),
    "inherited-own-fields-iterate-method-inherited-early-snapshot": (27111, 27111),
    "inherited-own-fields-iterate-method-super-early-snapshot": (27111, 27111),
    "inherited-own-fields-iterate-method-read": (7911, 7911),
    "inherited-own-fields-iterate-method-update": (10011, 10011),
    "inherited-own-fields-iterate-method-primitive": (8911, 8911),
    "inherited-own-fields-iterate-method-nested": (8511, 8511),
    "inherited-own-fields-iterate-method-nearest": (241106211, 241106211),
    "inherited-own-fields-iterate-method-arguments": (121137511, 121137511),
    "inherited-own-fields-iterate-method-missing": (749111, 749111),
    "inherited-own-fields-iterate-method-snapshot": (7111, 7111),
    "inherited-own-fields-iterate-method-add-field": (783111, 783111),
    "inherited-own-fields-iterate-method-alias-escape": (7711, None),
    "inherited-own-fields-iterate-method-alias-escape-distinct": (7711, 7711),
    "inherited-own-fields-iterate-method-recursive": (7711, 7711),
    "inherited-own-fields-iterate-method-dead-ambient": (7811, 7811),
    "inherited-own-fields-iterate-method-inherited-snapshot": (27211, 27211),
    "inherited-own-fields-iterate-method-override-missing": (771107411, 771107411),
    "inherited-own-fields-iterate-method-super-snapshot": (17111, 17111),
    "inherited-own-fields-iterate-method-receiver-argument": (7711, 7711),
    "inherited-own-fields-iterate-helper-captured": (8911, 8911),
    "inherited-own-fields-iterate-helper-holder": (40811, 40811),
    "inherited-own-fields-iterate-helper-nested": (30311, 30311),
    "inherited-own-fields-iterate-helper-distinct": (341115011, 341115011),
    "inherited-own-fields-iterate-helper-arguments": (2331156611, 2331156611),
    "inherited-own-fields-iterate-helper-receiver-escape": (7111, 7111),
    "inherited-own-fields-iterate-helper-method-before-fields": (911, 911),
    "inherited-own-fields-iterate-helper-unused-ambient": (8911, 8911),
    "inherited-own-fields-iterate-helper-dead-ambient": (8911, 8911),
    "inherited-own-fields-iterate-helper-slot-replaced": (9911, 9911),
    "inherited-own-fields-iterate-helper-branch-fields": (2111, 2111),
    "inherited-own-fields-iterate-helper-holder-receiver": (8911, 8911),
    "inherited-own-fields-iterate-helper-selected-receiver": (7711, 7711),
    "inherited-own-fields-iterate-shared": (1111, 1111),
    "inherited-own-fields-iterate-branches": (12113411, 12113411),
    "inherited-own-fields-iterate-empty": (7, 7),
    "inherited-own-fields-iterate-effects": (15, 15),
    "inherited-own-fields-iterate-repeated": (1111, 1111),
    "inherited-own-fields-iterate-array-replaced": (0, 11),
    "inherited-own-fields-iterate-snapshot-escape": (2, 2),
    "inherited-own-fields-iterate-break": (10, 10),
    "inherited-own-fields-iterate-return": (310, 310),
    "inherited-own-fields-iterate-throw": (310, 310),
    "inherited-own-fields-iterate-unused-ambient": (7, 7),
    "captured-holder-method": (8, 8),
    "captured-holder-sibling": (16, 16),
    "inherited-captured-holder-shared": (380, 380),
    "inherited-captured-holder-order": (23135646, 23135646),
    "inherited-captured-holder-distinct": (380, 380),
    "inherited-captured-holder-chain": (3083, 3083),
    "captured-holder-shared": (308, 308),
    "captured-holder-unused": (8, 8),
    "captured-holder-unused-capture": (8, 8),
    "captured-holder-replaced": (9, 9),
    "captured-holder-mutable-cell": (9, 9),
    "captured-holder-late-alias": (9, 9),
    "captured-holder-receiver": (8, 8),
    "captured-holder-unused-ambient": (8, 8),
    "captured-holder-receiver-escape": (8, 8),
    "captured-holder-surplus": (8, 8),
    "captured-holder-map": (8, 8),
    "inherited-post-super-holder": (8, 8),
    "inherited-post-super-holder-shared": (380, 380),
    "inherited-post-super-holder-chain": (3083, 3083),
    "inherited-post-super-holder-branches": (200204, 200204),
    "inherited-post-super-holder-branch-values": (200204, 200204),
    "inherited-post-super-holder-order": (23135646, 23135646),
    "inherited-post-super-holder-dead-ambient": (8, 8),
    "inherited-post-super-holder-unused-ambient": (8, 8),
    "inherited-post-super-holder-replaced": (9, 9),
    "inherited-post-super-holder-slot-replaced": (9, 9),
    "inherited-post-super-holder-receiver": (8, 8),
    "inherited-post-super-holder-surplus": (8, 8),
    "inherited-post-super-holder-argument-receiver": (8, 8),
    "inherited-post-super-holder-before": (None, None),
    "own-fields-branch-same-order": (21132117, 21132117),
    "own-fields-branch-overwrite": (21172121, 21172121),
    "own-fields-branch-nested": (212234256, 212234256),
    "inherited-own-fields-branch-base": (212234, 212234),
    "inherited-own-fields-branch-leaf": (212214, 212214),
    "inherited-own-fields-branch-arguments": (2232562896, 2232562896),
    "own-fields-branch-order": (12, 12),
    "inherited-own-fields-branch-missing": (21, 21),
    "inherited-own-fields-branch-early": (2223, 2223),
    "own-fields-branch-observed": (102, 102),
    "own-fields-branch-loop": (21, 21),
    "inherited-own-fields-branch-before-super": (1719, 1719),
    "inherited-branch-helper": (80050, 80050),
    "inherited-own-fields-branch-unused-ambient": (1, 1),
    "inherited-own-fields-shared": (214218, 214218),
    "inherited-own-fields-leaf": (216, 216),
    "inherited-own-fields-external": (211, 211),
    "inherited-own-fields-chain": (234, 234),
    "inherited-own-fields-clear": (1111, 1111),
    "inherited-own-fields-empty": (0, 0),
    "inherited-own-fields-conditional": (1, 1),
    "inherited-own-fields-added": (12, 12),
    "inherited-own-fields-empty-added": (1, 1),
    "inherited-own-fields-grandchild-added": (2, 2),
    "inherited-own-fields-sibling-added": (12, 12),
    "inherited-own-fields-leaf-collision": (1, 1),
    "inherited-own-fields-before-store": (2, 2),
    "inherited-own-fields-ancestor-write": (2, 2),
    "inherited-own-fields-implicit": (1, 1),
    "inherited-own-fields-loop": (11, 11),
    "static-call-arguments": (1022, 1022),
    "static-call-capture": (16, 16),
    "static-call-getter": (107, 107),
    "static-call-unused": (7, 7),
    "static-call-ambient": (7, 7),
    "static-call-duplicate": (9, 9),
    "static-call-accessor": (9, 8),
    "static-call-replaced": (9, 9),
    "static-call-detached": (7, 7),
    "static-call-new-this": (7, 7),
    "static-call-reserved": (7, 7),
    "inherited-static-call": (7, 7),
    "nested-helper-constructor": (8, 8),
    "inherited-nested-helper-distinct": (80, 80),
    "nested-helper-method": (16, 16),
    "nested-helper-chain": (83, 83),
    "nested-helper-shared": (38, 38),
    "nested-helper-diamond": (36, 36),
    "nested-helper-order": (1022, 1022),
    "nested-helper-changing": (9, 9),
    "nested-helper-writer": (8, 8),
    "nested-helper-identity": (8, 8),
    "nested-helper-excess": (8, 8),
    "nested-helper-primitive": (8, 8),
    "nested-helper-receiver": (7, 7),
    "nested-helper-effect": (7, 7),
    "nested-helper-newtarget": (8, 8),
    "nested-helper-recursive": (7, 7),
    "inherited-helper-distinct": (80, 80),
    "inherited-helper-chain": (83, 83),
    "inherited-helper-siblings": (30104, 30104),
    "inherited-helper-order": (1012123, 1012123),
    "inherited-helper-order-values": (1010013, 1010013),
    "inherited-helper-changing": (9, 9),
    "inherited-helper-identity": (1, 1),
    "inherited-helper-effect": (7, 7),
    "inherited-helper-receiver": (7, 7),
    "inherited-helper-newtarget": (8, 8),
    "captured-helper-method": (8, 8),
    "inherited-helper-method": (16, 16),
    "captured-helper-constructor": (8, 8),
    "captured-helper-order": (1022, 1022),
    "captured-helper-replaced": (9, 9),
    "captured-helper-writer": (7, 7),
    "inherited-helper-ambient": (7, 7),
    "captured-helper-identity": (1, 1),
    "captured-helper-receiver": (7, 7),
    "captured-helper-excess": (8, 8),
    "captured-helper-nested": (8, 8),
    "inherited-helper-constructor": (8, 8),
    "inherited-super-helper": (8, 8),
    "inherited-super-nearest": (102, 102),
    "inherited-super-receiver": (15, 15),
    "inherited-super-chain": (83, 83),
    "inherited-super-order": (112123, 112123),
    "inherited-super-missing": (7, 7),
    "inherited-super-replaced": (9, 9),
    "inherited-super-identity": (1, 1),
    "inherited-super-home": (7, 7),
    "inherited-super-capture": (7, 7),
    "inherited-super-dynamic": (7, 7),
    "inherited-super-foreign": (9, 9),
    "inherited-super-target-cfg": (7, 7),
    "inherited-post-super-method": (7, 7),
    "inherited-post-super-override": (10, 10),
    "inherited-post-super-order": (11212, 11212),
    "inherited-post-super-chain": (24, 24),
    "inherited-post-super-foreign": (9, 9),
    "inherited-post-super-before": (None, None),
    "inherited-post-super-dynamic": (7, 7),
    "inherited-post-super-replaced": (9, 9),
    "override-constructor": (15, 15),
    "override-middle": (21, 21),
    "override-leaf": (28, 28),
    "override-distinct": (27, 27),
    "override-ambient": (7, 7),
    "override-shadowed-receiver": (7, 7),
    "override-hidden-ancestor": (7, 7),
    "override-different-leaves": (44, 44),
    "override-unused-constructor": (7, 7),
    "inherited-method-leaf": (14, 14),
    "inherited-method-unused-constructor": (7, 7),
    "inherited-method-leaf-shadow": (7, 7),
    "inherited-method-base-shadow": (7, 7),
    "inherited-method": (7, 7),
    "inherited-method-chain": (14, 14),
    "inherited-method-instances": (273, 273),
    "inherited-method-constructor": (7, 7),
    "inherited-method-override": (21, 21),
    "inherited-method-ambient": (7, 7),
    "inherited-method-getter": (8, 8),
    "inherited-method-shadow": (9, 9),
    "local-holder-method": (8, 8),
    "local-holder-arrow": (8, 8),
    "local-holder-branches": (82, 82),
    "local-holder-order": (12, 12),
    "local-holder-replaced": (9, 9),
    "local-holder-alias": (9, 9),
    "local-holder-detached": (8, 8),
    "local-holder-receiver": (8, 8),
    "local-holder-ambient": (7, 7),
    "local-holder-global": (8, 8),
    "global-holder-methods": (81, 81),
    "global-holder-chain": (8, 8),
    "global-holder-order": (122, 122),
    "global-holder-dispatch": (38, 38),
    "global-holder-surplus": (8, 8),
    "global-holder-early": (None, None),
    "global-holder-indirect-early": (None, None),
    "global-holder-prefix-call": (8, 8),
    "global-holder-late-slot": (8, 8),
    "global-holder-replaced": (9, 9),
    "global-holder-slot-replaced": (9, 9),
    "global-holder-alias": (9, 9),
    "global-holder-detached": (8, 8),
    "global-holder-identity": (1, 1),
    "global-holder-receiver": (8, 8),
    "global-holder-ambient": (7, 7),
    "local-helper-arguments": (8, 8),
    "local-helper-branches": (82, 82),
    "local-helper-order": (122, 122),
    "local-helper-values": (122, 122),
    "local-helper-replaced": (9, 9),
    "local-helper-ambient": (7, 7),
    "local-helper-receiver": (7, 7),
    "local-helper-dynamic-key": (7, 7),
    "bootstrap-r": (7, 7),
    "bootstrap-config-r-defaults": (7, 7),
    "bootstrap-config-r-h-defaults": (7, 7),
    "empty": (7, 7),
    "number": (92, 92),
    "method": (92, 92),
    "method-arguments": (3737, 3737),
    "method-branches": (3434, 3434),
    "method-loop": (5555, 5555),
    "method-dispatch": (11131321, 11131321),
    "method-increment-dispatch": (11131321, 11131321),
    "method-decrement-dispatch": (11090921, 11090921),
    "method-counter-ambient": (7, 7),
    "method-dispatch-ambient": (7, 7),
    "method-dispatch-shadow": (7, 7),
    "method-dispatch-throw": (7, 7),
    "method-throw-ambient": (7, 7),
    "method-throw-object": (7, 7),
    "method-throw-parameter": (7, 7),
    "method-throw-default": (7, 7),
    "method-branch-ambient": (7, 7),
    "method-branch-shadow": (7, 7),
    "constructor-branch": (7, 7),
    "static-branch": (7, 7),
    "method-empty": (7, 7),
    "method-chain": (92, 92),
    "method-chain-empty": (7, 7),
    "method-chain-effects": (7437, 7437),
    "method-chain-order": (264, 264),
    "method-chain-replace": (9, 9),
    "method-chain-argument-replace": (79, 79),
    "method-chain-identity": (1, 1),
    "method-chain-argument-receiver": (9, 9),
    "method-chain-return-receiver": (7, 7),
    "method-chain-cycle": (7, 7),
    "method-shadow": (9, 9),
    "method-extracted": (1, 1),
    "method-constructor-read": (1, 1),
    "method-constructor-write": (9, 9),
    "method-constructor-call": (8, 8),
    "method-constructor-order": (132, 132),
    "method-constructor-chain": (48, 48),
    "method-constructor-constant": (7, 7),
    "method-constructor-identity": (1, 1),
    "method-constructor-argument-replace": (79, 79),
    "method-constructor-return-object": (9, 9),
    "method-constructor-argument-receiver": (9, 9),
    "method-constructor-return-receiver": (7, 7),
    "method-constructor-self-replace": (79, 79),
    "method-self-replace": (79, 79),
    "method-duplicate": (9, 9),
    "method-captured": (7, 7),
    "method-captured-class-name": (11, 11),
    "method-captured-class-key": (11, 11),
    "method-captured-class-replaced": (9, 9),
    "method-captured-class-writer": (7, 7),
    "method-captured-class-identity": (1, 1),
    "method-captured-object": (7, 7),
    "method-captured-class-cycle": (7, 7),
    "method-captured-class-ambient": (7, 7),
    "method-dynamic": (7, 7),
    "method-return-object": (9, 9),
    "helper-override": (0, 1),
    "helper-late": (0, 1),
    "helper-global-alias": (0, 1),
    "prototype-late": (9, 9),
    "prototype-alias": (11, 11),
    "field-initializer": (7, 7),
    "inherited": (7, 7),
    "inherited-explicit": (7, 7),
    "inherited-order": (312, 312),
    "inherited-chain": (13, 13),
    "inherited-base-instance": (27, 27),
    "inherited-new-target": (8, 8),
    "inherited-replacement": (9, 9),
    "inherited-number-return": (7, 7),
    "inherited-fields": (9, 9),
    "inherited-missing-super": (None, None),
    "inherited-double-super": (None, None),
    "inherited-dispatch": (118, 118),
    "bootstrap-base": (7, 7),
    "static-getter": (1, 1),
    "constructor-identity": (1, 1),
    "descriptor": (0, 0),
    "static-constant": (7, 7),
    "static-defaults": (923, 923),
    "static-defaults-chain": (12423, 12423),
    "static-default-fields": (7, 7),
    "static-chain": (1, 1),
    "static-methods": (192, 192),
    "static-setter": (7, 7),
    "static-duplicate": (9, 9),
    "static-write": (9, 9),
    "static-alias-write": (11, 11),
    "static-effect": (79, 79),
    "static-identity": (1, 1),
    "static-descriptor": (0, 0),
    "static-receiver": (1, 1),
    "static-foreign-receiver": (11, 11),
    "static-captured": (7, 7),
    "static-dynamic": (7, 7),
    "static-cycle": (7, 7),
    "static-repeated": (11, 11),
    "static-global-effect": (73, 73),
    "static-forward-chain": (1, 1),
    "static-order": (1323, 1323),
    "static-unused-setter": (7, 7),
    "static-name": (1, 0),
    "static-length": (7, 0),
    "static-home": (1, 0),
    "static-caller": (1, 1),
    "static-arguments": (1, 1),
    "bootstrap-config-defaults": (7, 7),
    "static-throw-unused": (7, 7),
    "static-throw-unused-chain": (7, 7),
    "static-throw-literal": (7, 7),
    "static-throw-error": (7, 7),
    "static-throw-chain": (7, 7),
    "static-throw-ambient": (7, 7),
    "static-throw-object": (7, 7),
    "static-error-return": (7, 7),
    "static-error-replaced": (7, 7),
    "static-error-coercion": (7, 7),
    "static-error-method": (7, 7),
    "receiver-defaults": (923, 923),
    "instance-defaults": (72, 72),
    "instance-default-replacement": (1, 1),
    "receiver-default-dispatch": (11131321, 11131321),
    "receiver-default-shadow": (7, 7),
    "receiver-default-write": (7, 7),
    "receiver-default-identity": (7, 7),
    "receiver-default-inherited": (7, 7),
}
GLOBAL_HOLDERS = {
    "local-holder-global",
    "global-holder-methods",
    "global-holder-chain",
    "global-holder-order",
    "global-holder-dispatch",
}
POSITIVES = GLOBAL_HOLDERS | {
    "inherited-own-fields-iterate-forward-direct",
    "inherited-own-fields-iterate-forward-captured",
    "inherited-own-fields-iterate-forward-holder",
    "inherited-own-fields-iterate-forward-arguments",
    "inherited-own-fields-iterate-forward-inherited",
    "inherited-own-fields-iterate-borrow-direct",
    "inherited-own-fields-iterate-borrow-captured",
    "inherited-own-fields-iterate-borrow-holder",
    "inherited-own-fields-iterate-borrow-two-arguments",
    "inherited-own-fields-iterate-borrow-inherited",
    "inherited-own-fields-iterate-borrow-arguments",
    "inherited-own-fields-iterate-getter-direct",
    "inherited-own-fields-iterate-getter-method",
    "inherited-own-fields-iterate-getter-nested",
    "inherited-own-fields-iterate-getter-chain",
    "inherited-own-fields-iterate-getter-arguments",
    "inherited-own-fields-iterate-getter-fresh-empty",
    "inherited-own-fields-iterate-method-read",
    "inherited-own-fields-iterate-method-update",
    "inherited-own-fields-iterate-method-primitive",
    "inherited-own-fields-iterate-method-nested",
    "inherited-own-fields-iterate-method-nearest",
    "inherited-own-fields-iterate-method-arguments",
    "inherited-own-fields-iterate-helper-captured",
    "inherited-own-fields-iterate-helper-holder",
    "inherited-own-fields-iterate-helper-nested",
    "inherited-own-fields-iterate-helper-distinct",
    "inherited-own-fields-iterate-helper-arguments",
    "inherited-own-fields-iterate-break",
    "inherited-own-fields-iterate-shared",
    "inherited-own-fields-iterate-branches",
    "inherited-own-fields-iterate-empty",
    "inherited-own-fields-iterate-effects",
    "inherited-own-fields-iterate-repeated",
    "captured-holder-method",
    "captured-holder-sibling",
    "inherited-captured-holder-shared",
    "inherited-captured-holder-order",
    "inherited-captured-holder-distinct",
    "inherited-captured-holder-chain",
    "captured-holder-shared",
    "captured-holder-unused-capture",
    "inherited-post-super-holder",
    "inherited-post-super-holder-shared",
    "inherited-post-super-holder-chain",
    "inherited-post-super-holder-branch-values",
    "inherited-post-super-holder-order",
    "own-fields-branch-same-order",
    "own-fields-branch-overwrite",
    "own-fields-branch-nested",
    "inherited-own-fields-branch-base",
    "inherited-own-fields-branch-leaf",
    "inherited-own-fields-branch-arguments",
    "inherited-branch-helper",
    "constructor-branch",  # Preserve the original source; its linear-only refusal is retired.
    "inherited-own-fields-shared",
    "inherited-own-fields-leaf",
    "inherited-own-fields-external",
    "inherited-own-fields-chain",
    "inherited-own-fields-clear",
    "inherited-own-fields-empty",
    "static-call-arguments",
    "static-call-capture",
    "static-call-getter",
    "static-call-unused",
    "nested-helper-constructor",
    "inherited-nested-helper-distinct",
    "nested-helper-method",
    "nested-helper-chain",
    "nested-helper-shared",
    "nested-helper-diamond",
    "nested-helper-order",
    "inherited-helper-distinct",
    "inherited-helper-chain",
    "inherited-helper-siblings",
    "inherited-helper-order-values",
    "inherited-helper-constructor",
    "captured-helper-method",
    "inherited-helper-method",
    "captured-helper-constructor",
    "captured-helper-order",
    "inherited-dispatch",
    "inherited-super-nearest",
    "inherited-super-receiver",
    "inherited-super-chain",
    "inherited-super-order",
    "inherited-post-super-method",
    "inherited-post-super-override",
    "inherited-post-super-order",
    "inherited-post-super-chain",
    "override-constructor",
    "override-middle",
    "override-leaf",
    "override-distinct",
    "inherited-method-override",
    "inherited-method-leaf",
    "inherited-method",
    "inherited-method-chain",
    "inherited-method-instances",
    "inherited-method-constructor",
    "inherited-base-instance",
    "inherited-explicit",
    "inherited-order",
    "inherited-chain",
    "local-holder-method",
    "local-holder-arrow",
    "local-holder-branches",
    "local-holder-order",
    "local-helper-arguments",
    "local-helper-branches",
    "local-helper-values",
    "local-helper-order",
    "empty",
    "number",
    "method",
    "method-arguments",
    "method-branches",
    "method-loop",
    "method-dispatch",
    "method-increment-dispatch",
    "method-decrement-dispatch",
    "method-empty",
    "method-chain",
    "method-chain-empty",
    "method-chain-effects",
    "method-chain-order",
    "method-chain-cycle",
    "method-constructor-call",
    "method-constructor-order",
    "method-constructor-chain",
    "method-constructor-constant",
    "method-captured-class-name",
    "method-captured-class-key",
    "static-constant",
    "static-defaults",
    "static-defaults-chain",
    "static-chain",
    "static-methods",
    "static-repeated",
    "static-forward-chain",
    "static-order",
    "receiver-defaults",
    "instance-defaults",
    "receiver-default-dispatch",
    "static-throw-unused",
    "static-throw-unused-chain",
}
PREPARATION = "--ctnative-specialize-class-initialization="
PREPARED_ONLY = {
    "inherited-own-fields-iterate-borrow-mixed-literal",
    "inherited-own-fields-iterate-borrow-missing-argument",
    "captured-holder-unused",
    "inherited-helper-order",
    "override-different-leaves",
    "bootstrap-r",
    "method-dispatch-throw",
    "method-throw-default",
    "static-throw-literal",
    "static-throw-error",
    "static-throw-chain",
}


def check_constructed_methods(args):
    checked = refused = 0
    for name, expected in {
        "plain-method": 7,
        "plain-before-store": None,
        "plain-constructor-before-store": None,
        "plain-borrowed-write": 9,
        "plain-self-replace": 79,
        "plain-constructor-write": 7,
        "plain-detached": 1,
        "plain-prototype": 83,
        "plain-prototype-identity": 1,
        "plain-prototype-replace": 79,
        "plain-prototype-self-replace": 79,
        "plain-prototype-before-store": None,
    }.items():
        source = args.fixtures / f"{name}.js"
        for command in ([args.node, "-e", NODE, str(source)], [args.reference, str(source)]):
            result = run(command, success=expected is not None)
            if expected is not None and result.stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}: constructed method observation changed")
        if name in ("plain-method", "plain-prototype"):
            checked += check_native(args, source, name, expected)
            continue
        raw = args.work / f"{name}.raw.mlir"
        run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        functions = len(FUNCTION.findall(raw.read_text()))
        for optimize in (False, True):
            result = run(
                [
                    args.opt,
                    str(raw),
                    "--ctjs-resolve-globals",
                    "--ctjs-lift-to-scf",
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                ]
            )
            check_refusal(name, result.stdout, functions)
            refused += 1
    return checked, refused


def check_refusal(name, text, functions):
    remaining = len(FUNCTION.findall(text))
    reasons = REFUSAL.findall(text)
    if (
        remaining + len(NATIVE.findall(text)) != functions
        or len(reasons) != remaining
        or not all(reasons)
        or not re.search(r"ctjs.func @_script_\$0\(.*ctnative.not_native", text)
        or re.search(r"emitc.func @main\(", text)
    ):
        raise RuntimeError(f"{name}: lost named native refusal boundary\n{text}")


def prepare(args, name, source, manifest, *, success, options="", diagnostic=""):
    config = args.work / f"{name}.json"
    output = args.work / f"{name}.prepared.mlir"
    config.write_text(json.dumps(manifest, indent=2) + "\n")
    result = run(
        [
            args.opt,
            str(source),
            PREPARATION + f"manifest={config} {options}",
            "-o",
            str(output),
        ],
        success=success,
    )
    if not success and (
        result.returncode != 1
        or "error:" not in result.stderr
        or diagnostic not in result.stderr
        or (output.exists() and output.read_text())
    ):
        raise RuntimeError(f"{name}: preparation failed without a diagnostic or emitted partial IR")
    return output


def check_ancestry_inputs(args, source, manifest):
    text = source.read_text()
    load = re.search(
        r'(%\w+) = "ctjs.load_global"\(\) <\{name = "__ctbrowser_class_heritage"', text
    )
    if not load:
        raise RuntimeError("ancestry control lost its original heritage helper")
    call = re.search(r'^.*"ctjs.call"\(' + re.escape(load[1]) + r", [^\n]+\n", text, re.M)
    operands = call[0].split("(", 1)[1].split(")", 1)[0].split(", ")
    _, _, derived, base, prototype = operands
    completion = re.search(
        r'^.*"ctjs.call"\(%\w+, %\w+, ' + re.escape(base) + r"\)[^\n]+\n", text, re.M
    )
    if not completion or len(operands) != 5:
        raise RuntimeError("ancestry control lost its source completion order")
    cases = {
        "self": text.replace(call[0], call[0].replace(", " + base + ",", ", " + derived + ",")),
        "late-base": text.replace(completion[0], "").replace(call[0], call[0] + completion[0]),
        "duplicate": text.replace(
            call[0], call[0] + re.sub(r"%\w+ =", "%duplicate_heritage =", call[0], count=1)
        ),
        "different-prototype": text.replace(
            call[0],
            '    %unattached = "ctjs.create_object"() : () -> !ctjs.value\n'
            + call[0].replace(", " + prototype + ")", ", %unattached)"),
        ),
    }
    for label, changed in cases.items():
        path = args.work / f"ancestry-{label}.mlir"
        path.write_text(changed)
        prepare(
            args,
            f"ancestry-{label}",
            path,
            dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
            success=False,
            diagnostic="class heritage",
        )
    prepare(
        args,
        "ancestry-undeclared",
        source,
        dict(manifest, initial_intrinsics=["__ctbrowser_class_defined"]),
        success=False,
        diagnostic="class heritage needs its declared direct helper",
    )
    return len(cases) + 1


def check_super_inputs(args, source, manifest):
    text = source.read_text()
    guard = re.search(r'(%\w+) = "ctjs.create_cell"\((%\w+)\)', text)
    write = re.search(
        r'^.*"ctjs.cell_set"\(' + re.escape(guard[1]) + r", (%\w+)\)[^\n]+\n", text, re.M
    )
    passing = re.search(r'^.*"ctjs.pass_new_target"[^\n]+\n', text, re.M)
    if not write or not passing:
        raise RuntimeError("explicit super control lost its original guard or new.target")
    cases = {
        "true-guard": text.replace("#ctjs.boolean<false>", "#ctjs.boolean<true>", 1),
        "false-write": text.replace(write[0], write[0].replace(write[1], guard[2])),
        "missing-new-target": text.replace(passing[0], ""),
        "escaped-guard": text.replace(
            write[0],
            write[0]
            + '    "ctjs.store_global"('
            + guard[1]
            + ') <{name = "leaked_guard"}> : (!ctjs.value) -> ()\n',
        ),
        "unknown-effect": text.replace(
            passing[0],
            '    %ambient = "ctjs.load_global"() <{name = "unproved", typeof_lookup = false}> : () -> !ctjs.value\n'
            + passing[0],
        ),
    }
    for label, changed in cases.items():
        path = args.work / f"super-{label}.mlir"
        path.write_text(changed)
        prepare(
            args,
            f"super-{label}",
            path,
            dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
            success=False,
        )
    for name in ("__ctbrowser_bind_this", "__ctbrowser_init_fields"):
        prepare(
            args,
            f"super-undeclared-{name}",
            source,
            dict(
                manifest,
                initial_intrinsics=[
                    item for item in manifest["initial_intrinsics"] if item != name
                ],
            ),
            success=False,
            diagnostic="super initialization requires declared helper identities",
        )
    prepare(
        args,
        "super-budget",
        source,
        manifest,
        success=False,
        options="max-steps=0",
        diagnostic="work budget exhausted",
    )
    return len(cases) + 3


def check_super_roots(args, source, manifest, diagnostic):
    count = 0

    def rooted(match):
        nonlocal count
        count += 1
        return (
            f"{match[1]}%super_root{count} = ctjs.constant #ctjs.undefined\n"
            f"{match[1]}ctjs.root %super_root{count} in {match[2]}\n" + match[0]
        )

    text, roots = re.subn(
        r'(^[ \t]*)"ctjs.frame_exit"\((%\w+)\)', rooted, source.read_text(), flags=re.M
    )
    if not roots:
        raise RuntimeError("super target control lost its original frame exits")
    path = args.work / "super-rooted-target.mlir"
    path.write_text(text)
    prepare(
        args,
        "super-rooted-target",
        path,
        dict(manifest, module_sha256=host.fingerprint(args.opt, path)),
        success=False,
        diagnostic=diagnostic,
    )


def check_proof_inputs(args, source, manifest, prepared, name):
    text = source.read_text()
    nested = args.work / "nested.mlir"
    changed, count = re.subn(
        r"(^[ \t]*ctjs.return [^\n]*\n)",
        "    builtin.module {\n"
        "      ctjs.func private @external$999() -> !ctjs.value attributes {upvalue_count = 0 : i32}\n"
        "    }\n\\1",
        text,
        count=1,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("nested function control could not find the script return")
    nested.write_text(changed)
    prepare(
        args,
        "nested",
        nested,
        dict(manifest, module_sha256=host.fingerprint(args.opt, nested)),
        success=False,
    )
    duplicate = args.work / "duplicate-closure.mlir"
    changed, count = re.subn(
        r"(^[ \t]*)%\w+( = ctjs.create_closure[^\n]*\n)",
        r"\g<0>\1%duplicate\2",
        text,
        count=1,
        flags=re.M,
    )
    if count != 1:
        raise RuntimeError("duplicate closure control could not find its creation")
    duplicate.write_text(changed)
    prepare(
        args,
        "duplicate-closure",
        duplicate,
        dict(manifest, module_sha256=host.fingerprint(args.opt, duplicate)),
        success=False,
    )
    forged = args.work / "forged.mlir"
    changed, count = re.subn(
        r"(\bctjs.func[^\n]*\battributes \{)",
        r'\1ctnative.not_native = "forged", ctnative.forged = true, ',
        text,
    )
    if count != len(FUNCTION.findall(text)):
        raise RuntimeError("forged annotation control did not mark every source function")
    forged.write_text(changed)
    signed = dict(manifest, module_sha256=host.fingerprint(args.opt, forged))
    prepare(args, "forged-untrusted", forged, dict(signed, initial_intrinsics=[]), success=False)
    output = prepare(args, "forged-trusted", forged, signed, success=True)
    if output.read_text() != prepared.read_text():
        raise RuntimeError("supplied native annotations survived successful live reanalysis")

    return check_proof_budget(args, source, manifest, prepared, name)


def check_proof_budget(args, source, manifest, prepared, name):
    # Find and pin the exact first complete census. Each unsuccessful limit
    # must withhold the entire rewrite; successful limits produce identical IR.
    low, high = 0, 100000
    while high - low > 1:
        limit = (low + high) // 2
        output = args.work / f"cutoff-{limit}.mlir"
        result = subprocess.run(
            [
                args.opt,
                str(source),
                PREPARATION + f"manifest={args.work / (name + '.json')} max-steps={limit}",
                "-o",
                str(output),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if result.returncode == 0:
            if output.read_text() != prepared.read_text():
                raise RuntimeError("work limit changed successful class initialization IR")
            high = limit
        else:
            if (
                result.returncode != 1
                or "class initialization work budget exhausted" not in result.stderr
                or (output.exists() and output.read_text())
            ):
                raise RuntimeError(f"work limit {limit} crashed or published partial proof")
            low = limit
    prepare(args, "last-cutoff", source, manifest, success=False, options=f"max-steps={low}")
    output = prepare(
        args, "first-complete", source, manifest, success=True, options=f"max-steps={high}"
    )
    if output.read_text() != prepared.read_text():
        raise RuntimeError("first complete work budget changed the prepared program")
    return high


def check_overflow_input(args, source):
    # This malformed closure index is rejected by the parser, before the host
    # fingerprint or preparation proof can inspect it.
    overflow = args.work / "overflow-closure.mlir"
    changed, count = re.subn(
        r"(ctjs.create_closure %\w+\[)\d+(\])",
        r"\g<1>4294967297\2",
        source.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("overflow closure control could not find its creation")
    overflow.write_text(changed)
    output = args.work / "overflow-parsed.mlir"
    result = run([args.opt, str(overflow), "-o", str(output)], success=False)
    if (
        result.returncode != 1
        or "integer constant out of range for attribute" not in result.stderr
        or (output.exists() and output.read_text())
    ):
        raise RuntimeError("overflow closure index escaped its parser refusal")


def check_getter_parent(args, source, manifest, prepared):
    # A numeric function index is insufficient: make_closure requires the
    # current function's closure, not an arbitrary value of the same IR type.
    forged = args.work / "getter-enclosing-closure.mlir"
    changed, count = re.subn(
        r"(?P<prefix>(?P<getter>%\w+) = ctjs.create_closure )%arg2"
        r"(?P<index>\[\d+\] this )(?P<undefined>%\w+)"
        r"(?P<tail>\n[ \t]*%\w+ = ctjs.constant #ctjs.undefined\n"
        r'[ \t]*ctjs.define_accessor "NAME" on %\w+ get (?P=getter) set %\w+)',
        lambda match: (
            match["prefix"]
            + match["undefined"]
            + match["index"]
            + match["undefined"]
            + match["tail"]
        ),
        source.read_text(),
        count=1,
    )
    if count != 1:
        raise RuntimeError("getter closure control could not find its NAME definition")
    forged.write_text(changed)
    prepare(
        args,
        "getter-enclosing-closure",
        forged,
        dict(manifest, module_sha256=host.fingerprint(args.opt, forged)),
        success=False,
    )
    text = source.read_text()
    definition = re.search(r'ctjs.define_accessor "NAME" on (%\w+) get (%\w+) set (%\w+)', text)
    if not definition:
        raise RuntimeError("getter home control lost its NAME definition")
    constructor, getter, undefined = definition.groups()
    home = re.search(rf"(?m)^([ \t]*ctjs.set_property {getter}\[%\w+\], ){constructor}$", text)
    if not home:
        raise RuntimeError("getter home control lost its source assignment")
    for label, replacement in (
        ("wrong-getter-home", home[1] + undefined),
        ("repeated-getter-home", home[0] + "\n" + home[0]),
    ):
        altered = args.work / f"{label}.mlir"
        altered.write_text(text[: home.start()] + replacement + text[home.end() :])
        prepare(
            args,
            label,
            altered,
            dict(manifest, module_sha256=host.fingerprint(args.opt, altered)),
            success=False,
        )
    closures = re.findall(
        r"(%\w+) = ctjs.create_closure %\w+\[(\d+)\] this %\w+\n"
        r"\s*%\w+ = ctjs.constant #ctjs.undefined\n"
        r'\s*ctjs.define_accessor "[^"]+" on %\w+ get \1 set %\w+',
        text,
    )
    symbols = re.compile(r"^\s*ctjs\.func\b[^@\n]*@([^\s(]+)", re.M)
    functions = symbols.findall(text)
    indices = {index for _, index in closures}
    getters = {name for name in functions if name.rsplit("$", 1)[-1] in indices}
    if len(getters) != 2 or symbols.findall(prepared.read_text()) != [
        name for name in functions if name not in getters
    ]:
        raise RuntimeError("class preparation did not remove exactly its two expanded getters")

    # Symbol references in the module's own attributes and nested operations
    # must retain the definition, even when closure uses are fully expanded.
    attribute = f"test.getter_ref = @{sorted(getters)[0]}"
    for label, pattern, replacement in (
        ("module", r"^module attributes \{", rf"\g<0>{attribute}, "),
        ("function", r"(\bctjs.func[^\n]*\battributes \{)", rf"\g<0>{attribute}, "),
        (
            "operation",
            r"^\s*%\w+ = ctjs.constant #ctjs.undefined$",
            rf"\g<0> {{{attribute}}}",
        ),
        ("unresolved", r"^module attributes \{", rf"\g<0>{attribute}::@missing, "),
    ):
        changed, count = re.subn(pattern, replacement, text, count=1, flags=re.M)
        if count != 1:
            raise RuntimeError(f"getter {label} reference control lost its attribute site")
        altered = args.work / f"getter-{label}-reference.mlir"
        altered.write_text(changed)
        prepare(
            args,
            f"getter-{label}-reference",
            altered,
            dict(manifest, module_sha256=host.fingerprint(args.opt, altered)),
            success=False,
            diagnostic=(
                "class initialization has an unresolved symbol reference"
                if label == "unresolved"
                else "static getter has a remaining symbol reference"
            ),
        )
    return 7


def check_class_capture_inputs(args, source, manifest):
    text = source.read_text()
    capture = re.search(
        r"^    %\w+ = ctjs.create_closure %arg2\[\d+\] this %\w+ captures (%\w+)[ \t]*$",
        text,
        re.M,
    )
    if not capture:
        raise RuntimeError("captured class control lost its original method capture")
    cell = capture[1]
    # SSA names are local to functions: keep cell mutations in the owner only.
    owner = next(
        match[0]
        for match in re.finditer(r"^  ctjs.func\b[^\n]*\n.*?^  }\n", text, re.M | re.S)
        if capture[0] in match[0]
    )
    stores = list(re.finditer(r"^    ctjs.cell_set " + cell + r", (%\w+)[ \t]*$", owner, re.M))
    if (
        len(stores) != 2
        or stores[0][1] != stores[1][1]
        or not stores[0].end() < owner.index(capture[0]) < stores[1].start()
    ):
        raise RuntimeError("captured class control lost its two identical constructor stores")
    initial = re.search(re.escape(cell) + r" = ctjs.create_cell (%\w+)", owner)
    if not initial:
        raise RuntimeError("captured class control lost its local cell initializer")
    final = stores[1]
    variants = {
        "changed-cell": owner[: final.start()]
        + f"    ctjs.cell_set {cell}, {initial[1]}"
        + owner[final.end() :],
        "cyclic-cell": owner[: final.start()]
        + f"    %capture_cycle = ctjs.cell_get {cell}\n"
        + f"    ctjs.cell_set {cell}, %capture_cycle"
        + owner[final.end() :],
        "early-cell-read": owner[: stores[0].start()]
        + f"    %capture_early = ctjs.cell_get {cell}\n"
        + owner[stores[0].start() :],
        # Keep the original constructor uses while a second cell hides a write.
        "chained-cell-alias": owner[: final.end()]
        + f"\n    %capture_alias_value = ctjs.cell_get {cell}\n"
        + "    %capture_alias_cell = ctjs.create_cell %capture_alias_value\n"
        + "    %capture_alias = ctjs.cell_get %capture_alias_cell\n"
        + '    %capture_prototype = ctjs.constant #ctjs.string<"prototype">\n'
        + f"    ctjs.set_property %capture_alias[%capture_prototype], {initial[1]}"
        + owner[final.end() :],
    }
    for label, changed in variants.items():
        altered = args.work / f"capture-{label}.mlir"
        altered.write_text(text.replace(owner, changed, 1))
        prepare(
            args,
            f"capture-{label}",
            altered,
            dict(manifest, module_sha256=host.fingerprint(args.opt, altered)),
            success=False,
            diagnostic=(
                "class constructor has an observable use outside its setup"
                if label == "chained-cell-alias"
                else ""
            ),
        )
    return len(variants)


def check_executable(args, name, native, expected):
    text = native.read_text()
    if any(token in text for token in ("ctjs.func", "ctnative.not_native", "ctjs.skipped")):
        raise RuntimeError(f"{name}: trusted class did not lower completely\n{text}")
    clean = args.work / f"{name}.clean.mlir"
    run(
        [
            args.opt,
            str(native),
            "--pass-pipeline=builtin.module(emitc.func(canonicalize,convert-scf-to-emitc,convert-arith-to-emitc,canonicalize,ctnative-prune-dead-stores,canonicalize))",
            "-o",
            str(clean),
        ]
    )
    deduced = args.work / f"{name}.deduced.mlir"
    run([args.opt, str(clean), "--ctnative-print-deduced", "-o", str(deduced)])
    checked = 0
    for layout, module in (("explicit", clean), ("deduced", deduced)):
        cpp = run([args.translate, "--mlir-to-cpp", str(module)]).stdout
        if any(token in cpp for token in ("ctbrowser::", '"prototype"', '"__home"')):
            raise RuntimeError(f"{name}: native class retained runtime or prototype storage")
        file = args.work / f"{name}.{layout}.cpp"
        file.write_text(cpp)
        for index, compiler in enumerate(find_compilers()):
            binary = file.with_suffix(f".{index}")
            run([compiler, *FLAGS, str(file), "-o", str(binary)])
            if run([str(binary.resolve())]).stdout != f"a={expected}\n":
                raise RuntimeError(f"{name}/{layout}: native class observations differ")
            checked += 1
    return checked


def check_helper_guards(args, declaration):
    checked = refused = 0
    for name, (body, native_expected) in {
        "bootstrap-r-direct": (declaration + "var a = r(null) ? 9 : 7;\n", False),
        "bootstrap-r-declaration": (
            declaration + "function probe() { return r(null) ? 9 : 7; } var a = probe();\n",
            True,
        ),
        "bootstrap-r-local": (
            "function probe() { " + declaration + "return r(null) ? 9 : 7; } var a = probe();\n",
            True,
        ),
    }.items():
        source = args.fixtures / f"{name}.js"
        source.write_text(body)
        for command in ([args.node, "-e", NODE, str(source)], [args.reference, str(source)]):
            observed = run(command)
            if observed.stdout != "a=7\n":
                raise RuntimeError(f"{name}: original helper observation changed")
        raw = args.work / f"{name}.raw.mlir"
        run([args.translate, "--ctbrowser-js-to-ctjs", str(source), "-o", str(raw)])
        functions = len(FUNCTION.findall(raw.read_text()))
        if "ctjs.skipped" in raw.read_text() or not functions:
            raise RuntimeError(f"{name}: helper source was omitted")
        for optimize in (False, True):
            native = args.work / f"{name}.{optimize}.mlir"
            run(
                [
                    args.opt,
                    str(raw),
                    "--ctjs-resolve-globals",
                    "--ctjs-lift-to-scf",
                    f"--ctnative-lower-to-emitc=optimize={str(optimize).lower()}",
                    "-o",
                    str(native),
                ]
            )
            if optimize and native_expected:
                checked += check_executable(args, name, native, 7)
            else:
                check_refusal(name, native.read_text(), functions)
                refused += 1
    return checked, refused


def check_prototype_keys(args, prepared):
    # The original helper reads numeric [0] on its parameter. Change only that
    # IR key and the method name to distinguish disjoint keys from collisions.
    text = prepared.read_text()
    literal = "ctjs.constant #ctjs.number<0>"
    if text.count(literal) != 1 or text.count('#ctjs.string<"read">') != 2:
        raise RuntimeError("prototype key control lost its original method/key sites")
    checked = refused = 0
    for label, method, key in (
        ("empty", "read", 'ctjs.constant #ctjs.string<"">'),
        ("negative-zero", "0", "ctjs.constant #ctjs.number<9223372036854775808>"),
        ("nan", "NaN", "ctjs.constant #ctjs.number<9221120237041090560>"),
        ("infinity", "Infinity", "ctjs.constant #ctjs.number<9218868437227405312>"),
        ("negative-infinity", "-Infinity", "ctjs.constant #ctjs.number<18442240474082181120>"),
        ("large", "read", "ctjs.constant #ctjs.number<4906019910204099648>"),
        ("dynamic", "read", 'ctjs.load_global "key"'),
    ):
        name = "prototype-key-" + label
        source = args.work / f"{name}.mlir"
        source.write_text(
            text.replace(literal, key).replace('#ctjs.string<"read">', f'#ctjs.string<"{method}">')
        )
        native = args.work / f"{name}.native.mlir"
        run([args.opt, str(source), "--ctnative-lower-to-emitc", "-o", str(native)])
        if label == "empty":
            checked += check_executable(args, name, native, 7)
        else:
            check_refusal(name, native.read_text(), len(FUNCTION.findall(text)))
            if "its prototype method is also observed as a value" not in native.read_text():
                raise RuntimeError(f"{name}: literal or unknown key escaped the prototype census")
            refused += 1
    return checked, refused
