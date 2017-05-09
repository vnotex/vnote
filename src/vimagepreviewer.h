#ifndef VIMAGEPREVIEWER_H
#define VIMAGEPREVIEWER_H

#include <QObject>
#include <QString>
#include <QTextBlock>
#include <QHash>

class VMdEdit;
class QTimer;
class QTextDocument;
class VFile;
class VDownloader;

class VImagePreviewer : public QObject
{
    Q_OBJECT
public:
    explicit VImagePreviewer(VMdEdit *p_edit, int p_timeToPreview);

    void disableImagePreview();
    void enableImagePreview();
    bool isPreviewEnabled();

    bool isImagePreviewBlock(const QTextBlock &p_block);

    QImage fetchCachedImageFromPreviewBlock(QTextBlock &p_block);

    // Clear the m_imageCache and all the preview blocks.
    // Then re-preview all the blocks.
    void refresh();

    void update();

private slots:
    void timerTimeout();
    void handleContentChange(int p_position, int p_charsRemoved, int p_charsAdded);
    void imageDownloaded(const QByteArray &p_data, const QString &p_url);

private:
    struct ImageInfo
    {
        ImageInfo(const QString &p_name, int p_width)
            : m_name(p_name), m_width(p_width)
        {
        }

        QString m_name;
        int m_width;
    };

    void previewImages();
    bool isValidImagePreviewBlock(QTextBlock &p_block);

    // Fetch the image link's URL if there is only one link.
    QString fetchImageUrlToPreview(const QString &p_text);

    // Fetch teh image's full path if there is only one image link.
    QString fetchImagePathToPreview(const QString &p_text);

    // Try to preview the image of @p_block.
    // Return the next block to process.
    QTextBlock previewImageOfOneBlock(QTextBlock &p_block);

    // Insert a new block to preview image.
    QTextBlock insertImagePreviewBlock(QTextBlock &p_block, const QString &p_imagePath);

    // @p_block is the image block. Update it to preview @p_imagePath.
    void updateImagePreviewBlock(QTextBlock &p_block, const QString &p_imagePath);

    void removeBlock(QTextBlock &p_block);

    // Corrupted image preview block: ObjectReplacementCharacter mixed with other
    // non-space characters.
    // Remove the ObjectReplacementCharacter chars.
    void clearCorruptedImagePreviewBlock(QTextBlock &p_block);

    void clearAllImagePreviewBlocks();

    QTextImageFormat fetchFormatFromPreviewBlock(QTextBlock &p_block);

    QString fetchImagePathFromPreviewBlock(QTextBlock &p_block);

    void updateFormatInPreviewBlock(QTextBlock &p_block,
                                    const QTextImageFormat &p_format);

    // Look up m_imageCache to get the resource name in QTextDocument's cache.
    // If there is none, insert it.
    QString imageCacheResourceName(const QString &p_imagePath);

    QString imagePathToCacheResourceName(const QString &p_imagePath);

    // Return true if and only if there is update.
    bool updateImageWidth(QTextImageFormat &p_format);

    // Whether it is a normal block or not.
    bool isNormalBlock(const QTextBlock &p_block);

    VMdEdit *m_edit;
    QTextDocument *m_document;
    VFile *m_file;
    QTimer *m_timer;
    bool m_enablePreview;
    bool m_isPreviewing;
    bool m_requestCearBlocks;
    bool m_requestRefreshBlocks;
    bool m_updatePending;

    // Map from image full path to QUrl identifier in the QTextDocument's cache.
    QHash<QString, ImageInfo> m_imageCache;;

    VDownloader *m_downloader;

    // The preview width.
    int m_imageWidth;

    static const int c_minImageWidth;
};

#endif // VIMAGEPREVIEWER_H
