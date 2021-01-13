#ifndef FILE_H
#define FILE_H

#include <QString>

#include <buffer/filetypehelper.h>

class QImage;

namespace vnotex
{
    class Node;

    class IFileWithImage
    {
    public:
        IFileWithImage() = default;

        virtual ~IFileWithImage() = default;

        virtual QString fetchImageFolderPath() = 0;

        // Insert image from @p_srcImagePath.
        // Return inserted image file path.
        virtual QString insertImage(const QString &p_srcImagePath, const QString &p_imageFileName) = 0;

        virtual QString insertImage(const QImage &p_image, const QString &p_imageFileName) = 0;

        virtual void removeImage(const QString &p_imagePath) = 0;
    };

    // Abstract file interface.
    class File
    {
    public:
        File() = default;

        virtual ~File() = default;

        virtual QString read() const = 0;

        virtual void write(const QString &p_content) = 0;

        virtual QString getName() const = 0;

        virtual QString getFilePath() const = 0;

        // The main content file of File.
        // In bundle case, getFilePath() may point to a folder, getContentPath() points to the real content file.
        virtual QString getContentPath() const = 0;

        // Path to resolve resources.
        virtual QString getResourcePath() const = 0;

        virtual IFileWithImage *getImageInterface() = 0;

        // Get the corresponding node if available.
        virtual Node *getNode() const = 0;

        const FileType &getContentType() const;

    protected:
        void setContentType(int p_type);

    private:
        int m_contentType = FileType::Others;
    };
}

#endif // FILE_H
