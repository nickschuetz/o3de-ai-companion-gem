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

    def test_rollback_returns_json(self):
        result = json.loads(api.rollback_last_batch())
        self.assertEqual(result["status"], "ok")
        self.assertTrue(result["data"]["rolled_back"])

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
        self.assertEqual(calls, ["undo"])

    def test_every_advertised_function_exists(self):
        for entry in json.loads(api.get_available_functions())["data"]:
            self.assertTrue(callable(getattr(api, entry["name"], None)), entry["name"])


if __name__ == "__main__":
    unittest.main()
