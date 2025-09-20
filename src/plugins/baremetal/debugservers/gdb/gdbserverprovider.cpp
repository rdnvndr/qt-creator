// Copyright (C) 2016 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gdbserverprovider.h"

#include <baremetal/baremetaldebugsupport.h>
#include <baremetal/baremetaldevice.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <debugger/debuggerengine.h>

#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/runcontrol.h>

#include <utils/environment.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/result.h>

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

using namespace Debugger;
using namespace ProjectExplorer;
using namespace Utils;

namespace BareMetal::Internal {

const char startupModeKeyC[] = "Mode";
const char peripheralDescriptionFileKeyC[] = "PeripheralDescriptionFile";
const char initCommandsKeyC[] = "InitCommands";
const char resetCommandsKeyC[] = "ResetCommands";
const char useExtendedRemoteKeyC[] = "UseExtendedRemote";
const char executableFileKeyC[] = "ExecutableFile";
const char additionalArgumentsKeyC[] = "AdditionalArguments";

// GdbServerProvider

GdbServerProvider::GdbServerProvider(const QString &id)
    : IDebugServerProvider(id)
{
    setEngineType(Debugger::GdbEngineType);
}

GdbServerProvider::StartupMode GdbServerProvider::startupMode() const
{
    return m_startupMode;
}

FilePath GdbServerProvider::peripheralDescriptionFile() const
{
    return m_peripheralDescriptionFile;
}

void GdbServerProvider::setStartupMode(StartupMode m)
{
    m_startupMode = m;
}

void GdbServerProvider::setPeripheralDescriptionFile(const FilePath &file)
{
    m_peripheralDescriptionFile = file;
}

QString GdbServerProvider::initCommands() const
{
    return m_initCommands;
}

void GdbServerProvider::setInitCommands(const QString &cmds)
{
    m_initCommands = cmds;
}

bool GdbServerProvider::useExtendedRemote() const
{
    return m_useExtendedRemote;
}

void GdbServerProvider::setUseExtendedRemote(bool useExtendedRemote)
{
    m_useExtendedRemote = useExtendedRemote;
}

QString GdbServerProvider::resetCommands() const
{
    return m_resetCommands;
}

void GdbServerProvider::setResetCommands(const QString &cmds)
{
    m_resetCommands = cmds;
}

Utils::CommandLine GdbServerProvider::command() const
{
    if (m_executableFile.isEmpty())
        return {};
    return CommandLine{m_executableFile, m_additionalArguments, CommandLine::Raw};
}

bool GdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!IDebugServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const GdbServerProvider *>(&other);
    return m_startupMode == p->m_startupMode
            && m_peripheralDescriptionFile == p->m_peripheralDescriptionFile
            && m_initCommands == p->m_initCommands
            && m_resetCommands == p->m_resetCommands
            && m_useExtendedRemote == p->m_useExtendedRemote;
}

void GdbServerProvider::toMap(Store &data) const
{
    IDebugServerProvider::toMap(data);
    data.insert(startupModeKeyC, m_startupMode);
    data.insert(peripheralDescriptionFileKeyC, m_peripheralDescriptionFile.toSettings());
    data.insert(initCommandsKeyC, m_initCommands);
    data.insert(resetCommandsKeyC, m_resetCommands);
    data.insert(useExtendedRemoteKeyC, m_useExtendedRemote);
    data.insert(executableFileKeyC, m_executableFile.toSettings());
    data.insert(additionalArgumentsKeyC, m_additionalArguments);
}

bool GdbServerProvider::isValid() const
{
    return (m_startupMode == GdbServerProvider::StartupOnNetwork && channel().isValid()) ||
           (m_startupMode == GdbServerProvider::StartupOnPipe && !channelPipe().isEmpty());
}

Result<> GdbServerProvider::setupDebuggerRunParameters(DebuggerRunParameters &rp,
                                                     RunControl *runControl) const
{
    Q_UNUSED(runControl)
    const CommandLine cmd = rp.inferior().command;
    const FilePath bin = FilePath::fromString(cmd.executable().path());
    if (bin.isEmpty()) {
        return ResultError(Tr::tr("Cannot debug: Local executable is not set."));
    }
    if (!bin.exists()) {
        return ResultError(Tr::tr("Cannot debug: Could not find executable for \"%1\".")
                                 .arg(bin.toUserOutput()));
    }

    ProcessRunData inferior;
    inferior.command.setExecutable(bin);
    inferior.command.setArguments(cmd.arguments());
    rp.setInferior(inferior);
    rp.setSymbolFile(bin);
    rp.setStartMode(AttachToRemoteServer);
    rp.setCommandsAfterConnect(initCommands()); // .. and here?
    rp.setCommandsForReset(resetCommands());
    if (m_startupMode == GdbServerProvider::StartupOnNetwork)
        rp.setRemoteChannel(channel());
    else
        rp.setRemoteChannelPipe(channelPipe());
    rp.setUseContinueInsteadOfRun(true);
    rp.setUseExtendedRemote(useExtendedRemote());
    rp.setPeripheralDescriptionFile(m_peripheralDescriptionFile);
    return ResultOk;
}

