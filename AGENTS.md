# AGENTS.md

Instructions for AI agents and LLM-based tools working on this codebase.

## Project Overview

AI Companion is an O3DE Gem that bridges AI agents and the Open 3D Engine Editor.
It provides a Python API, C++ EBus components, Lua gameplay scripts, and prefabs.
See [Docs/architecture.md](Docs/architecture.md) for the full architecture diagram.

## Repository Layout

```
Assets/Scripts/Lua/         Lua gameplay scripts (movement, AI, pickups, etc.)
Assets/Prefabs/             O3DE prefab files (.prefab)
Code/Include/AiCompanion/   Public C++ headers (EBus interfaces)
Code/Source/                 C++ implementation (system components, AgentServer, snapshot, validation)
Code/Source/Tests/           C++ unit tests (AZ::AzTest / Google Test)
Editor/Scripts/ai_companion/ Python API package
  api.py                    Main entry point (32 public functions)
  builders/                 Fluent builder classes (entity, scene, lighting, physics, terrain)
  templates/                Pre-configured entity factories (player, enemy, camera, etc.)
  feedback/                 Scene introspection (snapshot, inspector, validation report)
  safety/                   Input validation, sandboxing, undo/rollback
  utils/                    Component registry, JSON helpers, transform helpers
  version.py                Version constants (__version__, API_VERSION)
Tests/                      Python unit tests (unittest)
Docs/                       Documentation
```

## Versioning

This project is in **alpha** (0.x.y). The current version is defined in five places
that must stay in sync:

- `gem.json` — `"version"` field (gem version)
- `Editor/Scripts/ai_companion/version.py` — `__version__` (gem version) and `API_VERSION`
- `Code/Source/Network/AgentServer.cpp` — `gem_version` and `api_version` string literals
- `Code/Source/Tests/AgentServerTests.cpp` — expected version strings in tests
- `sbom.cdx.json` — the component `version` and its `purl`

When bumping the version, update all five files, move the `[Unreleased]` entries in
CHANGELOG.md under the new version with the date, and add the release link at the
bottom. The live suite's `test_api_version_matches_the_checkout` fails if the C++
literal and `gem.json` disagree.

## License Headers

Every source file must begin with a copyright and SPDX header. Use the format
matching the file type:

**Python / CMake / YAML:**
```
# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT
```

**C++ (.cpp / .h):**
```cpp
/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
```

**Lua:**
```lua
----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------
```

## Coding Conventions

### Python
- Functions and variables: `snake_case`
- Classes: `PascalCase`
- Modules/files: `snake_case.py`
- Type hints on all public function signatures
- All public API functions return JSON strings (via `utils/json_output.py` helpers)
- Mutating operations must be wrapped in undo batches (`@with_undo_batch` or manual)
- User-supplied inputs must pass through `safety/validators.py` before reaching O3DE

### C++
- Follow O3DE/AZ Framework conventions (AZ::Component, EBus, RTTI, SerializeContext)
- Use `AZStd::` containers and strings, not `std::`
- Use RapidJSON (bundled with O3DE) for JSON serialization

### Lua
- Scripts are self-contained gameplay behaviors attached via Script Canvas or Lua Component
- Configurable properties exposed via `Properties = { ... }` table at top of script

### Tests
- Python: `unittest` framework, files named `Tests/test_*.py`
- C++: `AZ::AzTest` (Google Test), files in `Code/Source/Tests/`
- Run Python tests: `python -m pytest Tests/` or `python -m unittest discover Tests`
- Live editor tests (`Tests/live/`, opt-in): `O3DE_LIVE_EDITOR_TEST=1 python -m pytest Tests/live` against a running editor, or `scripts/ci_live_test.sh` to bring one up on Xvfb first

## SBOM

When adding or removing dependencies, update `sbom.cdx.json` (CycloneDX 1.5 format).

## CHANGELOG

