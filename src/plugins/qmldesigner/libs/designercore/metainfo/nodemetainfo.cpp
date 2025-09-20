// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nodemetainfo.h"
#include "model.h"
#include "model/model_p.h"

#include "metainfo.h"
#include <enumeration.h>
#include <projectstorage/projectstorage.h>
#include <propertyparser.h>
#include <rewriterview.h>

#include <QDebug>
#include <QDir>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include <languageutils/fakemetaobject.h>
#include <qmljs/parser/qmljsast_p.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljs/qmljsscopechain.h>
#include <qmljs/qmljsvalueowner.h>

#include <utils/qtcassert.h>
#include <utils/algorithm.h>

// remove that if the old code model is removed
QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Wdeprecated-declarations")
QT_WARNING_DISABLE_CLANG("-Wdeprecated-declarations")
QT_WARNING_DISABLE_MSVC(4996)

namespace QmlDesigner {

namespace {

using Storage::ModuleKind;

auto category = ModelTracing::category;

struct TypeDescription
{
    QString className;
    int minorVersion{};
    int majorVersion{};
};

using namespace QmlJS;

using PropertyInfo = QPair<PropertyName, TypeName>;

QVector<PropertyInfo> getObjectTypes(const ObjectValue *ov, const ContextPtr &context, bool local = false, int rec = 0);


static QByteArray getUnqualifiedName(const QByteArray &name)
{
    const QList<QByteArray> nameComponents = name.split('.');
    if (nameComponents.size() < 2)
        return name;
    return nameComponents.constLast();
}

static TypeName resolveTypeName(const ASTPropertyReference *ref, const ContextPtr &context, QVector<PropertyInfo> &dotProperties)
{
    TypeName type = "unknown";

    if (ref->ast()->propertyToken().isValid()) {
        type = ref->ast()->memberType->name.toUtf8();

        const Value *value = context->lookupReference(ref);

        if (!value)
            return type;

        if (const CppComponentValue * componentObjectValue = value->asCppComponentValue()) {
            type = componentObjectValue->className().toUtf8();
            dotProperties = getObjectTypes(componentObjectValue, context);
        }  else if (const ObjectValue * objectValue = value->asObjectValue()) {
            dotProperties = getObjectTypes(objectValue, context);
        }

        if (type == "alias") {

            if (const ASTObjectValue * astObjectValue = value->asAstObjectValue()) {
                if (astObjectValue->typeName()) {
                    type = astObjectValue->typeName()->name.toUtf8();
                    const ObjectValue *objectValue = context->lookupType(astObjectValue->document(), astObjectValue->typeName());
                    if (objectValue)
                        dotProperties = getObjectTypes(objectValue, context);
                }
            } else if (const ObjectValue * objectValue = value->asObjectValue()) {
                type = objectValue->className().toUtf8();
                dotProperties = getObjectTypes(objectValue, context);
            } else if (value->asColorValue()) {
                type = "color";
            } else if (value->asUrlValue()) {
                type = "url";
            } else if (value->asStringValue()) {
                type = "string";
            } else if (value->asRealValue()) {
                type = "real";
            } else if (value->asIntValue()) {
                type = "int";
            } else if (value->asBooleanValue()) {
                type = "boolean";
            }
        }
    }

    return type;
}

static QString qualifiedTypeNameForContext(const ObjectValue *objectValue,
                                           const ViewerContext &vContext,
                                           const ImportDependencies &dep)
{
    QString cppName;
    QStringList packages;
    if (const CppComponentValue *cppComponent = value_cast<CppComponentValue>(objectValue)) {
        QString className = cppComponent->className();
        const QList<LanguageUtils::FakeMetaObject::Export> &exports = cppComponent->metaObject()->exports();
        for (const LanguageUtils::FakeMetaObject::Export &e : exports) {
            if (e.type == className)
                packages << e.package;
            if (e.package == CppQmlTypes::cppPackage)
                cppName = e.type;
        }
        if (packages.size() == 1 && packages.at(0) == CppQmlTypes::cppPackage)
            return packages.at(0) + QLatin1Char('.') + className;
    }
    // try to recover a "global context name"
    QStringList possibleLibraries;
    QStringList possibleQrcFiles;
    QStringList possibleFiles;
    bool hasQtQuick = false;
    do {
        if (objectValue->originId().isEmpty())
            break;
        CoreImport cImport = dep.coreImport(objectValue->originId());
        if (!cImport.valid())
            break;
        for (const Export &e : std::as_const(cImport.possibleExports)) {
            if (e.pathRequired.isEmpty() || vContext.paths.count(e.pathRequired) > 0) {
                switch (e.exportName.type) {
                case ImportType::Library:
                {
                    QString typeName = objectValue->className();
                    if (!e.typeName.isEmpty() && e.typeName != Export::libraryTypeName()) {
                        typeName = e.typeName;
                        if (typeName != objectValue->className())
                            qCWarning(qmljsLog) << "Outdated classname " << objectValue->className()
                                                << " vs " << typeName
                                                << " for " << e.exportName.toString();
                    }
                    if (packages.isEmpty() || packages.contains(e.exportName.libraryQualifiedPath())) {
                        if (e.exportName.splitPath.value(0) == QLatin1String("QtQuick"))
                            hasQtQuick = true;
                        possibleLibraries.append(e.exportName.libraryQualifiedPath() + '.' + typeName);
                    }
                    break;
                }
                case ImportType::File:
                {
                    // remove the search path prefix.
                    // this means that the same relative path wrt. different import paths will clash
                    QString filePath = e.exportName.path();
                    for (const Utils::FilePath &path : std::as_const(vContext.paths)) {
                        if (filePath.startsWith(path.path()) && filePath.size() > path.path().size()
                            && filePath.at(path.path().size()) == QLatin1Char('/')) {
                            filePath = filePath.mid(path.path().size() + 1);
                            break;
                        }
                    }

                    if (filePath.startsWith(QLatin1Char('/')))
                        filePath = filePath.mid(1);
                    QFileInfo fileInfo(filePath);
                    QStringList splitName = fileInfo.path().split(QLatin1Char('/'));
                    QString typeName = fileInfo.baseName();
                    if (!e.typeName.isEmpty()) {
                        if (e.typeName != fileInfo.baseName())
                            qCWarning(qmljsLog) << "type renaming in file import " << e.typeName
                                                << " for " << e.exportName.path();
                        typeName = e.typeName;
                    }
                    if (typeName != objectValue->className())
                        qCWarning(qmljsLog) << "Outdated classname " << objectValue->className()
                                            << " vs " << typeName
                                            << " for " << e.exportName.toString();
                    splitName.append(typeName);
                    possibleFiles.append(splitName.join(QLatin1Char('.')));
                    break;
                }
                case ImportType::QrcFile:
                {
                    QString filePath = e.exportName.path();
                    if (filePath.startsWith(QLatin1Char('/')))
                        filePath = filePath.mid(1);
                    QFileInfo fileInfo(filePath);
                    QStringList splitName = fileInfo.path().split(QLatin1Char('/'));
                    QString typeName = fileInfo.baseName();
                    if (!e.typeName.isEmpty()) {
                        if (e.typeName != fileInfo.baseName())
                            qCWarning(qmljsLog) << "type renaming in file import " << e.typeName
                                                << " for " << e.exportName.path();
                        typeName = e.typeName;
                    }
                    if (typeName != objectValue->className())
                        qCWarning(qmljsLog) << "Outdated classname " << objectValue->className()
                                            << " vs " << typeName
                                            << " for " << e.exportName.toString();
                    splitName.append(typeName);
                    possibleQrcFiles.append(splitName.join(QLatin1Char('.')));
                    break;
                }
                case ImportType::Invalid:
                case ImportType::UnknownFile:
                    break;
                case ImportType::Directory:
                case ImportType::ImplicitDirectory:
                case ImportType::QrcDirectory:
                    qCWarning(qmljsLog) << "unexpected import type in export "
                                        << e.exportName.toString() << " of coreExport "
                                        << objectValue->originId();
                    break;
                }
            }
        }
        auto optimalName = [] (const QStringList &list) -> QString {
            QString res = list.at(0);
            for (int i = 1; i < list.size(); ++i) {
                const QString &nameNow = list.at(i);
                if (nameNow.size() < res.size()
                        || (nameNow.size() == res.size() && nameNow < res))
                    res = nameNow;
            }
            return res;
        };
        if (!possibleLibraries.isEmpty()) {
            if (hasQtQuick) {
                for (const QString &libImport : std::as_const(possibleLibraries))
                    if (!libImport.startsWith(QLatin1String("QtQuick")))
                        possibleLibraries.removeAll(libImport);
            }
            return optimalName(possibleLibraries);
        }
        if (!possibleQrcFiles.isEmpty())
            return optimalName(possibleQrcFiles);
        if (!possibleFiles.isEmpty())
            return optimalName(possibleFiles);
    } while (false);
    if (!cppName.isEmpty())
        return CppQmlTypes::cppPackage + QLatin1Char('.') + cppName;
    if (const CppComponentValue *cppComponent = value_cast<CppComponentValue>(objectValue)) {
        if (cppComponent->moduleName().isEmpty())
            return cppComponent->className();
        else
            return cppComponent->moduleName() + QLatin1Char('.') + cppComponent->className();
    } else {
        return objectValue->className();
    }
}

class PropertyMemberProcessor : public MemberProcessor
{
public:
    PropertyMemberProcessor(const ContextPtr &context) : m_context(context)
    {}
    bool processProperty(const QString &name, const Value *value, const QmlJS::PropertyInfo &) override
    {
        PropertyName propertyName = name.toUtf8();
        const ASTPropertyReference *ref = value_cast<ASTPropertyReference>(value);
        if (ref) {
            QVector<PropertyInfo> dotProperties;
            const TypeName type = resolveTypeName(ref, m_context, dotProperties);
            m_properties.append({propertyName, type});
            if (!dotProperties.isEmpty()) {
                for (const PropertyInfo &propertyInfo : std::as_const(dotProperties)) {
                    PropertyName dotName = propertyInfo.first;
                    TypeName type = propertyInfo.second;
                    dotName = propertyName + '.' + dotName;
                    m_properties.append({dotName, type});
                }
            }
        } else {
            if (const CppComponentValue * cppComponentValue = value_cast<CppComponentValue>(value)) {
                TypeName qualifiedTypeName = qualifiedTypeNameForContext(cppComponentValue,
                    m_context->viewerContext(), *m_context->snapshot().importDependencies()).toUtf8();
                m_properties.append({propertyName, qualifiedTypeName});
            } else {
                QmlJS::TypeId typeId;
                TypeName typeName = typeId(value).toUtf8();

                if (typeName == "Function")
                    return processSlot(name, value);

                if (typeName == "number")
                    typeName = value->asIntValue() ? "int" : "real";

                m_properties.append({propertyName, typeName});
            }
        }
        return true;
    }

    bool processSignal(const QString &name, const Value * /*value*/) override
    {
        m_signals.append(name.toUtf8());
        return true;
    }

    bool processSlot(const QString &name, const Value * /*value*/) override
    {
        m_slots.append(name.toUtf8());
        return true;
    }

    QVector<PropertyInfo> properties() const { return m_properties; }

