# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.builders.scene_builder"""

import sys
import os
import json
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'Editor', 'Scripts'))

from ai_companion.builders.scene_builder import SceneBuilder
from ai_companion.builders.entity_builder import EntityBuilder


class TestSceneBuilder(unittest.TestCase):

    def test_basic_scene(self):
        """SceneBuilder should create a valid scene."""
        result = json.loads(
            SceneBuilder()
            .with_ground(50)
            .build()
        )
        self.assertEqual(result["status"], "ok")
        self.assertGreater(result["data"]["total_created"], 0)

    def test_scene_with_lighting(self):
        result = json.loads(
            SceneBuilder()
            .with_ground(30)
            .with_lighting("three_point")
            .build()
        )
        self.assertGreater(result["data"]["total_created"], 1)

    def test_scene_with_camera(self):
        result = json.loads(
            SceneBuilder()
            .with_camera("top_down")
            .build()
        )
        self.assertGreater(result["data"]["total_created"], 0)

    def test_scene_with_custom_entity(self):
        builder = EntityBuilder("CustomBox").at_position(5, 5, 0)
        result = json.loads(
            SceneBuilder()
            .with_ground(20)
            .add_entity(builder)
            .build()
        )
        self.assertGreater(result["data"]["total_created"], 1)

    def test_full_scene(self):
        result = json.loads(
            SceneBuilder()
            .with_ground(50)
            .with_lighting("outdoor")
            .with_camera("perspective")
            .build()
        )
        self.assertEqual(result["status"], "ok")
        self.assertGreaterEqual(result["data"]["total_created"], 3)


if __name__ == "__main__":
    unittest.main()
