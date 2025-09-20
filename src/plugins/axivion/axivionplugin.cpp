// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "axivionplugin.h"

#include "axivionperspective.h"
#include "axivionsettings.h"
#include "axiviontr.h"
#include "dashboard/dto.h"
#include "dashboard/error.h"
#include "localbuild.h"

#include <coreplugin/credentialquery.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/session.h>

#include <extensionsystem/iplugin.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <solutions/tasking/networkquery.h>
#include <solutions/tasking/tasktreerunner.h>

#include <texteditor/textdocument.h>
#include <texteditor/textmark.h>

#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/environment.h>
#include <utils/fileinprojectfinder.h>
#include <utils/networkaccessmanager.h>
#include <utils/qtcassert.h>
#include <utils/temporaryfile.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QUrlQuery>

#include <cmath>
#include <memory>

constexpr char s_axivionTextMarkId[] = "AxivionTextMark";

using namespace Core;
using namespace ProjectExplorer;
using namespace Tasking;
using namespace TextEditor;
using namespace Utils;

namespace Axivion::Internal {

QIcon iconForIssue(const std::optional<Dto::IssueKind> &issueKind)
{
    if (!issueKind)
        return {};

    static QHash<Dto::IssueKind, QIcon> prefixToIcon;

    auto it = prefixToIcon.constFind(*issueKind);
    if (it != prefixToIcon.constEnd())
        return *it;

    const QLatin1String prefix = Dto::IssueKindMeta::enumToStr(*issueKind);
    const Icon icon({{FilePath::fromString(":/axivion/images/button-" + prefix + ".png"),
                      Theme::PaletteButtonText}}, Icon::Tint);
    return prefixToIcon.insert(*issueKind, icon.icon()).value();
}

static QString anyToString(const Dto::Any &any)
{
   if (any.isNull() || !any.isString())
        return {};
    return any.getString();
}

static QString anyToPathString(const Dto::Any &any)
{
    const QString pathStr = anyToString(any);
    if (pathStr.isEmpty())
        return {};
    const FilePath fp = FilePath::fromUserInput(pathStr);
    return fp.contains("/") ? QString("%1 [%2]").arg(fp.fileName(), fp.path()) : fp.fileName();
}

// only the first found innerKey is used to add its value to the list
static QString anyListOfMapToString(const Dto::Any &any, const QStringList &innerKeys)
{
    if (any.isNull() || !any.isList())
        return {};
    const std::vector<Dto::Any> anyList = any.getList();
    QStringList list;
    for (const Dto::Any &inner : anyList) {
        if (!inner.isMap())
            continue;
        const std::map<QString, Dto::Any> innerMap = inner.getMap();
        for (const QString &innerKey : innerKeys) {
            auto value = innerMap.find(innerKey);
            if (value == innerMap.end())
                continue;
            list << anyToString(value->second);
            break;
        }
    }
    return list.join(", ");
}

static QString anyToNumberString(const Dto::Any &any)
{
    if (any.isNull())
        return {};
    if (any.isString()) // handle Infinity/NaN/...
        return any.getString();

    const double value = any.getDouble();
    double intPart;
    const double frac = std::modf(value, &intPart);
    if (frac != 0)
        return QString::number(value, 'f');
    return QString::number(value, 'f', 0);
}

QString anyToSimpleString(const Dto::Any &any, const QString &type,
                          const std::optional<std::vector<Dto::ColumnTypeOptionDto>> &options)
{
    if (type == "path")
        return anyToPathString(any);
    if (type == "string" || type == "state")
        return anyToString(any);
    if (type == "tags")
        return anyListOfMapToString(any, {"tag"});
    if (type == "number")
        return anyToNumberString(any);
    if (type == "owners") {
        return anyListOfMapToString(any, {"displayName", "name"});
    }
    if (type == "boolean") {
        if (!any.isBool())
            return {};
        if (options && options->size() == 2)
            return any.getBool() ? options->at(1).key : options->at(0).key;
        return any.getBool() ? QString("true") : QString("false");
    }

    QTC_ASSERT(false, qDebug() << "unhandled" << type);
    return {};
}

static QString apiTokenDescription()
{
    const QString ua = "Axivion" + QCoreApplication::applicationName() + "Plugin/"
                       + QCoreApplication::applicationVersion();
    QString user = Utils::qtcEnvironmentVariable("USERNAME");
    if (user.isEmpty())
        user = Utils::qtcEnvironmentVariable("USER");
    return "Automatically created by " + ua + " on " + user + "@" + QSysInfo::machineHostName();
}

template <typename DtoType>
struct GetDtoStorage
{
    QUrl url;
    std::optional<QByteArray> credential;
    std::optional<DtoType> dtoData;
};

template <typename DtoType>
struct PostDtoStorage
{
    QUrl url;
    std::optional<QByteArray> credential;
    QString password;
    QByteArray csrfToken;
    QByteArray writeData;
    std::optional<DtoType> dtoData;
};

static DashboardInfo toDashboardInfo(const GetDtoStorage<Dto::DashboardInfoDto> &dashboardStorage)
{
    const Dto::DashboardInfoDto &infoDto = *dashboardStorage.dtoData;
    const QVersionNumber versionNumber = infoDto.dashboardVersionNumber
        ? QVersionNumber::fromString(*infoDto.dashboardVersionNumber) : QVersionNumber();

    QStringList projects;
    QHash<QString, QUrl> projectUrls;

    if (infoDto.projects) {
        for (const Dto::ProjectReferenceDto &project : *infoDto.projects) {
            projects.push_back(project.name);
            projectUrls.insert(project.name, project.url);
        }
    }
    return {
        dashboardStorage.url,
        versionNumber,
        projects,
        projectUrls,
        infoDto.checkCredentialsUrl,
        infoDto.namedFiltersUrl,
        infoDto.userNamedFiltersUrl,
        infoDto.username,
    };
}

QUrlQuery IssueListSearch::toUrlQuery(QueryMode mode) const
{
    QUrlQuery query;
    QTC_ASSERT(!kind.isEmpty(), return query);
    query.addQueryItem("kind", kind);
    if (!versionStart.isEmpty())
        query.addQueryItem("start", versionStart);
    if (!versionEnd.isEmpty())
        query.addQueryItem("end", versionEnd);
    if (mode == QueryMode::SimpleQuery)
        return query;

    if (!owner.isEmpty())
        query.addQueryItem("user", owner);
    if (!filter_path.isEmpty())
        query.addQueryItem("filter_any path", filter_path);
    if (!state.isEmpty())
        query.addQueryItem("state", state);
    if (mode == QueryMode::FilterQuery)
        return query;

    QTC_CHECK(mode == QueryMode::FullQuery);
    query.addQueryItem("offset", QString::number(offset));
    if (limit)
        query.addQueryItem("limit", QString::number(limit));
    if (computeTotalRowCount)
        query.addQueryItem("computeTotalRowCount", "true");
    if (!sort.isEmpty())
        query.addQueryItem("sort", sort);
    if (!filter.isEmpty()) {
        for (auto f = filter.cbegin(), end = filter.cend(); f != end; ++f)
            query.addQueryItem(f.key(), f.value());
    }
    return query;
}

enum class ServerAccess { Unknown, NoAuthorization, WithAuthorization };

class AxivionPluginPrivate : public QObject
{
    Q_OBJECT
public:
    AxivionPluginPrivate();
    void handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors);
    void onStartupProjectChanged(Project *project);
    void fetchLocalDashboardInfo(const DashboardInfoHandler &handler,
                                 const QString &projectName);
    void fetchDashboardAndProjectInfo(const DashboardInfoHandler &handler,
                                      const QString &projectName);
    void handleOpenedDocs();
    void onDocumentOpened(IDocument *doc);
    void onDocumentClosed(IDocument * doc);
    void clearAllMarks();
    void updateExistingMarks();
    void handleIssuesForFile(const Dto::FileViewDto &fileView, const FilePath &filePath);
    void enableInlineIssues(bool enable);
    void fetchIssueInfo(DashboardMode dashboardMode, const QString &id);
    void fetchNamedFilters(DashboardMode dashboardMode);

