/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/std/string/string.h>

namespace AiCompanion
{
    class InputValidator
    {
    public:
        //! Maximum allowed length for entity names.
        static constexpr size_t MaxEntityNameLength = 128;

        //! Maximum allowed absolute value for position coordinates.
        static constexpr float MaxPositionBound = 10000.0f;

        //! Validates an entity name: must start with a letter, contain only
        //! alphanumeric characters, underscores, or hyphens, and be within length limits.
        static bool IsValidEntityName(const AZStd::string& name);

        //! Validates a component type name against expected patterns.
        //! Component types may contain letters, digits, spaces, hyphens, underscores, and parentheses.
        static bool IsValidComponentType(const AZStd::string& componentType);

        //! Validates that a position coordinate is finite and within world bounds.
        static bool IsValidPosition(float x, float y, float z);

        //! Validates a script/asset path: must not contain path traversal sequences,
        //! null bytes, or other dangerous patterns.
        static bool IsValidAssetPath(const AZStd::string& path);

        //! Returns a sanitized version of the entity name, replacing invalid characters.
        //! Returns an empty string if the name cannot be salvaged.
        static AZStd::string SanitizeEntityName(const AZStd::string& name);
    };
} // namespace AiCompanion
