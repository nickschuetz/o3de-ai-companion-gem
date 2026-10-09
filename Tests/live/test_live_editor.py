# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Live tests against a running O3DE Editor with the AiCompanion gem.

Skipped unless ``O3DE_LIVE_EDITOR_TEST=1``. The editor must be running with
a level open and its AgentServer listening on ``O3DE_EDITOR_HOST`` /
``O3DE_EDITOR_PORT`` (defaults 127.0.0.1:4600). ``scripts/ci_live_test.sh``
brings all of that up and tears it down.

These are the checks the unit suites cannot make: the Python package
imports inside the editor, the templates produce real entities, the native
request types answer, and the prefab guard refuses a missing prefab without
taking the editor down. Every mutation is undone through the gem's own
rollback so the level is left as it was found.
"""

from __future__ import annotations

import json
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "Editor", "Scripts"))

from agent_client import AgentClient  # noqa: E402

GEM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

if os.environ.get("O3DE_LIVE_EDITOR_TEST", "").strip() != "1":
    raise unittest.SkipTest("live editor tests are opt-in; set O3DE_LIVE_EDITOR_TEST=1")


def _entity_number(entity_id: str | int) -> int:
    match = re.search(r"\d+", str(entity_id))
    assert match, f"no numeric id in {entity_id!r}"
    return int(match.group(0))


class LiveEditorTest(unittest.TestCase):
    client: AgentClient

    @classmethod
    def setUpClass(cls) -> None:
        cls.client = AgentClient()
        pong = cls.client.request("ping")
        if pong.get("status") != "ok":
            raise unittest.SkipTest(f"AgentServer did not answer ping: {pong}")

    def native(self, request_type: str, **fields):
        """Call a native request type; skip the test if this gem build lacks it."""
        response = self.client.request(request_type, **fields)
        if response.get("status") == "error" and "Unknown request type" in str(response.get("error", "")):
            self.skipTest(f"{request_type} is not served by this gem build")
        return response


class TestProtocol(LiveEditorTest):
    def test_api_version_matches_the_checkout(self):
        response = self.client.request("get_api_version")
        self.assertEqual(response["status"], "ok", response)
        info = json.loads(response["output"])
        with open(os.path.join(GEM_ROOT, "gem.json"), encoding="utf-8") as fh:
            gem_version = json.load(fh)["version"]
        from ai_companion.version import API_VERSION

        self.assertEqual(info["protocol_version"], 1)
        self.assertEqual(info["gem_version"], gem_version, "C++ gem_version differs from gem.json; AGENTS.md lists the files to sync")
        self.assertEqual(info["api_version"], API_VERSION)
        self.assertIn("secure_mode", info)

    def test_unknown_request_type_is_an_error_not_a_hang(self):
        response = self.client.request("definitely_not_a_type")
        self.assertEqual(response["status"], "error")
        self.assertIn("Unknown request type", response["error"])


class TestPythonPackage(LiveEditorTest):
    def test_ai_companion_imports_and_reports_its_version(self):
        from ai_companion.version import API_VERSION

        result = self.client.api("get_api_version()")
        self.assertEqual(result["status"], "ok", result)
        self.assertEqual(result["data"]["version"], API_VERSION)

    def test_available_functions_all_exist(self):
        listed = self.client.api("get_available_functions()")["data"]
        names = ", ".join(sorted({f["name"] for f in listed}))
        output = self.client.run(f"from ai_companion.api import {names}\nprint('imported')\n")
        self.assertIn("imported", output)


class TestNativeRequestTypes(LiveEditorTest):
    def test_scene_snapshot_tree_and_validation_agree_on_entity_count(self):
        snapshot = json.loads(self.native("get_scene_snapshot")["output"])
        tree = json.loads(self.native("get_entity_tree")["output"])
        report = json.loads(self.native("validate_scene")["output"])

        self.assertIn("entities", snapshot)
        self.assertEqual(snapshot["entity_count"], len(snapshot["entities"]))
        self.assertIn("roots", tree)
        self.assertEqual(report["entity_count"], snapshot["entity_count"])
        self.assertIn("warnings", report)

    def test_get_entity_matches_the_snapshot(self):
        snapshot = json.loads(self.native("get_scene_snapshot")["output"])
        self.assertTrue(snapshot["entities"], "level has no entities")
        sample = snapshot["entities"][0]

        response = self.native("get_entity", entity_id=sample["id"])
        self.assertEqual(response["status"], "ok", response)
        entity = json.loads(response["output"])
        self.assertEqual(entity["id"], sample["id"])
        self.assertEqual(entity["name"], sample["name"])
        self.assertEqual(entity["components"], sample["components"])

        as_string = self.native("get_entity", entity_id=f"[{sample['id']}]")
        self.assertEqual(json.loads(as_string["output"])["id"], sample["id"])

    def test_get_entity_unknown_id_is_a_json_error(self):
        response = self.native("get_entity", entity_id=123456789)
        self.assertEqual(response["status"], "ok", response)
        self.assertIn("error", json.loads(response["output"]))

        missing = self.native("get_entity")
        self.assertEqual(missing["status"], "error")

    def test_get_bus_schema_describes_the_gem_bus(self):
        listing = json.loads(self.native("get_bus_schema")["output"])
        self.assertIn("AiCompanionRequestBus", listing["buses"])

        schema = json.loads(self.native("get_bus_schema", bus_name="AiCompanionRequestBus")["output"])
        events = {e["name"] for e in schema["events"]}
        self.assertTrue({"GetSceneSnapshot", "GetEntityTree", "ValidateScene"} <= events, events)

        unknown = json.loads(self.native("get_bus_schema", bus_name="NoSuchBus_12345")["output"])
        self.assertIn("error", unknown)


class TestTemplatesAndRollback(LiveEditorTest):
    def _entity_ids(self) -> set[int]:
        snapshot = json.loads(self.client.request("get_scene_snapshot")["output"])
        return {int(e["id"]) for e in snapshot["entities"]}

    def test_create_player_adds_an_entity_and_rollback_removes_it(self):
        before = self._entity_ids()
        created = self.client.api('create_player("LivePlayer", position=[1, 2, 1], movement="twin_stick")')
        self.assertEqual(created["status"], "ok", created)
        new_id = _entity_number(created["data"]["entity_id"])
        self.assertIn(new_id, self._entity_ids() - before)

        entity = json.loads(self.client.request("get_entity", entity_id=new_id)["output"])
        if "error" not in entity:
            self.assertEqual(entity["name"], "LivePlayer")
            self.assertAlmostEqual(entity["position"][0], 1.0, places=3)

        rolled = self.client.api("rollback_last_batch()")
        self.assertEqual(rolled["status"], "ok", rolled)
        self.assertNotIn(new_id, self._entity_ids(), "rollback did not remove the created entity")

    def test_create_entity_batch_then_rollback(self):
        before = self._entity_ids()
        spec = json.dumps([{"name": "LiveBatchA", "position": [3, 0, 0]}, {"name": "LiveBatchB", "position": [4, 0, 0]}])
        result = self.client.api(f"create_entity_batch({spec})")
        self.assertEqual(result["status"], "ok", result)
        added = self._entity_ids() - before
        self.assertEqual(len(added), 2, result)

        self.client.api("rollback_last_batch()")
        self.assertFalse(added & self._entity_ids())


class TestPrefabGuard(LiveEditorTest):
    def test_missing_prefab_is_refused_and_the_editor_survives(self):
        result = self.client.api('spawn_prefab("Live_Definitely_Missing")')
        self.assertEqual(result["status"], "error", result)
        self.assertEqual(result["details"]["code"], "prefab_not_found")
        self.assertEqual(self.client.request("ping")["status"], "ok")

    def test_shipped_prefab_spawns_then_rolls_back(self):
        before = {int(e["id"]) for e in json.loads(self.client.request("get_scene_snapshot")["output"])["entities"]}
        result = self.client.api('spawn_prefab("Enemy_Chaser", position=[5, 5, 1])')
        self.assertEqual(result["status"], "ok", result)
        self.assertTrue(result["data"]["spawned"])
        after = {int(e["id"]) for e in json.loads(self.client.request("get_scene_snapshot")["output"])["entities"]}
        self.assertTrue(after - before, "spawn_prefab reported success but no entity appeared")

        self.client.api("rollback_last_batch()")
        final = {int(e["id"]) for e in json.loads(self.client.request("get_scene_snapshot")["output"])["entities"]}
        self.assertFalse((after - before) & final, "rollback left prefab entities behind")


if __name__ == "__main__":
    unittest.main()
