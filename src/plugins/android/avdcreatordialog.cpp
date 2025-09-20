// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "avdcreatordialog.h"

#include "androidconfigurations.h"
#include "androiddevice.h"
#include "androidsdkmanager.h"
#include "androidtr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/projectexplorerconstants.h>

#include <solutions/spinner/spinner.h>
#include <solutions/tasking/tasktreerunner.h>

#include <utils/algorithm.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/progressdialog.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/tooltip/tooltip.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSysInfo>
#include <QTimer>
#include <QToolTip>

using namespace ProjectExplorer;
using namespace SpinnerSolution;
using namespace Tasking;
using namespace Utils;

namespace Android::Internal {

static Q_LOGGING_CATEGORY(avdDialogLog, "qtc.android.avdDialog", QtWarningMsg)

enum class DeviceType { Phone, Tablet, Automotive, TV, Wear, Desktop, PhoneOrTablet };

static DeviceType tagToDeviceType(const QString &type_tag)
{
    if (type_tag.contains("android-wear"))
        return DeviceType::Wear;
    else if (type_tag.contains("android-tv"))
        return DeviceType::TV;
    else if (type_tag.contains("android-automotive"))
        return DeviceType::Automotive;
    else if (type_tag.contains("android-desktop"))
        return DeviceType::Desktop;
    return DeviceType::PhoneOrTablet;
}

class AvdDialog : public QDialog
{
public:
    explicit AvdDialog();
    CreateAvdInfo avdInfo() const { return m_createdAvdInfo; }

private:
    const SystemImage *systemImage() const
    { return m_targetApiComboBox->currentData().value<SystemImage*>(); }
    QString name() const { return m_nameLineEdit->text(); }
    QString abi() const { return m_abiComboBox->currentText(); }
    QString deviceDefinition() const { return m_deviceDefinitionComboBox->currentText(); }
    int sdcardSize() const { return m_sdcardSizeSpinBox->value(); }

    bool eventFilter(QObject *obj, QEvent *event) override;
    void updateDeviceDefinitionComboBox();
    void updateApiLevelComboBox();
    void collectInitialData();
    void createAvd();

    struct DeviceDefinitionStruct
    {
        QString name_id;
        QString type_str;
        DeviceType deviceType;
    };

    CreateAvdInfo m_createdAvdInfo;
    QTimer m_hideTipTimer;
    QRegularExpression m_allowedNameChars;
    QList<DeviceDefinitionStruct> m_deviceDefinitionsList;
    QMap<DeviceType, QString> m_deviceTypeToStringMap;

