# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Enemy entity templates."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]

AI_SCRIPTS = {
    "chaser": "Scripts/Lua/enemy_chase_ai.lua",
    "turret": "Scripts/Lua/projectile_launcher.lua",
}


def create_enemy_entity(
    name: str,
    position: Optional[List[Number]] = None,
    ai_type: str = "chaser",
    mesh: str = "primitive_cube",
    health: float = 50.0,
    speed: float = 3.0,
) -> str:
    """Create an enemy entity with AI behavior.

    Args:
        name: Entity name.
        position: [x, y, z] world position.
        ai_type: AI behavior preset ("chaser" or "turret").
        mesh: Mesh asset to use.
        health: Enemy health value.
        speed: Movement speed for chaser enemies.

    Returns:
        JSON with entity details.
    """
    pos = position or [0, 0, 1]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_mesh(mesh)
        .with_physics(body_type="dynamic", mass=1.0)
        .with_collider(shape="box")
    )

    if ai_type in AI_SCRIPTS:
        builder.with_lua_script(AI_SCRIPTS[ai_type])
    else:
        available = ", ".join(AI_SCRIPTS.keys())
        raise ValueError(f"Unknown AI type '{ai_type}'. Available: {available}")

    return builder.build()
