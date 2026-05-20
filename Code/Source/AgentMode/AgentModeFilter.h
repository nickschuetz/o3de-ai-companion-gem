/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#pragma once

#include <QObject>

namespace AiCompanion::AgentMode
{
    //! QObject event filter that intercepts known editor modals and dismisses
    //! the ones safe to dismiss without user input. Designed to be installed
    //! on qApp while agent mode is active.
    //!
    //! Current policy:
    //!   - "Welcome to O3DE" dialogs are auto-closed.
    //!   - "Unsaved files detected", "Error Log", and "Startup Errors" are
    //!     logged but not auto-dismissed. Their auto-dismiss policy is
    //!     deferred until we have real data on when they fire.
    //!
    //! The class deliberately omits Q_OBJECT so it does not require moc. We
    //! do not define any signals or slots of our own; QObject's base
    //! eventFilter() override is all we need.
    class Filter : public QObject
    {
    public:
        explicit Filter(QObject* parent = nullptr);
        ~Filter() override;

    protected:
        bool eventFilter(QObject* watched, QEvent* event) override;
    };
} // namespace AiCompanion::AgentMode
