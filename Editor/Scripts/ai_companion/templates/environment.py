# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Environment entity templates (obstacles, decorations, etc.)."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]


def create_obstacle(
    name: str,
    position: Optional[List[Number]] = None,
    scale: Union[Number, List[Number]] = 1.0,
    mesh: str = "primitive_cube",
) -> str:
    """Create a static obstacle entity.

    Args:
        name: Entity name.
        position: [x, y, z] world position.
        scale: Uniform scale or [x, y, z] scale.
        mesh: Mesh asset to use.

    Returns:
        JSON with entity details.
    """
    pos = position or [0, 0, 0]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_mesh(mesh)
        .with_physics(body_type="static")
        .with_collider(shape="box")
    )

    if isinstance(scale, (int, float)):
        builder.with_scale(float(scale))
    else:
        builder.with_scale(scale[0], scale[1], scale[2])

    return builder.build()


def create_trigger_zone(
    name: str,
    position: Optional[List[Number]] = None,
    size: Optional[List[Number]] = None,
    script_path: Optional[str] = None,
) -> str:
    """Create an invisible trigger zone entity.

    Args:
        name: Entity name.
        position: [x, y, z] world position.
        size: [x, y, z] size of the trigger volume.
        script_path: Lua script to run on trigger events.

    Returns:
        JSON with entity details.
    """
    pos = position or [0, 0, 0]
    sz = size or [5, 5, 5]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_scale(sz[0], sz[1], sz[2])
        .with_component("Box Shape")
        .with_collider(shape="box")
    )

    if script_path:
        builder.with_lua_script(script_path)

    return builder.build()
