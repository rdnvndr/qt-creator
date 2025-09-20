// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionsbrowser.h"

#include "extensionmanagerconstants.h"
#include "extensionmanagersettings.h"
#include "extensionmanagertr.h"
#include "extensionsmodel.h"

#ifdef WITH_TESTS
#include "extensionmanager_test.h"
#endif // WITH_TESTS

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/welcomepagehelper.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginspec.h>
#include <extensionsystem/pluginview.h>
#include <extensionsystem/pluginmanager.h>

#include <solutions/spinner/spinner.h>
#include <solutions/tasking/conditional.h>
#include <solutions/tasking/networkquery.h>
#include <solutions/tasking/tasktree.h>
#include <solutions/tasking/tasktreerunner.h>

#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/hostosinfo.h>
#include <utils/icon.h>
#include <utils/layoutbuilder.h>
#include <utils/networkaccessmanager.h>
#include <utils/qtcprocess.h>
#include <utils/qtcwidgets.h>
#include <utils/stylehelper.h>
#include <utils/unarchiver.h>
#include <utils/utilsicons.h>

#include <QApplication>
#include <QItemDelegate>
#include <QLabel>
#include <QLayout>
#include <QListView>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QTemporaryFile>

using namespace Core;
using namespace ExtensionSystem;
using namespace Utils;
using namespace StyleHelper;
using namespace SpacingTokens;
using namespace WelcomePageHelpers;

namespace ExtensionManager::Internal {

Q_LOGGING_CATEGORY(browserLog, "qtc.extensionmanager.browser", QtWarningMsg)

constexpr int gapSize = HGapL;
constexpr int itemWidth = 330;
constexpr int cellWidth = itemWidth + gapSize;

class OptionChooser : public QComboBox
{
public:
    OptionChooser(const FilePath &iconMask, const QString &textTemplate, QWidget *parent = nullptr)
        : QComboBox(parent)
        , m_iconDefault(Icon({{iconMask, m_colorDefault}}, Icon::Tint).icon())
        , m_iconActive(Icon({{iconMask, m_colorActive}}, Icon::Tint).icon())
        , m_textTemplate(textTemplate)
    {
        setMouseTracking(true);
        connect(this, &QComboBox::currentIndexChanged, this, &QWidget::updateGeometry);
    }

protected:
    void paintEvent([[maybe_unused]] QPaintEvent *event) override
    {
        // +------------+------+---------+---------------+------------+
        // |            |      |         |  (VPaddingXs) |            |
        // |            |      |         +---------------+            |
        // |(HPaddingXs)|(icon)|(HGapXxs)|<template%item>|(HPaddingXs)|
        // |            |      |         +---------------+            |
        // |            |      |         |  (VPaddingXs) |            |
        // +------------+------+---------+---------------+------------+

        const bool active = currentIndex() > 0;
        const bool hover = underMouse();
        const TextFormat &tF = (active || hover) ? m_itemActiveTf : m_itemDefaultTf;

        const QRect iconRect(HPaddingXs, 0, m_iconSize.width(), height());
        const int textX = iconRect.right() + 1 + HGapXxs;
        const QRect textRect(textX, VPaddingXs,
                             width() - HPaddingXs - textX, tF.lineHeight());

        QPainter p(this);
        (active ? m_iconActive : m_iconDefault).paint(&p, iconRect);
        p.setPen(tF.color());
        p.setFont(tF.font());
        const QString elidedText = p.fontMetrics().elidedText(currentFormattedText(),
                                                              Qt::ElideRight,
                                                              textRect.width() + HPaddingXs);
        p.drawText(textRect, tF.drawTextFlags, elidedText);
    }

    void enterEvent(QEnterEvent *event) override
    {
        QComboBox::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        QComboBox::leaveEvent(event);
        update();
    }

private:
    QSize sizeHint() const override
    {
        const QFontMetrics fm(m_itemDefaultTf.font());
        const int textWidth = fm.horizontalAdvance(currentFormattedText());
        const int width =
            HPaddingXs
            + m_iconSize.width()
            + HGapXxs
            + textWidth
            + HPaddingXs;
        const int height =
            VPaddingXs
            + m_itemDefaultTf.lineHeight()
            + VPaddingXs;
        return {width, height};
    }

    QString currentFormattedText() const
    {
        return m_textTemplate.arg(currentText());
    }

