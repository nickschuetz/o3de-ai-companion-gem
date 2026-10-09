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
import shutil
import sys
import time
import unittest
from pathlib import Path, PurePosixPath

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "Editor", "Scripts"))

from agent_client import AgentClient  # noqa: E402

GEM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# The anim graph fixture (see Tests/live/fixtures/README.md) and where it goes
# in the host project; scripts/ci_live_test.sh copies it there before starting
# AssetProcessor, and TestAnimGraphs copies it itself when it is missing.
ANIM_GRAPH_FIXTURE = Path(__file__).resolve().parent / "fixtures" / "AiCompanionSample.animgraph"
ANIM_GRAPH_PROJECT_SUBDIR = Path("Assets") / "AiCompanionLiveTest"
ANIM_GRAPH_PRODUCT_PATH = "assets/aicompanionlivetest/aicompanionsample.animgraph"
ANIM_GRAPH_ASSET_TIMEOUT_S = 120.0


def _is_fixture_graph(file_name: str) -> bool:
    """Whether an anim graph's reported file name is the fixture.

    The engine reports the absolute path of the loaded product, with the
    platform's separators (backslashes on Windows) and the catalog's casing,
    so normalize separators and case and compare the product-path tail.
    """
    normalized = PurePosixPath(file_name.replace("\\", "/").lower())
    tail = PurePosixPath(ANIM_GRAPH_PRODUCT_PATH)
    return normalized.parts[-len(tail.parts):] == tail.parts

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
        if response.get("status") == "error" and (
            response.get("code") == "unknown_request_type" or "Unknown request type" in str(response.get("error", ""))
        ):
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
        self.assertEqual(response.get("code"), "unknown_request_type", response)

    def test_other_errors_carry_their_own_code(self):
        # Every error reply carries a code. o3de-mcp falls back to editor
        # Python on unknown_request_type alone, so a malformed argument must
        # answer validation_failed, never that one.
        response = self.client.request("get_entity", entity_id="not-an-id")
        self.assertEqual(response["status"], "error")
        self.assertEqual(response.get("code"), "validation_failed", response)
        self.assertIn("entity_id", response["error"])

    def test_malformed_requests_are_validation_failures(self):
        self.assertEqual(self.client.request("")["code"], "validation_failed")
        missing_script = self.client.request("execute_python")
        if missing_script.get("code") == "secure_mode":
            self.skipTest("secure mode refuses execute_python before reading its script")
        self.assertEqual(missing_script["status"], "error", missing_script)
        self.assertEqual(missing_script["code"], "validation_failed", missing_script)


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
    def test_native_ids_are_decimal_strings(self):
        # 64-bit entity ids exceed 2^53, so every id in native output is a
        # decimal string (API_VERSION 0.4.0); inputs accept either form.
        snapshot = json.loads(self.native("get_scene_snapshot")["output"])
        self.assertTrue(snapshot["entities"], "the level has no entities")
        for entity in snapshot["entities"]:
            self.assertRegex(entity["id"], r"^\d+$")
            if entity["parent_id"] is not None:
                self.assertRegex(entity["parent_id"], r"^\d+$")
        tree = json.loads(self.native("get_entity_tree")["output"])
        self.assertRegex(tree["roots"][0]["id"], r"^\d+$")
        sample = snapshot["entities"][0]
        as_number = self.native("get_entity", entity_id=int(sample["id"]))
        self.assertEqual(json.loads(as_number["output"])["id"], sample["id"])
        created = json.loads(self.native("create_entity", name="IdStringProbe")["output"])
        try:
            self.assertRegex(created["entity_id"], r"^\d+$")
            moved = json.loads(self.native("set_transform", entity_id=created["entity_id"], position=[1, 1, 1])["output"])
            self.assertEqual(moved["id"], created["entity_id"])
        finally:
            deleted = json.loads(self.native("delete_entity", entity_id=created["entity_id"])["output"])
        self.assertEqual(deleted["deleted"], created["entity_id"])

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

    def test_get_entity_unknown_id_is_not_found(self):
        # A well-formed id that no entity has: status error with not_found,
        # not an ok reply with an error object inside output.
        response = self.native("get_entity", entity_id=123456789)
        self.assertEqual(response["status"], "error", response)
        self.assertEqual(response.get("code"), "not_found", response)
        self.assertIn("No entity with id 123456789", response["error"])
        self.assertEqual(response["output"], "")

        missing = self.native("get_entity")
        self.assertEqual(missing["status"], "error")
        self.assertEqual(missing.get("code"), "validation_failed", missing)

    def test_get_bus_schema_describes_the_gem_bus(self):
        listing = json.loads(self.native("get_bus_schema")["output"])
        self.assertIn("AiCompanionRequestBus", listing["buses"])

        schema = json.loads(self.native("get_bus_schema", bus_name="AiCompanionRequestBus")["output"])
        events = {e["name"] for e in schema["events"]}
        self.assertTrue({"GetSceneSnapshot", "GetEntityTree", "ValidateScene"} <= events, events)

        unknown = self.native("get_bus_schema", bus_name="NoSuchBus_12345")
        self.assertEqual(unknown["status"], "error", unknown)
        self.assertEqual(unknown.get("code"), "not_found", unknown)
        self.assertIn("NoSuchBus_12345", unknown["error"])


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
        self.assertEqual(bad_name.get("code"), "validation_failed", bad_name)
        self.assertIn("invalid entity name", bad_name["error"])
        bad_pos = self.native("create_entity", name="Fine", position=[1, 2, 1e9])
        self.assertEqual(bad_pos["status"], "error")
        self.assertEqual(bad_pos.get("code"), "validation_failed", bad_pos)
        no_name = self.native("create_entity")
        self.assertEqual(no_name.get("code"), "validation_failed", no_name)
        missing = self.native("set_transform", entity_id=987654321, position=[0, 0, 0])
        self.assertEqual(missing["status"], "error")
        self.assertEqual(missing.get("code"), "not_found", missing)
        self.assertIn("does not exist", missing["error"])
        bad_scale = self.native("set_transform", entity_id=987654321, scale="big")
        self.assertEqual(bad_scale.get("code"), "validation_failed", bad_scale)
        gone = self.native("delete_entity", entity_id=987654321)
        self.assertEqual(gone.get("code"), "not_found", gone)
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
        self.assertEqual(refused.get("code"), "validation_failed", refused)
        self.assertIn("root", refused["error"])
        self.assertIn(int(root_id), self._ids())

    def test_mutations_refuse_a_protected_entity(self):
        # Protected names (EditorGlobal, SystemEntity, AZ::*) are refused by
        # the native mutations in C++ and by the Python package before it
        # touches an existing entity. The name validator never lets
        # create_entity make an "AZ::" name, so the probe is made the way a
        # system entity already exists: through editor Python.
        before = self._ids()
        printed = self.client.run(
            "import azlmbr.bus as bus\n"
            "import azlmbr.editor as editor\n"
            "import azlmbr.entity as entity\n"
            "eid = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())\n"
            "editor.EditorEntityAPIBus(bus.Event, 'SetName', eid, 'AZ::Probe')\n"
            "print(eid)\n"
        )
        probe_id = _entity_number(printed.strip().splitlines()[-1])
        self.assertIn(probe_id, self._ids() - before)
        try:
            deleted = self.native("delete_entity", entity_id=probe_id)
            self.assertEqual(deleted["status"], "error", deleted)
            self.assertEqual(deleted.get("code"), "validation_failed", deleted)
            self.assertIn("entity is protected", deleted["error"])
            moved = self.native("set_transform", entity_id=probe_id, position=[1, 2, 3])
            self.assertEqual(moved["status"], "error", moved)
            self.assertEqual(moved.get("code"), "validation_failed", moved)
            self.assertIn("entity is protected", moved["error"])
            self.assertIn(probe_id, self._ids())
            # The Python package's only mutation that names an existing entity
            # is parenting a new one under it; it refuses before creating anything.
            result = self.client.api(f'create_projectile_spawner("ProtectedParentProbe", parent_entity_id={probe_id})')
            self.assertEqual(result["status"], "error", result)
            self.assertEqual(result["code"], "validation_failed", result)
            self.assertIn("entity is protected", result["message"])
            self.assertEqual(self._ids() - before, {probe_id})
        finally:
            self.client.run(
                "import azlmbr.bus as bus\n"
                "import azlmbr.editor as editor\n"
                "import azlmbr.entity as entity\n"
                f"editor.ToolsApplicationRequestBus(bus.Broadcast, 'DeleteEntityById', entity.EntityId({probe_id}))\n"
            )
        self.assertEqual(self._ids(), before)


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

        looked_up = self.client.request("get_entity", entity_id=new_id)
        if looked_up.get("status") == "ok":
            entity = json.loads(looked_up["output"])
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


