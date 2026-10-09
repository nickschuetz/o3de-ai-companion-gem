/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/EBus/EBus.h>
#include <AzCore/Math/Vector3.h>
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

        //! Creates an entity named `name` at `position` (world), optionally
        //! under `parentId`, inside its own undo batch. The name must pass
        //! InputValidator::IsValidEntityName and the position its bound.
        //! Returns the new entity id, or an error message.
        virtual AZ::Outcome<AZ::u64, AZStd::string> CreateEntity(AZStd::string name, AZ::Vector3 position, AZ::u64 parentId) = 0;

        //! Sets any of world position, world rotation (Euler degrees, XYZ) and
        //! uniform scale on an existing entity, inside its own undo batch. A
        //! flag false leaves that part untouched.
        virtual AZ::Outcome<void, AZStd::string> SetTransform(
            AZ::u64 entityId,
            bool setPosition,
            AZ::Vector3 position,
            bool setRotation,
            AZ::Vector3 rotationDegrees,
            bool setScale,
            float uniformScale) = 0;

        //! Records an entity's current state (name, transform, components) in the
        //! level's prefab template now, instead of when the root undo batch ends.
        //! Creating an entity through the prefab system propagates the template
        //! and re-instantiates entities from it, wiping live changes that are
        //! not in the DOM yet; the editor only captures dirty entities when the
        //! outermost undo batch closes. Call this after configuring each entity
        //! in a call that creates several, so none of them come out bare.
        virtual AZ::Outcome<void, AZStd::string> CommitEntityToPrefab(AZ::EntityId entityId) = 0;

        //! Deletes an entity and its descendants inside its own undo batch.
        //! Refuses the level's root entity and ids that do not exist.
        virtual AZ::Outcome<void, AZStd::string> DeleteEntity(AZ::u64 entityId) = 0;

        //! Lists every EMotion FX anim graph the engine currently holds, as
        //! JSON: {"editor_mode", "anim_graphs": [{"id", "file_name",
        //! "owned_by_runtime", "owned_by_asset", "dirty", "num_nodes",
        //! "num_parameters", "instances": [{"entity_id", "actor_instance_id",
        //! "motion_set"}]}]}. Read-only; must be called on the main thread.
        //! Fails with "EMotion FX is not available" when that gem is absent.
        virtual AZ::Outcome<AZStd::string, AZStd::string> ListAnimGraphs() = 0;

        //! Describes one anim graph as JSON: its nodes with ports and incoming
        //! connections, state transitions with conditions, value parameters
        //! and node groups (see Animation/AnimGraphInspector.h for the shape).
        //! `selector` is the graph's decimal id or its file name (exact, then
        //! a case-insensitive match of the file name's tail). Read-only; must
        //! be called on the main thread. Fails with "anim graph not found: ..."
        //! or "EMotion FX is not available".
        virtual AZ::Outcome<AZStd::string, AZStd::string> GetAnimGraph(AZStd::string selector) = 0;
    };

    using AiCompanionEditorRequestBus = AZ::EBus<AiCompanionEditorRequests>;
} // namespace AiCompanion
