# AI Agent Best Practices: Token Efficiency and Performance

This guide helps AI agents and developers use the AiCompanion API efficiently.
Structured for both human readability and machine consumption.

## Quick Reference

| Goal | Recommended | Avoid | Why |
|------|-------------|-------|-----|
| Scene setup | `bootstrap_scene()` | Manual ground + lighting + camera | 1 call vs 3+ |
| Multiple entities | `create_entity_batch()` | Loop of `create_player()`/`create_enemy()` | 1 call vs N |
| Grid layouts | `create_grid()` | Nested loops with individual creates | 1 call vs N×M |
| Standard entities | `spawn_prefab("Player_TwinStick")` | `build_entity()` chain | Pre-configured |
| Scene inspection | `get_entity_tree()` | `get_scene_snapshot()` | Smaller response |
| Single entity check | `inspect_entity(id)` | `get_scene_snapshot()` | Targeted, less JSON |
| Health check | `ping` request type | `execute_python("print('ok')")` | No Python overhead |
| Capability check | `get_api_version` request type | Query each function individually | All-in-one |

---

## Token Efficiency

### Batch operations over individual calls

**Do:**
```python
create_entity_batch([
    {"name": "Enemy1", "type": "enemy", "position": [10, 0, 1], "ai_type": "chaser"},
    {"name": "Enemy2", "type": "enemy", "position": [-10, 0, 1], "ai_type": "chaser"},
    {"name": "Enemy3", "type": "enemy", "position": [0, 10, 1], "ai_type": "turret"},
])
```

**Don't:**
```python
create_enemy("Enemy1", position=[10, 0, 1], ai_type="chaser")
create_enemy("Enemy2", position=[-10, 0, 1], ai_type="chaser")
create_enemy("Enemy3", position=[0, 10, 1], ai_type="turret")
```

Each call requires a round-trip. Batching reduces network overhead and token cost.

### Use high-level templates

**Do:**
```python
create_player("Player", position=[0, 0, 1], movement="twin_stick")
```

**Don't:**
```python
build_entity("Player") \
    .at_position(0, 0, 1) \
    .with_mesh("primitive_capsule") \
    .with_physics(body_type="dynamic") \
    .with_collider(shape="capsule") \
    .with_lua_script("Scripts/Lua/twin_stick_movement.lua") \
    .build()
```

Templates encode best practices. Use `build_entity()` only when you need non-standard configurations.

### Use bootstrap functions for scenes

**Do:**
```python
bootstrap_scene(ground_size=50, lighting="three_point", camera="top_down")
```

**Don't:**
```python
create_physics_ground(size=50)
setup_lighting("three_point")
create_camera("MainCam", camera_type="top_down")
```

`bootstrap_scene()` combines all three into one call.

### Minimize scene snapshot calls

`get_scene_snapshot()` returns a full JSON dump of every entity. Use targeted alternatives:

| Need | Use | Response size |
|------|-----|--------------|
| Full scene state | `get_scene_snapshot()` | Large (all entities) |
| Hierarchy only | `get_entity_tree()` | Medium (names + parent/child) |
| One entity | `inspect_entity(id)` | Small (single entity) |
| Validation | `validate_scene()` | Small (issues only) |

### Cache discovery calls

Call these once at session start, not per operation:
- `get_component_catalog()`: returns all available component types
- `get_available_functions()`: returns all API functions with signatures

---

## Performance

### Combine operations in a single script

When using `execute_python`, combine related operations into one script:

**Do:**
```python
# One execute_python call with all operations
from ai_companion.api import *
begin_undo_batch("Setup enemies")
create_enemy("E1", position=[10, 0, 1], ai_type="chaser")
create_enemy("E2", position=[-10, 0, 1], ai_type="chaser")
create_enemy("E3", position=[0, 10, 1], ai_type="turret")
end_undo_batch()
```

