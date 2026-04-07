# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""
AI Companion API — Main entry point for AI-driven game development in O3DE.

All functions return JSON strings for structured parsing by MCP servers.
All mutating functions are wrapped in undo batches for automatic rollback on failure.

Usage from o3de-mcp's run_editor_python():
    from ai_companion.api import create_player, get_scene_snapshot
    result = create_player("Player", position=[0, 0, 1], movement="twin_stick")
    print(result)
"""

import json
from typing import Any, Dict, List, Optional, Union

from .version import API_VERSION
from .safety.rollback import with_undo_batch, begin_undo_batch, end_undo_batch, rollback_last_batch
from .safety.sandbox import get_sandbox, reset_sandbox, SandboxLimitError
from .safety.validators import validate_entity_name, validate_position, is_protected_entity
from .utils.json_output import success, error, batch_result
from .utils.component_registry import list_components, get_categories

Number = Union[int, float]


# ---------------------------------------------------------------------------
# Meta
# ---------------------------------------------------------------------------

def get_api_version() -> str:
    """Return the AI Companion API version."""
    return success({"version": API_VERSION})


def get_available_functions() -> str:
    """Return a list of all available API functions with their signatures."""
    functions = [
        {"name": "get_api_version", "args": [], "description": "Get API version"},
        {"name": "get_available_functions", "args": [], "description": "List all API functions"},
        {"name": "get_component_catalog", "args": ["category?"], "description": "List known O3DE components"},
        {"name": "bootstrap_scene", "args": ["preset?", "ground_size?", "lighting?", "camera?"], "description": "Set up a complete scene"},
        {"name": "bootstrap_twin_stick_arena", "args": ["size?", "wall_height?"], "description": "Create a twin-stick shooter arena"},
        {"name": "create_player", "args": ["name", "position?", "mesh?", "physics?", "movement?", "health?"], "description": "Create a player entity"},
        {"name": "create_enemy", "args": ["name", "position?", "ai_type?", "mesh?", "health?", "speed?"], "description": "Create an enemy entity"},
        {"name": "create_projectile_spawner", "args": ["name", "parent_entity_id?", "direction?", "speed?", "damage?"], "description": "Create a projectile spawner"},
        {"name": "create_pickup", "args": ["name", "position?", "pickup_type?", "value?"], "description": "Create a pickup entity"},
        {"name": "create_trigger_zone", "args": ["name", "position?", "size?", "script_path?"], "description": "Create a trigger zone"},
        {"name": "create_entity_batch", "args": ["specs"], "description": "Create multiple entities from a spec list"},
        {"name": "create_grid", "args": ["name_prefix", "rows", "cols", "spacing", "template?"], "description": "Create a grid of entities"},
        {"name": "build_entity", "args": ["name"], "description": "Start a fluent EntityBuilder"},
        {"name": "setup_lighting", "args": ["preset?"], "description": "Set up a lighting rig"},
        {"name": "create_camera", "args": ["name?", "camera_type?", "follow_target?", "offset?"], "description": "Create a camera entity"},
        {"name": "create_static_body", "args": ["name", "position", "mesh?", "collider_shape?"], "description": "Create a static physics body"},
        {"name": "create_dynamic_body", "args": ["name", "position", "mesh?", "mass?", "collider_shape?"], "description": "Create a dynamic physics body"},
        {"name": "create_physics_ground", "args": ["size?"], "description": "Create a ground plane with physics"},
        {"name": "get_scene_snapshot", "args": [], "description": "Get full scene state as JSON"},
        {"name": "get_entity_tree", "args": [], "description": "Get entity hierarchy tree"},
        {"name": "inspect_entity", "args": ["entity_id"], "description": "Deep inspect a specific entity"},
        {"name": "validate_scene", "args": [], "description": "Validate scene for common issues"},
        {"name": "list_prefabs", "args": [], "description": "List available AiCompanion prefabs"},
        {"name": "spawn_prefab", "args": ["prefab_name", "position?"], "description": "Instantiate a prefab"},
        {"name": "begin_undo_batch", "args": ["label?"], "description": "Start a manual undo batch"},
        {"name": "end_undo_batch", "args": [], "description": "End the current undo batch"},
        {"name": "rollback_last_batch", "args": [], "description": "Undo the last batch"},
    ]
    return success(functions)


def get_component_catalog(category: Optional[str] = None) -> str:
    """List all known O3DE component types, optionally filtered by category."""
    components = list_components(category)
    categories = get_categories()
    return success({
        "components": components,
        "categories": categories,
        "total": len(components),
    })


# ---------------------------------------------------------------------------
# Scene Bootstrap
# ---------------------------------------------------------------------------

@with_undo_batch("Bootstrap Scene")
def bootstrap_scene(
    preset: str = "default",
    ground_size: float = 50.0,
    lighting: str = "three_point",
    camera: str = "perspective",
) -> str:
    """Set up a complete scene with ground, lighting, and camera.

    Args:
        preset: Scene preset name (currently only "default").
        ground_size: Size of the ground plane.
        lighting: Lighting preset ("three_point", "outdoor", "indoor").
        camera: Camera type ("top_down", "isometric", "perspective", "side_view").

    Returns:
        JSON with all created entity details.
    """
    from .builders.scene_builder import SceneBuilder

    builder = (
        SceneBuilder()
        .with_ground(ground_size)
        .with_lighting(lighting)
        .with_camera(camera)
    )
    return builder.build()


@with_undo_batch("Bootstrap Twin-Stick Arena")
def bootstrap_twin_stick_arena(size: float = 30.0, wall_height: float = 3.0) -> str:
    """Create a twin-stick shooter arena with ground, walls, and lighting.

    Args:
        size: Arena size (width and depth).
        wall_height: Height of the arena walls.

    Returns:
        JSON with all created entity details.
    """
    from .builders.terrain_builder import create_ground, create_arena_walls
    from .builders.lighting_builder import create_lighting

    results = []

    # Ground
    ground = json.loads(create_ground(size=size + 10))
    results.append(ground)

    # Walls
    wall_results = create_arena_walls(size=size, height=wall_height)
    for wr in wall_results:
        results.append(json.loads(wr))

    # Lighting
    lights = json.loads(create_lighting("three_point"))
    results.append(lights)

    return success({
        "arena_size": size,
        "wall_height": wall_height,
        "entities_created": len(results),
        "details": results,
    })


# ---------------------------------------------------------------------------
# Entity Creation (High-Level)
# ---------------------------------------------------------------------------

@with_undo_batch("Create Player")
def create_player(
    name: str = "Player",
    position: Optional[List[Number]] = None,
    mesh: str = "primitive_capsule",
    physics: bool = True,
    movement: Optional[str] = None,
    health: float = 100.0,
) -> str:
    """Create a player entity with common game components.

    Args:
        name: Entity name.
        position: [x, y, z] world position. Defaults to [0, 0, 1].
        mesh: Mesh asset ("primitive_capsule", "primitive_cube", etc.).
        physics: Whether to add physics components.
        movement: Movement preset ("twin_stick" or None).
        health: Player health value.

    Returns:
        JSON with entity details.
    """
    from .templates.player import create_player_entity
    return create_player_entity(name, position, mesh, physics, movement, health)


@with_undo_batch("Create Enemy")
def create_enemy(
    name: str,
    position: Optional[List[Number]] = None,
    ai_type: str = "chaser",
    mesh: str = "primitive_cube",
    health: float = 50.0,
    speed: float = 3.0,
) -> str:
    """Create an enemy entity with AI behavior.

    Args:
        name: Entity name.
        position: [x, y, z] world position.
        ai_type: AI type ("chaser" or "turret").
        mesh: Mesh asset.
        health: Enemy health.
        speed: Movement speed (for chasers).

    Returns:
        JSON with entity details.
    """
    from .templates.enemy import create_enemy_entity
    return create_enemy_entity(name, position, ai_type, mesh, health, speed)


@with_undo_batch("Create Projectile Spawner")
def create_projectile_spawner(
    name: str,
    parent_entity_id: Optional[int] = None,
    direction: str = "forward",
    speed: float = 20.0,
    damage: float = 10.0,
) -> str:
    """Create a projectile spawner entity.

    Returns JSON with entity details.
    """
    from .templates.projectile import create_projectile_spawner_entity
    return create_projectile_spawner_entity(name, parent_entity_id, direction, speed, damage)


@with_undo_batch("Create Pickup")
def create_pickup(
    name: str,
    position: Optional[List[Number]] = None,
    pickup_type: str = "health",
    value: float = 25.0,
) -> str:
    """Create a pickup/collectible entity.

    Returns JSON with entity details.
    """
    from .templates.pickup import create_pickup_entity
    return create_pickup_entity(name, position, pickup_type, value)


@with_undo_batch("Create Trigger Zone")
def create_trigger_zone(
    name: str,
    position: Optional[List[Number]] = None,
    size: Optional[List[Number]] = None,
    script_path: Optional[str] = None,
) -> str:
    """Create an invisible trigger zone entity.

    Returns JSON with entity details.
    """
    from .templates.environment import create_trigger_zone as _create_tz
    return _create_tz(name, position, size, script_path)


# ---------------------------------------------------------------------------
# Batch Operations
# ---------------------------------------------------------------------------

@with_undo_batch("Batch Create Entities")
def create_entity_batch(specs: List[Dict[str, Any]]) -> str:
    """Create multiple entities from a specification list.

    Args:
        specs: List of entity specifications. Each spec is a dict with:
            - name (str): Entity name (required)
            - position (list): [x, y, z] position
            - type (str): Entity type ("player", "enemy", "pickup", "static", "dynamic")
            - Additional type-specific params (mesh, ai_type, etc.)

    Returns:
        JSON with all created entity details.
    """
    sandbox = get_sandbox()

    results = []
    for spec in specs:
        sandbox.check_timeout()

        entity_type = spec.get("type", "static")
        name = spec.get("name", f"Entity_{len(results)}")
        position = spec.get("position")

        if entity_type == "player":
            result = json.loads(create_player(
                name=name, position=position,
                movement=spec.get("movement"),
                health=spec.get("health", 100),
            ))
        elif entity_type == "enemy":
            result = json.loads(create_enemy(
                name=name, position=position,
                ai_type=spec.get("ai_type", "chaser"),
                speed=spec.get("speed", 3.0),
            ))
        elif entity_type == "pickup":
            result = json.loads(create_pickup(
                name=name, position=position,
                pickup_type=spec.get("pickup_type", "health"),
            ))
        elif entity_type == "dynamic":
            from .builders.physics_builder import create_dynamic_body
            result = json.loads(create_dynamic_body(
                name=name, position=position or [0, 0, 0],
                mass=spec.get("mass", 1.0),
            ))
        else:
            from .builders.physics_builder import create_static_body
            result = json.loads(create_static_body(
                name=name, position=position or [0, 0, 0],
            ))

        results.append(result.get("data", result))

    return batch_result(results, len(results))


@with_undo_batch("Create Grid")
def create_grid(
    name_prefix: str,
    rows: int,
    cols: int,
    spacing: float = 2.0,
    template: str = "primitive_cube",
) -> str:
    """Create a grid of entities.

    Args:
        name_prefix: Prefix for entity names (e.g., "Block" -> "Block_0_0", "Block_0_1").
        rows: Number of rows.
        cols: Number of columns.
        spacing: Distance between entities.
        template: Mesh asset for each entity.

    Returns:
        JSON with all created entity details.
    """
    valid, err = validate_entity_name(name_prefix)
    if not valid:
        return error(err)

    sandbox = get_sandbox()
    sandbox.check_entity_limit(rows * cols)

    from .builders.entity_builder import EntityBuilder

    results = []
    offset_x = -(cols - 1) * spacing / 2.0
    offset_y = -(rows - 1) * spacing / 2.0

    for r in range(rows):
        for c in range(cols):
            sandbox.check_timeout()
            name = f"{name_prefix}_{r}_{c}"
            x = offset_x + c * spacing
            y = offset_y + r * spacing

            result = json.loads(
                EntityBuilder(name)
                .at_position(x, y, 0)
                .with_mesh(template)
                .with_physics(body_type="static")
                .with_collider(shape="box")
                .build()
            )
            results.append(result.get("data", result))

    return batch_result(results, len(results))


# ---------------------------------------------------------------------------
# Fluent Builder Access
# ---------------------------------------------------------------------------

def build_entity(name: str):
    """Start building an entity with the fluent EntityBuilder.

    Usage:
        result = (build_entity("MyEntity")
            .at_position(0, 0, 1)
            .with_mesh("primitive_cube")
            .build())
    """
    from .builders.entity_builder import EntityBuilder
    return EntityBuilder(name)


# ---------------------------------------------------------------------------
# Lighting / Physics / Camera
# ---------------------------------------------------------------------------

@with_undo_batch("Setup Lighting")
def setup_lighting(preset: str = "three_point") -> str:
    """Set up a lighting rig from a preset.

    Args:
        preset: "three_point", "outdoor", or "indoor".

    Returns:
        JSON with light entity details.
    """
    from .builders.lighting_builder import create_lighting
    return create_lighting(preset)


@with_undo_batch("Create Camera")
def create_camera(
    name: str = "MainCamera",
    camera_type: str = "perspective",
    follow_target: Optional[str] = None,
    offset: Optional[List[Number]] = None,
) -> str:
    """Create a camera entity.

    Args:
        name: Camera entity name.
        camera_type: "top_down", "isometric", "perspective", "side_view".
        follow_target: Entity name to follow (future feature).
        offset: Custom position offset [x, y, z].

    Returns:
        JSON with entity details.
    """
    from .templates.camera import create_camera_entity
    return create_camera_entity(name, camera_type, follow_target, offset)


@with_undo_batch("Create Static Body")
def create_static_body(
    name: str,
    position: List[Number],
    mesh: str = "primitive_cube",
    collider_shape: str = "box",
) -> str:
    """Create a static physics body. Returns JSON with entity details."""
    from .builders.physics_builder import create_static_body as _csb
    return _csb(name, position, mesh, collider_shape)


@with_undo_batch("Create Dynamic Body")
def create_dynamic_body(
    name: str,
    position: List[Number],
    mesh: str = "primitive_cube",
    mass: float = 1.0,
    collider_shape: str = "box",
) -> str:
    """Create a dynamic physics body. Returns JSON with entity details."""
    from .builders.physics_builder import create_dynamic_body as _cdb
    return _cdb(name, position, mesh, mass, collider_shape)


@with_undo_batch("Create Physics Ground")
def create_physics_ground(size: float = 50.0) -> str:
    """Create a ground plane with physics collision. Returns JSON."""
    from .builders.terrain_builder import create_ground
    return create_ground(size)


# ---------------------------------------------------------------------------
# Scene Feedback
# ---------------------------------------------------------------------------

def get_scene_snapshot() -> str:
    """Get a full snapshot of the current scene as JSON."""
    from .feedback.scene_snapshot import get_scene_snapshot as _gss
    return _gss()


def get_entity_tree() -> str:
    """Get the entity hierarchy tree as JSON."""
    from .feedback.scene_snapshot import get_entity_tree as _get
    return _get()


def inspect_entity(entity_id: int) -> str:
    """Get detailed information about a specific entity."""
    from .feedback.entity_inspector import inspect_entity as _ie
    return _ie(entity_id)


def validate_scene() -> str:
    """Validate the current scene for common issues."""
    from .feedback.validation_report import validate_scene as _vs
    return _vs()


# ---------------------------------------------------------------------------
# Prefab Operations
# ---------------------------------------------------------------------------

def list_prefabs() -> str:
    """List all available AiCompanion prefabs."""
    prefabs = [
        {"name": "Player_TwinStick", "description": "Player with twin-stick movement", "category": "Player"},
        {"name": "Enemy_Chaser", "description": "Enemy that chases the player", "category": "Enemy"},
        {"name": "Enemy_Turret", "description": "Stationary turret enemy", "category": "Enemy"},
        {"name": "Projectile_Basic", "description": "Basic projectile with damage", "category": "Projectile"},
        {"name": "Pickup_Health", "description": "Health pickup", "category": "Pickup"},
        {"name": "Pickup_Ammo", "description": "Ammo pickup", "category": "Pickup"},
        {"name": "Environment_Ground", "description": "Ground plane with collision", "category": "Environment"},
        {"name": "Lighting_ThreePoint", "description": "Three-point lighting rig", "category": "Lighting"},
        {"name": "Camera_TopDown", "description": "Top-down camera", "category": "Camera"},
    ]
    return success(prefabs)


@with_undo_batch("Spawn Prefab")
def spawn_prefab(
    prefab_name: str,
    position: Optional[List[Number]] = None,
) -> str:
    """Instantiate an AiCompanion prefab at the given position.

    Args:
        prefab_name: Name of the prefab (e.g., "Player_TwinStick").
        position: [x, y, z] world position. Defaults to [0, 0, 0].

    Returns:
        JSON with spawned entity details.
    """
    pos = position or [0, 0, 0]

    try:
        import azlmbr.prefab as prefab_api
        import azlmbr.bus as bus
        from .utils.transform_helpers import to_vector3

        prefab_path = f"Prefabs/{prefab_name}.prefab"
        vec = to_vector3(pos)

        result = prefab_api.PrefabPublicRequestBus(
            bus.Broadcast, "InstantiatePrefab", prefab_path, None, vec
        )

        return success({
            "prefab": prefab_name,
            "position": pos,
            "spawned": True,
        })

    except ImportError:
        return success({
            "prefab": prefab_name,
            "position": pos,
            "spawned": False,
            "note": "Not running inside O3DE Editor",
        })
