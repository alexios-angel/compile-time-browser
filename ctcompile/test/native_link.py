"""Apply the shared native test link contract to a compiler invocation."""

import argparse
import subprocess

from Target.Cpp.harness import native_link_command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("a compiler command is required")
    raise SystemExit(subprocess.call(native_link_command(command, args.build)))


if __name__ == "__main__":
    main()
