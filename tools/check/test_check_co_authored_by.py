import subprocess
import sys
from pathlib import Path

import pytest

HOOK = Path(__file__).with_name("check-co-authored-by.py")


def run(tmp_path, message):
    path = tmp_path / "COMMIT_EDITMSG"
    path.write_text(message)
    # Run outside the checkout: in a copied worktree the .git pointer may not
    # resolve, and git interpret-trailers then fails before parsing anything.
    return subprocess.run(
        [sys.executable, HOOK, path], capture_output=True, text=True, cwd=tmp_path
    )


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
    assert run(tmp_path, message).returncode == 0


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
    result = run(tmp_path, message)
    # The hook's own refusal, not a crash.
    assert result.returncode == 1
    assert "must end with a trailer" in result.stderr
