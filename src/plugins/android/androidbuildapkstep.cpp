// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidbuildapkstep.h"
#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidqtversion.h"
#include "androidsdkmanager.h"
#include "androidtr.h"
#include "androidutils.h"
#include "keystorecertificatedialog.h"
#include "manifestwizard.h"
#include "javaparser.h"

#include <coreplugin/fileutils.h>
#include <coreplugin/icore.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/buildstep.h>
#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/buildsystem.h>
#include <projectexplorer/processparameters.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/target.h>
#include <projectexplorer/taskhub.h>

#include <qtsupport/qtkitaspect.h>

#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/fileutils.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcprocess.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

using namespace ProjectExplorer;
using namespace QtSupport;
using namespace Utils;

using namespace std::chrono_literals;

namespace Android::Internal {

static Q_LOGGING_CATEGORY(buildapkstepLog, "qtc.android.build.androidbuildapkstep", QtWarningMsg)

const QLatin1String AliasString("Alias name:");
const QLatin1String CertificateSeparator("*******************************************");

class CertificatesModel : public QAbstractListModel
{
public:
    CertificatesModel(const QString &rowCertificates, QObject *parent)
        : QAbstractListModel(parent)
    {
        int from = rowCertificates.indexOf(AliasString);
        QPair<QString, QString> item;
        while (from > -1) {
            from += 11;// strlen(AliasString);
            const int eol = rowCertificates.indexOf(QLatin1Char('\n'), from);
            item.first = rowCertificates.mid(from, eol - from).trimmed();
            const int eoc = rowCertificates.indexOf(CertificateSeparator, eol);
            item.second = rowCertificates.mid(eol + 1, eoc - eol - 2).trimmed();
            from = rowCertificates.indexOf(AliasString, eoc);
            m_certs.push_back(item);
        }
    }

protected:
    int rowCount(const QModelIndex &parent = {}) const override
    {
        if (parent.isValid())
            return 0;
        return m_certs.size();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
            return {};
        if (role == Qt::DisplayRole)
            return m_certs[index.row()].first;
        return m_certs[index.row()].second;
    }

private:
    QList<QPair<QString, QString>> m_certs;
};

class LibraryListModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    LibraryListModel(ProjectExplorer::BuildSystem *buildSystem, QObject *parent);

    QModelIndex index(int row, int column, const QModelIndex &) const override
    { return createIndex(row, column); }
    QModelIndex parent(const QModelIndex &) const override { return {};  }
    int rowCount(const QModelIndex &) const override { return m_entries.size(); }
    int columnCount(const QModelIndex &) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override;

    void removeEntries(QModelIndexList list);
    void addEntries(const QStringList &list);

signals:
    void enabledChanged(bool);

private:
    void updateModel();

    ProjectExplorer::BuildSystem *m_buildSystem;
    QStringList m_entries;
};

LibraryListModel::LibraryListModel(BuildSystem *buildSystem, QObject *parent)
    : QAbstractItemModel(parent)
    , m_buildSystem(buildSystem)
{
    updateModel();

    connect(buildSystem, &BuildSystem::parsingStarted, this, &LibraryListModel::updateModel);
    connect(buildSystem, &BuildSystem::parsingFinished, this, &LibraryListModel::updateModel);
    // Causes target()->activeBuildKey() result and consequently the node data
    // extracted below to change.
    connect(buildSystem->buildConfiguration(), &BuildConfiguration::activeRunConfigurationChanged,
            this, &LibraryListModel::updateModel);
}

QVariant LibraryListModel::data(const QModelIndex &index, int role) const
{
    QTC_ASSERT(index.row() >= 0 && index.row() < m_entries.size(), return {});
    if (role == Qt::DisplayRole)
        return QDir::cleanPath(m_entries.at(index.row()));
    return {};
}

void LibraryListModel::addEntries(const QStringList &list)
{
    const QString buildKey = m_buildSystem->buildConfiguration()->activeBuildKey();
    const ProjectNode *node = m_buildSystem->project()->findNodeForBuildKey(buildKey);
    QTC_ASSERT(node, return);

    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size() + list.size());

    const QDir dir = node->filePath().toFileInfo().absoluteDir();
    for (const QString &path : list)
        m_entries += "$$PWD/" + dir.relativeFilePath(path);

    m_buildSystem->setExtraData(buildKey, Constants::AndroidExtraLibs, m_entries);
    endInsertRows();
}

static bool greaterModelIndexByRow(const QModelIndex &a, const QModelIndex &b)
{
    return a.row() > b.row();
}

void LibraryListModel::removeEntries(QModelIndexList list)
{
    if (list.isEmpty())
        return;

    std::sort(list.begin(), list.end(), greaterModelIndexByRow);

    int i = 0;
    while (i < list.size()) {
        int lastRow = list.at(i++).row();
        int firstRow = lastRow;
        while (i < list.size() && firstRow - list.at(i).row()  <= 1)
            firstRow = list.at(i++).row();

        beginRemoveRows(QModelIndex(), firstRow, lastRow);
        int count = lastRow - firstRow + 1;
        while (count-- > 0)
            m_entries.removeAt(firstRow);
        endRemoveRows();
    }

    const QString buildKey = m_buildSystem->buildConfiguration()->activeBuildKey();
    m_buildSystem->setExtraData(buildKey, Constants::AndroidExtraLibs, m_entries);
}

void LibraryListModel::updateModel()
{
    const QString buildKey = m_buildSystem->buildConfiguration()->activeBuildKey();
    const ProjectNode *node = m_buildSystem->project()->findNodeForBuildKey(buildKey);
    if (!node)
        return;

    if (node->parseInProgress()) {
        emit enabledChanged(false);
        return;
    }

    bool enabled;
    beginResetModel();
    if (node->validParse()) {
        m_entries = node->data(Constants::AndroidExtraLibs).toStringList();
        enabled = true;
    } else {
        // parsing error
        m_entries.clear();
        enabled = false;
    }
    endResetModel();

    emit enabledChanged(enabled);
}

