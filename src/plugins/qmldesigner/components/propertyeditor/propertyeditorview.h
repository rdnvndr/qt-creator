// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "abstractview.h"
#include "qmldesigner_global.h"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <propertyeditorcomponentgenerator.h>

QT_BEGIN_NAMESPACE
class QEvent;
class QShortcut;
class QStackedWidget;
class QTimer;
QT_END_NAMESPACE

namespace QmlDesigner {

class CollapseButton;
class DynamicPropertiesModel;
class ModelNode;
class PropertyEditorQmlBackend;
class PropertyEditorView;
class PropertyEditorWidget;
class QmlObjectNode;

class QMLDESIGNER_EXPORT PropertyEditorView : public AbstractView
{
    Q_OBJECT

public:
    PropertyEditorView(class AsynchronousImageCache &imageCache,
                       ExternalDependenciesInterface &externalDependencies);
    ~PropertyEditorView() override;

    bool hasWidget() const override;
    WidgetInfo widgetInfo() override;

    void selectedNodesChanged(const QList<ModelNode> &selectedNodeList,
                              const QList<ModelNode> &lastSelectedNodeList) override;
    void nodeAboutToBeRemoved(const ModelNode &removedNode) override;
    void nodeRemoved(const ModelNode &removedNode,
                     const NodeAbstractProperty &parentProperty,
                     PropertyChangeFlags propertyChange) override;
    void propertiesRemoved(const QList<AbstractProperty>& propertyList) override;
    void propertiesAboutToBeRemoved(const QList<AbstractProperty> &propertyList) override;

    void modelAttached(Model *model) override;

    void modelAboutToBeDetached(Model *model) override;

    void variantPropertiesChanged(const QList<VariantProperty>& propertyList, PropertyChangeFlags propertyChange) override;
    void bindingPropertiesChanged(const QList<BindingProperty>& propertyList, PropertyChangeFlags propertyChange) override;
    void auxiliaryDataChanged(const ModelNode &node,
                              AuxiliaryDataKeyView key,
                              const QVariant &data) override;

    void signalDeclarationPropertiesChanged(const QVector<SignalDeclarationProperty> &propertyList,
                                            PropertyChangeFlags propertyChange) override;

    void instanceInformationsChanged(const QMultiHash<ModelNode, InformationName> &informationChangedHash) override;

    void nodeIdChanged(const ModelNode& node, const QString& newId, const QString& oldId) override;

    void resetView();
    void currentStateChanged(const ModelNode &node) override;
    void instancePropertyChanged(const QList<QPair<ModelNode, PropertyName> > &propertyList) override;

    void rootNodeTypeChanged(const QString &type, int majorVersion, int minorVersion) override;
    void nodeTypeChanged(const ModelNode& node, const TypeName &type, int majorVersion, int minorVersion) override;

    void nodeReparented(const ModelNode &node,
                        const NodeAbstractProperty &newPropertyParent,
                        const NodeAbstractProperty &oldPropertyParent,
                        AbstractView::PropertyChangeFlags propertyChange) override;

    void modelNodePreviewPixmapChanged(const ModelNode &node,
                                       const QPixmap &pixmap,
                                       const QByteArray &requestId) override;

    void importsChanged(const Imports &addedImports, const Imports &removedImports) override;
    void customNotification(const AbstractView *view,
                            const QString &identifier,
                            const QList<ModelNode> &nodeList,
                            const QList<QVariant> &data) override;

    void dragStarted(QMimeData *mimeData) override;
    void dragEnded() override;

    void changeValue(const QString &name);
    void changeExpression(const QString &name);
    void exportPropertyAsAlias(const QString &name);
    void removeAliasExport(const QString &name);

    bool locked() const;

    void currentTimelineChanged(const ModelNode &node) override;

    void refreshMetaInfos(const TypeIds &deletedTypeIds) override;

    DynamicPropertiesModel *dynamicPropertiesModel() const;

    static void setExpressionOnObjectNode(const QmlObjectNode &objectNode,
                                          PropertyNameView name,
                                          const QString &expression);

    static void generateAliasForProperty(const ModelNode &modelNode,
                                         const QString &propertyName);

    static void removeAliasForProperty(const ModelNode &modelNode,
                                         const QString &propertyName);

public slots:
    void handleToolBarAction(int action);

protected:
    void setValue(const QmlObjectNode &fxObjectNode, PropertyNameView name, const QVariant &value);
    bool eventFilter(QObject *obj, QEvent *event) override;

private: //functions
    void reloadQml();
    void updateSize();

    void select();
    void setActiveNodeToSelection();
    void forceSelection(const ModelNode &node);

    void delayedResetView();
    void setupQmlBackend();

    void commitVariantValueToModel(PropertyNameView propertyName, const QVariant &value);
    void commitAuxValueToModel(PropertyNameView propertyName, const QVariant &value);
    void removePropertyFromModel(PropertyNameView propertyName);

    bool noValidSelection() const;
    void highlightTextureProperties(bool highlight = true);

    ModelNode activeNode() const;
    void setActiveNode(const ModelNode &node);
    QList<ModelNode> currentNodes() const;

    void resetSelectionLocked();
    void setIsSelectionLocked(bool locked);

    bool isNodeOrChildSelected(const ModelNode &node) const;
    void resetIfNodeIsRemoved(const ModelNode &removedNode);

    static PropertyEditorView *instance();

    NodeMetaInfo findCommonAncestor(const ModelNode &node);

private: //variables
    AsynchronousImageCache &m_imageCache;
    ModelNode m_activeNode;
    QShortcut *m_updateShortcut;
    PropertyEditorWidget* m_stackedWidget;
    QString m_qmlDir;
    QHash<QString, PropertyEditorQmlBackend *> m_qmlBackendHash;
    PropertyEditorQmlBackend *m_qmlBackEndForCurrentType;
    PropertyComponentGenerator m_propertyComponentGenerator;
    PropertyEditorComponentGenerator m_propertyEditorComponentGenerator{m_propertyComponentGenerator};
    bool m_locked;
    bool m_textureAboutToBeRemoved = false;
    bool m_isSelectionLocked = false;
    DynamicPropertiesModel *m_dynamicPropertiesModel = nullptr;

    friend class PropertyEditorDynamicPropertiesProxyModel;
};

} //QmlDesigner
