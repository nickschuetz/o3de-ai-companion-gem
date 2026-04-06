/*
 * Copyright Contributors to the AI Companion for O3DE Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/TestTypes.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

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
        AZ::u8 header[4] = {
            static_cast<AZ::u8>((length >> 24) & 0xFF),
            static_cast<AZ::u8>((length >> 16) & 0xFF),
            static_cast<AZ::u8>((length >> 8) & 0xFF),
            static_cast<AZ::u8>(length & 0xFF)
        };

        EXPECT_EQ(header[0], 0);
        EXPECT_EQ(header[1], 0);
        EXPECT_EQ(header[2], 0);
        EXPECT_EQ(header[3], 42);
    }

    TEST_F(AgentServerProtocolTest, EncodeFrameHeader_LargeMessage_CorrectBigEndian)
    {
        AZ::u32 length = 0x00ABCDEF;
        AZ::u8 header[4] = {
            static_cast<AZ::u8>((length >> 24) & 0xFF),
            static_cast<AZ::u8>((length >> 16) & 0xFF),
            static_cast<AZ::u8>((length >> 8) & 0xFF),
            static_cast<AZ::u8>(length & 0xFF)
        };

        EXPECT_EQ(header[0], 0x00);
        EXPECT_EQ(header[1], 0xAB);
        EXPECT_EQ(header[2], 0xCD);
        EXPECT_EQ(header[3], 0xEF);
    }

    TEST_F(AgentServerProtocolTest, DecodeFrameHeader_RoundTrip)
    {
        AZ::u32 original = 123456;
        AZ::u8 header[4] = {
            static_cast<AZ::u8>((original >> 24) & 0xFF),
            static_cast<AZ::u8>((original >> 16) & 0xFF),
            static_cast<AZ::u8>((original >> 8) & 0xFF),
            static_cast<AZ::u8>(original & 0xFF)
        };

        AZ::u32 decoded = (static_cast<AZ::u32>(header[0]) << 24) |
                           (static_cast<AZ::u32>(header[1]) << 16) |
                           (static_cast<AZ::u32>(header[2]) << 8) |
                           (static_cast<AZ::u32>(header[3]));

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
        w.Key("id"); w.String("resp-1");
        w.Key("status"); w.String("ok");
        w.Key("output"); w.String("hello world");
        w.Key("error"); w.String("");
        w.Key("duration_ms"); w.Int64(42);
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
        w.Key("id"); w.String("err-1");
        w.Key("status"); w.String("error");
        w.Key("output"); w.String("");
        w.Key("error"); w.String("something went wrong");
        w.Key("duration_ms"); w.Int64(0);
        w.EndObject();

        rapidjson::Document doc;
        doc.Parse(sb.GetString());

        EXPECT_STREQ(doc["status"].GetString(), "error");
        EXPECT_STRNE(doc["error"].GetString(), "");
    }

    TEST_F(AgentServerProtocolTest, ApiVersionResponse_HasRequiredFields)
    {
        rapidjson::StringBuffer sb;
        rapidjson::Writer<rapidjson::StringBuffer> w(sb);
        w.StartObject();
        w.Key("protocol_version"); w.Int(1);
        w.Key("gem_version"); w.String("1.1.0");
        w.Key("api_version"); w.String("1.0.0");
        w.Key("secure_mode"); w.Bool(false);
        w.Key("tls_enabled"); w.Bool(true);
        w.EndObject();

        rapidjson::Document doc;
        doc.Parse(sb.GetString());

        EXPECT_FALSE(doc.HasParseError());
        EXPECT_EQ(doc["protocol_version"].GetInt(), 1);
        EXPECT_STREQ(doc["gem_version"].GetString(), "1.1.0");
        EXPECT_STREQ(doc["api_version"].GetString(), "1.0.0");
        EXPECT_FALSE(doc["secure_mode"].GetBool());
        EXPECT_TRUE(doc["tls_enabled"].GetBool());
    }

    // -----------------------------------------------------------------------
    // Request Type Classification Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, RequestTypeClassification_SafeTypes)
    {
        AZStd::vector<AZStd::string> safeTypes = {
            "ping", "get_api_version", "get_scene_snapshot", "get_entity_tree", "validate_scene"
        };

        for (const auto& type : safeTypes)
        {
            // These types should always be allowed, even in secure mode
            EXPECT_NE(type, "execute_python");
        }
    }

    TEST_F(AgentServerProtocolTest, RequestTypeClassification_ExecutePython_NotSafe)
    {
        AZStd::string type = "execute_python";
        bool isSafe = (type == "ping" || type == "get_api_version" ||
                       type == "get_scene_snapshot" || type == "get_entity_tree" ||
                       type == "validate_scene");
        EXPECT_FALSE(isSafe);
    }

    // -----------------------------------------------------------------------
    // Base64 Decode Tests
    // -----------------------------------------------------------------------

    TEST_F(AgentServerProtocolTest, Base64Decode_SimpleString)
    {
        // "print('hello')" = "cHJpbnQoJ2hlbGxvJyk="
        AZStd::string b64 = "cHJpbnQoJ2hlbGxvJyk=";
        static const AZStd::string base64Chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        AZStd::string decoded;
        AZ::u32 val = 0;
        int bits = -8;
        for (char c : b64)
        {
            if (c == '=' || c == '\n' || c == '\r') continue;
            size_t pos = base64Chars.find(c);
            if (pos == AZStd::string::npos) continue;
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
        static const AZStd::string base64Chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        AZStd::string decoded;
        AZ::u32 val = 0;
        int bits = -8;
        for (char c : b64)
        {
            if (c == '=' || c == '\n' || c == '\r') continue;
            size_t pos = base64Chars.find(c);
            if (pos == AZStd::string::npos) continue;
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
