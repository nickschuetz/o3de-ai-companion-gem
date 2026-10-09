/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AgentServer.h"
#include "RequestError.h"
#include "RequestParsing.h"
#include "ResponseBuilding.h"

#include <AiCompanion/AiCompanionEditorRequestBus.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Utils/Utils.h>
#include <AzCore/base.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzCore/std/string/conversions.h>
#include <AzToolsFramework/API/EditorPythonRunnerRequestsBus.h>

#include <AzCore/JSON/document.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

#include <chrono>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <sstream>

// TLS support via OpenSSL (linked through AzFramework)
#if AZ_TRAIT_OS_PLATFORM_APPLE || defined(AZ_PLATFORM_LINUX) || defined(AZ_PLATFORM_WINDOWS)
#include <openssl/err.h>
#include <openssl/ssl.h>
#define AI_COMPANION_TLS_AVAILABLE 1
#else
#define AI_COMPANION_TLS_AVAILABLE 0
#endif

#if defined(AZ_PLATFORM_WINDOWS)
#pragma comment(lib, "Ws2_32.lib")
#include <direct.h>
#define AI_COMPANION_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define AI_COMPANION_MKDIR(path) ::mkdir(path, 0700)
#endif

namespace AiCompanion
{
    // -------------------------------------------------------------------------
    // Construction / Destruction
    // -------------------------------------------------------------------------

    AgentServer::AgentServer() = default;

    AgentServer::~AgentServer()
    {
        Stop();
    }

    // -------------------------------------------------------------------------
    // Start / Stop
    // -------------------------------------------------------------------------

    bool AgentServer::Start(
        const AZStd::string& host,
        AZ::u16 port,
        bool secureMode,
        AgentServerLogLevel logLevel,
        const AZStd::string& tlsCertPath,
        const AZStd::string& tlsKeyPath)
    {
        if (m_running.load())
        {
            AZ_Warning("AiCompanion", false, "[AgentServer] Already running on %s:%u", m_host.c_str(), m_port);
            return false;
        }

        m_host = host;
        m_port = port;
        m_secureMode.store(secureMode);
        m_logLevel = logLevel;

#if defined(AZ_PLATFORM_WINDOWS)
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] WSAStartup failed");
            return false;
        }
        m_wsaInitialized = true;
#endif

        // TLS setup
        m_tlsEnabled = false;
        if (!tlsCertPath.empty() && !tlsKeyPath.empty())
        {
            if (!InitTls(tlsCertPath, tlsKeyPath))
            {
                AZ_Error("AiCompanion", false, "[AgentServer] TLS initialization failed");
#if defined(AZ_PLATFORM_WINDOWS)
                WSACleanup();
                m_wsaInitialized = false;
#endif
                return false;
            }
            m_tlsEnabled = true;
        }

        // Create listening socket.
        // Prevent child processes (AssetProcessor, AssetBuilder) from inheriting
        // this socket. Without this, multiple processes share the listen socket
        // and accept() becomes non-deterministic across processes.
#if defined(AZ_PLATFORM_LINUX)
        m_listenSocket = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
#else
        m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
        if (m_listenSocket == InvalidSocket)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to create socket");
            ShutdownTls();
#if defined(AZ_PLATFORM_WINDOWS)
            WSACleanup();
            m_wsaInitialized = false;
#endif
            return false;
        }

#if AZ_TRAIT_OS_PLATFORM_APPLE
        // macOS lacks SOCK_CLOEXEC; set FD_CLOEXEC after creation
        fcntl(m_listenSocket, F_SETFD, FD_CLOEXEC);
#elif defined(AZ_PLATFORM_WINDOWS)
        // On Windows, prevent socket handle inheritance by child processes
        SetHandleInformation(reinterpret_cast<HANDLE>(m_listenSocket), HANDLE_FLAG_INHERIT, 0);
#endif

        // Allow address reuse
        int optVal = 1;
#if defined(AZ_PLATFORM_WINDOWS)
        setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optVal), sizeof(optVal));
#else
        setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
#endif

        // Bind
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        if (inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr) != 1)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Invalid host address: %s", m_host.c_str());
            CloseSocket(m_listenSocket);
            ShutdownTls();
#if defined(AZ_PLATFORM_WINDOWS)
            WSACleanup();
            m_wsaInitialized = false;
#endif
            return false;
        }

        if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to bind to %s:%u", m_host.c_str(), m_port);
            CloseSocket(m_listenSocket);
            ShutdownTls();
#if defined(AZ_PLATFORM_WINDOWS)
            WSACleanup();
            m_wsaInitialized = false;
#endif
            return false;
        }

        if (listen(m_listenSocket, 1) != 0)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to listen on %s:%u", m_host.c_str(), m_port);
            CloseSocket(m_listenSocket);
            ShutdownTls();
#if defined(AZ_PLATFORM_WINDOWS)
            WSACleanup();
            m_wsaInitialized = false;
#endif
            return false;
        }

        m_running.store(true);

        // Connect to buses
        AgentServerRequestBus::Handler::BusConnect();

        // Start accept thread
        m_acceptThread = AZStd::thread(
            [this]()
            {
                AcceptLoop();
            });

        LogMinimal(
            "[AgentServer] Started on %s:%u (secure=%s, tls=%s, log=%s)",
            m_host.c_str(),
            m_port,
            m_secureMode.load() ? "on" : "off",
            m_tlsEnabled ? "on" : "off",
            m_logLevel == AgentServerLogLevel::Minimal        ? "minimal"
                : m_logLevel == AgentServerLogLevel::Standard ? "standard"
                                                              : "verbose");

        return true;
    }

    void AgentServer::Stop()
    {
        if (!m_running.exchange(false))
        {
            return;
        }

        LogMinimal("[AgentServer] Stopping...");

        // Close listen socket to unblock accept()
        CloseSocket(m_listenSocket);

        // Close client socket to unblock recv()
        SocketType clientSock = m_clientSocket.exchange(InvalidSocket);
        if (clientSock != InvalidSocket)
        {
            CloseSocket(clientSock);
        }

        // Join threads
        if (m_acceptThread.joinable())
        {
            m_acceptThread.join();
        }
        if (m_clientThread.joinable())
        {
            m_clientThread.join();
        }

        // Disconnect from buses
        AgentServerRequestBus::Handler::BusDisconnect();

        // Drain any pending requests with error
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
            for (auto& req : m_pendingRequests)
            {
                req->responsePromise.set_value(BuildErrorResponse(req->id, "Server shutting down", RequestError::ShuttingDown));
            }
            m_pendingRequests.clear();
        }

        ShutdownTls();

#if defined(AZ_PLATFORM_WINDOWS)
        if (m_wsaInitialized)
        {
            WSACleanup();
            m_wsaInitialized = false;
        }
