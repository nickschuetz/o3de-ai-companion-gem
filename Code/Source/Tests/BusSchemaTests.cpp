/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/JSON/document.h>
#include <AzCore/RTTI/BehaviorContext.h>

#include "Introspection/BusSchema.h"

namespace UnitTest
{
    // A probe bus reflected with per-argument {name, tooltip} metadata, so the
    // schema builder has names and tooltips to surface (the thing the editor's
    // generated .pyi stub drops for EBuses).
    class SchemaProbeRequests : public AZ::ComponentBus
    {
    public:
        virtual void Move(float dx, float dy) = 0;
        virtual bool Toggle(bool on) = 0;
        virtual int Query() = 0;
    };
    using SchemaProbeRequestBus = AZ::EBus<SchemaProbeRequests>;

    // A broadcast-only bus (no address) so its events reflect as Broadcast with
    // no leading bus-id argument. Exercises the m_broadcast path in WriteBus.
    class BroadcastProbeRequests : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        virtual void Ping(int code) = 0;
    };
    using BroadcastProbeRequestBus = AZ::EBus<BroadcastProbeRequests>;

    class BusSchemaTestFixture : public LeakDetectionFixture
    {
    protected:
        void SetUp() override
        {
            LeakDetectionFixture::SetUp();
            m_behaviorContext = aznew AZ::BehaviorContext();
            m_behaviorContext->EBus<SchemaProbeRequestBus>("SchemaProbeRequestBus")
                ->Event("Move", &SchemaProbeRequestBus::Events::Move, { { { "dx", "delta x" }, { "dy", "delta y" } } })
                ->Event("Toggle", &SchemaProbeRequestBus::Events::Toggle, { { { "on", "enable flag" } } })
                ->Event("Query", &SchemaProbeRequestBus::Events::Query);
            m_behaviorContext->EBus<BroadcastProbeRequestBus>("BroadcastProbeRequestBus")
                ->Event("Ping", &BroadcastProbeRequestBus::Events::Ping, { { { "code", "status code" } } });
        }

        void TearDown() override
        {
            delete m_behaviorContext;
            m_behaviorContext = nullptr;
            LeakDetectionFixture::TearDown();
        }

        AZ::BehaviorContext* m_behaviorContext = nullptr;
    };

    TEST_F(BusSchemaTestFixture, NullContext_ReturnsError)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(nullptr, "SchemaProbeRequestBus");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());
        EXPECT_TRUE(doc.HasMember("error"));
    }

    TEST_F(BusSchemaTestFixture, UnknownBus_ReturnsError)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(m_behaviorContext, "NoSuchBus");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());
        EXPECT_TRUE(doc.HasMember("error"));
    }

    TEST_F(BusSchemaTestFixture, EmptyName_ListsBuses)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(m_behaviorContext, "");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());
        ASSERT_TRUE(doc.HasMember("buses"));
        ASSERT_TRUE(doc["buses"].IsArray());
        bool found = false;
        for (const auto& entry : doc["buses"].GetArray())
        {
            if (AZStd::string(entry.GetString()) == "SchemaProbeRequestBus")
            {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    TEST_F(BusSchemaTestFixture, BusSchema_HasEventsSortedByName)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(m_behaviorContext, "SchemaProbeRequestBus");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());
        EXPECT_STREQ(doc["name"].GetString(), "SchemaProbeRequestBus");
        ASSERT_TRUE(doc["events"].IsArray());
        const auto& events = doc["events"];
        ASSERT_EQ(events.Size(), 3u);
        // Sorted: Move, Query, Toggle.
        EXPECT_STREQ(events[0]["name"].GetString(), "Move");
        EXPECT_STREQ(events[1]["name"].GetString(), "Query");
        EXPECT_STREQ(events[2]["name"].GetString(), "Toggle");
    }

    TEST_F(BusSchemaTestFixture, Event_CarriesArgumentNamesAndTooltips)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(m_behaviorContext, "SchemaProbeRequestBus");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());

        // "Move" is events[0] after sorting.
        const auto& move = doc["events"][0];
        ASSERT_STREQ(move["name"].GetString(), "Move");
        ASSERT_TRUE(move["args"].IsArray());
        ASSERT_EQ(move["args"].Size(), 2u);
        EXPECT_STREQ(move["args"][0]["name"].GetString(), "dx");
        EXPECT_STREQ(move["args"][0]["type"].GetString(), "float");
        EXPECT_STREQ(move["args"][0]["tooltip"].GetString(), "delta x");
        EXPECT_STREQ(move["args"][1]["name"].GetString(), "dy");
        EXPECT_STREQ(move["args"][1]["tooltip"].GetString(), "delta y");
        EXPECT_STREQ(move["returns"].GetString(), "void");
    }

    TEST_F(BusSchemaTestFixture, Event_CarriesReturnAndScalarTypes)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(m_behaviorContext, "SchemaProbeRequestBus");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());

        // "Toggle" is events[2]; one bool arg, bool return. Type names are the
        // engine's canonical reflected names on 26.05.0 (verified, then pinned).
        const auto& toggle = doc["events"][2];
        ASSERT_STREQ(toggle["name"].GetString(), "Toggle");
        ASSERT_EQ(toggle["args"].Size(), 1u);
        EXPECT_STREQ(toggle["args"][0]["name"].GetString(), "on");
        EXPECT_STREQ(toggle["args"][0]["type"].GetString(), "bool");
        EXPECT_STREQ(toggle["args"][0]["tooltip"].GetString(), "enable flag");
        EXPECT_STREQ(toggle["returns"].GetString(), "bool");

        // "Query" takes no arguments and returns int.
        const auto& query = doc["events"][1];
        ASSERT_STREQ(query["name"].GetString(), "Query");
        EXPECT_EQ(query["args"].Size(), 0u);
        EXPECT_STREQ(query["returns"].GetString(), "int");
    }

    TEST_F(BusSchemaTestFixture, BroadcastBus_NoAddressArgument)
    {
        const AZStd::string result = AiCompanion::BuildBusSchemaJson(m_behaviorContext, "BroadcastProbeRequestBus");
        rapidjson::Document doc;
        ASSERT_FALSE(doc.Parse(result.c_str()).HasParseError());

        ASSERT_TRUE(doc["events"].IsArray());
        ASSERT_EQ(doc["events"].Size(), 1u);
        const auto& ping = doc["events"][0];
        EXPECT_STREQ(ping["name"].GetString(), "Ping");
        // No address policy, so the event reflects as a Broadcast with no leading
        // bus-id argument: the single user argument is the only one.
        EXPECT_STREQ(ping["call_type"].GetString(), "Broadcast");
        ASSERT_EQ(ping["args"].Size(), 1u);
        EXPECT_STREQ(ping["args"][0]["name"].GetString(), "code");
        EXPECT_STREQ(ping["args"][0]["type"].GetString(), "int");
        EXPECT_STREQ(ping["args"][0]["tooltip"].GetString(), "status code");
    }
} // namespace UnitTest
