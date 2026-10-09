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

Against an editor started with ``AI_COMPANION_SECURE_MODE=1`` only
``TestSecureMode`` runs (``scripts/ci_live_test.sh`` does that with
``LIVE_SECURE=1``); every other class goes through ``execute_python``, which
secure mode refuses, so they skip themselves. ``TestSecureMode`` in turn
skips against an editor that is not in secure mode.
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


_secure_mode: bool | None = None


def _secure_mode_enabled(client: AgentClient) -> bool:
    """Whether the editor's AgentServer is in secure mode, asked once per run.

    ``get_api_version`` is answered on the server's network thread, so this
    costs one round trip and cannot block on the editor's main loop.
    """
    global _secure_mode
    if _secure_mode is None:
        response = client.request("get_api_version")
        info = json.loads(response["output"]) if response.get("status") == "ok" else {}
        _secure_mode = info.get("secure_mode") is True
    return _secure_mode


class LiveEditorTest(unittest.TestCase):
    client: AgentClient
    # Classes that drive the editor through execute_python cannot run in
    # secure mode and skip there; TestSecureMode sets this to True.
    runs_in_secure_mode = False

    @classmethod
    def setUpClass(cls) -> None:
        cls.client = AgentClient()
        pong = cls.client.request("ping")
        if pong.get("status") != "ok":
            raise unittest.SkipTest(f"AgentServer did not answer ping: {pong}")
        if not cls.runs_in_secure_mode and _secure_mode_enabled(cls.client):
            raise unittest.SkipTest("secure mode: execute_python is disabled")

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


class TestNativeMutations(LiveEditorTest):
    """create_entity / set_transform / delete_entity served in C++ with their own undo batches."""

    def _ids(self) -> set[int]:
        return {int(e["id"]) for e in json.loads(self.client.request("get_scene_snapshot")["output"])["entities"]}

    def test_create_set_transform_delete_round_trip(self):
        before = self._ids()
        created = self.native("create_entity", name="LiveNative", position=[2, 3, 4])
        self.assertEqual(created["status"], "ok", created)
        info = json.loads(created["output"])
        new_id = int(info["entity_id"])
        self.assertIn(new_id, self._ids() - before)
        self.assertEqual(info["name"], "LiveNative")

        moved = self.native("set_transform", entity_id=new_id, position=[5, 6, 7], rotation=[0, 0, 90], scale=2)
        self.assertEqual(moved["status"], "ok", moved)
        entity = json.loads(moved["output"])
        self.assertAlmostEqual(entity["position"][0], 5.0, places=3)
        self.assertAlmostEqual(entity["scale"][0], 2.0, places=3)
        self.assertAlmostEqual(abs(entity["rotation"][2]), 90.0, places=1)

        deleted = self.native("delete_entity", entity_id=new_id)
        self.assertEqual(deleted["status"], "ok", deleted)
        self.assertNotIn(new_id, self._ids())

    def test_create_entity_is_undoable(self):
        before = self._ids()
        created = self.native("create_entity", name="LiveNativeUndo")
        new_id = int(json.loads(created["output"])["entity_id"])
        self.assertIn(new_id, self._ids())
        self.client.run("import azlmbr.legacy.general as g\ng.undo()\n")
        if new_id in self._ids():
            self.native("delete_entity", entity_id=new_id)
            self.fail("create_entity was not undone by one editor Undo step")
        self.assertEqual(self._ids(), before)

    def test_validation_refuses_bad_input_before_touching_the_level(self):
        before = self._ids()
        bad_name = self.native("create_entity", name="9bad")
        self.assertEqual(bad_name["status"], "error")
        self.assertIn("invalid entity name", bad_name["error"])
        bad_pos = self.native("create_entity", name="Fine", position=[1, 2, 1e9])
        self.assertEqual(bad_pos["status"], "error")
        missing = self.native("set_transform", entity_id=987654321, position=[0, 0, 0])
        self.assertEqual(missing["status"], "error")
        self.assertIn("does not exist", missing["error"])
        self.assertEqual(self._ids(), before)

    def test_delete_refuses_the_level_root(self):
        # The level's container is the root with children (other parentless
        # entities can appear in roots too), named "Level" in a stock level.
        tree = json.loads(self.native("get_entity_tree")["output"])
        roots = tree["roots"]
        named = [r for r in roots if r["name"] == "Level"]
        root = named[0] if named else max(roots, key=lambda r: len(r.get("children", [])))
        root_id = root["id"]
        refused = self.native("delete_entity", entity_id=root_id)
        self.assertEqual(refused["status"], "error", refused)
        self.assertIn("root", refused["error"])
        self.assertIn(int(root_id), self._ids())