#endif

        LogMinimal("[AgentServer] Stopped");
    }

    // -------------------------------------------------------------------------
    // AgentServerRequestBus
    // -------------------------------------------------------------------------

    bool AgentServer::StartServer()
    {
        return Start(m_host, m_port, m_secureMode.load(), m_logLevel);
    }

    void AgentServer::StopServer()
    {
        Stop();
    }

    bool AgentServer::IsRunning() const
    {
        return m_running.load();
    }

    AZ::u16 AgentServer::GetPort() const
    {
        return m_running.load() ? m_port : 0;
    }

    AZStd::string AgentServer::GetHost() const
    {
        return m_host;
    }

    bool AgentServer::HasConnectedClient() const
    {
        return m_clientSocket.load() != InvalidSocket;
    }

    bool AgentServer::IsSecureMode() const
    {
        return m_secureMode.load();
    }

    void AgentServer::SetSecureMode(bool enabled)
    {
        m_secureMode.store(enabled);
        LogMinimal("[AgentServer] Secure mode %s", enabled ? "enabled" : "disabled");
    }

    // -------------------------------------------------------------------------
    // TickBus: Process pending requests on main thread
    // -------------------------------------------------------------------------

    void AgentServer::ProcessMainThreadQueue()
    {
        AZStd::deque<std::shared_ptr<PendingRequest>> localQueue;
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
            localQueue.swap(m_pendingRequests);
        }

        if (!localQueue.empty())
        {
            LogMinimal("[AgentServer] Processing %zu pending request(s) on main thread", localQueue.size());
        }

        for (auto& req : localQueue)
        {
            auto startTime = AZStd::chrono::steady_clock::now();
            AZStd::string response = HandleRequest(req->payload);
            auto endTime = AZStd::chrono::steady_clock::now();
            auto durationMs = static_cast<AZ::s64>(AZStd::chrono::duration_cast<AZStd::chrono::milliseconds>(endTime - startTime).count());

            // Replace the placeholder duration_ms=0 with actual timing.
            // HandleRequest already builds complete JSON via BuildResponse with duration_ms=0,
            // so we do a fast string replacement instead of re-parsing the entire JSON.
            const AZStd::string placeholder = "\"duration_ms\":0}";
            size_t pos = response.rfind(placeholder);
            if (pos != AZStd::string::npos)
            {
                AZStd::string replacement = AZStd::string::format("\"duration_ms\":%lld}", static_cast<long long>(durationMs));
                response.replace(pos, placeholder.size(), replacement);
            }

            LogStandard(
                "[AgentServer] req=%s type=%s duration=%lldms", req->id.c_str(), req->type.c_str(), static_cast<long long>(durationMs));

            req->responsePromise.set_value(AZStd::move(response));
        }
    }

    // -------------------------------------------------------------------------
    // Accept Loop (background thread)
    // -------------------------------------------------------------------------

    void AgentServer::AcceptLoop()
    {
        while (m_running.load())
        {
#if defined(AZ_PLATFORM_WINDOWS)
            WSAPOLLFD pfd{};
#else
            pollfd pfd{};
#endif
            pfd.fd = m_listenSocket;
            pfd.events = POLLIN;

#if defined(AZ_PLATFORM_WINDOWS)
            int pollResult = WSAPoll(&pfd, 1, PollTimeoutMs);
#else
            int pollResult = poll(&pfd, 1, PollTimeoutMs);
#endif

            if (pollResult <= 0)
            {
                continue; // timeout or error: check m_running
            }

            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
#if defined(AZ_PLATFORM_LINUX)
            // Use accept4 with SOCK_CLOEXEC to prevent child process inheritance
            SocketType clientSock = accept4(m_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen, SOCK_CLOEXEC);
#else
            SocketType clientSock = accept(m_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
#endif

            if (clientSock == InvalidSocket)
            {
                continue; // likely shutting down
            }

#if AZ_TRAIT_OS_PLATFORM_APPLE
            // macOS lacks accept4; set FD_CLOEXEC after accept to prevent child inheritance
            fcntl(clientSock, F_SETFD, FD_CLOEXEC);
#elif defined(AZ_PLATFORM_WINDOWS)
            // Prevent accepted socket from being inherited by child processes
            SetHandleInformation(reinterpret_cast<HANDLE>(clientSock), HANDLE_FLAG_INHERIT, 0);
#endif

            // Extract client address for logging
            char addrBuf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
            AZ::u16 clientPort = ntohs(clientAddr.sin_port);
            AZStd::string clientAddrStr(addrBuf);

            // Single-client policy: reject if another client is connected
            // But first check if the existing connection is stale (CLOSE-WAIT)
            {
                AZStd::lock_guard<AZStd::mutex> lock(m_clientMutex);
                SocketType existingSocket = m_clientSocket.load();
                if (existingSocket != InvalidSocket)
                {
                    // Probe the existing socket to detect stale connections.
                    // Use poll with zero timeout (non-blocking) to avoid blocking the accept loop.
                    bool isStale = false;
#if defined(AZ_PLATFORM_WINDOWS)
                    WSAPOLLFD probePfd{};
                    probePfd.fd = existingSocket;
                    probePfd.events = POLLIN;
                    int pollRet = WSAPoll(&probePfd, 1, 0);
#else
                    pollfd probePfd{};
                    probePfd.fd = existingSocket;
                    probePfd.events = POLLIN;
                    int pollRet = poll(&probePfd, 1, 0);
#endif
                    if (pollRet > 0)
                    {
                        if (probePfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                        {
                            isStale = true;
                        }
                        else if (probePfd.revents & POLLIN)
                        {
                            // Data or FIN available: peek to distinguish
                            char probeBuf;
                            int probeResult;
#if defined(AZ_PLATFORM_WINDOWS)
                            probeResult = recv(existingSocket, &probeBuf, 1, MSG_PEEK);
#else
                            probeResult = static_cast<int>(recv(existingSocket, &probeBuf, 1, MSG_PEEK | MSG_DONTWAIT));
#endif
                            if (probeResult == 0)
                            {
                                isStale = true; // FIN received: peer closed
                            }
                        }
                    }
                    if (!isStale)
                    {
                        LogMinimal(
                            "[AgentServer] Rejected connection from %s:%u: another client is connected", clientAddrStr.c_str(), clientPort);
                        CloseSocket(clientSock);
                        continue;
                    }

                    // Stale connection detected: force cleanup
                    LogMinimal("[AgentServer] Detected stale connection, cleaning up for new client");
                    SocketType staleSock = existingSocket;
                    CloseSocket(staleSock);
                    m_clientSocket.store(InvalidSocket);
                }
            }

            LogMinimal(
                "[AgentServer] Client connected from %s:%u (TLS: %s)", clientAddrStr.c_str(), clientPort, m_tlsEnabled ? "yes" : "no");

            // Wait for any previous client thread to finish
            if (m_clientThread.joinable())
            {
                m_clientThread.join();
            }

            m_clientSocket.store(clientSock);
            m_clientThread = AZStd::thread(
                [this, clientSock, clientAddrStr, clientPort]()
                {
                    ClientLoop(clientSock, clientAddrStr, clientPort);
                });
        }
    }

    // -------------------------------------------------------------------------
    // Client Loop (background thread)
    // -------------------------------------------------------------------------

    void AgentServer::ClientLoop(SocketType clientSocket, const AZStd::string& clientAddr, AZ::u16 clientPort)
    {
        SSL* ssl = nullptr;

#if AI_COMPANION_TLS_AVAILABLE
        if (m_tlsEnabled && m_sslCtx)
        {
            ssl = SSL_new(m_sslCtx);
            if (!ssl)
            {
                AZ_Error("AiCompanion", false, "[AgentServer] SSL_new failed");
                CloseSocket(clientSocket);
                m_clientSocket.store(InvalidSocket);
                return;
            }

            SSL_set_fd(ssl, static_cast<int>(clientSocket));
            if (SSL_accept(ssl) <= 0)
            {
                AZ_Error("AiCompanion", false, "[AgentServer] TLS handshake failed with %s:%u", clientAddr.c_str(), clientPort);
                LogVerbose("[AgentServer] TLS error: %s", ERR_error_string(ERR_get_error(), nullptr));
                SSL_free(ssl);
                CloseSocket(clientSocket);
                m_clientSocket.store(InvalidSocket);
                return;
            }
            LogVerbose("[AgentServer] TLS handshake complete with %s:%u, cipher=%s", clientAddr.c_str(), clientPort, SSL_get_cipher(ssl));
        }
#endif

        while (m_running.load())
        {
            AZStd::string requestJson;
            if (!ReadFramedMessage(clientSocket, ssl, requestJson))
            {
                break; // connection closed or error
            }

            LogVerbose(
                "[AgentServer] Received: %.4096s%s", requestJson.c_str(), requestJson.size() > LogTruncateSize ? "...(truncated)" : "");

            // Parse to extract type and determine dispatch
            rapidjson::Document doc;
            doc.Parse(requestJson.c_str());

            if (doc.HasParseError() || !doc.IsObject())
            {
                AZStd::string errorResp = BuildErrorResponse("", "Invalid JSON", RequestError::ValidationFailed);
                SendFramedMessage(clientSocket, ssl, errorResp);
                continue;
            }

            AZStd::string id = doc.HasMember("id") && doc["id"].IsString() ? doc["id"].GetString() : "";
            AZStd::string type = doc.HasMember("type") && doc["type"].IsString() ? doc["type"].GetString() : "";

            // Sanitize request ID: allow only alphanumeric, hyphen, underscore (max 64 chars).
            // Prevents path traversal when IDs are used in temp file paths or format strings.
            if (id.size() > 64)
            {
                id.resize(64);
            }
            for (char& c : id)
            {
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
                {
                    c = '_';
                }
            }

            if (type.empty())
            {
                AZStd::string errorResp = BuildErrorResponse(id, "Missing 'type' field", RequestError::ValidationFailed);
                SendFramedMessage(clientSocket, ssl, errorResp);
                continue;
            }

            AZStd::string response;

            // Handle lightweight requests directly on client thread
            if (type == "ping")
            {
                response = HandlePing(id);
                LogStandard("[AgentServer] req=%s type=ping status=ok duration=0ms", id.c_str());
            }
            else if (type == "get_api_version")
            {
                response = HandleGetApiVersion(id);
                LogStandard("[AgentServer] req=%s type=get_api_version status=ok duration=0ms", id.c_str());
            }
            else if (
                type == "get_scene_snapshot" || type == "get_entity_tree" || type == "validate_scene" || type == "get_entity" ||
                type == "get_bus_schema" || type == "create_entity" || type == "set_transform" || type == "delete_entity" ||
                type == "list_anim_graphs" || type == "get_anim_graph" || type == "create_anim_graph" || type == "remove_anim_graph" ||
                type == "load_anim_graph" || type == "save_anim_graph" || type == "add_anim_graph_node" ||
                type == "remove_anim_graph_node" || type == "set_anim_graph_entry_state" || type == "add_anim_graph_parameter" ||
                type == "remove_anim_graph_parameter" || type == "add_anim_graph_transition" || type == "remove_anim_graph_transition" ||
                type == "set_anim_graph_transition" || type == "connect_anim_graph_ports" || type == "disconnect_anim_graph_ports" ||
                type == "set_anim_graph_node" || type == "get_asset_status" || type == "get_asset_jobs" ||
                type == "get_asset_processor_status")
            {
                // Safe EBus calls: dispatch to main thread
                auto pending = std::make_shared<PendingRequest>();
                pending->id = id;
                pending->type = type;
                pending->payload = requestJson;
                auto future = pending->responsePromise.get_future();

                {
                    AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
                    m_pendingRequests.push_back(pending);
                }

                // Wait with timeout to prevent deadlock if main thread doesn't process
                auto waitStatus = future.wait_for(std::chrono::seconds(30));
                if (waitStatus == std::future_status::ready)
                {
                    response = future.get();
                }
                else
                {
                    response = BuildErrorResponse(
                        id,
                        "Request timed out waiting for main thread dispatch. "
                        "Ensure the editor is running and not blocked.",
                        RequestError::Timeout);
                    AZ_Warning(
                        "AiCompanion",
                        false,
                        "[AgentServer] Request %s (type=%s) timed out waiting for main thread",
                        id.c_str(),
                        type.c_str());
                }
            }
            else if (type == "execute_python")
            {
                if (m_secureMode.load())
                {
                    response = BuildErrorResponse(
                        id,
                        "execute_python is disabled in secure mode. "
                        "Only ping, get_api_version, get_scene_snapshot, get_entity_tree, validate_scene, "
                        "get_entity, get_bus_schema, create_entity, set_transform, delete_entity, "
                        "list_anim_graphs, get_anim_graph, create_anim_graph, remove_anim_graph, load_anim_graph, "
                        "save_anim_graph, add_anim_graph_node, remove_anim_graph_node, set_anim_graph_entry_state, "
                        "add_anim_graph_parameter, remove_anim_graph_parameter, add_anim_graph_transition, "
                        "remove_anim_graph_transition, set_anim_graph_transition, connect_anim_graph_ports, "
                        "disconnect_anim_graph_ports, set_anim_graph_node, get_asset_status, get_asset_jobs and "
                        "get_asset_processor_status are available.",
                        RequestError::SecureMode);
                    AZ_Warning("AiCompanion", false, "[AgentServer] Blocked execute_python in secure mode (req=%s)", id.c_str());
                }
                else
                {
                    // Dispatch to main thread
                    auto pending = std::make_shared<PendingRequest>();
                    pending->id = id;
                    pending->type = type;
                    pending->payload = requestJson;
                    auto future = pending->responsePromise.get_future();

                    {
                        AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
                        m_pendingRequests.push_back(pending);
                    }

                    // Wait with timeout to prevent deadlock if main thread doesn't process
                    auto waitStatus = future.wait_for(std::chrono::seconds(30));
                    if (waitStatus == std::future_status::ready)
                    {
                        response = future.get();
                    }
                    else
                    {
                        response = BuildErrorResponse(
                            id,
                            "Request timed out waiting for main thread dispatch. "
                            "Ensure the editor is running and not blocked.",
                            RequestError::Timeout);
                        AZ_Warning(
                            "AiCompanion",
                            false,
                            "[AgentServer] Request %s (type=execute_python) timed out waiting for main thread",
                            id.c_str());
                    }
                }
            }
            else
            {
                // Keep the message text: older clients match on it. The code is
                // the one clients fall back to editor Python on.
                response = BuildErrorResponse(
                    id, AZStd::string::format("Unknown request type: %s", type.c_str()), ResponseBuilding::UnknownRequestTypeCode);
            }

            LogVerbose("[AgentServer] Sending: %.4096s%s", response.c_str(), response.size() > LogTruncateSize ? "...(truncated)" : "");

            if (!SendFramedMessage(clientSocket, ssl, response))
            {
                break; // send failed: connection likely broken
            }
        }

        // Cleanup
#if AI_COMPANION_TLS_AVAILABLE
        if (ssl)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
#endif

        LogMinimal("[AgentServer] Client disconnected: %s:%u", clientAddr.c_str(), clientPort);
        CloseSocket(clientSocket);
        m_clientSocket.store(InvalidSocket);
    }

    // -------------------------------------------------------------------------
    // Protocol: Framed message I/O
    // -------------------------------------------------------------------------

    bool AgentServer::ReadFramedMessage(SocketType sock, SSL* ssl, AZStd::string& outJson)
    {
        // Read 4-byte big-endian length header
        AZ::u8 header[4];
        size_t headerRead = 0;

        while (headerRead < 4)
        {
            int n;
#if AI_COMPANION_TLS_AVAILABLE
            if (ssl)
            {
                n = SSL_read(ssl, header + headerRead, static_cast<int>(4 - headerRead));
            }
            else
#endif
            {
#if defined(AZ_PLATFORM_WINDOWS)
                n = recv(sock, reinterpret_cast<char*>(header + headerRead), static_cast<int>(4 - headerRead), 0);
#else
                n = static_cast<int>(recv(sock, header + headerRead, 4 - headerRead, 0));
#endif
            }

            if (n <= 0)
            {
                return false; // connection closed or error
            }
            headerRead += static_cast<size_t>(n);
        }

        AZ::u32 length = (static_cast<AZ::u32>(header[0]) << 24) | (static_cast<AZ::u32>(header[1]) << 16) |
            (static_cast<AZ::u32>(header[2]) << 8) | (static_cast<AZ::u32>(header[3]));

        if (length > MaxMessageSize)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Message too large: %u bytes (max %u)", length, MaxMessageSize);
            return false;
        }

        // Read body
        outJson.resize(length);
        size_t bodyRead = 0;
        while (bodyRead < length)
        {
            int n;
            size_t remaining = length - bodyRead;
            int chunkSize = static_cast<int>(AZStd::min(remaining, static_cast<size_t>(8192)));

#if AI_COMPANION_TLS_AVAILABLE
            if (ssl)
            {
                n = SSL_read(ssl, outJson.data() + bodyRead, chunkSize);
            }
            else
#endif
            {
#if defined(AZ_PLATFORM_WINDOWS)
                n = recv(sock, outJson.data() + bodyRead, chunkSize, 0);
#else
                n = static_cast<int>(recv(sock, outJson.data() + bodyRead, chunkSize, 0));
#endif
            }

            if (n <= 0)
            {
                return false;
            }
            bodyRead += static_cast<size_t>(n);
        }

        return true;
    }

    bool AgentServer::SendFramedMessage(SocketType sock, SSL* ssl, const AZStd::string& json)
    {
        AZ::u32 length = static_cast<AZ::u32>(json.size());
        AZ::u8 header[4] = { static_cast<AZ::u8>((length >> 24) & 0xFF),
                             static_cast<AZ::u8>((length >> 16) & 0xFF),
                             static_cast<AZ::u8>((length >> 8) & 0xFF),
                             static_cast<AZ::u8>(length & 0xFF) };

        // Send header
        auto sendBytes = [&](const void* data, int len) -> bool
        {
            int sent = 0;
            while (sent < len)
            {
                int n;
#if AI_COMPANION_TLS_AVAILABLE
                if (ssl)
                {
                    n = SSL_write(ssl, static_cast<const char*>(data) + sent, len - sent);
                }
                else
#endif
                {
#if defined(AZ_PLATFORM_WINDOWS)
                    n = send(sock, static_cast<const char*>(data) + sent, len - sent, 0);
#else
                    n = static_cast<int>(::send(sock, static_cast<const char*>(data) + sent, len - sent, 0));
#endif
                }
                if (n <= 0)
                {
                    return false;
                }
                sent += n;
            }
            return true;
        };

        return sendBytes(header, 4) && sendBytes(json.c_str(), static_cast<int>(length));
    }

    // -------------------------------------------------------------------------
    // Request Handling
    // -------------------------------------------------------------------------

    namespace
    {
        //! The failure a bus event answers when nothing handles it: a
        //! BroadcastResult leaves the default outcome untouched.
        AZStd::string EditorBusNoHandler()
        {
            return RequestError::EncodeError(
                RequestError::Unavailable, "AiCompanionEditorRequestBus has no handler; is the editor system component active?");
        }

        //! True when a native producer answered its own {"error": "..."}
        //! object instead of a result (SceneSnapshotProvider::CaptureEntity,
        //! BuildBusSchemaJson). The message is the object's "error"; the code
        //! is its "code" member when the producer wrote one, else not_found.
        bool IsErrorObject(const AZStd::string& json, AZStd::string& outCode, AZStd::string& outMessage)
        {
            rapidjson::Document doc;
            doc.Parse(json.c_str(), json.size());
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("error") || !doc["error"].IsString())
            {
                return false;
            }
            outMessage.assign(doc["error"].GetString(), doc["error"].GetStringLength());
            if (doc.HasMember("code") && doc["code"].IsString())
            {
                outCode.assign(doc["code"].GetString(), doc["code"].GetStringLength());
            }
            else
            {
                outCode = RequestError::NotFound;
            }
            return true;
        }
    } // namespace

    AZStd::string AgentServer::HandleRequest(const AZStd::string& jsonRequest)
    {
        rapidjson::Document doc;
        doc.Parse(jsonRequest.c_str());

        if (doc.HasParseError() || !doc.IsObject())
        {
            return BuildErrorResponse("", "Invalid JSON request", RequestError::ValidationFailed);
        }

        AZStd::string id = doc.HasMember("id") && doc["id"].IsString() ? doc["id"].GetString() : "";
        AZStd::string type = doc.HasMember("type") && doc["type"].IsString() ? doc["type"].GetString() : "";

        if (type == "get_scene_snapshot")
        {
            return HandleGetSceneSnapshot(id);
        }
        else if (type == "get_entity_tree")
        {
            return HandleGetEntityTree(id);
        }
        else if (type == "validate_scene")
        {
            return HandleValidateScene(id);
        }
        else if (type == "get_entity")
        {
            return HandleGetEntity(id, doc);
        }
        else if (type == "get_bus_schema")
        {
            return HandleGetBusSchema(id, doc);
        }
        else if (type == "create_entity")
        {
            return HandleCreateEntity(id, doc);
        }
        else if (type == "set_transform")
        {
            return HandleSetTransform(id, doc);
        }
        else if (type == "delete_entity")
        {
            return HandleDeleteEntity(id, doc);
        }
        else if (type == "list_anim_graphs")
        {
            return HandleListAnimGraphs(id);
        }
        else if (type == "get_anim_graph")
        {
            return HandleGetAnimGraph(id, doc);
        }
        else if (type == "create_anim_graph")
        {
            return HandleCreateAnimGraph(id);
        }
        else if (type == "remove_anim_graph")
        {
            return HandleRemoveAnimGraph(id, doc);
        }
        else if (type == "load_anim_graph")
        {
            return HandleLoadAnimGraph(id, doc);
        }
        else if (type == "save_anim_graph")
        {
            return HandleSaveAnimGraph(id, doc);
        }
        else if (type == "add_anim_graph_node")
        {
            return HandleAddAnimGraphNode(id, doc, jsonRequest);
        }
        else if (type == "remove_anim_graph_node")
        {
            return HandleRemoveAnimGraphNode(id, doc);
        }
        else if (type == "set_anim_graph_entry_state")
        {
            return HandleSetAnimGraphEntryState(id, doc);
        }
        else if (type == "add_anim_graph_parameter")
        {
            return HandleAddAnimGraphParameter(id, doc, jsonRequest);
        }
        else if (type == "remove_anim_graph_parameter")
        {
            return HandleRemoveAnimGraphParameter(id, doc);
        }
        else if (type == "add_anim_graph_transition")
        {
            return HandleAnimGraphJsonRequest(
                id, doc, jsonRequest, "add_anim_graph_transition", &AiCompanionEditorRequestBus::Events::AddAnimGraphTransition);
        }
        else if (type == "remove_anim_graph_transition")
        {
            return HandleRemoveAnimGraphTransition(id, doc);
        }
        else if (type == "set_anim_graph_transition")
        {
            return HandleAnimGraphJsonRequest(
                id, doc, jsonRequest, "set_anim_graph_transition", &AiCompanionEditorRequestBus::Events::SetAnimGraphTransition);
        }
        else if (type == "connect_anim_graph_ports")
        {
            return HandleAnimGraphJsonRequest(
                id, doc, jsonRequest, "connect_anim_graph_ports", &AiCompanionEditorRequestBus::Events::ConnectAnimGraphPorts);
        }
        else if (type == "disconnect_anim_graph_ports")
        {
            return HandleAnimGraphJsonRequest(
                id, doc, jsonRequest, "disconnect_anim_graph_ports", &AiCompanionEditorRequestBus::Events::DisconnectAnimGraphPorts);
        }
        else if (type == "set_anim_graph_node")
        {
            return HandleAnimGraphJsonRequest(
                id, doc, jsonRequest, "set_anim_graph_node", &AiCompanionEditorRequestBus::Events::SetAnimGraphNode);
        }
        else if (type == "get_asset_status")
        {
            return HandleGetAssetStatus(id, doc);
        }
        else if (type == "get_asset_jobs")
        {
            return HandleGetAssetJobs(id, doc);
        }
        else if (type == "get_asset_processor_status")
        {
            return HandleGetAssetProcessorStatus(id);
        }
        else if (type == "execute_python")
        {
            // Decode base64 script
            if (!doc.HasMember("script") || !doc["script"].IsString())
            {
                return BuildErrorResponse(id, "Missing 'script' field for execute_python", RequestError::ValidationFailed);
            }

            AZStd::string b64Script = doc["script"].GetString();

            // Decode base64 using a 256-byte lookup table for O(1) per character
            static const AZ::s8 b64Lookup[] = {
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0-15
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 16-31
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, // 32-47 (+,/)
                52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, // 48-63 (0-9)
                -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, // 64-79 (A-O)
                15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, // 80-95 (P-Z)
                -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // 96-111 (a-o)
                41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, // 112-127 (p-z)
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 128-255
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
            };

            AZStd::string decoded;
            decoded.reserve(b64Script.size() * 3 / 4);

            AZ::u32 val = 0;
            int bits = -8;
            for (char c : b64Script)
            {
                AZ::s8 idx = b64Lookup[static_cast<AZ::u8>(c)];
                if (idx < 0)
                {
                    continue; // skip '=', whitespace, invalid chars
                }
                val = (val << 6) + static_cast<AZ::u32>(idx);
                bits += 6;
                if (bits >= 0)
                {
                    decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
                    bits -= 8;
                }
            }

            if (decoded.empty())
            {
                return BuildErrorResponse(id, "Failed to decode base64 script", RequestError::ValidationFailed);
            }

            // Encode the decoded script as a Python bytes literal using hex escaping.
            // This is injection-proof: every byte becomes \xHH, so no character in the
            // user script can break out of the string literal.
            AZStd::string hexScript;
            hexScript.reserve(decoded.size() * 4 + 2);
            hexScript += "b'";
            static const char hexDigits[] = "0123456789abcdef";
            for (char c : decoded)
            {
                hexScript += "\\x";
                hexScript += hexDigits[static_cast<AZ::u8>(c) >> 4];
                hexScript += hexDigits[static_cast<AZ::u8>(c) & 0x0F];
            }
            hexScript += "'.decode('utf-8')";

            // Determine result file path. Use the O3DE project temp directory.
            AZStd::string tempDir;
            {
                AZ::IO::FixedMaxPathString projectRoot = AZ::Utils::GetProjectPath();
                if (!projectRoot.empty())
                {
                    AZ::IO::FixedMaxPath tempPath = AZ::IO::FixedMaxPath(projectRoot.c_str()) / "user" / "temp";
                    tempDir = tempPath.String().c_str();
                }
                else
                {
                    auto appRootOpt = AZ::Utils::GetDefaultAppRootPath();
                    tempDir = appRootOpt.has_value() ? AZStd::string(appRootOpt->c_str()) : AZStd::string(".");
                }
            }
            AZStd::string resultPath = AZStd::string::format("%s/_ai_companion_result_%s.json", tempDir.c_str(), id.c_str());

            // Ensure the temp directory exists (create parent dirs one by one)
            AZ::IO::FixedMaxPath dirPath(tempDir.c_str());
            AZStd::string dirStr = dirPath.String().c_str();
            for (size_t pos = dirStr.find('/'); pos != AZStd::string::npos; pos = dirStr.find('/', pos + 1))
            {
                AZStd::string partial = dirStr.substr(0, pos);
                if (!partial.empty())
                {
                    AI_COMPANION_MKDIR(partial.c_str());
                }
            }
            AI_COMPANION_MKDIR(dirStr.c_str());

            // Remove any pre-existing file at this path (mitigates symlink attacks)
            remove(resultPath.c_str());

            // Single script: capture stdout/stderr, execute user script, write result to file.
            // This avoids dependency on sys state persisting between ExecuteByString calls.
            AZStd::string wrappedScript = AZStd::string::format(
                "import sys, io as _io, json as _ac_json, os, traceback\n"
                "_ac_buf = _io.StringIO()\n"
                "_ac_old_stdout, _ac_old_stderr = sys.stdout, sys.stderr\n"
                "sys.stdout = sys.stderr = _ac_buf\n"
                "_ac_error = ''\n"
                "try:\n"
                "    exec(compile(%s, '<agent>', 'exec'))\n"
                "except Exception as _ac_e:\n"
                "    _ac_error = traceback.format_exc()\n"
                "    _ac_buf.write(_ac_error)\n"
                "finally:\n"
                "    sys.stdout, sys.stderr = _ac_old_stdout, _ac_old_stderr\n"
                "    _ac_result = _ac_json.dumps({'output': _ac_buf.getvalue(), 'error': _ac_error})\n"
                "    fd = os.open(r'%s', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)\n"
                "    with os.fdopen(fd, 'w') as _ac_f:\n"
                "        _ac_f.write(_ac_result)\n",
                hexScript.c_str(),
                resultPath.c_str());

            // Execute on main thread (we're already on main thread when called from OnTick)
            AzToolsFramework::EditorPythonRunnerRequestBus::Broadcast(
                &AzToolsFramework::EditorPythonRunnerRequestBus::Events::ExecuteByString, wrappedScript.c_str(), false);

            // Read the result file. A traceback in it means the script raised;
            // no file at all means the Python runner never ran the wrapper.
            AZStd::string output;
            AZStd::string error;
            const char* code = RequestError::ExecutionFailed;

            AZStd::string resultContent;
            {
                std::ifstream resultStream(resultPath.c_str(), std::ios::binary);
                if (resultStream.is_open())
                {
                    std::ostringstream ss;
                    ss << resultStream.rdbuf();
                    resultContent = ss.str().c_str();
                }
            }
            if (!resultContent.empty())
            {
                rapidjson::Document resultDoc;
                resultDoc.Parse(resultContent.c_str());
                if (!resultDoc.HasParseError() && resultDoc.IsObject())
                {
                    if (resultDoc.HasMember("output") && resultDoc["output"].IsString())
                    {
                        output = resultDoc["output"].GetString();
                    }
                    if (resultDoc.HasMember("error") && resultDoc["error"].IsString())
                    {
                        error = resultDoc["error"].GetString();
                    }
                }
            }
            else
            {
                error = "Failed to retrieve script execution result; is the editor's Python runner ready?";
                code = RequestError::Unavailable;
            }

            // Clean up the result file regardless of success/failure.
            remove(resultPath.c_str());

            if (!error.empty())
            {
                return ResponseBuilding::BuildResponse(id, "error", output, error, 0, code);
            }
            return BuildResponse(id, "ok", output, "", 0);
        }

        // A type the dispatch list accepts but HandleRequest does not know is a
        // server bug, not a missing request type: engine_error keeps clients
        // from treating it as an older gem and falling back to Python.
        return BuildErrorResponse(id, AZStd::string::format("Unhandled type in main thread: %s", type.c_str()), RequestError::EngineError);
    }

    AZStd::string AgentServer::HandlePing(const AZStd::string& id)
    {
        return BuildResponse(id, "ok", "pong", "", 0);
    }

    AZStd::string AgentServer::HandleGetApiVersion(const AZStd::string& id)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("protocol_version");
        w.Int(1);
        w.Key("gem_version");
        w.String("0.6.0");
        w.Key("api_version");
        w.String("0.4.0");
        w.Key("secure_mode");
        w.Bool(m_secureMode.load());
        w.Key("tls_enabled");
        w.Bool(m_tlsEnabled);
        w.EndObject();

        return BuildResponse(id, "ok", sb.GetString(), "", 0);
    }

    AZStd::string AgentServer::HandleGetSceneSnapshot(const AZStd::string& id)
    {
        AZStd::string snapshot;
        AiCompanionRequestBus::BroadcastResult(snapshot, &AiCompanionRequestBus::Events::GetSceneSnapshot);
        return BuildResponse(id, "ok", snapshot, "", 0);
    }

    AZStd::string AgentServer::HandleGetEntityTree(const AZStd::string& id)
    {
        AZStd::string tree;
        AiCompanionRequestBus::BroadcastResult(tree, &AiCompanionRequestBus::Events::GetEntityTree);
        return BuildResponse(id, "ok", tree, "", 0);
    }

    AZStd::string AgentServer::HandleValidateScene(const AZStd::string& id)
    {
        AZStd::string report;
        AiCompanionRequestBus::BroadcastResult(report, &AiCompanionRequestBus::Events::ValidateScene);
        return BuildResponse(id, "ok", report, "", 0);
    }

    AZStd::string AgentServer::HandleGetEntity(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u64 entityId = 0;
        if (!doc.HasMember("entity_id") || !RequestParsing::ParseEntityId(doc["entity_id"], entityId))
        {
            return BuildErrorResponse(
                id, "get_entity requires 'entity_id' (a decimal id, as a number or string)", RequestError::ValidationFailed);
        }

        AZStd::string json;
        AiCompanionRequestBus::BroadcastResult(json, &AiCompanionRequestBus::Events::GetEntity, entityId);
        if (json.empty())
        {
            return BuildErrorResponse(
                id, "AiCompanionRequestBus has no handler; is the gem's system component active?", RequestError::Unavailable);
        }
        AZStd::string code;
        AZStd::string message;
        if (IsErrorObject(json, code, message))
        {
            return BuildErrorResponse(id, message, code.c_str());
        }
        return BuildResponse(id, "ok", json, "", 0);
    }

    AZStd::string AgentServer::HandleGetBusSchema(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZStd::string busName;
        if (doc.HasMember("bus_name") && doc["bus_name"].IsString())
        {
            busName = doc["bus_name"].GetString();
        }

        AZStd::string json;
        AiCompanionEditorRequestBus::BroadcastResult(json, &AiCompanionEditorRequestBus::Events::GetBusSchema, busName);
        if (json.empty())
        {
            return FailureResponse(id, EditorBusNoHandler());
        }
        AZStd::string code;
        AZStd::string message;
        if (IsErrorObject(json, code, message))
        {
            return BuildErrorResponse(id, message, code.c_str());
        }
        return BuildResponse(id, "ok", json, "", 0);
    }

    AZStd::string AgentServer::HandleCreateEntity(const AZStd::string& id, const rapidjson::Document& doc)
    {
        if (!doc.HasMember("name") || !doc["name"].IsString())
        {
            return BuildErrorResponse(id, "create_entity requires 'name'", RequestError::ValidationFailed);
        }
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        if (doc.HasMember("position") && !RequestParsing::ParseVector3(doc["position"], position))
        {
            return BuildErrorResponse(
                id, "create_entity 'position' must be [x, y, z] within the position bound", RequestError::ValidationFailed);
        }
        AZ::u64 parentId = 0;
        if (doc.HasMember("parent_id") && !doc["parent_id"].IsNull() && !RequestParsing::ParseEntityId(doc["parent_id"], parentId))
        {
            return BuildErrorResponse(id, "create_entity 'parent_id' must be a decimal entity id", RequestError::ValidationFailed);
        }

        AZ::Outcome<AZ::u64, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::CreateEntity, AZStd::string(doc["name"].GetString()), position, parentId);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }

        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("entity_id");
        // Decimal string: 64-bit ids exceed 2^53 (see SceneSnapshotProvider).
        w.String(AZStd::string::format("%llu", static_cast<unsigned long long>(outcome.GetValue())).c_str());
        w.Key("name");
        w.String(doc["name"].GetString());
        w.Key("position");
        w.StartArray();
        w.Double(position.GetX());
        w.Double(position.GetY());
        w.Double(position.GetZ());
        w.EndArray();
        w.EndObject();
        return BuildResponse(id, "ok", sb.GetString(), "", 0);
    }

    AZStd::string AgentServer::HandleSetTransform(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u64 entityId = 0;
        if (!doc.HasMember("entity_id") || !RequestParsing::ParseEntityId(doc["entity_id"], entityId))
        {
            return BuildErrorResponse(id, "set_transform requires 'entity_id'", RequestError::ValidationFailed);
        }
        AZ::Vector3 position = AZ::Vector3::CreateZero();
        AZ::Vector3 rotation = AZ::Vector3::CreateZero();
        float scale = 1.0f;
        const bool setPosition = doc.HasMember("position");
        const bool setRotation = doc.HasMember("rotation");
        const bool setScale = doc.HasMember("scale");
        if (!setPosition && !setRotation && !setScale)
        {
            return BuildErrorResponse(
                id,
                "set_transform needs at least one of 'position', 'rotation' (Euler degrees) or 'scale'",
                RequestError::ValidationFailed);
        }
        if (setPosition && !RequestParsing::ParseVector3(doc["position"], position))
        {
            return BuildErrorResponse(
                id, "set_transform 'position' must be [x, y, z] within the position bound", RequestError::ValidationFailed);
        }
        if (setRotation && !RequestParsing::ParseVector3(doc["rotation"], rotation))
        {
            return BuildErrorResponse(id, "set_transform 'rotation' must be [x, y, z] Euler degrees", RequestError::ValidationFailed);
        }
        if (setScale)
        {
            if (!doc["scale"].IsNumber())
            {
                return BuildErrorResponse(id, "set_transform 'scale' must be a number", RequestError::ValidationFailed);
            }
            scale = static_cast<float>(doc["scale"].GetDouble());
        }

        AZ::Outcome<void, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome,
            &AiCompanionEditorRequestBus::Events::SetTransform,
            entityId,
            setPosition,
            position,
            setRotation,
            rotation,
            setScale,
            scale);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        AZStd::string entityJson;
        AiCompanionRequestBus::BroadcastResult(entityJson, &AiCompanionRequestBus::Events::GetEntity, entityId);
        return BuildResponse(id, "ok", entityJson, "", 0);
    }

    AZStd::string AgentServer::HandleDeleteEntity(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u64 entityId = 0;
        if (!doc.HasMember("entity_id") || !RequestParsing::ParseEntityId(doc["entity_id"], entityId))
        {
            return BuildErrorResponse(id, "delete_entity requires 'entity_id'", RequestError::ValidationFailed);
        }
        AZ::Outcome<void, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::DeleteEntity, entityId);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("deleted");
        w.String(AZStd::string::format("%llu", static_cast<unsigned long long>(entityId)).c_str());
        w.EndObject();
        return BuildResponse(id, "ok", sb.GetString(), "", 0);
    }

    AZStd::string AgentServer::HandleListAnimGraphs(const AZStd::string& id)
    {
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::ListAnimGraphs);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleGetAnimGraph(const AZStd::string& id, const rapidjson::Document& doc)
    {
        // The selector handed to the inspector is the decimal id when
        // anim_graph_id is given, else the file name.
        AZStd::string selector;
        if (doc.HasMember("anim_graph_id") && !doc["anim_graph_id"].IsNull())
        {
            AZ::u32 animGraphId = 0;
            if (!RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
            {
                return BuildErrorResponse(
                    id, "get_anim_graph 'anim_graph_id' must be a decimal id, as a number or string", RequestError::ValidationFailed);
            }
            selector = AZStd::string::format("%u", animGraphId);
        }
        else if (doc.HasMember("file_name") && doc["file_name"].IsString() && doc["file_name"].GetStringLength() > 0)
        {
            selector = doc["file_name"].GetString();
        }
        else
        {
            return BuildErrorResponse(id, "get_anim_graph needs anim_graph_id or file_name", RequestError::ValidationFailed);
        }

        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::GetAnimGraph, selector);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    // -------------------------------------------------------------------------
    // Response Builders
    // -------------------------------------------------------------------------

    AZStd::string AgentServer::BuildResponse(
        const AZStd::string& id, const char* status, const AZStd::string& output, const AZStd::string& error, AZ::s64 durationMs)
    {
        return ResponseBuilding::BuildResponse(id, status, output, error, durationMs);
    }

    AZStd::string AgentServer::BuildErrorResponse(const AZStd::string& id, const AZStd::string& error, const char* code)
    {
        AZ_Error("AiCompanion", false, "[AgentServer] req=%s error: %s", id.c_str(), error.c_str());
        return ResponseBuilding::BuildErrorResponse(id, error, code);
    }

    AZStd::string AgentServer::FailureResponse(const AZStd::string& id, const AZStd::string& encodedError)
    {
        AZStd::string code;
        AZStd::string message;
        RequestError::DecodeError(encodedError, code, message);
        return BuildErrorResponse(id, message, code.c_str());
    }

    // -------------------------------------------------------------------------
    // Logging
    // -------------------------------------------------------------------------

    void AgentServer::LogMinimal(const char* format, ...) const
    {
        // Minimal always logs
        va_list args;
        va_start(args, format);
        char buf[1024];
        azvsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        AZ_TracePrintf("AiCompanion", "%s\n", buf);
    }

    void AgentServer::LogStandard(const char* format, ...) const
    {
        if (m_logLevel < AgentServerLogLevel::Standard)
        {
            return;
        }
        va_list args;
        va_start(args, format);
        char buf[1024];
        azvsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        AZ_TracePrintf("AiCompanion", "%s\n", buf);
    }

    void AgentServer::LogVerbose(const char* format, ...) const
    {
        if (m_logLevel < AgentServerLogLevel::Verbose)
        {
            return;
        }
        va_list args;
        va_start(args, format);
        char buf[4096];
        azvsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        AZ_TracePrintf("AiCompanion", "%s\n", buf);
    }

    // -------------------------------------------------------------------------
    // Socket Helpers
    // -------------------------------------------------------------------------

    void AgentServer::CloseSocket(SocketType& sock)
    {
        if (sock == InvalidSocket)
        {
            return;
        }
#if defined(AZ_PLATFORM_WINDOWS)
        closesocket(sock);
#else
        close(sock);
#endif
        sock = InvalidSocket;
    }

    // -------------------------------------------------------------------------
    // TLS Helpers
    // -------------------------------------------------------------------------

    bool AgentServer::InitTls([[maybe_unused]] const AZStd::string& certPath, [[maybe_unused]] const AZStd::string& keyPath)
    {
#if AI_COMPANION_TLS_AVAILABLE
        // Note: SSL_library_init/SSL_load_error_strings/OpenSSL_add_all_algorithms
        // are deprecated no-ops in OpenSSL 1.1.0+. OpenSSL auto-initializes.

        const SSL_METHOD* method = TLS_server_method();
        m_sslCtx = SSL_CTX_new(method);
        if (!m_sslCtx)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to create SSL context");
            return false;
        }

        // Set minimum TLS version to 1.2
        SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);

        // Restrict to strong cipher suites
        SSL_CTX_set_cipher_list(m_sslCtx, "HIGH:!aNULL:!MD5:!RC4");

        if (SSL_CTX_use_certificate_file(m_sslCtx, certPath.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to load TLS certificate: %s", certPath.c_str());
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
            return false;
        }

        if (SSL_CTX_use_PrivateKey_file(m_sslCtx, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to load TLS private key: %s", keyPath.c_str());
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
            return false;
        }

        if (!SSL_CTX_check_private_key(m_sslCtx))
        {
            AZ_Error("AiCompanion", false, "[AgentServer] TLS certificate and private key do not match");
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
            return false;
        }

        LogMinimal("[AgentServer] TLS initialized (cert=%s)", certPath.c_str());
        return true;
#else
        AZ_Error("AiCompanion", false, "[AgentServer] TLS not available on this platform");
        return false;
#endif
    }

    void AgentServer::ShutdownTls()
    {
#if AI_COMPANION_TLS_AVAILABLE
        if (m_sslCtx)
        {
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
        }
#endif
        m_tlsEnabled = false;
    }

    AZStd::string AgentServer::HandleCreateAnimGraph(const AZStd::string& id)
    {
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::CreateAnimGraph);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleRemoveAnimGraph(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id, "remove_anim_graph requires 'anim_graph_id' (a decimal id, as a number or string)", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::RemoveAnimGraph, animGraphId);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    // -------------------------------------------------------------------------
    // Anim graph authoring. The server checks the request's shape (the id
    // fields and required strings) and forwards; every engine-facing rule
    // (names, types, placement, ownership, paths) is in Animation/AnimGraphAuthoring.
    // -------------------------------------------------------------------------

    namespace
    {
        //! A node id field: a decimal string, or a number, as text.
        bool ReadIdText(const rapidjson::Document& doc, const char* key, AZStd::string& out)
        {
            if (!doc.HasMember(key))
            {
                return false;
            }
            const rapidjson::Value& value = doc[key];
            if (value.IsString() && value.GetStringLength() > 0)
            {
                out.assign(value.GetString(), value.GetStringLength());
                return true;
            }
            if (value.IsUint64())
            {
                out = AZStd::string::format("%llu", static_cast<unsigned long long>(value.GetUint64()));
                return true;
            }
            return false;
        }

        //! An optional string field; absent or null reads as empty.
        bool ReadOptionalText(const rapidjson::Document& doc, const char* key, AZStd::string& out)
        {
            out.clear();
            if (!doc.HasMember(key) || doc[key].IsNull())
            {
                return true;
            }
            if (!doc[key].IsString())
            {
                return false;
            }
            out.assign(doc[key].GetString(), doc[key].GetStringLength());
            return true;
        }
    } // namespace

    AZStd::string AgentServer::HandleLoadAnimGraph(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZStd::string fileName;
        if (!ReadOptionalText(doc, "file_name", fileName) || fileName.empty())
        {
            return BuildErrorResponse(
                id,
                "load_anim_graph requires 'file_name': an .animgraph path, absolute, @alias@, or relative to the project root",
                RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::LoadAnimGraph, fileName);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleSaveAnimGraph(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id, "save_anim_graph requires 'anim_graph_id' (a decimal id, as a number or string)", RequestError::ValidationFailed);
        }
        AZStd::string fileName;
        if (!ReadOptionalText(doc, "file_name", fileName))
        {
            return BuildErrorResponse(id, "save_anim_graph 'file_name' must be a string", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::SaveAnimGraph, animGraphId, fileName);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleAddAnimGraphNode(
        const AZStd::string& id, const rapidjson::Document& doc, const AZStd::string& jsonRequest)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id, "add_anim_graph_node requires 'anim_graph_id' (a decimal id, as a number or string)", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::AddAnimGraphNode, animGraphId, jsonRequest);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleRemoveAnimGraphNode(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id,
                "remove_anim_graph_node requires 'anim_graph_id' (a decimal id, as a number or string)",
                RequestError::ValidationFailed);
        }
        AZStd::string nodeId;
        if (!ReadIdText(doc, "node_id", nodeId))
        {
            return BuildErrorResponse(
                id, "remove_anim_graph_node requires 'node_id' (the node's decimal id string)", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::RemoveAnimGraphNode, animGraphId, nodeId);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleSetAnimGraphEntryState(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id,
                "set_anim_graph_entry_state requires 'anim_graph_id' (a decimal id, as a number or string)",
                RequestError::ValidationFailed);
        }
        AZStd::string nodeId;
        if (!ReadIdText(doc, "node_id", nodeId))
        {
            return BuildErrorResponse(
                id, "set_anim_graph_entry_state requires 'node_id' (the node's decimal id string)", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::SetAnimGraphEntryState, animGraphId, nodeId);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleAddAnimGraphParameter(
        const AZStd::string& id, const rapidjson::Document& doc, const AZStd::string& jsonRequest)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id,
                "add_anim_graph_parameter requires 'anim_graph_id' (a decimal id, as a number or string)",
                RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::AddAnimGraphParameter, animGraphId, jsonRequest);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleRemoveAnimGraphParameter(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id,
                "remove_anim_graph_parameter requires 'anim_graph_id' (a decimal id, as a number or string)",
                RequestError::ValidationFailed);
        }
        AZStd::string name;
        if (!ReadOptionalText(doc, "name", name) || name.empty())
        {
            return BuildErrorResponse(
                id, "remove_anim_graph_parameter requires 'name' (the value parameter's name)", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::RemoveAnimGraphParameter, animGraphId, name);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleRemoveAnimGraphTransition(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id,
                "remove_anim_graph_transition requires 'anim_graph_id' (a decimal id, as a number or string)",
                RequestError::ValidationFailed);
        }
        AZStd::string transitionId;
        if (!ReadIdText(doc, "transition_id", transitionId))
        {
            return BuildErrorResponse(
                id,
                "remove_anim_graph_transition requires 'transition_id' (the transition's decimal id string)",
                RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::RemoveAnimGraphTransition, animGraphId, transitionId);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleAnimGraphJsonRequest(
        const AZStd::string& id,
        const rapidjson::Document& doc,
        const AZStd::string& jsonRequest,
        const char* requestType,
        AZ::Outcome<AZStd::string, AZStd::string> (AiCompanionEditorRequests::*event)(AZ::u32, AZStd::string))
    {
        AZ::u32 animGraphId = 0;
        if (!doc.HasMember("anim_graph_id") || !RequestParsing::ParseAnimGraphId(doc["anim_graph_id"], animGraphId))
        {
            return BuildErrorResponse(
                id,
                AZStd::string::format("%s requires 'anim_graph_id' (a decimal id, as a number or string)", requestType),
                RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, event, animGraphId, jsonRequest);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }
    // -------------------------------------------------------------------------
    // Asset readiness. The server checks the request's shape (a non-empty
    // path, boolean flags) and forwards; the path rule and the Asset
    // Processor calls are in the editor component.
    // -------------------------------------------------------------------------

    namespace
    {
        //! An optional boolean field; absent or null leaves `out` untouched.
        bool ReadOptionalBool(const rapidjson::Document& doc, const char* key, bool& out)
        {
            if (!doc.HasMember(key) || doc[key].IsNull())
            {
                return true;
            }
            if (!doc[key].IsBool())
            {
                return false;
            }
            out = doc[key].GetBool();
            return true;
        }
    } // namespace

    AZStd::string AgentServer::HandleGetAssetStatus(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZStd::string path;
        if (!ReadOptionalText(doc, "path", path) || path.empty())
        {
            return BuildErrorResponse(
                id,
                "get_asset_status requires 'path': a source relative path, a product relative path, or a full path to either",
                RequestError::ValidationFailed);
        }
        bool flushIo = false;
        if (!ReadOptionalBool(doc, "flush_io", flushIo))
        {
            return BuildErrorResponse(id, "get_asset_status 'flush_io' must be a boolean", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::GetAssetStatus, path, flushIo);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleGetAssetJobs(const AZStd::string& id, const rapidjson::Document& doc)
    {
        AZStd::string sourcePath;
        if (!ReadOptionalText(doc, "source_path", sourcePath) || sourcePath.empty())
        {
            return BuildErrorResponse(
                id,
                "get_asset_jobs requires 'source_path': the source file, relative to its scan folder or full",
                RequestError::ValidationFailed);
        }
        bool escalate = false;
        if (!ReadOptionalBool(doc, "escalate", escalate))
        {
            return BuildErrorResponse(id, "get_asset_jobs 'escalate' must be a boolean", RequestError::ValidationFailed);
        }
        bool includeLogs = false;
        if (!ReadOptionalBool(doc, "include_logs", includeLogs))
        {
            return BuildErrorResponse(id, "get_asset_jobs 'include_logs' must be a boolean", RequestError::ValidationFailed);
        }
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(
            outcome, &AiCompanionEditorRequestBus::Events::GetAssetJobs, sourcePath, escalate, includeLogs);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }

    AZStd::string AgentServer::HandleGetAssetProcessorStatus(const AZStd::string& id)
    {
        AZ::Outcome<AZStd::string, AZStd::string> outcome = AZ::Failure(EditorBusNoHandler());
        AiCompanionEditorRequestBus::BroadcastResult(outcome, &AiCompanionEditorRequestBus::Events::GetAssetProcessorStatus);
        if (!outcome.IsSuccess())
        {
            return FailureResponse(id, outcome.GetError());
        }
        return BuildResponse(id, "ok", outcome.GetValue(), "", 0);
    }
} // namespace AiCompanion
