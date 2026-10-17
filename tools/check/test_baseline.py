#!/usr/bin/env python3
"""Unit tests for baseline.py.

The benchmark report must record the binary, corpus and build configuration it
measured and refuse to report if any of them changes during the run. cmake,
ninja, git and the benchmark binary are mocked.
"""

import hashlib
import importlib.util
import json
import shlex
import subprocess
from pathlib import Path
from unittest.mock import patch

import pytest

SPEC = importlib.util.spec_from_file_location("baseline", Path(__file__).with_name("baseline.py"))
baseline = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(baseline)


class Rig:
    """A measured tree under tmp_path plus canned tool output."""

    def __init__(self, root):
        self.root = root
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

    def run_command(self, command, cwd=None):
        """Stand-in for baseline.run_command: canned output per tool, and the
        benchmark binary runs self.mutation to simulate a mid-run change."""
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
        pytest.fail(f"unexpected command: {command!r}")


@pytest.fixture
def rig(tmp_path, monkeypatch):
    rig = Rig(tmp_path.resolve())
    monkeypatch.setattr(baseline, "CORPORA", [("fixture", "fixture.js")])
    monkeypatch.setattr(baseline, "run_command", rig.run_command)
    monkeypatch.setattr(baseline, "machine", lambda: {"cpu": "measured CPU"})
    return rig


def test_artifacts_and_configuration_are_observed_without_local_head(rig):
    report = baseline.collect(rig.root)
    identity = report["identity"]
    assert identity["source"] == {"revision": None, "dirty": None}
    assert identity["binary"]["sha256"] == hashlib.sha256(rig.binary.read_bytes()).hexdigest()
    assert identity["corpora"][0]["sha256"] == hashlib.sha256(rig.corpus.read_bytes()).hexdigest()
    configuration = identity["configuration"]
    assert configuration["build_type"] == "Debug"
    assert configuration["compiler_path"] == "/fixture/tool chain/c++"
    assert configuration["compiler_version"] == "Configured C++ 13.3"
    assert configuration["commands"] == rig.commands.splitlines()
    assert "not verified" in identity["kind"]
    assert "commit" not in report
    for command in rig.calls:
        if command[0] == "git":
            assert command[2] == str(rig.root)


def test_valid_remote_git_metadata_preserves_dirty_state(rig):
    rig.git_valid = True
    source = baseline.collect(rig.root)["identity"]["source"]
    assert source == {"revision": "f" * 40, "dirty": True}


@pytest.mark.parametrize("name", ["binary", "corpus", "cache", "ninja"])
def test_changed_files_refuse_measurement(rig, name):
    path = getattr(rig, name)
    original = path.read_bytes()
    rig.mutation = lambda: path.write_bytes(original + b"\nchanged\n")
    with pytest.raises(ValueError, match="changed during measurement"):
        baseline.collect(rig.root)


def test_changed_build_commands_refuse_measurement(rig):
    rig.mutation = lambda: setattr(rig, "commands", "c++ -O3 changed.cpp\n")
    with pytest.raises(ValueError, match="changed during measurement"):
        baseline.collect(rig.root)


def test_missing_and_invalid_configuration_refuse_measurement(rig):
    rig.cache.unlink()
    with pytest.raises(FileNotFoundError):
        baseline.collect(rig.root)
    rig.cache.write_text("CMAKE_GENERATOR:INTERNAL=Ninja\n")
    with pytest.raises(ValueError, match="compiler are required"):
        baseline.collect(rig.root)
    assert not any(command[0] == str(rig.binary) for command in rig.calls)


@pytest.mark.parametrize("field", ["commands", "version"])
def test_empty_observed_commands_or_version_refuse_measurement(rig, field):
    setattr(rig, field, "")
    with pytest.raises(ValueError, match="must not be empty"):
        baseline.collect(rig.root)


def test_ssh_uses_current_collector_and_quotes_remote_path():
    remote = "~/projects/a 'quoted' directory; literal"
    done = subprocess.CompletedProcess([], 0, '{"identity": {}}', "")
    with patch.object(baseline.subprocess, "run", return_value=done) as execute:
        assert baseline.on_box("devbox", remote) == {"identity": {}}
    positional, keywords = execute.call_args
    command = positional[0]
    assert command[:2] == ["ssh", "devbox"]
    assert shlex.split(command[2]) == ["python3", "-", "--collect", remote]
    assert keywords["input"] == Path(baseline.__file__).read_text()
    assert keywords["check"]
    assert keywords["timeout"] > 0
