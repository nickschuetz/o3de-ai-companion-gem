# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Physics body creation helpers."""

from typing import List, Optional, Union

from .entity_builder import EntityBuilder

Number = Union[int, float]


def create_static_body(
    name: str,
    position: List[Number],
    mesh: str = "primitive_cube",
    collider_shape: str = "box",
) -> str:
    """Create a static physics body (non-moving collision geometry).

    Returns JSON with entity details.
    """
    return (
        EntityBuilder(name)
        .at_position(position[0], position[1], position[2])
        .with_mesh(mesh)
        .with_physics(body_type="static")
        .with_collider(shape=collider_shape)
        .build()
    )


def create_dynamic_body(
    name: str,
    position: List[Number],
    mesh: str = "primitive_cube",
    mass: float = 1.0,
    collider_shape: str = "box",
) -> str:
    """Create a dynamic physics body (moves under physics simulation).

    Returns JSON with entity details.
    """
    return (
        EntityBuilder(name)
        .at_position(position[0], position[1], position[2])
        .with_mesh(mesh)
        .with_physics(body_type="dynamic", mass=mass)
        .with_collider(shape=collider_shape)
        .build()
    )
