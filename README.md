# AI Companion for O3DE

An Open 3D Engine (O3DE) Gem that makes game development accessible to AI agents.
Designed to work with [o3de-mcp](https://github.com/nickschuetz/o3de-mcp), the
Model Context Protocol server for O3DE.

## What It Does

AI Companion sits between AI agents and the O3DE Editor, turning high-level
intent into engine operations. Agents connect through
[o3de-mcp](https://github.com/nickschuetz/o3de-mcp) (MCP tool calls) or
directly via the built-in AgentServer (TCP with length-prefixed JSON). Both
paths feed into a Python API layer that exposes 28+ functions for entity
creation, scene setup, lighting, physics, cameras, and scene inspection —
reducing what would otherwise take 70+ raw tool calls to under 10.

Beneath the Python API, a C++ native layer provides fast scene introspection
through EBus. The SceneSnapshotProvider traverses entities at engine speed and
serializes scene state as JSON, while the InputValidator enforces the same
safety rules in compiled code. Read-only queries like scene snapshots, entity
trees, and validation can bypass Python entirely by going straight through the
AgentServer's C++ request handlers.

A safety layer wraps every mutation. Inputs are validated against strict
patterns (entity names, positions, asset paths), operations are sandboxed
with configurable limits, and every change is captured in an undo batch that
rolls back automatically on failure. System entities are protected from
modification, and the AgentServer supports TLS encryption and a secure mode
that disables arbitrary code execution.

See the [architecture document](Docs/architecture.md) for the full system
diagram.

## Quick Start

**1. Clone**

```bash
git clone https://github.com/nickschuetz/o3de-ai-companion-gem.git
```

**2. Register and enable**

Linux / macOS:
```bash
o3de register --gem-path /path/to/o3de-ai-companion-gem
o3de enable-gem --gem-name AiCompanion --project-path /path/to/your/project
o3de enable-gem --gem-name EditorPythonBindings --project-path /path/to/your/project
```

Windows:
```powershell
o3de register --gem-path C:\path\to\o3de-ai-companion-gem
o3de enable-gem --gem-name AiCompanion --project-path C:\path\to\your\project
o3de enable-gem --gem-name EditorPythonBindings --project-path C:\path\to\your\project
```

**3. Build your project**

Linux / macOS:
```bash
cmake -B build -S . -G "Ninja Multi-Config"
cmake --build build --target Editor --config profile
```

Windows (Visual Studio Community 2022):
```powershell
cmake -B build -S . -G "Visual Studio 17 2022"
cmake --build build --target Editor --config profile
```

Windows (Visual Studio Community 2026):
```powershell
cmake -B build -S . -G "Visual Studio 18 2026"
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

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines
covering code style, safety requirements, testing, and AI-assisted contributions.
