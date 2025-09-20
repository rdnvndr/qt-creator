// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include "cmakewriter.h"

namespace QmlProjectManager {

namespace QmlProjectExporter {

class CMakeWriterV1 : public CMakeWriter
{
public:
    CMakeWriterV1(CMakeGenerator *parent);

    virtual QString mainLibName() const;

    QString sourceDirName() const override;
    void transformNode(NodePtr &node) const override;

    int identifier() const override;
    void writeRootCMakeFile(const NodePtr &node) const override;
    void writeModuleCMakeFile(const NodePtr &node, const NodePtr &root) const override;
    void writeSourceFiles(const NodePtr &node, const NodePtr &root) const override;

protected:
    void createDependencies(const Utils::FilePath &rootDir) const;
};

} // namespace QmlProjectExporter
} // namespace QmlProjectManager
