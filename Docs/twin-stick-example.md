# Twin-Stick Shooter Example Walkthrough

This guide walks through building a complete twin-stick shooter game using the
AI Companion API. The entire game is created in ~10 API calls.

## Prerequisites

1. O3DE project with AI Companion, EditorPythonBindings, and RemoteConsole enabled
2. Project built and editor running
3. o3de-mcp connected (or editor Python console available)

## Step 1: Create the Arena

```python
from ai_companion.api import bootstrap_twin_stick_arena
result = bootstrap_twin_stick_arena(size=30, wall_height=3)
print(result)
```

This creates:
- A 40x40 ground plane (size + 10 for border)
- 4 walls enclosing a 30x30 play area
- Three-point lighting (key, fill, rim lights)

## Step 2: Add the Player

```python
from ai_companion.api import create_player
result = create_player(
    name="Player",
    position=[0, 0, 1],
    movement="twin_stick",
    health=100,
)
print(result)
```

Creates a capsule entity with:
- Dynamic physics (no gravity, linear damping)
- Twin-stick movement script (WASD + mouse)
- `Player` tag for enemy AI targeting

## Step 3: Set Up the Camera

```python
from ai_companion.api import create_camera
result = create_camera(
    name="MainCamera",
    camera_type="top_down",
    offset=[0, 0, 25],
)
print(result)
```

Creates a camera 25 units above, looking straight down.

## Step 4: Spawn Enemies

```python
from ai_companion.api import create_enemy

create_enemy("Chaser1", position=[10, 10, 1], ai_type="chaser", speed=4)
create_enemy("Chaser2", position=[-10, 10, 1], ai_type="chaser", speed=3)
create_enemy("Chaser3", position=[10, -10, 1], ai_type="chaser", speed=3.5)
create_enemy("Turret1", position=[0, 12, 1], ai_type="turret")
```

- **Chasers**: Follow the player, attack in melee range
- **Turret**: Stationary, fires projectiles

## Step 5: Place Pickups

```python
from ai_companion.api import create_pickup

create_pickup("HealthPack1", position=[5, 5, 0.5], pickup_type="health", value=25)
create_pickup("HealthPack2", position=[-5, -5, 0.5], pickup_type="health", value=25)
create_pickup("AmmoPack1", position=[-7, 7, 0.5], pickup_type="ammo", value=10)
```

Pickups heal on contact and respawn after a delay.

## Step 6: Verify

```python
from ai_companion.api import get_scene_snapshot, validate_scene

print(get_scene_snapshot())
print(validate_scene())
```

The snapshot shows all entities with their positions and components.
The validation report flags any potential issues.

## Step 7: Play

Enter game mode in the editor to test the game.

## Customization Ideas

### Increase difficulty
```python
create_enemy("FastChaser", position=[0, -12, 1], ai_type="chaser", speed=8)
```

### Add obstacles
```python
from ai_companion.api import create_static_body
create_static_body("Pillar1", position=[5, 0, 1.5], mesh="primitive_cylinder")
create_static_body("Pillar2", position=[-5, 0, 1.5], mesh="primitive_cylinder")
```

### Bigger arena
```python
bootstrap_twin_stick_arena(size=50, wall_height=5)
```

### Wave spawning
Use `create_entity_batch()` to spawn waves of enemies:
```python
from ai_companion.api import create_entity_batch

wave = [
    {"name": f"Wave2_Chaser{i}", "type": "enemy", "position": [x, y, 1], "ai_type": "chaser"}
    for i, (x, y) in enumerate([(12, 12), (-12, 12), (12, -12), (-12, -12)])
]
create_entity_batch(wave)
```
