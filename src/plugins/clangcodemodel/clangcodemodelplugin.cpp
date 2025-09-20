// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangcodemodeltr.h"
#include "clangconstants.h"
#include "clangmodelmanagersupport.h"
#include "clangutils.h"

#ifdef WITH_TESTS
#  include "test/activationsequenceprocessortest.h"
#  include "test/clangdtests.h"
#  include "test/clangfixittest.h"
#endif

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <cppeditor/clangdiagnosticconfig.h>
#include <cppeditor/cppeditorconstants.h>
#include <cppeditor/cppmodelmanager.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/textmark.h>

#include <utils/action.h>
#include <utils/async.h>
#include <utils/environment.h>
#include <utils/qtcassert.h>
#include <utils/temporarydirectory.h>

#include <QFutureWatcher>

using namespace Core;
using namespace CppEditor;
using namespace ProjectExplorer;
using namespace Utils;

namespace ClangCodeModel::Internal {

class ClangCodeModelPlugin final: public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "ClangCodeModel.json")

public:
    ~ClangCodeModelPlugin() final;
    void initialize() final;

private:
    void generateCompilationDB();
    void createCompilationDBAction();

    Utils::Action *m_generateCompilationDBAction = nullptr;
    QFutureWatcher<GenerateCompilationDbResult> m_generatorWatcher;
};

ClangCodeModelPlugin::~ClangCodeModelPlugin()
{
    m_generatorWatcher.cancel();
    m_generatorWatcher.waitForFinished();
}

void ClangCodeModelPlugin::initialize()
{
    TaskHub::addCategory({Constants::TASK_CATEGORY_DIAGNOSTICS,
                          Tr::tr("Clang Code Model"),
                          Tr::tr("C++ code issues that Clangd found in the current document.")});
    CppModelManager::activateClangCodeModel(std::make_unique<ClangModelManagerSupport>());
    createCompilationDBAction();

    ActionBuilder updateStaleIndexEntries(this, "ClangCodeModel.UpdateStaleIndexEntries");
    updateStaleIndexEntries.setText(Tr::tr("Update Potentially Stale Clangd Index Entries"));
    updateStaleIndexEntries.addOnTriggered(this, &ClangModelManagerSupport::updateStaleIndexEntries);
    updateStaleIndexEntries.addToContainer(CppEditor::Constants::M_TOOLS_CPP);
    updateStaleIndexEntries.addToContainer(CppEditor::Constants::M_CONTEXT);

#ifdef WITH_TESTS
    addTestCreator(createActivationSequenceProcessorTest);
    addTestCreator(createClangdTestCompletion);
    addTestCreator(createClangdTestExternalChanges);
    addTestCreator(createClangdTestFindReferences);
    addTestCreator(createClangdTestFollowSymbol);
    addTestCreator(createClangdTestHighlighting);
    addTestCreator(createClangdTestIndirectChanges);
    addTestCreator(createClangdTestLocalReferences);
    addTestCreator(createClangdTestTooltips);
    addTestCreator(createClangFixItTest);
#endif
}

void ClangCodeModelPlugin::generateCompilationDB()
{
    Project * project = ProjectManager::startupProject();
    if (!project || !project->activeKit())
        return;

    const auto projectInfo = CppModelManager::projectInfo(project);
    if (!projectInfo)
        return;
    FilePath baseDir = projectInfo->buildRoot();
    if (baseDir == project->projectDirectory())
        baseDir = TemporaryDirectory::masterDirectoryFilePath();

    QFuture<GenerateCompilationDbResult> task
            = Utils::asyncRun(&Internal::generateCompilationDB, ProjectInfoList{projectInfo},
                              baseDir, CompilationDbPurpose::Project,
                              warningsConfigForProject(project),
                              globalClangOptions(),
                              FilePath());
    ProgressManager::addTask(task, Tr::tr("Generating Compilation DB"), "generate compilation db");
    m_generatorWatcher.setFuture(task);
}

void ClangCodeModelPlugin::createCompilationDBAction()
{
    // generate compile_commands.json
    ActionBuilder(this, Constants::GENERATE_COMPILATION_DB)
        .setParameterText(
            Tr::tr("Compilation Database for \"%1\""),
            Tr::tr("Compilation Database"),
            ActionBuilder::AlwaysEnabled)
        .bindContextAction(&m_generateCompilationDBAction)
        .setCommandAttribute(Command::CA_UpdateText)
        .setCommandDescription(Tr::tr("Generate Compilation Database"));

    if (Project *startupProject = ProjectManager::startupProject())
        m_generateCompilationDBAction->setParameter(startupProject->displayName());

    connect(&m_generatorWatcher, &QFutureWatcher<GenerateCompilationDbResult>::finished,
            this, [this] {
        QString message;
        if (m_generatorWatcher.future().resultCount()) {
            const GenerateCompilationDbResult result = m_generatorWatcher.result();
            if (result) {
                message = Tr::tr("Clang compilation database generated at \"%1\".")
                              .arg(result->toUserOutput());
            } else {
                message
                    = Tr::tr("Generating Clang compilation database failed: %1").arg(result.error());
            }
        } else {
            message = Tr::tr("Generating Clang compilation database canceled.");
        }
        MessageManager::writeFlashing(message);
        m_generateCompilationDBAction->setEnabled(true);
    });
    connect(m_generateCompilationDBAction, &QAction::triggered, this, [this] {
        if (!m_generateCompilationDBAction->isEnabled()) {
            MessageManager::writeDisrupting("Cannot generate compilation database: "
                                            "Generator is already running.");
            return;
        }
        Project * const project = ProjectManager::startupProject();
        if (!project) {
            MessageManager::writeDisrupting("Cannot generate compilation database: "
                                            "No active project.");
            return;
        }
        const ProjectInfo::ConstPtr projectInfo = CppModelManager::projectInfo(project);
        if (!projectInfo || projectInfo->projectParts().isEmpty()) {
            MessageManager::writeDisrupting("Cannot generate compilation database: "
                                            "Project has no C/C++ project parts.");
            return;
        }
        m_generateCompilationDBAction->setEnabled(false);
        generateCompilationDB();
    });
    connect(CppModelManager::instance(), &CppModelManager::projectPartsUpdated,
            this, [this](Project *project) {
        if (project != ProjectManager::startupProject())
            return;
        m_generateCompilationDBAction->setParameter(project->displayName());
    });
    connect(ProjectManager::instance(), &ProjectManager::startupProjectChanged,
            this, [this](Project *project) {
        m_generateCompilationDBAction->setParameter(project ? project->displayName() : "");
    });
    connect(ProjectManager::instance(), &ProjectManager::projectDisplayNameChanged,
            this, [this](Project *project) {
        if (project != ProjectManager::startupProject())
            return;
        m_generateCompilationDBAction->setParameter(project->displayName());
    });
    connect(ProjectManager::instance(), &ProjectManager::projectAdded,
            this, [this](Project *project) {
        project->registerGenerator(Constants::GENERATE_COMPILATION_DB,
                                   m_generateCompilationDBAction->text(),
                                   [this] { m_generateCompilationDBAction->trigger(); });
    });
}

} // namespace ClangCodeModel::Internal

#include "clangcodemodelplugin.moc"
