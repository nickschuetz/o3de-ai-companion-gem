# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.builders.entity_builder"""

import sys
import os
import json
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'Editor', 'Scripts'))

from ai_companion.builders.entity_builder import EntityBuilder


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
        self.assertIn("PhysX Rigid Body", result["data"]["component_ids"])

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
        self.assertIn("PhysX Collider", result["data"]["component_ids"])

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

    def test_uniform_scale(self):
        """Uniform scale should be accepted."""
        builder = EntityBuilder("Scaled").with_scale(2.0)
        self.assertEqual(builder._scale, 2.0)

    def test_non_uniform_scale(self):
        builder = EntityBuilder("Scaled").with_scale(1, 2, 3)
        self.assertEqual(builder._scale, [1.0, 2.0, 3.0])


if __name__ == "__main__":
    unittest.main()
