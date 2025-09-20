// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangparser.h"
#include "ldparser.h"
#include "lldparser.h"
#include "projectexplorerconstants.h"

using namespace Utils;

namespace ProjectExplorer {

static Task::TaskType taskType(const QString &capture)
{
    const QString lc = capture.toLower();
    if (lc == QLatin1String("error"))
        return Task::Error;
    if (lc == QLatin1String("warning"))
        return Task::Warning;
    return Task::Unknown;
}

// opt. drive letter + filename: (2 brackets)
static const char *const FILE_PATTERN = "(<command line>|([A-Za-z]:)?[^:]+\\.[^:]+)";

ClangParser::ClangParser() :
    m_commandRegExp(QLatin1String("^clang(\\+\\+)?: +(fatal +)?(warning|error|note): (.*)$")),
    m_inLineRegExp(QLatin1String("^In (.*?) included from (.*?):(\\d+):$")),
    m_messageRegExp(QLatin1Char('^') + QLatin1String(FILE_PATTERN) + QLatin1String("(:(\\d+):(\\d+)|\\((\\d+)\\) *): +(fatal +)?(error|warning|note): (.*)$")),
    m_summaryRegExp(QLatin1String("^\\d+ (warnings?|errors?)( and \\d (warnings?|errors?))? generated.$")),
    m_codesignRegExp(QLatin1String("^Code ?Sign error: (.*)$")),
    m_expectSnippet(false)
{
    setObjectName(QLatin1String("ClangParser"));
}

QList<OutputLineParser *> ClangParser::clangParserSuite()
{
    return {new ClangParser, new Internal::LldParser, new Internal::LdParser};
}

OutputLineParser::Result ClangParser::handleLine(const QString &line, OutputFormat type)
{
    if (type != StdErrFormat)
        return Status::NotHandled;
    const QString lne = rightTrimmed(line);
    QRegularExpressionMatch match = m_summaryRegExp.match(lne);
    if (match.hasMatch()) {
        flush();
        m_expectSnippet = false;
        return Status::Done;
    }

    match = m_commandRegExp.match(lne);
    if (match.hasMatch()) {
        m_expectSnippet = true;
        createOrAmendTask(taskType(match.captured(3)), match.captured(4), lne);
        return Status::InProgress;
    }

    match = m_inLineRegExp.match(lne);
    if (match.hasMatch()) {
        m_expectSnippet = true;
        const FilePath filePath = absoluteFilePath(FilePath::fromUserInput(match.captured(2)));
        const int lineNo = match.captured(3).toInt();
        const int column = 0;
        LinkSpecs linkSpecs;
        addLinkSpecForAbsoluteFilePath(linkSpecs, filePath, lineNo, column, match, 2);
        createOrAmendTask(Task::Unknown, lne.trimmed(), lne, false,
                          filePath, lineNo, column, linkSpecs);
        return {Status::InProgress, linkSpecs};
    }

    match = m_messageRegExp.match(lne);
    if (match.hasMatch()) {
        m_expectSnippet = true;
        bool ok = false;
        int lineNo = match.captured(4).toInt(&ok);
        int column = match.captured(5).toInt();
        if (!ok) {
            lineNo = match.captured(6).toInt(&ok);
            column = 0;
        }

        const FilePath filePath = absoluteFilePath(FilePath::fromUserInput(match.captured(1)));
        LinkSpecs linkSpecs;
        addLinkSpecForAbsoluteFilePath(linkSpecs, filePath, lineNo, column, match, 1);
        createOrAmendTask(taskType(match.captured(8)), match.captured(9), lne, false,
                          filePath, lineNo, column, linkSpecs);
        return {Status::InProgress, linkSpecs};
    }

    match = m_codesignRegExp.match(lne);
    if (match.hasMatch()) {
        m_expectSnippet = true;
        createOrAmendTask(Task::Error, match.captured(1), lne, false);
        return Status::InProgress;
    }

    if (m_expectSnippet) {
        createOrAmendTask(Task::Unknown, lne, lne, true);
        return Status::InProgress;
    }

    return Status::NotHandled;
}

Utils::Id ClangParser::id()
{
    return Utils::Id("ProjectExplorer.OutputParser.Clang");
}

} // ProjectExplorer

