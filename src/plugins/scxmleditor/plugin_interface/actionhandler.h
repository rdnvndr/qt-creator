// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mytypes.h"

#include <QObject>

QT_FORWARD_DECLARE_CLASS(QAction)

namespace ScxmlEditor::PluginInterface {

class ActionHandler : public QObject
{
    Q_OBJECT

public:
    explicit ActionHandler(QObject *parent = nullptr);

    QAction *action(ActionType type) const;

private:
    QList<QAction*> m_actions;
};

} // namespace ScxmlEditor::PluginInterface
