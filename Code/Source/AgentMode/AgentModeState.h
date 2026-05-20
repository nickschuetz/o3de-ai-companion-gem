/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <AzCore/IO/Path/Path.h>
#include <AzCore/base.h>

namespace AiCompanion::AgentMode
{
    //! Mirror of the JSON sidecar at
    //!   $XDG_STATE_HOME/o3de-ai-companion/agent_mode.json
    //! (default ~/.local/state/o3de-ai-companion/agent_mode.json)
    //!
    //! Python and C++ both read and write this file. Keep field names and
    //! semantics in sync with Editor/Scripts/ai_companion/agent_mode.py.
    struct State
    {
        bool enabled = false;
        bool suppressDialogs = false;
        AZ::s64 updatedAt = 0;
    };

    //! Resolve the canonical sidecar path. Honors $XDG_STATE_HOME and falls
    //! back to ~/.local/state. Same logic as the Python helper.
    AZ::IO::FixedMaxPath GetStatePath();

    //! Path the C++ component writes to mirror what it has actually observed
    //! and applied. Distinct from GetStatePath() (which is the input written
    //! by Python). Lets external tooling and tests verify the C++ side has
    //! caught up after a state change.
    AZ::IO::FixedMaxPath GetObservedStatePath();

    //! Try to read the sidecar. Returns true if the file existed and parsed
    //! successfully; outState is populated. Returns false on any failure;
    //! outState is left at its constructed defaults (all false).
    bool LoadState(State& outState);

    //! Persist what the component currently has applied to the observed-state
    //! file. ``filterInstalled`` reflects whether a QApplication event filter
    //! is actually live, since suppressDialogs alone does not guarantee the
    //! filter was installed (e.g. when qApp was null).
    void WriteObservedState(const State& state, bool filterInstalled);
} // namespace AiCompanion::AgentMode
