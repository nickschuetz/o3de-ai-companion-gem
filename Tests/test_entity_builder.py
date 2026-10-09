# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.builders.entity_builder"""

import sys
import os
import json
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'Editor', 'Scripts'))

from ai_companion.builders.entity_builder import (
    EntityBuilder,
    COLLIDER_SHAPE_PATH,
    COLLIDER_SHAPE_TYPES,
    RIGID_BODY_COMPUTE_MASS_PATH,
    RIGID_BODY_MASS_PATH,
)
from ai_companion.builders.physics_builder import create_dynamic_body, create_static_body
from ai_companion.templates.enemy import create_enemy_entity
from ai_companion.templates.player import create_player_entity


class _Outcome:
    def __init__(self, value=None, ok=True, err="stub error"):
        self._value = value
        self._ok = ok
        self._err = err

    def IsSuccess(self):  # noqa: N802
        return self._ok

    def GetValue(self):  # noqa: N802
        return self._value

    def GetError(self):  # noqa: N802
        return self._err


def _stub_azlmbr(calls, on_set=None):
    """Stub azlmbr modules for the builder's editor path.

    Every EditorComponentAPIBus call is appended to ``calls`` as
    ``(event, args)``. ``AddComponentOfType`` answers with a pair named after
    the component type so SetComponentProperty calls can be matched back to
    their component. ``on_set(property_path, value)`` may return an outcome
    or raise, to simulate a set that fails; by default every set succeeds.
    """

    def component_bus(call_type, event, *args):
        calls.append((event, args))
        if event == "FindComponentTypeIdsByEntityType":
            return [f"tid:{name}" for name in args[0]]
        if event == "AddComponentOfType":
            return _Outcome([f"pair:{args[1][4:]}"])
        if event == "SetComponentProperty":
            if on_set is not None:
                return on_set(args[1], args[2])
            return _Outcome()
        raise AssertionError(f"unexpected EditorComponentAPIBus event {event!r}")

    def tools_bus(call_type, event, *args):
        if event == "CreateNewEntity":
            return 4242
        if event == "AddDirtyEntity":
            return None
        raise AssertionError(f"unexpected ToolsApplicationRequestBus event {event!r}")

    def quiet_bus(call_type, event, *args):
        return None

    # Entity names the editor would report, for with_parent's protected check.
    entity_names = {1: "EditorGlobal", 2: "AZ::Probe", 3: "Player"}

    class _StubEntityId:
        def __init__(self, value):
            self.value = value

        def ToString(self):
            return f"[{self.value}]"

        def IsValid(self):
            return True

    def info_bus(call_type, event, eid):
        if event == "GetName":
            return entity_names.get(getattr(eid, "value", None))
        raise AssertionError(f"unexpected EditorEntityInfoRequestBus event {event!r}")

    def search_bus(call_type, event, search_filter):
        return [_StubEntityId(number) for number in entity_names]

    class _EntityType:
        Game = "Game"

    bus = types.ModuleType("azlmbr.bus")
    bus.Broadcast = object()
    bus.Event = object()
    editor = types.ModuleType("azlmbr.editor")
    editor.EditorComponentAPIBus = component_bus
    editor.ToolsApplicationRequestBus = tools_bus
    editor.EditorEntityAPIBus = quiet_bus
    editor.EditorEntityInfoRequestBus = info_bus
    # No AiCompanionEditorRequestBus: commit_entity_to_prefab tolerates its absence.
    entity = types.ModuleType("azlmbr.entity")
    entity.EntityId = lambda *a: _StubEntityId(a[0] if a else 4294967295)
    entity.SearchBus = search_bus
    entity.SearchFilter = lambda: object()
    entity.EntityType = _EntityType
    components = types.ModuleType("azlmbr.components")
    components.TransformBus = quiet_bus
    math = types.ModuleType("azlmbr.math")
    math.Vector3 = lambda x, y, z: ("Vector3", x, y, z)
    math.Uuid = lambda: "null-uuid"
    asset = types.ModuleType("azlmbr.asset")
    asset.AssetCatalogRequestBus = quiet_bus  # GetAssetIdByPath -> None: no asset set
    root = types.ModuleType("azlmbr")
    root.__path__ = []
    modules = {
        "azlmbr": root, "azlmbr.bus": bus, "azlmbr.editor": editor,
        "azlmbr.entity": entity, "azlmbr.components": components,
        "azlmbr.math": math, "azlmbr.asset": asset,
    }
    for name, mod in modules.items():
        if "." in name:
            setattr(root, name.split(".", 1)[1], mod)
    return modules


