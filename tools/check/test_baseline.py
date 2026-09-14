#!/usr/bin/env python3
"""Baseline provenance checks with temporary files and mocked build commands."""

import hashlib
import importlib.util
import json
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("baseline", Path(__file__).with_name("baseline.py"))
baseline = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(baseline)


class BaselineTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()
        self.binary = self.root / baseline.BINARY
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"measured executable")
        self.corpus = self.root / "fixture.js"
        self.corpus.write_text("let measured = 1;")
        self.cache = self.root / "build/CMakeCache.txt"
        self.cache.write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/fixture/tool chain/c++\n"
            "CMAKE_BUILD_TYPE:STRING=Debug\n"
            "CMAKE_CXX_FLAGS:STRING=-O0 -DMEASURED\n"
        )
        self.ninja = self.root / "build/build.ninja"
        self.ninja.write_text("# configured build\n")
        self.commands = "'/fixture/tool chain/c++' -O0 -DMEASURED fixture.cpp -o fixture.o\n"
        self.version = "Configured C++ 13.3\n"
        self.git_valid = False
        self.mutation = None
        self.calls = []
        for target, replacement in (
            ("CORPORA", [("fixture", "fixture.js")]),
            ("run_command", self.run_command),
            ("machine", lambda: {"cpu": "measured CPU"}),
        ):
            active = patch.object(baseline, target, replacement)
            active.start()
            self.addCleanup(active.stop)

    def run_command(self, command, cwd=None):
        self.calls.append(command)
        if command[0] == "cmake":
            return ""
        if command[0] == "ninja":
            return self.commands
        if command[-1] == "--version":
            return self.version
        if command[0] == "git":
            if not self.git_valid:
                raise subprocess.CalledProcessError(128, command, stderr="unavailable .git")
            if command[-1] == "--show-toplevel":
                return str(self.root) + "\n"
            if command[-1] == "HEAD":
                return "f" * 40 + "\n"
            return " M fixture.js\n"
        if command[0] == str(self.binary):
            if self.mutation:
                self.mutation()
            return json.dumps({"runs": [{"corpus": "fixture", "stages": []}]})
        self.fail(f"unexpected command: {command!r}")

    def test_artifacts_and_configuration_are_observed_without_local_head(self):
        report = baseline.collect(self.root)
        identity = report["identity"]
        self.assertEqual(identity["source"], {"revision": None, "dirty": None})
        self.assertEqual(
            identity["binary"]["sha256"], hashlib.sha256(self.binary.read_bytes()).hexdigest()
        )
        self.assertEqual(
            identity["corpora"][0]["sha256"], hashlib.sha256(self.corpus.read_bytes()).hexdigest()
        )
        configuration = identity["configuration"]
        self.assertEqual(configuration["build_type"], "Debug")
        self.assertEqual(configuration["compiler_path"], "/fixture/tool chain/c++")
        self.assertEqual(configuration["compiler_version"], "Configured C++ 13.3")
        self.assertEqual(configuration["commands"], self.commands.splitlines())
        self.assertIn("not verified", identity["kind"])
        self.assertNotIn("commit", report)
        for command in self.calls:
            if command[0] == "git":
                self.assertEqual(command[2], str(self.root))

    def test_valid_remote_git_metadata_preserves_dirty_state(self):
        self.git_valid = True
        source = baseline.collect(self.root)["identity"]["source"]
        self.assertEqual(source, {"revision": "f" * 40, "dirty": True})

    def test_changed_files_refuse_measurement(self):
        for path in (self.binary, self.corpus, self.cache, self.ninja):
            with self.subTest(path=path):
                original = path.read_bytes()
                self.mutation = lambda: path.write_bytes(original + b"\nchanged\n")
                with self.assertRaisesRegex(ValueError, "changed during measurement"):
                    baseline.collect(self.root)
                path.write_bytes(original)

    def test_changed_build_commands_refuse_measurement(self):
        self.mutation = lambda: setattr(self, "commands", "c++ -O3 changed.cpp\n")
        with self.assertRaisesRegex(ValueError, "changed during measurement"):
            baseline.collect(self.root)

    def test_missing_and_invalid_configuration_refuse_measurement(self):
        self.cache.unlink()
        with self.assertRaises(FileNotFoundError):
            baseline.collect(self.root)
        self.cache.write_text("CMAKE_GENERATOR:INTERNAL=Ninja\n")
        with self.assertRaisesRegex(ValueError, "compiler are required"):
            baseline.collect(self.root)
        self.assertFalse(any(command[0] == str(self.binary) for command in self.calls))

    def test_empty_observed_commands_or_version_refuse_measurement(self):
        for field in ("commands", "version"):
            with self.subTest(field=field):
                saved = getattr(self, field)
                setattr(self, field, "")
                with self.assertRaisesRegex(ValueError, "must not be empty"):
                    baseline.collect(self.root)
                setattr(self, field, saved)

    def test_ssh_uses_current_collector_and_quotes_remote_path(self):
        remote = "~/projects/a 'quoted' directory; literal"
        done = subprocess.CompletedProcess([], 0, '{"identity": {}}', "")
        with patch.object(baseline.subprocess, "run", return_value=done) as execute:
            self.assertEqual(baseline.on_box("devbox", remote), {"identity": {}})
        positional, keywords = execute.call_args
        command = positional[0]
        self.assertEqual(command[:2], ["ssh", "devbox"])
        self.assertEqual(shlex.split(command[2]), ["python3", "-", "--collect", remote])
        self.assertEqual(keywords["input"], Path(baseline.__file__).read_text())
        self.assertTrue(keywords["check"])
        self.assertGreater(keywords["timeout"], 0)


if __name__ == "__main__":
    unittest.main()
