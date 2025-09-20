// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionmanager_test.h"

#include "extensionsmodel.h"

#include <utils/fileutils.h>

#include <QTest>

namespace ExtensionManager::Internal {

class ExtensionsModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void testRepositoryJsonParser();
};

void ExtensionsModelTest::testRepositoryJsonParser()
{
    ExtensionsModel model;
    model.setRepositoryPaths({testData("defaultdata")});
}

QObject *createExtensionsModelTest()
{
    return new ExtensionsModelTest;
}

Utils::FilePath testData(const QString &id)
{
    return Utils::FilePath::fromUserInput(":/extensionmanager/testdata/" + id);
}

} // ExtensionManager::Internal

#include "extensionmanager_test.moc"