    PropertyNameList signalList() const { return m_signals; }
    PropertyNameList slotList() const { return m_slots; }

private:
    QVector<PropertyInfo> m_properties;
    PropertyNameList m_signals;
    PropertyNameList m_slots;
    const ContextPtr m_context;
};

inline static bool isValueType(const TypeName &type)
{
    static const PropertyTypeList objectValuesList({"QFont",
                                                    "QPoint",
                                                    "QPointF",
                                                    "QSize",
                                                    "QSizeF",
                                                    "QRect",
                                                    "QRectF",
                                                    "QVector2D",
                                                    "QVector3D",
                                                    "QVector4D",
                                                    "vector2d",
                                                    "vector3d",
                                                    "vector4d",
                                                    "font",
                                                    "QQuickIcon"});
    return objectValuesList.contains(type);
}

inline static bool isValueType(const QString &type)
{
    static const QStringList objectValuesList({"QFont",
                                               "QPoint",
                                               "QPointF",
                                               "QSize",
                                               "QSizeF",
                                               "QRect",
                                               "QRectF",
                                               "QVector2D",
                                               "QVector3D",
                                               "QVector4D",
                                               "vector2d",
                                               "vector3d",
                                               "vector4d",
                                               "font",
                                               "QQuickIcon"});
    return objectValuesList.contains(type);
}

const CppComponentValue *findQmlPrototype(const ObjectValue *ov, const ContextPtr &context)
{
    if (!ov)
        return nullptr;

    const CppComponentValue * qmlValue = value_cast<CppComponentValue>(ov);
    if (qmlValue)
        return qmlValue;

    return findQmlPrototype(ov->prototype(context), context);
}

QVector<PropertyInfo> getQmlTypes(const CppComponentValue *objectValue, const ContextPtr &context, bool local = false, int rec = 0);

QVector<PropertyInfo> getTypes(const ObjectValue *objectValue, const ContextPtr &context, bool local = false, int rec = 0)
{
    const CppComponentValue * qmlObjectValue = value_cast<CppComponentValue>(objectValue);

    if (qmlObjectValue)
        return getQmlTypes(qmlObjectValue, context, local, rec);

    return getObjectTypes(objectValue, context, local, rec);
}

QVector<PropertyInfo> getQmlTypes(const CppComponentValue *objectValue, const ContextPtr &context, bool local, int rec)
{
    QVector<PropertyInfo> propertyList;

    if (!objectValue)
        return propertyList;
    if (objectValue->className().isEmpty())
        return propertyList;

    if (rec > 4)
        return propertyList;

    PropertyMemberProcessor processor(context);
    objectValue->processMembers(&processor);

    for (const PropertyInfo &property : processor.properties()) {
        const PropertyName name = property.first;
        const QString nameAsString = QString::fromUtf8(name);
        if (!objectValue->isWritable(nameAsString) && objectValue->isPointer(nameAsString)) {
            //dot property
            const CppComponentValue * qmlValue = value_cast<CppComponentValue>(objectValue->lookupMember(nameAsString, context));
            if (qmlValue) {
                const QVector<PropertyInfo> dotProperties = getQmlTypes(qmlValue, context, false, rec + 1);
                for (const PropertyInfo &propertyInfo : dotProperties) {
                    const PropertyName dotName = name + '.' + propertyInfo.first;
                    const TypeName type = propertyInfo.second;
                    propertyList.append({dotName, type});
                }
            }
        }
        if (isValueType(objectValue->propertyType(nameAsString))) {
            const ObjectValue *dotObjectValue = value_cast<ObjectValue>(
                objectValue->lookupMember(nameAsString, context));

            if (dotObjectValue) {
                const QVector<PropertyInfo> dotProperties = getObjectTypes(dotObjectValue,
                                                                           context,
                                                                           false,
                                                                           rec + 1);
                for (const PropertyInfo &propertyInfo : dotProperties) {
                    const PropertyName dotName = name + '.' + propertyInfo.first;
                    const TypeName type = propertyInfo.second;
                    propertyList.append({dotName, type});
                }
            }
        }
        TypeName type = property.second;
        if (!objectValue->isPointer(nameAsString) && !objectValue->isListProperty(nameAsString))
            type = objectValue->propertyType(nameAsString).toUtf8();

        if (type == "unknown" && objectValue->hasProperty(nameAsString))
            type = objectValue->propertyType(nameAsString).toUtf8();

        propertyList.append({name, type});
    }

    if (!local)
        propertyList.append(getTypes(objectValue->prototype(context), context, local, rec));

    return propertyList;
}

PropertyNameList getSignals(const ObjectValue *objectValue, const ContextPtr &context, bool local = false)
{
    PropertyNameList signalList;

    if (!objectValue)
        return signalList;
    if (objectValue->className().isEmpty())
        return signalList;

    PropertyMemberProcessor processor(context);
    objectValue->processMembers(&processor);

    signalList.append(processor.signalList());

    PrototypeIterator prototypeIterator(objectValue, context);
    QList<const ObjectValue *> objects = prototypeIterator.all();

    if (!local) {
        for (const ObjectValue *prototype : objects)
            signalList.append(getSignals(prototype, context, true));
    }

    std::sort(signalList.begin(), signalList.end());
    signalList.erase(std::unique(signalList.begin(), signalList.end()), signalList.end());

    return signalList;
}

PropertyNameList getSlots(const ObjectValue *objectValue, const ContextPtr &context, bool local = false)
{
    PropertyNameList slotList;

    if (!objectValue)
        return slotList;
    if (objectValue->className().isEmpty())
        return slotList;

    PropertyMemberProcessor processor(context);
    objectValue->processMembers(&processor);

    if (const ASTObjectValue *astObjectValue = objectValue->asAstObjectValue())
        astObjectValue->processMembers(&processor);

    slotList.append(processor.slotList());

    PrototypeIterator prototypeIterator(objectValue, context);
    const QList<const ObjectValue *> objects = prototypeIterator.all();

    if (!local) {
        for (const ObjectValue *prototype : objects)
            slotList.append(getSlots(prototype, context, true));
    }

    std::sort(slotList.begin(), slotList.end());
    slotList.erase(std::unique(slotList.begin(), slotList.end()), slotList.end());

    return slotList;
}

QVector<PropertyInfo> getObjectTypes(const ObjectValue *objectValue, const ContextPtr &context, bool local, int rec)
{
    QVector<PropertyInfo> propertyList;

    if (!objectValue)
        return propertyList;
    if (objectValue->className().isEmpty())
        return propertyList;

    if (rec > 4)
        return propertyList;

    PropertyMemberProcessor processor(context);
    objectValue->processMembers(&processor);

    const auto props = processor.properties();

    for (const PropertyInfo &property : props) {
        const PropertyName name = property.first;
        const QString nameAsString = QString::fromUtf8(name);

        if (isValueType(property.second)) {
            const Value *dotValue = objectValue->lookupMember(nameAsString, context);

            if (!dotValue)
                continue;

            if (const Reference *ref = dotValue->asReference())
                dotValue = context->lookupReference(ref);

            if (const ObjectValue *dotObjectValue = dotValue->asObjectValue()) {
                const QVector<PropertyInfo> dotProperties = getObjectTypes(dotObjectValue,
                                                                           context,
                                                                           false,
                                                                           rec + 1);
                for (const PropertyInfo &propertyInfo : dotProperties) {
                    const PropertyName dotName = name + '.' + propertyInfo.first;
                    const TypeName type = propertyInfo.second;
                    propertyList.append({dotName, type});
                }
            }
        }
        propertyList.append(property);
    }

    if (!local) {
        const ObjectValue* prototype = objectValue->prototype(context);
        // TODO: can we move this to getType methode and use that one here then
        if (prototype == objectValue)
            return propertyList;

        const CppComponentValue * qmlObjectValue = value_cast<CppComponentValue>(prototype);

        if (qmlObjectValue)
            propertyList.append(getQmlTypes(qmlObjectValue, context, local, rec + 1));
        else
            propertyList.append(getObjectTypes(prototype, context, local, rec + 1));
    }

    return propertyList;
}
} // namespace

class NodeMetaInfoPrivate
{
public:
    using Pointer = std::shared_ptr<NodeMetaInfoPrivate>;
    NodeMetaInfoPrivate() = delete;
    NodeMetaInfoPrivate(Model *model, TypeName type, int maj = -1, int min = -1);
    NodeMetaInfoPrivate(const NodeMetaInfoPrivate &) = delete;
    NodeMetaInfoPrivate &operator=(const NodeMetaInfoPrivate &) = delete;
    ~NodeMetaInfoPrivate() = default;

    bool isValid() const;
    bool isFileComponent() const;
    const PropertyNameList &properties() const;
    const PropertyNameList &localProperties() const;
    PropertyNameList signalNames() const;
    PropertyNameList slotNames() const;
    PropertyName defaultPropertyName() const;
    const TypeName &propertyType(const PropertyName &propertyName) const;

    void setupPrototypes();
#ifndef QDS_USE_PROJECTSTORAGE
    QList<TypeDescription> prototypes() const;
#endif
    bool isPropertyWritable(const PropertyName &propertyName) const;
    bool isPropertyPointer(const PropertyName &propertyName) const;
    bool isPropertyList(const PropertyName &propertyName) const;
    bool isPropertyEnum(const PropertyName &propertyName) const;
    QStringList keysForEnum(const QString &enumName) const;
    bool cleverCheckType(const TypeName &otherType) const;
    QMetaType::Type variantTypeId(const PropertyName &properyName) const;

    int majorVersion() const;
    int minorVersion() const;
    const TypeName &qualfiedTypeName() const;
    Model *model() const;

    QByteArray cppPackageName() const;

    QString componentFileName() const;
    QString importDirectoryPath() const;
    Import requiredImport() const;

    static std::shared_ptr<NodeMetaInfoPrivate> create(Model *model,
                                                       const TypeName &type,
                                                       int maj = -1,
                                                       int min = -1);

    QSet<QByteArray> &prototypeCachePositives();
    QSet<QByteArray> &prototypeCacheNegatives();

private:

    const CppComponentValue *getCppComponentValue() const;
    const ObjectValue *getObjectValue() const;
    void setupPropertyInfo(const QVector<PropertyInfo> &propertyInfos);
    void setupLocalPropertyInfo(const QVector<PropertyInfo> &propertyInfos);
    QString lookupName() const;
    QStringList lookupNameComponent() const;
    const CppComponentValue *getNearestCppComponentValue() const;
    QString fullQualifiedImportAliasType() const;

    void ensureProperties() const;
    void initialiseProperties();

    TypeName m_qualfiedTypeName;
    int m_majorVersion = -1;
    int m_minorVersion = -1;
    bool m_isValid = false;
    bool m_isFileComponent = false;
    PropertyNameList m_properties;
    PropertyNameList m_signals;
    PropertyNameList m_slots;
    QList<TypeName> m_propertyTypes;
    PropertyNameList m_localProperties;
    PropertyName m_defaultPropertyName;
    QList<TypeDescription> m_prototypes;
    QSet<QByteArray> m_prototypeCachePositives;
    QSet<QByteArray> m_prototypeCacheNegatives;

    //storing the pointer would not be save
    ContextPtr context() const;
    const Document *document() const;

