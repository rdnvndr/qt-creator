// Copyright (C) 2022 The Qt Company Ltd.
// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androiddevice.h"

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidsignaloperation.h"
#include "androidtr.h"
#include "androidutils.h"
#include "avdcreatordialog.h"
#include "avdmanageroutputparser.h"

#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevicefactory.h>
#include <projectexplorer/devicesupport/idevicewidget.h>
#include <projectexplorer/environmentkitaspect.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runconfiguration.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>

#include <solutions/tasking/conditional.h>

#include <utils/fileutils.h>
#include <utils/guard.h>
#include <utils/port.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/shutdownguard.h>
#include <utils/stringutils.h>
#include <utils/url.h>

#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QInputDialog>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

using namespace std::chrono_literals;

namespace {
static Q_LOGGING_CATEGORY(androidDeviceLog, "qtc.android.androiddevice", QtWarningMsg)
}

namespace Android::Internal {

static constexpr char ipRegexStr[] = "(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})";
static const QRegularExpression ipRegex = QRegularExpression(ipRegexStr);
static constexpr char wifiDevicePort[] = "5555";

enum TagModification { CommentOut, Uncomment };
static class AndroidDeviceManagerInstance *s_instance = nullptr;

struct SdkToolResult
{
    bool success = false;
    QString stdOut;
    QString stdErr;
    QString exitMessage;
};

static SdkToolResult runAdbCommand(const QStringList &args)
{
    Process process;
    const CommandLine command{AndroidConfig::adbToolPath(), args};
    qCDebug(androidDeviceLog) << "Running command (sync):" << command.toUserOutput();
    process.setCommand(command);
    process.runBlocking(30s, EventLoopMode::On);
    const bool success = process.result() == ProcessResult::FinishedWithSuccess;
    const SdkToolResult result = {success,
                                  process.cleanedStdOut().trimmed(),
                                  process.cleanedStdErr().trimmed(),
                                  success ? QString() : process.exitMessage()};
    qCDebug(androidDeviceLog) << "Command finshed (sync):" << command.toUserOutput()
                              << "Success:" << success
                              << "Output:" << process.allRawOutput();
    return result;
}

class AndroidDeviceManagerInstance : public QObject
{
public:
    AndroidDeviceManagerInstance();
    ~AndroidDeviceManagerInstance()
    {
        QTC_ASSERT(s_instance == this, return);
        s_instance = nullptr;
    }

    void setupDevicesWatcher();
    void eraseAvd(const IDevice::Ptr &device);

    Group m_avdListRecipe{};
    TaskTreeRunner m_avdListRunner;
    TaskTreeRunner m_avdDeviceWatcherRunner;
    std::unique_ptr<Process> m_removeAvdProcess;
    QFileSystemWatcher m_avdFileSystemWatcher;
    Guard m_avdPathGuard;
};

static QString displayNameFromInfo(const AndroidDeviceInfo &info)
{
    return info.type == IDevice::Hardware ? AndroidConfig::getProductModel(info.serialNumber)
                                          : info.avdName;
}

static IDevice::DeviceState getDeviceState(const QString &serial, IDevice::MachineType type)
{
    const QStringList args = adbSelector(serial) << "shell" << "echo 1";
    const SdkToolResult result = runAdbCommand(args);
    if (result.success)
        return IDevice::DeviceReadyToUse;
    else if (type == IDevice::Emulator || result.stdErr.contains("unauthorized"))
        return IDevice::DeviceConnected;
    return IDevice::DeviceDisconnected;
}

static void updateDeviceState(const IDevice::ConstPtr &device)
{
    const AndroidDevice *dev = static_cast<const AndroidDevice *>(device.get());
    const QString serial = dev->serialNumber();
    const Id id = dev->id();
    if (!serial.isEmpty())
        DeviceManager::setDeviceState(id, getDeviceState(serial, dev->machineType()));
    else if (dev->machineType() == IDevice::Emulator)
        DeviceManager::setDeviceState(id, IDevice::DeviceConnected);
}

static void setEmulatorArguments()
{
    const QString helpUrl =
        "https://developer.android.com/studio/run/emulator-commandline#startup-options";

    QInputDialog dialog(Core::ICore::dialogParent());
    dialog.setWindowTitle(Tr::tr("Emulator Command-line Startup Options"));
    dialog.setLabelText(Tr::tr("Emulator command-line startup options "
                               "(<a href=\"%1\">Help Web Page</a>):")
                            .arg(helpUrl));
    dialog.setTextValue(AndroidConfig::emulatorArgs());

    if (auto label = dialog.findChild<QLabel *>()) {
        label->setOpenExternalLinks(true);
        label->setMinimumWidth(500);
    }

    if (dialog.exec() == QDialog::Accepted)
        AndroidConfig::setEmulatorArgs(dialog.textValue());
}

static QString emulatorName(const QString &serialNumber)
{
    const QStringList args = adbSelector(serialNumber) << "emu" << "avd" << "name";
    return runAdbCommand(args).stdOut;
}

static QString getRunningAvdsSerialNumber(const QString &name)
{
    const QStringList lines = AndroidConfig::devicesCommandOutput();
    for (const QString &line : lines) {
        // skip the daemon logs
        if (line.startsWith("* daemon"))
            continue;

        const QString serialNumber = line.left(line.indexOf('\t')).trimmed();
        if (!serialNumber.startsWith("emulator"))
            continue;

        const QString stdOut = emulatorName(serialNumber);
        if (stdOut.isEmpty())
            continue; // Not an avd

        if (stdOut.left(stdOut.indexOf('\n')) == name)
            return serialNumber;
    }
    return {};
}

static FilePath avdFilePath()
{
    QString avdEnvVar = qtcEnvironmentVariable("ANDROID_AVD_HOME");
    if (avdEnvVar.isEmpty()) {
        avdEnvVar = qtcEnvironmentVariable("ANDROID_SDK_HOME");
        if (avdEnvVar.isEmpty())
            avdEnvVar = qtcEnvironmentVariable("HOME");
        avdEnvVar.append("/.android/avd");
    }
    return FilePath::fromUserInput(avdEnvVar);
}

static IDevice::Ptr createDeviceFromInfo(const CreateAvdInfo &info)
{
    if (info.apiLevel < 0) {
        qCWarning(androidDeviceLog) << "System image of the created AVD is nullptr";
        return IDevice::Ptr();
    }
    AndroidDevice *dev = new AndroidDevice;
    const Id deviceId = AndroidDevice::idFromAvdInfo(info);
    dev->setupId(IDevice::AutoDetected, deviceId);
    dev->setMachineType(IDevice::Emulator);
    dev->setDisplayName(info.name);
    dev->setDeviceState(IDevice::DeviceConnected);
    dev->setAvdPath(avdFilePath() / (info.name + ".avd"));
    dev->setExtraData(Constants::AndroidAvdName, info.name);
    dev->setExtraData(Constants::AndroidCpuAbi, {info.abi});
    dev->setExtraData(Constants::AndroidSdk, info.apiLevel);
    return IDevice::Ptr(dev);
}

class AndroidDeviceWidget : public IDeviceWidget
{
public:
    AndroidDeviceWidget(const IDevice::Ptr &device);