    constexpr static Theme::Color m_colorDefault = Theme::Token_Text_Muted;
    constexpr static Theme::Color m_colorActive = Theme::Token_Text_Default;
    constexpr static QSize m_iconSize{16, 16};
    constexpr static TextFormat m_itemDefaultTf
        {m_colorDefault, UiElement::UiElementLabelMedium};
    constexpr static TextFormat m_itemActiveTf
        {m_colorActive, m_itemDefaultTf.uiElement};
    const QIcon m_iconDefault;
    const QIcon m_iconActive;
    const QString m_textTemplate;
};

static QString extensionStateDisplayString(ExtensionState state)
{
    switch (state) {
    case InstalledEnabled:
        return Tr::tr("Active");
    case InstalledDisabled:
        return Tr::tr("Inactive");
    default:
        return {};
    }
    return {};
}

class ExtensionItemWidget final : public QWidget
{
public:
    constexpr static QSize dividerS{1, 16};
    constexpr static TextFormat itemNameTF
        {Theme::Token_Text_Default, UiElement::UiElementH6};
    constexpr static TextFormat releaseStatusTF
        {Theme::Token_Notification_Alert_Default, UiElement::UiElementLabelSmall};
    constexpr static TextFormat countTF
        {Theme::Token_Text_Default, UiElement::UiElementLabelSmall,
         Qt::AlignCenter | Qt::TextDontClip};
    constexpr static TextFormat vendorTF
        {Theme::Token_Text_Muted, UiElement::UiElementLabelSmall,
         Qt::AlignVCenter | Qt::TextDontClip};
    constexpr static TextFormat stateActiveTF
        {vendorTF.themeColor, UiElement::UiElementCaption, vendorTF.drawTextFlags};
    constexpr static TextFormat stateInactiveTF
        {Theme::Token_Text_Subtle, stateActiveTF.uiElement, stateActiveTF.drawTextFlags};
    constexpr static TextFormat descriptionTF
        {itemNameTF.themeColor, UiElement::UiElementCaption};

    ExtensionItemWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // +---------------+-------+---------------+-----------------------------------------------------------------------------------+---------------+---------+
        // |               |       |               |                                  (ExPaddingGapL)                                  |               |         |
        // |               |       |               +----------+---------+---------------+---------+--------------+---------+-----------+               |         |
        // |               |       |               |<itemName>|(HGapXxs)|<releaseStatus>|(HGapXxs)|<installState>|(HGapXxs)|<checkmark>|               |         |
        // |               |       |               +----------+---------+---------------+---------+--------------+---------+-----------+               |         |
        // |               |       |               |                                     (VGapXxs)                                     |               |         |
        // |               |       |               +---------------------+--------+--------------+--------+--------+---------+---------+               |         |
        // |(ExPaddingGapL)|<icon> |(ExPaddingGapL)|       <vendor>      |(HGapXs)|<divider>(h16)|(HGapXs)|<dlIcon>|(HGapXxs)|<dlCount>|(ExPaddingGapL)|(gapSize)|
        // |               |(50x50)|               +---------------------+--------+--------------+--------+--------+---------+---------+               |         |
        // |               |       |               |                                     (VGapXxs)                                     |               |         |
        // |               |       |               +-----------------------------------------------------------------------------------+               |         |
        // |               |       |               |                                 <shortDescription>                                |               |         |
        // |               |       |               +-----------------------------------------------------------------------------------+               |         |
        // |               |       |               |                                  (ExPaddingGapL)                                  |               |         |
        // +---------------+-------+---------------+-----------------------------------------------------------------------------------+---------------+---------+
        // |                                                                      (gapSize)                                                                      |
        // +-----------------------------------------------------------------------------------------------------------------------------------------------------+

        m_iconLabel = new QLabel;
        m_iconLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        m_itemNameLabel = new ElidingLabel;
        applyTf(m_itemNameLabel, itemNameTF);
        m_itemNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        m_releaseStatus = new QLabel;
        applyTf(m_releaseStatus, releaseStatusTF, false);
        m_releaseStatus->setAlignment(Qt::AlignLeft);
        m_releaseStatus->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_installStateLabel = new QLabel;
        applyTf(m_installStateLabel, stateActiveTF, false);
        m_installStateLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        m_installStateIcon = new QLabel;
        m_installStateIcon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
        m_vendorLabel = new ElidingLabel;
        applyTf(m_vendorLabel, vendorTF);
        m_downloadDividerLabel = new QLabel;
        m_downloadIconLabel = new QLabel;
        m_downloadCountLabel = new QLabel;
        applyTf(m_downloadCountLabel, countTF);
        m_shortDescriptionLabel = new ElidingLabel;
        applyTf(m_shortDescriptionLabel, descriptionTF);