class TestTemplatesAndRollback(LiveEditorTest):
    def _entity_ids(self) -> set[int]:
        snapshot = json.loads(self.client.request("get_scene_snapshot")["output"])
        return {int(e["id"]) for e in snapshot["entities"]}

    def _delete(self, entity_id: int) -> None:
        self.client.run(
            "import azlmbr.bus as bus, azlmbr.editor as editor, azlmbr.entity as entity\n"
            f"editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityById', entity.EntityId({entity_id}))\n"
        )

    def test_create_player_adds_an_entity_and_rollback_removes_it(self):
        # movement=None keeps the Lua Script component off; see the next test.
        before = self._entity_ids()
        created = self.client.api('create_player("LivePlayer", position=[1, 2, 1], movement=None)')
        self.assertEqual(created["status"], "ok", created)
        new_id = _entity_number(created["data"]["entity_id"])
        self.assertIn(new_id, self._entity_ids() - before)

        entity = json.loads(self.client.request("get_entity", entity_id=new_id)["output"])
        if "error" not in entity:
            self.assertEqual(entity["name"], "LivePlayer")
            self.assertAlmostEqual(entity["position"][0], 1.0, places=3)

        rolled = self.client.api("rollback_last_batch()")
        self.assertEqual(rolled["status"], "ok", rolled)
        if new_id in self._entity_ids():
            self._delete(new_id)
            self.fail("rollback did not remove the created entity")

    def test_rollback_of_an_entity_with_a_lua_script(self):
        # Engine limitation on O3DE 26.10: undoing an entity creation goes
        # through prefab re-instantiation, and when the entity carries a Lua
        # Script whose asset is already loaded, ScriptEditorComponent::LoadScript
        # opens an undo batch from inside the undo, ToolsApplication rejects it,
        # and the entity survives the Undo. The gem records every entity it
        # creates and rollback deletes the survivors, so the outcome is the same
        # either way; the response says whether that path was needed.
        before = self._entity_ids()
        created = self.client.api('create_enemy("LiveChaser", position=[6, 6, 1])')
        self.assertEqual(created["status"], "ok", created)
        new_id = _entity_number(created["data"]["entity_id"])
        self.assertIn(new_id, self._entity_ids() - before)

        rolled = self.client.api("rollback_last_batch()")
        self.assertEqual(rolled["status"], "ok", rolled)
        self.assertEqual(self.client.request("ping")["status"], "ok")
        self.assertIn("leftover_entities_deleted", rolled["data"])
        if new_id in self._entity_ids():
            self._delete(new_id)
            self.fail(f"entity survived rollback even after survivor deletion: {rolled}")

    def test_failed_call_returns_a_rolled_back_error_not_a_traceback(self):
        before = self._entity_ids()
        result = self.client.api('create_enemy("LiveBadEnemy", ai_type="no_such_ai")')
        self.assertEqual(result["status"], "error", result)
        self.assertEqual(result["code"], "validation_failed")
        self.assertTrue(result["rolled_back"])
        self.assertEqual(self._entity_ids(), before)

    def test_create_entity_batch_then_rollback(self):
        before = self._entity_ids()
        spec = json.dumps([{"name": "LiveBatchA", "position": [3, 0, 0]}, {"name": "LiveBatchB", "position": [4, 0, 0]}])
        result = self.client.api(f"create_entity_batch({spec})")
        self.assertEqual(result["status"], "ok", result)
        added = self._entity_ids() - before
        self.assertEqual(len(added), 2, result)

        self.client.api("rollback_last_batch()")
        self.assertFalse(added & self._entity_ids())