    QWidget *m_gui;
    QComboBox *m_abiComboBox;
    QSpinBox *m_sdcardSizeSpinBox;
    QLineEdit *m_nameLineEdit;
    QComboBox *m_targetApiComboBox;
    QComboBox *m_deviceDefinitionComboBox;
    Utils::InfoLabel *m_warningText;
    QComboBox *m_deviceDefinitionTypeComboBox;
    QCheckBox *m_overwriteCheckBox;
    QDialogButtonBox *m_buttonBox;
    Tasking::TaskTreeRunner m_taskTreeRunner;
};

AvdDialog::AvdDialog()
    : QDialog(Core::ICore::dialogParent())
    , m_allowedNameChars(QLatin1String("[a-z|A-Z|0-9|._-]*"))
{
    resize(800, 0);
    setWindowTitle(Tr::tr("Create new AVD"));

    m_abiComboBox = new QComboBox;
    // Put the host architectures on top prioritizing 64 bit
    const QStringList armAbis = {
        ProjectExplorer::Constants::ANDROID_ABI_ARM64_V8A,
        ProjectExplorer::Constants::ANDROID_ABI_ARMEABI_V7A,
    };

    const QStringList x86Abis = {
        ProjectExplorer::Constants::ANDROID_ABI_X86_64,
        ProjectExplorer::Constants::ANDROID_ABI_X86
    };

    QStringList items;
    if (QSysInfo::currentCpuArchitecture().startsWith("arm"))
        items << armAbis << x86Abis;
    else
        items << x86Abis << armAbis;

    m_abiComboBox->addItems(items);

    m_sdcardSizeSpinBox = new QSpinBox;
    m_sdcardSizeSpinBox->setSuffix(Tr::tr(" MiB"));
    m_sdcardSizeSpinBox->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    m_sdcardSizeSpinBox->setRange(0, 1000000);
    m_sdcardSizeSpinBox->setValue(512);

    m_nameLineEdit = new QLineEdit;
    m_nameLineEdit->setValidator(new QRegularExpressionValidator(m_allowedNameChars, this));
    m_nameLineEdit->installEventFilter(this);

    m_targetApiComboBox = new QComboBox;

    m_deviceDefinitionComboBox = new QComboBox;

    m_warningText = new InfoLabel;
    m_warningText->setType(InfoLabel::Warning);
    m_warningText->setElideMode(Qt::ElideRight);

    m_deviceDefinitionTypeComboBox = new QComboBox;

    m_overwriteCheckBox = new QCheckBox(Tr::tr("Overwrite existing AVD name"));

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

    m_hideTipTimer.setInterval(2000);
    m_hideTipTimer.setSingleShot(true);

    using namespace Layouting;

    m_gui = new QWidget;

    Column {
        m_gui = Form {
            Tr::tr("Name:"), m_nameLineEdit, br,
            Tr::tr("Target ABI / API:"),
                Row { m_abiComboBox, m_targetApiComboBox }, br,
            QString(), m_warningText, br,
            Tr::tr("Skin definition:"),
                Row { m_deviceDefinitionTypeComboBox, m_deviceDefinitionComboBox }, br,
            Tr::tr("SD card size:"), m_sdcardSizeSpinBox, br,
            QString(), m_overwriteCheckBox,
            noMargin
        }.emerge(),
        st,
        m_buttonBox
    }.attachTo(this);

    connect(&m_hideTipTimer, &QTimer::timeout, this, &Utils::ToolTip::hide);
    connect(m_deviceDefinitionTypeComboBox, &QComboBox::currentIndexChanged,
            this, &AvdDialog::updateDeviceDefinitionComboBox);
    connect(m_abiComboBox, &QComboBox::currentIndexChanged,
            this, &AvdDialog::updateApiLevelComboBox);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AvdDialog::createAvd);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_deviceTypeToStringMap.insert(DeviceType::Phone, "Phone");
    m_deviceTypeToStringMap.insert(DeviceType::Tablet, "Tablet");
    m_deviceTypeToStringMap.insert(DeviceType::Automotive, "Automotive");
    m_deviceTypeToStringMap.insert(DeviceType::TV, "TV");
    m_deviceTypeToStringMap.insert(DeviceType::Wear, "Wear");
    m_deviceTypeToStringMap.insert(DeviceType::Desktop, "Desktop");

    collectInitialData();
}

