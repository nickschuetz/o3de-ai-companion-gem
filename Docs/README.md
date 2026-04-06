# AI Companion for O3DE — Documentation

## Overview

AI Companion is an O3DE Gem that provides a high-level Python API for AI-driven
game development. It bridges the gap between raw O3DE APIs and the kinds of
high-level operations that AI agents need to create games efficiently.

## Architecture

```
AI Agent (Claude, etc.)
    |
    v
o3de-mcp (MCP Server)
    |
    v  run_editor_python()
O3DE Editor
    |
    v  import ai_companion
AI Companion Python API
    |
    +-- Builders (entity, scene, lighting, physics, terrain)
    +-- Templates (player, enemy, camera, pickup, projectile)
    +-- Feedback (scene snapshot, entity inspector, validation)
    +-- Safety (validators, sandbox, rollback)
    |
    v  azlmbr / EBus
O3DE Engine (entities, components, physics, rendering)
```

## Installation

### Prerequisites

1. O3DE 2305.0 or later installed
2. A project created with O3DE
3. The following Gem enabled in your project:
   - **EditorPythonBindings** — Provides the `azlmbr` Python API

### Steps

```bash
# Clone the Gem
git clone https://github.com/nickschuetz/o3de-ai-companion-gem.git

# Register with O3DE
o3de register --gem-path /path/to/o3de-ai-companion-gem

# Enable in your project
o3de enable-gem --gem-name AiCompanion --project-path /path/to/project

# Rebuild your project
cmake --build build --target Editor --config profile
```

### Via o3de-mcp

If you're using o3de-mcp, the AI agent can install the Gem itself:

```python
register_gem(gem_path="/path/to/o3de-ai-companion-gem", project_path="/path/to/project")
enable_gem(gem_name="AiCompanion", project_path="/path/to/project")
build_project(project_path="/path/to/project", config="profile")
```

## Quick Start

After installation, launch the O3DE Editor and use the Python console
(or o3de-mcp) to create a game:

```python
from ai_companion.api import *

# Create a complete scene
bootstrap_scene(ground_size=50, lighting="three_point", camera="top_down")

# Add a player
create_player("Player", position=[0, 0, 1], movement="twin_stick")

# Check the result
print(get_scene_snapshot())
```

## Key Concepts

### JSON Responses

Every API function returns a JSON string for reliable parsing:

```json
{
    "status": "ok",
    "data": {
        "entity_id": 12345,
        "name": "Player",
        "component_ids": {"Mesh": 1, "PhysX Collider": 2}
    }
}
```

### Undo/Rollback

Every mutating API call is wrapped in an undo batch. If any operation fails,
all changes are automatically rolled back. You can also manually control undo:

```python
begin_undo_batch("My Custom Operation")
# ... operations ...
end_undo_batch()

# Or roll back the last operation
rollback_last_batch()
```

### Safety

All inputs are validated before reaching O3DE APIs:
- Entity names must match `^[A-Za-z][A-Za-z0-9_-]*$`
- Positions must be finite and within +-10,000
- Script paths cannot contain `..` (path traversal)
- System entities are protected from modification

## Further Reading

- [API Reference](api-reference.md) — Complete function documentation
- [Prefab Catalog](prefab-catalog.md) — Available prefabs
- [Lua Scripts](lua-scripts.md) — Gameplay script documentation
- [Safety Model](safety-model.md) — Security architecture
- [Twin-Stick Example](twin-stick-example.md) — Example walkthrough
- [o3de-mcp Integration](integration-with-o3de-mcp.md) — Using with o3de-mcp
- [Agent Best Practices](agent-best-practices.md) — Token efficiency & performance
