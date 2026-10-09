----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Enemy Chase AI
-- Simple state machine: Idle -> Chase -> Attack.
-- Chases a target entity by tag and attacks when in range.

local EnemyChaseAI = {
    Properties = {
        TargetTag = { default = "Player", description = "Tag of the entity to chase" },
        ChaseSpeed = { default = 3.0, description = "Chase movement speed" },
        DetectionRadius = { default = 50.0, description = "Distance at which enemy detects target" },
        AttackRange = { default = 2.0, description = "Distance to start attacking" },
        AttackDamage = { default = 10.0, description = "Damage dealt per attack" },
        AttackCooldown = { default = 1.0, description = "Seconds between attacks" },
        BodyTag = { default = "Enemy", description = "Registry name this enemy is listed under, for projectiles and pickups" },
        Health = { default = 30.0, description = "Hit points; TakeDamage events reduce it, EnemyDefeated is sent at 0" },
    },
}

-- Shared registry of live bodies by tag (_G.AiCompanionBodies[tag][key] = EntityId).
-- health_pickup.lua and damage_on_contact.lua scan it for contact by distance,
-- since PhysX trigger and collision callbacks are not reachable from launcher Lua.
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

-- math.atan2 was removed in Lua 5.3; O3DE ships Lua 5.4, where math.atan takes (y, x).
local function atan2(y, x)
    return math.atan(y, x)
end

-- States
local STATE_IDLE = "idle"
local STATE_CHASE = "chase"
local STATE_ATTACK = "attack"

function EnemyChaseAI:OnActivate()
    self.state = STATE_IDLE
    self.attackTimer = 0
    self.targetEntityId = nil
    -- The runtime ScriptComponent does not apply a .lua default for a property the
    -- prefab did not bake, so every property added after the prefabs were authored
    -- is read with a fallback.
    self.bodyTag = self.Properties.BodyTag or "Enemy"
    self.health = self.Properties.Health or 30.0
    self.defeated = false

    self.key = tostring(self.entityId)
    bodies(self.bodyTag)[self.key] = self.entityId

    self.tickHandler = TickBus.Connect(self)
    self.damageHandler = GameplayNotificationBus.Connect(self, GameplayNotificationId(self.entityId, "TakeDamage"))
end

function EnemyChaseAI:OnDeactivate()
    if self.key then
        bodies(self.bodyTag)[self.key] = nil
    end
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.damageHandler then
        self.damageHandler:Disconnect()
    end
end

-- TakeDamage arrives here (GameplayNotificationBus addressed to this entity).
function EnemyChaseAI:OnEventBegin(value)
    if self.defeated then
        return
    end
    self.health = self.health - (tonumber(value) or 0)
    Debug.Log("[AiCompanion] enemy took damage " .. tostring(value) .. " health " .. tostring(self.health))
    if self.health <= 0 then
        self.defeated = true
        Debug.Log("[AiCompanion] enemy defeated")
        -- Tell every registered score tracker, then remove ourselves.
        for _, trackerId in pairs(bodies("ScoreTracker")) do
            GameplayNotificationBus.Event.OnEventBegin(
                GameplayNotificationId(trackerId, "EnemyDefeated"), 1)
        end
        GameEntityContextRequestBus.Broadcast.DestroyGameEntity(self.entityId)
    end
end

function EnemyChaseAI:FindTarget()
    -- Prefer the shared body registry (twin_stick_movement.lua lists the player
    -- under "Player"), which needs no Tag component on the target. Fall back to
    -- the Tag component through TagGlobalRequestBus; the event reflects to
    -- scripting as "Get Entity By Tag", and O3DE's Lua binding strips spaces, so
    -- the callable name is GetEntityByTag, returning a single EntityId.
    local wanted = self.Properties.TargetTag or "Player"
    for _, id in pairs(bodies(wanted)) do
        if id ~= nil and id:IsValid() then
            self.targetEntityId = id
            return true
        end
    end
    local target = TagGlobalRequestBus.Event.GetEntityByTag(
        Crc32(wanted))
    if target ~= nil and target:IsValid() then
        self.targetEntityId = target
        return true
    end
    return false
