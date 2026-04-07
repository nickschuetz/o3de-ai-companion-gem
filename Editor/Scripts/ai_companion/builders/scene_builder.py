# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""SceneBuilder for bootstrapping complete scenes with ground, lighting, and camera."""

import json
from typing import Any, Dict, List, Optional

from .entity_builder import EntityBuilder
from ..safety.rollback import with_undo_batch
from ..utils.json_output import success


class SceneBuilder:
    """Fluent builder for setting up a complete scene.

    Usage:
        result = (SceneBuilder()
            .with_ground(size=50)
            .with_lighting("three_point")
            .with_camera("top_down")
            .build())
    """

    def __init__(self):
        self._builders: List[EntityBuilder] = []
        self._ground_size: Optional[float] = None
        self._lighting_preset: Optional[str] = None
        self._camera_type: Optional[str] = None
        self._camera_offset: List[float] = [0, 0, 20]

    def with_ground(self, size: float = 50.0) -> "SceneBuilder":
        """Add a ground plane to the scene."""
        self._ground_size = size
        return self

    def with_lighting(self, preset: str = "three_point") -> "SceneBuilder":
        """Add a lighting preset to the scene."""
        self._lighting_preset = preset
        return self

    def with_camera(
        self,
        camera_type: str = "perspective",
        offset: Optional[List[float]] = None,
    ) -> "SceneBuilder":
        """Add a camera to the scene."""
        self._camera_type = camera_type
        if offset:
            self._camera_offset = offset
        return self

    def add_entity(self, builder: EntityBuilder) -> "SceneBuilder":
        """Add a custom entity builder to the scene."""
        self._builders.append(builder)
        return self

    def build(self) -> str:
        """Execute the scene build. Returns JSON with all created entities."""
        created = []

        # Ground
        if self._ground_size is not None:
            from .terrain_builder import create_ground
            result = json.loads(create_ground(size=self._ground_size))
            created.append(result.get("data", {}))

        # Lighting
        if self._lighting_preset is not None:
            from .lighting_builder import create_lighting
            result = json.loads(create_lighting(preset=self._lighting_preset))
            created.append(result.get("data", {}))

        # Camera
        if self._camera_type is not None:
            from ..templates.camera import create_camera_entity
            result = json.loads(create_camera_entity(
                name="SceneCamera",
                camera_type=self._camera_type,
                offset=self._camera_offset,
            ))
            created.append(result.get("data", {}))

        # Custom entities
        for builder in self._builders:
            result = json.loads(builder.build())
            created.append(result.get("data", {}))

        return success({
            "scene_entities": created,
            "total_created": len(created),
        })
