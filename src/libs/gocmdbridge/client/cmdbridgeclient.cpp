// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmdbridgeclient.h"

#include "cmdbridgetr.h"

#include <utils/qtcprocess.h>
#include <utils/synchronizedvalue.h>

#include <QCborArray>
#include <QCborMap>
#include <QCborStreamReader>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPromise>
#include <QThread>
#include <QTimer>

Q_LOGGING_CATEGORY(clientLog, "qtc.cmdbridge.client", QtWarningMsg)

#define ASSERT_TYPE(expectedtype) \
    if (map.value("Type").toString() != expectedtype) { \
        const QString err = QString("Unexpected result type: %1, expected: %2") \
                                .arg(map.value("Type").toString(), expectedtype); \
        promise.setException(std::make_exception_ptr(std::runtime_error(err.toStdString()))); \
        promise.finish(); \
        return JobResult::Done; \
    }

using namespace Utils;

namespace CmdBridge {

enum class JobResult {
    Continue,
    Done,
};

namespace Internal {

struct ClientPrivate
{
    FilePath remoteCmdBridgePath;
    Environment environment;

    // Only access from the thread
    Process *process = nullptr;
    QThread *thread = nullptr;
    QTimer *watchDogTimer = nullptr;

    struct Jobs
    {
        int nextId = 0;
        QMap<int, std::function<JobResult(QVariantMap)>> map;
    };

    Utils::SynchronizedValue<Jobs> jobs;

    QMap<int, std::shared_ptr<QPromise<FilePath>>> watchers;

