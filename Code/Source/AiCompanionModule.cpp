/*
 * Copyright Contributors to the AI Companion for O3DE Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "AiCompanionSystemComponent.h"

namespace AiCompanion
{
    class AiCompanionModule : public AZ::Module
    {
    public:
        AZ_RTTI(AiCompanionModule, "{A1C0E2D4-5B6F-4A8E-9C3D-7E1F0B2A4D6C}", AZ::Module);
        AZ_CLASS_ALLOCATOR(AiCompanionModule, AZ::SystemAllocator);

        AiCompanionModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                AiCompanionSystemComponent::CreateDescriptor(),
            });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{
                azrtti_typeid<AiCompanionSystemComponent>(),
            };
        }
    };
} // namespace AiCompanion

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), AiCompanion::AiCompanionModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_AiCompanion, AiCompanion::AiCompanionModule)
#endif
