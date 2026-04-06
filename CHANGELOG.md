# Changelog

All notable changes to the AI Companion for O3DE project will be documented in this file.

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- Initial GitHub publication

## [1.1.0] - 2026-04-06

### Added

#### AgentServer — Built-in AI Agent Communication
- Purpose-built TCP listener replacing the RemoteConsole gem dependency
- Length-prefixed JSON protocol with 4-byte big-endian header for reliable message framing
- `AgentServerBus` EBus interface for runtime control (StartServer, StopServer, IsRunning, SetSecureMode)
- Safe request types handled directly in C++ without Python: `ping`, `get_api_version`, `get_scene_snapshot`, `get_entity_tree`, `validate_scene`
- `execute_python` request type with base64-encoded scripts and stdout/stderr capture

#### Security
- Secure mode toggle (`AI_COMPANION_SECURE_MODE`) — disables `execute_python`, allows only safe read-only EBus operations
- Optional TLS encryption for non-localhost connections (OpenSSL, TLS 1.2 minimum, self-signed cert support)
- Non-loopback bind address warning with security best practices guidance
- Single-client enforcement to prevent concurrent mutation conflicts
- 16 MiB message size limit to prevent memory exhaustion

#### Configuration
- Server enabled/disabled toggle (default: on) via settings registry or env var
- Configurable bind address (default: localhost) via `AI_COMPANION_SERVER_HOST`
- Configurable port via `O3DE_EDITOR_PORT` (default: 4600)
- TLS cert/key path configuration via env vars or settings registry
- Three-level audit logging (minimal, standard, verbose) via `AI_COMPANION_LOG_LEVEL`
- All settings follow env var > settings registry > default precedence

#### Documentation
- Agent best practices guide (`Docs/agent-best-practices.md`) — token efficiency, performance, and protocol tips for AI agents

### Changed
- Replaced RemoteConsole gem dependency with built-in AgentServer
- Updated all documentation to reflect AgentServer architecture
- Updated `gem.json` requirements (EditorPythonBindings only)

### Removed
- RemoteConsole gem dependency — no longer required

## [1.0.0] - 2026-04-06

### Added

#### Python API (`ai_companion.api`)
- `bootstrap_scene()` — Full scene setup with ground, lighting, and camera
- `bootstrap_twin_stick_arena()` — Enclosed arena with walls and lighting
- `create_player()` — Player entity with optional twin-stick movement
- `create_enemy()` — Enemy entity with chaser or turret AI
- `create_projectile_spawner()` — Projectile spawner attached to parent entity
- `create_pickup()` — Health and ammo pickup entities
- `create_trigger_zone()` — Invisible trigger volumes with optional scripts
- `create_entity_batch()` — Batch creation from spec lists
- `create_grid()` — Grid layout of entities
- `build_entity()` — Fluent `EntityBuilder` for composable entity construction
- `setup_lighting()` — Lighting rig presets (three-point, outdoor, indoor)
- `create_camera()` — Camera presets (top-down, isometric, perspective, side-view)
- `create_static_body()` / `create_dynamic_body()` — Physics body helpers
- `create_physics_ground()` — Ground plane with static collision
- `get_scene_snapshot()` — Full scene state as JSON (C++ EBus with Python fallback)
- `get_entity_tree()` — Entity hierarchy tree as JSON
- `inspect_entity()` — Deep entity inspection
- `validate_scene()` — Scene validation for common issues
- `list_prefabs()` / `spawn_prefab()` — Prefab catalog and instantiation
- `begin_undo_batch()` / `end_undo_batch()` / `rollback_last_batch()` — Manual undo control
- `get_api_version()` / `get_available_functions()` / `get_component_catalog()` — Meta/discovery

#### C++ Subsystems (AZ Framework)
- `AiCompanionRequestBus` EBus interface with scene snapshot, entity tree, and validation
- `SceneSnapshotProvider` — Fast C++ entity traversal and JSON serialization
- `InputValidator` — Compiled input validation (entity names, component types, positions, paths)
- `AiCompanionSystemComponent` — Runtime system component with EBus handler
- `AiCompanionEditorSystemComponent` — Editor component with Python path registration and behavior context reflection

#### Safety Layer
- Input validation on all user-supplied strings (regex, bounds, whitelist)
- Operation sandboxing (entity limits, recursion depth, timeouts)
- Undo-batch wrapping with automatic rollback on failure (`@with_undo_batch`)
- System entity protection (EditorGlobal, SystemEntity, AZ:: prefixed)
- Path traversal prevention on all asset/script paths

#### Lua Gameplay Scripts
- `twin_stick_movement.lua` — WASD/stick movement with mouse/stick aim
- `projectile_launcher.lua` — Configurable projectile firing on input
- `enemy_chase_ai.lua` — Idle/Chase/Attack state machine AI
- `health_pickup.lua` — Heal on contact with respawn support
- `damage_on_contact.lua` — Collision damage with auto-destroy and lifetime
- `score_tracker.lua` — Score tracking via gameplay events
- `game_over_trigger.lua` — Player health monitoring and game over trigger

#### Prefabs
- `Player_TwinStick` — Player with capsule mesh, physics, and twin-stick script
- `Enemy_Chaser` — Chase AI enemy with cube mesh
- `Enemy_Turret` — Stationary turret with projectile launcher
- `Projectile_Basic` — Small sphere projectile with damage on contact
- `Pickup_Health` — Health pickup with trigger collider and respawn
- `Pickup_Ammo` — Ammo pickup with trigger collider
- `Environment_Ground` — 50x50 ground plane with static physics
- `Lighting_ThreePoint` — Key/fill/rim light hierarchy
- `Camera_TopDown` — Top-down camera at 20 units height

#### Component Registry
- Human-readable name to O3DE type ID mapping
- Case-insensitive and alias resolution
- Fuzzy matching with suggestions on typos
- 30+ components across Core, Rendering, Lighting, Physics, Scripting, Camera, Audio, Shapes, UI, and Networking categories

#### Example
- Twin-stick shooter example (`Examples/TwinStickShooter/setup.py`)
- Single-script game creation (~10 API calls vs ~70 raw o3de-mcp calls)

#### Documentation
- API reference with full parameter documentation
- Prefab catalog with component listings
- Lua script reference with property tables
- Safety model and threat documentation
- Twin-stick shooter walkthrough
- o3de-mcp integration guide with architecture diagrams

#### Testing
- 88 Python unit tests (validators, builders, templates, registry, sandbox)
- 3 C++ test files (AZ::Test for component lifecycle, input validation, scene snapshots)

#### Infrastructure
- `gem.json` manifest with O3DE 2305.0+ compatibility
- CMake build system with runtime, editor, and test targets
- Cross-platform support (Linux, Windows, macOS)
- Apache-2.0 OR MIT dual license

[1.1.0]: https://github.com/nickschuetz/o3de-ai-companion-gem/releases/tag/v1.1.0
[1.0.0]: https://github.com/nickschuetz/o3de-ai-companion-gem/releases/tag/v1.0.0