const char KeystoreLocationKey[] = "KeystoreLocation";
const char BuildTargetSdkKey[] = "BuildTargetSdk";
const char BuildToolsVersionKey[] = "BuildToolsVersion";

class PasswordInputDialog : public QDialog
{
public:
    enum Context{
      KeystorePassword = 1,
      CertificatePassword
    };

    PasswordInputDialog(Context context, std::function<bool (const QString &)> callback,
                        const QString &extraContextStr);

    static QString getPassword(Context context, std::function<bool (const QString &)> callback,
                               const QString &extraContextStr, bool *ok = nullptr);

private:
    std::function<bool (const QString &)> verifyCallback = [](const QString &) { return true; };
    QLabel *inputContextlabel = new QLabel(this);
    QLineEdit *inputEdit = new QLineEdit(this);
    Utils::InfoLabel *warningLabel = new Utils::InfoLabel(::Android::Tr::tr("Incorrect password."),
                                                          Utils::InfoLabel::Warning, this);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                       this);
};

static bool checkKeystorePassword(const FilePath &keystorePath, const QString &keystorePasswd)
{
    if (keystorePasswd.isEmpty())
        return false;
    const CommandLine cmd(AndroidConfig::keytoolPath(),
                          {"-list", "-keystore", keystorePath.toUserOutput(),
                           "--storepass", keystorePasswd});
    Process proc;
    proc.setCommand(cmd);
    proc.runBlocking(10s);
    return proc.result() == ProcessResult::FinishedWithSuccess;
}

static bool checkCertificatePassword(const FilePath &keystorePath, const QString &keystorePasswd,
                                     const QString &alias, const QString &certificatePasswd)
{
    // assumes that the keystore password is correct
    QStringList arguments = {"-certreq", "-keystore", keystorePath.toUserOutput(),
                             "--storepass", keystorePasswd, "-alias", alias, "-keypass"};
    if (certificatePasswd.isEmpty())
        arguments << keystorePasswd;
    else
        arguments << certificatePasswd;

    Process proc;
    proc.setCommand({AndroidConfig::keytoolPath(), arguments});
    proc.runBlocking(10s);
    return proc.result() == ProcessResult::FinishedWithSuccess;
}

static bool checkCertificateExists(const FilePath &keystorePath, const QString &keystorePasswd,
                                   const QString &alias)
{
    // assumes that the keystore password is correct
    const QStringList arguments = {"-list", "-keystore", keystorePath.toUserOutput(),
                                   "--storepass", keystorePasswd, "-alias", alias};

    Process proc;
    proc.setCommand({AndroidConfig::keytoolPath(), arguments});
    proc.runBlocking(10s);
    return proc.result() == ProcessResult::FinishedWithSuccess;
}

// AndroidBuildApkWidget

class AndroidBuildApkWidget : public QWidget
{
public:
    explicit AndroidBuildApkWidget(AndroidBuildApkStep *step);

private:
    void setCertificates();
    void updateSigningWarning();
    void signPackageCheckBoxToggled(bool checked);
    void onOpenSslCheckBoxChanged();
    bool isOpenSslLibsIncluded();
    FilePath appProjectFilePath() const;
    QString openSslIncludeFileContent(const FilePath &projectPath);

private:
    AndroidBuildApkStep *m_step = nullptr;
    QCheckBox *m_signPackageCheckBox = nullptr;
    InfoLabel *m_signingDebugWarningLabel = nullptr;
    QComboBox *m_certificatesAliasComboBox = nullptr;
    QCheckBox *m_addDebuggerCheckBox = nullptr;
    QCheckBox *m_openSslCheckBox = nullptr;
};

