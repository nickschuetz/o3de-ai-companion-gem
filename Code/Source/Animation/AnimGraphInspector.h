/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>
#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/string/string.h>

namespace EMotionFX
{
    class AnimGraphNode;
    class AnimGraphStateMachine;
    class AnimGraphStateTransition;
    class ValueParameter;
} // namespace EMotionFX

namespace AiCompanion::AnimGraphInspector
{
    //! Read-only views of the EMotion FX anim graphs the engine currently
    //! holds. Both functions must run on the main thread: EMotion FX locks only
    //! the manager's lists, not a graph's contents, and the editor mutates
    //! graphs from the main thread. Graph pointers are resolved from the
    //! manager on every call and never kept.
    //!
    //! Each returns the JSON text on success, or a message on failure.
    //! "EMotion FX is not available" when the EMotion FX gem is not loaded.

    //! Lists every anim graph registered with the AnimGraphManager:
    //!   {"editor_mode": bool, "anim_graphs": [{"id", "file_name",
    //!    "owned_by_runtime", "owned_by_asset", "dirty", "num_nodes",
    //!    "num_parameters", "instances": [{"entity_id", "actor_instance_id",
    //!    "motion_set"}]}]}
    AZ::Outcome<AZStd::string, AZStd::string> ListAnimGraphs();

    //! Describes one anim graph: its nodes (with ports and incoming
    //! connections), state transitions (with conditions), value parameters
    //! and node groups. `selector` is the graph's decimal id, or a file name:
    //! an exact match of the graph's file name first, then a case-insensitive
    //! match of its tail. Fails with "anim graph not found: <selector>".
    AZ::Outcome<AZStd::string, AZStd::string> DescribeAnimGraph(const AZStd::string& selector);

    //! The per-object writers, shared with Animation/AnimGraphAuthoring so a
    //! write reply carries the same object a later get_anim_graph does.
    using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer>;

    //! One node object: id, name, type, palette_name, category, parent_id,
    //! can_act_as_state, has_output_pose, enabled, position, entry_state_id
    //! (a state machine's entry state, null for other nodes), motion_ids (a
    //! motion node's motion set entries, null for other nodes), input_ports
    //! with their incoming connection, output_ports.
    void WriteNode(JsonWriter& w, const EMotionFX::AnimGraphNode& node);

    //! One input port object of `node`: index, name, connection (the
    //! incoming connection's source_node_id and source_port, or null).
    void WriteInputPort(JsonWriter& w, const EMotionFX::AnimGraphNode& node, size_t portIndex);

    //! One transition object: id, state_machine_id, source_node_id (null for
    //! a wildcard), target_node_id, wildcard, blend_time, priority, disabled,
    //! conditions (type and the engine's summary text).
    void WriteTransition(
        JsonWriter& w, const EMotionFX::AnimGraphStateMachine& stateMachine, const EMotionFX::AnimGraphStateTransition& transition);

    //! One value parameter object: name, type, description, default, min,
    //! max (null for unranged types), group (`groupName` null at the root).
    void WriteValueParameter(JsonWriter& w, const EMotionFX::ValueParameter& parameter, const AZStd::string* groupName);
} // namespace AiCompanion::AnimGraphInspector
