// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "killappstep.h"

#include "abstractremotelinuxdeploystep.h"
#include "remotelinux_constants.h"
#include "remotelinuxtr.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/target.h>

#include <utils/processinterface.h>
#include <utils/qtcassert.h>

using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace RemoteLinux::Internal {

class KillAppStep : public AbstractRemoteLinuxDeployStep
{
public:
    KillAppStep(BuildStepList *bsl, Id id) : AbstractRemoteLinuxDeployStep(bsl, id)
    {
        setWidgetExpandedByDefault(false);

        setInternalInitializer([this]() -> Result<> {
            BuildConfiguration * const bc = buildConfiguration();
            QTC_ASSERT(bc, return make_unexpected(QString()));
            RunConfiguration * const rc = bc->activeRunConfiguration();
            m_remoteExecutable =  rc ? rc->runnable().command.executable() : FilePath();
            return {};
        });
    }

private:
    GroupItem deployRecipe() final;

    FilePath m_remoteExecutable;
};

GroupItem KillAppStep::deployRecipe()
{
    const auto onSetup = [this](DeviceProcessKiller &killer) {
        if (m_remoteExecutable.isEmpty()) {
            addSkipDeploymentMessage();
            return SetupResult::StopWithSuccess;
        }
        killer.setProcessPath(m_remoteExecutable);
        addProgressMessage(Tr::tr("Trying to kill \"%1\" on remote device...")
                                  .arg(m_remoteExecutable.path()));
        return SetupResult::Continue;
    };
    const auto onDone = [this](DoneWith result) {
        const QString message = result == DoneWith::Success ? Tr::tr("Remote application killed.")
            : Tr::tr("Failed to kill remote application. Assuming it was not running.");
        addProgressMessage(message);
        return DoneResult::Success;
    };
    return DeviceProcessKillerTask(onSetup, onDone);
}

class KillAppStepFactory final : public BuildStepFactory
{
public:
    KillAppStepFactory()
    {
        registerStep<KillAppStep>(Constants::KillAppStepId);
        setDisplayName(Tr::tr("Kill current application instance"));
        setSupportedConfiguration(RemoteLinux::Constants::DeployToGenericLinux);
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_DEPLOY);
    }
};

void setupKillAppStep()
{
    static KillAppStepFactory theKillAppStepFactory;
}

} // RemoteLinux::Internal
