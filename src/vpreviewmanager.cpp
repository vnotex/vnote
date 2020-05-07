#include "vpreviewmanager.h"

#include <QTextDocument>
#include <QDir>
#include <QUrl>
#include <QVector>
#include <QTextLayout>

#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vdownloader.h"
#include "pegmarkdownhighlighter.h"

extern VConfigManager *g_config;

VPreviewManager::VPreviewManager(VMdEditor *p_editor, PegMarkdownHighlighter *p_highlighter)
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

static QPixmap scalePreviewImage(const QPixmap &p_img, int p_width, int p_height)
{
    const Qt::TransformationMode tMode = Qt::SmoothTransformation;
    qreal sf = VUtils::calculateScaleFactor();
    if (p_width > 0) {
        if (p_height > 0) {
            return p_img.scaled(p_width * sf,
                                p_height * sf,
                                Qt::IgnoreAspectRatio,
                                tMode);
        } else {
            return p_img.scaledToWidth(p_width * sf, tMode);
        }
    } else if (p_height > 0) {
        return p_img.scaledToHeight(p_height * sf, tMode);
    } else {
        if (sf < 1.1) {
            return p_img;
        } else {
            return p_img.scaledToWidth(p_img.width() * sf, tMode);
        }
    }
}

void VPreviewManager::imageDownloaded(const QByteArray &p_data, const QString &p_url)
{
    if (!m_previewEnabled) {
        return;
    }

    auto it = m_urlMap.find(p_url);
    if (it == m_urlMap.end()) {
        return;
    }

    QSharedPointer<UrlImageInfo> info = it.value();
    m_urlMap.erase(it);

    if (m_editor->containsImage(info->m_name) || info->m_name.isEmpty()) {
        return;
    }

    QPixmap image;
    image.loadFromData(p_data);
    if (!image.isNull()) {
        m_editor->addImage(info->m_name,
                           scalePreviewImage(image, info->m_width, info->m_height));
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
    OrderedIntSet affectedBlocks;
    for (int i = 0; i < (int)PreviewSource::MaxNumberOfSources; ++i) {
        TS ts = ++timeStamp(static_cast<PreviewSource>(i));
        clearBlockObsoletePreviewInfo(ts, static_cast<PreviewSource>(i), affectedBlocks);
        clearObsoleteImages(ts, static_cast<PreviewSource>(i));
    }

    relayout(affectedBlocks);
}

void VPreviewManager::previewImages(TS p_timeStamp, const QVector<VElementRegion> &p_imageRegions)
{
    QVector<ImageLinkInfo> imageLinks;
    fetchImageLinksFromRegions(p_imageRegions, imageLinks);

    OrderedIntSet affectedBlocks;

    updateBlockPreviewInfo(p_timeStamp, imageLinks, affectedBlocks);

    clearBlockObsoletePreviewInfo(p_timeStamp, PreviewSource::ImageLink, affectedBlocks);

    clearObsoleteImages(p_timeStamp, PreviewSource::ImageLink);

    relayout(affectedBlocks);
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
        const VElementRegion &reg = p_imageRegions[i];
        QTextBlock firstBlock = doc->findBlock(reg.m_startPos);
        if (!firstBlock.isValid()) {
            continue;
        }

        // Image link may cross multiple regions.
        QTextBlock lastBlock = doc->findBlock(reg.m_endPos - 1);
        if (!lastBlock.isValid()) {
            continue;
        }

        int firstBlockStart = firstBlock.position();
        int lastBlockStart = lastBlock.position();
        int lastBlockEnd = lastBlockStart + lastBlock.length() - 1;

        QString text;
        if (firstBlock.blockNumber() == lastBlock.blockNumber()) {
            text = firstBlock.text().mid(reg.m_startPos - firstBlockStart,
                                         reg.m_endPos - reg.m_startPos);
        } else {
            text = firstBlock.text().mid(reg.m_startPos - firstBlockStart);

            QTextBlock block = firstBlock.next();
            while (block.isValid() && block.blockNumber() < lastBlock.blockNumber()) {
                text += "\n" + block.text();
                block = block.next();
            }

            text += "\n" + lastBlock.text().left(reg.m_endPos - lastBlockStart);
        }

        // Preview the image at the last block.
        ImageLinkInfo info(qMax(reg.m_startPos, lastBlockStart),
                           reg.m_endPos,
                           lastBlockStart,
                           lastBlock.blockNumber(),
                           calculateBlockMargin(firstBlock, m_editor->tabStopWidthW()));
        if ((reg.m_startPos == firstBlockStart
             || isAllSpaces(firstBlock.text(), 0, reg.m_startPos - firstBlockStart))
            && (reg.m_endPos == lastBlockEnd
                || isAllSpaces(lastBlock.text(),
                               reg.m_endPos - lastBlockStart,
                               lastBlockEnd - lastBlockStart))) {
            // Image block.
            info.m_isBlock = true;
        } else {
            // Inline image.
            info.m_isBlock = false;
        }

        fetchImageInfoToPreview(text, info);

        if (info.m_linkUrl.isEmpty() || info.m_linkShortUrl.isEmpty()) {
            continue;
        }

        p_imageLinks.append(info);
    }
}