    QPointer<Model> m_model;
    const ObjectValue *m_objectValue = nullptr;
    bool m_propertiesSetup = false;
};

bool NodeMetaInfoPrivate::isFileComponent() const
{
    return m_isFileComponent;
}

const PropertyNameList &NodeMetaInfoPrivate::properties() const
{
    ensureProperties();

    return m_properties;
}

const PropertyNameList &NodeMetaInfoPrivate::localProperties() const
{
    ensureProperties();

    return m_localProperties;
}

PropertyNameList NodeMetaInfoPrivate::signalNames() const
{
    ensureProperties();
    return m_signals;
}

PropertyNameList NodeMetaInfoPrivate::slotNames() const
{
    ensureProperties();
    return m_slots;
}

QSet<QByteArray> &NodeMetaInfoPrivate::prototypeCachePositives()
{
    return m_prototypeCachePositives;
}

QSet<QByteArray> &NodeMetaInfoPrivate::prototypeCacheNegatives()
{
    return m_prototypeCacheNegatives;
}

PropertyName NodeMetaInfoPrivate::defaultPropertyName() const
{
    if (!m_defaultPropertyName.isEmpty())
        return m_defaultPropertyName;
    return PropertyName("data");
}

static TypeName stringIdentifier(const TypeName &type, int maj, int min)
{
    return type + QByteArray::number(maj) + '_' + QByteArray::number(min);
}

std::shared_ptr<NodeMetaInfoPrivate> NodeMetaInfoPrivate::create(Model *model,
                                                                 const TypeName &type,
                                                                 int major,
                                                                 int minor)
{
    auto stringfiedType = stringIdentifier(type, major, minor);
    auto &cache = model->d->nodeMetaInfoCache();
    if (auto found = cache.find(stringfiedType); found != cache.end())
        return *found;

    auto newData = std::make_shared<NodeMetaInfoPrivate>(model, type, major, minor);

    if (!newData->isValid())
        return newData;

    auto stringfiedQualifiedType = stringIdentifier(newData->qualfiedTypeName(),
                                                    newData->majorVersion(),
                                                    newData->minorVersion());

    if (auto found = cache.find(stringfiedQualifiedType); found != cache.end()) {
        newData = *found;
        cache.insert(stringfiedType, newData);
        return newData;
    }

    if (stringfiedQualifiedType != stringfiedType)
        cache.insert(stringfiedQualifiedType, newData);

    cache.insert(stringfiedType, newData);

    return newData;
}

NodeMetaInfoPrivate::NodeMetaInfoPrivate(Model *model, TypeName type, int maj, int min)
    : m_qualfiedTypeName(type)
    , m_majorVersion(maj)
    , m_minorVersion(min)
    , m_model(model)
{
    if (context()) {
        const CppComponentValue *cppObjectValue = getCppComponentValue();

        if (cppObjectValue) {
            if (m_majorVersion == -1 && m_minorVersion == -1) {
                m_majorVersion = cppObjectValue->componentVersion().majorVersion();
                m_minorVersion = cppObjectValue->componentVersion().minorVersion();
            }
            m_objectValue = cppObjectValue;
            m_defaultPropertyName = cppObjectValue->defaultPropertyName().toUtf8();
            m_isValid = true;
            setupPrototypes();
        } else {
            const ObjectValue *objectValue = getObjectValue();
            if (objectValue) {
                const CppComponentValue *qmlValue = value_cast<CppComponentValue>(objectValue);

                if (qmlValue) {
                    if (m_majorVersion == -1 && m_minorVersion == -1) {
                        m_majorVersion = qmlValue->componentVersion().majorVersion();
                        m_minorVersion = qmlValue->componentVersion().minorVersion();
                        m_qualfiedTypeName = qmlValue->moduleName().toUtf8() + '.'
                                             + qmlValue->className().toUtf8();

                    } else if (m_majorVersion == qmlValue->componentVersion().majorVersion()
                               && m_minorVersion == qmlValue->componentVersion().minorVersion()) {
                        m_qualfiedTypeName = qmlValue->moduleName().toUtf8() + '.' + qmlValue->className().toUtf8();
                    } else {
                        return;
                    }
                } else {
                    m_isFileComponent = true;
                    const auto *imports = context()->imports(document());
                    const ImportInfo importInfo = imports->info(lookupNameComponent().constLast(),
                                                                context().data());

                    if (importInfo.isValid()) {
                        if (importInfo.type() == ImportType::Library) {
                            m_majorVersion = importInfo.version().majorVersion();
                            m_minorVersion = importInfo.version().minorVersion();
                        }
                        bool prepandName = (importInfo.type() == ImportType::Library
                                            || importInfo.type() == ImportType::Directory)
                                           && !m_qualfiedTypeName.contains('.');
                        if (prepandName)
                            m_qualfiedTypeName.prepend(importInfo.name().toUtf8() + '.');
                    }
                }
                m_objectValue = objectValue;
                m_defaultPropertyName = context()->defaultPropertyName(objectValue).toUtf8();
                m_isValid = true;
                setupPrototypes();
            } else {
                // Special case for aliased types for the rewriter

                const auto *imports = context()->imports(document());
                const ImportInfo importInfo = imports->info(QString::fromUtf8(m_qualfiedTypeName),
                                                            context().data());
                if (importInfo.isValid()) {
                    if (importInfo.type() == ImportType::Library) {
                        m_majorVersion = importInfo.version().majorVersion();
                        m_minorVersion = importInfo.version().minorVersion();
                    } else {
                        m_isFileComponent = true;
                    }

                    m_qualfiedTypeName = getUnqualifiedName(m_qualfiedTypeName);

                    bool prepandName = (importInfo.type() == ImportType::Library
                                        || importInfo.type() == ImportType::Directory);
                    if (prepandName)
                        m_qualfiedTypeName.prepend(importInfo.name().toUtf8() + '.');

                    m_qualfiedTypeName.replace("/", ".");
                }

                m_objectValue = getObjectValue();
                m_defaultPropertyName = context()->defaultPropertyName(objectValue).toUtf8();
                m_isValid = true;
                setupPrototypes();
            }
        }
    }
}

const CppComponentValue *NodeMetaInfoPrivate::getCppComponentValue() const
{
    const QList<TypeName> nameComponents = m_qualfiedTypeName.split('.');
    if (nameComponents.size() < 2)
        return nullptr;
    const TypeName &type = nameComponents.constLast();

    TypeName module;
    for (int i = 0; i < nameComponents.size() - 1; ++i) {
        if (i != 0)
            module += '/';
        module += nameComponents.at(i);
    }

    // get the qml object value that's available in the document
    const QmlJS::Imports *importsPtr = context()->imports(document());
    if (importsPtr) {
        const QList<QmlJS::Import> &imports = importsPtr->all();
        for (const QmlJS::Import &import : imports) {
            if (import.info.path() != QString::fromUtf8(module))
                continue;
            const Value *lookupResult = import.object->lookupMember(QString::fromUtf8(type), context());
            const CppComponentValue *cppValue = value_cast<CppComponentValue>(lookupResult);
            if (cppValue
                    && (m_majorVersion == -1 || m_majorVersion == cppValue->componentVersion().majorVersion())
                    && (m_minorVersion == -1 || m_minorVersion == cppValue->componentVersion().minorVersion())
                    )
                return cppValue;
        }
    }

    const CppComponentValue *value = value_cast<CppComponentValue>(getObjectValue());
    if (value)
        return value;

    // maybe 'type' is a cpp name
    const CppComponentValue *cppValue = context()->valueOwner()->cppQmlTypes().objectByCppName(QString::fromUtf8(type));

    if (cppValue) {
        const QList<LanguageUtils::FakeMetaObject::Export> exports = cppValue->metaObject()->exports();
        for (const LanguageUtils::FakeMetaObject::Export &exportValue : exports) {
            if (exportValue.package.toUtf8() != "<cpp>") {
                const QList<QmlJS::Import> imports = context()->imports(document())->all();
                for (const QmlJS::Import &import : imports) {
                    if (import.info.path() != exportValue.package)
                        continue;
                    const Value *lookupResult = import.object->lookupMember(exportValue.type, context());
                    const CppComponentValue *cppValue = value_cast<CppComponentValue>(lookupResult);
                    if (cppValue)
                        return cppValue;
                }
            }
        }
    }

    return cppValue;
}

const ObjectValue *NodeMetaInfoPrivate::getObjectValue() const
{
    return context()->lookupType(document(), lookupNameComponent());
}

ContextPtr NodeMetaInfoPrivate::context() const
{
#ifndef QDS_USE_PROJECTSTORAGE
    if (m_model && m_model->rewriterView() && m_model->rewriterView()->scopeChain())
        return m_model->rewriterView()->scopeChain()->context();
#endif

    return ContextPtr(nullptr);
}

const Document *NodeMetaInfoPrivate::document() const
{
    if (m_model && m_model->rewriterView())
        return m_model->rewriterView()->document().data();
    return nullptr;
}

void NodeMetaInfoPrivate::setupLocalPropertyInfo(const QVector<PropertyInfo> &localPropertyInfos)
{
    for (const PropertyInfo &propertyInfo : localPropertyInfos) {
        m_localProperties.append(propertyInfo.first);
    }
}

void NodeMetaInfoPrivate::setupPropertyInfo(const QVector<PropertyInfo> &propertyInfos)
{
    for (const PropertyInfo &propertyInfo : propertyInfos) {
        if (!m_properties.contains(propertyInfo.first)) {
            m_properties.append(propertyInfo.first);
            m_propertyTypes.append(propertyInfo.second);
        }
    }
}

bool NodeMetaInfoPrivate::isPropertyWritable(const PropertyName &propertyName) const
{
    if (!isValid())
        return false;

    ensureProperties();

    if (propertyName.contains('.')) {
        const PropertyNameList parts = propertyName.split('.');
        const PropertyName &objectName = parts.constFirst();
        const PropertyName &rawPropertyName = parts.constLast();
        const TypeName objectType = propertyType(objectName);

        if (isValueType(objectType))
            return true;

        auto objectInfo = create(m_model, objectType);
        if (objectInfo->isValid())
            return objectInfo->isPropertyWritable(rawPropertyName);
        else
            return true;
    }

    const CppComponentValue *qmlObjectValue = getNearestCppComponentValue();
    if (!qmlObjectValue)
        return true;
    if (qmlObjectValue->hasProperty(QString::fromUtf8(propertyName)))
        return qmlObjectValue->isWritable(QString::fromUtf8(propertyName));
    else
        return true; //all properties of components are writable
}

bool NodeMetaInfoPrivate::isPropertyList(const PropertyName &propertyName) const
{
    if (!isValid())
        return false;

    ensureProperties();

    if (propertyName.contains('.')) {
        const PropertyNameList parts = propertyName.split('.');
        const PropertyName &objectName = parts.constFirst();
        const PropertyName &rawPropertyName = parts.constLast();
        const TypeName objectType = propertyType(objectName);

        if (isValueType(objectType))
            return false;

        auto objectInfo = create(m_model, objectType);
        if (objectInfo->isValid())
            return objectInfo->isPropertyList(rawPropertyName);
        else
            return true;
    }

    const CppComponentValue *qmlObjectValue = getNearestCppComponentValue();
    if (!qmlObjectValue)
        return false;

    if (!qmlObjectValue->hasProperty(QString::fromUtf8(propertyName))) {
        const TypeName typeName = propertyType(propertyName);
        return (typeName == "Item"  || typeName == "QtObject");
    }

    return qmlObjectValue->isListProperty(QString::fromUtf8(propertyName));
}

bool NodeMetaInfoPrivate::isPropertyPointer(const PropertyName &propertyName) const
{
    if (!isValid())
        return false;

    ensureProperties();

    if (propertyName.contains('.')) {
        const PropertyNameList parts = propertyName.split('.');
        const PropertyName &objectName = parts.constFirst();
        const PropertyName &rawPropertyName = parts.constLast();
        const TypeName objectType = propertyType(objectName);

        if (isValueType(objectType))
            return false;

        auto objectInfo = create(m_model, objectType);
        if (objectInfo->isValid())
            return objectInfo->isPropertyPointer(rawPropertyName);
        else
            return true;
    }

    const CppComponentValue *qmlObjectValue = getNearestCppComponentValue();
    if (!qmlObjectValue)
        return false;
    return qmlObjectValue->isPointer(QString::fromUtf8(propertyName));
}

bool NodeMetaInfoPrivate::isPropertyEnum(const PropertyName &propertyName) const
{
    if (!isValid())
        return false;

    ensureProperties();

    if (propertyType(propertyName).contains("Qt::"))
        return true;

    if (propertyName.contains('.')) {
        const PropertyNameList parts = propertyName.split('.');
        const PropertyName &objectName = parts.constFirst();
        const PropertyName &rawPropertyName = parts.constLast();
        const TypeName objectType = propertyType(objectName);

        if (isValueType(objectType))
            return false;

        auto objectInfo = create(m_model, objectType);
        if (objectInfo->isValid())
            return objectInfo->isPropertyEnum(rawPropertyName);
        else
            return false;
    }

    const CppComponentValue *qmlObjectValue = getNearestCppComponentValue();
    if (!qmlObjectValue)
        return false;
    return qmlObjectValue->getEnum(QString::fromUtf8(propertyType(propertyName))).isValid();
}

static QByteArray getPackage(const QByteArray &name)
{
    QList<QByteArray> nameComponents = name.split('.');
    if (nameComponents.size() < 2)
        return QByteArray();
    nameComponents.removeLast();

    return nameComponents.join('.');
}


QList<TypeName> qtObjectTypes()
{
    static QList<TypeName> typeNames = {"QML.QtObject", "QtQml.QtObject", "<cpp>.QObject"};

    return typeNames;
}

bool NodeMetaInfoPrivate::cleverCheckType(const TypeName &otherType) const
{
    if (otherType == qualfiedTypeName())
        return true;

    if (isFileComponent())
        return false;

    if (qtObjectTypes().contains(qualfiedTypeName()) && qtObjectTypes().contains(otherType))
        return true;

    const QByteArray typeName = getUnqualifiedName(otherType);
    const QByteArray package = getPackage(otherType);

    if (cppPackageName() == package)
        return QByteArray(package + '.' + typeName) == (cppPackageName() + '.' + getUnqualifiedName(qualfiedTypeName()));

    const CppComponentValue *qmlObjectValue = getCppComponentValue();
    if (!qmlObjectValue)
        return false;

    const LanguageUtils::FakeMetaObject::Export exp =
            qmlObjectValue->metaObject()->exportInPackage(QString::fromUtf8(package));
    QString convertedName = exp.type;
    if (convertedName.isEmpty())
        convertedName = qmlObjectValue->className();

    return typeName == convertedName.toUtf8();
}

static TypeName toSimplifiedTypeName(const TypeName &typeName)
{
    return typeName.split('.').constLast();
}

QMetaType::Type NodeMetaInfoPrivate::variantTypeId(const PropertyName &propertyName) const
{
    TypeName typeName = toSimplifiedTypeName(propertyType(propertyName));

    if (typeName == "string")
        return QMetaType::QString;

    if (typeName == "color")
        return QMetaType::QColor;

    if (typeName == "int")
        return QMetaType::Int;

    if (typeName == "url")
        return QMetaType::QUrl;

    if (typeName == "real")
        return QMetaType::Double;

    if (typeName == "bool")
        return QMetaType::Bool;

    if (typeName == "boolean")
        return QMetaType::Bool;

    if (typeName == "date")
        return QMetaType::QDate;

    if (typeName == "alias")
        return QMetaType::User;

    if (typeName == "var")
        return QMetaType::User;

    if (typeName == "vector2d")
        return QMetaType::QVector2D;

    if (typeName == "vector3d")
        return QMetaType::QVector3D;

    if (typeName == "vector4d")
        return QMetaType::QVector4D;

    return static_cast<QMetaType::Type>(QMetaType::fromName(typeName.data()).id());
}

int NodeMetaInfoPrivate::majorVersion() const
{
    return m_majorVersion;
}

int NodeMetaInfoPrivate::minorVersion() const
{
    return m_minorVersion;
}

const TypeName &NodeMetaInfoPrivate::qualfiedTypeName() const
{
    return m_qualfiedTypeName;
}

Model *NodeMetaInfoPrivate::model() const
{
    return m_model;
}

QStringList NodeMetaInfoPrivate::keysForEnum(const QString &enumName) const
{
    if (!isValid())
        return {};

    const CppComponentValue *qmlObjectValue = getNearestCppComponentValue();
    if (!qmlObjectValue)
        return {};
    return qmlObjectValue->getEnum(enumName).keys();
}

QByteArray NodeMetaInfoPrivate::cppPackageName() const
{
    if (!isFileComponent()) {
        if (const CppComponentValue *qmlObject = getCppComponentValue())
            return qmlObject->moduleName().toUtf8();
    }
    return QByteArray();
}

QString NodeMetaInfoPrivate::componentFileName() const
{
    if (isFileComponent()) {
        const ASTObjectValue * astObjectValue = value_cast<ASTObjectValue>(getObjectValue());
        if (astObjectValue) {
            Utils::FilePath fileName;
            int line;
            int column;
            if (astObjectValue->getSourceLocation(&fileName, &line, &column))
                return fileName.toUrlishString();
        }
    }
    return QString();
}

QString NodeMetaInfoPrivate::importDirectoryPath() const
{
    ModelManagerInterface *modelManager = ModelManagerInterface::instance();

    if (isValid()) {
        const auto *imports = context()->imports(document());
        ImportInfo importInfo = imports->info(lookupNameComponent().constLast(), context().data());

        if (importInfo.type() == ImportType::Directory) {
            return importInfo.path();
        } else if (importInfo.type() == ImportType::Library) {
            if (modelManager) {
                const QStringList importPaths = model()->importPaths();
                for (const QString &importPath : importPaths) {
                    const QString targetPath = QDir(importPath).filePath(importInfo.path());
                    if (QDir(targetPath).exists())
                        return targetPath;
                    const QString targetPathVersion = QDir(importPath).filePath(importInfo.path()
                                                                                + '.'
                                                                                + QString::number(importInfo.version().majorVersion()));
                    if (QDir(targetPathVersion).exists())
                        return targetPathVersion;
                }
            }
        }
    }
    return QString();
}

Import NodeMetaInfoPrivate::requiredImport() const
{
    if (!isValid())
        return {};

    const auto *imports = context()->imports(document());
    ImportInfo importInfo = imports->info(lookupNameComponent().constLast(), context().data());

    if (importInfo.type() == ImportType::Directory) {
        return Import::createFileImport(importInfo.name(),
                                        importInfo.version().toString(),
                                        importInfo.as());
    } else if (importInfo.type() == ImportType::Library) {
        const QStringList importPaths = model()->importPaths();
        for (const QString &importPath : importPaths) {
            const QDir importDir(importPath);
            const QString targetPathVersion = importDir.filePath(
                importInfo.path() + '.' + QString::number(importInfo.version().majorVersion()));
            if (QDir(targetPathVersion).exists()) {
                return Import::createLibraryImport(importInfo.name(),
                                                   importInfo.version().toString(),
                                                   importInfo.as(),
                                                   {targetPathVersion});
            }

            const QString targetPath = importDir.filePath(importInfo.path());
            if (QDir(targetPath).exists()) {
                return Import::createLibraryImport(importInfo.name(),
                                                   importInfo.version().toString(),
                                                   importInfo.as(),
                                                   {targetPath});
            }
        }
    }
    return {};
}

QString NodeMetaInfoPrivate::lookupName() const
{
    QString className = QString::fromUtf8(m_qualfiedTypeName);
    QString packageName;

    QStringList packageClassName = className.split('.');
    if (packageClassName.size() > 1) {
        className = packageClassName.takeLast();
        packageName = packageClassName.join('.');
    }

    return CppQmlTypes::qualifiedName(
                packageName,
                className,
                LanguageUtils::ComponentVersion(m_majorVersion, m_minorVersion));
}

QStringList NodeMetaInfoPrivate::lookupNameComponent() const
{
    QString tempString = fullQualifiedImportAliasType();
    return tempString.split('.');
}

bool NodeMetaInfoPrivate::isValid() const
{
    return m_isValid && context() && document();
}

namespace {
TypeName nonexistingTypeName("Property does not exist...");
}

const TypeName &NodeMetaInfoPrivate::propertyType(const PropertyName &propertyName) const
{
    ensureProperties();

    if (!m_properties.contains(propertyName))
        return nonexistingTypeName;
    return m_propertyTypes.at(m_properties.indexOf(propertyName));
}

void NodeMetaInfoPrivate::setupPrototypes()
{
    QList<const ObjectValue *> objects;

    const ObjectValue *ov;

    if (m_isFileComponent)
        ov = getObjectValue();
    else
        ov = getCppComponentValue();

    PrototypeIterator prototypeIterator(ov, context());

    objects = prototypeIterator.all();

    if (prototypeIterator.error() != PrototypeIterator::NoError) {
        m_isValid = false;
        return;
    }

    for (const ObjectValue *ov : std::as_const(objects)) {
        TypeDescription description;
        description.className = ov->className();
        description.minorVersion = -1;
        description.majorVersion = -1;
        if (description.className == "QQuickItem") {
            /* Ugly hack to recover from wrong prototypes for Item */
            if (const CppComponentValue *qmlValue = value_cast<CppComponentValue>(
                    context()->lookupType(document(), {"Item"}))) {
                description.className = "QtQuick.Item";
                description.minorVersion = qmlValue->componentVersion().minorVersion();
                description.majorVersion = qmlValue->componentVersion().majorVersion();
                m_prototypes.append(description);
            } else {
                qWarning() << Q_FUNC_INFO << "Lookup for Item failed";
            }
            continue;
        }

        if (const CppComponentValue *qmlValue = value_cast<CppComponentValue>(ov)) {
            description.minorVersion = qmlValue->componentVersion().minorVersion();
            description.majorVersion = qmlValue->componentVersion().majorVersion();
            LanguageUtils::FakeMetaObject::Export qtquickExport = qmlValue->metaObject()->exportInPackage(QLatin1String("QtQuick"));
            LanguageUtils::FakeMetaObject::Export cppExport = qmlValue->metaObject()->exportInPackage(QLatin1String("<cpp>"));

            if (qtquickExport.isValid()) {
                description.className = qtquickExport.package + '.' + qtquickExport.type;
            } else {
                bool found = false;
                if (cppExport.isValid()) {
                    const QList<LanguageUtils::FakeMetaObject::Export> exports = qmlValue->metaObject()->exports();
                    for (const LanguageUtils::FakeMetaObject::Export &exportValue : exports) {
                        if (exportValue.package.toUtf8() != "<cpp>") {
                            found = true;
                            description.className = exportValue.package + '.' + exportValue.type;
                        }
                    }
                }
                if (!found) {
                    if (qmlValue->moduleName().isEmpty() && cppExport.isValid()) {
                        description.className = cppExport.package + '.' + cppExport.type;
                    } else if (!qmlValue->moduleName().isEmpty()) {
                        description.className.prepend(qmlValue->moduleName() + QLatin1Char('.'));
                    }
                }
            }
            m_prototypes.append(description);
        } else {
            if (context()->lookupType(document(), {ov->className()})) {
                const auto *allImports = context()->imports(document());
                ImportInfo importInfo = allImports->info(description.className, context().data());

                if (importInfo.isValid()) {
                    QString uri = importInfo.name();
                    uri.replace(QStringLiteral(","), QStringLiteral("."));
                    if (!uri.isEmpty())
                        description.className = QString(uri + "." + description.className);
                }

                m_prototypes.append(description);
            }
        }
    }
}

#ifndef QDS_USE_PROJECTSTORAGE
QList<TypeDescription> NodeMetaInfoPrivate::prototypes() const
{
    return m_prototypes;
}
#endif

const CppComponentValue *NodeMetaInfoPrivate::getNearestCppComponentValue() const
{
    if (m_isFileComponent)
        return findQmlPrototype(getObjectValue(), context());
    return getCppComponentValue();
}

QString NodeMetaInfoPrivate::fullQualifiedImportAliasType() const
{
    if (m_model && m_model->rewriterView())
        return model()->rewriterView()->convertTypeToImportAlias(QString::fromUtf8(m_qualfiedTypeName));
    return QString::fromUtf8(m_qualfiedTypeName);
}

void NodeMetaInfoPrivate::ensureProperties() const
{
    if (m_propertiesSetup)
        return;

    const_cast<NodeMetaInfoPrivate*>(this)->initialiseProperties();
}

void NodeMetaInfoPrivate::initialiseProperties()
{
    if (!isValid())
        return;

    m_propertiesSetup = true;

    QTC_ASSERT(m_objectValue, qDebug() << qualfiedTypeName(); return);

    setupPropertyInfo(getTypes(m_objectValue, context()));
    setupLocalPropertyInfo(getTypes(m_objectValue, context(), true));

    m_signals = getSignals(m_objectValue, context());
    m_slots = getSlots(m_objectValue, context());
}

NodeMetaInfo::NodeMetaInfo() = default;
NodeMetaInfo::NodeMetaInfo(const NodeMetaInfo &) = default;
NodeMetaInfo &NodeMetaInfo::operator=(const NodeMetaInfo &) = default;
NodeMetaInfo::NodeMetaInfo(NodeMetaInfo &&) = default;
NodeMetaInfo &NodeMetaInfo::operator=(NodeMetaInfo &&) = default;

#ifndef QDS_USE_PROJECTSTORAGE

NodeMetaInfo::NodeMetaInfo(Model *model, const TypeName &type, int maj, int min)
    : m_privateData(NodeMetaInfoPrivate::create(model, type, maj, min))
{
}
#endif

NodeMetaInfo::~NodeMetaInfo() = default;

bool NodeMetaInfo::isValid() const
{
    if constexpr (useProjectStorage())
        return bool(m_typeId);
    else
        return m_privateData && m_privateData->isValid();
}

MetaInfoType NodeMetaInfo::type(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (isValid()) {
            using NanotraceHR::keyValue;
            NanotraceHR::Tracer tracer{"get type",
                                       category(),
                                       keyValue("type id", m_typeId),
                                       keyValue("caller location", sl)};
            auto kind = typeData().traits.kind;
            tracer.end(keyValue("type kind", kind));

            switch (kind) {
            case Storage::TypeTraitsKind::Reference:
                return MetaInfoType::Reference;
            case Storage::TypeTraitsKind::Value:
                return MetaInfoType::Value;
            case Storage::TypeTraitsKind::Sequence:
                return MetaInfoType::Sequence;
            case Storage::TypeTraitsKind::None:
                return MetaInfoType::None;
            }
        }
    }

