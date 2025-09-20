// Copyright (C) 2018 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidrunnerworker.h"

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidtr.h"
#include "androidutils.h"

#include <debugger/debuggeritem.h>
#include <debugger/debuggerkitaspect.h>
#include <debugger/debuggerrunconfigurationaspect.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/environmentaspect.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/qmldebugcommandlinearguments.h>
#include <projectexplorer/runcontrol.h>
#include <projectexplorer/target.h>

#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtkitaspect.h>

#include <solutions/tasking/barrier.h>
#include <solutions/tasking/conditional.h>

#include <utils/hostosinfo.h>
#include <utils/port.h>
#include <utils/qtcprocess.h>
#include <utils/url.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTcpServer>

#include <chrono>

namespace {
static Q_LOGGING_CATEGORY(androidRunWorkerLog, "qtc.android.run.androidrunnerworker", QtWarningMsg)
static const int GdbTempFileMaxCounter = 20;
}

using namespace Debugger;
using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

using namespace std;
using namespace std::chrono_literals;
using namespace std::placeholders;

namespace Android::Internal {

static const QString pidPollingScript = QStringLiteral("while [ -d /proc/%1 ]; do sleep 1; done");
static const QRegularExpression userIdPattern("u(\\d+)_a");

static const std::chrono::milliseconds s_jdbTimeout = 60s;

static const Port s_localJdbServerPort(5038);

static qint64 extractPID(const QString &output, const QString &packageName)
{
    qint64 pid = -1;
    for (const QString &tuple : output.split('\n')) {
        // Make sure to remove null characters which might be present in the provided output
        const QStringList parts = tuple.simplified().remove(QChar('\0')).split(':');
        if (parts.length() == 2 && parts.first() == packageName) {
            pid = parts.last().toLongLong();
            break;
        }
    }
    return pid;
}

static QString lldbServerArch(const QString &androidAbi)
{
    if (androidAbi == ProjectExplorer::Constants::ANDROID_ABI_ARMEABI_V7A)
        return {"arm"};
    if (androidAbi == ProjectExplorer::Constants::ANDROID_ABI_X86)
        return {"i386"};
    if (androidAbi == ProjectExplorer::Constants::ANDROID_ABI_ARM64_V8A)
        return {"aarch64"};
    // Correct for x86_64 and best guess at anything that will evolve:
    return androidAbi; // x86_64
}

static FilePath debugServer(const BuildConfiguration *bc)
{
    // Search suitable lldb-server binary.
    const DebuggerItem *debugger = DebuggerKitAspect::debugger(bc->kit());
    if (!debugger || debugger->command().isEmpty())
        return {};
    // .../ndk/<ndk-version>/toolchains/llvm/prebuilt/<host-arch>/bin/lldb
    const FilePath prebuilt = debugger->command().parentDir().parentDir();
    const QString abiNeedle = lldbServerArch(apkDevicePreferredAbi(bc));

    // The new, built-in LLDB.
    const QDir::Filters dirFilter = HostOsInfo::isWindowsHost() ? QDir::Files
                                                                : QDir::Files|QDir::Executable;
    FilePath lldbServer;
    const auto handleLldbServerCandidate = [&abiNeedle, &lldbServer] (const FilePath &path) {
        if (path.parentDir().fileName() == abiNeedle) {
            lldbServer = path;
            return IterationPolicy::Stop;
        }
        return IterationPolicy::Continue;
    };
    prebuilt.iterateDirectory(handleLldbServerCandidate,
                              {{"lldb-server"}, dirFilter, QDirIterator::Subdirectories});
    return lldbServer;
}

class RunnerStorage
{
public:
    bool isPreNougat() const { return m_glue->apiLevel() > 0 && m_glue->apiLevel() <= 23; }

    CommandLine adbCommand(std::initializer_list<CommandLine::ArgRef> args) const
    {
        CommandLine cmd{AndroidConfig::adbToolPath(), args};
        cmd.prependArgs(adbSelector(m_glue->deviceSerialNumber()));
        return cmd;
    }

    QStringList userArgs() const
    {
        return m_processUser > 0 ? QStringList{"--user", QString::number(m_processUser)} : QStringList{};
    }

    QStringList packageArgs() const
    {
        // run-as <package-name> pwd fails on API 22 so route the pwd through shell.
        return QStringList{"shell", "run-as", m_packageName} + userArgs();
    }

    QString debugPortString() const
    {
        return QString::number(m_glue->runControl()->debugChannel().port());
    }

