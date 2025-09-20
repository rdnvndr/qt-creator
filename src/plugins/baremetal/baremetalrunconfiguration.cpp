// Copyright (C) 2016 Tim Sander <tim@krieglstein.org>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "baremetalrunconfiguration.h"

#include "baremetalconstants.h"
#include "baremetaltr.h"

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/buildtargetinfo.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>

using namespace ProjectExplorer;
using namespace Utils;

namespace BareMetal::Internal {

// RunConfigurations

class BareMetalRunConfiguration final : public RunConfiguration
{
public:
    explicit BareMetalRunConfiguration(BuildConfiguration *bc, Id id)
        : RunConfiguration(bc, id)
    {
        executable.setDeviceSelector(kit(), ExecutableAspect::RunDevice);
        executable.setPlaceHolderText(Tr::tr("Unknown"));

        setUpdater([this] {
            const BuildTargetInfo bti = buildTargetInfo();
            executable.setExecutable(bti.targetFilePath);
        });
    }

    ExecutableAspect executable{this};
    ArgumentsAspect arguments{this};
    WorkingDirectoryAspect workingDir{this};
};

class BareMetalCustomRunConfiguration final : public RunConfiguration
{
public:
    explicit BareMetalCustomRunConfiguration(BuildConfiguration *bc, Id id)
        : RunConfiguration(bc, id)
    {
        executable.setDeviceSelector(kit(), ExecutableAspect::RunDevice);
        executable.setSettingsKey("BareMetal.CustomRunConfig.Executable");
        executable.setPlaceHolderText(Tr::tr("Unknown"));
        executable.setReadOnly(false);
        executable.setHistoryCompleter("BareMetal.CustomRunConfig.History");
        executable.setExpectedKind(PathChooser::Any);

        setDefaultDisplayName(RunConfigurationFactory::decoratedTargetName(
            Tr::tr("Custom Executable"), kit()));
        setUsesEmptyBuildKeys();
    }

public:
    Tasks checkForIssues() const final
    {
        Tasks tasks;
        if (executable.executable().isEmpty()) {
            tasks << createConfigurationIssue(Tr::tr("The remote executable must be set in order to "
                                                     "run a custom remote run configuration."));
        }
        return tasks;
    }

    ExecutableAspect executable{this};
    ArgumentsAspect arguments{this};
    WorkingDirectoryAspect workingDir{this};
};


// BareMetalDeployConfigurationFactory

class BareMetalDeployConfigurationFactory : public DeployConfigurationFactory
{
public:
    BareMetalDeployConfigurationFactory()
    {
        setConfigBaseId("BareMetal.DeployConfiguration");
        setDefaultDisplayName(Tr::tr("Deploy to BareMetal Device"));
        addSupportedTargetDeviceType(Constants::BareMetalOsType);
    }
};

// BareMetalRunConfigurationFactory

class BareMetalRunConfigurationFactory final : public RunConfigurationFactory
{
public:
    BareMetalRunConfigurationFactory()
    {
        registerRunConfiguration<BareMetalRunConfiguration>(Constants::BAREMETAL_RUNCONFIG_ID);
        setDecorateDisplayNames(true);
        addSupportedTargetDeviceType(BareMetal::Constants::BareMetalOsType);
    }
};

// BaseMetalCustomRunConfigurationFactory

class BareMetalCustomRunConfigurationFactory final : public FixedRunConfigurationFactory
{
public:
    BareMetalCustomRunConfigurationFactory()
        : FixedRunConfigurationFactory(Tr::tr("Custom Executable"), true)
    {
        registerRunConfiguration<BareMetalCustomRunConfiguration>(Constants::BAREMETAL_CUSTOMRUNCONFIG_ID);
        addSupportedTargetDeviceType(BareMetal::Constants::BareMetalOsType);
    }
};


void setupBareMetalDeployAndRunConfigurations()
{
   static BareMetalDeployConfigurationFactory theBareMetalDeployConfigurationFactory;
   static BareMetalRunConfigurationFactory theBareMetalRunConfigurationFactory;
   static BareMetalCustomRunConfigurationFactory theBareMetalCustomRunConfigurationFactory;
}

} // BareMetal::Internal