end

function EnemyChaseAI:GetDistanceToTarget()
    if not self.targetEntityId then
        return math.huge
    end

    local myPos = TransformBus.Event.GetWorldTranslation(self.entityId)
    local targetPos = TransformBus.Event.GetWorldTranslation(self.targetEntityId)

    if not myPos or not targetPos then
        return math.huge
    end

    local diff = targetPos - myPos
    return diff:GetLength()
end

function EnemyChaseAI:OnTick(deltaTime, scriptTime)
    if self.defeated then
        return
    end
    self.attackTimer = math.max(0, self.attackTimer - deltaTime)

    -- Try to find target if we don't have one
    if not self.targetEntityId then
        if not self:FindTarget() then
            self.state = STATE_IDLE
            return
        end
    end

    local distance = self:GetDistanceToTarget()

    -- State transitions
    local nextState
    if distance > self.Properties.DetectionRadius then
        nextState = STATE_IDLE
    elseif distance <= self.Properties.AttackRange then
        nextState = STATE_ATTACK
    else
        nextState = STATE_CHASE
    end
    if nextState ~= self.state then
        self.state = nextState
        local me = TransformBus.Event.GetWorldTranslation(self.entityId)
        local them = self.targetEntityId and TransformBus.Event.GetWorldTranslation(self.targetEntityId) or nil
        local function fmt(v)
            if not v then return "nil" end
            return string.format("(%.1f, %.1f, %.1f)", v.x, v.y, v.z)
        end
        Debug.Log("[AiCompanion] enemy state " .. nextState .. " distance " .. string.format("%.2f", distance)
            .. " self " .. fmt(me) .. " target " .. fmt(them))
    end

    -- State behavior
    if self.state == STATE_CHASE then
        self:Chase(deltaTime)
    elseif self.state == STATE_ATTACK then
        self:Attack()
    end
end

function EnemyChaseAI:Chase(deltaTime)
    local myPos = TransformBus.Event.GetWorldTranslation(self.entityId)
    local targetPos = TransformBus.Event.GetWorldTranslation(self.targetEntityId)

    if not myPos or not targetPos then
        return
    end

    local direction = targetPos - myPos
    direction.z = 0  -- Stay on the same plane
    local len = direction:GetLength()

    if len > 0.01 then
        direction = direction / len
        local velocity = direction * self.Properties.ChaseSpeed
        RigidBodyRequestBus.Event.SetLinearVelocity(
            self.entityId, Vector3(velocity.x, velocity.y, 0))

        -- Face the target through angular velocity. Writing the transform of a
        -- simulated rigid body every tick teleports the body back to that pose
        -- each step, so the linear velocity above never moves it (the launcher
        -- warns "Transform of Entity ... with simulated body was set manually").
        local tm = TransformBus.Event.GetWorldTM(self.entityId)
        local forward = tm and tm:GetBasisY() or Vector3(0, 1, 0)
        local cross = forward.x * direction.y - forward.y * direction.x
        local dot = forward.x * direction.x + forward.y * direction.y
        local turn = atan2(cross, dot)
        RigidBodyRequestBus.Event.SetAngularVelocity(
            self.entityId, Vector3(0, 0, turn * 4.0))
    end
end

function EnemyChaseAI:Attack()
    if self.attackTimer <= 0 then
        Debug.Log("[AiCompanion] enemy attack " .. tostring(self.Properties.AttackDamage))
        -- Deal damage to the target (via a game event bus if available)
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(self.targetEntityId, "TakeDamage"),
            self.Properties.AttackDamage)

        self.attackTimer = self.Properties.AttackCooldown
    end
end

return EnemyChaseAI
