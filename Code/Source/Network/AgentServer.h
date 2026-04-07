/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AiCompanion/AgentServerBus.h>
#include <AiCompanion/AiCompanionBus.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/std/containers/deque.h>
#include <AzCore/std/parallel/mutex.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/std/parallel/atomic.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzCore/std/string/string.h>

#include <rapidjson/document.h>

#if AZ_TRAIT_OS_PLATFORM_APPLE || AZ_TRAIT_OS_IS_LINUX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <fcntl.h>
#elif defined(AZ_PLATFORM_WINDOWS)
    #include <WinSock2.h>
    #include <WS2tcpip.h>
#endif

// Forward declare OpenSSL types to avoid header dependency when TLS is not used
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace AiCompanion
{
#if AZ_TRAIT_OS_PLATFORM_APPLE || AZ_TRAIT_OS_IS_LINUX
    using SocketType = int;
    static constexpr SocketType InvalidSocket = -1;
#elif defined(AZ_PLATFORM_WINDOWS)
    using SocketType = SOCKET;
    static constexpr SocketType InvalidSocket = INVALID_SOCKET;
#endif

    //! Log level for audit logging
    enum class AgentServerLogLevel : AZ::u8
    {
        Minimal = 0,   // Server start/stop, client connect/disconnect, errors
        Standard = 1,  // Above + request type, ID, status, duration
        Verbose = 2    // Above + full request/response bodies (truncated)
    };

    //! A pending request queued for main-thread execution
    struct PendingRequest
    {
        AZStd::string id;
        AZStd::string type;
        AZStd::string payload; // base64 script for execute_python, empty for others
        AZStd::promise<AZStd::string> responsePromise;
    };

    //! AgentServer — Purpose-built TCP listener for AI agent communication.
    //! Replaces RemoteConsole with a length-prefixed JSON protocol.
    class AgentServer
        : public AZ::TickBus::Handler
        , public AgentServerRequestBus::Handler
    {
    public:
        AgentServer();
        ~AgentServer();

        //! Starts the server with the given configuration.
        bool Start(const AZStd::string& host, AZ::u16 port, bool secureMode,
                   AgentServerLogLevel logLevel = AgentServerLogLevel::Minimal,
                   const AZStd::string& tlsCertPath = "",
                   const AZStd::string& tlsKeyPath = "");

        //! Stops the server and cleans up all resources.
        void Stop();

        // -- AgentServerRequestBus overrides --
        bool StartServer() override;
        void StopServer() override;
        bool IsRunning() const override;
        AZ::u16 GetPort() const override;
        AZStd::string GetHost() const override;
        bool HasConnectedClient() const override;
        bool IsSecureMode() const override;
        void SetSecureMode(bool enabled) override;

        // -- TickBus --
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        // Background threads
        void AcceptLoop();
        void ClientLoop(SocketType clientSocket, const AZStd::string& clientAddr, AZ::u16 clientPort);

        // Protocol helpers
        bool ReadFramedMessage(SocketType sock, SSL* ssl, AZStd::string& outJson);
        bool SendFramedMessage(SocketType sock, SSL* ssl, const AZStd::string& json);

        // Request handling
        AZStd::string HandleRequest(const AZStd::string& jsonRequest);
        AZStd::string HandlePing(const AZStd::string& id);
        AZStd::string HandleGetApiVersion(const AZStd::string& id);
        AZStd::string HandleGetSceneSnapshot(const AZStd::string& id);
        AZStd::string HandleGetEntityTree(const AZStd::string& id);
        AZStd::string HandleValidateScene(const AZStd::string& id);

        // Response builders
        AZStd::string BuildResponse(const AZStd::string& id, const char* status,
                                     const AZStd::string& output, const AZStd::string& error,
                                     AZ::s64 durationMs);
        AZStd::string BuildErrorResponse(const AZStd::string& id, const AZStd::string& error);

        // Logging helpers
        void LogMinimal(const char* format, ...) const;
        void LogStandard(const char* format, ...) const;
        void LogVerbose(const char* format, ...) const;

        // Socket helpers
        void CloseSocket(SocketType& sock);

        // TLS helpers
        bool InitTls(const AZStd::string& certPath, const AZStd::string& keyPath);
        void ShutdownTls();

        // Configuration
        AZStd::string m_host = "127.0.0.1";
        AZ::u16 m_port = 4600;
        AZStd::atomic_bool m_secureMode{false};
        AgentServerLogLevel m_logLevel = AgentServerLogLevel::Minimal;

        // State
        AZStd::atomic_bool m_running{false};
        SocketType m_listenSocket = InvalidSocket;
        AZStd::atomic<SocketType> m_clientSocket{InvalidSocket};
        AZStd::thread m_acceptThread;
        AZStd::thread m_clientThread;
        AZStd::mutex m_clientMutex;

        // TLS
        SSL_CTX* m_sslCtx = nullptr;
        bool m_tlsEnabled = false;

        // Request queue (client thread → main thread)
        AZStd::mutex m_queueMutex;
        AZStd::deque<AZStd::shared_ptr<PendingRequest>> m_pendingRequests;

        // Protocol constants
        static constexpr AZ::u32 MaxMessageSize = 16 * 1024 * 1024; // 16 MiB
        static constexpr int PollTimeoutMs = 500;
        static constexpr size_t LogTruncateSize = 4096;

#if defined(AZ_PLATFORM_WINDOWS)
        bool m_wsaInitialized = false;
#endif
    };

} // namespace AiCompanion
