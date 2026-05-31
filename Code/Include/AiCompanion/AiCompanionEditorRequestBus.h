/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/any.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion
{
    //! Editor-side requests that need engine-internal types not exposed cleanly
    //! through EditorComponentAPIBus.
    class AiCompanionEditorRequests : public AZ::EBusTraits
    {
    public:
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;
        static const AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Sets a property on a component, unwrapping GenericComponentWrapper so
        //! PropertyTreeEditor receives an internally consistent (instance, type)
        //! pair.
        //!
        //! Works around o3de/o3de#19770: when a component does not inherit from
        //! EditorComponentBase (e.g. InputConfigurationComponent), the editor
        //! wraps it in a GenericComponentWrapper. The stock
        //! EditorComponentAPIBus.SetComponentProperty path hands
        //! PropertyTreeEditor a (wrapper-instance, wrapped-typeId) pair that
        //! disagrees with itself, which crashes the editor on Asset<T> writes.
        //! This entry point performs the same unwrap that o3de/o3de#19771 adds
        //! upstream, before constructing PropertyTreeEditor.
        //!
        //! Lifecycle: remove this bus and route callers back through
        //! EditorComponentAPIBus.SetComponentProperty once #19771 lands in the
        //! engine build the gem ships against.
        virtual AZ::Outcome<void, AZStd::string> SetComponentPropertyUnwrapped(
            AZ::EntityComponentIdPair pair, AZStd::string propertyPath, AZStd::any value) = 0;

        //! Returns the reflected schema of an EBus as a JSON string, read live
        //! from the BehaviorContext. Unlike the editor's generated .pyi stub
        //! (which lists EBus event arguments by type only), this includes each
        //! argument's NAME and TOOLTIP, so an agent gets a fully documented API.
        //!
        //! Gem-agnostic: works for any reflected bus, no per-gem catalog. Pass
        //! an empty busName to list every reflected bus name instead.
        //!
        //! Shape with a bus name:
        //!   {"name": "<bus>", "events": [{"name", "call_type", "returns",
        //!    "args": [{"name", "type", "tooltip"}]}]}
        //! Shape with empty busName: {"buses": ["<name>", ...]}
        //! On failure: {"error": "<message>"}
        virtual AZStd::string GetBusSchema(AZStd::string busName) = 0;
    };

    using AiCompanionEditorRequestBus = AZ::EBus<AiCompanionEditorRequests>;
} // namespace AiCompanion
