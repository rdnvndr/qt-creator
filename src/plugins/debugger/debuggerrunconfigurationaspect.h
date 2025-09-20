// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "debugger_global.h"

#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/runconfigurationaspects.h>

namespace Debugger {

class DEBUGGER_EXPORT DebuggerRunConfigurationAspect
    : public ProjectExplorer::GlobalOrProjectAspect
{
public:
    DebuggerRunConfigurationAspect(ProjectExplorer::BuildConfiguration *bc);
    ~DebuggerRunConfigurationAspect();

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    bool useCppDebugger() const;
    bool useQmlDebugger() const;
    bool usePythonDebugger() const;
    void setUseQmlDebugger(bool value);
    bool useMultiProcess() const;
    void setUseMultiProcess(bool on);
    QString overrideStartup() const;

    struct Data : BaseAspect::Data
    {
        bool useCppDebugger;
        bool useQmlDebugger;
        bool usePythonDebugger;
        bool useMultiProcess;
        QString overrideStartup;
    };

private:
    Utils::TriStateAspect m_cppAspect;
    Utils::TriStateAspect m_qmlAspect;
    Utils::TriStateAspect m_pythonAspect;
    Utils::BoolAspect m_multiProcessAspect;
    Utils::StringAspect m_overrideStartupAspect;
    ProjectExplorer::BuildConfiguration * const m_buildConfiguration;
};

} // namespace Debugger
