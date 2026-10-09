----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Damage on Contact
-- Damages the first registered target that comes within HitRadius. Used for
-- projectiles (Projectile_Basic) and hazards.
--
-- Contact is detected by distance, not by PhysX collision callbacks:
-- CollisionNotificationBus is reflected to Lua only in the Automation scope,
-- so a launcher script never receives OnCollisionBegin. Targets come from the
-- shared registry (_G.AiCompanionBodies[tag]) that enemy_chase_ai.lua and
-- twin_stick_movement.lua maintain, with TagGlobalRequestBus as a fallback.
--
-- As a projectile it moves itself through TransformBus. A spawned entity's
-- transform is not applied by its first tick, so projectile_launcher.lua
-- publishes the muzzle position and direction in _G.AiCompanionShot and this
-- script starts from that when Speed is left at 0 and a shot is pending.

local DamageOnContact = {
    Properties = {
        Damage = { default = 10.0, description = "Damage dealt on contact" },
        DestroyOnContact = { default = true, description = "Whether to destroy self after contact" },
        CooldownTime = { default = 0.5, description = "Cooldown between damage applications" },
        Lifetime = { default = 5.0, description = "Auto-destroy after this many seconds (0 = no limit)" },
        TargetTag = { default = "Enemy", description = "Registry/Tag name of entities this can damage" },
        HitRadius = { default = 0.75, description = "Distance at which contact is registered" },
        Speed = { default = 0.0, description = "Self-propelled speed along the launch direction (0 = use the launcher's shot, or stay put)" },
    },
}

local function bodies(tag)
    local all = rawget(_G, "AiCompanionBodies")
    if all == nil then
        all = {}
        _G.AiCompanionBodies = all
    end
    local t = all[tag]
    if t == nil then
        t = {}
        all[tag] = t
    end
    return t
end

function DamageOnContact:OnActivate()
    self.cooldownTimer = 0
    self.lifetimeTimer = self.Properties.Lifetime
    self.dead = false
    -- Properties added after the prefabs were authored need a fallback: the runtime
    -- ScriptComponent does not apply a .lua default for a property the prefab did not bake.
    self.targetTag = self.Properties.TargetTag or "Enemy"
    local radius = self.Properties.HitRadius or 0.75
    self.radiusSq = radius * radius

    -- Launch state. A shot published by projectile_launcher.lua wins over the
    -- entity's own transform, which is not yet applied on the first tick.
    local shot = rawget(_G, "AiCompanionShot")
    local speed = self.Properties.Speed or 0.0
    if shot ~= nil then
        self.px, self.py, self.pz = shot.x, shot.y, shot.z
        self.dx, self.dy, self.dz = shot.dx, shot.dy, shot.dz
        if speed <= 0 then
            speed = shot.speed or 0
        end
        _G.AiCompanionShot = nil
    else
        local pos = TransformBus.Event.GetWorldTranslation(self.entityId)
        local tm = TransformBus.Event.GetWorldTM(self.entityId)
        local fwd = tm and tm:GetBasisY() or Vector3(0, 1, 0)
        self.px, self.py, self.pz = pos.x, pos.y, pos.z
        self.dx, self.dy, self.dz = fwd.x, fwd.y, fwd.z
    end
    self.speed = speed
    self.moving = speed > 0

    self.tickHandler = TickBus.Connect(self)
end

function DamageOnContact:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
end

function DamageOnContact:Destroy()
    -- Lifetime and a hit can both fire in one tick; destroying twice corrupts the heap.
    if self.dead then
        return
    end
    self.dead = true
    GameEntityContextRequestBus.Broadcast.DestroyGameEntity(self.entityId)
end

function DamageOnContact:FindContact()
    local candidates = bodies(self.targetTag)
    if next(candidates) == nil then
        local tagged = TagGlobalRequestBus.Event.GetEntityByTag(Crc32(self.targetTag))
        if tagged ~= nil and tagged:IsValid() then
            candidates = { [tostring(tagged)] = tagged }
        end
    end
    for _, id in pairs(candidates) do
        local pos = TransformBus.Event.GetWorldTranslation(id)
        if pos then
            local ddx, ddy, ddz = pos.x - self.px, pos.y - self.py, pos.z - self.pz
            if ddx * ddx + ddy * ddy + ddz * ddz <= self.radiusSq then
                return id
            end
        end
    end
    return nil
end

function DamageOnContact:OnTick(deltaTime, scriptTime)
    if self.dead then
        return
    end

    if self.moving then
        self.px = self.px + self.dx * self.speed * deltaTime
        self.py = self.py + self.dy * self.speed * deltaTime
        self.pz = self.pz + self.dz * self.speed * deltaTime
        TransformBus.Event.SetWorldTranslation(self.entityId, Vector3(self.px, self.py, self.pz))
    else
        local pos = TransformBus.Event.GetWorldTranslation(self.entityId)
        if pos then
            self.px, self.py, self.pz = pos.x, pos.y, pos.z
        end
    end

    self.cooldownTimer = math.max(0, self.cooldownTimer - deltaTime)
    if self.cooldownTimer <= 0 then
        local target = self:FindContact()
        if target ~= nil then
            Debug.Log("[AiCompanion] contact damage " .. tostring(self.Properties.Damage) .. " to " .. tostring(target))
            GameplayNotificationBus.Event.OnEventBegin(
                GameplayNotificationId(target, "TakeDamage"),
                self.Properties.Damage)
            self.cooldownTimer = self.Properties.CooldownTime
            if self.Properties.DestroyOnContact then
                self:Destroy()
                return
            end
        end
    end

    if self.Properties.Lifetime > 0 then
        self.lifetimeTimer = self.lifetimeTimer - deltaTime
        if self.lifetimeTimer <= 0 then
            self:Destroy()
        end
    end
end

return DamageOnContact
