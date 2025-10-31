// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "appoutputpane.h"

#include "project.h"
#include "projectexplorer.h"
#include "projectexplorerconstants.h"
#include "projectexplorericons.h"
#include "projectexplorersettings.h"
#include "projectexplorertr.h"
#include "projectmanager.h"
#include "runcontrol.h"
#include "runconfigurationaspects.h"
#include "showoutputtaskhandler.h"
#include "windebuginterface.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/coreicons.h>
#include <coreplugin/icore.h>
#include <coreplugin/outputwindow.h>
#include <coreplugin/session.h>
#include <texteditor/behaviorsettings.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>

#include <extensionsystem/invoker.h>
#include <extensionsystem/pluginmanager.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/basetreeview.h>
#include <utils/layoutbuilder.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>
#include <utils/qtcolorbutton.h>
#include <utils/storekey.h>
#include <utils/stylehelper.h>
#include <utils/utilsicons.h>

#include <QAbstractListModel>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

static Q_LOGGING_CATEGORY(appOutputLog, "qtc.projectexplorer.appoutput", QtWarningMsg);

using namespace Core;
using namespace Utils;

namespace ProjectExplorer {
namespace Internal {

const char OPTIONS_PAGE_ID[] = "B.ProjectExplorer.AppOutputOptions";
const char SETTINGS_KEY[] = "ProjectExplorer/AppOutput/Zoom";
const char C_APP_OUTPUT[] = "ProjectExplorer.ApplicationOutput";
const char POP_UP_FOR_RUN_OUTPUT_KEY[] = "ProjectExplorer/Settings/ShowRunOutput";
const char POP_UP_FOR_DEBUG_OUTPUT_KEY[] = "ProjectExplorer/Settings/ShowDebugOutput";
const char CLEAN_OLD_OUTPUT_KEY[] = "ProjectExplorer/Settings/CleanOldAppOutput";
const char MERGE_CHANNELS_KEY[] = "ProjectExplorer/Settings/MergeStdErrAndStdOut";
const char WRAP_OUTPUT_KEY[] = "ProjectExplorer/Settings/WrapAppOutput";
const char DISCARD_OUTPUT_KEY[] = "ProjectExplorer/Settings/DiscardAppOutput";
const char MAX_LINES_KEY[] = "ProjectExplorer/Settings/MaxAppOutputLines";
const char OVERWRITE_BG_KEY[] = "ProjectExplorer/Settings/OverwriteBackground";
const char BACKGROUND_COLOR_KEY[] = "ProjectExplorer/Settings/BackgroundColor";

static QObject *debuggerPlugin()
{
    return ExtensionSystem::PluginManager::getObjectByName("DebuggerPlugin");
}

static QString msgAttachDebuggerTooltip(const QString &handleDescription = QString())
{
    return handleDescription.isEmpty() ?
           Tr::tr("Attach debugger to this process") :
           Tr::tr("Attach debugger to %1").arg(handleDescription);
}

static inline QString messageTypeToString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return {"Debug"};
    case QtInfoMsg:
        return {"Info"};
    case QtCriticalMsg:
        return {"Critical"};
    case QtWarningMsg:
        return {"Warning"};
    case QtFatalMsg:
        return {"Fatal"};
    default:
        return {"Unknown"};
    }
}

class LoggingCategoryRegistry : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    ~LoggingCategoryRegistry() { reset(); }

    QMap<QString, QLoggingCategory *> categories() { return m_categories; }

    void onNewCategory(const QString &data)
    {
        const QStringList catList = data.split(' ');
        QTC_ASSERT(catList.size() == 5, return);

        const QString catName = catList.first();
        if (m_categories.contains(catName))
            return;

        const auto category = new QLoggingCategory(catName.toUtf8());
        category->setEnabled(QtDebugMsg, catList.at(1).toInt());
        category->setEnabled(QtWarningMsg, catList.at(2).toInt());
        category->setEnabled(QtCriticalMsg, catList.at(3).toInt());
        category->setEnabled(QtInfoMsg, catList.at(4).toInt());

        m_categories[catName] = category;
        emit newLogCategory(catName, category);
    }

    void reset()
    {
        qDeleteAll(m_categories);
        m_categories.clear();
    }

signals:
    void newLogCategory(QString name, QLoggingCategory *category);

private:
    QMap<QString, QLoggingCategory *> m_categories;
};

class AppOutputWindow : public Core::OutputWindow
{
    Q_OBJECT

public:
    using OutputWindow::OutputWindow;

    void updateCategoriesProperties(const QMap<QString, QLoggingCategory *> &categories)
    {
        resetLastFilteredBlockNumber();
        m_categories = categories;
    }

    void setFilterEnabled(bool enabled) { m_filterEnabled = enabled; }
    bool filterEnabled() const { return m_filterEnabled; }

    LoggingCategoryRegistry *registry() { return &m_registry; }

private:
    TextMatchingFunction makeMatchingFilterFunction() const override
    {
        auto parentFilter = OutputWindow::makeMatchingFilterFunction();

        auto filter = [categories = m_categories](const QString &text) {
            if (categories.isEmpty())
                return true;

            for (auto i = categories.cbegin(), end = categories.cend(); i != end; ++i) {
                if (!text.contains(i.key()))
                    continue;
                QLoggingCategory * const cat = i.value();
                if (text.contains("[F]"))
                    return true;
                if (text.contains("[D]") && !cat->isDebugEnabled())
                    return false;
                if (text.contains("[W]") && !cat->isWarningEnabled())
                    return false;
                if (text.contains("[C]") && !cat->isCriticalEnabled())
                    return false;
                if (text.contains("[I]") && !cat->isInfoEnabled())
                    return false;
                return true;
            }
            return true;
        };

        return [filter, parentFilter](const QString &text) {
            return filter(text) && parentFilter(text);
        };
    }

    bool shouldFilterNewContentOnBlockCountChanged() const override
    {
        return m_filterEnabled || OutputWindow::shouldFilterNewContentOnBlockCountChanged();
    }

    LoggingCategoryRegistry m_registry{this};
    QMap<QString, QLoggingCategory *> m_categories;
    bool m_filterEnabled = false;
};

class LoggingCategoryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    using QAbstractListModel::QAbstractListModel;
    enum Column { Name, Debug, Warning, Critical, Fatal, Info };

    int columnCount(const QModelIndex &) const final { return 6; }
    int rowCount(const QModelIndex & = QModelIndex()) const final { return m_categories.size(); }

    void append(QString name, QLoggingCategory *category)
    {
        beginInsertRows(QModelIndex(), m_categories.size(), m_categories.size() + 1);
        m_categories.push_back({name, category});
        endInsertRows();
    }

    QVariant data(const QModelIndex &index, int role) const final
    {
        if (!index.isValid())
            return {};
        if (index.column() == Column::Name && role == Qt::DisplayRole)
            return m_categories.at(index.row()).first;
        if (index.column() >= Column::Debug && index.column() <= Column::Info
            && role == Qt::CheckStateRole) {
            auto entry = m_categories.at(index.row()).second;
            const bool isEnabled = entry->isEnabled(
                static_cast<QtMsgType>(index.column() - Column::Debug));
            return isEnabled ? Qt::Checked : Qt::Unchecked;
        }
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) final
    {
        if (!index.isValid())
            return false;
        if (role == Qt::CheckStateRole && index.column() >= Column::Debug
            && index.column() <= Column::Info) {
            QtMsgType msgType = static_cast<QtMsgType>(index.column() - Column::Debug);
            QLoggingCategory * const cat = m_categories[index.row()].second;
            bool isEnabled = cat->isEnabled(msgType);
            const Qt::CheckState current = isEnabled ? Qt::Checked : Qt::Unchecked;
            if (current != value.toInt()) {
                cat->setEnabled(msgType, value.toInt() == Qt::Checked);
                emit categoryChanged(m_categories[index.row()].first, cat);
                return true;
            }
        }
        return false;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const final
    {
        if (!index.isValid() || index.column() == LoggingCategoryModel::Column::Fatal)
            return Qt::NoItemFlags;
        if (index.column() == Column::Name)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }

    QVariant headerData(
        int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const final
    {
        if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
            return {};

        switch (section) {
        case Column::Name:
            return Tr::tr("Category");
        case Column::Debug:
            return Tr::tr("Debug");
        case Column::Warning:
            return Tr::tr("Warning");
        case Column::Critical:
            return Tr::tr("Critical");
        case Column::Fatal:
            return Tr::tr("Fatal");
        case Column::Info:
            return Tr::tr("Info");
        default:
            break;
        }

        return {};
    }

    void reset()
    {
        beginResetModel();
        m_categories.clear();
        endResetModel();
    }

signals:
    void categoryChanged(QString name, QLoggingCategory *category);

private:
    QList<QPair<QString, QLoggingCategory *>> m_categories;
};

AppOutputPane::RunControlTab::RunControlTab(RunControl *runControl, Core::OutputWindow *w) :
    runControl(runControl), window(w)
{
    if (runControl && w) {
        w->reset();
        runControl->setupFormatter(w->outputFormatter());
    }
}

AppOutputPane::AppOutputPane() :
    m_tabWidget(new QStackedWidget),
    m_stopAction(new QAction(Tr::tr("Stop"), this)),
    m_reRunButton(new QToolButton),
    m_stopButton(new QToolButton),
    m_attachButton(new QToolButton),
    m_settingsButton(new QToolButton),
    m_formatterWidget(new QWidget),
    m_tabComboBox(new QComboBox),
    m_closeCurrentTabButton(new QToolButton),
    m_handler(new ShowOutputTaskHandler(this,
        Tr::tr("Show &App Output"),
        Tr::tr("Show the output that generated this issue in Application Output."),
        Tr::tr("A")))
{
    setId("ApplicationOutput");
    setDisplayName(Tr::tr("Application Output"));
    setPriorityInStatusBar(60);

    ExtensionSystem::PluginManager::addObject(m_handler);

    setObjectName("AppOutputPane"); // Used in valgrind engine
    loadSettings();

    // Rerun
    m_reRunButton->setIcon(Utils::Icons::RUN_SMALL_TOOLBAR.icon());
    m_reRunButton->setToolTip(Tr::tr("Re-run this run-configuration."));
    m_reRunButton->setEnabled(false);
    connect(m_reRunButton, &QToolButton::clicked,
            this, &AppOutputPane::reRunRunControl);

    // Stop
    m_stopAction->setIcon(Utils::Icons::STOP_SMALL_TOOLBAR.icon());
    m_stopAction->setToolTip(Tr::tr("Stop running program."));
    m_stopAction->setEnabled(false);

    Core::Command *cmd = Core::ActionManager::registerAction(m_stopAction, Constants::STOP);
    cmd->setDescription(m_stopAction->toolTip());

    m_stopButton->setDefaultAction(cmd->action());

    connect(m_stopAction, &QAction::triggered,
            this, &AppOutputPane::stopRunControl);

    // Attach
    m_attachButton->setToolTip(msgAttachDebuggerTooltip());
    m_attachButton->setEnabled(false);
    m_attachButton->setIcon(Icons::DEBUG_START_SMALL_TOOLBAR.icon());

    connect(m_attachButton, &QToolButton::clicked,
            this, &AppOutputPane::attachToRunControl);

    connect(this, &IOutputPane::zoomInRequested, this, &AppOutputPane::zoomIn);
    connect(this, &IOutputPane::zoomOutRequested, this, &AppOutputPane::zoomOut);
    connect(this, &IOutputPane::resetZoomRequested, this, &AppOutputPane::resetZoom);

    m_settingsButton->setToolTip(Core::ICore::msgShowOptionsDialog());
    m_settingsButton->setIcon(Utils::Icons::SETTINGS_TOOLBAR.icon());
    connect(m_settingsButton, &QToolButton::clicked, this, [] {
        Core::ICore::showOptionsDialog(OPTIONS_PAGE_ID);
    });

    auto formatterWidgetsLayout = new QHBoxLayout;
    formatterWidgetsLayout->setContentsMargins(QMargins());
    m_formatterWidget->setLayout(formatterWidgetsLayout);

    // Spacer (?)
    m_closeCurrentTabButton->setToolTip(Tr::tr("Close output configuration."));
    m_closeCurrentTabButton->setIcon(Utils::Icons::CLOSE_TOOLBAR.icon());
    m_closeCurrentTabButton->setEnabled(false);
    connect(m_closeCurrentTabButton, &QToolButton::clicked, this, [this] {
        closeTab(m_tabWidget->currentIndex());
    });
    
    m_tabComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_tabComboBox->addItem(Tr::tr("<no configuration>"));
    connect(m_tabComboBox, &QComboBox::activated,
            m_tabWidget, &QStackedWidget::setCurrentIndex);
    connect(m_tabWidget, &QStackedWidget::currentChanged,
            m_tabComboBox, &QComboBox::setCurrentIndex);

    connect(m_tabWidget, &QStackedWidget::currentChanged,
            this, &AppOutputPane::tabChanged);

    connect(SessionManager::instance(), &SessionManager::aboutToUnloadSession,
            this, &AppOutputPane::aboutToUnloadSession);
    connect(ProjectManager::instance(), &ProjectManager::projectRemoved,
            this, &AppOutputPane::projectRemoved);

    setupFilterUi("AppOutputPane.Filter", "ProjectExplorer::Internal::AppOutputPane");
    setFilteringEnabled(false);
    setZoomButtonsEnabled(false);
    setupContext("Core.AppOutputPane", m_tabWidget);
}

AppOutputPane::~AppOutputPane()
{
    qCDebug(appOutputLog) << "AppOutputPane::~AppOutputPane: Entries left" << m_runControlTabs.size();

    for (const RunControlTab &rt : std::as_const(m_runControlTabs)) {
        delete rt.window;
        delete rt.runControl;
    }
    delete m_tabWidget;
    ExtensionSystem::PluginManager::removeObject(m_handler);
    delete m_handler;
}

AppOutputPane::RunControlTab *AppOutputPane::currentTab()
{
    return tabFor(m_tabWidget->currentWidget());
}

const AppOutputPane::RunControlTab *AppOutputPane::currentTab() const
{
    return tabFor(m_tabWidget->currentWidget());
}

RunControl *AppOutputPane::currentRunControl() const
{
    if (const RunControlTab * const tab = currentTab())
        return tab->runControl;
    return nullptr;
}

AppOutputPane::RunControlTab *AppOutputPane::tabFor(const RunControl *rc)
{
    const auto it = std::find_if(m_runControlTabs.begin(), m_runControlTabs.end(),
                                 [rc](RunControlTab &t) { return t.runControl == rc; });
    if (it == m_runControlTabs.end())
        return nullptr;
    return &*it;
}

AppOutputPane::RunControlTab *AppOutputPane::tabFor(const QWidget *outputWindow)
{
    const auto it = std::find_if(m_runControlTabs.begin(), m_runControlTabs.end(),
            [outputWindow](RunControlTab &t) { return t.window == outputWindow; });
    if (it == m_runControlTabs.end())
        return nullptr;
    return &*it;
}

const AppOutputPane::RunControlTab *AppOutputPane::tabFor(const QWidget *outputWindow) const
{
    return const_cast<AppOutputPane *>(this)->tabFor(outputWindow);
}

void AppOutputPane::updateCloseActions()
{
    const int tabCount = m_tabWidget->count();
    m_closeCurrentTabButton->setEnabled(tabCount > 0);
}

bool AppOutputPane::aboutToClose() const
{
    return Utils::allOf(m_runControlTabs, [](const RunControlTab &rt) {
        return !rt.runControl || !rt.runControl->isRunning() || rt.runControl->promptToStop();
    });
}

void AppOutputPane::aboutToUnloadSession()
{
    closeTabs(CloseTabWithPrompt);
}

QWidget *AppOutputPane::outputWidget(QWidget *)
{
    return m_tabWidget;
}

QList<QWidget *> AppOutputPane::toolBarWidgets() const
{
    return QList<QWidget *>{m_tabComboBox, m_closeCurrentTabButton, m_reRunButton, m_stopButton, 
      m_attachButton, m_settingsButton, m_formatterWidget} + IOutputPane::toolBarWidgets();
}

void AppOutputPane::clearContents()
{
    auto *currentWindow = qobject_cast<Core::OutputWindow *>(m_tabWidget->currentWidget());
    if (currentWindow)
        currentWindow->clear();
}

bool AppOutputPane::hasFocus() const
{
    QWidget *widget = m_tabWidget->currentWidget();
    if (!widget)
        return false;
    return widget->window()->focusWidget() == widget;
}

bool AppOutputPane::canFocus() const
{
    return m_tabWidget->currentWidget();
}

void AppOutputPane::setFocus()
{
    if (m_tabWidget->currentWidget())
        m_tabWidget->currentWidget()->setFocus();
}

void AppOutputPane::updateFilter()
{
    if (RunControlTab * const tab = currentTab()) {
        auto appwindow = qobject_cast<AppOutputWindow*>(tab->window);
        appwindow->updateCategoriesProperties(appwindow->registry()->categories());
        if (!tab->window->updateFilterProperties(
                filterText(),
                filterCaseSensitivity(),
                filterUsesRegexp(),
                filterIsInverted(),
                beforeContext(),
                afterContext())) {
            tab->window->filterNewContent();
        }
    }
}

const QList<Core::OutputWindow *> AppOutputPane::outputWindows() const
{
    QList<Core::OutputWindow *> windows;
    for (const RunControlTab &tab : std::as_const(m_runControlTabs)) {
        if (tab.window)
            windows << tab.window;
    }
    return windows;
}

void AppOutputPane::ensureWindowVisible(Core::OutputWindow *ow)
{
    m_tabWidget->setCurrentWidget(ow);
}

void AppOutputPane::createNewOutputWindow(RunControl *rc)
{
    QTC_ASSERT(rc, return);

    auto runControlChanged = [this, rc] {
        RunControl *current = currentRunControl();
        if (current && current == rc)
            enableButtons(current); // RunControl::isRunning() cannot be trusted in signal handler.
    };

    connect(rc, &RunControl::aboutToStart, this, runControlChanged);
    connect(rc, &RunControl::started, this, runControlChanged);
    connect(rc, &RunControl::stopped, this, [this, rc] {
        QTimer::singleShot(0, this, [this, rc] { runControlFinished(rc); });
        for (const RunControlTab &t : std::as_const(m_runControlTabs)) {
            if (t.runControl == rc) {
                if (t.window)
                    t.window->flush();
                break;
            }
        }
    });
    connect(rc, &RunControl::applicationProcessHandleChanged,
            this, &AppOutputPane::enableDefaultButtons);
    connect(rc, &RunControl::appendMessage,
            this, [this, rc](const QString &out, OutputFormat format) {
                appendMessage(rc, out, format);
            });

    // First look if we can reuse a tab
    const CommandLine thisCommand = rc->commandLine();
    const FilePath thisWorkingDirectory = rc->workingDirectory();
    const Environment thisEnvironment = rc->environment();
    const auto tab = std::find_if(
        m_runControlTabs.begin(), m_runControlTabs.end(), [&](const RunControlTab &tab) {
            if (!tab.runControl || !tab.runControl->isStopped())
                return false;
            return thisCommand == tab.runControl->commandLine()
                   && thisWorkingDirectory == tab.runControl->workingDirectory()
                   && thisEnvironment == tab.runControl->environment();
        });
    const auto updateOutputFileName = [this](int index, RunControl *rc) {
        qobject_cast<OutputWindow *>(m_tabWidget->widget(index))
        //: file name suggested for saving application output, %1 = run configuration display name
        ->setOutputFileNameHint(Tr::tr("application-output-%1.txt").arg(rc->displayName()));
    };
    const auto updateOutputFiltersWidget = [this](int index, RunControl *rc) {
        const auto aspect = rc->aspectData<EnableCategoriesFilterAspect>();
        const bool filterEnabled = aspect && aspect->value;
        qobject_cast<AppOutputWindow *>(m_tabWidget->widget(index))->setFilterEnabled(filterEnabled);
    };
    if (tab != m_runControlTabs.end()) {
        // Reuse this tab
        if (tab->runControl)
            delete tab->runControl;

        tab->runControl = rc;
        tab->window->reset();
        rc->setupFormatter(tab->window->outputFormatter());

        handleOldOutput(tab->window);

        // Update the title.
        const int tabIndex = m_tabWidget->indexOf(tab->window);
        QTC_ASSERT(tabIndex != -1, return);
        m_tabComboBox->setItemText(tabIndex, rc->displayName());
        updateOutputFileName(tabIndex, rc);
        updateOutputFiltersWidget(tabIndex, rc);

        tab->window->scrollToBottom();
        qCDebug(appOutputLog) << "AppOutputPane::createNewOutputWindow: Reusing tab"
                              << tabIndex << "for" << rc;
        return;
    }
    // Create new
    static int counter = 0;
    Id contextId = Id(C_APP_OUTPUT).withSuffix(counter++);
    Core::Context context(contextId);
    AppOutputWindow *ow = new AppOutputWindow(context, SETTINGS_KEY, m_tabWidget);
    ow->setWindowTitle(Tr::tr("Application Output Window"));
    ow->setWindowIcon(Icons::WINDOW.icon());
    ow->setWordWrapEnabled(m_settings.wrapOutput);
    ow->setMaxCharCount(m_settings.maxCharCount);
    ow->setDiscardExcessiveOutput(m_settings.discardExcessiveOutput);

    const QColor bgColor = m_settings.effectiveBackgroundColor();
    ow->outputFormatter()->setExplicitBackgroundColor(bgColor);
    StyleHelper::modifyPaletteBase(ow, bgColor);

    auto updateFontSettings = [ow] {
        ow->setBaseFont(TextEditor::TextEditorSettings::fontSettings().font());
    };

    auto updateBehaviorSettings = [ow] {
        ow->setWheelZoomEnabled(
                    TextEditor::globalBehaviorSettings().m_scrollWheelZooming);
    };

    updateFontSettings();
    updateBehaviorSettings();

    connect(ow, &Core::OutputWindow::wheelZoom, this, [this, ow]() {
        float fontZoom = ow->fontZoom();
        for (const RunControlTab &tab : std::as_const(m_runControlTabs))
            tab.window->setFontZoom(fontZoom);
    });
    connect(TextEditor::TextEditorSettings::instance(), &TextEditor::TextEditorSettings::fontSettingsChanged,
            ow, updateFontSettings);
    connect(TextEditor::TextEditorSettings::instance(), &TextEditor::TextEditorSettings::behaviorSettingsChanged,
            ow, updateBehaviorSettings);

    auto qtInternal = new QToolButton;
    qtInternal->setIcon(Core::Icons::QTLOGO.icon());
    qtInternal->setToolTip(Tr::tr("Filter Qt Internal Log Categories"));
    qtInternal->setCheckable(false);

    LoggingCategoryModel *categoryModel = new LoggingCategoryModel(this);
    QSortFilterProxyModel *sortFilterModel = new QSortFilterProxyModel(this);
    sortFilterModel->setSourceModel(categoryModel);
    sortFilterModel->sort(LoggingCategoryModel::Column::Name);
    sortFilterModel->setFilterKeyColumn(LoggingCategoryModel::Column::Name);

    connect(ow->registry(), &LoggingCategoryRegistry::newLogCategory,
            categoryModel, &LoggingCategoryModel::append);
    connect(categoryModel,&LoggingCategoryModel::categoryChanged,
            this, &AppOutputPane::updateFilter);

    BaseTreeView *categoryView = new BaseTreeView;
    categoryView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    categoryView->setFrameStyle(QFrame::Box);
    categoryView->setAttribute(Qt::WA_MacShowFocusRect, false);
    categoryView->setSelectionMode(QAbstractItemView::SingleSelection);
    categoryView->setContextMenuPolicy(Qt::CustomContextMenu);
    categoryView->setModel(sortFilterModel);

    for (int i = LoggingCategoryModel::Column::Name + 1; i < LoggingCategoryModel::Column::Info; i++)
        categoryView->resizeColumnToContents(i);

    auto filterEdit = new Utils::FancyLineEdit;
    filterEdit->setHistoryCompleter("LogFilterCompletionHistory");
    filterEdit->setFiltering(true);
    filterEdit->setPlaceholderText(Tr::tr("Filter categories by regular expression"));
    filterEdit->setValidationFunction(
        [](const QString &input) {
            return Utils::asyncRun([input]() -> Utils::Result<QString> {
                QRegularExpression re(input);
                if (re.isValid())
                    return input;

                return ResultError(
                    Tr::tr("Invalid regular expression: %1").arg(re.errorString()));
            });
        });
    connect(filterEdit,
            &Utils::FancyLineEdit::textChanged,
            sortFilterModel,
            [sortFilterModel](const QString &f) {
                QRegularExpression re(f);
                if (re.isValid())
                    sortFilterModel->setFilterRegularExpression(f);
            });

    connect(categoryView,
            &QAbstractItemView::customContextMenuRequested,
            this,
            [=] (const QPoint &pos) {
                QModelIndex idx = categoryView->indexAt(pos);

                QMenu m;
                auto uncheckAll = new QAction(Tr::tr("Uncheck All"), &m);

                auto isTypeColumn = [](int column) {
                    return column >= LoggingCategoryModel::Column::Debug
                           && column <= LoggingCategoryModel::Column::Info;
                };

                auto setChecked = [sortFilterModel](std::initializer_list<LoggingCategoryModel::Column> columns,
                                         Qt::CheckState checked) {
                    for (int row = 0, count = sortFilterModel->rowCount(); row < count; ++row) {
                        for (int column : columns) {
                            sortFilterModel->setData(sortFilterModel->index(row, column),
                                                       checked,
                                                       Qt::CheckStateRole);
                        }
                    }
                };

                if (idx.isValid() && isTypeColumn(idx.column())) {
                    const LoggingCategoryModel::Column column = static_cast<LoggingCategoryModel::Column>(
                        idx.column());
                    bool isChecked = idx.data(Qt::CheckStateRole).toInt() == Qt::Checked;
                    const QString uncheckText = isChecked ? Tr::tr("Uncheck All %1") : Tr::tr("Check All %1");

                    uncheckAll->setText(uncheckText.arg(messageTypeToString(
                        static_cast<QtMsgType>(column - LoggingCategoryModel::Column::Debug))));

                    Qt::CheckState newState = isChecked ? Qt::Unchecked : Qt::Checked;

                    connect(uncheckAll,
                            &QAction::triggered,
                            sortFilterModel,
                            [setChecked, column, newState]() { setChecked({column}, newState); });

                } else {
                    // No need to add Fatal here, as it is read-only
                    static auto allColumns = {LoggingCategoryModel::Column::Debug,
                                              LoggingCategoryModel::Column::Warning,
                                              LoggingCategoryModel::Column::Critical,
                                              LoggingCategoryModel::Column::Info};

                    connect(uncheckAll, &QAction::triggered, sortFilterModel, [setChecked]() {
                        setChecked(allColumns, Qt::Unchecked);
                    });
                }

                m.addAction(uncheckAll);
                m.exec(categoryView->mapToGlobal(pos));
            });

    connect(qtInternal, &QToolButton::clicked, filterEdit, [filterEdit] {
        filterEdit->setText("^(qt\\.).+");
    });

    connect(ow, &OutputWindow::cleanOldOutput, ow, [ow, categoryModel]() {
        categoryModel->reset();
        ow->updateCategoriesProperties({});
        ow->registry()->reset();
    });

    QWidget* cv = new QWidget;

    using namespace Layouting;
    // clang-format off
    Column {
        noMargin,
        Row {
            qtInternal,
            filterEdit,
        },
        categoryView,
    }.attachTo(cv);
    // clang-format on

    m_runControlTabs.push_back(RunControlTab(rc, ow));
    if (m_tabWidget->count() > 0) {
        m_tabComboBox->addItem(rc->displayName());
    } else {
        m_tabComboBox->setItemText(0, rc->displayName());
    }
    m_tabWidget->addWidget(ow);
    updateOutputFileName(m_tabWidget->count() - 1, rc);
    updateOutputFiltersWidget(m_tabWidget->count() - 1, rc);
    qCDebug(appOutputLog) << "AppOutputPane::createNewOutputWindow: Adding tab for" << rc;
    updateCloseActions();
    setFilteringEnabled(m_tabWidget->count() > 0);
}

void AppOutputPane::handleOldOutput(Core::OutputWindow *window) const
{
    if (m_settings.cleanOldOutput)
        window->clear();
    else
        window->grayOutOldContent();

    emit window->cleanOldOutput();
}

void AppOutputPane::updateFromSettings()
{
    const QColor bgColor = m_settings.effectiveBackgroundColor();
    for (const RunControlTab &tab : std::as_const(m_runControlTabs)) {
        tab.window->setWordWrapEnabled(m_settings.wrapOutput);
        tab.window->setMaxCharCount(m_settings.maxCharCount);
        tab.window->setDiscardExcessiveOutput(m_settings.discardExcessiveOutput);
        tab.window->outputFormatter()->setExplicitBackgroundColor(bgColor);
        StyleHelper::modifyPaletteBase(tab.window, bgColor);
    }
}

void AppOutputPane::appendMessage(RunControl *rc, const QString &out, OutputFormat format)
{
    RunControlTab * const tab = tabFor(rc);
    if (!tab)
        return;

    if (qobject_cast<AppOutputWindow *>(tab->window)->filterEnabled()) {
        const QStringList lines = out.split('\n');
        for (const QString &line : lines) {
            if (line.contains("_logging_categories") && line.contains("CATEGORY:")) {
                auto appwindow = qobject_cast<AppOutputWindow*>(tab->window);
                appwindow->registry()->onNewCategory(line.section("CATEGORY:", 1, 1).section('\n', 0, 0));
            }
        }
    }

    QString stringToWrite;
    if (format == NormalMessageFormat || format == ErrorMessageFormat) {
        stringToWrite = QTime::currentTime().toString();
        stringToWrite += ": ";
    }
    stringToWrite += out;
    tab->window->appendMessage(stringToWrite, format);

    if (format != NormalMessageFormat) {
        switch (tab->behaviorOnOutput) {
        case AppOutputPaneMode::FlashOnOutput:
            flash();
            break;
        case AppOutputPaneMode::PopupOnFirstOutput:
            tab->behaviorOnOutput = AppOutputPaneMode::FlashOnOutput;
            Q_FALLTHROUGH();
        case AppOutputPaneMode::PopupOnOutput:
            popup(NoModeSwitch);
            break;
        }
    }
}

void AppOutputPane::setSettings(const AppOutputSettings &settings)
{
    m_settings = settings;
    storeSettings();
    updateFromSettings();
}

void AppOutputPane::prepareRunControlStart(RunControl *runControl)
{
    createNewOutputWindow(runControl);
    flash(); // one flash for starting
    showTabFor(runControl);
    Id runMode = runControl->runMode();
    const auto popupMode = runMode == Constants::NORMAL_RUN_MODE
            ? settings().runOutputMode
            : runMode == Constants::DEBUG_RUN_MODE
                ? settings().debugOutputMode
                : AppOutputPaneMode::FlashOnOutput;
    setBehaviorOnOutput(runControl, popupMode);
}

void AppOutputPane::showOutputPaneForRunControl(RunControl *runControl)
{
    showTabFor(runControl);
    popup(IOutputPane::NoModeSwitch | IOutputPane::WithFocus);
}

void AppOutputPane::closeTabsWithoutPrompt()
{
    closeTabs(CloseTabNoPrompt);
}

const AppOutputPaneMode kRunOutputModeDefault = AppOutputPaneMode::PopupOnFirstOutput;
const AppOutputPaneMode kDebugOutputModeDefault = AppOutputPaneMode::FlashOnOutput;
const bool kCleanOldOutputDefault = false;
const bool kMergeChannelsDefault = false;
const bool kWrapOutputDefault = true;
const bool kDiscardOutputDefault = false;
const bool kOverwriteBGDefault = false;

void AppOutputPane::storeSettings() const
{
    QtcSettings *const s = Core::ICore::settings();
    s->setValueWithDefault(POP_UP_FOR_RUN_OUTPUT_KEY,
                           int(m_settings.runOutputMode),
                           int(kRunOutputModeDefault));
    s->setValueWithDefault(POP_UP_FOR_DEBUG_OUTPUT_KEY,
                           int(m_settings.debugOutputMode),
                           int(kDebugOutputModeDefault));
    s->setValueWithDefault(CLEAN_OLD_OUTPUT_KEY, m_settings.cleanOldOutput, kCleanOldOutputDefault);
    s->setValueWithDefault(MERGE_CHANNELS_KEY, m_settings.mergeChannels, kMergeChannelsDefault);
    s->setValueWithDefault(WRAP_OUTPUT_KEY, m_settings.wrapOutput, kWrapOutputDefault);
    s->setValueWithDefault(
        DISCARD_OUTPUT_KEY, m_settings.discardExcessiveOutput, kDiscardOutputDefault);
    s->setValueWithDefault(MAX_LINES_KEY,
                           m_settings.maxCharCount / 100,
                           Core::Constants::DEFAULT_MAX_CHAR_COUNT / 100);
    s->setValueWithDefault(OVERWRITE_BG_KEY, m_settings.overwriteBackground, kOverwriteBGDefault);
    s->setValueWithDefault(BACKGROUND_COLOR_KEY, m_settings.backgroundColor,
                           AppOutputSettings::defaultBackgroundColor());
}

void AppOutputPane::loadSettings()
{
    QtcSettings * const s = Core::ICore::settings();
    const auto modeFromSettings = [s](const Key key, AppOutputPaneMode defaultValue) {
        return static_cast<AppOutputPaneMode>(s->value(key, int(defaultValue)).toInt());
    };
    m_settings.runOutputMode = modeFromSettings(POP_UP_FOR_RUN_OUTPUT_KEY, kRunOutputModeDefault);
    m_settings.debugOutputMode = modeFromSettings(POP_UP_FOR_DEBUG_OUTPUT_KEY,
                                                  kDebugOutputModeDefault);
    m_settings.cleanOldOutput = s->value(CLEAN_OLD_OUTPUT_KEY, kCleanOldOutputDefault).toBool();
    m_settings.mergeChannels = s->value(MERGE_CHANNELS_KEY, kMergeChannelsDefault).toBool();
    m_settings.wrapOutput = s->value(WRAP_OUTPUT_KEY, kWrapOutputDefault).toBool();
    m_settings.discardExcessiveOutput = s->value(DISCARD_OUTPUT_KEY, kDiscardOutputDefault).toBool();
    m_settings.maxCharCount = s->value(MAX_LINES_KEY,
                                       Core::Constants::DEFAULT_MAX_CHAR_COUNT / 100).toInt() * 100;
    m_settings.overwriteBackground = s->value(OVERWRITE_BG_KEY, kOverwriteBGDefault).toBool();
    const QColor background = s->value(BACKGROUND_COLOR_KEY, QColor()).value<QColor>();
    m_settings.backgroundColor = background.isValid() ? background
                                                      : AppOutputSettings::defaultBackgroundColor();
}

void AppOutputPane::showTabFor(RunControl *rc)
{
    if (RunControlTab * const tab = tabFor(rc))
        m_tabWidget->setCurrentWidget(tab->window);
}

void AppOutputPane::setBehaviorOnOutput(RunControl *rc, AppOutputPaneMode mode)
{
    if (RunControlTab * const tab = tabFor(rc))
        tab->behaviorOnOutput = mode;
}

void AppOutputPane::reRunRunControl()
{
    RunControlTab * const tab = currentTab();
    QTC_ASSERT(tab, return);
    QTC_ASSERT(tab->runControl, return);
    QTC_ASSERT(!tab->runControl->isRunning(), return);

    handleOldOutput(tab->window);
    tab->window->scrollToBottom();
    tab->runControl->initiateStart();
}

void AppOutputPane::attachToRunControl()
{
    RunControl * const rc = currentRunControl();
    QTC_ASSERT(rc, return);
    QTC_ASSERT(rc->isRunning(), return);
    ExtensionSystem::Invoker<void>(debuggerPlugin(), "attachExternalApplication", rc);
}

void AppOutputPane::stopRunControl()
{
    RunControl * const rc = currentRunControl();
    QTC_ASSERT(rc, return);

    if (rc->isRunning()) {
        if (optionallyPromptToStop(rc)) {
            rc->initiateStop();
            enableButtons(rc);
        }
    } else {
        QTC_CHECK(false);
        rc->forceStop();
    }

    qCDebug(appOutputLog) << "AppOutputPane::stopRunControl" << rc;
}

void AppOutputPane::closeTabs(CloseTabMode mode)
{
    for (int t = m_tabWidget->count() - 1; t >= 0; t--)
        closeTab(t, mode);
}

QList<RunControl *> AppOutputPane::allRunControls() const
{
    const QList<RunControl *> list = Utils::transform<QList>(m_runControlTabs,[](const RunControlTab &tab) {
        return tab.runControl.data();
    });
    return Utils::filtered(list, [](RunControl *rc) { return rc; });
}

void AppOutputPane::closeTab(int tabIndex, CloseTabMode closeTabMode)
{
    QWidget * const tabWidget = m_tabWidget->widget(tabIndex);
    RunControlTab *tab = tabFor(tabWidget);
    QTC_ASSERT(tab, return);

    RunControl *runControl = tab->runControl;
    Core::OutputWindow *window = tab->window;
    qCDebug(appOutputLog) << "AppOutputPane::closeTab tab" << tabIndex << runControl << window;
    // Prompt user to stop
    if (closeTabMode == CloseTabWithPrompt) {
        if (runControl && runControl->isRunning() && !runControl->promptToStop())
            return;
        // The event loop has run, thus the ordering might have changed, a tab might
        // have been closed, so do some strange things...
        tabIndex = m_tabWidget->indexOf(tabWidget);
        tab = tabFor(tabWidget);
        if (tabIndex == -1 || !tab)
            return;
    }

    m_tabWidget->removeWidget(tabWidget);
    if (m_tabWidget->count() > 0) {
        m_tabComboBox->removeItem(tabIndex);
    } else {
        m_tabComboBox->setItemText(tabIndex, Tr::tr("<no configuration>"));
        m_tabComboBox->setCurrentIndex(0);
    }
    delete window;

    Utils::erase(m_runControlTabs, [runControl](const RunControlTab &t) {
        return t.runControl == runControl; });
    if (runControl) {
        if (runControl->isRunning()) {
            connect(runControl, &RunControl::stopped, runControl, &QObject::deleteLater);
            runControl->initiateStop();
        } else {
            delete runControl;
        }
    }
    updateCloseActions();
    setFilteringEnabled(m_tabWidget->count() > 0);

    if (m_runControlTabs.isEmpty())
        hide();
}

bool AppOutputPane::optionallyPromptToStop(RunControl *runControl)
{
    bool promptToStop = ProjectExplorerSettings::get(runControl).promptToStopRunControl();
    if (!runControl->promptToStop(&promptToStop))
        return false;
    setPromptToStopSettings(promptToStop);
    return true;
}

void AppOutputPane::projectRemoved()
{
    tabChanged(m_tabWidget->currentIndex());
}

void AppOutputPane::enableDefaultButtons()
{
    enableButtons(currentRunControl());
}

void AppOutputPane::zoomIn(int range)
{
    for (const RunControlTab &tab : std::as_const(m_runControlTabs))
        tab.window->zoomIn(range);
}

void AppOutputPane::zoomOut(int range)
{
    for (const RunControlTab &tab : std::as_const(m_runControlTabs))
        tab.window->zoomOut(range);
}

void AppOutputPane::resetZoom()
{
    for (const RunControlTab &tab : std::as_const(m_runControlTabs))
        tab.window->resetZoom();
}

void AppOutputPane::enableButtons(const RunControl *rc)
{
    if (rc) {
        const bool isRunning = rc->isRunning();
        m_reRunButton->setEnabled(rc->isStopped());
        m_reRunButton->setIcon(rc->icon().icon());
        m_stopAction->setEnabled(isRunning);
        if (isRunning && debuggerPlugin() && rc->applicationProcessHandle().isValid()) {
            m_attachButton->setEnabled(true);
            const QString tip = Tr::tr("PID %1").arg(rc->applicationProcessHandle().pid());
            m_attachButton->setToolTip(msgAttachDebuggerTooltip(tip));
        } else {
            m_attachButton->setEnabled(false);
            m_attachButton->setToolTip(msgAttachDebuggerTooltip());
        }
        setZoomButtonsEnabled(true);
    } else {
        m_reRunButton->setEnabled(false);
        m_reRunButton->setIcon(Utils::Icons::RUN_SMALL_TOOLBAR.icon());
        m_attachButton->setEnabled(false);
        m_attachButton->setToolTip(msgAttachDebuggerTooltip());
        m_stopAction->setEnabled(false);
        setZoomButtonsEnabled(false);
    }
    m_formatterWidget->setVisible(m_formatterWidget->layout()->count());
}

void AppOutputPane::tabChanged(int i)
{
    RunControlTab * const controlTab = tabFor(m_tabWidget->widget(i));
    if (i != -1 && controlTab) {
        auto appwindow = qobject_cast<AppOutputWindow*>(controlTab->window);
        appwindow->updateCategoriesProperties(appwindow->registry()->categories());
        if (!controlTab->window->updateFilterProperties(filterText(), filterCaseSensitivity(),
                                                    filterUsesRegexp(), filterIsInverted(),
                                                    beforeContext(), afterContext()))
            controlTab->window->filterNewContent();
        enableButtons(controlTab->runControl);
    } else {
        enableDefaultButtons();
    }
}

void AppOutputPane::runControlFinished(RunControl *runControl)
{
    const RunControlTab * const tab = tabFor(runControl);

    // This slot is queued, so the stop() call in closeTab might lead to this slot, after closeTab already cleaned up
    if (!tab)
        return;

    // Enable buttons for current
    RunControl *current = currentRunControl();

    qCDebug(appOutputLog) << "AppOutputPane::runControlFinished" << runControl
                          << m_tabWidget->indexOf(tab->window)
                          << "current" << current << m_runControlTabs.size();

    if (current && current == runControl)
        enableButtons(current);

    ProjectExplorerPlugin::updateRunActions();

    const bool isRunning = Utils::anyOf(m_runControlTabs, [](const RunControlTab &rt) {
        return rt.runControl && rt.runControl->isRunning();
    });

    if (!isRunning)
        WinDebugInterface::stop();
}

bool AppOutputPane::canNext() const
{
    return false;
}

bool AppOutputPane::canPrevious() const
{
    return false;
}

void AppOutputPane::goToNext()
{

}

void AppOutputPane::goToPrev()
{

}

bool AppOutputPane::canNavigate() const
{
    return false;
}

bool AppOutputPane::hasFilterContext() const
{
    return true;
}

class AppOutputSettingsWidget : public Core::IOptionsPageWidget
{
public:
    AppOutputSettingsWidget()
    {
        const AppOutputSettings &settings = appOutputPane().settings();
        m_wrapOutputCheckBox.setText(Tr::tr("Word-wrap output"));
        m_wrapOutputCheckBox.setChecked(settings.wrapOutput);
        m_discardOutputCheckBox.setText(Tr::tr("Discard excessive output"));
        m_discardOutputCheckBox.setToolTip(
            Tr::tr(
                "If this option is enabled, application output will be discarded if it "
                "continuously comes in faster than it can be handled."));
        m_discardOutputCheckBox.setChecked(settings.discardExcessiveOutput);
        m_cleanOldOutputCheckBox.setText(Tr::tr("Clear old output on a new run"));
        m_cleanOldOutputCheckBox.setChecked(settings.cleanOldOutput);
        m_mergeChannelsCheckBox.setText(Tr::tr("Merge stderr and stdout"));
        m_mergeChannelsCheckBox.setChecked(settings.mergeChannels);
        for (QComboBox * const modeComboBox
             : {&m_runOutputModeComboBox, &m_debugOutputModeComboBox}) {
            modeComboBox->addItem(Tr::tr("Always"), int(AppOutputPaneMode::PopupOnOutput));
            modeComboBox->addItem(Tr::tr("Never"), int(AppOutputPaneMode::FlashOnOutput));
            modeComboBox->addItem(Tr::tr("On First Output Only"),
                                  int(AppOutputPaneMode::PopupOnFirstOutput));
        }
        m_runOutputModeComboBox.setCurrentIndex(m_runOutputModeComboBox
                                                .findData(int(settings.runOutputMode)));
        m_debugOutputModeComboBox.setCurrentIndex(m_debugOutputModeComboBox
                                                  .findData(int(settings.debugOutputMode)));
        m_maxCharsBox.setMaximum(100000000);
        m_maxCharsBox.setValue(settings.maxCharCount);
        m_overwriteColor.setText(Tr::tr("Overwrite background color"));
        m_overwriteColor.setChecked(settings.overwriteBackground);
        m_overwriteColor.setToolTip(Tr::tr("Customize background color of the application output.\n"
                                           "Note: existing output will not get recolored."));
        m_backgroundColor.setMinimumSize(QSize(64, 0));
        m_backgroundColor.setAlphaAllowed(false);
        QColor bgColor = settings.backgroundColor;
        if (bgColor == AppOutputSettings::defaultBackgroundColor())
            bgColor = QColor();
        m_backgroundColor.setColor(bgColor);
        m_backgroundColor.setEnabled(m_overwriteColor.isChecked());
        auto resetColorButton = new QPushButton(Tr::tr("Reset"));
        resetColorButton->setToolTip(Tr::tr("Reset to default.", "Color"));
        resetColorButton->setEnabled(m_overwriteColor.isChecked());
        connect(resetColorButton, &QPushButton::clicked, this, [this] {
            m_backgroundColor.setColor({});
        });
        connect(&m_overwriteColor, &QCheckBox::clicked,
                this, [this, resetColorButton](bool checked) {
                m_backgroundColor.setEnabled(checked);
                resetColorButton->setEnabled(checked);
        });

        const auto layout = new QVBoxLayout(this);
        layout->addWidget(&m_wrapOutputCheckBox);
        layout->addWidget(&m_cleanOldOutputCheckBox);
        layout->addWidget(&m_discardOutputCheckBox);
        layout->addWidget(&m_mergeChannelsCheckBox);
        const auto maxCharsLayout = new QHBoxLayout;
        const QString msg = Tr::tr("Limit output to %1 characters");
        const QStringList parts = msg.split("%1") << QString() << QString();
        maxCharsLayout->addWidget(new QLabel(parts.at(0).trimmed()));
        maxCharsLayout->addWidget(&m_maxCharsBox);
        maxCharsLayout->addWidget(new QLabel(parts.at(1).trimmed()));
        maxCharsLayout->addStretch(1);
        const auto outputModeLayout = new QFormLayout;
        outputModeLayout->addRow(Tr::tr("Open Application Output when running:"), &m_runOutputModeComboBox);
        outputModeLayout->addRow(Tr::tr("Open Application Output when debugging:"),
                                 &m_debugOutputModeComboBox);
        const auto bgColorLayout = new QHBoxLayout;
        bgColorLayout->addWidget(&m_overwriteColor);
        bgColorLayout->addWidget(&m_backgroundColor);
        bgColorLayout->addWidget(resetColorButton);
        bgColorLayout->addStretch(1);
        layout->addLayout(outputModeLayout);
        layout->addLayout(maxCharsLayout);
        layout->addLayout(bgColorLayout);
        layout->addStretch(1);
    }

    void apply() final
    {
        AppOutputSettings s;
        s.wrapOutput = m_wrapOutputCheckBox.isChecked();
        s.discardExcessiveOutput = m_discardOutputCheckBox.isChecked();
        s.cleanOldOutput = m_cleanOldOutputCheckBox.isChecked();
        s.mergeChannels = m_mergeChannelsCheckBox.isChecked();
        s.runOutputMode = static_cast<AppOutputPaneMode>(
                    m_runOutputModeComboBox.currentData().toInt());
        s.debugOutputMode = static_cast<AppOutputPaneMode>(
                    m_debugOutputModeComboBox.currentData().toInt());
        s.maxCharCount = m_maxCharsBox.value();
        s.overwriteBackground = m_overwriteColor.isChecked();
        QColor bgColor = m_backgroundColor.color();
        if (!bgColor.isValid())
            bgColor = AppOutputSettings::defaultBackgroundColor();
        s.backgroundColor = bgColor;

        appOutputPane().setSettings(s);
    }

private:
    QCheckBox m_wrapOutputCheckBox;
    QCheckBox m_discardOutputCheckBox;
    QCheckBox m_cleanOldOutputCheckBox;
    QCheckBox m_mergeChannelsCheckBox;
    QCheckBox m_overwriteColor;
    QComboBox m_runOutputModeComboBox;
    QComboBox m_debugOutputModeComboBox;
    QSpinBox m_maxCharsBox;
    QtColorButton m_backgroundColor;
};

AppOutputSettingsPage::AppOutputSettingsPage()
{
    setId(OPTIONS_PAGE_ID);
    setDisplayName(Tr::tr("Application Output"));
    setCategory(Constants::BUILD_AND_RUN_SETTINGS_CATEGORY);
    setWidgetCreator([] { return new AppOutputSettingsWidget; });
}

static QPointer<AppOutputPane> theAppOutputPane;

AppOutputPane &appOutputPane()
{
    QTC_CHECK(!theAppOutputPane.isNull());
    return *theAppOutputPane;
}

void setupAppOutputPane()
{
    QTC_CHECK(theAppOutputPane.isNull());
    theAppOutputPane = new AppOutputPane;
}

void destroyAppOutputPane()
{
    QTC_CHECK(!theAppOutputPane.isNull());
    delete theAppOutputPane;
}

QColor AppOutputSettings::defaultBackgroundColor()
{
    return Utils::creatorColor(Theme::PaletteBase);
}

QColor AppOutputSettings::effectiveBackgroundColor() const
{
    return overwriteBackground ? backgroundColor : defaultBackgroundColor();
}

} // namespace Internal
} // namespace ProjectExplorer

#include "appoutputpane.moc"
