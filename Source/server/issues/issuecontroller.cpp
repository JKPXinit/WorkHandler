#include "issues/issuecontroller.h"

#include "api/apicontext.h"
#include "api/multipartparser.h"
#include "issues/issueidentifier.h"
#include "issues/issueservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;
constexpr qsizetype MaximumIssueBodySize = 30 * 1024 * 1024 + 256 * 1024;

std::optional<QString> queryItem(const QUrlQuery &query,
                                 const QString &name)
{
    return query.hasQueryItem(name)
        ? std::optional<QString>(query.queryItemValue(name))
        : std::nullopt;
}

IssueListInput listInput(const QHttpServerRequest &request)
{
    const QUrlQuery query(request.url());
    IssueListInput input;
    input.blockId = queryItem(query, QStringLiteral("block_id"));
    input.status = queryItem(query, QStringLiteral("status"));
    input.priority = queryItem(query, QStringLiteral("priority"));
    input.assigneeId = queryItem(query, QStringLiteral("assignee_id"));
    input.search = queryItem(query, QStringLiteral("q"));
    input.sort = queryItem(query, QStringLiteral("sort"));
    return input;
}

QHttpServerResponse listResponse(const IssueServiceResult &result)
{
    QJsonArray issues;
    for (const IssueRecord &issue : result.issues) {
        issues.append(issue.toJson());
    }
    return ApiContext::successResponse({
        {QStringLiteral("issues"), issues}
    });
}

QHttpServerResponse attachmentListResponse(const IssueServiceResult &result)
{
    QJsonArray attachments;
    for (const AttachmentRecord &attachment : result.attachments) {
        attachments.append(attachment.toJson());
    }
    return ApiContext::successResponse({
        {QStringLiteral("attachments"), attachments}
    });
}

bool parseIssueIdentifier(const QString &identifier, qint64 *issueId)
{
    return IssueIdentifier::parse(identifier, issueId);
}

QHttpServerResponse invalidTaskIdResponse()
{
    return ApiContext::errorResponse(
        StatusCode::BadRequest,
        QStringLiteral("invalid_task_id"),
        QStringLiteral("Issue identifier must be T followed by a positive integer or a legacy positive integer."));
}
}

IssueController::IssueController(ApiContext &apiContext, IssueService &service)
    : m_apiContext(apiContext)
    , m_service(service)
{
}