    return MetaInfoType::None;
}

bool NodeMetaInfo::isFileComponent(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is file component",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto isFileComponent = typeData().traits.isFileComponent;

        tracer.end(keyValue("is file component", isFileComponent));

        return isFileComponent;

    } else {
        return isValid() && m_privateData->isFileComponent();
    }
}

bool NodeMetaInfo::isSingleton(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is singleton",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto isSingleton = typeData().traits.isSingleton;

        tracer.end(keyValue("is singleton", isSingleton));

        return isSingleton;

    } else {
        return false;
    }
}

bool NodeMetaInfo::isInsideProject(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is inside project",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto isInsideProject = typeData().traits.isInsideProject;

        tracer.end(keyValue("is inside project", isInsideProject));

        return isInsideProject;

    } else {
        return false;
    }
}

FlagIs NodeMetaInfo::canBeContainer(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"can be container",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto canBeContainer = typeData().traits.canBeContainer;

        tracer.end(keyValue("can be container", canBeContainer));

        return canBeContainer;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::forceClip(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"force clip",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto forceClip = typeData().traits.forceClip;

        tracer.end(keyValue("force clip", forceClip));

        return forceClip;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::doesLayoutChildren(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"does layout children",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto doesLayoutChildren = typeData().traits.doesLayoutChildren;

        tracer.end(keyValue("does layout children", doesLayoutChildren));

        return doesLayoutChildren;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::canBeDroppedInFormEditor(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"can be dropped in form editor",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto canBeDroppedInFormEditor = typeData().traits.canBeDroppedInFormEditor;

        tracer.end(keyValue("can be dropped in form editor", canBeDroppedInFormEditor));

        return canBeDroppedInFormEditor;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::canBeDroppedInNavigator(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"can be dropped in navigator",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto canBeDroppedInNavigator = typeData().traits.canBeDroppedInNavigator;

        tracer.end(keyValue("can be dropped in navigator", canBeDroppedInNavigator));

        return canBeDroppedInNavigator;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::canBeDroppedInView3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"can be dropped in view3d",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto canBeDroppedInView3D = typeData().traits.canBeDroppedInView3D;

        tracer.end(keyValue("can be dropped in view3d", canBeDroppedInView3D));

        return canBeDroppedInView3D;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::isMovable(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is movable",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto isMovable = typeData().traits.isMovable;

        tracer.end(keyValue("is movable", isMovable));

        return isMovable;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::isResizable(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is resizable",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto isResizable = typeData().traits.isResizable;

        tracer.end(keyValue("is resizable", isResizable));

        return isResizable;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::hasFormEditorItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"has form editor item",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto hasFormEditorItem = typeData().traits.hasFormEditorItem;

        tracer.end(keyValue("has form editor item", hasFormEditorItem));

        return hasFormEditorItem;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::isStackedContainer(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is stacked container",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto isStackedContainer = typeData().traits.isStackedContainer;

        tracer.end(keyValue("is stacked container", isStackedContainer));

        return isStackedContainer;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::takesOverRenderingOfChildren(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"takes over rendering of children",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto takesOverRenderingOfChildren = typeData().traits.takesOverRenderingOfChildren;

        tracer.end(keyValue("takes over rendering of children", takesOverRenderingOfChildren));

        return takesOverRenderingOfChildren;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::visibleInNavigator(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"visible in navigator",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto visibleInNavigator = typeData().traits.visibleInNavigator;

        tracer.end(keyValue("visible in navigator", visibleInNavigator));

        return visibleInNavigator;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::hideInNavigator() const
{
    if constexpr (useProjectStorage()) {
        if (isValid())
            return typeData().traits.hideInNavigator;

        return FlagIs::False;
    }

    return FlagIs::Set;
}

FlagIs NodeMetaInfo::visibleInLibrary(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return FlagIs::False;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"visible in library",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto visibleInLibrary = typeData().traits.visibleInLibrary;

        tracer.end(keyValue("visible in library", visibleInLibrary));

        return visibleInLibrary;
    }

    return FlagIs::Set;
}

namespace {

[[maybe_unused]] auto propertyId(const ProjectStorageType &projectStorage,
                                 TypeId typeId,
                                 Utils::SmallStringView propertyName)
{
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get combound property id",
                               category(),
                               keyValue("type id", typeId),
                               keyValue("property name", propertyName)};

    auto begin = propertyName.begin();
    const auto end = propertyName.end();

    auto found = std::find(begin, end, '.');
    auto propertyId = projectStorage.propertyDeclarationId(typeId, {begin, found});

    if (propertyId && found != end) {
        auto propertyData = projectStorage.propertyDeclaration(propertyId);
        if (auto propertyTypeId = propertyData->propertyTypeId) {
            begin = std::next(found);
            found = std::find(begin, end, '.');
            propertyId = projectStorage.propertyDeclarationId(propertyTypeId, {begin, found});

            if (propertyId && found != end) {
                begin = std::next(found);
                auto id = projectStorage.propertyDeclarationId(propertyTypeId, {begin, end});

                tracer.end(keyValue("property id", id));

                return id;
            }
        }
    }

    tracer.end(keyValue("property id", propertyId));

    return propertyId;
}

} // namespace

bool NodeMetaInfo::hasProperty(Utils::SmallStringView propertyName) const
{
    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"has property",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("property name", propertyName)};

        if (!isValid())
            return false;

        auto hasPropertyId = bool(propertyId(*m_projectStorage, m_typeId, propertyName));

        tracer.end(keyValue("has property", hasPropertyId));

        return hasPropertyId;
    } else {
        return isValid() && m_privateData->properties().contains(QByteArrayView(propertyName));
    }
}

PropertyMetaInfos NodeMetaInfo::properties(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get properties",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        return Utils::transform<PropertyMetaInfos>(m_projectStorage->propertyDeclarationIds(m_typeId),
                                                   PropertyMetaInfo::bind(m_projectStorage));

    } else {
        const auto &properties = m_privateData->properties();

        PropertyMetaInfos propertyMetaInfos;
        propertyMetaInfos.reserve(static_cast<std::size_t>(properties.size()));

        for (const auto &name : properties)
            propertyMetaInfos.push_back({m_privateData, name});

        return propertyMetaInfos;
    }
}

PropertyMetaInfos NodeMetaInfo::localProperties(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get local properties",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        return Utils::transform<PropertyMetaInfos>(m_projectStorage->localPropertyDeclarationIds(
                                                       m_typeId),
                                                   PropertyMetaInfo::bind(m_projectStorage));

    } else {
        const auto &properties = m_privateData->localProperties();

        PropertyMetaInfos propertyMetaInfos;
        propertyMetaInfos.reserve(static_cast<std::size_t>(properties.size()));

        for (const auto &name : properties)
            propertyMetaInfos.emplace_back(m_privateData, name);

        return propertyMetaInfos;
    }
}

PropertyMetaInfo NodeMetaInfo::property(PropertyNameView propertyName) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get property",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("property name", propertyName)};

        return {propertyId(*m_projectStorage, m_typeId, propertyName), m_projectStorage};
    } else {
        if (hasProperty(propertyName)) {
            return PropertyMetaInfo{m_privateData, propertyName};
        }
        return {};
    }
}

