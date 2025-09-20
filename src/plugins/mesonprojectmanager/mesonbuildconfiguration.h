// Copyright (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/buildconfiguration.h>

namespace MesonProjectManager::Internal {

enum class MesonBuildType { plain, debug, debugoptimized, release, minsize, custom };

class MesonBuildConfiguration final : public ProjectExplorer::BuildConfiguration
{
    Q_OBJECT
public:
    MesonBuildConfiguration(ProjectExplorer::Target *target, Utils::Id id);

    void build(const QString &target);

    QStringList mesonConfigArgs();

    const QString &parameters() const;
    void setParameters(const QString &params);

signals:
    void parametersChanged();

private:
    void toMap(Utils::Store &map) const override;
    void fromMap(const Utils::Store &map) override;

    MesonBuildType m_buildType;
    QWidget *createConfigWidget() final;
    QString m_parameters;
};

void setupMesonBuildConfiguration();

} // MesonProjectManager::Internal
