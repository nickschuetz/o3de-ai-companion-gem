# Lua Script Reference

Gameplay Lua scripts in `Assets/Scripts/Lua/`. Attach to entities via the
Python API or by adding a Lua Script component in the editor.

## twin_stick_movement.lua

Top-down twin-stick movement controller.

| Property | Default | Description |
|----------|---------|-------------|
| `MoveSpeed` | 8.0 | Movement speed (units/sec) |
| `RotationSpeed` | 10.0 | Rotation interpolation speed |
| `InputScheme` | "keyboard" | Input scheme: keyboard or gamepad |

**Input Channels:**
- `move_x` / `move_y` — Movement (WASD or left stick)
- `aim_x` / `aim_y` — Aim direction (mouse or right stick)

**Requirements:** PhysX Rigid Body (dynamic, no gravity recommended)

## projectile_launcher.lua

Fires projectile prefabs on input.

| Property | Default | Description |
|----------|---------|-------------|
| `ProjectilePrefab` | "Prefabs/Projectile_Basic.prefab" | Prefab to spawn |
| `FireRate` | 5.0 | Shots per second |
| `ProjectileSpeed` | 20.0 | Projectile speed (units/sec) |
| `Damage` | 10.0 | Damage per projectile |
| `SpawnOffset` | (0, 1, 0) | Spawn offset from entity |

**Input Channels:**
- `fire` — Fire trigger

## enemy_chase_ai.lua

Simple chase-and-attack AI with state machine.

| Property | Default | Description |
|----------|---------|-------------|
| `TargetTag` | "Player" | Tag of entity to chase |
| `ChaseSpeed` | 3.0 | Chase speed (units/sec) |
| `DetectionRadius` | 50.0 | Distance to detect target |
| `AttackRange` | 2.0 | Distance to start attacking |
| `AttackDamage` | 10.0 | Damage per attack |
| `AttackCooldown` | 1.0 | Seconds between attacks |

**States:** Idle -> Chase -> Attack

**Requirements:** PhysX Rigid Body, Tag component on target entity

## health_pickup.lua

Pickup that heals on contact and optionally respawns.

| Property | Default | Description |
|----------|---------|-------------|
| `HealAmount` | 25.0 | Health restored on pickup |
| `RespawnTime` | 10.0 | Seconds before respawn (0 = no respawn) |
| `RotationSpeed` | 90.0 | Visual rotation (degrees/sec) |

**Requirements:** PhysX Collider set as trigger

## damage_on_contact.lua

Applies damage on collision. Used for projectiles and hazards.

| Property | Default | Description |
|----------|---------|-------------|
| `Damage` | 10.0 | Damage dealt on contact |
| `DestroyOnContact` | true | Destroy self after contact |
| `CooldownTime` | 0.5 | Cooldown between damage ticks |
| `Lifetime` | 5.0 | Auto-destroy after N seconds (0 = never) |

**Requirements:** PhysX Collider

## score_tracker.lua

Tracks and displays player score.

| Property | Default | Description |
|----------|---------|-------------|
| `ScorePerKill` | 100 | Points per enemy kill |
| `UICanvasEntity` | EntityId() | UI entity for score display |

**Events listened:**
- `EnemyDefeated` — Adds ScorePerKill
- `AddScore` — Adds custom value

## game_over_trigger.lua

Monitors player health and triggers game over.

| Property | Default | Description |
|----------|---------|-------------|
| `PlayerTag` | "Player" | Tag of player entity |
| `GameOverUICanvas` | EntityId() | UI canvas for game over screen |
| `MaxHealth` | 100.0 | Maximum player health |

**Events listened:**
- `TakeDamage` — Reduces health
- `Heal` — Restores health

**Events broadcast:**
- `GameOver` — When health reaches 0
