# AI Companion for O3DE

An Open 3D Engine (O3DE) Gem that makes game development accessible to AI agents.
Designed to work with [o3de-mcp](https://github.com/nickschuetz/o3de-mcp), the
Model Context Protocol server for O3DE.

## What It Does

AI Companion provides a high-level Python API that reduces complex game creation
tasks from 70+ raw tool calls to under 10. It includes:

- **Python API** for entity creation, scene setup, lighting, physics, and cameras
- **Fluent EntityBuilder** for composable entity construction
- **7 Lua gameplay scripts** (twin-stick movement, enemy AI, projectiles, pickups, etc.)
- **9 prefabs** (player, enemies, projectiles, pickups, lighting, camera)
- **Scene feedback** via C++ EBus for fast entity traversal and validation
- **AgentServer** — Built-in TCP listener with length-prefixed JSON protocol for AI agent communication
- **Safety layer** with input validation, operation sandboxing, and undo/rollback
- **Twin-stick shooter example** demonstrating all capabilities

## Quick Start

```bash
# 1. Clone
git clone https://github.com/nickschuetz/o3de-ai-companion-gem.git

# 2. Register and enable
o3de register --gem-path /path/to/o3de-ai-companion-gem
o3de enable-gem --gem-name AiCompanion --project-path /path/to/your/project

# 3. Ensure required Gem is enabled
o3de enable-gem --gem-name EditorPythonBindings --project-path /path/to/your/project

# 4. Build your project
cmake --build build --target Editor --config profile
```

## Usage

Once installed, the `ai_companion` Python package is available inside the O3DE Editor.
Use it directly or through o3de-mcp's `run_editor_python()`:

```python
from ai_companion.api import (
    bootstrap_scene,
    create_player,
    create_enemy,
    create_camera,
    get_scene_snapshot,
)

# Set up a scene with ground, lighting, and camera
bootstrap_scene(ground_size=50, lighting="three_point", camera="top_down")

# Create game entities
create_player("Player", position=[0, 0, 1], movement="twin_stick")
create_enemy("Enemy1", position=[10, 10, 1], ai_type="chaser", speed=4)
create_camera("MainCamera", camera_type="top_down")

# Inspect the result
print(get_scene_snapshot())
```

## Examples

See the [Examples/TwinStickShooter/](Examples/TwinStickShooter/) directory for a
complete game built with ~10 API calls.

## Documentation

- [Architecture](Docs/architecture.md)
- [Getting Started](Docs/README.md)
- [API Reference](Docs/api-reference.md)
- [Prefab Catalog](Docs/prefab-catalog.md)
- [Lua Scripts](Docs/lua-scripts.md)
- [Safety Model](Docs/safety-model.md)
- [Twin-Stick Example](Docs/twin-stick-example.md)
- [Integration with o3de-mcp](Docs/integration-with-o3de-mcp.md)
- [Agent Best Practices](Docs/agent-best-practices.md)

## Requirements

- O3DE 2305.0 or later
- **EditorPythonBindings** Gem (for Python API access)

## Platform Support

- Linux
- Windows
- macOS

## SBOM

A [CycloneDX Software Bill of Materials](sbom.cdx.json) is maintained for this
project. It documents all runtime and build dependencies. Update it when adding
or removing dependencies.

## License

Licensed under either of:

- [Apache License, Version 2.0](LICENSE-APACHE2.txt)
- [MIT License](LICENSE-MIT.txt)

at your option.

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in this project shall be dual licensed as above, without any
additional terms or conditions.

## Contributing

Contributions are welcome. Please ensure all code includes the appropriate
SPDX license header and follows the existing code style.