AndroidBuildApkWidget::AndroidBuildApkWidget(AndroidBuildApkStep *step)
    : m_step(step)
{
    QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);


    // Application Signature Group

    auto keystoreLocationChooser = new PathChooser;
    keystoreLocationChooser->setExpectedKind(PathChooser::File);
    keystoreLocationChooser->lineEdit()->setReadOnly(true);
    keystoreLocationChooser->setFilePath(m_step->keystorePath());
    keystoreLocationChooser->setInitialBrowsePathBackup(FileUtils::homePath());
    keystoreLocationChooser->setPromptDialogFilter(Tr::tr("Keystore files (*.keystore *.jks)"));
    keystoreLocationChooser->setPromptDialogTitle(Tr::tr("Select Keystore File"));
    connect(keystoreLocationChooser, &PathChooser::textChanged, this, [this, keystoreLocationChooser] {
        const FilePath file = keystoreLocationChooser->unexpandedFilePath();
        m_step->setKeystorePath(file);
        m_signPackageCheckBox->setChecked(!file.isEmpty());
        if (!file.isEmpty())
            setCertificates();
    });

    auto keystoreCreateButton = new QPushButton(Tr::tr("Create..."));
    connect(keystoreCreateButton, &QAbstractButton::clicked, this, [this, keystoreLocationChooser] {
        const auto data = executeKeystoreCertificateDialog();
        if (!data)
            return;
        keystoreLocationChooser->setFilePath(data->keystoreFilePath);
        m_step->setKeystorePath(data->keystoreFilePath);
        m_step->setKeystorePassword(data->keystorePassword);
        m_step->setCertificateAlias(data->certificateAlias);
        m_step->setCertificatePassword(data->certificatePassword);
        setCertificates();
    });

    m_signPackageCheckBox = new QCheckBox(Tr::tr("Sign package"));
    m_signPackageCheckBox->setChecked(m_step->signPackage());

    m_signingDebugWarningLabel = new InfoLabel(Tr::tr("Signing a debug package"),
                                               InfoLabel::Warning);
    m_signingDebugWarningLabel->hide();
    m_signingDebugWarningLabel->setSizePolicy(QSizePolicy::MinimumExpanding,
                                              QSizePolicy::Preferred);

    m_certificatesAliasComboBox = new QComboBox;
    m_certificatesAliasComboBox->setEnabled(false);
    m_certificatesAliasComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    using namespace Layouting;
    Group signPackageGroup {
        title(Tr::tr("Application Signature")),
        Form {
            Tr::tr("Keystore:"), keystoreLocationChooser, keystoreCreateButton, br,
            m_signPackageCheckBox, br,
            Tr::tr("Certificate alias:"), m_certificatesAliasComboBox,
                    m_signingDebugWarningLabel, st, br,
        }
    };

    connect(m_signPackageCheckBox, &QAbstractButton::toggled,
            this, &AndroidBuildApkWidget::signPackageCheckBoxToggled);

    auto updateAlias = [this](int idx) {
        QString alias = m_certificatesAliasComboBox->itemText(idx);
        if (!alias.isEmpty())
            m_step->setCertificateAlias(alias);
    };

    connect(m_certificatesAliasComboBox, &QComboBox::activated, this, updateAlias);
    connect(m_certificatesAliasComboBox, &QComboBox::currentIndexChanged, this, updateAlias);

    // Application group

    QtSupport::QtVersion *qt = QtSupport::QtKitAspect::qtVersion(m_step->kit());
    const int minApiSupported = defaultMinimumSDK(qt);
    QStringList targets = AndroidConfig::apiLevelNamesFor(
        sdkManager().filteredSdkPlatforms(minApiSupported));
    targets.removeDuplicates();

    auto targetSDKComboBox = new QComboBox();
    targetSDKComboBox->addItems(targets);
    targetSDKComboBox->setCurrentIndex(targets.indexOf(m_step->buildTargetSdk()));

    connect(targetSDKComboBox, &QComboBox::activated, this, [this, targetSDKComboBox](int idx) {
       const QString sdk = targetSDKComboBox->itemText(idx);
       m_step->setBuildTargetSdk(sdk);
   });
    targetSDKComboBox->setCurrentIndex(targets.indexOf(m_step->buildTargetSdk()));

    const QList<QVersionNumber> buildToolsVersions
        = Utils::transform(sdkManager().filteredBuildTools(minApiSupported),
                [](const BuildTools *pkg) {
        return pkg->revision();
    });

    auto buildToolsSdkComboBox = new QComboBox();
    for (const QVersionNumber &version : buildToolsVersions)
        buildToolsSdkComboBox->addItem(version.toString(), QVariant::fromValue(version));
    connect(buildToolsSdkComboBox, &QComboBox::activated, this,
            [this, buildToolsSdkComboBox](int idx) {
        m_step->setBuildToolsVersion(buildToolsSdkComboBox->itemData(idx).value<QVersionNumber>());
    });

    if (!buildToolsVersions.isEmpty()) {
        const int initIdx = (m_step->buildToolsVersion().majorVersion() < 1)
                                ? buildToolsVersions.indexOf(buildToolsVersions.last())
                                : buildToolsVersions.indexOf(m_step->buildToolsVersion());
        buildToolsSdkComboBox->setCurrentIndex(initIdx);
    }

    auto createAndroidTemplatesButton = new QPushButton(Tr::tr("Create Templates"));
    createAndroidTemplatesButton->setToolTip(
        Tr::tr("Create an Android package for Custom Java code, assets, and Gradle configurations."));
    connect(createAndroidTemplatesButton, &QAbstractButton::clicked, this, [this] {
        executeManifestWizard(m_step->buildSystem());
    });

    Group applicationGroup {
        title(Tr::tr("Application")),
        Form {
            Tr::tr("Android build-tools version:"), buildToolsSdkComboBox, br,
            Tr::tr("Android build platform SDK:"), targetSDKComboBox, br,
            Tr::tr("Android customization:"), createAndroidTemplatesButton,
        }
    };


    // Advanced Actions group

    m_addDebuggerCheckBox = new QCheckBox(Tr::tr("Add debug server"));
    m_addDebuggerCheckBox->setEnabled(false);
    m_addDebuggerCheckBox->setToolTip(Tr::tr("Packages debug server with "
           "the APK to enable debugging. For the signed APK this option is unchecked by default."));
    m_addDebuggerCheckBox->setChecked(m_step->addDebugger());
    connect(m_addDebuggerCheckBox, &QAbstractButton::toggled,
            m_step, &AndroidBuildApkStep::setAddDebugger);

    Group advancedGroup {
        title(Tr::tr("Advanced Actions")),
        Column {
            m_step->buildAAB,
            m_step->openPackageLocation,
            m_step->verboseOutput,
            m_addDebuggerCheckBox
        }
    };


    // Additional Libraries group

    auto additionalLibrariesGroup = new QGroupBox(Tr::tr("Additional Libraries"));
    additionalLibrariesGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto libsModel = new LibraryListModel(m_step->buildSystem(), this);
    connect(libsModel, &LibraryListModel::enabledChanged, this,
            [this, additionalLibrariesGroup](const bool enabled) {
                additionalLibrariesGroup->setEnabled(enabled);
                m_openSslCheckBox->setChecked(isOpenSslLibsIncluded());
    });

    auto libsView = new QListView;
    libsView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    libsView->setToolTip(Tr::tr("List of extra libraries to include in Android package and load on startup."));
    libsView->setModel(libsModel);

    auto addLibButton = new QPushButton;
    addLibButton->setText(Tr::tr("Add..."));
    addLibButton->setToolTip(Tr::tr("Select library to include in package."));
    addLibButton->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    connect(addLibButton, &QAbstractButton::clicked, this, [this, libsModel] {
        QStringList fileNames = QFileDialog::getOpenFileNames(this,
                                                              Tr::tr("Select additional libraries"),
                                                              QDir::homePath(),
                                                              Tr::tr("Libraries (*.so)"));
        if (!fileNames.isEmpty())
            libsModel->addEntries(fileNames);
    });

    auto removeLibButton = new QPushButton;
    removeLibButton->setText(Tr::tr("Remove"));
    removeLibButton->setToolTip(Tr::tr("Remove currently selected library from list."));
    connect(removeLibButton, &QAbstractButton::clicked, this, [libsModel, libsView] {
        QModelIndexList removeList = libsView->selectionModel()->selectedIndexes();
        libsModel->removeEntries(removeList);
    });

    m_openSslCheckBox = new QCheckBox(Tr::tr("Include prebuilt OpenSSL libraries"));
    m_openSslCheckBox->setToolTip(Tr::tr("This is useful for apps that use SSL operations. The path "
                                     "can be defined in Edit > Preferences > Devices > Android."));
    connect(m_openSslCheckBox, &QAbstractButton::clicked, this,
            &AndroidBuildApkWidget::onOpenSslCheckBoxChanged);

    Grid {
        m_openSslCheckBox, br,
        libsView, Column { addLibButton, removeLibButton, st }
    }.attachTo(additionalLibrariesGroup);

    QItemSelectionModel *libSelection = libsView->selectionModel();
    connect(libSelection, &QItemSelectionModel::selectionChanged, this, [libSelection, removeLibButton] {
        removeLibButton->setEnabled(libSelection->hasSelection());
    });

    const QString buildKey = m_step->buildConfiguration()->activeBuildKey();
    const ProjectNode *node = m_step->project()->findNodeForBuildKey(buildKey);
    additionalLibrariesGroup->setEnabled(node && !node->parseInProgress());

    // main layout

    Column {
        signPackageGroup,
        applicationGroup,
        advancedGroup,
        additionalLibrariesGroup,
        noMargin
    }.attachTo(this);

    connect(m_step->buildConfiguration(), &BuildConfiguration::buildTypeChanged,
            this, &AndroidBuildApkWidget::updateSigningWarning);

    connect(m_signPackageCheckBox, &QAbstractButton::clicked,
            m_addDebuggerCheckBox, &QWidget::setEnabled);

    signPackageCheckBoxToggled(m_step->signPackage());
    updateSigningWarning();
}

