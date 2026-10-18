"""Sync the working tree (incl. submodules) to the shared devbox and run the
build there. Usage:

  tools/remote-build.sh [ninja target ...]   default preset: configure + build
                                             + ctest (Ninja); with targets,
                                             build only those and run no tests
  tools/remote-build.sh sync                 sync the tree and the box's
                                             project deps only; build nothing
  tools/remote-build.sh windows              `windows` preset: cross-compile
                                             the examples, collect exes+SDL3.dll
                                             via windows-dist, rsync them back
                                             into examples-windows/

Targets are joined with spaces into `cmake --build ... --target`, so
`ctjs-opt -- -k 0` passes `-k 0` through to Ninja.

Environment: DEVBOX_HOST (ssh host or alias, default `devbox`), DEVBOX_DIR
(directory on the box, default projects/compile-time-browser), CT_BUILD_JOBS
and CT_TEST_JOBS (default: the box's CPU count), CLANG_STD_EMBED_TAG and
CLANG_STD_EMBED_RELEASE (the pinned toolchain), CTBROWSER_ANGLE (default ON)
and CTCOMPILE_MLIR (default ON).

tools/remote-build.sh runs this file in the repository's uv environment (the
`tools` dependency group, for Fabric). Every command on the box goes through
one fabric.Connection; the tree itself travels with rsync, which Fabric lacks.

The configure root is ctbrowser/, not the repository root: the repository is
a monorepo with ctbrowser/ and ctcompile/ as sibling projects (the LLVM
model), so CMakePresets.json lives in ctbrowser/ and every cmake and ctest
invocation below runs from there. The build trees still land at the
repository root (build/, build-windows/, ...), where the other tools look.

The `default` preset builds both projects: it sets
CTBROWSER_ENABLE_PROJECTS=ctcompile. There is no CI, so this script is the
only full build and test run; a sibling project it skipped would break
unnoticed. `browser` and `browser-no-llvm` are the engine-only configurations.

The box is github.com/alexios-angel/infra (sibling checkout ../infra),
reached via the ssh alias that `../infra/<cloud>-build-server/server.sh
ssh-config` writes into ~/.ssh/config; Fabric reads that file.
DEVBOX_HOST=ubuntu@<ip> overrides. Project deps the box deliberately doesn't
ship (the pinned clang-std-embed toolchain) are converged here, not in the
box's cloud-init.
"""

import os
import shlex
import subprocess
import sys
from pathlib import Path

from fabric import Connection
from paramiko import SSHException

# Converge project-owned deps on the box: the compiled ones ride in
# tools/Brewfile, and a box without linuxbrew gets none of them.
CONVERGE = r"""set -euo pipefail
BREW=/home/linuxbrew/.linuxbrew/bin/brew
if [ -x "$BREW" ]; then
  export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_ENV_HINTS=1
  "$BREW" bundle check --file="$HOME/$CT_REMOTE_DIR/tools/Brewfile" >/dev/null 2>&1 \
    || "$BREW" bundle install --file="$HOME/$CT_REMOTE_DIR/tools/Brewfile"
fi
# lit (and psutil, for its time limit) from pyproject.toml's test group, at the
# versions uv.lock pins, into /.venv where ctcompile/test/cmake/Lit.cmake looks
# first. --inexact keeps what other groups installed there (compare.py setup's
# Playwright); --frozen fails rather than re-resolving a stale lock.
UV=/home/linuxbrew/.linuxbrew/bin/uv
if [ -x "$UV" ]; then
  (cd "$HOME/$CT_REMOTE_DIR" && "$UV" sync --quiet --frozen --inexact --group test)
fi
tool="$HOME/$CT_REMOTE_DIR/tools/clang-std-embed"
# Reinstall whenever the pinned release changes, not only when the binary is
# missing; the .release stamp records which release is installed.
if [ "$(cat "$tool/.release" 2>/dev/null)" != "$CLANG_STD_EMBED_RELEASE" ]; then
  rm -rf "$tool"
  mkdir -p "$tool"
  curl -fsSL --retry 5 "$CLANG_STD_EMBED_RELEASE" | tar -xJ --strip-components=1 -C "$tool"
  printf '%s' "$CLANG_STD_EMBED_RELEASE" > "$tool/.release"
fi
# ANGLE, from the pinned release. fetch-angle.sh checks its own release stamp,
# so this is a no-op once the box is at the pin. It runs here rather than beside
# the cmake line because the Windows path needs it too, and a failed fetch must
# stop the run before configure.
cd "$HOME/$CT_REMOTE_DIR" && tools/fetch-angle.sh
"""