void AvdDialog::collectInitialData()
{
    const auto onProcessSetup = [this](Process &process) {
        m_gui->setEnabled(false);
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        const CommandLine cmd(AndroidConfig::avdManagerToolPath(), {"list", "device"});
        qCDebug(avdDialogLog).noquote() << "Running AVD Manager command:" << cmd.toUserOutput();
        process.setEnvironment(AndroidConfig::toolsEnvironment());
        process.setCommand(cmd);
    };
    const auto onProcessDone = [this](const Process &process, DoneWith result) {
        const QString output = process.allOutput();
        if (result == DoneWith::Error) {
            QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("Create new AVD"),
                                 Tr::tr("Avd list command failed. %1 %2")
                                     .arg(output, AndroidConfig::sdkToolsVersion().toString()));
            reject();
            return;
        }

/* Example output:

Available devices definitions:
id: 0 or "automotive_1024p_landscape"
    Name: Automotive (1024p landscape)
    OEM : Google
    Tag : android-automotive-playstore
---------
id: 1 or "automotive_1080p_landscape"
    Name: Automotive (1080p landscape)
    OEM : Google
    Tag : android-automotive
---------
id: 2 or "Galaxy Nexus"
    Name: Galaxy Nexus
    OEM : Google
---------
id: 3 or "desktop_large"
    Name: Large Desktop
    OEM : Google
    Tag : android-desktop
...
*/
        QStringList avdDeviceInfo;

        const auto lines = output.split('\n');
        for (const QString &line : lines) {
            if (line.startsWith("---------") || line.isEmpty()) {
                DeviceDefinitionStruct deviceDefinition;
                for (const QString &line : avdDeviceInfo) {
                    if (line.contains("id:")) {
                        deviceDefinition.name_id = line.split("or").at(1);
                        deviceDefinition.name_id = deviceDefinition.name_id.remove(0, 1).remove('"');
                    } else if (line.contains("Tag :")) {
                        deviceDefinition.type_str = line.split(':').at(1);
                        deviceDefinition.type_str = deviceDefinition.type_str.remove(0, 1);
                    }
                }

                DeviceType deviceType = tagToDeviceType(deviceDefinition.type_str);
                if (deviceType == DeviceType::PhoneOrTablet) {
                    if (deviceDefinition.name_id.contains("Tablet"))
                        deviceType = DeviceType::Tablet;
                    else
                        deviceType = DeviceType::Phone;
                }
                deviceDefinition.deviceType = deviceType;
                m_deviceDefinitionsList.append(deviceDefinition);
                avdDeviceInfo.clear();
            } else {
                avdDeviceInfo << line;
            }
        }
        for (const QString &type : std::as_const(m_deviceTypeToStringMap))
            m_deviceDefinitionTypeComboBox->addItem(type);

        updateApiLevelComboBox();
        m_gui->setEnabled(true);
    };

    struct SpinnerStruct {
        std::unique_ptr<Spinner> spinner;
    };

    const Storage<SpinnerStruct> storage;

    const auto onSetup = [this, storage] {
        storage->spinner.reset(new Spinner(SpinnerSize::Medium, m_gui));
        storage->spinner->show();
    };

    const Group recipe {
        storage,
        onGroupSetup(onSetup),
        ProcessTask(onProcessSetup, onProcessDone)
    };

    m_taskTreeRunner.start(recipe);
}

void AvdDialog::createAvd()
{
    const SystemImage *si = systemImage();
    if (!si || !si->isValid() || name().isEmpty()) {
        QMessageBox::warning(Core::ICore::dialogParent(),
                             Tr::tr("Create new AVD"), Tr::tr("Cannot create AVD. Invalid input."));
        return;
    }
    const CreateAvdInfo avdInfo{si->sdkStylePath(), si->apiLevel(), name(), abi(),
                                deviceDefinition(), sdcardSize()};

    struct Progress {
        Progress() {
            progressDialog.reset(createProgressDialog(0, Tr::tr("Create new AVD"),
                                                      Tr::tr("Creating new AVD device...")));
        }
        std::unique_ptr<QProgressDialog> progressDialog;
    };

    const Storage<Progress> progressStorage;

    const auto onCancelSetup = [progressStorage] {
        return std::make_pair(progressStorage->progressDialog.get(), &QProgressDialog::canceled);
    };

    const Storage<std::optional<QString>> errorStorage;

    const auto onDone = [errorStorage] {
        if (errorStorage->has_value()) {
            QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("Create new AVD"),
                                 errorStorage->value());
        }
    };

    const Group recipe {
        progressStorage,
        errorStorage,
        createAvdRecipe(errorStorage, avdInfo, m_overwriteCheckBox->isChecked())
            .withCancel(onCancelSetup),
        onGroupDone(onDone, CallDoneIf::Error)
    };

    m_taskTreeRunner.start(recipe, {}, [this, avdInfo](DoneWith result) {
        if (result == DoneWith::Error)
            return;
        m_createdAvdInfo = avdInfo;
        updateAvdList();
        accept();
    });
}

