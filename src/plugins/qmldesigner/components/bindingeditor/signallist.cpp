// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <qmldesignertr.h>

#include "signallist.h"

#include "signallistdelegate.h"

#include <qmldesignerplugin.h>
#include <qmldesignertr.h>

#include <coreplugin/icore.h>

#include <variantproperty.h>
#include <bindingproperty.h>
#include <signalhandlerproperty.h>
#include <qmldesignerconstants.h>
#include <qmlitemnode.h>
#include <nodeabstractproperty.h>

#include <utils/set_algorithm.h>

#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QTableView>

namespace QmlDesigner {

SignalListModel::SignalListModel(QObject *parent)
    : QStandardItemModel(0, 3, parent)
{
    setHeaderData(TargetColumn, Qt::Horizontal, Tr::tr("Item ID"));
    setHeaderData(SignalColumn, Qt::Horizontal, Tr::tr("Signal"));
    setHeaderData(ButtonColumn, Qt::Horizontal, "");
}

void SignalListModel::setConnected(int row, bool connected)
{
    for (int col = 0; col < columnCount(); ++col) {
        QModelIndex idx = index(row, col);
        setData(idx, connected, ConnectedRole);
    }
}


SignalListFilterModel::SignalListFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

bool SignalListFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex targetIndex = sourceModel()->index(sourceRow, SignalListModel::TargetColumn, sourceParent);
    QModelIndex signalIndex = sourceModel()->index(sourceRow, SignalListModel::SignalColumn, sourceParent);

    return (sourceModel()->data(targetIndex).toString().contains(filterRegularExpression())
            || sourceModel()->data(signalIndex).toString().contains(filterRegularExpression()));
}

SignalList::SignalList(QObject *)
    : m_model(Utils::makeUniqueObjectPtr<SignalListModel>(this))
    , m_modelNode()
{
}

SignalList::~SignalList()
{
    hideWidget();
}

void SignalList::prepareDialog()
{
    m_dialog = Utils::makeUniqueObjectPtr<SignalListDialog>(Core::ICore::dialogParent());
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dialog->initialize(m_model.get());
    m_dialog->setWindowTitle(::QmlDesigner::Tr::tr("Signal List for %1").arg(m_modelNode.validId()));

    auto delegate = m_dialog->signalListDelegate();
    connect(delegate, &SignalListDelegate::connectClicked, this, &SignalList::connectClicked);
}

void SignalList::showWidget()
{
    prepareDialog();
    m_dialog->show();
    m_dialog->raise();
}

void SignalList::hideWidget()
{
    if (m_dialog)
        m_dialog->close();
}

void SignalList::showWidget(const ModelNode &modelNode)
{
    auto signalList = new SignalList;
    signalList->setModelNode(modelNode);
    signalList->prepareSignals();
    signalList->showWidget();

    connect(signalList->m_dialog.get(), &QDialog::destroyed, [signalList]() {
        signalList->deleteLater();
    });
}

void SignalList::setModelNode(const ModelNode &modelNode)
{
    if (modelNode.isValid())
        m_modelNode = modelNode;
}

namespace {
template<typename Callback>
void callOnlyMouseSignalNames(const PropertyNameList &signalNames,
                              const PropertyNameList &mouseSignalNames,
                              const Callback &callback)
{
    std::set_union(signalNames.begin(),
                   signalNames.end(),
                   mouseSignalNames.begin(),
                   mouseSignalNames.end(),
                   Utils::make_iterator(callback));
}
} // namespace

void SignalList::prepareSignals()
{
    if (!m_modelNode.isValid())
        return;

    QList<QmlConnections> connections = QmlFlowViewNode::getAssociatedConnections(m_modelNode);

    for (ModelNode &node : m_modelNode.view()->allModelNodes()) {
        callOnlyMouseSignalNames(node.metaInfo().signalNames(),
                                 QmlFlowViewNode::mouseSignals(),
                                 [&](const PropertyName &signal) {
                                     appendSignalToModel(connections, node, signal);
                                 });

        // Gather valid properties and aliases from components
        for (const auto &property : node.metaInfo().properties()) {
            const NodeMetaInfo info = property.propertyType();

            callOnlyMouseSignalNames(info.signalNames(),
                                     QmlFlowViewNode::mouseSignals(),
                                     [&](const PropertyName &signal) {
                                         appendSignalToModel(connections, node, signal);
                                     });
        }
    }
}

void SignalList::connectClicked(const QModelIndex &modelIndex)
{
    auto proxyModel = static_cast<const SignalListFilterModel *>(modelIndex.model());
    QModelIndex mappedModelIndex = proxyModel->mapToSource(modelIndex);
    bool connected = mappedModelIndex.data(SignalListModel::ConnectedRole).toBool();

    if (!connected)
        addConnection(mappedModelIndex);
    else
        removeConnection(mappedModelIndex);
}