        using namespace Layouting;
        Row {
            m_iconLabel,
            Column {
                Row {
                    m_itemNameLabel,
                    m_releaseStatus,
                    st,
                    Widget {
                        bindTo(&m_installState),
                        Row {
                            m_installStateLabel,
                            m_installStateIcon,
                            spacing(HGapXxs),
                            noMargin,
                        },
                    },
                    spacing(HGapXxs),
                },
                Row {
                    m_vendorLabel,
                    Widget {
                        bindTo(&m_downloads),
                        Row {
                            m_downloadDividerLabel,
                            Space(HGapXs),
                            m_downloadIconLabel,
                            Space(HGapXxs),
                            m_downloadCountLabel,
                            tight,
                        },
                    },
                    spacing(HGapXs),
                },
                m_shortDescriptionLabel,
                noMargin,
                spacing(VGapXxs),
            },
            customMargins(ExPaddingGapL, ExPaddingGapL, ExPaddingGapL, ExPaddingGapL),
            spacing(ExPaddingGapL),
        }.attachTo(this);

        setFixedWidth(itemWidth);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        setAutoFillBackground(false);
    }

    void setData(const QModelIndex &index)
    {
        m_iconLabel->setPixmap(itemIcon(index, SizeSmall));
        m_itemNameLabel->setText(index.data(RoleName).toString());

        const QString statusString = statusDisplayString(index);
        m_releaseStatus->setText(statusString);
        m_releaseStatus->setVisible(!statusString.isEmpty());

        const ExtensionState state = index.data(RoleExtensionState).value<ExtensionState>();
        const QString stateString = extensionStateDisplayString(state);
        const bool showState = !stateString.isEmpty();
        m_installState->setVisible(showState);
        if (showState) {
            const bool active = state == InstalledEnabled;
            QPalette pal = m_installStateLabel->palette();
            pal.setColor(QPalette::WindowText, (active ? stateActiveTF : stateInactiveTF).color());
            m_installStateLabel->setPalette(pal);
            m_installStateLabel->setText(stateString);
            const FilePath checkmarkMask = ":/extensionmanager/images/checkmark.png";
            static const QPixmap iconActive = Icon({{checkmarkMask, Theme::Token_Accent_Muted}},
                                                   Icon::Tint).pixmap();
            static const QPixmap iconInactive = Icon({{checkmarkMask, stateInactiveTF.themeColor}},
                                                     Icon::Tint).pixmap();
            m_installStateIcon->setPixmap(active ? iconActive : iconInactive);
            m_installState->layout()->invalidate(); // QTCREATORBUG-32954
        }

        m_vendorLabel->setText(index.data(RoleVendor).toString());
        m_shortDescriptionLabel->setText(index.data(RoleDescriptionShort).toString());
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)
    {
        setData(index);

        const QRect bgRGlobal = option.rect.adjusted(0, 0, -gapSize, -gapSize);
        const QRect bgR = bgRGlobal.translated(-option.rect.topLeft());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(bgRGlobal.topLeft());

        {
            const bool selected = option.state & QStyle::State_Selected;
            const bool hovered = option.state & QStyle::State_MouseOver;
            const QColor fillColor =
                creatorColor(hovered ? WelcomePageHelpers::cardHoverBackground
                                     : WelcomePageHelpers::cardDefaultBackground);
            const QColor strokeColor =
                creatorColor(selected ? Theme::Token_Stroke_Strong
                             : hovered ? WelcomePageHelpers::cardHoverStroke
                                       : WelcomePageHelpers::cardDefaultStroke);
            StyleHelper::drawCardBg(painter, bgR, fillColor, strokeColor);
        }

        render(painter, bgR.topLeft(), {}, QWidget::DrawChildren);

        {
            const QPixmap badge = itemBadge(index, SizeSmall);
            painter->drawPixmap(bgR.topLeft(), badge);
        }

        if (index.data(RoleItemType) == ItemTypePack) {
            const QRect iconBgR = m_iconLabel->geometry();

            constexpr int circleSize = 18;
            constexpr int circleOverlap = 3; // Protrusion from lower right corner of iconRect
            const QRect smallCircle(iconBgR.right() + 1 + circleOverlap - circleSize,
                                    iconBgR.bottom() + 1 + circleOverlap - circleSize,
                                    circleSize, circleSize);
            const QColor fillColor = creatorColor(Theme::Token_Foreground_Muted);
            const QColor strokeColor = creatorColor(Theme::Token_Stroke_Subtle);
            StyleHelper::drawCardBg(painter, smallCircle, fillColor, strokeColor,
                                    circleSize / 2);

            painter->setFont(countTF.font());
            painter->setPen(countTF.color());
            const QStringList plugins = index.data(RolePlugins).toStringList();
            painter->drawText(smallCircle, countTF.drawTextFlags, QString::number(plugins.count()));
        }

        painter->restore();
    }

private:
    QLabel *m_iconLabel;
    QLabel *m_itemNameLabel;
    QLabel *m_releaseStatus;
    QWidget *m_installState;
    QLabel *m_installStateLabel;
    QLabel *m_installStateIcon;
    QLabel *m_vendorLabel;
    QWidget *m_downloads;
    QLabel *m_downloadIconLabel;
    QLabel *m_downloadDividerLabel;
    QLabel *m_downloadCountLabel;
    QLabel *m_shortDescriptionLabel;
};

