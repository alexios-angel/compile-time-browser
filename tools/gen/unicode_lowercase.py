#!/usr/bin/env python3
"""Generate Core's Unicode 17 lowercase table for a single UTF-16 code unit.

    python3 tools/gen/unicode_lowercase.py > ctbrowser/lib/Core/unicode_lowercase.inc

Default full lowercase is UnicodeData field 13 plus unconditional SpecialCasing
overrides. Final_Sigma cannot apply to an isolated unit; locale rules do not apply.
The generator checks every unit against the uncompressed UCD mapping and prints
its checksum to stderr for the independent exhaustive core_basics regression.
"""

import hashlib
import sys
import urllib.request

VERSION = "17.0.0"
HASHES = {
    "UnicodeData.txt": "2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c",
    "SpecialCasing.txt": "efc25faf19de21b92c1194c111c932e03d2a5eaf18194e33f1156e96de4c9588",
}
UNICODE_LICENSE = """UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2026 Unicode, Inc.

NOTICE TO USER: Carefully read the following legal agreement. BY
DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder."""


def fields(name):
    with urllib.request.urlopen(
        f"https://www.unicode.org/Public/{VERSION}/ucd/{name}", timeout=30
    ) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != HASHES[name]:
        raise ValueError(f"{name}: pinned Unicode checksum changed")
    for line in data.decode("utf-8").splitlines():
        line = line.partition("#")[0].strip()
        if line:
            yield [part.strip() for part in line.split(";")]


def main():
    lower = {}
    for row in fields("UnicodeData.txt"):
        cp = int(row[0], 16)
        if cp < 0x10000 and row[13]:
            lower[cp] = (int(row[13], 16),)
    for row in fields("SpecialCasing.txt"):
        cp = int(row[0], 16)
        if row[4]:
            assert row[4] == "Final_Sigma" or row[4].split()[0] in {"tr", "az", "lt"}
        elif cp < 0x10000:
            lower[cp] = tuple(int(value, 16) for value in row[1].split())
    lower = {cp: out for cp, out in lower.items() if out != (cp,)}
    assert {cp: out for cp, out in lower.items() if len(out) != 1} == {0x130: (0x69, 0x307)}
    assert all(all(unit < 0x10000 for unit in out) for out in lower.values())

    ranges = []
    for cp, out in sorted(lower.items()):
        if len(out) != 1:
            continue
        delta = out[0] - cp
        if (
            ranges
            and ranges[-1][3] == delta
            and cp - ranges[-1][1] in (1, 2)
            and (ranges[-1][0] == ranges[-1][1] or cp - ranges[-1][1] == ranges[-1][2])
        ):
            ranges[-1][2] = cp - ranges[-1][1]
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp, 1, delta])

    checksum = 14695981039346656037
    for cp in range(0x10000):
        expected = lower.get(cp, (cp,))
        actual = (cp,)
        for first, last, stride, delta in ranges:
            if first <= cp <= last and (cp - first) % stride == 0:
                actual = (cp + delta,)
                break
        if cp == 0x130:
            actual = (0x69, 0x307)
        assert actual == expected, hex(cp)
        for value in (len(expected), *expected):
            checksum = ((checksum ^ value) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    print(f"65,536 units checked; core_basics checksum 0x{checksum:016X}", file=sys.stderr)

    print("// GENERATED by tools/gen/unicode_lowercase.py; do not edit.")
    print(f"// Unicode {VERSION}: UnicodeData.txt + unconditional SpecialCasing.txt.")
    for name, digest in HASHES.items():
        print(f"// {name} SHA256: {digest}")
    for line in UNICODE_LICENSE.splitlines():
        print("// " + line if line else "//")
    print("// clang-format off")
    print("constexpr auto lowercase_ranges = std::to_array<lowercase_range>({")
    for first, last, stride, delta in ranges:
        print(f"    {{0x{first:04X}, 0x{last:04X}, {stride}, {delta}}},")
    print("});")
    print("// clang-format on")


if __name__ == "__main__":
    main()
