# AI Companion for O3DE

An Open 3D Engine (O3DE) Gem that makes game development accessible to AI agents.
Designed to work with [o3de-mcp](https://github.com/nickschuetz/o3de-mcp), the
Model Context Protocol server for O3DE.

## What It Does

AI Companion sits between AI agents and the O3DE Editor, turning high-level
intent into engine operations. Agents connect through
[o3de-mcp](https://github.com/nickschuetz/o3de-mcp) (MCP tool calls) or
directly via the built-in AgentServer (TCP with length-prefixed JSON). Both
paths feed into a Python API layer that exposes 32 functions for entity
creation, scene setup, lighting, physics, cameras, and scene inspection, so
that a configured entity is one call instead of the eight or so raw
create/add-component/set-property calls it replaces. The builders apply the
physics settings they are given (a rigid body's mass, a collider's shape) and
report what the editor accepted.

Beneath the Python API, a C++ native layer serves requests with no Python
at all. The SceneSnapshotProvider traverses entities at engine speed and
serializes scene state as JSON, while the InputValidator enforces the same
safety rules in compiled code. The AgentServer answers scene snapshots, entity
trees, single entities, validation, live EBus schemas, Asset Processor
readiness (`get_asset_status`, `get_asset_jobs`, `get_asset_processor_status`)
and views of EMotion FX anim graphs (`list_anim_graphs`, `get_anim_graph`)
straight from C++, authors
anim graphs through the Animation Editor's command system (create, load and
save a graph, add and remove nodes and parameters, set the entry state, wire
transitions with conditions and blend tree ports, adjust nodes), and
serves a validated mutation set (`create_entity`, `set_transform`,
`delete_entity`) that runs in its own undo batch, so an agent on a secure-mode
editor, where arbitrary code execution is disabled, can still build and tidy a
scene.

A safety layer wraps every mutation. Inputs are validated against strict
patterns (entity names, positions, asset paths), operations are sandboxed
with configurable limits, and every change is captured in an undo batch that
rolls back automatically on failure, deleting anything the editor's Undo
leaves behind. Errors come back as JSON with a `code` to branch on, never as
a traceback, and every AgentServer error reply carries a code of its own
(`validation_failed`, `not_found`, `unavailable`, `engine_error`,
`secure_mode`, `unknown_request_type` and a few more), so a client can branch
without parsing messages. System
entities are protected from modification, and the AgentServer supports TLS
encryption and a secure mode that disables arbitrary code execution.

See the [architecture document](Docs/architecture.md) for the full system
diagram.

## Quick Start

**1. Get the gem**

Clone it:

```bash
git clone https://github.com/nickschuetz/o3de-ai-companion-gem.git
```

or install a release from the remote gem repository, which needs no clone
(see [Getting Started](Docs/README.md#from-the-remote-gem-repository)):

```bash
o3de register --repo-uri https://raw.githubusercontent.com/nickschuetz/o3de-ai-companion-gem/main
o3de download --gem-name AiCompanion
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

## Verification

| Check | What it proves | How |
|-------|----------------|-----|
| Python unit suite | The package's logic, with `azlmbr` stubbed | `python -m pytest Tests/`, on Linux and Windows in CI |
| C++ unit suite | The AgentServer protocol, validators, snapshot and request parsing | `scripts/ci_build_test.sh` on a self-hosted runner (`ci:build` label) |
| Live editor suite | The package inside a real editor: native request types, templates, rollback, the prefab guard, multi-entity persistence, secure mode | `scripts/ci_live_test.sh` (`LIVE_SECURE=1` for secure mode), `ci:live` label |
| Launcher gameplay check | The Lua scripts running in a GameLauncher: registry, pickup, chase and attack, contact damage, no Lua errors | `scripts/ci_launcher_test.sh`, same label |

See [docs/ci-self-hosted-runner.md](docs/ci-self-hosted-runner.md) for the
runner requirements and what each script does.

## Documentation

- [Architecture](Docs/architecture.md)
- [Getting Started](Docs/README.md)
- [API Reference](Docs/api-reference.md)
- [Prefab Catalog](Docs/prefab-catalog.md)
- [Lua Scripts](Docs/lua-scripts.md)
- [Safety Model](Docs/safety-model.md)
- [Integration with o3de-mcp](Docs/integration-with-o3de-mcp.md)
- [Agent Best Practices](Docs/agent-best-practices.md)

## Requirements

- O3DE 2305.0 or later (also supports the 2.7.0, 24.09, 26.05.0, and 26.10.0 engine versions; the C++ and Python test suites are run against 26.10.0)
- **EditorPythonBindings** Gem (for Python API access)
- **EMotionFX** Gem (declared as a gem dependency; the editor module links it for the anim graph request types)

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
