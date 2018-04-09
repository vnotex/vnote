#include "vpreviewmanager.h"

#include <QTextDocument>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QVector>
#include <QTextLayout>

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vdownloader.h"
#include "hgmarkdownhighlighter.h"

extern VConfigManager *g_config;

VPreviewManager::VPreviewManager(VMdEditor *p_editor, HGMarkdownHighlighter *p_highlighter)
    : QObject(p_editor),
      m_editor(p_editor),
      m_document(p_editor->document()),
      m_highlighter(p_highlighter),
      m_previewEnabled(false)
{
    for (int i = 0; i < (int)PreviewSource::MaxNumberOfSources; ++i) {
        m_timeStamps[i] = 0;
    }

    m_downloader = new VDownloader(this);
    connect(m_downloader, &VDownloader::downloadFinished,
            this, &VPreviewManager::imageDownloaded);
}

void VPreviewManager::updateImageLinks(const QVector<VElementRegion> &p_imageRegions)
{
    if (!m_previewEnabled) {
        return;
    }

    TS ts = ++timeStamp(PreviewSource::ImageLink);
    previewImages(ts, p_imageRegions);
}

void VPreviewManager::imageDownloaded(const QByteArray &p_data, const QString &p_url)
{
    if (!m_previewEnabled) {
        return;
    }

    auto it = m_urlToName.find(p_url);
    if (it == m_urlToName.end()) {
        return;
    }

    QString name = it.value();
    m_urlToName.erase(it);

    if (m_editor->containsImage(name) || name.isEmpty()) {
        return;
    }

    QPixmap image;
    image.loadFromData(p_data);

    if (!image.isNull()) {
        m_editor->addImage(name, image);
        qDebug() << "downloaded image inserted in resource manager" << p_url << name;
        emit requestUpdateImageLinks();
    }
}

void VPreviewManager::setPreviewEnabled(bool p_enabled)
{
    if (m_previewEnabled != p_enabled) {
        m_previewEnabled = p_enabled;

        emit previewEnabledChanged(p_enabled);

        if (!m_previewEnabled) {
            clearPreview();
        } else {
            requestUpdateImageLinks();
        }
    }
}

void VPreviewManager::clearPreview()
{
    for (int i = 0; i < (int)PreviewSource::MaxNumberOfSources; ++i) {
        TS ts = ++timeStamp(static_cast<PreviewSource>(i));
        clearBlockObsoletePreviewInfo(ts, static_cast<PreviewSource>(i));
        clearObsoleteImages(ts, static_cast<PreviewSource>(i));
    }
}

void VPreviewManager::previewImages(TS p_timeStamp, const QVector<VElementRegion> &p_imageRegions)
{
    QVector<ImageLinkInfo> imageLinks;
    fetchImageLinksFromRegions(p_imageRegions, imageLinks);

    updateBlockPreviewInfo(p_timeStamp, imageLinks);

    clearBlockObsoletePreviewInfo(p_timeStamp, PreviewSource::ImageLink);

    clearObsoleteImages(p_timeStamp, PreviewSource::ImageLink);
}

// Returns true if p_text[p_start, p_end) is all spaces.
static bool isAllSpaces(const QString &p_text, int p_start, int p_end)
{
    int len = qMin(p_text.size(), p_end);
    for (int i = p_start; i < len; ++i) {
        if (!p_text[i].isSpace()) {
            return false;
        }
    }

    return true;
}

void VPreviewManager::fetchImageLinksFromRegions(QVector<VElementRegion> p_imageRegions,
                                                 QVector<ImageLinkInfo> &p_imageLinks)
{
    p_imageLinks.clear();

    if (p_imageRegions.isEmpty()) {
        return;
    }

    p_imageLinks.reserve(p_imageRegions.size());

    QTextDocument *doc = m_editor->document();

    for (int i = 0; i < p_imageRegions.size(); ++i) {
        VElementRegion &reg = p_imageRegions[i];
        QTextBlock block = doc->findBlock(reg.m_startPos);
        if (!block.isValid()) {
            continue;
        }

        int blockStart = block.position();
        int blockEnd = blockStart + block.length() - 1;
        QString text = block.text();
        Q_ASSERT(reg.m_endPos <= blockEnd);
        ImageLinkInfo info(reg.m_startPos,
                           reg.m_endPos,
                           blockStart,
                           block.blockNumber(),
                           calculateBlockMargin(block, m_editor->tabStopWidthW()));
        if ((reg.m_startPos == blockStart
             || isAllSpaces(text, 0, reg.m_startPos - blockStart))
            && (reg.m_endPos == blockEnd
                || isAllSpaces(text, reg.m_endPos - blockStart, blockEnd - blockStart))) {
            // Image block.
            info.m_isBlock = true;
            info.m_linkUrl = fetchImagePathToPreview(text, info.m_linkShortUrl);
        } else {
            // Inline image.
            info.m_isBlock = false;
            info.m_linkUrl = fetchImagePathToPreview(text.mid(reg.m_startPos - blockStart,
                                                              reg.m_endPos - reg.m_startPos),
                                                     info.m_linkShortUrl);
        }

        if (info.m_linkUrl.isEmpty()) {
            continue;
        }

        p_imageLinks.append(info);

        qDebug() << "image region" << i
                 << info.m_startPos << info.m_endPos << info.m_blockNumber
                 << info.m_linkShortUrl << info.m_linkUrl << info.m_isBlock;
    }
}