    void updateDeviceFromUi() final {}
    static QString dialogTitle();
    static bool messageDialog(const QString &msg, QMessageBox::Icon icon);
    static bool criticalDialog(const QString &error);
    static bool infoDialog(const QString &msg);
    static bool questionDialog(const QString &question);
};

static void setupWifiForDevice(const IDevice::Ptr &device, QWidget *parent)
{
    if (device->deviceState() != IDevice::DeviceReadyToUse) {
        AndroidDeviceWidget::infoDialog(
            Tr::tr("The device has to be connected with ADB debugging "
                   "enabled to use this feature."));
        return;
    }

    const auto androidDev = static_cast<const AndroidDevice *>(device.get());
    const QStringList adbSelector = Internal::adbSelector(androidDev->serialNumber());
    // prepare port
    QStringList args = adbSelector;
    args.append({"tcpip", wifiDevicePort});
    if (!runAdbCommand(args).success) {
        AndroidDeviceWidget::criticalDialog(
            Tr::tr("Opening connection port %1 failed.").arg(wifiDevicePort));
        return;
    }

    QTimer::singleShot(2000, parent, [adbSelector] {
        // Get device IP address
        QStringList args = adbSelector;
        args.append({"shell", "ip", "route"});
        const SdkToolResult ipRes = runAdbCommand(args);
        if (!ipRes.success) {
            AndroidDeviceWidget::criticalDialog(
                Tr::tr("Retrieving the device IP address failed."));
            return;
        }

        // Expected output from "ip route" is:
        // 192.168.1.0/24 dev wlan0 proto kernel scope link src 192.168.1.190
        // where the ip of interest is at the end of the line
        const QStringList ipParts = ipRes.stdOut.split(" ");
        QString ip;
        if (!ipParts.isEmpty()) {
            ip = ipParts.last();
        }
        if (!ipRegex.match(ipParts.last()).hasMatch()) {
            AndroidDeviceWidget::criticalDialog(
                Tr::tr("The retrieved IP address is invalid."));
            return;
        }

        // Connect to device
        args = adbSelector;
        args.append({"connect", QString("%1:%2").arg(ip).arg(wifiDevicePort)});
        if (!runAdbCommand(args).success) {
            AndroidDeviceWidget::criticalDialog(
                Tr::tr("Connecting to the device IP \"%1\" failed.").arg(ip));
            return;
        }
    });
}

AndroidDeviceWidget::AndroidDeviceWidget(const IDevice::Ptr &device)
    : IDeviceWidget(device)
{
    const auto dev = std::static_pointer_cast<AndroidDevice>(device);
    const auto formLayout = new QFormLayout(this);
    formLayout->setFormAlignment(Qt::AlignLeft);
    formLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(formLayout);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    formLayout->addRow(Tr::tr("Device name:"), new QLabel(dev->displayName()));
    formLayout->addRow(Tr::tr("Device type:"), new QLabel(dev->deviceTypeName()));

    QLabel *serialNumberLabel = new QLabel;
    formLayout->addRow(Tr::tr("Serial number:"), serialNumberLabel);

    const QString abis = dev->supportedAbis().join(", ");
    formLayout->addRow(Tr::tr("CPU architecture:"), new QLabel(abis));

    const auto osString = QString("%1 (SDK %2)").arg(dev->androidVersion()).arg(dev->sdkLevel());
    formLayout->addRow(Tr::tr("OS version:"), new QLabel(osString));

    if (dev->machineType() == IDevice::Hardware) {
        const QString authorizedStr = dev->deviceState() == IDevice::DeviceReadyToUse
                                          ? Tr::tr("Yes")
                                          : Tr::tr("No");
        formLayout->addRow(Tr::tr("Authorized:"), new QLabel(authorizedStr));
    }

    if (dev->machineType() == IDevice::Emulator) {
        const QString targetName = dev->androidTargetName();
        formLayout->addRow(Tr::tr("Android target flavor:"), new QLabel(targetName));
        formLayout->addRow(Tr::tr("SD card size:"), new QLabel(dev->sdcardSize()));
        formLayout->addRow(Tr::tr("Skin type:"), new QLabel(dev->skinName()));
        const QString openGlStatus = dev->openGLStatus();
        formLayout->addRow(Tr::tr("OpenGL status:"), new QLabel(openGlStatus));
    }

    // See QTCREATORBUG-31912 why this needs to be delayed.
    QTimer::singleShot(0, this, [serialNumberLabel, dev] {
        const QString serialNumber = dev->serialNumber(); // This executes a blocking process.
        const QString printableSerialNumber = serialNumber.isEmpty() ? Tr::tr("Unknown")
                                                                     : serialNumber;
        serialNumberLabel->setText(printableSerialNumber);
    });
}

QString AndroidDeviceWidget::dialogTitle()
{
    return Tr::tr("Android Device Manager");
}

bool AndroidDeviceWidget::messageDialog(const QString &msg, QMessageBox::Icon icon)
{
    qCDebug(androidDeviceLog) << msg;
    QMessageBox box(Core::ICore::dialogParent());
    box.QDialog::setWindowTitle(dialogTitle());
    box.setText(msg);
    box.setIcon(icon);
    box.setWindowFlag(Qt::WindowTitleHint);
    return box.exec();
}

bool AndroidDeviceWidget::criticalDialog(const QString &error)
{
    return messageDialog(error, QMessageBox::Critical);
}

bool AndroidDeviceWidget::infoDialog(const QString &message)
{
    return messageDialog(message, QMessageBox::Information);
}

bool AndroidDeviceWidget::questionDialog(const QString &question)
{
    QMessageBox box(Core::ICore::dialogParent());
    box.QDialog::setWindowTitle(dialogTitle());
    box.setText(question);
    box.setIcon(QMessageBox::Question);
    QPushButton *YesButton = box.addButton(QMessageBox::Yes);
    box.addButton(QMessageBox::No);
    box.setWindowFlag(Qt::WindowTitleHint);
    box.exec();

    if (box.clickedButton() == YesButton)
        return true;
    return false;
}

AndroidDevice::AndroidDevice()
{
    setupId(IDevice::AutoDetected, Constants::ANDROID_DEVICE_ID);
    setType(Constants::ANDROID_DEVICE_TYPE);
    setDefaultDisplayName(Tr::tr("Run on Android"));
    setDisplayType(Tr::tr("Android"));
    setMachineType(IDevice::Hardware);
    setOsType(OsType::OsTypeOtherUnix);
    setDeviceState(DeviceDisconnected);

    addDeviceAction({Tr::tr("Refresh"), [](const IDevice::Ptr &device) {
        updateDeviceState(device);
    }});
}

void AndroidDevice::addActionsIfNotFound()
{
    static const QString startAvdAction = Tr::tr("Start AVD");
    static const QString eraseAvdAction = Tr::tr("Erase AVD");
    static const QString avdArgumentsAction = Tr::tr("AVD Arguments");
    static const QString setupWifi = Tr::tr("Set up Wi-Fi");

    bool hasStartAction = false;
    bool hasEraseAction = false;
    bool hasAvdArgumentsAction = false;
    bool hasSetupWifi = false;

    for (const DeviceAction &item : deviceActions()) {
        if (item.display == startAvdAction)
            hasStartAction = true;
        else if (item.display == eraseAvdAction)
            hasEraseAction = true;
        else if (item.display == avdArgumentsAction)
            hasAvdArgumentsAction = true;
        else if (item.display == setupWifi)
            hasSetupWifi = true;
    }

    if (machineType() == Emulator) {
        if (!hasStartAction) {
            addDeviceAction({startAvdAction, [](const IDevice::Ptr &device) {
                static_cast<AndroidDevice *>(device.get())->startAvd();
            }});
        }

        if (!hasEraseAction) {
            addDeviceAction({eraseAvdAction, [](const IDevice::Ptr &device) {
                s_instance->eraseAvd(device);
            }});
        }

        if (!hasAvdArgumentsAction) {
            addDeviceAction({avdArgumentsAction, [](const IDevice::Ptr &) {
                setEmulatorArguments();
            }});
        }
    } else if (machineType() == Hardware && !ipRegex.match(id().toString()).hasMatch()) {
        if (!hasSetupWifi) {
            addDeviceAction({setupWifi, [](const IDevice::Ptr &device) {
                setupWifiForDevice(device, Core::ICore::dialogParent());
            }});
        }
    }
}

void AndroidDevice::fromMap(const Store &map)
{
    IDevice::fromMap(map);
    initAvdSettings();
    // Add Actions for Emulator and hardware if not added already.
    // This is needed because actions for Emulators and physical devices are not the same.
    addActionsIfNotFound();
    setFreePorts(PortList::fromString("5555-5585"));
}

IDevice::Ptr AndroidDevice::create()
{
    return IDevice::Ptr(new AndroidDevice);
}

AndroidDeviceInfo AndroidDevice::androidDeviceInfoFromDevice(const ConstPtr &dev)
{
    QTC_ASSERT(dev, return {});
    AndroidDeviceInfo info;
    info.state = dev->deviceState();
    info.avdName = dev->extraData(Constants::AndroidAvdName).toString();
    info.serialNumber = dev->extraData(Constants::AndroidSerialNumber).toString();
    info.cpuAbi = dev->extraData(Constants::AndroidCpuAbi).toStringList();
    info.avdPath = FilePath::fromSettings(dev->extraData(Constants::AndroidAvdPath));
    info.sdk = dev->extraData(Constants::AndroidSdk).toInt();
    info.type = dev->machineType();
    return info;
}

Id AndroidDevice::idFromDeviceInfo(const AndroidDeviceInfo &info)
{
    const QString id = (info.type == IDevice::Hardware ? info.serialNumber : info.avdName);
    return  Id(Constants::ANDROID_DEVICE_ID).withSuffix(':').withSuffix(id);
}

Id AndroidDevice::idFromAvdInfo(const CreateAvdInfo &info)
{
    return Id(Constants::ANDROID_DEVICE_ID).withSuffix(':').withSuffix(info.name);
}

QStringList AndroidDevice::supportedAbis() const
{
    return extraData(Constants::AndroidCpuAbi).toStringList();
}

bool AndroidDevice::canSupportAbis(const QStringList &abis) const
{
    // If the list is empty, no valid decision can be made, this means something is wrong
    // somewhere, but let's not stop deployment.
    QTC_ASSERT(!abis.isEmpty(), return true);

    const QStringList ourAbis = supportedAbis();
    QTC_ASSERT(!ourAbis.isEmpty(), return false);

    for (const QString &abi : abis)
        if (ourAbis.contains(abi))
            return true; // it's enough if only one abi match is found

    // If no exact match is found, let's take ABI backward compatibility into account
    // https://developer.android.com/ndk/guides/abis#android-platform-abi-support
    // arm64 usually can run {arm, armv7}, x86 can support {arm, armv7}, and 64-bit devices
    // can support their 32-bit variants.
    using namespace ProjectExplorer::Constants;
    const bool isTheirsArm = abis.contains(ANDROID_ABI_ARMEABI)
                                || abis.contains(ANDROID_ABI_ARMEABI_V7A);
    // The primary ABI at the first index
    const bool oursSupportsArm = ourAbis.first() == ANDROID_ABI_ARM64_V8A
                                || ourAbis.first() == ANDROID_ABI_X86;
    // arm64 and x86 can run armv7 and arm
    if (isTheirsArm && oursSupportsArm)
        return true;
    // x64 can run x86
    if (ourAbis.first() == ANDROID_ABI_X86_64 && abis.contains(ANDROID_ABI_X86))
        return true;

    return false;
}

bool AndroidDevice::canHandleDeployments() const
{
    // If hardware and disconned, it wouldn't be possilbe to start it, unlike an emulator
    if (machineType() == Hardware && deviceState() == DeviceDisconnected)
        return false;
    return true;
}

QString AndroidDevice::serialNumber() const
{
    const QString serialNumber = extraData(Constants::AndroidSerialNumber).toString();
    if (machineType() == Hardware)
        return serialNumber;
    return getRunningAvdsSerialNumber(avdName());
}

QString AndroidDevice::avdName() const
{
    return extraData(Constants::AndroidAvdName).toString();
}

int AndroidDevice::sdkLevel() const
{
    return extraData(Constants::AndroidSdk).toInt();
}

FilePath AndroidDevice::avdPath() const
{
    return FilePath::fromSettings(extraData(Constants::AndroidAvdPath));
}

void AndroidDevice::setAvdPath(const FilePath &path)
{
    setExtraData(Constants::AndroidAvdPath, path.toSettings());
    initAvdSettings();
}

QString AndroidDevice::androidVersion() const
{
    return androidNameForApiLevel(sdkLevel());
}

QString AndroidDevice::deviceTypeName() const
{
    if (machineType() == Emulator)
        return Tr::tr("Emulator for \"%1\"").arg(avdSettings()->value("hw.device.name").toString());
    return Tr::tr("Physical device");
}

QString AndroidDevice::skinName() const
{
    const QString skin = avdSettings()->value("skin.name").toString();
    return skin.isEmpty() ? Tr::tr("None", "No skin") : skin;
}

QString AndroidDevice::androidTargetName() const
{
    const QString target = avdSettings()->value("tag.display").toString();
    return target.isEmpty() ? Tr::tr("Unknown") : target;
}

QString AndroidDevice::sdcardSize() const
{
    const QString size = avdSettings()->value("sdcard.size").toString();
    return size.isEmpty() ? Tr::tr("Unknown") : size;
}

QString AndroidDevice::openGLStatus() const
{
    const QString openGL = avdSettings()->value("hw.gpu.enabled").toString();
    return openGL.isEmpty() ? Tr::tr("Unknown") : openGL;
}

void AndroidDevice::startAvd()
{
    const Storage<QString> serialNumberStorage;

    const auto onDone = [this, serialNumberStorage] {
        if (!serialNumberStorage->isEmpty())
            DeviceManager::setDeviceState(id(), IDevice::DeviceReadyToUse);
    };

    const Group root {
        serialNumberStorage,
        startAvdRecipe(avdName(), serialNumberStorage),
        onGroupDone(onDone, CallDoneIf::Success)
    };

    m_taskTreeRunner.start(root);
}

IDevice::DeviceInfo AndroidDevice::deviceInformation() const
{
    return IDevice::DeviceInfo();
}

IDeviceWidget *AndroidDevice::createWidget()
{
    return new AndroidDeviceWidget(shared_from_this());
}

DeviceProcessSignalOperation::Ptr AndroidDevice::signalOperation() const
{
    return DeviceProcessSignalOperation::Ptr(new AndroidSignalOperation());
}

ExecutableItem AndroidDevice::portsGatheringRecipe(const Storage<PortsOutputData> &output) const
{
    const Storage<QString> serialNumberStorage;
    const Storage<PortsInputData> input;

    const auto hasSerialNumber = [this, serialNumberStorage] {
        if (machineType() == Hardware)
            *serialNumberStorage = extraData(Constants::AndroidSerialNumber).toString();
        return machineType() == Hardware;
    };

    const auto onSerialNumberSetup = [this, input, serialNumberStorage] {
        const CommandLine cmd{AndroidConfig::adbToolPath(),
                              {adbSelector(*serialNumberStorage), "shell" , "netstat", "-a", "-n" }};
        *input = {freePorts(), cmd};
    };

    return Group {
        serialNumberStorage,
        input,
        If (!Sync(hasSerialNumber)) >> Then {
            serialNumberRecipe(avdName(), serialNumberStorage),
        },
        Sync(onSerialNumberSetup),
        portsFromProcessRecipe(input, output)
    };
}

QUrl AndroidDevice::toolControlChannel(const ControlChannelHint &) const
{
    QUrl url;
    url.setScheme(urlTcpScheme());
    QString deviceSerialNumber = serialNumber();
    const int colonPos = deviceSerialNumber.indexOf(QLatin1Char(':'));
    if (colonPos > 0) {
        // When wireless debugging is used then the device serial number will include a port number
        // The port number must be removed to form a valid hostname
        deviceSerialNumber.truncate(colonPos);
    }
    url.setHost(deviceSerialNumber);
    return url;
}

QSettings *AndroidDevice::avdSettings() const
{
    return m_avdSettings.get();
}

void AndroidDevice::initAvdSettings()
{
    const FilePath configPath = avdPath().resolvePath(QStringLiteral("config.ini"));
    m_avdSettings.reset(new QSettings(configPath.toUserOutput(), QSettings::IniFormat));
}

static void handleDevicesListChange(const QString &serialNumber)
{
    const QStringList serialBits = serialNumber.split('\t');
    if (serialBits.size() < 2)
        return;

    // Sample output of adb track-devices, the first 4 digits are for state type
    // and sometimes 4 zeros are reported as part for the serial number.
    // 00546db0e8d7 authorizing
    // 00546db0e8d7 device
    // 0000001711201JEC207789 offline
    // emulator-5554 device
    QString dirtySerial = serialBits.first().trimmed();
    if (dirtySerial.startsWith("0000"))
        dirtySerial = dirtySerial.mid(4);
    if (dirtySerial.startsWith("00"))
        dirtySerial = dirtySerial.mid(4);
    const bool isEmulator = dirtySerial.startsWith("emulator");

    const QString &serial = dirtySerial;
    const QString stateStr = serialBits.at(1).trimmed();

    IDevice::DeviceState state;
    if (stateStr == "device")
        state = IDevice::DeviceReadyToUse;
    else if (stateStr == "offline")
        state = IDevice::DeviceDisconnected;
    else
        state = IDevice::DeviceConnected;

    if (isEmulator) {
        const QString avdName = emulatorName(serial);
        const Id avdId = Id(Constants::ANDROID_DEVICE_ID).withSuffix(':').withSuffix(avdName);
        DeviceManager::setDeviceState(avdId, state);
    } else {
        const Id id = Id(Constants::ANDROID_DEVICE_ID).withSuffix(':').withSuffix(serial);
        QString displayName = AndroidConfig::getProductModel(serial);
        // Check if the device is connected via WiFi. A sample serial of such devices can be
        // like: "192.168.1.190:5555"
        static const auto ipRegex = QRegularExpression(ipRegexStr + QStringLiteral(":(\\d{1,5})"));
        if (ipRegex.match(serial).hasMatch())
            displayName += QLatin1String(" (WiFi)");

        if (IDevice::ConstPtr dev = DeviceManager::find(id)) {
            // DeviceManager doens't seem to have a way to directly update the name, if the name
            // of the device has changed, remove it and register it again with the new name.
            if (dev->displayName() == displayName)
                DeviceManager::setDeviceState(id, state);
            else
                DeviceManager::removeDevice(id);
        } else {
            AndroidDevice *newDev = new AndroidDevice();
            newDev->setupId(IDevice::AutoDetected, id);
            newDev->setDisplayName(displayName);
            newDev->setMachineType(IDevice::Hardware);
            newDev->setDeviceState(state);

            newDev->setExtraData(Constants::AndroidSerialNumber, serial);
            newDev->setExtraData(Constants::AndroidCpuAbi, AndroidConfig::getAbis(serial));
            newDev->setExtraData(Constants::AndroidSdk, AndroidConfig::getSDKVersion(serial));

            qCDebug(androidDeviceLog, "Registering new Android device id \"%s\".",
                    newDev->id().toString().toUtf8().data());
            DeviceManager::addDevice(IDevice::Ptr(newDev));
        }
    }
}

static void modifyManufacturerTag(const FilePath &avdPath, TagModification modification)
{
    if (!avdPath.exists())
        return;

    const FilePath configFilePath = avdPath / "config.ini";
    const Result<QByteArray> res = configFilePath.fileContents();
    if (!res)
        return;

    FileSaver saver(configFilePath);
    QTextStream textStream(normalizeNewlines(*res));
    while (!textStream.atEnd()) {
        QString line = textStream.readLine();
        if (line.contains("hw.device.manufacturer")) {
            if (modification == Uncomment)
                line.replace("#", "");
            else
                line.prepend("#");
        }
        line.append("\n");
        saver.write(line.toUtf8());
    }
    saver.finalize();
}

static void handleAvdListChange(const AndroidDeviceInfoList &avdList)
{
    QList<Id> existingAvds;
    for (int i = 0; i < DeviceManager::deviceCount(); ++i) {
        const IDevice::ConstPtr dev = DeviceManager::deviceAt(i);
        const bool isEmulator = dev->machineType() == IDevice::Emulator;
        if (isEmulator && dev->type() == Constants::ANDROID_DEVICE_TYPE)
            existingAvds.append(dev->id());
    }

    QList<Id> connectedDevs;
    for (const AndroidDeviceInfo &item : avdList) {
        const Id deviceId = AndroidDevice::idFromDeviceInfo(item);
        const QString displayName = displayNameFromInfo(item);
        IDevice::ConstPtr dev = DeviceManager::find(deviceId);
        if (dev) {
            const auto androidDev = static_cast<const AndroidDevice *>(dev.get());
            // DeviceManager doens't seem to have a way to directly update the name, if the name
            // of the device has changed, remove it and register it again with the new name.
            // Also account for the case of an AVD registered through old QC which might have
            // invalid data by checking if the avdPath is not empty.
            if (dev->displayName() != displayName || androidDev->avdPath().isEmpty()) {
                DeviceManager::removeDevice(dev->id());
            } else {
                // Find the state of the AVD retrieved from the AVD watcher
                const QString serial = getRunningAvdsSerialNumber(item.avdName);
                if (!serial.isEmpty()) {
                    const IDevice::DeviceState state = getDeviceState(serial, IDevice::Emulator);
                    if (dev->deviceState() != state) {
                        DeviceManager::setDeviceState(dev->id(), state);
                        qCDebug(androidDeviceLog, "Device id \"%s\" changed its state.",
                                dev->id().toString().toUtf8().data());
                    }
                } else {
                    DeviceManager::setDeviceState(dev->id(), IDevice::DeviceConnected);
                }
                connectedDevs.append(dev->id());
                continue;
            }
        }

        AndroidDevice::Ptr newDev = std::make_shared<AndroidDevice>();
        newDev->setupId(IDevice::AutoDetected, deviceId);
        newDev->setDisplayName(displayName);
        newDev->setMachineType(item.type);
        newDev->setDeviceState(item.state);

        newDev->setExtraData(Constants::AndroidAvdName, item.avdName);
        newDev->setExtraData(Constants::AndroidSerialNumber, item.serialNumber);
        newDev->setExtraData(Constants::AndroidCpuAbi, item.cpuAbi);
        newDev->setExtraData(Constants::AndroidSdk, item.sdk);
        newDev->setAvdPath(item.avdPath);

        qCDebug(androidDeviceLog, "Registering new Android device id \"%s\".",
                newDev->id().toString().toUtf8().data());
        DeviceManager::addDevice(newDev);
        connectedDevs.append(newDev->id());
    }

    // Set devices no longer connected to disconnected state.
    for (const Id &id : existingAvds) {
        if (!connectedDevs.contains(id)) {
            qCDebug(androidDeviceLog, "Removing AVD id \"%s\" because it no longer exists.",
                    id.toString().toUtf8().data());
            DeviceManager::removeDevice(id);
        }
    }
}

AndroidDeviceManagerInstance::AndroidDeviceManagerInstance()
{
    QTC_ASSERT(!s_instance, return);
    s_instance = this;

    const Storage<FilePaths> storage;

    const LoopUntil iterator([storage](int iteration) {
        return iteration == 0 || storage->count() > 0;
    });

    const auto onProcessSetup = [](Process &process) {
        const CommandLine cmd(AndroidConfig::avdManagerToolPath(), {"list", "avd"});
        qCDebug(androidDeviceLog).noquote() << "Running AVD Manager command:" << cmd.toUserOutput();
        process.setEnvironment(AndroidConfig::toolsEnvironment());
        process.setCommand(cmd);
    };
    const auto onProcessDone = [storage](const Process &process, DoneWith result) {
        const QString output = process.allOutput();
        if (result != DoneWith::Success) {
            qCDebug(androidDeviceLog)
                << "Avd list command failed" << output << AndroidConfig::sdkToolsVersion();
            return DoneResult::Error;
        }

        const auto parsedAvdList = parseAvdList(output);
        if (parsedAvdList.errorPaths.isEmpty()) {
            for (const FilePath &avdPath : std::as_const(*storage))
                modifyManufacturerTag(avdPath, Uncomment);
            storage->clear(); // Don't repeat anymore
            handleAvdListChange(parsedAvdList.avdList);
        } else {
            for (const FilePath &avdPath : parsedAvdList.errorPaths)
                modifyManufacturerTag(avdPath, CommentOut);
            storage->append(parsedAvdList.errorPaths);
        }
        return DoneResult::Success; // Repeat
    };

    // Currenly avdmanager tool fails to parse some AVDs because the correct
    // device definitions at devices.xml does not have some of the newest devices.
    // Particularly, failing because of tag "hw.device.manufacturer", thus removing
    // it would make paring successful. However, it has to be returned afterwards,
    // otherwise, Android Studio would give an error during parsing also. So this fix
    // aim to keep support for Qt Creator and Android Studio.

    m_avdListRecipe = For (iterator) >> Do {
        storage,
        ProcessTask(onProcessSetup, onProcessDone)
    };
}

void AndroidDeviceManagerInstance::setupDevicesWatcher()
{
    if (!AndroidConfig::adbToolPath().exists()) {
        qCDebug(androidDeviceLog) << "Cannot start ADB device watcher"
                                  <<  "because adb path does not exist.";
        return;
    }

    if (m_avdDeviceWatcherRunner.isRunning()) {
        qCDebug(androidDeviceLog) << "ADB device watcher is already running.";
        return;
    }

    const auto onSetup = [](Process &process) {
        const CommandLine command{AndroidConfig::adbToolPath(), {"track-devices"}};
        process.setCommand(command);
        process.setWorkingDirectory(command.executable().parentDir());
        process.setEnvironment(AndroidConfig::toolsEnvironment());
        process.setStdErrLineCallback([](const QString &error) {
            qCDebug(androidDeviceLog) << "ADB device watcher error" << error; });
        process.setStdOutLineCallback([](const QString &output) {
            handleDevicesListChange(output);
        });
    };
    const auto onDone = [](const Process &process, DoneWith result) {
        qCDebug(androidDeviceLog) << "ADB device watcher finished.";
        if (result != DoneWith::Error)
            return DoneResult::Error; // Stop the Forever loop.

        qCDebug(androidDeviceLog) << "ADB device watcher encountered an error:"
                                  << process.errorString();
        qCDebug(androidDeviceLog) << "Restarting the ADB device watcher now.";
        return DoneResult::Success; // Continue the Forever loop.
    };

    m_avdDeviceWatcherRunner.start(Group { Forever { ProcessTask(onSetup, onDone) } });

    // Setup AVD filesystem watcher to listen for changes when an avd is created/deleted,
    // or started/stopped
    m_avdFileSystemWatcher.addPath(avdFilePath().toFSPathString());
    connect(&m_avdFileSystemWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (!m_avdPathGuard.isLocked())
            updateAvdList();
    });
    // Call initial update
    updateAvdList();
}

