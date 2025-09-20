// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "msvcparser.h"
#include "projectexplorerconstants.h"
#include "buildmanager.h"

#include <utils/qtcassert.h>
#include <utils/fileutils.h>

#include <numeric>

using namespace Utils;

// As of MSVC 2015: "foo.cpp(42) :" -> "foo.cpp(42):"
static const char FILE_POS_PATTERN[] = "^(?:\\d+>)?(cl|LINK|.+?[^ ]) ?: ";

static QPair<FilePath, int> parseFileName(const QString &input)
{
    QString fileName = input;
    if (fileName.startsWith("LINK") || fileName.startsWith("cl"))
        return {{}, -1};

    // Extract linenumber (if it is there):
    int linenumber = -1;
    if (fileName.endsWith(')')) {
        int pos = fileName.lastIndexOf('(');
        if (pos >= 0) {
            // clang-cl gives column, too: "foo.cpp(34,1)" as opposed to MSVC "foo.cpp(34)".
            int endPos = fileName.indexOf(',', pos + 1);
            if (endPos < 0)
                endPos = fileName.size() - 1;
            bool ok = false;
            const int n = fileName.mid(pos + 1, endPos - pos - 1).toInt(&ok);
            if (ok) {
                fileName = fileName.left(pos);
                linenumber = n;
            }
        }
    }
    const QString normalized = FileUtils::normalizedPathName(fileName);
    return {FilePath::fromUserInput(normalized), linenumber};
}

using namespace ProjectExplorer;

// nmake/jom messages.
static Task handleNmakeJomMessage(const QString &line)
{
    static const QRegularExpression qmllint(
        "^(Warning|Error): ((.*):(\\d+):(\\d+)): .* ([[].*[]])$");
    if (qmllint.match(line).hasMatch())
        return {};
    Task::TaskType type = Task::Unknown;
    int matchLength = 0;
    if (line.startsWith("Error:")) {
        matchLength = 6;
        type = Task::Error;
    } else if (line.startsWith("Warning:")) {
        matchLength = 8;
        type = Task::Warning;
    } else {
        return {};
    }

    CompileTask task(type, line.mid(matchLength).trimmed());
    task.details << line;
    return task;
}

static Task::TaskType taskType(const QString &category)
{
    Task::TaskType type = Task::Unknown;
    if (category == "warning")
        type = Task::Warning;
    else if (category == "error")
        type = Task::Error;
    return type;
}

MsvcParser::MsvcParser()
{
    setObjectName("MsvcParser");
    m_compileRegExp.setPattern(QString(FILE_POS_PATTERN)
                               + ".*(?:(warning|error) ([A-Z]+\\d{4} ?: )|note: )(.*)$");
    QTC_CHECK(m_compileRegExp.isValid());
    m_additionalInfoRegExp.setPattern("^        (?:(could be |or )\\s*')?(.*)\\((\\d+)\\) : (.*)$");
    QTC_CHECK(m_additionalInfoRegExp.isValid());
}

Utils::Id MsvcParser::id()
{
    return Utils::Id("ProjectExplorer.OutputParser.Msvc");
}

OutputLineParser::Result MsvcParser::handleLine(const QString &line, OutputFormat type)
{
    if (type == OutputFormat::StdOutFormat) {
        QRegularExpressionMatch match = m_additionalInfoRegExp.match(line);
        if (line.startsWith("        ") && !match.hasMatch()) {
            if (currentTask().isNull())
                return Status::NotHandled;
            createOrAmendTask(Task::Unknown, {}, line, true);
            return Status::InProgress;
        }

        const Result res = processCompileLine(line);
        if (res.status != Status::NotHandled)
            return res;
        if (const Task t = handleNmakeJomMessage(line); !t.isNull()) {
            setCurrentTask(t);
            return Status::InProgress;
        }
        if (match.hasMatch()) {
            QString description = match.captured(1) + match.captured(4).trimmed();
            if (!match.captured(1).isEmpty())
                description.chop(1); // Remove trailing quote
            const FilePath filePath = absoluteFilePath(FilePath::fromUserInput(match.captured(2)));
            const int lineNo = match.captured(3).toInt();
            LinkSpecs linkSpecs;
            addLinkSpecForAbsoluteFilePath(linkSpecs, filePath, lineNo, -1, match, 2);
            createOrAmendTask(Task::Unknown, description, line, false, filePath, lineNo, 0, linkSpecs);
            return {Status::InProgress, linkSpecs};
        }
        return Status::NotHandled;
    }

    const Result res = processCompileLine(line);
    if (res.status != Status::NotHandled)
        return res;

    // Jom outputs errors to stderr
    if (const Task t = handleNmakeJomMessage(line); !t.isNull()) {
        setCurrentTask(t);
        return Status::InProgress;
    }

    return Status::NotHandled;
}

