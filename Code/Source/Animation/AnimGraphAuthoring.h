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
} // namespace AiCompanion::AnimGraphAuthoring