QString VPreviewManager::fetchImageUrlToPreview(const QString &p_text)
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

QString VPreviewManager::fetchImagePathToPreview(const QString &p_text, QString &p_url)
{
    p_url = fetchImageUrlToPreview(p_text);
    if (p_url.isEmpty()) {
        return p_url;
    }

    const VFile *file = m_editor->getFile();

    QString imagePath;
    QFileInfo info(file->fetchBasePath(), p_url);

    if (info.exists()) {
        if (info.isNativePath()) {
            // Local file.
            imagePath = QDir::cleanPath(info.absoluteFilePath());
        } else {
            imagePath = p_url;
        }
    } else {
        QString decodedUrl(p_url);
        VUtils::decodeUrl(decodedUrl);
        QFileInfo dinfo(file->fetchBasePath(), decodedUrl);
        if (dinfo.exists()) {
            if (dinfo.isNativePath()) {
                // Local file.
                imagePath = QDir::cleanPath(dinfo.absoluteFilePath());
            } else {
                imagePath = p_url;
            }
        } else {
            QUrl url(p_url);
            imagePath = url.toString();
        }
    }

    return imagePath;
}

QString VPreviewManager::imageResourceName(const ImageLinkInfo &p_link)
{
    QString name = p_link.m_linkShortUrl;
    if (m_editor->containsImage(name)
        || name.isEmpty()) {
        return name;
    }

    // Add it to the resource.
    QString imgPath = p_link.m_linkUrl;
    QFileInfo info(imgPath);
    QPixmap image;
    if (info.exists()) {
        // Local file.
        image = QPixmap(imgPath);
    } else {
        // URL. Try to download it.
        m_downloader->download(imgPath);
        m_urlToName.insert(imgPath, name);
    }

    if (image.isNull()) {
        return QString();
    }

    m_editor->addImage(name, image);
    return name;
}

QString VPreviewManager::imageResourceNameFromCodeBlock(const QSharedPointer<VImageToPreview> &p_image)
{
    QString name = "CODE_BLOCK_" + p_image->m_name;
    if (m_editor->containsImage(name)) {
        return name;
    }

    // Add it to the resource.
    if (p_image->m_image.isNull()) {
        return QString();
    }

    m_editor->addImage(name, p_image->m_image);
    return name;
}

int VPreviewManager::calculateBlockMargin(const QTextBlock &p_block, int p_tabStopWidth)
{
    static QHash<QString, int> spaceWidthOfFonts;

    if (!p_block.isValid()) {
        return 0;
    }

    QString text = p_block.text();
    int nrSpaces = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (!text[i].isSpace()) {
            break;
        } else if (text[i] == ' ') {
            ++nrSpaces;
        } else if (text[i] == '\t') {
            nrSpaces += p_tabStopWidth;
        }
    }

    if (nrSpaces == 0) {
        return 0;
    }

    int spaceWidth = 0;
    QFont font;
    QVector<QTextLayout::FormatRange> fmts = p_block.layout()->formats();
    if (fmts.isEmpty()) {
        font = p_block.charFormat().font();
    } else {
        font = fmts.first().format.font();
    }

    QString fontName = font.toString();
    auto it = spaceWidthOfFonts.find(fontName);
    if (it != spaceWidthOfFonts.end()) {
        spaceWidth = it.value();
    } else {
        spaceWidth = QFontMetrics(font).width(' ');
        spaceWidthOfFonts.insert(fontName, spaceWidth);
    }

    return spaceWidth * nrSpaces;
}

