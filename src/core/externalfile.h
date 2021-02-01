#ifndef EXTERNALFILE_H
#define EXTERNALFILE_H

#include "file.h"

namespace vnotex
{
    class ExternalFile : public File, public IFileWithImage
    {
    public:
        explicit ExternalFile(const QString &p_filePath);

        QString read() const Q_DECL_OVERRIDE;

        void write(const QString &p_content) Q_DECL_OVERRIDE;

        QString getName() const Q_DECL_OVERRIDE;

        QString getFilePath() const Q_DECL_OVERRIDE;

        QString getContentPath() const Q_DECL_OVERRIDE;

        // Path to resolve resources.
        QString getResourcePath() const Q_DECL_OVERRIDE;

        IFileWithImage *getImageInterface() Q_DECL_OVERRIDE;

        // Get the corresponding node if available.
        Node *getNode() const Q_DECL_OVERRIDE;

        // IFileWithImage interfaces.
    public:
        QString fetchImageFolderPath() Q_DECL_OVERRIDE;

        // Insert image from @p_srcImagePath.
        // Return inserted image file path.
        QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        QString insertImage(const QImage &p_image, const QString &p_imageFileName) Q_DECL_OVERRIDE;

        void removeImage(const QString &p_imagePath) Q_DECL_OVERRIDE;

    private:
        QString c_filePath;
    };
}

#endif // EXTERNALFILE_H
