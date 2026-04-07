# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Player entity templates."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]

MOVEMENT_SCRIPTS = {
    "twin_stick": "Scripts/Lua/twin_stick_movement.lua",
}


def create_player_entity(
    name: str = "Player",
    position: Optional[List[Number]] = None,
    mesh: str = "primitive_capsule",
    physics: bool = True,
    movement: Optional[str] = None,
    health: float = 100.0,
) -> str:
    """Create a player entity with common game components.

    Args:
        name: Entity name.
        position: [x, y, z] world position. Defaults to [0, 0, 1].
        mesh: Mesh asset to use.
        physics: Whether to add physics components.
        movement: Movement script preset (e.g., "twin_stick"). None for no script.
        health: Player health value (stored as a script property).

    Returns:
        JSON with entity details.
    """
    pos = position or [0, 0, 1]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_mesh(mesh)
    )

    if physics:
        builder.with_physics(body_type="dynamic", mass=1.0)
        builder.with_collider(shape="capsule")

    if movement and movement in MOVEMENT_SCRIPTS:
        builder.with_lua_script(MOVEMENT_SCRIPTS[movement])
    elif movement:
        raise ValueError(
            f"Unknown movement preset '{movement}'. "
            f"Available: {', '.join(MOVEMENT_SCRIPTS.keys())}"
        )

    return builder.build()
