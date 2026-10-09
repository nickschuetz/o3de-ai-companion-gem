/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion::ResponseBuilding
{
    //! The "code" an error reply carries when the request "type" is not one
    //! the AgentServer serves. Clients branch on it to fall back to editor
    //! Python; no other error reply carries it.
    inline constexpr const char* UnknownRequestTypeCode = "unknown_request_type";

    //! Serializes one AgentServer reply: {"id", "status", "output", "error",
    //! "duration_ms"} plus "code" when one is given (nullptr omits the key).
    AZStd::string BuildResponse(
        const AZStd::string& id,
        const char* status,
        const AZStd::string& output,
        const AZStd::string& error,
        AZ::s64 durationMs,
        const char* code = nullptr);

    //! An error reply with empty output and zero duration. "code" is emitted
    //! only when given.
    AZStd::string BuildErrorResponse(const AZStd::string& id, const AZStd::string& error, const char* code = nullptr);
} // namespace AiCompanion::ResponseBuilding
