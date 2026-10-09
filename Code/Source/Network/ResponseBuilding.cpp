/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "ResponseBuilding.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace AiCompanion::ResponseBuilding
{
    AZStd::string BuildResponse(
        const AZStd::string& id,
        const char* status,
        const AZStd::string& output,
        const AZStd::string& error,
        AZ::s64 durationMs,
        const char* code)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("id");
        w.String(id.c_str());
        w.Key("status");
        w.String(status);
        w.Key("output");
        w.String(output.c_str());
        w.Key("error");
        w.String(error.c_str());
        if (code != nullptr)
        {
            w.Key("code");
            w.String(code);
        }
        w.Key("duration_ms");
        w.Int64(durationMs);
        w.EndObject();
        return sb.GetString();
    }

    AZStd::string BuildErrorResponse(const AZStd::string& id, const AZStd::string& error, const char* code)
    {
        return BuildResponse(id, "error", "", error, 0, code);
    }
} // namespace AiCompanion::ResponseBuilding
