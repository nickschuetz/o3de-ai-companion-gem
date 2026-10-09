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

1. O3DE 2305.0 or later installed (the 2.7.0, 24.09, 26.05.0, and 26.10.0 engine versions are also supported; 26.10.0 is what the test suites run against)
2. A project created with O3DE
3. The following Gems enabled in your project:
   - **EditorPythonBindings** — Provides the `azlmbr` Python API
   - **EMotionFX**: declared as a dependency in `gem.json`; the editor module links it to serve the `list_anim_graphs` and `get_anim_graph` request types

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

### From the remote gem repository

The repository root carries a `repo.json` in O3DE's remote repository format
(schema 1.0.0), so the `o3de` CLI and the Project Manager can fetch a release
without a clone. Register the repository once, then download and register the
gem by name:

```bash
# Register the remote repository (the CLI appends /repo.json to this URI)
o3de register --repo-uri https://raw.githubusercontent.com/nickschuetz/o3de-ai-companion-gem/main

# Download the latest release listed in repo.json and register it
o3de download --gem-name AiCompanion

# Or pin a version, or take the tagged source tree through git instead of the archive
o3de download --gem-name AiCompanion==0.5.0
o3de download --gem-name AiCompanion --use-source-control

# Enable in your project and rebuild, as above
o3de enable-gem --gem-name AiCompanion --project-path /path/to/project
```

`o3de download` registers the gem itself (pass `--skip-auto-register` to
register later with `o3de register --gem-path`), placing it under the default
gems folder from your `o3de_manifest.json` unless `--dest-path` says otherwise.
The archive is GitHub's source archive for the release tag; it carries no
`sha256`, so the CLI prints its standard warning that the download could not be
hash-verified. Pick `--use-source-control` if you want the tag checked out
through git instead. After a new release, `o3de repo --refresh-repo <uri>`
picks up the updated manifest.

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
        "component_ids": {"Mesh": 1, "PhysX Primitive Collider": 2}
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

Rollback is one editor Undo step followed by deletion of any entity the batch
created that the undo left behind (an O3DE 26.10 quirk with Lua-scripted
entities; see `rollback_last_batch` in the [API reference](api-reference.md)).
A mutating call that raises comes back as a JSON error with `rolled_back: true`,
never as a traceback.

### Safety

All inputs are validated before reaching O3DE APIs:
- Entity names must match `^[A-Za-z][A-Za-z0-9_-]*$`
- Positions must be finite and within +-10,000
- Script paths cannot contain `..` (path traversal)
- System entities are protected from modification

## Verifying an installation

The quickest proof that the gem is wired up is a live run against the editor:

```bash
O3DE_ENGINE_PATH=/path/to/o3de AICOMPANION_PROJECT=/path/to/project \
bash scripts/ci_live_test.sh
```

It starts AssetProcessor, a virtual display and the editor, opens a level, and
runs `Tests/live/`: the package imports and reports its version, the native
request types answer, the templates create entities that `rollback_last_batch`
removes, multi-entity calls keep every entity, and `spawn_prefab` refuses a
missing prefab with the editor still alive. `scripts/ci_launcher_test.sh` goes
one step further and runs the Lua gameplay scripts in your project's
GameLauncher. Both are described in
[docs/ci-self-hosted-runner.md](../docs/ci-self-hosted-runner.md).

## Further Reading

- [API Reference](api-reference.md), complete function documentation
- [Prefab Catalog](prefab-catalog.md), available prefabs
- [Lua Scripts](lua-scripts.md), gameplay script documentation
- [Safety Model](safety-model.md), security architecture
- [Agent Mode](agent-mode.md), suppress human-only UI for unattended sessions
- [o3de-mcp Integration](integration-with-o3de-mcp.md), using with o3de-mcp
- [Agent Best Practices](agent-best-practices.md), token efficiency and performance
