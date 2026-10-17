#!/usr/bin/env bash
# Runs tools/remote_build.py (its docstring is the usage) in the repository's uv environment.
exec uv run --quiet --frozen --only-group tools --project "$(dirname "$0")/.." python "$(dirname "$0")/remote_build.py" "$@"
