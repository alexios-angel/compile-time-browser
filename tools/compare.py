#!/usr/bin/env python3
"""Drive ctbrowser and a real browser through the same interaction, live.

ctbrowser's goldens prove it renders the same as it did yesterday. They say
nothing about whether it matches a real browser, and feature parity with
Chrome/Firefox is the actual goal. This opens both, sends each the same clicks
and keystrokes ONE COMMAND AT A TIME, and screenshots either on demand - so an
AI can drive it, and a human can watch the windows and say "that click missed"
while it is still happening.

    tools/compare.py setup                     # once: a venv with Playwright
    tools/compare.py start --headed --delay 400 examples/pages/widgets.html
    tools/compare.py click 120 200
    tools/compare.py type Claude
    tools/compare.py key Tab
    tools/compare.py shot after-tab            # PNGs, one per engine
    tools/compare.py stop

The browsers cannot live inside one invocation - the caller is a shell, and
every command is a new process - so `start` leaves a DAEMON holding them and
every other verb is a one-line client that talks to it over a unix socket.

--engine= picks who is driven: ctbrowse (alias normal), chrome (alias
chromium), firefox. Repeatable and comma-accepting; the default is
ctbrowse,chrome. Each stands alone: --engine=chrome asks a real browser what it
does before deciding what ctbrowser should do, and --engine=ctbrowse drives the
engine by hand with no venv and nothing downloaded.

Stdlib only, except Playwright - and that is imported only when a real browser
is actually selected. Unlike the other tools here this one takes arguments, so
it uses argparse; gen-assets.py and gen-shaders.py take none because they are
generators with nothing to vary.
"""

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "compare"
# TCP on loopback rather than a unix socket, and a file holding the port. The
# repo lives on a DrvFs mount under WSL, which does not support unix sockets at
# all - bind() there fails with EOPNOTSUPP. ctdrive is reached the same way.
PORTFILE = OUT / "session.port"
VENV = ROOT / "tools" / ".venv"

# ctbrowse first so it is the left-hand image, which is the one being judged.
ALIASES = {
    "ctbrowse": "ctbrowse",
    "ctbrowser": "ctbrowse",
    "normal": "ctbrowse",
    "chrome": "chrome",
    "chromium": "chrome",
    "firefox": "firefox",
}


# --- images ----------------------------------------------------------------
#
# ctbrowser writes P6 PPM and has no PNG encoder - deliberately, since a golden
# a test byte-compares gains nothing from compression. But a PNG is what an AI
# or a browser can actually look at, so the conversion happens here, in about
# twenty lines of zlib rather than a dependency.


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """A binary P6 as (width, height, RGB bytes). ctbrowser writes no other kind."""
    data = path.read_bytes()
    fields: list[bytes] = []
    at = 0
    while len(fields) < 4:
        while at < len(data) and data[at : at + 1].isspace():
            at += 1
        if data[at : at + 1] == b"#":  # ctbrowser writes none, but P6 allows them
            while at < len(data) and data[at] != 0x0A:
                at += 1
            continue
        start = at
        while at < len(data) and not data[at : at + 1].isspace():
            at += 1
        fields.append(data[start:at])
    if fields[0] != b"P6":
        raise ValueError(f"{path}: not a binary PPM")
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[at + 1 :]


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    """8-bit RGB, no interlacing. Enough for a screenshot and nothing more."""

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        )

    stride = width * 3
    # Filter byte 0 (None) per scanline: the images are screenshots, and a
    # smarter filter would trade readability here for bytes nobody counts.
    raw = b"".join(b"\0" + rgb[y * stride : (y + 1) * stride] for y in range(height))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def ppm_to_png(ppm: Path, png: Path) -> None:
    width, height, rgb = read_ppm(ppm)
    write_png(png, width, height, rgb)


# --- fonts -----------------------------------------------------------------


