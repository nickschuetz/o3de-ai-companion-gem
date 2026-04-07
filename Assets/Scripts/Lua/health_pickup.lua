----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Health Pickup
-- Heals the contacting entity and optionally respawns after a delay.

local HealthPickup = {
    Properties = {
        HealAmount = { default = 25.0, description = "Amount of health restored on pickup" },
        RespawnTime = { default = 10.0, description = "Seconds before respawn (0 = no respawn)" },
        RotationSpeed = { default = 90.0, description = "Visual rotation speed in degrees/s" },
    },
}

function HealthPickup:OnActivate()
    self.isActive = true
    self.respawnTimer = 0

    self.tickHandler = TickBusHandler()
    self.tickHandler:Connect(self)

    -- Listen for trigger enter events
    self.triggerHandler = PhysicsComponentNotificationBusHandler()
    self.triggerHandler:Connect(self, self.entityId)
end

function HealthPickup:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.triggerHandler then
        self.triggerHandler:Disconnect()
    end
end

function HealthPickup:OnTriggerEnter(triggerEntityId, otherEntityId)
    if not self.isActive then
        return
    end

    -- Send heal event to the entity that entered the trigger
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(otherEntityId, "Heal"),
        self.Properties.HealAmount)

    -- Deactivate pickup
    self.isActive = false

    if self.Properties.RespawnTime > 0 then
        self.respawnTimer = self.Properties.RespawnTime
        -- Hide the entity visually
        RenderMeshComponentRequestBus.Event.SetVisibility(self.entityId, false)
    else
        -- Destroy the pickup
        GameEntityContextRequestBus.Broadcast.DestroyGameEntity(self.entityId)
    end
end

function HealthPickup:OnTick(deltaTime, scriptTime)
    if self.isActive then
        -- Visual rotation
        local currentRotation = TransformBus.Event.GetWorldRotationQuaternion(self.entityId)
        local rotDelta = Quaternion.CreateRotationZ(
            math.rad(self.Properties.RotationSpeed * deltaTime))
        TransformBus.Event.SetWorldRotationQuaternion(
            self.entityId, currentRotation * rotDelta)
    else
        -- Respawn timer
        if self.respawnTimer > 0 then
            self.respawnTimer = self.respawnTimer - deltaTime
            if self.respawnTimer <= 0 then
                self.isActive = true
                RenderMeshComponentRequestBus.Event.SetVisibility(self.entityId, true)
            end
        end
    end
end

return HealthPickup
