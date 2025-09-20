// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "coreplugin.h"
#include "coreplugintr.h"
#include "designmode.h"
#include "dialogs/ioptionspage.h"
#include "editmode.h"
#include "foldernavigationwidget.h"
#include "icore.h"
#include "idocument.h"
#include "iwizardfactory.h"
#include "loggingviewer.h"
#include "modemanager.h"
#include "session.h"
#include "settingsdatabase.h"
#include "systemsettings.h"
#include "themechooser.h"
#include "vcsmanager.h"

#include "actionmanager/actionmanager.h"
#include "coreconstants.h"
#include "documentmanager.h"
#include "fileutils.h"
#include "find/findplugin.h"
#include "locator/locator.h"

#include <extensionsystem/pluginerroroverview.h>
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <utils/algorithm.h>
#include <utils/checkablemessagebox.h>
#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/infobar.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/mimeutils.h>
#include <utils/networkaccessmanager.h>
#include <utils/passworddialog.h>
#include <utils/pathchooser.h>
#include <utils/savefile.h>
#include <utils/store.h>
#include <utils/stringutils.h>
#include <utils/textutils.h>
#include <utils/theme/theme.h>
#include <utils/theme/theme_p.h>

#include <QAuthenticator>
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QGuiApplication>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>

#include <cstdlib>

using namespace Utils;

namespace Core::Internal {

static CorePlugin *m_instance = nullptr;

const char kWarnCrashReportingSetting[] = "WarnCrashReporting";

CorePlugin::CorePlugin()
{
    QObject::connect(qApp, SIGNAL(fileOpenRequest(QString)), this, SLOT(fileOpenRequest(QString)));

    // Trigger creation as early as possible before anyone else could
    // mess with the systemEnvironment before it is "backed up".
    (void) systemSettings();

    qRegisterMetaType<Id>();
    qRegisterMetaType<Utils::Text::Position>();
    qRegisterMetaType<Utils::CommandLine>();
    qRegisterMetaType<Utils::FilePath>();
    qRegisterMetaType<Utils::Environment>();
    qRegisterMetaType<Utils::Store>();
    qRegisterMetaType<Utils::Key>();
    qRegisterMetaType<Utils::KeyList>();
    qRegisterMetaType<Utils::OldStore>();
    m_instance = this;
}

CorePlugin::~CorePlugin()
{
    IWizardFactory::destroyFeatureProvider();
    Find::destroy();

    delete m_locator;
    delete m_folderNavigationWidgetFactory;
    delete m_editMode;

    DesignMode::destroyModeIfRequired();

    delete m_core;
    SettingsDatabase::destroy();
    setCreatorTheme(nullptr);
}

CorePlugin *CorePlugin::instance()
{
    return m_instance;
}

struct CoreArguments {
    QColor overrideColor;
    Id themeId;
    bool presentationMode = false;
};

CoreArguments parseArguments(const QStringList &arguments)
{
    CoreArguments args;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("-color")) {
            const QString colorcode(arguments.at(i + 1));
            args.overrideColor = QColor(colorcode);
            i++; // skip the argument
        }
        if (arguments.at(i) == QLatin1String("-presentationMode"))
            args.presentationMode = true;
        if (arguments.at(i) == QLatin1String("-theme")) {
            args.themeId = Id::fromString(arguments.at(i + 1));
            i++; // skip the argument
        }
    }
    return args;
}

static void initProxyAuthDialog()
{
    QObject::connect(Utils::NetworkAccessManager::instance(),
                     &QNetworkAccessManager::proxyAuthenticationRequired,
                     Utils::NetworkAccessManager::instance(),
                     [](const QNetworkProxy &, QAuthenticator *authenticator) {
                         static bool doNotAskAgain = false;

                         std::optional<QPair<QString, QString>> answer
                             = Utils::PasswordDialog::getUserAndPassword(
                                 Tr::tr("Proxy Authentication Required"),
                                 authenticator->realm(),
                                 Tr::tr("Do not ask again."),
                                 {},
                                 &doNotAskAgain,
                                 Core::ICore::dialogParent());

                         if (answer) {
                             authenticator->setUser(answer->first);
                             authenticator->setPassword(answer->second);
                         }
                     });
}

