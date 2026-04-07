# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Ground/terrain setup helpers."""

from typing import Union

from .entity_builder import EntityBuilder

Number = Union[int, float]


def create_ground(
    size: Number = 50.0,
    name: str = "Ground",
) -> str:
    """Create a flat ground plane with physics collision.

    Args:
        size: Width and depth of the ground plane.
        name: Entity name for the ground.

    Returns:
        JSON with entity details.
    """
    half = float(size) / 2.0
    return (
        EntityBuilder(name)
        .at_position(0, 0, 0)
        .with_scale(float(size), float(size), 0.1)
        .with_mesh("primitive_cube")
        .with_physics(body_type="static")
        .with_collider(shape="box")
        .build()
    )


def create_wall(
    name: str,
    position_x: Number,
    position_y: Number,
    length: Number,
    height: Number = 3.0,
    thickness: Number = 0.5,
    orientation: str = "x",
) -> str:
    """Create a wall entity.

    Args:
        name: Wall entity name.
        position_x: X position of the wall center.
        position_y: Y position of the wall center.
        length: Length of the wall.
        height: Height of the wall.
        thickness: Thickness of the wall.
        orientation: "x" for wall along X axis, "y" for wall along Y axis.

    Returns:
        JSON with entity details.
    """
    if orientation == "x":
        scale_x, scale_y = float(length), float(thickness)
    else:
        scale_x, scale_y = float(thickness), float(length)

    return (
        EntityBuilder(name)
        .at_position(float(position_x), float(position_y), float(height) / 2.0)
        .with_scale(scale_x, scale_y, float(height))
        .with_mesh("primitive_cube")
        .with_physics(body_type="static")
        .with_collider(shape="box")
        .build()
    )


def create_arena_walls(size: Number = 30.0, height: Number = 3.0) -> list:
    """Create four walls enclosing a square arena.

    Returns a list of JSON result strings for each wall.
    """
    half = float(size) / 2.0
    results = []

    results.append(create_wall("WallNorth", 0, half, float(size), float(height), orientation="x"))
    results.append(create_wall("WallSouth", 0, -half, float(size), float(height), orientation="x"))
    results.append(create_wall("WallEast", half, 0, float(size), float(height), orientation="y"))
    results.append(create_wall("WallWest", -half, 0, float(size), float(height), orientation="y"))

    return results
