/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "ComponentState.h"

#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

namespace AiCompanion::ComponentState
{
    namespace
    {
        AZStd::string JoinQuoted(const AZStd::vector<AZStd::string>& words)
        {
            AZStd::string joined;
            for (const AZStd::string& word : words)
            {
                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += "\"";
                joined += word;
                joined += "\"";
            }
            return joined;
        }

        AZStd::string Join(const AZStd::vector<AZStd::string>& words)
        {
            AZStd::string joined;
            for (const AZStd::string& word : words)
            {
                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += word;
            }
            return joined;
        }

        void WriteStringArray(rapidjson::Writer<rapidjson::StringBuffer>& w, const char* key, const AZStd::vector<AZStd::string>& words)
        {
            w.Key(key);
            w.StartArray();
            for (const AZStd::string& word : words)
            {
                w.String(word.c_str(), static_cast<rapidjson::SizeType>(word.size()));
            }
            w.EndArray();
        }
    } // namespace

    const char* StateWord(bool active, bool pending, bool disabled)
    {
        if (active)
        {
            return ActiveWord;
        }
        if (pending)
        {
            return PendingWord;
        }
        if (disabled)
        {
            return DisabledWord;
        }
        return UnknownWord;
    }

    AZStd::string ServiceCrcWord(AZ::u32 crc)
    {
        return AZStd::string::format("0x%08x", crc);
    }

    AZStd::string Summarize(const Info& info)
    {
        if (info.m_state == ActiveWord)
        {
            return ActiveWord;
        }
        if (info.m_state == DisabledWord)
        {
            return "disabled by the user";
        }
        if (info.m_state != PendingWord)
        {
            return "state unknown";
        }

        AZStd::vector<AZStd::string> reasons;
        if (!info.m_incompatibleWith.empty())
        {
            reasons.push_back("incompatible with " + JoinQuoted(info.m_incompatibleWith));
        }
        if (!info.m_missingRequiredServices.empty())
        {
            reasons.push_back(
                AZStd::string(info.m_missingRequiredServices.size() == 1 ? "missing required service " : "missing required services ") +
                Join(info.m_missingRequiredServices));
        }
        if (!info.m_waitingOnPending.empty())
        {
            reasons.push_back("waiting on pending " + JoinQuoted(info.m_waitingOnPending));
        }
        if (reasons.empty())
        {
            return "disabled: pending, reason not reported by the editor";
        }
        return "disabled: " + Join(reasons);
    }

    AZStd::string BuildComponentStateJson(const Info& info)
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("name");
        w.String(info.m_name.c_str(), static_cast<rapidjson::SizeType>(info.m_name.size()));
        w.Key("state");
        w.String(info.m_state.c_str(), static_cast<rapidjson::SizeType>(info.m_state.size()));
        const AZStd::string summary = Summarize(info);
        w.Key("summary");
        w.String(summary.c_str(), static_cast<rapidjson::SizeType>(summary.size()));
        WriteStringArray(w, "incompatible_with", info.m_incompatibleWith);
        WriteStringArray(w, "waiting_on_pending", info.m_waitingOnPending);
        WriteStringArray(w, "missing_required_services", info.m_missingRequiredServices);
        WriteStringArray(w, "incompatible_services", info.m_incompatibleServices);
        WriteStringArray(w, "warnings", info.m_warnings);
        w.EndObject();
        return buffer.GetString();
    }
} // namespace AiCompanion::ComponentState
