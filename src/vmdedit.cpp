#include <QtWidgets>
#include "vmdedit.h"
#include "hgmarkdownhighlighter.h"
#include "vcodeblockhighlighthelper.h"
#include "vmdeditoperations.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "utils/vpreviewutils.h"
#include "dialog/vselectdialog.h"
#include "vimagepreviewer.h"
#include "vtextblockdata.h"

extern VConfigManager *g_config;
extern VNote *g_vnote;

VMdEdit::VMdEdit(VFile *p_file, VDocument *p_vdoc, MarkdownConverterType p_type,
                 QWidget *p_parent)
    : VEdit(p_file, p_parent), m_mdHighlighter(NULL)
{
    V_ASSERT(p_file->getDocType() == DocType::Markdown);

    setAcceptRichText(false);
    m_mdHighlighter = new HGMarkdownHighlighter(g_config->getMdHighlightingStyles(),
                                                g_config->getCodeBlockStyles(),
                                                g_config->getMarkdownHighlightInterval(),
                                                document());
    connect(m_mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
            this, &VMdEdit::generateEditOutline);

    // After highlight, the cursor may trun into non-visible. We should make it visible
    // in this case.
    connect(m_mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
            this, [this]() {
            makeBlockVisible(textCursor().block());
    });

    m_cbHighlighter = new VCodeBlockHighlightHelper(m_mdHighlighter, p_vdoc,
                                                    p_type);

    m_imagePreviewer = new VImagePreviewer(this);
    connect(m_mdHighlighter, &HGMarkdownHighlighter::imageLinksUpdated,
            m_imagePreviewer, &VImagePreviewer::imageLinksChanged);
    connect(m_imagePreviewer, &VImagePreviewer::requestUpdateImageLinks,
            m_mdHighlighter, &HGMarkdownHighlighter::updateHighlight);

    m_editOps = new VMdEditOperations(this, m_file);

    connect(m_editOps, &VEditOperations::statusMessage,
            this, &VEdit::statusMessage);
    connect(m_editOps, &VEditOperations::vimStatusUpdated,
            this, &VEdit::vimStatusUpdated);

    connect(this, &VMdEdit::cursorPositionChanged,
            this, &VMdEdit::updateCurHeader);

    connect(QApplication::clipboard(), &QClipboard::changed,
            this, &VMdEdit::handleClipboardChanged);

    updateFontAndPalette();

    updateConfig();
}

void VMdEdit::updateFontAndPalette()
{
    setFont(g_config->getMdEditFont());
    setPalette(g_config->getMdEditPalette());
}

void VMdEdit::beginEdit()
{
    updateFontAndPalette();

    updateConfig();

    Q_ASSERT(m_file->getContent() == toPlainTextWithoutImg());

    initInitImages();

    setReadOnly(false);
    setModified(false);

    // Request update outline.
    generateEditOutline();
}

void VMdEdit::endEdit()
{
    setReadOnly(true);
    clearUnusedImages();
}

void VMdEdit::saveFile()
{
    if (!document()->isModified()) {
        return;
    }

    m_file->setContent(toPlainTextWithoutImg());
    document()->setModified(false);
}

void VMdEdit::reloadFile()
{
    const QString &content = m_file->getContent();
    Q_ASSERT(content.indexOf(QChar::ObjectReplacementCharacter) == -1);

    setPlainText(content);

    setBlockLineDistanceHeight();

    setModified(false);
}

void VMdEdit::keyPressEvent(QKeyEvent *event)
{
    if (m_editOps->handleKeyPressEvent(event)) {
        return;
    }
    VEdit::keyPressEvent(event);
}

bool VMdEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasImage() || source->hasUrls()
           || VEdit::canInsertFromMimeData(source);
}

