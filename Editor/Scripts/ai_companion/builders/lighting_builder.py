# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Lighting rig presets for quick scene setup."""

import json
from typing import Dict

from .entity_builder import EntityBuilder
from ..utils.json_output import success

LIGHTING_PRESETS: Dict[str, dict] = {
    "three_point": {
        "description": "Classic three-point lighting: key, fill, and rim lights",
        "lights": [
            {"name": "KeyLight", "type": "Directional Light", "rotation": [-45, -30, 0]},
            {"name": "FillLight", "type": "Point Light", "position": [-10, -5, 8]},
            {"name": "RimLight", "type": "Spot Light", "position": [5, 10, 10], "rotation": [45, 0, 0]},
        ],
    },
    "outdoor": {
        "description": "Outdoor sunlight with ambient skylight",
        "lights": [
            {"name": "SunLight", "type": "Directional Light", "rotation": [-60, -20, 0]},
        ],
    },
    "indoor": {
        "description": "Soft indoor lighting with multiple point lights",
        "lights": [
            {"name": "CeilingLight1", "type": "Point Light", "position": [0, 0, 5]},
            {"name": "CeilingLight2", "type": "Point Light", "position": [8, 8, 5]},
            {"name": "CeilingLight3", "type": "Point Light", "position": [-8, -8, 5]},
        ],
    },
}


def create_lighting(preset: str = "three_point") -> str:
    """Create a lighting rig from a preset.

    Args:
        preset: One of "three_point", "outdoor", "indoor".

    Returns:
        JSON string with created light entity details.
    """
    if preset not in LIGHTING_PRESETS:
        available = ", ".join(LIGHTING_PRESETS.keys())
        raise ValueError(f"Unknown lighting preset '{preset}'. Available: {available}")

    config = LIGHTING_PRESETS[preset]
    created = []

    for light in config["lights"]:
        builder = EntityBuilder(light["name"])

        pos = light.get("position", [0, 0, 10])
        builder.at_position(pos[0], pos[1], pos[2])

        rot = light.get("rotation")
        if rot:
            builder.with_rotation(rot[0], rot[1], rot[2])

        builder.with_component(light["type"])

        result = json.loads(builder.build())
        created.append(result.get("data", {}))

    return success({
        "preset": preset,
        "description": config["description"],
        "lights": created,
    })


def list_lighting_presets() -> str:
    """List all available lighting presets."""
    presets = []
    for name, config in LIGHTING_PRESETS.items():
        presets.append({
            "name": name,
            "description": config["description"],
            "light_count": len(config["lights"]),
        })
    return success(presets)
