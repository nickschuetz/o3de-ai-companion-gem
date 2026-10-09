/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "Network/RequestError.h"
#include "Network/ResponseBuilding.h"

#include <AzCore/JSON/document.h>

#include <cstring>

namespace UnitTest
{
    class RequestErrorFixture : public LeakDetectionFixture
    {
    };

    TEST_F(RequestErrorFixture, Vocabulary_HasTheAgreedValues)
    {
        // The o3de-mcp client branches on these strings; a rename is a
        // protocol change.
        EXPECT_STREQ(AiCompanion::RequestError::ValidationFailed, "validation_failed");
        EXPECT_STREQ(AiCompanion::RequestError::NotFound, "not_found");
        EXPECT_STREQ(AiCompanion::RequestError::Unavailable, "unavailable");
        EXPECT_STREQ(AiCompanion::RequestError::EngineError, "engine_error");
        EXPECT_STREQ(AiCompanion::RequestError::SecureMode, "secure_mode");
        EXPECT_STREQ(AiCompanion::RequestError::UnknownRequestType, "unknown_request_type");
        EXPECT_STREQ(AiCompanion::RequestError::Timeout, "timeout");
        EXPECT_STREQ(AiCompanion::RequestError::ShuttingDown, "shutting_down");
        EXPECT_STREQ(AiCompanion::RequestError::ExecutionFailed, "execution_failed");
        // The unknown-type reply keeps the constant it always carried.
        EXPECT_STREQ(AiCompanion::ResponseBuilding::UnknownRequestTypeCode, AiCompanion::RequestError::UnknownRequestType);
    }

    TEST_F(RequestErrorFixture, Encode_WritesACodeAndMessageObject)
    {
        const AZStd::string encoded =
            AiCompanion::RequestError::EncodeError(AiCompanion::RequestError::NotFound, "entity 42 does not exist");

        rapidjson::Document doc;
        doc.Parse(encoded.c_str());
        ASSERT_FALSE(doc.HasParseError());
        ASSERT_TRUE(doc.IsObject());
        EXPECT_STREQ(doc["code"].GetString(), "not_found");
        EXPECT_STREQ(doc["message"].GetString(), "entity 42 does not exist");
        EXPECT_EQ(doc.MemberCount(), 2u);
    }

    TEST_F(RequestErrorFixture, EncodeDecode_RoundTripsAMessageWithQuotesAndBackslashes)
    {
        const AZStd::string message = "invalid entity name '9\"bad\\name' (letter first)";
        const AZStd::string encoded = AiCompanion::RequestError::EncodeError(AiCompanion::RequestError::ValidationFailed, message);

        AZStd::string code;
        AZStd::string decoded;
        AiCompanion::RequestError::DecodeError(encoded, code, decoded);
        EXPECT_EQ(code, "validation_failed");
        EXPECT_EQ(decoded, message);
    }

    TEST_F(RequestErrorFixture, Decode_PlainLegacyMessage_IsEngineErrorWithTheText)
    {
        // A failure that was never converted still reaches the client with a code.
        AZStd::string code;
        AZStd::string message;
        AiCompanion::RequestError::DecodeError("no root prefab is assigned", code, message);
        EXPECT_EQ(code, "engine_error");
        EXPECT_EQ(message, "no root prefab is assigned");
    }

    TEST_F(RequestErrorFixture, Decode_MalformedJson_IsEngineErrorWithTheWholeText)
    {
        AZStd::string code;
        AZStd::string message;
        AiCompanion::RequestError::DecodeError(R"({"code": "not_found", "message": )", code, message);
        EXPECT_EQ(code, "engine_error");
        EXPECT_EQ(message, R"({"code": "not_found", "message": )");
    }

    TEST_F(RequestErrorFixture, Decode_ObjectWithoutBothStrings_IsEngineErrorWithTheWholeText)
    {
        AZStd::string code;
        AZStd::string message;
        AiCompanion::RequestError::DecodeError(R"({"code": "not_found"})", code, message);
        EXPECT_EQ(code, "engine_error");
        EXPECT_EQ(message, R"({"code": "not_found"})");

        AiCompanion::RequestError::DecodeError(R"({"code": 7, "message": "x"})", code, message);
        EXPECT_EQ(code, "engine_error");
        EXPECT_EQ(message, R"({"code": 7, "message": "x"})");

        AiCompanion::RequestError::DecodeError("", code, message);
        EXPECT_EQ(code, "engine_error");
        EXPECT_EQ(message, "");
    }

    TEST_F(RequestErrorFixture, Decode_PassesAnUnlistedCodeThrough)
    {
        // The vocabulary can grow without the decoder changing.
        AZStd::string code;
        AZStd::string message;
        AiCompanion::RequestError::DecodeError(R"({"code": "future_code", "message": "later"})", code, message);
        EXPECT_EQ(code, "future_code");
        EXPECT_EQ(message, "later");
    }
} // namespace UnitTest
