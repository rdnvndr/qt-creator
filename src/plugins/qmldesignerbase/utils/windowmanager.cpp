// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "windowmanager.h"

#include <coreplugin/icore.h>

#include <QCursor>
#include <QGuiApplication>
#include <QMainWindow>
#include <QQmlEngine>
#include <QScreen>
#include <QWindow>

namespace QmlDesigner {

WindowManager::WindowManager()
{
    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, &WindowManager::focusWindowChanged);
    connect(
        Core::ICore::instance(), &Core::ICore::coreAboutToClose, this, &WindowManager::aboutToQuit);

    if (!connectMainWindowHandle())
        Core::ICore::instance()->mainWindow()->installEventFilter(this);
}

bool WindowManager::connectMainWindowHandle()
{
    if (QWindow *windowHandle = Core::ICore::instance()->mainWindow()->windowHandle()) {
        QMetaObject::Connection success = connect(
            windowHandle,
            &QWindow::visibleChanged,
            this,
            &WindowManager::mainWindowVisibleChanged,
            Qt::UniqueConnection);
        return success;
    }
    return false;
}

bool WindowManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == Core::ICore::instance()->mainWindow() && event->type() == QEvent::WinIdChange) {
        connectMainWindowHandle();
        Core::ICore::instance()->mainWindow()->removeEventFilter(this);
    }
    return QObject::eventFilter(watched, event);
}

void WindowManager::registerDeclarativeType()
{
    [[maybe_unused]] static const int typeIndex
        = qmlRegisterSingletonType<WindowManager>("StudioWindowManager",
                                                  1,
                                                  0,
                                                  "WindowManager",
                                                  [](QQmlEngine *, QJSEngine *) {
                                                      return new WindowManager();
                                                  });
}

WindowManager::~WindowManager() {}

QPoint WindowManager::globalCursorPosition()
{
    return QCursor::pos();
}

QRect WindowManager::getScreenGeometry(QPoint point)
{
    QScreen *screen = QGuiApplication::screenAt(point);

    if (!screen)
        return {};

    return screen->geometry();
}

} // namespace QmlDesigner
