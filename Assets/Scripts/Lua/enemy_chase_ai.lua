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
    },
}

-- States
local STATE_IDLE = "idle"
local STATE_CHASE = "chase"
local STATE_ATTACK = "attack"

function EnemyChaseAI:OnActivate()
    self.state = STATE_IDLE
    self.attackTimer = 0
    self.targetEntityId = nil

    self.tickHandler = TickBusHandler()
    self.tickHandler:Connect(self)
end

function EnemyChaseAI:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
end

function EnemyChaseAI:FindTarget()
    -- Find entities with the target tag
    local taggedEntities = TagGlobalRequestBus.Event.RequestTaggedEntities(
        Crc32(self.Properties.TargetTag))
    if taggedEntities and #taggedEntities > 0 then
        self.targetEntityId = taggedEntities[1]
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
    if distance > self.Properties.DetectionRadius then
        self.state = STATE_IDLE
    elseif distance <= self.Properties.AttackRange then
        self.state = STATE_ATTACK
    else
        self.state = STATE_CHASE
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
        PhysicsRigidBodyRequestBus.Event.SetLinearVelocity(
            self.entityId, Vector3(velocity.x, velocity.y, 0))

        -- Face the target
        local angle = math.atan2(direction.x, direction.y)
        TransformBus.Event.SetWorldRotationQuaternion(
            self.entityId, Quaternion.CreateRotationZ(-angle))
    end
end

function EnemyChaseAI:Attack()
    if self.attackTimer <= 0 then
        -- Deal damage to the target (via a game event bus if available)
        GameplayNotificationBus.Event.OnEventBegin(
            GameplayNotificationId(self.targetEntityId, "TakeDamage"),
            self.Properties.AttackDamage)

        self.attackTimer = self.Properties.AttackCooldown
    end
end

return EnemyChaseAI
