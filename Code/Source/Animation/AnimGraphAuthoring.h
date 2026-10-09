/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/Outcome/Outcome.h>
#include <AzCore/base.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion::AnimGraphAuthoring
{
    //! Writes to EMotion FX anim graphs through EMotion Studio's command
    //! system. Every function runs on the main thread (the AgentServer's
    //! dispatch does that), re-resolves its graph and nodes from the
    //! AnimGraphManager by id on each call, validates its input before any
    //! command is sent, and answers JSON text on success or a
    //! RequestError::EncodeError text on failure: validation_failed for a
    //! refused argument, not_found for a missing graph, node, parameter or
    //! file, unavailable when EMotion Studio is not loaded, engine_error with
    //! the command system's own result text.
    //!
    //! Undo: each request is one command, or one command group that rolls
    //! back on failure, so it is one step in the Animation Editor's own undo
    //! history (Undo in the Animation Editor pane). That history is separate
    //! from the editor's main Undo and from the gem's rollback functions;
    //! SaveAnimGraph is not undoable at all.
    //!
    //! Ownership: a graph owned by an asset or a runtime instance (one an
    //! Anim Graph component loaded) is refused for every write, since the
    //! command system edits would not reach the asset; the editable graphs
    //! are the ones CreateAnimGraph and LoadAnimGraph make.
    //!
    //! Note: CommandSystem::GetCommandManager() is a static local to the
    //! module that constructed it and is null here; EMStudio::GetManager()
    //! goes through AZ::Interface and is the accessor that works across
    //! modules. Commands always go as strings so their bodies run in the
    //! EMotionFX module, where that accessor is valid.

    //! Runs one command line ("CreateAnimGraph", "AnimGraphCreateNode ...")
    //! with the command manager's error handling off, so a failure never
    //! opens the error-report window. Answers the command's result text.
    AZ::Outcome<AZStd::string, AZStd::string> RunCommand(const AZStd::string& commandLine);

    //! Creates a new, unsaved anim graph. {"id": <u32>, "file_name": ""}.
    AZ::Outcome<AZStd::string, AZStd::string> CreateAnimGraph();

    //! Removes an editable anim graph by id. {"removed": <u32>}.
    AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId);

    //! Loads an .animgraph file as an editable graph. `fileName` is absolute,
    //! an @alias@ path, or relative to the project root; it must exist
    //! (not_found) and lie inside the project or engine root
    //! (validation_failed). A graph the command system already loaded from
    //! the same file is answered instead of loaded twice.
    //! {"id": <u32>, "file_name": "<as stored on the graph>"}.
    AZ::Outcome<AZStd::string, AZStd::string> LoadAnimGraph(const AZStd::string& fileName);

    //! Saves a graph to `fileName` (resolved like LoadAnimGraph; empty means
    //! the graph's own file name, refused when it has none) which must lie
    //! inside the project root; the parent directory is created. Not
    //! undoable. {"id": <u32>, "file_name": "<as stored>"}.
    AZ::Outcome<AZStd::string, AZStd::string> SaveAnimGraph(AZ::u32 animGraphId, const AZStd::string& fileName);

    //! Adds a node. `argumentsJson` is an object with "node_type" (an
    //! AnimGraphNode class name or palette name, case-insensitive), optional
    //! "parent_id" (node id; default the root state machine), optional
    //! "name" (else the engine generates "<Type>N") and optional "position"
    //! [x, y]. Answers the node object exactly as get_anim_graph emits it.
    AZ::Outcome<AZStd::string, AZStd::string> AddNode(AZ::u32 animGraphId, const AZStd::string& argumentsJson);

    //! Removes a node by id; the root state machine is refused.
    //! {"removed": "<node id>"}.
    AZ::Outcome<AZStd::string, AZStd::string> RemoveNode(AZ::u32 animGraphId, const AZStd::string& nodeId);

    //! Makes a node its state machine's entry state. {"entry_state_id": "<id>"}.
    AZ::Outcome<AZStd::string, AZStd::string> SetEntryState(AZ::u32 animGraphId, const AZStd::string& nodeId);

    //! Adds a value parameter. `argumentsJson` is an object with "name",
    //! "parameter_type" (Float, FloatSlider, FloatSpinner, Int, IntSlider,
    //! IntSpinner, Bool, Tag, String, Vector2, Vector3, Vector3Gizmo,
    //! Vector4, Color, Rotation, or the class name), optional "default",
    //! "min", "max" (typed per kind; min and max only for ranged types),
    //! "description" and "group" (created when missing, in the same undo
    //! step). Answers the parameter object as get_anim_graph emits it.
    AZ::Outcome<AZStd::string, AZStd::string> AddParameter(AZ::u32 animGraphId, const AZStd::string& argumentsJson);

    //! Removes a value parameter by name; a group is refused.
    //! {"removed": "<name>"}.
    AZ::Outcome<AZStd::string, AZStd::string> RemoveParameter(AZ::u32 animGraphId, const AZStd::string& name);

    //! Adds a state transition. `argumentsJson` is an object with
    //! "target_node_id" (a state inside a state machine), optional
    //! "source_node_id" (absent or null: a wildcard transition; else a state
    //! of the same state machine, never an exit node), the optional
    //! transition fields of SetTransition ("blend_time", "priority",
    //! "disabled", "sync_mode", "interpolation"), and optional "conditions":
    //! a list of {"condition_type": <ParameterCondition, TimeCondition,
    //! PlayTimeCondition, MotionCondition, StateCondition, TagCondition,
    //! Vector2Condition, or the engine class name>, "attributes": {<reflected
    //! field>: <value>}} (see AnimGraphCommandText's condition tables). The
    //! create, adjust and add-condition commands run as one group that is
    //! undone on failure. Answers the transition object exactly as
    //! get_anim_graph emits it.
    AZ::Outcome<AZStd::string, AZStd::string> AddTransition(AZ::u32 animGraphId, const AZStd::string& argumentsJson);

    //! Removes a transition by id. {"removed": "<transition id>"}.
    AZ::Outcome<AZStd::string, AZStd::string> RemoveTransition(AZ::u32 animGraphId, const AZStd::string& transitionId);

    //! Adjusts a transition. `argumentsJson` is an object with
    //! "transition_id" and at least one of "blend_time" (seconds, >= 0),
    //! "priority" (u32), "disabled" (bool), "sync_mode" (0 disabled, 1 track
    //! based, 2 clip based) and "interpolation" (0 linear, 1 ease curve).
    //! Answers the transition object as get_anim_graph emits it.
    AZ::Outcome<AZStd::string, AZStd::string> SetTransition(AZ::u32 animGraphId, const AZStd::string& argumentsJson);

    //! Connects an output port to an input port inside a blend tree.
    //! `argumentsJson` is an object with "source_node_id", "source_port",
    //! "target_node_id" and "target_port"; a port is an index or a name
    //! (exact, then case-insensitive). Both nodes must share a parent that
    //! is not a state machine, the ports must carry compatible data, the
    //! input port must be free, and the connection must not close a cycle.
    //! Answers the target's input port object as get_anim_graph emits it.
    AZ::Outcome<AZStd::string, AZStd::string> ConnectPorts(AZ::u32 animGraphId, const AZStd::string& argumentsJson);

    //! Removes the connection into an input port. `argumentsJson` is an
    //! object with "target_node_id" and "target_port" (index or name).
    //! {"removed": "<connection id>"}.
    AZ::Outcome<AZStd::string, AZStd::string> DisconnectPorts(AZ::u32 animGraphId, const AZStd::string& argumentsJson);

    //! Adjusts a node. `argumentsJson` is an object with "node_id" and at
    //! least one of "name" (unique graph-wide), "position" [x, y], "enabled"
    //! (bool) and "attributes": {<reflected field>: <value>}, each field a
    //! serialize field of the node's class (bases included, AnimGraphNode's
    //! own and the structural fields excluded) taking a number, bool or
    //! string, or a list of strings for a string list and for a motion
    //! node's "motionIds". Answers the node object as get_anim_graph emits it.
    AZ::Outcome<AZStd::string, AZStd::string> SetNode(AZ::u32 animGraphId, const AZStd::string& argumentsJson);
} // namespace AiCompanion::AnimGraphAuthoring
