// Copyright (C) 2019 Luxoft Sweden AB
// Copyright (C) 2018 Pelagicore AG
// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "appmanagerdeploypackagestep.h"

#include "appmanagerconstants.h"
#include "appmanagerstringaspect.h"
#include "appmanagertargetinformation.h"
#include "appmanagertr.h"

#include <projectexplorer/buildstep.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/target.h>

#include <remotelinux/remotelinux_constants.h>

#include <utils/filestreamer.h>

using namespace ProjectExplorer;
using namespace RemoteLinux;
using namespace Tasking;
using namespace Utils;

namespace AppManager::Internal {

#define SETTINGSPREFIX "ApplicationManagerPlugin.Deploy.DeployPackageStep."

class AppManagerDeployPackageStep : public BuildStep
{
public:
    AppManagerDeployPackageStep(BuildStepList *bsl, Id id)
        : BuildStep(bsl, id)
    {
        setDisplayName(Tr::tr("Deploy Application Manager package"));

        packageFilePath.setSettingsKey(SETTINGSPREFIX "FilePath");
        packageFilePath.setHistoryCompleter(SETTINGSPREFIX "FilePath.History");
        packageFilePath.setExpectedKind(PathChooser::File);
        packageFilePath.setLabelText(Tr::tr("Package file path:"));

        targetDirectory.setSettingsKey(SETTINGSPREFIX "TargetDirectory");
        targetDirectory.setHistoryCompleter(SETTINGSPREFIX "TargetDirectory.History");
        targetDirectory.setExpectedKind(PathChooser::Directory);
        targetDirectory.setLabelText(Tr::tr("Target directory:"));
        targetDirectory.setButtonsVisible(false);

        const auto updateAspects = [this] {
            const TargetInformation targetInformation(target());

            packageFilePath.setPlaceHolderPath(targetInformation.packageFile.absoluteFilePath());
            targetDirectory.setPlaceHolderPath(targetInformation.runDirectory.absolutePath());

            setEnabled(!targetInformation.isBuiltin);
        };

        connect(target(), &Target::activeRunConfigurationChanged, this, updateAspects);
        connect(target(), &Target::activeDeployConfigurationChanged, this, updateAspects);
        connect(target(), &Target::parsingFinished, this, updateAspects);
        connect(target(), &Target::runConfigurationsUpdated, this, updateAspects);
        connect(project(), &Project::displayNameChanged, this, updateAspects);

        updateAspects();
    }

private:
    bool init() final
    {
        return TargetInformation(target()).isValid();
    }

    GroupItem runRecipe() final
    {
        const auto onSetup = [this](FileStreamer &streamer) {
            const TargetInformation targetInformation(target());
            const FilePath source = packageFilePath.valueOrDefault(
                targetInformation.packageFile.absoluteFilePath());
            const FilePath targetDir = targetDirectory.valueOrDefault(
                targetInformation.runDirectory.absolutePath());
            const FilePath target = targetInformation.device->filePath(targetDir.path())
                                        .pathAppended(source.fileName());
            streamer.setSource(source);
            streamer.setDestination(target);
            emit addOutput("Starting uploading", OutputFormat::NormalMessage);
        };
        const auto onDone = [this](DoneWith result) {
            if (result == DoneWith::Success)
                emit addOutput(Tr::tr("Uploading finished"), OutputFormat::NormalMessage);
            else
                emit addOutput(Tr::tr("Uploading failed"), OutputFormat::ErrorMessage);
        };
        return FileStreamerTask(onSetup, onDone);
    }

    AppManagerFilePathAspect packageFilePath{this};
    AppManagerFilePathAspect targetDirectory{this};
};

// Factory

class AppManagerDeployPackageStepFactory final : public BuildStepFactory
{
public:
    AppManagerDeployPackageStepFactory()
    {
        registerStep<AppManagerDeployPackageStep>(Constants::DEPLOY_PACKAGE_STEP_ID);
        setDisplayName(Tr::tr("Deploy Application Manager package"));
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_DEPLOY);
    }
};

void setupAppManagerDeployPackageStep()
{
   static AppManagerDeployPackageStepFactory theAppManagerDeployPackageStepFactory;
}

} // namespace AppManager::Internal