PropertyNameList NodeMetaInfo::signalNames(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get signal names",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        return Utils::transform<PropertyNameList>(m_projectStorage->signalDeclarationNames(m_typeId),
                                                  &Utils::SmallString::toQByteArray);

    } else {
        return m_privateData->signalNames();
    }
}

PropertyNameList NodeMetaInfo::slotNames(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get slot names",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};
        return Utils::transform<PropertyNameList>(m_projectStorage->functionDeclarationNames(m_typeId),
                                                  &Utils::SmallString::toQByteArray);
    } else {
        return m_privateData->slotNames();
    }
}

PropertyName NodeMetaInfo::defaultPropertyName(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get default property name",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};
        if (auto name = m_projectStorage->propertyName(defaultPropertyDeclarationId())) {
            tracer.end(keyValue("default property name", name));
            return name->toQByteArray();
        }

    } else {
        return m_privateData->defaultPropertyName();
    }

    return {};
}

PropertyMetaInfo NodeMetaInfo::defaultProperty(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get default property",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto id = defaultPropertyDeclarationId();

        tracer.end(keyValue("default property id", id));

        return PropertyMetaInfo(id, m_projectStorage);
    } else {
        return property(defaultPropertyName());
    }
}

bool NodeMetaInfo::hasDefaultProperty(SL sl) const
{
    if (!isValid())
        return false;

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"has default property",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};
        auto hasDefaultProperty = bool(defaultPropertyDeclarationId());
        tracer.end(keyValue("has default property", hasDefaultProperty));

        return hasDefaultProperty;
    } else {
        return !defaultPropertyName().isEmpty();
    }
}

std::vector<NodeMetaInfo> NodeMetaInfo::selfAndPrototypes([[maybe_unused]] SL sl) const
{
    if (!isValid())
        return {};

#ifdef QDS_USE_PROJECTSTORAGE
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get self and prototypes",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return Utils::transform<NodeMetaInfos>(m_projectStorage->prototypeAndSelfIds(m_typeId),
                                           NodeMetaInfo::bind(m_projectStorage));
#else
    NodeMetaInfos hierarchy = {*this};
    Model *model = m_privateData->model();
    for (const TypeDescription &type : m_privateData->prototypes()) {
        auto &last = hierarchy.emplace_back(model,
                                            type.className.toUtf8(),
                                            type.majorVersion,
                                            type.minorVersion);
        if (!last.isValid())
            hierarchy.pop_back();
    }

    return hierarchy;
#endif
}

NodeMetaInfos NodeMetaInfo::prototypes([[maybe_unused]] SL sl) const
{
    if (!isValid())
        return {};

#ifdef QDS_USE_PROJECTSTORAGE
    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get prototypes",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};
    return Utils::transform<NodeMetaInfos>(m_projectStorage->prototypeIds(m_typeId),
                                           NodeMetaInfo::bind(m_projectStorage));

#else
    NodeMetaInfos hierarchy;
    Model *model = m_privateData->model();
    for (const TypeDescription &type : m_privateData->prototypes()) {
        auto &last = hierarchy.emplace_back(model,
                                            type.className.toUtf8(),
                                            type.majorVersion,
                                            type.minorVersion);
        if (!last.isValid())
            hierarchy.pop_back();
    }

    return hierarchy;
#endif
}

namespace {
template<const char *moduleName, const char *typeName, ModuleKind moduleKind = ModuleKind::QmlLibrary>
bool isBasedOnCommonType(NotNullPointer<const ProjectStorageType> projectStorage, TypeId typeId)
{
    if (!typeId)
        return false;

    auto base = projectStorage->commonTypeId<moduleName, typeName, moduleKind>();

    return projectStorage->isBasedOn(typeId, base);
}
} // namespace

bool NodeMetaInfo::defaultPropertyIsComponent() const
{
    if (!isValid())
        return false;

    if (useProjectStorage()) {
        auto id = defaultPropertyDeclarationId();
        auto propertyDeclaration = m_projectStorage->propertyDeclaration(id);

        using namespace Storage::Info;
        return isBasedOnCommonType<QML, Component>(m_projectStorage, propertyDeclaration->typeId);
    } else {
        if (hasDefaultProperty())
            return defaultProperty().propertyType().isQmlComponent();
        return false;
    }
}

QString NodeMetaInfo::displayName() const
{
    return {};
}

#ifndef QDS_USE_PROJECTSTORAGE
TypeName NodeMetaInfo::typeName() const
{
    if (isValid())
        return m_privateData->qualfiedTypeName();

    return {};
}

TypeName NodeMetaInfo::simplifiedTypeName() const
{
    if (isValid())
        return toSimplifiedTypeName(typeName());

    return {};
}

int NodeMetaInfo::majorVersion() const
{
    if constexpr (!useProjectStorage()) {
        if (isValid())
            return m_privateData->majorVersion();
    }

    return -1;
}

int NodeMetaInfo::minorVersion() const
{
    if constexpr (!useProjectStorage()) {
        if (isValid())
            return m_privateData->minorVersion();
    }

    return -1;
}
#endif

Storage::Info::ExportedTypeNames NodeMetaInfo::allExportedTypeNames(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get all exported type names",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        return m_projectStorage->exportedTypeNames(m_typeId);
    }

    return {};
}

Storage::Info::ExportedTypeNames NodeMetaInfo::exportedTypeNamesForSourceId(SourceId sourceId) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get exported type names for source id",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("source id", sourceId)};

        return m_projectStorage->exportedTypeNames(m_typeId, sourceId);
    }

    return {};
}

Storage::Info::TypeHints NodeMetaInfo::typeHints(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get type hints",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto hints = m_projectStorage->typeHints(m_typeId);

        tracer.end(keyValue("type hints", hints));

        return hints;
    }

    return {};
}

Utils::PathString NodeMetaInfo::iconPath(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get icon path",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto iconPath = m_projectStorage->typeIconPath(m_typeId);

        tracer.end(keyValue("icon path", iconPath));

        return iconPath;
    }

    return {};
}

Storage::Info::ItemLibraryEntries NodeMetaInfo::itemLibrariesEntries(SL sl) const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get item library entries",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto entries = m_projectStorage->itemLibraryEntries(m_typeId);

        tracer.end(keyValue("item library entries", entries));

        return entries;
    }

    return {};
}

SourceId NodeMetaInfo::sourceId(SL sl) const
{
    if (!isValid())
        return SourceId{};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get source id",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto id = typeData().sourceId;

        tracer.end(keyValue("source id", id));

        return id;
    }

    return SourceId{};
}

#ifndef QDS_USE_PROJECTSTORAGE
QString NodeMetaInfo::componentFileName() const
{
    if constexpr (!useProjectStorage()) {
        if (isValid()) {
            return m_privateData->componentFileName();
        }
    }

    return {};
}

QString NodeMetaInfo::importDirectoryPath() const
{
    if constexpr (!useProjectStorage()) {
        if (isValid()) {
            return m_privateData->importDirectoryPath();
        }
    }

    return {};
}

QString NodeMetaInfo::requiredImportString() const
{
    if (!isValid())
        return {};

    if constexpr (!useProjectStorage()) {
        Import imp = m_privateData->requiredImport();
        if (!imp.isEmpty())
            return imp.toImportString();
    }

    return {};
}
#endif

SourceId NodeMetaInfo::propertyEditorPathId(SL sl) const
{
    if (!isValid())
        return SourceId{};

    if (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get property editor path id",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        auto id = m_projectStorage->propertyEditorPathId(m_typeId);

        tracer.end(keyValue("property editor path id", id));

        return id;
    }

    return SourceId{};
}

const Storage::Info::Type &NodeMetaInfo::typeData() const
{
    if (!m_typeData)
        m_typeData = m_projectStorage->type(m_typeId);

    return *m_typeData;
}

PropertyDeclarationId NodeMetaInfo::defaultPropertyDeclarationId() const
{
    if (!m_defaultPropertyId)
        m_defaultPropertyId.emplace(m_projectStorage->defaultPropertyDeclarationId(m_typeId));

    return *m_defaultPropertyId;
}

bool NodeMetaInfo::isSubclassOf([[maybe_unused]] const TypeName &type,
                                [[maybe_unused]] int majorVersion,
                                [[maybe_unused]] int minorVersion) const
{
    if (!isValid()) {
        qWarning() << "NodeMetaInfo is invalid" << type;
        return false;
    }

#ifndef QDS_USE_PROJECTSTORAGE

    if (typeName().isEmpty())
        return false;

    if (typeName() == type)
        return true;

    if (m_privateData->prototypeCachePositives().contains(
            stringIdentifier(type, majorVersion, minorVersion)))
        return true; //take a shortcut - optimization

    if (m_privateData->prototypeCacheNegatives().contains(
            stringIdentifier(type, majorVersion, minorVersion)))
        return false; //take a shortcut - optimization

    const NodeMetaInfos superClassList = prototypes();
    for (const NodeMetaInfo &superClass : superClassList) {
        if (superClass.m_privateData->cleverCheckType(type)) {
            m_privateData->prototypeCachePositives().insert(
                stringIdentifier(type, majorVersion, minorVersion));
            return true;
        }
    }
    m_privateData->prototypeCacheNegatives().insert(stringIdentifier(type, majorVersion, minorVersion));
#endif
    return false;
}

