
// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "metainforeader.h"
#include "metainfo.h"

#include <designercoretr.h>
#include <invalidmetainfoexception.h>

#include <utils/algorithm.h>

#include <QString>
#include <QFileInfo>
#include <QDebug>
#include <QIcon>

namespace QmlDesigner {
namespace Internal {

enum {
    debug = false
};

const QString rootElementName = QStringLiteral("MetaInfo");
const QString typeElementName = QStringLiteral("Type");
const QString importsElementName = QStringLiteral("Imports");
const QString ItemLibraryEntryElementName = QStringLiteral("ItemLibraryEntry");
const QString HintsElementName = QStringLiteral("Hints");
const QString QmlSourceElementName = QStringLiteral("QmlSource");
const QString PropertyElementName = QStringLiteral("Property");
const QString ExtraFileElementName = QStringLiteral("ExtraFile");

MetaInfoReader::MetaInfoReader(const MetaInfo &metaInfo)
        : m_parserState(Undefined),
          m_metaInfo(metaInfo)
{
    m_overwriteDuplicates = false;
}

void MetaInfoReader::readMetaInfoFile(const QString &path, bool overwriteDuplicates)
{
    m_documentPath = path;
    m_overwriteDuplicates = overwriteDuplicates;
    m_parserState = ParsingDocument;
    if (!SimpleAbstractStreamReader::readFile(path)) {
        qWarning() << "readMetaInfoFile()" << path;
        qWarning() << errors();
        m_parserState = Error;
        throw InvalidMetaInfoException();
    }

    if (!errors().isEmpty()) {
        qWarning() << "readMetaInfoFile()" << path;
        qWarning() << errors();
        m_parserState = Error;
        throw InvalidMetaInfoException();
    }
    syncItemLibraryEntries();
}

QStringList MetaInfoReader::errors()
{
    return QmlJS::SimpleAbstractStreamReader::errors();
}

void MetaInfoReader::setQualifcation(const TypeName &qualification)
{
    m_qualication = qualification;
}

void MetaInfoReader::elementStart(const QString &name,
                                  [[maybe_unused]] const QmlJS::SourceLocation &nameLocation)
{
    switch (parserState()) {
    case ParsingDocument: setParserState(readDocument(name)); break;
    case ParsingMetaInfo: setParserState(readMetaInfoRootElement(name)); break;
    case ParsingType: setParserState(readTypeElement(name)); break;
    case ParsingItemLibrary: setParserState(readItemLibraryEntryElement(name)); break;
    case ParsingProperty: setParserState(readPropertyElement(name)); break;
    case ParsingQmlSource: setParserState(readQmlSourceElement(name)); break;
    case ParsingExtraFile: setParserState(readExtraFileElement(name)); break;
    case ParsingHints:
    case Finished:
    case Undefined: setParserState(Error);
        addError(DesignerCore::Tr::tr("Illegal state while parsing."), currentSourceLocation());
    case Error:
    default: return;
    }
}

void MetaInfoReader::elementEnd()
{
    switch (parserState()) {
    case ParsingMetaInfo: setParserState(Finished); break;
    case ParsingType: setParserState(ParsingMetaInfo); break;
    case ParsingImports: setParserState(ParsingMetaInfo); break;
    case ParsingItemLibrary: keepCurrentItemLibraryEntry(); setParserState((ParsingType)); break;
    case ParsingHints: setParserState(ParsingType); break;
    case ParsingProperty: insertProperty(); setParserState(ParsingItemLibrary);  break;
    case ParsingQmlSource: setParserState(ParsingItemLibrary); break;
    case ParsingExtraFile: setParserState(ParsingItemLibrary); break;
    case ParsingDocument:
    case Finished:
    case Undefined: setParserState(Error);
        addError(DesignerCore::Tr::tr("Illegal state while parsing."), currentSourceLocation());
    case Error:
    default: return;
    }
}

void MetaInfoReader::propertyDefinition(const QString &name,
                                        [[maybe_unused]] const QmlJS::SourceLocation &nameLocation,
                                        const QVariant &value,
                                        [[maybe_unused]] const QmlJS::SourceLocation &valueLocation)
{
    switch (parserState()) {
    case ParsingType: readTypeProperty(name, value); break;
    case ParsingImports:
        break; // not supported anymore
    case ParsingItemLibrary: readItemLibraryEntryProperty(name, value); break;
    case ParsingProperty: readPropertyProperty(name, value); break;
    case ParsingQmlSource: readQmlSourceProperty(name, value); break;
    case ParsingExtraFile: readExtraFileProperty(name, value); break;
    case ParsingMetaInfo:
        addError(DesignerCore::Tr::tr("No property definition allowed."), currentSourceLocation());
        break;
    case ParsingDocument:
    case ParsingHints: readHint(name, value); break;
    case Finished:
    case Undefined: setParserState(Error);
        addError(DesignerCore::Tr::tr("Illegal state while parsing."), currentSourceLocation());
    case Error:
    default: return;
    }
}

MetaInfoReader::ParserSate MetaInfoReader::readDocument(const QString &name)
{
    if (name == rootElementName) {
        m_currentClassName.clear();
        m_currentIcon.clear();
        return ParsingMetaInfo;
    } else {
        addErrorInvalidType(name);
        return Error;
    }
}

MetaInfoReader::ParserSate MetaInfoReader::readMetaInfoRootElement(const QString &name)
{
    if (name == typeElementName) {
        m_currentClassName.clear();
        m_currentIcon.clear();
        m_currentHints.clear();
        return ParsingType;
    } else if (name == importsElementName) {
        return ParsingImports;
    } else {
        addErrorInvalidType(name);
        return Error;
    }
}

MetaInfoReader::ParserSate MetaInfoReader::readTypeElement(const QString &name)
{
    if (name == ItemLibraryEntryElementName) {
        m_currentEntry = ItemLibraryEntry();
        m_currentEntry.setType(m_currentClassName);
        m_currentEntry.setTypeIcon(QIcon(m_currentIcon));

        m_currentEntry.addHints(m_currentHints);

        return ParsingItemLibrary;
    } else if (name == HintsElementName) {
        return ParsingHints;
    } else {
        addErrorInvalidType(name);
        return Error;
    }
}

MetaInfoReader::ParserSate MetaInfoReader::readItemLibraryEntryElement(const QString &name)
{
    if (name == QmlSourceElementName) {
        return ParsingQmlSource;
    } else if (name == PropertyElementName) {
        m_currentPropertyName = PropertyName();
        m_currentPropertyType.clear();
        m_currentPropertyValue = QVariant();
        return ParsingProperty;
    } else if (name == ExtraFileElementName) {
        return ParsingExtraFile;
    } else {
        addErrorInvalidType(name);
        return Error;
    }
}

MetaInfoReader::ParserSate MetaInfoReader::readPropertyElement(const QString &name)
{
    addErrorInvalidType(name);
    return Error;
}

MetaInfoReader::ParserSate MetaInfoReader::readQmlSourceElement(const QString &name)
{
    addErrorInvalidType(name);
    return Error;
}

MetaInfoReader::ParserSate MetaInfoReader::readExtraFileElement(const QString &name)
{
    addErrorInvalidType(name);
    return Error;
}

void MetaInfoReader::readTypeProperty(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("name")) {
        m_currentClassName = value.toString().toUtf8();
        if (!m_qualication.isEmpty()) //prepend qualification
            m_currentClassName = m_qualication + "." + m_currentClassName;
    } else if (name == QStringLiteral("icon")) {
        m_currentIcon = absoluteFilePathForDocument(value.toString());
    } else {
        //: do not translate "Type"
        addError(DesignerCore::Tr::tr("Unknown property for Type \"%1.\".").arg(name),
                 currentSourceLocation());
        setParserState(Error);
    }
}

void MetaInfoReader::readItemLibraryEntryProperty(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("name")) {
        m_currentEntry.setName(value.toString());
    } else if (name == QStringLiteral("category")) {
        m_currentEntry.setCategory(value.toString());
    } else if (name == QStringLiteral("libraryIcon")) {
        m_currentEntry.setLibraryEntryIconPath(absoluteFilePathForDocument(value.toString()));
    } else if (name == QStringLiteral("version")) {
        setVersion(value.toString());
    } else if (name == QStringLiteral("requiredImport")) {
        m_currentEntry.setRequiredImport(value.toString());
    } else if (name == QStringLiteral("toolTip")) {
        m_currentEntry.setToolTip(value.toString());
    } else {
        addError(DesignerCore::Tr::tr("Unknown property for ItemLibraryEntry \"%1.\".").arg(name),
                 currentSourceLocation());
        setParserState(Error);
    }
}

