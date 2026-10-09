# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Build the launcher-test arena through the gem's Python API and save it.

Run from scripts/ci_launcher_test.sh with the editor's AgentServer up. Takes
the level name as its only argument. Everything is placed so the gameplay
scripts fire on their own, with no input:

- a ground plane with lights and a camera (bootstrap_scene), so the dynamic
  bodies have something to stand on,
- a player with the twin-stick movement script (registers itself under
  "Player" in the shared body registry),
- a health pickup inside the player's PickupRadius (collects on the first
  ticks and reports it),
- a chasing enemy a few metres away (finds the player, closes in, attacks),
- a contact-damage hazard sitting on the enemy's start position (damages the
  enemy on its first tick and destroys itself).
"""

from __future__ import annotations

import json
import sys
import time

from agent_client import AgentClient


def main(level_name: str) -> int:
    client = AgentClient(timeout=120)

    created = client.run(
        "import azlmbr.legacy.general as general\n"
        f"general.create_level_no_prompt('', {level_name!r}, 1024, 1, 4096, False)\n"
        "print('LEVEL=' + str(general.get_current_level_name()))\n"
    )
    if f"LEVEL={level_name}" not in created:
        print(f"could not create level {level_name}: {created.strip()}", file=sys.stderr)
        return 1

    # Sanity check that the new level accepts a named entity before building
    # on it (create, read back, delete). It has never failed; it is kept so a
    # future level-creation problem fails here with a clear message rather
    # than as a bare entity in the saved level.
    ready = False
    for _ in range(30):
        probe = client.api('create_entity_batch([{"name": "ReadyProbe", "position": [0, 0, 0]}])')
        ids = [e.get("entity_id") for e in probe.get("data", {}).get("entities", []) if e.get("entity_id")]
        for entity_id in ids:
            info = json.loads(client.request("get_entity", entity_id=str(entity_id).strip("[]"))["output"])
            if info.get("name") == "ReadyProbe":
                ready = True
            client.request("delete_entity", entity_id=str(entity_id).strip("[]"))
        if ready:
            break
        time.sleep(2)
    if not ready:
        print("the new level never accepted a named entity", file=sys.stderr)
        return 1
    print("level ready")

    steps = [
        # Ground first: the player and enemy are dynamic rigid bodies under
        # gravity, and without a floor they fall away from each other forever
        # (the first run showed the chase starting and never reaching attack
        # range). bootstrap_scene also adds lights and a camera.
        ('bootstrap_scene(ground_size=30.0)', "bootstrap_scene"),
        ('create_player("ArenaPlayer", position=[0, 0, 1], movement="twin_stick")', "create_player"),
        ('create_pickup("ArenaHealth", position=[0, 0.6, 1], pickup_type="health")', "create_pickup"),
        ('create_enemy("ArenaChaser", position=[6, 0, 1], ai_type="chaser")', "create_enemy"),
        (
            'create_trigger_zone("ArenaHazard", position=[6, 0, 1], size=[1, 1, 1], '
            'script_path="Scripts/Lua/damage_on_contact.lua")',
            "create_trigger_zone",
        ),
    ]
    def describe(entity_id) -> str:
        info = json.loads(client.request("get_entity", entity_id=str(entity_id).strip("[]"))["output"])
        comps = [c for c in info.get("components", []) if not c.startswith("Editor") or "RigidBody" in c or "Collider" in c]
        return f"{info.get('name')} pos={info.get('position')} scale={info.get('scale')} comps={comps}"

    for expression, name in steps:
        result = client.api(expression, imports=name)
        if result.get("status") != "ok":
            print(f"{name} failed: {json.dumps(result)}", file=sys.stderr)
            return 1
        data = result["data"]
        created_ids = [data["entity_id"]] if "entity_id" in data else [e.get("entity_id") for e in data.get("scene_entities", []) if e.get("entity_id")]
        print(f"{name}: {len(created_ids)} entit{'y' if len(created_ids) == 1 else 'ies'}")
        for entity_id in created_ids:
            print("   " + describe(entity_id))

    # Diagnostics (kept in the run log): the ground collider's properties, and
    # the player's height after three seconds of editor Play mode. If the
    # player falls here too, the physics setup is wrong before the launcher
    # ever sees it.
    diag = client.run(
        "import azlmbr.bus as bus, azlmbr.editor as editor, azlmbr.entity as entity, azlmbr.legacy.general as general, time, json\n"
        "ids = entity.SearchBus(bus.Broadcast, 'SearchEntities', entity.SearchFilter())\n"
        "def by_name(n):\n"
        "    for i in ids:\n"
        "        if editor.EditorEntityInfoRequestBus(bus.Event, 'GetName', i) == n: return i\n"
        "g = by_name('Ground')\n"
        "comps = editor.EditorComponentAPIBus(bus.Broadcast, 'GetComponentsOfEntity', g) or []\n"
        "for c in comps:\n"
        "    props = editor.EditorComponentAPIBus(bus.Broadcast, 'BuildComponentPropertyList', c) or []\n"
        "    keep = [p for p in props if any(k in p for k in ('Shape', 'Box', 'Dimensions', 'Trigger', 'Collision', 'Scale'))]\n"
        "    if keep: print('GROUND COMPONENT', c, keep[:12])\n"
        "pl = by_name('ArenaPlayer')\n"
        "import azlmbr.components as components\n"
        "general.enter_game_mode()\n"
        "general.idle_wait(3.0)\n"
        "pos = components.TransformBus(bus.Event, 'GetWorldTranslation', pl)\n"
        "print('PLAYER AFTER 3S IN PLAY MODE', pos)\n"
        "general.exit_game_mode()\n"
        "general.idle_wait(1.0)\n"
    )
    print(diag.strip()[:3000])

    saved = client.run(
        "import azlmbr.legacy.general as general\n"
        "general.save_level()\n"
        "print('SAVED=' + str(general.get_current_level_name()))\n"
    )
    if f"SAVED={level_name}" not in saved:
        print(f"save_level did not confirm: {saved.strip()}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