    void appendStdOut(const QString &data)
    {
        m_glue->runControl()->postMessage(data, StdOutFormat);
    }

    void appendStdErr(const QString &data)
    {
        m_glue->runControl()->postMessage(data, StdErrFormat);
    }

    RunnerInterface *m_glue = nullptr;

    QString m_packageName;
    QString m_packageDir;
    QString m_intentName;
    QStringList m_beforeStartAdbCommands;
    QStringList m_afterFinishAdbCommands;
    QString m_amStartExtraArgs;
    qint64 m_processPID = -1;
    qint64 m_processUser = -1;
    bool m_useCppDebugger = false;
    QmlDebugServicesPreset m_qmlDebugServices;
    QUrl m_qmlServer;
    QString m_extraAppParams;
    Utils::Environment m_extraEnvVars;
    Utils::FilePath m_debugServerPath; // On build device, typically as part of ndk
    bool m_useAppParamsForQmlDebugger = false;
};

static void setupStorage(RunnerStorage *storage, RunnerInterface *glue)
{
    storage->m_glue = glue;
    auto aspect = glue->runControl()->aspectData<Debugger::DebuggerRunConfigurationAspect>();
    const Id runMode = glue->runControl()->runMode();
    const bool debuggingMode = runMode == ProjectExplorer::Constants::DEBUG_RUN_MODE;
    storage->m_useCppDebugger = debuggingMode && aspect->useCppDebugger;
    if (debuggingMode && aspect->useQmlDebugger)
        storage->m_qmlDebugServices = QmlDebuggerServices;
    else if (runMode == ProjectExplorer::Constants::QML_PROFILER_RUN_MODE)
        storage->m_qmlDebugServices = QmlProfilerServices;
    else if (runMode == ProjectExplorer::Constants::QML_PREVIEW_RUN_MODE)
        storage->m_qmlDebugServices = QmlPreviewServices;
    else
        storage->m_qmlDebugServices = NoQmlDebugServices;

    if (storage->m_qmlDebugServices != NoQmlDebugServices) {
        qCDebug(androidRunWorkerLog) << "QML debugging enabled";
        QTcpServer server;
        const bool isListening = server.listen(QHostAddress::LocalHost);
        QTC_ASSERT(isListening,
                   qDebug() << Tr::tr("No free ports available on host for QML debugging."));
        storage->m_qmlServer.setScheme(Utils::urlTcpScheme());
        storage->m_qmlServer.setHost(server.serverAddress().toString());
        storage->m_qmlServer.setPort(server.serverPort());
        qCDebug(androidRunWorkerLog) << "QML server:" << storage->m_qmlServer.toDisplayString();
    }

    BuildConfiguration *bc = glue->runControl()->buildConfiguration();
    storage->m_packageName = packageName(bc);
    storage->m_intentName = storage->m_packageName + '/' + activityName(bc);
    qCDebug(androidRunWorkerLog) << "Intent name:" << storage->m_intentName
                                 << "Package name:" << storage->m_packageName;
    qCDebug(androidRunWorkerLog) << "Device API:" << glue->apiLevel();

    storage->m_extraEnvVars = glue->runControl()->aspectData<EnvironmentAspect>()->environment;
    qCDebug(androidRunWorkerLog).noquote() << "Environment variables for the app"
                                           << storage->m_extraEnvVars.toStringList();

    if (bc->buildType() != BuildConfiguration::BuildType::Release)
        storage->m_extraAppParams = glue->runControl()->commandLine().arguments();

    if (const Store sd = glue->runControl()->settingsData(Constants::ANDROID_AM_START_ARGS);
        !sd.isEmpty()) {
        QTC_CHECK(sd.first().typeId() == QMetaType::QString);
        storage->m_amStartExtraArgs = sd.first().toString();
    }

    if (const Store sd = glue->runControl()->settingsData(Constants::ANDROID_PRESTARTSHELLCMDLIST);
        !sd.isEmpty()) {
        const QVariant &first = sd.first();
        QTC_CHECK(first.typeId() == QMetaType::QStringList);
        const QStringList commands = first.toStringList();
        for (const QString &shellCmd : commands)
            storage->m_beforeStartAdbCommands.append(QString("shell %1").arg(shellCmd));
    }

    if (const Store sd = glue->runControl()->settingsData(Constants::ANDROID_POSTFINISHSHELLCMDLIST);
        !sd.isEmpty()) {
        const QVariant &first = sd.first();
        QTC_CHECK(first.typeId() == QMetaType::QStringList);
        const QStringList commands = first.toStringList();
        for (const QString &shellCmd : commands)
            storage->m_afterFinishAdbCommands.append(QString("shell %1").arg(shellCmd));
    }

    storage->m_debugServerPath = debugServer(bc);
    qCDebug(androidRunWorkerLog).noquote() << "Device Serial:" << glue->deviceSerialNumber()
                                           << ", API level:" << glue->apiLevel()
                                           << ", Extra Start Args:" << storage->m_amStartExtraArgs
                                           << ", Before Start ADB cmds:" << storage->m_beforeStartAdbCommands
                                           << ", After finish ADB cmds:" << storage->m_afterFinishAdbCommands
                                           << ", Debug server path:" << storage->m_debugServerPath;

    QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(bc->kit());
    storage->m_useAppParamsForQmlDebugger = version->qtVersion() >= QVersionNumber(5, 12);
}

static ExecutableItem forceStopRecipe(const Storage<RunnerStorage> &storage)
{
    const auto onForceStopSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({"shell", "am", "force-stop", storage->m_packageName}));
    };

    const auto pidCheckSync = Sync([storage] { return storage->m_processPID != -1; });

    const auto onPidOfSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({"shell", "pidof", storage->m_packageName}));
    };
    const auto onPidOfDone = [storage](const Process &process) {
        const QString pid = process.cleanedStdOut().trimmed();
        return pid == QString::number(storage->m_processPID);
    };
    const auto pidOfTask = ProcessTask(onPidOfSetup, onPidOfDone, CallDoneIf::Success);

    const auto onRunAsSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({"shell", "run-as", storage->m_packageName, "kill", "-9",
                                       QString::number(storage->m_processPID)}));
    };
    const auto runAsTask = ProcessTask(onRunAsSetup);

    const auto onKillSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({"shell", "kill", "-9",
                                                QString::number(storage->m_processPID)}));
    };

    return Group {
        ProcessTask(onForceStopSetup) || successItem,
        If (pidCheckSync && pidOfTask && !runAsTask) >> Then {
            ProcessTask(onKillSetup) || successItem
        }
    };
}

