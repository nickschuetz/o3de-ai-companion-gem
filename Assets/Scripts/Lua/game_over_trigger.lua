----------------------------------------------------------------------------------------------------
-- Copyright Contributors to the AI Companion for O3DE Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Game Over Trigger
-- Monitors player health and triggers game over when health reaches zero.

local GameOverTrigger = {
    Properties = {
        PlayerTag = { default = "Player", description = "Tag of the player entity to monitor" },
        GameOverUICanvas = { default = EntityId(), description = "UI canvas entity to show on game over" },
        MaxHealth = { default = 100.0, description = "Maximum player health" },
    },
}

function GameOverTrigger:OnActivate()
    self.currentHealth = self.Properties.MaxHealth
    self.isGameOver = false
    self.playerEntityId = nil

    self.tickHandler = TickBusHandler()
    self.tickHandler:Connect(self)

    -- Find the player entity
    local taggedEntities = TagGlobalRequestBus.Event.RequestTaggedEntities(
        Crc32(self.Properties.PlayerTag))
    if taggedEntities and #taggedEntities > 0 then
        self.playerEntityId = taggedEntities[1]

        -- Listen for damage events on the player
        self.damageHandler = GameplayNotificationBusHandler()
        self.damageHandler:Connect(self,
            GameplayNotificationId(self.playerEntityId, "TakeDamage"))

        self.healHandler = GameplayNotificationBusHandler()
        self.healHandler:Connect(self,
            GameplayNotificationId(self.playerEntityId, "Heal"))
    end

    Debug.Log("[GameOverTrigger] Activated. Health: " .. tostring(self.currentHealth))
end

function GameOverTrigger:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.damageHandler then
        self.damageHandler:Disconnect()
    end
    if self.healHandler then
        self.healHandler:Disconnect()
    end
end

function GameOverTrigger:OnEventBegin(value)
    if self.isGameOver then
        return
    end

    local busId = GameplayNotificationBus.GetCurrentBusId()

    if busId == GameplayNotificationId(self.playerEntityId, "TakeDamage") then
        self.currentHealth = math.max(0, self.currentHealth - value)
        Debug.Log("[GameOverTrigger] Player took " .. tostring(value)
            .. " damage. Health: " .. tostring(self.currentHealth))

        if self.currentHealth <= 0 then
            self:TriggerGameOver()
        end
    elseif busId == GameplayNotificationId(self.playerEntityId, "Heal") then
        self.currentHealth = math.min(self.Properties.MaxHealth, self.currentHealth + value)
        Debug.Log("[GameOverTrigger] Player healed " .. tostring(value)
            .. ". Health: " .. tostring(self.currentHealth))
    end
end

function GameOverTrigger:TriggerGameOver()
    self.isGameOver = true
    Debug.Log("[GameOverTrigger] GAME OVER!")

    -- Show game over UI
    if self.Properties.GameOverUICanvas:IsValid() then
        UiCanvasAssetRefBus.Event.LoadCanvas(self.Properties.GameOverUICanvas)
    end

    -- Broadcast game over event
    GameplayNotificationBus.Event.OnEventBegin(
        GameplayNotificationId(self.entityId, "GameOver"), 0)
end

function GameOverTrigger:OnTick(deltaTime, scriptTime)
    -- Could add health regeneration or other per-frame checks here
end

function GameOverTrigger:GetHealth()
    return self.currentHealth
end

return GameOverTrigger
