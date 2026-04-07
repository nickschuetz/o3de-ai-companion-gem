/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AiCompanion/AiCompanionBus.h>

namespace AiCompanion
{
    class AiCompanionSystemComponent
        : public AZ::Component
        , protected AiCompanionRequestBus::Handler
    {
    public:
        AZ_COMPONENT(AiCompanionSystemComponent, "{B2D1F3E5-6C7A-4B9F-AD4E-8F2A1C3B5E7D}");

        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

    protected:
        // AZ::Component overrides
        void Init() override;
        void Activate() override;
        void Deactivate() override;

        // AiCompanionRequestBus overrides
        AZStd::string GetSceneSnapshot() override;
        AZStd::string GetEntityTree() override;
        AZStd::string ValidateScene() override;
        bool ValidateEntityName(const AZStd::string& name) override;
        bool ValidateComponentType(const AZStd::string& componentType) override;
    };
} // namespace AiCompanion