static ExecutableItem removeForwardPortRecipe(RunnerStorage *storage, const QString &port,
                                              const QString &adbArg, const QString &portType)
{
    const auto onForwardListSetup = [](Process &process) {
        process.setCommand({AndroidConfig::adbToolPath(), {"forward", "--list"}});
    };
    const auto onForwardListDone = [port](const Process &process) {
        return process.cleanedStdOut().trimmed().contains(port);
    };

    const auto onForwardRemoveSetup = [storage, port](Process &process) {
        process.setCommand(storage->adbCommand({"forward", "--remove", port}));
    };
    const auto onForwardRemoveDone = [storage](const Process &process) {
        emit storage->appendStdErr(process.cleanedStdErr().trimmed());
        return true;
    };

    const auto onForwardPortSetup = [storage, port, adbArg](Process &process) {
        process.setCommand(storage->adbCommand({"forward", port, adbArg}));
    };
    const auto onForwardPortDone = [storage, port, portType](DoneWith result) {
        if (result == DoneWith::Success) {
            storage->m_afterFinishAdbCommands.push_back("forward --remove " + port);
        } else {
            //: %1 = QML/JDB/C++
            emit storage->m_glue->finished(Tr::tr("Failed to forward %1 debugging ports.").arg(portType));
        }
    };

    return Group {
        If (ProcessTask(onForwardListSetup, onForwardListDone)) >> Then {
            ProcessTask(onForwardRemoveSetup, onForwardRemoveDone, CallDoneIf::Error)
        },
        ProcessTask(onForwardPortSetup, onForwardPortDone)
    };
}

