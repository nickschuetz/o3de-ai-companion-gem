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

    -- Connect to tick bus for per-frame updates
    self.tickHandler = TickBusHandler()
    self.tickHandler:Connect(self)

    -- Connect to input notifications
    self.inputHandler = InputEventNotificationBusHandler()
    self.inputHandler:Connect(self, InputEventNotificationId("move_x"))

    self.inputHandlerY = InputEventNotificationBusHandler()
    self.inputHandlerY:Connect(self, InputEventNotificationId("move_y"))

    self.inputHandlerAimX = InputEventNotificationBusHandler()
    self.inputHandlerAimX:Connect(self, InputEventNotificationId("aim_x"))

    self.inputHandlerAimY = InputEventNotificationBusHandler()
    self.inputHandlerAimY:Connect(self, InputEventNotificationId("aim_y"))

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
    local inputName = InputEventNotificationBus.GetCurrentBusId():GetName()
    if inputName == "move_x" then
        self.moveX = value
    elseif inputName == "move_y" then
        self.moveY = value
    elseif inputName == "aim_x" then
        self.aimX = value
    elseif inputName == "aim_y" then
        self.aimY = value
    end
end

function TwinStickMovement:OnHeld(value)
    self:OnPressed(value)
end

function TwinStickMovement:OnReleased(value)
    local inputName = InputEventNotificationBus.GetCurrentBusId():GetName()
    if inputName == "move_x" then
        self.moveX = 0
    elseif inputName == "move_y" then
        self.moveY = 0
    elseif inputName == "aim_x" then
        self.aimX = 0
    elseif inputName == "aim_y" then
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
        PhysicsRigidBodyRequestBus.Event.SetLinearVelocity(entityId, velocity)
    else
        -- Stop horizontal movement but preserve vertical velocity
        local currentVel = PhysicsRigidBodyRequestBus.Event.GetLinearVelocity(self.entityId)
        if currentVel then
            PhysicsRigidBodyRequestBus.Event.SetLinearVelocity(
                self.entityId, Vector3(0, 0, currentVel.z))
        end
    end

    -- Aim direction (rotation)
    local aimLen = math.sqrt(self.aimX * self.aimX + self.aimY * self.aimY)
    if aimLen > 0.1 then
        local angle = math.atan2(self.aimX, self.aimY)
        local targetRotation = Quaternion.CreateRotationZ(-angle)
        local currentRotation = TransformBus.Event.GetWorldRotationQuaternion(self.entityId)

        local newRotation = currentRotation:Slerp(targetRotation,
            math.min(1.0, self.Properties.RotationSpeed * deltaTime))
        TransformBus.Event.SetWorldRotationQuaternion(self.entityId, newRotation)
    end
end

return TwinStickMovement
