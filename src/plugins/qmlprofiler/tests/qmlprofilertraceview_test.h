// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qmlprofiler/qmlprofilermodelmanager.h>
#include <qmlprofiler/qmlprofilertraceview.h>

#include <QObject>

namespace QmlProfiler::Internal {

class QmlProfilerTraceViewTest : public QObject
{
    Q_OBJECT

public:
    QmlProfilerTraceViewTest();

private slots:
    void testStateChanges();

private:
    QmlProfilerModelManager modelManager;
    QmlProfilerTraceView traceView;
};

} // namespace QmlProfiler::Internal