// The startBarrier is passed when logcat process received "Sending WAIT chunk" message.
// The settledBarrier is passed when logcat process received "debugger has settled" message.
static ExecutableItem jdbRecipe(const Storage<RunnerStorage> &storage,
                                const SingleBarrier &startBarrier,
                                const SingleBarrier &settledBarrier)
{
    const auto onSetup = [storage] {
        return storage->m_useCppDebugger ? SetupResult::Continue : SetupResult::StopWithSuccess;
    };

    const auto onTaskTreeSetup = [storage](TaskTree &taskTree) {
        taskTree.setRecipe({removeForwardPortRecipe(storage.activeStorage(),
                            "tcp:" + s_localJdbServerPort.toString(),
                            "jdwp:" + QString::number(storage->m_processPID), "JDB")
        });
    };

    const auto onJdbSetup = [settledBarrier](Process &process) {
        const FilePath jdbPath = AndroidConfig::openJDKLocation().pathAppended("bin/jdb")
                                     .withExecutableSuffix();
        const QString portArg = QString("com.sun.jdi.SocketAttach:hostname=localhost,port=%1")
                                    .arg(s_localJdbServerPort.toString());
        process.setCommand({jdbPath, {"-connect", portArg}});
        process.setProcessMode(ProcessMode::Writer);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setReaperTimeout(s_jdbTimeout);
        QObject::connect(settledBarrier->barrier(), &Barrier::done, &process, [processPtr = &process] {
            processPtr->write("ignore uncaught java.lang.Throwable\n"
                              "threads\n"
                              "cont\n"
                              "exit\n");
        });
    };
    const auto onJdbDone = [](const Process &process, DoneWith result) {
        qCDebug(androidRunWorkerLog) << qPrintable(process.allOutput());
        if (result == DoneWith::Cancel)
            qCCritical(androidRunWorkerLog) << "Terminating JDB due to timeout";
    };

    return Group {
        onGroupSetup(onSetup),
        waitForBarrierTask(startBarrier),
        TaskTreeTask(onTaskTreeSetup),
        ProcessTask(onJdbSetup, onJdbDone).withTimeout(60s)
    };
}

static ExecutableItem logcatRecipe(const Storage<RunnerStorage> &storage)
{
    struct Buffer {
        QStringList timeArgs;
        QByteArray stdOutBuffer;
        QByteArray stdErrBuffer;
    };

    const Storage<Buffer> bufferStorage;
    const SingleBarrier startJdbBarrier;   // When logcat received "Sending WAIT chunk".
    const SingleBarrier settledJdbBarrier; // When logcat received "debugger has settled".

    const auto onTimeSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({"shell", "date", "+%s"}));
    };
    const auto onTimeDone = [bufferStorage](const Process &process) {
        bufferStorage->timeArgs = {"-T", QDateTime::fromSecsSinceEpoch(
            process.cleanedStdOut().trimmed().toInt()).toString("MM-dd hh:mm:ss.mmm")};
    };

    const auto onLogcatSetup = [storage, bufferStorage, startJdbBarrier, settledJdbBarrier](Process &process) {
        RunnerStorage *storagePtr = storage.activeStorage();
        Buffer *bufferPtr = bufferStorage.activeStorage();
        const auto parseLogcat = [storagePtr, bufferPtr, start = startJdbBarrier->barrier(),
                                  settled = settledJdbBarrier->barrier(), processPtr = &process](
                                     QProcess::ProcessChannel channel) {
            if (storagePtr->m_processPID == -1)
                return;

            QByteArray &buffer = channel == QProcess::StandardOutput ? bufferPtr->stdOutBuffer
                                                                     : bufferPtr->stdErrBuffer;
            const QByteArray &text = channel == QProcess::StandardOutput
                                         ? processPtr->readAllRawStandardOutput()
                                         : processPtr->readAllRawStandardError();
            QList<QByteArray> lines = text.split('\n');
            // lines always contains at least one item
            lines[0].prepend(buffer);
            if (lines.last().endsWith('\n'))
                buffer.clear();
            else
                buffer = lines.takeLast(); // incomplete line

            const QString pidString = QString::number(storagePtr->m_processPID);
            for (const QByteArray &msg : std::as_const(lines)) {
                const QString line = QString::fromUtf8(msg).trimmed() + QLatin1Char('\n');
                // Get type excluding the initial color characters
                const QString msgType = line.mid(5, 2);
                const bool isFatal = msgType == "F/";
                if (!line.contains(pidString) && !isFatal)
                    continue;

                if (storagePtr->m_useCppDebugger) {
                    if (start->current() == 0 && msg.indexOf("Sending WAIT chunk") > 0)
                        start->advance();
                    else if (settled->current() == 0 && msg.indexOf("debugger has settled") > 0)
                        settled->advance();
                }

                static const QRegularExpression regExpLogcat{
                    "^\\x1B\\[[0-9]+m"   // color
                    "\\w/"               // message type
                    ".*"                 // source
                    "(\\(\\s*\\d*\\)):"  // pid           1. capture
                    "\\s*"
                    ".*"                 // message
                    "\\x1B\\[[0-9]+m"    // color
                    "[\\n\\r]*$"
                };

                static QStringList errorMsgTypes{"W/", "E/", "F/"};
                const bool onlyError = channel == QProcess::StandardError;
                const QRegularExpressionMatch match = regExpLogcat.match(line);
                if (match.hasMatch()) {
                    const QString pidMatch = match.captured(1);
                    const QString cleanPidMatch = pidMatch.mid(1, pidMatch.size() - 2).trimmed();
                    const QString output = QString(line).remove(pidMatch);
                    if (isFatal) {
                        emit storagePtr->appendStdErr(output);
                    } else if (cleanPidMatch == pidString) {
                        if (onlyError || errorMsgTypes.contains(msgType))
                            emit storagePtr->appendStdErr(output);
                        else
                            emit storagePtr->appendStdOut(output);
                    }
                } else {
                    if (onlyError || errorMsgTypes.contains(msgType))
                        emit storagePtr->appendStdErr(line);
                    else
                        emit storagePtr->appendStdOut(line);
                }
            }
        };
        QObject::connect(&process, &Process::readyReadStandardOutput, &process, [parseLogcat] {
            parseLogcat(QProcess::StandardOutput);
        });
        QObject::connect(&process, &Process::readyReadStandardError, &process, [parseLogcat] {
            parseLogcat(QProcess::StandardError);
        });
        process.setCommand(storage->adbCommand({"logcat", "-v", "color", "-v", "brief",
                                                bufferStorage->timeArgs}));
    };

    return Group {
        parallel,
        startJdbBarrier,
        settledJdbBarrier,
        Group {
            bufferStorage,
            ProcessTask(onTimeSetup, onTimeDone, CallDoneIf::Success) || successItem,
            ProcessTask(onLogcatSetup)
        },
        jdbRecipe(storage, startJdbBarrier, settledJdbBarrier)
    };
}