def _property_sets(calls):
    """The (pair, property_path, value) triples of every SetComponentProperty call."""
    return [args for event, args in calls if event == "SetComponentProperty"]


class TestEntityBuilder(unittest.TestCase):

    def test_basic_creation(self):
        """EntityBuilder should create a valid entity result."""
        result = json.loads(EntityBuilder("TestEntity").build())
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["data"]["name"], "TestEntity")

    def test_with_position(self):
        result = json.loads(
            EntityBuilder("Positioned")
            .at_position(1, 2, 3)
            .build()
        )
        self.assertEqual(result["data"]["position"], [1.0, 2.0, 3.0])

    def test_with_mesh(self):
        result = json.loads(
            EntityBuilder("Meshed")
            .with_mesh("primitive_cube")
            .build()
        )
        self.assertIn("Mesh", result["data"]["component_ids"])

    def test_with_physics(self):
        result = json.loads(
            EntityBuilder("Physical")
            .with_physics(body_type="dynamic", mass=2.0)
            .build()
        )
        self.assertIn("PhysX Dynamic Rigid Body", result["data"]["component_ids"])

    def test_with_static_physics(self):
        result = json.loads(
            EntityBuilder("Static")
            .with_physics(body_type="static")
            .build()
        )
        self.assertIn("PhysX Static Rigid Body", result["data"]["component_ids"])

    def test_with_collider(self):
        result = json.loads(
            EntityBuilder("Collided")
            .with_collider(shape="sphere")
            .build()
        )
        self.assertIn("PhysX Primitive Collider", result["data"]["component_ids"])

    def test_with_lua_script(self):
        result = json.loads(
            EntityBuilder("Scripted")
            .with_lua_script("Scripts/Lua/test.lua")
            .build()
        )
        self.assertIn("Lua Script", result["data"]["component_ids"])

    def test_with_component(self):
        result = json.loads(
            EntityBuilder("Custom")
            .with_component("Camera")
            .build()
        )
        self.assertIn("Camera", result["data"]["component_ids"])

    def test_chained(self):
        """Full chain should work."""
        result = json.loads(
            EntityBuilder("FullEntity")
            .at_position(5, 10, 1)
            .with_mesh("primitive_capsule")
            .with_physics(body_type="dynamic")
            .with_collider(shape="capsule")
            .with_lua_script("Scripts/Lua/test.lua")
            .build()
        )
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["data"]["name"], "FullEntity")
        self.assertEqual(result["data"]["position"], [5.0, 10.0, 1.0])
        self.assertGreater(len(result["data"]["component_ids"]), 0)

    def test_invalid_name_raises(self):
        with self.assertRaises(ValueError):
            EntityBuilder("")

    def test_invalid_name_digit_start(self):
        with self.assertRaises(ValueError):
            EntityBuilder("1Bad")

    def test_invalid_position(self):
        with self.assertRaises(ValueError):
            EntityBuilder("Test").at_position(float('nan'), 0, 0)

    def test_invalid_script_path(self):
        with self.assertRaises(ValueError):
            EntityBuilder("Test").with_lua_script("../../etc/passwd")

    def test_invalid_component_type(self):
        with self.assertRaises(ValueError):
            EntityBuilder("Test").with_component("NonexistentComponent12345")

    def test_component_type_characters_are_validated_before_the_registry(self):
        # A name with shell or Python syntax in it must fail the character
        # pattern, not come back as a fuzzy "did you mean Mesh?" suggestion.
        for bad in ("Mesh; rm -rf /", "Mesh\nimport os", "", "9Mesh"):
            with self.subTest(component_type=bad):
                with self.assertRaises(ValueError) as ctx:
                    EntityBuilder("Test").with_component(bad)
                self.assertNotIn("Did you mean", str(ctx.exception))

    def test_component_type_is_resolved_against_the_registry(self):
        builder = EntityBuilder("Test").with_component("physx rigid body")
        self.assertEqual(builder._components[0]["type"], "PhysX Dynamic Rigid Body")
        with self.assertRaises(ValueError) as ctx:
            EntityBuilder("Test").with_component("Mesh Thing")
        self.assertIn("Did you mean", str(ctx.exception))

    def test_with_parent_refuses_a_protected_entity(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls)):
            for protected in (1, 2, "[2]"):
                with self.subTest(parent=protected):
                    with self.assertRaises(ValueError) as ctx:
                        EntityBuilder("Child").with_parent(protected)
                    self.assertIn("entity is protected", str(ctx.exception))

    def test_with_parent_accepts_an_ordinary_entity(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls)):
            result = json.loads(EntityBuilder("Child").with_parent(3).build())
        self.assertEqual(result["status"], "ok", result)

    def test_uniform_scale(self):
        """Uniform scale should be accepted."""
        builder = EntityBuilder("Scaled").with_scale(2.0)
        self.assertEqual(builder._scale, 2.0)

    def test_non_uniform_scale(self):
        builder = EntityBuilder("Scaled").with_scale(1, 2, 3)
        self.assertEqual(builder._scale, [1.0, 2.0, 3.0])

    def test_unknown_collider_shape_raises(self):
        with self.assertRaises(ValueError):
            EntityBuilder("Test").with_collider(shape="dodecahedron")

    def test_collider_shape_is_normalized(self):
        builder = EntityBuilder("Test").with_collider(shape=" Capsule ")
        self.assertEqual(builder._components[0]["properties"]["shape"], "capsule")

    def test_invalid_mass_raises(self):
        for bad in (-1.0, float("nan"), float("inf"), "heavy"):
            with self.subTest(mass=bad):
                with self.assertRaises(ValueError):
                    EntityBuilder("Test").with_physics(body_type="dynamic", mass=bad)

    def test_zero_mass_is_accepted(self):
        """The engine treats 0 kg as infinite mass; it is a valid request."""
        builder = EntityBuilder("Test").with_physics(body_type="dynamic", mass=0)
        self.assertEqual(builder._components[0]["properties"]["mass"], 0.0)


