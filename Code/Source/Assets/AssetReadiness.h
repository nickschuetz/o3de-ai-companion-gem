/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Asset/AssetSystemTypes.h>

namespace AiCompanion::AssetReadiness
{
    //! The words, argument rules and reply shapes of the asset readiness
    //! request types (get_asset_status, get_asset_jobs,
    //! get_asset_processor_status). Nothing here talks to the Asset
    //! Processor, so the unit tests cover every word and every reply the gem
    //! sends; AiCompanionEditorSystemComponent makes the bus calls and hands
    //! the results in.

    //! How many bytes of a job log get_asset_jobs returns before cutting it.
    inline constexpr size_t MaxJobLogBytes = 64 * 1024;

    //! The longest path argument the request types accept, in bytes.
    inline constexpr size_t MaxPathBytes = 1024;

    //! The AzFramework asset status as the lowercase word the reply carries:
    //! unknown, missing, queued, compiling, compiled, failed. A value the gem
    //! does not know reads as "unknown".
    const char* AssetStatusWord(AzFramework::AssetSystem::AssetStatus status);

    //! AzToolsFramework::AssetSystem::JobStatus, mirrored value for value so
    //! this file and its tests stay out of AzToolsFramework (the runtime
    //! library the tests link cannot depend on it).
    //! AiCompanionEditorSystemComponent static_asserts that the values match.
    enum class JobState : AZ::s32
    {
        Any = -1,
        Queued,
        InProgress,
        Failed,
        FailedInvalidSourceNameExceedsMaxLimit,
        Completed,
        Missing,
    };

    //! The job state as the reply's word: queued, in_progress, failed,
    //! completed, missing. The long failure reads as "failed" with
    //! `outDetail` set to "invalid_source_name_exceeds_max_limit"; every
    //! other state clears `outDetail`. Any and unknown values read as
    //! "unknown".
    const char* JobStateWord(JobState state, AZStd::string& outDetail);

    //! Whether a job in this state has failed (either failure value).
    bool IsFailedState(JobState state);

    //! Cuts `log` at `maxBytes`, backing off so the cut never splits a UTF-8
    //! sequence, and returns whether it cut anything.
    bool TruncateJobLog(AZStd::string& log, size_t maxBytes = MaxJobLogBytes);

    //! Whether a path argument can go to the Asset Processor as a search
    //! term: non-empty, at most MaxPathBytes, no control characters. Full
    //! paths are allowed (unlike InputValidator::IsValidAssetPath), since the
    //! engine accepts a source relative path, a product relative path or a
    //! full path to either. `outReason` says what was wrong.
    bool IsAcceptablePath(const AZStd::string& path, AZStd::string& outReason);

    //! One job as get_asset_jobs reports it, already converted to words.
    struct JobRecord
    {
        AZStd::string m_jobKey;
        AZStd::string m_platform;
        //! The builder uuid as text, in the engine's braced form.
        AZStd::string m_builder;
        //! The relative source path and scan folder the Asset Processor
        //! matched, which tell a caller what its path argument resolved to.
        AZStd::string m_sourceFile;
        AZStd::string m_watchFolder;
        JobState m_state = JobState::Queued;
        AZ::s64 m_errorCount = 0;
        AZ::s64 m_warningCount = 0;
        AZ::u64 m_jobRunKey = 0;
        //! Whether the reply carries a "log" member at all (logs were
        //! requested and the job failed).
        bool m_hasLog = false;
        //! When m_hasLog: whether the Asset Processor returned the log text.
        //! False writes "log": null.
        bool m_logAvailable = false;
        AZStd::string m_log;
        //! Writes "truncated": true after a cut log.
        bool m_truncated = false;
    };

    //! {"path": <as given>, "status": <word>, "connected": true}
    AZStd::string BuildAssetStatusJson(const AZStd::string& path, AzFramework::AssetSystem::AssetStatus status);

    //! {"source_path": <as given>, "jobs": [{"job_key", "platform",
    //!  "builder", "source_file", "watch_folder", "status", "status_detail"?,
    //!  "error_count", "warning_count", "job_run_key": "<decimal u64>",
    //!  "log"?, "truncated"?}]}
    AZStd::string BuildAssetJobsJson(const AZStd::string& sourcePath, const AZStd::vector<JobRecord>& jobs);

    //! {"connected": <bool>, "ping_ms": <number>}; a non-finite ping writes 0.
    AZStd::string BuildAssetProcessorStatusJson(bool connected, float pingMs);
} // namespace AiCompanion::AssetReadiness
