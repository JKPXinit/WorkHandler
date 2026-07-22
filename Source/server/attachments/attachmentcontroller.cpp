#include "attachments/attachmentcontroller.h"

#include "api/apicontext.h"
#include "attachments/attachmentservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QUrlQuery>

namespace {
using StatusCode = QHttpServerResponse::StatusCode;
}

AttachmentController::AttachmentController(ApiContext &apiContext,
                                           AttachmentService &service)
    : m_apiContext(apiContext)
    , m_service(service)
{
}

void AttachmentController::registerRoutes(QHttpServer &server)
{
    using Method = QHttpServerRequest::Method;

    server.route(
        QStringLiteral("/api/attachments/<arg>"), Method::Get,
        [this](qint64 id, const QHttpServerRequest &request) {
            const ApiContext::AuthorizationResult authorization =
                m_apiContext.authorize(request, false);
            if (!authorization.authorized) {
                return ApiContext::authorizationError(authorization);
            }

            const QString size = QUrlQuery(request.url()).queryItemValue(
                QStringLiteral("size"));
            if (!size.isEmpty() && size != QStringLiteral("thumb")) {
                return ApiContext::errorResponse(
                    StatusCode::BadRequest,
                    QStringLiteral("invalid_attachment_size"),
                    QStringLiteral("Attachment size must be thumb or omitted."));
            }
            const AttachmentServiceResult result = m_service.read(
                id, size == QStringLiteral("thumb"));
            if (!result.ok()) {
                return errorResponse(result);
            }
            QHttpServerResponse response(
                QByteArrayLiteral("image/webp"), result.fileData, StatusCode::Ok);
            ApiContext::addCorsHeaders(&response);
            return response;
        });

}

QHttpServerResponse AttachmentController::errorResponse(
    const AttachmentServiceResult &result)
{
    StatusCode status = StatusCode::InternalServerError;
    switch (result.error) {
    case AttachmentServiceError::InvalidInput:
        status = StatusCode::BadRequest;
        break;
    case AttachmentServiceError::NotFound:
        status = StatusCode::NotFound;
        break;
    case AttachmentServiceError::Forbidden:
        status = StatusCode::Forbidden;
        break;
    case AttachmentServiceError::Conflict:
        status = StatusCode::Conflict;
        break;
    case AttachmentServiceError::Database:
    case AttachmentServiceError::Storage:
    case AttachmentServiceError::None:
        break;
    }
    return ApiContext::errorResponse(status, result.code, result.message);
}
