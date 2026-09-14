#!/usr/bin/env python3
"""Bounded regression checks for the comparison rig; no browser/build required."""

import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import compare


class ComparisonDeadlines(unittest.TestCase):
    def test_reply_and_trickled_reply_obey_one_deadline(self):
        for trickle in (False, True):
            with self.subTest(trickle=trickle), tempfile.TemporaryDirectory() as temp:
                portfile = Path(temp) / "session.port"
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
                            with self.assertRaises(TimeoutError):
                                compare.request({"verb": "info", "args": []})
                            self.assertLess(time.monotonic() - started, 1)
                    finally:
                        finished.set()
                        worker.join(timeout=1)
                    self.assertFalse(worker.is_alive())

    def test_startup_partial_line_times_out_and_reaps_child(self):
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
                with self.assertRaises(TimeoutError):
                    compare.Ctbrowse(Path("unused"), (1, 1), False)
            self.assertIsNotNone(proc.poll())
            self.assertTrue(proc.stdout.closed)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=1)
            proc.stdout.close()

    def test_force_cleanup_reaps_child_ignoring_terminate(self):
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
            self.assertEqual(proc.stdout.readline(), b"ready\n")
            with patch.object(compare, "CLOSE_TIMEOUT", 0.05):
                compare.stop_process(proc)
            self.assertEqual(proc.returncode, -signal.SIGKILL)
            self.assertTrue(proc.stdout.closed)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=1)
            proc.stdout.close()

    def test_nested_deadline_is_not_cancelled_and_action_timeout_propagates(self):
        started = time.monotonic()
        with self.assertRaises(TimeoutError), compare.time_limit(0.1):
            with compare.time_limit(1):
                time.sleep(0.01)
            time.sleep(1)
        self.assertLess(time.monotonic() - started, 0.5)
        session = object.__new__(compare.Session)
        session.delay = 0
        session.engines = [type("Stalled", (), {"name": "stalled"})(), object()]
        with patch.object(session, "dispatch", side_effect=TimeoutError("stalled")) as dispatch:
            with self.assertRaises(TimeoutError):
                session.run({"verb": "eval", "args": ["while(true){}"]})
            self.assertEqual(dispatch.call_count, 1)

    def test_idle_client_does_not_strand_daemon(self):
        with tempfile.TemporaryDirectory() as temp:
            portfile = Path(temp) / "session.port"
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
                [sys.executable, "-c", script, str(Path(compare.__file__).parent), temp],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            try:
                until = time.monotonic() + 3
                while not portfile.exists() and proc.poll() is None and time.monotonic() < until:
                    time.sleep(0.01)
                self.assertTrue(portfile.exists())
                with socket.create_connection(("127.0.0.1", int(portfile.read_text()))) as idle:
                    idle.sendall(b'{"verb":')  # no newline
                    time.sleep(0.15)
                    with (
                        patch.object(compare, "PORTFILE", portfile),
                        patch.object(compare, "REQUEST_TIMEOUT", 1),
                    ):
                        self.assertTrue(compare.request({"verb": "info"})["ok"])
                        self.assertTrue(compare.request({"verb": "stop"})["ok"])
                out, err = proc.communicate(timeout=2)
                self.assertEqual(proc.returncode, 0, (out, err))
                self.assertFalse(portfile.exists())
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=2)

    def test_stop_reports_cleanup_timeout(self):
        with tempfile.TemporaryDirectory() as temp:
            portfile = Path(temp) / "session.port"
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
                    with self.assertRaisesRegex(TimeoutError, "shutting down"):
                        compare.request({"verb": "stop"})
                worker.join(timeout=1)
                self.assertFalse(worker.is_alive())

    def test_timed_out_driver_request_reaps_child(self):
        proc = subprocess.Popen(
            [
                sys.executable,
                "-u",
                "-c",
                "import socket,time; s=socket.socket(); s.bind(('127.0.0.1',0)); s.listen(); "
                "print('listening on 127.0.0.1:'+str(s.getsockname()[1])); "
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
                with self.assertRaises(TimeoutError):
                    driver.send(cmd="eval", script="stalled")
                self.assertIsNone(driver.sock)
                self.assertIsNotNone(proc.poll())
                driver.close()  # repeated cleanup is harmless
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=1)
            proc.stdout.close()

    def test_detached_startup_failure_reaps_daemon(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
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
                    patch.object(
                        sys, "argv", [str(fake), "start", "unused", "--engine", "ctbrowse"]
                    ),
                ):
                    self.assertEqual(compare.main(), 1)
                self.assertEqual(len(created), 1)
                self.assertIsNotNone(created[0].poll())
                self.assertFalse((out / "session.port").exists())
            finally:
                for proc in created:
                    if proc.poll() is None:
                        os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait(timeout=1)

    def test_parity_records_reference_versions(self):
        spec = importlib.util.spec_from_file_location(
            "css_parity_review_test", Path(compare.__file__).with_name("css-parity.py")
        )
        parity = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = parity
        spec.loader.exec_module(parity)
        with (
            tempfile.TemporaryDirectory() as temp,
            patch.object(parity, "RECORD", Path(temp) / "parity.txt"),
        ):
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
            self.assertIn(json.dumps(versions, sort_keys=True), parity.RECORD.read_text())
            self.assertEqual(
                parity.read_record()[f"bootstrap-{parity.FIXTURES[0]}.html"]["elements"], 1
            )


if __name__ == "__main__":
    unittest.main()
