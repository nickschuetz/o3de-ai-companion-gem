# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Projectile entity templates."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]


def create_projectile_spawner_entity(
    name: str,
    parent_entity_id: Optional[int] = None,
    direction: str = "forward",
    speed: float = 20.0,
    damage: float = 10.0,
) -> str:
    """Create a projectile spawner entity (attaches to a parent like a player).

    Args:
        name: Entity name.
        parent_entity_id: Parent entity ID to attach to.
        direction: Firing direction ("forward", "up", etc.).
        speed: Projectile speed.
        damage: Damage per projectile.

    Returns:
        JSON with entity details.
    """
    builder = (
        EntityBuilder(name)
        .at_position(0, 0, 0)
        .with_lua_script("Scripts/Lua/projectile_launcher.lua")
    )

    if parent_entity_id is not None:
        builder.with_parent(parent_entity_id)

    return builder.build()
