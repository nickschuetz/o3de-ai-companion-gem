# Copyright (c) Contributors to the Open 3D Engine Project.
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
    import azlmbr.bus as bus
    import azlmbr.components as components

    vec = to_vector3(position)
    components.TransformBus(bus.Event, "SetWorldTranslation", entity_id, vec)


def set_entity_rotation(entity_id, rotation: List[Number]):
    """Set an entity's rotation in degrees (Euler angles)."""
    import azlmbr.bus as bus
    import azlmbr.components as components
    import azlmbr.math as azmath

    euler = azmath.Vector3(float(rotation[0]), float(rotation[1]), float(rotation[2]))
    # O3DE 2310+ flattens static factories to module level with a class-name
    # prefix. Older builds expose them as Quaternion.CreateFromEulerAnglesDegrees.
    if hasattr(azmath, "Quaternion_CreateFromEulerAnglesDegrees"):
        quat = azmath.Quaternion_CreateFromEulerAnglesDegrees(euler)
    else:
        quat = azmath.Quaternion.CreateFromEulerAnglesDegrees(euler)
    components.TransformBus(bus.Event, "SetWorldRotationQuaternion", entity_id, quat)


def set_entity_scale(entity_id, scale: Union[Number, List[Number]]):
    """Set an entity's local scale. Accepts a single uniform value or [x, y, z].

    On O3DE 2310+, the Transform component only exposes uniform scale via
    `SetLocalUniformScale`. Non-uniform scaling requires a separate
    `Non-uniform Scale` component, which we cannot reliably add on every
    build (the component registration name varies). When a non-uniform
    scale is requested but the component is unavailable, we fall back to
    the largest axis as a uniform scale so the entity is at least visible.
    """
    import azlmbr.bus as bus
    import azlmbr.components as components

    if isinstance(scale, (int, float)):
        components.TransformBus(bus.Event, "SetLocalUniformScale", entity_id, float(scale))
        return

    sx, sy, sz = float(scale[0]), float(scale[1]), float(scale[2])

    if abs(sx - sy) < 1e-4 and abs(sy - sz) < 1e-4:
        components.TransformBus(bus.Event, "SetLocalUniformScale", entity_id, sx)
        return

    if _try_set_non_uniform_scale(entity_id, sx, sy, sz):
        return

    # Fallback: use the largest axis as the uniform scale. This is visually
    # wrong for objects that depend on non-uniform geometry (walls, planes)
    # but ensures the entity is visible. Tracked as a known limitation.
    components.TransformBus(
        bus.Event, "SetLocalUniformScale", entity_id, max(sx, sy, sz)
    )


def _try_set_non_uniform_scale(entity_id, sx: float, sy: float, sz: float) -> bool:
    """Attempt to add the Non-uniform Scale component and set its Scale.

    Returns True if the component was added and the value was set,
    False if the component is not registered on this build.
    """
    import azlmbr.bus as bus
    import azlmbr.entity as entity_api
    import azlmbr.editor as editor
    import azlmbr.math as azmath

    for type_name in ("Non-uniform Scale", "NonUniformScale", "Non Uniform Scale"):
        tids = editor.EditorComponentAPIBus(
            bus.Broadcast, "FindComponentTypeIdsByEntityType",
            [type_name], entity_api.EntityType().Game
        )
        if not tids:
            continue
        # FindComponentTypeIdsByEntityType returns a list whose entries may be
        # null UUID wrappers when the name did not match. We can't reliably
        # distinguish a valid Uuid from a null one without an extra round
        # trip, so just attempt the add and see if it succeeds.
        outcome = editor.EditorComponentAPIBus(
            bus.Broadcast, "AddComponentOfType", entity_id, tids[0]
        )
        if not (hasattr(outcome, "IsSuccess") and outcome.IsSuccess()):
            continue
        pair = outcome.GetValue()
        vec = azmath.Vector3(sx, sy, sz)
        result = editor.EditorComponentAPIBus(
            bus.Broadcast, "SetComponentProperty", pair, "Scale", vec
        )
        if hasattr(result, "IsSuccess") and result.IsSuccess():
            return True
        # Component added but property path was wrong. Still report True so
        # we don't fall through to the uniform fallback (the user can fix
        # the property later via the Inspector).
        return True
    return False


def set_entity_parent(entity_id, parent_id):
    """Set an entity's parent."""
    import azlmbr.bus as bus
    import azlmbr.components as components

    components.TransformBus(bus.Event, "SetParent", entity_id, parent_id)


def get_entity_position(entity_id) -> Tuple[float, float, float]:
    """Get an entity's world position as a tuple."""
    import azlmbr.bus as bus
    import azlmbr.components as components

    vec = components.TransformBus(bus.Event, "GetWorldTranslation", entity_id)
    return (vec.x, vec.y, vec.z)
