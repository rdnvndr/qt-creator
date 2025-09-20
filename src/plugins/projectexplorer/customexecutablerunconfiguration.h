// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "environmentaspect.h"
#include "runconfigurationaspects.h"
#include "runcontrol.h"

namespace ProjectExplorer {

class PROJECTEXPLORER_EXPORT CustomExecutableRunConfiguration : public RunConfiguration
{
    Q_OBJECT

public:
    CustomExecutableRunConfiguration(BuildConfiguration *bc, Utils::Id id);
    explicit CustomExecutableRunConfiguration(BuildConfiguration *bc);

    QString defaultDisplayName() const;

private:
    bool isEnabled(Utils::Id) const override;
    Tasks checkForIssues() const override;

    void configurationDialogFinished();

    EnvironmentAspect environment{this};
    ExecutableAspect executable{this};
    ArgumentsAspect arguments{this};
    WorkingDirectoryAspect workingDir{this};
    TerminalAspect terminal{this};
    RunAsRootAspect runAsRoot{this};
};

class CustomExecutableRunConfigurationFactory : public FixedRunConfigurationFactory
{
public:
    CustomExecutableRunConfigurationFactory();
};

} // namespace ProjectExplorer
