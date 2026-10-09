/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/std/string/string.h>

namespace AiCompanion::RequestError
{
    //! The "code" vocabulary of the AgentServer's error replies. Every error
    //! reply carries exactly one of these next to its message, so a client
    //! branches on the code and shows the message. o3de-mcp relays both.

    //! A malformed or refused argument: invalid JSON, a missing "type" or
    //! "script" field, a bad base64 script, a missing or unparsable
    //! entity_id / anim_graph_id, an invalid entity name, a position outside
    //! the bound, a scale out of range, or a refusal to delete the level root.
    inline constexpr const char* ValidationFailed = "validation_failed";

    //! The named entity, anim graph, node, transition, parameter or bus does
    //! not exist.
    inline constexpr const char* NotFound = "not_found";

    //! A subsystem the request needs is not loaded: EMotion FX, EMotion
    //! Studio's command system, the editor request bus, the Python runner.
    inline constexpr const char* Unavailable = "unavailable";

    //! The engine refused or failed the operation; the message is the
    //! engine's own text (a prefab system message, a failed EMotion FX
    //! command). Also the code DecodeError assigns to a message that is not
    //! in the encoded form, so an unconverted failure still has a code.
    inline constexpr const char* EngineError = "engine_error";

    //! execute_python refused because the server runs in secure mode.
    inline constexpr const char* SecureMode = "secure_mode";

    //! The request "type" is not one the server serves. Clients fall back to
    //! editor Python on this code and on no other.
    inline constexpr const char* UnknownRequestType = "unknown_request_type";

    //! The main thread did not answer within the dispatch timeout.
    inline constexpr const char* Timeout = "timeout";

    //! The server is stopping; the request was dropped unprocessed.
    inline constexpr const char* ShuttingDown = "shutting_down";

    //! An execute_python script raised; the message is the Python traceback.
    inline constexpr const char* ExecutionFailed = "execution_failed";

    //! Serializes a failure as the JSON text {"code": "...", "message": "..."}.
    //! The AiCompanionEditorRequestBus events return this text as their
    //! AZ::Outcome failure so the AgentServer can hand the code through.
    AZStd::string EncodeError(const char* code, const AZStd::string& message);

    //! Splits an EncodeError text into its code and message. Any input that
    //! is not that JSON shape (a plain legacy message, malformed JSON, an
    //! object lacking a string "code" or "message") yields EngineError with
    //! the whole input as the message, so a caller always gets a code.
    void DecodeError(const AZStd::string& maybeEncoded, AZStd::string& outCode, AZStd::string& outMessage);
} // namespace AiCompanion::RequestError
