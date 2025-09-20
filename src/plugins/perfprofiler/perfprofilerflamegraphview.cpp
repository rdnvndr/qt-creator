// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "perfprofilerflamegraphmodel.h"
#include "perfprofilerflamegraphview.h"

#include <tracing/flamegraph.h>
#include <tracing/timelinetheme.h>
#include <utils/theme/theme.h>

#include <QQmlContext>
#include <QQmlEngine>

namespace PerfProfiler::Internal {

PerfProfilerFlameGraphView::PerfProfilerFlameGraphView(QWidget *parent)
    : QQuickWidget(parent)
{
    setObjectName(QLatin1String("PerfProfilerFlameGraphView"));

    PerfProfilerTraceManager *manager = &traceManager();
    m_model = new PerfProfilerFlameGraphModel(manager);

    engine()->addImportPath(":/qt/qml/");
    Timeline::TimelineTheme::setupTheme(engine());

    rootContext()->setContextProperty(QStringLiteral("flameGraphModel"), m_model);
    setSource(QUrl(QStringLiteral(
                       "qrc:/qt/qml/QtCreator/PerfProfiler/PerfProfilerFlameGraphView.qml")));
    setClearColor(Utils::creatorColor(Utils::Theme::Timeline_BackgroundColor1));

    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(rootObject(), SIGNAL(typeSelected(int)), this, SIGNAL(typeSelected(int)));
    connect(m_model, &PerfProfilerFlameGraphModel::gotoSourceLocation,
            this, &PerfProfilerFlameGraphView::gotoSourceLocation);
}

PerfProfilerFlameGraphView::~PerfProfilerFlameGraphView()
{
    delete m_model;
}

void PerfProfilerFlameGraphView::selectByTypeId(int typeId)
{
    rootObject()->setProperty("selectedTypeId", typeId);
}

void PerfProfilerFlameGraphView::resetRoot()
{
    QMetaObject::invokeMethod(rootObject(), "resetRoot");
}

bool PerfProfilerFlameGraphView::isZoomed() const
{
    return rootObject()->property("zoomed").toBool();
}

} // namespace PerfProfiler::Internal
