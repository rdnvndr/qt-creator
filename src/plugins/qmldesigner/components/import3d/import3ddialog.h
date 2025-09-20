// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include "import3dimporter.h"

#include <modelnode.h>

#include <utils/filepath.h>

#include <QDialog>
#include <QIcon>
#include <QJsonObject>
#include <QPointer>
#include <QSet>

QT_BEGIN_NAMESPACE
class QGridLayout;
class QLabel;
class QListWidgetItem;
class QPushButton;
QT_END_NAMESPACE

namespace Utils {
class OutputFormatter;
}

namespace QmlDesigner {
class Import3dCanvas;
class Import3dConnectionManager;
class NodeInstanceView;
class RewriterView;

namespace Ui {
class Import3dDialog;
}

class Import3dDialog : public QDialog
{
    Q_OBJECT

public:
    explicit Import3dDialog(const QStringList &importFiles,
                            const QVariantMap &supportedExts,
                            const QVariantMap &supportedOpts,
                            const QJsonObject &defaultOpts,
                            const QSet<QString> &preselectedFilesForOverwrite,
                            AbstractView *view,
                            QWidget *parent = nullptr);
    ~Import3dDialog();

    static void updateImport(AbstractView *view,
                             const Utils::FilePath &import3dQml,
                             const ModelNode &updateNode,
                             const QVariantMap &supportedExts,
                             const QVariantMap &supportedOpts);

    void keyPressEvent(QKeyEvent *event) override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void addError(const QString &error, const QString &srcPath = {});
    void addWarning(const QString &warning, const QString &srcPath = {});
    void addInfo(const QString &info, const QString &srcPath = {});

private:
    struct ImportData
    {
        QListWidgetItem *listItem = {};
        QLabel *iconLabel = {};
        QLabel *infoLabel = {};
        QPushButton *removeButton = {};
        Import3dImporter::PreviewData previewData;
    };

    struct OptionsData
    {
        int optionsRows = 0;
        int optionsHeight = 0;
        QList<QWidget *> contentWidgets;
    };

    void setCloseButtonState(bool importing);
    void updatePreviewOptions();

    void onImport();
    void setImportProgress(int value, const QString &text);
    void onImportReadyForPreview(const QString &path,
                                 const QList<Import3dImporter::PreviewData> &previewData);
    void onRequestImageUpdate();
    void onRequestRotation(const QPointF &delta);
    void onImportNearlyFinished();
    void onImportFinished();
    void onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onClose();
    void doClose();
    void toggleAdvanced();
    void onRemoveAsset(const QString &assetName);

    void createTab(const QString &tabLabel, int optionsIndex, const QJsonObject &groups);
    QGridLayout *createOptionsGrid(QWidget *contentWidget, bool advanced, int optionsIndex,
                                   const QJsonObject &groups);
    void updateUi();
    QString assetNameForListItem(QListWidgetItem *item);

    bool isSimpleGroup(const QString &id);
    bool isSimpleOption(const QString &id);
    bool isHiddenOption(const QString &id);
    bool optionsChanged();

    void startPreview();
    void cleanupPreviewPuppet();
    Import3dCanvas *canvas();
    void resetOptionControls();

    Ui::Import3dDialog *ui = nullptr;
    Utils::OutputFormatter *m_outputFormatter = nullptr;
    QPointer<Import3dConnectionManager> m_connectionManager;
    QPointer<NodeInstanceView> m_nodeInstanceView;
    QPointer<RewriterView> m_rewriterView;
    QPointer<AbstractView> m_view;
    ModelPointer m_model;

    QMap<QString, ImportData> m_importData;
    Utils::FilePath m_previewFile;

    QStringList m_quick3DFiles;
    QString m_quick3DImportPath;
    Import3dImporter m_importer;
    QVector<QJsonObject> m_importOptions;
    QHash<QString, int> m_extToImportOptionsMap;
    QSet<QString> m_preselectedFilesForOverwrite;
    bool m_closeOnFinish = true;
    QList<QHash<QString, QWidget *>> m_labelToControlWidgetMaps;
    OptionsData m_simpleData;
    OptionsData m_advancedData;
    bool m_advancedMode = false;
    int m_dialogHeight = 350;
    bool m_explicitClose = false;
    bool m_updatingControlStates = true;
    bool m_firstImport = true;
    QIcon m_selectedRemoveIcon;
    QIcon m_unselectedRemoveIcon;
};
}