inline QString deEscape(const QString &value)
{
    QString result = value;

    result.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    result.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));

    return result;
}

inline QVariant deEscapeVariant(const QVariant &value)
{
    if (value.typeId() == QMetaType::QString)
        return deEscape(value.toString());
    return value;
}
void MetaInfoReader::readPropertyProperty(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("name")) {
        m_currentPropertyName = value.toByteArray();
    } else if (name == QStringLiteral("type")) {
        m_currentPropertyType = value.toString();
    } else if (name == QStringLiteral("value")) {
        m_currentPropertyValue = deEscapeVariant(value);
    } else {
        //: do not translate "Property"
        addError(DesignerCore::Tr::tr("Unknown property for Property \"%1.\".").arg(name),
                 currentSourceLocation());
        setParserState(Error);
    }
}

void MetaInfoReader::readQmlSourceProperty(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("source")) {
        m_currentEntry.setTemplatePath(absoluteFilePathForDocument(value.toString()));
    } else {
        addError(DesignerCore::Tr::tr("Unknown property for QmlSource \"%1.\".").arg(name),
                 currentSourceLocation());
        setParserState(Error);
    }
}

void MetaInfoReader::readExtraFileProperty(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("source")) {
        m_currentEntry.addExtraFilePath(absoluteFilePathForDocument(value.toString()));
    } else {
        addError(DesignerCore::Tr::tr("Unknown property for ExtraFile \"%1.\".").arg(name),
                 currentSourceLocation());
        setParserState(Error);
    }
}

