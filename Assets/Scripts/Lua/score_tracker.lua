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

-- Enemies find score trackers through the shared registry (see enemy_chase_ai.lua).
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

function ScoreTracker:OnActivate()
    self.score = 0
    self.key = tostring(self.entityId)
    bodies("ScoreTracker")[self.key] = self.entityId

    -- Listen for score events
    self.scoreHandler = GameplayNotificationBus.Connect(self, GameplayNotificationId(self.entityId, "AddScore"))
    self.killHandler = GameplayNotificationBus.Connect(self, GameplayNotificationId(self.entityId, "EnemyDefeated"))

    Debug.Log("[ScoreTracker] Activated. Score: 0")
end

function ScoreTracker:OnDeactivate()
    if self.key then
        bodies("ScoreTracker")[self.key] = nil
    end
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