def font_conf(path: Path) -> Path:
    """Point a browser at ctbrowser's own faces.

    ctbrowser renders in the vendored Tinos / Fira Sans / Cousine; a system
    browser resolves serif/sans/mono to whatever fontconfig says, which here is
    Noto. Left alone, EVERY glyph differs and font substitution buries the
    differences that are actually about the engine.
    """
    fonts = ROOT / "fonts"
    # The CONCRETE names matter as much as the generics. A browser's default
    # standard font is "Times New Roman", not "serif" - map only the generics
    # and that request falls through to whatever happens to be first in the
    # directory, which is how a heading came out sans-serif and looked like an
    # engine bug for a while. Tinos, Fira Sans and Cousine are metric-compatible
    # with the three names below, which is why those pairings and not others.
    families = [
        ("serif", "Tinos"),
        ("Times New Roman", "Tinos"),
        ("Times", "Tinos"),
        ("sans-serif", "Fira Sans"),
        ("Arial", "Fira Sans"),
        ("Helvetica", "Fira Sans"),
        ("monospace", "Cousine"),
        ("Courier New", "Cousine"),
        ("Courier", "Cousine"),
    ]
    rules = "".join(
        f"""  <match target="pattern">
    <test qual="any" name="family"><string>{asked}</string></test>
    <edit name="family" mode="assign" binding="strong"><string>{real}</string></edit>
  </match>
"""
        for asked, real in families
    )
    path.write_text(
        '<?xml version="1.0"?>\n'
        '<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n'
        f"<fontconfig>\n  <dir>{fonts}</dir>\n{rules}"
        # And a last resort, so a family named by neither list still lands on a
        # face from this directory rather than on a system font.
        '  <match target="pattern">\n'
        '    <edit name="family" mode="append_last"><string>Tinos</string></edit>\n'
        "  </match>\n"
        "</fontconfig>\n"
    )
    return path


# --- engines ---------------------------------------------------------------


class Ctbrowse:
    """ctbrowser, through the ctdrive binary and its loopback socket."""

    name = "ctbrowse"

    def __init__(self, page: Path, size: tuple[int, int], headed: bool):
        exe = ROOT / "build" / "src" / "examples" / "ctdrive"
        if not exe.exists():
            raise FileNotFoundError(f"{exe} not built; cmake --build --preset default")
        env = dict(os.environ)
        if not headed:
            env["SDL_VIDEODRIVER"] = "offscreen"
            env["SDL_AUDIODRIVER"] = "dummy"
        self.proc = subprocess.Popen(
            [str(exe), str(page), "--port", "0", "--size", str(size[0]), str(size[1])],
            cwd=ROOT,
            env=env,
            stdout=subprocess.PIPE,
            text=True,
        )
        # It prints the port it got before doing anything else; asking for 0
        # means the OS chose one and this is the only way to learn it.
        assert self.proc.stdout is not None
        line = self.proc.stdout.readline()
        if "listening on" not in line:
            raise RuntimeError(f"ctdrive did not start: {line.strip()}")
        port = int(line.rsplit(":", 1)[1])
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.io = self.sock.makefile("rw")

    def send(self, **cmd) -> dict:
        self.io.write(json.dumps(cmd) + "\n")
        self.io.flush()
        return json.loads(self.io.readline())

    def click(self, x, y, button=0):
        return self.send(cmd="click", x=x, y=y, button=button)

    def move(self, x, y):
        return self.send(cmd="move", x=x, y=y)

    def down(self, x, y, button=0):
        return self.send(cmd="down", x=x, y=y, button=button)

    def up(self, x, y, button=0):
        return self.send(cmd="up", x=x, y=y, button=button)

    def type_(self, text):
        return self.send(cmd="type", text=text)

    def key(self, code, shift=False, ctrl=False):
        return self.send(cmd="key", code=code, shift=shift, ctrl=ctrl)

    def wheel(self, notches, at=None):
        if at is None:
            return self.send(cmd="wheel", notches=notches)
        return self.send(cmd="wheel", notches=notches, x=at[0], y=at[1])

    def eval_(self, script):
        return self.send(cmd="eval", script=script)

    def shot(self, path: Path) -> dict:
        # ctbrowser writes PPM and has no PNG encoder; convert here and report
        # the file that actually exists, so a caller can open what it is told.
        ppm = path.with_suffix(".ppm")
        answer = self.send(cmd="shot", path=str(ppm))
        if answer.get("ok"):
            ppm_to_png(ppm, path)
            ppm.unlink()
            answer["path"] = str(path)
        return answer

    def close(self):
        try:
            self.send(cmd="quit")
        except (OSError, ValueError):
            pass
        self.proc.terminate()


