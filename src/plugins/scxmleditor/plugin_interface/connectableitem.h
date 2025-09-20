// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "baseitem.h"
#include "transitionitem.h"

#include <QPen>

QT_FORWARD_DECLARE_CLASS(QGraphicsSceneMouseEvent)
QT_FORWARD_DECLARE_CLASS(QPainterPath)

namespace ScxmlEditor {

namespace PluginInterface {

class CornerGrabberItem;
class HighlightItem;
class QuickTransitionItem;

/**
 * @brief The ConnectableItem class is the base class for all draggable state-items.
 */
class ConnectableItem : public BaseItem
{
    Q_OBJECT

public:
    explicit ConnectableItem(const QPointF &pos, BaseItem *parent = nullptr);
    ~ConnectableItem() override;

    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;

    void addOutputTransition(TransitionItem *transition);
    void addInputTransition(TransitionItem *transition);
    void removeOutputTransition(TransitionItem *transition);
    void removeInputTransition(TransitionItem *transition);
    QList<TransitionItem*> outputTransitions() const;
    QList<TransitionItem*> inputTransitions() const;

    void setMinimumWidth(int width);
    void setMinimumHeight(int height);
    int minHeight() const
    {
        return m_minimumHeight;
    }

    int minWidth() const
    {
        return m_minimumWidth;
    }

    void finalizeCreation() override;
    void init(ScxmlTag *tag, BaseItem *parentItem = nullptr, bool initChildren = true, bool blockUpdates = false) override;

    void updateUIProperties() override;
    void updateAttributes() override;
    void updateEditorInfo(bool allChildren = false) override;
    void moveStateBy(qreal dx, qreal dy) override;
    void setHighlight(bool hl) override;

    int transitionCount() const;
    int outputTransitionCount() const;
    int inputTransitionCount() const;
    bool hasInputTransitions(const ConnectableItem *parentItem, bool checkChildren = false) const;
    bool hasOutputTransitions(const ConnectableItem *parentItem, bool checkChildren = false) const;
    QPointF getInternalPosition(const TransitionItem *transition, TransitionItem::TransitionTargetType type) const;

    void updateOutputTransitions();
    void updateInputTransitions();
    void updateTransitions(bool allChildren = false);
    void updateTransitionAttributes(bool allChildren = false);

    void addOverlappingItem(ConnectableItem *item);
    void removeOverlappingItem(ConnectableItem *item);
    void checkOverlapping() override;

    // Parent change
    virtual void releaseFromParent();
    virtual void connectToParent(BaseItem *parentItem);

    qreal getOpacity();

protected:
    void readUISpecifiedProperties(const ScxmlTag *tag) override;
    void addTransitions(const ScxmlTag *tag);
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

    virtual bool canStartTransition(ItemType type) const;
    virtual void transitionCountChanged();
    virtual void transitionsChanged();

private:
    void updateCornerPositions();
    void createCorners();
    void removeCorners();
    void updateShadowClipRegion();

    QList<TransitionItem*> m_outputTransitions;
    QList<TransitionItem*> m_inputTransitions;
    QList<CornerGrabberItem*> m_corners;
    QList<QuickTransitionItem*> m_quickTransitions;
    HighlightItem *m_highlighItem = nullptr;
    TransitionItem *m_newTransition = nullptr;
    QPen m_selectedPen;
    QBrush m_releasedFromParentBrush;
    int m_minimumWidth = 120;
    int m_minimumHeight = 100;
    bool m_releasedFromParent = false;
    int m_releasedIndex = -1;
    QGraphicsItem *m_releasedParent = nullptr;
    QPointF m_newTransitionStartedPoint;
    QPainterPath m_shadowClipPath;
    QPointF m_moveStartPoint;
    bool m_moveMacroStarted = false;
    QList<ConnectableItem*> m_overlappedItems;
};

} // namespace PluginInterface
} // namespace ScxmlEditor