class TestAnimGraphs(LiveEditorTest):
    """The anim graph request types served in C++ from EMotion FX.

    The read types (``list_anim_graphs``, ``get_anim_graph``) are checked
    against the asset-loaded fixture; the authoring types build, save, reload
    and tear down their own graph through EMotion Studio's command system.

    The fixture graph is copied into the host project (``AICOMPANION_PROJECT``),
    built by AssetProcessor, and loaded by an Anim Graph component on an entity
    this class creates; EMotion FX registers the loaded graph with its
    AnimGraphManager, which is what the request types read. Teardown deletes
    the entity and removes the copied file when this class copied it.
    """

    entity_id: int = 0
    graph_id: int | None = None
    copied_fixture: Path | None = None

    @classmethod
    def setUpClass(cls) -> None:
        super().setUpClass()
        probe = cls.client.request("list_anim_graphs")
        if probe.get("status") == "error" and probe.get("code") == "unknown_request_type":
            raise unittest.SkipTest("list_anim_graphs is not served by this gem build")
        project = os.environ.get("AICOMPANION_PROJECT", "").strip()
        if not project:
            raise unittest.SkipTest("AICOMPANION_PROJECT is not set; the anim graph fixture cannot be copied into the project")
        cls._place_fixture(project)
        cls._wait_for_asset()
        cls._create_fixture_entity()
        cls.graph_id = cls._wait_for_graph()

    @classmethod
    def tearDownClass(cls) -> None:
        if cls.entity_id:
            cls.client.request("delete_entity", entity_id=cls.entity_id)
            cls.entity_id = 0
        if cls.copied_fixture and cls.copied_fixture.exists():
            cls.copied_fixture.unlink()
            try:
                cls.copied_fixture.parent.rmdir()
            except OSError:
                pass  # the directory holds something else; leave it
            cls.copied_fixture = None

    @classmethod
    def _place_fixture(cls, project: str) -> None:
        # The copy lives here, not only in scripts/ci_live_test.sh, so the
        # class works wherever the suite runs (the script is Linux-only).
        target = Path(project) / ANIM_GRAPH_PROJECT_SUBDIR / ANIM_GRAPH_FIXTURE.name
        if target.exists():
            return  # scripts/ci_live_test.sh put it there and will remove it
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ANIM_GRAPH_FIXTURE, target)
        cls.copied_fixture = target

    @classmethod
    def _wait_for_asset(cls) -> None:
        # AssetProcessor has to notice the file and build it; poll the catalog.
        script = (
            "import azlmbr.bus as bus, azlmbr.asset as asset, azlmbr.math as math\n"
            f"aid = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', {ANIM_GRAPH_PRODUCT_PATH!r}, math.Uuid(), False)\n"
            "print('ASSET_VALID=' + str(aid is not None and aid.is_valid()))\n"
        )
        deadline = time.monotonic() + ANIM_GRAPH_ASSET_TIMEOUT_S
        last = ""
        while time.monotonic() < deadline:
            last = cls.client.run(script)
            if "ASSET_VALID=True" in last:
                return
            time.sleep(3)
        raise unittest.SkipTest(f"{ANIM_GRAPH_PRODUCT_PATH} never appeared in the asset catalog: {last.strip()}")

    @classmethod
    def _create_fixture_entity(cls) -> None:
        # An Actor component is required by the Anim Graph component
        # (EMotionFXActorService); it needs no actor asset to activate.
        # Setting the asset property fires the component's ChangeNotify, which
        # queues the asset load that registers the graph with EMotion FX.
        script = (
            "import azlmbr.bus as bus, azlmbr.editor as editor, azlmbr.entity as entity\n"
            "import azlmbr.asset as asset, azlmbr.math as math\n"
            "eid = editor.ToolsApplicationRequestBus(bus.Broadcast, 'CreateNewEntity', entity.EntityId())\n"
            "editor.EditorEntityAPIBus(bus.Event, 'SetName', eid, 'LiveAnimGraph')\n"
            "game = entity.EntityType().Game\n"
            "actor_type = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Actor'], game)[0]\n"
            "graph_type = editor.EditorComponentAPIBus(bus.Broadcast, 'FindComponentTypeIdsByEntityType', ['Anim Graph'], game)[0]\n"
            "editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', eid, [actor_type])\n"
            "added = editor.EditorComponentAPIBus(bus.Broadcast, 'AddComponentsOfType', eid, [graph_type])\n"
            "pair = added.GetValue()[0]\n"
            f"aid = asset.AssetCatalogRequestBus(bus.Broadcast, 'GetAssetIdByPath', {ANIM_GRAPH_PRODUCT_PATH!r}, math.Uuid(), False)\n"
            "outcome = editor.EditorComponentAPIBus(bus.Broadcast, 'SetComponentProperty', pair, 'Anim graph', aid)\n"
            "print('SET_OK=' + str(outcome.IsSuccess()))\n"
            "print('ENTITY=' + str(eid.ToString()))\n"
        )
        output = cls.client.run(script)
        match = re.search(r"ENTITY=\[?(\d+)", output)
        if not match:
            raise unittest.SkipTest(f"could not create the fixture entity: {output.strip()}")
        cls.entity_id = int(match.group(1))
        if "SET_OK=True" not in output:
            cls.tearDownClass()
            raise unittest.SkipTest(f"could not assign the anim graph asset: {output.strip()}")

    @classmethod
    def _wait_for_graph(cls) -> int:
        deadline = time.monotonic() + 60.0
        last: dict = {}
        while time.monotonic() < deadline:
            response = cls.client.request("list_anim_graphs")
            if response.get("status") == "ok":
                last = json.loads(response["output"])
                for graph in last.get("anim_graphs", []):
                    if _is_fixture_graph(graph["file_name"]):
                        return int(graph["id"])
            else:
                last = response
            time.sleep(2)
        cls.tearDownClass()
        raise unittest.SkipTest(f"the fixture graph never appeared in list_anim_graphs: {last}")

    def test_list_anim_graphs_describes_the_fixture(self):
        listing = json.loads(self.native("list_anim_graphs")["output"])
        self.assertIn("editor_mode", listing)
        graph = next(g for g in listing["anim_graphs"] if g["id"] == self.graph_id)
        self.assertTrue(_is_fixture_graph(graph["file_name"]), graph)
        self.assertTrue(graph["owned_by_asset"], graph)
        self.assertEqual(graph["num_nodes"], 3)
        self.assertEqual(graph["num_parameters"], 1)
        self.assertIsInstance(graph["instances"], list)
        for instance in graph["instances"]:
            # 64-bit entity ids travel as decimal strings (or null).
            self.assertTrue(instance["entity_id"] is None or re.fullmatch(r"\d+", instance["entity_id"]), instance)

    def test_get_anim_graph_by_id(self):
        response = self.native("get_anim_graph", anim_graph_id=self.graph_id)
        self.assertEqual(response["status"], "ok", response)
        graph = json.loads(response["output"])
        self.assertEqual(graph["id"], self.graph_id)

        nodes = {n["id"]: n for n in graph["nodes"]}
        self.assertEqual(len(nodes), 3, graph["nodes"])
        self.assertEqual({n["name"] for n in nodes.values()}, {"Root", "Idle", "WalkForward"})
        root = nodes[graph["root_state_machine_id"]]
        self.assertEqual(root["name"], "Root")
        self.assertIsNone(root["parent_id"])
        self.assertEqual(root["type"], "AnimGraphStateMachine")
        for name in ("Idle", "WalkForward"):
            node = next(n for n in nodes.values() if n["name"] == name)
            self.assertEqual(node["parent_id"], root["id"])
            self.assertEqual(node["type"], "AnimGraphMotionNode")
            self.assertTrue(node["can_act_as_state"])
            self.assertEqual(len(node["position"]), 2)

        self.assertEqual(len(graph["transitions"]), 1, graph["transitions"])
        transition = graph["transitions"][0]
        self.assertEqual(transition["state_machine_id"], root["id"])
        self.assertEqual(nodes[transition["source_node_id"]]["name"], "Idle")
        self.assertEqual(nodes[transition["target_node_id"]]["name"], "WalkForward")
        self.assertFalse(transition["wildcard"])
        self.assertAlmostEqual(transition["blend_time"], 0.3, places=3)
        self.assertEqual(len(transition["conditions"]), 1)
        self.assertEqual(transition["conditions"][0]["type"], "AnimGraphParameterCondition")

        self.assertEqual(len(graph["parameters"]), 1, graph["parameters"])
        parameter = graph["parameters"][0]
        self.assertEqual(parameter["name"], "Speed")
        self.assertIsNone(parameter["group"])
        self.assertIsNotNone(parameter["min"])
        self.assertEqual(graph["node_groups"], [])

    def test_get_anim_graph_by_id_string_and_file_name_agree(self):
        by_id = json.loads(self.native("get_anim_graph", anim_graph_id=str(self.graph_id))["output"])
        by_name = json.loads(self.native("get_anim_graph", file_name="AiCompanionSample.animgraph")["output"])
        self.assertEqual(by_id["id"], self.graph_id)
        self.assertEqual(by_name["id"], self.graph_id)

    def test_create_and_remove_anim_graph(self):
        # The write path: EMotion Studio's command system reached from the
        # gem's module (CommandSystem::GetCommandManager() is module-local and
        # null there; EMStudio::GetManager() is the cross-module accessor).
        created = self.native("create_anim_graph")
        if created.get("status") == "error" and created.get("code") == "unavailable":
            self.skipTest(f"EMotion Studio (the Animation Editor's command system) is not loaded: {created['error']}")
        self.assertEqual(created["status"], "ok", created)
        new_graph = json.loads(created["output"])
        self.assertIsInstance(new_graph["id"], int)
        self.assertEqual(new_graph["file_name"], "")
        try:
            listing = json.loads(self.native("list_anim_graphs")["output"])
            entry = next(g for g in listing["anim_graphs"] if g["id"] == new_graph["id"])
            self.assertFalse(entry["owned_by_asset"], entry)
            described = json.loads(self.native("get_anim_graph", anim_graph_id=new_graph["id"])["output"])
            self.assertEqual(len(described["nodes"]), 1, described["nodes"])  # the root state machine
            self.assertEqual(described["nodes"][0]["type"], "AnimGraphStateMachine")
        finally:
            removed = self.native("remove_anim_graph", anim_graph_id=new_graph["id"])
        self.assertEqual(removed["status"], "ok", removed)
        self.assertEqual(json.loads(removed["output"])["removed"], new_graph["id"])
        listing = json.loads(self.native("list_anim_graphs")["output"])
        self.assertNotIn(new_graph["id"], [g["id"] for g in listing["anim_graphs"]])
        gone = self.native("remove_anim_graph", anim_graph_id=new_graph["id"])
        self.assertEqual(gone["status"], "error", gone)
        self.assertEqual(gone.get("code"), "not_found", gone)
        self.assertIn("not found", gone["error"])

    def test_get_anim_graph_errors(self):
        unknown = self.native("get_anim_graph", anim_graph_id=4000000000)
        self.assertEqual(unknown["status"], "error", unknown)
        self.assertEqual(unknown.get("code"), "not_found", unknown)
        self.assertIn("not found", unknown["error"])
        missing = self.native("get_anim_graph")
        self.assertEqual(missing["status"], "error", missing)
        self.assertEqual(missing.get("code"), "validation_failed", missing)
        self.assertIn("anim_graph_id or file_name", missing["error"])
        bad = self.native("get_anim_graph", anim_graph_id="not-an-id")
        self.assertEqual(bad["status"], "error", bad)
        self.assertEqual(bad.get("code"), "validation_failed", bad)

    def test_remove_anim_graph_errors(self):
        missing = self.native("remove_anim_graph")
        self.assertEqual(missing["status"], "error", missing)
        self.assertEqual(missing.get("code"), "validation_failed", missing)
        self.assertIn("anim_graph_id", missing["error"])
        unknown = self.native("remove_anim_graph", anim_graph_id=4000000000)
        self.assertEqual(unknown["status"], "error", unknown)
        self.assertEqual(unknown.get("code"), "not_found", unknown)

    # Authoring

    AUTHORED_RELATIVE = ANIM_GRAPH_PROJECT_SUBDIR / "Authored.animgraph"

    def _create_editable_graph(self) -> int:
        created = self.native("create_anim_graph")
        if created.get("status") == "error" and created.get("code") == "unavailable":
            self.skipTest(f"EMotion Studio (the Animation Editor's command system) is not loaded: {created['error']}")
        self.assertEqual(created["status"], "ok", created)
        return int(json.loads(created["output"])["id"])

    def _describe(self, graph_id: int) -> tuple[dict, dict]:
        """The described graph and its nodes keyed by name."""
        response = self.native("get_anim_graph", anim_graph_id=graph_id)
        self.assertEqual(response["status"], "ok", response)
        described = json.loads(response["output"])
        return described, {n["name"]: n for n in described["nodes"]}

    def _expect_error(self, response: dict, code: str, fragment: str | None = None) -> None:
        self.assertEqual(response["status"], "error", response)
        self.assertEqual(response.get("code"), code, response)
        if fragment:
            self.assertIn(fragment, response["error"], response)

    def test_authoring_round_trip(self):
        # Build a graph node by node, save it into the project, load it back
        # and remove everything, so the editor and the project are left as
        # found. Every write is one step in the Animation Editor's undo
        # history (not the editor's main Undo), which this does not exercise.
        project = Path(os.environ["AICOMPANION_PROJECT"])
        target = project / self.AUTHORED_RELATIVE
        owned = [self._create_editable_graph()]
        graph_id = owned[0]
        try:
            idle = self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type="AnimGraphMotionNode", name="Idle", position=[0, 0])
            self.assertEqual(idle["status"], "ok", idle)
            idle = json.loads(idle["output"])
            walk = self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type="Motion", name="Walk", position=[240, 0])
            self.assertEqual(walk["status"], "ok", walk)
            walk = json.loads(walk["output"])

            described, nodes = self._describe(graph_id)
            root = nodes["Root"]
            self.assertEqual(root["id"], described["root_state_machine_id"])
            self.assertEqual(set(nodes), {"Root", "Idle", "Walk"})
            for node in (idle, walk):
                self.assertEqual(node["type"], "AnimGraphMotionNode")
                self.assertEqual(node["parent_id"], root["id"])
                self.assertTrue(re.fullmatch(r"\d+", node["id"]), node)
                # The write reply is the same object get_anim_graph emits.
                self.assertEqual(nodes[node["name"]], node)
            self.assertEqual(idle["position"], [0, 0])
            self.assertEqual(walk["position"], [240, 0])
            # The first state added to a state machine becomes its entry state.
            self.assertEqual(root["entry_state_id"], idle["id"])
            self.assertIsNone(idle["entry_state_id"])

            entry = self.native("set_anim_graph_entry_state", anim_graph_id=graph_id, node_id=walk["id"])
            self.assertEqual(entry["status"], "ok", entry)
            self.assertEqual(json.loads(entry["output"])["entry_state_id"], walk["id"])
            _, nodes = self._describe(graph_id)
            self.assertEqual(nodes["Root"]["entry_state_id"], walk["id"])

            speed = self.native(
                "add_anim_graph_parameter",
                anim_graph_id=graph_id,
                name="Speed",
                parameter_type="FloatSlider",
                default=0.2,
                min=0,
                max=1,
                description="Walk speed",
            )
            self.assertEqual(speed["status"], "ok", speed)
            speed = json.loads(speed["output"])
            self.assertEqual(speed["name"], "Speed")
            self.assertEqual(speed["description"], "Walk speed")
            self.assertIsNone(speed["group"])
            self.assertAlmostEqual(float(speed["default"]), 0.2, places=5)
            self.assertAlmostEqual(float(speed["min"]), 0.0, places=5)
            self.assertAlmostEqual(float(speed["max"]), 1.0, places=5)
            described, _ = self._describe(graph_id)
            self.assertEqual(described["parameters"], [speed])

            # A group that does not exist yet is created in the same step.
            crouch = self.native(
                "add_anim_graph_parameter", anim_graph_id=graph_id, name="Crouch", parameter_type="Bool", default=True, group="Locomotion"
            )
            self.assertEqual(crouch["status"], "ok", crouch)
            crouch = json.loads(crouch["output"])
            self.assertEqual(crouch["group"], "Locomotion")
            self.assertIsNone(crouch["min"])
            described, _ = self._describe(graph_id)
            self.assertEqual([p["name"] for p in described["parameters"]], ["Speed", "Crouch"])

            removed = self.native("remove_anim_graph_parameter", anim_graph_id=graph_id, name="Speed")
            self.assertEqual(removed["status"], "ok", removed)
            self.assertEqual(json.loads(removed["output"])["removed"], "Speed")
            described, _ = self._describe(graph_id)
            self.assertEqual([p["name"] for p in described["parameters"]], ["Crouch"])

            removed = self.native("remove_anim_graph_node", anim_graph_id=graph_id, node_id=idle["id"])
            self.assertEqual(removed["status"], "ok", removed)
            self.assertEqual(json.loads(removed["output"])["removed"], idle["id"])
            _, nodes = self._describe(graph_id)
            self.assertEqual(set(nodes), {"Root", "Walk"})

            saved = self.native("save_anim_graph", anim_graph_id=graph_id, file_name=str(self.AUTHORED_RELATIVE))
            self.assertEqual(saved["status"], "ok", saved)
            saved = json.loads(saved["output"])
            self.assertEqual(saved["id"], graph_id)
            self.assertTrue(target.is_file(), saved)
            self.assertIn("AnimGraphMotionNode", target.read_text(encoding="utf-8"))
            self.assertEqual(PurePosixPath(saved["file_name"].replace("\\", "/")).name, target.name, saved)
            listing = json.loads(self.native("list_anim_graphs")["output"])
            entry = next(g for g in listing["anim_graphs"] if g["id"] == graph_id)
            self.assertFalse(entry["dirty"], entry)

            # The same path answers the graph already loaded under that file
            # name: CommandLoadAnimGraph reuses a command-loaded graph instead
            # of loading twice (AnimGraphCommands.cpp:79-93).
            reloaded = self.native("load_anim_graph", file_name=str(target))
            self.assertEqual(reloaded["status"], "ok", reloaded)
            self.assertEqual(json.loads(reloaded["output"])["id"], graph_id)

            # Once removed, the file loads as a new graph with the saved
            # content; a project-relative path resolves the same file.
            removed_graph = self.native("remove_anim_graph", anim_graph_id=graph_id)
            self.assertEqual(removed_graph["status"], "ok", removed_graph)
            owned.remove(graph_id)
            fresh = self.native("load_anim_graph", file_name=str(self.AUTHORED_RELATIVE))
            self.assertEqual(fresh["status"], "ok", fresh)
            fresh = json.loads(fresh["output"])
            owned.append(int(fresh["id"]))
            described, nodes = self._describe(fresh["id"])
            self.assertEqual(set(nodes), {"Root", "Walk"})
            self.assertEqual(nodes["Root"]["entry_state_id"], nodes["Walk"]["id"])
            self.assertEqual([p["name"] for p in described["parameters"]], ["Crouch"])
            self.assertEqual(described["parameters"][0]["group"], "Locomotion")
        finally:
            for gid in owned:
                self.client.request("remove_anim_graph", anim_graph_id=gid)
            if target.exists():
                target.unlink()

    def test_authoring_refusals(self):
        graph_id = self._create_editable_graph()
        try:
            self._expect_error(
                self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type="NoSuchNode"), "validation_failed", "known:"
            )
            self._expect_error(
                self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type="Motion", parent_id="123456789"), "not_found"
            )
            self._expect_error(
                self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type="Motion", name='Say "hi"'), "validation_failed"
            )
            # A final node only belongs in a blend tree; the root is a state machine.
            self._expect_error(self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type="BlendTreeFinalNode"), "validation_failed")
            self._expect_error(self.native("add_anim_graph_node", anim_graph_id=graph_id), "validation_failed", "node_type")
            self._expect_error(
                self.native("add_anim_graph_parameter", anim_graph_id=graph_id, name="Flag", parameter_type="Bool", min=0),
                "validation_failed",
                "min",
            )
            self._expect_error(
                self.native("add_anim_graph_parameter", anim_graph_id=graph_id, name="Flag", parameter_type="Quaternion"),
                "validation_failed",
                "known:",
            )
            self._expect_error(self.native("remove_anim_graph_parameter", anim_graph_id=graph_id, name="Nope"), "not_found")
            self._expect_error(self.native("save_anim_graph", anim_graph_id=graph_id, file_name="../outside.animgraph"), "validation_failed")
            self._expect_error(self.native("save_anim_graph", anim_graph_id=graph_id), "validation_failed", "file_name")
            self._expect_error(
                self.native("load_anim_graph", file_name=str(ANIM_GRAPH_PROJECT_SUBDIR / "DoesNotExist.animgraph")), "not_found"
            )
            described, nodes = self._describe(graph_id)
            self._expect_error(
                self.native("remove_anim_graph_node", anim_graph_id=graph_id, node_id=described["root_state_machine_id"]),
                "validation_failed",
                "root",
            )
            self._expect_error(self.native("remove_anim_graph_node", anim_graph_id=graph_id, node_id="not-an-id"), "validation_failed")
            # The fixture graph belongs to its asset; writes to it are refused.
            self._expect_error(
                self.native("add_anim_graph_node", anim_graph_id=self.graph_id, node_type="Motion"), "validation_failed", "owned by"
            )
            self._expect_error(self.native("remove_anim_graph", anim_graph_id=self.graph_id), "validation_failed", "owned by")
            # Nothing above touched the graph.
            self.assertEqual(set(nodes), {"Root"})
            self.assertEqual(described["parameters"], [])
        finally:
            removed = self.native("remove_anim_graph", anim_graph_id=graph_id)
        self.assertEqual(removed["status"], "ok", removed)

    def _add_node(self, graph_id: int, node_type: str, name: str, **fields) -> dict:
        response = self.native("add_anim_graph_node", anim_graph_id=graph_id, node_type=node_type, name=name, **fields)
        self.assertEqual(response["status"], "ok", response)
        return json.loads(response["output"])

    def test_transitions_connections_and_node_edits(self):
        # The second authoring batch: state transitions with conditions, blend
        # tree port connections and node adjustments, each verified through
        # get_anim_graph and torn down with the graph.
        graph_id = self._create_editable_graph()
        try:
            idle = self._add_node(graph_id, "Motion", "Idle")
            walk = self._add_node(graph_id, "Motion", "Walk", position=[240, 0])
            speed = self.native("add_anim_graph_parameter", anim_graph_id=graph_id, name="Speed", parameter_type="FloatSlider")
            self.assertEqual(speed["status"], "ok", speed)

            # A transition with a blend time and a parameter condition, built
            # as one command group (create, adjust, add condition).
            added = self.native(
                "add_anim_graph_transition",
                anim_graph_id=graph_id,
                source_node_id=idle["id"],
                target_node_id=walk["id"],
                blend_time=0.25,
                conditions=[
                    {"condition_type": "ParameterCondition", "attributes": {"parameterName": "Speed", "function": "GREATER", "testValue": 0.5}}
                ],
            )
            self.assertEqual(added["status"], "ok", added)
            transition = json.loads(added["output"])
            self.assertTrue(re.fullmatch(r"\d+", transition["id"]), transition)
            self.assertEqual(transition["source_node_id"], idle["id"])
            self.assertEqual(transition["target_node_id"], walk["id"])
            self.assertFalse(transition["wildcard"])
            self.assertAlmostEqual(transition["blend_time"], 0.25, places=5)
            self.assertEqual(transition["priority"], 0)
            self.assertFalse(transition["disabled"])
            self.assertEqual(len(transition["conditions"]), 1, transition)
            self.assertEqual(transition["conditions"][0]["type"], "AnimGraphParameterCondition")
            # The engine's summary: "...: Parameter Name='Speed', Test Function='param > testValue', Test Value=0.50, ..."
            self.assertIn("Speed", transition["conditions"][0]["summary"])
            self.assertIn("param > testValue", transition["conditions"][0]["summary"])
            self.assertIn("0.50", transition["conditions"][0]["summary"])
            described, nodes = self._describe(graph_id)
            self.assertEqual(transition["state_machine_id"], nodes["Root"]["id"])
            # The write reply is the same object get_anim_graph emits.
            self.assertEqual(described["transitions"], [transition])

            # A wildcard transition names no source.
            added = self.native("add_anim_graph_transition", anim_graph_id=graph_id, source_node_id=None, target_node_id=idle["id"])
            self.assertEqual(added["status"], "ok", added)
            wildcard = json.loads(added["output"])
            self.assertTrue(wildcard["wildcard"])
            self.assertIsNone(wildcard["source_node_id"])
            self.assertEqual(wildcard["target_node_id"], idle["id"])
            self.assertEqual(wildcard["conditions"], [])
            described, _ = self._describe(graph_id)
            self.assertEqual({t["id"] for t in described["transitions"]}, {transition["id"], wildcard["id"]})

            adjusted = self.native(
                "set_anim_graph_transition", anim_graph_id=graph_id, transition_id=transition["id"], disabled=True, priority=3, blend_time=0.1
            )
            self.assertEqual(adjusted["status"], "ok", adjusted)
            adjusted = json.loads(adjusted["output"])
            self.assertEqual(adjusted["id"], transition["id"])
            self.assertTrue(adjusted["disabled"])
            self.assertEqual(adjusted["priority"], 3)
            self.assertAlmostEqual(adjusted["blend_time"], 0.1, places=5)
            self.assertEqual(len(adjusted["conditions"]), 1)  # adjusting keeps the conditions
            described, _ = self._describe(graph_id)
            self.assertIn(adjusted, described["transitions"])

            for transition_id in (transition["id"], wildcard["id"]):
                removed = self.native("remove_anim_graph_transition", anim_graph_id=graph_id, transition_id=transition_id)
                self.assertEqual(removed["status"], "ok", removed)
                self.assertEqual(json.loads(removed["output"])["removed"], transition_id)
            described, _ = self._describe(graph_id)
            self.assertEqual(described["transitions"], [])

            # Blend tree wiring: a Blend Two node's output pose into the final node.
            tree = self._add_node(graph_id, "BlendTree", "Tree")
            final = self._add_node(graph_id, "BlendTreeFinalNode", "Final", parent_id=tree["id"])
            blend = self._add_node(graph_id, "Blend Two", "Blend", parent_id=tree["id"])
            connected = self.native(
                "connect_anim_graph_ports",
                anim_graph_id=graph_id,
                source_node_id=blend["id"],
                source_port="Output Pose",
                target_node_id=final["id"],
                target_port="Input Pose",
            )
            self.assertEqual(connected["status"], "ok", connected)
            port = json.loads(connected["output"])
            self.assertEqual(port["index"], 0)
            self.assertEqual(port["name"], "Input Pose")
            self.assertEqual(port["connection"], {"source_node_id": blend["id"], "source_port": 0})
            _, nodes = self._describe(graph_id)
            self.assertEqual(nodes["Final"]["input_ports"], [port])
            # The port's connection is the one entry an input port holds.
            self._expect_error(
                self.native(
                    "connect_anim_graph_ports",
                    anim_graph_id=graph_id,
                    source_node_id=blend["id"],
                    source_port=0,
                    target_node_id=final["id"],
                    target_port=0,
                ),
                "validation_failed",
                "already has a connection",
            )
            disconnected = self.native("disconnect_anim_graph_ports", anim_graph_id=graph_id, target_node_id=final["id"], target_port=0)
            self.assertEqual(disconnected["status"], "ok", disconnected)
            self.assertTrue(re.fullmatch(r"\d+", json.loads(disconnected["output"])["removed"]), disconnected)
            _, nodes = self._describe(graph_id)
            self.assertIsNone(nodes["Final"]["input_ports"][0]["connection"])

            # Node adjustments: rename and move, then the motion node's motion
            # ids (the engine stores them with random-selection weights), then
            # disable.
            renamed = self.native("set_anim_graph_node", anim_graph_id=graph_id, node_id=idle["id"], name="Stand", position=[10, 20])
            self.assertEqual(renamed["status"], "ok", renamed)
            renamed = json.loads(renamed["output"])
            self.assertEqual(renamed["id"], idle["id"])
            self.assertEqual(renamed["name"], "Stand")
            self.assertEqual(renamed["position"], [10, 20])
            self.assertEqual(renamed["motion_ids"], [])
            motions = self.native(
                "set_anim_graph_node", anim_graph_id=graph_id, node_id=idle["id"], attributes={"motionIds": ["jack_idle_zup"], "loop": False}
            )
            self.assertEqual(motions["status"], "ok", motions)
            motions = json.loads(motions["output"])
            self.assertEqual(motions["motion_ids"], ["jack_idle_zup"])
            _, nodes = self._describe(graph_id)
            self.assertEqual(nodes["Stand"], motions)
            self.assertEqual(nodes["Stand"]["motion_ids"], ["jack_idle_zup"])
            self.assertIsNone(nodes["Tree"]["motion_ids"])
            disabled = self.native("set_anim_graph_node", anim_graph_id=graph_id, node_id=walk["id"], enabled=False)
            self.assertEqual(disabled["status"], "ok", disabled)
            self.assertFalse(json.loads(disabled["output"])["enabled"])
            _, nodes = self._describe(graph_id)
            self.assertFalse(nodes["Walk"]["enabled"])
        finally:
            removed = self.native("remove_anim_graph", anim_graph_id=graph_id)
        self.assertEqual(removed["status"], "ok", removed)

    def test_transition_and_connection_refusals(self):
        graph_id = self._create_editable_graph()
        try:
            idle = self._add_node(graph_id, "Motion", "Idle")
            walk = self._add_node(graph_id, "Motion", "Walk")
            sub = self._add_node(graph_id, "AnimGraphStateMachine", "Sub")
            inner = self._add_node(graph_id, "Motion", "Inner", parent_id=sub["id"])
            tree = self._add_node(graph_id, "BlendTree", "Tree")
            final = self._add_node(graph_id, "BlendTreeFinalNode", "Final", parent_id=tree["id"])
            blend = self._add_node(graph_id, "Blend Two", "Blend", parent_id=tree["id"])
            const = self._add_node(graph_id, "Float Constant", "Const", parent_id=tree["id"])
            # The states of different state machines cannot be joined.
            self._expect_error(
                self.native("add_anim_graph_transition", anim_graph_id=graph_id, source_node_id=idle["id"], target_node_id=inner["id"]),
                "validation_failed",
                "same state machine",
            )
            # A blend tree node is not a state.
            self._expect_error(
                self.native("add_anim_graph_transition", anim_graph_id=graph_id, source_node_id=blend["id"], target_node_id=final["id"]),
                "validation_failed",
                "not a state",
            )
            self._expect_error(
                self.native(
                    "add_anim_graph_transition",
                    anim_graph_id=graph_id,
                    source_node_id=idle["id"],
                    target_node_id=walk["id"],
                    conditions=[{"condition_type": "NoSuchCondition"}],
                ),
                "validation_failed",
                "known:",
            )
            self._expect_error(
                self.native(
                    "add_anim_graph_transition",
                    anim_graph_id=graph_id,
                    source_node_id=idle["id"],
                    target_node_id=walk["id"],
                    conditions=[{"condition_type": "TimeCondition", "attributes": {"parameterName": "Speed"}}],
                ),
                "validation_failed",
                "supported:",
            )
            self._expect_error(
                self.native(
                    "add_anim_graph_transition",
                    anim_graph_id=graph_id,
                    source_node_id=idle["id"],
                    target_node_id=walk["id"],
                    conditions=[{"condition_type": "ParameterCondition", "attributes": {"parameterName": "Nope"}}],
                ),
                "validation_failed",
                "no value parameter",
            )
            self._expect_error(
                self.native("add_anim_graph_transition", anim_graph_id=graph_id, source_node_id=idle["id"], target_node_id=walk["id"], blend_time=-1),
                "validation_failed",
                "blend_time",
            )
            self._expect_error(self.native("remove_anim_graph_transition", anim_graph_id=graph_id, transition_id="123456789"), "not_found")
            self._expect_error(
                self.native("set_anim_graph_transition", anim_graph_id=graph_id, transition_id="123456789", disabled=True), "not_found"
            )
            # Ports: a state has none to connect, and names are checked.
            self._expect_error(
                self.native(
                    "connect_anim_graph_ports",
                    anim_graph_id=graph_id,
                    source_node_id=idle["id"],
                    source_port=0,
                    target_node_id=walk["id"],
                    target_port=0,
                ),
                "validation_failed",
                "add_anim_graph_transition",
            )
            self._expect_error(
                self.native(
                    "connect_anim_graph_ports",
                    anim_graph_id=graph_id,
                    source_node_id=blend["id"],
                    source_port="Output Pose",
                    target_node_id=final["id"],
                    target_port="Pose In",
                ),
                "validation_failed",
                '"Input Pose" (0)',
            )
            self._expect_error(
                self.native(
                    "connect_anim_graph_ports",
                    anim_graph_id=graph_id,
                    source_node_id=blend["id"],
                    source_port="Output Pose",
                    target_node_id=blend["id"],
                    target_port="Pose 1",
                ),
                "validation_failed",
                "same node",
            )
            # A float output into a pose input carries the wrong data.
            self._expect_error(
                self.native(
                    "connect_anim_graph_ports",
                    anim_graph_id=graph_id,
                    source_node_id=const["id"],
                    source_port=0,
                    target_node_id=final["id"],
                    target_port="Input Pose",
                ),
                "validation_failed",
                "incompatible",
            )
            self._expect_error(
                self.native("disconnect_anim_graph_ports", anim_graph_id=graph_id, target_node_id=final["id"], target_port="Input Pose"),
                "not_found",
                "no connection",
            )
            # Node edits: unknown fields are listed, names stay unique.
            self._expect_error(
                self.native("set_anim_graph_node", anim_graph_id=graph_id, node_id=idle["id"], attributes={"motionId": ["x"]}),
                "validation_failed",
                "motionIds",
            )
            self._expect_error(
                self.native("set_anim_graph_node", anim_graph_id=graph_id, node_id=idle["id"], name="Walk"), "validation_failed", "already exists"
            )
            self._expect_error(self.native("set_anim_graph_node", anim_graph_id=graph_id, node_id=idle["id"]), "validation_failed", "at least one")
            # Nothing above touched the graph.
            described, nodes = self._describe(graph_id)
            self.assertEqual(described["transitions"], [])
            self.assertEqual(set(nodes), {"Root", "Idle", "Walk", "Sub", "Inner", "Tree", "Final", "Blend", "Const"})
            self.assertIsNone(nodes["Final"]["input_ports"][0]["connection"])
        finally:
            removed = self.native("remove_anim_graph", anim_graph_id=graph_id)
        self.assertEqual(removed["status"], "ok", removed)


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
        self.assertEqual(response.get("code"), "secure_mode", response)
        self.assertIn("secure mode", str(response.get("error", "")).lower())

    def test_read_only_request_types_answer(self):
        requests = [
            ("ping", {}),
            ("get_api_version", {}),
            ("get_scene_snapshot", {}),
            ("get_entity_tree", {}),
            ("validate_scene", {}),
            ("get_bus_schema", {"bus_name": "AiCompanionRequestBus"}),
            ("list_anim_graphs", {}),
        ]
        for request_type, fields in requests:
            with self.subTest(request_type=request_type):
                response = self.client.request(request_type, **fields)
                if request_type == "list_anim_graphs" and response.get("code") == "unknown_request_type":
                    self.skipTest("list_anim_graphs is not served by this gem build")
                self.assertEqual(response["status"], "ok", response)

    def test_list_anim_graphs_answers_in_secure_mode(self):
        response = self.client.request("list_anim_graphs")
        if response.get("code") == "unknown_request_type":
            self.skipTest("list_anim_graphs is not served by this gem build")
        self.assertEqual(response["status"], "ok", response)
        listing = json.loads(response["output"])
        self.assertIn("editor_mode", listing)
        self.assertIsInstance(listing["anim_graphs"], list)

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

    def test_anim_graph_authoring_works_without_python(self):
        # The authoring types are validated C++ paths, so they stay available
        # in secure mode; this builds and removes a graph with no execute_python.
        created = self.client.request("create_anim_graph")
        if created.get("status") == "error" and created.get("code") in ("unknown_request_type", "unavailable"):
            self.skipTest(f"anim graph authoring is not available here: {created.get('error')}")
        self.assertEqual(created["status"], "ok", created)
        graph_id = json.loads(created["output"])["id"]
        try:
            node = self.client.request("add_anim_graph_node", anim_graph_id=graph_id, node_type="Motion", name="Idle")
            self.assertEqual(node["status"], "ok", node)
            self.assertEqual(json.loads(node["output"])["name"], "Idle")
            parameter = self.client.request("add_anim_graph_parameter", anim_graph_id=graph_id, name="Speed", parameter_type="Float")
            self.assertEqual(parameter["status"], "ok", parameter)
            described = json.loads(self.client.request("get_anim_graph", anim_graph_id=graph_id)["output"])
            self.assertEqual({n["name"] for n in described["nodes"]}, {"Root", "Idle"})
            self.assertEqual([p["name"] for p in described["parameters"]], ["Speed"])
        finally:
            removed = self.client.request("remove_anim_graph", anim_graph_id=graph_id)
        self.assertEqual(removed["status"], "ok", removed)


if __name__ == "__main__":
    unittest.main()
