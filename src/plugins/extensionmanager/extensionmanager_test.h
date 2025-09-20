// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QObject>

namespace ExtensionManager::Internal {

QObject *createExtensionsModelTest();

Utils::FilePath testData(const QString &id);

} // namespace ExtensionManager::Internal
