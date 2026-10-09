/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/Math/Vector3.h>
#include <AzCore/base.h>
#include <rapidjson/document.h>

namespace AiCompanion::RequestParsing
{
    //! Reads an entity id from a request field. Accepts a JSON number or a
    //! decimal string, with or without the "[id]" brackets the Python side
    //! emits. Returns false (and leaves outId untouched) for anything else or
    //! for id 0.
    bool ParseEntityId(const rapidjson::Value& value, AZ::u64& outId);

    //! Reads a 3-element JSON array of numbers into a Vector3. Returns false
    //! for any other shape, for non-finite values, or for components outside
    //! the InputValidator position bound.
    bool ParseVector3(const rapidjson::Value& value, AZ::Vector3& out);
} // namespace AiCompanion::RequestParsing