// Unit tests:

#ifdef WITH_TESTS
#   include <QTest>

#   include "projectexplorer_test.h"
#   include "outputparser_test.h"

namespace ProjectExplorer::Internal {

void ProjectExplorerTest::testClangOutputParser_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<OutputParserTester::Channel>("inputChannel");
    QTest::addColumn<QStringList>("childStdOutLines");
    QTest::addColumn<QStringList>("childStdErrLines");
    QTest::addColumn<Tasks>("tasks");

    auto compileTask = [](Task::TaskType type,
                          const QString &description,
                          const Utils::FilePath &file,
                          int line,
                          int column,
                          const QList<QTextLayout::FormatRange> formats)
    {
        CompileTask task(type, description, file, line, column);
        task.formats = formats;
        return task;
    };

    auto formatRange = [](int start, int length, const QString &anchorHref = QString())
    {
        QTextCharFormat format;
        format.setAnchorHref(anchorHref);

        return QTextLayout::FormatRange{start, length, format};
    };

    QTest::newRow("pass-through stdout")
            << QString::fromLatin1("Sometext") << OutputParserTester::STDOUT
            << QStringList("Sometext") << QStringList()
            << Tasks();

    QTest::newRow("pass-through stderr")
            << QString::fromLatin1("Sometext") << OutputParserTester::STDERR
            << QStringList() << QStringList("Sometext")
            << Tasks();

