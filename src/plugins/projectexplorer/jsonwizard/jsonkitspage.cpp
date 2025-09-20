// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "jsonkitspage.h"
#include "jsonwizard.h"

#include "../kit.h"
#include "../project.h"
#include "../projectexplorertr.h"
#include "../projectmanager.h"

#include <coreplugin/featureprovider.h>

#include <utils/algorithm.h>
#include <utils/macroexpander.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>

using namespace Core;
using namespace Utils;

namespace ProjectExplorer {

const char KEY_FEATURE[] = "feature";
const char KEY_CONDITION[] = "condition";

JsonKitsPage::JsonKitsPage(QWidget *parent) : TargetSetupPage(parent)
{ }

void JsonKitsPage::initializePage()
{
    auto wiz = qobject_cast<JsonWizard *>(wizard());
    QTC_ASSERT(wiz, return);

    connect(wiz, &JsonWizard::filesPolished, this, &JsonKitsPage::setupProjectFiles);

    const Id platform = Id::fromString(wiz->stringValue(QLatin1String("Platform")));
    const QSet<Id> preferred
            = evaluate(m_preferredFeatures, wiz->value(QLatin1String("PreferredFeatures")), wiz);
    const QSet<Id> required = evaluate(m_requiredFeatures,
                                       wiz->value(QLatin1String("RequiredFeatures")),
                                       wiz);

    const FilePath projectFilePath = wiz->expander()->expand(
        Utils::FilePath::fromString(unexpandedProjectPath()));
    setTasksGenerator([required, preferred, platform, projectFilePath](const Kit *k) -> Tasks {
        if (!k->hasFeatures(required))
            return {CompileTask(Task::Error, Tr::tr("At least one required feature is not present."))};
        if (platform.isValid() && !k->supportedPlatforms().contains(platform))
            return {CompileTask(Task::Unknown, Tr::tr("Platform is not supported."))};
        if (!k->hasFeatures(preferred))
            return {
                CompileTask(Task::Unknown, Tr::tr("At least one preferred feature is not present."))};
        if (const auto issuesGenerator = ProjectManager::getIssuesGenerator(projectFilePath))
            return issuesGenerator(k);
        return {};
    });
    setProjectPath(projectFilePath);

    TargetSetupPage::initializePage();
}

void JsonKitsPage::cleanupPage()
{
    auto wiz = qobject_cast<JsonWizard *>(wizard());
    QTC_ASSERT(wiz, return);

    disconnect(wiz, &JsonWizard::allDone, this, nullptr);

    TargetSetupPage::cleanupPage();
}

void JsonKitsPage::setUnexpandedProjectPath(const QString &path)
{
    m_unexpandedProjectPath = path;
}

QString JsonKitsPage::unexpandedProjectPath() const
{
    return m_unexpandedProjectPath;
}

void JsonKitsPage::setRequiredFeatures(const QVariant &data)
{
    m_requiredFeatures = parseFeatures(data);
}

void JsonKitsPage::setPreferredFeatures(const QVariant &data)
{
    m_preferredFeatures = parseFeatures(data);
}

void JsonKitsPage::setupProjectFiles(const JsonWizard::GeneratorFiles &files)
{
    for (const JsonWizard::GeneratorFile &f : files) {
        if (f.file.attributes() & GeneratedFile::OpenProjectAttribute) {
            Project *project = ProjectManager::openProject(Utils::mimeTypeForFile(f.file.filePath()),
                                                           f.file.filePath().absoluteFilePath());
            if (project) {
                if (setupProject(project))
                    project->saveSettings();
                delete project;
            }
        }
    }
}

QSet<Id> JsonKitsPage::evaluate(const QList<JsonKitsPage::ConditionalFeature> &list,
                                const QVariant &defaultSet, JsonWizard *wiz)
{
    if (list.isEmpty())
        return Id::fromStringList(defaultSet.toStringList());

    QSet<Id> features;
    for (const ConditionalFeature &f : list) {
        if (JsonWizard::boolFromVariant(f.condition, wiz->expander()))
            features.insert(Id::fromString(wiz->expander()->expand(f.feature)));
    }
    return features;
}

QList<JsonKitsPage::ConditionalFeature> JsonKitsPage::parseFeatures(const QVariant &data,
                                                                      QString *errorMessage)
{
    QList<ConditionalFeature> result;
    if (errorMessage)
        errorMessage->clear();

    if (data.isNull())
        return result;
    if (data.typeId() != QMetaType::QVariantList) {
        if (errorMessage)
            *errorMessage = Tr::tr("Feature list is set and not of type list.");
        return result;
    }

    const QList<QVariant> elements = data.toList();
    for (const QVariant &element : elements) {
        if (element.typeId() == QMetaType::QString) {
            result.append({ element.toString(), QVariant(true) });
        } else if (element.typeId() == QMetaType::QVariantMap) {
            const QVariantMap obj = element.toMap();
            const QString feature = obj.value(QLatin1String(KEY_FEATURE)).toString();
            if (feature.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = Tr::tr("No \"%1\" key found in feature list object.")
                        .arg(QLatin1String(KEY_FEATURE));
                }
                return QList<ConditionalFeature>();
            }

            result.append({ feature, obj.value(QLatin1String(KEY_CONDITION), true) });
        } else {
            if (errorMessage)
                *errorMessage = Tr::tr("Feature list element is not a string or object.");
            return QList<ConditionalFeature>();
        }
    }

    return result;
}

} // namespace ProjectExplorer
