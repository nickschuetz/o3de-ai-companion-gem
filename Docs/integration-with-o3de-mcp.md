# Integration with o3de-mcp

How AI Companion works with [o3de-mcp](https://github.com/nickschuetz/o3de-mcp).

## Architecture

```
Claude / AI Agent
       |
       v
  o3de-mcp (MCP Server)
       |
       |  Length-prefixed JSON over TCP (port 4600)
       v
  O3DE Editor (AiCompanion AgentServer + EditorPythonBindings)
       |
       |  import ai_companion
       v
  AI Companion Python API
       |
       +-- Python layer (builders, templates, feedback, safety)
       |
       +-- C++ EBus (SceneSnapshotProvider, InputValidator)
       |
       v
  O3DE Engine (Entity/Component System)
```

## How It Works

1. The AI agent sends a `run_editor_python()` call through o3de-mcp
2. o3de-mcp builds a length-prefixed JSON request with the base64-encoded script and sends it via TCP to the AgentServer (port 4600)
3. The AgentServer dispatches the script to the main thread for execution via `EditorPythonRunnerRequestBus`
4. The script imports `ai_companion.api` and calls high-level functions
5. AI Companion translates these into `azlmbr` API calls (entity creation, etc.)
6. Results are returned as a framed JSON response back through the same connection

## AgentServer Protocol

The AgentServer uses a length-prefixed JSON protocol:

```
[4 bytes: uint32 big-endian length N] [N bytes: UTF-8 JSON body]
```

### Request types

| Type | Purpose | Requires Python |
|------|---------|----------------|
| `execute_python` | Run a Python script (base64-encoded) | Yes |
| `ping` | Connection health check | No |
| `get_api_version` | Protocol and gem version info | No |
| `get_scene_snapshot` | Full scene state as JSON | No (C++ EBus) |
| `get_entity_tree` | Entity hierarchy tree | No (C++ EBus) |
| `validate_scene` | Scene validation | No (C++ EBus) |

o3de-mcp uses `ping` for protocol detection, `get_api_version` inside
`get_capabilities()` to confirm the gem is present, and the three C++ request
types behind its `get_scene_snapshot`, `get_entity_tree` and `validate_scene`
tools. Everything else goes through `execute_python`. The C++ request types
keep working when the AgentServer runs in secure mode, which disables
`execute_python`.

### Response format

```json
{"id": "uuid", "status": "ok|error", "output": "...", "error": "...", "duration_ms": 123}
```

See [Agent Best Practices](agent-best-practices.md) for token efficiency and performance tips.

## Before vs After

### Without AI Companion (raw o3de-mcp)

Creating a player entity requires ~8 separate tool calls:

```
1. create_entity("Player")
2. add_component(entity_id, "Mesh")
3. set_component_property(entity_id, "Mesh", "mesh_asset", "capsule")
4. add_component(entity_id, "PhysX Primitive Collider")
5. set_component_property(entity_id, "PhysX Primitive Collider", "shape", "capsule")
6. add_component(entity_id, "PhysX Dynamic Rigid Body")
7. set_component_property(entity_id, "PhysX Dynamic Rigid Body", "mass", 1.0)
8. add_component(entity_id, "Lua Script")
9. set_component_property(entity_id, "Lua Script", "script", "twin_stick.lua")
```

### With AI Companion (1 call)

```python
run_editor_python('''
from ai_companion.api import create_player
print(create_player("Player", position=[0,0,1], movement="twin_stick"))
''')
```

### Full Game Comparison

| Operation | Raw o3de-mcp | With AI Companion |
|-----------|-------------|-------------------|
| Create player | ~8 calls | 1 call |
| Create arena | ~20 calls | 1 call |
| Create enemy | ~6 calls | 1 call |
| Create pickup | ~5 calls | 1 call |
| Full twin-stick game | ~70 calls | ~10 calls |

## Token Efficiency

AI Companion reduces token usage by:
- Fewer round trips (1 call vs 8+)
- Shorter prompts (function names vs multi-step scripts)
- Structured JSON responses (vs raw text parsing)

## Configuration

### AgentServer Settings

| Setting | Env Var | Settings Registry Key | Default |
|---------|---------|----------------------|---------|
| Enabled | `AI_COMPANION_SERVER_ENABLED` | `/O3DE/AiCompanion/AgentServer/Enabled` | `true` |
| Host | `AI_COMPANION_SERVER_HOST` | `/O3DE/AiCompanion/AgentServer/Host` | `127.0.0.1` |
| Port | `O3DE_EDITOR_PORT` | `/O3DE/AiCompanion/AgentServer/Port` | `4600` |
| Secure Mode | `AI_COMPANION_SECURE_MODE` | `/O3DE/AiCompanion/AgentServer/SecureMode` | `false` |
| TLS Enabled | `AI_COMPANION_TLS_ENABLED` | `/O3DE/AiCompanion/AgentServer/TlsEnabled` | `false` |
| TLS Cert | `AI_COMPANION_TLS_CERT` | `/O3DE/AiCompanion/AgentServer/TlsCertPath` | (none) |
| TLS Key | `AI_COMPANION_TLS_KEY` | `/O3DE/AiCompanion/AgentServer/TlsKeyPath` | (none) |
| Log Level | `AI_COMPANION_LOG_LEVEL` | `/O3DE/AiCompanion/AgentServer/LogLevel` | `minimal` |

**Precedence:** env var > settings registry > default.

### o3de-mcp Client Settings

| Setting | Env Var | Default |
|---------|---------|---------|
| Host | `O3DE_EDITOR_HOST` | `127.0.0.1` |
| Port | `O3DE_EDITOR_PORT` | `4600` |
| TLS Enabled | `O3DE_EDITOR_TLS` | `0` (off) |
| TLS Verify | `O3DE_EDITOR_TLS_VERIFY` | `0` (off when TLS on) |
| TLS CA Cert | `O3DE_EDITOR_TLS_CA` | (none) |
| Connect timeout | `O3DE_EDITOR_CONNECT_TIMEOUT` | `5` seconds |
| Command timeout | `O3DE_EDITOR_TIMEOUT` | `600` seconds (the editor runs each script synchronously) |
| Project path | `O3DE_PROJECT_PATH` | (auto-detected) |
| Viewport capture settle time | `O3DE_CAPTURE_WAIT` | (o3de-mcp default) |

Enabling TLS without `O3DE_EDITOR_TLS_VERIFY=1` encrypts the channel but does
not authenticate the peer.

The Gem automatically registers its Python path when the editor starts.

## o3de-mcp Tool Surface

o3de-mcp exposes 66 tools in five groups: capabilities (1), editor (40),
introspection (3), project (17) and assets (5). AI Companion sits behind the
editor group. Tools that matter most when working with this gem:

- `run_editor_python` runs a script that can `import ai_companion`.
- `begin_session` / `exec_in_session` / `end_session` keep a Python namespace
  alive across calls, so `ai_companion` is imported once per session instead
  of once per request. `Examples/TwinStickShooter/run_batched.py` uses them.
- `get_scene_snapshot`, `get_entity_tree` and `validate_scene` return the
  gem's C++ snapshot and validation output without any editor Python.
- `instantiate_prefab` accepts gem-shipped prefabs such as
  `Prefabs/Player_TwinStick.prefab`; it checks the asset catalog, not only the
  project root, before calling the prefab system.
- `set_transform`, `set_parent`, `assign_asset`, `capture_viewport` and the
  console and CVAR tools cover the low-level operations the builders wrap.

o3de-mcp 0.4.0 or later is what this page describes (native snapshot tools,
gem detection). Install it from PyPI with `pip install o3de-mcp`, or work
against a checkout with `pip install -e .` (or its `src/` on `PYTHONPATH`).
Either way it requires the `mcp` 2.x Python SDK. A stale 1.x install in the same
interpreter makes the server fail at import time, which the MCP client reports
as a closed connection.

## Capability Detection

`get_capabilities()` probes the editor port and, when it answers, sends the
AgentServer's native `get_api_version` request. The response distinguishes a
bare socket from the gem:

```json
"editor": {
  "status": "connected",
  "ai_companion_gem": true,
  "agent_server": {"protocol_version": 1, "gem_version": "0.3.0", "api_version": "1.0"}
}
```

`ai_companion_gem` is `false` (with a hint) when only the legacy RemoteConsole
answered, in which case `import ai_companion` will not work either.

To confirm the Python API from within the editor:

```python
run_editor_python('''
from ai_companion.api import get_api_version
print(get_api_version())
''')
```

## Best Practices

1. **Use batch operations** — `create_entity_batch()` is more efficient than
   individual `create_enemy()` calls for multiple entities

2. **Check before creating** — Use `get_scene_snapshot()` to verify the current
   state before adding entities

3. **Validate after creation** — Use `validate_scene()` to catch common issues

4. **Use undo batches** — Group related operations so they can be rolled back
   together if needed

5. **Prefer templates** — Use `create_player()`, `create_enemy()`, etc. over
   the low-level `build_entity()` for common patterns

6. **Use `ping` for health checks** — Cheaper than executing Python

For comprehensive guidance, see [Agent Best Practices](agent-best-practices.md).
