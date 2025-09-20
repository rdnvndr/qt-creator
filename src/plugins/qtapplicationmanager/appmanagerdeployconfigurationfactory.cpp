// Copyright (C) 2019 Luxoft Sweden AB
// Copyright (C) 2018 Pelagicore AG
// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "appmanagerdeployconfigurationfactory.h"

#include "appmanagerconstants.h"
#include "appmanagertr.h"

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/target.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <boot2qt/qdbconstants.h>
#include <remotelinux/remotelinux_constants.h>
#include <cmakeprojectmanager/cmakeprojectconstants.h>

using namespace ProjectExplorer;

namespace AppManager::Internal {

static bool isNecessaryToDeploy(const BuildConfiguration *bc)
{
    auto device = RunDeviceKitAspect::device(bc->kit());
    return device && device->type() != ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE;
}

class AppManagerDeployConfigurationFactory final : public DeployConfigurationFactory
{
public:
    AppManagerDeployConfigurationFactory()
    {
        setConfigBaseId(Constants::DEPLOYCONFIGURATION_ID);
        setDefaultDisplayName(Tr::tr("Automatic Application Manager Deploy Configuration"));
        addSupportedTargetDeviceType(ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE);
        addSupportedTargetDeviceType(RemoteLinux::Constants::GenericLinuxOsType);
        addSupportedTargetDeviceType(Qdb::Constants::QdbLinuxOsType);
        setSupportedProjectType(CMakeProjectManager::Constants::CMAKE_PROJECT_ID);

        addInitialStep(Constants::CMAKE_PACKAGE_STEP_ID);
        addInitialStep(Constants::DEPLOY_PACKAGE_STEP_ID, isNecessaryToDeploy);
        addInitialStep(Constants::INSTALL_PACKAGE_STEP_ID);
    }
};

void setupAppManagerDeployConfiguration()
{
    static AppManagerDeployConfigurationFactory theAppManagerDeployConfigurationFactory;
}

} // AppManager::Internal