# rsync the whole tree including submodule checkouts; leave remote build
# artifacts in place so incremental builds survive a sync.
#
# Exclude every build*/ tree (build-windows/, build-asan/, ...): a CMake cache
# holds absolute paths from the machine that wrote it, and cmake refuses a
# cache copied from elsewhere. tools/clang-std-embed stays local: the
# server-side copy is converged by CONVERGE.
#
# Directories created on the box itself are excluded and protected from
# --delete, so a sync from a checkout that lacks them does not wipe them:
# third-party/angle/ (~34 MB fetched by tools/fetch-angle.sh from a pinned
# release), the uv environment /.venv/ (lit for the lit suite, and Playwright
# once `tools/check/compare.py setup` has run there; a local one holds this
# machine's interpreter paths and must not travel) and tools/.venv/, where
# Playwright lived before the uv environment. Windows toolchains and packaged
# executables are cached build assets too.
#
# Switching worktrees can replace a file with an older version than the last
# build saw. --no-times gives changed files fresh destination mtimes so Ninja
# rebuilds them; --checksum leaves identical files (and their mtimes) alone.
RSYNC = [
    "rsync", "-az", "--checksum", "--no-times", "--delete",
    "--exclude", ".git/",
    "--exclude", ".claude/",
    "--exclude", "build*/",
    "--exclude", "tools/clang-std-embed/",
    "--exclude", "tools/llvm-mingw/",
    "--exclude", "examples-windows/",
    "--exclude", "/.venv/",
    "--exclude", "tools/.venv/",
    "--exclude", "third-party/angle/",
    "--exclude", "*.d",
    "--filter", "protect *.pch", "--filter", "protect *.gch", "--filter", "protect build*/",
    "--filter", "protect tools/clang-std-embed/", "--filter", "protect third-party/angle/",
    "--filter", "protect /.venv/", "--filter", "protect tools/.venv/",
    "--filter", "protect tools/llvm-mingw/", "--filter", "protect examples-windows/",
    "--filter", "protect *.d",
]  # fmt: skip

UNREACHABLE = """\
cannot reach '{host}' — likely one of:
  alias missing:    ../infra/<cloud>-build-server/server.sh ssh-config
  box stopped:      ../infra/<cloud>-build-server/server.sh start
  your IP changed:  ../infra/<cloud>-build-server/server.sh allow-ip
or set DEVBOX_HOST=ubuntu@<ip>."""


def open_connection(host):
    """Open the connection the way `ssh -o ConnectTimeout=5 host` would.

    Raises OSError or SSHException when the box cannot be reached.
    """
    conn = Connection(host, connect_timeout=5)
    # Paramiko finds the agent only through SSH_AUTH_SOCK; ssh's IdentityAgent
    # (~/.ssh/build-agent.sock for the build boxes) names it for ssh alone.
    agent = conn.ssh_config.get("identityagent")
    if agent and agent.lower() != "none":
        os.environ["SSH_AUTH_SOCK"] = os.path.expanduser(agent)
    conn.open()
    return conn


def connect(host):
    """open_connection, exiting with the likely causes when it fails."""
    try:
        conn = open_connection(host)
    except (OSError, SSHException):
        print(UNREACHABLE.format(host=host), file=sys.stderr)
        sys.exit(1)
    # ~/.ssh/config's ServerAliveInterval, which Paramiko does not read.
    # ponytail: keepalives only; Paramiko has no ServerAliveCountMax, so a dead
    # link is noticed by TCP's own timeout rather than after five missed replies.
    conn.transport.set_keepalive(int(conn.ssh_config.get("serveraliveinterval", 60)))
    return conn


