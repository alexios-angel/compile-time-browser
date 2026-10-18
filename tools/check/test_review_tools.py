#!/usr/bin/env python3
"""Unit tests for compare.py (the ctbrowser-vs-real-browser driver) and css-parity.py.

Covers request deadlines, child-process cleanup, and the parity record format.
Needs no browser or build.
"""

import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import threading
import time
from unittest.mock import patch

import pytest

import compare


@pytest.mark.parametrize("trickle", [False, True])
def test_reply_and_trickled_reply_obey_one_deadline(tmp_path, trickle):
    portfile = tmp_path / "session.port"
    finished = threading.Event()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        portfile.write_text(str(listener.getsockname()[1]))

        def stall():
            peer, _ = listener.accept()
            with peer:
                peer.recv(4096)
                while not finished.wait(0.01):
                    if trickle:
                        try:
                            peer.sendall(b" ")
                        except OSError:
                            break

        worker = threading.Thread(target=stall, daemon=True)
        worker.start()
        try:
            with (
                patch.object(compare, "PORTFILE", portfile),
                patch.object(compare, "REQUEST_TIMEOUT", 0.1),
            ):
                started = time.monotonic()
                with pytest.raises(TimeoutError):
                    compare.request({"verb": "info", "args": []})
                assert time.monotonic() - started < 1
        finally:
            finished.set()
            worker.join(timeout=1)
        assert not worker.is_alive()


def test_startup_partial_line_times_out_and_reaps_child():
    proc = subprocess.Popen(
        [sys.executable, "-u", "-c", "import time; print('listening', end=''); time.sleep(10)"],
        stdout=subprocess.PIPE,
    )
    try:
        with (
            patch.object(compare.Ctbrowse, "_spawn_local", return_value=proc),
            patch.object(compare, "START_TIMEOUT", 0.1),
            patch.object(compare, "CLOSE_TIMEOUT", 0.1),
        ):
            with pytest.raises(TimeoutError):
                compare.Ctbrowse(Path("unused"), (1, 1), False)
        assert proc.poll() is not None
        assert proc.stdout.closed
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=1)
        proc.stdout.close()


def test_force_cleanup_reaps_child_ignoring_terminate():
    proc = subprocess.Popen(
        [
            sys.executable,
            "-u",
            "-c",
            "import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
            "print('ready'); time.sleep(10)",
        ],
        stdout=subprocess.PIPE,
    )
    try:
        assert proc.stdout.readline() == b"ready\n"
        with patch.object(compare, "CLOSE_TIMEOUT", 0.05):
            compare.stop_process(proc)
        assert proc.returncode == -signal.SIGKILL
        assert proc.stdout.closed
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=1)
        proc.stdout.close()


def test_nested_deadline_is_not_cancelled_and_action_timeout_propagates():
    started = time.monotonic()
    with pytest.raises(TimeoutError), compare.time_limit(0.1):
        with compare.time_limit(1):
            time.sleep(0.01)
        time.sleep(1)
    assert time.monotonic() - started < 0.5
    session = object.__new__(compare.Session)
    session.delay = 0
    session.engines = [type("Stalled", (), {"name": "stalled"})(), object()]
    with patch.object(session, "dispatch", side_effect=TimeoutError("stalled")) as dispatch:
        with pytest.raises(TimeoutError):
            session.run({"verb": "eval", "args": ["while(true){}"]})
        assert dispatch.call_count == 1


