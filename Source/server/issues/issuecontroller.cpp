#include "issues/issuecontroller.h"

#include "api/apicontext.h"
#include "issues/issueservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;

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
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const IssueServiceResult result = m_service.get(id);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("issue"), result.issue.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues"), Method::Post,
        [this](const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            QJsonObject body;
            QString parseError;
            if (!ApiContext::parseJsonObject(request, &body, &parseError)) {
                return ApiContext::errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("invalid_json"), parseError);
            }
            const IssueServiceResult result = m_service.create(
                body, authorization.user);
            return result.ok()
                ? ApiContext::successResponse(
                      {{QStringLiteral("issue"), result.issue.toJson()}},
                      StatusCode::Created)
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>"), Method::Put,
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            QJsonObject body;
            QString parseError;
            if (!ApiContext::parseJsonObject(request, &body, &parseError)) {
                return ApiContext::errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("invalid_json"), parseError);
            }
            const IssueServiceResult result = m_service.update(
                id, body, authorization.user);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("issue"), result.issue.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/issues/<arg>/status"), Method::Put,
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
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
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
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
    case IssueServiceError::None:
        break;
    }
    return ApiContext::errorResponse(status, result.code, result.message);
}
