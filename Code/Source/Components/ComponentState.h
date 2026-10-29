/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */
#pragma once

#include <AzCore/base.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace AiCompanion::ComponentState
{
    //! What the editor knows about one component on an entity after it was
    //! added: whether it is active, pending (the editor could not activate
    //! it: an incompatible sibling or a missing required service) or disabled
    //! by the user, and why. Nothing here talks to the editor, so the unit
    //! tests cover every word and the reply shape;
    //! AiCompanionEditorSystemComponent::GetComponentState fills it from the
    //! composition buses.

    //! The three places the editor keeps a component, as the reply words them.
    inline constexpr const char* ActiveWord = "active";
    inline constexpr const char* PendingWord = "pending";
    inline constexpr const char* DisabledWord = "disabled";
    inline constexpr const char* UnknownWord = "unknown";

    struct Info
    {
        //! The component's friendly (Entity Inspector) name.
        AZStd::string m_name;
        //! One of ActiveWord, PendingWord, DisabledWord, UnknownWord.
        AZStd::string m_state;
        //! Friendly names of the active components this one conflicts with.
        AZStd::vector<AZStd::string> m_incompatibleWith;
        //! Friendly names of pending components that would provide a service
        //! this one requires, so activating them first would activate this one.
        AZStd::vector<AZStd::string> m_waitingOnPending;
        //! Required services no component on the entity provides, as the
        //! service CRCs in hex (the engine keeps no names for them).
        AZStd::vector<AZStd::string> m_missingRequiredServices;
        //! Services this component declares incompatible, as CRCs in hex.
        AZStd::vector<AZStd::string> m_incompatibleServices;
        //! The component descriptor's own warnings.
        AZStd::vector<AZStd::string> m_warnings;
    };

    //! The state word for a component found in the active list, the pending
    //! list or the disabled list. Active wins, then pending, then disabled;
    //! none of them reads as UnknownWord.
    const char* StateWord(bool active, bool pending, bool disabled);

    //! A service CRC as the reply prints it: "0x" and eight lowercase hex digits.
    AZStd::string ServiceCrcWord(AZ::u32 crc);

    //! One line an agent can show as is: "active", "disabled: incompatible
    //! with \"Box Shape\"", "disabled: missing required service 0x...",
    //! "disabled by the user" or "state unknown".
    AZStd::string Summarize(const Info& info);

    //! The reply object: {"name", "state", "summary", "incompatible_with",
    //! "waiting_on_pending", "missing_required_services",
    //! "incompatible_services", "warnings"}; the arrays are always present.
    AZStd::string BuildComponentStateJson(const Info& info);
} // namespace AiCompanion::ComponentState
