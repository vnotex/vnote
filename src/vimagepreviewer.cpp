#include "vimagepreviewer.h"

#include <QTimer>
#include <QTextDocument>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QVector>
#include "vmdedit.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "utils/vpreviewutils.h"
#include "vfile.h"
#include "vdownloader.h"
#include "hgmarkdownhighlighter.h"
#include "vtextblockdata.h"

extern VConfigManager *g_config;

const int VImagePreviewer::c_minImageWidth = 100;

VImagePreviewer::VImagePreviewer(VMdEdit *p_edit)
    : QObject(p_edit), m_edit(p_edit), m_document(p_edit->document()),
      m_file(p_edit->getFile()), m_imageWidth(c_minImageWidth),
      m_timeStamp(0), m_previewIndex(0),
      m_previewEnabled(g_config->getEnablePreviewImages()), m_isPreviewing(false)
{
    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(400);
    connect(m_updateTimer, &QTimer::timeout,
            this, &VImagePreviewer::doUpdatePreviewImageWidth);

    m_downloader = new VDownloader(this);
    connect(m_downloader, &VDownloader::downloadFinished,
            this, &VImagePreviewer::imageDownloaded);
}

void VImagePreviewer::imageLinksChanged(const QVector<VElementRegion> &p_imageRegions)
{
    kickOffPreview(p_imageRegions);
}

void VImagePreviewer::kickOffPreview(const QVector<VElementRegion> &p_imageRegions)
{
    if (!m_previewEnabled) {
        Q_ASSERT(m_imageRegions.isEmpty());
        Q_ASSERT(m_previewImages.isEmpty());
        Q_ASSERT(m_imageCache.isEmpty());
        return;
    }

    m_isPreviewing = true;

    m_imageRegions = p_imageRegions;
    ++m_timeStamp;

    previewImages();

    shrinkImageCache();
    m_isPreviewing = false;
}

void VImagePreviewer::previewImages()
{
    // Get the width of the m_edit.
    m_imageWidth = qMax(m_edit->size().width() - 50, c_minImageWidth);

    QVector<ImageLinkInfo> imageLinks;
    fetchImageLinksFromRegions(imageLinks);

    QTextCursor cursor(m_document);
    previewImageLinks(imageLinks, cursor);
    clearObsoletePreviewImages(cursor);
}

void VImagePreviewer::initImageFormat(QTextImageFormat &p_imgFormat,
                                      const QString &p_imageName,
                                      const PreviewImageInfo &p_info) const
{
    p_imgFormat.setName(p_imageName);
    p_imgFormat.setProperty((int)ImageProperty::ImageID, p_info.m_id);
    p_imgFormat.setProperty((int)ImageProperty::ImageSource, (int)PreviewImageSource::Image);
    p_imgFormat.setProperty((int)ImageProperty::ImageType,
                            p_info.m_isBlock ? (int)PreviewImageType::Block
                                             : (int)PreviewImageType::Inline);
}

void VImagePreviewer::previewImageLinks(QVector<ImageLinkInfo> &p_imageLinks,
                                        QTextCursor &p_cursor)
{
    bool hasNewPreview = false;
    EditStatus status;
    for (int i = 0; i < p_imageLinks.size(); ++i) {
        ImageLinkInfo &link = p_imageLinks[i];
        if (link.m_previewImageID > -1) {
            continue;
        }

        QString imageName = imageCacheResourceName(link.m_linkUrl);
        if (imageName.isEmpty()) {
            continue;
        }

        PreviewImageInfo info(m_previewIndex++, m_timeStamp,
                              link.m_linkUrl, link.m_isBlock);
        QTextImageFormat imgFormat;
        initImageFormat(imgFormat, imageName, info);

        updateImageWidth(imgFormat);

        saveEditStatus(status);
        p_cursor.joinPreviousEditBlock();
        p_cursor.setPosition(link.m_endPos);
        if (link.m_isBlock) {
            p_cursor.movePosition(QTextCursor::EndOfBlock);
            VEditUtils::insertBlockWithIndent(p_cursor);
        }

        p_cursor.insertImage(imgFormat);
        p_cursor.endEditBlock();

        restoreEditStatus(status);

        Q_ASSERT(!m_previewImages.contains(info.m_id));
        m_previewImages.insert(info.m_id, info);
        link.m_previewImageID = info.m_id;

        hasNewPreview = true;
        qDebug() << "preview new image" << info.toString();
    }

    if (hasNewPreview) {
        emit m_edit->statusChanged();
    }
}