static ExecutableItem preStartRecipe(const Storage<RunnerStorage> &storage)
{
    const Storage<CommandLine> cmdStorage;
    const LoopUntil iterator([storage](int iteration) {
        return iteration < storage->m_beforeStartAdbCommands.size();
    });

    const auto onArgsSetup = [storage, cmdStorage] {
        *cmdStorage = storage->adbCommand({"shell", "am", "start", "-n", storage->m_intentName});
        if (storage->m_useCppDebugger)
            *cmdStorage << "-D";
    };

    const auto onPreCommandSetup = [storage, iterator](Process &process) {
        process.setCommand(storage->adbCommand(
            {storage->m_beforeStartAdbCommands.at(iterator.iteration()).split(' ', Qt::SkipEmptyParts)}));
    };
    const auto onPreCommandDone = [storage](const Process &process) {
        emit storage->appendStdErr(process.cleanedStdErr().trimmed());
    };

    const auto isQmlDebug = [storage] {
        return storage->m_qmlDebugServices != NoQmlDebugServices;
    };
    const auto onTaskTreeSetup = [storage](TaskTree &taskTree) {
        const QString port = "tcp:" + QString::number(storage->m_qmlServer.port());
        taskTree.setRecipe({removeForwardPortRecipe(storage.activeStorage(), port, port, "QML")});
    };
    const auto onQmlDebugSync = [storage, cmdStorage] {
        const QString qmljsdebugger = QString("port:%1,block,services:%2")
            .arg(storage->m_qmlServer.port()).arg(qmlDebugServices(storage->m_qmlDebugServices));

        if (storage->m_useAppParamsForQmlDebugger) {
            if (!storage->m_extraAppParams.isEmpty())
                storage->m_extraAppParams.prepend(' ');
            storage->m_extraAppParams.prepend("-qmljsdebugger=" + qmljsdebugger);
        } else {
            *cmdStorage << "-e" << "qml_debug" << "true"
                         << "-e" << "qmljsdebugger" << qmljsdebugger;
        }
    };

    const auto onActivitySetup = [storage, cmdStorage](Process &process) {
        cmdStorage->addArgs(storage->m_amStartExtraArgs, CommandLine::Raw);

        if (!storage->m_extraAppParams.isEmpty()) {
            const QByteArray appArgs = storage->m_extraAppParams.toUtf8();
            qCDebug(androidRunWorkerLog).noquote() << "Using application arguments: " << appArgs;
            *cmdStorage << "-e" << "extraappparams" << QString::fromLatin1(appArgs.toBase64());
        }

        if (storage->m_extraEnvVars.hasChanges()) {
            const QByteArray extraEnv = storage->m_extraEnvVars.toStringList().join('\t').toUtf8();
            *cmdStorage << "-e" << "extraenvvars" <<  QString::fromLatin1(extraEnv.toBase64());
        }
        process.setCommand(*cmdStorage);
    };
    const auto onActivityDone = [storage](const Process &process) {
        emit storage->m_glue->finished(
            Tr::tr("Activity Manager error: %1").arg(process.cleanedStdErr().trimmed()));
    };

    return Group {
        cmdStorage,
        onGroupSetup(onArgsSetup),
        For (iterator) >> Do {
            ProcessTask(onPreCommandSetup, onPreCommandDone, CallDoneIf::Error)
        },
        If (isQmlDebug) >> Then {
            TaskTreeTask(onTaskTreeSetup),
            Sync(onQmlDebugSync)
        },
        ProcessTask(onActivitySetup, onActivityDone, CallDoneIf::Error)
    };
}