**Don't:**
```python
# Three separate execute_python calls
# Call 1:
create_enemy("E1", position=[10, 0, 1], ai_type="chaser")
# Call 2:
create_enemy("E2", position=[-10, 0, 1], ai_type="chaser")
# Call 3:
create_enemy("E3", position=[0, 10, 1], ai_type="turret")
```

Each `execute_python` request incurs TCP round-trip + TickBus dispatch latency.

### Use explicit undo batching

**Do:**
```python
begin_undo_batch("Build arena")
# ... multiple operations ...
end_undo_batch()
```

This creates one undo entry. Without explicit batching, each API call creates its own undo batch via `@with_undo_batch`, which adds overhead.

### Prefer C++ EBus paths

These functions use fast C++ entity traversal (no Python overhead) and return
the C++ JSON verbatim, with decimal-string ids and no `status`/`data` envelope:
- `get_scene_snapshot()` calls `SceneSnapshotProvider::CaptureSnapshot()`
- `get_entity_tree()` calls `SceneSnapshotProvider::CaptureEntityTree()`
- `validate_scene()` calls `SceneSnapshotProvider::ValidateScene()`
- `inspect_entity(id)` has a C++ counterpart in the `get_entity` request type, `SceneSnapshotProvider::CaptureEntity()`

The AgentServer serves them as direct request types, bypassing Python entirely,
alongside the rest of its native set: `get_entity`, `get_bus_schema` (live EBus
discovery), the validated mutations `create_entity`, `set_transform` and
`delete_entity` (each its own undo batch; missing entities, the level root and
the protected system entities refused), the asset readiness queries
`get_asset_status`, `get_asset_jobs` and `get_asset_processor_status`, the anim graph reads `list_anim_graphs`
and `get_anim_graph`, and the anim graph writes `create_anim_graph`,
`remove_anim_graph`, `load_anim_graph`, `save_anim_graph`,
`add_anim_graph_node`, `remove_anim_graph_node`, `set_anim_graph_entry_state`,
`add_anim_graph_parameter`, `remove_anim_graph_parameter`,
`add_anim_graph_transition`, `remove_anim_graph_transition`,
`set_anim_graph_transition`, `connect_anim_graph_ports`,
`disconnect_anim_graph_ports` and `set_anim_graph_node`. Every one of them
works in secure mode.

### TLS performance implications

When TLS is enabled on the AgentServer:
- **Connection handshake**: ~1-2ms additional latency (one-time cost per connection, amortized by connection reuse)
- **Data transfer overhead**: ~5-15% CPU overhead for encryption/decryption
- **For typical AI workflows** (small JSON messages): overhead is negligible
- **Recommendation**: Use TLS only for non-localhost connections. On localhost, plaintext is safe and faster.

---

## Protocol Tips

### Connection health

Use the `ping` request type for connection checks: it is handled directly on the server's network thread without touching Python or the main thread:

```json
{"id": "check-1", "type": "ping"}
→ {"id": "check-1", "status": "ok", "output": "pong", ...}
```

### Capability discovery

Use `get_api_version` to discover server capabilities in one call:

```json
{"id": "init-1", "type": "get_api_version"}
→ {"id": "init-1", "status": "ok", "output": "{\"protocol_version\": 1, \"gem_version\": \"0.5.0\", ...}", ...}
```

The response includes `secure_mode` and `tls_enabled` flags so agents can adapt.

### Script self-containment, or a session