bool MsvcParser::isContinuation(const QString &line) const
{
    return line.contains("note: ");
}

MsvcParser::Result MsvcParser::processCompileLine(const QString &line)
{
    QRegularExpressionMatch match = m_compileRegExp.match(line);
    if (match.hasMatch()) {
        QPair<FilePath, int> position = parseFileName(match.captured(1));
        const FilePath filePath = absoluteFilePath(position.first);
        LinkSpecs linkSpecs;
        addLinkSpecForAbsoluteFilePath(linkSpecs, filePath, position.second, -1, match, 1);
        const QString &description = match.captured(3) + match.captured(4).trimmed();
        createOrAmendTask(
            taskType(match.captured(2)),
            description,
            line,
            false,
            filePath,
            position.second,
            0,
            linkSpecs);
        return {Status::InProgress, linkSpecs};
    }

    flush();
    return Status::NotHandled;
}

// --------------------------------------------------------------------------
// ClangClParser: The compiler errors look similar to MSVC, except that the
// column number is also given and there are no 4digit CXXXX error numbers.
// They are output to stderr.
// --------------------------------------------------------------------------

// ".\qwindowsgdinativeinterface.cpp(48,3) :  error: unknown type name 'errr'"
static inline QString clangClCompilePattern()
{
    return QLatin1String(FILE_POS_PATTERN) + " ?(warning|error): (.*)$";
}

ClangClParser::ClangClParser()
    : m_compileRegExp(clangClCompilePattern())
{
    setObjectName("ClangClParser");
    QTC_CHECK(m_compileRegExp.isValid());
}

// Check for a code marker '~~~~ ^ ~~~~~~~~~~~~' underlining above code.
static inline bool isClangCodeMarker(const QString &trimmedLine)
{
    return trimmedLine.constEnd() ==
            std::find_if(trimmedLine.constBegin(), trimmedLine.constEnd(),
                         [] (QChar c) { return c != ' ' && c != '^' && c != '~'; });
}

OutputLineParser::Result ClangClParser::handleLine(const QString &line, OutputFormat type)
{
    if (type == StdOutFormat) {
        if (const Task t = handleNmakeJomMessage(line); !t.isNull()) {
            setCurrentTask(t);
            flush();
            return Status::Done;
        }
        return Status::NotHandled;
    }
    const QString lne = rightTrimmed(line); // Strip \n.

    if (const Task t = handleNmakeJomMessage(lne); !t.isNull()) {
        setCurrentTask(t);
        flush();
        return Status::Done;
    }

    // Finish a sequence of warnings/errors: "2 warnings generated."
    if (!lne.isEmpty() && lne.at(0).isDigit() && lne.endsWith("generated.")) {
        flush();
        return Status::Done;
    }

    // Start a new error message by a sequence of "In file included from " which is to be skipped.
    if (lne.startsWith("In file included from ")) {
        flush();
        return Status::Done;
    }

    QRegularExpressionMatch match = m_compileRegExp.match(lne);
    if (match.hasMatch()) {
        flush();
        const QPair<FilePath, int> position = parseFileName(match.captured(1));
        const FilePath file = absoluteFilePath(position.first);
        const int lineNo = position.second;
        LinkSpecs linkSpecs;
        addLinkSpecForAbsoluteFilePath(linkSpecs, file, lineNo, -1, match, 1);
        createOrAmendTask(
            taskType(match.captured(2)), match.captured(3).trimmed(), line, false, file, lineNo);
        return {Status::InProgress, linkSpecs};
    }

    if (!currentTask().isNull()) {
        const QString trimmed = lne.trimmed();
        if (isClangCodeMarker(trimmed)) {
            flush();
            return Status::Done;
        }
        createOrAmendTask(Task::Unknown, {}, line, true);
        return Status::InProgress;
    }

    return Status::NotHandled;
}

