/*
 * Copyright Contributors to the AI Companion for O3DE Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/TestTypes.h>

#include "Snapshot/SceneSnapshotProvider.h"

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

    TEST_F(SceneSnapshotTestFixture, ValidateScene_ReturnsValidJson)
    {
        AZStd::string result = AiCompanion::SceneSnapshotProvider::ValidateScene();
        EXPECT_FALSE(result.empty());
        EXPECT_NE(result.find("warnings"), AZStd::string::npos);
        EXPECT_NE(result.find("status"), AZStd::string::npos);
    }
} // namespace UnitTest
