# Prefab Catalog

Pre-configured entity prefabs in `Assets/Prefabs/`. Spawn them with:

```python
from ai_companion.api import spawn_prefab
spawn_prefab("Player_TwinStick", position=[0, 0, 1])
```

## Player

### Player_TwinStick
Player entity with twin-stick movement controls.
- **Mesh**: Capsule
- **Physics**: Dynamic rigid body, capsule collider, no gravity, linear damping
- **Script**: `twin_stick_movement.lua` (WASD movement, mouse aim)
- **Tag**: `Player`

## Enemies

### Enemy_Chaser
Enemy that chases the player and attacks in melee range.
- **Mesh**: Cube
- **Physics**: Dynamic rigid body, box collider
- **Script**: `enemy_chase_ai.lua` (Idle -> Chase -> Attack state machine)
- **Tag**: `Enemy`

### Enemy_Turret
Stationary turret that fires projectiles at the player.
- **Mesh**: Cylinder (1.5x scale)
- **Physics**: Static rigid body
- **Script**: `projectile_launcher.lua` (1 shot/sec, speed 15, damage 15)
- **Tag**: `Enemy`, `Turret`

## Projectiles

### Projectile_Basic
Basic projectile with damage on contact and auto-destroy.
- **Mesh**: Sphere (0.2 scale)
- **Physics**: Dynamic rigid body (trigger collider, no gravity)
- **Script**: `damage_on_contact.lua` (10 damage, destroys on contact, 5s lifetime)
- **Tag**: `Projectile`

## Pickups

### Pickup_Health
Health pickup that heals on contact and respawns.
- **Mesh**: Sphere (0.5 scale)
- **Physics**: Trigger collider
- **Script**: `health_pickup.lua` (25 HP heal, 10s respawn, visual rotation)
- **Tag**: `Pickup`, `Health`

### Pickup_Ammo
Ammo pickup that restores ammunition.
- **Mesh**: Cube (0.5 scale)
- **Physics**: Trigger collider
- **Script**: `health_pickup.lua` (configured for ammo)
- **Tag**: `Pickup`, `Ammo`

## Environment

### Environment_Ground
Flat ground plane with collision.
- **Mesh**: Cube (50x50x0.1 scale)
- **Physics**: Static rigid body, box collider

## Lighting

### Lighting_ThreePoint
Three-point lighting rig with parent entity and 3 child lights.
- **Key Light**: Directional light, warm white, intensity 5.0
- **Fill Light**: Point light, cool white, intensity 2.0, 30m radius
- **Rim Light**: Spot light, neutral, intensity 3.0, 45-degree cone

## Camera

### Camera_TopDown
Top-down camera looking straight down.
- **Position**: (0, 0, 20)
- **Rotation**: (-90, 0, 0) — looking straight down
- **FOV**: 60 degrees
- **Active on activation**: Yes
