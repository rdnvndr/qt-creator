// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakeproject.h"

#include "cmakebuildsystem.h"
#include "cmakekitaspect.h"
#include "cmakeprojectconstants.h"
#include "cmakeprojectimporter.h"
#include "cmakeprojectmanagertr.h"
#include "presetsmacros.h"

#include <coreplugin/icontext.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildinfo.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/toolchainkitaspect.h>

#include <qtsupport/qtkitaspect.h>

#include <utils/mimeconstants.h>

using namespace ProjectExplorer;
using namespace Utils;
using namespace CMakeProjectManager::Internal;

namespace CMakeProjectManager {

static FilePath cmakeListTxtFromFilePath(const FilePath &filepath)
{
    if (filepath.endsWith(Constants::CMAKE_CACHE_TXT)) {
        QString errorMessage;
        const CMakeConfig config = CMakeConfig::fromFile(filepath, &errorMessage);
        const FilePath cmakeListsTxt = config.filePathValueOf("CMAKE_HOME_DIRECTORY")
                                           .pathAppended(Constants::CMAKE_LISTS_TXT);
        if (cmakeListsTxt.exists())
            return cmakeListsTxt;
    }
    return filepath;
}

/*!
  \class CMakeProject
*/
CMakeProject::CMakeProject(const FilePath &fileName)
    : Project(Utils::Constants::CMAKE_MIMETYPE, cmakeListTxtFromFilePath(fileName))
    , m_settings(this, true)
{
    setId(CMakeProjectManager::Constants::CMAKE_PROJECT_ID);
    setProjectLanguages(Core::Context(ProjectExplorer::Constants::CXX_LANGUAGE_ID));
    setDisplayName(projectDisplayName(projectFilePath()));
    setCanBuildProducts();
    setBuildSystemCreator<CMakeBuildSystem>("cmake");

    // Allow presets to check if being run under Qt Creator
    Environment::modifySystemEnvironment({{"QTC_RUN", "1"}});

    // This only influences whether 'Install into temporary host directory'
    // will show up by default enabled in some remote deploy configurations.
    // We rely on staging via the actual cmake build step.
    setHasMakeInstallEquivalent(false);

    readPresets();

    if (fileName.endsWith(Constants::CMAKE_CACHE_TXT))
        m_buildDirToImport = fileName.parentDir();
}

CMakeProject::~CMakeProject()
{
    delete m_projectImporter;
}

Tasks CMakeProject::projectIssues(const Kit *k) const
{
    Tasks result = Project::projectIssues(k);
    result.append(m_issues);
    return result;
}

ProjectImporter *CMakeProject::projectImporter() const
{
    if (!m_projectImporter)
        m_projectImporter = new CMakeProjectImporter(projectFilePath(), this);
    return m_projectImporter;
}

void CMakeProject::addIssue(IssueType type, const QString &text)
{
    m_issues.append(createTask(type, text));
}

void CMakeProject::clearIssues()
{
    m_issues.clear();
}

PresetsData CMakeProject::presetsData() const
{
    return m_presetsData;
}

template<typename T>
static QStringList recursiveInheritsList(const T &presetsHash, const QStringList &inheritsList)
{
    QStringList result;
    for (const QString &inheritFrom : inheritsList) {
        result << inheritFrom;
        if (presetsHash.contains(inheritFrom)) {
            auto item = presetsHash[inheritFrom];
            if (item.inherits)
                result << recursiveInheritsList(presetsHash, item.inherits.value());
        }
    }
    return result;
}

Internal::PresetsData CMakeProject::combinePresets(Internal::PresetsData &cmakePresetsData,
                                                   Internal::PresetsData &cmakeUserPresetsData)
{
    Internal::PresetsData result;
    result.version = cmakePresetsData.version;
    result.cmakeMinimimRequired = cmakePresetsData.cmakeMinimimRequired;

    result.include = cmakePresetsData.include;
    if (result.include) {
        if (cmakeUserPresetsData.include)
            result.include->append(cmakeUserPresetsData.include.value());
    } else {
        result.include = cmakeUserPresetsData.include;
    }

    result.vendor = cmakePresetsData.vendor;
    if (result.vendor) {
        if (cmakeUserPresetsData.vendor)
            result.vendor->insert(cmakeUserPresetsData.vendor.value());
    } else {
        result.vendor = cmakeUserPresetsData.vendor;
    }

    result.hasValidPresets = cmakePresetsData.hasValidPresets && cmakeUserPresetsData.hasValidPresets;

    auto combinePresetsInternal = [](auto &presetsHash,
                                     auto &presets,
                                     auto &userPresets,
                                     const QString &presetType) {
        // Populate the hash map with the CMakePresets
        for (const auto &p : presets)
            presetsHash.insert(p.name, p);

        auto resolveInherits = [](auto &presetsHash, auto &presetsList) {
            Utils::sort(presetsList, [](const auto &left, const auto &right) {
                const bool sameInheritance = left.inherits && right.inherits
                                             && left.inherits.value() == right.inherits.value();
                const bool leftInheritsRight = left.inherits
                                               && left.inherits.value().contains(right.name);

                const bool inheritsGreater = left.inherits && right.inherits
                                             && !left.inherits.value().isEmpty()
                                             && !right.inherits.value().isEmpty()
                                             && left.inherits.value().first()
                                                    > right.inherits.value().first();

                const bool noInheritsGreater = !left.inherits && !right.inherits
                                               && left.name > right.name;

                if ((left.inherits && !right.inherits) || leftInheritsRight || sameInheritance
                    || inheritsGreater || noInheritsGreater)
                    return false;
                return true;
            });
            for (auto &p : presetsList) {
                if (!p.inherits)
                    continue;

                const QStringList inheritsList = recursiveInheritsList(presetsHash,
                                                                       p.inherits.value());
                Utils::reverseForeach(inheritsList, [&presetsHash, &p](const QString &inheritFrom) {
                    if (presetsHash.contains(inheritFrom)) {
                        p.inheritFrom(presetsHash[inheritFrom]);
                        presetsHash[p.name] = p;
                    }
                });
            }
        };

        // First resolve the CMakePresets
        resolveInherits(presetsHash, presets);

        // Add the CMakeUserPresets to the resolve hash map
        for (const auto &p : userPresets) {
            if (presetsHash.contains(p.name)) {
                TaskHub::addTask(
                    BuildSystemTask(Task::TaskType::Error,
                                    Tr::tr("CMakeUserPresets.json cannot re-define the %1 preset: %2")
                                        .arg(presetType)
                                        .arg(p.name),
                                    "CMakeUserPresets.json"));
                TaskHub::requestPopup();
            } else {
                presetsHash.insert(p.name, p);
            }
        }

        // Then resolve the CMakeUserPresets
        resolveInherits(presetsHash, userPresets);

        // Get both CMakePresets and CMakeUserPresets into the result
        auto result = presets;

        // std::vector doesn't have append
        std::copy(userPresets.begin(), userPresets.end(), std::back_inserter(result));
        return result;
    };

    QHash<QString, PresetsDetails::ConfigurePreset> configurePresetsHash;
    QHash<QString, PresetsDetails::BuildPreset> buildPresetsHash;

    result.configurePresets = combinePresetsInternal(configurePresetsHash,
                                                     cmakePresetsData.configurePresets,
                                                     cmakeUserPresetsData.configurePresets,
                                                     "configure");
    result.buildPresets = combinePresetsInternal(buildPresetsHash,
                                                 cmakePresetsData.buildPresets,
                                                 cmakeUserPresetsData.buildPresets,
                                                 "build");

    return result;
}

void CMakeProject::setupBuildPresets(Internal::PresetsData &presetsData)
{
    for (auto &buildPreset : presetsData.buildPresets) {
        if (buildPreset.inheritConfigureEnvironment) {
            if (!buildPreset.configurePreset && !buildPreset.hidden) {
                TaskHub::addTask(BuildSystemTask(
                    Task::TaskType::Error,
                    Tr::tr("Build preset %1 is missing a corresponding configure preset.")
                        .arg(buildPreset.name)));
                TaskHub::requestPopup();
                presetsData.hasValidPresets = false;
            }

            const QString &configurePresetName = buildPreset.configurePreset.value_or(QString());
            buildPreset.environment
                = Utils::findOrDefault(presetsData.configurePresets,
                                       [configurePresetName](
                                           const PresetsDetails::ConfigurePreset &configurePreset) {
                                           return configurePresetName == configurePreset.name;
                                       })
                      .environment;
        }
    }
}

QString CMakeProject::projectDisplayName(const Utils::FilePath &projectFilePath)
{
    const QString fallbackDisplayName = projectFilePath.absolutePath().fileName();

    Result<QByteArray> fileContent = projectFilePath.fileContents();
    cmListFile cmakeListFile;
    std::string errorString;
    if (fileContent) {
        fileContent = fileContent->replace("\r\n", "\n");
        if (!cmakeListFile.ParseString(
                fileContent->toStdString(), projectFilePath.fileName().toStdString(), errorString)) {
            return fallbackDisplayName;
        }
    }

    QHash<QString, QString> setVariables;
    for (const auto &func : cmakeListFile.Functions) {
        if (func.LowerCaseName() == "set" && func.Arguments().size() == 2)
            setVariables.insert(
                QString::fromUtf8(func.Arguments()[0].Value),
                QString::fromUtf8(func.Arguments()[1].Value));

        if (func.LowerCaseName() == "project" && func.Arguments().size() > 0) {
            const QString projectName = QString::fromUtf8(func.Arguments()[0].Value);
            if (projectName.startsWith("${") && projectName.endsWith("}")) {
                const QString projectVar = projectName.mid(2, projectName.size() - 3);
                if (setVariables.contains(projectVar))
                    return setVariables.value(projectVar);
                else
                    return fallbackDisplayName;
            }
            return projectName;
        }
    }

    return fallbackDisplayName;
}

Internal::CMakeSpecificSettings &CMakeProject::settings()
{
    return m_settings;
}

void CMakeProject::readPresets()
{
    auto parsePreset = [](const Utils::FilePath &presetFile) -> Internal::PresetsData {
        Internal::PresetsData data;
        Internal::PresetsParser parser;

        QString errorMessage;
        int errorLine = -1;

        if (parser.parse(presetFile, errorMessage, errorLine)) {
            data = parser.presetsData();
        } else {
            TaskHub::addTask(
                BuildSystemTask(Task::TaskType::Error, errorMessage, presetFile, errorLine));
            TaskHub::requestPopup();
            data.hasValidPresets = false;
        }
        return data;
    };

    std::function<void(Internal::PresetsData & presetData, Utils::FilePaths & inclueStack)>
        resolveIncludes = [&](Internal::PresetsData &presetData, Utils::FilePaths &includeStack) {
            if (presetData.include) {
                for (const QString &path : presetData.include.value()) {
                    Utils::FilePath includePath = Utils::FilePath::fromUserInput(path);
                    if (!includePath.isAbsolutePath())
                        includePath = presetData.fileDir.resolvePath(path);

                    Internal::PresetsData includeData = parsePreset(includePath);
                    if (includeData.include) {
                        if (includeStack.contains(includePath)) {
                            TaskHub::addTask(BuildSystemTask(
                                Task::TaskType::Warning,
                                Tr::tr("Attempt to include \"%1\" which was already parsed.")
                                    .arg(includePath.path()),
                                Utils::FilePath(),
                                -1));
                            TaskHub::requestPopup();
                        } else {
                            resolveIncludes(includeData, includeStack);
                        }
                    }

                    presetData.configurePresets = includeData.configurePresets
                                                  + presetData.configurePresets;
                    presetData.buildPresets = includeData.buildPresets + presetData.buildPresets;
                    presetData.hasValidPresets = includeData.hasValidPresets && presetData.hasValidPresets;

                    includeStack << includePath;
                }
            }
        };

    const Utils::FilePath cmakePresetsJson = projectDirectory().pathAppended("CMakePresets.json");
    const Utils::FilePath cmakeUserPresetsJson = projectDirectory().pathAppended("CMakeUserPresets.json");

    Internal::PresetsData cmakePresetsData;
    if (cmakePresetsJson.exists())
        cmakePresetsData = parsePreset(cmakePresetsJson);
    Internal::PresetsData cmakeUserPresetsData;
    if (cmakeUserPresetsJson.exists())
        cmakeUserPresetsData = parsePreset(cmakeUserPresetsJson);

    // Both presets are optional, but at least one needs to be present
    if (!cmakePresetsJson.exists() && !cmakeUserPresetsJson.exists())
        return;

    // resolve the include
    Utils::FilePaths includeStack = {cmakePresetsJson};
    resolveIncludes(cmakePresetsData, includeStack);

    includeStack = {cmakeUserPresetsJson};
    resolveIncludes(cmakeUserPresetsData, includeStack);

    m_presetsData = combinePresets(cmakePresetsData, cmakeUserPresetsData);
    setupBuildPresets(m_presetsData);

    if (!m_presetsData.hasValidPresets) {
        m_presetsData = {};
        return;
    }

    for (const auto &configPreset : std::as_const(m_presetsData.configurePresets)) {
        if (configPreset.hidden)
            continue;

        if (configPreset.condition) {
            if (!CMakePresets::Macros::evaluatePresetCondition(configPreset, projectFilePath()))
                continue;
        }
        m_presetsData.havePresets = true;
        break;
    }
}

FilePath CMakeProject::buildDirectoryToImport() const
{
    return m_buildDirToImport;
}

ProjectExplorer::DeploymentKnowledge CMakeProject::deploymentKnowledge() const
{
    return !files([](const ProjectExplorer::Node *n) {
                return n->filePath().fileName() == "QtCreatorDeployment.txt";
            })
                   .isEmpty()
               ? DeploymentKnowledge::Approximative
               : DeploymentKnowledge::Bad;
}

void CMakeProject::configureAsExampleProject(ProjectExplorer::Kit *kit)
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
}

void CMakeProjectManager::CMakeProject::setOldPresetKits(
    const QList<ProjectExplorer::Kit *> &presetKits) const
{
    m_oldPresetKits = presetKits;
}

QList<Kit *> CMakeProject::oldPresetKits() const
{
    return m_oldPresetKits;
}

} // namespace CMakeProjectManager
