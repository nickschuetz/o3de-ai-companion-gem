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

        //! Largest scale factor a set_transform call may apply, per axis.
        static constexpr float MaxScale = 1000.0f;

        //! Three scale elements within this of each other count as one uniform scale.
        static constexpr float UniformScaleTolerance = 1e-6f;

        //! Validates an entity name: must start with a letter, contain only
        //! alphanumeric characters, underscores, or hyphens, and be within length limits.
        static bool IsValidEntityName(const AZStd::string& name);

        //! Validates a component type name against expected patterns.
        //! Component types may contain letters, digits, spaces, hyphens, underscores, and parentheses.
        static bool IsValidComponentType(const AZStd::string& componentType);

        //! Validates that a position coordinate is finite and within world bounds.
        static bool IsValidPosition(float x, float y, float z);

        //! Validates a scale: every element finite and in (0, MaxScale]. A uniform
        //! scale is checked by passing the same value three times.
        static bool IsValidScale(float x, float y, float z);

        //! Whether the three elements are equal within UniformScaleTolerance, so
        //! the scale is the Transform's uniform scale and needs no Non-uniform
        //! Scale component.
        static bool IsUniformScale(float x, float y, float z);

        //! Validates a script/asset path: must not contain path traversal sequences,
        //! null bytes, or other dangerous patterns.
        static bool IsValidAssetPath(const AZStd::string& path);

        //! Returns a sanitized version of the entity name, replacing invalid characters.
        //! Returns an empty string if the name cannot be salvaged.
        static AZStd::string SanitizeEntityName(const AZStd::string& name);

        //! Whether an entity name belongs to a protected system entity that no agent
        //! request may modify or delete: "EditorGlobal", "SystemEntity", or any name
        //! starting with "AZ::". Mirrors is_protected_entity in the Python package.
        static bool IsProtectedEntityName(const AZStd::string& name);
    };
} // namespace AiCompanion