void AndroidBuildApkWidget::signPackageCheckBoxToggled(bool checked)
{
    m_certificatesAliasComboBox->setEnabled(checked);
    m_step->setSignPackage(checked);
    m_addDebuggerCheckBox->setChecked(!checked);
    updateSigningWarning();
    if (!checked)
        return;
    if (!m_step->keystorePath().isEmpty())
        setCertificates();
}

void AndroidBuildApkWidget::onOpenSslCheckBoxChanged()
{
    Utils::FilePath projectPath = appProjectFilePath();
    QFile projectFile(projectPath.toFSPathString());
    if (!projectFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
        qWarning() << "Cannot open project file to add OpenSSL extra libs: " << projectPath;
        return;
    }

    const QString searchStr = openSslIncludeFileContent(projectPath);
    QTextStream textStream(&projectFile);

    QString fileContent = textStream.readAll();
    if (!m_openSslCheckBox->isChecked()) {
        fileContent.remove("\n" + searchStr);
    } else if (!fileContent.contains(searchStr, Qt::CaseSensitive)) {
        fileContent.append(searchStr + "\n");
    }

    projectFile.resize(0);
    textStream << fileContent;
    projectFile.close();
}

FilePath AndroidBuildApkWidget::appProjectFilePath() const
{
    const FilePath topLevelFile = m_step->buildConfiguration()->buildSystem()->projectFilePath();
    if (topLevelFile.fileName() == "CMakeLists.txt")
        return topLevelFile;
    static const auto isApp = [](Node *n) { return n->asProjectNode()
                && n->asProjectNode()->productType() == ProductType::App; };
    Node * const appNode = m_step->buildConfiguration()->project()->rootProjectNode()
            ->findNode(isApp);
    return appNode ? appNode ->filePath() : topLevelFile;
}

bool AndroidBuildApkWidget::isOpenSslLibsIncluded()
{
    Utils::FilePath projectPath = appProjectFilePath();
    const QString searchStr = openSslIncludeFileContent(projectPath);
    QFile projectFile(projectPath.toFSPathString());
    if (!projectFile.open(QIODevice::ReadOnly))
        return false;
    QTextStream textStream(&projectFile);
    QString fileContent = textStream.readAll();
    projectFile.close();
    return fileContent.contains(searchStr, Qt::CaseSensitive);
}

QString AndroidBuildApkWidget::openSslIncludeFileContent(const FilePath &projectPath)
{
    QString openSslPath = AndroidConfig::openSslLocation().path();
    if (projectPath.suffixView() == u"pro")
        return "android: include(" + openSslPath + "/openssl.pri)";
    if (projectPath.fileNameView() == u"CMakeLists.txt")
        return "if (ANDROID)\n    include(" + openSslPath + "/CMakeLists.txt)\nendif()";
    return {};
}

void AndroidBuildApkWidget::setCertificates()
{
    QAbstractItemModel *certificates = m_step->keystoreCertificates();
    if (certificates) {
        m_signPackageCheckBox->setChecked(certificates);
        m_certificatesAliasComboBox->setModel(certificates);
    }
}

void AndroidBuildApkWidget::updateSigningWarning()
{
    bool nonRelease = m_step->buildType() != BuildConfiguration::Release;
    bool visible = m_step->signPackage() && nonRelease;
    m_signingDebugWarningLabel->setVisible(visible);
}

// AndroidBuildApkStep

AndroidBuildApkStep::AndroidBuildApkStep(BuildStepList *parent, Utils::Id id)
    : AbstractProcessStep(parent, id),
      m_buildTargetSdk(AndroidConfig::apiLevelNameFor(sdkManager().latestAndroidSdkPlatform()))
{
    setImmutable(true);
    setDisplayName(Tr::tr("Build Android APK"));

    QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(kit());

    // FIXME: This is not saved due to missing setSettingsKey(). Intentional?
    buildAAB.setLabelText(Tr::tr("Build Android App Bundle (*.aab)"));
    buildAAB.setVisible(version && version->qtVersion() >= QVersionNumber(5, 14));

    // FIXME: This is not saved due to missing setSettingsKey(). Intentional?
    openPackageLocation.setLabelText(Tr::tr("Open package location after build"));

    verboseOutput.setSettingsKey("VerboseOutput");
    verboseOutput.setLabelText(Tr::tr("Verbose output"));

    connect(this, &BuildStep::addOutput, this, [this](const QString &string, OutputFormat format) {
        if (format == OutputFormat::Stderr)
            stdError(string);
    });
}

