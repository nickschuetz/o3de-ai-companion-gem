# API Reference

All functions are available from `ai_companion.api`. Every function returns
a JSON string.

## Meta

### `get_api_version() -> str`
Returns the current API version.

### `get_available_functions() -> str`
Lists all available API functions with their parameter signatures.

### `get_component_catalog(category=None) -> str`
Lists all known O3DE component types. Optionally filter by category
(e.g., "Physics", "Rendering", "Lighting").

## Scene Bootstrap

### `bootstrap_scene(preset="default", ground_size=50, lighting="three_point", camera="perspective") -> str`
Sets up a complete scene with ground plane, lighting rig, and camera.

**Parameters:**
- `preset` — Scene preset (currently `"default"`)
- `ground_size` — Width/depth of ground plane
- `lighting` — Lighting preset: `"three_point"`, `"outdoor"`, `"indoor"`
- `camera` — Camera type: `"top_down"`, `"isometric"`, `"perspective"`, `"side_view"`

### `bootstrap_twin_stick_arena(size=30, wall_height=3) -> str`
Creates an enclosed arena with ground, four walls, and three-point lighting.

## Entity Creation

### `create_player(name="Player", position=None, mesh="primitive_capsule", physics=True, movement=None, health=100) -> str`
Creates a player entity.

**Parameters:**
- `movement` — `"twin_stick"` or `None`

### `create_enemy(name, position=None, ai_type="chaser", mesh="primitive_cube", health=50, speed=3.0) -> str`
Creates an enemy entity with AI behavior.

**Parameters:**
- `ai_type` — `"chaser"` (follows player) or `"turret"` (stationary, fires projectiles)

### `create_projectile_spawner(name, parent_entity_id=None, direction="forward", speed=20, damage=10) -> str`
Creates a projectile spawner, typically attached to a player or turret.

### `create_pickup(name, position=None, pickup_type="health", value=25) -> str`
Creates a collectible pickup.

**Parameters:**
- `pickup_type` — `"health"` or `"ammo"`

### `create_trigger_zone(name, position=None, size=None, script_path=None) -> str`
Creates an invisible trigger zone with optional script.

## Batch Operations

### `create_entity_batch(specs) -> str`
Creates multiple entities from a list of specs.

```python
create_entity_batch([
    {"name": "Enemy1", "type": "enemy", "position": [5, 5, 1], "ai_type": "chaser"},
    {"name": "Enemy2", "type": "enemy", "position": [-5, 5, 1], "ai_type": "turret"},
    {"name": "Crate1", "type": "static", "position": [3, 0, 0.5]},
])
```

### `create_grid(name_prefix, rows, cols, spacing=2.0, template="primitive_cube") -> str`
Creates a grid of entities. Useful for floors, walls, or arrays of objects.

## Fluent Builder

### `build_entity(name) -> EntityBuilder`
Returns an EntityBuilder for chained construction:

```python
build_entity("MyEntity") \
    .at_position(0, 0, 1) \
    .with_mesh("primitive_sphere") \
    .with_physics(body_type="dynamic", mass=2.0) \
    .with_collider(shape="sphere") \
    .with_lua_script("Scripts/Lua/my_script.lua") \
    .build()
```

**EntityBuilder methods:**
- `.at_position(x, y, z)`
- `.with_rotation(rx, ry, rz)` — Euler degrees
- `.with_scale(sx, sy?, sz?)` — Uniform or non-uniform
- `.with_parent(parent_id)`
- `.with_mesh(mesh_asset)`
- `.with_material(material_path)`
- `.with_physics(body_type, mass)`
- `.with_collider(shape)`
- `.with_lua_script(script_path)`
- `.with_script_canvas(graph_path)`
- `.with_component(component_type, **properties)`
- `.build()` — Execute and return JSON

## Lighting / Physics / Camera

### `setup_lighting(preset="three_point") -> str`
Creates a lighting rig. Presets: `"three_point"`, `"outdoor"`, `"indoor"`.

### `create_camera(name="MainCamera", camera_type="perspective", follow_target=None, offset=None) -> str`
Creates a camera entity.

### `create_static_body(name, position, mesh="primitive_cube", collider_shape="box") -> str`
Creates a non-moving physics object.

### `create_dynamic_body(name, position, mesh="primitive_cube", mass=1.0, collider_shape="box") -> str`
Creates a physics-simulated object.

### `create_physics_ground(size=50) -> str`
Creates a ground plane with static physics collision.

## Scene Feedback

### `get_scene_snapshot() -> str`
Returns a JSON snapshot of all entities, their transforms, and component lists.
Uses the C++ EBus for performance, with a Python fallback.

### `get_entity_tree() -> str`
Returns the entity hierarchy as a nested JSON tree.

### `inspect_entity(entity_id) -> str`
Deep inspection of a single entity: all components, properties, and children.

### `validate_scene() -> str`
Checks for common issues: unnamed entities, entities at origin, missing components.

## Prefab Operations

### `list_prefabs() -> str`
Lists all AiCompanion prefabs with descriptions.

### `spawn_prefab(prefab_name, position=None) -> str`
Instantiates a prefab at the given position.

Available prefabs: `Player_TwinStick`, `Enemy_Chaser`, `Enemy_Turret`,
`Projectile_Basic`, `Pickup_Health`, `Pickup_Ammo`, `Environment_Ground`,
`Lighting_ThreePoint`, `Camera_TopDown`.

## Undo/Rollback

### `begin_undo_batch(label="AI Operation") -> str`
Manually begin an undo batch.

### `end_undo_batch() -> str`
End the current undo batch.

### `rollback_last_batch() -> str`
Undo the last completed batch.

## Further Reading

- [Agent Best Practices](agent-best-practices.md) — Token efficiency, performance, and protocol tips