    void switchDashboardMode(DashboardMode mode, bool byLocalBuildButton);

    void onSessionLoaded(const QString &sessionName);
    void onAboutToSaveSession();

public:
    // active id used for any network communication, defaults to settings' default
    // set to projects settings' dashboard id on open project
    Id m_dashboardServerId;
    // TODO: Should be set to Unknown on server address change in settings.
    ServerAccess m_serverAccess = ServerAccess::Unknown;
    // TODO: Should be cleared on username change in settings.
    std::optional<QByteArray> m_apiToken;
    // local build access
    std::optional<LocalDashboardAccess> m_localDashboard;

    NetworkAccessManager m_networkAccessManager;
    std::optional<DashboardInfo> m_dashboardInfo;
    std::optional<DashboardInfo> m_localDashboardInfo;
    std::optional<Dto::ProjectInfoDto> m_currentProjectInfo;
    std::optional<Dto::ProjectInfoDto> m_currentLocalProjectInfo;
    std::optional<QString> m_analysisVersion;
    QList<Dto::NamedFilterInfoDto> m_globalNamedFilters;
    QList<Dto::NamedFilterInfoDto> m_userNamedFilters;
    Project *m_project = nullptr;
    bool m_runningQuery = false;
    TaskTreeRunner m_taskTreeRunner;
    std::unordered_map<IDocument *, std::unique_ptr<TaskTree>> m_docMarksTrees;
    TaskTreeRunner m_issueInfoRunner;
    TaskTreeRunner m_namedFilterRunner;
    FileInProjectFinder m_fileFinder; // FIXME maybe obsolete when path mapping is implemented
    QMetaObject::Connection m_fileFinderConnection;
    QHash<FilePath, QSet<TextMark *>> m_allMarks;
    bool m_inlineIssuesEnabled = true;
    DashboardMode m_dashboardMode = DashboardMode::Global;
};

static AxivionPluginPrivate *dd = nullptr;

class AxivionTextMark : public TextMark
{
public:
    AxivionTextMark(const FilePath &filePath, const Dto::LineMarkerDto &issue,
                    std::optional<Theme::Color> color)
        : TextMark(filePath, issue.startLine, {"Axivion", s_axivionTextMarkId})
    {
        const QString markText = issue.description;
        const QString id = issue.kind + QString::number(issue.id.value_or(-1));
        setToolTip(id + '\n' + markText);
        setIcon(iconForIssue(issue.getOptionalKindEnum()));
        if (color)
            setColor(*color);
        setPriority(TextMark::NormalPriority);
        setLineAnnotation(markText);
        setActionsProvider([id] {
            auto action = new QAction;
            action->setIcon(Icons::INFO.icon());
            action->setToolTip(Tr::tr("Show Issue Properties"));
            QObject::connect(action, &QAction::triggered,
                             dd, [id] {
                const bool useGlobal = currentDashboardMode() == DashboardMode::Global
                        || !currentIssueHasValidPathMapping();
                dd->fetchIssueInfo(useGlobal ? DashboardMode::Global : DashboardMode::Local, id);
            });
            return QList{action};
        });
    }
};

void fetchLocalDashboardInfo(const DashboardInfoHandler &handler, const QString &projectName)
{
    QTC_ASSERT(dd, return);
    dd->fetchLocalDashboardInfo(handler, projectName);
}

void fetchDashboardAndProjectInfo(const DashboardInfoHandler &handler, const QString &projectName)
{
    QTC_ASSERT(dd, return);
    dd->fetchDashboardAndProjectInfo(handler, projectName);
}

std::optional<Dto::ProjectInfoDto> projectInfo()
{
    QTC_ASSERT(dd, return {});
    return dd->m_currentProjectInfo;
}

std::optional<Dto::ProjectInfoDto> localProjectInfo()
{
    QTC_ASSERT(dd, return {});
    return dd->m_currentLocalProjectInfo;
}

void fetchNamedFilters(DashboardMode dashboardMode)
{
    QTC_ASSERT(dd, return);
    dd->fetchNamedFilters(dashboardMode);
}

static QList<Dto::NamedFilterInfoDto> withoutRestricted(const QString &kind, const QList<Dto::NamedFilterInfoDto> &f)
{
    return Utils::filtered(f, [kind](const Dto::NamedFilterInfoDto &dto) {
        if (dto.supportsAllIssueKinds)
            return true;
        return !dto.issueKindRestrictions || dto.issueKindRestrictions->contains(kind)
               || dto.issueKindRestrictions->contains("UNIVERSAL");
    });
};

// TODO: Introduce FilterScope enum { Global, User } and use it instead of bool global.
QList<NamedFilter> knownNamedFiltersFor(const QString &issueKind, bool global)
{
    QTC_ASSERT(dd, return {});

    if (issueKind.isEmpty()) // happens after initial dashboad and filters fetch
        return {};

    return Utils::transform(withoutRestricted(issueKind, global ? dd->m_globalNamedFilters : dd->m_userNamedFilters),
                               [global](const Dto::NamedFilterInfoDto &dto) {
        return NamedFilter{dto.key, dto.displayName, global};
    });
}

std::optional<Dto::NamedFilterInfoDto> namedFilterInfoForKey(const QString &key, bool global)
{
    QTC_ASSERT(dd, return std::nullopt);

    const auto findFilter = [](const QList<Dto::NamedFilterInfoDto> filters, const QString &key)
            -> std::optional<Dto::NamedFilterInfoDto> {
        const int index = Utils::indexOf(filters, [key](const Dto::NamedFilterInfoDto &dto) {
            return dto.key == key;
        });
        if (index == -1)
            return std::nullopt;
        return filters.at(index);
    };

    if (global)
        return findFilter(dd->m_globalNamedFilters, key);
    else
        return findFilter(dd->m_userNamedFilters, key);
}

// FIXME: extend to give some details?
// FIXME: move when curl is no more in use?
bool handleCertificateIssue(const Utils::Id &serverId)
{
    QTC_ASSERT(dd, return false);
    const QString serverHost = QUrl(settings().serverForId(serverId).dashboard).host();
    if (QMessageBox::question(ICore::dialogParent(), Tr::tr("Certificate Error"),
                              Tr::tr("Server certificate for %1 cannot be authenticated.\n"
                                     "Do you want to disable SSL verification for this server?\n"
                                     "Note: This can expose you to man-in-the-middle attack.")
                              .arg(serverHost))
            != QMessageBox::Yes) {
        return false;
    }
    settings().disableCertificateValidation(serverId);
    settings().apply();

    return true;
}

