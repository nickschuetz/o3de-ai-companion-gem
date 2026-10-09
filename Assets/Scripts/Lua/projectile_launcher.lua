----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Projectile Launcher
-- Fires a projectile spawnable on the "fire" input, with a configurable rate.
--
-- Spawning goes through SpawnableScriptMediator, the launcher-reachable path.
-- PrefabPublicRequestBus is editor-only (Automation scope) and is nil in a
-- game launcher. ProjectilePrefab is a spawnable asset reference; assign the
-- projectile prefab's .spawnable in the Inspector (or through o3de-mcp's
-- assign_asset). Without one, Fire() logs once and does nothing.
--
-- The projectile's spawn transform is not applied by its first tick, so the
-- muzzle position, direction, speed and damage are published in
-- _G.AiCompanionShot for damage_on_contact.lua to launch from.

local ProjectileLauncher = {
    Properties = {
        ProjectilePrefab = { default = SpawnableScriptAssetRef(), description = "Projectile spawnable to fire" },
        FireRate = { default = 5.0, description = "Shots per second" },
        ProjectileSpeed = { default = 20.0, description = "Projectile speed in units/s" },
        Damage = { default = 10.0, description = "Damage per projectile" },
        SpawnOffset = { default = Vector3(0, 1.0, 0), description = "Spawn offset along the entity's forward (Y) axis" },
    },
}

-- math.atan2 was removed in Lua 5.3; O3DE ships Lua 5.4, where math.atan takes (y, x).
local function atan2(y, x)
    return math.atan(y, x)
end

function ProjectileLauncher:OnActivate()
    self.fireCooldown = 0
    self.isFiring = false
    self.warnedNoPrefab = false
    self.mediator = SpawnableScriptMediator()

    self.tickHandler = TickBus.Connect(self)
    self.idFire = InputEventNotificationId("fire")
    self.inputHandler = InputEventNotificationBus.Connect(self, self.idFire)
end

function ProjectileLauncher:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.inputHandler then
        self.inputHandler:Disconnect()
    end
    if self.mediator then
        self.mediator:Clear()
    end
end

function ProjectileLauncher:OnPressed(value)
    self.isFiring = true
end

function ProjectileLauncher:OnHeld(value)
    self.isFiring = true
end

function ProjectileLauncher:OnReleased(value)
    self.isFiring = false
end

function ProjectileLauncher:OnTick(deltaTime, scriptTime)
    self.fireCooldown = math.max(0, self.fireCooldown - deltaTime)

    if self.isFiring and self.fireCooldown <= 0 then
        self:Fire()
        self.fireCooldown = 1.0 / self.Properties.FireRate
    end
end

function ProjectileLauncher:Fire()
    -- An unset SpawnableScriptAssetRef yields a ticket with id 0 (EntitySpawnTicket
    -- reflects only GetId to script).
    local ticket = self.Properties.ProjectilePrefab and self.mediator:CreateSpawnTicket(self.Properties.ProjectilePrefab)
    if ticket == nil or ticket:GetId() == 0 then
        if not self.warnedNoPrefab then
            Debug.Log("[ProjectileLauncher] ProjectilePrefab is not set; nothing to fire")
            self.warnedNoPrefab = true
        end
        return
    end

    local tm = TransformBus.Event.GetWorldTM(self.entityId)
    local forward = tm:GetBasisY():GetNormalized()
    local origin = TransformBus.Event.GetWorldTranslation(self.entityId)
    local offset = self.Properties.SpawnOffset
    local spawnPos = Vector3(
        origin.x + forward.x * offset.y,
        origin.y + forward.y * offset.y,
        origin.z + offset.z)

    _G.AiCompanionShot = {
        x = spawnPos.x, y = spawnPos.y, z = spawnPos.z,
        dx = forward.x, dy = forward.y, dz = forward.z,
        speed = self.Properties.ProjectileSpeed,
        damage = self.Properties.Damage,
    }

    -- Yaw (degrees) about Z so the projectile's local +Y points along the aim.
    local yawDegrees = math.deg(atan2(-forward.x, forward.y))
    self.mediator:SpawnAndParentAndTransform(ticket, EntityId(), spawnPos, Vector3(0, 0, yawDegrees), 1.0)
end

return ProjectileLauncher