void VPreviewManager::fetchImageInfoToPreview(const QString &p_text, ImageLinkInfo &p_info)
{
    QString surl = VUtils::fetchImageLinkUrlToPreview(p_text, p_info.m_width, p_info.m_height);
    p_info.m_linkShortUrl = surl;
    if (surl.isEmpty()) {
        p_info.m_linkUrl = surl;
        return;
    }

    const VFile *file = m_editor->getFile();
    p_info.m_linkUrl = VUtils::linkUrlToPath(file->fetchBasePath(), surl);
}

QString VPreviewManager::imageResourceName(const ImageLinkInfo &p_link)
{
    // Add size info to the name.
    QString name = QString("%1_%2_%3").arg(p_link.m_linkShortUrl)
                                      .arg(p_link.m_width)
                                      .arg(p_link.m_height);
    if (m_editor->containsImage(name)) {
        return name;
    }

    // Add it to the resource.
    QPixmap image;
    QString imgPath = p_link.m_linkUrl;
    if (QFileInfo::exists(imgPath)) {
        // Local file.
        image = VUtils::pixmapFromFile(imgPath);
        if (image.isNull()) {
            return QString();
        }
    } else {
        // URL. Try to download it.
        // qrc:// files will touch this path.
        m_downloader->download(imgPath);

        QSharedPointer<UrlImageInfo> info(new UrlImageInfo(name,
                                                           p_link.m_width,
                                                           p_link.m_height));
        m_urlMap.insert(imgPath, info);

        return QString();
    }

    m_editor->addImage(name,
                       scalePreviewImage(image, p_link.m_width, p_link.m_height));
    return name;
}

QString VPreviewManager::imageResourceNameForSource(PreviewSource p_source,
                                                    const QSharedPointer<VImageToPreview> &p_image)
{
    QString name = QString::number((int)p_source) + "_" + p_image->m_name;
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
                                             const QVector<ImageLinkInfo> &p_imageLinks,
                                             OrderedIntSet &p_affectedBlocks)
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

        VTextBlockData *blockData = static_cast<VTextBlockData *>(block.userData());
        if (!blockData) {
            continue;
        }

        VPreviewInfo *info = new VPreviewInfo(PreviewSource::ImageLink,
                                              p_timeStamp,
                                              link.m_startPos - link.m_blockPos,
                                              link.m_endPos - link.m_blockPos,
                                              link.m_padding,
                                              !link.m_isBlock,
                                              name,
                                              m_editor->imageSize(name),
                                              QString());
        bool tsUpdated = blockData->insertPreviewInfo(info);
        imageCache(PreviewSource::ImageLink).insert(name, p_timeStamp);
        if (!tsUpdated) {
            // No need to relayout the block if only timestamp is updated.
            p_affectedBlocks.insert(link.m_blockNumber, QMapDummyValue());
            m_highlighter->addPossiblePreviewBlock(link.m_blockNumber);
        }
    }
}