AxivionPluginPrivate::AxivionPluginPrivate()
{
#if QT_CONFIG(ssl)
    connect(&m_networkAccessManager, &QNetworkAccessManager::sslErrors,
            this, &AxivionPluginPrivate::handleSslErrors);
#endif // ssl
    connect(&settings().highlightMarks, &BoolAspect::changed,
            this, &AxivionPluginPrivate::updateExistingMarks);
    connect(SessionManager::instance(), &SessionManager::sessionLoaded,
            this, &AxivionPluginPrivate::onSessionLoaded);
    connect(SessionManager::instance(), &SessionManager::aboutToSaveSession,
            this, &AxivionPluginPrivate::onAboutToSaveSession);

}

void AxivionPluginPrivate::handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    QTC_ASSERT(dd, return);
#if QT_CONFIG(ssl)
    const QList<QSslError::SslError> accepted{
        QSslError::CertificateNotYetValid, QSslError::CertificateExpired,
        QSslError::InvalidCaCertificate, QSslError::CertificateUntrusted,
        QSslError::HostNameMismatch
    };
    if (Utils::allOf(errors,
                     [&accepted](const QSslError &e) { return accepted.contains(e.error()); })) {
        const bool shouldValidate = settings().serverForId(dd->m_dashboardServerId).validateCert;
        if (!shouldValidate || handleCertificateIssue(dd->m_dashboardServerId))
            reply->ignoreSslErrors(errors);
    }
#else // ssl
    Q_UNUSED(reply)
    Q_UNUSED(errors)
#endif // ssl
}

void AxivionPluginPrivate::onStartupProjectChanged(Project *project)
{
    if (project == m_project)
        return;

    if (m_project)
        disconnect(m_fileFinderConnection);

    m_project = project;

    if (!m_project) {
        m_fileFinder.setProjectDirectory({});
        m_fileFinder.setProjectFiles({});
        return;
    }

    m_fileFinder.setProjectDirectory(m_project->projectDirectory());
    m_fileFinderConnection = connect(m_project, &Project::fileListChanged, this, [this] {
        m_fileFinder.setProjectFiles(m_project->files(Project::AllFiles));
        handleOpenedDocs();
    });
}

static QUrl constructUrl(DashboardMode dashboardMode, const QString &projectName,
                         const QString &subPath, const QUrlQuery &query)
{
    if (!dd->m_dashboardInfo)
        return {};
    const QByteArray encodedProjectName = QUrl::toPercentEncoding(projectName);
    const QUrl path(QString{"api/projects/" + QString::fromUtf8(encodedProjectName) + '/'});
    QUrl url = resolveDashboardInfoUrl(dashboardMode, path);
    if (!subPath.isEmpty() && QTC_GUARD(!subPath.startsWith('/')))
        url = url.resolved(subPath);
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

static constexpr int httpStatusCodeOk = 200;
constexpr char s_htmlContentType[] = "text/html";
constexpr char s_plaintextContentType[] = "text/plain";
constexpr char s_svgContentType[] = "image/svg+xml";
constexpr char s_jsonContentType[] = "application/json";

static bool isServerAccessEstablished(DashboardMode dashboardMode)
{
    if (dashboardMode == DashboardMode::Global) {
        return dd->m_serverAccess == ServerAccess::NoAuthorization
               || (dd->m_serverAccess == ServerAccess::WithAuthorization && dd->m_apiToken);
    }
    return dd->m_localDashboard.has_value();
}

static QByteArray basicAuth(const LocalDashboardAccess &localAccess)
{
    const QByteArray credentials = QString{localAccess.user + ':' + localAccess.password}
                                       .toUtf8().toBase64();
    return "Basic " + credentials;
}

static QByteArray contentTypeData(ContentType contentType)
{
    switch (contentType) {
    case ContentType::Html:      return s_htmlContentType;
    case ContentType::Json:      return s_jsonContentType;
    case ContentType::PlainText: return s_plaintextContentType;
    case ContentType::Svg:       return s_svgContentType;
    }
    return {};
}

QUrl resolveDashboardInfoUrl(DashboardMode dashboardMode, const QUrl &resource)
{
    QTC_ASSERT(dd, return {});
    QTC_ASSERT(dd->m_dashboardInfo, return {});
    if (dashboardMode == DashboardMode::Global)
        return dd->m_dashboardInfo->source.resolved(resource);
    QTC_ASSERT(dd->m_localDashboardInfo, return {});
    return dd->m_localDashboardInfo->source.resolved(resource);

}

Group downloadDataRecipe(DashboardMode dashboardMode, const Storage<DownloadData> &storage)
{
    const auto onQuerySetup = [storage, dashboardMode](NetworkQuery &query) {
        if (!isServerAccessEstablished(dashboardMode))
            return SetupResult::StopWithError; // TODO: start authorizationRecipe()?

        QNetworkRequest request(storage->inputUrl);
        request.setRawHeader("Accept", contentTypeData(storage->expectedContentType));
        if (dashboardMode == DashboardMode::Global) {
            if (dd->m_serverAccess == ServerAccess::WithAuthorization && dd->m_apiToken)
                request.setRawHeader("Authorization", "AxToken " + *dd->m_apiToken);
        } else {
            request.setRawHeader("Authorization", basicAuth(*dd->m_localDashboard));
        }
        const QByteArray ua = "Axivion" + QCoreApplication::applicationName().toUtf8() +
                              "Plugin/" + QCoreApplication::applicationVersion().toUtf8();
        request.setRawHeader("X-Axivion-User-Agent", ua);
        query.setRequest(request);
        query.setNetworkAccessManager(&dd->m_networkAccessManager);
        return SetupResult::Continue;
    };
    const auto onQueryDone = [storage](const NetworkQuery &query, DoneWith doneWith) {
        QNetworkReply *reply = query.reply();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader)
                                        .toString()
                                        .split(';')
                                        .constFirst()
                                        .trimmed()
                                        .toLower();
        if (doneWith == DoneWith::Success && statusCode == httpStatusCodeOk
            && contentType == QString::fromUtf8(contentTypeData(storage->expectedContentType))) {
            storage->outputData = reply->readAll();
            return DoneResult::Success;
        }
        return DoneResult::Error;
    };
    return {NetworkQueryTask(onQuerySetup, onQueryDone)};
}

