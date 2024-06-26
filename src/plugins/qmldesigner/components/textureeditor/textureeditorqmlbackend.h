// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "designerpropertymap.h"
#include "qmlanchorbindingproxy.h"
#include "qmlmodelnodeproxy.h"

#include <utils/uniqueobjectptr.h>

#include <nodemetainfo.h>

class PropertyEditorValue;

QT_BEGIN_NAMESPACE
class QQuickWidget;
QT_END_NAMESPACE

namespace QmlDesigner {

class AssetImageProvider;
class TextureEditorContextObject;
class TextureEditorTransaction;
class TextureEditorView;

class TextureEditorQmlBackend
{
    Q_DISABLE_COPY(TextureEditorQmlBackend)

public:
    TextureEditorQmlBackend(TextureEditorView *materialEditor,
                            class AsynchronousImageCache &imageCache);
    ~TextureEditorQmlBackend();

    void setup(const QmlObjectNode &selectedTextureNode, const QString &stateName, const QUrl &qmlSpecificsFile,
               TextureEditorView *textureEditor);
    void setValue(const QmlObjectNode &fxObjectNode, const PropertyName &name, const QVariant &value);

    QQmlContext *context() const;
    TextureEditorContextObject *contextObject() const;
    QQuickWidget *widget() const;
    void setSource(const QUrl &url);
    QmlAnchorBindingProxy &backendAnchorBinding();
    DesignerPropertyMap &backendValuesPropertyMap();
    TextureEditorTransaction *textureEditorTransaction() const;

    PropertyEditorValue *propertyValueForName(const QString &propertyName);

    static QString propertyEditorResourcesPath();

    void emitSelectionToBeChanged();
    void emitSelectionChanged();

    void setValueforAuxiliaryProperties(const QmlObjectNode &qmlObjectNode, AuxiliaryDataKeyView key);

private:
    void createPropertyEditorValue(const QmlObjectNode &qmlObjectNode,
                                   const PropertyName &name, const QVariant &value,
                                   TextureEditorView *textureEditor);
    PropertyName auxNamePostFix(const PropertyName &propertyName);

    // to avoid a crash while destructing DesignerPropertyMap in the QQmlData
    // this needs be destructed after m_quickWidget->engine() is destructed
    DesignerPropertyMap m_backendValuesPropertyMap;

    Utils::UniqueObjectPtr<QQuickWidget> m_quickWidget = nullptr;
    QmlAnchorBindingProxy m_backendAnchorBinding;
    QmlModelNodeProxy m_backendModelNode;
    QScopedPointer<TextureEditorTransaction> m_textureEditorTransaction;
    QScopedPointer<TextureEditorContextObject> m_contextObject;
    AssetImageProvider *m_textureEditorImageProvider = nullptr;
};

} // namespace QmlDesigner