void VPreviewManager::updateBlockPreviewInfo(TS p_timeStamp,
                                             const QVector<ImageLinkInfo> &p_imageLinks)
{
    for (auto const & link : p_imageLinks) {
        QTextBlock block = m_document->findBlockByNumber(link.m_blockNumber);
        if (!block.isValid()) {
            continue;
        }

        QString name = imageResourceName(link);
        if (name.isEmpty()) {
            continue;
        }

        VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(block.userData());
        Q_ASSERT(blockData);

        VPreviewInfo *info = new VPreviewInfo(PreviewSource::ImageLink,
                                              p_timeStamp,
                                              link.m_startPos - link.m_blockPos,
                                              link.m_endPos - link.m_blockPos,
                                              link.m_padding,
                                              !link.m_isBlock,
                                              name,
                                              m_editor->imageSize(name));
        blockData->insertPreviewInfo(info);

        imageCache(PreviewSource::ImageLink).insert(name, p_timeStamp);

        qDebug() << "block" << link.m_blockNumber
                 << imageCache(PreviewSource::ImageLink).size()
                 << blockData->toString();
    }

    // TODO: may need to call m_editor->update()?
}

void VPreviewManager::updateBlockPreviewInfo(TS p_timeStamp,
                                             PreviewSource p_source,
                                             const QVector<QSharedPointer<VImageToPreview> > &p_images)
{
    QSet<int> affectedBlocks;
    for (auto const & img : p_images) {
        if (img.isNull()) {
            continue;
        }

        QTextBlock block = m_document->findBlockByNumber(img->m_blockNumber);
        if (!block.isValid()) {
            continue;
        }

        QString name = imageResourceNameFromCodeBlock(img);
        if (name.isEmpty()) {
            continue;
        }

        VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(block.userData());
        Q_ASSERT(blockData);
        VPreviewInfo *info = new VPreviewInfo(p_source,
                                              p_timeStamp,
                                              img->m_startPos - img->m_blockPos,
                                              img->m_endPos - img->m_blockPos,
                                              img->m_padding,
                                              !img->m_isBlock,
                                              name,
                                              m_editor->imageSize(name));
        bool tsUpdated = blockData->insertPreviewInfo(info);
        imageCache(p_source).insert(name, p_timeStamp);
        if (!tsUpdated) {
            // No need to relayout the block if only timestamp is updated.
            affectedBlocks.insert(img->m_blockNumber);
            m_highlighter->addPossiblePreviewBlock(img->m_blockNumber);
        }
    }

    // Relayout these blocks since they may not have been changed.
    m_editor->relayout(affectedBlocks);
    m_editor->update();
}

void VPreviewManager::clearObsoleteImages(long long p_timeStamp, PreviewSource p_source)
{
    QHash<QString, long long> &cache = imageCache(p_source);

    for (auto it = cache.begin(); it != cache.end();) {
        if (it.value() < p_timeStamp) {
            m_editor->removeImage(it.key());
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

void VPreviewManager::clearBlockObsoletePreviewInfo(long long p_timeStamp,
                                                    PreviewSource p_source)
{
    QSet<int> affectedBlocks;
    QVector<int> obsoleteBlocks;
    const QSet<int> &blocks = m_highlighter->getPossiblePreviewBlocks();
    qDebug() << "possible preview blocks" << blocks;
    for (auto i : blocks) {
        QTextBlock block = m_document->findBlockByNumber(i);
        if (!block.isValid()) {
            obsoleteBlocks.append(i);
            continue;
        }

        VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(block.userData());
        if (!blockData) {
            continue;
        }

        if (blockData->clearObsoletePreview(p_timeStamp, p_source)) {
            affectedBlocks.insert(i);
        }

        if (blockData->getPreviews().isEmpty()) {
            obsoleteBlocks.append(i);
        }
    }

    m_highlighter->clearPossiblePreviewBlocks(obsoleteBlocks);

    m_editor->relayout(affectedBlocks);
}

void VPreviewManager::refreshPreview()
{
    if (!m_previewEnabled) {
        return;
    }

    clearPreview();

    // No need to request updating code blocks since this will also update them.
    requestUpdateImageLinks();
}

void VPreviewManager::updateCodeBlocks(const QVector<QSharedPointer<VImageToPreview> > &p_images)
{
    if (!m_previewEnabled) {
        return;
    }

    TS ts = ++timeStamp(PreviewSource::CodeBlock);

    updateBlockPreviewInfo(ts, PreviewSource::CodeBlock, p_images);

    clearBlockObsoletePreviewInfo(ts, PreviewSource::CodeBlock);

    clearObsoleteImages(ts, PreviewSource::CodeBlock);
}