template <typename DtoType, template <typename> typename DtoStorageType>
static Group dtoRecipe(const Storage<DtoStorageType<DtoType>> &dtoStorage)
{
    const Storage<std::optional<QByteArray>> storage;

    const auto onNetworkQuerySetup = [dtoStorage](NetworkQuery &query) {
        QNetworkRequest request(dtoStorage->url);
        request.setRawHeader("Accept", s_jsonContentType);
        if (dtoStorage->credential) // Unauthorized access otherwise
            request.setRawHeader("Authorization", *dtoStorage->credential);
        const QByteArray ua = "Axivion" + QCoreApplication::applicationName().toUtf8() +
                              "Plugin/" + QCoreApplication::applicationVersion().toUtf8();
        request.setRawHeader("X-Axivion-User-Agent", ua);

        if constexpr (std::is_same_v<DtoStorageType<DtoType>, PostDtoStorage<DtoType>>) {
            request.setRawHeader("Content-Type", "application/json");
            request.setRawHeader("AX-CSRF-Token", dtoStorage->csrfToken);
            query.setWriteData(dtoStorage->writeData);
            query.setOperation(NetworkOperation::Post);
        }

        query.setRequest(request);
        query.setNetworkAccessManager(&dd->m_networkAccessManager);
    };

    const auto onNetworkQueryDone = [storage, dtoStorage](const NetworkQuery &query,
                                                          DoneWith doneWith) {
        QNetworkReply *reply = query.reply();
        const QNetworkReply::NetworkError error = reply->error();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader)
                                        .toString()
                                        .split(';')
                                        .constFirst()
                                        .trimmed()
                                        .toLower();
        if (doneWith == DoneWith::Success && statusCode == httpStatusCodeOk
            && contentType == s_jsonContentType) {
            *storage = reply->readAll();
            dtoStorage->url = reply->url();
            return DoneResult::Success;
        }

        QString errorString;
        if (contentType == s_jsonContentType) {
            const Result<Dto::ErrorDto> error
                = Dto::ErrorDto::deserializeExpected(reply->readAll());

            if (error) {
                if constexpr (std::is_same_v<DtoType, Dto::DashboardInfoDto>) {
                    // Suppress logging error on unauthorized dashboard fetch
                    if (!dtoStorage->credential && error->type == "UnauthenticatedException") {
                        dtoStorage->url = reply->url();
                        return DoneResult::Success;
                    }
                }

                if (statusCode == 400 && error->type == "InvalidFilterException"
                        && !error->message.isEmpty()) {
                    // handle error..
                    showFilterException(error->message);
                    return DoneResult::Error;
                }

                if constexpr (std::is_same_v<DtoStorageType<DtoType>, PostDtoStorage<DtoType>>
                              && std::is_same_v<DtoType, Dto::ApiTokenInfoDto>) {
                    if (statusCode == 400 && error->type == "PasswordVerificationException" && error->data) {
                        const auto it = error->data->find("passwordMayBeUsedAsApiToken");
                        if (it != error->data->end()) {
                            const Dto::Any data = it->second;
                            if (data.isBool() && data.getBool()) {
                                Dto::ApiTokenInfoDto fakeDto{
                                    QString(),
                                    QString(),
                                    true,
                                    QString(),
                                    QString(),
                                    dtoStorage->password,
                                    QString(),
                                    QString(),
                                    QString(),
                                    QString(),
                                    std::optional<QString>(),
                                    QString(),
                                    false
                                };
                                dtoStorage->dtoData = fakeDto;
                                return DoneResult::Success;
                            }
                        }
                    }
                }
                errorString = Error(DashboardError(reply->url(), statusCode,
                    reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString(),
                                     *error)).message();
            } else {
                errorString = error.error();
            }
        } else if (statusCode != 0) {
            errorString = Error(HttpError(reply->url(), statusCode,
                reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString(),
                                 QString::fromUtf8(reply->readAll()))).message(); // encoding?
        } else {
            errorString = Error(NetworkError(reply->url(), error, reply->errorString())).message();
        }

        showErrorMessage(errorString);
        return DoneResult::Error;
    };

    const auto onDeserializeSetup = [storage](Async<Result<DtoType>> &task) {
        if (!*storage)
            return SetupResult::StopWithSuccess;

        const auto deserialize = [](QPromise<Result<DtoType>> &promise, const QByteArray &input) {
            promise.addResult(DtoType::deserializeExpected(input));
        };
        task.setConcurrentCallData(deserialize, **storage);
        return SetupResult::Continue;
    };

    const auto onDeserializeDone = [dtoStorage](const Async<Result<DtoType>> &task,
                                                DoneWith doneWith) {
        if (doneWith == DoneWith::Success && task.isResultAvailable()) {
            const auto result = task.result();
            if (result) {
                dtoStorage->dtoData = *result;
                return DoneResult::Success;
            }
            MessageManager::writeFlashing(QString("Axivion: %1").arg(result.error()));
        } else {
            MessageManager::writeFlashing(QString("Axivion: %1")
                .arg(Tr::tr("Unknown Dto structure deserialization error.")));
        }
        return DoneResult::Error;
    };

    return {
        storage,
        NetworkQueryTask(onNetworkQuerySetup, onNetworkQueryDone),
        AsyncTask<Result<DtoType>>(onDeserializeSetup, onDeserializeDone)
    };
}

static QString credentialOperationMessage(CredentialOperation operation)
{
    switch (operation) {
    case CredentialOperation::Get:
        return Tr::tr("The ApiToken cannot be read in a secure way.");
    case CredentialOperation::Set:
        return Tr::tr("The ApiToken cannot be stored in a secure way.");
    case CredentialOperation::Delete:
        return Tr::tr("The ApiToken cannot be deleted in a secure way.");
    }
    return {};
}

static void handleCredentialError(const CredentialQuery &credential)
{
    const QString keyChainMessage = credential.errorString().isEmpty() ? QString()
        : QString(" %1").arg(Tr::tr("Key chain message: \"%1\".").arg(credential.errorString()));
    MessageManager::writeFlashing(QString("Axivion: %1")
        .arg(credentialOperationMessage(credential.operation()) + keyChainMessage));
}