def serve_repo() -> str:
    """A local HTTP server rooted at the repository. Returns its base URL.

    THE PAGE IS SERVED, NOT OPENED. Chrome treats every file:// URL as its own
    origin, so a page's `<script src="../assets/p5.js">` is a cross-origin
    request and is blocked - the window comes up blank with nothing in the
    console, which reads as "ctbrowser renders and Chrome does not" when the
    truth is that Chrome never loaded the script. `--allow-file-access-from-files`
    does not cover it.

    Serving also makes the comparison fairer: a real page is fetched over HTTP,
    and that is the resolution behaviour both engines should be judged against.
    Loopback, an ephemeral port, and a daemon thread that dies with the process.
    """
    import functools
    import http.server
    import threading

    # UTF-8, EXPLICITLY. SimpleHTTPRequestHandler sends `text/javascript` with
    # no charset, and a browser then decodes the script as whatever it guesses -
    # which for p5.js, whose source contains `const π = Math.PI`, splits that
    # identifier into two Latin-1 bytes and rejects the bundle with a syntax
    # error 7,000 lines in. The page that loads it should declare its own
    # encoding as well; this makes the server right regardless.
    http.server.SimpleHTTPRequestHandler.extensions_map = dict(
        http.server.SimpleHTTPRequestHandler.extensions_map,
        **{".js": "text/javascript; charset=utf-8",
           ".html": "text/html; charset=utf-8",
           ".css": "text/css; charset=utf-8",
           ".json": "application/json; charset=utf-8"},
    )
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(ROOT))
    # Quiet: one request line per asset is noise in a tool whose whole output is
    # meant to be read.
    handler.log_message = lambda *a, **k: None
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return f"http://127.0.0.1:{server.server_port}"


class Reference:
    """Chromium or Firefox, through Playwright."""

    served: str | None = None

    def __init__(self, name: str, pw, page_path: Path, size, headed: bool):
        self.name = name
        kind = pw.chromium if name == "chrome" else pw.firefox
        self.browser = kind.launch(headless=not headed)
        self.page = self.browser.new_page(viewport={"width": size[0], "height": size[1]})
        if Reference.served is None:
            Reference.served = serve_repo()
        relative = page_path.resolve().relative_to(ROOT).as_posix()
        self.page.goto(f"{Reference.served}/{relative}")

    def click(self, x, y, button=0):
        names = {0: "left", 1: "middle", 2: "right"}
        self.page.mouse.click(x, y, button=names.get(button, "left"))
        return {"ok": True}

    def move(self, x, y):
        self.page.mouse.move(x, y)
        return {"ok": True}

    def down(self, x, y, button=0):
        self.page.mouse.move(x, y)
        self.page.mouse.down(button={0: "left", 1: "middle", 2: "right"}.get(button, "left"))
        return {"ok": True}

    def up(self, x, y, button=0):
        self.page.mouse.move(x, y)
        self.page.mouse.up(button={0: "left", 1: "middle", 2: "right"}.get(button, "left"))
        return {"ok": True}

    def type_(self, text):
        self.page.keyboard.type(text)
        return {"ok": True}

    def key(self, code, shift=False, ctrl=False):
        # ctbrowser speaks DOM `code` ("KeyA", "ArrowLeft"); Playwright wants
        # the key NAME ("a", "ArrowLeft"), so the KeyX/DigitX forms translate
        # and everything else is already the same word.
        name = code
        if code.startswith("Key") and len(code) == 4:
            name = code[3] if shift else code[3].lower()
        elif code.startswith("Digit") and len(code) == 6:
            name = code[5]
        parts = (["Control"] if ctrl else []) + (["Shift"] if shift else []) + [name]
        self.page.keyboard.press("+".join(parts))
        return {"ok": True}

    def wheel(self, notches, at=None):
        if at is not None:
            self.page.mouse.move(at[0], at[1])
        # ctbrowser's notch is positive AWAY from the user and it scrolls by
        # browser_options::wheel_step (53px); Chrome takes pixels. Matched here
        # so a wheel command means the same distance on both.
        self.page.mouse.wheel(0, -notches * 53)
        return {"ok": True}

    def eval_(self, script):
        # Shaped like ctdrive's answer, which reports what the script LOGGED -
        # run_script cannot return a value. Wrapping console.log on this side
        # keeps one question askable of both engines.
        # The internals are __cmp_-prefixed because they share a scope with the
        # script being evaluated: named `out`, a page script declaring its own
        # `out` dies with "Identifier 'out' has already been declared" in the
        # reference browser ONLY, which reads exactly like an engine difference.
        logged = self.page.evaluate(
            "(__cmp_src) => { const __cmp_log = []; const __cmp_real = console.log;"
            " console.log = (...a) => __cmp_log.push(a.join(' '));"
            " try { eval(__cmp_src); } finally { console.log = __cmp_real; }"
            " return __cmp_log; }",
            script,
        )
        return {"ok": True, "error": "", "console": logged}

    def shot(self, path: Path) -> dict:
        self.page.screenshot(path=str(path))
        return {"ok": True, "path": str(path)}

    def close(self):
        self.browser.close()


