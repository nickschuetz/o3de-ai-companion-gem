/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "Snapshot/SceneSnapshotProvider.h"

#include <AzCore/JSON/document.h>

namespace UnitTest
{
    class SceneSnapshotTestFixture : public LeakDetectionFixture
    {
    };

    TEST_F(SceneSnapshotTestFixture, CaptureSnapshot_ReturnsValidJson)
    {
        // Without a running application context, this should return an empty-but-valid result
        AZStd::string result = AiCompanion::SceneSnapshotProvider::CaptureSnapshot();
        EXPECT_FALSE(result.empty());
        // Should contain the entity_count key
        EXPECT_NE(result.find("entity_count"), AZStd::string::npos);
        EXPECT_NE(result.find("entities"), AZStd::string::npos);
    }

    TEST_F(SceneSnapshotTestFixture, CaptureEntityTree_ReturnsValidJson)
    {
        AZStd::string result = AiCompanion::SceneSnapshotProvider::CaptureEntityTree();
        EXPECT_FALSE(result.empty());
        EXPECT_NE(result.find("roots"), AZStd::string::npos);
    }

    TEST_F(SceneSnapshotTestFixture, CaptureEntity_InvalidId_ReturnsErrorJson)
    {
        AZStd::string result = AiCompanion::SceneSnapshotProvider::CaptureEntity(AZ::EntityId());
        EXPECT_NE(result.find("\"error\""), AZStd::string::npos);
        EXPECT_EQ(result.find("\"components\""), AZStd::string::npos);
    }

    TEST_F(SceneSnapshotTestFixture, CaptureEntity_UnknownId_ReturnsErrorJson)
    {
        // No application context is running, so no entity can be found.
        AZStd::string result = AiCompanion::SceneSnapshotProvider::CaptureEntity(AZ::EntityId(123456));
        EXPECT_NE(result.find("\"error\""), AZStd::string::npos);
        // Entity ids travel as decimal strings, never as JSON numbers.
        EXPECT_NE(result.find("\"entity_id\":\"123456\""), AZStd::string::npos) << result.c_str();
    }

    TEST_F(SceneSnapshotTestFixture, EffectiveScale_UniformAloneRepeatsOnEveryAxis)
    {
        const AZ::Vector3 effective = AiCompanion::SceneSnapshotProvider::EffectiveScale(3.0f, AZStd::nullopt);
        EXPECT_TRUE(effective.IsClose(AZ::Vector3(3.0f, 3.0f, 3.0f)));
    }

    TEST_F(SceneSnapshotTestFixture, EffectiveScale_MultipliesTheComponentByTheUniformScale)
    {
        const AZ::Vector3 effective = AiCompanion::SceneSnapshotProvider::EffectiveScale(2.0f, AZ::Vector3(50.0f, 50.0f, 1.0f));
        EXPECT_TRUE(effective.IsClose(AZ::Vector3(100.0f, 100.0f, 2.0f)));
    }

    TEST_F(SceneSnapshotTestFixture, EntityToJson_WithoutNonUniformScaleComponent)
    {
        AiCompanion::SceneSnapshotProvider::EntityInfo info;
        info.id = AZ::EntityId(42);
        info.name = "Cube";
        info.position = AZ::Vector3(1.0f, 2.0f, 3.0f);
        info.uniformScale = 2.0f;
        info.componentNames = { "TransformComponent" };

        const AZStd::string json = AiCompanion::SceneSnapshotProvider::EntityToJson(info);
        rapidjson::Document doc;
        doc.Parse(json.c_str());
        ASSERT_FALSE(doc.HasParseError()) << json.c_str();
        EXPECT_STREQ(doc["id"].GetString(), "42");
        EXPECT_STREQ(doc["name"].GetString(), "Cube");
        EXPECT_TRUE(doc["parent_id"].IsNull());
        ASSERT_TRUE(doc["scale"].IsArray());
        EXPECT_DOUBLE_EQ(doc["scale"][0].GetDouble(), 2.0);
        EXPECT_DOUBLE_EQ(doc["scale"][2].GetDouble(), 2.0);
        EXPECT_TRUE(doc["non_uniform_scale"].IsNull());
        ASSERT_TRUE(doc["effective_scale"].IsArray());
        EXPECT_DOUBLE_EQ(doc["effective_scale"][0].GetDouble(), 2.0);
        EXPECT_DOUBLE_EQ(doc["effective_scale"][1].GetDouble(), 2.0);
        EXPECT_DOUBLE_EQ(doc["effective_scale"][2].GetDouble(), 2.0);
        EXPECT_EQ(doc["components"].Size(), 1u);
    }

