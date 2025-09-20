// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "abstracteditorsupport.h"

#include "cppeditortr.h"
#include "cppfilesettingspage.h"
#include "cppmodelmanager.h"

#include <utils/fileutils.h>
#include <utils/macroexpander.h>
#include <utils/templateengine.h>

using namespace Utils;

namespace CppEditor {

AbstractEditorSupport::AbstractEditorSupport(QObject *parent) :
    QObject(parent), m_revision(1)
{
    CppModelManager::addExtraEditorSupport(this);
}

AbstractEditorSupport::~AbstractEditorSupport()
{
    CppModelManager::removeExtraEditorSupport(this);
}

void AbstractEditorSupport::updateDocument()
{
    ++m_revision;
    CppModelManager::updateSourceFiles({filePath()});
}

void AbstractEditorSupport::notifyAboutUpdatedContents() const
{
    CppModelManager::emitAbstractEditorSupportContentsUpdated(
                filePath().toUrlishString(), sourceFilePath().toUrlishString(), contents());
}

QString AbstractEditorSupport::licenseTemplate(ProjectExplorer::Project *project,
                                               const FilePath &filePath, const QString &className)
{
    const QString license = Internal::cppFileSettingsForProject(project).licenseTemplate();
    Utils::MacroExpander expander;
    expander.registerVariable("Cpp:License:FileName", Tr::tr("The file name."),
                              [filePath] { return filePath.fileName(); });
    expander.registerVariable("Cpp:License:ClassName", Tr::tr("The class name."),
                              [className] { return className; });

    return TemplateEngine::processText(&expander, license).value_or(QString());
}

bool AbstractEditorSupport::usePragmaOnce(ProjectExplorer::Project *project)
{
    return Internal::cppFileSettingsForProject(project).headerPragmaOnce;
}

} // CppEditor
