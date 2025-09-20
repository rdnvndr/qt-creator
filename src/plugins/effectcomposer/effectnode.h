// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>
#include <QSet>
#include <QUrl>

namespace EffectComposer {

class EffectNode : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString nodeName MEMBER m_name CONSTANT)
    Q_PROPERTY(QString nodeDescription MEMBER m_description CONSTANT)
    Q_PROPERTY(QUrl nodeIcon MEMBER m_iconPath CONSTANT)
    Q_PROPERTY(QString nodeQenPath MEMBER m_qenPath CONSTANT)
    Q_PROPERTY(bool canBeAdded MEMBER m_canBeAdded NOTIFY canBeAddedChanged)
    Q_PROPERTY(bool canBeRemoved MEMBER m_canBeRemoved CONSTANT)

public:
    EffectNode(const QString &qenPath, bool isBuiltIn);

    QString name() const;
    QString description() const;
    QString qenPath() const;
    QHash<QString, QString> defaultImagesHash() const { return m_defaultImagesHash; }
    bool isCustom() const { return m_isCustom; }

    void setCanBeAdded(bool enabled);

    bool hasUniform(const QString &name);

signals:
    void canBeAddedChanged();

private:
    QString m_name;
    QString m_description;
    QString m_qenPath;
    QUrl m_iconPath;
    bool m_isCustom = false;
    bool m_canBeAdded = true;
    bool m_canBeRemoved = false;
    QSet<QString> m_uniformNames;
    QHash<QString, QString> m_defaultImagesHash;
};

} // namespace EffectComposer

