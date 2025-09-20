// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QQuickWidget>

namespace PerfProfiler::Internal {

class PerfProfilerFlameGraphModel;

class PerfProfilerFlameGraphView : public QQuickWidget
{
    Q_OBJECT
public:
    PerfProfilerFlameGraphView(QWidget *parent);
    ~PerfProfilerFlameGraphView();

    void selectByTypeId(int typeId);
    void resetRoot();
    bool isZoomed() const;

signals:
    void gotoSourceLocation(QString file, int line, int column);
    void typeSelected(int typeId);

private:
    PerfProfilerFlameGraphModel *m_model;
};

} // namespace PerfProfiler::Internal