void AndroidDeviceManagerInstance::eraseAvd(const IDevice::Ptr &device)
{
    if (!device)
        return;

    if (device->machineType() == IDevice::Hardware)
        return;

    const QString name = static_cast<const AndroidDevice *>(device.get())->avdName();
    const QString question
        = Tr::tr("Erase the Android AVD \"%1\"?\nThis cannot be undone.").arg(name);
    if (!AndroidDeviceWidget::questionDialog(question))
        return;

    qCDebug(androidDeviceLog) << QString("Erasing Android AVD \"%1\" from the system.").arg(name);
    m_removeAvdProcess.reset(new Process);
    const CommandLine command(AndroidConfig::avdManagerToolPath(), {"delete", "avd", "-n", name});
    qCDebug(androidDeviceLog).noquote() << "Running command (removeAvd):" << command.toUserOutput();
    m_removeAvdProcess->setEnvironment(AndroidConfig::toolsEnvironment());
    m_removeAvdProcess->setCommand(command);
    connect(m_removeAvdProcess.get(), &Process::done, this, [this, device] {
        const QString name = device->displayName();
        if (m_removeAvdProcess->result() == ProcessResult::FinishedWithSuccess) {
            qCDebug(androidDeviceLog, "Android AVD id \"%s\" removed from the system.",
                    qPrintable(name));
            // Remove the device from QtC after it's been removed using avdmanager.
            DeviceManager::removeDevice(device->id());
        } else {
            AndroidDeviceWidget::criticalDialog(Tr::tr("An error occurred while removing the "
                                                       "Android AVD \"%1\" using avdmanager tool.").arg(name));
        }
        m_removeAvdProcess.release()->deleteLater();
    });
    m_removeAvdProcess->start();
}

