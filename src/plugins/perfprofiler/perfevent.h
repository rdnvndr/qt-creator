// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "perfeventtype.h"

#include <tracing/traceevent.h>

#include <utils/qtcassert.h>

#include <QDataStream>
#include <QVariant>

namespace PerfProfiler::Internal {

class PerfEvent : public Timeline::TraceEvent
{
public:
    static const qint32 staticClassId = 0x70657266; // 'perf'

    PerfEvent() : TraceEvent(staticClassId) {}

    enum SpecialTypeId {
        AmbiguousTypeId        =  0,
        InvalidTypeId          = -1,
        ThreadStartTypeId      = -2,
        ThreadEndTypeId        = -3,
        LostTypeId             = -4,
        ContextSwitchTypeId    = -5,
        LastSpecialTypeId      = -6
    };

    int numAttributes() const { return m_values.length() + 1; }
    qint32 attributeId(int i) const { return i == 0 ? typeIndex() : m_values[i - 1].first; }
    quint64 attributeValue(int i) const { return i == 0 ? m_value : m_values[i - 1].second; }

    const QList<qint32> &origFrames() const { return m_origFrames; }
    quint8 origNumGuessedFrames() const { return m_origNumGuessedFrames; }

    const QList<qint32> &frames() const { return m_frames; }
    void setFrames(const QList<qint32> &frames) { m_frames = frames; }

    const QHash<qint32, QVariant> &traceData() const { return m_traceData; }

    quint32 pid() const { return m_pid; }
    quint32 tid() const { return m_tid; }

    quint8 numGuessedFrames() const { return m_numGuessedFrames; }
    void setNumGuessedFrames(quint8 numGuessedFrames) { m_numGuessedFrames = numGuessedFrames; }

    quint8 feature() const { return m_feature; }

    quint8 extra() const { return m_extra; }
    void setExtra(quint8 extra) { m_extra = extra; }

private:
    friend QDataStream &operator>>(QDataStream &stream, PerfEvent &event);
    friend QDataStream &operator<<(QDataStream &stream, const PerfEvent &event);

    QList<QPair<qint32, quint64>> m_values;
    QList<qint32> m_origFrames;
    QList<qint32> m_frames;
    QHash<qint32, QVariant> m_traceData;
    quint32 m_pid = 0;
    quint32 m_tid = 0;
    quint64 m_value = 0;
    quint32 m_cpu = 0;
    quint8 m_origNumGuessedFrames = 0;
    quint8 m_numGuessedFrames = 0;
    quint8 m_feature = PerfEventType::InvalidFeature;
    quint8 m_extra = 0;
};

inline QDataStream &operator>>(QDataStream &stream, PerfEvent &event)
{
    stream >> event.m_feature;
    switch (event.m_feature) {
    case PerfEventType::Command:
    case PerfEventType::LocationDefinition:
    case PerfEventType::SymbolDefinition:
    case PerfEventType::AttributesDefinition:
    case PerfEventType::StringDefinition:
    case PerfEventType::FeaturesDefinition:
    case PerfEventType::Error:
    case PerfEventType::Progress:
    case PerfEventType::TracePointFormat:
        return stream; // in fact type data. to be handled elsewhere
    case PerfEventType::ThreadStart:
    case PerfEventType::ThreadEnd:
    case PerfEventType::LostDefinition:
    case PerfEventType::Sample:
    case PerfEventType::TracePointSample:
    case PerfEventType::ContextSwitchDefinition:
        break;
    default:
        qWarning() << "Unrecognized perf event feature" << event.m_feature;
        return stream;
    }

    quint64 timestamp;
    stream >> event.m_pid >> event.m_tid >> timestamp >> event.m_cpu;

    static const quint64 qint64Max = static_cast<quint64>(std::numeric_limits<qint64>::max());
    event.setTimestamp(static_cast<qint64>(qMin(timestamp, qint64Max)));

    switch (event.m_feature) {
    case PerfEventType::ThreadStart:
        event.setTypeIndex(PerfEvent::ThreadStartTypeId);
        break;
    case PerfEventType::ThreadEnd:
        event.setTypeIndex(PerfEvent::ThreadEndTypeId);
        break;
    case PerfEventType::LostDefinition:
        event.setTypeIndex(PerfEvent::LostTypeId);
        break;
    case PerfEventType::ContextSwitchDefinition:
        event.setTypeIndex(PerfEvent::ContextSwitchTypeId);
        bool isSwitchOut;
        stream >> isSwitchOut;
        event.setExtra(isSwitchOut);
        break;
    default: {
        qint32 firstAttributeId;
        stream >> event.m_origFrames >> event.m_origNumGuessedFrames;
        QList<QPair<qint32, quint64>> values;
        stream >> values;
        if (values.isEmpty()) {
            firstAttributeId = PerfEvent::LastSpecialTypeId;
        } else {
            firstAttributeId = PerfEvent::LastSpecialTypeId - values.first().first;
            event.m_value = values.first().second;
            for (auto it = values.constBegin() + 1, end = values.constEnd(); it != end; ++it) {
                event.m_values.push_back({ PerfEvent::LastSpecialTypeId - it->first,
                                           it->second });
            }
        }
        if (event.m_feature == PerfEventType::TracePointSample)
            stream >> event.m_traceData;
        event.setTypeIndex(firstAttributeId);
    }
    }

    return stream;
}

inline QDataStream &operator<<(QDataStream &stream, const PerfEvent &event)
{
    quint8 feature = event.feature();
    stream << feature << event.m_pid << event.m_tid
           << (event.timestamp() < 0ll ? 0ull : static_cast<quint64>(event.timestamp()))
           << event.m_cpu;
    switch (feature) {
    case PerfEventType::ThreadStart:
    case PerfEventType::ThreadEnd:
    case PerfEventType::LostDefinition:
        break;
    case PerfEventType::ContextSwitchDefinition:
        stream << bool(event.extra());
        break;
    case PerfEventType::Sample:
    case PerfEventType::TracePointSample: {
        stream << event.m_origFrames << event.m_origNumGuessedFrames;

        QList<QPair<qint32, quint64>> values;
        for (int i = 0, end = event.numAttributes(); i < end; ++i) {
            values.push_back({ PerfEvent::LastSpecialTypeId - event.attributeId(i),
                               event.attributeValue(i) });
        }
        stream << values;

        if (feature == PerfEventType::TracePointSample)
            stream << event.m_traceData;
        break;
    }
    default:
        QTC_CHECK(false);
    }

    return stream;
}

} // namespace PerfProfiler::Internal
