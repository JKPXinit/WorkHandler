#include "attachments/attachmentcontroller.h"

#include "api/apicontext.h"
#include "attachments/attachmentservice.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpHeaders>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

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
            const QByteArray contentType = result.attachment.contentType.isEmpty()
                ? QByteArrayLiteral("application/octet-stream")
                : result.attachment.contentType.toUtf8();
            QHttpServerResponse response(contentType, result.fileData, StatusCode::Ok);
            QHttpHeaders headers = response.headers();
            headers.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
                           QStringLiteral("attachment; filename*=UTF-8''%1")
                               .arg(QString::fromUtf8(QUrl::toPercentEncoding(
                                   result.attachment.filename))));
            response.setHeaders(std::move(headers));
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
