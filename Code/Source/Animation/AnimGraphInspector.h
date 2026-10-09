/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/Outcome/Outcome.h>
#include <AzCore/std/string/string.h>

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
} // namespace AiCompanion::AnimGraphInspector
