#include <QtWidgets>
#include "vmdedit.h"
#include "hgmarkdownhighlighter.h"
#include "vmdeditoperations.h"
#include "vnote.h"
#include "vconfigmanager.h"
#include "vtoc.h"
#include "utils/vutils.h"

extern VConfigManager vconfig;

enum ImageProperty { ImagePath = 1 };

VMdEdit::VMdEdit(VFile *p_file, QWidget *p_parent)
    : VEdit(p_file, p_parent), m_mdHighlighter(NULL)
{
    Q_ASSERT(p_file->getDocType() == DocType::Markdown);

    setAcceptRichText(false);
    m_mdHighlighter = new HGMarkdownHighlighter(vconfig.getMdHighlightingStyles(),
                                                500, document());
    connect(m_mdHighlighter, &HGMarkdownHighlighter::highlightCompleted,
            this, &VMdEdit::generateEditOutline);
    connect(m_mdHighlighter, &HGMarkdownHighlighter::imageBlocksUpdated,
            this, &VMdEdit::updateImageBlocks);
    m_editOps = new VMdEditOperations(this, m_file);

    connect(this, &VMdEdit::cursorPositionChanged,
            this, &VMdEdit::updateCurHeader);

    m_editOps->updateTabSettings();
    updateFontAndPalette();
}

void VMdEdit::updateFontAndPalette()
{
    setFont(vconfig.getMdEditFont());
    setPalette(vconfig.getMdEditPalette());
}

void VMdEdit::beginEdit()
{
    m_editOps->updateTabSettings();
    updateFontAndPalette();

    setFont(vconfig.getMdEditFont());

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
    QString &content = m_file->getContent();
    Q_ASSERT(content.indexOf(QChar::ObjectReplacementCharacter) == -1);
    setPlainText(content);
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
    if (source->hasImage()) {
        // Image data in the clipboard
        bool ret = m_editOps->insertImageFromMimeData(source);
        if (ret) {
            return;
        }
    } else if (source->hasUrls()) {
        // Paste an image file
        bool ret = m_editOps->insertURLFromMimeData(source);
        if (ret) {
            return;
        }
    }
    VEdit::insertFromMimeData(source);
}

void VMdEdit::imageInserted(const QString &p_name)
{
    m_insertedImages.append(p_name);
}

void VMdEdit::initInitImages()
{
    m_initImages = VUtils::imagesFromMarkdownFile(m_file->retrivePath());
}

void VMdEdit::clearUnusedImages()
{
    QVector<QString> images = VUtils::imagesFromMarkdownFile(m_file->retrivePath());

    if (!m_insertedImages.isEmpty()) {
        QVector<QString> imageNames(images.size());
        for (int i = 0; i < imageNames.size(); ++i) {
            imageNames[i] = VUtils::fileNameFromPath(images[i]);
        }

        QDir dir = QDir(m_file->retriveImagePath());
        for (int i = 0; i < m_insertedImages.size(); ++i) {
            QString name = m_insertedImages[i];
            int j;
            for (j = 0; j < imageNames.size(); ++j) {
                if (name == imageNames[j]) {
                    break;
                }
            }

            // Delete it
            if (j == imageNames.size()) {
                QString imagePath = dir.filePath(name);
                QFile(imagePath).remove();
                qDebug() << "delete inserted image" << imagePath;
            }
        }
        m_insertedImages.clear();
    }

    for (int i = 0; i < m_initImages.size(); ++i) {
        QString imagePath = m_initImages[i];
        int j;
        for (j = 0; j < images.size(); ++j) {
            if (imagePath == images[j]) {
                break;
            }
        }

        // Delete it
        if (j == images.size()) {
            QFile(imagePath).remove();
            qDebug() << "delete existing image" << imagePath;
        }
    }
    m_initImages.clear();
}

void VMdEdit::updateCurHeader()
{
    int curHeader = 0;
    QTextCursor cursor(this->textCursor());
    int curLine = cursor.block().firstLineNumber();
    int i = 0;
    for (i = m_headers.size() - 1; i >= 0; --i) {
        if (m_headers[i].lineNumber <= curLine) {
            curHeader = m_headers[i].lineNumber;
            break;
        }
    }
    emit curHeaderChanged(curHeader, i == -1 ? 0 : i);
}

