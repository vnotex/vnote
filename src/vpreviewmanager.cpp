#include "vpreviewmanager.h"

#include <QTextDocument>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QVector>
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vdownloader.h"
#include "hgmarkdownhighlighter.h"
#include "vtextblockdata.h"

extern VConfigManager *g_config;

VPreviewManager::VPreviewManager(VMdEditor *p_editor)
    : QObject(p_editor),
      m_editor(p_editor),
      m_previewEnabled(false)
{
    m_blockImageInfo.resize(PreviewSource::Invalid);

    m_downloader = new VDownloader(this);
    connect(m_downloader, &VDownloader::downloadFinished,
            this, &VPreviewManager::imageDownloaded);
}

void VPreviewManager::imageLinksUpdated(const QVector<VElementRegion> &p_imageRegions)
{
    if (!m_previewEnabled) {
        return;
    }

    m_imageRegions = p_imageRegions;

    previewImages();
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

        if (!m_previewEnabled) {
            clearPreview();
        }
    }
}

void VPreviewManager::clearPreview()
{
    for (int i = 0; i < m_blockImageInfo.size(); ++i) {
        m_blockImageInfo[i].clear();
    }

    updateEditorBlockImages();
}

void VPreviewManager::previewImages()
{
    QVector<ImageLinkInfo> imageLinks;
    fetchImageLinksFromRegions(imageLinks);

    updateBlockImageInfo(imageLinks);

    updateEditorBlockImages();
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

void VPreviewManager::fetchImageLinksFromRegions(QVector<ImageLinkInfo> &p_imageLinks)
{
    p_imageLinks.clear();

    if (m_imageRegions.isEmpty()) {
        return;
    }

    p_imageLinks.reserve(m_imageRegions.size());

    QTextDocument *doc = m_editor->document();

    for (int i = 0; i < m_imageRegions.size(); ++i) {
        VElementRegion &reg = m_imageRegions[i];
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
                           calculateBlockMargin(block));
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

void VPreviewManager::updateBlockImageInfo(const QVector<ImageLinkInfo> &p_imageLinks)
{
    QVector<VBlockImageInfo2> &blockInfos = m_blockImageInfo[PreviewSource::ImageLink];
    blockInfos.clear();

    for (int i = 0; i < p_imageLinks.size(); ++i) {
        const ImageLinkInfo &link = p_imageLinks[i];

        QString name = imageResourceName(link);
        if (name.isEmpty()) {
            continue;
        }

        VBlockImageInfo2 info(link.m_blockNumber,
                              name,
                              link.m_startPos - link.m_blockPos,
                              link.m_endPos - link.m_blockPos,
                              link.m_padding,
                              !link.m_isBlock);

        blockInfos.push_back(info);
    }
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

void VPreviewManager::updateEditorBlockImages()
{
    // TODO: need to combine all preview sources.
    Q_ASSERT(m_blockImageInfo.size() == 1);

    m_editor->updateBlockImages(m_blockImageInfo[PreviewSource::ImageLink]);
}

int VPreviewManager::calculateBlockMargin(const QTextBlock &p_block)
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
            nrSpaces += m_editor->tabStopWidth();
        }
    }

    if (nrSpaces == 0) {
        return 0;
    }

    int spaceWidth = 0;
    QFont font = p_block.charFormat().font();
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
