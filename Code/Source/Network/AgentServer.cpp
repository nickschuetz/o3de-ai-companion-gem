/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AgentServer.h"

#include <AzCore/base.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzCore/std/string/conversions.h>
#include <AzCore/Utils/Utils.h>
#include <AzToolsFramework/API/EditorPythonRunnerRequestsBus.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include <cstdarg>
#include <cstring>

// TLS support via OpenSSL (linked through AzFramework)
#if AZ_TRAIT_OS_PLATFORM_APPLE || AZ_TRAIT_OS_IS_LINUX || defined(AZ_PLATFORM_WINDOWS)
    #include <openssl/ssl.h>
    #include <openssl/err.h>
    #define AI_COMPANION_TLS_AVAILABLE 1
#else
    #define AI_COMPANION_TLS_AVAILABLE 0
#endif

#if defined(AZ_PLATFORM_WINDOWS)
    #pragma comment(lib, "Ws2_32.lib")
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
        const AZStd::string& host, AZ::u16 port, bool secureMode,
        AgentServerLogLevel logLevel,
        const AZStd::string& tlsCertPath, const AZStd::string& tlsKeyPath)
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

        // Create listening socket
        m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

        // Allow address reuse
        int optVal = 1;
#if defined(AZ_PLATFORM_WINDOWS)
        setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&optVal), sizeof(optVal));
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
        AZ::TickBus::Handler::BusConnect();

        // Start accept thread
        m_acceptThread = AZStd::thread([this]() { AcceptLoop(); });

        LogMinimal("[AgentServer] Started on %s:%u (secure=%s, tls=%s, log=%s)",
                   m_host.c_str(), m_port,
                   m_secureMode.load() ? "on" : "off",
                   m_tlsEnabled ? "on" : "off",
                   m_logLevel == AgentServerLogLevel::Minimal ? "minimal" :
                   m_logLevel == AgentServerLogLevel::Standard ? "standard" : "verbose");

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
        AZ::TickBus::Handler::BusDisconnect();
        AgentServerRequestBus::Handler::BusDisconnect();

        // Drain any pending requests with error
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
            for (auto& req : m_pendingRequests)
            {
                req->responsePromise.set_value(BuildErrorResponse(req->id, "Server shutting down"));
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
    // TickBus — Process pending requests on main thread
    // -------------------------------------------------------------------------

    void AgentServer::OnTick([[maybe_unused]] float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        AZStd::deque<AZStd::shared_ptr<PendingRequest>> localQueue;
        {
            AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
            localQueue.swap(m_pendingRequests);
        }

        for (auto& req : localQueue)
        {
            auto startTime = AZStd::chrono::steady_clock::now();
            AZStd::string response = HandleRequest(req->payload);
            auto endTime = AZStd::chrono::steady_clock::now();
            auto durationMs = AZStd::chrono::duration_cast<AZStd::chrono::milliseconds>(endTime - startTime).count();

            // Inject timing into the response
            rapidjson::Document doc;
            doc.Parse(response.c_str());
            if (!doc.HasParseError() && doc.IsObject())
            {
                doc.RemoveMember("duration_ms");
                doc.AddMember("duration_ms", rapidjson::Value(durationMs), doc.GetAllocator());

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                doc.Accept(writer);
                response = buffer.GetString();
            }

            LogStandard("[AgentServer] req=%s type=%s duration=%lldms",
                        req->id.c_str(), req->type.c_str(), static_cast<long long>(durationMs));

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
                continue; // timeout or error — check m_running
            }

            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            SocketType clientSock = accept(m_listenSocket,
                                           reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);

            if (clientSock == InvalidSocket)
            {
                continue; // likely shutting down
            }

            // Extract client address for logging
            char addrBuf[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf));
            AZ::u16 clientPort = ntohs(clientAddr.sin_port);
            AZStd::string clientAddrStr(addrBuf);

            // Single-client policy: reject if another client is connected
            {
                AZStd::lock_guard<AZStd::mutex> lock(m_clientMutex);
                if (m_clientSocket.load() != InvalidSocket)
                {
                    LogMinimal("[AgentServer] Rejected connection from %s:%u — another client is connected",
                               clientAddrStr.c_str(), clientPort);
                    CloseSocket(clientSock);
                    continue;
                }
            }

            LogMinimal("[AgentServer] Client connected from %s:%u (TLS: %s)",
                       clientAddrStr.c_str(), clientPort, m_tlsEnabled ? "yes" : "no");

            // Wait for any previous client thread to finish
            if (m_clientThread.joinable())
            {
                m_clientThread.join();
            }

            m_clientSocket.store(clientSock);
            m_clientThread = AZStd::thread([this, clientSock, clientAddrStr, clientPort]()
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
                AZ_Error("AiCompanion", false, "[AgentServer] TLS handshake failed with %s:%u",
                         clientAddr.c_str(), clientPort);
                LogVerbose("[AgentServer] TLS error: %s",
                          ERR_error_string(ERR_get_error(), nullptr));
                SSL_free(ssl);
                CloseSocket(clientSocket);
                m_clientSocket.store(InvalidSocket);
                return;
            }
            LogVerbose("[AgentServer] TLS handshake complete with %s:%u, cipher=%s",
                      clientAddr.c_str(), clientPort, SSL_get_cipher(ssl));
        }
