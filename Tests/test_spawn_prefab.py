# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.api.spawn_prefab.

``PrefabPublicRequestBus.InstantiatePrefab`` crashes the editor when the
template cannot be loaded (observed on O3DE 26.10.0). A C++ segfault is not a
Python exception, so the property that matters is that the bus is never
reached for a prefab that does not exist. These tests run ``spawn_prefab``
against stub ``azlmbr`` modules and record every bus call.
"""

import json
import os
import sys
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Editor", "Scripts"))

from ai_companion import api  # noqa: E402

GEM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


class _Outcome:
    def __init__(self, value, ok=True):
        self._value = value
        self._ok = ok

    def IsSuccess(self):  # noqa: N802
        return self._ok

    def GetValue(self):  # noqa: N802
        return self._value

    def GetError(self):  # noqa: N802
        return "stub error"


def _stub_azlmbr(calls, outcome):
    """Build stub azlmbr modules that record prefab bus calls."""

    def prefab_bus(call_type, event, *args):
        calls.append((event, args))
        if event == "InstantiatePrefab":
            return outcome
        raise AssertionError(f"unexpected event {event!r}")

    bus = types.ModuleType("azlmbr.bus")
    bus.Broadcast = object()
    entity = types.ModuleType("azlmbr.entity")
    entity.EntityId = lambda *a: ("EntityId", a)
    math = types.ModuleType("azlmbr.math")
    math.Vector3 = lambda x, y, z: ("Vector3", x, y, z)
    paths = types.ModuleType("azlmbr.paths")
    paths.projectroot = "/nonexistent/project"
    paths.engroot = "/nonexistent/engine"
    prefab = types.ModuleType("azlmbr.prefab")
    prefab.PrefabPublicRequestBus = prefab_bus
    root = types.ModuleType("azlmbr")
    root.__path__ = []  # mark as a package so submodule imports consult sys.modules
    for name, mod in (("bus", bus), ("entity", entity), ("math", math), ("paths", paths), ("prefab", prefab)):
        setattr(root, name, mod)
    return {
        "azlmbr": root,
        "azlmbr.bus": bus,
        "azlmbr.entity": entity,
        "azlmbr.math": math,
        "azlmbr.paths": paths,
        "azlmbr.prefab": prefab,
    }


class TestFindPrefabFile(unittest.TestCase):
    def test_gem_prefab_is_found_under_gem_assets(self):
        located = api.find_prefab_file("Player_TwinStick")
        self.assertEqual(located["relative_path"], "Prefabs/Player_TwinStick.prefab")
        self.assertEqual(
            located["found"],
            os.path.join(GEM_ROOT, "Assets", "Prefabs", "Player_TwinStick.prefab"),
        )

    def test_unknown_prefab_is_not_found(self):
        located = api.find_prefab_file("Definitely_Not_A_Prefab")
        self.assertIsNone(located["found"])
        self.assertTrue(located["searched"])

    def test_project_root_is_searched_inside_the_editor(self):
        calls = []
        modules = _stub_azlmbr(calls, _Outcome(1))
        with mock.patch.dict(sys.modules, modules):
            located = api.find_prefab_file("Nope")
        self.assertIn("/nonexistent/project", located["searched"])
        self.assertIn("/nonexistent/engine", located["searched"])

    def test_every_shipped_prefab_resolves(self):
        listed = json.loads(api.list_prefabs())["data"]
        for entry in listed:
            with self.subTest(prefab=entry["name"]):
                self.assertIsNotNone(api.find_prefab_file(entry["name"])["found"])


class TestSpawnPrefabGuard(unittest.TestCase):
    def test_missing_prefab_never_reaches_the_bus(self):
        calls = []
        modules = _stub_azlmbr(calls, _Outcome(1))
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(api.spawn_prefab("Definitely_Not_A_Prefab"))
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["details"]["code"], "prefab_not_found")
        self.assertEqual(calls, [])

    def test_path_like_names_are_rejected_before_lookup(self):
        for bad in ("", "../Levels/x", "Prefabs/Player_TwinStick", "a\\b"):
            with self.subTest(name=bad):
                calls = []
                modules = _stub_azlmbr(calls, _Outcome(1))
                with mock.patch.dict(sys.modules, modules):
                    result = json.loads(api.spawn_prefab(bad))
                self.assertEqual(result["status"], "error")
                self.assertEqual(result["details"]["code"], "invalid_prefab_name")
                self.assertEqual(calls, [])

    def test_existing_prefab_reaches_the_bus(self):
        calls = []
        modules = _stub_azlmbr(calls, _Outcome(4242))
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(api.spawn_prefab("Player_TwinStick", position=[1, 2, 3]))
        self.assertEqual(result["status"], "ok", result)
        self.assertTrue(result["data"]["spawned"])
        self.assertEqual(result["data"]["entity_id"], 4242)
        self.assertEqual(len(calls), 1)
        event, args = calls[0]
        self.assertEqual(event, "InstantiatePrefab")
        self.assertEqual(args[0], "Prefabs/Player_TwinStick.prefab")
        self.assertEqual(args[1], ("EntityId", ()))
        self.assertEqual(args[2], ("Vector3", 1.0, 2.0, 3.0))

    def test_failed_outcome_is_reported(self):
        calls = []
        modules = _stub_azlmbr(calls, _Outcome(None, ok=False))
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(api.spawn_prefab("Enemy_Chaser"))
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["details"]["code"], "instantiate_failed")

    def test_outside_the_editor_reports_not_spawned(self):
        result = json.loads(api.spawn_prefab("Enemy_Chaser"))
        self.assertEqual(result["status"], "ok")
        self.assertFalse(result["data"]["spawned"])


if __name__ == "__main__":
    unittest.main()
