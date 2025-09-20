// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "jsonwizardgeneratorfactory.h"

#include "jsonwizard.h"

#include "../editorconfiguration.h"
#include "../project.h"
#include "../projectexplorerconstants.h"
#include "../projectexplorertr.h"

#include <coreplugin/dialogs/promptoverwritedialog.h>

#include <texteditor/icodestylepreferences.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <texteditor/storagesettings.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/textindenter.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/mimeutils.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTextCursor>
#include <QTextDocument>

using namespace Core;
using namespace TextEditor;
using namespace Utils;

namespace ProjectExplorer {

// --------------------------------------------------------------------
// Helpers:
// --------------------------------------------------------------------

static ICodeStylePreferences *codeStylePreferences(Project *project, Id languageId)
{
    if (!languageId.isValid())
        return nullptr;

    if (project)
        return project->editorConfiguration()->codeStyle(languageId);

    return TextEditorSettings::codeStyle(languageId);
}

// --------------------------------------------------------------------
// JsonWizardGenerator:
// --------------------------------------------------------------------

Result<> JsonWizardGenerator::formatFile(const JsonWizard *wizard, GeneratedFile *file)
{
    if (file->isBinary() || file->contents().isEmpty())
        return ResultOk; // nothing to do

    Id languageId = TextEditorSettings::languageId(Utils::mimeTypeForFile(file->filePath()).name());

    if (!languageId.isValid())
        return ResultOk; // don't modify files like *.ui, *.pro

    auto baseProject = qobject_cast<Project *>(wizard->property("SelectedProject").value<QObject *>());
    ICodeStylePreferencesFactory *factory = TextEditorSettings::codeStyleFactory(languageId);

    QTextDocument doc(file->contents());
    QTextCursor cursor(&doc);
    Indenter *indenter = nullptr;
    if (factory) {
        indenter = factory->createIndenter(&doc);
        indenter->setFileName(file->filePath());
    }
    if (!indenter)
        indenter = new TextIndenter(&doc);
    ICodeStylePreferences *codeStylePrefs = codeStylePreferences(baseProject, languageId);
    indenter->setCodeStylePreferences(codeStylePrefs);

    cursor.select(QTextCursor::Document);
    indenter->indent(cursor,
                     QChar::Null,
                     codeStylePrefs->currentTabSettings());
    delete indenter;
    if (globalStorageSettings().m_cleanWhitespace) {
        QTextBlock block = doc.firstBlock();
        while (block.isValid()) {
            TabSettings::removeTrailingWhitespace(cursor, block);
            block = block.next();
        }
    }
    file->setContents(doc.toPlainText());

    return ResultOk;
}

Result<> JsonWizardGenerator::writeFile(const JsonWizard *wizard, GeneratedFile *file)
{
    Q_UNUSED(wizard)
    Q_UNUSED(file)
    return ResultOk;
}

Result<> JsonWizardGenerator::postWrite(const JsonWizard *wizard, GeneratedFile *file)
{
    Q_UNUSED(wizard)
    Q_UNUSED(file)
    return ResultOk;
}

Result<> JsonWizardGenerator::polish(const JsonWizard *wizard, GeneratedFile *file)
{
    Q_UNUSED(wizard)
    Q_UNUSED(file)
    return ResultOk;
}

Result<> JsonWizardGenerator::allDone(const JsonWizard *wizard, GeneratedFile *file)
{
    Q_UNUSED(wizard)
    Q_UNUSED(file)
    return ResultOk;
}

JsonWizardGenerator::OverwriteResult JsonWizardGenerator::promptForOverwrite(JsonWizard::GeneratorFiles *files,
                                                                             QString *errorMessage)
{
    FilePaths existingFiles;
    bool oddStuffFound = false;

    for (const JsonWizard::GeneratorFile &f : std::as_const(*files)) {
        if (f.file.filePath().exists()
                && !(f.file.attributes() & GeneratedFile::ForceOverwrite)
                && !(f.file.attributes() & GeneratedFile::KeepExistingFileAttribute))
            existingFiles.append(f.file.filePath());
    }
    if (existingFiles.isEmpty())
        return OverwriteOk;

    // Before prompting to overwrite existing files, loop over files and check
    // if there is anything blocking overwriting them (like them being links or folders).
    // Format a file list message as ( "<file1> [readonly], <file2> [folder]").
    const QString commonExistingPath = FileUtils::commonPath(existingFiles).toUserOutput();
    const int commonPathSize = commonExistingPath.size();
    QString fileNamesMsgPart;
    for (const FilePath &filePath : std::as_const(existingFiles)) {
        if (filePath.exists()) {
            if (!fileNamesMsgPart.isEmpty())
                fileNamesMsgPart += QLatin1String(", ");
            const QString namePart = filePath.toUserOutput().mid(commonPathSize);
            if (filePath.isDir()) {
                oddStuffFound = true;
                fileNamesMsgPart += Tr::tr("%1 [folder]").arg(namePart);
            } else if (filePath.isSymLink()) {
                oddStuffFound = true;
                fileNamesMsgPart += Tr::tr("%1 [symbolic link]").arg(namePart);
            } else if (!filePath.isWritableDir() && !filePath.isWritableFile()) {
                oddStuffFound = true;
                fileNamesMsgPart += Tr::tr("%1 [read only]").arg(namePart);
            }
        }
    }

    if (oddStuffFound) {
        *errorMessage = Tr::tr("The directory %1 contains files which cannot be overwritten:\n%2.")
                .arg(commonExistingPath).arg(fileNamesMsgPart);
        return OverwriteError;
    }

    // Prompt to overwrite existing files.
    PromptOverwriteDialog overwriteDialog;

    // Scripts cannot handle overwrite
    overwriteDialog.setFiles(existingFiles);
    for (const JsonWizard::GeneratorFile &file : std::as_const(*files))
        if (!file.generator->canKeepExistingFiles())
            overwriteDialog.setFileEnabled(file.file.filePath(), false);
    if (overwriteDialog.exec() != QDialog::Accepted)
        return OverwriteCanceled;

    const QSet<FilePath> existingFilesToKeep = Utils::toSet(overwriteDialog.uncheckedFiles());
    if (existingFilesToKeep.size() == files->size()) // All exist & all unchecked->Cancel.
        return OverwriteCanceled;

    // Set 'keep' attribute in files
    for (JsonWizard::GeneratorFile &file : *files) {
        if (!existingFilesToKeep.contains(file.file.filePath()))
            continue;

        file.file.setAttributes(file.file.attributes() | GeneratedFile::KeepExistingFileAttribute);
    }
    return OverwriteOk;
}

Result<> JsonWizardGenerator::formatFiles(const JsonWizard *wizard, JsonWizard::GeneratorFiles *files)
{
    for (auto i = files->begin(); i != files->end(); ++i) {
        if (const Result<> res = i->generator->formatFile(wizard, &(i->file)); !res)
            return res;
    }
    return ResultOk;
}

Result<> JsonWizardGenerator::writeFiles(const JsonWizard *wizard, JsonWizard::GeneratorFiles *files)
{
    for (auto i = files->begin(); i != files->end(); ++i) {
        if (const Result<> res = i->generator->writeFile(wizard, &(i->file)); !res)
            return res;
    }
    return ResultOk;
}

Result<> JsonWizardGenerator::postWrite(const JsonWizard *wizard, JsonWizard::GeneratorFiles *files)
{
    for (auto i = files->begin(); i != files->end(); ++i) {
        if (const Result<> res = i->generator->postWrite(wizard, &(i->file)); !res)
            return res;
    }
    return ResultOk;
}

Result<> JsonWizardGenerator::polish(const JsonWizard *wizard, JsonWizard::GeneratorFiles *files)
{
    for (auto i = files->begin(); i != files->end(); ++i) {
        if (const Result<> res = i->generator->polish(wizard, &(i->file)); !res)
            return res;
    }
    return ResultOk;
}

Result<> JsonWizardGenerator::allDone(const JsonWizard *wizard, JsonWizard::GeneratorFiles *files)
{
    for (auto i = files->begin(); i != files->end(); ++i) {
        if (const Result<> res = i->generator->allDone(wizard, &(i->file)); !res)
            return res;
    }
    return ResultOk;
}

// --------------------------------------------------------------------
// JsonWizardGeneratorFactory:
// --------------------------------------------------------------------

void JsonWizardGeneratorFactory::setTypeIdsSuffixes(const QStringList &suffixes)
{
    m_typeIds = Utils::transform(suffixes, [](QString suffix) {
        return Id(Constants::GENERATOR_ID_PREFIX).withSuffix(suffix);
    });
}

void JsonWizardGeneratorFactory::setTypeIdsSuffix(const QString &suffix)
{
    setTypeIdsSuffixes({suffix});
}

} // namespace ProjectExplorer
