# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""
Twin-Stick Shooter Example — Full Game Setup

Run this script via o3de-mcp's run_editor_python() to build a complete
twin-stick shooter game in a single call:

    run_editor_python(open("path/to/setup.py").read())

Or import and call from within the editor Python console:

    from ai_companion.examples import twin_stick_shooter
    twin_stick_shooter.setup()
"""

from ai_companion.api import (
    bootstrap_twin_stick_arena,
    create_player,
    create_enemy,
    create_pickup,
    create_camera,
    get_scene_snapshot,
)


def setup():
    """Build the complete twin-stick shooter example."""

    print("=== Twin-Stick Shooter Setup ===")
    print()

    # 1. Create the arena (ground + walls + lighting)
    print("[1/6] Creating arena...")
    arena = bootstrap_twin_stick_arena(size=30, wall_height=3)
    print(arena)
    print()

    # 2. Create the player at the center
    print("[2/6] Creating player...")
    player = create_player(
        name="Player",
        position=[0, 0, 1],
        movement="twin_stick",
        health=100,
    )
    print(player)
    print()

    # 3. Set up the top-down camera
    print("[3/6] Creating camera...")
    camera = create_camera(
        name="MainCamera",
        camera_type="top_down",
        follow_target="Player",
        offset=[0, 0, 25],
    )
    print(camera)
    print()

    # 4. Spawn enemies
    print("[4/6] Creating enemies...")
    enemies = [
        create_enemy("Chaser1", position=[10, 10, 1], ai_type="chaser", speed=4),
        create_enemy("Chaser2", position=[-10, 10, 1], ai_type="chaser", speed=3),
        create_enemy("Chaser3", position=[10, -10, 1], ai_type="chaser", speed=3.5),
        create_enemy("Turret1", position=[0, 12, 1], ai_type="turret"),
    ]
    for e in enemies:
        print(e)
    print()

    # 5. Place pickups
    print("[5/6] Placing pickups...")
    pickups = [
        create_pickup("HealthPack1", position=[5, 5, 0.5], pickup_type="health", value=25),
        create_pickup("HealthPack2", position=[-5, -5, 0.5], pickup_type="health", value=25),
        create_pickup("AmmoPack1", position=[-7, 7, 0.5], pickup_type="ammo", value=10),
    ]
    for p in pickups:
        print(p)
    print()

    # 6. Verify the scene
    print("[6/6] Verifying scene...")
    snapshot = get_scene_snapshot()
    print(snapshot)
    print()

    print("=== Twin-Stick Shooter Ready! ===")
    print("Enter game mode to play.")


# Auto-run when executed as a script
if __name__ == "__main__":
    setup()
else:
    # Also auto-run when loaded via run_editor_python()
    setup()