bool NodeMetaInfo::isSuitableForMouseAreaFill(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is suitable for mouse area fill",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto itemId = m_projectStorage->commonTypeId<QtQuick, Item>();
        auto mouseAreaId = m_projectStorage->commonTypeId<QtQuick, MouseArea>();
        auto controlsControlId = m_projectStorage->commonTypeId<QtQuick_Controls, Control>();
        auto templatesControlId = m_projectStorage->commonTypeId<QtQuick_Templates, Control>();

        auto isSuitableForMouseAreaFill = m_projectStorage->isBasedOn(m_typeId,
                                                                      itemId,
                                                                      mouseAreaId,
                                                                      controlsControlId,
                                                                      templatesControlId);

        tracer.end(keyValue("is suitable for mouse area fill", isSuitableForMouseAreaFill));

        return isSuitableForMouseAreaFill;

    } else {
        return isSubclassOf("QtQuick.Item") && !isSubclassOf("QtQuick.MouseArea")
               && !isSubclassOf("QtQuick.Controls.Control")
               && !isSubclassOf("QtQuick.Templates.Control");
    }
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo, [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 1 node meta info",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("meta info type id", metaInfo.m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId, metaInfo.m_typeId);
#else
    if (!isValid())
        return false;
    if (majorVersion() == -1 && minorVersion() == -1)
        return isSubclassOf(metaInfo.typeName());
    return isSubclassOf(metaInfo.typeName(), metaInfo.majorVersion(), metaInfo.minorVersion());
#endif
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo1,
                             const NodeMetaInfo &metaInfo2,
                             [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 2 node meta infos",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId, metaInfo1.m_typeId, metaInfo2.m_typeId);
#else
    if (!isValid())
        return false;
    if (majorVersion() == -1 && minorVersion() == -1)
        return (isSubclassOf(metaInfo1.typeName()) || isSubclassOf(metaInfo2.typeName()));

    return (isSubclassOf(metaInfo1.typeName(), metaInfo1.majorVersion(), metaInfo1.minorVersion())
            || isSubclassOf(metaInfo2.typeName(), metaInfo2.majorVersion(), metaInfo2.minorVersion()));
#endif
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo1,
                             const NodeMetaInfo &metaInfo2,
                             const NodeMetaInfo &metaInfo3,
                             [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 3 node meta infos",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId,
                                       metaInfo1.m_typeId,
                                       metaInfo2.m_typeId,
                                       metaInfo3.m_typeId);
#else
    if (!isValid())
        return false;
    if (majorVersion() == -1 && minorVersion() == -1)
        return (isSubclassOf(metaInfo1.typeName()) || isSubclassOf(metaInfo2.typeName())
                || isSubclassOf(metaInfo3.typeName()));

    return (isSubclassOf(metaInfo1.typeName(), metaInfo1.majorVersion(), metaInfo1.minorVersion())
            || isSubclassOf(metaInfo2.typeName(), metaInfo2.majorVersion(), metaInfo2.minorVersion())
            || isSubclassOf(metaInfo3.typeName(), metaInfo3.majorVersion(), metaInfo3.minorVersion()));
#endif
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo1,
                             const NodeMetaInfo &metaInfo2,
                             const NodeMetaInfo &metaInfo3,
                             const NodeMetaInfo &metaInfo4,
                             [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 4 node meta infos",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId,
                                       metaInfo1.m_typeId,
                                       metaInfo2.m_typeId,
                                       metaInfo3.m_typeId,
                                       metaInfo4.m_typeId);
#else
    return isValid()
           && (isSubclassOf(metaInfo1.typeName(), metaInfo1.majorVersion(), metaInfo1.minorVersion())
               || isSubclassOf(metaInfo2.typeName(), metaInfo2.majorVersion(), metaInfo2.minorVersion())
               || isSubclassOf(metaInfo3.typeName(), metaInfo3.majorVersion(), metaInfo3.minorVersion())
               || isSubclassOf(metaInfo4.typeName(),
                               metaInfo4.majorVersion(),
                               metaInfo4.minorVersion()));
#endif
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo1,
                             const NodeMetaInfo &metaInfo2,
                             const NodeMetaInfo &metaInfo3,
                             const NodeMetaInfo &metaInfo4,
                             const NodeMetaInfo &metaInfo5,
                             [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 5 node meta infos",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId,
                                       metaInfo1.m_typeId,
                                       metaInfo2.m_typeId,
                                       metaInfo3.m_typeId,
                                       metaInfo4.m_typeId,
                                       metaInfo5.m_typeId);
#else
    return isValid()
           && (isSubclassOf(metaInfo1.typeName(), metaInfo1.majorVersion(), metaInfo1.minorVersion())
               || isSubclassOf(metaInfo2.typeName(), metaInfo2.majorVersion(), metaInfo2.minorVersion())
               || isSubclassOf(metaInfo3.typeName(), metaInfo3.majorVersion(), metaInfo3.minorVersion())
               || isSubclassOf(metaInfo4.typeName(), metaInfo4.majorVersion(), metaInfo4.minorVersion())
               || isSubclassOf(metaInfo5.typeName(),
                               metaInfo5.majorVersion(),
                               metaInfo5.minorVersion()));
#endif
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo1,
                             const NodeMetaInfo &metaInfo2,
                             const NodeMetaInfo &metaInfo3,
                             const NodeMetaInfo &metaInfo4,
                             const NodeMetaInfo &metaInfo5,
                             const NodeMetaInfo &metaInfo6,
                             [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 6 node meta infos",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId,
                                       metaInfo1.m_typeId,
                                       metaInfo2.m_typeId,
                                       metaInfo3.m_typeId,
                                       metaInfo4.m_typeId,
                                       metaInfo5.m_typeId,
                                       metaInfo6.m_typeId);
#else
    return isValid()
           && (isSubclassOf(metaInfo1.typeName(), metaInfo1.majorVersion(), metaInfo1.minorVersion())
               || isSubclassOf(metaInfo2.typeName(), metaInfo2.majorVersion(), metaInfo2.minorVersion())
               || isSubclassOf(metaInfo3.typeName(), metaInfo3.majorVersion(), metaInfo3.minorVersion())
               || isSubclassOf(metaInfo4.typeName(), metaInfo4.majorVersion(), metaInfo4.minorVersion())
               || isSubclassOf(metaInfo5.typeName(), metaInfo5.majorVersion(), metaInfo5.minorVersion())
               || isSubclassOf(metaInfo6.typeName(),
                               metaInfo6.majorVersion(),
                               metaInfo6.minorVersion()));
#endif
}

bool NodeMetaInfo::isBasedOn(const NodeMetaInfo &metaInfo1,
                             const NodeMetaInfo &metaInfo2,
                             const NodeMetaInfo &metaInfo3,
                             const NodeMetaInfo &metaInfo4,
                             const NodeMetaInfo &metaInfo5,
                             const NodeMetaInfo &metaInfo6,
                             const NodeMetaInfo &metaInfo7,
                             [[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is based on 7 node meta infos",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return m_projectStorage->isBasedOn(m_typeId,
                                       metaInfo1.m_typeId,
                                       metaInfo2.m_typeId,
                                       metaInfo3.m_typeId,
                                       metaInfo4.m_typeId,
                                       metaInfo5.m_typeId,
                                       metaInfo6.m_typeId,
                                       metaInfo7.m_typeId);
#else
    return isValid()
           && (isSubclassOf(metaInfo1.typeName(), metaInfo1.majorVersion(), metaInfo1.minorVersion())
               || isSubclassOf(metaInfo2.typeName(), metaInfo2.majorVersion(), metaInfo2.minorVersion())
               || isSubclassOf(metaInfo3.typeName(), metaInfo3.majorVersion(), metaInfo3.minorVersion())
               || isSubclassOf(metaInfo4.typeName(), metaInfo4.majorVersion(), metaInfo4.minorVersion())
               || isSubclassOf(metaInfo5.typeName(), metaInfo5.majorVersion(), metaInfo5.minorVersion())
               || isSubclassOf(metaInfo6.typeName(), metaInfo6.majorVersion(), metaInfo6.minorVersion())
               || isSubclassOf(metaInfo7.typeName(),
                               metaInfo7.majorVersion(),
                               metaInfo7.minorVersion()));
#endif
}

bool NodeMetaInfo::isGraphicalItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is graphical item",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto itemId = m_projectStorage->commonTypeId<QtQuick, Item>();
        auto windowId = m_projectStorage->commonTypeId<QtQuick_Window, Window>();
        auto dialogId = m_projectStorage->commonTypeId<QtQuick_Dialogs, Dialog>();
        auto popupId = m_projectStorage->commonTypeId<QtQuick_Controls, Popup>();

        return m_projectStorage->isBasedOn(m_typeId, itemId, windowId, dialogId, popupId);
    } else {
        return isValid()
               && (isSubclassOf("QtQuick.Item") || isSubclassOf("QtQuick.Window.Window")
                   || isSubclassOf("QtQuick.Dialogs.Dialog")
                   || isSubclassOf("QtQuick.Controls.Popup"));
    }
}

bool NodeMetaInfo::isQtObject(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is Qt object",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QML, QtObject>(m_projectStorage, m_typeId);
    } else {
        return isValid() && (isSubclassOf("QtQuick.QtObject") || isSubclassOf("QtQml.QtObject"));
    }
}

bool NodeMetaInfo::isQtQmlConnections([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is Qt Qml connections",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isBasedOnCommonType<QtQml, Connections>(m_projectStorage, m_typeId);
#else
    return isValid() && simplifiedTypeName() == "Connections";
#endif
}

bool NodeMetaInfo::isLayoutable(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is layoutable",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto positionerId = m_projectStorage->commonTypeId<QtQuick, Positioner>();
        auto layoutId = m_projectStorage->commonTypeId<QtQuick_Layouts, Layout>();
        auto splitViewId = m_projectStorage->commonTypeId<QtQuick_Controls, SplitView>();

        return m_projectStorage->isBasedOn(m_typeId, positionerId, layoutId, splitViewId);

    } else {
        return isValid()
               && (isSubclassOf("QtQuick.Positioner") || isSubclassOf("QtQuick.Layouts.Layout")
                   || isSubclassOf("QtQuick.Controls.SplitView"));
    }
}

bool NodeMetaInfo::isQtQuickLayoutsLayout(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Layouts.Layout",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Layouts, Layout>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Layouts.Layout");
    }
}

bool NodeMetaInfo::isView(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is view",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto listViewId = m_projectStorage->commonTypeId<QtQuick, ListView>();
        auto gridViewId = m_projectStorage->commonTypeId<QtQuick, GridView>();
        auto pathViewId = m_projectStorage->commonTypeId<QtQuick, PathView>();
        return m_projectStorage->isBasedOn(m_typeId, listViewId, gridViewId, pathViewId);
    } else {
        return isValid()
               && (isSubclassOf("QtQuick.ListView") || isSubclassOf("QtQuick.GridView")
                   || isSubclassOf("QtQuick.PathView"));
    }
}

bool NodeMetaInfo::usesCustomParser([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"uses custom parser",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    return typeData().traits.usesCustomParser;
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();
    return type == "VisualItemModel" || type == "VisualDataModel" || type == "ListModel"
           || type == "XmlListModel";
#endif
}

namespace {

template<typename... TypeIds>
bool isTypeId(TypeId typeId, TypeIds... otherTypeIds)
{
    static_assert(((std::is_same_v<TypeId, TypeIds>) &&...), "Parameter must be a TypeId!");

    return ((typeId == otherTypeIds) || ...);
}

} // namespace

bool NodeMetaInfo::isVector2D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is vector2d",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isTypeId(m_typeId, m_projectStorage->commonTypeId<QtQuick, vector2d>());
    } else {
        if (!m_privateData)
            return false;

        auto type = m_privateData->qualfiedTypeName();

        return type == "vector2d" || type == "QtQuick.vector2d" || type == "QVector2D";
    }
}

bool NodeMetaInfo::isVector3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is vector3d",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isTypeId(m_typeId, m_projectStorage->commonTypeId<QtQuick, vector3d>());
    } else {
        if (!m_privateData)
            return false;

        auto type = m_privateData->qualfiedTypeName();

        return type == "vector3d" || type == "QtQuick.vector3d" || type == "QVector3D";
    }
}

bool NodeMetaInfo::isVector4D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is vector4d",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isTypeId(m_typeId, m_projectStorage->commonTypeId<QtQuick, vector4d>());
    } else {
        if (!m_privateData)
            return false;

        auto type = m_privateData->qualfiedTypeName();

        return type == "vector4d" || type == "QtQuick.vector4d" || type == "QVector4D";
    }
}

bool NodeMetaInfo::isQtQuickPropertyChanges(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.PropertyChanges",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Storage::Info::PropertyChanges>(m_projectStorage,
                                                                            m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.PropertyChanges");
    }
}

bool NodeMetaInfo::isQtSafeRendererSafeRendererPicture(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is Qt.SafeRenderer.SafeRendererPicture",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<Qt_SafeRenderer, SafeRendererPicture>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("Qt.SafeRenderer.SafeRendererPicture");
    }
}

bool NodeMetaInfo::isQtSafeRendererSafePicture(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is Qt.SafeRenderer.SafePicture",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<Qt_SafeRenderer, SafePicture>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("Qt.SafeRenderer.SafePicture");
    }
}

bool NodeMetaInfo::isQtQuickTimelineKeyframe(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Timeline.Keyframe",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Timeline, Keyframe>(m_projectStorage, m_typeId);

    } else {
        return isValid() && isSubclassOf("QtQuick.Timeline.Keyframe");
    }
}

bool NodeMetaInfo::isQtQuickTimelineTimelineAnimation(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Timeline.TimelineAnimation",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Timeline, TimelineAnimation>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Timeline.TimelineAnimation");
    }
}

bool NodeMetaInfo::isQtQuickTimelineTimeline(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Timeline.Timeline",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Timeline, Timeline>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Timeline.Timeline");
    }
}

bool NodeMetaInfo::isQtQuickTimelineKeyframeGroup(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Timeline.KeyframeGroup",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Timeline, KeyframeGroup>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Timeline.KeyframeGroup");
    }
}

bool NodeMetaInfo::isListOrGridView(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is list or grid view",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto listViewId = m_projectStorage->commonTypeId<QtQuick, ListView>();
        auto gridViewId = m_projectStorage->commonTypeId<QtQuick, GridView>();
        return m_projectStorage->isBasedOn(m_typeId, listViewId, gridViewId);
    } else {
        return isValid() && (isSubclassOf("QtQuick.ListView") || isSubclassOf("QtQuick.GridView"));
    }
}

bool NodeMetaInfo::isNumber(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is number",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto intId = m_projectStorage->builtinTypeId<int>();
        auto uintId = m_projectStorage->builtinTypeId<uint>();
        auto floatId = m_projectStorage->builtinTypeId<float>();
        auto doubleId = m_projectStorage->builtinTypeId<double>();

        return isTypeId(m_typeId, intId, uintId, floatId, doubleId);
    } else {
        if (!isValid()) {
            return false;
        }

        return isFloat() || isInteger();
    }
}

bool NodeMetaInfo::isQtQuickExtrasPicture(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Extras.Picture",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Extras, Picture>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Extras.Picture");
    }
}

bool NodeMetaInfo::isQtQuickGradient([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE

    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is QtQuick.Gradient",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isBasedOnCommonType<QtQuick, Gradient>(m_projectStorage, m_typeId);
#else
    return isValid() && (isSubclassOf("QtQuick.Gradient"));
#endif
}

bool NodeMetaInfo::isQtQuickImage([[maybe_unused]] SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Image",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;

        return isBasedOnCommonType<QtQuick, Image>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Image");
    }
}

bool NodeMetaInfo::isQtQuickBorderImage([[maybe_unused]] SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.BorderImage",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;

        return isBasedOnCommonType<QtQuick, BorderImage>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.BorderImage");
    }
}

