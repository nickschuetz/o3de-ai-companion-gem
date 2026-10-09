# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Fluent EntityBuilder for creating entities with components in a single call."""

import json
from typing import Any, Dict, List, Optional, Tuple, Union

from ..safety.validators import (
    validate_entity_name,
    validate_position,
    validate_component_type,
    validate_asset_path,
    validate_float,
    validate_target_entity,
)
from ..safety.sandbox import get_sandbox
from ..utils.json_output import success, error, entity_result
from ..utils.component_registry import resolve_component

Number = Union[int, float]

# EditorComponentAPI property paths for the physics components, as the editor
# reflects them on O3DE 26.x. The path segments are the EditContext display
# names: "Configuration" is EditorRigidBodyComponent::m_config
# (Gems/PhysX/Core/Code/Source/EditorRigidBodyComponent.cpp) and "Compute
# Mass" / "Mass" are RigidBodyConfiguration::m_computeMass / m_mass as
# reflected there. "Shape Configuration" is EditorColliderComponent::
# m_proxyShapeConfiguration and "Shape" is EditorProxyShapeConfig::m_shapeType
# (Gems/PhysX/Core/Code/Source/EditorColliderComponent.cpp). The engine's own
# editor tests use the same strings (AutomatedTesting/Gem/PythonTests/
# EditorPythonTestTools/.../PhysXDynamicRigidBodyComponent.py and
# editor_physx_primitive_collider.py). Kept as constants so a rename in a
# later engine release is a one-line fix.
RIGID_BODY_COMPUTE_MASS_PATH = "Configuration|Compute Mass"
RIGID_BODY_MASS_PATH = "Configuration|Mass"
COLLIDER_SHAPE_PATH = "Shape Configuration|Shape"

# Physics::ShapeType values (Code/Framework/AzFramework/AzFramework/Physics/
# ShapeConfiguration.h). The editor accepts the plain integer for the enum
# property, which is what the engine's Collider_SphereShapeEditing test
# passes; azlmbr.physics.ShapeType_* is not used because Capsule is not
# reflected there.
COLLIDER_SHAPE_TYPES: Dict[str, int] = {
    "sphere": 0,
    "box": 1,
    "capsule": 2,
    "cylinder": 3,
}

# Mass in kilograms. The engine's lower bound is 0 (treated as infinite mass);
# the upper bound here is only a sanity check against typos.
MAX_MASS_KG = 1.0e9

_BUS_CALL_ERRORS = (AttributeError, TypeError, RuntimeError, ValueError)