static Group authorizationRecipe(DashboardMode dashboardMode)
{
    if (dashboardMode == DashboardMode::Local) {
        QTC_ASSERT(dd->m_currentProjectInfo, return {}); // we should have a global one already

        const Storage<LocalDashboardAccess> serverAccessStorage;
        const Storage<GetDtoStorage<Dto::DashboardInfoDto>> dashboardStorage;
        const auto onLocalAuthorizationSetup = [serverAccessStorage] {
            std::optional<LocalDashboardAccess>
                    access = localDashboardAccessFor(dd->m_currentProjectInfo->name);
            if (!access)
                return SetupResult::StopWithError;
            *serverAccessStorage = *access;
            return SetupResult::Continue;
        };

        const auto onDashboardSetup = [serverAccessStorage, dashboardStorage] {
            dashboardStorage->credential = basicAuth(*serverAccessStorage);
            dashboardStorage->url = serverAccessStorage->url;
            return SetupResult::Continue;
        };

        const auto onDashboardDone = [serverAccessStorage, dashboardStorage](DoneWith result) {
            if (result != DoneWith::Success)
                return DoneResult::Error;  // should we handle this somehow?
            dd->m_localDashboard.emplace(*serverAccessStorage);
            dd->m_localDashboardInfo = toDashboardInfo(*dashboardStorage);
            return DoneResult::Success;
        };

        return {
            serverAccessStorage,
            onGroupSetup(onLocalAuthorizationSetup),
            Group {
                dashboardStorage,
                onGroupSetup(onDashboardSetup),
                dtoRecipe(dashboardStorage),
                onGroupDone(onDashboardDone)
            }
        };
    }

    const Id serverId = dd->m_dashboardServerId;
    const Storage<QUrl> serverUrlStorage;
    const Storage<GetDtoStorage<Dto::DashboardInfoDto>> unauthorizedDashboardStorage;
    const auto onUnauthorizedGroupSetup = [serverUrlStorage, unauthorizedDashboardStorage] {
        unauthorizedDashboardStorage->url = *serverUrlStorage;
        return isServerAccessEstablished(DashboardMode::Global) ? SetupResult::StopWithSuccess
                                                                : SetupResult::Continue;
    };
    const auto onUnauthorizedDashboard = [unauthorizedDashboardStorage, serverId] {
        if (unauthorizedDashboardStorage->dtoData) {
            const Dto::DashboardInfoDto &dashboardInfo = *unauthorizedDashboardStorage->dtoData;
            const QString &username = settings().serverForId(serverId).username;
            if (username.isEmpty()
                || (dashboardInfo.username && *dashboardInfo.username == username)) {
                dd->m_serverAccess = ServerAccess::NoAuthorization;
                dd->m_dashboardInfo = toDashboardInfo(*unauthorizedDashboardStorage);
                return;
            }
            MessageManager::writeFlashing(QString("Axivion: %1")
                .arg(Tr::tr("Unauthenticated access failed (wrong user), "
                            "using authenticated access...")));
        }
        dd->m_serverAccess = ServerAccess::WithAuthorization;
    };

    const auto onCredentialLoopCondition = [](int) {
        return dd->m_serverAccess == ServerAccess::WithAuthorization && !dd->m_apiToken;
    };
    const auto onGetCredentialSetup = [serverId](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Get);
        credential.setService(s_axivionKeychainService);
        credential.setKey(credentialKey(settings().serverForId(serverId)));
    };
    const auto onGetCredentialDone = [](const CredentialQuery &credential, DoneWith result) {
        if (result == DoneWith::Success)
            dd->m_apiToken = credential.data();
        else
            handleCredentialError(credential);
        // TODO: In case of an error we are multiplying the ApiTokens on Axivion server for each
        //       Creator run, but at least things should continue to work OK in the current session.
        return DoneResult::Success;
    };

    const Storage<QString> passwordStorage;
    const Storage<GetDtoStorage<Dto::DashboardInfoDto>> dashboardStorage;
    const auto onPasswordGroupSetup
            = [serverId, serverUrlStorage, passwordStorage, dashboardStorage] {
        if (dd->m_apiToken)
            return SetupResult::StopWithSuccess;

        bool ok = false;
        const AxivionServer server = settings().serverForId(serverId);
        const QString text(Tr::tr("Enter the password for:\nDashboard: %1\nUser: %2")
                               .arg(server.dashboard, server.username));
        *passwordStorage = QInputDialog::getText(ICore::dialogParent(),
            Tr::tr("Axivion Server Password"), text, QLineEdit::Password, {}, &ok);
        if (!ok)
            return SetupResult::StopWithError;

        const QString credential = server.username + ':' + *passwordStorage;
        dashboardStorage->credential = "Basic " + credential.toUtf8().toBase64();
        dashboardStorage->url = *serverUrlStorage;
        return SetupResult::Continue;
    };

    const Storage<PostDtoStorage<Dto::ApiTokenInfoDto>> apiTokenStorage;
    const auto onApiTokenGroupSetup = [passwordStorage, dashboardStorage, apiTokenStorage] {
        if (!dashboardStorage->dtoData)
            return SetupResult::StopWithSuccess;

        dd->m_dashboardInfo = toDashboardInfo(*dashboardStorage);

        const Dto::DashboardInfoDto &dashboardDto = *dashboardStorage->dtoData;
        if (!dashboardDto.userApiTokenUrl)
            return SetupResult::StopWithError;

        apiTokenStorage->credential = dashboardStorage->credential;
        apiTokenStorage->url = resolveDashboardInfoUrl(DashboardMode::Global,
                                                       *dashboardDto.userApiTokenUrl);
        apiTokenStorage->csrfToken = dashboardDto.csrfToken.toUtf8();
        const Dto::ApiTokenCreationRequestDto requestDto{*passwordStorage, "IdePlugin",
                                                         apiTokenDescription(), 0};
        apiTokenStorage->writeData = requestDto.serialize();
        apiTokenStorage->password = *passwordStorage;
        return SetupResult::Continue;
    };

    const auto onSetCredentialSetup = [apiTokenStorage, serverId](CredentialQuery &credential) {
        if (!apiTokenStorage->dtoData || !apiTokenStorage->dtoData->token)
            return SetupResult::StopWithSuccess;

        dd->m_apiToken = apiTokenStorage->dtoData->token->toUtf8();
        credential.setOperation(CredentialOperation::Set);
        credential.setService(s_axivionKeychainService);
        credential.setKey(credentialKey(settings().serverForId(serverId)));
        credential.setData(*dd->m_apiToken);
        return SetupResult::Continue;
    };
    const auto onSetCredentialDone = [](const CredentialQuery &credential) {
        handleCredentialError(credential);
        // TODO: In case of an error we are multiplying the ApiTokens on Axivion server for each
        //       Creator run, but at least things should continue to work OK in the current session.
        return DoneResult::Success;
    };

    const auto onDashboardGroupSetup = [serverUrlStorage, dashboardStorage] {
        if (dd->m_dashboardInfo || dd->m_serverAccess != ServerAccess::WithAuthorization
            || !dd->m_apiToken) {
            return SetupResult::StopWithSuccess; // Unauthorized access should have collect dashboard before
        }
        dashboardStorage->credential = "AxToken " + *dd->m_apiToken;
        dashboardStorage->url = *serverUrlStorage;
        return SetupResult::Continue;
    };
    const auto onDeleteCredentialSetup = [dashboardStorage, serverId](CredentialQuery &credential) {
        if (dashboardStorage->dtoData) {
            dd->m_dashboardInfo = toDashboardInfo(*dashboardStorage);
            return SetupResult::StopWithSuccess;
        }
        dd->m_apiToken = {};
        MessageManager::writeFlashing(QString("Axivion: %1")
            .arg(Tr::tr("The stored ApiToken is not valid anymore, removing it.")));
        credential.setOperation(CredentialOperation::Delete);
        credential.setService(s_axivionKeychainService);
        credential.setKey(credentialKey(settings().serverForId(serverId)));
        return SetupResult::Continue;
    };

    return {
        serverUrlStorage,
        onGroupSetup([serverUrlStorage, serverId] {
            *serverUrlStorage = QUrl(settings().serverForId(serverId).dashboard);
        }),
        Group {
            unauthorizedDashboardStorage,
            onGroupSetup(onUnauthorizedGroupSetup),
            dtoRecipe(unauthorizedDashboardStorage),
            Sync(onUnauthorizedDashboard),
            onGroupDone([serverUrlStorage, unauthorizedDashboardStorage] {
                *serverUrlStorage = unauthorizedDashboardStorage->url;
            }),
        },
        For (LoopUntil(onCredentialLoopCondition)) >> Do {
            CredentialQueryTask(onGetCredentialSetup, onGetCredentialDone),
            Group {
                passwordStorage,
                dashboardStorage,
                onGroupSetup(onPasswordGroupSetup),
                dtoRecipe(dashboardStorage) || successItem, // GET DashboardInfoDto
                Group { // POST ApiTokenCreationRequestDto, GET ApiTokenInfoDto.
                    apiTokenStorage,
                    onGroupSetup(onApiTokenGroupSetup),
                    dtoRecipe(apiTokenStorage),
                    CredentialQueryTask(onSetCredentialSetup, onSetCredentialDone, CallDoneIf::Error)
                }
            },
            Group {
                finishAllAndSuccess,
                dashboardStorage,
                onGroupSetup(onDashboardGroupSetup),
                dtoRecipe(dashboardStorage),
                CredentialQueryTask(onDeleteCredentialSetup)
            }
        }
    };
}

