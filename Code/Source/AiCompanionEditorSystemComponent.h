/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include "AiCompanionSystemComponent.h"
#include "Network/AgentServer.h"

#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace AiCompanion
{
    class AiCompanionEditorSystemComponent
        : public AiCompanionSystemComponent
        , private AzToolsFramework::EditorEvents::Bus::Handler
    {
    public:
        AZ_RTTI(AiCompanionEditorSystemComponent, "{D4F3B5A7-8E9C-4DBB-CF6A-AB4C3E5D7A9F}", AiCompanionSystemComponent);
        AZ_CLASS_ALLOCATOR(AiCompanionEditorSystemComponent, AZ::SystemAllocator);

        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

    protected:
        // AZ::Component overrides
        void Activate() override;
        void Deactivate() override;

    private:
        //! Registers the Editor/Scripts/ directory so the ai_companion Python package is importable.
        void RegisterPythonPaths();

        //! Starts the AgentServer with configuration from settings registry and env vars.
        void StartAgentServer();

        AZStd::unique_ptr<AgentServer> m_agentServer;
    };
} // namespace AiCompanion
