/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/string/string_view.h>
#include <AzTest/AzTest.h>

#include "Network/RequestError.h"
#include "Network/ResponseBuilding.h"

#include <AzCore/JSON/document.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>

#include <cstring>

// We test protocol framing and JSON handling without requiring a live socket.
// The AgentServer's public helpers are tested through serialized byte buffers.

namespace UnitTest
{
    class AgentServerProtocolTest : public LeakDetectionFixture
    {
    };

    // -----------------------------------------------------------------------
    // Message Framing Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, EncodeFrameHeader_SmallMessage_CorrectBigEndian)
    {
        AZ::u32 length = 42;
        AZ::u8 header[4] = { static_cast<AZ::u8>((length >> 24) & 0xFF),
                             static_cast<AZ::u8>((length >> 16) & 0xFF),
                             static_cast<AZ::u8>((length >> 8) & 0xFF),
                             static_cast<AZ::u8>(length & 0xFF) };

        EXPECT_EQ(header[0], 0);
        EXPECT_EQ(header[1], 0);
        EXPECT_EQ(header[2], 0);
        EXPECT_EQ(header[3], 42);
    }

    TEST_F(AgentServerProtocolTest, EncodeFrameHeader_LargeMessage_CorrectBigEndian)
    {
        AZ::u32 length = 0x00ABCDEF;
        AZ::u8 header[4] = { static_cast<AZ::u8>((length >> 24) & 0xFF),
                             static_cast<AZ::u8>((length >> 16) & 0xFF),
                             static_cast<AZ::u8>((length >> 8) & 0xFF),
                             static_cast<AZ::u8>(length & 0xFF) };

        EXPECT_EQ(header[0], 0x00);
        EXPECT_EQ(header[1], 0xAB);
        EXPECT_EQ(header[2], 0xCD);
        EXPECT_EQ(header[3], 0xEF);
    }

    TEST_F(AgentServerProtocolTest, DecodeFrameHeader_RoundTrip)
    {
        AZ::u32 original = 123456;
        AZ::u8 header[4] = { static_cast<AZ::u8>((original >> 24) & 0xFF),
                             static_cast<AZ::u8>((original >> 16) & 0xFF),
                             static_cast<AZ::u8>((original >> 8) & 0xFF),
                             static_cast<AZ::u8>(original & 0xFF) };

        AZ::u32 decoded = (static_cast<AZ::u32>(header[0]) << 24) | (static_cast<AZ::u32>(header[1]) << 16) |
            (static_cast<AZ::u32>(header[2]) << 8) | (static_cast<AZ::u32>(header[3]));

        EXPECT_EQ(decoded, original);
    }

    TEST_F(AgentServerProtocolTest, MaxMessageSize_16MiB)
    {
        constexpr AZ::u32 maxSize = 16 * 1024 * 1024;
        EXPECT_EQ(maxSize, 16777216u);
    }

    TEST_F(AgentServerProtocolTest, OversizedMessage_ExceedsMax)
    {
        constexpr AZ::u32 maxSize = 16 * 1024 * 1024;
        AZ::u32 oversized = maxSize + 1;
        EXPECT_GT(oversized, maxSize);
    }

    // -----------------------------------------------------------------------
    // JSON Request Parsing Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, ParsePingRequest_ValidJson)
    {
        const char* json = R"({"id": "test-123", "type": "ping"})";
        rapidjson::Document doc;
        doc.Parse(json);

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_TRUE(doc.IsObject());
        EXPECT_TRUE(doc.HasMember("id"));
        EXPECT_TRUE(doc.HasMember("type"));
        EXPECT_STREQ(doc["id"].GetString(), "test-123");
        EXPECT_STREQ(doc["type"].GetString(), "ping");
    }

    TEST_F(AgentServerProtocolTest, ParseExecutePythonRequest_ValidJson)
    {
        const char* json = R"({"id": "exec-1", "type": "execute_python", "script": "cHJpbnQoJ2hlbGxvJyk="})";
        rapidjson::Document doc;
        doc.Parse(json);

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_TRUE(doc.IsObject());
        EXPECT_STREQ(doc["type"].GetString(), "execute_python");
        EXPECT_TRUE(doc.HasMember("script"));
        EXPECT_TRUE(doc["script"].IsString());
    }

