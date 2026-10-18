# CASES below copies each case's RUN lines from its .test file until that
# case's switch commit deletes them there; until then, edit both copies.
"""The Packaging lit case whose Python step runs among other RUN lines, as a
pytest test. test_case is parametrized over CASES, whose keys are the case's
lit paths relative to this folder without .test: `test_case[roundtrip]` is
lit's `ctcompile :: Packaging/roundtrip.test`. Each test runs all of its case's
RUN lines in order through lit_lines.run, every line under bash, as lit runs
them.
"""

from pathlib import Path

import pytest

import lit_lines

HERE = Path(__file__).resolve().parent
MAINS = {}

CASES = {
    "roundtrip": [
        "rm -rf %t && mkdir -p %t && ctcompile-test-app_bundle %t",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctcompile %S/app --entry index.html --launcher ctrun -o %t/app --allow-env-overrides --verbose --fonts %{monorepo}/ctbrowser/resources/fonts --manifest %t/app.json 2>&1 | FileCheck %s --check-prefix=PACKAGED",
        "test -x %t/app",
        "not ctcompile %S/app --entry index.html --fonts %{monorepo}/ctbrowser/resources/fonts --bundle -o /dev/full 2>&1 | FileCheck %s --check-prefix=FULL",
        "not ctcompile %S/app --entry index.html --fonts %{monorepo}/ctbrowser/resources/fonts --bundle -o %t/failed.ctapp --manifest /dev/full 2>&1 | FileCheck %s --check-prefix=FULL",
        "python3 -m json.tool %t/app.json | FileCheck %s --check-prefix=MANIFEST",
        'python3 -c \'import json, sys; s = json.load(open(sys.argv[1]))["scripts"]; sys.exit(len(s) != 2 or s[0]["program_id"] == s[1]["program_id"])\' %t/app.json',
        "test $(stat -c %%s %t/app) -gt $(stat -c %%s ctrun)",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 %t/app",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy %t/app --info | python3 -m json.tool | FileCheck %s --check-prefix=INFO",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy %t/app --help | FileCheck %s --check-prefix=HELP",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 %t/app --not-a-flag stray",
        "not env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctrun",
        "not env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctrun %S/app/index.html 2>&1 | FileCheck %s --check-prefix=NOT-A-BUNDLE",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctbrowse %S/app/index.html --size 320 240 --frames 2 --shot %t/flags.ppm",
        "head -c 15 %t/flags.ppm | FileCheck %s --check-prefix=FLAGS",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONT_PATH=%{monorepo}/ctbrowser/resources/fonts CTBROWSER_SCREENSHOT=%t/from-source.ppm ctbrowse %S/app/index.html",
        "env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_SCREENSHOT=%t/packaged.ppm %t/app",
        "cmp %t/from-source.ppm %t/packaged.ppm",
        "not env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctcompile %S/app-module --entry index.html --launcher ctrun -o %t/app-module 2>&1 | FileCheck %s --check-prefix=MODULE",
        "test -f %t/no-images.ctapp && test -f %t/module-page.ctapp",
        "not env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctrun %t/no-images.ctapp",
        "not env SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy CTBROWSER_TEST_FRAMES=2 CTBROWSER_NETWORK=0 CTBROWSER_FONTS=font8x8 ctrun %t/module-page.ctapp",
    ],
}


@pytest.mark.parametrize("case", CASES)
def test_case(lit, case):
    lit_lines.run(lit, HERE / f"{case}.test", CASES[case], MAINS)