Each `execute_python` request (o3de-mcp's `run_editor_python`) runs in a fresh
`exec()` context. Always import what you need:

```python
from ai_companion.api import bootstrap_scene, create_player, get_scene_snapshot
bootstrap_scene()
create_player("Player", position=[0, 0, 1])
print(get_scene_snapshot())
```

For a multi-step build, open one o3de-mcp session instead: `begin_session`,
then `exec_in_session` per step, then `end_session`. Imports and variables
persist across steps, each step is its own request so the editor's main thread
drains between them, and a failed step leaves the earlier ones inspectable.
(Creating several entities in one call used to leave every entity but the last
bare; the builder now commits each entity to the level template as it goes, so
`bootstrap_scene`, `create_entity_batch` and `create_grid` are safe in a single
request.)

For read-only checks that need no Python at all, o3de-mcp's `get_scene_snapshot`,
`get_entity_tree`, `get_entity` and `validate_scene` tools call the gem's C++
request types directly. They are the cheapest way to look at the scene, and the
only way when the AgentServer runs in secure mode, where the native
`create_entity`, `set_transform` and `delete_entity` request types (o3de-mcp's
tools of the same names try them first) are the only way to change it, and the
anim graph types behind o3de-mcp's animation tools the only way to read and
author an anim graph.

### Wait for an asset you just wrote

Writing a source file (an anim graph, a prefab, a texture) starts an Asset
Processor job, and the product is not usable until that job completes. Poll
the native `get_asset_status` request type instead of sleeping, and never ask
the engine to compile synchronously: the gem does not expose
`CompileAssetSync`, which would hold the editor's main thread for the whole
build.

1. Right after the write, call `get_asset_status` once with `flush_io: true`.
   The Asset Processor flushes its file change queue before answering, so the
   file is seen even if the OS has not finished writing it. `path` is the
   source path relative to its scan folder (`Assets/MyGraphs/Walk.animgraph`),
   the product path (`assets/mygraphs/walk.animgraph`) or a full path.
2. Poll `get_asset_status` without `flush_io` about once a second until
   `status` is `compiled` or `failed`. `queued` and `compiling` mean the Asset
   Processor has the file. Asking also moves the asset up the build queue.
   Observed on O3DE 26.10.0: a source whose job failed keeps answering
   `missing` (a failed source has no products), the same word as a file the
   Asset Processor has not registered yet, so `missing` that lasts more than a
   few seconds after a write is not "still waiting": go to step 3.
3. On `failed`, or on a lasting `missing`, call `get_asset_jobs` with
   `source_path` and `include_logs: true`: a job with `status` `failed` is the
   build failure, and each failed job carries its `log` (cut at 64 KB) and
   `error_count`. A source the Asset Processor has never seen answers
   `engine_error` here (it has no job record), which tells the two cases
   apart. A full path under the project or engine root is turned into the
   root-relative form before either query (the reply's `query_path` shows
   it); on 26.10.0 the engine answers `missing` for a full source path asked
   as is, although its header lists full paths as accepted.
4. `get_asset_processor_status` says whether the editor is connected at all
   (`connected`, `ping_ms`); the two queries answer `unavailable` without a
   connection.

```json
{"id": "w-1", "type": "get_asset_status", "path": "Assets/MyGraphs/Walk.animgraph", "flush_io": true}
→ {"id": "w-1", "status": "ok", "output": "{\"path\": \"Assets/MyGraphs/Walk.animgraph\", \"status\": \"queued\", \"connected\": true}", ...}
```

o3de-mcp's `wait_for_asset` tool (to follow) runs this loop for you.

### Error handling

Check the `status` field in every response:
- `"ok"`: the operation succeeded, result in `output`
- `"error"`: the operation failed; branch on `code`, show `error`

Every AgentServer error reply carries a `code`: `validation_failed` (fix the
argument), `not_found` (the entity, anim graph or bus is gone; refresh your
view of the scene), `unavailable` (a subsystem such as EMotion FX is not
loaded; do not retry), `engine_error` (the engine's own refusal; `error` is
its text), `secure_mode` (`execute_python` is off; use the native types),
`execution_failed` (your script raised; `error` is the traceback),
`unknown_request_type` (this build lacks the type; fall back to
`execute_python`, the one code to fall back on), `timeout` and
`shutting_down`. A native type never answers `ok` with an error hidden inside
`output`, so there is no need to parse `output` for an `error` key. The
[integration guide](integration-with-o3de-mcp.md#response-format) defines each
code. The `code` inside a Python API function's JSON is the package's own
vocabulary; it overlaps with the server's on `validation_failed`, `not_found`
and `engine_error`.

The `duration_ms` field helps identify slow operations for optimization.
