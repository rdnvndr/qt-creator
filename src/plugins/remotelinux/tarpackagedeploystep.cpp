// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tarpackagedeploystep.h"

#include "abstractremotelinuxdeploystep.h"
#include "remotelinux_constants.h"
#include "remotelinuxtr.h"

#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/devicesupport/filetransfer.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/qtcprocess.h>
#include <utils/processinterface.h>

using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace RemoteLinux::Internal {

// TarPackageDeployStep

class TarPackageDeployStep : public AbstractRemoteLinuxDeployStep
{
public:
    TarPackageDeployStep(BuildStepList *bsl, Id id)
        : AbstractRemoteLinuxDeployStep(bsl, id)
    {
        setWidgetExpandedByDefault(false);

        setInternalInitializer([this]() -> Result<> {
            const BuildStep *tarCreationStep = nullptr;

            for (BuildStep *step : deployConfiguration()->stepList()->steps()) {
                if (step == this)
                    break;
                if (step->id() == Constants::TarPackageCreationStepId) {
                    tarCreationStep = step;
                    break;
                }
            }
            if (!tarCreationStep)
                return make_unexpected(Tr::tr("No tarball creation step found."));

            m_packageFilePath =
                FilePath::fromVariant(tarCreationStep->data(Constants::TarPackageFilePathId));
            return isDeploymentPossible();
        });
    }

private:
    QString remoteFilePath() const;
    GroupItem deployRecipe() final;
    GroupItem uploadTask();
    GroupItem installTask();

    FilePath m_packageFilePath;
};

QString TarPackageDeployStep::remoteFilePath() const
{
    return QLatin1String("/tmp/") + m_packageFilePath.fileName();
}

GroupItem TarPackageDeployStep::uploadTask()
{
    const auto onSetup = [this](FileTransfer &transfer) {
        const FilesToTransfer files {{m_packageFilePath,
                        deviceConfiguration()->filePath(remoteFilePath())}};
        transfer.setFilesToTransfer(files);
        connect(&transfer, &FileTransfer::progress, this, &TarPackageDeployStep::addProgressMessage);
        addProgressMessage(Tr::tr("Uploading package to device..."));
    };
    const auto onDone = [this](const FileTransfer &transfer, DoneWith result) {
        if (result == DoneWith::Success)
            addProgressMessage(Tr::tr("Successfully uploaded package file."));
        else
            addErrorMessage(transfer.resultData().m_errorString);
    };
    return FileTransferTask(onSetup, onDone);
}

GroupItem TarPackageDeployStep::installTask()
{
    const auto onSetup = [this](Process &process) {
        const QString cmdLine = QLatin1String("cd / && tar xvf ") + remoteFilePath()
                + " && (rm " + remoteFilePath() + " || :)";
        process.setCommand({deviceConfiguration()->filePath("/bin/sh"), {"-c", cmdLine}});
        Process *proc = &process;
        connect(proc, &Process::readyReadStandardOutput, this, [this, proc] {
            handleStdOutData(proc->readAllStandardOutput());
        });
        connect(proc, &Process::readyReadStandardError, this, [this, proc] {
            handleStdErrData(proc->readAllStandardError());
        });
        addProgressMessage(Tr::tr("Installing package to device..."));
    };
    const auto onDone = [this](const Process &process, DoneWith result) {
        if (result == DoneWith::Success) {
            saveDeploymentTimeStamp(DeployableFile(m_packageFilePath, {}), {});
            addProgressMessage(Tr::tr("Successfully installed package file."));
            return;
        }
        addErrorMessage(Tr::tr("Installing package failed.") + process.errorString());
    };
    return ProcessTask(onSetup, onDone);
}

GroupItem TarPackageDeployStep::deployRecipe()
{
    const auto onSetup = [this] {
        if (hasLocalFileChanged(DeployableFile(m_packageFilePath, {})))
            return SetupResult::Continue;
        addSkipDeploymentMessage();
        return SetupResult::StopWithSuccess;
    };
    return Group { onGroupSetup(onSetup), uploadTask(), installTask() };
}


// TarPackageDeployStepFactory

class TarPackageDeployStepFactory final : public BuildStepFactory
{
public:
    TarPackageDeployStepFactory()
    {
        registerStep<TarPackageDeployStep>(Constants::TarPackageDeployStepId);
        setDisplayName(Tr::tr("Deploy tarball via SFTP upload"));
        setSupportedConfiguration(RemoteLinux::Constants::DeployToGenericLinux);
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_DEPLOY);
    }
};

void setupTarPackageDeployStep()
{
    static TarPackageDeployStepFactory theTarPackageDeployStepFactory;
}

} // RemoteLinux::Internal