class TestPhysicsPropertiesInEditor(unittest.TestCase):
    """The builder's editor path must apply mass and collider shape.

    Before this, ``with_physics(mass=80)`` recorded the mass but the editor
    path only added the components, and the saved level carried the mass the
    editor computed from the colliders (1000 kg for a player built with 80).
    These tests run ``build()`` against stub azlmbr modules and assert on the
    EditorComponentAPIBus SetComponentProperty calls.
    """

    def _build(self, builder, on_set=None):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls, on_set)):
            result = json.loads(builder.build())
        return result, calls

    def test_mass_and_shape_are_set_through_the_component_api(self):
        result, calls = self._build(
            EntityBuilder("Player")
            .at_position(0, 0, 1)
            .with_physics(body_type="dynamic", mass=80)
            .with_collider(shape="capsule")
        )
        self.assertEqual(result["status"], "ok", result)
        self.assertEqual(_property_sets(calls), [
            ("pair:PhysX Dynamic Rigid Body", "Configuration|Compute Mass", False),
            ("pair:PhysX Dynamic Rigid Body", "Configuration|Mass", 80.0),
            ("pair:PhysX Primitive Collider", "Shape Configuration|Shape", 2),
        ])
        self.assertEqual(result["data"]["applied_properties"], {
            "PhysX Dynamic Rigid Body": {
                RIGID_BODY_COMPUTE_MASS_PATH: False,
                RIGID_BODY_MASS_PATH: 80.0,
            },
            "PhysX Primitive Collider": {COLLIDER_SHAPE_PATH: 2},
        })
        self.assertNotIn("property_warnings", result["data"])

    def test_compute_mass_is_switched_off_before_mass_is_set(self):
        """A custom mass only sticks once the editor stops computing it."""
        _, calls = self._build(EntityBuilder("Crate").with_physics(mass=5))
        paths = [path for _, path, _ in _property_sets(calls)]
        self.assertLess(paths.index(RIGID_BODY_COMPUTE_MASS_PATH), paths.index(RIGID_BODY_MASS_PATH))

    def test_every_shape_maps_to_its_engine_enum_value(self):
        # Physics::ShapeType order: Sphere, Box, Capsule, Cylinder.
        self.assertEqual(COLLIDER_SHAPE_TYPES, {"sphere": 0, "box": 1, "capsule": 2, "cylinder": 3})
        for shape, enum_value in COLLIDER_SHAPE_TYPES.items():
            with self.subTest(shape=shape):
                _, calls = self._build(EntityBuilder("Shape").with_collider(shape=shape))
                self.assertEqual(_property_sets(calls), [
                    ("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, enum_value),
                ])

    def test_static_body_sets_no_physics_properties(self):
        result, calls = self._build(EntityBuilder("Wall").with_physics(body_type="static"))
        self.assertEqual(result["status"], "ok")
        self.assertEqual(_property_sets(calls), [])
        self.assertNotIn("applied_properties", result["data"])

    def test_failed_set_is_reported_and_does_not_abort_the_build(self):
        def on_set(path, value):
            if path == RIGID_BODY_MASS_PATH:
                return _Outcome(ok=False, err="path provided was not found in tree")
            return _Outcome()

        result, calls = self._build(
            EntityBuilder("Player").with_physics(mass=80).with_collider(shape="box"),
            on_set,
        )
        self.assertEqual(result["status"], "ok", result)
        self.assertEqual(result["data"]["name"], "Player")
        # The failing set did not stop the collider shape from being applied.
        self.assertEqual(_property_sets(calls)[-1], ("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, 1))
        self.assertEqual(result["data"]["applied_properties"], {
            "PhysX Dynamic Rigid Body": {RIGID_BODY_COMPUTE_MASS_PATH: False},
            "PhysX Primitive Collider": {COLLIDER_SHAPE_PATH: 1},
        })
        warnings = result["data"]["property_warnings"]
        self.assertEqual(len(warnings), 1)
        self.assertIn(RIGID_BODY_MASS_PATH, warnings[0])
        self.assertIn("not found in tree", warnings[0])

    def test_raising_set_is_reported_and_does_not_abort_the_build(self):
        def on_set(path, value):
            if path == COLLIDER_SHAPE_PATH:
                raise RuntimeError("binding rejected the value")
            return _Outcome()

        result, _ = self._build(
            EntityBuilder("Player").with_physics(mass=80).with_collider(shape="sphere"),
            on_set,
        )
        self.assertEqual(result["status"], "ok", result)
        self.assertEqual(result["data"]["applied_properties"], {
            "PhysX Dynamic Rigid Body": {RIGID_BODY_COMPUTE_MASS_PATH: False, RIGID_BODY_MASS_PATH: 80.0},
        })
        self.assertEqual(len(result["data"]["property_warnings"]), 1)
        self.assertIn("RuntimeError: binding rejected the value", result["data"]["property_warnings"][0])

    def test_physics_component_that_failed_to_add_is_skipped(self):
        """No pair for the component means nothing to set, and no warning."""
        calls = []
        modules = _stub_azlmbr(calls)
        real_bus = modules["azlmbr.editor"].EditorComponentAPIBus

        def component_bus(call_type, event, *args):
            # The builder retries a failed AddComponentOfType with the plural
            # AddComponentsOfType; fail both for the rigid body.
            if event in ("AddComponentOfType", "AddComponentsOfType") and "Rigid Body" in str(args[1]):
                calls.append((event, args))
                return _Outcome(ok=False)
            return real_bus(call_type, event, *args)

        modules["azlmbr.editor"].EditorComponentAPIBus = component_bus
        with mock.patch.dict(sys.modules, modules):
            result = json.loads(EntityBuilder("Ghost").with_physics(mass=80).with_collider("box").build())
        self.assertEqual(result["status"], "ok", result)
        self.assertEqual(_property_sets(calls), [("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, 1)])
        self.assertNotIn("property_warnings", result["data"])


class TestPhysicsPropertiesThroughTemplates(unittest.TestCase):
    """The templates and physics helpers reach the same apply step."""

    def _sets(self, fn, *args, **kwargs):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls)):
            result = json.loads(fn(*args, **kwargs))
        self.assertEqual(result["status"], "ok", result)
        return _property_sets(calls)

    def test_create_dynamic_body_applies_its_mass_and_shape(self):
        self.assertEqual(self._sets(create_dynamic_body, "Crate", [0, 0, 0], mass=80, collider_shape="sphere"), [
            ("pair:PhysX Dynamic Rigid Body", RIGID_BODY_COMPUTE_MASS_PATH, False),
            ("pair:PhysX Dynamic Rigid Body", RIGID_BODY_MASS_PATH, 80.0),
            ("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, 0),
        ])

    def test_create_static_body_applies_only_the_shape(self):
        self.assertEqual(self._sets(create_static_body, "Wall", [0, 0, 0], collider_shape="box"), [
            ("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, 1),
        ])

    def test_player_template_applies_its_mass_and_capsule(self):
        self.assertEqual(self._sets(create_player_entity), [
            ("pair:PhysX Dynamic Rigid Body", RIGID_BODY_COMPUTE_MASS_PATH, False),
            ("pair:PhysX Dynamic Rigid Body", RIGID_BODY_MASS_PATH, 1.0),
            ("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, 2),
        ])

    def test_enemy_template_applies_its_mass_and_box(self):
        self.assertEqual(self._sets(create_enemy_entity, "Chaser"), [
            ("pair:PhysX Dynamic Rigid Body", RIGID_BODY_COMPUTE_MASS_PATH, False),
            ("pair:PhysX Dynamic Rigid Body", RIGID_BODY_MASS_PATH, 1.0),
            ("pair:PhysX Primitive Collider", COLLIDER_SHAPE_PATH, 1),
        ])


if __name__ == "__main__":
    unittest.main()