class TestMultiEntityPersistence(LiveEditorTest):
    """Every entity of a multi-entity call must keep its configuration.

    Creating an entity through the prefab system propagates the level template,
    which re-instantiates entities from the template DOM and wipes live changes
    not yet captured there. Before the builder gave each entity its own undo
    batch, only the last entity of bootstrap_scene / create_entity_batch kept
    its name, transform and components (the launcher check saved a ground as a
    bare "Entity2"). A follow-up creation forces the propagation here.
    """

    def _entity(self, entity_id) -> dict:
        return json.loads(self.client.request("get_entity", entity_id=_entity_number(entity_id))["output"])

    def test_bootstrap_scene_entities_survive_the_next_creation(self):
        result = self.client.api("bootstrap_scene(ground_size=4.0)")
        self.assertEqual(result["status"], "ok", result)
        ids = {e["name"]: e["entity_id"] for e in result["data"]["scene_entities"] if e.get("entity_id")}
        self.assertIn("Ground", ids, result)

        self.client.api('create_entity_batch([{"name": "LiveAfterBootstrap", "position": [1, 1, 0]}])')

        ground = self._entity(ids["Ground"])
        self.assertEqual(ground["name"], "Ground")
        self.assertAlmostEqual(ground["scale"][0], 4.0, places=3)
        self.assertAlmostEqual(ground["position"][2], -2.0, places=3)
        comps = " ".join(ground["components"])
        self.assertIn("Collider", comps, ground)
        self.assertIn("RigidBody", comps, ground)

        self.client.api("rollback_last_batch()")
        self.client.api("rollback_last_batch()")

    def test_entity_batch_keeps_every_name(self):
        spec = json.dumps([{"name": f"LiveBatch{i}", "position": [i, 0, 0]} for i in range(3)])
        result = self.client.api(f"create_entity_batch({spec})")
        self.assertEqual(result["status"], "ok", result)
        self.client.api('create_entity_batch([{"name": "LiveBatchTail", "position": [9, 0, 0]}])')
        names = {self._entity(e["entity_id"])["name"] for e in result["data"]["entities"]}
        self.assertEqual(names, {"LiveBatch0", "LiveBatch1", "LiveBatch2"})
        self.client.api("rollback_last_batch()")
        self.client.api("rollback_last_batch()")


class TestPrefabGuard(LiveEditorTest):
    def test_missing_prefab_is_refused_and_the_editor_survives(self):
        result = self.client.api('spawn_prefab("Live_Definitely_Missing")')
        self.assertEqual(result["status"], "error", result)
        self.assertEqual(result["code"], "prefab_not_found")
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


class TestSecureMode(LiveEditorTest):
    """What the AgentServer serves and refuses under AI_COMPANION_SECURE_MODE=1.

    Needs an editor started in secure mode and no particular level: the
    native read types answer on whatever is loaded, which may be nothing.
    """

    runs_in_secure_mode = True

    @classmethod
    def setUpClass(cls) -> None:
        super().setUpClass()
        if not _secure_mode_enabled(cls.client):
            raise unittest.SkipTest("editor is not in secure mode; start it with AI_COMPANION_SECURE_MODE=1")

    def test_api_version_reports_secure_mode(self):
        response = self.client.request("get_api_version")
        self.assertEqual(response["status"], "ok", response)
        self.assertIs(json.loads(response["output"])["secure_mode"], True)

    def test_execute_python_is_refused(self):
        response = self.client.execute_python("print(1)")
        self.assertEqual(response["status"], "error", response)
        self.assertIn("secure mode", str(response.get("error", "")).lower())

    def test_read_only_request_types_answer(self):
        requests = [
            ("ping", {}),
            ("get_api_version", {}),
            ("get_scene_snapshot", {}),
            ("get_entity_tree", {}),
            ("validate_scene", {}),
            ("get_bus_schema", {"bus_name": "AiCompanionRequestBus"}),
        ]
        for request_type, fields in requests:
            with self.subTest(request_type=request_type):
                response = self.client.request(request_type, **fields)
                self.assertEqual(response["status"], "ok", response)

    def test_native_outputs_are_json_without_errors(self):
        snapshot = json.loads(self.client.request("get_scene_snapshot")["output"])
        self.assertIn("entities", snapshot)
        tree = json.loads(self.client.request("get_entity_tree")["output"])
        self.assertIn("roots", tree)
        report = json.loads(self.client.request("validate_scene")["output"])
        self.assertIn("warnings", report)
        schema = json.loads(self.client.request("get_bus_schema", bus_name="AiCompanionRequestBus")["output"])
        self.assertNotIn("error", schema, schema)
        self.assertIn("events", schema)

    def test_native_mutations_work_without_python(self):
        # The point of the validated mutation set: an agent on a secure-mode
        # editor can still build and tidy a scene, with no execute_python.
        created = self.client.request("create_entity", name="SecureNative", position=[1, 2, 3])
        self.assertEqual(created["status"], "ok", created)
        new_id = int(json.loads(created["output"])["entity_id"])
        ids = {int(e["id"]) for e in json.loads(self.client.request("get_scene_snapshot")["output"])["entities"]}
        self.assertIn(new_id, ids)

        moved = self.client.request("set_transform", entity_id=new_id, position=[4, 5, 6])
        self.assertEqual(moved["status"], "ok", moved)
        self.assertAlmostEqual(json.loads(moved["output"])["position"][0], 4.0, places=3)

        deleted = self.client.request("delete_entity", entity_id=new_id)
        self.assertEqual(deleted["status"], "ok", deleted)
        ids = {int(e["id"]) for e in json.loads(self.client.request("get_scene_snapshot")["output"])["entities"]}
        self.assertNotIn(new_id, ids)


if __name__ == "__main__":
    unittest.main()
