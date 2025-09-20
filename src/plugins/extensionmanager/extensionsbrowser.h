// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QWidget>

QT_FORWARD_DECLARE_CLASS(QLabel)

namespace ExtensionManager::Internal {

class ExtensionsModel;

class ExtensionsBrowser final : public QWidget
{
    Q_OBJECT

public:
    ExtensionsBrowser(ExtensionsModel *model, QWidget *parent = nullptr);
    ~ExtensionsBrowser();

    void setFilter(const QString &filter);

    void adjustToWidth(const int width);
    QSize sizeHint() const override;

    int extraListViewWidth() const; // Space for scrollbar, etc.

    void showEvent(QShowEvent *event) override;

    QModelIndex currentIndex() const;
    void selectIndex(const QModelIndex &index);

signals:
    void itemSelected(const QModelIndex &current, const QModelIndex &previous);

private:
    void fetchExtensions();

    class ExtensionsBrowserPrivate *d = nullptr;
};

constexpr static QSize iconBgSizeSmall{50, 50};
constexpr static QSize iconBgSizeBig{68, 68};
enum Size {
    SizeSmall,
    SizeBig,
};
QPixmap itemIcon(const QModelIndex &index, Size size);
QPixmap itemBadge(const QModelIndex &index, Size size);

} // ExtensionManager::Internal
