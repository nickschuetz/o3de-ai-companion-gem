----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Health Pickup
-- Heals the first registered target that comes within PickupRadius, then
-- hides itself and optionally respawns after a delay.
--
-- Contact is detected by distance, not by a physics trigger. PhysX's
-- TriggerNotificationBus is reflected to Lua only in the Automation scope,
-- so a launcher script never receives OnTriggerEnter. Targets are found in
-- the shared registry that twin_stick_movement.lua and enemy_chase_ai.lua
-- maintain (_G.AiCompanionBodies[tag]), falling back to the Tag component
-- (TagGlobalRequestBus) for a single entity when the registry has no entry.

local HealthPickup = {
    Properties = {
        HealAmount = { default = 25.0, description = "Amount of health restored on pickup" },
        RespawnTime = { default = 10.0, description = "Seconds before respawn (0 = no respawn)" },
        RotationSpeed = { default = 90.0, description = "Visual rotation speed in degrees/s" },
        TargetTag = { default = "Player", description = "Registry/Tag name of entities that can collect this pickup" },
        PickupRadius = { default = 1.0, description = "Distance at which the pickup is collected" },
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

function HealthPickup:OnActivate()
    self.isActive = true
    self.respawnTimer = 0
    -- Properties added after the prefabs were authored need a fallback: the runtime
    -- ScriptComponent does not apply a .lua default for a property the prefab did not bake.
    self.targetTag = self.Properties.TargetTag or "Player"
    local radius = self.Properties.PickupRadius or 1.0
    self.radiusSq = radius * radius
    self.tickHandler = TickBus.Connect(self)
end

function HealthPickup:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
end

-- Returns the first candidate within PickupRadius, or nil.
function HealthPickup:FindCollector()
    local myPos = TransformBus.Event.GetWorldTranslation(self.entityId)
    if not myPos then
        return nil
    end
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
            local dx, dy, dz = pos.x - myPos.x, pos.y - myPos.y, pos.z - myPos.z
            if dx * dx + dy * dy + dz * dz <= self.radiusSq then
                return id
            end
        end
    end
    return nil
end

function HealthPickup:Collect(collectorId)
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(collectorId, "Heal"),
        self.Properties.HealAmount)

    self.isActive = false

    if self.Properties.RespawnTime > 0 then
        self.respawnTimer = self.Properties.RespawnTime
        RenderMeshComponentRequestBus.Event.SetVisibility(self.entityId, false)
    else
        GameEntityContextRequestBus.Broadcast.DestroyGameEntity(self.entityId)
    end
end

function HealthPickup:OnTick(deltaTime, scriptTime)
    if self.isActive then
        local collector = self:FindCollector()
        if collector ~= nil then
            self:Collect(collector)
            return
        end
        local currentRotation = TransformBus.Event.GetWorldRotationQuaternion(self.entityId)
        local rotDelta = Quaternion.CreateRotationZ(math.rad(self.Properties.RotationSpeed * deltaTime))
        TransformBus.Event.SetWorldRotationQuaternion(self.entityId, currentRotation * rotDelta)
    elseif self.respawnTimer > 0 then
        self.respawnTimer = self.respawnTimer - deltaTime
        if self.respawnTimer <= 0 then
            self.isActive = true
            RenderMeshComponentRequestBus.Event.SetVisibility(self.entityId, true)
        end
    end
end

return HealthPickup
