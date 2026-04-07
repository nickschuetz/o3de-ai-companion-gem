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
            API["api.py<br/>28+ public functions"]

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
            SSP["SceneSnapshotProvider"]
            IV["InputValidator"]
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
    MCP -->|"run_editor_python()"| API
    AgentSrv --> AS
    AS -->|"request queue"| SysComp

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
| **api.py** | Main entry point exposing 28+ public functions for scene setup, entity creation, inspection, and undo |
| **Builders** | Fluent builder classes for constructing entities, scenes, lighting rigs, physics bodies, and terrain |
| **Templates** | Pre-configured factory functions for common entity types (player, enemy, camera, pickup, projectile, environment) |
| **Feedback** | Scene introspection: snapshots, entity inspection, and validation reports |
| **Safety** | Input validation, operation sandboxing, and automatic undo/rollback |

### C++ Native Layer

The C++ layer (`Code/Source/`) provides performance-critical operations and the
network server.

| Component | Purpose |
|-----------|---------|
| **AiCompanionSystemComponent** | EBus handler connecting Python API to C++ scene operations |
| **SceneSnapshotProvider** | Fast entity traversal and JSON serialization of scene state |
| **InputValidator** | C++ counterpart to Python validators for entity names and component types |
| **AgentServer** | TCP listener (default `127.0.0.1:4600`) with length-prefixed JSON protocol, TLS support, and audit logging |

### Gameplay Layer

| Component | Purpose |
|-----------|---------|
| **Lua Scripts** | 7 scripts covering twin-stick movement, enemy chase AI, damage, health pickups, projectile launching, scoring, and game-over detection |
| **Prefabs** | 9 ready-to-use prefabs for rapid prototyping (player, enemies, pickups, projectile, camera, lighting, ground) |

## Data Flow

1. **AI Agent** sends a request (via MCP tool call or TCP JSON message)
2. **Python API** receives the call and delegates to the appropriate builder or template
3. **Safety layer** validates all inputs (names, positions, asset paths) and begins an undo batch
4. **Builders** invoke `azlmbr` (O3DE Python bindings) to create entities and attach components
5. On success the undo batch is committed; on failure it is rolled back automatically
6. A **JSON response** with entity IDs, component IDs, and status is returned to the agent
7. The agent can call **Feedback** functions to inspect the scene and decide its next action

## Network Protocol (AgentServer)

The AgentServer uses a length-prefixed JSON protocol over TCP:

```
[4-byte message length (big-endian)] [JSON body]
```

Supported request types: `ping`, `get_api_version`, `get_scene_snapshot`,
`get_entity_tree`, `validate_scene`.

Security features:
- Optional TLS/SSL encryption
- Configurable audit logging (Minimal, Standard, Verbose)
- Maximum message size: 16 MiB
- `execute_python` disabled in secure mode

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
