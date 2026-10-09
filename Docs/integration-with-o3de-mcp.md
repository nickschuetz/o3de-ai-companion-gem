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
| `get_entity` | One entity (`entity_id` parameter) | No (C++ EBus) |
| `validate_scene` | Scene validation | No (C++ EBus) |
| `get_bus_schema` | Reflected EBus description (`bus_name` parameter, empty lists all) | No (C++ BehaviorContext) |
| `create_entity` | Create a named entity (`name`, `position?`, `parent_id?`), validated, undoable | No (C++) |
| `set_transform` | Set `position` / `rotation` (Euler degrees) / `scale` on `entity_id`, validated, undoable | No (C++) |
| `delete_entity` | Delete `entity_id` and descendants; refuses the level root, undoable | No (C++) |
| `list_anim_graphs` | Every EMotion FX anim graph the engine holds: id, file name, ownership and dirty flags, node and parameter counts, actor instances | No (C++ EMotion FX) |
| `get_anim_graph` | One anim graph (`anim_graph_id` as number or string, or `file_name`): nodes with ports and connections, transitions with conditions, parameters, node groups | No (C++ EMotion FX) |
| `create_anim_graph` | A new, unsaved editable anim graph: `{"id", "file_name"}` | No (C++ EMotion Studio) |
| `remove_anim_graph` | Remove the editable graph `anim_graph_id` | No (C++ EMotion Studio) |
| `load_anim_graph` | Load `file_name` (absolute, `@alias@`, or project-relative; inside the project or engine root) as an editable graph | No (C++ EMotion Studio) |
| `save_anim_graph` | Save `anim_graph_id` to `file_name` (default: its own; inside the project root); not undoable | No (C++ EMotion Studio) |
| `add_anim_graph_node` | Add a `node_type` node (class or palette name) under `parent_id` (default: the root) with optional `name` and `position`; answers the node as `get_anim_graph` does | No (C++ EMotion Studio) |
| `remove_anim_graph_node` | Remove `node_id` (never the root) | No (C++ EMotion Studio) |
| `set_anim_graph_entry_state` | Make `node_id` its state machine's entry state | No (C++ EMotion Studio) |
| `add_anim_graph_parameter` | Add value parameter `name` of `parameter_type` with optional `default`, `min`, `max`, `description`, `group`; answers the parameter as `get_anim_graph` does | No (C++ EMotion Studio) |
| `remove_anim_graph_parameter` | Remove value parameter `name` | No (C++ EMotion Studio) |
| `add_anim_graph_transition` | Add a transition into state `target_node_id` from `source_node_id` (absent or null: wildcard) with optional `blend_time`, `priority`, `disabled`, `sync_mode`, `interpolation` and `conditions` (`[{condition_type, attributes}]`); answers the transition as `get_anim_graph` does | No (C++ EMotion Studio) |
| `remove_anim_graph_transition` | Remove `transition_id` | No (C++ EMotion Studio) |
| `set_anim_graph_transition` | Set any of `blend_time`, `priority`, `disabled`, `sync_mode`, `interpolation` on `transition_id` | No (C++ EMotion Studio) |
| `connect_anim_graph_ports` | Connect `source_port` of `source_node_id` to `target_port` of `target_node_id` inside a blend tree (a port is an index or a name); answers the input port as `get_anim_graph` does | No (C++ EMotion Studio) |
| `disconnect_anim_graph_ports` | Remove the connection into `target_port` of `target_node_id` | No (C++ EMotion Studio) |
| `set_anim_graph_node` | Set any of `name`, `position`, `enabled`, `attributes` (reflected fields, e.g. a motion node's `motionIds`) on `node_id`; answers the node as `get_anim_graph` does | No (C++ EMotion Studio) |

o3de-mcp uses `ping` for protocol detection, `get_api_version` inside
`get_capabilities()` to confirm the gem is present, and the C++ request types
behind its `get_scene_snapshot`, `get_entity_tree`, `get_entity`,
`validate_scene` and `get_bus_schema_live` tools. Everything else goes through `execute_python`. The C++ request types
keep working when the AgentServer runs in secure mode, which disables
`execute_python`. o3de-mcp wrappers for `list_anim_graphs` and
`get_anim_graph` are to follow; until then a client sends the request types
directly. Both are read-only: `get_anim_graph` answers
`anim graph not found: <selector>` (code `not_found`) for an unknown graph, and
both answer `EMotion FX is not available` (code `unavailable`) when the
EMotionFX gem is not loaded.

The fifteen authoring types run through EMotion Studio's command system, so each
request is one step in the Animation Editor's own undo history, not the
editor's main Undo (and a save is not undoable). The gem validates every
argument before sending a command: names may not contain `"`, `%`, `{` or
`}`; `node_type` must be a creatable AnimGraphNode class and allowed under the
parent (only states inside a state machine, entry and exit nodes only in a
child state machine, a final node only in a blend tree); `parameter_type` must
be one of Float, FloatSlider, FloatSpinner, Int, IntSlider, IntSpinner, Bool,
Tag, String, Vector2, Vector3, Vector3Gizmo, Vector4, Color, Rotation or the
engine class name, with `min` and `max` only for the ranged ones and values
typed per kind (number, integer, bool, string, or an array of 2, 3 or 4
numbers); paths are normalized against the project root. The type fields are
`node_type` and `parameter_type` because `type` is the request envelope's own
field. A graph owned by an asset or runtime instance (one an Anim Graph
component plays) is refused for every write with `validation_failed`; load
the file with `load_anim_graph` to edit a copy, then save it.

The wiring types add their own checks. `add_anim_graph_transition` needs a
target state inside a state machine and a source that is a state of the same
state machine and not an exit node (absent or null: a wildcard transition);
its `conditions` are `{"condition_type", "attributes"}` objects where the type
is ParameterCondition, TimeCondition, PlayTimeCondition, MotionCondition,
StateCondition, TagCondition, Vector2Condition or the engine class name, and
the attributes are the condition's reflected fields (for a parameter
condition `parameterName`, `function` as 0 to 7 or GREATER, GREATEREQUAL,
LESS, LESSEQUAL, NOTEQUAL, EQUAL, INRANGE, NOTINRANGE, `testValue`,
`rangeValue`, `timeRequirement`, `stringFunction`, `testString`; the other
types are listed in `Code/Source/Animation/AnimGraphCommandText.cpp`); node
ids named by a condition must exist and parameter names must name a value
parameter. The field is `condition_type` because `type` is the envelope's
own field. `connect_anim_graph_ports` resolves a port given as a name
(exactly, then case-insensitively) before the engine sees it, needs both nodes
in the same blend tree, compatible port data types, a free input port and no
cycle, and refuses a state as the target with a pointer to
`add_anim_graph_transition`. `set_anim_graph_node` checks a new name is
unique and that each `attributes` key is a reflected field of the node's
class, taking a number, bool or string, or a list of strings for a string
list and for a motion node's `motionIds`; an unknown key answers
`validation_failed` listing the settable fields. Without EMotion
Studio (the Animation Editor's command system) every authoring type answers
`unavailable`; a command the engine refuses answers `engine_error` with the
engine's own text.

### Response format

```json
{"id": "uuid", "status": "ok|error", "output": "...", "error": "...", "code": "...", "duration_ms": 123}
```

`code` is present on every `error` reply and absent from `ok` replies.

Three id forms exist, and o3de-mcp documents the same split: native JSON
carries every 64-bit entity id as a decimal string (`API_VERSION` 0.4.0 and
up; 0.3.0 and lower sent JSON numbers, which a JavaScript parser corrupts above
2^53), the editor-Python fallback sentences print bracketed `[id]`, and every
tool and request field accepts either a number or a string.

Every error reply carries a `code` beside its `error` message, so a client
branches on the code and shows the message. The vocabulary:

| Code | When |
|------|------|
| `validation_failed` | A malformed or refused argument: invalid JSON, a missing `type` or `script` field, a bad base64 script, a missing or unparsable `entity_id` or `anim_graph_id`, an invalid entity name, a position outside the bound, a scale out of range, a refusal to delete the level root, or an anim graph write refused by the gem's own checks (a bad name, type, placement, value or path, or a graph an asset owns) |
| `not_found` | The entity, anim graph or bus does not exist |
| `unavailable` | A subsystem the request needs is not loaded: EMotion FX, EMotion Studio's command system, the gem's editor system component, the prefab system, or the editor's Python runner |
| `engine_error` | The engine refused or failed the operation; `error` is the engine's own text (a prefab system message such as `no root prefab is assigned`, a failed EMotion FX command) |
| `secure_mode` | `execute_python` refused because the server runs in secure mode |
| `execution_failed` | The `execute_python` script raised; `error` holds the traceback and `output` what the script printed first |
| `unknown_request_type` | The request `type` is not one the server serves; the one code a client falls back to editor Python on |
| `timeout` | The editor's main thread did not answer within 30 seconds |
| `shutting_down` | The server was stopping and dropped the request |

A native type never answers `ok` with a failure hidden inside `output`: an
unknown entity in `get_entity` and an unknown bus in `get_bus_schema` are
`error` replies with `not_found`, and a validation refusal from the mutation
or anim graph types is `validation_failed`. o3de-mcp converts any reply whose
status is not `ok` into its own `{"status": "error", "code", "message"}`
envelope and keeps the code, so an agent branches the same way on both sides.
It falls back to `execute_python` on `unknown_request_type` alone.

This vocabulary is the C++ server's own. The `code` field inside the JSON the
Python API functions print to `output` is the Python package's (see the
[API reference](api-reference.md#responses-and-error-codes)); the two overlap on
purpose where they mean the same thing (`validation_failed`, `not_found`,
`engine_error`) and differ where the layers differ (the server has
`unavailable`, `secure_mode`, `execution_failed`, `unknown_request_type`,
`timeout` and `shutting_down`; the package has `limit_exceeded`,
`not_in_editor`, `editor_running`, `io_error`, `instantiate_failed` and
`prefab_not_found`).

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

### Per-Operation Comparison

| Operation | Raw o3de-mcp | With AI Companion |
|-----------|-------------|-------------------|
| Create player | ~8 calls | 1 call |
| Create arena | ~20 calls | 1 call |
| Create enemy | ~6 calls | 1 call |
| Create pickup | ~5 calls | 1 call |

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

o3de-mcp (main, after 0.4.0) exposes 67 tools in five groups: capabilities
(1), editor (41), introspection (3), project (17) and assets (5). AI Companion
sits behind the editor group. Tools that matter most when working with this
gem:

- `run_editor_python` runs a script that can `import ai_companion`.
- `begin_session` / `exec_in_session` / `end_session` keep a Python namespace
  alive across calls, so `ai_companion` is imported once per session instead
  of once per request.
- `get_scene_snapshot`, `get_entity_tree`, `get_entity` and `validate_scene`
  return the gem's C++ snapshot and validation output without any editor
  Python, and `get_bus_schema_live` asks the gem's native `get_bus_schema`
  first. o3de-mcp's `create_entity`, `set_transform` and `delete_entity` try
  the gem's native mutation request types first and fall back to editor Python
  only when the gem answers `unknown_request_type` (nickschuetz/o3de-mcp#18).
  `create_level` creates and opens a level through the engine's six-argument
  `create_level_no_prompt` binding (nickschuetz/o3de-mcp#20; earlier versions
  never created one).
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
  "agent_server": {"protocol_version": 1, "gem_version": "0.5.0", "api_version": "0.3.0"}
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