void VMdEdit::generateEditOutline()
{
    QTextDocument *doc = document();
    m_headers.clear();
    // Assume that each block contains only one line
    // Only support # syntax for now
    QRegExp headerReg("(#{1,6})\\s*(\\S.*)");  // Need to trim the spaces
    for (QTextBlock block = doc->begin(); block != doc->end(); block = block.next()) {
        Q_ASSERT(block.lineCount() == 1);
        if ((block.userState() == HighlightBlockState::BlockNormal) &&
            headerReg.exactMatch(block.text())) {
            VHeader header(headerReg.cap(1).length(),
                           headerReg.cap(2).trimmed(), "", block.firstLineNumber());
            m_headers.append(header);
        }
    }

    emit headersChanged(m_headers);
    updateCurHeader();
}

void VMdEdit::scrollToHeader(int p_headerIndex)
{
    Q_ASSERT(p_headerIndex >= 0);
    if (p_headerIndex < m_headers.size()) {
        int line = m_headers[p_headerIndex].lineNumber;
        qDebug() << "scroll editor to" << p_headerIndex << "line" << line;
        scrollToLine(line);
    }
}

void VMdEdit::updateImageBlocks(QSet<int> p_imageBlocks)
{
    // We need to handle blocks backward to avoid shifting all the following blocks.
    // Inserting the preview image block may cause highlighter to emit signal again.
    QList<int> blockList = p_imageBlocks.toList();
    std::sort(blockList.begin(), blockList.end(), std::greater<int>());
    auto it = blockList.begin();
    while (it != blockList.end()) {
        previewImageOfBlock(*it);
        ++it;
    }

    // Clean up un-referenced QChar::ObjectReplacementCharacter.
    clearOrphanImagePreviewBlock();

    emit statusChanged();
}

void VMdEdit::clearOrphanImagePreviewBlock()
{
    QTextDocument *doc = document();
    QTextBlock block = doc->begin();
    while (block.isValid()) {
        if (isOrphanImagePreviewBlock(block)) {
            qDebug() << "remove orphan image preview block" << block.blockNumber();
            QTextBlock nextBlock = block.next();
            removeBlock(block);
            block = nextBlock;
        } else {
            clearCorruptedImagePreviewBlock(block);
            block = block.next();
        }
    }
}

bool VMdEdit::isOrphanImagePreviewBlock(QTextBlock p_block)
{
    if (isImagePreviewBlock(p_block)) {
        // It is an orphan image preview block if previous block is not
        // a block need to preview (containing exactly one image).
        QTextBlock prevBlock = p_block.previous();
        if (prevBlock.isValid()) {
            if (fetchImageToPreview(prevBlock.text()).isEmpty()) {
                return true;
            } else {
                return false;
            }
        } else {
            return true;
        }
    }
    return false;
}

void VMdEdit::clearCorruptedImagePreviewBlock(QTextBlock p_block)
{
    if (!p_block.isValid()) {
        return;
    }
    QString text = p_block.text();
    QVector<int> replacementChars;
    bool onlySpaces = true;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QChar::ObjectReplacementCharacter) {
            replacementChars.append(i);
        } else if (!text[i].isSpace()) {
            onlySpaces = false;
        }
    }
    if (!onlySpaces && !replacementChars.isEmpty()) {
        // ObjectReplacementCharacter mixed with other non-space texts.
        // Users corrupt the image preview block. Just remove the char.
        QTextCursor cursor(p_block);
        int blockPos = p_block.position();
        for (int i = replacementChars.size() - 1; i >= 0; --i) {
            int pos = replacementChars[i];
            cursor.setPosition(blockPos + pos);
            cursor.deleteChar();
        }
        Q_ASSERT(text.remove(QChar::ObjectReplacementCharacter) == p_block.text());
    }
}

QString VMdEdit::fetchImageToPreview(const QString &p_text)
{
    QRegExp regExp("\\!\\[[^\\]]*\\]\\((images/[^/\\)]+)\\)");
    int index = regExp.indexIn(p_text);
    if (index == -1) {
        return QString();
    }
    int lastIndex = regExp.lastIndexIn(p_text);
    if (lastIndex != index) {
        return QString();
    }
    return regExp.capturedTexts()[1];
}

