# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Convenience helpers for working with O3DE transforms via azlmbr."""

from typing import List, Optional, Tuple, Union

Number = Union[int, float]


def to_vector3(values: Optional[List[Number]] = None, x: Number = 0, y: Number = 0, z: Number = 0):
    """Convert a list or x/y/z values to an azlmbr.math.Vector3."""
    import azlmbr.math as azmath

    if values is not None:
        if len(values) == 3:
            return azmath.Vector3(float(values[0]), float(values[1]), float(values[2]))
        raise ValueError(f"Expected 3 values for Vector3, got {len(values)}")
    return azmath.Vector3(float(x), float(y), float(z))


def set_entity_position(entity_id, position: List[Number]):
    """Set an entity's world position."""
    import azlmbr.components as components

    vec = to_vector3(position)
    components.TransformBus(azlmbr.bus.Event, "SetWorldTranslation", entity_id, vec)


def set_entity_rotation(entity_id, rotation: List[Number]):
    """Set an entity's rotation in degrees (Euler angles)."""
    import azlmbr.components as components
    import azlmbr.math as azmath
    import math

    # Convert degrees to radians
    rad_x = math.radians(float(rotation[0]))
    rad_y = math.radians(float(rotation[1]))
    rad_z = math.radians(float(rotation[2]))

    quat = azmath.Quaternion.CreateFromEulerAnglesDegrees(
        azmath.Vector3(float(rotation[0]), float(rotation[1]), float(rotation[2]))
    )
    components.TransformBus(azlmbr.bus.Event, "SetWorldRotationQuaternion", entity_id, quat)


def set_entity_scale(entity_id, scale: Union[Number, List[Number]]):
    """Set an entity's local scale. Accepts a single uniform value or [x, y, z]."""
    import azlmbr.components as components

    if isinstance(scale, (int, float)):
        vec = to_vector3(x=scale, y=scale, z=scale)
    else:
        vec = to_vector3(scale)
    components.TransformBus(azlmbr.bus.Event, "SetLocalScale", entity_id, vec)


def set_entity_parent(entity_id, parent_id):
    """Set an entity's parent."""
    import azlmbr.components as components

    components.TransformBus(azlmbr.bus.Event, "SetParent", entity_id, parent_id)


def get_entity_position(entity_id) -> Tuple[float, float, float]:
    """Get an entity's world position as a tuple."""
    import azlmbr.components as components

    vec = components.TransformBus(azlmbr.bus.Event, "GetWorldTranslation", entity_id)
    return (vec.x, vec.y, vec.z)
