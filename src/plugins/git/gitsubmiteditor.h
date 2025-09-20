// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "commitdata.h"

#include <utils/filepath.h>

#include <vcsbase/vcsbasesubmiteditor.h>

#include <QFutureWatcher>
#include <QStringList>

namespace VcsBase { class SubmitFileModel; }

namespace Git::Internal {

class GitSubmitEditorWidget;
class GitSubmitEditorPanelData;

class GitSubmitEditor : public VcsBase::VcsBaseSubmitEditor
{
    Q_OBJECT

public:
    GitSubmitEditor();
    ~GitSubmitEditor() override;

    void setCommitData(const CommitData &);
    GitSubmitEditorPanelData panelData() const;
    CommitType commitType() const { return m_commitType; }
    QString amendHash() const;
    void updateFileModel() override;

protected:
    QByteArray fileContents() const override;
    void forceUpdateFileModel();

private:
    void slotDiffSelected(const QList<int> &rows);
    void showCommit(const QString &commit);
    void showLog(const QStringList &range);
    void commitDataRetrieved();

    inline GitSubmitEditorWidget *submitEditorWidget();
    inline const GitSubmitEditorWidget *submitEditorWidget() const;

    VcsBase::SubmitFileModel *m_model = nullptr;
    QTextCodec *m_commitEncoding = nullptr;
    CommitType m_commitType = SimpleCommit;
    QString m_amenHash;
    Utils::FilePath m_workingDirectory;
    bool m_firstUpdate = true;
    QFutureWatcher<Utils::Result<CommitData>> m_fetchWatcher;
};

} // Git::Internal