static void initTAndCAcceptDialog()
{
    ExtensionSystem::PluginManager::instance()->setAcceptTermsAndConditionsCallback(
        [](ExtensionSystem::PluginSpec *spec) {
            using namespace Layouting;

            QDialog dialog(ICore::dialogParent());
            dialog.setWindowTitle(Tr::tr("Terms and Conditions"));

            QDialogButtonBox buttonBox;
            QCheckBox *acceptCheckBox;
            QPushButton *acceptButton
                = buttonBox.addButton(Tr::tr("Accept"), QDialogButtonBox::ButtonRole::YesRole);
            QPushButton *decline
                = buttonBox.addButton(Tr::tr("Decline"), QDialogButtonBox::ButtonRole::NoRole);
            acceptButton->setAutoDefault(false);
            acceptButton->setDefault(false);
            acceptButton->setEnabled(false);
            decline->setAutoDefault(true);
            decline->setDefault(true);
            QObject::connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            QObject::connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

            const QLatin1String legal = QLatin1String(
                "I confirm that I have reviewed and accept the terms and conditions\n"
                "of this extension. I confirm that I have the authority and ability to\n"
                "accept the terms and conditions of this extension for the customer.\n"
                "I acknowledge that if the customer and the Qt Company already have a\n"
                "valid agreement in place, that agreement shall apply, but these terms\n"
                "shall govern the use of this extension.");

            // clang-format off
            Column {
                Tr::tr("The plugin %1 requires you to accept the following terms and conditions:").arg(spec->name()), br,
                TextEdit {
                    markdown(spec->termsAndConditions()->text),
                    readOnly(true),
                }, br,
                Row {
                    acceptCheckBox = new QCheckBox(legal), &buttonBox,
                }
            }.attachTo(&dialog);
            // clang-format on

            QObject::connect(
                acceptCheckBox, &QCheckBox::toggled, acceptButton, &QPushButton::setEnabled);

            return dialog.exec() == QDialog::Accepted;
        });
}

static void addToPathChooserContextMenu(PathChooser *pathChooser, QMenu *menu)
{
    QList<QAction *> actions = menu->actions();
    QAction *firstAction = actions.isEmpty() ? nullptr : actions.first();

    if (pathChooser->filePath().exists()) {
        auto showInGraphicalShell = new QAction(FileUtils::msgGraphicalShellAction(), menu);
        QObject::connect(showInGraphicalShell, &QAction::triggered, pathChooser, [pathChooser] {
            Core::FileUtils::showInGraphicalShell(pathChooser->filePath());
        });
        menu->insertAction(firstAction, showInGraphicalShell);

        auto showInTerminal = new QAction(FileUtils::msgTerminalHereAction(), menu);
        QObject::connect(showInTerminal, &QAction::triggered, pathChooser, [pathChooser] {
            if (pathChooser->openTerminalHandler())
                pathChooser->openTerminalHandler()();
            else
                FileUtils::openTerminal(pathChooser->filePath(), {});
        });
        menu->insertAction(firstAction, showInTerminal);

    } else {
        auto mkPathAct = new QAction(Tr::tr("Create Folder"), menu);
        QObject::connect(mkPathAct, &QAction::triggered, pathChooser, [pathChooser] {
            pathChooser->filePath().ensureWritableDir();
            pathChooser->triggerChanged();
        });
        menu->insertAction(firstAction, mkPathAct);
    }

    if (firstAction)
        menu->insertSeparator(firstAction);
}

