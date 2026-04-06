# Twin-Stick Shooter Example

A complete twin-stick shooter game built with the AI Companion Gem, demonstrating
how AI agents can create full games through o3de-mcp.

## What Gets Created

- **Arena**: 30x30 enclosed area with walls and a ground plane
- **Player**: Capsule entity with twin-stick movement (WASD + mouse aim)
- **Enemies**: 3 chaser enemies + 1 turret enemy
- **Pickups**: 2 health packs + 1 ammo pack
- **Camera**: Top-down camera looking straight down
- **Lighting**: Three-point lighting rig

## How to Run

### Via o3de-mcp (AI Agent)

```python
run_editor_python(open("/path/to/Examples/TwinStickShooter/setup.py").read())
```

### Via Editor Python Console

```python
import sys
sys.path.insert(0, "/path/to/AiCompanion/Examples/TwinStickShooter")
import setup
```

### Step by Step

If you prefer to build the game step by step:

```python
from ai_companion.api import *

# 1. Arena
bootstrap_twin_stick_arena(size=30, wall_height=3)

# 2. Player
create_player("Player", position=[0, 0, 1], movement="twin_stick")

# 3. Camera
create_camera("MainCamera", camera_type="top_down")

# 4. Enemies
create_enemy("Chaser1", position=[10, 10, 1], ai_type="chaser")

# 5. Pickups
create_pickup("HealthPack", position=[5, 5, 0.5], pickup_type="health")

# 6. Play!
# Enter game mode in the editor
```

## Controls

- **WASD**: Move
- **Mouse**: Aim direction
- **Left Click**: Fire projectile (requires projectile spawner attached)

## Customization

All entity parameters are configurable. For example, to make enemies faster:

```python
create_enemy("FastChaser", position=[5, 5, 1], ai_type="chaser", speed=8.0)
```

To change arena size:

```python
bootstrap_twin_stick_arena(size=50, wall_height=5)
```

## API Calls Used

This example uses only **10 API calls** to create a complete playable game.
Without the AI Companion Gem, the same setup would require approximately
**70-80 raw o3de-mcp tool calls**.

| API Call | Purpose |
|----------|---------|
| `bootstrap_twin_stick_arena()` | Ground + 4 walls + lighting |
| `create_player()` | Player entity with movement script |
| `create_camera()` | Top-down camera |
| `create_enemy()` x4 | Enemy entities with AI scripts |
| `create_pickup()` x3 | Health and ammo pickups |
| `get_scene_snapshot()` | Verify the scene |
