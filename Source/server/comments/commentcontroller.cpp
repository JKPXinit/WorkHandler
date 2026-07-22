#include "comments/commentcontroller.h"

#include "api/apicontext.h"
#include "api/multipartparser.h"
#include "comments/commentservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonObject>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;
constexpr qsizetype MaximumCommentBodySize = 30 * 1024 * 1024 + 256 * 1024;
}

CommentController::CommentController(ApiContext &apiContext,
                                     CommentService &service)
    : m_apiContext(apiContext)
    , m_service(service)
{
}

void CommentController::registerRoutes(QHttpServer &server)
{
    using Method = QHttpServerRequest::Method;

    server.route(
        QStringLiteral("/api/issues/<arg>/comments"), Method::Get,
        [this](qint64 issueId, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const CommentServiceResult result = m_service.list(issueId);
            if (!result.ok()) {
                return errorResponse(result);
            }
            QJsonArray comments;
            for (const CommentRecord &comment : result.comments) {
                comments.append(comment.toJson());
            }
            return ApiContext::successResponse({
                {QStringLiteral("comments"), comments}
            });
        });

    server.route(
        QStringLiteral("/api/issues/<arg>/comments"), Method::Post,
        [this](qint64 issueId, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const QByteArray contentType = request.value("Content-Type").toLower();
            CommentServiceResult result;
            if (contentType.startsWith(
                    QByteArrayLiteral("multipart/form-data"))) {
                QString content;
                QList<MultipartFile> files;
                QString parseError;
                if (!MultipartParser::parseComment(
                        request, MaximumCommentBodySize, 9,
                        &content, &files, &parseError)) {
                    return ApiContext::errorResponse(
                        StatusCode::BadRequest,
                        QStringLiteral("invalid_multipart"), parseError);
                }
                result = m_service.createWithImages(
                    issueId, content, files, authorization.user);
            } else {
                QJsonObject body;
                QString parseError;
                if (!ApiContext::parseJsonObject(request, &body, &parseError)) {
                    return ApiContext::errorResponse(
                        StatusCode::BadRequest,
                        QStringLiteral("invalid_json"), parseError);
                }
                result = m_service.create(
                    issueId, body, authorization.user);
            }
            return result.ok()
                ? ApiContext::successResponse(
                      {{QStringLiteral("comment"), result.comment.toJson()}},
                      StatusCode::Created)
                : errorResponse(result);
        });
}

QHttpServerResponse CommentController::errorResponse(
    const CommentServiceResult &result)
{
    StatusCode status = StatusCode::InternalServerError;
    switch (result.error) {
    case CommentServiceError::InvalidInput:
        status = StatusCode::BadRequest;
        break;
    case CommentServiceError::NotFound:
        status = StatusCode::NotFound;
        break;
    case CommentServiceError::Forbidden:
        status = StatusCode::Forbidden;
        break;
    case CommentServiceError::Conflict:
        status = StatusCode::Conflict;
        break;
    case CommentServiceError::Database:
    case CommentServiceError::None:
        break;
    }
    return ApiContext::errorResponse(status, result.code, result.message);
}