    Result<> readPacket(QCborStreamReader &reader);
    std::optional<Result<>> handleWatchResults(const QVariantMap &map);
};

QString decodeString(QCborStreamReader &reader)
{
    QString result;
    auto r = reader.readString();
    while (r.status == QCborStreamReader::Ok) {
        result += r.data;
        r = reader.readString();
    }

    if (r.status == QCborStreamReader::Error) {
        // handle error condition
        result.clear();
    }
    return result;
}

QByteArray decodeByteArray(QCborStreamReader &reader)
{
    QByteArray result;
    auto r = reader.readByteArray();
    while (r.status == QCborStreamReader::Ok) {
        result += r.data;
        r = reader.readByteArray();
    }

    if (r.status == QCborStreamReader::Error) {
        // handle error condition
        result.clear();
    }
    return result;
}

QVariant simpleToVariant(QCborSimpleType s)
{
    switch (s) {
    case QCborSimpleType::False:
        return false;
    case QCborSimpleType::True:
        return true;
    case QCborSimpleType::Null:
        return {};
    case QCborSimpleType::Undefined:
        return {};
    }
    return {};
}

static QVariant readVariant(QCborStreamReader &reader);

QVariant decodeArray(QCborStreamReader &reader)
{
    QVariantList result;
    reader.enterContainer();
    while (reader.lastError() == QCborError::NoError && reader.hasNext()) {
        result.append(readVariant(reader));
    }
    reader.leaveContainer();
    return result;
}

static QVariant readVariant(QCborStreamReader &reader)
{
    QVariant result;

    switch (reader.type()) {
    case QCborStreamReader::UnsignedInteger:
        result = reader.toUnsignedInteger();
        break;
    case QCborStreamReader::NegativeInteger:
        result = reader.toInteger();
        break;
    case QCborStreamReader::ByteString:
        return decodeByteArray(reader);
    case QCborStreamReader::TextString:
        return decodeString(reader);
    case QCborStreamReader::Array:
        return decodeArray(reader);
    case QCborStreamReader::Map:
        QTC_ASSERT(!"Not implemented", return {});
    case QCborStreamReader::Tag:
        // QCborTag tag = reader.toTag();
        QTC_ASSERT(!"Not implemented", return {});
    case QCborStreamReader::SimpleType:
        result = simpleToVariant(reader.toSimpleType());
        break;
    case QCborStreamReader::HalfFloat: {
        float f;
        qfloat16 qf = reader.toFloat16();
        qFloatFromFloat16(&f, &qf, 1);
        result = f;
        break;
    }
    case QCborStreamReader::Float:
        result = reader.toFloat();
        break;
    case QCborStreamReader::Double:
        result = reader.toDouble();
        break;
    case QCborStreamReader::Invalid:
        QTC_ASSERT(!"Invalid type", return {});
    }
    reader.next();
    return result;
}

std::optional<Result<>> ClientPrivate::handleWatchResults(const QVariantMap &map)
{
    const QString type = map.value("Type").toString();
    if (type == "watchEvent") {
        auto id = map.value("Id").toInt();
        auto it = watchers.find(id);

        if (it == watchers.end())
            return ResultError(QString("No watcher found for id %1").arg(id));

        auto promise = it.value();
        if (!promise->isCanceled())
            promise->addResult(FilePath::fromUserInput(map.value("Path").toString()));

        return Result<>{};
    } else if (type == "removewatchresult") {
        auto id = map.value("Id").toInt();
        watchers.remove(id);
        return Result<>{};
    }

    return std::nullopt;
}

Result<> ClientPrivate::readPacket(QCborStreamReader &reader)
{
    if (!reader.enterContainer())
        return ResultError(QString("The packet did not contain a container"));

    Q_ASSERT(QThread::currentThread() == thread);

    QVariantMap map;

    while (reader.lastError() == QCborError::NoError && reader.hasNext()) {
        auto key = reader.type() == QCborStreamReader::String ? decodeString(reader) : QString();
        map.insert(key, readVariant(reader));
    }

    if (!reader.leaveContainer())
        return ResultError(QString("The packet did not contain a finalized map"));

    if (!map.contains("Id")) {
        return ResultError(QString("The packet did not contain an Id"));
    }

    auto watchHandled = handleWatchResults(map);
    if (watchHandled)
        return *watchHandled;

    auto id = map.value("Id").toInt();
    auto j = jobs.readLocked();
    auto it = j->map.find(id);
    if (it == j->map.end())
        return ResultError(
            QString("No job found for packet with id %1: %2")
                .arg(id)
                .arg(QString::fromUtf8(QJsonDocument::fromVariant(map).toJson())));

    if (it.value()(map) == JobResult::Done) {
        j.unlock();
        auto jw = jobs.writeLocked();
        jw->map.remove(id);
    }

    return {};
}

} // namespace Internal

Client::Client(const Utils::FilePath &remoteCmdBridgePath, const Utils::Environment &env)
    : d(new Internal::ClientPrivate())
{
    d->remoteCmdBridgePath = remoteCmdBridgePath;
    d->environment = env;
}

Client::~Client()
{
    if (d->thread->isRunning() && exit())
        d->thread->wait(2000);
}

Result<> Client::start(bool deleteOnExit)
{
    d->thread = new QThread(this);
    d->thread->setObjectName("CmdBridgeClientThread");
    d->thread->start();

    d->process = new Process();
    d->process->moveToThread(d->thread);

    d->watchDogTimer = new QTimer();
    d->watchDogTimer->setInterval(1000);
    d->watchDogTimer->moveToThread(d->thread);

    connect(d->thread, &QThread::finished, d->watchDogTimer, &QTimer::deleteLater);
    connect(d->watchDogTimer, &QTimer::timeout, d->process, [this] {
        QTC_ASSERT(d->process, return);
        QCborMap args;
        args.insert(QString("Id"), -1);
        args.insert(QString("Type"), QString("ping"));
        d->process->writeRaw(args.toCborValue().toCbor());
    });
    connect(d->process, &Process::started, d->watchDogTimer, qOverload<>(&QTimer::start));

    Result<> result = ResultOk;

    QMetaObject::invokeMethod(
        d->process,
        [this, deleteOnExit]() -> Result<> {
            if (deleteOnExit)
                d->process->setCommand({d->remoteCmdBridgePath, {"-deleteOnExit"}});
            else
                d->process->setCommand({d->remoteCmdBridgePath, {}});
            d->process->setEnvironment(d->environment);
            d->process->setProcessMode(ProcessMode::Writer);
            d->process->setProcessChannelMode(QProcess::ProcessChannelMode::SeparateChannels);
            // Make sure the process has a codec, otherwise it will try to ask us recursively
            // and dead lock.
            d->process->setUtf8Codec();

            connect(d->process, &Process::done, d->process, [this] {
                if (d->process->resultData().m_exitCode != 0) {
                    qCWarning(clientLog)
                        << "Process exited with error code:" << d->process->resultData().m_exitCode
                        << "Error:" << d->process->errorString()
                        << "StandardError:" << d->process->readAllStandardError()
                        << "StandardOutput:" << d->process->readAllStandardOutput();
                }

                auto j = d->jobs.writeLocked();
                for (auto it = j->map.begin(); it != j->map.end();) {
                    auto func = it.value();
                    auto id = it.key();
                    it = j->map.erase(it);
                    func(QVariantMap{
                        {"Type", "error"},
                        {"Id", id},
                        {"Error", QString("Process exited: %1").arg(d->process->errorString())},
                        {"ErrorType", (d->process->exitCode() == 0 ? "NormalExit" : "ErrorExit")}});
                }

                emit done(d->process->resultData());
                d->process->deleteLater();
                d->process = nullptr;
                QThread::currentThread()->quit();
            });

            auto stateMachine =
                [markerOffset = 0, state = int(0), packetSize(0), packetData = QByteArray(), this](
                    QByteArray &buffer) mutable -> bool {
                    static const QByteArray MagicCode{GOBRIDGE_MAGIC_PACKET_MARKER};

                    if (state == 0) {
                        const auto offsetMagicCode = MagicCode.mid(markerOffset);
                        int start = buffer.indexOf(offsetMagicCode);
                        if (start == -1) {
                            if (buffer.size() < offsetMagicCode.size()
                                && offsetMagicCode.startsWith(buffer)) {
                                // Partial magic marker?
                                markerOffset += buffer.size();
                                buffer.clear();
                                return false;
                            }
                            // Broken package, search for next magic marker
                            qCWarning(clientLog)
                                << "Magic marker was not found, buffer content:" << buffer;
                            // If we don't find a magic marker, the rest of the buffer is trash.
                            buffer.clear();
                        } else {
                            buffer.remove(0, start + offsetMagicCode.size());
                            state = 1;
                        }
                        markerOffset = 0;
                    }

                    if (state == 1) {
                        if (buffer.size() < 4)
                            return false; // wait for more data
                        QDataStream ds(buffer);
                        ds >> packetSize;
                        // TODO: Enforce max size in bridge.
                        if (packetSize > 0 && packetSize < 16384) {
                            state = 2;
                            buffer.remove(0, sizeof(packetSize));
                        } else {
                            // Broken package, search for next magic marker
                            qCWarning(clientLog) << "Invalid packet size" << packetSize;
                            state = 0;
                        }
                    }

                    if (state == 2) {
                        auto packetDataRemaining = packetSize - packetData.size();
                        auto dataAvailable = buffer.size();
                        auto availablePacketData = qMin(packetDataRemaining, dataAvailable);
                        packetData.append(buffer.left(availablePacketData));
                        buffer.remove(0, availablePacketData);

                        if (packetData.size() == packetSize) {
                            QCborStreamReader reader;
                            reader.addData(packetData);
                            packetData.clear();
                            state = 0;
                            auto result = d->readPacket(reader);
                            QTC_CHECK_RESULT(result);
                        }
                    }
                    return !buffer.isEmpty();
                };

            connect(
                d->process,
                &Process::readyReadStandardError,
                d->process,
                [this, buffer = QByteArray(), stateMachine]() mutable {
                    buffer.append(d->process->readAllRawStandardError());
                    while (stateMachine(buffer)) {}
                });

            connect(d->process, &Process::readyReadStandardOutput, d->process, [this] {
                qCWarning(clientLog).noquote() << d->process->readAllStandardOutput();
            });

            d->process->start();

            if (!d->process)
                return ResultError(Tr::tr("Failed starting bridge process"));

            if (!d->process->waitForStarted())
                return ResultError(
                    Tr::tr("Failed starting bridge process: %1").arg(d->process->errorString()));
            return ResultOk;
        },
        Qt::BlockingQueuedConnection,
        &result);

    return result;
}

enum class Errors {
    Handle,
    DontHandle,
};

template<class R>
static Utils::Result<QFuture<R>> createJob(
    Internal::ClientPrivate *d,
    QCborMap args,
    const std::function<JobResult(QVariantMap map, QPromise<R> &promise)> &resultFunc,
    Errors handleErrors = Errors::Handle)
{
    if (!d->process || !d->process->isRunning())
        return ResultError(Tr::tr("Bridge process not running"));

    std::shared_ptr<QPromise<R>> promise = std::make_shared<QPromise<R>>();
    QFuture<R> future = promise->future();

    promise->start();

    auto j = d->jobs.writeLocked();
    int id = j->nextId++;
    j->map.insert(id, [handleErrors, promise, resultFunc](QVariantMap map) {
        QString type = map.value("Type").toString();
        if (handleErrors == Errors::Handle && type == "error") {
            const QString err = map.value("Error", QString{}).toString();
            const QString errType = map.value("ErrorType", QString{}).toString();
            if (errType == "ENOENT") {
                promise->setException(
                    std::make_exception_ptr(std::system_error(ENOENT, std::generic_category())));
                promise->finish();
            } else if (errType == "NormalExit") {
                promise->setException(std::make_exception_ptr(std::runtime_error("NormalExit")));
                promise->finish();
            } else {
                qCWarning(clientLog) << "Error (" << errType << "):" << err;
                promise->setException(
                    std::make_exception_ptr(std::runtime_error(err.toStdString())));
                promise->finish();
            }
            return JobResult::Done;
        }

        JobResult result = resultFunc(map, *promise);
        if (result == JobResult::Done)
            promise->finish();

        return result;
    });

    args.insert(QString("Id"), id);

    QMetaObject::invokeMethod(
        d->process,
        [d, args]() {
            QTC_ASSERT(d->process, return);
            d->process->writeRaw(args.toCborValue().toCbor());
        },
        Qt::QueuedConnection);

    return future;
}

static Utils::Result<QFuture<void>> createVoidJob(
    Internal::ClientPrivate *d, const QCborMap &args, const QString &resulttype)
{
    return createJob<void>(d, args, [resulttype](QVariantMap map, QPromise<void> &promise) {
        ASSERT_TYPE(resulttype);
        promise.finish();
        return JobResult::Done;
    });
}

Result<QFuture<Client::ExecResult>> Client::execute(
    const Utils::CommandLine &cmdLine, const Utils::Environment &env, const QByteArray &stdIn)
{
    QCborMap execArgs = QCborMap{
        {"Args",
         QCborArray::fromStringList(
             QStringList() << cmdLine.executable().nativePath() << cmdLine.splitArguments())}};
    if (env.hasChanges())
        execArgs.insert(QCborValue("Env"), QCborArray::fromStringList(env.toStringList()));

    if (!stdIn.isEmpty())
        execArgs.insert(QCborValue("Stdin"), QCborValue(stdIn));

    QCborMap exec{{"Type", "exec"}, {"Exec", execArgs}};

    return createJob<ExecResult>(d.get(), exec, [](QVariantMap map, QPromise<ExecResult> &promise) {
        QString type = map.value("Type").toString();
        if (type == "execdata") {
            QByteArray stdOut = map.value("Stdout").toByteArray();
            QByteArray stdErr = map.value("Stderr").toByteArray();
            promise.addResult(std::make_pair(stdOut, stdErr));
            return JobResult::Continue;
        }

        promise.addResult(map.value("Code").toInt());
        return JobResult::Done;
    });
}

Result<QFuture<Client::FindData>> Client::find(
    const QString &directory, const Utils::FileFilter &filter)
{
    // TODO: golang's walkDir does not support automatically following symlinks.
    if (filter.iteratorFlags.testFlag(QDirIterator::FollowSymlinks))
        return ResultError(Tr::tr("FollowSymlinks is not supported"));

    QCborMap findArgs{
        {"Type", "find"},
        {"Find",
         QCborMap{
             {"Directory", directory},
             {"FileFilters", filter.fileFilters.toInt()},
             {"NameFilters", QCborArray::fromStringList(filter.nameFilters)},
             {"IteratorFlags", filter.iteratorFlags.toInt()}}}};

    return createJob<FindData>(
        d.get(),
        findArgs,
        [hasEntries = false,
         cache = QList<FindData>()](QVariantMap map, QPromise<FindData> &promise) mutable {
            if (promise.isCanceled())
                return JobResult::Done;

            QString type = map.value("Type").toString();
            if (type == "finddata") {
                hasEntries = true;
                FindEntry data;
                data.type = map.value("Type").toString();
                data.id = map.value("Id").toInt();
                data.path = map.value("Path").toString();
                data.size = map.value("Size").toLongLong();
                data.mode = map.value("Mode").toInt();
                data.isDir = map.value("IsDir").toBool();
                data.modTime = QDateTime::fromSecsSinceEpoch(map.value("ModTime").toULongLong());

                cache.append(data);
                if (cache.size() > 1000) {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                    for (const auto &entry : cache)
                        promise.addResult(entry);
#else
                    promise.addResults(cache);
#endif
                    cache.clear();
                }
                return JobResult::Continue;
            } else if (type == "error") {
                hasEntries = true;
                promise.addResult(make_unexpected(map.value("Error", QString{}).toString()));
                return JobResult::Done;
            }

            if (cache.size() > 0) {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                for (const auto &entry : cache)
                    promise.addResult(entry);
#else
                promise.addResults(cache);
#endif
            } else if (!hasEntries) {
                promise.addResult(make_unexpected(std::nullopt));
            }

            return JobResult::Done;
        },
        Errors::DontHandle);
}

Utils::Result<QFuture<QString>> Client::readlink(const QString &path)
{
    return createJob<QString>(
        d.get(),
        QCborMap{{"Type", "readlink"}, {"Path", path}},
        [](const QVariantMap &map, QPromise<QString> &promise) {
            ASSERT_TYPE("readlinkresult");

            promise.addResult(map.value("Target").toString());
            return JobResult::Done;
        });
}

Utils::Result<QFuture<QString>> Client::fileId(const QString &path)
{
    return createJob<QString>(
        d.get(),
        QCborMap{{"Type", "fileid"}, {"Path", path}},
        [](QVariantMap map, QPromise<QString> &promise) {
            ASSERT_TYPE("fileidresult");

            promise.addResult(map.value("FileId").toString());
            return JobResult::Done;
        });
}

Utils::Result<QFuture<quint64>> Client::freeSpace(const QString &path)
{
    return createJob<quint64>(
        d.get(),
        QCborMap{{"Type", "freespace"}, {"Path", path}},
        [](QVariantMap map, QPromise<quint64> &promise) {
            ASSERT_TYPE("freespaceresult");
            promise.addResult(map.value("FreeSpace").toULongLong());
            return JobResult::Done;
        });
}

Utils::Result<QFuture<QByteArray>> Client::readFile(
    const QString &path, qint64 limit, qint64 offset)
{
    return createJob<QByteArray>(
        d.get(),
        QCborMap{
            {"Type", "readfile"},
            {"ReadFile", QCborMap{{"Path", path}, {"Limit", limit}, {"Offset", offset}}}},
        [](QVariantMap map, QPromise<QByteArray> &promise) mutable {
            QString type = map.value("Type").toString();

            if (type == "readfiledata") {
                promise.addResult(map.value("Contents").toByteArray());
                return JobResult::Continue;
            }

            ASSERT_TYPE("readfiledone");
            return JobResult::Done;
        });
}

Utils::Result<QFuture<qint64>> Client::writeFile(
    const QString &path, const QByteArray &contents)
{
    return createJob<qint64>(
        d.get(),
        QCborMap{
            {"Type", "writefile"},
            {"WriteFile",
             QCborMap{
                 {"Path", path},
                 {"Contents", contents},
             }}},
        [](QVariantMap map, QPromise<qint64> &promise) {
            ASSERT_TYPE("writefileresult");
            promise.addResult(map.value("WrittenBytes").toLongLong());
            return JobResult::Done;
        });
}

Utils::Result<QFuture<void>> Client::removeFile(const QString &path)
{
    return createVoidJob(d.get(), QCborMap{{"Type", "remove"}, {"Path", path}}, "removeresult");
}

Utils::Result<QFuture<void>> Client::removeRecursively(const QString &path)
{
    return createVoidJob(d.get(), QCborMap{{"Type", "removeall"}, {"Path", path}}, "removeallresult");
}

Utils::Result<QFuture<void>> Client::ensureExistingFile(const QString &path)
{
    return createVoidJob(
        d.get(),
        QCborMap{{"Type", "ensureexistingfile"}, {"Path", path}},
        "ensureexistingfileresult");
}

Utils::Result<QFuture<void>> Client::createDir(const QString &path)
{
    return createVoidJob(d.get(), QCborMap{{"Type", "createdir"}, {"Path", path}}, "createdirresult");
}

Utils::Result<QFuture<void>> Client::copyFile(const QString &source, const QString &target)
{
    return createVoidJob(
        d.get(),
        QCborMap{
            {"Type", "copyfile"},
            {"CopyFile", QCborMap{{"Source", source}, {"Target", target}}},
        },
        "copyfileresult");
}

Utils::Result<QFuture<void>> Client::renameFile(const QString &source, const QString &target)
{
    return createVoidJob(
        d.get(),
        QCborMap{
            {"Type", "renamefile"},
            {"RenameFile", QCborMap{{"Source", source}, {"Target", target}}},
        },
        "renamefileresult");
}

Utils::Result<QFuture<FilePath>> Client::createTempFile(const QString &path)
{
    return createJob<FilePath>(
        d.get(),
        QCborMap{{"Type", "createtempfile"}, {"Path", path}},
        [](QVariantMap map, QPromise<FilePath> &promise) {
            ASSERT_TYPE("createtempfileresult");

            promise.addResult(FilePath::fromUserInput(map.value("Path").toString()));

            return JobResult::Done;
        });
}

/*
Convert QFileDevice::Permissions to Unix chmod flags.
The mode is copied from system libraries.
The logic is copied from qfiledevice_p.h "toMode_t" function.
*/
constexpr int toUnixChmod(QFileDevice::Permissions permissions)
{
    int mode = 0;
    if (permissions & (QFileDevice::ReadOwner | QFileDevice::ReadUser))
        mode |= 0000400; // S_IRUSR
    if (permissions & (QFileDevice::WriteOwner | QFileDevice::WriteUser))
        mode |= 0000200; // S_IWUSR
    if (permissions & (QFileDevice::ExeOwner | QFileDevice::ExeUser))
        mode |= 0000100; // S_IXUSR
    if (permissions & QFileDevice::ReadGroup)
        mode |= 0000040; // S_IRGRP
    if (permissions & QFileDevice::WriteGroup)
        mode |= 0000020; // S_IWGRP
    if (permissions & QFileDevice::ExeGroup)
        mode |= 0000010; // S_IXGRP
    if (permissions & QFileDevice::ReadOther)
        mode |= 0000004; // S_IROTH
    if (permissions & QFileDevice::WriteOther)
        mode |= 0000002; // S_IWOTH
    if (permissions & QFileDevice::ExeOther)
        mode |= 0000001; // S_IXOTH
    return mode;
}

Utils::Result<QFuture<void>> Client::setPermissions(
    const QString &path, QFile::Permissions perms)
{
    int p = toUnixChmod(perms);

    return createVoidJob(
        d.get(),
        QCborMap{
            {"Type", "setpermissions"}, {"SetPermissions", QCborMap{{"Path", path}, {"Mode", p}}}},
        "setpermissionsresult");
}

class GoFilePathWatcher : public FilePathWatcher
{
    QFutureWatcher<FilePath> m_futureWatcher;

public:
    using Watch = QFuture<FilePath>;

public:
    GoFilePathWatcher(Watch watch)
    {
        connect(&m_futureWatcher, &QFutureWatcher<FilePath>::resultReadyAt, this, [this](int idx) {
            emit pathChanged(m_futureWatcher.resultAt(idx));
        });

        m_futureWatcher.setFuture(watch);
    }

