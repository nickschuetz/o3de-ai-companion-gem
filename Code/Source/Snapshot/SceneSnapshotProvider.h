/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Component/EntityId.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion
{
    class SceneSnapshotProvider
    {
    public:
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