void AvdDialog::updateDeviceDefinitionComboBox()
{
    DeviceType curDeviceType = m_deviceTypeToStringMap.key(
        m_deviceDefinitionTypeComboBox->currentText());

    m_deviceDefinitionComboBox->clear();
    for (const DeviceDefinitionStruct &item : std::as_const(m_deviceDefinitionsList)) {
        if (item.deviceType == curDeviceType)
            m_deviceDefinitionComboBox->addItem(item.name_id);
    }
    m_deviceDefinitionComboBox->addItem("Custom");

    updateApiLevelComboBox();
}

void AvdDialog::updateApiLevelComboBox()
{
    const SystemImageList installedSystemImages = sdkManager().installedSystemImages();
    DeviceType curDeviceType = m_deviceTypeToStringMap.key(
        m_deviceDefinitionTypeComboBox->currentText());

    QString selectedAbi = abi();
    auto hasAbi = [selectedAbi](const SystemImage *image) {
        return image && image->isValid() && (image->abiName() == selectedAbi);
    };

    const SystemImageList filteredList = Utils::filtered(
        installedSystemImages, [hasAbi, &curDeviceType](const SystemImage *image) {
        DeviceType deviceType = tagToDeviceType(image->sdkStylePath().split(';').at(2));
        if (deviceType == DeviceType::PhoneOrTablet
            && (curDeviceType == DeviceType::Phone || curDeviceType == DeviceType::Tablet)) {
            curDeviceType = DeviceType::PhoneOrTablet;
        }
        return image && deviceType == curDeviceType && hasAbi(image);
    });

    m_targetApiComboBox->clear();
    for (SystemImage *image : std::as_const(filteredList)) {
            QString imageString = "android-" % QString::number(image->apiLevel());
            const QStringList imageSplits = image->sdkStylePath().split(';');
            if (imageSplits.size() == 4)
                imageString += QStringLiteral(" (%1)").arg(imageSplits.at(2));
            m_targetApiComboBox->addItem(imageString,
                                                   QVariant::fromValue<SystemImage *>(image));
            m_targetApiComboBox->setItemData(m_targetApiComboBox->count() - 1,
                                                       image->descriptionText(),
                                                       Qt::ToolTipRole);
    }

    const QString installRecommendationMsg = Tr::tr(
        "Install a system image from the SDK Manager first.");

    if (installedSystemImages.isEmpty()) {
        m_targetApiComboBox->setEnabled(false);
        m_warningText->setVisible(true);
        m_warningText->setText(Tr::tr("No system images found.") + " " + installRecommendationMsg);
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    } else if (filteredList.isEmpty()) {
        m_targetApiComboBox->setEnabled(false);
        m_warningText->setVisible(true);
        m_warningText->setText(Tr::tr("No system images found for %1.").arg(abi()) + " " +
                               installRecommendationMsg);
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    } else {
        m_warningText->setVisible(false);
        m_targetApiComboBox->setEnabled(true);
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
}

bool AvdDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_nameLineEdit && event->type() == QEvent::KeyPress) {
        auto ke = static_cast<QKeyEvent *>(event);
        const QString key = ke->text();
        if (!key.isEmpty() && !m_allowedNameChars.match(key).hasMatch()) {
            QPoint position = m_nameLineEdit->parentWidget()->mapToGlobal(m_nameLineEdit->geometry().bottomLeft());
            position -= Utils::ToolTip::offsetFromPosition();
            Utils::ToolTip::show(position, Tr::tr("Allowed characters are: a-z A-Z 0-9 and . _ -"), m_nameLineEdit);
            m_hideTipTimer.start();
        } else {
            m_hideTipTimer.stop();
            Utils::ToolTip::hide();
        }
    }
    return QDialog::eventFilter(obj, event);
}

std::optional<CreateAvdInfo> executeAvdCreatorDialog()
{
    AvdDialog dialog;
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.avdInfo();
}

} // Android::Internal