void VImagePreviewer::clearObsoletePreviewImages(QTextCursor &p_cursor)
{
    // Clean up the hash.
    for (auto it = m_previewImages.begin(); it != m_previewImages.end();) {
        PreviewImageInfo &info = it.value();
        if (info.m_timeStamp != m_timeStamp) {
            qDebug() << "obsolete preview image" << info.toString();
            it = m_previewImages.erase(it);
        } else {
            ++it;
        }
    }

    bool hasObsolete = false;
    QTextBlock block = m_document->begin();
    // Clean block data and delete obsolete preview.
    while (block.isValid()) {
        if (!VTextBlockData::containsPreviewImage(block)) {
            block = block.next();
            continue;
        } else {
            QTextBlock nextBlock = block.next();
            // Notice the short circuit.
            hasObsolete = clearObsoletePreviewImagesOfBlock(block, p_cursor) || hasObsolete;
            block = nextBlock;
        }
    }

    if (hasObsolete) {
        emit m_edit->statusChanged();
    }
}

bool VImagePreviewer::isImageSourcePreviewImage(const QTextImageFormat &p_format) const
{
    if (!p_format.isValid()) {
        return false;
    }

    bool ok = true;
    int src = p_format.property((int)ImageProperty::ImageSource).toInt(&ok);
    if (ok) {
        return src == (int)PreviewImageSource::Image;
    } else {
        return false;
    }
}

bool VImagePreviewer::clearObsoletePreviewImagesOfBlock(QTextBlock &p_block,
                                                        QTextCursor &p_cursor)
{
    QString text = p_block.text();
    bool hasObsolete = false;
    bool hasOtherChars = false;
    bool hasValidPreview = false;
    EditStatus status;

    // From back to front.
    for (int i = text.size() - 1; i >= 0; --i) {
        if (text[i].isSpace()) {
            continue;
        }

        if (text[i] == QChar::ObjectReplacementCharacter) {
            int pos = p_block.position() + i;
            Q_ASSERT(m_document->characterAt(pos) == QChar::ObjectReplacementCharacter);

            QTextImageFormat imageFormat = VPreviewUtils::fetchFormatFromPosition(m_document, pos);
            if (!isImageSourcePreviewImage(imageFormat)) {
                hasValidPreview = true;
                continue;
            }

            long long imageID = VPreviewUtils::getPreviewImageID(imageFormat);
            auto it = m_previewImages.find(imageID);
            if (it == m_previewImages.end()) {
                // It is obsolete since we can't find it in the cache.
                qDebug() << "remove obsolete preview image" << imageID;
                saveEditStatus(status);
                p_cursor.joinPreviousEditBlock();
                p_cursor.setPosition(pos);
                p_cursor.deleteChar();
                p_cursor.endEditBlock();
                restoreEditStatus(status);
                hasObsolete = true;
            } else {
                hasValidPreview = true;
            }
        } else {
            hasOtherChars = true;
        }
    }

    if (hasObsolete && !hasOtherChars && !hasValidPreview) {
        // Delete the whole block.
        qDebug() << "delete a preview block" << p_block.blockNumber();
        saveEditStatus(status);
        p_cursor.joinPreviousEditBlock();
        p_cursor.setPosition(p_block.position());
        VEditUtils::removeBlock(p_cursor);
        p_cursor.endEditBlock();
        restoreEditStatus(status);
    }

    return hasObsolete;
}

// Returns true if p_text[p_start, p_end) is all spaces.
static bool isAllSpaces(const QString &p_text, int p_start, int p_end)
{
    for (int i = p_start; i < p_end && i < p_text.size(); ++i) {
        if (!p_text[i].isSpace()) {
            return false;
        }
    }

    return true;
}

