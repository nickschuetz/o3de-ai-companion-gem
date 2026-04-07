----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Projectile Launcher
-- Fires projectiles on input with configurable rate, speed, and damage.

local ProjectileLauncher = {
    Properties = {
        ProjectilePrefab = { default = "Prefabs/Projectile_Basic.prefab", description = "Prefab to spawn" },
        FireRate = { default = 5.0, description = "Shots per second" },
        ProjectileSpeed = { default = 20.0, description = "Projectile speed in units/s" },
        Damage = { default = 10.0, description = "Damage per projectile" },
        SpawnOffset = { default = Vector3(0, 1.0, 0), description = "Spawn offset from entity" },
    },
}

function ProjectileLauncher:OnActivate()
    self.fireCooldown = 0
    self.isFiring = false

    self.tickHandler = TickBusHandler()
    self.tickHandler:Connect(self)

    self.inputHandler = InputEventNotificationBusHandler()
    self.inputHandler:Connect(self, InputEventNotificationId("fire"))
end

function ProjectileLauncher:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.inputHandler then
        self.inputHandler:Disconnect()
    end
end

function ProjectileLauncher:OnPressed(value)
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
    local transform = TransformBus.Event.GetWorldTM(self.entityId)
    local forward = transform:GetBasisY():GetNormalized()
    local spawnPos = TransformBus.Event.GetWorldTranslation(self.entityId)
        + forward * self.Properties.SpawnOffset.y

    -- Spawn projectile prefab
    local spawnTicket = PrefabPublicRequestBus.Broadcast.InstantiatePrefab(
        self.Properties.ProjectilePrefab, nil, spawnPos)

    -- Apply velocity to the spawned projectile
    if spawnTicket then
        local velocity = forward * self.Properties.ProjectileSpeed
        -- The projectile's damage_on_contact script handles the rest
    end
end

return ProjectileLauncher