    QTest::newRow("clang++ warning")
            << QString::fromLatin1("clang++: warning: argument unused during compilation: '-mthreads'")
            << OutputParserTester::STDERR
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "argument unused during compilation: '-mthreads'"));

    QTest::newRow("clang++ error")
            << QString::fromLatin1("clang++: error: no input files [err_drv_no_input_files]")
            << OutputParserTester::STDERR
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "no input files [err_drv_no_input_files]"));

    QTest::newRow("complex warning")
            << QString::fromLatin1("In file included from ..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qnamespace.h:45:\n"
                                   "..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qglobal.h(1425) :  warning: unknown attribute 'dllimport' ignored [-Wunknown-attributes]\n"
                                   "class Q_CORE_EXPORT QSysInfo {\n"
                                   "      ^")
            << OutputParserTester::STDERR
            << QStringList() << QStringList()
            << Tasks{compileTask(
                   Task::Warning,
                   "unknown attribute 'dllimport' ignored [-Wunknown-attributes]\n"
                   "In file included from ..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qnamespace.h:45:\n"
                   "..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qglobal.h(1425) :  warning: unknown attribute 'dllimport' ignored [-Wunknown-attributes]\n"
                   "class Q_CORE_EXPORT QSysInfo {\n"
                   "      ^",
                   FilePath::fromUserInput("..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qglobal.h"),
                   1425, 0,
                   QList<QTextLayout::FormatRange>()
                       << formatRange(61, 278))};

        QTest::newRow("note")
                << QString::fromLatin1("..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qglobal.h:1289:27: note: instantiated from:\n"
                                       "#    define Q_CORE_EXPORT Q_DECL_IMPORT\n"
                                       "                          ^")
                << OutputParserTester::STDERR
                << QStringList() << QStringList()
                << (Tasks()
                    << compileTask(Task::Unknown,
                                   "instantiated from:\n"
                                   "..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qglobal.h:1289:27: note: instantiated from:\n"
                                   "#    define Q_CORE_EXPORT Q_DECL_IMPORT\n"
                                   "                          ^",
                                   FilePath::fromUserInput("..\\..\\..\\QtSDK1.1\\Desktop\\Qt\\4.7.3\\mingw\\include/QtCore/qglobal.h"),
                                   1289, 27,
                                   QList<QTextLayout::FormatRange>()
                                       << formatRange(19, 167)));

        QTest::newRow("fatal error")
                << QString::fromLatin1("/usr/include/c++/4.6/utility:68:10: fatal error: 'bits/c++config.h' file not found\n"
                                       "#include <bits/c++config.h>\n"
                                       "         ^")
                << OutputParserTester::STDERR
                << QStringList() << QStringList()
                << (Tasks()
                    << compileTask(Task::Error,
                                   "'bits/c++config.h' file not found\n"
                                   "/usr/include/c++/4.6/utility:68:10: fatal error: 'bits/c++config.h' file not found\n"
                                   "#include <bits/c++config.h>\n"
                                   "         ^",
                                   FilePath::fromUserInput("/usr/include/c++/4.6/utility"),
                                   68, 10,
                                   QList<QTextLayout::FormatRange>()
                                       << formatRange(34, 0)
                                       << formatRange(34, 28, "olpfile:///usr/include/c++/4.6/utility::68::10")
                                       << formatRange(62, 93)));

        QTest::newRow("line confusion")
                << QString::fromLatin1("/home/code/src/creator/src/plugins/coreplugin/manhattanstyle.cpp:567:51: warning: ?: has lower precedence than +; + will be evaluated first [-Wparentheses]\n"
                                       "            int x = option->rect.x() + horizontal ? 2 : 6;\n"
                                       "                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ^")
                << OutputParserTester::STDERR
                << QStringList() << QStringList()
                << (Tasks()
                    << compileTask(Task::Warning,
                                   "?: has lower precedence than +; + will be evaluated first [-Wparentheses]\n"
                                   "/home/code/src/creator/src/plugins/coreplugin/manhattanstyle.cpp:567:51: warning: ?: has lower precedence than +; + will be evaluated first [-Wparentheses]\n"
                                   "            int x = option->rect.x() + horizontal ? 2 : 6;\n"
                                   "                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ^",
                                   FilePath::fromUserInput("/home/code/src/creator/src/plugins/coreplugin/manhattanstyle.cpp"),
                                   567, 51,
                                   QList<QTextLayout::FormatRange>()
                                       << formatRange(74, 0)
                                       << formatRange(74, 64, "olpfile:///home/code/src/creator/src/plugins/coreplugin/manhattanstyle.cpp::567::51")
                                       << formatRange(138, 202)));

        QTest::newRow("code sign error")
                << QString::fromLatin1("Check dependencies\n"
                                       "Code Sign error: No matching provisioning profiles found: No provisioning profiles with a valid signing identity (i.e. certificate and private key pair) were found.\n"
                                       "CodeSign error: code signing is required for product type 'Application' in SDK 'iOS 7.0'")
                << OutputParserTester::STDERR
                << QStringList() << QStringList("Check dependencies")
                << (Tasks()
                    << CompileTask(Task::Error,
                                   "No matching provisioning profiles found: No provisioning profiles with a valid signing identity (i.e. certificate and private key pair) were found.")
                    << CompileTask(Task::Error,
                                   "code signing is required for product type 'Application' in SDK 'iOS 7.0'"));
}

void ProjectExplorerTest::testClangOutputParser()
{
    OutputParserTester testbench;
    testbench.setLineParsers(ClangParser::clangParserSuite());
    QFETCH(QString, input);
    QFETCH(OutputParserTester::Channel, inputChannel);
    QFETCH(Tasks, tasks);
    QFETCH(QStringList, childStdOutLines);
    QFETCH(QStringList, childStdErrLines);

    testbench.testParsing(input, inputChannel, tasks, childStdOutLines, childStdErrLines);
}

} // ProjectExplorer::Internal

#endif // WITH_TESTS
