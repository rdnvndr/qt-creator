// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "nodehints.h"
#include "model.h"

#include "metainfo.h"
#include <enumeration.h>
#include <itemlibraryentry.h>
#include <nodeabstractproperty.h>
#include <nodemetainfo.h>
#include <propertyparser.h>
#include <rewriterview.h>

#include <QDebug>

#include <qmljs/qmljsscopechain.h>
#include <qmljs/parser/qmljsast_p.h>
#include <qmljs/qmljsmodelmanagerinterface.h>
#include <qmljs/qmljsvalueowner.h>
#include <languageutils/fakemetaobject.h>

#include <utils/qtcassert.h>

#ifndef QDS_USE_PROJECTSTORAGE
#  include <itemlibraryinfo.h>
#endif

#include <QJSEngine>

#include <memory>
#include <mutex>

namespace QmlDesigner {

static bool isSwipeView(const ModelNode &node)
{
    if (node.metaInfo().isQtQuickControlsSwipeView())
        return true;

    return false;
}

namespace Internal {

static std::once_flag s_singletonFlag;
static std::unique_ptr<QJSEngine> s_qJSEngine;
static JSObject *s_jsObject = nullptr;

static QVariant evaluateExpression(const QString &expression, const ModelNode &modelNode, const ModelNode &otherNode)
{
    std::call_once(s_singletonFlag, []() {
        s_qJSEngine.reset(new QJSEngine);
        s_jsObject = new JSObject(s_qJSEngine.get());
        QJSValue jsValue = s_qJSEngine->newQObject(s_jsObject);
        s_qJSEngine->globalObject().setProperty("model", jsValue);
    });

    s_jsObject->setModelNode(modelNode);
    s_jsObject->setOtherNode(otherNode);

    QJSValue value = s_qJSEngine->evaluate(expression);

    if (value.isError())
        return expression;
    return s_qJSEngine->evaluate(expression).toVariant();
}

} //Internal

QmlDesigner::NodeHints::NodeHints(const ModelNode &node)
#ifdef QDS_USE_PROJECTSTORAGE
    : NodeHints{node.metaInfo()}
#endif
{
    m_modelNode = node;
#ifndef QDS_USE_PROJECTSTORAGE

    if (!isValid())
        return;

    const ItemLibraryInfo *libraryInfo = model()->metaInfo().itemLibraryInfo();

    if (!m_modelNode.metaInfo().isValid()) {

        QList <ItemLibraryEntry> itemLibraryEntryList = libraryInfo->entriesForType(
                    modelNode().type(), modelNode().majorVersion(), modelNode().minorVersion());

        if (!itemLibraryEntryList.isEmpty())
            m_hints = itemLibraryEntryList.constFirst().hints();
    } else { /* If we have meta information we run the complete type hierarchy and check for hints */
        const auto classHierarchy = m_modelNode.metaInfo().selfAndPrototypes();
        for (const NodeMetaInfo &metaInfo : classHierarchy) {
            QList <ItemLibraryEntry> itemLibraryEntryList = libraryInfo->entriesForType(
                        metaInfo.typeName(), metaInfo.majorVersion(), metaInfo.minorVersion());

            if (!itemLibraryEntryList.isEmpty() && !itemLibraryEntryList.constFirst().hints().isEmpty()) {
                m_hints = itemLibraryEntryList.constFirst().hints();
                return;
            }

        }
    }
#endif
}

NodeHints::NodeHints(const NodeMetaInfo &metaInfo)
    : m_metaInfo{metaInfo}
{
}

NodeHints::NodeHints(const ItemLibraryEntry &entry, [[maybe_unused]] Model *model)
#ifdef QDS_USE_PROJECTSTORAGE
    : NodeHints{NodeMetaInfo{entry.typeId(), model->projectStorage()}}
#endif
{
    if constexpr (!useProjectStorage())
        m_hints = entry.hints();
}

namespace {
bool convert(FlagIs flagIs)
{
    return flagIs == FlagIs::True;
}
} // namespace

bool NodeHints::canBeContainerFor(const ModelNode &potenialChild) const
{
    /* The default is true for now to avoid confusion. Once our .metaInfo files in Qt
       use the feature we can change the default to false. */

    if (!isValid())
        return true;

    auto flagIs = m_metaInfo.canBeContainer();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("canBeContainer", true, potenialChild);
}

bool NodeHints::forceClip() const
{
    if (!isValid())
        return false;

    if (isSwipeView(modelNode()))
        return true;

    auto flagIs = m_metaInfo.forceClip();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("forceClip", false);
}

bool NodeHints::doesLayoutChildren() const
{
    if (!isValid())
        return false;

    if (isSwipeView(modelNode()))
        return true;

    auto flagIs = m_metaInfo.doesLayoutChildren();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("doesLayoutChildren", false);
}

bool NodeHints::canBeDroppedInFormEditor() const
{
    auto flagIs = m_metaInfo.canBeDroppedInFormEditor();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("canBeDroppedInFormEditor", true);
}

bool NodeHints::canBeDroppedInNavigator() const
{
    auto flagIs = m_metaInfo.canBeDroppedInNavigator();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("canBeDroppedInNavigator", true);
}

bool NodeHints::canBeDroppedInView3D() const
{
    auto flagIs = m_metaInfo.canBeDroppedInView3D();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("canBeDroppedInView3D", false);
}

bool NodeHints::isMovable() const
{
    if (!isValid())
        return true;

    auto flagIs = m_metaInfo.isMovable();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("isMovable", true);
}

bool NodeHints::isResizable() const
{
    if (!isValid())
        return true;

    auto flagIs = m_metaInfo.isResizable();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("isResizable", true);
}

bool NodeHints::hasFormEditorItem() const
{
    if (!isValid())
        return true;

    auto flagIs = m_metaInfo.hasFormEditorItem();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("hasFormEditorItem", true);
}

bool NodeHints::isStackedContainer() const
{
    if (!isValid())
        return false;

    if (isSwipeView(modelNode()))
        return true;

    auto flagIs = m_metaInfo.isStackedContainer();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("isStackedContainer", false);
}

bool NodeHints::canBeReparentedTo(const ModelNode &potenialParent)
{
    if (!isValid())
        return true;

    return evaluateBooleanExpression("canBeReparented", true, potenialParent);
}

QString NodeHints::indexPropertyForStackedContainer() const
{
    if (!isValid())
        return QString();

    const QString expression = hints().value("indexPropertyForStackedContainer");

    if (expression.isEmpty())
        return QString();

    return Internal::evaluateExpression(expression, modelNode(), ModelNode()).toString();
}

QStringList NodeHints::visibleNonDefaultProperties() const
{
    if (!isValid())
        return {};

    const QString expression = hints().value("visibleNonDefaultProperties");

    if (expression.isEmpty())
        return {};

    return Internal::evaluateExpression(expression, modelNode(), ModelNode()).toString().split(",");
}

bool NodeHints::takesOverRenderingOfChildren() const
{
    if (!isValid())
        return false;

    auto flagIs = m_metaInfo.takesOverRenderingOfChildren();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("takesOverRenderingOfChildren", false);
}

bool NodeHints::visibleInNavigator() const
{
    if (!isValid())
        return false;

    auto flagIs = m_metaInfo.visibleInNavigator();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("visibleInNavigator", false);
}

bool NodeHints::hideInNavigator() const
{
    if (!isValid())
        return false;

    auto flagIs = m_metaInfo.hideInNavigator();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("hideInNavigator", false);
}

bool NodeHints::visibleInLibrary() const
{
    auto flagIs = m_metaInfo.visibleInLibrary();

    if (flagIs != FlagIs::Set)
        return convert(flagIs);

    return evaluateBooleanExpression("visibleInLibrary", true);
}

QString NodeHints::forceNonDefaultProperty() const
{
    const QString expression = hints().value("forceNonDefaultProperty");

    if (expression.isEmpty())
        return {};

    return Internal::evaluateExpression(expression, modelNode(), ModelNode()).toString();
}

QVariant parseValue(const QString &string)
{
    if (string == "true")
        return true;
    if (string == "false")
        return false;
    bool ok = false;
    double d = string.toDouble(&ok);
    if (ok)
        return d;

    return string;
}

QPair<QString, QVariant> NodeHints::setParentProperty() const
{
    const QString expression = hints().value("setParentProperty");

    if (expression.isEmpty())
        return {};

    const QString str = Internal::evaluateExpression(expression, modelNode(), ModelNode()).toString();

    QStringList list = str.split(":");

    if (list.size() != 2)
        return {};

    return qMakePair(list.first().trimmed(), parseValue(list.last().trimmed()));
}

QString NodeHints::bindParentToProperty() const
{
    const QString expression = hints().value("bindParentToProperty");

    if (expression.isEmpty())
        return {};

    return Internal::evaluateExpression(expression, modelNode(), ModelNode()).toString();
}

QHash<QString, QString> NodeHints::hints() const
{
#ifdef QDS_USE_PROJECTSTORAGE
    if (m_hints.empty()) {
        for (const auto &[name, expression] : m_metaInfo.typeHints())
            m_hints.insert(name.toQString(), expression.toQString());
    }
#endif

    return m_hints;
}

NodeHints NodeHints::fromModelNode(const ModelNode &modelNode)
{
    return NodeHints(modelNode);
}

NodeHints NodeHints::fromItemLibraryEntry(const ItemLibraryEntry &entry, Model *model)
{
    return NodeHints(entry, model);
}

const ModelNode &NodeHints::modelNode() const
{
    return m_modelNode;
}

bool NodeHints::isValid() const
{
    return modelNode().isValid();
}

Model *NodeHints::model() const
{
    return modelNode().model();
}

bool NodeHints::evaluateBooleanExpression(const QString &hintName, bool defaultValue, const ModelNode otherNode) const
{
    const QString expression = hints().value(hintName);

    if (expression.isEmpty())
        return defaultValue;

    return Internal::evaluateExpression(expression, modelNode(), otherNode).toBool();
}

namespace Internal {

JSObject::JSObject(QObject *parent) : QObject (parent)
{

}

void JSObject::setModelNode(const ModelNode &node)
{
    m_modelNode = node;
    emit modelNodeChanged();
}

void JSObject::setOtherNode(const ModelNode &node)
{
    m_otherNode = node;
    emit otherNodeChanged();
}

bool JSObject::hasParent() const
{
    return !m_modelNode.isRootNode()
            && m_modelNode.hasParentProperty();
}

bool JSObject::hasChildren() const
{
    return m_modelNode.hasAnySubModelNodes();
}

bool JSObject::currentParentIsRoot() const
{
    return m_modelNode.hasParentProperty()
            && m_modelNode.parentProperty().isValid()
            && m_modelNode.parentProperty().parentModelNode().isRootNode();
}

bool JSObject::potentialParentIsRoot() const
{
    return m_otherNode.isValid() && m_otherNode.isRootNode();
}

bool JSObject::potentialChildIsRoot() const
{
    return m_otherNode.isValid() && m_otherNode.isRootNode();
}

bool JSObject::isSubclassOf(const QString &typeName)
{
    NodeMetaInfo metaInfo = m_modelNode.metaInfo();
    auto model = m_modelNode.model();

    auto base = model->metaInfo(typeName.toUtf8());

    return metaInfo.isBasedOn(base);
}

bool JSObject::rootItemIsSubclassOf(const QString &typeName)
{
    NodeMetaInfo metaInfo = m_modelNode.view()->rootModelNode().metaInfo();

    auto model = m_modelNode.model();

    auto base = model->metaInfo(typeName.toUtf8());

    return metaInfo.isBasedOn(base);
}

bool JSObject::currentParentIsSubclassOf(const QString &typeName)
{
    if (m_modelNode.hasParentProperty()
            && m_modelNode.parentProperty().isValid()) {
        NodeMetaInfo metaInfo = m_modelNode.parentProperty().parentModelNode().metaInfo();
        auto model = m_modelNode.model();
        auto base = model->metaInfo(typeName.toUtf8());

        return metaInfo.isBasedOn(base);
    }

    return false;
}

bool JSObject::potentialParentIsSubclassOf(const QString &typeName)
{
    NodeMetaInfo metaInfo = m_otherNode.metaInfo();

    auto model = m_modelNode.model();

    auto base = model->metaInfo(typeName.toUtf8());

    return metaInfo.isBasedOn(base);
}

bool JSObject::potentialChildIsSubclassOf(const QString &typeName)
{
    NodeMetaInfo metaInfo = m_otherNode.metaInfo();

    auto model = m_otherNode.model();

    auto base = model->metaInfo(typeName.toUtf8());

    return metaInfo.isBasedOn(base);
}

} //Internal

} // namespace QmlDesigner
