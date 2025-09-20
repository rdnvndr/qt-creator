// Copyright (C) 2016 BlackBerry Limited. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qnxanalyzesupport.h"
#include "qnxconstants.h"
#include "qnxdebugsupport.h"
#include "qnxdevice.h"
#include "qnxqtversion.h"
#include "qnxrunconfiguration.h"
#include "qnxsettingspage.h"
#include "qnxtoolchain.h"
#include "qnxtr.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildstep.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchainkitaspect.h>

#include <remotelinux/remotelinux_constants.h>

#include <QAction>

using namespace Core;
using namespace ProjectExplorer;

namespace Qnx::Internal {

class QnxDeployStepFactory : public BuildStepFactory
{
public:
    QnxDeployStepFactory(Utils::Id existingStepId, Utils::Id overrideId = {})
    {
        cloneStepCreator(existingStepId, overrideId);
        setSupportedConfiguration(Constants::QNX_QNX_DEPLOYCONFIGURATION_ID);
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_DEPLOY);
    }
};

class QnxDeployConfigurationFactory : public DeployConfigurationFactory
{
public:
    QnxDeployConfigurationFactory()
    {
        setConfigBaseId(Constants::QNX_QNX_DEPLOYCONFIGURATION_ID);
        setDefaultDisplayName(Tr::tr("Deploy to QNX Device"));
        addSupportedTargetDeviceType(Constants::QNX_QNX_OS_TYPE);
        setUseDeploymentDataView();

        addInitialStep(RemoteLinux::Constants::MakeInstallStepId, [](BuildConfiguration *bc) {
            const Project * const prj = bc->project();
            return prj->deploymentKnowledge() == DeploymentKnowledge::Bad
                    && prj->hasMakeInstallEquivalent();
        });
        addInitialStep(ProjectExplorer::Constants::DEVICE_CHECK_STEP);
        addInitialStep(Constants::QNX_DIRECT_UPLOAD_STEP_ID);
    }
};

void setupQnxDeployment()
{
    static QnxDeployConfigurationFactory deployConfigFactory;
    static QnxDeployStepFactory directUploadDeployFactory{RemoteLinux::Constants::DirectUploadStepId,
                                                   Constants::QNX_DIRECT_UPLOAD_STEP_ID};
    static QnxDeployStepFactory makeInstallStepFactory{RemoteLinux::Constants::MakeInstallStepId};
}

class QnxPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Qnx.json")

    void initialize() final
    {
        setupQnxDevice();
        setupQnxToolchain();
        setupQnxQtVersion();
        setupQnxDeployment();
        setupQnxRunnning();
        setupQnxDebugging();
        setupQnxQmlProfiler();
        setupQnxSettingsPage(this);
    }

    void extensionsInitialized() final
    {
        const Utils::Id QNX_DEBUGGING_GROUP = "Debugger.Group.Qnx";

        QAction *debugSeparator = nullptr;
        QAction *attachToQnxApplication = nullptr;

        ActionContainer *mstart = Core::ActionManager::actionContainer(ProjectExplorer::Constants::M_DEBUG_STARTDEBUGGING);
        mstart->appendGroup(QNX_DEBUGGING_GROUP);
        mstart->addSeparator(Core::Context(Core::Constants::C_GLOBAL), QNX_DEBUGGING_GROUP,
                             &debugSeparator);

        // Attach support
        ActionBuilder(this, "Debugger.AttachToQnxApplication")
            .setText(Tr::tr("Attach to remote QNX application..."))
            .addToContainer(ProjectExplorer::Constants::M_DEBUG_STARTDEBUGGING, QNX_DEBUGGING_GROUP)
            .bindContextAction(&attachToQnxApplication)
            .addOnTriggered(this, &showAttachToProcessDialog);

        connect(KitManager::instance(), &KitManager::kitsChanged, this,
                [attachToQnxApplication, debugSeparator] {
            auto isQnxKit = [](const Kit *kit) {
                return RunDeviceTypeKitAspect::deviceTypeId(kit) == Constants::QNX_QNX_OS_TYPE
                       && RunDeviceKitAspect::device(kit) && kit->isValid();
            };

            const bool hasValidQnxKit = KitManager::kit(isQnxKit) != nullptr;

            attachToQnxApplication->setVisible(hasValidQnxKit);
            debugSeparator->setVisible(hasValidQnxKit);
        });
    }
};

} // Qnx::Internal

#include "qnxplugin.moc"
