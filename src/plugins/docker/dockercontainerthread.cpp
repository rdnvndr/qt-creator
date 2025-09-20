// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockercontainerthread.h"

#include "dockertr.h"

#include <utils/qtcprocess.h>

#include <QLoggingCategory>

using namespace Utils;

Q_LOGGING_CATEGORY(dockerThreadLog, "qtc.docker.device.thread", QtWarningMsg);

namespace Docker::Internal {

// THIS OBJECT MAY NEVER KNOW OR CALL ANY OTHER OBJECTS, EXCEPT ITS OWN !!!
class Internal : public QObject
{
public:
    Internal(const DockerContainerThread::Init &init)
        : m_init(init)
    {}

    ~Internal()
    {
        if (m_startProcess && m_startProcess->isRunning()) {
            // Kill instead of stop so we don't wait for the process to finish.
            m_startProcess->kill();
            m_startProcess->waitForFinished();
        }
    }

    Result<QString> start()
    {
        QString containerId;

        if (Result<QString> create = createContainer(); !create)
            return make_unexpected(create.error());
        else
            containerId = *create;

        if (Result<> start = startContainer(); !start)
            return make_unexpected(start.error());

        return containerId;
    }

private:
    Result<QString> createContainer()
    {
        Process createProcess;
        createProcess.setCommand(m_init.createContainerCmd);
        createProcess.runBlocking();

        if (createProcess.result() != ProcessResult::FinishedWithSuccess) {
            return make_unexpected(
                Tr::tr("Failed creating Docker container: %1")
                    .arg(createProcess.verboseExitMessage()));
        }

        m_containerId = createProcess.cleanedStdOut().trimmed();
        if (m_containerId.isEmpty())
            return make_unexpected(
                Tr::tr("Failed creating Docker container. No container ID received."));

        qCDebug(dockerThreadLog) << "ContainerId:" << m_containerId;
        return m_containerId;
    }

    Result<> startContainer()
    {
        using namespace std::chrono_literals;

        Process eventProcess;
        // Start an docker event listener to listen for the container start event
        eventProcess.setCommand(
            {m_init.dockerBinaryPath,
             {"events", "--filter", "event=start", "--filter", "container=" + m_containerId}});
        eventProcess.setProcessMode(ProcessMode::Reader);
        eventProcess.start();
        if (!eventProcess.waitForStarted(5s)) {
            if (eventProcess.state() == QProcess::NotRunning) {
                return ResultError(
                    Tr::tr("Failed starting Docker event listener. Exit code: %1, output: %2")
                        .arg(eventProcess.exitCode())
                        .arg(eventProcess.allOutput()));
            }
        }

        m_startProcess = new Process(this);

        m_startProcess->setCommand(
            {m_init.dockerBinaryPath, {"container", "start", "-a", "-i", m_containerId}});
        m_startProcess->setProcessMode(ProcessMode::Writer);
        m_startProcess->start();
        if (!m_startProcess->waitForStarted(5s)) {
            if (m_startProcess->state() == QProcess::NotRunning) {
                return ResultError(
                    Tr::tr("Failed starting Docker container. Exit code: %1, output: %2")
                        .arg(m_startProcess->exitCode())
                        .arg(m_startProcess->allOutput()));
            }
            // Lets assume it will start soon
            qCWarning(dockerThreadLog)
                << "Docker container start process took more than 5 seconds to start.";
        }
        qCDebug(dockerThreadLog) << "Started container: " << m_startProcess->commandLine();

        // Read a line from the eventProcess
        while (true) {
            if (!eventProcess.waitForReadyRead(5s)) {
                m_startProcess->kill();
                if (!m_startProcess->waitForFinished(5s)) {
                    qCWarning(dockerThreadLog)
                        << "Docker start process took more than 5 seconds to finish.";
                }
                return ResultError(
                    Tr::tr("Failed starting Docker container. Exit code: %1, output: %2")
                        .arg(eventProcess.exitCode())
                        .arg(eventProcess.allOutput()));
            }
            if (!eventProcess.stdOutLines().isEmpty()) {
                break;
            }
        }
        qCDebug(dockerThreadLog) << "Started event received for container: " << m_containerId;
        eventProcess.kill();
        if (!eventProcess.waitForFinished(5s)) {
            qCWarning(dockerThreadLog)
                << "Docker event listener process took more than 5 seconds to finish.";
        }
        return ResultOk;
    }

private:
    DockerContainerThread::Init m_init;
    QString m_containerId;
    Process *m_startProcess = nullptr;
};

DockerContainerThread::DockerContainerThread(Init init)
    : m_internal(new Internal(init))
{
    m_thread.setObjectName("Docker Container Thread");
    m_internal->moveToThread(&m_thread);
    QObject::connect(&m_thread, &QThread::finished, m_internal, &QObject::deleteLater);
    m_thread.start();
}

Result<> DockerContainerThread::start()
{
    Result<QString> result;
    QMetaObject::invokeMethod(m_internal, &Internal::start, Qt::BlockingQueuedConnection, &result);
    if (result) {
        m_containerId = *result;
        return ResultOk;
    }
    return ResultError(result.error());
}

DockerContainerThread::~DockerContainerThread()
{
    m_thread.quit();
    m_thread.wait();
}

QString DockerContainerThread::containerId() const
{
    return m_containerId;
}

Result<std::unique_ptr<DockerContainerThread>> DockerContainerThread::create(const Init &init)
{
    std::unique_ptr<DockerContainerThread> thread(new DockerContainerThread(init));

    if (Result<> result = thread->start(); !result)
        return make_unexpected(result.error());

    return thread;
}

} // namespace Docker::Internal