    ~GoFilePathWatcher() override
    {
        m_futureWatcher.disconnect();
        m_futureWatcher.cancel();
    }
};

void Client::stopWatch(int id)
{
    QMetaObject::invokeMethod(d->process, [this, id]() mutable {
        QTC_ASSERT(d->process, return);
        QCborMap stopWatch{{"Type", "stopwatch"}, {"Id", id}};
        d->watchers.remove(id);
        d->process->writeRaw(stopWatch.toCborValue().toCbor());
    });
}

Utils::Result<std::unique_ptr<FilePathWatcher>> Client::watch(const QString &path)
{
    auto jobResult = createJob<GoFilePathWatcher::Watch>(
        d.get(),
        QCborMap{{"Type", "watch"}, {"Path", path}},
        [this](QVariantMap map, QPromise<GoFilePathWatcher::Watch> &promise) {
            ASSERT_TYPE("addwatchresult");

            auto watchPromise = std::make_shared<QPromise<FilePath>>();
            QFuture<FilePath> watchFuture = watchPromise->future();
            watchPromise->start();
            auto watcherId = map.value("Id").toInt();
            d->watchers.insert(watcherId, std::move(watchPromise));

            promise.addResult(watchFuture);

            QFutureWatcher<FilePath> *watcher = new QFutureWatcher<FilePath>();
            connect(watcher, &QFutureWatcher<FilePath>::canceled, this, [this, watcherId, watcher] {
                stopWatch(watcherId);
                watcher->deleteLater();
            });
            connect(this, &QObject::destroyed, watcher, [watcher] { watcher->deleteLater(); });
            watcher->setFuture(watchFuture);
            return JobResult::Done;
        });

    if (!jobResult)
        return ResultError(jobResult.error());

    try {
        return std::make_unique<GoFilePathWatcher>(jobResult->result());
    } catch (const std::exception &e) {
        return ResultError(QString::fromUtf8(e.what()));
    }
}

Utils::Result<QFuture<void>> Client::signalProcess(int pid, Utils::ControlSignal signal)
{
    QString signalString;
    switch (signal) {
    case Utils::ControlSignal::Interrupt:
        signalString = "interrupt";
        break;
    case ControlSignal::Terminate:
        signalString = "terminate";
        break;
    case ControlSignal::Kill:
        signalString = "kill";
        break;
    case ControlSignal::KickOff:
        return ResultError(Tr::tr("Kickoff signal is not supported"));
    case ControlSignal::CloseWriteChannel:
        return ResultError(Tr::tr("CloseWriteChannel signal is not supported"));
    }

    return createVoidJob(
        d.get(),
        QCborMap{{"Type", "signal"}, {"signal", QCborMap{{"Pid", pid}, {"Signal", signalString}}}},
        "signalsuccess");
}

Result<QFuture<QString>> Client::owner(const QString &path)
{
    return createJob<QString>(
        d.get(),
        QCborMap{{"Type", "owner"}, {"Path", path}},
        [](QVariantMap map, QPromise<QString> &promise) {
            ASSERT_TYPE("ownerresult");

            promise.addResult(map.value("Owner").toString());

            return JobResult::Done;
        });
}

Result<QFuture<uint>> Client::ownerId(const QString &path)
{
    return createJob<uint>(
        d.get(),
        QCborMap{{"Type", "ownerid"}, {"Path", path}},
        [](QVariantMap map, QPromise<uint> &promise) {
            ASSERT_TYPE("owneridresult");

            promise.addResult(uint(map.value("OwnerId").toInt()));

            return JobResult::Done;
        });
}

Result<QFuture<QString>> Client::group(const QString &path)
{
    return createJob<QString>(
        d.get(),
        QCborMap{{"Type", "group"}, {"Path", path}},
        [](QVariantMap map, QPromise<QString> &promise) {
            ASSERT_TYPE("groupresult");

            promise.addResult(map.value("Group").toString());

            return JobResult::Done;
        });
}

Result<QFuture<uint>> Client::groupId(const QString &path)
{
    return createJob<uint>(
        d.get(),
        QCborMap{{"Type", "groupid"}, {"Path", path}},
        [](QVariantMap map, QPromise<uint> &promise) {
            ASSERT_TYPE("groupidresult");

            promise.addResult(uint(map.value("GroupId").toInt()));

            return JobResult::Done;
        });
}


bool Client::exit()
{
    try {
        createVoidJob(d.get(), QCborMap{{"Type", "exit"}}, "exitres").and_then([](auto future) {
            future.waitForFinished();
            return Result<>();
        });
        return true;
    } catch (const std::runtime_error &e) {
        if (e.what() == std::string("NormalExit"))
            return true;

        qCWarning(clientLog) << "Client::exit() caught exception:" << e.what();
        return false;
    } catch (const std::exception &e) {
        qCWarning(clientLog) << "Client::exit() caught exception:" << e.what();
        return false;
    } catch (...) {
        qCWarning(clientLog) << "Client::exit() caught unexpected exception";
        return false;
    }
}

Utils::Result<QFuture<Client::Stat>> Client::stat(const QString &path)
{
    return createJob<Stat>(
        d.get(),
        QCborMap{{"Type", "stat"}, {"Stat", QCborMap{{"Path", path}}}},
        [](QVariantMap map, QPromise<Stat> &promise) {
            ASSERT_TYPE("statresult");

            Stat stat;
            stat.size = map.value("Size").toLongLong();
            stat.mode = map.value("Mode").toInt();
            stat.usermode = map.value("UserMode").toUInt();
            stat.modTime = QDateTime::fromSecsSinceEpoch(map.value("ModTime").toULongLong());
            stat.numHardLinks = map.value("NumHardLinks").toInt();
            stat.isDir = map.value("IsDir").toBool();

            promise.addResult(stat);

            return JobResult::Done;
        });
}

Result<QFuture<bool>> Client::is(const QString &path, Is is)
{
    return createJob<bool>(
        d.get(),
        QCborMap{{"Type", "is"}, {"Is", QCborMap{{"Path", path}, {"Check", static_cast<int>(is)}}}},
        [](QVariantMap map, QPromise<bool> &promise) {
            ASSERT_TYPE("isresult");

            promise.addResult(map.value("Result").toBool());

            return JobResult::Done;
        });
}

Result<FilePath> Client::getCmdBridgePath(
    OsType osType, OsArch osArch, const FilePath &libExecPath)
{
    static const QMap<OsType, QString> typeToString = {
        {OsType::OsTypeWindows, QStringLiteral("windows")},
        {OsType::OsTypeLinux, QStringLiteral("linux")},
        {OsType::OsTypeMac, QStringLiteral("darwin")},
        {OsType::OsTypeOtherUnix, QStringLiteral("linux")},
        {OsType::OsTypeOther, QStringLiteral("other")},
    };

    static const QMap<OsArch, QString> archToString = {
        {OsArch::OsArchX86, QStringLiteral("386")},
        {OsArch::OsArchAMD64, QStringLiteral("amd64")},
        {OsArch::OsArchArm, QStringLiteral("arm")},
        {OsArch::OsArchArm64, QStringLiteral("arm64")},
        {OsArch::OsArchUnknown, QStringLiteral("unknown")},
    };

    const QString type = typeToString.value(osType);
    const QString arch = archToString.value(osArch);

    QString cmdBridgeName = QStringLiteral("cmdbridge-%1-%2").arg(type, arch);

    if (osType == OsType::OsTypeWindows)
        cmdBridgeName += QStringLiteral(".exe");

    const FilePath result = libExecPath.resolvePath(cmdBridgeName);
    if (result.exists())
        return result;

    return ResultError(
        QString(Tr::tr("No command bridge found for architecture %1-%2")).arg(type, arch));
}

} // namespace CmdBridge