def test_idle_client_does_not_strand_daemon(tmp_path):
    portfile = tmp_path / "session.port"
    script = (
        "import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); "
        "import compare; "
        "compare.OUT=Path(sys.argv[2]); compare.PORTFILE=compare.OUT/'session.port'; "
        "compare.REQUEST_TIMEOUT=.1; "
        "compare.Session=lambda _: type('Rig',(),{'engines':[],"
        "'run':lambda self,c: {'ok':True,'stopping':c['verb']=='stop'},"
        "'close':lambda self: None})(); "
        "raise SystemExit(compare.serve(None))"
    )
    proc = subprocess.Popen(
        [sys.executable, "-c", script, str(Path(compare.__file__).parent), str(tmp_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        until = time.monotonic() + 3
        while not portfile.exists() and proc.poll() is None and time.monotonic() < until:
            time.sleep(0.01)
        assert portfile.exists()
        with socket.create_connection(("127.0.0.1", int(portfile.read_text()))) as idle:
            idle.sendall(b'{"verb":')  # no newline
            time.sleep(0.15)
            with (
                patch.object(compare, "PORTFILE", portfile),
                patch.object(compare, "REQUEST_TIMEOUT", 1),
            ):
                assert compare.request({"verb": "info"})["ok"]
                assert compare.request({"verb": "stop"})["ok"]
        out, err = proc.communicate(timeout=2)
        assert proc.returncode == 0, (out, err)
        assert not portfile.exists()
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.communicate(timeout=2)


def test_stop_reports_cleanup_timeout(tmp_path):
    portfile = tmp_path / "session.port"
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        portfile.write_text(str(listener.getsockname()[1]))

        def respond():
            peer, _ = listener.accept()
            with peer:
                peer.recv(4096)
                peer.sendall(b'{"ok":true,"stopping":true}\n')

        worker = threading.Thread(target=respond, daemon=True)
        worker.start()
        with (
            patch.object(compare, "PORTFILE", portfile),
            patch.object(compare, "REQUEST_TIMEOUT", 0.1),
        ):
            with pytest.raises(TimeoutError, match="shutting down"):
                compare.request({"verb": "stop"})
        worker.join(timeout=1)
        assert not worker.is_alive()


def test_timed_out_driver_request_reaps_child():
    proc = subprocess.Popen(
        [
            sys.executable,
            "-u",
            "-c",
            "import socket,time; s=socket.socket(); s.bind(('127.0.0.1',0)); s.listen(); "
            "print('listening on 127.0.0.1:'+str(s.getsockname()[1])+' token 0f'); "
            "peer,_=s.accept(); peer.recv(4096); time.sleep(10)",
        ],
        stdout=subprocess.PIPE,
    )
    try:
        with (
            patch.object(compare.Ctbrowse, "_spawn_local", return_value=proc),
            patch.object(compare, "REQUEST_TIMEOUT", 0.1),
            patch.object(compare, "CLOSE_TIMEOUT", 0.1),
        ):
            driver = compare.Ctbrowse(Path("unused"), (1, 1), False)
            with pytest.raises(TimeoutError):
                driver.send(cmd="eval", script="stalled")
            assert driver.sock is None
            assert proc.poll() is not None
            driver.close()  # repeated cleanup is harmless
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=1)
        proc.stdout.close()


def test_detached_startup_failure_reaps_daemon(tmp_path):
    out = tmp_path
    fake = out / "fake.py"
    fake.write_text("import time; time.sleep(10)\n")
    created = []
    spawn = subprocess.Popen

    def record(*args, **kwargs):
        proc = spawn(*args, **kwargs)
        created.append(proc)
        return proc

    try:
        with (
            patch.object(compare, "OUT", out),
            patch.object(compare, "PORTFILE", out / "session.port"),
            patch.object(compare, "__file__", str(fake)),
            patch.object(compare, "START_TIMEOUT", 0.1),
            patch.object(compare, "CLOSE_TIMEOUT", 0.1),
            patch.object(compare.subprocess, "Popen", side_effect=record),
            patch.object(sys, "argv", [str(fake), "start", "unused", "--engine", "ctbrowse"]),
        ):
            assert compare.main() == 1
        assert len(created) == 1
        assert created[0].poll() is not None
        assert not (out / "session.port").exists()
    finally:
        for proc in created:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
            proc.wait(timeout=1)


def test_parity_records_reference_versions(tmp_path):
    spec = importlib.util.spec_from_file_location(
        "css_parity_review_test", Path(compare.__file__).with_name("css-parity.py")
    )
    parity = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = parity
    spec.loader.exec_module(parity)
    with patch.object(parity, "RECORD", tmp_path / "parity.txt"):
        versions = {"playwright": "observed-playwright", "chrome": "observed-browser"}
        parity.write_record(
            {
                parity.FIXTURES[0]: {
                    "versions": versions,
                    "elements": 1,
                    "findings": [],
                    "substituted": 0,
                }
            }
        )
        assert json.dumps(versions, sort_keys=True) in parity.RECORD.read_text()
        assert parity.read_record()[f"bootstrap-{parity.FIXTURES[0]}.html"]["elements"] == 1
