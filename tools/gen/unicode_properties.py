#!/usr/bin/env python3
"""Regenerate ctbrowser/lib/Script/regex_properties.inc - the Unicode Character
Database as the regular-expression engine's `\\p{...}` reads it (ECMA-262
22.2.2.9, UnicodeMatchProperty and UnicodeMatchPropertyValue).

Everything is code-point RANGES, sorted and disjoint, in one flat array with a
name index over it, so a lookup is one linear scan of the names at compile
time and one binary search per character at match time:

  * General_Category, from UnicodeData.txt, plus the eight groupings the
    specification's table 68 lists (L, LC, M, N, P, S, Z, C) and every alias
    PropertyValueAliases.txt gives them (`Letter`, `Lu`, `Uppercase_Letter`).
  * Script and Script_Extensions, from Scripts.txt and ScriptExtensions.txt,
    with their four-letter aliases. A code point with no Script_Extensions
    entry extends exactly its Script.
  * The binary properties of table 67 - DerivedCoreProperties.txt,
    PropList.txt, emoji-data.txt, DerivedNormalizationProps.txt (CWKCF) and
    UnicodeData.txt (Bidi_Mirrored) - with their PropertyAliases.txt aliases,
    and the three ECMAScript adds: Any, ASCII, Assigned.
  * The binary properties of STRINGS of table 69 (`v` flag), from
    emoji-sequences.txt and emoji-zwj-sequences.txt: the lone code points as
    ranges and the sequences as a second table.
  * CaseFolding.txt's simple folds (C + S), for Canonicalize under `iu`.

    python3 tools/gen/unicode_properties.py

The version is PINNED, not "latest": test262's property-escape tests are
generated from one Unicode release and assert the exact sets, so the engine
must carry that same release until the corpus moves. Bump VERSION with it.
"""

import sys

from cpp_table import generate
import urllib.request

VERSION = "17.0.0"
UCD = "https://www.unicode.org/Public/%s/ucd/" % VERSION
EMOJI = "https://www.unicode.org/Public/%s/emoji/" % VERSION

# ECMA-262 table 67: the binary properties `\p{Name}` may name, each with the
# file that defines it. The aliases come from PropertyAliases.txt.
BINARY = {
    "DerivedCoreProperties.txt": [
        "Alphabetic",
        "Case_Ignorable",
        "Cased",
        "Changes_When_Casefolded",
        "Changes_When_Casemapped",
        "Changes_When_Lowercased",
        "Changes_When_Titlecased",
        "Changes_When_Uppercased",
        "Default_Ignorable_Code_Point",
        "Grapheme_Base",
        "Grapheme_Extend",
        "ID_Continue",
        "ID_Start",
        "Lowercase",
        "Math",
        "Uppercase",
        "XID_Continue",
        "XID_Start",
    ],
    "PropList.txt": [
        "ASCII_Hex_Digit",
        "Bidi_Control",
        "Dash",
        "Deprecated",
        "Diacritic",
        "Extender",
        "Hex_Digit",
        "IDS_Binary_Operator",
        "IDS_Trinary_Operator",
        "Ideographic",
        "Join_Control",
        "Logical_Order_Exception",
        "Noncharacter_Code_Point",
        "Pattern_Syntax",
        "Pattern_White_Space",
        "Quotation_Mark",
        "Radical",
        "Regional_Indicator",
        "Sentence_Terminal",
        "Soft_Dotted",
        "Terminal_Punctuation",
        "Unified_Ideograph",
        "Variation_Selector",
        "White_Space",
    ],
    "emoji/emoji-data.txt": [
        "Emoji",
        "Emoji_Component",
        "Emoji_Modifier",
        "Emoji_Modifier_Base",
        "Emoji_Presentation",
        "Extended_Pictographic",
    ],
    "DerivedNormalizationProps.txt": ["Changes_When_NFKC_Casefolded"],
}

# ECMA-262 table 69: the binary properties of strings, `v` flag only.
STRING_PROPERTIES = [
    "Basic_Emoji",
    "Emoji_Keycap_Sequence",
    "RGI_Emoji_Modifier_Sequence",
    "RGI_Emoji_Flag_Sequence",
    "RGI_Emoji_Tag_Sequence",
    "RGI_Emoji_ZWJ_Sequence",
]


def fetch(url):
    with urllib.request.urlopen(url) as response:
        return response.read().decode("utf-8").split("\n")


def rows_of(lines):
    """(first, last, [fields...]) for every data line of a UCD file."""
    for line in lines:
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        span = fields[0].split("..")
        yield int(span[0], 16), int(span[-1], 16), fields[1:]


def to_ranges(points):
    """Sorted, disjoint, maximal [lo, hi] pairs of a set of code points."""
    out = []
    for cp in sorted(points):
        if out and out[-1][1] == cp - 1:
            out[-1][1] = cp
        else:
            out.append([cp, cp])
    return [tuple(r) for r in out]