void VMdEdit::insertFromMimeData(const QMimeData *source)
{
    VSelectDialog dialog(tr("Insert From Clipboard"), this);
    dialog.addSelection(tr("Insert As Image"), 0);
    dialog.addSelection(tr("Insert As Text"), 1);

    if (source->hasImage()) {
        // Image data in the clipboard
        if (source->hasText()) {
            if (dialog.exec() == QDialog::Accepted) {
                if (dialog.getSelection() == 1) {
                    // Insert as text.
                    Q_ASSERT(source->hasText() && source->hasImage());
                    VEdit::insertFromMimeData(source);
                    return;
                }
            } else {
                return;
            }
        }
        m_editOps->insertImageFromMimeData(source);
        return;
    } else if (source->hasUrls()) {
        QList<QUrl> urls = source->urls();
        if (urls.size() == 1 && VUtils::isImageURL(urls[0])) {
            if (dialog.exec() == QDialog::Accepted) {
                // FIXME: After calling dialog.exec(), source->hasUrl() returns false.
                if (dialog.getSelection() == 0) {
                    // Insert as image.
                    m_editOps->insertImageFromURL(urls[0]);
                    return;
                }
                QMimeData newSource;
                newSource.setUrls(urls);
                VEdit::insertFromMimeData(&newSource);
                return;
            } else {
                return;
            }
        }
    } else if (source->hasText()) {
        QString text = source->text();
        if (VUtils::isImageURLText(text)) {
            // The text is a URL to an image.
            if (dialog.exec() == QDialog::Accepted) {
                if (dialog.getSelection() == 0) {
                    // Insert as image.
                    QUrl url(text);
                    if (url.isValid()) {
                        m_editOps->insertImageFromURL(QUrl(text));
                    }
                    return;
                }
            } else {
                return;
            }
        }
        Q_ASSERT(source->hasText());
    }
    VEdit::insertFromMimeData(source);
}

void VMdEdit::imageInserted(const QString &p_path)
{
    ImageLink link;
    link.m_path = p_path;
    if (m_file->isRelativeImageFolder()) {
        link.m_type = ImageLink::LocalRelativeInternal;
    } else {
        link.m_type = ImageLink::LocalAbsolute;
    }

    m_insertedImages.append(link);
}

void VMdEdit::initInitImages()
{
    m_initImages = VUtils::fetchImagesFromMarkdownFile(m_file,
                                                       ImageLink::LocalRelativeInternal);
}

void VMdEdit::clearUnusedImages()
{
    QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(m_file,
                                                                    ImageLink::LocalRelativeInternal);

    if (!m_insertedImages.isEmpty()) {
        for (int i = 0; i < m_insertedImages.size(); ++i) {
            const ImageLink &link = m_insertedImages[i];

            if (link.m_type != ImageLink::LocalRelativeInternal) {
                continue;
            }

            int j;
            for (j = 0; j < images.size(); ++j) {
                if (VUtils::equalPath(link.m_path, images[j].m_path)) {
                    break;
                }
            }

            // This inserted image is no longer in the file.
            if (j == images.size()) {
                if (!QFile(link.m_path).remove()) {
                    qWarning() << "fail to delete unused inserted image" << link.m_path;
                } else {
                    qDebug() << "delete unused inserted image" << link.m_path;
                }
            }
        }

        m_insertedImages.clear();
    }

    for (int i = 0; i < m_initImages.size(); ++i) {
        const ImageLink &link = m_initImages[i];

        V_ASSERT(link.m_type == ImageLink::LocalRelativeInternal);

        int j;
        for (j = 0; j < images.size(); ++j) {
            if (VUtils::equalPath(link.m_path, images[j].m_path)) {
                break;
            }
        }

        // Original local relative image is no longer in the file.
        if (j == images.size()) {
            if (!QFile(link.m_path).remove()) {
                qWarning() << "fail to delete unused original image" << link.m_path;
            } else {
                qDebug() << "delete unused original image" << link.m_path;
            }
        }
    }

    m_initImages.clear();
}

int VMdEdit::currentCursorHeader() const
{
    if (m_headers.isEmpty()) {
        return -1;
    }

    int curLine = textCursor().block().firstLineNumber();
    int i = 0;
    for (i = m_headers.size() - 1; i >= 0; --i) {
        if (!m_headers[i].isEmpty()) {
            if (m_headers[i].lineNumber <= curLine) {
                break;
            }
        }
    }

    if (i == -1) {
        return -1;
    } else {
        Q_ASSERT(m_headers[i].index == i);
        return i;
    }
}

void VMdEdit::updateCurHeader()
{
    if (m_headers.isEmpty()) {
        return;
    }

    int idx = currentCursorHeader();
    if (idx == -1) {
        emit curHeaderChanged(VAnchor(m_file, "", -1, -1));
        return;
    }

    emit curHeaderChanged(VAnchor(m_file, "", m_headers[idx].lineNumber, m_headers[idx].index));
}

