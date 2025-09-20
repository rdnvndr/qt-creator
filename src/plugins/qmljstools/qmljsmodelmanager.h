// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qmljstools_global.h"

#include <qmljs/qmljsmodelmanagerinterface.h>

#include <QFuture>

namespace ProjectExplorer { class Project; }

namespace QmlJSTools::Internal {

class QMLJSTOOLS_EXPORT ModelManager: public QmlJS::ModelManagerInterface
{
    Q_OBJECT

public:
    ModelManager();
    ~ModelManager() override;

    void delayedInitialization();
protected:
    QHash<QString, QmlJS::Dialect> languageForSuffix() const override;
    void writeMessageInternal(const QString &msg) const override;
    WorkingCopy workingCopyInternal() const override;
    void addTaskInternal(const QFuture<void> &result, const QString &msg,
                         const Utils::Id taskId) const override;
    ProjectInfo defaultProjectInfoForProject(
        ProjectBase *project, const Utils::FilePaths &hiddenRccFolders) const override;
private:
    void updateDefaultProjectInfo();
    void loadDefaultQmlTypeDescriptions();
    QHash<QString, QmlJS::Dialect> initLanguageForSuffix() const;
};

QMLJSTOOLS_EXPORT ProjectExplorer::Project *
    projectFromProjectInfo(const ModelManager::ProjectInfo &projectInfo);

} // namespace QmlJSTools::Internal
