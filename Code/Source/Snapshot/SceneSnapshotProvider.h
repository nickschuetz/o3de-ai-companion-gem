/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/optional.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion
{
    class SceneSnapshotProvider
    {
    public:
        //! What the native output reports about one entity.
        struct EntityInfo
        {
            AZ::EntityId id;
            AZStd::string name;
            AZ::EntityId parentId;
            AZ::Vector3 position = AZ::Vector3::CreateZero();
            AZ::Vector3 rotation = AZ::Vector3::CreateZero();
            //! The Transform component's uniform scale.
            float uniformScale = 1.0f;
            //! The Non-uniform Scale component's value when the entity has
            //! one (a NonUniformScaleRequestBus handler is connected), else
            //! nullopt. The bus answers (0, 0, 0) without a handler, so it is
            //! never read blind.
            AZStd::optional<AZ::Vector3> nonUniformScale;
            AZStd::vector<AZStd::string> componentNames;
        };

        //! The scale the entity actually renders with: the uniform scale times
        //! the Non-uniform Scale component's value, or the uniform scale on
        //! every axis when the entity has no such component.
        static AZ::Vector3 EffectiveScale(float uniformScale, const AZStd::optional<AZ::Vector3>& nonUniformScale);

        //! Writes one entity as the JSON object get_entity, get_scene_snapshot
        //! and the set_transform reply emit: {"id", "name", "parent_id",
        //! "position", "rotation", "scale" (the uniform scale on every axis),
        //! "non_uniform_scale" (the component's value or null),
        //! "effective_scale", "components"}. Ids are decimal strings.
        static AZStd::string EntityToJson(const EntityInfo& info);

        //! Captures a full snapshot of the current scene as JSON.
        //! Traverses all entities, their transforms, and component lists.
        //! Returns a JSON string with entity_count, entities array, and metadata.
        static AZStd::string CaptureSnapshot();

        //! Captures the entity hierarchy tree as JSON.
        //! Returns a nested JSON structure reflecting parent-child relationships.
        static AZStd::string CaptureEntityTree();

        //! Captures one entity (transform, parent, component list) as JSON.
        //! Returns {"error": "..."} when no entity with that id is active.
        static AZStd::string CaptureEntity(AZ::EntityId entityId);

        //! Validates the current scene for common issues.
        //! Checks for: unnamed entities, entities at origin, missing components, etc.
        //! Returns a JSON validation report.
        static AZStd::string ValidateScene();
    };
} // namespace AiCompanion
