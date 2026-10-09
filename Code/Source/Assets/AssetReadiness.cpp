/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AssetReadiness.h"

#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

#include <cmath>

namespace AiCompanion::AssetReadiness
{
    const char* AssetStatusWord(AzFramework::AssetSystem::AssetStatus status)
    {
        using namespace AzFramework::AssetSystem;
        switch (status)
        {
        case AssetStatus_Missing:
            return "missing";
        case AssetStatus_Queued:
            return "queued";
        case AssetStatus_Compiling:
            return "compiling";
        case AssetStatus_Compiled:
            return "compiled";
        case AssetStatus_Failed:
            return "failed";
        case AssetStatus_Unknown:
        default:
            return "unknown";
        }
    }

    const char* JobStateWord(JobState state, AZStd::string& outDetail)
    {
        outDetail.clear();
        switch (state)
        {
        case JobState::Queued:
            return "queued";
        case JobState::InProgress:
            return "in_progress";
        case JobState::Failed:
            return "failed";
        case JobState::FailedInvalidSourceNameExceedsMaxLimit:
            outDetail = "invalid_source_name_exceeds_max_limit";
            return "failed";
        case JobState::Completed:
            return "completed";
        case JobState::Missing:
            return "missing";
        case JobState::Any:
        default:
            return "unknown";
        }
    }

    bool IsFailedState(JobState state)
    {
        return state == JobState::Failed || state == JobState::FailedInvalidSourceNameExceedsMaxLimit;
    }

    bool TruncateJobLog(AZStd::string& log, size_t maxBytes)
    {
        if (log.size() <= maxBytes)
        {
            return false;
        }
        size_t cut = maxBytes;
        // A UTF-8 continuation byte is 10xxxxxx; back off to the byte that
        // starts the sequence so the cut leaves whole characters behind.
        while (cut > 0 && (static_cast<unsigned char>(log[cut]) & 0xC0) == 0x80)
        {
            --cut;
        }
        log.resize(cut);
        return true;
    }

    bool IsAcceptablePath(const AZStd::string& path, AZStd::string& outReason)
    {
        if (path.empty())
        {
            outReason = "the path is empty";
            return false;
        }
        if (path.size() > MaxPathBytes)
        {
            outReason = AZStd::string::format("the path is longer than %zu bytes", MaxPathBytes);
            return false;
        }
        for (char c : path)
        {
            const unsigned char byte = static_cast<unsigned char>(c);
            if (byte < 0x20 || byte == 0x7F)
            {
                outReason = "the path contains a control character";
                return false;
            }
        }
        return true;
    }

    AZStd::string BuildAssetStatusJson(const AZStd::string& path, AzFramework::AssetSystem::AssetStatus status)
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("path");
        w.String(path.c_str(), static_cast<rapidjson::SizeType>(path.size()));
        w.Key("status");
        w.String(AssetStatusWord(status));
        w.Key("connected");
        w.Bool(true);
        w.EndObject();
        return buffer.GetString();
    }

    AZStd::string BuildAssetJobsJson(const AZStd::string& sourcePath, const AZStd::vector<JobRecord>& jobs)
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("source_path");
        w.String(sourcePath.c_str(), static_cast<rapidjson::SizeType>(sourcePath.size()));
        w.Key("jobs");
        w.StartArray();
        for (const JobRecord& job : jobs)
        {
            w.StartObject();
            w.Key("job_key");
            w.String(job.m_jobKey.c_str(), static_cast<rapidjson::SizeType>(job.m_jobKey.size()));
            w.Key("platform");
            w.String(job.m_platform.c_str(), static_cast<rapidjson::SizeType>(job.m_platform.size()));
            w.Key("builder");
            w.String(job.m_builder.c_str(), static_cast<rapidjson::SizeType>(job.m_builder.size()));
            w.Key("source_file");
            w.String(job.m_sourceFile.c_str(), static_cast<rapidjson::SizeType>(job.m_sourceFile.size()));
            w.Key("watch_folder");
            w.String(job.m_watchFolder.c_str(), static_cast<rapidjson::SizeType>(job.m_watchFolder.size()));
            AZStd::string detail;
            const char* word = JobStateWord(job.m_state, detail);
            w.Key("status");
            w.String(word);
            if (!detail.empty())
            {
                w.Key("status_detail");
                w.String(detail.c_str(), static_cast<rapidjson::SizeType>(detail.size()));
            }
            w.Key("error_count");
            w.Int64(job.m_errorCount);
            w.Key("warning_count");
            w.Int64(job.m_warningCount);
            // A u64 as a decimal string, the convention for every 64-bit id
            // in native output (a double-based JSON parser corrupts it).
            const AZStd::string runKey = AZStd::string::format("%llu", static_cast<unsigned long long>(job.m_jobRunKey));
            w.Key("job_run_key");
            w.String(runKey.c_str(), static_cast<rapidjson::SizeType>(runKey.size()));
            if (job.m_hasLog)
            {
                w.Key("log");
                if (job.m_logAvailable)
                {
                    w.String(job.m_log.c_str(), static_cast<rapidjson::SizeType>(job.m_log.size()));
                }
                else
                {
                    w.Null();
                }
                if (job.m_truncated)
                {
                    w.Key("truncated");
                    w.Bool(true);
                }
            }
            w.EndObject();
        }
        w.EndArray();
        w.EndObject();
        return buffer.GetString();
    }

    AZStd::string BuildAssetProcessorStatusJson(bool connected, float pingMs)
    {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("connected");
        w.Bool(connected);
        w.Key("ping_ms");
        w.Double(std::isfinite(pingMs) ? static_cast<double>(pingMs) : 0.0);
        w.EndObject();
        return buffer.GetString();
    }
} // namespace AiCompanion::AssetReadiness