template<typename DtoType>
static Group fetchDataRecipe(DashboardMode dashboardMode, const QUrl &url,
                             const std::function<void(const DtoType &)> &handler)
{
    const Storage<GetDtoStorage<DtoType>> dtoStorage;

    const auto onDtoSetup = [dtoStorage, dashboardMode, url] {
        if (!isServerAccessEstablished(dashboardMode))
            return SetupResult::StopWithError;

        if (dashboardMode == DashboardMode::Global) {
            if (dd->m_serverAccess == ServerAccess::WithAuthorization && dd->m_apiToken)
                dtoStorage->credential = "AxToken " + *dd->m_apiToken;
        } else {
            dtoStorage->credential = basicAuth(*dd->m_localDashboard);
        }
        dtoStorage->url = url;
        return SetupResult::Continue;
    };
    const auto onDtoDone = [dtoStorage, handler] {
        if (dtoStorage->dtoData)
            handler(*dtoStorage->dtoData);
    };

    const Group recipe {
        authorizationRecipe(dashboardMode),
        Group {
            dtoStorage,
            onGroupSetup(onDtoSetup),
            dtoRecipe(dtoStorage),
            onGroupDone(onDtoDone)
        }
    };
    return recipe;
}

static std::optional<DashboardInfo> &dashboardInfo(DashboardMode dashboardMode)
{
   return (dashboardMode == DashboardMode::Global) ?  dd->m_dashboardInfo
                                                    : dd->m_localDashboardInfo;
}

Group dashboardInfoRecipe(DashboardMode dashboardMode, const DashboardInfoHandler &handler)
{
    const auto onSetup = [dashboardMode, handler] {
        if (auto info = dashboardInfo(dashboardMode)) {
            if (handler)
                handler(*info);
            return SetupResult::StopWithSuccess;
        }

        dd->m_networkAccessManager.setCookieJar(new QNetworkCookieJar); // remove old cookies
        return SetupResult::Continue;
    };

    const auto onDone = [dashboardMode, handler] {
        if (!handler)
            return;
        if (auto info = dashboardInfo(dashboardMode))
            handler(*info);
        else
            handler(ResultError("Error")); // TODO: Collect error message in the storage.
    };

    const Group root {
        onGroupSetup(onSetup), // Stops if cache exists.
        authorizationRecipe(dashboardMode),
        handler ? onGroupDone(onDone) : nullItem
    };
    return root;
}

Group projectInfoRecipe(DashboardMode dashboardMode, const QString &projectName)
{
    const auto onSetup = [dashboardMode, projectName] {
        dd->clearAllMarks();
        if (dashboardMode == DashboardMode::Global)
            dd->m_currentProjectInfo = {};
        else
            dd->m_currentLocalProjectInfo = {};
        dd->m_analysisVersion = {};
    };

    const auto onTaskTreeSetup = [dashboardMode, projectName](TaskTree &taskTree) {
        const bool globalFail = dashboardMode == DashboardMode::Global && !dd->m_dashboardInfo;
        const bool localFail = dashboardMode == DashboardMode::Local && !dd->m_localDashboardInfo;
        if (globalFail || localFail) {
                MessageManager::writeDisrupting(
                            QString("Axivion: %1").arg(dashboardMode == DashboardMode::Global
                                                       ? Tr::tr("Fetching DashboardInfo error.")
                                                       : Tr::tr("Fetching local DashboardInfo error.")));
                return SetupResult::StopWithError;
        }
        const bool noProjects = (dashboardMode == DashboardMode::Global
                                 && dd->m_dashboardInfo->projects.isEmpty())
                || (dashboardMode == DashboardMode::Local
                    && dd->m_localDashboardInfo->projects.isEmpty());
        if (noProjects) {
            updateDashboard();
            return SetupResult::StopWithSuccess;
        }

        const auto handler = [dashboardMode](const Dto::ProjectInfoDto &data) {
            if (dashboardMode == DashboardMode::Global) {
                dd->m_currentProjectInfo = data;
                if (!dd->m_currentProjectInfo->versions.empty())
                    setAnalysisVersion(dd->m_currentProjectInfo->versions.back().date);
            } else {
                dd->m_currentLocalProjectInfo = data;
                if (!dd->m_currentLocalProjectInfo->versions.empty())
                    setAnalysisVersion(dd->m_currentLocalProjectInfo->versions.back().date);
            }
            updateDashboard();
            dd->handleOpenedDocs();
        };

        if (dashboardMode == DashboardMode::Global) {
            const QString targetProjectName = projectName.isEmpty()
                    ? dd->m_dashboardInfo->projects.first() : projectName;
            auto it = dd->m_dashboardInfo->projectUrls.constFind(targetProjectName);
            if (it == dd->m_dashboardInfo->projectUrls.constEnd())
                it = dd->m_dashboardInfo->projectUrls.constBegin();
            taskTree.setRecipe(fetchDataRecipe<Dto::ProjectInfoDto>(dashboardMode,
                                                                    resolveDashboardInfoUrl(dashboardMode, *it),
                                                                    handler));
        } else {
            auto it = dd->m_localDashboardInfo->projectUrls.constFind(projectName);
            if (it == dd->m_localDashboardInfo->projectUrls.constEnd())
                it = dd->m_localDashboardInfo->projectUrls.constBegin();
            taskTree.setRecipe(fetchDataRecipe<Dto::ProjectInfoDto>(dashboardMode,
                                                                    resolveDashboardInfoUrl(dashboardMode, *it),
                                                                    handler));
        }
        return SetupResult::Continue;
    };

    return {
        onGroupSetup(onSetup),
        TaskTreeTask(onTaskTreeSetup)
    };
}

Group issueTableRecipe(DashboardMode dashboardMode, const IssueListSearch &search,
                       const IssueTableHandler &handler)
{
    QTC_ASSERT(dd->m_currentProjectInfo, return {}); // TODO: Call handler with unexpected?

    const QUrlQuery query = search.toUrlQuery(QueryMode::FullQuery);
    if (query.isEmpty())
        return {}; // TODO: Call handler with unexpected?

    const QUrl url = constructUrl(dashboardMode, dd->m_currentProjectInfo->name, "issues", query);
    return fetchDataRecipe<Dto::IssueTableDto>(dashboardMode, url, handler);
}

Group lineMarkerRecipe(DashboardMode dashboardMode, const FilePath &filePath,
                       const LineMarkerHandler &handler)
{
    QTC_ASSERT(dd->m_currentProjectInfo, return {}); // TODO: Call handler with unexpected?
    QTC_ASSERT(!filePath.isEmpty(), return {}); // TODO: Call handler with unexpected?

    const QString fileName = QString::fromUtf8(QUrl::toPercentEncoding(filePath.path()));
    const QUrlQuery query({{"filename", fileName}});
    const QUrl url = constructUrl(dashboardMode, dd->m_currentProjectInfo->name, "files", query);
    return fetchDataRecipe<Dto::FileViewDto>(dashboardMode, url, handler);
}

void AxivionPluginPrivate::fetchLocalDashboardInfo(const DashboardInfoHandler &handler,
                                                   const QString &projectName)
{
    m_taskTreeRunner.start({dashboardInfoRecipe(DashboardMode::Local, handler),
                            projectInfoRecipe(DashboardMode::Local, projectName)});
}