static ExecutableItem postDoneRecipe(const Storage<RunnerStorage> &storage)
{
    const LoopUntil iterator([storage](int iteration) {
        return iteration < storage->m_afterFinishAdbCommands.size();
    });

    const auto onProcessSetup = [storage, iterator](Process &process) {
        process.setCommand(storage->adbCommand(
            {storage->m_afterFinishAdbCommands.at(iterator.iteration()).split(' ', Qt::SkipEmptyParts)}));
    };

    const auto onDone = [storage] {
        storage->m_processPID = -1;
        storage->m_processUser = -1;
        const QString package = storage->m_packageName;
        const QString message = storage->m_glue->wasCancelled()
                                    ? Tr::tr("Android target \"%1\" terminated.").arg(package)
                                    : Tr::tr("Android target \"%1\" died.").arg(package);
        emit storage->m_glue->finished(message);
    };

    return Group {
        finishAllAndSuccess,
        For (iterator) >> Do {
            ProcessTask(onProcessSetup)
        },
        onGroupDone(onDone)
    };
}

static QString tempDebugServerPath(int count)
{
    static const QString tempDebugServerPathTemplate = "/data/local/tmp/%1";
    return tempDebugServerPathTemplate.arg(count);
}

static ExecutableItem uploadDebugServerRecipe(const Storage<RunnerStorage> &storage,
                                              const QString &debugServerFileName)
{
    const Storage<QString> tempDebugServerPathStorage;
    const LoopUntil iterator([tempDebugServerPathStorage](int iteration) {
        return tempDebugServerPathStorage->isEmpty() && iteration <= GdbTempFileMaxCounter;
    });
    const auto onDeviceFileExistsSetup = [storage, iterator](Process &process) {
        process.setCommand(
            storage->adbCommand({"shell", "ls", tempDebugServerPath(iterator.iteration()), "2>/dev/null"}));
    };
    const auto onDeviceFileExistsDone = [iterator, tempDebugServerPathStorage](
                                            const Process &process, DoneWith result) {
        if (result == DoneWith::Error || process.stdOut().trimmed().isEmpty())
            *tempDebugServerPathStorage = tempDebugServerPath(iterator.iteration());
        return true;
    };
    const auto onTempDebugServerPath = [tempDebugServerPathStorage] {
        if (tempDebugServerPathStorage->isEmpty()) {
            qCDebug(androidRunWorkerLog) << "Can not get temporary file name";
            return false;
        }
        return true;
    };

    const auto onCleanupSetup = [storage, tempDebugServerPathStorage](Process &process) {
        process.setCommand(storage->adbCommand({"shell", "rm", "-f", *tempDebugServerPathStorage}));
    };
    const auto onCleanupDone = [] {
        qCDebug(androidRunWorkerLog) << "Debug server cleanup failed.";
    };

    const auto onServerUploadSetup = [storage, tempDebugServerPathStorage](Process &process) {
        process.setCommand(storage->adbCommand(
            {"push", storage->m_debugServerPath.path(), *tempDebugServerPathStorage}));
    };

    const auto onServerCopySetup = [storage, tempDebugServerPathStorage, debugServerFileName](Process &process) {
        process.setCommand(storage->adbCommand({storage->packageArgs(), "cp",
                                                *tempDebugServerPathStorage, debugServerFileName}));
    };

    const auto onServerChmodSetup = [storage, debugServerFileName](Process &process) {
        process.setCommand(storage->adbCommand({storage->packageArgs(), "chmod", "777", debugServerFileName}));
    };

    const auto onDebugSetupFinished = [storage] {
        storage->m_glue->runControl()->setQmlChannel(storage->m_qmlServer);
        emit storage->m_glue->started(storage->m_processPID, storage->m_packageDir);
    };

    return Group {
        tempDebugServerPathStorage,
        For (iterator) >> Do {
            ProcessTask(onDeviceFileExistsSetup, onDeviceFileExistsDone)
        },
        Sync(onTempDebugServerPath),
        If (!ProcessTask(onServerUploadSetup)) >> Then {
            Sync([] { qCDebug(androidRunWorkerLog) << "Debug server upload to temp directory failed"; }),
            ProcessTask(onCleanupSetup, onCleanupDone, CallDoneIf::Error) && errorItem
        },
        If (!ProcessTask(onServerCopySetup)) >> Then {
            Sync([] { qCDebug(androidRunWorkerLog) << "Debug server copy from temp directory failed"; }),
            ProcessTask(onCleanupSetup, onCleanupDone, CallDoneIf::Error) && errorItem
        },
        If (!ProcessTask(onServerChmodSetup)) >> Then {
            Sync([] { qCDebug(androidRunWorkerLog) << "Debug server chmod failed"; }),
            ProcessTask(onCleanupSetup, onCleanupDone, CallDoneIf::Error) && errorItem
        },
        ProcessTask(onCleanupSetup, onCleanupDone, CallDoneIf::Error) || successItem,
        Sync(onDebugSetupFinished)
    };
}

