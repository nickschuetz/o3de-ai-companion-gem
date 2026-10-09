# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""The manual undo functions in ai_companion.api must return JSON like every
other public function. They used to be re-exported straight from
safety.rollback and returned None, which the live suite caught when
``print(rollback_last_batch())`` produced ``None`` instead of JSON."""

import json
import os
import sys
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Editor", "Scripts"))

from ai_companion import api  # noqa: E402
from ai_companion.safety import rollback  # noqa: E402


class TestUndoApiReturnsJson(unittest.TestCase):
    def setUp(self):
        rollback._batch_depth = 0

    def test_begin_and_end_return_json_and_track_depth(self):
        begun = json.loads(api.begin_undo_batch("Live"))
        self.assertEqual(begun["status"], "ok")
        self.assertEqual(begun["data"], {"label": "Live", "depth": 1})
        ended = json.loads(api.end_undo_batch())
        self.assertEqual(ended["status"], "ok")
        self.assertEqual(ended["data"]["depth"], 0)

    def test_rollback_returns_json_outside_the_editor(self):
        result = json.loads(api.rollback_last_batch())
        self.assertEqual(result["status"], "ok")
        self.assertFalse(result["data"]["rolled_back"])  # no azlmbr here
        self.assertEqual(result["data"]["leftover_entities_deleted"], 0)

    def test_rollback_calls_the_editors_undo(self):
        # The live suite found rollback leaving entities behind: the bus has no
        # "Undo" event and the call failed silently, so general.undo() never ran.
        calls = []
        legacy = types.ModuleType("azlmbr.legacy")
        legacy.__path__ = []
        general = types.ModuleType("azlmbr.legacy.general")
        general.undo = lambda: calls.append("undo")
        root = types.ModuleType("azlmbr")
        root.__path__ = []
        root.legacy = legacy
        legacy.general = general
        modules = {"azlmbr": root, "azlmbr.legacy": legacy, "azlmbr.legacy.general": general}
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(api.rollback_last_batch())
        self.assertEqual(result["status"], "ok")
        self.assertTrue(result["data"]["rolled_back"])
        self.assertEqual(calls, ["undo"])

    @staticmethod
    def _editor_stub(existing, deleted, undo_calls):
        """azlmbr stubs: EntityExists consults ``existing``, DeleteEntityById removes."""
        def tools_bus(call_type, event, *args):
            if event == "EntityExists":
                return args[0] in existing
            if event == "DeleteEntityById":
                existing.discard(args[0]); deleted.append(args[0])
                return None
            if event in ("BeginUndoBatch", "EndUndoBatch"):
                return None
            raise AssertionError(f"unexpected event {event}")
        editor = types.ModuleType("azlmbr.editor"); editor.ToolsApplicationRequestBus = tools_bus
        bus = types.ModuleType("azlmbr.bus"); bus.Broadcast = object()
        legacy = types.ModuleType("azlmbr.legacy"); legacy.__path__ = []
        general = types.ModuleType("azlmbr.legacy.general"); general.undo = lambda: undo_calls.append("undo")
        legacy.general = general
        root = types.ModuleType("azlmbr"); root.__path__ = []
        root.editor, root.bus, root.legacy = editor, bus, legacy
        return {"azlmbr": root, "azlmbr.editor": editor, "azlmbr.bus": bus,
                "azlmbr.legacy": legacy, "azlmbr.legacy.general": general}

    def test_rollback_deletes_entities_the_undo_left_behind(self):
        # The engine can leave a Lua-scripted entity in place after Undo; the
        # gem recorded what it created and finishes the job.
        existing, deleted, undo_calls = {11, 22}, [], []
        modules = self._editor_stub(existing, deleted, undo_calls)
        rollback._last_batch_entities = [11, 22, 33]
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(api.rollback_last_batch())
        self.assertEqual(undo_calls, ["undo"])
        self.assertEqual(sorted(deleted), [11, 22])
        self.assertEqual(result["data"], {"rolled_back": True, "leftover_entities_deleted": 2})
        self.assertEqual(rollback._last_batch_entities, [])

    def test_exception_inside_an_api_call_becomes_a_rolled_back_error(self):
        existing, deleted, undo_calls = set(), [], []
        modules = self._editor_stub(existing, deleted, undo_calls)
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(api.create_enemy("Bad", ai_type="no_such_ai"))
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["code"], "validation_failed")
        self.assertTrue(result["rolled_back"])
        self.assertEqual(result["details"]["exception"], "ValueError")
        self.assertEqual(result["details"]["operation"], "Create Enemy")
        self.assertEqual(undo_calls, ["undo"])

    def test_sandbox_limit_becomes_limit_exceeded(self):
        result = json.loads(api.create_grid("Block", rows=50, cols=50))
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["code"], "limit_exceeded")
        self.assertTrue(result["rolled_back"])

    def test_every_advertised_function_exists(self):
        for entry in json.loads(api.get_available_functions())["data"]:
            self.assertTrue(callable(getattr(api, entry["name"], None)), entry["name"])


if __name__ == "__main__":
    unittest.main()
