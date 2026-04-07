----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Score Tracker
-- Tracks player score by listening for enemy defeat events.

local ScoreTracker = {
    Properties = {
        ScorePerKill = { default = 100, description = "Points awarded per enemy kill" },
        UICanvasEntity = { default = EntityId(), description = "Entity with UI canvas for score display" },
    },
}

function ScoreTracker:OnActivate()
    self.score = 0

    -- Listen for score events
    self.scoreHandler = GameplayNotificationBusHandler()
    self.scoreHandler:Connect(self, GameplayNotificationId(self.entityId, "AddScore"))

    self.killHandler = GameplayNotificationBusHandler()
    self.killHandler:Connect(self, GameplayNotificationId(self.entityId, "EnemyDefeated"))

    Debug.Log("[ScoreTracker] Activated. Score: 0")
end

function ScoreTracker:OnDeactivate()
    if self.scoreHandler then
        self.scoreHandler:Disconnect()
    end
    if self.killHandler then
        self.killHandler:Disconnect()
    end
end

function ScoreTracker:OnEventBegin(value)
    local busId = GameplayNotificationBus.GetCurrentBusId()

    if busId == GameplayNotificationId(self.entityId, "EnemyDefeated") then
        self.score = self.score + self.Properties.ScorePerKill
        Debug.Log("[ScoreTracker] Enemy defeated! Score: " .. tostring(self.score))
    elseif busId == GameplayNotificationId(self.entityId, "AddScore") then
        self.score = self.score + value
        Debug.Log("[ScoreTracker] Score added: " .. tostring(value) .. " Total: " .. tostring(self.score))
    end

    self:UpdateUI()
end

function ScoreTracker:UpdateUI()
    if self.Properties.UICanvasEntity:IsValid() then
        -- Update UI text element if available
        UiTextBus.Event.SetText(self.Properties.UICanvasEntity, "Score: " .. tostring(self.score))
    end
end

function ScoreTracker:GetScore()
    return self.score
end

return ScoreTracker