// Unit tests:

#ifdef WITH_TESTS
#   include <QTest>
#   include "projectexplorer_test.h"
#   include "projectexplorer/outputparser_test.h"

namespace ProjectExplorer::Internal {

void ProjectExplorerTest::testMsvcOutputParsers_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<OutputParserTester::Channel>("inputChannel");
    QTest::addColumn<QStringList>("childStdOutLines");
    QTest::addColumn<QStringList>("childStdErrLines");
    QTest::addColumn<Tasks >("tasks");

    auto compileTask = [](Task::TaskType type,
                          const QString &description,
                          const Utils::FilePath &file,
                          int line,
                          const QList<QTextLayout::FormatRange> formats)
    {
        CompileTask task(type, description, file, line);
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
            << "Sometext" << OutputParserTester::STDOUT
            << QStringList("Sometext") << QStringList()
            << Tasks();
    QTest::newRow("pass-through stderr")
            << "Sometext" << OutputParserTester::STDERR
            << QStringList() << QStringList("Sometext")
            << Tasks();

    QTest::newRow("labeled error")
            << "qmlstandalone\\main.cpp(54) : error C4716: 'findUnresolvedModule' : must return a value"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "C4716: 'findUnresolvedModule' : must return a value",
                               FilePath::fromUserInput("qmlstandalone\\main.cpp"), 54));

    QTest::newRow("labeled error-2015")
            << "qmlstandalone\\main.cpp(54): error C4716: 'findUnresolvedModule' : must return a value"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "C4716: 'findUnresolvedModule' : must return a value",
                               FilePath::fromUserInput("qmlstandalone\\main.cpp"), 54));

    QTest::newRow("labeled error with number prefix")
            << "1>qmlstandalone\\main.cpp(54) : error C4716: 'findUnresolvedModule' : must return a value"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "C4716: 'findUnresolvedModule' : must return a value",
                               FilePath::fromUserInput("qmlstandalone\\main.cpp"), 54));

    QTest::newRow("labeled warning")
            << "x:\\src\\plugins\\projectexplorer\\msvcparser.cpp(69) : warning C4100: 'something' : unreferenced formal parameter"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "C4100: 'something' : unreferenced formal parameter",
                               FilePath::fromUserInput("x:\\src\\plugins\\projectexplorer\\msvcparser.cpp"), 69));

    QTest::newRow("labeled warning with number prefix")
            << "1>x:\\src\\plugins\\projectexplorer\\msvcparser.cpp(69) : warning C4100: 'something' : unreferenced formal parameter"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "C4100: 'something' : unreferenced formal parameter",
                               FilePath::fromUserInput("x:\\src\\plugins\\projectexplorer\\msvcparser.cpp"), 69));

    QTest::newRow("labeled chained warning")
            << "x:\\src\\libs\\narf\\stringutils.cpp(155): warning C4996: "
               "'std::wstring_convert<std::codecvt_utf8_utf16<wchar_t,1114111,(std::codecvt_mode)0>"
               ",wchar_t,std::allocator<wchar_t>,std::allocator<char>>::from_bytes': "
               "warning STL4017: std::wbuffer_convert, std::wstring_convert, and the <codecvt> "
               "header (containing std::codecvt_mode, std::codecvt_utf8, std::codecvt_utf16, and "
               "std::codecvt_utf8_utf16) are deprecated in C++17. more blabla"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "STL4017: std::wbuffer_convert, std::wstring_convert, and the "
                               "<codecvt> header (containing std::codecvt_mode, std::codecvt_utf8, "
                               "std::codecvt_utf16, and std::codecvt_utf8_utf16) are deprecated in "
                               "C++17. more blabla",
                               FilePath::fromUserInput("x:\\src\\libs\\narf\\stringutils.cpp"), 155));

    QTest::newRow("additional information")
            << "x:\\src\\plugins\\texteditor\\icompletioncollector.h(50) : warning C4099: 'TextEditor::CompletionItem' : type name first seen using 'struct' now seen using 'class'\n"
               "        x:\\src\\plugins\\texteditor\\completionsupport.h(39) : see declaration of 'TextEditor::CompletionItem'"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "C4099: 'TextEditor::CompletionItem' : type name first seen using 'struct' now seen using 'class'",
                               FilePath::fromUserInput("x:\\src\\plugins\\texteditor\\icompletioncollector.h"), 50)
                << CompileTask(Task::Unknown,
                               "see declaration of 'TextEditor::CompletionItem'",
                               FilePath::fromUserInput("x:\\src\\plugins\\texteditor\\completionsupport.h"), 39));

    QTest::newRow("additional information with prefix")
            << "2>x:\\src\\plugins\\texteditor\\icompletioncollector.h(50) : warning C4099: 'TextEditor::CompletionItem' : type name first seen using 'struct' now seen using 'class'\n"
               "        x:\\src\\plugins\\texteditor\\completionsupport.h(39) : see declaration of 'TextEditor::CompletionItem'"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "C4099: 'TextEditor::CompletionItem' : type name first seen using 'struct' now seen using 'class'",
                               FilePath::fromUserInput("x:\\src\\plugins\\texteditor\\icompletioncollector.h"), 50)
                << CompileTask(Task::Unknown,
                               "see declaration of 'TextEditor::CompletionItem'",
                               FilePath::fromUserInput("x:\\src\\plugins\\texteditor\\completionsupport.h"), 39));

    QTest::newRow("fatal linker error")
            << "LINK : fatal error LNK1146: no argument specified with option '/LIBPATH:'"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "LNK1146: no argument specified with option '/LIBPATH:'"));

    // This actually comes through stderr!
    QTest::newRow("command line warning")
            << "cl : Command line warning D9002 : ignoring unknown option '-fopenmp'"
            << OutputParserTester::STDERR
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               "D9002 : ignoring unknown option '-fopenmp'"));

    QTest::newRow("complex error")
            << "..\\untitled\\main.cpp(19) : error C2440: 'initializing' : cannot convert from 'int' to 'std::_Tree<_Traits>::iterator'\n"
               "        with\n"
               "        [\n"
               "            _Traits=std::_Tmap_traits<int,double,std::less<int>,std::allocator<std::pair<const int,double>>,false>\n"
               "        ]\n"
               "        No constructor could take the source type, or constructor overload resolution was ambiguous"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << compileTask(Task::Error,
                               "C2440: 'initializing' : cannot convert from 'int' to 'std::_Tree<_Traits>::iterator'\n"
                               "..\\untitled\\main.cpp(19) : error C2440: 'initializing' : cannot convert from 'int' to 'std::_Tree<_Traits>::iterator'\n"
                               "        with\n"
                               "        [\n"
                               "            _Traits=std::_Tmap_traits<int,double,std::less<int>,std::allocator<std::pair<const int,double>>,false>\n"
                               "        ]\n"
                               "        No constructor could take the source type, or constructor overload resolution was ambiguous",
                               FilePath::fromUserInput("..\\untitled\\main.cpp"),
                               19,
                               QList<QTextLayout::FormatRange>()
                                   << formatRange(85, 365)));

    QTest::newRow("Linker error 1")
            << "main.obj : error LNK2019: unresolved external symbol \"public: void __thiscall Data::doit(void)\" (?doit@Data@@QAEXXZ) referenced in function _main"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "LNK2019: unresolved external symbol \"public: void __thiscall Data::doit(void)\" (?doit@Data@@QAEXXZ) referenced in function _main",
                               FilePath::fromUserInput("main.obj"), -1));

    QTest::newRow("Linker error 2")
            << "debug\\Experimentation.exe : fatal error LNK1120: 1 unresolved externals"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "LNK1120: 1 unresolved externals",
                               FilePath::fromUserInput("debug\\Experimentation.exe")));

    QTest::newRow("nmake error")
            << "Error: dependent '..\\..\\..\\..\\creator-2.5\\src\\plugins\\coreplugin\\ifile.h' does not exist."
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "dependent '..\\..\\..\\..\\creator-2.5\\src\\plugins\\coreplugin\\ifile.h' does not exist."));

    QTest::newRow("jom error")
            << "Error: dependent 'main.cpp' does not exist."
            << OutputParserTester::STDERR
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                               "dependent 'main.cpp' does not exist."));

    QTest::newRow("Multiline error")
            << "c:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\INCLUDE\\xutility(2227) : warning C4996: 'std::_Copy_impl': Function call with parameters that may be unsafe - this call relies on the caller to check that the passed values are correct. To disable this warning, use -D_SCL_SECURE_NO_WARNINGS. See documentation on how to use Visual C++ 'Checked Iterators'\n"
               "        c:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\INCLUDE\\xutility(2212) : see declaration of 'std::_Copy_impl'\n"
               "        symbolgroupvalue.cpp(2314) : see reference to function template instantiation '_OutIt std::copy<const unsigned char*,unsigned short*>(_InIt,_InIt,_OutIt)' being compiled\n"
               "        with\n"
               "        [\n"
               "            _OutIt=unsigned short *,\n"
               "            _InIt=const unsigned char *\n"
               "        ]"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                        "C4996: 'std::_Copy_impl': Function call with parameters that may be unsafe - this call relies on the caller to check that the passed values are correct. To disable this warning, use -D_SCL_SECURE_NO_WARNINGS. See documentation on how to use Visual C++ 'Checked Iterators'",
                        FilePath::fromUserInput("c:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\INCLUDE\\xutility"), 2227)
                << CompileTask(Task::Unknown,
                        "see declaration of 'std::_Copy_impl'",
                        FilePath::fromUserInput("c:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\INCLUDE\\xutility"), 2212)
                << compileTask(Task::Unknown,
                        "see reference to function template instantiation '_OutIt std::copy<const unsigned char*,unsigned short*>(_InIt,_InIt,_OutIt)' being compiled\n"
                        "        symbolgroupvalue.cpp(2314) : see reference to function template instantiation '_OutIt std::copy<const unsigned char*,unsigned short*>(_InIt,_InIt,_OutIt)' being compiled\n"
                        "        with\n"
                        "        [\n"
                        "            _OutIt=unsigned short *,\n"
                        "            _InIt=const unsigned char *\n"
                        "        ]",
                        FilePath::fromUserInput("symbolgroupvalue.cpp"),
                        2314,
                        QList<QTextLayout::FormatRange>()
                            << formatRange(141, 287)));

    QTest::newRow("Ambiguous symbol")
            << "D:\\Project\\file.h(98) : error C2872: 'UINT64' : ambiguous symbol\n"
               "        could be 'C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include\\basetsd.h(83) : unsigned __int64 UINT64'\n"
               "        or       'D:\\Project\\types.h(71) : Types::UINT64'"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Error,
                        "C2872: 'UINT64' : ambiguous symbol",
                        FilePath::fromUserInput("D:\\Project\\file.h"), 98)
                << CompileTask(Task::Unknown,
                        "could be unsigned __int64 UINT64",
                        FilePath::fromUserInput("C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include\\basetsd.h"), 83)
                << CompileTask(Task::Unknown,
                        "or Types::UINT64",
                        FilePath::fromUserInput("D:\\Project\\types.h"), 71));

    QTest::newRow("ignore moc note")
            << "/home/qtwebkithelpviewer.h:0: Note: No relevant classes found. No output generated."
            << OutputParserTester::STDERR
            << QStringList() << QStringList("/home/qtwebkithelpviewer.h:0: Note: No relevant classes found. No output generated.")
            << (Tasks());

    QTest::newRow("error with note")
            << "main.cpp(7): error C2733: 'func': second C linkage of overloaded function not allowed\n"
               "main.cpp(6): note: see declaration of 'func'"
            << OutputParserTester::STDOUT
            << QStringList() << QStringList()
            << Tasks{compileTask(Task::Error,
                               "C2733: 'func': second C linkage of overloaded function not allowed\n"
                               "main.cpp(7): error C2733: 'func': second C linkage of overloaded function not allowed\n"
                               "main.cpp(6): note: see declaration of 'func'",
                               FilePath::fromUserInput("main.cpp"),
                               7,
                               QList<QTextLayout::FormatRange>()
                                   << formatRange(67, 130))};

    QTest::newRow("cyrillic warning") // QTCREATORBUG-20297
            << QString::fromUtf8("cl: командная строка warning D9025: переопределение \"/MDd\" на \"/MTd\"")
            << OutputParserTester::STDERR
            << QStringList() << QStringList()
            << (Tasks()
                << CompileTask(Task::Warning,
                               QString::fromUtf8("D9025: переопределение \"/MDd\" на \"/MTd\"")));
}

