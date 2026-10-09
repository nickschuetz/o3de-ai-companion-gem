/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "Assets/AssetReadiness.h"

#include <AzCore/JSON/document.h>

#include <cmath>

// The words, argument rules and reply shapes of the asset readiness request
// types, checked without an Asset Processor: the status and job-state
// mappings, the log cut, the path rule and the three JSON replies.

namespace UnitTest
{
    namespace Readiness = AiCompanion::AssetReadiness;
    using AzFramework::AssetSystem::AssetStatus;

    class AssetReadinessFixture : public LeakDetectionFixture
    {
    protected:
        rapidjson::Document Parse(const AZStd::string& json)
        {
            rapidjson::Document doc;
            doc.Parse(json.c_str(), json.size());
            EXPECT_FALSE(doc.HasParseError()) << json.c_str();
            return doc;
        }
    };

    // -- Asset status words ---------------------------------------------------

    TEST_F(AssetReadinessFixture, AssetStatusWord_MapsEveryEngineValue)
    {
        // The six words of the get_asset_status contract, one per engine value.
        EXPECT_STREQ(Readiness::AssetStatusWord(AzFramework::AssetSystem::AssetStatus_Unknown), "unknown");
        EXPECT_STREQ(Readiness::AssetStatusWord(AzFramework::AssetSystem::AssetStatus_Missing), "missing");
        EXPECT_STREQ(Readiness::AssetStatusWord(AzFramework::AssetSystem::AssetStatus_Queued), "queued");
        EXPECT_STREQ(Readiness::AssetStatusWord(AzFramework::AssetSystem::AssetStatus_Compiling), "compiling");
        EXPECT_STREQ(Readiness::AssetStatusWord(AzFramework::AssetSystem::AssetStatus_Compiled), "compiled");
        EXPECT_STREQ(Readiness::AssetStatusWord(AzFramework::AssetSystem::AssetStatus_Failed), "failed");
    }

    TEST_F(AssetReadinessFixture, AssetStatusWord_UnknownValueReadsAsUnknown)
    {
        EXPECT_STREQ(Readiness::AssetStatusWord(static_cast<AssetStatus>(99)), "unknown");
    }

    // -- Job state words -------------------------------------------------------

