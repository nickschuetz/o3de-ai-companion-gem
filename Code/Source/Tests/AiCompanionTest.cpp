/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzTest/AzTest.h>

#include "AiCompanionSystemComponent.h"

namespace UnitTest
{
    class AiCompanionTestFixture : public LeakDetectionFixture
    {
    };

    TEST_F(AiCompanionTestFixture, SystemComponent_CreatesDescriptor)
    {
        auto* descriptor = AiCompanion::AiCompanionSystemComponent::CreateDescriptor();
        ASSERT_NE(descriptor, nullptr);
        delete descriptor;
    }

    TEST_F(AiCompanionTestFixture, SystemComponent_ProvidesCorrectServices)
    {
        AZ::ComponentDescriptor::DependencyArrayType provided;
        AiCompanion::AiCompanionSystemComponent::GetProvidedServices(provided);
        EXPECT_EQ(provided.size(), 1);
        EXPECT_EQ(provided[0], AZ_CRC("AiCompanionService"));
    }

    TEST_F(AiCompanionTestFixture, SystemComponent_IncompatibleWithSelf)
    {
        AZ::ComponentDescriptor::DependencyArrayType incompatible;
        AiCompanion::AiCompanionSystemComponent::GetIncompatibleServices(incompatible);
        EXPECT_EQ(incompatible.size(), 1);
        EXPECT_EQ(incompatible[0], AZ_CRC("AiCompanionService"));
    }
} // namespace UnitTest

// Generates the AzRunUnitTests entry point AzTestRunner loads. Exactly one test
// source file in the module defines it; without it the module exports no runner
// symbol and none of the gem's tests can execute.
AZ_UNIT_TEST_HOOK(DEFAULT_UNIT_TEST_ENV);