RunWorker *GdbServerProvider::targetRunner(RunControl *runControl) const
{
    const CommandLine cmd = command();
    if (m_startupMode != GdbServerProvider::StartupOnNetwork || cmd.isEmpty())
        return nullptr;

    // Command arguments are in host OS style as the bare metal's GDB servers are launched
    // on the host, not on that target.
    return createProcessWorker(runControl, [cmd](Process &process) {
        // Baremetal's GDB servers are launched on the host, not on the target.
        process.setCommand(cmd.toLocal());
    });
}

void GdbServerProvider::fromMap(const Store &data)
{
    IDebugServerProvider::fromMap(data);
    m_startupMode = static_cast<StartupMode>(data.value(startupModeKeyC).toInt());
    m_peripheralDescriptionFile = FilePath::fromSettings(data.value(peripheralDescriptionFileKeyC));
    m_executableFile = FilePath::fromSettings(data.value(executableFileKeyC));
    m_additionalArguments = data.value(additionalArgumentsKeyC).toString();
    m_initCommands = data.value(initCommandsKeyC).toString();
    m_resetCommands = data.value(resetCommandsKeyC).toString();
    m_useExtendedRemote = data.value(useExtendedRemoteKeyC).toBool();
}

// GdbServerProviderConfigWidget

GdbServerProviderConfigWidget::GdbServerProviderConfigWidget(
        GdbServerProvider *provider)
    : IDebugServerProviderConfigWidget(provider)
{
    m_startupModeComboBox = new QComboBox(this);
    m_startupModeComboBox->setToolTip(Tr::tr("Choose the desired startup mode "
                                             "of the GDB server provider."));
    m_mainLayout->addRow(Tr::tr("Startup mode:"), m_startupModeComboBox);

    m_peripheralDescriptionFileChooser = new PathChooser(this);
    m_peripheralDescriptionFileChooser->setExpectedKind(PathChooser::File);
    m_peripheralDescriptionFileChooser->setPromptDialogFilter(
                Tr::tr("Peripheral description files (*.svd)"));
    m_peripheralDescriptionFileChooser->setPromptDialogTitle(
                Tr::tr("Select Peripheral Description File"));
    m_mainLayout->addRow(Tr::tr("Peripheral description file:"),
                         m_peripheralDescriptionFileChooser);

    populateStartupModes();
    setFromProvider();

    connect(m_startupModeComboBox, &QComboBox::currentIndexChanged,
            this, &GdbServerProviderConfigWidget::dirty);
    connect(m_peripheralDescriptionFileChooser, &PathChooser::textChanged,
            this, &GdbServerProviderConfigWidget::dirty);
}

void GdbServerProviderConfigWidget::apply()
{
    const auto p = static_cast<GdbServerProvider *>(m_provider);
    p->setStartupMode(startupMode());
    p->setPeripheralDescriptionFile(peripheralDescriptionFile());
    IDebugServerProviderConfigWidget::apply();
}

void GdbServerProviderConfigWidget::discard()
{
    setFromProvider();
    IDebugServerProviderConfigWidget::discard();
}

GdbServerProvider::StartupMode GdbServerProviderConfigWidget::startupModeFromIndex(
        int idx) const
{
    return static_cast<GdbServerProvider::StartupMode>(
                m_startupModeComboBox->itemData(idx).toInt());
}

GdbServerProvider::StartupMode GdbServerProviderConfigWidget::startupMode() const
{
    const int idx = m_startupModeComboBox->currentIndex();
    return startupModeFromIndex(idx);
}

void GdbServerProviderConfigWidget::setStartupMode(GdbServerProvider::StartupMode m)
{
    for (int idx = 0; idx < m_startupModeComboBox->count(); ++idx) {
        if (m == startupModeFromIndex(idx)) {
            m_startupModeComboBox->setCurrentIndex(idx);
            break;
        }
    }
}

static QString startupModeName(GdbServerProvider::StartupMode m)
{
    switch (m) {
    case GdbServerProvider::StartupOnNetwork:
        return Tr::tr("Startup in TCP/IP Mode");
    case GdbServerProvider::StartupOnPipe:
        return Tr::tr("Startup in Pipe Mode");
    default:
        return {};
    }
}

void GdbServerProviderConfigWidget::populateStartupModes()
{
    const QSet<GdbServerProvider::StartupMode> modes = static_cast<GdbServerProvider *>(
                m_provider)->supportedStartupModes();
    for (const auto mode : modes)
        m_startupModeComboBox->addItem(startupModeName(mode), mode);
}

FilePath GdbServerProviderConfigWidget::peripheralDescriptionFile() const
{
    return m_peripheralDescriptionFileChooser->filePath();
}

void GdbServerProviderConfigWidget::setPeripheralDescriptionFile(const FilePath &file)
{
    m_peripheralDescriptionFileChooser->setFilePath(file);
}

void GdbServerProviderConfigWidget::setFromProvider()
{
    const auto p = static_cast<GdbServerProvider *>(m_provider);
    setStartupMode(p->startupMode());
    setPeripheralDescriptionFile(p->peripheralDescriptionFile());
}

QString GdbServerProviderConfigWidget::defaultInitCommandsTooltip()
{
    return Tr::tr("Enter GDB commands to reset the board "
                  "and to write the nonvolatile memory.");
}

QString GdbServerProviderConfigWidget::defaultResetCommandsTooltip()
{
    return Tr::tr("Enter GDB commands to reset the hardware. "
                  "The MCU should be halted after these commands.");
}

} // BareMetal::Internal