void VMdEdit::generateEditOutline()
{
    QTextDocument *doc = document();

    QVector<VHeader> headers;

    // Assume that each block contains only one line
    // Only support # syntax for now
    QRegExp headerReg("(#{1,6})\\s+(\\S.*)");  // Need to trim the spaces
    int baseLevel = -1;
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        V_ASSERT(block.lineCount() == 1);
        if ((block.userState() == HighlightBlockState::Normal) &&
            headerReg.exactMatch(block.text())) {
            int level = headerReg.cap(1).length();
            VHeader header(level, headerReg.cap(2).trimmed(),
                           "", block.firstLineNumber(), headers.size());
            headers.append(header);

            if (baseLevel == -1) {
                baseLevel = level;
            } else if (baseLevel > level) {
                baseLevel = level;
            }
        }
    }

    m_headers.clear();

    int curLevel = baseLevel - 1;
    for (auto & item : headers) {
        while (item.level > curLevel + 1) {
            curLevel += 1;

            // Insert empty level which is an invalid header.
            m_headers.append(VHeader(curLevel, c_emptyHeaderName, "", -1, m_headers.size()));
        }

        item.index = m_headers.size();
        m_headers.append(item);
        curLevel = item.level;
    }

    emit headersChanged(m_headers);

    updateCurHeader();
}

void VMdEdit::scrollToHeader(const VAnchor &p_anchor)
{
    if (p_anchor.lineNumber == -1
        || p_anchor.m_outlineIndex < 0) {
        // Move to the start of document if m_headers is not empty.
        // Otherwise, there is no outline, so just let it be.
        if (!m_headers.isEmpty()) {
            moveCursor(QTextCursor::Start);
        }

        return;
    } else if (p_anchor.m_outlineIndex >= m_headers.size()) {
        return;
    }

    scrollToLine(p_anchor.lineNumber);
}

QString VMdEdit::toPlainTextWithoutImg()
{
    QString text;
    bool readOnly = isReadOnly();
    setReadOnly(true);
    text = getPlainTextWithoutPreviewImage();
    setReadOnly(readOnly);

    return text;
}

QString VMdEdit::getPlainTextWithoutPreviewImage() const
{
    QVector<Region> deletions;

    while (true) {
        deletions.clear();

        while (m_imagePreviewer->isPreviewing()) {
            VUtils::sleepWait(100);
        }

        // Iterate all the block to get positions for deletion.
        QTextBlock block = document()->begin();
        bool tryAgain = false;
        while (block.isValid()) {
            if (VTextBlockData::containsPreviewImage(block)) {
                if (!getPreviewImageRegionOfBlock(block, deletions)) {
                    tryAgain = true;
                    break;
                }
            }

            block = block.next();
        }

        if (tryAgain) {
            continue;
        }

        QString text = toPlainText();
        // deletions is sorted by m_startPos.
        // From back to front.
        for (int i = deletions.size() - 1; i >= 0; --i) {
            const Region &reg = deletions[i];
            qDebug() << "img region to delete" << reg.m_startPos << reg.m_endPos;
            text.remove(reg.m_startPos, reg.m_endPos - reg.m_startPos);
        }

        return text;
    }
}

bool VMdEdit::getPreviewImageRegionOfBlock(const QTextBlock &p_block,
                                           QVector<Region> &p_regions) const
{
    QTextDocument *doc = document();
    QVector<Region> regs;
    QString text = p_block.text();
    int nrOtherChar = 0;
    int nrImage = 0;
    bool hasBlock = false;

    // From back to front.
    for (int i = text.size() - 1; i >= 0; --i) {
        if (text[i].isSpace()) {
            continue;
        }

        if (text[i] == QChar::ObjectReplacementCharacter) {
            int pos = p_block.position() + i;
            Q_ASSERT(doc->characterAt(pos) == QChar::ObjectReplacementCharacter);

            QTextImageFormat imageFormat = VPreviewUtils::fetchFormatFromPosition(doc, pos);
            if (imageFormat.isValid()) {
                ++nrImage;
                bool isBlock = VPreviewUtils::getPreviewImageType(imageFormat) == PreviewImageType::Block;
                if (isBlock) {
                    hasBlock = true;
                } else {
                    regs.push_back(Region(pos, pos + 1));
                }
            } else {
                return false;
            }
        } else {
            ++nrOtherChar;
        }
    }

    if (hasBlock) {
        if (nrOtherChar > 0 || nrImage > 1) {
            // Inconsistent state.
            return false;
        }

        regs.push_back(Region(p_block.position(), p_block.position() + p_block.length()));
    }

    p_regions.append(regs);
    return true;
}

