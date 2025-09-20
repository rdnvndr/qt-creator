// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qbsproject.h"

#include "qbsbuildconfiguration.h"
#include "qbsbuildstep.h"
#include "qbsinstallstep.h"
#include "qbsnodes.h"
#include "qbsnodetreebuilder.h"
#include "qbspmlogging.h"
#include "qbsprojectimporter.h"
#include "qbsprojectmanagerconstants.h"
#include "qbsprojectmanagertr.h"
#include "qbsprojectparser.h"
#include "qbsrequest.h"
#include "qbssession.h"
#include "qbssettings.h"

#include <coreplugin/documentmanager.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/iversioncontrol.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/vcsmanager.h>
#include <cppeditor/cppprojectfile.h>
#include <cppeditor/generatedcodemodelsupport.h>
#include <projectexplorer/buildinfo.h>
#include <projectexplorer/buildmanager.h>
#include <projectexplorer/buildtargetinfo.h>
#include <projectexplorer/deployconfiguration.h>
#include <projectexplorer/deploymentdata.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/headerpath.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/environmentkitaspect.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectupdater.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/toolchain.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>

#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljstools/qmljsmodelmanager.h>

#include <qtsupport/qtcppkitinfo.h>
#include <qtsupport/qtkitaspect.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QSet>
#include <QVariantMap>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace QbsProjectManager {
namespace Internal {

// --------------------------------------------------------------------
// Constants:
// --------------------------------------------------------------------

class OpTimer
{
public:
    OpTimer(const char *name) : m_name(name)
    {
        m_timer.start();
    }
    ~OpTimer()
    {
        if (qtcEnvironmentVariableIsSet(Constants::QBS_PROFILING_ENV)) {
            MessageManager::writeSilently(
                QString("operation %1 took %2ms").arg(QLatin1String(m_name)).arg(m_timer.elapsed()));
        }
    }

private:
    QElapsedTimer m_timer;
    const char * const m_name;
};

// --------------------------------------------------------------------
// QbsProject:
// --------------------------------------------------------------------

QbsProject::QbsProject(const FilePath &fileName)
    : Project(Utils::Constants::QBS_MIMETYPE, fileName)
{
    setId(Constants::PROJECT_ID);
    setProjectLanguages(Context(ProjectExplorer::Constants::CXX_LANGUAGE_ID));
    setCanBuildProducts();
    setDisplayName(fileName.completeBaseName());
    setBuildSystemCreator<QbsBuildSystem>("qbs");
}

QbsProject::~QbsProject()
{
    delete m_importer;
}

ProjectImporter *QbsProject::projectImporter() const
{
    if (!m_importer)
        m_importer = new QbsProjectImporter(projectFilePath());
    return m_importer;
}

void QbsProject::configureAsExampleProject(Kit *kit)
{
    QList<BuildInfo> infoList;
    const QList<Kit *> kits(kit != nullptr ? QList<Kit *>({kit}) : KitManager::kits());
    for (Kit *k : kits) {
        if (QtSupport::QtKitAspect::qtVersion(k) != nullptr) {
            if (auto factory = BuildConfigurationFactory::find(k, projectFilePath()))
                infoList << factory->allAvailableSetups(k, projectFilePath());
        }
    }
    setup(infoList);
    if (activeBuildSystem())
        static_cast<QbsBuildSystem *>(activeBuildSystem())->prepareForParsing();
}

static bool supportsNodeAction(const QbsBuildSystem *bs, ProjectAction action, const Node *node)
{
    if (!bs->isProjectEditable())
        return false;
    if (action == RemoveFile || action == Rename)
        return node->asFileNode();
    return false;
}

QbsBuildSystem::QbsBuildSystem(BuildConfiguration *bc)
    : BuildSystem(bc),
      m_session(new QbsSession(this, BuildDeviceKitAspect::device(bc->kit()))),
      m_cppCodeModelUpdater(
          ProjectUpdaterFactory::createProjectUpdater(ProjectExplorer::Constants::CXX_LANGUAGE_ID))
{
    connect(m_session, &QbsSession::newGeneratedFilesForSources, this,
            [this](const QHash<QString, QStringList> &generatedFiles) {
        for (ExtraCompiler * const ec : std::as_const(m_extraCompilers))
            ec->deleteLater();
        m_extraCompilers.clear();
        for (auto it = m_sourcesForGeneratedFiles.cbegin();
             it != m_sourcesForGeneratedFiles.cend(); ++it) {
            for (const QString &sourceFile : it.value()) {
                const FilePaths generatedFilePaths = transform(
                            generatedFiles.value(sourceFile),
                            [](const QString &s) { return FilePath::fromString(s); });
                if (!generatedFilePaths.empty()) {
                    m_extraCompilers.append(it.key()->create(
                                                project(), FilePath::fromString(sourceFile),
                                                generatedFilePaths));
                }
            }
        }
        CppEditor::GeneratedCodeModelSupport::update(m_extraCompilers);
        for (ExtraCompiler *compiler : std::as_const(m_extraCompilers)) {
            if (compiler->isDirty())
                compiler->compileFile();
        }
        m_sourcesForGeneratedFiles.clear();
    });
    connect(m_session, &QbsSession::errorOccurred, this, [](QbsSession::Error e) {
        const QString msg = Tr::tr("Fatal qbs error: %1").arg(QbsSession::errorString(e));
        TaskHub::addTask(BuildSystemTask(Task::Error, msg));
    });
    connect(m_session, &QbsSession::fileListUpdated, this, &QbsBuildSystem::delayParsing);

    delayParsing();

    connect(bc->project(), &Project::activeBuildConfigurationChanged,
            this, &QbsBuildSystem::delayParsing);
    connect(bc->project(), &Project::projectFileIsDirty, this, &QbsBuildSystem::delayParsing);
    updateProjectNodes({});
}

QbsBuildSystem::~QbsBuildSystem()
{
    // Trigger any pending parsingFinished signals before destroying any other build system part:
    m_guard = {};
    m_parseRequest.reset();
    delete m_cppCodeModelUpdater;
    delete m_qbsProjectParser;
    qDeleteAll(m_extraCompilers);
}

bool QbsBuildSystem::supportsAction(Node *context, ProjectAction action, const Node *node) const
{
    if (dynamic_cast<QbsGroupNode *>(context)) {
        if (action == AddNewFile || action == AddExistingFile)
            return true;
    }

    if (dynamic_cast<QbsProductNode *>(context)) {
        if (action == AddNewFile || action == AddExistingFile)
            return true;
    }

    return supportsNodeAction(this, action, node);
}

bool QbsBuildSystem::addFiles(Node *context, const FilePaths &filePaths, FilePaths *notAdded)
{
    if (auto n = dynamic_cast<QbsGroupNode *>(context)) {
        FilePaths notAddedDummy;
        if (!notAdded)
            notAdded = &notAddedDummy;

        const QbsProductNode *prdNode = parentQbsProductNode(n);
        QTC_ASSERT(prdNode, *notAdded += filePaths; return false);
        return addFilesToProduct(filePaths, prdNode->productData(), n->groupData(), notAdded);
    }

    if (auto n = dynamic_cast<QbsProductNode *>(context)) {
        FilePaths notAddedDummy;
        if (!notAdded)
            notAdded = &notAddedDummy;
        return addFilesToProduct(filePaths, n->productData(), n->mainGroup(), notAdded);
    }

    return BuildSystem::addFiles(context, filePaths, notAdded);
}

RemovedFilesFromProject QbsBuildSystem::removeFiles(Node *context, const FilePaths &filePaths,
                                                    FilePaths *notRemoved)
{
    if (auto n = dynamic_cast<QbsGroupNode *>(context)) {
        FilePaths notRemovedDummy;
        if (!notRemoved)
            notRemoved = &notRemovedDummy;
        const QbsProductNode * const prdNode = parentQbsProductNode(n);
            QTC_ASSERT(prdNode, *notRemoved += filePaths; return RemovedFilesFromProject::Error);
            return removeFilesFromProduct(filePaths, prdNode->productData(), n->groupData(),
                                          notRemoved);
    }

    if (auto n = dynamic_cast<QbsProductNode *>(context)) {
        FilePaths notRemovedDummy;
        if (!notRemoved)
            notRemoved = &notRemovedDummy;
        return removeFilesFromProduct(filePaths, n->productData(), n->mainGroup(), notRemoved);
    }

    return BuildSystem::removeFiles(context, filePaths, notRemoved);
}

bool QbsBuildSystem::renameFiles(Node *context, const FilePairs &filesToRename, FilePaths *notRenamed)
{
    if (auto *n = dynamic_cast<QbsGroupNode *>(context)) {
        const QbsProductNode * const prdNode = parentQbsProductNode(n);
        QTC_ASSERT(prdNode, return false);

        if (session()->apiLevel() >= 6) {
            return renameFilesInProduct(
                filesToRename, prdNode->productData(), n->groupData(), notRenamed);
        }

        bool success = true;
        for (const auto &[oldFilePath, newFilePath] : filesToRename) {
            if (!renameFileInProduct(
                    oldFilePath.toUrlishString(),
                    newFilePath.toUrlishString(),
                    prdNode->productData(),
                    n->groupData())) {
                success = false;
                if (notRenamed)
                    *notRenamed << oldFilePath;
            }
        }
        return success;
    }

    if (auto *n = dynamic_cast<QbsProductNode *>(context)) {
        if (session()->apiLevel() >= 6)
            return renameFilesInProduct(filesToRename, n->productData(), n->mainGroup(), notRenamed);

        bool success = true;
        for (const auto &[oldFilePath, newFilePath] : filesToRename) {
            if (!renameFileInProduct(
                    oldFilePath.toUrlishString(),
                    newFilePath.toUrlishString(),
                    n->productData(),
                    n->mainGroup())) {
                success = false;
                if (notRenamed)
                    *notRenamed << oldFilePath;
            }
        }
        return success;
    }

    return BuildSystem::renameFiles(context, filesToRename, notRenamed);
}

bool QbsBuildSystem::addDependencies(ProjectExplorer::Node *context, const QStringList &dependencies)
{
    const QStringList lowercaseDeps = transform(dependencies, [](const QString &dep) -> QString {
        QTC_ASSERT(dep.size() > 3, return dep);
        return dep.left(3) + dep.mid(3).toLower();
    });

    if (session()->apiLevel() < 9)
        return BuildSystem::addDependencies(context, lowercaseDeps);

    if (auto *n = dynamic_cast<QbsGroupNode *>(context)) {
        const QbsProductNode * const prdNode = parentQbsProductNode(n);
        QTC_ASSERT(prdNode, return false);
        return addDependenciesToProduct(lowercaseDeps, prdNode->productData(), n->groupData());
    }

    if (auto *n = dynamic_cast<QbsProductNode *>(context))
        return addDependenciesToProduct(lowercaseDeps, n->productData(), n->mainGroup());

    return BuildSystem::addDependencies(context, dependencies);
}

QVariant QbsBuildSystem::additionalData(Id id) const
{
    if (id == "QmlDesignerImportPath") {
        const QJsonObject project = session()->projectData();
        QStringList paths;
        forAllProducts(project, [&paths](const QJsonObject &product) {
            for (const QJsonValue &v : product.value("properties").toObject()
                                           .value("qmlDesignerImportPaths").toArray()) {
                paths << v.toString();
            }
        });
        return paths;
    }
    return BuildSystem::additionalData(id);
}

ProjectExplorer::DeploymentKnowledge QbsProject::deploymentKnowledge() const
{
    return DeploymentKnowledge::Perfect;
}

FilePaths QbsBuildSystem::filesGeneratedFrom(const FilePath &sourceFile) const
{
    return FileUtils::toFilePathList(session()->filesGeneratedFrom(sourceFile.toUrlishString()));
}

bool QbsBuildSystem::isProjectEditable() const
{
    return !isParsing() && !BuildManager::isBuilding(target());
}

// Ensure that the file is not read only
bool QbsBuildSystem::ensureWriteableQbsFile(const FilePath &file)
{
    if (!file.isWritableFile()) {
        // Try via vcs manager
        IVersionControl *versionControl =
            VcsManager::findVersionControlForDirectory(file.parentDir());
        if (!versionControl || !versionControl->vcsOpen(file)) {
            bool makeWritable = file.setPermissions(file.permissions() | QFile::WriteUser);
            if (!makeWritable) {
                QMessageBox::warning(ICore::dialogParent(),
                                     Tr::tr("Failed"),
                                     Tr::tr("Could not write project file %1.").arg(file.toUserOutput()));
                return false;
            }
        }
    }
    return true;
}

bool QbsBuildSystem::addFilesToProduct(
        const FilePaths &filePaths,
        const QJsonObject &product,
        const QJsonObject &group,
        FilePaths *notAdded)
{
    ensureWriteableQbsFile(groupFilePath(group));
    const FileChangeResult result = session()->addFiles(
                Utils::transform(filePaths, &FilePath::path),
                product.value("full-display-name").toString(),
                group.value("name").toString());
    if (result.error().hasError()) {
        MessageManager::writeDisrupting(result.error().toString());
        *notAdded = FileUtils::toFilePathList(result.failedFiles());
    }
    return notAdded->isEmpty();
}

RemovedFilesFromProject QbsBuildSystem::removeFilesFromProduct(
        const FilePaths &filePaths,
        const QJsonObject &product,
        const QJsonObject &group,
        FilePaths *notRemoved)
{
    const auto allWildcardsInGroup = transform<FilePaths>(
                group.value("source-artifacts-from-wildcards").toArray(),
                [this](const QJsonValue &v) { return locationFilePath(v.toObject()); });
    FilePaths wildcardFiles;
    FilePaths nonWildcardFiles;
    for (const FilePath &filePath : filePaths) {
        if (allWildcardsInGroup.contains(filePath))
            wildcardFiles << filePath;
        else
            nonWildcardFiles << filePath;
    }

    ensureWriteableQbsFile(groupFilePath(group));
    const FileChangeResult result = session()->removeFiles(
                Utils::transform(nonWildcardFiles, &FilePath::path),
                product.value("name").toString(),
                group.value("name").toString());

    *notRemoved = Utils::transform(result.failedFiles(), [this](const QString &f) {
        return projectFilePath().withNewPath(f);
    });
    if (result.error().hasError())
        MessageManager::writeDisrupting(result.error().toString());
    const bool success = notRemoved->isEmpty();
    if (!wildcardFiles.isEmpty())
        *notRemoved += wildcardFiles;
    if (!success)
        return RemovedFilesFromProject::Error;
    if (!wildcardFiles.isEmpty())
        return RemovedFilesFromProject::Wildcard;
    return RemovedFilesFromProject::Ok;
}

bool QbsBuildSystem::renameFileInProduct(
        const QString &oldPath,
        const QString &newPath,
        const QJsonObject &product,
        const QJsonObject &group)
{
    if (newPath.isEmpty())
        return false;
    FilePaths dummy;
    // FIXME: The qbs API need a (bulk) renaming feature
    if (removeFilesFromProduct({FilePath::fromString(oldPath)}, product, group, &dummy)
            != RemovedFilesFromProject::Ok) {
        return false;
    }
    return addFilesToProduct({FilePath::fromString(newPath)}, product, group, &dummy);
}

bool QbsBuildSystem::renameFilesInProduct(
    const Utils::FilePairs &files,
    const QJsonObject &product,
    const QJsonObject &group,
    Utils::FilePaths *notRenamed)
{
    const auto allWildcardsInGroup = transform<FilePaths>(
        group.value("source-artifacts-from-wildcards").toArray(),
        [this](const QJsonValue &v) { return locationFilePath(v.toObject()); });
    using FileStringPair = std::pair<QString, QString>;
    using FileStringPairs = QList<FileStringPair>;
    FileStringPairs nonWildcardFiles;
    for (const FilePair &file : files) {
        if (!allWildcardsInGroup.contains(file.first))
            nonWildcardFiles << std::make_pair(file.first.path(), file.second.path());
    }

    ensureWriteableQbsFile(groupFilePath(group));
    const FileChangeResult result = session()->renameFiles(
        nonWildcardFiles,
        product.value("name").toString(),
        group.value("name").toString());

    *notRenamed = Utils::transform(result.failedFiles(), [this](const QString &f) {
        return projectFilePath().withNewPath(f);
    });
    if (result.error().hasError())
        MessageManager::writeDisrupting(result.error().toString());
    return notRenamed->isEmpty();
}

bool QbsBuildSystem::addDependenciesToProduct(
    const QStringList &deps, const QJsonObject &product, const QJsonObject &group)
{
    ensureWriteableQbsFile(groupFilePath(group));
    const ErrorInfo error = session()->addDependencies(
        deps, product.value("full-display-name").toString(), group.value("name").toString());
    if (error.hasError()) {
        MessageManager::writeDisrupting(error.toString());
        return false;
    }
    return true;
}

QString QbsBuildSystem::profile() const
{
    return QbsProfileManager::ensureProfileForKit(kit());
}

void QbsBuildSystem::updateAfterParse()
{
    qCDebug(qbsPmLog) << "Updating data after parse";
    OpTimer opTimer("updateAfterParse");
    updateProjectNodes([this] {
        updateDocuments();
        updateBuildTargetData();
        updateCppCodeModel();
        updateExtraCompilers();
        updateQmlJsCodeModel();
        m_envCache.clear();
        m_guard.markAsSuccess();
        m_guard = {};
        emitBuildSystemUpdated();
    });
}

void QbsBuildSystem::updateProjectNodes(const std::function<void ()> &continuation)
{
    m_treeCreationWatcher = new TreeCreationWatcher(this);
    connect(m_treeCreationWatcher, &TreeCreationWatcher::finished, this,
            [this, watcher = m_treeCreationWatcher, continuation] {
        std::unique_ptr<QbsProjectNode> rootNode(watcher->result());
        if (watcher != m_treeCreationWatcher) {
            watcher->deleteLater();
            return;
        }
        OpTimer("updateProjectNodes continuation");
        m_treeCreationWatcher->deleteLater();
        m_treeCreationWatcher = nullptr;
        if (project()->activeBuildSystem() != this) {
            return;
        }
        project()->setDisplayName(rootNode->displayName());
        setRootProjectNode(std::move(rootNode));
        if (continuation)
            continuation();
    });
    m_treeCreationWatcher->setFuture(Utils::asyncRun(ProjectExplorerPlugin::sharedThreadPool(),
            QThread::LowPriority, &buildQbsProjectTree,
            project()->displayName(), project()->projectFilePath(), project()->projectDirectory(),
            projectData()));
}

QbsBuildConfiguration *QbsBuildSystem::qbsBuildConfig() const
{
    return static_cast<QbsBuildConfiguration *>(buildConfiguration());
}

FilePath QbsBuildSystem::locationFilePath(const QJsonObject &loc) const
{
    return projectDirectory().withNewPath(loc.value("file-path").toString());
}

FilePath QbsBuildSystem::groupFilePath(const QJsonObject &group) const
{
    return locationFilePath(group.value("location").toObject());
}

FilePath QbsBuildSystem::installRoot()
{
    const auto dc = buildConfiguration()->activeDeployConfiguration();
    if (dc) {
        const QList<BuildStep *> steps = dc->stepList()->steps();
        for (const BuildStep * const step : steps) {
            if (!step->stepEnabled())
                continue;
            if (const auto qbsInstallStep = qobject_cast<const QbsInstallStep *>(step))
                return qbsInstallStep->installRoot();
        }
    }
    const QbsBuildStep * const buildStep = qbsBuildConfig()->qbsStep();
    return buildStep && buildStep->install() ? buildStep->installRoot() : FilePath();
}

void QbsBuildSystem::handleQbsParsingDone(bool success)
{
    QTC_ASSERT(m_qbsProjectParser, return);

    qCDebug(qbsPmLog) << "Parsing done, success:" << success;

    generateErrors(m_qbsProjectParser->error());

    bool dataChanged = false;
    bool envChanged = m_lastParseEnv != m_qbsProjectParser->environment();
    m_lastParseEnv = m_qbsProjectParser->environment();
    const bool isActiveBuildSystem = project()->activeBuildSystem() == this;
    if (success) {
        const QJsonObject projectData = m_qbsProjectParser->session()->projectData();
        if (projectData != m_projectData) {
            m_projectData = projectData;
            dataChanged = isActiveBuildSystem;
        } else if (isActiveBuildSystem
                   && (!project()->rootProjectNode() || static_cast<QbsProjectNode *>(
                           project()->rootProjectNode())->projectData() != projectData)) {
            // This is needed to trigger the necessary updates when switching targets.
            // Nothing has changed on the BuildSystem side, but this build system's data now
            // represents the project, so the data has changed from the overall project's
            // point of view.
            dataChanged = true;
        }
    }

    delete m_qbsProjectParser;
    m_qbsProjectParser = nullptr;

    if (dataChanged) {
        updateAfterParse();
        return;
    } else if (envChanged) {
        updateCppCodeModel();
    }
    if (success)
        m_guard.markAsSuccess();
    m_guard = {};

    // This one used to change the executable path of a Qbs desktop run configuration
    // in case the "install" check box in the build step is unchecked and then build
    // is triggered (which is otherwise a no-op).
    emitBuildSystemUpdated();
}

void QbsBuildSystem::triggerParsing()
{
    scheduleParsing({});
}

void QbsBuildSystem::delayParsing()
{
    if (buildConfiguration()->isActive())
        requestDelayedParse();
}

ExtraCompiler *QbsBuildSystem::findExtraCompiler(const ExtraCompilerFilter &filter) const
{
    return Utils::findOrDefault(m_extraCompilers, filter);
}

void QbsBuildSystem::scheduleParsing(const QVariantMap &extraConfig)
{
    m_parseRequest.reset(new QbsRequest);
    m_parseRequest->setParseData({this, extraConfig});
    connect(m_parseRequest.get(), &QbsRequest::done, this, [this] {
        m_parseRequest.release()->deleteLater();
    });
    m_parseRequest->start();
}

void QbsBuildSystem::startParsing(const QVariantMap &extraConfig)
{
    QTC_ASSERT(!m_qbsProjectParser, return);

    FilePath dir = buildConfiguration()->buildDirectory();
    Store config = qbsBuildConfig()->qbsConfiguration();
    QString installRoot = config.value(Constants::QBS_INSTALL_ROOT_KEY).toString();
    if (installRoot.isEmpty()) {
        installRoot = buildConfiguration()->macroExpander()->expand(
            QbsSettings::defaultInstallDirTemplate());
    }
    config.insert(Constants::QBS_INSTALL_ROOT_KEY, FilePath::fromUserInput(installRoot).path());
    config.insert(Constants::QBS_RESTORE_BEHAVIOR_KEY, "restore-and-track-changes");
    for (auto it = extraConfig.begin(); it != extraConfig.end(); ++it)
        config.insert(keyFromString(it.key()), it.value());
    Environment env = buildConfiguration()->environment();

    m_guard = guardParsingRun();

    prepareForParsing();

    cancelDelayedParseRequest();

    QTC_ASSERT(!m_qbsProjectParser, return);
    m_qbsProjectParser = new QbsProjectParser(this);
    m_treeCreationWatcher = nullptr;
    connect(m_qbsProjectParser, &QbsProjectParser::done,
            this, &QbsBuildSystem::handleQbsParsingDone);

    QbsProfileManager::updateProfileIfNecessary(kit());
    m_qbsProjectParser->parse(config, env, dir, qbsBuildConfig()->configurationName());
}

void QbsBuildSystem::cancelParsing()
{
    QTC_ASSERT(m_qbsProjectParser, return);
    m_qbsProjectParser->cancel();
}

void QbsBuildSystem::updateAfterBuild()
{
    OpTimer opTimer("updateAfterBuild");
    const QJsonObject projectData = session()->projectData();
    if (projectData == m_projectData) {
        DeploymentData deploymentDataTmp = deploymentData();
        deploymentDataTmp.setLocalInstallRoot(installRoot());
        setDeploymentData(deploymentDataTmp);
        emitBuildSystemUpdated();
        return;
    }
    qCDebug(qbsPmLog) << "Updating data after build";
    m_projectData = projectData;
    updateProjectNodes([this] {
        updateBuildTargetData();
        updateExtraCompilers();
        m_envCache.clear();
    });
}

void QbsBuildSystem::generateErrors(const ErrorInfo &e)
{
    e.generateTasks(Task::Error);
}

void QbsBuildSystem::prepareForParsing()
{
    TaskHub::clearTasks(ProjectExplorer::Constants::TASK_CATEGORY_BUILDSYSTEM);
    if (m_qbsProjectParser)
        m_qbsProjectParser->cancel();
}

void QbsBuildSystem::updateDocuments()
{
    OpTimer opTimer("updateDocuments");
    const FilePath buildDir = FilePath::fromString(
                m_projectData.value("build-directory").toString());
    const auto filePaths = transform<QSet<FilePath>>(
            m_projectData.value("build-system-files").toArray(),
            [](const QJsonValue &v) { return FilePath::fromString(v.toString()); });

    // A changed qbs file (project, module etc) should trigger a re-parse, but not if
    // the file was generated by qbs itself, in which case that might cause an infinite loop.
    const QSet<FilePath> nonBuildDirFilePaths = filtered(filePaths,
                                                            [buildDir](const FilePath &p) {
                                                                return !p.isChildOf(buildDir);
                                                            });
    project()->setExtraProjectFiles(nonBuildDirFilePaths);
}

static QString getMimeType(const QJsonObject &sourceArtifact)
{
    using namespace Utils::Constants;
    const auto tags = sourceArtifact.value("file-tags").toArray();
    if (tags.contains("hpp")) {
        const QString filePath = sourceArtifact.value("file-path").toString();
        if (CppEditor::ProjectFile::isAmbiguousHeader(filePath))
            return Utils::mimeTypeForFile(filePath).name();
        return QString(CPP_HEADER_MIMETYPE);
    }
    if (tags.contains("cpp"))
        return QString(CPP_SOURCE_MIMETYPE);
    if (tags.contains("c"))
        return QString(C_SOURCE_MIMETYPE);
    if (tags.contains("objc"))
        return QString(OBJECTIVE_C_SOURCE_MIMETYPE);
    if (tags.contains("objcpp"))
        return QString(OBJECTIVE_CPP_SOURCE_MIMETYPE);
    return {};
}

static QString groupLocationToCallGroupId(const QJsonObject &location)
{
    return QString::fromLatin1("%1:%2:%3")
                        .arg(location.value("file-path").toString())
                        .arg(location.value("line").toString())
                        .arg(location.value("column").toString());
}

// TODO: Receive the values from qbs when QBS-1030 is resolved.
static void getExpandedCompilerFlags(QStringList &cFlags, QStringList &cxxFlags,
                                     const QJsonObject &properties)
{
    const auto getCppProp = [properties](const char *propertyName) {
        return properties.value("cpp." + QLatin1String(propertyName));
    };
    const QJsonValue &enableExceptions = getCppProp("enableExceptions");
    const QJsonValue &enableRtti = getCppProp("enableRtti");
    const QString warningLevel = getCppProp("warningLevel").toString();
    QStringList commonFlags = arrayToStringList(getCppProp("platformCommonCompilerFlags"));
    commonFlags << arrayToStringList(getCppProp("commonCompilerFlags"))
                << arrayToStringList(getCppProp("platformDriverFlags"))
                << arrayToStringList(getCppProp("driverFlags"));
    const QStringList toolchain = arrayToStringList(properties.value("qbs.toolchain"));
    if (toolchain.contains("gcc")) {
        if (const QString sysroot = properties.value("qbs.sysroot").toString();
                !sysroot.isEmpty()) {
            commonFlags << "--sysroot" << sysroot;
        }
        bool hasTargetOption = false;
        if (toolchain.contains("clang")) {
            const int majorVersion = getCppProp("compilerVersionMajor").toInt();
            const int minorVersion = getCppProp("compilerVersionMinor").toInt();
            if (majorVersion > 3 || (majorVersion == 3 && minorVersion >= 1))
                hasTargetOption = true;
        }
        if (hasTargetOption) {
            commonFlags << "-target" << getCppProp("target").toString();
        } else {
            const QString targetArch = getCppProp("targetArch").toString();
            if (targetArch == "x86_64")
                commonFlags << "-m64";
            else if (targetArch == "i386")
                commonFlags << "-m32";
            const QString machineType = getCppProp("machineType").toString();
            if (!machineType.isEmpty())
                commonFlags << ("-march=" + machineType);
        }
        if (warningLevel == "all")
            commonFlags << "-Wall" << "-Wextra";
        else if (warningLevel == "none")
            commonFlags << "-w";
        const QStringList targetOS = arrayToStringList(properties.value("qbs.targetOS"));
        if (targetOS.contains("unix")) {
            const QVariant positionIndependentCode = getCppProp("positionIndependentCode");
            if (!positionIndependentCode.isValid() || positionIndependentCode.toBool())
                commonFlags << "-fPIC";
        }
        cFlags = cxxFlags = commonFlags;
        cFlags << arrayToStringList(getCppProp("cFlags"));
        cxxFlags << arrayToStringList(getCppProp("cxxFlags"));

        const auto cxxLanguageVersion = arrayToStringList(getCppProp("cxxLanguageVersion"));
        if (cxxLanguageVersion.contains("c++23"))
            cxxFlags << "-std=c++2b";
        else if (cxxLanguageVersion.contains("c++20"))
            cxxFlags << "-std=c++20";
        else if (cxxLanguageVersion.contains("c++17"))
            cxxFlags << "-std=c++17";
        else if (cxxLanguageVersion.contains("c++14"))
            cxxFlags << "-std=c++14";
        else if (cxxLanguageVersion.contains("c++11"))
            cxxFlags << "-std=c++11";
        else if (!cxxLanguageVersion.isEmpty())
            cxxFlags << ("-std=" + cxxLanguageVersion.first());
        const QString cxxStandardLibrary = getCppProp("cxxStandardLibrary").toString();
        if (!cxxStandardLibrary.isEmpty() && toolchain.contains("clang"))
            cxxFlags << ("-stdlib=" + cxxStandardLibrary);
        if (!enableExceptions.isUndefined()) {
            cxxFlags << QLatin1String(enableExceptions.toBool()
                                      ? "-fexceptions" : "-fno-exceptions");
        }
        if (!enableRtti.isUndefined())
            cxxFlags << QLatin1String(enableRtti.toBool() ? "-frtti" : "-fno-rtti");

        const auto cLanguageVersion = arrayToStringList(getCppProp("cLanguageVersion"));
        if (cLanguageVersion.contains("c18"))
            cFlags << "-cstd=c18";
        else if (cLanguageVersion.contains("c17"))
            cFlags << "-std=c17";
        else if (cLanguageVersion.contains("c11"))
            cFlags << "-std=c11";
        else if (cLanguageVersion.contains("c99"))
            cFlags << "-std=c99";
        else if (!cLanguageVersion.isEmpty())
            cFlags << ("-std=" + cLanguageVersion.first());

        if (targetOS.contains("darwin")) {
            const auto darwinVersion = getCppProp("minimumDarwinVersion").toString();
            if (!darwinVersion.isEmpty()) {
                const auto darwinVersionFlag = getCppProp("minimumDarwinVersionCompilerFlag")
                        .toString();
                if (!darwinVersionFlag.isEmpty())
                    cxxFlags << (darwinVersionFlag + '=' + darwinVersion);
            }
        }
    } else if (toolchain.contains("msvc")) {
        if (enableExceptions.toBool()) {
            const QString exceptionModel = getCppProp("exceptionHandlingModel").toString();
            if (exceptionModel == "default")
                commonFlags << "/EHsc";
            else if (exceptionModel == "seh")
                commonFlags << "/EHa";
            else if (exceptionModel == "externc")
                commonFlags << "/EHs";
        }
        if (warningLevel == "all")
            commonFlags << "/Wall";
        else if (warningLevel == "none")
            commonFlags << "/w";
        cFlags = cxxFlags = commonFlags;
        cFlags << "/TC";
        cxxFlags << "/TP";
        if (!enableRtti.isUndefined())
            cxxFlags << QLatin1String(enableRtti.toBool() ? "/GR" : "/GR-");
        const QJsonArray cxxLanguageVersion = getCppProp("cxxLanguageVersion").toArray();
        if (cxxLanguageVersion.contains("c++23"))
            cxxFlags << "/std:c++latest";
        else if (cxxLanguageVersion.contains("c++20"))
            cxxFlags << "/std:c++20";
        else if (cxxLanguageVersion.contains("c++17"))
            cxxFlags << "/std:c++17";
    } else {
        cFlags = cxxFlags = commonFlags;
    }
}

static RawProjectPart generateProjectPart(
        const FilePath &refFile,
        const QJsonObject &product,
        const QJsonObject &group,
        const std::shared_ptr<const Toolchain> &cToolchain,
        const std::shared_ptr<const Toolchain> &cxxToolchain,
        QtMajorVersion qtVersion,
        QString cPch,
        QString cxxPch,
        QString objcPch,
        QString objcxxPch
        )
{
    const QString productName = product.value("full-display-name").toString();
    const QString groupName = group.isEmpty() ? productName + "_generated_qtc_internal"
                                              : group.value("name").toString();
    const QJsonObject &groupOrProduct = group.isEmpty() ? product : group;
    RawProjectPart rpp;
    rpp.setQtVersion(qtVersion);
    QJsonObject props = group.value("module-properties").toObject();
    if (props.isEmpty())
        props = product.value("module-properties").toObject();
    rpp.setCallGroupId(groupLocationToCallGroupId(groupOrProduct.value("location").toObject()));

    QStringList cFlags;
    QStringList cxxFlags;
    getExpandedCompilerFlags(cFlags, cxxFlags, props);
    rpp.setFlagsForC({cToolchain.get(), cFlags, {}});
    rpp.setFlagsForCxx({cxxToolchain.get(), cxxFlags, {}});

    const QStringList defines = arrayToStringList(props.value("cpp.defines"))
            + arrayToStringList(props.value("cpp.platformDefines"));
    rpp.setMacros(transform<QList>(defines,
            [](const QString &s) { return Macro::fromKeyValue(s); }));

    ProjectExplorer::HeaderPaths headerPaths;
    QStringList list = arrayToStringList(props.value("cpp.includePaths"));
    list.removeDuplicates();
    for (const QString &p : std::as_const(list))
        headerPaths += HeaderPath::makeUser(FilePath::fromUserInput(p));
    list = arrayToStringList(props.value("cpp.distributionIncludePaths"))
            + arrayToStringList(props.value("cpp.systemIncludePaths"));
    list.removeDuplicates();
    for (const QString &p : std::as_const(list))
        headerPaths += HeaderPath::makeSystem(FilePath::fromUserInput(p));
    list = arrayToStringList(props.value("cpp.frameworkPaths"));
    list.append(arrayToStringList(props.value("cpp.systemFrameworkPaths")));
    list.removeDuplicates();
    for (const QString &p : std::as_const(list))
        headerPaths += HeaderPath::makeFramework(refFile.withNewPath(p));
    rpp.setHeaderPaths(headerPaths);
    rpp.setDisplayName(groupName);
    const QJsonObject location = groupOrProduct.value("location").toObject();
    rpp.setProjectFileLocation(
        refFile.withNewPath(location.value("file-path").toString()),
        location.value("line").toInt(),
        location.value("column").toInt());
    rpp.setBuildSystemTarget(QbsProductNode::getBuildKey(product));
    if (product.value("is-runnable").toBool()) {
        rpp.setBuildTargetType(BuildTargetType::Executable);
    } else {
        const QJsonArray pType = product.value("type").toArray();
        if (pType.contains("staticlibrary") || pType.contains("dynamiclibrary")
                || pType.contains("loadablemodule")) {
            rpp.setBuildTargetType(BuildTargetType::Library);
        } else {
            rpp.setBuildTargetType(BuildTargetType::Unknown);
        }
    }
    rpp.setSelectedForBuilding(groupOrProduct.value("is-enabled").toBool());

    QHash<QString, QJsonObject> filePathToSourceArtifact;
    bool hasCFiles = false;
    bool hasCxxFiles = false;
    bool hasObjcFiles = false;
    bool hasObjcxxFiles = false;
    const auto artifactWorker = [&](const QJsonObject &source) {
        const QString filePath = refFile.withNewPath(source.value("file-path").toString()).toUrlishString();
        QJsonObject translatedSource = source;
        translatedSource.insert("file-path", filePath);
        filePathToSourceArtifact.insert(filePath, translatedSource);
        for (const QJsonValue &tag : source.value("file-tags").toArray()) {
            if (tag == "c")
                hasCFiles = true;
            else if (tag == "cpp")
                hasCxxFiles = true;
            else if (tag == "objc")
                hasObjcFiles = true;
            else if (tag == "objcpp")
                hasObjcxxFiles = true;
        }
    };
    if (!group.isEmpty())
        forAllArtifacts(group, artifactWorker);
    else
        forAllArtifacts(product, ArtifactType::Generated, artifactWorker);

    QSet<QString> pchFiles;
    if (hasCFiles && props.value("cpp.useCPrecompiledHeader").toBool()
            && !cPch.isEmpty()) {
        pchFiles << cPch;
    }
    if (hasCxxFiles && props.value("cpp.useCxxPrecompiledHeader").toBool()
            && !cxxPch.isEmpty()) {
        pchFiles << cxxPch;
    }
    if (hasObjcFiles && props.value("cpp.useObjcPrecompiledHeader").toBool()
            && !objcPch.isEmpty()) {
        pchFiles << objcPch;
    }
    if (hasObjcxxFiles
            && props.value("cpp.useObjcxxPrecompiledHeader").toBool()
            && !objcxxPch.isEmpty()) {
        pchFiles << objcxxPch;
    }
    if (pchFiles.count() > 1) {
        qCWarning(qbsPmLog) << "More than one pch file enabled for source files in group"
                            << groupName << "in product" << productName;
        qCWarning(qbsPmLog) << "Expect problems with code model";
    }
    rpp.setPreCompiledHeaders(Utils::toList(pchFiles));
    rpp.setIncludedFiles(
        Utils::transform(arrayToStringList(props.value("cpp.prefixHeaders")), [&](const QString &f) {
            return refFile.withNewPath(f).toUrlishString();
        }));
    rpp.setFiles(filePathToSourceArtifact.keys(), {},
                 [filePathToSourceArtifact](const QString &filePath) {
        // Keep this lambda thread-safe!
        return getMimeType(filePathToSourceArtifact.value(filePath));
    });
    return rpp;
}

static RawProjectParts generateProjectParts(
        const FilePath &refFile,
        const QJsonObject &projectData,
        const std::shared_ptr<const Toolchain> &cToolchain,
        const std::shared_ptr<const Toolchain> &cxxToolchain,
        QtMajorVersion qtVersion
        )
{
    RawProjectParts rpps;
    const auto translatedPath = [&](const QJsonValue &v) {
        QTC_ASSERT(v.isString(), return QString());
        return refFile.withNewPath(v.toString()).toUrlishString();
    };
    forAllProducts(projectData, [&](const QJsonObject &prd) {
        QString cPch;
        QString cxxPch;
        QString objcPch;
        QString objcxxPch;
        const auto &pchFinder = [&](const QJsonObject &artifact) {
            const QJsonArray fileTags = artifact.value("file-tags").toArray();
            if (fileTags.contains("c_pch_src"))
                cPch = translatedPath(artifact.value("file-path"));
            if (fileTags.contains("cpp_pch_src"))
                cxxPch = translatedPath(artifact.value("file-path"));
            if (fileTags.contains("objc_pch_src"))
                objcPch = translatedPath(artifact.value("file-path"));
            if (fileTags.contains("objcpp_pch_src"))
                objcxxPch = translatedPath(artifact.value("file-path"));
        };
        forAllArtifacts(prd, ArtifactType::All, pchFinder);
        const Utils::QtMajorVersion qtVersionForPart
            = prd.value("module-properties").toObject().value("Qt.core.version").isUndefined()
                  ? Utils::QtMajorVersion::None
                  : qtVersion;
        const QJsonArray groups = prd.value("groups").toArray();
        const auto appendIfNotEmpty = [&rpps](const RawProjectPart &rpp) {
            if (!rpp.files.isEmpty())
                rpps << rpp;
        };
        for (const QJsonValue &g : groups) {
            appendIfNotEmpty(generateProjectPart(
                                 refFile, prd, g.toObject(), cToolchain, cxxToolchain, qtVersionForPart,
                                 cPch, cxxPch, objcPch, objcxxPch));
        }
        appendIfNotEmpty(generateProjectPart(
                             refFile, prd, {}, cToolchain, cxxToolchain, qtVersionForPart,
                             cPch, cxxPch, objcPch, objcxxPch));
    });
    return rpps;
}

void QbsBuildSystem::updateCppCodeModel()
{
    OpTimer optimer("updateCppCodeModel");
    const QJsonObject projectData = session()->projectData();
    if (projectData.isEmpty())
        return;

    const QtSupport::CppKitInfo kitInfo(kit());
    QTC_ASSERT(kitInfo.isValid(), return);
    const auto cToolchain = std::shared_ptr<Toolchain>(kitInfo.cToolchain
            ? kitInfo.cToolchain->clone() : nullptr);
    const auto cxxToolchain = std::shared_ptr<Toolchain>(kitInfo.cxxToolchain
            ? kitInfo.cxxToolchain->clone() : nullptr);

    m_cppCodeModelUpdater->update({project(), kitInfo, activeParseEnvironment(), {},
            [projectData, kitInfo, cToolchain, cxxToolchain, refFile = project()->projectFilePath()] {
                    return generateProjectParts(refFile, projectData, cToolchain, cxxToolchain,
                                                kitInfo.projectPartQtVersion);
    }});
}

void QbsBuildSystem::updateExtraCompilers()
{
    OpTimer optimer("updateExtraCompilers");
    const QJsonObject projectData = session()->projectData();
    if (projectData.isEmpty())
        return;

    const QList<ExtraCompilerFactory *> factories = ExtraCompilerFactory::extraCompilerFactories();
    QHash<QString, QStringList> sourcesForGeneratedFiles;
    m_sourcesForGeneratedFiles.clear();

    forAllProducts(projectData, [&, this](const QJsonObject &prd) {
        const QString productName = prd.value("full-display-name").toString();
        forAllArtifacts(prd, ArtifactType::Source, [&, this](const QJsonObject &source) {
            const QString filePath = source.value("file-path").toString();
            for (const QJsonValue &tag : source.value("file-tags").toArray()) {
                for (auto i = factories.cbegin(); i != factories.cend(); ++i) {
                    if ((*i)->sourceTag() == tag.toString()) {
                        m_sourcesForGeneratedFiles[*i] << filePath;
                        sourcesForGeneratedFiles[productName] << filePath;
                    }
                }
            }
        });
    });

    if (!sourcesForGeneratedFiles.isEmpty())
        session()->requestFilesGeneratedFrom(sourcesForGeneratedFiles);
}

void QbsBuildSystem::updateQmlJsCodeModel()
{
    OpTimer optimer("updateQmlJsCodeModel");
    QmlJS::ModelManagerInterface *modelManager = QmlJS::ModelManagerInterface::instance();
    if (!modelManager)
        return;
    QmlJS::ModelManagerInterface::ProjectInfo projectInfo
        = modelManager->defaultProjectInfoForProject(project(),
                                                     project()->files(Project::HiddenRccFolders));

    const QJsonObject projectData = session()->projectData();
    if (projectData.isEmpty())
        return;

    forAllProducts(projectData, [&projectInfo](const QJsonObject &product) {
        for (const QJsonValue &path : product.value("properties").toObject()
             .value("qmlImportPaths").toArray()) {
            projectInfo.importPaths.maybeInsert(Utils::FilePath::fromString(path.toString()),
                                                QmlJS::Dialect::Qml);
        }
    });

    project()->setProjectLanguage(ProjectExplorer::Constants::QMLJS_LANGUAGE_ID,
                                  !projectInfo.sourceFiles.isEmpty());
    modelManager->updateProjectInfo(projectInfo, project());
}

void QbsBuildSystem::updateApplicationTargets()
{
    QList<BuildTargetInfo> applications;
    forAllProducts(session()->projectData(), [this, &applications](const QJsonObject &productData) {
        if (!productData.value("is-enabled").toBool() || !productData.value("is-runnable").toBool())
            return;

        // TODO: Perhaps put this into a central location instead. Same for module properties etc
        const auto getProp = [productData](const QString &propName) {
            return productData.value("properties").toObject().value(propName);
        };
        const bool isQtcRunnable = getProp("qtcRunnable").toBool();
        const bool usesTerminal = getProp("consoleApplication").toBool();
        const QString projectFile = productData.value("location").toObject()
                .value("file-path").toString();
        QString targetFile;
        for (const QJsonValue &v : productData.value("generated-artifacts").toArray()) {
            const QJsonObject artifact = v.toObject();
            if (artifact.value("is-target").toBool() && artifact.value("is-executable").toBool()) {
                targetFile = artifact.value("file-path").toString();
                break;
            }
        }
        BuildTargetInfo bti;
        bti.buildKey = QbsProductNode::getBuildKey(productData);
        bti.targetFilePath = projectFilePath().withNewPath(targetFile);
        bti.projectFilePath = projectFilePath().withNewPath(projectFile);
        bti.isQtcRunnable = isQtcRunnable; // Fixed up below.
        bti.usesTerminal = usesTerminal;
        bti.displayName = productData.value("full-display-name").toString();
        bti.runEnvModifier = [targetFile, productData, this](Utils::Environment &env, bool usingLibraryPaths) {
            const QString productName = productData.value("full-display-name").toString();
            if (session()->projectData().isEmpty())
                return;

            const QString key = env.toStringList().join(QChar())
                    + productName
                    + QString::number(usingLibraryPaths);
            const auto it = m_envCache.constFind(key);
            if (it != m_envCache.constEnd()) {
                env = it.value();
                return;
            }

            QProcessEnvironment procEnv = env.toProcessEnvironment();
            procEnv.insert("QBS_RUN_FILE_PATH", targetFile);
            QStringList setupRunEnvConfig;
            if (!usingLibraryPaths)
                setupRunEnvConfig << "ignore-lib-dependencies";
            // TODO: It'd be preferable if we could somenow make this asynchronous.
            RunEnvironmentResult result = session()->getRunEnvironment(productName, procEnv,
                                                                       setupRunEnvConfig);
            if (result.error().hasError()) {
                Core::MessageManager::writeFlashing(
                    Tr::tr("Error retrieving run environment: %1").arg(result.error().toString()));
                return;
            }
            QProcessEnvironment fullEnv = result.environment();
            QTC_ASSERT(!fullEnv.isEmpty(), fullEnv = procEnv);
            env = Utils::Environment();
            for (const QString &key : fullEnv.keys())
                env.set(key, fullEnv.value(key));
            m_envCache.insert(key, env);
        };

        applications.append(bti);
    });
    setApplicationTargets(applications);
}

void QbsBuildSystem::updateDeploymentInfo()
{
    if (session()->projectData().isEmpty())
        return;
    DeploymentData deploymentData;
    forAllProducts(session()->projectData(), [&](const QJsonObject &product) {
        forAllArtifacts(product, ArtifactType::All, [&](const QJsonObject &artifact) {
            const QJsonObject installData = artifact.value("install-data").toObject();
            if (installData.value("is-installable").toBool()) {
                deploymentData.addFile(
                            projectFilePath().withNewPath(artifact.value("file-path").toString()),
                            QFileInfo(installData.value("install-file-path").toString()).path(),
                            artifact.value("is-executable").toBool()
                                ? DeployableFile::TypeExecutable : DeployableFile::TypeNormal);
            }
        });
    });
    deploymentData.setLocalInstallRoot(installRoot());
    setDeploymentData(deploymentData);
}

void QbsBuildSystem::updateBuildTargetData()
{
    OpTimer optimer("updateBuildTargetData");
    updateApplicationTargets();
    updateDeploymentInfo();

    // This one used after a normal build.
    emitBuildSystemUpdated();
}

} // namespace Internal
} // namespace QbsProjectManager