static QString packageSubPath(const AndroidBuildApkStep *step)
{
    const bool deb = (step->buildConfiguration()->buildType() == BuildConfiguration::Debug);
    const bool sign = step->signPackage();
    if (!step->buildAAB()) { // APK build
        if (deb && !sign)
            return "apk/debug/android-build-debug.apk";
        return QLatin1String(sign ? "apk/release/android-build-release-signed.apk"
                                  : "apk/release/android-build-release-unsigned.apk");
    }
    return QLatin1String(deb ? "bundle/debug/android-build-debug.aab"
                             : "bundle/release/android-build-release.aab");
}

static FilePath packagePath(const AndroidBuildApkStep *step)
{
    return androidBuildDirectory(step->buildConfiguration()) / "build/outputs" / packageSubPath(step);
}

bool AndroidBuildApkStep::init()
{
    if (!AbstractProcessStep::init()) {
        reportWarningOrError(Tr::tr("\"%1\" step failed initialization.").arg(displayName()),
                             Task::Error);
        return false;
    }

    if (m_signPackage) {
        qCDebug(buildapkstepLog) << "Signing enabled";
        // check keystore and certificate passwords
        if (!verifyKeystorePassword() || !verifyCertificatePassword()) {
            reportWarningOrError(Tr::tr("Keystore/Certificate password verification failed."),
                                 Task::Error);
            return false;
        }

        if (buildType() != BuildConfiguration::Release)
            reportWarningOrError(Tr::tr("Warning: Signing a debug or profile package."), Task::Warning);
    }

    QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(kit());
    if (!version) {
        reportWarningOrError(Tr::tr("The Qt version for kit %1 is invalid.").arg(kit()->displayName()),
                             Task::Error);
        return false;
    }

    const int minSDKForKit = minimumSDK(kit());
    if (minimumSDK(buildConfiguration()) < minSDKForKit) {
        const QString error
                = Tr::tr("The API level set for the APK is less than the minimum required by the kit."
                         "\nThe minimum API level required by the kit is %1.")
                .arg(minSDKForKit);
        reportWarningOrError(error, Task::Error);
        return false;
    }

    m_openPackageLocationForRun = openPackageLocation();
    const FilePath outputDir = androidBuildDirectory(buildConfiguration());
    m_packagePath = packagePath(this);

    qCDebug(buildapkstepLog).noquote() << "APK or AAB path:" << m_packagePath.toUserOutput();

    FilePath command = version->hostBinPath().pathAppended("androiddeployqt").withExecutableSuffix();

    m_inputFile = AndroidQtVersion::androidDeploymentSettings(buildConfiguration());
    if (m_inputFile.isEmpty()) {
        m_skipBuilding = true;
        reportWarningOrError(
            Tr::tr("No valid input file for \"%1\".").arg(buildConfiguration()->activeBuildKey()),
            Task::Warning);
        return true;
    }
    m_skipBuilding = false;

    if (m_buildTargetSdk.isEmpty()) {
        reportWarningOrError(Tr::tr("Android build SDK version is not defined. Check Android settings.")
                             , Task::Error);
        return false;
    }

    updateBuildToolsVersionInJsonFile();

    QStringList arguments = {"--input", m_inputFile.path(),
                             "--output", outputDir.path(),
                             "--android-platform", m_buildTargetSdk,
                             "--jdk", AndroidConfig::openJDKLocation().path()};

    if (verboseOutput())
        arguments << "--verbose";

    arguments << "--gradle";

    if (buildAAB())
        arguments << "--aab" <<  "--jarsigner";

    if (buildType() == BuildConfiguration::Release) {
        arguments << "--release";
    }

    QStringList argumentsPasswordConcealed = arguments;

    if (m_signPackage) {
        arguments << "--sign" << m_keystorePath.path() << m_certificateAlias
                  << "--storepass" << m_keystorePasswd;
        argumentsPasswordConcealed << "--sign" << "******"
                                   << "--storepass" << "******";
        if (!m_certificatePasswd.isEmpty()) {
            arguments << "--keypass" << m_certificatePasswd;
            argumentsPasswordConcealed << "--keypass" << "******";
        }

    }

    // Must be the last option, otherwise androiddeployqt might use the other
    // params (e.g. --sign) to choose not to add gdbserver
    if (version->qtVersion() >= QVersionNumber(5, 6, 0)) {
        if (m_addDebugger || buildType() == ProjectExplorer::BuildConfiguration::Debug)
            arguments << "--gdbserver";
        else
            arguments << "--no-gdbserver";
    }

    processParameters()->setCommandLine({command, arguments});

    // Generate arguments with keystore password concealed
    setupProcessParameters(&m_concealedParams);
    m_concealedParams.setCommandLine({command, argumentsPasswordConcealed});
    setDisplayedParameters(&m_concealedParams);
    return true;
}

void AndroidBuildApkStep::setupOutputFormatter(OutputFormatter *formatter)
{
    const auto parser = new JavaParser;
    parser->setProjectFileList(project()->files(Project::AllFiles));

    const QString buildKey = buildConfiguration()->activeBuildKey();
    const ProjectNode *node = project()->findNodeForBuildKey(buildKey);
    FilePath sourceDirPath;
    if (node)
        sourceDirPath = FilePath::fromVariant(node->data(Constants::AndroidPackageSourceDir));
    parser->setSourceDirectory(sourceDirPath.canonicalPath());
    parser->setBuildDirectory(androidBuildDirectory(buildConfiguration()));
    formatter->addLineParser(parser);
    AbstractProcessStep::setupOutputFormatter(formatter);
}

