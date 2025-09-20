// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "diagnosticmanager.h"

#include "client.h"
#include "languageclientmanager.h"
#include "languageclienttr.h"

#include <coreplugin/editormanager/documentmodel.h>

#include <projectexplorer/project.h>
#include <projectexplorer/taskhub.h>

#include <texteditor/fontsettings.h>
#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/textmark.h>
#include <texteditor/textstyles.h>

#include <utils/stringutils.h>
#include <utils/utilsicons.h>

#include <QAction>

using namespace LanguageServerProtocol;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace LanguageClient {

class TextMark : public TextEditor::TextMark
{
public:
    TextMark(TextDocument *doc, const Diagnostic &diag, const Client *client)
        : TextEditor::TextMark(doc, diag.range().start().line() + 1, {client->name(), client->id()})
    {
        setLineAnnotation(diag.message());
        setToolTip(diag.message());
        switch (diag.severity().value_or(DiagnosticSeverity::Hint)) {
        case DiagnosticSeverity::Error:
            setColor(Theme::CodeModel_Error_TextMarkColor);
            setIcon(Icons::CODEMODEL_ERROR.icon());
            break;
        case DiagnosticSeverity::Warning:
            setColor(Theme::CodeModel_Warning_TextMarkColor);
            setIcon(Icons::CODEMODEL_WARNING.icon());
            break;
        default:
            setColor(Theme::CodeModel_Info_TextMarkColor);
            break;
        }
    }
};

struct VersionedDiagnostics
{
    std::optional<int> version;
    QList<LanguageServerProtocol::Diagnostic> diagnostics;
};

class Marks
{
public:
    ~Marks() { qDeleteAll(marks); }
    bool enabled = true;
    QList<TextEditor::TextMark *> marks;
};

class DiagnosticManager::DiagnosticManagerPrivate
{
public:
    DiagnosticManagerPrivate(Client *client)
        : m_client(client)
    {}

    void showTasks(TextDocument *doc) {
        if (!doc || m_client != LanguageClientManager::clientForDocument(doc))
            return;
        TaskHub::clearTasks(m_taskCategory);
        const Tasks tasks = m_issuePaneEntries.value(doc->filePath());
        for (const Task &t : tasks)
            TaskHub::addTask(t);
    }

    QMap<FilePath, VersionedDiagnostics> m_diagnostics;
    QMap<FilePath, Marks> m_marks;
    Client *m_client;
    QHash<FilePath, Tasks> m_issuePaneEntries;
    Id m_extraSelectionsId = TextEditorWidget::CodeWarningsSelection;
    bool m_forceCreateTasks = true;
    Id m_taskCategory = Constants::TASK_CATEGORY_DIAGNOSTICS;
};

DiagnosticManager::DiagnosticManager(Client *client)
    : d(std::make_unique<DiagnosticManagerPrivate>(client))
{
    auto updateCurrentEditor = [this](Core::IEditor *editor) {
        if (editor)
            d->showTasks(qobject_cast<TextDocument *>(editor->document()));
    };
    connect(Core::EditorManager::instance(),
            &Core::EditorManager::currentEditorChanged,
            this,
            updateCurrentEditor);
}

DiagnosticManager::~DiagnosticManager()
{
    clearDiagnostics();
}

void DiagnosticManager::setDiagnostics(const FilePath &filePath,
                                       const QList<Diagnostic> &diagnostics,
                                       const std::optional<int> &version)
{
    hideDiagnostics(filePath);
    d->m_diagnostics[filePath] = {version, filteredDiagnostics(diagnostics)};
}

void DiagnosticManager::hideDiagnostics(const Utils::FilePath &filePath)
{
    if (auto doc = TextDocument::textDocumentForFilePath(filePath)) {
        if (doc == TextDocument::currentTextDocument())
            TaskHub::clearTasks(d->m_taskCategory);
        for (BaseTextEditor *editor : BaseTextEditor::textEditorsForDocument(doc))
            editor->editorWidget()->setExtraSelections(d->m_extraSelectionsId, {});
    }
    d->m_marks.remove(filePath);
    d->m_issuePaneEntries.remove(filePath);
}

QList<Diagnostic> DiagnosticManager::filteredDiagnostics(const QList<Diagnostic> &diagnostics) const
{
    return diagnostics;
}

void DiagnosticManager::disableDiagnostics(TextEditor::TextDocument *document)
{

    Marks &marks = d->m_marks[document->filePath()];
    if (!marks.enabled)
        return;
    for (TextEditor::TextMark *mark : std::as_const(marks.marks))
        mark->setColor(Utils::Theme::Color::IconsDisabledColor);
    marks.enabled = false;
}

void DiagnosticManager::showDiagnostics(const FilePath &filePath, int version)
{
    d->m_issuePaneEntries.remove(filePath);
    if (TextDocument *doc = TextDocument::textDocumentForFilePath(filePath)) {
        QList<QTextEdit::ExtraSelection> extraSelections;
        const VersionedDiagnostics &versionedDiagnostics = d->m_diagnostics.value(filePath);
        if (versionedDiagnostics.version.value_or(version) == version
            && !versionedDiagnostics.diagnostics.isEmpty()) {
            Marks &marks = d->m_marks[filePath];
            const bool isProjectFile = d->m_client->fileBelongsToProject(filePath);
            for (const Diagnostic &diagnostic : versionedDiagnostics.diagnostics) {
                const QTextEdit::ExtraSelection selection
                    = createDiagnosticSelection(diagnostic, doc->document());
                if (!selection.cursor.isNull())
                    extraSelections << selection;
                if (TextEditor::TextMark *mark = createTextMark(doc, diagnostic, isProjectFile))
                    marks.marks.append(mark);
                if (std::optional<Task> task = createTask(doc, diagnostic, isProjectFile))
                    d->m_issuePaneEntries[filePath].append(*task);
            }
            if (!marks.marks.isEmpty())
                emit textMarkCreated(filePath);
        }

        for (BaseTextEditor *editor : BaseTextEditor::textEditorsForDocument(doc))
            editor->editorWidget()->setExtraSelections(d->m_extraSelectionsId, extraSelections);

        if (doc == TextDocument::currentTextDocument())
            d->showTasks(doc);
    }
}

Client *DiagnosticManager::client() const
{
    return d->m_client;
}

TextEditor::TextMark *DiagnosticManager::createTextMark(TextDocument *doc,
                                                        const Diagnostic &diagnostic,
                                                        bool /*isProjectFile*/) const
{
    static const QIcon icon = Icon::fromTheme("edit-copy");
    static const QString tooltip = Tr::tr("Copy to Clipboard");
    auto mark = new TextMark(doc, diagnostic, d->m_client);
    mark->setActionsProvider([text = diagnostic.message()] {
        QAction *action = new QAction();
        action->setIcon(icon);
        action->setToolTip(tooltip);
        QObject::connect(action, &QAction::triggered, [text] {
            setClipboardAndSelection(text);
        });
        return QList<QAction *>{action};
    });
    return mark;
}

std::optional<Task> DiagnosticManager::createTask(
        TextDocument *doc,
        const LanguageServerProtocol::Diagnostic &diagnostic,
        bool isProjectFile) const
{
    if (!isProjectFile && !d->m_forceCreateTasks)
        return {};

    Task::TaskType taskType = Task::TaskType::Unknown;
    QIcon icon;

    if (const std::optional<DiagnosticSeverity> severity = diagnostic.severity()) {
        switch (*severity) {
        case DiagnosticSeverity::Error:
            taskType = Task::TaskType::Error;
            icon = Icons::CODEMODEL_ERROR.icon();
            break;
        case DiagnosticSeverity::Warning:
            taskType = Task::TaskType::Warning;
            icon = Icons::CODEMODEL_WARNING.icon();
            break;
        default:
            break;
        }
    }

    Task task(
        taskType,
        taskText(diagnostic),
        doc->filePath(),
        diagnostic.range().start().line() + 1,
        d->m_taskCategory,
        icon,
        Task::NoOptions);

    if (const std::optional<CodeDescription> codeDescription = diagnostic.codeDescription())
        task.addLinkDetail(codeDescription->href());

    return task;
}

QString DiagnosticManager::taskText(const LanguageServerProtocol::Diagnostic &diagnostic) const
{
    return diagnostic.message();
}

void DiagnosticManager::setTaskCategory(const Utils::Id &taskCategory)
{
    d->m_taskCategory = taskCategory;
}

void DiagnosticManager::setForceCreateTasks(bool forceCreateTasks)
{
    d->m_forceCreateTasks = forceCreateTasks;
}

QTextEdit::ExtraSelection DiagnosticManager::createDiagnosticSelection(
    const LanguageServerProtocol::Diagnostic &diagnostic, QTextDocument *textDocument) const
{
    const DiagnosticSeverity severity = diagnostic.severity().value_or(DiagnosticSeverity::Warning);
    TextStyle style;
    if (severity == DiagnosticSeverity::Error)
        style = C_ERROR;
    else if (severity == DiagnosticSeverity::Warning)
        style = C_WARNING;
    else if (severity == DiagnosticSeverity::Information)
        style = C_INFO;
    else
        return {};

    QTextCursor cursor(textDocument);
    cursor.setPosition(diagnostic.range().start().toPositionInDocument(textDocument));
    cursor.setPosition(diagnostic.range().end().toPositionInDocument(textDocument),
                       QTextCursor::KeepAnchor);

    const QTextCharFormat format = TextEditorSettings::fontSettings().toTextCharFormat(style);

    return QTextEdit::ExtraSelection{cursor, format};
}

void DiagnosticManager::setExtraSelectionsId(const Utils::Id &extraSelectionsId)
{
    // this function should be called before any diagnostics are handled
    QTC_CHECK(d->m_diagnostics.isEmpty());
    d->m_extraSelectionsId = extraSelectionsId;
}

void DiagnosticManager::forAllMarks(std::function<void (TextEditor::TextMark *)> func)
{
    for (const Marks &marks : std::as_const(d->m_marks)) {
        for (TextEditor::TextMark *mark : marks.marks)
            func(mark);
    }
}

void DiagnosticManager::clearDiagnostics()
{
    for (const Utils::FilePath &path : d->m_diagnostics.keys())
        hideDiagnostics(path);
    d->m_diagnostics.clear();
    QTC_ASSERT(d->m_marks.isEmpty(), d->m_marks.clear());
}

QList<Diagnostic> DiagnosticManager::diagnosticsAt(const FilePath &filePath,
                                                   const QTextCursor &cursor) const
{
    const int documentRevision = d->m_client->documentVersion(filePath);
    auto it = d->m_diagnostics.find(filePath);
    if (it == d->m_diagnostics.end())
        return {};
    if (documentRevision != it->version.value_or(documentRevision))
        return {};
    return Utils::filtered(it->diagnostics, [range = Range(cursor)](const Diagnostic &diagnostic) {
        return diagnostic.range().overlaps(range);
    });
}

bool DiagnosticManager::hasDiagnostic(const FilePath &filePath,
                                      const TextDocument *doc,
                                      const LanguageServerProtocol::Diagnostic &diag) const
{
    if (!doc)
        return false;
    const auto it = d->m_diagnostics.find(filePath);
    if (it == d->m_diagnostics.end())
        return {};
    const int revision = d->m_client->documentVersion(filePath);
    if (revision != it->version.value_or(revision))
        return false;
    return it->diagnostics.contains(diag);
}

bool DiagnosticManager::hasDiagnostics(const TextDocument *doc) const
{
    const FilePath docPath = doc->filePath();
    const auto it = d->m_diagnostics.find(docPath);
    if (it == d->m_diagnostics.end())
        return {};
    const int revision = d->m_client->documentVersion(docPath);
    if (revision != it->version.value_or(revision))
        return false;
    return !it->diagnostics.isEmpty();
}

} // namespace LanguageClient