Result<> CorePlugin::initialize(const QStringList &arguments)
{
    initTAndCAcceptDialog();
    initProxyAuthDialog();

    if (ThemeEntry::availableThemes().isEmpty())
        return ResultError(Tr::tr("No themes found in installation."));

    const CoreArguments args = parseArguments(arguments);
    Theme *themeFromArg = ThemeEntry::createTheme(args.themeId);
    Theme *theme = themeFromArg ? themeFromArg
                                : ThemeEntry::createTheme(ThemeEntry::themeSetting());
    Theme::setInitialPalette(theme); // Initialize palette before setting it
    setCreatorTheme(theme);
    InfoBar::initialize(ICore::settings());
    CheckableMessageBox::initialize(ICore::settings());
    new ActionManager(this);
    ActionManager::setPresentationModeEnabled(args.presentationMode);
    if (args.overrideColor.isValid())
        ICore::setOverrideColor(args.overrideColor);
    m_core = new ICore;
    m_locator = new Locator;
    std::srand(unsigned(QDateTime::currentDateTime().toSecsSinceEpoch()));
    m_editMode = new EditMode;
    ModeManager::activateMode(m_editMode->id());
    m_folderNavigationWidgetFactory = new FolderNavigationWidgetFactory;

    IOptionsPage::registerCategory(
        Constants::SETTINGS_CATEGORY_CORE,
        Tr::tr("Environment"),
        ":/core/images/settingscategory_core.png");

    // Shared by Help and ScreenRecorder
    IOptionsPage::registerCategory(
        Constants::HELP_CATEGORY, Tr::tr("Help"), ":/core/images/settingscategory_help.png");

    IWizardFactory::initialize();

    // Make sure we respect the process's umask when creating new files
    SaveFile::initializeUmask();

    Find::initialize();
    m_locator->initialize();

    MacroExpander *expander = Utils::globalMacroExpander();
    expander->registerVariable("CurrentDate:ISO", Tr::tr("The current date (ISO)."),
                               [] { return QDate::currentDate().toString(Qt::ISODate); });
    expander->registerVariable("CurrentTime:ISO", Tr::tr("The current time (ISO)."),
                               [] { return QTime::currentTime().toString(Qt::ISODate); });
    expander->registerVariable("CurrentDate:RFC", Tr::tr("The current date (RFC2822)."),
                               [] { return QDate::currentDate().toString(Qt::RFC2822Date); });
    expander->registerVariable("CurrentTime:RFC", Tr::tr("The current time (RFC2822)."),
                               [] { return QTime::currentTime().toString(Qt::RFC2822Date); });
    expander->registerVariable("CurrentDate:Locale", Tr::tr("The current date (Locale)."),
                               [] { return QLocale::system()
                                        .toString(QDate::currentDate(), QLocale::ShortFormat); });
    expander->registerVariable("CurrentTime:Locale", Tr::tr("The current time (Locale)."),
                               [] { return QLocale::system()
                                        .toString(QTime::currentTime(), QLocale::ShortFormat); });
    expander->registerVariable("Config:DefaultProjectDirectory", Tr::tr("The configured default directory for projects."),
                               [] { return DocumentManager::projectsDirectory().toUrlishString(); });
    expander->registerVariable("Config:LastFileDialogDirectory", Tr::tr("The directory last visited in a file dialog."),
                               [] { return DocumentManager::fileDialogLastVisitedDirectory().toUrlishString(); });
    expander->registerVariable("HostOs:isWindows",
                               Tr::tr("Is %1 running on Windows?")
                                   .arg(QGuiApplication::applicationDisplayName()),
                               [] {
                                   return QVariant(Utils::HostOsInfo::isWindowsHost()).toString();
                               });
    expander->registerVariable("HostOs:isOSX",
                               Tr::tr("Is %1 running on OS X?")
                                   .arg(QGuiApplication::applicationDisplayName()),
                               [] { return QVariant(Utils::HostOsInfo::isMacHost()).toString(); });
    expander->registerVariable("HostOs:isLinux",
                               Tr::tr("Is %1 running on Linux?")
                                   .arg(QGuiApplication::applicationDisplayName()),
                               [] { return QVariant(Utils::HostOsInfo::isLinuxHost()).toString(); });
    expander->registerVariable("HostOs:isUnix",
                               Tr::tr("Is %1 running on any unix-based platform?")
                                   .arg(QGuiApplication::applicationDisplayName()),
                               [] {
                                   return QVariant(Utils::HostOsInfo::isAnyUnixHost()).toString();
                               });
    expander->registerVariable("HostOs:PathListSeparator",
                               Tr::tr("The path list separator for the platform."),
                               [] { return QString(Utils::HostOsInfo::pathListSeparator()); });
    expander->registerVariable("HostOs:ExecutableSuffix",
                               Tr::tr("The platform executable suffix."),
                               [] { return QString(Utils::HostOsInfo::withExecutableSuffix("")); });
    expander->registerFileVariables("IDE:Executable",
                               Tr::tr("The path to the running %1 itself.").arg(QGuiApplication::applicationDisplayName()),
                               []() { return FilePath::fromUserInput(QCoreApplication::applicationFilePath()); });
    expander->registerVariable("IDE:ResourcePath",
                               Tr::tr("The directory where %1 finds its pre-installed resources.")
                                   .arg(QGuiApplication::applicationDisplayName()),
                               [] { return ICore::resourcePath().toUrlishString(); });
    expander->registerVariable("IDE:UserResourcePath",
                               Tr::tr("The directory where %1 puts custom user data.")
                                   .arg(QGuiApplication::applicationDisplayName()),
                               [] { return ICore::userResourcePath().toUrlishString(); });
    expander->registerPrefix("CurrentDate:", Tr::tr("The current date (QDate formatstring)."),
                             [](const QString &fmt) { return QDate::currentDate().toString(fmt); });
    expander->registerPrefix("CurrentTime:", Tr::tr("The current time (QTime formatstring)."),
                             [](const QString &fmt) { return QTime::currentTime().toString(fmt); });
    expander->registerVariable("UUID", Tr::tr("Generate a new UUID."),
                               [] { return QUuid::createUuid().toString(); });

    expander->registerPrefix("#:", Tr::tr("A comment."), [](const QString &) { return QString(); });
    expander->registerPrefix("Asciify:",
                             Tr::tr("Convert string to pure ASCII."),
                             [expander](const QString &s) { return asciify(expander->expand(s)); });

    Utils::PathChooser::setAboutToShowContextMenuHandler(&addToPathChooserContextMenu);

#ifdef ENABLE_CRASHREPORTING
    connect(
        ICore::instance(),
        &ICore::coreOpened,
        this,
        &CorePlugin::warnAboutCrashReporing,
        Qt::QueuedConnection);
#endif

#ifdef WITH_TESTS
    addTestCreator(&createVcsManagerTest);
#endif

    return ResultOk;
}