bool NodeMetaInfo::isAlias(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is alias",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        return false; // all types are already resolved
    } else {
        return isValid() && m_privateData->qualfiedTypeName() == "alias";
    }
}

bool NodeMetaInfo::isQtQuickPositioner(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Positioner",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;

        return isBasedOnCommonType<QtQuick, Positioner>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Positioner");
    }
}

bool NodeMetaInfo::isQtQuickPropertyAnimation(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.PropertyAnimation",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, PropertyAnimation>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.PropertyAnimation");
    }
}

bool NodeMetaInfo::isQtQuickRectangle([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE

    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is QtQuick.Rectange",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isBasedOnCommonType<QtQuick, Rectangle>(m_projectStorage, m_typeId);
#else
    return isValid() && isSubclassOf("QtQuick.Rectangle");
#endif
}

bool NodeMetaInfo::isQtQuickRepeater(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Repeater",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Repeater>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Repeater");
    }
}

bool NodeMetaInfo::isQtQuickShapesShape(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Shapes.Shape",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Shapes, Shape>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Shapes.Shape");
    }
}

bool NodeMetaInfo::isQtQuickControlsTabBar(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Controls.TabBar",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Controls, TabBar>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Controls.TabBar");
    }
}

bool NodeMetaInfo::isQtQuickControlsLabel([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is QtQuick.Controls.SwipeView",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isBasedOnCommonType<QtQuick_Controls, Label>(m_projectStorage, m_typeId);
#else
    return isValid() && isSubclassOf("QtQuick.Controls.Label");
#endif
}

bool NodeMetaInfo::isQtQuickControlsSwipeView(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Controls.SwipeView",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Controls, SwipeView>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Controls.SwipeView");
    }
}

bool NodeMetaInfo::isQtQuick3DCamera(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Camera",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Camera>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Camera");
    }
}

bool NodeMetaInfo::isQtQuick3DBakedLightmap(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.BakedLightmap",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, BakedLightmap>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.BakedLightmap");
    }
}

bool NodeMetaInfo::isQtQuick3DBuffer(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Buffer",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Buffer>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Buffer");
    }
}

bool NodeMetaInfo::isQtQuick3DInstanceListEntry(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.InstanceListEntry",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, InstanceListEntry>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.InstanceListEntry");
    }
}

bool NodeMetaInfo::isQtQuick3DLight(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Light",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Light>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Light");
    }
}

bool NodeMetaInfo::isQtQmlModelsListElement(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQml.Models.ListElement",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQml_Models, ListElement>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.ListElement");
    }
}

bool NodeMetaInfo::isQtQuickListModel(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.ListModel",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQml_Models, ListModel>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.ListModel");
    }
}

bool NodeMetaInfo::isQtQuickListView(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.ListView",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, ListView>(m_projectStorage, m_typeId);
    } else {
        return isValid() && (isSubclassOf("QtQuick.ListView"));
    }
}

bool QmlDesigner::NodeMetaInfo::isQtQuickGridView(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.GridView",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, GridView>(m_projectStorage, m_typeId);
    } else {
        return isValid() && (isSubclassOf("QtQuick.GridView"));
    }
}

bool NodeMetaInfo::isQtQuick3DInstanceList(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.InstanceList",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, InstanceList>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.InstanceList");
    }
}

bool NodeMetaInfo::isQtQuick3DParticles3DParticle3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Particles3D.Particle3D",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D_Particles3D, Particle3D>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Particles3D.Particle3D");
    }
}

bool NodeMetaInfo::isQtQuick3DParticles3DParticleEmitter3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Particles3D.ParticleEmitter3D",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D_Particles3D, ParticleEmitter3D>(m_projectStorage,
                                                                             m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Particles3D.ParticleEmitter3D");
    }
}

bool NodeMetaInfo::isQtQuick3DParticles3DAttractor3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Particles3D.Attractor3D",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D_Particles3D, Attractor3D>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Particles3D.Attractor3D");
    }
}

bool NodeMetaInfo::isQtQuick3DParticlesAbstractShape(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Particles3D.AbstractShape",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D_Particles3D, QQuick3DParticleAbstractShape, ModuleKind::CppLibrary>(
            m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QQuick3DParticleAbstractShape");
    }
}

bool NodeMetaInfo::isQtQuickItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Item",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Item>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Item");
    }
}

bool NodeMetaInfo::isQtQuickPath(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Path",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Path>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Path");
    }
}

bool NodeMetaInfo::isQtQuickPauseAnimation(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.PauseAnimation",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, PauseAnimation>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.PauseAnimation");
    }
}

bool NodeMetaInfo::isQtQuickTransition(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Transition",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Transition>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Transition");
    }
}

bool NodeMetaInfo::isQtQuickWindowWindow(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Window.Window",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Window, Window>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Window.Window");
    }
}

bool NodeMetaInfo::isQtQuickLoader(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Loader",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Loader>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Loader");
    }
}

bool NodeMetaInfo::isQtQuickState(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.State",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, State>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.State");
    }
}

bool NodeMetaInfo::isQtQuickStateGroup(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.StateGroup",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, StateGroup>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.StateGroup");
    }
}

bool NodeMetaInfo::isQtQuickStateOperation(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.StateOperation",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, QQuickStateOperation, ModuleKind::CppLibrary>(m_projectStorage,
                                                                                          m_typeId);
    } else {
        return isValid() && isSubclassOf("<cpp>.QQuickStateOperation");
    }
}

bool NodeMetaInfo::isQtQuickStudioComponentsArcItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Studio.Components.ArcItem",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Studio_Components, ArcItem>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Studio.Components.ArcItem");
    }
}

bool NodeMetaInfo::isQtQuickText(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Text",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick, Text>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Text");
    }
}

bool NodeMetaInfo::isQtMultimediaSoundEffect(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtMultimedia.SoundEffect",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtMultimedia, SoundEffect>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtMultimedia.SoundEffect");
    }
}

bool NodeMetaInfo::isFlowViewItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is FlowView.ViewItem",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        auto flowItemId = m_projectStorage->commonTypeId<FlowView, FlowItem>();
        auto flowWildcardId = m_projectStorage->commonTypeId<FlowView, FlowWildcard>();
        auto flowDecisionId = m_projectStorage->commonTypeId<FlowView, FlowDecision>();
        return m_projectStorage->isBasedOn(m_typeId, flowItemId, flowWildcardId, flowDecisionId);
    } else {
        return isValid()
               && (isSubclassOf("FlowView.FlowItem") || isSubclassOf("FlowView.FlowWildcard")
                   || isSubclassOf("FlowView.FlowDecision"));
    }
}

bool NodeMetaInfo::isFlowViewFlowItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is FlowView.FlowItem",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<FlowView, FlowItem>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("FlowView.FlowItem");
    }
}

bool NodeMetaInfo::isFlowViewFlowView(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is FlowView.FlowView",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<FlowView, FlowView>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("FlowView.FlowView");
    }
}

bool NodeMetaInfo::isFlowViewFlowActionArea() const
{
    if constexpr (useProjectStorage()) {
        using namespace Storage::Info;
        return isBasedOnCommonType<FlowView, FlowActionArea>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("FlowView.FlowActionArea");
    }
}

bool NodeMetaInfo::isFlowViewFlowTransition(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is FlowView.FlowTransition",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<FlowView, FlowTransition>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("FlowView.FlowTransition");
    }
}

bool NodeMetaInfo::isFlowViewFlowDecision(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is FlowView.FlowDecision",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<FlowView, FlowDecision>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("FlowView.FlowDecision");
    }
}

bool NodeMetaInfo::isFlowViewFlowWildcard(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is FlowView.FlowWildcard",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<FlowView, FlowWildcard>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("FlowView.FlowWildcard");
    }
}

bool NodeMetaInfo::isQtQuickStudioComponentsGroupItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Studio.Components.GroupItem",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Studio_Components, GroupItem>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Studio.Components.GroupItem");
    }
}

bool NodeMetaInfo::isQtQuickStudioComponentsSvgPathItem(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Studio.Components.SvgPathItem",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Studio_Components, SvgPathItem>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Studio.Components.SvgPathItem");
    }
}

bool NodeMetaInfo::isQtQuickStudioUtilsJsonListModel(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick.Studio.Utils.JsonListModel",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick_Studio_Components, JsonListModel>(m_projectStorage,
                                                                             m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick.Studio.Utils.JsonListModel");
    }
}

bool NodeMetaInfo::isQmlComponent([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is QML.Component",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isBasedOnCommonType<QML, Component>(m_projectStorage, m_typeId);
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "Component" || type == "QQmlComponent";
#endif
}

bool NodeMetaInfo::isFont([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is font",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->commonTypeId<QtQuick, font>());
#else
    return isValid() && simplifiedTypeName() == "font";
#endif
}

bool NodeMetaInfo::isColor([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is color",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->builtinTypeId<QColor>());
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "QColor" || type == "color" || type == "color";
#endif
}

bool NodeMetaInfo::isBool([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is bool",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->builtinTypeId<bool>());
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "bool" || type == "boolean";
#endif
}

bool NodeMetaInfo::isInteger([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is integer",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->builtinTypeId<int>());
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "int" || type == "integer" || type == "uint";
#endif
}

bool NodeMetaInfo::isFloat([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is float",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    auto floatId = m_projectStorage->builtinTypeId<float>();
    auto doubleId = m_projectStorage->builtinTypeId<double>();

    return isTypeId(m_typeId, floatId, doubleId);
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "qreal" || type == "double" || type == "float" || type == "real";
#endif
}

bool NodeMetaInfo::isVariant([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is variant",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->builtinTypeId<QVariant>());
#else
    if (!isValid())
        return false;

    const auto type = simplifiedTypeName();

    return type == "QVariant" || type == "var" || type == "variant";
#endif
}

bool NodeMetaInfo::isString([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is string",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->builtinTypeId<QString>());
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "string" || type == "QString";
#endif
}

bool NodeMetaInfo::isUrl([[maybe_unused]] SL sl) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return false;

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"is url",
                               category(),
                               keyValue("type id", m_typeId),
                               keyValue("caller location", sl)};

    using namespace Storage::Info;
    return isValid() && isTypeId(m_typeId, m_projectStorage->builtinTypeId<QUrl>());
#else
    if (!isValid())
        return false;

    auto type = simplifiedTypeName();

    return type == "url" || type == "QUrl";
#endif
}

bool NodeMetaInfo::isQtQuick3DTexture(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Texture",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Texture>(m_projectStorage, m_typeId);
    } else {
        return isValid()
               && (isSubclassOf("QtQuick3D.Texture") || isSubclassOf("<cpp>.QQuick3DTexture"));
    }
}

bool NodeMetaInfo::isQtQuick3DShader(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Shader",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Shader>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Shader");
    }
}

bool NodeMetaInfo::isQtQuick3DPass(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Pass",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Pass>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Pass");
    }
}

bool NodeMetaInfo::isQtQuick3DCommand(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Command",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Command>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Command");
    }
}

bool NodeMetaInfo::isQtQuick3DDefaultMaterial(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.DefaultMaterial",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, DefaultMaterial>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.DefaultMaterial");
    }
}

bool NodeMetaInfo::isQtQuick3DMaterial() const
{
    if constexpr (useProjectStorage()) {
        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Material>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Material");
    }
}

bool NodeMetaInfo::isQtQuick3DModel(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Model",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Storage::Info::Model>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Model");
    }
}

bool NodeMetaInfo::isQtQuick3DNode(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Node",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Node>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Node");
    }
}

bool NodeMetaInfo::isQtQuick3DParticles3DAffector3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Particles3D.Affector3D",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D_Particles3D, Affector3D>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Affector3D");
    }
}

bool NodeMetaInfo::isQtQuick3DView3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.View3D",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, View3D>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.View3D");
    }
}

bool NodeMetaInfo::isQtQuick3DPrincipledMaterial(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.PrincipledMaterial",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, PrincipledMaterial>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.PrincipledMaterial");
    }
}

bool NodeMetaInfo::isQtQuick3DSpecularGlossyMaterial(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.SpecularGlossyMaterial",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, SpecularGlossyMaterial>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.SpecularGlossyMaterial");
    }
}

bool NodeMetaInfo::isQtQuick3DParticles3DSpriteParticle3D(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Particles3D.SpriteParticle3D",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D_Particles3D, SpriteParticle3D>(m_projectStorage,
                                                                            m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Particles3D.SpriteParticle3D");
    }
}

bool NodeMetaInfo::isQtQuick3DTextureInput(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.TextureInput",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, TextureInput>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.TextureInput");
    }
}

bool NodeMetaInfo::isQtQuick3DCubeMapTexture(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.CubeMapTexture",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, CubeMapTexture>(m_projectStorage, m_typeId);
    } else {
        return isValid()
               && (isSubclassOf("QtQuick3D.CubeMapTexture")
                   || isSubclassOf("<cpp>.QQuick3DCubeMapTexture"));
    }
}

bool NodeMetaInfo::isQtQuick3DSceneEnvironment(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.SceneEnvironment",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, SceneEnvironment>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.SceneEnvironment");
    }
}

bool NodeMetaInfo::isQtQuick3DEffect(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is QtQuick3D.Effect",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        using namespace Storage::Info;
        return isBasedOnCommonType<QtQuick3D, Effect>(m_projectStorage, m_typeId);
    } else {
        return isValid() && isSubclassOf("QtQuick3D.Effect");
    }
}