void setupDevicesWatcher()
{
    s_instance->setupDevicesWatcher();
}

void updateAvdList()
{
    if (AndroidConfig::adbToolPath().exists())
        s_instance->m_avdListRunner.start(s_instance->m_avdListRecipe);
}

Group createAvdRecipe(const Storage<std::optional<QString>> &errorStorage,
                      const CreateAvdInfo &info, bool force)
{
    struct GuardWrapper {
        GuardLocker locker = GuardLocker(s_instance->m_avdPathGuard);
        QByteArray buffer;
    };

    const Storage<GuardWrapper> storage;

    const auto onSetup = [storage, info, force](Process &process) {
        CommandLine cmd(AndroidConfig::avdManagerToolPath(), {"create", "avd", "-n", info.name});
        cmd.addArgs({"-k", info.sdkStylePath});
        if (info.sdcardSize > 0)
            cmd.addArgs({"-c", QString("%1M").arg(info.sdcardSize)});

        const QString deviceDef = info.deviceDefinition;
        if (!deviceDef.isEmpty() && deviceDef != "Custom")
            cmd.addArgs({"-d", deviceDef});

        if (force)
            cmd.addArg("-f");

        process.setProcessMode(ProcessMode::Writer);
        process.setEnvironment(AndroidConfig::toolsEnvironment());
        process.setCommand(cmd);
        process.setWriteData("yes\n"); // yes to "Do you wish to create a custom hardware profile"

        QByteArray *buffer = &storage->buffer;
        Process *processPtr = &process;

        QObject::connect(processPtr, &Process::readyReadStandardOutput, processPtr, [processPtr, buffer] {
            // This interaction is needed only if there is no "-d" arg for the avdmanager command.
            *buffer += processPtr->readAllRawStandardOutput();
            if (buffer->endsWith(QByteArray("]:"))) {
                // truncate to last line
                const int index = buffer->lastIndexOf('\n');
                if (index != -1)
                    *buffer = buffer->mid(index);
                if (buffer->contains("hw.gpu.enabled"))
                    processPtr->write("yes\n");
                else
                    processPtr->write("\n");
                buffer->clear();
            }
        });
    };

    const auto onDone = [errorStorage](const Process &process) {
        const QString stdErr = process.stdErr();
        const QString errorMessage = stdErr.isEmpty() ? process.exitMessage()
                                                      : process.exitMessage() + "\n\n" + stdErr;
        *errorStorage = errorMessage;
    };

    return {
        storage,
        ProcessTask(onSetup, onDone, CallDoneIf::Error)
    };
}

