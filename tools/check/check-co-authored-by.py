#!/usr/bin/env python3
"""commit-msg hook: the message must end with a trailer

    Co-Authored-By: <name> <email>

naming the model that wrote the commit. Trailers are read the way git reads
them (git interpret-trailers: the message's last paragraph), so a line buried
in the body does not count. Usage: check-co-authored-by.py <message-file>
"""

import re
import subprocess
import sys

TRAILER = re.compile(r"(?i:co-authored-by): [^<>\n]*[^<>\s] <[^<>@\s]+@[^<>\s]+>")


def has_trailer(message_file):
    trailers = subprocess.run(
        ["git", "interpret-trailers", "--parse", message_file],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return any(TRAILER.fullmatch(line) for line in trailers.splitlines())


def main(argv):
    if has_trailer(argv[1]):
        return 0
    print(
        "The commit message must end with a trailer naming the model that wrote it:\n"
        "    Co-Authored-By: <name> <email>\n"
        "for example: Co-Authored-By: GPT-6.1 Sol <noreply@openai.com>",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
