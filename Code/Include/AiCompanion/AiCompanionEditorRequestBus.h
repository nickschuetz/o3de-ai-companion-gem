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
    //! CreateAnimGraph, RemoveAnimGraph, LoadAnimGraph, SaveAnimGraph,
    //! AddAnimGraphNode, RemoveAnimGraphNode, SetAnimGraphEntryState,
    //! AddAnimGraphParameter, RemoveAnimGraphParameter, AddAnimGraphTransition,
    //! RemoveAnimGraphTransition, SetAnimGraphTransition, ConnectAnimGraphPorts,
    //! DisconnectAnimGraphPorts, SetAnimGraphNode) return their failure as
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
        //! <id>", validation_failed for a graph an asset or runtime instance
        //! owns, unavailable without EMotion Studio, or engine_error.
        virtual AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId) = 0;

        //! The anim graph authoring events below share one contract (see
        //! Animation/AnimGraphAuthoring.h): main thread only; each is one
        //! command or one rolled-back command group, so one step in the
        //! Animation Editor's own undo history (not the editor's main Undo);
        //! a graph owned by an asset or runtime instance is refused with
        //! validation_failed; failures are the encoded {"code", "message"}
        //! text. Ids of nodes are decimal strings; the graph id is a u32.

        //! Loads an .animgraph file as an editable graph. `fileName` is
        //! absolute, an @alias@ path, or relative to the project root; it must
        //! exist (not_found) and lie inside the project or engine root
        //! (validation_failed). Success JSON: {"id": <u32>, "file_name": ".."}.
        //! A graph the command system already loaded from that file is
        //! answered instead of loaded again.
        virtual AZ::Outcome<AZStd::string, AZStd::string> LoadAnimGraph(AZStd::string fileName) = 0;

        //! Saves a graph to `fileName` (empty: the graph's own file name,
        //! refused when it has none), which must lie inside the project root;
        //! the parent directory is created. Not undoable. Success JSON:
        //! {"id": <u32>, "file_name": "<as stored on the graph>"}.
        virtual AZ::Outcome<AZStd::string, AZStd::string> SaveAnimGraph(AZ::u32 animGraphId, AZStd::string fileName) = 0;

        //! Adds a node. `argumentsJson` is a JSON object: "node_type" (an
        //! AnimGraphNode class name or palette name, case-insensitive),
        //! optional "parent_id" (default: the root state machine), "name"
        //! (default: engine-generated) and "position" [x, y]. The type is
        //! checked against the creatable node classes and the Animation
        //! Editor's placement rules. Success JSON: the node object exactly as
        //! GetAnimGraph emits it.
        virtual AZ::Outcome<AZStd::string, AZStd::string> AddAnimGraphNode(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;

        //! Removes a node by id; the root state machine is refused. Success
        //! JSON: {"removed": "<node id>"}.
        virtual AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraphNode(AZ::u32 animGraphId, AZStd::string nodeId) = 0;

        //! Makes a node its state machine's entry state. Success JSON:
        //! {"entry_state_id": "<node id>"}.
        virtual AZ::Outcome<AZStd::string, AZStd::string> SetAnimGraphEntryState(AZ::u32 animGraphId, AZStd::string nodeId) = 0;

        //! Adds a value parameter. `argumentsJson` is a JSON object: "name",
        //! "parameter_type" (Float, FloatSlider, FloatSpinner, Int,
        //! IntSlider, IntSpinner, Bool, Tag, String, Vector2, Vector3,
        //! Vector3Gizmo, Vector4, Color, Rotation, or the class name),
        //! optional "default", "min", "max" (typed per kind; min and max for
        //! ranged types only), "description" and "group" (created when
        //! missing, in the same undo step). Success JSON: the parameter object
        //! as GetAnimGraph emits it.
        virtual AZ::Outcome<AZStd::string, AZStd::string> AddAnimGraphParameter(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;

        //! Removes a value parameter by name; a group is refused. Success
        //! JSON: {"removed": "<name>"}.
        virtual AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraphParameter(AZ::u32 animGraphId, AZStd::string name) = 0;

        //! Adds a state transition. `argumentsJson` is a JSON object:
        //! "target_node_id" (a state), optional "source_node_id" (absent or
        //! null: a wildcard transition; else a state of the same state
        //! machine), optional "blend_time", "priority", "disabled",
        //! "sync_mode", "interpolation", and optional "conditions": a list of
        //! {"condition_type": <ParameterCondition, TimeCondition,
        //! PlayTimeCondition, MotionCondition, StateCondition, TagCondition,
        //! Vector2Condition, or the class name>, "attributes": {<reflected
        //! field>: <value>}}. One command group, undone on failure. Success
        //! JSON: the transition object exactly as GetAnimGraph emits it.
        virtual AZ::Outcome<AZStd::string, AZStd::string> AddAnimGraphTransition(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;

        //! Removes a transition by id. Success JSON: {"removed": "<id>"}.
        virtual AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraphTransition(AZ::u32 animGraphId, AZStd::string transitionId) = 0;

        //! Adjusts a transition. `argumentsJson` is a JSON object:
        //! "transition_id" and at least one of "blend_time" (seconds),
        //! "priority", "disabled" (bool), "sync_mode" (0 disabled, 1 track
        //! based, 2 clip based), "interpolation" (0 linear, 1 ease curve).
        //! Success JSON: the transition object as GetAnimGraph emits it.
        virtual AZ::Outcome<AZStd::string, AZStd::string> SetAnimGraphTransition(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;

        //! Connects an output port to an input port inside a blend tree.
        //! `argumentsJson` is a JSON object: "source_node_id", "source_port",
        //! "target_node_id", "target_port" (a port is an index or a name).
        //! Success JSON: the target's input port object as GetAnimGraph
        //! emits it (index, name, connection).
        virtual AZ::Outcome<AZStd::string, AZStd::string> ConnectAnimGraphPorts(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;

        //! Removes the connection into an input port. `argumentsJson` is a
        //! JSON object: "target_node_id", "target_port". Success JSON:
        //! {"removed": "<connection id>"}.
        virtual AZ::Outcome<AZStd::string, AZStd::string> DisconnectAnimGraphPorts(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;

        //! Adjusts a node. `argumentsJson` is a JSON object: "node_id" and at
        //! least one of "name", "position" [x, y], "enabled" (bool) and
        //! "attributes": {<reflected field>: <value>} (a number, bool or
        //! string; a list of strings for a string list and for a motion
        //! node's "motionIds"). Success JSON: the node object as
        //! GetAnimGraph emits it.
        virtual AZ::Outcome<AZStd::string, AZStd::string> SetAnimGraphNode(AZ::u32 animGraphId, AZStd::string argumentsJson) = 0;
    };

    using AiCompanionEditorRequestBus = AZ::EBus<AiCompanionEditorRequests>;
} // namespace AiCompanion