class ExtensionItemDelegate : public QItemDelegate
{
public:
    explicit ExtensionItemDelegate(QObject *parent)
        : QItemDelegate(parent)
    {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)
        const override
    {
        m_itemWidget.paint(painter, option, index);
    }

    QSize sizeHint([[maybe_unused]] const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        m_itemWidget.setData(index);
        return {cellWidth, m_itemWidget.minimumSizeHint().height() + gapSize};
    }

private:
    mutable ExtensionItemWidget m_itemWidget;
};

class SortFilterProxyModel : public QSortFilterProxyModel
{
public:
    struct SortOption {
        const QString displayName;
        const Role role;
        const Qt::SortOrder order = Qt::AscendingOrder;
    };

    struct FilterOption {
        const QString displayName;
        const std::function<bool(const QModelIndex &)> indexAcceptedFunc;
    };

    SortFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    static const QList<SortOption> &sortOptions()
    {
        static const QList<SortOption> options = {
            {Tr::tr("Last updated"), RoleDateUpdated, Qt::DescendingOrder},
            {Tr::tr("Name"), RoleName},
        };
        return options;
    }

    void setSortOption(int index)
    {
        QTC_ASSERT(index < sortOptions().count(), index = 0);
        m_sortOptionIndex = index;
        const SortOption &option = sortOptions().at(index);

        // Ensure some order for cases with insufficient data, e.g. RoleDownloadCount
        setSortRole(RoleName);
        sort(0);
        if (option.role == RoleName)
            return; // Already sorted.

        setSortRole(option.role);
        sort(0, option.order);
    }

    static const QList<FilterOption> &filterOptions()
    {
        static const QList<FilterOption> options = {
            {
                Tr::tr("All", "Extensions filter"),
                []([[maybe_unused]] const QModelIndex &index) { return true; },
            },
            {
                Tr::tr("Extension packs"),
                [](const QModelIndex &index) {
                    return index.data(RoleItemType).value<ItemType>() == ItemTypePack;
                },
            },
            {
                Tr::tr("Individual extensions"),
                [](const QModelIndex &index) {
                    return index.data(RoleItemType).value<ItemType>() == ItemTypeExtension;
                },
            },
        };
        return options;
    }