void VImagePreviewer::fetchImageLinksFromRegions(QVector<ImageLinkInfo> &p_imageLinks)
{
    p_imageLinks.clear();

    if (m_imageRegions.isEmpty()) {
        return;
    }

    p_imageLinks.reserve(m_imageRegions.size());

    for (int i = 0; i < m_imageRegions.size(); ++i) {
        VElementRegion &reg = m_imageRegions[i];
        QTextBlock block = m_document->findBlock(reg.m_startPos);
        if (!block.isValid()) {
            continue;
        }

        int blockStart = block.position();
        int blockEnd = blockStart + block.length() - 1;
        QString text = block.text();
        Q_ASSERT(reg.m_endPos <= blockEnd);
        ImageLinkInfo info(reg.m_startPos, reg.m_endPos);
        if ((reg.m_startPos == blockStart
             || isAllSpaces(text, 0, reg.m_startPos - blockStart))
            && (reg.m_endPos == blockEnd
                || isAllSpaces(text, reg.m_endPos - blockStart, blockEnd - blockStart))) {
            // Image block.
            info.m_isBlock = true;
            info.m_linkUrl = fetchImagePathToPreview(text);
        } else {
            // Inline image.
            info.m_isBlock = false;
            info.m_linkUrl = fetchImagePathToPreview(text.mid(reg.m_startPos - blockStart,
                                                              reg.m_endPos - reg.m_startPos));
        }

        // Check if this image link has been previewed previously.
        info.m_previewImageID = isImageLinkPreviewed(info);

        // Sorted in descending order of m_startPos.
        p_imageLinks.append(info);

        qDebug() << "image region" << i << info.m_startPos << info.m_endPos
                 << info.m_linkUrl << info.m_isBlock << info.m_previewImageID;
    }
}

long long VImagePreviewer::isImageLinkPreviewed(const ImageLinkInfo &p_info)
{
    long long imageID = -1;
    if (p_info.m_isBlock) {
        QTextBlock block = m_document->findBlock(p_info.m_startPos);
        QTextBlock nextBlock = block.next();
        if (!nextBlock.isValid()) {
            return imageID;
        }

        if (!isImagePreviewBlock(nextBlock)) {
            return imageID;
        }

        // Make sure the indentation is the same as @block.
        if (VEditUtils::hasSameIndent(block, nextBlock)) {
            QTextImageFormat format = fetchFormatFromPreviewBlock(nextBlock);
            if (isImageSourcePreviewImage(format)) {
                imageID = VPreviewUtils::getPreviewImageID(format);
            }
        }
    } else {
        QTextImageFormat format = VPreviewUtils::fetchFormatFromPosition(m_document, p_info.m_endPos);
        if (isImageSourcePreviewImage(format)) {
            imageID = VPreviewUtils::getPreviewImageID(format);
        }
    }

    if (imageID != -1) {
        auto it = m_previewImages.find(imageID);
        if (it != m_previewImages.end()) {
            PreviewImageInfo &img = it.value();
            if (img.m_path == p_info.m_linkUrl
                && img.m_isBlock == p_info.m_isBlock) {
                img.m_timeStamp = m_timeStamp;
            } else {
                imageID = -1;
            }
        } else {
            // This preview image does not exist in the cache, which means it may
            // be deleted before but added back by user's undo action.
            // We treat it an obsolete preview image.
            imageID = -1;
        }
    }

    return imageID;
}

bool VImagePreviewer::isImagePreviewBlock(const QTextBlock &p_block)
{
    if (!p_block.isValid()) {
        return false;
    }

    QString text = p_block.text().trimmed();
    return text == QString(QChar::ObjectReplacementCharacter);
}

QString VImagePreviewer::fetchImageUrlToPreview(const QString &p_text)
{
    QRegExp regExp(VUtils::c_imageLinkRegExp);

    int index = regExp.indexIn(p_text);
    if (index == -1) {
        return QString();
    }

    int lastIndex = regExp.lastIndexIn(p_text);
    if (lastIndex != index) {
        return QString();
    }

    return regExp.capturedTexts()[2].trimmed();
}

