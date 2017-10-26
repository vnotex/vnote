#ifndef VPREVIEWMANAGER_H
#define VPREVIEWMANAGER_H

#include <QObject>
#include <QString>
#include <QTextBlock>
#include <QHash>
#include <QVector>
#include "hgmarkdownhighlighter.h"
#include "vmdeditor.h"

class VDownloader;


class VPreviewManager : public QObject
{
    Q_OBJECT
public:
    explicit VPreviewManager(VMdEditor *p_editor);

    void setPreviewEnabled(bool p_enabled);

    // Clear all the preview.
    void clearPreview();

public slots:
    // Image links were updated from the highlighter.
    void imageLinksUpdated(const QVector<VElementRegion> &p_imageRegions);

signals:
    // Request highlighter to update image links.
    void requestUpdateImageLinks();

private slots:
    // Non-local image downloaded for preview.
    void imageDownloaded(const QByteArray &p_data, const QString &p_url);

private:
    // Sources of the preview.
    enum PreviewSource
    {
        ImageLink = 0,
        Invalid
    };

    struct ImageLinkInfo
    {
        ImageLinkInfo()
            : m_startPos(-1),
              m_endPos(-1),
              m_blockNumber(-1),
              m_margin(0),
              m_isBlock(false)
        {
        }

        ImageLinkInfo(int p_startPos,
                      int p_endPos,
                      int p_blockNumber,
                      int p_margin)
            : m_startPos(p_startPos),
              m_endPos(p_endPos),
              m_blockNumber(p_blockNumber),
              m_margin(p_margin),
              m_isBlock(false)
        {
        }

        int m_startPos;

        int m_endPos;

        int m_blockNumber;

        // Left margin of this block in pixels.
        int m_margin;

        // Short URL within the () of ![]().
        // Used as the ID of the image.
        QString m_linkShortUrl;

        // Full URL of the link.
        QString m_linkUrl;

        // Whether it is an image block.
        bool m_isBlock;
    };

    // Start to preview images according to image links.
    void previewImages();

    // According to m_imageRegions, fetch the image link Url.
    // @p_imageRegions: output.
    void fetchImageLinksFromRegions(QVector<ImageLinkInfo> &p_imageLinks);

    // Fetch the image link's URL if there is only one link.
    QString fetchImageUrlToPreview(const QString &p_text);

    // Fetch teh image's full path if there is only one image link.
    // @p_url: contains the short URL in ![]().
    QString fetchImagePathToPreview(const QString &p_text, QString &p_url);

    void updateBlockImageInfo(const QVector<ImageLinkInfo> &p_imageLinks);

    // Get the name of the image in the resource manager.
    // Will add the image to the resource manager if not exists.
    // Returns empty if fail to add the image to the resource manager.
    QString imageResourceName(const ImageLinkInfo &p_link);

    // Ask the editor to preview images.
    void updateEditorBlockImages();

    // Calculate the block margin (prefix spaces) in pixels.
    int calculateBlockMargin(const QTextBlock &p_block);

    VMdEditor *m_editor;

    VDownloader *m_downloader;

    // Whether preview is enabled.
    bool m_previewEnabled;

    // Regions of all the image links.
    QVector<VElementRegion> m_imageRegions;

    // All preview images and information.
    // Each preview source corresponds to one vector.
    QVector<QVector<VBlockImageInfo>> m_blockImageInfo;

    // Map from URL to name in the resource manager.
    // Used for downloading images.
    QHash<QString, QString> m_urlToName;
};

#endif // VPREVIEWMANAGER_H
