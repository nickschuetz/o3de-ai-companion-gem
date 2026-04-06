----------------------------------------------------------------------------------------------------
-- Copyright Contributors to the AI Companion for O3DE Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Damage on Contact
-- Applies damage to entities upon collision. Useful for projectiles and hazards.

local DamageOnContact = {
    Properties = {
        Damage = { default = 10.0, description = "Damage dealt on contact" },
        DestroyOnContact = { default = true, description = "Whether to destroy self after contact" },
        CooldownTime = { default = 0.5, description = "Cooldown between damage applications" },
        Lifetime = { default = 5.0, description = "Auto-destroy after this many seconds (0 = no limit)" },
    },
}

function DamageOnContact:OnActivate()
    self.cooldownTimer = 0
    self.lifetimeTimer = self.Properties.Lifetime

    self.tickHandler = TickBusHandler()
    self.tickHandler:Connect(self)

    -- Listen for collision events
    self.collisionHandler = PhysicsComponentNotificationBusHandler()
    self.collisionHandler:Connect(self, self.entityId)
end

function DamageOnContact:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.collisionHandler then
        self.collisionHandler:Disconnect()
    end
end

function DamageOnContact:OnCollisionBegin(collision)
    if self.cooldownTimer > 0 then
        return
    end

    local otherEntityId = collision.otherEntity

    -- Send damage event
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(otherEntityId, "TakeDamage"),
        self.Properties.Damage)

    self.cooldownTimer = self.Properties.CooldownTime

    if self.Properties.DestroyOnContact then
        GameEntityContextRequestBus.Broadcast.DestroyGameEntity(self.entityId)
    end
end

function DamageOnContact:OnTick(deltaTime, scriptTime)
    -- Cooldown
    self.cooldownTimer = math.max(0, self.cooldownTimer - deltaTime)

    -- Lifetime auto-destroy
    if self.Properties.Lifetime > 0 then
        self.lifetimeTimer = self.lifetimeTimer - deltaTime
        if self.lifetimeTimer <= 0 then
            GameEntityContextRequestBus.Broadcast.DestroyGameEntity(self.entityId)
        end
    end
end

return DamageOnContact