static Id generateOpenPageCommandId(IOptionsPage *page)
{
    // The page and category are prioritized by their alphabetical order so usually the ids are
    // prepended by some prioritizing characters like D.ProjectExplorer.KitsOptions separated
    // by dots. Create a new actions id by joining the last parts of the page and category id
    // with an additional ".SettingsPage."
    const QStringList pageIdParts = page->id().toString().split('.');
    const QStringList categoryIdParts = page->category().toString().split('.');
    if (pageIdParts.isEmpty() || categoryIdParts.isEmpty())
        return {};

    const Id candidate = Id::fromString(
        QStringList{"Preferences", categoryIdParts.last(), pageIdParts.last()}.join('.'));
    QString suffix;
    int i = 0;
    while (ActionManager::command(candidate.withSuffix(suffix)))
        suffix = QString::number(++i);
    return candidate.withSuffix(suffix);
}

static void registerActionsForOptions()
{
    QMap<Utils::Id, QString> categoryDisplay;
    for (IOptionsPage *page : IOptionsPage::allOptionsPages()) {
        if (!categoryDisplay.contains(page->category()) && !page->displayCategory().isEmpty())
            categoryDisplay[page->category()] = page->displayCategory();
    }
    for (IOptionsPage *page : IOptionsPage::allOptionsPages()) {
        const Id commandId = generateOpenPageCommandId(page);
        if (!commandId.isValid())
            continue;

        ActionBuilder(m_instance, commandId)
            .setText(Tr::tr("%1 > %2 Preferences...")
                         .arg(categoryDisplay.value(page->category()), page->displayName()))
            .addOnTriggered(m_instance, [id = page->id()] {
                ICore::showOptionsDialog(id);
            });
    }
}

