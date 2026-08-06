#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include "api/multipartparser.h"

#include <QList>
#include <QString>
#include <QStringList>

struct ImageProcessingOptions
{
    int maximumWidth {1920};
    int thumbnailWidth {480};
    bool keepOriginal {false};
};

struct ProcessedImage
{
    QString storagePath;
    QString thumbnailPath;
    QString originalPath;
    qint64 fileSize {0};
};

struct ProcessedFile
{
    QString storagePath;
    qint64 fileSize {0};
};

struct StagedFile
{
    QString originalPath;
    QString stagedPath;
};

struct StagedFileRemoval
{
    QList<StagedFile> files;
};

class ImageProcessor
{
public:
    explicit ImageProcessor(const QString &storageRoot);

    bool process(const MultipartFile &file,
                 const ImageProcessingOptions &options,
                 ProcessedImage *processed,
                 QString *errorMessage,
                 bool *storageError = nullptr) const;
    bool storeFile(const MultipartFile &file,
                   ProcessedFile *processed,
                   QString *errorMessage) const;
    QString absolutePath(const QString &relativePath) const;
    bool remove(const QString &relativePath) const;
    bool stageRemoval(const QStringList &relativePaths,
                      StagedFileRemoval *staged,
                      QString *errorMessage) const;
    void rollbackRemoval(StagedFileRemoval *staged) const;
    void commitRemoval(StagedFileRemoval *staged) const;

private:
    bool writeWebP(const class QImage &image,
                   const QString &relativePath,
                   QString *errorMessage) const;
    bool writeFile(const QByteArray &data,
                   const QString &relativePath,
                   QString *errorMessage) const;

    QString m_storageRoot;
};

#endif // IMAGEPROCESSOR_H