void SignalList::appendSignalToModel(const QList<QmlConnections> &connections,
                                     ModelNode &node,
                                     const PropertyName &signal,
                                     const PropertyName &property)
{
    QStandardItem *idItem = new QStandardItem();
    QString id(node.validId());
    if (!property.isEmpty())
        id += "." + QString::fromLatin1(property);

    idItem->setData(id, Qt::DisplayRole);

    QStandardItem *signalItem = new QStandardItem();
    signalItem->setData(signal, Qt::DisplayRole);

    QStandardItem *buttonItem = new QStandardItem();

    idItem->setData(false, SignalListModel::ConnectedRole);
    signalItem->setData(false, SignalListModel::ConnectedRole);
    buttonItem->setData(false, SignalListModel::ConnectedRole);

    for (const QmlConnections &connection : connections) {
        if (connection.target() == id) {
            for (const SignalHandlerProperty &property : connection.signalProperties()) {
                auto signalWithoutPrefix = SignalHandlerProperty::prefixRemoved(property.name());
                if (signalWithoutPrefix == signal) {
                    buttonItem->setData(connection.modelNode().internalId(),
                                        SignalListModel::ConnectionsInternalIdRole);

                    idItem->setData(true, SignalListModel::ConnectedRole);
                    signalItem->setData(true, SignalListModel::ConnectedRole);
                    buttonItem->setData(true, SignalListModel::ConnectedRole);
                }
            }
        }
    }
    m_model->appendRow({idItem, signalItem, buttonItem});
}

void SignalList::addConnection(const QModelIndex &modelIndex)
{
    const QModelIndex targetModelIndex = modelIndex.siblingAtColumn(SignalListModel::TargetColumn);
    const QModelIndex signalModelIndex = modelIndex.siblingAtColumn(SignalListModel::SignalColumn);
    const QModelIndex buttonModelIndex = modelIndex.siblingAtColumn(SignalListModel::ButtonColumn);
    const PropertyName signalName = m_model->data(signalModelIndex,
                                                 Qt::DisplayRole).toByteArray();

    QmlDesignerPlugin::emitUsageStatistics(Constants::EVENT_CONNECTION_ADDED);

    AbstractView *view = m_modelNode.view();
    const ModelNode rootModelNode = view->rootModelNode();

    if (rootModelNode.isValid() && rootModelNode.metaInfo().isValid()) {
#ifndef QDS_USE_PROJECTSTORAGE
        NodeMetaInfo nodeMetaInfo = view->model()->qtQmlConnectionsMetaInfo();
        if (nodeMetaInfo.isValid()) {
#endif
            view->executeInTransaction("ConnectionModel::addConnection", [&] {
#ifdef QDS_USE_PROJECTSTORAGE
                ModelNode newNode = view->createModelNode("Connections");
#else
                ModelNode newNode = view->createModelNode("QtQuick.Connections",
                                                          nodeMetaInfo.majorVersion(),
                                                          nodeMetaInfo.minorVersion());
#endif
                const QString source = m_modelNode.validId() + ".trigger()";

                if (QmlItemNode::isValidQmlItemNode(m_modelNode))
                    m_modelNode.nodeAbstractProperty("data").reparentHere(newNode);
                else
                    rootModelNode.nodeAbstractProperty(rootModelNode.metaInfo().defaultPropertyName()).reparentHere(newNode);

                const QString expression = m_model->data(targetModelIndex, Qt::DisplayRole).toString();
                newNode.bindingProperty("target").setExpression(expression);
                newNode.signalHandlerProperty(SignalHandlerProperty::prefixAdded(signalName)).setSource(source);

                m_model->setConnected(modelIndex.row(), true);
                m_model->setData(buttonModelIndex, newNode.internalId(), SignalListModel::ConnectionsInternalIdRole);
            });
#ifndef QDS_USE_PROJECTSTORAGE
        }
#endif
    }
}

void SignalList::removeConnection(const QModelIndex &modelIndex)
{
    const QModelIndex signalModelIndex = modelIndex.siblingAtColumn(SignalListModel::SignalColumn);
    const QModelIndex buttonModelIndex = modelIndex.siblingAtColumn(SignalListModel::ButtonColumn);
    const PropertyName signalName = m_model->data(signalModelIndex,
                                                 Qt::DisplayRole).toByteArray();
    const int connectionInternalId = m_model->data(buttonModelIndex,
                                                  SignalListModel::ConnectionsInternalIdRole).toInt();

    AbstractView *view = m_modelNode.view();
    const ModelNode connectionModelNode = view->modelNodeForInternalId(connectionInternalId);
    SignalHandlerProperty targetSignal;

    if (connectionModelNode.isValid())
        targetSignal = connectionModelNode.signalHandlerProperty(signalName);

    ModelNode node = targetSignal.parentModelNode();
    if (node.isValid()) {
        view->executeInTransaction(
            "ConnectionModel::removeConnection",
            [this, modelIndex, buttonModelIndex, targetSignal, &node] {
                const QList<SignalHandlerProperty> allSignals = node.signalProperties();
                if (allSignals.size() > 1) {
                    const auto targetSignalWithPrefix = SignalHandlerProperty::prefixAdded(
                        targetSignal.name());
                    for (const SignalHandlerProperty &signal : allSignals)
                        if (signal.name() == targetSignalWithPrefix)
                            node.removeProperty(targetSignalWithPrefix);
                } else {
                    node.destroy();
                }
                m_model->setConnected(modelIndex.row(), false);
                m_model->setData(buttonModelIndex,
                                 QVariant(),
                                 SignalListModel::ConnectionsInternalIdRole);
            });
    }
}

} // QmlDesigner namespace