void MetaInfoReader::readHint(const QString &name, const QVariant &value)
{
    m_currentHints.insert(name, value.toString());
}

void MetaInfoReader::setVersion(const QString &versionNumber)
{
    const TypeName typeName = m_currentEntry.typeName();
    int major = 1;
    int minor = 0;

    if (!versionNumber.isEmpty()) {
        int val;
        bool ok;
        if (versionNumber.contains(QLatin1Char('.'))) {
            val = versionNumber.split(QLatin1Char('.')).constFirst().toInt(&ok);
            major = ok ? val : major;
            val = versionNumber.split(QLatin1Char('.')).constLast().toInt(&ok);
            minor = ok ? val : minor;
        } else {
            val = versionNumber.toInt(&ok);
            major = ok ? val : major;
        }
    }
    m_currentEntry.setType(typeName, major, minor);
}

MetaInfoReader::ParserSate MetaInfoReader::parserState() const
{
    return m_parserState;
}

void MetaInfoReader::setParserState(ParserSate newParserState)
{
    m_parserState = newParserState;
}

void MetaInfoReader::syncItemLibraryEntries()
{
    try {
        m_metaInfo.itemLibraryInfo()->addEntries(m_bufferedEntries, m_overwriteDuplicates);
    } catch (const InvalidMetaInfoException &) {
        addError(DesignerCore::Tr::tr("Invalid or duplicate library entry \"%1.\".")
                     .arg(m_currentEntry.name()),
                 currentSourceLocation());
    }
    m_bufferedEntries.clear();
}

void MetaInfoReader::keepCurrentItemLibraryEntry()
{
    m_bufferedEntries.append(m_currentEntry);
}

void MetaInfoReader::insertProperty()
{
    m_currentEntry.addProperty(m_currentPropertyName, m_currentPropertyType, m_currentPropertyValue);
}

void MetaInfoReader::addErrorInvalidType(const QString &typeName)
{
    addError(DesignerCore::Tr::tr("Invalid type \"%1.\".").arg(typeName), currentSourceLocation());
}

QString MetaInfoReader::absoluteFilePathForDocument(const QString &relativeFilePath)
{
    QFileInfo fileInfo(relativeFilePath);
    if (!fileInfo.isAbsolute() && !fileInfo.exists())
        fileInfo.setFile(QFileInfo(m_documentPath).absolutePath() + QStringLiteral("/") + relativeFilePath);
    if (fileInfo.exists())
        return fileInfo.absoluteFilePath();

    qWarning() << relativeFilePath << "does not exist";
    return relativeFilePath;
}

} //Internal
} //QmlDesigner