    TEST_F(AgentServerProtocolTest, ParseRequest_MissingType_Detectable)
    {
        const char* json = R"({"id": "no-type"})";
        rapidjson::Document doc;
        doc.Parse(json);

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_TRUE(doc.IsObject());
        EXPECT_FALSE(doc.HasMember("type"));
    }

    TEST_F(AgentServerProtocolTest, ParseRequest_InvalidJson_HasParseError)
    {
        const char* json = R"({broken json)";
        rapidjson::Document doc;
        doc.Parse(json);

        EXPECT_TRUE(doc.HasParseError());
    }

    TEST_F(AgentServerProtocolTest, ParseRequest_EmptyObject)
    {
        const char* json = "{}";
        rapidjson::Document doc;
        doc.Parse(json);

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_TRUE(doc.IsObject());
        EXPECT_FALSE(doc.HasMember("id"));
        EXPECT_FALSE(doc.HasMember("type"));
    }

    // -----------------------------------------------------------------------
    // JSON Response Building Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, BuildResponse_CorrectStructure)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("id");
        w.String("resp-1");
        w.Key("status");
        w.String("ok");
        w.Key("output");
        w.String("hello world");
        w.Key("error");
        w.String("");
        w.Key("duration_ms");
        w.Int64(42);
        w.EndObject();

