#include "issues/issueidentifier.h"

namespace IssueIdentifier {

QString format(qint64 issueId)
{
    return issueId > 0
        ? QStringLiteral("T%1").arg(issueId)
        : QString();
}

bool parse(const QString &identifier, qint64 *issueId)
{
    QString digits = identifier;
    if (digits.startsWith(QLatin1Char('T'), Qt::CaseInsensitive)) {
        digits.remove(0, 1);
    }
    if (digits.isEmpty()) {
        return false;
    }

    bool ok = false;
    const qint64 parsed = digits.toLongLong(&ok);
    if (!ok || parsed <= 0 || QString::number(parsed) != digits) {
        return false;
    }
    if (issueId) {
        *issueId = parsed;
    }
    return true;
}

}