#endif

        while (m_running.load())
        {
            AZStd::string requestJson;
            if (!ReadFramedMessage(clientSocket, ssl, requestJson))
            {
                break; // connection closed or error
            }

            LogVerbose("[AgentServer] Received: %.4096s%s",
                      requestJson.c_str(),
                      requestJson.size() > LogTruncateSize ? "...(truncated)" : "");

            // Parse to extract type and determine dispatch
            rapidjson::Document doc;
            doc.Parse(requestJson.c_str());

            if (doc.HasParseError() || !doc.IsObject())
            {
                AZStd::string errorResp = BuildErrorResponse("", "Invalid JSON");
                SendFramedMessage(clientSocket, ssl, errorResp);
                continue;
            }

            AZStd::string id = doc.HasMember("id") && doc["id"].IsString()
                ? doc["id"].GetString() : "";
            AZStd::string type = doc.HasMember("type") && doc["type"].IsString()
                ? doc["type"].GetString() : "";

            if (type.empty())
            {
                AZStd::string errorResp = BuildErrorResponse(id, "Missing 'type' field");
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
            else if (type == "get_scene_snapshot" || type == "get_entity_tree" || type == "validate_scene")
            {
                // Safe EBus calls — dispatch to main thread
                auto pending = AZStd::make_shared<PendingRequest>();
                pending->id = id;
                pending->type = type;
                pending->payload = requestJson;
                auto future = pending->responsePromise.get_future();

                {
                    AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
                    m_pendingRequests.push_back(pending);
                }

                response = future.get();
            }
            else if (type == "execute_python")
            {
                if (m_secureMode.load())
                {
                    response = BuildErrorResponse(id,
                        "execute_python is disabled in secure mode. "
                        "Only ping, get_api_version, get_scene_snapshot, get_entity_tree, "
                        "and validate_scene are available.");
                    AZ_Warning("AiCompanion", false,
                               "[AgentServer] Blocked execute_python in secure mode (req=%s)", id.c_str());
                }
                else
                {
                    // Dispatch to main thread
                    auto pending = AZStd::make_shared<PendingRequest>();
                    pending->id = id;
                    pending->type = type;
                    pending->payload = requestJson;
                    auto future = pending->responsePromise.get_future();

                    {
                        AZStd::lock_guard<AZStd::mutex> lock(m_queueMutex);
                        m_pendingRequests.push_back(pending);
                    }

                    response = future.get();
                }
            }
            else
            {
                response = BuildErrorResponse(id,
                    AZStd::string::format("Unknown request type: %s", type.c_str()));
            }

            LogVerbose("[AgentServer] Sending: %.4096s%s",
                      response.c_str(),
                      response.size() > LogTruncateSize ? "...(truncated)" : "");

            if (!SendFramedMessage(clientSocket, ssl, response))
            {
                break; // send failed — connection likely broken
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
    // Protocol — Framed message I/O
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
                n = recv(sock, reinterpret_cast<char*>(header + headerRead),
                         static_cast<int>(4 - headerRead), 0);
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

        AZ::u32 length = (static_cast<AZ::u32>(header[0]) << 24) |
                          (static_cast<AZ::u32>(header[1]) << 16) |
                          (static_cast<AZ::u32>(header[2]) << 8) |
                          (static_cast<AZ::u32>(header[3]));

        if (length > MaxMessageSize)
        {
            AZ_Error("AiCompanion", false,
                     "[AgentServer] Message too large: %u bytes (max %u)", length, MaxMessageSize);
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
        AZ::u8 header[4] = {
            static_cast<AZ::u8>((length >> 24) & 0xFF),
            static_cast<AZ::u8>((length >> 16) & 0xFF),
            static_cast<AZ::u8>((length >> 8) & 0xFF),
            static_cast<AZ::u8>(length & 0xFF)
        };

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

    AZStd::string AgentServer::HandleRequest(const AZStd::string& jsonRequest)
    {
        rapidjson::Document doc;
        doc.Parse(jsonRequest.c_str());

        if (doc.HasParseError() || !doc.IsObject())
        {
            return BuildErrorResponse("", "Invalid JSON request");
        }

        AZStd::string id = doc.HasMember("id") && doc["id"].IsString()
            ? doc["id"].GetString() : "";
        AZStd::string type = doc.HasMember("type") && doc["type"].IsString()
            ? doc["type"].GetString() : "";

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
        else if (type == "execute_python")
        {
            // Decode base64 script
            if (!doc.HasMember("script") || !doc["script"].IsString())
            {
                return BuildErrorResponse(id, "Missing 'script' field for execute_python");
            }

            AZStd::string b64Script = doc["script"].GetString();

            // Decode base64
            // Using a simple base64 decode implementation
            static const AZStd::string base64Chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            AZStd::string decoded;
            decoded.reserve(b64Script.size() * 3 / 4);

            AZ::u32 val = 0;
            int bits = -8;
            for (char c : b64Script)
            {
                if (c == '=' || c == '\n' || c == '\r')
                {
                    continue;
                }
                size_t pos = base64Chars.find(c);
                if (pos == AZStd::string::npos)
                {
                    continue;
                }
                val = (val << 6) + static_cast<AZ::u32>(pos);
                bits += 6;
                if (bits >= 0)
                {
                    decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
                    bits -= 8;
                }
            }

            if (decoded.empty())
            {
                return BuildErrorResponse(id, "Failed to decode base64 script");
            }

            // Wrap the script to capture stdout/stderr
            // We use a unique global variable keyed by request ID to retrieve output
            AZStd::string wrappedScript = AZStd::string::format(
                "import sys, io as _io\n"
                "_ac_buf = _io.StringIO()\n"
                "_ac_old_stdout, _ac_old_stderr = sys.stdout, sys.stderr\n"
                "sys.stdout = sys.stderr = _ac_buf\n"
                "_ac_error = ''\n"
                "try:\n"
                "    exec(compile(%s, '<agent>', 'exec'))\n"
                "except Exception as _ac_e:\n"
                "    import traceback\n"
                "    _ac_error = traceback.format_exc()\n"
                "    _ac_buf.write(_ac_error)\n"
                "finally:\n"
                "    sys.stdout, sys.stderr = _ac_old_stdout, _ac_old_stderr\n"
                "    import json as _ac_json\n"
                "    _ac_result = {'output': _ac_buf.getvalue(), 'error': _ac_error}\n"
                "    # Store result for C++ retrieval\n"
                "    if not hasattr(sys, '_ai_companion_results'):\n"
                "        sys._ai_companion_results = {}\n"
                "    sys._ai_companion_results['%s'] = _ac_json.dumps(_ac_result)\n",
                // Pass the decoded script as a repr'd string literal
                AZStd::string::format("'''%s'''",
                    // Escape triple quotes in the script
                    [&decoded]() -> AZStd::string {
                        AZStd::string escaped;
                        escaped.reserve(decoded.size() + 64);
                        for (size_t i = 0; i < decoded.size(); ++i)
                        {
                            if (decoded[i] == '\'' && i + 2 < decoded.size() &&
                                decoded[i+1] == '\'' && decoded[i+2] == '\'')
                            {
                                escaped += "'''\"'''\"'''";
                                i += 2;
                            }
                            else if (decoded[i] == '\\')
                            {
                                escaped += "\\\\";
                            }
                            else
                            {
                                escaped += decoded[i];
                            }
                        }
                        return escaped;
                    }().c_str()
                ).c_str(),
                id.c_str()
            );

            // Execute on main thread (we're already on main thread when called from OnTick)
            AzToolsFramework::EditorPythonRunnerRequestBus::Broadcast(
                &AzToolsFramework::EditorPythonRunnerRequestBus::Events::ExecuteByString,
                wrappedScript.c_str(), false);

            // Retrieve the result
            AZStd::string retrieveScript = AZStd::string::format(
                "import sys\n"
                "if hasattr(sys, '_ai_companion_results') and '%s' in sys._ai_companion_results:\n"
                "    print(sys._ai_companion_results.pop('%s'))\n"
                "else:\n"
                "    print('{\"output\": \"\", \"error\": \"No result captured\"}')\n",
                id.c_str(), id.c_str());

            // We need to capture the print output from the retrieve script
            // Use a file-based approach for reliable retrieval
            AZ::IO::FixedMaxPath tempDir;
            AZ::Utils::GetDefaultAppRootPath(tempDir);
            AZStd::string resultPath = AZStd::string::format("%s/_ai_companion_result_%s.json",
                tempDir.c_str(), id.c_str());

            AZStd::string fileRetrieveScript = AZStd::string::format(
                "import sys, json\n"
                "result = '{\"output\": \"\", \"error\": \"No result captured\"}'\n"
                "if hasattr(sys, '_ai_companion_results') and '%s' in sys._ai_companion_results:\n"
                "    result = sys._ai_companion_results.pop('%s')\n"
                "with open(r'%s', 'w') as f:\n"
                "    f.write(result)\n",
                id.c_str(), id.c_str(), resultPath.c_str());

            AzToolsFramework::EditorPythonRunnerRequestBus::Broadcast(
                &AzToolsFramework::EditorPythonRunnerRequestBus::Events::ExecuteByString,
                fileRetrieveScript.c_str(), false);

            // Read the result file
            AZStd::string output;
            AZStd::string error;

            FILE* resultFile = fopen(resultPath.c_str(), "r");
            if (resultFile)
            {
                char readBuf[8192];
                AZStd::string resultContent;
                while (size_t bytesRead = fread(readBuf, 1, sizeof(readBuf), resultFile))
                {
                    resultContent.append(readBuf, bytesRead);
                }
                fclose(resultFile);
                remove(resultPath.c_str());

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
                error = "Failed to retrieve script execution result";
            }

            const char* status = error.empty() ? "ok" : "error";
            return BuildResponse(id, status, output, error, 0);
        }

        return BuildErrorResponse(id, AZStd::string::format("Unhandled type in main thread: %s", type.c_str()));
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
        w.Key("protocol_version"); w.Int(1);
        w.Key("gem_version"); w.String("0.2.0");
        w.Key("api_version"); w.String("0.1.0");
        w.Key("secure_mode"); w.Bool(m_secureMode.load());
        w.Key("tls_enabled"); w.Bool(m_tlsEnabled);
        w.EndObject();

        return BuildResponse(id, "ok", sb.GetString(), "", 0);
    }

    AZStd::string AgentServer::HandleGetSceneSnapshot(const AZStd::string& id)
    {
        AZStd::string snapshot;
        AiCompanionRequestBus::BroadcastResult(snapshot,
            &AiCompanionRequestBus::Events::GetSceneSnapshot);
        return BuildResponse(id, "ok", snapshot, "", 0);
    }

    AZStd::string AgentServer::HandleGetEntityTree(const AZStd::string& id)
    {
        AZStd::string tree;
        AiCompanionRequestBus::BroadcastResult(tree,
            &AiCompanionRequestBus::Events::GetEntityTree);
        return BuildResponse(id, "ok", tree, "", 0);
    }

    AZStd::string AgentServer::HandleValidateScene(const AZStd::string& id)
    {
        AZStd::string report;
        AiCompanionRequestBus::BroadcastResult(report,
            &AiCompanionRequestBus::Events::ValidateScene);
        return BuildResponse(id, "ok", report, "", 0);
    }

    // -------------------------------------------------------------------------
    // Response Builders
    // -------------------------------------------------------------------------

    AZStd::string AgentServer::BuildResponse(
        const AZStd::string& id, const char* status,
        const AZStd::string& output, const AZStd::string& error,
        AZ::s64 durationMs)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("id"); w.String(id.c_str());
        w.Key("status"); w.String(status);
        w.Key("output"); w.String(output.c_str());
        w.Key("error"); w.String(error.c_str());
        w.Key("duration_ms"); w.Int64(durationMs);
        w.EndObject();
        return sb.GetString();
    }

    AZStd::string AgentServer::BuildErrorResponse(const AZStd::string& id, const AZStd::string& error)
    {
        AZ_Error("AiCompanion", false, "[AgentServer] req=%s error: %s", id.c_str(), error.c_str());
        return BuildResponse(id, "error", "", error, 0);
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

    bool AgentServer::InitTls(
        [[maybe_unused]] const AZStd::string& certPath,
        [[maybe_unused]] const AZStd::string& keyPath)
    {
#if AI_COMPANION_TLS_AVAILABLE
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();

        const SSL_METHOD* method = TLS_server_method();
        m_sslCtx = SSL_CTX_new(method);
        if (!m_sslCtx)
        {
            AZ_Error("AiCompanion", false, "[AgentServer] Failed to create SSL context");
            return false;
        }

        // Set minimum TLS version to 1.2
        SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);

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

} // namespace AiCompanion