void ProjectExplorerTest::testMsvcOutputParsers()
{
    OutputParserTester testbench;
    testbench.addLineParser(new MsvcParser);
    QFETCH(QString, input);
    QFETCH(OutputParserTester::Channel, inputChannel);
    QFETCH(Tasks, tasks);
    QFETCH(QStringList, childStdOutLines);
    QFETCH(QStringList, childStdErrLines);

    testbench.testParsing(input, inputChannel, tasks, childStdOutLines, childStdErrLines);
}

void ProjectExplorerTest::testClangClOutputParsers_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<OutputParserTester::Channel>("inputChannel");
    QTest::addColumn<QStringList>("childStdOutLines");
    QTest::addColumn<QStringList>("childStdErrLines");
    QTest::addColumn<Tasks >("tasks");

    const QString clangClCompilerLog =
            "In file included from .\\qwindowseglcontext.cpp:40:\n"
            "./qwindowseglcontext.h(282,15) :  warning: private field 'm_version' is not used [-Wunused-private-field]\n"
            "const int m_version; //! majorVersion<<8 + minorVersion\n"
            "5 warnings generated.\n"
            ".\\qwindowsclipboard.cpp(60,19) :  warning: unused variable 'formatTextPlainC' [-Wunused-const-variable]\n"
            "static const char formatTextPlainC[] = \"text/plain\";\n"
            "                  ^\n"
            ".\\qwindowsclipboard.cpp(61,19) :  warning: unused variable 'formatTextHtmlC' [-Wunused-const-variable]\n"
            "static const char formatTextHtmlC[] = \"text/html\";\n"
            "                  ^\n"
            "2 warnings generated.\n"
            ".\\qwindowsgdinativeinterface.cpp(48,3) :  error: unknown type name 'errr'\n"
            "  errr\n"
            "  ^\n"
            ".\\qwindowsgdinativeinterface.cpp(51,1) :  error: expected unqualified-id\n"
            "void *QWindowsGdiNativeInterface::nativeResourceForBackingStore(const QByteArray &resource, QBackingStore *bs)\n"
            "^\n"
            "2 errors generated.\n";

    const QString ignoredStderr =
            "NMAKE : fatal error U1077: 'D:\\opt\\LLVM64_390\\bin\\clang-cl.EXE' : return code '0x1'\n"
            "Stop.";

    const QString input = clangClCompilerLog + ignoredStderr;
    const QStringList expectedStderr = ignoredStderr.split('\n');

    QTest::newRow("error")
            << input
            << OutputParserTester::STDERR
        << QStringList() << expectedStderr
            << (Tasks()
                << CompileTask(Task::Warning,
                           "private field 'm_version' is not used [-Wunused-private-field]\n"
                           "./qwindowseglcontext.h(282,15) :  warning: private field 'm_version' is not used [-Wunused-private-field]\n"
                           "const int m_version; //! majorVersion<<8 + minorVersion",
                           FilePath::fromUserInput("./qwindowseglcontext.h"), 282)
                << CompileTask(Task::Warning,
                           "unused variable 'formatTextPlainC' [-Wunused-const-variable]\n"
                           ".\\qwindowsclipboard.cpp(60,19) :  warning: unused variable 'formatTextPlainC' [-Wunused-const-variable]\n"
                           "static const char formatTextPlainC[] = \"text/plain\";",
                           FilePath::fromUserInput(".\\qwindowsclipboard.cpp"), 60)
                << CompileTask(Task::Warning,
                           "unused variable 'formatTextHtmlC' [-Wunused-const-variable]\n"
                           ".\\qwindowsclipboard.cpp(61,19) :  warning: unused variable 'formatTextHtmlC' [-Wunused-const-variable]\n"
                           "static const char formatTextHtmlC[] = \"text/html\";",
                           FilePath::fromUserInput(".\\qwindowsclipboard.cpp"), 61)
                << CompileTask(Task::Error,
                           "unknown type name 'errr'\n"
                           ".\\qwindowsgdinativeinterface.cpp(48,3) :  error: unknown type name 'errr'\n"
                           "  errr",
                           FilePath::fromUserInput(".\\qwindowsgdinativeinterface.cpp"), 48)
                << CompileTask(Task::Error,
                           "expected unqualified-id\n"
                           ".\\qwindowsgdinativeinterface.cpp(51,1) :  error: expected unqualified-id\n"
                           "void *QWindowsGdiNativeInterface::nativeResourceForBackingStore(const QByteArray &resource, QBackingStore *bs)",
                           FilePath::fromUserInput(".\\qwindowsgdinativeinterface.cpp"), 51));

    QTest::newRow("other error")
            << "C:\\Program Files\\LLVM\\bin\\clang-cl.exe /nologo /c /EHsc /Od -m64 /Zi /MDd "
               "/DUNICODE /D_UNICODE /DWIN32 /FdTestForError.cl.pdb "
               "/FoC:\\MyData\\Project_home\\cpp\build-TestForError-msvc_2017_clang-Debug\\Debug_msvc_201_47eca974c876c8b3\\TestForError.b6dd39ae\\3a52ce780950d4d9\\main.cpp.obj "
               "C:\\MyData\\Project_home\\cpp\\TestForError\\main.cpp /TP\r\n"
               "C:\\MyData\\Project_home\\cpp\\TestForError\\main.cpp(3,10): error: expected ';' after return statement\r\n"
               "return 0\r\n"
               "              ^\r\n"
               "              ;"
            << OutputParserTester::STDERR
            << QStringList()
            << QStringList{"C:\\Program Files\\LLVM\\bin\\clang-cl.exe /nologo /c /EHsc /Od -m64 /Zi /MDd "
               "/DUNICODE /D_UNICODE /DWIN32 /FdTestForError.cl.pdb "
               "/FoC:\\MyData\\Project_home\\cpp\build-TestForError-msvc_2017_clang-Debug\\Debug_msvc_201_47eca974c876c8b3\\TestForError.b6dd39ae\\3a52ce780950d4d9\\main.cpp.obj "
               "C:\\MyData\\Project_home\\cpp\\TestForError\\main.cpp /TP", "              ;"}
            << Tasks{CompileTask(Task::Error,
                             "expected ';' after return statement\n"
                             "C:\\MyData\\Project_home\\cpp\\TestForError\\main.cpp(3,10): error: expected ';' after return statement\n"
                             "return 0",
                             FilePath::fromUserInput("C:\\MyData\\Project_home\\cpp\\TestForError\\main.cpp"),
                             3)};
}

void ProjectExplorerTest::testClangClOutputParsers()
{
    OutputParserTester testbench;
    testbench.addLineParser(new ClangClParser);
    QFETCH(QString, input);
    QFETCH(OutputParserTester::Channel, inputChannel);
    QFETCH(QStringList, childStdOutLines);
    QFETCH(QStringList, childStdErrLines);
    QFETCH(Tasks, tasks);

    testbench.testParsing(input, inputChannel, tasks, childStdOutLines, childStdErrLines);
}

} // ProjectExplorer::Internal

#endif // WITH_TEST
