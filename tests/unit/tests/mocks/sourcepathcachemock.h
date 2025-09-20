// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../utils/googletest.h"

#include <sourcepathids.h>
#include <sourcepathstorage/sourcepath.h>
#include <sourcepathstorage/sourcepathcacheinterface.h>

class SourcePathCacheMock : public QmlDesigner::SourcePathCacheInterface
{
public:
    virtual ~SourcePathCacheMock() = default;

    QmlDesigner::SourceId createSourceId(QmlDesigner::SourcePathView path);

    MOCK_METHOD(QmlDesigner::SourceId,
                sourceId,
                (QmlDesigner::SourcePathView sourcePath),
                (const, override));
    MOCK_METHOD(QmlDesigner::SourceId,
                sourceId,
                (QmlDesigner::DirectoryPathId directoryPathId, Utils::SmallStringView fileName),
                (const, override));
    MOCK_METHOD(QmlDesigner::FileNameId,
                fileNameId,
                (Utils::SmallStringView fileName),
                (const, override));
    MOCK_METHOD(QmlDesigner::SourcePath,
                sourcePath,
                (QmlDesigner::SourceId sourceId),
                (const, override));
    MOCK_METHOD(QmlDesigner::DirectoryPathId,
                directoryPathId,
                (Utils::SmallStringView directoryPath),
                (const, override));
    MOCK_METHOD(Utils::PathString,
                directoryPath,
                (QmlDesigner::DirectoryPathId directoryPathId),
                (const, override));
    MOCK_METHOD(Utils::SmallString,
                fileName,
                (QmlDesigner::FileNameId fileName),
                (const, override));
    MOCK_METHOD(void, populateIfEmpty, (), (override));
};

class SourcePathCacheMockWithPaths : public SourcePathCacheMock
{
public:
    SourcePathCacheMockWithPaths(QmlDesigner::SourcePathView path);

    QmlDesigner::SourcePath path;
    QmlDesigner::SourceId sourceId;
};