void AxivionPluginPrivate::fetchDashboardAndProjectInfo(const DashboardInfoHandler &handler,
                                                        const QString &projectName)
{
    m_taskTreeRunner.start({dashboardInfoRecipe(DashboardMode::Global, handler),
                            projectInfoRecipe(DashboardMode::Global, projectName)});
}

Group tableInfoRecipe(DashboardMode dashboardMode, const QString &prefix,
                      const TableInfoHandler &handler)
{
    QTC_ASSERT(dd->m_currentProjectInfo, return {});
    const QUrlQuery query({{"kind", prefix}});
    const QUrl url = constructUrl(dashboardMode, dd->m_currentProjectInfo->name, "issues_meta", query);
    return fetchDataRecipe<Dto::TableInfoDto>(dashboardMode, url, handler);
}

void AxivionPluginPrivate::fetchIssueInfo(DashboardMode dashboardMode, const QString &id)
{
    if (!m_currentProjectInfo || !dd->m_analysisVersion)
        return;

    const QUrl url = constructUrl(dashboardMode,
                                  dd->m_currentProjectInfo->name,
                                  QString("issues/" + id + "/properties/"),
                                  {{"version", *dd->m_analysisVersion}});

    const Storage<DownloadData> storage;

    const auto onSetup = [storage, url] { storage->inputUrl = url; };

    const auto onDone = [storage] {
        QByteArray fixedHtml = storage->outputData;
        const int idx = fixedHtml.indexOf("<div class=\"ax-issuedetails-table-container\">");
        if (idx >= 0)
            fixedHtml = "<html><body>" + fixedHtml.mid(idx);
        updateIssueDetails(QString::fromUtf8(fixedHtml));
    };

    m_issueInfoRunner.start({
        storage,
        onGroupSetup(onSetup),
        downloadDataRecipe(dashboardMode, storage),
        onGroupDone(onDone, CallDoneIf::Success)
    });
}

static QList<Dto::NamedFilterInfoDto> extractNamedFiltersFromJsonArray(const QByteArray &json)
{
    QList<Dto::NamedFilterInfoDto> result;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError)
        return result;
    if (!doc.isArray())
        return result;
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonDocument objDocument(value.toObject());
        const auto filter = Dto::NamedFilterInfoDto::deserializeExpected(objDocument.toJson());
        if (filter)
            result.append(*filter);
    }
    return result;
}

void AxivionPluginPrivate::fetchNamedFilters(DashboardMode dashboardMode)
{
    QTC_ASSERT(m_dashboardInfo, return);

    // use simple downloadDatarecipe() as we cannot handle an array of a dto at the moment
    const Storage<DownloadData> globalStorage;
    const Storage<DownloadData> userStorage;

    const auto onSetup = [globalStorage, userStorage, dashboardMode] {
        auto info = dashboardInfo(dashboardMode);
        QTC_ASSERT(info, return);
        globalStorage->inputUrl = info->globalNamedFilters
                ? info->source.resolved(*info->globalNamedFilters) : QUrl();
        userStorage->inputUrl = info->userNamedFilters
                ? info->source.resolved(*info->userNamedFilters) : QUrl();

        globalStorage->expectedContentType = ContentType::Json;
        userStorage->expectedContentType = ContentType::Json;
    };
    const auto onDone = [this, globalStorage, userStorage] {
        m_globalNamedFilters = extractNamedFiltersFromJsonArray(globalStorage->outputData);
        m_userNamedFilters = extractNamedFiltersFromJsonArray(userStorage->outputData);
        updateNamedFilters();
    };

    Group namedFiltersGroup = Group {
            globalStorage,
            userStorage,
            onGroupSetup(onSetup),
            downloadDataRecipe(dashboardMode, globalStorage) || successItem,
            downloadDataRecipe(dashboardMode, userStorage) || successItem,
            onGroupDone(onDone)
    };

    m_namedFilterRunner.start(namedFiltersGroup);
}

void AxivionPluginPrivate::handleOpenedDocs()
{
    const QList<IDocument *> openDocuments = DocumentModel::openedDocuments();
    for (IDocument *doc : openDocuments)
        onDocumentOpened(doc);
}

void AxivionPluginPrivate::clearAllMarks()
{
    for (const QSet<TextMark *> &marks : std::as_const(m_allMarks))
       qDeleteAll(marks);
    m_allMarks.clear();
}

void AxivionPluginPrivate::updateExistingMarks() // update whether highlight marks or not
{
    static Theme::Color color = Theme::Color(Theme::Bookmarks_TextMarkColor); // FIXME!
    const bool colored = settings().highlightMarks();

    auto changeColor = colored ? [](TextMark *mark) { mark->setColor(color); }
                               : [](TextMark *mark) { mark->unsetColor(); };

    for (const QSet<TextMark *> &marksForFile : std::as_const(m_allMarks)) {
        for (auto mark : marksForFile)
            changeColor(mark);
    }
}

void AxivionPluginPrivate::onDocumentOpened(IDocument *doc)
{
    if (!m_inlineIssuesEnabled)
        return;

    if (!doc || !m_currentProjectInfo)
        return;

    const FilePath docFilePath = doc->filePath();
    if (m_allMarks.contains(docFilePath)) // FIXME local vs global dashboard
        return;

    FilePath filePath = settings().mappedFilePath(docFilePath, m_currentProjectInfo->name);
    if (filePath.isEmpty() && m_project && m_project->isKnownFile(docFilePath))
        filePath = docFilePath.relativeChildPath(m_project->projectDirectory());

    if (filePath.isEmpty())
        return;

    const auto handler = [this, docFilePath](const Dto::FileViewDto &data) {
        if (data.lineMarkers.empty())
            return;
        handleIssuesForFile(data, docFilePath);
    };
    TaskTree *taskTree = new TaskTree;
    const bool useGlobal = m_dashboardMode == DashboardMode::Global
            || !currentIssueHasValidPathMapping();
    taskTree->setRecipe(lineMarkerRecipe(useGlobal ? DashboardMode::Global
                                                   : DashboardMode::Local, filePath, handler));
    m_docMarksTrees.insert_or_assign(doc, std::unique_ptr<TaskTree>(taskTree));
    connect(taskTree, &TaskTree::done, this, [this, doc] {
        const auto it = m_docMarksTrees.find(doc);
        QTC_ASSERT(it != m_docMarksTrees.end(), return);
        it->second.release()->deleteLater();
        m_docMarksTrees.erase(it);
    });
    taskTree->start();
}

void AxivionPluginPrivate::onDocumentClosed(IDocument *doc)
{
    const auto document = qobject_cast<TextDocument *>(doc);
    if (!document)
        return;

    const auto it = m_docMarksTrees.find(doc);
    if (it != m_docMarksTrees.end())
        m_docMarksTrees.erase(it);

    qDeleteAll(m_allMarks.take(document->filePath()));
}

void AxivionPluginPrivate::handleIssuesForFile(const Dto::FileViewDto &fileView,
                                               const FilePath &filePath)
{
    if (fileView.lineMarkers.empty())
        return;

    std::optional<Theme::Color> color = std::nullopt;
    if (settings().highlightMarks())
        color.emplace(Theme::Color(Theme::Bookmarks_TextMarkColor)); // FIXME!
    for (const Dto::LineMarkerDto &marker : std::as_const(fileView.lineMarkers)) {
        // FIXME the line location can be wrong (even the whole issue could be wrong)
        // depending on whether this line has been changed since the last axivion run and the
        // current state of the file - some magic has to happen here
        m_allMarks[filePath] << new AxivionTextMark(filePath, marker, color);
    }
}

