// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <asynchronousimagecache.h>
#include <abstractview.h>
#include <createtexture.h>
#include <nodemetainfo.h>

#include <utils/filepath.h>

#include <QObject>
#include <QPointer>

QT_FORWARD_DECLARE_CLASS(QImage)
QT_FORWARD_DECLARE_CLASS(QPixmap)
QT_FORWARD_DECLARE_CLASS(QTemporaryDir)

namespace QmlDesigner {

class BundleHelper;
class ContentLibraryItem;
class ContentLibraryMaterial;
class ContentLibraryTexture;
class ContentLibraryWidget;
class Model;

class ContentLibraryView : public AbstractView
{
    Q_OBJECT

public:
    ContentLibraryView(AsynchronousImageCache &imageCache,
                       ExternalDependenciesInterface &externalDependencies);
    ~ContentLibraryView() override;

    bool hasWidget() const override;
    WidgetInfo widgetInfo() override;
    void registerWidgetInfo() override;
    // AbstractView
    void modelAttached(Model *model) override;
    void modelAboutToBeDetached(Model *model) override;
    void importsChanged(const Imports &addedImports, const Imports &removedImports) override;
    void selectedNodesChanged(const QList<ModelNode> &selectedNodeList,
                              const QList<ModelNode> &lastSelectedNodeList) override;
    void customNotification(const AbstractView *view, const QString &identifier,
                            const QList<ModelNode> &nodeList, const QList<QVariant> &data) override;
    void nodeReparented(const ModelNode &node, const NodeAbstractProperty &newPropertyParent,
                        const NodeAbstractProperty &oldPropertyParent,
                        AbstractView::PropertyChangeFlags propertyChange) override;
    void nodeAboutToBeRemoved(const ModelNode &removedNode) override;
    void auxiliaryDataChanged(const ModelNode &node,
                              AuxiliaryDataKeyView type,
                              const QVariant &data) override;
    void modelNodePreviewPixmapChanged(const ModelNode &node,
                                       const QPixmap &pixmap,
                                       const QByteArray &requestId) override;

private:
    void connectImporter();
    bool isMaterialBundle(const QString &bundleId) const;
    bool isItemBundle(const QString &bundleId) const;
    void active3DSceneChanged(qint32 sceneId);
    void updateBundlesQuick3DVersion();
    void addLibAssets(const QStringList &paths, const QString &bundlePath = {});
    void addLib3DComponent(const ModelNode &node);
    void addLibItem(const ModelNode &node, const QPixmap &iconPixmap = {});
    void importBundleToContentLib();
    void saveIconToBundle(const auto &image, const QString &iconPath);
    void decodeAndAddToContentLib(const QByteArray &encodedInternalIds);

#ifdef QDS_USE_PROJECTSTORAGE
    void applyBundleMaterialToDropTarget(const ModelNode &bundleMat, const TypeName &typeName = {});
#else
    void applyBundleMaterialToDropTarget(const ModelNode &bundleMat,
                                         const NodeMetaInfo &metaInfo = {});
#endif
    ModelNode getBundleMaterialDefaultInstance(const TypeName &type);

    QPointer<ContentLibraryWidget> m_widget;
    QList<ModelNode> m_bundleMaterialTargets;
    ModelNode m_bundleItemTarget; // target of the dropped bundle item
    QVariant m_bundleItemPos; // pos of the dropped bundle item
    QList<ModelNode> m_selectedModels; // selected 3D model nodes
    ContentLibraryMaterial *m_draggedBundleMaterial = nullptr;
    ContentLibraryTexture *m_draggedBundleTexture = nullptr;
    ContentLibraryItem *m_draggedBundleItem = nullptr;
    std::unique_ptr<BundleHelper> m_bundleHelper;
    AsynchronousImageCache &m_imageCache;
    bool m_bundleMaterialAddToSelected = false;
    bool m_hasQuick3DImport = false;
    qint32 m_sceneId = -1;
    QString m_generatedFolderName;
    QString m_bundleId;
    QHash<ModelNode, QString> m_nodeIconHash;
    int m_remainingIconsToSave = 0;

    static constexpr char BUNDLE_VERSION[] = "1.0";
    static constexpr char ADD_ITEM_REQ_ID[] = "AddItemReqId";
};

} // namespace QmlDesigner
