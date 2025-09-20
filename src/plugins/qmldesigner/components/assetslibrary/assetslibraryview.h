// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "abstractview.h"

#include <utils/filepath.h>
#include <utils/uniqueobjectptr.h>

#include <QPointer>
#include <QTimer>

#include <mutex>

namespace QmlDesigner {

class AssetsLibraryWidget;
class AsynchronousImageCache;

class AssetsLibraryView : public AbstractView
{
    Q_OBJECT

public:
    AssetsLibraryView(AsynchronousImageCache &imageCache,
                      ExternalDependenciesInterface &externalDependencies);
    ~AssetsLibraryView() override;

    bool hasWidget() const override;
    WidgetInfo widgetInfo() override;

    // AbstractView
    void modelAttached(Model *model) override;
    void modelAboutToBeDetached(Model *model) override;
    void exportedTypeNamesChanged(const ExportedTypeNames &added,
                                  const ExportedTypeNames &removed) override;

    void setResourcePath(const QString &resourcePath);

private:
    class ImageCacheData;
    ImageCacheData *imageCacheData();

    void customNotification(const AbstractView *view, const QString &identifier,
                            const QList<ModelNode> &nodeList, const QList<QVariant> &data) override;
    QHash<QString, Utils::FilePath> collectFiles(const Utils::FilePath &dirPath,
                                                 const QString &suffix);
    void sync3dImports();

    std::once_flag imageCacheFlag;
    std::unique_ptr<ImageCacheData> m_imageCacheData;
    Utils::UniqueObjectPtr<AssetsLibraryWidget> m_widget;
    QString m_lastResourcePath;
    QTimer m_3dImportsSyncTimer;
};

}