def _set_component_property(editor, bus, pair, property_path: str, value: Any) -> Tuple[bool, str]:
    """Set one property through EditorComponentAPIBus, never raising.

    Returns ``(True, "")`` when the editor reports success, otherwise
    ``(False, reason)``. A failed set must not abort a build: the entity and
    its components already exist, and the agent can correct the property
    afterwards from the warning in the result.
    """
    try:
        outcome = editor.EditorComponentAPIBus(
            bus.Broadcast, "SetComponentProperty", pair, property_path, value
        )
    except _BUS_CALL_ERRORS as exc:
        return False, f"{type(exc).__name__}: {exc}"
    if outcome is None:
        return False, "no outcome returned"
    if hasattr(outcome, "IsSuccess") and not outcome.IsSuccess():
        reason = outcome.GetError() if hasattr(outcome, "GetError") else "SetComponentProperty failed"
        return False, str(reason)
    return True, ""


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
        """Set the entity's parent by entity ID.

        Parenting under a protected system entity (``EditorGlobal``,
        ``SystemEntity``, any ``AZ::`` name) is refused with ``ValueError``;
        the parent's name is looked up in the editor at this call.
        """
        valid, err = validate_target_entity(parent_id)
        if not valid:
            raise ValueError(err)
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
        """Add a PhysX rigid body component.

        For a dynamic body ``mass`` (kilograms) is applied to the component on
        build: "Compute Mass" is switched off and "Mass" set, so the value
        survives in the saved level instead of the editor's computed mass.
        Static bodies have no mass.
        """
        if body_type == "dynamic":
            valid, err = validate_float(mass, 0.0, MAX_MASS_KG, "mass")
            if not valid:
                raise ValueError(err)
            self._components.append({
                "type": "PhysX Dynamic Rigid Body",
                "properties": {"mass": float(mass)},
            })
        else:
            self._components.append({
                "type": "PhysX Static Rigid Body",
                "properties": {},
            })
        return self

    def with_collider(self, shape: str = "box") -> "EntityBuilder":
        """Add a PhysX Primitive Collider component with the specified shape.

        ``shape`` is one of ``box``, ``sphere``, ``capsule`` or ``cylinder``
        and is applied to the component's "Shape" property on build. The
        shape's dimensions stay at the engine defaults.
        """
        key = str(shape).strip().lower()
        if key not in COLLIDER_SHAPE_TYPES:
            raise ValueError(
                f"Unknown collider shape '{shape}'. "
                f"Available: {', '.join(COLLIDER_SHAPE_TYPES)}"
            )
        self._components.append({
            "type": "PhysX Primitive Collider",
            "properties": {"shape": key},
        })
        return self

    def with_input(self, bindings_path: str) -> "EntityBuilder":
        """Add an Input component bound to the given .inputbindings asset.

        ``bindings_path`` is the gem-relative source path, e.g.
        ``Assets/Input/twin_stick.inputbindings``.
        """
        valid, err = validate_asset_path(bindings_path)
        if not valid:
            raise ValueError(err)
        self._components.append({
            "type": "Input",
            "properties": {"bindings_path": bindings_path},
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
        """Add an arbitrary component by type name.

        The name's characters are validated first (letters, digits, spaces,
        hyphens, underscores, parentheses), then it is resolved against the
        component registry, which suggests the closest known names on a miss.
        """
        valid, err = validate_component_type(component_type)
        if not valid:
            raise ValueError(err)
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

            return self._build_in_editor(editor, bus, entity_api)

        except ImportError:
            return self._outside_editor_result()

    def _build_in_editor(self, editor, bus, entity_api) -> str:
        """Create and configure the entity, then commit it to the prefab template."""
        from ..utils.transform_helpers import (
            commit_entity_to_prefab,
            mark_entity_dirty,
            set_entity_position,
            set_entity_rotation,
            set_entity_scale,
            set_entity_parent,
        )

        # Create entity
        entity_id = editor.ToolsApplicationRequestBus(
            bus.Broadcast, "CreateNewEntity", entity_api.EntityId()
        )
        get_sandbox().record_entity(entity_id)

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

        # The name and transform writes above do not mark the entity dirty
        # for the prefab system on their own; without this a saved level
        # keeps the entity's defaults.
        mark_entity_dirty(entity_id)

        # Add components.
        #
        # AddComponentOfType returns Outcome<vector<EntityComponentIdPair>>
        # on this build, despite the singular name. The actual pair is at
        # index 0 of the wrapped vector; we need that proxy to address
        # SetComponentProperty by EntityComponentIdPair.
        component_ids = {}
        component_pairs = {}
        entity_type_game = entity_api.EntityType().Game
        null_uuid_str = "00000000-0000-0000-0000-000000000000"
        for comp in self._components:
            comp_type = comp["type"]
            type_ids = editor.EditorComponentAPIBus(
                bus.Broadcast, "FindComponentTypeIdsByEntityType",
                [comp_type], entity_type_game
            )
            if not type_ids:
                continue
            tid = type_ids[0]
            if null_uuid_str in str(tid):
                continue

            pair = None
            outcome = editor.EditorComponentAPIBus(
                bus.Broadcast, "AddComponentOfType", entity_id, tid
            )
            if hasattr(outcome, "IsSuccess") and outcome.IsSuccess():
                val = outcome.GetValue()
                pair = val[0] if isinstance(val, list) and val else val
            else:
                # Older builds only have AddComponentsOfType (plural).
                outcome = editor.EditorComponentAPIBus(
                    bus.Broadcast, "AddComponentsOfType", entity_id, [tid]
                )
                if hasattr(outcome, "IsSuccess") and outcome.IsSuccess():
                    val = outcome.GetValue()
                    if val:
                        pair = val[0] if isinstance(val, list) else val

            if pair is not None:
                from ..utils.id_helpers import id_to_jsonable
                component_ids[comp_type] = id_to_jsonable(pair)
                component_pairs[comp_type] = pair

        from ..utils.id_helpers import id_to_jsonable

        self._apply_asset_properties(component_pairs)
        applied, warnings = self._apply_physics_properties(component_pairs)
        mark_entity_dirty(entity_id)
        commit_entity_to_prefab(entity_id)

        extra: Dict[str, Any] = {}
        if applied:
            extra["applied_properties"] = applied
        if warnings:
            extra["property_warnings"] = warnings

        return entity_result(
            entity_id=id_to_jsonable(entity_id),
            name=self._name,
            component_ids=component_ids,
            position=self._position,
            extra=extra or None,
        )

    def _apply_physics_properties(
        self, component_pairs: Dict[str, Any]
    ) -> Tuple[Dict[str, Dict[str, Any]], List[str]]:
        """Apply the stored mass and collider shape to the physics components.

        The rigid body is created with "Compute Mass" on, and the editor then
        overwrites ``m_mass`` with the mass it derives from the colliders
        (EditorRigidBodyComponent::CreateEditorWorldRigidBody), so a level
        saved with the default settings carries the computed value, not the
        one the agent asked for. Switching "Compute Mass" off before setting
        "Mass" makes the requested value stick. The collider's "Shape" is an
        enum written as its integer value.

        Every set is guarded: a bus call that raises or reports a failed
        outcome is recorded as a warning and the build carries on. Returns
        ``(applied, warnings)`` where ``applied`` maps component type to the
        property paths and values that were accepted.
        """
        import azlmbr.bus as bus
        import azlmbr.editor as editor

        applied: Dict[str, Dict[str, Any]] = {}
        warnings: List[str] = []

        def set_property(comp_type: str, property_path: str, value: Any) -> None:
            pair = component_pairs.get(comp_type)
            if pair is None:
                return
            ok, err = _set_component_property(editor, bus, pair, property_path, value)
            if ok:
                applied.setdefault(comp_type, {})[property_path] = value
            else:
                warnings.append(f"{comp_type}: could not set '{property_path}': {err}")

        for comp in self._components:
            props = comp.get("properties") or {}
            if comp["type"] == "PhysX Dynamic Rigid Body" and "mass" in props:
                set_property("PhysX Dynamic Rigid Body", RIGID_BODY_COMPUTE_MASS_PATH, False)
                set_property("PhysX Dynamic Rigid Body", RIGID_BODY_MASS_PATH, float(props["mass"]))
            elif comp["type"] == "PhysX Primitive Collider" and "shape" in props:
                shape_value = COLLIDER_SHAPE_TYPES.get(props["shape"])
                if shape_value is None:
                    warnings.append(
                        f"PhysX Primitive Collider: unknown shape '{props['shape']}' not applied")
                    continue
                set_property("PhysX Primitive Collider", COLLIDER_SHAPE_PATH, shape_value)

        return applied, warnings

    def _outside_editor_result(self) -> str:
        """Running outside the editor: a mock result so unit tests can run."""
        return entity_result(
            entity_id=0,
            name=self._name,
            component_ids={c["type"]: 0 for c in self._components},
            position=self._position,
        )

    def _apply_asset_properties(self, component_pairs: Dict[str, Any]) -> None:
        """Wire Asset<T> properties on freshly-created components.

        ``component_pairs`` maps component_type to the EntityComponentIdPair
        PythonProxyObject returned by AddComponentOfType. We resolve the
        product path for each asset-bearing component, look up its AssetId
        through the catalog, and call SetComponentProperty by pair directly.

        Failures are non-fatal: an unknown component, an unresolvable asset
        path, or a SetComponentProperty that reports IsSuccess False just
        leaves the property unset on that entity.
        """
        import azlmbr.bus as bus
        import azlmbr.editor as editor
        import azlmbr.asset as asset_api
        import azlmbr.math as math_api
        from ..utils.asset_paths import (
            resolve_primitive_mesh,
            resolve_lua_script,
            resolve_input_bindings,
        )

        null_type = math_api.Uuid()

        def set_asset_property(comp_type: str, property_path: str, product_path: str) -> None:
            pair = component_pairs.get(comp_type)
            if pair is None or not product_path:
                return
            asset_id = asset_api.AssetCatalogRequestBus(
                bus.Broadcast, "GetAssetIdByPath", product_path, null_type, False
            )
            if asset_id is None:
                return
            editor.EditorComponentAPIBus(
                bus.Broadcast, "SetComponentProperty",
                pair, property_path, asset_id,
            )

        def set_asset_property_unwrapped(comp_type: str, property_path: str, product_path: str) -> None:
            # Same shape as set_asset_property, but routes through the gem's
            # AiCompanionEditorRequestBus.SetComponentPropertyUnwrapped shim
            # for components that don't inherit from EditorComponentBase. See
            # o3de/o3de#19770 / PR #19771. Drop this helper and collapse the
            # caller back into set_asset_property once #19771 lands.
            pair = component_pairs.get(comp_type)
            if pair is None or not product_path:
                return
            asset_id = asset_api.AssetCatalogRequestBus(
                bus.Broadcast, "GetAssetIdByPath", product_path, null_type, False
            )
            if asset_id is None:
                return
            editor.AiCompanionEditorRequestBus(
                bus.Broadcast, "SetComponentPropertyUnwrapped",
                pair, property_path, asset_id,
            )

        for comp in self._components:
            props = comp.get("properties") or {}
            if comp["type"] == "Mesh":
                product = resolve_primitive_mesh(props.get("mesh_asset"))
                if product:
                    set_asset_property("Mesh", "Controller|Configuration|Model Asset", product)
            elif comp["type"] == "Lua Script":
                product = resolve_lua_script(props.get("script_path"))
                if product:
                    set_asset_property("Lua Script", "Script", product)
            elif comp["type"] == "Input":
                # InputConfigurationComponent inherits from AZ::Component (not
                # EditorComponentBase), so the editor wraps it in a
                # GenericComponentWrapper. The stock EditorComponentAPIBus path
                # passes PropertyTreeEditor a (wrapper-instance, wrapped-typeId)
                # pair that disagrees with itself, which crashes
                # Asset<>::Release() during the next move-assignment. Route
                # through the gem's SetComponentPropertyUnwrapped shim, which
                # performs the same unwrap as o3de/o3de#19771 before building
                # PropertyTreeEditor. Drop this branch and the
                # set_asset_property_unwrapped helper once #19771 lands.
                product = resolve_input_bindings(props.get("bindings_path"))
                if product:
                    props["resolved_bindings_product"] = product
                    set_asset_property_unwrapped(
                        "Input", "Input to event bindings", product)
