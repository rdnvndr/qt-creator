// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "effectutils.h"

#include <coreplugin/icore.h>

#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>

namespace EffectComposer {

QString EffectUtils::codeFromJsonArray(const QJsonArray &codeArray)
{
    if (codeArray.isEmpty())
        return {};

    QString codeString;
    for (const auto &element : codeArray)
        codeString += element.toString() + '\n';

    codeString.chop(1); // Remove last '\n'
    return codeString;
}

QString EffectUtils::nodesSourcesPath()
{
#ifdef SHARE_QML_PATH
    if (Utils::qtcEnvironmentVariableIsSet("LOAD_QML_FROM_SOURCE"))
        return QLatin1String(SHARE_QML_PATH) + "/effectComposerNodes";
#endif
    return Core::ICore::resourcePath("qmldesigner/effectComposerNodes").toUrlishString();
}

QString EffectUtils::nodeLibraryPath()
{
    QStandardPaths::StandardLocation location = QStandardPaths::DocumentsLocation;

    return QStandardPaths::writableLocation(location)
           + "/QtDesignStudio/effect_composer/node_library";
}

QString EffectUtils::nodeNameToFileName(const QString &nodeName)
{
    static const QRegularExpression re("[^a-zA-Z0-9]");
    QString newName = nodeName;
    newName.replace(re, "_");
    return newName;
}

} // namespace EffectComposer