static ExecutableItem startNativeDebuggingRecipe(const Storage<RunnerStorage> &storage)
{
    const auto onSetup = [storage] {
        return storage->m_useCppDebugger ? SetupResult::Continue : SetupResult::StopWithSuccess;
    };

    const Storage<QString> debugServerFileStorage;

    const auto onAppDirSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({storage->packageArgs(), "/system/bin/sh", "-c", "pwd"}));
    };
    const auto onAppDirDone = [storage](const Process &process, DoneWith result) {
        if (result == DoneWith::Success)
            storage->m_packageDir = process.stdOut().trimmed();
        else
            emit storage->m_glue->finished(Tr::tr("Failed to find application directory."));
    };

    // Add executable flag to package dir. Gdb can't connect to running server on device on
    // e.g. on Android 8 with NDK 10e
    const auto onChmodSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand({storage->packageArgs(), "chmod", "a+x", storage->m_packageDir.trimmed()}));
    };
    const auto onServerPathCheck = [storage] {
        if (storage->m_debugServerPath.exists())
            return true;
        const QString msg = Tr::tr("Cannot find C++ debug server in NDK installation.") + "\n" +
                            Tr::tr("The lldb-server binary has not been found.");
        emit storage->m_glue->finished(msg);
        return false;
    };

    const auto killAll = [storage](const QString &name) {
        return ProcessTask([storage, name](Process &process) {
                   process.setCommand(storage->adbCommand({storage->packageArgs(), "killall", name}));
               }) || successItem;
    };

    const auto uploadDebugServer = [storage, debugServerFileStorage](const QString &debugServerFileName) {
        return If (uploadDebugServerRecipe(storage, debugServerFileName)) >> Then {
            Sync([debugServerFileStorage, debugServerFileName] { *debugServerFileStorage = debugServerFileName; })
        } >> Else {
            Sync([storage] {
                emit storage->m_glue->finished(Tr::tr("Cannot copy C++ debug server."));
                return false;
            })
        };
    };

    const auto onRemoveDebugSocketSetup = [storage](Process &process) {
        const QString serverSocket = storage->m_packageDir + "/debug-socket";
        process.setCommand(storage->adbCommand({storage->packageArgs(), "rm", serverSocket}));
    };

    const auto onDebugServerSetup = [storage, debugServerFileStorage](Process &process) {
         const QString serverSocket = storage->m_packageDir + "/debug-socket";
        process.setCommand(storage->adbCommand(
            {storage->packageArgs(), *debugServerFileStorage, "platform",
             "--listen", QString("unix-abstract://%1").arg(serverSocket)}));
    };

    return Group {
        debugServerFileStorage,
        onGroupSetup(onSetup),
        ProcessTask(onAppDirSetup, onAppDirDone),
        ProcessTask(onChmodSetup) || successItem,
        Sync(onServerPathCheck),
        killAll("lldb-server"),
        uploadDebugServer("./lldb-server"),
        ProcessTask(onRemoveDebugSocketSetup) || successItem,
        ProcessTask(onDebugServerSetup)
    };
}