void CorePlugin::extensionsInitialized()
{
    DesignMode::createModeIfRequired();
    Find::extensionsInitialized();
    m_locator->extensionsInitialized();
    ICore::extensionsInitialized();
    if (ExtensionSystem::PluginManager::hasError())
        ExtensionSystem::showPluginErrorOverview();
    checkSettings();
    registerActionsForOptions();
}

bool CorePlugin::delayedInitialize()
{
    m_locator->delayedInitialize();
    IWizardFactory::allWizardFactories(); // scan for all wizard factories
    return true;
}

QObject *CorePlugin::remoteCommand(const QStringList & /* options */,
                                   const QString &workingDirectory,
                                   const QStringList &args)
{
    if (!ExtensionSystem::PluginManager::isInitializationDone()) {
        connect(ExtensionSystem::PluginManager::instance(),
                &ExtensionSystem::PluginManager::initializationDone, this,
                [this, workingDirectory, args] { remoteCommand({}, workingDirectory, args); });
        return nullptr;
    }
    const FilePaths filePaths = Utils::transform(args, FilePath::fromUserInput);
    IDocument *res = ICore::openFiles(
                filePaths,
                ICore::OpenFilesFlags(ICore::SwitchMode | ICore::CanContainLineAndColumnNumbers | ICore::SwitchSplitIfAlreadyVisible),
                FilePath::fromString(workingDirectory));
    ICore::raiseMainWindow();
    return res;
}

void CorePlugin::fileOpenRequest(const QString &f)
{
    if (ExtensionSystem::PluginManager::isShuttingDown())
        return;
    remoteCommand(QStringList(), QString(), QStringList(f));
}

void CorePlugin::checkSettings()
{
    const auto showMsgBox = [this](const QString &msg, QMessageBox::Icon icon) {
        connect(ICore::instance(), &ICore::coreOpened, this, [msg, icon] {
            QMessageBox msgBox(ICore::dialogParent());
            msgBox.setWindowTitle(Tr::tr("Settings File Error"));
            msgBox.setText(msg);
            msgBox.setIcon(icon);
            msgBox.exec();
        }, Qt::QueuedConnection);
    };
    const QtcSettings * const userSettings = ICore::settings();
    QString errorDetails;
    switch (userSettings->status()) {
    case QSettings::NoError: {
        const QFileInfo fi(userSettings->fileName());
        if (fi.exists() && !fi.isWritable()) {
            const QString errorMsg = Tr::tr("The settings file \"%1\" is not writable.\n"
                                            "You will not be able to store any %2 settings.")
                                         .arg(QDir::toNativeSeparators(userSettings->fileName()),
                                              QGuiApplication::applicationDisplayName());
            showMsgBox(errorMsg, QMessageBox::Warning);
        }
        return;
    }
    case QSettings::AccessError:
        errorDetails = Tr::tr("The file is not readable.");
        break;
    case QSettings::FormatError:
        errorDetails = Tr::tr("The file is invalid.");
        break;
    }
    const QString errorMsg
        = Tr::tr("Error reading settings file \"%1\": %2\n"
                 "You will likely experience further problems using this instance of %3.")
              .arg(QDir::toNativeSeparators(userSettings->fileName()),
                   errorDetails,
                   QGuiApplication::applicationDisplayName());
    showMsgBox(errorMsg, QMessageBox::Critical);
}

