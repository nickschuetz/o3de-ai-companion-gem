/*
 * Copyright Contributors to the AI Companion for O3DE Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzTest/AzTest.h>
#include <AzCore/UnitTest/TestTypes.h>

#include "Validation/InputValidator.h"

namespace UnitTest
{
    class InputValidatorTestFixture : public LeakDetectionFixture
    {
    };

    // --- Entity name validation ---

    TEST_F(InputValidatorTestFixture, EntityName_ValidSimple)
    {
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidEntityName("Player"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidEntityName("Enemy1"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidEntityName("my_entity"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidEntityName("player-two"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidEntityName("A"));
    }

    TEST_F(InputValidatorTestFixture, EntityName_RejectsEmpty)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName(""));
    }

    TEST_F(InputValidatorTestFixture, EntityName_RejectsStartWithDigit)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("1Entity"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("0"));
    }

    TEST_F(InputValidatorTestFixture, EntityName_RejectsStartWithUnderscore)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("_entity"));
    }

    TEST_F(InputValidatorTestFixture, EntityName_RejectsSpecialChars)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("entity!"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("ent ity"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("entity@home"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName("entity;drop"));
    }

    TEST_F(InputValidatorTestFixture, EntityName_RejectsTooLong)
    {
        AZStd::string longName(129, 'a');
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidEntityName(longName));

        AZStd::string maxName(128, 'a');
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidEntityName(maxName));
    }

    // --- Component type validation ---

    TEST_F(InputValidatorTestFixture, ComponentType_ValidNames)
    {
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidComponentType("Mesh"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidComponentType("PhysX Collider"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidComponentType("Directional Light"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidComponentType("PhysX Rigid Body (Dynamic)"));
    }

    TEST_F(InputValidatorTestFixture, ComponentType_RejectsEmpty)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidComponentType(""));
    }

    TEST_F(InputValidatorTestFixture, ComponentType_RejectsInjection)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidComponentType("Mesh; rm -rf /"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidComponentType("Mesh\n import os"));
    }

    // --- Position validation ---

    TEST_F(InputValidatorTestFixture, Position_ValidValues)
    {
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidPosition(0.0f, 0.0f, 0.0f));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidPosition(100.0f, -200.0f, 50.5f));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidPosition(10000.0f, -10000.0f, 0.0f));
    }

    TEST_F(InputValidatorTestFixture, Position_RejectsNaN)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidPosition(
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
    }

    TEST_F(InputValidatorTestFixture, Position_RejectsInfinity)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidPosition(
            std::numeric_limits<float>::infinity(), 0.0f, 0.0f));
    }

    TEST_F(InputValidatorTestFixture, Position_RejectsOutOfBounds)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidPosition(10001.0f, 0.0f, 0.0f));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidPosition(0.0f, -10001.0f, 0.0f));
    }

    // --- Asset path validation ---

    TEST_F(InputValidatorTestFixture, AssetPath_ValidPaths)
    {
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidAssetPath("Scripts/Lua/player.lua"));
        EXPECT_TRUE(AiCompanion::InputValidator::IsValidAssetPath("Assets/Prefabs/Player.prefab"));
    }

    TEST_F(InputValidatorTestFixture, AssetPath_RejectsEmpty)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidAssetPath(""));
    }

    TEST_F(InputValidatorTestFixture, AssetPath_RejectsTraversal)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidAssetPath("../../etc/passwd"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidAssetPath("Scripts/../../../secret"));
    }

    TEST_F(InputValidatorTestFixture, AssetPath_RejectsAbsolute)
    {
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidAssetPath("/etc/passwd"));
        EXPECT_FALSE(AiCompanion::InputValidator::IsValidAssetPath("C:\\Windows\\System32"));
    }

    // --- Sanitize entity name ---

    TEST_F(InputValidatorTestFixture, Sanitize_RemovesInvalidChars)
    {
        EXPECT_EQ(AiCompanion::InputValidator::SanitizeEntityName("my entity!"), "my_entity");
        EXPECT_EQ(AiCompanion::InputValidator::SanitizeEntityName("Player @1"), "Player_1");
    }

    TEST_F(InputValidatorTestFixture, Sanitize_HandlesEmpty)
    {
        EXPECT_EQ(AiCompanion::InputValidator::SanitizeEntityName(""), "");
    }

    TEST_F(InputValidatorTestFixture, Sanitize_SkipsLeadingNonLetter)
    {
        EXPECT_EQ(AiCompanion::InputValidator::SanitizeEntityName("123abc"), "abc");
    }
} // namespace UnitTest