QString VImagePreviewer::fetchImagePathToPreview(const QString &p_text)
{
    QString imageUrl = fetchImageUrlToPreview(p_text);
    if (imageUrl.isEmpty()) {
        return imageUrl;
    }

    QString imagePath;
    QFileInfo info(m_file->retriveBasePath(), imageUrl);

    if (info.exists()) {
        if (info.isNativePath()) {
            // Local file.
            imagePath = QDir::cleanPath(info.absoluteFilePath());
        } else {
            imagePath = imageUrl;
        }
    } else {
        QString decodedUrl(imageUrl);
        VUtils::decodeUrl(decodedUrl);
        QFileInfo dinfo(m_file->retriveBasePath(), decodedUrl);
        if (dinfo.exists()) {
            if (dinfo.isNativePath()) {
                // Local file.
                imagePath = QDir::cleanPath(dinfo.absoluteFilePath());
            } else {
                imagePath = imageUrl;
            }
        } else {
            QUrl url(imageUrl);
            imagePath = url.toString();
        }
    }

    return imagePath;
}

void VImagePreviewer::clearAllPreviewImages()
{
    m_imageRegions.clear();
    ++m_timeStamp;

    QTextCursor cursor(m_document);
    clearObsoletePreviewImages(cursor);

    m_imageCache.clear();
}

QTextImageFormat VImagePreviewer::fetchFormatFromPreviewBlock(const QTextBlock &p_block) const
{
    QTextCursor cursor(p_block);
    int shift = p_block.text().indexOf(QChar::ObjectReplacementCharacter);
    if (shift >= 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, shift + 1);
    } else {
        return QTextImageFormat();
    }

    return cursor.charFormat().toImageFormat();
}

QString VImagePreviewer::imageCacheResourceName(const QString &p_imagePath)
{
    V_ASSERT(!p_imagePath.isEmpty());

    auto it = m_imageCache.find(p_imagePath);
    if (it != m_imageCache.end()) {
        return it.value().m_name;
    }

    // Add it to the resource cache even if it may exist there.
    QFileInfo info(p_imagePath);
    QImage image;
    if (info.exists()) {
        // Local file.
        image = QImage(p_imagePath);
    } else {
        // URL. Try to download it.
        m_downloader->download(p_imagePath);
    }

    if (image.isNull()) {
        return QString();
    }

    QString name(imagePathToCacheResourceName(p_imagePath));
    m_document->addResource(QTextDocument::ImageResource, name, image);
    m_imageCache.insert(p_imagePath, ImageInfo(name, image.width()));

    return name;
}

QString VImagePreviewer::imagePathToCacheResourceName(const QString &p_imagePath)
{
    return p_imagePath;
}

void VImagePreviewer::imageDownloaded(const QByteArray &p_data, const QString &p_url)
{
    QImage image(QImage::fromData(p_data));

    if (!image.isNull()) {
        auto it = m_imageCache.find(p_url);
        if (it != m_imageCache.end()) {
            return;
        }

        QString name(imagePathToCacheResourceName(p_url));
        m_document->addResource(QTextDocument::ImageResource, name, image);
        m_imageCache.insert(p_url, ImageInfo(name, image.width()));

        qDebug() << "downloaded image cache insert" << p_url << name;
        emit requestUpdateImageLinks();
    }
}

QImage VImagePreviewer::fetchCachedImageByID(long long p_id)
{
    auto imgIt = m_previewImages.find(p_id);
    if (imgIt == m_previewImages.end()) {
        return QImage();
    }

    QString path = imgIt->m_path;
    if (path.isEmpty()) {
        return QImage();
    }

    auto it = m_imageCache.find(path);
    if (it == m_imageCache.end()) {
        return QImage();
    }

    return m_document->resource(QTextDocument::ImageResource, it.value().m_name).value<QImage>();
}