    void setFilterOption(int index)
    {
        QTC_ASSERT(index < filterOptions().count(), index = 0);
        beginResetModel();
        m_filterOptionIndex = index;
        endResetModel();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override
    {
        const QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        return filterOptions().at(m_filterOptionIndex).indexAcceptedFunc(index);
    }

    int m_filterOptionIndex = 0;
    int m_sortOptionIndex = 0;
};

class ExtensionsBrowserPrivate
{
public:
    bool dataFetched = false;
    ExtensionsModel *model;
    QLineEdit *searchBox;
    OptionChooser *filterChooser;
    OptionChooser *sortChooser;
    QListView *extensionsView;
    QItemSelectionModel *selectionModel = nullptr;
    QSortFilterProxyModel *searchProxyModel;
    SortFilterProxyModel *sortFilterProxyModel;
    int columnsCount = 2;
    Tasking::TaskTreeRunner taskTreeRunner;
    SpinnerSolution::Spinner *m_spinner;
};

static QWidget *extensionViewPlaceHolder()
{
    static const TextFormat tF {Theme::Token_Text_Muted, UiElementH4};
    auto text = new QLabel;
    applyTf(text, tF, false);
    text->setAlignment(Qt::AlignCenter);
    text->setText(Tr::tr("No extension found!"));
    text->setWordWrap(true);

    using namespace Layouting;
    // clang-format off
    return Column {
        Space(SpacingTokens::ExVPaddingGapXl),
        text,
        st,
        noMargin,
    }.emerge();
    // clang-format on
}

ExtensionsBrowser::ExtensionsBrowser(ExtensionsModel *model, QWidget *parent)
    : QWidget(parent)
    , d(new ExtensionsBrowserPrivate)
{
    d->model = model;

    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    static const TextFormat titleTF
        {Theme::Token_Text_Default, UiElementH2};
    auto titleLabel = new ElidingLabel(Tr::tr("Manage Extensions"));
    applyTf(titleLabel, titleTF);

    auto externalRepoSwitch = new QtcSwitch("Use external repository");
    externalRepoSwitch->setEnabled(settings().useExternalRepo.isEnabled());
    if (settings().useExternalRepo.isEnabled())
        externalRepoSwitch->setToolTip("<html>" + externalRepoWarningNote());
    else
        externalRepoSwitch->setToolTip(settings().useExternalRepo.toolTip());

    d->searchBox = new QtcSearchBox;
    d->searchBox->setPlaceholderText(Tr::tr("Search"));

    d->searchProxyModel = new QSortFilterProxyModel(this);
    d->searchProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    d->searchProxyModel->setFilterRole(RoleSearchText);
    d->searchProxyModel->setSourceModel(d->model);

    d->sortFilterProxyModel = new SortFilterProxyModel(this);
    d->sortFilterProxyModel->setSourceModel(d->searchProxyModel);

    d->filterChooser = new OptionChooser(":/extensionmanager/images/filter.png",
                                         Tr::tr("Filter by: %1"));
    d->filterChooser->addItems(Utils::transform(SortFilterProxyModel::filterOptions(),
                                                &SortFilterProxyModel::FilterOption::displayName));
    d->filterChooser->hide(); // TODO: Unhide when ready. See QTCREATORBUG-31751

    d->sortChooser = new OptionChooser(":/extensionmanager/images/sort.png", Tr::tr("Sort by: %1"));
    d->sortChooser->addItems(Utils::transform(SortFilterProxyModel::sortOptions(),
                                              &SortFilterProxyModel::SortOption::displayName));

    auto settingsToolButton = new QPushButton;
    settingsToolButton->setIcon(Icons::SETTINGS.icon());
    settingsToolButton->setFlat(true);
    settingsToolButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    d->extensionsView = new QListView;
    d->extensionsView->setFrameStyle(QFrame::NoFrame);
    d->extensionsView->setItemDelegate(new ExtensionItemDelegate(this));
    d->extensionsView->setResizeMode(QListView::Adjust);
    d->extensionsView->setSelectionMode(QListView::SingleSelection);
    d->extensionsView->setUniformItemSizes(true);
    d->extensionsView->setViewMode(QListView::IconMode);
    d->extensionsView->setModel(d->sortFilterProxyModel);
    d->extensionsView->setMouseTracking(true);

    QStackedWidget *extensionViewStack;

    const int rightMargin = extraListViewWidth() + gapSize;
    using namespace Layouting;
    Column {
        Row {
            titleLabel,
            settingsToolButton,
            customMargins(0, VPaddingM, rightMargin, 0),
        },
        Row {
            Column {
                Row{ st, externalRepoSwitch },
                d->searchBox,
            },
            customMargins(0, VPaddingM, rightMargin, VPaddingM),
        },
        Row {
            d->filterChooser,
            st,
            d->sortChooser,
            customMargins(0, 0, rightMargin, 0),
        },
        Stack {
            bindTo(&extensionViewStack),
            d->extensionsView,
            Row {
                extensionViewPlaceHolder(),
                customMargins(0, 0, rightMargin, 0),
            },
        },
        noMargin, spacing(0),
    }.attachTo(this);

    WelcomePageHelpers::setBackgroundColor(this, Theme::Token_Background_Default);
    WelcomePageHelpers::setBackgroundColor(d->extensionsView, Theme::Token_Background_Default);
    WelcomePageHelpers::setBackgroundColor(d->extensionsView->viewport(),
                                           Theme::Token_Background_Default);

    d->m_spinner = new SpinnerSolution::Spinner(SpinnerSolution::SpinnerSize::Large, this);
    d->m_spinner->hide();

    auto updateModel = [this] {
        d->sortFilterProxyModel->sort(0);

        if (d->selectionModel == nullptr) {
            d->selectionModel = new QItemSelectionModel(d->sortFilterProxyModel,
                                                          d->extensionsView);
            d->extensionsView->setSelectionModel(d->selectionModel);
            connect(d->extensionsView->selectionModel(), &QItemSelectionModel::currentChanged,
                    this, &ExtensionsBrowser::itemSelected);
        }
    };

    auto updatePlaceHolderVisibility = [this, extensionViewStack] {
        extensionViewStack->setCurrentIndex(d->sortFilterProxyModel->rowCount() == 0 ? 1 : 0);
    };

    auto updateExternalRepoSwitch = [externalRepoSwitch] {
        const QSignalBlocker blocker(externalRepoSwitch);
        externalRepoSwitch->setChecked(settings().useExternalRepo());
    };
    updateExternalRepoSwitch();

    connect(PluginManager::instance(), &PluginManager::pluginsChanged, this, updateModel);
    connect(d->searchBox, &QLineEdit::textChanged,
            d->searchProxyModel, &QSortFilterProxyModel::setFilterWildcard);
    connect(d->sortChooser, &OptionChooser::currentIndexChanged,
            d->sortFilterProxyModel, &SortFilterProxyModel::setSortOption);
    connect(d->filterChooser, &OptionChooser::currentIndexChanged,
            d->sortFilterProxyModel, &SortFilterProxyModel::setFilterOption);
    connect(d->sortFilterProxyModel, &SortFilterProxyModel::rowsRemoved,
            this, updatePlaceHolderVisibility);
    connect(d->sortFilterProxyModel, &SortFilterProxyModel::rowsInserted,
            this, updatePlaceHolderVisibility);
    connect(settingsToolButton, &QAbstractButton::clicked, this, []() {
        ICore::showOptionsDialog(Constants::EXTENSIONMANAGER_SETTINGSPAGE_ID);
    });
    connect(&settings().useExternalRepo, &BaseAspect::changed, this, updateExternalRepoSwitch);
    connect(externalRepoSwitch, &QAbstractButton::toggled, this, [](bool checked) {
        settings().useExternalRepo.setValue(checked);
        settings().writeSettings();
    });
    connect(&settings(), &AspectContainer::changed, this, [this] {
        d->dataFetched = false;
        fetchExtensions();
    });
}

ExtensionsBrowser::~ExtensionsBrowser()
{
    delete d;
}

void ExtensionsBrowser::setFilter(const QString &filter)
{
    d->searchBox->setText(filter);
}

void ExtensionsBrowser::adjustToWidth(const int width)
{
    const int widthForItems = width - extraListViewWidth();
    d->columnsCount = qMax(1, qFloor(widthForItems / cellWidth));
    updateGeometry();
}

QSize ExtensionsBrowser::sizeHint() const
{
    const int columsWidth = d->columnsCount * cellWidth;
    return { columsWidth + extraListViewWidth(), 0};
}

int ExtensionsBrowser::extraListViewWidth() const
{
    // TODO: Investigate "transient" scrollbar, just for this list view.
    constexpr int extraPadding = qMax(0, ExVPaddingGapXl - gapSize);
    return d->extensionsView->style()->pixelMetric(QStyle::PM_ScrollBarExtent)
           + extraPadding
           + 1; // Needed
}

void ExtensionsBrowser::showEvent(QShowEvent *event)
{
    if (!d->dataFetched) {
        d->dataFetched = true;
        fetchExtensions();
    }
    QWidget::showEvent(event);
}

QModelIndex ExtensionsBrowser::currentIndex() const
{
    return d->selectionModel->currentIndex();
}

void ExtensionsBrowser::selectIndex(const QModelIndex &index)
{
    d->selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
}
class Downloader : public QObject
{
    Q_OBJECT
public:
    ~Downloader() { abort(); }