def main(args):
    if args[:1] in (["-h"], ["--help"]):
        print("\n\n".join(__doc__.split("\n\n")[:4]))
        return 0
    here = Path(__file__).resolve().parent
    host = os.environ.get("DEVBOX_HOST", "devbox")
    # The directory on the box. Give each worktree its own: the rsync carries
    # --delete, so two checkouts syncing to one directory would erase each
    # other's tree mid-compile.
    #
    #   DEVBOX_DIR=projects/ctbrowser-lexical ./tools/remote-build.sh
    remote_dir = os.environ.get("DEVBOX_DIR", "projects/compile-time-browser")
    # The pinned clang-std-embed toolchain release (from the embed repo). There
    # is no CI, so this is the only place the pin lives. sync-to-ctbrowser.sh
    # may install a locally built one instead; the rsync protect filter keeps
    # whichever is on the server.
    tag = os.environ.get("CLANG_STD_EMBED_TAG", "clang-std-embed-e3986d225")
    release = os.environ.get(
        "CLANG_STD_EMBED_RELEASE",
        f"https://github.com/alexios-angel/embed/releases/download/{tag}/{tag}-linux-x86_64.tar.xz",
    )
    # ANGLE is on for a devbox build and off everywhere else. The option is a
    # FATAL_ERROR when third-party/angle/ is missing, which is right for a
    # developer who has not fetched it, so it is set here, where fetch-angle.sh
    # has just run, rather than in the shared `default` preset. Without it
    # gl_basics prints SKIP and the render-angle-* tests are never registered,
    # so the suite passes without testing WebGL. CTBROWSER_ANGLE=OFF opts out.
    angle = os.environ.get("CTBROWSER_ANGLE", "ON")

    conn = connect(host)

    def run(command, **kwargs):
        # No stdin forwarding: nothing on the box reads it, and invoke's stdin
        # thread would otherwise hold the terminal.
        result = conn.run(command, warn=True, in_stream=False, **kwargs)
        if result.failed:
            sys.exit(result.exited)
        return result

    # Use the devbox CPU count, not the local machine's. Explicit CTest jobs
    # also work with CTest 3.28, where the preset's jobs=0 still runs tests
    # serially. The shared devbox lock serializes build sessions; either pool
    # can be capped.
    devbox_jobs = run("nproc", hide=True).stdout.strip()
    build_jobs = os.environ.get("CT_BUILD_JOBS", devbox_jobs)
    test_jobs = os.environ.get("CT_TEST_JOBS", devbox_jobs)
    print(
        f"devbox: {devbox_jobs} CPUs; build jobs={build_jobs}; CTest jobs={test_jobs}", flush=True
    )

    repo_root = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=here, check=True, capture_output=True, text=True,
    ).stdout.strip()  # fmt: skip

    subprocess.run([*RSYNC, f"{repo_root}/", f"{host}:{remote_dir}/"], check=True)

    env = f"CLANG_STD_EMBED_RELEASE={shlex.quote(release)} CT_REMOTE_DIR={shlex.quote(remote_dir)}"
    run(f"{env} bash -c {shlex.quote(CONVERGE)}")

    if args[:1] == ["sync"]:
        print(f"synced {repo_root} to {host}:{remote_dir}")
    elif args[:1] == ["windows"]:
        # The cross build needs compiled libraries in the mingw sysroot that the
        # box does not ship (Boost.URL, Boost.ProgramOptions, zlib, libpng,
        # libjpeg-turbo, mimalloc, simdutf, cpptrace), so the two idempotent
        # builder scripts run first.
        #
        # ~/projects/boost-inc is the isolated Boost include dir the cross
        # toolchain wants: cmake/toolchains/windows-x86_64.cmake puts it on the
        # cross compile's -isystem path, so it must hold a boost/ symlink and
        # nothing else. Pointing it at brew's whole include/ would put the
        # host's SDL3, libpng and zlib headers in front of the Windows ones.
        run(
            'inc="$HOME/projects/boost-inc"; mkdir -p "$inc";'
            ' [ -e "$inc/boost" ] || ln -s /home/linuxbrew/.linuxbrew/include/boost "$inc/boost";'
            ' ls "$inc/boost/version.hpp" >/dev/null'
        )
        run(
            f"cd {remote_dir} && tools/mingw/build-libs-mingw.sh && tools/mingw/build-boost-mingw.sh"
        )
        run(
            f"cd {remote_dir}/ctbrowser && cmake --preset windows -DCTBROWSER_WITH_ANGLE={angle}"
            " && cmake --build --preset windows && cmake --build --preset windows --target windows-dist"
        )
        subprocess.run(
            [
                "rsync",
                "-az",
                f"{host}:{remote_dir}/examples-windows/",
                f"{repo_root}/examples-windows/",
            ],
            check=True,
        )
        print("examples-windows/ refreshed from the devbox")
    else:
        # Pass the brew prefix as CMAKE_PREFIX_PATH. The -isystem include path
        # is enough for header-plus-library dependencies, but SDL3 and SDL3_ttf
        # ship CMake config packages that find_package cannot see without it; a
        # missing SDL3_ttf silently falls back to the font8x8 bitmap font and
        # skews every text measurement.
        prefix = ""
        if conn.run("[ -d /home/linuxbrew/.linuxbrew ]", warn=True, in_stream=False).ok:
            prefix = "-DCMAKE_PREFIX_PATH=/home/linuxbrew/.linuxbrew"
        # CTCOMPILE_ENABLE_MLIR defaults to OFF so a runtime-only build needs no
        # LLVM, and CMakePresets.json is shared with machines that have none.
        # The devbox has LLVM, so turn it on here; otherwise ctcompile's
        # dialect, importer, EmitC backend and their lit/differential tests are
        # silently skipped. CTCOMPILE_MLIR=OFF opts out.
        configure = (
            f"cmake --preset default -DCTBROWSER_WITH_ANGLE={angle}"
            f" -DCTCOMPILE_ENABLE_MLIR={os.environ.get('CTCOMPILE_MLIR', 'ON')} {prefix}"
        )
        build = f"cd {remote_dir}/ctbrowser && {configure} && cmake --build --preset default -j{build_jobs}"
        if args:
            run(f"{build} --target {' '.join(args)}")
        else:
            run(f"{build} && ctest --preset default -j{test_jobs}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