bool VImagePreviewer::updateImageWidth(QTextImageFormat &p_format)
{
    long long imageID = VPreviewUtils::getPreviewImageID(p_format);
    auto imgIt = m_previewImages.find(imageID);
    if (imgIt == m_previewImages.end()) {
        return false;
    }

    auto it = m_imageCache.find(imgIt->m_path);
    if (it != m_imageCache.end()) {
        int newWidth = it.value().m_width;
        if (g_config->getEnablePreviewImageConstraint()) {
            newWidth = qMin(m_imageWidth, newWidth);
        }

        if (newWidth != p_format.width()) {
            p_format.setWidth(newWidth);
            return true;
        }
    }

    return false;
}

void VImagePreviewer::updatePreviewImageWidth()
{
    if (!m_previewEnabled) {
        return;
    }

    m_updateTimer->stop();
    m_updateTimer->start();
}

void VImagePreviewer::doUpdatePreviewImageWidth()
{
    // Get the width of the m_edit.
    m_imageWidth = qMax(m_edit->size().width() - 50, c_minImageWidth);

    bool updated = false;
    QTextBlock block = m_document->begin();
    QTextCursor cursor(block);
    while (block.isValid()) {
        if (VTextBlockData::containsPreviewImage(block)) {
            // Notice the short circuit.
            updated = updatePreviewImageWidthOfBlock(block, cursor) || updated;
        }

        block = block.next();
    }

    if (updated) {
        emit m_edit->statusChanged();
    }
}

bool VImagePreviewer::updatePreviewImageWidthOfBlock(const QTextBlock &p_block,
                                                     QTextCursor &p_cursor)
{
    QString text = p_block.text();
    bool updated = false;
    EditStatus status;

    // From back to front.
    for (int i = text.size() - 1; i >= 0; --i) {
        if (text[i].isSpace()) {
            continue;
        }

        if (text[i] == QChar::ObjectReplacementCharacter) {
            int pos = p_block.position() + i;
            Q_ASSERT(m_document->characterAt(pos) == QChar::ObjectReplacementCharacter);

            QTextImageFormat imageFormat = VPreviewUtils::fetchFormatFromPosition(m_document, pos);
            if (imageFormat.isValid()
                && isImageSourcePreviewImage(imageFormat)
                && updateImageWidth(imageFormat)) {
                saveEditStatus(status);
                p_cursor.joinPreviousEditBlock();
                p_cursor.setPosition(pos);
                p_cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                Q_ASSERT(p_cursor.charFormat().toImageFormat().isValid());
                p_cursor.setCharFormat(imageFormat);
                p_cursor.endEditBlock();
                restoreEditStatus(status);
                updated = true;
            }
        }
    }

    return updated;
}

void VImagePreviewer::shrinkImageCache()
{
    const int MaxSize = 20;
    if (m_imageCache.size() > m_previewImages.size()
        && m_imageCache.size() > MaxSize) {
        QHash<QString, bool> usedImagePath;
        for (auto it = m_previewImages.begin(); it != m_previewImages.end(); ++it) {
            usedImagePath.insert(it->m_path, true);
        }

        for (auto it = m_imageCache.begin(); it != m_imageCache.end();) {
            if (!usedImagePath.contains(it.key())) {
                qDebug() << "shrink one image" << it.key();
                it = m_imageCache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void VImagePreviewer::saveEditStatus(EditStatus &p_status) const
{
    p_status.m_modified = m_edit->isModified();
    p_status.m_undoAvailable = m_document->isUndoAvailable();
    p_status.m_redoAvailable = m_document->isRedoAvailable();
}

void VImagePreviewer::restoreEditStatus(const EditStatus &p_status)
{
    int st = 0;
    if (!p_status.m_undoAvailable) {
        st |= 1;
    }

    if (!p_status.m_redoAvailable) {
        st |= 2;
    }

    if (st > 0) {
        QTextDocument::Stacks stack = QTextDocument::UndoStack;
        if (st == 2) {
            stack = QTextDocument::RedoStack;
        } else if (st == 3) {
            stack = QTextDocument::UndoAndRedoStacks;
        }

        m_document->clearUndoRedoStacks(stack);
    }

    // Clear undo and redo stacks will change the state to modified.
    m_edit->setModified(p_status.m_modified);
}
