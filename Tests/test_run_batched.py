# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Smoke tests for Examples/TwinStickShooter/run_batched.py.

The runner needs o3de-mcp and the ``mcp`` 2.x SDK at runtime. CI has neither,
so ``mcp`` and ``o3de_mcp`` are stubbed here; the tests check that the module
imports against the mcp 2.x surface it declares (``MCPServer`` from
``mcp.server``), that a run opens one session, executes the setup and every
step in it, and ends the session, and that error detection catches the
failure shapes the tools produce.
"""

import asyncio
import importlib
import json
import os
import sys
import types
import unittest
from unittest import mock

EXAMPLE_DIR = os.path.join(os.path.dirname(__file__), "..", "Examples", "TwinStickShooter")


class _Content:
    def __init__(self, text):
        self.text = text


class _Result:
    def __init__(self, text):
        self.content = [_Content(text)]


class _FakeMCPServer:
    """Records call_tool invocations and answers like the o3de-mcp session tools."""

    calls = []
    fail_step = None

    def __init__(self, name):
        self.name = name

    async def call_tool(self, tool, kwargs):
        _FakeMCPServer.calls.append((tool, kwargs))
        if tool == "begin_session":
            return _Result(json.dumps({"session_id": "abcd1234"}))
        if tool == "exec_in_session":
            if _FakeMCPServer.fail_step and _FakeMCPServer.fail_step in kwargs["script"]:
                return _Result(json.dumps({"error": "boom"}))
            return _Result("ok\n")
        if tool == "end_session":
            return _Result(json.dumps({"ended": kwargs["session_id"]}))
        raise AssertionError(f"unexpected tool {tool}")


def _load_runner():
    mcp_pkg = types.ModuleType("mcp")
    mcp_pkg.__path__ = []
    mcp_server = types.ModuleType("mcp.server")
    mcp_server.MCPServer = _FakeMCPServer
    o3de_pkg = types.ModuleType("o3de_mcp")
    o3de_pkg.__path__ = []
    tools_pkg = types.ModuleType("o3de_mcp.tools")
    tools_pkg.__path__ = []
    editor = types.ModuleType("o3de_mcp.tools.editor")
    editor.register_editor_tools = lambda mcp: None
    modules = {
        "mcp": mcp_pkg,
        "mcp.server": mcp_server,
        "o3de_mcp": o3de_pkg,
        "o3de_mcp.tools": tools_pkg,
        "o3de_mcp.tools.editor": editor,
    }
    sys.modules.pop("run_batched", None)
    with mock.patch.dict(sys.modules, modules):
        sys.path.insert(0, EXAMPLE_DIR)
        try:
            return importlib.import_module("run_batched")
        finally:
            sys.path.remove(EXAMPLE_DIR)


class TestRunBatched(unittest.TestCase):
    def setUp(self):
        _FakeMCPServer.calls = []
        _FakeMCPServer.fail_step = None
        self.runner = _load_runner()

    def test_imports_against_the_mcp2_surface(self):
        self.assertIs(self.runner.MCPServer, _FakeMCPServer)
        self.assertTrue(self.runner.STEPS)
        self.assertTrue(all(len(step) == 2 for step in self.runner.STEPS))

    def test_setup_puts_gem_scripts_on_the_path_once(self):
        self.assertIn("Editor/Scripts", self.runner.SETUP.replace("\\", "/"))
        self.assertIn("import steps", self.runner.SETUP)
        for _, script in self.runner.STEPS:
            self.assertNotIn("sys.path", script)

    def test_run_uses_one_session_for_every_step(self):
        rc = asyncio.run(self.runner.main())
        self.assertEqual(rc, 0)
        tools = [tool for tool, _ in _FakeMCPServer.calls]
        self.assertEqual(tools[0], "begin_session")
        self.assertEqual(tools[-1], "end_session")
        execs = [kw for tool, kw in _FakeMCPServer.calls if tool == "exec_in_session"]
        self.assertEqual(len(execs), 1 + len(self.runner.STEPS))
        self.assertTrue(all(kw["session_id"] == "abcd1234" for kw in execs))
        self.assertEqual(execs[0]["script"], self.runner.SETUP)

    def test_failed_step_stops_the_run_and_still_ends_the_session(self):
        _FakeMCPServer.fail_step = "build_arena"
        rc = asyncio.run(self.runner.main())
        self.assertEqual(rc, 1)
        tools = [tool for tool, _ in _FakeMCPServer.calls]
        self.assertEqual(tools[-1], "end_session")
        execs = [kw for tool, kw in _FakeMCPServer.calls if tool == "exec_in_session"]
        self.assertIn("build_arena", execs[-1]["script"])

    def test_failure_detection(self):
        failed = self.runner._failed
        self.assertTrue(failed(json.dumps({"status": "error", "code": "timeout", "message": "x"})))
        self.assertTrue(failed(json.dumps({"error": "Session x not found"})))
        self.assertTrue(failed("Could not connect to O3DE Editor"))
        self.assertFalse(failed(json.dumps({"status": "ok", "data": {"entity_id": "[1]"}})))
        self.assertFalse(failed("Using already-open level 'DefaultLevel'."))


if __name__ == "__main__":
    unittest.main()
