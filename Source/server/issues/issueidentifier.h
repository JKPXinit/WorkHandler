#ifndef ISSUEIDENTIFIER_H
#define ISSUEIDENTIFIER_H

#include <QString>

namespace IssueIdentifier {

QString format(qint64 issueId);
bool parse(const QString &identifier, qint64 *issueId);

}

#endif // ISSUEIDENTIFIER_H