void AndroidBuildApkStep::showInGraphicalShell()
{
    FilePath packagePath = m_packagePath;
    if (!packagePath.exists()) { // File name might be incorrect. See: QTCREATORBUG-22627
        packagePath = packagePath.parentDir();
        if (!packagePath.exists()) {
            qCDebug(buildapkstepLog).noquote()
                    << "Could not open package location: " << packagePath;
            return;
        }
    }
    Core::FileUtils::showInGraphicalShell(packagePath);
}

QWidget *AndroidBuildApkStep::createConfigWidget()
{
    return new AndroidBuildApkWidget(this);
}

bool AndroidBuildApkStep::verifyKeystorePassword()
{
    if (!m_keystorePath.exists()) {
        reportWarningOrError(Tr::tr("Cannot sign the package. Invalid keystore path (%1).")
                             .arg(m_keystorePath.toUserOutput()), Task::Error);
        return false;
    }

    if (checkKeystorePassword(m_keystorePath, m_keystorePasswd))
        return true;

    bool success = false;
    auto verifyCallback = std::bind(&checkKeystorePassword,
                                    m_keystorePath, std::placeholders::_1);
    m_keystorePasswd = PasswordInputDialog::getPassword(PasswordInputDialog::KeystorePassword,
                                                        verifyCallback, "", &success);
    return success;
}

bool AndroidBuildApkStep::verifyCertificatePassword()
{
    if (!checkCertificateExists(m_keystorePath, m_keystorePasswd, m_certificateAlias)) {
        reportWarningOrError(Tr::tr("Cannot sign the package. Certificate alias %1 does not exist.")
                             .arg(m_certificateAlias), Task::Error);
        return false;
    }

    if (checkCertificatePassword(m_keystorePath, m_keystorePasswd,
                                 m_certificateAlias, m_certificatePasswd)) {
        return true;
    }

    bool success = false;
    auto verifyCallback = std::bind(&checkCertificatePassword,
                                    m_keystorePath, m_keystorePasswd,
                                    m_certificateAlias, std::placeholders::_1);

    m_certificatePasswd = PasswordInputDialog::getPassword(PasswordInputDialog::CertificatePassword,
                                                           verifyCallback, m_certificateAlias,
                                                           &success);
    return success;
}


static bool copyFileIfNewer(const FilePath &sourceFilePath, const FilePath &destinationFilePath)
{
    if (sourceFilePath == destinationFilePath)
        return true;
    if (destinationFilePath.exists()) {
        if (sourceFilePath.lastModified() <= destinationFilePath.lastModified())
            return true;
        if (!destinationFilePath.removeFile())
            return false;
    }

    if (!destinationFilePath.parentDir().ensureWritableDir())
        return false;
    Result<> result = sourceFilePath.copyFile(destinationFilePath);
    QTC_ASSERT_RESULT(result, return false);
    return true;
}

