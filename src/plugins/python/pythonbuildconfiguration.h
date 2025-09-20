// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "pythonbuildsystem.h"

#include <projectexplorer/abstractprocessstep.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildstep.h>


namespace ProjectExplorer { class Interpreter; }
namespace Python::Internal {

class PipPackageInfo;
class PySideUicExtraCompiler;

class PySideBuildStep : public ProjectExplorer::AbstractProcessStep
{
    Q_OBJECT
public:
    PySideBuildStep(ProjectExplorer::BuildStepList *bsl, Utils::Id id);
    ~PySideBuildStep();

    void checkForPySide(const Utils::FilePath &python);

    QList<PySideUicExtraCompiler *> extraCompilers() const;

    static Utils::Id id();

private:
    void checkForPySide(const Utils::FilePath &python, const QString &pySidePackageName);
    void handlePySidePackageInfo(const PipPackageInfo &pySideInfo,
                                 const Utils::FilePath &python,
                                 const QString &requestedPackageName);

    Tasking::GroupItem runRecipe() final;
    void updateExtraCompilers();

    std::unique_ptr<QFutureWatcher<PipPackageInfo>> m_watcher;
    QMetaObject::Connection m_watcherConnection;

    Utils::FilePathAspect m_pysideProject{this};
    Utils::FilePathAspect m_pysideUic{this};
    QList<PySideUicExtraCompiler *> m_extraCompilers;
};

class PythonBuildConfiguration : public ProjectExplorer::BuildConfiguration
{
    Q_OBJECT
public:
    PythonBuildConfiguration(ProjectExplorer::Target *target, const Utils::Id &id);

    QWidget *createConfigWidget() override;
    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    Utils::FilePath python() const;
    std::optional<Utils::FilePath> venv() const;

private:
    void initialize(const ProjectExplorer::BuildInfo &info);
    void updateInterpreter(const std::optional<ProjectExplorer::Interpreter> &python);
    void updatePython(const Utils::FilePath &python);
    void updateDocuments();
    void handlePythonUpdated(const Utils::FilePath &python);

    Utils::FilePath m_python;
    std::optional<Utils::FilePath> m_venv;
};

void setupPySideBuildStep();
void setupPythonBuildConfiguration();

} // namespace Python::Internal
