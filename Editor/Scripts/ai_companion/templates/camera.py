# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Camera entity templates."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]

CAMERA_PRESETS = {
    "top_down": {
        "position_offset": [0, 0, 20],
        "rotation": [-90, 0, 0],
    },
    "isometric": {
        "position_offset": [10, -10, 15],
        "rotation": [-45, 0, 45],
    },
    "perspective": {
        "position_offset": [0, -10, 5],
        "rotation": [-20, 0, 0],
    },
    "side_view": {
        "position_offset": [20, 0, 5],
        "rotation": [0, 0, -90],
    },
}


def create_camera_entity(
    name: str = "MainCamera",
    camera_type: str = "perspective",
    follow_target: Optional[str] = None,
    offset: Optional[List[Number]] = None,
) -> str:
    """Create a camera entity.

    Args:
        name: Camera entity name.
        camera_type: Camera preset ("top_down", "isometric", "perspective", "side_view").
        follow_target: Name of entity to follow (requires a follow script).
        offset: Custom position offset [x, y, z]. Overrides preset offset.

    Returns:
        JSON with entity details.
    """
    if camera_type not in CAMERA_PRESETS:
        available = ", ".join(CAMERA_PRESETS.keys())
        raise ValueError(f"Unknown camera type '{camera_type}'. Available: {available}")

    preset = CAMERA_PRESETS[camera_type]
    pos = offset or preset["position_offset"]
    rot = preset["rotation"]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_rotation(rot[0], rot[1], rot[2])
        .with_component("Camera")
    )

    return builder.build()
