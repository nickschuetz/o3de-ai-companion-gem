/*
 * Copyright Contributors to the AI Companion for O3DE Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "InputValidator.h"

#include <AzCore/std/string/regex.h>
#include <cmath>

namespace AiCompanion
{
    bool InputValidator::IsValidEntityName(const AZStd::string& name)
    {
        if (name.empty() || name.size() > MaxEntityNameLength)
        {
            return false;
        }

        // Must start with a letter, then allow alphanumeric, underscore, hyphen
        for (size_t i = 0; i < name.size(); ++i)
        {
            const char c = name[i];
            if (i == 0)
            {
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                {
                    return false;
                }
            }
            else
            {
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-'))
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool InputValidator::IsValidComponentType(const AZStd::string& componentType)
    {
        if (componentType.empty() || componentType.size() > 256)
        {
            return false;
        }

        // Component types may contain letters, digits, spaces, hyphens, underscores, parentheses
        for (const char c : componentType)
        {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                  c == '_' || c == '(' || c == ')'))
            {
                return false;
            }
        }

        // Must start with a letter
        const char first = componentType[0];
        if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z')))
        {
            return false;
        }

        return true;
    }

    bool InputValidator::IsValidPosition(float x, float y, float z)
    {
        // Check for NaN and infinity
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        {
            return false;
        }

        // Check bounds
        if (std::fabs(x) > MaxPositionBound ||
            std::fabs(y) > MaxPositionBound ||
            std::fabs(z) > MaxPositionBound)
        {
            return false;
        }

        return true;
    }

    bool InputValidator::IsValidAssetPath(const AZStd::string& path)
    {
        if (path.empty() || path.size() > 1024)
        {
            return false;
        }

        // Reject null bytes
        if (path.find('\0') != AZStd::string::npos)
        {
            return false;
        }

        // Reject path traversal
        if (path.find("..") != AZStd::string::npos)
        {
            return false;
        }

        // Reject absolute paths (must be relative to project/gem)
        if (path.size() > 0 && (path[0] == '/' || path[0] == '\\'))
        {
            return false;
        }

        // Reject Windows absolute paths (e.g., C:\)
        if (path.size() >= 2 && path[1] == ':')
        {
            return false;
        }

        return true;
    }

    AZStd::string InputValidator::SanitizeEntityName(const AZStd::string& name)
    {
        if (name.empty())
        {
            return {};
        }

        AZStd::string result;
        result.reserve(AZStd::min(name.size(), MaxEntityNameLength));

        for (size_t i = 0; i < name.size() && result.size() < MaxEntityNameLength; ++i)
        {
            const char c = name[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            {
                result.push_back(c);
            }
            else if ((c >= '0' && c <= '9') || c == '_' || c == '-')
            {
                if (!result.empty()) // Don't start with non-letter
                {
                    result.push_back(c);
                }
            }
            else if (c == ' ')
            {
                if (!result.empty())
                {
                    result.push_back('_');
                }
            }
            // Skip other characters
        }

        return result;
    }
} // namespace AiCompanion
