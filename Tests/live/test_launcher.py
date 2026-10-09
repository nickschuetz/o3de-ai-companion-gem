# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Checks on a GameLauncher log produced by scripts/ci_launcher_test.sh.

Skipped unless ``AICOMPANION_GAME_LOG`` points at the launcher's Game.log for
a run of the arena level. The gem's Lua scripts write ``[AiCompanion] ...``
markers through ``Debug.Log``; each test asserts one gameplay mechanic fired
at runtime, which is the only kind of evidence that the launcher-reachable
Lua port actually works.
"""

from __future__ import annotations

import os
import re
import unittest

LOG = os.environ.get("AICOMPANION_GAME_LOG", "").strip()
LEVEL = os.environ.get("AICOMPANION_LAUNCHER_LEVEL", "AiCompanionLauncherArena")

if not LOG:
    raise unittest.SkipTest("launcher checks need AICOMPANION_GAME_LOG (set by scripts/ci_launcher_test.sh)")


class LauncherLogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        with open(LOG, encoding="utf-8", errors="replace") as fh:
            cls.lines = fh.read().splitlines()
        cls.text = "\n".join(cls.lines)

    def test_the_arena_level_loaded(self):
        self.assertRegex(self.text, re.compile(rf"Level load complete: .*{LEVEL.lower()}", re.I))

    def test_no_lua_runtime_errors(self):
        # A nil global or a wrong bus name shows up here as a Script error; the
        # static checks cannot catch a name that exists but behaves differently.
        bad = [l for l in self.lines if re.search(r"\[Error\] \(Script\)|attempt to (call|index) a nil value|\(Script\) - .*error", l)]
        self.assertEqual(bad, [], "Lua runtime errors in the launcher log")

    def test_player_registered_and_pickup_collected(self):
        # health_pickup.lua found the player through the shared body registry
        # (twin_stick_movement.lua registered it) and collected by distance.
        self.assertIn("[AiCompanion] pickup collected", self.text)

    def test_enemy_found_and_attacked_the_player(self):
        # enemy_chase_ai.lua located the player, chased by rigid-body velocity,
        # and attacked inside AttackRange.
        self.assertIn("[AiCompanion] enemy attack", self.text)

    def test_contact_damage_hit_the_enemy(self):
        # damage_on_contact.lua (as a hazard, Speed 0) found the enemy in the
        # registry within HitRadius and sent TakeDamage; the enemy reported it.
        self.assertIn("[AiCompanion] contact damage", self.text)
        self.assertIn("[AiCompanion] enemy took damage", self.text)


if __name__ == "__main__":
    unittest.main()
