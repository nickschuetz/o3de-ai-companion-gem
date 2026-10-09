/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 */

#include "AgentModeFilter.h"

#include <AzCore/Debug/Trace.h>

#include <QEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QString>
#include <QWidget>

namespace AiCompanion::AgentMode
{
    Filter::Filter(QObject* parent)
        : QObject(parent)
    {
    }

    Filter::~Filter() = default;

    bool Filter::eventFilter(QObject* watched, QEvent* event)
    {
        if (event->type() != QEvent::Show)
        {
            return QObject::eventFilter(watched, event);
        }

        QWidget* widget = qobject_cast<QWidget*>(watched);
        if (!widget)
        {
            return QObject::eventFilter(watched, event);
        }

        const QString title = widget->windowTitle();
        if (title.isEmpty())
        {
            return QObject::eventFilter(watched, event);
        }

        if (title.startsWith(QStringLiteral("Welcome to O3DE")))
        {
            AZ_Printf("AiCompanion", "[AgentMode] Auto-dismissing '%s' dialog\n", title.toUtf8().constData());
            // Defer close to the next event-loop iteration so Qt finishes
            // its show machinery before the widget tears itself down.
            QMetaObject::invokeMethod(widget, "close", Qt::QueuedConnection);
            return false;
        }

        if (title.startsWith(QStringLiteral("Unsaved files detected")) || title.startsWith(QStringLiteral("Error Log")) ||
            title.startsWith(QStringLiteral("Startup Errors")))
        {
            AZ_Printf("AiCompanion", "[AgentMode] Observed modal '%s' (auto-dismiss not configured)\n", title.toUtf8().constData());
            return QObject::eventFilter(watched, event);
        }

        // Any other message box would block the editor's main thread inside
        // QDialog::exec() until a human clicks it, which on an unattended editor
        // means every AgentServer request times out from then on (seen with the
        // "Entity Creation Error" box PrefabIntegrationManager raises). Log what
        // it said, so the reason reaches the editor log, and reject it.
        if (auto* box = qobject_cast<QMessageBox*>(widget))
        {
            AZ_Printf(
                "AiCompanion", "[AgentMode] Rejecting modal '%s': %s\n", title.toUtf8().constData(), box->text().toUtf8().constData());
            QMetaObject::invokeMethod(widget, "reject", Qt::QueuedConnection);
            return false;
        }

        return QObject::eventFilter(watched, event);
    }
} // namespace AiCompanion::AgentMode
