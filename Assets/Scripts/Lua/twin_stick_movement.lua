----------------------------------------------------------------------------------------------------
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- SPDX-License-Identifier: Apache-2.0 OR MIT
----------------------------------------------------------------------------------------------------

-- Twin-Stick Movement Controller
-- Provides WASD/left-stick movement and mouse/right-stick aim for top-down gameplay.

local TwinStickMovement = {
    Properties = {
        MoveSpeed = { default = 8.0, description = "Movement speed in units per second" },
        RotationSpeed = { default = 10.0, description = "Rotation interpolation speed" },
        InputScheme = { default = "keyboard", description = "Input scheme: keyboard or gamepad" },
    },
}

function TwinStickMovement:OnActivate()
    self.moveDirection = Vector3(0, 0, 0)
    self.aimDirection = Vector3(0, 1, 0)

    -- Connect to tick bus for per-frame updates.
    -- O3DE 26050 dropped the BusName + 'Handler' constructor; Bus.Connect(self [, busId])
    -- returns the handler in one call.
    self.tickHandler = TickBus.Connect(self)

    -- Connect to input notifications.
    -- O3DE 26050: InputEventNotificationId no longer exposes :GetName(); we stash
    -- the IDs and compare via the reflected ::Equal operator (Lua '==') in the
    -- OnPressed/OnHeld/OnReleased handlers.
    self.idMoveX = InputEventNotificationId("move_x")
    self.idMoveY = InputEventNotificationId("move_y")
    self.idAimX = InputEventNotificationId("aim_x")
    self.idAimY = InputEventNotificationId("aim_y")

    self.inputHandler = InputEventNotificationBus.Connect(self, self.idMoveX)
    self.inputHandlerY = InputEventNotificationBus.Connect(self, self.idMoveY)
    self.inputHandlerAimX = InputEventNotificationBus.Connect(self, self.idAimX)
    self.inputHandlerAimY = InputEventNotificationBus.Connect(self, self.idAimY)

    self.moveX = 0
    self.moveY = 0
    self.aimX = 0
    self.aimY = 0
end

function TwinStickMovement:OnDeactivate()
    if self.tickHandler then
        self.tickHandler:Disconnect()
    end
    if self.inputHandler then
        self.inputHandler:Disconnect()
    end
    if self.inputHandlerY then
        self.inputHandlerY:Disconnect()
    end
    if self.inputHandlerAimX then
        self.inputHandlerAimX:Disconnect()
    end
    if self.inputHandlerAimY then
        self.inputHandlerAimY:Disconnect()
    end
end

function TwinStickMovement:OnPressed(value)
    local busId = InputEventNotificationBus.GetCurrentBusId()
    if busId == self.idMoveX then
        self.moveX = value
    elseif busId == self.idMoveY then
        self.moveY = value
    elseif busId == self.idAimX then
        self.aimX = value
    elseif busId == self.idAimY then
        self.aimY = value
    end
end

function TwinStickMovement:OnHeld(value)
    self:OnPressed(value)
end

function TwinStickMovement:OnReleased(value)
    local busId = InputEventNotificationBus.GetCurrentBusId()
    if busId == self.idMoveX then
        self.moveX = 0
    elseif busId == self.idMoveY then
        self.moveY = 0
    elseif busId == self.idAimX then
        self.aimX = 0
    elseif busId == self.idAimY then
        self.aimY = 0
    end
end

function TwinStickMovement:OnTick(deltaTime, scriptTime)
    -- Movement
    local moveLen = math.sqrt(self.moveX * self.moveX + self.moveY * self.moveY)
    if moveLen > 0.1 then
        local normalizedX = self.moveX / moveLen
        local normalizedY = self.moveY / moveLen

        local velocity = Vector3(
            normalizedX * self.Properties.MoveSpeed,
            normalizedY * self.Properties.MoveSpeed,
            0
        )

        -- Apply velocity via physics
        local entityId = self.entityId
        RigidBodyRequestBus.Event.SetLinearVelocity(entityId, velocity)
    else
        -- Stop horizontal movement but preserve vertical velocity
        local currentVel = RigidBodyRequestBus.Event.GetLinearVelocity(self.entityId)
        if currentVel then
            RigidBodyRequestBus.Event.SetLinearVelocity(
                self.entityId, Vector3(0, 0, currentVel.z))
        end
    end

    -- Aim (yaw). The Player is a dynamic rigid body, so PhysX overwrites any
    -- direct TransformBus rotation we set. Drive yaw via angular velocity on
    -- the rigid body instead. Horizontal mouse delta (per-frame pixel delta)
    -- maps to yaw rate around Z; gamepad right-stick X would land here too as
    -- a sustained value.
    if math.abs(self.aimX) > 0.001 then
        local turnSpeed = -self.aimX * self.Properties.RotationSpeed
        RigidBodyRequestBus.Event.SetAngularVelocity(
            self.entityId, Vector3(0, 0, turnSpeed))
    else
        -- No aim input: stop spinning so we don't drift after a flick.
        RigidBodyRequestBus.Event.SetAngularVelocity(
            self.entityId, Vector3(0, 0, 0))
    end
    -- Mouse deltas are one-shot per frame; gamepad stick values get re-set
    -- on every tick anyway.
    self.aimX = 0
    self.aimY = 0
end

return TwinStickMovement
