// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QString>

QT_FORWARD_DECLARE_CLASS(QJsonArray)

namespace EffectComposer {

class EffectUtils
{
public:
    EffectUtils() = delete;

    static QString codeFromJsonArray(const QJsonArray &codeArray);
    static QString nodesSourcesPath();
    static QString nodeLibraryPath();
    static QString nodeNameToFileName(const QString &nodeName);
};

} // namespace EffectComposer