// Factory

class AndroidDeviceFactory final : public IDeviceFactory
{
public:
    AndroidDeviceFactory()
        : IDeviceFactory(Constants::ANDROID_DEVICE_TYPE)
    {
        setDisplayName(Tr::tr("Android Device"));
        setCombinedIcon(":/android/images/androiddevicesmall.png",
                        ":/android/images/androiddevice.png");
        setConstructionFunction(&AndroidDevice::create);
        setCreator([] {
            if (!AndroidConfig::sdkToolsOk()) {
                AndroidDeviceWidget::infoDialog(Tr::tr("Android support is not yet configured."));
                return IDevice::Ptr();
            }

            const auto info = executeAvdCreatorDialog();
            if (!info)
                return IDevice::Ptr();

            const IDevice::Ptr dev = createDeviceFromInfo(*info);
            if (const auto androidDev = static_cast<AndroidDevice *>(dev.get())) {
                qCDebug(androidDeviceLog, "Created new Android AVD id \"%s\".",
                        qPrintable(androidDev->avdName()));
                return dev;
            }
            AndroidDeviceWidget::criticalDialog(
                Tr::tr("The device info returned from AvdDialog is invalid."));
            return IDevice::Ptr();
        });
    }
};

void setupAndroidDevice()
{
    static AndroidDeviceFactory theAndroidDeviceFactory;
}

void setupAndroidDeviceManager()
{
    static GuardedObject<AndroidDeviceManagerInstance> theAndroidDeviceManager;
}

} // Android::Internal
