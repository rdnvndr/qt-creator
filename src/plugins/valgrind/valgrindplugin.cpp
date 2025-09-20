// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "callgrindtool.h"
#include "memchecktool.h"
#include "valgrindsettings.h"
#include "valgrindtr.h"

#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/runconfiguration.h>

#ifdef WITH_TESTS
#   include "valgrindmemcheckparsertest.h"
#   include "valgrindtestrunnertest.h"
#endif

using namespace Core;
using namespace ProjectExplorer;

namespace Valgrind::Internal {

class ValgrindRunConfigurationAspect : public GlobalOrProjectAspect
{
public:
    ValgrindRunConfigurationAspect(BuildConfiguration *)
    {
        setProjectSettings(new ValgrindSettings(false));
        setGlobalSettings(&globalSettings());
        setId(ANALYZER_VALGRIND_SETTINGS);
        setDisplayName(Tr::tr("Valgrind Settings"));
        setUsingGlobalSettings(true);
        resetProjectToGlobalSettings();
        setConfigWidgetCreator([this] { return createRunConfigAspectWidget(this); });
    }
};

class ValgrindPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Valgrind.json")

public:
    void initialize() final
    {
        setupMemcheckTool(this);
        setupCallgrindTool(this);

        RunConfiguration::registerAspect<ValgrindRunConfigurationAspect>();
#ifdef WITH_TESTS
        addTestCreator(createValgrindMemcheckParserTest);
        addTestCreator(createValgrindTestRunnerTest);
#endif
    }
};

} // Valgrind::Internal

#include "valgrindplugin.moc"
