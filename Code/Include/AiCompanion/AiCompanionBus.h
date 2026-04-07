/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion
{
    class AiCompanionRequests : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Returns a JSON snapshot of the current scene (all entities, components, transforms).
        virtual AZStd::string GetSceneSnapshot() = 0;

        //! Returns a JSON representation of the entity hierarchy tree.
        virtual AZStd::string GetEntityTree() = 0;

        //! Validates the current scene for common issues. Returns JSON report.
        virtual AZStd::string ValidateScene() = 0;

        //! Validates an entity name against naming rules.
        virtual bool ValidateEntityName(const AZStd::string& name) = 0;

        //! Validates a component type name against known component types.
        virtual bool ValidateComponentType(const AZStd::string& componentType) = 0;
    };

    using AiCompanionRequestBus = AZ::EBus<AiCompanionRequests>;

} // namespace AiCompanion