Tasking::GroupItem AndroidBuildApkStep::runRecipe()
{
    using namespace Tasking;

    const auto setupHelper = [this] {
        QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(kit());
        if (!version) {
            reportWarningOrError(Tr::tr("The Qt version for kit %1 is invalid.")
                                     .arg(kit()->displayName()), Task::Error);
            return false;
        }

        const auto androidAbis = applicationAbis(kit());
        const QString buildKey = buildConfiguration()->activeBuildKey();
        const FilePath buildDir = buildDirectory();
        const FilePath androidBuildDir = androidBuildDirectory(buildConfiguration());
        for (const auto &abi : androidAbis) {
            FilePath androidLibsDir = androidBuildDir / "libs" / abi;
            if (!androidLibsDir.exists()) {
                if (!androidLibsDir.ensureWritableDir()) {
                    reportWarningOrError(
                        Tr::tr("The Android build folder \"%1\" was not found and could "
                               "not be created.")
                            .arg(androidLibsDir.toUserOutput()),
                        Task::Error);
                    return false;
                } else if (version->qtVersion() >= QVersionNumber(6, 0, 0)
                           && version->qtVersion() <= QVersionNumber(6, 1, 1)) {
                    // 6.0.x <= Qt <= 6.1.1 used to need a manaul call to _prepare_apk_dir target,
                    // and now it's made directly with ALL target, so this code below ensures
                    // these versions are not broken.
                    const QString fileName = QString("lib%1_%2.so").arg(buildKey, abi);
                    const FilePath from = buildDir / fileName;
                    const FilePath to = androidLibsDir / fileName;
                    if (!from.exists() || to.exists())
                        continue;

                    if (!from.copyFile(to)) {
                        reportWarningOrError(Tr::tr("Cannot copy the target's lib file \"%1\" to "
                                                    "the Android build folder \"%2\".")
                                                 .arg(fileName, androidLibsDir.toUserOutput()),
                                             Task::Error);
                        return false;
                    }
                }
            }
        }

        const bool inputExists = m_inputFile.exists();
        if (inputExists && !isQtCreatorGenerated(m_inputFile))
            return true; // use the generated file if it was not generated by qtcreator

        BuildSystem *bs = buildSystem();
        const FilePaths targets = Utils::transform(
            bs->extraData(buildKey, Android::Constants::AndroidTargets).toStringList(),
            &FilePath::fromUserInput);
        if (targets.isEmpty())
            return inputExists; // qmake does this job for us

        QJsonObject deploySettings = deploymentSettings(kit());
        QString applicationBinary;
        if (!version->supportsMultipleQtAbis()) {
            QTC_ASSERT(androidAbis.size() == 1, return false);
            applicationBinary = buildSystem()->buildTarget(buildKey).targetFilePath.path();
            FilePath androidLibsDir = androidBuildDir / "libs" / androidAbis.first();
            for (const FilePath &target : targets) {
                if (!copyFileIfNewer(target, androidLibsDir.pathAppended(target.fileName()))) {
                    reportWarningOrError(
                        Tr::tr("Cannot copy file \"%1\" to Android build libs folder \"%2\".")
                            .arg(target.toUserOutput(), androidLibsDir.toUserOutput()),
                        Task::Error);
                    return false;
                }
            }
            deploySettings["target-architecture"] = androidAbis.first();
        } else {
            applicationBinary = buildSystem()->buildTarget(buildKey).targetFilePath.fileName();
            QJsonObject architectures;

            // Copy targets to android build folder
            for (const auto &abi : androidAbis) {
                QString targetSuffix = QString{"_%1.so"}.arg(abi);
                if (applicationBinary.endsWith(targetSuffix)) {
                    // Keep only TargetName from "lib[TargetName]_abi.so"
                    applicationBinary.remove(0, 3).chop(targetSuffix.size());
                }

                FilePath androidLibsDir = androidBuildDir / "libs" / abi;
                for (const FilePath &target : targets) {
                    if (target.endsWith(targetSuffix)) {
                        const FilePath destination = androidLibsDir.pathAppended(target.fileName());
                        if (!copyFileIfNewer(target, destination)) {
                            reportWarningOrError(
                                Tr::tr("Cannot copy file \"%1\" to Android build libs folder \"%2\".")
                                    .arg(target.toUserOutput(), androidLibsDir.toUserOutput()),
                                Task::Error);
                            return false;
                        }
                        architectures[abi] = archTriplet(abi);
                    }
                }
            }
            deploySettings["architectures"] = architectures;
        }
        deploySettings["application-binary"] = applicationBinary;

        const QString extraLibs = bs->extraData(buildKey, Android::Constants::AndroidExtraLibs).toString();
        if (!extraLibs.isEmpty())
            deploySettings["android-extra-libs"] = extraLibs;

        const QString androidSrcs = bs->extraData(buildKey, Android::Constants::AndroidPackageSourceDir).toString();
        if (!androidSrcs.isEmpty())
            deploySettings["android-package-source-directory"] = androidSrcs;

        const QString qmlImportPath = bs->extraData(buildKey, "QML_IMPORT_PATH").toString();
        if (!qmlImportPath.isEmpty())
            deploySettings["qml-import-paths"] = qmlImportPath;

        QString qmlRootPath = bs->extraData(buildKey, "QML_ROOT_PATH").toString();
        if (qmlRootPath.isEmpty())
            qmlRootPath = project()->rootProjectDirectory().path();
        deploySettings["qml-root-path"] = qmlRootPath;

        const Result<qint64> result = m_inputFile.writeFileContents(QJsonDocument{deploySettings}.toJson());
        if (!result) {
            reportWarningOrError(
                Tr::tr("Cannot open androiddeployqt input file \"%1\" for writing.")
                    .arg(m_inputFile.toUserOutput())
                    .append(' ')
                    .append(result.error()),
                Task::Error);
            return false;
        }

        return true;
    };

    const auto onSetup = [this, setupHelper] {
        if (m_skipBuilding) {
            reportWarningOrError(Tr::tr("Android deploy settings file not found, "
                                        "not building an APK."), Task::Error);
            return SetupResult::StopWithSuccess;
        }
        if (skipInstallationAndPackageSteps(buildConfiguration())) {
            reportWarningOrError(Tr::tr("Product type is not an application, not building an APK."),
                                 Task::Warning);
            return SetupResult::StopWithSuccess;
        }
        if (setupHelper())
            return SetupResult::Continue;
        reportWarningOrError(Tr::tr("Cannot set up \"%1\", not building an APK.")
                                 .arg(displayName()), Task::Error);
        return SetupResult::StopWithError;
    };
    const auto onDone = [this] {
        if (m_openPackageLocationForRun)
            QTimer::singleShot(0, this, &AndroidBuildApkStep::showInGraphicalShell);
    };

    const Group root {
        onGroupSetup(onSetup),
        onGroupDone(onDone, CallDoneIf::Success),
        defaultProcessTask()
    };
    return root;
}

void AndroidBuildApkStep::reportWarningOrError(const QString &message, Task::TaskType type)
{
    qCDebug(buildapkstepLog) << message;
    emit addOutput(message, OutputFormat::ErrorMessage);
    TaskHub::addTask(BuildSystemTask(type, message));
}

void AndroidBuildApkStep::updateBuildToolsVersionInJsonFile()
{
    Result<QByteArray> contents = m_inputFile.fileContents();
    if (!contents)
        return;

    static const QRegularExpression regex(R"("sdkBuildToolsRevision":."[0-9.]+")");
    QRegularExpressionMatch match = regex.match(QString::fromUtf8(contents.value()));
    const QString version = buildToolsVersion().toString();
    if (match.hasMatch() && !version.isEmpty()) {
        const auto newStr = QLatin1String("\"sdkBuildToolsRevision\": \"%1\"").arg(version).toUtf8();
        contents->replace(match.captured(0).toUtf8(), newStr);
        m_inputFile.writeFileContents(contents.value());
    }
}

void AndroidBuildApkStep::fromMap(const Store &map)
{
    m_keystorePath = FilePath::fromSettings(map.value(KeystoreLocationKey));
    m_signPackage = false; // don't restore this
    m_buildTargetSdk = map.value(BuildTargetSdkKey).toString();
    m_buildToolsVersion = QVersionNumber::fromString(map.value(BuildToolsVersionKey).toString());
    if (m_buildTargetSdk.isEmpty()) {
        m_buildTargetSdk = AndroidConfig::apiLevelNameFor(sdkManager().latestAndroidSdkPlatform());
    }
    ProjectExplorer::BuildStep::fromMap(map);
}

void AndroidBuildApkStep::toMap(Store &map) const
{
    ProjectExplorer::AbstractProcessStep::toMap(map);
    map.insert(KeystoreLocationKey, m_keystorePath.toSettings());
    map.insert(BuildTargetSdkKey, m_buildTargetSdk);
    map.insert(BuildToolsVersionKey, m_buildToolsVersion.toString());
}

Utils::FilePath AndroidBuildApkStep::keystorePath() const
{
    return m_keystorePath;
}

QString AndroidBuildApkStep::buildTargetSdk() const
{
    return m_buildTargetSdk;
}

