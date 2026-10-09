/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "Network/RequestParsing.h"

#include <rapidjson/document.h>

namespace UnitTest
{
    class RequestParsingFixture : public LeakDetectionFixture
    {
    protected:
        rapidjson::Document Parse(const char* json)
        {
            rapidjson::Document doc;
            doc.Parse(json);
            EXPECT_FALSE(doc.HasParseError());
            return doc;
        }
    };

    TEST_F(RequestParsingFixture, EntityId_AcceptsNumberStringAndBracketedString)
    {
        AZ::u64 id = 0;
        EXPECT_TRUE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": 42})")["v"], id));
        EXPECT_EQ(id, 42u);
        EXPECT_TRUE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": "17720413266153757478"})")["v"], id));
        EXPECT_EQ(id, 17720413266153757478ull);
        EXPECT_TRUE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": "[123]"})")["v"], id));
        EXPECT_EQ(id, 123u);
    }

    TEST_F(RequestParsingFixture, EntityId_RejectsZeroGarbageAndWrongTypes)
    {
        AZ::u64 id = 7;
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": 0})")["v"], id));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": "0"})")["v"], id));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": "12abc"})")["v"], id));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": ""})")["v"], id));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": [1]})")["v"], id));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseEntityId(Parse(R"({"v": -5})")["v"], id));
        EXPECT_EQ(id, 7u); // untouched on failure
    }

    TEST_F(RequestParsingFixture, Vector3_AcceptsThreeNumbers)
    {
        AZ::Vector3 v;
        EXPECT_TRUE(AiCompanion::RequestParsing::ParseVector3(Parse(R"({"v": [1, 2.5, -3]})")["v"], v));
        EXPECT_FLOAT_EQ(v.GetX(), 1.0f);
        EXPECT_FLOAT_EQ(v.GetY(), 2.5f);
        EXPECT_FLOAT_EQ(v.GetZ(), -3.0f);
    }

    TEST_F(RequestParsingFixture, Vector3_RejectsWrongShapeAndOutOfBounds)
    {
        AZ::Vector3 v;
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseVector3(Parse(R"({"v": [1, 2]})")["v"], v));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseVector3(Parse(R"({"v": [1, "2", 3]})")["v"], v));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseVector3(Parse(R"({"v": "1,2,3"})")["v"], v));
        EXPECT_FALSE(AiCompanion::RequestParsing::ParseVector3(Parse(R"({"v": [1, 2, 1e9]})")["v"], v));
    }
} // namespace UnitTest
