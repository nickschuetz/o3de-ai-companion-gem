/*
 * Copyright Contributors to the AI Companion for O3DE Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AiCompanionSystemComponent.h"
#include "Snapshot/SceneSnapshotProvider.h"
#include "Validation/InputValidator.h"

#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/RTTI/BehaviorContext.h>

namespace AiCompanion
{
    void AiCompanionSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<AiCompanionSystemComponent, AZ::Component>()
                ->Version(1);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<AiCompanionSystemComponent>(
                    "AiCompanion", "Provides AI-agent-friendly APIs for scene inspection and validation.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC("System"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true);
            }
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<AiCompanionRequestBus>("AiCompanionRequestBus")
                ->Attribute(AZ::Script::Attributes::Category, "AiCompanion")
                ->Event("GetSceneSnapshot", &AiCompanionRequestBus::Events::GetSceneSnapshot)
                ->Event("GetEntityTree", &AiCompanionRequestBus::Events::GetEntityTree)
                ->Event("ValidateScene", &AiCompanionRequestBus::Events::ValidateScene)
                ->Event("ValidateEntityName", &AiCompanionRequestBus::Events::ValidateEntityName)
                ->Event("ValidateComponentType", &AiCompanionRequestBus::Events::ValidateComponentType);
        }
    }

    void AiCompanionSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC("AiCompanionService"));
    }

    void AiCompanionSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC("AiCompanionService"));
    }

    void AiCompanionSystemComponent::GetRequiredServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& required)
    {
    }

    void AiCompanionSystemComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void AiCompanionSystemComponent::Init()
    {
    }

    void AiCompanionSystemComponent::Activate()
    {
        AiCompanionRequestBus::Handler::BusConnect();
    }

    void AiCompanionSystemComponent::Deactivate()
    {
        AiCompanionRequestBus::Handler::BusDisconnect();
    }

    AZStd::string AiCompanionSystemComponent::GetSceneSnapshot()
    {
        return SceneSnapshotProvider::CaptureSnapshot();
    }

    AZStd::string AiCompanionSystemComponent::GetEntityTree()
    {
        return SceneSnapshotProvider::CaptureEntityTree();
    }

    AZStd::string AiCompanionSystemComponent::ValidateScene()
    {
        return SceneSnapshotProvider::ValidateScene();
    }

    bool AiCompanionSystemComponent::ValidateEntityName(const AZStd::string& name)
    {
        return InputValidator::IsValidEntityName(name);
    }

    bool AiCompanionSystemComponent::ValidateComponentType(const AZStd::string& componentType)
    {
        return InputValidator::IsValidComponentType(componentType);
    }
} // namespace AiCompanion