    TEST_F(AssetReadinessFixture, JobStateWord_MapsEveryState)
    {
        AZStd::string detail = "stale";
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::Queued, detail), "queued");
        EXPECT_TRUE(detail.empty());
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::InProgress, detail), "in_progress");
        EXPECT_TRUE(detail.empty());
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::Failed, detail), "failed");
        EXPECT_TRUE(detail.empty());
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::Completed, detail), "completed");
        EXPECT_TRUE(detail.empty());
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::Missing, detail), "missing");
        EXPECT_TRUE(detail.empty());
    }

    TEST_F(AssetReadinessFixture, JobStateWord_LongFailureIsFailedWithDetail)
    {
        AZStd::string detail;
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::FailedInvalidSourceNameExceedsMaxLimit, detail), "failed");
        EXPECT_EQ(detail, "invalid_source_name_exceeds_max_limit");
    }

    TEST_F(AssetReadinessFixture, JobStateWord_AnyAndUnknownValuesReadAsUnknown)
    {
        AZStd::string detail = "stale";
        EXPECT_STREQ(Readiness::JobStateWord(Readiness::JobState::Any, detail), "unknown");
        EXPECT_TRUE(detail.empty());
        EXPECT_STREQ(Readiness::JobStateWord(static_cast<Readiness::JobState>(42), detail), "unknown");
    }

    TEST_F(AssetReadinessFixture, JobState_MirrorsTheEngineEnumValues)
    {
        // JobStatus is written to the Asset Processor's database, so the
        // engine appends values and never reorders them; the mirror relies
        // on exactly these numbers (the editor component static_asserts the
        // same against the AzToolsFramework header).
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::Any), -1);
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::Queued), 0);
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::InProgress), 1);
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::Failed), 2);
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::FailedInvalidSourceNameExceedsMaxLimit), 3);
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::Completed), 4);
        EXPECT_EQ(static_cast<AZ::s32>(Readiness::JobState::Missing), 5);
    }

    TEST_F(AssetReadinessFixture, IsFailedState_BothFailuresOnly)
    {
        EXPECT_TRUE(Readiness::IsFailedState(Readiness::JobState::Failed));
        EXPECT_TRUE(Readiness::IsFailedState(Readiness::JobState::FailedInvalidSourceNameExceedsMaxLimit));
        EXPECT_FALSE(Readiness::IsFailedState(Readiness::JobState::Queued));
        EXPECT_FALSE(Readiness::IsFailedState(Readiness::JobState::InProgress));
        EXPECT_FALSE(Readiness::IsFailedState(Readiness::JobState::Completed));
        EXPECT_FALSE(Readiness::IsFailedState(Readiness::JobState::Missing));
        EXPECT_FALSE(Readiness::IsFailedState(Readiness::JobState::Any));
    }

    // -- Log truncation --------------------------------------------------------

    TEST_F(AssetReadinessFixture, TruncateJobLog_LeavesAShortLogAlone)
    {
        AZStd::string log = "short";
        EXPECT_FALSE(Readiness::TruncateJobLog(log, 5));
        EXPECT_EQ(log, "short");
        EXPECT_FALSE(Readiness::TruncateJobLog(log));
        EXPECT_EQ(log, "short");
    }

    TEST_F(AssetReadinessFixture, TruncateJobLog_CutsAtTheLimit)
    {
        AZStd::string log = "0123456789";
        EXPECT_TRUE(Readiness::TruncateJobLog(log, 4));
        EXPECT_EQ(log, "0123");
    }

    TEST_F(AssetReadinessFixture, TruncateJobLog_DefaultLimitIs64KB)
    {
        EXPECT_EQ(Readiness::MaxJobLogBytes, static_cast<size_t>(64 * 1024));
        AZStd::string log(Readiness::MaxJobLogBytes + 1, 'x');
        EXPECT_TRUE(Readiness::TruncateJobLog(log));
        EXPECT_EQ(log.size(), Readiness::MaxJobLogBytes);
        AZStd::string exact(Readiness::MaxJobLogBytes, 'x');
        EXPECT_FALSE(Readiness::TruncateJobLog(exact));
    }

    TEST_F(AssetReadinessFixture, TruncateJobLog_NeverSplitsAUtf8Sequence)
    {
        // "ab" then U+00E9 (two bytes: C3 A9) then "cd"; a cut at byte 3
        // would land inside the sequence and back off to byte 2.
        AZStd::string log = "ab\xC3\xA9"
                            "cd";
        EXPECT_TRUE(Readiness::TruncateJobLog(log, 3));
        EXPECT_EQ(log, "ab");

        // A cut right after the sequence keeps it whole.
        log = "ab\xC3\xA9"
              "cd";
        EXPECT_TRUE(Readiness::TruncateJobLog(log, 4));
        EXPECT_EQ(log, "ab\xC3\xA9");
    }

    // -- Path rule -------------------------------------------------------------

    TEST_F(AssetReadinessFixture, IsAcceptablePath_AcceptsTheThreePathForms)
    {
        AZStd::string reason;
        EXPECT_TRUE(Readiness::IsAcceptablePath("Assets/Probe/thing.animgraph", reason)) << reason.c_str();
        EXPECT_TRUE(Readiness::IsAcceptablePath("assets/probe/thing.animgraph", reason)) << reason.c_str();
        EXPECT_TRUE(Readiness::IsAcceptablePath("/home/me/Project/Assets/Probe/thing.animgraph", reason)) << reason.c_str();
        EXPECT_TRUE(Readiness::IsAcceptablePath("C:\\Project\\Assets\\Probe\\thing.animgraph", reason)) << reason.c_str();
        EXPECT_TRUE(Readiness::IsAcceptablePath("levels/defaultlevel/defaultlevel.spawnable", reason)) << reason.c_str();
    }

    TEST_F(AssetReadinessFixture, IsAcceptablePath_RefusesEmptyLongAndControlCharacters)
    {
        AZStd::string reason;
        EXPECT_FALSE(Readiness::IsAcceptablePath("", reason));
        EXPECT_EQ(reason, "the path is empty");

        AZStd::string longPath(Readiness::MaxPathBytes + 1, 'a');
        EXPECT_FALSE(Readiness::IsAcceptablePath(longPath, reason));
        EXPECT_NE(reason.find("longer than"), AZStd::string::npos) << reason.c_str();
        AZStd::string exact(Readiness::MaxPathBytes, 'a');
        EXPECT_TRUE(Readiness::IsAcceptablePath(exact, reason)) << reason.c_str();

        EXPECT_FALSE(Readiness::IsAcceptablePath("a\nb", reason));
        EXPECT_EQ(reason, "the path contains a control character");
        EXPECT_FALSE(Readiness::IsAcceptablePath(AZStd::string("a\0b", 3), reason));
        EXPECT_EQ(reason, "the path contains a control character");
        EXPECT_FALSE(Readiness::IsAcceptablePath("a\x7F", reason));
    }

    // -- Replies ---------------------------------------------------------------

    TEST_F(AssetReadinessFixture, BuildAssetStatusJson_EchoesThePathAndTheWord)
    {
        rapidjson::Document doc =
            Parse(Readiness::BuildAssetStatusJson("Assets/Probe/thing.animgraph", AzFramework::AssetSystem::AssetStatus_Compiled));
        EXPECT_STREQ(doc["path"].GetString(), "Assets/Probe/thing.animgraph");
        EXPECT_STREQ(doc["status"].GetString(), "compiled");
        EXPECT_TRUE(doc["connected"].GetBool());
        EXPECT_EQ(doc.MemberCount(), 3u);
    }

    TEST_F(AssetReadinessFixture, BuildAssetProcessorStatusJson_ConnectedAndPing)
    {
        rapidjson::Document doc = Parse(Readiness::BuildAssetProcessorStatusJson(true, 1.5f));
        EXPECT_TRUE(doc["connected"].GetBool());
        EXPECT_DOUBLE_EQ(doc["ping_ms"].GetDouble(), 1.5);
        EXPECT_EQ(doc.MemberCount(), 2u);
    }

    TEST_F(AssetReadinessFixture, BuildAssetProcessorStatusJson_DisconnectedAndNonFinitePingWriteZero)
    {
        rapidjson::Document doc = Parse(Readiness::BuildAssetProcessorStatusJson(false, 0.0f));
        EXPECT_FALSE(doc["connected"].GetBool());
        EXPECT_DOUBLE_EQ(doc["ping_ms"].GetDouble(), 0.0);

        // RapidJSON refuses to write NaN or infinity; the builder substitutes 0.
        doc = Parse(Readiness::BuildAssetProcessorStatusJson(true, std::nanf("")));
        EXPECT_DOUBLE_EQ(doc["ping_ms"].GetDouble(), 0.0);
        doc = Parse(Readiness::BuildAssetProcessorStatusJson(true, INFINITY));
        EXPECT_DOUBLE_EQ(doc["ping_ms"].GetDouble(), 0.0);
    }

    TEST_F(AssetReadinessFixture, BuildAssetJobsJson_EmptyListIsAnEmptyArray)
    {
        rapidjson::Document doc = Parse(Readiness::BuildAssetJobsJson("Assets/Probe/none.animgraph", {}));
        EXPECT_STREQ(doc["source_path"].GetString(), "Assets/Probe/none.animgraph");
        ASSERT_TRUE(doc["jobs"].IsArray());
        EXPECT_EQ(doc["jobs"].Size(), 0u);
    }

    TEST_F(AssetReadinessFixture, BuildAssetJobsJson_WritesEveryFieldAndTheRunKeyAsText)
    {
        Readiness::JobRecord job;
        job.m_jobKey = "Anim Graph";
        job.m_platform = "pc";
        job.m_builder = "{3A8E7A0A-5D0E-4C2A-9F0B-1E2D3C4B5A69}";
        job.m_sourceFile = "Assets/Probe/thing.animgraph";
        job.m_watchFolder = "/home/me/Project";
        job.m_state = Readiness::JobState::Completed;
        job.m_errorCount = 0;
        job.m_warningCount = 2;
        // Above 2^53: a number here would be corrupted by a double-based parser.
        job.m_jobRunKey = 18446744073709551615ull;

        rapidjson::Document doc = Parse(Readiness::BuildAssetJobsJson("Assets/Probe/thing.animgraph", { job }));
        ASSERT_EQ(doc["jobs"].Size(), 1u);
        const rapidjson::Value& out = doc["jobs"][0];
        EXPECT_STREQ(out["job_key"].GetString(), "Anim Graph");
        EXPECT_STREQ(out["platform"].GetString(), "pc");
        EXPECT_STREQ(out["builder"].GetString(), "{3A8E7A0A-5D0E-4C2A-9F0B-1E2D3C4B5A69}");
        EXPECT_STREQ(out["source_file"].GetString(), "Assets/Probe/thing.animgraph");
        EXPECT_STREQ(out["watch_folder"].GetString(), "/home/me/Project");
        EXPECT_STREQ(out["status"].GetString(), "completed");
        EXPECT_FALSE(out.HasMember("status_detail"));
        EXPECT_EQ(out["error_count"].GetInt64(), 0);
        EXPECT_EQ(out["warning_count"].GetInt64(), 2);
        ASSERT_TRUE(out["job_run_key"].IsString());
        EXPECT_STREQ(out["job_run_key"].GetString(), "18446744073709551615");
        // No log was requested: neither key is present.
        EXPECT_FALSE(out.HasMember("log"));
        EXPECT_FALSE(out.HasMember("truncated"));
    }

    TEST_F(AssetReadinessFixture, BuildAssetJobsJson_FailedJobCarriesDetailLogAndTruncation)
    {
        Readiness::JobRecord failed;
        failed.m_jobKey = "Anim Graph";
        failed.m_state = Readiness::JobState::FailedInvalidSourceNameExceedsMaxLimit;
        failed.m_errorCount = 1;
        failed.m_hasLog = true;
        failed.m_logAvailable = true;
        failed.m_log = "E: could not parse";
        failed.m_truncated = true;

        Readiness::JobRecord noLog;
        noLog.m_jobKey = "Other";
        noLog.m_state = Readiness::JobState::Failed;
        noLog.m_hasLog = true;
        noLog.m_logAvailable = false;

        rapidjson::Document doc = Parse(Readiness::BuildAssetJobsJson("x", { failed, noLog }));
        ASSERT_EQ(doc["jobs"].Size(), 2u);

        const rapidjson::Value& first = doc["jobs"][0];
        EXPECT_STREQ(first["status"].GetString(), "failed");
        EXPECT_STREQ(first["status_detail"].GetString(), "invalid_source_name_exceeds_max_limit");
        EXPECT_STREQ(first["log"].GetString(), "E: could not parse");
        EXPECT_TRUE(first["truncated"].GetBool());

        // The Asset Processor returned no log: the key is present and null,
        // and nothing was truncated.
        const rapidjson::Value& second = doc["jobs"][1];
        EXPECT_STREQ(second["status"].GetString(), "failed");
        EXPECT_FALSE(second.HasMember("status_detail"));
        EXPECT_TRUE(second["log"].IsNull());
        EXPECT_FALSE(second.HasMember("truncated"));
    }
} // namespace UnitTest
