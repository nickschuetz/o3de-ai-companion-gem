# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Pickup/collectible entity templates."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]

PICKUP_SCRIPTS = {
    "health": "Scripts/Lua/health_pickup.lua",
    "ammo": "Scripts/Lua/health_pickup.lua",  # Reuses health_pickup with different properties
}


def create_pickup_entity(
    name: str,
    position: Optional[List[Number]] = None,
    pickup_type: str = "health",
    value: float = 25.0,
    mesh: str = "primitive_sphere",
) -> str:
    """Create a pickup/collectible entity.

    Args:
        name: Entity name.
        position: [x, y, z] world position.
        pickup_type: Type of pickup ("health" or "ammo").
        value: Pickup value (heal amount, ammo count, etc.).
        mesh: Mesh asset to use.

    Returns:
        JSON with entity details.
    """
    pos = position or [0, 0, 0.5]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_scale(0.5)
        .with_mesh(mesh)
        .with_collider(shape="sphere")
    )

    if pickup_type in PICKUP_SCRIPTS:
        builder.with_lua_script(PICKUP_SCRIPTS[pickup_type])
    else:
        available = ", ".join(PICKUP_SCRIPTS.keys())
        raise ValueError(f"Unknown pickup type '{pickup_type}'. Available: {available}")

    return builder.build()
