# AI Agent Best Practices — Token Efficiency & Performance

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
    {"name": "Enemy1", "position": [10, 0, 1], "ai_type": "chaser"},
    {"name": "Enemy2", "position": [-10, 0, 1], "ai_type": "chaser"},
    {"name": "Enemy3", "position": [0, 10, 1], "ai_type": "turret"},
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
    .with_mesh("capsule") \
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
create_physics_ground("Ground", size=50)
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
- `get_component_catalog()` — returns all available component types
- `get_available_functions()` — returns all API functions with signatures

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

These functions use fast C++ entity traversal (no Python overhead):
- `get_scene_snapshot()` → `SceneSnapshotProvider::CaptureSnapshot()`
- `get_entity_tree()` → `SceneSnapshotProvider::CaptureEntityTree()`
- `validate_scene()` → `SceneSnapshotProvider::ValidateScene()`

They are also available as direct AgentServer request types (`get_scene_snapshot`, `get_entity_tree`, `validate_scene`), bypassing Python entirely.

### TLS performance implications

When TLS is enabled on the AgentServer:
- **Connection handshake**: ~1-2ms additional latency (one-time cost per connection, amortized by connection reuse)
- **Data transfer overhead**: ~5-15% CPU overhead for encryption/decryption
- **For typical AI workflows** (small JSON messages): overhead is negligible
- **Recommendation**: Use TLS only for non-localhost connections. On localhost, plaintext is safe and faster.

---

## Protocol Tips

### Connection health

Use the `ping` request type for connection checks — it's handled directly on the server's network thread without touching Python or the main thread:

```json
{"id": "check-1", "type": "ping"}
→ {"id": "check-1", "status": "ok", "output": "pong", ...}
```

### Capability discovery

Use `get_api_version` to discover server capabilities in one call:

```json
{"id": "init-1", "type": "get_api_version"}
→ {"id": "init-1", "status": "ok", "output": "{\"protocol_version\": 1, \"gem_version\": \"1.1.0\", ...}", ...}
```

The response includes `secure_mode` and `tls_enabled` flags so agents can adapt.

### Script self-containment

Each `execute_python` request runs in a fresh `exec()` context. Always import what you need:

```python
from ai_companion.api import bootstrap_scene, create_player, get_scene_snapshot
bootstrap_scene()
create_player("Player", position=[0, 0, 1])
print(get_scene_snapshot())
```

### Error handling

Check the `status` field in every response:
- `"ok"` — operation succeeded, result in `output`
- `"error"` — operation failed, details in `error`

The `duration_ms` field helps identify slow operations for optimization.