void IssueController::registerRoutes(QHttpServer &server)
{
    using Method = QHttpServerRequest::Method;

    server.route(
        QStringLiteral("/api/issues"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const IssueServiceResult result = m_service.list(listInput(request));
            return result.ok() ? listResponse(result) : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/blocks/<arg>/issues"), Method::Get,
        [this](qint64 blockId, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            IssueListInput input = listInput(request);
            input.blockId = QString::number(blockId);
            input.requireBlock = true;
            const IssueServiceResult result = m_service.list(input);
            return result.ok() ? listResponse(result) : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>"), Method::Get,
        [this](const QString &identifier, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            qint64 id = 0;
            if (!parseIssueIdentifier(identifier, &id)) {
                return invalidTaskIdResponse();
            }

            const IssueServiceResult result = m_service.get(id);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("issue"), result.issue.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>/attachments"), Method::Get,
        [this](const QString &identifier, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            qint64 id = 0;
            if (!parseIssueIdentifier(identifier, &id)) {
                return invalidTaskIdResponse();
            }
            const IssueServiceResult result = m_service.descriptionAttachments(id);
            return result.ok() ? attachmentListResponse(result) : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues"), Method::Post,
        [this](const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const QByteArray contentType = request.value("Content-Type").toLower();
            IssueServiceResult result;
            if (contentType.startsWith(QByteArrayLiteral("multipart/form-data"))) {
                QJsonObject body;
                QList<MultipartFile> files;
                QList<qint64> removeAttachmentIds;
                QString parseError;
                if (!MultipartParser::parseAttachmentForm(
                        request, QByteArrayLiteral("issue"), MaximumIssueBodySize, 9, &body, &files,
                        &removeAttachmentIds, &parseError)
                    || files.isEmpty() || !removeAttachmentIds.isEmpty()) {
                    return ApiContext::errorResponse(
                        StatusCode::BadRequest,
                        QStringLiteral("invalid_multipart"),
                        parseError.isEmpty()
                            ? QStringLiteral("Issue creation multipart data is invalid.")
                            : parseError);
                }
                result = m_service.createWithAttachments(
                    body, files, authorization.user);
            } else {
                QJsonObject body;
                QString parseError;
                if (!ApiContext::parseJsonObject(request, &body, &parseError)) {
                    return ApiContext::errorResponse(
                        StatusCode::BadRequest,
                        QStringLiteral("invalid_json"), parseError);
                }
                result = m_service.create(body, authorization.user);
            }
            return result.ok()
                ? ApiContext::successResponse(
                      {{QStringLiteral("issue"), result.issue.toJson()}},
                      StatusCode::Created)
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>"), Method::Put,
        [this](const QString &identifier, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            qint64 id = 0;
            if (!parseIssueIdentifier(identifier, &id)) {
                return invalidTaskIdResponse();
            }

            const QByteArray contentType = request.value("Content-Type").toLower();
            IssueServiceResult result;
            if (contentType.startsWith(QByteArrayLiteral("multipart/form-data"))) {
                QJsonObject body;
                QList<MultipartFile> files;
                QList<qint64> removeAttachmentIds;
                QString parseError;
                if (!MultipartParser::parseAttachmentForm(
                        request, QByteArrayLiteral("issue"), MaximumIssueBodySize, 9, &body, &files,
                        &removeAttachmentIds, &parseError)) {
                    return ApiContext::errorResponse(
                        StatusCode::BadRequest,
                        QStringLiteral("invalid_multipart"), parseError);
                }
                result = m_service.updateWithAttachments(
                    id, body, files, removeAttachmentIds, authorization.user);
            } else {
                QJsonObject body;
                QString parseError;
                if (!ApiContext::parseJsonObject(request, &body, &parseError)) {
                    return ApiContext::errorResponse(
                        StatusCode::BadRequest,
                        QStringLiteral("invalid_json"), parseError);
                }
                result = m_service.update(id, body, authorization.user);
            }
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("issue"), result.issue.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>/status"), Method::Put,
        [this](const QString &identifier, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            qint64 id = 0;
            if (!parseIssueIdentifier(identifier, &id)) {
                return invalidTaskIdResponse();
            }

            QJsonObject body;
            QString parseError;
            if (!ApiContext::parseJsonObject(request, &body, &parseError)) {
                return ApiContext::errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("invalid_json"), parseError);
            }
            const IssueServiceResult result = m_service.changeStatus(
                id, body, authorization.user);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("issue"), result.issue.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>"), Method::Delete,
        [this](const QString &identifier, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            qint64 id = 0;
            if (!parseIssueIdentifier(identifier, &id)) {
                return invalidTaskIdResponse();
            }

            const IssueServiceResult result = m_service.remove(
                id, authorization.user);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("deleted_id"), result.deletedId}
                  })
                : errorResponse(result);
        });
}

QHttpServerResponse IssueController::errorResponse(
    const IssueServiceResult &result)
{
    StatusCode status = StatusCode::InternalServerError;
    switch (result.error) {
    case IssueServiceError::InvalidInput:
        status = StatusCode::BadRequest;
        break;
    case IssueServiceError::NotFound:
        status = StatusCode::NotFound;
        break;
    case IssueServiceError::Forbidden:
        status = StatusCode::Forbidden;
        break;
    case IssueServiceError::Conflict:
        status = StatusCode::Conflict;
        break;
    case IssueServiceError::Database:
    case IssueServiceError::Storage:
    case IssueServiceError::None:
        break;
    }
    return ApiContext::errorResponse(status, result.code, result.message);
}
