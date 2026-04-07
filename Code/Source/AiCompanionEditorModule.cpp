/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "AiCompanionEditorSystemComponent.h"

namespace AiCompanion
{
    class AiCompanionEditorModule : public AZ::Module
    {
    public:
        AZ_RTTI(AiCompanionEditorModule, "{C3E2A4F6-7D8B-4CAA-BE5F-9A3B2D4C6F8E}", AZ::Module);
        AZ_CLASS_ALLOCATOR(AiCompanionEditorModule, AZ::SystemAllocator);

        AiCompanionEditorModule()
        {
            m_descriptors.insert(m_descriptors.end(), {
                AiCompanionEditorSystemComponent::CreateDescriptor(),
            });
        }

        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{
                azrtti_typeid<AiCompanionEditorSystemComponent>(),
            };
        }
    };
} // namespace AiCompanion

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME, _Editor), AiCompanion::AiCompanionEditorModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_AiCompanion_Editor, AiCompanion::AiCompanionEditorModule)
#endif
