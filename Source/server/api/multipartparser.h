#ifndef MULTIPARTPARSER_H
#define MULTIPARTPARSER_H

#include <QByteArray>
#include <QList>
#include <QString>

class QHttpServerRequest;

struct MultipartFile
{
    QString filename;
    QByteArray contentType;
    QByteArray data;
};

class MultipartParser
{
public:
    static bool parseSingleFile(const QHttpServerRequest &request,
                                const QByteArray &fieldName,
                                qsizetype maximumBodySize,
                                MultipartFile *file,
                                QString *errorMessage);
    static bool parseComment(const QHttpServerRequest &request,
                             qsizetype maximumBodySize,
                             int maximumFiles,
                             QString *content,
                             QList<MultipartFile> *files,
                             QString *errorMessage);
};

#endif // MULTIPARTPARSER_H
