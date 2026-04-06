# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Fluent EntityBuilder for creating entities with components in a single call."""

import json
from typing import Any, Dict, List, Optional, Union

from ..safety.validators import (
    validate_entity_name,
    validate_position,
    validate_component_type,
    validate_asset_path,
)
from ..safety.sandbox import get_sandbox
from ..utils.json_output import success, error, entity_result
from ..utils.component_registry import resolve_component

Number = Union[int, float]


class EntityBuilder:
    """Fluent builder for creating O3DE entities with components.

    Usage:
        result = (EntityBuilder("Player")
            .at_position(0, 0, 1)
            .with_mesh("capsule")
            .with_physics(body_type="dynamic", mass=1.0)
            .with_lua_script("Scripts/Lua/twin_stick_movement.lua")
            .build())
    """

    def __init__(self, name: str):
        valid, err = validate_entity_name(name)
        if not valid:
            raise ValueError(err)
        self._name = name
        self._position: Optional[List[float]] = None
        self._rotation: Optional[List[float]] = None
        self._scale: Optional[Union[float, List[float]]] = None
        self._parent_id: Optional[int] = None
        self._components: List[Dict[str, Any]] = []

    def at_position(self, x: Number, y: Number, z: Number) -> "EntityBuilder":
        """Set the entity's world position."""
        pos = [float(x), float(y), float(z)]
        valid, err = validate_position(pos)
        if not valid:
            raise ValueError(err)
        self._position = pos
        return self

    def with_rotation(self, rx: Number, ry: Number, rz: Number) -> "EntityBuilder":
        """Set the entity's rotation in degrees (Euler angles)."""
        self._rotation = [float(rx), float(ry), float(rz)]
        return self

    def with_scale(self, sx: Number, sy: Optional[Number] = None, sz: Optional[Number] = None) -> "EntityBuilder":
        """Set the entity's scale. Pass one value for uniform scale, three for non-uniform."""
        if sy is None and sz is None:
            self._scale = float(sx)
        else:
            self._scale = [float(sx), float(sy or sx), float(sz or sx)]
        return self

    def with_parent(self, parent_id: int) -> "EntityBuilder":
        """Set the entity's parent by entity ID."""
        self._parent_id = parent_id
        return self

    def with_mesh(self, mesh_asset: str = "primitive_sphere") -> "EntityBuilder":
        """Add a Mesh component with the specified mesh asset."""
        self._components.append({
            "type": "Mesh",
            "properties": {"mesh_asset": mesh_asset},
        })
        return self

    def with_material(self, material_path: Optional[str] = None) -> "EntityBuilder":
        """Add a Material component."""
        props = {}
        if material_path:
            valid, err = validate_asset_path(material_path)
            if not valid:
                raise ValueError(err)
            props["material_path"] = material_path
        self._components.append({"type": "Material", "properties": props})
        return self

    def with_physics(self, body_type: str = "dynamic", mass: float = 1.0) -> "EntityBuilder":
        """Add PhysX rigid body + collider components."""
        if body_type == "dynamic":
            self._components.append({
                "type": "PhysX Rigid Body",
                "properties": {"mass": mass},
            })
        else:
            self._components.append({
                "type": "PhysX Static Rigid Body",
                "properties": {},
            })
        return self

    def with_collider(self, shape: str = "box") -> "EntityBuilder":
        """Add a PhysX Collider component with the specified shape."""
        self._components.append({
            "type": "PhysX Collider",
            "properties": {"shape": shape},
        })
        return self

    def with_lua_script(self, script_path: str) -> "EntityBuilder":
        """Add a Lua Script component."""
        valid, err = validate_asset_path(script_path)
        if not valid:
            raise ValueError(err)
        self._components.append({
            "type": "Lua Script",
            "properties": {"script_path": script_path},
        })
        return self

    def with_script_canvas(self, graph_path: str) -> "EntityBuilder":
        """Add a Script Canvas component."""
        valid, err = validate_asset_path(graph_path)
        if not valid:
            raise ValueError(err)
        self._components.append({
            "type": "Script Canvas",
            "properties": {"graph_path": graph_path},
        })
        return self

    def with_component(self, component_type: str, **properties) -> "EntityBuilder":
        """Add an arbitrary component by type name."""
        canonical, err = resolve_component(component_type)
        if canonical is None:
            raise ValueError(err)
        self._components.append({
            "type": canonical,
            "properties": properties,
        })
        return self

    def build(self) -> str:
        """Execute the build: create the entity and add all components.

        Returns a JSON string with the entity_id and component details.
        """
        sandbox = get_sandbox()
        sandbox.check_entity_limit(1)
        sandbox.check_timeout()

        try:
            import azlmbr.editor as editor
            import azlmbr.bus as bus
            import azlmbr.entity as entity_api
            import azlmbr.components as components
            from ..utils.transform_helpers import (
                set_entity_position,
                set_entity_rotation,
                set_entity_scale,
                set_entity_parent,
            )

            # Create entity
            entity_id = editor.ToolsApplicationRequestBus(
                bus.Broadcast, "CreateNewEntity", entity_api.EntityId()
            )

            # Set name
            editor.EditorEntityAPIBus(
                bus.Event, "SetName", entity_id, self._name
            )

            # Set transform
            if self._position:
                set_entity_position(entity_id, self._position)
            if self._rotation:
                set_entity_rotation(entity_id, self._rotation)
            if self._scale:
                if isinstance(self._scale, (int, float)):
                    set_entity_scale(entity_id, self._scale)
                else:
                    set_entity_scale(entity_id, self._scale)

            # Set parent
            if self._parent_id is not None:
                set_entity_parent(entity_id, self._parent_id)

            # Add components
            component_ids = {}
            for comp in self._components:
                comp_type = comp["type"]
                type_ids = editor.EditorComponentAPIBus(
                    bus.Broadcast, "FindComponentTypeIdsByEntityType",
                    [comp_type], entity_api.EntityComponentIdPair.EntityType
                )
                if type_ids:
                    outcome = editor.EditorComponentAPIBus(
                        bus.Broadcast, "AddComponentsOfType", entity_id, [type_ids[0]]
                    )
                    if outcome.IsSuccess():
                        comp_id_list = outcome.GetValue()
                        if comp_id_list:
                            component_ids[comp_type] = int(comp_id_list[0])

            return entity_result(
                entity_id=int(entity_id),
                name=self._name,
                component_ids=component_ids,
                position=self._position,
            )

        except ImportError:
            # Running outside editor — return a mock result for testing
            return entity_result(
                entity_id=0,
                name=self._name,
                component_ids={c["type"]: 0 for c in self._components},
                position=self._position,
            )
