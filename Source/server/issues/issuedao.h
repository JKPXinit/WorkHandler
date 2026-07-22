#ifndef ISSUEDAO_H
#define ISSUEDAO_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

class DatabaseManager;
class QSqlQuery;

struct IssueUserReference
{
    qint64 id {0};
    QString username;
    QString displayName;

    QJsonObject toJson() const;
};

struct IssueRecord
{
    qint64 id {0};
    qint64 blockId {0};
    QString title;
    QString description;
    QString status;
    QString priority;
    qint64 reporterId {0};
    std::optional<qint64> assigneeId;
    QString createdAt;
    IssueUserReference reporter;
    std::optional<IssueUserReference> assignee;
    qint64 commentCount {0};
    qint64 attachmentCount {0};

    QJsonObject toJson() const;
};

enum class IssueSort {
    CreatedDescending,
    CreatedAscending,
    PriorityDescending,
    TitleAscending
};

struct IssueFilter
{
    std::optional<qint64> blockId;
    std::optional<QString> status;
    std::optional<QString> priority;
    std::optional<qint64> assigneeId;
    QString search;
    IssueSort sort {IssueSort::CreatedDescending};
};

enum class IssueDaoError {
    None,
    Conflict,
    Database
};

struct IssueDaoResult
{
    IssueDaoError error {IssueDaoError::None};
    QString message;

    bool ok() const;
};

class IssueDao
{
public:
    explicit IssueDao(DatabaseManager &database);

    IssueDaoResult issues(const IssueFilter &filter,
                          QList<IssueRecord> *issues) const;
    IssueDaoResult issueById(qint64 id,
                             std::optional<IssueRecord> *issue) const;
    IssueDaoResult blockExists(qint64 id, bool *exists) const;
    IssueDaoResult userExists(qint64 id, bool *exists) const;
    IssueDaoResult create(const IssueRecord &values,
                          IssueRecord *createdIssue) const;
    IssueDaoResult update(const IssueRecord &values,
                          IssueRecord *updatedIssue) const;
    IssueDaoResult updateStatus(qint64 id,
                                const QString &status,
                                IssueRecord *updatedIssue) const;
    IssueDaoResult remove(qint64 id, bool *removed) const;

private:
    IssueDaoResult exists(const QString &statement,
                          qint64 id,
                          bool *found) const;
    static IssueRecord readIssue(const QSqlQuery &query);

    DatabaseManager &m_database;
};

#endif // ISSUEDAO_H
