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
    //! Runs one EMotion FX command line ("CreateAnimGraph", "AnimGraphCreateNode -animGraphID 1 ...")
    //! through EMotion Studio's command manager, so every change lands in the
    //! Animation Editor's undo history. Must be called on the main thread.
    //! Fails when EMotion Studio is not loaded in this editor or the command
    //! reports failure; the failure text is the engine's own result string.
    //! Note: CommandSystem::GetCommandManager() is a static local to the module
    //! that constructed it and is null here; EMStudio::GetManager() goes through
    //! AZ::Interface and is the accessor that works across modules.
    AZ::Outcome<AZStd::string, AZStd::string> RunCommand(const AZStd::string& commandLine);

    //! Creates a new, unsaved anim graph. Success JSON: {"id": <u32>, "file_name": ""}.
    AZ::Outcome<AZStd::string, AZStd::string> CreateAnimGraph();

    //! Removes an anim graph by id. Success JSON: {"removed": <u32>}.
    AZ::Outcome<AZStd::string, AZStd::string> RemoveAnimGraph(AZ::u32 animGraphId);
} // namespace AiCompanion::AnimGraphAuthoring
