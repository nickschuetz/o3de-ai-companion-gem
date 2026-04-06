# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.safety.validators"""

import sys
import os
import unittest

# Add the Editor/Scripts directory to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'Editor', 'Scripts'))

from ai_companion.safety.validators import (
    validate_entity_name,
    validate_component_type,
    validate_position,
    validate_asset_path,
    validate_float,
    is_protected_entity,
)


class TestEntityNameValidation(unittest.TestCase):

    def test_valid_names(self):
        valid_names = ["Player", "Enemy1", "my_entity", "player-two", "A", "CamelCase"]
        for name in valid_names:
            ok, err = validate_entity_name(name)
            self.assertTrue(ok, f"Expected '{name}' to be valid, got: {err}")

    def test_empty_name(self):
        ok, err = validate_entity_name("")
        self.assertFalse(ok)
        self.assertIn("empty", err.lower())

    def test_starts_with_digit(self):
        ok, _ = validate_entity_name("1Entity")
        self.assertFalse(ok)

    def test_starts_with_underscore(self):
        ok, _ = validate_entity_name("_entity")
        self.assertFalse(ok)

    def test_special_characters(self):
        for name in ["entity!", "ent ity", "entity@home", "entity;drop", "a\nb"]:
            ok, _ = validate_entity_name(name)
            self.assertFalse(ok, f"Expected '{name}' to be invalid")

    def test_too_long(self):
        ok, _ = validate_entity_name("a" * 129)
        self.assertFalse(ok)

    def test_max_length(self):
        ok, _ = validate_entity_name("a" * 128)
        self.assertTrue(ok)

    def test_non_string_type(self):
        ok, err = validate_entity_name(123)
        self.assertFalse(ok)
        self.assertIn("string", err.lower())


class TestComponentTypeValidation(unittest.TestCase):

    def test_valid_types(self):
        valid = ["Mesh", "PhysX Collider", "Directional Light", "PhysX Rigid Body (Dynamic)"]
        for t in valid:
            ok, err = validate_component_type(t)
            self.assertTrue(ok, f"Expected '{t}' to be valid, got: {err}")

    def test_empty(self):
        ok, _ = validate_component_type("")
        self.assertFalse(ok)

    def test_injection(self):
        ok, _ = validate_component_type("Mesh; rm -rf /")
        self.assertFalse(ok)

    def test_newline_injection(self):
        ok, _ = validate_component_type("Mesh\nimport os")
        self.assertFalse(ok)


class TestPositionValidation(unittest.TestCase):

    def test_valid_positions(self):
        ok, _ = validate_position([0, 0, 0])
        self.assertTrue(ok)
        ok, _ = validate_position([100.5, -200.3, 50])
        self.assertTrue(ok)
        ok, _ = validate_position((1, 2, 3))
        self.assertTrue(ok)

    def test_wrong_length(self):
        ok, _ = validate_position([1, 2])
        self.assertFalse(ok)
        ok, _ = validate_position([1, 2, 3, 4])
        self.assertFalse(ok)

    def test_nan(self):
        ok, _ = validate_position([float('nan'), 0, 0])
        self.assertFalse(ok)

    def test_infinity(self):
        ok, _ = validate_position([float('inf'), 0, 0])
        self.assertFalse(ok)

    def test_out_of_bounds(self):
        ok, _ = validate_position([10001, 0, 0])
        self.assertFalse(ok)

    def test_non_numeric(self):
        ok, _ = validate_position(["a", 0, 0])
        self.assertFalse(ok)

    def test_not_list(self):
        ok, _ = validate_position("not a list")
        self.assertFalse(ok)


class TestAssetPathValidation(unittest.TestCase):

    def test_valid_paths(self):
        ok, _ = validate_asset_path("Scripts/Lua/player.lua")
        self.assertTrue(ok)
        ok, _ = validate_asset_path("Assets/Prefabs/Player.prefab")
        self.assertTrue(ok)

    def test_empty(self):
        ok, _ = validate_asset_path("")
        self.assertFalse(ok)

    def test_traversal(self):
        ok, _ = validate_asset_path("../../etc/passwd")
        self.assertFalse(ok)

    def test_absolute_unix(self):
        ok, _ = validate_asset_path("/etc/passwd")
        self.assertFalse(ok)

    def test_absolute_windows(self):
        ok, _ = validate_asset_path("C:\\Windows\\System32")
        self.assertFalse(ok)

    def test_null_byte(self):
        ok, _ = validate_asset_path("file\x00.lua")
        self.assertFalse(ok)

    def test_too_long(self):
        ok, _ = validate_asset_path("a" * 1025)
        self.assertFalse(ok)


class TestFloatValidation(unittest.TestCase):

    def test_valid(self):
        ok, _ = validate_float(5.0, 0, 10)
        self.assertTrue(ok)

    def test_nan(self):
        ok, _ = validate_float(float('nan'), 0, 10)
        self.assertFalse(ok)

    def test_out_of_range(self):
        ok, _ = validate_float(11, 0, 10)
        self.assertFalse(ok)

    def test_non_number(self):
        ok, _ = validate_float("5", 0, 10)
        self.assertFalse(ok)


class TestProtectedEntities(unittest.TestCase):

    def test_protected(self):
        self.assertTrue(is_protected_entity("EditorGlobal"))
        self.assertTrue(is_protected_entity("SystemEntity"))
        self.assertTrue(is_protected_entity("AZ::SystemEntity"))
        self.assertTrue(is_protected_entity("AZ::SomeOtherThing"))

    def test_not_protected(self):
        self.assertFalse(is_protected_entity("Player"))
        self.assertFalse(is_protected_entity("Enemy1"))


if __name__ == "__main__":
    unittest.main()
