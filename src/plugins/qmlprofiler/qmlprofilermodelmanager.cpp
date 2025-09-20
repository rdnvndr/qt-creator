// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlprofilerdetailsrewriter.h"
#include "qmlprofilermodelmanager.h"
#include "qmlprofilernotesmodel.h"
#include "qmlprofilertr.h"
#include "qmlprofilertracefile.h"

#include <coreplugin/progressmanager/progressmanager.h>
#include <tracing/tracestashfile.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStack>

#include <functional>

namespace QmlProfiler {

static const char *ProfileFeatureNames[] = {
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "JavaScript"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Memory Usage"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Pixmap Cache"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Scene Graph"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Animations"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Painting"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Compiling"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Creating"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Binding"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Handling Signal"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Input Events"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Debug Messages"),
    QT_TRANSLATE_NOOP("QtC::QmlProfiler", "Quick3D")
};

Q_STATIC_ASSERT(sizeof(ProfileFeatureNames) == sizeof(char *) * MaximumProfileFeature);

class QmlProfilerEventTypeStorage : public Timeline::TraceEventTypeStorage
{
public:
    const Timeline::TraceEventType &get(int typeId) const override;
    void set(int typeId, Timeline::TraceEventType &&type) override;
    int append(Timeline::TraceEventType &&type) override;
    int size() const override;
    void clear() override;

private:
    std::vector<QmlEventType> m_types;
};

class QmlProfilerEventStorage : public Timeline::TraceEventStorage
{
public:
    using ErrorHandler = std::function<void(const QString &)>;

    QmlProfilerEventStorage(const ErrorHandler &errorHandler);

    int append(Timeline::TraceEvent &&event) override;
    int size() const override;
    void clear() override;
    bool replay(const std::function<bool(Timeline::TraceEvent &&)> &receiver) const override;
    void finalize() override;

    ErrorHandler errorHandler() const;
    void setErrorHandler(const ErrorHandler &errorHandler);

private:
    Timeline::TraceStashFile<QmlEvent> m_file;
    std::function<void(const QString &)> m_errorHandler;
    int m_size = 0;
};

class QmlProfilerModelManager::QmlProfilerModelManagerPrivate
{
public:
    Internal::QmlProfilerTextMarkModel *textMarkModel = nullptr;
    Internal::QmlProfilerDetailsRewriter *detailsRewriter = nullptr;

    bool isRestrictedToRange = false;

    void addEventType(const QmlEventType &eventType);
    void handleError(const QString &message);

