/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#include "RequestParsing.h"

#include "Validation/InputValidator.h"

#include <AzCore/std/string/string.h>

#include <cmath>
#include <cstdlib>

namespace AiCompanion::RequestParsing
{
    bool ParseEntityId(const rapidjson::Value& value, AZ::u64& outId)
    {
        if (value.IsUint64())
        {
            if (value.GetUint64() == 0)
            {
                return false;
            }
            outId = value.GetUint64();
            return true;
        }
        if (!value.IsString())
        {
            return false;
        }
        AZStd::string text = value.GetString();
        while (!text.empty() && (text.front() == '[' || text.front() == ' '))
        {
            text.erase(0, 1);
        }
        while (!text.empty() && (text.back() == ']' || text.back() == ' '))
        {
            text.pop_back();
        }
        if (text.empty())
        {
            return false;
        }
        char* end = nullptr;
        const unsigned long long parsed = strtoull(text.c_str(), &end, 10);
        if (end == text.c_str() || (end && *end != '\0') || parsed == 0)
        {
            return false;
        }
        outId = static_cast<AZ::u64>(parsed);
        return true;
    }

    bool ParseAnimGraphId(const rapidjson::Value& value, AZ::u32& outId)
    {
        if (value.IsUint())
        {
            outId = value.GetUint();
            return true;
        }
        if (value.IsNumber())
        {
            // Negative, fractional, or above the u32 range.
            return false;
        }
        if (!value.IsString())
        {
            return false;
        }
        const AZStd::string text = value.GetString();
        if (text.empty() || text.find_first_not_of("0123456789") != AZStd::string::npos)
        {
            return false;
        }
        char* end = nullptr;
        const unsigned long long parsed = strtoull(text.c_str(), &end, 10);
        if (end == text.c_str() || (end && *end != '\0') || parsed > 0xFFFFFFFFull)
        {
            return false;
        }
        outId = static_cast<AZ::u32>(parsed);
        return true;
    }

    bool ParseVector3(const rapidjson::Value& value, AZ::Vector3& out)
    {
        if (!value.IsArray() || value.Size() != 3)
        {
            return false;
        }
        float components[3];
        for (rapidjson::SizeType i = 0; i < 3; ++i)
        {
            if (!value[i].IsNumber())
            {
                return false;
            }
            components[i] = static_cast<float>(value[i].GetDouble());
            if (!std::isfinite(components[i]))
            {
                return false;
            }
        }
        if (!InputValidator::IsValidPosition(components[0], components[1], components[2]))
        {
            return false;
        }
        out = AZ::Vector3(components[0], components[1], components[2]);
        return true;
    }
} // namespace AiCompanion::RequestParsing
