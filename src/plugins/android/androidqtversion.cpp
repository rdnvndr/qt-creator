// Copyright (C) 2016 BogDan Vatra <bog_dan_ro@yahoo.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconfigurations.h"
#include "androidconstants.h"
#include "androidqtversion.h"
#include "androidtr.h"
#include "androidutils.h"

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>

#include <qtsupport/qtkitaspect.h>
#include <qtsupport/qtsupportconstants.h>
#include <qtsupport/qtversionmanager.h>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/target.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/buildsystem.h>

#include <proparser/profileevaluator.h>

#include <qtsupport/qtversionfactory.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#ifdef WITH_TESTS
#   include <QTest>
#endif // WITH_TESTS

using namespace ProjectExplorer;
using namespace Utils;

namespace Android::Internal {

AndroidQtVersion::AndroidQtVersion()
    : m_guard(std::make_unique<QObject>())
{
    QObject::connect(AndroidConfigurations::instance(),
                     &AndroidConfigurations::aboutToUpdate,
                     m_guard.get(),
                     [this] { resetCache(); });
}

bool AndroidQtVersion::isValid() const
{
    if (!QtVersion::isValid())
        return false;
    if (qtAbis().isEmpty())
        return false;
    return true;
}

QString AndroidQtVersion::invalidReason() const
{
    QString tmp = QtVersion::invalidReason();
    if (tmp.isEmpty()) {
        if (AndroidConfig::ndkLocation(this).isEmpty())
            return Tr::tr("NDK is not configured in Devices > Android.");
        if (AndroidConfig::sdkLocation().isEmpty())
            return Tr::tr("SDK is not configured in Devices > Android.");
        if (qtAbis().isEmpty())
            return Tr::tr("Failed to detect the ABIs used by the Qt version. Check the settings in "
                          "Devices > Android for errors.");
    }
    return tmp;
}

bool AndroidQtVersion::supportsMultipleQtAbis() const
{
    return qtVersion() >= QVersionNumber(5, 14) && qtVersion() < QVersionNumber(6, 0);
}

Abis AndroidQtVersion::detectQtAbis() const
{
    Abis result = qtAbisFromJson();
    if (result.isEmpty() && AndroidConfig::sdkFullyConfigured()) {
        ensureMkSpecParsed();
        result = Utils::transform<Abis>(m_androidAbis, &androidAbi2Abi);
    }
    return result;
}

void AndroidQtVersion::addToBuildEnvironment(const Kit *k, Utils::Environment &env) const
{
    QtVersion::addToBuildEnvironment(k, env);

    // this env vars are used by qmake mkspecs to generate makefiles (check QTDIR/mkspecs/android-g++/qmake.conf for more info)
    env.set(QLatin1String("ANDROID_NDK_HOST"), AndroidConfig::toolchainHost(this));
    env.set(QLatin1String("ANDROID_NDK_ROOT"), AndroidConfig::ndkLocation(this).toUserOutput());
    env.set(QLatin1String("ANDROID_NDK_PLATFORM"),
        AndroidConfig::bestNdkPlatformMatch(qMax(minimumNDK(), minimumSDK(k)), this));
}

void AndroidQtVersion::setupQmakeRunEnvironment(Utils::Environment &env) const
{
    env.set(QLatin1String("ANDROID_NDK_ROOT"),
            AndroidConfig::ndkLocation(this).toUserOutput());
}

QString AndroidQtVersion::description() const
{
    //: Qt Version is meant for Android
    return Tr::tr("Android");
}

const QStringList AndroidQtVersion::androidAbis() const
{
    return Utils::transform(detectQtAbis(), &Abi::toAndroidAbi);
}

int AndroidQtVersion::minimumNDK() const
{
    ensureMkSpecParsed();
    return m_minNdk;
}

QString AndroidQtVersion::androidDeploymentSettingsFileName(const BuildConfiguration *bc)
{
    const BuildSystem *bs = bc->buildSystem();
    if (!bs)
        return {};
    const QString buildKey = bc->activeBuildKey();
    const QString displayName = bs->buildTarget(buildKey).displayName;
    const QString fileName = isQt5CmakeProject(bc->target())
                                 ? QLatin1String("android_deployment_settings.json")
                                 : QString::fromLatin1("android-%1-deployment-settings.json")
                                       .arg(displayName);
    return fileName;
}

Utils::FilePath AndroidQtVersion::androidDeploymentSettings(const BuildConfiguration *bc)
{
    // Try to fetch the file name from node data as provided by qmake and Qbs
    QString buildKey = bc->activeBuildKey();
    const ProjectNode *node = bc->project()->findNodeForBuildKey(buildKey);
    if (node) {
        const QString nameFromData = node->data(Constants::AndroidDeploySettingsFile).toString();
        if (!nameFromData.isEmpty())
            return Utils::FilePath::fromUserInput(nameFromData);
    }

    // If unavailable, construct the name by ourselves (CMake)
    const QString fileName = androidDeploymentSettingsFileName(bc);
    return buildDirectory(bc) / fileName;
}

AndroidQtVersion::BuiltWith AndroidQtVersion::builtWith(bool *ok) const
{
    // version.prefix() not yet set when this is called
    const FilePath coreModuleJson = qmakeFilePath().parentDir().parentDir() / "modules/Core.json";
    if (coreModuleJson.exists()) {
        if (const Result<QByteArray> contents = coreModuleJson.fileContents())
            return parseModulesCoreJson(*contents, ok);
    }

    if (ok)
        *ok = false;
    return {};
}

static int versionFromPlatformString(const QString &string, bool *ok = nullptr)
{
    static const QRegularExpression regex("android-(\\d+)");
    const QRegularExpressionMatch match = regex.match(string);
    if (ok)
        *ok = false;
    return match.hasMatch() ? match.captured(1).toInt(ok) : -1;
}

static AndroidQtVersion::BuiltWith parseBuiltWith(const QJsonObject &jsonObject, bool *ok)
{
    bool validPlatformString = false;
    AndroidQtVersion::BuiltWith result;
    if (const QJsonValue builtWith = jsonObject.value("built_with"); !builtWith.isUndefined()) {
        if (const QJsonValue android = builtWith["android"]; !android.isUndefined()) {
            if (const QJsonValue apiVersion = android["api_version"]; !apiVersion.isUndefined()) {
                const QString apiVersionString = apiVersion.toString();
                const int v = versionFromPlatformString(apiVersionString, &validPlatformString);
                if (validPlatformString)
                    result.apiVersion = v;
            }
            if (const QJsonValue ndk = android["ndk"]; !ndk.isUndefined()) {
                if (const QJsonValue version = ndk["version"]; !version.isUndefined())
                    result.ndkVersion = QVersionNumber::fromString(version.toString());
            }
        }
    }

    if (ok)
        *ok = validPlatformString && !result.ndkVersion.isNull();
    return result;
}

static AndroidQtVersion::BuiltWith parsePlatforms(const QJsonObject &jsonObject, bool *ok)
{
    AndroidQtVersion::BuiltWith result;
    if (ok)
        *ok = false;
    for (const QJsonValue &platformValue : jsonObject.value("platforms").toArray()) {
        const QJsonObject platform = platformValue.toObject();
        if (platform.value("name").toString() != QLatin1String("Android"))
            continue;
        const QJsonArray targets = platform.value("targets").toArray();
        if (targets.isEmpty())
            continue;
        const QJsonObject target = targets.first().toObject();
        const QString apiVersionString = target.value("api_version").toString();
        if (apiVersionString.isNull())
            continue;
        bool apiVersionOK = false;
        result.apiVersion = versionFromPlatformString(apiVersionString, &apiVersionOK);
        if (!apiVersionOK)
            continue;
        const QString ndkVersionString = target.value("ndk_version").toString();
        if (ndkVersionString.isNull())
            continue;
        result.ndkVersion = QVersionNumber::fromString(ndkVersionString);
        if (result.apiVersion != -1 && !result.ndkVersion.isNull()) {
            if (ok)
                *ok = true;
            break;
        }
    }
    return result;
}

AndroidQtVersion::BuiltWith AndroidQtVersion::parseModulesCoreJson(const QByteArray &data, bool *ok)
{
    AndroidQtVersion::BuiltWith result;
    const QJsonObject jsonObject = QJsonDocument::fromJson(data).object();
    const int schemaVersion = jsonObject.value("schema_version").toInt(1);
    if (schemaVersion >= 2)
        result = parsePlatforms(jsonObject, ok);
    else
        result = parseBuiltWith(jsonObject, ok);
    return result;
}

void AndroidQtVersion::parseMkSpec(ProFileEvaluator *evaluator) const
{
    m_androidAbis = evaluator->values("ALL_ANDROID_ABIS");
    if (m_androidAbis.isEmpty())
        m_androidAbis = QStringList{evaluator->value(Constants::ANDROID_TARGET_ARCH)};
    const QString androidPlatform = evaluator->value("ANDROID_PLATFORM");
    if (!androidPlatform.isEmpty())
        m_minNdk = versionFromPlatformString(androidPlatform);
    QtVersion::parseMkSpec(evaluator);
}

QSet<Utils::Id> AndroidQtVersion::availableFeatures() const
{
    QSet<Utils::Id> features = QtSupport::QtVersion::availableFeatures();
    features.insert(QtSupport::Constants::FEATURE_MOBILE);
    features.remove(QtSupport::Constants::FEATURE_QT_CONSOLE);
    features.remove(QtSupport::Constants::FEATURE_QT_WEBKIT);
    return features;
}

QSet<Utils::Id> AndroidQtVersion::targetDeviceTypes() const
{
    return {Constants::ANDROID_DEVICE_TYPE};
}


// Factory

class AndroidQtVersionFactory : public QtSupport::QtVersionFactory
{
public:
    AndroidQtVersionFactory()
    {
        setQtVersionCreator([] { return new AndroidQtVersion; });
        setSupportedType(Constants::ANDROID_QT_TYPE);
        setPriority(90);

        setRestrictionChecker([](const SetupData &setup) {
            return !setup.config.contains("android-no-sdk")
                   && (setup.config.contains("android")
                       || setup.platforms.contains("android"));
        });
    }
};

void setupAndroidQtVersion()
{
    static AndroidQtVersionFactory theAndroidQtVersionFactory;
}

#ifdef WITH_TESTS

class AndroidQtVersionTest final : public QObject
{
    Q_OBJECT

private slots:
   void testAndroidQtVersionParseBuiltWith_data();
   void testAndroidQtVersionParseBuiltWith();
};

void AndroidQtVersionTest::testAndroidQtVersionParseBuiltWith_data()
{
    QTest::addColumn<QString>("modulesCoreJson");
    QTest::addColumn<bool>("hasInfo");
    QTest::addColumn<QVersionNumber>("ndkVersion");
    QTest::addColumn<int>("apiVersion");

    QTest::newRow("Android Qt 6.4")
        << R"({
                "module_name": "Core",
                "version": "6.4.1",
                "built_with": {
                    "compiler_id": "Clang",
                    "compiler_target": "x86_64-none-linux-android23",
                    "compiler_version": "12.0.8",
                    "cross_compiled": true,
                    "target_system": "Android"
                }
            })"
        << false
        << QVersionNumber()
        << -1;

    QTest::newRow("Android Qt 6.5")
        << R"({
                "module_name": "Core",
                "version": "6.5.0",
                "built_with": {
                    "android": {
                        "api_version": "android-31",
                        "ndk": {
                            "version": "25.1.8937393"
                        }
                    },
                    "compiler_id": "Clang",
                    "compiler_target": "i686-none-linux-android23",
                    "compiler_version": "14.0.6",
                    "cross_compiled": true,
                    "target_system": "Android"
                }
            })"
        << true
        << QVersionNumber(25, 1, 8937393)
        << 31;

    QTest::newRow("Android Qt 6.9")
        << R"({
                "schema_version": 2,
                "name": "Core",
                "repository": "qtbase",
                "version": "6.9.0",
                "platforms": [
                  {
                    "name": "Android",
                    "version": "1",
                    "compiler_id": "Clang",
                    "compiler_version": "17.0.2",
                    "targets": [
                      {
                        "api_version": "android-34",
                        "ndk_version": "26.1.10909125",
                        "architecture": "arm",
                        "abi": "arm-little_endian-ilp32-eabi"
                      }
                    ]
                  }
                ]
            })"
        << true
        << QVersionNumber(26, 1, 10909125)
        << 34;
}

void AndroidQtVersionTest::testAndroidQtVersionParseBuiltWith()
{
    QFETCH(QString, modulesCoreJson);
    QFETCH(bool, hasInfo);
    QFETCH(int, apiVersion);
    QFETCH(QVersionNumber, ndkVersion);

    bool ok = false;
    const AndroidQtVersion::BuiltWith bw =
            AndroidQtVersion::parseModulesCoreJson(modulesCoreJson.toUtf8(), &ok);
    QCOMPARE(ok, hasInfo);
    QCOMPARE(bw.apiVersion, apiVersion);
    QCOMPARE(bw.ndkVersion, ndkVersion);
}

QObject *createAndroidQtVersionTest()
{
    return new AndroidQtVersionTest;
}

#endif // WITH_TESTS

} // Android::Internal

#include "androidqtversion.moc"
