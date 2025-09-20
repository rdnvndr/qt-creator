// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extracompiler.h"

#include "buildmanager.h"
#include "environmentkitaspect.h"
#include "projectmanager.h"
#include "target.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/idocument.h>

#include <solutions/tasking/tasktreerunner.h>

#include <utils/async.h>
#include <utils/guard.h>
#include <utils/result.h>
#include <utils/qtcprocess.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QThreadPool>
#include <QTimer>

using namespace Core;
using namespace Tasking;
using namespace Utils;

namespace ProjectExplorer {

Q_GLOBAL_STATIC(QThreadPool, s_extraCompilerThreadPool);
Q_GLOBAL_STATIC(QList<ExtraCompilerFactory *>, factories);
Q_LOGGING_CATEGORY(log, "qtc.projectexplorer.extracompiler", QtWarningMsg);

class ExtraCompilerPrivate
{
public:
    const Project *project;
    FilePath source;
    FileNameToContentsHash contents;
    QDateTime compileTime;
    IEditor *lastEditor = nullptr;
    Guard lock;
    bool dirty = false;
    QTimer timer;
    TaskTreeRunner m_taskTreeRunner;
};

ExtraCompiler::ExtraCompiler(const Project *project, const FilePath &source,
                             const FilePaths &targets, QObject *parent) :
    QObject(parent), d(std::make_unique<ExtraCompilerPrivate>())
{
    d->project = project;
    d->source = source;
    for (const FilePath &target : targets)
        d->contents.insert(target, QByteArray());
    d->timer.setSingleShot(true);

    connect(&d->timer, &QTimer::timeout, this, &ExtraCompiler::compileIfDirty);
    connect(BuildManager::instance(), &BuildManager::buildStateChanged,
            this, &ExtraCompiler::onTargetsBuilt);

    connect(ProjectManager::instance(), &ProjectManager::projectRemoved,
            this, [this](Project *project) {
        if (project == d->project)
            deleteLater();
    });

    EditorManager *editorManager = EditorManager::instance();
    connect(editorManager, &EditorManager::currentEditorChanged,
            this, &ExtraCompiler::onEditorChanged);
    connect(editorManager, &EditorManager::editorAboutToClose,
            this, &ExtraCompiler::onEditorAboutToClose);

    // Use existing target files, where possible. Otherwise run the compiler.
    QDateTime sourceTime = d->source.lastModified();
    for (const FilePath &target : targets) {
        if (!target.exists()) {
            d->dirty = true;
            continue;
        }

        QDateTime lastModified = target.lastModified();
        if (lastModified < sourceTime)
            d->dirty = true;

        if (!d->compileTime.isValid() || d->compileTime > lastModified)
            d->compileTime = lastModified;

        const Result<QByteArray> contents = target.fileContents();
        QTC_ASSERT_RESULT(contents, return);

        setContent(target, *contents);
    }
}

ExtraCompiler::~ExtraCompiler() = default;

const Project *ExtraCompiler::project() const
{
    return d->project;
}

FilePath ExtraCompiler::source() const
{
    return d->source;
}

QByteArray ExtraCompiler::content(const FilePath &file) const
{
    return d->contents.value(file);
}

FilePaths ExtraCompiler::targets() const
{
    return d->contents.keys();
}

void ExtraCompiler::forEachTarget(std::function<void (const FilePath &)> func) const
{
    for (auto it = d->contents.constBegin(), end = d->contents.constEnd(); it != end; ++it)
        func(it.key());
}

void ExtraCompiler::updateCompileTime()
{
    d->compileTime = QDateTime::currentDateTime();
}

QThreadPool *ExtraCompiler::extraCompilerThreadPool()
{
    return s_extraCompilerThreadPool();
}

GroupItem ExtraCompiler::compileFileItem()
{
    return taskItemImpl(fromFileProvider());
}

void ExtraCompiler::compileFile()
{
    compileImpl(fromFileProvider());
}

void ExtraCompiler::compileContent(const QByteArray &content)
{
    compileImpl([content] { return content; });
}

void ExtraCompiler::compileImpl(const ContentProvider &provider)
{
    d->m_taskTreeRunner.start({taskItemImpl(provider)});
}

void ExtraCompiler::compileIfDirty()
{
    qCDebug(log) << Q_FUNC_INFO;
    if (!d->lock.isLocked() && d->dirty && d->lastEditor) {
        qCDebug(log) << '\t' << "about to compile";
        d->dirty = false;
        compileContent(d->lastEditor->document()->contents());
    }
}

ExtraCompiler::ContentProvider ExtraCompiler::fromFileProvider() const
{
    const auto provider = [fileName = source()] {
        QFile file(fileName.toUrlishString());
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return QByteArray();
        return file.readAll();
    };
    return provider;
}

bool ExtraCompiler::isDirty() const
{
    return d->dirty;
}

void ExtraCompiler::block()
{
    qCDebug(log) << Q_FUNC_INFO;
    d->lock.lock();
}

void ExtraCompiler::unblock()
{
    qCDebug(log) << Q_FUNC_INFO;
    d->lock.unlock();
    if (!d->lock.isLocked() && !d->timer.isActive())
        d->timer.start();
}

void ExtraCompiler::onTargetsBuilt(Project *project)
{
    if (project != d->project || BuildManager::isBuilding(project))
        return;

    // This is mostly a fall back for the cases when the generator couldn't be run.
    // It pays special attention to the case where a source file was newly created
    const QDateTime sourceTime = d->source.lastModified();
    if (d->compileTime.isValid() && d->compileTime >= sourceTime)
        return;

    forEachTarget([&](const FilePath &target) {
        QFileInfo fi(target.toFileInfo());
        QDateTime generateTime = fi.exists() ? fi.lastModified() : QDateTime();
        if (generateTime.isValid() && (generateTime > sourceTime)) {
            if (d->compileTime >= generateTime)
                return;

            const Result<QByteArray> contents = target.fileContents();
            QTC_ASSERT_RESULT(contents, return);

            d->compileTime = generateTime;
            setContent(target, *contents);
        }
    });
}

void ExtraCompiler::onEditorChanged(IEditor *editor)
{
    // Handle old editor
    if (d->lastEditor) {
        IDocument *doc = d->lastEditor->document();
        disconnect(doc, &IDocument::contentsChanged,
                   this, &ExtraCompiler::setDirty);

        if (d->dirty) {
            d->dirty = false;
            compileContent(doc->contents());
        }
    }

    if (editor && editor->document()->filePath() == d->source) {
        d->lastEditor = editor;

        // Handle new editor
        connect(d->lastEditor->document(), &IDocument::contentsChanged,
                this, &ExtraCompiler::setDirty);
    } else {
        d->lastEditor = nullptr;
    }
}

void ExtraCompiler::setDirty()
{
    d->dirty = true;
    d->timer.start(1000);
}

void ExtraCompiler::onEditorAboutToClose(IEditor *editor)
{
    if (d->lastEditor != editor)
        return;

    // Oh no our editor is going to be closed
    // get the content first
    IDocument *doc = d->lastEditor->document();
    disconnect(doc, &IDocument::contentsChanged,
               this, &ExtraCompiler::setDirty);
    if (d->dirty) {
        d->dirty = false;
        compileContent(doc->contents());
    }
    d->lastEditor = nullptr;
}

Environment ExtraCompiler::buildEnvironment() const
{
    if (BuildConfiguration *bc = project()->activeBuildConfiguration())
        return bc->environment();

    const EnvironmentItems changes = EnvironmentKitAspect::buildEnvChanges(project()->activeKit());
    Environment env = Environment::systemEnvironment();
    env.modify(changes);
    return env;
}

void ExtraCompiler::setContent(const FilePath &file, const QByteArray &contents)
{
    qCDebug(log).noquote() << Q_FUNC_INFO << contents;
    auto it = d->contents.find(file);
    if (it != d->contents.end()) {
        if (it.value() != contents) {
            it.value() = contents;
            emit contentsChanged(file);
        }
    }
}

ExtraCompilerFactory::ExtraCompilerFactory()
{
    factories->append(this);
}

ExtraCompilerFactory::~ExtraCompilerFactory()
{
    factories->removeAll(this);
}

QList<ExtraCompilerFactory *> ExtraCompilerFactory::extraCompilerFactories()
{
    return *factories();
}

ProcessExtraCompiler::ProcessExtraCompiler(const Project *project, const FilePath &source,
                                           const FilePaths &targets, QObject *parent) :
    ExtraCompiler(project, source, targets, parent)
{ }

GroupItem ProcessExtraCompiler::taskItemImpl(const ContentProvider &provider)
{
    const auto onSetup = [this, provider](Async<FileNameToContentsHash> &async) {
        async.setThreadPool(extraCompilerThreadPool());
        // The passed synchronizer has cancelOnWait set to true by default.
        async.setConcurrentCallData(&ProcessExtraCompiler::runInThread, this, command(),
                                    workingDirectory(), arguments(), provider, buildEnvironment());
    };
    const auto onDone = [this](const Async<FileNameToContentsHash> &async) {
        if (!async.isResultAvailable())
            return;
        const FileNameToContentsHash data = async.result();
        if (data.isEmpty())
            return; // There was some kind of error...
        for (auto it = data.constBegin(), end = data.constEnd(); it != end; ++it)
            setContent(it.key(), it.value());
        updateCompileTime();
    };
    return AsyncTask<FileNameToContentsHash>(onSetup, onDone, CallDoneIf::Success);
}

FilePath ProcessExtraCompiler::workingDirectory() const
{
    return {};
}

QStringList ProcessExtraCompiler::arguments() const
{
    return {};
}

bool ProcessExtraCompiler::prepareToRun(const QByteArray &sourceContents)
{
    Q_UNUSED(sourceContents)
    return true;
}

Tasks ProcessExtraCompiler::parseIssues(const QByteArray &stdErr)
{
    Q_UNUSED(stdErr)
    return {};
}

void ProcessExtraCompiler::runInThread(QPromise<FileNameToContentsHash> &promise,
                                       const FilePath &cmd, const FilePath &workDir,
                                       const QStringList &args, const ContentProvider &provider,
                                       const Environment &env)
{
    if (cmd.isEmpty() || !cmd.toFileInfo().isExecutable())
        return;

    const QByteArray sourceContents = provider();
    if (sourceContents.isNull() || !prepareToRun(sourceContents))
        return;

    Process process;

    process.setEnvironment(env);
    if (!workDir.isEmpty())
        process.setWorkingDirectory(workDir);
    process.setCommand({cmd, args});
    process.setWriteData(sourceContents);
    process.start();
    if (!process.waitForStarted())
        return;

    while (!promise.isCanceled()) {
        using namespace std::chrono_literals;
        if (process.waitForFinished(200ms))
            break;
    }

    if (promise.isCanceled())
        return;

    promise.addResult(handleProcessFinished(&process));
}

} // namespace ProjectExplorer
