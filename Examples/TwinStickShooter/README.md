# Twin-Stick Shooter Example

A complete twin-stick shooter scene built with the AI Companion Gem,
demonstrating how AI agents can compose full games through o3de-mcp.

## What gets created

- **Arena**: 30 x 30 enclosed area with walls and a ground plane.
- **Player**: capsule entity with twin-stick movement (WASD + mouse aim).
- **Enemies**: 3 chaser enemies plus 1 turret enemy.
- **Pickups**: 2 health packs plus 1 ammo pack.
- **Camera**: top-down camera looking straight down.
- **Lighting**: three-point lighting rig.

## How to run

The example will look for an already-open level first, then try to open
one of the project's default levels (e.g. `DefaultLevel`). On most project
templates this just works; if your project has no level by any of those
names, the helper will print a clear error pointing you at File > New
Level.

### Recommended pattern, batched invocation

This is the most reliable path. Each step is its own AgentServer request,
so the editor's main thread drains between calls and the prefab system has
time to finalize newly-created entities before the next batch. It avoids
the `SetName` race that affects the all-in-one form on some O3DE builds.

```bash
python Examples/TwinStickShooter/run_batched.py
```

The runner imports `o3de_mcp.tools.editor._pool` for the transport. Install
o3de-mcp with `pip install -e .` from its checkout before running.

### Quick-demo pattern, all-in-one

A single `run_editor_python` call. Convenient for one-liners; subject to a
known timing race that can leave the first batch of entities with default
names like `Entity1` through `Entity7` instead of `Ground`, `WallNorth`,
etc. The script's printed output reports the intended names regardless.

Via o3de-mcp:

```python
run_editor_python(open("/path/to/Examples/TwinStickShooter/setup.py").read())
```

Via the Editor Python Console:

```python
import sys
sys.path.insert(0, "/path/to/AiCompanion/Examples/TwinStickShooter")
import setup
```

The `import` statement runs `setup()` automatically (Python module bodies
execute on first import). To re-run after edits, use
`import importlib; importlib.reload(setup)`.

### Step by step

Each step is also callable on its own from the editor's Python console:

```python
import sys
sys.path.insert(0, "/path/to/AiCompanion/Examples/TwinStickShooter")
import steps

print(steps.ensure_level_open())
print(steps.build_arena(size=30, wall_height=3))
print(steps.add_player())
print(steps.add_camera())
for r in steps.add_enemies(): print(r)
for r in steps.add_pickups(): print(r)
print(steps.verify_scene())
```

## Controls

- **WASD**: move
- **Mouse**: aim direction
- **Left click**: fire projectile (requires a projectile spawner attached)

## Customization

All parameters are configurable. Example, faster chasers:

```python
from ai_companion.api import create_enemy
create_enemy("FastChaser", position=[5, 5, 1], ai_type="chaser", speed=8.0)
```

Bigger arena:

```python
from ai_companion.api import bootstrap_twin_stick_arena
bootstrap_twin_stick_arena(size=50, wall_height=5)
```

## API calls used

This example uses roughly 10 high-level API calls to create a complete
playable scene. Without the AI Companion Gem, the same setup would require
70 to 80 raw o3de-mcp tool calls.

| API call | Purpose |
|----------|---------|
| `bootstrap_twin_stick_arena()` | Ground + 4 walls + lighting |
| `create_player()` | Player entity with movement script |
| `create_camera()` | Top-down camera |
| `create_enemy()` x 4 | Enemy entities with AI scripts |
| `create_pickup()` x 3 | Health and ammo pickups |
| `get_scene_snapshot()` | Verify the scene |