    int resolveStackTop();
};

QmlProfilerModelManager::QmlProfilerModelManager(QObject *parent)
    : Timeline::TimelineTraceManager({}, std::make_unique<QmlProfilerEventTypeStorage>(), parent)
    , d(new QmlProfilerModelManagerPrivate)
{
    setNotesModel(new QmlProfilerNotesModel(this));
    d->textMarkModel = new Internal::QmlProfilerTextMarkModel(this);

    d->detailsRewriter = new Internal::QmlProfilerDetailsRewriter(this);
    connect(d->detailsRewriter, &Internal::QmlProfilerDetailsRewriter::rewriteDetailsString,
            this, &QmlProfilerModelManager::setTypeDetails);
    connect(d->detailsRewriter, &Internal::QmlProfilerDetailsRewriter::eventDetailsChanged,
            this, &QmlProfilerModelManager::typeDetailsFinished);
    auto storage = new QmlProfilerEventStorage(QmlProfilerEventStorage::ErrorHandler());
    storage->setErrorHandler([this](const QString &message) { emit error(message); });
    std::unique_ptr<Timeline::TraceEventStorage> storagePtr(storage);
    swapEventStorage(storagePtr);
}

QmlProfilerModelManager::~QmlProfilerModelManager()
{
    delete d;
}

Internal::QmlProfilerTextMarkModel *QmlProfilerModelManager::textMarkModel() const
{
    return d->textMarkModel;
}

void QmlProfilerModelManager::registerFeatures(quint64 features, QmlEventLoader eventLoader,
                                               Initializer initializer, Finalizer finalizer,
                                               Clearer clearer)
{
    const TraceEventLoader traceEventLoader = eventLoader ? [eventLoader](
            const Timeline::TraceEvent &event, const Timeline::TraceEventType &type) {
        QTC_ASSERT(event.is<QmlEvent>(), return);
        QTC_ASSERT(type.is<QmlEventType>(), return);
        eventLoader(event.asConstRef<QmlEvent>(), type.asConstRef<QmlEventType>());
    } : TraceEventLoader();

    Timeline::TimelineTraceManager::registerFeatures(features, traceEventLoader, initializer,
                                                     finalizer, clearer);
}

const QmlEventType &QmlProfilerModelManager::eventType(int typeId) const
{
    static const QmlEventType invalid;
    const Timeline::TraceEventType &type = TimelineTraceManager::eventType(typeId);
    QTC_ASSERT(type.is<QmlEventType>(), return invalid);
    return type.asConstRef<QmlEventType>();
}

void QmlProfilerModelManager::replayEvents(TraceEventLoader loader, Initializer initializer,
                                           Finalizer finalizer, ErrorHandler errorHandler,
                                           QFutureInterface<void> &future) const
{
    replayQmlEvents(static_cast<QmlEventLoader>(loader), initializer, finalizer, errorHandler,
                    future);
}

static bool isStateful(const QmlEventType &type)
{
    // Events of these types carry state that has to be taken into account when adding later events:
    // PixmapCacheEvent: Total size of the cache and size of pixmap currently being loaded
    // MemoryAllocation: Total size of the JS heap and the amount of it currently in use
    const Message message = type.message();
    return message == PixmapCacheEvent || message == MemoryAllocation;
}

void QmlProfilerModelManager::replayQmlEvents(QmlEventLoader loader,
                                              Initializer initializer, Finalizer finalizer,
                                              ErrorHandler errorHandler,
                                              QFutureInterface<void> &future) const
{
    if (initializer)
        initializer();

    const auto result = eventStorage()->replay([&](Timeline::TraceEvent &&event) {
        if (future.isCanceled())
            return false;

        QTC_ASSERT(event.is<QmlEvent>(), return false);
        loader(event.asRvalueRef<QmlEvent>(), eventType(event.typeIndex()));
        return true;
    });

    if (!result && errorHandler) {
        errorHandler(future.isCanceled() ? QString()
                                         : Tr::tr("Failed to replay QML events from stash file."));
    } else if (result && finalizer) {
        finalizer();
    }
}

void QmlProfilerModelManager::initialize()
{
    d->textMarkModel->hideTextMarks();
    TimelineTraceManager::initialize();
}

void QmlProfilerModelManager::clearEventStorage()
{
    TimelineTraceManager::clearEventStorage();
    emit traceChanged();
}

void QmlProfilerModelManager::clearTypeStorage()
{
    d->textMarkModel->clear();
    TimelineTraceManager::clearTypeStorage();
}

static QString getDisplayName(const QmlEventType &event)
{
    if (event.location().filename().isEmpty()) {
        return Tr::tr("<bytecode>");
    } else {
        const QString filePath = QUrl(event.location().filename()).path();
        return filePath.mid(filePath.lastIndexOf(QLatin1Char('/')) + 1) + QLatin1Char(':') +
                QString::number(event.location().line());
    }
}

static QString getInitialDetails(const QmlEventType &event)
{
    QString details = event.data();
    // generate details string
    if (!details.isEmpty()) {
        details = details.replace(QLatin1Char('\n'),QLatin1Char(' ')).simplified();
        if (details.isEmpty()) {
            if (event.rangeType() == Javascript)
                details = Tr::tr("anonymous function");
        } else {
            static const QRegularExpression rewrite("^\\(function \\$(\\w+)\\(\\) \\{ (return |)(.+) \\}\\)$");
            QRegularExpressionMatch match = rewrite.match(details);
            if (match.hasMatch())
                details = match.captured(1) + QLatin1String(": ") + match.captured(3);
            if (details.startsWith(QLatin1String("file://")) ||
                    details.startsWith(QLatin1String("qrc:/")))
                details = details.mid(details.lastIndexOf(QLatin1Char('/')) + 1);
        }
    }

    return details;
}

void QmlProfilerModelManager::QmlProfilerModelManagerPrivate::handleError(const QString &message)
{
    // What to do here?
    qWarning() << message;
}

const char *QmlProfilerModelManager::featureName(ProfileFeature feature)
{
    return ProfileFeatureNames[feature];
}

void QmlProfilerModelManager::finalize()
{
    d->detailsRewriter->reloadDocuments();

    // Load notes after the timeline models have been initialized ...
    // which happens on stateChanged(Done).

    TimelineTraceManager::finalize();
    d->textMarkModel->showTextMarks();
    emit traceChanged();
}

void QmlProfilerModelManager::populateFileFinder(const ProjectExplorer::BuildConfiguration *bc)
{
    d->detailsRewriter->populateFileFinder(bc);
}

Utils::FilePath QmlProfilerModelManager::findLocalFile(const QString &remoteFile)
{
    return d->detailsRewriter->getLocalFile(remoteFile);
}

void QmlProfilerModelManager::setTypeDetails(int typeId, const QString &details)
{
    QTC_ASSERT(typeId < numEventTypes(), return);
    QmlEventType type = eventType(typeId);
    type.setData(details);
    // Don't rewrite the details again, but directly push the type into the type storage.
    Timeline::TimelineTraceManager::setEventType(typeId, std::move(type));
    emit typeDetailsChanged(typeId);
}

void QmlProfilerModelManager::restrictByFilter(QmlProfilerModelManager::QmlEventFilter filter)
{
    return Timeline::TimelineTraceManager::restrictByFilter([filter](TraceEventLoader loader) {
        const auto filteredQmlLoader = filter([loader](const QmlEvent &event,
                                                       const QmlEventType &type) {
            loader(event, type);
        });

        return [filteredQmlLoader](const Timeline::TraceEvent &event,
                                   const Timeline::TraceEventType &type) {
            filteredQmlLoader(static_cast<const QmlEvent &>(event),
                              static_cast<const QmlEventType &>(type));
        };
    });
}

int QmlProfilerModelManager::appendEventType(QmlEventType &&type)
{
    type.setDisplayName(getDisplayName(type));
    type.setData(getInitialDetails(type));

    const QmlEventLocation &location = type.location();
    if (location.isValid()) {
        const RangeType rangeType = type.rangeType();
        const QmlEventLocation localLocation(d->detailsRewriter->getLocalFile(location.filename())
                                                 .toUrlishString(),
                                             location.line(),
                                             location.column());

        // location and type are invalid after this
        const int typeIndex = TimelineTraceManager::appendEventType(std::move(type));

        // Only bindings and signal handlers need rewriting
        if (rangeType == Binding || rangeType == HandlingSignal)
            d->detailsRewriter->requestDetailsForLocation(typeIndex, location);
        d->textMarkModel->addTextMarkId(typeIndex, localLocation);
        return typeIndex;
    } else {
        // There is no point in looking for invalid locations; just add the type
        return TimelineTraceManager::appendEventType(std::move(type));
    }
}

void QmlProfilerModelManager::setEventType(int typeIndex, QmlEventType &&type)
{
    type.setDisplayName(getDisplayName(type));
    type.setData(getInitialDetails(type));

    const QmlEventLocation &location = type.location();
    if (location.isValid()) {
        // Only bindings and signal handlers need rewriting
        if (type.rangeType() == Binding || type.rangeType() == HandlingSignal)
            d->detailsRewriter->requestDetailsForLocation(typeIndex, location);
        d->textMarkModel->addTextMarkId(typeIndex,
                                        QmlEventLocation(d->detailsRewriter
                                                             ->getLocalFile(location.filename())
                                                             .toUrlishString(),
                                                         location.line(),
                                                         location.column()));
    }

    TimelineTraceManager::setEventType(typeIndex, std::move(type));
}


void QmlProfilerModelManager::appendEvent(QmlEvent &&event)
{
    TimelineTraceManager::appendEvent(std::move(event));
}

void QmlProfilerModelManager::restrictToRange(qint64 start, qint64 end)
{
    d->isRestrictedToRange = (start != -1 || end != -1);
    restrictByFilter(rangeFilter(start, end));
}

bool QmlProfilerModelManager::isRestrictedToRange() const
{
    return d->isRestrictedToRange;
}

QmlProfilerModelManager::QmlEventFilter
QmlProfilerModelManager::rangeFilter(qint64 rangeStart, qint64 rangeEnd) const
{
    return [this, rangeStart, rangeEnd] (QmlEventLoader loader) {
        // TODO: It seems that below 2 variables are passed by copy to the lambda body,
        //       thus changing their values inside the lambda body are local to the body only.
        //       Setting "crossedRangeStart = true" inside the lambda looks no-op.
        //       Passing always empty "stack" into the lambda also looks no-op.
        //       Was the intention to pass these variables by reference?
        QStack<QmlEvent> stack;
        bool crossedRangeStart = false;

        return [this, rangeStart, rangeEnd, loader, crossedRangeStart, stack](
                   const QmlEvent &event, const QmlEventType &type) mutable {

            // No restrictions: load all events
            if (rangeStart == -1 || rangeEnd == -1) {
                loader(event, type);
                return true;
            }

            // Double-check if rangeStart has been crossed. Some versions of Qt send dirty data.
            qint64 adjustedTimestamp = event.timestamp();
            if (event.timestamp() < rangeStart && !crossedRangeStart) {
                if (type.rangeType() != UndefinedRangeType) {
                    if (event.rangeStage() == RangeStart)
                        stack.push(event);
                    else if (event.rangeStage() == RangeEnd && !stack.isEmpty())
                        stack.pop();
                    return true;
                } else if (isStateful(type)) {
                    adjustedTimestamp = rangeStart;
                } else {
                    return true;
                }
            } else {
                if (!crossedRangeStart) {
                    for (auto stashed : std::as_const(stack)) {
                        stashed.setTimestamp(rangeStart);
                        loader(stashed, eventType(stashed.typeIndex()));
                    }
                    stack.clear();
                    crossedRangeStart = true;
                }
                if (event.timestamp() > rangeEnd) {
                    if (type.rangeType() != UndefinedRangeType) {
                        if (event.rangeStage() == RangeEnd) {
                            if (stack.isEmpty()) {
                                QmlEvent endEvent(event);
                                endEvent.setTimestamp(rangeEnd);
                                loader(endEvent, type);
                            } else {
                                stack.pop();
                            }
                        } else if (event.rangeStage() == RangeStart) {
                            stack.push(event);
                        }
                        return true;
                    } else if (isStateful(type)) {
                        adjustedTimestamp = rangeEnd;
                    } else {
                        return true;
                    }
                }
            }

            if (adjustedTimestamp != event.timestamp()) {
                QmlEvent adjusted(event);
                adjusted.setTimestamp(adjustedTimestamp);
                loader(adjusted, type);
            } else {
                loader(event, type);
            }
            return true;
        };
    };
}

Timeline::TimelineTraceFile *QmlProfilerModelManager::createTraceFile()
{
    return new Internal::QmlProfilerTraceFile(this);
}

const Timeline::TraceEventType &QmlProfilerEventTypeStorage::get(int typeId) const
{
    Q_ASSERT(typeId >= 0);
    return m_types.at(static_cast<size_t>(typeId));
}

void QmlProfilerEventTypeStorage::set(int typeId, Timeline::TraceEventType &&type)
{
    Q_ASSERT(typeId >= 0);
    const size_t index = static_cast<size_t>(typeId);
    if (m_types.size() <= index)
        m_types.resize(index + 1);
    QTC_ASSERT(type.is<QmlEventType>(), return);
    m_types[index] = std::move(type.asRvalueRef<QmlEventType>());
}

int QmlProfilerEventTypeStorage::append(Timeline::TraceEventType &&type)
{
    const size_t index = m_types.size();
    if (type.is<QmlEventType>()) {
        m_types.push_back(std::move(type.asRvalueRef<QmlEventType>()));
    } else {
        QTC_CHECK(false);
        m_types.emplace_back();
    }
    QTC_ASSERT(index <= static_cast<size_t>(std::numeric_limits<int>::max()),
               return std::numeric_limits<int>::max());
    return static_cast<int>(index);
}

int QmlProfilerEventTypeStorage::size() const
{
    const size_t size = m_types.size();
    QTC_ASSERT(size <= static_cast<size_t>(std::numeric_limits<int>::max()),
               return std::numeric_limits<int>::max());
    return static_cast<int>(size);
}

void QmlProfilerEventTypeStorage::clear()
{
    m_types.clear();
}

QmlProfilerEventStorage::QmlProfilerEventStorage(
        const std::function<void (const QString &)> &errorHandler)
    : m_file("qmlprofiler-data"), m_errorHandler(errorHandler)
{
    if (!m_file.open() && m_errorHandler)
        errorHandler(Tr::tr("Cannot open temporary trace file to store events."));
}

int QmlProfilerEventStorage::append(Timeline::TraceEvent &&event)
{
    QTC_ASSERT(event.is<QmlEvent>(), return m_size);
    m_file.append(std::move(event.asRvalueRef<QmlEvent>()));
    return m_size++;
}

int QmlProfilerEventStorage::size() const
{
    return m_size;
}

void QmlProfilerEventStorage::clear()
{
    m_size = 0;
    m_file.clear();
    if (!m_file.open() && m_errorHandler)
        m_errorHandler(Tr::tr("Failed to reset temporary trace file."));
}

void QmlProfilerEventStorage::finalize()
{
    if (!m_file.flush() && m_errorHandler)
        m_errorHandler(Tr::tr("Failed to flush temporary trace file."));
}

QmlProfilerEventStorage::ErrorHandler QmlProfilerEventStorage::errorHandler() const
{
    return m_errorHandler;
}

void QmlProfilerEventStorage::setErrorHandler(
        const QmlProfilerEventStorage::ErrorHandler &errorHandler)
{
    m_errorHandler = errorHandler;
}

bool QmlProfilerEventStorage::replay(
        const std::function<bool (Timeline::TraceEvent &&)> &receiver) const
{
    switch (m_file.replay(receiver)) {
    case Timeline::TraceStashFile<QmlEvent>::ReplaySuccess:
        return true;
    case Timeline::TraceStashFile<QmlEvent>::ReplayOpenFailed:
        if (m_errorHandler)
            m_errorHandler(Tr::tr("Could not re-open temporary trace file."));
        break;
    case Timeline::TraceStashFile<QmlEvent>::ReplayLoadFailed:
        // Happens if the loader rejects an event. Not an actual error
        break;
    case Timeline::TraceStashFile<QmlEvent>::ReplayReadPastEnd:
        if (m_errorHandler)
            m_errorHandler(Tr::tr("Read past end in temporary trace file."));
        break;
    }
    return false;
}

} // namespace QmlProfiler