# --- the daemon ------------------------------------------------------------


class Session:
    def __init__(self, args):
        self.delay = args.delay / 1000.0
        self.engines: list = []
        self.playwright = None
        page = Path(args.page)
        if not page.exists():
            raise FileNotFoundError(f"{page} does not exist")
        size = (args.size[0], args.size[1])

        wanted = args.engines
        if "ctbrowse" in wanted:
            self.engines.append(Ctbrowse(page, size, args.headed))
        real = [e for e in wanted if e != "ctbrowse"]
        if real:
            from playwright.sync_api import sync_playwright

            if not args.system_fonts:
                OUT.mkdir(parents=True, exist_ok=True)
                os.environ["FONTCONFIG_FILE"] = str(font_conf(OUT / "fonts.conf"))
            self.playwright = sync_playwright().start()
            for name in real:
                self.engines.append(Reference(name, self.playwright, page, size, args.headed))

    def run(self, cmd: dict) -> dict:
        verb = cmd["verb"]
        if verb == "stop":
            return {"ok": True, "stopping": True}
        if verb == "info":
            return {"ok": True, "engines": [e.name for e in self.engines], "delay": self.delay}

        # The pause is what makes this watchable. It goes BEFORE the input so
        # the human sees the page as it was, then the change - not a change
        # they have already missed.
        if verb not in ("shot",) and self.delay:
            time.sleep(self.delay)

        out: dict = {"ok": True}
        for engine in self.engines:
            try:
                out[engine.name] = self.dispatch(engine, cmd)
            except Exception as e:  # one engine failing must not kill the rig
                out[engine.name] = {"ok": False, "error": f"{type(e).__name__}: {e}"}
                out["ok"] = False
        return out

    def dispatch(self, engine, cmd: dict) -> dict:
        verb, a = cmd["verb"], cmd["args"]
        if verb == "click":
            return engine.click(float(a[0]), float(a[1]), int(a[2]) if len(a) > 2 else 0)
        if verb == "move":
            return engine.move(float(a[0]), float(a[1]))
        if verb == "down":
            return engine.down(float(a[0]), float(a[1]), int(a[2]) if len(a) > 2 else 0)
        if verb == "up":
            return engine.up(float(a[0]), float(a[1]), int(a[2]) if len(a) > 2 else 0)
        if verb == "type":
            return engine.type_(" ".join(a))
        if verb == "key":
            return engine.key(a[0], cmd.get("shift", False), cmd.get("ctrl", False))
        if verb == "wheel":
            at = (float(a[1]), float(a[2])) if len(a) > 2 else None
            return engine.wheel(float(a[0]), at)
        if verb == "eval":
            return engine.eval_(" ".join(a))
        if verb == "shot":
            name = a[0] if a else "shot"
            directory = OUT / "shots" / name
            directory.mkdir(parents=True, exist_ok=True)
            return engine.shot(directory / f"{engine.name}.png")
        return {"ok": False, "error": f"unknown verb: {verb}"}

    def close(self):
        for engine in self.engines:
            try:
                engine.close()
            except Exception:
                pass
        if self.playwright is not None:
            self.playwright.stop()


