# API Reference

All functions are available from `ai_companion.api`. Every function returns
a JSON string.

## Responses and error codes

Success: `{"status": "ok", "data": ...}`. Failure:

```json
{"status": "error", "code": "validation_failed", "message": "...", "details": {...}, "rolled_back": true}
```

`code` is what to branch on. The general codes:

| Code | Meaning |
|------|---------|
| `validation_failed` | An argument failed the safety validators (name, position, component, prefab name, unknown template type) |
| `limit_exceeded` | A sandbox limit was hit (entity count per call, recursion depth, timeout) |
| `not_in_editor` | The call needs the editor's `azlmbr` and it is not available |
| `not_found` | A named prefab, file or entity does not exist (`prefab_not_found` for `spawn_prefab`) |
| `editor_running` | The operation needs the editor closed first |
| `io_error` | A file could not be read or written |
| `engine_error` | An `azlmbr` call failed or raised |
| `instantiate_failed` | The prefab system returned a failed outcome |

An exception inside a mutating call never reaches the agent as a traceback:
the undo batch is ended, undone, any entity the call created is deleted, and
the error is returned with `rolled_back: true` plus `details.exception` and
`details.operation`. For callers written against 0.4.0, `details.code` mirrors
`code`.

Entity ids in this package's JSON are bracketed strings such as
`"[6524019704300593900]"`, the form `azlmbr` prints; the AgentServer's native
request types return plain decimal strings (see the
[architecture document](architecture.md#network-protocol-agentserver)).

These codes belong to the Python package. The C++ AgentServer's own replies
(`{"id", "status", "output", "error", "duration_ms"}`) carry their own `code`
on every `error` reply: `validation_failed`, `not_found`, `unavailable`,
`engine_error`, `secure_mode`, `execution_failed`, `unknown_request_type`,
`timeout` and `shutting_down`, defined in the
[integration guide](integration-with-o3de-mcp.md#response-format). The two
vocabularies overlap on purpose where they mean the same thing
(`validation_failed`, `not_found`, `engine_error`) and differ where the layers
differ. A Python API error travels inside an `ok` reply's `output`; a server
`code` is on the reply itself. The AgentServer's native request types (scene
reads, the validated mutations, and the anim graph reads and writes) are listed
in the [integration guide](integration-with-o3de-mcp.md#request-types); they
are not part of this Python package.

## Meta

### `get_api_version() -> str`
Returns the current API version.

### `get_available_functions() -> str`
Lists all available API functions with their parameter signatures.

### `get_component_catalog(category=None) -> str`
Lists all known O3DE component types. Optionally filter by category
(e.g., "Physics", "Rendering", "Lighting").

## Scene Bootstrap

Every entity a call creates is committed to the level's prefab template as soon
as it is configured, so calls that create several entities (`bootstrap_scene`,
`create_entity_batch`, `create_grid`, `bootstrap_twin_stick_arena`) keep every
entity's name, transform and components, and a saved level contains them.

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
- `.with_scale(sx, sy?, sz?)`: uniform or non-uniform. A non-uniform scale is applied through the gem's C++ `SetScale` event, which adds the editor's Non-uniform Scale component the way the Transform component's own button does (editor Python cannot add it); on a gem build without the event the largest axis is applied as a uniform scale
- `.with_parent(parent_id)`
- `.with_mesh(mesh_asset)`
- `.with_material(material_path)`
- `.with_physics(body_type, mass)`: `"dynamic"` or `"static"`; `mass` (kilograms) applies to dynamic bodies only
- `.with_collider(shape)`: `"box"`, `"sphere"`, `"capsule"` or `"cylinder"`
- `.with_lua_script(script_path)`
- `.with_script_canvas(graph_path)`
- `.with_component(component_type, **properties)`
- `.build()` — Execute and return JSON

**Physics properties applied on build.** For a dynamic body, `build()` sets
`Configuration|Compute Mass` to false and `Configuration|Mass` to the requested
mass on the PhysX Dynamic Rigid Body, so the value is kept in the saved level
rather than the mass the editor computes from the colliders. For a primitive
collider it sets `Shape Configuration|Shape` to the requested shape; the
shape's dimensions (box size, sphere radius, capsule height and radius) stay at
the engine defaults. Properties passed to `.with_component(**properties)` are
not applied; set them afterwards with `set_component_property`. The result's
`applied_properties` lists what the editor accepted, and `property_warnings`
lists any set the editor refused; a refused set does not fail the build.

## Lighting / Physics / Camera

### `setup_lighting(preset="three_point") -> str`
Creates a lighting rig. Presets: `"three_point"`, `"outdoor"`, `"indoor"`.

### `create_camera(name="MainCamera", camera_type="perspective", follow_target=None, offset=None) -> str`
Creates a camera entity.

### `create_static_body(name, position, mesh="primitive_cube", collider_shape="box") -> str`
Creates a non-moving physics object.

### `create_dynamic_body(name, position, mesh="primitive_cube", mass=1.0, collider_shape="box") -> str`
Creates a physics-simulated object. `mass` (kilograms) and `collider_shape` are
applied to the components as described under the fluent builder.

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

The prefab file is located on disk before the prefab system is asked for it.
On engines built before [o3de/o3de#20099](https://github.com/o3de/o3de/pull/20099)
(merged into `development` on 2026-09-08, so not in the 26.10.0 builds),
`PrefabPublicRequestBus.InstantiatePrefab` crashes the editor when the template
cannot be loaded, and a C++ segfault cannot be caught from Python, so an unknown
name never reaches the bus. Newer engines return a failure on their own; the
guard is kept for the older ones. Error responses carry a `code`:

| Code | Meaning |
|------|---------|
| `validation_failed` | Empty name, or one containing `/`, `\` or `..` (`details.reason` says which), or a bad position |
| `prefab_not_found` | No `Prefabs/<name>.prefab` under the gem `Assets` folder, the project root or the engine root (`details.searched` lists them); the prefab system was not called |
| `instantiate_failed` | The prefab system returned a failed outcome |

On success `data` contains `prefab`, `path`, `position`, `spawned: true` and the
new `entity_id`.

### `find_prefab_file(prefab_name) -> dict`
The lookup behind `spawn_prefab`, without instantiating anything. Returns a
plain dict (not JSON) with `relative_path` (`Prefabs/<name>.prefab`), `found`
(absolute path, or `None`) and `searched` (the roots checked, in order). Useful
for validating a name before a batch, or for locating a gem prefab from code
that will call the prefab bus itself.

## Agent Mode

See [Agent Mode](agent-mode.md) for the full contract (settings-registry keys,
the JSON sidecar, and the observed-state file).

### `set_agent_mode(enabled=True, suppress_dialogs=True) -> str`
Enable or disable runtime dialog suppression for unattended sessions. Writes
the sidecar the editor system component polls; takes effect without a restart.

### `get_agent_mode() -> str`
Current runtime agent-mode state.

### `configure_editor_prefs_for_agent(enabled=True) -> str`
Persistent editor preferences for an agent-driven workflow: welcome dialog off
and auto-load of the last level on. Applies on the next editor start; the
editor must not be running when this is called. `enabled=False` restores them.

### `get_agent_mode_status() -> str`
Snapshot of both the runtime and the persistent state.

## Undo/Rollback

### `begin_undo_batch(label="AI Operation") -> str`
Manually begin an undo batch.

### `end_undo_batch() -> str`
End the current undo batch.

### `rollback_last_batch() -> str`
Undo the last completed batch (one editor Undo step), then delete any entity
that batch created which the undo left behind. Returns
`{"rolled_back": true, "leftover_entities_deleted": n}`.

The second step exists because on O3DE 26.10 an entity that carries a Lua
Script component can survive the undo: undoing an entity creation
re-instantiates the prefab, and if the script asset is already loaded the
editor's `ScriptEditorComponent::LoadScript` opens an undo batch from inside
the undo, which `ToolsApplication` rejects. The gem records every entity it
creates, so rollback finishes the job either way; `leftover_entities_deleted`
tells you whether it had to. The same path runs automatically when a mutating
call raises.

## Further Reading

- [Agent Best Practices](agent-best-practices.md) — Token efficiency, performance, and protocol tips
