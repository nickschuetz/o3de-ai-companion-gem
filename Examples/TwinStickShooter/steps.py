# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Twin-Stick Shooter example, broken into discrete step functions.

Each function performs one logical step of the scene setup and is safe to
invoke as its own `run_editor_python` call. The recommended invocation
pattern (driven from outside the editor via o3de-mcp) is to call each step
in its own request so the editor's main thread fully drains between calls
and the prefab system has time to finalize newly-created entities before
the next step fires.

The all-in-one entry point in `setup.py` calls these functions in order.
That works for quick demos, but on builds that exhibit the known SetName
race after `open_level`, the first batch of entities may show up in the
Outliner as `EntityN` placeholders. Splitting calls across requests
sidesteps the race entirely.
"""

LEVEL_NAME = "TwinStickArena"
FALLBACK_LEVELS = ("DefaultLevel", "Default", "Main", "MainLevel")


def ensure_level_open(level_name: str = LEVEL_NAME) -> str:
    """Make sure a level is open in the editor.

    Strategy:
      1. If a level is already open, reuse it.
      2. Try to open one of the project's default levels by basename.
      3. As a last resort, call legacy `create_level_no_prompt`.

    Returns a human-readable status string. Safe to call outside the editor.
    """
    try:
        import azlmbr.legacy.general as general
    except ImportError:
        return "Not running inside O3DE Editor; skipping level setup."

    try:
        current = general.get_current_level_name()
    except Exception:
        current = ""

    if current and current.lower() != "untitled":
        return f"Using already-open level '{current}'."

    for candidate in FALLBACK_LEVELS:
        try:
            if general.open_level(candidate):
                if hasattr(general, "idle_wait"):
                    general.idle_wait(0.5)
                return f"Opened existing project level '{candidate}'."
        except Exception:
            pass

    try:
        general.create_level_no_prompt(level_name, 1024, 1, False)
        if general.get_current_level_name():
            return f"Created and opened level '{level_name}'."
    except Exception as exc:
        return (
            f"Could not auto-create a level ({exc}). "
            "Open a level manually via File > New Level, then re-run."
        )

    return (
        "No level is open and none of the default level names could be "
        "opened. Create a level via File > New Level, then re-run."
    )


def build_arena(size: float = 30.0, wall_height: float = 3.0) -> str:
    """Step 1: ground, walls, and three-point lighting."""
    from ai_companion.api import bootstrap_twin_stick_arena
    return bootstrap_twin_stick_arena(size=size, wall_height=wall_height)


def add_player(position=(0, 0, 1), health: float = 100.0) -> str:
    """Step 2: capsule player with twin-stick movement and physics."""
    from ai_companion.api import create_player
    return create_player(
        name="Player",
        position=list(position),
        movement="twin_stick",
        health=health,
    )


def add_camera(offset=(0, 0, 25)) -> str:
    """Step 3: top-down camera following the player."""
    from ai_companion.api import create_camera
    return create_camera(
        name="MainCamera",
        camera_type="top_down",
        follow_target="Player",
        offset=list(offset),
    )


def add_enemies() -> list:
    """Step 4: three chasers plus one stationary turret."""
    from ai_companion.api import create_enemy
    return [
        create_enemy("Chaser1", position=[10, 10, 1], ai_type="chaser", speed=4),
        create_enemy("Chaser2", position=[-10, 10, 1], ai_type="chaser", speed=3),
        create_enemy("Chaser3", position=[10, -10, 1], ai_type="chaser", speed=3.5),
        create_enemy("Turret1", position=[0, 12, 1], ai_type="turret"),
    ]


def add_pickups() -> list:
    """Step 5: two health pickups and one ammo pickup."""
    from ai_companion.api import create_pickup
    return [
        create_pickup("HealthPack1", position=[5, 5, 0.5], pickup_type="health", value=25),
        create_pickup("HealthPack2", position=[-5, -5, 0.5], pickup_type="health", value=25),
        create_pickup("AmmoPack1", position=[-7, 7, 0.5], pickup_type="ammo", value=10),
    ]


def verify_scene() -> str:
    """Step 6: capture a snapshot of the current scene."""
    from ai_companion.api import get_scene_snapshot
    return get_scene_snapshot()