def serve(args) -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    PORTFILE.unlink(missing_ok=True)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    try:
        session = Session(args)
    except Exception as e:
        listener.close()
        print(f"compare.py: {e}", file=sys.stderr)
        return 1
    # Written only once the engines are UP, so a client that finds the file
    # knows there is something to talk to.
    PORTFILE.write_text(str(listener.getsockname()[1]))
    print(f"compare.py: {', '.join(e.name for e in session.engines)} ready", flush=True)

    try:
        while True:
            peer, _ = listener.accept()
            with peer, peer.makefile("rw") as io:
                line = io.readline()
                if not line:
                    continue
                try:
                    answer = session.run(json.loads(line))
                except Exception as e:
                    answer = {"ok": False, "error": f"{type(e).__name__}: {e}"}
                io.write(json.dumps(answer) + "\n")
                io.flush()
                if answer.get("stopping"):
                    break
    finally:
        session.close()
        listener.close()
        PORTFILE.unlink(missing_ok=True)
    return 0


def ask(payload: dict) -> int:
    if not PORTFILE.exists():
        print("compare.py: no session; run `tools/compare.py start <page.html>`", file=sys.stderr)
        return 1
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("127.0.0.1", int(PORTFILE.read_text())))
            with s.makefile("rw") as io:
                io.write(json.dumps(payload) + "\n")
                io.flush()
                answer = json.loads(io.readline())
    except OSError as e:
        print(f"compare.py: cannot reach the session: {e}", file=sys.stderr)
        return 1
    # WAIT FOR IT TO ACTUALLY GO. The daemon answers `stop` and only then
    # closes the browsers and drops the port file; returning immediately means
    # the next `start` finds a file whose port is already dead, which is what
    # anyone scripting a start/stop cycle hits on the first try.
    if answer.get("stopping"):
        for _ in range(100):
            if not PORTFILE.exists():
                break
            time.sleep(0.1)
    print(json.dumps(answer, indent=2))
    return 0 if answer.get("ok") else 1


def stale_session() -> bool:
    """A port file whose port nobody is listening on - a daemon that was killed."""
    if not PORTFILE.exists():
        return False
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(1)
            s.connect(("127.0.0.1", int(PORTFILE.read_text())))
        return False
    except (OSError, ValueError):
        return True


# --- setup and entry -------------------------------------------------------


def setup() -> int:
    """The venv Playwright lives in. Only needed for chrome/firefox."""
    if not VENV.exists():
        print(f"creating {VENV}")
        subprocess.run([sys.executable, "-m", "venv", str(VENV)], check=True)
    pip = VENV / "bin" / "pip"
    subprocess.run([str(pip), "install", "--quiet", "playwright"], check=True)
    subprocess.run([str(VENV / "bin" / "playwright"), "install", "chromium", "firefox"], check=True)
    print(f"ready: {VENV}")
    return 0


def reexec_in_venv() -> None:
    """Re-run under the venv when a real browser is wanted and Playwright is not here."""
    python = VENV / "bin" / "python3"
    if not python.exists():
        print("compare.py: playwright not installed; run `tools/compare.py setup`", file=sys.stderr)
        raise SystemExit(1)
    os.execv(str(python), [str(python), str(Path(__file__).resolve()), *sys.argv[1:]])