    TEST_F(SceneSnapshotTestFixture, EntityToJson_WithNonUniformScaleComponent)
    {
        AiCompanion::SceneSnapshotProvider::EntityInfo info;
        info.id = AZ::EntityId(7);
        info.name = "Wall";
        info.parentId = AZ::EntityId(42);
        info.uniformScale = 1.0f;
        info.nonUniformScale = AZ::Vector3(50.0f, 50.0f, 1.0f);
        info.componentNames = { "TransformComponent", "EditorNonUniformScaleComponent" };

        const AZStd::string json = AiCompanion::SceneSnapshotProvider::EntityToJson(info);
        rapidjson::Document doc;
        doc.Parse(json.c_str());
        ASSERT_FALSE(doc.HasParseError()) << json.c_str();
        EXPECT_STREQ(doc["parent_id"].GetString(), "42");
        // "scale" stays the Transform's uniform scale on every axis.
        EXPECT_DOUBLE_EQ(doc["scale"][0].GetDouble(), 1.0);
        EXPECT_DOUBLE_EQ(doc["scale"][1].GetDouble(), 1.0);
        EXPECT_DOUBLE_EQ(doc["scale"][2].GetDouble(), 1.0);
        ASSERT_TRUE(doc["non_uniform_scale"].IsArray());
        EXPECT_DOUBLE_EQ(doc["non_uniform_scale"][0].GetDouble(), 50.0);
        EXPECT_DOUBLE_EQ(doc["non_uniform_scale"][1].GetDouble(), 50.0);
        EXPECT_DOUBLE_EQ(doc["non_uniform_scale"][2].GetDouble(), 1.0);
        EXPECT_DOUBLE_EQ(doc["effective_scale"][0].GetDouble(), 50.0);
        EXPECT_DOUBLE_EQ(doc["effective_scale"][2].GetDouble(), 1.0);
    }

    TEST_F(SceneSnapshotTestFixture, EntityToJson_EffectiveScaleCombinesBoth)
    {
        AiCompanion::SceneSnapshotProvider::EntityInfo info;
        info.id = AZ::EntityId(9);
        info.uniformScale = 3.0f;
        info.nonUniformScale = AZ::Vector3(1.0f, 1.0f, 1.0f);

        rapidjson::Document doc;
        doc.Parse(AiCompanion::SceneSnapshotProvider::EntityToJson(info).c_str());
        ASSERT_FALSE(doc.HasParseError());
        EXPECT_DOUBLE_EQ(doc["scale"][0].GetDouble(), 3.0);
        EXPECT_DOUBLE_EQ(doc["non_uniform_scale"][0].GetDouble(), 1.0);
        EXPECT_DOUBLE_EQ(doc["effective_scale"][0].GetDouble(), 3.0);
        EXPECT_DOUBLE_EQ(doc["effective_scale"][2].GetDouble(), 3.0);
    }

    TEST_F(SceneSnapshotTestFixture, ValidateScene_ReturnsValidJson)
    {
        AZStd::string result = AiCompanion::SceneSnapshotProvider::ValidateScene();
        EXPECT_FALSE(result.empty());
        EXPECT_NE(result.find("warnings"), AZStd::string::npos);
        EXPECT_NE(result.find("status"), AZStd::string::npos);
    }
} // namespace UnitTest