def general_category():
    """cp -> gc for every code point; Cn where UnicodeData.txt says nothing."""
    gc = ["Cn"] * 0x110000
    mirrored = set()
    first = None
    for line in fetch(UCD + "UnicodeData.txt"):
        if not line.strip():
            continue
        fields = line.split(";")
        cp = int(fields[0], 16)
        if fields[1].endswith(", First>"):
            first = cp
            continue
        if fields[1].endswith(", Last>"):
            for c in range(first, cp + 1):
                gc[c] = fields[2]
                if fields[9] == "Y":
                    mirrored.add(c)
            continue
        gc[cp] = fields[2]
        if fields[9] == "Y":
            mirrored.add(cp)
    return gc, mirrored


def value_aliases():
    """property -> {short value -> [every name of it]}, from PropertyValueAliases.txt."""
    out = {}
    for line in fetch(UCD + "PropertyValueAliases.txt"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        out.setdefault(fields[0], {})[fields[1]] = fields[1:]
    return out


def property_aliases():
    """long name -> [every name of it], from PropertyAliases.txt."""
    out = {}
    for line in fetch(UCD + "PropertyAliases.txt"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        out[fields[1]] = fields
    return out


def binary_properties(gc, mirrored):
    """name -> set of code points, for every property table 67 lists."""
    props = {}
    for path, names in BINARY.items():
        wanted = set(names)
        for first, last, fields in rows_of(fetch(UCD + path)):
            if fields[0] in wanted:
                props.setdefault(fields[0], set()).update(range(first, last + 1))
        for name in names:
            if name not in props:
                raise SystemExit("%s: %s is not in %s" % (sys.argv[0], name, path))
    props["Bidi_Mirrored"] = mirrored
    props["Any"] = set(range(0x110000))
    props["ASCII"] = set(range(0x80))
    props["Assigned"] = {cp for cp in range(0x110000) if gc[cp] != "Cn"}
    return props


def scripts():
    """(cp -> script short name, cp -> set of extension short names)."""
    aliases = value_aliases()["sc"]
    long_to_short = {}
    for short, names in aliases.items():
        for name in names:
            long_to_short[name] = short
    sc = ["Zzzz"] * 0x110000
    for first, last, fields in rows_of(fetch(UCD + "Scripts.txt")):
        for cp in range(first, last + 1):
            sc[cp] = long_to_short[fields[0]]
    scx = {}
    for first, last, fields in rows_of(fetch(UCD + "ScriptExtensions.txt")):
        for cp in range(first, last + 1):
            scx[cp] = set(fields[0].split())
    return sc, scx, aliases


def string_properties():
    """name -> (set of lone code points, list of code point sequences)."""
    out = {name: (set(), []) for name in STRING_PROPERTIES}
    for path in ("emoji-sequences.txt", "emoji-zwj-sequences.txt"):
        for line in fetch(EMOJI + path):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [f.strip() for f in line.split(";")]
            name = fields[1]
            if name not in out:
                raise SystemExit("%s: %s is not a property of strings" % (sys.argv[0], name))
            points, sequences = out[name]
            if ".." in fields[0]:
                lo, hi = fields[0].split("..")
                points.update(range(int(lo, 16), int(hi, 16) + 1))
                continue
            seq = [int(c, 16) for c in fields[0].split()]
            if len(seq) == 1:
                points.add(seq[0])
            else:
                sequences.append(tuple(seq))
    # RGI_Emoji is the union of the six (table 69's note).
    all_points, all_sequences = set(), []
    for points, sequences in out.values():
        all_points |= points
        all_sequences.extend(sequences)
    out["RGI_Emoji"] = (all_points, all_sequences)
    for name in out:
        out[name] = (out[name][0], sorted(set(out[name][1])))
    return out


def case_folds():
    """[(cp, folded)] for CaseFolding.txt's C and S statuses, sorted by cp."""
    out = []
    for first, last, fields in rows_of(fetch(UCD + "CaseFolding.txt")):
        if fields[0] in ("C", "S"):
            out.append((first, int(fields[1], 16)))
    out.sort()
    return out


class table:
    """The flat range array and the name indexes over it."""

    def __init__(self):
        self.ranges = []
        self.indexes = {}

    def add(self, index, names, ranges):
        at = len(self.ranges)
        self.ranges.extend(ranges)
        for name in names:
            self.indexes.setdefault(index, []).append((name, at, len(ranges)))

    def print_index(self, index, comment):
        print("// %s" % comment)
        print("constexpr rx_property %s[] = {" % index)
        for name, at, count in sorted(self.indexes[index]):
            print('    {"%s", %d, %d},' % (name, at, count))
        print("};")
        print()


def main():
    gc, mirrored = general_category()
    aliases = value_aliases()
    prop_aliases = property_aliases()
    t = table()

    # General_Category: each value, then the groupings, every name they have.
    by_gc = {}
    for cp, value in enumerate(gc):
        by_gc.setdefault(value, set()).add(cp)
    for short, names in aliases["gc"].items():
        members = {
            cp
            for value, cps in by_gc.items()
            if value.startswith(short) or value == short
            for cp in cps
        }
        if short == "LC":
            members = by_gc["Lu"] | by_gc["Ll"] | by_gc["Lt"]
        t.add("rx_general_category", names, to_ranges(members))

    # Script and Script_Extensions.
    sc, scx, sc_aliases = scripts()
    by_sc = {}
    for cp, short in enumerate(sc):
        by_sc.setdefault(short, set()).add(cp)
    by_scx = {short: set(members) for short, members in by_sc.items()}
    for cp, exts in scx.items():
        by_scx[sc[cp]].discard(cp)
        for short in exts:
            by_scx[short].add(cp)
    # Unknown (Zzzz) is every code point no script claims, and a legal value.
    for short, names in sorted(sc_aliases.items()):
        t.add("rx_script", names, to_ranges(by_sc.get(short, set())))
        t.add("rx_script_extensions", names, to_ranges(by_scx.get(short, set())))

    # The binary properties, with every alias PropertyAliases.txt knows.
    for name, points in sorted(binary_properties(gc, mirrored).items()):
        t.add("rx_binary", prop_aliases.get(name, [name]), to_ranges(points))

    # The properties of strings: lone code points as ranges, sequences flat.
    sequences = []
    string_index = []
    for name, (points, seqs) in sorted(string_properties().items()):
        at = len(t.ranges)
        ranges = to_ranges(points)
        t.ranges.extend(ranges)
        seq_at = len(sequences)
        for seq in seqs:
            sequences.append(len(seq))
            sequences.extend(seq)
        string_index.append((name, at, len(ranges), seq_at, len(seqs)))

    print("// GENERATED by tools/gen/unicode_properties.py - do not edit.")
    print("// Unicode %s: UnicodeData.txt, DerivedCoreProperties.txt, PropList.txt," % VERSION)
    print("// emoji/emoji-data.txt, DerivedNormalizationProps.txt, Scripts.txt,")
    print("// ScriptExtensions.txt, PropertyAliases.txt, PropertyValueAliases.txt,")
    print("// CaseFolding.txt, emoji-sequences.txt and emoji-zwj-sequences.txt.")
    print("// clang-format off")
    print()
    print("// Every range of every property, [lo, hi] inclusive, sorted and disjoint")
    print("// within one property; the indexes below say where a property starts.")
    print("constexpr rx_range rx_ranges[] = {")
    for at in range(0, len(t.ranges), 4):
        row = "".join("{0x%X, 0x%X}, " % (a, b) for a, b in t.ranges[at : at + 4])
        print("    " + row.rstrip())
    print("};")
    print()
    t.print_index(
        "rx_general_category",
        "General_Category, every value and grouping under every name.",
    )
    t.print_index("rx_script", "Script, under the long and the four-letter name.")
    t.print_index(
        "rx_script_extensions",
        "Script_Extensions: a code point with no entry extends its Script.",
    )
    t.print_index("rx_binary", "The binary properties of table 67, under every alias.")
    print("// The properties of strings (table 69, `v` only): lone code points as")
    print("// ranges, and the sequences as {length, code points...} runs.")
    print("constexpr rx_string_property rx_strings[] = {")
    for name, at, count, seq_at, seq_count in string_index:
        print('    {"%s", %d, %d, %d, %d},' % (name, at, count, seq_at, seq_count))
    print("};")
    print("constexpr char32_t rx_sequences[] = {")
    for at in range(0, len(sequences), 12):
        print("    " + "".join("0x%X, " % cp for cp in sequences[at : at + 12]).rstrip())
    print("};")
    print()
    print("// CaseFolding.txt, statuses C and S: Canonicalize(ch) under `iu`,")
    print("// {code point, its simple fold} sorted by code point ...")
    folds = case_folds()
    print("constexpr char32_t rx_folds[][2] = {")
    for at in range(0, len(folds), 6):
        print("    " + "".join("{0x%X, 0x%X}, " % (a, b) for a, b in folds[at : at + 6]).rstrip())
    print("};")
    print("// ... and the same pairs as {fold, code point} sorted by fold, so a")
    print("// class can ask which code points share one.")
    unfolds = sorted((b, a) for a, b in folds)
    print("constexpr char32_t rx_unfolds[][2] = {")
    for at in range(0, len(unfolds), 6):
        print("    " + "".join("{0x%X, 0x%X}, " % (a, b) for a, b in unfolds[at : at + 6]).rstrip())
    print("};")
    print("// clang-format on")
    return 0


if __name__ == "__main__":
    raise SystemExit(generate(main, "ctbrowser/lib/Script/regex_properties.inc"))