void CorePlugin::warnAboutCrashReporing()
{
    if (!ICore::infoBar()->canInfoBeAdded(kWarnCrashReportingSetting))
        return;

    QString warnStr = ICore::settings()->value("CrashReportingEnabled", false).toBool()
            ? Tr::tr("%1 collects crash reports for the sole purpose of fixing bugs. "
                 "To disable this feature go to %2.")
            : Tr::tr("%1 can collect crash reports for the sole purpose of fixing bugs. "
                 "To enable this feature go to %2.");

    if (Utils::HostOsInfo::isMacHost()) {
        warnStr = warnStr.arg(QGuiApplication::applicationDisplayName(),
                              QGuiApplication::applicationDisplayName()
                                  + Tr::tr(" > Preferences > Environment > System"));
    } else {
        warnStr = warnStr.arg(QGuiApplication::applicationDisplayName(),
                              Tr::tr("Edit > Preferences > Environment > System"));
    }

    Utils::InfoBarEntry info(kWarnCrashReportingSetting, warnStr,
                             Utils::InfoBarEntry::GlobalSuppression::Enabled);
    info.setTitle(Tr::tr("Crash Reporting"));
    info.addCustomButton(
        ICore::msgShowOptionsDialog(),
        [] { ICore::showOptionsDialog(Core::Constants::SETTINGS_ID_SYSTEM); },
        {},
        InfoBarEntry::ButtonAction::SuppressPersistently);

    info.setDetailsWidgetCreator([]() -> QWidget * {
        auto label = new QLabel;
        label->setWindowTitle(Tr::tr("Crash Reporting"));
        label->setWordWrap(true);
        label->setOpenExternalLinks(true);
        label->setText(msgCrashpadInformation());
        label->setContentsMargins(0, 0, 0, 8);
        return label;
    });
    ICore::infoBar()->addInfo(info);
}

// static
QString CorePlugin::msgCrashpadInformation()
{
#if ENABLE_CRASHREPORTING
#if CRASHREPORTING_USES_CRASHPAD
    const QString backend = "Google Crashpad";
    const QString url
        = "https://chromium.googlesource.com/crashpad/crashpad/+/master/doc/overview_design.md";
#else
    const QString backend = "Google Breakpad";
    const QString url
        = "https://chromium.googlesource.com/breakpad/breakpad/+/HEAD/docs/client_design.md";
#endif
    //: %1 = application name, %2 crash backend name (Google Crashpad or Google Breakpad)
    return Tr::tr("%1 uses %2 for collecting crashes and sending them to Sentry "
                  "for processing. %2 may capture arbitrary contents from crashed process’ "
                  "memory, including user sensitive information, URLs, and whatever other content "
                  "users have trusted %1 with. The collected crash reports are however only used "
                  "for the sole purpose of fixing bugs.")
               .arg(QGuiApplication::applicationDisplayName(), backend)
           + "<br><br>" + Tr::tr("More information:") + "<br><a href='" + url
           + "'>"
           //: %1 = crash backend name (Google Crashpad or Google Breakpad)
           + Tr::tr("%1 Overview").arg(backend)
           + "</a>"
             "<br><a href='https://sentry.io/security/'>"
           + Tr::tr("%1 security policy").arg("Sentry.io") + "</a>";
#else
    return {};
#endif
}

ExtensionSystem::IPlugin::ShutdownFlag CorePlugin::aboutToShutdown()
{
    LoggingViewer::hideLoggingView();
    Find::aboutToShutdown();
    m_locator->aboutToShutdown();
    ICore::aboutToShutdown();
    return SynchronousShutdown;
}

} // Core::Internal