void VMdEdit::previewImageOfBlock(int p_block)
{
    QTextDocument *doc = document();
    QTextBlock block = doc->findBlockByNumber(p_block);
    if (!block.isValid()) {
        return;
    }

    QString text = block.text();
    QString imageLink = fetchImageToPreview(text);
    if (imageLink.isEmpty()) {
        return;
    }
    QString imagePath = QDir(m_file->retriveBasePath()).filePath(imageLink);
    qDebug() << "block" << p_block << "image" << imagePath;

    if (isImagePreviewBlock(p_block + 1)) {
        updateImagePreviewBlock(p_block + 1, imagePath);
        return;
    }
    insertImagePreviewBlock(p_block, imagePath);
}

bool VMdEdit::isImagePreviewBlock(int p_block)
{
    QTextDocument *doc = document();
    QTextBlock block = doc->findBlockByNumber(p_block);
    if (!block.isValid()) {
        return false;
    }
    QString text = block.text().trimmed();
    return text == QString(QChar::ObjectReplacementCharacter);
}

bool VMdEdit::isImagePreviewBlock(QTextBlock p_block)
{
    if (!p_block.isValid()) {
        return false;
    }
    QString text = p_block.text().trimmed();
    return text == QString(QChar::ObjectReplacementCharacter);
}

void VMdEdit::insertImagePreviewBlock(int p_block, const QString &p_image)
{
    QTextDocument *doc = document();

    QImage image(p_image);
    if (image.isNull()) {
        return;
    }

    // Store current status.
    bool modified = isModified();
    int pos = textCursor().position();

    QTextCursor cursor(doc->findBlockByNumber(p_block));
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertBlock();

    QTextImageFormat imgFormat;
    imgFormat.setName(p_image);
    imgFormat.setProperty(ImagePath, p_image);
    cursor.insertImage(imgFormat);
    Q_ASSERT(cursor.block().text().at(0) == QChar::ObjectReplacementCharacter);
    cursor.endEditBlock();

    QTextCursor tmp = textCursor();
    tmp.setPosition(pos);
    setTextCursor(tmp);
    setModified(modified);
    emit statusChanged();
}

void VMdEdit::updateImagePreviewBlock(int p_block, const QString &p_image)
{
    Q_ASSERT(isImagePreviewBlock(p_block));
    QTextDocument *doc = document();
    QTextBlock block = doc->findBlockByNumber(p_block);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(block);
    int shift = block.text().indexOf(QChar::ObjectReplacementCharacter);
    if (shift > 0) {
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, shift + 1);
    }
    QTextImageFormat format = cursor.charFormat().toImageFormat();
    Q_ASSERT(format.isValid());
    QString curPath = format.property(ImagePath).toString();

    if (curPath == p_image) {
        return;
    }
    // Update it with the new image.
    QImage image(p_image);
    if (image.isNull()) {
        // Delete current preview block.
        removeBlock(block);
        qDebug() << "remove invalid image in block" << p_block;
        return;
    }
    format.setName(p_image);
    qDebug() << "update block" << p_block << "to image" << p_image;
}

void VMdEdit::removeBlock(QTextBlock p_block)
{
    QTextCursor cursor(p_block);
    cursor.select(QTextCursor::BlockUnderCursor);
    cursor.removeSelectedText();
}

QString VMdEdit::toPlainTextWithoutImg() const
{
    QString text = toPlainText();
    int start = 0;
    do {
        int index = text.indexOf(QChar::ObjectReplacementCharacter, start);
        if (index == -1) {
            break;
        }
        start = removeObjectReplacementLine(text, index);
    } while (start < text.size());
    return text;
}

int VMdEdit::removeObjectReplacementLine(QString &p_text, int p_index) const
{
    Q_ASSERT(p_text.size() > p_index && p_text.at(p_index) == QChar::ObjectReplacementCharacter);
    Q_ASSERT(p_text.at(p_index + 1) == '\n');
    int prevLineIdx = p_text.lastIndexOf('\n', p_index);
    if (prevLineIdx == -1) {
        prevLineIdx = 0;
    }
    // Remove \n[....?\n]
    p_text.remove(prevLineIdx + 1, p_index - prevLineIdx + 1);
    return prevLineIdx;
}