void VMdEdit::handleClipboardChanged(QClipboard::Mode p_mode)
{
    if (!hasFocus()) {
        return;
    }

    if (p_mode == QClipboard::Clipboard) {
        QClipboard *clipboard = QApplication::clipboard();
        const QMimeData *mimeData = clipboard->mimeData();
        if (mimeData->hasText()) {
            QString text = mimeData->text();
            if (clipboard->ownsClipboard()) {
                if (text.trimmed() == QString(QChar::ObjectReplacementCharacter)) {
                    QImage image = tryGetSelectedImage();
                    clipboard->clear(QClipboard::Clipboard);
                    if (!image.isNull()) {
                        clipboard->setImage(image, QClipboard::Clipboard);
                    }
                } else {
                    // Try to remove all the preview image in text.
                    VEditUtils::removeObjectReplacementCharacter(text);
                    if (text.size() != mimeData->text().size()) {
                        clipboard->clear(QClipboard::Clipboard);
                        clipboard->setText(text);
                    }
                }
            }
        }
    }
}

QImage VMdEdit::tryGetSelectedImage()
{
    QImage image;
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        return image;
    }
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    QTextDocument *doc = document();
    QTextImageFormat format;
    for (int i = start; i < end; ++i) {
        if (doc->characterAt(i) == QChar::ObjectReplacementCharacter) {
            format = VPreviewUtils::fetchFormatFromPosition(doc, i);
            break;
        }
    }

    if (format.isValid()) {
        PreviewImageSource src = VPreviewUtils::getPreviewImageSource(format);
        long long id = VPreviewUtils::getPreviewImageID(format);
        if (src == PreviewImageSource::Image) {
            Q_ASSERT(m_imagePreviewer->isEnabled());
            image = m_imagePreviewer->fetchCachedImageByID(id);
        }
    }

    return image;
}

void VMdEdit::resizeEvent(QResizeEvent *p_event)
{
    m_imagePreviewer->updatePreviewImageWidth();

    VEdit::resizeEvent(p_event);
}

const QVector<VHeader> &VMdEdit::getHeaders() const
{
    return m_headers;
}

bool VMdEdit::jumpTitle(bool p_forward, int p_relativeLevel, int p_repeat)
{
    if (m_headers.isEmpty()) {
        return false;
    }

    QTextCursor cursor = textCursor();
    int cursorLine = cursor.block().firstLineNumber();
    int targetIdx = -1;
    // -1: skip level check.
    int targetLevel = 0;
    int idx = currentCursorHeader();
    if (idx == -1) {
        // Cursor locates at the beginning, before any headers.
        if (p_relativeLevel < 0 || !p_forward) {
            return false;
        }
    }

    int delta = 1;
    if (!p_forward) {
        delta = -1;
    }

    bool firstHeader = true;
    for (targetIdx = idx == -1 ? 0 : idx;
         targetIdx >= 0 && targetIdx < m_headers.size();
         targetIdx += delta) {
        const VHeader &header = m_headers[targetIdx];
        if (header.isEmpty()) {
            continue;
        }

        if (targetLevel == 0) {
            // The target level has not been init yet.
            Q_ASSERT(firstHeader);
            targetLevel = header.level;
            if (p_relativeLevel < 0) {
                targetLevel += p_relativeLevel;
                if (targetLevel < 1) {
                    // Invalid level.
                    return false;
                }
            } else if (p_relativeLevel > 0) {
                targetLevel = -1;
            }
        }

        if (targetLevel == -1 || header.level == targetLevel) {
            if (firstHeader
                && (cursorLine == header.lineNumber
                    || p_forward)
                && idx != -1) {
                // This header is not counted for the repeat.
                firstHeader = false;
                continue;
            }

            if (--p_repeat == 0) {
                // Found.
                break;
            }
        } else if (header.level < targetLevel) {
            // Stop by higher level.
            return false;
        }

        firstHeader = false;
    }

    if (targetIdx < 0 || targetIdx >= m_headers.size()) {
        return false;
    }

    // Jump to target header.
    int line = m_headers[targetIdx].lineNumber;
    if (line > -1) {
        QTextBlock block = document()->findBlockByLineNumber(line);
        if (block.isValid()) {
            cursor.setPosition(block.position());
            setTextCursor(cursor);
            return true;
        }
    }

    return false;
}