    void setUrl(const QUrl &url) { m_url = url; }
    void setDestination(QFile *file) { m_file = file; }

    void abort()
    {
        if (m_reply) {
            disconnect(m_reply, &QNetworkReply::finished, this, nullptr);
            m_reply->abort();
        }
    }

    void start()
    {
        if (!m_file || !m_file->isOpen()) {
            emit done(Tasking::DoneResult::Error);
            return;
        }

        m_reply = NetworkAccessManager::instance()->get(QNetworkRequest(m_url));
        m_reply->setParent(this);

        connect(m_reply, &QNetworkReply::readyRead, this, [this] {
            QByteArray data = m_reply->readAll();
            if (m_file->write(data) != data.size()) {
                m_file->close();
                abort();
                emit done(Tasking::DoneResult::Error);
            }
        });

        connect(m_reply, &QNetworkReply::downloadProgress, this, &Downloader::downloadProgress);
#ifndef QT_NO_SSL
        connect(m_reply, &QNetworkReply::sslErrors, this, &Downloader::sslErrors);
#endif
        connect(m_reply, &QNetworkReply::finished, this, [this] {
            m_file->close();
            if (m_reply->error() == QNetworkReply::NoError)
                emit done(Tasking::DoneResult::Success);
            else
                emit done(Tasking::DoneResult::Error);
        });

        if (m_reply->isRunning())
            emit started();
    }

signals:
    void started();
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
#ifndef QT_NO_SSL
    void sslErrors(const QList<QSslError> &errors);
#endif
    void done(Tasking::DoneResult result);

private:
    QUrl m_url;
    QFile *m_file = nullptr;
    QNetworkReply *m_reply = nullptr;
};

using DownloadTask = Tasking::SimpleCustomTask<Downloader>;

void ExtensionsBrowser::fetchExtensions()
{
#ifdef WITH_TESTS
    // Uncomment for testing with a local repository.
    // d->model->setRepositoryPath(testData("defaultdata")); return;
#endif // WITH_TESTS

    FilePaths urls = Utils::transform(settings().repositoryUrls(), &FilePath::fromUserInput);

    if (!settings().useExternalRepo() || urls.isEmpty()) {
        d->model->setRepositoryPaths({});
        return;
    }

    using namespace Tasking;

    const FilePath unpackDestination = ICore::userResourcePath() / "extensionstore";
    if (unpackDestination.exists())
        unpackDestination.removeRecursively();

    Storage<FilePaths> unpackedRepositories;
    Storage<QTemporaryFile> storage;

    LoopList urlIterator(urls);

    const auto setupDownloader = [storage, urlIterator](Downloader &downloader) {
        storage->setFileTemplate(
            QDir::tempPath() + "/extensionstore-XXXXXX." + urlIterator->completeSuffix());
        if (!storage->open())
            return SetupResult::StopWithError;
        qCDebug(browserLog) << "Downloading" << *urlIterator << "to" << storage->fileName();
        downloader.setUrl(urlIterator->toUrl());
        downloader.setDestination(&*storage);
        return SetupResult::Continue;
    };

    const auto setupUnarchiver =
        [storage, unpackDestination, urlIterator, unpackedRepositories](Unarchiver &unarchiver) {
            const FilePath archive = FilePath::fromString(storage->fileName());
            const FilePath destination = unpackDestination / archive.baseName();
            storage->flush();
            qCDebug(browserLog) << "Unpacking" << archive << "to" << destination;
            unarchiver.setArchive(archive);
            unarchiver.setDestination(destination);
            *unpackedRepositories << destination;
        };

    const auto isRemoteUrl = [urlIterator]() {
        return urlIterator->scheme() == QLatin1String("http")
               || urlIterator->scheme() == QLatin1String("https");
    };

    const auto isDirectory = [urlIterator]() { return urlIterator->isReadableDir(); };

    const auto warnInvalidUrl = [urlIterator] {
        qCWarning(browserLog) << *urlIterator
                              << "is not a http(s) url or an existing directory, skipping";
    };

    const auto addDirectory = [urlIterator, unpackedRepositories] {
        *unpackedRepositories << *urlIterator;
    };

    // clang-format off
    Group group {
        unpackedRepositories,
        Sync([this] { d->m_spinner->show(); }),
        For (urlIterator) >> Do {
            continueOnError,
            If (isRemoteUrl) >> Then {
                storage,
                DownloadTask { setupDownloader },
                UnarchiverTask { setupUnarchiver },
            } >> ElseIf(isDirectory) >> Then {
                Sync { addDirectory }
            } >> Else {
                Sync { warnInvalidUrl }
            }
        },

        onGroupDone([this, unpackedRepositories](DoneWith result) {
            d->m_spinner->hide();
            qCDebug(browserLog) << "Done with" << result << "unpacked repositories" << *unpackedRepositories;
            d->model->setRepositoryPaths(*unpackedRepositories);
        }, CallDoneIf::SuccessOrError)
    };
    // clang-format on

    d->taskTreeRunner.start(group);
}

const int iconRectRounding = 4;

QPixmap itemIcon(const QModelIndex &index, Size size)
{
    const QSize iconBgS = size == SizeSmall ? iconBgSizeSmall : iconBgSizeBig;
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(iconBgS * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);
    const QRect iconBgR(QPoint(), pixmap.deviceIndependentSize().toSize());

    const bool isEnabled = PluginManager::specExistsAndIsEnabled(index.data(RoleId).toString());
    const QGradientStops gradientStops = {
        {0, creatorColor(Theme::Token_Gradient01_Start)},
        {1, creatorColor(Theme::Token_Gradient01_End)},
    };

    const Theme::Color color = Theme::Token_Basic_White;
    static const QIcon packS = Icon({{":/extensionmanager/images/packsmall.png", color}},
                                    Icon::Tint).icon();
    static const QIcon packB = Icon({{":/extensionmanager/images/packbig.png", color}},
                                    Icon::Tint).icon();
    static const QIcon extensionS = Icon({{":/extensionmanager/images/extensionsmall.png",
                                           color}}, Icon::Tint).icon();
    static const QIcon extensionB = Icon({{":/extensionmanager/images/extensionbig.png",
                                           color}}, Icon::Tint).icon();
    const ItemType itemType = index.data(RoleItemType).value<ItemType>();
    const QIcon &icon = (itemType == ItemTypePack) ? (size == SizeSmall ? packS : packB)
                                                   : (size == SizeSmall ? extensionS : extensionB);
    const qreal iconOpacityDisabled = 0.5;

    QPainter p(&pixmap);
    QLinearGradient gradient(iconBgR.topRight(), iconBgR.bottomLeft());
    gradient.setStops(gradientStops);
    if (!isEnabled)
        p.setOpacity(iconOpacityDisabled);
    StyleHelper::drawCardBg(&p, iconBgR, gradient, Qt::NoPen, iconRectRounding);
    icon.paint(&p, iconBgR);

    return pixmap;
}

QPixmap itemBadge(const QModelIndex &index, [[maybe_unused]] Size size)
{
    const QString badgeText = index.data(RoleBadge).toString();
    if (badgeText.isNull())
        return {};

    constexpr TextFormat badgeTF
        {Theme::Token_Basic_White, UiElement::UiElementLabelSmall};

    const QFont font = badgeTF.font();
    const int textWidth = QFontMetrics(font).horizontalAdvance(badgeText);
    const QSize badgeS(ExPaddingGapM + textWidth + ExPaddingGapM,
                       ExPaddingGapS + badgeTF.lineHeight() + ExPaddingGapS);
    const QRect badgeR(QPoint(), badgeS);
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap(badgeS * dpr);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(dpr);

    QPainter p(&pixmap);
    StyleHelper::drawCardBg(&p, badgeR, creatorColor(Theme::Token_Notification_Neutral_Default),
                            Qt::NoPen, iconRectRounding);
    p.setFont(font);
    p.setPen(badgeTF.color());
    p.drawText(badgeR, Qt::AlignCenter, badgeText);
    return pixmap;
}

} // ExtensionManager::Internal

#include "extensionsbrowser.moc"
