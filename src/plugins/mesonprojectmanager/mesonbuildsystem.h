// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mesonprojectparser.h"
#include "kitdata.h"

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/target.h>

#include <utils/filesystemwatcher.h>

namespace ProjectExplorer { class ProjectUpdater; }

namespace MesonProjectManager::Internal {

class MesonBuildSystem final : public ProjectExplorer::BuildSystem
{
    Q_OBJECT

public:
    MesonBuildSystem(ProjectExplorer::BuildConfiguration *bc);
    ~MesonBuildSystem() final;

    void triggerParsing() final;

    inline const BuildOptionsList &buildOptions() const { return m_parser.buildOptions(); }
    inline const TargetsList &targets() const { return m_parser.targets(); }

    bool configure();
    bool setup();
    bool wipe();

    const QStringList &targetList() const { return m_parser.targetsNames(); }

    void setMesonConfigArgs(const QStringList &args) { m_pendingConfigArgs = args; }

private:
    bool parseProject();
    void updateKit(ProjectExplorer::Kit *kit);
    bool needsSetup();
    void parsingCompleted(bool success);
    QStringList configArgs(bool isSetup);
    void buildDirectoryChanged();

    ProjectExplorer::BuildSystem::ParseGuard m_parseGuard;
    MesonProjectParser m_parser;
    std::unique_ptr<ProjectExplorer::ProjectUpdater> m_cppCodeModelUpdater;
    QStringList m_pendingConfigArgs;
    Utils::FileSystemWatcher m_IntroWatcher;
    KitData m_kitData;
};

void setupMesonBuildSystem();

} // MesonProjectManager::Internal
