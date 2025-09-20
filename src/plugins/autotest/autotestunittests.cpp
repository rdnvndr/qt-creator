// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "autotestunittests.h"

#include "testcodeparser.h"
#include "testtreemodel.h"

#include "qtest/qttestframework.h"

#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cpptoolstestcase.h>
#include <cppeditor/projectinfo.h>

#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainkitaspect.h>

#include <qtsupport/qtkitaspect.h>

#include <utils/environment.h>

#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

using namespace Core;
using namespace ExtensionSystem;
using namespace ProjectExplorer;
using namespace Utils;

namespace Autotest::Internal {

class AutotestUnitTests : public QObject
{
    Q_OBJECT

public:
    AutotestUnitTests()
        : m_model(TestTreeModel::instance())
    {}

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testCodeParser();
    void testCodeParser_data();
    void testCodeParserSwitchStartup();
    void testCodeParserSwitchStartup_data();
    void testCodeParserGTest();
    void testCodeParserGTest_data();
    void testCodeParserBoostTest();
    void testCodeParserBoostTest_data();
    void testModelManagerInterface();

private:
    TestTreeModel *m_model = nullptr;
    CppEditor::Tests::TemporaryCopiedDir *m_tmpDir = nullptr;
    bool m_isQt4 = false;
    bool m_checkBoost = false;
    ProjectExplorer::Kit *m_kit = nullptr;
};


void AutotestUnitTests::initTestCase()
{
    const QList<Kit *> allKits = KitManager::kits();
    if (allKits.count() == 0)
        QSKIP("This test requires at least one kit to be present");

    m_kit = findOr(allKits, nullptr, [](Kit *k) {
            return k->isValid() && QtSupport::QtKitAspect::qtVersion(k) != nullptr;
    });
    if (!m_kit)
        QSKIP("The test requires at least one valid kit with a valid Qt");

    if (auto qtVersion = QtSupport::QtKitAspect::qtVersion(m_kit))
        m_isQt4 = qtVersion->qtVersionString().startsWith('4');
    else
        QSKIP("Could not figure out which Qt version is used for default kit.");
    const Toolchain * const toolchain = ToolchainKitAspect::cxxToolchain(m_kit);
    if (!toolchain)
        QSKIP("This test requires that there is a kit with a toolchain.");

    m_tmpDir = new CppEditor::Tests::TemporaryCopiedDir(":/unit_test");

    if (!qtcEnvironmentVariableIsEmpty("BOOST_INCLUDE_DIR")) {
        m_checkBoost = true;
    } else {
        if (HostOsInfo::isLinuxHost()
                && (QFileInfo::exists("/usr/include/boost/version.hpp")
                    || QFileInfo::exists("/usr/local/include/boost/version.hpp"))) {
            qDebug() << "Found boost at system level - will run boost parser test.";
            m_checkBoost = true;
        }
    }

    // Enable quick check for derived tests
    theQtTestFramework().quickCheckForDerivedTests.setValue(true);
}

void AutotestUnitTests::cleanupTestCase()
{
    delete m_tmpDir;
}

void AutotestUnitTests::testCodeParser()
{
    QFETCH(FilePath, projectFilePath);
    QFETCH(int, expectedAutoTestsCount);
    QFETCH(int, expectedNamedQuickTestsCount);
    QFETCH(int, expectedUnnamedQuickTestsCount);
    QFETCH(int, expectedDataTagsCount);

    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    QVERIFY(projectManager.open(projectFilePath, true, m_kit));

    QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
    QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
    QVERIFY(parserSpy.wait(20000));
    QVERIFY(modelUpdateSpy.wait());

    if (m_isQt4)
        expectedNamedQuickTestsCount = expectedUnnamedQuickTestsCount = 0;

    QCOMPARE(m_model->autoTestsCount(), expectedAutoTestsCount);
    QCOMPARE(m_model->namedQuickTestsCount(), expectedNamedQuickTestsCount);
    QCOMPARE(m_model->unnamedQuickTestsCount(), expectedUnnamedQuickTestsCount);
    QCOMPARE(m_model->dataTagsCount(), expectedDataTagsCount);
}

void AutotestUnitTests::testCodeParser_data()
{
    QTest::addColumn<FilePath>("projectFilePath");
    QTest::addColumn<int>("expectedAutoTestsCount");
    QTest::addColumn<int>("expectedNamedQuickTestsCount");
    QTest::addColumn<int>("expectedUnnamedQuickTestsCount");
    QTest::addColumn<int>("expectedDataTagsCount");

    const FilePath base = m_tmpDir->filePath();
    QTest::newRow("plainAutoTest")
            << base / "plain/plain.pro"
            << 1 << 0 << 0 << 0;
    QTest::newRow("mixedAutoTestAndQuickTests")
            << base / "mixed_atp/mixed_atp.pro"
            << 4 << 10 << 5 << 10;
    QTest::newRow("plainAutoTestQbs")
            << base / "plain/plain.qbs"
            << 1 << 0 << 0 << 0;
    QTest::newRow("mixedAutoTestAndQuickTestsQbs")
            << base / "mixed_atp/mixed_atp.qbs"
            << 4 << 10 << 5 << 10;
}

void AutotestUnitTests::testCodeParserSwitchStartup()
{
    QFETCH(FilePaths, projectFilePaths);
    QFETCH(QList<int>, expectedAutoTestsCount);
    QFETCH(QList<int>, expectedNamedQuickTestsCount);
    QFETCH(QList<int>, expectedUnnamedQuickTestsCount);
    QFETCH(QList<int>, expectedDataTagsCount);

    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    for (int i = 0; i < projectFilePaths.size(); ++i) {
        qDebug() << "Opening project" << projectFilePaths.at(i);
        QVERIFY(projectManager.open(projectFilePaths.at(i), true, m_kit));

        QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
        QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
        QVERIFY(parserSpy.wait(20000));
        QVERIFY(modelUpdateSpy.wait());

        QCOMPARE(m_model->autoTestsCount(), expectedAutoTestsCount.at(i));
        QCOMPARE(m_model->namedQuickTestsCount(),
                 m_isQt4 ? 0 : expectedNamedQuickTestsCount.at(i));
        QCOMPARE(m_model->unnamedQuickTestsCount(),
                 m_isQt4 ? 0 : expectedUnnamedQuickTestsCount.at(i));
        QCOMPARE(m_model->dataTagsCount(),
                 expectedDataTagsCount.at(i));
    }
}

void AutotestUnitTests::testCodeParserSwitchStartup_data()
{
    QTest::addColumn<FilePaths>("projectFilePaths");
    QTest::addColumn<QList<int> >("expectedAutoTestsCount");
    QTest::addColumn<QList<int> >("expectedNamedQuickTestsCount");
    QTest::addColumn<QList<int> >("expectedUnnamedQuickTestsCount");
    QTest::addColumn<QList<int> >("expectedDataTagsCount");

    const FilePath base = m_tmpDir->filePath();
    FilePaths projects {
        base / "plain/plain.pro",
        base / "mixed_atp/mixed_atp.pro",
        base / "plain/plain.qbs",
        base / "mixed_atp/mixed_atp.qbs"
    };

    QList<int> expectedAutoTests = QList<int>()         << 1 << 4 << 1 << 4;
    QList<int> expectedNamedQuickTests = QList<int>()   << 0 << 10 << 0 << 10;
    QList<int> expectedUnnamedQuickTests = QList<int>() << 0 << 5 << 0 << 5;
    QList<int> expectedDataTagsCount = QList<int>()     << 0 << 10 << 0 << 10;

    QTest::newRow("loadMultipleProjects")
            << projects << expectedAutoTests << expectedNamedQuickTests
            << expectedUnnamedQuickTests << expectedDataTagsCount;
}

void AutotestUnitTests::testCodeParserGTest()
{
    if (qtcEnvironmentVariableIsEmpty("GOOGLETEST_DIR")) {
        const QString qcSource = QString(QTCREATORDIR);
        const FilePath gtestSrc = FilePath::fromUserInput(qcSource)
                                      .pathAppended("src/libs/3rdparty/googletest");
        if (gtestSrc.exists()) {
            qDebug() << "Trying to use googletest submodule in" << gtestSrc.toUserOutput() << ".";
            Environment::modifySystemEnvironment({EnvironmentItem{"GOOGLETEST_DIR",
                                                                  gtestSrc.toUserOutput()}});
        } else {
            QSKIP("This test needs googletest - set GOOGLETEST_DIR (point to googletest repository)");
        }
    }

    QFETCH(FilePath, projectFilePath);
    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    QVERIFY(projectManager.open(projectFilePath, true, m_kit));

    QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
    QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
    QVERIFY(parserSpy.wait(20000));
    QVERIFY(modelUpdateSpy.wait());

    QCOMPARE(m_model->gtestNamesCount(), 8);

    QMultiMap<QString, int> expectedNamesAndSets;
    expectedNamesAndSets.insert(QStringLiteral("FactorialTest"), 3);
    expectedNamesAndSets.insert(QStringLiteral("FactorialTest_Iterative"), 2);
    expectedNamesAndSets.insert(QStringLiteral("Sum"), 2);
    expectedNamesAndSets.insert(QStringLiteral("QueueTest"), 2);
    expectedNamesAndSets.insert(QStringLiteral("DummyTest"), 1); // used as parameterized test
    expectedNamesAndSets.insert(QStringLiteral("DummyTest"), 1); // used as 'normal' test
    expectedNamesAndSets.insert(QStringLiteral("NumberAsNameStart"), 1);
    expectedNamesAndSets.insert(QStringLiteral("NamespaceTest"), 1);

    QMultiMap<QString, int> foundNamesAndSets = m_model->gtestNamesAndSets();
    QCOMPARE(expectedNamesAndSets.size(), foundNamesAndSets.size());
    for (const QString &name : expectedNamesAndSets.keys())
        QCOMPARE(expectedNamesAndSets.values(name), foundNamesAndSets.values(name));

    // check also that no Qt related tests have been found
    QCOMPARE(m_model->autoTestsCount(), 0);
    QCOMPARE(m_model->namedQuickTestsCount(), 0);
    QCOMPARE(m_model->unnamedQuickTestsCount(), 0);
    QCOMPARE(m_model->dataTagsCount(), 0);
    QCOMPARE(m_model->boostTestNamesCount(), 0);
}

void AutotestUnitTests::testCodeParserGTest_data()
{
    QTest::addColumn<FilePath>("projectFilePath");
    QTest::newRow("simpleGoogletest")
        << m_tmpDir->filePath() / "simple_gt/simple_gt.pro";
    QTest::newRow("simpleGoogletestQbs")
        << m_tmpDir->filePath() / "simple_gt/simple_gt.qbs";
}

void AutotestUnitTests::testCodeParserBoostTest()
{
    if (!m_checkBoost)
        QSKIP("This test needs boost - set BOOST_INCLUDE_DIR (or have it installed)");

    QFETCH(FilePath, projectFilePath);
    QFETCH(QString, extension);
    CppEditor::Tests::ProjectOpenerAndCloser projectManager;
    const CppEditor::ProjectInfo::ConstPtr projectInfo
            = projectManager.open(projectFilePath, true, m_kit);
    QVERIFY(projectInfo);

    QSignalSpy parserSpy(m_model->parser(), &TestCodeParser::parsingFinished);
    QSignalSpy modelUpdateSpy(m_model, &TestTreeModel::sweepingDone);
    QVERIFY(parserSpy.wait(20000));
    QVERIFY(modelUpdateSpy.wait());

    QCOMPARE(m_model->boostTestNamesCount(), 5);

    const FilePath basePath = projectInfo->projectRoot();
    QVERIFY(!basePath.isEmpty());

    QMap<QString, int> expectedSuitesAndTests;

    auto pathConstructor = [basePath, extension](const QString &name, const QString &subPath) {
        return QString(name + '|' + basePath.pathAppended(subPath + extension).toUrlishString());
    };
    expectedSuitesAndTests.insert(pathConstructor("Master Test Suite", "tests/deco/deco"), 2); // decorators w/o suite
    expectedSuitesAndTests.insert(pathConstructor("Master Test Suite", "tests/fix/fix"), 2); // fixtures
    expectedSuitesAndTests.insert(pathConstructor("Master Test Suite", "tests/params/params"), 3); // functions
    expectedSuitesAndTests.insert(pathConstructor("Suite1", "tests/deco/deco"), 4);
    expectedSuitesAndTests.insert(pathConstructor("SuiteOuter", "tests/deco/deco"), 5); // 2 sub suites + 3 tests

    QMap<QString, int> foundNamesAndSets = m_model->boostTestSuitesAndTests();
    QCOMPARE(expectedSuitesAndTests.size(), foundNamesAndSets.size());
    for (auto it = expectedSuitesAndTests.cbegin(); it != expectedSuitesAndTests.cend(); ++it)
        QCOMPARE(*it, foundNamesAndSets.value(it.key()));

    // check also that no Qt related tests have been found
    QCOMPARE(m_model->autoTestsCount(), 0);
    QCOMPARE(m_model->namedQuickTestsCount(), 0);
    QCOMPARE(m_model->unnamedQuickTestsCount(), 0);
    QCOMPARE(m_model->dataTagsCount(), 0);
    QCOMPARE(m_model->gtestNamesCount(), 0);
}

void AutotestUnitTests::testCodeParserBoostTest_data()
{
    QTest::addColumn<FilePath>("projectFilePath");
    QTest::addColumn<QString>("extension");
    QTest::newRow("simpleBoostTest")
        << m_tmpDir->filePath() / "simple_boost/simple_boost.pro" << QString(".pro");
    QTest::newRow("simpleBoostTestQbs")
        << m_tmpDir->filePath() / "simple_boost/simple_boost.qbs" << QString(".qbs");
}

static int executeScenario(const QString &scenario)
{
    const PluginManager::ProcessData data = PluginManager::creatorProcessData();
    QStringList additionalArgs{ "-scenario", scenario };
    if (!data.m_args.contains("-settingspath") && !data.m_settingsPath.isEmpty())
        additionalArgs << "-settingspath" << data.m_settingsPath;
    return QProcess::execute(data.m_executable, data.m_args + additionalArgs);
}

void AutotestUnitTests::testModelManagerInterface()
{
    if (qtcEnvironmentVariableIsSet("QTC_SKIP_AUTOTEST_SCENARIO"))
        QSKIP("Test skipped due to set environment variable QTC_SKIP_AUTOTEST_SCENARIO.");
    QCOMPARE(executeScenario("TestModelManagerInterface"), 0);
}

QObject *createAutotestUnitTests()
{
    return new AutotestUnitTests;
}

} // namespace Autotest::Internal

#include "autotestunittests.moc"
