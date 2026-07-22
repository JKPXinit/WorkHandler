#include "blocks/blockcontroller.h"

#include "api/apicontext.h"
#include "blocks/blockservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonObject>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;
}

BlockController::BlockController(ApiContext &apiContext, BlockService &service)
    : m_apiContext(apiContext)
    , m_service(service)
{
}

void BlockController::registerRoutes(QHttpServer &server)
{
    using Method = QHttpServerRequest::Method;

    server.route(
        QStringLiteral("/api/blocks"), Method::Get,
        [this](const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const BlockServiceResult result = m_service.list();
            if (!result.ok()) {
                return errorResponse(result);
            }
            QJsonArray blocks;
            for (const BlockRecord &block : result.blocks) {
                blocks.append(block.toJson());
            }
            return ApiContext::successResponse({
                {QStringLiteral("blocks"), blocks}
            });
        });

    server.route(
        QStringLiteral("/api/blocks/<arg>"), Method::Get,
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const BlockServiceResult result = m_service.get(id);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("block"), result.block.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/blocks"), Method::Post,
        [this](const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, true);
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
            const BlockServiceResult result = m_service.create(body);
            return result.ok()
                ? ApiContext::successResponse(
                      {{QStringLiteral("block"), result.block.toJson()}},
                      StatusCode::Created)
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/blocks/<arg>"), Method::Put,
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, true);
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
            const BlockServiceResult result = m_service.update(id, body);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("block"), result.block.toJson()}
                  })
                : errorResponse(result);
        });

    server.route(
        QStringLiteral("/api/blocks/<arg>"), Method::Delete,
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, true);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const BlockServiceResult result = m_service.remove(id);
            return result.ok()
                ? ApiContext::successResponse({
                      {QStringLiteral("deleted_id"), result.deletedId}
                  })
                : errorResponse(result);
        });
}

QHttpServerResponse BlockController::errorResponse(
    const BlockServiceResult &result)
{
    StatusCode status = StatusCode::InternalServerError;
    switch (result.error) {
    case BlockServiceError::InvalidInput:
        status = StatusCode::BadRequest;
        break;
    case BlockServiceError::NotFound:
        status = StatusCode::NotFound;
        break;
    case BlockServiceError::Conflict:
        status = StatusCode::Conflict;
        break;
    case BlockServiceError::Database:
    case BlockServiceError::None:
        break;
    }
    return ApiContext::errorResponse(status, result.code, result.message);
}