void AndroidBuildApkStep::setBuildTargetSdk(const QString &sdk)
{
    m_buildTargetSdk = sdk;
}

QVersionNumber AndroidBuildApkStep::buildToolsVersion() const
{
    return m_buildToolsVersion;
}

void AndroidBuildApkStep::setBuildToolsVersion(const QVersionNumber &version)
{
    m_buildToolsVersion = version;
}

void AndroidBuildApkStep::stdError(const QString &output)
{
    QString newOutput = output;
    static const QRegularExpression re("^(\\n)+");
    newOutput.remove(re);

    if (newOutput.isEmpty())
        return;

    if (newOutput.startsWith("warning", Qt::CaseInsensitive)
        || newOutput.startsWith("note", Qt::CaseInsensitive))
        TaskHub::addTask(BuildSystemTask(Task::Warning, newOutput));
    else
        TaskHub::addTask(BuildSystemTask(Task::Error, newOutput));
}

QVariant AndroidBuildApkStep::data(Utils::Id id) const
{
    if (id == Constants::AndroidNdkPlatform) {
        if (auto qtVersion = QtKitAspect::qtVersion(kit()))
            return AndroidConfig::bestNdkPlatformMatch(minimumSDK(buildConfiguration()), qtVersion);
        return {};
    }
    if (id == Constants::NdkLocation) {
        if (auto qtVersion = QtKitAspect::qtVersion(kit()))
            return QVariant::fromValue(AndroidConfig::ndkLocation(qtVersion));
        return {};
    }
    if (id == Constants::SdkLocation)
        return QVariant::fromValue(AndroidConfig::sdkLocation());

    if (id == Constants::AndroidMkSpecAbis)
        return applicationAbis(kit());

    return AbstractProcessStep::data(id);
}

void AndroidBuildApkStep::setKeystorePath(const Utils::FilePath &path)
{
    m_keystorePath = path;
    m_certificatePasswd.clear();
    m_keystorePasswd.clear();
}

void AndroidBuildApkStep::setKeystorePassword(const QString &pwd)
{
    m_keystorePasswd = pwd;
}

void AndroidBuildApkStep::setCertificateAlias(const QString &alias)
{
    m_certificateAlias = alias;
}

void AndroidBuildApkStep::setCertificatePassword(const QString &pwd)
{
    m_certificatePasswd = pwd;
}

bool AndroidBuildApkStep::signPackage() const
{
    return m_signPackage;
}

void AndroidBuildApkStep::setSignPackage(bool b)
{
    m_signPackage = b;
}

bool AndroidBuildApkStep::addDebugger() const
{
    return m_addDebugger;
}

void AndroidBuildApkStep::setAddDebugger(bool debug)
{
    m_addDebugger = debug;
}

QAbstractItemModel *AndroidBuildApkStep::keystoreCertificates()
{
    // check keystore passwords
    if (!verifyKeystorePassword())
        return nullptr;

    CertificatesModel *model = nullptr;
    const QStringList params = {"-list", "-v", "-keystore", m_keystorePath.toUserOutput(),
        "-storepass", m_keystorePasswd, "-J-Duser.language=en"};

    Process keytoolProc;
    keytoolProc.setCommand({AndroidConfig::keytoolPath(), params});
    using namespace std::chrono_literals;
    keytoolProc.runBlocking(30s);
    if (keytoolProc.result() > ProcessResult::FinishedWithError)
        QMessageBox::critical(nullptr, Tr::tr("Error"), Tr::tr("Failed to run keytool."));
    else
        model = new CertificatesModel(keytoolProc.cleanedStdOut(), this);

    return model;
}

PasswordInputDialog::PasswordInputDialog(PasswordInputDialog::Context context,
                                         std::function<bool (const QString &)> callback,
                                         const QString &extraContextStr) :
    QDialog(Core::ICore::dialogParent(), Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint),
    verifyCallback(callback)

{
    inputEdit->setEchoMode(QLineEdit::Password);

    warningLabel->hide();

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(inputContextlabel);
    mainLayout->addWidget(inputEdit);
    mainLayout->addWidget(warningLabel);
    mainLayout->addWidget(buttonBox);

    connect(inputEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.isEmpty());
    });

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this] {
        if (verifyCallback(inputEdit->text())) {
            accept(); // Dialog accepted.
        } else {
            warningLabel->show();
            inputEdit->clear();
            adjustSize();
        }
    });

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setWindowTitle(context == KeystorePassword ? Tr::tr("Keystore") : Tr::tr("Certificate"));

    QString contextStr;
    if (context == KeystorePassword)
        contextStr = Tr::tr("Enter keystore password");
    else
        contextStr = Tr::tr("Enter certificate password");

    contextStr += extraContextStr.isEmpty() ? QStringLiteral(":") :
                                              QStringLiteral(" (%1):").arg(extraContextStr);
    inputContextlabel->setText(contextStr);
}

QString PasswordInputDialog::getPassword(Context context, std::function<bool (const QString &)> callback,
                                         const QString &extraContextStr, bool *ok)
{
    PasswordInputDialog dlg(context, callback, extraContextStr);
    bool isAccepted = dlg.exec() == QDialog::Accepted;
    if (ok)
        *ok = isAccepted;
    return isAccepted ? dlg.inputEdit->text() : QString();
}

// AndroidBuildApkStepFactory

class AndroidBuildApkStepFactory final : public BuildStepFactory
{
public:
    AndroidBuildApkStepFactory()
    {
        registerStep<AndroidBuildApkStep>(Constants::ANDROID_BUILD_APK_ID);
        setSupportedDeviceType(Constants::ANDROID_DEVICE_TYPE);
        setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
        setDisplayName(Tr::tr("Build Android APK"));
        setRepeatable(false);
    }
};

void setupAndroidBuildApkStep()
{
    static AndroidBuildApkStepFactory theAndroidBuildApkStepFactory;
}

} // Android::Internal

#include "androidbuildapkstep.moc"
