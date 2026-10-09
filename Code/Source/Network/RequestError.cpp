/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "RequestError.h"

#include <AzCore/JSON/document.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

namespace AiCompanion::RequestError
{
    AZStd::string EncodeError(const char* code, const AZStd::string& message)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("code");
        w.String(code != nullptr ? code : EngineError);
        w.Key("message");
        w.String(message.c_str(), static_cast<rapidjson::SizeType>(message.size()));
        w.EndObject();
        return AZStd::string(sb.GetString(), sb.GetSize());
    }

    void DecodeError(const AZStd::string& maybeEncoded, AZStd::string& outCode, AZStd::string& outMessage)
    {
        rapidjson::Document doc;
        doc.Parse(maybeEncoded.c_str(), maybeEncoded.size());
        if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("code") && doc["code"].IsString() && doc.HasMember("message") &&
            doc["message"].IsString())
        {
            outCode.assign(doc["code"].GetString(), doc["code"].GetStringLength());
            outMessage.assign(doc["message"].GetString(), doc["message"].GetStringLength());
            return;
        }
        outCode = EngineError;
        outMessage = maybeEncoded;
    }
} // namespace AiCompanion::RequestError