This project uses [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format
and [Semantic Versioning](https://semver.org/). Add entries under `[Unreleased]`
for any user-facing changes.

## Key Design Decisions

- **JSON everywhere**: All Python API responses are JSON strings so AI agents can
  reliably parse them. Use `utils/json_output.success()` and `.error()`.
- **Safety first**: Never bypass validators or skip undo batches for mutating ops.
  Protected entities (EditorGlobal, SystemEntity, AZ::*) must never be modified.
- **No third-party Python deps**: The Python package uses only the standard library
  and O3DE's `azlmbr` bindings. Keep it that way.
- **Prefab Version fields**: The `"Version": "1.0.0"` inside `.prefab` files is an
  O3DE prefab format version, not the project version. Do not change it.

## Token Efficiency and Performance

These guidelines apply both to agents consuming the API at runtime and to agents
modifying the codebase. See [Docs/agent-best-practices.md](Docs/agent-best-practices.md)
for the full reference with code examples.

### Prefer high-level calls over low-level composition

| Goal | Use | Instead of |
|------|-----|------------|
| Scene setup | `bootstrap_scene()` | Manual ground + lighting + camera (3+ calls) |
| Standard entity | `create_player()` / `spawn_prefab()` | `build_entity()` chain |
| Multiple entities | `create_entity_batch()` | Loop of individual creates |
| Grid layouts | `create_grid()` | Nested loops |

Reserve `build_entity()` for non-standard configurations only.

### Minimize scene inspection cost

`get_scene_snapshot()` serializes every entity in the scene. Use the narrowest
query that answers the question:

| Need | Function | Cost |
|------|----------|------|
| Full state | `get_scene_snapshot()` | Heavy — all entities, all components |
| Hierarchy only | `get_entity_tree()` | Medium — names and parent/child relationships |
| Single entity | `inspect_entity(id)` | Light — one entity |
| Issue check | `validate_scene()` | Light — problems only |
| Single entity, no Python | `get_entity` AgentServer request type | Light — C++ only, works in secure mode |

### Cache discovery responses

Call these once per session, not per operation:
- `get_api_version` — protocol version, gem version, secure mode, TLS status
- `get_available_functions()` — all API functions with signatures
- `get_component_catalog()` — all available component types

### Batch operations and undo

Wrap related mutations in a single explicit undo batch to reduce overhead. Each
individual API call otherwise creates its own `@with_undo_batch`, adding latency:

```python
begin_undo_batch("Build arena")
# ... multiple creates / modifications ...
end_undo_batch()
```

### Combine operations in a single `execute_python` call

Each `execute_python` request incurs TCP round-trip + TickBus dispatch latency.
Group related operations into one script instead of issuing separate requests.
Each script runs in a fresh `exec()` context, so always include imports.

### Prefer C++ EBus paths for read-only queries

These functions route through fast C++ entity traversal, bypassing Python:
- `get_scene_snapshot()` → `SceneSnapshotProvider::CaptureSnapshot()`
- `get_entity_tree()` → `SceneSnapshotProvider::CaptureEntityTree()`
- `validate_scene()` → `SceneSnapshotProvider::ValidateScene()`

They are also available as direct AgentServer request types (`get_scene_snapshot`,
`get_entity_tree`, `validate_scene`), skipping `execute_python` entirely, alongside
`get_entity` (one entity by id), `get_bus_schema` (live EBus reflection) and the
validated mutation set `create_entity`, `set_transform`, `delete_entity`, which work
in secure mode and each run in their own undo batch.

### Use `ping` for health checks

The `ping` request type is handled on the server network thread with zero Python
or main-thread overhead. Never use `execute_python("print('ok')")` for liveness.

### TLS considerations

TLS adds ~1-2ms handshake latency (amortized by connection reuse) and ~5-15% CPU
overhead for encryption. Use TLS only for non-localhost connections; on localhost,
plaintext is safe and faster.

### Response conventions

All API responses are JSON. Check the `status` field:
- `"ok"` — succeeded, result in `data`
- `"error"` — failed; branch on `code` (`validation_failed`, `limit_exceeded`,
  `not_in_editor`, `not_found`, `editor_running`, `io_error`, `engine_error`,
  `instantiate_failed`, `prefab_not_found`), read `message` for the reason, and
  `rolled_back: true` means the batch was undone and anything it created deleted

The `duration_ms` field is present on AgentServer responses and useful for
identifying slow operations during profiling.

## Common Tasks

**Add a new Python API function:**
1. Implement in the appropriate subpackage (builders/, templates/, feedback/)
2. Expose it in `api.py` with input validation and undo-batch wrapping
3. Add a test in `Tests/test_*.py`
4. Document in `Docs/api-reference.md`

**Add a new Lua script:**
1. Create in `Assets/Scripts/Lua/` with the license header
2. Document configurable Properties at the top of the script
3. Add to `Docs/lua-scripts.md`

**Add a new prefab:**
1. Create in `Assets/Prefabs/`
2. Document in `Docs/prefab-catalog.md`

**Add a C++ component or EBus:**
1. Public header in `Code/Include/AiCompanion/`
2. Implementation in `Code/Source/`
3. Register in the appropriate cmake file and system component
4. Add tests in `Code/Source/Tests/`
