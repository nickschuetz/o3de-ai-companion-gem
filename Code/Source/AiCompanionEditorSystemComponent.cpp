/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AiCompanionEditorSystemComponent.h"

#include "AgentMode/AgentModeFilter.h"
#include "AgentMode/AgentModeState.h"
#include "Introspection/BusSchema.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistryMergeUtils.h>
#include <AzCore/Utils/Utils.h>
#include <AzFramework/API/ApplicationAPI.h>
#include <AzToolsFramework/API/EditorPythonRunnerRequestsBus.h>
#include <AzToolsFramework/PropertyTreeEditor/PropertyTreeEditor.h>
#include <AzToolsFramework/ToolsComponents/GenericComponentWrapper.h>

#include <QApplication>

#include <cstdlib>

namespace AiCompanion
{
    void AiCompanionEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<AiCompanionEditorSystemComponent, AZ::Component>()->Version(1);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext
                    ->Class<AiCompanionEditorSystemComponent>(
                        "AiCompanion Editor",
                        "Editor-side AI Companion component that registers Python API paths and provides editor-specific functionality.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC("System"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true);
            }
        }

        if (auto* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<AiCompanionEditorRequestBus>("AiCompanionEditorRequestBus")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation)
                ->Attribute(AZ::Script::Attributes::Category, "AiCompanion")
                ->Attribute(AZ::Script::Attributes::Module, "editor")
                ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::ExcludeFlags::All)
                ->Event("SetComponentPropertyUnwrapped", &AiCompanionEditorRequestBus::Events::SetComponentPropertyUnwrapped)
                ->Event(
                    "GetBusSchema",
                    &AiCompanionEditorRequestBus::Events::GetBusSchema,
                    { { { "busName", "Reflected bus name to describe, e.g. 'DioramaSpriteRequestBus'. Empty lists all bus names." } } });
        }
    }

    void AiCompanionEditorSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        AiCompanionSystemComponent::GetProvidedServices(provided);
        provided.push_back(AZ_CRC("AiCompanionEditorService"));
    }

    void AiCompanionEditorSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        AiCompanionSystemComponent::GetIncompatibleServices(incompatible);
        incompatible.push_back(AZ_CRC("AiCompanionEditorService"));
    }

    void AiCompanionEditorSystemComponent::GetRequiredServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& required)
    {
    }

    void AiCompanionEditorSystemComponent::GetDependentServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void AiCompanionEditorSystemComponent::Activate()
    {
        AiCompanionSystemComponent::Activate();
        AzToolsFramework::EditorEvents::Bus::Handler::BusConnect();
        AZ::SystemTickBus::Handler::BusConnect();
        AiCompanionEditorRequestBus::Handler::BusConnect();
        RegisterPythonPaths();
        StartAgentServer();

        m_lastAgentModePoll = std::chrono::steady_clock::now();
        RefreshAgentMode();
    }

    void AiCompanionEditorSystemComponent::Deactivate()
    {
        if (m_agentModeFilter)
        {
            if (QApplication* app = qApp)
            {
                app->removeEventFilter(m_agentModeFilter.get());
            }
            m_agentModeFilter.reset();
        }

        if (m_agentServer)
        {
            m_agentServer->Stop();
            m_agentServer.reset();
        }

        AiCompanionEditorRequestBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        AzToolsFramework::EditorEvents::Bus::Handler::BusDisconnect();
        AiCompanionSystemComponent::Deactivate();
    }

    void AiCompanionEditorSystemComponent::OnSystemTick()
    {
        if (m_agentServer)
        {
            m_agentServer->ProcessMainThreadQueue();
        }

        // Re-read the agent-mode state file at most once per second. The file
        // is tiny (~120 bytes) and a missing file is a cheap fast-path.
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastAgentModePoll >= std::chrono::seconds(1))
        {
            m_lastAgentModePoll = now;
            RefreshAgentMode();
        }
    }

    void AiCompanionEditorSystemComponent::RefreshAgentMode()
    {
        AgentMode::State current;
        AgentMode::LoadState(current);

        const bool wantsFilter = current.enabled && current.suppressDialogs;
        const bool hasFilter = m_agentModeFilter != nullptr;

        if (wantsFilter && !hasFilter)
        {
            if (QApplication* app = qApp)
            {
                m_agentModeFilter = AZStd::make_unique<AgentMode::Filter>();
                app->installEventFilter(m_agentModeFilter.get());
                AZ_Printf("AiCompanion", "[AgentMode] Enabled; event filter installed on qApp.\n");
            }
        }
        else if (!wantsFilter && hasFilter)
        {
            if (QApplication* app = qApp)
            {
                app->removeEventFilter(m_agentModeFilter.get());
            }
            m_agentModeFilter.reset();
            AZ_Printf("AiCompanion", "[AgentMode] Disabled; event filter removed.\n");
        }

        m_cachedAgentMode = current;
        // Mirror the applied state into the observed-state file so external
        // tooling can verify the C++ side has caught up after an input-file
        // change. AZ_Printf from OnSystemTick is not reliable post-init, so
        // this file is the sanctioned introspection path.
        AgentMode::WriteObservedState(current, m_agentModeFilter != nullptr);
    }

    void AiCompanionEditorSystemComponent::RegisterPythonPaths()
    {
        // Resolve the Gem's root directory and add Editor/Scripts/ to Python sys.path
        // so that `import ai_companion` works from EditorPythonBindings.
        AZ::IO::FixedMaxPath gemRoot;
        if (auto* settingsRegistry = AZ::SettingsRegistry::Get())
        {
            // Look up the Gem's source path from the settings registry
            AZStd::string gemPath;
            const auto gemKey = AZStd::string::format("/O3DE/Gems/AiCompanion/SourcePaths/0");
            if (settingsRegistry->Get(gemPath, gemKey))
            {
                gemRoot = AZ::IO::FixedMaxPath(gemPath.c_str());
            }
        }

        if (gemRoot.empty())
        {
            AZ_Warning(
                "AiCompanion",
                false,
                "Could not resolve AiCompanion Gem root path. "
                "The ai_companion Python package may not be importable.");
            return;
        }

        AZ::IO::FixedMaxPath scriptsPath = gemRoot / "Editor" / "Scripts";

        // Add to Python sys.path via EditorPythonBindings
        AzToolsFramework::EditorPythonRunnerRequestBus::Broadcast(
            &AzToolsFramework::EditorPythonRunnerRequestBus::Events::ExecuteByString,
            AZStd::string::format(
                "import sys; path = r'%s'; "
                "sys.path.insert(0, path) if path not in sys.path else None",
                scriptsPath.c_str())
                .c_str(),
            false);

        AZ_TracePrintf("AiCompanion", "Registered Python path: %s\n", scriptsPath.c_str());
    }
    void AiCompanionEditorSystemComponent::StartAgentServer()
    {
        // Resolve configuration: env var > settings registry > default
        bool enabled = true;
        AZStd::string host = "127.0.0.1";
        AZ::s64 portValue = 4600;
        bool secureMode = false;
        bool tlsEnabled = false;
        AZStd::string tlsCertPath;
        AZStd::string tlsKeyPath;
        AZStd::string logLevelStr = "minimal";

        if (auto* sr = AZ::SettingsRegistry::Get())
        {
            sr->Get(enabled, "/O3DE/AiCompanion/AgentServer/Enabled");
            sr->Get(host, "/O3DE/AiCompanion/AgentServer/Host");
            sr->Get(portValue, "/O3DE/AiCompanion/AgentServer/Port");
            sr->Get(secureMode, "/O3DE/AiCompanion/AgentServer/SecureMode");
            sr->Get(tlsEnabled, "/O3DE/AiCompanion/AgentServer/TlsEnabled");
            sr->Get(tlsCertPath, "/O3DE/AiCompanion/AgentServer/TlsCertPath");
            sr->Get(tlsKeyPath, "/O3DE/AiCompanion/AgentServer/TlsKeyPath");
            sr->Get(logLevelStr, "/O3DE/AiCompanion/AgentServer/LogLevel");
        }

        // Environment variable overrides (highest priority)
        if (const char* envEnabled = std::getenv("AI_COMPANION_SERVER_ENABLED"))
        {
            AZStd::string val(envEnabled);
            enabled = (val != "0" && val != "false");
        }
        if (const char* envHost = std::getenv("AI_COMPANION_SERVER_HOST"))
        {
            host = envHost;
        }
        if (const char* envPort = std::getenv("O3DE_EDITOR_PORT"))
        {
            portValue = std::atoi(envPort);
        }
        if (const char* envSecure = std::getenv("AI_COMPANION_SECURE_MODE"))
        {
            AZStd::string val(envSecure);
            secureMode = (val == "1" || val == "true");
        }
        if (const char* envTls = std::getenv("AI_COMPANION_TLS_ENABLED"))
        {
            AZStd::string val(envTls);
            tlsEnabled = (val == "1" || val == "true");
        }
        if (const char* envCert = std::getenv("AI_COMPANION_TLS_CERT"))
        {
            tlsCertPath = envCert;
        }
        if (const char* envKey = std::getenv("AI_COMPANION_TLS_KEY"))
        {
            tlsKeyPath = envKey;
        }
        if (const char* envLog = std::getenv("AI_COMPANION_LOG_LEVEL"))
        {
            logLevelStr = envLog;
        }

        if (!enabled)
        {
            AZ_Printf("AiCompanion", "[AgentServer] Disabled by configuration\n");
            return;
        }

        // Warn if binding to non-loopback
        if (host != "127.0.0.1" && host != "::1" && host != "localhost")
        {
            AZ_Warning(
                "AiCompanion",
                false,
                "AgentServer binding to %s — this exposes the server to the network. "
                "Best practices: enable TLS (set TlsEnabled=true with cert/key paths), "
                "use a firewall, restrict to trusted networks, consider SSH tunneling, "
                "or enable secure mode to disable execute_python.",
                host.c_str());
        }

        // Parse log level
        AgentServerLogLevel logLevel = AgentServerLogLevel::Minimal;
        if (logLevelStr == "standard")
        {
            logLevel = AgentServerLogLevel::Standard;
        }
        else if (logLevelStr == "verbose")
        {
            logLevel = AgentServerLogLevel::Verbose;
        }

        AZ::u16 port = static_cast<AZ::u16>(portValue);

        // Determine TLS cert/key paths
        AZStd::string certPath;
        AZStd::string keyPath;
        if (tlsEnabled)
        {
            certPath = tlsCertPath;
            keyPath = tlsKeyPath;
            if (certPath.empty() || keyPath.empty())
            {
                AZ_Warning("AiCompanion", false, "[AgentServer] TLS enabled but cert/key paths not configured. Starting without TLS.");
                certPath.clear();
                keyPath.clear();
            }
        }

        m_agentServer = AZStd::make_unique<AgentServer>();
        if (!m_agentServer->Start(host, port, secureMode, logLevel, certPath, keyPath))
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to start on %s:%u", host.c_str(), port);
            m_agentServer.reset();
        }
    }

    AZ::Outcome<void, AZStd::string> AiCompanionEditorSystemComponent::SetComponentPropertyUnwrapped(
        AZ::EntityComponentIdPair pair, AZStd::string propertyPath, AZStd::any value)
    {
        AZ::Entity* entity = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, pair.GetEntityId());
        if (entity == nullptr)
        {
            return AZ::Failure(AZStd::string("entity not found"));
        }

        AZ::Component* component = entity->FindComponent(pair.GetComponentId());
        if (component == nullptr)
        {
            return AZ::Failure(AZStd::string("component not found on entity"));
        }

        // Unwrap GenericComponentWrapper so PropertyTreeEditor's (instance,
        // type) pair is internally consistent. See AiCompanionEditorRequestBus.h
        // for rationale; mirrors o3de/o3de#19771 locally pending merge.
        void* instance = reinterpret_cast<void*>(component);
        if (auto* wrapper = azrtti_cast<AzToolsFramework::Components::GenericComponentWrapper*>(component))
        {
            if (AZ::Component* tmpl = wrapper->GetTemplate())
            {
                instance = reinterpret_cast<void*>(tmpl);
            }
        }

        AzToolsFramework::PropertyTreeEditor pte(instance, component->GetUnderlyingComponentType());
        const auto outcome = pte.SetProperty(propertyPath, value);
        if (!outcome.IsSuccess())
        {
            return AZ::Failure(outcome.GetError());
        }
        return AZ::Success();
    }

    AZStd::string AiCompanionEditorSystemComponent::GetBusSchema(AZStd::string busName)
    {
        AZ::BehaviorContext* behaviorContext = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(behaviorContext, &AZ::ComponentApplicationRequests::GetBehaviorContext);
        return BuildBusSchemaJson(behaviorContext, busName);
    }

} // namespace AiCompanion
