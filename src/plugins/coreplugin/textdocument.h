// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "idocument.h"

#include <utils/textfileformat.h>

namespace Core {

namespace Internal { class TextDocumentPrivate; }

class CORE_EXPORT BaseTextDocument : public IDocument
{
    Q_OBJECT

public:
    using ReadResult = Utils::TextFileFormat::ReadResult;

    explicit BaseTextDocument(QObject *parent = nullptr);
    ~BaseTextDocument() override;

    Utils::TextFileFormat format() const;
    const QTextCodec *codec() const;
    QByteArray codecName() const;
    void setCodec(const QTextCodec *);
    void setCodec(const QByteArray &name);
    virtual bool supportsCodec(const QByteArray &) const;
    void switchUtf8Bom();
    bool supportsUtf8Bom() const;
    Utils::TextFileFormat::LineTerminationMode lineTerminationMode() const;

    ReadResult read(const Utils::FilePath &filePath, QStringList *plainTextList);
    ReadResult read(const Utils::FilePath &filePath, QString *plainText);

    bool hasDecodingError() const;
    QByteArray decodingErrorSample() const;

    Utils::Result<> write(const Utils::FilePath &filePath, const QString &data) const;
    Utils::Result<> write(const Utils::FilePath &filePath, const Utils::TextFileFormat &format, const QString &data) const;

    void setSupportsUtf8Bom(bool value);
    void setLineTerminationMode(Utils::TextFileFormat::LineTerminationMode mode);

    static bool isUtf8Codec(const QByteArray &name);

private:
    Internal::TextDocumentPrivate *d;
};

} // namespace Core