bool NodeMetaInfo::isEnumeration(SL sl) const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return false;

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is enumeration",
                                   category(),
                                   keyValue("type id", m_typeId),
                                   keyValue("caller location", sl)};

        return typeData().traits.isEnum;
    }

    return false;
}

PropertyMetaInfo::PropertyMetaInfo() = default;
PropertyMetaInfo::PropertyMetaInfo(const PropertyMetaInfo &) = default;
PropertyMetaInfo &PropertyMetaInfo::operator=(const PropertyMetaInfo &) = default;
PropertyMetaInfo::PropertyMetaInfo(PropertyMetaInfo &&) = default;
PropertyMetaInfo &PropertyMetaInfo::operator=(PropertyMetaInfo &&) = default;

PropertyMetaInfo::PropertyMetaInfo(
    [[maybe_unused]] std::shared_ptr<NodeMetaInfoPrivate> nodeMetaInfoPrivateData,
    [[maybe_unused]] PropertyNameView propertyName)
#ifndef QDS_USE_PROJECTSTORAGE
    : m_nodeMetaInfoPrivateData{nodeMetaInfoPrivateData}
    , m_propertyName{propertyName.toByteArray()}
#endif
{}

PropertyMetaInfo::~PropertyMetaInfo() = default;

NodeMetaInfo PropertyMetaInfo::propertyType() const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return {};

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get property type",
                               category(),
                               keyValue("property declaration id", m_id)};

    return {propertyData().propertyTypeId, m_projectStorage};
#else
    if (isValid())
        return NodeMetaInfo{nodeMetaInfoPrivateData()->model(),
                            nodeMetaInfoPrivateData()->propertyType(propertyName()),
                            -1,
                            -1};
#endif

    return {};
}

NodeMetaInfo PropertyMetaInfo::type() const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (!isValid())
        return {};

    using NanotraceHR::keyValue;
    NanotraceHR::Tracer tracer{"get property owner type ",
                               category(),
                               keyValue("property declaration id", m_id)};

    return NodeMetaInfo(propertyData().typeId, m_projectStorage);
#endif

    return {};
}

PropertyName PropertyMetaInfo::name() const
{
    if (!isValid())
        return {};

    if constexpr (useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"get property name",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        return propertyData().name.toQByteArray();
    } else {
        return propertyName();
    }
}

bool PropertyMetaInfo::isWritable() const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is property writable",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        return !(propertyData().traits & Storage::PropertyDeclarationTraits::IsReadOnly);
    } else {
        return isValid() && nodeMetaInfoPrivateData()->isPropertyWritable(propertyName());
    }
}

bool PropertyMetaInfo::isReadOnly() const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is property read only",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        return propertyData().traits & Storage::PropertyDeclarationTraits::IsReadOnly;
    } else {
        return !isWritable();
    }
}

bool PropertyMetaInfo::isListProperty() const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is list property",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        return propertyData().traits & Storage::PropertyDeclarationTraits::IsList;
    } else {
        return isValid() && nodeMetaInfoPrivateData()->isPropertyList(propertyName());
    }
}

bool PropertyMetaInfo::isEnumType() const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is enum type",
                                   category(),
                                   keyValue("property has enumeration type", m_id)};

        return propertyType().isEnumeration();
    } else {
        return isValid() && nodeMetaInfoPrivateData()->isPropertyEnum(propertyName());
    }
}

bool PropertyMetaInfo::isPrivate() const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is private property",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        return isValid() && propertyData().name.startsWith("__");
    } else {
        return isValid() && propertyName().startsWith("__");
    }
}

bool PropertyMetaInfo::isPointer() const
{
    if constexpr (useProjectStorage()) {
        if (!isValid())
            return {};

        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"is pointer property",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        return isValid() && (propertyData().traits & Storage::PropertyDeclarationTraits::IsPointer);
    } else {
        return isValid() && nodeMetaInfoPrivateData()->isPropertyPointer(propertyName());
    }
}

namespace {
template<typename... QMetaTypes>
bool isType(const QMetaType &type, const QMetaTypes &...types)
{
    return ((type == types) || ...);
}
} // namespace

QVariant PropertyMetaInfo::castedValue(const QVariant &value) const
{
    if (!isValid())
        return {};

    if constexpr (!useProjectStorage()) {
        using NanotraceHR::keyValue;
        NanotraceHR::Tracer tracer{"cast value",
                                   category(),
                                   keyValue("property declaration id", m_id)};

        const QVariant variant = value;
        QVariant copyVariant = variant;
        const TypeName &typeName = propertyTypeName();
        // skip casting flags and keep them as int. TODO: use flags as enums
        if (isEnumType() || variant.canConvert<Enumeration>() || typeName.endsWith("Flags"))
            return variant;

        QMetaType::Type typeId = nodeMetaInfoPrivateData()->variantTypeId(propertyName());

        if (variant.typeId() == ModelNode::variantTypeId()) {
            return variant;
        } else if (typeId == QMetaType::User && typeName == "QVariant") {
            return variant;
        } else if (typeId == QMetaType::User && typeName == "variant") {
            return variant;
        } else if (typeId == QMetaType::User && typeName == "var") {
            return variant;
        } else if (variant.typeId() == QMetaType::QVariantList) {
            // TODO: check the contents of the list
            return variant;
        } else if (typeName == "var" || typeName == "variant") {
            return variant;
        } else if (typeName == "alias") {
            // TODO: The QML compiler resolves the alias type. We probably should do the same.
            return variant;
        } else if (typeName == "<cpp>.double") {
            return variant.toDouble();
        } else if (typeName == "<cpp>.float") {
            return variant.toFloat();
        } else if (typeName == "<cpp>.int") {
            return variant.toInt();
        } else if (typeName == "<cpp>.bool") {
            return variant.toBool();
        } else if (copyVariant.convert(typeId)) {
            return copyVariant;
        }

    } else {
        if (isEnumType() && value.canConvert<Enumeration>())
            return value;

        const TypeId &typeId = propertyData().propertyTypeId;

        static constexpr auto boolType = QMetaType::fromType<bool>();
        static constexpr auto intType = QMetaType::fromType<int>();
        static constexpr auto longType = QMetaType::fromType<long>();
        static constexpr auto longLongType = QMetaType::fromType<long long>();
        static constexpr auto floatType = QMetaType::fromType<float>();
        static constexpr auto doubleType = QMetaType::fromType<double>();
        static constexpr auto qStringType = QMetaType::fromType<QString>();
        static constexpr auto qUrlType = QMetaType::fromType<QUrl>();
        static constexpr auto qColorType = QMetaType::fromType<QColor>();

        if (value.typeId() == QMetaType::User && value.typeId() == ModelNode::variantTypeId()) {
            return value;
        } else if (typeId == m_projectStorage->builtinTypeId<QVariant>()) {
            return value;
        } else if (typeId == m_projectStorage->builtinTypeId<double>()) {
            return value.toDouble();
        } else if (typeId == m_projectStorage->builtinTypeId<float>()) {
            return value.toFloat();
        } else if (typeId == m_projectStorage->builtinTypeId<int>()) {
            return value.toInt();
        } else if (typeId == m_projectStorage->builtinTypeId<bool>()) {
            return isType(value.metaType(), boolType, intType, longType, longLongType, floatType, doubleType)
                   && value.toBool();
        } else if (typeId == m_projectStorage->builtinTypeId<QString>()) {
            if (isType(value.metaType(), qStringType))
                return value;
            else
                return QString{};
        } else if (typeId == m_projectStorage->builtinTypeId<QDateTime>()) {
            return value.toDateTime();
        } else if (typeId == m_projectStorage->builtinTypeId<QUrl>()) {
            if (isType(value.metaType(), qUrlType))
                return value;
            else if (isType(value.metaType(), qStringType))
                return value.toUrl();
            else
                return QUrl{};
        } else if (typeId == m_projectStorage->builtinTypeId<QColor>()) {
            if (isType(value.metaType(), qColorType))
                return value;
            else
                return QColor{};
        } else if (typeId == m_projectStorage->builtinTypeId<QVector2D>()) {
            return value.value<QVector2D>();
        } else if (typeId == m_projectStorage->builtinTypeId<QVector3D>()) {
            return value.value<QVector3D>();
        } else if (typeId == m_projectStorage->builtinTypeId<QVector4D>()) {
            return value.value<QVector4D>();
        }
    }

    return {};
}

const Storage::Info::PropertyDeclaration &PropertyMetaInfo::propertyData() const
{
    if (!m_propertyData)
        m_propertyData = m_projectStorage->propertyDeclaration(m_id);

    return *m_propertyData;
}

TypeName PropertyMetaInfo::propertyTypeName() const
{
#ifndef QDS_USE_PROJECTSTORAGE
    return propertyType().typeName();
#else
    return {};
#endif
}

const NodeMetaInfoPrivate *PropertyMetaInfo::nodeMetaInfoPrivateData() const
{
#ifndef QDS_USE_PROJECTSTORAGE
    return m_nodeMetaInfoPrivateData.get();
#else
    return nullptr;
#endif
}

const PropertyName &PropertyMetaInfo::propertyName() const
{
#ifndef QDS_USE_PROJECTSTORAGE
    return m_propertyName;
#else
    static PropertyName dummy;
    return dummy;
#endif
}

NodeMetaInfo NodeMetaInfo::commonBase(const NodeMetaInfo &metaInfo) const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (isValid() && metaInfo) {
        const auto firstTypeIds = m_projectStorage->prototypeAndSelfIds(m_typeId);
        const auto secondTypeIds = m_projectStorage->prototypeAndSelfIds(metaInfo.m_typeId);
        auto found = std::ranges::find_if(firstTypeIds, [&](TypeId firstTypeId) {
            return std::ranges::find(secondTypeIds, firstTypeId) != secondTypeIds.end();
        });

        if (found != firstTypeIds.end())
            return NodeMetaInfo{*found, m_projectStorage};
    }
#else
    for (const NodeMetaInfo &info : metaInfo.selfAndPrototypes()) {
        if (isBasedOn(info)) {
            return info;
        }
    }
#endif

    return {};
}

NodeMetaInfo::NodeMetaInfos NodeMetaInfo::heirs() const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (isValid()) {
        return Utils::transform<NodeMetaInfos>(m_projectStorage->heirIds(m_typeId),
                                               NodeMetaInfo::bind(m_projectStorage));
    }
#endif

    return {};
}

namespace {

void addCompoundProperties(CompoundPropertyMetaInfos &inflatedProperties,
                           const PropertyMetaInfo &parentProperty,
                           PropertyMetaInfos properties)
{
    for (PropertyMetaInfo &property : properties)
        inflatedProperties.emplace_back(std::move(property), parentProperty);
}

bool maybeCanHaveProperties(const NodeMetaInfo &type)
{
    if (!type)
        return false;

    using namespace Storage::Info;
    const auto &cache = type.projectStorage().commonTypeCache();
    auto typeId = type.id();
    const auto &typeIdsWithoutProperties = cache.typeIdsWithoutProperties();
    const auto begin = typeIdsWithoutProperties.begin();
    const auto end = typeIdsWithoutProperties.end();

    return std::find(begin, end, typeId) == end;
}

void addSubProperties(CompoundPropertyMetaInfos &inflatedProperties,
                      PropertyMetaInfo &propertyMetaInfo,
                      const NodeMetaInfo &propertyType)
{
    if (maybeCanHaveProperties(propertyType)) {
        auto subProperties = propertyType.properties();
        if (!subProperties.empty()) {
            addCompoundProperties(inflatedProperties, propertyMetaInfo, subProperties);
            return;
        }
    }

    inflatedProperties.emplace_back(std::move(propertyMetaInfo));
}

bool isValueOrNonListReadOnlyReference(const NodeMetaInfo &propertyType,
                                       const PropertyMetaInfo &property)
{
    return propertyType.type() == MetaInfoType::Value
            || (property.isReadOnly() && !property.isListProperty());
}

} // namespace

CompoundPropertyMetaInfos MetaInfoUtils::inflateValueProperties(PropertyMetaInfos properties)
{
    CompoundPropertyMetaInfos inflatedProperties;
    inflatedProperties.reserve(properties.size() * 2);

    for (auto &property : properties) {
        if (auto propertyType = property.propertyType(); propertyType.type() == MetaInfoType::Value)
            addSubProperties(inflatedProperties, property, propertyType);
        else
            inflatedProperties.emplace_back(std::move(property));
    }

    return inflatedProperties;
}

CompoundPropertyMetaInfos MetaInfoUtils::inflateValueAndReadOnlyProperties(PropertyMetaInfos properties)
{
    CompoundPropertyMetaInfos inflatedProperties;
    inflatedProperties.reserve(properties.size() * 2);

    for (auto &property : properties) {
        if (auto propertyType = property.propertyType(); isValueOrNonListReadOnlyReference(propertyType, property))
            addSubProperties(inflatedProperties, property, propertyType);
        else
            inflatedProperties.emplace_back(std::move(property));
    }

    return inflatedProperties;
}

CompoundPropertyMetaInfos MetaInfoUtils::addInflatedValueAndReadOnlyProperties(PropertyMetaInfos properties)
{
    CompoundPropertyMetaInfos inflatedProperties;
    inflatedProperties.reserve(properties.size() * 2);

    for (auto &property : properties) {
        if (auto propertyType = property.propertyType(); isValueOrNonListReadOnlyReference(propertyType, property)) {
            addSubProperties(inflatedProperties, property, propertyType);
            if (!property.isReadOnly())
                inflatedProperties.emplace_back(std::move(property));
        } else {
            inflatedProperties.emplace_back(std::move(property));
        }
    }

    return inflatedProperties;
}

} // namespace QmlDesigner

QT_WARNING_POP
