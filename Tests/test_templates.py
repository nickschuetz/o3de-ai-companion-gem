# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.templates"""

import sys
import os
import json
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'Editor', 'Scripts'))

from ai_companion.templates.player import create_player_entity
from ai_companion.templates.enemy import create_enemy_entity
from ai_companion.templates.camera import create_camera_entity
from ai_companion.templates.pickup import create_pickup_entity
from ai_companion.templates.projectile import create_projectile_spawner_entity
from ai_companion.templates.environment import create_obstacle, create_trigger_zone


class TestPlayerTemplate(unittest.TestCase):

    def test_default_player(self):
        result = json.loads(create_player_entity())
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["data"]["name"], "Player")

    def test_player_with_movement(self):
        result = json.loads(create_player_entity(movement="twin_stick"))
        self.assertIn("Lua Script", result["data"]["component_ids"])

    def test_player_custom_position(self):
        result = json.loads(create_player_entity(position=[5, 10, 2]))
        self.assertEqual(result["data"]["position"], [5.0, 10.0, 2.0])

    def test_player_invalid_movement(self):
        with self.assertRaises(ValueError):
            create_player_entity(movement="nonexistent")


class TestEnemyTemplate(unittest.TestCase):

    def test_chaser(self):
        result = json.loads(create_enemy_entity("Chaser1", ai_type="chaser"))
        self.assertEqual(result["status"], "ok")
        self.assertIn("Lua Script", result["data"]["component_ids"])

    def test_turret(self):
        result = json.loads(create_enemy_entity("Turret1", ai_type="turret"))
        self.assertEqual(result["status"], "ok")

    def test_invalid_ai_type(self):
        with self.assertRaises(ValueError):
            create_enemy_entity("Bad", ai_type="nonexistent")


class TestCameraTemplate(unittest.TestCase):

    def test_top_down(self):
        result = json.loads(create_camera_entity(camera_type="top_down"))
        self.assertEqual(result["status"], "ok")

    def test_isometric(self):
        result = json.loads(create_camera_entity(camera_type="isometric"))
        self.assertEqual(result["status"], "ok")

    def test_invalid_type(self):
        with self.assertRaises(ValueError):
            create_camera_entity(camera_type="nonexistent")


class TestPickupTemplate(unittest.TestCase):

    def test_health_pickup(self):
        result = json.loads(create_pickup_entity("HP1", pickup_type="health"))
        self.assertEqual(result["status"], "ok")

    def test_ammo_pickup(self):
        result = json.loads(create_pickup_entity("Ammo1", pickup_type="ammo"))
        self.assertEqual(result["status"], "ok")

    def test_invalid_type(self):
        with self.assertRaises(ValueError):
            create_pickup_entity("Bad", pickup_type="nonexistent")


class TestProjectileTemplate(unittest.TestCase):

    def test_basic(self):
        result = json.loads(create_projectile_spawner_entity("Launcher1"))
        self.assertEqual(result["status"], "ok")


class TestEnvironmentTemplate(unittest.TestCase):

    def test_obstacle(self):
        result = json.loads(create_obstacle("Rock1", position=[5, 5, 0]))
        self.assertEqual(result["status"], "ok")

    def test_trigger_zone(self):
        result = json.loads(create_trigger_zone("Zone1", position=[0, 0, 0], size=[5, 5, 5]))
        self.assertEqual(result["status"], "ok")


if __name__ == "__main__":
    unittest.main()
