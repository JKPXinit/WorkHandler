#ifndef COMMENTDAO_H
#define COMMENTDAO_H

#include <QJsonObject>
#include <QList>
#include <QString>

class DatabaseManager;
class QSqlQuery;

struct CommentUserReference
{
    qint64 id {0};
    QString username;
    QString displayName;

    QJsonObject toJson() const;
};

struct CommentRecord
{
    qint64 id {0};
    qint64 issueId {0};
    qint64 userId {0};
    QString content;
    QString createdAt;
    CommentUserReference user;

    QJsonObject toJson() const;
};

enum class CommentDaoError {
    None,
    Conflict,
    Database
};

struct CommentDaoResult
{
    CommentDaoError error {CommentDaoError::None};
    QString message;

    bool ok() const;
};

class CommentDao
{
public:
    explicit CommentDao(DatabaseManager &database);

    CommentDaoResult comments(qint64 issueId,
                              QList<CommentRecord> *comments) const;
    CommentDaoResult issueExists(qint64 issueId, bool *exists) const;
    CommentDaoResult create(qint64 issueId,
                            qint64 userId,
                            const QString &content,
                            CommentRecord *createdComment) const;

private:
    CommentDaoResult commentById(qint64 id,
                                 CommentRecord *comment,
                                 bool *found) const;
    static CommentRecord readComment(const QSqlQuery &query);

    DatabaseManager &m_database;
};

#endif // COMMENTDAO_H
