#!/usr/bin/env python3
"""Regenerate ctbrowser/lib/Shell/net/nfc_table.inc - the Unicode Annex #15
data the URL parser's UTS #46 step 2 (and its "the label must already be NFC"
validity criterion) normalise with.

Three tables, all from Python's own unicodedata, which carries the UCD:

  * the canonical decompositions, FULLY expanded here so the run time needs no
    recursion. Hangul is left out: it is algorithmic (Unicode 3.12) and would
    otherwise be 11,172 rows.
  * Canonical_Combining_Class, nonzero only.
  * the canonical pairs that RECOMPOSE. Full_Composition_Exclusion needs no
    second file: a pair is in the table only when NFC itself puts it back
    together, which is the property's definition rather than a copy of it.

    python3 tools/gen/nfc_table.py > ctbrowser/lib/Shell/net/nfc_table.inc
"""

import sys
import unicodedata

SBASE, LBASE, VBASE, TBASE = 0xAC00, 0x1100, 0x1161, 0x11A7
LCOUNT, VCOUNT, TCOUNT = 19, 21, 28
NCOUNT = VCOUNT * TCOUNT
SCOUNT = LCOUNT * NCOUNT


def canonical(cp):
    """One step of canonical decomposition, or ()."""
    d = unicodedata.decomposition(chr(cp))
    if not d or d.startswith("<"):
        return ()
    return tuple(int(x, 16) for x in d.split())


def full_decomposition(cp):
    out = []
    stack = [cp]
    while stack:
        c = stack.pop(0)
        d = canonical(c)
        if d:
            stack = list(d) + stack
        else:
            out.append(c)
    return tuple(out)


def main():
    decomps = []
    for cp in range(0x110000):
        if SBASE <= cp < SBASE + SCOUNT:
            continue  # Hangul is algorithmic
        if canonical(cp):
            decomps.append((cp, full_decomposition(cp)))
    ccc = []
    for cp in range(0x110000):
        c = unicodedata.combining(chr(cp))
        if c == 0:
            continue
        if ccc and ccc[-1][1] == cp - 1 and ccc[-1][2] == c:
            ccc[-1][1] = cp
        else:
            ccc.append([cp, cp, c])
    # The composition table, derived empirically from Python's own NFC so that
    # Full_Composition_Exclusion needs no second file: a canonical pair
    # composes only when NFC actually puts it back together.
    pairs = []
    for cp in range(0x110000):
        d = canonical(cp)
        if len(d) != 2:
            continue
        if unicodedata.normalize("NFC", chr(d[0]) + chr(d[1])) == chr(cp):
            pairs.append((d[0], d[1], cp))
    pairs.sort()

    data = []
    print("// --- NFC (Unicode Annex #15), for UTS #46 step 2 -----------------")
    print("// Canonical decompositions, FULLY expanded so the run time needs no")
    print("// recursion. Hangul is algorithmic and is not in the table.")
    print("constexpr nfd_entry nfd_table[] = {")
    for cp, d in decomps:
        at = len(data)
        data.extend(d)
        print("    {0x%X, %d, %d}," % (cp, at, len(d)))
    print("};")
    print("constexpr char32_t nfd_data[] = {")
    for at in range(0, len(data), 8):
        print("    " + "".join("0x%X, " % c for c in data[at : at + 8]).rstrip())
    print("};")
    print()
    print("// Canonical_Combining_Class, nonzero only.")
    print("constexpr ccc_range ccc_ranges[] = {")
    for first, last, c in ccc:
        print("    {0x%X, 0x%X, %d}," % (first, last, c))
    print("};")
    print()
    print("// The canonical pairs that RECOMPOSE - the composition exclusions")
    print("// are already gone, having been derived from NFC itself. Sorted by")
    print("// (starter, second) for the binary search.")
    print("constexpr compose_pair compose_pairs[] = {")
    for a, b, cp in pairs:
        print("    {0x%X, 0x%X, 0x%X}," % (a, b, cp))
    print("};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
