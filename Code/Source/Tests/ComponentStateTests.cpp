/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "Components/ComponentState.h"

#include <AzCore/JSON/document.h>

// The words and the reply shape of GetComponentState, checked without an
// editor: the state word, the service CRC form, the summary line an agent
// shows and the JSON object.

namespace UnitTest
{
    namespace State = AiCompanion::ComponentState;

    class ComponentStateFixture : public LeakDetectionFixture
    {
    };

    TEST_F(ComponentStateFixture, StateWordPrefersActiveThenPendingThenDisabled)
    {
        EXPECT_STREQ(State::StateWord(true, false, false), "active");
        EXPECT_STREQ(State::StateWord(true, true, true), "active");
        EXPECT_STREQ(State::StateWord(false, true, false), "pending");
        EXPECT_STREQ(State::StateWord(false, true, true), "pending");
        EXPECT_STREQ(State::StateWord(false, false, true), "disabled");
        EXPECT_STREQ(State::StateWord(false, false, false), "unknown");
    }

    TEST_F(ComponentStateFixture, ServiceCrcWordIsEightLowercaseHexDigits)
    {
        EXPECT_EQ(State::ServiceCrcWord(0x0F1A2B3Cu), "0x0f1a2b3c");
        EXPECT_EQ(State::ServiceCrcWord(0u), "0x00000000");
    }

    TEST_F(ComponentStateFixture, SummaryOfAnActiveComponentIsTheWordActive)
    {
        State::Info info;
        info.m_state = State::ActiveWord;
        EXPECT_EQ(State::Summarize(info), "active");
    }

    TEST_F(ComponentStateFixture, SummaryNamesTheIncompatibleSiblings)
    {
        State::Info info;
        info.m_name = "Sphere Shape";
        info.m_state = State::PendingWord;
        info.m_incompatibleWith = { "Box Shape" };
        EXPECT_EQ(State::Summarize(info), "disabled: incompatible with \"Box Shape\"");

        info.m_incompatibleWith.push_back("Capsule Shape");
        EXPECT_EQ(State::Summarize(info), "disabled: incompatible with \"Box Shape\", \"Capsule Shape\"");
    }

    TEST_F(ComponentStateFixture, SummaryListsMissingServicesAndPendingProviders)
    {
        State::Info info;
        info.m_state = State::PendingWord;
        info.m_missingRequiredServices = { "0x0f1a2b3c" };
        EXPECT_EQ(State::Summarize(info), "disabled: missing required service 0x0f1a2b3c");

        info.m_missingRequiredServices.push_back("0x00000001");
        info.m_waitingOnPending = { "Mesh" };
        EXPECT_EQ(State::Summarize(info), "disabled: missing required services 0x0f1a2b3c, 0x00000001, waiting on pending \"Mesh\"");
    }

    TEST_F(ComponentStateFixture, SummaryCoversPendingWithoutAReasonDisabledAndUnknown)
    {
        State::Info info;
        info.m_state = State::PendingWord;
        EXPECT_EQ(State::Summarize(info), "disabled: pending, reason not reported by the editor");
        info.m_state = State::DisabledWord;
        EXPECT_EQ(State::Summarize(info), "disabled by the user");
        info.m_state = State::UnknownWord;
        EXPECT_EQ(State::Summarize(info), "state unknown");
    }

    TEST_F(ComponentStateFixture, JsonCarriesEveryFieldWithTheArraysAlwaysPresent)
    {
        State::Info info;
        info.m_name = "Sphere Shape";
        info.m_state = State::PendingWord;
        info.m_incompatibleWith = { "Box Shape" };
        info.m_incompatibleServices = { "0x1234abcd" };
        info.m_warnings = { "a descriptor warning" };

        rapidjson::Document doc;
        doc.Parse(State::BuildComponentStateJson(info).c_str());
        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["name"].GetString(), "Sphere Shape");
        EXPECT_STREQ(doc["state"].GetString(), "pending");
        EXPECT_STREQ(doc["summary"].GetString(), "disabled: incompatible with \"Box Shape\"");
        ASSERT_TRUE(doc["incompatible_with"].IsArray());
        ASSERT_EQ(doc["incompatible_with"].Size(), 1u);
        EXPECT_STREQ(doc["incompatible_with"][0].GetString(), "Box Shape");
        ASSERT_TRUE(doc["waiting_on_pending"].IsArray());
        EXPECT_EQ(doc["waiting_on_pending"].Size(), 0u);
        ASSERT_TRUE(doc["missing_required_services"].IsArray());
        EXPECT_EQ(doc["missing_required_services"].Size(), 0u);
        ASSERT_EQ(doc["incompatible_services"].Size(), 1u);
        EXPECT_STREQ(doc["incompatible_services"][0].GetString(), "0x1234abcd");
        ASSERT_EQ(doc["warnings"].Size(), 1u);
        EXPECT_STREQ(doc["warnings"][0].GetString(), "a descriptor warning");
    }

    TEST_F(ComponentStateFixture, JsonOfAnActiveComponentHasEmptyArrays)
    {
        State::Info info;
        info.m_name = "Box Shape";
        info.m_state = State::ActiveWord;

        rapidjson::Document doc;
        doc.Parse(State::BuildComponentStateJson(info).c_str());
        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["state"].GetString(), "active");
        EXPECT_STREQ(doc["summary"].GetString(), "active");
        for (const char* key :
             { "incompatible_with", "waiting_on_pending", "missing_required_services", "incompatible_services", "warnings" })
        {
            ASSERT_TRUE(doc.HasMember(key)) << key;
            EXPECT_EQ(doc[key].Size(), 0u) << key;
        }
    }
} // namespace UnitTest