void VPreviewManager::updateBlockPreviewInfo(TS p_timeStamp,
                                             PreviewSource p_source,
                                             const QVector<QSharedPointer<VImageToPreview> > &p_images,
                                             OrderedIntSet &p_affectedBlocks)
{
    for (auto const & img : p_images) {
        if (img.isNull()) {
            continue;
        }

        QTextBlock block = m_document->findBlockByNumber(img->m_blockNumber);
        if (!block.isValid()) {
            continue;
        }

        QString name = imageResourceNameForSource(p_source, img);
        if (name.isEmpty()) {
            continue;
        }

        VTextBlockData *blockData = static_cast<VTextBlockData *>(block.userData());
        if (!blockData) {
            continue;
        }

        VPreviewInfo *info = new VPreviewInfo(p_source,
                                              p_timeStamp,
                                              img->m_startPos - img->m_blockPos,
                                              img->m_endPos - img->m_blockPos,
                                              img->m_padding,
                                              !img->m_isBlock,
                                              name,
                                              m_editor->imageSize(name),
                                              img->m_background);
        bool tsUpdated = blockData->insertPreviewInfo(info);
        imageCache(p_source).insert(name, p_timeStamp);
        if (!tsUpdated) {
            // No need to relayout the block if only timestamp is updated.
            p_affectedBlocks.insert(img->m_blockNumber, QMapDummyValue());
            m_highlighter->addPossiblePreviewBlock(img->m_blockNumber);
        }
    }
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
                                                    PreviewSource p_source,
                                                    OrderedIntSet &p_affectedBlocks)
{
    QVector<int> obsoleteBlocks;
    const QSet<int> &blocks = m_highlighter->getPossiblePreviewBlocks();
    for (auto i : blocks) {
        QTextBlock block = m_document->findBlockByNumber(i);
        if (!block.isValid()) {
            obsoleteBlocks.append(i);
            continue;
        }

        VTextBlockData *blockData = static_cast<VTextBlockData *>(block.userData());
        if (!blockData) {
            continue;
        }

        if (blockData->clearObsoletePreview(p_timeStamp, p_source)) {
            p_affectedBlocks.insert(i, QMapDummyValue());
        }

        if (blockData->getPreviews().isEmpty()) {
            obsoleteBlocks.append(i);
        }
    }

    m_highlighter->clearPossiblePreviewBlocks(obsoleteBlocks);
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

    OrderedIntSet affectedBlocks;

    updateBlockPreviewInfo(ts, PreviewSource::CodeBlock, p_images, affectedBlocks);

    clearBlockObsoletePreviewInfo(ts, PreviewSource::CodeBlock, affectedBlocks);

    clearObsoleteImages(ts, PreviewSource::CodeBlock);

    relayout(affectedBlocks);
}

void VPreviewManager::updateMathjaxBlocks(const QVector<QSharedPointer<VImageToPreview> > &p_images)
{
    if (!m_previewEnabled) {
        return;
    }

    TS ts = ++timeStamp(PreviewSource::MathjaxBlock);

    OrderedIntSet affectedBlocks;

    updateBlockPreviewInfo(ts, PreviewSource::MathjaxBlock, p_images, affectedBlocks);

    clearBlockObsoletePreviewInfo(ts, PreviewSource::MathjaxBlock, affectedBlocks);

    clearObsoleteImages(ts, PreviewSource::MathjaxBlock);

    relayout(affectedBlocks);
}

void VPreviewManager::checkBlocksForObsoletePreview(const QList<int> &p_blocks)
{
    if (p_blocks.isEmpty()) {
        return;
    }

    OrderedIntSet affectedBlocks;
    for (auto blockNum : p_blocks) {
        QTextBlock block = m_document->findBlockByNumber(blockNum);
        if (!block.isValid()) {
            continue;
        }

        VTextBlockData *blockData = static_cast<VTextBlockData *>(block.userData());
        if (!blockData) {
            continue;
        }

        if (blockData->getPreviews().isEmpty()) {
            continue;
        }

        for (int i = 0; i < (int)PreviewSource::MaxNumberOfSources; ++i) {
            if (blockData->getPreviews().isEmpty()) {
                break;
            }

            PreviewSource ps = static_cast<PreviewSource>(i);
            if (blockData->clearObsoletePreview(timeStamp(ps), ps)) {
                affectedBlocks.insert(blockNum, QMapDummyValue());
            }
        }
    }

    relayout(affectedBlocks);
}

void VPreviewManager::relayoutEditor(const OrderedIntSet &p_blocks)
{
    OrderedIntSet bs(p_blocks);
    int first, last;
    m_editor->visibleBlockRange(first, last);
    for (int i = first; i <= last; ++i) {
        bs.insert(i, QMapDummyValue());
    }

    m_editor->relayout(bs);

    // We may get the wrong visible block range before relayout() updating the document size.
    OrderedIntSet after;
    int afterFirst = m_editor->firstVisibleBlockNumber();
    for (int i = afterFirst; i < first; ++i) {
        if (!bs.contains(i)) {
            after.insert(i, QMapDummyValue());
        }
    }

    m_editor->relayout(after);
}