void AxivionPluginPrivate::enableInlineIssues(bool enable)
{
    if (m_inlineIssuesEnabled == enable)
        return;
    m_inlineIssuesEnabled = enable;

    if (enable && m_dashboardServerId.isValid())
        handleOpenedDocs();
    else
        clearAllMarks();
}

void AxivionPluginPrivate::switchDashboardMode(DashboardMode mode, bool byLocalBuildButton)
{
    if (m_dashboardMode == mode)
        return;
    m_dashboardMode = mode;
    leaveOrEnterDashboardMode(byLocalBuildButton);
}

static constexpr char SV_PROJECTNAME[] = "Axivion.ProjectName";
static constexpr char SV_DASHBOARDID[] = "Axivion.DashboardId";

void AxivionPluginPrivate::onSessionLoaded(const QString &sessionName)
{
    // explicitly ignore default session to avoid triggering dialogs at startup
    if (sessionName == "default")
        return;

    const QString projectName = SessionManager::sessionValue(SV_PROJECTNAME).toString();
    const Id dashboardId = Id::fromSetting(SessionManager::sessionValue(SV_DASHBOARDID));
    if (!dashboardId.isValid())
        switchActiveDashboardId({});
    else if (activeDashboardId() != dashboardId)
        switchActiveDashboardId(dashboardId);
    reinitDashboard(projectName);
}

void AxivionPluginPrivate::onAboutToSaveSession()
{
    // explicitly ignore default session
    if (SessionManager::startupSession() == "default")
        return;

    SessionManager::setSessionValue(SV_DASHBOARDID, activeDashboardId().toSetting());
    const QString projectName = m_currentProjectInfo ? m_currentProjectInfo->name : QString();
    SessionManager::setSessionValue(SV_PROJECTNAME, projectName);
}

class AxivionPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Axivion.json")

    ~AxivionPlugin() final
    {
        delete dd;
        dd = nullptr;
    }

    void initialize() final
    {
        IOptionsPage::registerCategory(
            "XY.Axivion", Tr::tr("Axivion"), ":/axivion/images/axivion.png");

        setupAxivionPerspective();

        dd = new AxivionPluginPrivate;

        connect(ProjectManager::instance(), &ProjectManager::startupProjectChanged,
                dd, &AxivionPluginPrivate::onStartupProjectChanged);
        connect(EditorManager::instance(), &EditorManager::documentOpened,
                dd, &AxivionPluginPrivate::onDocumentOpened);
        connect(EditorManager::instance(), &EditorManager::documentClosed,
                dd, &AxivionPluginPrivate::onDocumentClosed);
    }

    ShutdownFlag aboutToShutdown() final
    {
        if (shutdownAllLocalDashboards([this] { emit asynchronousShutdownFinished(); }))
            return AsynchronousShutdown;
        else
            return SynchronousShutdown;
    }
};

void fetchIssueInfo(DashboardMode dashboardMode, const QString &id)
{
    QTC_ASSERT(dd, return);
    dd->fetchIssueInfo(dashboardMode, id);
}

void switchActiveDashboardId(const Id &toDashboardId)
{
    QTC_ASSERT(dd, return);
    dd->m_dashboardServerId = toDashboardId;
    dd->m_serverAccess = ServerAccess::Unknown;
    dd->m_apiToken.reset();
    dd->m_dashboardInfo.reset();
    dd->m_localDashboard.reset();
    dd->m_localDashboardInfo.reset();
    dd->m_currentProjectInfo.reset();
    dd->m_globalNamedFilters.clear();
    dd->m_userNamedFilters.clear();
    updateNamedFilters();
}

const std::optional<DashboardInfo> currentDashboardInfo()
{
    QTC_ASSERT(dd, return std::nullopt);
    return dd->m_dashboardInfo;
}

const Id activeDashboardId()
{
    QTC_ASSERT(dd, return {});
    return dd->m_dashboardServerId;
}

void setAnalysisVersion(const QString &version)
{
    QTC_ASSERT(dd, return);
    if (dd->m_analysisVersion.value_or("") == version)
        return;
    dd->m_analysisVersion = version;
}

void enableInlineIssues(bool enable)
{
    QTC_ASSERT(dd, return);
    dd->enableInlineIssues(enable);
}

Utils::FilePath findFileForIssuePath(const Utils::FilePath &issuePath)
{
    QTC_ASSERT(dd, return {});
    if (!dd->m_project || !dd->m_currentProjectInfo)
        return {};
    const FilePaths result = dd->m_fileFinder.findFile(issuePath.toUrl());
    if (result.size() == 1)
        return dd->m_project->projectDirectory().resolvePath(result.first());
    return {};
}

void switchDashboardMode(DashboardMode mode, bool byLocalBuildButton)
{
    QTC_ASSERT(dd, return);
    dd->switchDashboardMode(mode, byLocalBuildButton);
}

DashboardMode currentDashboardMode()
{
    QTC_ASSERT(dd, return DashboardMode::Global);
    return dd->m_dashboardMode;
}

void updateEnvironmentForLocalBuild(Environment *env)
{
    QTC_ASSERT(env, return);
    QTC_ASSERT(dd, return);
    QTC_ASSERT(dd->m_dashboardInfo && dd->m_currentProjectInfo, return);
    if (!dd->m_apiToken)
        return;

    QJsonObject json;
    json.insert("apiToken", QString::fromUtf8(*dd->m_apiToken));
    const QJsonDocument doc(json);
    QByteArray bytes = doc.toJson(QJsonDocument::Compact);
    if (bytes.size() < 256)
        bytes.append(256 - bytes.size(), 0x20);
    QTC_ASSERT(bytes.size() >= 256, qDebug() << bytes.size(); return);
    QRandomGenerator *gen = QRandomGenerator::global();
    QByteArray key;
    key.reserve(bytes.size());
    for (int i = 0, end = bytes.size(); i < end; ++i)
        key.append(gen->bounded(0, 256) & 0xFF);

    QTC_ASSERT(bytes.size() == key.size(), return);
    QByteArray xored;
    xored.reserve(bytes.size());
    for (int i = 0, end = bytes.size(); i < end; ++i)
        xored.append(bytes.at(i) ^ key.at(i));

    // write key to file
    TemporaryFile keyFile("axivion-XXXXXX");
    keyFile.setAutoRemove(false);
    if (!keyFile.open())
        return;
    if (!keyFile.write(key))
        return;
    keyFile.close();
    // set environment variables
    env->set("AXIVION_PASSFILE", keyFile.fileName());
    env->set("AXIVION_PASSWORD", QString::fromUtf8(xored.toBase64()));
    env->set("AXIVION_DASHBOARD_URL", dd->m_dashboardInfo->source.toString());
    if (dd->m_dashboardInfo->userName)
        env->set("AXIVION_USERNAME", *dd->m_dashboardInfo->userName);
    env->set("AXIVION_LOCAL_BUILD", "1");
    const QString ua = QString("Axivion" + QCoreApplication::applicationName()
                               + "Plugin/" + QCoreApplication::applicationVersion());
    env->set("AXIVION_USER_AGENT", ua);
    env->set("AXIVION_PROJECT_NAME", dd->m_currentProjectInfo->name);
}

} // Axivion::Internal

#include "axivionplugin.moc"
