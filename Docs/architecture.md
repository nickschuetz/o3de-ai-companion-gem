# Architecture

This document describes the high-level architecture of the AI Companion Gem for
Open 3D Engine (O3DE).

## Overview

AI Companion is an O3DE Gem that provides a bridge between AI agents and the O3DE
Editor. It exposes a high-level Python API that AI agents invoke (typically via
[o3de-mcp](https://github.com/nickschuetz/o3de-mcp)) to create entities, set up
scenes, and inspect the running editor state. A C++ layer provides fast scene
introspection via EBus and a TCP-based AgentServer for direct agent communication.

## Architecture Diagram

```mermaid
flowchart TB
    Agent["AI Agent<br/>(Claude, etc.)"]
    MCP["o3de-mcp<br/>MCP Server"]
    AgentSrv["AgentServer<br/>TCP / JSON"]

    subgraph O3DE["O3DE Editor"]
        direction TB

        subgraph PythonAPI["Python API Layer  (Editor/Scripts/ai_companion/)"]
            API["api.py<br/>32 public functions"]

            subgraph Builders["Builders"]
                EB["EntityBuilder"]
                SB["SceneBuilder"]
                LB["LightingBuilder"]
                PB["PhysicsBuilder"]
                TB_["TerrainBuilder"]
            end

            subgraph Templates["Templates"]
                TP["player / enemy / camera<br/>pickup / projectile<br/>environment"]
            end

            subgraph Feedback["Feedback"]
                SS["SceneSnapshot"]
                EI["EntityInspector"]
                VR["ValidationReport"]
            end

            subgraph Safety["Safety"]
                VAL["Validators"]
                SBX["Sandbox"]
                RB["Rollback / Undo"]
            end
        end

        subgraph CppLayer["C++ Native Layer  (Code/Source/)"]
            SysComp["AiCompanionSystemComponent<br/>EBus Handler"]
            EdComp["AiCompanionEditorSystemComponent<br/>native mutations, CommitEntityToPrefab,<br/>GetBusSchema, Agent Mode"]
            SSP["SceneSnapshotProvider"]
            IV["InputValidator"]
            RP["RequestParsing"]
            AS["AgentServer<br/>TCP Listener"]
        end

        subgraph GameplayLayer["Gameplay Layer  (Assets/)"]
            Lua["Lua Scripts (7)<br/>movement, AI, pickups,<br/>projectiles, scoring"]
            Prefabs["Prefabs (9)<br/>player, enemies, pickups,<br/>camera, lighting, ground"]
        end

        Engine["O3DE Engine<br/>Entities / Components / EBus"]
    end

    Agent -->|"MCP tool calls"| MCP
    Agent -->|"TCP JSON"| AgentSrv
    MCP -->|"run_editor_python() / sessions"| API
    MCP -->|"native requests:<br/>get_api_version, get_scene_snapshot,<br/>get_entity_tree, get_entity, validate_scene,<br/>get_bus_schema, create_entity, set_transform, delete_entity"| AgentSrv
    AgentSrv --> AS
    AS --> RP
    AS -->|"request queue"| SysComp
    AS -->|"mutations, bus schema"| EdComp
    EdComp -->|"validated, own undo batch"| Engine
    Builders -->|"CommitEntityToPrefab"| EdComp

    API --> Builders
    API --> Templates
    API --> Feedback
    API --> Safety

    Builders -->|"azlmbr"| Engine
    Templates --> Builders
    Feedback --> SSP
    Safety -->|"validates"| Builders
    Safety -->|"undo batches"| Engine

    SysComp --> SSP
    SysComp --> IV
    SSP -->|"entity traversal"| Engine

    Lua -->|"script components"| Engine
    Prefabs -->|"instantiate"| Engine

    style O3DE fill:#1a1a2e,stroke:#e94560,color:#eee
    style PythonAPI fill:#16213e,stroke:#0f3460,color:#eee
    style CppLayer fill:#1a1a2e,stroke:#e94560,color:#eee
    style GameplayLayer fill:#1a1a2e,stroke:#533483,color:#eee
    style Agent fill:#e94560,stroke:#e94560,color:#fff
    style MCP fill:#0f3460,stroke:#0f3460,color:#fff
    style AgentSrv fill:#0f3460,stroke:#0f3460,color:#fff
```

## Component Descriptions

### Python API Layer

The Python API (`Editor/Scripts/ai_companion/`) is the primary interface for AI
agents. All functions return JSON strings for reliable parsing.

| Component | Purpose |
|-----------|---------|
| **api.py** | Main entry point exposing 32 public functions for scene setup, entity creation, inspection, and undo |
| **Builders** | Fluent builder classes for constructing entities, scenes, lighting rigs, physics bodies, and terrain |
| **Templates** | Pre-configured factory functions for common entity types (player, enemy, camera, pickup, projectile, environment) |
| **Feedback** | Scene introspection: snapshots, entity inspection, and validation reports |
| **Safety** | Input validation, operation sandboxing, and automatic undo/rollback |

### C++ Native Layer

The C++ layer (`Code/Source/`) provides performance-critical operations and the
network server.

| Component | Purpose |
|-----------|---------|
| **AiCompanionSystemComponent** | EBus handler connecting Python API to C++ scene operations (`AiCompanionRequestBus`, exported to editor Python as `azlmbr.ai_companion`) |
| **AiCompanionEditorSystemComponent** | Editor-side handler (`AiCompanionEditorRequestBus`): the validated native mutations (`CreateEntity`, `SetTransform`, `DeleteEntity`, each in its own undo batch), `CommitEntityToPrefab` (records an entity in the level template immediately, so multi-entity calls persist), `GetBusSchema`, the `SetComponentPropertyUnwrapped` workaround, and Agent Mode's dialog filter |
| **SceneSnapshotProvider** | Fast entity traversal and JSON serialization of scene state, whole scene or one entity |
| **InputValidator** | C++ counterpart to Python validators for entity names, positions and component types |
| **RequestParsing** | Parses request arguments (entity ids as numbers or strings, Vector3 arrays within the position bound) for the native request types |
| **BusSchema** | Builds a JSON description of any reflected EBus from the live BehaviorContext, with argument names and tooltips |
| **AgentServer** | TCP listener (default `127.0.0.1:4600`) with length-prefixed JSON protocol, TLS support, secure mode, and audit logging |

### Gameplay Layer

| Component | Purpose |
|-----------|---------|
| **Lua Scripts** | 7 scripts covering twin-stick movement, enemy chase AI, damage, health pickups, projectile launching, scoring, and game-over detection |
| **Prefabs** | 9 ready-to-use prefabs for rapid prototyping (player, enemies, pickups, projectile, camera, lighting, ground) |

## Data Flow

1. **AI Agent** sends a request (via MCP tool call or TCP JSON message)
2. **Python API** receives the call and delegates to the appropriate builder or template
3. **Safety layer** validates all inputs (names, positions, asset paths) and begins an undo batch
4. **Builders** invoke `azlmbr` (O3DE Python bindings) to create entities and attach components, then commit each entity to the level's prefab template so the next creation cannot wipe it
5. On success the undo batch is committed; on failure it is rolled back automatically (an editor Undo plus deletion of any entity the undo leaves behind) and the error is returned as JSON with a `code`
6. A **JSON response** with entity IDs, component IDs, and status is returned to the agent
7. The agent can call **Feedback** functions to inspect the scene and decide its next action

## Network Protocol (AgentServer)

The AgentServer uses a length-prefixed JSON protocol over TCP:

```
[4-byte message length (big-endian)] [JSON body]
```

Supported request types: `ping`, `get_api_version`, `get_scene_snapshot`,
`get_entity_tree`, `get_entity`, `validate_scene`, `get_bus_schema`,
`create_entity`, `set_transform`, `delete_entity`, `execute_python`.

`get_entity` takes `entity_id` (decimal, as a number or string) and returns one
entity's transform, parent and component list; `get_bus_schema` takes an
optional `bus_name` and returns the reflected EBus description from the live
`BehaviorContext` (every bus name when `bus_name` is empty). Both are served
in C++ with no Python involved.

o3de-mcp uses all of them: `ping` for protocol detection, `get_api_version`
inside its `get_capabilities` tool to confirm the gem is present and report its
versions, the three C++ snapshot types behind its `get_scene_snapshot`,
`get_entity_tree` and `validate_scene` tools, and `execute_python` for
everything else (`run_editor_python` and the `begin_session` /
`exec_in_session` tools).

`create_entity` (`name`, optional `position` and `parent_id`), `set_transform`
(`entity_id` plus any of `position`, `rotation` as Euler degrees, `scale`) and
`delete_entity` (`entity_id`) are the validated mutation set: each runs the
C++ `InputValidator` on its arguments, refuses missing entities and the level
root, and executes inside its own editor undo batch, so an agent on a
secure-mode editor can still build and tidy a scene without Python.

Requests that require main-thread access (every type except `ping` and
`get_api_version`) are dispatched via a lock-free queue from the
client thread to the `AZ::SystemTickBus` handler, which processes them every few
milliseconds regardless of editor focus state. A 30-second timeout prevents
deadlocks if the main thread is blocked.

Security features:
- Optional TLS/SSL encryption (TLS 1.2+, strong cipher suites: `HIGH:!aNULL:!MD5:!RC4`)
- Configurable audit logging (Minimal, Standard, Verbose)
- Maximum message size: 16 MiB
- `execute_python` disabled in secure mode
- Request ID sanitization (alphanumeric, hyphen, underscore) for audit logging
- Injection-proof Python script encoding (hex-escaped byte literals)
- Exclusive temp file creation (`O_EXCL`) with restrictive permissions (`0600`)

Reliability features:
- Socket inheritance prevention (`SOCK_CLOEXEC` / `FD_CLOEXEC` / `SetHandleInformation`)
  ensures only the Editor process owns the listen socket
- Stale connection detection via non-blocking socket probing in the accept loop
- Platform-specific `accept` hardening: `accept4` on Linux, `fcntl` on macOS,
  `SetHandleInformation` on Windows

### Known Limitations

- **No authentication**: The server does not require API keys or tokens. Bind to
  localhost (the default) and rely on OS-level access control.
- **No Python sandbox**: `execute_python` runs arbitrary code with full editor
  privileges. Use secure mode to disable it in untrusted environments.
- **Single client**: Only one TCP connection is accepted at a time.

## Safety Architecture

```
Input arrives
  --> Validators (name format, position bounds, path traversal checks)
    --> Sandbox (operation rate/count limits)
      --> Undo batch opened
        --> O3DE Engine mutation
      --> Undo batch committed (or rolled back on error)
```

Protected entities (`EditorGlobal`, `SystemEntity`, `AZ::SystemEntity`) cannot be
modified. Entity names must match `^[A-Za-z][A-Za-z0-9_-]*$`, positions must be
finite and within +/-10,000 units, and asset paths cannot contain traversal
sequences (`..`) or null bytes.
