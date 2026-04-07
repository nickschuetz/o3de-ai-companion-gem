/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion
{
    //! EBus interface for controlling and querying the AgentServer.
    //! The AgentServer provides a TCP listener for AI agent communication,
    //! replacing the need for O3DE's RemoteConsole gem.
    class AgentServerRequests : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Starts the server on the configured host and port.
        virtual bool StartServer() = 0;

        //! Stops the server and disconnects any connected client.
        virtual void StopServer() = 0;

        //! Returns true if the server is currently listening for connections.
        virtual bool IsRunning() const = 0;

        //! Returns the port the server is bound to (0 if not running).
        virtual AZ::u16 GetPort() const = 0;

        //! Returns the host address the server is bound to.
        virtual AZStd::string GetHost() const = 0;

        //! Returns true if a client is currently connected.
        virtual bool HasConnectedClient() const = 0;

        //! Returns true if secure mode is active (execute_python disabled).
        virtual bool IsSecureMode() const = 0;

        //! Enables or disables secure mode at runtime.
        virtual void SetSecureMode(bool enabled) = 0;
    };

    using AgentServerRequestBus = AZ::EBus<AgentServerRequests>;

} // namespace AiCompanion
