#ifndef VIMAGEPREVIEWER_H
#define VIMAGEPREVIEWER_H

#include <QObject>
#include <QString>
#include <QTextBlock>
#include <QHash>
#include "hgmarkdownhighlighter.h"

class QTimer;
class VMdEdit;
class QTextDocument;
class VFile;
class VDownloader;

class VImagePreviewer : public QObject
{
    Q_OBJECT
public:
    explicit VImagePreviewer(VMdEdit *p_edit, const HGMarkdownHighlighter *p_highlighter);

    // Whether @p_block is an image previewed block.
    // The image previewed block is a block containing only the special character
    // and whitespaces.
    bool isImagePreviewBlock(const QTextBlock &p_block);

    QImage fetchCachedImageByID(long long p_id);

    // Update preview image width.
    void updatePreviewImageWidth();

    bool isPreviewing() const;

    bool isEnabled() const;

public slots:
    // Image links have changed.
    void imageLinksChanged(const QVector<VElementRegion> &p_imageRegions);

private slots:
    // Non-local image downloaded for preview.
    void imageDownloaded(const QByteArray &p_data, const QString &p_url);

    // Update preview image width right now.
    void doUpdatePreviewImageWidth();

signals:
    // Request highlighter to update image links.
    void requestUpdateImageLinks();

    // Emit after finishing previewing.
    void previewFinished();

    // Emit after updating preview width.
    void previewWidthUpdated();

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

    struct ImageLinkInfo
    {
        ImageLinkInfo()
            : m_startPos(-1), m_endPos(-1),
              m_isBlock(false), m_previewImageID(-1)
        {
        }

        ImageLinkInfo(int p_startPos, int p_endPos)
            : m_startPos(p_startPos), m_endPos(p_endPos),
              m_isBlock(false), m_previewImageID(-1)
        {
        }

        int m_startPos;
        int m_endPos;
        QString m_linkUrl;

        // Whether it is an image block.
        bool m_isBlock;

        // The previewed image ID if this link has been previewed.
        // -1 if this link has not yet been previewed.
        long long m_previewImageID;
    };

    // Info about a previewed image.
    struct PreviewImageInfo
    {
        PreviewImageInfo() : m_id(-1), m_timeStamp(-1)
        {
        }

        PreviewImageInfo(long long p_id, long long p_timeStamp,
                         const QString p_path, bool p_isBlock)
            : m_id(p_id), m_timeStamp(p_timeStamp),
              m_path(p_path), m_isBlock(p_isBlock)
        {
        }

        QString toString()
        {
            return QString("PreviewImageInfo(ID %0 path %1 stamp %2 isBlock %3")
                          .arg(m_id).arg(m_path).arg(m_timeStamp).arg(m_isBlock);
        }

        long long m_id;
        long long m_timeStamp;
        QString m_path;
        bool m_isBlock;
    };

    // Status about the VMdEdit, used for restore.
    struct EditStatus
    {
        EditStatus()
            : m_modified(false)
        {
        }

        bool m_modified;
    };

    // Kick off new preview of m_imageRegions.
    void kickOffPreview(const QVector<VElementRegion> &p_imageRegions);

    // Preview images according to m_timeStamp and m_imageRegions.
    void previewImages();

    // According to m_imageRegions, fetch the image link Url.
    // Will check if this link has been previewed correctly and mark the previewed
    // image with the newest timestamp.
    // @p_imageLinks should be sorted in descending order of m_startPos.
    void fetchImageLinksFromRegions(QVector<ImageLinkInfo> &p_imageLinks);

    // Preview not previewed image links in @p_imageLinks.
    // Insert the preview block with same indentation with the link block.
    // @p_imageLinks should be sorted in descending order of m_startPos.
    void previewImageLinks(QVector<ImageLinkInfo> &p_imageLinks, QTextCursor &p_cursor);

    // Clear obsolete preview images whose timeStamp does not match current one
    // or does not exist in the cache.
    void clearObsoletePreviewImages(QTextCursor &p_cursor);

    // Clear obsolete preview image in @p_block.
    // A preview image is obsolete if it is not in the cache.
    // If it is a preview block, delete the whole block.
    // @p_block: a block may contain multiple preview images;
    // @p_cursor: cursor used to manipulate the text;
    bool clearObsoletePreviewImagesOfBlock(QTextBlock &p_block, QTextCursor &p_cursor);

    // Update the width of preview image in @p_block.
    bool updatePreviewImageWidthOfBlock(const QTextBlock &p_block, QTextCursor &p_cursor);

    // Check if there is a correct previewed image following the @p_info link.
    // Returns the previewImageID if yes. Otherwise, returns -1.
    long long isImageLinkPreviewed(const ImageLinkInfo &p_info);

    // Fetch the image link's URL if there is only one link.
    QString fetchImageUrlToPreview(const QString &p_text);

    // Fetch teh image's full path if there is only one image link.
    QString fetchImagePathToPreview(const QString &p_text);

    // Clear all the previewed images.
    void clearAllPreviewImages();

    // Fetch the text image format from an image preview block.
    QTextImageFormat fetchFormatFromPreviewBlock(const QTextBlock &p_block) const;

    // Whether the preview image is Image source.
    bool isImageSourcePreviewImage(const QTextImageFormat &p_format) const;

    void initImageFormat(QTextImageFormat &p_imgFormat,
                         const QString &p_imageName,
                         const PreviewImageInfo &p_info) const;

    // Look up m_imageCache to get the resource name in QTextDocument's cache.
    // If there is none, insert it.
    QString imageCacheResourceName(const QString &p_imagePath);

    QString imagePathToCacheResourceName(const QString &p_imagePath);

    // Return true if and only if there is update.
    bool updateImageWidth(QTextImageFormat &p_format);

    // Clean up image cache.
    void shrinkImageCache();

    void saveEditStatus(EditStatus &p_status) const;
    void restoreEditStatus(const EditStatus &p_status);

    VMdEdit *m_edit;
    QTextDocument *m_document;
    VFile *m_file;

    const HGMarkdownHighlighter *m_highlighter;

    // Map from image full path to QUrl identifier in the QTextDocument's cache.
    QHash<QString, ImageInfo> m_imageCache;

    VDownloader *m_downloader;

    // The preview width.
    int m_imageWidth;

    // Used to denote the obsolete previewed images.
    // Increased when a new preview is kicked off.
    long long m_timeStamp;

    // Incremental ID for previewed images.
    long long m_previewIndex;

    // Map from previewImageID to PreviewImageInfo.
    QHash<long long, PreviewImageInfo> m_previewImages;

    // Regions of all the image links.
    QVector<VElementRegion> m_imageRegions;

    // Timer for updatePreviewImageWidth().
    QTimer *m_updateTimer;

    // Whether preview is enabled.
    bool m_previewEnabled;

    // Whether preview is ongoing.
    bool m_isPreviewing;

    static const int c_minImageWidth;
};

inline bool VImagePreviewer::isPreviewing() const
{
    return m_isPreviewing;
}

inline bool VImagePreviewer::isEnabled() const
{
    return m_previewEnabled;
}

#endif // VIMAGEPREVIEWER_H
