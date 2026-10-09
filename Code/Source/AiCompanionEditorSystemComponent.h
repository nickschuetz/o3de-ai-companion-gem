/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include "AgentMode/AgentModeState.h"
#include "AiCompanionSystemComponent.h"
#include "Network/AgentServer.h"

#include <AiCompanion/AiCompanionEditorRequestBus.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

#include <chrono>

class QTimer;

namespace AiCompanion::AgentMode
{
    class Filter;
}

namespace AiCompanion
{
    class AiCompanionEditorSystemComponent
        : public AiCompanionSystemComponent
        , private AzToolsFramework::EditorEvents::Bus::Handler
        , public AZ::SystemTickBus::Handler
        , public AiCompanionEditorRequestBus::Handler
    {
    public:
        AZ_COMPONENT(AiCompanionEditorSystemComponent, "{D4F3B5A7-8E9C-4DBB-CF6A-AB4C3E5D7A9F}", AiCompanionSystemComponent);

        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);
        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent);

        // AiCompanionEditorRequestBus::Handler
        AZ::Outcome<void, AZStd::string> SetComponentPropertyUnwrapped(
            AZ::EntityComponentIdPair pair, AZStd::string propertyPath, AZStd::any value) override;

        AZStd::string GetBusSchema(AZStd::string busName) override;
        AZ::Outcome<AZ::u64, AZStd::string> CreateEntity(AZStd::string name, AZ::Vector3 position, AZ::u64 parentId) override;
        AZ::Outcome<void, AZStd::string> SetTransform(
            AZ::u64 entityId,
            bool setPosition,
            AZ::Vector3 position,
            bool setRotation,
            AZ::Vector3 rotationDegrees,
            bool setScale,
            float uniformScale) override;
        AZ::Outcome<void, AZStd::string> DeleteEntity(AZ::u64 entityId) override;
        AZ::Outcome<void, AZStd::string> CommitEntityToPrefab(AZ::EntityId entityId) override;
        AZ::Outcome<AZStd::string, AZStd::string> ListAnimGraphs() override;
        AZ::Outcome<AZStd::string, AZStd::string> GetAnimGraph(AZStd::string selector) override;
        AZ::Outcome<AZStd::string, AZStd::string> CreateAnimGraph() override;
        AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId) override;
        AZ::Outcome<AZStd::string, AZStd::string> LoadAnimGraph(AZStd::string fileName) override;
        AZ::Outcome<AZStd::string, AZStd::string> SaveAnimGraph(AZ::u32 animGraphId, AZStd::string fileName) override;
        AZ::Outcome<AZStd::string, AZStd::string> AddAnimGraphNode(AZ::u32 animGraphId, AZStd::string argumentsJson) override;
        AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraphNode(AZ::u32 animGraphId, AZStd::string nodeId) override;
        AZ::Outcome<AZStd::string, AZStd::string> SetAnimGraphEntryState(AZ::u32 animGraphId, AZStd::string nodeId) override;
        AZ::Outcome<AZStd::string, AZStd::string> AddAnimGraphParameter(AZ::u32 animGraphId, AZStd::string argumentsJson) override;
        AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraphParameter(AZ::u32 animGraphId, AZStd::string name) override;
        AZ::Outcome<AZStd::string, AZStd::string> AddAnimGraphTransition(AZ::u32 animGraphId, AZStd::string argumentsJson) override;
        AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraphTransition(AZ::u32 animGraphId, AZStd::string transitionId) override;
        AZ::Outcome<AZStd::string, AZStd::string> SetAnimGraphTransition(AZ::u32 animGraphId, AZStd::string argumentsJson) override;
        AZ::Outcome<AZStd::string, AZStd::string> ConnectAnimGraphPorts(AZ::u32 animGraphId, AZStd::string argumentsJson) override;
        AZ::Outcome<AZStd::string, AZStd::string> DisconnectAnimGraphPorts(AZ::u32 animGraphId, AZStd::string argumentsJson) override;
        AZ::Outcome<AZStd::string, AZStd::string> SetAnimGraphNode(AZ::u32 animGraphId, AZStd::string argumentsJson) override;

    protected:
        // AZ::Component overrides
        void Activate() override;
        void Deactivate() override;

        // AZ::SystemTickBus::Handler
        void OnSystemTick() override;

    private:
        //! Registers the Editor/Scripts/ directory so the ai_companion Python package is importable.
        void RegisterPythonPaths();

        //! Starts the AgentServer with configuration from settings registry and env vars.
        void StartAgentServer();

        //! Starts a Qt timer that drains the AgentServer's main-thread queue
        //! independently of the editor's idle/SystemTick loop, which the editor
        //! throttles or halts when its window is not the foreground app. Agent
        //! automation always runs with the editor unfocused, so relying on
        //! OnSystemTick alone starves the queue for minutes. See Deactivate.
        void StartQueuePump();

        //! Refreshes agent-mode state from the JSON sidecar and installs or
        //! removes the QApplication event filter to match.
        void RefreshAgentMode();

        // Agent mode runtime. State comes from
        //   $XDG_STATE_HOME/o3de-ai-companion/agent_mode.json
        // written by Editor/Scripts/ai_companion/agent_mode.py. See
        // Docs/agent-mode.md for the contract.
        AgentMode::State m_cachedAgentMode;
        AZStd::unique_ptr<AgentMode::Filter> m_agentModeFilter;
        std::chrono::steady_clock::time_point m_lastAgentModePoll;

        AZStd::unique_ptr<AgentServer> m_agentServer;

        // Drains the AgentServer queue on the main thread off the Qt event loop,
        // so it keeps running while the editor window is unfocused (when the
        // editor's own idle/SystemTick loop is throttled). Owned via Qt parent
        // (qApp); cleared in Deactivate.
        QTimer* m_queuePumpTimer = nullptr;
    };
} // namespace AiCompanion
