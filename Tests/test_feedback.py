# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.utils and feedback modules."""

import sys
import os
import json
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'Editor', 'Scripts'))

from ai_companion.utils.json_output import success, error, entity_result, batch_result
from ai_companion.utils.component_registry import (
    resolve_component,
    list_components,
    get_categories,
)


class TestJsonOutput(unittest.TestCase):

    def test_success_basic(self):
        result = json.loads(success())
        self.assertEqual(result["status"], "ok")

    def test_success_with_data(self):
        result = json.loads(success({"key": "value"}))
        self.assertEqual(result["data"]["key"], "value")

    def test_success_with_message(self):
        result = json.loads(success(message="done"))
        self.assertEqual(result["message"], "done")

    def test_error_basic(self):
        result = json.loads(error("something broke"))
        self.assertEqual(result["status"], "error")
        self.assertEqual(result["message"], "something broke")

    def test_error_with_rollback(self):
        result = json.loads(error("failed", rolled_back=True))
        self.assertTrue(result["rolled_back"])

    def test_entity_result(self):
        result = json.loads(entity_result(
            entity_id=123,
            name="Test",
            component_ids={"Mesh": 1},
            position=[1, 2, 3],
        ))
        self.assertEqual(result["data"]["entity_id"], 123)
        self.assertEqual(result["data"]["name"], "Test")
        self.assertEqual(result["data"]["position"], [1, 2, 3])

    def test_batch_result(self):
        entities = [{"name": "A"}, {"name": "B"}]
        result = json.loads(batch_result(entities, 2))
        self.assertEqual(result["data"]["total_created"], 2)
        self.assertEqual(len(result["data"]["entities"]), 2)


class TestComponentRegistry(unittest.TestCase):

    def test_exact_match(self):
        canonical, err = resolve_component("Mesh")
        self.assertEqual(canonical, "Mesh")
        self.assertIsNone(err)

    def test_case_insensitive(self):
        canonical, err = resolve_component("mesh")
        self.assertEqual(canonical, "Mesh")

    def test_alias_match(self):
        canonical, err = resolve_component("RigidBody")
        self.assertEqual(canonical, "PhysX Rigid Body")

    def test_unknown_with_suggestion(self):
        canonical, err = resolve_component("Mesh Component")
        # Should get a suggestion since "mesh" is a substring match
        self.assertIsNone(canonical)
        self.assertIn("Did you mean", err)

    def test_completely_unknown(self):
        canonical, err = resolve_component("CompletelyFakeComponent12345")
        self.assertIsNone(canonical)
        self.assertIn("Unknown", err)

    def test_empty(self):
        canonical, err = resolve_component("")
        self.assertIsNone(canonical)

    def test_list_all_components(self):
        components = list_components()
        self.assertGreater(len(components), 0)
        self.assertIn("name", components[0])
        self.assertIn("category", components[0])

    def test_list_by_category(self):
        physics = list_components(category="Physics")
        self.assertGreater(len(physics), 0)
        for c in physics:
            self.assertEqual(c["category"], "Physics")

    def test_get_categories(self):
        categories = get_categories()
        self.assertIn("Physics", categories)
        self.assertIn("Rendering", categories)
        self.assertIn("Lighting", categories)


class TestSandbox(unittest.TestCase):

    def test_entity_limit(self):
        from ai_companion.safety.sandbox import OperationSandbox, SandboxLimitError

        sandbox = OperationSandbox(max_entities=5)
        sandbox.begin()
        sandbox.check_entity_limit(3)
        sandbox.check_entity_limit(2)

        with self.assertRaises(SandboxLimitError):
            sandbox.check_entity_limit(1)

    def test_depth_limit(self):
        from ai_companion.safety.sandbox import OperationSandbox, SandboxLimitError

        sandbox = OperationSandbox(max_depth=3)
        sandbox.begin()
        sandbox.check_depth()
        sandbox.check_depth()
        sandbox.check_depth()

        with self.assertRaises(SandboxLimitError):
            sandbox.check_depth()

    def test_timeout(self):
        from ai_companion.safety.sandbox import OperationSandbox, SandboxLimitError

        sandbox = OperationSandbox(timeout=0.0)
        sandbox.begin()

        with self.assertRaises(SandboxLimitError):
            sandbox.check_timeout()


if __name__ == "__main__":
    unittest.main()