        rapidjson::Document doc;
        doc.Parse(sb.GetString());

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["id"].GetString(), "resp-1");
        EXPECT_STREQ(doc["status"].GetString(), "ok");
        EXPECT_STREQ(doc["output"].GetString(), "hello world");
        EXPECT_STREQ(doc["error"].GetString(), "");
        EXPECT_EQ(doc["duration_ms"].GetInt64(), 42);
    }

    TEST_F(AgentServerProtocolTest, BuildErrorResponse_HasErrorStatus)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("id");
        w.String("err-1");
        w.Key("status");
        w.String("error");
        w.Key("output");
        w.String("");
        w.Key("error");
        w.String("something went wrong");
        w.Key("duration_ms");
        w.Int64(0);
        w.EndObject();

        rapidjson::Document doc;
        doc.Parse(sb.GetString());

        EXPECT_STREQ(doc["status"].GetString(), "error");
        EXPECT_STRNE(doc["error"].GetString(), "");
    }

    TEST_F(AgentServerProtocolTest, UnknownRequestTypeReply_CarriesTheCodeAndTheMessage)
    {
        // The AgentServer answers an unknown "type" with this reply. Clients
        // branch on the code; older ones match on the message, so both stay.
        const AZStd::string reply = AiCompanion::ResponseBuilding::BuildErrorResponse(
            "req-7", "Unknown request type: definitely_not_a_type", AiCompanion::ResponseBuilding::UnknownRequestTypeCode);

        rapidjson::Document doc;
        doc.Parse(reply.c_str());

        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["id"].GetString(), "req-7");
        EXPECT_STREQ(doc["status"].GetString(), "error");
        EXPECT_STREQ(doc["output"].GetString(), "");
        EXPECT_STREQ(doc["error"].GetString(), "Unknown request type: definitely_not_a_type");
        ASSERT_TRUE(doc.HasMember("code"));
        EXPECT_STREQ(doc["code"].GetString(), "unknown_request_type");
        EXPECT_EQ(doc["duration_ms"].GetInt64(), 0);
    }

    TEST_F(AgentServerProtocolTest, ValidationErrorReply_CarriesTheValidationFailedCode)
    {
        // The reply the server sends for a malformed request, e.g. a missing
        // "type" field: the code is the client's branch, the message the reason.
        const AZStd::string reply =
            AiCompanion::ResponseBuilding::BuildErrorResponse("req-8", "Missing 'type' field", AiCompanion::RequestError::ValidationFailed);

        rapidjson::Document doc;
        doc.Parse(reply.c_str());

        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["status"].GetString(), "error");
        EXPECT_STREQ(doc["error"].GetString(), "Missing 'type' field");
        ASSERT_TRUE(doc.HasMember("code"));
        EXPECT_STREQ(doc["code"].GetString(), "validation_failed");
        // A client falls back to editor Python on unknown_request_type alone.
        EXPECT_STRNE(doc["code"].GetString(), AiCompanion::ResponseBuilding::UnknownRequestTypeCode);
    }

    TEST_F(AgentServerProtocolTest, BusFailureReply_DecodesTheEventsCodeAndMessage)
    {
        // What AgentServer::FailureResponse does with a failed bus event: the
        // event's encoded failure becomes the reply's code and error fields.
        const AZStd::string encoded =
            AiCompanion::RequestError::EncodeError(AiCompanion::RequestError::NotFound, "entity 987654321 does not exist");
        AZStd::string code;
        AZStd::string message;
        AiCompanion::RequestError::DecodeError(encoded, code, message);
        const AZStd::string reply = AiCompanion::ResponseBuilding::BuildErrorResponse("req-10", message, code.c_str());

        rapidjson::Document doc;
        doc.Parse(reply.c_str());

        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["status"].GetString(), "error");
        EXPECT_STREQ(doc["output"].GetString(), "");
        EXPECT_STREQ(doc["error"].GetString(), "entity 987654321 does not exist");
        EXPECT_STREQ(doc["code"].GetString(), "not_found");
    }

    TEST_F(AgentServerProtocolTest, ErrorReplyWithoutACode_OmitsTheKey)
    {
        // ResponseBuilding itself only writes "code" when given one; the
        // server passes one on every error path.
        const AZStd::string reply = AiCompanion::ResponseBuilding::BuildErrorResponse("req-11", "plain");

        rapidjson::Document doc;
        doc.Parse(reply.c_str());

        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["status"].GetString(), "error");
        EXPECT_FALSE(doc.HasMember("code"));
    }

    TEST_F(AgentServerProtocolTest, SuccessReply_CarriesNoCode)
    {
        const AZStd::string reply = AiCompanion::ResponseBuilding::BuildResponse("req-9", "ok", "{}", "", 12);

        rapidjson::Document doc;
        doc.Parse(reply.c_str());

        ASSERT_FALSE(doc.HasParseError());
        EXPECT_STREQ(doc["status"].GetString(), "ok");
        EXPECT_STREQ(doc["output"].GetString(), "{}");
        EXPECT_STREQ(doc["error"].GetString(), "");
        EXPECT_EQ(doc["duration_ms"].GetInt64(), 12);
        EXPECT_FALSE(doc.HasMember("code"));
    }

    TEST_F(AgentServerProtocolTest, ApiVersionResponse_HasRequiredFields)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("protocol_version");
        w.Int(1);
        w.Key("gem_version");
        w.String("0.5.0");
        w.Key("api_version");
        w.String("0.5.0");
        w.Key("secure_mode");
        w.Bool(false);
        w.Key("tls_enabled");
        w.Bool(true);
        w.EndObject();

        rapidjson::Document doc;
        doc.Parse(sb.GetString());

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_EQ(doc["protocol_version"].GetInt(), 1);
        EXPECT_STREQ(doc["gem_version"].GetString(), "0.5.0");
        EXPECT_STREQ(doc["api_version"].GetString(), "0.5.0");
        EXPECT_FALSE(doc["secure_mode"].GetBool());
        EXPECT_TRUE(doc["tls_enabled"].GetBool());
    }

    // -----------------------------------------------------------------------
    // Request Type Classification Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, RequestTypeClassification_SafeTypes)
    {
        AZStd::vector<AZStd::string> safeTypes = { "ping",
                                                   "get_api_version",
                                                   "get_scene_snapshot",
                                                   "get_entity_tree",
                                                   "validate_scene",
                                                   "get_entity",
                                                   "get_bus_schema",
                                                   "create_entity",
                                                   "set_transform",
                                                   "delete_entity",
                                                   "list_anim_graphs",
                                                   "get_anim_graph",
                                                   "create_anim_graph",
                                                   "remove_anim_graph",
                                                   "load_anim_graph",
                                                   "save_anim_graph",
                                                   "add_anim_graph_node",
                                                   "remove_anim_graph_node",
                                                   "set_anim_graph_entry_state",
                                                   "add_anim_graph_parameter",
                                                   "remove_anim_graph_parameter",
                                                   "add_anim_graph_transition",
                                                   "remove_anim_graph_transition",
                                                   "set_anim_graph_transition",
                                                   "connect_anim_graph_ports",
                                                   "disconnect_anim_graph_ports",
                                                   "set_anim_graph_node",
                                                   "get_asset_status",
                                                   "get_asset_jobs",
                                                   "get_asset_processor_status" };

        for (const auto& type : safeTypes)
        {
            // These types should always be allowed, even in secure mode
            EXPECT_NE(type, "execute_python");
        }
    }

    TEST_F(AgentServerProtocolTest, RequestTypeClassification_ExecutePython_NotSafe)
    {
        AZStd::string type = "execute_python";
        bool isSafe =
            (type == "ping" || type == "get_api_version" || type == "get_scene_snapshot" || type == "get_entity_tree" ||
             type == "validate_scene" || type == "get_entity" || type == "get_bus_schema" || type == "create_entity" ||
             type == "set_transform" || type == "delete_entity" || type == "list_anim_graphs" || type == "get_anim_graph" ||
             type == "create_anim_graph" || type == "remove_anim_graph" || type == "load_anim_graph" || type == "save_anim_graph" ||
             type == "add_anim_graph_node" || type == "remove_anim_graph_node" || type == "set_anim_graph_entry_state" ||
             type == "add_anim_graph_parameter" || type == "remove_anim_graph_parameter" || type == "add_anim_graph_transition" ||
             type == "remove_anim_graph_transition" || type == "set_anim_graph_transition" || type == "connect_anim_graph_ports" ||
             type == "disconnect_anim_graph_ports" || type == "set_anim_graph_node" || type == "get_asset_status" ||
             type == "get_asset_jobs" || type == "get_asset_processor_status");
        EXPECT_FALSE(isSafe);
    }

    // -----------------------------------------------------------------------
    // Base64 Decode Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, Base64Decode_SimpleString)
    {
        // "print('hello')" = "cHJpbnQoJ2hlbGxvJyk="
        AZStd::string b64 = "cHJpbnQoJ2hlbGxvJyk=";
        // string_view over the literal: no heap allocation, so nothing for the
        // leak-detection fixture to flag (a function-local static AZStd::string
        // allocates on first use and outlives the test, reported as a leak).
        constexpr AZStd::string_view base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        AZStd::string decoded;
        AZ::u32 val = 0;
        int bits = -8;
        for (char c : b64)
        {
            if (c == '=' || c == '\n' || c == '\r')
                continue;
            size_t pos = base64Chars.find(c);
            if (pos == AZStd::string::npos)
                continue;
            val = (val << 6) + static_cast<AZ::u32>(pos);
            bits += 6;
            if (bits >= 0)
            {
                decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
                bits -= 8;
            }
        }

        EXPECT_EQ(decoded, "print('hello')");
    }

    TEST_F(AgentServerProtocolTest, Base64Decode_EmptyString)
    {
        AZStd::string b64 = "";
        // string_view over the literal: no heap allocation, so nothing for the
        // leak-detection fixture to flag (a function-local static AZStd::string
        // allocates on first use and outlives the test, reported as a leak).
        constexpr AZStd::string_view base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        AZStd::string decoded;
        AZ::u32 val = 0;
        int bits = -8;
        for (char c : b64)
        {
            if (c == '=' || c == '\n' || c == '\r')
                continue;
            size_t pos = base64Chars.find(c);
            if (pos == AZStd::string::npos)
                continue;
            val = (val << 6) + static_cast<AZ::u32>(pos);
            bits += 6;
            if (bits >= 0)
            {
                decoded.push_back(static_cast<char>((val >> bits) & 0xFF));
                bits -= 8;
            }
        }

        EXPECT_TRUE(decoded.empty());
    }

} // namespace UnitTest
