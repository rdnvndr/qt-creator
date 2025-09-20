// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0
#include "fileextractor.h"

#include <QRandomGenerator>
#include <QStorageInfo>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <utils/fileutils.h>
#include <utils/qtcassert.h>
#include <utils/unarchiver.h>

using namespace Utils;

namespace QmlDesigner {

FileExtractor::FileExtractor(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(100);
    m_timer.setSingleShot(false);

    QObject::connect(this, &FileExtractor::targetFolderExistsChanged, this, [this] {
        if (targetFolderExists())
            m_birthTime = QFileInfo(m_targetPath.toUrlishString() + "/" + m_archiveName).birthTime();
        else
            m_birthTime = QDateTime();

        emit birthTimeChanged();
    });

    QObject::connect(
        &m_timer, &QTimer::timeout, this, [this] {
            static QHash<QString, int> hash;
            QDirIterator it(m_targetFolder, {"*.*"}, QDir::Files, QDirIterator::Subdirectories);

            int count = 0;
            while (it.hasNext()) {
                if (!hash.contains(it.fileName())) {
                    m_currentFile = it.fileName();
                    hash.insert(m_currentFile, 0);
                    emit currentFileChanged();
                }
                it.next();
                count++;
            }

            qint64 currentSize = m_bytesBefore
                                 - QStorageInfo(m_targetPath.toFileInfo().dir()).bytesAvailable();

            // We can not get the uncompressed size of the archive yet, that is why we use an
            // approximation. We assume a 50% compression rate.

            int progress = 0;
            if (m_compressedSize > 0)
                progress = std::min(100ll, currentSize * 100 / m_compressedSize * 2);

            if (progress >= 0) {
                m_progress = progress;
                emit progressChanged();
            } else {
                qWarning() << "FileExtractor has got negative progress. Likely due to QStorageInfo.";
            }

            m_size = QString::number(currentSize);
            m_count = QString::number(count);
            emit sizeChanged();
        });
}

FileExtractor::~FileExtractor()
{
    removeTempTargetPath();
}

void FileExtractor::changeTargetPath(const QString &path)
{
    removeTempTargetPath();
    m_targetPath = FilePath::fromString(path);
    emit targetPathChanged();
    emit targetFolderExistsChanged();
}

QString FileExtractor::targetPath() const
{
    return m_targetPath.toUserOutput();
}

void FileExtractor::setTargetPath(const QString &path)
{
    removeTempTargetPath();
    m_targetPath = FilePath::fromString(path);

    QDir dir(m_targetPath.toUrlishString());

    if (!path.isEmpty() && !dir.exists()) {
        // Even though m_targetPath will be created eventually, we need to make sure the path exists
        // before m_bytesBefore is being calculated.
        dir.mkpath(m_targetPath.toUrlishString());
    }
}

void FileExtractor::browse()
{
    const FilePath path = FileUtils::getExistingDirectory(tr("Choose Directory"), m_targetPath);
    if (!path.isEmpty()) {
        removeTempTargetPath();
        m_targetPath = path;
    }

    emit targetPathChanged();
    emit targetFolderExistsChanged();
}

void FileExtractor::setSourceFile(const QString &sourceFilePath)
{
    m_sourceFile = FilePath::fromString(sourceFilePath);
    emit targetFolderExistsChanged();
}

void FileExtractor::setArchiveName(const QString &filePath)
{
    m_archiveName = filePath;
    emit targetFolderExistsChanged();
}

const QString FileExtractor::detailedText() const
{
    return m_detailedText;
}

void FileExtractor::setClearTargetPathContents(bool value)
{
    if (m_clearTargetPathContents != value) {
        m_clearTargetPathContents = value;
        emit clearTargetPathContentsChanged();
    }
}

bool FileExtractor::clearTargetPathContents() const
{
    return m_clearTargetPathContents;
}

void FileExtractor::setAlwaysCreateDir(bool value)
{
    if (m_alwaysCreateDir != value) {
        m_alwaysCreateDir = value;
        emit alwaysCreateDirChanged();
    }
}

bool FileExtractor::alwaysCreateDir() const
{
    return m_alwaysCreateDir;
}

bool FileExtractor::finished() const
{
    return m_finished;
}

QString FileExtractor::currentFile() const
{
    return m_currentFile;
}

QString FileExtractor::size() const
{
    return m_size;
}

QString FileExtractor::count() const
{
    return m_count;
}

bool FileExtractor::targetFolderExists() const
{
    return QFileInfo::exists(m_targetPath.toUrlishString() + "/" + m_archiveName);
}

int FileExtractor::progress() const
{
    return m_progress;
}

QDateTime FileExtractor::birthTime() const
{
    return m_birthTime;
}

QString FileExtractor::archiveName() const
{
    return m_archiveName;
}

QString FileExtractor::sourceFile() const
{
    return m_sourceFile.toUrlishString();
}

void FileExtractor::extract()
{
    if (m_targetPath.isEmpty()) {
        auto uniqueText = QByteArray::number(QRandomGenerator::global()->generate(), 16);
        QString tempFileName = QDir::tempPath() + "/.qds_" + uniqueText + "_extract_" + m_archiveName + "_dir";

        m_targetPath = FilePath::fromString(tempFileName);
        m_isTempTargetPath = true;
    }

    m_targetFolder = m_targetPath.toUrlishString() + "/" + m_archiveName;

    // If the target directory already exists, remove it and its content

    QTC_ASSERT(!m_targetPath.isEmpty(), return );

    FilePath targetFilePath = FilePath::fromUserInput(m_targetFolder);
    if (targetFilePath.exists() && m_clearTargetPathContents)
        targetFilePath.removeRecursively();

    // Create a new directory to generate a proper creation date
    if (m_alwaysCreateDir)
        targetFilePath.createDir();

    m_unarchiver = makeUniqueObjectLatePtr<Unarchiver>();

    m_unarchiver->setArchive(m_sourceFile);
    m_unarchiver->setDestination(m_targetPath);

    connect(m_unarchiver.get(), &Unarchiver::progress, this, [this](const FilePath &path) {
        m_detailedText += path.toUserOutput() + "\n";
        emit detailedTextChanged();
    });

    m_timer.start();
    m_bytesBefore = QStorageInfo(m_targetPath.toFileInfo().dir()).bytesAvailable();
    m_compressedSize = QFileInfo(m_sourceFile.toUrlishString()).size();
    if (m_compressedSize <= 0)
        qWarning() << "Compressed size for file '" << m_sourceFile << "' is zero or invalid: " << m_compressedSize;

    QObject::connect(m_unarchiver.get(), &Unarchiver::done, this, [this]() {
        Result<> r = m_unarchiver->result();
        m_unarchiver.reset();
        m_finished = r.has_value();
        m_timer.stop();

        m_progress = 100;
        emit progressChanged();

        emit targetFolderExistsChanged();
        emit finishedChanged();
        QTC_CHECK(m_finished);
    });
    m_unarchiver->start();
}

void QmlDesigner::FileExtractor::removeTempTargetPath()
{
    if (m_isTempTargetPath && m_targetPath.exists()) {
        QTC_ASSERT(m_targetPath.toUrlishString().startsWith(QDir::tempPath()), qDebug() << m_targetPath;
                   return );
        m_targetPath.removeRecursively();
        m_isTempTargetPath = false;
    }
}

} // namespace QmlDesigner
