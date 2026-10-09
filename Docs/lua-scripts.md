# Lua Script Reference

Gameplay Lua scripts in `Assets/Scripts/Lua/`. Attach to entities via the
Python API or by adding a Lua Script component in the editor.

> **Status: building blocks, verified by static checks only.** Every bus and
> class these scripts call is reflected to launcher Lua on O3DE 26.10 (checked
> against the engine source), and each file compiles under Lua 5.4, which is
> what O3DE ships. They have not been exercised in a running GameLauncher
> since the port described below. Mouse aim in `twin_stick_movement.lua` was
> observed not to dispatch in the editor's Play mode (May 2026); test it in a
> launcher build.
>
> **How contact works.** PhysX's `CollisionNotificationBus` and
> `TriggerNotificationBus` are reflected to Lua only in the Automation
> (editor) scope, and `PrefabPublicRequestBus` is editor-only too, so launcher
> scripts cannot use physics callbacks or the prefab bus. The scripts share a
> registry instead: `twin_stick_movement.lua` and `enemy_chase_ai.lua` list
> their entity under a tag in `_G.AiCompanionBodies[tag]`, and
> `health_pickup.lua` and `damage_on_contact.lua` test distance against those
> entries each tick (falling back to the Tag component through
> `TagGlobalRequestBus` when the registry has no entry for the tag).
> Projectiles spawn through `SpawnableScriptMediator`. The earlier Twin-Stick
> Shooter example, built before this port, is archived at the git tag
> `archive/twin-stick-example`.
>
> Properties added by the port (`BodyTag`, `Health`, `TargetTag`,
> `PickupRadius`, `HitRadius`, `Speed`) are read with fallbacks, because the
> runtime Script component does not apply a `.lua` default for a property the
> prefab did not bake.

## twin_stick_movement.lua

Top-down twin-stick movement controller.

| Property | Default | Description |
|----------|---------|-------------|
| `MoveSpeed` | 8.0 | Movement speed (units/sec) |
| `RotationSpeed` | 10.0 | Rotation interpolation speed |
| `BodyTag` | "Player" | Registry name this entity is listed under, for pickups and enemy projectiles |
| `InputScheme` | "keyboard" | Input scheme: keyboard or gamepad |

**Input Channels:**
- `move_x` / `move_y`, movement (WASD or left stick)
- `aim_x` / `aim_y`, aim direction (mouse or right stick)

**Requirements:** PhysX Dynamic Rigid Body (no gravity recommended)

## projectile_launcher.lua

Fires a projectile spawnable on the `fire` input, through
`SpawnableScriptMediator`.

| Property | Default | Description |
|----------|---------|-------------|
| `ProjectilePrefab` | (unset) | `SpawnableScriptAssetRef`; assign the projectile prefab's `.spawnable` in the Inspector or with o3de-mcp's `assign_asset`. Unset: `Fire()` logs once and does nothing |
| `FireRate` | 5.0 | Shots per second |
| `ProjectileSpeed` | 20.0 | Projectile speed (units/sec), published to the spawned projectile |
| `Damage` | 10.0 | Damage per projectile, published to the spawned projectile |
| `SpawnOffset` | (0, 1, 0) | Spawn offset; `y` is the distance along the entity's forward axis, `z` is added to height |

**Input Channels:**
- `fire`, fire trigger

The muzzle position, direction, speed and damage are published in
`_G.AiCompanionShot` so the projectile can launch from the muzzle on its first
tick, before its spawn transform is applied.

## enemy_chase_ai.lua

Simple chase-and-attack AI with state machine, plus hit points.

| Property | Default | Description |
|----------|---------|-------------|
| `TargetTag` | "Player" | Tag of entity to chase |
| `ChaseSpeed` | 3.0 | Chase speed (units/sec) |
| `DetectionRadius` | 50.0 | Distance to detect target |
| `AttackRange` | 2.0 | Distance to start attacking |
| `AttackDamage` | 10.0 | Damage per attack |
| `AttackCooldown` | 1.0 | Seconds between attacks |
| `BodyTag` | "Enemy" | Registry name this enemy is listed under, for projectiles and pickups |
| `Health` | 30.0 | Hit points; `TakeDamage` events reduce it |

**States:** Idle -> Chase -> Attack

**Events listened:** `TakeDamage` (addressed to this entity)

**Events broadcast:** `EnemyDefeated` to every registered score tracker when
health reaches 0; the enemy then destroys itself.

**Requirements:** PhysX Dynamic Rigid Body, Tag component on target entity

## health_pickup.lua

Pickup that heals the first registered target within `PickupRadius`, then
hides and optionally respawns. Contact is by distance, not a physics trigger.

| Property | Default | Description |
|----------|---------|-------------|
| `HealAmount` | 25.0 | Health restored on pickup |
| `RespawnTime` | 10.0 | Seconds before respawn (0 = no respawn) |
| `RotationSpeed` | 90.0 | Visual rotation (degrees/sec) |
| `TargetTag` | "Player" | Registry/Tag name of entities that can collect it |
| `PickupRadius` | 1.0 | Distance at which it is collected |

**Events broadcast:** `Heal` to the collector

**Requirements:** Mesh component (hidden while respawning). A collider is
optional; detection does not use it.

## damage_on_contact.lua

Damages the first registered target within `HitRadius`. Used for projectiles
and hazards. Contact is by distance, not a physics collision callback.

| Property | Default | Description |
|----------|---------|-------------|
| `Damage` | 10.0 | Damage dealt on contact |
| `DestroyOnContact` | true | Destroy self after contact |
| `CooldownTime` | 0.5 | Cooldown between damage ticks |
| `Lifetime` | 5.0 | Auto-destroy after N seconds (0 = never) |
| `TargetTag` | "Enemy" | Registry/Tag name of entities it can damage |
| `HitRadius` | 0.75 | Distance at which contact is registered |
| `Speed` | 0.0 | Self-propelled speed along the launch direction; 0 uses the launcher's published shot speed, or stays put for a hazard |

**Events broadcast:** `TakeDamage` to the target

As a projectile it moves itself through `TransformBus`, starting from the
launcher's published muzzle position and direction. A collider is optional.

## score_tracker.lua

Tracks and displays player score. Registers itself under `ScoreTracker` in the
shared registry so enemies can report defeats without knowing its entity id.

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
