// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qmldesigner/components/propertyeditor/qmlanchorbindingproxy.h>
#include <qmldesigner/components/propertyeditor/qmlmodelnodeproxy.h>

#include <coreplugin/icontext.h>

#include <QFrame>
#include <QFuture>
#include <QUrl>

class StudioQuickWidget;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace EffectComposer {

class EffectComposerView;
class EffectComposerModel;
class EffectComposerNodesModel;

class EffectComposerWidget : public QFrame
{
    Q_OBJECT

public:
    EffectComposerWidget(EffectComposerView *view);
    ~EffectComposerWidget() = default;

    void contextHelp(const Core::IContext::HelpCallback &callback) const;

    static QString qmlSourcesPath();
    void clearSearchFilter();

    void delayedUpdateModel();
    void updateModel();
    void initView();
    void openComposition(const QString &path);

    StudioQuickWidget *quickWidget() const;
    QPointer<EffectComposerModel> effectComposerModel() const;
    QPointer<EffectComposerNodesModel> effectComposerNodesModel() const;

    Q_INVOKABLE void addEffectNode(const QString &nodeQenPath);
    Q_INVOKABLE void removeEffectNodeFromLibrary(const QString &nodeName);
    Q_INVOKABLE void focusSection(int section);
    Q_INVOKABLE void doOpenComposition();
    Q_INVOKABLE QRect screenRect() const;
    Q_INVOKABLE QPoint globalPos(const QPoint &point) const;
    Q_INVOKABLE QString uniformDefaultImage(const QString &nodeName,
                                            const QString &uniformName) const;
    Q_INVOKABLE QString imagesPath() const;
    Q_INVOKABLE bool isEffectAsset(const QUrl &url) const;
    Q_INVOKABLE void dropAsset(const QUrl &url);
    Q_INVOKABLE bool isEffectNode(const QByteArray &mimeData) const;
    Q_INVOKABLE void dropNode(const QByteArray &mimeData);
    Q_INVOKABLE void updateCanBeAdded();
    Q_INVOKABLE bool isMCUProject() const;

    QSize sizeHint() const override;

private:
    void reloadQmlSource();
    void handleImportScanTimer();

    QPointer<EffectComposerModel> m_effectComposerModel;
    QPointer<EffectComposerView> m_effectComposerView;
    QPointer<StudioQuickWidget> m_quickWidget;
    QmlDesigner::QmlModelNodeProxy m_backendModelNode;
    QmlDesigner::QmlAnchorBindingProxy m_backendAnchorBinding;

    struct ImportScanData {
        QFuture<void> future;
        int counter = 0;
        QTimer *timer = nullptr;
        QmlDesigner::TypeName type;
        Utils::FilePath path;
    };

    ImportScanData m_importScan;
    QString m_compositionPath;
};

} // namespace EffectComposer

