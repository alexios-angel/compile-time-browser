import subprocess
import sys
from pathlib import Path

import pytest

HOOK = Path(__file__).with_name("check-co-authored-by.py")


def passes(tmp_path, message):
    path = tmp_path / "COMMIT_EDITMSG"
    path.write_text(message)
    return subprocess.run([sys.executable, HOOK, path], capture_output=True).returncode == 0


@pytest.mark.parametrize(
    "message",
    [
        "Fix it\n\nCo-Authored-By: GPT-6.1 Sol <noreply@openai.com>\n",
        "Merge lane-x into queue/13-int: why\n\nCo-Authored-By: Kimi K3 <noreply@moonshot.ai>\n",
        "Fix it\n\nBody.\n\nCo-Authored-By: Kimi K3 <noreply@moonshot.ai>\n"
        "Co-Authored-By: GPT-6.1 Sol <noreply@openai.com>\n",
        "Fix it\n\nco-authored-by: Claude Opus 5.5 <noreply@anthropic.com>\n# a comment\n",
    ],
)
def test_accepts_a_final_trailer(tmp_path, message):
    assert passes(tmp_path, message)


@pytest.mark.parametrize(
    "message",
    [
        "Fix it\n",
        "Fix it\n\nCo-Authored-By: GPT-6.1 Sol\n",
        "Fix it\n\nCo-Authored-By: <noreply@openai.com>\n",
        "Fix it\n\nCo-Authored-By: GPT-6.1 Sol <not-an-email>\n",
        "Fix it\n\nCo-Authored-By: GPT-6.1 Sol <noreply@openai.com>\n\nA later paragraph.\n",
    ],
)
def test_rejects_a_missing_or_malformed_trailer(tmp_path, message):
    assert not passes(tmp_path, message)
