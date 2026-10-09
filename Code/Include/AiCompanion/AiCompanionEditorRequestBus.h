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
    //!
    //! Failure convention: the events the AgentServer serves (CreateEntity,
    //! SetTransform, DeleteEntity, ListAnimGraphs, GetAnimGraph,
    //! CreateAnimGraph, RemoveAnimGraph) return their AZ::Outcome failure as
    //! the JSON text {"code": "<code>", "message": "<text>"} written by
    //! RequestError::EncodeError, with the code from the RequestError
    //! vocabulary (validation_failed, not_found, unavailable, engine_error).
    //! The server decodes it into the reply's "code" and "error" fields; an
    //! editor Python caller that reads GetError() sees the JSON text.
    //! SetComponentPropertyUnwrapped and CommitEntityToPrefab are called only
    //! from the Python package and keep plain-text failures.
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
        //! On failure: {"error": "<message>", "code": "<RequestError code>"}
        //! (not_found for an unknown bus, unavailable without a BehaviorContext).
        virtual AZStd::string GetBusSchema(AZStd::string busName) = 0;

        //! Creates an entity named `name` at `position` (world), optionally
        //! under `parentId`, inside its own undo batch. The name must pass
        //! InputValidator::IsValidEntityName and the position its bound.
        //! Returns the new entity id, or an encoded error (validation_failed
        //! for the name, position or scale; not_found for the parent;
        //! unavailable without the prefab system; engine_error with the
        //! prefab system's own text).
        virtual AZ::Outcome<AZ::u64, AZStd::string> CreateEntity(AZStd::string name, AZ::Vector3 position, AZ::u64 parentId) = 0;

        //! Sets any of world position, world rotation (Euler degrees, XYZ) and
        //! uniform scale on an existing entity, inside its own undo batch. A
        //! flag false leaves that part untouched. Fails not_found for a
        //! missing entity and validation_failed for a bad position or scale.
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
        //! Refuses the level's root entity (validation_failed) and ids that
        //! do not exist (not_found).
        virtual AZ::Outcome<void, AZStd::string> DeleteEntity(AZ::u64 entityId) = 0;

        //! Lists every EMotion FX anim graph the engine currently holds, as
        //! JSON: {"editor_mode", "anim_graphs": [{"id", "file_name",
        //! "owned_by_runtime", "owned_by_asset", "dirty", "num_nodes",
        //! "num_parameters", "instances": [{"entity_id", "actor_instance_id",
        //! "motion_set"}]}]}. The 64-bit entity id is a decimal string or
        //! null; the anim graph id is a number. Read-only; must be called on
        //! the main thread.
        //! Fails unavailable, "EMotion FX is not available", when that gem is
        //! absent.
        virtual AZ::Outcome<AZStd::string, AZStd::string> ListAnimGraphs() = 0;

        //! Describes one anim graph as JSON: its nodes with ports and incoming
        //! connections, state transitions with conditions, value parameters
        //! and node groups (see Animation/AnimGraphInspector.h for the shape).
        //! `selector` is the graph's decimal id or its file name (exact, then
        //! a case-insensitive match of the file name's tail). Read-only; must
        //! be called on the main thread. Fails not_found, "anim graph not
        //! found: ...", or unavailable, "EMotion FX is not available".
        virtual AZ::Outcome<AZStd::string, AZStd::string> GetAnimGraph(AZStd::string selector) = 0;

        //! Creates a new, unsaved EMotion FX anim graph through EMotion Studio's
        //! command system (undoable in the Animation Editor). Success JSON:
        //! {"id": <u32>, "file_name": ""}. Main thread only. Fails unavailable
        //! when EMotion Studio is not loaded in this editor and engine_error
        //! with the command system's text when the command fails.
        virtual AZ::Outcome<AZStd::string, AZStd::string> CreateAnimGraph() = 0;

        //! Removes an anim graph by id through the command system. Success
        //! JSON: {"removed": <u32>}. Fails not_found, "anim graph not found:
        //! <id>", unavailable without EMotion Studio, or engine_error.
        virtual AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId) = 0;
    };

    using AiCompanionEditorRequestBus = AZ::EBus<AiCompanionEditorRequests>;
} // namespace AiCompanion