def engines_from(values: list[str]) -> list[str]:
    names: list[str] = []
    for value in values or ["ctbrowse,chrome"]:
        for part in value.split(","):
            part = part.strip().lower()
            if not part:
                continue
            if part not in ALIASES:
                raise SystemExit(f"compare.py: unknown engine '{part}'")
            if ALIASES[part] not in names:
                names.append(ALIASES[part])
    return names


def main() -> int:
    parser = argparse.ArgumentParser(prog="compare.py", description=__doc__.splitlines()[0])
    verbs = parser.add_subparsers(dest="verb", required=True)

    start = verbs.add_parser("start", help="open the page in every selected engine")
    start.add_argument("page")
    start.add_argument("--engine", action="append", dest="engine_values", metavar="NAME")
    start.add_argument("--headed", action="store_true", help="real windows, so a human can watch")
    start.add_argument("--delay", type=float, default=0, metavar="MS", help="pause before an input")
    start.add_argument("--size", type=int, nargs=2, default=[800, 600], metavar=("W", "H"))
    start.add_argument("--system-fonts", action="store_true", help="do not force ctbrowser's faces")
    start.add_argument("--foreground", action="store_true", help="do not detach the daemon")

    verbs.add_parser("setup", help="create the venv Playwright needs")
    verbs.add_parser("stop", help="close every engine")
    verbs.add_parser("info", help="what is running")
    for verb, count in (("click", 2), ("move", 2), ("down", 2), ("up", 2), ("wheel", 1)):
        p = verbs.add_parser(verb)
        p.add_argument("args", nargs="+", metavar="N", help=f"{count} or more numbers")
    for verb in ("type", "eval"):
        verbs.add_parser(verb).add_argument("args", nargs="+", metavar="TEXT")
    key = verbs.add_parser("key")
    key.add_argument("args", nargs=1, metavar="CODE", help='a DOM code: "Tab", "KeyA", "ArrowLeft"')
    key.add_argument("--shift", action="store_true")
    key.add_argument("--ctrl", action="store_true")
    shot = verbs.add_parser("shot")
    shot.add_argument("args", nargs="*", default=["shot"], metavar="NAME")

    args = parser.parse_args()

    if args.verb == "setup":
        return setup()

    if args.verb == "start":
        args.engines = engines_from(args.engine_values)
        if any(e != "ctbrowse" for e in args.engines):
            try:
                import playwright  # noqa: F401
            except ImportError:
                reexec_in_venv()
        if stale_session():
            PORTFILE.unlink(missing_ok=True)  # a daemon that was killed, not stopped
        if PORTFILE.exists():
            print("compare.py: a session is already running; `stop` it first", file=sys.stderr)
            return 1
        if args.foreground:
            return serve(args)
        # Detach, so the caller's shell gets its prompt back and the browsers
        # outlive it - the whole point is that the next command finds them.
        if os.fork() != 0:
            for _ in range(600):  # a first Playwright launch is not quick
                time.sleep(0.1)
                if PORTFILE.exists():
                    return ask({"verb": "info", "args": []})
            print("compare.py: the session did not come up", file=sys.stderr)
            return 1
        os.setsid()
        # DETACH THE STREAMS, or the daemon holds the caller's stdout open and
        # a pipeline never sees EOF - `compare.py start ... | head` hangs for
        # as long as the browsers live, which is forever.
        OUT.mkdir(parents=True, exist_ok=True)
        log = os.open(str(OUT / "session.log"), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
        null = os.open(os.devnull, os.O_RDONLY)
        os.dup2(null, 0)
        os.dup2(log, 1)
        os.dup2(log, 2)
        sys.exit(serve(args))

    return ask(
        {
            "verb": args.verb,
            "args": getattr(args, "args", []),
            "shift": getattr(args, "shift", False),
            "ctrl": getattr(args, "ctrl", False),
        }
    )


if __name__ == "__main__":
    raise SystemExit(main())
