// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "formwindowfile.h"
#include "qtcreatorintegration.h"
#include "designerconstants.h"
#include "resourcehandler.h"

#include <utils/filepath.h>
#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>

#include <QApplication>
#include <QBuffer>
#include <QDebug>
#include <QDesignerFormWindowInterface>
#include <QDesignerFormWindowManagerInterface>
#include <QDesignerFormEditorInterface>
#include <QTextDocument>
#include <QUndoStack>

using namespace Core;
using namespace Utils;

namespace Designer::Internal {

FormWindowFile::FormWindowFile(QDesignerFormWindowInterface *form, QObject *parent)
  : m_formWindow(form)
{
    setMimeType(Utils::Constants::FORM_MIMETYPE);
    setParent(parent);
    setId(Utils::Id(Designer::Constants::K_DESIGNER_XML_EDITOR_ID));
    // Designer needs UTF-8 regardless of settings.
    setCodec("UTF-8");
    connect(m_formWindow->core()->formWindowManager(), &QDesignerFormWindowManagerInterface::formWindowRemoved,
            this, &FormWindowFile::slotFormWindowRemoved);
    connect(m_formWindow->commandHistory(), &QUndoStack::indexChanged,
            this, &FormWindowFile::setShouldAutoSave);
    connect(m_formWindow.data(), &QDesignerFormWindowInterface::changed, this, &FormWindowFile::updateIsModified);

    m_resourceHandler = new ResourceHandler(form);
    connect(this, &FormWindowFile::filePathChanged,
            m_resourceHandler, &ResourceHandler::updateResources);
}

Result<> FormWindowFile::open(const FilePath &filePath, const FilePath &realFilePath)
{
    if (Designer::Constants::Internal::debug)
        qDebug() << "FormWindowFile::open" << filePath.toUserOutput();

    QDesignerFormWindowInterface *form = formWindow();
    QTC_ASSERT(form, return ResultError(ResultAssert));

    if (filePath.isEmpty())
        return ResultError("File name is empty"); // FIXME: Use something better

    QString contents;
    TextFileFormat::ReadResult readResult = read(filePath.absoluteFilePath(), &contents);
    if (readResult.code != TextFileFormat::ReadSuccess)
        return ResultError(readResult.error);

    form->setFileName(filePath.absoluteFilePath().toUrlishString());
    const QByteArray contentsBA = contents.toUtf8();
    QBuffer str;
    str.setData(contentsBA);
    str.open(QIODevice::ReadOnly);
    QString errorString;
    if (!form->setContents(&str, &errorString))
        return ResultError(errorString);
    form->setDirty(filePath != realFilePath);

    syncXmlFromFormWindow();
    setFilePath(filePath.absoluteFilePath());
    setShouldAutoSave(false);
    resourceHandler()->updateProjectResources();

    return ResultOk;
}

Result<> FormWindowFile::saveImpl(const FilePath &filePath, bool autoSave)
{
    if (!m_formWindow)
        return ResultError("ASSERT: FormWindoFile: !m_formWindow");
    if (filePath.isEmpty())
        return ResultError("ASSERT: FormWindowFile: filePath.isEmpty()");

    const QString oldFormName = m_formWindow->fileName();
    if (!autoSave)
        m_formWindow->setFileName(filePath.toUrlishString());

    const Result<> res = writeFile(filePath);
    m_shouldAutoSave = false;
    if (autoSave)
        return res;

    if (!res) {
        m_formWindow->setFileName(oldFormName);
        return res;
    }

    m_formWindow->setDirty(false);
    setFilePath(filePath);
    updateIsModified();

    return ResultOk;
}

QByteArray FormWindowFile::contents() const
{
    return formWindowContents().toUtf8();
}

Result<> FormWindowFile::setContents(const QByteArray &contents)
{
    if (Designer::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << contents.size();

    document()->clear();

    QTC_ASSERT(m_formWindow, return ResultError(ResultAssert));

    if (contents.isEmpty())
        return ResultError(ResultAssert);

    // If we have an override cursor, reset it over Designer loading,
    // should it pop up messages about missing resources or such.
    const bool hasOverrideCursor = QApplication::overrideCursor();
    QCursor overrideCursor;
    if (hasOverrideCursor) {
        overrideCursor = QCursor(*QApplication::overrideCursor());
        QApplication::restoreOverrideCursor();
    }

    const bool success = m_formWindow->setContents(QString::fromUtf8(contents));

    if (hasOverrideCursor)
        QApplication::setOverrideCursor(overrideCursor);

    if (!success)
        return ResultError(ResultAssert);

    syncXmlFromFormWindow();
    setShouldAutoSave(false);
    return ResultOk;
}

void FormWindowFile::setFilePath(const FilePath &newName)
{
    m_formWindow->setFileName(newName.toUrlishString());
    IDocument::setFilePath(newName);
}

void FormWindowFile::updateIsModified()
{
    if (m_modificationChangedGuard.isLocked())
        return;

    bool value = m_formWindow && m_formWindow->isDirty();
    if (value)
        emit contentsChanged();
    if (value == m_isModified)
        return;
    m_isModified = value;
    emit changed();
}

bool FormWindowFile::shouldAutoSave() const
{
    return m_shouldAutoSave;
}

bool FormWindowFile::isModified() const
{
    return m_formWindow && m_formWindow->isDirty();
}

bool FormWindowFile::isSaveAsAllowed() const
{
    return true;
}

Result<> FormWindowFile::reload(ReloadFlag flag, ChangeType type)
{
    if (flag == FlagIgnore) {
        if (!m_formWindow || type != TypeContents)
            return ResultOk;
        const bool wasModified = m_formWindow->isDirty();
        {
            Utils::GuardLocker locker(m_modificationChangedGuard);
            // hack to ensure we clean the clear state in form window
            m_formWindow->setDirty(false);
            m_formWindow->setDirty(true);
        }
        if (!wasModified)
            updateIsModified();
        return ResultOk;
    } else {
        emit aboutToReload();
        const Result<> result = open(filePath(), filePath());
        emit reloadFinished(result.has_value());
        return result;
    }
}

void FormWindowFile::setFallbackSaveAsFileName(const QString &fn)
{
    if (Designer::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << filePath() << fn;

    m_suggestedName = fn;
}

QString FormWindowFile::fallbackSaveAsFileName() const
{
    return m_suggestedName;
}

bool FormWindowFile::supportsCodec(const QByteArray &codec) const
{
    return TextEditor::TextDocument::isUtf8Codec(codec);
}

Result<> FormWindowFile::writeFile(const Utils::FilePath &filePath) const
{
    if (Designer::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << this->filePath() << filePath;
    auto *integration = qobject_cast<QtCreatorIntegration *>(m_formWindow->core()->integration());
    Q_ASSERT(integration);
    if (!integration->setQtVersionFromFile(filePath))
        integration->resetQtVersion();
    return write(filePath, format(), m_formWindow->contents());
}

QDesignerFormWindowInterface *FormWindowFile::formWindow() const
{
    return m_formWindow;
}

void FormWindowFile::syncXmlFromFormWindow()
{
    document()->setPlainText(formWindowContents());
}

QString FormWindowFile::formWindowContents() const
{
    // TODO: No warnings about spacers here
    QTC_ASSERT(m_formWindow, return QString());
    return m_formWindow->contents();
}

ResourceHandler *FormWindowFile::resourceHandler() const
{
    return m_resourceHandler;
}

void FormWindowFile::slotFormWindowRemoved(QDesignerFormWindowInterface *w)
{
    // Release formwindow as soon as the FormWindowManager removes
    // as calls to isDirty() are triggered at arbitrary times
    // while building.
    if (w == m_formWindow)
        m_formWindow = nullptr;
}

} // namespace Designer::Internal
