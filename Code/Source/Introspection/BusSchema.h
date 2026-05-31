/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/std/string/string.h>
#include <AzCore/std/string/string_view.h>

namespace AZ
{
    class BehaviorContext;
}

namespace AiCompanion
{
    //! Builds a JSON description of a reflected EBus from a BehaviorContext.
    //!
    //! This reads the live C++ reflection, so unlike the editor's generated
    //! .pyi stub (which lists EBus event arguments by type only) it includes
    //! each argument's NAME and TOOLTIP. It is gem-agnostic: it works for any
    //! reflected bus with no per-gem catalog.
    //!
    //! With a bus name, returns:
    //!   {"name": "<bus>", "events": [{"name", "call_type", "returns",
    //!    "args": [{"name", "type", "tooltip"}]}]}
    //! With an empty busName, lists every reflected bus: {"buses": ["<name>"]}
    //! On failure (no context, unknown bus): {"error": "<message>"}
    //!
    //! Event names and bus names are sorted so the output is stable.
    AZStd::string BuildBusSchemaJson(AZ::BehaviorContext* behaviorContext, AZStd::string_view busName);
} // namespace AiCompanion