static ExecutableItem pidRecipe(const Storage<RunnerStorage> &storage)
{
    const auto onPidSetup = [storage](Process &process) {
        const QString pidScript = storage->isPreNougat()
            ? QString("for p in /proc/[0-9]*; do cat <$p/cmdline && echo :${p##*/}; done")
            : QString("pidof -s '%1'").arg(storage->m_packageName);
        process.setCommand(storage->adbCommand({"shell", pidScript}));
    };
    const auto onPidDone = [storage](const Process &process) {
        const QString out = process.allOutput();
        if (storage->isPreNougat())
            storage->m_processPID = extractPID(out, storage->m_packageName);
        else if (!out.isEmpty())
            storage->m_processPID = out.trimmed().toLongLong();
    };

    const auto onUserSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand(
            {"shell", "ps", "-o", "user", "-p", QString::number(storage->m_processPID)}));
    };
    const auto onUserDone = [storage](const Process &process) {
        const QString out = process.allOutput();
        if (out.isEmpty())
            return DoneResult::Error;

        QRegularExpressionMatch match;
        qsizetype matchPos = out.indexOf(userIdPattern, 0, &match);
        if (matchPos >= 0 && match.capturedLength(1) > 0) {
            bool ok = false;
            const qint64 processUser = match.captured(1).toInt(&ok);
            if (ok) {
                storage->m_processUser = processUser;
                qCDebug(androidRunWorkerLog) << "Process ID changed to:" << storage->m_processPID;
                if (!storage->m_useCppDebugger) {
                    storage->m_glue->runControl()->setQmlChannel(storage->m_qmlServer);
                    emit storage->m_glue->started(storage->m_processPID, storage->m_packageDir);
                }
                return DoneResult::Success;
            }
        }
        return DoneResult::Error;
    };

    const auto onArtSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand(
            {"shell", "pm", "art", "clear-app-profiles", storage->m_packageName}));
    };
    const auto onArtDone = [storage](const Process &process) {
        if (process.result() == ProcessResult::FinishedWithSuccess)
            emit storage->appendStdOut(Tr::tr("Art: Cleared App Profiles."));
        else
            emit storage->appendStdOut(Tr::tr("Art: Clearing App Profiles failed."));
        return DoneResult::Success;
    };

    const auto onCompileSetup = [storage](Process &process) {
        process.setCommand(storage->adbCommand(
            {"shell", "pm", "compile", "-m", "verify", "-f", storage->m_packageName}));
    };
    const auto onCompileDone = [storage](const Process &process) {
        if (process.result() == ProcessResult::FinishedWithSuccess)
            emit storage->appendStdOut(Tr::tr("Art: Compiled App Profiles."));
        else
            emit storage->appendStdOut(Tr::tr("Art: Compiling App Profiles failed."));
        return DoneResult::Success;
    };

    const auto onIsAliveSetup = [storage](Process &process) {
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setCommand(storage->adbCommand({"shell", pidPollingScript.arg(storage->m_processPID)}));
    };

    // clang-format off
    return Group {
        Forever {
            stopOnSuccess,
            ProcessTask(onPidSetup, onPidDone, CallDoneIf::Success),
            timeoutTask(200ms)
        }.withTimeout(45s),
        ProcessTask(onUserSetup, onUserDone, CallDoneIf::Success),
        ProcessTask(onArtSetup, onArtDone),
        ProcessTask(onCompileSetup, onCompileDone),
        Group {
            parallel,
            startNativeDebuggingRecipe(storage),
            ProcessTask(onIsAliveSetup)
        }
    };
    // clang-format on
}

void RunnerInterface::cancel()
{
    m_wasCancelled = true;
    emit canceled();
}

ExecutableItem runnerRecipe(const Storage<RunnerInterface> &glueStorage)
{
    const Storage<RunnerStorage> storage;

    const auto onSetup = [glueStorage, storage] {
        if (glueStorage->runControl()->buildConfiguration() == nullptr)
            return SetupResult::StopWithError;
        setupStorage(storage.activeStorage(), glueStorage.activeStorage());
        return SetupResult::Continue;
    };

    return Group {
        finishAllAndSuccess,
        storage,
        onGroupSetup(onSetup),
        Group {
            forceStopRecipe(storage),
            Group {
                parallel,
                stopOnSuccessOrError,
                logcatRecipe(storage),
                Group {
                    preStartRecipe(storage),
                    pidRecipe(storage)
                }
            }
        }.withCancel([glueStorage] {
            return std::make_pair(glueStorage.activeStorage(), &RunnerInterface::canceled);
        }),
        forceStopRecipe(storage),
        postDoneRecipe(storage)
    };
}

} // namespace Android::Internal
